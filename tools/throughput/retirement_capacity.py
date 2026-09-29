#!/usr/bin/env python3
"""Recompute the A1 #881 campaign capacity from the pinned current source tree.

This is arithmetic only. Amendment A1 times the native-host batch groups: one
fresh compiler process per (configuration, recipe) object group compiles every
fixture of the group, and each link or self-host stage row is a singleton
group. The checked-in support declaration and the fixture recipe table fix the
group shapes; the blocked service profile has no authenticated #508 rows or
validator-derived eligibility, so group sizes are a source-derived envelope
(every supported subject timed, every registered control appended), never an
admitted campaign size or a host-rate claim.

The model mirrors tools/throughput/retirement_campaign.h:
``(G + U) * 2 * (warmups + rounds * pairs)`` invocations per stage, the
``row-round-pair`` and ``group-round-pair`` result populations, per-batch
metrics artifacts packed into metrics shards (at most ``2 * ceil(bytes / cap)``
shards per writer under greedy rotation), and the untimed cross-target
code-artifact batches (two variants times production and reproduction).
The per-input metrics bound and the time bounds are reviewed pins set at
integration time; the report shows which values fit the store.
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
        match = (re.fullmatch(r"([0-9]+)(?:u|U|ull|ULL)?", factor) or
                 re.fullmatch(r"UINT64_C\(([0-9]+)\)", factor))
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


REPORT_INPUTS = [
    "docs/native-retirement-support-v1.tsv",
    "tools/bench_service/profiles/native-retirement-performance-v1.blocked",
    "tools/native_retirement_contract.py",
    "tools/native_retirement_performance_binding.py",
    "tools/native_retirement_performance_schema.py",
    "tools/native_retirement_result_input.py",
    "tools/throughput/retirement_budget.h",
    "tools/throughput/retirement_campaign.h",
    "tools/throughput/retirement_capacity.py",
    "tools/throughput/retirement_command.h",
    "tools/throughput/retirement_execution.h",
    "tools/throughput/retirement_metrics.h",
    "tools/throughput/retirement_samples.h",
    "tools/throughput/retirement_stats.h",
    "tools/throughput/retirement_store.h",
    "tools/throughput/retirement_untimed.h",
]

# The measured-class figures the contract cites for its estimate (one process
# compiling 369 host fixtures takes about 1.1 s; x86 process startup about
# 34 ms, #1295). Illustrative only; the reviewed budget pins its own bounds.
CONTRACT_ESTIMATE_LARGE_BATCH_NS = 1_100_000_000
CONTRACT_ESTIMATE_SMALL_BATCH_NS = 34_000_000


def source_limits(root):
    """Every limit the model uses, parsed from the checked-in C sources."""
    tools = root / "tools/throughput"
    stats = tools / "retirement_stats.h"
    execution = tools / "retirement_execution.h"
    samples = tools / "retirement_samples.h"
    store = tools / "retirement_store.h"
    campaign = tools / "retirement_campaign.h"
    metrics = tools / "retirement_metrics.h"
    untimed = tools / "retirement_untimed.h"
    budget = tools / "retirement_budget.h"
    command = tools / "retirement_command.h"
    source = {
        "rounds": source_define(stats, "TP_RETIREMENT_ROUNDS"),
        "minimum_pairs": source_define(stats, "TP_RETIREMENT_MIN_PAIRS_PER_ROUND"),
        "maximum_pairs": source_define(execution, "TP_RETIREMENT_EXECUTION_MAX_PAIRS"),
        "warmups": source_define(execution, "TP_RETIREMENT_WARMUPS"),
        "campaign_stages": source_define(campaign, "TP_RETIREMENT_CAMPAIGN_STAGES"),
        "min_external_store_entries": source_define(
            campaign, "TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES"),
        "sample_total_records": source_product(samples, "TP_RETIREMENT_SAMPLE_TOTAL_RECORDS"),
        "sample_partition_records": source_product(samples, "TP_RETIREMENT_SAMPLE_PARTITION_RECORDS"),
        "sample_partitions": source_define(samples, "TP_RETIREMENT_SAMPLE_PARTITIONS"),
        "spool_record_bytes": source_define(samples, "TP_RETIREMENT_SAMPLE_RECORD_BYTES"),
        "sample_records_per_shard": source_product(samples, "TP_RETIREMENT_SAMPLE_SHARD_RECORDS"),
        "sample_record_bytes_max": source_define(samples, "TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX"),
        "batch_record_bytes_max": source_define(samples, "TP_RETIREMENT_BATCH_RECORD_BYTES_MAX"),
        "transcript_records_per_shard": source_define(execution, "TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS"),
        "transcript_bytes_per_shard": source_product(execution, "TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES"),
        "transcript_shard_cap": source_define(execution, "TP_RETIREMENT_TRANSCRIPT_SHARDS"),
        "transcript_record_bytes_max": source_define(execution, "TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX"),
        "metrics_shard_bytes": source_product(execution, "TP_RETIREMENT_METRICS_SHARD_BYTES"),
        "metrics_shard_cap": source_define(execution, "TP_RETIREMENT_METRICS_SHARDS"),
        "metrics_artifact_bytes": source_product(metrics, "TP_RETIREMENT_METRICS_ARTIFACT_BYTES"),
        "metrics_line_bytes": source_product(metrics, "TP_RETIREMENT_METRICS_LINE_BYTES"),
        "batch_inputs": source_define(metrics, "TP_RETIREMENT_BATCH_INPUTS"),
        "input_list_bytes": source_product(metrics, "TP_RETIREMENT_INPUT_LIST_BYTES"),
        "input_list_arguments": source_define(metrics, "TP_RETIREMENT_INPUT_LIST_ARGUMENTS"),
        "command_arguments": source_define(command, "TP_RETIREMENT_COMMAND_ARGUMENTS"),
        "command_bytes": source_define(command, "TP_RETIREMENT_COMMAND_BYTES"),
        "untimed_record_bytes_max": source_define(untimed, "TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX"),
        "untimed_batches_per_group": source_define(budget, "TP_RETIREMENT_BUDGET_UNTIMED_BATCHES"),
        "store_files": source_define(store, "TP_RETIREMENT_STORE_FILES"),
        "store_file_bytes": source_product(store, "TP_RETIREMENT_STORE_FILE_BYTES"),
        "store_total_bytes": source_product(store, "TP_RETIREMENT_STORE_TOTAL_BYTES"),
    }
    derivation = re.search(r'#define TP_RETIREMENT_BUDGET_DERIVATION \\\n\s*"([^"]*)"\s*\\\n\s*"([^"]*)"',
                           budget.read_text(encoding="utf-8"))
    if not derivation:
        raise ValueError("could not read the reviewed-budget derivation from retirement_budget.h")
    source["budget_derivation"] = derivation.group(1) + derivation.group(2)
    if source["campaign_stages"] != 2 or source["minimum_pairs"] % 2:
        raise ValueError("unexpected fixed campaign structure in source")
    if source["sample_partition_records"] % source["sample_records_per_shard"]:
        raise ValueError("numeric shards do not divide a full manifest partition")
    if source["metrics_shard_bytes"] > source["store_file_bytes"] \
            or source["metrics_artifact_bytes"] > source["metrics_shard_bytes"]:
        raise ValueError("a metrics shard must be one store file holding at least one artifact")
    return source


def metrics_shard_bound(artifacts, total_bytes, shard_bytes):
    """Greedy rotation: any two consecutive shards exceed one shard's capacity."""
    return min(artifacts, 2 * ceil_div(total_bytes, shard_bytes))


