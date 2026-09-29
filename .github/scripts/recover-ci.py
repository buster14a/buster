#!/usr/bin/env python3
"""Control CI recovery and merge-queue fail-fast without running candidate code.

recover() owns eligibility and the single PR rerun request. watch() observes
the live required checks for one merge group, plus Buster CI jobs before its
aggregate finishes. Its step-deadline policy (step_deadline_candidates,
request_step_deadline_cancel, escalate_step_deadline) independently stops the
desktop Release lanes' workflow-tool step when it stays in progress beyond its
budget. Only the trusted default-branch workflow mutates runs.
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
WATCH_PROBES = 600
WATCH_SECONDS = 30
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
# Normal cancellation gets this long to stop the same overdue step before the
# documented force-cancel endpoint is used for that exact attempt.
FORCE_CANCEL_GRACE_SECONDS = 5 * 60


class SkipRecovery(Exception):
    """A normal, observable decision to leave the original result alone."""


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
            run["path"] != WORKFLOW_PATH or
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
            run.get("path") != WORKFLOW_PATH or
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
        path = run.get("path")
        if not isinstance(path, str) or not isinstance(run.get("check_suite_id"), int):
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
        if (check.get("head_sha") != head_sha or
                check.get("app", {}).get("id") != GITHUB_ACTIONS_APP_ID or
                required_run is None or
                check.get("check_suite", {}).get("id") != required_run["check_suite_id"]):
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


def step_deadline_candidates(run, jobs, now):
    """Split this attempt's in-progress workflow-tool steps into overdue and refused.

    Queued, waiting and completed jobs or steps are never candidates. A step
    whose job identity or start timestamps disagree with the exact attempt is
    refused rather than guessed; its job start must not follow its step start.
    """
    overdue = []
    refused = []
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
        if (len(matches) != 1 or not isinstance(job.get("id"), int) or
                not isinstance(step.get("number"), int) or
                job.get("run_id") != run.get("id") or
                job.get("run_attempt") != run.get("run_attempt") or
                job.get("head_sha") != run.get("head_sha") or
                job.get("conclusion") is not None or step.get("conclusion") is not None):
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
    text = ("STEP_DEADLINE action=" + action + " run=" + str(record["run_id"]) +
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


def still_overdue(api, original, repository, expected, clock):
    """Re-read the exact attempt and return the expected steps that remain overdue."""
    run_path = "actions/runs/" + str(original["id"])
    latest = latest_group_runs(api, original["head_sha"]).get(WORKFLOW_PATH)
    if latest is None or latest.get("id") != original["id"]:
        raise SkipRecovery("Buster CI merge-group run was replaced before step-deadline cancellation.")
    jobs = api.all(run_path + "/jobs", "jobs", filter="latest")
    # The single run read follows the paginated reads, immediately before mutation.
    run = api.request(run_path)
    check_watch_run(run, original, repository)
    remaining = []
    if run.get("status") != "completed":
        expected_ids = {deadline_identity(record) for record in expected}
        overdue, _refused = step_deadline_candidates(run, jobs, clock())
        remaining = [record for record in overdue if deadline_identity(record) in expected_ids]
    return remaining


def request_step_deadline_cancel(api, original, repository, overdue, clock, log):
    """Request normal cancellation of the exact attempt holding revalidated overdue steps."""
    state = None
    remaining = still_overdue(api, original, repository, overdue, clock)
    if not remaining:
        for record in overdue:
            note(log, deadline_line("refused-progressed", record))
    else:
        action = "cancel-requested" if api.cancel(original["id"]) else "run-already-finished"
        for record in remaining:
            note(log, deadline_line(action, record))
        state = {"records": remaining, "requested_at": clock(), "disposition": action}
    return state


def escalate_step_deadline(api, original, repository, state, run, jobs, clock, log):
    """Force-cancel only when normal cancellation left the same overdue step running."""
    expected_ids = {deadline_identity(record) for record in state["records"]}
    now = clock()
    overdue, _refused = step_deadline_candidates(run, jobs, now)
    stuck = [record for record in overdue if deadline_identity(record) in expected_ids]
    if stuck and now - state["requested_at"] >= FORCE_CANCEL_GRACE_SECONDS:
        stuck = still_overdue(api, original, repository, stuck, clock)
        if stuck:
            forced = api.force_cancel(original["id"])
            state["disposition"] = "force-cancel-requested" if forced else "run-already-finished"
            for record in stuck:
                note(log, deadline_line(state["disposition"], record))
    if not stuck:
        state["disposition"] = "step-stopped"
        for record in state["records"]:
            note(log, deadline_line("step-stopped", record))


def deadline_summary(state):
    text = ""
    if state is not None:
        text = (" Step deadline " + state["disposition"] + " for Buster CI run " +
                str(state["records"][0]["run_id"]) + " attempt " +
                str(state["records"][0]["run_attempt"]) + ": " +
                ", ".join(record["job_name"] + " job " + str(record["job_id"])
                          for record in state["records"]) + ".")
    return text


def watch(api, event, sleep_fn=time.sleep, max_probes=WATCH_PROBES, clock=time.time, log=None):
    log = [] if log is None else log
    repository = event["repository"]
    original = event["workflow_run"]
    if event.get("action") != "in_progress":
        raise SkipRecovery("Watcher accepts only the in-progress workflow_run delivery.")
    if original.get("event") != "merge_group":
        raise SkipRecovery("Watcher accepts only merge-group Buster CI runs.")
    names = required_checks(api, repository)
    run_path = "actions/runs/" + str(original["id"])
    deadline = None
    refusals = set()
    fail_fast = None
    for probe in range(max_probes):
        runs = latest_group_runs(api, original["head_sha"])
        run = runs.get(WORKFLOW_PATH)
        if run is None or run.get("id") != original["id"]:
            raise SkipRecovery("Buster CI merge-group run was superseded or disappeared.")
        check_watch_run(run, original, repository)
        jobs = api.all(run_path + "/jobs", "jobs", filter="latest")
        failed = []
        for job in jobs:
            status = job.get("status")
            conclusion = job.get("conclusion")
            if status == "completed":
                if conclusion is None:
                    raise ValueError("Completed CI job has no conclusion.")
                if conclusion != "success":
                    failed.append(job.get("name", "unnamed"))
            elif conclusion is not None:
                raise ValueError("Incomplete CI job already has a conclusion.")
        if deadline is None:
            overdue, refused = step_deadline_candidates(run, jobs, clock())
            for record in refused:
                if deadline_identity(record) + (record["reason"],) not in refusals:
                    refusals.add(deadline_identity(record) + (record["reason"],))
                    note(log, deadline_line("refused", record))
            if overdue:
                deadline = request_step_deadline_cancel(api, original, repository, overdue,
                                                        clock, log)
        elif deadline["disposition"] == "cancel-requested":
            escalate_step_deadline(api, original, repository, deadline, run, jobs, clock, log)
        checks = required_check_results(api, run["head_sha"], runs, names)
        bad = sorted(name for name, check in checks.items()
                     if check.get("status") == "completed" and check.get("conclusion") != "success")
        if failed or bad or (run.get("status") == "completed" and run.get("conclusion") != "success"):
            if fail_fast is None:
                cancelled = cancel_merge_group_runs(api, run["head_sha"])
                reason = ", ".join(sorted(set(failed + bad)))
                if not reason:
                    reason = "Buster CI conclusion " + str(run.get("conclusion"))
                fail_fast = ("Merge-group fail-fast observed " + reason +
                             "; requested cancellation of " + str(len(cancelled)) +
                             " exact-head run(s): " +
                             ", ".join(str(run_id) for run_id in cancelled))
            # Keep observing a still-running overdue step until it stops or
            # its bounded force-cancel escalation has been requested.
            if deadline is None or deadline["disposition"] != "cancel-requested":
                return fail_fast + deadline_summary(deadline)
        elif names.issubset(checks) and all(checks[name].get("status") == "completed" and
                                           checks[name].get("conclusion") == "success"
                                           for name in names):
            return ("All required merge-group checks completed successfully; no cancellation "
                    "requested." + deadline_summary(deadline))
        if probe + 1 < max_probes:
            sleep_fn(WATCH_SECONDS)
    raise TimeoutError("Merge-group fail-fast watcher exceeded its bounded polling window.")


def lifecycle_summary(title, message, event, repository, handler_sha, handler_id,
                      handler_attempt, records=()):
    source = event["workflow_run"]

    def code(value):
        return "<code>" + html.escape(str(value)) + "</code>"

    source_url = ("https://github.com/" + repository + "/actions/runs/" +
                  str(source["id"]) + "/attempts/" + str(source["run_attempt"]))
    handler_url = ("https://github.com/" + repository + "/actions/runs/" +
                   str(handler_id) + "/attempts/" + str(handler_attempt))
    return ("## " + title + "\n\n" +
            "Upstream: [Buster CI run " + str(source["id"]) + " attempt " +
            str(source["run_attempt"]) + "](" + source_url + "), " +
            code(event["action"]) + " notification for " +
            code(source["event"]) + " on " + code(source["head_branch"]) +
            " at " + code(source["head_sha"]) + ".\n\n" +
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
    title = "Buster CI lifecycle controller error"
    message = "The controller raised an error; the job log has the traceback."
    try:
        if mode == "watch":
            message = watch(api, event, log=records)
            title = "Buster CI merge-group watcher"
        elif mode == "recover":
            message = recover(api, event)
            title = "Buster CI recovery decision"
        else:
            raise ValueError("Expected recover or watch mode")
    except SkipRecovery as skipped:
        message = "No action: " + str(skipped)
        title = "Buster CI lifecycle no action"
    finally:
        print(message)
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
            summary.write(lifecycle_summary(
                title, message, event, os.environ["GITHUB_REPOSITORY"],
                os.environ["GITHUB_SHA"], os.environ["GITHUB_RUN_ID"],
                os.environ["GITHUB_RUN_ATTEMPT"], records))


if __name__ == "__main__":
    main()
