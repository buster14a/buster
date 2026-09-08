#!/usr/bin/env python3
"""Install a checksum-verified Zig archive, never a cached executable/build tree.

The reviewed manifest owns the version and all six target digests. Both cache
hits and downloads cross the same verification boundary before tar or Zig runs.
This is dependency setup only; build.c continues to own the compiler matrix.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

TARGETS = frozenset(f"{arch}-{system}" for arch in ("x86_64", "aarch64")
                    for system in ("linux", "macos", "windows"))


def load_pin(manifest, target):
    data = json.loads(Path(manifest).read_text(encoding="utf-8"))
    if target not in TARGETS or set(data["sha256"]) != TARGETS:
        raise ValueError("Zig manifest must cover exactly the six supported targets")
    version = data["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Zig version must be an exact release")
    for digest in data["sha256"].values():
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("Zig digests must be lowercase SHA-256 values")
    return version, data["sha256"][target]


def verify_archive(archive, expected):
    digest = hashlib.sha256()
    with Path(archive).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected:
        raise ValueError(f"Zig archive checksum mismatch: {actual}; expected {expected}. "
                         "Do not execute or republish this cache entry.")


def download_archive(url, destination):
    """Bound network retries; partial downloads never become cache entries."""
    destination = Path(destination)
    partial = destination.with_name(destination.name + ".part")
    complete = False
    try:
        for attempt in range(3):
            if not complete:
                try:
                    with urllib.request.urlopen(url, timeout=60) as source, partial.open("wb") as output:
                        shutil.copyfileobj(source, output, length=1024 * 1024)
                    partial.replace(destination)
                    complete = True
                except (OSError, urllib.error.URLError):
                    if attempt == 2:
                        raise
                    time.sleep(attempt + 1)
    finally:
        partial.unlink(missing_ok=True)


def install(target, manifest, cache_directory, install_directory, github_path=None):
    version, digest = load_pin(manifest, target)
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
        download_archive(url, archive)
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
        staging.replace(root)
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
