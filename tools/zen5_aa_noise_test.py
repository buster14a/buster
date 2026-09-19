"""Offline regression tests for the fixed-count Zen 5 A/A record."""

from __future__ import annotations

from typing import Any

from zen5_aa_noise import (
    BLOCKS_PER_ROUND,
    CAPTURE_SCHEMA,
    PAIRS_PER_BLOCK,
    PAIRS_PER_ROUND,
    ROUNDS,
    TOTAL_PAIRS,
    analyze_capture,
    canonical_bytes,
    derived_labels,
    derived_paths,
    expected_schedule,
    observation_reasons,
    schedule_sha256,
    sha256_bytes,
    validate_capture,
)

def synthetic_capture(*, invalid: bool = False) -> dict[str, Any]:
    schedule = expected_schedule()
    observations: list[dict[str, Any]] = []
    now = 1_000_000_000
    gap = 2_000_000
    minimum_gap = 1_000_000
    previous_block: tuple[int, int] | None = None
    output_sha = "a" * 64
    for slot in schedule:
        block = (slot["round"], slot["block"])
        if previous_block is not None and block != previous_block:
            now += gap
        first_label, second_label = derived_labels(slot)
        first_path, second_path = derived_paths(slot)
        center = 1_000_000 + slot["sequence"] * 10
        first = center + (3 if first_label == "B" else -3) + (2 if first_path == "path1" else -2)
        second = center + (3 if second_label == "B" else -3) + (2 if second_path == "path1" else -2)
        started = now
        finished = started + first + second
        observation = {
            **slot,
            "first_label": first_label,
            "second_label": second_label,
            "first_path": first_path,
            "second_path": second_path,
            "started_monotonic_ns": started,
            "finished_monotonic_ns": finished,
            "first_exit_status": 0,
            "second_exit_status": 0,
            "first_wall_ns": first,
            "second_wall_ns": second,
            "first_peak_rss_bytes": 100_000_000 + (1 if first_label == "B" else 0),
            "second_peak_rss_bytes": 100_000_000 + (1 if second_label == "B" else 0),
            "first_output_sha256": output_sha,
            "second_output_sha256": output_sha,
            "valid": True,
            "invalid_reasons": [],
        }
        observations.append(observation)
        now = finished + 1000
        previous_block = block
    capture = {
        "schema": CAPTURE_SCHEMA,
        "version": 1,
        "purpose": "immutable-binary A/A noise calibration",
        "metric_family": ["wall_time_ns", "peak_rss_bytes"],
        "repository": {"revision": "b" * 40, "tree": "c" * 40, "status": ""},
        "environment_fingerprint_sha256": "d" * 64,
        "host_qualification_sha256": "e" * 64,
        "source_identity_sha256": "f" * 64,
        "binary_paths": {
            "path0": {"path": "/immutable/a/compiler", "size": 100, "mode": 0o555, "sha256": "1" * 64},
            "path1": {"path": "/immutable/b/compiler", "size": 100, "mode": 0o555, "sha256": "1" * 64},
        },
        "expected_output_sha256": output_sha,
        "plan": {
            "rounds": ROUNDS,
            "blocks_per_round": BLOCKS_PER_ROUND,
            "pairs_per_block": PAIRS_PER_BLOCK,
            "pairs_per_round": PAIRS_PER_ROUND,
            "total_pairs": TOTAL_PAIRS,
            "minimum_interblock_gap_ns": minimum_gap,
            "schedule_sha256": schedule_sha256(),
            "stopping_rule": "fixed-count-no-optional-stopping",
            "outlier_policy": "retain-all-no-deletion",
            "decision": "descriptive-only",
        },
        "schedule": schedule,
        "observations": observations,
        "ordinary_ci_guard_unchanged": True,
        "statistical_decision": "not-evaluated",
        "capture_status": "complete",
        "invalid_reasons": [],
    }
    if invalid:
        observation = capture["observations"][7]
        observation["second_exit_status"] = 9
        reasons = observation_reasons(observation, output_sha)
        observation["valid"] = False
        observation["invalid_reasons"] = reasons
        capture["capture_status"] = "invalid"
        capture["invalid_reasons"] = [f"observation 7: {reason}" for reason in reasons]
    return capture


def run_self_test() -> int:
    assert len(expected_schedule()) == TOTAL_PAIRS
    assert len({(slot["round"], slot["block"]) for slot in expected_schedule()}) == ROUNDS * BLOCKS_PER_ROUND
    capture = synthetic_capture()
    problems, invalid = validate_capture(capture)
    assert not problems and not invalid
    analysis = analyze_capture(capture, sha256_bytes(canonical_bytes(capture)))
    assert analysis["analysis_status"] == "descriptive-complete"
    assert analysis["retained_pair_slots"] == TOTAL_PAIRS
    assert analysis["metrics"]["wall_time_ns"]["label_effect"]["count"] == TOTAL_PAIRS
    assert analysis["metrics"]["wall_time_ns"]["pair_center"]["relative_linear_drift_per_pair"] > 0.0

    invalid_capture = synthetic_capture(invalid=True)
    problems, invalid = validate_capture(invalid_capture)
    assert not problems and invalid
    invalid_analysis = analyze_capture(invalid_capture, sha256_bytes(canonical_bytes(invalid_capture)))
    assert invalid_analysis["analysis_status"] == "invalid"
    assert invalid_analysis["metrics"] is None

    corrupted = synthetic_capture()
    corrupted["observations"][0]["order"] = "BA"
    problems, _ = validate_capture(corrupted)
    assert any("predeclared schedule" in problem for problem in problems)

    stopped = synthetic_capture()
    stopped["observations"].pop()
    problems, _ = validate_capture(stopped)
    assert any("exactly 120 slots" in problem for problem in problems)

    print("zen5_aa_noise self-test passed")
    return 0


