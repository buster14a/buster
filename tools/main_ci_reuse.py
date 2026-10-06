#!/usr/bin/env python3
"""Read-only queue-to-main CI reuse for issue #1808.

Native, mobile, UEFI, desktop and analyzer validation are reusable. Desktop
jobs retain main's Zig cache lifecycle, analyzer retains a truthful receipt,
and lint still checks the main history. This module
never creates a check or changes a ref. A missing proof schedules the normal
jobs, while a proof that changes after jobs were skipped fails the aggregate.

source_run binds finalization to the decision receipt and checks discovery for
competing runs. Only inconclusive discovery reads retry; changed evidence never
does. cli retains a diagnostic result even when verification fails (#2134).
Admission metadata is independently proved and retained, never reused as work (#2388).
"""

import argparse
from collections import Counter
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.error

import github_ci_time
from merge_queue_admission import AdmissionError, GitHub, require
from native_retirement_integration import APIReadError

REPOSITORY = "buster14a/buster"
REPOSITORY_ID = 1071732997
WORKFLOW_ID = 197051687
WORKFLOW_PATH = ".github/workflows/ci.yml"
POLICY = "buster-main-ci-reuse-v2"
MAX_AGE = timedelta(hours=2)
RESULT_SCHEMA = "buster-main-ci-reuse-result-v1"
MAX_RECEIPT_BYTES = 32768
DISCOVERY_DELAYS = (1, 2)
DISCOVERY_PATH = f"actions/workflows/{WORKFLOW_ID}/runs"
# Exact GitHub-normalized spelling observed for the two unexpanded matrices
# in run 36835320572. Never accept an arbitrary expression-looking job name.
UNEXPANDED_REUSE_NAME = (
    "${{ matrix.name }}${{ (((github.event_name == 'pull_request') && "
    "github.event.pull_request.draft && (github.run_attempt == '1') && "
    "startsWith(matrix.runner, 'macos-') && ' (deferred for draft PR)') || '') }}")
SHA = re.compile(r"[0-9a-f]{40}\Z")
ARTIFACT_DIGEST = re.compile(r"sha256:[0-9a-f]{64}\Z")
# Job name, artifact prefix, and mandatory coverage step. The artifact contains
# the resolved toolchain/runner provenance and the source-run diagnostics.
REUSED = (
    ("Linux x86-64 native", "native-linux-x86_64", "Execution-mode matrix"),
    ("Linux AArch64 native", "native-linux-aarch64", "Execution-mode matrix"),
    ("macOS AArch64 native", "native-macos-aarch64", "Execution-mode matrix"),
    ("Windows x86-64 native", "native-windows-x86_64", "Execution-mode matrix (Windows)"),
    ("Windows AArch64 native", "native-windows-aarch64", "Execution-mode matrix (Windows)"),
    ("Android x86-64", "mobile-android-x86_64", "Test (Android)"),
    ("iOS AArch64", "mobile-ios-aarch64", "Test (iOS simulator)"),
    ("UEFI firmware boot", "uefi-boot", "Build compiler and boot both architectures in all allocators"),
)
REUSED_NAMES = frozenset(row[0] for row in REUSED)
# Desktop jobs still execute cache publication on main, under their existing
# names and authority. Only their already-proven validation is reused.
DESKTOP = tuple((f"{name} {shard}", f"desktop-{os_name}-{arch}-{shard}",
                 "Combination matrix (Windows)" if os_name == "windows"
                 else "Combination matrix (Linux, macOS)")
                for name, os_name, arch in (
                    ("Linux x86-64", "linux", "x86_64"),
                    ("Linux AArch64", "linux", "aarch64"),
                    ("macOS AArch64", "macos", "aarch64"),
                    ("Windows x86-64", "windows", "x86_64"),
                    ("Windows AArch64", "windows", "aarch64"))
                for shard in (("release",) + github_ci_time.SPLIT_CHECK_SHARDS
                              if name in github_ci_time.SPLIT_CHECK_PLATFORMS
                              else github_ci_time.COMBINATION_SHARDS))