def metrics_artifact_bound(inputs, per_input, header, source):
    """The reviewed metrics bound of one object batch of ``inputs`` inputs."""
    value = header + inputs * per_input
    if not 0 < inputs <= source["batch_inputs"] or value > source["metrics_artifact_bytes"]:
        raise ValueError("a batch's reviewed metrics bound exceeds the per-artifact cap")
    return value


def campaign_model(groups, runtime_rows, pairs, untimed, per_input, source, header=4096):
    """Mirror tp_retirement_campaign_capacity for an A1 campaign.

    ``groups`` lists the timed batch groups as ``{"kind", "inputs", "members"}``
    (a singleton has one input and member); ``untimed`` lists the untimed
    code-artifact groups the same way.  ``per_input`` and ``header`` form the
    reviewed per-artifact metrics bound.
    """
    if pairs < source["minimum_pairs"] or pairs > source["maximum_pairs"] or pairs % 2:
        raise ValueError("pairs must follow the source-locked even sampling range")
    objects = [group for group in groups if group["kind"] == "object"]
    singletons = len(groups) - len(objects)
    if not groups or runtime_rows < 0 or runtime_rows > singletons:
        raise ValueError("runtime rows must be singleton stage groups of a nonempty campaign")
    stages = source["campaign_stages"]
    rounds = source["rounds"]
    per_unit = checked_product(2, source["warmups"] + checked_product(rounds, pairs))
    rows = sum(group["members"] for group in groups)
    compiler = checked_product(len(groups), per_unit)
    runtime = checked_product(runtime_rows, per_unit)
    invocations = compiler + runtime
    row_samples = checked_product(rows, rounds, pairs)
    batch_samples = checked_product(len(objects), rounds, pairs)
    samples = row_samples + batch_samples
    partitions = (ceil_div(row_samples, source["sample_partition_records"])
                  + ceil_div(batch_samples, source["sample_partition_records"]))
    metrics_per_batch = sum(metrics_artifact_bound(group["inputs"], per_input, header, source)
                            for group in objects)
    metrics_artifacts = checked_product(len(objects), per_unit)
    metrics_bytes = checked_product(metrics_per_batch, per_unit)
    metrics_shards = metrics_shard_bound(metrics_artifacts, metrics_bytes, source["metrics_shard_bytes"])
    transcript_shards = ceil_div(invocations, source["transcript_records_per_shard"])
    sample_shards = (ceil_div(row_samples, source["sample_records_per_shard"])
                     + ceil_div(batch_samples, source["sample_records_per_shard"]))
    untimed_objects = [group for group in untimed if group["kind"] == "object"]
    per_group = source["untimed_batches_per_group"]
    untimed_batches = checked_product(len(untimed), per_group)
    untimed_artifacts = checked_product(len(untimed_objects), per_group)
    untimed_bytes = checked_product(sum(metrics_artifact_bound(group["inputs"], per_input, header, source)
                                        for group in untimed_objects), per_group)
    untimed_shards = metrics_shard_bound(untimed_artifacts, untimed_bytes, source["metrics_shard_bytes"])
    untimed_record_files = 1 if untimed else 0
    untimed_record_bytes = checked_product(untimed_batches, source["untimed_record_bytes_max"])
    transcript_bytes = checked_product(invocations, source["transcript_record_bytes_max"])
    sample_bytes = (checked_product(row_samples, source["sample_record_bytes_max"])
                    + checked_product(batch_samples, source["batch_record_bytes_max"]))
    shard_files = stages * (transcript_shards + sample_shards + metrics_shards) + untimed_shards
    payload_files = shard_files + untimed_record_files
    payload_bytes = (stages * (transcript_bytes + sample_bytes + metrics_bytes)
                     + untimed_bytes + untimed_record_bytes)
    entry_cap = source["store_files"] - source["min_external_store_entries"]
    collector_fits = (
        transcript_shards <= source["transcript_shard_cap"]
        and sample_shards <= source["transcript_shard_cap"]
        and metrics_shards <= source["metrics_shard_cap"]
        and untimed_shards <= source["metrics_shard_cap"]
        and samples <= source["sample_total_records"]
        and partitions <= source["sample_partitions"])
    # Before M4 every per-batch metrics artifact was its own store entry.
    unsharded_files = (stages * (transcript_shards + sample_shards + metrics_artifacts)
                       + untimed_artifacts + untimed_record_files)
    return {
        "groups": len(groups), "object_groups": len(objects), "singleton_groups": singletons,
        "timed_rows": rows, "runtime_rows": runtime_rows, "pairs_per_round": pairs,
        "per_input_metrics_bound_bytes": per_input, "metrics_header_bound_bytes": header,
        "invocations_per_stage": invocations, "compiler_batches_per_stage": compiler,
        "runtime_processes_per_stage": runtime,
        "invocations_both_stages": stages * invocations,
        "row_samples_per_stage": row_samples, "batch_samples_per_stage": batch_samples,
        "sample_partitions_per_stage": partitions,
        "metrics_artifacts_per_stage": metrics_artifacts,
        "metrics_bytes_per_stage_upper_bound": metrics_bytes,
        "metrics_shards_per_stage_upper_bound": metrics_shards,
        "transcript_shards_per_stage": transcript_shards, "sample_shards_per_stage": sample_shards,
        "untimed_groups": len(untimed), "untimed_batches": untimed_batches,
        "untimed_metrics_artifacts": untimed_artifacts,
        "untimed_metrics_bytes_upper_bound": untimed_bytes,
        "untimed_metrics_shards_upper_bound": untimed_shards,
        "untimed_record_files": untimed_record_files,
        "payload_files": payload_files, "payload_bytes_upper_bound": payload_bytes,
        "entry_cap_after_minimum_external_entries": entry_cap,
        "store_total_bytes": source["store_total_bytes"],
        "entries_fit": payload_files <= entry_cap,
        "bytes_fit": payload_bytes <= source["store_total_bytes"],
        "collector_caps_fit": collector_fits,
        "fits": collector_fits and payload_files <= entry_cap
                and payload_bytes <= source["store_total_bytes"],
        "entries_if_each_metrics_artifact_were_a_store_file": unsharded_files,
        "external_entries_and_bytes_excluded": True,
    }


