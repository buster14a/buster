#!/usr/bin/env python3
"""Recompute the A1 #881 campaign capacity from the pinned current source tree.

This is arithmetic only. Amendment A1 times the native-host batch groups: one
fresh compiler process per (configuration, recipe) object group compiles every
fixture of the group, and each link or self-host stage row is a singleton
group. The checked-in support declaration and the fixture recipe table fix the
object group shapes; the blocked service profile has no authenticated #508 rows
or validator-derived eligibility, so object group sizes are a source-derived
envelope (every supported subject timed, every registered control appended),
never an admitted campaign size or a host-rate claim.

Stage singletons (native link and self-host rows, timed) and untimed
singletons (cross-target link and self-host rows) are counted, never assumed:
from canonical #508 performance rows when ``--performance-rows`` supplies them
(the validator's own ``_batch_groups``/``_untimed_groups`` partition), and
otherwise from the declaration-derived minimum the validator enforces (one
native link and one native self-host row, no cross-target stage row). The
report also states the declaration's upper envelope for stage rows (both
stages on every declared object identity) and how many further runtime-eligible
stage singletons the store could hold, so the minimum is never mistaken for an
upper bound.

The model mirrors tools/throughput/retirement_campaign.h:
``(G + U) * 2 * (warmups + rounds * pairs)`` invocations per stage, the
``row-round-pair`` and ``group-round-pair`` result populations, per-batch
metrics artifacts packed into metrics shards (at most ``2 * ceil(bytes / cap)``
shards per writer under greedy rotation), and the untimed cross-target
code-artifact batches (two variants times production and reproduction).
The per-input metrics bound and the time bounds are reviewed pins set at
integration time; the report shows which values fit the store and states the
largest per-input metrics bound that fits at the 254-pair maximum as an
explicit assumption the reviewed budget must satisfy, never a measurement.

It also mirrors tools/bench_service/retirement_compose.c's
``tp_compose_bounds_of`` (``composer_model``): the composer's outputs,
including the #619 statistics adapter input, which (#1880) is a manifest over
greedy whole-line series shards of at most one 64 MiB store file each, so
the whole A1 family's series fits the per-file cap as some tens of entries.
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
    "tools/bench_service/retirement_compose.c",
    "tools/bench_service/retirement_compose.h",
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
    compose_c = root / "tools/bench_service/retirement_compose.c"
    compose_h = root / "tools/bench_service/retirement_compose.h"
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
        "receipt_bytes": source_product(execution, "TP_RETIREMENT_RECEIPT_BYTES"),
        "code_record_bytes_max": source_define(samples, "TP_RETIREMENT_CODE_RECORD_BYTES_MAX"),
        "maximum_cells": source_define(stats, "TP_RETIREMENT_MAX_CELLS"),
        "compose_dimensions": source_define(compose_h, "TP_RETIREMENT_COMPOSE_DIMENSIONS"),
        "compose_bootstrap_members": source_define(compose_h, "TP_RETIREMENT_COMPOSE_BOOTSTRAP_MEMBERS"),
        "compose_cell_members": source_define(compose_h, "TP_RETIREMENT_COMPOSE_CELL_MEMBERS"),
        "compose_partitions": source_define(compose_h, "TP_RETIREMENT_COMPOSE_PARTITIONS"),
        "compose_fixed_outputs": source_define(compose_h, "TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS"),
        "compose_series_shard_cap": source_define(compose_h, "TP_RETIREMENT_COMPOSE_SERIES_SHARDS"),
        "compose_ratio_line_bytes": source_define(compose_c, "TP_COMPOSE_RATIO_LINE_BYTES"),
        "compose_member_line_bytes": source_define(compose_c, "TP_COMPOSE_MEMBER_LINE_BYTES"),
        "compose_series_header_bytes": source_define(compose_c, "TP_COMPOSE_SERIES_HEADER_BYTES"),
        "compose_series_end_bytes": source_define(compose_c, "TP_COMPOSE_SERIES_END_BYTES"),
        "compose_series_line_bytes": source_define(compose_c, "TP_COMPOSE_SERIES_LINE_BYTES"),
        "compose_series_manifest_fixed_bytes": source_define(compose_c, "TP_COMPOSE_SERIES_MANIFEST_FIXED_BYTES"),
        "compose_series_manifest_line_bytes": source_define(compose_c, "TP_COMPOSE_SERIES_MANIFEST_LINE_BYTES"),
        "compose_replay_member_bytes": source_define(compose_c, "TP_COMPOSE_REPLAY_MEMBER_BYTES"),
        "compose_replay_fixed_bytes": source_define(compose_c, "TP_COMPOSE_REPLAY_FIXED_BYTES"),
        "compose_manifest_fixed_bytes": source_define(compose_c, "TP_COMPOSE_MANIFEST_FIXED_BYTES"),
        "compose_manifest_shard_bytes": source_define(compose_c, "TP_COMPOSE_MANIFEST_SHARD_BYTES"),
        "compose_bundle_bytes": source_define(compose_c, "TP_COMPOSE_BUNDLE_BYTES"),
        "compose_seal_fixed_bytes": source_define(compose_c, "TP_COMPOSE_SEAL_FIXED_BYTES"),
        "compose_seal_entry_bytes": source_define(compose_c, "TP_COMPOSE_SEAL_ENTRY_BYTES"),
        "compose_retained_line_bytes": source_define(compose_c, "TP_COMPOSE_RETAINED_LINE_BYTES"),
    }
    retained_header = re.findall(r'^#define\s+TP_RETIREMENT_RETAINED_MANIFEST_HEADER\s+"([^"]*)\\n"\s*$',
                                 store.read_text(encoding="utf-8"), re.MULTILINE)
    if len(retained_header) != 1:
        raise ValueError("could not read the retained manifest header from retirement_store.h")
    # sizeof of the C string literal: its text, the LF and the NUL.
    source["retained_manifest_header_size"] = len(retained_header[0]) + 2
    series_shard = re.findall(r"^#define\s+TP_RETIREMENT_COMPOSE_SERIES_SHARD_BYTES\s+(\S+)\s*$",
                              compose_h.read_text(encoding="utf-8"), re.MULTILINE)
    if series_shard != ["TP_RETIREMENT_STORE_FILE_BYTES"]:
        raise ValueError("the composer's series shard is not one store file")
    source["compose_series_shard_bytes"] = source["store_file_bytes"]
    derivation = re.search(r'#define TP_RETIREMENT_BUDGET_DERIVATION((?:\s*\\\n\s*"[^"]*")+)',
                           budget.read_text(encoding="utf-8"))
    if not derivation:
        raise ValueError("could not read the reviewed-budget derivation from retirement_budget.h")
    source["budget_derivation"] = "".join(re.findall(r'"([^"]*)"', derivation.group(1)))
    stage_names = re.search(r'tp_retirement_budget_stage_names\[[A-Z_]+\] = \{\s*([^}]*)\}',
                            budget.read_text(encoding="utf-8"))
    if not stage_names:
        raise ValueError("could not read the reviewed-budget stage names from retirement_budget.h")
    source["budget_stages"] = re.findall(r'"([^"]*)"', stage_names.group(1))
    if source["budget_stages"] != ["object", "link", "self-host-stage1"]:
        raise ValueError("the reviewed budget's stage keys differ from the validator's stages")
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


def composer_model(model, code_rows, source, prior_entries=0, bootstrap_members=None):
    """Mirror tp_compose_bounds_of (tools/bench_service/retirement_compose.c).

    The family's cells are the timed rows (wall time and peak memory), the
    runtime rows and the object groups (the batch pair); aggregates and
    slices depend on dimension values the model does not carry, so they are
    taken at the #619 bootstrap cap unless ``bootstrap_members`` is given.
    The series (the #619 adapter input stream) is bounded by its maximal line
    widths and split (#1880) into greedy whole-line shards of at most one
    store file: every shard but the last holds more than ``shard - line_max``
    bytes. ``prior_entries`` sizes the sealed record, as the composer's shape
    does. Returns every output's bound, the files (series shards included)
    and whether each output fits its caps.
    """
    per_unit = checked_product(source["rounds"], model["pairs_per_round"])
    rows, runtime, objects = model["timed_rows"], model["runtime_rows"], model["object_groups"]
    manifests = manifest_count = 0
    for records in (rows * per_unit, objects * per_unit):
        partitions = ceil_div(records, source["sample_partition_records"])
        manifest_count += partitions
        manifests += (partitions * source["compose_manifest_fixed_bytes"]
                      + ceil_div(records, source["sample_records_per_shard"])
                      * source["compose_manifest_shard_bytes"])
    cells = [rows, rows, runtime, objects, objects]
    bootstrap = source["compose_bootstrap_members"] if bootstrap_members is None else bootstrap_members
    members = bootstrap + sum(cells)
    lines = checked_product(source["compose_dimensions"] + 2, sum(cells), per_unit)
    series = (source["compose_series_header_bytes"]
              + members * (source["compose_member_line_bytes"] + source["compose_series_end_bytes"])
              + lines * source["compose_ratio_line_bytes"])
    shard_bytes = source["compose_series_shard_bytes"]
    series_shards = 1 + (series - 1) // (shard_bytes - source["compose_series_line_bytes"] + 1)
    outputs = {
        "result_input_manifests": manifests,
        "code_records": code_rows * source["code_record_bytes_max"],
        "adapter_input_series": series,
        "adapter_input_manifest": (source["compose_series_manifest_fixed_bytes"]
                                   + series_shards * source["compose_series_manifest_line_bytes"]),
        "adapter_output": source["compose_replay_fixed_bytes"] + members * source["compose_replay_member_bytes"],
        "result_bundle": source["compose_bundle_bytes"],
        "execution_receipt": source["receipt_bytes"],
        "retained_manifest": (source["retained_manifest_header_size"]
                              + source["store_files"] * source["compose_retained_line_bytes"]),
        "sealed_result": (source["compose_seal_fixed_bytes"]
                          + (prior_entries + source["store_files"]) * source["compose_seal_entry_bytes"]),
    }
    single_files = {name: value for name, value in outputs.items() if name != "adapter_input_series"}
    family_fits = (all(cells) and sum(cells) <= source["compose_cell_members"]
                   and rows <= source["maximum_cells"] and 0 < code_rows <= source["maximum_cells"]
                   and manifest_count <= source["compose_partitions"])
    return {
        "family_cells": sum(cells), "family_members_upper_bound": members,
        "series_lines": lines, "series_bytes_upper_bound": series,
        "series_shard_bytes": shard_bytes, "series_shards_upper_bound": series_shards,
        "single_file_series_fits": series <= source["store_file_bytes"],
        "outputs": outputs,
        "files": manifest_count + source["compose_fixed_outputs"] + series_shards,
        "bytes_upper_bound": sum(outputs.values()),
        "fits": (family_fits and series_shards <= source["compose_series_shard_cap"]
                 and all(value <= source["store_file_bytes"] for value in single_files.values())),
    }


def store_with_composer(model, composer, source):
    """The whole store: both campaign stages' payload plus the composer's
    outputs, against the entries left after the minimum external entries and
    the 128 GiB total (external entries, the prior closure and retained
    declarations are the caller's and excluded, as in ``campaign_model``)."""
    entries = model["payload_files"] + composer["files"]
    total = model["payload_bytes_upper_bound"] + composer["bytes_upper_bound"]
    return {
        "entries": entries, "entry_cap": model["entry_cap_after_minimum_external_entries"],
        "bytes": total, "byte_cap": source["store_total_bytes"],
        "fits": (model["fits"] and composer["fits"]
                 and entries <= model["entry_cap_after_minimum_external_entries"]
                 and total <= source["store_total_bytes"]),
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


def budget_key(group):
    """The reviewed budget's key for one group: (kind, stage)."""
    return (group["kind"], group.get("stage", "object" if group["kind"] == "object" else None))


def budget_counts(groups, runtime_rows, pairs, untimed, source):
    """The counts the reviewed-budget derivation multiplies, keyed as the
    budget keys its bounds (group kind and stage; untimed groups separately),
    and the contract's illustrative object-batch estimate (not a reviewed
    bound; stage singletons have no contract estimate and are excluded)."""
    per_unit = 2 * (source["warmups"] + source["rounds"] * pairs)
    per_group = source["campaign_stages"] * per_unit
    for group in list(groups) + list(untimed):
        kind, stage = budget_key(group)
        if (kind, stage) != ("object", "object") and (kind != "singleton" or stage not in
                                                      source["budget_stages"][1:]):
            raise ValueError("a group has no reviewed-budget key (kind and stage)")

    def by_key(items, repeats):
        counts = {}
        for group in items:
            key = "/".join(budget_key(group))
            counts[key] = counts.get(key, 0) + repeats
        return dict(sorted(counts.items()))

    large = sum(1 for group in groups if group["kind"] == "object" and group["inputs"] > 4)
    small = sum(1 for group in groups if group["kind"] == "object") - large
    illustrative_ns = per_group * (large * CONTRACT_ESTIMATE_LARGE_BATCH_NS
                                   + small * CONTRACT_ESTIMATE_SMALL_BATCH_NS)
    return {
        "derivation": source["budget_derivation"],
        "compiler_batches_both_stages": per_group * len(groups),
        "compiler_batches_by_kind_and_stage": by_key(groups, per_group),
        "runtime_processes_both_stages": per_group * runtime_rows,
        "untimed_batches": source["untimed_batches_per_group"] * len(untimed),
        "untimed_batches_by_kind_and_stage": by_key(untimed, source["untimed_batches_per_group"]),
        "singletons_costed_as_one_input_batches": False,
        "untimed_bounds_are_the_slowest_untimed_target": True,
        "illustrative_object_batch_hours_at_contract_estimates": round(illustrative_ns / 3.6e12, 2),
        "illustrative_rates_are_a_reviewed_bound": False,
    }


def maximum_fitting_stage_singletons(groups, runtime_rows, pairs, untimed, per_input, source):
    """How many further runtime-eligible timed stage singletons still fit."""
    model = campaign_model(groups, runtime_rows, pairs, untimed, per_input, source)
    if not model["fits"]:
        return None
    low, high = 0, 1
    extra = {"kind": "singleton", "stage": "link", "inputs": 1, "members": 1}

    def fits(count):
        return campaign_model(list(groups) + [extra] * count, runtime_rows + count, pairs, untimed,
                              per_input, source)["fits"]
    while fits(high):
        low, high = high, high * 2
    while high - low > 1:
        middle = (low + high) // 2
        low, high = (middle, high) if fits(middle) else (low, middle)
    return low


def stage_rows_from_performance_rows(binding, data):
    """Count stage singletons from canonical #508 performance rows with the
    validator's own partition: timed singletons by stage, the runtime-eligible
    timed rows, and untimed singletons by stage (code-observed cross-target
    link and self-host rows)."""
    parsed, _axes, _family = binding._performance_rows(data, "performance_rows")
    timed = [group for group in binding._batch_groups(parsed)
             if group["kind"] == binding.SINGLETON_STAGE_GROUP]
    untimed = [group for group in binding._untimed_groups(parsed)
               if group["kind"] == binding.SINGLETON_STAGE_GROUP]
    runtime = sum(1 for row in binding._timed_rows(parsed) if row["metrics"]["generated_runtime"])
    return ([group["identity"]["artifact_stage"] for group in timed], runtime,
            [group["identity"]["artifact_stage"] for group in untimed], "census")


def stage_rows_from_declaration(binding):
    """The declaration-derived minimum the validator enforces: a complete
    stage population has at least one native link and one native self-host
    row (SUPPORT_MIN_STAGE_ROW_COUNT - SUPPORT_OBJECT_ROW_COUNT stage rows) and
    needs no cross-target stage row. Runtime eligibility is conditional, so
    the worst case (every timed stage row runtime-eligible) is used."""
    minimum = binding.SUPPORT_MIN_STAGE_ROW_COUNT - binding.SUPPORT_OBJECT_ROW_COUNT
    stages = [stage for stage in binding.STAGES if stage != "object"]
    if minimum != len(stages):
        raise ValueError("the validator's minimum stage population is not one row per stage")
    return stages, minimum, [], "declaration-minimum"


def a1_groups(root, binding, contract, stage_rows=None):
    """Source-derived envelope of the A1 batch groups.

    Per native-host configuration, each fixture recipe of the supported
    subjects is one object group whose members are those subjects; every
    compiler-default group also carries the registered non-object controls and
    rejection fixtures as appended controls (the worst case for inputs and
    metrics bytes). Link and self-host stage rows are singleton groups counted
    by ``stage_rows`` (census or declaration minimum). The untimed object
    groups repeat the object shapes on every cross-target, without controls;
    the untimed singletons are the counted cross-target stage rows.
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
    timed = [dict(group, stage="object") for _ in range(configurations) for group in per_configuration]
    timed_stages, runtime, untimed_stages, stage_source = stage_rows or stage_rows_from_declaration(binding)
    timed.extend({"kind": "singleton", "stage": stage, "recipe": "stage", "members": 1, "inputs": 1,
                  "fixtures": []} for stage in timed_stages)
    untimed_targets = len(binding.TARGETS) - 1
    untimed = [{"kind": "object", "stage": "object", "recipe": group["recipe"], "members": group["members"],
                "inputs": group["members"], "fixtures": group["fixtures"][:group["members"]]}
               for _ in range(untimed_targets * configurations) for group in per_configuration]
    untimed.extend({"kind": "singleton", "stage": stage, "recipe": "stage", "members": 1, "inputs": 1,
                    "fixtures": []} for stage in untimed_stages)
    stages = {"source": stage_source, "runtime_rows": runtime,
              "timed_singletons_by_stage": {stage: timed_stages.count(stage) for stage in binding.STAGES[1:]},
              "untimed_singletons_by_stage": {stage: untimed_stages.count(stage)
                                              for stage in binding.STAGES[1:]},
              "declared_object_identities": binding.SUPPORT_OBJECT_ROW_COUNT,
              "native_supported_object_identities": configurations * sum(
                  group["members"] for group in per_configuration)}
    # Every stage row shares its identity with a declared object row, so both
    # stages on every declared identity bound the stage rows from above.
    stages["stage_rows_upper_envelope"] = {
        "timed": stages["native_supported_object_identities"] * (len(binding.STAGES) - 1),
        "untimed": (binding.SUPPORT_OBJECT_ROW_COUNT - stages["native_supported_object_identities"])
        * (len(binding.STAGES) - 1)}
    return timed, untimed, configurations, per_configuration, stages


def input_list_bytes(fixtures):
    """Canonical response-file size of one batch (validator ``_input_list_bytes``)."""
    return sum(3 + len(item) + item.count('"') + item.count("\\") for item in fixtures)


def build_report(root, require_committed=True, performance_rows=None):
    root = root.resolve()
    input_digests = verify_committed_inputs(root, REPORT_INPUTS) if require_committed else {}
    sys.path.insert(0, str(root / "tools"))
    import native_retirement_contract as contract
    import native_retirement_performance_binding as binding
    stage_rows = None
    rows_sha256 = None
    if performance_rows is not None:
        data = Path(performance_rows).read_bytes()
        rows_sha256 = hashlib.sha256(data).hexdigest()
        stage_rows = stage_rows_from_performance_rows(binding, data)

    support_bytes = (root / binding.SUPPORT_DECLARATION_PATH).read_bytes()
    support_sha = hashlib.sha256(support_bytes).hexdigest()
    profile_path = root / "tools/bench_service/profiles/native-retirement-performance-v1.blocked"
    profile = parse_kv(profile_path)
    if profile.get("support-declaration") != binding.SUPPORT_DECLARATION_PATH:
        raise ValueError("blocked profile does not name the checked-in support declaration")
    if profile.get("support-declaration-sha256") != support_sha:
        raise ValueError("support declaration digest differs from the blocked profile pin")
    source = source_limits(root)
    timed, untimed, configurations, per_configuration, stages = a1_groups(root, binding, contract, stage_rows)
    runtime_rows = stages["runtime_rows"]
    largest_list = max(input_list_bytes(group["fixtures"]) for group in per_configuration)
    largest_inputs = max(group["inputs"] for group in per_configuration)
    if largest_inputs > source["batch_inputs"] or largest_list > source["input_list_bytes"] \
            or largest_inputs > source["input_list_arguments"]:
        raise ValueError("a batch's input list exceeds the response-file bounds")
    # Code bytes are recorded once per code-observed row on every target: the
    # timed rows and every untimed group's members.
    code_rows = sum(group["members"] for group in timed) + sum(group["members"] for group in untimed)
    scenarios = {}
    for pairs in (source["minimum_pairs"], source["maximum_pairs"]):
        for per_input in (4096, 8192, 16384):
            for runtime in sorted({0, runtime_rows}):
                model = campaign_model(timed, runtime, pairs, untimed, per_input, source)
                if runtime:
                    # The #619 family needs a runtime cell; without one the
                    # composer refuses, so only runtime-bearing scenarios
                    # carry its outputs.
                    model["composer"] = composer_model(model, code_rows, source)
                    model["store_with_composer"] = store_with_composer(model, model["composer"], source)
                scenarios[f"pairs-{pairs}/per-input-{per_input}/runtime-{runtime}"] = model
    maxima = {f"pairs-{pairs}": maximum_fitting_per_input(timed, runtime_rows, pairs, untimed, source)
              for pairs in (source["minimum_pairs"], source["maximum_pairs"])}
    maximum_pairs = source["maximum_pairs"]
    assumption = maxima[f"pairs-{maximum_pairs}"]
    headroom = {f"per-input-{per_input}": maximum_fitting_stage_singletons(
                    timed, runtime_rows, maximum_pairs, untimed, per_input, source)
                for per_input in (4096, 8192)}
    report = {
        "schema": "buster-native-retirement-capacity-model-v2",
        "source": {
            "commit": git_value(root, "rev-parse", "HEAD") if require_committed else None,
            "tree": git_value(root, "rev-parse", "HEAD^{tree}") if require_committed else None,
            "all_report_inputs_match_head": require_committed,
            "report_input_sha256": input_digests,
            "support_declaration": binding.SUPPORT_DECLARATION_PATH,
            "support_declaration_sha256": support_sha,
            "performance_rows_sha256": rows_sha256,
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
            "stage_singleton_groups": sum(group["kind"] == "singleton" for group in timed),
            "stage_rows": stages,
            "timed_rows_envelope": sum(group["members"] for group in timed),
            "untimed_groups": len(untimed),
            "untimed_singleton_groups": sum(group["kind"] == "singleton" for group in untimed),
            "code_rows": code_rows,
            "largest_batch_inputs": largest_inputs,
            "largest_input_list_bytes": largest_list,
            "batch_argv_is_constant_in_inputs": True,
            "actual_eligibility_available": False,
        },
        "policy_limits": source,
        "scenarios": scenarios,
        "maximum_fitting_per_input_metrics_bound": maxima,
        "measured_bound_assumption": {
            "pairs_per_round": maximum_pairs,
            "maximum_per_input_metrics_bytes": assumption,
            "statement": (f"the reviewed per-input metrics bound must be at most {assumption} bytes per "
                          f"input at {maximum_pairs} pairs for this population; this is an assumption "
                          "about measured metrics sizes that integration must confirm, not a measurement"),
            "measured_here": False,
        },
        "additional_runtime_stage_singletons_fitting_at_maximum_pairs": headroom,
        "reviewed_budget": {
            **budget_counts(timed, runtime_rows, source["maximum_pairs"], untimed, source),
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
        f"stage_source={population['stage_rows']['source']} "
        f"untimed_singletons={population['untimed_singleton_groups']} "
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
        if "composer" in model:
            composer, store = model["composer"], model["store_with_composer"]
            lines.append(
                f"{name} composer series_bytes<={composer['series_bytes_upper_bound']} "
                f"series_shards<={composer['series_shards_upper_bound']} composer_files={composer['files']} "
                f"store_entries={store['entries']}/{store['entry_cap']} "
                f"store_bytes={store['bytes']}/{store['byte_cap']} "
                f"verdict={'fits' if store['fits'] else 'rejected'}")
    maxima = report["maximum_fitting_per_input_metrics_bound"]
    budget = report["reviewed_budget"]
    lines.extend([
        "max_fitting_per_input_metrics_bound " + " ".join(f"{key}={value}" for key, value in maxima.items()),
        "ASSUMPTION " + report["measured_bound_assumption"]["statement"],
        f"reviewed_budget pinned={str(budget['pinned']).lower()} "
        f"compiler_batches={budget['compiler_batches_both_stages']} "
        f"runtime_processes={budget['runtime_processes_both_stages']} untimed_batches={budget['untimed_batches']} "
        f"illustrative_object_batch_hours={budget['illustrative_object_batch_hours_at_contract_estimates']}",
        "CONCLUSION modeled-only; payload bytes use the source-proven maximal line widths and the reviewed "
        "per-input metrics bound, and exclude the caller's external entries/bytes; the recipe stays blocked.",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2],
                        help="immutable checkout root (default: this repository)")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--performance-rows", type=Path, default=None,
                        help="canonical #508 performance rows: count stage singletons from the census")
    args = parser.parse_args()
    try:
        report = build_report(args.repo, performance_rows=args.performance_rows)
    except (OSError, ValueError, OverflowError, KeyError) as error:
        parser.error(str(error))
    if args.format == "json":
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(render_text(report))


if __name__ == "__main__":
    main()
