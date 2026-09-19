#!/usr/bin/env python3
"""Install a checksum-verified Zig archive, never a cached executable/build tree.

The reviewed manifest owns the version and all six target digests. Both cache
hits and downloads cross the same verification boundary before tar or Zig runs.
Its per-target compressed sizes mirror the official Zig release index
(https://ziglang.org/download/index.json) for the pinned version and are the
receipt ceilings; a future pin update must update the corresponding size facts
with its digest. This is dependency setup only; build.c continues to own the
compiler matrix.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

TARGETS = frozenset(f"{arch}-{system}" for arch in ("x86_64", "aarch64")
                    for system in ("linux", "macos", "windows"))
DOWNLOAD_ATTEMPTS = 3
DOWNLOAD_TIMEOUT_SECONDS = 60
DOWNLOAD_CHUNK_SIZE = 1024 * 1024
INSTALL_PUBLISH_ATTEMPTS = 3
INSTALL_PUBLISH_RETRY_SECONDS = 0.1


def load_pin(manifest, target):
    data = json.loads(Path(manifest).read_text(encoding="utf-8"))
    digests = data.get("sha256")
    sizes = data.get("size")
    if target not in TARGETS or not isinstance(digests, dict) or set(digests) != TARGETS:
        raise ValueError("Zig manifest must cover exactly the six supported targets")
    if not isinstance(sizes, dict) or set(sizes) != TARGETS:
        raise ValueError("Zig manifest must provide sizes for exactly the six supported targets")
    version = data["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Zig version must be an exact release")
    for digest in digests.values():
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("Zig digests must be lowercase SHA-256 values")
    for size in sizes.values():
        if type(size) is not int or size < 0:
            raise ValueError("Zig archive sizes must be non-negative integers")
    return version, digests[target], sizes[target]


def verify_archive(archive, expected):
    digest = hashlib.sha256()
    with Path(archive).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected:
        raise ValueError(f"Zig archive checksum mismatch: {actual}; expected {expected}. "
                         "Do not execute or republish this cache entry.")


def _copy_archive(source, output, maximum):
    received = 0
    while True:
        remaining = maximum - received
        read_size = min(DOWNLOAD_CHUNK_SIZE, remaining + 1)
        chunk = source.read(read_size)
        if not chunk:
            break
        received += len(chunk)
        if received > maximum:
            raise ValueError(f"Zig archive exceeds maximum compressed size of {maximum} bytes "
                             f"(received {received} bytes)")
        written = output.write(chunk)
        if written != len(chunk):
            raise OSError(f"short Zig archive write: wrote {written} of {len(chunk)} bytes")
        if received == maximum:
            extra = source.read(1)
            if extra:
                received += len(extra)
                raise ValueError(f"Zig archive exceeds maximum compressed size of {maximum} bytes "
                                 f"(received at least {received} bytes)")
            break
    return received


def _remove_partial(partial):
    partial.unlink(missing_ok=True)


def download_archive(url, destination, max_bytes):
    """Bound received bytes and network retries; partial files never cache."""
    destination = Path(destination)
    partial = destination.with_name(destination.name + ".part")
    if type(max_bytes) is not int or max_bytes < 0:
        raise ValueError("Zig archive maximum size must be a non-negative integer")
    try:
        for attempt in range(DOWNLOAD_ATTEMPTS):
            try:
                with urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT_SECONDS) as source, partial.open("wb") as output:
                    _copy_archive(source, output, max_bytes)
                partial.replace(destination)
                return
            except ValueError:
                _remove_partial(partial)
                raise
            except (OSError, urllib.error.URLError):
                _remove_partial(partial)
                if attempt == DOWNLOAD_ATTEMPTS - 1:
                    raise
                time.sleep(attempt + 1)
    finally:
        _remove_partial(partial)


def _running_on_windows():
    return os.name == "nt"


def _publish_installation(staging, root, windows):
    for attempt in range(INSTALL_PUBLISH_ATTEMPTS):
        if root.exists():
            raise ValueError(f"Zig installation directory appeared during publication: {root}")
        try:
            if windows:
                # Unlike os.replace(), Windows rename fails closed if another
                # process creates the destination between the existence check
                # and publication.
                staging.rename(root)
            else:
                staging.replace(root)
            break
        except OSError as error:
            transient = windows and getattr(error, "winerror", None) == 5
            if not transient or attempt == INSTALL_PUBLISH_ATTEMPTS - 1:
                raise
            if root.exists():
                raise ValueError(f"Zig installation directory appeared during publication: {root}") from error
            time.sleep(INSTALL_PUBLISH_RETRY_SECONDS * (attempt + 1))


def install(target, manifest, cache_directory, install_directory, github_path=None):
    version, digest, max_bytes = load_pin(manifest, target)
    archive = Path(cache_directory).resolve() / "archive"
    root = Path(install_directory).resolve()
    if root.exists():
        raise ValueError(f"Zig installation directory already exists: {root}")
    archive.parent.mkdir(parents=True, exist_ok=True)
    cached = archive.is_file()
    started = time.monotonic()
    if not cached:
        extension = "zip" if target.endswith("-windows") else "tar.xz"
        url = f"https://ziglang.org/download/{version}/zig-{target}-{version}.{extension}"
        download_archive(url, archive, max_bytes)
    # Never trust cache-hit, archive names, or a prior extraction as integrity.
    verify_archive(archive, digest)
    root.parent.mkdir(parents=True, exist_ok=True)
    tar = str(Path(os.environ["SystemRoot"]) / "System32" / "tar.exe") if os.name == "nt" else "tar"
    with tempfile.TemporaryDirectory(prefix="buster-zig-", dir=root.parent) as temporary:
        staging = Path(temporary) / "install"
        staging.mkdir()
        subprocess.run([tar, "-xf", str(archive), "-C", str(staging), "--strip-components=1"], check=True)
        executable = staging / ("zig.exe" if target.endswith("-windows") else "zig")
        result = subprocess.run([str(executable), "version"], check=True, capture_output=True, text=True)
        if result.stdout.strip() != version:
            raise ValueError("Verified Zig archive did not report the pinned version")
        _publish_installation(staging, root, _running_on_windows())
    if github_path:
        with Path(github_path).open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(str(root) + "\n")
    print(f"ZIG_SETUP source={'cache' if cached else 'download'} target={target} "
          f"version={version} sha256={digest} bytes={archive.stat().st_size} "
          f"seconds={time.monotonic() - started:.3f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=sorted(TARGETS), required=True)
    parser.add_argument("--manifest", default=".github/zig.json")
    parser.add_argument("--cache-directory", required=True)
    parser.add_argument("--install-directory", required=True)
    args = parser.parse_args()
    status = 0
    try:
        install(args.target, args.manifest, args.cache_directory, args.install_directory, os.getenv("GITHUB_PATH"))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Zig setup failed: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
