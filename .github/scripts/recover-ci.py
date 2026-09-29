#!/usr/bin/env python3
"""Control CI recovery and merge-queue fail-fast without running candidate code.

recover() owns eligibility and the single PR rerun request. watch() observes
the live required checks for one merge group, plus Buster CI jobs before its
aggregate finishes. The step watchdog bounds stalled workflow-tools jobs from
this independent observer; see docs/ci-stalled-step-watchdog.md. Only the
trusted default-branch workflow mutates runs.
"""

from datetime import datetime
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
# Match ci.yml's workflow_tools step budget. The independent observer allows
# two further minutes for timeout cleanup and metadata propagation (#1866).
WORKFLOW_TOOLS_STEP = "Workflow tool regression tests"
WORKFLOW_TOOLS_TIMEOUT_SECONDS = {
    "Linux x86-64 release": 120,
    "Linux AArch64 release": 120,
    "macOS x86-64 release": 300,
    "macOS AArch64 release": 300,
    "Windows x86-64 release": 300,
    "Windows AArch64 release": 300,
}
STEP_REPORTING_GRACE_SECONDS = 120
STEP_CANCEL_GRACE_SECONDS = 120
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

    def cancel(self, run_id, *, force=False):
        endpoint = "force-cancel" if force else "cancel"
        try:
            self.request("actions/runs/" + str(run_id) + "/" + endpoint, method="POST")
            cancelled = True
        except urllib.error.HTTPError as error:
            if error.code != 409:
                raise
            cancelled = False
        return cancelled


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


def stalled_tool_steps(run, jobs, now):
    """Select only explicitly budgeted running steps; queued time is irrelevant."""
    stalled = []
    if run.get("status") == "in_progress" and run.get("conclusion") is None:
        for job in jobs:
            budget = WORKFLOW_TOOLS_TIMEOUT_SECONDS.get(job.get("name"))
            if budget is None or job.get("status") != "in_progress":
                continue
            for step in job.get("steps", []):
                if (step.get("name") != WORKFLOW_TOOLS_STEP or
                        step.get("status") != "in_progress"):
                    continue
                if (type(job.get("id")) is not int or job["id"] <= 0 or
                        job.get("run_id") != run["id"] or
                        job.get("head_sha") != run["head_sha"] or
                        job.get("conclusion") is not None or
                        job.get("completed_at") is not None or
                        type(step.get("number")) is not int or step["number"] <= 0 or
                        step.get("conclusion") is not None or
                        step.get("completed_at") is not None):
                    raise ValueError("Invalid running workflow-tools identity.")
                started = utc_timestamp(step.get("started_at"))
                job_started = utc_timestamp(job.get("started_at"))
                if started < job_started:
                    raise ValueError("Workflow-tools step predates its job.")
                elapsed = now - started
                deadline = budget + STEP_REPORTING_GRACE_SECONDS
                if elapsed >= deadline:
                    stalled.append({
                        "run_id": run["id"], "attempt": run["run_attempt"],
                        "head_sha": run["head_sha"], "job_id": job["id"],
                        "job": job["name"], "step": step["name"],
                        "number": step["number"], "started_at": step["started_at"],
                        "budget_seconds": budget, "deadline_seconds": deadline,
                        "elapsed_seconds": int(elapsed),
                    })
    identities = [stalled_step_identity(step) for step in stalled]
    if len(identities) != len(set(identities)):
        raise ValueError("Duplicate running workflow-tools identity.")
    return stalled


def utc_timestamp(value):
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError("Expected a GitHub UTC start timestamp.")
    return datetime.fromisoformat(value[:-1] + "+00:00").timestamp()


def stalled_step_identity(step):
    return tuple(step[key] for key in (
        "run_id", "attempt", "head_sha", "job_id", "job", "step", "number", "started_at"))


def confirm_stalled_step(api, event, expected, now_fn):
    """Never cancel from the discovery snapshot or a previous job attempt."""
    original = event["workflow_run"]
    run = latest_group_runs(api, original["head_sha"]).get(WORKFLOW_PATH)
    if run is None:
        raise SkipRecovery("Buster CI merge-group run disappeared.")
    check_watch_run(run, original, event["repository"])
    confirmed = None
    if run.get("status") == "in_progress" and run.get("conclusion") is None:
        path = "actions/runs/" + str(run["id"])
        jobs = api.all(path + "/attempts/" + str(run["run_attempt"]) + "/jobs", "jobs")
        matches = [step for step in stalled_tool_steps(run, jobs, now_fn())
                   if stalled_step_identity(step) == stalled_step_identity(expected)]
        fresh = api.request(path)
        check_watch_run(fresh, original, event["repository"])
        if (matches and fresh.get("status") == "in_progress" and
                fresh.get("conclusion") is None):
            confirmed = matches[0]
    return confirmed


