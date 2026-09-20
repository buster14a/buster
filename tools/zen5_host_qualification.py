#!/usr/bin/env python3
"""Capture and validate model-specific Ryzen 7 9700X PMU qualification evidence.

This tool is deliberately separate from ordinary timing trials and from the
native-retirement statistical decision. It records exact event encodings,
actual-host identity, per-event availability, and perf's running fraction.
"""

from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path
import shutil
import sys
import tempfile
from typing import Any

from zen5_qualification_common import (
    MAX_CAPTURE_BYTES,
    RESULT_SCHEMA,
    QualificationError,
    atomic_write_json,
    canonical_bytes,
    collect_git_identity,
    collect_host,
    collect_ibs,
    environment_contract,
    file_snapshots,
    group_expression,
    hash_path_record,
    load_json,
    load_manifest,
    manifest_path,
    map_perf_entries,
    parse_perf_json,
    read_bounded_text,
    run_binary,
    sha256_bytes,
    sha256_file,
    validate_host,
)
from zen5_qualification_replay import compare_results, validate_result

def capture_group(
    group: dict[str, Any],
    repeats: int,
    minimum_fraction: float,
    cpu: int,
    command: list[str],
    cwd: Path,
    output_paths: list[Path],
) -> tuple[list[dict[str, Any]], list[str]]:
    runs: list[dict[str, Any]] = []
    problems: list[str] = []
    expression = group_expression(group)
    for repeat in range(repeats):
        with tempfile.TemporaryDirectory(prefix="buster-zen5-pmu-") as temporary:
            perf_output = Path(temporary) / "perf.json"
            invocation = [
                "perf",
                "stat",
                "--json-output",
                "--no-big-num",
                "--output",
                str(perf_output),
                "-e",
                expression,
                "--",
                "taskset",
                "-c",
                str(cpu),
                *command,
            ]
            status, stdout, stderr = run_binary(invocation, cwd=cwd)
            raw_perf = read_bounded_text(perf_output, MAX_CAPTURE_BYTES)
            run_problems: list[str] = []
            observations: dict[str, Any] = {}
            if raw_perf is None:
                run_problems.append("perf output file is unavailable")
            else:
                try:
                    entries = parse_perf_json(raw_perf)
                    observations, mapping_problems = map_perf_entries(group, entries, minimum_fraction)
                    run_problems.extend(mapping_problems)
                except QualificationError as error:
                    run_problems.append(str(error))
            output_artifacts = file_snapshots(output_paths)
            for artifact in output_artifacts:
                if artifact.get("sha256") is None:
                    run_problems.append(f"declared output artifact unavailable: {artifact['path']}")
            if status != 0:
                run_problems.append(f"perf/workload exited with status {status}")
            run = {
                "repeat": repeat,
                "invocation": invocation,
                "exit_status": status,
                "stdout_size": len(stdout),
                "stdout_sha256": sha256_bytes(stdout),
                "stderr_size": len(stderr),
                "stderr_sha256": sha256_bytes(stderr),
                "perf_json": raw_perf,
                "perf_json_sha256": sha256_bytes(raw_perf.encode("utf-8")) if raw_perf is not None else None,
                "observations": observations,
                "output_artifacts": output_artifacts,
                "valid": not run_problems,
                "invalid_reasons": run_problems,
            }
            runs.append(run)
            problems.extend(f"{group['id']} repeat {repeat}: {problem}" for problem in run_problems)
    return runs, problems


