#!/usr/bin/env python3
"""Independent data validator for disabled #3212 sampling research packets.

Ownership: trusted tools/bench_direct publisher. No process execution, admission,
GitHub lookup, retry or profile activation. The caller supplies facts obtained
from authenticated GitHub request/executor history, never from candidate files.
A valid packet remains unqualified research; campaign inference needs separate
real-host, calibration, independence and corpus review.

Map: schedule derives slots/reservations; validate_packet joins trusted facts,
raw compare configuration and paired outputs; read_packet parses bounded data.
Statistics are recomputed with the existing trusted uarch_lab implementation.
"""

from __future__ import annotations

import csv
import importlib.util
import json
import math
import re
from pathlib import Path

from compiler_receipt import APPROVED_HOST, LAB_SCHEMA, MIN_PAIRS, classify

PROTOCOL = "buster-compiler-main-sampling-qualification-v1"
SCHEMA = "buster-main-sampling-packet-v1"
EVIDENCE_CLASS = "unqualified-sampling-research"
SHORT = "compiler-main-40pairs-candidate-v1"
LONG = "compiler-compare-v1"
LARGE = "compiler-main-80pairs-candidate-v1"
REQUEST_SELECTORS = {"pilot": "profile: compiler-main-sampling-pilot-v1",
                     "confirm": "profile: compiler-main-sampling-confirm-v1"}
ROW_LIMIT = 4096
FILE_LIMIT = 32 * 1024 * 1024
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")

# Fixed trusted sibling, never a script path from the evidence or request.
_spec = importlib.util.spec_from_file_location("_sampling_trusted_lab", Path(__file__).resolve().parents[1] / "uarch_lab.py")
_lab = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lab)


def schedule(phase: str, packet: int) -> dict:
    result = {}
    if type(packet) is int and phase == "pilot" and 0 <= packet < 3:
        result = {"family": ("aa", "ab1", "ab2")[packet], "reservation_seconds": 3600,
                  "slots": [(LONG, 0, 0), (SHORT, 0, 40), (LARGE, 0, 80)]}
    elif type(packet) is int and phase == "confirm" and 0 <= packet < 40:
        family = 1 if packet % 4 == 1 else 2 if packet % 4 == 3 else 0
        block = packet // 4
        count = 3 if packet in (37, 39) else 4
        comparator = block < 3 and packet % 4 != 2
        ordinal = block * 4 if family else (block * 2 + (packet % 4 == 2)) * 4
        slots = [(SHORT, ordinal + index, 40) for index in range(count)]
        if comparator:
            slots.insert(0 if (block + family) % 2 == 0 else len(slots), (LONG, block, 0))
        result = {"family": ("aa", "ab1", "ab2")[family],
                  "reservation_seconds": 1440 if comparator else 960, "slots": slots}
    return result


def number(value: object, positive: bool = False) -> bool:
    result = type(value) in (int, float) and -1e18 <= value <= 1e18 and math.isfinite(value)
    return result and (value > 0 if positive else value >= 0)


def integer(value: object) -> int | None:
    result = None
    if isinstance(value, str) and len(value) <= 20 and re.fullmatch(r"0|[1-9][0-9]*", value):
        result = int(value)
    return result


