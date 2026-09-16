#!/usr/bin/env python3
"""Retain bounded CMake diagnostics; never configure, build, or infer a verdict."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys


FILES = (
    "CMakeCache.txt",
    "CMakeFiles/CMakeConfigureLog.yaml",
    "CMakeFiles/CMakeOutput.log",
    "CMakeFiles/CMakeError.log",
    "cmake-profile.json",
)
MAX_FILE_BYTES = 32 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_TREES = 32
MAX_ROOT_ENTRIES = 4096
IDENTITY_KEYS = (
    "GITHUB_REPOSITORY", "GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT",
    "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion",
)


def regular_path(root, relative):
    """Reject links/reparse points in every component below the trusted root."""
    current = root
    parts = Path(relative).parts
    if not parts or Path(relative).is_absolute() or ".." in parts:
        raise ValueError("invalid relative path")
    for index, part in enumerate(parts):
        current = current / part
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("link or reparse point refused")
        expected = stat.S_ISREG if index == len(parts) - 1 else stat.S_ISDIR
        if not expected(info.st_mode):
            raise ValueError("not a regular file/directory")
    return current


def collect(build_root, output, *, profile_requested=False, environment=None,
            file_limit=MAX_FILE_BYTES, total_limit=MAX_TOTAL_BYTES):
    build_root = Path(build_root).absolute()
    output = Path(output).absolute()
    # Resolve trusted parents (e.g. macOS /var), but not the supplied endpoints.
    for endpoint in (build_root, output):
        try:
            info = endpoint.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("linked input/output root refused")
    if output.exists():
        raise ValueError("output already exists; use a fresh evidence directory")
    build_root = build_root.resolve()
    output = output.resolve()
    if output == build_root or build_root in output.parents:
        raise ValueError("output must be outside the build root")
    if file_limit < 1 or total_limit < 1:
        raise ValueError("byte limits must be positive")
    output.mkdir(parents=True)
    env = os.environ if environment is None else environment
    report = {"schema": 1, "kind": "cmake-configure-evidence", "role": "diagnostic-only",
              "profile_requested": profile_requested,
              "identity": {key: env[key] for key in IDENTITY_KEYS if key in env},
              "files": [], "errors": [], "tree_count": 0, "captured_bytes": 0}
    trees = []
    try:
        with os.scandir(build_root) as entries:
            for count, entry in enumerate(entries, 1):
                if count > MAX_ROOT_ENTRIES:
                    raise ValueError("build-root entry limit exceeded")
                if entry.name.startswith("build-ci_on-cc_"):
                    trees.append(entry.name)
                    if len(trees) > MAX_TREES:
                        raise ValueError("configure tree limit exceeded")
    except FileNotFoundError:
        report["errors"].append("build root missing; no configure evidence")
    except (OSError, ValueError) as error:
        report["errors"].append(str(error))
        trees = []
    if not trees and not report["errors"]:
        report["errors"].append("no matrix configure trees found")
    report["tree_count"] = len(trees)
    for tree in sorted(trees):
        for name in FILES:
            relative = f"{tree}/{name}"
            record = {"path": relative}
            try:
                source = regular_path(build_root, relative)
                available = min(file_limit, total_limit - report["captured_bytes"])
                if source.stat().st_size > available:
                    raise ValueError("evidence byte limit exceeded")
                with source.open("rb") as stream:
                    data = stream.read(available + 1)
                if len(data) > available:
                    raise ValueError("evidence grew beyond byte limit")
                destination = output / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                with destination.open("xb") as stream:
                    stream.write(data)
                report["captured_bytes"] += len(data)
                record.update(status="captured", bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            except FileNotFoundError:
                record["status"] = "missing"
            except (OSError, ValueError) as error:
                record.update(status="error", reason=str(error))
                report["errors"].append(f"{relative}: {error}")
            report["files"].append(record)
    # Missing files explain failed/early configure attempts. They are never a
    # successful probe, nor an excuse to change the authoritative lane result.
    report["configure_logs_captured"] = sum(
        item["status"] == "captured" and item["path"].endswith("/CMakeFiles/CMakeConfigureLog.yaml")
        for item in report["files"])
    report["profiles_captured"] = sum(
        item["status"] == "captured" and item["path"].endswith("/cmake-profile.json")
        for item in report["files"])
    with (output / "manifest.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, default=Path("build"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile-requested", choices=("0", "1"), default="0")
    args = parser.parse_args()
    try:
        report = collect(args.build_root, args.output, profile_requested=args.profile_requested == "1")
        print(f"CMake evidence: {report['tree_count']} trees, {report['configure_logs_captured']} configure logs, "
              f"{report['profiles_captured']} profiles, {len(report['errors'])} collection errors")
        result = 1 if report["errors"] else 0
    except (OSError, ValueError) as error:
        print(f"CMake evidence collection failed: {error}", file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    sys.exit(main())
