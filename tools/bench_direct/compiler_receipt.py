#!/usr/bin/env python3
"""Shared contract of the 9700X compiler comparison (#2752, #2769, #2761).

Ownership: `tools/bench_direct`, trusted `main` only. The host harness
(`compiler_compare.py`) writes a receipt; the hosted publisher
(`compiler_publish.py`) re-validates it as data and publishes one exact-head
check run. Both share the names and rules below. Nothing gates merging on
these checks: they are report-only evidence.

Two modes measure the same way and differ only in what they compare:
    main   a commit after it landed on main against its first parent, the
           main commit it landed on, or, when that commit has no valid
           measurement, the nearest first-parent ancestor with one, so a
           merge burst's unmeasured commits are inside a measured range
           (#2752); merging never waits for it
    pull   an owner pull request head against its merge base with the base
           branch, on request and without merging (#2769)
Each mode publishes its own check name and marker.

Both modes also run THROUGHPUT_PROFILE (#2761): the predeclared native
throughput corpus of `./build.sh bench_throughput` on the same two compilers,
so the corpus has a Zen 5 route. Its evidence must be complete and bound to
the measured binaries; its regressions, like the self-host verdict, are
reported and never decide.

Pull mode adds SCALING_PROFILE when the pull request also adds or changes
SCALING_REQUEST (#424): `./build.sh bench_throughput scale` times the
candidate compiler alone, compiling and linking generated multi-TU inputs with
-fcompile-jobs=W on whole physical cores. It is report-only too; its bundles
must be valid and bound to the candidate binary.

Map (searchable symbols):
    RECEIPT_SCHEMA, LAB_SCHEMA, MODES, check_name, check_marker   identities
    attempt_marker                                         one attempt's check (#2803)
    PROFILE                                                frozen profile
    APPROVED_HOST, observed_cpu_model, host_problem        observed Zen 5 host
    MEASURED_OUTCOMES, MIN_PAIRS, classify                 core validity
    THROUGHPUT_PROFILE, classify_throughput, throughput_digest   corpus (#2761)
    SCALING_REQUEST, SCALING_PROFILE, classify_scaling, scaling_digest   multi-TU scaling (#424)
    REGRESSION_POLICIES, regression_policy                 report-only switch
    range_label                                            main baseline relation
    render                                                 readable report
"""

from __future__ import annotations

import json
import csv
import hashlib
import math
import re
import statistics
from pathlib import Path