DESKTOP_NAMES = frozenset(row[0] for row in DESKTOP)
ANALYZER_STEPS = ("Bootstrap and identify candidate build driver",
                  "Exercise analyzer failure and coverage controls",
                  "Configure the authoritative split-source database",
                  "Analyze candidate and aggregate all module shards")
ANALYZER_RECEIPT_STEPS = ("Report reused analyzer validation",
                         "Retain analyzer inventory, results and measurements")
SOURCE_COVERAGE = REUSED + DESKTOP + (("Clang analyzer shards", "clang-analyzer", ANALYZER_STEPS[-1]),)
CACHE_STEPS = ("Checkout", "Resolve exact Zig cache policy", "Restore Zig archive",
               "Validate exact Zig cache restore", "Install verified Zig",
               "Retain exact Zig cache evidence", "Report reused desktop validation",
               "Sanitize desktop logs", "Retain desktop logs")
VALIDATION_STEPS = ("Workflow tool regression tests", "Bootstrap wrapper regression tests",
                    "Install mold", "Install latest stable LLVM", "Application compilers",
                    "Combination matrix (Linux, macOS)", "Combination matrix (Windows)",
                    "Collect CMake configure evidence", "Desktop result and reproduction")
RETAINED_NAMES = tuple(name for name in github_ci_time.combination_jobs()
                       if name not in REUSED_NAMES)


def instant(value, label):
    require(isinstance(value, str), label + " has no timestamp")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise AdmissionError(label + " has an invalid timestamp") from error
    require(parsed.tzinfo is not None, label + " has no timezone")
    return parsed


def exact_run(run, *, run_id, sha, event, branch):
    require(isinstance(run, dict) and run.get("id") == run_id and type(run_id) is int and run_id > 0,
            "run ID mismatch")
    require(run.get("workflow_id") == WORKFLOW_ID and
            run.get("path", "").split("@", 1)[0] == WORKFLOW_PATH,
            "workflow identity mismatch")
    require(run.get("repository", {}).get("full_name") == REPOSITORY and
            run.get("repository", {}).get("id") == REPOSITORY_ID and
            run.get("head_repository", {}).get("id") == REPOSITORY_ID,
            "repository identity mismatch")
    require(run.get("head_sha") == sha and run.get("head_commit", {}).get("id") == sha,
            "run commit mismatch")
    require(run.get("event") == event and run.get("head_branch") == branch,
            "run event or branch mismatch")
    # An explicit rerun is a fresh execution request. Refuse partial rerun
    # reconstruction and a superseding incomplete or failed attempt.
    if type(run.get("run_attempt")) is not int or run["run_attempt"] != 1:
        raise ReuseError("changed-attempt", "rerun requires fresh main validation")


