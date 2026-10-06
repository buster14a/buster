#!/usr/bin/env python3
"""Authorize one 9700X compiler comparison of a commit on main (#2752).

Run from trusted `main` by the hosted `authorize-compiler` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run` when `.github/workflows/9700x-compiler-request.yml` completes for
a `push` to main. Nothing in the payload is trusted: this re-reads the request
run, the pushed commit, its first-parent chain, the baseline and main itself,
and fails closed unless they agree.

The commit is measured against its first parent, the main commit it landed
on, unless that commit has no valid main measurement: then against the
nearest first-parent ancestor, at most compiler_github.RECONCILE_DEPTH back,
that has one (choose_base), so the commits a merge burst left unmeasured are
inside a measured range. Without such an ancestor in reach, or when the check
listing cannot be read, the first parent is the baseline. The baseline must
be on the commit's first-parent chain (verify), so it is always main's own
code. The commit must still be on main (main equals it or descends from it),
so a force-moved or foreign ref is never measured. A queue merge commit's second
parent and pull request are recorded for the report; main is trusted code,
so no author gate applies: whatever landed on main, including bot-authored
pull requests, is measured.
"""

from __future__ import annotations

import os
import sys
import urllib.parse

from authorize import COMMIT, DECIMAL, REPOSITORY, fetch, full_name
from compiler_github import GITHUB_ACTIONS_APP_ID, measured, parse_chain
from compiler_receipt import check_name

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


def choose_base(chain: list[str], checks: dict) -> str:
    """The nearest commit of chain with a valid main measurement in checks (sha -> rows), else chain[0]."""
    found = [sha for sha in chain if any(measured(row, sha) for row in checks.get(sha, []))]
    return found[0] if found else chain[0]


def verify(repository: str, run_id: int, head: str, run: object, commit: object, base_commit: object,
           on_main: object, pulls: object, chain: list[str] | None = None) -> tuple[list[str], dict]:
    """Return the failed checks and the measurement identity.

    base_commit is the chosen baseline's record; chain is head's first-parent
    chain, nearest first (default: the first parent alone). The baseline must
    be on it, and range counts the first-parent commits it spans.
    """
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
    first_parent = parents[0]
    pull_head = parents[1] if len(parents) == 2 else head
    chain = chain if chain and chain[0] == first_parent else [first_parent]
    base = base_commit.get("sha") if isinstance(base_commit, dict) else None
    if not (isinstance(base, str) and base in chain):
        failures.append("baseline commit is on the first-parent chain")
    head_tree, base_tree = tree(commit), tree(base_commit)
    if not all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in (head_tree, base_tree)):
        failures.append("commit and first parent trees")
    if not (isinstance(on_main, dict) and on_main.get("status") in ("identical", "ahead")):
        failures.append("commit is still on main")
    if not failures:
        result = {"base": base, "base_tree": base_tree, "head_tree": head_tree,
                  "pull": merged_pull(head, pulls), "pull_head": pull_head,
                  "first_parent": first_parent, "range": str(chain.index(base) + 1)}
    return failures, result


def main_chain(prefix: str, token: str, head: str, commit: object) -> tuple[list[str], str]:
    """(head's first-parent chain, chosen baseline) from GitHub's records; ([], '') without a first parent."""
    parents = commit.get("parents") if isinstance(commit, dict) else None
    first = parents[0].get("sha") if isinstance(parents, list) and parents and isinstance(parents[0], dict) else ""
    chain: list[str] = []
    base = ""
    if isinstance(first, str) and COMMIT.fullmatch(first):
        chain, base = [first], first
        try:
            listed = parse_chain(fetch(f"{prefix}/commits?sha={head}&per_page=100", token), head)
            chain = listed if listed and listed[0] == first else chain
            query = urllib.parse.urlencode({"check_name": check_name("main"), "filter": "all",
                                            "app_id": GITHUB_ACTIONS_APP_ID, "per_page": 100})
            checks: dict = {}
            for sha in chain:
                rows = fetch(f"{prefix}/commits/{sha}/check-runs?{query}", token)
                checks[sha] = rows.get("check_runs", []) if isinstance(rows, dict) else []
                if any(measured(row, sha) for row in checks[sha]):
                    break
            base = choose_base(chain, checks)
        except (OSError, ValueError) as error:
            # Losing the range never refuses the measurement: the first parent is measured.
            chain, base = [first], first
            print(f"BENCH_COMPILER_RANGE_UNAVAILABLE {error}; measuring against the first parent", file=sys.stderr)
    return chain, base


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
        chain, base = main_chain(prefix, token, head, commit)
        base_commit = fetch(f"{prefix}/commits/{base}", token) if isinstance(base, str) and COMMIT.fullmatch(base) else None
        # status "ahead"/"identical": main descends from (or is) the commit.
        on_main = fetch(f"{prefix}/compare/{head}...main", token)
        pulls = fetch(f"{prefix}/commits/{head}/pulls?per_page=10", token)
        failures, result = verify(repository, int(run_id), head, run, commit, base_commit, on_main, pulls, chain)
    if failures:
        print("BENCH_COMPILER_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={result['base']}\nbase_tree={result['base_tree']}\n"
                         f"head_tree={result['head_tree']}\npull={result['pull']}\npull_head={result['pull_head']}\n"
                         f"first_parent={result['first_parent']}\nrange={result['range']}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