RECEIPT_SCHEMA = "buster-9700x-compiler-receipt-v1"
LAB_SCHEMA = "buster-uarch-lab-compare-v2"
# mode: (check name, external-ID marker prefix).
MODES = {
    "main": ("9700X compiler benchmark", "buster-9700x-compiler-main-v1"),
    "pull": ("9700X compiler benchmark (pull request)", "buster-9700x-compiler-pr-v1"),
}
CHECK_NAME, MARKER = MODES["main"]
# The approved Zen 5 host (#2761): the observed CPU model, never a runner
# label or target flag, must name the Ryzen 7 9700X.
APPROVED_HOST = re.compile(r"AMD Ryzen 7 9700X\b")
SHA = re.compile(r"[0-9a-f]{40}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
DECIMAL = re.compile(r"[1-9][0-9]*\Z")

# The routine comparison of both modes. It is frozen per profile name:
# changing any value needs a new name so receipts stay comparable. Deeper
# diagnosis (top-down, sampling, IBS) stays a separate, manually requested
# lab run.
PROFILE = {
    "name": "compiler-compare-v1",
    "workload": "uarch_lab compare default: stage-1 self-host compile of the unity "
                "src/buster/apps/ide/ide.c on the configured base tree",
    "frozen_source": "the base revision: a main commit's first parent or a pull request's merge base",
    "build": "./build.sh generate --cc clang --no-include-tests; ./build.sh build --config Release -t ide",
    "cpu": 2,
    "target_minutes": 10,
    "warmups": 1,
    "profile_steps": [],
    "corpus": "not included in compiler-compare-v1",
}
# The throughput corpus run after the self-host comparison (#2761), frozen per
# name like PROFILE. The harness is tools/throughput at the base revision (a
# main commit), built by its own build.c command; `arguments` follow the
# baseline/candidate/output/ids that compiler_compare supplies.
THROUGHPUT_SCHEMA = 2
THROUGHPUT_PROFILE = {
    "name": "throughput-corpus-v2",
    "workload": "bench_throughput run: the predeclared default CI corpus under both retained FAST/QUALITY modes, "
                "paired, two rounds, with its regression guard",
    "harness": "tools/throughput at the base revision, built and run by ./build.sh bench_throughput",
    "arguments": ["--profile", "ci", "--mode", "all", "--pairs", "20", "--warmups", "2", "--timeout", "120",
                  "--cpu", "2"],
    "workloads": ["tiny_startup", "large_function", "many_functions", "symbol_table", "control_flow",
                  "backend_pressure"],
    "modes": ["fast", "quality"],
    "pairs_per_round": 20,
    "rounds": 2,
    "warmups": 2,
    "cpu": 2,
}
# The multi-TU scaling leg (#424), frozen per name like PROFILE. Each series
# is one `bench_throughput scale` bundle on the candidate binary, with the
# --compiler/--output that compiler_compare supplies. "cores" leaves CPU 0's
# physical core to the runner and other housekeeping and places W workers on
# whole cores (plus one SMT point over the remaining logical CPUs); "machine"
# is the separate whole-host series, 8 cores and 8C/16T on the 9700X.
SCALING_REQUEST = "benchmarks/9700x/scaling.request"
INLINE_ACCEPTANCE_REQUEST_LINE = "canonical-inline-self-host-v1"
INLINE_ACCEPTANCE_SCHEMA = "buster-inline-self-host-acceptance-v1"
INLINE_ACCEPTANCE_PROFILE = {
    "source": "candidate-HEAD src/buster/apps/ide/ide.c with that build's generated headers",
    "compiler_modes": {"a": "default; -fcanonical-inline omitted", "b": "-fcanonical-inline"},
    "stages": ["stage-1 compiler construction", "generated stage-2 self-host compile"],
    "pairs": 12,
    "warmups": 1,
    "pairing": "ABBA",
    "measurements": ["wall", "instructions:u", "peak RSS", "executable-section code bytes"],
    "correctness": "each mode's stage-2 output must byte-match its stage-1 compiler"
}
SCALING_SCHEMA = "buster-throughput-scaling-v1"
SCALING_PROFILE = {
    "name": "scaling-v1",
    "workload": "bench_throughput scale: generated multi-TU compile-and-link with -fcompile-jobs=W on the "
                "candidate compiler, workers placed on whole physical cores (report-only)",
    "harness": "tools/throughput at the base revision, built and run by ./build.sh bench_throughput",
    "series": {
        "cores": ["--cpu-set", "auto", "--exclude-core", "0", "--workers", "1,2,4,7", "--allow-smt",
                  "--profile", "ci", "--repeats", "15", "--warmups", "2", "--timeout", "120"],
        "machine": ["--cpu-set", "auto", "--workers", "8", "--allow-smt", "--shape", "equal", "--shape", "skewed",
                    "--shape", "tiny", "--profile", "ci", "--repeats", "15", "--warmups", "2", "--timeout", "120"],
    },
}

# The analyzer comparison is a separate, explicitly requested workload on the
# existing pull-compare route. Keep its command and trial order immutable.
ANALYZER_REQUEST_LINE = "profile: clang-analyze-full-v1"
ANALYZER_REQUEST_PATH = "benchmarks/9700x/compiler-compare.request"
ANALYZER_SUMMARY_SCHEMA = "buster-clang-analyze-full-v1"
ANALYZER_PROFILE = {
    "name": "clang-analyze-full-v1",
    "workload": "complete split-source Release clang_analyze inventory from one candidate compile database",
    "source": "candidate HEAD source and compile_commands.json, shared byte-for-byte by both drivers",
    "database": "trusted merge-base driver generate --ci --no-sanitize --no-fuzz --no-lto --linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF",
    "analysis": "clang_analyze <database> --config Release --shards 8 --jobs 2 --timeout 600 --quiet --clang <trusted resolved executable>",
    "clang_identity": "trusted-main captures the resolved Clang executable and complete resource-directory file-tree digests; that absolute executable is passed to every analyzer driver",
    "aggregate": "independent clang_analyze <same database> --config Release --shards 8 --aggregate --results <run results>",
    "preflight": "separate fresh --prepare runs for baseline and candidate; excluded from four matched analysis arms",
    "order": ["baseline", "candidate", "candidate", "baseline"],
    "repeats_per_driver": 2,
    "shards": 8,
    "jobs": 2,
    "timeout_per_tu_seconds": 600,
    "expected_rows": 182,
    "expected_candidate_executions": 135,
    "expected_candidate_aliases": 47,
    "driver_resource_observation": "ProcessWaitResult.resources wait4 CPU and largest individual RSS high-water; both statuses must be observed",
    "whole_tree_sampler": "each full ANALYZE_RUN must report process_tree_status=complete; RSS remains sampled, not a continuous high-water",
    "native_budget_seconds": 4500,
    "report_only": True,
}
ANALYZER_RUNS = ("baseline-0", "candidate-0", "candidate-1", "baseline-1")
ANALYZER_PHASE_FIELDS = (
    "phase", "role", "trial", "wall_us", "user_cpu_us", "system_cpu_us", "peak_rss_bytes",
    "cpu_status", "memory_status", "exit_status", "timed_out", "cleanup_failed", "capture_failed",
    "stdout_bytes", "stderr_bytes", "state",
)
ANALYZER_REQUIRED_FILES = (
    "request.txt", "compile_commands.json", "clang.json", "drivers/baseline.driver", "drivers/baseline.complete",
    "drivers/baseline.checkout", "drivers/candidate.driver", "drivers/candidate.complete",
    "drivers/candidate.checkout", "profile/profile.tsv",
    "profile/helper.log",
    "profile/prepare-baseline/manifest.txt", "profile/prepare-candidate/manifest.txt",
)
ANALYZER_STRING_LIMIT = 32 * 1024 * 1024
ANALYZER_ROW_LIMIT = 4096
# `--mode all` of the current tools/throughput (tp_modes) selects both
# retained modes. The named v2 profile binds the exact workload x mode cells;
# historical four-mode v1 receipts keep their distinct identity.
THROUGHPUT_DECISIONS = ("regression", "inconclusive", "no substantial regression detected")
# Each case's gate tests: the wall and peak-RSS metrics for every round.
THROUGHPUT_TEST_METRICS = ("wall_seconds", "peak_rss_bytes")
# A wall-time CI needs at least six complete pairs (uarch_lab sign_test_rank).
MIN_PAIRS = 6
# Every complete verdict counts, whatever its direction; "inconclusive" means
# too few pairs and "NO VERDICT" no pair at all, so neither is a measurement.
MEASURED_OUTCOMES = ("faster", "slower", "below-floor", "no detectable difference")
# Only report-only exists. "enforce" is reserved for a separately reviewed
# rollout with qualified A/A noise and thresholds; until then it fails closed.
REGRESSION_POLICIES = ("report-only",)


IDENTITY_KEYS = ("mode", "repository", "ref", "pull", "pull_head", "base", "base_tree", "head", "head_tree",
                 "trusted_revision", "request_run_id", "run_id", "run_attempt")


def inline_acceptance_requested(candidate: Path) -> bool:
    """Only the exact owner-requested selector opts into issue #48's fixed profile."""
    request = candidate / "benchmarks/9700x/compiler-compare.request"
    try:
        return INLINE_ACCEPTANCE_REQUEST_LINE in request.read_text(encoding="utf-8").splitlines()
    except OSError:
        return False


def validate_inline_acceptance(summary: object, expected_revision: str, expected_candidate_sha: str | None = None) -> list[str]:
    """Validate opt-in #48 evidence shape; missing PMU counters need an explicit NA reason."""
    problems = []
    if not isinstance(summary, dict) or summary.get("schema") != INLINE_ACCEPTANCE_SCHEMA or summary.get("status") != "complete":
        return ["issue #48 inline receipt has an unexpected or incomplete schema"]
    if summary.get("source_revision") != expected_revision:
        problems.append("issue #48 inline receipt source revision does not match candidate HEAD")
    profile = summary.get("profile")
    if not isinstance(profile, dict) or any(profile.get(key) != value for key, value in INLINE_ACCEPTANCE_PROFILE.items()):
        problems.append("issue #48 inline receipt profile does not match the fixed acceptance profile")
    if not isinstance(profile, dict) or profile.get("cpu") != 2 or not re.fullmatch(r"[0-9a-f]{64}", str(profile.get("source_sha256", ""))):
        problems.append("issue #48 inline receipt lacks the expected source hash or CPU pin")
    candidate = summary.get("candidate_compiler")
    if not isinstance(candidate, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(candidate.get("sha256", ""))):
        problems.append("issue #48 candidate compiler identity is missing")
    elif expected_candidate_sha and candidate.get("sha256") != expected_candidate_sha:
        problems.append("issue #48 timed candidate compiler differs from the frozen candidate-HEAD binary")
    fixed = summary.get("fixed_point")
    if not isinstance(fixed, dict) or any(fixed.get(mode) is not True for mode in ("off", "on")):
        problems.append("issue #48 inline receipt did not reproduce both stage-1 compilers")
    for stage_name in ("stage1", "selfhost_runtime"):
        stage = summary.get(stage_name)
        values = stage.get("metrics") if isinstance(stage, dict) else None
        if not isinstance(values, dict):
            problems.append(f"issue #48 {stage_name} metrics are missing")
            continue
        for metric in ("wall", "instructions", "peak_rss"):
            if not isinstance(values.get(metric), dict):
                problems.append(f"issue #48 {stage_name} metric {metric} is missing")
        wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
        if not all(is_number(wall.get(key)) and math.isfinite(wall[key]) and wall[key] > 0
                   for key in ("a_median", "b_median", "ratio")):
            problems.append(f"issue #48 {stage_name} wall-time measurements are missing")
        code = values.get("code_bytes") if isinstance(values.get("code_bytes"), dict) else {}
        if not all(is_number(code.get(key)) and math.isfinite(code[key]) and code[key] > 0
                   for key in ("a_value", "b_value", "ratio")):
            problems.append(f"issue #48 {stage_name} executable-section code-byte measurements are missing")
        instructions = values.get("instructions") if isinstance(values.get("instructions"), dict) else {}
        instruction_ratio = instructions.get("ratio")
        counters = values.get("counter_availability")
        if instruction_ratio is None:
            if not isinstance(counters, dict) or counters.get("perf_stat") is not False or not counters.get("reason"):
                problems.append(f"issue #48 {stage_name} retired-instruction NA has no unavailable-counter reason")
        elif not is_number(instruction_ratio) or not math.isfinite(instruction_ratio) or instruction_ratio <= 0:
            problems.append(f"issue #48 {stage_name} retired-instruction ratio is invalid")
        elif not all(is_number(instructions.get(key)) and math.isfinite(instructions[key]) and instructions[key] > 0
                     for key in ("a_median", "b_median")):
            problems.append(f"issue #48 {stage_name} retired-instruction counts are missing")
    return problems


class AnalyzerRecordReader:
    """Bounded reader for the native length-prefixed PLAN/RESULT records."""

    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def line(self) -> bytes:
        end = self.data.find(b"\n", self.position)
        if end < 0:
            raise ValueError("unterminated record line")
        value = self.data[self.position:end]
        self.position = end + 1
        return value

    def number(self) -> int:
        value = self.line()
        if not re.fullmatch(rb"(?:0|[1-9][0-9]*)", value):
            raise ValueError("malformed unsigned record number")
        return int(value)

    def string(self) -> bytes:
        length = self.number()
        if length > ANALYZER_STRING_LIMIT or length > len(self.data) - self.position:
            raise ValueError("record string exceeds its bound")
        end = self.position + length
        value = self.data[self.position:end]
        self.position = end
        if self.position >= len(self.data) or self.data[self.position] != 10:
            raise ValueError("record string lacks its terminator")
        self.position += 1
        return value

    def done(self) -> bool:
        return self.position == len(self.data)


def analyzer_text(value: bytes) -> str:
    text = value.decode("utf-8")
    if "\x00" in text:
        raise ValueError("record string contains NUL")
    return text


def analyzer_fields(line: str, marker: str) -> dict:
    """Parse one fixed marker line without accepting duplicate or malformed keys."""
    fields = line.split()
    if not fields or fields[0] != marker:
        raise ValueError(f"missing {marker} record")
    result = {}
    for field in fields[1:]:
        key, separator, value = field.partition("=")
        if not separator or not key or not value or key in result:
            raise ValueError(f"malformed {marker} field {field!r}")
        result[key] = value
    return result


def analyzer_log_records(text: str, marker: str) -> list[dict]:
    """Parse all marker records in one retained driver log."""
    return [analyzer_fields(line, marker) for line in text.splitlines() if line.startswith(marker + " ")]


def analyzer_observed_record(text: str, marker: str) -> dict:
    """Retain one raw marker record without treating it as validated evidence."""
    lines = [line for line in text.splitlines() if line.startswith(marker + " ")]
    if not lines:
        return {"record_status": "missing", "raw_record": None, "fields": {}}
    if len(lines) != 1:
        return {"record_status": "duplicate", "raw_record": None, "raw_records": lines, "fields": {},
                "record_count": len(lines)}
    try:
        fields = analyzer_fields(lines[0], marker)
    except ValueError as error:
        return {"record_status": "malformed", "raw_record": lines[0], "fields": {},
                "reason": str(error)}
    return {"record_status": "observed", "raw_record": lines[0], "fields": fields}


def analyzer_observed_cost_records(text: str, marker: str) -> dict:
    """Retain raw cost rows, including malformed or duplicate observations."""
    records = []
    for line in text.splitlines():
        if line.startswith(marker + " "):
            try:
                fields = analyzer_fields(line, marker)
                records.append({"record_status": "observed", "raw_record": line, "fields": fields})
            except ValueError as error:
                records.append({"record_status": "malformed", "raw_record": line, "fields": {},
                                "validation_reason": str(error)})
    return {"record_count": len(records), "records": records}


def analyzer_cost_observation(record: dict, schema: tuple[str, ...], numbers: tuple[str, ...],
                              costs: tuple[str, ...], expected: dict | None, scope: str,
                              statuses: tuple[str, ...] = ()) -> dict:
    """Validate one cost row independently without discarding its raw fields."""
    item = dict(record, validation_status="failed", observed_numeric={}, costs={})
    if record.get("record_status") != "observed":
        item["validation_reason"] = record.get("validation_reason", "malformed record")
        return item
    fields, values, errors = record["fields"], {}, []
    try:
        analyzer_exact_fields(fields, schema, scope)
    except ValueError as error:
        errors.append(str(error))
    for key in numbers:
        try:
            values[key] = analyzer_uint(fields, key, scope)
        except ValueError as error:
            errors.append(str(error))
    item.update(values, observed_numeric=values)
    item["status"] = fields.get("status") if "status" in fields else None
    if expected is not None:
        for key, value in expected.items():
            got = fields.get(key) if isinstance(value, str) else values.get(key)
            if got != value:
                errors.append(f"{scope} {key} does not match its analyzer plan")
    if statuses and item["status"] not in statuses:
        errors.append(f"{scope} has an invalid status")
    if values.get("context_proof_us", 0) > values.get("planning_us", values.get("context_proof_us", 0)):
        errors.append(f"{scope} context proof exceeds planning duration")
    if errors:
        item["validation_status"], item["validation_reason"] = "failed", "; ".join(errors)
    elif expected is None:
        item["validation_status"], item["validation_reason"] = "unbound", "manifest unavailable"
    else:
        item["validation_status"], item["validation_reason"] = "plan-bound", None
        item["costs"] = {key: values[key] for key in costs if key in values}
    return item


def analyzer_cost_set_status(count: int, expected: int, records: list[dict]) -> str:
    if expected == 0:
        return "not-applicable" if count == 0 else "unexpected"
    if count == 0:
        return "missing"
    if count < expected:
        return "partial"
    if count > expected:
        return "duplicate"
    if any(record.get("record_status") != "observed" for record in records):
        return "malformed"
    if any(record.get("validation_status") != "plan-bound" for record in records):
        return "invalid"
    return "complete"


def analyzer_aggregate_cost_observation(record: dict, plan: dict | None, version: int, scope: str) -> dict:
    """Retain aggregate counters independently of strict successful-run validation."""
    item = dict(record, validation_status="incomplete", observed_numeric={}, field_checks={},
                plan_checks={}, plan_identity_status="unavailable", count_match_status="unavailable",
                result_state="incomplete")
    if record.get("record_status") != "observed":
        item["validation_reason"] = record.get("validation_reason", "malformed record")
        return item

    if version == 1:
        numeric_schema = ("eligible", "checked", "excluded_config_or_language", "failures", "shards",
                          "peak_child_rss_bytes")
        if plan is None:
            expected = None
            identity_keys = ("eligible", "excluded_config_or_language", "shards")
        else:
            expected = {"eligible": plan["selected_rows"], "checked": plan["selected_rows"],
                        "excluded_config_or_language": plan["excluded_rows"], "failures": 0,
                        "shards": plan["shards"]}
            identity_keys = ("eligible", "excluded_config_or_language", "shards")
    else:
        numeric_schema = ("selected_rows", "checked", "unique_executions", "aliased_rows",
                          "excluded_config_or_language", "failures", "shards", "peak_child_rss_bytes")
        if plan is None:
            expected = None
            identity_keys = ("selected_rows", "unique_executions", "aliased_rows",
                             "excluded_config_or_language", "shards")
        else:
            expected = {"selected_rows": plan["selected_rows"], "checked": plan["selected_rows"],
                        "unique_executions": plan["unique_executions"], "aliased_rows": plan["aliases"],
                        "excluded_config_or_language": plan["excluded_rows"], "failures": 0,
                        "shards": plan["shards"]}
            identity_keys = ("selected_rows", "unique_executions", "aliased_rows",
                             "excluded_config_or_language", "shards")

    fields = record.get("fields", {})
    expected_schema = set(numeric_schema) | {"status"}
    schema_ok = set(fields) == expected_schema
    values, field_checks, errors = {}, {}, []
    if not schema_ok:
        missing = sorted(expected_schema - set(fields))
        extra = sorted(set(fields) - expected_schema)
        errors.append(f"{scope} fields differ from the fixed record schema (missing={missing}, extra={extra})")
    for key, raw_value in fields.items():
        if key == "status":
            continue
        try:
            value = analyzer_uint(fields, key, scope)
            values[key] = value
            field_checks[key] = {"state": "observed", "raw": raw_value, "value": value}
        except ValueError as error:
            field_checks[key] = {"state": "invalid", "raw": raw_value, "reason": str(error)}
            errors.append(str(error))
    for key in numeric_schema:
        if key not in field_checks:
            field_checks[key] = {"state": "missing", "expected": expected.get(key) if expected else None}
    status = fields.get("status")
    item["status"] = status
    if status not in ("pass", "fail"):
        errors.append(f"{scope} has an invalid status")
    if expected is not None:
        for key, expected_value in expected.items():
            observed = values.get(key)
            matches = observed == expected_value
            field_checks.setdefault(key, {"state": "missing"})
            field_checks[key].update(expected=expected_value, matches=matches)
            if not matches:
                errors.append(f"{scope} {key} does not match its analyzer plan/result contract")
        identity_checks = [field_checks.get(key, {}).get("matches") is True for key in identity_keys]
        item["plan_identity_status"] = "matched" if all(identity_checks) else "mismatch"
        count_keys = (*identity_keys, "checked")
        item["count_match_status"] = "matched" if all(
            field_checks.get(key, {}).get("matches") is True for key in count_keys) else "mismatch"
        item["plan_checks"] = {key: field_checks[key] for key in expected}
    else:
        item["validation_status"] = "unbound"
        item["plan_identity_status"] = "unavailable"
        item["count_match_status"] = "unavailable"

    complete = schema_ok and all(field_checks.get(key, {}).get("state") == "observed" for key in numeric_schema) \
        and status in ("pass", "fail")
    item.update(observed_numeric=values, field_checks=field_checks)
    if not complete:
        item["result_state"] = "incomplete"
        item["validation_status"] = "incomplete"
    elif expected is None:
        item["result_state"] = "unbound"
        item["validation_status"] = "unbound"
    elif status != "pass" or any(not check["matches"] for check in item["plan_checks"].values()):
        item["result_state"] = "failed"
        item["validation_status"] = "plan-bound-failed" if item["plan_identity_status"] == "matched" else "failed"
    else:
        item["result_state"] = "passed"
        item["validation_status"] = "plan-bound"
    if errors:
        item["validation_reason"] = "; ".join(dict.fromkeys(errors))
    return item


def analyzer_parse_run_cost_observations(analysis_text: str, aggregate_text: str,
                                         plan: dict | None, label: str) -> dict:
    """Retain per-record plan-bound costs even when strict arm validation fails."""
    version = plan["version"] if plan else (1 if label.startswith("baseline-") else 2)
    shards = plan["shards"] if plan else 8
    analysis_plans = analyzer_observed_cost_records(analysis_text, "ANALYZE_PLAN")
    aggregate_plans = analyzer_observed_cost_records(aggregate_text, "ANALYZE_PLAN")
    analysis_shards = analyzer_observed_cost_records(analysis_text, "ANALYZE_SHARD")
    analysis_aggregates = analyzer_observed_cost_records(analysis_text, "ANALYZE_AGGREGATE")
    independent_aggregates = analyzer_observed_cost_records(aggregate_text, "ANALYZE_AGGREGATE")
    plan_records = analysis_plans["records"]
    run_records = [record for record in plan_records if record["fields"].get("mode") == "run"]
    worker_records = [record for record in plan_records if record["fields"].get("mode") == "worker"]
    expected_global = None if plan is None else {
        "selected_rows": plan["selected_rows"],
        "unique_executions": plan["unique_executions"],
        "aliases": plan["aliases"],
    }
    run_plan_records, worker_plan_records, aggregate_plan_records, shard_records = [], [], [], []
    if version == 2:
        plan_fields = ("mode", "selected_rows", "unique_executions", "aliases", "planning_us", "context_proof_us")
        plan_numbers = ("selected_rows", "unique_executions", "aliases", "planning_us", "context_proof_us")
        plan_costs = ("planning_us", "context_proof_us")
        for record in run_records:
            run_plan_records.append(analyzer_cost_observation(record, plan_fields, plan_numbers, plan_costs,
                (expected_global | {"mode": "run"}) if expected_global is not None else None, f"{label} run PLAN"))
        worker_fields = ("mode", "shard", "selected_rows", "unique_executions", "aliases", "planning_us", "context_proof_us")
        worker_numbers = ("shard", *plan_numbers)
        for record in worker_records:
            worker_plan_records.append(analyzer_cost_observation(record, worker_fields, worker_numbers, plan_costs,
                (expected_global | {"mode": "worker"}) if expected_global is not None else None, f"{label} worker PLAN"))
        for record in aggregate_plans["records"]:
            aggregate_plan_records.append(analyzer_cost_observation(record, plan_fields, plan_numbers, plan_costs,
                (expected_global | {"mode": "aggregate"}) if expected_global is not None else None,
                f"{label} aggregate PLAN"))
        shard_fields = ("shard", "selected_rows", "unique_executions", "aliased_rows", "elapsed_us",
                        "context_preflight_us", "context_postflight_us", "peak_child_rss_bytes", "status")
        shard_numbers = shard_fields[:-1]
        for record in analysis_shards["records"]:
            shard_records.append(analyzer_cost_observation(record, shard_fields, shard_numbers,
                ("selected_rows", "unique_executions", "aliased_rows", "elapsed_us", "context_preflight_us",
                 "context_postflight_us", "peak_child_rss_bytes"), None, f"{label} V2 ANALYZE_SHARD", ("pass", "fail")))
            item = shard_records[-1]
            shard = item["observed_numeric"].get("shard")
            if plan is not None and shard is not None and shard < shards:
                rows = [row for row in plan["rows"] if row["shard"] == shard]
                executions = sum(row["representative"] == row["index"] for row in rows)
                aliases = len(rows) - executions
                expected = {"shard": shard, "selected_rows": len(rows), "unique_executions": executions,
                            "aliased_rows": aliases}
                mismatch = [key for key, value in expected.items() if item["observed_numeric"].get(key) != value]
                if mismatch:
                    item.update(validation_status="failed", validation_reason="shard counters do not match the plan")
                elif item["record_status"] == "observed" and item["validation_status"] == "unbound":
                    item.update(validation_status="plan-bound", validation_reason=None)
                    item["costs"] = {key: item["observed_numeric"][key]
                                     for key in shard_fields[:-1] if key in item["observed_numeric"]}
    else:
        shard_fields = ("shard", "units", "elapsed_us", "peak_child_rss_bytes", "status")
        shard_numbers = shard_fields[:-1]
        for record in analysis_shards["records"]:
            shard_records.append(analyzer_cost_observation(record, shard_fields, shard_numbers,
                ("units", "elapsed_us", "peak_child_rss_bytes"), None,
                f"{label} PLAN_V1 ANALYZE_SHARD", ("pass", "fail")))
            item = shard_records[-1]
            shard = item["observed_numeric"].get("shard")
            if plan is not None and shard is not None and shard < shards:
                expected_units = sum(row["shard"] == shard for row in plan["rows"])
                if item["observed_numeric"].get("units") != expected_units:
                    item.update(validation_status="failed", validation_reason="shard unit count does not match plan")
                elif item["record_status"] == "observed" and item["validation_status"] == "unbound":
                    item.update(validation_status="plan-bound", validation_reason=None)
                    item["costs"] = {key: item["observed_numeric"][key]
                                     for key in shard_fields[:-1] if key in item["observed_numeric"]}
    for records in (worker_plan_records, shard_records):
        shards_seen = [record.get("observed_numeric", {}).get("shard") for record in records]
        for record in records:
            shard = record.get("observed_numeric", {}).get("shard")
            if isinstance(shard, int) and (shard >= shards or shards_seen.count(shard) > 1):
                record.update(validation_status="failed", validation_reason="invalid or duplicate shard ID", costs={})
    aggregate_records = [analysis_aggregates["records"], independent_aggregates["records"]]
    for records in aggregate_records:
        for index, record in enumerate(records):
            records[index] = analyzer_aggregate_cost_observation(
                record, plan, version, f"{label} aggregate")
    run_status = analyzer_cost_set_status(len(run_plan_records), 1 if version == 2 else 0, run_plan_records)
    worker_status = analyzer_cost_set_status(len(worker_plan_records), shards if version == 2 else 0,
                                             worker_plan_records)
    aggregate_plan_status = analyzer_cost_set_status(len(aggregate_plan_records), 1 if version == 2 else 0,
                                                     aggregate_plan_records)
    shard_status = analyzer_cost_set_status(len(shard_records), shards, shard_records)
    analysis_aggregate_status = analyzer_cost_set_status(len(analysis_aggregates["records"]), 1,
                                                         analysis_aggregates["records"])
    independent_aggregate_status = analyzer_cost_set_status(len(independent_aggregates["records"]), 1,
                                                            independent_aggregates["records"])
    if len(plan_records) != len(run_records) + len(worker_records):
        run_status = "unexpected"
    if version == 1 and plan_records:
        run_status = "unexpected"
    if version == 1 and aggregate_plans["records"]:
        aggregate_plan_status = "unexpected"
    observation_status = "complete" if all(status in ("complete", "not-applicable") for status in
        (run_status, worker_status, aggregate_plan_status, shard_status,
         analysis_aggregate_status, independent_aggregate_status)) else "partial"
    if not any((plan_records, aggregate_plans["records"], analysis_shards["records"],
                analysis_aggregates["records"], independent_aggregates["records"])):
        observation_status = "missing"
    record_sets = {"run_plan": run_status, "worker_plan": worker_status,
                   "aggregate_plan": aggregate_plan_status, "shards": shard_status,
                   "analysis_aggregate": analysis_aggregate_status,
                   "independent_aggregate": independent_aggregate_status}
    return {"format": "PLAN_V1" if version == 1 else "PLAN_V2",
            "expected_shards": shards,
            "planning_context_status": "unavailable in baseline PLAN_V1" if version == 1
            else "reported by candidate PLAN_V2",
            "observation_status": observation_status,
            "validation_status": "failed",
            "record_sets": record_sets,
            "raw_record_sets": {"analysis_plan": analysis_plans, "aggregate_plan": aggregate_plans,
                                "analysis_shards": analysis_shards, "analysis_aggregate": analysis_aggregates,
                                "independent_aggregates": independent_aggregates},
            "run_plan_records": run_plan_records, "worker_plan_records": worker_plan_records,
            "aggregate_plan_records": aggregate_plan_records, "shard_records": shard_records,
            "analysis_aggregate_records": analysis_aggregates["records"],
            "independent_aggregate_records": independent_aggregates["records"],
            "run_plan": run_plan_records[0] if len(run_plan_records) == 1 else None,
            "worker_plans": worker_plan_records if version == 2 else None,
            "aggregate_plan": aggregate_plan_records[0] if len(aggregate_plan_records) == 1 else None,
            "shards": shard_records,
            "analysis_aggregate": analysis_aggregates["records"][0] if len(analysis_aggregates["records"]) == 1 else None,
            "independent_aggregate": independent_aggregates["records"][0]
            if len(independent_aggregates["records"]) == 1 else None}


def analyzer_uint(fields: dict, key: str, scope: str) -> int:
    value = fields.get(key, "")
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)", value):
        raise ValueError(f"{scope} has an invalid nonnegative {key} counter")
    return int(value)


def analyzer_exact_fields(fields: dict, expected: tuple[str, ...], scope: str) -> None:
    if set(fields) != set(expected):
        raise ValueError(f"{scope} fields differ from the fixed record schema")


def analyzer_aggregate_summary(fields: dict, plan: dict, scope: str) -> dict:
    """Validate one ordinary or independent aggregate result against its plan."""
    if plan["version"] == 1:
        expected_fields = ("eligible", "checked", "excluded_config_or_language", "failures", "shards",
                           "peak_child_rss_bytes", "status")
        expected = {"eligible": plan["selected_rows"], "checked": plan["selected_rows"],
                    "excluded_config_or_language": plan["excluded_rows"], "failures": 0,
                    "shards": plan["shards"]}
    else:
        expected_fields = ("selected_rows", "checked", "unique_executions", "aliased_rows",
                           "excluded_config_or_language", "failures", "shards", "peak_child_rss_bytes", "status")
        expected = {"selected_rows": plan["selected_rows"], "checked": plan["selected_rows"],
                    "unique_executions": plan["unique_executions"], "aliased_rows": plan["aliases"],
                    "excluded_config_or_language": plan["excluded_rows"], "failures": 0,
                    "shards": plan["shards"]}
    analyzer_exact_fields(fields, expected_fields, scope)
    result = {}
    for key in expected_fields:
        if key == "status":
            if fields[key] != "pass":
                raise ValueError(f"{scope} did not report status=pass")
            result[key] = fields[key]
        else:
            value = analyzer_uint(fields, key, scope)
            if key in expected and value != expected[key]:
                raise ValueError(f"{scope} {key} does not match its analyzer plan")
            result[key] = value
    return result


