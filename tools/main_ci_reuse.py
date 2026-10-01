#!/usr/bin/env python3
"""Read-only queue-to-main CI reuse for issue #1808.

Only the eight native, mobile and UEFI jobs are reusable. The desktop shards
populate main's Zig cache, lint checks the main history, and the analyzer has
an event-specific reference; all three stay on the main push. This module
never creates a check or changes a ref. A missing proof schedules the normal
jobs, while a proof that changes after jobs were skipped fails the aggregate.

The decision publishes a digest-bound receipt for this main run. Finalization
revalidates that source ID and independently audits workflow-scoped uniqueness;
it never rediscovers or substitutes the source. Both phases retain classified
results, including failures (#2134). Coverage policy remains in REUSED.
"""

import argparse
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

REPOSITORY = "buster14a/buster"
REPOSITORY_ID = 1071732997
WORKFLOW_ID = 197051687
WORKFLOW_PATH = ".github/workflows/ci.yml"
POLICY = "buster-main-ci-reuse-v1"
MAX_AGE = timedelta(hours=2)
RECEIPT_SCHEMA = "buster-main-ci-reuse-receipt-v1"
RESULT_SCHEMA = "buster-main-ci-reuse-result-v1"
MAX_RECEIPT_BYTES = 32768
LOOKUP_ATTEMPTS = 3
DIGEST = re.compile(r"[0-9a-f]{64}\Z")
SHA = re.compile(r"[0-9a-f]{40}\Z")
# The REST API leaves skipped native/mobile matrices unexpanded, with the
# same expression name for both job groups. Accept only this observed shape;
# never deduplicate arbitrary jobs or treat a skipped placeholder as execution.
SKIPPED_MATRIX_NAME = (
    "${{ matrix.name }}${{ (((github.event_name == 'pull_request') && "
    "github.event.pull_request.draft && (github.run_attempt == '1') && "
    "startsWith(matrix.runner, 'macos-') && ' (deferred for draft PR)') || '') }}"
)
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
RETAINED_NAMES = tuple(name for name in github_ci_time.COMBINATION_JOBS
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
    require(type(run.get("run_attempt")) is int and run["run_attempt"] == 1,
            "rerun requires fresh main validation")


class ReuseError(AdmissionError):
    """A classified failure; diagnostics are never a reuse authorization."""

    def __init__(self, kind, message):
        super().__init__(message)
        self.kind = kind


def queue_candidates(api, sha, observations, *, scoped=False):
    # Finalization audits uniqueness in the named workflow instead of repeating
    # repository-wide discovery. An empty listing is inconclusive, not success:
    # retry it a bounded number of times, then reject even if the ID still exists.
    path = f"actions/workflows/{WORKFLOW_ID}/runs" if scoped else "actions/runs"
    candidates = []
    for attempt in range(1, (LOOKUP_ATTEMPTS if scoped else 1) + 1):
        observation = {"path": path, "attempt": attempt, "head_sha": sha, "event": "merge_group"}
        observations.setdefault("queue_lookups", []).append(observation)
        rows = api.pages(path, "workflow_runs", event="merge_group", head_sha=sha)
        if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
            raise ReuseError("malformed-queue-list", "malformed queue workflow listing")
        if scoped and any(row.get("workflow_id") != WORKFLOW_ID for row in rows):
            raise ReuseError("malformed-queue-list", "workflow-scoped listing returned another workflow")
        candidates = [row for row in rows if row.get("workflow_id") == WORKFLOW_ID]
        observation.update({
            "row_count": len(rows),
            "candidate_count": len(candidates),
            "candidates": [{key: row.get(key) if type(row.get(key)) is int else None
                            for key in ("id", "run_attempt")}
                           for row in candidates[:20]],
        })
        if len(candidates) > 1:
            raise ReuseError("ambiguous-queue-run", "multiple exact queue workflows")
        if candidates or not scoped or attempt == LOOKUP_ATTEMPTS:
            break
        time.sleep(attempt)
    if not candidates:
        raise ReuseError("missing-queue-run", "exact queue workflow is missing from listing")
    return candidates


def source_run(api, sha, *, expected=None, observations=None):
    observations = {} if observations is None else observations
    observations["stage"] = "source-run"
    if expected is None:
        observations["stage"] = "source-discovery"
        candidate = queue_candidates(api, sha, observations)[0]
        run_id = candidate.get("id")
        require(type(run_id) is int and run_id > 0, "invalid queue run ID")
        observations["stage"] = "source-run"
        source = api.get("actions/runs/" + str(run_id))
        require(isinstance(source, dict), "source run response must be an object")
        require(source.get("run_attempt") == candidate.get("run_attempt"),
                "queue run changed during discovery")
    else:
        # The digest-bound decision identifies the source. Never substitute a
        # different successful run, or the first row returned by a later list.
        run_id = expected["source_run_id"]
        source = api.get("actions/runs/" + str(run_id))
    require(isinstance(source, dict), "source run response must be an object")
    branch = source.get("head_branch", "")
    require(isinstance(branch, str) and branch.startswith("gh-readonly-queue/main/"),
            "source is not a main merge-queue ref")
    exact_run(source, run_id=run_id, sha=sha, event="merge_group", branch=branch)
    require(source.get("status") == "completed" and source.get("conclusion") == "success",
            "queue workflow has not succeeded")
    if expected is not None:
        require(branch == expected["source_branch"] and
                source.get("updated_at") == expected["source_completed_at"],
                "bound queue source changed after main jobs were skipped")
        observations["stage"] = "source-uniqueness"
        candidate = queue_candidates(api, sha, observations, scoped=True)[0]
        require(candidate.get("id") == run_id and candidate.get("run_attempt") == 1,
                "bound queue source was replaced or rerun")
        # Validate list identity too: a malformed filtered response is not proof.
        exact_run(candidate, run_id=run_id, sha=sha, event="merge_group", branch=branch)
    return source


def successful_source_jobs(api, source, sha):
    run_id = source["id"]
    jobs = api.pages(f"actions/runs/{run_id}/attempts/1/jobs", "jobs")
    jobs, extras = github_ci_time.separate_reuse_job(jobs, run_id, 1, sha)
    require(not extras, "; ".join(extras))
    errors = github_ci_time.validate_required_jobs(jobs, run_id, 1, sha,
                                                   complete_active=False)
    require(not errors, "; ".join(errors))
    identifiers = [job.get("id") for job in jobs]
    require(all(type(identifier) is int and identifier > 0 for identifier in identifiers) and
            len(set(identifiers)) == len(identifiers), "source job IDs are missing or duplicated")
    by_name = {job["name"]: job for job in jobs}
    for name, _, step in REUSED:
        mandatory = {step}
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
    return [{"name": name, "job_id": by_name[name]["id"]} for name, _, _ in REUSED]


def retained_artifacts(api, source, now):
    run_id = source["id"]
    rows = api.pages(f"actions/runs/{run_id}/artifacts", "artifacts")
    result = []
    for _, prefix, _ in REUSED:
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


def verify_source(api, sha, current_run_id, workflow_blob, now, *, expected=None, observations=None):
    observations = {} if observations is None else observations
    observations["stage"] = "current-run"
    current = api.get("actions/runs/" + str(current_run_id))
    exact_run(current, run_id=current_run_id, sha=sha, event="push", branch="main")
    require(current.get("run_attempt") == 1, "main run was rerun")
    source = source_run(api, sha, expected=expected, observations=observations)
    observations["stage"] = "source-freshness"
    completed = instant(source.get("updated_at"), "source completion")
    started = instant(current.get("created_at"), "main creation")
    require(completed <= started and started - completed <= MAX_AGE,
            "queue result is not a fresh predecessor of this main push")
    require(completed <= now, "source completion is in the future")
    observations["stage"] = "workflow-identity"
    blob = api.get("contents/" + WORKFLOW_PATH, ref=sha)
    require(blob.get("type") == "file" and blob.get("sha") == workflow_blob,
            "workflow revision differs from exact checkout")
    observations["stage"] = "source-jobs"
    jobs = successful_source_jobs(api, source, sha)
    observations["stage"] = "source-artifacts"
    artifacts = retained_artifacts(api, source, now)
    observations["stage"] = "post-collection"
    # Re-read both runs after paged jobs and artifacts: an attempt change or
    # interrupted source cannot authorize a skip during collection.
    final_source = api.get("actions/runs/" + str(source["id"]))
    final_current = api.get("actions/runs/" + str(current_run_id))
    require(final_source.get("run_attempt") == 1 and
            final_source.get("status") == "completed" and
            final_source.get("conclusion") == "success" and
            final_source.get("updated_at") == source["updated_at"] and
            final_current.get("run_attempt") == 1 and
            final_current.get("head_sha") == sha,
            "run or attempt moved during evidence collection")
    return {"schema": RECEIPT_SCHEMA, "policy": POLICY, "repository": REPOSITORY, "head_sha": sha,
            "main_run_id": current_run_id, "main_attempt": 1, "source_branch": source["head_branch"],
            "workflow_id": WORKFLOW_ID, "workflow_path": WORKFLOW_PATH,
            "workflow_blob": workflow_blob, "source_run_id": source["id"],
            "source_attempt": 1, "source_completed_at": source["updated_at"],
            "source_jobs": jobs, "source_artifacts": artifacts}


def current_execution_jobs(rows, sha, run_id, observations):
    require(isinstance(rows, list) and all(isinstance(row, dict) for row in rows),
            "current job inventory is malformed")
    identifiers = [row.get("id") for row in rows]
    require(all(type(identifier) is int and identifier > 0 for identifier in identifiers) and
            len(set(identifiers)) == len(identifiers), "current job IDs are missing or duplicated")
    skipped = [row for row in rows if row.get("name") in REUSED_NAMES or
               row.get("name") == SKIPPED_MATRIX_NAME]
    observations["main_skipped_jobs"] = [{key: row.get(key) for key in
                                         ("id", "name", "run_attempt", "status", "conclusion")}
                                        for row in skipped]
    names = [row.get("name") for row in skipped]
    if SKIPPED_MATRIX_NAME in names:
        require(len(names) == 3 and names.count(SKIPPED_MATRIX_NAME) == 2 and
                names.count("UEFI firmware boot") == 1,
                "skipped main matrix group inventory is incomplete or mixed")
    else:
        require(len(names) == len(REUSED_NAMES) and set(names) == REUSED_NAMES,
                "skipped main job inventory is incomplete or duplicated")
    for row in skipped:
        require(row.get("run_id") == run_id and row.get("head_sha") == sha and
                type(row.get("run_attempt")) is int and row["run_attempt"] == 1 and
                row.get("status") == "completed" and row.get("conclusion") == "skipped" and
                row.get("steps") in (None, []) and
                (row.get("runner_id") is None or type(row.get("runner_id")) is int and row["runner_id"] == 0) and
                row.get("runner_name") in (None, ""),
                "reusable main job executed or has invalid skip evidence")
    return [row for row in rows if row.get("name") not in REUSED_NAMES and
            row.get("name") != SKIPPED_MATRIX_NAME]


def verify_current_jobs(api, sha, run_id, *, observations=None):
    observations = {} if observations is None else observations
    current = api.get("actions/runs/" + str(run_id))
    exact_run(current, run_id=run_id, sha=sha, event="push", branch="main")
    rows = api.pages(f"actions/runs/{run_id}/jobs", "jobs", filter="all")
    rows = current_execution_jobs(rows, sha, run_id, observations)
    jobs = github_ci_time.latest_run_jobs(rows, run_id, 1, sha)
    jobs, extras = github_ci_time.separate_reuse_job(jobs, run_id, 1, sha, required=True)
    require(not extras, "; ".join(extras))
    errors = github_ci_time.validate_required_jobs(jobs, run_id, 1, sha,
                                                   expected_names=RETAINED_NAMES)
    require(not errors, "; ".join(errors))
    require(api.get("actions/runs/" + str(run_id)).get("run_attempt") == 1,
            "main attempt moved during job collection")
    return [{"name": job["name"], "job_id": job["id"]} for job in jobs]


def local_workflow_blob(sha):
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    require(head == sha, "checkout does not match the main run")
    return subprocess.check_output(["git", "hash-object", WORKFLOW_PATH], text=True).strip()


def unique_fields(pairs):
    fields = {}
    for key, value in pairs:
        require(key not in fields, "decision receipt contains a duplicate field")
        fields[key] = value
    return fields


def read_receipt(text, expected_digest, sha, run_id, workflow_blob):
    require(DIGEST.fullmatch(expected_digest) is not None, "invalid expected receipt digest")
    require(0 < len(text.encode("utf-8")) <= MAX_RECEIPT_BYTES,
            "decision receipt is missing or exceeds the size bound")
    receipt = json.loads(text, object_pairs_hook=unique_fields)
    require(isinstance(receipt, dict), "decision receipt must be an object")
    require(receipt_digest(receipt) == expected_digest, "decision receipt digest mismatch")
    require(receipt.get("schema") == RECEIPT_SCHEMA and receipt.get("policy") == POLICY and
            receipt.get("repository") == REPOSITORY and receipt.get("head_sha") == sha and
            type(receipt.get("main_run_id")) is int and receipt["main_run_id"] == run_id and
            type(receipt.get("main_attempt")) is int and receipt["main_attempt"] == 1 and
            receipt.get("workflow_id") == WORKFLOW_ID and
            receipt.get("workflow_path") == WORKFLOW_PATH and
            receipt.get("workflow_blob") == workflow_blob,
            "decision receipt does not bind this main run and workflow")
    require(type(receipt.get("source_run_id")) is int and receipt["source_run_id"] > 0 and
            type(receipt.get("source_attempt")) is int and receipt["source_attempt"] == 1 and
            isinstance(receipt.get("source_branch"), str) and
            receipt["source_branch"].startswith("gh-readonly-queue/main/"),
            "decision receipt has an invalid source identity")
    instant(receipt.get("source_completed_at"), "receipt source completion")
    return receipt


def write_result(path, report):
    # Leave an incomplete record before network work, and atomically replace it
    # at termination. Abrupt process failure cannot leave a success-shaped file.
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n",
                         encoding="utf-8")
    temporary.replace(path)


