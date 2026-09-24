#!/usr/bin/env python3
"""Replay same-source cross-build controls for the Ryzen 7 9700X.

An admitted service produces the capture. This reader does not build or run a
compiler. It preserves the fixed A/A schedule while allowing the two logical
subjects to be separately built binaries of identical source. A valid capture
is descriptive build-provenance evidence, never a performance verdict.
"""

from __future__ import annotations

import argparse
import posixpath
from pathlib import Path
import re
import statistics
import sys
from typing import Any

from zen5_aa_noise import (
    CAPTURE_SCHEMA as AA_CAPTURE_SCHEMA,
    TOTAL_PAIRS,
    NoiseError,
    derived_labels,
    effect_summary,
    expected_schedule,
    identity_problems,
    integer,
    lag_one_correlation,
    load_json,
    metric_rows,
    nearest_rank,
    observation_reasons,
    per_partition,
    relative_drift_per_pair,
    sha256_file,
    validate_capture as validate_aa_capture,
    SHA256_RE,
    write_json,
)


CAPTURE_SCHEMA = "buster-zen5-build-control-capture-v1"
ANALYSIS_SCHEMA = "buster-zen5-build-control-model-v1"
CONTROL_KINDS = {"same-root-rebuild", "cross-root"}
MAX_U64 = (1 << 64) - 1


def absolute_root(value: Any) -> bool:
    return (
        isinstance(value, str)
        and value.startswith("/")
        and value != "/"
        and posixpath.normpath(value) == value
        and ".." not in value.split("/")
    )


def argv_problems(value: Any, name: str) -> list[str]:
    if not isinstance(value, list) or not value or not all(isinstance(item, str) and item for item in value):
        return [f"{name} must be a nonempty argv string list"]
    return []


def normalized_argv(value: list[str], root: str) -> list[str]:
    pattern = re.compile(re.escape(root) + r"(?=$|[/=:])")
    return [pattern.sub("${BUILD_ROOT}", argument) for argument in value]


def build_problems(value: Any, name: str, source_identity: Any, repository: Any) -> list[str]:
    problems: list[str] = []
    if not isinstance(value, dict):
        return [f"{name} must be an object"]
    if value.get("source_identity_sha256") != source_identity:
        problems.append(f"{name}.source_identity_sha256 differs from the capture")
    if isinstance(repository, dict):
        if value.get("source_revision") != repository.get("revision"):
            problems.append(f"{name}.source_revision differs from the capture")
        if value.get("source_tree") != repository.get("tree"):
            problems.append(f"{name}.source_tree differs from the capture")
    if not absolute_root(value.get("build_root")):
        problems.append(f"{name}.build_root must be a normalized absolute path")
    for key in (
        "toolchain_identity_sha256",
        "build_environment_sha256",
        "compile_commands_sha256",
        "normalized_commands_sha256",
        "build_log_sha256",
    ):
        if not SHA256_RE.fullmatch(str(value.get(key))):
            problems.append(f"{name}.{key} is malformed")
    for key in ("build_argv", "compile_argv", "link_argv"):
        problems.extend(argv_problems(value.get(key), f"{name}.{key}"))
    started = value.get("build_started_monotonic_ns")
    finished = value.get("build_finished_monotonic_ns")
    if not integer(started) or not integer(finished, positive=True) or finished <= started:
        problems.append(f"{name} build monotonic bounds are malformed")
    problems.extend(identity_problems(value.get("binary"), f"{name}.binary"))
    binary = value.get("binary")
    if isinstance(binary, dict) and not absolute_root(binary.get("path")):
        problems.append(f"{name}.binary.path must be a normalized absolute path")
    section = value.get("text_section")
    if not isinstance(section, dict):
        problems.append(f"{name}.text_section must be an object")
    else:
        for key in ("file_offset", "virtual_address"):
            if not integer(section.get(key)):
                problems.append(f"{name}.text_section.{key} is malformed")
        if not integer(section.get("size"), positive=True):
            problems.append(f"{name}.text_section.size is malformed")
        if not SHA256_RE.fullmatch(str(section.get("sha256"))):
            problems.append(f"{name}.text_section.sha256 is malformed")
        if (
            isinstance(binary, dict)
            and integer(binary.get("size"))
            and integer(section.get("file_offset"))
            and integer(section.get("size"), positive=True)
        ):
            if section["file_offset"] + section["size"] > binary["size"]:
                problems.append(f"{name}.text_section exceeds the binary size")
    return problems