def maximum_fitting_per_input(groups, runtime_rows, pairs, untimed, source, header=4096, step=256):
    best = None
    per_input = step
    largest = max(group["inputs"] for group in list(groups) + list(untimed) if group["kind"] == "object")
    while header + largest * per_input <= source["metrics_artifact_bytes"]:
        if not campaign_model(groups, runtime_rows, pairs, untimed, per_input, source, header)["fits"]:
            break
        best = per_input
        per_input += step
    return best


def budget_counts(groups, runtime_rows, pairs, untimed, source):
    """The counts the reviewed-budget derivation multiplies, and the contract's
    illustrative per-batch estimate (not a reviewed bound)."""
    per_unit = 2 * (source["warmups"] + source["rounds"] * pairs)
    per_group = source["campaign_stages"] * per_unit
    large = sum(1 for group in groups if group["kind"] == "object" and group["inputs"] > 4)
    small = len(groups) - large
    illustrative_ns = per_group * (large * CONTRACT_ESTIMATE_LARGE_BATCH_NS
                                   + small * CONTRACT_ESTIMATE_SMALL_BATCH_NS)
    return {
        "derivation": source["budget_derivation"],
        "compiler_batches_both_stages": per_group * len(groups),
        "runtime_processes_both_stages": per_group * runtime_rows,
        "untimed_batches": source["untimed_batches_per_group"] * len(untimed),
        "illustrative_compiler_hours_at_contract_estimates": round(illustrative_ns / 3.6e12, 2),
        "illustrative_rates_are_a_reviewed_bound": False,
    }