def _history_problems(history: object, request: dict, executor: dict) -> list[str]:
    problems = []
    if not isinstance(history, list) or not history or len(history) > 43:
        problems.append("authenticated attempted-packet history is missing or oversized")
        history = []
    phases = {"pilot": [], "confirm": []}
    seen_runs = set()
    current = []
    for row in history:
        if not isinstance(row, dict):
            problems.append("authenticated attempt row is not an object")
            continue
        phase, packet = row.get("phase"), row.get("packet")
        planned = schedule(phase, packet)
        key = (str(row.get("run_id")), str(row.get("run_attempt")))
        if not planned or key in seen_runs or row.get("run_attempt") != "1":
            problems.append("authenticated attempt is undeclared, duplicate, or a forbidden rerun")
        seen_runs.add(key)
        if phase in phases:
            phases[phase].append(packet)
        if row.get("reservation_seconds") != planned.get("reservation_seconds"):
            problems.append("authenticated attempt reservation contradicts deterministic schedule")
        if row.get("state") not in ("complete", "failed", "cancelled", "invalid", "incomplete", "not_run"):
            problems.append("authenticated attempt state is missing or unknown")
        occupancy = row.get("physical_packet_wall_us")
        if occupancy is not None and (not number(occupancy) or occupancy > planned.get("reservation_seconds", 0) * 1000000):
            problems.append("authenticated attempt occupancy exceeds its charged reservation")
        if phase == request.get("phase") and packet == request.get("packet"):
            current.append(row)
    for phase, packets in phases.items():
        if packets != list(range(len(packets))):
            problems.append(f"{phase} packet history is not an immutable chronological prefix")
    if request.get("phase") == "pilot" and phases["confirm"]:
        problems.append("pilot request follows confirmatory attempts")
    if request.get("phase") == "confirm" and phases["pilot"] != [0, 1, 2]:
        problems.append("confirmation lacks every charged pilot attempt")
    if len(current) != 1 or any(current[0].get(key) != executor.get(key) for key in ("run_id", "run_attempt")):
        problems.append("packet has no unique matching authenticated executor attempt")
    if len(current) == 1 and current[0].get("state") != "complete":
        problems.append("current authenticated executor attempt is incomplete or failed")
    return problems


