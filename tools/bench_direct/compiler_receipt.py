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

A published check names the docs/performance-validation-v1.json rows its
validated legs discharged (covered_inventory_rows); a failed, skipped,
superseded or unrequested leg discharges nothing, and
performance_inventory_test.py keeps INVENTORY_LEGS and the inventory in step.

Map (searchable symbols):
    RECEIPT_SCHEMA, LAB_SCHEMA, MODES, check_name, check_marker   identities
    attempt_marker                                         one attempt's check (#2803)
    PROFILE                                                frozen profile
    APPROVED_HOST, observed_cpu_model, host_problem        observed Zen 5 host
    MEASURED_OUTCOMES, MIN_PAIRS, classify                 core validity
    THROUGHPUT_PROFILE, classify_throughput, throughput_digest   corpus (#2761)
    THROUGHPUT_HARNESS_FLOOR, HARNESS_ADMISSIONS           base-harness admission (#3714)
    THROUGHPUT_HISTORICAL_PROFILE, THROUGHPUT_HISTORICAL_REQUEST_LINE   named historical corpus (#3714)
    EVIDENCE_INVENTORY, evidence_lab_required, evidence_corpus_required,
        evidence_identity                                  sealed raw evidence v1 (#2918)
    SCALING_REQUEST, SCALING_PROFILE, classify_scaling, scaling_digest   multi-TU scaling (#424)
    INLINE_ACCEPTANCE_PROFILE, validate_inline_acceptance  issue #48 same-candidate inliner pair
    AST_PILOT_ACCEPTANCE_PROFILE, AST_PILOT_BUDGET, ast_pilot_budget_verdict,
        validate_ast_pilot_acceptance                      issue #3397 -fc-ast-pilot pair
    CONSTANT_REUSE_ACCEPTANCE_PROFILE,
        validate_constant_reuse_acceptance                 issue #3011 constant-reuse flag pairs
    PROCESS_FLOOR_PROFILE, process_floor_verdict,
        validate_process_floor_acceptance                  issue #1125 per-process floor and its controls
    THROUGHPUT_REAL_SOURCE_PROFILE, validate_throughput_real_source,
        throughput_real_source_digest                      real-source throughput (#2761)
    PRODUCTION_PROFILE_PROFILE, validate_production_profile,
        production_profile_fields                          PGO/LTO production profile (#2761)
    COMPILER_PROBES_PROFILE, validate_compiler_probes,
        compiler_probes_digest                             FAST RA and line-mark probes (#2761)
    INVENTORY_LEGS, inventory_leg_record, covered_inventory_rows,
        render_inventory_coverage                          inventory rows each validated leg discharged (#2761)
    FRONTEND_SELECTOR_PROFILE, validate_frontend_selector, selection_medians,
        frontend_selector_digest                           frontend and selector probes (#2761)
    frontend_scaling_population, frontend_scaling_problems,
        frontend_scaling_digest                            shared benchmark_frontend_scaling.py checks
    FRONTEND_NESTING_PROFILE, validate_frontend_nesting    deep-nesting families (#2676)
    WILD_LINKER_PROFILE, validate_wild_linker, wild_linker_population,
        wild_linker_digest                                 host Wild against its comparator (#2761)
    LINK_TLS_PROFILE, validate_link_tls, link_tls_digest   whole-link TLS companion cells (#1915)
    SELFHOST_INLINE_PROFILE, validate_selfhost_inline,
        selfhost_inline_digest                             self-built inline-mode base/head pair (#58)
    REGRESSION_POLICIES, regression_policy                 report-only switch
    range_label                                            main baseline relation
    render                                                 readable report
"""

from __future__ import annotations

import json
import csv
import hashlib
import math
import os
import re
import shlex
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
# Supported automatic-main profiles are selected by the authenticated route.
# The historical PROFILE remains frozen; arbitrary sample overrides are absent.
MAIN40_PROFILE = dict(PROFILE, name="compiler-main-40pairs-v1", pairs=40, seed=20261003,
                      min_effect_percent=0.5, confidence=0.95, bootstrap_resamples=2000,
                      fresh_copy=True, order="ABBA")
MAIN_PROFILES = {PROFILE["name"]: PROFILE, MAIN40_PROFILE["name"]: MAIN40_PROFILE}


def named_main_profile(token: object) -> dict:
    """Return only a named fixed MAIN recipe; never derive authority from a receipt."""
    if not isinstance(token, str) or token not in MAIN_PROFILES:
        raise ValueError("unsupported trusted main profile")
    return MAIN_PROFILES[token]


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
# Admission floor for THROUGHPUT_PROFILE's base-revision harness (#3714). Before
# this commit, `bench_throughput run --mode all` also selected the retired none
# and mir-stack allocators, so a base without it runs 24 cells at alpha
# 0.01/48 and can never satisfy the 12-cell v2 population. compiler_compare
# rejects such a base before any build or lab, as an unsupported historical
# harness; this is a necessary condition only, and classify_throughput stays
# the full check of the exported corpus.
THROUGHPUT_HARNESS_FLOOR = "293132e418cb2449b36e3b92ae38aaa487813e43"
# The pull route's three corpus dispositions, decided by hosted authorization
# before dispatch and re-derived by compiler_compare on the host (#3714).
HARNESS_ADMISSIONS = ("admitted", "historical", "unsupported")
# The named historical corpus recipe (#3714), frozen per name like PROFILE. A
# pull request whose merge base predates THROUGHPUT_HARNESS_FLOOR selects it
# with one fresh exact request line; it is refused for any other base. The
# corpus then runs from tools/throughput at the pinned main revision, built by
# that revision's own `./build.sh bench_throughput` in a detached worktree under --work,
# on the same two binaries and with the v2 arguments and FAST/QUALITY
# population, so classify_throughput applies unchanged. The pin is the base of
# main's c6201a26 comparison, whose v2 corpus the current consumer accepted on
# the 9700X (run 38227553632). Changing any value needs a new name.
THROUGHPUT_HISTORICAL_REQUEST_LINE = "profile: throughput-corpus-historical-v1"
THROUGHPUT_HISTORICAL_HARNESS_REVISION = "afe67cbebcb257e7fe130a8eccdd5ff08a79b555"
THROUGHPUT_HISTORICAL_PROFILE = dict(
    THROUGHPUT_PROFILE, name="throughput-corpus-historical-v1",
    harness=f"tools/throughput at pinned main revision {THROUGHPUT_HISTORICAL_HARNESS_REVISION}, built and run by that "
            "revision's ./build.sh bench_throughput in a detached worktree under the work directory",
    harness_revision=THROUGHPUT_HISTORICAL_HARNESS_REVISION,
    admission=f"pull request merge base predates {THROUGHPUT_HARNESS_FLOOR}")
# Sealed raw evidence v1 (#2918, docs/compiler-evidence-v1.md). The producer
# exports these members as required, writes EVIDENCE_OMISSIONS, and seals the
# bundle last with the trusted `./build.sh compiler_evidence seal`; the
# publisher re-derives the same inventory natively with `verify`. The native
# tables in tools/compiler_evidence.c must stay equal (compiler_test checks).
EVIDENCE_INVENTORY = "buster-compiler-ordinary-raw-v1"
# The ordinary pull snapshot-v1 route (#3211) adds its closure records and its exact
# owned-phase population; the identity's EVIDENCE_ROUTE_KEYS lines select it.
EVIDENCE_SNAPSHOT_INVENTORY = "buster-compiler-snapshot-raw-v1"
EVIDENCE_CLOSURE_MEMBERS = tuple(f"closure-{operation}.json{suffix}" for operation in ("snapshot", "restore", "verify")
                                 for suffix in ("", ".manifest.tsv"))
EVIDENCE_PHASE_SUFFIXES = ("", ".argv", ".stdout", ".stderr", ".bootstrap.complete")
EVIDENCE_MANIFEST = "evidence-manifest.tsv"
EVIDENCE_SEAL = "evidence-seal.txt"
EVIDENCE_OMISSIONS = "evidence-omissions.json"
EVIDENCE_OMISSIONS_SCHEMA = "buster-compiler-evidence-omissions-v1"
EVIDENCE_OMISSION_REASONS = ("excluded-type", "member-limit", "byte-budget", "file-budget", "changed-during-copy",
                             "read-error")
EVIDENCE_OMISSION_EXAMPLES = 50
EVIDENCE_LAB_FIXED = ("compare.json", "summary.json", "pairs.json", "a/lab.json", "b/lab.json", "a/env/env.json")
EVIDENCE_LAB_LOGS = ("commands.log", "wrapper-rss.log", "source-run.log", "metrics-probe.log", "warmup-0.log")
EVIDENCE_CORPUS_CORE = ("samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl",
                        "capabilities.jsonl", "complete.txt", "summary.json", "cpuinfo.txt")
# Identity lines after schema and inventory, in manifest order.
EVIDENCE_IDENTITY_KEYS = ("repository", "request_run", "request_attempt", "executor_run", "executor_attempt",
                          "trusted_revision", "baseline_revision", "candidate_revision", "baseline_tree",
                          "candidate_tree", "lab_profile", "corpus_profile", "corpus_harness_revision")
# Trailing identity lines of a snapshot-v1 attempt only; a legacy-rebuild identity has none.
EVIDENCE_ROUTE_KEYS = ("preparation_policy", "owned_phases")


def evidence_lab_required(pairs: int, counters: tuple[bool, bool], metrics: tuple[bool, bool]) -> tuple[str, ...]:
    """Lab-relative required members for a plan of `pairs` and each variant's declared streams."""
    required = [*EVIDENCE_LAB_FIXED, *(f"{variant}/{log}" for variant in "ab" for log in EVIDENCE_LAB_LOGS)]
    for index, variant in enumerate("ab"):
        for pair in range(1, pairs + 1):
            required.extend(f"pairs/{pair:04d}-{variant}{suffix}" for suffix, declared in
                            ((".csv", counters[index]), (".ccmetrics", metrics[index])) if declared)
    return tuple(required)


def evidence_corpus_required(profile: dict) -> tuple[str, ...]:
    """Corpus-relative required members of an admitted corpus profile."""
    return (*EVIDENCE_CORPUS_CORE, *(f"inputs/{name}.c" for name in profile["workloads"]))


def evidence_preparation(receipt: dict) -> str:
    """The receipt's declared preparation policy, as validate_closure reads it, or '' when unsupported."""
    declared = receipt.get("preparation_policy", "legacy-rebuild" if receipt.get("closure") is None else "snapshot-v1") \
        if isinstance(receipt, dict) else None
    return declared if declared in ("legacy-rebuild", "snapshot-v1") else ""


def evidence_inventory(receipt: dict) -> str:
    """The sealed inventory a receipt's route selects (#2918, #3211)."""
    return EVIDENCE_SNAPSHOT_INVENTORY if evidence_preparation(receipt) == "snapshot-v1" else EVIDENCE_INVENTORY


def evidence_identity(receipt: dict, request_attempt: str) -> str:
    """The trusted identity lines of a receipt's ordinary attempt, or '' when it has none.

    A snapshot-v1 attempt appends its route and its owned-phase count, which select the snapshot
    inventory and its exact phase population; validate_closure binds that declared policy to the
    receipt's closure records and the retained closure manifests.
    """
    identity = receipt.get("identity") if isinstance(receipt, dict) else None
    profile, corpus = (receipt.get("profile"), receipt.get("throughput_profile")) if isinstance(receipt, dict) else (None, None)
    text = ""
    if isinstance(identity, dict) and isinstance(profile, dict) and isinstance(corpus, dict):
        harness = corpus.get("harness_revision") if corpus.get("name") == THROUGHPUT_HISTORICAL_PROFILE["name"] \
            else identity.get("base")
        values = (identity.get("repository"), identity.get("request_run_id"), request_attempt, identity.get("run_id"),
                  identity.get("run_attempt"), identity.get("trusted_revision"), identity.get("base"),
                  identity.get("head"), identity.get("base_tree"), identity.get("head_tree"), profile.get("name"),
                  corpus.get("name"), harness)
        if all(isinstance(value, str) and value and "\t" not in value and "\n" not in value for value in values):
            text = "".join(f"{key}\t{value}\n" for key, value in zip(EVIDENCE_IDENTITY_KEYS, values))
        policy = evidence_preparation(receipt)
        population = receipt.get("phase_ownership")
        count = population.get("count") if isinstance(population, dict) else None
        if policy == "snapshot-v1" and type(count) is int and 1 <= count <= 256:
            text += f"preparation_policy\tsnapshot-v1\nowned_phases\t{count}\n" if text else ""
        elif policy != "legacy-rebuild":
            text = ""
    return text


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
# Issue #3397: the -fc-ast-pilot adoption budget of docs/agents/frontend/ast.md
# (Measurement plan, stage 1) on the pull-compare route. One fresh exact line
# in ANALYZER_REQUEST_PATH selects it; the flag pair, cells, pair and warm-up
# counts are frozen here and nothing in the request can change them. Changing
# any value needs a new name.
AST_PILOT_ACCEPTANCE_REQUEST_LINE = "profile: ast-pilot-acceptance-v1"
AST_PILOT_ACCEPTANCE_SCHEMA = "buster-ast-pilot-acceptance-v1"
AST_PILOT_FLAG = "-fc-ast-pilot"
AST_PILOT_COMPILE = ["cc", "-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1",
                     "-DBUSTER_INCLUDE_TESTS=0", "-g0", "src/buster/apps/ide/ide.c"]
AST_PILOT_CELLS = {"syntax-only": "-fsyntax-only", "object": "-c"}
AST_PILOT_ACCEPTANCE_PROFILE = {
    "name": "ast-pilot-acceptance-v1",
    "source": "candidate-HEAD src/buster/apps/ide/ide.c with that build's generated headers, -g0, no link",
    "compiler": "the one frozen candidate-HEAD tests-off Clang Release ide for both variants",
    "compiler_modes": {"a": "default; -fc-ast-pilot omitted", "b": "-fc-ast-pilot"},
    "cells": {"syntax-only": "-fsyntax-only", "object": "-c"},
    "pairs": 20,
    "warmups": 1,
    "pairing": "ABBA",
    "fresh_copy": True,
    "cpu": 2,
    "measurements": ["wall", "instructions:u", "peak RSS", "c_ast.retained_bytes of an untimed -v syntax-only run of B"],
    "correctness": "byte-identical -c objects, and identical stdout/stderr in every run of both cells",
    "budget": "docs/agents/frontend/ast.md Measurement plan, stage-1 adoption",
    "report_only": True,
}
# The ast.md adoption budget as numbers. syntax-only: B/A at most 1 - gain in
# retired instructions and in the median paired wall ratio, whose 95% CI must
# also exclude 1; object: wall not detectably slower (CI low bound at most 1);
# every cell: B's median peak RSS above A's by at most retained bytes times
# (1 + rss_slack).
AST_PILOT_BUDGET = {"syntax_only_min_gain": 0.01, "rss_slack": 0.05}

# #4058 final default/default migration evidence. The ordinary lab/corpus and
# historical flag-pair profiles remain immutable. This fixed extra leg compares
# both prepared binaries on the same current-main source/header population.
AST_RETIREMENT_REQUEST_LINE = "profile: ast-retirement-acceptance-v1"
AST_RETIREMENT_SCHEMA = "buster-ast-retirement-acceptance-v1"
AST_RETIREMENT_CORPUS = tuple(THROUGHPUT_PROFILE["workloads"])
AST_RETIREMENT_INPUT_DIRECTORY = "work/ast-retirement-inputs"
AST_RETIREMENT_CELLS = {
    "unity-syntax-only": {"workload": "unity", "operation": "syntax-only", "allocator": None},
    "unity-object": {"workload": "unity", "operation": "object", "allocator": None},
    **{f"{name}-syntax-only": {"workload": name, "operation": "syntax-only", "allocator": None}
       for name in AST_RETIREMENT_CORPUS},
    **{f"{name}-object-{mode}": {"workload": name, "operation": "object", "allocator": mode}
       for name in AST_RETIREMENT_CORPUS for mode in THROUGHPUT_PROFILE["modes"]},
}
AST_RETIREMENT_PROFILE = {
    "name": "ast-retirement-acceptance-v1",
    "source": "the baseline/current-main tree and generated headers plus its six unchanged throughput CI sources",
    "compilers": "prepared tests-off Clang Release baseline and candidate ide binaries, default/default",
    "cells": AST_RETIREMENT_CELLS,
    "pairs": 20,
    "warmups": 1,
    "pairing": "ABBA",
    "fresh_copy": True,
    "cpu": 2,
    "phase_metrics": False,
    "measurements": ["wall with 95% CI", "instructions:u", "wait4 peak RSS",
                     "untimed C_AST and C_AST_FACTS retained bytes; separate symbol-binding bytes when reported"],
    "correctness": "byte-identical object references and diagnostics; deterministic output in every timed run",
    "report_only": True,
}


def ast_retirement_cell_command(cell: str) -> list[str]:
    """One fixed command, without the binary or its output path."""
    record = AST_RETIREMENT_CELLS[cell]
    if record["workload"] == "unity":
        command = list(AST_PILOT_COMPILE)
    else:
        command = ["cc", "-g0", "-O0", f"{AST_RETIREMENT_INPUT_DIRECTORY}/{record['workload']}.c"]
    command.append("-fsyntax-only" if record["operation"] == "syntax-only" else "-c")
    if record["allocator"] is not None:
        command.append(f"-fregister-allocator={record['allocator']}")
    return command
# Issue #3011: the opt-in constant-reuse arms on the pull-compare route. One
# fresh exact line in ANALYZER_REQUEST_PATH selects it. Each cell times the one
# frozen candidate binary on the same -g0 unity object compile, A default and
# B passing that cell's flag; the flags, cells, pair and warm-up counts are
# frozen here and nothing in the request can change them. B's object differs
# from A's by design; diagnostics must not. It is report-only.
CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE = "profile: constant-reuse-acceptance-v1"
CONSTANT_REUSE_ACCEPTANCE_SCHEMA = "buster-constant-reuse-acceptance-v1"
CONSTANT_REUSE_COMPILE = AST_PILOT_COMPILE + ["-c"]
CONSTANT_REUSE_CELLS = {"intern": "-fcanonical-intern-constants", "cse": "-fcanonical-fast-cse"}
CONSTANT_REUSE_ACCEPTANCE_PROFILE = {
    "name": "constant-reuse-acceptance-v1",
    "source": "candidate-HEAD src/buster/apps/ide/ide.c with that build's generated headers, -g0, -c, no link",
    "compiler": "the one frozen candidate-HEAD tests-off Clang Release ide for both variants",
    "compiler_modes": {"a": "default; the cell's flag omitted", "b": "the cell's one opt-in flag"},
    "cells": dict(CONSTANT_REUSE_CELLS),
    "pairs": 20,
    "warmups": 1,
    "pairing": "ABBA",
    "fresh_copy": True,
    "cpu": 2,
    "measurements": ["wall", "instructions:u", "peak RSS", "executable-section code bytes"],
    "correctness": "identical stdout/stderr in every run of both cells; B's object differs from A's by design",
    "report_only": True,
}
# Issue #1125: the per-process floor of `ide cc` on the pull-compare route. One
# fresh exact line in ANALYZER_REQUEST_PATH selects it. Each cell times the one
# frozen candidate binary in fresh processes on PROCESS_FLOOR_SOURCE, alone in
# its directory: A is the native x86-64 object compile and B is the cell's
# control, the same command (A/A calibration), the AArch64 object compile or
# -fsyntax-only. The source, commands, pair and warm-up counts are frozen here
# and nothing in the request can change them. It is report-only.
PROCESS_FLOOR_REQUEST_LINE = "profile: process-floor-v1"
PROCESS_FLOOR_SCHEMA = "buster-process-floor-v1"
PROCESS_FLOOR_SOURCE = "int main(void){return 0;}\n"
PROCESS_FLOOR_SOURCE_NAME = "tiny.c"
PROCESS_FLOOR_OBJECT = ["cc", "-g0", "-c", PROCESS_FLOOR_SOURCE_NAME]
# B's compile per cell; A is always PROCESS_FLOOR_OBJECT. An object compile
# also passes `-o OUT`.
PROCESS_FLOOR_CELLS = {
    "aa": list(PROCESS_FLOOR_OBJECT),
    "aarch64": ["cc", "-g0", "-target", "aarch64-unknown-linux", "-c", PROCESS_FLOOR_SOURCE_NAME],
    "syntax-only": ["cc", "-g0", "-fsyntax-only", PROCESS_FLOOR_SOURCE_NAME],
}
PROCESS_FLOOR_PROFILE = {
    "name": "process-floor-v1",
    "source": "int main(void){return 0;} as tiny.c, alone in the working directory",
    "source_sha256": hashlib.sha256(PROCESS_FLOOR_SOURCE.encode("utf-8")).hexdigest(),
    "compiler": "the one frozen candidate-HEAD tests-off Clang Release ide for both variants",
    "compiler_modes": {"a": " ".join(PROCESS_FLOOR_OBJECT + ["-o", "OUT"]), "b": "the cell's command"},
    "cells": {cell: " ".join(argv + (["-o", "OUT"] if "-c" in argv else [])) for cell, argv in PROCESS_FLOOR_CELLS.items()},
    "pairs": 100,
    "warmups": 2,
    "pairing": "ABBA",
    "fresh_copy": True,
    "cpu": 2,
    "measurements": ["wall", "task-clock", "compiler wall_ns", "instructions:u", "peak RSS"],
    "correctness": "byte-identical A/A objects, and identical stdout/stderr in every run of every cell",
    "report_only": True,
}
# Issue #2761: the Zen 5 route for real-source throughput. One fresh exact line
# in ANALYZER_REQUEST_PATH selects it on the pull-compare route. After the
# corpus leg, the base revision's `bench_throughput run --real-source` times
# each pinned version-2 descriptor's own compile_link_argv on the matched
# baseline/candidate binaries. Inputs, sampling and stopping are frozen here;
# changing any value needs a new name. It is report-only: the guard is off.
THROUGHPUT_REAL_SOURCE_REQUEST_LINE = "profile: throughput-real-source-v1"
THROUGHPUT_REAL_SOURCE_STAGING_SCHEMA = "buster-throughput-real-source-staging-v1"
THROUGHPUT_REAL_SOURCE_DECISION = "diagnostic (guard disabled)"
# Each staged root must hash to its descriptor's input_tree_sha256. A fetched
# file or archive is checked against its pinned SHA-256 before use; a copy
# comes from the base checkout. An archive contributes only the members under
# its prefix, with the prefix removed.
THROUGHPUT_REAL_SOURCE_INPUTS = {
    "cjson-1.7.19-native": {
        "descriptor": "tools/throughput/workloads/cjson-1.7.19.workload",
        "descriptor_sha256": "0bd68a027a8c30d95eeddae7907f3278dfd265084db515424944262bfb878685",
        "input_tree_sha256": "38f1730a865924c9b1918c7483c8abf6a7a0467ac465a34412f7630b713d1a52",
        "requested_translation_unit_bytes": 121771,
        "files": [{"path": name, "sha256": digest,
                   "url": "https://raw.githubusercontent.com/DaveGamble/cJSON/c859b25da02955fef659d658b8f324b5cde87be3/" + name}
                  for name, digest in (
                      ("cJSON.c", "298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d"),
                      ("cJSON.h", "25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee"),
                      ("cJSON_Utils.c", "bf3092696763a9a446c39365d7c35f23e5acfdca1bd1da33ef66c1161f9243e9"),
                      ("cJSON_Utils.h", "1050a7cce8ffe352c509e0c1faad505b9b8a09cac3a1c45c544447868e05f3b5"))],
        "copies": [{"path": "generated/roundtrip.c", "from": "tests/basic_cjson_roundtrip.c",
                    "sha256": "1f528863c902c039ae3d1a9391eb5a0478d5fa60bd123b8e170a9e981c852da8"}],
        "archive": None,
    },
    "lua-5.4.8-native": {
        "descriptor": "tools/throughput/workloads/lua-5.4.8.workload",
        "descriptor_sha256": "97d9674fa41f68a3b948ef8bbf9a8e250c95e619bdf5ec1d751a31833bcc7e82",
        "input_tree_sha256": "ad56eca3663473ae288f0650e8da4361a951bc68985881e1168ae619b4ef6999",
        "requested_translation_unit_bytes": 717585,
        "files": [],
        "copies": [],
        "archive": {"url": "https://www.lua.org/ftp/lua-5.4.8.tar.gz", "format": "tar.gz", "prefix": "lua-5.4.8/src/",
                    "sha256": "4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae"},
    },
    "sqlite-3.53.4-amalgamation-native": {
        "descriptor": "tools/throughput/workloads/sqlite-3.53.4.workload",
        "descriptor_sha256": "cf4c5d38e88f61aaf1530c65c2c203d17164e8c9fe2a570001112832bfe65e24",
        "input_tree_sha256": "25607ba4393af6bd955ae63b22061974e2cacbc96dfde146878e3d41fdc3ad8b",
        "requested_translation_unit_bytes": 10701256,
        "files": [],
        "copies": [],
        "archive": {"url": "https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip", "format": "zip",
                    "prefix": "sqlite-amalgamation-3530400/",
                    "sha256": "1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d"},
    },
}
THROUGHPUT_REAL_SOURCE_PROFILE = {
    "name": "throughput-real-source-v1",
    "workload": "bench_throughput run --real-source: each pinned version-2 descriptor's frozen compile_link_argv "
                "(source to linked executable) on its verified staged tree, under both FAST/QUALITY modes",
    "harness": "tools/throughput at the base revision, built and run by ./build.sh bench_throughput",
    "inputs": THROUGHPUT_REAL_SOURCE_INPUTS,
    "arguments": ["--no-guard", "--mode", "all", "--pairs", "20", "--warmups", "2", "--timeout", "120", "--cpu", "2"],
    "modes": ["fast", "quality"],
    "pairs_per_round": 20,
    "rounds": 2,
    "warmups": 2,
    "cpu": 2,
    "seed": 20260907,
    "sampling": "per descriptor and mode: two warmups per variant, then two fixed rounds of 20 adjacent A/B pairs; "
                "ABBA blocks and job order follow the recorded seed; every sample is retained",
    "stopping": "fixed: every pair of both rounds runs; no early stop, rerun, trimming or best-of-N",
    "decision": "report-only: the guard is off; paired medians and per-round sign tests are reported and never gate",
    "report_only": True,
}
# Issue #2761: the Zen 5 route for PGO/LTO production-profile admission. One
# fresh exact line in ANALYZER_REQUEST_PATH selects it on the pull-compare
# route. The candidate head's own `./build.sh production_profile` builds its
# trees, trains both profiles, inspects and tests the PGO+LTO compiler in an
# isolated clone of the head, without its own hosted-style benchmark; then the
# base revision's `bench_throughput run` times that run's Release compiler
# against its PGO+LTO production compiler with THROUGHPUT_PROFILE's arguments.
# Changing any value needs a new name. It is report-only like the corpus.
PRODUCTION_PROFILE_REQUEST_LINE = "profile: production-profile-v1"
PRODUCTION_PROFILE_LEDGER = "BUSTER_PGO_PHASES_V1"
# The frozen --no-benchmark phase population in ledger slot order, as
# (slot, label, kind, variant): production_profile_phase_expected's three
# toolchain probes, seven trees x (configure, build), two trainings x (train,
# merge), inspect and test. Copied from a real producer ledger.
PRODUCTION_PROFILE_LEDGER_PHASES = (
    (1, "clang-version", "toolchain", "-"), (2, "clang-target", "toolchain", "-"),
    (3, "llvm-profdata-version", "toolchain", "-"),
    (4, "generate-release", "configure", "release"), (5, "build-release", "build", "release"),
    (6, "generate-g0", "configure", "g0"), (7, "build-g0", "build", "g0"),
    (8, "generate-lto", "configure", "lto"), (9, "build-lto", "build", "lto"),
    (10, "generate-instrumented", "configure", "instrumented"), (11, "build-instrumented", "build", "instrumented"),
    (12, "generate-instrumented-lto", "configure", "instrumented-lto"),
    (13, "build-instrumented-lto", "build", "instrumented-lto"),
    (14, "train-no-lto", "train", "instrumented"), (15, "merge-profile-no-lto", "merge", "instrumented"),
    (16, "train-lto", "train", "instrumented-lto"), (17, "merge-profile-lto", "merge", "instrumented-lto"),
    (18, "generate-pgo", "configure", "pgo"), (19, "build-pgo", "build", "pgo"),
    (20, "generate-pgo-lto", "configure", "pgo-lto"), (21, "build-pgo-lto", "build", "pgo-lto"),
    (22, "inspect-production-sections", "inspect", "pgo-lto"), (23, "test-production", "test", "pgo-lto"),
)
PRODUCTION_PROFILE_PHASES = len(PRODUCTION_PROFILE_LEDGER_PHASES)
PRODUCTION_PROFILE_PROFILE = {
    "name": "production-profile-v1",
    "build": "candidate-HEAD ./build.sh production_profile --no-benchmark in an isolated clone of the head: seven "
             "Release trees, ThinLTO and non-LTO trainings on the native corpus, debug-section inspection and "
             "test_all on the PGO+LTO production compiler",
    "build_arguments": ["--no-benchmark", "--jobs", "16", "--build-lanes", "4"],
    "compilers": {"baseline": "release", "candidate": "pgo-lto"},
    "workload": THROUGHPUT_PROFILE["workload"],
    "harness": THROUGHPUT_PROFILE["harness"],
    # The production compiler must emit the Release compiler's exact objects.
    "arguments": [*THROUGHPUT_PROFILE["arguments"], "--require-identical-output"],
    "decision": "report-only: the corpus decisions are reported and never gate",
    "report_only": True,
}
# Issue #2761: the Zen 5 route for targeted register-allocator and scheduler
# claims. tools/fast_ra_scaling.c and tools/machine_line_mark_scaling.c include
# compiler internals, so they cannot be direct workloads. One fresh exact line
# in ANALYZER_REQUEST_PATH selects this on the pull-compare route:
# tools/bench_direct/compiler_probes.c (native, from the trusted checkout) copies the head's probe sources outside
# both checkouts (the audits' practice), builds each probe with the command its
# audit records against the merge base's and the head's own src/ in isolated
# clones, then times both builds as fixed alternating pairs on the pinned CPU.
# The FAST probe's audit command predates #2876, whose machine.c needs one
# codegen.c helper; codegen.c is the only source added. Changing any value
# needs a new name. It is report-only like the corpus.
COMPILER_PROBES_REQUEST_LINE = "profile: compiler-probes-v1"
COMPILER_PROBES_SCHEMA = "buster-compiler-probes-v1"
COMPILER_PROBES_ROLES = ("baseline", "candidate")
# A timed sample cannot exceed the harness's per-invocation timeout; bounding it here keeps
# every median and ratio finite before any float arithmetic.
COMPILER_PROBES_INVOCATION_TIMEOUT_SECONDS = 60
COMPILER_PROBES_SAMPLE_LIMIT_NS = COMPILER_PROBES_INVOCATION_TIMEOUT_SECONDS * 1_000_000_000
COMPILER_PROBES_HARNESS = "compiler_probes.c"
COMPILER_PROBES_HARNESS_FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fwrapv",
                                 "-fno-strict-aliasing", "-funsigned-char"]
_PROBE_DEFINES = ["-DBUSTER_INCLUDE_TESTS=0", "-DBUSTER_LINK_LIBC=1", "-DBUSTER_OPTIMIZE=1", "-DBUSTER_SINGLE_THREADED=1"]
_PROBE_LANGUAGE = ["-Isrc", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char"]
_PROBE_LIBRARY = ["src/buster/lib/arena.c", "src/buster/lib/os.c", "src/buster/lib/integer.c", "src/buster/lib/string.c",
                  "src/buster/lib/float.c", "src/buster/lib/time.c"]
_PROBE_LINK = ["-Wl,--gc-sections", "-lpthread", "-ldl", "-lm"]
_DECIMAL = "[0-9]+"
COMPILER_PROBES_PROFILE = {
    "name": "compiler-probes-v1",
    "build": "the candidate head's probe source, copied outside both checkouts, compiled by clang against the merge "
             "base's and the head's own src/ in isolated shared clones; one binary per probe and revision",
    "compiler": "clang",
    "harness": "tools/bench_direct/compiler_probes.c from the trusted checkout, compiled by clang with "
               "COMPILER_PROBES_HARNESS_FLAGS; its --profile output equals this profile",
    "probes": {
        "fast_ra": {
            "source": "tools/fast_ra_scaling.c",
            "audit": "docs/performance-audits/2026-09-07T222944Z.md",
            "compile": ["-O2", "-g", "-ffunction-sections", "-fdata-sections", *_PROBE_DEFINES, *_PROBE_LANGUAGE],
            "sources": ["src/buster/lib/compiler/codegen/machine.c", "src/buster/lib/compiler/codegen/codegen.c",
                        *_PROBE_LIBRARY],
            "link": _PROBE_LINK,
            "metric": "total_ns",
            # Every non-_ns field a sample line prints, each matching its pattern exactly.
            "check_fields": {key: _DECIMAL for key in ("blocks", "prepass_bytes", "total_bytes", "spills", "reloads",
                                                       "copies", "edits", "frame")},
            "cases": [{"name": f"{shape}-{blocks}", "arguments": [str(blocks), "11", shape],
                       "markers": {"blocks": str(blocks)}}
                      for shape in ("chain", "fanout") for blocks in (256, 1024, 4096, 16384)],
        },
        "line_mark": {
            "source": "tools/machine_line_mark_scaling.c",
            "audit": "docs/performance-audits/2026-09-18T224655Z.md",
            "compile": ["-O2", "-g", "-ffunction-sections", "-fdata-sections", "-Wall", "-Werror",
                        "-Wno-unused-function", "-Wno-unused-variable", *_PROBE_DEFINES,
                        "-DBUSTER_LINE_MARK_HELPER_BENCH=0", *_PROBE_LANGUAGE],
            "sources": ["src/buster/lib/compiler/codegen/machine.c", *_PROBE_LIBRARY],
            "link": _PROBE_LINK,
            "metric": "schedule_ns",
            "check_fields": {"mode": "schedule", "leaves": _DECIMAL, "rows": _DECIMAL, "batch": _DECIMAL,
                             "retained_bytes": _DECIMAL, "permuted": _DECIMAL, "displacement": _DECIMAL,
                             "hash": "[0-9a-f]{16}"},
            # The balanced reduction has leaves MOV_RI, leaves - 1 ADD64, one MOV_RR and RET.
            "cases": [{"name": f"schedule-{leaves}", "arguments": ["schedule", str(leaves), "11", "1"],
                       "markers": {"mode": "schedule", "leaves": str(leaves), "rows": str(2 * leaves + 1), "batch": "1"}}
                      for leaves in (1024, 4096, 16384, 65536)],
        },
    },
    # Each invocation prints one line per sample after its own untimed warmup.
    "repeats": 11,
    "warmups": 2,
    "pairs": 30,
    "order": "warmups alternate baseline, candidate; even pairs run baseline first, odd pairs candidate first",
    "stopping": "fixed: exactly warmups + pairs invocations per binary and case, no early stop or extension",
    "cpu": 2,
    "statistic": "per invocation the median of its samples; per pair candidate/baseline; per case the median pair "
                 "ratio with a distribution-free 95% order-statistic interval",
    "decision": "report-only: ratios and intervals are reported and never gate",
    "report_only": True,
}
# Issue #2761: the Zen 5 route for frontend and selector claims. One fresh
# exact line in ANALYZER_REQUEST_PATH selects it on the pull-compare route.
# After the corpus leg, the base revision's tools/benchmark_frontend_scaling.py
# and tools/selection_benchmark.py run on the matched baseline/candidate
# binaries, then the harness itself runs `ide bench` in alternating fresh
# processes of both binaries from the base checkout. Arguments, populations and
# the pinned CPU are frozen here; changing any value needs a new name. It is
# report-only: every timing is reported and never gates, but each tool's own
# correctness check (identical objects, identical work counts) must hold.
FRONTEND_SELECTOR_REQUEST_LINE = "profile: frontend-selector-v1"
FRONTEND_SELECTOR_SCHEMA = "buster-frontend-selector-v1"
# benchmark_frontend_scaling.py --suite all: (kind, counts, modes) in run order.
FRONTEND_SCALING_CASES = (
    ("small", (1,), ("syntax", "object")),
    *((order, (count,), ("syntax", "object")) for count in (8192, 16384, 32768)
      for order in ("positional", "ascending", "descending", "range")),
    ("scopes", (512, 1024, 2048, 4096, 8192), ("syntax",)),
    ("deep-scopes", (16, 64, 256), ("syntax",)),
    ("fresh-bindings", (0, 1, 4, 16, 64, 256, 1024, 4096, 16384, 65536), ("syntax", "object")),
)
SELECTION_WORK_FIELDS = ("functions", "fallback_functions", "ir_instructions", "mir_instructions")
# Every count is captured as 1 to 19 digits, so int() never sees a token past Python's
# integer-string conversion limit; a longer token simply leaves the line unparsed.
IDE_BENCH = re.compile(r"BENCH_C_FRONTEND path=(\S+) iterations=([0-9]{1,19}) bytes=([0-9]{1,19}) "
                       r"min_ns=([0-9]{1,19}) median_ns=([0-9]{1,19}) throughput_mb_s=([0-9]{1,19})")
FRONTEND_SELECTOR_PROFILE = {
    "name": "frontend-selector-v1",
    "harness": "tools/benchmark_frontend_scaling.py and tools/selection_benchmark.py at the base revision; "
               "`ide bench` driven by compiler_compare from the base checkout",
    "cpu": 2,
    "frontend_scaling": {
        "workload": "--suite all: generated pointer tables, scopes, deep scopes and fresh bindings; "
                    "-fsyntax-only and -c cells, fresh process per sample",
        "arguments": ["--suite", "all", "--warmups", "1", "--repetitions", "11", "--cpu", "2"],
        "suite": "all", "warmups": 1, "repetitions": 11,
        "correctness": "byte-identical -c objects from both binaries; unchanged sources and binaries",
    },
    "selection": {
        "workload": "the tool's three generated workloads: bench-select replay plus fresh-process -O2 -c "
                    "under both register allocators, programs executed once and failure paths checked",
        "arguments": ["--samples", "15", "--allocators", "fast", "quality", "--check", "--cpu", "2",
                      "--timeout", "120", "--build-description", "frontend-selector-v1"],
        "samples": 15, "allocators": ["fast", "quality"],
        "workloads": ["many-small.c", "one-large.c", "abi-and-memory.c"],
        "correctness": "identical target and selected work counts, zero fallback functions and identical object "
                       "bytes per allocator across every sample of both binaries",
    },
    "ide_bench": {
        "workload": "`ide bench`: preprocess, parse and analyze tests/basic_c_operations.c of the base checkout, "
                    "one warm-up and 30 timed in-process iterations per run",
        "source": "tests/basic_c_operations.c", "iterations": 30, "pairs": 20,
        "pairing": "fresh process per run; pair p runs baseline first when p is even, candidate first when odd",
        "correctness": "every run exits 0 with exactly one BENCH_C_FRONTEND line for the same source and bytes",
    },
    "decision": "report-only: medians are reported and never gate",
    "report_only": True,
}
# Issue #2676: the Zen 5 route for deep-nesting claims. One fresh exact line in
# ANALYZER_REQUEST_PATH selects it on the pull-compare route. After the corpus
# leg, the base revision's tools/benchmark_frontend_scaling.py --suite nesting
# runs on the matched baseline/candidate binaries: one function per family whose
# control statements nest 2,000, 8,000 and 32,000 levels deep, so each step is
# four times the last and a term growing with the square of the depth shows as
# a sixteenfold step. Families, depths, populations and the pinned CPU are
# frozen here; changing any value needs a new name. It is report-only, but the
# tool's own check (byte-identical -c objects) must hold.
FRONTEND_NESTING_REQUEST_LINE = "profile: frontend-nesting-v1"
FRONTEND_NESTING_FAMILIES = ("braced-while", "braced-for", "braced-if", "braced-switch", "braceless-while",
                             "braceless-for", "braceless-do", "braceless-switch", "declaration", "label", "goto")
FRONTEND_NESTING_DEPTHS = (2000, 8000, 32000)
# benchmark_frontend_scaling.py --suite nesting: (kind, counts, modes) in run order.
FRONTEND_NESTING_CASES = (
    ("small", (1,), ("syntax", "object")),
    *((family, FRONTEND_NESTING_DEPTHS, ("syntax", "object")) for family in FRONTEND_NESTING_FAMILIES),
)
FRONTEND_NESTING_PROFILE = {
    "name": "frontend-nesting-v1",
    "harness": "tools/benchmark_frontend_scaling.py at the base revision",
    "cpu": 2,
    "frontend_scaling": {
        "workload": "--suite nesting: per family one function whose control statements nest each depth deep; "
                    "-fsyntax-only and -c cells, fresh process per sample",
        "arguments": ["--suite", "nesting", "--warmups", "1", "--repetitions", "11", "--cpu", "2"],
        "suite": "nesting", "warmups": 1, "repetitions": 11,
        "families": list(FRONTEND_NESTING_FAMILIES), "depths": list(FRONTEND_NESTING_DEPTHS),
        "correctness": "byte-identical -c objects from both binaries; unchanged sources and binaries",
    },
    "decision": "report-only: medians are reported and never gate",
    "report_only": True,
}
# Issue #2761: the Zen 5 route for tool:wild_linker_bench.c (#2645). One fresh
# exact line in ANALYZER_REQUEST_PATH selects it on the pull-compare route. The
# head's own ide objects are built by clang without LTO in an isolated clone of
# the head, in the audit's unity and separate trees; the trusted revision's
# tools/wild_linker_acceptance.c --link-only then times the host's installed
# Wild against the audit's comparator, mold, on the audit's three direct
# link-only ide cells under its fixed link policy (2026-10-04T185601Z). Nothing
# is downloaded: both linkers are found with `command -v`; a host without mold
# compares against its default `ld` with that kind's own equivalent options.
# Cells, populations and CPUs are frozen here; changing any value needs a new
# name. It is report-only, but every link must succeed and repeat byte-identically.
WILD_LINKER_REQUEST_LINE = "profile: wild-linker-v1"
WILD_LINKER_SCHEMA = "buster-wild-linker-v1"
WILD_LINKER_BUILD_ID = "0x2645264526452645264526452645264526452645"
# kind -> its `command -v` name, how its --version output starts, its options
# matching Wild's --no-fork --icf=none, and its one-thread option. GNU ld neither
# forks nor folds identical code and links on one thread, so it adds none.
WILD_LINKER_COMPARATORS = {
    "mold": {"command": "mold", "version": "mold ", "policy": "--no-fork --icf=none", "threads1": "--threads=1"},
    "bfd": {"command": "ld", "version": "GNU ld ", "policy": "", "threads1": ""},
}
WILD_LINKER_CELLS = ["ide-debug", "ide-unity-release", "ide-separate-release"]
# What `wild --version` prints and what Wild writes into its output's .comment by default
# (Args::linker_identity at wild e9a9b0cb, libwild/src/args.rs). Its version is one of the forms
# wild/build.rs writes: the Cargo version at a release tag, the origin/main merge-base hash, that
# version and hash with "-modified", or that version with "non-git-build" (distribution builds,
# e.g. the 9700X host's "Wild 0.10.0 non-git-build"). Another linker installed as `wild` is
# refused by both checks.
WILD_IDENTITY = re.compile(r"Wild (?:[0-9][0-9A-Za-z.+-]*(?: (?:[0-9a-f]{40}-modified|non-git-build))?"
                           r"|[0-9a-f]{40}) \(compatible with GNU linkers\)\Z")
# Wild writes that identity into .comment behind this prefix (Layout's `format!("Linker: {}\0", ...)`,
# libwild/src/layout.rs at e9a9b0cb); run 38255545884 refused the host's genuine output for lacking it.
WILD_COMMENT_PREFIX = "Linker: "
WILD_LINKER_LINKERS = ("comparator", "wild")
# Every token of a retained Wild argv: inert inside double quotes and outside them, so the
# canonical quoted join is one simple command whatever the shell.
WILD_ARGV_TOKEN = re.compile(r"[A-Za-z0-9_./+=,:@%-]+\Z")
# The complete grammar of the arguments after the linker in Clang's printed argv for these
# cells: flags without operands, options with their admitted operands, system -L directories,
# plain -l names and object or archive operands. Anything else, notably a response file
# (`@file`, which Wild expands recursively, args.rs at e9a9b0cb) or an option that loads
# another file (-T, --script, --plugin, --just-symbols), is refused.
WILD_ARGV_FLAGS = frozenset({"--hash-style=gnu", "--hash-style=both", "--build-id", "--eh-frame-hdr", "-pie",
                             "-no-pie", "--as-needed", "--no-as-needed", "--push-state", "--pop-state"})
WILD_ARGV_OPERANDS = {"-z": frozenset({"relro", "now", "noexecstack"}), "-m": frozenset({"elf_x86_64"}),
                      "-dynamic-linker": None, "-o": None}
WILD_ARGV_LIBRARY = re.compile(r"-l[A-Za-z0-9_+.-]+\Z")
WILD_LINKER_TREE_ARGUMENTS = ["--cc", "clang", "--linker", "DEFAULT", "--no-include-tests", "--no-fuzz", "--no-sanitize",
                              "--no-lto", "--", "-DBUSTER_DEBUG_INFO=ON", "-DBUSTER_FRAME_POINTERS=ON"]
WILD_LINKER_PROFILE = {
    "name": "wild-linker-v1",
    "harness": "tools/wild_linker_acceptance.c --link-only at the trusted revision, built with clang -std=c11 -O2",
    "inputs": "the head's own ide objects and archives, clang without LTO, with debug information and frame "
              "pointers, built once in an isolated clone of the head; system startup files and libraries are the host's",
    "trees": {"build-wild-unity": [*WILD_LINKER_TREE_ARGUMENTS, "-DBUSTER_UNITY_BUILD=ON"],
              "build-wild-separate": [*WILD_LINKER_TREE_ARGUMENTS, "-DBUSTER_UNITY_BUILD=OFF"]},
    "builds": [["build-wild-unity", "Release"], ["build-wild-separate", "Debug"], ["build-wild-separate", "Release"]],
    "cells": WILD_LINKER_CELLS,
    "cell_trees": {"ide-debug": ["build-wild-separate", "Debug"], "ide-unity-release": ["build-wild-unity", "Release"],
                   "ide-separate-release": ["build-wild-separate", "Release"]},
    "wild": {"command": "wild", "policy": "--no-fork --icf=none", "threads1": "--threads=1",
             "identity": "Wild <version> (compatible with GNU linkers), from --version and its output's .comment"},
    "comparator": "mold, the audit's comparator; the host's default ld (GNU ld) only when mold is absent",
    "policy": f"Clang's printed Wild argv for each cell, identical for both linkers, plus --no-gc-sections and "
              f"--build-id={WILD_LINKER_BUILD_ID}; no LTO",
    "cpus": [2, 3, 4, 5],
    "seed": 2645, "warmups": 2, "pairs": 21, "control_pairs": 9, "bootstraps": 4001,
    "sampling": "per cell, 2 warmups then 21 seeded randomized AB/BA pairs at default threads and at one thread; "
                "9 A/A pairs per linker on ide-debug; fixed populations, no adaptive stopping",
    "correctness": "every link exits 0; each linker's output links again byte-identically and compiles a probe; "
                   "the object and archive manifest is unchanged at the end",
    "decision": "report-only: paired medians are reported and never gate",
    "report_only": True,
}
# Issue #1915: the Zen 5 route for whole-link `ide cc` timing of the x86-64 ELF
# TLS companion lookup (#2247). One fresh exact line in ANALYZER_REQUEST_PATH
# selects it on the pull-compare route. After the corpus leg the trusted
# revision's tools/link_tls_bench.py builds the frozen inputs below with the
# host's clang and ar, links every cell once with each of the matched
# baseline/candidate ide binaries, requires byte-identical images that print
# the frozen output, then times fresh `ide cc` link processes of both binaries
# as fixed alternating pairs on the pinned CPU. Inputs, cells and populations
# are frozen here; changing any value needs a new name. It is report-only.
LINK_TLS_REQUEST_LINE = "profile: link-tls-v1"
LINK_TLS_SCHEMA = "buster-link-tls-v1"
LINK_TLS_ROLES = ("baseline", "candidate")
LINK_TLS_HARNESS = "link_tls_bench.py"
# A timed sample cannot exceed the per-invocation timeout; bounding it here keeps
# every median and ratio finite before any float arithmetic.
LINK_TLS_INVOCATION_TIMEOUT_SECONDS = 60
LINK_TLS_SAMPLE_LIMIT_NS = LINK_TLS_INVOCATION_TIMEOUT_SECONDS * 1_000_000_000
_LINK_TLS_OBJECT = ["-O0", "-fPIC", "-g0", "-c"]


def _link_tls_sums(variables: int) -> str:
    """What a linked tls:N image prints: each variable i starts at i and both calls add every one, then increment it."""
    first = variables * (variables - 1) // 2
    return f"{first} {first + variables}\n"


LINK_TLS_PROFILE = {
    "name": "link-tls-v1",
    "issue": "#1915",
    "harness": "tools/link_tls_bench.py at the trusted revision, run by compiler_compare after the corpus leg on the "
               "matched baseline/candidate ide binaries",
    "compiler": "clang",
    # Source kinds the harness writes: main calls sum_all twice and prints both sums; tls:N defines N global
    # `__thread long` variables (v_i = i) read and incremented once each by sum_all, so two general-dynamic
    # TLS sites per variable; plain is the no-TLS control, 4096 globals and 4096 functions summed in a chain.
    "inputs": {
        "main.o": {"source": "main", "arguments": ["-O0", "-g0", "-c"]},
        "m1024.o": {"source": "tls:1024", "arguments": _LINK_TLS_OBJECT},
        "m4096.o": {"source": "tls:4096", "arguments": _LINK_TLS_OBJECT},
        "m16384.o": {"source": "tls:16384", "arguments": _LINK_TLS_OBJECT},
        "n16384.o": {"source": "tls:16384", "arguments": ["-fno-plt", *_LINK_TLS_OBJECT]},
        "m16384.a": {"archive": ["m16384.o"]},
        "plain.o": {"source": "plain", "arguments": _LINK_TLS_OBJECT},
    },
    "cells": [
        {"name": "tls-gd-2048", "arguments": ["cc", "-no-pie", "main.o", "m1024.o"], "stdout": _link_tls_sums(1024)},
        {"name": "tls-gd-8192", "arguments": ["cc", "-no-pie", "main.o", "m4096.o"], "stdout": _link_tls_sums(4096)},
        {"name": "tls-gd-32768", "arguments": ["cc", "-no-pie", "main.o", "m16384.o"], "stdout": _link_tls_sums(16384)},
        {"name": "tls-gd-32768-pie", "arguments": ["cc", "-pie", "main.o", "m16384.o"], "stdout": _link_tls_sums(16384)},
        {"name": "tls-gd-32768-no-plt", "arguments": ["cc", "-no-pie", "main.o", "n16384.o"],
         "stdout": _link_tls_sums(16384)},
        {"name": "tls-gd-32768-archive", "arguments": ["cc", "-no-pie", "main.o", "m16384.a"],
         "stdout": _link_tls_sums(16384)},
        {"name": "no-tls-control", "arguments": ["cc", "-no-pie", "main.o", "plain.o"], "stdout": "8386560 8386560\n"},
    ],
    "warmups": 2,
    "pairs": 20,
    "cpu": 2,
    "order": "warmups alternate baseline, candidate; even pairs run baseline first, odd pairs candidate first",
    "stopping": "fixed: exactly warmups + pairs invocations per binary and cell, no early stop or extension",
    "timing": "wall nanoseconds of one fresh `taskset -c CPU ide ARGUMENTS -o OUTPUT` process from spawn to wait, "
              "with its user plus system CPU; startup, argument parsing, input reads, linking and publication included",
    "correctness": "each binary links every cell once before timing; both images are byte-identical, exit 0 and "
                   "print the frozen output, and every timed link exits 0",
    "statistic": "per pair candidate/baseline wall time; per cell the median pair ratio with a distribution-free "
                 "95% order-statistic interval",
    "decision": "report-only: ratios and intervals are reported and never gate",
    "report_only": True,
}
# Issue #58: the Zen 5 route for comparing the compilers each side would
# self-build with -fcanonical-inline. One fresh exact line in
# ANALYZER_REQUEST_PATH selects it on the pull-compare route. After each role's
# Clang Release build, the trusted tools/bench_direct/selfhost_inline_compare.py
# compiles that role's own unity source once with stage1_command; after the
# ordinary comparison it times the two stage-1 compilers compiling the frozen
# merge-base tree with compile_flags, requiring byte-identical outputs, then
# runs the topdown (Zen 5 metric groups, including the front-end ones) and
# sampling profile steps for both. Everything is frozen here; changing any
# value needs a new name. It is report-only.
SELFHOST_INLINE_REQUEST_LINE = "profile: self-host-inline-compare-v1"
SELFHOST_INLINE_SCHEMA = "buster-selfhost-inline-compare-v1"
SELFHOST_INLINE_STAGE1_SCHEMA = "buster-selfhost-inline-stage1-v1"
SELFHOST_INLINE_HARNESS = "selfhost_inline_compare.py"
SELFHOST_INLINE_PROFILE = {
    "name": "self-host-inline-compare-v1",
    "issue": "#58",
    "harness": "tools/bench_direct/selfhost_inline_compare.py at the trusted revision, run by compiler_compare",
    "stage1": "after each role's Clang Release build, with that revision and its build/generated checked out, its ide "
              "runs stage1_command once (untimed) on the role's own unity source",
    "stage1_command": ["cc", "-march=native", "-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1",
                       "-DBUSTER_INCLUDE_TESTS=0", "-g", "src/buster/apps/ide/ide.c", "-lm", "-fcanonical-inline"],
    "workload": "uarch_lab compare of the baseline (A) and candidate (B) stage-1 compilers compiling the ordinary "
                "comparison's frozen merge-base tree with uarch_lab's default compile plus compile_flags",
    "compile_flags": ["-fcanonical-inline"],
    "pairs": 30,
    "warmups": 1,
    "cpu": 2,
    "order": "ABBA",
    "min_effect_percent": 0.5,
    "profile_steps": ["topdown", "sampling"],
    "correctness": "both stage-1 compilers must produce byte-identical outputs before timing "
                   "(--require-identical-output), and every timed run must reproduce its reference",
    "decision": "report-only: paired ratios, PMU counters and profiles are reported and never gate",
    "report_only": True,
}
# The topdown metrics the report lists: Zen 5 front-end groups and the level-1 split.
SELFHOST_INLINE_FRONTEND = re.compile(r"(?i)frontend|fetch|decod|op_?cache|icache|itlb|dispatch|pipelinel1")
SELFHOST_INLINE_TOPDOWN_ROWS = 12
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
ANALYZER_REQUEST_LINE_V2 = "profile: clang-analyze-full-v2"
ANALYZER_REQUEST_PATH = "benchmarks/9700x/compiler-compare.request"
ANALYZER_SUMMARY_SCHEMA = "buster-clang-analyze-full-v1"
ANALYZER_SUMMARY_SCHEMA_V2 = "buster-clang-analyze-full-v2"
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
ANALYZER_PROFILE_V2 = {
    "name": "clang-analyze-full-v2",
    "workload": "complete split-source Release clang_analyze inventory derived from the retained candidate compile database",
    "source": "candidate HEAD source and exact compile_commands.json bytes, shared byte-for-byte by both drivers",
    "database": "trusted merge-base driver generate --ci --no-sanitize --no-fuzz --no-lto --linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF",
    "inventory": "ordered native Release/LinuxCMake selection and analyzer projection re-derived from the complete candidate database; baseline PLAN_V1 and every candidate PLAN_V2 must match",
    "analysis": "clang_analyze <database> --config Release --shards 8 --jobs 2 --timeout 600 --quiet --clang <trusted resolved executable>",
    "clang_identity": "trusted-main captures the resolved Clang executable and complete resource-directory file-tree digests; that absolute executable is passed to every analyzer driver",
    "aggregate": "independent clang_analyze <same database> --config Release --shards 8 --aggregate --results <run results>",
    "preflight": "separate fresh --prepare runs for baseline and candidate; excluded from four matched analysis arms",
    "order": ["baseline", "candidate", "candidate", "baseline"],
    "repeats_per_driver": 2,
    "shards": 8,
    "jobs": 2,
    "timeout_per_tu_seconds": 600,
    "candidate_alias_policy": "canonical partition and context envelope are recomputed from exact PLAN_V2 rows; an unproven invocation class is accepted only when every row remains self-represented, carries the same explicit reason, and has no context proof or inventories",
    "driver_resource_observation": "ProcessWaitResult.resources wait4 CPU and largest individual RSS high-water; both statuses must be observed",
    "whole_tree_sampler": "each full ANALYZE_RUN must report process_tree_status=complete; RSS remains sampled, not a continuous high-water",
    "native_budget_seconds": 4500,
    "report_only": True,
}
ANALYZER_PROFILE_BY_LINE = {
    ANALYZER_REQUEST_LINE: ANALYZER_PROFILE,
    ANALYZER_REQUEST_LINE_V2: ANALYZER_PROFILE_V2,
}
ANALYZER_SCHEMA_BY_LINE = {
    ANALYZER_REQUEST_LINE: ANALYZER_SUMMARY_SCHEMA,
    ANALYZER_REQUEST_LINE_V2: ANALYZER_SUMMARY_SCHEMA_V2,
}
ANALYZER_REQUEST_LINES = tuple(ANALYZER_PROFILE_BY_LINE)
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
        if not all(_finite_positive(wall.get(key)) for key in ("a_median", "b_median", "ratio")):
            problems.append(f"issue #48 {stage_name} wall-time measurements are missing")
        code = values.get("code_bytes") if isinstance(values.get("code_bytes"), dict) else {}
        if not all(_finite_positive(code.get(key)) for key in ("a_value", "b_value", "ratio")):
            problems.append(f"issue #48 {stage_name} executable-section code-byte measurements are missing")
        instructions = values.get("instructions") if isinstance(values.get("instructions"), dict) else {}
        instruction_ratio = instructions.get("ratio")
        counters = values.get("counter_availability")
        if instruction_ratio is None:
            if not isinstance(counters, dict) or counters.get("perf_stat") is not False or not counters.get("reason"):
                problems.append(f"issue #48 {stage_name} retired-instruction NA has no unavailable-counter reason")
        elif not _finite_positive(instruction_ratio):
            problems.append(f"issue #48 {stage_name} retired-instruction ratio is invalid")
        elif not all(_finite_positive(instructions.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"issue #48 {stage_name} retired-instruction counts are missing")
    return problems


def ast_pilot_budget_verdict(cells: dict, retained_bytes: int, correctness: dict) -> dict:
    """{outcome, checks} of the #3397 adoption budget, from per-cell lab metrics.

    Each check is pass, fail, inconclusive (the CI cannot decide) or
    unavailable (a counter is NA); the outcome fails on any failure, passes
    only when every check passes, and is inconclusive otherwise."""
    limit = 1.0 - AST_PILOT_BUDGET["syntax_only_min_gain"]
    syntax, object_ = cells["syntax-only"], cells["object"]
    checks = {}
    ratio = syntax["instructions"].get("ratio")
    checks["syntax_only_instructions"] = {
        "ratio": ratio, "limit": limit,
        "outcome": "unavailable" if ratio is None else "pass" if ratio <= limit else "fail"}
    wall = syntax["wall"]
    checks["syntax_only_wall"] = {
        "ratio": wall["ratio"], "ci_low": wall.get("ci_low"), "ci_high": wall.get("ci_high"), "limit": limit,
        "outcome": "fail" if wall["ratio"] > limit else
                   "pass" if wall.get("ci_high") is not None and wall["ci_high"] < 1.0 else "inconclusive"}
    wall = object_["wall"]
    checks["object_wall"] = {
        "ratio": wall["ratio"], "ci_low": wall.get("ci_low"), "ci_high": wall.get("ci_high"), "limit": 1.0,
        "outcome": "inconclusive" if wall.get("ci_low") is None else "fail" if wall["ci_low"] > 1.0 else "pass"}
    allowed = retained_bytes * (1.0 + AST_PILOT_BUDGET["rss_slack"])
    for cell in AST_PILOT_CELLS:
        rss = cells[cell]["peak_rss"]
        growth = rss["b_median"] - rss["a_median"]
        checks[f"{cell}_peak_rss"] = {"a_median": rss["a_median"], "b_median": rss["b_median"], "growth": growth,
                                      "allowed": allowed, "outcome": "pass" if growth <= allowed else "fail"}
    checks["correctness"] = {**correctness, "outcome": "pass" if all(value is True for value in correctness.values()) else "fail"}
    outcomes = [check["outcome"] for check in checks.values()]
    outcome = "fail" if "fail" in outcomes else "pass" if all(item == "pass" for item in outcomes) else "inconclusive"
    return {"outcome": outcome, "checks": checks, "budget": dict(AST_PILOT_BUDGET), "report_only": True}


# A JSON integer is unbounded; math.isfinite and float arithmetic raise OverflowError past
# the float range, so evidence numbers are bounded before any of them.
EVIDENCE_INTEGER_LIMIT = 1 << 63


def _number(value: object) -> bool:
    """A finite float or an integer inside EVIDENCE_INTEGER_LIMIT; never raises, booleans excluded."""
    return (type(value) is int and -EVIDENCE_INTEGER_LIMIT < value < EVIDENCE_INTEGER_LIMIT) or \
        (type(value) is float and math.isfinite(value))


def _finite_positive(value: object) -> bool:
    return _number(value) and value > 0


def validate_throughput_real_source(status: object, summary: object, metadata: object, staging: object,
                                    cpuinfo: object, binaries: object) -> list[str]:
    """Reasons the evidence is not one complete THROUGHPUT_REAL_SOURCE_PROFILE run on these binaries and host.

    status is the harness exit, summary/metadata its summary.json and metadata.json,
    staging the helper's staging.json and cpuinfo the run's own cpuinfo.txt snapshot.
    """
    label = "real-source throughput"
    profile = THROUGHPUT_REAL_SOURCE_PROFILE
    inputs = profile["inputs"]
    pairs, rounds = profile["pairs_per_round"], profile["rounds"]
    cells = [f"{name}/{mode}" for name in inputs for mode in profile["modes"]]
    problems: list[str] = []
    summary = summary if isinstance(summary, dict) else None
    metadata = metadata if isinstance(metadata, dict) else None
    binaries = binaries if isinstance(binaries, dict) else {}
    if type(status) is not int or status != 0:
        problems.append(f"{label} harness exited {status!r}, not 0")
    if summary is None or metadata is None:
        return [*problems, f"{label} summary.json or metadata.json is missing or not an object"]
    if summary.get("schema") != THROUGHPUT_SCHEMA or summary.get("valid") is not True or \
            summary.get("guard_enabled") is not False:
        problems.append(f"{label} summary is not a valid unguarded schema-2 comparison")
    if summary.get("family_alpha") != 0.01 or summary.get("per_test_alpha") != 0.01 / (len(cells) * 2) or \
            summary.get("confirmed_regressions") != 0 or summary.get("inconclusive_cases") != 0:
        problems.append(f"{label} summary alpha or report-only totals differ from the profile")
    rows = summary.get("comparisons")
    names = [row.get("name") if isinstance(row, dict) else None for row in rows] if isinstance(rows, list) else []
    if names != cells:
        problems.append(f"{label} comparisons {names!r} are not exactly the profile cells {cells!r}")
    wanted = sorted((metric, number) for metric in THROUGHPUT_TEST_METRICS for number in range(rounds))
    for row in rows if isinstance(rows, list) else []:
        name = row.get("name") if isinstance(row, dict) else None
        medians = row.get("medians") if isinstance(row, dict) and isinstance(row.get("medians"), dict) else {}
        tests = row.get("tests") if isinstance(row, dict) and isinstance(row.get("tests"), list) else []
        if not isinstance(row, dict) or row.get("decision") != THROUGHPUT_REAL_SOURCE_DECISION:
            problems.append(f"{label} cell {name!r} is not a report-only diagnostic decision")
        for metric in THROUGHPUT_TEST_METRICS:
            values = medians.get(metric)
            if not isinstance(values, list) or len(values) != 2 or not all(_finite_positive(value) for value in values):
                problems.append(f"{label} cell {name!r} has incomplete {metric} medians")
        got = sorted((test.get("metric"), test.get("round")) for test in tests if isinstance(test, dict))
        if got != wanted or any(not isinstance(test, dict) or type(test.get("pairs")) is not int or test["pairs"] != pairs
                                for test in tests):
            problems.append(f"{label} cell {name!r} does not have {pairs} complete pairs per metric and round")
    facts = {"schema": THROUGHPUT_SCHEMA, "seed": profile["seed"], "pairs_per_round": pairs, "rounds": rounds,
             "warmups": profile["warmups"], "cpu": profile["cpu"], "profile": "ci", "scale": 1, "input_schema": 1,
             "workloads": [], "flags": [], "allocation_compilers": [],
             "cache_policy": "warm-filesystem-new-process", "clock": "monotonic"}
    for key, value in facts.items():
        if type(metadata.get(key)) is not type(value) or metadata.get(key) != value:
            problems.append(f"{label} metadata {key} {metadata.get(key)!r} is not the profile's {value!r}")
    real = metadata.get("real_sources")
    real = real if isinstance(real, list) else []
    if [row.get("name") if isinstance(row, dict) else None for row in real] != list(inputs):
        problems.append(f"{label} metadata does not list exactly the pinned descriptors in order")
    for row in real:
        pinned = inputs.get(row.get("name")) if isinstance(row, dict) else None
        if pinned is None:
            continue
        descriptor = row.get("descriptor")
        if row.get("descriptor_sha256") != pinned["descriptor_sha256"] or \
                row.get("input_tree_sha256") != pinned["input_tree_sha256"] or \
                row.get("requested_translation_unit_bytes") != pinned["requested_translation_unit_bytes"] or \
                row.get("operation") != "source-to-linked-executable" or row.get("template") != "compile_link_argv" or \
                not isinstance(descriptor, str) or not descriptor.endswith("/" + pinned["descriptor"]):
            problems.append(f"{label} descriptor {row.get('name')!r} is not its pinned identity")
    jobs = metadata.get("jobs")
    expected_jobs = [(name, mode) for name in inputs for mode in profile["modes"]]
    if not isinstance(jobs, list) or len(jobs) != len(expected_jobs):
        problems.append(f"{label} metadata job population is incomplete")
    else:
        for index, (job, (name, mode)) in enumerate(zip(jobs, expected_jobs)):
            if not isinstance(job, dict) or job.get("job") != index or job.get("name") != name or \
                    job.get("mode") != mode or job.get("artifact") != "executable" or \
                    job.get("sha256") != inputs[name]["input_tree_sha256"] or \
                    job.get("bytes") != inputs[name]["requested_translation_unit_bytes"]:
                problems.append(f"{label} metadata job {index} is not {name}/{mode} on its pinned tree")
    provenance = metadata.get("compiler_provenance")
    provenance = provenance if isinstance(provenance, list) and len(provenance) == 2 else [{}, {}]
    for role, row in zip(("baseline", "candidate"), provenance):
        recorded = binaries.get(role) if isinstance(binaries.get(role), dict) else {}
        digest = row.get("sha256") if isinstance(row, dict) else None
        if not (isinstance(digest, str) and SHA256.fullmatch(digest)) or digest != recorded.get("sha256"):
            problems.append(f"{label} {role} compiler is not the measured {role} binary")
    staged = staging.get("inputs") if isinstance(staging, dict) else None
    if not isinstance(staging, dict) or staging.get("schema") != THROUGHPUT_REAL_SOURCE_STAGING_SCHEMA or \
            staging.get("status") != "complete" or not isinstance(staged, dict) or list(staged) != list(inputs):
        problems.append(f"{label} staging record is missing, incomplete or not the pinned input set")
    else:
        for name, pinned in inputs.items():
            row = staged[name] if isinstance(staged[name], dict) else {}
            archive = pinned["archive"]
            if row.get("descriptor_sha256") != pinned["descriptor_sha256"] or \
                    row.get("files") != [{"path": item["path"], "sha256": item["sha256"]} for item in pinned["files"]] or \
                    row.get("copies") != [{"path": item["path"], "sha256": item["sha256"]} for item in pinned["copies"]] or \
                    row.get("archive_sha256") != (archive["sha256"] if archive else None):
                problems.append(f"{label} staged input {name!r} does not match its pins")
    models = [line.split(":", 1)[1].strip() for line in cpuinfo.splitlines() if line.startswith("model name")] \
        if isinstance(cpuinfo, str) else []
    if not models or not all(APPROVED_HOST.search(model) for model in models):
        problems.append(f"{label} run's own cpuinfo does not name the approved Zen 5 host (AMD Ryzen 7 9700X)")
    return problems


def throughput_real_source_digest(summary: object) -> list[dict]:
    """The report's view of each real-source cell: base/candidate wall and peak-RSS medians."""
    rows = summary.get("comparisons") if isinstance(summary, dict) and isinstance(summary.get("comparisons"), list) else []
    digest = []
    for row in rows:
        if not isinstance(row, dict):
            continue
        medians = row.get("medians") if isinstance(row.get("medians"), dict) else {}
        wall, rss = medians.get("wall_seconds"), medians.get("peak_rss_bytes")
        digest.append({"name": row.get("name"), "decision": row.get("decision"),
                       "wall_seconds": wall if isinstance(wall, list) else None,
                       "peak_rss_bytes": rss if isinstance(rss, list) else None})
    return digest


def production_profile_fields(text: object) -> dict:
    """The key=value lines of a production_profile summary.txt; an unreadable or repeated key leaves it out."""
    fields: dict = {}
    repeated: set = set()
    for line in text.splitlines() if isinstance(text, str) else []:
        key, separator, value = line.partition("=")
        if separator:
            if key in fields:
                repeated.add(key)
            fields[key] = value
    return {key: value for key, value in fields.items() if key not in repeated}


def validate_production_profile(build_status: object, summary_text: object, ledger_text: object, head: str,
                                status: object, summary: object, metadata: object, binaries: object) -> list[str]:
    """Reasons the evidence is not one complete PRODUCTION_PROFILE_PROFILE run on these binaries.

    build_status is production_profile's exit, summary_text its summary.txt and ledger_text its
    evidence/phases.tsv; status, summary and metadata are the timed corpus run's exit, summary.json
    and metadata.json; binaries maps baseline/candidate to the measured release/pgo-lto hashes.
    """
    label = "production profile"
    problems: list[str] = []
    if type(build_status) is not int or build_status != 0:
        problems.append(f"{label} build exited {build_status!r}, not 0")
    fields = production_profile_fields(summary_text)
    candidate = binaries.get("candidate") if isinstance(binaries, dict) and isinstance(binaries.get("candidate"), dict) else {}
    for key, wanted in (("status", "passed"), ("source_revision", head), ("debug_sections", "absent"),
                        ("correctness", "test_all"), ("benchmarks", "skipped"),
                        ("production_compiler_sha256", candidate.get("sha256"))):
        if not isinstance(wanted, str) or fields.get(key) != wanted:
            problems.append(f"{label} summary.txt {key} is {fields.get(key)!r}, not {wanted!r}")
    lines = ledger_text.splitlines() if isinstance(ledger_text, str) else []
    header = f"{PRODUCTION_PROFILE_LEDGER}\tcomplete=1\tphases={PRODUCTION_PROFILE_PHASES}\texpected={PRODUCTION_PROFILE_PHASES}"
    columns = "index\tlabel\tkind\tvariant\tstart_us\tduration_us\toutcome\tuser_cpu_us\tsystem_cpu_us\tpeak_memory_bytes"
    rows = [line.split("\t") for line in lines[2:]]
    # Every frozen slot exactly once, in order, so a duplicated, missing or substituted phase is refused.
    population = [(row[0], row[1], row[2], row[3]) if len(row) == 10 else None for row in rows]
    expected = [(str(slot), name, kind, variant) for slot, name, kind, variant in PRODUCTION_PROFILE_LEDGER_PHASES]
    if len(lines) < 2 or lines[0] != header or lines[1] != columns or population != expected or \
            any(row[6] != "passed" for row in rows):
        problems.append(f"{label} phase ledger is not the {PRODUCTION_PROFILE_PHASES} frozen phases, each passed once in order")
    if isinstance(binaries, dict) and isinstance(binaries.get("baseline"), dict) and \
            binaries["baseline"].get("sha256") == candidate.get("sha256"):
        problems.append(f"{label} release and pgo-lto compilers are the same binary")
    problems.extend(f"{label} corpus: {reason}" for reason in classify_throughput_exit(status, summary, metadata, binaries))
    return problems


def compiler_probes_interval(ratios: list) -> tuple[float, float]:
    """The distribution-free 95% order-statistic interval of the median of ratios (sorted copy taken here)."""
    ordered = sorted(ratios)
    count = len(ordered)
    # The largest k whose lower tail P(Binomial(count, 1/2) < k) stays within 2.5%.
    lower, tail = 0, 0.0
    while lower < count // 2 and tail + math.comb(count, lower) / 2 ** count <= 0.025:
        tail += math.comb(count, lower) / 2 ** count
        lower += 1
    lower = max(lower, 1)
    return ordered[lower - 1], ordered[count - lower]


def _compiler_probes_invocation(value: object, definition: dict, case: dict, repeats: int) -> tuple[float | None, object]:
    """(median sample, check fields) of one well-formed passed invocation, else (None, None).

    Its self-check must carry exactly the probe's check_fields, each matching its pattern, and the
    case's fixed workload markers (size, and for line-mark the mode, row count and batch).
    """
    result = None, None
    check = value.get("check") if isinstance(value, dict) else None
    if isinstance(value, dict) and set(value) == {"exit", "samples", "check"} and type(value["exit"]) is int and \
            value["exit"] == 0 and isinstance(value["samples"], list) and len(value["samples"]) == repeats and \
            all(type(sample) is int and 0 < sample <= COMPILER_PROBES_SAMPLE_LIMIT_NS for sample in value["samples"]) and \
            isinstance(check, dict) and set(check) == set(definition["check_fields"]) and \
            all(isinstance(check[key], str) and re.fullmatch(pattern, check[key])
                for key, pattern in definition["check_fields"].items()) and \
            all(check[key] == marker for key, marker in case["markers"].items()):
        result = float(statistics.median(value["samples"])), value["check"]
    return result


def compiler_probes_case_statistics(probe: str, case: object) -> tuple[dict | None, list[str]]:
    """Re-derive one case's statistics from its raw invocations; (statistics, problems)."""
    profile = COMPILER_PROBES_PROFILE
    definition = profile["probes"].get(probe) if isinstance(probe, str) else None
    label = f"compiler probes {probe}/{case.get('name') if isinstance(case, dict) else '?'}"
    expected = None
    if definition is not None and isinstance(case, dict):
        expected = next((item for item in definition["cases"] if item["name"] == case.get("name")), None)
    if expected is None or case.get("probe") != probe or case.get("arguments") != expected["arguments"]:
        return None, [f"{label} is not a case of the frozen profile"]
    problems: list[str] = []
    checks = {role: [] for role in COMPILER_PROBES_ROLES}
    warmups = case.get("warmups")
    if not isinstance(warmups, dict) or set(warmups) != set(COMPILER_PROBES_ROLES) or \
            any(not isinstance(warmups[role], list) or len(warmups[role]) != profile["warmups"] for role in COMPILER_PROBES_ROLES):
        problems.append(f"{label} does not have {profile['warmups']} warmups per binary")
    else:
        for role in COMPILER_PROBES_ROLES:
            for item in warmups[role]:
                median, check = _compiler_probes_invocation(item, definition, expected, profile["repeats"])
                if median is None:
                    problems.append(f"{label} has a failed or partial {role} warmup")
                checks[role].append(check)
    pairs = case.get("pairs")
    ratios: list[float] = []
    medians = {role: [] for role in COMPILER_PROBES_ROLES}
    if not isinstance(pairs, list) or len(pairs) != profile["pairs"]:
        problems.append(f"{label} does not have exactly {profile['pairs']} pairs")
        pairs = []
    for index, pair in enumerate(pairs):
        order = ["baseline", "candidate"] if index % 2 == 0 else ["candidate", "baseline"]
        if not isinstance(pair, dict) or pair.get("order") != order or set(pair) != {"order", *COMPILER_PROBES_ROLES}:
            problems.append(f"{label} pair {index} is malformed or out of the fixed order")
            continue
        values = {}
        for role in COMPILER_PROBES_ROLES:
            values[role], check = _compiler_probes_invocation(pair[role], definition, expected, profile["repeats"])
            checks[role].append(check)
        if values["baseline"] is None or values["candidate"] is None:
            problems.append(f"{label} pair {index} has a failed or partial invocation")
            continue
        for role in COMPILER_PROBES_ROLES:
            medians[role].append(values[role])
        ratios.append(values["candidate"] / values["baseline"])
    for role in COMPILER_PROBES_ROLES:
        if any(check != checks[role][0] for check in checks[role]):
            problems.append(f"{label} {role} self-check output is not identical across invocations")
    if problems or len(ratios) != profile["pairs"]:
        return None, problems
    low, high = compiler_probes_interval(ratios)
    return {"name": f"{probe}/{expected['name']}", "baseline_ns": statistics.median(medians["baseline"]),
            "candidate_ns": statistics.median(medians["candidate"]), "ratio": statistics.median(ratios),
            "ratio_low": low, "ratio_high": high,
            "candidate_faster": sum(1 for ratio in ratios if ratio < 1.0),
            "outputs_identical": checks["baseline"][0] == checks["candidate"][0]}, []


def compiler_probes_digest(summary: object) -> list[dict]:
    """The re-derived per-case statistics of a compiler-probes summary; malformed cases are left out."""
    digest = []
    cases = summary.get("cases") if isinstance(summary, dict) else None
    for case in cases if isinstance(cases, list) else []:
        statistics_value, unused_problems = compiler_probes_case_statistics(
            case.get("probe") if isinstance(case, dict) else None, case)
        if statistics_value is not None:
            digest.append(statistics_value)
    return digest


def validate_compiler_probes(status: object, summary: object, base: str, head: str, binaries: object,
                             sources: object) -> list[str]:
    """Reasons the evidence is not one complete COMPILER_PROBES_PROFILE run of these probe binaries.

    status is the compiler_probes.c harness's exit and summary its summary.json; base and head are the
    compared revisions. binaries maps probe -> role -> {"sha256", "revision"} and sources maps probe ->
    the copied probe source's sha256, both hashed by compiler_compare independently of the harness.
    Every statistic is re-derived from the raw samples; the harness states none.
    """
    label = "compiler probes"
    profile = COMPILER_PROBES_PROFILE
    problems: list[str] = []
    if type(status) is not int or status != 0:
        problems.append(f"{label} exited {status!r}, not 0")
    if not isinstance(summary, dict):
        return [*problems, f"{label} summary is missing or malformed"]
    for key, wanted in (("schema", COMPILER_PROBES_SCHEMA), ("profile", profile["name"]), ("base", base),
                        ("head", head), ("status", "complete"), ("errors", [])):
        if summary.get(key) != wanted:
            problems.append(f"{label} summary {key} is not {wanted!r}")
    wanted_builds = [{"probe": probe, "role": role, "revision": revision, "exit": 0}
                     for probe in profile["probes"] for role, revision in zip(COMPILER_PROBES_ROLES, (base, head))]
    if summary.get("builds") != wanted_builds:
        problems.append(f"{label} builds are not one passed build per probe and revision")
    for probe in profile["probes"]:
        if not isinstance(sources, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(sources.get(probe, ""))):
            problems.append(f"{label} {probe} source hash is missing")
        for role, revision in zip(COMPILER_PROBES_ROLES, (base, head)):
            measured = binaries.get(probe, {}).get(role) if isinstance(binaries, dict) and \
                isinstance(binaries.get(probe), dict) else None
            if not isinstance(measured, dict) or measured.get("revision") != revision or \
                    not re.fullmatch(r"[0-9a-f]{64}", str(measured.get("sha256", ""))):
                problems.append(f"{label} {probe} {role} binary is not a measured {revision[:12]} build")
    cases = summary.get("cases")
    wanted_cases = [(probe, case["name"]) for probe, definition in profile["probes"].items() for case in definition["cases"]]
    names = [(case.get("probe"), case.get("name")) if isinstance(case, dict) else None
             for case in (cases if isinstance(cases, list) else [])]
    if names != wanted_cases:
        problems.append(f"{label} cases are not the frozen {len(wanted_cases)} cases in order")
    for case in cases if isinstance(cases, list) else []:
        unused_value, case_problems = compiler_probes_case_statistics(
            case.get("probe") if isinstance(case, dict) else None, case)
        problems.extend(case_problems)
    return problems


def frontend_scaling_population(cases: tuple = FRONTEND_SCALING_CASES) -> list[tuple[str, int, str]]:
    """(kind, count, mode) of every frontend-scaling record of `cases`, in run order; FRONTEND_SELECTOR_PROFILE's by default."""
    return [(kind, count, mode) for kind, counts, modes in cases for count in counts for mode in modes]


def ide_bench_fields(line: object) -> dict | None:
    """The parsed BENCH_C_FRONTEND line, or None when it is not exactly one well-formed line."""
    match = IDE_BENCH.fullmatch(line) if isinstance(line, str) else None
    fields = None
    if match:
        fields = {"path": match.group(1), **{key: int(match.group(index)) for index, key in
                                            enumerate(("iterations", "bytes", "min_ns", "median_ns", "throughput_mb_s"), 2)}}
        if not all(_number(value) for key, value in fields.items() if key != "path"):
            fields = None
    return fields


def _samples(values: object, count: int, positive: bool) -> bool:
    return isinstance(values, list) and len(values) == count and \
        all(_number(value) and (value > 0 if positive else value >= 0) for value in values)


def _median(values: object) -> float | None:
    """The median of a non-empty list of finite numbers, else None; reported medians are re-derived with it."""
    usable = isinstance(values, list) and values and \
        all(_number(value) for value in values)
    return statistics.median(values) if usable else None


def _selection_sample(sample: object, allocators: list) -> bool:
    """Whether one selection_benchmark sample has every typed identity, work count, timing and object field."""
    metrics = sample.get("selection") if isinstance(sample, dict) and isinstance(sample.get("selection"), dict) else {}
    built = sample.get("objects") if isinstance(sample, dict) and isinstance(sample.get("objects"), dict) else {}
    return isinstance(sample, dict) and isinstance(sample.get("target"), str) and bool(sample["target"]) and \
        all(type(metrics.get(key)) is int and _number(metrics[key]) and metrics[key] >= 0
            for key in (*SELECTION_WORK_FIELDS, "median_ns")) and \
        metrics["fallback_functions"] == 0 and \
        all(metrics[key] > 0 for key in ("functions", "mir_instructions", "median_ns")) and \
        sorted(built) == sorted(allocators) and \
        all(isinstance(item, dict) and type(item.get("wall_ns")) is int and _finite_positive(item["wall_ns"]) and
            isinstance(item.get("sha256"), str) and SHA256.fullmatch(item["sha256"]) is not None
            for item in built.values())


def selection_medians(samples: object, allocators: list) -> dict | None:
    """Re-derived {selection_median_ns, compile_median_ns} of one binary's samples, or None when any is malformed."""
    usable = isinstance(samples, list) and samples and all(_selection_sample(sample, allocators) for sample in samples)
    return {"selection_median_ns": statistics.median(sample["selection"]["median_ns"] for sample in samples),
            "compile_median_ns": {allocator: statistics.median(sample["objects"][allocator]["wall_ns"] for sample in samples)
                                  for allocator in allocators}} if usable else None


def frontend_scaling_problems(scaling: dict, cpu: int, population: list, scaling_status: object, provenance: object,
                              results: object, hashes: dict) -> list[str]:
    """Reasons a benchmark_frontend_scaling.py run is not exactly `scaling`'s suite and `population` on `hashes`.

    scaling is a profile's frontend_scaling section, scaling_status the tool's exit and provenance/results
    its provenance.json and results.json; every cell needs its full samples and object cells their hash.
    """
    problems: list[str] = []
    label = "frontend scaling"
    if type(scaling_status) is not int or scaling_status != 0:
        problems.append(f"{label} exited {scaling_status!r}, not 0")
    provenance = provenance if isinstance(provenance, dict) else {}
    for key, wanted in (("suite", scaling["suite"]), ("warmups", scaling["warmups"]),
                        ("repetitions", scaling["repetitions"]), ("cpu", cpu), ("affinity", [cpu]), ("complete", True),
                        ("binary_sha256", hashes)):
        if type(provenance.get(key)) is not type(wanted) or provenance.get(key) != wanted:
            problems.append(f"{label} provenance {key} {provenance.get(key)!r} is not {wanted!r}")
    if not (isinstance(provenance.get("cpu_model"), str) and APPROVED_HOST.search(provenance["cpu_model"])):
        problems.append(f"{label} observed CPU {provenance.get('cpu_model')!r} is not the approved Zen 5 host "
                        "(AMD Ryzen 7 9700X)")
    records = results if isinstance(results, list) else []
    # A boolean count equals 1 in Python, so the type is part of the cell's identity.
    observed = [(row.get("kind"), row.get("count") if type(row.get("count")) is int else None, row.get("mode"))
                if isinstance(row, dict) else None for row in records]
    if observed != population:
        problems.append(f"{label} records are not exactly the {len(population)} profile cells")
    for row in records:
        if not isinstance(row, dict):
            continue
        name = f"{row.get('kind')}-{row.get('count')}/{row.get('mode')}"
        compilers = row.get("compilers") if isinstance(row.get("compilers"), dict) else {}
        if sorted(compilers) != ["baseline", "candidate"] or not all(
                isinstance(item, dict) and _samples(item.get("wall_seconds"), scaling["repetitions"], True) and
                _samples(item.get("cpu_seconds"), scaling["repetitions"], False) for item in compilers.values()):
            problems.append(f"{label} cell {name} does not have {scaling['repetitions']} complete samples per binary")
        elif any(item.get(f"{metric}_median") != _median(item[f"{metric}_seconds"])
                 for item in compilers.values() for metric in ("wall", "cpu")):
            problems.append(f"{label} cell {name} reports medians its samples do not give")
        if row.get("mode") == "object" and not (isinstance(row.get("object_sha256"), str) and
                                                SHA256.fullmatch(row["object_sha256"])):
            problems.append(f"{label} cell {name} has no identical-object hash")
    return problems


def validate_frontend_selector(scaling_status: object, provenance: object, results: object, selection_status: object,
                               report: object, bench: object, binaries: object) -> list[str]:
    """Reasons the evidence is not one complete FRONTEND_SELECTOR_PROFILE run on these binaries and host.

    scaling_status/selection_status are the two tools' exits, provenance/results the frontend-scaling
    provenance.json and results.json, report the selection report.json, bench the harness's ide-bench.json
    and binaries the measured baseline/candidate records.
    """
    profile = FRONTEND_SELECTOR_PROFILE
    cpu = profile["cpu"]
    binaries = binaries if isinstance(binaries, dict) else {}
    hashes = {role: (binaries.get(role) or {}).get("sha256") if isinstance(binaries.get(role), dict) else None
              for role in ("baseline", "candidate")}
    problems: list[str] = []
    if not all(isinstance(value, str) and SHA256.fullmatch(value) for value in hashes.values()):
        problems.append("frontend-selector measured binaries are not both recorded")
    problems.extend(frontend_scaling_problems(profile["frontend_scaling"], cpu, frontend_scaling_population(),
                                              scaling_status, provenance, results, hashes))
    # Selection.
    label = "selection benchmark"
    selection = profile["selection"]
    if type(selection_status) is not int or selection_status != 0:
        problems.append(f"{label} exited {selection_status!r}, not 0")
    report = report if isinstance(report, dict) else {}
    host = report.get("host") if isinstance(report.get("host"), dict) else {}
    compilers = report.get("compilers") if isinstance(report.get("compilers"), dict) else {}
    if report.get("schema_version") != 1 or report.get("build_description") != profile["name"]:
        problems.append(f"{label} report is not a schema-1 {profile['name']} report")
    if {role: (compilers.get(role) or {}).get("sha256") if isinstance(compilers.get(role), dict) else None
            for role in ("baseline", "candidate")} != hashes or sorted(compilers) != ["baseline", "candidate"]:
        problems.append(f"{label} compilers are not the measured baseline and candidate binaries")
    if not (isinstance(host.get("cpu"), str) and APPROVED_HOST.search(host["cpu"])):
        problems.append(f"{label} observed CPU {host.get('cpu')!r} is not the approved Zen 5 host (AMD Ryzen 7 9700X)")
    if host.get("affinity") != [cpu]:
        problems.append(f"{label} affinity {host.get('affinity')!r} is not [{cpu}]")
    workloads = report.get("workloads") if isinstance(report.get("workloads"), list) else []
    if [Path(row.get("path", "")).name if isinstance(row, dict) and isinstance(row.get("path"), str) else None
            for row in workloads] != selection["workloads"]:
        problems.append(f"{label} workloads are not exactly {selection['workloads']!r}")
    for row in workloads:
        if not isinstance(row, dict):
            continue
        name = Path(str(row.get("path"))).name
        samples = row.get("samples") if isinstance(row.get("samples"), dict) else {}
        flat = [sample for role in ("baseline", "candidate") for sample in
                (samples.get(role) if isinstance(samples.get(role), list) else [])]
        if sorted(samples) != ["baseline", "candidate"] or any(
                not isinstance(samples[role], list) or len(samples[role]) != selection["samples"] for role in samples):
            problems.append(f"{label} workload {name} does not have {selection['samples']} samples per binary")
        identities, objects = set(), {allocator: set() for allocator in selection["allocators"]}
        complete = all(_selection_sample(sample, selection["allocators"]) for sample in flat)
        # Identities and hashes are compared only once every field is typed, so nothing unhashable reaches a set.
        for sample in flat if complete else []:
            identities.add((sample["target"], *(sample["selection"][key] for key in SELECTION_WORK_FIELDS)))
            for allocator, item in sample["objects"].items():
                objects[allocator].add(item["sha256"])
        summary = row.get("summary") if isinstance(row.get("summary"), dict) else {}
        if not complete:
            problems.append(f"{label} workload {name} has a malformed, incomplete or fallback sample")
        elif any(not isinstance(summary.get(role), dict) or
                 {key: summary[role].get(key) for key in ("selection_median_ns", "compile_median_ns")} !=
                 selection_medians(samples.get(role), selection["allocators"]) for role in ("baseline", "candidate")):
            problems.append(f"{label} workload {name} reports medians its samples do not give")
        if len(identities) > 1:
            problems.append(f"{label} workload {name} selected different work or targets across samples")
        if any(len(found) > 1 for found in objects.values()):
            problems.append(f"{label} workload {name} produced different objects")
    # ide bench.
    label = "ide bench"
    wanted = profile["ide_bench"]
    bench = bench if isinstance(bench, dict) else {}
    if bench.get("schema") != FRONTEND_SELECTOR_SCHEMA or bench.get("cpu") != cpu or bench.get("binaries") != hashes:
        problems.append(f"{label} record is not a {FRONTEND_SELECTOR_SCHEMA} run on the measured binaries and CPU {cpu}")
    if not (isinstance(bench.get("cpu_model"), str) and APPROVED_HOST.search(bench["cpu_model"])):
        problems.append(f"{label} observed CPU {bench.get('cpu_model')!r} is not the approved Zen 5 host (AMD Ryzen 7 9700X)")
    runs = bench.get("runs") if isinstance(bench.get("runs"), list) else []
    order = [role for pair in range(wanted["pairs"]) for role in
             (("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline"))]
    if [run.get("role") if isinstance(run, dict) else None for run in runs] != order:
        problems.append(f"{label} runs are not the profile's {len(order)} alternating runs")
    sizes = set()
    for index, run in enumerate(runs):
        fields = ide_bench_fields(run.get("line")) if isinstance(run, dict) else None
        if not isinstance(run, dict) or run.get("exit") != 0 or fields is None or fields["path"] != wanted["source"] or \
                fields["iterations"] != wanted["iterations"] or fields["bytes"] <= 0 or \
                not 0 < fields["min_ns"] <= fields["median_ns"]:
            problems.append(f"{label} run {index} did not report one complete {wanted['iterations']}-iteration line")
        else:
            sizes.add(fields["bytes"])
    if len(sizes) > 1:
        problems.append(f"{label} runs measured different source sizes")
    return problems


def frontend_selector_digest(results: object, report: object, bench: object) -> list[dict]:
    """The report's view: baseline/candidate medians re-derived from the raw samples of every cell, never
    taken from a tool's own summary fields."""
    digest = frontend_scaling_digest(results)
    for row in report.get("workloads", []) if isinstance(report, dict) and isinstance(report.get("workloads"), list) else []:
        samples = row.get("samples") if isinstance(row, dict) and isinstance(row.get("samples"), dict) else {}
        name = Path(str(row.get("path"))).name if isinstance(row, dict) else "NA"
        allocators = FRONTEND_SELECTOR_PROFILE["selection"]["allocators"]
        roles = [selection_medians(samples.get(role), allocators) or {} for role in ("baseline", "candidate")]
        digest.append({"name": f"selection {name}", "unit": "ns",
                       "values": [role.get("selection_median_ns") for role in roles]})
        for allocator in allocators:
            digest.append({"name": f"selection {name} -c {allocator}", "unit": "ns",
                           "values": [role["compile_median_ns"][allocator] if role else None for role in roles]})
    runs = bench.get("runs") if isinstance(bench, dict) and isinstance(bench.get("runs"), list) else []
    medians = {"baseline": [], "candidate": []}
    for run in runs:
        fields = ide_bench_fields(run.get("line")) if isinstance(run, dict) else None
        if fields is not None and run.get("role") in ("baseline", "candidate"):
            medians[run["role"]].append(fields["median_ns"])
    if runs:
        digest.append({"name": "ide bench", "unit": "ns",
                       "values": [statistics.median(medians[role]) if medians[role] else None
                                  for role in ("baseline", "candidate")]})
    return digest


def validate_frontend_nesting(scaling_status: object, provenance: object, results: object, binaries: object) -> list[str]:
    """Reasons the evidence is not one complete FRONTEND_NESTING_PROFILE run on these binaries and host.

    scaling_status is the tool's exit, provenance/results its provenance.json and results.json and
    binaries the measured baseline/candidate records.
    """
    profile = FRONTEND_NESTING_PROFILE
    binaries = binaries if isinstance(binaries, dict) else {}
    hashes = {role: (binaries.get(role) or {}).get("sha256") if isinstance(binaries.get(role), dict) else None
              for role in ("baseline", "candidate")}
    problems: list[str] = []
    if not all(isinstance(value, str) and SHA256.fullmatch(value) for value in hashes.values()):
        problems.append("frontend-nesting measured binaries are not both recorded")
    problems.extend(frontend_scaling_problems(profile["frontend_scaling"], profile["cpu"],
                                              frontend_scaling_population(FRONTEND_NESTING_CASES), scaling_status,
                                              provenance, results, hashes))
    return problems


def frontend_scaling_digest(results: object) -> list[dict]:
    """Baseline/candidate wall medians of every frontend-scaling cell, re-derived from its raw samples."""
    digest = []
    for row in results if isinstance(results, list) else []:
        compilers = row.get("compilers") if isinstance(row, dict) and isinstance(row.get("compilers"), dict) else {}
        values = [_median(compilers[role].get("wall_seconds")) if isinstance(compilers.get(role), dict) else None
                  for role in ("baseline", "candidate")]
        if isinstance(row, dict):
            digest.append({"name": f"frontend {row.get('kind')}-{row.get('count')}/{row.get('mode')}",
                           "unit": "s", "values": values})
    return digest


def wild_linker_population() -> list[tuple[str, int, str, str]]:
    """(cell, pairs, A linker, B linker) of every WILD_LINKER_PROFILE comparison, in the harness's run order."""
    profile = WILD_LINKER_PROFILE
    population = []
    for cell in profile["cells"]:
        population += [(f"{cell}-default", profile["pairs"], "comparator", "wild"),
                       (f"{cell}-threads1", profile["pairs"], "comparator", "wild")]
        if cell == "ide-debug":
            population += [("ide-debug-aa-comparator", profile["control_pairs"], "comparator", "comparator"),
                           ("ide-debug-aa-wild", profile["control_pairs"], "wild", "wild")]
    return population


def wild_linker_bundle_files() -> dict[str, str]:
    """Bundle key -> the harness output each one reads, relative to the exported wild_linker directory."""
    files = {"identity": "wild-linker.json", "raw": "raw.csv", "summary": "summary.csv", "commands": "commands.tsv",
             "affinity": "affinity.txt", "completion": "completion.txt"}
    for cell in WILD_LINKER_CELLS:
        files[f"inputs {cell}"] = f"{cell}-inputs.sha256"
        files[f"argv {cell}"] = f"{cell}-argv.txt"
        for linker in WILD_LINKER_LINKERS:
            files[f"output {cell} {linker}"] = f"{cell}-{linker}.output.sha256"
            files[f"comment {cell} {linker}"] = f"{cell}-{linker}.comment.txt"
    return files


def _xorshift(state: int) -> int:
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= state >> 17
    return state ^ ((state << 5) & 0xFFFFFFFF)


def wild_linker_schedule() -> tuple[dict, dict]:
    """Replay the harness's seeded generator: ({cell: [first variant index per round]}, {cell: state before
    its bootstrap}), so the run order and every bootstrap interval are re-derived rather than trusted."""
    state = WILD_LINKER_PROFILE["seed"]
    orders, bootstrap = {}, {}
    for cell, pairs, _, _ in wild_linker_population():
        order = []
        for _ in range(pairs + WILD_LINKER_PROFILE["warmups"]):
            state = _xorshift(state)
            order.append(state & 1)
        orders[cell] = order
        bootstrap[cell] = state
        for _ in range(WILD_LINKER_PROFILE["bootstraps"] * pairs):
            state = _xorshift(state)
    return orders, bootstrap


def _paired_interval(ratios: list[float], state: int) -> tuple[float, float]:
    """The harness's report(): 4001 resampled medians from the seeded generator, [100] and [3900] once sorted."""
    count = len(ratios)
    boots = []
    for _ in range(WILD_LINKER_PROFILE["bootstraps"]):
        sample = []
        for _ in range(count):
            state = _xorshift(state)
            sample.append(ratios[state % count])
        boots.append(sorted(sample)[count // 2])
    boots.sort()
    return boots[100], boots[3900]


def _csv_rows(text: object) -> list[dict] | None:
    try:
        rows = list(csv.DictReader(text.splitlines())) if isinstance(text, str) else None
    except csv.Error:
        rows = None
    return rows


def _close(left: float, right: object) -> bool:
    """The harness prints %.6f of double arithmetic; a re-derivation from printed samples agrees to 1e-5."""
    return _number(right) and abs(left - right) <= 1e-5 * max(1.0, abs(left))


def wild_linker_cells(raw: object, summary: object) -> tuple[list[dict], list[str]]:
    """(cells, problems): every comparison re-derived from raw.csv against the frozen population, seeded order
    and summary.csv; a cell whose samples are incomplete is reported with no values."""
    problems: list[str] = []
    rows = _csv_rows(raw)
    reported = _csv_rows(summary)
    if rows is None or reported is None:
        return [], ["wild-linker raw.csv or summary.csv is not CSV"]
    reported_by_cell = {}
    for row in reported:
        if row.get("cell") in reported_by_cell:
            problems.append(f"wild-linker summary repeats cell {row.get('cell')!r}")
        reported_by_cell[row.get("cell")] = row
    population = wild_linker_population()
    if sorted(reported_by_cell, key=str) != sorted(cell for cell, _, _, _ in population):
        problems.append("wild-linker summary cells are not exactly the profile's comparisons")
    orders, states = wild_linker_schedule()
    warmups = WILD_LINKER_PROFILE["warmups"]
    cells = []
    for cell, pairs, first, second in population:
        timed = [row for row in rows if row.get("cell") == cell]
        expected = [(variant, str(round_)) for round_, lead in zip(range(-warmups, pairs), orders[cell])
                    for variant in (("A", "B") if lead == 0 else ("B", "A"))]
        values = {"A": [], "B": []}
        complete = [(row.get("variant"), row.get("round")) for row in timed] == expected
        for row in timed if complete else []:
            try:
                wall = float(row["wall_ms"])
                exit_status = int(row["exit"])
            except (TypeError, ValueError):
                wall, exit_status = math.nan, -1
            if exit_status != 0 or not (math.isfinite(wall) and wall > 0):
                complete = False
            elif int(row["round"]) >= 0:
                values[row["variant"]].append(wall)
        record = {"name": cell, "pairs": pairs, "linkers": [first, second], "values": [None, None],
                  "ratio": None, "ci95": [None, None]}
        if not complete:
            problems.append(f"wild-linker {cell} is not {warmups} warmups and {pairs} seeded pairs, each linked successfully")
        else:
            ratios = [b / a for a, b in zip(values["A"], values["B"])]
            medians = [sorted(values[key])[pairs // 2] for key in ("A", "B")]
            ratio = sorted(ratios)[pairs // 2]
            interval = _paired_interval(ratios, states[cell])
            row = reported_by_cell.get(cell) or {}
            try:
                printed = [float(row.get(key)) for key in ("median_A_ms", "median_B_ms", "median_paired_B_over_A",
                                                            "ci95_low", "ci95_high")]
                printed_pairs = int(row.get("n"))
            except (TypeError, ValueError):
                printed, printed_pairs = [], None
            if printed_pairs != pairs or len(printed) != 5 or \
                    not all(_close(mine, theirs) for mine, theirs in zip([*medians, ratio, *interval], printed)):
                problems.append(f"wild-linker {cell} summary does not match its raw samples")
            record.update({"values": medians, "ratio": ratio, "ci95": list(interval)})
        cells.append(record)
    return cells, problems


def wild_linker_digest(raw: object, summary: object) -> list[dict]:
    """The report's view of every comparison, re-derived from the raw samples."""
    return wild_linker_cells(raw, summary)[0]


def _unique_pairs(pairs: list) -> dict:
    """json object hook: a repeated key could hide a second value, so it is malformed."""
    keys = [key for key, _ in pairs]
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate JSON key")
    return dict(pairs)


def _wild_linker_identity_problems(identity: object, head: str) -> tuple[dict, list[str]]:
    """(linkers, problems) for the harness-side identity record."""
    profile = WILD_LINKER_PROFILE
    identity = identity if isinstance(identity, dict) else {}
    problems: list[str] = []
    for key, wanted in (("schema", WILD_LINKER_SCHEMA), ("profile", profile["name"]), ("head", head),
                        ("cpus", profile["cpus"]), ("exit", 0)):
        if type(identity.get(key)) is not type(wanted) or identity.get(key) != wanted:
            problems.append(f"wild-linker record {key} {identity.get(key)!r} is not {wanted!r}")
    if not (isinstance(identity.get("cpu_model"), str) and APPROVED_HOST.search(identity["cpu_model"])):
        problems.append(f"wild-linker observed CPU {identity.get('cpu_model')!r} is not the approved Zen 5 host "
                        "(AMD Ryzen 7 9700X)")
    linkers = identity.get("linkers") if isinstance(identity.get("linkers"), dict) else {}
    usable = {}
    for role in WILD_LINKER_LINKERS:
        linker = linkers.get(role) if isinstance(linkers.get(role), dict) else {}
        kind = linker.get("kind")
        frozen = profile["wild"] if role == "wild" else WILD_LINKER_COMPARATORS.get(kind) if isinstance(kind, str) else None
        if role == "wild" and kind != "wild":
            frozen = None
        version = linker.get("version")
        if frozen is None:
            problems.append(f"wild-linker {role} kind {kind!r} is not supported")
        elif linker.get("command") != frozen["command"] or linker.get("policy") != frozen["policy"] or \
                linker.get("threads1") != frozen["threads1"]:
            problems.append(f"wild-linker {role} command or options are not the frozen ones for {kind!r}")
        elif role == "comparator" and not (isinstance(version, str) and version.startswith(frozen["version"])):
            problems.append(f"wild-linker comparator version {version!r} is not a {kind} version")
        elif role == "wild" and not (isinstance(version, str) and WILD_IDENTITY.fullmatch(version)):
            problems.append(f"wild-linker wild version {version!r} is not Wild's own identity")
        if role == "comparator" and linker.get("substituted") is not (kind != "mold"):
            problems.append("wild-linker comparator substitution flag does not match its kind")
        if not (isinstance(version, str) and version.strip()):
            problems.append(f"wild-linker {role} has no recorded version output")
        if not all(isinstance(linker.get(key), str) and linker[key].startswith("/") and "'" not in linker[key] and
                   '"' not in linker[key] for key in ("path", "realpath")):
            problems.append(f"wild-linker {role} path is not a recorded absolute path")
        if not (isinstance(linker.get("sha256"), str) and SHA256.fullmatch(linker["sha256"])) or \
                linker.get("sha256_after") != linker.get("sha256"):
            problems.append(f"wild-linker {role} SHA-256 is missing or changed during the leg")
        usable[role] = linker
    if usable.get("wild", {}).get("sha256") == usable.get("comparator", {}).get("sha256"):
        problems.append("wild-linker Wild and its comparator are the same executable")
    root = identity.get("build_root")
    if not (isinstance(root, str) and root.startswith("/") and "'" not in root and "\n" not in root and
            os.path.normpath(root) == root):
        problems.append(f"wild-linker build root {root!r} is not a recorded absolute path")
    harness = identity.get("harness") if isinstance(identity.get("harness"), dict) else {}
    if sorted(harness) != ["wild_linker_acceptance.c", "wild_linker_bench.c"] or \
            not all(isinstance(value, str) and SHA256.fullmatch(value) for value in harness.values()):
        problems.append("wild-linker harness sources are not both identified")
    return usable, problems


def validate_wild_linker(status: object, files: object, head: str, expected_identity: object = None) -> list[str]:
    """Reasons the evidence is not one complete WILD_LINKER_PROFILE run of these linkers on these inputs.

    status is the harness exit; files maps wild_linker_bundle_files() keys to their exported text, and
    expected_identity, when given, is the receipt's copy of the identity record, which must match the export.
    """
    profile = WILD_LINKER_PROFILE
    files = files if isinstance(files, dict) else {}
    problems: list[str] = []
    if type(status) is not int or status != 0:
        problems.append(f"wild-linker harness exited {status!r}, not 0")
    missing = [key for key in wild_linker_bundle_files() if not isinstance(files.get(key), str)]
    if missing:
        return [*problems, f"wild-linker evidence lacks {', '.join(missing)}"]
    try:
        identity = json.loads(files["identity"], object_pairs_hook=_unique_pairs)
    except ValueError:
        identity = None
    if expected_identity is not None and identity != expected_identity:
        problems.append("wild-linker exported identity record differs from the receipt")
    linkers, found = _wild_linker_identity_problems(identity, head)
    problems.extend(found)
    if files["affinity"] != "affinity=%s\n" % ",".join(str(cpu) for cpu in profile["cpus"]):
        problems.append(f"wild-linker harness affinity {files['affinity'].strip()!r} is not CPUs {profile['cpus']!r}")
    if files["completion"] != "link_only=PASS\n":
        problems.append("wild-linker harness did not record a complete link-only pass")
    inputs = identity.get("inputs") if isinstance(identity, dict) and isinstance(identity.get("inputs"), dict) else {}
    for cell in profile["cells"]:
        manifest = files[f"inputs {cell}"]
        lines = manifest.splitlines()
        if not lines or not all(re.fullmatch(r"[0-9a-f]{64}  \S.*", line) for line in lines) or \
                not any(line.endswith((".o", ".a")) for line in lines):
            problems.append(f"wild-linker {cell} input manifest is empty or malformed")
        if inputs.get(cell) != hashlib.sha256(manifest.encode()).hexdigest():
            problems.append(f"wild-linker {cell} input manifest is not the one the receipt records")
        for linker in WILD_LINKER_LINKERS:
            if not SHA256.fullmatch(files[f"output {cell} {linker}"].strip()):
                problems.append(f"wild-linker {cell} {linker} output has no SHA-256")
    _, cell_problems = wild_linker_cells(files["raw"], files["summary"])
    problems.extend(cell_problems)
    rows = _csv_rows(files["raw"]) or []
    # Every setup step ran (the harness stops on the first failure); a timed or check row never failed.
    counts = {"validated-link": 6, "output-identity": 6, "linked-compiler-smoke": 6, "same-linker-repeat": 6,
              "same-linker-byte-identity": 6, "unchanged-object-manifest": 3, "freeze-object-manifest": 3,
              "capture-direct-linker": 3}
    for name, count in counts.items():
        if [row.get("exit") for row in rows if row.get("cell") == name] != ["0"] * count:
            problems.append(f"wild-linker {name} did not pass {count} times")
    root = identity.get("build_root") if isinstance(identity, dict) and isinstance(identity.get("build_root"), str) else ""
    expected = {}
    for cell in profile["cells"]:
        commands, argv_problems = wild_linker_cell_commands(cell, files[f"argv {cell}"], files[f"inputs {cell}"],
                                                            root, linkers)
        expected[cell] = commands
        problems.extend(argv_problems)
        wild = linkers.get("wild", {}).get("version")
        comments = {linker: files[f"comment {cell} {linker}"].splitlines() for linker in WILD_LINKER_LINKERS}
        if not isinstance(wild, str) or WILD_COMMENT_PREFIX + wild not in comments["wild"]:
            problems.append(f"wild-linker {cell} Wild output does not carry the recorded Wild identity in .comment")
        if any(WILD_IDENTITY.fullmatch(line.removeprefix(WILD_COMMENT_PREFIX)) for line in comments["comparator"]):
            problems.append(f"wild-linker {cell} comparator output carries a Wild identity in .comment")
    # Every timed command, and each validated and repeated link, is exactly the command derived from the
    # cell's retained argv and the frozen options: no other input, linker or shell text can run.
    commands = {}
    for line in files["commands"].splitlines():
        fields = line.split("\t", 4)
        if len(fields) == 5:
            commands[fields[0]] = fields
    timed = {}
    for cell in profile["cells"]:
        timed.update({name: (cell, first, second) for name, _, first, second in wild_linker_population()
                      if name.startswith(cell + "-")})
    current = None
    for row in rows:
        name = row.get("cell")
        fields = commands.get(row.get("sequence"))
        command = fields[4] if fields and fields[1:4] == [name, row.get("variant"), row.get("round")] else None
        if name in timed and row.get("variant") in ("A", "B"):
            current, first, second = timed[name]
            role = first if row["variant"] == "A" else second
            threads = "threads1" if name.endswith("-threads1") else "default"
            allowed = {(expected.get(current) or {}).get((role, threads))}
        elif name in ("validated-link", "same-linker-repeat") and current is not None:
            allowed = {(expected.get(current) or {}).get((role, "default")) for role in WILD_LINKER_LINKERS}
        else:
            continue
        if command is None or command not in allowed:
            problems.append(f"wild-linker {name} {row.get('variant')} round {row.get('round')} is not the command "
                            "derived from the retained argv and the frozen options")
            break
    return problems


def _link_tls_invocation(value: object) -> int | None:
    """Wall nanoseconds of one well-formed timed link that exited 0, else None."""
    result = None
    if isinstance(value, dict) and set(value) == {"exit", "wall_ns", "cpu_ns"} and type(value["exit"]) is int and \
            value["exit"] == 0 and type(value["wall_ns"]) is int and 0 < value["wall_ns"] <= LINK_TLS_SAMPLE_LIMIT_NS and \
            type(value["cpu_ns"]) is int and 0 <= value["cpu_ns"] <= LINK_TLS_SAMPLE_LIMIT_NS:
        result = value["wall_ns"]
    return result


def link_tls_cell_statistics(cell: object) -> tuple[dict | None, list[str]]:
    """Re-derive one cell's statistics from its identity record and raw invocations; (statistics, problems)."""
    profile = LINK_TLS_PROFILE
    name = cell.get("name") if isinstance(cell, dict) else None
    label = f"link-tls {name if isinstance(name, str) else '?'}"
    expected = next((item for item in profile["cells"] if item["name"] == name), None)
    if expected is None or cell.get("arguments") != expected["arguments"]:
        return None, [f"{label} is not a cell of the frozen profile"]
    problems: list[str] = []
    identity = cell.get("identity")
    keys = {"exit", "sha256", "size_bytes", "run_exit", "stdout"}
    if not isinstance(identity, dict) or set(identity) != set(LINK_TLS_ROLES) or \
            any(not isinstance(identity[role], dict) or set(identity[role]) != keys for role in LINK_TLS_ROLES):
        problems.append(f"{label} identity record is malformed")
    else:
        for role in LINK_TLS_ROLES:
            record = identity[role]
            if record["exit"] != 0 or record["run_exit"] != 0 or record["stdout"] != expected["stdout"] or \
                    not isinstance(record["sha256"], str) or not re.fullmatch("[0-9a-f]{64}", record["sha256"]) or \
                    type(record["size_bytes"]) is not int or record["size_bytes"] <= 0:
                problems.append(f"{label} {role} image did not link, run and print the frozen output")
        if identity["baseline"]["sha256"] != identity["candidate"]["sha256"] or \
                identity["baseline"]["size_bytes"] != identity["candidate"]["size_bytes"]:
            problems.append(f"{label} baseline and candidate images are not byte-identical")
    warmups = cell.get("warmups")
    if not isinstance(warmups, dict) or set(warmups) != set(LINK_TLS_ROLES) or \
            any(not isinstance(warmups[role], list) or len(warmups[role]) != profile["warmups"] for role in LINK_TLS_ROLES):
        problems.append(f"{label} does not have {profile['warmups']} warmups per binary")
    elif any(_link_tls_invocation(item) is None for role in LINK_TLS_ROLES for item in warmups[role]):
        problems.append(f"{label} has a failed or malformed warmup")
    pairs = cell.get("pairs")
    ratios: list[float] = []
    walls = {role: [] for role in LINK_TLS_ROLES}
    if not isinstance(pairs, list) or len(pairs) != profile["pairs"]:
        problems.append(f"{label} does not have exactly {profile['pairs']} pairs")
        pairs = []
    for index, pair in enumerate(pairs):
        order = ["baseline", "candidate"] if index % 2 == 0 else ["candidate", "baseline"]
        if not isinstance(pair, dict) or pair.get("order") != order or set(pair) != {"order", *LINK_TLS_ROLES}:
            problems.append(f"{label} pair {index} is malformed or out of the fixed order")
            continue
        values = {role: _link_tls_invocation(pair[role]) for role in LINK_TLS_ROLES}
        if values["baseline"] is None or values["candidate"] is None:
            problems.append(f"{label} pair {index} has a failed or malformed link")
            continue
        for role in LINK_TLS_ROLES:
            walls[role].append(values[role])
        ratios.append(values["candidate"] / values["baseline"])
    if problems or len(ratios) != profile["pairs"]:
        return None, problems
    low, high = compiler_probes_interval(ratios)
    return {"name": expected["name"], "baseline_ns": statistics.median(walls["baseline"]),
            "candidate_ns": statistics.median(walls["candidate"]), "ratio": statistics.median(ratios),
            "ratio_low": low, "ratio_high": high, "candidate_faster": sum(1 for ratio in ratios if ratio < 1.0),
            "image_sha256": identity["baseline"]["sha256"]}, []


def link_tls_digest(summary: object) -> list[dict]:
    """The re-derived per-cell statistics of a link-tls summary; malformed cells are left out."""
    digest = []
    cells = summary.get("cells") if isinstance(summary, dict) else None
    for cell in cells if isinstance(cells, list) else []:
        statistics_value, unused_problems = link_tls_cell_statistics(cell)
        if statistics_value is not None:
            digest.append(statistics_value)
    return digest


def validate_link_tls(status: object, summary: object, binaries: object) -> list[str]:
    """Reasons the evidence is not one complete LINK_TLS_PROFILE run of these binaries.

    status is link_tls_bench.py's exit and summary its summary.json; binaries is the receipt's
    role -> {"sha256", ...} record of the measured ide binaries, hashed by compiler_compare itself.
    Every statistic is re-derived from the raw samples; the harness states none.
    """
    label = "link-tls"
    profile = LINK_TLS_PROFILE
    problems: list[str] = []
    if type(status) is not int or status != 0:
        problems.append(f"{label} exited {status!r}, not 0")
    if not isinstance(summary, dict):
        return [*problems, f"{label} summary is missing or malformed"]
    for key, wanted in (("schema", LINK_TLS_SCHEMA), ("profile", profile["name"]), ("cpu", profile["cpu"]),
                        ("status", "complete"), ("errors", [])):
        if summary.get(key) != wanted:
            problems.append(f"{label} summary {key} is {summary.get(key)!r}, not {wanted!r}")
    hashes = {role: binaries[role].get("sha256") if isinstance(binaries, dict) and isinstance(binaries.get(role), dict)
              else None for role in LINK_TLS_ROLES}
    if any(not isinstance(value, str) for value in hashes.values()) or summary.get("binaries") != hashes:
        problems.append(f"{label} did not measure the recorded baseline and candidate binaries")
    elif hashes["baseline"] == hashes["candidate"]:
        problems.append(f"{label} baseline and candidate are the same binary")
    inputs = summary.get("inputs")
    if not isinstance(inputs, dict) or set(inputs) != set(profile["inputs"]) or \
            any(not isinstance(value, str) or not re.fullmatch("[0-9a-f]{64}", value) for value in inputs.values()):
        problems.append(f"{label} inputs are not the frozen input set, each with its sha256")
    toolchain = summary.get("toolchain")
    if not isinstance(toolchain, dict) or set(toolchain) != {"clang", "ar"} or \
            any(not isinstance(value, str) or not value for value in toolchain.values()):
        problems.append(f"{label} does not record the clang and ar that built its inputs")
    cells = summary.get("cells")
    if not isinstance(cells, list) or [cell.get("name") if isinstance(cell, dict) else None for cell in cells] != \
            [cell["name"] for cell in profile["cells"]]:
        problems.append(f"{label} cells are not the frozen cells in order")
        cells = []
    for cell in cells:
        problems.extend(link_tls_cell_statistics(cell)[1])
    return problems


def validate_selfhost_inline(stage1: object, result: object, binaries: object) -> list[str]:
    """Reasons the stage-1 and result documents are not one complete SELFHOST_INLINE_PROFILE run.

    stage1 maps baseline/candidate to selfhost_inline_compare.py's stage1.json; result is its result.json;
    binaries is the receipt's role -> {"sha256", "revision", ...} record, hashed by compiler_compare itself.
    The timed uarch_lab documents are checked by selfhost_inline_compare.validate_compare_documents.
    """
    label = "self-host inline"
    profile = SELFHOST_INLINE_PROFILE
    problems: list[str] = []
    roles = ("baseline", "candidate")
    if not isinstance(stage1, dict) or set(stage1) != set(roles):
        return [f"{label} stage-1 records are missing"]
    shas: dict = {}
    for role in roles:
        record = stage1[role]
        expected = binaries.get(role) if isinstance(binaries, dict) and isinstance(binaries.get(role), dict) else {}
        if not isinstance(record, dict) or record.get("schema") != SELFHOST_INLINE_STAGE1_SCHEMA or \
                record.get("status") != "complete":
            problems.append(f"{label} {role} stage-1 record is malformed or incomplete")
            continue
        if record.get("revision") != expected.get("revision") or not isinstance(expected.get("revision"), str):
            problems.append(f"{label} {role} stage-1 compiler was not built from the {role} revision")
        compiler = record.get("compiler") if isinstance(record.get("compiler"), dict) else {}
        if compiler.get("sha256") != expected.get("sha256") or not isinstance(expected.get("sha256"), str):
            problems.append(f"{label} {role} stage-1 compiler was not built by the recorded {role} binary")
        if record.get("command") != profile["stage1_command"]:
            problems.append(f"{label} {role} stage-1 command is not the frozen command")
        built = record.get("stage1") if isinstance(record.get("stage1"), dict) else {}
        if not re.fullmatch("[0-9a-f]{64}", str(built.get("sha256", ""))) or type(built.get("size_bytes")) is not int or \
                built["size_bytes"] <= 0:
            problems.append(f"{label} {role} stage-1 compiler identity is malformed")
        else:
            shas[role] = built["sha256"]
    if not isinstance(result, dict):
        return [*problems, f"{label} result is missing or malformed"]
    for key, wanted in (("schema", SELFHOST_INLINE_SCHEMA), ("status", "complete"), ("profile", profile["name"])):
        if result.get(key) != wanted:
            problems.append(f"{label} result {key} is {result.get(key)!r}, not {wanted!r}")
    baseline = binaries.get("baseline") if isinstance(binaries, dict) and isinstance(binaries.get("baseline"), dict) else {}
    if result.get("base_revision") != baseline.get("revision"):
        problems.append(f"{label} did not time the frozen merge-base tree")
    if len(shas) == len(roles) and result.get("stage1_compilers") != {role: {"sha256": shas[role]} for role in roles}:
        problems.append(f"{label} timed compilers are not the recorded stage-1 compilers")
    return problems


def _finite_number(value: object) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def selfhost_inline_frontend_problems(summary: object) -> list[str]:
    """Reasons a self-host inline summary lacks usable paired front-end topdown evidence.

    Every topdown row must be well formed; the level-1 front-end-bound metric (a name ending in
    frontend_bound, as uarch_lab's dominant_category reads it) must be present with finite values for
    both compilers; and no front-end row may carry a value for only one of them. Other groups (cache,
    backend) are reported but never stand in for it, because step_topdown marks the step ok even when a
    group fails or is unavailable."""
    label = "self-host inline"
    profile = summary.get("profile") if isinstance(summary, dict) and isinstance(summary.get("profile"), dict) else {}
    topdown = profile.get("topdown") if isinstance(profile.get("topdown"), dict) else {}
    rows = topdown.get("metrics")
    if topdown.get("status") != "ok" or not isinstance(rows, list) or not rows:
        return [f"{label} topdown metrics are missing"]
    problems: list[str] = []
    if any(not isinstance(row, dict) or not isinstance(row.get("group"), str) or not isinstance(row.get("metric"), str)
           for row in rows):
        problems.append(f"{label} topdown has a malformed metric row")
        rows = [row for row in rows if isinstance(row, dict)]
    frontend = [row for row in rows if SELFHOST_INLINE_FRONTEND.search(f"{row.get('group', '')} {row.get('metric', '')}")]
    if not any(str(row.get("metric", "")).lower().endswith("frontend_bound") and _finite_number(row.get("a")) and
               _finite_number(row.get("b")) for row in frontend):
        problems.append(f"{label} topdown lacks a paired level-1 frontend_bound metric")
    if any(_finite_number(row.get("a")) != _finite_number(row.get("b")) for row in frontend):
        problems.append(f"{label} topdown has a front-end metric measured for only one compiler")
    return problems


def selfhost_inline_digest(summary: object) -> dict | None:
    """The reported rows of a self-host inline uarch summary: paired metrics, code bytes, front-end topdown."""
    if not isinstance(summary, dict) or not isinstance(summary.get("metrics"), dict):
        return None
    metrics = {}
    for key in ("wall", "task_clock", "instructions", "cycles", "ipc", "branch_misses", "peak_rss"):
        row = summary["metrics"].get(key) if isinstance(summary["metrics"].get(key), dict) else {}
        metrics[key] = {name: row.get(name) for name in ("a_median", "b_median", "ratio", "ci_low", "ci_high", "outcome")}
    code = summary.get("code_bytes") if isinstance(summary.get("code_bytes"), dict) else {}
    profile = summary.get("profile") if isinstance(summary.get("profile"), dict) else {}
    topdown = profile.get("topdown") if isinstance(profile.get("topdown"), dict) else {}
    rows = [row for row in topdown.get("metrics") or [] if isinstance(row, dict) and
            SELFHOST_INLINE_FRONTEND.search(f"{row.get('group', '')} {row.get('metric', '')}")]
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) else {}
    return {"metrics": metrics, "code_bytes": {name: code.get(name) for name in ("a_value", "b_value", "ratio")},
            "frontend": [{name: row.get(name) for name in ("group", "metric", "unit", "a", "b", "relative_change_percent")}
                         for row in rows[:SELFHOST_INLINE_TOPDOWN_ROWS]],
            "outputs_identical": summary.get("outputs_identical"), "verdict": verdict.get("text"),
            "warnings": [item for item in summary.get("warnings") or [] if isinstance(item, str)]}


def _shell_quote(text: str) -> str:
    """The harness's quote(): one single-quoted shell word."""
    return "'" + text.replace("'", "'\\''") + "'"


def _system_path(path: str) -> bool:
    """An absolute path that stays under /usr, /lib, /lib64 or /lib32 once `..` is resolved."""
    normal = os.path.normpath(path)
    roots = ("/usr", "/lib", "/lib64", "/lib32")
    return path.startswith("/") and (normal in roots or normal.startswith(tuple(root + "/" for root in roots)))


def _contained(path: str, directory: str) -> bool:
    """Whether path is already canonical (no `..`, `.` or empty component) and lies strictly inside directory,
    so a raw-string prefix or membership match cannot hide a traversal out of the cell's tree."""
    return os.path.normpath(path) == path and os.path.normpath(directory) == directory and \
        path.startswith(directory + "/")


def _wild_argv_grammar_problems(cell: str, arguments: list[str], config: str) -> list[str]:
    """Reasons the arguments after the linker are not the frozen Clang link grammar (WILD_ARGV_FLAGS and friends)."""
    problems: list[str] = []
    index = 0
    while index < len(arguments) and not problems:
        token = arguments[index]
        if token.startswith("@"):
            problems.append(f"wild-linker {cell} retained argv passes a response file {token!r}, which Wild would "
                            "expand without it being hashed")
        elif token in WILD_ARGV_OPERANDS:
            operand = arguments[index + 1] if index + 1 < len(arguments) else None
            allowed = WILD_ARGV_OPERANDS[token]
            if operand is None or (allowed is not None and operand not in allowed) or \
                    (token == "-dynamic-linker" and not _system_path(operand)) or \
                    (token == "-o" and operand != f"{config}/ide"):
                problems.append(f"wild-linker {cell} retained argv gives {token} the unadmitted operand {operand!r}")
            index += 1
        elif token == f"--dependency-file=CMakeFiles/ide.dir/{config}/link.d":
            # CMake's linker-written link dependencies (CMAKE_LINK_DEPENDS_USE_LINKER) name a file in the
            # target's own directory; cmake 4.4.4 on the 9700X passes it (run 38255545884).
            pass
        elif token.startswith("-L"):
            if not _system_path(token[2:]):
                problems.append(f"wild-linker {cell} retained argv searches {token[2:]!r}, which is not a system directory")
        elif token.startswith("-"):
            if token not in WILD_ARGV_FLAGS and not WILD_ARGV_LIBRARY.fullmatch(token):
                problems.append(f"wild-linker {cell} retained argv passes the unadmitted option {token!r}")
        elif not token.endswith((".o", ".a")):
            problems.append(f"wild-linker {cell} retained argv passes {token!r}, which is not an object or archive")
        index += 1
    return problems


def wild_linker_cell_commands(cell: str, argv: str, manifest: str, root: str, linkers: dict) -> tuple[dict, list[str]]:
    """({(role, default|threads1): command}, problems) for one cell, from its retained Wild argv.

    The argv must start with the recorded Wild, write the cell's ide, and take every relative object or
    archive from the cell's hashed input manifest; system startup objects must come from /usr or /lib.
    """
    profile = WILD_LINKER_PROFILE
    tree, config = profile["cell_trees"][cell]
    directory = f"{root}/{tree}"
    problems: list[str] = []
    wild = linkers.get("wild", {}).get("path")
    comparator = linkers.get("comparator", {})
    try:
        tokens = shlex.split(argv) if "\n" not in argv.rstrip("\n") else None
    except ValueError:
        tokens = None
    argv = argv.rstrip("\n")
    # The retained argv must be exactly Clang's form, one double-quoted inert word per token; a packet
    # that appends shell syntax (`; :`) and rewrites its commands to match is refused here, and the
    # expected commands are rebuilt from the admitted tokens rather than from the raw text.
    if tokens is not None and (not all(WILD_ARGV_TOKEN.match(token) for token in tokens) or
                               argv != " ".join(f'"{token}"' for token in tokens)):
        problems.append(f"wild-linker {cell} retained argv is not one double-quoted shell-inert word per argument")
        tokens = None
    argv = " ".join(f'"{token}"' for token in tokens or [])
    listed = {line[66:] for line in manifest.splitlines() if len(line) > 66}
    if not tokens or not isinstance(wild, str) or tokens[0] != wild or not argv.startswith(f'"{wild}" '):
        problems.append(f"wild-linker {cell} retained argv does not start with the recorded Wild")
        tokens = tokens or []
    problems.extend(_wild_argv_grammar_problems(cell, tokens[1:], config))
    outputs = [tokens[index + 1] for index, token in enumerate(tokens[:-1]) if token == "-o"]
    if outputs != [f"{config}/ide"]:
        problems.append(f"wild-linker {cell} retained argv does not write {config}/ide")
    objects = [token for token in tokens[1:] if token.endswith((".o", ".a"))]
    local = [token for token in objects if not token.startswith("/")]
    if not local:
        problems.append(f"wild-linker {cell} retained argv links no object of the head")
    for token in objects:
        path = token if token.startswith("/") else f"{directory}/{token}"
        if token.startswith("/") and ((root and token.startswith(root + "/")) or not _system_path(token)):
            problems.append(f"wild-linker {cell} links {token!r}, which is neither a hashed input nor a system file")
            break
        if not token.startswith("/") and not _contained(path, directory):
            problems.append(f"wild-linker {cell} links {token!r}, which leaves {tree} once normalised")
            break
        if not token.startswith("/") and path not in listed:
            problems.append(f"wild-linker {cell} links {token!r}, which its input manifest does not hash")
            break
    if any(not _contained(path, directory) for path in listed):
        problems.append(f"wild-linker {cell} input manifest lists files outside {tree}")
    common = f" --no-gc-sections --build-id={WILD_LINKER_BUILD_ID}"
    prefix = f"cd {_shell_quote(directory)} && "
    commands = {}
    if tokens and isinstance(wild, str) and tokens[0] == wild and isinstance(comparator.get("path"), str):
        candidate = f"{prefix}{argv}{common} {profile['wild']['policy']}"
        reference = (f"{prefix}{_shell_quote(comparator['path'])}{argv[len(wild) + 2:]}{common} "
                     f"{comparator.get('policy')}")
        commands = {("wild", "default"): candidate, ("wild", "threads1"): f"{candidate} {profile['wild']['threads1']}",
                    ("comparator", "default"): reference,
                    ("comparator", "threads1"): f"{reference} {comparator.get('threads1')}"}
    return commands, problems


def validate_ast_retirement_acceptance(summary: object, base: str, head: str, binaries: dict) -> list[str]:
    """Validate #4058's fixed two-binary population; the publisher replays raw cells."""
    label = "issue #4058 AST retirement receipt"
    if not isinstance(summary, dict) or summary.get("schema") != AST_RETIREMENT_SCHEMA or summary.get("status") != "complete":
        return [f"{label} is missing or incomplete"]
    if not isinstance(binaries, dict) or any(not isinstance(binaries.get(role), dict) for role in ("baseline", "candidate")):
        return [f"{label} prepared compiler identities are missing"]
    problems = []
    if summary.get("profile") != AST_RETIREMENT_PROFILE or summary.get("base_revision") != base or summary.get("head_revision") != head:
        problems.append(f"{label} profile or source revisions differ from the authorized comparison")
    expected = {role: {key: binaries.get(role, {}).get(key) for key in ("sha256", "size_bytes", "revision")}
                for role in ("baseline", "candidate")}
    if summary.get("binaries") != expected or expected["baseline"]["revision"] != base or expected["candidate"]["revision"] != head:
        problems.append(f"{label} compiler identities differ from the prepared binaries")
    if not isinstance(summary.get("cpu_model"), str) or not APPROVED_HOST.search(summary["cpu_model"]):
        problems.append(f"{label} was not measured on the approved Ryzen 7 9700X")
    source = summary.get("source")
    if not isinstance(source, dict) or source.get("revision") != base:
        problems.append(f"{label} source is not the frozen baseline/current-main revision")
    else:
        trees = source.get("frozen_input_tree")
        if not isinstance(trees, dict) or set(trees) != {"src", "build/generated"} or any(
                not isinstance(row, dict) or not SHA256.fullmatch(str(row.get("sha256", ""))) or
                type(row.get("files")) is not int or row["files"] <= 0 or type(row.get("bytes")) is not int or row["bytes"] <= 0
                for row in trees.values()):
            problems.append(f"{label} frozen source/header tree identities are incomplete")
        corpus = source.get("corpus")
        if not isinstance(corpus, dict) or set(corpus) != set(AST_RETIREMENT_CORPUS):
            problems.append(f"{label} corpus is not the six unchanged CI sources")
        else:
            for name, row in corpus.items():
                if not isinstance(row, dict) or row.get("path") != f"{AST_RETIREMENT_INPUT_DIRECTORY}/{name}.c" or \
                        not SHA256.fullmatch(str(row.get("sha256", ""))) or type(row.get("size_bytes")) is not int or row["size_bytes"] <= 0:
                    problems.append(f"{label} corpus identity is malformed for {name}")
        unity = source.get("unity")
        if not isinstance(unity, dict) or unity.get("path") != "src/buster/apps/ide/ide.c" or \
                not SHA256.fullmatch(str(unity.get("sha256", ""))) or type(unity.get("size_bytes")) is not int or unity["size_bytes"] <= 0:
            problems.append(f"{label} unity source identity is missing")
        generator = source.get("generator")
        if not isinstance(generator, dict) or generator.get("revision") != base or \
                not SHA256.fullmatch(str(generator.get("sha256", ""))) or type(generator.get("size_bytes")) is not int or generator["size_bytes"] <= 0:
            problems.append(f"{label} baseline generator identity is missing")
    cells, probes = summary.get("cells"), summary.get("probes")
    if not isinstance(cells, dict) or set(cells) != set(AST_RETIREMENT_CELLS) or \
            not isinstance(probes, dict) or set(probes) != set(AST_RETIREMENT_CELLS):
        problems.append(f"{label} does not contain all twenty cells and retained-byte probes")
    else:
        for cell, row in cells.items():
            metrics = row.get("metrics") if isinstance(row, dict) else None
            if not isinstance(metrics, dict) or any(not isinstance(metrics.get(name), dict) for name in ("wall", "instructions", "peak_rss")):
                problems.append(f"{label} has missing metrics for {cell}")
                continue
            if any(not _finite_positive(metrics["wall"].get(name)) for name in ("a_median", "b_median", "ratio", "ci_low", "ci_high")) or \
                    any(not _finite_positive(metrics["instructions"].get(name)) for name in ("a_median", "b_median", "ratio")) or \
                    any(not _finite_positive(metrics["peak_rss"].get(name)) for name in ("a_median", "b_median")):
                problems.append(f"{label} required native metrics are unavailable or invalid for {cell}")
            if row.get("outputs_identical") is not True or row.get("diagnostics_identical") is not True:
                problems.append(f"{label} object/diagnostic identity failed for {cell}")
            probe = probes[cell]
            if not isinstance(probe, dict) or set(probe) != {"baseline", "candidate"}:
                problems.append(f"{label} retained-byte probe is incomplete for {cell}")
                continue
            for role, retained in probe.items():
                if not isinstance(retained, dict) or type(retained.get("tree_retained_bytes")) is not int or retained["tree_retained_bytes"] <= 0 or \
                        type(retained.get("fact_retained_bytes")) is not int or retained["fact_retained_bytes"] < 0:
                    problems.append(f"{label} retained tree/fact measurements are missing for {cell}/{role}")
    return problems


def validate_ast_pilot_acceptance(summary: object, expected_revision: str, expected_candidate_sha: str | None = None) -> list[str]:
    """Validate opt-in #3397 evidence shape and re-derive its budget verdict."""
    label = "issue #3397 ast-pilot receipt"
    if not isinstance(summary, dict) or summary.get("schema") != AST_PILOT_ACCEPTANCE_SCHEMA or summary.get("status") != "complete":
        return [f"{label} has an unexpected or incomplete schema"]
    problems = []
    if summary.get("source_revision") != expected_revision:
        problems.append(f"{label} source revision does not match candidate HEAD")
    profile = summary.get("profile")
    if not isinstance(profile, dict) or set(profile) != set(AST_PILOT_ACCEPTANCE_PROFILE) | {"source_sha256"} or \
            any(profile.get(key) != value for key, value in AST_PILOT_ACCEPTANCE_PROFILE.items()) or \
            not SHA256.fullmatch(str(profile.get("source_sha256", ""))):
        problems.append(f"{label} profile does not match the fixed acceptance profile")
    candidate = summary.get("candidate_compiler")
    if not isinstance(candidate, dict) or not SHA256.fullmatch(str(candidate.get("sha256", ""))):
        problems.append(f"{label} candidate compiler identity is missing")
    elif expected_candidate_sha and candidate.get("sha256") != expected_candidate_sha:
        problems.append(f"{label} timed compiler differs from the frozen candidate-HEAD binary")
    cells = summary.get("cells")
    if not isinstance(cells, dict) or set(cells) != set(AST_PILOT_CELLS):
        return [*problems, f"{label} does not hold exactly the syntax-only and object cells"]
    metrics = {}
    for cell in AST_PILOT_CELLS:
        row = cells[cell] if isinstance(cells[cell], dict) else {}
        values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
        wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
        rss = values.get("peak_rss") if isinstance(values.get("peak_rss"), dict) else {}
        instructions = values.get("instructions") if isinstance(values.get("instructions"), dict) else None
        if not all(_finite_positive(wall.get(key)) for key in ("a_median", "b_median", "ratio", "ci_low", "ci_high")):
            problems.append(f"{label} {cell} wall-time measurements are missing")
        if not all(_finite_positive(rss.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"{label} {cell} peak-RSS measurements are missing")
        if instructions is None:
            problems.append(f"{label} {cell} retired-instruction metric is missing")
        elif instructions.get("ratio") is None:
            counters = values.get("counter_availability")
            if not isinstance(counters, dict) or counters.get("perf_stat") is not False or not counters.get("reason"):
                problems.append(f"{label} {cell} retired-instruction NA has no unavailable-counter reason")
        elif not _finite_positive(instructions.get("ratio")) or \
                not all(_finite_positive(instructions.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"{label} {cell} retired-instruction counts are invalid")
        for key in ("outputs_identical", "diagnostics_identical"):
            if type(row.get(key)) is not bool:
                problems.append(f"{label} {cell} {key} is not recorded")
        metrics[cell] = values
    tree = summary.get("tree")
    retained = tree.get("retained_bytes") if isinstance(tree, dict) else None
    # Bounded first: ast_pilot_budget_verdict multiplies retained bytes by a float.
    if type(retained) is not int or not _finite_positive(retained) or not isinstance(tree.get("nodes"), int) or \
            tree["nodes"] <= 0 or \
            tree.get("a_tree_lines") != 0:
        problems.append(f"{label} tree retained bytes are missing, or variant A built a tree")
    if problems:
        return problems
    correctness = {"object_outputs_identical": cells["object"]["outputs_identical"],
                   "diagnostics_identical": all(cells[cell]["diagnostics_identical"] for cell in AST_PILOT_CELLS)}
    if summary.get("verdict") != ast_pilot_budget_verdict(metrics, retained, correctness):
        problems.append(f"{label} verdict differs from the budget recomputed from its metrics")
    return problems


def validate_constant_reuse_acceptance(summary: object, expected_revision: str, expected_candidate_sha: str | None = None) -> list[str]:
    """Validate opt-in #3011 evidence shape: both cells complete, measured and diagnostics-identical."""
    label = "issue #3011 constant-reuse receipt"
    if not isinstance(summary, dict) or summary.get("schema") != CONSTANT_REUSE_ACCEPTANCE_SCHEMA or summary.get("status") != "complete":
        return [f"{label} has an unexpected or incomplete schema"]
    problems = []
    if summary.get("source_revision") != expected_revision:
        problems.append(f"{label} source revision does not match candidate HEAD")
    profile = summary.get("profile")
    if not isinstance(profile, dict) or set(profile) != set(CONSTANT_REUSE_ACCEPTANCE_PROFILE) | {"source_sha256"} or \
            any(profile.get(key) != value for key, value in CONSTANT_REUSE_ACCEPTANCE_PROFILE.items()) or \
            not SHA256.fullmatch(str(profile.get("source_sha256", ""))):
        problems.append(f"{label} profile does not match the fixed acceptance profile")
    candidate = summary.get("candidate_compiler")
    if not isinstance(candidate, dict) or not SHA256.fullmatch(str(candidate.get("sha256", ""))):
        problems.append(f"{label} candidate compiler identity is missing")
    elif expected_candidate_sha and candidate.get("sha256") != expected_candidate_sha:
        problems.append(f"{label} timed compiler differs from the frozen candidate-HEAD binary")
    cells = summary.get("cells")
    if not isinstance(cells, dict) or set(cells) != set(CONSTANT_REUSE_CELLS):
        return [*problems, f"{label} does not hold exactly the intern and cse cells"]
    for cell in CONSTANT_REUSE_CELLS:
        row = cells[cell] if isinstance(cells[cell], dict) else {}
        values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
        wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
        rss = values.get("peak_rss") if isinstance(values.get("peak_rss"), dict) else {}
        code = values.get("code_bytes") if isinstance(values.get("code_bytes"), dict) else {}
        instructions = values.get("instructions") if isinstance(values.get("instructions"), dict) else None
        if not all(_finite_positive(wall.get(key)) for key in ("a_median", "b_median", "ratio", "ci_low", "ci_high")):
            problems.append(f"{label} {cell} wall-time measurements are missing")
        if not all(_finite_positive(rss.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"{label} {cell} peak-RSS measurements are missing")
        if not all(_finite_positive(code.get(key)) for key in ("a_value", "b_value")):
            problems.append(f"{label} {cell} executable-section code bytes are missing")
        if instructions is None:
            problems.append(f"{label} {cell} retired-instruction metric is missing")
        elif instructions.get("ratio") is None:
            counters = values.get("counter_availability")
            if not isinstance(counters, dict) or counters.get("perf_stat") is not False or not counters.get("reason"):
                problems.append(f"{label} {cell} retired-instruction NA has no unavailable-counter reason")
        elif not _finite_positive(instructions.get("ratio")) or \
                not all(_finite_positive(instructions.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"{label} {cell} retired-instruction counts are invalid")
        if row.get("diagnostics_identical") is not True:
            problems.append(f"{label} {cell} diagnostics differ between the default and flagged compiles")
    return problems


def process_floor_verdict(cells: object) -> dict:
    """Issue #1125's stop rule over the cells' paired wall ratios (B/A).

    A control resolves the floor only when its ratio's 95% CI lies wholly
    outside the A/A cell's. Each `*_ms` value is A's median minus the
    control's, so a positive value is what the x86-64 object compile costs
    above that control; it is None when either median is missing.
    """
    def metric(cell: str, key: str) -> dict:
        row = cells.get(cell) if isinstance(cells, dict) and isinstance(cells.get(cell), dict) else {}
        values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
        return values.get(key) if isinstance(values.get(key), dict) else {}

    def above(values: dict) -> float | None:
        a, b = values.get("a_median"), values.get("b_median")
        return round((a - b) * 1000.0, 6) if _finite_positive(a) and _finite_positive(b) else None

    aa = metric("aa", "wall")
    aa_low, aa_high = aa.get("ci_low"), aa.get("ci_high")
    result: dict = {"aa_interval": [aa_low, aa_high]}
    for cell in ("aarch64", "syntax-only"):
        wall = metric(cell, "wall")
        low, high = wall.get("ci_low"), wall.get("ci_high")
        bounded = all(_finite_positive(value) for value in (low, high, aa_low, aa_high))
        result[cell] = {"ci": [low, high], "outside_aa": bool(bounded and (high < aa_low or low > aa_high)),
                        "wall_ms": above(wall), "task_clock_ms": above(metric(cell, "task_clock"))}
    return result


def validate_process_floor_acceptance(summary: object, expected_revision: str,
                                      expected_candidate_sha: str | None = None) -> list[str]:
    """Validate #1125 floor evidence: every cell complete and measured, A/A objects identical, verdict re-derived."""
    label = "issue #1125 process-floor receipt"
    if not isinstance(summary, dict) or summary.get("schema") != PROCESS_FLOOR_SCHEMA or summary.get("status") != "complete":
        return [f"{label} has an unexpected or incomplete schema"]
    problems = []
    if summary.get("source_revision") != expected_revision:
        problems.append(f"{label} source revision does not match candidate HEAD")
    if summary.get("profile") != PROCESS_FLOOR_PROFILE:
        problems.append(f"{label} profile does not match the fixed profile")
    candidate = summary.get("candidate_compiler")
    if not isinstance(candidate, dict) or not SHA256.fullmatch(str(candidate.get("sha256", ""))):
        problems.append(f"{label} candidate compiler identity is missing")
    elif expected_candidate_sha and candidate.get("sha256") != expected_candidate_sha:
        problems.append(f"{label} timed compiler differs from the frozen candidate-HEAD binary")
    cells = summary.get("cells")
    if not isinstance(cells, dict) or set(cells) != set(PROCESS_FLOOR_CELLS):
        return [*problems, f"{label} does not hold exactly the aa, aarch64 and syntax-only cells"]
    for cell in PROCESS_FLOOR_CELLS:
        row = cells[cell] if isinstance(cells[cell], dict) else {}
        values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
        wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
        task = values.get("task_clock") if isinstance(values.get("task_clock"), dict) else {}
        instructions = values.get("instructions") if isinstance(values.get("instructions"), dict) else None
        if not all(_finite_positive(wall.get(key)) for key in ("a_median", "b_median", "ratio", "ci_low", "ci_high")):
            problems.append(f"{label} {cell} wall-time measurements are missing")
        if not all(_finite_positive(task.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"{label} {cell} task-clock measurements are missing")
        # Peak RSS is NA when the compiler stays near the perf wrapper's own footprint; the compiler's own wall_ns
        # is NA when -fmetrics-out is unavailable for either command. A value that is present must be valid.
        for key in ("peak_rss", "compiler_wall"):
            optional = values.get(key)
            if not isinstance(optional, dict) or \
                    any(optional.get(field) is not None and not _finite_positive(optional.get(field))
                        for field in ("a_median", "b_median")):
                problems.append(f"{label} {cell} {key} record is missing or invalid")
        if instructions is None:
            problems.append(f"{label} {cell} retired-instruction metric is missing")
        elif instructions.get("ratio") is None:
            counters = values.get("counter_availability")
            if not isinstance(counters, dict) or counters.get("perf_stat") is not False or not counters.get("reason"):
                problems.append(f"{label} {cell} retired-instruction NA has no unavailable-counter reason")
        elif not _finite_positive(instructions.get("ratio")) or \
                not all(_finite_positive(instructions.get(key)) for key in ("a_median", "b_median")):
            problems.append(f"{label} {cell} retired-instruction counts are invalid")
        if row.get("diagnostics_identical") is not True:
            problems.append(f"{label} {cell} diagnostics differ between the two commands")
        if cell == "aa" and row.get("outputs_identical") is not True:
            problems.append(f"{label} aa objects differ between two runs of the same command")
    if summary.get("verdict") != process_floor_verdict(cells):
        problems.append(f"{label} verdict does not follow from its cells")
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


def analyzer_candidate_plan_problems(plan: dict, *, allow_fallback: bool = False) -> list[str]:
    """Validate the V2 representative-owned context envelope and alias map.

    Historical V1 receipts keep the original all-or-nothing duplicate-class
    rule. V2 may retain an exact invocation class as independent executions
    when the native planner could not prove a safe shared context; that class
    must then be entirely self-represented and have no proof envelope.
    """
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

        if allow_fallback and all(row["representative"] == row["index"] for row in members):
            unique += len(members)
            reason = members[0]["reason"]
            if not reason or reason == "single-row" or any(row["proven"] or row["reason"] != reason or row["context_proof_present"] or
                                 row["input_inventory"] or row["search_inventory"] for row in members):
                problems.append(f"candidate fallback invocation class rooted at {canonical['index']} has an invalid context envelope")
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


def analyzer_candidate_partition_counts(plan: dict) -> dict:
    """Recompute potential groups, proven alias groups, executions and aliases from plan rows."""
    classes: dict[tuple, list[dict]] = {}
    for row in plan.get("rows", []) if isinstance(plan, dict) else []:
        key = (row.get("directory"), row.get("file"), row.get("shard"), tuple(row.get("argv", [])))
        classes.setdefault(key, []).append(row)
    aliases = sum(row.get("representative") != row.get("index")
                  for row in plan.get("rows", []) if isinstance(row, dict))
    return {"selected_rows": len(plan.get("rows", [])),
            "unique_executions": len(plan.get("rows", [])) - aliases,
            "alias_rows": aliases,
            "candidate_groups": sum(len(members) > 1 for members in classes.values()),
            "proven_groups": sum(len(members) > 1 and any(row.get("representative") != row.get("index")
                                                            for row in members)
                                  for members in classes.values())}


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


ANALYZER_DATABASE_ENTRY_LIMIT = 16384


def analyzer_database_shell_split(command: str) -> list[str]:
    """Match the build driver's POSIX compile-command tokenizer for CMake's command field."""
    arguments: list[str] = []
    index = 0
    whitespace = " \t\n\r\v\f"
    while index < len(command):
        while index < len(command) and command[index] in whitespace:
            index += 1
        if index == len(command):
            break
        argument: list[str] = []
        while index < len(command) and command[index] not in whitespace:
            character = command[index]
            index += 1
            if character == "'":
                end = command.find("'", index)
                if end < 0:
                    raise ValueError("compile database command has an unterminated single quote")
                argument.extend(command[index:end])
                index = end + 1
            elif character == '"':
                while index < len(command) and command[index] != '"':
                    quoted = command[index]
                    index += 1
                    if quoted == "\\" and index < len(command):
                        quoted = command[index]
                        index += 1
                    argument.append(quoted)
                if index == len(command):
                    raise ValueError("compile database command has an unterminated double quote")
                index += 1
            elif character == "\\":
                if index == len(command):
                    raise ValueError("compile database command ends in an escape")
                argument.append(command[index])
                index += 1
            else:
                argument.append(character)
        arguments.append("".join(argument))
    return arguments


def analyzer_database_path_equal(left: str, right: str) -> bool:
    """Match the build driver's Linux path equality: slash folding and trailing slash trimming only."""
    while len(left) > 1 and left[-1] in "/\\":
        left = left[:-1]
    while len(right) > 1 and right[-1] in "/\\":
        right = right[:-1]
    return left.replace("\\", "/") == right.replace("\\", "/")


def analyzer_database_relative_path(directory: str, path: str) -> str:
    """Match build_relative_path on the Linux analyzer route without resolving the filesystem."""
    return path if path.startswith("/") else directory + ("" if directory.endswith(("/", "\\")) else "/") + path


def analyzer_database_has_component(path: str, component: str) -> bool:
    return any(part == component for part in re.split(r"[/\\]", path))


def analyzer_database_matches_config(output: str, arguments: list[str], config: str) -> bool:
    """Mirror clang_analyze_entry_matches_config for its exact Release selection predicate."""
    if not config:
        return True
    markers = (f"CMAKE_INTDIR={config}", f'CMAKE_INTDIR="{config}"', f'CMAKE_INTDIR=\\"{config}\\"')
    return any(analyzer_database_has_component(value, config) or any(marker in value for marker in markers)
               for value in ([output] if output else []) + arguments)


def analyzer_database_project(arguments: list[str], clang: str) -> list[str]:
    """Mirror clang_analyzer_command, preserving semantic argv in original order."""
    if not arguments:
        raise ValueError("compile database command has no arguments")
    dropped = {"-c", "-fcolor-diagnostics", "-MD", "-MMD", "-MP"}
    separate = {"-MF", "-MJ", "-MQ", "-MT", "-o", "--output", "-dependency-file"}
    joined = ("-MF", "-MJ", "-MQ", "-MT", "-o", "--output=", "-dependency-file=")
    projected = [clang or arguments[0], "--analyze", "-Xanalyzer", "-analyzer-output=text",
                 "-fno-color-diagnostics", "-Wno-error=unused-command-line-argument"]
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument in dropped:
            index += 1
        elif argument in separate:
            index += 2 if index + 1 < len(arguments) else 1
        elif any(len(argument) > len(prefix) and argument.startswith(prefix) for prefix in joined):
            index += 1
        else:
            projected.append(argument)
            index += 1
    return projected


def analyzer_database_module(file: str) -> str:
    """Mirror clang_analyze_module's basename and paired *_test ownership rule."""
    basename = re.split(r"[/\\]", file)[-1]
    module = basename[:-2]
    return module[:-5] if module.endswith("_test") else module


def analyzer_database_shard(module: str, shards: int) -> int:
    """Mirror the C driver's byte-stable 64-bit FNV-1a module shard assignment."""
    value = 14695981039346656037
    for byte in module.encode("utf-8"):
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value % shards


def analyzer_native_release_inventory(data: bytes, clang: str, *, config: str = "Release", shards: int = 8) -> dict:
    """Derive the ordered eligible Linux CMake inventory from exact retained CDB bytes."""
    if not isinstance(data, bytes) or not data or len(data) > ANALYZER_STRING_LIMIT:
        raise ValueError("compile database is empty or exceeds its byte bound")

    def unique_object(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("compile database object repeats a JSON property")
            result[key] = value
        return result

    database = json.loads(data.decode("utf-8"), object_pairs_hook=unique_object)
    if not isinstance(database, list) or not database or len(database) > ANALYZER_DATABASE_ENTRY_LIMIT:
        raise ValueError("compile database is not a bounded nonempty CMake array")
    selected: list[dict] = []
    excluded = 0
    identities = set()
    for entry in database:
        if not isinstance(entry, dict):
            raise ValueError("compile database contains a non-object entry")
        directory = entry.get("directory", "")
        file = entry.get("file", "")
        output = entry.get("output", "")
        command = entry.get("command", "")
        original = entry.get("arguments", [])
        if not isinstance(directory, str) or not directory or not isinstance(file, str) or not file or \
                not isinstance(output, str) or not isinstance(command, str) or not isinstance(original, list) or \
                any(not isinstance(argument, str) for argument in original):
            raise ValueError("compile database entry has malformed directory, file, output or arguments")
        arguments = list(original)
        if not arguments and command:
            arguments = analyzer_database_shell_split(command)
        if not arguments or not arguments[0]:
            raise ValueError("compile database entry has no usable compiler command")
        is_c_source = file.lower().endswith(".c")
        if not is_c_source or not analyzer_database_matches_config(output, arguments, config):
            excluded += 1
            continue
        source = analyzer_database_relative_path(directory, file)
        source_found = False
        for argument in arguments:
            if "\x00" in argument or (argument and argument[0] == "@"):
                raise ValueError("selected compile command contains a NUL byte or response file")
            if argument and argument[0] != "-" and argument.lower().endswith(".c") and \
                    analyzer_database_path_equal(source, analyzer_database_relative_path(directory, argument)):
                source_found = True
        if not source_found:
            raise ValueError("selected compile command does not explicitly name its source")
        if "\x00" in directory or "\x00" in file or "\x00" in output:
            raise ValueError("selected compile database identity contains a NUL byte")
        if not output:
            for index in range(1, len(arguments) - 1):
                if arguments[index] == "-o":
                    output = arguments[index + 1]
        identity = (directory, file, output)
        if identity in identities:
            raise ValueError("compile database has a duplicate selected TU identity")
        identities.add(identity)
        module = analyzer_database_module(file)
        selected.append({"index": len(selected), "shard": analyzer_database_shard(module, shards),
                         "module": module, "file": file, "directory": directory, "output": output,
                         "original_argv": arguments, "argv": analyzer_database_project(arguments, clang)})
        if len(selected) > ANALYZER_ROW_LIMIT:
            raise ValueError("selected analyzer inventory exceeds its row bound")
    if not selected:
        raise ValueError("compile database has no eligible Release C translation units")
    return {"entries": len(database), "selected_rows": len(selected), "excluded_rows": excluded, "rows": selected}


def analyzer_inventory_plan_problems(inventory: dict, baseline_plan: dict, candidate_plan: dict) -> list[str]:
    """Bind both plan versions to the complete ordered CDB inventory.

    PLAN_V1 does not serialize directory, output or original argv. Its exact
    database bytes plus every ordered row's module, file, shard and projection
    bind those legacy rows to their derived inventory entries. PLAN_V2 carries
    the full identity and must match every field directly.
    """
    problems: list[str] = []
    expected = inventory.get("rows") if isinstance(inventory, dict) else None
    baseline_rows = baseline_plan.get("rows") if isinstance(baseline_plan, dict) else None
    candidate_rows = candidate_plan.get("rows") if isinstance(candidate_plan, dict) else None
    if not isinstance(expected, list) or not isinstance(baseline_rows, list) or not isinstance(candidate_rows, list):
        return ["analyzer plans do not contain ordered rows for the derived database inventory"]
    if len(baseline_rows) != len(expected) or len(candidate_rows) != len(expected):
        problems.append("baseline or candidate plan omits or adds rows from the complete derived CDB inventory")
        return problems
    baseline_fields = ("index", "shard", "module", "file", "argv")
    candidate_fields = ("index", "shard", "module", "file", "directory", "output", "original_argv", "argv")
    expected_baseline = [{key: row[key] for key in baseline_fields} for row in expected]
    observed_baseline = [{key: row.get(key) for key in baseline_fields} for row in baseline_rows]
    expected_candidate = [{key: row[key] for key in candidate_fields} for row in expected]
    observed_candidate = [{key: row.get(key) for key in candidate_fields} for row in candidate_rows]
    if observed_baseline != expected_baseline:
        problems.append("baseline PLAN_V1 ordered row identities or projections differ from the complete derived CDB inventory")
    if observed_candidate != expected_candidate:
        problems.append("candidate PLAN_V2 directory, file, output, original/projected argv, module or shard differs from the complete derived CDB inventory")
    return problems


def analyzer_profile_summary(files: dict, identity: dict,
                             request_line: str = ANALYZER_REQUEST_LINE) -> tuple[dict, list[str]]:
    """Re-derive the full analyzer evidence from raw plans, shard results and logs."""
    problems: list[str] = []
    if request_line not in ANALYZER_PROFILE_BY_LINE:
        problems.append("requested analyzer profile selector is not recognized")
        request_line = ANALYZER_REQUEST_LINE
    selected_profile = ANALYZER_PROFILE_BY_LINE[request_line]
    profile = json.loads(json.dumps(selected_profile))
    schema = ANALYZER_SCHEMA_BY_LINE[request_line]
    dynamic_inventory = request_line == ANALYZER_REQUEST_LINE_V2
    baseline_revision = identity.get("base") if isinstance(identity, dict) else None
    candidate_revision = identity.get("head") if isinstance(identity, dict) else None
    summary = {"schema": schema, "status": "failed", "profile": profile,
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
        if request_line not in request_lines:
            raise ValueError("retained profile request lacks the analyzer selector")
        summary["request"] = {"line": request_line, "sha256": hashlib.sha256(request_bytes).hexdigest(),
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
        derived_inventory = analyzer_native_release_inventory(compile_commands, clang["path"]) if dynamic_inventory else None
        if derived_inventory is not None:
            summary["compile_commands"]["native_database_entries"] = derived_inventory["entries"]
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
            expected_selected = derived_inventory["selected_rows"] if derived_inventory else ANALYZER_PROFILE["expected_rows"]
            expected_excluded = derived_inventory["excluded_rows"] if derived_inventory else baseline_plan["excluded_rows"]
            plans_ok = plans_ok and baseline_plan["selected_rows"] == expected_selected
            plans_ok = plans_ok and candidate_plan["selected_rows"] == baseline_plan["selected_rows"]
            plans_ok = plans_ok and baseline_plan["database"] == candidate_plan["database"] == compile_commands
            plans_ok = plans_ok and baseline_plan["clang"] == candidate_plan["clang"]
            plans_ok = plans_ok and baseline_plan["excluded_rows"] == candidate_plan["excluded_rows"] == expected_excluded
            baseline_semantics = [{key: row[key] for key in ("index", "shard", "module", "file", "argv")}
                                  for row in baseline_plan["rows"]]
            candidate_semantics = [{key: row[key] for key in ("index", "shard", "module", "file", "argv")}
                                   for row in candidate_plan["rows"]]
            semantic_digest = hashlib.sha256(json.dumps(baseline_semantics, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            plans_ok = plans_ok and baseline_semantics == candidate_semantics
            aliases = sum(row["representative"] != row["index"] for row in candidate_plan["rows"])
            unique = candidate_plan["selected_rows"] - aliases
            plans_ok = plans_ok and candidate_plan["unique_executions"] == unique
            plans_ok = plans_ok and candidate_plan["aliases"] == aliases
            if not dynamic_inventory:
                plans_ok = plans_ok and unique == ANALYZER_PROFILE["expected_candidate_executions"]
                plans_ok = plans_ok and aliases == ANALYZER_PROFILE["expected_candidate_aliases"]
            plans_ok = plans_ok and baseline_plan["unique_executions"] == baseline_plan["selected_rows"] and not baseline_plan["aliases"]
            plans_ok = plans_ok and not candidate_plan["fixture"]
            candidate_envelope_problems = analyzer_candidate_plan_problems(candidate_plan, allow_fallback=dynamic_inventory)
            if candidate_envelope_problems:
                problems.extend(candidate_envelope_problems)
                plans_ok = False
            partition = analyzer_candidate_partition_counts(candidate_plan)
            candidate_alias_groups = partition["proven_groups"]
            if dynamic_inventory and derived_inventory:
                candidate_inventory_rows = [{key: row[key] for key in ("index", "shard", "module", "file", "directory",
                                                                        "output", "original_argv", "argv")}
                                             for row in derived_inventory["rows"]]
                inventory_problems = analyzer_inventory_plan_problems(
                    derived_inventory, baseline_plan, candidate_plan)
                if inventory_problems:
                    problems.extend(inventory_problems)
                    plans_ok = False
                plans_ok = plans_ok and partition["selected_rows"] == derived_inventory["selected_rows"]
                plans_ok = plans_ok and partition["unique_executions"] == candidate_plan["unique_executions"]
                plans_ok = plans_ok and partition["alias_rows"] == candidate_plan["aliases"]
                full_inventory_digest = hashlib.sha256(json.dumps(candidate_inventory_rows, sort_keys=True,
                                                                   separators=(",", ":")).encode()).hexdigest()
            else:
                full_inventory_digest = None
            summary["inventory"] = {"selected_rows": baseline_plan["selected_rows"],
                                    "excluded_rows": baseline_plan["excluded_rows"],
                                    "baseline_unique_executions": baseline_plan["selected_rows"],
                                    "candidate_unique_executions": candidate_plan["unique_executions"],
                                    "candidate_alias_rows": candidate_plan["aliases"],
                                    "candidate_alias_groups": candidate_alias_groups,
                                    "semantic_argv_sha256": semantic_digest,
                                    "database_sha256": hashlib.sha256(compile_commands).hexdigest()}
            if dynamic_inventory and derived_inventory:
                summary["inventory"].update({"raw_database_entries": derived_inventory["entries"],
                                             "candidate_groups": partition["candidate_groups"],
                                             "candidate_proven_groups": partition["proven_groups"],
                                             "inventory_rows_sha256": full_inventory_digest})
            if not plans_ok:
                if dynamic_inventory:
                    problems.append("baseline V1 and candidate V2 plans differ from the common derived complete CDB inventory or alias contract")
                else:
                    # Preserve the exact historical V1 diagnostic text, which is part of its archived summary schema.
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
                                        "candidate_groups": summary["inventory"].get("candidate_groups",
                                                                                      summary["inventory"].get("candidate_alias_groups")),
                                        "proven_groups": summary["inventory"].get("candidate_proven_groups",
                                                                                    summary["inventory"].get("candidate_alias_groups")),
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
        for trial, (name, role) in enumerate(zip(ANALYZER_RUNS, selected_profile["order"])):
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
        for label, role in zip(ANALYZER_RUNS, selected_profile["order"]):
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
                    problems.extend(analyzer_candidate_plan_problems(plan, allow_fallback=dynamic_inventory))
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
                    parsed_run_counters.get("jobs") != selected_profile["jobs"] or \
                    not (isinstance(parsed_run_counters.get("peak_pending_workers"), int) and
                         1 <= parsed_run_counters["peak_pending_workers"] <= selected_profile["jobs"]):
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
    except (ValueError, KeyError, TypeError, IndexError, UnicodeDecodeError, OverflowError, RecursionError) as error:
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
    if not isinstance(receipt, dict):
        return ["receipt does not name a frozen full Clang analyzer profile"]
    request_line = receipt.get("analyzer_request_line")
    if not isinstance(request_line, str):
        return ["receipt does not name a frozen full Clang analyzer selector"]
    selected_profile = ANALYZER_PROFILE_BY_LINE.get(request_line)
    if selected_profile is None or receipt.get("profile") != selected_profile:
        return ["receipt does not name a frozen full Clang analyzer profile and selector"]
    if receipt.get("mode") != "pull":
        problems.append(f"{selected_profile['name']} is valid only in pull mode")
    if receipt.get("analyzer_profile") != selected_profile:
        problems.append("receipt analyzer profile marker is not frozen")
    if receipt.get("analyzer_request_line") != request_line:
        problems.append("receipt does not name the exact analyzer profile selector")
    if not isinstance(bundle, dict) or not isinstance(bundle.get("files"), dict):
        return [*problems, "raw analyzer evidence bundle is missing"]
    derived, errors = analyzer_profile_summary(bundle["files"], receipt.get("identity"), request_line)
    problems.extend(errors)
    if summary != derived:
        if request_line == ANALYZER_REQUEST_LINE:
            # Keep the original diagnostic byte-for-byte for archived V1 evidence.
            problems.append("retained analyzer summary does not match the raw V1/V2 plans, results and logs")
        else:
            problems.append("retained analyzer summary does not match the raw V2 plans, derived inventory, results and logs")
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


def validate_closure(receipt: dict, bundle: object, expected_policy: str | None = None,
                     expected_phase_driver_sha256: str | None = None, expected_trusted_revision: str | None = None,
                     require_owned_phases: bool = False, expected_phase_schema: str | None = None,
                     require_owned_preflight: bool = False, expected_profile: str | None = None) -> list[str]:
    """Replay native producer/consumer identities as bounded data, including the frozen baseline executable."""
    if type(require_owned_phases) is not bool or type(require_owned_preflight) is not bool or \
            expected_phase_schema not in (None, "buster-compiler-snapshot-phases-v1", "buster-compiler-utility-phases-v1",
                                          "buster-compiler-main-owned-phases-v1"):
        return ["native ownership requirements do not match a supported trusted route"]
    if expected_phase_schema == "buster-compiler-main-owned-phases-v1" and \
            (expected_policy is None or expected_profile is None or not require_owned_phases or not require_owned_preflight):
        return ["MAIN ownership requires explicit trusted profile, preparation policy and complete preflight"]
    if expected_profile is not None:
        try:
            selected_profile = named_main_profile(expected_profile)
        except ValueError:
            return ["native ownership profile does not match a supported trusted route"]
        if receipt.get("mode") != "main" or receipt.get("profile") != selected_profile:
            return ["ordinary main profile differs from the trusted named recipe"]
        if expected_phase_schema != "buster-compiler-main-owned-phases-v1" and expected_profile != PROFILE["name"]:
            return ["the short main profile requires its explicit trusted MAIN-owned route"]
    closure = receipt.get("closure")
    declared = receipt.get("preparation_policy", "legacy-rebuild" if closure is None else "snapshot-v1")
    if expected_policy not in (None, "legacy-rebuild", "snapshot-v1") or declared not in ("legacy-rebuild", "snapshot-v1") or \
            (expected_policy is not None and declared != expected_policy):
        return ["frozen baseline preparation policy does not match the trusted route"]
    if closure is None:
        if declared == "snapshot-v1" or expected_policy == "snapshot-v1":
            return ["requested frozen baseline closure receipt is missing"]
        if require_owned_phases or expected_phase_schema is not None or require_owned_preflight:
            from compiler_owned_phase import validate_population
            raw = bundle if isinstance(bundle, dict) else {}
            return validate_population(receipt, raw.get("owned_phases"), expected_phase_driver_sha256,
                expected_trusted_revision, raw.get("owned_throughput"), expected_phase_schema=expected_phase_schema,
                require_owned_preflight=require_owned_preflight, expected_profile=expected_profile)
        return []  # historical and default legacy-rebuild receipts
    if declared != "snapshot-v1" or not isinstance(closure, dict) or closure.get("policy") != "snapshot-v1" or closure.get("fallback") is not None:
        return ["frozen baseline closure policy/fallback is unsupported"]
    identity = receipt.get("identity") if isinstance(receipt.get("identity"), dict) else {}
    binaries = receipt.get("binaries") if isinstance(receipt.get("binaries"), dict) else {}
    raw = bundle if isinstance(bundle, dict) else {}
    reasons: list[str] = []
    manifests: list[bytes] = []
    for operation in ("snapshot", "restore", "verify"):
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
        if record.get("ownership_schema") != "buster-native-qualification-supervisor-v1" or record.get("cleanup_proven") is not True or \
                any(type(record.get(key)) is not int or record[key] < 0 for key in ("cleanup_us", "cleanup_waves", "cleanup_signalled", "cleanup_reaped")) or \
                record.get("cleanup_signalled") != 0 or record.get("cleanup_reaped") != 0:
            reasons.append(f"frozen baseline {operation} child ownership or clean nominal completion is unproven")
        if type(record.get("duration_us")) is not int or record["duration_us"] < 0:
            reasons.append(f"frozen baseline {operation} duration missing or malformed")
        if type(record.get("harness_preparation_us")) is not int or record["harness_preparation_us"] < 0:
            reasons.append(f"frozen baseline {operation} harness preparation timing missing or malformed")
    if len(manifests) == 3 and any(raw != manifests[0] for raw in manifests[1:]):
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
                   for operation in ("snapshot", "restore", "verify")):
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
            configuration = bindings.get("bootstrap_config", "")
            marker = bindings.get("bootstrap_marker", "")
            artifact = bindings.get("bootstrap_artifact", "")
            if not SHA256.fullmatch(configuration) or not artifact.startswith("posix/" + configuration + "/") or \
                    artifact.count("/") != 2 or marker != artifact + ".complete" or \
                    ("bootstrap", marker) not in rows or ("bootstrap", artifact) not in rows or \
                    rows["bootstrap", marker][1] != "F" or rows["bootstrap", artifact][1] != "F" or \
                    not int(rows["bootstrap", artifact][2]) & 0o111:
                raise ValueError("manifest actual baseline bootstrap marker/executable pair is missing")
            for operation in ("snapshot", "restore", "verify"):
                if closure[operation].get("harness_sha256") != rows["build", "throughput-tools/throughput"][6] or \
                        closure[operation].get("bootstrap_marker_sha256") != rows["bootstrap", marker][6] or \
                        closure[operation].get("bootstrap_artifact_sha256") != rows["bootstrap", artifact][6]:
                    raise ValueError("manifest actual producer/consumer executable hashes mismatch")
            baseline = binaries.get("baseline") if isinstance(binaries.get("baseline"), dict) else {}
            if any(rows[key][1] != "F" or not int(rows[key][2]) & 0o111 for key in (("build", "Release/ide"), ("build", "throughput-tools/throughput"))):
                raise ValueError("manifest compiler/native corpus executable identity malformed")
            if not any(key[0] == "resource" for key in rows):
                raise ValueError("manifest configured Clang resource closure absent")
            if rows["build", "Release/ide"][6] != baseline.get("sha256"):
                raise ValueError("manifest baseline compiler binary mismatch")
            for key in ("CMAKE_C_COMPILER", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM", "clang", "cmake", "ninja", "tcc", "resource"):
                if not bindings.get(key, "").startswith("/") or (key != "resource" and ("tool", key) not in rows):
                    raise ValueError("manifest configured compiler/linker/tool/resource identity missing")
        except (UnicodeError, ValueError, IndexError, TypeError):
            reasons.append("frozen baseline manifest source/toolchain/configuration/closure mismatch")
    if require_owned_phases or require_owned_preflight or expected_phase_schema is not None or "phase_ownership" in receipt:
        from compiler_owned_phase import validate_population
        reasons.extend(validate_population(receipt, raw.get("owned_phases"), expected_phase_driver_sha256,
                                           expected_trusted_revision, raw.get("owned_throughput"),
                                           expected_phase_schema=expected_phase_schema,
                                           require_owned_preflight=require_owned_preflight, expected_profile=expected_profile))
    return reasons


def classify(summary: object, binaries: object, *, expected_profile: str | None = None) -> list[str]:
    """Reasons the lab summary is not a valid core measurement; empty when valid."""
    reasons: list[str] = []
    selected_profile = None
    if expected_profile is not None:
        try:
            selected_profile = named_main_profile(expected_profile)
        except ValueError:
            return ["lab profile does not match a supported trusted named recipe"]
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
    if selected_profile is MAIN40_PROFILE:
        facts = {"pairs": 40, "complete_pairs": 40, "seed": 20261003, "confidence": 0.95,
                 "bootstrap_resamples": 2000, "fresh_copy": True, "order": "ABBA"}
        if any(type(plan.get(key)) is not type(value) or plan[key] != value for key, value in facts.items()) or \
                any(type(count) is not int or count != 40 for count in counts.values()):
            reasons.append("main forty-pair lab did not complete its exact frozen plan")
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) else {}
    if selected_profile is MAIN40_PROFILE and (type(verdict.get("n")) is not int or verdict["n"] != 40 or
            type(verdict.get("min_effect_percent")) not in (int, float) or verdict["min_effect_percent"] != 0.5):
        reasons.append("main forty-pair verdict count/floor differs from the frozen recipe")
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
        elif not _finite_positive(value):
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


def classify_throughput_exit(status: object, summary: object, metadata: object, binaries: object) -> list[str]:
    """Permit report-only exits only after complete normal producer fields and decisions."""
    reasons = classify_throughput(summary, metadata, binaries)
    summary = summary if isinstance(summary, dict) else {}
    metadata = metadata if isinstance(metadata, dict) else {}
    profile = THROUGHPUT_PROFILE
    pairs, rounds = profile["pairs_per_round"], profile["rounds"]

    def finite(value: object, *, positive: bool = False) -> bool:
        return _number(value) and (value > 0 if positive else value >= 0)

    def integer(value: object, *, positive: bool = False) -> bool:
        return type(value) is int and (value > 0 if positive else value >= 0)

    facts = {"schema": 2, "seed": 20260907, "pairs_per_round": pairs, "rounds": rounds,
             "warmups": profile["warmups"], "cpu": profile["cpu"], "scale": 1, "input_schema": 1}
    if type(summary.get("schema")) is not int or any(type(metadata.get(key)) is not int or metadata[key] != value
                                                      for key, value in facts.items()):
        reasons.append("throughput report-only schema/profile facts must be exact integers")
    if metadata.get("cache_policy") != "warm-filesystem-new-process" or metadata.get("clock") != "monotonic":
        reasons.append("throughput report-only cache/clock facts are missing")
    for key, wanted in (("family_alpha", 0.01), ("per_test_alpha", 0.01 / (12 * 2))):
        if not finite(summary.get(key), positive=True) or summary[key] != wanted:
            reasons.append(f"throughput report-only {key} does not match the original gate")
    provenance = metadata.get("compiler_provenance")
    if not isinstance(provenance, list) or len(provenance) != 2 or any(
            not isinstance(row, dict) or not integer(row.get("bytes"), positive=True) for row in provenance):
        reasons.append("throughput report-only compiler byte counts are missing")
    jobs = metadata.get("jobs")
    expected = [(name, mode) for name in profile["workloads"] for mode in profile["modes"]]
    if not isinstance(jobs, list) or len(jobs) != len(expected):
        reasons.append("throughput report-only workload input population is incomplete")
    else:
        for index, (job, (name, mode)) in enumerate(zip(jobs, expected)):
            if (not isinstance(job, dict) or type(job.get("job")) is not int or job["job"] != index or
                    job.get("name") != name or job.get("mode") != mode or job.get("artifact") != "object" or
                    not isinstance(job.get("source"), str) or not job["source"].startswith("/") or
                    any(byte in job["source"] for byte in ("\x00", "\n", "\r")) or
                    not isinstance(job.get("sha256"), str) or not SHA256.fullmatch(job["sha256"]) or
                    any(not integer(job.get(key), positive=True) for key in ("bytes", "physical_lines", "defined_functions"))):
                reasons.append(f"throughput report-only workload input {index} is incomplete")
    regressions, inconclusive = 0, 0
    rows = summary.get("comparisons", [])
    for row in rows if isinstance(rows, list) else []:
        if not isinstance(row, dict):
            continue
        name, medians = row.get("name"), row.get("medians")
        for metric in THROUGHPUT_TEST_METRICS:
            values = medians.get(metric) if isinstance(medians, dict) else None
            if not isinstance(values, list) or len(values) != 2 or any(not finite(value, positive=True) for value in values):
                reasons.append(f"throughput report-only {name!r} has incomplete {metric} medians")
        tests = row.get("tests")
        valid_tests = isinstance(tests, list)
        for test in tests if isinstance(tests, list) else []:
            valid = (isinstance(test, dict) and type(test.get("round")) is int and
                     type(test.get("pairs")) is int and test["pairs"] == pairs and
                     type(test.get("regression")) is bool and finite(test.get("median_ratio"), positive=True) and
                     finite(test.get("ci_low"), positive=True) and finite(test.get("ci_high"), positive=True) and
                     test["ci_low"] <= test["ci_high"] and
                     finite(test.get("baseline_relative_mad")) and finite(test.get("candidate_relative_mad")) and
                     integer(test.get("margin_exceedances")) and test["margin_exceedances"] <= pairs and
                     finite(test.get("p_value")) and test["p_value"] <= 1 and
                     test["regression"] == (test["p_value"] <= 0.01 / 24))
            valid_tests = valid_tests and valid
        if not valid_tests:
            reasons.append(f"throughput report-only {name!r} has incomplete normal test fields")
        else:
            confirmed, uncertain = False, False
            for metric, margin in (("wall_seconds", 1.15), ("peak_rss_bytes", 1.20)):
                metric_tests = [test for test in tests if test.get("metric") == metric]
                count = sum(test["regression"] for test in metric_tests)
                confirmed = confirmed or count == rounds
                uncertain = uncertain or 0 < count < rounds or any(
                    not test["regression"] and test["ci_high"] > margin for test in metric_tests)
            decision = "regression" if confirmed else "inconclusive" if uncertain else "no substantial regression detected"
            if row.get("decision") != decision:
                reasons.append(f"throughput report-only {name!r} decision contradicts normal per-round tests")
            regressions += int(confirmed)
            inconclusive += int(not confirmed and uncertain)
    if type(summary.get("confirmed_regressions")) is not int or summary["confirmed_regressions"] != regressions or \
            type(summary.get("inconclusive_cases")) is not int or summary["inconclusive_cases"] != inconclusive:
        reasons.append("throughput report-only decision totals contradict normal per-round tests")
    if type(status) is not int or status not in (0, 1):
        reasons.append(f"bench_throughput run exited {status} (see throughput.log)")
    elif status == 1 and regressions <= 0:
        reasons.append("bench_throughput exit 1 has no counted confirmed corpus regression")
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
    # An out-of-range JSON integer renders as NA; a float format would raise OverflowError on it.
    return form % value if type(value) is float or _number(value) else "NA"


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
             *render_inventory_coverage(receipt.get("inventory_coverage")),
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


# Inventory rows of docs/performance-validation-v1.json that each receipt leg
# discharges, per mode (#2761). A row is discharged only by a leg of an attempt
# whose evidence the publisher validated (conclusion success); every other leg
# is listed with why it discharged nothing. performance_inventory_test.py fails
# when a covered main-compare/pull-compare row is reachable from no leg here, or
# when a leg names a row that is not a covered row of its mode's route.
INVENTORY_COVERAGE_SCHEMA = "buster-performance-inventory-coverage-v1"
INVENTORY_ROUTES = {"main": "main-compare", "pull": "pull-compare"}
INVENTORY_LEGS = {
    "main": {
        "self-host": ("uarch_lab:compare@main",),
        "corpus": ("build:bench_throughput@main",),
    },
    "pull": {
        "self-host": ("uarch_lab:compare",),
        "corpus": ("build:bench_throughput",),
        "scaling": ("build:bench_throughput",),
        "analyzer": ("build:clang_analyze_benchmark", "tool:clang_analyze_benchmark.c"),
        "inline": ("uarch_lab:compare@canonical-inline-self-host",),
        "ast-pilot": ("uarch_lab:compare@ast-pilot-acceptance",),
        "constant-reuse": ("uarch_lab:compare@constant-reuse-acceptance",),
        "process-floor": ("uarch_lab:compare@process-floor",),
        "real-source": ("build:bench_throughput@throughput-real-source",),
        "production-profile": ("build:production_profile",),
        "compiler-probes": ("tool:fast_ra_scaling.c", "tool:machine_line_mark_scaling.c"),
        "frontend-selector": ("tool:benchmark_frontend_scaling.py", "tool:selection_benchmark.py", "ide:bench"),
        "frontend-nesting": ("tool:benchmark_frontend_scaling.py@frontend-nesting",),
        "wild-linker": ("tool:wild_linker_bench.c",),
        "link-tls": ("tool:link_tls_bench.py",),
        "self-host-inline": ("uarch_lab:compare@self-host-inline-compare",),
        "historical-corpus": ("build:bench_throughput@throughput-corpus-historical",),
    },
}
# Opt-in legs whose receipt record carries its own requested/status fields.
INVENTORY_OPT_IN_RECORDS = {"inline": "inline_acceptance", "ast-pilot": "ast_pilot_acceptance",
                            "constant-reuse": "constant_reuse_acceptance", "process-floor": "process_floor",
                            "real-source": "throughput_real_source", "production-profile": "production_profile",
                            "compiler-probes": "compiler_probes", "frontend-selector": "frontend_selector",
                            "frontend-nesting": "frontend_nesting",
                            "wild-linker": "wild_linker", "link-tls": "link_tls",
                            "self-host-inline": "selfhost_inline"}


def inventory_leg_record(receipt: dict, leg: str) -> tuple[bool | None, str]:
    """(ran, recorded status) of one leg in a receipt; ran is None when the leg left no record.

    ran is False when the receipt says the leg was not requested, and True
    when it was requested; the recorded status is the receipt's own account
    of it, which never discharges anything by itself.
    """
    analyzer = receipt.get("profile") in ANALYZER_PROFILE_BY_LINE.values()
    state = receipt.get("state") if isinstance(receipt.get("state"), str) else "missing"
    ran: bool | None = None
    recorded = "missing"
    if leg == "analyzer":
        ran, recorded = analyzer, state if analyzer else "not requested"
    elif leg == "self-host":
        ran, recorded = not analyzer, "not requested" if analyzer else state
    elif leg in ("corpus", "historical-corpus"):
        # The named historical recipe (#3714) discharges only its own row, never the v2 corpus row.
        historical = receipt.get("throughput_profile") == THROUGHPUT_HISTORICAL_PROFILE
        if analyzer or ("throughput_profile" in receipt and receipt["throughput_profile"] is None) or \
                historical != (leg == "historical-corpus"):
            ran, recorded = False, "not requested"
        elif "throughput_profile" in receipt:
            corpus = receipt.get("throughput") if isinstance(receipt.get("throughput"), dict) else {}
            ran = True
            recorded = "missing" if not corpus else "valid" if corpus.get("valid") is True and \
                corpus.get("exit") in (None, 0) else "failed"
    elif leg == "scaling":
        if "scaling_profile" in receipt:
            series = receipt.get("scaling") if isinstance(receipt.get("scaling"), dict) else {}
            # Compare, never hash: a malformed status (an object or list) is a failure, not a crash.
            valid = all(isinstance(row, dict) and row.get("status") == "valid"
                        for row in (series.get(name) for name in SCALING_PROFILE["series"]))
            ran, recorded = True, "valid" if valid else "failed"
        else:
            ran, recorded = False, "not requested"
    elif leg in INVENTORY_OPT_IN_RECORDS:
        record = receipt.get(INVENTORY_OPT_IN_RECORDS[leg])
        if record is None or (isinstance(record, dict) and record.get("requested") is False):
            ran, recorded = False, "not requested"
        elif isinstance(record, dict) and record.get("requested") is True:
            ran, recorded = True, record.get("status") if isinstance(record.get("status"), str) else "missing"
        else:
            recorded = "malformed"
    else:
        recorded = "unknown leg"
    return ran, recorded


def covered_inventory_rows(receipt: object, conclusion: str, mode: str) -> dict:
    """Which inventory rows this attempt's validated evidence discharged, per leg (pure).

    mode is the trusted mode being published, never the receipt's own claim.
    A leg discharges its rows only when the attempt concluded success, which
    the publisher grants only after re-validating every requested leg's raw
    evidence; a superseded, failed, invalid, absent or unrequested leg is
    listed as not discharged with its reason. A row is discharged when every
    requested leg naming it discharged and at least one did. Report-only:
    nothing here changes a conclusion.
    """
    legs = INVENTORY_LEGS.get(mode, {})
    receipt = receipt if isinstance(receipt, dict) else None
    entries: list[dict] = []
    for leg, rows in legs.items():
        ran, recorded = inventory_leg_record(receipt, leg) if receipt is not None else (None, "no receipt")
        if receipt is None:
            status = "not discharged: no readable receipt"
        elif ran is False:
            status = "not discharged: not requested"
        elif ran is None:
            status = f"not discharged: no {leg} record ({recorded})"
        elif conclusion == "success":
            status = "discharged"
        elif conclusion == "neutral" or receipt.get("state") == "superseded":
            status = "not discharged: superseded before measurement"
        elif recorded not in ("complete", "measured", "valid"):
            status = f"not discharged: leg {recorded}"
        else:
            status = "not discharged: evidence refused by the publisher"
        entries.append({"leg": leg, "rows": list(rows), "recorded": recorded, "status": status})
    statuses: dict[str, list[str]] = {}
    for entry in entries:
        for row in entry["rows"]:
            statuses.setdefault(row, []).append(entry["status"])
    by_row: dict[str, str] = {}
    for row, found in statuses.items():
        requested = [status for status in found if status != "not discharged: not requested"] or found
        by_row[row] = next((status for status in requested if status != "discharged"), "discharged")
    return {"schema": INVENTORY_COVERAGE_SCHEMA, "inventory": "docs/performance-validation-v1.json",
            "route": INVENTORY_ROUTES.get(mode, "none"), "conclusion": conclusion, "legs": entries,
            "rows": dict(sorted(by_row.items())),
            "discharged": sorted(row for row, status in by_row.items() if status == "discharged")}


def render_inventory_coverage(coverage: object) -> list[str]:
    """Markdown lines naming each inventory row and leg this attempt did or did not discharge."""
    lines: list[str] = []
    if isinstance(coverage, dict) and isinstance(coverage.get("legs"), list):
        discharged = coverage.get("discharged") if isinstance(coverage.get("discharged"), list) else []
        lines += [f"Inventory coverage (`docs/performance-validation-v1.json`, route `{coverage.get('route', 'NA')}`): "
                  f"{len(discharged)} rows discharged by this attempt's validated Zen 5 evidence; any other row "
                  "still needs its own evidence.", "",
                  "| Leg | Inventory rows | Receipt status | Coverage |", "| --- | --- | --- | --- |"]
        for entry in coverage["legs"]:
            if isinstance(entry, dict):
                rows = entry.get("rows") if isinstance(entry.get("rows"), list) else []
                lines.append("| %s | %s | %s | %s |" % (entry.get("leg", "NA"), ", ".join(f"`{row}`" for row in rows),
                                                          entry.get("recorded", "NA"), entry.get("status", "NA")))
        lines.append("")
    return lines


def render(receipt: dict, summary: object, conclusion: str, notes: list[str]) -> str:
    """Readable per-candidate report: identities, verdict, core metrics and timings."""
    if isinstance(receipt, dict) and receipt.get("profile") in ANALYZER_PROFILE_BY_LINE.values():
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
        *render_inventory_coverage(receipt.get("inventory_coverage") if isinstance(receipt, dict) else None),
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
        named = receipt.get("throughput_profile") if receipt.get("throughput_profile") == THROUGHPUT_HISTORICAL_PROFILE \
            else THROUGHPUT_PROFILE
        lines += ["", f"Throughput corpus `{named['name']}`: {len(cases)} cases, "
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
    ast = receipt.get("ast_pilot_acceptance") if isinstance(receipt, dict) else None
    if isinstance(ast, dict) and ast.get("requested"):
        ast_summary = ast.get("summary") if isinstance(ast.get("summary"), dict) else {}
        verdict = ast_summary.get("verdict") if isinstance(ast_summary.get("verdict"), dict) else {}
        checks = verdict.get("checks") if isinstance(verdict.get("checks"), dict) else {}
        tree = ast_summary.get("tree") if isinstance(ast_summary.get("tree"), dict) else {}
        lines += ["", f"Issue #3397 -fc-ast-pilot adoption budget: {ast.get('status', 'NA')}, verdict "
                  f"{verdict.get('outcome', 'NA')} ({AST_PILOT_ACCEPTANCE_PROFILE['pairs']} ABBA pairs per cell; "
                  "report-only; budget in docs/agents/frontend/ast.md).",
                  "One candidate binary: A = default, B passes -fc-ast-pilot. Ratios below 1 mean B is lower. "
                  "Counters may be NA when the host cannot schedule them.",
                  f"Tree retained bytes (B, -v): {number(tree.get('retained_bytes'), '%.0f')}.", "",
                  "| Cell | Wall B/A [95% CI] | Retired instructions B/A | Peak RSS A / B | Outputs identical | Diagnostics identical |",
                  "| --- | --- | --- | --- | --- | --- |"]
        cells = ast_summary.get("cells") if isinstance(ast_summary.get("cells"), dict) else {}
        for cell in AST_PILOT_CELLS:
            row = cells.get(cell) if isinstance(cells.get(cell), dict) else {}
            values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
            wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
            rss = values.get("peak_rss") if isinstance(values.get("peak_rss"), dict) else {}
            lines.append("| %s | %s [%s, %s] | %s | %s / %s | %s | %s |" % (
                cell, number(wall.get("ratio"), "%.4f"), number(wall.get("ci_low"), "%.4f"),
                number(wall.get("ci_high"), "%.4f"), number((values.get("instructions") or {}).get("ratio"), "%.4f"),
                number(rss.get("a_median"), "%.0f"), number(rss.get("b_median"), "%.0f"),
                row.get("outputs_identical", "NA"), row.get("diagnostics_identical", "NA")))
        lines += ["", "| Budget check | Outcome |", "| --- | --- |"]
        lines += [f"| {name} | {check.get('outcome', 'NA') if isinstance(check, dict) else 'NA'} |"
                  for name, check in checks.items()]
        if ast.get("exit") not in (None, 0):
            lines.append(f"Issue #3397 profile exit: {ast.get('exit')}.")
    reuse = receipt.get("constant_reuse_acceptance") if isinstance(receipt, dict) else None
    if isinstance(reuse, dict) and reuse.get("requested"):
        reuse_summary = reuse.get("summary") if isinstance(reuse.get("summary"), dict) else {}
        lines += ["", f"Issue #3011 constant-reuse arms: {reuse.get('status', 'NA')} "
                  f"({CONSTANT_REUSE_ACCEPTANCE_PROFILE['pairs']} ABBA pairs per cell; report-only).",
                  "One candidate binary on the -g0 unity object compile: A = default, B passes the cell's flag. "
                  "Ratios below 1 mean B is lower. Counters may be NA when the host cannot schedule them.", "",
                  "| Cell (B flag) | Wall B/A [95% CI] | Retired instructions B/A | Peak RSS A / B | Code bytes A / B | Diagnostics identical |",
                  "| --- | --- | --- | --- | --- | --- |"]
        cells = reuse_summary.get("cells") if isinstance(reuse_summary.get("cells"), dict) else {}
        for cell, flag in CONSTANT_REUSE_CELLS.items():
            row = cells.get(cell) if isinstance(cells.get(cell), dict) else {}
            values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
            wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
            rss = values.get("peak_rss") if isinstance(values.get("peak_rss"), dict) else {}
            code = values.get("code_bytes") if isinstance(values.get("code_bytes"), dict) else {}
            lines.append("| %s (%s) | %s [%s, %s] | %s | %s / %s | %s / %s | %s |" % (
                cell, flag, number(wall.get("ratio"), "%.4f"), number(wall.get("ci_low"), "%.4f"),
                number(wall.get("ci_high"), "%.4f"), number((values.get("instructions") or {}).get("ratio"), "%.4f"),
                number(rss.get("a_median"), "%.0f"), number(rss.get("b_median"), "%.0f"),
                number(code.get("a_value"), "%.0f"), number(code.get("b_value"), "%.0f"),
                row.get("diagnostics_identical", "NA")))
        if reuse.get("exit") not in (None, 0):
            lines.append(f"Issue #3011 profile exit: {reuse.get('exit')}.")
    floor = receipt.get("process_floor") if isinstance(receipt, dict) else None
    if isinstance(floor, dict) and floor.get("requested"):
        floor_summary = floor.get("summary") if isinstance(floor.get("summary"), dict) else {}
        lines += ["", f"Issue #1125 per-process floor: {floor.get('status', 'NA')} "
                  f"({PROCESS_FLOOR_PROFILE['pairs']} ABBA pairs per cell; report-only).",
                  f"One candidate binary in fresh processes on `{PROCESS_FLOOR_SOURCE.strip()}`: A = "
                  f"`{PROCESS_FLOOR_PROFILE['compiler_modes']['a']}`, B = the cell's command. Wall is the harness span "
                  "around the pinned perf wrapper; task-clock is the compiler process alone. A - B is what the "
                  "x86-64 object compile costs above B. Counters may be NA when the host cannot schedule them.", "",
                  "| Cell (B) | Wall A / B ms | Wall A - B ms | Wall B/A [95% CI] | Task-clock A / B ms "
                  "| Instructions A / B | Outside A/A |",
                  "| --- | --- | --- | --- | --- | --- | --- |"]
        cells = floor_summary.get("cells") if isinstance(floor_summary.get("cells"), dict) else {}
        verdict = floor_summary.get("verdict") if isinstance(floor_summary.get("verdict"), dict) else {}
        for cell, command in PROCESS_FLOOR_PROFILE["cells"].items():
            row = cells.get(cell) if isinstance(cells.get(cell), dict) else {}
            values = row.get("metrics") if isinstance(row.get("metrics"), dict) else {}
            wall = values.get("wall") if isinstance(values.get("wall"), dict) else {}
            task = values.get("task_clock") if isinstance(values.get("task_clock"), dict) else {}
            instructions = values.get("instructions") if isinstance(values.get("instructions"), dict) else {}
            control = verdict.get(cell) if isinstance(verdict.get(cell), dict) else {}
            milliseconds = [value * 1000.0 if _number(value) else None
                            for value in (wall.get("a_median"), wall.get("b_median"), task.get("a_median"),
                                          task.get("b_median"))]
            lines.append("| %s (`%s`) | %s / %s | %s | %s [%s, %s] | %s / %s | %s / %s | %s |" % (
                cell, command, number(milliseconds[0], "%.3f"), number(milliseconds[1], "%.3f"),
                number(control.get("wall_ms") if cell != "aa" else None, "%.3f"),
                number(wall.get("ratio"), "%.4f"), number(wall.get("ci_low"), "%.4f"),
                number(wall.get("ci_high"), "%.4f"), number(milliseconds[2], "%.3f"), number(milliseconds[3], "%.3f"),
                number(instructions.get("a_median"), "%.0f"), number(instructions.get("b_median"), "%.0f"),
                "A/A" if cell == "aa" else control.get("outside_aa", "NA")))
        if floor.get("exit") not in (None, 0):
            lines.append(f"Issue #1125 profile exit: {floor.get('exit')}.")
    real = receipt.get("throughput_real_source") if isinstance(receipt, dict) else None
    if isinstance(real, dict) and real.get("requested"):
        profile = THROUGHPUT_REAL_SOURCE_PROFILE
        lines += ["", f"Real-source throughput ({profile['name']}): {real.get('status', 'NA')} "
                  f"({profile['pairs_per_round']} pairs x {profile['rounds']} rounds per cell; report-only, guard off). "
                  "Each cell times its descriptor's compile_link_argv to a linked executable.", "",
                  "| Descriptor / mode | Base wall (ms) | Candidate wall (ms) | Candidate / base | Peak MiB base / candidate |",
                  "| --- | ---: | ---: | ---: | ---: |"]
        for row in real.get("cells") if isinstance(real.get("cells"), list) else []:
            wall = row.get("wall_seconds") if isinstance(row, dict) and isinstance(row.get("wall_seconds"), list) else []
            rss = row.get("peak_rss_bytes") if isinstance(row, dict) and isinstance(row.get("peak_rss_bytes"), list) else []
            ok = len(wall) == 2 and all(_finite_positive(value) for value in wall)
            memory = len(rss) == 2 and all(_finite_positive(value) for value in rss)
            lines.append("| %s | %s | %s | %s | %s |" % (
                row.get("name") if isinstance(row, dict) else "NA",
                "%.1f" % (wall[0] * 1000) if ok else "NA", "%.1f" % (wall[1] * 1000) if ok else "NA",
                "%.4f" % (wall[1] / wall[0]) if ok else "NA",
                "%.1f / %.1f" % (rss[0] / 1048576, rss[1] / 1048576) if memory else "NA"))
        if real.get("exit") not in (None, 0):
            lines.append(f"Real-source throughput exit: {real.get('exit')}.")
    production = receipt.get("production_profile") if isinstance(receipt, dict) else None
    if isinstance(production, dict) and production.get("requested"):
        profile = PRODUCTION_PROFILE_PROFILE
        compilers = production.get("binaries") if isinstance(production.get("binaries"), dict) else {}
        # A malformed role record renders as NA; validation refuses it, so rendering must not raise first.
        hashes = [str(compilers[role].get("sha256", "NA"))[:12] if isinstance(compilers.get(role), dict) else "NA"
                  for role in ("baseline", "candidate")]
        lines += ["", f"PGO/LTO production profile ({profile['name']}): {production.get('status', 'NA')} "
                  "(the head's own Release compiler as base, its PGO+LTO production compiler as candidate, on the "
                  f"{THROUGHPUT_PROFILE['name']} corpus; report-only). Release `%s`, PGO+LTO `%s`." % tuple(hashes), "",
                  "| Workload / mode | Release wall (ms) | PGO+LTO wall (ms) | PGO+LTO / Release | Decision |",
                  "| --- | ---: | ---: | ---: | --- |"]
        for row in production.get("cells") if isinstance(production.get("cells"), list) else []:
            wall = row.get("wall_seconds") if isinstance(row, dict) and isinstance(row.get("wall_seconds"), list) else []
            ok = len(wall) == 2 and all(_finite_positive(value) for value in wall)
            lines.append("| %s | %s | %s | %s | %s |" % (
                row.get("name") if isinstance(row, dict) else "NA",
                "%.1f" % (wall[0] * 1000) if ok else "NA", "%.1f" % (wall[1] * 1000) if ok else "NA",
                "%.4f" % (wall[1] / wall[0]) if ok else "NA", row.get("decision") if isinstance(row, dict) else "NA"))
        for key, title in (("build_exit", "build"), ("exit", "corpus")):
            if production.get(key) not in (None, 0):
                lines.append(f"PGO/LTO production profile {title} exit: {production.get(key)}.")
    probes = receipt.get("compiler_probes") if isinstance(receipt, dict) else None
    if isinstance(probes, dict) and probes.get("requested"):
        profile = COMPILER_PROBES_PROFILE
        lines += ["", f"Compiler probes ({profile['name']}): {probes.get('status', 'NA')} (FAST register-allocation and "
                  "scheduler line-mark probes built against the base and the head, "
                  f"{profile['pairs']} alternating pairs per case on CPU {profile['cpu']}; report-only).", "",
                  "| Probe / case | Base (us) | Head (us) | Head / base [95% interval] | Head faster | Same self-check |",
                  "| --- | ---: | ---: | ---: | ---: | --- |"]
        for row in probes.get("cells") if isinstance(probes.get("cells"), list) else []:
            row = row if isinstance(row, dict) else {}
            values = [row.get(key) for key in ("baseline_ns", "candidate_ns", "ratio", "ratio_low", "ratio_high")]
            ok = all(_finite_positive(value) for value in values)
            lines.append("| %s | %s | %s | %s | %s/%d | %s |" % (
                row.get("name", "NA"), "%.1f" % (values[0] / 1000) if ok else "NA",
                "%.1f" % (values[1] / 1000) if ok else "NA",
                "%.4f [%.4f, %.4f]" % tuple(values[2:]) if ok else "NA", row.get("candidate_faster", "NA"),
                profile["pairs"], "yes" if row.get("outputs_identical") is True else "no"))
        if probes.get("exit") not in (None, 0):
            lines.append(f"Compiler probes exit: {probes.get('exit')}.")
    selector = receipt.get("frontend_selector") if isinstance(receipt, dict) else None
    if isinstance(selector, dict) and selector.get("requested"):
        lines += ["", f"Frontend and selector probes ({FRONTEND_SELECTOR_PROFILE['name']}): {selector.get('status', 'NA')} "
                  "(benchmark_frontend_scaling.py, selection_benchmark.py and `ide bench` on both binaries; medians; "
                  "report-only).", "",
                  "| Cell | Base | Head | Head / base |", "| --- | ---: | ---: | ---: |"]
        for row in selector.get("cells") if isinstance(selector.get("cells"), list) else []:
            values = row.get("values") if isinstance(row, dict) and isinstance(row.get("values"), list) else []
            ok = len(values) == 2 and all(_finite_positive(value) for value in values)
            scale, unit = (1000.0, "ms") if isinstance(row, dict) and row.get("unit") == "s" else (1e-3, "us")
            lines.append("| %s | %s | %s | %s |" % (
                row.get("name") if isinstance(row, dict) else "NA",
                "%.1f %s" % (values[0] * scale, unit) if ok else "NA", "%.1f %s" % (values[1] * scale, unit) if ok else "NA",
                "%.4f" % (values[1] / values[0]) if ok else "NA"))
        exits = selector.get("exits") if isinstance(selector.get("exits"), dict) else {}
        for key, value in exits.items():
            if value not in (None, 0):
                lines.append(f"Frontend-selector {key} exit: {value}.")
    nesting = receipt.get("frontend_nesting") if isinstance(receipt, dict) else None
    if isinstance(nesting, dict) and nesting.get("requested"):
        profile = FRONTEND_NESTING_PROFILE["frontend_scaling"]
        lines += ["", f"Deep-nesting families ({FRONTEND_NESTING_PROFILE['name']}): {nesting.get('status', 'NA')} "
                  f"(benchmark_frontend_scaling.py --suite nesting on both binaries, depths "
                  f"{', '.join(str(depth) for depth in profile['depths'])}; wall medians; report-only).", "",
                  "| Cell | Base | Head | Head / base |", "| --- | ---: | ---: | ---: |"]
        for row in nesting.get("cells") if isinstance(nesting.get("cells"), list) else []:
            values = row.get("values") if isinstance(row, dict) and isinstance(row.get("values"), list) else []
            ok = len(values) == 2 and all(_finite_positive(value) for value in values)
            lines.append("| %s | %s | %s | %s |" % (
                row.get("name") if isinstance(row, dict) else "NA",
                "%.1f ms" % (values[0] * 1000.0) if ok else "NA", "%.1f ms" % (values[1] * 1000.0) if ok else "NA",
                "%.4f" % (values[1] / values[0]) if ok else "NA"))
        if nesting.get("exit") not in (None, 0):
            lines.append(f"Frontend-nesting exit: {nesting.get('exit')}.")
    linker = receipt.get("wild_linker") if isinstance(receipt, dict) else None
    if isinstance(linker, dict) and linker.get("requested"):
        linkers = linker.get("linkers") if isinstance(linker.get("linkers"), dict) else {}
        named = []
        for role in ("wild", "comparator"):
            item = linkers.get(role) if isinstance(linkers.get(role), dict) else {}
            named.append("%s `%s` (%s, `%s`)" % (role, item.get("path", "NA"), item.get("version", "NA"),
                                                 str(item.get("sha256", "NA"))[:12]))
        substituted = isinstance(linkers.get("comparator"), dict) and linkers["comparator"].get("substituted") is True
        lines += ["", f"Host Wild linker ({WILD_LINKER_PROFILE['name']}): {linker.get('status', 'NA')} (direct link-only "
                  "ide cells on the head's own objects; A = comparator, B = Wild; seeded paired medians with the "
                  "harness's bootstrap 95% interval; report-only). " + "; ".join(named) + "." +
                  (" mold is not installed on this host, so the comparator is its default ld." if substituted else ""), "",
                  "| Cell | Pairs | A median (ms) | B median (ms) | Paired B / A | 95% interval |",
                  "| --- | ---: | ---: | ---: | ---: | --- |"]
        for row in linker.get("cells") if isinstance(linker.get("cells"), list) else []:
            row = row if isinstance(row, dict) else {}
            values = row.get("values") if isinstance(row.get("values"), list) else []
            interval = row.get("ci95") if isinstance(row.get("ci95"), list) else []
            ok = len(values) == 2 and all(_finite_positive(value) for value in values) and _finite_positive(row.get("ratio"))
            bounds = len(interval) == 2 and all(_finite_positive(value) for value in interval)
            lines.append("| %s | %s | %s | %s | %s | %s |" % (
                row.get("name", "NA"), row.get("pairs", "NA"), "%.3f" % values[0] if ok else "NA",
                "%.3f" % values[1] if ok else "NA", "%.4f" % row["ratio"] if ok else "NA",
                "[%.4f, %.4f]" % tuple(interval) if bounds else "NA"))
        for key, title in (("build_exit", "input build"), ("exit", "harness")):
            if linker.get(key) not in (None, 0):
                lines.append(f"Wild linker {title} exit: {linker.get(key)}.")
    link_tls = receipt.get("link_tls") if isinstance(receipt, dict) else None
    if isinstance(link_tls, dict) and link_tls.get("requested"):
        profile = LINK_TLS_PROFILE
        lines += ["", f"TLS companion links ({profile['name']}): {link_tls.get('status', 'NA')} (fresh `ide cc` link "
                  "processes of both binaries on frozen TLS-heavy inputs, byte-identical images required, "
                  f"{profile['pairs']} alternating pairs per cell on CPU {profile['cpu']}; report-only).", "",
                  "| Cell | Base (ms) | Head (ms) | Head / base [95% interval] | Head faster |",
                  "| --- | ---: | ---: | ---: | ---: |"]
        for row in link_tls.get("cells") if isinstance(link_tls.get("cells"), list) else []:
            row = row if isinstance(row, dict) else {}
            values = [row.get(key) for key in ("baseline_ns", "candidate_ns", "ratio", "ratio_low", "ratio_high")]
            ok = all(_finite_positive(value) for value in values)
            lines.append("| %s | %s | %s | %s | %s/%d |" % (
                row.get("name", "NA"), "%.3f" % (values[0] / 1e6) if ok else "NA",
                "%.3f" % (values[1] / 1e6) if ok else "NA",
                "%.4f [%.4f, %.4f]" % tuple(values[2:]) if ok else "NA", row.get("candidate_faster", "NA"),
                profile["pairs"]))
        if link_tls.get("exit") not in (None, 0):
            lines.append(f"TLS companion link harness exit: {link_tls.get('exit')}.")
    selfhost = receipt.get("selfhost_inline") if isinstance(receipt, dict) else None
    if isinstance(selfhost, dict) and selfhost.get("requested"):
        profile = SELFHOST_INLINE_PROFILE
        digest = selfhost.get("digest") if isinstance(selfhost.get("digest"), dict) else {}
        lines += ["", f"Self-built inline-mode compilers ({profile['name']}): {selfhost.get('status', 'NA')} (each "
                  "side's stage-1 compiler built with `-fcanonical-inline`, both compiling the merge-base tree with it, "
                  f"byte-identical outputs required, {profile['pairs']} ABBA pairs on CPU {profile['cpu']} plus "
                  "topdown and sampling; report-only).", "",
                  "| Metric | Base | Head | Head / base [95% CI] |", "| --- | ---: | ---: | ---: |"]
        metrics = digest.get("metrics") if isinstance(digest.get("metrics"), dict) else {}
        for key in ("wall", "task_clock", "instructions", "cycles", "ipc", "branch_misses", "peak_rss"):
            row = metrics.get(key) if isinstance(metrics.get(key), dict) else {}
            values = [row.get(name) for name in ("a_median", "b_median", "ratio")]
            interval = [row.get(name) for name in ("ci_low", "ci_high")]
            lines.append("| %s | %s | %s | %s |" % (
                key, *("%.6g" % value if _finite_positive(value) else "NA" for value in values[:2]),
                ("%.4f" % values[2] if _finite_positive(values[2]) else "NA") +
                (" [%.4f, %.4f]" % tuple(interval) if all(_finite_positive(value) for value in interval) else "")))
        code = digest.get("code_bytes") if isinstance(digest.get("code_bytes"), dict) else {}
        if all(_finite_positive(code.get(name)) for name in ("a_value", "b_value", "ratio")):
            lines.append("| code bytes | %d | %d | %.4f |" % (code["a_value"], code["b_value"], code["ratio"]))
        frontend = digest.get("frontend") if isinstance(digest.get("frontend"), list) else []
        if frontend:
            lines += ["", "| Front-end topdown metric | Base | Head | Change |", "| --- | ---: | ---: | ---: |"]
            for row in frontend:
                row = row if isinstance(row, dict) else {}
                change = row.get("relative_change_percent")
                lines.append("| %s %s | %s | %s | %s |" % (
                    row.get("group", "NA"), row.get("metric", "NA"),
                    *("%.6g" % row[key] if isinstance(row.get(key), (int, float)) else "NA" for key in ("a", "b")),
                    "%+.2f%%" % change if isinstance(change, (int, float)) else "NA"))
        if selfhost.get("exit") not in (None, 0):
            lines.append(f"Self-built inline-mode harness exit: {selfhost.get('exit')}.")
    reasons = receipt.get("reasons") if isinstance(receipt, dict) else None
    warnings = summary.get("warnings")
    for item in (*notes, *(reasons if isinstance(reasons, list) else ()),
                 *(warnings if isinstance(warnings, list) else ())):
        lines.append(f"- {item}")
    return "\n".join(lines)


def dumps(value: object) -> str:
    return json.dumps(value, sort_keys=True, indent=2)