def a1_groups(root, binding, contract):
    """Source-derived envelope of the A1 batch groups.

    Per native-host configuration, each fixture recipe of the supported
    subjects is one object group whose members are those subjects; every
    compiler-default group also carries the registered non-object controls and
    rejection fixtures as appended controls (the worst case for inputs and
    metrics bytes). Link and self-host stage rows are singleton groups. The
    untimed groups repeat the object shapes on every cross-target, without
    controls.
    """
    support = read_tsv(root / binding.SUPPORT_DECLARATION_PATH)
    supported = [row["path"] for row in support
                 if row["role"] == "subject" and row["compile_obligation"] == "supported-object-zero-fallback"]
    non_object = [row["path"] for row in support
                  if row["role"] == "subject" and row["compile_obligation"] == "registered-non-object-control"]
    rejection = [row["path"] for row in support if row["role"] == "negative-diagnostic-fixture"]
    recipes = {}
    for path in supported:
        recipes.setdefault(contract.expected_fixture_recipe(path)[0], []).append(path)
    configurations = len(binding.ALLOCATORS) * len(binding.FRONTENDS) * len(binding.PIC)
    per_configuration = []
    for recipe in sorted(recipes, key=lambda name: (name != "compiler-default", name)):
        members = recipes[recipe]
        controls = non_object + rejection if recipe == "compiler-default" else []
        per_configuration.append({"kind": "object", "recipe": recipe, "members": len(members),
                                  "inputs": len(members) + len(controls),
                                  "fixtures": members + controls})
    timed = [dict(group) for _ in range(configurations) for group in per_configuration]
    stage_rows = len(binding.STAGES) - 1
    timed.extend({"kind": "singleton", "recipe": "stage", "members": 1, "inputs": 1, "fixtures": []}
                 for _ in range(stage_rows))
    untimed_targets = len(binding.TARGETS) - 1
    untimed = [{"kind": "object", "recipe": group["recipe"], "members": group["members"],
                "inputs": group["members"], "fixtures": group["fixtures"][:group["members"]]}
               for _ in range(untimed_targets * configurations) for group in per_configuration]
    return timed, untimed, configurations, per_configuration, stage_rows


