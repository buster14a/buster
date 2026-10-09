#!/usr/bin/env python3
"""Validate a 9700X compiler receipt and publish its exact-head check (#2752, #2769).

Run from trusted `main` by the hosted `publish-compiler` (main mode) and
`publish-pull` (pull mode) jobs of `.github/workflows/9700x-direct-bench.yml`,
the only jobs there that complete the check. It reads this run's evidence
artifact through the API as data (a size-bounded zip; nothing in it is
executed), re-checks the receipt against the identities authorization produced
in the same attempt, requires the observed host to be the approved Zen 5 host,
re-derives validity from the lab's own summary.json and the throughput
corpus's own summary.json and metadata.json (#2761), and, when the receipt
names the scaling profile (#424), each scaling bundle's own scaling.json and
scaling-metadata.json, and completes the
attempt's check run named check_name(mode) with external ID attempt_marker(...)
on the head. The checks are report-only; nothing gates merging on them.

Conclusions (the performance verdict never decides; report-only):
    success  a valid core measurement, whatever its direction
    neutral  a pull request head moved before measurement
    failure  authorization refused, evidence missing, mismatched or invalid,
             or an unimplemented regression policy was requested
A failure says the candidate was not benchmarked; it is never relabelled.

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

Map: decide (pure decision), read_evidence, queue_delay, links,
commit_report, recovered_identity, bind_request, report_check_url, main.
Api lives in compiler_github.
"""

from __future__ import annotations

import io
import base64
import hashlib
import json
import os
import re
import stat
import sys
import urllib.error
import urllib.parse
import zipfile
from datetime import datetime

from authorize_compiler import verify as verify_main
from compiler_github import (ARTIFACT_LIMIT, BENCH_WORKFLOW, COMPARE_JOBS, SERVER, TEXT_LIMIT, Api, complete_check,
                             owned_checks, parse_chain, run_url)
from inline_acceptance import metrics as inline_metrics, validate_documents as validate_inline_documents
from compiler_receipt import (validate_closure, ANALYZER_PROFILE, ANALYZER_PROFILE_BY_LINE, ANALYZER_REQUIRED_FILES,
                              ANALYZER_REQUEST_LINES, ANALYZER_REQUEST_PATH, DECIMAL, IDENTITY_KEYS,
                              INLINE_ACCEPTANCE_REQUEST_LINE, INLINE_ACCEPTANCE_PROFILE,
                              MODES, PROFILE, RECEIPT_SCHEMA, SHA, attempt_marker, check_marker,
                              check_name, classify, classify_scaling, classify_throughput, host_problem, number,
                              range_label, regression_policy, render, SCALING_PROFILE, scaling_digest, validate_inline_acceptance,
                              THROUGHPUT_PROFILE, throughput_digest, validate_analyzer_bundle)

ARTIFACT_PREFIX = "buster-9700x-compiler-"
MEMBER_LIMIT = 8 * 1024 * 1024
ANALYZER_MEMBER_LIMIT = 32 * 1024 * 1024
ANALYZER_BUNDLE_LIMIT = ARTIFACT_LIMIT * 3 // 4
AUTHORIZE_JOBS = {"main": "Authorize the main commit comparison"}
REPORT_MARKDOWN_LIMIT = 36000


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