def _series_problems(bundle: object, slot: tuple, binaries: dict, workload: dict, ownership: dict) -> tuple[list[str], dict]:
    problems = []
    shown = {"profile": slot[0], "ordinal": slot[1], "complete_pairs": None, "uncertainty": None,
             "outcome": None, "phase_metrics": None, "counters": None, "phases": None}
    if not isinstance(bundle, dict):
        bundle = {}
    raw, pairs, summary = bundle.get("compare"), bundle.get("pairs"), bundle.get("summary")
    if not all(isinstance(value, dict) for value in (raw, summary)) or not isinstance(pairs, list):
        problems.append("raw compare.json, pairs.json or summary.json is missing or malformed")
        raw, pairs, summary = {}, [], {}
    config = raw.get("config")
    expected = dict(workload, cpu=2, pairs=slot[2] or None, target_minutes=10, warmups=1,
                    seed=20261003, profile_steps=[], sudo=False, require_identical_output=False,
                    canonical_inline_pair=False, fresh_copy=True, min_effect_percent=0.5, process_ownership=ownership)
    if config != expected:
        problems.append("raw compare configuration does not exactly match the frozen profile/workload")
    raw_ownership = config.get("process_ownership") if isinstance(config, dict) else None
    if not isinstance(raw_ownership, dict) or type(raw_ownership.get("group")) is not int:
        problems.append("raw experimental process ownership is missing, unversioned or not an integer group")
    if raw.get("version") != 1 or raw.get("mode") != "compare":
        problems.append("raw comparison schema/mode is invalid")
    plan, summary_plan = raw.get("plan"), summary.get("plan")
    if not isinstance(plan, dict):
        plan = {}
    if not isinstance(summary_plan, dict):
        summary_plan = {}
    count = plan.get("pairs")
    if type(count) is not int or count < MIN_PAIRS or count > ROW_LIMIT // 2 or (slot[2] and count != slot[2]):
        problems.append("planned pair count contradicts the predeclared profile")
    if plan.get("order") != "ABBA" or plan.get("fresh_copy") is not True:
        problems.append("raw pair order/fresh-copy plan is not the immutable plan")
    if summary_plan != dict(plan, seed=20261003, confidence=0.95, bootstrap_resamples=2000,
                            complete_pairs=count, fresh_copy=True):
        problems.append("summary inference settings/counts contradict the raw pair plan")
    steps = raw.get("steps")
    if not isinstance(steps, dict) or set(steps) != {"env", "prepare", "timed"} or any(
            not isinstance(state, dict) or state.get("status") != "ok" for state in (steps or {}).values()):
        problems.append("raw comparison has missing, extra or incomplete required steps")
    if summary.get("steps") != {"env": "ok", "prepare": "ok", "timed": "ok"}:
        problems.append("summary required steps are incomplete")
    variants = raw.get("variants")
    variants = variants if isinstance(variants, dict) else {}
    for key, role in (("a", "baseline"), ("b", "candidate")):
        recorded = binaries.get(role, {})
        recorded = recorded if isinstance(recorded, dict) else {}
        variant = variants.get(key, {})
        if not isinstance(variant, dict) or variant.get("role") != role or any(
                variant.get(field) != recorded.get(field) for field in ("sha256", "size_bytes")):
            problems.append(f"raw {role} binary identity differs from the frozen built binary")
        info = summary.get(role, {})
        if not isinstance(info, dict) or info.get("runs") != count or info.get("identical_runs") != count:
            problems.append(f"summary {role} run/deterministic-output counts do not cover the exact plan")
    problems.extend(classify(summary, binaries))
    if summary.get("schema") != LAB_SCHEMA or summary.get("cpu") != 2 or any(
            summary.get(key) != workload.get(key) for key in ("command", "repo_root")):
        problems.append("summary workload/source/cpu identity differs from frozen raw configuration")
    host = summary.get("host")
    if not isinstance(host, dict) or not APPROVED_HOST.search(str(host.get("cpu_model", ""))):
        problems.append("summary did not observe the approved Ryzen 7 9700X")
    if not isinstance(host, dict) or host.get("git_revision") != binaries.get("baseline", {}).get("revision"):
        problems.append("summary workload source revision differs from the frozen baseline")
    if type(count) is not int or len(pairs) != 2 * count:
        problems.append("raw pair stream is truncated, duplicated or oversized")
    grouped = []
    raw_good = type(count) is int and MIN_PAIRS <= count <= ROW_LIMIT // 2 and len(pairs) == count * 2
    if raw_good:
        for index in range(count):
            order = "AB" if (index + 1) % 2 else "BA"
            members = {}
            for member, key in zip(pairs[index * 2:index * 2 + 2], order.lower()):
                valid = isinstance(member, dict) and member.get("pair") == index + 1 and member.get("variant") == key and \
                    member.get("order") == order and type(member.get("exit")) is int and member["exit"] == 0 and \
                    member.get("identical") is True and number(member.get("span_s"), positive=True) and \
                    1e-9 <= member["span_s"] <= 3600
                if not valid:
                    raw_good = False
                else:
                    members[key] = member
            if len(members) == 2:
                grouped.append({"pair": index + 1, "order": order,
                                "metrics_a": {"wall": members["a"]["span_s"]},
                                "metrics_b": {"wall": members["b"]["span_s"]}})
    if not raw_good:
        problems.append("raw paired samples are missing, unordered, failed, non-positive or not deterministic")
    else:
        wall = _lab.compare_series([(pair["metrics_a"]["wall"], pair["metrics_b"]["wall"]) for pair in grouped],
                                   "s", "lower", 20261003, time_metric=True, floor=0.005)
        verdict = summary.get("verdict")
        if not isinstance(verdict, dict) or verdict.get("n") != count or verdict.get("min_effect_percent") != 0.5 or any(
                verdict.get(key) != wall.get(key) for key in ("ratio", "ci_low", "ci_high", "ci_coverage", "change_percent", "outcome")):
            problems.append("summary verdict counts/floor/inference contradict independently reconstructed wall evidence")
        shown["complete_pairs"] = count
        shown["uncertainty"] = {"ratio": wall["ratio"], "ci_low": wall["ci_low"], "ci_high": wall["ci_high"],
                                "half_width_percentage_points": (wall["ci_high"] - wall["ci_low"]) * 50}
        metrics = summary.get("metrics")
        reported = metrics.get("wall", {}) if isinstance(metrics, dict) else {}
        if not isinstance(reported, dict) or any(reported.get(key) != value for key, value in wall.items()):
            problems.append("reported wall inference contradicts independent raw-pair reconstruction")
        checks = _lab.compare_checks(grouped)
        if summary.get("checks") != checks or any(check.get("checked") is not True or check.get("flag") is not False for check in checks.values()):
            problems.append("order/drift checks were not evaluated, were tampered with or flagged")
        if _lab.classify(wall["bootstrap_ci_low"], wall["bootstrap_ci_high"], True, 0.005) != wall["outcome"]:
            problems.append("bootstrap and sign-test wall outcomes disagree")
        shown["outcome"] = wall["outcome"]
    for key in ("phase_metrics", "counters", "phases"):
        shown[key] = summary.get(key)  # Unavailable remains null/explicit NA, never zero.
    return problems, shown