def failure_detail(error):
    # Never serialize response bodies, token-bearing URLs, or arbitrary exception
    # messages. Only our validation errors are printable; cap and redact those.
    kind = "internal-error"
    reason = type(error).__name__
    if isinstance(error, AdmissionError):
        kind = getattr(error, "kind", "invalid-evidence")
        reason = str(error)
        if "pagination" in reason or "paged API" in reason or "API response" in reason:
            kind = "incomplete-or-malformed-api"
    elif isinstance(error, urllib.error.HTTPError):
        kind, reason = "api-http-error", f"GitHub API returned HTTP {error.code}"
    elif isinstance(error, (OSError, urllib.error.URLError)):
        kind, reason = "api-or-io-error", type(error).__name__
    elif isinstance(error, (ValueError, TypeError, KeyError, RecursionError)):
        kind, reason = "malformed-data", type(error).__name__
    token = os.environ.get("GH_TOKEN", "")
    if token:
        reason = reason.replace(token, "[redacted]")
    return {"kind": kind, "reason": " ".join(reason.split())[:1024]}


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("decide", "finish"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-digest", default="")
    arguments = parser.parse_args()
    status = 0
    receipt = None
    verified = False
    observations = {"stage": "context"}
    report = {"schema": RESULT_SCHEMA, "phase": arguments.phase, "status": "incomplete",
              "observations": observations}
    try:
        write_result(arguments.output, report)
        require(os.environ.get("GITHUB_REPOSITORY") == REPOSITORY and
                os.environ.get("GITHUB_EVENT_NAME") == "push" and
                os.environ.get("GITHUB_REF") == "refs/heads/main", "not a main push")
        sha = os.environ.get("GITHUB_SHA", "")
        require(SHA.fullmatch(sha) is not None, "invalid main commit")
        run_id = int(os.environ.get("GITHUB_RUN_ID", "0"))
        require(run_id > 0, "invalid main run ID")
        require(os.environ.get("GITHUB_RUN_ATTEMPT") == "1", "explicit rerun requires full CI")
        report.update(repository=REPOSITORY, head_sha=sha, main_run_id=run_id, main_attempt=1)
        observations["stage"] = "checkout"
        workflow_blob = local_workflow_blob(sha)
        expected = None
        if arguments.phase == "finish":
            observations["stage"] = "receipt-handoff"
            if DIGEST.fullmatch(arguments.expected_digest):
                report["expected_digest"] = arguments.expected_digest
            expected = read_receipt(os.environ.get("SOURCE_RECEIPT", ""),
                                    arguments.expected_digest, sha, run_id, workflow_blob)
            report["expected_source_run_id"] = expected["source_run_id"]
            report["expected_source_attempt"] = expected["source_attempt"]
        api = GitHub(REPOSITORY, os.environ.get("GH_TOKEN", ""))
        receipt = verify_source(api, sha, run_id, workflow_blob, datetime.now(timezone.utc),
                                expected=expected, observations=observations)
        # Transport canonical, single-line JSON as data in a job output/env var,
        # never interpolate it into shell source. Keep it below the output limit.
        serialized = json.dumps(receipt, sort_keys=True, separators=(",", ":"), allow_nan=False)
        require(len(serialized.encode("utf-8")) <= MAX_RECEIPT_BYTES, "decision receipt exceeds size bound")
        if arguments.phase == "finish":
            observations["stage"] = "receipt-reverification"
            require(receipt_digest(receipt) == arguments.expected_digest,
                    "queue evidence changed after main jobs were skipped")
            observations["stage"] = "main-jobs"
            receipt["main_jobs"] = verify_current_jobs(api, sha, run_id, observations=observations)
            # Reaudit uniqueness after paged main work, then re-read the source
            # directly so an attempt started during listing cannot hide behind it.
            source_run(api, sha, expected=expected, observations=observations)
            observations["stage"] = "final-run-check"
            source = api.get("actions/runs/" + str(receipt["source_run_id"]))
            exact_run(source, run_id=receipt["source_run_id"], sha=sha,
                      event="merge_group", branch=receipt["source_branch"])
            require(source.get("status") == "completed" and source.get("conclusion") == "success" and
                    source.get("updated_at") == receipt["source_completed_at"],
                    "queue attempt moved after main job collection")
            current = api.get("actions/runs/" + str(run_id))
            exact_run(current, run_id=run_id, sha=sha, event="push", branch="main")
        observations["stage"] = "complete"
        report.update(status="verified", receipt=receipt)
        write_result(arguments.output, report)
        verified = True
    except Exception as error:
        report.pop("receipt", None)
        report.update(status="rejected" if arguments.phase == "finish" else "unavailable",
                      error=failure_detail(error))
        message = report["error"]["reason"]
        if arguments.phase == "finish":
            print("Main CI reuse re-verification failed: " + message, file=sys.stderr)
            status = 1
        else:
            print("Main CI reuse unavailable; running full validation: " + message)
        try:
            write_result(arguments.output, report)
        except OSError:
            print("Main CI reuse could not retain its diagnostic record", file=sys.stderr)
            status = 1
    if arguments.phase == "decide":
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write("reuse=" + ("true" if verified else "false") + "\n")
            if verified:
                output.write("receipt_digest=" + receipt_digest(receipt) + "\n")
                output.write("receipt=" + serialized + "\n")
    if verified:
        source = receipt["source_run_id"]
        print(f"Main CI reuse verified {len(REUSED)} source jobs in "
              f"https://github.com/{REPOSITORY}/actions/runs/{source}/attempts/1")
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
        if verified:
            summary.write(f"Queue validation reused for {len(REUSED)} jobs: "
                          f"[run {receipt['source_run_id']}, attempt 1]"
                          f"(https://github.com/{REPOSITORY}/actions/runs/"
                          f"{receipt['source_run_id']}/attempts/1). "
                          "The skipped main jobs did not execute.\n")
        else:
            summary.write(f"Main CI reuse {report['status']} at {observations['stage']}; "
                          "no reuse proof was issued by this phase. See its diagnostic JSON.\n")
    return status


if __name__ == "__main__":
    sys.exit(cli())
