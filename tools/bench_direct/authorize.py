#!/usr/bin/env python3
"""Authorize one direct 9700X workload run from GitHub's own records (#2704).

Run from trusted `main` by the hosted `authorize` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run`, so its event payload describes the request workflow's run.
This re-reads that run and the pull request for its head commit through the
API and fails closed unless both name the owner. It emits the current run
attempt and the pull request's base commit only after every check holds.

It also reads the pull request's changed files (plan) and says what was
requested: workloads (the selection contract of workload_selection.py) and a
compiler comparison (an added or modified COMPARE_REQUEST, #2769, or
SCALING_REQUEST, #424, whose scaling leg runs inside the comparison). For a
comparison it resolves the merge base with the base branch and both trees from
GitHub's records, so the host job and the publisher bind the same identities.
"""

from __future__ import annotations

import json
import os
import re
import sys
import urllib.parse
import urllib.request

from workload_selection import api_changes, select

MAINTAINER = {"login": "davidgmbb", "id": 39247043}
REQUEST_WORKFLOW = ".github/workflows/9700x-direct-request.yml"
COMMIT = re.compile(r"[0-9a-f]{40}")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
DECIMAL = re.compile(r"[1-9][0-9]*")
API = "https://api.github.com"
COMPARE_REQUEST = "benchmarks/9700x/compiler-compare.request"
# Kept equal to compiler_receipt.SCALING_REQUEST; this file imports nothing local.
SCALING_REQUEST = "benchmarks/9700x/scaling.request"
FILE_PAGES = 30
FILE_PAGE_SIZE = 100
# GitHub's compare endpoint never returns more than 300 files.
COMPARE_FILE_LIMIT = 300


def identity(record: object) -> dict | None:
    """Login and numeric ID of an API user object, or None when malformed."""
    result = None
    if isinstance(record, dict) and type(record.get("id")) is int:
        result = {"login": record.get("login"), "id": record.get("id")}
    return result


def full_name(record: object) -> object:
    return record.get("full_name") if isinstance(record, dict) else None


def plan(files: object) -> tuple[bool, bool, list[str]]:
    """(workloads, compare, refusals) requested by the changed files.

    Workload selection is shared with the executor (workload_selection, #2924);
    any refusal means no host work may start.
    """
    rows = [row for row in (files if isinstance(files, list) else [])
            if isinstance(row, dict) and isinstance(row.get("filename"), str) and isinstance(row.get("status"), str)]
    workloads, problems = select(api_changes(rows))
    compare = any(row["filename"] in (COMPARE_REQUEST, SCALING_REQUEST) and row["status"] != "removed"
                  for row in rows)
    return bool(workloads), compare, problems


def inventory(fetch_page, expected: object) -> tuple[list[dict], list[str]]:
    """The pull request's complete changed-file rows, or why they are incomplete (#2939).

    `fetch_page(n)` returns page n. Complete means every page and row was well
    formed, no file repeated, and the row count equals the pull request's own
    `changed_files`; a short or empty page, an exhausted page budget and a
    count mismatch are all failures, never an empty or partial plan.
    """
    files: list[dict] = []
    failures: list[str] = []
    seen: set[str] = set()
    if type(expected) is not int or expected < 0:
        failures.append("changed-file count of the pull request")
    for page in range(1, FILE_PAGES + 1) if not failures else ():
        rows = fetch_page(page)
        if not isinstance(rows, list):
            failures.append(f"changed-file page {page} is malformed")
            break
        for row in rows:
            name = row.get("filename") if isinstance(row, dict) else None
            status = row.get("status") if isinstance(row, dict) else None
            previous = row.get("previous_filename") if isinstance(row, dict) else None
            if not isinstance(name, str) or not name or not isinstance(status, str) or \
                    (previous is not None and not isinstance(previous, str)) or \
                    (status == "renamed" and not previous):
                failures.append(f"changed-file page {page} has a malformed row")
                break
            if name in seen:
                failures.append(f"changed-file inventory repeats {name}")
                break
            seen.add(name)
            files.append(row)
        if failures or len(files) >= expected or len(rows) < FILE_PAGE_SIZE:
            break
    if not failures and len(files) != expected:
        failures.append(f"changed-file inventory has {len(files)} of {expected} files "
                        f"(page budget {FILE_PAGES} x {FILE_PAGE_SIZE})")
    return files, failures


