#!/usr/bin/env python3
"""Validate a 9700X compiler receipt and publish its exact-head check (#2752, #2769).

Run from trusted `main` by the hosted `publish-compiler` (main mode) and
`publish-pull` (pull mode) jobs of `.github/workflows/9700x-direct-bench.yml`,
the only jobs there that complete the check. It reads this run's evidence
artifact through the API as data (a size-bounded zip; nothing in it is
executed), re-checks the receipt against the identities authorization produced
in the same attempt, requires the observed host to be the approved Zen 5 host,
requires the job's retained #2758 machine-specification record
(machine-specifications/report.json, see machine_record) to name that host
with the receipt's own CPU model and this run, attempt and compare job,
re-derives validity from the lab's own summary.json and the throughput
corpus's own summary.json and metadata.json (#2761), and, when the receipt
names the scaling profile (#424), each scaling bundle's own scaling.json and
scaling-metadata.json, and, when the receipt holds an opt-in same-candidate
flag-pair profile (#48 inline, #3397 ast-pilot, #3011 constant-reuse) or the #1125
per-process floor, that profile's raw lab documents, and completes the
attempt's check run named check_name(mode) with external ID attempt_marker(...)
on the head. The checks are report-only; nothing gates merging on them.

Conclusions (the performance verdict never decides; report-only):
    success  a valid core measurement, whatever its direction
    neutral  a pull request head moved before measurement
    failure  authorization refused, evidence missing, mismatched or invalid,
             or an unimplemented regression policy was requested
A failure says the candidate was not benchmarked; it is never relabelled.
Whatever the conclusion, the check's summary, its JSON (`inventory_coverage`)
and the main commit report name the inventory rows that this attempt's
validated legs discharged, from the trusted mode and the decided conclusion
(compiler_receipt.covered_inventory_rows, #2761). Only a success discharges a
row.

The check is the attempt's own check (#2803): the request bridge or the start
job usually created it queued and moved it to in_progress; this completes the
same check through compiler_github.complete_check, creates it only when none
exists, and leaves an already completed one unchanged. The report carries
explicit links to the check, the originating workflow attempt and the
evidence artifact.

Main mode also writes the commit report entry (#2804) to the `comment` job
output: validated data plus the rendered commit_report markdown, which the
separately permitted comment job upserts with compiler_comment.py.

Publication-only recovery (#2804): with BQ_RECOVER_RUN_ID and
BQ_RECOVER_ATTEMPT, run by `.github/workflows/9700x-compiler-report.yml`, it
re-derives a past main attempt's identity from GitHub's records
(recovered_identity, bind_request), re-reads that attempt's retained
evidence, and emits only the comment entry. It writes no check and runs no
measurement; expired or unverifiable evidence is reported and not published.
Only an attempt whose authenticated workflow revision has no record retention
step may lack the machine record (machine_record_required); a record that is
present is still checked.

Sealed raw evidence v1 (#2918, docs/compiler-evidence-v1.md): an ordinary
artifact that carries evidence-manifest.tsv or evidence-seal.txt is extracted
with the strict sampling_archive reader and verified by the trusted native
`./build.sh compiler_evidence verify` under the identity this publisher
authorized (verify_evidence_bundle); a pull snapshot-v1 receipt's route selects
the snapshot inventory (#3211). The verified bytes are then replayed: the
lab's raw pairs through the trusted uarch_lab statistics (sealed_lab_replay,
lab_raw_inference) and the corpus through this checkout's `bench_throughput
compare` in a disposable copy (sealed_corpus_replay); the saved summaries must
follow. The receipt's evidence_bundle must name the verified inventory and
manifest (evidence_bundle_problems). A measured ordinary attempt must be sealed;
only publication-only recovery of a run whose producer predates sealing
(sealing_required) may be legacy-unsealed, its outcome standing with its raw
bytes reported unverified. Anything else fails closed.

Map: validate_inline_bundle, validate_ast_pilot_bundle and validate_constant_reuse_bundle (opt-in flag pairs),
validate_process_floor_bundle (#1125 per-process floor),
validate_real_source_bundle, validate_production_profile_bundle,
validate_compiler_probes_bundle, validate_frontend_selector_bundle,
validate_frontend_nesting_bundle (#2676 deep nesting) and
validate_wild_linker_bundle (#2761 opt-in legs), validate_link_tls_bundle (#1915),
validate_selfhost_inline_bundle (#58),
validate_ast_retirement_bundle (#4058 two-binary retirement cells and raw replay),
decide (pure decision), sealed_lab_replay, sealed_corpus_replay,
verify_evidence_bundle, evidence_bundle_problems and sealing_required (sealed raw
evidence), lab_raw_inference (shared lab replay), inventory_rows (discharged inventory rows),
machine_record and machine_record_required (#2758 host facts), read_evidence,
verify_analyzer_request_freshness (fresh request selectors), queue_delay, links,
commit_report, recovered_identity, bind_request, report_check_url, main.
Api lives in compiler_github.
"""

from __future__ import annotations

import io
import base64
import binascii
import hashlib
from pathlib import Path
import json
import os
import re
import stat
import subprocess
import sys
import urllib.error
import urllib.parse
import zipfile
import zlib
from datetime import datetime

from authorize_compiler import verify as verify_main
from compiler_github import (ARTIFACT_LIMIT, BENCH_WORKFLOW, COMPARE_JOBS, SERVER, TEXT_LIMIT, Api, complete_check,
                             measured, owned_checks, parse_chain, run_url, stamp)
from inline_acceptance import metrics as inline_metrics, validate_documents as validate_inline_documents
from selfhost_inline_compare import validate_compare_documents as validate_selfhost_inline_documents
from ast_pilot_acceptance import (cell_record as ast_pilot_cell_record, validate_documents as validate_ast_pilot_documents,
                                  validate_probe as validate_ast_pilot_probe)
from ast_retirement_acceptance import (cell_record as ast_retirement_cell_record,
                                      validate_documents as validate_ast_retirement_documents,
                                      validate_probe as validate_ast_retirement_probe)
from compiler_receipt import EVIDENCE_MANIFEST, EVIDENCE_SEAL, evidence_identity, evidence_inventory
from constant_reuse_acceptance import (cell_record as constant_reuse_cell_record,
                                       validate_documents as validate_constant_reuse_documents)
from process_floor_acceptance import (cell_record as process_floor_cell_record,
                                      validate_documents as validate_process_floor_documents)
from compiler_receipt import (validate_closure, APPROVED_HOST, ANALYZER_PROFILE, ANALYZER_PROFILE_BY_LINE, ANALYZER_REQUIRED_FILES,
                              ANALYZER_REQUEST_LINES, ANALYZER_REQUEST_PATH, DECIMAL, IDENTITY_KEYS,
                              INLINE_ACCEPTANCE_REQUEST_LINE, INLINE_ACCEPTANCE_PROFILE,
                              AST_PILOT_ACCEPTANCE_REQUEST_LINE, AST_PILOT_ACCEPTANCE_PROFILE, AST_PILOT_CELLS,
                              validate_ast_pilot_acceptance,
                              AST_RETIREMENT_REQUEST_LINE, AST_RETIREMENT_PROFILE, AST_RETIREMENT_CELLS,
                              validate_ast_retirement_acceptance, ast_retirement_cell_command,
                              CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE, CONSTANT_REUSE_ACCEPTANCE_PROFILE, CONSTANT_REUSE_CELLS,
                              validate_constant_reuse_acceptance,
                              PROCESS_FLOOR_REQUEST_LINE, PROCESS_FLOOR_PROFILE, PROCESS_FLOOR_CELLS,
                              validate_process_floor_acceptance,
                              THROUGHPUT_REAL_SOURCE_PROFILE, THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
                              throughput_real_source_digest, validate_throughput_real_source,
                              PRODUCTION_PROFILE_PROFILE, PRODUCTION_PROFILE_REQUEST_LINE, validate_production_profile,
                              COMPILER_PROBES_PROFILE, COMPILER_PROBES_REQUEST_LINE, compiler_probes_digest,
                              validate_compiler_probes,
                              FRONTEND_SELECTOR_PROFILE, FRONTEND_SELECTOR_REQUEST_LINE, frontend_selector_digest,
                              validate_frontend_selector,
                              FRONTEND_NESTING_PROFILE, FRONTEND_NESTING_REQUEST_LINE, frontend_scaling_digest,
                              validate_frontend_nesting,
                              WILD_LINKER_PROFILE, WILD_LINKER_REQUEST_LINE, validate_wild_linker, wild_linker_bundle_files,
                              LINK_TLS_PROFILE, LINK_TLS_REQUEST_LINE, link_tls_digest, validate_link_tls,
                              SELFHOST_INLINE_PROFILE, SELFHOST_INLINE_REQUEST_LINE, selfhost_inline_digest,
                              validate_selfhost_inline,
                              wild_linker_digest,
                              MODES, PROFILE, RECEIPT_SCHEMA, SHA, attempt_marker, check_marker,
                              check_name, classify, classify_scaling, classify_throughput, host_problem, number, _number,
                              range_label, regression_policy, render, SCALING_PROFILE, scaling_digest, validate_inline_acceptance,
                              THROUGHPUT_PROFILE, throughput_digest, validate_analyzer_bundle, covered_inventory_rows,
                              render_inventory_coverage,
                              HARNESS_ADMISSIONS, THROUGHPUT_HARNESS_FLOOR, THROUGHPUT_HISTORICAL_HARNESS_REVISION,
                              THROUGHPUT_HISTORICAL_PROFILE, THROUGHPUT_HISTORICAL_REQUEST_LINE)

ARTIFACT_PREFIX = "buster-9700x-compiler-"
MEMBER_LIMIT = 8 * 1024 * 1024
# Sealed raw evidence (#2918): the bundle's 4096 entries plus the receipt and machine record.
EVIDENCE_ARCHIVE_MEMBERS = 8192
EVIDENCE_VERIFY_TIMEOUT_SECONDS = 600
EVIDENCE_VERIFY_COMMAND = (os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                        "build.sh"), "compiler_evidence")
ANALYZER_MEMBER_LIMIT = 32 * 1024 * 1024
ANALYZER_BUNDLE_LIMIT = ARTIFACT_LIMIT * 3 // 4
AUTHORIZE_JOBS = {"main": "Authorize the main commit comparison"}
REPORT_MARKDOWN_LIMIT = 36000
# The #2758 startup record, retained by the 9700X comparison job (#2761).
MACHINE_RECORD = "machine-specifications/report.json"
MACHINE_SCHEMA = "buster-machine-specifications-v1"
MACHINE_JOBS = {"main": "compare", "pull": "compare-pull"}
MACHINE_STATUSES = ("available", "partial", "unknown", "unavailable", "not_applicable")
MACHINE_REQUIRED = ("schema", "run_id", "run_attempt", "job", "cpu_model", "machine_arch")
# A bench workflow revision retains the record exactly when one of its steps is named this.
MACHINE_RETENTION_STEP = "machine-specification record\n"
MACHINE_OPTIONAL = ("cpu_vendor", "cpu_logical_online", "cpu_process_available", "cpu_affinity",
                    "memory_total_bytes", "kernel_release", "execution_context")


def artifact_name(head: str, attempt: str) -> str:
    return f"{ARTIFACT_PREFIX}{head}-{attempt}"



INLINE_BUNDLE_FILES = {
    "acceptance": "inline_acceptance/acceptance.json",
    "stage1_ids": "inline_acceptance/stage1/identities.json",
    "stage1_summary": "inline_acceptance/stage1/summary.json",
    "stage1_meta": "inline_acceptance/stage1/compare.json",
    "stage1_a": "inline_acceptance/stage1/a/lab.json",
    "stage1_b": "inline_acceptance/stage1/b/lab.json",
    "selfhost_ids": "inline_acceptance/selfhost/identities.json",
    "selfhost_summary": "inline_acceptance/selfhost/summary.json",
    "selfhost_meta": "inline_acceptance/selfhost/compare.json",
    "selfhost_a": "inline_acceptance/selfhost/a/lab.json",
    "selfhost_b": "inline_acceptance/selfhost/b/lab.json",
}


def validate_inline_bundle(receipt_inline: object, bundle: object, head: str, candidate_sha: str) -> list[str]:
    """Re-derive the requested inliner result from raw profile summaries and configs."""
    problems: list[str] = []
    if not isinstance(receipt_inline, dict) or receipt_inline.get("requested") is not True:
        return problems
    if receipt_inline.get("status") != "complete" or receipt_inline.get("exit") != 0:
        problems.append("receipt issue #48 inline run did not complete successfully")
    if receipt_inline.get("request_line") != INLINE_ACCEPTANCE_REQUEST_LINE:
        problems.append("receipt inline request selector is not the exact supported line")
    if receipt_inline.get("profile") != INLINE_ACCEPTANCE_PROFILE:
        problems.append("receipt inline profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(INLINE_BUNDLE_FILES):
        return [*problems, "requested issue #48 raw evidence bundle is incomplete"]
    acceptance = bundle.get("acceptance")
    if not isinstance(acceptance, dict):
        return [*problems, "raw issue #48 acceptance document is missing"]
    if receipt_inline.get("summary") != acceptance:
        problems.append("receipt and raw inline acceptance documents differ")
    problems.extend(validate_inline_acceptance(acceptance, head, candidate_sha))
    try:
        first = validate_inline_documents(bundle["stage1_summary"], bundle["stage1_meta"],
                                          {"a": bundle["stage1_a"], "b": bundle["stage1_b"]},
                                          candidate_sha, candidate_sha)
        stage1 = acceptance.get("stage1_compilers") or {}
        def identities(value: object, label: str) -> dict:
            if not isinstance(value, dict) or set(value) != {"off", "on"}:
                raise RuntimeError(f"{label} output identity manifest is malformed")
            for mode in ("off", "on"):
                row = value.get(mode)
                if (not isinstance(row, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(row.get("sha256", ""))) or
                        type(row.get("size_bytes")) is not int or row["size_bytes"] <= 0):
                    raise RuntimeError(f"{label} {mode} output identity is malformed")
            return value
        first_ids = identities(bundle["stage1_ids"], "stage-1")
        recorded_stage1 = acceptance.get("stage1_compilers")
        if recorded_stage1 != first_ids:
            raise RuntimeError("stage-1 compiler identities do not match the raw identity manifest")
        off_sha = first_ids["off"]["sha256"]
        on_sha = first_ids["on"]["sha256"]
        second = validate_inline_documents(bundle["selfhost_summary"], bundle["selfhost_meta"],
                                           {"a": bundle["selfhost_a"], "b": bundle["selfhost_b"]},
                                           off_sha, on_sha)
        second_ids = identities(bundle["selfhost_ids"], "self-host")
        if second_ids != first_ids:
            raise RuntimeError("stage-2 output identities do not reproduce the stage-1 compilers")
        if acceptance.get("stage1", {}).get("metrics") != inline_metrics(first):
            problems.append("stage-1 acceptance metrics do not match raw summary")
        if acceptance.get("stage1", {}).get("outputs_identical") != first.get("outputs_identical"):
            problems.append("stage-1 output identity result does not match raw summary")
        if acceptance.get("selfhost_runtime", {}).get("metrics") != inline_metrics(second):
            problems.append("self-host runtime acceptance metrics do not match raw summary")
        if acceptance.get("selfhost_runtime", {}).get("outputs_identical") != second.get("outputs_identical"):
            problems.append("self-host output identity result does not match raw summary")
    except (RuntimeError, AttributeError, TypeError) as error:
        problems.append(f"raw issue #48 profile is incomplete or mismatched: {error}")
    return problems


AST_PILOT_BUNDLE_FILES = {
    "acceptance": "ast_pilot_acceptance/acceptance.json",
    "probe": "ast_pilot_acceptance/probe/probe.json",
    **{f"{cell}_{key}": f"ast_pilot_acceptance/{cell}/{member}" for cell in AST_PILOT_CELLS
       for key, member in (("summary", "summary.json"), ("meta", "compare.json"), ("a", "a/lab.json"), ("b", "b/lab.json"))},
}


def validate_ast_pilot_bundle(receipt_ast: object, bundle: object, head: str, candidate_sha: str) -> list[str]:
    """Re-derive the requested #3397 result from its raw cell summaries, configs and tree probe."""
    if receipt_ast is None:
        return []
    problems: list[str] = []
    if not isinstance(receipt_ast, dict) or receipt_ast.get("requested") is not True:
        return ["receipt ast-pilot record is present but not a request"]
    if receipt_ast.get("status") != "complete" or receipt_ast.get("exit") != 0:
        problems.append("receipt issue #3397 ast-pilot run did not complete successfully")
    if receipt_ast.get("request_line") != AST_PILOT_ACCEPTANCE_REQUEST_LINE:
        problems.append("receipt ast-pilot request selector is not the exact supported line")
    if receipt_ast.get("profile") != AST_PILOT_ACCEPTANCE_PROFILE:
        problems.append("receipt ast-pilot profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(AST_PILOT_BUNDLE_FILES):
        return [*problems, "requested issue #3397 raw evidence bundle is incomplete"]
    acceptance = bundle.get("acceptance")
    if not isinstance(acceptance, dict):
        return [*problems, "raw issue #3397 acceptance document is missing"]
    if receipt_ast.get("summary") != acceptance:
        problems.append("receipt and raw ast-pilot acceptance documents differ")
    problems.extend(validate_ast_pilot_acceptance(acceptance, head, candidate_sha))
    try:
        cells = acceptance.get("cells") if isinstance(acceptance.get("cells"), dict) else {}
        for cell in AST_PILOT_CELLS:
            raw = validate_ast_pilot_documents(cell, bundle[f"{cell}_summary"], bundle[f"{cell}_meta"],
                                               {"a": bundle[f"{cell}_a"], "b": bundle[f"{cell}_b"]}, candidate_sha)
            if cells.get(cell) != ast_pilot_cell_record(raw):
                problems.append(f"ast-pilot {cell} acceptance metrics or identity results do not match its raw summary")
        if acceptance.get("tree") != validate_ast_pilot_probe(bundle["probe"]):
            problems.append("ast-pilot retained-bytes record does not match the raw tree probe")
    except (RuntimeError, AttributeError, TypeError, KeyError) as error:
        problems.append(f"raw issue #3397 profile is incomplete or mismatched: {error}")
    return problems


AST_RETIREMENT_BUNDLE_FILES = {
    "acceptance": "ast_retirement_acceptance/acceptance.json",
    "source": "ast_retirement_acceptance/source.json",
    "rss_build": "ast_retirement_acceptance/rss-helper/build.json",
    "owner": "ast_retirement_acceptance/owner/metadata.json",
    **{f"{cell}_{key}": f"ast_retirement_acceptance/{cell}/{member}" for cell in AST_RETIREMENT_CELLS
       for key, member in (("summary", "summary.json"), ("meta", "compare.json"), ("a", "a/lab.json"),
                           ("b", "b/lab.json"), ("reference", "reference.json"))},
    **{f"{cell}_probe": f"ast_retirement_acceptance/probe/{cell}/probe.json" for cell in AST_RETIREMENT_CELLS},
}


def ast_retirement_reference(cell: str, bundle: dict) -> dict:
    """Verify the fixed bounded transport of each reference image and its raw diagnostics."""
    prefix = f"ast_retirement_acceptance/{cell}/"
    files, manifest = bundle["files"], bundle[f"{cell}_reference"]
    summary = bundle[f"{cell}_summary"]
    if not isinstance(manifest, dict) or set(manifest) != {"a", "b"}:
        raise ValueError(f"{cell} reference image manifest is malformed")
    if manifest["a"] != manifest["b"]:
        raise ValueError(f"{cell} compiler arms do not bind one identical shared raw reference")
    images, diagnostics, decoded_chunks = {}, {}, {}
    for key in ("a", "b"):
        image = manifest[key]
        if not isinstance(image, dict) or set(image) != {"size_bytes", "sha256", "chunks"} or \
                type(image.get("size_bytes")) is not int or not 0 <= image["size_bytes"] <= ANALYZER_MEMBER_LIMIT * 2 or \
                not re.fullmatch(r"[0-9a-f]{64}", str(image.get("sha256", ""))) or \
                not isinstance(image.get("chunks"), list) or len(image["chunks"]) != 2:
            raise ValueError(f"{cell} {key} reference image identity is malformed")
        parts = []
        for index, chunk in enumerate(image["chunks"]):
            wanted = f"reference.{index:03d}.bin.gz"
            raw = files.get(prefix + wanted)
            if not isinstance(chunk, dict) or set(chunk) != {"path", "compression", "size_bytes", "sha256",
                                                           "stored_size_bytes", "stored_sha256"} or \
                    chunk.get("path") != wanted or not isinstance(raw, bytes) or \
                    chunk.get("compression") != "gzip-v1" or type(chunk.get("size_bytes")) is not int or \
                    chunk["size_bytes"] != min(max(0, image["size_bytes"] - index * ANALYZER_MEMBER_LIMIT), ANALYZER_MEMBER_LIMIT) or \
                    type(chunk.get("stored_size_bytes")) is not int or chunk["stored_size_bytes"] != len(raw) or \
                    len(raw) > ANALYZER_MEMBER_LIMIT or chunk.get("stored_sha256") != hashlib.sha256(raw).hexdigest() or \
                    raw[:10] != b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff":
                raise ValueError(f"{cell} {key} reference chunk differs from its bounded stored bytes")
            decoded = decoded_chunks.get(wanted)
            if decoded is None:
                try:
                    decoder = zlib.decompressobj(31)
                    decoded = decoder.decompress(raw, chunk["size_bytes"] + 1)
                except zlib.error as error:
                    raise ValueError(f"{cell} {key} reference gzip is invalid: {error}") from error
                if len(decoded) != chunk["size_bytes"] or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail or \
                        chunk.get("sha256") != hashlib.sha256(decoded).hexdigest():
                    raise ValueError(f"{cell} {key} reference gzip differs from its bounded raw identity")
                decoded_chunks[wanted] = decoded
            parts.append(decoded)
        images[key] = b"".join(parts)
        if image["size_bytes"] != len(images[key]) or image["sha256"] != hashlib.sha256(images[key]).hexdigest() or \
                AST_RETIREMENT_CELLS[cell]["operation"] == "object" and not images[key]:
            raise ValueError(f"{cell} {key} reference image hash or size differs from retained chunks")
        diagnostics[key] = files.get(prefix + key + "/reference.diagnostics")
        if not isinstance(diagnostics[key], bytes):
            raise ValueError(f"{cell} {key} reference diagnostics are missing")
    if summary.get("outputs_identical") is not (images["a"] == images["b"]):
        raise ValueError(f"{cell} output identity result differs from the raw reference images")
    if summary.get("diagnostics_identical") is not (diagnostics["a"] == diagnostics["b"]):
        raise ValueError(f"{cell} diagnostics identity result differs from the raw transcripts")
    if summary.get("outputs_identical") is not True or summary.get("diagnostics_identical") is not True:
        raise ValueError(f"{cell} reference images or diagnostics differ between the two binaries")
    return manifest


def ast_retirement_raw_cell(cell: str, bundle: dict, source: dict, binaries: dict,
                            cpu_model: str) -> dict:
    """Replay one fixed cell from the retained pairs, counters and observed variant host records."""
    from sampling_qualification_receipt import _lab
    prefix = f"ast_retirement_acceptance/{cell}/"
    files = bundle["files"]
    summary, meta = bundle[f"{cell}_summary"], bundle[f"{cell}_meta"]
    labs = {key: bundle[f"{cell}_{key}"] for key in ("a", "b")}
    raw = validate_ast_retirement_documents(cell, summary, meta, labs, binaries, source)
    observer = meta["rss_observer"]
    if observer["build"] != bundle["rss_build"]:
        raise ValueError(f"{cell} observer differs from the retained native build")
    ast_retirement_reference(cell, bundle)
    records = sampling_json(files, prefix + "pairs.json", False)
    if not isinstance(records, list) or len(records) != AST_RETIREMENT_PROFILE["pairs"] * 2:
        raise ValueError(f"{cell} complete raw pair population is missing")
    config, plan = meta.get("config"), meta.get("plan")
    if meta.get("version") != 1 or meta.get("mode") != "compare" or not isinstance(plan, dict) or \
            not isinstance(config, dict) or summary.get("method") != _lab.COMPARE_METHOD or \
            summary.get("plan") != dict(plan, seed=config.get("seed"), confidence=_lab.CONFIDENCE,
                bootstrap_resamples=_lab.BOOTSTRAP_RESAMPLES, complete_pairs=AST_RETIREMENT_PROFILE["pairs"], fresh_copy=True):
        raise ValueError(f"{cell} saved inference method or complete plan differs from its raw configuration")
    if summary.get("host", {}).get("cpu_model") != cpu_model or not APPROVED_HOST.search(cpu_model):
        raise ValueError(f"{cell} raw summary host differs from the approved receipt host")
    for key, role in (("a", "baseline"), ("b", "candidate")):
        variant = (meta.get("variants") or {}).get(key)
        observed, lab_config = summary[role], labs[key]["config"]
        if not isinstance(variant, dict) or variant.get("role") != role or \
                variant.get("ide") != lab_config.get("ide") or observed.get("path") != variant.get("ide") or \
                any(variant.get(field) != binaries[role][field] for field in ("sha256", "size_bytes")) or \
                observed.get("identical_runs") != AST_RETIREMENT_PROFILE["pairs"]:
            raise ValueError(f"{cell} {key} raw variant identity or population differs from the prepared binary")
        environment = sampling_json(files, prefix + key + "/env/env.json")
        if environment.get("cpu_model") != cpu_model or environment.get("cpu") != AST_RETIREMENT_PROFILE["cpu"] or \
                environment.get("ide_sha256") != binaries[role]["sha256"] or \
                environment.get("git_revision") != source["revision"]:
            raise ValueError(f"{cell} {key} raw observed host, compiler or source differs from the receipt")
        perf = (labs[key].get("capabilities") or {}).get("perf_stat")
        collection = labs[key].get("collection")
        if not isinstance(perf, dict) or perf.get("usable") is not True or not isinstance(perf.get("reason"), str) or \
                not isinstance(collection, dict) or collection.get("metrics_out") is not False:
            raise ValueError(f"{cell} {key} has unavailable counters or instrumented timed collection")
    for index, record in enumerate(records):
        if not isinstance(record, dict) or not _number(record.get("maxrss_bytes")) or record["maxrss_bytes"] <= 0:
            raise ValueError(f"{cell} raw pair {index + 1} lacks positive wait4 peak RSS")
        number, key = index // 2 + 1, ("ab" if index // 2 % 2 == 0 else "ba")[index % 2]
        ast_retirement_rss_record(cell, number, key, record, observer, binaries, meta, files,
                                  bundle["owner"]["ownership"]["work_root"] + "/ast-retirement-acceptance")
        csv_bytes = files.get(prefix + f"pairs/{number:04d}-{key}.csv")
        if not isinstance(csv_bytes, bytes):
            raise ValueError(f"{cell} raw retired-instruction counter file is missing")
        counters = [row for row in _lab.parse_stat(csv_bytes.decode("utf-8"))
                    if _lab.normalize_event(row.get("event", "")) == "instructions"]
        if len(counters) != 1 or counters[0].get("event") != "instructions:u" or \
                not _number(counters[0].get("value")) or counters[0]["value"] <= 0:
            raise ValueError(f"{cell} raw pair {number} {key} lacks positive retired instructions:u")
    if type(config.get("seed")) is not int or not _number(config.get("min_effect_percent")) or \
            not 0 <= config["min_effect_percent"] < 100:
        raise ValueError(f"{cell} raw statistical configuration is malformed")
    lab_raw_inference(files, prefix, records, labs, summary, meta, config["seed"], config["min_effect_percent"])
    return raw


def ast_retirement_rss_record(cell: str, number: int, key: str, record: dict, observer: dict,
                              binaries: dict, meta: dict, files: dict, output_root: str | None = None) -> None:
    """Bind every exact compiler/wrapper argv to its independent raw packet."""
    from sampling_qualification_receipt import _lab
    role = "baseline" if key == "a" else "candidate"
    binding, workload, wrapper = (record.get(name) for name in ("child_rss_binding", "compiler_argv", "wrapper_argv"))
    if not isinstance(binding, dict) or set(binding) != {"nonce", "binary_sha256", "argv_sha256", "process_group"} or \
            not isinstance(workload, list) or not workload or not all(isinstance(arg, str) for arg in workload) or \
            not isinstance(wrapper, list) or not all(isinstance(arg, str) for arg in wrapper):
        raise ValueError(f"{cell} {number}/{key} native RSS sample command/binding is missing")
    nonce = hashlib.sha256((observer["run_nonce"] + f":{number}:{key}").encode("ascii")).hexdigest()[:32]
    if binding["nonce"] != nonce or binding["binary_sha256"] != binaries[role]["sha256"] or \
            binding["argv_sha256"] != _lab.child_rss_argv_digest(workload):
        raise ValueError(f"{cell} {number}/{key} native RSS sample identity differs")
    helper = observer["build"]["binary"]["path"]
    try:
        index = wrapper.index(helper)
        packet_path = wrapper[index + 2]
    except (ValueError, IndexError) as error:
        raise ValueError(f"{cell} {number}/{key} native RSS wrapper is malformed") from error
    packet = Path(packet_path)
    directory = packet.parent.parent
    if not packet.is_absolute() or str(packet) != packet_path or any(part in (".", "..") for part in packet.parts) or \
            packet.name != f"{number:04d}-{key}.rss" or packet.parent.name != "pairs" or directory.name != cell or \
            (output_root is not None and directory.parent != Path(output_root)):
        raise ValueError(f"{cell} {number}/{key} native RSS output path differs")
    binary = Path(workload[0])
    original = Path(meta["variants"][key]["ide"])
    if not binary.is_absolute() or len(binary.parents) < 3 or binary.parents[2] != directory / key or \
            binary.parent.parent.name != "instances" or not re.fullmatch(r"run-[A-Za-z0-9_]+", binary.parent.name) or binary.name != original.name:
        raise ValueError(f"{cell} {number}/{key} native RSS compiler is not the declared fresh copy")
    expected_workload = [str(binary), *ast_retirement_cell_command(cell)]
    if AST_RETIREMENT_CELLS[cell]["operation"] == "object":
        expected_workload += ["-o", str(directory / key / "out.exe")]
    csv = str(packet.with_suffix(".csv"))
    expected_wrapper = ["taskset", "-c", str(AST_RETIREMENT_PROFILE["cpu"]), meta["config"]["perf"], "stat", "-x,",
                        "-o", csv, "-e", ",".join(_lab.TIMED_EVENTS), "--", helper, "--output", str(packet),
                        "--nonce", nonce, "--binary-sha256", binding["binary_sha256"], "--argv-sha256", binding["argv_sha256"], "--", *expected_workload]
    if workload != expected_workload or wrapper != expected_wrapper:
        raise ValueError(f"{cell} {number}/{key} native RSS compiler/wrapper argv differs from the fixed profile")
    data = files.get(f"ast_retirement_acceptance/{cell}/pairs/{number:04d}-{key}.rss")
    observed = _lab.child_rss_packet(data, binding)
    if record.get("child_rss") != observed or _lab.rss_value(record) is None:
        raise ValueError(f"{cell} {number}/{key} native RSS raw observation is unavailable or differs from the saved pair")


def ast_retirement_owner(bundle: dict, base: str, head: str, trusted_revision: str | None) -> None:
    """Replay the AST-only native lifecycle proof, not a whole-route population."""
    import compiler_owned_phase as contract
    metadata = bundle["owner"]
    if not isinstance(metadata, dict) or set(metadata) != {"schema", "ownership"} or metadata["schema"] != "buster-ast-retirement-owner-v1":
        raise ValueError("AST retirement native owner metadata is missing")
    owner = metadata["ownership"]
    if not isinstance(owner, dict) or owner.get("schema") != contract.POPULATION_SCHEMA or owner.get("state") != "complete" or \
            owner.get("owned_preflight") is not False or type(owner.get("count")) is not int or owner.get("count") != 1 or not isinstance(owner.get("phases"), list) or len(owner["phases"]) != 1 or \
            any(not isinstance(owner.get(key), str) or not re.fullmatch(r"[a-f0-9]{40}", owner[key]) for key in ("trusted_revision", "trusted_tree")) or \
            (trusted_revision is not None and owner["trusted_revision"] != trusted_revision):
        raise ValueError("AST retirement scoped native owner population/trusted revision differs")
    for name in ("trusted_root", "directory", "work_root", "evidence_root", "python_path", "driver_path"):
        value = owner.get(name)
        if not isinstance(value, str) or not value.startswith("/") or str(Path(value)) != value or any(part == ".." for part in Path(value).parts):
            raise ValueError("AST retirement native owner path is malformed")
    row = owner["phases"][0]
    if not isinstance(row, dict) or type(row.get("ordinal")) is not int or row.get("ordinal") != 1 or row.get("file") != "0001.json" or row.get("phase") != "ast-retirement-acceptance" or \
            row.get("kind") != "run" or row.get("allow_exit_failure") is not False or type(row.get("timeout")) is not int or row.get("timeout") != 2700 or \
            type(row.get("bridge_wall_us")) is not int or row["bridge_wall_us"] <= 0 or row.get("cwd") != bundle["unity-syntax-only_meta"]["config"]["repo_root"]:
        raise ValueError("AST retirement native owner phase recipe differs")
    trusted, work, root = owner["trusted_root"], owner["work_root"], row["cwd"]
    argv = row.get("argv")
    if not isinstance(argv, list) or len(argv) != 21 or not isinstance(argv[6], str) or not argv[6].startswith("/"):
        raise ValueError("AST retirement native owner command is malformed")
    variants = bundle["unity-syntax-only_meta"]["variants"]
    expected = [owner["python_path"], "-B", trusted + "/tools/bench_direct/ast_retirement_acceptance.py",
                "--lab", trusted + "/tools/uarch_lab.py", "--harness", argv[6],
                "--baseline-ide", variants["a"]["ide"], "--candidate-ide", variants["b"]["ide"],
                "--repo-root", root, "--base-revision", base, "--head-revision", head,
                "--cpu", "2", "--output", work + "/ast-retirement-acceptance"]
    if argv != expected or owner["evidence_root"] != work + "/ast-retirement-owner" or owner["directory"] != owner["evidence_root"] + "/owned-phases":
        raise ValueError("AST retirement native owner exact invocation/paths differ")
    stem = "ast_retirement_acceptance/owner/owned-phases/0001.json"
    files = bundle["files"]
    raw, stdout, stderr, command, marker = (files.get(stem + suffix) for suffix in ("", ".stdout", ".stderr", ".argv", ".bootstrap.complete"))
    native = contract.read_record(raw)
    if command != contract.command_bytes(argv) or row.get("receipt_sha256") != contract.sha(raw) or \
            not isinstance(stdout, bytes) or not isinstance(stderr, bytes) or not isinstance(marker, bytes):
        raise ValueError("AST retirement native owner raw population differs")
    problems = contract.validate_record(native, argv, root, 2700, owner["driver_sha256"], stdout, stderr,
                                        receipt_path=owner["directory"] + "/0001.json")
    problems += contract.validate_bootstrap(native, marker, owner)
    if native.get("duration_us", 0) > row["bridge_wall_us"]:
        problems.append("AST retirement native owner wall omits owned work")
    if problems:
        raise ValueError("; ".join(problems))


def validate_ast_retirement_bundle(record: object, bundle: object, base: str, head: str,
                                   binaries: object, cpu_model: str, trusted_revision: str | None = None) -> list[str]:
    """Re-derive #4058's two-binary cells and retained components from exact sealed raw bytes."""
    problems: list[str] = []
    if record is not None:
        if not isinstance(record, dict) or record.get("requested") is not True:
            problems.append("receipt ast-retirement record is present but not a request")
        else:
            if record.get("status") != "complete" or record.get("exit") != 0:
                problems.append("receipt issue #4058 ast-retirement run did not complete successfully")
            if record.get("request_line") != AST_RETIREMENT_REQUEST_LINE:
                problems.append("receipt ast-retirement request selector is not the exact supported line")
            if record.get("profile") != AST_RETIREMENT_PROFILE:
                problems.append("receipt ast-retirement profile is not the frozen profile")
            if not isinstance(bundle, dict) or set(bundle) != {*AST_RETIREMENT_BUNDLE_FILES, "files"}:
                problems.append("requested issue #4058 raw evidence bundle is incomplete")
            elif not isinstance(bundle.get("acceptance"), dict) or not isinstance(bundle.get("files"), dict):
                problems.append("raw issue #4058 acceptance document or sealed files are missing")
            else:
                acceptance = bundle["acceptance"]
                if record.get("summary") != acceptance:
                    problems.append("receipt and raw ast-retirement acceptance documents differ")
                if acceptance.get("cpu_model") != cpu_model:
                    problems.append("ast-retirement observed CPU differs from the receipt host")
                problems.extend(validate_ast_retirement_acceptance(acceptance, base, head, binaries))
                try:
                    for key, member in AST_RETIREMENT_BUNDLE_FILES.items():
                        if sampling_json(bundle["files"], member) != bundle[key]:
                            raise ValueError(f"ast-retirement document differs from retained bytes: {member}")
                    source = acceptance["source"]
                    if record.get("owner") != bundle["owner"]:
                        raise ValueError("AST retirement receipt differs from its native owner metadata")
                    ast_retirement_owner(bundle, base, head, trusted_revision)
                    if source != bundle["source"]:
                        raise ValueError("ast-retirement source binding differs from its raw source document")
                    observer_build = bundle["rss_build"]
                    from sampling_qualification_receipt import _lab
                    _lab.child_rss_build_validate(observer_build)
                    trusted_source = Path(__file__).with_name("retirement_rss.c").read_bytes()
                    if observer_build["source"]["sha256"] != hashlib.sha256(trusted_source).hexdigest() or observer_build["source"]["size_bytes"] != len(trusted_source):
                        raise ValueError("ast-retirement RSS helper source differs from the trusted publisher implementation")
                    if acceptance.get("rss_observer_build") != observer_build:
                        raise ValueError("ast-retirement observer differs from its retained native build receipt")
                    for name, key in (("source.c", "source"), ("retirement-rss", "binary")):
                        data = bundle["files"].get("ast_retirement_acceptance/rss-helper/" + name)
                        identity = observer_build[key]
                        if not isinstance(data, bytes) or len(data) != identity["size_bytes"] or hashlib.sha256(data).hexdigest() != identity["sha256"]:
                            raise ValueError("ast-retirement native RSS helper/source bytes differ from their identities")
                    for name, identity in (("unity", source["unity"]), *source["corpus"].items()):
                        data = bundle["files"].get(f"ast_retirement_acceptance/inputs/{name}.c")
                        if not isinstance(data, bytes) or len(data) != identity["size_bytes"] or \
                                hashlib.sha256(data).hexdigest() != identity["sha256"]:
                            raise ValueError(f"ast-retirement {name} source identity differs from retained bytes")
                    for cell in AST_RETIREMENT_CELLS:
                        raw = ast_retirement_raw_cell(cell, bundle, source, binaries, cpu_model)
                        if acceptance["cells"].get(cell) != ast_retirement_cell_record(raw):
                            raise ValueError(f"{cell} acceptance metrics differ from its raw paired inference")
                        logs = {key: bundle["files"].get(f"ast_retirement_acceptance/probe/{cell}/{key}.log")
                                for key in ("a", "b")}
                        probe = validate_ast_retirement_probe(cell, bundle[f"{cell}_probe"], binaries, source, logs)
                        for key in ("a", "b"):
                            row, reference = bundle[f"{cell}_probe"][key], bundle[f"{cell}_reference"][key]
                            if AST_RETIREMENT_CELLS[cell]["operation"] == "object":
                                if row.get("output_sha256") != reference["sha256"] or row.get("output_size_bytes") != reference["size_bytes"]:
                                    raise ValueError(f"{cell} {key} retained-byte probe object differs from the timed reference")
                            elif row.get("output_sha256") is not None or row.get("output_size_bytes") is not None:
                                raise ValueError(f"{cell} {key} syntax-only probe unexpectedly produced an object")
                        if acceptance["probes"].get(cell) != probe:
                            raise ValueError(f"{cell} retained component record differs from its raw verbose probe")
                except (RuntimeError, ValueError, UnicodeDecodeError, AttributeError, TypeError, KeyError, ArithmeticError) as error:
                    problems.append(f"raw issue #4058 profile is incomplete or mismatched: {error}")
    return problems


CONSTANT_REUSE_BUNDLE_FILES = {
    "acceptance": "constant_reuse_acceptance/acceptance.json",
    **{f"{cell}_{key}": f"constant_reuse_acceptance/{cell}/{member}" for cell in CONSTANT_REUSE_CELLS
       for key, member in (("summary", "summary.json"), ("meta", "compare.json"), ("a", "a/lab.json"), ("b", "b/lab.json"))},
}


def validate_constant_reuse_bundle(receipt_reuse: object, bundle: object, head: str, candidate_sha: str) -> list[str]:
    """Re-derive the requested #3011 result from its raw cell summaries and configs."""
    if receipt_reuse is None:
        return []
    problems: list[str] = []
    if not isinstance(receipt_reuse, dict) or receipt_reuse.get("requested") is not True:
        return ["receipt constant-reuse record is present but not a request"]
    if receipt_reuse.get("status") != "complete" or receipt_reuse.get("exit") != 0:
        problems.append("receipt issue #3011 constant-reuse run did not complete successfully")
    if receipt_reuse.get("request_line") != CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE:
        problems.append("receipt constant-reuse request selector is not the exact supported line")
    if receipt_reuse.get("profile") != CONSTANT_REUSE_ACCEPTANCE_PROFILE:
        problems.append("receipt constant-reuse profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(CONSTANT_REUSE_BUNDLE_FILES):
        return [*problems, "requested issue #3011 raw evidence bundle is incomplete"]
    acceptance = bundle.get("acceptance")
    if not isinstance(acceptance, dict):
        return [*problems, "raw issue #3011 acceptance document is missing"]
    if receipt_reuse.get("summary") != acceptance:
        problems.append("receipt and raw constant-reuse acceptance documents differ")
    problems.extend(validate_constant_reuse_acceptance(acceptance, head, candidate_sha))
    try:
        cells = acceptance.get("cells") if isinstance(acceptance.get("cells"), dict) else {}
        for cell in CONSTANT_REUSE_CELLS:
            raw = validate_constant_reuse_documents(cell, bundle[f"{cell}_summary"], bundle[f"{cell}_meta"],
                                                    {"a": bundle[f"{cell}_a"], "b": bundle[f"{cell}_b"]}, candidate_sha)
            if cells.get(cell) != constant_reuse_cell_record(raw):
                problems.append(f"constant-reuse {cell} acceptance metrics or identity results do not match its raw summary")
    except (RuntimeError, AttributeError, TypeError, KeyError) as error:
        problems.append(f"raw issue #3011 profile is incomplete or mismatched: {error}")
    return problems


PROCESS_FLOOR_BUNDLE_FILES = {
    "acceptance": "process_floor/acceptance.json",
    **{f"{cell}_{key}": f"process_floor/{cell}/{member}" for cell in PROCESS_FLOOR_CELLS
       for key, member in (("summary", "summary.json"), ("meta", "compare.json"), ("a", "a/lab.json"), ("b", "b/lab.json"))},
}


def validate_process_floor_bundle(receipt_floor: object, bundle: object, head: str, candidate_sha: str) -> list[str]:
    """Re-derive the requested #1125 result from its raw cell summaries and configs."""
    if receipt_floor is None:
        return []
    problems: list[str] = []
    if not isinstance(receipt_floor, dict) or receipt_floor.get("requested") is not True:
        return ["receipt process-floor record is present but not a request"]
    if receipt_floor.get("status") != "complete" or receipt_floor.get("exit") != 0:
        problems.append("receipt issue #1125 process-floor run did not complete successfully")
    if receipt_floor.get("request_line") != PROCESS_FLOOR_REQUEST_LINE:
        problems.append("receipt process-floor request selector is not the exact supported line")
    if receipt_floor.get("profile") != PROCESS_FLOOR_PROFILE:
        problems.append("receipt process-floor profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(PROCESS_FLOOR_BUNDLE_FILES):
        return [*problems, "requested issue #1125 raw evidence bundle is incomplete"]
    acceptance = bundle.get("acceptance")
    if not isinstance(acceptance, dict):
        return [*problems, "raw issue #1125 acceptance document is missing"]
    if receipt_floor.get("summary") != acceptance:
        problems.append("receipt and raw process-floor acceptance documents differ")
    problems.extend(validate_process_floor_acceptance(acceptance, head, candidate_sha))
    try:
        cells = acceptance.get("cells") if isinstance(acceptance.get("cells"), dict) else {}
        for cell in PROCESS_FLOOR_CELLS:
            raw = validate_process_floor_documents(cell, bundle[f"{cell}_summary"], bundle[f"{cell}_meta"],
                                                   {"a": bundle[f"{cell}_a"], "b": bundle[f"{cell}_b"]}, candidate_sha)
            if cells.get(cell) != process_floor_cell_record(raw):
                problems.append(f"process-floor {cell} metrics or identity results do not match its raw summary")
    except (RuntimeError, AttributeError, TypeError, KeyError) as error:
        problems.append(f"raw issue #1125 profile is incomplete or mismatched: {error}")
    return problems


REAL_SOURCE_BUNDLE_FILES = {
    "summary": "throughput_real_source/summary.json",
    "metadata": "throughput_real_source/metadata.json",
    "staging": "throughput_real_source/staging.json",
    "cpuinfo": "throughput_real_source/cpuinfo.txt",
}


def validate_real_source_bundle(record: object, bundle: object, binaries: object) -> list[str]:
    """Re-derive the requested #2761 real-source result from its exported summary, metadata, staging and host."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt real-source throughput record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("exit") != 0:
        problems.append("receipt real-source throughput run did not complete successfully")
    if record.get("request_line") != THROUGHPUT_REAL_SOURCE_REQUEST_LINE:
        problems.append("receipt real-source request selector is not the exact supported line")
    if record.get("profile") != THROUGHPUT_REAL_SOURCE_PROFILE:
        problems.append("receipt real-source profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(REAL_SOURCE_BUNDLE_FILES):
        return [*problems, "requested real-source throughput evidence bundle is incomplete"]
    problems.extend(validate_throughput_real_source(record.get("exit"), bundle["summary"], bundle["metadata"],
                                                    bundle["staging"], bundle["cpuinfo"], binaries))
    if record.get("cells") != throughput_real_source_digest(bundle["summary"]):
        problems.append("receipt real-source cells do not match the exported summary")
    return problems


PRODUCTION_PROFILE_BUNDLE_FILES = {
    "build_summary": "production_profile/summary.txt",
    "ledger": "production_profile/evidence/phases.tsv",
    "summary": "production_profile/throughput/summary.json",
    "metadata": "production_profile/throughput/metadata.json",
}


def validate_production_profile_bundle(record: object, bundle: object, head: str) -> list[str]:
    """Re-derive the requested #2761 PGO/LTO result from its exported build summary, ledger and corpus."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt production-profile record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("build_exit") != 0 or record.get("exit") not in (0, 1):
        problems.append("receipt production-profile run did not complete successfully")
    if record.get("request_line") != PRODUCTION_PROFILE_REQUEST_LINE:
        problems.append("receipt production-profile request selector is not the exact supported line")
    if record.get("profile") != PRODUCTION_PROFILE_PROFILE:
        problems.append("receipt production-profile profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(PRODUCTION_PROFILE_BUNDLE_FILES):
        return [*problems, "requested production-profile evidence bundle is incomplete"]
    problems.extend(validate_production_profile(record.get("build_exit"), bundle["build_summary"], bundle["ledger"], head,
                                                record.get("exit"), bundle["summary"], bundle["metadata"],
                                                record.get("binaries")))
    if record.get("cells") != throughput_real_source_digest(bundle["summary"]):
        problems.append("receipt production-profile cells do not match the exported corpus summary")
    return problems


COMPILER_PROBES_BUNDLE_FILES = {"summary": "compiler_probes/summary.json"}


def validate_compiler_probes_bundle(record: object, bundle: object, base: str, head: str) -> list[str]:
    """Re-derive the requested #2761 probe result from its exported raw samples."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt compiler-probes record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("exit") != 0:
        problems.append("receipt compiler-probes run did not complete successfully")
    if record.get("request_line") != COMPILER_PROBES_REQUEST_LINE:
        problems.append("receipt compiler-probes request selector is not the exact supported line")
    if record.get("profile") != COMPILER_PROBES_PROFILE:
        problems.append("receipt compiler-probes profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(COMPILER_PROBES_BUNDLE_FILES):
        return [*problems, "requested compiler-probes evidence bundle is incomplete"]
    problems.extend(validate_compiler_probes(record.get("exit"), bundle["summary"], base, head, record.get("binaries"),
                                             record.get("probe_source_sha256")))
    if record.get("cells") != compiler_probes_digest(bundle["summary"]):
        problems.append("receipt compiler-probes cells do not match the exported raw samples")
    return problems


FRONTEND_SELECTOR_BUNDLE_FILES = {
    "provenance": "frontend_selector/frontend_scaling/provenance.json",
    "results": "frontend_selector/frontend_scaling/results.json",
    "report": "frontend_selector/selection/report.json",
    "bench": "frontend_selector/ide_bench/ide-bench.json",
}


def validate_frontend_selector_bundle(record: object, bundle: object, binaries: object) -> list[str]:
    """Re-derive the requested #2761 frontend and selector probes from their exported records."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt frontend-selector record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("exits") != {"frontend_scaling": 0, "selection": 0}:
        problems.append("receipt frontend-selector run did not complete successfully")
    if record.get("request_line") != FRONTEND_SELECTOR_REQUEST_LINE:
        problems.append("receipt frontend-selector request selector is not the exact supported line")
    if record.get("profile") != FRONTEND_SELECTOR_PROFILE:
        problems.append("receipt frontend-selector profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(FRONTEND_SELECTOR_BUNDLE_FILES):
        return [*problems, "requested frontend-selector evidence bundle is incomplete"]
    exits = record.get("exits") if isinstance(record.get("exits"), dict) else {}
    problems.extend(validate_frontend_selector(exits.get("frontend_scaling"), bundle["provenance"], bundle["results"],
                                               exits.get("selection"), bundle["report"], bundle["bench"], binaries))
    if record.get("cells") != frontend_selector_digest(bundle["results"], bundle["report"], bundle["bench"]):
        problems.append("receipt frontend-selector cells do not match the exported evidence")
    return problems


FRONTEND_NESTING_BUNDLE_FILES = {
    "provenance": "frontend_nesting/provenance.json",
    "results": "frontend_nesting/results.json",
}


def validate_frontend_nesting_bundle(record: object, bundle: object, binaries: object) -> list[str]:
    """Re-derive the requested #2676 deep-nesting families from their exported records."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt frontend-nesting record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("exit") != 0:
        problems.append("receipt frontend-nesting run did not complete successfully")
    if record.get("request_line") != FRONTEND_NESTING_REQUEST_LINE:
        problems.append("receipt frontend-nesting request selector is not the exact supported line")
    if record.get("profile") != FRONTEND_NESTING_PROFILE:
        problems.append("receipt frontend-nesting profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(FRONTEND_NESTING_BUNDLE_FILES):
        return [*problems, "requested frontend-nesting evidence bundle is incomplete"]
    problems.extend(validate_frontend_nesting(record.get("exit"), bundle["provenance"], bundle["results"], binaries))
    if record.get("cells") != frontend_scaling_digest(bundle["results"]):
        problems.append("receipt frontend-nesting cells do not match the exported evidence")
    return problems


WILD_LINKER_BUNDLE_FILES = {key: f"wild_linker/{name}" for key, name in wild_linker_bundle_files().items()}


def validate_wild_linker_bundle(record: object, bundle: object, head: str) -> list[str]:
    """Re-derive the requested #2761 Wild linker leg from its exported samples, manifests and linker identities."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt wild-linker record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("build_exit") != 0 or record.get("exit") != 0:
        problems.append("receipt wild-linker run did not complete successfully")
    if record.get("request_line") != WILD_LINKER_REQUEST_LINE:
        problems.append("receipt wild-linker request selector is not the exact supported line")
    if record.get("profile") != WILD_LINKER_PROFILE:
        problems.append("receipt wild-linker profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(WILD_LINKER_BUNDLE_FILES):
        return [*problems, "requested wild-linker evidence bundle is incomplete"]
    identity = record.get("identity") if isinstance(record.get("identity"), dict) else {}
    if record.get("linkers") != identity.get("linkers"):
        problems.append("receipt wild-linker linkers differ from its identity record")
    problems.extend(validate_wild_linker(record.get("exit"), bundle, head, identity))
    if record.get("cells") != wild_linker_digest(bundle["raw"], bundle["summary"]):
        problems.append("receipt wild-linker cells do not match the exported samples")
    return problems


LINK_TLS_BUNDLE_FILES = {"summary": "link_tls/summary.json"}


def validate_link_tls_bundle(record: object, bundle: object, binaries: object) -> list[str]:
    """Re-derive the requested #1915 TLS companion link result from its exported raw samples."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt link-tls record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("exit") != 0:
        problems.append("receipt link-tls run did not complete successfully")
    if record.get("request_line") != LINK_TLS_REQUEST_LINE:
        problems.append("receipt link-tls request selector is not the exact supported line")
    if record.get("profile") != LINK_TLS_PROFILE:
        problems.append("receipt link-tls profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(LINK_TLS_BUNDLE_FILES):
        return [*problems, "requested link-tls evidence bundle is incomplete"]
    problems.extend(validate_link_tls(record.get("exit"), bundle["summary"], binaries))
    if record.get("cells") != link_tls_digest(bundle["summary"]):
        problems.append("receipt link-tls cells do not match the exported raw samples")
    return problems


SELFHOST_INLINE_BUNDLE_FILES = {name: f"selfhost_inline/{name}" for name in (
    "stage1-baseline/stage1.json", "stage1-candidate/stage1.json", "compare/result.json", "compare/lab/summary.json",
    "compare/lab/compare.json", "compare/lab/a/lab.json", "compare/lab/b/lab.json")}


def validate_selfhost_inline_bundle(record: object, bundle: object, binaries: object) -> list[str]:
    """Re-validate the requested #58 self-built inline-mode result from its exported raw documents."""
    if record is None:
        return []
    if not isinstance(record, dict) or record.get("requested") is not True:
        return ["receipt self-host inline record is present but not a request"]
    problems: list[str] = []
    if record.get("status") != "complete" or record.get("exit") != 0:
        problems.append("receipt self-host inline run did not complete successfully")
    if record.get("request_line") != SELFHOST_INLINE_REQUEST_LINE:
        problems.append("receipt self-host inline request selector is not the exact supported line")
    if record.get("profile") != SELFHOST_INLINE_PROFILE:
        problems.append("receipt self-host inline profile is not the frozen profile")
    if not isinstance(bundle, dict) or set(bundle) != set(SELFHOST_INLINE_BUNDLE_FILES):
        return [*problems, "requested self-host inline evidence bundle is incomplete"]
    stage1 = {role: bundle[f"stage1-{role}/stage1.json"] for role in ("baseline", "candidate")}
    problems.extend(validate_selfhost_inline(stage1, bundle["compare/result.json"], binaries))
    expected = {role: (stage1[role].get("stage1") or {}).get("sha256") if isinstance(stage1[role], dict) else None
                for role in stage1}
    problems.extend(validate_selfhost_inline_documents(
        bundle["compare/lab/summary.json"], bundle["compare/lab/compare.json"],
        {"a": bundle["compare/lab/a/lab.json"], "b": bundle["compare/lab/b/lab.json"]}, expected))
    if record.get("digest") != selfhost_inline_digest(bundle["compare/lab/summary.json"]):
        problems.append("receipt self-host inline digest does not match the exported summary")
    return problems


def decide(expected: dict, authorized: bool, compare_result: str, receipt: object, summary: object,
           policy_value: str, throughput: object = None, require_throughput: bool = True,
           extra_reasons: list[str] | None = None, *,
           expected_phase_schema: str | None = None,
           expected_profile: str | None = None, expected_preparation_policy: str | None = None,
           require_owned_phases: bool = False, require_owned_preflight: bool = False,
           expected_phase_driver_sha256: str | None = None,
           expected_measurement_revision: str | None = None,
           expected_throughput_profile: dict | None = None,
           dispatch_refusal: str = "", evidence_bundle: dict | None = None) -> tuple[str, str, list[str]]:
    """(conclusion, title, reasons) for one attempt; never consults the verdict's direction.

    expected_throughput_profile is THROUGHPUT_PROFILE unless hosted pull authorization admitted
    the named historical recipe (#3714); a receipt can never select it by itself. A non-empty
    dispatch_refusal is that authorization's explicit unsupported disposition: no host ran.

    throughput is {"summary": ..., "metadata": ..., "scaling": {series: {"summary", "metadata"}}}
    from the evidence artifact. Scaling is checked whenever the receipt names
    its profile; only a pull request that asked for it has one.
    evidence_bundle is verify_evidence_bundle's account of the artifact's sealed raw evidence
    (#2918); None (publication-only recovery, the owned routes) leaves it unchecked.
    A receipt from before the corpus leg (#2761) has no throughput_profile;
    only publication-only recovery of such a past attempt passes
    require_throughput=False, and a receipt that names the profile is always
    checked against it. Utility alone explicitly supplies its distinct trusted
    expected_phase_schema; a receipt cannot opt into that route by selfclaim.
    """
    reasons: list[str] = list(extra_reasons or [])
    from compiler_receipt import named_main_profile
    expected_data = PROFILE
    if expected_profile is not None:
        try:
            expected_data = named_main_profile(expected_profile)
        except ValueError as error:
            reasons.append(str(error))
    if expected_phase_schema == "buster-compiler-main-owned-phases-v1" and (
            expected.get("mode") != "main" or expected_profile is None or
            expected_preparation_policy not in ("legacy-rebuild", "snapshot-v1") or
            require_owned_phases is not True or require_owned_preflight is not True):
        reasons.append("Main-owned publication requires trusted main profile, preparation policy and owned preflight")
    conclusion, title = "failure", "Not benchmarked"
    policy, problem = regression_policy(policy_value)
    if dispatch_refusal:
        title = "Not benchmarked: unsupported historical harness"
        reasons.append(dispatch_refusal)
    elif not authorized:
        reasons.append("authorization refused or did not complete in this attempt; this candidate has a "
                       "benchmark coverage gap (see the authorize-compiler job)")
    elif not isinstance(receipt, dict):
        reasons.append("no readable receipt.json in this attempt's evidence artifact "
                       f"(compare job result: {compare_result or 'unknown'})")
    else:
        selected_profile = next((profile for profile in ANALYZER_PROFILE_BY_LINE.values()
                                 if receipt.get("profile") == profile), None)
        analyzer_profile = selected_profile is not None
        if receipt.get("schema") != RECEIPT_SCHEMA:
            reasons.append(f"receipt schema {receipt.get('schema')!r} is not {RECEIPT_SCHEMA}")
        identity = receipt.get("identity") if isinstance(receipt.get("identity"), dict) else {}
        for key in IDENTITY_KEYS:
            if identity.get(key) != expected.get(key):
                reasons.append(f"receipt {key} {identity.get(key)!r} does not match {expected.get(key)!r}")
        if receipt.get("mode") != expected.get("mode"):
            reasons.append(f"receipt mode {receipt.get('mode')!r} is not {expected.get('mode')!r}")
        # Receipts before range baselines (#2752) carry no coverage.
        coverage = {"first_parent": expected.get("first_parent"), "range": expected.get("range")}
        if "coverage" in receipt and receipt.get("coverage") != coverage:
            reasons.append(f"receipt coverage {receipt.get('coverage')!r} does not match {coverage!r}")
        if analyzer_profile:
            if expected.get("mode") != "pull" or receipt.get("mode") != "pull":
                reasons.append(f"{selected_profile['name']} is valid only in pull mode")
            if receipt.get("analyzer_profile") != selected_profile:
                reasons.append("receipt analyzer profile marker is not frozen")
        elif receipt.get("profile") != expected_data:
            reasons.append("receipt profile is not the frozen comparison profile")
        if host_problem(receipt):
            reasons.append(host_problem(receipt))
        state = receipt.get("state")
        if not reasons and state == "superseded":
            conclusion, title = "neutral", "Superseded before measurement"
            reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
        elif not reasons and state == "measured":
            if analyzer_profile:
                analyzer_bundle = throughput.get("analyzer") if isinstance(throughput, dict) else None
                if receipt.get("throughput_profile") is not None or "scaling_profile" in receipt:
                    reasons.append(f"{selected_profile['name']} cannot be combined with compiler throughput or scaling")
                inline = receipt.get("inline_acceptance")
                if isinstance(inline, dict) and inline.get("requested") is True:
                    reasons.append(f"{selected_profile['name']} cannot be combined with issue #48 self-host acceptance")
                if receipt.get("ast_pilot_acceptance") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with issue #3397 ast-pilot acceptance")
                if receipt.get("ast_retirement_acceptance") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with issue #4058 ast-retirement acceptance")
                if receipt.get("constant_reuse_acceptance") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with issue #3011 constant-reuse acceptance")
                if receipt.get("process_floor") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the issue #1125 process-floor profile")
                if receipt.get("throughput_real_source") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with real-source throughput")
                if receipt.get("production_profile") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the PGO/LTO production profile")
                if receipt.get("compiler_probes") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the compiler probes")
                if receipt.get("frontend_selector") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the frontend-selector probes")
                if receipt.get("wild_linker") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the wild-linker leg")
                if receipt.get("link_tls") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the link-tls leg")
                if receipt.get("selfhost_inline") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the self-host inline leg")
                if receipt.get("frontend_nesting") is not None:
                    reasons.append(f"{selected_profile['name']} cannot be combined with the frontend-nesting leg")
                reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
                reasons.extend(validate_analyzer_bundle(receipt,
                                                        analyzer_bundle.get("summary") if isinstance(analyzer_bundle, dict) else None,
                                                        analyzer_bundle))
            else:
                if evidence_bundle is not None:
                    reasons.extend(evidence_bundle_problems(receipt, evidence_bundle))
                reasons.extend(classify(summary, receipt.get("binaries"), expected_profile=expected_profile))
                reasons.extend(validate_closure(receipt, throughput.get("closure") if isinstance(throughput, dict) else None,
                                                expected_phase_schema=expected_phase_schema, expected_profile=expected_profile,
                                                expected_policy=expected_preparation_policy,
                                                require_owned_phases=require_owned_phases,
                                                require_owned_preflight=require_owned_preflight,
                                                expected_phase_driver_sha256=expected_phase_driver_sha256,
                                                expected_trusted_revision=expected_measurement_revision))
                if require_throughput or "throughput_profile" in receipt:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    wanted = expected_throughput_profile or THROUGHPUT_PROFILE
                    if receipt.get("throughput_profile") != wanted:
                        reasons.append("receipt throughput profile is not the frozen corpus profile")
                    if wanted == THROUGHPUT_HISTORICAL_PROFILE:
                        revision = THROUGHPUT_HISTORICAL_HARNESS_REVISION
                        if expected.get("mode") != "pull":
                            reasons.append(f"{wanted['name']} is valid only in pull mode")
                        if receipt.get("throughput_historical") != {"harness_revision": revision,
                                                                    "observed_revision": revision,
                                                                    "status": "prepared"}:
                            reasons.append(f"{wanted['name']} harness was not the prepared pinned revision {revision}")
                    elif "throughput_historical" in receipt:
                        reasons.append("receipt carries a historical corpus harness the route did not admit")
                    reasons.extend(classify_throughput(corpus.get("summary"), corpus.get("metadata"),
                                                       receipt.get("binaries")))
                if "scaling_profile" in receipt:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if receipt.get("scaling_profile") != SCALING_PROFILE:
                        reasons.append("receipt scaling profile is not the frozen scaling profile")
                    reasons.extend(classify_scaling(corpus.get("scaling"), receipt.get("binaries")))
                inline = receipt.get("inline_acceptance")
                if isinstance(inline, dict) and inline.get("requested") is True:
                    if expected.get("mode") != "pull":
                        reasons.append("issue #48 self-host comparison is valid only in pull mode")
                    candidate = (receipt.get("binaries") or {}).get("candidate") or {}
                    reasons.extend(validate_inline_bundle(inline, corpus.get("inline_bundle"),
                                                          expected.get("head", ""), candidate.get("sha256", "")))
                if receipt.get("ast_pilot_acceptance") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("issue #3397 ast-pilot comparison is valid only in pull mode")
                    candidate = (receipt.get("binaries") or {}).get("candidate") or {}
                    reasons.extend(validate_ast_pilot_bundle(receipt["ast_pilot_acceptance"], corpus.get("ast_pilot_bundle"),
                                                             expected.get("head", ""), candidate.get("sha256", "")))
                if receipt.get("ast_retirement_acceptance") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("issue #4058 ast-retirement comparison is valid only in pull mode")
                    if any(receipt.get(key) is not None for key in
                           ("ast_pilot_acceptance", "constant_reuse_acceptance", "process_floor", "throughput_real_source",
                            "production_profile", "compiler_probes", "frontend_selector", "frontend_nesting",
                            "wild_linker", "link_tls", "selfhost_inline")) or "scaling_profile" in receipt:
                        reasons.append("issue #4058 ast-retirement acceptance cannot be combined with another opt-in leg")
                    if not isinstance(evidence_bundle, dict) or evidence_bundle.get("state") != "verified":
                        reasons.append("issue #4058 ast-retirement acceptance requires verified sealed raw evidence")
                    host = receipt.get("host") if isinstance(receipt.get("host"), dict) else {}
                    reasons.extend(validate_ast_retirement_bundle(receipt["ast_retirement_acceptance"],
                        corpus.get("ast_retirement_bundle"), expected.get("base", ""), expected.get("head", ""),
                        receipt.get("binaries"), host.get("cpu_model", ""),
                        expected_measurement_revision or receipt.get("identity", {}).get("trusted_revision")))
                if receipt.get("constant_reuse_acceptance") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("issue #3011 constant-reuse comparison is valid only in pull mode")
                    # The inline profile rides every pull compare (main's request history holds its line), so it
                    # is not a competing opt-in leg here.
                    others = [key for key in ("ast_pilot_acceptance", "throughput_real_source", "production_profile",
                                              "compiler_probes", "frontend_selector", "wild_linker", "link_tls",
                                              "selfhost_inline")
                              if receipt.get(key) is not None]
                    if others:
                        reasons.append("issue #3011 constant-reuse acceptance cannot be combined with another opt-in leg")
                    candidate = (receipt.get("binaries") or {}).get("candidate") or {}
                    reasons.extend(validate_constant_reuse_bundle(receipt["constant_reuse_acceptance"],
                                                                  corpus.get("constant_reuse_bundle"),
                                                                  expected.get("head", ""), candidate.get("sha256", "")))
                if receipt.get("process_floor") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the issue #1125 process-floor profile is valid only in pull mode")
                    # The inline profile rides every pull compare (main's request history holds its line), so it
                    # is not a competing opt-in leg here.
                    if any(receipt.get(key) is not None for key in
                           ("ast_pilot_acceptance", "constant_reuse_acceptance", "throughput_real_source",
                            "production_profile", "compiler_probes", "frontend_selector", "wild_linker", "link_tls",
                            "selfhost_inline", "frontend_nesting")):
                        reasons.append("the issue #1125 process-floor profile cannot be combined with another opt-in leg")
                    candidate = (receipt.get("binaries") or {}).get("candidate") or {}
                    reasons.extend(validate_process_floor_bundle(receipt["process_floor"], corpus.get("process_floor_bundle"),
                                                                 expected.get("head", ""), candidate.get("sha256", "")))
                if receipt.get("throughput_real_source") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("real-source throughput is valid only in pull mode")
                    reasons.extend(validate_real_source_bundle(receipt["throughput_real_source"],
                                                               corpus.get("real_source_bundle"), receipt.get("binaries")))
                if receipt.get("production_profile") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the PGO/LTO production profile is valid only in pull mode")
                    if receipt.get("throughput_real_source") is not None or receipt.get("ast_pilot_acceptance") is not None:
                        reasons.append("the PGO/LTO production profile cannot be combined with real-source "
                                       "throughput or ast-pilot acceptance")
                    reasons.extend(validate_production_profile_bundle(receipt["production_profile"],
                                                                      corpus.get("production_profile_bundle"),
                                                                      expected.get("head", "")))
                if receipt.get("compiler_probes") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the compiler probes are valid only in pull mode")
                    if any(receipt.get(key) is not None for key in
                           ("throughput_real_source", "ast_pilot_acceptance", "production_profile")):
                        reasons.append("the compiler probes cannot be combined with real-source throughput, "
                                       "ast-pilot acceptance or the PGO/LTO production profile")
                    reasons.extend(validate_compiler_probes_bundle(receipt["compiler_probes"],
                                                                   corpus.get("compiler_probes_bundle"),
                                                                   expected.get("base", ""), expected.get("head", "")))
                if receipt.get("frontend_selector") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the frontend-selector probes are valid only in pull mode")
                    if any(receipt.get(key) is not None for key in
                           ("throughput_real_source", "ast_pilot_acceptance", "production_profile", "compiler_probes")):
                        reasons.append("the frontend-selector probes cannot be combined with real-source throughput, "
                                       "ast-pilot acceptance, the PGO/LTO production profile or the compiler probes")
                    reasons.extend(validate_frontend_selector_bundle(receipt["frontend_selector"],
                                                                     corpus.get("frontend_selector_bundle"),
                                                                     receipt.get("binaries")))
                if receipt.get("wild_linker") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the wild-linker leg is valid only in pull mode")
                    if any(receipt.get(key) is not None for key in
                           ("throughput_real_source", "ast_pilot_acceptance", "production_profile", "compiler_probes",
                            "frontend_selector")):
                        reasons.append("the wild-linker leg cannot be combined with real-source throughput, ast-pilot "
                                       "acceptance, the PGO/LTO production profile, the compiler probes or the "
                                       "frontend-selector probes")
                    reasons.extend(validate_wild_linker_bundle(receipt["wild_linker"], corpus.get("wild_linker_bundle"),
                                                               expected.get("head", "")))
                if receipt.get("link_tls") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the link-tls leg is valid only in pull mode")
                    if any(receipt.get(key) is not None for key in
                           ("throughput_real_source", "ast_pilot_acceptance", "production_profile", "compiler_probes",
                            "frontend_selector", "wild_linker")):
                        reasons.append("the link-tls leg cannot be combined with real-source throughput, ast-pilot "
                                       "acceptance, the PGO/LTO production profile, the compiler probes, the "
                                       "frontend-selector probes or the wild-linker leg")
                    reasons.extend(validate_link_tls_bundle(receipt["link_tls"], corpus.get("link_tls_bundle"),
                                                            receipt.get("binaries")))
                if receipt.get("selfhost_inline") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the self-host inline leg is valid only in pull mode")
                    # The #48 inline leg is selected by presence, not freshness, so it runs beside this one.
                    if any(receipt.get(key) is not None for key in
                           ("throughput_real_source", "ast_pilot_acceptance", "production_profile", "compiler_probes",
                            "frontend_selector", "wild_linker", "link_tls", "constant_reuse_acceptance")) or \
                            "scaling_profile" in receipt:
                        reasons.append("the self-host inline leg cannot be combined with another opt-in leg, the "
                                       "scaling leg included")
                    reasons.extend(validate_selfhost_inline_bundle(receipt["selfhost_inline"],
                                                                   corpus.get("selfhost_inline_bundle"),
                                                                   receipt.get("binaries")))
                if receipt.get("frontend_nesting") is not None:
                    corpus = throughput if isinstance(throughput, dict) else {}
                    if expected.get("mode") != "pull":
                        reasons.append("the frontend-nesting leg is valid only in pull mode")
                    if any(receipt.get(key) is not None for key in
                           ("throughput_real_source", "ast_pilot_acceptance", "production_profile", "compiler_probes",
                            "frontend_selector", "wild_linker", "link_tls", "selfhost_inline",
                            "constant_reuse_acceptance")):
                        reasons.append("the frontend-nesting leg cannot be combined with another opt-in leg")
                    reasons.extend(validate_frontend_nesting_bundle(receipt["frontend_nesting"],
                                                                    corpus.get("frontend_nesting_bundle"),
                                                                    receipt.get("binaries")))
            if compare_result != "success":
                reasons.append(f"compare job result is {compare_result!r}, not success")
            if problem:
                reasons.append(problem)
            if not reasons:
                conclusion = "success"
                if analyzer_profile:
                    inventory = (summary.get("inventory") or {}) if isinstance(summary, dict) else {}
                    title = f"Measured ({policy}): full analyzer inventory, {inventory.get('selected_rows', 'NA')} rows"
                else:
                    verdict = summary.get("verdict", {})
                    title = f"Measured ({policy}): wall B/A {verdict.get('ratio'):.4f}, {verdict.get('outcome')}"
        elif not reasons:
            reasons.append(f"host measurement state is {state!r}")
            reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
    return conclusion, title, reasons


# The trusted corpus replayer (#2918): this checkout's own `bench_throughput compare`, run from its root.
TRUSTED_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPUS_REPLAY_COMMAND = (os.path.join(TRUSTED_ROOT, "build.sh"), "bench_throughput", "compare")
CORPUS_REPLAY_TIMEOUT_SECONDS = 600
CORPUS_REPLAYED = ("summary.json", "summary.md")


def sealed_lab_replay(files: dict[str, bytes]) -> int:
    """Replay a verified bundle's compiler-compare-v1 lab from its raw pairs; the pair count (#2918).

    The raw plan must be the frozen profile's adaptive ABBA plan, every saved variant fact must
    follow from the raw records and the trusted uarch_lab statistics must reproduce the saved
    inference (lab_raw_inference). The cross-variant output and code-size facts read the
    unretained reference images, so they are not replayed. ValueError otherwise.
    """
    from sampling_qualification_receipt import _lab
    raw, summary = sampling_json(files, "lab/compare.json"), sampling_json(files, "lab/summary.json")
    records = sampling_json(files, "lab/pairs.json", False)
    config, saved = raw.get("config"), raw.get("plan")
    if raw.get("version") != 1 or raw.get("mode") != "compare" or not isinstance(config, dict) or \
            any(config.get(key) != PROFILE[key] for key in ("cpu", "target_minutes", "warmups", "profile_steps")) or \
            config.get("pairs") is not None or type(config.get("seed")) is not int or \
            type(config.get("min_effect_percent")) not in (int, float) or \
            any(config.get(key) for key in ("ast_pilot_pair", "ast_retirement_cell", "constant_reuse_pair", "process_floor_pair",
                                            "canonical_inline_pair", "sudo")):
        raise ValueError("lab raw config is not the frozen compiler-compare-v1 profile")
    count = saved.get("pairs") if isinstance(saved, dict) else None
    if type(count) is not int or not 10 <= count <= 1000 or count % 2 or saved.get("order") != "ABBA" or \
            not isinstance(records, list) or len(records) != count * 2:
        raise ValueError("lab adaptive plan or complete paired population is missing")
    expected_plan = dict(saved, seed=config["seed"], confidence=_lab.CONFIDENCE,
                         bootstrap_resamples=_lab.BOOTSTRAP_RESAMPLES, complete_pairs=count,
                         fresh_copy=bool(config.get("fresh_copy")))
    if summary.get("plan") != expected_plan or summary.get("method") != _lab.COMPARE_METHOD or \
            summary.get("command") != config.get("command") or summary.get("cpu") != config["cpu"]:
        raise ValueError("lab saved plan or method differs from its raw configuration")
    variant_meta = {}
    for key, role in _lab.VARIANTS:
        variant, observed = (raw.get("variants") or {}).get(key), summary.get(role)
        meta = sampling_json(files, f"lab/{key}/lab.json")
        if not isinstance(variant, dict) or not isinstance(observed, dict) or \
                any(observed.get(field) != value for field, value in
                    (("path", variant.get("ide")), ("sha256", variant.get("sha256")),
                     ("size_bytes", variant.get("size_bytes")), ("runs", count), ("failed", 0),
                     ("identical_runs", count), ("deterministic", True))):
            raise ValueError(f"lab saved {role} population does not follow from its raw records")
        perf, collection = (meta.get("capabilities") or {}).get("perf_stat"), meta.get("collection")
        if not isinstance(perf, dict) or type(perf.get("usable")) is not bool or not isinstance(perf.get("reason"), str) or \
                not isinstance(collection, dict) or type(collection.get("metrics_out")) is not bool:
            raise ValueError(f"lab variant {key} counter or metrics availability was not observed")
        variant_meta[key] = meta
    lab_raw_inference(files, "lab/", records, variant_meta, summary, raw, config["seed"], config["min_effect_percent"])
    return count


def sealed_corpus_replay(files: dict[str, bytes], temporary: str) -> int:
    """Replay a verified bundle's corpus with the trusted native comparison; the case count (#2918).

    `bench_throughput compare` checks the completion hashes and population and recomputes the
    statistics in a disposable copy, since it rewrites its summaries; the regenerated summaries
    must equal the retained bytes. A valid regression stays report-only. ValueError otherwise.
    """
    root = os.path.join(temporary, "corpus-replay")
    for name, data in files.items():
        if name.startswith("throughput/"):
            target = os.path.join(root, *name[len("throughput/"):].split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "xb") as writer:
                writer.write(data)
    try:
        completed = subprocess.run([*CORPUS_REPLAY_COMMAND, "--output", root], capture_output=True, text=True,
                                   timeout=CORPUS_REPLAY_TIMEOUT_SECONDS, check=False,
                                   cwd=TRUSTED_ROOT)
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"corpus replay did not run: {error.__class__.__name__}: {error}") from error
    for name in CORPUS_REPLAYED:
        retained = files.get("throughput/" + name)
        if name == "summary.json" or retained is not None:
            try:
                with open(os.path.join(root, name), "rb") as reader:
                    replayed = reader.read()
            except OSError:
                replayed = None
            if replayed != retained:
                tail = ((completed.stderr or "") + (completed.stdout or "")).strip()[-300:]
                raise ValueError(f"corpus {name} does not replay (bench_throughput compare exited "
                                 f"{completed.returncode}{': ' + tail if tail else ''})")
    comparisons = sampling_json(files, "throughput/summary.json").get("comparisons")
    if not isinstance(comparisons, list):
        raise ValueError("corpus summary has no comparisons")
    return len(comparisons)


def verify_evidence_bundle(throughput: dict, expected: dict, corpus_profile: dict, request_attempt: str,
                           receipt: dict | None = None, required: bool = True) -> dict:
    """The artifact's sealed raw evidence, natively verified under this publisher's own identity (#2918).

    {"state": "legacy-unsealed"} when read_evidence found neither control file and the route does not
    require v1 (sealing_required), {"state": "unsealed"} when it does; {"state": "verified",
    "members", "payload_bytes", "manifest_sha256", "replay"} when `compiler_evidence verify` reproduces
    the sealed manifest from the extracted bytes and the lab and corpus replay from them
    (sealed_lab_replay, sealed_corpus_replay); otherwise {"state": "refused", "reason"}. The identity
    comes from the authorized expected identity and admitted profiles, never from the receipt; only a
    snapshot-v1 receipt's route lines (its declared policy and owned-phase count) come from it, and
    validate_closure binds that policy to its closure records, so a receipt cannot select the weaker
    ordinary inventory for a snapshot attempt (#3211). The snapshot inventory carries every ordinary
    member, so the same replay applies.
    """
    import tempfile
    result: dict = {"state": "unsealed" if required else "legacy-unsealed"}
    if "sealed_files" in throughput:
        files = throughput.get("sealed_files")
        route = {key: receipt[key] for key in ("preparation_policy", "closure", "phase_ownership")
                 if isinstance(receipt, dict) and key in receipt}
        identity = evidence_identity({"identity": expected, "profile": PROFILE, "throughput_profile": corpus_profile, **route},
                                     request_attempt)
        result = {"state": "refused", "reason": throughput.get("sealed_problem") or "sealed evidence archive is unreadable"}
        if isinstance(files, dict) and not identity:
            result["reason"] = "the authorized identity cannot name a sealed bundle"
        elif isinstance(files, dict):
            try:
                with tempfile.TemporaryDirectory(prefix="compiler-evidence-") as temporary:
                    bundle = os.path.join(temporary, "bundle")
                    os.mkdir(bundle)
                    for name, data in files.items():
                        target = os.path.join(bundle, *name.split("/"))
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        with os.fdopen(os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                                               0o600), "wb") as writer:
                            writer.write(data)
                    with open(os.path.join(temporary, "identity.tsv"), "w", encoding="utf-8") as writer:
                        writer.write(identity)
                    completed = subprocess.run([*EVIDENCE_VERIFY_COMMAND, "verify", bundle,
                                                os.path.join(temporary, "identity.tsv")],
                                               capture_output=True, text=True, timeout=EVIDENCE_VERIFY_TIMEOUT_SECONDS,
                                               check=False)
                    lines = (completed.stdout or "").strip().splitlines()
                    line = lines[-1] if lines else (completed.stderr or "").strip()[-400:]
                    fields = dict(item.split("=", 1) for item in line.split()[1:] if "=" in item)
                    if completed.returncode == 0 and line.startswith("COMPILER_EVIDENCE_VERIFIED ") and \
                            fields.get("members", "").isdigit() and fields.get("payload_bytes", "").isdigit() and \
                            re.fullmatch("[0-9a-f]{64}", fields.get("manifest_sha256", "")):
                        try:
                            replay = {"lab_pairs": sealed_lab_replay(files),
                                      "corpus_cases": sealed_corpus_replay(files, temporary)}
                            result = {"state": "verified", "inventory": evidence_inventory(route),
                                      "members": int(fields["members"]),
                                      "payload_bytes": int(fields["payload_bytes"]),
                                      "manifest_sha256": fields["manifest_sha256"], "replay": replay}
                        except (ValueError, LookupError, TypeError, AttributeError, ArithmeticError) as error:
                            result["reason"] = f"sealed raw evidence does not replay: {error}"
                    else:
                        result["reason"] = f"native verify exited {completed.returncode}: {line or 'no output'}"
            except (OSError, ValueError, subprocess.SubprocessError, UnicodeError) as error:
                result["reason"] = f"sealed evidence could not be verified: {error.__class__.__name__}: {error}"
    return result


def evidence_bundle_problems(receipt: dict, bundle: dict) -> list[str]:
    """Reasons a measured receipt's raw-evidence claim disagrees with the verified artifact (#2918)."""
    claimed = receipt.get("evidence_bundle")
    problems: list[str] = []
    if bundle.get("state") == "legacy-unsealed":
        if claimed is not None:
            problems.append("receipt names sealed raw evidence that the artifact does not carry")
    elif bundle.get("state") == "unsealed":
        problems.append("sealed raw evidence v1 is required on this route, but the artifact carries neither "
                        "control file")
    elif bundle.get("state") != "verified":
        problems.append(f"sealed raw evidence refused: {bundle.get('reason', 'unknown')}")
    elif not isinstance(claimed, dict) or claimed.get("state") != "sealed" or claimed.get("inventory") != bundle.get("inventory") or \
            any(claimed.get(key) != bundle.get(key) for key in ("manifest_sha256", "members", "payload_bytes")):
        problems.append("receipt evidence_bundle does not name the verified sealed manifest")
    return problems


def evidence_bundle_note(bundle: dict) -> str:
    """One report line for the artifact's raw-evidence state; refusals are reasons instead."""
    note = ""
    if bundle.get("state") == "legacy-unsealed":
        note = ("raw evidence: legacy-unsealed (no v1 manifest or seal, #2918); the outcome stands but its "
                "retained raw bytes are unverified")
    elif bundle.get("state") == "verified":
        replay = bundle.get("replay") or {}
        note = (f"raw evidence: sealed {bundle['inventory']} verified natively, {bundle['members']} members, "
                f"{bundle['payload_bytes']} bytes, manifest {bundle['manifest_sha256']}; replayed "
                f"{replay.get('lab_pairs', 'NA')} lab pairs and {replay.get('corpus_cases', 'NA')} corpus cases")
    return note


class DuplicateKey(ValueError):
    """A JSON object repeated a key; the evidence is rejected, not resolved to a winner."""


def unique_object(pairs: list) -> dict:
    result: dict = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKey(key)
        result[key] = value
    return result


def member_identity(name: str) -> str:
    """Archive member name after the aliasing a zip consumer may apply (slashes, './', repeats, trailing '/')."""
    parts = [part for part in name.replace("\\", "/").split("/") if part not in ("", ".")]
    return "/".join(parts)



MAIN_PHASE_SCHEMA = "buster-compiler-main-owned-phases-v1"
MAIN_ARCHIVE_FILE_LIMIT = 16384
MAIN_ROUTE_FIELDS = ("main_profile", "main_preparation_policy", "main_phase_schema", "main_measurement_revision",
                     "main_policy_revision", "main_certificate_revision", "main_certificate_sha256",
                     "lab_sha256", "python_path", "python_sha256", "driver_sha256", "compare_sha256",
                     "receipt_sha256", "owned_phase_sha256", "owned_plan_sha256",
                     "trusted_root", "candidate_root", "work_root", "evidence_root")


def main_runtime_pins(api: Api, route: dict) -> None:
    """The loaded reader must be the frozen H reader, never a later P reinterpretation."""
    from sampling_qualification_receipt import _lab
    root = os.path.dirname(os.path.abspath(__file__))
    sources = (("tools/uarch_lab.py", route["lab_sha256"], _lab.__file__),
               ("tools/bench_direct/compiler_compare.py", route["compare_sha256"], root + "/compiler_compare.py"),
               ("tools/bench_direct/compiler_receipt.py", route["receipt_sha256"], root + "/compiler_receipt.py"),
               ("tools/bench_direct/compiler_owned_phase.py", route["owned_phase_sha256"], root + "/compiler_owned_phase.py"),
               ("tools/bench_direct/compiler_owned_plan.py", route["owned_plan_sha256"], root + "/compiler_owned_plan.py"),
               ("tools/bench_direct/compiler_publish.py", None, __file__))
    source_bytes = {}
    for path, pin, local_path in sources:
        value = api.request("/contents/" + path + "?" + urllib.parse.urlencode({"ref": route["main_measurement_revision"]}))
        if not isinstance(value, dict) or value.get("type") != "file" or value.get("encoding") != "base64" or \
                type(value.get("size")) is not int or not 0 < value["size"] <= 1024 * 1024 or \
                not isinstance(value.get("content"), str):
            raise ValueError("frozen Main reader source is missing or malformed: " + path)
        raw = base64.b64decode("".join(value["content"].split()), validate=True)
        with open(local_path, "rb") as stream:
            local = stream.read(1024 * 1024 + 1)
        if len(raw) != value["size"] or raw != local or \
                pin is not None and hashlib.sha256(raw).hexdigest() != pin:
            raise ValueError("loaded Main reader differs from frozen H source: " + path)
        source_bytes[path] = len(raw)
    route["runtime_source_bytes"] = source_bytes


def main_route_historical(api: Api, revision: str) -> bool:
    """Whether original policy revision P predates native MAIN routing (both files absent)."""
    from authorize_compiler import MAIN_ROUTE_MODULE_PATH, MAIN_ROUTE_PATH, main_route_content
    return all(main_route_content(api, path, revision, absent_ok=True) is None
               for path in (MAIN_ROUTE_PATH, MAIN_ROUTE_MODULE_PATH))


def main_route(api: Api, repository: str, executor_run: str, attempt: str) -> dict:
    """Authenticate original executor N -> policy P -> measurement H through GitHub and the native resolver."""
    from authorize_compiler import resolve_main_route
    if not isinstance(executor_run, str) or not DECIMAL.fullmatch(executor_run) or \
            not isinstance(attempt, str) or not DECIMAL.fullmatch(attempt) or int(attempt) < 1:
        raise ValueError("Main routing requires an original positive executor attempt")
    original = api.request(f"/actions/runs/{executor_run}/attempts/{attempt}")
    if not isinstance(original, dict) or type(original.get("id")) is not int or original["id"] != int(executor_run) or \
            type(original.get("run_attempt")) is not int or original["run_attempt"] != int(attempt) or \
            original.get("path") != BENCH_WORKFLOW or original.get("event") != "workflow_run" or \
            original.get("head_branch") != "main" or not isinstance(original.get("head_sha"), str) or \
            not SHA.fullmatch(original["head_sha"]) or \
            not isinstance(original.get("repository"), dict) or original["repository"].get("full_name") != repository:
        raise ValueError("Main routing original executor attempt identity is malformed")
    route = resolve_main_route(api, repository, original, attempt)
    if not isinstance(route, dict) or type(route.get("main_owned")) is not bool or \
            any(not isinstance(route.get(key), str) for key in MAIN_ROUTE_FIELDS) or \
            route["main_policy_revision"] != original["head_sha"] or \
            not SHA.fullmatch(route["main_measurement_revision"]):
        raise ValueError("Main native routing result is malformed or rebound to another original policy")
    if route["main_owned"]:
        from compiler_receipt import named_main_profile
        named_main_profile(route["main_profile"])
        if route["main_phase_schema"] != MAIN_PHASE_SCHEMA or \
                route["main_preparation_policy"] not in ("legacy-rebuild", "snapshot-v1") or \
                any(not re.fullmatch(r"[0-9a-f]{64}", route[key]) for key in
                    ("lab_sha256", "python_sha256", "driver_sha256", "compare_sha256", "receipt_sha256",
                     "owned_phase_sha256", "owned_plan_sha256")) or not route["python_path"].startswith("/"):
            raise ValueError("Main-owned route lacks its frozen profile, policy or runtime pins")
        if any(not route[key].startswith("/") or any(part in ("", ".", "..") for part in route[key][1:].split("/"))
               for key in ("trusted_root", "candidate_root", "work_root", "evidence_root")):
            raise ValueError("Main-owned route lacks independently approved canonical roots")
        main_runtime_pins(api, route)
    elif route["main_profile"] != PROFILE["name"] or route["main_preparation_policy"] != "legacy-rebuild" or \
            route["main_measurement_revision"] != original["head_sha"]:
        raise ValueError("historical Main route changed the original long baseline recipe")
    title = original.get("display_title")
    request = re.fullmatch(r"9700X request ([1-9][0-9]*)\.([1-9][0-9]*) head ([0-9a-f]{40})", title) if isinstance(title, str) else None
    # A routed revision already required this title in resolve_main_route; only a pre-routing
    # historical run (absent policy at original P) predates the canonical request title.
    if request is None and (route["main_owned"] or not main_route_historical(api, original["head_sha"])):
        raise ValueError("Main original executor lacks its independently reverified request identity")
    route.update(executor_run=executor_run, executor_attempt=attempt,
                 **({"request_run": "-", "request_attempt": "-", "request_head": "-"} if request is None else
                    {"request_run": request[1], "request_attempt": request[2], "request_head": request[3]}))
    if route["main_owned"]:
        from authorize_compiler import main_route_attempt
        unused_run, original_request, original_head = main_route_attempt(api, repository, original, attempt)
        if str(original_request["id"]) != request[1] or str(original_request["run_attempt"]) != request[2] or original_head != request[3]:
            raise ValueError("Main route original request attempt was rebound")
        route["original_request"] = original_request
    return route


def main_runtime_record(files: dict[str, bytes], route: dict, ownership: dict) -> dict:
    """Bind native actual file observations to protected original P -> H and API-loaded H helper sizes."""
    raw = files.get("main-runtime.tsv")
    if not isinstance(raw, bytes) or not 0 < len(raw) <= 16384:
        raise ValueError("Main native runtime observations are missing or oversized")
    record = sampling_tsv(raw)
    wanted = {"schema": "buster-compiler-main-runtime-v1", "measurement_revision": route["main_measurement_revision"],
              "policy_revision": route["main_policy_revision"], "executor_run": route["executor_run"],
              "executor_attempt": route["executor_attempt"], "request_run": route["request_run"],
              "request_attempt": route["request_attempt"], "request_head": route["request_head"],
              **{key: route[key] for key in ("trusted_root", "candidate_root", "work_root", "evidence_root")}}
    paths = {"python": route["python_path"], "driver": ownership.get("driver_path"),
             **{role: route["trusted_root"] + "/" + path for role, path in
                (("lab", "tools/uarch_lab.py"), ("compare", "tools/bench_direct/compiler_compare.py"),
                 ("receipt", "tools/bench_direct/compiler_receipt.py"),
                 ("owned_phase", "tools/bench_direct/compiler_owned_phase.py"),
                 ("owned_plan", "tools/bench_direct/compiler_owned_plan.py"))}}
    sizes = route.get("runtime_source_bytes")
    if not isinstance(sizes, dict):
        raise ValueError("Main runtime observations lack freshly verified frozen H source sizes")
    for role, path in paths.items():
        wanted[role + "_path"] = path
        wanted[role + "_sha256"] = route[role + "_sha256"]
        count = sampling_integer(record.get(role + "_bytes"), True)
        if count > 512 << 20 or role not in ("python", "driver") and sizes.get(path[len(route["trusted_root"]) + 1:]) != count:
            raise ValueError("Main native observed file bytes differ from immutable H source")
        wanted[role + "_bytes"] = str(count)
    if list(record) != list(wanted) or any(record.get(key) != value for key, value in wanted.items()):
        raise ValueError("Main native runtime pins, source roots or original attempt identity differ")
    return record



MAIN_NATIVE_FILES = frozenset(("main-route.tsv", "main-facts.tsv", "main-identity.tsv", "main-runtime.tsv",
                              "physical-job-clock.tsv", "main-clock.tsv", "main-summary.md", "main-owner.json",
                              "main-owner.json.argv", "main-owner.json.bootstrap.complete",
                              "main-owner.json.stdout", "main-owner.json.stderr"))


def main_archive_files(payload: bytes) -> dict[str, bytes]:
    """Normalize the uploader's fixed LCA layout in memory, keeping original proof paths separately bound."""
    raw = sampling_archive(payload, member_limit=MAIN_ARCHIVE_FILE_LIMIT, require_nonexecutable=True)
    flat, nested = "receipt.json" in raw, "evidence/receipt.json" in raw
    if flat == nested:
        raise ValueError("owned Main ZIP has missing or colliding ordinary evidence roots")
    if flat:
        if any(name.startswith(("evidence/", "evidence.native/")) for name in raw):
            raise ValueError("owned Main ZIP mixes flat and nested evidence roots")
        return raw
    result = {}
    for name, value in raw.items():
        if name.startswith("evidence/"):
            relative = name[len("evidence/"):]
        elif name.startswith("evidence.native/"):
            relative = name[len("evidence.native/"):]
            if relative not in MAIN_NATIVE_FILES:
                raise ValueError("owned Main ZIP contains undeclared native sibling data")
        else:
            raise ValueError("owned Main ZIP has data outside the fixed evidence roots")
        if relative in result and (relative not in MAIN_NATIVE_FILES or result[relative] != value):
            raise ValueError("owned Main ZIP has colliding or changed duplicate native proof")
        result[relative] = value
    return result


def main_owner_files(api: Api, files: dict[str, bytes], route: dict, receipt: dict, ownership: dict) -> dict:
    """The canonical native manager and original platform attempt must both prove complete cleanup within budget."""
    from compiler_owned_phase import read_record, validate_record, validate_bootstrap, command_bytes
    identity = receipt.get("identity")
    if not isinstance(identity, dict) or any(not isinstance(identity.get(key), str) or not identity[key] for key in
            ("pull", "pull_head", "base", "base_tree", "head", "head_tree", "request_run_id", "run_id", "run_attempt")) or \
            identity["request_run_id"] != route["request_run"] or identity["run_id"] != route["executor_run"] or \
            identity["run_attempt"] != route["executor_attempt"] or identity["head"] != route["request_head"]:
        raise ValueError("Main outer identity differs from its original API executor and request")
    clock = sampling_tsv(files.get("main-clock.tsv"))
    columns = ("schema", "physical_job_clock_sha256", "job_elapsed_at_native_entry_us",
               "native_elapsed_at_owner_admission_us", "remaining_us", "timeout_seconds")
    clock_raw = files.get("physical-job-clock.tsv")
    if tuple(clock) != columns or clock.get("schema") != "buster-compiler-main-clock-v1" or \
            not isinstance(clock_raw, bytes) or clock["physical_job_clock_sha256"] != hashlib.sha256(clock_raw).hexdigest():
        raise ValueError("Main native clamped clock observation is missing or rebound")
    entry = sampling_integer(clock["job_elapsed_at_native_entry_us"], True)
    elapsed = sampling_integer(clock["native_elapsed_at_owner_admission_us"])
    remaining = sampling_integer(clock["remaining_us"], True)
    timeout = sampling_integer(clock["timeout_seconds"], True)
    # Reuse the existing shared conservative clamp: preentry consumes worker
    # budget too. The separate full API job below includes checkout and upload.
    if entry + elapsed >= 5280 * 1000000 or remaining != 5280 * 1000000 - entry - elapsed or \
            timeout != remaining // 1000000:
        raise ValueError("Main owner timeout extends or contradicts the actual shared job-clock allocation")
    argv = [route["python_path"], "-B", route["trusted_root"] + "/tools/bench_direct/compiler_compare.py",
            "--candidate", route["candidate_root"], "--lab", route["trusted_root"] + "/tools/uarch_lab.py",
            "--work", route["work_root"], "--evidence", route["evidence_root"],
            "--summary", route["evidence_root"] + ".native/main-summary.md",
            "--closure-policy", route["main_preparation_policy"], "--main-owned-phases",
            "--main-profile", route["main_profile"], "--closure-driver", ownership.get("driver_path"),
            "--mode", "main", "--repository", "buster14a/buster", "--ref", "refs/heads/main",
            "--pull", identity["pull"], "--pull-head", identity["pull_head"], "--base", identity["base"],
            "--base-tree", identity["base_tree"], "--head", identity["head"], "--head-tree", identity["head_tree"],
            "--trusted-revision", route["main_measurement_revision"], "--request-run-id", route["request_run"],
            "--run-id", route["executor_run"], "--run-attempt", route["executor_attempt"]]
    stem = "main-owner.json"
    raw = {label: files.get(stem + suffix) for label, suffix in
           (("receipt", ""), ("command", ".argv"), ("bootstrap", ".bootstrap.complete"),
            ("stdout", ".stdout"), ("stderr", ".stderr"))}
    if any(not isinstance(value, bytes) or len(value) > MEMBER_LIMIT for value in raw.values()) or \
            raw["command"] != command_bytes(argv):
        raise ValueError("Main exact five native owner members or canonical command are missing or changed")
    native = read_record(raw["receipt"])
    reasons = validate_record(native, argv, route["trusted_root"], timeout, route["driver_sha256"],
        raw["stdout"], raw["stderr"], receipt_path=route["evidence_root"] + ".native/main-owner.json")
    reasons += validate_bootstrap(native, raw["bootstrap"], ownership)
    if reasons:
        raise ValueError("Main outer native cleanup proof is invalid: " + "; ".join(reasons))
    if entry + elapsed + native["duration_us"] > 5280 * 1000000 or \
            any(type(row.get("bridge_wall_us")) is not int or row["bridge_wall_us"] < 0 for row in ownership["phases"]) or \
            sum(row["bridge_wall_us"] for row in ownership["phases"]) > native["duration_us"]:
        raise ValueError("Main outer and complete child phase clock populations exceed their original budget")
    if api is None:
        raise ValueError("Main publication has no independently authenticated original platform job")
    jobs = api.pages(f"/actions/runs/{route['executor_run']}/attempts/{route['executor_attempt']}/jobs", "jobs")
    matches = [job for job in jobs if isinstance(job, dict) and job.get("name") == COMPARE_JOBS["main"]]
    if len(matches) != 1:
        raise ValueError("Main original physical job is missing or ambiguous")
    job = matches[0]
    if type(job.get("id")) is not int or job["id"] <= 0 or type(job.get("run_id")) is not int or \
            str(job["run_id"]) != route["executor_run"] or type(job.get("run_attempt")) is not int or \
            str(job["run_attempt"]) != route["executor_attempt"] or job.get("head_sha") != route["main_policy_revision"] or \
            type(job.get("runner_id")) is not int or job["runner_id"] <= 0 or \
            not isinstance(job.get("runner_name"), str) or not job["runner_name"] or \
            not isinstance(job.get("labels"), list) or any(not isinstance(label, str) for label in job["labels"]) or \
            not {"self-hosted", "Linux", "X64", "buster-zen5", "ryzen-9700x"}.issubset(set(job["labels"])):
        raise ValueError("Main physical runner or original executor attempt provenance is malformed")
    accounting = sampling_job_accounting(job, entry + elapsed + native["duration_us"], 5400, job_name=COMPARE_JOBS["main"])
    authority = {"repository": "buster14a/buster", "run_id": route["executor_run"],
                 "executor": {"head_sha": route["main_policy_revision"], "run_attempt": int(route["executor_attempt"])}}
    platform = physical_clock_binding(authority, files, job, "main", COMPARE_JOBS["main"])
    if entry < platform["observed_pre_entry_us"]:
        raise ValueError("Main native entry predates its authenticated original platform observation")
    return {"state": "complete", "native_owner_wall_us": native["duration_us"], "accounting": accounting,
            "clock": platform, "allocated_timeout_seconds": timeout,
            "owner_sha256": hashlib.sha256(raw["receipt"]).hexdigest()}


def main_owned_files(files: dict[str, bytes], route: dict, *, prefix: str = "",
                     expected_roots: dict | None = None, diagnostic: bool = False,
                     expected_cpu_model: str = "AMD Ryzen 7 9700X 8-Core Processor",
                     api: Api | None = None) -> tuple[dict, dict, dict]:
    """Decode one trusted owned ordinary population as bounded data; no artifact code is executed."""
    from compiler_receipt import named_main_profile
    if route.get("main_owned") is not True or route.get("main_phase_schema") != MAIN_PHASE_SCHEMA or \
            route.get("main_preparation_policy") not in ("legacy-rebuild", "snapshot-v1") or \
            not isinstance(route.get("main_measurement_revision"), str) or not SHA.fullmatch(route["main_measurement_revision"]) or \
            not isinstance(route.get("driver_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", route["driver_sha256"]):
        raise ValueError("owned ordinary evidence requires an independently trusted Main route")
    profile = named_main_profile(route["main_profile"])
    if not diagnostic:
        if any(name.rsplit("/", 1)[-1] in ("fixture-plan.json", "fixture-status.json", "cleanup-uncertain") for name in files):
            raise ValueError("diagnostic or cleanup-uncertain Main evidence cannot become publication authority")
        for name in files:
            if name.endswith((".json",)) and name.rsplit("/", 1)[-1] in ("receipt.json", "summary.json", "compare.json", "metadata.json"):
                if sampling_json(files, name).get("diagnostic_fixture") is True:
                    raise ValueError("diagnostic Main evidence cannot become publication authority")
    receipt = sampling_json(files, prefix + "receipt.json")
    summary = sampling_json(files, prefix + "lab/summary.json")
    ownership = receipt.get("phase_ownership")
    if receipt.get("mode") != "main" or receipt.get("profile") != profile or \
            receipt.get("preparation_policy") != route["main_preparation_policy"] or \
            not isinstance(ownership, dict) or ownership.get("schema") != MAIN_PHASE_SCHEMA or \
            ownership.get("owned_preflight") is not True or \
            ownership.get("trusted_revision") != route["main_measurement_revision"] or \
            ownership.get("driver_sha256") != route["driver_sha256"] or \
            ownership.get("python_path") != route.get("python_path") or \
            not isinstance(ownership.get("phases"), list) or not 1 <= len(ownership["phases"]) <= 256:
        raise ValueError("owned ordinary profile, preparation, runtime or complete preflight differs from its trusted route")
    roots = {key: ownership.get(key) for key in
             ("candidate_root", "trusted_root", "work_root", "evidence_root", "binaries_root", "directory", "lab_path")}
    if any(not isinstance(value, str) or not value.startswith("/") or
           any(part in ("", ".", "..") for part in value[1:].split("/")) for value in roots.values()) or \
            roots["binaries_root"] != roots["work_root"] + "/bin" or \
            roots["directory"] != roots["evidence_root"] + "/owned-phases" or \
            roots["lab_path"] != roots["trusted_root"] + "/tools/uarch_lab.py":
        raise ValueError("owned ordinary canonical source/work/evidence paths differ from the frozen recipe")
    if expected_roots is not None:
        if any(roots.get(key) != value for key, value in expected_roots.items()):
            raise ValueError("owned ordinary roots differ from the independently frozen native fixture")
    elif any(roots[key] != route.get(key) for key in ("candidate_root", "trusted_root", "work_root", "evidence_root")):
        raise ValueError("owned ordinary roots changed the trusted workflow recipe")
    runtime = main_runtime_record(files, route, ownership) if not diagnostic else None
    outer = main_owner_files(api, files, route, receipt, ownership) if not diagnostic else None
    owned = {}
    suffixes = (("receipt", ""), ("command", ".argv"), ("stdout", ".stdout"),
                ("stderr", ".stderr"), ("bootstrap", ".bootstrap.complete"))
    for phase in ownership["phases"]:
        name = phase.get("file") if isinstance(phase, dict) else None
        if not isinstance(name, str) or not re.fullmatch(r"[0-9]{4}\.json", name) or name in owned:
            raise ValueError("owned ordinary phase population is ambiguous")
        owned[name] = {label: files.get(prefix + "owned-phases/" + name + suffix) for label, suffix in suffixes}
        if any(not isinstance(raw, bytes) or len(raw) > MEMBER_LIMIT for raw in owned[name].values()):
            raise ValueError("owned ordinary exact five raw phase members are missing or oversized")
    actual = {name[len(prefix + "owned-phases/"):] for name in files if name.startswith(prefix + "owned-phases/")}
    if actual != {name + suffix for name in owned for unused, suffix in suffixes}:
        raise ValueError("owned ordinary phase files are missing or undeclared")
    throughput = {"summary": sampling_json(files, prefix + "throughput/summary.json"),
                  "metadata": sampling_json(files, prefix + "throughput/metadata.json")}
    closure = {"owned_phases": owned, "owned_throughput": {
        "summary": files[prefix + "throughput/summary.json"], "metadata": files[prefix + "throughput/metadata.json"]}}
    if route["main_preparation_policy"] == "snapshot-v1":
        closure.update({operation: files.get(prefix + "closure-" + operation + ".json.manifest.tsv")
                        for operation in ("snapshot", "restore", "verify")})
    if receipt.get("throughput_profile") != THROUGHPUT_PROFILE or "scaling_profile" in receipt or \
            not isinstance(receipt.get("inline_acceptance"), dict) or receipt["inline_acceptance"].get("requested") is not False or \
            "ast_pilot_acceptance" in receipt or "ast_retirement_acceptance" in receipt or \
            "constant_reuse_acceptance" in receipt or "process_floor" in receipt or \
            "throughput_real_source" in receipt or "production_profile" in receipt or \
            "compiler_probes" in receipt or "frontend_selector" in receipt or "wild_linker" in receipt or \
            "link_tls" in receipt or "selfhost_inline" in receipt or "frontend_nesting" in receipt:
        raise ValueError("owned ordinary Main evidence changed the complete original corpus recipe")
    identity = receipt.get("identity")
    if not isinstance(identity, dict) or identity.get("trusted_revision") != route["main_measurement_revision"]:
        raise ValueError("owned ordinary identity is rebound to another measurement revision")
    reasons = validate_closure(receipt, closure, expected_policy=route["main_preparation_policy"],
        require_owned_phases=True, require_owned_preflight=True, expected_phase_schema=MAIN_PHASE_SCHEMA,
        expected_profile=route["main_profile"], expected_phase_driver_sha256=route["driver_sha256"],
        expected_trusted_revision=route["main_measurement_revision"])
    reasons += classify(summary, receipt.get("binaries"), expected_profile=route["main_profile"])
    reasons += classify_throughput(throughput["summary"], throughput["metadata"], receipt.get("binaries"))
    if reasons:
        raise ValueError("owned ordinary semantic proof is invalid: " + "; ".join(reasons[:12]))
    plan = {"source_root": roots["candidate_root"], "baseline_revision": identity.get("base")}
    replay = utility_series_replay(files, prefix + "lab/", plan, "", receipt.get("binaries"),
                                  expected_cpu_model=expected_cpu_model, expected_profile=route["main_profile"],
                                  work_root=roots["work_root"])
    throughput.update(closure=closure, main_owned_lab=replay, main_runtime=runtime, main_owner=outer)
    return receipt, summary, throughput


def main_owned_evidence(api: Api, run_id: str, name: str, route: dict) -> tuple[object, object, str, dict, dict]:
    """The original attempt's unique immutable ZIP, requiring all owned raw proof and lab inference."""
    artifact = {}
    try:
        listing = api.request(f"/actions/runs/{run_id}/artifacts?" + urllib.parse.urlencode({"name": name, "per_page": 10}))
        rows = listing.get("artifacts") if isinstance(listing, dict) else None
        if not isinstance(rows, list) or len(rows) >= 10:
            raise ValueError("owned ordinary artifact inventory is malformed or clipped")
        matches = [row for row in rows if isinstance(row, dict) and row.get("name") == name]
        if len(matches) != 1:
            raise ValueError("owned ordinary artifact is missing or ambiguous")
        artifact = matches[0]
        origin = artifact.get("workflow_run")
        if type(artifact.get("id")) is not int or artifact["id"] <= 0 or artifact.get("expired") is not False or \
                type(artifact.get("size_in_bytes")) is not int or not 0 < artifact["size_in_bytes"] <= ARTIFACT_LIMIT or \
                not isinstance(origin, dict) or type(origin.get("id")) is not int or origin["id"] != int(run_id) or \
                origin.get("head_sha") != route["main_policy_revision"] or \
                not isinstance(artifact.get("digest"), str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", artifact["digest"]):
            raise ValueError("owned ordinary artifact API identity, origin, size, expiry or digest is malformed")
        payload = api.download(api.prefix + f"/actions/artifacts/{artifact['id']}/zip", max_bytes=ARTIFACT_LIMIT)
        digest = hashlib.sha256(payload).hexdigest()
        if len(payload) != artifact["size_in_bytes"] or "sha256:" + digest != artifact["digest"]:
            raise ValueError("owned ordinary downloaded ZIP differs from its immutable API artifact")
        files = main_archive_files(payload)
        receipt, summary, throughput = main_owned_files(files, route, api=api)
        throughput["machine"] = files.get(MACHINE_RECORD)
        artifact = dict(artifact, verified_archive_sha256=digest)
        return receipt, summary, "", artifact, throughput
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, urllib.error.URLError) as error:
        return None, None, "Main-owned evidence refused: " + str(error), artifact, {}


def machine_record(raw: object, receipt: object, mode: str, run_id: str, attempt: str,
                   require: bool = True) -> tuple[list[str], dict]:
    """(problems, facts) for the retained #2758 machine-specification record.

    Required facts must be `available` and agree with the approved host, the
    receipt's observed CPU model and the attempt being published. Optional
    facts are reported with the collector's own status and reason, never
    filled in. Only publication-only recovery of an attempt whose authenticated
    workflow revision predates retention (machine_record_required) passes
    require=False; a record that is present is always checked.
    """
    problems: list[str] = []
    facts: dict = {}
    record = None
    if raw is None:
        if require:
            problems.append(f"machine-specification record {MACHINE_RECORD} is missing from the evidence artifact")
    else:
        try:
            record = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object)
        except (DuplicateKey, UnicodeDecodeError, ValueError, AttributeError):
            record = None
        if not isinstance(record, dict) or not record or any(
                not isinstance(value, dict) or set(value) != {"value", "status", "reason"} or
                not all(isinstance(value[key], str) for key in value) or value["status"] not in MACHINE_STATUSES
                for value in record.values()):
            problems.append(f"machine-specification record {MACHINE_RECORD} is malformed")
            record = None
    if record is not None:
        for key in (*MACHINE_REQUIRED, *MACHINE_OPTIONAL):
            field = record.get(key)
            if field is None:
                facts[key] = "absent from the record"
            elif field["status"] == "available":
                facts[key] = field["value"]
            else:
                facts[key] = field["status"] + (f" ({field['reason']})" if field["reason"] else "")
        missing = [key for key in MACHINE_REQUIRED if (record.get(key) or {}).get("status") != "available"]
        for key in missing:
            problems.append(f"machine-specification record lacks required host fact {key}: {facts[key]}")
        if not missing:
            value = {key: record[key]["value"] for key in MACHINE_REQUIRED}
            host = receipt.get("host") if isinstance(receipt, dict) else None
            observed = host.get("cpu_model") if isinstance(host, dict) else None
            if value["schema"] != MACHINE_SCHEMA:
                problems.append(f"machine-specification record schema {value['schema']!r} is not {MACHINE_SCHEMA}")
            if not APPROVED_HOST.search(value["cpu_model"]):
                problems.append(f"machine-specification record CPU {value['cpu_model']!r} is not the approved "
                                "Zen 5 host (AMD Ryzen 7 9700X)")
            if not isinstance(observed, str) or value["cpu_model"].strip() != observed.strip():
                problems.append(f"machine-specification record CPU {value['cpu_model']!r} differs from the "
                                f"receipt's observed CPU {observed!r}")
            if value["machine_arch"] != "x86_64":
                problems.append(f"machine-specification record architecture {value['machine_arch']!r} is not x86_64")
            identity = {"run_id": run_id, "run_attempt": attempt, "job": MACHINE_JOBS.get(mode, "")}
            for key, wanted in identity.items():
                if not wanted or value[key] != wanted:
                    problems.append(f"machine-specification record {key} {value[key]!r} is not this attempt's {wanted!r}")
    return problems, facts


# The producer seals every measured ordinary attempt from the revision that added this (#2918).
SEALING_PRODUCER = "tools/bench_direct/compiler_compare.py"
SEALING_PRODUCER_MARKER = b"def seal_evidence("


def sealing_required(api: Api, revision: str) -> bool:
    """Whether an authenticated run revision's producer sealed its measured ordinary attempts (#2918).

    Only a readable producer without the sealing step is a legacy revision whose recovered
    attempt may be legacy-unsealed; a malformed revision, an unreadable or absent file and
    every later revision require v1.
    """
    content = None
    if isinstance(revision, str) and SHA.fullmatch(revision):
        content, unused_problem = _github_file(api, revision, SEALING_PRODUCER)
    return not content or SEALING_PRODUCER_MARKER in content


def machine_record_required(api: Api, revision: str) -> bool:
    """Whether the bench workflow at an authenticated run revision retains the machine record.

    Only a readable workflow without the retention step is a legacy revision
    whose recovered attempt may lack the record; a malformed revision, an
    unreadable or absent file and every later revision require it.
    """
    content = None
    if isinstance(revision, str) and SHA.fullmatch(revision):
        content, unused_problem = _github_file(api, revision, BENCH_WORKFLOW)
    return not content or MACHINE_RETENTION_STEP in content.decode("utf-8", errors="replace")


def read_evidence(api: Api, run_id: str, name: str, *, trusted_main_route: dict | None = None) -> tuple[object, object, str, dict, dict]:
    """(receipt, summary, problem, artifact row, throughput) from this run's one evidence artifact."""
    if trusted_main_route is not None:
        return main_owned_evidence(api, run_id, name, trusted_main_route)
    listing = api.request(f"/actions/runs/{run_id}/artifacts?" + urllib.parse.urlencode({"name": name, "per_page": 10}))
    rows = [row for row in listing.get("artifacts", []) if isinstance(row, dict) and row.get("name") == name] \
        if isinstance(listing, dict) else []
    receipt = summary = None
    problem = ""
    artifact: dict = {}
    throughput: dict = {}
    if len(rows) != 1:
        problem = f"expected one evidence artifact {name}, found {len(rows)}"
    elif rows[0].get("expired") or type(rows[0].get("size_in_bytes")) is not int or \
            rows[0]["size_in_bytes"] > ARTIFACT_LIMIT or not isinstance(rows[0].get("archive_download_url"), str):
        problem = "evidence artifact is expired, oversized or malformed"
    else:
        artifact = rows[0]
        payload = api.download(rows[0]["archive_download_url"])
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            # Aliases only detect collisions; required members are still looked up by exact name.
            members: dict = {}
            aliases: set = set()
            for info in archive.infolist():
                alias = member_identity(info.filename)
                if alias in aliases:
                    problem = f"evidence archive has duplicate member {alias!r} ({info.filename!r})"
                    break
                aliases.add(alias)
                members[info.filename] = info
            machine_info = members.get(MACHINE_RECORD) if not problem else None
            machine = None if machine_info is None else \
                archive.read(machine_info) if machine_info.file_size <= MEMBER_LIMIT else b""
            receipt_info = members.get("receipt.json") if not problem else None
            if receipt_info is not None and receipt_info.file_size <= MEMBER_LIMIT:
                try:
                    receipt = json.loads(archive.read(receipt_info).decode("utf-8"), object_pairs_hook=unique_object)
                except DuplicateKey as error:
                    problem = f"evidence member receipt.json has duplicate JSON key {error.args[0]!r}"
                except (UnicodeDecodeError, ValueError):
                    problem = "evidence member receipt.json is malformed JSON"
            if not problem and isinstance(receipt, dict) and receipt.get("profile") in ANALYZER_PROFILE_BY_LINE.values():
                superseded = receipt.get("state") == "superseded"
                raw_files: dict[str, bytes] = {}
                raw_bytes = 0
                for info in archive.infolist():
                    name = info.filename
                    if not name.startswith("analyzer/") or info.is_dir():
                        continue
                    relative = name[len("analyzer/"):]
                    if not relative or member_identity(relative) != relative or ".." in relative.split("/"):
                        problem = f"analyzer evidence member path is malformed: {name!r}"
                        break
                    kind = stat.S_IFMT(info.external_attr >> 16)
                    if kind not in (0, stat.S_IFREG):
                        problem = f"analyzer evidence member is not a regular file: {name!r}"
                        break
                    limit = MEMBER_LIMIT if relative.endswith(".json") else ANALYZER_MEMBER_LIMIT
                    raw_bytes += info.file_size
                    if info.file_size > limit or raw_bytes > ANALYZER_BUNDLE_LIMIT:
                        problem = f"analyzer evidence bundle exceeds a member or total size bound at {name!r}"
                        break
                    try:
                        raw_files[relative] = archive.read(info)
                    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
                        problem = f"analyzer evidence member {name!r} could not be read: {error}"
                        break
                required = ("request.txt",) if superseded else (*ANALYZER_REQUIRED_FILES, "summary.json")
                for member in required:
                    if member not in raw_files and not problem:
                        problem = f"required analyzer evidence member analyzer/{member} is missing"
                summary = None
                if not problem and "summary.json" in raw_files:
                    try:
                        summary = json.loads(raw_files["summary.json"].decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member analyzer/summary.json has duplicate JSON key {error.args[0]!r}"
                    except (UnicodeDecodeError, ValueError):
                        problem = "evidence member analyzer/summary.json is malformed JSON"
                throughput = {"analyzer": {"summary": summary, "files": raw_files} if not problem else None,
                              "machine": machine}
                return receipt, summary, problem, artifact, throughput
            values = []
            scaling = [f"scaling/{name}/{leaf}" for name in SCALING_PROFILE["series"]
                       for leaf in ("scaling.json", "scaling-metadata.json")]
            for member in ("receipt.json", "lab/summary.json", "throughput/summary.json", "throughput/metadata.json",
                           *scaling):
                info = members.get(member) if not problem else None
                value = None
                if info is not None and info.file_size <= MEMBER_LIMIT:
                    try:
                        value = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                    except ValueError:
                        value = None
                values.append(value)
            receipt, summary = values[:2]
            throughput = {"summary": values[2], "metadata": values[3],
                          "scaling": {name: {"summary": values[4 + 2 * index], "metadata": values[5 + 2 * index]}
                                      for index, name in enumerate(SCALING_PROFILE["series"])},
                          "machine": machine}
            # Sealed raw evidence (#2918): the whole bundle, through the strict bounded reader.
            if not problem and (EVIDENCE_MANIFEST in members or EVIDENCE_SEAL in members):
                try:
                    throughput["sealed_files"] = sampling_archive(payload, member_limit=EVIDENCE_ARCHIVE_MEMBERS,
                                                                  require_nonexecutable=True)
                except ValueError as error:
                    throughput["sealed_files"] = None
                    throughput["sealed_problem"] = f"sealed evidence archive refused: {error}"
            if isinstance(receipt, dict) and receipt.get("closure") is not None:
                closure_files: dict = {}
                for operation in ("snapshot", "restore", "verify"):
                    member = f"closure-{operation}.json.manifest.tsv"
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required frozen baseline evidence {member} missing or oversized"
                        break
                    closure_files[operation] = archive.read(info)
                throughput["closure"] = closure_files
            # A non-owned pull snapshot-v1 attempt's native phase population (#3211): validate_closure
            # replays every declared phase's five retained members, as for the owned routes.
            ownership = receipt.get("phase_ownership") if isinstance(receipt, dict) and not problem else None
            if isinstance(ownership, dict):
                owned: dict = {}
                phases = ownership.get("phases")
                for phase in phases if isinstance(phases, list) else []:
                    name = phase.get("file") if isinstance(phase, dict) else None
                    if not isinstance(name, str) or not re.fullmatch(r"[0-9]{4}\.json", name) or name in owned:
                        problem = "native phase population is ambiguous"
                        break
                    infos = {label: members.get("owned-phases/" + name + suffix) for label, suffix in
                             (("receipt", ""), ("command", ".argv"), ("stdout", ".stdout"), ("stderr", ".stderr"),
                              ("bootstrap", ".bootstrap.complete"))}
                    if any(info is None or info.file_size > MEMBER_LIMIT for info in infos.values()):
                        problem = f"required native phase evidence owned-phases/{name} missing or oversized"
                        break
                    owned[name] = {label: archive.read(info) for label, info in infos.items()}
                throughput.setdefault("closure", {})["owned_phases"] = owned
                corpus_raw = {key: members.get(f"throughput/{key}.json") for key in ("summary", "metadata")}
                if all(info is not None and info.file_size <= MEMBER_LIMIT for info in corpus_raw.values()):
                    throughput["closure"]["owned_throughput"] = {key: archive.read(info) for key, info in corpus_raw.items()}
            receipt_inline = receipt.get("inline_acceptance") if isinstance(receipt, dict) else None
            if isinstance(receipt_inline, dict) and receipt_inline.get("requested") is True:
                bundle: dict = {}
                for key, member in INLINE_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required issue #48 evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed JSON"
                        break
                throughput["inline_bundle"] = bundle if not problem and len(bundle) == len(INLINE_BUNDLE_FILES) else None
            if isinstance(receipt, dict) and receipt.get("ast_pilot_acceptance") is not None and not problem:
                bundle = {}
                for key, member in AST_PILOT_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required issue #3397 evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed JSON"
                        break
                throughput["ast_pilot_bundle"] = bundle if not problem and len(bundle) == len(AST_PILOT_BUNDLE_FILES) else None
            if isinstance(receipt, dict) and receipt.get("ast_retirement_acceptance") is not None and not problem:
                bundle = {}
                for key, member in AST_RETIREMENT_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required issue #4058 evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed JSON"
                        break
                if not problem and len(bundle) == len(AST_RETIREMENT_BUNDLE_FILES):
                    bundle["files"] = throughput.get("sealed_files")
                    throughput["ast_retirement_bundle"] = bundle
                else:
                    throughput["ast_retirement_bundle"] = None
            if isinstance(receipt, dict) and receipt.get("constant_reuse_acceptance") is not None and not problem:
                bundle = {}
                for key, member in CONSTANT_REUSE_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required issue #3011 evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed JSON"
                        break
                throughput["constant_reuse_bundle"] = bundle if not problem and len(bundle) == len(CONSTANT_REUSE_BUNDLE_FILES) else None
            if isinstance(receipt, dict) and receipt.get("process_floor") is not None and not problem:
                bundle = {}
                for key, member in PROCESS_FLOOR_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required issue #1125 evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed JSON"
                        break
                throughput["process_floor_bundle"] = bundle if not problem and len(bundle) == len(PROCESS_FLOOR_BUNDLE_FILES) else None
            if isinstance(receipt, dict) and receipt.get("throughput_real_source") is not None and not problem:
                bundle = {}
                for key, member in REAL_SOURCE_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required real-source throughput evidence member {member} missing or oversized"
                        break
                    try:
                        text = archive.read(info).decode("utf-8")
                        bundle[key] = text if key == "cpuinfo" else json.loads(text, object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["real_source_bundle"] = bundle if not problem and len(bundle) == len(REAL_SOURCE_BUNDLE_FILES) else None
            if isinstance(receipt, dict) and receipt.get("production_profile") is not None and not problem:
                bundle = {}
                for key, member in PRODUCTION_PROFILE_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required production-profile evidence member {member} missing or oversized"
                        break
                    try:
                        text = archive.read(info).decode("utf-8")
                        bundle[key] = json.loads(text, object_pairs_hook=unique_object) if member.endswith(".json") else text
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["production_profile_bundle"] = (bundle if not problem and
                                                           len(bundle) == len(PRODUCTION_PROFILE_BUNDLE_FILES) else None)
            if isinstance(receipt, dict) and receipt.get("compiler_probes") is not None and not problem:
                bundle = {}
                for key, member in COMPILER_PROBES_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required compiler-probes evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["compiler_probes_bundle"] = (bundle if not problem and
                                                        len(bundle) == len(COMPILER_PROBES_BUNDLE_FILES) else None)
            if isinstance(receipt, dict) and receipt.get("frontend_selector") is not None and not problem:
                bundle = {}
                for key, member in FRONTEND_SELECTOR_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required frontend-selector evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["frontend_selector_bundle"] = (bundle if not problem and
                                                          len(bundle) == len(FRONTEND_SELECTOR_BUNDLE_FILES) else None)
            if isinstance(receipt, dict) and receipt.get("frontend_nesting") is not None and not problem:
                bundle = {}
                for key, member in FRONTEND_NESTING_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required frontend-nesting evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["frontend_nesting_bundle"] = (bundle if not problem and
                                                         len(bundle) == len(FRONTEND_NESTING_BUNDLE_FILES) else None)
            if isinstance(receipt, dict) and receipt.get("wild_linker") is not None and not problem:
                bundle = {}
                for key, member in WILD_LINKER_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required wild-linker evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = archive.read(info).decode("utf-8")
                    except UnicodeDecodeError:
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["wild_linker_bundle"] = (bundle if not problem and
                                                    len(bundle) == len(WILD_LINKER_BUNDLE_FILES) else None)
            if isinstance(receipt, dict) and receipt.get("link_tls") is not None and not problem:
                bundle = {}
                for key, member in LINK_TLS_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required link-tls evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["link_tls_bundle"] = (bundle if not problem and
                                                 len(bundle) == len(LINK_TLS_BUNDLE_FILES) else None)
            if isinstance(receipt, dict) and receipt.get("selfhost_inline") is not None and not problem:
                bundle = {}
                for key, member in SELFHOST_INLINE_BUNDLE_FILES.items():
                    info = members.get(member)
                    if info is None or info.file_size > MEMBER_LIMIT:
                        problem = f"required self-host inline evidence member {member} missing or oversized"
                        break
                    try:
                        bundle[key] = json.loads(archive.read(info).decode("utf-8"), object_pairs_hook=unique_object)
                    except DuplicateKey as error:
                        problem = f"evidence member {member} has duplicate JSON key {error.args[0]!r}"
                        break
                    except (UnicodeDecodeError, ValueError):
                        problem = f"evidence member {member} is malformed"
                        break
                throughput["selfhost_inline_bundle"] = (bundle if not problem and
                                                        len(bundle) == len(SELFHOST_INLINE_BUNDLE_FILES) else None)
    return receipt, summary, problem, artifact, throughput


def _github_file(api: Api, revision: str, name: str = ANALYZER_REQUEST_PATH) -> tuple[bytes | None, str]:
    """Read one exact file (the request file by default) at a commit from GitHub's immutable contents API."""
    path = urllib.parse.quote(name, safe="/")
    try:
        value = api.request(f"/contents/{path}?{urllib.parse.urlencode({'ref': revision})}")
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return b"", ""
        return None, f"GitHub request-file read failed at {revision}: HTTP {error.code}"
    except (OSError, TimeoutError, ValueError) as error:
        return None, f"GitHub request-file read failed at {revision}: {error}"
    if not isinstance(value, dict) or value.get("type") != "file" or value.get("encoding") != "base64" or \
            type(value.get("size")) is not int or value["size"] < 0 or value["size"] > 2 * 1024 * 1024 or \
            not isinstance(value.get("content"), str):
        return None, f"GitHub request-file record is malformed at {revision}"
    try:
        content = base64.b64decode(value["content"], validate=False)
    except (ValueError, TypeError) as error:
        return None, f"GitHub request-file content is malformed at {revision}: {error}"
    if len(content) != value["size"]:
        return None, f"GitHub request-file size differs from its contents record at {revision}"
    return content, ""


def verify_analyzer_request_freshness(api: Api, expected: dict, receipt: object, throughput: object) -> str:
    """Derive the pull request's requested profile from its exact head and every parent."""
    if not isinstance(receipt, dict) or expected.get("mode") != "pull":
        return "pull request profile freshness check received a non-pull receipt"
    request_line = receipt.get("analyzer_request_line")
    selected_profile = ANALYZER_PROFILE_BY_LINE.get(request_line) if isinstance(request_line, str) else None
    analyzer_profile = selected_profile is not None and receipt.get("profile") == selected_profile
    if request_line is not None and not analyzer_profile:
        return "receipt analyzer profile marker does not match its exact selector"
    if not analyzer_profile and receipt.get("profile") != PROFILE:
        return "pull receipt does not name a recognized compiler or analyzer profile"
    if selected_profile is not None and receipt.get("analyzer_profile") != selected_profile:
        return "receipt analyzer profile marker does not match its exact selector"
    identity = receipt.get("identity") if isinstance(receipt.get("identity"), dict) else {}
    head = expected.get("head")
    if identity.get("head") != head or not SHA.fullmatch(str(head or "")):
        return "pull request profile freshness head does not match the authorized pull head"
    try:
        commit = api.request(f"/commits/{head}")
    except urllib.error.HTTPError as error:
        return f"pull request profile freshness commit read failed: HTTP {error.code}"
    except (OSError, TimeoutError, ValueError) as error:
        return f"pull request profile freshness commit read failed: {error}"
    parents = commit.get("parents") if isinstance(commit, dict) and commit.get("sha") == head else None
    parent_shas = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
    if not 1 <= len(parent_shas) <= 2 or len(parent_shas) != len(parents) or \
            any(not isinstance(parent, str) or not SHA.fullmatch(parent) for parent in parent_shas):
        return "pull request profile freshness could not verify the head's parent identities"
    head_bytes, problem = _github_file(api, head)
    if problem:
        return problem
    try:
        head_lines = head_bytes.decode("utf-8").splitlines()
    except UnicodeDecodeError:
        return "authorized pull request profile file is not UTF-8"
    head_counts = {line: head_lines.count(line) for line in ANALYZER_REQUEST_LINES}
    head_inline_count = head_lines.count(INLINE_ACCEPTANCE_REQUEST_LINE)
    head_ast_count = head_lines.count(AST_PILOT_ACCEPTANCE_REQUEST_LINE)
    head_retirement_count = head_lines.count(AST_RETIREMENT_REQUEST_LINE)
    head_reuse_count = head_lines.count(CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE)
    head_floor_count = head_lines.count(PROCESS_FLOOR_REQUEST_LINE)
    head_real_count = head_lines.count(THROUGHPUT_REAL_SOURCE_REQUEST_LINE)
    head_production_count = head_lines.count(PRODUCTION_PROFILE_REQUEST_LINE)
    head_probes_count = head_lines.count(COMPILER_PROBES_REQUEST_LINE)
    head_selector_count = head_lines.count(FRONTEND_SELECTOR_REQUEST_LINE)
    head_linker_count = head_lines.count(WILD_LINKER_REQUEST_LINE)
    head_link_tls_count = head_lines.count(LINK_TLS_REQUEST_LINE)
    head_selfhost_count = head_lines.count(SELFHOST_INLINE_REQUEST_LINE)
    head_nesting_count = head_lines.count(FRONTEND_NESTING_REQUEST_LINE)
    parent_analyzer_deltas = {line: [] for line in ANALYZER_REQUEST_LINES}
    parent_inline_counts = []
    ast_deltas = []
    retirement_deltas = []
    reuse_deltas = []
    floor_deltas = []
    real_deltas = []
    production_deltas = []
    probes_deltas = []
    linker_deltas = []
    selector_deltas = []
    link_tls_deltas = []
    selfhost_deltas = []
    nesting_deltas = []
    for parent in parent_shas:
        parent_bytes, problem = _github_file(api, parent)
        if problem:
            return problem
        try:
            parent_lines = parent_bytes.decode("utf-8").splitlines()
        except UnicodeDecodeError:
            return f"parent pull request profile file is not UTF-8 at {parent}"
        for line in ANALYZER_REQUEST_LINES:
            parent_analyzer_deltas[line].append(head_counts[line] - parent_lines.count(line))
        parent_inline_counts.append(parent_lines.count(INLINE_ACCEPTANCE_REQUEST_LINE))
        ast_deltas.append(head_ast_count - parent_lines.count(AST_PILOT_ACCEPTANCE_REQUEST_LINE))
        retirement_deltas.append(head_retirement_count - parent_lines.count(AST_RETIREMENT_REQUEST_LINE))
        reuse_deltas.append(head_reuse_count - parent_lines.count(CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE))
        floor_deltas.append(head_floor_count - parent_lines.count(PROCESS_FLOOR_REQUEST_LINE))
        real_deltas.append(head_real_count - parent_lines.count(THROUGHPUT_REAL_SOURCE_REQUEST_LINE))
        production_deltas.append(head_production_count - parent_lines.count(PRODUCTION_PROFILE_REQUEST_LINE))
        probes_deltas.append(head_probes_count - parent_lines.count(COMPILER_PROBES_REQUEST_LINE))
        selector_deltas.append(head_selector_count - parent_lines.count(FRONTEND_SELECTOR_REQUEST_LINE))
        linker_deltas.append(head_linker_count - parent_lines.count(WILD_LINKER_REQUEST_LINE))
        link_tls_deltas.append(head_link_tls_count - parent_lines.count(LINK_TLS_REQUEST_LINE))
        selfhost_deltas.append(head_selfhost_count - parent_lines.count(SELFHOST_INLINE_REQUEST_LINE))
        nesting_deltas.append(head_nesting_count - parent_lines.count(FRONTEND_NESTING_REQUEST_LINE))
    ast_record = receipt.get("ast_pilot_acceptance")
    if ast_record is not None and not all(delta == 1 for delta in ast_deltas):
        return ("the exact ast-pilot selector was not freshly added once at this head relative to every parent; "
                "append one selector line for each explicit ast-pilot request")
    if ast_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in ast_deltas):
        return "a fresh ast-pilot selector has no ast-pilot acceptance record in the receipt"
    retirement_record = receipt.get("ast_retirement_acceptance")
    if retirement_record is not None and not all(delta == 1 for delta in retirement_deltas):
        return ("the exact ast-retirement selector was not freshly added once at this head relative to every parent; "
                "append one selector line for each explicit ast-retirement request")
    if retirement_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in retirement_deltas):
        return "a fresh ast-retirement selector has no ast-retirement acceptance record in the receipt"
    reuse_record = receipt.get("constant_reuse_acceptance")
    if reuse_record is not None and not all(delta == 1 for delta in reuse_deltas):
        return ("the exact constant-reuse selector was not freshly added once at this head relative to every parent; "
                "append one selector line for each explicit constant-reuse request")
    if reuse_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in reuse_deltas):
        return "a fresh constant-reuse selector has no constant-reuse acceptance record in the receipt"
    floor_record = receipt.get("process_floor")
    if floor_record is not None and not all(delta == 1 for delta in floor_deltas):
        return ("the exact process-floor selector was not freshly added once at this head relative to every parent; "
                "append one selector line for each explicit process-floor request")
    if floor_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in floor_deltas):
        return "a fresh process-floor selector has no process-floor record in the receipt"
    real_record = receipt.get("throughput_real_source")
    if real_record is not None and not all(delta == 1 for delta in real_deltas):
        return ("the exact real-source throughput selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit real-source request")
    if real_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in real_deltas):
        return "a fresh real-source throughput selector has no real-source record in the receipt"
    production_record = receipt.get("production_profile")
    if production_record is not None and not all(delta == 1 for delta in production_deltas):
        return ("the exact production-profile selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit production-profile request")
    if production_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in production_deltas):
        return "a fresh production-profile selector has no production-profile record in the receipt"
    probes_record = receipt.get("compiler_probes")
    if probes_record is not None and not all(delta == 1 for delta in probes_deltas):
        return ("the exact compiler-probes selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit compiler-probes request")
    if probes_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in probes_deltas):
        return "a fresh compiler-probes selector has no compiler-probes record in the receipt"
    selector_record = receipt.get("frontend_selector")
    if selector_record is not None and not all(delta == 1 for delta in selector_deltas):
        return ("the exact frontend-selector selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit frontend-selector request")
    if selector_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in selector_deltas):
        return "a fresh frontend-selector selector has no frontend-selector record in the receipt"
    linker_record = receipt.get("wild_linker")
    if linker_record is not None and not all(delta == 1 for delta in linker_deltas):
        return ("the exact wild-linker selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit wild-linker request")
    if linker_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in linker_deltas):
        return "a fresh wild-linker selector has no wild-linker record in the receipt"
    link_tls_record = receipt.get("link_tls")
    if link_tls_record is not None and not all(delta == 1 for delta in link_tls_deltas):
        return ("the exact link-tls selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit link-tls request")
    if link_tls_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in link_tls_deltas):
        return "a fresh link-tls selector has no link-tls record in the receipt"
    selfhost_record = receipt.get("selfhost_inline")
    if selfhost_record is not None and not all(delta == 1 for delta in selfhost_deltas):
        return ("the exact self-host inline selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit self-host inline request")
    if selfhost_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in selfhost_deltas):
        return "a fresh self-host inline selector has no self-host inline record in the receipt"
    nesting_record = receipt.get("frontend_nesting")
    if nesting_record is not None and not all(delta == 1 for delta in nesting_deltas):
        return ("the exact frontend-nesting selector was not freshly added once at this head relative to every "
                "parent; append one selector line for each explicit frontend-nesting request")
    if nesting_record is None and receipt.get("state") != "superseded" and any(delta > 0 for delta in nesting_deltas):
        return "a fresh frontend-nesting selector has no frontend-nesting record in the receipt"
    if analyzer_profile:
        selected_deltas = parent_analyzer_deltas[request_line]
        other_deltas = [delta for line, values in parent_analyzer_deltas.items() if line != request_line
                        for delta in values]
        if not selected_deltas or not all(delta == 1 for delta in selected_deltas):
            return ("the exact analyzer selector was not freshly added once at this head relative to every parent; "
                    "append one selector line for each explicit full-inventory request")
        if any(delta != 0 for delta in other_deltas):
            return "the other recognized analyzer selector count changed at this head"
        analyzer = throughput.get("analyzer") if isinstance(throughput, dict) else None
        files = analyzer.get("files") if isinstance(analyzer, dict) else None
        if not isinstance(files, dict) or not isinstance(files.get("request.txt"), bytes):
            return "analyzer request bytes are missing from retained evidence"
        if head_bytes != files["request.txt"]:
            return "retained analyzer request bytes do not match the authorized GitHub head"
        if any(head_inline_count > count for count in parent_inline_counts):
            return f"{selected_profile['name']} cannot be combined with a newly requested inline acceptance profile"
        if receipt.get("analyzer_request_sha256") != hashlib.sha256(head_bytes).hexdigest():
            return "receipt analyzer request digest does not match the authorized GitHub head"
    elif any(delta > 0 for values in parent_analyzer_deltas.values() for delta in values):
        return "a fresh analyzer selector does not match the legacy compiler-profile receipt"
    return ""


def queue_delay(api: Api, run_id: str, attempt: str, mode: str) -> object:
    """Seconds the compare job waited for the 9700X runner, or None."""
    delay = None
    jobs = api.request(f"/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
    rows = [job for job in jobs.get("jobs", []) if isinstance(job, dict) and job.get("name") == COMPARE_JOBS[mode]] \
        if isinstance(jobs, dict) else []
    if len(rows) == 1 and isinstance(rows[0].get("created_at"), str) and isinstance(rows[0].get("started_at"), str):
        parse = lambda text: datetime.fromisoformat(text.replace("Z", "+00:00"))  # noqa: E731
        delay = (parse(rows[0]["started_at"]) - parse(rows[0]["created_at"])).total_seconds()
    return delay


def artifact_link(repository: str, run_id: str, artifact: dict) -> str:
    return f"{SERVER}/{repository}/actions/runs/{run_id}/artifacts/{artifact['id']}" \
        if type(artifact.get("id")) is int else ""


def links(check_url: str, workflow_url: str, attempt: str, evidence_url: str, expires: str) -> str:
    """Explicit links: a check's own details URL is not relied on (#2804)."""
    parts = [f"[Full check]({check_url})" if check_url else "Full check: NA",
             f"[Workflow run, attempt {attempt}]({workflow_url})",
             f"[Evidence artifact]({evidence_url}) (retained until {expires or 'NA'}; it expires even though "
             "this report stays)" if evidence_url else "Evidence artifact: unavailable"]
    return " · ".join(parts)


def inventory_rows(shown: dict) -> str:
    """The discharged inventory row ids of a published attempt, or none."""
    coverage = shown.get("inventory_coverage") if isinstance(shown.get("inventory_coverage"), dict) else {}
    rows = coverage.get("discharged") if isinstance(coverage.get("discharged"), list) else []
    return ", ".join(f"`{row}`" for row in rows) or "none"


UNRESOLVED_OUTCOMES = ("no detectable difference", "below-floor", "inconclusive")


def selected_profile(shown: dict, profile: dict) -> str:
    """The profile a report measured and which trusted route selected it (#3212)."""
    route = shown.get("main_route") if isinstance(shown.get("main_route"), dict) else None
    if shown.get("mode") == "pull":
        return f"`{profile.get('name', 'NA')}` (explicit pull-request request)"
    if route is None:
        return f"`{profile.get('name', 'NA')}` (Main route not resolved)"
    policy = str(route.get("main_policy_revision", "NA"))[:12]
    if route.get("main_owned") is True:
        certificate = route.get("main_certificate_revision")
        reviewed = f"certificate `{certificate[:12]}`" if isinstance(certificate, str) and certificate != "-" \
            else "no short-profile certificate"
        return f"`{route.get('main_profile', 'NA')}` (authenticated automatic Main route, policy `{policy}`, {reviewed})"
    return f"`{route.get('main_profile', 'NA')}` (Main routing disabled at policy `{policy}`)"


def achieved_uncertainty(verdict: dict) -> str:
    """Half the 95% B/A interval in percent of the baseline, or NA when unmeasured."""
    low, high = verdict.get("ci_low"), verdict.get("ci_high")
    result = "NA (not measured)"
    if type(low) in (int, float) and type(high) in (int, float) and 0 < low <= high < float("inf"):
        result = "±%.2f%% (half-width of the 95%% CI of wall B/A)" % ((high - low) * 50.0)
    return result


def leg_costs(timings: dict) -> str:
    """Per-leg host seconds as recorded; missing legs stay NA, never zero."""
    builds = timings.get("build_seconds") if isinstance(timings.get("build_seconds"), dict) else {}
    return "builds %s s, self-host measurement %s s, corpus %s s, total %s s; queue delay %s s" % (
        " + ".join(number(builds.get(key), "%.0f") for key in ("baseline", "candidate")),
        number(timings.get("measurement_seconds"), "%.0f"), number(timings.get("throughput_seconds"), "%.0f"),
        number(timings.get("total_seconds"), "%.0f"), number(timings.get("queue_delay_seconds"), "%.0f"))


def commit_report(shown: dict, summary: object, conclusion: str, title: str, notes: list[str],
                  link_line: str) -> str:
    """The commit-page report (#2804): verdict, scope, metrics with units, links; tables collapsed."""
    identity = shown.get("identity", {}) if isinstance(shown.get("identity"), dict) else {}
    summary = summary if isinstance(summary, dict) else {}
    if shown.get("profile") in ANALYZER_PROFILE_BY_LINE.values():
        inventory = summary.get("inventory") if isinstance(summary.get("inventory"), dict) else {}
        profile = shown.get("profile") if isinstance(shown.get("profile"), dict) else ANALYZER_PROFILE
        timings = shown.get("timings") if isinstance(shown.get("timings"), dict) else {}
        lines = [
            f"### {check_name('pull')}: {conclusion}, {title}", "",
            "This report retains a fixed full-inventory Clang analyzer comparison. It reports workload evidence and "
            "measurement completeness; it has no speedup or regression verdict.", "",
            "| | |", "| --- | --- |",
            f"| Candidate | `{identity.get('head', 'NA')}` (this commit) |",
            f"| Baseline | `{identity.get('base', 'NA')}` (merge base) |",
            f"| Profile | `{profile.get('name', 'NA')}` |",
            f"| Selected / excluded rows | {inventory.get('selected_rows', 'NA')} / {inventory.get('excluded_rows', 'NA')} |",
            f"| Baseline executions | {inventory.get('baseline_unique_executions', 'NA')} |",
            f"| Candidate executions / aliases | {inventory.get('candidate_unique_executions', 'NA')} / "
            f"{inventory.get('candidate_alias_rows', 'NA')} |",
            f"| Evidence status | `{summary.get('status', 'missing')}` |",
            f"| Inventory rows discharged | {inventory_rows(shown)} |",
            f"| Captured | {timings.get('started_at', 'NA')} to {timings.get('finished_at', 'NA')} |",
            "", link_line, "",
        ]
        reasons = shown.get("reasons") if isinstance(shown.get("reasons"), list) else []
        lines += [f"- {item}" for item in (*notes, *reasons)][:20]
        lines += ["", "<details><summary>Full analyzer evidence summary</summary>", "",
                  render(shown, summary, conclusion, notes), "", "</details>"]
        text = "\n".join(lines)
        if len(text) > REPORT_MARKDOWN_LIMIT:
            text = "\n".join(lines[:lines.index("<details><summary>Full analyzer evidence summary</summary>")]) \
                + "\nThe full evidence summary exceeds the comment bound; see the full check."
        return text
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) and conclusion == "success" else {}
    metrics = summary.get("metrics") if isinstance(summary.get("metrics"), dict) else {}
    wall = metrics.get("wall") if isinstance(metrics.get("wall"), dict) and verdict else {}
    plan = summary.get("plan") if isinstance(summary.get("plan"), dict) and verdict else {}
    host = shown.get("host") if isinstance(shown.get("host"), dict) else {}
    profile = shown.get("profile") if isinstance(shown.get("profile"), dict) else {}
    timings = shown.get("timings") if isinstance(shown.get("timings"), dict) else {}
    unit = wall.get("unit") if isinstance(wall.get("unit"), str) else "s"
    coverage = shown.get("coverage") if isinstance(shown.get("coverage"), dict) else {}
    baseline = range_label(coverage.get("range"), coverage.get("first_parent")) or "first parent or range baseline"
    unresolved = " This outcome is unresolved at the achieved resolution: it is neither equivalence nor a " \
        "performance pass." if verdict.get("outcome") in UNRESOLVED_OUTCOMES else ""
    lines = [
        f"### {check_name(shown.get('mode', 'main'))}: {conclusion}, {title}", "",
        (verdict.get("text") if isinstance(verdict.get("text"), str) else
         "No valid measurement: this commit was not benchmarked by this attempt.")
        + unresolved + " Performance policy: report-only. This never blocks merging, and a successful measurement "
        "is not by itself a speedup or an equivalence.", "",
        "| | |", "| --- | --- |",
        f"| Candidate | `{identity.get('head', 'NA')}` (this commit) |",
        f"| Baseline | `{identity.get('base', 'NA')}` ({baseline}) |",
        "| Wall time B/A | %s, 95%% CI [%s, %s], %s |" % (
            number(verdict.get("ratio"), "%.4f"), number(verdict.get("ci_low"), "%.4f"),
            number(verdict.get("ci_high"), "%.4f"), verdict.get("outcome", "NA")),
        f"| Wall median A / B | {number(wall.get('a_median'), '%.6g')} {unit} / "
        f"{number(wall.get('b_median'), '%.6g')} {unit} |",
        f"| Complete pairs | {number(plan.get('complete_pairs'), '%d')} |",
        f"| Achieved uncertainty | {achieved_uncertainty(verdict)} |",
        f"| Selected profile | {selected_profile(shown, profile)} |",
        f"| Host cost per leg | {leg_costs(timings)} |",
        f"| Host (observed) | `{host.get('cpu_model', 'NA')}` |",
        f"| Inventory rows discharged | {inventory_rows(shown)} |",
        f"| Scope | `{profile.get('name', 'NA')}`: {profile.get('workload', 'NA')}; corpus "
        f"{profile.get('corpus', 'NA')} |",
        f"| Captured | {timings.get('started_at', 'NA')} to {timings.get('finished_at', 'NA')} by run "
        f"{identity.get('run_id', 'NA')} attempt {identity.get('run_attempt', 'NA')}; trusted harness "
        f"`{identity.get('trusted_revision', 'NA')}` |",
        "", link_line, "",
    ]
    reasons = shown.get("reasons") if isinstance(shown.get("reasons"), list) else []
    lines += [f"- {item}" for item in (*notes, *reasons)][:20]
    detail = render(shown, summary, conclusion, notes)
    lines += ["", "<details><summary>All metrics, identities and host timings</summary>", "", detail, "", "</details>"]
    text = "\n".join(lines)
    if len(text) > REPORT_MARKDOWN_LIMIT:
        text = "\n".join(lines[:lines.index("<details><summary>All metrics, identities and host timings</summary>")]) \
            + "\nThe full tables exceed the comment bound; see the full check."
    return text


# The bench workflow's run-name: "9700X request <request run>.<request attempt> head <sha>".
REQUEST_TITLE = re.compile(r"9700X request ([1-9][0-9]*)\.([1-9][0-9]*) head ([0-9a-f]{40})")


def recovered_identity(api: Api, repository: str, run_id: str, attempt: str) -> tuple[dict, str, str, list[str]]:
    """(expected, authorize result, compare result, problems) of a past main attempt from GitHub's records.

    Publication-only recovery (#2804): the attempt's own jobs, not this run,
    supply the authorization; the identity is re-derived the way
    authorize_compiler derives it and the receipt must still equal it.
    """
    problems: list[str] = []
    run = api.request(f"/actions/runs/{run_id}")
    run = run if isinstance(run, dict) else {}
    if not (run.get("path") == BENCH_WORKFLOW and run.get("event") == "workflow_run" and
            str(run.get("id")) == run_id and type(run.get("run_attempt")) is int and run["run_attempt"] >= int(attempt)
            and isinstance(run.get("head_sha"), str) and SHA.fullmatch(run["head_sha"])):
        problems.append("the run is not an attempt of the 9700X direct workload benchmark workflow")
    jobs = api.request(f"/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
    rows = [job for job in (jobs.get("jobs", []) if isinstance(jobs, dict) else []) if isinstance(job, dict)]
    result = {}
    for role, name in (("authorize", AUTHORIZE_JOBS["main"]), ("compare", COMPARE_JOBS["main"])):
        matches = [job for job in rows if job.get("name") == name and str(job.get("run_attempt", attempt)) == attempt]
        result[role] = matches[0].get("conclusion") or "" if len(matches) == 1 else ""
    listing = api.pages(f"/actions/runs/{run_id}/artifacts", "artifacts")
    pattern = re.compile(re.escape(ARTIFACT_PREFIX) + r"([0-9a-f]{40})-" + attempt + r"\Z")
    heads = [match.group(1) for row in listing if isinstance(row, dict) and isinstance(row.get("name"), str)
             for match in [pattern.fullmatch(row["name"])] if match]
    head = heads[0] if len(heads) == 1 else ""
    expected = {"mode": "main", "repository": repository, "ref": "refs/heads/main", "head": head,
                "trusted_revision": run.get("head_sha", ""), "run_id": run_id, "run_attempt": attempt}
    # The workflow's own run-name records the request run and attempt that started it; sealed
    # raw evidence (#2918) is verified under that original request attempt.
    title = REQUEST_TITLE.fullmatch(run.get("display_title", "")) if isinstance(run.get("display_title"), str) else None
    if title and title.group(3) == head:
        expected["titled_request"] = (title.group(1), title.group(2))
    if not head:
        problems.append(f"expected one evidence artifact of attempt {attempt}, found {len(heads)}")
    return expected, result["authorize"], result["compare"], problems


def bind_request(api: Api, expected: dict, receipt: object, *, trusted_main_route: dict | None = None) -> list[str]:
    """Fill expected from the receipt's request run, re-verified exactly as authorize-compiler does."""
    identity = receipt.get("identity") if isinstance(receipt, dict) and isinstance(receipt.get("identity"), dict) else {}
    request_run, head, repository = identity.get("request_run_id", ""), expected["head"], expected["repository"]
    original_request = None
    if trusted_main_route is not None and trusted_main_route.get("main_owned") is True:
        original_request = trusted_main_route.get("original_request")
        if not isinstance(original_request, dict) or request_run != trusted_main_route.get("request_run") or \
                head != trusted_main_route.get("request_head") or \
                str(original_request.get("id")) != request_run or \
                str(original_request.get("run_attempt")) != trusted_main_route.get("request_attempt"):
            return ["the receipt request differs from its original independently authenticated Main attempt"]
    # The baseline the attempt measured: it must still be on head's first-parent chain.
    base = identity.get("base", "")
    problems = []
    if not (isinstance(request_run, str) and DECIMAL.fullmatch(request_run) and SHA.fullmatch(head)):
        problems.append("the receipt names no request run")
    else:
        commit = api.request(f"/commits/{head}")
        failures, result = verify_main(
            repository, int(request_run), head, original_request if original_request is not None else api.request(f"/actions/runs/{request_run}"), commit,
            api.request(f"/commits/{base}") if isinstance(base, str) and SHA.fullmatch(base) else None,
            api.request(f"/compare/{head}...main"), api.request(f"/commits/{head}/pulls?per_page=10"),
            parse_chain(api.request(f"/commits?sha={head}&per_page=100"), head))
        problems.extend(f"request re-verification failed: {item}" for item in failures)
        expected.update(result, request_run_id=request_run)
    return problems


def report_check_url(api: Api, head: str, marker: str) -> str:
    """The attempt's completed check, or the legacy single check of a pre-#2803 attempt."""
    rows = [row for row in owned_checks(api, head, "main", marker) if row.get("status") == "completed"]
    if not rows:
        rows = [row for row in owned_checks(api, head, "main", check_marker(head, "main"))
                if row.get("status") == "completed"]
    return rows[-1].get("html_url", "") if len(rows) >= 1 and isinstance(rows[-1].get("html_url"), str) else ""


def write_output(path: str, values: dict) -> None:
    with open(path or os.devnull, "a", encoding="utf-8") as stream:
        for name, value in values.items():
            stream.write(f"{name}={value}\n")



# Disabled sampling research uses the existing trusted hosted write boundary.
# Archive bytes are decoded in memory; no member is materialized or executed.
SAMPLING_CHECK_NAME = "9700X compiler sampling research"
SAMPLING_ARTIFACT_PREFIX = "buster-9700x-sampling-"
SAMPLING_HOST_JOB = "Sampling qualification packet"


def sampling_archive(payload: bytes, *, member_limit: int = 2048, require_nonexecutable: bool = False) -> dict[str, bytes]:
    """Bound both the ZIP directory and inflated files before consuming data."""
    import zlib
    try:
        if type(member_limit) is not int or not 1 <= member_limit <= MAIN_ARCHIVE_FILE_LIMIT or type(require_nonexecutable) is not bool:
            raise ValueError("bounded archive member limit is invalid")
        if not isinstance(payload, bytes) or not 0 < len(payload) <= ARTIFACT_LIMIT:
            raise ValueError("sampling archive is missing or oversized")
        # Reject ZIP64/multipart archives and huge directories before ZipFile
        # allocates a ZipInfo for every advertised member.
        end = payload.rfind(b"PK\x05\x06", max(0, len(payload) - 65557))
        if end < 0 or end + 22 > len(payload):
            raise ValueError("sampling archive has no bounded ZIP directory")
        import struct
        signature, disk, directory_disk, disk_count, count, size, offset, comment = struct.unpack_from("<4s4H2LH", payload, end)
        if signature != b"PK\x05\x06" or disk or directory_disk or disk_count != count or \
                not 0 < count <= member_limit or size == 0xffffffff or offset == 0xffffffff or \
                offset + size != end or end + 22 + comment != len(payload):
            raise ValueError("sampling ZIP directory is multipart, oversized or malformed")
        result = {}
        aliases = set()
        expanded = 0
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            entries = archive.infolist()
            if len(entries) != count:
                raise ValueError("sampling ZIP entry count differs from directory")
            for entry in entries:
                name = entry.filename
                alias = member_identity(name)
                components = name.rstrip("/").split("/")
                if entry.orig_filename != name or not name or len(name) > 512 or "\\" in name or name.startswith("/") or \
                        any(part in ("", ".", "..") for part in components) or \
                        any(ord(byte) < 32 or ord(byte) > 126 for byte in name) or alias in aliases:
                    raise ValueError("sampling ZIP has an unsafe or duplicate member")
                aliases.add(alias)
                kind = stat.S_IFMT(entry.external_attr >> 16)
                if entry.flag_bits & 1 or entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) or \
                        kind not in ((0, stat.S_IFDIR) if entry.is_dir() else (0, stat.S_IFREG)) or \
                        require_nonexecutable and not entry.is_dir() and (entry.external_attr >> 16) & 0o111:
                    raise ValueError("sampling ZIP has an encrypted or nonregular member")
                if entry.is_dir():
                    if entry.file_size:
                        raise ValueError("sampling ZIP directory carries file data")
                    continue
                expanded += entry.file_size
                if not 0 <= entry.file_size <= ANALYZER_MEMBER_LIMIT or expanded > ARTIFACT_LIMIT:
                    raise ValueError("sampling ZIP exceeds its inflated member or total bound")
                data = archive.read(entry)
                if len(data) != entry.file_size:
                    raise ValueError("sampling ZIP member length differs from directory")
                result[name] = data
        return result


    except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError) as error:
        raise ValueError("sampling archive compression or directory data is corrupt") from error


def sampling_tsv(data: bytes, table: bool = False) -> object:
    """Strict bounded TSV data, with no duplicate fields or ambiguous cells."""
    if not isinstance(data, bytes) or not 0 < len(data) <= MEMBER_LIMIT:
        raise ValueError("sampling TSV is missing or oversized")
    text = data.decode("ascii")
    if not text.endswith("\n") or "\r" in text or any((ord(c) < 32 and c not in "\n\t") or ord(c) > 126 for c in text):
        raise ValueError("sampling TSV has a malformed line or control byte")
    rows = [line.split("\t") for line in text[:-1].split("\n")]
    if len(rows) > 4096 or any(not row or any(not cell for cell in row) for row in rows):
        raise ValueError("sampling TSV has missing cells or excessive rows")
    if table:
        header = rows[0]
        if len(set(header)) != len(header) or any(len(row) != len(header) for row in rows[1:]):
            raise ValueError("sampling TSV has duplicate columns or inconsistent rows")
        return [dict(zip(header, row)) for row in rows[1:]]
    if any(len(row) != 2 for row in rows) or len({row[0] for row in rows}) != len(rows):
        raise ValueError("sampling TSV has duplicate or malformed fields")
    return dict(rows)


def sampling_supervision(data: bytes) -> dict:
    row = sampling_tsv(data)
    required = {"schema", "cleanup_proven", "wall_us", "adoption_waves", "adopted_signalled", "adopted_reaped"}
    if set(row) != required or row["schema"] != "buster-native-qualification-supervisor-v1" or \
            row["cleanup_proven"] != "true" or row["adopted_signalled"] != "0" or row["adopted_reaped"] != "0":
        raise ValueError("sampling phase cleanup is absent, uncertain or adopted unexpected children")
    for key in ("wall_us", "adoption_waves"):
        sampling_integer(row[key])
    if int(row["wall_us"]) <= 0:
        raise ValueError("sampling supervision duration is unavailable")
    return row


def sampling_job_accounting(job: object, native_wall_us: int, reservation_seconds: int, *,
                            job_name: str = SAMPLING_HOST_JOB) -> dict:
    """Charge the complete physical job, including checkout/upload/cleanup."""
    if not isinstance(job, dict) or job.get("name") != job_name or \
            job.get("status") != "completed" or job.get("conclusion") != "success" or \
            type(native_wall_us) is not int or native_wall_us <= 0 or \
            type(reservation_seconds) is not int or reservation_seconds <= 0:
        raise ValueError("sampling physical job or native occupancy is incomplete")
    stamps = []
    for key in ("created_at", "started_at", "completed_at"):
        text = job.get(key)
        if not isinstance(text, str):
            raise ValueError("sampling Actions timestamp is unavailable")
        value = datetime.fromisoformat(text.replace("Z", "+00:00"))
        if value.utcoffset() is None:
            raise ValueError("sampling Actions timestamp has no timezone")
        stamps.append(value)
    created, started, completed = stamps
    wall = round((completed - started).total_seconds() * 1000000)
    queue = (started - created).total_seconds()
    # Actions timestamps have second precision. Charge an upper bound instead
    # of manufacturing exact microsecond job timing from those observations.
    upper = wall + 2000000
    if wall <= 0 or queue < 0 or native_wall_us > upper or upper > reservation_seconds * 1000000:
        raise ValueError("sampling occupancy exceeds its whole-job reservation")
    return {"physical_job_wall_us": wall, "physical_job_wall_upper_us": upper,
            "native_packet_wall_us": native_wall_us, "queue_delay_seconds": queue}


def sampling_authority(environment: dict) -> tuple[Api, dict]:
    """Re-query GitHub records; the existing native admission owns policy."""
    import subprocess
    import tempfile
    from pathlib import Path
    import authorize as direct_authorize
    repository = environment.get("BQ_REPOSITORY", "")
    head = environment.get("BQ_HEAD_COMMIT", "")
    request_id = environment.get("BQ_REQUEST_RUN_ID", "")
    run_id = environment.get("BQ_RUN_ID", "")
    if not direct_authorize.REPOSITORY.fullmatch(repository) or not SHA.fullmatch(head) or \
            any(not DECIMAL.fullmatch(value) for value in (request_id, run_id)) or \
            environment.get("BQ_RUN_ATTEMPT") != "1" or environment.get("BQ_REQUEST_ATTEMPT") != "1" or \
            environment.get("GITHUB_RUN_ID") != run_id or environment.get("GITHUB_RUN_ATTEMPT") != "1" or \
            environment.get("GITHUB_REPOSITORY") != repository or not environment.get("GH_TOKEN"):
        raise ValueError("sampling publication lacks exact trusted workflow inputs")
    api = Api(repository, environment["GH_TOKEN"])
    execution = api.request(f"/actions/runs/{run_id}")
    if not isinstance(execution, dict) or str(execution.get("id")) != run_id or \
            execution.get("run_attempt") != 1 or execution.get("path") != BENCH_WORKFLOW or \
            execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or \
            not isinstance(execution.get("repository"), dict) or execution["repository"].get("full_name") != repository or \
            direct_authorize.full_name(execution.get("head_repository")) != repository or \
            direct_authorize.identity(execution.get("actor")) != direct_authorize.MAINTAINER or \
            direct_authorize.identity(execution.get("triggering_actor")) != direct_authorize.MAINTAINER or \
            execution.get("head_sha") != environment.get("GITHUB_SHA") or \
            execution.get("display_title") != f"9700X request {request_id}.1 head {head}":
        raise ValueError("sampling executor workflow provenance is unavailable")
    request = api.request(f"/actions/runs/{request_id}")
    pulls = api.request(f"/commits/{head}/pulls?per_page=100")
    problems, unused_base = direct_authorize.verify(repository, int(request_id), head, request, pulls)
    if problems:
        raise ValueError("sampling request ownership failed: " + ", ".join(problems))
    commit = api.request(f"/commits/{head}")
    parents = commit.get("parents") if isinstance(commit, dict) else None
    if not isinstance(parents, list) or not 1 <= len(parents) <= 2 or any(
            not isinstance(parent, dict) or not SHA.fullmatch(str(parent.get("sha", ""))) for parent in parents):
        raise ValueError("sampling request has an unsupported Git parent inventory")
    compared = [api.request(f"/compare/{parent['sha']}...{head}") for parent in parents]
    marker = direct_authorize.sampling_content(repository, direct_authorize.COMPARE_REQUEST, head, environment["GH_TOKEN"])
    selected = direct_authorize.sampling_fresh_selector(marker, compared)
    if selected is None:
        raise ValueError("sampling selector is not fresh against every Git parent")
    pull = next(row for row in pulls if isinstance(row, dict) and row.get("state") == "open" and
                isinstance(row.get("head"), dict) and row["head"].get("sha") == head)
    # The fixed trusted build driver consumes only re-queried records. Artifact
    # paths, scripts and executables can never become a process command here.
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="sampling-publication-") as temporary:
        directory = Path(temporary) / "admission"
        if not direct_authorize.sampling_data(repository, environment["GH_TOKEN"], request, pull, head, "1",
                                             marker, compared, directory):
            raise ValueError("sampling native admission data is unavailable")
        output = directory / "admitted.env"
        command = [str(root / "build.sh"), "compiler_profile_qualification", "--admit",
                   "--allowlist", str(directory / "allowlist.tsv"),
                   "--request", str(directory / "request.txt"), "--facts", str(directory / "facts.tsv"),
                   "--history", str(directory / "history.tsv"), "--freeze", str(directory / "freeze.tsv"),
                   "--parent-freeze", str(directory / "parent-freeze.tsv"),
                   "--output", str(output)]
        child_environment = {key: value for key, value in environment.items() if key not in ("GH_TOKEN", "GITHUB_TOKEN")}
        try:
            result = subprocess.run(command, cwd=root, env=child_environment, timeout=120,
                                    check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired as error:
            raise ValueError("trusted native admission exceeded its bounded hosted pass") from error
        if result.returncode != 0 or not output.is_file() or output.stat().st_size > 16384:
            raise ValueError("trusted native sampling admission refused publication")
        admitted = {}
        for line in output.read_text(encoding="ascii").splitlines():
            key, separator, value = line.partition("=")
            if not separator or not re.fullmatch(r"sampling_[a-z][a-z0-9_]*", key) or key in admitted or not value or \
                    any(ord(c) < 32 or ord(c) > 126 for c in value):
                raise ValueError("native sampling admission output is ambiguous")
            admitted[key] = value
        required = {
            "sampling_phase": "BQ_SAMPLING_PHASE", "sampling_packet": "BQ_SAMPLING_PACKET",
            "sampling_family": "BQ_SAMPLING_FAMILY", "sampling_freeze_revision": "BQ_SAMPLING_FREEZE_REVISION",
            "sampling_freeze_sha256": "BQ_SAMPLING_FREEZE_SHA256",
            "sampling_campaign_parent": "BQ_SAMPLING_CAMPAIGN_PARENT",
            "sampling_parent_freeze_revision": "BQ_SAMPLING_PARENT_FREEZE_REVISION",
            "sampling_protocol_sha256": "BQ_SAMPLING_PROTOCOL_SHA256", "sampling_base": "BQ_SAMPLING_BASE",
            "sampling_base_tree": "BQ_SAMPLING_BASE_TREE", "sampling_candidate_revision": "BQ_SAMPLING_CANDIDATE_REVISION",
            "sampling_trusted_revision": "BQ_SAMPLING_TRUSTED_REVISION"}
        if admitted.get("sampling_admitted") != "true" or any(
                key not in admitted or not environment.get(variable) or admitted[key] != environment[variable]
                for key, variable in required.items()):
            raise ValueError("publication identity contradicts the freshly repeated native admission")
        freeze_bytes = (directory / "freeze.tsv").read_bytes()
        if hashlib.sha256(freeze_bytes).hexdigest() != admitted["sampling_freeze_sha256"]:
            raise ValueError("committed freeze differs from native admission digest")
        authority = {"admitted": admitted, "freeze": sampling_tsv(freeze_bytes), "freeze_bytes": freeze_bytes,
                     "parent_freeze": sampling_tsv((directory / "parent-freeze.tsv").read_bytes()) if
                         (directory / "parent-freeze.tsv").stat().st_size else {},
                     "acquisition_plan": sampling_tsv((directory / "acquisition-plan.tsv").read_bytes()),
                     "acquisition_plan_bytes": (directory / "acquisition-plan.tsv").read_bytes(),
                     "facts": sampling_tsv((directory / "facts.tsv").read_bytes()),
                     "history": sampling_tsv((directory / "history.tsv").read_bytes(), True),
                     "request_line": selected[0], "request": request, "executor": execution,
                     "repository": repository, "head": head, "request_id": request_id, "run_id": run_id}
    return api, authority


def sampling_check_marker(authority: dict) -> str:
    admitted = authority["admitted"]
    return ("buster-main-sampling-v1:" + admitted["sampling_freeze_sha256"] + ":" +
            admitted["sampling_phase"] + ":" + admitted["sampling_packet"] + ":" +
            authority["request_id"] + ":" + authority["run_id"] + ":1")


def sampling_owned(row: object, authority: dict) -> bool:
    from compiler_github import GITHUB_ACTIONS_APP_ID
    return isinstance(row, dict) and type(row.get("id")) is int and row["id"] > 0 and \
        row.get("name") == SAMPLING_CHECK_NAME and row.get("head_sha") == authority["head"] and \
        row.get("external_id") == sampling_check_marker(authority) and isinstance(row.get("app"), dict) and \
        row["app"].get("id") == GITHUB_ACTIONS_APP_ID and row.get("status") in ("queued", "in_progress", "completed")


def sampling_checks(api: Api, authority: dict) -> list[dict]:
    from compiler_github import GITHUB_ACTIONS_APP_ID
    query = urllib.parse.urlencode({"check_name": SAMPLING_CHECK_NAME, "filter": "all", "app_id": GITHUB_ACTIONS_APP_ID})
    rows = api.pages(f"/commits/{authority['head']}/check-runs?{query}", "check_runs")
    owned = [row for row in rows if sampling_owned(row, authority)]
    if len(owned) > 1:
        raise ValueError("sampling attempt has duplicate owned checks")
    return owned


def sampling_write(api: Api, authority: dict, fields: dict) -> dict:
    from compiler_github import write_check
    rows = sampling_checks(api, authority)
    if rows:
        return write_check(api, rows[0], fields)
    body = dict(fields, name=SAMPLING_CHECK_NAME, head_sha=authority["head"],
                external_id=sampling_check_marker(authority))
    try:
        written = write_check(api, None, body)
        if sampling_owned(written, authority):
            return written
    except (urllib.error.URLError, TimeoutError, ValueError):
        pass
    # A lost POST can become visible later. One lookup may resolve ownership;
    # an empty lookup does not prove absence and can never authorize another POST.
    rows = sampling_checks(api, authority)
    if len(rows) != 1:
        raise ValueError("sampling check creation is ambiguous; no duplicate write or host work authorized")
    return rows[0]


def sampling_queue(environment: dict) -> int:
    api, authority = sampling_authority(environment)
    admitted = authority["admitted"]
    summary = ("Unqualified sampling research; routine profile remains disabled.\n\n"
               "Lifecycle protocol: sampling-terminal-native-v1.\n" +
               f"Phase {admitted['sampling_phase']}, packet {admitted['sampling_packet']}; "
               f"whole physical-job reservation {admitted['sampling_reservation_seconds']} seconds.\n"
               "Native Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\n" +
               f"Request run {authority['request_id']} attempt 1: {run_url(authority['repository'], authority['request_id'], '1')}\n" +
               f"Workflow run {authority['run_id']} attempt 1: {run_url(authority['repository'], authority['run_id'], '1')}")
    row = sampling_write(api, authority, {"status": "queued",
                         "details_url": run_url(authority["repository"], authority["run_id"], "1"),
                         "output": {"title": "Queued unqualified sampling research", "summary": summary}})
    if row.get("status") != "queued":
        raise ValueError("sampling queue is already running or terminal; no new physical assignment authorized")
    print(f"COMPILER_SAMPLING_QUEUED check={row.get('id')} state={row.get('status')} qualification=unqualified")
    return 0


def campaign_artifact_identity(payload: bytes, row: dict, files: dict[str, bytes], retain: bool) -> dict:
    """Retain immutable ZIP and raw-member identities only after API-byte agreement."""
    if type(retain) is not bool:
        raise ValueError("archive identity selection is not boolean")
    if not retain:
        return row
    digest = hashlib.sha256(payload).hexdigest()
    if row.get("digest") != "sha256:" + digest or type(row.get("size_in_bytes")) is not int or \
            row["size_in_bytes"] != len(payload):
        raise ValueError("campaign API artifact digest or byte length differs from the downloaded ZIP")
    manifest = b"".join((name + "\t" + hashlib.sha256(raw).hexdigest() + "\t" + str(len(raw)) + "\n").encode("ascii")
                        for name, raw in sorted(files.items()))
    if len(manifest) > ANALYZER_MEMBER_LIMIT:
        raise ValueError("campaign raw member manifest exceeds its bound")
    return dict(row, verified_zip_sha256=digest, verified_zip_bytes=len(payload),
                verified_member_manifest_sha256=hashlib.sha256(manifest).hexdigest(),
                verified_member_count=len(files), verified_member_manifest=manifest)


def sampling_read_artifact(api: Api, authority: dict, *, retain_archive_identity: bool = False) -> tuple[dict[str, bytes], dict]:
    name = SAMPLING_ARTIFACT_PREFIX + authority["head"] + "-1"
    listing = api.request(f"/actions/runs/{authority['run_id']}/artifacts?" +
                          urllib.parse.urlencode({"name": name, "per_page": 10}))
    rows = listing.get("artifacts") if isinstance(listing, dict) else None
    if not isinstance(rows, list) or len(rows) >= 10:
        raise ValueError("sampling artifact inventory is unavailable or capped")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == name]
    if len(matches) != 1:
        raise ValueError("sampling attempt has no unique evidence artifact")
    row = matches[0]
    origin = row.get("workflow_run")
    if type(row.get("id")) is not int or row["id"] <= 0 or row.get("expired") is not False or \
            type(row.get("size_in_bytes")) is not int or not 0 < row["size_in_bytes"] <= ARTIFACT_LIMIT or \
            not isinstance(origin, dict) or str(origin.get("id")) != authority["run_id"] or \
            origin.get("head_sha") != authority["executor"].get("head_sha"):
        raise ValueError("sampling artifact is expired, oversized or belongs to another executor")
    # Construct the authenticated repository endpoint; never accept a download
    # host or executable path supplied by an artifact or request.
    payload = api.download(api.prefix + f"/actions/artifacts/{row['id']}/zip")
    # A packet retains about 3500 bounded data members (corpus logs and per-pair
    # records); compiled outputs are pruned on the host and refused here.
    files = sampling_archive(payload, member_limit=MAIN_ARCHIVE_FILE_LIMIT, require_nonexecutable=True)
    return files, campaign_artifact_identity(payload, row, files, retain_archive_identity)


def sampling_json(files: dict[str, bytes], name: str, object_only: bool = True, *, max_bytes: int = ANALYZER_MEMBER_LIMIT) -> object:
    """Read bounded JSON data, rejecting duplicate keys and non-finite values."""
    raw = files.get(name)
    if type(max_bytes) is not int or not 0 < max_bytes <= 64 * 1024 * 1024 or not isinstance(raw, bytes) or not 0 < len(raw) <= max_bytes:
        raise ValueError("sampling JSON missing or oversized: " + name)
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate sampling JSON key")
            result[key] = value
        return result
    def nonfinite(unused):
        raise ValueError("non-finite sampling JSON number")
    def finite(value):
        import math
        result = float(value)
        if not math.isfinite(result):
            raise ValueError("non-finite sampling JSON number")
        return result
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique, parse_constant=nonfinite, parse_float=finite)
    if object_only and not isinstance(value, dict):
        raise ValueError("sampling JSON is not an object: " + name)
    return value


def sampling_integer(value: object, positive: bool = False) -> int:
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]{0,19}", value):
        raise ValueError("sampling integer is unavailable or noncanonical")
    result = int(value)
    if result > (1 << 64) - 1 or (positive and result == 0):
        raise ValueError("sampling integer is outside its bound")
    return result


def sampling_plan(authority: dict) -> dict:
    """Use only the acquisition bytes authenticated by native phase admission."""
    from sampling_qualification_receipt import schedule
    admitted, frozen, parent = authority["admitted"], authority["freeze"], authority["parent_freeze"]
    plan = authority["acquisition_plan"]
    raw = authority["acquisition_plan_bytes"]
    digest = hashlib.sha256(raw).hexdigest()
    phase = admitted["sampling_phase"]
    if phase == "acquire":
        expected_hash, revision = admitted["sampling_freeze_sha256"], admitted["sampling_freeze_revision"]
    elif phase == "pilot":
        expected_hash, revision = frozen["campaign_parent"], frozen["campaign_parent_revision"]
    else:
        expected_hash, revision = parent["campaign_parent"], parent["campaign_parent_revision"]
    if digest != expected_hash or plan.get("schema") != "buster-main-sampling-acquisition-v1" or \
            plan.get("phase") != "acquire" or plan.get("trusted_revision") != admitted["sampling_trusted_revision"] or \
            plan.get("protocol_sha256") != admitted["sampling_protocol_sha256"]:
        raise ValueError("sampling acquisition bytes do not bind the admitted phase ancestry")
    packet = sampling_integer(admitted["sampling_packet"])
    planned = schedule(phase, packet)
    if not planned or admitted.get("sampling_family") != planned["family"] or \
            sampling_integer(admitted.get("sampling_reservation_seconds"), True) != planned["reservation_seconds"]:
        raise ValueError("sampling native admission contradicts the deterministic reservation")
    return {"record": plan, "sha256": digest, "revision": revision, "phase": phase, "packet": packet,
            "schedule": planned, "output": plan["store_root"] + "/" + digest + "/prepared"}


def sampling_prepared(api: Api, authority: dict, files: dict[str, bytes], context: dict) -> dict:
    """Replay the actual producer inventory through the existing data adapter."""
    from compiler_preparation import validate_prepared
    plan = context["record"]
    pins = {}
    for role, revision in (("base", plan["base"]), ("head", plan["ab1_revision"]),
                           ("secondary_head", plan["ab2_revision"])):
        row = api.request("/git/commits/" + revision)
        tree = row.get("tree") if isinstance(row, dict) else None
        if not isinstance(row, dict) or row.get("sha") != revision or not isinstance(tree, dict) or \
                not SHA.fullmatch(str(tree.get("sha", ""))):
            raise ValueError("sampling immutable source tree is unavailable")
        pins[role] = revision
        pins[{"base": "base_tree", "head": "head_tree", "secondary_head": "secondary_tree"}[role]] = tree["sha"]
    if pins["base_tree"] != plan["base_tree"]:
        raise ValueError("sampling acquisition baseline tree differs from GitHub's immutable object")
    expected = dict(pins, root=plan["source_root"], output=context["output"], policy="snapshot-v1", arm_count=3)
    immediate = {}
    for name, raw in files.items():
        if name.startswith("prepared/"):
            basename = name[len("prepared/"):]
            if "/" in basename or not re.fullmatch(r"[A-Za-z0-9_.-]{1,160}", basename):
                raise ValueError("sampling prepared export has a nested or malformed member")
            immediate[basename] = raw
    prepared = sampling_json(files, "prepared/prepared.json")
    closure = {op: sampling_json(files, "prepared/closure-" + op + ".json") for op in ("snapshot", "restore", "verify")}
    manifests = {op: immediate.get("closure-" + op + ".json.manifest.tsv") for op in closure}
    bundle = {"manifest": immediate.get("prepared.manifest.tsv"), "workload": immediate.get("prepared.workload.tsv"),
              "ledger": immediate.get("phases.tsv"), "files": immediate, "closure": closure, "closure_manifests": manifests}
    problems = validate_prepared(expected, prepared, bundle)
    if problems:
        raise ValueError("sampling acquisition preparation replay failed: " + "; ".join(problems[:8]))
    for role in ("baseline", "candidate", "candidate2"):
        value = prepared.get(role + "_bytes")
        if type(value) is not int or not 0 < value <= 536870912:
            raise ValueError("sampling acquired compiler size is unavailable or exceeds its declared bound")
    if immediate.get("closure.manifest.tsv") != bundle["manifest"]:
        raise ValueError("sampling persistent closure export differs from the actual preparation inventory")
    return {"record": prepared, "bytes": immediate["prepared.json"], "sha256": hashlib.sha256(immediate["prepared.json"]).hexdigest(),
            "expected": expected, "bundle": bundle, "files": immediate}


def sampling_host(authority: dict, files: dict[str, bytes], context: dict) -> dict:
    host = sampling_json(files, "host.json")
    wanted = {"schema": "buster-main-sampling-host-v1", "state": "complete",
              "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor", "observed_from": "/proc/cpuinfo",
              "request_head": authority["head"], "run_id": authority["run_id"], "run_attempt": "1",
              "request_run_id": authority["request_id"],
              "measurement_trusted_revision": context["record"]["trusted_revision"],
              "policy_trusted_revision": authority["executor"]["head_sha"],
              "freeze_sha256": authority["admitted"]["sampling_freeze_sha256"],
              "acquisition_campaign": context["sha256"], "protocol_sha256": context["record"]["protocol_sha256"]}
    if set(host) != set(wanted) | {"logical_processor_records"} or \
            any(type(host.get(key)) is not type(value) or host.get(key) != value for key, value in wanted.items()) or \
            type(host.get("logical_processor_records")) is not int or not 0 < host["logical_processor_records"] <= 4096:
        raise ValueError("sampling actual CPU observation or executor/source bindings are incomplete")
    return host


def sampling_host_job(api: Api, authority: dict) -> dict:
    rows = api.pages(f"/actions/runs/{authority['run_id']}/attempts/1/jobs", "jobs")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == SAMPLING_HOST_JOB]
    if len(matches) != 1:
        raise ValueError("sampling physical attempt has no unique platform job")
    job = matches[0]
    if type(job.get("id")) is not int or job["id"] <= 0 or \
            str(job.get("run_id")) != authority["run_id"] or job.get("run_attempt") != 1 or \
            type(job.get("runner_id")) is not int or job["runner_id"] <= 0 or \
            not isinstance(job.get("runner_name"), str) or not job["runner_name"] or \
            not isinstance(job.get("labels"), list) or not job["labels"] or \
            any(not isinstance(label, str) or not label for label in job["labels"]):
        raise ValueError("sampling physical platform runner provenance is unavailable")
    return job


def sampling_phase_proofs(files: dict[str, bytes], context: dict, prepared: dict) -> tuple[dict, dict]:
    from compiler_receipt import validate_closure
    owner = sampling_tsv(files.get("owner.tsv"))
    owner_keys = {"schema", "physical_packet_wall_us", "process_state", "timed_out", "cleanup_failed", "within_reservation", "cancelled"}
    if set(owner) != owner_keys or owner.get("schema") != "buster-main-sampling-owner-v1" or \
            owner.get("process_state") != "complete" or owner.get("within_reservation") != "true" or \
            any(owner.get(key) != "0" for key in ("timed_out", "cleanup_failed", "cancelled")):
        raise ValueError("sampling outer worker ownership is failed, cancelled or incomplete")
    owner_wall = sampling_integer(owner["physical_packet_wall_us"], True)
    terminal = sampling_tsv(files.get("packet.tsv"))
    terminal_keys = {"physical_packet_wall_us", "prep_us", "captured_input_files_unchanged", "within_reservation",
                     "process_state", "qualification_state", "queue_delay"}
    if set(terminal) != terminal_keys or terminal.get("process_state") != "complete" or \
            terminal.get("captured_input_files_unchanged") != "true" or terminal.get("within_reservation") != "true" or \
            terminal.get("qualification_state") != "unvalidated" or terminal.get("queue_delay") != "unavailable":
        raise ValueError("sampling native packet is incomplete or claims unsupported qualification")
    packet_wall = sampling_integer(terminal["physical_packet_wall_us"], True)
    prep = sampling_integer(terminal["prep_us"])
    if prep > packet_wall or packet_wall > owner_wall or owner_wall > context["schedule"]["reservation_seconds"] * 1000000:
        raise ValueError("sampling preparation, packet and worker wall accounting contradict")
    phases = sampling_tsv(files.get("controller.tsv"), True)
    wanted = ["trusted-harness-pin"]
    if context["phase"] == "acquire":
        wanted += ["clone-sources", "fetch-pinned-arms", "baseline-tree", "ab1-tree", "ab2-tree",
                   "primary-arm-checkout", "acquire-prepared-closure"]
    else:
        wanted += ["full-default-corpus"]
    columns = {"stage", "phase", "wall_us", "exit_status", "timed_out", "cleanup_failed", "cancelled", "state"}
    if len(phases) != len(wanted):
        raise ValueError("sampling controller phase ledger is missing or changed")
    proofs = {"owner-supervision.tsv"}
    proof_bounds = {"owner-supervision.tsv": owner_wall}
    phase_wall = 0
    for index, (row, name) in enumerate(zip(phases, wanted), 1):
        if set(row) != columns or row.get("stage") != str(index) or row.get("phase") != name or row.get("state") != "complete" or \
                any(row.get(key) != "0" for key in ("exit_status", "timed_out", "cleanup_failed", "cancelled")):
            raise ValueError("sampling controller phase is failed, undeclared or not run")
        phase_wall += sampling_integer(row["wall_us"], True)
        stem = f"controller-{index}-{name}"
        proofs.add(stem + "-supervision.tsv")
        proof_bounds[stem + "-supervision.tsv"] = sampling_integer(row["wall_us"], True)
        for stream in ("stdout", "stderr"):
            raw = files.get(stem + "." + stream + ".log")
            if not isinstance(raw, bytes) or len(raw) > 1024 * 1024:
                raise ValueError("sampling controller bounded phase output is missing")
    if phase_wall > packet_wall:
        raise ValueError("sampling controller phases exceed native packet occupancy")
    if context["phase"] != "acquire":
        verify_indexes = [99, *range(len(context["schedule"]["slots"]))]
        for index in verify_indexes:
            for side in ("before", "after"):
                stem = f"closure-{index}-{side}"
                record = sampling_json(files, stem + ".json")
                raw = files.get(stem + ".json.manifest.tsv")
                closure = dict(prepared["bundle"]["closure"], verify=record)
                manifests = dict(prepared["bundle"]["closure_manifests"], verify=raw)
                replay = {"preparation_policy": "snapshot-v1", "identity": prepared["expected"],
                          "binaries": {"baseline": {"sha256": prepared["record"]["baseline_sha256"]}},
                          "closure": {"policy": "snapshot-v1", "fallback": None, **closure}}
                issues = validate_closure(replay, manifests, expected_policy="snapshot-v1")
                if raw != prepared["bundle"]["manifest"] or issues:
                    raise ValueError("sampling per-phase native closure changed: " + "; ".join(issues[:3]))
                proofs.add(stem + "-supervision.tsv")
                proof_bounds[stem + "-supervision.tsv"] = packet_wall
        raw_attempts = sampling_tsv(files.get("attempts.tsv"), True)
        if len(raw_attempts) != len(context["schedule"]["slots"]):
            raise ValueError("sampling measured process ledger is missing")
        for index in range(len(context["schedule"]["slots"])):
            proofs.add(f"trial-{index}-supervision.tsv")
            proof_bounds[f"trial-{index}-supervision.tsv"] = sampling_integer(raw_attempts[index].get("wall_us"), True)
            for stream in ("stdout", "stderr"):
                raw = files.get(f"trial-{index}.{stream}.log")
                if not isinstance(raw, bytes) or len(raw) > 1024 * 1024:
                    raise ValueError("sampling measured phase bounded output is missing")
    observed = {name for name in files if name.endswith("-supervision.tsv")}
    if observed != proofs:
        raise ValueError("sampling complete native supervision proof set is missing or undeclared")
    for name in proofs:
        proof = sampling_supervision(files[name])
        if sampling_integer(proof["wall_us"], True) > proof_bounds[name]:
            raise ValueError("sampling supervision wall exceeds its recorded phase or owner duration")
    return owner, terminal


def sampling_acquisition(authority: dict, files: dict[str, bytes], context: dict, prepared: dict) -> dict:
    from compiler_preparation import absolute
    row = sampling_tsv(files.get("acquisition.tsv"))
    plan = context["record"]
    wanted = {"schema": "buster-main-sampling-acquisition-receipt-v1", "phase": "acquire", "packet": "0",
              "measurement": "false", "campaign": context["sha256"], "base": plan["base"], "base_tree": plan["base_tree"],
              "request_head": plan["request_head"], "trusted_revision": plan["trusted_revision"],
              "ab1_revision": plan["ab1_revision"], "ab2_revision": plan["ab2_revision"],
              "protocol_sha256": plan["protocol_sha256"], "prepared_sha256": prepared["sha256"],
              "reservation_seconds": "1800", "process_state": "complete", "qualification_state": "unvalidated"}
    digests = {"lab_sha256", "python_sha256", "driver_sha256"}
    if set(row) != set(wanted) | digests | {"python_path", "trusted_root"} or not absolute(row.get("python_path")) or \
            len(row["python_path"]) > 256 or any(ord(char) < 33 or ord(char) > 126 or char == "\\" for char in row["python_path"]) or \
            not absolute(row.get("trusted_root")) or len(row["trusted_root"]) > 256 or \
            any(ord(char) < 33 or ord(char) > 126 or char == "\\" for char in row["trusted_root"]) or \
            any(row.get(key) != value for key, value in wanted.items()) or \
            any(not re.fullmatch(r"[a-f0-9]{64}", row.get(key, "")) for key in digests) or \
            any(name == "identity.tsv" or name == "attempts.tsv" or name.startswith("trial-") or name.startswith("throughput/")
                for name in files):
        raise ValueError("sampling acquisition identity is incomplete or contains measured outcomes")
    return row


def sampling_source_hashes(api: Api, authority: dict, context: dict, acquired: dict) -> None:
    import base64
    from pathlib import Path
    from sampling_qualification_receipt import _lab
    actual_lab = Path(_lab.__file__).read_bytes()
    if not 0 < len(actual_lab) <= 1024 * 1024 or hashlib.sha256(actual_lab).hexdigest() != acquired["lab_sha256"]:
        raise ValueError("sampling statistical replay module differs from the acquired trusted lab source")
    revision = context["record"]["trusted_revision"]
    for path, key in (("tools/uarch_lab.py", "lab_sha256"),
                      ("docs/compiler-main-sampling-qualification-v1.json", "protocol_sha256")):
        record = api.request("/contents/" + path + "?ref=" + revision)
        if not isinstance(record, dict) or record.get("type") != "file" or record.get("encoding") != "base64" or \
                type(record.get("size")) is not int or not 0 < record["size"] <= 1024 * 1024 or \
                not isinstance(record.get("content"), str):
            raise ValueError("sampling fixed trusted source is missing or exceeds its bound")
        raw = base64.b64decode(record["content"].replace("\n", ""), validate=True)
        if len(raw) != record["size"] or hashlib.sha256(raw).hexdigest() != acquired[key]:
            raise ValueError("sampling fixed trusted source differs from acquisition " + key)


def sampling_review_attempt(api: Api, run_id: str) -> dict:
    """Select original research attempt one while refusing later executor reruns."""
    if not isinstance(run_id, str) or not DECIMAL.fullmatch(run_id):
        raise ValueError("historical sampling run identifier is invalid")
    original = api.request("/actions/runs/" + run_id + "/attempts/1")
    latest = api.request("/actions/runs/" + run_id)
    if not isinstance(original, dict) or type(original.get("id")) is not int or str(original["id"]) != run_id or \
            type(original.get("run_attempt")) is not int or original["run_attempt"] != 1 or \
            not isinstance(latest, dict) or type(latest.get("id")) is not int or str(latest["id"]) != run_id or \
            type(latest.get("run_attempt")) is not int or latest["run_attempt"] != 1:
        raise ValueError("historical sampling original attempt is unavailable or was rerun")
    return original


def campaign_reviewed(authority: dict, prefix: str) -> bool:
    """Keep historical data review separate from every live admission vocabulary."""
    flag = authority.get("historical_review", False)
    if type(flag) is not bool:
        raise ValueError("historical campaign review flag is not boolean")
    proof = authority.get("admitted")
    if flag and (not isinstance(proof, dict) or proof.get(prefix + "_historical_valid") != "true" or
                 proof.get(prefix + "_historical_execution_authority") != "false" or
                 proof.get(prefix + "_historical_qualification") != "unqualified" or
                 prefix + "_admitted" in proof):
        raise ValueError("historical campaign lacks its distinct nonexecution native review")
    return flag


def sampling_reviewed(authority: dict) -> bool:
    return campaign_reviewed(authority, "sampling")



def sampling_prior_acquisition(api: Api, authority: dict, context: dict) -> tuple[dict, dict]:
    """Re-read the previously authenticated acquisition, never current-copy authority."""
    history = authority["history"]
    rows = [row for row in history if row.get("phase") == "acquire" and row.get("packet") == "0"]
    if len(rows) != 1 or rows[0].get("campaign") != context["sha256"] or rows[0].get("freeze_revision") != context["revision"] or \
            rows[0].get("state") != "complete" or rows[0].get("request_run_attempt") != "1" or rows[0].get("executor_run_attempt") != "1":
        raise ValueError("sampling lacks its unique authenticated complete acquisition")
    previous = rows[0]
    historical = sampling_reviewed(authority)
    request = sampling_review_attempt(api, previous["request_run_id"]) if historical else api.request("/actions/runs/" + previous["request_run_id"])
    execution = sampling_review_attempt(api, previous["executor_run_id"]) if historical else api.request("/actions/runs/" + previous["executor_run_id"])
    if not isinstance(request, dict) or not isinstance(execution, dict) or \
            str(request.get("id")) != previous["request_run_id"] or request.get("run_attempt") != 1 or \
            str(execution.get("id")) != previous["executor_run_id"] or execution.get("run_attempt") != 1 or \
            execution.get("status") != "completed" or execution.get("conclusion") != "success" or \
            not SHA.fullmatch(str(request.get("head_sha", ""))) or execution.get("path") != BENCH_WORKFLOW or \
            execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or \
            not isinstance(execution.get("repository"), dict) or execution["repository"].get("full_name") != authority["repository"] or \
            execution.get("display_title") != f"9700X request {previous['request_run_id']}.1 head {request['head_sha']}":
        raise ValueError("sampling acquisition executor provenance is unavailable")
    if historical:
        old = authority.get("historical_acquisition")
        if not isinstance(old, dict) or not sampling_reviewed(old) or old.get("history") != [] or \
                old.get("request") != request or old.get("executor") != execution or \
                old.get("request_id") != previous["request_run_id"] or old.get("run_id") != previous["executor_run_id"] or \
                old.get("repository") != authority["repository"] or old.get("head") != request["head_sha"]:
            raise ValueError("historical sampling acquisition does not have original independently reviewed authority")
        original_context = sampling_plan(old)
        if original_context["phase"] != "acquire" or original_context["packet"] != 0 or \
                original_context["sha256"] != context["sha256"] or original_context["revision"] != context["revision"] or \
                old.get("acquisition_plan_bytes") != authority["acquisition_plan_bytes"]:
            raise ValueError("historical sampling original acquisition ancestry changed")
    else:
        old = dict(authority, head=request["head_sha"], request_id=previous["request_run_id"],
                   run_id=previous["executor_run_id"], request=request, executor=execution,
                   admitted=dict(authority["admitted"], sampling_phase="acquire", sampling_packet="0",
                                 sampling_family="acquire", sampling_freeze_sha256=context["sha256"],
                                 sampling_freeze_revision=context["revision"], sampling_reservation_seconds="1800"))
    old_context = dict(context, phase="acquire", packet=0, schedule={"family": "acquire", "reservation_seconds": 1800, "slots": []})
    files, unused_artifact = sampling_read_artifact(api, old)
    if historical:
        old = bind_historical_transport(old, files, "sampling")
    prepared = sampling_prepared(api, old, files, old_context)
    host = sampling_host(old, files, old_context)
    owner, unused_terminal = sampling_phase_proofs(files, old_context, prepared)
    old_job = sampling_host_job(api, old)
    sampling_job_accounting(old_job, sampling_integer(owner["physical_packet_wall_us"], True), 1800)
    physical_clock_binding(old, files, old_job, "sampling", SAMPLING_HOST_JOB)
    acquired = sampling_acquisition(old, files, old_context, prepared)
    sampling_source_hashes(api, old, old_context, acquired)
    return prepared, acquired


def sampling_history(api: Api, authority: dict, context: dict, occupancy: dict, state: str) -> list[dict]:
    """Retain every admitted attempted row and charge platform job occupancy."""
    from sampling_qualification_receipt import schedule
    result = []
    for source in authority["history"]:
        phase, packet = source["phase"], sampling_integer(source["packet"])
        plan = schedule(phase, packet)
        if not plan or source.get("state") != "complete" or source.get("request_run_attempt") != "1" or source.get("executor_run_attempt") != "1":
            raise ValueError("sampling previous attempted history is incomplete or forbidden")
        run_id = source["executor_run_id"]
        historical = sampling_reviewed(authority)
        execution = sampling_review_attempt(api, run_id) if historical else api.request("/actions/runs/" + run_id)
        request = sampling_review_attempt(api, source["request_run_id"]) if historical else api.request("/actions/runs/" + source["request_run_id"])
        if not isinstance(execution, dict) or str(execution.get("id")) != run_id or execution.get("run_attempt") != 1 or \
                execution.get("status") != "completed" or execution.get("conclusion") != "success" or \
                execution.get("head_branch") != "main" or not isinstance(execution.get("repository"), dict) or \
                execution["repository"].get("full_name") != authority["repository"] or \
                execution.get("path") != BENCH_WORKFLOW or execution.get("event") != "workflow_run" or \
                not isinstance(request, dict) or str(request.get("id")) != source["request_run_id"] or request.get("run_attempt") != 1 or \
                execution.get("display_title") != f"9700X request {source['request_run_id']}.1 head {request.get('head_sha')}":
            raise ValueError("sampling prior platform attempt identity changed")
        prior = dict(authority, run_id=run_id, executor=execution)
        job = sampling_host_job(api, prior)
        # Native admission history stores the API second-resolution upper bound.
        expected_upper = sampling_integer(source["physical_wall_us"], True)
        account = sampling_job_accounting(job, 1, plan["reservation_seconds"])
        if account["physical_job_wall_upper_us"] != expected_upper:
            raise ValueError("sampling authenticated attempt occupancy changed during publication")
        result.append({"phase": phase, "packet": packet, "request_run_id": source["request_run_id"],
                       "run_id": run_id, "run_attempt": "1", "state": source["state"],
                       "reservation_seconds": plan["reservation_seconds"], "actions_job_occupancy_us": expected_upper,
                       "physical_packet_wall_us": None, "campaign": source["campaign"], "freeze_revision": source["freeze_revision"]})
    result.append({"phase": context["phase"], "packet": context["packet"], "request_run_id": authority["request_id"],
                   "run_id": authority["run_id"], "run_attempt": "1", "state": state,
                   "reservation_seconds": context["schedule"]["reservation_seconds"],
                   "actions_job_occupancy_us": occupancy["physical_job_wall_upper_us"],
                   "physical_packet_wall_us": occupancy["native_packet_wall_us"],
                   "campaign": authority["admitted"]["sampling_freeze_sha256"],
                   "freeze_revision": authority["admitted"]["sampling_freeze_revision"]})
    return result

def sampling_trusted(authority: dict, context: dict, prepared: dict, acquired: dict, host: dict,
                     terminal: dict, occupancy: dict, history: list[dict]) -> dict:
    """Construct frozen expectations from committed metadata and API facts."""
    from sampling_qualification_receipt import REQUEST_SELECTORS, _lab
    frozen, admitted, plan = authority["freeze"], authority["admitted"], context["record"]
    family = context["schedule"]["family"]
    candidate_role = "baseline" if family == "aa" else "candidate" if family == "ab1" else "candidate2"
    revision = plan["base"] if family == "aa" else plan["ab1_revision"] if family == "ab1" else plan["ab2_revision"]
    record = prepared["record"]
    wanted = {"prepared_sha256": prepared["sha256"], "closure_sha256": record["snapshot_digest"],
              "baseline_sha256": record["baseline_sha256"], "aa_candidate_sha256": record["baseline_sha256"],
              "ab1_candidate_sha256": record["candidate_sha256"], "ab2_candidate_sha256": record["candidate2_sha256"],
              "baseline_bytes": str(record["baseline_bytes"]), "ab1_candidate_bytes": str(record["candidate_bytes"]),
              "ab2_candidate_bytes": str(record["candidate2_bytes"]), "base": plan["base"], "base_tree": plan["base_tree"],
              "trusted_revision": plan["trusted_revision"], "baseline_revision": plan["base"],
              "aa_candidate_revision": plan["base"], "ab1_revision": plan["ab1_revision"], "ab2_revision": plan["ab2_revision"],
              "protocol_sha256": plan["protocol_sha256"],
              **{key: acquired[key] for key in ("lab_sha256", "python_sha256", "driver_sha256")}}
    if any(frozen.get(key) != value for key, value in wanted.items()):
        raise ValueError("sampling committed freeze contradicts the authenticated acquisition artifacts")
    identity = {"schema": "buster-main-sampling-packet-v1", "phase": context["phase"], "packet": str(context["packet"]),
                "campaign": admitted["sampling_freeze_sha256"], "reservation_seconds": str(context["schedule"]["reservation_seconds"]),
                "family": family, "trials": str(len(context["schedule"]["slots"])), "base": plan["base"],
                "base_tree": plan["base_tree"], "request_head": authority["head"], "baseline_revision": plan["base"],
                "candidate_revision": revision, "baseline_sha256": record["baseline_sha256"],
                "candidate_sha256": record[candidate_role + "_sha256"], "lab_sha256": acquired["lab_sha256"],
                "protocol_sha256": plan["protocol_sha256"], "python_sha256": acquired["python_sha256"],
                "driver_sha256": acquired["driver_sha256"], "closure_sha256": record["snapshot_digest"],
                "freeze_sha256": admitted["sampling_freeze_sha256"], "trusted_revision": plan["trusted_revision"],
                "prepared_sha256": prepared["sha256"], "baseline_bytes": str(record["baseline_bytes"]),
                "candidate_bytes": str(record[candidate_role + "_bytes"]), "cpu": "2", "warmups": "1", "seed": "20261003",
                "floor_percent": "0.5", "fresh_copy": "true", "routine_enabled": "false",
                "evidence_class": "unqualified-sampling-research"}
    binaries = {"baseline": {"sha256": record["baseline_sha256"], "revision": plan["base"],
                              "size_bytes": record["baseline_bytes"], "path": context["output"] + "/bin/ide-base"},
                "candidate": {"sha256": record[candidate_role + "_sha256"], "revision": revision,
                              "size_bytes": record[candidate_role + "_bytes"],
                              "path": context["output"] + "/bin/" + {"baseline": "ide-base", "candidate": "ide-cand", "candidate2": "ide-cand2"}[candidate_role]}}
    workload = {"command": _lab.shell_join(["IDE"] + _lab.DEFAULT_COMPILE + ["-o", "OUT"]),
                "repo_root": plan["source_root"], "perf": "perf", "extra": [], "extra_by_variant": {"a": [], "b": []}}
    request = {"repository": authority["repository"], "actor": authority["facts"]["actor_login"],
               "owner": authority["facts"]["owner_login"], "selector": REQUEST_SELECTORS[context["phase"]],
               "request_run_id": authority["request_id"], "request_head": authority["head"],
               "freeze_revision": admitted["sampling_freeze_revision"], "phase": context["phase"], "packet": context["packet"],
               "campaign": admitted["sampling_freeze_sha256"], "acquisition_campaign": context["sha256"],
               "acquisition_revision": context["revision"],
               "pilot_campaign": frozen["campaign_parent"] if context["phase"] == "confirm" else admitted["sampling_freeze_sha256"],
               "pilot_revision": frozen["campaign_parent_revision"] if context["phase"] == "confirm" else admitted["sampling_freeze_revision"]}
    executor = {"repository": authority["repository"], "request_run_id": authority["request_id"],
                "run_id": authority["run_id"], "run_attempt": "1", "cpu_model": host["cpu_model"],
                "physical_packet_wall_us": sampling_integer(terminal["physical_packet_wall_us"], True),
                "actions_job_occupancy_us": occupancy["physical_job_wall_upper_us"],
                "queue_delay_seconds": occupancy["queue_delay_seconds"]}
    # Current is provisional complete for raw replay. A validator failure
    # rewrites the current history row to invalid before publication.
    return {"authenticated": True, "request": request, "executor": executor, "identity": identity,
            "binaries": binaries, "workload_config": workload, "attempts": history}


def historical_current_transport(authority: dict, kind: str) -> dict[str, bytes]:
    """Rejoin current API/native input bytes without admitting or measuring anything."""
    if kind not in ("sampling", "preparation", "utility"):
        raise ValueError("historical transport kind is not declared")
    names = {"request.txt", "allowlist.tsv", "facts.tsv", "history.tsv"}
    names |= {"freeze.tsv", "parent-freeze.tsv", "acquisition-plan.tsv"} if kind == "sampling" else {"plan.tsv"}
    current = authority.get("raw")
    if not isinstance(current, dict) or set(current) != names or any(
            not isinstance(value, bytes) or len(value) > 1024 * 1024 for value in current.values()):
        raise ValueError("historical current native transport is missing or ambiguous")
    records = authority.get("historical_records")
    api_raw = records.get("api") if isinstance(records, dict) else None
    proof = authority.get("native_api_proof")
    admitted = authority.get("admitted")
    if not isinstance(api_raw, bytes) or not 0 < len(api_raw) <= 65536 or not isinstance(proof, dict) or \
            not isinstance(admitted, dict) or any(not isinstance(key, str) or not isinstance(value, str) for key, value in proof.items()) or \
            sampling_tsv(api_raw) != proof or b"".join((key + "\t" + value + "\n").encode("ascii") for key, value in proof.items()) != api_raw or \
            admitted.get(kind + "_historical_api_sha256") != hashlib.sha256(api_raw).hexdigest():
        raise ValueError("historical current API proof is not joined to its native review")
    hashed = {"allowlist.tsv": "allowlist_sha256", "facts.tsv": "facts_sha256", "history.tsv": "history_sha256",
              "freeze.tsv" if kind == "sampling" else "plan.tsv": "freeze_sha256"}
    if kind == "sampling":
        hashed.update({"parent-freeze.tsv": "parent_freeze_sha256", "acquisition-plan.tsv": "acquisition_sha256"})
    if any(proof.get(label) != ("-" if name == "parent-freeze.tsv" and not current[name] else
            hashlib.sha256(current[name]).hexdigest()) for name, label in hashed.items()):
        raise ValueError("historical current transport differs from native-validated API input digests")
    freeze_key = "sampling_freeze_sha256" if kind == "sampling" else kind + "_plan_sha256"
    if proof.get("freeze_sha256") != admitted.get(freeze_key):
        raise ValueError("historical current plan differs from its native reviewed immutable identity")
    if kind == "sampling" and admitted.get("sampling_historical_valid") == "true" and admitted.get("sampling_phase") != "acquire" and \
            proof.get("parent_freeze_sha256") != admitted.get("sampling_campaign_parent"):
        raise ValueError("historical current parent freeze differs from its native review")
    facts = sampling_tsv(current["facts.tsv"])
    if facts != authority.get("facts"):
        raise ValueError("historical current facts differ from native API-proof input")
    marker = facts.get("fresh_parent_0")
    if not isinstance(marker, str) or not marker or marker == "-" or current["request.txt"] != (marker + "\n").encode("ascii") or \
            facts.get("parent_count") not in ("1", "2") or \
            facts.get("fresh_parent_1") != (marker if facts["parent_count"] == "2" else "-"):
        raise ValueError("historical request marker differs from every original fresh-parent observation")
    execution, request = authority.get("executor"), authority.get("request")
    if not isinstance(request, dict) or type(request.get("id")) is not int or request["id"] <= 0 or \
            type(request.get("run_attempt")) is not int or request["run_attempt"] != 1 or \
            str(request["id"]) != authority.get("request_id") or request.get("head_sha") != authority.get("head") or \
            proof.get("request_run_id") != authority.get("request_id") or proof.get("request_run_attempt") != "1" or \
            proof.get("request_head") != authority.get("head"):
        raise ValueError("historical current request contradicts its native API proof")
    if execution is not None and (not isinstance(execution, dict) or type(execution.get("id")) is not int or execution["id"] <= 0 or \
            type(execution.get("run_attempt")) is not int or execution["run_attempt"] != 1 or \
            str(execution["id"]) != authority.get("run_id") or proof.get("executor_run_id") != authority.get("run_id") or \
            proof.get("executor_run_attempt") != "1" or proof.get("executor_head") != execution.get("head_sha") or \
            proof.get("policy_revision") != execution.get("head_sha") or admitted.get(kind + "_policy_revision") != execution.get("head_sha")):
        raise ValueError("historical current executor contradicts its native API proof")
    return current


def historical_transport(authority: dict, files: dict[str, bytes], kind: str) -> dict[str, bytes]:
    """Join retained producer transport to the separate current historical API proof."""
    if kind not in ("sampling", "preparation", "utility"):
        raise ValueError("historical transport kind is not declared")
    if authority.get("historical_review") is not True:
        return authority.get("raw", {}) if kind == "sampling" else authority["raw"]
    if not campaign_reviewed(authority, kind):
        raise ValueError("original transport lacks its distinct historical native review")
    current = historical_current_transport(authority, kind)
    original = authority.get("raw_original")
    if not isinstance(original, dict) or set(original) != set(current) or any(
            not isinstance(value, bytes) or len(value) > 1024 * 1024 for value in original.values()):
        raise ValueError("historical original native transport is missing or ambiguous")
    if any(files.get(name) != original[name] for name in current) or any(
            current[name] != original[name] for name in set(current) - {"facts.tsv"}):
        raise ValueError("original native transport differs from immutable API-selected records")
    current_sha, original_sha = (hashlib.sha256(raw["facts.tsv"]).hexdigest() for raw in (current, original))
    current_facts, original_facts = (sampling_tsv(raw["facts.tsv"]) for raw in (current, original))
    binding = authority.get("historical_original_facts_binding")
    expected = {"historical_original_facts_valid": "true", "current_facts_sha256": current_sha,
                "original_facts_sha256": original_sha, "historical_execution_authority": "false", "qualification": "unqualified"}
    if binding != expected:
        raise ValueError("original retained facts lack their exact nonexecution native binding")
    if original_facts.get("pull_state") != "open" or current_facts.get("pull_state") not in ("open", "closed") or \
            set(original_facts) != set(current_facts) or any(current_facts[key] != original_facts[key] for key in current_facts if key != "pull_state"):
        raise ValueError("original producer facts contradict the retained admission or current immutable API identity")
    return original


def bind_historical_transport(authority: dict, files: dict[str, bytes], kind: str) -> dict:
    """Invoke only the fixed trusted data validator after the original API ZIP selection."""
    import authorize as direct_authorize
    bound = direct_authorize.bind_historical_original_transport(authority, files, kind)
    if not isinstance(bound, dict):
        raise ValueError("original historical transport binder returned no authority data")
    historical_transport(bound, files, kind)
    return bound


def sampling_validate(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    from sampling_qualification_receipt import validate_packet
    if authority.get("historical_review") is True:
        historical_transport(authority, files, "sampling")
    context = sampling_plan(authority)
    prepared = sampling_prepared(api, authority, files, context)
    host = sampling_host(authority, files, context)
    owner, terminal = sampling_phase_proofs(files, context, prepared)
    job = sampling_host_job(api, authority)
    occupancy = sampling_job_accounting(job, sampling_integer(owner["physical_packet_wall_us"], True),
                                        context["schedule"]["reservation_seconds"])
    occupancy["pre_entry_platform_clock"] = physical_clock_binding(authority, files, job, "sampling", SAMPLING_HOST_JOB)
    occupancy["native_owner_wall_us"] = sampling_integer(owner["physical_packet_wall_us"], True)
    occupancy["native_packet_wall_us"] = sampling_integer(terminal["physical_packet_wall_us"], True)
    if context["phase"] == "acquire":
        acquired = sampling_acquisition(authority, files, context, prepared)
        sampling_source_hashes(api, authority, context, acquired)
        if authority["history"]:
            raise ValueError("sampling acquisition has prior campaign attempts")
        result = {"schema": "buster-main-sampling-acquisition-validation-v1", "packet_state": "complete-valid-research",
                  "qualification_state": "unqualified", "evidence_class": "unqualified-sampling-research",
                  "routine_profile_enabled": False, "phase": "acquire", "packet": 0, "family": "acquire",
                  "reservation_seconds": 1800, "measurement": False, "series": [], "problems": []}
        history = sampling_history(api, authority, context, occupancy, "complete")
    else:
        original, acquired = sampling_prior_acquisition(api, authority, context)
        if prepared["files"] != original["files"]:
            raise ValueError("sampling packet preparation metadata differs from the authenticated acquisition")
        history = sampling_history(api, authority, context, occupancy, "complete")
        trusted = sampling_trusted(authority, context, original, acquired, host, terminal, occupancy, history)
        identity = sampling_tsv(files.get("identity.tsv"))
        attempts = sampling_tsv(files.get("attempts.tsv"), True)
        if any(row.get("cpu_status") != "1" or row.get("memory_status") != "1" for row in attempts):
            raise ValueError("sampling nominal CPU or memory observation is unavailable")
        series = {}
        for index in range(len(context["schedule"]["slots"])):
            root = f"trial-{index}/"
            raw = sampling_json(files, root + "compare.json")
            pairs = sampling_json(files, root + "pairs.json", False)
            summary = sampling_json(files, root + "summary.json")
            variants = raw.get("variants")
            if not isinstance(variants, dict) or any(not isinstance(variants.get(key), dict) or
                    variants[key].get("ide") != trusted["binaries"][role]["path"]
                    for key, role in (("a", "baseline"), ("b", "candidate"))):
                raise ValueError("sampling raw stream compiler path differs from the acquired binary")
            series[index] = {"compare": raw, "pairs": pairs, "summary": summary}
        corpus_summary = sampling_json(files, "throughput/summary.json")
        corpus_metadata = sampling_json(files, "throughput/metadata.json")
        problems = classify_throughput(corpus_summary, corpus_metadata, trusted["binaries"])
        provenance = corpus_metadata.get("compiler_provenance")
        if not isinstance(provenance, list) or len(provenance) != 2 or any(
                not isinstance(row, dict) or row.get("path") != trusted["binaries"][role]["path"] or
                row.get("revision_label") != trusted["binaries"][role]["revision"] or
                type(row.get("bytes")) is not int or row["bytes"] != trusted["binaries"][role]["size_bytes"]
                for row, role in zip(provenance or [], ("baseline", "candidate"))):
            problems.append("full corpus compiler path/source/size differs from acquired binaries")
        if corpus_metadata.get("flags") != [] or corpus_metadata.get("allocation_compilers") != [] or \
                corpus_metadata.get("scale") != 1 or corpus_metadata.get("seed") != 20260907:
            problems.append("full corpus launch configuration differs from the unchanged CI profile")
        result = validate_packet(identity, attempts, terminal, series, trusted)
        result["problems"].extend(problems)
        result["full_required_corpus"] = throughput_digest(corpus_summary)
        if result["problems"]:
            result["packet_state"] = "incomplete"
            history[-1]["state"] = "invalid"
        else:
            result["outstanding_qualification"] = [item for item in result["outstanding_qualification"] if item != "full required corpus"]
    result.update(physical_packet_wall_us=sampling_integer(terminal["physical_packet_wall_us"], True),
                  prep_us=sampling_integer(terminal["prep_us"]), accounting=occupancy,
                  authenticated_attempt_history=history, host=host,
                  platform_runner={key: job.get(key) for key in ("id", "runner_id", "runner_name", "runner_group_id", "runner_group_name", "labels")},
                  acquisition_campaign=context["sha256"], acquisition_revision=context["revision"],
                  acquired_runtime={key: acquired[key] for key in ("python_path", "trusted_root", "python_sha256", "lab_sha256", "driver_sha256")},
                  prepared_sha256=prepared["sha256"],
                  acquisition_preparation_costs=sampling_json(files, "prepared/preparation-cost.json"),
                  acquired_binaries={role: {"sha256": prepared["record"][role + "_sha256"],
                                           "size_bytes": prepared["record"][role + "_bytes"]}
                                     for role in ("baseline", "candidate", "candidate2")})
    return result



CAMPAIGN_FACT_FIELDS = (
    "phase", "packet", "family", "request_run", "request_attempt", "executor_run", "executor_attempt",
    "policy_revision", "measurement_revision", "freeze_revision", "freeze_sha256", "parent_freeze_sha256",
    "artifact_id", "artifact_sha256", "artifact_bytes", "job_wall_us", "native_wall_us", "state",
    "slots", "slot_validations", "corpus_cells", "calibration_low_ppm", "calibration_high_ppm",
    "short_halfwidth_ppm", "raw_replay_sha256", "terminal_api_sha256", "terminal_api_bytes")
CAMPAIGN_ARCHIVE_FIELDS = (
    "phase", "packet", "request_run", "executor_run", "artifact_id", "artifact_sha256", "artifact_bytes",
    "archive_kind", "archive_reference", "archive_version", "archive_sha256", "archive_bytes", "archive_receipt_sha256")
CAMPAIGN_CRITERIA_FIELDS = (
    "schema", "measurement_revision", "preparation_plan_sha256", "preparation_raw_replay_sha256",
    "legacy_immutable_aa_low_ppm", "legacy_immutable_aa_high_ppm", "snapshot_immutable_aa_low_ppm",
    "snapshot_immutable_aa_high_ppm", "snapshot_cross_build_aa_low_ppm", "snapshot_cross_build_aa_high_ppm",
    "aa_corpus_regressions", "utility_plan_sha256", "utility_raw_replay_sha256",
    "utility_legacy_wall_us", "utility_snapshot_wall_us", "utility_job_wall_us")


CAMPAIGN_REPLAY_LIMIT = 64 * 1024 * 1024


def campaign_json(value: object, *, max_bytes: int = MEMBER_LIMIT) -> bytes:
    """Canonical retained data only; no check conclusion or receipt grants authority."""
    raw = (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False) + "\n").encode("ascii")
    if type(max_bytes) is not int or not 0 < max_bytes <= CAMPAIGN_REPLAY_LIMIT or not 0 < len(raw) <= max_bytes:
        raise ValueError("campaign retained replay data exceeds its bound")
    return raw


def campaign_ppm(low: object, high: object) -> tuple[int, int, int]:
    """Conservatively compact an already replayed interval; compute no new statistic."""
    from decimal import Decimal, localcontext, ROUND_FLOOR, ROUND_CEILING
    if not all(_number(value) for value in (low, high)) or not 0 < low <= high:
        raise ValueError("campaign replayed interval is missing, unordered or non-finite")
    with localcontext() as context:
        context.prec = 1100
        first = Decimal.from_float(low) if type(low) is float else Decimal(low)
        last = Decimal.from_float(high) if type(high) is float else Decimal(high)
        lower = int(((first - 1) * 1000000).to_integral_value(rounding=ROUND_FLOOR))
        upper = int(((last - 1) * 1000000).to_integral_value(rounding=ROUND_CEILING))
        halfwidth = int(((last - first) * 500000).to_integral_value(rounding=ROUND_CEILING))
    if not -(1 << 63) <= lower <= upper < (1 << 63) or not 0 <= halfwidth < (1 << 64):
        raise ValueError("campaign compact interval exceeds native integer bounds")
    return lower, upper, halfwidth


def campaign_table(fields: tuple[str, ...], rows: list[dict]) -> bytes:
    lines = ["\t".join(fields)]
    for row in rows:
        if set(row) != set(fields) or any(not isinstance(row[key], str) or not row[key] or
                any(ord(char) < 32 or ord(char) > 126 for char in row[key]) for key in fields):
            raise ValueError("campaign compact row is missing, noncanonical or ambiguous")
        lines.append("\t".join(row[key] for key in fields))
    raw = ("\n".join(lines) + "\n").encode("ascii")
    if len(raw) > MEMBER_LIMIT:
        raise ValueError("campaign compact table exceeds its bound")
    return raw


def campaign_not_run(phase: str, packet: int, family: str) -> dict:
    row = {key: "-" for key in CAMPAIGN_FACT_FIELDS}
    row.update(phase=phase, packet=str(packet), family=family, state="not_run",
               artifact_bytes="0", corpus_cells="0", terminal_api_bytes="0")
    return row


def campaign_archive_row(fact: dict, archived: dict | None) -> dict:
    """Join a protected first-party archive receipt; syntax never proves storage."""
    row = {key: fact[key] for key in CAMPAIGN_ARCHIVE_FIELDS[:7]}
    row.update({key: "-" for key in CAMPAIGN_ARCHIVE_FIELDS[7:]})
    row["archive_bytes"] = "0"
    if archived is None:
        return row
    if not isinstance(archived, dict) or set(archived) != set(CAMPAIGN_ARCHIVE_FIELDS) or \
            any(archived.get(key) != row[key] for key in CAMPAIGN_ARCHIVE_FIELDS[:7]):
        raise ValueError("campaign archive receipt differs from independently selected original attempt")
    digest = fact["artifact_sha256"] if fact["artifact_id"] != "-" else fact["terminal_api_sha256"]
    count = fact["artifact_bytes"] if fact["artifact_id"] != "-" else fact["terminal_api_bytes"]
    if fact["state"] == "not_run" or archived.get("archive_kind") != "library" or \
            not isinstance(archived.get("archive_reference"), str) or \
            not re.fullmatch(r"libfile_[a-f0-9]{32}", archived["archive_reference"]) or \
            not isinstance(archived.get("archive_version"), str) or \
            not re.fullmatch(r"(?:0|[1-9][0-9]*)", archived["archive_version"]) or \
            archived.get("archive_sha256") != digest or archived.get("archive_bytes") != count or \
            digest == "-" or not isinstance(archived.get("archive_receipt_sha256"), str) or \
            not re.fullmatch(r"[a-f0-9]{64}", archived["archive_receipt_sha256"]):
        raise ValueError("campaign durable archive identity or transferred byte binding is unavailable")
    return dict(archived)


def campaign_positive_int(value: object) -> int:
    if type(value) is not int or not 0 < value <= (1 << 64) - 1:
        raise ValueError("campaign observed integer is missing, nonpositive or exceeds its bound")
    return value


def campaign_native_scope(phase: str, packet: int, authority: dict) -> tuple:
    """Bind the original reviewed slot before API reads or invalid-data retention."""
    from sampling_qualification_receipt import schedule
    sampling = phase in ("acquire", "pilot", "confirm")
    planned = schedule(phase, packet) if sampling else {}
    if type(packet) is not int or (sampling and not planned) or (not sampling and (phase not in ("preparation", "utility") or packet != 0)):
        raise ValueError("campaign phase or packet is not declared")
    prefix = "sampling" if sampling else phase
    if not campaign_reviewed(authority, prefix):
        raise ValueError("campaign lacks its distinct historical native review")
    admitted = authority["admitted"]
    expected = {prefix + "_phase": "qualify" if phase == "preparation" else phase,
                prefix + "_packet": str(packet), prefix + "_family": planned["family"] if sampling else phase}
    if any(admitted.get(key) != value for key, value in expected.items()):
        raise ValueError("campaign original native review belongs to another planned slot")
    execution = authority.get("executor")
    if not isinstance(execution, dict) or not isinstance(execution.get("head_sha"), str) or \
            not SHA.fullmatch(execution["head_sha"]) or admitted.get(prefix + "_policy_revision") != execution["head_sha"]:
        raise ValueError("campaign original native review has another policy revision")
    return sampling, planned, admitted, prefix


def campaign_original_identity(phase: str, packet: int, authority: dict, artifact: dict) -> tuple:
    """Join original native-reviewed source and verified immutable artifact identity."""
    sampling, planned, admitted, prefix = campaign_native_scope(phase, packet, authority)
    revision_key = "sampling_freeze_revision" if sampling else prefix + "_plan_revision"
    hash_key = "sampling_freeze_sha256" if sampling else prefix + "_plan_sha256"
    measurement = admitted[prefix + "_trusted_revision"]
    policy = authority["executor"]["head_sha"]
    if any(not isinstance(value, str) or not SHA.fullmatch(value) for value in (measurement, policy, admitted[revision_key])) or \
            not isinstance(admitted[hash_key], str) or not re.fullmatch(r"[a-f0-9]{64}", admitted[hash_key]) or \
            any(not isinstance(authority.get(key), str) or not DECIMAL.fullmatch(authority[key]) for key in ("request_id", "run_id")) or \
            any(not isinstance(authority.get(key), dict) or type(authority[key].get("run_attempt")) is not int or
                authority[key]["run_attempt"] != 1 or type(authority[key].get("id")) is not int or
                str(authority[key]["id"]) != authority["request_id" if key == "request" else "run_id"]
                for key in ("request", "executor")) or \
            admitted.get(prefix + "_policy_revision") != policy:
        raise ValueError("campaign original API policy, source or attempt identity is invalid")
    if type(artifact.get("id")) is not int or artifact["id"] <= 0 or \
            not isinstance(artifact.get("verified_zip_sha256"), str) or not re.fullmatch(r"[a-f0-9]{64}", artifact["verified_zip_sha256"]) or \
            type(artifact.get("verified_zip_bytes")) is not int or artifact["verified_zip_bytes"] <= 0 or \
            not isinstance(artifact.get("verified_member_manifest_sha256"), str) or \
            not re.fullmatch(r"[a-f0-9]{64}", artifact["verified_member_manifest_sha256"]):
        raise ValueError("campaign raw replay lacks independent immutable ZIP identity")
    if sampling and phase != "acquire" and authority["freeze"].get("campaign_parent") != admitted.get("sampling_campaign_parent"):
        raise ValueError("campaign parent freeze contradicts original native review")
    return sampling, planned, admitted, measurement, policy, revision_key, hash_key


def campaign_transport_decode(value: object) -> bytes:
    if not isinstance(value, str) or len(value) > 1400000:
        raise ValueError("campaign retained transport encoding is missing or oversized")
    try:
        raw = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ValueError("campaign retained transport encoding is invalid") from error
    if len(raw) > 1024 * 1024 or base64.b64encode(raw).decode("ascii") != value:
        raise ValueError("campaign retained transport encoding is noncanonical")
    return raw


def campaign_current_record(authority: dict, kind: str) -> dict:
    current = historical_current_transport(authority, kind)
    return {"current": {name: base64.b64encode(raw).decode("ascii") for name, raw in current.items()},
            "api_proof": base64.b64encode(authority["historical_records"]["api"]).decode("ascii"),
            "native_api_proof": authority["native_api_proof"]}


def campaign_restore_current(authority: dict, record: object, kind: str) -> dict:
    if not isinstance(record, dict) or set(record) != {"current", "api_proof", "native_api_proof"} or not isinstance(record["current"], dict):
        raise ValueError("campaign retained current native API input record is missing")
    restored = dict(authority, raw={name: campaign_transport_decode(value) for name, value in record["current"].items()})
    api_raw = campaign_transport_decode(record["api_proof"])
    native_proof = sampling_tsv(api_raw)
    if record["native_api_proof"] != native_proof:
        raise ValueError("campaign retained current API proof duplicate changed")
    # Canonical JSON sorts keys; recover native field order from its exact TSV.
    restored.update(historical_records={"api": api_raw}, native_api_proof=native_proof)
    historical_current_transport(restored, kind)
    return restored


def campaign_transport_record(authority: dict, kind: str) -> dict:
    original = authority.get("raw_original")
    historical_transport(authority, original if isinstance(original, dict) else {}, kind)
    return dict(campaign_current_record(authority, kind),
                original={name: base64.b64encode(raw).decode("ascii") for name, raw in original.items()},
                native_original_facts_binding=authority["historical_original_facts_binding"])


def campaign_restore_transport(authority: dict, record: object, manifest: bytes, kind: str) -> dict:
    """Rejoin retained transport bytes and original member hashes without any API or execution."""
    if not isinstance(record, dict) or set(record) != {"current", "original", "api_proof", "native_api_proof", "native_original_facts_binding"} or \
            not isinstance(record["original"], dict):
        raise ValueError("campaign completed replay lacks its original transport binding")
    current = {name: record[name] for name in ("current", "api_proof", "native_api_proof")}
    restored = campaign_restore_current(authority, current, kind)
    restored.update(raw_original={name: campaign_transport_decode(value) for name, value in record["original"].items()},
                    historical_original_facts_binding=record["native_original_facts_binding"])
    original = historical_transport(restored, restored["raw_original"], kind)
    try:
        lines = manifest.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError("campaign retained member manifest encoding changed") from error
    for name, raw in original.items():
        expected = name + "\t" + hashlib.sha256(raw).hexdigest() + "\t" + str(len(raw))
        if lines.count(expected) != 1 or sum(line.split("\t", 1)[0] == name for line in lines) != 1:
            raise ValueError("campaign retained original transport differs from raw member manifest")
    return restored


def campaign_complete_fact(phase: str, packet: int, authority: dict, artifact: dict, result: dict) -> tuple[dict, bytes]:
    """Compact only the existing validator's complete raw replay, including scientific negatives."""
    from sampling_qualification_receipt import SHORT, LONG
    sampling, planned, admitted, measurement, policy, revision_key, hash_key = campaign_original_identity(phase, packet, authority, artifact)
    if result.get("packet_state") not in ("complete-valid-research", "complete-negative-research") or \
            result.get("problems") != [] or result.get("qualification_state") != "unqualified":
        raise ValueError("campaign cannot compact incomplete raw evidence")
    if result.get("phase") != ("qualify" if phase == "preparation" else phase) or result.get("packet") != packet:
        raise ValueError("campaign raw replay belongs to another planned packet")
    if phase in ("preparation", "utility") and result.get("campaign_criteria") != campaign_criteria_fragment(phase, result):
        raise ValueError("campaign compact prerequisite criteria contradict retained raw validation")
    account = result["accounting"]
    job_wall = campaign_positive_int(account["physical_job_wall_upper_us"])
    native_wall = campaign_positive_int(account["native_owner_wall_us"])
    if native_wall > job_wall:
        raise ValueError("campaign retained native wall exceeds API original whole job")
    replay = campaign_json({"schema": "buster-compiler-campaign-raw-replay-v1", "phase": phase, "packet": packet,
        "request": authority["request"], "executor": authority["executor"], "facts": authority["facts"],
        "native_review": admitted, "parent_freeze_sha256": authority["freeze"]["campaign_parent"] if sampling and phase != "acquire" else "-",
        "artifact_id": artifact["id"], "artifact_sha256": artifact["verified_zip_sha256"],
        "artifact_bytes": artifact["verified_zip_bytes"], "member_manifest_sha256": artifact["verified_member_manifest_sha256"],
        "historical_transport": campaign_transport_record(authority, "sampling" if sampling else phase), "validation": result})
    row = campaign_not_run(phase, packet, planned["family"] if sampling else phase)
    row.update(request_run=authority["request_id"], request_attempt="1", executor_run=authority["run_id"], executor_attempt="1",
               policy_revision=policy, measurement_revision=measurement, freeze_revision=admitted[revision_key],
               freeze_sha256=admitted[hash_key], artifact_id=str(artifact["id"]), artifact_sha256=artifact["verified_zip_sha256"],
               artifact_bytes=str(artifact["verified_zip_bytes"]), job_wall_us=str(job_wall), native_wall_us=str(native_wall),
               state="complete", corpus_cells=str(0 if phase == "acquire" else 12 if sampling else 60 if phase == "preparation" else 24),
               raw_replay_sha256=hashlib.sha256(replay).hexdigest())
    if sampling and phase != "acquire":
        row["parent_freeze_sha256"] = authority["freeze"]["campaign_parent"]
        series, slots = result.get("series"), planned["slots"]
        if not isinstance(series, list) or len(series) != len(slots):
            raise ValueError("campaign validated slot population differs from the fixed plan")
        outcomes = []
        for item, slot in zip(series, slots):
            if not isinstance(item, dict) or item.get("profile") != slot[0] or item.get("ordinal") != slot[1]:
                raise ValueError("campaign validated slot order differs from native schedule")
            outcome = {"no detectable difference": "unchanged"}.get(item.get("outcome"), item.get("outcome"))
            if outcome not in ("unchanged", "below-floor", "slower", "faster", "inconclusive"):
                raise ValueError("campaign validated statistical outcome is unavailable")
            outcomes.append(outcome)
        row.update(slots=",".join(outcomes), slot_validations=",".join(["valid"] * len(slots)))
        if phase == "pilot":
            short = next(item for item in series if item["profile"] == SHORT)
            uncertainty = short["uncertainty"]
            row["short_halfwidth_ppm"] = str(campaign_ppm(uncertainty["ci_low"], uncertainty["ci_high"])[2])
            if packet:
                long = next(item for item in series if item["profile"] == LONG)
                uncertainty = long["uncertainty"]
                low, high, unused_halfwidth = campaign_ppm(uncertainty["ci_low"], uncertainty["ci_high"])
                row.update(calibration_low_ppm=str(low), calibration_high_ppm=str(high))
    return row, replay



def campaign_api_wall(job: object) -> int:
    """Retain conservative API timestamp occupancy even for failed or over-budget jobs."""
    if not isinstance(job, dict) or type(job.get("id")) is not int or job["id"] <= 0 or \
            type(job.get("run_id")) is not int or job["run_id"] <= 0 or \
            type(job.get("run_attempt")) is not int or job["run_attempt"] != 1 or job.get("status") != "completed":
        raise ValueError("campaign original terminal API job is unavailable")
    stamps = []
    for key in ("created_at", "started_at", "completed_at"):
        value = job.get(key)
        if not isinstance(value, str):
            raise ValueError("campaign API timestamp is unavailable")
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.utcoffset() is None:
            raise ValueError("campaign API timestamp has no timezone")
        stamps.append(parsed)
    created, started, completed = stamps
    delta = completed - started
    wall = delta.days * 86400000000 + delta.seconds * 1000000 + delta.microseconds
    if wall < 0 or started < created:
        raise ValueError("campaign original API job clock is unordered")
    return campaign_positive_int(wall + 2000000)


def campaign_invalid_row(phase: str, packet: int, authority: dict, artifact: dict, job: dict | None) -> dict:
    """Compact an invalid replay with only independently observed API wall, if available."""
    sampling, planned, admitted, measurement, policy, revision_key, hash_key = campaign_original_identity(phase, packet, authority, artifact)
    job_wall = "-"
    if job is not None:
        campaign_audit_job_binding(job, phase, authority)
        try:
            job_wall = str(campaign_api_wall(job))
        except ValueError:
            pass
    row = campaign_not_run(phase, packet, planned["family"] if sampling else phase)
    row.update(request_run=authority["request_id"], request_attempt="1", executor_run=authority["run_id"], executor_attempt="1",
               policy_revision=policy, measurement_revision=measurement, freeze_revision=admitted[revision_key],
               freeze_sha256=admitted[hash_key], artifact_id=str(artifact["id"]), artifact_sha256=artifact["verified_zip_sha256"],
               artifact_bytes=str(artifact["verified_zip_bytes"]), job_wall_us=job_wall, state="invalid")
    if sampling and phase != "acquire":
        row["parent_freeze_sha256"] = authority["freeze"]["campaign_parent"]
    return row


def campaign_audit_job_binding(job: object, phase: str, authority: dict) -> dict:
    execution = authority.get("executor")
    name = SAMPLING_HOST_JOB if phase in ("acquire", "pilot", "confirm") else PREPARATION_HOST_JOB if phase == "preparation" else UTILITY_HOST_JOB
    if not isinstance(execution, dict) or type(execution.get("id")) is not int or \
            not isinstance(job, dict) or job.get("name") != name or type(job.get("id")) is not int or job["id"] <= 0 or \
            type(job.get("run_id")) is not int or job["run_id"] != execution["id"] or \
            type(job.get("run_attempt")) is not int or job["run_attempt"] != 1 or job.get("head_sha") != execution.get("head_sha"):
        raise ValueError("campaign original API job belongs to another attempt or source")
    return job


def campaign_audit_job(api: Api, phase: str, authority: dict) -> dict:
    """Select original API job data independently of measurement runner eligibility."""
    run_id = authority.get("run_id")
    execution = authority.get("executor")
    if not isinstance(run_id, str) or not DECIMAL.fullmatch(run_id) or not isinstance(execution, dict) or \
            type(execution.get("id")) is not int or str(execution["id"]) != run_id or \
            type(execution.get("run_attempt")) is not int or execution["run_attempt"] != 1 or \
            not isinstance(execution.get("head_sha"), str) or not SHA.fullmatch(execution["head_sha"]):
        raise ValueError("campaign original executor job identity is unavailable")
    rows = api.pages(f"/actions/runs/{run_id}/attempts/1/jobs", "jobs")
    if not isinstance(rows, list) or len(rows) > 1000:
        raise ValueError("campaign original API job inventory is missing or oversized")
    name = SAMPLING_HOST_JOB if phase in ("acquire", "pilot", "confirm") else PREPARATION_HOST_JOB if phase == "preparation" else UTILITY_HOST_JOB
    selected = [row for row in rows if isinstance(row, dict) and row.get("name") == name]
    if len(selected) != 1:
        raise ValueError("campaign original physical API job is not unique")
    return campaign_audit_job_binding(selected[0], phase, authority)


def campaign_invalid_fact(api: Api, phase: str, packet: int, authority: dict, artifact: dict,
                          error: ValueError, result: dict | None = None) -> tuple[dict, bytes]:
    """Retain invalid raw data without inventing a complete native or timing proof."""
    sampling, unused_plan, admitted, unused_measurement, unused_policy, unused_revision_key, unused_hash_key = campaign_original_identity(phase, packet, authority, artifact)
    job = None
    try:
        job = campaign_audit_job(api, phase, authority)
    except ValueError:
        pass
    row = campaign_invalid_row(phase, packet, authority, artifact, job)
    replay = campaign_json({"schema": "buster-compiler-campaign-invalid-replay-v1", "phase": phase, "packet": packet,
        "qualification_state": "unqualified", "execution_authority": False,
        "request": authority["request"], "executor": authority["executor"], "job": job,
        "facts": authority["facts"], "native_review": admitted, "parent_freeze_sha256": row["parent_freeze_sha256"],
        "artifact_id": artifact["id"], "artifact_sha256": artifact["verified_zip_sha256"],
        "artifact_bytes": artifact["verified_zip_bytes"], "member_manifest_sha256": artifact["verified_member_manifest_sha256"],
        "current_transport": campaign_current_record(authority, "sampling" if sampling else phase),
        "validation_error": str(error)[:1000], "partial_validation": result})
    row["raw_replay_sha256"] = hashlib.sha256(replay).hexdigest()
    return row, replay


def campaign_aa_corpus_counts(files: dict[str, bytes]) -> dict:
    counts = {}
    for label in ("legacy/immutable-aa", "snapshot/immutable-aa", "snapshot/cross-build-aa"):
        corpus = sampling_json(files, "qualification/" + label + "-throughput/summary.json")
        count = corpus.get("confirmed_regressions")
        if type(count) is not int or not 0 <= count <= 12:
            raise ValueError("campaign prerequisite A/A corpus regression count is unavailable")
        counts[label] = count
    return counts


def campaign_criteria_fragment(phase: str, result: dict) -> dict:
    """Recompact retained validated intervals, exact A/A counts and clocks without inference."""
    criterion = {}
    if phase == "preparation":
        counts = result.get("campaign_aa_corpus_regressions")
        labels = ("legacy/immutable-aa", "snapshot/immutable-aa", "snapshot/cross-build-aa")
        if not isinstance(counts, dict) or set(counts) != set(labels) or any(
                type(counts[label]) is not int or not 0 <= counts[label] <= 12 for label in labels):
            raise ValueError("campaign retained original A/A corpus counts are unavailable")
        for label, stem in zip(labels, ("legacy_immutable_aa", "snapshot_immutable_aa", "snapshot_cross_build_aa")):
            item = result["series"][label]
            low, high, unused_halfwidth = campaign_ppm(item["ci_low"], item["ci_high"])
            criterion[stem + "_low_ppm"], criterion[stem + "_high_ppm"] = str(low), str(high)
        criterion["aa_corpus_regressions"] = str(sum(counts.values()))
    elif phase == "utility":
        net = result["utility_observation"]
        criterion.update(utility_legacy_wall_us=str(campaign_positive_int(net["legacy_leg_us"])),
                         utility_snapshot_wall_us=str(campaign_positive_int(net["snapshot_leg_us"])),
                         utility_job_wall_us=str(campaign_positive_int(net["physical_job_wall_upper_us"])))
    return criterion


def campaign_ingest_packet(api: Api, phase: str, packet: int, authority: dict, archive: dict | None = None) -> dict:
    """Replay one original retained API-selected ZIP and immediately return durable data.

    The separate original-attempt native constructor supplies authority. This
    reader neither admits an execution nor schedules a campaign. Ingestion must
    occur while the original immutable ZIP is retained; final aggregation does
    not download old Actions artifacts or fall back after expiration.
    """
    if not isinstance(authority, dict) or authority.get("repository") != "buster14a/buster" or \
            not campaign_reviewed(authority, "sampling" if phase in ("acquire", "pilot", "confirm") else phase):
        raise ValueError("campaign ingestion lacks original independently reviewed authority")
    campaign_native_scope(phase, packet, authority)
    historical_current_transport(authority, "sampling" if phase in ("acquire", "pilot", "confirm") else phase)
    if phase in ("acquire", "pilot", "confirm"):
        reader, validator = sampling_read_artifact, sampling_validate
    elif phase == "preparation":
        reader, validator = preparation_read_artifact, preparation_validate
    elif phase == "utility":
        reader, validator = utility_read_artifact, utility_validate
    else:
        raise ValueError("campaign ingestion phase is not declared")
    files, artifact = reader(api, authority, retain_archive_identity=True)
    campaign_original_identity(phase, packet, authority, artifact)
    result = None
    try:
        authority = bind_historical_transport(authority, files, "sampling" if phase in ("acquire", "pilot", "confirm") else phase)
        result = validator(api, authority, files)
        if result.get("packet_state") not in ("complete-valid-research", "complete-negative-research") or \
                result.get("problems") != [] or result.get("qualification_state") != "unqualified":
            raise ValueError("campaign original raw validation is incomplete")
        result = dict(result)
        if phase == "preparation":
            result["campaign_aa_corpus_regressions"] = campaign_aa_corpus_counts(files)
        result["campaign_criteria"] = campaign_criteria_fragment(phase, result)
        row, replay = campaign_complete_fact(phase, packet, authority, artifact, result)
    except ValueError as error:
        row, replay = campaign_invalid_fact(api, phase, packet, authority, artifact, error, result)
    manifest = artifact.get("verified_member_manifest")
    if not isinstance(manifest, bytes) or hashlib.sha256(manifest).hexdigest() != artifact["verified_member_manifest_sha256"]:
        raise ValueError("campaign immutable raw member manifest is missing or changed")
    return {"schema": "buster-compiler-campaign-ingestion-v1", "phase": phase, "packet": packet,
            "fact": row, "archive": campaign_archive_row(row, archive), "raw_replay": replay, "raw_manifest": manifest,
            "qualification_state": "unqualified", "physical_qualification": False, "archive_storage_assessed": False}


CAMPAIGN_TERMINAL_FIELDS = (
    "schema", "kind", "phase", "packet", "family", "executor_inventory_count", "selected_executor_inventory_id",
    "physical_job_id", "physical_job_state", "physical_job_conclusion", "physical_job_started_at", "physical_job_completed_at",
    "terminal_state", "terminal_api_sha256", "terminal_api_bytes", "context_revision", "context_main_relation")


def campaign_terminal_inventory(authority: dict, parsed: dict) -> dict | None:
    """Rejoin complete original API artifact pages; a failed attempt can retain a partial ZIP."""
    selected = authority.get("selected_artifact")
    inventory = authority.get("terminal_artifact_inventory")
    if authority.get("artifact_inventory_complete") is not True or not isinstance(inventory, list) or len(inventory) > 1000:
        raise ValueError("campaign terminal original artifact inventory is unavailable or incomplete")
    selection = parsed.get("artifact_inventory_selection")
    if not isinstance(selection, dict):
        raise ValueError("campaign terminal API envelope lacks artifact inventory selection")
    prefix = authority["terminal_proof"]["kind"]
    expected = "buster-9700x-" + prefix + "-" + authority["head"] + "-1"
    if authority["executor"] is None:
        if inventory != [] or selected is not None or authority.get("expected_artifact_name") != expected or \
                selection.get("expected_name") != expected or selection.get("selected_id") is not None or selection.get("pages") != [] or \
                selection.get("matching_ids") != [] or selection.get("executor_run") != "-" or selection.get("complete") is not True:
            raise ValueError("campaign missing executor has a fabricated artifact inventory")
        return None
    if authority.get("expected_artifact_name") != expected or selection.get("expected_name") != expected or \
            selection.get("executor_run") != authority["run_id"] or selection.get("complete") is not True:
        raise ValueError("campaign terminal expected artifact identity changed")
    observations = parsed["api_observations"]
    pages = []
    for page in range(1, 11):
        path = f"/actions/runs/{authority['run_id']}/artifacts?per_page=100&page={page}"
        if path not in observations:
            break
        value = observations[path]
        if not isinstance(value, dict) or type(value.get("total_count")) is not int or not 0 <= value["total_count"] <= 1000 or \
                not isinstance(value.get("artifacts"), list) or len(value["artifacts"]) > 100:
            raise ValueError("campaign terminal original artifact API page is malformed")
        pages.append(value)
    if not pages or any(len(page["artifacts"]) != 100 for page in pages[:-1]) or \
            any(page["total_count"] != pages[0]["total_count"] for page in pages) or \
            [row for page in pages for row in page["artifacts"]] != inventory or len(inventory) != pages[0]["total_count"] or \
            selection.get("pages") != [f"/actions/runs/{authority['run_id']}/artifacts?per_page=100&page={page}" for page in range(1, len(pages) + 1)]:
        raise ValueError("campaign terminal original artifact API inventory is truncated or changed")
    if any(not isinstance(row, dict) or type(row.get("id")) is not int or row["id"] <= 0 or
           not isinstance(row.get("name"), str) or not row["name"] for row in inventory) or len({row["id"] for row in inventory}) != len(inventory):
        raise ValueError("campaign terminal original artifact inventory has ambiguous identity")
    matches = [row for row in inventory if row["name"] == expected]
    if len(matches) > 1 or selected != (matches[0] if matches else None) or \
            selection.get("matching_ids") != [row["id"] for row in matches] or selection.get("selected_id") != (matches[0]["id"] if matches else None):
        raise ValueError("campaign terminal original expected artifact is not its unique API inventory member")
    if selected is not None:
        origin = selected.get("workflow_run")
        if not isinstance(origin, dict) or type(origin.get("id")) is not int or str(origin["id"]) != authority["run_id"] or \
                origin.get("head_sha") != authority["executor"]["head_sha"] or type(selected.get("expired")) is not bool or \
                type(selected.get("size_in_bytes")) is not int or selected["size_in_bytes"] <= 0:
            raise ValueError("campaign terminal original artifact belongs to another source or attempt")
    return selected


def campaign_terminal_scope(phase: str, packet: int, authority: dict) -> tuple:
    """Join a distinct native terminal data proof; it cannot certify a measurement."""
    from sampling_qualification_receipt import schedule
    sampling = phase in ("acquire", "pilot", "confirm")
    planned = schedule(phase, packet) if sampling else {}
    if type(packet) is not int or (sampling and not planned) or (not sampling and (phase not in ("preparation", "utility") or packet != 0)):
        raise ValueError("campaign terminal slot is not declared")
    prefix = "sampling" if sampling else phase
    admitted = authority.get("admitted")
    flags = {prefix + "_historical_terminal_valid": "true", prefix + "_historical_valid": "false",
             prefix + "_historical_measurement_valid": "false", prefix + "_historical_execution_authority": "false",
             prefix + "_historical_qualification": "unqualified"}
    if authority.get("historical_terminal_review") is not True or authority.get("repository") != "buster14a/buster" or \
            not isinstance(admitted, dict) or prefix + "_admitted" in admitted or any(admitted.get(key) != value for key, value in flags.items()):
        raise ValueError("campaign terminal data lacks its distinct nonexecution native proof")
    current = historical_current_transport(authority, prefix)
    records, terminal = authority.get("historical_records"), authority.get("terminal_proof")
    terminal_raw = records.get("terminal") if isinstance(records, dict) else None
    envelope = authority.get("terminal_api_envelope")
    if not isinstance(terminal_raw, bytes) or not 0 < len(terminal_raw) <= 16384 or not isinstance(terminal, dict) or \
            set(terminal) != set(CAMPAIGN_TERMINAL_FIELDS) or sampling_tsv(terminal_raw) != terminal or \
            not isinstance(envelope, bytes) or not 0 < len(envelope) <= 8 * 1024 * 1024 or records.get("envelope") != envelope:
        raise ValueError("campaign terminal proof or original API envelope is missing or changed")
    envelope_sha = hashlib.sha256(envelope).hexdigest()
    wanted = {"schema": "buster-compiler-historical-terminal-v1", "kind": prefix,
              "phase": "qualify" if phase == "preparation" else phase, "packet": str(packet),
              "family": planned["family"] if sampling else phase, "terminal_api_sha256": envelope_sha,
              "terminal_api_bytes": str(len(envelope))}
    if any(terminal.get(key) != value for key, value in wanted.items()) or authority.get("terminal_api_sha256") != envelope_sha:
        raise ValueError("campaign terminal source belongs to another original slot or envelope")
    request, executor = authority["request"], authority.get("executor")
    request_id, run_id = authority["request_id"], authority.get("run_id")
    context = authority.get("historical_context_revision")
    if not isinstance(authority.get("head"), str) or not SHA.fullmatch(authority["head"]):
        raise ValueError("campaign terminal original request head is invalid")
    no_executor = executor is None
    if no_executor:
        if run_id != "-" or terminal["executor_inventory_count"] != "0" or terminal["selected_executor_inventory_id"] != "-" or \
                terminal["terminal_state"] != "hostless" or current["allowlist.tsv"] != b"" or \
                not isinstance(context, str) or not SHA.fullmatch(context) or terminal["context_revision"] != context or \
                terminal["context_main_relation"] not in ("ahead", "identical") or admitted.get(prefix + "_policy_revision") != "-" or \
                authority["native_api_proof"].get("policy_revision") != "-" or \
                any(authority["native_api_proof"].get(key) != "-" for key in ("executor_run_id", "executor_run_attempt", "executor_head")):
            raise ValueError("campaign missing executor was replaced by a policy, attempt or fabricated inventory")
    elif not isinstance(executor.get("head_sha"), str) or not SHA.fullmatch(executor["head_sha"]) or \
            terminal["executor_inventory_count"] != "1" or terminal["selected_executor_inventory_id"] != run_id or \
            context != "-" or terminal["context_revision"] != "-" or terminal["context_main_relation"] != "-" or \
            terminal["terminal_state"] not in ("failed", "cancelled", "incomplete", "invalid"):
        raise ValueError("campaign known terminal executor or its separate context is inconsistent")
    identity = {prefix + "_phase": wanted["phase"], prefix + "_packet": str(packet), prefix + "_family": wanted["family"],
                prefix + "_historical_request_run_id": request_id, prefix + "_historical_request_run_attempt": "1",
                prefix + "_historical_request_head": authority["head"],
                prefix + "_historical_executor_run_id": run_id, prefix + "_historical_executor_run_attempt": "-" if no_executor else "1",
                prefix + "_historical_context_revision": context, prefix + "_historical_terminal_state": terminal["terminal_state"],
                prefix + "_historical_terminal_api_sha256": envelope_sha, prefix + "_historical_terminal_api_bytes": str(len(envelope))}
    def observed(value):
        if value is None:
            return "-"
        if not isinstance(value, str) or not value:
            raise ValueError("campaign terminal API string is malformed")
        return value
    identity[prefix + "_historical_request_conclusion"] = observed(request.get("conclusion"))
    identity[prefix + "_historical_executor_conclusion"] = "-" if no_executor else observed(executor.get("conclusion"))
    job = authority.get("selected_physical_job")
    job_fields = ("id", "status", "conclusion", "started_at", "completed_at")
    labels = ("id", "state", "conclusion", "started_at", "completed_at")
    if job is not None:
        name = SAMPLING_HOST_JOB if sampling else PREPARATION_HOST_JOB if phase == "preparation" else UTILITY_HOST_JOB
        if no_executor or not isinstance(job, dict) or type(job.get("id")) is not int or job["id"] <= 0 or \
                type(job.get("run_id")) is not int or job["run_id"] != executor["id"] or \
                type(job.get("run_attempt")) is not int or job["run_attempt"] != 1 or job.get("head_sha") != executor["head_sha"] or job.get("name") != name:
            raise ValueError("campaign terminal physical job belongs to another actual attempt")
    for field, label in zip(job_fields, labels):
        value = "-" if job is None else str(job["id"]) if field == "id" else observed(job.get(field))
        if terminal.get("physical_job_" + label) != value:
            raise ValueError("campaign terminal physical job identity or observed timestamps changed")
        identity[prefix + "_historical_physical_job_" + label] = value
    if any(admitted.get(key) != value for key, value in identity.items()):
        raise ValueError("campaign terminal retained API data differs from its native output")
    revision_key = "sampling_freeze_revision" if sampling else prefix + "_plan_revision"
    hash_key = "sampling_freeze_sha256" if sampling else prefix + "_plan_sha256"
    if any(not isinstance(admitted.get(key), str) or not SHA.fullmatch(admitted[key]) for key in (revision_key, prefix + "_trusted_revision")) or \
            admitted.get(hash_key) != authority["native_api_proof"].get("freeze_sha256"):
        raise ValueError("campaign terminal actual committed plan or measurement revision is absent")
    parsed = sampling_json({"envelope.json": envelope}, "envelope.json")
    envelope_keys = {"schema", "repository", "kind", "request_run", "executor_run", "context_revision",
                     "api_observations", "native_api_proof", "review_context_selection", "artifact_inventory_selection"}
    observations = parsed.get("api_observations")
    if set(parsed) != envelope_keys or parsed.get("schema") != "buster-compiler-historical-terminal-api-envelope-v1" or \
            any(parsed.get(key) != value for key, value in (("repository", authority["repository"]), ("kind", prefix),
                ("request_run", request_id), ("executor_run", run_id), ("context_revision", context))) or \
            parsed.get("native_api_proof") != authority["native_api_proof"] or not isinstance(observations, dict) or \
            observations.get(f"/actions/runs/{request_id}/attempts/1") != request or \
            (not no_executor and observations.get(f"/actions/runs/{run_id}/attempts/1") != executor):
        raise ValueError("campaign terminal envelope differs from independently selected original API records")
    if job is not None:
        observed_jobs = [row for path, value in observations.items() if isinstance(path, str) and
                         (path == f"/actions/runs/{run_id}/attempts/1/jobs" or path.startswith(f"/actions/runs/{run_id}/attempts/1/jobs?")) and
                         isinstance(value, dict) and isinstance(value.get("jobs"), list) for row in value["jobs"]]
        if sum(row == job for row in observed_jobs) != 1:
            raise ValueError("campaign terminal physical job is not its exact original API inventory member")
    selection = parsed["review_context_selection"]
    if not isinstance(selection, dict) or selection.get("freeze_revision") != admitted[revision_key] or \
            selection.get("freeze_sha256") != admitted[hash_key] or \
            selection.get("revision") != (context if no_executor else executor["head_sha"]):
        raise ValueError("campaign terminal separate review context changed committed plan identity")
    campaign_terminal_inventory(authority, parsed)
    return sampling, planned, admitted, prefix, revision_key, hash_key, envelope, job


def campaign_terminal_manifest_count(manifest: bytes) -> int:
    """Count the exact canonical raw file inventory retained at ZIP ingestion."""
    if not isinstance(manifest, bytes) or not 0 < len(manifest) <= ANALYZER_MEMBER_LIMIT or not manifest.endswith(b"\n"):
        raise ValueError("campaign terminal raw member manifest is missing or noncanonical")
    try:
        lines = manifest.decode("ascii").splitlines()
        if manifest != ("\n".join(lines) + "\n").encode("ascii"):
            raise ValueError("campaign terminal raw member manifest line endings changed")
    except UnicodeDecodeError as error:
        raise ValueError("campaign terminal raw member manifest encoding changed") from error
    names, aliases = [], set()
    for line in lines:
        fields = line.split("\t")
        if len(fields) != 3:
            raise ValueError("campaign terminal raw member manifest row is malformed")
        name, digest, size = fields
        alias = member_identity(name)
        if not name or len(name) > 512 or "\\" in name or name.startswith("/") or \
                any(part in ("", ".", "..") for part in name.split("/")) or \
                any(ord(char) < 32 or ord(char) > 126 for char in name) or alias in aliases or \
                not re.fullmatch(r"[a-f0-9]{64}", digest) or not re.fullmatch(r"0|[1-9][0-9]*", size):
            raise ValueError("campaign terminal raw member manifest has an unsafe or duplicate identity")
        aliases.add(alias)
        names.append(name)
    if not 0 < len(names) <= 65536 or names != sorted(names):
        raise ValueError("campaign terminal raw member manifest population or order changed")
    return len(names)

def campaign_terminal_data(phase: str, packet: int, authority: dict, artifact: dict | None = None) -> tuple[dict, bytes, bytes]:
    sampling, planned, admitted, prefix, revision_key, hash_key, envelope, job = campaign_terminal_scope(phase, packet, authority)
    row = campaign_not_run(phase, packet, planned["family"] if sampling else phase)
    wall = "-"
    if job is not None:
        try:
            wall = str(campaign_api_wall(job))
        except ValueError:
            pass
    row.update(request_run=authority["request_id"], request_attempt="1", executor_run=authority["run_id"],
               executor_attempt="-" if authority["executor"] is None else "1", policy_revision=admitted[prefix + "_policy_revision"],
               measurement_revision=admitted[prefix + "_trusted_revision"], freeze_revision=admitted[revision_key],
               freeze_sha256=admitted[hash_key], job_wall_us=wall, state=authority["terminal_proof"]["terminal_state"],
               terminal_api_sha256=hashlib.sha256(envelope).hexdigest(), terminal_api_bytes=str(len(envelope)))
    if sampling and phase != "acquire":
        row["parent_freeze_sha256"] = authority["native_api_proof"]["parent_freeze_sha256"]
    selected = authority["selected_artifact"]
    manifest = None
    retained_artifact = None
    if selected is None:
        if artifact is not None:
            raise ValueError("campaign zero-artifact terminal attempt was given an invented ZIP")
    else:
        if not isinstance(artifact, dict) or any(artifact.get(key) != value for key, value in selected.items()) or selected["expired"] is not False or \
                not isinstance(selected.get("digest"), str) or selected["digest"] != "sha256:" + str(artifact.get("verified_zip_sha256")) or \
                type(artifact.get("verified_zip_bytes")) is not int or artifact["verified_zip_bytes"] != selected["size_in_bytes"] or \
                not isinstance(artifact.get("verified_member_manifest"), bytes) or \
                not 0 < len(artifact["verified_member_manifest"]) <= ANALYZER_MEMBER_LIMIT or \
                artifact.get("verified_member_manifest_sha256") != hashlib.sha256(artifact["verified_member_manifest"]).hexdigest() or \
                type(artifact.get("verified_member_count")) is not int or not 0 < artifact["verified_member_count"] <= 65536:
            raise ValueError("campaign terminal partial ZIP lacks independently verified original bytes and member manifest")
        manifest = artifact["verified_member_manifest"]
        if artifact["verified_member_count"] != campaign_terminal_manifest_count(manifest):
            raise ValueError("campaign terminal verified ZIP member count differs from retained inventory")
        row.update(artifact_id=str(selected["id"]), artifact_sha256=artifact["verified_zip_sha256"],
                   artifact_bytes=str(artifact["verified_zip_bytes"]))
        retained_artifact = dict(artifact)
        retained_artifact["verified_member_manifest"] = base64.b64encode(manifest).decode("ascii")
    current = campaign_current_record(authority, prefix)
    replay = campaign_json({"schema": "buster-compiler-campaign-terminal-replay-v1", "phase": phase, "packet": packet,
        "qualification_state": "unqualified", "execution_authority": False, "request": authority["request"], "executor": authority["executor"],
        "facts": authority["facts"], "native_review": admitted, "current_transport": current,
        "terminal_proof": base64.b64encode(authority["historical_records"]["terminal"]).decode("ascii"),
        "api_envelope": base64.b64encode(envelope).decode("ascii"), "historical_context_revision": authority["historical_context_revision"],
        "job": job, "verified_artifact": retained_artifact}, max_bytes=CAMPAIGN_REPLAY_LIMIT)
    row["raw_replay_sha256"] = hashlib.sha256(replay).hexdigest()
    retained = dict(authority["raw"], **{"native-api-proof.tsv": authority["historical_records"]["api"],
                                      "historical-terminal.tsv": authority["historical_records"]["terminal"],
                                      "terminal-api-envelope.json": envelope})
    if manifest is None:
        manifest = b"".join((name + "\t" + hashlib.sha256(raw).hexdigest() + "\t" + str(len(raw)) + "\n").encode("ascii")
                            for name, raw in sorted(retained.items()))
    return row, replay, manifest


def campaign_retain_terminal_observation(diagnostic: dict) -> dict:
    """Keep refused lookup observations separately from native-reviewed campaign facts."""
    if not isinstance(diagnostic, dict) or diagnostic.get("terminal_valid") is not False or \
            diagnostic.get("execution_authority") is not False or diagnostic.get("qualification") != "unqualified":
        raise ValueError("campaign refused lookup diagnostic claims terminal or execution authority")
    flags = {"terminal_valid", "execution_authority", "qualification"}
    if diagnostic.get("envelope_unavailable") is True:
        reasons = {"API observations exceed the 8 MiB data bound", "API observations are not bounded JSON data"}
        if set(diagnostic) != flags | {"envelope_unavailable", "diagnostic_envelope_unavailable"} or \
                diagnostic["diagnostic_envelope_unavailable"] not in reasons:
            raise ValueError("campaign unavailable observation envelope invents identity or cost")
    else:
        raw = diagnostic.get("diagnostic_envelope")
        if set(diagnostic) != flags | {"api_observations", "diagnostic_envelope", "diagnostic_sha256", "diagnostic_bytes"} or \
                not isinstance(raw, bytes) or not 0 < len(raw) <= 8 * 1024 * 1024 or \
                type(diagnostic.get("diagnostic_bytes")) is not int or diagnostic["diagnostic_bytes"] != len(raw) or \
                diagnostic.get("diagnostic_sha256") != hashlib.sha256(raw).hexdigest():
            raise ValueError("campaign refused lookup observation bytes or identity changed")
        parsed = sampling_json({"diagnostic.json": raw}, "diagnostic.json", max_bytes=8 * 1024 * 1024)
        if set(parsed) != {"schema", "repository", "kind", "api_observations", *flags} or \
                parsed.get("schema") != "buster-compiler-historical-terminal-diagnostic-envelope-v1" or \
                parsed.get("repository") != "buster14a/buster" or parsed.get("kind") not in ("sampling", "preparation", "utility") or \
                parsed.get("terminal_valid") is not False or parsed.get("execution_authority") is not False or \
                parsed.get("qualification") != "unqualified" or not isinstance(parsed.get("api_observations"), dict) or \
                parsed["api_observations"] != diagnostic["api_observations"] or \
                json.dumps(parsed, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii") != raw:
            raise ValueError("campaign refused lookup diagnostic differs from actual bounded observation data")
    return dict(diagnostic, schema="buster-compiler-campaign-terminal-audit-v1")

def campaign_ingest_terminal(phase: str, packet: int, authority: dict, archive: dict | None = None, *, raw_zip: bytes | None = None) -> dict:
    """Retain one native-reviewed original terminal API envelope; no ZIP or measurement fallback."""
    campaign_terminal_scope(phase, packet, authority)
    selected, artifact = authority["selected_artifact"], None
    if selected is None:
        if raw_zip is not None:
            raise ValueError("campaign terminal zero artifact inventory contradicts supplied ZIP bytes")
    else:
        sampling = phase in ("acquire", "pilot", "confirm")
        limit = ARTIFACT_LIMIT if sampling else PREPARATION_ARCHIVE_LIMIT
        if selected.get("expired") is not False or not isinstance(raw_zip, bytes) or not 0 < len(raw_zip) <= limit:
            raise ValueError("campaign terminal partial ZIP is expired, unavailable or oversized")
        files = sampling_archive(raw_zip, member_limit=MAIN_ARCHIVE_FILE_LIMIT, require_nonexecutable=True) \
            if sampling else preparation_archive(raw_zip)
        artifact = campaign_artifact_identity(raw_zip, selected, files, True)
    row, replay, manifest = campaign_terminal_data(phase, packet, authority, artifact)
    return {"schema": "buster-compiler-campaign-terminal-ingestion-v1", "phase": phase, "packet": packet,
            "fact": row, "archive": campaign_archive_row(row, archive), "raw_replay": replay, "raw_manifest": manifest,
            "qualification_state": "unqualified", "physical_qualification": False, "archive_storage_assessed": False}


def campaign_restore_terminal(record: dict, phase: str, packet: int) -> dict:
    if record.get("schema") != "buster-compiler-campaign-terminal-replay-v1" or record.get("qualification_state") != "unqualified" or \
            record.get("execution_authority") is not False or record.get("phase") != phase or type(record.get("packet")) is not int or record["packet"] != packet:
        raise ValueError("campaign retained terminal data claims measurement or another slot")
    request, executor = record.get("request"), record.get("executor")
    if not isinstance(request, dict) or type(request.get("id")) is not int or (executor is not None and not isinstance(executor, dict)):
        raise ValueError("campaign retained terminal original attempts are missing")
    authority = {"historical_terminal_review": True, "repository": "buster14a/buster", "request": request, "executor": executor,
                 "request_id": str(request["id"]), "run_id": "-" if executor is None else str(executor.get("id")),
                 "head": request.get("head_sha"), "facts": record.get("facts"), "admitted": record.get("native_review"),
                 "historical_context_revision": record.get("historical_context_revision"), "selected_physical_job": record.get("job")}
    prefix = "sampling" if phase in ("acquire", "pilot", "confirm") else phase
    authority = campaign_restore_current(authority, record.get("current_transport"), prefix)
    terminal_raw = campaign_transport_decode(record.get("terminal_proof"))
    value = record.get("api_envelope")
    if not isinstance(value, str) or len(value) > 12 * 1024 * 1024:
        raise ValueError("campaign retained terminal API envelope encoding is missing or oversized")
    try:
        envelope = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ValueError("campaign retained terminal API envelope encoding is invalid") from error
    if not 0 < len(envelope) <= 8 * 1024 * 1024 or base64.b64encode(envelope).decode("ascii") != value:
        raise ValueError("campaign retained terminal API envelope encoding changed")
    parsed = sampling_json({"envelope.json": envelope}, "envelope.json")
    selection = parsed.get("artifact_inventory_selection")
    if not isinstance(selection, dict) or not isinstance(parsed.get("api_observations"), dict):
        raise ValueError("campaign retained terminal artifact inventory is missing")
    rows = []
    if executor is not None:
        for page in range(1, 11):
            path = f"/actions/runs/{authority['run_id']}/artifacts?per_page=100&page={page}"
            if path not in parsed["api_observations"]:
                break
            value = parsed["api_observations"][path]
            if not isinstance(value, dict) or not isinstance(value.get("artifacts"), list):
                raise ValueError("campaign retained terminal artifact API page changed")
            rows.extend(value["artifacts"])
    selected_id = selection.get("selected_id")
    selected = next((row for row in rows if isinstance(row, dict) and row.get("id") == selected_id), None)
    authority.update(terminal_artifact_inventory=rows, selected_artifact=selected,
                     expected_artifact_name=selection.get("expected_name"), artifact_inventory_complete=selection.get("complete"))
    authority["historical_records"].update(terminal=terminal_raw, envelope=envelope)
    authority.update(terminal_proof=sampling_tsv(terminal_raw), terminal_api_envelope=envelope,
                     terminal_api_sha256=hashlib.sha256(envelope).hexdigest())
    return authority


def campaign_assemble_facts(ingested: list[dict | None]) -> dict:
    """Join retained immutable ingestion data; no API, ZIP reads or feature eligibility."""
    from sampling_qualification_receipt import schedule
    if not isinstance(ingested, list) or len(ingested) != 46:
        raise ValueError("campaign requires exactly 44 sampling and two prerequisite slots")
    planned = [("acquire", 0), *(("pilot", index) for index in range(3)),
               *(("confirm", index) for index in range(40)), ("preparation", 0), ("utility", 0)]
    rows, saved, replays, manifests, validations = [], [], {}, {}, {}
    seen_request, seen_executor, seen_artifact = set(), set(), set()
    measurement = None
    for index, (phase, packet) in enumerate(planned):
        item = ingested[index]
        plan = schedule(phase, packet) if index < 44 else {"family": phase}
        if item is None:
            row = campaign_not_run(phase, packet, plan["family"])
            archived = campaign_archive_row(row, None)
        else:
            if not isinstance(item, dict) or item.get("schema") not in ("buster-compiler-campaign-ingestion-v1", "buster-compiler-campaign-terminal-ingestion-v1") or \
                    item.get("phase") != phase or type(item.get("packet")) is not int or item["packet"] != packet or \
                    item.get("qualification_state") != "unqualified" or item.get("physical_qualification") is not False or \
                    item.get("archive_storage_assessed") is not False:
                raise ValueError("campaign retained ingestion belongs to another phase or claims authority")
            row, replay, manifest = item.get("fact"), item.get("raw_replay"), item.get("raw_manifest")
            if not isinstance(row, dict) or not isinstance(replay, bytes) or not isinstance(manifest, bytes) or \
                    not 0 < len(manifest) <= ANALYZER_MEMBER_LIMIT or hashlib.sha256(replay).hexdigest() != row.get("raw_replay_sha256"):
                raise ValueError("campaign retained raw replay or manifest identity changed")
            if item["schema"] == "buster-compiler-campaign-terminal-ingestion-v1":
                record = sampling_json({"replay.json": replay}, "replay.json", max_bytes=CAMPAIGN_REPLAY_LIMIT)
                authority = campaign_restore_terminal(record, phase, packet)
                artifact = record.get("verified_artifact")
                if artifact is not None:
                    if not isinstance(artifact, dict) or not isinstance(artifact.get("verified_member_manifest"), str) or \
                            len(artifact["verified_member_manifest"]) > 45 * 1024 * 1024:
                        raise ValueError("campaign retained terminal ZIP manifest is missing or oversized")
                    artifact = dict(artifact)
                    try:
                        raw_manifest = base64.b64decode(artifact["verified_member_manifest"], validate=True)
                    except (ValueError, binascii.Error) as error:
                        raise ValueError("campaign retained terminal ZIP manifest encoding changed") from error
                    if len(raw_manifest) > ANALYZER_MEMBER_LIMIT or base64.b64encode(raw_manifest).decode("ascii") != artifact["verified_member_manifest"]:
                        raise ValueError("campaign retained terminal ZIP manifest is noncanonical")
                    artifact["verified_member_manifest"] = raw_manifest
                expected, rebuilt, rebuilt_manifest = campaign_terminal_data(phase, packet, authority, artifact)
                if row != expected or replay != rebuilt or manifest != rebuilt_manifest:
                    raise ValueError("campaign terminal compact data differs from retained original API/native proof")
            else:
                record = sampling_json({"replay.json": replay}, "replay.json")
                if record.get("phase") != phase or type(record.get("packet")) is not int or record["packet"] != packet or \
                        record.get("member_manifest_sha256") != hashlib.sha256(manifest).hexdigest():
                    raise ValueError("campaign retained raw member manifest differs from original ingestion")
                authority = {"historical_review": True, "repository": "buster14a/buster",
                             "request": record["request"], "executor": record["executor"],
                             "request_id": str(record["request"]["id"]), "run_id": str(record["executor"]["id"]),
                             "head": record["request"]["head_sha"],
                             "facts": record["facts"], "admitted": record["native_review"],
                             "freeze": {"campaign_parent": record.get("parent_freeze_sha256", "-")}}
                artifact = {"id": record["artifact_id"], "verified_zip_sha256": record["artifact_sha256"],
                            "verified_zip_bytes": record["artifact_bytes"],
                            "verified_member_manifest_sha256": record["member_manifest_sha256"]}
                campaign_original_identity(phase, packet, authority, artifact)
                if row.get("state") == "complete":
                    authority = campaign_restore_transport(authority, record.get("historical_transport"), manifest,
                                                           "sampling" if phase in ("acquire", "pilot", "confirm") else phase)
                    expected, rebuilt = campaign_complete_fact(phase, packet, authority, artifact, record["validation"])
                    if row != expected or replay != rebuilt:
                        raise ValueError("campaign compact facts differ from retained original raw validation")
                    validations[phase, packet] = record["validation"], row
                elif row.get("state") == "invalid":
                    authority = campaign_restore_current(authority, record.get("current_transport"),
                                                         "sampling" if phase in ("acquire", "pilot", "confirm") else phase)
                    if record.get("schema") != "buster-compiler-campaign-invalid-replay-v1" or \
                            record.get("qualification_state") != "unqualified" or record.get("execution_authority") is not False or \
                            row.get("native_wall_us") != "-" or row.get("slots") != "-" or row.get("slot_validations") != "-":
                        raise ValueError("campaign invalid raw data claims a complete measurement")
                    expected = campaign_invalid_row(phase, packet, authority, artifact, record.get("job"))
                    expected["raw_replay_sha256"] = hashlib.sha256(replay).hexdigest()
                    if row != expected:
                        raise ValueError("campaign invalid row differs from retained original API data")
                else:
                    raise ValueError("campaign ingestion state needs its separate native terminal data proof")
            for key, seen in (("request_run", seen_request), ("executor_run", seen_executor), ("artifact_id", seen_artifact)):
                if row[key] == "-":
                    continue
                if row[key] in seen:
                    raise ValueError("campaign original attempt or immutable artifact appears twice")
                seen.add(row[key])
            if measurement is not None and row["measurement_revision"] != measurement:
                raise ValueError("campaign measurement revision changed across original attempts")
            measurement = row["measurement_revision"]
            archived = item.get("archive")
            if not isinstance(archived, dict):
                raise ValueError("campaign retained archive join is missing")
            if archived.get("archive_kind") == "-":
                if archived != campaign_archive_row(row, None):
                    raise ValueError("campaign unavailable archive join changed identity")
            else:
                archived = campaign_archive_row(row, archived)
            replays[phase + "-" + str(packet) + ".json"] = replay
            manifests[phase + "-" + str(packet) + ".manifest.tsv"] = manifest
        rows.append(row)
        saved.append(archived)
    criteria = b""
    if ("preparation", 0) in validations and ("utility", 0) in validations:
        preparation, prep_row = validations["preparation", 0]
        utility, utility_row = validations["utility", 0]
        criterion = {"schema": "buster-compiler-main-profile-criteria-v1", "measurement_revision": measurement,
                     "preparation_plan_sha256": prep_row["freeze_sha256"], "preparation_raw_replay_sha256": prep_row["raw_replay_sha256"],
                     "utility_plan_sha256": utility_row["freeze_sha256"], "utility_raw_replay_sha256": utility_row["raw_replay_sha256"],
                     **campaign_criteria_fragment("preparation", preparation), **campaign_criteria_fragment("utility", utility)}
        if set(criterion) != set(CAMPAIGN_CRITERIA_FIELDS):
            raise ValueError("campaign retained prerequisite criteria are incomplete")
        criteria = b"".join((key + "\t" + criterion[key] + "\n").encode("ascii") for key in CAMPAIGN_CRITERIA_FIELDS)
    return {"schema": "buster-compiler-campaign-data-v1", "qualification_state": "unqualified", "physical_qualification": False,
            "facts": campaign_table(CAMPAIGN_FACT_FIELDS, rows), "archive": campaign_table(CAMPAIGN_ARCHIVE_FIELDS, saved),
            "criteria": criteria, "raw_replays": replays, "raw_manifests": manifests,
            "archive_storage_assessed": False, "native_inventory_review_required": True}


def sampling_observed_costs(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    """Show unavailable and failed measurements honestly, without zero filling."""
    observed = {"physical_packet_wall_us": None, "owner_wall_us": None, "prep_us": None,
                "physical_job_wall_us": None, "physical_job_wall_upper_us": None, "queue_delay_seconds": None}
    for name, mappings in (("packet.tsv", (("physical_packet_wall_us", "physical_packet_wall_us"), ("prep_us", "prep_us"))),
                           ("owner.tsv", (("physical_packet_wall_us", "owner_wall_us"),))):
        try:
            row = sampling_tsv(files.get(name))
            for source, target in mappings:
                observed[target] = sampling_integer(row.get(source), target != "prep_us")
        except (ValueError, UnicodeError, TypeError):
            pass
    try:
        job = sampling_host_job(api, authority)
        observed["platform_job_state"] = job.get("status")
        observed["platform_job_conclusion"] = job.get("conclusion")
        stamps = [datetime.fromisoformat(job[key].replace("Z", "+00:00")) for key in ("created_at", "started_at", "completed_at")]
        if all(stamp.utcoffset() is not None for stamp in stamps):
            wall = round((stamps[2] - stamps[1]).total_seconds() * 1000000)
            queue = (stamps[1] - stamps[0]).total_seconds()
            if wall > 0 and queue >= 0:
                observed.update(physical_job_wall_us=wall, physical_job_wall_upper_us=wall + 2000000,
                                queue_delay_seconds=queue)
    except (ValueError, KeyError, TypeError, AttributeError, OverflowError, OSError, urllib.error.URLError, TimeoutError):
        pass
    return observed


def sampling_publish(environment: dict) -> int:
    """Bounded hosted publication; all evidence remains data and unqualified."""
    api, authority = sampling_authority(environment)
    admitted = authority["admitted"]
    files, artifact = {}, {}
    result = {"schema": "buster-main-sampling-publication-v1", "packet_state": "incomplete",
              "qualification_state": "unqualified", "evidence_class": "unqualified-sampling-research",
              "routine_profile_enabled": False, "phase": admitted["sampling_phase"],
              "packet": sampling_integer(admitted["sampling_packet"]), "family": admitted["sampling_family"],
              "reservation_seconds": sampling_integer(admitted["sampling_reservation_seconds"], True),
              "problems": []}
    try:
        files, artifact = sampling_read_artifact(api, authority)
        result = sampling_validate(api, authority, files)
    except (OSError, ValueError, UnicodeError, TypeError, KeyError, IndexError, AttributeError, RecursionError,
            urllib.error.URLError, TimeoutError) as error:
        result["problems"].append(("evidence validation failed: " + type(error).__name__ + ": " + str(error))[:1000])
        result["accounting"] = sampling_observed_costs(api, authority, files)
        result["authenticated_attempt_history"] = list(authority["history"]) + [{
            "phase": admitted["sampling_phase"], "packet": result["packet"], "request_run_id": authority["request_id"],
            "run_id": authority["run_id"], "run_attempt": "1", "state": "incomplete",
            "reservation_seconds": result["reservation_seconds"],
            "actions_job_occupancy_us": result["accounting"]["physical_job_wall_upper_us"],
            "physical_packet_wall_us": result["accounting"]["physical_packet_wall_us"],
            "campaign": admitted["sampling_freeze_sha256"], "freeze_revision": admitted["sampling_freeze_revision"]}]
    success = result["packet_state"] == "complete-valid-research" and not result["problems"]
    conclusion = "success" if success else "failure"
    title = "Valid unqualified sampling packet" if success else "Incomplete unqualified sampling packet"
    workflow_url = run_url(authority["repository"], authority["run_id"], "1")
    summary = ("Unqualified sampling research; routine profile remains disabled.\n\n"
               "Lifecycle protocol: sampling-terminal-native-v1.\n"
               f"Phase {admitted['sampling_phase']}, packet {admitted['sampling_packet']}; "
               f"whole physical-job reservation {admitted['sampling_reservation_seconds']} seconds.\n"
               f"Packet state: {result['packet_state']}. Qualification state: unqualified.\n\n" +
               f"Request run {authority['request_id']} attempt 1: {run_url(authority['repository'], authority['request_id'], '1')}\n" +
               f"Workflow run {authority['run_id']} attempt 1: {workflow_url}\n\n")
    if result["problems"]:
        summary += "Evidence problems:\n" + "\n".join("- " + str(problem)[:1000] for problem in result["problems"][:30]) + "\n\n"
    fence = chr(96) * 3
    summary += "Observed accounting:\n" + fence + "json\n" + json.dumps(result.get("accounting", {}), sort_keys=True) + "\n" + fence + "\n"
    if artifact:
        summary += "\nEvidence: " + artifact_link(authority["repository"], authority["run_id"], artifact) + "\n"
    report = json.dumps(result, sort_keys=True, indent=2)
    row = sampling_write(api, authority, {"status": "completed", "conclusion": conclusion, "details_url": workflow_url,
                         "output": {"title": title, "summary": summary[:TEXT_LIMIT],
                                    "text": fence + "json\n" + report[:TEXT_LIMIT - 16] + "\n" + fence}})
    with open(environment.get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
        stream.write(summary + "\n")
    print(f"COMPILER_SAMPLING_PUBLISHED {conclusion} check={row.get('id')} state={row.get('status')} qualification=unqualified")
    return 0 if success and row.get("status") == "completed" and row.get("conclusion") == "success" else 1



# Distinct preparation research remains disabled until its reviewed native
# admission admits the sole predeclared request. These are API/data adapters,
# not a second preparation controller: artifact bytes never become commands.
PREPARATION_CHECK_NAME = "9700X compiler preparation research"
PREPARATION_HOST_JOB = "Compiler preparation qualification"
PREPARATION_ARCHIVE_LIMIT = 2 << 30
PREPARATION_MEMBER_LIMIT = 8 << 20
PREPARATION_FILE_LIMIT = 65536


def preparation_authority(environment: dict) -> tuple[Api, dict]:
    import subprocess
    import tempfile
    from pathlib import Path
    import authorize as direct_authorize
    repository, head = environment.get("BQ_REPOSITORY", ""), environment.get("BQ_HEAD_COMMIT", "")
    request_id, run_id = environment.get("BQ_REQUEST_RUN_ID", ""), environment.get("BQ_RUN_ID", "")
    if repository != "buster14a/buster" or not SHA.fullmatch(head) or \
            any(not DECIMAL.fullmatch(value) for value in (request_id, run_id)) or \
            environment.get("BQ_RUN_ATTEMPT") != "1" or environment.get("BQ_REQUEST_ATTEMPT") != "1" or \
            environment.get("GITHUB_RUN_ID") != run_id or environment.get("GITHUB_RUN_ATTEMPT") != "1" or \
            environment.get("GITHUB_REPOSITORY") != repository or not environment.get("GH_TOKEN"):
        raise ValueError("preparation publication lacks exact trusted workflow inputs")
    api = Api(repository, environment["GH_TOKEN"])
    execution = api.request(f"/actions/runs/{run_id}")
    if not isinstance(execution, dict) or str(execution.get("id")) != run_id or \
            execution.get("run_attempt") != 1 or execution.get("path") != BENCH_WORKFLOW or \
            execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or \
            not isinstance(execution.get("repository"), dict) or execution["repository"].get("full_name") != repository or \
            direct_authorize.full_name(execution.get("head_repository")) != repository or \
            direct_authorize.identity(execution.get("actor")) != direct_authorize.MAINTAINER or \
            direct_authorize.identity(execution.get("triggering_actor")) != direct_authorize.MAINTAINER or \
            execution.get("head_sha") != environment.get("GITHUB_SHA") or \
            execution.get("display_title") != f"9700X request {request_id}.1 head {head}":
        raise ValueError("preparation executor workflow provenance is unavailable")
    request = api.request(f"/actions/runs/{request_id}")
    pulls = api.request(f"/commits/{head}/pulls?per_page=100")
    problems, unused_base = direct_authorize.verify(repository, int(request_id), head, request, pulls)
    if problems:
        raise ValueError("preparation request ownership failed: " + ", ".join(problems))
    commit = api.request(f"/commits/{head}")
    parents = commit.get("parents") if isinstance(commit, dict) else None
    if not isinstance(parents, list) or not 1 <= len(parents) <= 2 or any(
            not isinstance(row, dict) or not SHA.fullmatch(str(row.get("sha", ""))) for row in parents):
        raise ValueError("preparation request has an unsupported parent inventory")
    compared = [api.request(f"/compare/{row['sha']}...{head}") for row in parents]
    marker = direct_authorize.sampling_content(repository, direct_authorize.COMPARE_REQUEST, head, environment["GH_TOKEN"])
    selected = direct_authorize.preparation_fresh_selector(marker, compared)
    if selected is None:
        raise ValueError("preparation selector is not fresh against every Git parent")
    pull = next(row for row in pulls if isinstance(row, dict) and row.get("state") == "open" and
                isinstance(row.get("head"), dict) and row["head"].get("sha") == head)
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="preparation-publication-") as temporary:
        directory = Path(temporary) / "admission"
        if not direct_authorize.preparation_data(repository, environment["GH_TOKEN"], request, pull, head, "1",
                                                marker, compared, directory):
            raise ValueError("preparation native admission data is unavailable")
        output = directory / "admitted.env"
        cleanup, workspace = environment.get("RUNNER_TEMP"), environment.get("GITHUB_WORKSPACE")
        if not cleanup or not workspace:
            raise ValueError("preparation hosted cleanup roots are unavailable")
        command = [str(root / "build.sh"), "compiler_profile_qualification", "--admit-preparation",
                   *(str(directory / name) for name in ("allowlist.tsv", "request.txt", "facts.tsv", "history.tsv", "plan.tsv")),
                   cleanup, workspace, str(output)]
        child_environment = {key: value for key, value in environment.items() if key not in ("GH_TOKEN", "GITHUB_TOKEN")}
        try:
            result = subprocess.run(command, cwd=root, env=child_environment, timeout=120, check=False,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired as error:
            raise ValueError("trusted native preparation admission exceeded its bounded hosted pass") from error
        if result.returncode != 0 or not output.is_file() or output.stat().st_size > 16384:
            raise ValueError("trusted native preparation admission refused publication")
        admitted = {}
        for line in output.read_text(encoding="ascii").splitlines():
            key, separator, value = line.partition("=")
            if not separator or not re.fullmatch(r"preparation_[a-z][a-z0-9_]*", key) or key in admitted or not value or \
                    any(ord(char) < 32 or ord(char) > 126 for char in value):
                raise ValueError("native preparation admission output is ambiguous")
            admitted[key] = value
        names = ("phase", "packet", "family", "reservation_seconds", "worker_seconds", "timeout_minutes",
                 "plan_revision", "plan_sha256", "protocol_sha256", "base", "base_tree",
                 "candidate_revision", "candidate_tree", "trusted_revision")
        if set(admitted) != {"preparation_admitted", *("preparation_" + name for name in names)} or \
                admitted.get("preparation_admitted") != "true" or any(
                    admitted["preparation_" + name] != environment.get("BQ_PREPARATION_" + name.upper()) for name in names):
            raise ValueError("preparation identity contradicts freshly repeated native admission")
        raw = {name: (directory / name).read_bytes() for name in
               ("request.txt", "plan.tsv", "allowlist.tsv", "facts.tsv", "history.tsv")}
        if hashlib.sha256(raw["plan.tsv"]).hexdigest() != admitted["preparation_plan_sha256"]:
            raise ValueError("committed preparation plan differs from native admission digest")
        authority = {"admitted": admitted, "plan": sampling_tsv(raw["plan.tsv"]), "raw": raw,
                     "history": sampling_tsv(raw["history.tsv"], True), "facts": sampling_tsv(raw["facts.tsv"]),
                     "request": request, "executor": execution, "request_line": selected[0],
                     "repository": repository, "head": head, "request_id": request_id, "run_id": run_id}
    return api, authority


def preparation_check_marker(authority: dict) -> str:
    return ("buster-compiler-preparation-v1:" + authority["admitted"]["preparation_plan_sha256"] +
            ":qualify:0:" + authority["request_id"] + ":" + authority["run_id"] + ":1")


def preparation_owned(row: object, authority: dict) -> bool:
    from compiler_github import GITHUB_ACTIONS_APP_ID
    return isinstance(row, dict) and type(row.get("id")) is int and row["id"] > 0 and \
        row.get("name") == PREPARATION_CHECK_NAME and row.get("head_sha") == authority["head"] and \
        row.get("external_id") == preparation_check_marker(authority) and isinstance(row.get("app"), dict) and \
        row["app"].get("id") == GITHUB_ACTIONS_APP_ID and row.get("status") in ("queued", "in_progress", "completed")


def preparation_checks(api: Api, authority: dict) -> list[dict]:
    from compiler_github import GITHUB_ACTIONS_APP_ID
    query = urllib.parse.urlencode({"check_name": PREPARATION_CHECK_NAME, "filter": "all", "app_id": GITHUB_ACTIONS_APP_ID})
    rows = api.pages(f"/commits/{authority['head']}/check-runs?{query}", "check_runs")
    owned = [row for row in rows if preparation_owned(row, authority)]
    if len(owned) > 1:
        raise ValueError("preparation attempt has duplicate owned checks")
    return owned


def preparation_write(api: Api, authority: dict, fields: dict) -> dict:
    from compiler_github import write_check
    rows = preparation_checks(api, authority)
    if rows:
        return write_check(api, rows[0], fields)
    body = dict(fields, name=PREPARATION_CHECK_NAME, head_sha=authority["head"],
                external_id=preparation_check_marker(authority))
    try:
        written = write_check(api, None, body)
        if preparation_owned(written, authority):
            return written
    except (urllib.error.URLError, TimeoutError, ValueError):
        pass
    rows = preparation_checks(api, authority)
    if len(rows) != 1:
        raise ValueError("preparation check creation is ambiguous; no duplicate write or host work authorized")
    return rows[0]


def preparation_summary(authority: dict) -> str:
    return ("Unqualified preparation research; default preparation remains disabled.\n\n"
            "Lifecycle protocol: preparation-terminal-native-v1.\n"
            "Phase qualify, packet 0; whole physical-job reservation 5400 seconds.\n\n" +
            f"Request run {authority['request_id']} attempt 1: {run_url(authority['repository'], authority['request_id'], '1')}\n" +
            f"Workflow run {authority['run_id']} attempt 1: {run_url(authority['repository'], authority['run_id'], '1')}\n")


def preparation_queue(environment: dict) -> int:
    api, authority = preparation_authority(environment)
    row = preparation_write(api, authority, {"status": "queued",
        "details_url": run_url(authority["repository"], authority["run_id"], "1"),
        "output": {"title": "Queued unqualified preparation research", "summary": preparation_summary(authority)}})
    if row.get("status") != "queued":
        raise ValueError("preparation queue is already running or terminal; no new physical assignment authorized")
    print(f"COMPILER_PREPARATION_QUEUED check={row.get('id')} state={row.get('status')} qualification=unqualified")
    return 0


def preparation_archive(payload: bytes) -> dict[str, bytes]:
    """Bound ZIP/ZIP64 counts before allocation; retain every regular raw member."""
    import struct
    import zlib
    try:
        if not isinstance(payload, bytes) or not 0 < len(payload) <= PREPARATION_ARCHIVE_LIMIT:
            raise ValueError("preparation archive is missing or oversized")
        end = payload.rfind(b"PK\x05\x06", max(0, len(payload) - 65557))
        if end < 0 or end + 22 > len(payload):
            raise ValueError("preparation archive has no bounded ZIP directory")
        signature, disk, directory_disk, disk_count, count, size, offset, comment = struct.unpack_from("<4s4H2LH", payload, end)
        directory_end = end
        if signature != b"PK\x05\x06" or disk or directory_disk or end + 22 + comment != len(payload):
            raise ValueError("preparation ZIP is multipart or has trailing bytes")
        if size == 0xffffffff or offset == 0xffffffff:
            raise ValueError("preparation ZIP64 sizes/offsets exceed the declared archive bound")
        if count == 0xffff or disk_count == 0xffff:
            if end < 20:
                raise ValueError("preparation ZIP64 locator is missing")
            magic, zip_disk, record_offset, disks = struct.unpack_from("<4sLQL", payload, end - 20)
            if magic != b"PK\x06\x07" or zip_disk or disks != 1 or record_offset + 56 != end - 20:
                raise ValueError("preparation ZIP64 locator is malformed")
            fields = struct.unpack_from("<4sQ2H2L4Q", payload, record_offset)
            if fields[0] != b"PK\x06\x06" or fields[1] != 44 or fields[4] or fields[5] or fields[6] != fields[7]:
                raise ValueError("preparation ZIP64 directory is multipart or unsupported")
            if fields[8] != size or fields[9] != offset:
                raise ValueError("preparation ZIP64 count record changes the bounded directory")
            disk_count, count = fields[6:8]
            directory_end = record_offset
        if disk_count != count or not 0 < count <= PREPARATION_FILE_LIMIT + 128 or size > 32 << 20 or \
                offset + size != directory_end:
            raise ValueError("preparation ZIP directory exceeds its complete bounded population")
        result, aliases, expanded = {}, set(), 0
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            entries = archive.infolist()
            if len(entries) != count:
                raise ValueError("preparation ZIP entry count differs from directory")
            for entry in entries:
                name = entry.filename
                alias = member_identity(name)
                components = name.rstrip("/").split("/")
                if entry.orig_filename != name or not name or len(name) > 512 or "\\" in name or name.startswith("/") or \
                        any(part in ("", ".", "..") for part in components) or \
                        any(ord(char) < 32 or ord(char) > 126 for char in name) or alias in aliases:
                    raise ValueError("preparation ZIP has an unsafe or duplicate member")
                aliases.add(alias)
                kind = stat.S_IFMT(entry.external_attr >> 16)
                if entry.flag_bits & 1 or entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) or \
                        kind not in ((0, stat.S_IFDIR) if entry.is_dir() else (0, stat.S_IFREG)):
                    raise ValueError("preparation ZIP has an encrypted or nonregular member")
                if entry.is_dir():
                    if entry.file_size:
                        raise ValueError("preparation ZIP directory carries data")
                    continue
                expanded += entry.file_size
                if len(result) >= PREPARATION_FILE_LIMIT or not 0 <= entry.file_size <= PREPARATION_MEMBER_LIMIT or \
                        expanded > PREPARATION_ARCHIVE_LIMIT:
                    raise ValueError("preparation ZIP exceeds its raw file/member/total bound")
                raw = archive.read(entry)
                if len(raw) != entry.file_size:
                    raise ValueError("preparation ZIP member length differs from directory")
                result[name] = raw
        return result
    except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError, struct.error) as error:
        raise ValueError("preparation archive compression or directory data is corrupt") from error


def preparation_read_artifact(api: Api, authority: dict, *, retain_archive_identity: bool = False) -> tuple[dict[str, bytes], dict]:
    name = "buster-9700x-preparation-" + authority["head"] + "-1"
    listing = api.request(f"/actions/runs/{authority['run_id']}/artifacts?" +
                          urllib.parse.urlencode({"name": name, "per_page": 10}))
    rows = listing.get("artifacts") if isinstance(listing, dict) else None
    if not isinstance(rows, list) or len(rows) >= 10:
        raise ValueError("preparation artifact inventory is unavailable or capped")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == name]
    if len(matches) != 1:
        raise ValueError("preparation attempt has no unique evidence artifact")
    row = matches[0]
    origin = row.get("workflow_run")
    if type(row.get("id")) is not int or row["id"] <= 0 or row.get("expired") is not False or \
            type(row.get("size_in_bytes")) is not int or not 0 < row["size_in_bytes"] <= PREPARATION_ARCHIVE_LIMIT or \
            not isinstance(origin, dict) or str(origin.get("id")) != authority["run_id"] or \
            origin.get("head_sha") != authority["executor"].get("head_sha"):
        raise ValueError("preparation artifact is expired, oversized or belongs to another executor")
    payload = api.download(api.prefix + f"/actions/artifacts/{row['id']}/zip", max_bytes=PREPARATION_ARCHIVE_LIMIT)
    files = preparation_archive(payload)
    return files, campaign_artifact_identity(payload, row, files, retain_archive_identity)


def preparation_expected(api: Api, authority: dict, files: dict[str, bytes]) -> tuple[dict, dict]:
    from pathlib import Path
    from compiler_preparation import absolute
    from sampling_qualification_receipt import _lab
    plan, admitted = authority["plan"], authority["admitted"]
    expected = {"base": plan["baseline_revision"], "base_tree": plan["baseline_tree"],
                "head": plan["candidate_revision"], "head_tree": plan["candidate_tree"],
                "root": plan["source_root"], "output": plan["output_root"],
                "python": plan["python_path"], "python_sha256": plan["python_sha256"],
                "trusted_lab_sha256": plan["lab_sha256"],
                "trusted_lab": plan["trusted_root"] + "/tools/uarch_lab.py"}
    for role in ("base", "head"):
        row = api.request("/git/commits/" + expected[role])
        if not isinstance(row, dict) or row.get("sha") != expected[role] or \
                not isinstance(row.get("tree"), dict) or row["tree"].get("sha") != expected[role + "_tree"]:
            raise ValueError("preparation immutable GitHub source tree contradicts the committed plan")
    for path, key in (("tools/uarch_lab.py", "lab_sha256"),
                      ("docs/compiler-preparation-qualification-v1.md", "protocol_sha256")):
        row = api.request("/contents/" + path + "?ref=" + plan["trusted_revision"])
        if not isinstance(row, dict) or row.get("type") != "file" or row.get("encoding") != "base64" or \
                type(row.get("size")) is not int or not 0 < row["size"] <= 1024 * 1024 or not isinstance(row.get("content"), str):
            raise ValueError("preparation trusted source is missing or oversized")
        raw = base64.b64decode(row["content"].replace("\n", ""), validate=True)
        if len(raw) != row["size"] or hashlib.sha256(raw).hexdigest() != plan[key]:
            raise ValueError("preparation trusted source differs from its predeclared identity")
    raw_lab = Path(_lab.__file__).read_bytes()
    if not 0 < len(raw_lab) <= 1024 * 1024 or hashlib.sha256(raw_lab).hexdigest() != plan["lab_sha256"]:
        raise ValueError("preparation statistical replay module differs from the committed trusted lab source")
    host = sampling_json(files, "host.json")
    wanted = {"schema": "buster-compiler-preparation-host-v1", "state": "complete",
              "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor", "observed_from": "/proc/cpuinfo",
              "request_head": authority["head"], "run_id": authority["run_id"], "run_attempt": "1",
              "request_run_id": authority["request_id"], "measurement_trusted_revision": plan["trusted_revision"],
              "policy_trusted_revision": authority["executor"]["head_sha"],
              "plan_revision": admitted["preparation_plan_revision"], "plan_sha256": admitted["preparation_plan_sha256"],
              "protocol_sha256": plan["protocol_sha256"], "trusted_lab_sha256": plan["lab_sha256"],
              "python": plan["python_path"], "python_sha256": plan["python_sha256"],
              "native_driver_sha256": plan["native_driver_sha256"], "trusted_lab": expected["trusted_lab"]}
    if set(host) != set(wanted) | {"logical_processor_records", "native_driver"} or \
            any(type(host.get(key)) is not type(value) or host.get(key) != value for key, value in wanted.items()) or \
            type(host.get("logical_processor_records")) is not int or not 0 < host["logical_processor_records"] <= 4096 or \
            not absolute(host.get("trusted_lab")) or not host["trusted_lab"].endswith("/tools/uarch_lab.py") or \
            not absolute(host.get("native_driver")):
        raise ValueError("preparation actual host/tool observation or workflow bindings are incomplete")
    trusted_root = plan["trusted_root"]
    if not absolute(trusted_root) or not host["native_driver"].startswith(trusted_root + "/"):
        raise ValueError("preparation native driver is outside the pinned trusted harness")
    return expected, host


def preparation_bundles(files: dict[str, bytes]) -> tuple[dict, dict]:
    from compiler_preparation import SERIES
    receipt = sampling_json(files, "qualification/qualification.json")
    bundles = {}
    for arm in ("legacy", "snapshot"):
        prefix = "qualification/" + arm + "/"
        immediate = {name[len(prefix):]: raw for name, raw in files.items()
                     if name.startswith(prefix) and "/" not in name[len(prefix):]}
        prepared = sampling_json(files, prefix + "prepared.json")
        bundle = {"prepared": prepared, "manifest": immediate.get("prepared.manifest.tsv"),
                  "workload": immediate.get("prepared.workload.tsv"), "ledger": immediate.get("phases.tsv"), "files": immediate}
        if arm == "snapshot":
            bundle["closure"] = {op: sampling_json(files, prefix + "closure-" + op + ".json")
                                 for op in ("snapshot", "restore", "verify")}
            bundle["closure_manifests"] = {op: immediate.get("closure-" + op + ".json.manifest.tsv")
                                          for op in bundle["closure"]}
        for member, name, unused_same_source in SERIES:
            if member == arm:
                lab, corpus = prefix + name + "-lab/", prefix + name + "-throughput/"
                bundle[name] = {"lab": sampling_json(files, lab + "compare.json"),
                                "summary": sampling_json(files, lab + "summary.json"),
                                "pairs": sampling_json(files, lab + "pairs.json", False),
                                "throughput": sampling_json(files, corpus + "summary.json"),
                                "metadata": sampling_json(files, corpus + "metadata.json"),
                                "throughput_raw": files.get(corpus + "summary.json"),
                                "metadata_raw": files.get(corpus + "metadata.json")}
                # Preserve data from every declared stream, including empty
                # command/error logs. No summary can stand in for missing raw.
                for suffix in ("a/lab.json", "b/lab.json"):
                    sampling_json(files, lab + suffix)
                for suffix in ("samples.csv", "telemetry.csv", "jobs.tsv", "commands.jsonl", "capabilities.jsonl", "complete.txt"):
                    raw = files.get(corpus + suffix)
                    if not isinstance(raw, bytes) or len(raw) > PREPARATION_MEMBER_LIMIT:
                        raise ValueError("preparation complete corpus raw log/marker is missing")
        bundles[arm] = bundle
    return receipt, bundles


def preparation_series_replay(row: dict, expected: dict, same_source: bool) -> dict:
    """Use the trusted historical statistics on independently retained pairs."""
    from sampling_qualification_receipt import _lab
    raw, summary, pairs = row["lab"], row["summary"], row["pairs"]
    plan = raw.get("plan")
    count = plan.get("pairs") if isinstance(plan, dict) else None
    if type(count) is not int or not 10 <= count <= 1000 or count % 2 or not isinstance(pairs, list) or len(pairs) != count * 2:
        raise ValueError("preparation raw paired population is incomplete or exceeds the immutable profile")
    if summary.get("plan") != dict(plan, seed=20261003, confidence=0.95, bootstrap_resamples=2000,
                                  complete_pairs=count, fresh_copy=True) or \
            summary.get("cpu") != 2 or summary.get("command") != expected["command"] or \
            summary.get("repo_root") != expected["root"] or \
            summary.get("steps") != {"env": "ok", "prepare": "ok", "timed": "ok"}:
        raise ValueError("preparation raw inference/count/workload settings differ from the saved summary")
    steps = raw.get("steps")
    if not isinstance(steps, dict) or set(steps) != {"env", "prepare", "timed"} or any(
            not isinstance(step, dict) or step.get("status") != "ok" for step in steps.values()):
        raise ValueError("preparation raw comparison has incomplete required steps")
    host = summary.get("host")
    if not isinstance(host, dict) or host.get("cpu_model") != "AMD Ryzen 7 9700X 8-Core Processor" or \
            host.get("git_revision") != expected["base"]:
        raise ValueError("preparation measured lab host or workload source revision changed")
    for role in ("baseline", "candidate"):
        item = summary.get(role)
        if not isinstance(item, dict) or item.get("runs") != count or item.get("identical_runs") != count:
            raise ValueError("preparation deterministic-output counts do not cover every declared pair")
    grouped = []
    for index in range(count):
        order = "AB" if (index + 1) % 2 else "BA"
        members = {}
        for item, variant in zip(pairs[index * 2:index * 2 + 2], order.lower()):
            if not isinstance(item, dict) or item.get("pair") != index + 1 or item.get("order") != order or \
                    item.get("variant") != variant or type(item.get("exit")) is not int or item["exit"] != 0 or \
                    item.get("identical") is not True or type(item.get("span_s")) not in (int, float) or \
                    not 1e-9 <= item["span_s"] <= 3600:
                raise ValueError("preparation raw pairs are missing, failed, unordered or non-deterministic")
            members[variant] = item
        grouped.append({"pair": index + 1, "order": order, "metrics_a": {"wall": members["a"]["span_s"]},
                        "metrics_b": {"wall": members["b"]["span_s"]}})
    wall = _lab.compare_series([(pair["metrics_a"]["wall"], pair["metrics_b"]["wall"]) for pair in grouped],
                               "s", "lower", 20261003, time_metric=True, floor=0.005)
    verdict = summary.get("verdict")
    metrics = summary.get("metrics")
    if not isinstance(verdict, dict) or verdict.get("n") != count or verdict.get("min_effect_percent") != 0.5 or \
            any(verdict.get(key) != wall.get(key) for key in
                ("ratio", "ci_low", "ci_high", "ci_coverage", "change_percent", "outcome")) or \
            not isinstance(metrics, dict) or metrics.get("wall") != wall or \
            summary.get("checks") != _lab.compare_checks(grouped) or \
            any(check.get("checked") is not True or check.get("flag") is not False for check in summary["checks"].values()):
        raise ValueError("preparation summary contradicts independently reconstructed wall evidence")
    return {"complete_pairs": count, "observed_timed_wall_us": sum(item["span_s"] for item in pairs) * 1000000,
            "ratio": wall["ratio"], "ci_low": wall["ci_low"],
            "ci_high": wall["ci_high"], "outcome": wall["outcome"], "phase_metrics": summary.get("phase_metrics"),
            "counters": summary.get("counters"), "phases": summary.get("phases")}


def preparation_control_failures(series: dict, pointers: dict, aa_corpora: dict) -> list[str]:
    """Assess complete controls; native-operation costs do not assess ordinary-job net savings."""
    failures = []
    for name, result in series.items():
        if name.endswith("-aa") and not 0.995 <= result["ci_low"] <= result["ci_high"] <= 1.005:
            failures.append(name + ": complete same-source A/A 95% interval lies outside [0.995,1.005]")
    for name, summary in aa_corpora.items():
        if summary.get("confirmed_regressions") != 0:
            failures.append(name + ": complete same-source corpus has confirmed regressions")
    if pointers["snapshot"]["total_us"] >= pointers["legacy"]["total_us"]:
        failures.append("complete snapshot native-operation cost is not less than legacy native-operation cost")
    return failures


def preparation_phase_proofs(authority: dict, files: dict[str, bytes], expected: dict, host: dict) -> tuple[dict, dict, dict]:
    from compiler_preparation import parse_argv
    plan_sha = authority["admitted"]["preparation_plan_sha256"]
    owner_raw = files.get("owner.tsv")
    owner = sampling_tsv(owner_raw)
    wanted = {"schema": "buster-compiler-preparation-owner-v1", "phase": "qualify", "packet": "0", "plan_sha256": plan_sha,
              "wall_scope": "entry-through-child-cleanup-before-terminal-publication", "process_state": "complete",
              "timed_out": "0", "cleanup_failed": "0", "within_reservation": "true", "cancelled": "0",
              "qualification_state": "unvalidated", "default_activated": "false"}
    if set(owner) != set(wanted) | {"physical_packet_wall_us"} or any(owner.get(key) != value for key, value in wanted.items()):
        raise ValueError("preparation owned worker is failed, cancelled, exhausted or incomplete")
    owner_wall = sampling_integer(owner["physical_packet_wall_us"], True)
    publication = sampling_tsv(files.get("owner-publication.tsv"))
    pub_wanted = {"schema": "buster-compiler-preparation-owner-publication-v1",
                  "owner_sha256": hashlib.sha256(owner_raw).hexdigest(), "scope": "entry-through-owner-publication",
                  "observation_publication_us": "unavailable", "within_reservation": "true"}
    if set(publication) != set(pub_wanted) | {"initial_scope_us", "publication_us", "observed_wall_us"} or \
            any(publication.get(key) != value for key, value in pub_wanted.items()):
        raise ValueError("preparation owner publication observation is missing or falsely assigns its final tail")
    initial = sampling_integer(publication["initial_scope_us"], True)
    pub_us = sampling_integer(publication["publication_us"])
    observed_wall = sampling_integer(publication["observed_wall_us"], True)
    if initial != owner_wall or observed_wall != owner_wall + pub_us or observed_wall > 5400 * 1000000:
        raise ValueError("preparation owner and publication whole-operation clock accounting contradict")
    terminal = sampling_tsv(files.get("preparation.tsv"))
    term_wanted = {"schema": "buster-compiler-preparation-controller-v1", "phase": "qualify", "packet": "0",
                   "plan_sha256": plan_sha, "process_state": "complete", "qualification_state": "unvalidated",
                   "default_activated": "false", "cleanup_proven": "true", "source_root": expected["root"],
                   "output_root": expected["output"], "tools_before": "true", "tools_after": "true", "exported": "true"}
    if set(terminal) != set(term_wanted) | {"duration_us"} or any(terminal.get(key) != value for key, value in term_wanted.items()):
        raise ValueError("preparation native control/source/tool/export proof is incomplete")
    duration = sampling_integer(terminal["duration_us"], True)
    if duration > owner_wall or duration > 5280 * 1000000:
        raise ValueError("preparation worker exceeds its reserved native clock")
    phases = sampling_tsv(files.get("controller.tsv"), True)
    trusted_root = host["trusted_lab"][:-len("/tools/uarch_lab.py")]
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null"]
    commands = [
        ("trusted-harness-pin", git + ["-C", trusted_root, "rev-parse", "HEAD"]),
        ("clone-preparation-source", git + ["clone", "--no-checkout", "--no-tags", "https://github.com/buster14a/buster.git", expected["root"]]),
        ("fetch-preparation-pins", git + ["-C", expected["root"], "fetch", "--no-tags", "origin", expected["base"], expected["head"]]),
        ("baseline-tree", git + ["-C", expected["root"], "rev-parse", expected["base"] + "^{tree}"]),
        ("candidate-tree", git + ["-C", expected["root"], "rev-parse", expected["head"] + "^{tree}"]),
        ("candidate-checkout", git + ["-C", expected["root"], "checkout", "--detach", expected["head"]]),
        ("legacy-snapshot-five-long-controls", [host["native_driver"], "compiler_closure", "qualify", expected["root"],
            expected["output"], expected["base"], expected["base_tree"], expected["head"], expected["head_tree"],
            expected["trusted_lab"], expected["python"]])]
    columns = {"stage", "phase", "wall_us", "exit_status", "timed_out", "cleanup_failed", "cancelled", "state"}
    proofs = {"owner-supervision.tsv": owner_wall}
    phase_wall = 0
    if len(phases) != len(commands):
        raise ValueError("preparation controller phase population differs from the native fixed plan")
    for index, (row, (name, argv)) in enumerate(zip(phases, commands), 1):
        if set(row) != columns or row.get("stage") != str(index) or row.get("phase") != name or row.get("state") != "complete" or \
                any(row.get(key) != "0" for key in ("exit_status", "timed_out", "cleanup_failed", "cancelled")):
            raise ValueError("preparation controller phase is failed, missing or undeclared")
        wall = sampling_integer(row["wall_us"], True)
        phase_wall += wall
        stem = f"controller-{index}-{name}"
        if parse_argv(files.get(stem + ".argv")) != argv:
            raise ValueError("preparation controller argv changed the admitted recipe")
        for stream in ("stdout", "stderr"):
            raw = files.get(stem + "." + stream + ".log")
            if not isinstance(raw, bytes) or len(raw) > 1024 * 1024:
                raise ValueError("preparation controller raw phase output is missing or oversized")
        proofs[stem + "-supervision.tsv"] = wall
    if phase_wall > duration or {name for name in files if name.endswith("-supervision.tsv")} != set(proofs):
        raise ValueError("preparation phase clocks or complete supervision proof population contradict")
    for name, bound in proofs.items():
        proof = sampling_supervision(files.get(name))
        if sampling_integer(proof["wall_us"], True) > bound:
            raise ValueError("preparation supervision exceeds its native phase/owner clock")
    return owner, publication, terminal


def preparation_job(api: Api, authority: dict) -> dict:
    rows = api.pages(f"/actions/runs/{authority['run_id']}/attempts/1/jobs", "jobs")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == PREPARATION_HOST_JOB]
    if len(matches) != 1:
        raise ValueError("preparation physical attempt has no unique platform job")
    row = matches[0]
    if type(row.get("id")) is not int or row["id"] <= 0 or str(row.get("run_id")) != authority["run_id"] or \
            row.get("run_attempt") != 1 or type(row.get("runner_id")) is not int or row["runner_id"] <= 0 or \
            not isinstance(row.get("runner_name"), str) or not row["runner_name"] or \
            not isinstance(row.get("labels"), list) or not row["labels"] or any(
                not isinstance(label, str) or not label for label in row["labels"]):
        raise ValueError("preparation physical runner platform provenance is unavailable")
    return row


def preparation_observed_costs(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    result = {"native_owner_wall_us": None, "native_observed_wall_us": None, "owner_publication_us": None,
              "observation_publication_us": None, "native_controller_us": None, "physical_job_wall_us": None,
              "physical_job_wall_upper_us": None, "queue_delay_seconds": None}
    for name, keys in (("owner.tsv", {"physical_packet_wall_us": "native_owner_wall_us"}),
                       ("owner-publication.tsv", {"observed_wall_us": "native_observed_wall_us", "publication_us": "owner_publication_us"}),
                       ("preparation.tsv", {"duration_us": "native_controller_us"})):
        try:
            row = sampling_tsv(files.get(name))
            for source, target in keys.items():
                result[target] = sampling_integer(row.get(source), target != "owner_publication_us")
        except (ValueError, UnicodeError, TypeError):
            pass
    try:
        job = preparation_job(api, authority)
        result.update(platform_job_state=job.get("status"), platform_job_conclusion=job.get("conclusion"))
        stamps = [datetime.fromisoformat(job[key].replace("Z", "+00:00")) for key in ("created_at", "started_at", "completed_at")]
        if all(stamp.utcoffset() is not None for stamp in stamps):
            wall = round((stamps[2] - stamps[1]).total_seconds() * 1000000)
            queue = (stamps[1] - stamps[0]).total_seconds()
            if wall > 0 and queue >= 0:
                result.update(physical_job_wall_us=wall, physical_job_wall_upper_us=wall + 2000000, queue_delay_seconds=queue)
    except (ValueError, KeyError, TypeError, AttributeError, OverflowError, OSError, urllib.error.URLError, TimeoutError):
        pass
    return result


def preparation_validate(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    from compiler_preparation import validate, WORKLOAD_COMMAND, SERIES
    if any(name.rsplit("/", 1)[-1] in ("fixture-plan.json", "fixture-status.json") for name in files):
        raise ValueError("diagnostic preparation fixture cannot become physical publication authority")
    for name, raw in files.items():
        if name.endswith(("compare.json", "summary.json", "qualification.json", "metadata.json")) and \
                sampling_json(files, name).get("diagnostic_fixture") is True:
            raise ValueError("diagnostic preparation fixture cannot become physical publication authority")
    if authority["history"]:
        raise ValueError("preparation is a single charged attempt; prior outcomes cannot authorize replacement")
    transport = historical_transport(authority, files, "preparation")
    for name, raw in transport.items():
        if files.get(name) != raw:
            raise ValueError("preparation native transport differs from freshly authenticated admission records")
    expected, host = preparation_expected(api, authority, files)
    claim = sampling_tsv(files.get("claim.tsv"))
    wanted = {"schema": "buster-compiler-preparation-claim-v1", "profile": "compiler-baseline-closure-qualification-v1",
              "phase": "qualify", "packet": "0", "plan_revision": authority["admitted"]["preparation_plan_revision"],
              "plan_sha256": authority["admitted"]["preparation_plan_sha256"], "request_run_id": authority["request_id"],
              "request_run_attempt": "1", "executor_run_id": authority["run_id"], "executor_run_attempt": "1",
              "request_head": authority["head"], "policy_trusted_revision": authority["executor"]["head_sha"],
              "measurement_trusted_revision": authority["plan"]["trusted_revision"], "source_root": expected["root"],
              "output_root": expected["output"], "driver": host["native_driver"], "reservation_seconds": "5400",
              "worker_seconds": "5280", "tail_seconds": "120", "state": "claimed"}
    for name, label in (("request.txt", "request_sha256"), ("plan.tsv", "plan_transport_sha256"),
                        ("allowlist.tsv", "allowlist_sha256"), ("facts.tsv", "facts_sha256"), ("history.tsv", "history_sha256")):
        wanted[label] = hashlib.sha256(transport[name]).hexdigest()
    clock_raw = files.get("physical-job-clock.tsv")
    if not isinstance(clock_raw, bytes):
        raise ValueError("preparation native pre-entry clock is missing")
    wanted["physical_job_clock_sha256"] = hashlib.sha256(clock_raw).hexdigest()
    if set(claim) != set(wanted) | {"evidence"} or any(claim.get(key) != value for key, value in wanted.items()) or \
            not isinstance(claim.get("evidence"), str) or not claim["evidence"].startswith("/") or \
            claim["evidence"] == "/" or any(part in ("", ".", "..") for part in claim["evidence"][1:].split("/")):
        raise ValueError("preparation immutable first-claim identity is absent or contradicts admission")
    owner, publication, terminal = preparation_phase_proofs(authority, files, expected, host)
    job = preparation_job(api, authority)
    accounting = sampling_job_accounting(job, sampling_integer(publication["observed_wall_us"], True),
                                        5400, job_name=PREPARATION_HOST_JOB)
    accounting["pre_entry_platform_clock"] = physical_clock_binding(authority, files, job, "preparation", PREPARATION_HOST_JOB)
    accounting.update(native_owner_wall_us=sampling_integer(owner["physical_packet_wall_us"], True),
                      native_observed_wall_us=sampling_integer(publication["observed_wall_us"], True),
                      owner_publication_us=sampling_integer(publication["publication_us"]),
                      observation_publication_us=None, native_controller_us=sampling_integer(terminal["duration_us"], True))
    receipt, bundles = preparation_bundles(files)
    problems = validate(expected, receipt, bundles)
    if problems:
        raise ValueError("preparation native qualification replay failed: " + "; ".join(problems[:12]))
    if receipt["duration_us"] > sampling_integer(sampling_tsv(files["controller.tsv"], True)[-1]["wall_us"], True):
        raise ValueError("preparation qualification duration exceeds its native controller")
    series_expected = dict(expected, command=WORKLOAD_COMMAND)
    series = {}
    from compiler_preparation import frozen_binary
    for arm, name, same in SERIES:
        base_arm = "legacy" if name == "cross-build-aa" else arm
        baseline = frozen_binary(bundles[base_arm]["prepared"], base_arm, "baseline", expected)
        candidate = frozen_binary(bundles[arm]["prepared"], arm, "baseline" if same else "candidate", expected)
        provenance = bundles[arm][name]["metadata"].get("compiler_provenance")
        if not isinstance(provenance, list) or len(provenance) != 2 or any(
                not isinstance(item, dict) or item.get("path") != binary[0] or item.get("sha256") != binary[1] or
                type(item.get("bytes")) is not int or item["bytes"] != binary[2]
                for item, binary in zip(provenance, (baseline, candidate))):
            raise ValueError("preparation full corpus binary path/hash/true size differs from the frozen arm")
        series[arm + "/" + name] = preparation_series_replay(bundles[arm][name], series_expected, same)
        from compiler_preparation import parse_ledger
        unused_root, phases = parse_ledger(bundles[arm]["prepared"], bundles[arm]["ledger"])
        lab_phase = next(row for row in phases if row["phase"] == name + "-lab")
        if series[arm + "/" + name]["observed_timed_wall_us"] > lab_phase["elapsed"] + 2:
            raise ValueError("preparation raw timed pair wall exceeds its native lab phase")
        cleanup = sampling_json(bundles[arm]["files"], f"{lab_phase['stage']}-{name}-lab.cleanup.json")
        if cleanup["duration_us"] > lab_phase["elapsed"]:
            raise ValueError("preparation native cleanup wall exceeds its observed lab phase")
    pointers = receipt["preparation_costs"]
    controls = preparation_control_failures(series, pointers,
        {arm + "/" + name: bundles[arm][name]["throughput"] for arm, name, same in SERIES if same})
    history = [{"phase": "qualify", "packet": 0, "request_run_id": authority["request_id"], "run_id": authority["run_id"],
                "run_attempt": "1", "state": "complete-negative-research" if controls else "complete-valid-research", "reservation_seconds": 5400,
                "actions_job_occupancy_us": accounting["physical_job_wall_upper_us"],
                "campaign": authority["admitted"]["preparation_plan_sha256"],
                "freeze_revision": authority["admitted"]["preparation_plan_revision"]}]
    return {"schema": "buster-compiler-preparation-publication-v1",
            "packet_state": "complete-negative-research" if controls else "complete-valid-research",
            "qualification_state": "unqualified", "default_activated": False, "routine_profile_enabled": False,
            "evidence_class": "unqualified-preparation-research", "phase": "qualify", "packet": 0,
            "plan_revision": authority["admitted"]["preparation_plan_revision"],
            "plan_sha256": authority["admitted"]["preparation_plan_sha256"],
            "source_identity": {key: expected[key] for key in ("base", "base_tree", "head", "head_tree")},
            "measurement_trusted_revision": authority["plan"]["trusted_revision"],
            "policy_trusted_revision": authority["executor"]["head_sha"], "host": host,
            "platform_runner": {key: job[key] for key in ("id", "runner_id", "runner_name", "labels")},
            "reservation_seconds": 5400, "accounting": accounting, "series": series,
            "preparation_costs": pointers, "preparation_cost_scope": "native-operation",
            "qualification_publication_us": None, "whole_job_net_savings_assessed": False,
            "predeclared_controls": {"aa_families": 3, "aa_interval": [0.995, 1.005],
                                    "all_full_corpora_valid": True,
                                    "snapshot_native_operation_cost_less_than_legacy": pointers["snapshot"]["total_us"] < pointers["legacy"]["total_us"]},
            "control_failures": controls, "authenticated_attempt_history": history, "problems": []}


PREPARATION_FAILURE_TAIL_BYTES = 512


def preparation_failure_problems(files: dict[str, bytes]) -> list[str]:
    """Diagnostics only (#3960): name the retained failed controller phase.

    Reads the artifact's raw controller.tsv, preparation.tsv and the failed
    phase's stderr log as data. Never raises and never changes the packet
    state, qualification or publication refusal."""
    def clean(raw: bytes) -> str:
        return "".join(chr(byte) if 32 <= byte <= 126 else " " if byte in (9, 10, 13) else "?" for byte in raw)
    problems = []
    try:
        rows = sampling_tsv(files.get("controller.tsv", b""), True)
    except (ValueError, UnicodeError, TypeError):
        rows = None
        problems.append("controller phase table is missing or malformed in the retained artifact")
    for row in (rows or [])[:32]:
        if row.get("state") != "failed":
            continue
        stem = f"controller-{row.get('stage')}-{row.get('phase')}"
        error = files.get(stem + ".stderr.log", b"")
        error = error if isinstance(error, bytes) else b""
        problems.append(("failed controller phase " + " ".join(
            f"{key}={clean(str(row.get(key, '')).encode('ascii', 'replace'))}" for key in
            ("stage", "phase", "wall_us", "exit_status", "timed_out", "cleanup_failed", "cancelled")) +
            f" stderr_bytes={len(error)} stderr_tail=\"" +
            clean(error[-PREPARATION_FAILURE_TAIL_BYTES:]).replace('"', "?") + "\"")[:1000])
    try:
        terminal = sampling_tsv(files.get("preparation.tsv", b""))
    except (ValueError, UnicodeError, TypeError):
        terminal = None
        problems.append("worker preparation.tsv is missing or malformed in the retained artifact")
    if terminal is not None and terminal.get("process_state") != "complete":
        problems.append(("worker preparation.tsv " + " ".join(
            f"{key}={clean(str(terminal[key]).encode('ascii', 'replace'))}" for key in
            ("process_state", "reason", "cleanup_proven", "tools_before", "tools_after", "exported")
            if key in terminal))[:1000])
    return problems


def preparation_publish(environment: dict) -> int:
    api, authority = preparation_authority(environment)
    files, artifact, diagnostics = {}, {}, []
    result = {"schema": "buster-compiler-preparation-publication-v1", "packet_state": "incomplete",
              "qualification_state": "unqualified", "default_activated": False, "routine_profile_enabled": False,
              "evidence_class": "unqualified-preparation-research", "phase": "qualify", "packet": 0,
              "reservation_seconds": 5400, "whole_job_net_savings_assessed": False, "problems": []}
    try:
        files, artifact = preparation_read_artifact(api, authority)
        if environment.get("BQ_PREPARATION_RESULT") != "success":
            diagnostics = preparation_failure_problems(files)
            raise ValueError("preparation physical job is failed, cancelled, skipped or unavailable")
        result = preparation_validate(api, authority, files)
    except (OSError, ValueError, UnicodeError, TypeError, KeyError, IndexError, AttributeError, RecursionError,
            urllib.error.URLError, TimeoutError) as error:
        result["problems"].append(("evidence validation failed: " + type(error).__name__ + ": " + str(error))[:1000])
        result["problems"].extend(diagnostics)
        result["accounting"] = preparation_observed_costs(api, authority, files)
        result["authenticated_attempt_history"] = list(authority["history"]) + [{
            "phase": "qualify", "packet": 0, "request_run_id": authority["request_id"], "run_id": authority["run_id"],
            "run_attempt": "1", "state": "incomplete", "reservation_seconds": 5400,
            "actions_job_occupancy_us": result["accounting"]["physical_job_wall_upper_us"],
            "campaign": authority["admitted"]["preparation_plan_sha256"],
            "freeze_revision": authority["admitted"]["preparation_plan_revision"]}]
    success = result["packet_state"] == "complete-valid-research" and not result["problems"]
    conclusion = "success" if success else "failure"
    title = ("Valid unqualified preparation packet" if success else
             "Unqualified preparation controls failed" if result["packet_state"] == "complete-negative-research" else
             "Incomplete unqualified preparation packet")
    summary = preparation_summary(authority) + f"\nPacket state: {result['packet_state']}. Qualification state: unqualified.\n"
    summary += ("\nNative preparation operation costs are compared; complete ordinary-job net savings is unassessed. "
                "Qualification/export/publication and ordinary owned-phase overhead are unavailable.\n")
    if result["problems"]:
        summary += "\nEvidence problems:\n" + "\n".join("- " + str(problem)[:1000] for problem in result["problems"][:30]) + "\n"
    if result.get("control_failures"):
        summary += "\nCompleted control findings:\n" + "\n".join("- " + str(item)[:1000] for item in result["control_failures"]) + "\n"
    fence = chr(96) * 3
    summary += "\nObserved accounting:\n" + fence + "json\n" + json.dumps(result.get("accounting", {}), sort_keys=True) + "\n" + fence + "\n"
    if artifact:
        summary += "\nEvidence: " + artifact_link(authority["repository"], authority["run_id"], artifact) + "\n"
    report = json.dumps(result, sort_keys=True, indent=2)
    row = preparation_write(api, authority, {"status": "completed", "conclusion": conclusion,
        "details_url": run_url(authority["repository"], authority["run_id"], "1"),
        "output": {"title": title, "summary": summary[:TEXT_LIMIT], "text": fence + "json\n" + report[:TEXT_LIMIT - 16] + "\n" + fence}})
    with open(environment.get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
        stream.write(summary + "\n")
    print(f"COMPILER_PREPARATION_PUBLISHED {conclusion} check={row.get('id')} state={row.get('status')} qualification=unqualified")
    return 0 if success and row.get("status") == "completed" and row.get("conclusion") == "success" else 1


UTILITY_CHECK_NAME = "9700X compiler closure utility research"
UTILITY_HOST_JOB = "Compiler closure utility"


def physical_clock_data(environment: dict) -> int:
    """Read public platform start facts; native code owns admission and deadline clamping."""
    import time
    from pathlib import Path
    import authorize as direct_authorize
    kind = environment.get("BQ_PHYSICAL_CLOCK_KIND")
    jobs = {"sampling": ("sampling", SAMPLING_HOST_JOB), "preparation": ("preparation", PREPARATION_HOST_JOB),
            "utility": ("utility", UTILITY_HOST_JOB), "main": ("compare", COMPARE_JOBS["main"])}
    repository, run_id, attempt = environment.get("GITHUB_REPOSITORY", ""), environment.get("GITHUB_RUN_ID", ""), environment.get("GITHUB_RUN_ATTEMPT", "")
    revision, runner = environment.get("GITHUB_SHA", ""), environment.get("RUNNER_NAME", "")
    request_id, head = environment.get("BQ_REQUEST_RUN_ID", ""), environment.get("BQ_HEAD_COMMIT", "")
    if kind not in jobs or repository != "buster14a/buster" or not DECIMAL.fullmatch(run_id) or not DECIMAL.fullmatch(attempt) or int(attempt) < 1 or \
            (kind != "main" and attempt != "1") or \
            not SHA.fullmatch(revision) or environment.get("GITHUB_JOB") != jobs[kind][0] or \
            not runner or any(ord(char) < 32 or ord(char) > 126 for char in runner) or \
            not DECIMAL.fullmatch(request_id) or not SHA.fullmatch(head):
        raise ValueError("physical clock lacks exact platform/request/runner inputs")
    started_wall, started_mono = time.time_ns() // 1000, time.monotonic_ns()
    # Deliberately tokenless even if an inherited environment has a credential.
    api = Api(repository, "", response_limit=128 * 1024)
    execution = api.request(f"/actions/runs/{run_id}/attempts/{attempt}" if kind == "main" else f"/actions/runs/{run_id}")
    if not isinstance(execution, dict) or type(execution.get("id")) is not int or str(execution["id"]) != run_id or \
            type(execution.get("run_attempt")) is not int or execution["run_attempt"] != int(attempt) or \
            execution.get("path") != BENCH_WORKFLOW or execution.get("event") != "workflow_run" or \
            execution.get("head_branch") != "main" or execution.get("head_sha") != revision or \
            direct_authorize.full_name(execution.get("repository")) != repository or \
            direct_authorize.full_name(execution.get("head_repository")) != repository or \
            (kind != "main" and (direct_authorize.identity(execution.get("actor")) != direct_authorize.MAINTAINER or \
             direct_authorize.identity(execution.get("triggering_actor")) != direct_authorize.MAINTAINER or \
             execution.get("display_title") != f"9700X request {request_id}.1 head {head}")):
        raise ValueError("physical clock current workflow provenance differs")
    if kind == "main":
        from authorize_compiler import main_route_attempt
        unused_execution, request, actual_head = main_route_attempt(api, repository, execution, attempt)
        if str(request["id"]) != request_id or actual_head != head:
            raise ValueError("Main physical clock differs from the original automatic push request")
    rows = api.pages(f"/actions/runs/{run_id}/attempts/{attempt}/jobs", "jobs")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == jobs[kind][1]]
    if len(matches) != 1:
        raise ValueError("physical clock platform job is absent or ambiguous")
    job = matches[0]
    if type(job.get("id")) is not int or job["id"] <= 0 or str(job.get("run_id")) != run_id or \
            type(job.get("run_attempt")) is not int or job["run_attempt"] != int(attempt) or job.get("head_sha") != revision or \
            job.get("status") != "in_progress" or job.get("conclusion") is not None or \
            type(job.get("runner_id")) is not int or job["runner_id"] <= 0 or job.get("runner_name") != runner or \
            not isinstance(job.get("labels"), list) or not job["labels"] or any(
                not isinstance(value, str) or not value for value in job["labels"]) or \
            not {"self-hosted", "Linux", "X64", "buster-zen5", "ryzen-9700x"}.issubset(set(job["labels"])):
        raise ValueError("physical clock actual active runner/platform attempt is unavailable")
    stamp = job.get("started_at")
    if not isinstance(stamp, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", stamp):
        raise ValueError("physical clock platform start timestamp is unavailable")
    job_start = round(datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp() * 1000000)
    finished_wall, elapsed = time.time_ns() // 1000, (time.monotonic_ns() - started_mono) // 1000
    if not 0 < job_start <= started_wall <= finished_wall or not 0 <= elapsed <= 120 * 1000000 or \
            abs((finished_wall - started_wall) - elapsed) > 1000000 or finished_wall - job_start > 5400 * 1000000:
        raise ValueError("physical clock observation is stale, inconsistent or outside its reservation")
    record = {"schema": "buster-compiler-physical-job-clock-v1", "kind": kind, "repository": repository,
              "run_id": run_id, "run_attempt": attempt, "policy_trusted_revision": revision,
              "job_id": str(job["id"]), "job_name": jobs[kind][1], "runner_id": str(job["runner_id"]), "runner_name": runner,
              "started_at": stamp, "started_unix_us": str(job_start), "start_lower_unix_us": str(job_start - 1000000),
              "observer_started_unix_us": str(started_wall), "observer_finished_unix_us": str(finished_wall),
              "observer_monotonic_elapsed_us": str(elapsed), "timestamp_precision_us": "1000000",
              "observation_scope": "public-platform-job-start"}
    raw = "".join(key + "\t" + value + "\n" for key, value in record.items()).encode("ascii")
    root = Path(environment.get("RUNNER_TEMP", ""))
    output, env_file = root / "compiler-physical-job-clock.tsv", environment.get("GITHUB_ENV", "")
    if not root.is_absolute() or not root.is_dir() or not env_file or len(raw) > 16384:
        raise ValueError("physical clock evidence/environment destination is unavailable")
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(raw)
    encoded = base64.b64encode(raw).decode("ascii")
    with open(env_file, "a", encoding="ascii") as stream:
        stream.write("BQ_PHYSICAL_JOB_DATA=" + encoded + "\n")
        stream.write("BQ_PHYSICAL_JOB_DATA_FILE=" + str(output) + "\n")
    print(f"COMPILER_PHYSICAL_CLOCK kind={kind} job={job['id']} attempt={attempt}")
    return 0


def utility_export_manifest(raw: bytes, members: dict[str, bytes], *,
                            expected_schema: str = "BUSTER_COMPILER_CLOSURE_UTILITY_EXPORT_V1") -> dict:
    """Bind every retained data member to the native exact export inventory."""
    if expected_schema not in ("BUSTER_COMPILER_CLOSURE_UTILITY_EXPORT_V1", "BUSTER_COMPILER_MAIN_FORTY_EXPORT_V1"):
        raise ValueError("export manifest requires a trusted supported schema")
    if not isinstance(raw, bytes) or not 0 < len(raw) <= 8 << 20 or not raw.endswith(b"\n"):
        raise ValueError("utility export manifest is missing, truncated or oversized")
    rows = raw.decode("ascii").splitlines()
    if not rows or rows[0] != expected_schema or len(rows) > 131073:
        raise ValueError("utility export manifest schema/population differs")
    seen, regular, directories = {}, set(), set()
    for row in rows[1:]:
        fields = row.split("\t")
        if len(fields) != 5:
            raise ValueError("utility export manifest row is malformed")
        kind, name, digest, size, mode = fields
        if kind not in ("F", "D") or not name or any(part in ("", ".", "..") for part in name.split("/")) or \
                name.startswith("/") or not re.fullmatch(r"[A-Za-z0-9_.+/-]+", name) or name in seen:
            raise ValueError("utility export manifest path/kind is unsafe or duplicated")
        size, mode = sampling_integer(size), sampling_integer(mode)
        if mode > 0o7777:
            raise ValueError("utility export manifest mode is malformed")
        if kind == "D":
            if digest != "-" or size != 0 or name in members:
                raise ValueError("utility export directory contains an invented file identity")
            directories.add(name)
        else:
            data = members.get(name)
            if not re.fullmatch(r"[a-f0-9]{64}", digest) or mode & 0o111 or \
                    not isinstance(data, bytes) or len(data) != size or hashlib.sha256(data).hexdigest() != digest:
                raise ValueError("utility export manifest differs from retained regular data")
            regular.add(name)
        seen[name] = {"kind": kind, "sha256": digest if kind == "F" else None, "bytes": size, "mode": mode}
    if not regular or regular != set(members) or any(
            "/".join(name.split("/")[:index]) not in directories
            for name in regular for index in range(1, len(name.split("/")))):
        raise ValueError("utility export manifest does not cover exactly the retained data and parent directories")
    return seen


def utility_net_observation(legacy_us: int, snapshot_us: int, physical_upper_us: int) -> dict:
    """Assess the once-only trace with every outside-leg physical microsecond charged to snapshot."""
    if any(type(value) is not int or not 0 < value <= (1 << 64) - 1
           for value in (legacy_us, snapshot_us, physical_upper_us)) or \
            physical_upper_us > 5400 * 1000000 or physical_upper_us < legacy_us + snapshot_us:
        raise ValueError("utility complete leg/whole-job clocks are unavailable or inconsistent")
    residual = physical_upper_us - legacy_us - snapshot_us
    charged_snapshot = snapshot_us + residual
    return {"legacy_leg_us": legacy_us, "snapshot_leg_us": snapshot_us,
            "physical_job_wall_upper_us": physical_upper_us, "physical_residual_us": residual,
            "residual_charged_to_legacy_us": 0, "residual_charged_to_snapshot_us": residual,
            "snapshot_charged_us": charged_snapshot,
            "observed_net_saving_us": legacy_us - charged_snapshot,
            "criterion": "snapshot-plus-residual-less-than-legacy",
            "charge_policy": "all-physical-residual-to-snapshot",
            "criterion_met": charged_snapshot < legacy_us,
            "assessment_scope": "once-only-controlled-trace",
            "baseline_recipe_scope": "declared-supervised-ordinary-recipes",
            "historical_unwrapped_legacy_savings_assessed": False,
            "general_workload_savings_assessed": False,
            "hosted_api_publication_us": None}
def utility_authority(environment: dict) -> tuple[Api, dict]:
    import subprocess
    import tempfile
    from pathlib import Path
    import authorize as direct_authorize
    repository, head = environment.get("BQ_REPOSITORY", ""), environment.get("BQ_HEAD_COMMIT", "")
    request_id, run_id = environment.get("BQ_REQUEST_RUN_ID", ""), environment.get("BQ_RUN_ID", "")
    if repository != "buster14a/buster" or not SHA.fullmatch(head) or \
            any(not DECIMAL.fullmatch(value) for value in (request_id, run_id)) or \
            environment.get("BQ_RUN_ATTEMPT") != "1" or environment.get("BQ_REQUEST_ATTEMPT") != "1" or \
            environment.get("GITHUB_RUN_ID") != run_id or environment.get("GITHUB_RUN_ATTEMPT") != "1" or \
            environment.get("GITHUB_REPOSITORY") != repository or not environment.get("GH_TOKEN"):
        raise ValueError("utility publication lacks exact trusted workflow inputs")
    api = Api(repository, environment["GH_TOKEN"])
    execution = api.request(f"/actions/runs/{run_id}")
    if not isinstance(execution, dict) or type(execution.get("id")) is not int or str(execution.get("id")) != run_id or \
            type(execution.get("run_attempt")) is not int or execution.get("run_attempt") != 1 or execution.get("path") != BENCH_WORKFLOW or \
            execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or \
            not isinstance(execution.get("repository"), dict) or execution["repository"].get("full_name") != repository or \
            direct_authorize.full_name(execution.get("head_repository")) != repository or \
            direct_authorize.identity(execution.get("actor")) != direct_authorize.MAINTAINER or \
            direct_authorize.identity(execution.get("triggering_actor")) != direct_authorize.MAINTAINER or \
            execution.get("head_sha") != environment.get("GITHUB_SHA") or \
            execution.get("display_title") != f"9700X request {request_id}.1 head {head}":
        raise ValueError("utility executor workflow provenance is unavailable")
    request = api.request(f"/actions/runs/{request_id}")
    if not isinstance(request, dict) or type(request.get("run_attempt")) is not int or request["run_attempt"] != 1:
        raise ValueError("utility request API attempt is not the exact first attempt")
    pulls = api.request(f"/commits/{head}/pulls?per_page=100")
    problems, unused_base = direct_authorize.verify(repository, int(request_id), head, request, pulls, expected_run_attempt=1)
    if problems:
        raise ValueError("utility request ownership failed: " + ", ".join(problems))
    commit = api.request(f"/commits/{head}")
    parents = commit.get("parents") if isinstance(commit, dict) else None
    if not isinstance(parents, list) or not 1 <= len(parents) <= 2 or any(
            not isinstance(row, dict) or not SHA.fullmatch(str(row.get("sha", ""))) for row in parents):
        raise ValueError("utility request has an unsupported parent inventory")
    compared = [api.request(f"/compare/{row['sha']}...{head}") for row in parents]
    marker = direct_authorize.sampling_content(repository, direct_authorize.COMPARE_REQUEST, head, environment["GH_TOKEN"])
    selected = direct_authorize.utility_fresh_selector(marker, compared)
    if selected is None:
        raise ValueError("utility selector is not fresh against every Git parent")
    pull = next(row for row in pulls if isinstance(row, dict) and row.get("state") == "open" and
                isinstance(row.get("head"), dict) and row["head"].get("sha") == head)
    if type(pull.get("number")) is not int or pull["number"] <= 0:
        raise ValueError("utility request pull number is unavailable")
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="utility-publication-") as temporary:
        directory = Path(temporary) / "admission"
        if not direct_authorize.utility_data(repository, environment["GH_TOKEN"], request, pull, head, "1",
                                                marker, compared, directory):
            raise ValueError("utility native admission data is unavailable")
        output = directory / "admitted.env"
        cleanup, workspace = environment.get("RUNNER_TEMP"), environment.get("GITHUB_WORKSPACE")
        if not cleanup or not workspace:
            raise ValueError("utility hosted cleanup roots are unavailable")
        command = [str(root / "build.sh"), "compiler_profile_qualification", "--admit-utility",
                   *(str(directory / name) for name in ("allowlist.tsv", "request.txt", "facts.tsv", "history.tsv", "plan.tsv")),
                   cleanup, workspace, str(output)]
        child_environment = {key: value for key, value in environment.items() if key not in ("GH_TOKEN", "GITHUB_TOKEN")}
        try:
            result = subprocess.run(command, cwd=root, env=child_environment, timeout=120, check=False,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired as error:
            raise ValueError("trusted native utility admission exceeded its bounded hosted pass") from error
        if result.returncode != 0 or not output.is_file() or output.stat().st_size > 16384:
            raise ValueError("trusted native utility admission refused publication")
        admitted = {}
        for line in output.read_text(encoding="ascii").splitlines():
            key, separator, value = line.partition("=")
            if not separator or not re.fullmatch(r"utility_[a-z][a-z0-9_]*", key) or key in admitted or not value or \
                    any(ord(char) < 32 or ord(char) > 126 for char in value):
                raise ValueError("native utility admission output is ambiguous")
            admitted[key] = value
        names = ("phase", "packet", "family", "reservation_seconds", "worker_seconds", "timeout_minutes",
                 "plan_revision", "plan_sha256", "protocol_sha256", "base", "base_tree",
                 "candidate_revision", "candidate_tree", "pull_head", "trusted_revision")
        if set(admitted) != {"utility_admitted", *("utility_" + name for name in names)} or \
                admitted.get("utility_admitted") != "true" or any(
                    admitted["utility_" + name] != environment.get("BQ_UTILITY_" + name.upper()) for name in names):
            raise ValueError("utility identity contradicts freshly repeated native admission")
        raw = {name: (directory / name).read_bytes() for name in
               ("request.txt", "plan.tsv", "allowlist.tsv", "facts.tsv", "history.tsv")}
        if hashlib.sha256(raw["plan.tsv"]).hexdigest() != admitted["utility_plan_sha256"]:
            raise ValueError("committed utility plan differs from native admission digest")
        authority = {"admitted": admitted, "plan": sampling_tsv(raw["plan.tsv"]), "raw": raw,
                     "history": sampling_tsv(raw["history.tsv"], True), "facts": sampling_tsv(raw["facts.tsv"]),
                     "request": request, "executor": execution, "request_line": selected[0],
                     "repository": repository, "head": head, "request_id": request_id, "run_id": run_id,
                     "pull": str(pull["number"])}
    return api, authority



def utility_check_marker(authority: dict) -> str:
    return ("buster-compiler-closure-utility-v1:" + authority["admitted"]["utility_plan_sha256"] +
            ":utility:0:" + authority["request_id"] + ":" + authority["run_id"] + ":1")


def utility_owned(row: object, authority: dict) -> bool:
    from compiler_github import GITHUB_ACTIONS_APP_ID
    return isinstance(row, dict) and type(row.get("id")) is int and row["id"] > 0 and \
        row.get("name") == UTILITY_CHECK_NAME and row.get("head_sha") == authority["head"] and \
        row.get("external_id") == utility_check_marker(authority) and isinstance(row.get("app"), dict) and \
        row["app"].get("id") == GITHUB_ACTIONS_APP_ID and row.get("status") in ("queued", "in_progress", "completed")


def utility_checks(api: Api, authority: dict) -> list[dict]:
    from compiler_github import GITHUB_ACTIONS_APP_ID
    query = urllib.parse.urlencode({"check_name": UTILITY_CHECK_NAME, "filter": "all", "app_id": GITHUB_ACTIONS_APP_ID})
    rows = api.pages(f"/commits/{authority['head']}/check-runs?{query}", "check_runs")
    owned = [row for row in rows if utility_owned(row, authority)]
    if len(owned) > 1:
        raise ValueError("utility attempt has duplicate owned checks")
    return owned


def utility_write(api: Api, authority: dict, fields: dict) -> dict:
    from compiler_github import write_check
    rows = utility_checks(api, authority)
    if rows:
        return write_check(api, rows[0], fields)
    body = dict(fields, name=UTILITY_CHECK_NAME, head_sha=authority["head"],
                external_id=utility_check_marker(authority))
    try:
        written = write_check(api, None, body)
        if utility_owned(written, authority):
            return written
    except (urllib.error.URLError, TimeoutError, ValueError):
        pass
    rows = utility_checks(api, authority)
    if len(rows) != 1:
        raise ValueError("utility check creation is ambiguous; no duplicate write or host work authorized")
    return rows[0]


def utility_summary(authority: dict) -> str:
    return ("Unqualified compiler closure utility research; routine snapshot preparation remains disabled.\n\n"
            "Lifecycle protocol: closure-utility-terminal-native-v1.\n"
            "Phase utility, packet 0; whole physical-job reservation 5400 seconds.\n\n" +
            f"Request run {authority['request_id']} attempt 1: {run_url(authority['repository'], authority['request_id'], '1')}\n" +
            f"Workflow run {authority['run_id']} attempt 1: {run_url(authority['repository'], authority['run_id'], '1')}\n")


def utility_queue(environment: dict) -> int:
    api, authority = utility_authority(environment)
    row = utility_write(api, authority, {"status": "queued",
        "details_url": run_url(authority["repository"], authority["run_id"], "1"),
        "output": {"title": "Queued unqualified utility research", "summary": utility_summary(authority)}})
    if row.get("status") != "queued":
        raise ValueError("utility queue is already running or terminal; no new physical assignment authorized")
    print(f"COMPILER_UTILITY_QUEUED check={row.get('id')} state={row.get('status')} qualification=unqualified")
    return 0




def utility_read_artifact(api: Api, authority: dict, *, retain_archive_identity: bool = False) -> tuple[dict[str, bytes], dict]:
    name = "buster-9700x-utility-" + authority["head"] + "-1"
    listing = api.request(f"/actions/runs/{authority['run_id']}/artifacts?" +
                          urllib.parse.urlencode({"name": name, "per_page": 10}))
    rows = listing.get("artifacts") if isinstance(listing, dict) else None
    if not isinstance(rows, list) or len(rows) >= 10:
        raise ValueError("utility artifact inventory is unavailable or capped")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == name]
    if len(matches) != 1:
        raise ValueError("utility attempt has no unique evidence artifact")
    row = matches[0]
    origin = row.get("workflow_run")
    if type(row.get("id")) is not int or row["id"] <= 0 or row.get("expired") is not False or \
            type(row.get("size_in_bytes")) is not int or not 0 < row["size_in_bytes"] <= PREPARATION_ARCHIVE_LIMIT or \
            not isinstance(origin, dict) or str(origin.get("id")) != authority["run_id"] or \
            origin.get("head_sha") != authority["executor"].get("head_sha"):
        raise ValueError("utility artifact is expired, oversized or belongs to another executor")
    payload = api.download(api.prefix + f"/actions/artifacts/{row['id']}/zip", max_bytes=PREPARATION_ARCHIVE_LIMIT)
    files = preparation_archive(payload)
    return files, campaign_artifact_identity(payload, row, files, retain_archive_identity)



def utility_job(api: Api, authority: dict) -> dict:
    rows = api.pages(f"/actions/runs/{authority['run_id']}/attempts/1/jobs", "jobs")
    matches = [row for row in rows if isinstance(row, dict) and row.get("name") == UTILITY_HOST_JOB]
    if len(matches) != 1:
        raise ValueError("utility physical attempt has no unique platform job")
    row = matches[0]
    if type(row.get("id")) is not int or row["id"] <= 0 or str(row.get("run_id")) != authority["run_id"] or \
            type(row.get("run_attempt")) is not int or row["run_attempt"] != 1 or \
            row.get("head_sha") != authority["executor"]["head_sha"] or type(row.get("runner_id")) is not int or row["runner_id"] <= 0 or \
            not isinstance(row.get("runner_name"), str) or not row["runner_name"] or \
            not isinstance(row.get("labels"), list) or not row["labels"] or any(
                not isinstance(label, str) or not label for label in row["labels"]):
        raise ValueError("utility physical runner platform provenance is unavailable")
    return row





def utility_source_identity(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    from pathlib import Path
    from compiler_preparation import absolute
    from sampling_qualification_receipt import _lab
    plan, admitted = authority["plan"], authority["admitted"]
    commits = {}
    for role in ("baseline", "candidate"):
        revision, tree = plan[role + "_revision"], plan[role + "_tree"]
        row = api.request("/git/commits/" + revision)
        if not isinstance(row, dict) or row.get("sha") != revision or \
                not isinstance(row.get("tree"), dict) or row["tree"].get("sha") != tree:
            raise ValueError("utility immutable source Git tree contradicts its committed plan")
        commits[role] = row
    parents = commits["candidate"].get("parents")
    if not isinstance(parents, list) or [row.get("sha") if isinstance(row, dict) else None for row in parents] != \
            [plan["baseline_revision"], plan["pull_head"]]:
        raise ValueError("utility candidate alias is not the exact baseline/AB1 parent join")
    arm = api.request("/git/commits/" + plan["pull_head"])
    if not isinstance(arm, dict) or arm.get("sha") != plan["pull_head"] or \
            not isinstance(arm.get("tree"), dict) or arm["tree"].get("sha") != plan["candidate_tree"]:
        raise ValueError("utility candidate alias differs from the exact frozen AB1 source tree")
    trusted = Path(__file__).resolve().parent
    sources = (("tools/uarch_lab.py", "lab_sha256", Path(_lab.__file__)),
               ("tools/bench_direct/compiler_compare.py", "comparator_sha256", trusted / "compiler_compare.py"),
               ("tools/bench_direct/compiler_receipt.py", "receipt_sha256", trusted / "compiler_receipt.py"),
               ("tools/bench_direct/compiler_owned_phase.py", "owned_phase_sha256", trusted / "compiler_owned_phase.py"),
               ("tools/bench_direct/compiler_owned_plan.py", None, trusted / "compiler_owned_plan.py"),
               ("docs/compiler-closure-utility-v1.md", "protocol_sha256", None))
    for path, key, local in sources:
        row = api.request("/contents/" + path + "?ref=" + plan["trusted_revision"])
        if not isinstance(row, dict) or row.get("type") != "file" or row.get("encoding") != "base64" or \
                type(row.get("size")) is not int or not 0 < row["size"] <= 1024 * 1024 or not isinstance(row.get("content"), str):
            raise ValueError("utility pinned trusted harness data is missing or oversized")
        raw = base64.b64decode(row["content"].replace("\n", ""), validate=True)
        if len(raw) != row["size"] or key is not None and hashlib.sha256(raw).hexdigest() != plan[key] or \
                local is not None and local.read_bytes() != raw:
            raise ValueError("utility actual trusted replay/comparator source differs from its committed pin")
    host = sampling_json(files, "host.json")
    wanted = {"schema": "buster-compiler-closure-utility-host-v1", "state": "complete",
              "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor", "observed_from": "/proc/cpuinfo",
              "request_head": authority["head"], "run_id": authority["run_id"], "run_attempt": "1",
              "request_run_id": authority["request_id"], "measurement_trusted_revision": plan["trusted_revision"],
              "policy_trusted_revision": authority["executor"]["head_sha"],
              "plan_revision": admitted["utility_plan_revision"], "plan_sha256": admitted["utility_plan_sha256"],
              "protocol_sha256": plan["protocol_sha256"], "trusted_lab_sha256": plan["lab_sha256"],
              "python": plan["python_path"], "python_sha256": plan["python_sha256"],
              "native_driver_sha256": plan["native_driver_sha256"],
              "trusted_lab": plan["trusted_root"] + "/tools/uarch_lab.py",
              "comparator": plan["trusted_root"] + "/tools/bench_direct/compiler_compare.py",
              "comparator_sha256": plan["comparator_sha256"],
              "receipt_adapter": plan["trusted_root"] + "/tools/bench_direct/compiler_receipt.py",
              "receipt_sha256": plan["receipt_sha256"],
              "owned_phase": plan["trusted_root"] + "/tools/bench_direct/compiler_owned_phase.py",
              "owned_phase_sha256": plan["owned_phase_sha256"]}
    if set(host) != set(wanted) | {"logical_processor_records", "native_driver", "bootstrap_marker_sha256"} or \
            any(type(host.get(key)) is not type(value) or host.get(key) != value for key, value in wanted.items()) or \
            type(host.get("logical_processor_records")) is not int or not 0 < host["logical_processor_records"] <= 4096 or \
            not absolute(host.get("native_driver")) or not host["native_driver"].startswith(plan["trusted_root"] + "/") or \
            not re.fullmatch(r"[0-9a-f]{64}", str(host.get("bootstrap_marker_sha256", ""))):
        raise ValueError("utility actual CPU/tool observation or workflow binding is incomplete")
    return host




def utility_phase_proofs(authority: dict, files: dict[str, bytes], host: dict, *,
                         expected_phase_schema: str = "buster-compiler-utility-phases-v1") -> tuple[dict, dict, dict, list[dict]]:
    if expected_phase_schema not in ("buster-compiler-utility-phases-v1", "buster-compiler-main-owned-phases-v1"):
        raise ValueError("Utility requires an explicitly trusted owned population")
    plan = authority['plan']
    expected = {'root': plan['source_root'], 'output': plan['output_root'], 'base': plan['baseline_revision'],
                'base_tree': plan['baseline_tree'], 'head': plan['candidate_revision'], 'head_tree': plan['candidate_tree']}
    from compiler_preparation import parse_argv
    plan_sha = authority["admitted"]["utility_plan_sha256"]
    owner_raw = files.get("owner.tsv")
    owner = sampling_tsv(owner_raw)
    wanted = {"schema": "buster-compiler-closure-utility-owner-v1", "phase": "utility", "packet": "0", "plan_sha256": plan_sha,
              "wall_scope": "public-platform-job-start-lower-through-child-cleanup-before-terminal-publication", "process_state": "complete",
              "timed_out": "0", "cleanup_failed": "0", "within_reservation": "true", "cancelled": "0",
              "qualification_state": "unvalidated", "default_activated": "false",
              "manager_launch_attempted": "1", "manager_wait_observed": "1", "manager_cleanup_proven": "1"}
    if set(owner) != set(wanted) | {"physical_packet_wall_us", "native_entry_wall_us", "job_elapsed_at_native_entry_us", "physical_job_clock_sha256"} or any(owner.get(key) != value for key, value in wanted.items()):
        raise ValueError("utility owned worker is failed, cancelled, exhausted or incomplete")
    owner_wall = sampling_integer(owner["physical_packet_wall_us"], True)
    native_wall = sampling_integer(owner["native_entry_wall_us"], True)
    entry_elapsed = sampling_integer(owner["job_elapsed_at_native_entry_us"], True)
    clock_raw = files.get("physical-job-clock.tsv")
    if not isinstance(clock_raw, bytes) or owner["physical_job_clock_sha256"] != hashlib.sha256(clock_raw).hexdigest() or \
            owner_wall != native_wall + entry_elapsed or owner_wall > 5400 * 1000000:
        raise ValueError("utility native entry and public job-start scopes contradict")
    publication = sampling_tsv(files.get("owner-publication.tsv"))
    pub_wanted = {"schema": "buster-compiler-closure-utility-owner-publication-v1",
                  "owner_sha256": hashlib.sha256(owner_raw).hexdigest(), "scope": "public-platform-job-start-lower-through-owner-publication",
                  "observation_publication_us": "unavailable", "within_reservation": "true"}
    if set(publication) != set(pub_wanted) | {"initial_scope_us", "publication_us", "observed_wall_us"} or \
            any(publication.get(key) != value for key, value in pub_wanted.items()):
        raise ValueError("utility owner publication observation is missing or falsely assigns its final tail")
    initial = sampling_integer(publication["initial_scope_us"], True)
    pub_us = sampling_integer(publication["publication_us"])
    observed_wall = sampling_integer(publication["observed_wall_us"], True)
    if initial != owner_wall or observed_wall != owner_wall + pub_us or observed_wall > 5400 * 1000000:
        raise ValueError("utility owner and publication whole-operation clock accounting contradict")
    terminal = sampling_tsv(files.get("utility.tsv"))
    term_wanted = {"schema": "buster-compiler-closure-utility-controller-v1", "phase": "utility", "packet": "0",
                   "plan_sha256": plan_sha, "process_state": "complete", "qualification_state": "unvalidated",
                   "default_activated": "false", "cleanup_proven": "true", "source_root": expected["root"],
                   "output_root": expected["output"], "tools_before": "true", "tools_after": "true", "exported": "true", "complete_legs": "2",
                   "clock_scope": "bootstrap-through-export-hashfinalization",
                   "utility_charge_policy": "all-physical-residual-to-snapshot", "net_utility": "unavailable",
                   "terminal_publication_us": "unavailable"}
    if set(terminal) != set(term_wanted) | {"duration_us"} or any(terminal.get(key) != value for key, value in term_wanted.items()):
        raise ValueError("utility native control/source/tool/export proof is incomplete")
    duration = sampling_integer(terminal["duration_us"], True)
    if duration > native_wall or duration > 5280 * 1000000:
        raise ValueError("utility worker exceeds its reserved native clock")
    phases = sampling_tsv(files.get("controller.tsv"), True)
    trusted_root = host["trusted_lab"][:-len("/tools/uarch_lab.py")]
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null"]
    commands = [
        ("trusted-harness-pin", git + ["-C", trusted_root, "rev-parse", "HEAD"]),
        ("trusted-harness-clean", git + ["-C", trusted_root, "diff", "--quiet", "--exit-code", "HEAD", "--"]),
        ("clone-utility-source", git + ["clone", "--no-checkout", "--no-tags", "https://github.com/buster14a/buster.git", expected["root"]]),
        ("fetch-utility-pins", git + ["-C", expected["root"], "fetch", "--no-tags", "origin", expected["base"], expected["head"], plan["pull_head"]]),
        ("baseline-tree", git + ["-C", expected["root"], "rev-parse", expected["base"] + "^{tree}"]),
        ("candidate-tree", git + ["-C", expected["root"], "rev-parse", expected["head"] + "^{tree}"]),
        ("candidate-first-parent", git + ["-C", expected["root"], "rev-parse", expected["head"] + "^1"]),
        ("candidate-second-parent", git + ["-C", expected["root"], "rev-parse", expected["head"] + "^2"])]
    identity = ["--mode", "main", "--repository", "buster14a/buster", "--ref", "refs/heads/main",
                "--pull", authority["pull"], "--pull-head", plan["pull_head"], "--base", expected["base"],
                "--base-tree", expected["base_tree"], "--head", expected["head"], "--head-tree", expected["head_tree"],
                "--trusted-revision", plan["trusted_revision"], "--request-run-id", authority["request_id"],
                "--run-id", authority["run_id"], "--run-attempt", "1"]
    for leg, policy in (("legacy", "legacy-rebuild"), ("snapshot", "snapshot-v1")):
        compare = [host["python"], "-B", host["comparator"], "--candidate", expected["root"],
                   "--lab", host["trusted_lab"], "--work", expected["output"] + "/" + leg + "-work",
                   "--evidence", expected["output"] + "/" + leg + "-evidence",
                   "--summary", expected["output"] + "/" + leg + ".md", "--closure-policy", policy]
        compare += (["--main-owned-phases", "--main-profile", "compiler-compare-v1"]
                    if expected_phase_schema == "buster-compiler-main-owned-phases-v1" else ["--utility-owned-phases"])
        compare += ["--closure-driver", host["native_driver"]]
        commands += [
            (leg + "-reset-checkout", git + ["-C", expected["root"], "checkout", "--quiet", "--detach", expected["head"]]),
            (leg + "-reset-tracked-source", git + ["-C", expected["root"], "reset", "--hard", "--quiet", expected["head"]]),
            (leg + "-reset-build-cache", git + ["-C", expected["root"], "clean", "-fdx"]),
            (leg + "-trusted-bootstrap", [trusted_root + "/build.sh", "compiler_profile_qualification", "--plan"]),
            (leg + "-ordinary-compare", compare + identity)]
    columns = {"stage", "phase", "wall_us", "exit_status", "timed_out", "cleanup_failed", "cancelled", "state"}
    proofs = {"owner-supervision.tsv": native_wall}
    phase_wall = 0
    if len(phases) != len(commands):
        raise ValueError("utility controller phase population differs from the native fixed plan")
    for index, (row, (name, argv)) in enumerate(zip(phases, commands), 1):
        if set(row) != columns or row.get("stage") != str(index) or row.get("phase") != name or row.get("state") != "complete" or \
                any(row.get(key) != "0" for key in ("exit_status", "timed_out", "cleanup_failed", "cancelled")):
            raise ValueError("utility controller phase is failed, missing or undeclared")
        wall = sampling_integer(row["wall_us"], True)
        phase_wall += wall
        stem = f"controller-{index}-{name}"
        if parse_argv(files.get(stem + ".argv")) != argv:
            raise ValueError("utility controller argv changed the admitted recipe")
        for stream in ("stdout", "stderr"):
            raw = files.get(stem + "." + stream + ".log")
            if not isinstance(raw, bytes) or len(raw) > 1024 * 1024:
                raise ValueError("utility controller raw phase output is missing or oversized")
        proofs[stem + "-supervision.tsv"] = wall
    if phase_wall > duration or {name for name in files if name.endswith("-supervision.tsv")} != set(proofs):
        raise ValueError("utility phase clocks or complete supervision proof population contradict")
    for name, bound in proofs.items():
        proof = sampling_supervision(files.get(name))
        if sampling_integer(proof["wall_us"], True) > bound:
            raise ValueError("utility supervision exceeds its native phase/owner clock")
    return owner, publication, terminal, phases





def utility_table(raw: bytes, schema: str, header: tuple[str, ...]) -> list[dict]:
    if not isinstance(raw, bytes) or not raw.startswith((schema + "\n").encode("ascii")):
        raise ValueError("utility native table schema is absent")
    body = raw[len(schema) + 1:]
    if not body.startswith(("\t".join(header) + "\n").encode("ascii")):
        raise ValueError("utility native table columns differ")
    return sampling_tsv(body, True)


def utility_leg_records(authority: dict, files: dict[str, bytes], host: dict, terminal: dict, phases: list[dict]) -> list[dict]:
    from compiler_preparation import manifest_inventory
    plan = authority["plan"]
    header = ("leg", "preparation_policy", "start_us", "finish_us", "wall_us", "clock_scope", "export_sha256",
              "receipt_sha256", "native_driver_sha256", "inventory_sha256", "process_state")
    legs = utility_table(files.get("utility-legs.tsv"), "BUSTER_COMPILER_CLOSURE_UTILITY_LEGS_V1", header)
    inventory_header = ("leg", "call", "root", "base", "base_tree", "wall_us", "manifest_sha256", "cleanup_us",
                        "adoption_waves", "adopted_signalled", "adopted_reaped", "process_state")
    inventory = utility_table(files.get("utility-inventory.tsv"), "BUSTER_COMPILER_CLOSURE_UTILITY_INVENTORY_V1", inventory_header)
    if len(legs) != 2 or len(inventory) != 2:
        raise ValueError("utility both declared legs and final inventories are required")
    previous, sum_wall = 0, 0
    for index, (row, proof, leg, policy) in enumerate(zip(legs, inventory, ("legacy", "snapshot"), ("legacy-rebuild", "snapshot-v1"))):
        start, finish, wall = (sampling_integer(row[key], True) for key in ("start_us", "finish_us", "wall_us"))
        if row["leg"] != leg or row["preparation_policy"] != policy or row["process_state"] != "complete" or \
                row["clock_scope"] != "bootstrap-through-export-hashfinalization" or \
                row["native_driver_sha256"] != plan["native_driver_sha256"] or start < previous or finish <= start or \
                finish - start != wall or any(not re.fullmatch(r"[0-9a-f]{64}", row[key]) for key in
                    ("export_sha256", "receipt_sha256", "inventory_sha256")):
            raise ValueError("utility complete leg clocks/order/policy/identities contradict")
        previous = finish
        sum_wall += wall
        prefix = "utility/" + leg + "/"
        manifest = files.get("utility-" + leg + "-export.tsv")
        if not isinstance(manifest, bytes) or hashlib.sha256(manifest).hexdigest() != row["export_sha256"]:
            raise ValueError("utility leg export hash differs from native finalized clock record")
        members = {name[len(prefix):]: data for name, data in files.items() if name.startswith(prefix)}
        utility_export_manifest(manifest, members)
        receipt_raw, inventory_raw = members.get("ordinary/receipt.json"), members.get("inventory.manifest.tsv")
        if not isinstance(receipt_raw, bytes) or hashlib.sha256(receipt_raw).hexdigest() != row["receipt_sha256"] or \
                not isinstance(inventory_raw, bytes) or hashlib.sha256(inventory_raw).hexdigest() != row["inventory_sha256"]:
            raise ValueError("utility ordinary receipt/final inventory raw digest differs")
        wanted = {"leg": leg, "call": "compiler_closure_inventory", "root": plan["source_root"],
                  "base": plan["baseline_revision"], "base_tree": plan["baseline_tree"],
                  "manifest_sha256": row["inventory_sha256"], "adopted_signalled": "0", "adopted_reaped": "0",
                  "process_state": "complete"}
        if any(proof.get(key) != value for key, value in wanted.items()):
            raise ValueError("utility final native closure inventory or child ownership is incomplete")
        inv_wall, cleanup, waves = (sampling_integer(proof[key], key == "wall_us") for key in
                                    ("wall_us", "cleanup_us", "adoption_waves"))
        observed = sampling_json(files, prefix + "inventory.json")
        expected = {key: value for key, value in wanted.items() if key not in ("process_state", "adopted_signalled", "adopted_reaped")}
        expected.update(schema="buster-compiler-closure-utility-inventory-v1", state="complete", cleanup_proven=True,
                        cleanup_us=cleanup, adoption_waves=waves, adopted_signalled=0, adopted_reaped=0,
                        qualification_state="unvalidated", default_activated=False)
        if set(observed) != set(expected) | {"wall_us"} or any(
                type(observed.get(key)) is not type(value) or observed.get(key) != value for key, value in expected.items()) or \
                type(observed.get("wall_us")) is not int or not 0 < observed["wall_us"] <= inv_wall or cleanup > observed["wall_us"]:
            raise ValueError("utility final inventory raw producer/ledger observation differs")
        leg_phases = [item for item in phases if item["phase"].startswith(leg + "-")]
        if [item["phase"] for item in leg_phases] != [leg + "-" + name for name in
                ("reset-checkout", "reset-tracked-source", "reset-build-cache", "trusted-bootstrap", "ordinary-compare")]:
            raise ValueError("utility leg phase recipe is incomplete")
        if sum(sampling_integer(item["wall_us"], True) for item in leg_phases) + inv_wall > wall:
            raise ValueError("utility leg clock omits reset/bootstrap/compare/final inventory work")
        manifest_inventory(inventory_raw, {"root": plan["source_root"], "base": plan["baseline_revision"],
                                           "base_tree": plan["baseline_tree"]})
        row["observed_wall_us"] = wall
    duration = sampling_integer(terminal["duration_us"], True)
    if sum_wall + sum(sampling_integer(row["wall_us"], True) for row in phases
                       if not row["phase"].startswith(("legacy-", "snapshot-"))) > duration or \
            sampling_integer(legs[-1]["finish_us"], True) - sampling_integer(legs[0]["start_us"], True) > duration:
        raise ValueError("utility native controller clock omits measured legs or pre-leg work")
    return legs




def lab_raw_inference(files: dict[str, bytes], prefix: str, records: list, variant_meta: dict, summary: dict,
                      raw: dict, seed: int, min_effect: float) -> object:
    """Recompute a compare lab's inference from its retained raw pairs with the trusted uarch_lab statistics.

    Each record must be the next member of the declared ABBA order, succeeded, deterministic and
    bounded, with its counter state equal to its variant's actual probe and the raw counter and
    compiler-metrics files its availability claims. The saved counters, metrics, phases, checks
    and verdict must equal the recomputed ones; ValueError otherwise. Returns the phases.
    """
    from sampling_qualification_receipt import _lab
    loaded = []
    for index, record in enumerate(records):
        number, variant = index // 2 + 1, ("ab" if index // 2 % 2 == 0 else "ba")[index % 2]
        order = "AB" if number % 2 else "BA"
        if not isinstance(record, dict) or type(record.get("pair")) is not int or record["pair"] != number or \
                record.get("order") != order or record.get("variant") != variant or \
                type(record.get("exit")) is not int or record["exit"] != 0 or record.get("identical") is not True or \
                not _number(record.get("span_s")) or \
                not 0 < record["span_s"] <= 3600 or type(record.get("counters")) is not bool:
            raise ValueError("lab raw pair is failed, truncated, unordered or non-deterministic")
        if record["counters"] is not variant_meta[variant]["capabilities"]["perf_stat"]["usable"]:
            raise ValueError("lab raw timed counter status differs from its actual probe")
        for field in ("cpu_s", "maxrss_bytes", "harness_rss_bytes", "wrapper_rss_bytes"):
            value = record.get(field)
            if value is not None and (not _number(value) or value < 0):
                raise ValueError("lab unavailable resource observation is malformed")
        stem = prefix + "pairs/" + f"{number:04d}-{variant}"
        if "child_rss_binding" in record:
            if not raw.get("config", {}).get("ast_retirement_cell") or not isinstance(raw.get("rss_observer"), dict):
                raise ValueError("compiler-child RSS observation escaped its fixed retirement profile")
            observed = _lab.child_rss_packet(files.get(stem + ".rss"), record["child_rss_binding"])
            if record.get("child_rss") != observed:
                raise ValueError("lab saved child-RSS observation differs from its raw native packet")
        counter_raw, metrics_raw = files.get(stem + ".csv"), files.get(stem + ".ccmetrics")
        if record["counters"] and (not isinstance(counter_raw, bytes) or not counter_raw) or \
                variant_meta[variant]["collection"]["metrics_out"] and (not isinstance(metrics_raw, bytes) or not metrics_raw):
            raise ValueError("lab claimed counters/compiler metrics lack their raw files")
        rows = _lab.parse_stat((counter_raw or b"").decode("utf-8"))
        metrics = _lab.parse_cc_metrics((metrics_raw or b"").decode("utf-8"))
        wall_ns = metrics["header"].get("wall_ns")
        task = record.get("cpu_s") if record["counters"] is False else _lab.task_seconds(rows)
        loaded.append(dict(record, values=_lab.stat_values(rows), lines=_lab.stat_lines(rows), metrics=metrics,
                           cc_wall_s=_lab.ratio(wall_ns, 1e9) if type(wall_ns) in (int, float) else None, task_s=task))
    pairs = _lab.complete_pairs(loaded)
    metrics = {key: dict(_lab.compare_series([(pair["metrics_a"][key], pair["metrics_b"][key]) for pair in pairs],
                    unit, direction, seed, time_metric=unit == "s", floor=_lab.metric_floor(key, min_effect)), label=label)
               for key, unit, direction, label in _lab.COMPARE_METRICS}
    phase_records = [(_lab.measured_input(pair["a"]["metrics"]), _lab.measured_input(pair["b"]["metrics"])) for pair in pairs]
    phase_records = [(a, b) for a, b in phase_records if a and b]
    phases = None
    if phase_records:
        phases = {}
        for phase in _lab.PHASES + ("total",):
            a, b = (_lab.phase_ns_series([pair[side] for pair in phase_records], phase) for side in (0, 1))
            values = [(x / 1e6, y / 1e6) for x, y in zip(a, b)] if a and b else [(None, None)] * len(phase_records)
            phases[phase] = _lab.compare_series(values, "ms", "lower", seed, time_metric=True, floor=min_effect / 100.0)
    counter_states = [variant_meta[key]["capabilities"]["perf_stat"] for key in ("a", "b")]
    counters = {"perf_stat": all(row["usable"] for row in counter_states),
                "reason": "; ".join(sorted({str(row["reason"]) for row in counter_states}))}
    if summary.get("counters") != counters or summary.get("phase_metrics") != raw.get("phase_metrics"):
        raise ValueError("lab saved optional counter/phase availability contradicts raw observations")
    if summary.get("metrics") != metrics or summary.get("phases") != phases or \
            summary.get("checks") != _lab.compare_checks(pairs) or \
            summary.get("verdict") != _lab.compare_verdict(metrics, phases, min_effect):
        raise ValueError("lab inference contradicts independently retained raw pairs")
    return phases


def utility_series_replay(files: dict[str, bytes], prefix: str, plan: dict, leg: str, binaries: dict, *,
                          expected_cpu_model: str = "AMD Ryzen 7 9700X 8-Core Processor",
                          expected_profile: str = "compiler-compare-v1", work_root: str | None = None) -> dict:
    """Replay the ordinary profile without turning explanatory warnings into gates."""
    from sampling_qualification_receipt import _lab
    from compiler_receipt import named_main_profile
    named_main_profile(expected_profile)
    fixed = expected_profile == "compiler-main-40pairs-v1"
    raw, summary = sampling_json(files, prefix + "compare.json"), sampling_json(files, prefix + "summary.json")
    records = sampling_json(files, prefix + "pairs.json", False)
    command = _lab.shell_join(["IDE"] + _lab.DEFAULT_COMPILE + ["-o", "OUT"])
    config = {"command": command, "repo_root": plan["source_root"], "cpu": 2, "perf": "perf", "pairs": 40 if fixed else None,
              "target_minutes": 10, "warmups": 1, "seed": 20261003, "profile_steps": [], "sudo": False,
              "require_identical_output": False, "extra": [], "canonical_inline_pair": False,
              "extra_by_variant": {"a": [], "b": []}, "fresh_copy": True, "min_effect_percent": 0.5}
    actual = raw.get("config")
    if raw.get("version") != 1 or raw.get("mode") != "compare" or not isinstance(actual, dict) or actual != config or any(
            type(actual[key]) is not type(value) for key, value in config.items()
            if value is not None and key not in ("target_minutes", "min_effect_percent")) or \
            any(type(actual[key]) not in (int, float) for key in ("target_minutes", "min_effect_percent")):
        raise ValueError("utility ordinary raw lab config changed the immutable profile")
    saved = raw.get("plan")
    count = saved.get("pairs") if isinstance(saved, dict) else None
    if type(count) is not int or not 10 <= count <= 1000 or count % 2 or saved.get("order") != "ABBA" or \
            saved.get("fresh_copy") is not True or not isinstance(saved.get("reason"), str) or \
            (saved["reason"] != "--pairs 40" if fixed else not saved["reason"].startswith("--target-minutes 10:")) or \
            not isinstance(records, list) or len(records) != count * 2:
        raise ValueError("utility ordinary adaptive plan or complete paired population is missing")
    if fixed and count != 40 or work_root is not None and (
            set(saved) != {"pairs", "reason", "order", "fresh_copy"} or
            not isinstance(work_root, str) or not work_root.startswith("/") or
            any(part in ("", ".", "..") for part in work_root[1:].split("/"))):
        raise ValueError("ordinary fixed profile or canonical work root differs")
    expected_plan = dict(saved, seed=20261003, confidence=0.95, bootstrap_resamples=2000, complete_pairs=count, fresh_copy=True)
    if summary.get("plan") != expected_plan or summary.get("cpu") != 2 or summary.get("command") != command or \
            summary.get("repo_root") != plan["source_root"] or summary.get("method") != _lab.COMPARE_METHOD:
        raise ValueError("utility ordinary saved inference/count/workload settings differ")
    steps = raw.get("steps")
    if not isinstance(steps, dict) or set(steps) != {"env", "prepare", "timed"} or any(
            not isinstance(row, dict) or row.get("status") != "ok" for row in steps.values()) or \
            summary.get("steps") != {"env": "ok", "prepare": "ok", "timed": "ok"}:
        raise ValueError("utility ordinary lab has incomplete required steps")
    host = summary.get("host")
    if not isinstance(host, dict) or host.get("cpu_model") != expected_cpu_model or \
            host.get("git_revision") != plan["baseline_revision"]:
        raise ValueError("utility actual lab host or frozen workload source differs")
    variant_meta = {}
    for key, role, name in (("a", "baseline", "ide-base"), ("b", "candidate", "ide-cand")):
        binary, variant = binaries.get(role), (raw.get("variants") or {}).get(key)
        path = (work_root if work_root is not None else plan["output_root"] + "/" + leg + "-work") + "/bin/" + name
        if not isinstance(binary, dict) or not isinstance(variant, dict) or \
                variant != {"role": role, "ide": path, "sha256": binary.get("sha256"), "size_bytes": binary.get("size_bytes")}:
            raise ValueError("utility ordinary raw binary path/hash/true size differs")
        observed = summary.get(role)
        if not isinstance(observed, dict) or any(observed.get(field) != value for field, value in
                (("path", path), ("sha256", binary["sha256"]), ("size_bytes", binary["size_bytes"]),
                 ("runs", count), ("failed", 0), ("identical_runs", count), ("deterministic", True))):
            raise ValueError("utility ordinary deterministic population does not cover every declared pair")
        meta = sampling_json(files, prefix + key + "/lab.json")
        expected = {"command": _lab.shell_join([path] + _lab.DEFAULT_COMPILE + ["-o", "OUT"]), "cpu": 2, "perf": "perf",
                    "repo_root": plan["source_root"], "ide": path, "role": role, "extra": [], "fresh_copy": True}
        if meta.get("config") != expected or not isinstance(meta.get("capabilities"), dict) or \
                not isinstance(meta.get("collection"), dict) or type(meta["collection"].get("metrics_out")) is not bool:
            raise ValueError("utility variant settings or availability facts are missing")
        variant_meta[key] = meta
        perf = meta["capabilities"].get("perf_stat")
        if not isinstance(perf, dict) or type(perf.get("usable")) is not bool or not isinstance(perf.get("reason"), str):
            raise ValueError("utility actual counter availability was not observed")
        for suffix in ("commands.log", "wrapper-rss.log", "source-run.log", "metrics-probe.log", "warmup-0.log"):
            if not isinstance(files.get(prefix + key + "/" + suffix), bytes):
                raise ValueError("utility ordinary raw preparation/warmup/command log is missing")
    sampling_json(files, prefix + "a/env/env.json")
    phases = lab_raw_inference(files, prefix, records, variant_meta, summary, raw, 20261003, 0.5)
    # Flags, cross-arm outputs, wide CIs and NA counters remain report-only.
    return {"complete_pairs": count, "observed_timed_wall_us": sum(row["span_s"] for row in records) * 1000000,
            "verdict": summary["verdict"], "checks": summary["checks"], "warnings": summary.get("warnings"),
            "outputs_identical": summary.get("outputs_identical"), "phase_metrics": summary.get("phase_metrics"),
            "counters": summary.get("counters"), "phases": phases}




def utility_corpus_raw(files: dict[str, bytes], prefix: str, plan: dict, leg: str, binaries: dict, *,
                       expected_cpu_model: str = "AMD Ryzen 7 9700X 8-Core Processor") -> dict:
    """Bind the native complete marker and every fixed timing/diagnostic population."""
    import csv
    import math
    raw_names = ("samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl", "capabilities.jsonl")
    expected = "schema=2 jobs=12 pairs=20 rounds=2 guard=1\n"
    for name in raw_names:
        raw = files.get(prefix + name)
        if not isinstance(raw, bytes):
            raise ValueError("utility complete corpus raw file is missing")
        expected += hashlib.sha256(raw).hexdigest() + " " + name + "\n"
    if files.get(prefix + "complete.txt") != expected.encode("ascii"):
        raise ValueError("utility corpus completion hashes differ from retained raw bytes")
    metadata, summary = sampling_json(files, prefix + "metadata.json"), sampling_json(files, prefix + "summary.json")
    problems = classify_throughput(summary, metadata, binaries)
    if problems:
        raise ValueError("utility ordinary corpus is incomplete: " + "; ".join(problems[:12]))
    cpuinfo = files.get(prefix + "cpuinfo.txt")
    if not isinstance(cpuinfo, bytes):
        raise ValueError("utility corpus actual CPU observation is missing")
    models = [line.partition(":")[2].strip() for line in cpuinfo.decode("ascii").splitlines() if line.startswith("model name")]
    if not models or any(model != expected_cpu_model for model in models):
        raise ValueError("utility corpus raw CPU observation differs from its native outer owner")
    wanted_jobs = [(name, mode) for name in THROUGHPUT_PROFILE["workloads"] for mode in THROUGHPUT_PROFILE["modes"]]
    expected_jobs = "".join(f"{index}\t{name}/{mode}\n" for index, (name, mode) in enumerate(wanted_jobs))
    if files[prefix + "jobs.tsv"] != expected_jobs.encode("ascii") or metadata.get("flags") != [] or \
            metadata.get("allocation_compilers") != [] or metadata.get("cache_policy") != "warm-filesystem-new-process" or \
            metadata.get("clock") != "monotonic" or any(
            type(metadata.get(key)) is not int or metadata[key] != value
            for key, value in (("seed", 20260907), ("scale", 1), ("input_schema", 1))):
        raise ValueError("utility corpus jobs, flags or original launch settings differ")
    provenance = metadata["compiler_provenance"]
    for role, row, name in zip(("baseline", "candidate"), provenance, ("ide-base", "ide-cand")):
        binary = binaries[role]
        wanted = {"path": plan["output_root"] + "/" + leg + "-work/bin/" + name,
                  "revision_label": plan["baseline_revision" if role == "baseline" else "candidate_revision"],
                  "sha256": binary["sha256"], "bytes": binary["size_bytes"]}
        if row != wanted or type(row.get("bytes")) is not int:
            raise ValueError("utility corpus actual binary paths/hashes/sizes differ")
    jobs = metadata.get("jobs")
    if not isinstance(jobs, list) or len(jobs) != 12:
        raise ValueError("utility complete corpus generated-input metadata is missing")
    corpus_root = plan["output_root"] + "/" + leg + "-work/throughput"
    for index, (row, (name, mode)) in enumerate(zip(jobs, wanted_jobs)):
        if not isinstance(row, dict) or type(row.get("job")) is not int or row["job"] != index or \
                row.get("name") != name or row.get("mode") != mode or row.get("artifact") != "object" or \
                not isinstance(row.get("source"), str) or not row["source"].startswith(corpus_root + "/inputs/"):
            raise ValueError("utility corpus generated-input source or mode population differs")
        suffix = row["source"][len(corpus_root) + 1:]
        source = files.get(prefix + suffix)
        if "/" in suffix[len("inputs/"):] or not isinstance(source, bytes) or \
                type(row.get("bytes")) is not int or row["bytes"] != len(source) or \
                row.get("sha256") != hashlib.sha256(source).hexdigest():
            raise ValueError("utility corpus generated input hash/size differs from raw source data")
    header = ("round,pair,order,variant,job,wall_seconds,user_seconds,system_seconds,peak_rss_bytes,cycles,"
              "instructions,branches,branch_misses,cache_references,cache_misses,arena_calls,arena_bytes,"
              "output_bytes,source_bytes,source_lines,source_functions,output_sha256,minor_faults,major_faults,"
              "voluntary_context_switches,involuntary_context_switches").split(",")
    populations, sample_ids, timed_us = {}, set(), 0.0
    for filename, pair_limit in (("samples.csv", 20), ("telemetry.csv", 3)):
        raw = files[prefix + filename]
        if not raw.endswith(b"\n"):
            raise ValueError("utility corpus raw table is truncated")
        table = csv.reader(io.StringIO(raw.decode("ascii")), strict=True)
        if next(table, None) != header:
            raise ValueError("utility corpus raw table schema differs")
        rows, orders, identity_outputs = {}, {}, {}
        for fields in table:
            if len(fields) != len(header):
                raise ValueError("utility corpus raw row shape differs")
            round_no, pair, order, variant, job = [sampling_integer(value) for value in fields[:5]]
            key = (round_no, pair, variant, job)
            order_key = (round_no, pair, job)
            if round_no >= 2 or pair >= pair_limit or order >= 2 or variant >= 2 or job >= 12 or key in rows or \
                    order in orders.setdefault(order_key, set()):
                raise ValueError("utility corpus raw row is duplicate or outside its fixed population")
            orders[order_key].add(order)
            values = []
            for position, text in enumerate(fields[5:17], 5):
                if position >= 9 and text == "NA":
                    values.append(None)
                    continue
                value = float(text)
                if not math.isfinite(value) or value < 0 or position in (5, 8) and value <= 0:
                    raise ValueError("utility corpus raw resource value is malformed")
                values.append(value)
            for position in (*range(17, 21), *range(22, 26)):
                if position >= 22 and fields[position] == "NA":
                    continue
                sampling_integer(fields[position], position in (17, 18, 19))
            if not re.fullmatch(r"[0-9a-f]{64}", fields[21]):
                raise ValueError("utility corpus deterministic-output hash is malformed")
            row_job = jobs[job]
            if any(type(row_job.get(label)) is not int or row_job[label] < 0 for label in ("bytes", "physical_lines", "defined_functions")) or any(int(fields[position]) != row_job[label] for position, label in
                   ((18, "bytes"), (19, "physical_lines"), (20, "defined_functions"))):
                raise ValueError("utility corpus raw source denominators differ from generated input")
            identity = (fields[17], fields[21], *fields[18:21])
            old = identity_outputs.setdefault((job, variant), identity)
            if identity != old:
                raise ValueError("utility corpus output or workload changed within a measured variant")
            rows[key] = fields
            sample = (f"timing-r{round_no}-p{pair}-j{job}-v{variant}" if filename == "samples.csv" else
                      f"{'pmu' if round_no == 0 else 'allocations'}-j{job}-p{pair}-v{variant}")
            sample_ids.add(sample)
            if filename == "samples.csv":
                timed_us += values[0] * 1000000
        expected_count = 12 * 2 * 2 * pair_limit
        if filename == "samples.csv" and len(rows) != expected_count:
            raise ValueError("utility full corpus fixed timed population is incomplete")
        for job in range(12):
            for number in range(2):
                count = sum(key[0] == number and key[3] == job for key in rows)
                if count not in ((2 * pair_limit,) if filename == "samples.csv" else (0, 2 * pair_limit)):
                    raise ValueError("utility corpus diagnostic population is partial")
        populations[filename] = len(rows)
    sample_ids.update(f"warmup-j{job}-w{repeat}-v{variant}" for job in range(12) for repeat in range(2) for variant in range(2))
    records = {}
    for filename in ("commands.jsonl", "capabilities.jsonl"):
        rows = {}
        raw = files[prefix + filename]
        if not raw.endswith(b"\n"):
            raise ValueError("utility native corpus command/status stream is truncated")
        for line in raw.decode("utf-8").splitlines():
            row = sampling_json({"row": line.encode("utf-8")}, "row")
            sample = row.get("sample") if isinstance(row, dict) else None
            if not isinstance(sample, str) or sample in rows:
                raise ValueError("utility corpus command/status population is ambiguous")
            rows[sample] = row
        if set(rows) != sample_ids:
            raise ValueError("utility corpus warmup/timed/diagnostic commands and statuses are incomplete")
        records[filename] = rows
    for sample in sample_ids:
        status = records["capabilities.jsonl"][sample]
        if any(type(status.get(key)) is not int or status[key] != 0 for key in ("exit_code", "signal", "timeout", "launch_error")):
            raise ValueError("utility raw corpus native child failed or was not reaped")
        row = records["commands.jsonl"][sample]
        parsed = re.search(r"-j([0-9]+)-(?:p[0-2]-|w[0-1]-)?v([01])$", sample)
        if parsed is None:
            raise ValueError("utility corpus sample identity cannot bind its exact command")
        job_index, variant = map(int, parsed.groups())
        job = jobs[job_index]
        wanted_argv = [provenance[variant]["path"], "cc", "-g0", "-O0",
                       "-fsource-metrics=" + corpus_root + "/artifacts/" + sample + ".metrics", "-c",
                       "-fregister-allocator=" + job["mode"], job["source"], "-o",
                       corpus_root + "/artifacts/" + job["name"] + "-" + job["mode"] + "-" + str(variant) + ".o"]
        if row.get("cwd") != plan["source_root"] or row.get("argv") != wanted_argv or \
                not isinstance(files.get(prefix + "artifacts/" + sample + ".log"), bytes):
            raise ValueError("utility corpus retained command or native raw log differs from the original harness")
    return {"complete_timed_samples": populations["samples.csv"], "diagnostic_samples": populations["telemetry.csv"],
            "observed_timed_wall_us": timed_us, **throughput_digest(summary)}




def utility_ordinary_leg(authority: dict, files: dict[str, bytes], host: dict, row: dict, phases: list[dict], *,
                         expected_phase_schema: str = "buster-compiler-utility-phases-v1") -> dict:
    from compiler_preparation import manifest_inventory
    plan, leg, policy = authority["plan"], row["leg"], row["preparation_policy"]
    prefix, raw_prefix = "utility/" + leg + "/ordinary/", "utility/" + leg + "/"
    receipt = sampling_json(files, prefix + "receipt.json")
    summary = sampling_json(files, raw_prefix + "lab/summary.json")
    throughput = {"summary": sampling_json(files, raw_prefix + "throughput/summary.json"),
                  "metadata": sampling_json(files, raw_prefix + "throughput/metadata.json")}
    expected = {"mode": "main", "repository": authority["repository"], "ref": "refs/heads/main", "pull": authority["pull"],
                "pull_head": plan["pull_head"], "base": plan["baseline_revision"], "base_tree": plan["baseline_tree"],
                "head": plan["candidate_revision"], "head_tree": plan["candidate_tree"], "trusted_revision": plan["trusted_revision"],
                "request_run_id": authority["request_id"], "run_id": authority["run_id"], "run_attempt": "1",
                "first_parent": plan["baseline_revision"], "range": "1"}
    closure = {}
    if leg == "snapshot":
        closure = {operation: files.get(prefix + "closure-" + operation + ".json.manifest.tsv")
                   for operation in ("snapshot", "restore", "verify")}
    elif receipt.get("closure") is not None:
        raise ValueError("utility legacy treatment gained an undeclared snapshot closure")
    ownership = receipt.get("phase_ownership")
    owned_paths = {"driver_path": host["native_driver"], "trusted_root": plan["trusted_root"],
                   "candidate_root": plan["source_root"], "work_root": plan["output_root"] + "/" + leg + "-work",
                   "evidence_root": plan["output_root"] + "/" + leg + "-evidence",
                   "binaries_root": plan["output_root"] + "/" + leg + "-work/bin",
                   "directory": plan["output_root"] + "/" + leg + "-evidence/owned-phases",
                   "python_path": host["python"], "lab_path": host["trusted_lab"],
                   "bootstrap_marker_sha256": host["bootstrap_marker_sha256"],
                   "driver_sha256": plan["native_driver_sha256"], "trusted_revision": plan["trusted_revision"]}
    if not isinstance(ownership, dict) or ownership.get("schema") != expected_phase_schema or \
            ownership.get("owned_preflight") is not True or \
            any(ownership.get(key) != value for key, value in owned_paths.items()) or \
            not isinstance(ownership.get("phases"), list):
        raise ValueError("utility ordinary " + leg + " ownership schema, paths or pinned preflight differ")
    owned = {}
    for phase in ownership["phases"]:
        name = phase.get("file") if isinstance(phase, dict) else None
        if not isinstance(name, str) or not re.fullmatch(r"[0-9]{4}\.json", name) or name in owned:
            raise ValueError("utility ordinary owned-phase population is ambiguous")
        owned[name] = {label: files.get(prefix + "owned-phases/" + name + suffix)
                       for label, suffix in (("receipt", ""), ("command", ".argv"), ("stdout", ".stdout"),
                                             ("stderr", ".stderr"), ("bootstrap", ".bootstrap.complete"))}
    closure["owned_phases"] = owned
    closure["owned_throughput"] = {"summary": files.get(prefix + "throughput/summary.json"),
                                   "metadata": files.get(prefix + "throughput/metadata.json")}
    raw_owned = {name[len(prefix + "owned-phases/"):] for name in files if name.startswith(prefix + "owned-phases/")}
    if raw_owned != {name + suffix for name in owned for suffix in ("", ".argv", ".stdout", ".stderr", ".bootstrap.complete")}:
        raise ValueError("utility ordinary owned-phase raw files are missing or undeclared")
    throughput["closure"] = closure
    conclusion, unused_title, problems = decide(expected, True, "success", receipt, summary, "report-only", throughput,
        expected_phase_schema=expected_phase_schema, expected_profile="compiler-compare-v1",
        expected_preparation_policy=policy, require_owned_phases=True, require_owned_preflight=True,
        expected_phase_driver_sha256=plan["native_driver_sha256"], expected_measurement_revision=plan["trusted_revision"])
    problems += validate_closure(receipt, closure, expected_policy=policy,
        expected_phase_driver_sha256=plan["native_driver_sha256"], expected_trusted_revision=plan["trusted_revision"],
        require_owned_phases=True, require_owned_preflight=True, expected_phase_schema=expected_phase_schema,
        expected_profile="compiler-compare-v1")
    if conclusion != "success" or problems or receipt.get("coverage") != {"first_parent": expected["first_parent"], "range": "1"} or \
            receipt.get("preparation_policy") != policy or receipt.get("profile") != PROFILE or \
            receipt.get("throughput_profile") != THROUGHPUT_PROFILE or "scaling_profile" in receipt or \
            receipt.get("reasons") != [] or (receipt.get("inline_acceptance") or {}).get("requested") is not False:
        raise ValueError("utility ordinary leg is failed/incomplete or changed its original treatment: " + "; ".join(problems[:12]))
    binaries = receipt.get("binaries")
    if not isinstance(binaries, dict) or set(binaries) != {"baseline", "candidate"}:
        raise ValueError("utility ordinary true built binary population is missing")
    unused_root, inventory, unused_bindings, unused_order = manifest_inventory(files.get(raw_prefix + "inventory.manifest.tsv"),
        {"root": plan["source_root"], "base": plan["baseline_revision"], "base_tree": plan["baseline_tree"]})
    for role in ("baseline", "candidate"):
        item = binaries[role]
        if not isinstance(item, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(item.get("sha256", ""))) or \
                type(item.get("size_bytes")) is not int or not 0 < item["size_bytes"] <= 512 << 20 or \
                item.get("revision") != plan["baseline_revision" if role == "baseline" else "candidate_revision"]:
            raise ValueError("utility ordinary binary digest/true size/source revision is malformed")
        cache = files.get(prefix + role + ".CMakeCache.txt")
        if not isinstance(cache, bytes) or b"BUSTER_INCLUDE_TESTS:BOOL=OFF\n" not in cache or \
                ("CMAKE_HOME_DIRECTORY:INTERNAL=" + plan["source_root"] + "\n").encode() not in cache:
            raise ValueError("utility ordinary actual Clang Release/tests-off configuration is missing")
    baseline = inventory["build", "Release/ide"]
    if baseline[6] != binaries["baseline"]["sha256"] or int(baseline[5]) != binaries["baseline"]["size_bytes"]:
        raise ValueError("utility final source closure contains a different baseline executable")
    # Any ordinary exported duplicate must be the same bytes as the independent
    # complete native work-tree copy. Its optional size cap cannot hide raw data.
    for name, raw in files.items():
        if name.startswith(prefix + "lab/") or name.startswith(prefix + "throughput/"):
            retained = raw_prefix + name[len(prefix):]
            if files.get(retained) != raw:
                raise ValueError("utility ordinary exported copy differs from complete native raw evidence")
    series = utility_series_replay(files, raw_prefix + "lab/", plan, leg, binaries, expected_cpu_model=host["cpu_model"])
    corpus = utility_corpus_raw(files, raw_prefix + "throughput/", plan, leg, binaries, expected_cpu_model=host["cpu_model"])
    phase = next(item for item in phases if item["phase"] == leg + "-ordinary-compare")
    outer_wall = sampling_integer(phase["wall_us"], True)
    if series["observed_timed_wall_us"] + corpus["observed_timed_wall_us"] > outer_wall + 2:
        raise ValueError("utility raw self-host/corpus timed work exceeds the actual ordinary phase")
    if sum(sampling_integer(str(item.get("bridge_wall_us")), True)
                                for item in receipt["phase_ownership"]["phases"]) > outer_wall:
        raise ValueError("utility ordinary child ownership clocks exceed their actual outer owner")
    return {"preparation_policy": policy, "state": "complete", "native_leg_wall_us": row["observed_wall_us"],
            "export_sha256": row["export_sha256"], "receipt_sha256": row["receipt_sha256"],
            "inventory_sha256": row["inventory_sha256"], "binaries": binaries, "series": series, "throughput": corpus,
            "ordinary_timings": receipt.get("timings"), "explanatory_warnings_are_report_only": True}




def physical_clock_binding(authority: dict, files: dict[str, bytes], job: dict, kind: str, job_name: str) -> dict:
    row = sampling_tsv(files.get("physical-job-clock.tsv"))
    wanted = {"schema": "buster-compiler-physical-job-clock-v1", "kind": kind, "repository": authority["repository"],
              "run_id": authority["run_id"], "run_attempt": str(authority["executor"]["run_attempt"]), "policy_trusted_revision": authority["executor"]["head_sha"],
              "job_id": str(job["id"]), "job_name": job_name, "runner_id": str(job["runner_id"]),
              "runner_name": job["runner_name"], "started_at": job["started_at"],
              "timestamp_precision_us": "1000000", "observation_scope": "public-platform-job-start"}
    fields = {"started_unix_us", "start_lower_unix_us", "observer_started_unix_us", "observer_finished_unix_us", "observer_monotonic_elapsed_us"}
    if set(row) != set(wanted) | fields or any(row.get(key) != value for key, value in wanted.items()):
        raise ValueError("utility native pre-entry clock differs from fresh platform job facts")
    value = {key: sampling_integer(row[key], key != "observer_monotonic_elapsed_us") for key in fields}
    start = round(datetime.fromisoformat(job["started_at"].replace("Z", "+00:00")).timestamp() * 1000000)
    finish = round(datetime.fromisoformat(job["completed_at"].replace("Z", "+00:00")).timestamp() * 1000000) + 1000000
    if value["started_unix_us"] != start or value["start_lower_unix_us"] != start - 1000000 or \
            not start <= value["observer_started_unix_us"] <= value["observer_finished_unix_us"] <= finish or \
            value["observer_monotonic_elapsed_us"] > 120000000 or \
            abs(value["observer_finished_unix_us"] - value["observer_started_unix_us"] - value["observer_monotonic_elapsed_us"]) > 1000000:
        raise ValueError("utility pre-entry public UTC/monotonic clock observation contradicts")
    return {"job_id": job["id"], "started_at": row["started_at"], **value,
            "record_sha256": hashlib.sha256(files["physical-job-clock.tsv"]).hexdigest(),
            "observed_pre_entry_us": value["observer_finished_unix_us"] - value["start_lower_unix_us"]}


def utility_clock_binding(authority: dict, files: dict[str, bytes], job: dict) -> dict:
    return physical_clock_binding(authority, files, job, "utility", UTILITY_HOST_JOB)




def utility_validate(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    if any(name.rsplit("/", 1)[-1] in ("fixture-plan.json", "fixture-status.json", "cleanup-uncertain", "utility-overrun.tsv")
           for name in files):
        raise ValueError("diagnostic, cleanup-uncertain or overrun Utility evidence cannot become publication authority")
    for name in files:
        if name.endswith(("compare.json", "summary.json", "metadata.json", "receipt.json", "host.json")) and \
                sampling_json(files, name).get("diagnostic_fixture") is True:
            raise ValueError("diagnostic Utility evidence cannot become physical publication authority")
    if authority["history"]:
        raise ValueError("Utility is one charged attempt; any prior attempt prohibits replacement")
    transport = historical_transport(authority, files, "utility")
    for name, raw in transport.items():
        if files.get(name) != raw:
            raise ValueError("Utility native transport differs from freshly authenticated native admission")
    host, plan = utility_source_identity(api, authority, files), authority["plan"]
    claim = sampling_tsv(files.get("claim.tsv"))
    wanted = {"schema": "buster-compiler-closure-utility-claim-v1", "profile": "compiler-baseline-closure-utility-v1",
              "phase": "utility", "packet": "0", "plan_revision": authority["admitted"]["utility_plan_revision"],
              "plan_sha256": authority["admitted"]["utility_plan_sha256"], "request_run_id": authority["request_id"],
              "request_run_attempt": "1", "executor_run_id": authority["run_id"], "executor_run_attempt": "1",
              "request_head": authority["head"], "policy_trusted_revision": authority["executor"]["head_sha"],
              "measurement_trusted_revision": plan["trusted_revision"], "source_root": plan["source_root"],
              "output_root": plan["output_root"], "driver": host["native_driver"], "pull": authority["pull"],
              "pull_head": plan["pull_head"], "bootstrap_marker_sha256": host["bootstrap_marker_sha256"],
              "reservation_seconds": "5400", "worker_seconds": "5280", "tail_seconds": "120", "state": "claimed"}
    for name, label in (("request.txt", "request_sha256"), ("plan.tsv", "plan_transport_sha256"),
                        ("allowlist.tsv", "allowlist_sha256"), ("facts.tsv", "facts_sha256"), ("history.tsv", "history_sha256")):
        wanted[label] = hashlib.sha256(transport[name]).hexdigest()
    clock_raw = files.get("physical-job-clock.tsv")
    if not isinstance(clock_raw, bytes):
        raise ValueError("Utility native pre-entry clock is missing")
    wanted["physical_job_clock_sha256"] = hashlib.sha256(clock_raw).hexdigest()
    if set(claim) != set(wanted) | {"evidence"} or any(claim.get(key) != value for key, value in wanted.items()) or \
            not isinstance(claim.get("evidence"), str) or not claim["evidence"].startswith("/") or \
            claim["evidence"] == "/" or any(part in ("", ".", "..") for part in claim["evidence"][1:].split("/")):
        raise ValueError("Utility immutable first-claim identities contradict fresh authority")
    owner, publication, terminal, phases = utility_phase_proofs(authority, files, host, expected_phase_schema="buster-compiler-main-owned-phases-v1")
    job = utility_job(api, authority)
    accounting = sampling_job_accounting(job, sampling_integer(publication["observed_wall_us"], True), 5400, job_name=UTILITY_HOST_JOB)
    clock = utility_clock_binding(authority, files, job)
    if sampling_integer(owner["job_elapsed_at_native_entry_us"], True) + 2000000 < clock["observed_pre_entry_us"]:
        raise ValueError("Utility native entry predates its public platform observation")
    accounting.update(native_clock_observations_validated=True, native_owner_wall_us=sampling_integer(owner["native_entry_wall_us"], True),
                      native_platform_start_wall_us=sampling_integer(owner["physical_packet_wall_us"], True),
                      job_elapsed_at_native_entry_us=sampling_integer(owner["job_elapsed_at_native_entry_us"], True),
                      native_observed_wall_us=sampling_integer(publication["observed_wall_us"], True),
                      owner_publication_us=sampling_integer(publication["publication_us"]), observation_publication_us=None,
                      native_controller_us=sampling_integer(terminal["duration_us"], True), pre_entry_platform_clock=clock)
    rows = utility_leg_records(authority, files, host, terminal, phases)
    legs = {row["leg"]: utility_ordinary_leg(authority, files, host, row, phases, expected_phase_schema="buster-compiler-main-owned-phases-v1") for row in rows}
    net = utility_net_observation(rows[0]["observed_wall_us"], rows[1]["observed_wall_us"], accounting["physical_job_wall_upper_us"])
    state = "complete-valid-research" if net["criterion_met"] else "complete-negative-research"
    history = [{"phase": "utility", "packet": 0, "request_run_id": authority["request_id"], "run_id": authority["run_id"],
                "run_attempt": "1", "state": state, "reservation_seconds": 5400,
                "actions_job_occupancy_us": accounting["physical_job_wall_upper_us"],
                "campaign": authority["admitted"]["utility_plan_sha256"],
                "freeze_revision": authority["admitted"]["utility_plan_revision"]}]
    return {"schema": "buster-compiler-closure-utility-publication-v1", "packet_state": state,
            "qualification_state": "unqualified", "default_activated": False, "routine_profile_enabled": False,
            "evidence_class": "unqualified-closure-utility-research", "phase": "utility", "packet": 0,
            "plan_revision": authority["admitted"]["utility_plan_revision"], "plan_sha256": authority["admitted"]["utility_plan_sha256"],
            "source_identity": {key: plan[key] for key in ("baseline_revision", "baseline_tree", "candidate_revision", "candidate_tree", "pull_head")},
            "measurement_trusted_revision": plan["trusted_revision"], "policy_trusted_revision": authority["executor"]["head_sha"],
            "host": host, "platform_runner": {key: job[key] for key in ("id", "runner_id", "runner_name", "labels")},
            "reservation_seconds": 5400, "leg_order": ["legacy", "snapshot"], "legs": legs,
            "accounting": accounting, "utility_observation": net, "hosted_api_publication_us": None,
            "terminal_publication_us": None, "general_workload_savings_assessed": False,
            "baseline_recipe_scope": "declared-supervised-ordinary-recipes",
            "historical_unwrapped_legacy_savings_assessed": False,
            "authenticated_attempt_history": history, "problems": []}




def utility_observed_costs(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    result = {"native_owner_wall_us": None, "native_platform_start_wall_us": None,
              "unvalidated_native_packet_wall_us": None, "native_clock_observations_validated": False,
              "native_observed_wall_us": None, "owner_publication_us": None,
              "observation_publication_us": None, "native_controller_us": None, "physical_job_wall_us": None,
              "physical_job_wall_upper_us": None, "queue_delay_seconds": None}
    for name, keys in (("owner.tsv", {"native_entry_wall_us": "native_owner_wall_us", "physical_packet_wall_us": "unvalidated_native_packet_wall_us"}),
                       ("owner-publication.tsv", {"observed_wall_us": "native_observed_wall_us", "publication_us": "owner_publication_us"}),
                       ("utility.tsv", {"duration_us": "native_controller_us"})):
        try:
            row = sampling_tsv(files.get(name))
            for source, target in keys.items():
                try:
                    result[target] = sampling_integer(row.get(source), target != "owner_publication_us")
                except (ValueError, TypeError):
                    pass
        except (ValueError, UnicodeError, TypeError):
            pass
    try:
        owner = sampling_tsv(files.get("owner.tsv"))
        if owner.get("wall_scope") == "public-platform-job-start-lower-through-child-cleanup-before-terminal-publication":
            result["native_platform_start_wall_us"] = sampling_integer(owner["physical_packet_wall_us"], True)
    except (ValueError, UnicodeError, TypeError, KeyError):
        pass
    try:
        job = utility_job(api, authority)
        result.update(platform_job_state=job.get("status"), platform_job_conclusion=job.get("conclusion"))
        stamps = [datetime.fromisoformat(job[key].replace("Z", "+00:00")) for key in ("created_at", "started_at", "completed_at")]
        if all(stamp.utcoffset() is not None for stamp in stamps):
            wall = round((stamps[2] - stamps[1]).total_seconds() * 1000000)
            queue = (stamps[1] - stamps[0]).total_seconds()
            if wall > 0 and queue >= 0:
                result.update(physical_job_wall_us=wall, physical_job_wall_upper_us=wall + 2000000, queue_delay_seconds=queue)
    except (ValueError, KeyError, TypeError, AttributeError, OverflowError, OSError, urllib.error.URLError, TimeoutError):
        pass
    result["native_leg_clocks"] = None
    try:
        rows = utility_table(files.get("utility-legs.tsv"), "BUSTER_COMPILER_CLOSURE_UTILITY_LEGS_V1",
            ("leg", "preparation_policy", "start_us", "finish_us", "wall_us", "clock_scope", "export_sha256",
             "receipt_sha256", "native_driver_sha256", "inventory_sha256", "process_state"))
        result["native_leg_clocks"] = [{"leg": row["leg"], "process_state": row["process_state"],
                                       "unvalidated_wall_us": sampling_integer(row["wall_us"])} for row in rows]
    except (ValueError, UnicodeError, TypeError, KeyError):
        pass
    return result




def utility_publish(environment: dict) -> int:
    api, authority = utility_authority(environment)
    if len(utility_checks(api, authority)) != 1:
        raise ValueError("Utility publication requires the existing native-admitted attempt check")
    files, artifact = {}, {}
    result = {"schema": "buster-compiler-closure-utility-publication-v1", "packet_state": "incomplete",
              "qualification_state": "unqualified", "default_activated": False, "routine_profile_enabled": False,
              "evidence_class": "unqualified-closure-utility-research", "phase": "utility", "packet": 0,
              "reservation_seconds": 5400, "general_workload_savings_assessed": False, "hosted_api_publication_us": None, "problems": [],
              "baseline_recipe_scope": "declared-supervised-ordinary-recipes",
              "historical_unwrapped_legacy_savings_assessed": False}
    try:
        files, artifact = utility_read_artifact(api, authority)
        if environment.get("BQ_UTILITY_RESULT") != "success":
            raise ValueError("utility physical job is failed, cancelled, skipped or unavailable")
        result = utility_validate(api, authority, files)
    except (OSError, ValueError, UnicodeError, TypeError, KeyError, IndexError, AttributeError, RecursionError,
            urllib.error.URLError, TimeoutError) as error:
        result["problems"].append(("evidence validation failed: " + type(error).__name__ + ": " + str(error))[:1000])
        result["accounting"] = utility_observed_costs(api, authority, files)
        result["authenticated_attempt_history"] = list(authority["history"]) + [{
            "phase": "utility", "packet": 0, "request_run_id": authority["request_id"], "run_id": authority["run_id"],
            "run_attempt": "1", "state": "incomplete", "reservation_seconds": 5400,
            "actions_job_occupancy_us": result["accounting"]["physical_job_wall_upper_us"],
            "campaign": authority["admitted"]["utility_plan_sha256"],
            "freeze_revision": authority["admitted"]["utility_plan_revision"]}]
    success = result["packet_state"] == "complete-valid-research" and not result["problems"]
    conclusion = "success" if success else "failure"
    title = ("Valid unqualified utility packet" if success else
             "Complete unqualified Utility; net criterion not met" if result["packet_state"] == "complete-negative-research" else
             "Incomplete unqualified utility packet")
    summary = utility_summary(authority) + f"\nPacket state: {result['packet_state']}. Qualification state: unqualified.\n"
    summary += ("\nOnce-only controlled Utility trace: all authenticated physical-job residual time is charged to snapshot. "
                "Valid ordinary self-host/corpus regressions remain complete report-only data. "
                "Hosted API/publication time is unavailable and separate; general workload savings is unassessed.\n")
    summary += ("Baseline recipe scope: declared-supervised-ordinary-recipes. "
                "Historical unwrapped legacy savings assessed: false.\n")
    if result.get("utility_observation"):
        summary += "\nObserved Utility criterion met: " + str(result["utility_observation"]["criterion_met"]).lower() + ".\n"
    if result["problems"]:
        summary += "\nEvidence problems:\n" + "\n".join("- " + str(problem)[:1000] for problem in result["problems"][:30]) + "\n"
    if result.get("control_failures"):
        summary += "\nCompleted control findings:\n" + "\n".join("- " + str(item)[:1000] for item in result["control_failures"]) + "\n"
    fence = chr(96) * 3
    summary += "\nObserved accounting:\n" + fence + "json\n" + json.dumps(result.get("accounting", {}), sort_keys=True) + "\n" + fence + "\n"
    if artifact:
        summary += "\nEvidence: " + artifact_link(authority["repository"], authority["run_id"], artifact) + "\n"
    report = json.dumps(result, sort_keys=True, indent=2)
    row = utility_write(api, authority, {"status": "completed", "conclusion": conclusion,
        "details_url": run_url(authority["repository"], authority["run_id"], "1"),
        "output": {"title": title, "summary": summary[:TEXT_LIMIT], "text": fence + "json\n" + report[:TEXT_LIMIT - 16] + "\n" + fence}})
    with open(environment.get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
        stream.write(summary + "\n")
    print(f"COMPILER_UTILITY_PUBLISHED {conclusion} check={row.get('id')} state={row.get('status')} qualification=unqualified")
    return 0 if success and row.get("status") == "completed" and row.get("conclusion") == "success" else 1



UNCHANGED_SCHEMA = "buster-ci-main-unchanged-v1"
UNCHANGED_TITLE = "Not measured: compiler/build and benchmark inputs unchanged."
UNCHANGED_KEYS = ("schema", "event", "unchanged", "base", "head", "policy", "subject_changes", "policy_changes", "reason")


def unchanged_proof(api: Api, environment: dict, expected: dict, route: dict | None, compare_result: str) -> tuple[dict, list[str]]:
    """The validated #3923 unchanged-input decision for one authorized MAIN attempt, or its refusal reasons.

    Every identity is re-checked here as data: the native receipt must name
    this attempt's baseline, head and trusted policy; the compare job must not
    have run; the route must be the non-owned legacy route; and the baseline
    must still hold a valid main measurement in GitHub's own check records,
    so a reference that changed or vanished while planning never anchors a skip.
    """
    get = lambda key: environment.get(key, "")  # noqa: E731
    problems: list[str] = []
    try:
        receipt = json.loads(get("BQ_UNCHANGED_RECEIPT"), object_pairs_hook=unique_object)
    except ValueError:
        receipt = None
    if not (isinstance(receipt, dict) and tuple(receipt) == UNCHANGED_KEYS):
        problems.append("unchanged-input receipt is missing or malformed")
        receipt = {}
    for key, wanted in (("schema", UNCHANGED_SCHEMA), ("event", "main_push"), ("unchanged", True),
                        ("base", expected.get("base")), ("head", expected.get("head")),
                        ("policy", get("BQ_TRUSTED_REVISION")), ("reason", "reviewed-prose-only")):
        if receipt and receipt.get(key) != wanted:
            problems.append(f"unchanged-input receipt {key} {receipt.get(key)!r} is not {wanted!r}")
    for key in ("subject_changes", "policy_changes"):
        if receipt and not (type(receipt.get(key)) is int and receipt[key] >= 0):
            problems.append(f"unchanged-input receipt {key} is not a count")
    if compare_result != "skipped":
        problems.append(f"compare job result is {compare_result!r}, not skipped")
    if route is None or route.get("main_owned") is not False:
        problems.append("unchanged-input skips apply only to the non-owned legacy main route")
    if not DECIMAL.fullmatch(get("BQ_CLASSIFICATION_MS")):
        problems.append("hosted classification time is missing")
    reference = {"check_url": get("BQ_REFERENCE_CHECK_URL"), "measured_at": get("BQ_REFERENCE_MEASURED_AT")}
    base = expected.get("base", "")
    if get("BQ_BASE_MEASURED") != "true" or not SHA.fullmatch(base):
        problems.append("the baseline is not an actual measurement")
    elif not problems:
        rows = [row for row in owned_checks(api, base, "main", "") if measured(row, base)]
        if not any(row.get("html_url") == reference["check_url"] and row.get("completed_at") == reference["measured_at"]
                   for row in rows):
            problems.append("the baseline measurement changed or vanished while planning")
    return ({**receipt, "reference": reference, "classification_ms": int(get("BQ_CLASSIFICATION_MS"))}
            if not problems else {}), problems


def unchanged_report(expected: dict, proof: dict, request_attempt: str, coverage: dict) -> str:
    """The neutral report; it links the reference measurement and copies none of its samples or verdict."""
    reference = proof["reference"]
    lines = [f"**{check_name('main')}: neutral**", "", f"### {UNCHANGED_TITLE}", "",
             "No 9700X job was allocated: no compiler was built and nothing was sampled. This is not a "
             "measurement and carries no verdict, samples or 0% change; it makes no claim about physical host "
             "conditions, the host toolchain or performance (docs/compiler-main-unchanged-skip.md).", "",
             f"- Head: `{expected['head']}` (request run {expected['request_run_id']}.{request_attempt}, executor "
             f"run {expected['run_id']} attempt {expected['run_attempt']})",
             f"- Last actual measurement: `{expected['base']}`, [its check]({reference['check_url']}) completed "
             f"{reference['measured_at']}; any regression or failure it reported is unchanged by this skip",
             f"- Range: {expected.get('range', 'NA')} first-parent commits since that measurement "
             f"(first parent `{expected.get('first_parent', 'NA')}`)",
             f"- Classifier: `{proof['schema']}` at trusted revision `{proof['policy']}`; reason "
             f"`{proof['reason']}`; {proof['subject_changes']} prose path changes in the measured subject, "
             f"{proof['policy_changes']} in the measurement policy since the head",
             f"- Observed hosted classification time: {proof['classification_ms'] / 1000:.3f} s. Avoided physical "
             "runner time is not estimated here.", ""]
    lines += render_inventory_coverage(coverage)
    return "\n".join(lines)


def unchanged_publish(api: Api, environment: dict, expected: dict, proof: dict) -> int:
    """Complete the attempt's check as neutral and hand the commit report its entry."""
    get = lambda key: environment.get(key, "")  # noqa: E731
    head, attempt, mode = expected["head"], expected["run_attempt"], "main"
    coverage = covered_inventory_rows(None, "neutral", mode)
    shown = {"mode": mode, "identity": expected, "state": "unchanged-not-measured", "unchanged": proof,
             "coverage": {"first_parent": expected.get("first_parent"), "range": expected.get("range")},
             "inventory_coverage": coverage, "reasons": [UNCHANGED_TITLE]}
    workflow_url = run_url(expected["repository"], expected["run_id"], attempt)
    report = unchanged_report(expected, proof, get("BQ_REQUEST_ATTEMPT"), coverage)
    marker = attempt_marker(head, mode, expected["request_run_id"], get("BQ_REQUEST_ATTEMPT"), attempt)
    body = {"details_url": get("BQ_DETAILS_URL"), "status": "completed", "conclusion": "neutral",
            "output": {"title": UNCHANGED_TITLE, "summary": (report + "\n\n" + links("", workflow_url, attempt, "", ""))[:TEXT_LIMIT],
                       "text": ("```json\n" + json.dumps(shown, sort_keys=True, indent=2))[:TEXT_LIMIT - 4] + "\n```"}}
    rows, written = complete_check(api, head, mode, marker, body)
    check_url = rows[-1].get("html_url", "") if rows and isinstance(rows[-1].get("html_url"), str) else ""
    print(f"BENCH_COMPILER_PUBLISHED neutral unchanged check_runs={[row.get('id') for row in rows]}"
          + ("" if written else " (already completed by another writer; left unchanged)"))
    link_line = links(check_url, workflow_url, attempt, "", "")
    with open(get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
        stream.write(report + "\n\n" + link_line + "\n")
    entry = {"mode": mode, "head": head, "base": expected["base"], "run_id": expected["run_id"], "run_attempt": attempt,
             "request_run_id": expected["request_run_id"], "trusted_revision": expected["trusted_revision"],
             "conclusion": "neutral", "title": UNCHANGED_TITLE, "captured_at": stamp(), "check_url": check_url,
             "run_url": workflow_url, "artifact_url": "", "artifact_expires_at": "NA",
             "markdown": (report + "\n\n" + link_line)[:REPORT_MARKDOWN_LIMIT]}
    write_output(get("GITHUB_OUTPUT"), {"comment": json.dumps(entry, sort_keys=True, separators=(",", ":")), "head": head})
    return 0


def main() -> int:
    if sys.argv[1:] in (["utility-queue"], ["utility-publish"]):
        try:
            return (utility_queue if sys.argv[1] == "utility-queue" else utility_publish)(dict(os.environ))
        except (OSError, ValueError, urllib.error.URLError, TimeoutError) as error:
            print(f"COMPILER_UTILITY_PUBLICATION_REFUSED {error}", file=sys.stderr)
            return 1
    if sys.argv[1:] == ["physical-clock"]:
        try:
            return physical_clock_data(dict(os.environ))
        except (OSError, ValueError, urllib.error.URLError, TimeoutError) as error:
            print(f"COMPILER_PHYSICAL_CLOCK_REFUSED {error}", file=sys.stderr)
            return 1
    if sys.argv[1:] in (["sampling-queue"], ["sampling-publish"]):
        try:
            return (sampling_queue if sys.argv[1] == "sampling-queue" else sampling_publish)(dict(os.environ))
        except (OSError, ValueError, urllib.error.URLError, TimeoutError) as error:
            print(f"COMPILER_SAMPLING_PUBLICATION_REFUSED {error}", file=sys.stderr)
            return 1
    if sys.argv[1:] in (["preparation-queue"], ["preparation-publish"]):
        try:
            return (preparation_queue if sys.argv[1] == "preparation-queue" else preparation_publish)(dict(os.environ))
        except (OSError, ValueError, urllib.error.URLError, TimeoutError) as error:
            print(f"COMPILER_PREPARATION_PUBLICATION_REFUSED {error}", file=sys.stderr)
            return 1
    if sys.argv[1:]:
        print("BENCH_COMPILER_PUBLISH_FAIL unsupported command", file=sys.stderr)
        return 1
    environment = os.environ
    get = lambda key: environment.get(key, "")  # noqa: E731
    recover = bool(get("BQ_RECOVER_RUN_ID"))
    run_id = get("BQ_RECOVER_RUN_ID") if recover else get("BQ_RUN_ID")
    attempt = get("BQ_RECOVER_ATTEMPT") if recover else get("BQ_RUN_ATTEMPT")
    mode = "main" if recover else get("BQ_MODE")
    repository = get("BQ_REPOSITORY")
    head = get("BQ_HEAD_COMMIT")
    code = 1
    if not (mode in MODES and (recover or SHA.fullmatch(head)) and DECIMAL.fullmatch(run_id) and
            DECIMAL.fullmatch(attempt) and get("GH_TOKEN") and
            (recover or DECIMAL.fullmatch(get("BQ_REQUEST_RUN_ID")) and DECIMAL.fullmatch(get("BQ_REQUEST_ATTEMPT")))):
        print("BENCH_COMPILER_PUBLISH_FAIL invalid workflow inputs", file=sys.stderr)
    else:
        api = Api(repository, get("GH_TOKEN"))
        notes: list[str] = []
        workflow_revision = ""
        if recover:
            expected, authorize_result, compare_result, problems = recovered_identity(api, repository, run_id, attempt)
            titled = expected.pop("titled_request", None)
            head = expected["head"]
            authorized = authorize_result == "success" and not problems
            # The original run's own workflow revision, before the Main route rebinds trusted_revision.
            workflow_revision = expected["trusted_revision"]
        else:
            expected = {"mode": mode, "repository": repository, "ref": get("BQ_REF"), "pull": get("BQ_PULL"),
                        "pull_head": get("BQ_PULL_HEAD"), "base": get("BQ_BASE_COMMIT"),
                        "base_tree": get("BQ_BASE_TREE"), "head": head, "head_tree": get("BQ_HEAD_TREE"),
                        "trusted_revision": get("BQ_TRUSTED_REVISION"), "request_run_id": get("BQ_REQUEST_RUN_ID"),
                        "run_id": run_id, "run_attempt": attempt}
            if mode == "main":
                expected.update(first_parent=get("BQ_FIRST_PARENT"), range=get("BQ_RANGE"))
            compare_result, problems, titled = get("BQ_COMPARE_RESULT"), [], None
            authorized = get("BQ_AUTHORIZE_RESULT") == "success" and get("BQ_AUTHORIZED_ATTEMPT") == attempt
        notes.extend(problems)
        receipt = summary = None
        artifact: dict = {}
        throughput: dict = {}
        problem = ""
        route = None
        machine_problems: list[str] = []
        machine_facts: dict = {}
        evidence_bundle: dict | None = None
        if mode == "main" and authorized:
            try:
                route = main_route(api, repository, run_id, attempt)
                expected["trusted_revision"] = route["main_measurement_revision"]
            except (OSError, ValueError, TypeError, urllib.error.URLError, subprocess.SubprocessError) as error:
                authorized = False
                problem = "Main route refused: " + str(error)
                notes.append(problem)
        # #3923: a proven unchanged-input attempt never dispatched the 9700X;
        # a refused proof falls through and reports the missing measurement.
        if mode == "main" and not recover and authorized and get("BQ_UNCHANGED") == "true":
            proof, refusals = unchanged_proof(api, environment, expected, route, compare_result)
            if proof:
                return unchanged_publish(api, environment, expected, proof)
            notes.extend("unchanged-input skip refused: " + reason for reason in refusals)
        # #3714: the hosted pre-dispatch corpus disposition. An unsupported base was never
        # dispatched, so it has no evidence to read; only "historical" admits the named recipe.
        admission = get("BQ_HARNESS_ADMISSION") if mode == "pull" and not recover else "admitted"
        expected_corpus = THROUGHPUT_HISTORICAL_PROFILE if admission == "historical" else THROUGHPUT_PROFILE
        refusal = ""
        if authorized and admission not in HARNESS_ADMISSIONS:
            authorized = False
            problem = f"hosted harness admission {admission!r} is not a reviewed disposition"
            notes.append(problem)
        elif authorized and admission == "unsupported":
            authorized = False
            refusal = (f"unsupported historical throughput harness: merge base {expected.get('base')} predates "
                       f"{THROUGHPUT_HARNESS_FLOOR}, so its --mode all is not the {THROUGHPUT_PROFILE['name']} "
                       f"FAST/QUALITY population; not dispatched to the 9700X. Only a fresh "
                       f"`{THROUGHPUT_HISTORICAL_REQUEST_LINE}` request line can measure this base")
        if authorized:
            receipt, summary, problem, artifact, throughput = read_evidence(api, run_id, artifact_name(head, attempt),
                trusted_main_route=route if route is not None and route["main_owned"] else None)
            if problem:
                notes.append(problem)
            if not recover and not problem and isinstance(receipt, dict) and expected.get("mode") == "pull":
                problem = verify_analyzer_request_freshness(api, expected, receipt, throughput)
                if problem:
                    notes.append(problem)
            if recover:
                problems = bind_request(api, expected, receipt, trusted_main_route=route)
                notes.extend(problems)
                authorized = not problems
            owned = route is not None and route["main_owned"]
            if isinstance(receipt, dict) and not owned and receipt.get("profile") == PROFILE:
                # Recovery verifies under the original request attempt its own run-name records; one
                # that names another request run, or none, cannot name a sealed bundle and refuses it.
                request_attempt = get("BQ_REQUEST_ATTEMPT") if not recover else \
                    titled[1] if titled and titled[0] == expected.get("request_run_id") else ""
                evidence_bundle = verify_evidence_bundle(throughput, expected, expected_corpus, request_attempt, receipt,
                                                         not recover or sealing_required(api, workflow_revision))
                if evidence_bundle_note(evidence_bundle):
                    notes.append(evidence_bundle_note(evidence_bundle))
            if isinstance(receipt, dict):
                require = not recover or machine_record_required(api, workflow_revision)
                machine_problems, machine_facts = machine_record(throughput.get("machine"), receipt, mode, run_id,
                                                                 attempt, require=require)
        conclusion, title, reasons = decide(expected, authorized, compare_result, receipt, summary,
                                            get("BQ_REGRESSION_POLICY"), throughput, not recover,
                                            ([problem] if problem else []) + machine_problems,
                                            **({"expected_phase_schema": route["main_phase_schema"],
                                                "expected_profile": route["main_profile"],
                                                "expected_preparation_policy": route["main_preparation_policy"],
                                                "require_owned_phases": True, "require_owned_preflight": True,
                                                "expected_phase_driver_sha256": route["driver_sha256"],
                                                "expected_measurement_revision": route["main_measurement_revision"]}
                                               if route is not None and route["main_owned"] else
                                               {"expected_throughput_profile": expected_corpus,
                                                "dispatch_refusal": refusal, "evidence_bundle": evidence_bundle}))
        shown = dict(receipt) if isinstance(receipt, dict) else {"mode": mode, "identity": expected}
        # The authorized range, never the host's own account of it.
        shown.pop("coverage", None)
        if mode == "main" and expected.get("range"):
            shown["coverage"] = {"first_parent": expected.get("first_parent"), "range": expected.get("range")}
        shown["reasons"] = reasons
        # Which inventory rows this attempt discharged (#2761): from the trusted
        # mode and the decided conclusion, never from the host's own claim.
        shown["inventory_coverage"] = covered_inventory_rows(receipt, conclusion, mode)
        if machine_facts:
            shown["machine"] = machine_facts
        if evidence_bundle is not None:
            shown["evidence_bundle_verified"] = evidence_bundle
        if route is not None:
            shown["main_route"] = {key: route[key] for key in ("main_owned", *MAIN_ROUTE_FIELDS)}
        # The corpus as its own retained summary says, never the host's digest.
        if isinstance(throughput.get("summary"), dict):
            shown["throughput"] = throughput_digest(throughput["summary"])
        # Scaling likewise comes from the retained bundles, never the host's digest.
        if isinstance(receipt, dict) and "scaling_profile" in receipt:
            shown["scaling"] = scaling_digest(throughput.get("scaling"))
        if isinstance(shown.get("timings"), dict):
            shown["timings"] = dict(shown["timings"], queue_delay_seconds=queue_delay(api, run_id, attempt, mode))
        workflow_url = run_url(repository, run_id, attempt)
        evidence_url = artifact_link(repository, run_id, artifact)
        expires = artifact.get("expires_at", "") if isinstance(artifact.get("expires_at"), str) else ""
        check_url = ""
        if recover:
            check_url = report_check_url(api, head, "") if SHA.fullmatch(head) else ""
        else:
            marker = attempt_marker(head, mode, expected["request_run_id"], get("BQ_REQUEST_ATTEMPT"), attempt)
            report = render(shown, summary, conclusion, notes) + "\n\n" + links("", workflow_url, attempt,
                                                                                 evidence_url, expires)
            body = {"details_url": get("BQ_DETAILS_URL"), "status": "completed", "conclusion": conclusion,
                    "output": {"title": title[:200], "summary": report[:TEXT_LIMIT],
                               "text": ("```json\n" + json.dumps(shown, sort_keys=True, indent=2))[:TEXT_LIMIT - 4]
                               + "\n```"}}
            rows, written = complete_check(api, head, mode, marker, body)
            check_url = rows[-1].get("html_url", "") if rows and isinstance(rows[-1].get("html_url"), str) else ""
            print(f"BENCH_COMPILER_PUBLISHED {conclusion} check_runs={[row.get('id') for row in rows]}"
                  + ("" if written else " (already completed by another writer; left unchanged)"))
        link_line = links(check_url, workflow_url, attempt, evidence_url, expires)
        with open(get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
            stream.write(render(shown, summary, conclusion, notes) + "\n\n" + link_line + "\n")
        identity = shown.get("identity") if isinstance(shown.get("identity"), dict) else expected
        timings = shown.get("timings") if isinstance(shown.get("timings"), dict) else {}
        # Recovery republishes only what the retained evidence still proves;
        # missing or unverifiable evidence is reported, never published.
        recoverable = not recover or (authorized and isinstance(receipt, dict))
        if not recoverable:
            print("BENCH_COMPILER_RECOVERY_REFUSED " + "; ".join(notes or reasons), file=sys.stderr)
        if mode == "main" and SHA.fullmatch(head) and recoverable:
            entry = {"mode": mode, "head": head, "base": expected.get("base") or identity.get("base", ""),
                     "run_id": run_id, "run_attempt": attempt,
                     "request_run_id": expected.get("request_run_id") or identity.get("request_run_id", ""),
                     "trusted_revision": expected.get("trusted_revision", ""), "conclusion": conclusion,
                     "title": title[:200], "captured_at": str(timings.get("finished_at") or timings.get("started_at")
                                                              or "NA")[:40],
                     "check_url": check_url, "run_url": workflow_url, "artifact_url": evidence_url,
                     "artifact_expires_at": expires or "NA",
                     "markdown": commit_report(shown, summary, conclusion, title, notes, link_line)}
            write_output(get("GITHUB_OUTPUT"), {"comment": json.dumps(entry, sort_keys=True, separators=(",", ":")),
                                                "head": head})
        code = 0 if recoverable else 1
    return code


if __name__ == "__main__":
    sys.exit(main())
