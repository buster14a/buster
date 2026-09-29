#!/usr/bin/env python3
"""Collect and summarize GitHub CI timing without mixing partial runs or matrices.

Ownership: GitHub Actions observations only; tools/ci_time.py owns Forgejo's
very different timestamps/retention repair. No inferred or imputed durations.
Entry points: collect reads the GitHub REST API; summarize is entirely offline.
Each workflow-blob/runner-label cohort has its own median and exclusion counts.
queue-collect/queue-summarize measure runner scheduling across every workflow
(#1805): queue_collect, queue_summarize, _queue_job_record, _occupancy.
"""
import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import re
import statistics
import sys
import time
import urllib.parse
import urllib.request

PLATFORMS = ("Linux x86-64", "Linux AArch64", "macOS x86-64", "macOS AArch64",
             "Windows x86-64", "Windows AArch64")
MOBILE = ("Android x86-64", "iOS x86-64", "iOS AArch64")
SHARDED_JOBS = PLATFORMS + MOBILE + ("Workflow lint", "CI complete")
UNIX_NATIVE = tuple(name + " native" for name in PLATFORMS if not name.startswith("Windows"))
NATIVE = tuple(name + " native" for name in PLATFORMS)
LEGACY_PARTITIONED_JOBS = SHARDED_JOBS + UNIX_NATIVE
PARTITIONED_JOBS = SHARDED_JOBS + NATIVE
UEFI = ("UEFI firmware boot",)
ANALYZER = ("Clang analyzer shards",)
LEGACY_SUITE_JOBS = LEGACY_PARTITIONED_JOBS + UEFI + ANALYZER
SUITE_JOBS = PARTITIONED_JOBS + UEFI + ANALYZER
COMBINATION_SHARDS = ("release", "checks")
COMBINATION_PLATFORMS = tuple(f"{platform} {shard}" for platform in PLATFORMS for shard in COMBINATION_SHARDS)
LEGACY_COMBINATION_JOBS = COMBINATION_PLATFORMS + MOBILE + UNIX_NATIVE + UEFI + ANALYZER + ("Workflow lint", "CI complete")
COMBINATION_JOBS = COMBINATION_PLATFORMS + MOBILE + NATIVE + UEFI + ANALYZER + ("Workflow lint", "CI complete")
RUN_FIELDS = ("id", "head_sha", "head_branch", "event", "path", "status", "conclusion",
              "run_attempt", "created_at", "run_started_at", "html_url")
JOB_FIELDS = ("id", "name", "run_attempt", "status", "conclusion", "created_at", "started_at", "completed_at", "labels")
STEP_FIELDS = ("name", "status", "conclusion", "started_at", "completed_at")
API_TIMEOUT_SECONDS = 30.0
JOB_METADATA_REFRESH_BUDGET_SECONDS = 30.0
JOB_METADATA_REFRESH_DELAYS_SECONDS = (1.0, 2.0, 4.0)
# The run search API returns at most 1000 results for one query.
RUN_SEARCH_RESULT_LIMIT = 1000
QUEUE_RUN_FIELDS = ("id", "name", "path", "event", "head_sha", "head_branch", "status", "conclusion",
                    "run_attempt", "created_at", "updated_at", "html_url")
# Live run listings; a queued run has no runner yet and is still demand.
LIVE_RUN_STATUSES = ("queued", "in_progress", "waiting", "pending", "requested")
QUEUE_LISTING_ATTEMPTS = 3
# Bounded concurrent REST reads; results keep run-ID order.
QUEUE_FETCH_WORKERS = 8
# Longest observed CI attempt plus assignment wait; earlier occupancy is incomplete.
QUEUE_OCCUPANCY_WARMUP_SECONDS = 5400
QUEUE_JOB_FIELDS = ("id", "run_id", "run_attempt", "head_sha", "name", "status", "conclusion", "created_at",
                    "started_at", "completed_at", "labels", "runner_id", "runner_name", "runner_group_name")


def timestamp(value):
    result = datetime.fromisoformat(value.replace("Z", "+00:00")) if value else None
    if result is not None and result.tzinfo is None:
        raise ValueError("GitHub timestamps must have a timezone")
    return result


