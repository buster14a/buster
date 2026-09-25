#!/usr/bin/env python3
"""Recompute #881 campaign capacity from the pinned current source tree.

This is arithmetic only. The checked-in support declaration determines the
raw census dimensions, but the blocked service profile has no authenticated
#508 rows/inputs pins or validator-derived eligibility/oracle records. The
report therefore gives a source-derived envelope, never an admitted campaign
size or host-rate claim.
"""

import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def source_define(path, name):
    text = path.read_text(encoding="utf-8")
    matches = re.findall(
        rf"^#define\s+{re.escape(name)}\s+([0-9]+)(?:u|U|ull|ULL)?\s*$",
        text, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f"expected one integer source definition for {name} in {path}")
    return int(matches[0])


def source_product(path, name):
    text = path.read_text(encoding="utf-8")
    matches = re.findall(rf"^#define\s+{re.escape(name)}\s+(.+?)\s*$", text, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f"expected one source expression for {name} in {path}")
    expression = matches[0]
    constant = re.fullmatch(r"UINT64_C\(([0-9]+)\)", expression.strip())
    if constant:
        return int(constant.group(1))
    expression = expression.strip()
    if expression.startswith("(") and expression.endswith(")"):
        expression = expression[1:-1].strip()
    factors = [part.strip() for part in expression.split("*")]
    result = 1
    for factor in factors:
        match = re.fullmatch(r"([0-9]+)(?:u|U|ull|ULL)?", factor)
        if not match:
            raise ValueError(f"unsupported integer expression for {name}: {expression}")
        result *= int(match.group(1))
    return result


def ceil_div(value, divisor):
    if value < 0 or divisor <= 0:
        raise ValueError("capacity division requires a nonnegative value and positive divisor")
    return value // divisor + bool(value % divisor)


def checked_product(*values):
    result = 1
    for value in values:
        if value < 0:
            raise ValueError("capacity inputs must be nonnegative")
        result *= value
        if result > (1 << 64) - 1:
            raise OverflowError("capacity product exceeds uint64")
    return result


def read_tsv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        rows = list(reader)
    if not rows:
        raise ValueError(f"empty support declaration: {path}")
    return rows