def input_list_bytes(fixtures):
    """Canonical response-file size of one batch (validator ``_input_list_bytes``)."""
    return sum(3 + len(item) + item.count('"') + item.count("\\") for item in fixtures)


def build_report(root, require_committed=True):
    root = root.resolve()
    input_digests = verify_committed_inputs(root, REPORT_INPUTS) if require_committed else {}
    sys.path.insert(0, str(root / "tools"))
    import native_retirement_contract as contract
    import native_retirement_performance_binding as binding

    support_bytes = (root / binding.SUPPORT_DECLARATION_PATH).read_bytes()
    support_sha = hashlib.sha256(support_bytes).hexdigest()
    profile_path = root / "tools/bench_service/profiles/native-retirement-performance-v1.blocked"
    profile = parse_kv(profile_path)
    if profile.get("support-declaration") != binding.SUPPORT_DECLARATION_PATH:
        raise ValueError("blocked profile does not name the checked-in support declaration")
    if profile.get("support-declaration-sha256") != support_sha:
        raise ValueError("support declaration digest differs from the blocked profile pin")
    source = source_limits(root)
    timed, untimed, configurations, per_configuration, stage_rows = a1_groups(root, binding, contract)
    largest_list = max(input_list_bytes(group["fixtures"]) for group in per_configuration)
    largest_inputs = max(group["inputs"] for group in per_configuration)
    if largest_inputs > source["batch_inputs"] or largest_list > source["input_list_bytes"] \
            or largest_inputs > source["input_list_arguments"]:
        raise ValueError("a batch's input list exceeds the response-file bounds")
    scenarios = {}
    for pairs in (source["minimum_pairs"], source["maximum_pairs"]):
        for per_input in (4096, 8192, 16384):
            for runtime in (0, stage_rows):
                model = campaign_model(timed, runtime, pairs, untimed, per_input, source)
                scenarios[f"pairs-{pairs}/per-input-{per_input}/runtime-{runtime}"] = model
    maxima = {f"pairs-{pairs}": maximum_fitting_per_input(timed, stage_rows, pairs, untimed, source)
              for pairs in (source["minimum_pairs"], source["maximum_pairs"])}
    report = {
        "schema": "buster-native-retirement-capacity-model-v2",
        "source": {
            "commit": git_value(root, "rev-parse", "HEAD") if require_committed else None,
            "tree": git_value(root, "rev-parse", "HEAD^{tree}") if require_committed else None,
            "all_report_inputs_match_head": require_committed,
            "report_input_sha256": input_digests,
            "support_declaration": binding.SUPPORT_DECLARATION_PATH,
            "support_declaration_sha256": support_sha,
            "profile": str(profile_path.relative_to(root)),
            "profile_status": profile.get("status"),
            "campaign_budget_pinned": "campaign-budget-sha256" in profile,
        },
        "population": {
            "native_host_configurations": configurations,
            "recipe_groups_per_configuration": [
                {"recipe": group["recipe"], "members": group["members"], "inputs": group["inputs"]}
                for group in per_configuration],
            "timed_object_groups": sum(group["kind"] == "object" for group in timed),
            "stage_singleton_groups": stage_rows,
            "timed_rows_envelope": sum(group["members"] for group in timed),
            "untimed_groups": len(untimed),
            "largest_batch_inputs": largest_inputs,
            "largest_input_list_bytes": largest_list,
            "batch_argv_is_constant_in_inputs": True,
            "actual_eligibility_available": False,
        },
        "policy_limits": source,
        "scenarios": scenarios,
        "maximum_fitting_per_input_metrics_bound": maxima,
        "reviewed_budget": {
            **budget_counts(timed, stage_rows, source["maximum_pairs"], untimed, source),
            "pinned": "campaign-budget-sha256" in profile,
            "one_hour_worker_budget_applies": False,
        },
        "evidence": {
            "capacity_is_arithmetic_only": True,
            "reviewed_bounds_are_integration_pins": True,
            "fixture_or_model_is_acceptance": False,
        },
    }
    return report