def measure(run):
    """A successful six-platform first attempt, or an explicit exclusion reason."""
    reason = None
    result = None
    jobs = run.get("jobs", [])
    names = sorted(job.get("name", "") for job in jobs)
    combinations = names in (sorted(LEGACY_COMBINATION_JOBS), sorted(COMBINATION_JOBS))
    suites = names in (sorted(LEGACY_PARTITIONED_JOBS), sorted(PARTITIONED_JOBS),
                       sorted(LEGACY_SUITE_JOBS), sorted(SUITE_JOBS),
                       sorted(LEGACY_COMBINATION_JOBS), sorted(COMBINATION_JOBS))
    sharded = names == sorted(SHARDED_JOBS) or suites
    if run.get("status") != "completed":
        reason = "not-completed"
    elif run.get("conclusion") != "success":
        reason = run.get("conclusion") or "no-conclusion"
    elif run.get("run_attempt") != 1:
        reason = "rerun"
    elif names != sorted(PLATFORMS) and not sharded:
        reason = "incomplete-or-different-matrix"
    elif not run.get("workflow_blob_sha"):
        reason = "unknown-workflow-revision"
    else:
        starts = []
        finishes = []
        busy = 0.0
        step_seconds = {}
        job_seconds = {}
        job_queue_seconds = {}
        for job in jobs:
            name = job["name"]
            required = set()
            if name in PLATFORMS or name in COMBINATION_PLATFORMS:
                required.add("Combination matrix (Windows)" if name.startswith("Windows")
                             else "Combination matrix (Linux, macOS)")
                if combinations:
                    required.update(("Install verified Zig", "Desktop result and reproduction", "Retain desktop logs"))
                    if name.endswith(" release"):
                        required.update(("Workflow tool regression tests", "Bootstrap wrapper regression tests"))
                if not suites and not name.startswith("Windows"):
                    required.add("Execution-mode matrix")
                if not sharded:
                    if name.startswith("macOS"):
                        required.add("Test (iOS simulator)")
                    if name == "Linux x86-64":
                        required.add("Test (Android)")
            elif name in NATIVE:
                required.add("Execution-mode matrix (Windows)" if name.startswith("Windows")
                             else "Execution-mode matrix")
                if not name.startswith("Windows"):
                    required.add("Native configuration differential matrix")
            elif name.startswith("iOS"):
                required.add("Test (iOS simulator)")
            elif name.startswith("Android"):
                required.add("Test (Android)")
            elif name == "Workflow lint":
                required.add("Validate every GitHub workflow")
            elif name == "CI complete":
                required.add("Require every shard")
                if combinations:
                    required.add("Verify every desktop partition exists")
            elif name in UEFI:
                required.add("Build compiler and boot both architectures in all allocators")
            elif name in ANALYZER:
                required.update(("Exercise analyzer failure and coverage controls",
                                 "Compare reference analysis and aggregate all module shards"))
            passed = {step["name"] for step in job.get("steps", []) if step.get("conclusion") == "success"}
            if job.get("conclusion") != "success" or job.get("run_attempt") != 1 or not required <= passed:
                reason = "incomplete-coverage"
            start, finish = timestamp(job.get("started_at")), timestamp(job.get("completed_at"))
            if start is None or finish is None or finish < start:
                reason = "invalid-job-timestamps"
            else:
                starts.append(start)
                finishes.append(finish)
                job_seconds[name] = (finish - start).total_seconds()
                busy += job_seconds[name]
            queued = timestamp(job.get("created_at"))
            job_queue_seconds[name] = (start - queued).total_seconds() if queued is not None and start is not None and queued <= start else None
            durations = {}
            for step in job.get("steps", []):
                left, right = timestamp(step.get("started_at")), timestamp(step.get("completed_at"))
                if left is not None and right is not None and right >= left:
                    durations[step["name"]] = (right - left).total_seconds()
            step_seconds[name] = durations
        created = timestamp(run.get("created_at"))
        if reason is None:
            if created is None or created > min(starts):
                reason = "invalid-run-timestamps"
            else:
                result = {"run_id": run["id"], "head_sha": run["head_sha"],
                          "elapsed_seconds": (max(finishes) - created).total_seconds(),
                          "execution_span_seconds": (max(finishes) - min(starts)).total_seconds(),
                          "initial_queue_seconds": (min(starts) - created).total_seconds(),
                          "runner_seconds": busy, "step_seconds": step_seconds,
                          "job_seconds": job_seconds, "job_queue_seconds": job_queue_seconds}
    return result, reason


def summarize(data):
    cohorts = defaultdict(list)
    excluded = Counter()
    seen = set()
    for run in data["runs"]:
        identity = (run["id"], run.get("run_attempt"))
        if identity in seen:
            raise ValueError("Duplicate run/attempt observations would bias the median")
        seen.add(identity)
        sample, reason = measure(run)
        if reason:
            excluded[reason] += 1
        else:
            runners = tuple(sorted((job["name"], tuple(sorted(job.get("labels", [])))) for job in run["jobs"]))
            cohorts[(run["workflow_blob_sha"], runners)].append(sample)
    rows = []
    for (revision, runners), samples in sorted(cohorts.items()):
        rows.append({"workflow_blob_sha": revision, "runners": runners, "n": len(samples),
                     "medians": {key: statistics.median(sample[key] for sample in samples)
                                 for key in ("elapsed_seconds", "execution_span_seconds", "initial_queue_seconds", "runner_seconds")},
                     "samples": samples})
    return {"schema": 1, "cohorts": rows, "excluded": dict(excluded),
            "notes": ["Elapsed = workflow creation to last required job completion; queueing is included.",
                      "Execution span still includes any staggered runner starts; runner_seconds sums active job intervals.",
                      "Cancelled, failed, partial, rerun and differently configured runs are never pooled into a speedup.",
                      "Cache state and compiler source changes require separate review; these are descriptive medians, not causal claims."]}


