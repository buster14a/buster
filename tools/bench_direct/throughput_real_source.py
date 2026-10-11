#!/usr/bin/env python3
"""Stage the pinned real-source inputs of THROUGHPUT_REAL_SOURCE_PROFILE (#2761).

Ownership: `tools/bench_direct`, trusted `main` only. compiler_compare calls
stage() before timing, outside every measured interval. Each input of
compiler_receipt.THROUGHPUT_REAL_SOURCE_INPUTS gets a fresh root: pinned files
and one pinned archive are downloaded and checked against their SHA-256 before
use, and copies come from the base checkout. The base checkout's descriptor
must match its pinned SHA-256. The native harness then re-verifies every
staged tree against its descriptor's inventory and tree hash, before and after
timing. No request-provided URLs, paths, flags or counts are accepted.

Map: download (bounded fetch), extract (safe archive members), stage (one
root per input and the staging record), harness_arguments (--real-source
pairs), main (manual reproduction outside the route).
"""
from __future__ import annotations

import argparse
import hashlib
import http.client
import io
import json
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

from compiler_receipt import THROUGHPUT_REAL_SOURCE_INPUTS, THROUGHPUT_REAL_SOURCE_STAGING_SCHEMA

DOWNLOAD_LIMIT = 16 * 1024 * 1024
DOWNLOAD_TIMEOUT_SECONDS = 120
MEMBER_LIMIT = 64 * 1024 * 1024


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def download(url: str) -> bytes:
    """Fetch one pinned URL, refusing anything larger than DOWNLOAD_LIMIT."""
    try:
        with urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response:
            data = response.read(DOWNLOAD_LIMIT + 1)
    except http.client.HTTPException as error:
        raise ValueError(f"{url} download failed: {error.__class__.__name__}: {error}") from error
    if len(data) > DOWNLOAD_LIMIT:
        raise ValueError(f"{url} exceeds the {DOWNLOAD_LIMIT} byte download limit")
    return data


def pinned(data: bytes, digest: str, label: str) -> bytes:
    if sha256(data) != digest:
        raise ValueError(f"{label} SHA-256 {sha256(data)} is not the pinned {digest}")
    return data


def relative(name: str, prefix: str) -> str | None:
    """The safe relative path of an archive member under prefix, or None for anything else."""
    if not name.startswith(prefix) or name == prefix:
        return None
    path = PurePosixPath(name[len(prefix):])
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts) or "\\" in name:
        raise ValueError(f"archive member {name!r} is not a safe relative path")
    return str(path)


def extract(data: bytes, archive: dict) -> dict[str, bytes]:
    """Regular-file members under the archive's prefix, keyed by their path below it."""
    members: dict[str, bytes] = {}
    prefix = archive["prefix"]
    if archive["format"] == "tar.gz":
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as bundle:
            for info in bundle.getmembers():
                path = relative(info.name, prefix)
                if path is None or info.isdir():
                    continue
                if not info.isreg() or info.size > MEMBER_LIMIT:
                    raise ValueError(f"archive member {info.name!r} is not a bounded regular file")
                reader = bundle.extractfile(info)
                members[path] = reader.read() if reader else b""
    elif archive["format"] == "zip":
        with zipfile.ZipFile(io.BytesIO(data)) as bundle:
            for info in bundle.infolist():
                path = relative(info.filename, prefix)
                if path is None or info.is_dir():
                    continue
                if info.file_size > MEMBER_LIMIT or (info.external_attr >> 16) & 0o170000 not in (0, 0o100000):
                    raise ValueError(f"archive member {info.filename!r} is not a bounded regular file")
                members[path] = bundle.read(info)
    else:
        raise ValueError(f"unsupported archive format {archive['format']!r}")
    if not members:
        raise ValueError(f"archive has no members under {prefix!r}")
    return members


def write(root: Path, path: str, data: bytes) -> None:
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    with open(target, "xb") as stream:
        stream.write(data)


def stage(repo_root: Path, work: Path, fetch=download) -> tuple[dict[str, Path], dict]:
    """Create one fresh root per pinned input under work; raise ValueError on any mismatch."""
    roots: dict[str, Path] = {}
    record = {"schema": THROUGHPUT_REAL_SOURCE_STAGING_SCHEMA, "status": "incomplete", "inputs": {}}
    work.mkdir(parents=True, exist_ok=False)
    for name, pins in THROUGHPUT_REAL_SOURCE_INPUTS.items():
        descriptor = repo_root / pins["descriptor"]
        pinned(descriptor.read_bytes(), pins["descriptor_sha256"], f"{name} descriptor {pins['descriptor']}")
        root = work / name
        root.mkdir()
        archive = pins["archive"]
        if archive is not None:
            data = pinned(fetch(archive["url"]), archive["sha256"], f"{name} archive")
            for path, member in sorted(extract(data, archive).items()):
                write(root, path, member)
        for item in pins["files"]:
            write(root, item["path"], pinned(fetch(item["url"]), item["sha256"], f"{name} file {item['path']}"))
        for item in pins["copies"]:
            data = pinned((repo_root / item["from"]).read_bytes(), item["sha256"], f"{name} copy {item['from']}")
            write(root, item["path"], data)
        roots[name] = root
        record["inputs"][name] = {
            "descriptor": pins["descriptor"], "descriptor_sha256": pins["descriptor_sha256"], "root": str(root),
            "archive_sha256": archive["sha256"] if archive else None,
            "files": [{"path": item["path"], "sha256": item["sha256"]} for item in pins["files"]],
            "copies": [{"path": item["path"], "sha256": item["sha256"]} for item in pins["copies"]]}
    record["status"] = "complete"
    return roots, record


def harness_arguments(repo_root: Path, roots: dict[str, Path]) -> list[str]:
    """`--real-source DESCRIPTOR ROOT` for every pinned input, in profile order."""
    arguments: list[str] = []
    for name, pins in THROUGHPUT_REAL_SOURCE_INPUTS.items():
        arguments += ["--real-source", str((repo_root / pins["descriptor"]).resolve()), str(roots[name].resolve())]
    return arguments


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo-root", type=Path, required=True, help="checkout holding the pinned descriptors")
    parser.add_argument("--work", type=Path, required=True, help="new directory for the staged roots")
    arguments = parser.parse_args(argv)
    try:
        roots, record = stage(arguments.repo_root, arguments.work)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"throughput_real_source: {error}", file=sys.stderr)
        return 1
    print(json.dumps(record, indent=2, sort_keys=True))
    print(" ".join(harness_arguments(arguments.repo_root, roots)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
