#!/usr/bin/env python3
"""Freeze a same-repository PR comparison off the benchmark runner.

This reader produces an immutable request record only. It cannot install source,
approve an environment, or dispatch the protected workflow.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections.abc import Callable
from typing import Any

REPOSITORY = "buster14a/buster"
RECIPE = "compiler-throughput-pr-v1"
WORKLOAD_POLICY = "ordinary-ci-default-six-frozen-baseline-self-host-v1"
HEX = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")
KEY = re.compile(r"[A-Za-z0-9._-]{1,64}\Z")


class ResolutionError(ValueError):
    """An input or observed GitHub identity is unsuitable for admission."""


def _sha(value: Any, label: str, width: int | None = None) -> str:
    if not isinstance(value, str) or not HEX.fullmatch(value) or (width and len(value) != width):
        raise ResolutionError(f"invalid {label}")
    return value


def _pull_identity(pull: Any, number: int) -> tuple[str, str]:
    if not isinstance(pull, dict) or pull.get("number") != number or pull.get("state") != "open":
        raise ResolutionError("PR is missing, closed, or changed")
    base = pull.get("base")
    head = pull.get("head")
    if not isinstance(base, dict) or not isinstance(head, dict):
        raise ResolutionError("PR source identity is missing")
    if not isinstance(base.get("repo"), dict) or base["repo"].get("full_name") != REPOSITORY:
        raise ResolutionError("PR base repository is not the approved repository")
    if not isinstance(head.get("repo"), dict) or head["repo"].get("full_name") != REPOSITORY:
        raise ResolutionError("fork or deleted head repository is not admitted")
    base_sha = _sha(base.get("sha"), "PR base commit")
    head_sha = _sha(head.get("sha"), "PR head commit", len(base_sha))
    if base.get("ref") != "main":
        raise ResolutionError("PR does not target protected main")
    return base_sha, head_sha


def _commit_tree(fetch: Callable[[str], Any], sha: str) -> str:
    commit = fetch(f"repos/{REPOSITORY}/commits/{sha}")
    if not isinstance(commit, dict) or commit.get("sha") != sha:
        raise ResolutionError("exact commit object is unavailable or substituted")
    data = commit.get("commit")
    tree = data.get("tree") if isinstance(data, dict) else None
    return _sha(tree.get("sha") if isinstance(tree, dict) else None, "commit tree", len(sha))


def resolve_pr(
    fetch: Callable[[str], Any], number: int, key: str, baseline: str | None = None
) -> dict[str, Any]:
    if not isinstance(number, int) or not 1 <= number <= 2**31 - 1:
        raise ResolutionError("invalid PR number")
    if not KEY.fullmatch(key):
        raise ResolutionError("invalid idempotency key")
    if baseline is not None:
        _sha(baseline, "explicit baseline commit")

    pull_path = f"repos/{REPOSITORY}/pulls/{number}"
    first_base, first_head = _pull_identity(fetch(pull_path), number)
    base = baseline if baseline is not None else first_base
    _sha(base, "baseline commit", len(first_head))
    base_tree = _commit_tree(fetch, base)
    head_tree = _commit_tree(fetch, first_head)
    second_base, second_head = _pull_identity(fetch(pull_path), number)
    if (first_base, first_head) != (second_base, second_head):
        raise ResolutionError("PR base or head changed during resolution; retry with a new key")

    record: dict[str, Any] = {
        "schema": "BQ-PR-REQUEST-V1",
        "kind": "exploratory-pr-head",
        "repository": REPOSITORY,
        "pull_request": number,
        "pr_base_commit": first_base,
        "candidate_commit": first_head,
        "candidate_tree": head_tree,
        "baseline_commit": base,
        "baseline_tree": base_tree,
        "recipe": RECIPE,
        "workload_policy": WORKLOAD_POLICY,
        "idempotency_key": key,
        "source_admission": "required",
        "dispatch_ready": False,
    }
    canonical = json.dumps(record, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    record["request_sha256"] = hashlib.sha256(canonical.encode("ascii")).hexdigest()
    return record


def _gh_fetch(path: str) -> Any:
    try:
        response = subprocess.run(
            ["gh", "api", path], check=True, capture_output=True, text=True, timeout=30
        )
        return json.loads(response.stdout)
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        raise ResolutionError(f"GitHub metadata request failed for {path}: {error}") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=REPOSITORY)
    parser.add_argument("--pr", type=int, required=True)
    parser.add_argument("--idempotency-key", required=True)
    parser.add_argument("--baseline-commit", help="exact commit; defaults to the PR base SHA frozen here")
    args = parser.parse_args()
    try:
        if args.repository != REPOSITORY:
            raise ResolutionError("repository is not the approved repository")
        record = resolve_pr(_gh_fetch, args.pr, args.idempotency_key, args.baseline_commit)
        print(json.dumps(record, sort_keys=True, indent=2))
        return 0
    except ResolutionError as error:
        print(f"request resolution failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