def parse_kv(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if not sep or not key or key in result:
            raise ValueError(f"malformed or duplicate profile entry in {path}: {line}")
        result[key] = value
    return result


def git_value(root, *args):
    result = subprocess.run(["git", "-C", str(root), *args], check=False,
                            capture_output=True, text=True)
    if result.returncode:
        raise ValueError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def verify_committed_inputs(root, relative_paths):
    """Require every source byte used by this report to be tracked and at HEAD."""
    verified = {}
    for relative in sorted(set(relative_paths)):
        path = root / relative
        if not path.is_file():
            raise ValueError(f"required report input is missing: {relative}")
        tracked = subprocess.run(
            ["git", "-C", str(root), "ls-files", "--error-unmatch", "--", relative],
            check=False, capture_output=True, text=True)
        if tracked.returncode or tracked.stdout.strip() != relative:
            raise ValueError(f"report input is untracked at HEAD: {relative}")
        committed = subprocess.run(
            ["git", "-C", str(root), "show", f"HEAD:{relative}"],
            check=False, capture_output=True)
        if committed.returncode:
            raise ValueError(f"report input is absent from HEAD: {relative}")
        working_bytes = path.read_bytes()
        if committed.stdout != working_bytes:
            raise ValueError(f"report input differs from HEAD: {relative}")
        verified[relative] = hashlib.sha256(working_bytes).hexdigest()
    return verified


def campaign_model(eligible_rows, runtime_rows, pairs, source):
    if eligible_rows <= 0 or runtime_rows < 0 or runtime_rows > eligible_rows:
        raise ValueError("runtime rows must be within a positive compiler-eligible population")
    if pairs < source["minimum_pairs"] or pairs > source["maximum_pairs"] or pairs % 2:
        raise ValueError("pairs must follow the source-locked even sampling range")
    rounds = source["rounds"]
    stages = source["campaign_stages"]
    per_row = checked_product(2, source["warmups"] + checked_product(rounds, pairs))
    compiler_per_stage = checked_product(eligible_rows, per_row)
    runtime_per_stage = checked_product(runtime_rows, per_row)
    invocations_per_stage = compiler_per_stage + runtime_per_stage
    if invocations_per_stage > (1 << 64) - 1:
        raise OverflowError("per-stage invocation count exceeds uint64")
    samples_per_stage = checked_product(eligible_rows, rounds, pairs)
    if samples_per_stage > source["samples_per_stage_cap"]:
        raise ValueError("per-stage paired samples exceed the checked-in collector ceiling")
    transcript_shards_per_stage = ceil_div(invocations_per_stage,
                                           source["transcript_records_per_shard"])
    sample_shards_per_stage = ceil_div(samples_per_stage,
                                       source["transcript_records_per_shard"])
    if transcript_shards_per_stage > source["transcript_shard_cap"]:
        raise ValueError("transcript shard count exceeds the checked-in collector ceiling")
    if sample_shards_per_stage > source["transcript_shard_cap"]:
        raise ValueError("sample shard count exceeds the checked-in collector ceiling")

    total_invocations = checked_product(invocations_per_stage, stages)
    total_samples = checked_product(samples_per_stage, stages)
    spool_bytes_per_stage = checked_product(samples_per_stage, source["spool_record_bytes"])
    total_spool_bytes = checked_product(spool_bytes_per_stage, stages)
    transcript_bytes_per_stage_upper = checked_product(
        invocations_per_stage, source["transcript_line_cap"])
    total_transcript_bytes_upper = checked_product(transcript_bytes_per_stage_upper, stages)
    typed_records_per_stage_model = checked_product(samples_per_stage,
                                                     source["typed_record_bytes_model"])
    typed_records_total_model = checked_product(typed_records_per_stage_model, stages)
    sample_jsonl_bytes_per_stage_upper = checked_product(
        samples_per_stage, source["sample_line_cap"])
    sample_jsonl_bytes_both_stages_upper = checked_product(
        sample_jsonl_bytes_per_stage_upper, stages)
    sample_jsonl_full_shard_upper = checked_product(
        source["transcript_records_per_shard"], source["sample_line_cap"])
    deadline_after_cleanup = source["worker_budget_seconds"] - source["cleanup_budget_seconds"]
    if deadline_after_cleanup <= 0:
        raise ValueError("cleanup reserve consumes the complete worker budget")

    return {
        "assumptions": {
            "compiler_eligible_rows": eligible_rows,
            "runtime_eligible_rows": runtime_rows,
            "pairs_per_round": pairs,
            "runtime_rows_are": "scenario only; not authenticated current eligibility",
            "all_sample_metrics_in_one_row_record": [
                "compiler_wall_time", "compiler_peak_rss", "generated_code_bytes",
                "generated_runtime"],
            "warmups_per_variant_included_in_invocation_count": source["warmups"],
            "campaign_stages": ["A/A", "A/B"],
        },
        "calls": {
            "compiler_invocations_per_stage": compiler_per_stage,
            "runtime_invocations_per_stage": runtime_per_stage,
            "all_invocations_per_stage": invocations_per_stage,
            "all_invocations_both_stages": total_invocations,
        },
        "samples": {
            "paired_numeric_records_per_stage": samples_per_stage,
            "paired_numeric_records_both_stages": total_samples,
            "shards_per_stage_by_record_cap": sample_shards_per_stage,
            "shards_both_stages_by_record_cap": checked_product(sample_shards_per_stage, stages),
            "collector_binary_spool_bytes_per_stage": spool_bytes_per_stage,
            "collector_binary_spool_bytes_both_stages": total_spool_bytes,
            "jsonl_line_cap_bytes": source["sample_line_cap"],
            "jsonl_export_bytes_per_stage_if_every_line_hits_cap": sample_jsonl_bytes_per_stage_upper,
            "jsonl_export_bytes_both_stages_if_every_line_hits_cap": sample_jsonl_bytes_both_stages_upper,
            "jsonl_bytes_per_full_shard_if_every_line_hits_cap": sample_jsonl_full_shard_upper,
            "jsonl_full_shard_fits_source_byte_cap":
                sample_jsonl_full_shard_upper <= source["transcript_bytes_per_shard"],
            "jsonl_line_cap_is_source_ceiling_not_observed_size": True,
            "typed_result_record_bytes_at_180_each_per_stage_model": typed_records_per_stage_model,
            "typed_result_record_bytes_at_180_each_both_stages_model": typed_records_total_model,
            "typed_result_record_size_is_a_model": True,
        },
        "transcripts": {
            "shards_per_stage_by_record_cap": transcript_shards_per_stage,
            "shards_both_stages_by_record_cap": checked_product(transcript_shards_per_stage, stages),
            "record_cap_per_shard": source["transcript_records_per_shard"],
            "byte_cap_per_shard": source["transcript_bytes_per_shard"],
            "max_line_bytes": source["transcript_line_cap"],
            "bytes_per_full_shard_if_every_line_hits_max": checked_product(
                source["transcript_records_per_shard"], source["transcript_line_cap"]),
            "average_line_budget_at_full_record_cap_bytes":
                source["transcript_bytes_per_shard"] // source["transcript_records_per_shard"],
            "actual_serialized_shard_sizes_available": False,
            "per_shard_byte_cap_proven": False,
            "bytes_per_stage_if_every_line_hits_max": transcript_bytes_per_stage_upper,
            "bytes_both_stages_if_every_line_hits_max": total_transcript_bytes_upper,
            "actual_transcript_and_bundle_fit_proven": False,
            "upper_bound_is_not_observed_size": True,
        },
        "deadline": {
            "worker_budget_seconds": source["worker_budget_seconds"],
            "bounded_cleanup_reserve_seconds": source["cleanup_budget_seconds"],
            "remaining_for_all_work_seconds": deadline_after_cleanup,
            "mean_invocation_budget_ns_if_all_other_work_costs_zero":
                deadline_after_cleanup * 1_000_000_000 // total_invocations,
            "materialization_build_correctness_quiet_phase_sealing_and_service_costs_seconds": None,
            "whole_job_fit_proven": False,
        },
        "storage": {
            "indexed_payload_ceiling_bytes": source["bundle_payload_cap_bytes"],
            "six_copy_ceiling_reservation_bytes": checked_product(
                source["bundle_payload_cap_bytes"], source["sealed_copy_count"]),
            "copies_at_ceiling": source["sealed_copy_count"],
            "six_copy_value_excludes_archive_headers_and_external_publication": True,
        },
    }


def build_report(root):
    root = root.resolve()
    source_paths = [
        "docs/native-retirement-support-v1.tsv",
        "tools/bench_service/profiles/native-retirement-performance-v1.blocked",
        "tools/bench_service/EXPORT.md",
        "tools/bench_service/README.md",
        "tools/bench_service/systemd_broker.c",
        "tools/bench_service/worker_linux.h",
        "tools/native_retirement_performance_binding.py",
        "tools/native_retirement_performance_schema.py",
        "tools/native_retirement_result_input.py",
        "tools/throughput/retirement_campaign.h",
        "tools/throughput/retirement_capacity.py",
        "tools/throughput/retirement_execution.h",
        "tools/throughput/retirement_samples.h",
        "tools/throughput/retirement_stats.h",
    ]
    input_digests = verify_committed_inputs(root, source_paths)
    sys.path.insert(0, str(root / "tools"))
    import native_retirement_performance_binding as binding

    support_path = root / binding.SUPPORT_DECLARATION_PATH
    support_bytes = support_path.read_bytes()
    support_sha = hashlib.sha256(support_bytes).hexdigest()
    profile_path = root / "tools/bench_service/profiles/native-retirement-performance-v1.blocked"
    profile = parse_kv(profile_path)
    if profile.get("support-declaration") != binding.SUPPORT_DECLARATION_PATH:
        raise ValueError("blocked profile does not name the checked-in support declaration")
    if profile.get("support-declaration-sha256") != support_sha:
        raise ValueError("support declaration digest differs from the blocked profile pin")

    support_rows = read_tsv(support_path)
    input_count, subjects, groups, object_rows = binding._approved_support_counts()
    stage_rows = len(binding.STAGES) - 1
    obligation_counts = {}
    subject_count = 0
    supported_subjects = 0
    controls = 0
    for row in support_rows:
        role = row["role"]
        obligation = row["compile_obligation"]
        key = f"{role}:{obligation}"
        obligation_counts[key] = obligation_counts.get(key, 0) + 1
        if role == "subject":
            subject_count += 1
            if obligation == "supported-object-zero-fallback":
                supported_subjects += 1
            elif obligation == "registered-non-object-control":
                controls += 1
    expected_controls = controls * len(binding.TARGETS) * len(binding.FRONTENDS) * len(binding.PIC) * len(binding.ALLOCATORS)
    supported_rows = supported_subjects * len(binding.TARGETS) * len(binding.FRONTENDS) * len(binding.PIC) * len(binding.ALLOCATORS)
    derived_object_rows = object_rows
    if (input_count != len(support_rows) or subject_count != subjects or
            supported_rows + expected_controls != derived_object_rows):
        raise ValueError("support-source row derivation disagrees with binding dimensions")

    exec_path = root / "tools/throughput/retirement_execution.h"
    samples_path = root / "tools/throughput/retirement_samples.h"
    stats_path = root / "tools/throughput/retirement_stats.h"
    worker_path = root / "tools/bench_service/worker_linux.h"
    broker_path = root / "tools/bench_service/systemd_broker.c"
    export_doc = (root / "tools/bench_service/EXPORT.md").read_text(encoding="utf-8")
    copy_match = re.search(r"At maximum capacity, reserve ([a-z]+) independent copies", export_doc)
    copy_counts = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6}
    if not copy_match or copy_match.group(1) not in copy_counts:
        raise ValueError("could not derive the sealed-copy count from EXPORT.md")
    runtime_match = re.search(r"RuntimeMaxSec=([0-9]+)us", broker_path.read_text(encoding="utf-8"))
    if not runtime_match or int(runtime_match.group(1)) % 1_000_000:
        raise ValueError("could not derive whole-second worker runtime cap from broker source")
    cleanup_text = (root / "tools/bench_service/README.md").read_text(encoding="utf-8")
    cleanup_match = re.search(
        r"configured ([0-9]+)-second grace, then sends KILL and polls for at\s+most another ([0-9]+) seconds",
        cleanup_text)
    if not cleanup_match:
        raise ValueError("could not derive bounded worker cleanup allowance from service contract")

    source = {
        "rounds": source_define(stats_path, "TP_RETIREMENT_ROUNDS"),
        "minimum_pairs": source_define(stats_path, "TP_RETIREMENT_MIN_PAIRS_PER_ROUND"),
        "maximum_pairs": source_define(exec_path, "TP_RETIREMENT_EXECUTION_MAX_PAIRS"),
        "warmups": source_define(exec_path, "TP_RETIREMENT_WARMUPS"),
        "campaign_stages": 2,
        "samples_per_stage_cap": source_product(samples_path, "TP_RETIREMENT_SAMPLE_TOTAL_RECORDS"),
        "spool_record_bytes": source_define(samples_path, "TP_RETIREMENT_SAMPLE_RECORD_BYTES"),
        "transcript_records_per_shard": source_define(exec_path, "TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS"),
        "transcript_bytes_per_shard": source_product(exec_path, "TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES"),
        "transcript_shard_cap": source_define(exec_path, "TP_RETIREMENT_TRANSCRIPT_SHARDS"),
        "transcript_line_cap": source_define(exec_path, "TP_RETIREMENT_EXECUTION_LINE_CAP"),
        "sample_line_cap": source_define(samples_path, "TP_RETIREMENT_SAMPLE_LINE_CAP"),
        "typed_record_bytes_model": 180,
        "worker_budget_seconds": int(runtime_match.group(1)) // 1_000_000,
        "cleanup_budget_seconds": sum(int(value) for value in cleanup_match.groups()),
        "bundle_payload_cap_bytes": source_product(worker_path, "BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP"),
        "sealed_copy_count": copy_counts[copy_match.group(1)],
    }
    if source["campaign_stages"] != 2 or source["minimum_pairs"] % 2:
        raise ValueError("unexpected fixed campaign structure in source")

    full_rows_min = object_rows + stage_rows
    max_eligible = supported_rows + stage_rows
    if max_eligible > binding.SUPPORT_MIN_STAGE_ROW_COUNT or full_rows_min != binding.SUPPORT_MIN_STAGE_ROW_COUNT:
        raise ValueError("stage-row derivation disagrees with the performance binding")
    census_pins_present = "census-inputs-sha256" in profile and "census-rows-sha256" in profile
    report = {
        "schema": "buster-native-retirement-capacity-model-v1",
        "source": {
            "commit": git_value(root, "rev-parse", "HEAD"),
            "tree": git_value(root, "rev-parse", "HEAD^{tree}"),
            "all_report_inputs_match_head": True,
            "report_input_sha256": input_digests,
            "support_declaration": binding.SUPPORT_DECLARATION_PATH,
            "support_declaration_sha256": support_sha,
            "support_pin_matches_blocked_profile": True,
            "profile": str(profile_path.relative_to(root)),
            "profile_status": profile.get("status"),
            "campaign_code": [
                "tools/throughput/retirement_campaign.h",
                "tools/throughput/retirement_execution.h",
                "tools/throughput/retirement_samples.h",
                "tools/throughput/retirement_stats.h"],
            "worker_budget_source": "tools/bench_service/systemd_broker.c RuntimeMaxSec",
            "cleanup_source": "tools/bench_service/README.md",
            "typed_result_record_model_source": "tools/bench_service/EXPORT.md (180 bytes per record floor)",
        },
        "current_population": {
            "support_input_count": input_count,
            "subject_count": subjects,
            "supported_object_subjects": supported_subjects,
            "registered_non_object_control_subjects": controls,
            "support_role_obligation_counts": obligation_counts,
            "object_rows_including_controls": object_rows,
            "supported_object_rows": supported_rows,
            "registered_control_rows": expected_controls,
            "minimum_extra_link_and_self_host_rows": stage_rows,
            "minimum_canonical_rows_from_source": full_rows_min,
            "maximum_compiler_eligible_rows_for_envelope": max_eligible,
            "actual_validator_derived_eligible_rows": None,
            "actual_runtime_eligible_rows": None,
            "actual_eligibility_available": False,
            "eligibility_blocker": (
                "blocked profile lacks independently authenticated census-inputs-sha256 and "
                "census-rows-sha256; current validator-derived eligibility, per-row configuration "
                "identity, and independent oracles are absent" if not census_pins_present else
                "current execution validator/oracle receipts are not present in this source tree"),
            "production_ab_authorized": False,
            "ab_authorization_blocker": "no reviewed current #426 A/A admission receipt",
        },
        "policy_limits": source,
        "models": {
            "rows": "maximum compiler-eligible envelope from supported-object rows plus the two required extra stages; not actual eligibility",
            "pairs": "minimum approved pair count only; current #426 may require more before A/B",
            "runtime_zero": campaign_model(max_eligible, 0, source["minimum_pairs"], source),
            "runtime_all_eligible": campaign_model(max_eligible, max_eligible,
                                                    source["minimum_pairs"], source),
        },
        "evidence": {
            "capacity_is_arithmetic_only": True,
            "hosted_per_invocation_rate_available": False,
            "full_job_phase_duration_bounds_available": False,
            "full_one_hour_feasibility_established": False,
            "fixture_or_model_is_acceptance": False,
        },
    }
    return report


