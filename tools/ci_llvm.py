#!/usr/bin/env python3
"""Install the newest stable upstream LLVM release for a CI native lane.

CI prefers the latest stable compiler over whatever the hosted image ships.
The release is resolved from the llvm/llvm-project GitHub releases at run time:
the highest `llvmorg-X.Y.Z` release that is neither a draft nor a prerelease
and already publishes this target's archive. The archive is verified against
the SHA-256 digest and size GitHub records for that asset before anything is
extracted, and only the compiler, linker, archiver, and Clang resource tree are
written, so the multi-gigabyte distribution never lands on the runner disk.
Linux additionally stages a pinned, verified ICU package privately for LLD.

LLVM releases older than MINIMUM_VERSION are rejected: 22.1.0 is the first
stable release with the AVX10 host-detection correction (issue #1501).
BUSTER_CI_LLVM_VERSION pins an exact release when reproducing a past run.
macOS is intentionally absent; its Clang lane is Xcode's AppleClang.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request

RELEASES_URL = "https://api.github.com/repos/llvm/llvm-project/releases?per_page=100"
MINIMUM_VERSION = (22, 1, 0)
TAG_PATTERN = re.compile(r"llvmorg-([0-9]+)\.([0-9]+)\.([0-9]+)")
ASSET_STEMS = {
    "x86_64-linux": "LLVM-{version}-Linux-X64",
    "aarch64-linux": "LLVM-{version}-Linux-ARM64",
    "x86_64-windows": "clang+llvm-{version}-x86_64-pc-windows-msvc",
    "aarch64-windows": "clang+llvm-{version}-aarch64-pc-windows-msvc",
}
KEPT_TOOLS = frozenset((
    "clang", "clang++", "clang-cl", "clang-cpp",
    "lld", "ld.lld", "ld64.lld", "lld-link", "wasm-ld",
    "llvm-ar", "llvm-ranlib", "llvm-lib", "llvm-nm", "llvm-objcopy", "llvm-strip",
    "llvm-objdump", "llvm-readobj", "llvm-readelf", "llvm-symbolizer", "llvm-addr2line",
    "llvm-profdata", "llvm-cov", "llvm-rc", "llvm-mt", "llvm-dlltool", "llvm-dwarfdump",
))
DOWNLOAD_ATTEMPTS = 3
DOWNLOAD_TIMEOUT_SECONDS = 120
CHUNK_SIZE = 1024 * 1024
# Ubuntu Jammy's signed main/updates/security indexes, inspected in run
# 36838076717, publish these ICU 70.1-2 packages. LLVM 23.1.2's Linux LLD
# needs all three ICU 70 SONAMEs; its verified archives do not bundle them.
ICU_RUNTIME_ASSETS = {
    "x86_64-linux": {
        "name": "libicu70_70.1-2_amd64.deb", "size": 10581942,
        "digest": "sha256:58a154f6307289813da2276f900498ef536ae7c0522d2cf31a3c3c5cf62dfd9a",
        "browser_download_url": "https://archive.ubuntu.com/ubuntu/pool/main/i/icu/libicu70_70.1-2_amd64.deb",
    },
    "aarch64-linux": {
        "name": "libicu70_70.1-2_arm64.deb", "size": 10503646,
        "digest": "sha256:ac68372cf4a976e6a206858fd9b28c68e49d37d650b9b8653270038a6e7bc174",
        "browser_download_url": "https://ports.ubuntu.com/ubuntu-ports/pool/main/i/icu/libicu70_70.1-2_arm64.deb",
    },
}


def parse_version(tag):
    match = TAG_PATTERN.fullmatch(tag)
    return tuple(int(part) for part in match.groups()) if match else None


def format_version(version):
    return ".".join(str(part) for part in version)


def archive_formats(zstd_available):
    return ("tar.zst", "tar.xz") if zstd_available else ("tar.xz",)


def select_release(releases, target, formats, pinned=None):
    """Return (version, asset) for the newest stable release carrying this target."""
    if target not in ASSET_STEMS:
        raise ValueError(f"no upstream LLVM archive is defined for {target}")
    candidates = []
    for release in releases:
        version = parse_version(release.get("tag_name", ""))
        if version is None or release.get("draft") or release.get("prerelease"):
            continue
        if pinned is not None and version != pinned:
            continue
        assets = {asset.get("name"): asset for asset in release.get("assets", [])}
        stem = ASSET_STEMS[target].format(version=format_version(version))
        for extension in formats:
            asset = assets.get(f"{stem}.{extension}")
            if asset is not None:
                candidates.append((version, asset))
                break
    if not candidates:
        wanted = format_version(pinned) if pinned is not None else "any stable"
        raise ValueError(f"no {wanted} LLVM release publishes a {target} archive")
    version, asset = max(candidates, key=lambda candidate: candidate[0])
    if version < MINIMUM_VERSION:
        raise ValueError(f"LLVM {format_version(version)} predates the required "
                         f"{format_version(MINIMUM_VERSION)} AVX10 host-detection fix")
    digest = asset.get("digest") or ""
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError(f"{asset.get('name')} has no published SHA-256 digest")
    if type(asset.get("size")) is not int or asset["size"] <= 0:
        raise ValueError(f"{asset.get('name')} has no published size")
    return version, asset


def kept_member(name):
    """Map an archive member to its install-relative path, or None to skip it."""
    parts = PurePosixPath(name).parts
    if len(parts) < 2 or any(part in ("..", "") for part in parts) or name.startswith("/"):
        return None
    relative = PurePosixPath(*parts[1:])
    if relative.parts[:2] == ("lib", "clang") and len(relative.parts) > 2:
        return relative
    if len(relative.parts) == 2 and relative.parts[0] == "bin":
        tool = relative.name
        stem = tool[:-4] if tool.lower().endswith(".exe") else tool
        if tool.lower().endswith(".dll") or stem in KEPT_TOOLS or re.fullmatch(r"clang-[0-9]+", stem):
            return relative
    return None


def _request(url, token, accept):
    headers = {"Accept": accept, "User-Agent": "buster-ci-llvm"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return urllib.request.Request(url, headers=headers)


def fetch_releases(token):
    # The listing is fetched before any download, so a stalled API read would otherwise fail the whole lane.
    last_error = None
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(_request(RELEASES_URL, token, "application/vnd.github+json"),
                                        timeout=DOWNLOAD_TIMEOUT_SECONDS) as response:
                return json.load(response)
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            last_error = error
            print(f"LLVM release listing attempt {attempt} failed: {error}", file=sys.stderr)
            time.sleep(attempt * 5)
    raise RuntimeError(f"could not list LLVM releases: {last_error}")


def download(asset, destination, token):
    expected = asset["digest"].split(":", 1)[1]
    size = asset["size"]
    last_error = None
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            digest = hashlib.sha256()
            received = 0
            request = _request(asset["browser_download_url"], None, "application/octet-stream")
            with urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response, \
                    destination.open("wb") as output:
                while True:
                    chunk = response.read(CHUNK_SIZE)
                    if not chunk:
                        break
                    received += len(chunk)
                    if received > size:
                        raise ValueError(f"{asset['name']} exceeds its published size of {size} bytes")
                    digest.update(chunk)
                    output.write(chunk)
            if received != size:
                raise ValueError(f"{asset['name']} is {received} bytes; expected {size}")
            if digest.hexdigest() != expected:
                raise ValueError(f"{asset['name']} checksum mismatch: {digest.hexdigest()}; expected {expected}")
            return
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            last_error = error
            print(f"LLVM download attempt {attempt} failed: {error}", file=sys.stderr)
            time.sleep(attempt * 5)
    raise RuntimeError(f"could not download {asset['name']}: {last_error}")


def extract(archive_stream, install, mode):
    # Link targets are resolved, so the root must be too (macOS /var -> /private/var).
    install = install.resolve()
    links = []
    with tarfile.open(fileobj=archive_stream, mode=mode) as archive:
        for member in archive:
            relative = kept_member(member.name)
            if relative is None:
                continue
            target = install.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.issym() or member.islnk():
                links.append((target, member.linkname, member.issym()))
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                with target.open("wb") as output:
                    shutil.copyfileobj(source, output, CHUNK_SIZE)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)
    for target, linkname, symbolic in links:
        if symbolic:
            resolved = (target.parent / linkname).resolve()
        else:
            member_relative = kept_member(linkname)
            resolved = install.joinpath(*member_relative.parts) if member_relative else None
        if resolved is None or install not in resolved.parents or not resolved.is_file():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.symlink(os.path.relpath(resolved, target.parent), target)
        except OSError:
            shutil.copy2(resolved, target)


def unpack(archive, install):
    if archive.name.endswith(".tar.zst"):
        process = subprocess.Popen(["zstd", "-d", "-c", "--long=31", str(archive)], stdout=subprocess.PIPE)
        try:
            extract(process.stdout, install, "r|")
        finally:
            process.stdout.close()
            status = process.wait()
        if status != 0:
            raise RuntimeError(f"zstd failed with status {status}")
    else:
        with archive.open("rb") as stream:
            extract(stream, install, "r|xz")


def validate_tools(target, bin_directory, version):
    """Require the selected compiler, linker frontends and archiver to launch."""
    suffix = ".exe" if target.endswith("windows") else ""
    tools = ["clang", "llvm-ar", "lld-link" if target.endswith("windows") else "ld.lld"]
    tools.extend(tool for tool in ("ld.lld", "ld64.lld", "lld-link", "wasm-ld")
                 if tool not in tools and (bin_directory / (tool + suffix)).is_file())
    for tool in tools:
        executable = bin_directory / (tool + suffix)
        try:
            probe = subprocess.run([str(executable), "--version"], check=True,
                                   capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError) as error:
            diagnostic = getattr(error, "stderr", None) or str(error)
            raise RuntimeError(f"LLVM tool {executable} is not launchable: {diagnostic}") from error
        identity = probe.stdout.strip()
        print(f"LLVM_TOOL_READY tool={tool} path={executable}\n{identity}")
        if tool == "clang" and (not identity or
                                f"clang version {format_version(version)}" not in identity.splitlines()[0]):
            raise RuntimeError(f"installed clang does not report version {format_version(version)}")


def provision_linux_runtime(target, work, install, token):
    """Stage verified ICU 70 privately for the upstream LLD's ELF RUNPATH."""
    if target in ICU_RUNTIME_ASSETS:
        asset = ICU_RUNTIME_ASSETS[target]
        package = work / asset["name"]
        staging = work / "icu-runtime"
        if staging.exists():
            shutil.rmtree(staging)
        print(f"LLVM_RUNTIME package={asset['name']} size={asset['size']} digest={asset['digest']} "
              f"url={asset['browser_download_url']}")
        download(asset, package, token)
        subprocess.run(["dpkg-deb", "--extract", str(package), str(staging)], check=True, timeout=30)
        triplet = "x86_64-linux-gnu" if target == "x86_64-linux" else "aarch64-linux-gnu"
        source = staging / "usr" / "lib" / triplet
        # Preserve real ICU 70 SONAME links; never alias the host's newer ICU ABI.
        for name in ("libicui18n.so.70", "libicuuc.so.70", "libicudata.so.70"):
            if not (source / name).is_file():
                raise RuntimeError(f"verified ICU package is missing {name}")
        shutil.copytree(source, install / "lib", symlinks=True, dirs_exist_ok=True)
        notices = install / "share" / "licenses" / "icu70"
        notices.mkdir(parents=True, exist_ok=True)
        shutil.copy2(staging / "usr" / "share" / "doc" / "libicu70" / "copyright", notices / "copyright")
        package.unlink()
        shutil.rmtree(staging)