def _aa_shape(capture: dict[str, Any]) -> dict[str, Any]:
    """Reuse the unchanged v1 slot, timing, and invalidity checks."""
    builds = capture.get("builds") if isinstance(capture.get("builds"), dict) else {}
    binary = builds.get("A", {}).get("binary") if isinstance(builds.get("A"), dict) else None
    binary = binary if isinstance(binary, dict) else {"size": 1, "mode": 0o555, "sha256": "0" * 64}
    staging = capture.get("staging_paths") if isinstance(capture.get("staging_paths"), dict) else {}
    shaped = dict(capture)
    shaped.update(
        schema=AA_CAPTURE_SCHEMA,
        version=1,
        purpose="immutable-binary A/A noise calibration",
        binary_paths={
            path_id: {
                "path": staging.get(path_id, f"/invalid/{path_id}"),
                **{key: binary.get(key) for key in ("size", "mode", "sha256")},
            }
            for path_id in ("path0", "path1")
        },
    )
    observations = capture.get("observations")
    if isinstance(observations, list):
        shaped_observations = []
        for observation in observations:
            if isinstance(observation, dict):
                base_reasons = observation_reasons(observation, str(capture.get("expected_output_sha256")))
                shaped_observations.append({**observation, "valid": not base_reasons, "invalid_reasons": base_reasons})
            else:
                shaped_observations.append(observation)
        shaped["observations"] = shaped_observations
    return shaped


