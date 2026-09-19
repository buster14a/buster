#!/usr/bin/env python3
"""Validate and summarize fixed-count Ryzen 7 9700X immutable-binary A/A evidence.

The input is produced by an admitted execution service, not by this tool.  The
validator requires two rounds of four separated swap blocks and sixty pairs per
round.  Every planned slot remains present.  Invalid slots make the experiment
invalid; they are never deleted before analysis.

This is an empirical noise description.  It does not change the ordinary CI
guard, establish equivalence, or issue a native-retirement performance verdict.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import sys
from typing import Any


CAPTURE_SCHEMA = "buster-zen5-aa-capture-v1"
ANALYSIS_SCHEMA = "buster-zen5-aa-noise-model-v1"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
MAX_JSON_BYTES = 64 * 1024 * 1024
ROUNDS = 2
BLOCKS_PER_ROUND = 4
PAIRS_PER_BLOCK = 15
PAIRS_PER_ROUND = BLOCKS_PER_ROUND * PAIRS_PER_BLOCK
TOTAL_PAIRS = ROUNDS * PAIRS_PER_ROUND
ASSIGNMENTS = (
    ("normal", "normal"),
    ("swapped", "normal"),
    ("normal", "swapped"),
    ("swapped", "swapped"),
)


class NoiseError(ValueError):
    """A deterministic capture, validation, or replay error."""


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            if size > MAX_JSON_BYTES:
                raise NoiseError(f"input exceeds {MAX_JSON_BYTES} bytes: {path}")
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> Any:
    raw = path.read_bytes()
    if len(raw) > MAX_JSON_BYTES:
        raise NoiseError(f"JSON exceeds {MAX_JSON_BYTES} bytes: {path}")
    try:
        return json.loads(raw)
    except json.JSONDecodeError as error:
        raise NoiseError(f"invalid JSON in {path}: {error}") from error


def write_json(path: Path, value: Any) -> None:
    payload = canonical_bytes(value)
    if len(payload) > MAX_JSON_BYTES:
        raise NoiseError(f"output exceeds {MAX_JSON_BYTES} bytes")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    with temporary.open("xb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def integer(value: Any, *, positive: bool = False) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= (1 if positive else 0)


def expected_schedule() -> list[dict[str, Any]]:
    schedule: list[dict[str, Any]] = []
    sequence = 0
    for round_index in range(ROUNDS):
        for block_index, (label_assignment, path_assignment) in enumerate(ASSIGNMENTS):
            for pair_in_block in range(PAIRS_PER_BLOCK):
                order = "AB" if (round_index + block_index + pair_in_block) % 2 == 0 else "BA"
                schedule.append(
                    {
                        "sequence": sequence,
                        "round": round_index,
                        "block": block_index,
                        "pair_in_block": pair_in_block,
                        "order": order,
                        "label_assignment": label_assignment,
                        "path_assignment": path_assignment,
                    }
                )
                sequence += 1
    return schedule


def schedule_sha256() -> str:
    return sha256_bytes(canonical_bytes(expected_schedule()))


def derived_labels(slot: dict[str, Any]) -> tuple[str, str]:
    return ("A", "B") if slot["order"] == "AB" else ("B", "A")


def label_to_side(label: str, assignment: str) -> str:
    if assignment == "normal":
        return "left" if label == "A" else "right"
    return "right" if label == "A" else "left"


def side_to_path(side: str, assignment: str) -> str:
    if assignment == "normal":
        return "path0" if side == "left" else "path1"
    return "path1" if side == "left" else "path0"


def derived_paths(slot: dict[str, Any]) -> tuple[str, str]:
    first_label, second_label = derived_labels(slot)
    first_side = label_to_side(first_label, slot["label_assignment"])
    second_side = label_to_side(second_label, slot["label_assignment"])
    return side_to_path(first_side, slot["path_assignment"]), side_to_path(second_side, slot["path_assignment"])


def identity_problems(value: Any, name: str) -> list[str]:
    problems: list[str] = []
    if not isinstance(value, dict):
        return [f"{name} must be an object"]
    if not isinstance(value.get("path"), str) or not value["path"]:
        problems.append(f"{name}.path is malformed")
    if not integer(value.get("size")):
        problems.append(f"{name}.size is malformed")
    if not integer(value.get("mode")) or value["mode"] > 0o7777:
        problems.append(f"{name}.mode is malformed")
    if not SHA256_RE.fullmatch(str(value.get("sha256"))):
        problems.append(f"{name}.sha256 is malformed")
    return problems


def plan_problems(plan: Any) -> list[str]:
    problems: list[str] = []
    if not isinstance(plan, dict):
        return ["plan must be an object"]
    expected_scalars = {
        "rounds": ROUNDS,
        "blocks_per_round": BLOCKS_PER_ROUND,
        "pairs_per_block": PAIRS_PER_BLOCK,
        "pairs_per_round": PAIRS_PER_ROUND,
        "total_pairs": TOTAL_PAIRS,
        "schedule_sha256": schedule_sha256(),
        "stopping_rule": "fixed-count-no-optional-stopping",
        "outlier_policy": "retain-all-no-deletion",
        "decision": "descriptive-only",
    }
    for key, expected in expected_scalars.items():
        if plan.get(key) != expected:
            problems.append(f"plan.{key} must be {expected!r}")
    if not integer(plan.get("minimum_interblock_gap_ns"), positive=True):
        problems.append("plan.minimum_interblock_gap_ns must be positive")
    return problems


def observation_reasons(observation: dict[str, Any], expected_output_sha256: str) -> list[str]:
    reasons: list[str] = []
    for position in ("first", "second"):
        exit_status = observation.get(f"{position}_exit_status")
        wall = observation.get(f"{position}_wall_ns")
        rss = observation.get(f"{position}_peak_rss_bytes")
        output = observation.get(f"{position}_output_sha256")
        if not isinstance(exit_status, int) or isinstance(exit_status, bool):
            reasons.append(f"{position} exit status is unavailable")
        elif exit_status != 0:
            reasons.append(f"{position} execution exited with status {exit_status}")
        if not integer(wall, positive=True):
            reasons.append(f"{position} wall time is unavailable or nonpositive")
        if not integer(rss, positive=True):
            reasons.append(f"{position} peak RSS is unavailable or nonpositive")
        if output != expected_output_sha256:
            reasons.append(f"{position} output digest mismatch")
    return reasons


def validate_capture(value: Any) -> tuple[list[str], list[str]]:
    """Return ``(structural_problems, replayed_invalid_reasons)``."""

    problems: list[str] = []
    derived_invalid: list[str] = []
    if not isinstance(value, dict) or value.get("schema") != CAPTURE_SCHEMA:
        return [f"capture schema must be {CAPTURE_SCHEMA}"], []
    if value.get("version") != 1:
        problems.append("capture version must be 1")
    if value.get("purpose") != "immutable-binary A/A noise calibration":
        problems.append("capture purpose is malformed")
    if value.get("metric_family") != ["wall_time_ns", "peak_rss_bytes"]:
        problems.append("metric_family must contain wall_time_ns and peak_rss_bytes")
    if value.get("statistical_decision") != "not-evaluated":
        problems.append("A/A capture must not contain a performance verdict")
    if value.get("ordinary_ci_guard_unchanged") is not True:
        problems.append("ordinary CI guard must remain unchanged")

    repository = value.get("repository")
    if not isinstance(repository, dict):
        problems.append("repository must be an object")
    else:
        if not COMMIT_RE.fullmatch(str(repository.get("revision"))):
            derived_invalid.append("repository revision is unavailable or malformed")
        if not COMMIT_RE.fullmatch(str(repository.get("tree"))):
            derived_invalid.append("repository tree is unavailable or malformed")
        if repository.get("status") != "":
            derived_invalid.append("repository checkout is not clean")

    for field in ("environment_fingerprint_sha256", "host_qualification_sha256", "source_identity_sha256"):
        if not SHA256_RE.fullmatch(str(value.get(field))):
            problems.append(f"{field} is malformed")

    binaries = value.get("binary_paths")
    if not isinstance(binaries, dict) or set(binaries) != {"path0", "path1"}:
        problems.append("binary_paths must contain exactly path0 and path1")
        binaries = {}
    for path_id in ("path0", "path1"):
        problems.extend(identity_problems(binaries.get(path_id), f"binary_paths.{path_id}"))
    if isinstance(binaries.get("path0"), dict) and isinstance(binaries.get("path1"), dict):
        if binaries["path0"].get("path") == binaries["path1"].get("path"):
            problems.append("path0 and path1 must use distinct pathnames")
        for field in ("size", "mode", "sha256"):
            if binaries["path0"].get(field) != binaries["path1"].get(field):
                derived_invalid.append(f"binary path identities differ in {field}")
    expected_output = value.get("expected_output_sha256")
    if not SHA256_RE.fullmatch(str(expected_output)):
        problems.append("expected_output_sha256 is malformed")
        expected_output = ""

    problems.extend(plan_problems(value.get("plan")))
    plan = value.get("plan") if isinstance(value.get("plan"), dict) else {}
    minimum_gap = plan.get("minimum_interblock_gap_ns") if integer(plan.get("minimum_interblock_gap_ns"), positive=True) else 1

    expected = expected_schedule()
    schedule = value.get("schedule")
    if schedule != expected:
        problems.append("schedule differs from the fixed version-1 schedule")

    observations = value.get("observations")
    if not isinstance(observations, list) or len(observations) != TOTAL_PAIRS:
        problems.append(f"observations must contain exactly {TOTAL_PAIRS} slots")
        observations = []
    previous_finish: int | None = None
    previous_block: tuple[int, int] | None = None
    for index, slot in enumerate(expected):
        if index >= len(observations):
            break
        observation = observations[index]
        prefix = f"observation {index}"
        if not isinstance(observation, dict):
            problems.append(f"{prefix} must be an object")
            continue
        for key, expected_value in slot.items():
            if observation.get(key) != expected_value:
                problems.append(f"{prefix}.{key} differs from the predeclared schedule")
        first_label, second_label = derived_labels(slot)
        first_path, second_path = derived_paths(slot)
        if observation.get("first_label") != first_label or observation.get("second_label") != second_label:
            problems.append(f"{prefix} logical label order is inconsistent")
        if observation.get("first_path") != first_path or observation.get("second_path") != second_path:
            problems.append(f"{prefix} path assignment is inconsistent")

        started = observation.get("started_monotonic_ns")
        finished = observation.get("finished_monotonic_ns")
        if not integer(started) or not integer(finished, positive=True) or finished <= started:
            problems.append(f"{prefix} monotonic bounds are malformed")
        elif previous_finish is not None:
            if started < previous_finish:
                problems.append(f"{prefix} overlaps or precedes the prior slot")
            current_block = (slot["round"], slot["block"])
            if current_block != previous_block and started - previous_finish < minimum_gap:
                derived_invalid.append(
                    f"{prefix}: inter-block gap {started - previous_finish} ns is below {minimum_gap} ns"
                )
        if integer(finished, positive=True):
            previous_finish = finished
        previous_block = (slot["round"], slot["block"])

        reasons = observation_reasons(observation, str(expected_output))
        declared_reasons = observation.get("invalid_reasons")
        if declared_reasons != reasons:
            problems.append(f"{prefix}.invalid_reasons do not replay")
        if observation.get("valid") is not (not reasons):
            problems.append(f"{prefix}.valid does not replay")
        derived_invalid.extend(f"{prefix}: {reason}" for reason in reasons)

    declared_invalid = value.get("invalid_reasons")
    if not isinstance(declared_invalid, list) or not all(isinstance(reason, str) and reason for reason in declared_invalid):
        problems.append("invalid_reasons must be a string list")
        declared_invalid = []
    elif len(declared_invalid) != len(set(declared_invalid)):
        problems.append("invalid_reasons contains duplicates")
    missing = [reason for reason in derived_invalid if reason not in declared_invalid]
    if missing:
        problems.append("invalid_reasons omits replayed failures: " + "; ".join(missing))

    status = value.get("capture_status")
    if status not in {"complete", "invalid"}:
        problems.append("capture_status must be complete or invalid")
    elif status == "complete" and (declared_invalid or derived_invalid):
        problems.append("complete capture contains invalidity reasons")
    elif status == "invalid" and not declared_invalid:
        problems.append("invalid capture must retain at least one reason")
    return problems, derived_invalid


def relative_difference(numerator: float, denominator: float) -> float:
    center = (numerator + denominator) * 0.5
    if center <= 0.0:
        raise NoiseError("relative difference requires positive observations")
    return (numerator - denominator) / center


def nearest_rank(values: list[float], probability: float) -> float:
    if not values:
        raise NoiseError("quantile requires observations")
    ordered = sorted(values)
    rank = max(1, math.ceil(probability * len(ordered)))
    return ordered[min(rank, len(ordered)) - 1]


def median_absolute_deviation(values: list[float]) -> float:
    center = statistics.median(values)
    return statistics.median(abs(value - center) for value in values)


def lag_one_correlation(values: list[float]) -> float | None:
    if len(values) < 3:
        return None
    left = values[:-1]
    right = values[1:]
    left_mean = statistics.fmean(left)
    right_mean = statistics.fmean(right)
    numerator = sum((a - left_mean) * (b - right_mean) for a, b in zip(left, right))
    left_norm = sum((a - left_mean) ** 2 for a in left)
    right_norm = sum((b - right_mean) ** 2 for b in right)
    denominator = math.sqrt(left_norm * right_norm)
    return numerator / denominator if denominator else None


def relative_drift_per_pair(values: list[float]) -> float | None:
    if len(values) < 2:
        return None
    x_mean = (len(values) - 1) * 0.5
    y_mean = statistics.fmean(values)
    denominator = sum((index - x_mean) ** 2 for index in range(len(values)))
    if denominator == 0.0 or y_mean == 0.0:
        return None
    slope = sum((index - x_mean) * (value - y_mean) for index, value in enumerate(values)) / denominator
    return slope / y_mean


def effect_summary(values: list[float]) -> dict[str, Any]:
    absolute = [abs(value) for value in values]
    return {
        "count": len(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "minimum": min(values),
        "maximum": max(values),
        "mad": median_absolute_deviation(values),
        "absolute_nearest_rank": {
            "p50": nearest_rank(absolute, 0.50),
            "p90": nearest_rank(absolute, 0.90),
            "p95": nearest_rank(absolute, 0.95),
            "p99": nearest_rank(absolute, 0.99),
            "maximum": max(absolute),
        },
    }


def metric_rows(capture: dict[str, Any], metric: str) -> tuple[list[dict[str, Any]], list[float]]:
    rows: list[dict[str, Any]] = []
    centers: list[float] = []
    suffix = "wall_ns" if metric == "wall_time_ns" else "peak_rss_bytes"
    for observation in capture["observations"]:
        first = float(observation[f"first_{suffix}"])
        second = float(observation[f"second_{suffix}"])
        first_label = observation["first_label"]
        first_path = observation["first_path"]
        label_values = {first_label: first, observation["second_label"]: second}
        path_values = {first_path: first, observation["second_path"]: second}
        center = (first + second) * 0.5
        centers.append(center)
        rows.append(
            {
                "sequence": observation["sequence"],
                "round": observation["round"],
                "block": observation["block"],
                "label_effect": relative_difference(label_values["B"], label_values["A"]),
                "path_effect": relative_difference(path_values["path1"], path_values["path0"]),
                "order_effect": relative_difference(second, first),
                "pair_center": center,
            }
        )
    return rows, centers


def per_partition(rows: list[dict[str, Any]], key: str) -> list[dict[str, Any]]:
    groups: dict[tuple[int, ...], list[dict[str, Any]]] = {}
    for row in rows:
        identity = (row["round"],) if key == "round" else (row["round"], row["block"])
        groups.setdefault(identity, []).append(row)
    result: list[dict[str, Any]] = []
    for identity in sorted(groups):
        group = groups[identity]
        entry: dict[str, Any] = {"round": identity[0], "count": len(group)}
        if len(identity) == 2:
            entry["block"] = identity[1]
        for effect in ("label_effect", "path_effect", "order_effect"):
            entry[f"median_{effect}"] = statistics.median(row[effect] for row in group)
        entry["median_pair_center"] = statistics.median(row["pair_center"] for row in group)
        result.append(entry)
    return result


def analyze_capture(capture: dict[str, Any], capture_sha256: str) -> dict[str, Any]:
    structural, derived_invalid = validate_capture(capture)
    if structural:
        raise NoiseError("capture replay failed: " + "; ".join(structural))
    invalid = list(capture.get("invalid_reasons", []))
    base = {
        "schema": ANALYSIS_SCHEMA,
        "version": 1,
        "capture_sha256": capture_sha256,
        "capture_status": capture["capture_status"],
        "analysis_status": "invalid" if invalid else "descriptive-complete",
        "invalid_reasons": invalid,
        "retained_pair_slots": TOTAL_PAIRS,
        "deleted_pair_slots": 0,
        "optional_stopping": False,
        "outlier_deletion": False,
        "performance_decision": "not-evaluated",
        "interpretation": (
            "Empirical same-binary noise only. Observed resolution is not a universal threshold, "
            "not demonstrated equivalence, and not a candidate verdict."
        ),
    }
    if invalid or derived_invalid:
        base["metrics"] = None
        return base

    metrics: dict[str, Any] = {}
    for metric in capture["metric_family"]:
        rows, centers = metric_rows(capture, metric)
        label = [row["label_effect"] for row in rows]
        path = [row["path_effect"] for row in rows]
        order = [row["order_effect"] for row in rows]
        block_rows = per_partition(rows, "block")
        overall_center = statistics.median(centers)
        block_center_shifts = [
            (entry["median_pair_center"] - overall_center) / overall_center for entry in block_rows
        ]
        metrics[metric] = {
            "label_effect": effect_summary(label),
            "path_effect": effect_summary(path),
            "order_effect": effect_summary(order),
            "pair_center": {
                "median": overall_center,
                "minimum": min(centers),
                "maximum": max(centers),
                "lag_one_correlation": lag_one_correlation(centers),
                "relative_linear_drift_per_pair": relative_drift_per_pair(centers),
                "maximum_absolute_block_median_shift": max(abs(value) for value in block_center_shifts),
            },
            "empirical_resolution_fraction": nearest_rank([abs(value) for value in label], 0.95),
            "per_round": per_partition(rows, "round"),
            "per_block": block_rows,
        }
    base["metrics"] = metrics
    return base



def self_test() -> int:
    from zen5_aa_noise_test import run_self_test

    return run_self_test()

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    subparsers = parser.add_subparsers(dest="command")
    validate = subparsers.add_parser("validate", help="replay a raw A/A capture")
    validate.add_argument("capture", type=Path)
    analyze = subparsers.add_parser("analyze", help="write the descriptive noise model")
    analyze.add_argument("capture", type=Path)
    analyze.add_argument("--output", type=Path, required=True)
    schedule = subparsers.add_parser("schedule", help="print the fixed version-1 schedule")
    schedule.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.self_test:
            return self_test()
        if arguments.command == "schedule":
            value = {
                "schema": "buster-zen5-aa-schedule-v1",
                "schedule_sha256": schedule_sha256(),
                "schedule": expected_schedule(),
            }
            if arguments.output:
                write_json(arguments.output, value)
            else:
                sys.stdout.buffer.write(canonical_bytes(value))
            return 0
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
        raise NoiseError("select validate, analyze, schedule, or --self-test")
    except (NoiseError, FileNotFoundError, PermissionError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
