#!/usr/bin/env python3
"""Authorize one 9700X compiler comparison of a main merge group (#2752).

Run from trusted `main` by the hosted `authorize-compiler` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run` when `.github/workflows/9700x-compiler-request.yml` completes for
a `merge_group` event. Nothing in the payload is trusted: this re-reads the
request run, the live queue ref, the synthetic group commit and the one pull
request it admits, and fails closed unless they agree.

The group commit must have exactly two parents: the queue base first, the pull
request head second. Measuring the head against that first parent pairs the
candidate with its immediate predecessor even while the predecessor is still a
speculative group; admission later requires the first parent to be main.

The candidate's own build runs on the host, so the existing trust boundary of
the direct path applies unchanged: the pull request must be authored by the
owner (login and numeric ID) from a branch of this repository. GitHub's queue
events carry no pull-request author; the author comes from the pull request.
"""

from __future__ import annotations

import os
import re
import sys
import urllib.parse

from authorize import COMMIT, DECIMAL, MAINTAINER, REPOSITORY, fetch, full_name, identity

REQUEST_WORKFLOW = ".github/workflows/9700x-compiler-request.yml"
QUEUE_BRANCH = re.compile(r"gh-readonly-queue/main/pr-([1-9][0-9]{0,8})-[0-9a-f]{40}")


def queue_pull(branch: str) -> int | None:
    """The pull request number named by a main queue branch, or None."""
    match = QUEUE_BRANCH.fullmatch(branch) if isinstance(branch, str) else None
    return int(match.group(1)) if match else None


def tree(commit: object) -> object:
    record = commit.get("commit") if isinstance(commit, dict) else None
    record = record.get("tree") if isinstance(record, dict) else None
    return record.get("sha") if isinstance(record, dict) else None


def verify(repository: str, run_id: int, head: str, branch: str, run: object, ref: object,
           group: object, base_commit: object, pull: object) -> tuple[list[str], dict]:
    """Return the failed checks and the measurement identity."""
    failures: list[str] = []
    result: dict = {}
    if not isinstance(run, dict):
        run = {}
        failures.append("request run record")
    for name, holds in (
        ("request run id", type(run.get("id")) is int and run.get("id") == run_id),
        ("request workflow", run.get("path") == REQUEST_WORKFLOW),
        ("request event", run.get("event") == "merge_group"),
        ("request conclusion", run.get("status") == "completed" and run.get("conclusion") == "success"),
        ("request head commit", run.get("head_sha") == head),
        ("request queue branch", run.get("head_branch") == branch and queue_pull(branch) is not None),
        ("request repository", full_name(run.get("repository")) == repository),
        ("request head repository", full_name(run.get("head_repository")) == repository),
    ):
        if not holds:
            failures.append(name)
    live = ref.get("object") if isinstance(ref, dict) else None
    if not (isinstance(ref, dict) and ref.get("ref") == "refs/heads/" + branch and isinstance(live, dict)
            and live.get("sha") == head):
        failures.append("live queue ref names the head")
    parents = group.get("parents") if isinstance(group, dict) else None
    parents = [parent.get("sha") for parent in parents if isinstance(parent, dict)] \
        if isinstance(parents, list) else []
    if not (isinstance(group, dict) and group.get("sha") == head and len(parents) == 2
            and all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in parents)):
        failures.append("group commit has the queue base and one pull request as parents")
        parents = ["", ""]
    base, pull_head = parents
    if not (isinstance(base_commit, dict) and base_commit.get("sha") == base):
        failures.append("group base commit")
    head_tree, base_tree = tree(group), tree(base_commit)
    if not all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in (head_tree, base_tree)):
        failures.append("group and base trees")
    if not isinstance(pull, dict):
        pull = {}
        failures.append("pull request record")
    pull_head_record = pull.get("head") if isinstance(pull.get("head"), dict) else {}
    pull_base_record = pull.get("base") if isinstance(pull.get("base"), dict) else {}
    for name, holds in (
        ("pull request number", type(pull.get("number")) is int and pull.get("number") == queue_pull(branch)),
        ("pull request open", pull.get("state") == "open"),
        ("pull request author", identity(pull.get("user")) == MAINTAINER),
        ("pull request head repository", full_name(pull_head_record.get("repo")) == repository),
        ("pull request base repository", full_name(pull_base_record.get("repo")) == repository),
        ("pull request targets main", pull_base_record.get("ref") == "main"),
        ("pull request head is the second parent", bool(pull_head) and pull_head_record.get("sha") == pull_head),
    ):
        if not holds:
            failures.append(name)
    if not failures:
        result = {"base": base, "base_tree": base_tree, "head_tree": head_tree,
                  "pull": str(pull["number"]), "pull_head": pull_head}
    return failures, result


def main() -> int:
    environment = os.environ
    repository = environment.get("BQ_REPOSITORY", "")
    run_id = environment.get("BQ_REQUEST_RUN_ID", "")
    head = environment.get("BQ_HEAD_COMMIT", "")
    branch = environment.get("BQ_HEAD_BRANCH", "")
    attempt = environment.get("BQ_RUN_ATTEMPT", "")
    token = environment.get("GH_TOKEN", "")
    output = environment.get("GITHUB_OUTPUT", "")
    failures: list[str] = []
    result: dict = {}
    if not (REPOSITORY.fullmatch(repository) and DECIMAL.fullmatch(run_id) and COMMIT.fullmatch(head)
            and queue_pull(branch) is not None and DECIMAL.fullmatch(attempt) and token and output):
        failures.append("workflow inputs")
    else:
        prefix = f"/repos/{repository}"
        run = fetch(f"{prefix}/actions/runs/{run_id}", token)
        ref = fetch(f"{prefix}/git/ref/heads/{urllib.parse.quote(branch, safe='/')}", token)
        group = fetch(f"{prefix}/commits/{head}", token)
        parents = group.get("parents") if isinstance(group, dict) else None
        base = parents[0].get("sha") if isinstance(parents, list) and parents and isinstance(parents[0], dict) else ""
        base_commit = fetch(f"{prefix}/commits/{base}", token) if isinstance(base, str) and COMMIT.fullmatch(base) else None
        pull = fetch(f"{prefix}/pulls/{queue_pull(branch)}", token)
        failures, result = verify(repository, int(run_id), head, branch, run, ref, group, base_commit, pull)
    if failures:
        print("BENCH_COMPILER_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={result['base']}\nbase_tree={result['base_tree']}\n"
                         f"head_tree={result['head_tree']}\npull={result['pull']}\npull_head={result['pull_head']}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
