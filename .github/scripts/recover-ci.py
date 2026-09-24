#!/usr/bin/env python3
"""Control CI recovery and merge-queue fail-fast without running candidate code.

recover() owns eligibility and the single PR rerun request. watch() observes
the live required checks for one merge group, plus Buster CI jobs before its
aggregate finishes. Only the trusted default-branch workflow mutates runs.
"""

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

    def cancel(self, run_id):
        try:
            self.request("actions/runs/" + str(run_id) + "/cancel", method="POST")
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


def watch(api, event, sleep_fn=time.sleep, max_probes=WATCH_PROBES):
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


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    api = GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"])
    if event["repository"]["full_name"] != os.environ["GITHUB_REPOSITORY"]:
        raise ValueError("Event repository mismatch")
    mode = sys.argv[1] if len(sys.argv) > 1 else "recover"
    try:
        if mode == "watch":
            message = watch(api, event)
            title = "Merge-group CI fail-fast"
        elif mode == "recover":
            message = recover(api, event)
            title = "Cancelled CI recovery"
        else:
            raise ValueError("Expected recover or watch mode")
    except SkipRecovery as skipped:
        message = "No action: " + str(skipped)
        title = "CI lifecycle controller"
    print(message)
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
        summary.write("## " + title + "\n\n" + message + "\n")


if __name__ == "__main__":
    main()