def analyzer_parse_run_costs(analysis_text: str, aggregate_text: str, plan: dict, label: str) -> dict:
    """Bind version-aware analyzer plan/shard cost records to one full arm."""
    version = plan["version"]
    shards = plan["shards"]
    analysis_plans = analyzer_log_records(analysis_text, "ANALYZE_PLAN")
    aggregate_plans = analyzer_log_records(aggregate_text, "ANALYZE_PLAN")
    analysis_aggregates = analyzer_log_records(analysis_text, "ANALYZE_AGGREGATE")
    aggregate_results = analyzer_log_records(aggregate_text, "ANALYZE_AGGREGATE")
    shard_records = analyzer_log_records(analysis_text, "ANALYZE_SHARD")
    analysis_aggregate = analyzer_aggregate_summary(
        analysis_aggregates[0], plan, f"{label} in-run aggregate") if len(analysis_aggregates) == 1 else None
    independent_aggregate = analyzer_aggregate_summary(
        aggregate_results[0], plan, f"{label} independent aggregate") if len(aggregate_results) == 1 else None
    if analysis_aggregate is None:
        raise ValueError(f"{label} analysis log does not contain exactly one aggregate result")
    if independent_aggregate is None:
        raise ValueError(f"{label} aggregate log does not contain exactly one aggregate result")
    if len(shard_records) != shards:
        raise ValueError(f"{label} analysis log does not contain one ANALYZE_SHARD record per shard")

    rows_by_shard = {shard: [row for row in plan["rows"] if row["shard"] == shard] for shard in range(shards)}
    shard_by_id = {}
    if version == 1:
        if analysis_plans or aggregate_plans:
            raise ValueError(f"{label} PLAN_V1 unexpectedly emitted PLAN_V2 cost records")
        for fields in shard_records:
            analyzer_exact_fields(fields, ("shard", "units", "elapsed_us", "peak_child_rss_bytes", "status"),
                                  f"{label} PLAN_V1 ANALYZE_SHARD")
            shard = analyzer_uint(fields, "shard", f"{label} PLAN_V1 ANALYZE_SHARD")
            if shard in shard_by_id or shard >= shards:
                raise ValueError(f"{label} PLAN_V1 shard records have duplicate or invalid shard IDs")
            values = {key: analyzer_uint(fields, key, f"{label} PLAN_V1 ANALYZE_SHARD")
                      for key in ("units", "elapsed_us", "peak_child_rss_bytes")}
            if values["units"] != len(rows_by_shard[shard]) or fields["status"] != "pass":
                raise ValueError(f"{label} PLAN_V1 shard {shard} does not match its plan")
            shard_by_id[shard] = {"shard": shard, **values, "status": fields["status"]}
        if set(shard_by_id) != set(range(shards)):
            raise ValueError(f"{label} PLAN_V1 shard records do not cover every shard")
        return {"format": "PLAN_V1", "planning_context_status": "unavailable in baseline PLAN_V1",
                "run_plan": None, "worker_plans": None, "aggregate_plan": None,
                "shards": [shard_by_id[shard] for shard in range(shards)],
                "analysis_aggregate": analysis_aggregate, "independent_aggregate": independent_aggregate}

    if version != 2:
        raise ValueError(f"{label} uses an unsupported analyzer plan version")
    if len(analysis_plans) != 1 + shards:
        raise ValueError(f"{label} analysis log must contain one run PLAN and one worker PLAN per shard")
    if len(aggregate_plans) != 1:
        raise ValueError(f"{label} aggregate log must contain exactly one aggregate PLAN")
    run_plan_rows = [fields for fields in analysis_plans if fields.get("mode") == "run"]
    worker_plan_rows = [fields for fields in analysis_plans if fields.get("mode") == "worker"]
    if len(run_plan_rows) != 1 or len(worker_plan_rows) != shards:
        raise ValueError(f"{label} analysis PLAN modes are incomplete or unexpected")
    run_plan_fields = run_plan_rows[0]
    analyzer_exact_fields(run_plan_fields, ("mode", "selected_rows", "unique_executions", "aliases", "planning_us",
                                            "context_proof_us"), f"{label} run PLAN")
    run_plan = {key: analyzer_uint(run_plan_fields, key, f"{label} run PLAN")
                for key in ("selected_rows", "unique_executions", "aliases", "planning_us", "context_proof_us")}
    expected_global = {"selected_rows": plan["selected_rows"], "unique_executions": plan["unique_executions"],
                       "aliases": plan["aliases"]}
    if any(run_plan[key] != value for key, value in expected_global.items()):
        raise ValueError(f"{label} run PLAN counts differ from its V2 manifest")
    if run_plan["context_proof_us"] > run_plan["planning_us"]:
        raise ValueError(f"{label} run PLAN context proof exceeds its planning duration")

    worker_by_id = {}
    for fields in worker_plan_rows:
        analyzer_exact_fields(fields, ("mode", "shard", "selected_rows", "unique_executions", "aliases", "planning_us",
                                       "context_proof_us"), f"{label} worker PLAN")
        shard = analyzer_uint(fields, "shard", f"{label} worker PLAN")
        if shard in worker_by_id or shard >= shards:
            raise ValueError(f"{label} worker PLANs have duplicate or invalid shard IDs")
        item = {key: analyzer_uint(fields, key, f"{label} worker PLAN")
                for key in ("selected_rows", "unique_executions", "aliases", "planning_us", "context_proof_us")}
        if any(item[key] != value for key, value in expected_global.items()):
            raise ValueError(f"{label} worker PLAN shard {shard} counts differ from its V2 manifest")
        if item["context_proof_us"] > item["planning_us"]:
            raise ValueError(f"{label} worker PLAN shard {shard} context proof exceeds its planning duration")
        worker_by_id[shard] = {"shard": shard, **item}
    if set(worker_by_id) != set(range(shards)):
        raise ValueError(f"{label} worker PLANs do not cover every shard")

    for fields in shard_records:
        analyzer_exact_fields(fields, ("shard", "selected_rows", "unique_executions", "aliased_rows", "elapsed_us",
                                       "context_preflight_us", "context_postflight_us", "peak_child_rss_bytes", "status"),
                              f"{label} V2 ANALYZE_SHARD")
        shard = analyzer_uint(fields, "shard", f"{label} V2 ANALYZE_SHARD")
        if shard in shard_by_id or shard >= shards:
            raise ValueError(f"{label} V2 shard records have duplicate or invalid shard IDs")
        values = {key: analyzer_uint(fields, key, f"{label} V2 ANALYZE_SHARD")
                  for key in ("selected_rows", "unique_executions", "aliased_rows", "elapsed_us", "context_preflight_us",
                              "context_postflight_us", "peak_child_rss_bytes")}
        expected_rows = rows_by_shard[shard]
        expected_selected = len(expected_rows)
        expected_exec = sum(row["representative"] == row["index"] for row in expected_rows)
        expected_alias = expected_selected - expected_exec
        if (values["selected_rows"], values["unique_executions"], values["aliased_rows"]) != \
                (expected_selected, expected_exec, expected_alias) or fields["status"] != "pass":
            raise ValueError(f"{label} V2 shard {shard} counters or status differ from its manifest")
        shard_by_id[shard] = {"shard": shard, **values, "status": fields["status"]}
    if set(shard_by_id) != set(range(shards)):
        raise ValueError(f"{label} V2 shard records do not cover every shard")

    aggregate_plan_fields = aggregate_plans[0]
    analyzer_exact_fields(aggregate_plan_fields, ("mode", "selected_rows", "unique_executions", "aliases", "planning_us",
                                                  "context_proof_us"), f"{label} aggregate PLAN")
    if aggregate_plan_fields["mode"] != "aggregate":
        raise ValueError(f"{label} independent aggregate PLAN has the wrong mode")
    aggregate_plan = {key: analyzer_uint(aggregate_plan_fields, key, f"{label} aggregate PLAN")
                      for key in ("selected_rows", "unique_executions", "aliases", "planning_us", "context_proof_us")}
    if any(aggregate_plan[key] != value for key, value in expected_global.items()):
        raise ValueError(f"{label} independent aggregate PLAN counts differ from its V2 manifest")
    if aggregate_plan["context_proof_us"] > aggregate_plan["planning_us"]:
        raise ValueError(f"{label} aggregate PLAN context proof exceeds its planning duration")
    if run_plan_fields["mode"] != "run":
        raise ValueError(f"{label} ordinary analyzer PLAN has the wrong mode")
    return {"format": "PLAN_V2", "planning_context_status": "reported by candidate PLAN_V2",
            "run_plan": run_plan, "worker_plans": [worker_by_id[shard] for shard in range(shards)],
            "aggregate_plan": aggregate_plan, "shards": [shard_by_id[shard] for shard in range(shards)],
            "analysis_aggregate": analysis_aggregate, "independent_aggregate": independent_aggregate}


def analyzer_json(data: bytes) -> object:
    """Load one small analyzer JSON file while rejecting duplicate object keys."""
    def unique(pairs: list) -> dict:
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate analyzer JSON key {key!r}")
            result[key] = value
        return result
    return json.loads(data.decode("utf-8"), object_pairs_hook=unique)


def analyzer_parse_plan(data: bytes) -> dict:
    """Parse only the stable V1/V2 plan envelope; V2 context proof stays opaque."""
    reader = AnalyzerRecordReader(data)
    magic = analyzer_text(reader.line())
    version = 1 if magic == "BUSTER_CLANG_ANALYZE_PLAN_V1" else 2 if magic == "BUSTER_CLANG_ANALYZE_PLAN_V2" else 0
    if not version:
        raise ValueError("unknown analyzer plan version")
    results = analyzer_text(reader.string())
    config = analyzer_text(reader.string())
    clang = analyzer_text(reader.string())
    shards = reader.number()
    timeout = reader.number()
    count = reader.number()
    excluded = reader.number()
    unique_executions = count
    aliases = 0
    fixture = False
    if count > ANALYZER_ROW_LIMIT:
        raise ValueError("analyzer plan row count exceeds its bound")
    if version == 2:
        unique_executions = reader.number()
        aliases = reader.number()
        fixture_value = reader.number()
        if fixture_value > 1:
            raise ValueError("invalid analyzer fixture flag")
        fixture = bool(fixture_value)
    database = reader.string()
    rows = []
    for expected_index in range(count):
        index = reader.number()
        shard = reader.number()
        representative = index
        if version == 2:
            representative = reader.number()
        module = analyzer_text(reader.string())
        file = analyzer_text(reader.string())
        directory = output = ""
        original_argv = []
        context_proof_present = False
        context_proof_sha256 = hashlib.sha256(b"").hexdigest()
        input_inventory = []
        search_inventory = []
        reason = ""
        if version == 2:
            directory = analyzer_text(reader.string())
            output = analyzer_text(reader.string())
            argument_count = reader.number()
            if argument_count > 4096:
                raise ValueError("original analyzer argv exceeds its bound")
            original_argv = [analyzer_text(reader.string()) for _ in range(argument_count)]
        command_count = reader.number()
        if command_count > 4096:
            raise ValueError("projected analyzer argv exceeds its bound")
        command = [analyzer_text(reader.string()) for _ in range(command_count)]
        proven = False
        if version == 2:
            proven_value = reader.number()
            if proven_value > 1:
                raise ValueError("invalid analyzer proof flag")
            proven = bool(proven_value)
            reason = analyzer_text(reader.string())
            context_proof = reader.string()  # opaque bytes; only compare their digest for repeated-plan consistency
            context_proof_present = bool(context_proof)
            context_proof_sha256 = hashlib.sha256(context_proof).hexdigest()
            input_count = reader.number()
            if input_count > 100000:
                raise ValueError("analyzer input inventory exceeds its bound")
            for _ in range(input_count):
                path = analyzer_text(reader.string())
                digest = analyzer_text(reader.string())
                metadata = analyzer_text(reader.string())
                rechecked = reader.number()
                if rechecked > 1:
                    raise ValueError("invalid analyzer content-recheck flag")
                input_inventory.append({"path": path, "sha256": digest, "metadata": metadata,
                                        "content_rechecked": bool(rechecked)})
            search_count = reader.number()
            if search_count > 100000:
                raise ValueError("analyzer search inventory exceeds its bound")
            for _ in range(search_count):
                path = analyzer_text(reader.string())
                entries = reader.number()
                fingerprint = analyzer_text(reader.string())
                search_inventory.append({"path": path, "entries": entries, "fingerprint": fingerprint})
        if index != expected_index or shard >= shards:
            raise ValueError("analyzer plan row identity or shard is invalid")
        rows.append({"index": index, "shard": shard, "representative": representative, "module": module,
                     "file": file, "directory": directory, "output": output, "original_argv": original_argv,
                     "argv": command, "proven": proven, "reason": reason,
                     "context_proof_present": context_proof_present,
                     "context_proof_sha256": context_proof_sha256, "input_inventory": input_inventory,
                     "search_inventory": search_inventory})
    if not reader.done():
        raise ValueError("trailing analyzer plan bytes")
    return {"version": version, "results": results, "config": config, "clang": clang, "shards": shards,
            "timeout": timeout, "selected_rows": count, "excluded_rows": excluded,
            "unique_executions": unique_executions, "aliases": aliases, "fixture": fixture,
            "database": database, "rows": rows}


def analyzer_parse_result(data: bytes, version: int) -> dict:
    """Parse one terminal shard result for either supported analyzer record version."""
    reader = AnalyzerRecordReader(data)
    expected_magic = f"BUSTER_CLANG_ANALYZE_RESULT_V{version}"
    magic = analyzer_text(reader.line())
    fingerprint = analyzer_text(reader.line())
    shard = reader.number()
    count = reader.number()
    executions = count
    aliases = 0
    if version == 2:
        executions = reader.number()
        aliases = reader.number()
    elapsed = reader.number()
    rss = reader.number()
    if magic != expected_magic or count > ANALYZER_ROW_LIMIT:
        raise ValueError("unknown or oversized analyzer shard result")
    rows = []
    for _ in range(count):
        index = reader.number()
        representative = index
        launched = True
        if version == 2:
            representative = reader.number()
            launched_value = reader.number()
            if launched_value > 1:
                raise ValueError("invalid analyzer launch flag")
            launched = bool(launched_value)
        status = reader.number()
        duration = reader.number()
        digest = analyzer_text(reader.line())
        if not SHA256.fullmatch(digest):
            raise ValueError("invalid analyzer diagnostic digest")
        rows.append({"index": index, "representative": representative, "launched": launched,
                     "status": status, "duration_us": duration, "log_sha256": digest})
    if not reader.done():
        raise ValueError("trailing analyzer shard result bytes")
    return {"version": version, "fingerprint": fingerprint, "shard": shard, "selected_rows": count,
            "executions": executions, "aliases": aliases, "elapsed_us": elapsed, "peak_child_rss_bytes": rss,
            "rows": rows}


def analyzer_plan_identity(plan: dict) -> dict:
    """Stable row, alias and inventory contract; proof digests bind repeated plans, not producer identity."""
    return {key: plan[key] for key in ("version", "config", "clang", "shards", "timeout", "selected_rows",
                                       "excluded_rows", "unique_executions", "aliases", "fixture", "database")} | {
        "rows": [{key: row[key] for key in ("index", "shard", "representative", "module", "file", "directory",
                                             "output", "original_argv", "argv", "proven", "reason",
                                             "context_proof_present", "context_proof_sha256", "input_inventory",
                                             "search_inventory")}
                 for row in plan["rows"]]}