def validate_packet(identity: object, attempts: object, terminal: object, series: object, trusted: object) -> dict:
    """Consume data using previously authenticated trusted facts; never qualifies a campaign."""
    problems = []
    trusted = trusted if isinstance(trusted, dict) else {}
    request = trusted.get("request") if isinstance(trusted.get("request"), dict) else {}
    executor = trusted.get("executor") if isinstance(trusted.get("executor"), dict) else {}
    frozen = trusted.get("identity") if isinstance(trusted.get("identity"), dict) else {}
    binaries = trusted.get("binaries") if isinstance(trusted.get("binaries"), dict) else {}
    workload = trusted.get("workload_config") if isinstance(trusted.get("workload_config"), dict) else {}
    planned = schedule(request.get("phase"), request.get("packet"))
    if trusted.get("authenticated") is not True or not planned:
        problems.append("trusted authenticated request/executor facts or deterministic packet plan are missing")
    if request.get("selector") != REQUEST_SELECTORS.get(request.get("phase")) or not request.get("owner") or request.get("actor") != request.get("owner"):
        problems.append("request is not the exact authenticated owner qualification selector")
    for key in ("repository", "request_run_id"):
        if not request.get(key) or request.get(key) != executor.get(key):
            problems.append(f"authenticated request/executor {key} join failed")
    if executor.get("run_attempt") != "1" or not re.fullmatch(r"[1-9][0-9]*", str(executor.get("run_id", ""))):
        problems.append("executor attempt is unknown or a forbidden rerun")
    if not HEX40.fullmatch(str(request.get("freeze_revision", ""))) or not HEX40.fullmatch(str(request.get("request_head", ""))):
        problems.append("request lacks committed freeze and head source identities")
    if not APPROVED_HOST.search(str(executor.get("cpu_model", ""))):
        problems.append("authenticated executor did not observe the approved Ryzen 7 9700X")
    problems.extend(_history_problems(trusted.get("attempts"), request, executor))
    group = integer(identity.get("process_owner_group")) if isinstance(identity, dict) else None
    if group is None or group <= 1 or group > 2147483647 or type(executor.get("process_owner_group")) is not int or group != executor.get("process_owner_group"):
        problems.append("packet process owner group is missing, malformed or differs from the authenticated native owner receipt")
    ownership = {"schema": "compiler-experiment-owner-group-v1", "group": group, "failure_policy": "abort-owned-group"}
    identity_static = {key: value for key, value in identity.items() if key != "process_owner_group"} if isinstance(identity, dict) else {}
    frozen_static = {key: value for key, value in frozen.items() if key != "process_owner_group"}
    if "process_owner_group" in frozen and frozen["process_owner_group"] != str(group):
        problems.append("trusted packet owner group contradicts the authenticated executor group")
    expected_constants = {"schema": SCHEMA, "phase": request.get("phase"), "packet": str(request.get("packet")),
                          "family": planned.get("family"), "trials": str(len(planned.get("slots", []))),
                          "reservation_seconds": str(planned.get("reservation_seconds")), "cpu": "2",
                          "warmups": "1", "seed": "20261003", "floor_percent": "0.5", "fresh_copy": "true",
                          "routine_enabled": "false", "evidence_class": EVIDENCE_CLASS,
                          "campaign": request.get("campaign"), "freeze_sha256": request.get("campaign"),
                          "request_head": request.get("request_head")}
    if not isinstance(identity, dict) or identity_static != frozen_static or any(frozen.get(key) != value for key, value in expected_constants.items()):
        problems.append("packet identity contradicts authenticated frozen identities or predeclared schedule")
    for key in ("base", "base_tree", "baseline_revision", "candidate_revision", "trusted_revision"):
        if not HEX40.fullmatch(str(frozen.get(key, ""))):
            problems.append(f"frozen {key} source identity is missing or malformed")
    for key in ("baseline_sha256", "candidate_sha256", "lab_sha256", "protocol_sha256", "python_sha256",
                "driver_sha256", "closure_sha256", "freeze_sha256", "campaign"):
        if not HEX64.fullmatch(str(frozen.get(key, ""))):
            problems.append(f"frozen {key} artifact/closure identity is missing or malformed")
    for role in ("baseline", "candidate"):
        binary = binaries.get(role)
        if not isinstance(binary, dict) or binary.get("sha256") != frozen.get(role + "_sha256") or \
                binary.get("revision") != frozen.get(role + "_revision") or type(binary.get("size_bytes")) is not int or binary.get("size_bytes", 0) <= 0:
            problems.append(f"{role} built binary is not bound to frozen source/artifact identity")
    if frozen.get("baseline_revision") != frozen.get("base"):
        problems.append("baseline binary does not belong to the frozen workload base")
    if planned.get("family") == "aa" and any(frozen.get("baseline_" + key) != frozen.get("candidate_" + key) for key in ("revision", "sha256")):
        problems.append("A/A packet is not the same frozen binary identity")
    if set(workload) != {"command", "repo_root", "perf", "extra", "extra_by_variant"} or workload.get("extra") != [] or workload.get("extra_by_variant") != {"a": [], "b": []}:
        problems.append("trusted frozen workload configuration is incomplete or has undeclared flags")
    terminal = terminal if isinstance(terminal, dict) else {}
    occupancy = integer(terminal.get("physical_packet_wall_us"))
    preparation = integer(terminal.get("prep_us"))
    if occupancy is None or occupancy <= 0 or preparation is None or preparation > occupancy or occupancy != executor.get("physical_packet_wall_us") or occupancy > planned.get("reservation_seconds", 0) * 1000000:
        problems.append("physical occupancy/preparation is missing, differs from trusted observation or exceeds reservation")
    if terminal.get("within_reservation") != "true" or terminal.get("captured_input_files_unchanged") != "true" or terminal.get("process_state") != "complete" or terminal.get("qualification_state") != "unvalidated":
        problems.append("packet has changed inputs, incomplete processes or an unauthorized qualification claim")
    attempts = attempts if isinstance(attempts, list) else []
    series = series if isinstance(series, dict) else {}
    slots = planned.get("slots", [])
    if len(attempts) != len(slots) or set(series) != set(range(len(slots))):
        problems.append("every planned stream and attempted/not-run history must be retained")
    displayed = []
    measured_us = 0
    for index, slot in enumerate(slots):
        row = attempts[index] if index < len(attempts) and isinstance(attempts[index], dict) else {}
        expected = {"trial": str(index), "profile": slot[0], "family": planned["family"],
                    "ordinal": str(slot[1]), "pairs": str(slot[2])}
        if any(row.get(key) != value for key, value in expected.items()):
            problems.append(f"stream {index} contradicts immutable slot/family/profile/count")
        wall_us = integer(row.get("wall_us"))
        if wall_us is None or wall_us <= 0:
            problems.append(f"stream {index} measurement occupancy is unavailable")
        else:
            measured_us += wall_us
        if row.get("state") != "process-complete-unvalidated" or any(row.get(key) != "0" for key in ("exit_status", "timed_out", "cleanup_failed", "capture_failed")):
            problems.append(f"stream {index} is failed, cancelled, invalid, incomplete or not run")
        if row.get("closure_before") != frozen.get("closure_sha256") or row.get("closure_after") != frozen.get("closure_sha256"):
            problems.append(f"stream {index} source/toolchain/generated closure changed")
        issues, shown = _series_problems(series.get(index), slot, binaries, workload, ownership)
        problems.extend(f"stream {index}: {problem}" for problem in issues)
        bundle = series.get(index)
        raw_pairs = bundle.get("pairs") if isinstance(bundle, dict) else None
        if isinstance(raw_pairs, list) and all(isinstance(member, dict) and number(member.get("span_s")) for member in raw_pairs):
            paired_us = sum(member["span_s"] for member in raw_pairs) * 1000000
            if wall_us is None or paired_us > wall_us + len(raw_pairs):
                problems.append(f"stream {index} raw paired spans exceed measured process occupancy")
        cpu_status, memory_status = row.get("cpu_status"), row.get("memory_status")
        if cpu_status not in ("0", "1", "2", "3") or memory_status not in ("0", "1", "2", "3"):
            problems.append(f"stream {index} resource observation status is missing or invalid")
        cpu_values = [integer(row.get(key)) for key in ("user_cpu_us", "system_cpu_us")]
        memory_value = integer(row.get("peak_rss_bytes"))
        if cpu_status == "1" and any(value is None for value in cpu_values):
            problems.append(f"stream {index} observed CPU resource values are missing")
        if memory_status == "1" and (memory_value is None or memory_value <= 0):
            problems.append(f"stream {index} observed memory resource value is missing")
        shown.update(process_state=row.get("state"), measurement_wall_us=wall_us,
                     cpu_status=cpu_status, memory_status=memory_status,
                     user_cpu_us=cpu_values[0] if cpu_status == "1" else None,
                     system_cpu_us=cpu_values[1] if cpu_status == "1" else None,
                     peak_rss_bytes=memory_value if memory_status == "1" else None)
        displayed.append(shown)
    if occupancy is not None and preparation is not None and measured_us + preparation > occupancy:
        problems.append("series and preparation durations exceed complete physical occupancy")
    queue_delay = executor.get("queue_delay_seconds")
    if queue_delay is not None and not number(queue_delay):
        problems.append("authenticated queue delay is malformed")
        queue_delay = None
    return {"schema": "buster-main-sampling-packet-validation-v1",
            "packet_state": "incomplete" if problems else "complete-valid-research",
            "qualification_state": "unqualified", "evidence_class": EVIDENCE_CLASS, "routine_profile_enabled": False,
            "phase": request.get("phase"), "packet": request.get("packet"), "family": planned.get("family"),
            "reservation_seconds": planned.get("reservation_seconds"), "physical_packet_wall_us": occupancy,
            "prep_us": preparation, "queue_delay_seconds": queue_delay, "process_owner_group": group, "series": displayed, "problems": problems,
            "authenticated_attempt_history": trusted.get("attempts"),
            "outstanding_qualification": ["full required corpus", "independent near-boundary calibration",
                                         "between-trial independence review", "complete confirmatory campaign"]}