def validate_capture(value: Any) -> tuple[list[str], list[str]]:
    """Return structural problems and replayed invalidity reasons separately."""
    if not isinstance(value, dict) or value.get("schema") != CAPTURE_SCHEMA:
        return [f"capture schema must be {CAPTURE_SCHEMA}"], []
    problems: list[str] = []
    if value.get("version") != 1:
        problems.append("capture version must be 1")
    if value.get("purpose") != "same-source cross-build calibration":
        problems.append("capture purpose is malformed")
    kind = value.get("control_kind")
    if not isinstance(kind, str) or kind not in CONTROL_KINDS:
        problems.append("control_kind must be same-root-rebuild or cross-root")
    if not SHA256_RE.fullmatch(str(value.get("predeclared_family_sha256"))):
        problems.append("predeclared_family_sha256 is malformed")
    repository = value.get("repository")
    source_identity = value.get("source_identity_sha256")
    builds = value.get("builds")
    if not isinstance(builds, dict) or set(builds) != {"A", "B"}:
        problems.append("builds must contain exactly A and B")
        builds = {}
    for label in ("A", "B"):
        problems.extend(build_problems(builds.get(label), f"builds.{label}", source_identity, repository))
    a, b = builds.get("A"), builds.get("B")
    if isinstance(a, dict) and isinstance(b, dict):
        roots = (a.get("build_root"), b.get("build_root"))
        if all(absolute_root(root) for root in roots):
            if kind == "same-root-rebuild" and roots[0] != roots[1]:
                problems.append("same-root-rebuild must use one configured build root")
            if kind == "cross-root" and roots[0] == roots[1]:
                problems.append("cross-root must use distinct configured build roots")
        for key in (
            "source_revision", "source_tree", "source_identity_sha256",
            "toolchain_identity_sha256", "build_environment_sha256", "normalized_commands_sha256",
        ):
            if a.get(key) != b.get(key):
                problems.append(f"builds differ in {key}")
        for key in ("build_argv", "compile_argv", "link_argv"):
            left, right = a.get(key), b.get(key)
            if (
                not argv_problems(left, key)
                and not argv_problems(right, key)
                and all(absolute_root(root) for root in roots)
            ):
                if normalized_argv(left, roots[0]) != normalized_argv(right, roots[1]):
                    problems.append(f"builds differ in normalized {key}")
        first_finish, second_start = a.get("build_finished_monotonic_ns"), b.get("build_started_monotonic_ns")
        if integer(first_finish, positive=True) and integer(second_start) and second_start < first_finish:
            problems.append("builds overlap; controls must be built serially")
        binary_a, binary_b = a.get("binary"), b.get("binary")
        if isinstance(binary_a, dict) and isinstance(binary_b, dict) and binary_a.get("path") == binary_b.get("path"):
            problems.append("frozen build binaries must have distinct paths")

    staging = value.get("staging_paths")
    if not isinstance(staging, dict) or set(staging) != {"path0", "path1"}:
        problems.append("staging_paths must contain exactly path0 and path1")
    else:
        for key in ("path0", "path1"):
            if not absolute_root(staging[key]):
                problems.append(f"staging_paths.{key} must be a normalized absolute path")
        if staging["path0"] == staging["path1"]:
            problems.append("staging paths must be distinct")
        for label, build in (("A", a), ("B", b)):
            if isinstance(build, dict) and isinstance(build.get("binary"), dict):
                if build["binary"].get("path") in staging.values():
                    problems.append(f"builds.{label}.binary.path collides with a mutable staging path")

    shaped = _aa_shape(value)
    aa_problems, aa_invalid = validate_aa_capture(shaped)
    problems.extend(aa_problems)
    invalid = list(aa_invalid)
    observations = value.get("observations")
    if isinstance(observations, list):
        schedule = expected_schedule()
        hashes = {
            label: (
                build["binary"].get("sha256")
                if isinstance(build, dict) and isinstance(build.get("binary"), dict)
                else None
            )
            for label, build in (("A", a), ("B", b))
        }
        for index, observation in enumerate(observations[:TOTAL_PAIRS]):
            if not isinstance(observation, dict):
                continue
            for field in (
                "first_wall_ns", "second_wall_ns",
                "first_peak_rss_bytes", "second_peak_rss_bytes",
            ):
                if integer(observation.get(field)) and observation[field] > MAX_U64:
                    problems.append(f"observation {index}.{field} exceeds uint64")
            reasons = observation_reasons(observation, str(value.get("expected_output_sha256")))
            labels = derived_labels(schedule[index])
            for position, label in zip(("first", "second"), labels):
                observed = observation.get(f"{position}_binary_sha256")
                if not SHA256_RE.fullmatch(str(observed)):
                    reasons.append(f"{position} binary digest is unavailable")
                elif hashes[label] is not None and observed != hashes[label]:
                    reasons.append(f"{position} binary digest differs from build {label}")
            if observation.get("invalid_reasons") != reasons:
                problems.append(f"observation {index}.invalid_reasons do not replay")
            if observation.get("valid") is not (not reasons):
                problems.append(f"observation {index}.valid does not replay")
            base_reasons = observation_reasons(observation, str(value.get("expected_output_sha256")))
            invalid.extend(f"observation {index}: {reason}" for reason in reasons if reason not in base_reasons)
        if observations and isinstance(observations[0], dict) and isinstance(b, dict):
            first_start = observations[0].get("started_monotonic_ns")
            last_build_finish = b.get("build_finished_monotonic_ns")
            if integer(first_start) and integer(last_build_finish, positive=True) and first_start < last_build_finish:
                problems.append("timing begins before both builds finish")
    declared = value.get("invalid_reasons")
    if isinstance(declared, list):
        missing = [reason for reason in invalid if reason not in declared]
        if missing:
            problems.append("invalid_reasons omits replayed failures: " + "; ".join(missing))
    return problems, invalid


def _rename_partition(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        {("median_build_effect" if key == "median_label_effect" else key): value for key, value in row.items()}
        for row in rows
    ]