def analyzer_candidate_plan_problems(plan: dict) -> list[str]:
    """Validate the V2 representative-owned context envelope and alias map."""
    problems: list[str] = []
    if plan.get("version") != 2:
        return ["candidate analyzer plan is not V2"]
    rows = plan.get("rows")
    if not isinstance(rows, list) or len(rows) != plan.get("selected_rows"):
        return ["candidate analyzer plan row count is inconsistent"]
    classes: dict[tuple, list[dict]] = {}
    for row in rows:
        representative = row.get("representative")
        if not isinstance(representative, int) or not 0 <= representative < len(rows):
            problems.append(f"candidate row {row.get('index')} names an invalid canonical representative")
            continue
        root = rows[representative]
        if root["representative"] != representative or root["shard"] != row["shard"]:
            problems.append(f"candidate row {row['index']} does not map to a same-shard canonical root")
            continue
        if (row["directory"], row["file"], row["shard"], row["argv"]) != \
                (root["directory"], root["file"], root["shard"], root["argv"]):
            problems.append(f"candidate row {row['index']} differs from its canonical invocation")
        key = (row["directory"], row["file"], row["shard"], tuple(row["argv"]))
        classes.setdefault(key, []).append(row)

    aliases = 0
    unique = 0
    for members in classes.values():
        canonical = min(members, key=lambda row: (row["directory"].encode("utf-8"),
                                                   row["file"].encode("utf-8"),
                                                   row["output"].encode("utf-8"), row["index"]))
        if len(members) == 1:
            unique += 1
            if canonical["representative"] != canonical["index"] or canonical["proven"] or \
                    canonical["reason"] != "single-row" or canonical["context_proof_present"] or \
                    canonical["input_inventory"] or canonical["search_inventory"]:
                problems.append(f"candidate singleton row {canonical['index']} has an invalid context envelope")
            continue

        aliases += len(members) - 1
        unique += 1
        root = canonical
        if root["representative"] != root["index"] or not root["proven"] or root["reason"] or \
                not root["context_proof_present"] or not root["input_inventory"] or not root["search_inventory"]:
            problems.append(f"candidate canonical root {root['index']} lacks its proven context envelope")
        for row in members:
            if row["representative"] != root["index"]:
                problems.append(f"candidate row {row['index']} does not target canonical root {root['index']}")
            if row["index"] != root["index"] and (not row["proven"] or row["reason"] or
                    row["context_proof_present"] or row["input_inventory"] or row["search_inventory"]):
                problems.append(f"candidate alias row {row['index']} carries non-canonical context")
        for item in root["input_inventory"]:
            if not Path(item["path"]).is_absolute() or not SHA256.fullmatch(item["sha256"]) or \
                    not item["metadata"] or not item["content_rechecked"]:
                problems.append(f"candidate canonical root {root['index']} has malformed input context")
                break
        for item in root["search_inventory"]:
            if not Path(item["path"]).is_absolute() or item["entries"] < 0 or \
                    not SHA256.fullmatch(item["fingerprint"]):
                problems.append(f"candidate canonical root {root['index']} has malformed search context")
                break
    if aliases != plan.get("aliases") or unique != plan.get("unique_executions") or \
            unique + aliases != plan.get("selected_rows"):
        problems.append("candidate V2 alias and unique-execution totals differ from its canonical mapping")
    return problems


def analyzer_bootstrap_provenance(driver: bytes, marker: bytes) -> dict:
    """Bind a copied native driver to its immutable bootstrap manifest and inputs."""
    lines = marker.decode("utf-8").splitlines()
    if len(lines) < 6 or lines[0] != "BUSTER_BOOTSTRAP_CACHE_V1" or lines[-1] != "END":
        raise ValueError("malformed bootstrap completion manifest")
    config = lines[1].split("\t")
    artifact = lines[2].split("\t")
    if len(config) != 2 or config[0] != "config" or not SHA256.fullmatch(config[1]) or \
            len(artifact) != 3 or artifact[0] != "artifact" or not re.fullmatch(r"build-[A-Za-z0-9-]+", artifact[1]) or \
            not SHA256.fullmatch(artifact[2]):
        raise ValueError("malformed bootstrap artifact identity")
    dependencies = {}
    for line in lines[3:-1]:
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] != "dependency" or not fields[1] or not SHA256.fullmatch(fields[2]) or fields[1] in dependencies:
            raise ValueError("malformed or repeated bootstrap dependency")
        dependencies[fields[1]] = fields[2]
    if not dependencies.get("build.c"):
        raise ValueError("bootstrap manifest does not bind build.c")
    if not SHA256.fullmatch(dependencies.get("tools/clang_analyze.c", "")) or \
            not SHA256.fullmatch(dependencies.get("tools/clang_analyze_benchmark.c", "")):
        raise ValueError("bootstrap manifest does not bind the analyzer and trusted benchmark helper sources")
    driver_digest = hashlib.sha256(driver).hexdigest()
    if driver_digest != artifact[2]:
        raise ValueError("copied build driver does not match its bootstrap manifest")
    result = {"sha256": driver_digest, "size_bytes": len(driver), "bootstrap_config": config[1],
              "bootstrap_manifest_sha256": hashlib.sha256(marker).hexdigest(),
              "bootstrap_manifest_size_bytes": len(marker), "dependency_count": len(dependencies),
              "build_c_sha256": dependencies["build.c"],
              "clang_analyze_sha256": dependencies["tools/clang_analyze.c"],
              "benchmark_helper_sha256": dependencies["tools/clang_analyze_benchmark.c"]}
    return result


def analyzer_phase_rows(data: bytes) -> list[dict]:
    """Read the helper's fixed tab-separated ProcessWaitResult evidence."""
    text = data.decode("utf-8")
    rows = list(csv.DictReader(text.splitlines(), delimiter="\t"))
    if not text.endswith("\n") or not rows or tuple(rows[0].keys()) != ANALYZER_PHASE_FIELDS:
        raise ValueError("malformed native analyzer phase table")
    parsed = []
    number_fields = ANALYZER_PHASE_FIELDS[2:7] + ANALYZER_PHASE_FIELDS[9:15]
    for row in rows:
        if set(row) != set(ANALYZER_PHASE_FIELDS):
            raise ValueError("native analyzer phase table columns changed")
        value = dict(row)
        for key in number_fields:
            if not re.fullmatch(r"(?:0|[1-9][0-9]*)", value[key]):
                raise ValueError(f"invalid native phase counter {key}")
            value[key] = int(value[key])
        parsed.append(value)
    return parsed


