#!/usr/bin/env python3
"""Authorize one direct 9700X workload run from GitHub's own records (#2704).

Run from trusted `main` by the hosted `authorize` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run`, so its event payload describes the request workflow's run.
This re-reads that run and the pull request for its head commit through the
API and fails closed unless both name the owner. It emits the current run
attempt and the pull request's base commit only after every check holds.
"""

from __future__ import annotations

import json
import os
import re
import sys
import urllib.request

MAINTAINER = {"login": "davidgmbb", "id": 39247043}
REQUEST_WORKFLOW = ".github/workflows/9700x-direct-request.yml"
COMMIT = re.compile(r"[0-9a-f]{40}")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
DECIMAL = re.compile(r"[1-9][0-9]*")
API = "https://api.github.com"


def identity(record: object) -> dict | None:
    """Login and numeric ID of an API user object, or None when malformed."""
    result = None
    if isinstance(record, dict) and type(record.get("id")) is int:
        result = {"login": record.get("login"), "id": record.get("id")}
    return result


def full_name(record: object) -> object:
    return record.get("full_name") if isinstance(record, dict) else None


def verify(repository: str, run_id: int, head: str, run: object, pulls: object) -> tuple[list[str], str]:
    """Return the failed checks and the pull request's base commit."""
    failures: list[str] = []
    base = ""
    if not isinstance(run, dict):
        run = {}
        failures.append("request run record")
    for name, holds in (
        ("request run id", type(run.get("id")) is int and run.get("id") == run_id),
        ("request workflow", run.get("path") == REQUEST_WORKFLOW),
        ("request event", run.get("event") == "pull_request"),
        ("request conclusion", run.get("status") == "completed" and run.get("conclusion") == "success"),
        ("request head commit", run.get("head_sha") == head),
        ("request repository", full_name(run.get("repository")) == repository),
        ("request head repository", full_name(run.get("head_repository")) == repository),
        ("request actor", identity(run.get("actor")) == MAINTAINER),
        ("request triggering actor", identity(run.get("triggering_actor")) == MAINTAINER),
    ):
        if not holds:
            failures.append(name)
    matching = [pull for pull in (pulls if isinstance(pulls, list) else [])
                if isinstance(pull, dict) and pull.get("state") == "open"
                and isinstance(pull.get("head"), dict) and pull["head"].get("sha") == head]
    if len(matching) != 1:
        failures.append("exactly one open pull request for the head commit")
    else:
        pull = matching[0]
        base_record = pull.get("base") if isinstance(pull.get("base"), dict) else {}
        candidate = base_record.get("sha")
        for name, holds in (
            ("pull request author", identity(pull.get("user")) == MAINTAINER),
            ("pull request head repository", full_name(pull["head"].get("repo")) == repository),
            ("pull request base repository", full_name(base_record.get("repo")) == repository),
            ("pull request base commit", isinstance(candidate, str) and bool(COMMIT.fullmatch(candidate))),
        ):
            if not holds:
                failures.append(name)
        if not failures:
            base = candidate
    return failures, base


def fetch(path: str, token: str) -> object:
    request = urllib.request.Request(API + path, headers={
        "Authorization": "Bearer " + token,
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def main() -> int:
    environment = os.environ
    repository = environment.get("BQ_REPOSITORY", "")
    run_id = environment.get("BQ_REQUEST_RUN_ID", "")
    head = environment.get("BQ_HEAD_COMMIT", "")
    attempt = environment.get("BQ_RUN_ATTEMPT", "")
    token = environment.get("GH_TOKEN", "")
    output = environment.get("GITHUB_OUTPUT", "")
    failures: list[str] = []
    base = ""
    if not (REPOSITORY.fullmatch(repository) and DECIMAL.fullmatch(run_id) and COMMIT.fullmatch(head)
            and DECIMAL.fullmatch(attempt) and token and output):
        failures.append("workflow inputs")
    else:
        run = fetch(f"/repos/{repository}/actions/runs/{run_id}", token)
        pulls = fetch(f"/repos/{repository}/commits/{head}/pulls?per_page=100", token)
        failures, base = verify(repository, int(run_id), head, run, pulls)
    if failures:
        print("BENCH_DIRECT_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={base}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