class ReuseError(AdmissionError):
    """A classified refusal, never an authorization to reuse coverage."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def failure_record(error):
    # Do not persist transport bodies/headers or an exception's token-bearing
    # request. Only a bounded, redacted message and numeric HTTP status survive.
    code = "invalid-evidence" if isinstance(error, AdmissionError) else "internal-error"
    if isinstance(error, ReuseError):
        code = error.code
    elif isinstance(error, urllib.error.HTTPError) or (isinstance(error, APIReadError) and
                                                      type(error.status) is int):
        code = "http-error"
    elif isinstance(error, (urllib.error.URLError, TimeoutError, ConnectionError, OSError)):
        code = "transport-or-io-error"
    elif isinstance(error, (ValueError, TypeError)):
        code = "malformed-evidence"
    message = str(error)
    token = os.environ.get("GH_TOKEN", "")
    if token:
        message = message.replace(token, "[redacted]")
    # JSON escapes newlines; also keep printed diagnostics on one physical line.
    message = " ".join(message.split())[:1024]
    result = {"code": code, "type": type(error).__name__, "message": message}
    if isinstance(error, urllib.error.HTTPError):
        result["http_status"] = error.code
    elif isinstance(error, APIReadError) and type(error.status) is int:
        result["http_status"] = error.status
    return result


def read_source(api, run_id, diagnostics):
    diagnostics["source_run"] = {"requested_run_id": run_id, "read_status": "unavailable"}
    source = api.get("actions/runs/" + str(run_id))
    if isinstance(source, dict):
        record = {key: source.get(key) if type(source.get(key)) is int else None
                  for key in ("id", "run_attempt", "workflow_id")}
        record["head_sha"] = (source.get("head_sha") if
                              isinstance(source.get("head_sha"), str) and
                              SHA.fullmatch(source["head_sha"]) else None)
        for key in ("status", "conclusion"):
            value = source.get(key)
            record[key] = value if value in (None, "completed", "in_progress", "queued", "waiting",
                                             "success", "failure", "cancelled", "skipped", "neutral", "timed_out") else "invalid"
        record["read_status"] = "received"
        diagnostics["source_run"] = record
    else:
        diagnostics["source_run"] = {"malformed": True}
    return source


def check_source(source, sha, run_id, branch=None, completed=None):
    observed_branch = source.get("head_branch", "") if isinstance(source, dict) else ""
    require(isinstance(observed_branch, str) and
            observed_branch.startswith("gh-readonly-queue/main/"),
            "source is not a main merge-queue ref")
    exact_run(source, run_id=run_id, sha=sha, event="merge_group",
              branch=observed_branch if branch is None else branch)
    require(source.get("status") == "completed" and source.get("conclusion") == "success",
            "queue workflow has not succeeded")
    if completed is not None and source.get("updated_at") != completed:
        raise ReuseError("source-changed", "queue completion changed after the decision")


def discovery_retryable(error):
    # No retry of duplicate runs, failed jobs/artifacts, changed identities or
    # attempts, permission errors, malformed pages, or exhausted page bounds.
    if isinstance(error, ReuseError):
        retry = error.code in ("missing-source", "moving-list")
    elif isinstance(error, APIReadError):
        # GitHub.get wraps exhausted GET errors from the shared reader. Only
        # transport, throttling and server failures are inconclusive evidence.
        retry = error.status is None or (type(error.status) is int and
                                         (error.status == 429 or 500 <= error.status < 600))
    elif isinstance(error, urllib.error.HTTPError):
        retry = error.code == 429 or 500 <= error.code < 600
    else:
        retry = isinstance(error, (urllib.error.URLError, TimeoutError, ConnectionError))
    return retry


def source_run(api, sha, *, expected=None, diagnostics=None):
    diagnostics = {} if diagnostics is None else diagnostics
    snapshots = diagnostics.setdefault("discovery", [])
    source = None
    if expected is not None:
        diagnostics["stage"] = "bound-source"
        source = read_source(api, expected["source_run_id"], diagnostics)
        check_source(source, sha, expected["source_run_id"], expected["source_branch"],
                     expected["source_completed_at"])
    # Recollect at most three complete discovery snapshots in either phase.
    # Per-GET retries have their own budget; these delays are not a wall-time
    # bound. No pages, jobs or artifacts are borrowed from failed snapshots.
    delays = DISCOVERY_DELAYS
    for attempt in range(len(delays) + 1):
        diagnostics["stage"] = "source-discovery"
        snapshot = {"read": attempt + 1, "path": DISCOVERY_PATH,
                    "event": "merge_group", "head_sha": sha}
        snapshots.append(snapshot)
        try:
            try:
                rows = api.pages(DISCOVERY_PATH, "workflow_runs", event="merge_group", head_sha=sha)
            except AdmissionError as error:
                # Preserve the shared reader's completeness contract. A moving
                # total can be recollected; malformed or unbounded data cannot.
                if str(error) == "API response is truncated or changed during pagination":
                    raise ReuseError("moving-list", str(error)) from error
                raise ReuseError("incomplete-list", str(error)) from error
            if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
                raise ReuseError("malformed-list", "workflow listing contains malformed rows")
            snapshot["candidate_count"] = len(rows)
            snapshot["candidates"] = [
                {key: row.get(key) if type(row.get(key)) is int else None
                 for key in ("id", "run_attempt", "workflow_id")} for row in rows[:10]]
            if not rows:
                raise ReuseError("missing-source", "exact queue workflow is absent from discovery")
            if len(rows) != 1:
                raise ReuseError("ambiguous-source", "multiple exact queue workflow records")
            selected = rows[0]
            if expected is not None and selected.get("id") != expected["source_run_id"]:
                raise ReuseError("source-replaced", "discovery does not identify the bound source run")
            # A scoped endpoint must not smuggle an unrelated record into proof.
            check_source(selected, sha, selected.get("id"))
        except (AdmissionError, OSError, ValueError, TypeError) as error:
            snapshot["error"] = failure_record(error)
            if attempt == len(delays) or not discovery_retryable(error):
                raise
            time.sleep(delays[attempt])
            # A source rerun/failure during backoff must stop immediately, even
            # when discovery is still inconsistent on the following read.
            if expected is not None:
                diagnostics["stage"] = "bound-source"
                source = read_source(api, expected["source_run_id"], diagnostics)
                check_source(source, sha, expected["source_run_id"], expected["source_branch"],
                             expected["source_completed_at"])
        else:
            break
    diagnostics["stage"] = "source-run"
    source = read_source(api, selected["id"], diagnostics)
    check_source(source, sha, selected["id"], selected["head_branch"])
    require(source.get("run_attempt") == selected.get("run_attempt") and
            source.get("updated_at") == selected.get("updated_at"),
            "queue run changed during discovery")
    if expected is not None:
        check_source(source, sha, expected["source_run_id"], expected["source_branch"],
                     expected["source_completed_at"])
    return source


def reconciled_check_inventory(api, sha):
    """Checks-only complete snapshot; verify stable bounded totals on every page."""
    rows = []
    total = None
    page = 1
    while total is None or len(rows) < total:
        require(page <= 10, "reconciler check pagination limit reached")
        batch = api.get(f"commits/{sha}/check-runs", filter="all", per_page=100, page=page)
        require(isinstance(batch, dict), "malformed reconciler check page")
        count = batch.get("total_count")
        require(type(count) is int and 0 <= count <= 1000 and
                (total is None or count == total),
                "missing, changing or excessive reconciler check inventory")
        total = count
        chunk = batch.get("check_runs")
        require(isinstance(chunk, list) and len(chunk) <= 100 and
                len(rows) + len(chunk) <= total and (bool(chunk) or len(rows) == total),
                "incomplete reconciler check pagination")
        rows.extend(chunk)
        require(len(chunk) == 100 or len(rows) == total, "partial reconciler check page")
        page += 1
    return rows


def successful_source_jobs(api, source, sha, *, diagnostics=None):
    diagnostics = {} if diagnostics is None else diagnostics
    run_id = source["id"]
    jobs = api.pages(f"actions/runs/{run_id}/attempts/1/jobs", "jobs")
    try:
        checks = (reconciled_check_inventory(api, sha)
                  if github_ci_time.reconciled_job_candidates(jobs) else [])
        jobs, diagnostics["source_reconciled_checks"] = github_ci_time.separate_reconciled_jobs(
            jobs, run_id, 1, sha, checks)
    except ValueError as error:
        raise AdmissionError(str(error)) from error
    jobs, extras = github_ci_time.separate_reuse_job(jobs, run_id, 1, sha, event='merge_group')
    require(not extras, "; ".join(extras))
    errors = github_ci_time.validate_required_jobs(jobs, run_id, 1, sha,
                                                   expected_names=github_ci_time.combination_jobs(),
                                                   complete_active=False)
    require(not errors, "; ".join(errors))
    identifiers = [job.get("id") for job in jobs]
    require(all(type(identifier) is int and identifier > 0 for identifier in identifiers) and
            len(set(identifiers)) == len(identifiers), "source job IDs are missing or duplicated")
    by_name = {job["name"]: job for job in jobs}
    for name, _, step in SOURCE_COVERAGE:
        mandatory = {step}
        if name in github_ci_time.ANALYZER:
            mandatory.update(ANALYZER_STEPS)
        if name in github_ci_time.NATIVE:
            if name.startswith("Windows"):
                mandatory.add("Native MSVC reference differential")
            else:
                mandatory.add("Native configuration differential matrix")
        for required_step in mandatory:
            matches = [row for row in by_name[name].get("steps", [])
                       if row.get("name") == required_step]
            require(len(matches) == 1 and matches[0].get("status") == "completed" and
                    matches[0].get("conclusion") == "success",
                    name + ": required coverage step is missing or unsuccessful: " + required_step)
    return [{"name": name, "job_id": by_name[name]["id"]} for name, _, _ in SOURCE_COVERAGE]


def retained_artifacts(api, source, now):
    run_id = source["id"]
    rows = api.pages(f"actions/runs/{run_id}/artifacts", "artifacts")
    result = []
    for _, prefix, _ in SOURCE_COVERAGE:
        name = f"{prefix}-{run_id}-1"
        matches = [row for row in rows if row.get("name") == name]
        require(len(matches) == 1, "missing or ambiguous queue artifact: " + name)
        artifact = matches[0]
        origin = artifact.get("workflow_run", {})
        require(type(artifact.get("id")) is int and artifact["id"] > 0 and
                type(artifact.get("size_in_bytes")) is int and artifact["size_in_bytes"] > 0 and
                artifact.get("expired") is False and
                ARTIFACT_DIGEST.fullmatch(artifact.get("digest", "")) is not None and
                instant(artifact.get("expires_at"), name) > now and
                origin.get("id") == run_id and origin.get("repository_id") == REPOSITORY_ID and
                origin.get("head_repository_id") == REPOSITORY_ID and
                origin.get("head_sha") == source["head_sha"] and
                origin.get("head_branch") == source["head_branch"],
                "queue artifact expired, empty or misbound: " + name)
        result.append({"name": name, "id": artifact["id"], "digest": artifact["digest"]})
    return result


def receipt_digest(receipt):
    payload = json.dumps(receipt, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    return hashlib.sha256(payload).hexdigest()


def verify_source(api, sha, current_run_id, workflow_blob, now, *, expected=None, diagnostics=None):
    diagnostics = {} if diagnostics is None else diagnostics
    diagnostics["stage"] = "main-run"
    current = api.get("actions/runs/" + str(current_run_id))
    exact_run(current, run_id=current_run_id, sha=sha, event="push", branch="main")
    require(current.get("run_attempt") == 1, "main run was rerun")
    source = source_run(api, sha, expected=expected, diagnostics=diagnostics)
    diagnostics["stage"] = "source-freshness"
    completed = instant(source.get("updated_at"), "source completion")
    started = instant(current.get("created_at"), "main creation")
    require(completed <= started and started - completed <= MAX_AGE,
            "queue result is not a fresh predecessor of this main push")
    require(completed <= now, "source completion is in the future")
    diagnostics["stage"] = "workflow-identity"
    blob = api.get("contents/" + WORKFLOW_PATH, ref=sha)
    require(blob.get("type") == "file" and blob.get("sha") == workflow_blob,
            "workflow revision differs from exact checkout")
    diagnostics["stage"] = "source-jobs"
    jobs = successful_source_jobs(api, source, sha, diagnostics=diagnostics)
    diagnostics["stage"] = "source-artifacts"
    artifacts = retained_artifacts(api, source, now)
    diagnostics["stage"] = "source-recheck"
    # Re-read both runs after paged jobs and artifacts: an attempt change or
    # interrupted source cannot authorize a skip during collection.
    final_source = read_source(api, source["id"], diagnostics)
    final_current = api.get("actions/runs/" + str(current_run_id))
    check_source(final_source, sha, source["id"], source["head_branch"], source["updated_at"])
    exact_run(final_current, run_id=current_run_id, sha=sha, event="push", branch="main")
    require(final_source.get("run_attempt") == 1 and
            final_source.get("status") == "completed" and
            final_source.get("conclusion") == "success" and
            final_source.get("updated_at") == source["updated_at"] and
            final_current.get("run_attempt") == 1 and
            final_current.get("head_sha") == sha,
            "run or attempt moved during evidence collection")
    return {"policy": POLICY, "repository": REPOSITORY, "head_sha": sha,
            "main_run_id": current_run_id, "main_attempt": 1,
            "source_branch": source["head_branch"],
            "workflow_id": WORKFLOW_ID, "workflow_path": WORKFLOW_PATH,
            "workflow_blob": workflow_blob, "source_run_id": source["id"],
            "source_attempt": 1, "source_completed_at": source["updated_at"],
            "source_jobs": jobs, "source_artifacts": artifacts}


def separate_skipped_jobs(rows, sha, run_id, diagnostics):
    """Account for skipped groups before the name-based executed-job reader.

    GitHub emits two same-name, unexpanded matrix placeholders when job-level
    if prevents expansion. They are not two executions of one logical job.
    The workflow separately requires native/mobile/UEFI needs results skipped.
    """
    require(isinstance(rows, list) and all(isinstance(row, dict) for row in rows),
            "malformed current job inventory")
    identifiers = [row.get("id") for row in rows]
    require(all(type(value) is int and value > 0 for value in identifiers) and
            len(set(identifiers)) == len(identifiers), "current job IDs are missing or duplicated")
    skipped = [row for row in rows if row.get("name") in REUSED_NAMES or
               row.get("name") == UNEXPANDED_REUSE_NAME]
    names = Counter(row["name"] for row in skipped)
    require(names in (Counter(REUSED_NAMES), Counter({UNEXPANDED_REUSE_NAME: 2, "UEFI firmware boot": 1})),
            "reused main job groups are missing, duplicated or mixed")
    for row in skipped:
        require(row.get("run_id") == run_id and row.get("head_sha") == sha and
                type(row.get("run_attempt")) is int and row["run_attempt"] == 1 and
                row.get("status") == "completed" and row.get("conclusion") == "skipped" and
                row.get("steps") == [] and row.get("runner_id") in (None, 0),
                "reusable main job executed or has invalid skipped identity")
    diagnostics["skipped_jobs"] = [{"name": row["name"], "job_id": row["id"], "conclusion": "skipped"}
                                   for row in skipped]
    return [row for row in rows if row.get("name") not in REUSED_NAMES and
            row.get("name") != UNEXPANDED_REUSE_NAME]


def verify_current_jobs(api, sha, run_id, *, diagnostics=None):
    diagnostics = {} if diagnostics is None else diagnostics
    current = api.get("actions/runs/" + str(run_id))
    exact_run(current, run_id=run_id, sha=sha, event="push", branch="main")
    rows = api.pages(f"actions/runs/{run_id}/jobs", "jobs", filter="all")
    try:
        checks = (reconciled_check_inventory(api, sha)
                  if github_ci_time.reconciled_job_candidates(rows) else [])
        rows, diagnostics["current_reconciled_checks"] = github_ci_time.separate_reconciled_jobs(
            rows, run_id, 1, sha, checks)
    except ValueError as error:
        raise AdmissionError(str(error)) from error
    rows = separate_skipped_jobs(rows, sha, run_id, diagnostics)
    jobs = github_ci_time.latest_run_jobs(rows, run_id, 1, sha)
    jobs, extras = github_ci_time.separate_reuse_job(jobs, run_id, 1, sha, required=True, event='push')
    require(not extras, "; ".join(extras))
    desktop = [job for job in jobs if job.get("name") in DESKTOP_NAMES]
    require({job["name"] for job in desktop} == DESKTOP_NAMES and len(desktop) == len(DESKTOP),
            "main cache job identities are missing or duplicated")
    for job in desktop:
        require(job.get("run_id") == run_id and job.get("head_sha") == sha and
                job.get("run_attempt") == 1 and job.get("status") == "completed" and
                job.get("conclusion") == "success", "main cache job failed or is misbound")
        for step_name in CACHE_STEPS + VALIDATION_STEPS:
            matches = [step for step in job.get("steps", []) if step.get("name") == step_name]
            expected = "success" if step_name in CACHE_STEPS else "skipped"
            require(len(matches) == 1 and matches[0].get("status") == "completed" and
                    matches[0].get("conclusion") == expected,
                    job["name"] + ": cache-only step proof missing: " + step_name)
    analyzer = [job for job in jobs if job.get("name") in github_ci_time.ANALYZER]
    require(len(analyzer) == 1, "main analyzer receipt job is missing or duplicated")
    for job in analyzer:
        require(job.get("run_id") == run_id and job.get("head_sha") == sha and
                job.get("run_attempt") == 1 and job.get("status") == "completed" and
                job.get("conclusion") == "success", "main analyzer receipt failed or is misbound")
        for step_name in ANALYZER_STEPS + ANALYZER_RECEIPT_STEPS:
            matches = [step for step in job.get("steps", []) if step.get("name") == step_name]
            expected = "success" if step_name in ANALYZER_RECEIPT_STEPS else "skipped"
            require(len(matches) == 1 and matches[0].get("status") == "completed" and
                    matches[0].get("conclusion") == expected,
                    "main analyzer receipt step proof missing: " + step_name)
    validation = [job for job in jobs if job.get("name") not in DESKTOP_NAMES and
                  job.get("name") not in github_ci_time.ANALYZER]
    errors = github_ci_time.validate_required_jobs(validation, run_id, 1, sha,
                       expected_names=tuple(name for name in RETAINED_NAMES if name not in DESKTOP_NAMES and
                                            name not in github_ci_time.ANALYZER))
    require(not errors, "; ".join(errors))
    identifiers = [job.get("id") for job in jobs]
    require(all(type(identifier) is int and identifier > 0 for identifier in identifiers) and
            len(set(identifiers)) == len(identifiers), "current job IDs are missing or duplicated")
    require(api.get("actions/runs/" + str(run_id)).get("run_attempt") == 1,
            "main attempt moved during job collection")
    return [{"name": job["name"], "job_id": job["id"]} for job in jobs]


def local_workflow_blob(sha):
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    require(head == sha, "checkout does not match the main run")
    return subprocess.check_output(["git", "hash-object", WORKFLOW_PATH], text=True).strip()


def parse_expected_receipt(text, digest, sha, run_id, workflow_blob):
    require(isinstance(text, str) and 0 < len(text.encode("utf-8")) <= MAX_RECEIPT_BYTES,
            "decision receipt is missing or exceeds the handoff bound")
    require(re.fullmatch(r"[0-9a-f]{64}", digest) is not None, "invalid decision receipt digest")
    receipt = json.loads(text)
    require(isinstance(receipt, dict) and receipt_digest(receipt) == digest,
            "decision receipt digest mismatch")
    require(receipt.get("policy") == POLICY and receipt.get("repository") == REPOSITORY and
            receipt.get("head_sha") == sha and receipt.get("workflow_id") == WORKFLOW_ID and
            receipt.get("workflow_path") == WORKFLOW_PATH and receipt.get("workflow_blob") == workflow_blob,
            "decision receipt identity mismatch")
    require(type(receipt.get("main_run_id")) is int and receipt["main_run_id"] == run_id and
            type(receipt.get("main_attempt")) is int and receipt["main_attempt"] == 1,
            "decision receipt belongs to another main run or attempt")
    require(type(receipt.get("source_run_id")) is int and receipt["source_run_id"] > 0 and
            type(receipt.get("source_attempt")) is int and receipt["source_attempt"] == 1,
            "invalid decision source identity")
    branch = receipt.get("source_branch")
    require(isinstance(branch, str) and branch.startswith("gh-readonly-queue/main/"),
            "invalid decision source branch")
    instant(receipt.get("source_completed_at"), "decision source completion")
    return receipt


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("decide", "finish"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-digest", default="")
    parser.add_argument("--expected-receipt", default="")
    arguments = parser.parse_args()
    diagnostics = {"stage": "environment", "discovery": []}
    result = {"schema": RESULT_SCHEMA, "phase": arguments.phase,
              "status": "unavailable" if arguments.phase == "decide" else "failed",
              "diagnostics": diagnostics}
    status = 0 if arguments.phase == "decide" else 1
    receipt = None
    expected = None
    try:
        require(os.environ.get("GITHUB_REPOSITORY") == REPOSITORY and
                os.environ.get("GITHUB_EVENT_NAME") == "push" and
                os.environ.get("GITHUB_REF") == "refs/heads/main", "not a main push")
        sha = os.environ.get("GITHUB_SHA", "")
        require(SHA.fullmatch(sha) is not None, "invalid main commit")
        run_id = int(os.environ.get("GITHUB_RUN_ID", "0"))
        require(run_id > 0, "invalid main run ID")
        require(os.environ.get("GITHUB_RUN_ATTEMPT") == "1", "explicit rerun requires full CI")
        result.update(repository=REPOSITORY, head_sha=sha, main_run_id=run_id, main_attempt=1)
        diagnostics["stage"] = "checkout"
        workflow_blob = local_workflow_blob(sha)
        if arguments.phase == "finish":
            diagnostics["stage"] = "receipt-handoff"
            result["expected_digest"] = (arguments.expected_digest if
                                         re.fullmatch(r"[0-9a-f]{64}", arguments.expected_digest) else None)
            expected = parse_expected_receipt(arguments.expected_receipt, arguments.expected_digest,
                                               sha, run_id, workflow_blob)
            result["expected_source_run_id"] = expected["source_run_id"]
            result["expected_source_attempt"] = expected["source_attempt"]
        diagnostics["stage"] = "api-client"
        api = GitHub(REPOSITORY, os.environ.get("GH_TOKEN", ""))
        receipt = verify_source(api, sha, run_id, workflow_blob,
                                datetime.now(timezone.utc), expected=expected, diagnostics=diagnostics)
        handoff = json.dumps(receipt, sort_keys=True, separators=(",", ":"), allow_nan=False)
        require(len(handoff.encode("utf-8")) <= MAX_RECEIPT_BYTES, "decision receipt exceeds handoff bound")
        if arguments.phase == "finish":
            diagnostics["stage"] = "receipt-comparison"
            require(receipt_digest(receipt) == arguments.expected_digest,
                    "queue evidence changed after main jobs were skipped")
            diagnostics["stage"] = "main-jobs"
            receipt["main_jobs"] = verify_current_jobs(api, sha, run_id, diagnostics=diagnostics)
            diagnostics["stage"] = "final-source-recheck"
            source = read_source(api, receipt["source_run_id"], diagnostics)
            check_source(source, sha, receipt["source_run_id"], receipt["source_branch"],
                         receipt["source_completed_at"])
            current = api.get("actions/runs/" + str(run_id))
            exact_run(current, run_id=run_id, sha=sha, event="push", branch="main")
        diagnostics["stage"] = "complete"
        result.update(status="verified", receipt=receipt)
        status = 0
    except Exception as error:
        receipt = None
        result["error"] = failure_record(error)
        prefix = ("Main CI reuse re-verification failed: " if arguments.phase == "finish" else
                  "Main CI reuse unavailable; running full validation: ")
        print(prefix + result["error"]["message"], file=sys.stderr)
    # Write before positive outputs. A failure record has no receipt and cannot
    # authorize reuse. Overwrite any stale result from an earlier invocation.
    try:
        arguments.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n",
                                    encoding="utf-8")
    except OSError as error:
        receipt = None
        status = 1
        print("Main CI reuse result could not be retained: " + failure_record(error)["message"], file=sys.stderr)
    if receipt is not None:
        source = receipt["source_run_id"]
        print(f"Main CI reuse verified {len(SOURCE_COVERAGE)} source jobs in "
              f"https://github.com/{REPOSITORY}/actions/runs/{source}/attempts/1")
    if arguments.phase == "decide":
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write("reuse=" + ("true" if receipt is not None else "false") + "\n")
            if receipt is not None:
                output.write("receipt_digest=" + receipt_digest(receipt) + "\n")
                output.write("receipt=" + handoff + "\n")
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
        if receipt is not None:
            summary.write(f"Queue validation verified for {len(SOURCE_COVERAGE)} jobs: "
                          f"[run {receipt['source_run_id']}, attempt 1]"
                          f"(https://github.com/{REPOSITORY}/actions/runs/"
                          f"{receipt['source_run_id']}/attempts/1). "
                          "Native, mobile and UEFI jobs did not execute on main. "
                          "Desktop jobs ran only the main Zig cache lifecycle and analyzer only retained a receipt; "
                          "their validation did not repeat.\n")
        else:
            summary.write(f"Main CI reuse {arguments.phase} did not verify; "
                          f"stage: {diagnostics['stage']}. No reuse success was issued.\n")
    return status


if __name__ == "__main__":
    sys.exit(cli())
