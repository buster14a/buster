#!/usr/bin/env python3
"""Control CI recovery and merge-queue fail-fast without running candidate code.

recover() owns eligibility and the single PR rerun request. watch() makes one
bounded pass over live merge-group checks; the workflow invokes it on completion
events and a recovery schedule. Within that pass, the step-deadline policy
(step_deadline_candidates, still_overdue, stop_overdue_steps) independently
stops the desktop Release lanes' workflow-tool step when it stays in progress
beyond its budget; see "Merge-group workflow-tool step deadline" in
docs/ci-cancellation-recovery.md. Only trusted default-branch code mutates runs.
"""

import datetime
import html
import json
import os
from pathlib import Path
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


MAX_ATTEMPTS = 2
MAX_PAGES = 10
WORKFLOW_PATH = ".github/workflows/ci.yml"
OPT_OUT_LABEL = "ci-no-retry"
MAX_QUEUE_REFS = 25
QUEUE_REF_PREFIX = "refs/heads/gh-readonly-queue/main/"
RULESET_ID = 22537199
GITHUB_ACTIONS_APP_ID = 15368
REQUIRED_WORKFLOW_PATHS = {
    "CI complete": "ci.yml",
    "Linux x86-64 bootstrap evidence": "self-host-audit.yml",
    "Canonical TCC bootstrap": "tcc-bootstrap.yml",
    "GPU Linux consumers": "gpu-toolchains.yml",
    "Benchmark service workflow policy": "bench-service-policy.yml",
    "API migration policy": "api-migration-policy.yml",
    "Native retirement merge admission": "api-migration-policy.yml",
    "Main integration admission": "merge-queue-admission.yml",
}
# #1810's short-lived publisher uses this exact-head marker without a workflow
# check suite. A check with the same name and an unrelated marker is ignored.
RECONCILED_CHECK_MARKERS = {"Main integration admission": "buster-merge-queue-admission-v1:"}
ACTIVE_RUN_STATUSES = frozenset(("queued", "pending", "waiting", "requested", "in_progress"))
# #1866: run 36571009755 left this step in progress for over 30 minutes past
# its 5-minute timeout. Budgets mirror the step's ci.yml timeout-minutes for
# each exact desktop Release job name; test_merge_queue_fail_fast.py rejects
# drift. The grace covers runner reporting and cancellation latency. Elapsed
# time starts at GitHub's step start, never at run creation or queue time.
WORKFLOW_TOOLS_STEP = "Workflow tool regression tests"
WORKFLOW_TOOLS_BUDGET_SECONDS = {
    "Linux x86-64 release": 2 * 60,
    "Linux AArch64 release": 2 * 60,
    "macOS x86-64 release": 5 * 60,
    "macOS AArch64 release": 5 * 60,
    "Windows x86-64 release": 5 * 60,
    "Windows AArch64 release": 5 * 60,
}
STEP_DEADLINE_GRACE_SECONDS = 10 * 60
# Passes keep no state, so normal cancellation gets this long, inside the pass
# that requested it, to stop the same overdue step before force-cancel. It must
# leave room for the reads within the watcher job's five-minute timeout.
FORCE_CANCEL_GRACE_SECONDS = 2 * 60
STEP_DEADLINE_POLL_SECONDS = 20


class SkipRecovery(Exception):
    """A normal, observable decision to leave the original result alone."""


def workflow_file(path):
    if not isinstance(path, str):
        return None
    file, separator, ref = path.partition("@")
    return file if file.startswith(".github/workflows/") and (not separator or ref) else None


