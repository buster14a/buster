#!/usr/bin/env python3
"""Authorize one 9700X compiler comparison of a commit on main (#2752).

Run from trusted `main` by the hosted `authorize-compiler` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run` when `.github/workflows/9700x-compiler-request.yml` completes for
a `push` to main. Nothing in the payload is trusted: this re-reads the request
run, the pushed commit, its first parent and main itself, and fails closed
unless they agree.

The commit is measured against its first parent, the main commit it landed
on. It must still be on main (main equals it or descends from it), so a
force-moved or foreign ref is never measured. A queue merge commit's second
parent and pull request are recorded for the report; main is trusted code,
so no author gate applies: whatever landed on main, including bot-authored
pull requests, is measured.
"""

from __future__ import annotations

import os
import sys

from authorize import COMMIT, DECIMAL, REPOSITORY, fetch, full_name

REQUEST_WORKFLOW = ".github/workflows/9700x-compiler-request.yml"


def tree(commit: object) -> object:
    record = commit.get("commit") if isinstance(commit, dict) else None
    record = record.get("tree") if isinstance(record, dict) else None
    return record.get("sha") if isinstance(record, dict) else None


def merged_pull(head: str, pulls: object) -> str:
    """The pull request whose merge produced head, or "0" for a direct push."""
    numbers = [pull.get("number") for pull in (pulls if isinstance(pulls, list) else [])
               if isinstance(pull, dict) and pull.get("merge_commit_sha") == head and type(pull.get("number")) is int]
    return str(numbers[0]) if len(numbers) == 1 else "0"


def verify(repository: str, run_id: int, head: str, run: object, commit: object, base_commit: object,
           on_main: object, pulls: object) -> tuple[list[str], dict]:
    """Return the failed checks and the measurement identity."""
    failures: list[str] = []
    result: dict = {}
    if not isinstance(run, dict):
        run = {}
        failures.append("request run record")
    for name, holds in (
        ("request run id", type(run.get("id")) is int and run.get("id") == run_id),
        ("request workflow", run.get("path") == REQUEST_WORKFLOW),
        ("request event", run.get("event") == "push"),
        ("request branch", run.get("head_branch") == "main"),
        ("request conclusion", run.get("status") == "completed" and run.get("conclusion") == "success"),
        ("request head commit", run.get("head_sha") == head),
        ("request repository", full_name(run.get("repository")) == repository),
        ("request head repository", full_name(run.get("head_repository")) == repository),
    ):
        if not holds:
            failures.append(name)
    parents = commit.get("parents") if isinstance(commit, dict) else None
    parents = [parent.get("sha") for parent in parents if isinstance(parent, dict)] if isinstance(parents, list) else []
    if not (isinstance(commit, dict) and commit.get("sha") == head and 1 <= len(parents) <= 2
            and all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in parents)):
        failures.append("commit has a first parent on main")
        parents = [""]
    base = parents[0]
    pull_head = parents[1] if len(parents) == 2 else head
    if not (isinstance(base_commit, dict) and base_commit.get("sha") == base):
        failures.append("first parent commit")
    head_tree, base_tree = tree(commit), tree(base_commit)
    if not all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in (head_tree, base_tree)):
        failures.append("commit and first parent trees")
    if not (isinstance(on_main, dict) and on_main.get("status") in ("identical", "ahead")):
        failures.append("commit is still on main")
    if not failures:
        result = {"base": base, "base_tree": base_tree, "head_tree": head_tree,
                  "pull": merged_pull(head, pulls), "pull_head": pull_head}
    return failures, result


def main() -> int:
    environment = os.environ
    repository = environment.get("BQ_REPOSITORY", "")
    run_id = environment.get("BQ_REQUEST_RUN_ID", "")
    head = environment.get("BQ_HEAD_COMMIT", "")
    attempt = environment.get("BQ_RUN_ATTEMPT", "")
    token = environment.get("GH_TOKEN", "")
    output = environment.get("GITHUB_OUTPUT", "")
    failures: list[str] = []
    result: dict = {}
    if not (REPOSITORY.fullmatch(repository) and DECIMAL.fullmatch(run_id) and COMMIT.fullmatch(head)
            and DECIMAL.fullmatch(attempt) and token and output):
        failures.append("workflow inputs")
    else:
        prefix = f"/repos/{repository}"
        run = fetch(f"{prefix}/actions/runs/{run_id}", token)
        commit = fetch(f"{prefix}/commits/{head}", token)
        parents = commit.get("parents") if isinstance(commit, dict) else None
        base = parents[0].get("sha") if isinstance(parents, list) and parents and isinstance(parents[0], dict) else ""
        base_commit = fetch(f"{prefix}/commits/{base}", token) if isinstance(base, str) and COMMIT.fullmatch(base) else None
        # status "ahead"/"identical": main descends from (or is) the commit.
        on_main = fetch(f"{prefix}/compare/{head}...main", token)
        pulls = fetch(f"{prefix}/commits/{head}/pulls?per_page=10", token)
        failures, result = verify(repository, int(run_id), head, run, commit, base_commit, on_main, pulls)
    if failures:
        print("BENCH_COMPILER_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={result['base']}\nbase_tree={result['base_tree']}\n"
                         f"head_tree={result['head_tree']}\npull={result['pull']}\npull_head={result['pull_head']}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
