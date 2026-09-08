#!/usr/bin/env python3
"""Recover one interrupted PR validation, without running PR code.

recover() owns eligibility and the single rerun request. GitHub owns bounded
REST pagination. Only the default-branch workflow executes this with a token.
"""

import json
import os
from pathlib import Path
import urllib.parse
import urllib.request


MAX_ATTEMPTS = 2
MAX_PAGES = 10
WORKFLOW_PATH = ".github/workflows/ci.yml"
OPT_OUT_LABEL = "ci-no-retry"


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


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    api = GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"])
    if event["repository"]["full_name"] != os.environ["GITHUB_REPOSITORY"]:
        raise ValueError("Event repository mismatch")
    try:
        message = recover(api, event)
    except SkipRecovery as skipped:
        message = "No retry: " + str(skipped)
    print(message)
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
        summary.write("## Cancelled CI recovery\n\n" + message + "\n")


if __name__ == "__main__":
    main()
