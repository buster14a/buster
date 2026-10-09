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
from compiler_receipt import (ANALYZER_PROFILE, ANALYZER_PROFILE_BY_LINE, ANALYZER_REQUIRED_FILES,
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
                    reasons.append(f"{selected_profile['name']} cannot be combined with compiler throughput or scaling")
                inline = receipt.get("inline_acceptance")
                if isinstance(inline, dict) and inline.get("requested") is True:
                    reasons.append(f"{selected_profile['name']} cannot be combined with issue #48 self-host acceptance")
                reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
                reasons.extend(validate_analyzer_bundle(receipt,
                                                        analyzer_bundle.get("summary") if isinstance(analyzer_bundle, dict) else None,
                                                        analyzer_bundle))
            else:
                reasons.extend(classify(summary, receipt.get("binaries")))
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



# Disabled sampling research uses the existing trusted hosted write boundary.
# Archive bytes are decoded in memory; no member is materialized or executed.
SAMPLING_CHECK_NAME = "9700X compiler sampling research"
SAMPLING_ARTIFACT_PREFIX = "buster-9700x-sampling-"
SAMPLING_HOST_JOB = "Sampling qualification packet"


def sampling_archive(payload: bytes) -> dict[str, bytes]:
    """Bound both the ZIP directory and inflated files before consuming data."""
    import zlib
    try:
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
                not 0 < count <= 2048 or size == 0xffffffff or offset == 0xffffffff or \
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
                        kind not in ((0, stat.S_IFDIR) if entry.is_dir() else (0, stat.S_IFREG)):
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
            execution.get("head_sha") != environment.get("GITHUB_SHA"):
        raise ValueError("sampling executor workflow provenance is unavailable")
    request = api.request(f"/actions/runs/{request_id}")
    pulls = api.request(f"/commits/{head}/pulls?per_page=100")
    problems, unused_base = direct_authorize.verify(repository, int(request_id), head, request, pulls)
    if problems:
        raise ValueError("sampling request ownership failed: " + ", ".join(problems))
    if execution.get("display_title") != f"9700X request {request_id}.1 head {head}":
        raise ValueError("sampling executor is not linked to the exact request attempt")
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


def sampling_read_artifact(api: Api, authority: dict) -> tuple[dict[str, bytes], dict]:
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
    return sampling_archive(payload), row


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


def sampling_prior_acquisition(api: Api, authority: dict, context: dict) -> tuple[dict, dict]:
    """Re-read the previously authenticated acquisition, never current-copy authority."""
    history = authority["history"]
    rows = [row for row in history if row.get("phase") == "acquire" and row.get("packet") == "0"]
    if len(rows) != 1 or rows[0].get("campaign") != context["sha256"] or rows[0].get("freeze_revision") != context["revision"] or \
            rows[0].get("state") != "complete" or rows[0].get("request_run_attempt") != "1" or rows[0].get("executor_run_attempt") != "1":
        raise ValueError("sampling lacks its unique authenticated complete acquisition")
    previous = rows[0]
    request = api.request("/actions/runs/" + previous["request_run_id"])
    execution = api.request("/actions/runs/" + previous["executor_run_id"])
    if not isinstance(request, dict) or not isinstance(execution, dict) or \
            str(request.get("id")) != previous["request_run_id"] or request.get("run_attempt") != 1 or \
            str(execution.get("id")) != previous["executor_run_id"] or execution.get("run_attempt") != 1 or \
            execution.get("status") != "completed" or execution.get("conclusion") != "success" or \
            not SHA.fullmatch(str(request.get("head_sha", ""))) or execution.get("path") != BENCH_WORKFLOW or \
            execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or \
            not isinstance(execution.get("repository"), dict) or execution["repository"].get("full_name") != authority["repository"] or \
            execution.get("display_title") != f"9700X request {previous['request_run_id']}.1 head {request['head_sha']}":
        raise ValueError("sampling acquisition executor provenance is unavailable")
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
    sampling_job_accounting(sampling_host_job(api, old), sampling_integer(owner["physical_packet_wall_us"], True), 1800)
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
        execution = api.request("/actions/runs/" + run_id)
        request = api.request("/actions/runs/" + source["request_run_id"])
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


def preparation_read_artifact(api: Api, authority: dict) -> tuple[dict[str, bytes], dict]:
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
    return preparation_archive(api.download(api.prefix + f"/actions/artifacts/{row['id']}/zip", max_bytes=PREPARATION_ARCHIVE_LIMIT)), row


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
                                "metadata": sampling_json(files, corpus + "metadata.json")}
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
    """Assess complete measurements against the frozen practical-equivalence criteria."""
    failures = []
    for name, result in series.items():
        if name.endswith("-aa") and not 0.995 <= result["ci_low"] <= result["ci_high"] <= 1.005:
            failures.append(name + ": complete same-source A/A 95% interval lies outside [0.995,1.005]")
    for name, summary in aa_corpora.items():
        if summary.get("confirmed_regressions") != 0:
            failures.append(name + ": complete same-source corpus has confirmed regressions")
    if pointers["snapshot"]["total_us"] >= pointers["legacy"]["total_us"]:
        failures.append("complete snapshot preparation cost is not less than legacy cost")
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
    if set(claim) != set(wanted) | {"evidence"} or any(claim.get(key) != value for key, value in wanted.items()) or \
            not isinstance(claim.get("evidence"), str) or not claim["evidence"].startswith("/") or \
            claim["evidence"] == "/" or any(part in ("", ".", "..") for part in claim["evidence"][1:].split("/")):
        raise ValueError("preparation immutable first-claim identity is absent or contradicts admission")
    owner, publication, terminal = preparation_phase_proofs(authority, files, expected, host)
    job = preparation_job(api, authority)
    accounting = sampling_job_accounting(job, sampling_integer(publication["observed_wall_us"], True),
                                        5400, job_name=PREPARATION_HOST_JOB)
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
            "preparation_costs": pointers, "qualification_publication_us": None,
            "predeclared_controls": {"aa_families": 3, "aa_interval": [0.995, 1.005],
                                    "all_full_corpora_valid": True,
                                    "snapshot_cost_less_than_legacy": pointers["snapshot"]["total_us"] < pointers["legacy"]["total_us"]},
            "control_failures": controls, "authenticated_attempt_history": history, "problems": []}


def preparation_publish(environment: dict) -> int:
    api, authority = preparation_authority(environment)
    files, artifact = {}, {}
    result = {"schema": "buster-compiler-preparation-publication-v1", "packet_state": "incomplete",
              "qualification_state": "unqualified", "default_activated": False, "routine_profile_enabled": False,
              "evidence_class": "unqualified-preparation-research", "phase": "qualify", "packet": 0,
              "reservation_seconds": 5400, "problems": []}
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


def main() -> int:
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