class GitHub:
    def __init__(self, repository, token):
        self.prefix = "https://api.github.com/repos/" + repository + "/"
        self.token = token

    def request(self, path, *, method="GET", **query):
        url = self.prefix + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        request = urllib.request.Request(url, method=method, headers={
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            body = response.read()
        return json.loads(body) if body else None

    def all(self, path, key=None, **query):
        result = []
        for page in range(1, MAX_PAGES + 1):
            data = self.request(path, per_page=100, page=page, **query)
            rows = data[key] if key else data
            result.extend(rows)
            if len(rows) < 100:
                break
        else:
            raise SkipRecovery("Pagination limit reached; inspect manually.")
        return result

    def cancel(self, run_id, endpoint="cancel"):
        try:
            self.request("actions/runs/" + str(run_id) + "/" + endpoint, method="POST")
            cancelled = True
        except urllib.error.HTTPError as error:
            if error.code != 409:
                raise
            cancelled = False
        return cancelled

    def force_cancel(self, run_id):
        # GitHub documents force-cancel only for runs not responding to cancel.
        return self.cancel(run_id, "force-cancel")


def check_run(run, event_run, repository):
    if (run["id"] != event_run["id"] or
            run["head_sha"] != event_run["head_sha"] or
            run["head_branch"] != event_run["head_branch"] or
            run["workflow_id"] != event_run["workflow_id"] or
            run["head_repository"]["full_name"] != repository["full_name"] or
            run["head_branch"] == repository["default_branch"] or
            workflow_file(run.get("path")) != WORKFLOW_PATH or
            run["event"] not in ("push", "pull_request") or
            run["status"] != "completed" or
            run["conclusion"] not in ("cancelled", "failure") or
            run["run_attempt"] != event_run["run_attempt"] or
            run["run_attempt"] >= MAX_ATTEMPTS):
        raise SkipRecovery("Run is ineligible, already retried, or changed.")


def check_pr(pr, run, repository):
    if (pr["state"] != "open" or
            pr["head"]["repo"]["full_name"] != repository["full_name"] or
            pr["head"]["ref"] != run["head_branch"] or
            pr["head"]["sha"] != run["head_sha"] or
            any(label["name"] == OPT_OUT_LABEL for label in pr["labels"])):
        raise SkipRecovery("PR is closed, superseded, external, or opted out.")


def recover(api, event):
    repository = event["repository"]
    original = event["workflow_run"]
    run_path = "actions/runs/" + str(original["id"])
    run = api.request(run_path)
    check_run(run, original, repository)

    # Push events need a live lookup: their embedded PR list can be empty.
    prs = api.all("pulls", state="open", head=(
        repository["full_name"].split("/")[0] + ":" + run["head_branch"]))
    if len(prs) != 1:
        raise SkipRecovery("No single open PR for this branch.")
    pr = prs[0]
    check_pr(pr, run, repository)

    jobs = api.all(run_path + "/jobs", "jobs", filter="latest")
    cancelled = [job for job in jobs if job["conclusion"] == "cancelled"]
    if not cancelled or any(job["status"] != "completed" for job in jobs):
        raise SkipRecovery("No completed cancellation to recover.")
    # The aggregate intentionally fails when a prerequisite is cancelled.
    # All other failures (including timeouts) require investigation, not retries.
    for job in jobs:
        if (job["conclusion"] not in ("success", "cancelled", "skipped") and
                not (job["name"] == "CI complete" and job["conclusion"] == "failure")):
            raise SkipRecovery("A validation job failed; inspect manually.")

    runs = api.all("actions/workflows/" + str(run["workflow_id"]) + "/runs",
                   "workflow_runs", branch=run["head_branch"])
    if any(other["id"] > run["id"] or
           (other["id"] != run["id"] and other["head_sha"] == run["head_sha"] and
            other["status"] != "completed") for other in runs):
        raise SkipRecovery("A newer or active replacement run exists.")

    # Recheck immediately before the mutation, after all paginated reads.
    check_pr(api.request("pulls/" + str(pr["number"])), run, repository)
    check_run(api.request(run_path), original, repository)
    api.request(run_path + "/rerun-failed-jobs", method="POST")
    return ("Requested attempt 2 for PR #" + str(pr["number"]) + ", run " +
            str(run["id"]) + ", commit " + run["head_sha"] + ". " +
            str(len(cancelled)) + " cancelled jobs; successful jobs are retained.")



def check_watch_run(run, event_run, repository):
    if (run.get("id") != event_run.get("id") or
            run.get("head_sha") != event_run.get("head_sha") or
            run.get("head_branch") != event_run.get("head_branch") or
            not str(run.get("head_branch", "")).startswith("gh-readonly-queue/main/") or
            run.get("workflow_id") != event_run.get("workflow_id") or
            run.get("head_repository", {}).get("full_name") != repository.get("full_name") or
            workflow_file(run.get("path")) != WORKFLOW_PATH or
            run.get("event") != "merge_group" or
            run.get("run_attempt") != event_run.get("run_attempt")):
        raise SkipRecovery("Merge-group watch identity changed; refusing cancellation.")


def required_checks(api, repository):
    ruleset = api.request("rulesets/" + str(RULESET_ID))
    if (ruleset.get("id") != RULESET_ID or ruleset.get("enforcement") != "active" or
            ruleset.get("source_type") != "Repository" or
            ruleset.get("source") != repository.get("full_name") or
            ruleset.get("conditions", {}).get("ref_name", {}).get("include") != ["refs/heads/main"] or
            not any(rule.get("type") == "merge_queue" for rule in ruleset.get("rules", []))):
        raise ValueError("Main merge-queue ruleset is not active.")
    rules = [rule for rule in ruleset.get("rules", []) if rule.get("type") == "required_status_checks"]
    if len(rules) != 1:
        raise ValueError("Expected one required-check rule.")
    rows = rules[0].get("parameters", {}).get("required_status_checks", [])
    names = [row.get("context") for row in rows]
    if (len(names) != len(set(names)) or set(names) != set(REQUIRED_WORKFLOW_PATHS) or
            any(row.get("integration_id") != GITHUB_ACTIONS_APP_ID for row in rows)):
        raise ValueError("Required GitHub Actions check inventory changed.")
    return frozenset(names)


def latest_group_runs(api, head_sha):
    rows = api.all("actions/runs", "workflow_runs", event="merge_group", head_sha=head_sha)
    selected = {}
    for run in rows:
        if run.get("head_sha") != head_sha or run.get("event") != "merge_group":
            raise ValueError("Merge-group run query returned a mismatched source.")
        path = workflow_file(run.get("path"))
        if path is None or not isinstance(run.get("check_suite_id"), int):
            raise ValueError("Merge-group run is missing its workflow or check suite identity.")
        old = selected.get(path)
        if old is None or (run["id"], run["run_attempt"]) > (old["id"], old["run_attempt"]):
            selected[path] = run
    return selected


def required_check_results(api, head_sha, runs, names):
    rows = api.all("commits/" + head_sha + "/check-runs", "check_runs", filter="all")
    selected = {}
    for check in rows:
        name = check.get("name")
        if name not in names:
            continue
        required_run = runs.get(".github/workflows/" + REQUIRED_WORKFLOW_PATHS[name])
        marker = RECONCILED_CHECK_MARKERS.get(name)
        reconciled = marker is not None and check.get("external_id") == marker + head_sha
        if (check.get("head_sha") != head_sha or
                check.get("app", {}).get("id") != GITHUB_ACTIONS_APP_ID or
                (not reconciled and (required_run is None or
                 required_run.get("status") != "completed" or
                 check.get("check_suite", {}).get("id") != required_run["check_suite_id"]))):
            continue
        old = selected.get(name)
        if old is None or check["id"] > old["id"]:
            selected[name] = check
    return selected


def cancel_merge_group_runs(api, head_sha):
    runs = api.all("actions/runs", "workflow_runs", event="merge_group", head_sha=head_sha)
    cancelled = []
    for run in runs:
        if run.get("head_sha") != head_sha or run.get("event") != "merge_group":
            raise ValueError("Merge-group run query returned a mismatched source.")
        if run.get("status") in ACTIVE_RUN_STATUSES and api.cancel(run["id"]):
            cancelled.append(run["id"])
    return cancelled


def live_group_heads(api):
    rows = api.request("git/matching-refs/heads/gh-readonly-queue/main/")
    if not isinstance(rows, list) or len(rows) > MAX_QUEUE_REFS:
        raise ValueError("Merge-group ref inventory is malformed or exceeds the bound.")
    heads = {}
    for row in rows:
        ref, sha = row.get("ref"), row.get("object", {}).get("sha")
        if (not isinstance(ref, str) or not ref.startswith(QUEUE_REF_PREFIX) or
                not ref.removeprefix(QUEUE_REF_PREFIX) or
                not isinstance(sha, str) or len(sha) != 40 or
                any(char not in "0123456789abcdef" for char in sha)):
            raise ValueError("Merge-group ref inventory contains an invalid identity.")
        heads[sha] = ref
    if len(heads) != len(rows):
        raise ValueError("Merge-group ref inventory contains duplicate heads.")
    return heads


def github_time(value):
    """Return epoch seconds for an ISO 8601 timestamp with an explicit offset."""
    seconds = None
    if isinstance(value, str) and "T" in value:
        text = value[:-1] + "+00:00" if value.endswith("Z") else value
        try:
            parsed = datetime.datetime.fromisoformat(text)
        except ValueError:
            parsed = None
        if parsed is not None and parsed.tzinfo is not None:
            seconds = parsed.timestamp()
    return seconds


def utc_text(seconds):
    return datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")


def positive_id(value):
    # bool is an int subclass; GitHub job IDs and step numbers never are.
    return type(value) is int and value > 0


def step_deadline_candidates(run, jobs, now):
    """Split this attempt's in-progress workflow-tool steps into overdue and refused.

    Only an active run qualifies. Queued, waiting and completed jobs or steps
    are never candidates. A step whose job identity, completion metadata or
    start timestamps disagree with the exact attempt is refused rather than
    guessed, as is a job ID listed twice; its job start must not follow its
    step start.
    """
    overdue = []
    refused = []
    job_ids = [job.get("id") for job in jobs]
    if run.get("status") not in ACTIVE_RUN_STATUSES or run.get("conclusion") is not None:
        # A finished run's stale step metadata is never cancellation authority.
        jobs = []
    for job in jobs:
        budget = WORKFLOW_TOOLS_BUDGET_SECONDS.get(job.get("name"))
        steps = job.get("steps")
        if budget is None or job.get("status") != "in_progress" or not isinstance(steps, list):
            continue
        matches = [step for step in steps
                   if isinstance(step, dict) and step.get("name") == WORKFLOW_TOOLS_STEP]
        active = [step for step in matches if step.get("status") == "in_progress"]
        if not active:
            continue
        step = active[0]
        record = {
            "run_id": run.get("id"), "run_attempt": run.get("run_attempt"),
            "head_sha": run.get("head_sha"), "job_id": job.get("id"),
            "job_name": job.get("name"), "step_number": step.get("number"),
            "step_name": WORKFLOW_TOOLS_STEP, "started_at": step.get("started_at"),
            "budget_seconds": budget,
        }
        started = github_time(step.get("started_at"))
        job_started = github_time(job.get("started_at"))
        if (len(matches) != 1 or not positive_id(job.get("id")) or
                job_ids.count(job.get("id")) != 1 or not positive_id(step.get("number")) or
                job.get("run_id") != run.get("id") or
                job.get("run_attempt") != run.get("run_attempt") or
                job.get("head_sha") != run.get("head_sha") or
                job.get("conclusion") is not None or step.get("conclusion") is not None or
                job.get("completed_at") is not None or step.get("completed_at") is not None):
            refused.append(dict(record, reason="job or step identity does not match this attempt"))
        elif started is None or job_started is None or started < job_started:
            refused.append(dict(record, reason="malformed job or step start timestamp"))
        else:
            deadline = started + budget + STEP_DEADLINE_GRACE_SECONDS
            if now > deadline:
                overdue.append(dict(record, deadline=deadline, elapsed_seconds=int(now - started)))
    return overdue, refused


def deadline_identity(record):
    return tuple(str(record[key]) for key in (
        "run_id", "run_attempt", "head_sha", "job_id", "step_number", "started_at"))


def deadline_line(action, record):
    text = ("STEP_DEADLINE_V1 action=" + action + " run=" + str(record["run_id"]) +
            " attempt=" + str(record["run_attempt"]) + " head=" + str(record["head_sha"]) +
            " job=" + str(record["job_id"]) + " job_name=" + json.dumps(record["job_name"]) +
            " step=" + str(record["step_number"]) +
            " step_name=" + json.dumps(record["step_name"]) +
            " started_at=" + json.dumps(record["started_at"]) +
            " budget_seconds=" + str(record["budget_seconds"]) +
            " grace_seconds=" + str(STEP_DEADLINE_GRACE_SECONDS))
    if "deadline" in record:
        text += (" deadline=" + utc_text(record["deadline"]) +
                 " elapsed_seconds=" + str(record["elapsed_seconds"]))
    if "reason" in record:
        text += " reason=" + json.dumps(record["reason"])
    return text


def note(log, line):
    print(line, flush=True)
    log.append(line)


def still_overdue(api, repository, live_refs, original, expected, clock):
    """Re-read the exact attempt and return the expected steps that remain overdue."""
    head_sha = original["head_sha"]
    run_path = "actions/runs/" + str(original["id"])
    if live_group_heads(api).get(head_sha) != live_refs.get(head_sha):
        raise SkipRecovery("Queue ref changed before step-deadline cancellation.")
    latest = latest_group_runs(api, head_sha).get(WORKFLOW_PATH)
    if latest is None or latest.get("id") != original["id"]:
        raise SkipRecovery("Buster CI merge-group run was replaced before step-deadline cancellation.")
    check_watch_run(latest, original, repository)
    # Jobs of this attempt only, never whichever attempt is now latest.
    jobs = api.all(run_path + "/attempts/" + str(original["run_attempt"]) + "/jobs", "jobs")
    # The single run read follows the paginated reads, immediately before mutation.
    run = api.request(run_path)
    check_watch_run(run, original, repository)
    expected_ids = {deadline_identity(record) for record in expected}
    overdue, _refused = step_deadline_candidates(run, jobs, clock())
    return [record for record in overdue if deadline_identity(record) in expected_ids]


def stop_overdue_steps(api, repository, live_refs, run, overdue, cancel_requested, clock,
                       sleep_fn, log):
    """Normal-cancel, wait for, then force-cancel revalidated overdue steps in one pass.

    Passes keep no state, so the bounded force-cancel grace elapses inside the
    pass that requested normal cancellation, or saw fail-fast request it. Each
    mutation follows a fresh still_overdue() read of the exact attempt, and its
    evidence is flushed before the POST. A changed identity is recorded rather
    than raised, so a fail-fast result from the same pass survives.
    """
    disposition = None
    stuck = []
    try:
        stuck = still_overdue(api, repository, live_refs, run, overdue, clock)
        if not stuck:
            # Without a prior fail-fast cancel, the step progressed on its own.
            disposition = "step-stopped" if cancel_requested else "refused-progressed"
            for record in overdue:
                note(log, deadline_line(disposition, record))
        elif not cancel_requested:
            # An uncertain POST still leaves the exact identity in the log and
            # summary. Transport errors are never retried.
            for record in stuck:
                note(log, deadline_line("cancel-requesting", record))
            cancel_requested = api.cancel(run["id"])
        if stuck:
            disposition = "cancel-requested" if cancel_requested else "run-already-finished"
            for record in stuck:
                note(log, deadline_line(disposition, record))
        waited = 0
        while stuck and cancel_requested and waited < FORCE_CANCEL_GRACE_SECONDS:
            sleep_fn(STEP_DEADLINE_POLL_SECONDS)
            waited += STEP_DEADLINE_POLL_SECONDS
            current = still_overdue(api, repository, live_refs, run, stuck, clock)
            if not current:
                disposition = "step-stopped"
                for record in stuck:
                    note(log, deadline_line(disposition, record))
            stuck = current
        if stuck and cancel_requested:
            # The loop's final still_overdue() read immediately precedes this.
            for record in stuck:
                note(log, deadline_line("force-cancel-requesting", record))
            forced = api.force_cancel(run["id"])
            disposition = "force-cancel-requested" if forced else "run-already-finished"
            for record in stuck:
                note(log, deadline_line(disposition, record))
    except SkipRecovery as changed:
        disposition = "refused-changed"
        for record in stuck or overdue:
            note(log, deadline_line(disposition, dict(record, reason=str(changed))))
    return ("Step deadline " + disposition + " for Buster CI run " + str(run["id"]) +
            " attempt " + str(run["run_attempt"]) + ": " +
            ", ".join(record["job_name"] + " job " + str(record["job_id"])
                      for record in overdue) + ".")


def watch_head(api, repository, head_sha, live_refs, original=None, clock=time.time,
               sleep_fn=time.sleep, log=None):
    log = [] if log is None else log
    if head_sha not in live_refs:
        raise SkipRecovery("Merge-group head is no longer a live queue ref.")
    names = required_checks(api, repository)
    runs = latest_group_runs(api, head_sha)
    run = runs.get(WORKFLOW_PATH)
    if run is None:
        raise SkipRecovery("No Buster CI run for this live merge group.")
    check_watch_run(run, original if original is not None else run, repository)
    if "refs/heads/" + run["head_branch"] != live_refs[head_sha]:
        raise SkipRecovery("Buster CI queue ref does not match the live head.")
    jobs = api.all("actions/runs/" + str(run["id"]) + "/jobs", "jobs", filter="latest")
    failed = []
    for job in jobs:
        status, conclusion = job.get("status"), job.get("conclusion")
        if status == "completed":
            if conclusion is None:
                raise ValueError("Completed CI job has no conclusion.")
            if conclusion != "success":
                failed.append(job.get("name", "unnamed"))
        elif conclusion is not None:
            raise ValueError("Incomplete CI job already has a conclusion.")
    # Discovery only reads; stop_overdue_steps() revalidates before mutating.
    overdue, refused = step_deadline_candidates(run, jobs, clock())
    for record in refused:
        note(log, deadline_line("refused", record))
    checks = required_check_results(api, head_sha, runs, names)
    bad = sorted(name for name, check in checks.items()
                 if check.get("status") == "completed" and check.get("conclusion") != "success")
    if failed or bad or (run.get("status") == "completed" and run.get("conclusion") != "success"):
        # A completion event may have become stale while we read jobs/checks.
        # Re-read the queue ref and latest attempt immediately before mutation.
        if live_group_heads(api).get(head_sha) != live_refs[head_sha]:
            raise SkipRecovery("Queue ref changed before cancellation.")
        current_runs = latest_group_runs(api, head_sha)
        if {path: (item["id"], item["run_attempt"]) for path, item in current_runs.items()} != {
                path: (item["id"], item["run_attempt"]) for path, item in runs.items()}:
            raise SkipRecovery("A merge-group workflow attempt changed before cancellation.")
        latest = current_runs.get(WORKFLOW_PATH)
        check_watch_run(latest or {}, run, repository)
        if bad:
            current_checks = required_check_results(api, head_sha, current_runs, names)
            if any(current_checks.get(name, {}).get("status") != "completed" or
                   current_checks[name].get("conclusion") == "success" or
                   current_checks[name].get("id") != checks[name]["id"] for name in bad):
                raise SkipRecovery("A required check changed before cancellation.")
        if failed:
            current_jobs = api.all("actions/runs/" + str(run["id"]) + "/jobs", "jobs",
                                   filter="latest")
            if current_jobs != jobs:
                raise SkipRecovery("Buster CI jobs changed before cancellation.")
        cancelled = cancel_merge_group_runs(api, head_sha)
        reason = ", ".join(sorted(set(failed + bad)))
        if not reason:
            reason = "Buster CI conclusion " + str(run.get("conclusion"))
        message = ("Merge-group fail-fast observed " + reason + "; requested cancellation of " +
                   str(len(cancelled)) + " exact-head run(s): " +
                   ", ".join(str(run_id) for run_id in cancelled))
        if overdue:
            # Fail-fast's normal cancel may not stop a hung step; escalate it
            # here so a later pass is not left with the same stuck run.
            message += " " + stop_overdue_steps(api, repository, live_refs, run, overdue,
                                                run["id"] in cancelled, clock, sleep_fn, log)
        return message
    if names.issubset(checks) and all(checks[name].get("status") == "completed" and
                                     checks[name].get("conclusion") == "success" for name in names):
        return "All required merge-group checks completed successfully; no cancellation requested."
    if overdue:
        # The cancelled jobs' completion event or the next sweep then runs
        # fail-fast for the rest of the exact-head group.
        return stop_overdue_steps(api, repository, live_refs, run, overdue, False, clock,
                                  sleep_fn, log)
    return "Required checks remain pending; the next event or sweep will recheck."


def watch(api, event, event_name="workflow_run", clock=time.time, sleep_fn=time.sleep,
          log=None):
    log = [] if log is None else log
    repository = event["repository"]
    refs = live_group_heads(api)
    if event_name == "workflow_run":
        original = event["workflow_run"]
        if (event.get("action") not in ("in_progress", "completed") or
                original.get("event") != "merge_group" or
                original.get("head_repository", {}).get("full_name") != repository["full_name"] or
                workflow_file(original.get("path")) not in {
                    WORKFLOW_PATH, *(".github/workflows/" + p
                                     for p in REQUIRED_WORKFLOW_PATHS.values())}):
            raise SkipRecovery("Watcher accepts only required merge-group workflow deliveries.")
        expected = original if workflow_file(original["path"]) == WORKFLOW_PATH else None
        return watch_head(api, repository, original["head_sha"], refs, expected, clock,
                          sleep_fn, log)
    if event_name not in ("schedule", "workflow_dispatch"):
        raise SkipRecovery("Watcher accepts only completion, dispatch or sweep events.")
    messages = []
    for head in refs:
        try:
            messages.append(head + ": " + watch_head(api, repository, head, refs, None, clock,
                                                     sleep_fn, log))
        except SkipRecovery as skipped:
            messages.append(head + ": " + str(skipped))
    return "\n".join(messages) if messages else "No live merge groups."


def lifecycle_summary(title, message, event, repository, handler_sha, handler_id,
                      handler_attempt, records=()):
    def code(value):
        return "<code>" + html.escape(str(value)) + "</code>"

    handler_url = ("https://github.com/" + repository + "/actions/runs/" +
                   str(handler_id) + "/attempts/" + str(handler_attempt))
    if "workflow_run" in event:
        source = event["workflow_run"]
        source_url = ("https://github.com/" + repository + "/actions/runs/" +
                      str(source["id"]) + "/attempts/" + str(source["run_attempt"]))
        upstream = ("Upstream: [" + html.escape(str(source.get("name", "Buster CI"))) +
                    " run " + str(source["id"]) + " attempt " +
                    str(source["run_attempt"]) + "](" + source_url + "), " +
                    code(event["action"]) + " notification for " +
                    code(source["event"]) + " on " + code(source["head_branch"]) +
                    " at " + code(source["head_sha"]) + ".\n\n")
    else:
        upstream = "Trigger: " + code(event.get("schedule", "manual sweep")) + ".\n\n"
    return ("## " + title + "\n\n" + upstream +
            "Trusted handler: [run " + str(handler_id) + " attempt " +
            str(handler_attempt) + "](" + handler_url + ") on default-branch " +
            code(handler_sha) + ".\n\n" +
            "Decision: " + html.escape(message) + "\n" +
            "".join(("\nController records:\n\n" if index == 0 else "") +
                    "- " + code(line) + "\n" for index, line in enumerate(records)))


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    api = GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"])
    if event["repository"]["full_name"] != os.environ["GITHUB_REPOSITORY"]:
        raise ValueError("Event repository mismatch")
    mode = sys.argv[1] if len(sys.argv) > 1 else "recover"
    # Records of mutations already requested survive a later controller error.
    records = []
    title = "CI lifecycle controller error"
    message = "The controller raised an error; the job log has the traceback."
    try:
        if mode == "watch":
            message = watch(api, event, os.environ["GITHUB_EVENT_NAME"], log=records)
            title = "Merge-group CI fail-fast"
        elif mode == "recover":
            message = recover(api, event)
            title = "Cancelled CI recovery"
        else:
            raise ValueError("Expected recover or watch mode")
    except SkipRecovery as skipped:
        message = "No action: " + str(skipped)
        title = "CI lifecycle controller"
    finally:
        print(message)
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
            summary.write(lifecycle_summary(
                title, message, event, os.environ["GITHUB_REPOSITORY"],
                os.environ["GITHUB_SHA"], os.environ["GITHUB_RUN_ID"],
                os.environ["GITHUB_RUN_ATTEMPT"], records))


if __name__ == "__main__":
    main()