def analyzer_profile_summary(files: dict, identity: dict) -> tuple[dict, list[str]]:
    """Re-derive the full analyzer evidence from raw plans, shard results and logs."""
    problems: list[str] = []
    profile = json.loads(json.dumps(ANALYZER_PROFILE))
    baseline_revision = identity.get("base") if isinstance(identity, dict) else None
    candidate_revision = identity.get("head") if isinstance(identity, dict) else None
    summary = {"schema": ANALYZER_SUMMARY_SCHEMA, "status": "failed", "profile": profile,
               "baseline_revision": baseline_revision, "candidate_revision": candidate_revision,
               "request": {}, "clang": {}, "driver_checkouts": {}, "driver_provenance": {}, "compile_commands": {},
               "inventory": {}, "preparation": {},
               "measurements": [], "runs": [], "per_tu": [],
               "sampler": {"status": "incomplete", "reason": "no complete full-run sample evidence was verified"},
               "wait4_rss_scope": "largest individual high-water in each wait4 accounting scope; not simultaneous tree RSS"}
    for name in ANALYZER_REQUIRED_FILES:
        if not isinstance(files.get(name), bytes):
            problems.append(f"required analyzer evidence {name} is missing")
    if problems:
        summary["problems"] = problems
        return summary, problems
    try:
        helper_log = files["profile/helper.log"].decode("utf-8", "replace")
        helper_exit_match = re.search(r"(?:^|\n)exit=([0-9]+)(?:\n|$)", helper_log)
        summary["helper_exit"] = int(helper_exit_match.group(1)) if helper_exit_match else None
        helper_profile_line = re.search(r"ANALYZE_BENCHMARK_PROFILE name=clang-analyze-full-v1 elapsed_us=([0-9]+) phases=([0-9]+) status=(pass|fail)", helper_log)
        summary["native_helper"] = {"elapsed_us": int(helper_profile_line.group(1)) if helper_profile_line else None,
                                    "phase_count": int(helper_profile_line.group(2)) if helper_profile_line else None,
                                    "status": helper_profile_line.group(3) if helper_profile_line else "missing"}
        if summary["helper_exit"] != 0 or summary["native_helper"]["status"] != "pass" or summary["native_helper"]["phase_count"] != 10:
            problems.append("native helper did not complete the fixed ten-phase analyzer profile")
        request_bytes = files["request.txt"]
        request_lines = analyzer_text(request_bytes).splitlines()
        if ANALYZER_REQUEST_LINE not in request_lines:
            raise ValueError("retained profile request lacks the analyzer selector")
        summary["request"] = {"line": ANALYZER_REQUEST_LINE, "sha256": hashlib.sha256(request_bytes).hexdigest(),
                              "size_bytes": len(request_bytes)}
        compile_commands = files["compile_commands.json"]
        if not compile_commands:
            raise ValueError("empty compile_commands.json")
        summary["compile_commands"] = {"sha256": hashlib.sha256(compile_commands).hexdigest(),
                                       "size_bytes": len(compile_commands)}
        clang = analyzer_json(files["clang.json"])
        if not isinstance(clang, dict) or clang.get("schema") != "buster-analyzer-clang-provenance-v1" or \
                not isinstance(clang.get("path"), str) or not Path(clang["path"]).is_absolute() or \
                not SHA256.fullmatch(str(clang.get("sha256", ""))) or \
                not SHA256.fullmatch(str(clang.get("resource_tree_sha256", ""))):
            raise ValueError("trusted Clang executable/resource provenance is malformed")
        summary["clang"] = clang
        for role, revision in (("baseline", baseline_revision), ("candidate", candidate_revision)):
            checkout = analyzer_text(files[f"drivers/{role}.checkout"]).strip()
            if not SHA.fullmatch(checkout):
                raise ValueError(f"{role} driver checkout record is malformed")
            summary["driver_checkouts"][role] = checkout
            provenance = analyzer_bootstrap_provenance(files[f"drivers/{role}.driver"], files[f"drivers/{role}.complete"])
            provenance["source_revision"] = checkout
            summary["driver_provenance"][role] = provenance
        if summary["driver_checkouts"] != {"baseline": baseline_revision, "candidate": candidate_revision}:
            problems.append("native driver checkout records do not match the authorized base and head")
        if summary["driver_provenance"]["baseline"]["benchmark_helper_sha256"] != \
                summary["driver_provenance"]["candidate"]["benchmark_helper_sha256"]:
            problems.append("native benchmark helper source differs between baseline and candidate drivers")
        plans = {}
        result_parents = set()
        for label, role in (("prepare-baseline", "baseline"), ("prepare-candidate", "candidate"),
                            *((name, name.split("-", 1)[0]) for name in ANALYZER_RUNS)):
            data = files.get(f"profile/{label}/manifest.txt")
            if not isinstance(data, bytes):
                problems.append(f"analyzer manifest {label} is missing")
                continue
            plans[label] = analyzer_parse_plan(data)
            result_path = Path(plans[label]["results"])
            if not result_path.is_absolute() or result_path.name != label or result_path.parent.name != "profile":
                problems.append(f"analyzer manifest {label} does not bind its own fresh result directory")
            else:
                result_parents.add(str(result_path.parent))
        if len(plans) == 6 and len(result_parents) != 1:
            problems.append("analyzer preflight and full-run manifests do not share one exact fresh results root")
        baseline_plan = plans.get("prepare-baseline")
        candidate_plan = plans.get("prepare-candidate")
        if baseline_plan and candidate_plan:
            plans_ok = baseline_plan["version"] == 1 and candidate_plan["version"] == 2
            plans_ok = plans_ok and all(plan["config"] == "Release" and plan["shards"] == 8 and plan["timeout"] == 600 and
                                        plan["clang"] == clang["path"]
                                        for plan in (baseline_plan, candidate_plan))
            plans_ok = plans_ok and baseline_plan["selected_rows"] == ANALYZER_PROFILE["expected_rows"]
            plans_ok = plans_ok and candidate_plan["selected_rows"] == baseline_plan["selected_rows"]
            plans_ok = plans_ok and baseline_plan["database"] == candidate_plan["database"] == compile_commands
            plans_ok = plans_ok and baseline_plan["clang"] == candidate_plan["clang"]
            plans_ok = plans_ok and baseline_plan["excluded_rows"] == candidate_plan["excluded_rows"]
            baseline_semantics = [{key: row[key] for key in ("index", "shard", "module", "file", "argv")}
                                  for row in baseline_plan["rows"]]
            candidate_semantics = [{key: row[key] for key in ("index", "shard", "module", "file", "argv")}
                                   for row in candidate_plan["rows"]]
            semantic_digest = hashlib.sha256(json.dumps(baseline_semantics, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            plans_ok = plans_ok and baseline_semantics == candidate_semantics
            aliases = sum(row["representative"] != row["index"] for row in candidate_plan["rows"])
            unique = candidate_plan["selected_rows"] - aliases
            plans_ok = plans_ok and candidate_plan["unique_executions"] == unique == ANALYZER_PROFILE["expected_candidate_executions"]
            plans_ok = plans_ok and candidate_plan["aliases"] == aliases == ANALYZER_PROFILE["expected_candidate_aliases"]
            plans_ok = plans_ok and baseline_plan["unique_executions"] == baseline_plan["selected_rows"] and not baseline_plan["aliases"]
            plans_ok = plans_ok and not candidate_plan["fixture"]
            candidate_envelope_problems = analyzer_candidate_plan_problems(candidate_plan)
            if candidate_envelope_problems:
                problems.extend(candidate_envelope_problems)
                plans_ok = False
            candidate_alias_groups = len({row["representative"] for row in candidate_plan["rows"]
                                          if row["representative"] != row["index"]})
            summary["inventory"] = {"selected_rows": baseline_plan["selected_rows"],
                                    "excluded_rows": baseline_plan["excluded_rows"],
                                    "baseline_unique_executions": baseline_plan["selected_rows"],
                                    "candidate_unique_executions": candidate_plan["unique_executions"],
                                    "candidate_alias_rows": candidate_plan["aliases"],
                                    "candidate_alias_groups": candidate_alias_groups,
                                    "semantic_argv_sha256": semantic_digest,
                                    "database_sha256": hashlib.sha256(compile_commands).hexdigest()}
            if not plans_ok:
                problems.append("baseline V1 and candidate V2 plans differ from the fixed common full inventory or alias contract")
        else:
            baseline_semantics = []
            candidate_semantics = []
        for label, role in (("prepare-baseline", "baseline"), ("prepare-candidate", "candidate")):
            phase_name = f"prepare-{role}"
            stdout = files.get(f"profile/{phase_name}.stdout.log", b"")
            summary["preparation"][role] = {"process_wall_us": None, "internal_planning_us": None,
                                           "context_proof_us": None,
                                           "internal_counters": "unavailable in baseline PLAN_V1" if role == "baseline"
                                           else "missing candidate PLAN_V2 counters"}
            prepare_records = analyzer_log_records(stdout.decode("utf-8", "replace"), "ANALYZE_PREPARE")
            lines = [line for line in stdout.decode("utf-8", "replace").splitlines()
                     if line.startswith("ANALYZE_PREPARE ")]
            if len(lines) == 1:
                fields = prepare_records[0]
                planning_us = fields.get("planning_us", "")
                context_us = fields.get("context_proof_us", "")
                if not re.fullmatch(r"(?:0|[1-9][0-9]*)", planning_us) or not re.fullmatch(r"(?:0|[1-9][0-9]*)", context_us):
                    problems.append(f"{role} preparation lacks nonnegative planning/context-proof counters")
                else:
                    summary["preparation"][role]["internal_planning_us"] = int(planning_us)
                    summary["preparation"][role]["context_proof_us"] = int(context_us)
                    summary["preparation"][role]["internal_counters"] = "reported by ANALYZE_PREPARE"
                    if int(context_us) > int(planning_us):
                        problems.append(f"{role} preparation context proof exceeds its planning duration")
                if fields.get("status") not in (None, "pass"):
                    problems.append(f"{role} preparation reported status {fields.get('status')!r}")
                if role == "candidate" and candidate_plan:
                    analyzer_exact_fields(fields, ("selected_rows", "unique_executions", "aliases", "candidate_groups",
                                                    "proven_groups", "excluded_config_or_language", "planning_us",
                                                    "context_proof_us", "status"), "candidate ANALYZE_PREPARE")
                    expected_prepare = {"selected_rows": candidate_plan["selected_rows"],
                                        "unique_executions": candidate_plan["unique_executions"],
                                        "aliases": candidate_plan["aliases"],
                                        "candidate_groups": summary["inventory"].get("candidate_alias_groups"),
                                        "proven_groups": summary["inventory"].get("candidate_alias_groups"),
                                        "excluded_config_or_language": candidate_plan["excluded_rows"]}
                    for key, expected in expected_prepare.items():
                        if not re.fullmatch(r"(?:0|[1-9][0-9]*)", fields.get(key, "")) or int(fields[key]) != expected:
                            problems.append(f"candidate preparation {key} does not match its V2 manifest")
                    if fields.get("status") != "pass":
                        problems.append("candidate preparation did not report status=pass")
                    else:
                        summary["preparation"][role]["internal_counters"] = "reported by ANALYZE_PREPARE"
            elif role == "candidate":
                problems.append("candidate preparation does not contain exactly one ANALYZE_PREPARE counter record")
            elif prepare_records:
                problems.append("baseline PLAN_V1 unexpectedly emitted an ANALYZE_PREPARE record")
        phase_rows = analyzer_phase_rows(files["profile/profile.tsv"])
        # The native helper records every analysis immediately before its matching aggregate.
        expected_phases = [("prepare-baseline", "baseline", 0), ("prepare-candidate", "candidate", 0)]
        for trial, (name, role) in enumerate(zip(ANALYZER_RUNS, ANALYZER_PROFILE["order"])):
            expected_phases.extend(((f"analysis-{name}", role, trial), (f"aggregate-{name}", role, trial)))
        if len(phase_rows) != len(expected_phases):
            problems.append("native measurement table does not contain all 10 profile phases")
        for row, expected in zip(phase_rows, expected_phases):
            if (row["phase"], row["role"], row["trial"]) != expected:
                problems.append("native measurement phases do not follow the fixed preflight and B,C,C,B order")
                break
            if row["state"] != "complete" or row["cpu_status"] != "observed" or row["memory_status"] != "observed" or \
                    row["timed_out"] or row["cleanup_failed"] or row["capture_failed"] or row["exit_status"] != 0 or \
                    row["wall_us"] <= 0 or row["user_cpu_us"] + row["system_cpu_us"] <= 0 or row["peak_rss_bytes"] <= 0:
                problems.append(f"native {row['phase']} observation is incomplete")
            if row["phase"].startswith("prepare-"):
                summary["preparation"][row["role"]]["process_wall_us"] = row["wall_us"]
        summary["measurements"] = phase_rows
        run_results = {}
        sampler_states = []
        measurements = {row["phase"]: row for row in phase_rows}
        verified_run_count = 0
        for label, role in zip(ANALYZER_RUNS, ANALYZER_PROFILE["order"]):
            plan = plans.get(label)
            expected_version = 1 if role == "baseline" else 2
            version = plan["version"] if plan else expected_version
            manifest = files.get(f"profile/{label}/manifest.txt")
            prepared = plans.get(f"prepare-{role}")
            if plan is None:
                problems.append(f"analyzer run {label} manifest is missing")
                plan_rows = []
            else:
                plan_rows = plan["rows"]
                expected_results = label
                if plan["version"] != expected_version or not prepared or \
                        analyzer_plan_identity(plan) != analyzer_plan_identity(prepared) or \
                        Path(plan["results"]).name != expected_results or Path(plan["results"]).parent.name != "profile" or \
                        plan["database"] != compile_commands or plan["config"] != "Release" or \
                        plan["shards"] != 8 or plan["timeout"] != 600:
                    problems.append(f"analyzer run {label} does not reproduce its separate prepare plan")
                if plan["version"] == 2:
                    problems.extend(analyzer_candidate_plan_problems(plan))
            rows_by_index = {row["index"]: row for row in plan_rows}
            run_log_rows = {}
            run_log_data = {}
            observed_exec = observed_alias = 0
            terminal_shards = 0
            for shard in range(8):
                result_path = f"profile/{label}/shard-{shard}/result.txt"
                result_data = files.get(result_path)
                if not isinstance(result_data, bytes):
                    problems.append(f"analyzer run {label} is missing shard {shard} terminal result")
                    continue
                try:
                    shard_result = analyzer_parse_result(result_data, version)
                except (ValueError, KeyError, TypeError, IndexError, UnicodeDecodeError, OverflowError) as error:
                    problems.append(f"analyzer run {label} shard {shard} terminal result is malformed: {error}")
                    continue
                terminal_shards += 1
                expected_rows = [row for row in plan_rows if row["shard"] == shard]
                if not plan or shard_result["fingerprint"] != hashlib.sha256(manifest or b"").hexdigest() or \
                        shard_result["shard"] != shard or shard_result["selected_rows"] != len(expected_rows) or \
                        len(shard_result["rows"]) != len(expected_rows):
                    problems.append(f"analyzer run {label} shard {shard} header does not bind its plan")
                if [result["index"] for result in shard_result["rows"]] != \
                        [row["index"] for row in expected_rows]:
                    problems.append(f"analyzer run {label} shard {shard} rows do not match their planned order")
                if version == 2:
                    expected_exec = sum(row["representative"] == row["index"] for row in expected_rows)
                    expected_alias = len(expected_rows) - expected_exec
                    if plan and (shard_result["executions"] != expected_exec or
                                 shard_result["aliases"] != expected_alias):
                        problems.append(f"analyzer run {label} shard {shard} alias counts differ from its plan")
                    observed_exec += shard_result["executions"]
                    observed_alias += shard_result["aliases"]
                for result_row in shard_result["rows"]:
                    index = result_row["index"]
                    plan_row = rows_by_index.get(index)
                    expected_rep = plan_row["representative"] if plan_row and version == 2 else index
                    expected_launch = expected_rep == index
                    if plan_row is None or plan_row["shard"] != shard or \
                            result_row["representative"] != expected_rep or result_row["launched"] != expected_launch or \
                            result_row["status"] != 0 or (expected_launch and result_row["duration_us"] <= 0) or \
                            (not expected_launch and result_row["duration_us"] != 0):
                        problems.append(f"analyzer run {label} row {index} is incomplete or inconsistent")
                    log_path = f"profile/{label}/shard-{shard}/unit-{index}.log"
                    log_data = files.get(log_path)
                    log_bound = isinstance(log_data, bytes) and \
                        hashlib.sha256(log_data).hexdigest() == result_row["log_sha256"]
                    if not log_bound:
                        problems.append(f"analyzer run {label} row {index} diagnostic log is missing or unbound")
                    if index in run_log_rows:
                        problems.append(f"analyzer run {label} contains duplicate result row {index}")
                        continue
                    run_log_rows[index] = {"status": result_row["status"], "duration_us": result_row["duration_us"],
                                           "log_sha256": result_row["log_sha256"], "launched": result_row["launched"],
                                           "representative": result_row["representative"],
                                           "diagnostic_log_path": log_path, "diagnostic_log_bound": log_bound}
                    if isinstance(log_data, bytes):
                        run_log_data[index] = log_data
                if version == 1:
                    observed_exec += len(shard_result["rows"])
            if plan:
                expected_executions = plan["selected_rows"] if version == 1 else plan["unique_executions"]
                expected_aliases = 0 if version == 1 else plan["aliases"]
                if observed_exec != expected_executions or observed_alias != expected_aliases:
                    problems.append(f"analyzer run {label} executed/alias counts differ from its plan")
            for index, result_row in run_log_rows.items():
                representative = result_row["representative"]
                if representative != index and (run_log_data.get(index) is None or
                                                  run_log_data.get(index) != run_log_data.get(representative) or
                                                  result_row["log_sha256"] != run_log_rows.get(representative, {}).get("log_sha256")):
                    problems.append(f"analyzer run {label} alias row {index} does not match canonical root {representative}")

            analysis_stdout = files.get(f"profile/analysis-{label}.stdout.log", b"").decode("utf-8", "replace")
            aggregate_stdout = files.get(f"profile/aggregate-{label}.stdout.log", b"").decode("utf-8", "replace")
            run_record = analyzer_observed_record(analysis_stdout, "ANALYZE_RUN")
            analysis_aggregate_record = analyzer_observed_record(analysis_stdout, "ANALYZE_AGGREGATE")
            independent_aggregate_record = analyzer_observed_record(aggregate_stdout, "ANALYZE_AGGREGATE")
            for record, description in ((run_record, "ANALYZE_RUN"),
                                        (analysis_aggregate_record, "in-run aggregate"),
                                        (independent_aggregate_record, "independent aggregate")):
                if record["record_status"] != "observed":
                    problems.append(f"analyzer run {label} {description} record is {record['record_status']}")
            internal_costs = analyzer_parse_run_cost_observations(analysis_stdout, aggregate_stdout, plan, label)
            for record_set, state in internal_costs["record_sets"].items():
                if state not in ("complete", "not-applicable"):
                    problems.append(f"analyzer run {label} {record_set} cost record set is {state}")
            for record_set in ("shard_records", "analysis_aggregate_records", "independent_aggregate_records"):
                for record in internal_costs[record_set]:
                    if record.get("status") == "fail":
                        item = record.get("shard", "aggregate")
                        problems.append(f"analyzer run {label} {record_set} {item} reports status=fail")
            try:
                if plan is None:
                    raise ValueError("analyzer run manifest is unavailable")
                analyzer_parse_run_costs(analysis_stdout, aggregate_stdout, plan, label)
                internal_costs["validation_status"] = "complete"
                cost_validation_status = "complete"
            except (ValueError, KeyError, TypeError, IndexError, UnicodeDecodeError, OverflowError) as error:
                internal_costs["validation_status"] = "failed"
                internal_costs["reason"] = str(error)
                cost_validation_status = "failed"
                problems.append(f"analyzer run {label} internal cost records are incomplete: {error}")

            run_fields = run_record["fields"]
            required_run_counters = ("elapsed_us", "peak_pending_workers", "jobs", "samples", "peak_live_processes",
                                     "sampled_peak_tree_rss_bytes")
            parsed_run_counters = {}
            for key in required_run_counters:
                try:
                    parsed_run_counters[key] = analyzer_uint(run_fields, key, f"analyzer run {label}")
                except ValueError as error:
                    parsed_run_counters[key] = None
                    problems.append(str(error))
            sampler_status = run_fields.get("process_tree_status", "missing")
            sampler_reason = run_fields.get("process_tree_reason", "missing")
            sampler_complete = run_record["record_status"] == "observed" and sampler_status == "complete" and \
                sampler_reason == "none" and all(isinstance(parsed_run_counters[key], int) and
                parsed_run_counters[key] > 0 for key in ("samples", "peak_live_processes", "sampled_peak_tree_rss_bytes"))
            sampler_states.append(sampler_complete)
            summary["sampler"].setdefault("runs", []).append({"name": label,
                "record_status": run_record["record_status"], "status": sampler_status,
                "reason": sampler_reason, **parsed_run_counters})
            if not sampler_complete:
                problems.append(f"analyzer run {label} whole-tree sampler is incomplete or unavailable")
            if run_fields.get("status") != "pass" or (plan and run_fields.get("results") != plan["results"]) or \
                    parsed_run_counters.get("jobs") != ANALYZER_PROFILE["jobs"] or \
                    not (isinstance(parsed_run_counters.get("peak_pending_workers"), int) and
                         1 <= parsed_run_counters["peak_pending_workers"] <= ANALYZER_PROFILE["jobs"]):
                problems.append(f"analyzer run {label} driver record does not match the fixed successful run")

            analysis_phase = measurements.get(f"analysis-{label}", {})
            aggregate_phase = measurements.get(f"aggregate-{label}", {})
            selected_rows = plan["selected_rows"] if plan else None
            successful_rows = sum(row["status"] == 0 for row in run_log_rows.values())
            failed_rows = sum(row["status"] != 0 for row in run_log_rows.values())
            result_coverage = {"planned_rows": selected_rows, "terminal_shards": terminal_shards,
                               "observed_rows": len(run_log_rows), "passing_rows_observed": successful_rows,
                               "failed_rows_observed": failed_rows,
                               "unrecorded_rows": max(0, selected_rows - len(run_log_rows))
                               if selected_rows is not None else None}
            aggregate_statuses = (analysis_aggregate_record["fields"].get("status"),
                                  independent_aggregate_record["fields"].get("status"))
            all_rows_pass = selected_rows is not None and result_coverage["unrecorded_rows"] == 0 and \
                failed_rows == 0 and all(row["diagnostic_log_bound"] for row in run_log_rows.values())
            phase_statuses = (analysis_phase.get("state"), aggregate_phase.get("state"))
            fully_verified = cost_validation_status == "complete" and run_record["record_status"] == "observed" and \
                run_fields.get("status") == "pass" and sampler_complete and all_rows_pass and \
                aggregate_statuses == ("pass", "pass") and phase_statuses == ("complete", "complete") and \
                analysis_phase.get("exit_status") == 0 and aggregate_phase.get("exit_status") == 0
            if fully_verified:
                observed_status = "complete"
                verified_run_count += 1
            elif run_fields.get("status") == "fail" or "failed" in phase_statuses or failed_rows or \
                    "fail" in aggregate_statuses:
                observed_status = "failed"
            else:
                observed_status = "partial"
            summary_run = {"name": label, "role": role, "order_index": ANALYZER_RUNS.index(label),
                           "status": observed_status, "selected_rows": selected_rows,
                           "planned_unique_executions": plan["unique_executions"] if plan else None,
                           "planned_alias_rows": plan["aliases"] if plan and version == 2 else 0 if plan else None,
                           "executed_tus": observed_exec if terminal_shards else None,
                           "alias_rows": observed_alias if terminal_shards else None,
                           "result_coverage": result_coverage,
                           "analysis_phase_state": analysis_phase.get("state"),
                           "analysis_exit_status": analysis_phase.get("exit_status"),
                           "aggregate_phase_state": aggregate_phase.get("state"),
                           "aggregate_exit_status": aggregate_phase.get("exit_status"),
                           "analysis_process_wall_us": analysis_phase.get("wall_us"),
                           "independent_aggregate_process_wall_us": aggregate_phase.get("wall_us"),
                           "analysis_wait4_user_cpu_us": analysis_phase.get("user_cpu_us"),
                           "analysis_wait4_system_cpu_us": analysis_phase.get("system_cpu_us"),
                           "analysis_wait4_largest_individual_rss_bytes": analysis_phase.get("peak_rss_bytes"),
                           "aggregate_wait4_user_cpu_us": aggregate_phase.get("user_cpu_us"),
                           "aggregate_wait4_system_cpu_us": aggregate_phase.get("system_cpu_us"),
                           "aggregate_wait4_largest_individual_rss_bytes": aggregate_phase.get("peak_rss_bytes"),
                           "aggregate_includes": "replanning and verification; no separate aggregate-only internal timer",
                           "analysis_aggregate_record": analysis_aggregate_record,
                           "independent_aggregate_record": independent_aggregate_record,
                           "internal_costs": internal_costs,
                           "driver_record": run_record["raw_record"] or run_record.get("raw_records"),
                           "driver_record_status": run_record["record_status"],
                           "driver_reported_status": run_fields.get("status"),
                           "driver_reported_results": run_fields.get("results"),
                           "driver_reported_run_elapsed_us": parsed_run_counters["elapsed_us"],
                           "peak_pending_workers": parsed_run_counters["peak_pending_workers"],
                           "peak_live_processes": parsed_run_counters["peak_live_processes"],
                           "whole_tree_sampler_status": sampler_status,
                           "whole_tree_sampler_reason": sampler_reason,
                           "sampled_peak_tree_rss_bytes": parsed_run_counters["sampled_peak_tree_rss_bytes"],
                           "per_tu_elapsed_us": {str(index): row["duration_us"]
                                                 for index, row in sorted(run_log_rows.items())}}
            summary["runs"].append(summary_run)
            run_results[label] = run_log_rows
        if verified_run_count != len(ANALYZER_RUNS):
            problems.append("not all four full analyzer runs were independently verified")
        if len(sampler_states) == len(ANALYZER_RUNS) and all(sampler_states):
            summary["sampler"].update(status="complete", scope="sampled process-tree RSS lower bound",
                                       completeness="all four ANALYZE_RUN records reported complete")
        per_tu_plan = baseline_plan or candidate_plan
        if per_tu_plan:
            per_tu = []
            for row in per_tu_plan["rows"]:
                index = row["index"]
                candidate_row = candidate_plan["rows"][index] if candidate_plan else None
                run_evidence = {}
                for label in ANALYZER_RUNS:
                    result_row = run_results.get(label, {}).get(index)
                    result_state = "missing" if result_row is None else \
                        "passed" if result_row["status"] == 0 else "failed"
                    run_evidence[label] = {"state": result_state,
                                           "status": result_row["status"] if result_row else None,
                                           "duration_us": result_row["duration_us"] if result_row else None,
                                           "diagnostics_sha256": result_row["log_sha256"] if result_row else None,
                                           "diagnostic_log_path": result_row["diagnostic_log_path"] if result_row else None,
                                           "diagnostic_log_bound": result_row["diagnostic_log_bound"] if result_row else None,
                                           "representative": result_row["representative"] if result_row else None,
                                           "launched": result_row["launched"] if result_row else None}
                observed_hashes = [item["diagnostics_sha256"] for item in run_evidence.values()
                                   if item["diagnostics_sha256"] is not None]
                all_hashes_observed = len(observed_hashes) == len(ANALYZER_RUNS)
                diagnostics_stable = len(set(observed_hashes)) == 1 if all_hashes_observed else None
                if diagnostics_stable is False:
                    problems.append(f"diagnostics differ across baseline/candidate repeats for row {index}")
                baseline_durations = [run_evidence[label]["duration_us"] for label in ("baseline-0", "baseline-1")]
                candidate_durations = [run_evidence[label]["duration_us"] if candidate_row and
                                       candidate_row["representative"] == index else None
                                       for label in ("candidate-0", "candidate-1")]
                baseline_rows_pass = all(run_evidence[label]["state"] == "passed" and
                                         run_evidence[label]["duration_us"] is not None and
                                         run_evidence[label]["duration_us"] > 0
                                         for label in ("baseline-0", "baseline-1"))
                candidate_rows_pass = candidate_row is not None and candidate_row["representative"] == index and \
                    all(run_evidence[label]["state"] == "passed" and
                        run_evidence[label]["duration_us"] is not None and
                        run_evidence[label]["duration_us"] > 0
                        for label in ("candidate-0", "candidate-1"))
                candidate_launches = [run_evidence[label]["launched"] for label in ("candidate-0", "candidate-1")]
                candidate_execution_observed = candidate_launches[0] \
                    if all(isinstance(value, bool) for value in candidate_launches) and \
                    candidate_launches[0] == candidate_launches[1] else None
                first_diagnostic = next((item["diagnostics_sha256"] for item in run_evidence.values()
                                         if item["diagnostics_sha256"] is not None), None)
                per_tu.append({"index": index, "module": row["module"], "file": row["file"],
                               "diagnostics_sha256": first_diagnostic,
                               "diagnostics_stable_across_runs": diagnostics_stable,
                               "candidate_representative": candidate_row["representative"] if candidate_row else None,
                               "candidate_planned_execution": candidate_row["representative"] == index
                               if candidate_row else None,
                               "candidate_executed": candidate_execution_observed,
                               "run_evidence": run_evidence,
                               "baseline_elapsed_us": baseline_durations,
                               "candidate_elapsed_us": candidate_durations,
                               "baseline_median_elapsed_us": statistics.median(baseline_durations)
                               if baseline_rows_pass else None,
                               "candidate_median_elapsed_us": statistics.median(candidate_durations)
                               if candidate_rows_pass else None})
            summary["per_tu"] = per_tu

        def phase_total(prefix: str, key: str, expected_count: int) -> int | None:
            rows = [row for row in phase_rows if row["phase"].startswith(prefix)]
            if len(rows) != expected_count or any(not isinstance(row.get(key), int) for row in rows):
                return None
            return sum(row[key] for row in rows)

        summary["totals"] = {
            "preflight_process_wall_us": phase_total("prepare-", "wall_us", 2),
            "matched_full_analysis_process_wall_us": phase_total("analysis-", "wall_us", 4),
            "independent_aggregate_process_wall_us": phase_total("aggregate-", "wall_us", 4),
            "all_native_phase_process_wall_us": sum(row["wall_us"] for row in phase_rows)
            if len(phase_rows) == len(expected_phases) else None,
            "wait4_user_cpu_us": phase_total("", "user_cpu_us", len(expected_phases)),
            "wait4_system_cpu_us": phase_total("", "system_cpu_us", len(expected_phases)),
            "wait4_peak_largest_individual_rss_bytes": max((row["peak_rss_bytes"] for row in phase_rows), default=None)
            if len(phase_rows) == len(expected_phases) else None,
            "whole_tree_sampler_complete": summary["sampler"].get("status") == "complete",
        }
    except (ValueError, KeyError, TypeError, IndexError, UnicodeDecodeError, OverflowError) as error:
        problems.append(f"analyzer evidence is malformed or incomplete: {error}")
    summary["status"] = "complete" if not problems else "failed"
    summary["comparison_invalid"] = bool(problems)
    if summary["comparison_invalid"]:
        for row in summary.get("per_tu", []):
            row["baseline_median_elapsed_us"] = None
            row["candidate_median_elapsed_us"] = None
    summary["problems"] = problems
    return summary, problems


def validate_analyzer_bundle(receipt: object, summary: object, bundle: object) -> list[str]:
    """Re-derive the report from raw evidence and bind it to the authorized request."""
    problems: list[str] = []
    if not isinstance(receipt, dict) or receipt.get("profile") != ANALYZER_PROFILE:
        return ["receipt does not name the frozen clang-analyze-full-v1 profile"]
    if receipt.get("mode") != "pull":
        problems.append("clang-analyze-full-v1 is valid only in pull mode")
    if receipt.get("analyzer_request_line") != ANALYZER_REQUEST_LINE:
        problems.append("receipt does not name the exact analyzer profile selector")
    if not isinstance(bundle, dict) or not isinstance(bundle.get("files"), dict):
        return [*problems, "raw analyzer evidence bundle is missing"]
    derived, errors = analyzer_profile_summary(bundle["files"], receipt.get("identity"))
    problems.extend(errors)
    if summary != derived:
        problems.append("retained analyzer summary does not match the raw V1/V2 plans, results and logs")
    if receipt.get("analyzer") != derived:
        problems.append("receipt analyzer section does not match the independently re-derived evidence")
    if receipt.get("analyzer_request_sha256") != derived.get("request", {}).get("sha256"):
        problems.append("receipt analyzer request digest does not match its retained exact selector file")
    if receipt.get("analyzer_clang_provenance") != derived.get("clang"):
        problems.append("receipt Clang executable/resource provenance does not match the retained raw manifest")
    if receipt.get("analyzer_driver_checkouts") != derived.get("driver_checkouts"):
        problems.append("receipt native driver checkout observations do not match retained raw evidence")
    if receipt.get("analyzer_driver_provenance") != derived.get("driver_provenance"):
        problems.append("receipt native driver hashes/bootstrap provenance do not match retained raw evidence")
    return problems


def check_name(mode: str = "main") -> str:
    return MODES[mode][0]


def check_marker(head: str, mode: str = "main") -> str:
    if not (isinstance(head, str) and SHA.fullmatch(head)):
        raise ValueError("check marker needs an exact 40-hex head")
    return MODES[mode][1] + ":" + head


def attempt_marker(head: str, mode: str, request_run_id: str, request_attempt: str, run_attempt: str) -> str:
    """External ID of one measurement attempt's check (#2803).

    The request run and its attempt name the scheduling, the bench attempt the
    measurement, so a transport retry finds the same check while a deliberate
    re-run (of either workflow) gets its own. The external ID is only a lookup
    key; ownership also needs the GitHub Actions app, the name and the head.
    """
    for value in (request_run_id, request_attempt, run_attempt):
        if not (isinstance(value, str) and DECIMAL.fullmatch(value)):
            raise ValueError("attempt marker needs decimal run and attempt numbers")
    return f"{check_marker(head, mode)}:{request_run_id}.{request_attempt}:{run_attempt}"


def observed_cpu_model() -> str:
    """The running host's CPU model name from /proc/cpuinfo, or 'NA'."""
    model = "NA"
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("model name"):
                model = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    return model


def host_problem(receipt: object) -> str:
    """Why the receipt's observed host is not the approved Zen 5 host, or ''."""
    host = receipt.get("host") if isinstance(receipt, dict) else None
    model = host.get("cpu_model") if isinstance(host, dict) else None
    problem = ""
    if not (isinstance(model, str) and APPROVED_HOST.search(model)):
        problem = f"observed CPU {model!r} is not the approved Zen 5 host (AMD Ryzen 7 9700X)"
    return problem


def regression_policy(value: str) -> tuple[str, str]:
    """(policy, problem): an unset variable is report-only; anything else unknown fails."""
    policy = value.strip() if isinstance(value, str) else ""
    policy = policy or "report-only"
    problem = ""
    if policy not in REGRESSION_POLICIES:
        problem = (f"regression policy {policy!r} is not implemented; only report-only exists until "
                   "regression thresholds are qualified (#2752)")
    return policy, problem


def range_label(commits: object, first_parent: object) -> str:
    """A main baseline's relation to the head, from the authorized range size, or '' when unknown.

    commits counts the first-parent main commits the comparison spans: 1 is
    the head alone against its first parent.
    """
    label = ""
    if commits == "1":
        label = "first parent"
    elif isinstance(commits, str) and DECIMAL.fullmatch(commits) and isinstance(first_parent, str) and \
            SHA.fullmatch(first_parent):
        label = (f"range of {commits} first-parent main commits: the nearest earlier main commit with a valid "
                 f"measurement, because first parent `{first_parent}` has none; the result covers the whole range "
                 "and does not isolate one commit")
    return label


def is_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def validate_closure(receipt: dict, bundle: object) -> list[str]:
    """Replay native producer/consumer identities as bounded data, including the frozen baseline executable."""
    closure = receipt.get("closure")
    if closure is None:
        return []  # historical and default legacy-rebuild receipts
    if not isinstance(closure, dict) or closure.get("policy") != "snapshot-v1" or closure.get("fallback") is not None:
        return ["frozen baseline closure policy/fallback is unsupported"]
    identity = receipt.get("identity") if isinstance(receipt.get("identity"), dict) else {}
    binaries = receipt.get("binaries") if isinstance(receipt.get("binaries"), dict) else {}
    raw = bundle if isinstance(bundle, dict) else {}
    reasons: list[str] = []
    manifests: list[bytes] = []
    for operation in ("snapshot", "restore"):
        record = closure.get(operation)
        manifest = raw.get(operation)
        if not isinstance(record, dict) or not isinstance(manifest, bytes) or not 0 < len(manifest) <= 8 * 1024 * 1024:
            reasons.append(f"frozen baseline {operation} receipt/manifest missing or oversized")
            continue
        manifests.append(manifest)
        expected = {"schema": "buster-compiler-closure-v1", "policy": "snapshot-v1", "state": "complete",
                    "operation": operation, "base": identity.get("base"), "base_tree": identity.get("base_tree")}
        if any(record.get(key) != value for key, value in expected.items()):
            reasons.append(f"frozen baseline {operation} source/tree/policy identity mismatch")
        if record.get("manifest_sha256") != hashlib.sha256(manifest).hexdigest():
            reasons.append(f"frozen baseline {operation} manifest hash mismatch")
        if type(record.get("duration_us")) is not int or record["duration_us"] < 0:
            reasons.append(f"frozen baseline {operation} duration missing or malformed")
    if len(manifests) == 2 and manifests[0] != manifests[1]:
        reasons.append("frozen baseline restore differs from the saved source/generated/configuration/toolchain closure")
    if manifests:
        try:
            lines = manifests[0].decode("utf-8").splitlines()
            if len(lines) < 5 or lines[0] != "BUSTER_COMPILER_CLOSURE_V1" or \
                    lines[2] != "base\t" + str(identity.get("base")) or lines[3] != "tree\t" + str(identity.get("base_tree")) or \
                    not lines[1].startswith("root\t/"):
                raise ValueError("manifest source/tree/root header mismatch")
            root_hash = hashlib.sha256(lines[1][5:].encode()).hexdigest()
            if any(not isinstance(closure.get(operation), dict) or closure[operation].get("root_sha256") != root_hash
                   for operation in ("snapshot", "restore")):
                raise ValueError("manifest matched root identity mismatch")
            rows: dict[tuple[str, str], list[str]] = {}
            total = 0
            bindings: dict[str, str] = {}
            for line in lines[4:-1]:
                fields = line.split("\t")
                if fields[0] == "binding":
                    if len(fields) != 3 or fields[1] in bindings:
                        raise ValueError("manifest toolchain binding malformed or repeated")
                    bindings[fields[1]] = fields[2]
                    continue
                if len(fields) != 8 or fields[0] not in ("source", "build", "bootstrap", "tool", "resource") or \
                        fields[1] not in ("F", "D") or not all(value.isdecimal() for value in fields[2:6]) or \
                        any(part in ("", ".", "..") for part in fields[7].split("/")):
                    raise ValueError("manifest record is malformed or unsafe")
                key = fields[0], fields[7]
                if key in rows or (fields[1] == "F" and not SHA256.fullmatch(fields[6])) or \
                        (fields[1] == "D" and (fields[6] != "-" or any(int(value) for value in fields[3:6]))) or \
                        int(fields[2]) > 4095 or int(fields[4]) >= 1_000_000_000:
                    raise ValueError("manifest record duplicate or invalid hash")
                rows[key] = fields
                total += int(fields[5])
            if lines[-1] != f"END\t{len(rows)}\t{total}" or len(rows) > 65536 or total > 8 << 30:
                raise ValueError("manifest file/byte inventory mismatch")
            required = (("source", "build.c"), ("source", "build.sh"), ("source", "tools/bootstrap_driver.sh"),
                        ("build", "CMakeCache.txt"), ("build", "Release/ide"), ("build", "throughput-tools/throughput"))
            if any(key not in rows for key in required) or not any(key[0] == "bootstrap" and key[1].endswith(".complete") for key in rows):
                raise ValueError("manifest baseline source/generated/build-driver/harness closure is incomplete")
            baseline = binaries.get("baseline") if isinstance(binaries.get("baseline"), dict) else {}
            if rows["build", "Release/ide"][6] != baseline.get("sha256"):
                raise ValueError("manifest baseline compiler binary mismatch")
            for key in ("CMAKE_C_COMPILER", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM", "clang", "cmake", "ninja", "tcc", "resource"):
                if not bindings.get(key, "").startswith("/") or (key != "resource" and ("tool", key) not in rows):
                    raise ValueError("manifest configured compiler/linker/tool/resource identity missing")
        except (UnicodeError, ValueError, IndexError, TypeError):
            reasons.append("frozen baseline manifest source/toolchain/configuration/closure mismatch")
    return reasons


def classify(summary: object, binaries: object) -> list[str]:
    """Reasons the lab summary is not a valid core measurement; empty when valid."""
    reasons: list[str] = []
    if not isinstance(summary, dict):
        summary = {}
        reasons.append("summary.json is missing or not an object")
    if not isinstance(binaries, dict):
        binaries = {}
    if summary and summary.get("schema") != LAB_SCHEMA:
        reasons.append(f"lab schema {summary.get('schema')!r} is not {LAB_SCHEMA}")
    for role in ("baseline", "candidate"):
        variant = summary.get(role) if isinstance(summary.get(role), dict) else {}
        recorded = binaries.get(role) if isinstance(binaries.get(role), dict) else {}
        digest = recorded.get("sha256")
        if not (isinstance(digest, str) and SHA256.fullmatch(digest)) or variant.get("sha256") != digest:
            reasons.append(f"{role} binary hash does not match the built binary")
        if variant.get("failed") != 0 or type(variant.get("runs")) is not int or variant.get("runs", 0) < 1:
            reasons.append(f"{role} has failed or missing timed runs")
        if variant.get("deterministic") is not True:
            reasons.append(f"{role} output was not byte-identical across its own runs")
    plan = summary.get("plan") if isinstance(summary.get("plan"), dict) else {}
    pairs = plan.get("complete_pairs")
    if type(pairs) is not int or pairs < MIN_PAIRS:
        reasons.append(f"{pairs!r} complete pairs; at least {MIN_PAIRS} are required")
    # uarch_lab compare runs one A and one B member per planned pair (warmups and the fresh-copy reference
    # are not timed runs), keeps going after a failed member, and counts a pair complete when both succeeded.
    # A finished experiment therefore has runs == plan.pairs == complete_pairs on both sides; a failure
    # or truncation leaves fewer, and a shortened plan is not a completed one.
    planned = plan.get("pairs")
    if type(planned) is not int or planned < MIN_PAIRS:
        reasons.append(f"declared plan has {planned!r} pairs; the declared sample plan is required and needs "
                       f"at least {MIN_PAIRS}")
    counts = {role: summary.get(role, {}).get("runs") if isinstance(summary.get(role), dict) else None
              for role in ("baseline", "candidate")}
    if type(counts["baseline"]) is int and type(counts["candidate"]) is int and \
            counts["baseline"] != counts["candidate"]:
        reasons.append(f"baseline ran {counts['baseline']} times but candidate {counts['candidate']}; "
                       "every pair has one run of each")
    for role, count in counts.items():
        if type(count) is int and type(pairs) is int and count != pairs:
            reasons.append(f"{role} has {count} timed runs but {pairs} complete pairs are claimed")
        if type(count) is int and type(planned) is int and count != planned:
            reasons.append(f"{role} has {count} timed runs but the plan declares {planned} pairs; "
                           "the experiment did not complete as declared")
    if type(pairs) is int and type(planned) is int and pairs != planned:
        reasons.append(f"{pairs} complete pairs do not match the {planned} planned pairs")
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) else {}
    metrics = summary.get("metrics")
    if verdict.get("metric") != "wall" or verdict.get("outcome") not in MEASURED_OUTCOMES:
        reasons.append(f"wall-time verdict {verdict.get('outcome')!r} is not a complete measurement")
    wall = metrics.get("wall") if isinstance(metrics, dict) and isinstance(metrics.get("wall"), dict) else {}
    if not wall:
        reasons.append("summary has no wall metric record to check the verdict against")
    for key in ("ratio", "ci_low", "ci_high"):
        value = verdict.get(key)
        if not is_number(value):
            reasons.append(f"wall-time verdict has no numeric {key}")
        elif not (math.isfinite(value) and value > 0):
            reasons.append(f"wall-time verdict {key} {value!r} is not a finite positive ratio")
        elif wall and wall.get(key) != value:
            reasons.append(f"wall-time verdict {key} {value!r} contradicts the wall metric {wall.get(key)!r}")
    low, high = verdict.get("ci_low"), verdict.get("ci_high")
    if is_number(low) and is_number(high) and low > high:
        reasons.append(f"wall-time verdict confidence interval is reversed: ci_low {low!r} > ci_high {high!r}")
    if wall and wall.get("outcome") != verdict.get("outcome"):
        reasons.append(f"wall-time verdict outcome {verdict.get('outcome')!r} contradicts the wall metric "
                       f"{wall.get('outcome')!r}")
    return reasons


def classify_throughput(summary: object, metadata: object, binaries: object) -> list[str]:
    """Reasons the corpus evidence is not a complete run of THROUGHPUT_PROFILE on these binaries."""
    reasons: list[str] = []
    summary = summary if isinstance(summary, dict) else {}
    metadata = metadata if isinstance(metadata, dict) else {}
    binaries = binaries if isinstance(binaries, dict) else {}
    if not summary:
        reasons.append("throughput summary.json is missing or not an object")
    if not metadata:
        reasons.append("throughput metadata.json is missing or not an object")
    if summary and (summary.get("schema") != THROUGHPUT_SCHEMA or summary.get("valid") is not True
                    or summary.get("guard_enabled") is not True):
        reasons.append("throughput summary is not a valid guarded schema-2 comparison")
    profile = THROUGHPUT_PROFILE
    planned = {"schema": THROUGHPUT_SCHEMA, "profile": "ci", "pairs_per_round": profile["pairs_per_round"],
               "rounds": profile["rounds"], "warmups": profile["warmups"], "cpu": profile["cpu"],
               "workloads": profile["workloads"]}
    for key, value in planned.items():
        if metadata and metadata.get(key) != value:
            reasons.append(f"throughput {key} {metadata.get(key)!r} is not the profile's {value!r}")
    provenance = metadata.get("compiler_provenance")
    provenance = provenance if isinstance(provenance, list) and len(provenance) == 2 else [{}, {}]
    for role, row in zip(("baseline", "candidate"), provenance):
        recorded = binaries.get(role) if isinstance(binaries.get(role), dict) else {}
        digest = row.get("sha256") if isinstance(row, dict) else None
        if not (isinstance(digest, str) and SHA256.fullmatch(digest)) or digest != recorded.get("sha256"):
            reasons.append(f"throughput {role} compiler is not the measured {role} binary")
    if summary:
        reasons.extend(classify_throughput_cases(summary.get("comparisons")))
        counted = {"confirmed_regressions": "regression", "inconclusive_cases": "inconclusive"}
        comparisons = summary.get("comparisons") if isinstance(summary.get("comparisons"), list) else []
        for key, decision in counted.items():
            value = summary.get(key)
            actual = sum(1 for row in comparisons if isinstance(row, dict) and row.get("decision") == decision)
            if type(value) is not int:
                reasons.append(f"throughput summary has no integer {key}")
            elif value != actual:
                reasons.append(f"throughput summary {key} {value} does not match its {actual} {decision!r} cases")
    return reasons


def classify_throughput_cases(comparisons: object) -> list[str]:
    """Reasons the corpus rows are not exactly one valid row per profile workload x allocator mode."""
    reasons: list[str] = []
    rounds = THROUGHPUT_PROFILE["rounds"]
    expected = [f"{name}/{mode}" for name in THROUGHPUT_PROFILE["workloads"] for mode in THROUGHPUT_PROFILE["modes"]]
    rows = comparisons if isinstance(comparisons, list) else []
    if not isinstance(comparisons, list):
        reasons.append("throughput summary has no comparisons list")
    names = [row.get("name") if isinstance(row, dict) else None for row in rows]
    seen: set = set()
    duplicates = sorted({name for name in names if isinstance(name, str) and (name in seen or seen.add(name))})
    if duplicates:
        reasons.append(f"throughput comparisons repeat cells {duplicates}")
    missing = [name for name in expected if name not in seen]
    if missing:
        reasons.append(f"throughput comparisons miss {len(missing)} of {len(expected)} workload/mode cells: {missing}")
    foreign = sorted({str(name) for name in names if name not in expected})
    if foreign:
        reasons.append(f"throughput comparisons have cells outside the profile: {foreign}")
    wanted_tests = {(metric, number) for metric in THROUGHPUT_TEST_METRICS for number in range(rounds)}
    for row in rows:
        if not isinstance(row, dict):
            reasons.append("throughput comparison row is not an object")
            continue
        name = row.get("name")
        if row.get("decision") not in THROUGHPUT_DECISIONS:
            reasons.append(f"throughput cell {name!r} has decision {row.get('decision')!r}, not a guarded decision")
        if not isinstance(row.get("medians"), dict):
            reasons.append(f"throughput cell {name!r} has no medians")
        tests = row.get("tests")
        got = [(test.get("metric"), test.get("round")) for test in tests if isinstance(test, dict)] \
            if isinstance(tests, list) else None
        if got is None or len(got) != len(tests) or sorted(got, key=repr) != sorted(wanted_tests, key=repr):
            reasons.append(f"throughput cell {name!r} does not have exactly one test per gate metric and round")
    return reasons


def throughput_digest(summary: object) -> dict:
    """The report's view of a corpus summary: counts and each case's decision."""
    summary = summary if isinstance(summary, dict) else {}
    comparisons = summary.get("comparisons") if isinstance(summary.get("comparisons"), list) else []
    return {"valid": summary.get("valid"), "confirmed_regressions": summary.get("confirmed_regressions"),
            "inconclusive_cases": summary.get("inconclusive_cases"),
            "cases": [{"name": row.get("name"), "decision": row.get("decision")}
                      for row in comparisons if isinstance(row, dict)]}


def classify_scaling(bundles: object, binaries: object) -> list[str]:
    """Reasons the scaling bundles are not a complete SCALING_PROFILE run on the candidate binary.

    bundles maps each series name to {"summary": scaling.json, "metadata": scaling-metadata.json}.
    """
    reasons: list[str] = []
    bundles = bundles if isinstance(bundles, dict) else {}
    binaries = binaries if isinstance(binaries, dict) else {}
    candidate = binaries.get("candidate") if isinstance(binaries.get("candidate"), dict) else {}
    digest = candidate.get("sha256")
    for name in SCALING_PROFILE["series"]:
        bundle = bundles.get(name) if isinstance(bundles.get(name), dict) else {}
        summary = bundle.get("summary") if isinstance(bundle.get("summary"), dict) else {}
        metadata = bundle.get("metadata") if isinstance(bundle.get("metadata"), dict) else {}
        if not summary or not metadata:
            reasons.append(f"scaling series {name} has no scaling.json or scaling-metadata.json")
            continue
        if summary.get("schema") != SCALING_SCHEMA or metadata.get("schema") != SCALING_SCHEMA:
            reasons.append(f"scaling series {name} is not a {SCALING_SCHEMA} bundle")
        if summary.get("status") != "valid":
            reasons.append(f"scaling series {name} is {summary.get('status')!r}: {summary.get('reason')!r}")
        if not (isinstance(digest, str) and SHA256.fullmatch(digest)) or metadata.get("compiler_sha256") != digest:
            reasons.append(f"scaling series {name} compiler is not the measured candidate binary")
        measured = [row for row in summary.get("series", []) if isinstance(row, dict) and isinstance(row.get("points"), list)] \
            if isinstance(summary.get("series"), list) else []
        if summary.get("status") == "valid" and not measured:
            reasons.append(f"scaling series {name} has no measured points")
    return reasons


def scaling_digest(bundles: object) -> dict:
    """The report's view of each scaling series: placement and every measured point."""
    bundles = bundles if isinstance(bundles, dict) else {}
    digest = {}
    for name in SCALING_PROFILE["series"]:
        bundle = bundles.get(name) if isinstance(bundles.get(name), dict) else {}
        summary = bundle.get("summary") if isinstance(bundle.get("summary"), dict) else {}
        metadata = bundle.get("metadata") if isinstance(bundle.get("metadata"), dict) else {}
        series = summary.get("series") if isinstance(summary.get("series"), list) else []
        digest[name] = {
            "status": summary.get("status"), "cpu_set": metadata.get("cpu_set"),
            "excluded_cpus": metadata.get("excluded_cpus"), "physical_cores": metadata.get("physical_cores"),
            "logical_cpus": metadata.get("logical_cpus"),
            "series": [{"name": row.get("name"), "inputs": row.get("inputs"),
                        "points": [{key: point.get(key) for key in (
                            "workers", "placement", "observed_workers", "wall_median", "speedup", "speedup_interval",
                            "efficiency", "cpu_inflation", "rss_inflation")}
                            for point in row.get("points", []) if isinstance(point, dict)]}
                       for row in series if isinstance(row, dict) and isinstance(row.get("points"), list)],
        }
    return digest


# Report rows: summary key, label and the unit uarch_lab.COMPARE_METRICS declares for it.
REPORT_METRICS = (("wall", "wall time (harness span)", "s"), ("task_clock", "task-clock", "s"),
                  ("instructions", "instructions", "count"), ("cycles", "cycles", "count"),
                  ("branch_misses", "branch misses", "count"), ("page_faults", "page faults", "count"),
                  ("peak_rss", "peak RSS", "bytes"))


def number(value: object, form: str) -> str:
    return form % value if isinstance(value, (int, float)) and not isinstance(value, bool) else "NA"


def render_analyzer(receipt: dict, summary: dict, conclusion: str, notes: list[str]) -> str:
    """Report the fixed analyzer evidence without a speedup verdict."""
    identity = receipt.get("identity") if isinstance(receipt.get("identity"), dict) else {}
    host = receipt.get("host") if isinstance(receipt.get("host"), dict) else {}
    inventory = summary.get("inventory") if isinstance(summary.get("inventory"), dict) else {}
    totals = summary.get("totals") if isinstance(summary.get("totals"), dict) else {}
    sampler = summary.get("sampler") if isinstance(summary.get("sampler"), dict) else {}
    clang = summary.get("clang") if isinstance(summary.get("clang"), dict) else {}
    runs = summary.get("runs") if isinstance(summary.get("runs"), list) else []
    preparation = summary.get("preparation") if isinstance(summary.get("preparation"), dict) else {}
    profile = receipt.get("profile") if isinstance(receipt.get("profile"), dict) else ANALYZER_PROFILE
    def counter_seconds(value: object, missing: str = "NA") -> str:
        return f"{number(value / 1e6, '%.3f')} s" if isinstance(value, int) and not isinstance(value, bool) else missing
    def seconds_value(value: object) -> str:
        return number(value / 1e6, "%.3f") if isinstance(value, int) and not isinstance(value, bool) else "NA"
    def integer_value(value: object) -> str:
        return str(value) if isinstance(value, int) and not isinstance(value, bool) else "NA"
    def cpu_seconds(user: object, system: object) -> str:
        if not all(isinstance(value, int) and not isinstance(value, bool) for value in (user, system)):
            return "NA"
        return number((user + system) / 1e6, "%.3f")
    def cost_seconds(record: object, key: str) -> str:
        if not isinstance(record, dict):
            return "NA (missing record)"
        values = record.get("observed_numeric") if isinstance(record.get("observed_numeric"), dict) else {}
        validation = record.get("validation_status", "unvalidated")
        return f"{counter_seconds(values.get(key))} ({validation})"
    def plan_cost_cells(records: object, role: object, set_status: object) -> str:
        rows = records if isinstance(records, list) else []
        if role == "baseline" and not rows:
            return "unavailable in baseline PLAN_V1"
        if not rows:
            return f"missing record ({set_status or 'missing'})"
        prefix = "unexpected baseline PLAN record: " if role == "baseline" else ""
        return prefix + "; ".join(f"{cost_seconds(item, 'planning_us')} / {cost_seconds(item, 'context_proof_us')}"
                                   for item in rows)

    lines = [f"**{check_name('pull')}: {conclusion}** (report-only; validity does not imply a speedup or regression)", "",
             "This is a fixed full-inventory analyzer comparison. It reports process observations and evidence completeness; "
             "it does not calculate a performance verdict.", "",
             f"Profile `{profile.get('name', 'NA')}`: {profile.get('workload', 'NA')}.",
             f"Profile evidence status: `{summary.get('status', 'missing')}`. Observed host: "
             f"`{host.get('cpu_model', 'NA')}`.", "",
             "| Identity | Value |", "| --- | --- |"]
    for key in IDENTITY_KEYS:
        lines.append(f"| {key} | `{identity.get(key, 'NA')}` |")
    lines += ["", "| Work and tool | Value |", "| --- | --- |",
              f"| Selected / excluded rows | {inventory.get('selected_rows', 'NA')} / {inventory.get('excluded_rows', 'NA')} |",
              f"| Baseline executions | {inventory.get('baseline_unique_executions', 'NA')} |",
              f"| Candidate executions / aliases | {inventory.get('candidate_unique_executions', 'NA')} / "
              f"{inventory.get('candidate_alias_rows', 'NA')} |",
              f"| Compile database SHA-256 | `{(summary.get('compile_commands') or {}).get('sha256', 'NA')}` |",
              f"| Clang | `{clang.get('path', 'NA')}`; binary `{clang.get('sha256', 'NA')}` |",
              f"| Clang resource tree | `{clang.get('resource_tree_sha256', 'NA')}`; "
              f"{clang.get('resource_file_count', 'NA')} files, {clang.get('resource_total_bytes', 'NA')} bytes |",
              f"| Native full-run order | `{', '.join(ANALYZER_RUNS)}` |",
              f"| Native helper elapsed | {counter_seconds((summary.get('native_helper') or {}).get('elapsed_us'))} |",
              f"| Whole-tree sampler | `{sampler.get('status', 'incomplete')}`; "
              f"{sampler.get('scope', 'sampled process-tree RSS lower bound')} |",
              "| wait4 RSS scope | Largest individual high-water in each wait4 accounting scope; it does not measure simultaneous tree RSS. |",
              "", "| Run | Driver | Run status | Analysis phase / exit | Aggregate phase / exit | TUs / aliases | Analysis wall | Analysis wait4 CPU | Analysis largest individual RSS | "
              "Independent aggregate wall | Aggregate wait4 CPU | Aggregate largest individual RSS | Sampled tree RSS / live processes |",
              "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in runs:
        sampler_peak = row.get("sampled_peak_tree_rss_bytes")
        live = row.get("peak_live_processes")
        coverage = row.get("result_coverage") if isinstance(row.get("result_coverage"), dict) else {}
        lines.append("| {name} | {role} | {status} | {analysis_state} / {analysis_exit} | "
                     "{aggregate_state} / {aggregate_exit} | {execs} / {aliases} | {analysis} s | {analysis_cpu} s | "
                     "{analysis_rss} B | {aggregate} s | {aggregate_cpu} s | {aggregate_rss} B | {tree} B / {live} |".format(
            name=row.get("name", "NA"), role=row.get("role", "NA"), status=row.get("status", "partial"),
            analysis_state=row.get("analysis_phase_state", "NA"),
            analysis_exit=integer_value(row.get("analysis_exit_status")),
            aggregate_state=row.get("aggregate_phase_state", "NA"),
            aggregate_exit=integer_value(row.get("aggregate_exit_status")),
            execs=integer_value(row.get("executed_tus")), aliases=integer_value(row.get("alias_rows")),
            analysis=seconds_value(row.get("analysis_process_wall_us")),
            aggregate=seconds_value(row.get("independent_aggregate_process_wall_us")),
            analysis_cpu=cpu_seconds(row.get("analysis_wait4_user_cpu_us"), row.get("analysis_wait4_system_cpu_us")),
            analysis_rss=integer_value(row.get("analysis_wait4_largest_individual_rss_bytes")),
            aggregate_cpu=cpu_seconds(row.get("aggregate_wait4_user_cpu_us"), row.get("aggregate_wait4_system_cpu_us")),
            aggregate_rss=integer_value(row.get("aggregate_wait4_largest_individual_rss_bytes")),
            tree=integer_value(sampler_peak), live=integer_value(live)))
    if summary.get("comparison_invalid") is True or summary.get("status") != "complete":
        lines += ["", "**Comparison status: invalid.** Failed or incomplete runs are retained for diagnosis and do not support a speedup or regression conclusion.",
                  "", "| Run | Result rows observed / planned | Passing rows observed | Failed rows observed | Unrecorded rows | In-run aggregate | Independent aggregate |",
                  "| --- | ---: | ---: | ---: | ---: | --- | --- |"]
        for row in runs:
            coverage = row.get("result_coverage") if isinstance(row.get("result_coverage"), dict) else {}
            costs = row.get("internal_costs") if isinstance(row.get("internal_costs"), dict) else {}
            analysis_aggregate = row.get("analysis_aggregate_record")
            independent_aggregate = row.get("independent_aggregate_record")
            analysis_status = analysis_aggregate.get("fields", {}).get("status", "missing") \
                if isinstance(analysis_aggregate, dict) else "missing"
            independent_status = independent_aggregate.get("fields", {}).get("status", "missing") \
                if isinstance(independent_aggregate, dict) else "missing"
            analysis_cost_records = costs.get("analysis_aggregate_records")
            independent_cost_records = costs.get("independent_aggregate_records")
            analysis_validation = analysis_cost_records[0].get("validation_status", "unvalidated") \
                if isinstance(analysis_cost_records, list) and analysis_cost_records else \
                (costs.get("record_sets") or {}).get("analysis_aggregate", "missing")
            independent_validation = independent_cost_records[0].get("validation_status", "unvalidated") \
                if isinstance(independent_cost_records, list) and independent_cost_records else \
                (costs.get("record_sets") or {}).get("independent_aggregate", "missing")
            lines.append("| {name} | {observed} / {planned} | {passing} | {failed} | {missing} | {analysis} / {analysis_validation} | {aggregate} / {aggregate_validation} |".format(
                name=row.get("name", "NA"), observed=integer_value(coverage.get("observed_rows")),
                planned=integer_value(coverage.get("planned_rows")),
                passing=integer_value(coverage.get("passing_rows_observed")),
                failed=integer_value(coverage.get("failed_rows_observed")),
                missing=integer_value(coverage.get("unrecorded_rows")),
                analysis=analysis_status, analysis_validation=analysis_validation,
                aggregate=independent_status, aggregate_validation=independent_validation))
        failed_rows = [(row, label, evidence) for row in summary.get("per_tu", [])
                       if isinstance(row, dict) and isinstance(row.get("run_evidence"), dict)
                       for label, evidence in row["run_evidence"].items()
                       if isinstance(evidence, dict) and evidence.get("state") == "failed"]
        if failed_rows:
            lines += ["", "Failed translation-unit result rows:", "",
                      "| Run | Row | Source | Result status | Duration | Diagnostic log | SHA-256 |",
                      "| --- | ---: | --- | ---: | ---: | --- | --- |"]
            for row, label, evidence in failed_rows:
                lines.append(f"| {label} | {row.get('index', 'NA')} | `{row.get('file', 'NA')}` | "
                             f"{integer_value(evidence.get('status'))} | {counter_seconds(evidence.get('duration_us'))} | "
                             f"`{evidence.get('diagnostic_log_path') or 'NA'}` | "
                             f"`{evidence.get('diagnostics_sha256') or 'NA'}` |")
        lines += ["", "Observed aggregate counters (diagnostic only; failed or mismatched records cannot qualify):", "",
                  "| Run | Capture | Status / result / plan identity | Eligible / selected | Checked | Unique executions | Aliases | Failures | Peak child RSS |",
                  "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
        def aggregate_cell(item: dict, key: str, role: object, version: object) -> str:
            if role == "baseline" and version == "PLAN_V1" and key in ("unique_executions", "aliased_rows"):
                return "N/A (PLAN_V1)"
            values = item.get("observed_numeric") if isinstance(item.get("observed_numeric"), dict) else {}
            if key in values:
                return integer_value(values[key])
            checks = item.get("field_checks") if isinstance(item.get("field_checks"), dict) else {}
            return "NA (invalid)" if checks.get(key, {}).get("state") == "invalid" else "NA (missing)"
        for row in runs:
            costs = row.get("internal_costs") if isinstance(row.get("internal_costs"), dict) else {}
            version = costs.get("format")
            for capture, record_set in (("in-run", "analysis_aggregate_records"),
                                        ("independent", "independent_aggregate_records")):
                records = costs.get(record_set) if isinstance(costs.get(record_set), list) else []
                if not records:
                    records = [{"status": "missing", "result_state": "incomplete",
                                "validation_status": "missing", "plan_identity_status": "unavailable",
                                "observed_numeric": {}, "field_checks": {}}]
                for item in records:
                    result = f"{item.get('status', 'missing')} / {item.get('result_state', 'incomplete')} / " \
                        f"identity {item.get('plan_identity_status', 'unavailable')} / " \
                        f"{item.get('validation_status', 'unvalidated')}"
                    selection_key = "eligible" if version == "PLAN_V1" else "selected_rows"
                    lines.append("| {run} | {capture} | {result} | {selection} | {checked} | "
                                 "{unique} | {aliases} | {failures} | {rss} |".format(
                        run=row.get("name", "NA"), capture=capture, result=result,
                        selection=aggregate_cell(item, selection_key, row.get("role"), version),
                        checked=aggregate_cell(item, "checked", row.get("role"), version),
                        unique=aggregate_cell(item, "unique_executions", row.get("role"), version),
                        aliases=aggregate_cell(item, "aliased_rows", row.get("role"), version),
                        failures=aggregate_cell(item, "failures", row.get("role"), version),
                        rss=aggregate_cell(item, "peak_child_rss_bytes", row.get("role"), version)))
    lines += ["", f"Totals: preflight process wall {counter_seconds(totals.get('preflight_process_wall_us'))}; "
              f"four full-analysis process walls {counter_seconds(totals.get('matched_full_analysis_process_wall_us'))}; "
              f"independent aggregate process walls {counter_seconds(totals.get('independent_aggregate_process_wall_us'))}; "
              f"wait4 CPU {cpu_seconds(totals.get('wait4_user_cpu_us'), totals.get('wait4_system_cpu_us'))} s.",
              "The independent aggregate wall includes a fresh plan and verification. It is not aggregate-only time.", "",
              "| Preflight | Process wall | Internal planning | Context proof |", "| --- | ---: | ---: | ---: |"]
    for role in ("baseline", "candidate"):
        row = preparation.get(role) if isinstance(preparation.get(role), dict) else {}
        missing = "unavailable in baseline PLAN_V1" if role == "baseline" else "missing candidate PLAN_V2 counter"
        lines.append(f"| {role} | {counter_seconds(row.get('process_wall_us'), missing)} | "
                     f"{counter_seconds(row.get('internal_planning_us'), missing)} | "
                     f"{counter_seconds(row.get('context_proof_us'), missing)} |")
    lines += ["", "| Full arm | Driver / cost validation | Run PLAN planning / context proof | Independent aggregate PLAN planning / context proof |",
              "| --- | --- | --- | --- |"]
    for row in runs:
        costs = row.get("internal_costs") if isinstance(row.get("internal_costs"), dict) else {}
        record_sets = costs.get("record_sets") if isinstance(costs.get("record_sets"), dict) else {}
        run = plan_cost_cells(costs.get("run_plan_records"), row.get("role"), record_sets.get("run_plan"))
        aggregate = plan_cost_cells(costs.get("aggregate_plan_records"), row.get("role"),
                                    record_sets.get("aggregate_plan"))
        validation = f"{costs.get('validation_status', 'missing')} / {costs.get('observation_status', 'missing')}"
        lines.append("| {name} | {role} / {validation} | {run} | {aggregate} |".format(
            name=row.get("name", "NA"), role=row.get("role", "NA"),
            validation=validation, run=run, aggregate=aggregate))
    lines += ["", "| Full arm | Shard | Worker PLAN planning / context proof | Worker validation | Shard result / counter validation | "
              "Shard elapsed | Context preflight | Context postflight | Shard peak child RSS |",
              "| --- | ---: | --- | --- | --- | ---: | ---: | ---: | ---: |"]
    for row in runs:
        costs = row.get("internal_costs") if isinstance(row.get("internal_costs"), dict) else {}
        worker_plans = costs.get("worker_plan_records") if isinstance(costs.get("worker_plan_records"), list) else []
        shard_rows = costs.get("shards") if isinstance(costs.get("shards"), list) else []
        shard_count = costs.get("expected_shards") if isinstance(costs.get("expected_shards"), int) else 0
        for shard_id in range(shard_count):
            worker_rows = [item for item in worker_plans if item.get("shard") == shard_id]
            shard_observations = [item for item in shard_rows if item.get("shard") == shard_id]
            if row.get("role") == "baseline" and not worker_rows:
                worker_cost = worker_validation = "unavailable in baseline PLAN_V1"
            elif worker_rows:
                worker_cost = "; ".join(
                    f"{cost_seconds(item, 'planning_us')} / {cost_seconds(item, 'context_proof_us')}"
                    for item in worker_rows)
                worker_validation = ", ".join(item.get("validation_status", "unvalidated") for item in worker_rows)
            else:
                worker_cost = "NA (missing worker PLAN)"
                worker_validation = "missing record"
            if shard_observations:
                shard_status = "; ".join(
                    f"{item.get('status', 'NA')} / {item.get('validation_status', 'unvalidated')}"
                    for item in shard_observations)
                elapsed = "; ".join(cost_seconds(item, "elapsed_us") for item in shard_observations)
                if costs.get("format") == "PLAN_V1":
                    preflight = postflight = "unavailable in baseline PLAN_V1"
                else:
                    preflight = "; ".join(cost_seconds(item, "context_preflight_us") for item in shard_observations)
                    postflight = "; ".join(cost_seconds(item, "context_postflight_us") for item in shard_observations)
                rss = "; ".join(integer_value(item.get("observed_numeric", {}).get("peak_child_rss_bytes"))
                                 + f" B ({item.get('validation_status', 'unvalidated')})" for item in shard_observations)
            else:
                shard_status = "missing ANALYZE_SHARD / missing record"
                elapsed = preflight = postflight = rss = "NA (missing record)"
            lines.append(f"| {row.get('name', 'NA')} | {shard_id} | {worker_cost} | {worker_validation} | "
                         f"{shard_status} | {elapsed} | {preflight} | {postflight} | {rss} |")
    lines += ["", "Worker PLAN and shard rows are per-shard observations. Shards can run concurrently; these values are "
              "not summed into serial wall time or a critical-path duration. Shard elapsed begins after context preflight "
              "and includes execution plus context postflight; preflight is outside elapsed and postflight is already "
              "inside it, so neither should be added again to derive wall time. Baseline PLAN_V1 has no run/worker/aggregate "
              "planning or context-proof counters, so those cells are unavailable rather than zero. Full-arm cost validation "
              "stays failed unless every plan, shard, and aggregate check passes; individually plan-bound cost observations "
              "remain visible with their validation state."]
    lines += ["", f"Per-TU timing and diagnostic evidence covers {len(summary.get('per_tu', []))} rows and is retained in the artifact. "
              "Full-run process wall includes setup/planning performed by the driver; baseline internal planning counters may be "
              "unavailable for legacy V1. Whole-tree RSS remains sampled even when the completeness status is complete; "
              "wait4 RSS is the largest individual high-water, not a concurrent sum. Invalid comparisons suppress per-TU median "
              "summaries; the independent per-run source durations remain available for diagnosis.", ""]
    reasons = receipt.get("reasons") if isinstance(receipt.get("reasons"), list) else []
    lines += [f"- {item}" for item in (*notes, *reasons)]
    return "\n".join(lines)


def render(receipt: dict, summary: object, conclusion: str, notes: list[str]) -> str:
    """Readable per-candidate report: identities, verdict, core metrics and timings."""
    if isinstance(receipt, dict) and receipt.get("profile") == ANALYZER_PROFILE:
        return render_analyzer(receipt, summary if isinstance(summary, dict) else {}, conclusion, notes)
    identity = receipt.get("identity", {}) if isinstance(receipt, dict) else {}
    timings = receipt.get("timings", {}) if isinstance(receipt, dict) else {}
    summary = summary if isinstance(summary, dict) else {}
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) else {}
    mode = receipt.get("mode") if isinstance(receipt, dict) and receipt.get("mode") in MODES else "main"
    lines = [
        f"**{check_name(mode)}: {conclusion}** (performance policy: report-only; a slow result does not block)",
        "",
        verdict.get("text") or "No wall-time verdict.",
        "",
        "| Identity | Value |",
        "| --- | --- |",
    ]
    for key in IDENTITY_KEYS:
        lines.append(f"| {key} | `{identity.get(key, 'NA')}` |")
    coverage = receipt.get("coverage") if isinstance(receipt, dict) else None
    label = range_label(coverage.get("range"), coverage.get("first_parent")) if isinstance(coverage, dict) else ""
    if mode == "main" and label:
        lines += ["", f"Baseline: {label}."]
    profile = receipt.get("profile", {}) if isinstance(receipt, dict) else {}
    host = receipt.get("host", {}) if isinstance(receipt, dict) else {}
    lines += ["", f"Profile `{profile.get('name', 'NA')}`: {profile.get('workload', 'NA')}.",
              f"Observed host: `{host.get('cpu_model', 'NA') if isinstance(host, dict) else 'NA'}`.", ""]
    metrics = summary.get("metrics") if isinstance(summary.get("metrics"), dict) else {}
    if metrics:
        lines += ["A = baseline, B = candidate. B/A is the candidate-to-baseline ratio (below 1 means the candidate is lower; "
                  "lower is better for every metric below). The 95% interval is a confidence interval of the dimensionless "
                  "B/A ratio, not of the medians. Medians are in the unit column (exact base units); NA means not measured.", "",
                  "| Metric | Unit | A (baseline) median | B (candidate) median | B/A ratio | 95% CI of B/A | Outcome |",
                  "| --- | --- | --- | --- | --- | --- | --- |"]
        for key, label, unit in REPORT_METRICS:
            row = metrics.get(key) if isinstance(metrics.get(key), dict) else {}
            if isinstance(row.get("unit"), str) and row["unit"] != unit:
                row = {"outcome": f"rejected: unit {row['unit']!r}, expected {unit!r}"}
            lines.append("| %s | %s | %s | %s | %s | [%s, %s] | %s |" % (
                label, unit, number(row.get("a_median"), "%.6g"), number(row.get("b_median"), "%.6g"),
                number(row.get("ratio"), "%.4f"), number(row.get("ci_low"), "%.4f"),
                number(row.get("ci_high"), "%.4f"), row.get("outcome", "NA")))
        lines.append("")
    timings = timings if isinstance(timings, dict) else {}
    builds = timings.get("build_seconds") if isinstance(timings.get("build_seconds"), dict) else {}
    scaled = ", scaling %s s" % number(timings.get("scaling_seconds"), "%.0f") if "scaling_seconds" in timings else ""
    lines.append("Host time: builds %s s, measurement %s s, corpus %s s%s, total %s s; queue delay before the host "
                 "job %s s." % (
        " + ".join(number(builds.get(key), "%.0f") for key in (("baseline", "candidate") if receipt.get("closure") else ("baseline", "candidate", "closure"))),
        number(timings.get("measurement_seconds"), "%.0f"), number(timings.get("throughput_seconds"), "%.0f"),
        scaled, number(timings.get("total_seconds"), "%.0f"),
        number(timings.get("queue_delay_seconds"), "%.0f")))
    if receipt.get("closure"):
        lines.append("Frozen baseline closure: snapshot-v1, same matched root, no fallback; snapshot %s s "
                     "(includes harness preparation %s s), restore/validation %s s, checkout %s s." % (
            number(timings.get("closure_snapshot_seconds"), "%.3f"),
            number(timings.get("harness_preparation_seconds"), "%.3f"),
            number(timings.get("closure_restore_seconds"), "%.3f"),
            number(timings.get("closure_checkout_seconds"), "%.3f")))
    corpus = receipt.get("throughput") if isinstance(receipt, dict) else None
    if isinstance(corpus, dict):
        cases = corpus.get("cases") if isinstance(corpus.get("cases"), list) else []
        flagged = [f"{row.get('name')}: {row.get('decision')}" for row in cases if isinstance(row, dict)
                   and row.get("decision") != "no substantial regression detected"]
        lines += ["", f"Throughput corpus `{THROUGHPUT_PROFILE['name']}`: {len(cases)} cases, "
                  f"{corpus.get('confirmed_regressions', 'NA')} confirmed regressions, "
                  f"{corpus.get('inconclusive_cases', 'NA')} inconclusive (report-only)."
                  + (" " + "; ".join(flagged) + "." if flagged else "")]
    scaling = receipt.get("scaling") if isinstance(receipt, dict) else None
    if isinstance(scaling, dict):
        lines += ["", f"Multi-TU scaling `{SCALING_PROFILE['name']}` (report-only; speedup against the one-worker "
                  "reference of the same inputs, with conservative 95% bounds; CPU and RSS are inflation over "
                  "that reference):"]
        for name in SCALING_PROFILE["series"]:
            row = scaling.get(name) if isinstance(scaling.get(name), dict) else {}
            lines += ["", f"Series `{name}`: {row.get('status', 'NA')} on CPU set `{row.get('cpu_set', 'NA')}` "
                      f"({row.get('physical_cores', 'NA')} cores, {row.get('logical_cpus', 'NA')} logical CPUs"
                      + (f"; housekeeping `{row.get('excluded_cpus')}` excluded" if row.get("excluded_cpus") else "")
                      + ").", "", "| Inputs | Workers | Placement | Observed | Wall s | Speedup | 95% bounds | "
                      "Efficiency | CPU | RSS |", "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
            for shape in row.get("series", []) if isinstance(row.get("series"), list) else []:
                for point in shape.get("points", []) if isinstance(shape, dict) else []:
                    if not isinstance(point, dict) or not point.get("workers"):
                        continue
                    bounds = point.get("speedup_interval") if isinstance(point.get("speedup_interval"), list) else []
                    lines.append("| %s (%s) | %s | %s | %s | %s | %s | [%s, %s] | %s | %s | %s |" % (
                        shape.get("name"), shape.get("inputs"), point.get("workers"), point.get("placement"),
                        point.get("observed_workers"), number(point.get("wall_median"), "%.4f"),
                        number(point.get("speedup"), "%.3f"), number(bounds[0] if len(bounds) == 2 else None, "%.3f"),
                        number(bounds[1] if len(bounds) == 2 else None, "%.3f"), number(point.get("efficiency"), "%.3f"),
                        number(point.get("cpu_inflation"), "%.3f"), number(point.get("rss_inflation"), "%.3f")))
    inline = receipt.get("inline_acceptance") if isinstance(receipt, dict) else None
    if isinstance(inline, dict) and inline.get("requested"):
        inline_summary = inline.get("summary") if isinstance(inline.get("summary"), dict) else {}
        lines += ["", f"Issue #48 same-candidate inliner acceptance: {inline.get('status', 'NA')} "
                  f"({INLINE_ACCEPTANCE_PROFILE['pairs']} ABBA pairs per stage; report-only).",
                  "A = feature omitted; B passes -fcanonical-inline. Ratios below 1 mean B is lower. "
                  "Counters may be NA when the host cannot schedule them.", "",
                  "| Stage | Wall B/A | Retired instructions B/A | Peak RSS B/A | Executable code bytes A/B | Fixed point |",
                  "| --- | --- | --- | --- | --- | --- |"]
        for key, label, fixed_key in (("stage1", "Stage-1 compiler construction", "off"),
                                      ("selfhost_runtime", "Generated compiler self-host runtime", "on")):
            stage = inline_summary.get(key) if isinstance(inline_summary.get(key), dict) else {}
            values = stage.get("metrics") if isinstance(stage.get("metrics"), dict) else {}
            code = values.get("code_bytes") if isinstance(values.get("code_bytes"), dict) else {}
            lines.append("| %s | %s | %s | %s | %s / %s (%s) | %s |" % (
                label,
                number((values.get("wall") or {}).get("ratio"), "%.4f"),
                number((values.get("instructions") or {}).get("ratio"), "%.4f"),
                number((values.get("peak_rss") or {}).get("ratio"), "%.4f"),
                number(code.get("a_value"), "%.0f"), number(code.get("b_value"), "%.0f"),
                number(code.get("ratio"), "%.4f"),
                inline_summary.get("fixed_point", {}).get(fixed_key, "NA")))
        if inline.get("exit") not in (None, 0):
            lines.append(f"Issue #48 profile exit: {inline.get('exit')}.")
    reasons = receipt.get("reasons") if isinstance(receipt, dict) else None
    warnings = summary.get("warnings")
    for item in (*notes, *(reasons if isinstance(reasons, list) else ()),
                 *(warnings if isinstance(warnings, list) else ())):
        lines.append(f"- {item}")
    return "\n".join(lines)


def dumps(value: object) -> str:
    return json.dumps(value, sort_keys=True, indent=2)