def api_get(repository, path, token, timeout=API_TIMEOUT_SECONDS):
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28",
               "User-Agent": "buster-ci-timing"}
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(f"https://api.github.com/repos/{repository}/{path}", headers=headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        result = json.load(response)
    return result


def _required_job_steps(name):
    required = set()
    if name in COMBINATION_PLATFORMS:
        required.update(("Install verified Zig", "Desktop result and reproduction", "Retain desktop logs",
                         "Combination matrix (Windows)" if name.startswith("Windows")
                         else "Combination matrix (Linux, macOS)"))
        if name.endswith(" release"):
            required.update(("Workflow tool regression tests", "Bootstrap wrapper regression tests"))
    elif name in NATIVE:
        required.update(("Native result and reproduction",
                         "Execution-mode matrix (Windows)" if name.startswith("Windows")
                         else "Execution-mode matrix"))
        if not name.startswith("Windows"):
            required.add("Native configuration differential matrix")
    return tuple(sorted(required))


def _metadata_pending(message):
    return f"metadata pending: {message}"


def _metadata_can_refresh(errors):
    return bool(errors) and all(error.startswith("metadata pending:") for error in errors)


def _job_evidence(jobs):
    evidence = []
    for job in jobs:
        required_steps = []
        steps = job.get("steps", [])
        if not isinstance(steps, list):
            steps = []
        for name in _required_job_steps(job.get("name")):
            matching = [step for step in steps if isinstance(step, dict) and step.get("name") == name]
            required_steps.append({
                "name": name,
                "matching_records": len(matching),
                "observed": [{"status": step.get("status"), "conclusion": step.get("conclusion")}
                             for step in matching],
            })
        record = {key: job.get(key) for key in
                  ("id", "name", "run_id", "head_sha", "run_attempt", "status", "conclusion")}
        record["required_steps"] = required_steps
        evidence.append(record)
    return evidence


def validate_required_jobs(jobs, run_id, run_attempt, head_sha):
    """Pure fail-closed gate for the latest jobs of this exact workflow run.

    A partial rerun may retain a successful job from an earlier attempt of the
    same immutable run/source. Failed historical attempts are not substituted
    for latest results, and timing cohorts still reject all reruns.
    """
    errors = []
    if not isinstance(jobs, list):
        return [_metadata_pending("job inventory is not a list")]
    names = [job.get("name") if isinstance(job, dict) else None for job in jobs]
    if Counter(names) != Counter(COMBINATION_JOBS):
        errors.append(_metadata_pending("required job identities are missing, duplicated or unexpected"))
    for job in jobs:
        if not isinstance(job, dict):
            errors.append(_metadata_pending("malformed job record"))
            continue
        name = job.get("name", "missing")
        attempt = job.get("run_attempt")
        if job.get("run_id") != run_id or job.get("head_sha") != head_sha:
            errors.append(f"{name}: job belongs to another run or source")
        if not isinstance(attempt, int) or isinstance(attempt, bool) or not 1 <= attempt <= run_attempt:
            errors.append(f"{name}: invalid job attempt")
        if name == "CI complete":
            if attempt != run_attempt or job.get("status") != "in_progress":
                errors.append(_metadata_pending("CI complete is not the current active attempt"))
        elif job.get("status") != "completed":
            status = job.get("status")
            if status in (None, "queued", "in_progress", "waiting", "pending"):
                errors.append(_metadata_pending(f"{name}: job status is not terminal (status={status!r})"))
            else:
                errors.append(f"{name}: required job did not complete successfully (status={status!r})")
        elif job.get("conclusion") != "success":
            conclusion = job.get("conclusion")
            if conclusion is None:
                errors.append(_metadata_pending(f"{name}: completed job has no conclusion"))
            else:
                errors.append(f"{name}: required job did not complete successfully (conclusion={conclusion!r})")
        for step_name in _required_job_steps(name):
            steps = job.get("steps", [])
            if not isinstance(steps, list):
                errors.append(_metadata_pending(f"{name}: required step {step_name!r} has no usable records"))
                continue
            matching = [step for step in steps if isinstance(step, dict) and step.get("name") == step_name]
            if len(matching) != 1:
                errors.append(_metadata_pending(
                    f"{name}: required step {step_name!r} lacks unique completion proof "
                    f"(found {len(matching)} records)"))
            elif matching[0].get("status") != "completed" or matching[0].get("conclusion") != "success":
                conclusion = matching[0].get("conclusion")
                if conclusion not in (None, "") and conclusion != "success":
                    errors.append(f"{name}: required step {step_name!r} concluded {conclusion!r}; success required")
                else:
                    errors.append(_metadata_pending(
                        f"{name}: required step {step_name!r} lacks completed success proof "
                        f"(status={matching[0].get('status')!r}, conclusion={conclusion!r})"))
    return sorted(set(errors))


def latest_run_jobs(jobs, run_id, run_attempt, head_sha):
    """Select by attempt, never by success; prior green cannot hide later red."""
    latest = {}
    seen = set()
    for job in jobs:
        if not isinstance(job, dict):
            raise ValueError("Malformed historical job")
        name, attempt = job.get("name"), job.get("run_attempt")
        if not isinstance(name, str) or not name or not isinstance(attempt, int) or isinstance(attempt, bool) or not 1 <= attempt <= run_attempt:
            raise ValueError("Malformed historical job name/attempt")
        if job.get("run_id") != run_id or job.get("head_sha") != head_sha:
            raise ValueError("Historical job belongs to another run or source")
        identity = (name, attempt)
        if identity in seen:
            raise ValueError("Duplicate job identity within one attempt")
        seen.add(identity)
        if name not in latest or attempt > latest[name]["run_attempt"]:
            latest[name] = job
    return list(latest.values())


def require_jobs(args):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository or ""):
        raise ValueError("Repository must have owner/name form")
    if args.run_id <= 0 or args.run_attempt <= 0:
        raise ValueError("A positive current run ID and attempt are required")
    token = os.getenv("GH_TOKEN") or os.getenv("GITHUB_TOKEN")
    run = api_get(args.repository, f"actions/runs/{args.run_id}", token)
    if run.get("id") != args.run_id or run.get("run_attempt") != args.run_attempt or \
            run.get("path", "").split("@", 1)[0] != ".github/workflows/ci.yml":
        raise ValueError("The API run identity does not match this CI execution")
    head_sha = run.get("head_sha")
    if not isinstance(head_sha, str) or not re.fullmatch(r"[0-9a-f]{40}", head_sha):
        raise ValueError("The API run has no exact source identity")
    deadline = time.monotonic() + JOB_METADATA_REFRESH_BUDGET_SECONDS
    jobs = []
    errors = []
    snapshot_attempts = 0
    for snapshot_attempt in range(len(JOB_METADATA_REFRESH_DELAYS_SECONDS) + 1):
        snapshot_attempts = snapshot_attempt + 1
        errors = []
        inventory = []
        total = None
        page = 1
        while total is None or len(inventory) < total:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                errors = [_metadata_pending("job metadata refresh budget exhausted before a complete snapshot")]
                break
            batch = api_get(args.repository,
                            f"actions/runs/{args.run_id}/jobs?filter=all&per_page=100&page={page}",
                            token, timeout=min(API_TIMEOUT_SECONDS, remaining))
            count = batch.get("total_count")
            if not isinstance(count, int) or isinstance(count, bool) or not 1 <= count <= 1000 or \
                    (total is not None and count != total):
                raise ValueError("Missing, changing or excessive job inventory")
            total = count
            chunk = batch.get("jobs")
            if not isinstance(chunk, list) or not chunk or len(inventory) + len(chunk) > total:
                raise ValueError("Incomplete job pagination; refusing a partial inventory snapshot")
            inventory.extend(chunk)
            page += 1
        if errors:
            break
        try:
            # filter=latest can hide successful non-rerun jobs. Reconstruct each
            # logical job from all attempts of this exact immutable run/head.
            jobs = latest_run_jobs(inventory, args.run_id, args.run_attempt, head_sha)
        except ValueError as error:
            jobs = []
            errors = [_metadata_pending(f"job-attempt inventory is inconsistent: {error}")]
        else:
            errors = validate_required_jobs(jobs, args.run_id, args.run_attempt, head_sha)
        if not _metadata_can_refresh(errors) or snapshot_attempt >= len(JOB_METADATA_REFRESH_DELAYS_SECONDS):
            break
        delay = JOB_METADATA_REFRESH_DELAYS_SECONDS[snapshot_attempt]
        remaining = deadline - time.monotonic()
        if delay >= remaining:
            errors.append(_metadata_pending("refresh budget expired before another exact-run snapshot"))
            break
        time.sleep(delay)
    if _metadata_can_refresh(errors):
        errors = [f"{error}; exact run/head proof unresolved after {snapshot_attempts} snapshots "
                  f"(run {args.run_id}, head {head_sha})" for error in errors]
    return {"schema": 1, "run_id": args.run_id, "run_attempt": args.run_attempt,
            "run_head_sha": head_sha, "checkout_sha": os.getenv("GITHUB_SHA", "unknown"),
            "success": not errors, "errors": errors,
            "job_metadata": {"snapshot_attempts": snapshot_attempts,
                             "refreshes": max(0, snapshot_attempts - 1),
                             "refresh_budget_seconds": JOB_METADATA_REFRESH_BUDGET_SECONDS},
            "jobs": _job_evidence(jobs)}