def render_text(report):
    population = report["population"]
    lines = [
        f"CAPACITY_MODEL_A1 commit={report['source']['commit']} tree={report['source']['tree']}",
        f"support_sha256={report['source']['support_declaration_sha256']} "
        f"profile={report['source']['profile_status']} "
        f"campaign_budget_pinned={str(report['source']['campaign_budget_pinned']).lower()}",
        f"groups object={population['timed_object_groups']} singleton={population['stage_singleton_groups']} "
        f"configurations={population['native_host_configurations']} "
        f"timed_rows_envelope={population['timed_rows_envelope']} untimed_groups={population['untimed_groups']} "
        f"largest_batch_inputs={population['largest_batch_inputs']} "
        f"largest_input_list_bytes={population['largest_input_list_bytes']}",
    ]
    for name, model in report["scenarios"].items():
        lines.append(
            f"{name} invocations_per_stage={model['invocations_per_stage']} "
            f"metrics_artifacts_per_stage={model['metrics_artifacts_per_stage']} "
            f"metrics_shards_per_stage<={model['metrics_shards_per_stage_upper_bound']} "
            f"untimed_batches={model['untimed_batches']} payload_files={model['payload_files']}"
            f"/{model['entry_cap_after_minimum_external_entries']} "
            f"payload_bytes={model['payload_bytes_upper_bound']}/{model['store_total_bytes']} "
            f"verdict={'fits' if model['fits'] else 'rejected'} "
            f"unsharded_entries={model['entries_if_each_metrics_artifact_were_a_store_file']}")
    maxima = report["maximum_fitting_per_input_metrics_bound"]
    budget = report["reviewed_budget"]
    lines.extend([
        "max_fitting_per_input_metrics_bound " + " ".join(f"{key}={value}" for key, value in maxima.items()),
        f"reviewed_budget pinned={str(budget['pinned']).lower()} "
        f"compiler_batches={budget['compiler_batches_both_stages']} "
        f"runtime_processes={budget['runtime_processes_both_stages']} untimed_batches={budget['untimed_batches']} "
        f"illustrative_compiler_hours={budget['illustrative_compiler_hours_at_contract_estimates']}",
        "CONCLUSION modeled-only; payload bytes use the source-proven maximal line widths and the reviewed "
        "per-input metrics bound, and exclude the caller's external entries/bytes; the recipe stays blocked.",
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
