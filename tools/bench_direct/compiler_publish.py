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
from compiler_receipt import (validate_closure, ANALYZER_PROFILE, ANALYZER_REQUIRED_FILES, ANALYZER_REQUEST_LINE,
                              ANALYZER_REQUEST_PATH, DECIMAL, IDENTITY_KEYS,
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
           extra_reasons: list[str] | None = None) -> tuple[str, str, list[str]]:
    """(conclusion, title, reasons) for one attempt; never consults the verdict's direction.

    throughput is {"summary": ..., "metadata": ..., "scaling": {series: {"summary", "metadata"}}}
    from the evidence artifact. Scaling is checked whenever the receipt names
    its profile; only a pull request that asked for it has one.
    A receipt from before the corpus leg (#2761) has no throughput_profile;
    only publication-only recovery of such a past attempt passes
    require_throughput=False, and a receipt that names the profile is always
    checked against it.
    """
    reasons: list[str] = list(extra_reasons or [])
    conclusion, title = "failure", "Not benchmarked"
    policy, problem = regression_policy(policy_value)
    if not authorized:
        reasons.append("authorization refused or did not complete in this attempt; this candidate has a "
                       "benchmark coverage gap (see the authorize-compiler job)")
    elif not isinstance(receipt, dict):
        reasons.append("no readable receipt.json in this attempt's evidence artifact "
                       f"(compare job result: {compare_result or 'unknown'})")
    else:
        analyzer_profile = receipt.get("profile") == ANALYZER_PROFILE
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
                reasons.append("clang-analyze-full-v1 is valid only in pull mode")
            if receipt.get("analyzer_profile") != ANALYZER_PROFILE:
                reasons.append("receipt analyzer profile marker is not frozen")
        elif receipt.get("profile") != PROFILE:
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
                    reasons.append("clang-analyze-full-v1 cannot be combined with compiler throughput or scaling")
                inline = receipt.get("inline_acceptance")
                if isinstance(inline, dict) and inline.get("requested") is True:
                    reasons.append("clang-analyze-full-v1 cannot be combined with issue #48 self-host acceptance")
                reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
                reasons.extend(validate_analyzer_bundle(receipt,
                                                        analyzer_bundle.get("summary") if isinstance(analyzer_bundle, dict) else None,
                                                        analyzer_bundle))
            else:
                reasons.extend(classify(summary, receipt.get("binaries")))
                reasons.extend(validate_closure(receipt, throughput.get("closure") if isinstance(throughput, dict) else None))
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


def read_evidence(api: Api, run_id: str, name: str) -> tuple[object, object, str, dict, dict]:
    """(receipt, summary, problem, artifact row, throughput) from this run's one evidence artifact."""
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
            if not problem and isinstance(receipt, dict) and receipt.get("profile") == ANALYZER_PROFILE:
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
                for operation in ("snapshot", "restore"):
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
    analyzer_profile = receipt.get("profile") == ANALYZER_PROFILE
    if not analyzer_profile and receipt.get("profile") != PROFILE:
        return "pull receipt does not name a recognized compiler or analyzer profile"
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
    head_count = head_lines.count(ANALYZER_REQUEST_LINE)
    head_inline_count = head_lines.count(INLINE_ACCEPTANCE_REQUEST_LINE)
    parent_analyzer_deltas = []
    parent_inline_counts = []
    for parent in parent_shas:
        parent_bytes, problem = _github_file(api, parent)
        if problem:
            return problem
        try:
            parent_lines = parent_bytes.decode("utf-8").splitlines()
        except UnicodeDecodeError:
            return f"parent pull request profile file is not UTF-8 at {parent}"
        parent_analyzer_deltas.append(head_count - parent_lines.count(ANALYZER_REQUEST_LINE))
        parent_inline_counts.append(parent_lines.count(INLINE_ACCEPTANCE_REQUEST_LINE))
    if analyzer_profile:
        if not parent_analyzer_deltas or not all(delta == 1 for delta in parent_analyzer_deltas):
            return ("the exact analyzer selector was not freshly added once at this head relative to every parent; "
                    "append one selector line for each explicit full-inventory request")
        analyzer = throughput.get("analyzer") if isinstance(throughput, dict) else None
        files = analyzer.get("files") if isinstance(analyzer, dict) else None
        if not isinstance(files, dict) or not isinstance(files.get("request.txt"), bytes):
            return "analyzer request bytes are missing from retained evidence"
        if head_bytes != files["request.txt"]:
            return "retained analyzer request bytes do not match the authorized GitHub head"
        if any(head_inline_count > count for count in parent_inline_counts):
            return "clang-analyze-full-v1 cannot be combined with a newly requested inline acceptance profile"
        if receipt.get("analyzer_request_sha256") != hashlib.sha256(head_bytes).hexdigest():
            return "receipt analyzer request digest does not match the authorized GitHub head"
    elif any(delta > 0 for delta in parent_analyzer_deltas):
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
    if shown.get("profile") == ANALYZER_PROFILE:
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


def bind_request(api: Api, expected: dict, receipt: object) -> list[str]:
    """Fill expected from the receipt's request run, re-verified exactly as authorize-compiler does."""
    identity = receipt.get("identity") if isinstance(receipt, dict) and isinstance(receipt.get("identity"), dict) else {}
    request_run, head, repository = identity.get("request_run_id", ""), expected["head"], expected["repository"]
    # The baseline the attempt measured: it must still be on head's first-parent chain.
    base = identity.get("base", "")
    problems = []
    if not (isinstance(request_run, str) and DECIMAL.fullmatch(request_run) and SHA.fullmatch(head)):
        problems.append("the receipt names no request run")
    else:
        commit = api.request(f"/commits/{head}")
        failures, result = verify_main(
            repository, int(request_run), head, api.request(f"/actions/runs/{request_run}"), commit,
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


def main() -> int:
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
        if authorized:
            receipt, summary, problem, artifact, throughput = read_evidence(api, run_id, artifact_name(head, attempt))
            if problem:
                notes.append(problem)
            if not recover and not problem and isinstance(receipt, dict) and expected.get("mode") == "pull":
                problem = verify_analyzer_request_freshness(api, expected, receipt, throughput)
                if problem:
                    notes.append(problem)
            if recover:
                problems = bind_request(api, expected, receipt)
                notes.extend(problems)
                authorized = not problems
        conclusion, title, reasons = decide(expected, authorized, compare_result, receipt, summary,
                                            get("BQ_REGRESSION_POLICY"), throughput, not recover,
                                            [problem] if problem else [])
        shown = dict(receipt) if isinstance(receipt, dict) else {"mode": mode, "identity": expected}
        # The authorized range, never the host's own account of it.
        shown.pop("coverage", None)
        if mode == "main" and expected.get("range"):
            shown["coverage"] = {"first_parent": expected.get("first_parent"), "range": expected.get("range")}
        shown["reasons"] = reasons
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