def collect(args):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        raise ValueError("Repository must have owner/name form")
    token = os.getenv("GH_TOKEN") or os.getenv("GITHUB_TOKEN")
    selected = []
    exhausted = False
    for page in range(1, args.max_pages + 1):
        if len(selected) < args.limit and not exhausted:
            query = {"per_page": 100, "page": page}
            if args.branch:
                query["branch"] = args.branch
            if args.head_sha:
                query["head_sha"] = args.head_sha
            batch = api_get(args.repository, "actions/runs?" + urllib.parse.urlencode(query), token)["workflow_runs"]
            exhausted = len(batch) < 100
            for run in batch:
                if run["path"].split("@", 1)[0] == args.workflow and len(selected) < args.limit:
                    selected.append({key: run.get(key) for key in RUN_FIELDS})
    for run in selected:
        run["jobs"] = []
        if run["status"] == "completed":
            page = 1
            total = None
            while total is None or len(run["jobs"]) < total:
                batch = api_get(args.repository, f"actions/runs/{run['id']}/jobs?filter=latest&per_page=100&page={page}", token)
                total = batch["total_count"]
                if not batch["jobs"] and len(run["jobs"]) < total:
                    raise ValueError("Incomplete job pagination; refusing a partial measurement")
                for job in batch["jobs"]:
                    record = {key: job.get(key) for key in JOB_FIELDS}
                    record["steps"] = [{key: step.get(key) for key in STEP_FIELDS} for step in job.get("steps", [])]
                    run["jobs"].append(record)
                page += 1
            query = urllib.parse.urlencode({"ref": run["head_sha"]})
            run["workflow_blob_sha"] = api_get(args.repository, f"contents/{args.workflow}?{query}", token)["sha"]
    return {"schema": 1, "repository": args.repository, "fetched_at": datetime.now(timezone.utc).isoformat(),
            "selection": {"branch": args.branch, "head_sha": args.head_sha, "workflow": args.workflow,
                          "limit": args.limit, "page_bound_reached": not exhausted and len(selected) < args.limit},
            "runs": selected}


