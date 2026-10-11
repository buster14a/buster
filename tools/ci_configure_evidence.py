#!/usr/bin/env python3
"""Retain bounded CMake and Ninja diagnostics; never configure, build, or infer a verdict."""
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
    ".ninja_log",
)
RETAINED_NAMES = {".ninja_log": "ninja_log.txt"}
TREE_PREFIXES = (
    "build-ci_on-cc_",
    "build-release-ci_on-cc_",
    "build-checks-ci_on-cc_",
    "build-sanitized-debug-ci_on-cc_",
    "build-sanitized-release-ci_on-cc_",
    "build-portability-ci_on-cc_",
)
MAX_FILE_BYTES = 32 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_TREES = 32
MAX_ROOT_ENTRIES = 4096
IDENTITY_KEYS = (
    "GITHUB_REPOSITORY", "GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT",
    "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion",
)


def matrix_tree_name(name):
    """Recognize only native matrix configure trees, never superbuild trees."""
    return any(name.startswith(prefix) for prefix in TREE_PREFIXES)


def summarize_ninja_log(data: bytes, build_directory: str | None = None) -> dict:
    lines = data.decode("utf-8", errors="replace").splitlines()
    version = None
    malformed_lines = 0
    entries = {}
    header_prefix = "# ninja log v"
    header_seen = False
    for line in lines:
        if not line.strip():
            continue
        if not header_seen:
            header_seen = True
            if line.startswith(header_prefix):
                try:
                    version = int(line[len(header_prefix):])
                except ValueError:
                    malformed_lines += 1
            else:
                malformed_lines += 1
            continue
        fields = line.split("\t")
        if len(fields) < 4:
            malformed_lines += 1
            continue
        try:
            start_ms = int(fields[0])
            end_ms = int(fields[1])
            int(fields[2])
        except ValueError:
            malformed_lines += 1
            continue
        output = fields[3]
        # Ninja invocation clocks reset; these times are relative to each
        # invocation and are not comparable across invocations.
        entries[output] = {
            "output": output,
            "start_ms": start_ms,
            "end_ms": end_ms,
            "duration_ms": end_ms - start_ms,
            "hash": fields[4] if len(fields) > 4 else None,
        }
    output_groups_with_hash = []
    aliased_outputs = {}
    aliases = set()
    if build_directory is not None:
        normalized_outputs = {}
        for entry in entries.values():
            normalized_output = os.path.normcase(entry["output"])
            normalized_outputs.setdefault(normalized_output, []).append(entry)
        build_directory = build_directory.rstrip("/\\")
        for entry in entries.values():
            output = entry["output"]
            drive_letter = len(output) > 1 and output[0].isalpha() and output[1] == ":"
            if (os.path.isabs(output) or output.startswith("/") or drive_letter
                    or entry["hash"] is None):
                continue
            absolute_output = os.path.normcase(f"{build_directory}/{output}")
            for alias in normalized_outputs.get(absolute_output, []):
                if (alias["output"] != output and alias["output"] not in aliases
                        and alias["start_ms"] == entry["start_ms"]
                        and alias["end_ms"] == entry["end_ms"]
                        and alias["hash"] == entry["hash"]):
                    aliased_outputs.setdefault(output, []).append(alias["output"])
                    aliases.add(alias["output"])
    for entry in entries.values():
        output = entry["output"]
        if output in aliases:
            continue
        outputs = sorted([output, *aliased_outputs.get(output, [])])
        output_groups_with_hash.append(({
            "output": output,
            "outputs": outputs,
            "start_ms": entry["start_ms"],
            "end_ms": entry["end_ms"],
            "duration_ms": entry["duration_ms"],
            "shared_interval_hash": False,
        }, entry["hash"]))
    interval_counts = {}
    for group, command_hash in output_groups_with_hash:
        if command_hash is not None:
            key = (group["start_ms"], group["end_ms"], command_hash)
            interval_counts[key] = interval_counts.get(key, 0) + 1
    for group, command_hash in output_groups_with_hash:
        if command_hash is not None:
            key = (group["start_ms"], group["end_ms"], command_hash)
            group["shared_interval_hash"] = interval_counts[key] > 1
    all_output_groups = sorted(
        (group for group, _ in output_groups_with_hash),
        key=lambda group: (-group["duration_ms"], group["output"]))
    return {
        "version": version,
        "output_groups": len(all_output_groups),
        "ambiguous_output_groups": sum(
            group["shared_interval_hash"] for group in all_output_groups),
        "malformed_lines": malformed_lines,
        "output_group_duration_sum_ms": sum(
            group["duration_ms"] for group in all_output_groups),
        "top": all_output_groups[:15],
        "all_output_groups": all_output_groups,
    }


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
              "files": [], "errors": [], "tree_count": 0, "captured_bytes": 0,
              "ninja_log_summaries": []}
    trees = []
    try:
        with os.scandir(build_root) as entries:
            for count, entry in enumerate(entries, 1):
                if count > MAX_ROOT_ENTRIES:
                    raise ValueError("build-root entry limit exceeded")
                if matrix_tree_name(entry.name):
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
            retained_relative = f"{tree}/{RETAINED_NAMES.get(name, name)}"
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
                destination = output / retained_relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                with destination.open("xb") as stream:
                    stream.write(data)
                report["captured_bytes"] += len(data)
                record.update(status="captured", retained_path=retained_relative,
                              bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            except FileNotFoundError:
                record["status"] = "missing"
            except (OSError, ValueError) as error:
                record.update(status="error", reason=str(error))
                report["errors"].append(f"{relative}: {error}")
            report["files"].append(record)
            if name == ".ninja_log" and record["status"] == "captured":
                summary_path = output / tree / "ninja-log-summary.json"
                try:
                    summary = summarize_ninja_log(
                        data, (build_root / tree).resolve().as_posix())
                    with summary_path.open("x", encoding="utf-8") as stream:
                        json.dump(summary, stream, indent=2, sort_keys=True)
                        stream.write("\n")
                except (OSError, ValueError) as error:
                    report["errors"].append(f"{tree}/ninja-log-summary.json: {error}")
                else:
                    report["ninja_log_summaries"].append({"tree": tree, **summary})
    # Missing files explain failed/early configure attempts. They are never a
    # successful probe, nor an excuse to change the authoritative lane result.
    report["configure_logs_captured"] = sum(
        item["status"] == "captured" and item["path"].endswith("/CMakeFiles/CMakeConfigureLog.yaml")
        for item in report["files"])
    report["profiles_captured"] = sum(
        item["status"] == "captured" and item["path"].endswith("/cmake-profile.json")
        for item in report["files"])
    report["ninja_logs_captured"] = sum(
        item["status"] == "captured" and item["path"].endswith("/.ninja_log")
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
              f"{report['profiles_captured']} profiles, {len(report['errors'])} collection errors, "
              f"{report['ninja_logs_captured']} ninja logs")
        for summary in report["ninja_log_summaries"]:
            tree = summary["tree"]
            print(f"NINJA_LOG_SUMMARY tree={tree} output_groups={summary['output_groups']} "
                  f"output_group_duration_sum_seconds="
                  f"{summary['output_group_duration_sum_ms'] / 1000:.3f} "
                  f"ambiguous_output_groups={summary['ambiguous_output_groups']} "
                  f"malformed={summary['malformed_lines']}")
            for rank, edge in enumerate(summary["top"], 1):
                outputs = f" outputs={len(edge['outputs'])}" if len(edge["outputs"]) > 1 else ""
                shared_interval_hash = " shared_interval_hash=1" if edge["shared_interval_hash"] else ""
                print(f"NINJA_LOG_EDGE tree={tree} rank={rank} seconds={edge['duration_ms'] / 1000:.3f} "
                      f"output={edge['output']}{outputs}{shared_interval_hash}")
        result = 1 if report["errors"] else 0
    except (OSError, ValueError) as error:
        print(f"CMake evidence collection failed: {error}", file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    sys.exit(main())