def _read(path: Path) -> str:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > FILE_LIMIT:
        raise ValueError(f"missing, linked or oversized evidence file: {path.name}")
    return path.read_text(encoding="utf-8")


def read_tsv(path: Path, table: bool = False) -> object:
    text = _read(path)
    rows = list(csv.reader(text.splitlines(), delimiter="\t"))
    if not rows or len(rows) > ROW_LIMIT or any(not row for row in rows):
        raise ValueError("missing or oversized TSV rows")
    if table:
        header = rows[0]
        if len(set(header)) != len(header) or any(len(row) != len(header) for row in rows[1:]):
            raise ValueError("duplicate TSV header or malformed row")
        return [dict(zip(header, row)) for row in rows[1:]]
    if any(len(row) != 2 for row in rows) or len({row[0] for row in rows}) != len(rows):
        raise ValueError("malformed or duplicate TSV identity")
    return dict(rows)


def read_packet(directory: Path, trusted: dict) -> dict:
    """Bounded artifact reader; trusted facts must come from the hosted adapter."""
    try:
        identity = read_tsv(directory / "identity.tsv")
        attempts = read_tsv(directory / "attempts.tsv", table=True)
        terminal = read_tsv(directory / "packet.tsv")
        planned = schedule(trusted.get("request", {}).get("phase"), trusted.get("request", {}).get("packet"))
        series = {}
        for index in range(len(planned.get("slots", []))):
            root = directory / f"trial-{index}"
            bundle = {}
            for name, key in (("compare.json", "compare"), ("pairs.json", "pairs"), ("summary.json", "summary")):
                try:
                    bundle[key] = json.loads(_read(root / name))
                except (OSError, ValueError):
                    bundle[key] = None
            series[index] = bundle
        result = validate_packet(identity, attempts, terminal, series, trusted)
    except (OSError, ValueError, TypeError) as error:
        result = validate_packet({}, [], {}, {}, trusted)
        result["problems"].append(f"packet could not be read: {error}")
    return result