def _utc_now():
    return datetime.now(timezone.utc).isoformat()


def _complete_pages(repository, path, key, token, limit, live):
    """Every page of one listing and its reported total, or ValueError.

    Historical listings must reconcile exactly. A live status listing changes
    while it is paged, so it may end short; callers retain the reported total.
    """
    items = []
    total = None
    page = 1
    ended = False
    while not ended and (total is None or len(items) < total):
        separator = "&" if "?" in path else "?"
        batch = api_get(repository, f"{path}{separator}per_page=100&page={page}", token)
        count = batch.get("total_count")
        if not isinstance(count, int) or isinstance(count, bool) or not 0 <= count <= limit or \
                (total is not None and count != total and not live):
            raise ValueError(f"Missing, changing or over-limit total_count for {path}; narrow the selection")
        total = count if total is None else total
        chunk = batch.get(key)
        if not isinstance(chunk, list):
            raise ValueError(f"Malformed page for {path}")
        ended = len(chunk) < 100
        if not live and ((ended and len(items) + len(chunk) != total) or len(items) + len(chunk) > total):
            raise ValueError(f"Incomplete pagination for {path}; refusing a partial inventory")
        items.extend(chunk)
        page += 1
    return items, total


def _stable_pages(repository, path, key, token, limit):
    """Retry a listing whose total moved between pages (a job was added to a live run)."""
    result = None
    failure = None
    for _ in range(QUEUE_LISTING_ATTEMPTS):
        if result is None:
            try:
                result = _complete_pages(repository, path, key, token, limit, False)[0]
            except ValueError as error:
                failure = error
    if result is None:
        raise failure
    return result


def _created_windows(repository, since, until, token):
    """Split [since, until] until every created= query fits the search result limit."""
    windows = []
    pending = [(since, until)]
    while pending:
        left, right = pending.pop()
        query = urllib.parse.urlencode({"created": f"{left.strftime('%Y-%m-%dT%H:%M:%SZ')}.."
                                                   f"{right.strftime('%Y-%m-%dT%H:%M:%SZ')}", "per_page": 1})
        count = api_get(repository, "actions/runs?" + query, token).get("total_count")
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            raise ValueError("Missing run total_count")
        if count <= RUN_SEARCH_RESULT_LIMIT:
            windows.append((left, right))
        elif (right - left).total_seconds() < 2:
            raise ValueError("More than one search page of runs in one second; cannot enumerate completely")
        else:
            middle = left + (right - left) / 2
            middle = middle.replace(microsecond=0)
            # created= bounds are inclusive and second-granular: keep halves disjoint.
            pending.append((middle + timedelta(seconds=1), right))
            pending.append((left, middle))
    return sorted(windows)


