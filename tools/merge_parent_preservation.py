#!/usr/bin/env python3
"""Detect newly introduced merges that discard clean second-parent changes."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


KNOWN_ISSUE_1041_MERGE = "718ee0064bdc3513499784e7522ebad5e39bc29c"
ZERO_SHA = re.compile(r"^0+$")


class MergePreservationError(RuntimeError):
    pass


def run_git(repository: Path, arguments: list[str], *, input_bytes: bytes | None = None,
            environment: dict[str, str] | None = None, check: bool = True) -> subprocess.CompletedProcess[bytes]:
    env = os.environ.copy()
    if environment:
        env.update(environment)
    try:
        result = subprocess.run(
            ["git", "-C", str(repository), *arguments],
            input=input_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            timeout=60,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise MergePreservationError(f"git {' '.join(arguments)} exceeded 60 seconds") from error
    if check and result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise MergePreservationError(f"git {' '.join(arguments)} failed: {detail}")
    return result


def git_text(repository: Path, *arguments: str, environment: dict[str, str] | None = None) -> str:
    return run_git(repository, list(arguments), environment=environment).stdout.decode("utf-8", errors="replace").strip()


def resolve_commit(repository: Path, revision: str) -> str:
    return git_text(repository, "rev-parse", "--verify", f"{revision}^{{commit}}")


def commit_details(repository: Path, revision: str) -> tuple[str, list[str]]:
    output = git_text(repository, "show", "-s", "--format=%T%n%P", revision)
    fields = output.split("\n", 1)
    tree = fields[0]
    parents = fields[1].split() if len(fields) > 1 else []
    return tree, parents


def is_ancestor(repository: Path, ancestor: str, descendant: str) -> bool:
    return run_git(repository, ["merge-base", "--is-ancestor", ancestor, descendant], check=False).returncode == 0


def resolve_range_base(repository: Path, candidate: str, requested_base: str | None,
                       event_name: str) -> tuple[str | None, list[str]]:
    _tree, parents = commit_details(repository, candidate)
    base = requested_base.strip() if requested_base else ""
    if base and not ZERO_SHA.fullmatch(base):
        base = resolve_commit(repository, base)
    else:
        base = ""

    if event_name in ("pull_request", "merge_group"):
        if not base:
            raise MergePreservationError(f"{event_name} is missing its exact base SHA")
        if len(parents) != 2:
            raise MergePreservationError(f"{event_name} candidate must be a two-parent merge commit")
        if parents[0] != base:
            raise MergePreservationError(f"{event_name} candidate first parent does not equal the event base SHA")
    elif not base:
        # Manual dispatches and new tag pushes have no useful before-SHA. Limit
        # those runs to the candidate commit itself instead of scanning all
        # history that was already on the ref.
        base = parents[0] if parents else ""

    if base:
        if not is_ancestor(repository, base, candidate):
            raise MergePreservationError("the requested candidate base is not an ancestor of the candidate")
        return base, parents
    return None, parents


def merge_commits_in_range(repository: Path, candidate: str, base: str | None) -> list[str]:
    arguments = ["rev-list", "--reverse", "--topo-order", "--merges", candidate]
    if base:
        arguments.append(f"^{base}")
    output = git_text(repository, *arguments)
    return output.splitlines() if output else []


def read_patch(repository: Path, commit: str, parents: list[str]) -> bytes:
    if len(parents) != 1:
        return b""
    return run_git(repository, [
        "diff", "--binary", "--full-index", "--no-ext-diff", "--no-renames",
        parents[0], commit, "--",
    ]).stdout


def apply_check(repository: Path, patch: bytes, index_path: Path, *, reverse: bool) -> bool:
    environment = {"GIT_INDEX_FILE": str(index_path)}
    arguments = ["apply", "--cached", "--check"]
    if reverse:
        arguments.append("--reverse")
    arguments.append("-")
    return run_git(repository, arguments, input_bytes=patch, environment=environment, check=False).returncode == 0


def inspect_merge(repository: Path, merge_sha: str) -> dict[str, object] | None:
    merge_tree, parents = commit_details(repository, merge_sha)
    if len(parents) != 2:
        return None
    first_parent, second_parent = parents
    first_tree, _ = commit_details(repository, first_parent)
    if merge_tree != first_tree:
        return None
    second_tree, _ = commit_details(repository, second_parent)
    if second_tree == merge_tree:
        return None

    merge_bases = git_text(repository, "merge-base", "--all", first_parent, second_parent).splitlines()
    if len(merge_bases) != 1:
        return {
            "merge": merge_sha,
            "first_parent": first_parent,
            "second_parent": second_parent,
            "kind": "unclassified",
            "reason": "merge has no unique merge base",
        }
    merge_base = merge_bases[0]
    base_tree, _ = commit_details(repository, merge_base)
    if second_tree == base_tree:
        return None

    # `git cherry` enumerates regular commits unique to the second side. Its
    # patch-equivalence marker is not proof that a change survived in the
    # result, so content-check both '+' and '-' rows against the final trees.
    cherry = git_text(repository, "cherry", "-v", first_parent, second_parent)
    if not cherry:
        return None

    present: list[tuple[str, str]] = []
    missing: list[tuple[str, str]] = []
    ambiguous: list[tuple[str, str]] = []
    with tempfile.TemporaryDirectory(prefix="buster-merge-parent-preservation-") as temporary:
        second_index = Path(temporary) / "second-parent.index"
        result_index = Path(temporary) / "result.index"
        run_git(repository, ["read-tree", second_parent], environment={"GIT_INDEX_FILE": str(second_index)})
        run_git(repository, ["read-tree", merge_sha], environment={"GIT_INDEX_FILE": str(result_index)})
        for line in cherry.splitlines():
            fields = line.split(" ", 2)
            if len(fields) < 2 or fields[0] not in ("+", "-"):
                continue
            commit_sha = fields[1]
            subject = fields[2] if len(fields) > 2 else ""
            _tree, commit_parents = commit_details(repository, commit_sha)
            patch = read_patch(repository, commit_sha, commit_parents)
            if not patch:
                continue
            # Ignore transient commits whose exact patch is no longer in the
            # second parent's final tree (for example, a change later reverted).
            if not apply_check(repository, patch, second_index, reverse=True):
                continue
            if apply_check(repository, patch, result_index, reverse=True):
                present.append((commit_sha, subject))
            elif apply_check(repository, patch, result_index, reverse=False):
                missing.append((commit_sha, subject))
                return {
                    "merge": merge_sha,
                    "first_parent": first_parent,
                    "second_parent": second_parent,
                    "merge_base": merge_base,
                    "kind": "discarded-second-parent-changes",
                    "missing": missing,
                    "ambiguous": ambiguous,
                    "represented": present,
                }
            else:
                ambiguous.append((commit_sha, subject))

    if missing or ambiguous:
        return {
            "merge": merge_sha,
            "first_parent": first_parent,
            "second_parent": second_parent,
            "merge_base": merge_base,
            "kind": "discarded-second-parent-changes",
            "missing": missing,
            "ambiguous": ambiguous,
            "represented": present,
        }
    return None


def check_history(repository: Path, candidate_revision: str, base_revision: str | None,
                  event_name: str = "") -> dict[str, object]:
    candidate = resolve_commit(repository, candidate_revision)
    base, _parents = resolve_range_base(repository, candidate, base_revision, event_name)
    merges = merge_commits_in_range(repository, candidate, base)
    findings: list[dict[str, object]] = []
    first_parent_equal = 0
    for merge_sha in merges:
        merge_tree, parents = commit_details(repository, merge_sha)
        if len(parents) == 2 and merge_tree == commit_details(repository, parents[0])[0]:
            first_parent_equal += 1
        finding = inspect_merge(repository, merge_sha)
        if finding:
            findings.append(finding)
    return {
        "candidate": candidate,
        "base": base,
        "merge_count": len(merges),
        "first_parent_equal_count": first_parent_equal,
        "findings": findings,
    }


def format_finding(finding: dict[str, object]) -> list[str]:
    if finding["kind"] == "unclassified":
        return [
            f"::error title=Merge-parent history is ambiguous::{finding['merge']} has the first parent's tree, "
            f"but the second parent cannot be classified ({finding['second_parent']}).",
            f"  reason: {finding['reason']}",
            "Review the merge history and preserve all second-parent changes before merging.",
        ]
    lines = [
        f"::error title=Merge drops second-parent changes::{finding['merge']} has the first parent's tree, "
        f"but second-parent changes are not represented ({finding['second_parent']}).",
        f"  merge-base: {finding['merge_base']}",
    ]
    for label in ("missing", "ambiguous"):
        for commit_sha, subject in finding[label]:
            suffix = f" — {subject}" if subject else ""
            lines.append(f"  {label}: {commit_sha}{suffix}")
    lines.append("Preserve the second-parent changes in the merge result, or make the merge a true no-op.")
    return lines


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--candidate", default=os.environ.get("GITHUB_SHA", ""))
    parser.add_argument("--base", default=os.environ.get("MERGE_PARENT_BASE_SHA", ""))
    parser.add_argument("--event-name", default=os.environ.get("GITHUB_EVENT_NAME", ""))
    options = parser.parse_args(arguments)
    if not options.candidate:
        parser.error("--candidate or GITHUB_SHA is required")
    repository = Path(options.repository).resolve()
    try:
        report = check_history(repository, options.candidate, options.base, options.event_name)
    except MergePreservationError as error:
        print(f"::error title=Merge-parent preservation check failed::{error}")
        return 2

    print(
        "Merge-parent preservation: "
        f"candidate={report['candidate']} base={report['base'] or '(root)'} "
        f"merges={report['merge_count']} first-parent-equal={report['first_parent_equal_count']}"
    )
    findings = report["findings"]
    for finding in findings:
        for line in format_finding(finding):
            print(line)
    if findings:
        return 1
    print("No newly introduced merge was found to discard a cleanly identifiable second-parent change.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