def request_delta(head: str, commit: object, comparisons: object) -> tuple[list[dict], list[str]]:
    """Fresh changed files at head, relative to every parent (#3087).

    A merged request inherited unchanged from either parent is not an
    affirmative request for this candidate. The API's capped diff cannot
    prove a complete plan at its limit, so that case fails closed.
    """
    failures: list[str] = []
    result: list[dict] = []
    parents = commit.get("parents") if isinstance(commit, dict) else None
    parents = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
    if not (isinstance(commit, dict) and commit.get("sha") == head and 1 <= len(parents) <= 2
            and len(parents) == len(commit["parents"])
            and all(isinstance(sha, str) and COMMIT.fullmatch(sha) and sha != head for sha in parents)
            and len(set(parents)) == len(parents)):
        failures.append("request head and parent identities")
    if not isinstance(comparisons, list) or len(comparisons) != len(parents):
        failures.append("request head-parent comparisons")
    common: set[str] | None = None
    for parent, compared in zip(parents, comparisons if isinstance(comparisons, list) else []) if not failures else ():
        if not (isinstance(compared, dict) and compared.get("status") == "ahead"
                and isinstance(compared.get("base_commit"), dict) and compared["base_commit"].get("sha") == parent
                and isinstance(compared.get("merge_base_commit"), dict)
                and compared["merge_base_commit"].get("sha") == parent):
            failures.append("request comparison parent identity")
            break
        rows = compared.get("files")
        if not isinstance(rows, list) or len(rows) >= COMPARE_FILE_LIMIT:
            failures.append("request comparison file inventory is malformed or capped")
            break
        files, problems = inventory(
            lambda page: rows[(page - 1) * FILE_PAGE_SIZE:page * FILE_PAGE_SIZE], len(rows))
        failures.extend(problems)
        if failures:
            break
        names = {row["filename"] for row in files}
        if common is None:
            result, common = files, names
        else:
            common &= names
    result = [row for row in result if row["filename"] in common] if not failures and common is not None else []
    return result, failures


def comparison(head: str, compared: object, head_commit: object) -> tuple[list[str], dict]:
    """The merge base and both trees of a requested compiler comparison."""
    failures: list[str] = []
    base = compared.get("merge_base_commit") if isinstance(compared, dict) else None
    base_sha = base.get("sha") if isinstance(base, dict) else None
    base_tree = base.get("commit", {}).get("tree", {}).get("sha") if isinstance(base, dict) else None
    head_tree = head_commit.get("commit", {}).get("tree", {}).get("sha") if isinstance(head_commit, dict) else None
    for name, holds in (
        ("comparison merge base", isinstance(base_sha, str) and bool(COMMIT.fullmatch(base_sha)) and base_sha != head),
        ("comparison base tree", isinstance(base_tree, str) and bool(COMMIT.fullmatch(base_tree))),
        ("comparison head commit", isinstance(head_commit, dict) and head_commit.get("sha") == head),
        ("comparison head tree", isinstance(head_tree, str) and bool(COMMIT.fullmatch(head_tree))),
    ):
        if not holds:
            failures.append(name)
    return failures, ({} if failures else {"merge_base": base_sha, "merge_base_tree": base_tree, "head_tree": head_tree})


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
    number = next(pull["number"] for pull in pulls if isinstance(pull, dict) and pull.get("state") == "open"
                  and isinstance(pull.get("head"), dict) and pull["head"].get("sha") == head) if not failures else 0
    files: list = []
    if not failures:
        detail = fetch(f"/repos/{repository}/pulls/{number}", token)
        if not (isinstance(detail, dict) and detail.get("number") == number
                and isinstance(detail.get("head"), dict) and detail["head"].get("sha") == head):
            failures.append("pull request record for the head commit")
        else:
            files, problems = inventory(
                lambda page: fetch(f"/repos/{repository}/pulls/{number}/files?per_page={FILE_PAGE_SIZE}&page={page}",
                                   token), detail.get("changed_files"))
            failures.extend(problems)
    workloads, compare, problems = plan(files if not failures else [])
    failures.extend(problems)
    request_files: list[dict] = []
    if not failures and (workloads or compare):
        request_commit = fetch(f"/repos/{repository}/commits/{head}", token)
        parents = request_commit.get("parents", []) if isinstance(request_commit, dict) else []
        parents = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
        compared_parents = [
            fetch(f"/repos/{repository}/compare/{parent}...{head}", token)
            for parent in parents[:2] if isinstance(parent, str) and COMMIT.fullmatch(parent)]
        delta, problems = request_delta(head, request_commit, compared_parents)
        failures.extend(problems)
        changed = {row["filename"] for row in delta}
        request_files = [row for row in files if row["filename"] in changed]
        fresh_workloads, fresh_compare, problems = plan(request_files)
        failures.extend(problems)
        workloads, compare = workloads and fresh_workloads, compare and fresh_compare
    extra = {"merge_base": "", "merge_base_tree": "", "head_tree": ""}
    if not failures and compare:
        compared = fetch(f"/repos/{repository}/compare/{urllib.parse.quote(base)}...{head}", token)
        problems, extra = comparison(head, compared, fetch(f"/repos/{repository}/commits/{head}", token))
        failures.extend(problems)
    if failures:
        print("BENCH_DIRECT_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        paths = [row["filename"] for row in request_files
                 if row["filename"] in (COMPARE_REQUEST, SCALING_REQUEST)
                 or re.fullmatch(r"benchmarks/9700x/[^/]+\.(c|data)", row["filename"])]
        print(f"BENCH_DIRECT_REQUEST head={head} files={json.dumps(paths)} "
              f"workloads={str(workloads).lower()} compare={str(compare).lower()}")
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={base}\npull={number}\nworkloads={str(workloads).lower()}\n"
                         f"request_head={head}\ncompare={str(compare).lower()}\nmerge_base={extra['merge_base']}\n"
                         f"merge_base_tree={extra['merge_base_tree']}\nhead_tree={extra['head_tree']}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