def stop_stalled_step(api, event, expected, sleep_fn, now_fn):
    """Normal cancel, then one bounded, identity-revalidated escalation."""
    confirmed = confirm_stalled_step(api, event, expected, now_fn)
    message = None
    if confirmed is not None:
        evidence = json.dumps(confirmed, sort_keys=True)
        # Flush before the write: even a timeout/uncertain API response retains
        # the triggering identity. Never retry a POST on a transport error.
        print("CI_STEP_TIMEOUT_V1 " + evidence, flush=True)
        accepted = api.cancel(confirmed["run_id"])
        if not accepted:
            disposition = "normal cancellation conflicted; no force cancellation requested"
        else:
            print("CI_STEP_TIMEOUT_CANCEL_V1 normal=requested " + evidence, flush=True)
            sleep_fn(STEP_CANCEL_GRACE_SECONDS)
            try:
                still_stalled = confirm_stalled_step(api, event, confirmed, now_fn)
                if still_stalled is None:
                    disposition = "normal cancellation requested; step/run progressed or completed; no force cancellation"
                elif api.cancel(confirmed["run_id"], force=True):
                    disposition = "normal and force cancellation requested; terminal result not yet verified"
                else:
                    disposition = "normal cancellation requested; force cancellation conflicted"
            except SkipRecovery as changed:
                disposition = "normal cancellation requested; no force cancellation: " + str(changed)
        message = "Merge-group step watchdog: " + evidence + "; " + disposition + "."
        print("CI_STEP_TIMEOUT_DISPOSITION_V1 " + message, flush=True)
    return message


def watch(api, event, sleep_fn=time.sleep, max_probes=WATCH_PROBES, now_fn=time.time):
    repository = event["repository"]
    original = event["workflow_run"]
    if event.get("action") != "in_progress":
        raise SkipRecovery("Watcher accepts only the in-progress workflow_run delivery.")
    if original.get("event") != "merge_group":
        raise SkipRecovery("Watcher accepts only merge-group Buster CI runs.")
    names = required_checks(api, repository)
    run_path = "actions/runs/" + str(original["id"])
    for probe in range(max_probes):
        runs = latest_group_runs(api, original["head_sha"])
        run = runs.get(WORKFLOW_PATH)
        if run is None or run.get("id") != original["id"]:
            raise SkipRecovery("Buster CI merge-group run was superseded or disappeared.")
        check_watch_run(run, original, repository)
        jobs = api.all(run_path + "/jobs", "jobs", filter="latest")
        stalled = stalled_tool_steps(run, jobs, now_fn())
        if stalled:
            message = stop_stalled_step(api, event, stalled[0], sleep_fn, now_fn)
            if message is not None:
                return message
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
        checks = required_check_results(api, run["head_sha"], runs, names)
        bad = sorted(name for name, check in checks.items()
                     if check.get("status") == "completed" and check.get("conclusion") != "success")
        if failed or bad or (run.get("status") == "completed" and run.get("conclusion") != "success"):
            cancelled = cancel_merge_group_runs(api, run["head_sha"])
            reason = ", ".join(sorted(set(failed + bad)))
            if not reason:
                reason = "Buster CI conclusion " + str(run.get("conclusion"))
            return ("Merge-group fail-fast observed " + reason + "; requested cancellation of " +
                    str(len(cancelled)) + " exact-head run(s): " +
                    ", ".join(str(run_id) for run_id in cancelled))
        if names.issubset(checks) and all(checks[name].get("status") == "completed" and
                                         checks[name].get("conclusion") == "success" for name in names):
            return "All required merge-group checks completed successfully; no cancellation requested."
        if probe + 1 < max_probes:
            sleep_fn(WATCH_SECONDS)
    raise TimeoutError("Merge-group fail-fast watcher exceeded its bounded polling window.")


def lifecycle_summary(title, message, event, repository, handler_sha, handler_id,
                      handler_attempt):
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
            "Decision: " + html.escape(message) + "\n")


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    api = GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"])
    if event["repository"]["full_name"] != os.environ["GITHUB_REPOSITORY"]:
        raise ValueError("Event repository mismatch")
    mode = sys.argv[1] if len(sys.argv) > 1 else "recover"
    try:
        if mode == "watch":
            message = watch(api, event)
            title = "Buster CI merge-group watcher"
        elif mode == "recover":
            message = recover(api, event)
            title = "Buster CI recovery decision"
        else:
            raise ValueError("Expected recover or watch mode")
    except SkipRecovery as skipped:
        message = "No action: " + str(skipped)
        title = "Buster CI lifecycle no action"
    print(message)
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
        summary.write(lifecycle_summary(
            title, message, event, os.environ["GITHUB_REPOSITORY"],
            os.environ["GITHUB_SHA"], os.environ["GITHUB_RUN_ID"],
            os.environ["GITHUB_RUN_ATTEMPT"]))


if __name__ == "__main__":
    main()