def render_text(report):
    population = report["current_population"]
    models = report["models"]
    lines = [
        f"CAPACITY_MODEL commit={report['source']['commit']} tree={report['source']['tree']}",
        f"report_inputs=tracked_byte_identical_to_HEAD count={len(report['source']['report_input_sha256'])}",
        f"support_sha256={report['source']['support_declaration_sha256']}",
        f"population objects={population['object_rows_including_controls']} "
        f"supported={population['supported_object_rows']} controls={population['registered_control_rows']} "
        f"extra_stage_rows={population['minimum_extra_link_and_self_host_rows']} "
        f"canonical_min={population['minimum_canonical_rows_from_source']}",
        "eligibility=UNAVAILABLE actual_compiler_rows=unknown actual_runtime_rows=unknown "
        "production_AB=BLOCKED #426_receipt=absent",
    ]
    for key, label in (("runtime_zero", "runtime_rows=0"),
                       ("runtime_all_eligible", "runtime_rows=eligible_upper_bound")):
        model = models[key]
        lines.append(
            f"{label} eligible_rows={model['assumptions']['compiler_eligible_rows']} "
            f"pairs={model['assumptions']['pairs_per_round']} "
            f"compiler_calls={model['calls']['compiler_invocations_per_stage'] * 2} "
            f"runtime_calls={model['calls']['runtime_invocations_per_stage'] * 2} "
            f"total_calls={model['calls']['all_invocations_both_stages']} "
            f"samples_both_stages={model['samples']['paired_numeric_records_both_stages']} "
            f"spool_bytes_both_stages={model['samples']['collector_binary_spool_bytes_both_stages']} "
            f"sample_jsonl_cap_both_stages={model['samples']['jsonl_export_bytes_both_stages_if_every_line_hits_cap']} "
            f"typed_180B_model_both_stages={model['samples']['typed_result_record_bytes_at_180_each_both_stages_model']} "
            f"transcript_8KiB_upper_bound={model['transcripts']['bytes_both_stages_if_every_line_hits_max']} "
            f"one_hour_mean_ns_if_every_non_invocation_cost_is_zero="
            f"{model['deadline']['mean_invocation_budget_ns_if_all_other_work_costs_zero']}"
        )
    samples = models["runtime_all_eligible"]["samples"]
    lines.append(
        f"sample_jsonl_line_cap={samples['jsonl_line_cap_bytes']} "
        f"shards_both_stages_by_record_cap={samples['shards_both_stages_by_record_cap']} "
        f"full_shard_upper={samples['jsonl_bytes_per_full_shard_if_every_line_hits_cap']} "
        f"transcript_shard_byte_cap={models['runtime_all_eligible']['transcripts']['byte_cap_per_shard']} "
        f"full_sample_shard_fits_byte_cap={str(samples['jsonl_full_shard_fits_source_byte_cap']).lower()} "
        "line_sizes=source_ceiling_not_observed spool=72B_per_numeric_record typed_record=180B_model_only"
    )
    transcript = models["runtime_all_eligible"]["transcripts"]
    lines.append(
        f"transcript_shard_record_cap={transcript['record_cap_per_shard']} "
        f"byte_cap={transcript['byte_cap_per_shard']} "
        f"line_cap={transcript['max_line_bytes']} "
        f"all_max_lines_per_full_shard={transcript['bytes_per_full_shard_if_every_line_hits_max']} "
        f"average_line_budget_at_full_record_cap={transcript['average_line_budget_at_full_record_cap_bytes']} "
        "actual_shard_sizes=unavailable per_shard_fit=UNPROVEN total_128GiB_bundle_fit=UNPROVEN"
    )
    lines.extend([
        f"service_budget_seconds={report['policy_limits']['worker_budget_seconds']} "
        f"cleanup_reserve_seconds={report['policy_limits']['cleanup_budget_seconds']} "
        f"sealed_payload_ceiling_bytes={report['policy_limits']['bundle_payload_cap_bytes']} "
        f"six_copy_ceiling_reservation_bytes={models['runtime_all_eligible']['storage']['six_copy_ceiling_reservation_bytes']}",
        "CONCLUSION modeled-only; build/correctness/quiet-phase/sealing/cleanup service costs lack current bounds; "
        "no per-shard transcript-size proof, full one-hour fit, or production A/B authorization is established.",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2],
                        help="immutable checkout root (default: this repository)")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()
    try:
        report = build_report(args.repo)
    except (OSError, ValueError, OverflowError, KeyError) as error:
        parser.error(str(error))
    if args.format == "json":
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(render_text(report))


if __name__ == "__main__":
    main()