def _queue_run_detail(repository, token, run):
    run["attempts"] = {}
    latest = run.get("run_attempt") or 1
    for attempt in range(1, latest + 1):
        if attempt == latest:
            detail = run
        else:
            detail = api_get(repository, f"actions/runs/{run['id']}/attempts/{attempt}", token)
        run["attempts"][str(attempt)] = {"run_started_at": detail.get("run_started_at"),
                                         "status": detail.get("status"), "conclusion": detail.get("conclusion")}
    jobs = _stable_pages(repository, f"actions/runs/{run['id']}/jobs?filter=all", "jobs", token, RUN_SEARCH_RESULT_LIMIT)
    run["observed_at"] = _utc_now()
    run["jobs"] = []
    for job in jobs:
        record = {key: job.get(key) for key in QUEUE_JOB_FIELDS}
        record["step_count"] = len(job.get("steps") or [])
        run["jobs"].append(record)
    return run


def queue_collect(args):
    """Runs created in [since, until] plus every currently unfinished run, with all job attempts."""
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        raise ValueError("Repository must have owner/name form")
    since, until = timestamp(args.since), timestamp(args.until)
    if since is None or until is None or until <= since:
        raise ValueError("Use timezone-qualified --since earlier than --until")
    since, until = since.astimezone(timezone.utc).replace(microsecond=0), until.astimezone(timezone.utc).replace(microsecond=0)
    token = os.getenv("GH_TOKEN") or os.getenv("GITHUB_TOKEN")
    selected = {}
    listings = []
    windows = _created_windows(args.repository, since, until, token)
    queries = [({"created": f"{left.strftime('%Y-%m-%dT%H:%M:%SZ')}..{right.strftime('%Y-%m-%dT%H:%M:%SZ')}"}, False)
               for left, right in windows]
    queries += [({"status": status}, True) for status in LIVE_RUN_STATUSES]
    for query, live in queries:
        path = "actions/runs?" + urllib.parse.urlencode(query)
        if live:
            batch, total = _complete_pages(args.repository, path, "workflow_runs", token, RUN_SEARCH_RESULT_LIMIT, True)
        else:
            batch = _stable_pages(args.repository, path, "workflow_runs", token, RUN_SEARCH_RESULT_LIMIT)
            total = len(batch)
        listings.append({"query": query, "reported_total": total, "obtained": len(batch), "observed_at": _utc_now()})
        for run in batch:
            record = selected.get(run["id"]) or dict({key: run.get(key) for key in QUEUE_RUN_FIELDS},
                                                     run_started_at=run.get("run_started_at"), selected_by=[])
            kind = "created" if "created" in query else "live"
            record["selected_by"] = sorted(set(record["selected_by"]) | {kind})
            selected[run["id"]] = record
    runs = [selected[identity] for identity in sorted(selected)]
    with ThreadPoolExecutor(max_workers=QUEUE_FETCH_WORKERS) as pool:
        runs = list(pool.map(lambda run: _queue_run_detail(args.repository, token, run), runs))
    return {"schema": 1, "kind": "queue", "repository": args.repository, "fetched_at": _utc_now(),
            "window": {"since": since.isoformat(), "until": until.isoformat()}, "listings": listings, "runs": runs}