def install_llvm(target, work, releases, token, pinned=None):
    formats = archive_formats(shutil.which("zstd") is not None)
    version, asset = select_release(releases, target, formats, pinned)
    work.mkdir(parents=True, exist_ok=True)
    archive = work / asset["name"]
    install = work / f"llvm-{format_version(version)}"
    if install.exists():
        shutil.rmtree(install)
    print(f"LLVM_SELECTED version={format_version(version)} asset={asset['name']} digest={asset['digest']}")
    download(asset, archive, token)
    unpack(archive, install)
    archive.unlink()
    provision_linux_runtime(target, work, install, token)
    bin_directory = install / "bin"
    validate_tools(target, bin_directory, version)
    return version, bin_directory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--target", required=True, choices=sorted(ASSET_STEMS))
    parser.add_argument("--work-directory", required=True, type=Path)
    parser.add_argument("--github-path", type=Path, help="append the bin directory to this GITHUB_PATH file")
    parser.add_argument("--github-env", type=Path, help="record BUSTER_CI_LLVM_BIN/VERSION in this GITHUB_ENV file")
    arguments = parser.parse_args(argv)
    pin = os.environ.get("BUSTER_CI_LLVM_VERSION", "")
    pinned = parse_version(f"llvmorg-{pin}") if pin else None
    if pin and pinned is None:
        parser.error("BUSTER_CI_LLVM_VERSION must be an exact X.Y.Z release")
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    version, bin_directory = install_llvm(arguments.target, arguments.work_directory,
                                          fetch_releases(token), token, pinned)
    if arguments.github_path:
        with arguments.github_path.open("a", encoding="utf-8") as stream:
            stream.write(f"{bin_directory}\n")
    if arguments.github_env:
        with arguments.github_env.open("a", encoding="utf-8") as stream:
            stream.write(f"BUSTER_CI_LLVM_BIN={bin_directory}\nBUSTER_CI_LLVM_VERSION={format_version(version)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