def analyze_capture(capture: dict[str, Any], capture_sha256: str) -> dict[str, Any]:
    problems, invalid = validate_capture(capture)
    if problems:
        raise NoiseError("cross-build replay failed: " + "; ".join(problems))
    analysis: dict[str, Any] = {
        "schema": ANALYSIS_SCHEMA,
        "version": 1,
        "capture_sha256": capture_sha256,
        "control_kind": capture["control_kind"],
        "capture_status": capture["capture_status"],
        "analysis_status": "invalid" if invalid else "descriptive-complete",
        "invalid_reasons": capture["invalid_reasons"],
        "retained_pair_slots": TOTAL_PAIRS,
        "deleted_pair_slots": 0,
        "performance_decision": "not-evaluated",
        "interpretation": (
            "Same-source build sensitivity only. Do not subtract a control median, infer "
            "layout-independent speedup, or treat a complete record as acceptance."
        ),
        "metrics": None,
    }
    if invalid:
        return analysis
    a, b = capture["builds"]["A"], capture["builds"]["B"]
    analysis["builds"] = {
        "binary_sha256_equal": a["binary"]["sha256"] == b["binary"]["sha256"],
        "text_section_file_offset_delta": b["text_section"]["file_offset"] - a["text_section"]["file_offset"],
        "text_section_virtual_address_delta": (
            b["text_section"]["virtual_address"] - a["text_section"]["virtual_address"]
        ),
        "text_section_size_delta": b["text_section"]["size"] - a["text_section"]["size"],
        "text_section_sha256_equal": a["text_section"]["sha256"] == b["text_section"]["sha256"],
    }
    metrics: dict[str, Any] = {}
    for metric in capture["metric_family"]:
        rows, centers = metric_rows(capture, metric)
        blocks = per_partition(rows, "block")
        overall_center = statistics.median(centers)
        metrics[metric] = {
            "build_effect": effect_summary([row["label_effect"] for row in rows]),
            "path_effect": effect_summary([row["path_effect"] for row in rows]),
            "order_effect": effect_summary([row["order_effect"] for row in rows]),
            "pair_center": {
                "median": overall_center,
                "minimum": min(centers),
                "maximum": max(centers),
                "lag_one_correlation": lag_one_correlation(centers),
                "relative_linear_drift_per_pair": relative_drift_per_pair(centers),
                "maximum_absolute_block_median_shift": max(
                    abs((row["median_pair_center"] - overall_center) / overall_center) for row in blocks
                ),
            },
            "observed_absolute_build_difference_p95": nearest_rank([abs(row["label_effect"]) for row in rows], 0.95),
            "per_round": _rename_partition(per_partition(rows, "round")),
            "per_block": _rename_partition(blocks),
        }
    analysis["metrics"] = metrics
    return analysis


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    subparsers = parser.add_subparsers(dest="command")
    validate = subparsers.add_parser("validate", help="replay a raw cross-build capture")
    validate.add_argument("capture", type=Path)
    analyze = subparsers.add_parser("analyze", help="write the descriptive build-control model")
    analyze.add_argument("capture", type=Path)
    analyze.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.self_test:
            from zen5_build_control_test import run_self_test

            return run_self_test()
        if arguments.command in {"validate", "analyze"}:
            capture = load_json(arguments.capture)
            problems, _ = validate_capture(capture)
            if problems:
                for problem in problems:
                    print(problem, file=sys.stderr)
                return 2
            if arguments.command == "validate":
                print(f"valid {CAPTURE_SCHEMA}: {arguments.capture}")
                return 0
            analysis = analyze_capture(capture, sha256_file(arguments.capture))
            write_json(arguments.output, analysis)
            print(f"{analysis['analysis_status']}: {arguments.output}")
            return 0 if analysis["analysis_status"] == "descriptive-complete" else 2
        raise NoiseError("select validate, analyze, or --self-test")
    except (NoiseError, FileNotFoundError, PermissionError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