def decide(expected: dict, authorized: bool, compare_result: str, receipt: object, summary: object,
           policy_value: str, throughput: object = None, require_throughput: bool = True,
           extra_reasons: list[str] | None = None, *,
           expected_phase_schema: str | None = None,
           expected_profile: str | None = None, expected_preparation_policy: str | None = None,
           require_owned_phases: bool = False, require_owned_preflight: bool = False,
           expected_phase_driver_sha256: str | None = None,
           expected_measurement_revision: str | None = None) -> tuple[str, str, list[str]]:
    """(conclusion, title, reasons) for one attempt; never consults the verdict's direction.

    throughput is {"summary": ..., "metadata": ..., "scaling": {series: {"summary", "metadata"}}}
    from the evidence artifact. Scaling is checked whenever the receipt names
    its profile; only a pull request that asked for it has one.
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
    if not authorized:
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
                reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
                reasons.extend(validate_analyzer_bundle(receipt,
                                                        analyzer_bundle.get("summary") if isinstance(analyzer_bundle, dict) else None,
                                                        analyzer_bundle))
            else:
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
                    if receipt.get("throughput_profile") != THROUGHPUT_PROFILE:
                        reasons.append("receipt throughput profile is not the frozen corpus profile")
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
    if request is None:
        raise ValueError("Main original executor lacks its independently reverified request identity")
    route.update(executor_run=executor_run, executor_attempt=attempt,
                 request_run=request[1], request_attempt=request[2], request_head=request[3])
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
            not isinstance(receipt.get("inline_acceptance"), dict) or receipt["inline_acceptance"].get("requested") is not False:
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
        receipt, summary, throughput = main_owned_files(main_archive_files(payload), route, api=api)
        artifact = dict(artifact, verified_archive_sha256=digest)
        return receipt, summary, "", artifact, throughput
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, urllib.error.URLError) as error:
        return None, None, "Main-owned evidence refused: " + str(error), artifact, {}


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
        with zipfile.ZipFile(io.BytesIO(api.download(rows[0]["archive_download_url"]))) as archive:
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
                throughput = {"analyzer": {"summary": summary, "files": raw_files} if not problem else None}
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
                                      for index, name in enumerate(SCALING_PROFILE["series"])}}
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
    return receipt, summary, problem, artifact, throughput


def _github_file(api: Api, revision: str) -> tuple[bytes | None, str]:
    """Read the exact request file at a commit from GitHub's immutable contents API."""
    path = urllib.parse.quote(ANALYZER_REQUEST_PATH, safe="/")
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
    parent_analyzer_deltas = {line: [] for line in ANALYZER_REQUEST_LINES}
    parent_inline_counts = []
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
    lines = [
        f"### {check_name(shown.get('mode', 'main'))}: {conclusion}, {title}", "",
        (verdict.get("text") if isinstance(verdict.get("text"), str) else
         "No valid measurement: this commit was not benchmarked by this attempt.")
        + " Performance policy: report-only. This never blocks merging, and a successful measurement is not by "
        "itself a speedup or an equivalence.", "",
        "| | |", "| --- | --- |",
        f"| Candidate | `{identity.get('head', 'NA')}` (this commit) |",
        f"| Baseline | `{identity.get('base', 'NA')}` ({baseline}) |",
        "| Wall time B/A | %s, 95%% CI [%s, %s], %s |" % (
            number(verdict.get("ratio"), "%.4f"), number(verdict.get("ci_low"), "%.4f"),
            number(verdict.get("ci_high"), "%.4f"), verdict.get("outcome", "NA")),
        f"| Wall median A / B | {number(wall.get('a_median'), '%.6g')} {unit} / "
        f"{number(wall.get('b_median'), '%.6g')} {unit} |",
        f"| Complete pairs | {number(plan.get('complete_pairs'), '%d')} |",
        f"| Host (observed) | `{host.get('cpu_model', 'NA')}` |",
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
    return dict(row, verified_zip_sha256=digest, verified_zip_bytes=len(payload),
                verified_member_manifest_sha256=hashlib.sha256(manifest).hexdigest(),
                verified_member_count=len(files))


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
    files = sampling_archive(payload)
    return files, campaign_artifact_identity(payload, row, files, retain_archive_identity)


def sampling_json(files: dict[str, bytes], name: str, object_only: bool = True) -> object:
    """Read bounded JSON data, rejecting duplicate keys and non-finite values."""
    raw = files.get(name)
    if not isinstance(raw, bytes) or not 0 < len(raw) <= ANALYZER_MEMBER_LIMIT:
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


def sampling_reviewed(authority: dict) -> bool:
    """A historical native review is data authority, never a physical admission."""
    flag = authority.get("historical_review", False)
    if type(flag) is not bool:
        raise ValueError("historical sampling review flag is not boolean")
    if flag and (not isinstance(authority.get("admitted"), dict) or
                 authority["admitted"].get("sampling_historical_valid") != "true" or
                 "sampling_admitted" in authority["admitted"]):
        raise ValueError("historical sampling lacks its separate native review proof")
    return flag


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


def sampling_validate(api: Api, authority: dict, files: dict[str, bytes]) -> dict:
    from sampling_qualification_receipt import validate_packet
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
    for name, raw in authority["raw"].items():
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
        wanted[label] = hashlib.sha256(authority["raw"][name]).hexdigest()
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


def preparation_publish(environment: dict) -> int:
    api, authority = preparation_authority(environment)
    files, artifact = {}, {}
    result = {"schema": "buster-compiler-preparation-publication-v1", "packet_state": "incomplete",
              "qualification_state": "unqualified", "default_activated": False, "routine_profile_enabled": False,
              "evidence_class": "unqualified-preparation-research", "phase": "qualify", "packet": 0,
              "reservation_seconds": 5400, "whole_job_net_savings_assessed": False, "problems": []}
    try:
        files, artifact = preparation_read_artifact(api, authority)
        if environment.get("BQ_PREPARATION_RESULT") != "success":
            raise ValueError("preparation physical job is failed, cancelled, skipped or unavailable")
        result = preparation_validate(api, authority, files)
    except (OSError, ValueError, UnicodeError, TypeError, KeyError, IndexError, AttributeError, RecursionError,
            urllib.error.URLError, TimeoutError) as error:
        result["problems"].append(("evidence validation failed: " + type(error).__name__ + ": " + str(error))[:1000])
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




def utility_series_replay(files: dict[str, bytes], prefix: str, plan: dict, leg: str, binaries: dict, *,
                          expected_cpu_model: str = "AMD Ryzen 7 9700X 8-Core Processor",
                          expected_profile: str = "compiler-compare-v1", work_root: str | None = None) -> dict:
    """Replay the ordinary profile without turning explanatory warnings into gates."""
    import math
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
    loaded = []
    for index, record in enumerate(records):
        number, variant = index // 2 + 1, ("ab" if index // 2 % 2 == 0 else "ba")[index % 2]
        order = "AB" if number % 2 else "BA"
        if not isinstance(record, dict) or type(record.get("pair")) is not int or record["pair"] != number or \
                record.get("order") != order or record.get("variant") != variant or \
                type(record.get("exit")) is not int or record["exit"] != 0 or record.get("identical") is not True or \
                type(record.get("span_s")) not in (int, float) or not math.isfinite(record["span_s"]) or \
                not 0 < record["span_s"] <= 3600 or type(record.get("counters")) is not bool:
            raise ValueError("utility ordinary raw pair is failed, truncated, unordered or non-deterministic")
        if record["counters"] is not variant_meta[variant]["capabilities"]["perf_stat"]["usable"]:
            raise ValueError("utility raw timed counter status differs from its actual probe")
        for field in ("cpu_s", "maxrss_bytes", "harness_rss_bytes", "wrapper_rss_bytes"):
            value = record.get(field)
            if value is not None and (type(value) not in (int, float) or not math.isfinite(value) or value < 0):
                raise ValueError("utility unavailable resource observation is malformed")
        stem = prefix + "pairs/" + f"{number:04d}-{variant}"
        counter_raw, metrics_raw = files.get(stem + ".csv"), files.get(stem + ".ccmetrics")
        if record["counters"] and (not isinstance(counter_raw, bytes) or not counter_raw) or \
                variant_meta[variant]["collection"]["metrics_out"] and (not isinstance(metrics_raw, bytes) or not metrics_raw):
            raise ValueError("utility claimed counters/compiler metrics lack their raw files")
        rows = _lab.parse_stat((counter_raw or b"").decode("utf-8"))
        metrics = _lab.parse_cc_metrics((metrics_raw or b"").decode("utf-8"))
        wall_ns = metrics["header"].get("wall_ns")
        task = record.get("cpu_s") if record["counters"] is False else _lab.task_seconds(rows)
        loaded.append(dict(record, values=_lab.stat_values(rows), lines=_lab.stat_lines(rows), metrics=metrics,
                           cc_wall_s=_lab.ratio(wall_ns, 1e9) if type(wall_ns) in (int, float) else None, task_s=task))
    pairs = _lab.complete_pairs(loaded)
    metrics = {key: dict(_lab.compare_series([(pair["metrics_a"][key], pair["metrics_b"][key]) for pair in pairs],
                    unit, direction, 20261003, time_metric=unit == "s", floor=_lab.metric_floor(key, 0.5)), label=label)
               for key, unit, direction, label in _lab.COMPARE_METRICS}
    phase_records = [(_lab.measured_input(pair["a"]["metrics"]), _lab.measured_input(pair["b"]["metrics"])) for pair in pairs]
    phase_records = [(a, b) for a, b in phase_records if a and b]
    phases = None
    if phase_records:
        phases = {}
        for phase in _lab.PHASES + ("total",):
            a, b = (_lab.phase_ns_series([pair[side] for pair in phase_records], phase) for side in (0, 1))
            values = [(x / 1e6, y / 1e6) for x, y in zip(a, b)] if a and b else [(None, None)] * len(phase_records)
            phases[phase] = _lab.compare_series(values, "ms", "lower", 20261003, time_metric=True, floor=0.005)
    counter_states = [variant_meta[key]["capabilities"]["perf_stat"] for key in ("a", "b")]
    counters = {"perf_stat": all(row["usable"] for row in counter_states),
                "reason": "; ".join(sorted({str(row["reason"]) for row in counter_states}))}
    if summary.get("counters") != counters or summary.get("phase_metrics") != raw.get("phase_metrics"):
        raise ValueError("utility saved optional counter/phase availability contradicts raw observations")
    if summary.get("metrics") != metrics or summary.get("phases") != phases or \
            summary.get("checks") != _lab.compare_checks(pairs) or \
            summary.get("verdict") != _lab.compare_verdict(metrics, phases, 0.5):
        raise ValueError("utility ordinary inference contradicts independently retained raw pairs")
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
    for name, raw in authority["raw"].items():
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
        wanted[label] = hashlib.sha256(authority["raw"][name]).hexdigest()
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
        if recover:
            expected, authorize_result, compare_result, problems = recovered_identity(api, repository, run_id, attempt)
            head = expected["head"]
            authorized = authorize_result == "success" and not problems
        else:
            expected = {"mode": mode, "repository": repository, "ref": get("BQ_REF"), "pull": get("BQ_PULL"),
                        "pull_head": get("BQ_PULL_HEAD"), "base": get("BQ_BASE_COMMIT"),
                        "base_tree": get("BQ_BASE_TREE"), "head": head, "head_tree": get("BQ_HEAD_TREE"),
                        "trusted_revision": get("BQ_TRUSTED_REVISION"), "request_run_id": get("BQ_REQUEST_RUN_ID"),
                        "run_id": run_id, "run_attempt": attempt}
            if mode == "main":
                expected.update(first_parent=get("BQ_FIRST_PARENT"), range=get("BQ_RANGE"))
            compare_result, problems = get("BQ_COMPARE_RESULT"), []
            authorized = get("BQ_AUTHORIZE_RESULT") == "success" and get("BQ_AUTHORIZED_ATTEMPT") == attempt
        notes.extend(problems)
        receipt = summary = None
        artifact: dict = {}
        throughput: dict = {}
        problem = ""
        route = None
        if mode == "main" and authorized:
            try:
                route = main_route(api, repository, run_id, attempt)
                expected["trusted_revision"] = route["main_measurement_revision"]
            except (OSError, ValueError, TypeError, urllib.error.URLError) as error:
                authorized = False
                problem = "Main route refused: " + str(error)
                notes.append(problem)
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
        conclusion, title, reasons = decide(expected, authorized, compare_result, receipt, summary,
                                            get("BQ_REGRESSION_POLICY"), throughput, not recover,
                                            [problem] if problem else [],
                                            **({"expected_phase_schema": route["main_phase_schema"],
                                                "expected_profile": route["main_profile"],
                                                "expected_preparation_policy": route["main_preparation_policy"],
                                                "require_owned_phases": True, "require_owned_preflight": True,
                                                "expected_phase_driver_sha256": route["driver_sha256"],
                                                "expected_measurement_revision": route["main_measurement_revision"]}
                                               if route is not None and route["main_owned"] else {}))
        shown = dict(receipt) if isinstance(receipt, dict) else {"mode": mode, "identity": expected}
        # The authorized range, never the host's own account of it.
        shown.pop("coverage", None)
        if mode == "main" and expected.get("range"):
            shown["coverage"] = {"first_parent": expected.get("first_parent"), "range": expected.get("range")}
        shown["reasons"] = reasons
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