def _percentiles(values):
    ordered = sorted(values)
    result = {"n": len(ordered)}
    if ordered:
        # Nearest rank: the smallest observation covering the percentile.
        for label, percent in (("p50", 50), ("p90", 90)):
            result[label] = ordered[max(0, -(-len(ordered) * percent // 100) - 1)]
        result["max"] = ordered[-1]
    return result


def runner_family(labels):
    text = " ".join(labels or ()).lower()
    result = "other"
    for family in ("macos", "windows", "ubuntu"):
        if family in text:
            result = family
    return result


def _queue_job_record(run, job):
    """Classify one job; a placeholder started_at without a runner is never execution evidence."""
    observed = timestamp(run.get("observed_at"))
    created = timestamp(job.get("created_at"))
    started = timestamp(job.get("started_at"))
    completed = timestamp(job.get("completed_at"))
    attempt = run.get("attempts", {}).get(str(job.get("run_attempt")), {})
    attempt_start = timestamp(attempt.get("run_started_at"))
    runner_id = job.get("runner_id")
    assigned = (isinstance(runner_id, int) and not isinstance(runner_id, bool) and runner_id > 0) or bool(job.get("runner_name"))
    status = job.get("status")
    record = {"run_id": run["id"], "run_attempt": job.get("run_attempt"), "job_id": job.get("id"),
              "head_sha": job.get("head_sha"), "name": job.get("name"), "event": run.get("event"),
              "workflow": (run.get("path") or "").split("@", 1)[0], "status": status,
              "conclusion": job.get("conclusion"), "labels": ",".join(sorted(job.get("labels") or [])) or "unlabelled",
              "family": runner_family(job.get("labels")), "assigned": assigned, "created": created,
              "observed": observed, "dependency_wait": None, "assignment_wait": None, "execution": None,
              "busy": None, "waiting": None}
    if created is not None and attempt_start is not None and created >= attempt_start:
        record["dependency_wait"] = (created - attempt_start).total_seconds()
    if assigned and created is not None and started is not None and started >= created:
        record["assignment_wait"] = (started - created).total_seconds()
        record["waiting"] = (created, started)
        end = completed if status == "completed" else observed
        if end is not None and end >= started:
            record["busy"] = (started, end)
            if status == "completed":
                record["execution"] = (end - started).total_seconds()
    elif not assigned and created is not None:
        end = completed if status == "completed" else observed
        if end is not None and end >= created:
            record["waiting"] = (created, end)
    return record


def _occupancy(records, begin):
    """Sweep assigned and waiting intervals; time-weighted occupancy while jobs waited after begin."""
    events = []
    for record in records:
        if record["busy"] is not None:
            events.append((record["busy"][0], 0, 1, 0))
            events.append((record["busy"][1], 0, -1, 0))
        if record["waiting"] is not None and record["waiting"][1] > record["waiting"][0]:
            events.append((record["waiting"][0], 1, 0, 1))
            events.append((record["waiting"][1], 1, 0, -1))
    events.sort(key=lambda event: (event[0], -(event[2] + event[3])))
    busy = 0
    waiting = 0
    peak_busy = 0
    peak_waiting = 0
    while_waiting = Counter()
    previous = None
    for moment, _, busy_delta, waiting_delta in events:
        if previous is not None and waiting > 0 and moment > max(previous, begin):
            while_waiting[busy] += (moment - max(previous, begin)).total_seconds()
        busy += busy_delta
        waiting += waiting_delta
        if moment >= begin:
            peak_busy = max(peak_busy, busy)
            peak_waiting = max(peak_waiting, waiting)
        previous = moment
    contended = sum(while_waiting.values())
    return {"measured_from": begin.isoformat(), "peak_occupied": peak_busy, "peak_waiting": peak_waiting, "seconds_with_waiting_jobs": contended,
            "occupied_while_waiting_seconds": {str(key): value for key, value in sorted(while_waiting.items())},
            "max_occupied_while_waiting": max(while_waiting) if while_waiting else None}


def queue_summarize(data):
    if data.get("kind") != "queue":
        raise ValueError("queue-summarize requires queue-collect output")
    records = []
    seen = set()
    for run in data["runs"]:
        for job in run.get("jobs", []):
            identity = (job.get("id"), job.get("run_attempt"))
            if identity in seen:
                raise ValueError("Duplicate job observation would bias the distribution")
            seen.add(identity)
            records.append(_queue_job_record(run, job))
    groups = defaultdict(list)
    for record in records:
        groups[(record["labels"], record["event"])].append(record)
    rows = []
    for (labels, event), members in sorted(groups.items()):
        queued = [record for record in members if not record["assigned"] and record["status"] != "completed"]
        oldest = None
        for record in queued:
            if record["created"] is not None and record["observed"] is not None:
                age = (record["observed"] - record["created"]).total_seconds()
                if oldest is None or age > oldest["age_seconds"]:
                    oldest = {key: record[key] for key in ("run_id", "run_attempt", "job_id", "head_sha", "name", "status")}
                    oldest.update(created_at=record["created"].isoformat(), observed_at=record["observed"].isoformat(),
                                  age_seconds=age)
        rows.append({"labels": labels, "event": event, "family": members[0]["family"], "jobs": len(members),
                     "dependency_wait_seconds": _percentiles([r["dependency_wait"] for r in members if r["dependency_wait"] is not None]),
                     "assignment_wait_seconds": _percentiles([r["assignment_wait"] for r in members if r["assignment_wait"] is not None]),
                     "execution_seconds": _percentiles([r["execution"] for r in members if r["execution"] is not None]),
                     "runner_seconds": sum(r["execution"] for r in members if r["execution"] is not None),
                     "active_at_observation": sum(1 for r in members if r["assigned"] and r["status"] == "in_progress"),
                     "queued_at_observation": len(queued),
                     "never_assigned_completed": dict(Counter(r["conclusion"] for r in members
                                                              if not r["assigned"] and r["status"] == "completed")),
                     "oldest_ready": oldest})
    families = defaultdict(list)
    for record in records:
        families[record["family"]].append(record)
    since, until = timestamp(data["window"]["since"]), timestamp(data["window"]["until"])
    # Jobs of runs created before the window are not collected; skip their tail.
    begin = since + timedelta(seconds=QUEUE_OCCUPANCY_WARMUP_SECONDS)
    occupancy = {family: _occupancy(members, begin) for family, members in sorted(families.items())} if begin < until else {}
    outcomes = defaultdict(float)
    conclusions = {run["id"]: run.get("conclusion") or run.get("status") for run in data["runs"]}
    for record in records:
        if record["execution"] is not None:
            outcomes[(record["family"], record["workflow"], record["event"], conclusions[record["run_id"]])] += record["execution"]
    runner_seconds = [{"family": family, "workflow": workflow, "event": event, "run_outcome": outcome, "seconds": seconds}
                      for (family, workflow, event, outcome), seconds in sorted(outcomes.items(), key=lambda item: str(item[0]))]
    throughput = defaultdict(lambda: {"completed": 0, "success": 0, "latency_seconds": []})
    for run in data["runs"]:
        created, updated = timestamp(run.get("created_at")), timestamp(run.get("updated_at"))
        if "created" in run.get("selected_by", []) and run.get("status") == "completed" and created and updated:
            row = throughput[((run.get("path") or "").split("@", 1)[0], run.get("event"))]
            row["completed"] += 1
            if run.get("conclusion") == "success":
                row["success"] += 1
                row["latency_seconds"].append((updated - created).total_seconds())
    hours = (until - since).total_seconds() / 3600.0
    completions = [{"workflow": workflow, "event": event, "completed": row["completed"], "success": row["success"],
                    "success_per_hour": row["success"] / hours,
                    "success_latency_seconds": _percentiles(row["latency_seconds"])}
                   for (workflow, event), row in sorted(throughput.items())]
    return {"schema": 1, "kind": "queue-summary", "repository": data.get("repository"),
            "fetched_at": data.get("fetched_at"), "window": data["window"], "groups": rows,
            "occupancy_by_family": occupancy, "runner_seconds_by_run_outcome": runner_seconds,
            "completions": completions,
            "unknown": ["organization effective concurrency allowance per runner family",
                        "demand from other repositories sharing that allowance",
                        "GitHub hosted-runner fleet provisioning state"],
            "notes": ["Dependency wait = attempt start to job creation; assignment wait = job creation to runner start.",
                      "A job without runner_id/runner_name is unassigned; its started_at is a placeholder, not execution.",
                      "Occupancy starts after a warm-up because runs created before the window are not collected; it counts only this repository's assigned jobs; a flat plateau while jobs wait bounds, "
                      "but does not identify, the effective allowance.",
                      "Success latency uses run updated_at of the latest attempt and includes reruns."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    gather = sub.add_parser("collect")
    gather.add_argument("--repository", default="buster14a/buster")
    gather.add_argument("--branch")
    gather.add_argument("--head-sha")
    gather.add_argument("--workflow", default=".github/workflows/ci.yml")
    gather.add_argument("--limit", type=int, default=20)
    gather.add_argument("--max-pages", type=int, default=5)
    gather.add_argument("--output", required=True)
    gate = sub.add_parser("require-jobs", help="Require all named partitions in the current Actions run")
    gate.add_argument("--repository", default=os.getenv("GITHUB_REPOSITORY"))
    gate.add_argument("--run-id", type=int, default=os.getenv("GITHUB_RUN_ID", "0"))
    gate.add_argument("--run-attempt", type=int, default=os.getenv("GITHUB_RUN_ATTEMPT", "0"))
    gate.add_argument("--output")
    report = sub.add_parser("summarize")
    report.add_argument("input")
    report.add_argument("--output")
    queue = sub.add_parser("queue-collect", help="Record runner assignment for every workflow in a window")
    queue.add_argument("--repository", default="buster14a/buster")
    queue.add_argument("--since", required=True)
    queue.add_argument("--until", required=True)
    queue.add_argument("--output", required=True)
    queue_report = sub.add_parser("queue-summarize")
    queue_report.add_argument("input")
    queue_report.add_argument("--output")
    args = parser.parse_args()
    status = 0
    try:
        if args.command == "collect":
            if not 1 <= args.limit <= 100 or not 1 <= args.max_pages <= 20:
                raise ValueError("Use limit 1..100 and max-pages 1..20")
            data = collect(args)
        elif args.command == "require-jobs":
            data = require_jobs(args)
            status = 0 if data["success"] else 1
        elif args.command == "queue-collect":
            data = queue_collect(args)
        elif args.command == "queue-summarize":
            data = queue_summarize(json.loads(Path(args.input).read_text(encoding="utf-8")))
        else:
            data = summarize(json.loads(Path(args.input).read_text(encoding="utf-8")))
        text = json.dumps(data, indent=2) + "\n"
        if args.output:
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(text, encoding="utf-8")
        else:
            print(text, end="")
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"CI timing failed: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