def capture(arguments: argparse.Namespace) -> int:
    manifest, digest = load_manifest(arguments.manifest)
    if not shutil.which("perf") or not shutil.which("taskset"):
        raise QualificationError("capture requires perf and taskset")
    command = list(arguments.command)
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        raise QualificationError("capture requires a workload after --")
    cwd = arguments.working_directory.resolve(strict=True)
    executable = Path(command[0])
    if not executable.is_absolute():
        executable = cwd / executable
    executable_record = hash_path_record(executable)
    repository, repository_problems = collect_git_identity(arguments.repository_root)
    host, host_problems = collect_host(arguments.cpu)
    host_problems.extend(validate_host(manifest, host))
    external = [hash_path_record(path) for path in arguments.environment_input]
    contract = environment_contract(host, external)
    fingerprint = sha256_bytes(canonical_bytes(contract))
    inputs_before = [hash_path_record(path) for path in arguments.input]
    output_paths = [path.resolve() if path.is_absolute() else cwd / path for path in arguments.output_artifact]
    groups: list[dict[str, Any]] = []
    problems = repository_problems + host_problems
    for group in manifest["groups"]:
        runs, group_problems = capture_group(
            group,
            manifest["repeats"],
            float(manifest["minimum_running_fraction"]),
            arguments.cpu,
            command,
            cwd,
            output_paths,
        )
        groups.append({"id": group["id"], "definition": group, "runs": runs})
        problems.extend(group_problems)
    inputs_after = [hash_path_record(path) for path in arguments.input]
    if inputs_before != inputs_after:
        problems.append("one or more immutable workload inputs changed during capture")
    result = {
        "schema": RESULT_SCHEMA,
        "version": 1,
        "created_utc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "purpose": manifest["purpose"],
        "event_manifest": str((arguments.manifest or manifest_path()).resolve()),
        "event_manifest_sha256": digest,
        "tool_sha256": sha256_file(Path(__file__).resolve()),
        "repository": repository,
        "host": host,
        "environment_contract": contract,
        "environment_fingerprint_sha256": fingerprint,
        "workload": {
            "working_directory": str(cwd),
            "argv": command,
            "selected_cpu": arguments.cpu,
            "executable": executable_record,
            "inputs_before": inputs_before,
            "inputs_after": inputs_after,
            "declared_output_artifacts": [str(path) for path in output_paths],
        },
        "groups": groups,
        "optional_attribution": collect_ibs(Path("/"), manifest),
        "ordinary_timing_trials_separate": True,
        "statistical_decision": "not-evaluated",
        "qualification_status": "invalid" if problems else "pmu-qualified",
        "invalid_reasons": problems,
    }
    atomic_write_json(arguments.output, result)
    replay_problems = validate_result(result, manifest, digest)
    if replay_problems:
        print("capture replay failed:", file=sys.stderr)
        for problem in replay_problems:
            print(f"- {problem}", file=sys.stderr)
        return 2
    print(f"{result['qualification_status']}: {arguments.output}")
    return 0 if result["qualification_status"] == "pmu-qualified" else 2


def command_validate(arguments: argparse.Namespace) -> int:
    manifest, digest = load_manifest(arguments.manifest)
    value = load_json(arguments.result)
    problems = validate_result(value, manifest, digest)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 2
    print(f"valid {RESULT_SCHEMA}: {arguments.result}")
    return 0


def command_compare(arguments: argparse.Namespace) -> int:
    left = load_json(arguments.before)
    right = load_json(arguments.after)
    value = compare_results(left, right)
    if arguments.output:
        atomic_write_json(arguments.output, value)
    else:
        sys.stdout.buffer.write(canonical_bytes(value))
    return 1 if value["requalification_required"] else 0



def self_test() -> int:
    from zen5_host_qualification_test import run_self_test

    return run_self_test()

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run offline parser and policy tests")
    subparsers = parser.add_subparsers(dest="command_name")

    capture_parser = subparsers.add_parser("capture", help="capture a fixed diagnostic replay on the physical host")
    capture_parser.add_argument("--manifest", type=Path)
    capture_parser.add_argument("--output", type=Path, required=True)
    capture_parser.add_argument("--cpu", type=int, default=2)
    capture_parser.add_argument("--repository-root", type=Path, default=Path.cwd())
    capture_parser.add_argument("--working-directory", type=Path, default=Path.cwd())
    capture_parser.add_argument("--input", type=Path, action="append", default=[])
    capture_parser.add_argument("--output-artifact", type=Path, action="append", default=[])
    capture_parser.add_argument("--environment-input", type=Path, action="append", default=[])
    capture_parser.add_argument("command", nargs=argparse.REMAINDER)

    validate_parser = subparsers.add_parser("validate", help="replay a captured qualification result")
    validate_parser.add_argument("--manifest", type=Path)
    validate_parser.add_argument("result", type=Path)

    compare_parser = subparsers.add_parser("compare", help="compare environment contracts and require requalification on drift")
    compare_parser.add_argument("before", type=Path)
    compare_parser.add_argument("after", type=Path)
    compare_parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.self_test:
            return self_test()
        if arguments.command_name == "capture":
            return capture(arguments)
        if arguments.command_name == "validate":
            return command_validate(arguments)
        if arguments.command_name == "compare":
            return command_compare(arguments)
        raise QualificationError("select capture, validate, compare, or --self-test")
    except (QualificationError, FileNotFoundError, PermissionError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
