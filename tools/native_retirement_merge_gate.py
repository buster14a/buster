#!/usr/bin/env python3
"""Fail-closed admission for native-retirement-sensitive pull requests.

Ordinary feature heads may validate the retirement closure ephemerally, but a
retirement-sensitive pull request is mergeable only after the trusted writer
has replaced its head with an attested integration commit for exact current
main.  The default-branch policy workflow also invalidates those attestations
when main advances, preserving the repository's deliberately non-strict
behind-branch policy for unrelated pull requests.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import urllib.parse
import urllib.request


MODULE_PATH = Path(__file__).with_name("native_retirement_integration.py")
SPEC = importlib.util.spec_from_file_location("native_retirement_integration", MODULE_PATH)
integration = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = integration
SPEC.loader.exec_module(integration)

SCHEMA = "buster-native-retirement-merge-admission-v1"
STATUS_CONTEXT = "Native retirement trusted integration"
REQUIRED_CHECK_NAME = "Native retirement merge admission"
TRAILER_BASE = "Native-retirement-base"
TRAILER_CANDIDATE = "Native-retirement-candidate"
TRAILER_FINAL_TREE = "Native-retirement-final-tree"
TRAILER_EVIDENCE = "Native-retirement-integration-evidence"
TRAILER_KIND = "Native-retirement-transition-kind"
TRAILER_KEYS = (
    TRAILER_BASE,
    TRAILER_CANDIDATE,
    TRAILER_FINAL_TREE,
    TRAILER_EVIDENCE,
    TRAILER_KIND,
)
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
MAX_API_PAGES = 10


class AdmissionError(Exception):
    """A pull request cannot be admitted through the ordinary merge path."""


def canonical_json(value: dict) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def require_hex(value, pattern, label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise AdmissionError(f"{label} is not a canonical digest: {value!r}")
    return value


def commit(repo: Path, revision: str) -> str:
    return integration._commit(repo, revision)


def tree(repo: Path, revision: str) -> str:
    return integration._tree(repo, revision)


def commit_parents(repo: Path, revision: str) -> tuple[str, ...]:
    line = integration._git(repo, "rev-list", "--parents", "-n", "1", revision).stdout.strip()
    fields = line.split()
    if not fields or fields[0] != commit(repo, revision):
        raise AdmissionError("cannot resolve integration commit parents")
    return tuple(fields[1:])


def commit_message(repo: Path, revision: str) -> str:
    return integration._git(repo, "show", "-s", "--format=%B", revision).stdout


def parse_trailers(message: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in message.splitlines():
        for key in TRAILER_KEYS:
            prefix = key + ": "
            if line.startswith(prefix):
                if key in values:
                    raise AdmissionError("duplicate integration trailer: " + key)
                values[key] = line[len(prefix):].strip()
    return values


def bound_sources(repo: Path, base: str) -> frozenset[str]:
    result = integration._git(
        repo, "show", base + ":docs/native-retirement-repository-sources-v1.json"
    )
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AdmissionError("trusted base has malformed repository-source snapshot") from error
    records = data.get("records")
    if not isinstance(records, list):
        raise AdmissionError("trusted base repository-source snapshot has no records")
    sources: set[str] = set()
    for record in records:
        source = record.get("source") if isinstance(record, dict) else None
        if not isinstance(source, str):
            raise AdmissionError("trusted base repository-source snapshot has a malformed record")
        sources.add(integration._canonical_path(source))
    if not sources:
        raise AdmissionError("trusted base repository-source snapshot is unexpectedly empty")
    return frozenset(sources)


def changed_paths(repo: Path, base: str, head: str) -> tuple[str, ...]:
    return integration.changed_paths(repo, base, head)


def classification_requires_writer(classification, changed: tuple[str, ...],
                                   sources: frozenset[str]) -> bool:
    source_change = any(path in sources for path in changed)
    return source_change or classification.kind in ("bootstrap", "policy")


def clean_merge_tree(repo: Path, base: str, candidate: str) -> str:
    result = integration._git(
        repo, "merge-tree", "--write-tree", base, candidate, check=False
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "merge conflict"
        raise AdmissionError("candidate no longer forms a conflict-free current-main tree: " + detail)
    first = result.stdout.splitlines()[0].strip() if result.stdout.splitlines() else ""
    return require_hex(first, HEX40, "clean combined tree")


def tree_changed_paths(repo: Path, old_tree: str, new_tree: str) -> tuple[str, ...]:
    result = integration._git(
        repo, "diff-tree", "--no-commit-id", "--name-only", "--no-renames", "-r",
        old_tree, new_tree,
    )
    return tuple(path for path in result.stdout.splitlines() if path)


def load_status(path: Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise AdmissionError(f"cannot read integration status evidence {path}: {error}") from error
    if not isinstance(value, dict):
        raise AdmissionError("integration status evidence is not an object")
    return value


def verify_status(status: dict, head: str, evidence: str) -> dict:
    if status.get("sha") != head:
        raise AdmissionError("integration status evidence is bound to a different head")
    rows = status.get("statuses")
    if not isinstance(rows, list):
        raise AdmissionError("integration status evidence has no status rows")
    matches = []
    for row in rows:
        if not isinstance(row, dict) or row.get("context") != STATUS_CONTEXT:
            continue
        matches.append(row)
    if not matches:
        raise AdmissionError(
            "exact head lacks the trusted GitHub Actions integration attestation"
        )
    row = max(matches, key=lambda value: int(value.get("id", 0)))
    creator = row.get("creator")
    if (row.get("state") != "success" or not isinstance(creator, dict) or
            creator.get("login") != "github-actions[bot]"):
        raise AdmissionError("latest trusted GitHub Actions integration attestation is not successful")
    description = row.get("description")
    if not isinstance(description, str) or evidence[:16] not in description:
        raise AdmissionError("integration status does not name the exact evidence digest")
    target_url = row.get("target_url")
    if not isinstance(target_url, str) or not target_url.startswith("https://github.com/"):
        raise AdmissionError("integration status has no trusted workflow target")
    return row


def integration_record(repo: Path, head: str) -> tuple[tuple[str, ...], dict[str, str]]:
    parents = commit_parents(repo, head)
    trailers = parse_trailers(commit_message(repo, head))
    return parents, trailers


def check_pull_request(repo: Path, base: str, head: str, current_main: str,
                       status_path: Path | None, allow_pending: bool, *,
                       status_data: dict | None = None) -> dict:
    repo = repo.resolve()
    base = commit(repo, base)
    head = commit(repo, head)
    current_main = require_hex(current_main, HEX40, "current main")
    sources = bound_sources(repo, base)
    direct_changed = changed_paths(repo, base, head)
    direct_classification = integration.classify_candidate(repo, base, head)

    parents, trailers = integration_record(repo, head)
    is_integration = (
        len(parents) == 2 and
        all(key in trailers for key in TRAILER_KEYS)
    )
    if is_integration:
        recorded_base = require_hex(trailers[TRAILER_BASE], HEX40, "integration base")
        candidate = require_hex(
            trailers[TRAILER_CANDIDATE], HEX40, "integration candidate"
        )
        final_tree = require_hex(
            trailers[TRAILER_FINAL_TREE], HEX40, "integration final tree"
        )
        evidence = require_hex(
            trailers[TRAILER_EVIDENCE], HEX64, "integration evidence"
        )
        transition_kind = trailers[TRAILER_KIND]
        if transition_kind not in integration.TRANSITION_KINDS:
            raise AdmissionError("integration commit has an invalid transition kind")
        if recorded_base != base or parents[0] != base:
            raise AdmissionError("integration commit is not based on the pull request base")
        if recorded_base != current_main:
            raise AdmissionError(
                "main advanced after trusted integration; dispatch the writer again"
            )
        if parents[1] != candidate:
            raise AdmissionError("integration candidate trailer does not match second parent")
        if tree(repo, head) != final_tree:
            raise AdmissionError("integration final-tree trailer does not match the head tree")

        candidate_changed = changed_paths(repo, base, candidate)
        candidate_classification = integration.classify_candidate(repo, base, candidate)
        integration.enforce_classification(
            candidate_classification, transition_kind, True
        )
        if not classification_requires_writer(
                candidate_classification, candidate_changed, sources):
            raise AdmissionError(
                "trusted integration was used for a pull request that does not require it"
            )

        combined = clean_merge_tree(repo, base, candidate)
        final_changes = set(tree_changed_paths(repo, combined, final_tree))
        if not final_changes <= integration.GENERATED_PATHS:
            raise AdmissionError(
                "trusted integration changed non-generated paths after the reviewed candidate: " +
                ", ".join(sorted(final_changes - integration.GENERATED_PATHS))
            )
        if status_path is None and status_data is None:
            raise AdmissionError("trusted integration status evidence is required")
        status_row = verify_status(status_data if status_data is not None else load_status(status_path),
                                   head, evidence)
        return {
            "schema": SCHEMA,
            "status": "admitted",
            "mode": "trusted-integration",
            "base": base,
            "head": head,
            "candidate": candidate,
            "final_tree": final_tree,
            "transition_kind": transition_kind,
            "generated_paths": sorted(final_changes),
            "attestation_id": status_row.get("id"),
        }

    if direct_classification.generated_paths:
        integration.enforce_classification(direct_classification, None, True)
    if direct_classification.kind == "split-required":
        integration.enforce_classification(direct_classification, None, True)
    requires_writer = classification_requires_writer(
        direct_classification, direct_changed, sources
    )
    if requires_writer:
        if allow_pending:
            return {
                "schema": SCHEMA,
                "status": "pending",
                "mode": "pending-integration",
                "base": base,
                "head": head,
                "transition_kind": direct_classification.kind,
            }
        raise AdmissionError(
            "this pull request changes native-retirement-bound or trusted state; "
            "ordinary merge is blocked until the trusted integration workflow "
            "publishes and attests the exact current-main combined head"
        )
    return {
        "schema": SCHEMA,
        "status": "admitted",
        "mode": "ordinary",
        "base": base,
        "head": head,
    }


def verify_group_publication(api, status: dict, base: str) -> dict:
    """A status posted during publication is not a completed writer run."""
    prefix = "https://github.com/" + api.repository + "/actions/runs/"
    target = status.get("target_url", "")
    run_id = target.removeprefix(prefix)
    if not target.startswith(prefix) or re.fullmatch(r"[1-9][0-9]*", run_id) is None:
        raise AdmissionError("group attestation must name this repository's writer run")
    run = api.request("actions/runs/" + run_id)
    if (run.get("id") != int(run_id) or
            run.get("repository", {}).get("full_name") != api.repository or
            run.get("path") != ".github/workflows/native-retirement-integration.yml" or
            run.get("event") != "workflow_dispatch" or run.get("head_branch") != "main" or
            run.get("head_sha") != base or run.get("status") != "completed" or
            run.get("conclusion") != "success" or
            type(run.get("run_attempt")) is not int or run["run_attempt"] < 1):
        raise AdmissionError("group publication is not a successful current-base trusted writer run")
    try:
        started = datetime.fromisoformat(run["run_started_at"].replace("Z", "+00:00"))
        posted = datetime.fromisoformat(status["created_at"].replace("Z", "+00:00"))
        finished = datetime.fromisoformat(run["updated_at"].replace("Z", "+00:00"))
        current_attempt = (started.utcoffset() is not None and posted.utcoffset() is not None and
                           finished.utcoffset() is not None and started <= posted <= finished)
    except (KeyError, TypeError, ValueError, AttributeError) as error:
        raise AdmissionError("writer attempt timestamps are missing or invalid") from error
    if not current_attempt:
        raise AdmissionError("integration status predates the latest writer attempt")
    return {"run_id": run["id"], "run_attempt": run["run_attempt"]}


def source_candidate(repo: Path, base: str, head: str, api) -> dict:
    """Recover source for a fresh authorized dispatch without reusing output.

    Authorization still targets the live PR head. Only a successfully published
    two-parent integration may be unwrapped, and its original source must still
    form a clean merge with current main. The existing writer performs its full
    reconstruction; this helper changes no refs or generated files.
    """
    base, head = commit(repo, base), commit(repo, head)
    parents, trailers = integration_record(repo, head)
    source = head
    if any(key in trailers for key in TRAILER_KEYS):
        if len(parents) != 2 or not all(key in trailers for key in TRAILER_KEYS):
            raise AdmissionError("incomplete previous integration record")
        old_base = require_hex(trailers[TRAILER_BASE], HEX40, "previous integration base")
        integration._git(repo, "merge-base", "--is-ancestor", old_base, base)
        if api is None:
            raise AdmissionError("source recovery requires live trusted publication evidence")
        statuses = {"sha": head, "statuses": api.all("statuses/" + head)}
        admitted = check_pull_request(repo, old_base, head, old_base, None, False,
                                      status_data=statuses)
        evidence = require_hex(trailers[TRAILER_EVIDENCE], HEX64, "previous integration evidence")
        verify_group_publication(api, verify_status(statuses, head, evidence), old_base)
        source = admitted["candidate"]
    classification = integration.classify_candidate(repo, base, source)
    integration.enforce_classification(classification, None, True)
    combined = clean_merge_tree(repo, base, source)
    return {"base": base, "head": head, "source_head": source,
            "combined_tree": combined, "classification": classification.kind}


def check_merge_group(repo: Path, base: str, head: str, current_main: str, api) -> dict:
    """Admit only a single exact-tree projection of the existing writer's output.

    GitHub owns the synthetic commit. Its SHA may differ from the attested PR
    head, but its entire tree must be identical. No generated bytes are written
    and no status on the PR head substitutes for combined-head CI.
    """
    base, head = commit(repo, base), commit(repo, head)
    if base != require_hex(current_main, HEX40, "current main"):
        raise AdmissionError(
            f"group base does not equal live main: main={current_main} "
            f"base={base} head={head}; wait for the predecessor or rebuild the group"
        )
    parents = commit_parents(repo, head)
    if len(parents) != 2 or parents[0] != base:
        raise AdmissionError("merge groups must contain one candidate with current main as first parent")
    member = parents[1]
    if tree(repo, head) != clean_merge_tree(repo, base, member):
        raise AdmissionError("merge group does not match its conflict-free combined tree")

    classification = integration.classify_candidate(repo, base, head)
    changed = changed_paths(repo, base, head)
    sensitive = bool(classification.generated_paths) or classification_requires_writer(
        classification, changed, bound_sources(repo, base)
    ) or classification.kind == "split-required"
    report = {"schema": SCHEMA, "status": "admitted", "mode": "ordinary-merge-group",
              "base": base, "head": head, "final_tree": tree(repo, head)}
    if sensitive:
        if api is None:
            raise AdmissionError("retirement-sensitive merge groups require live trusted publication evidence")
        pulls = api.all("commits/" + member + "/pulls")
        matches = [pull for pull in pulls if pull.get("head", {}).get("sha") == member and
                   pull.get("state") == "open" and pull.get("draft") is False and
                   pull.get("head", {}).get("repo", {}).get("full_name") == api.repository and
                   pull.get("base", {}).get("repo", {}).get("full_name") == api.repository and
                   pull.get("base", {}).get("ref") == "main"]
        if len(matches) != 1:
            raise AdmissionError("group must identify one open same-repository attested PR head")
        # Use the exact creator-bearing status endpoint, never the combined
        # status endpoint (which does not preserve publisher identity).
        statuses = {"sha": member, "statuses": api.all("statuses/" + member)}
        parents, trailers = integration_record(repo, member)
        if len(parents) != 2 or not all(key in trailers for key in TRAILER_KEYS):
            raise AdmissionError("merge group member has no trusted integration record")
        evidence = require_hex(trailers[TRAILER_EVIDENCE], HEX64, "integration evidence")
        status = verify_status(statuses, member, evidence)
        # Reuse every PR admission invariant, including source ownership,
        # current-base identity, transition class and generated-only final delta.
        admitted = check_pull_request(repo, base, member, current_main, None, False,
                                      status_data=statuses)
        if admitted.get("mode") != "trusted-integration" or report["final_tree"] != admitted["final_tree"]:
            raise AdmissionError("merge group tree differs from the exact attested final tree")
        publication = verify_group_publication(api, status, base)
        report.update(mode="trusted-integration-merge-group", integration_head=member,
                      candidate=admitted["candidate"], pull_request=matches[0]["number"],
                      attestation_id=admitted["attestation_id"], publication=publication)
    return report


def check_event(repo: Path, base: str, head: str, current_main: str,
                status_path: Path | None, event: str, allow_pending: bool, api=None) -> dict:
    if event == "pull_request":
        return check_pull_request(
            repo, base, head, current_main, status_path, allow_pending
        )
    if event == "merge_group":
        return check_merge_group(repo, base, head, current_main, api)
    return {
        "schema": SCHEMA,
        "status": "admitted",
        "mode": event,
        "base": commit(repo, base),
        "head": commit(repo, head),
    }


class GitHub:
    def __init__(self, repository: str, token: str):
        if repository.count("/") != 1 or not token:
            raise AdmissionError("GitHub repository/token is missing")
        self.repository = repository
        self.prefix = "https://api.github.com/repos/" + repository + "/"
        self.token = token

    def request(self, path: str, *, method: str = "GET", body: dict | None = None,
                **query):
        url = self.prefix + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        data = None if body is None else json.dumps(body).encode("utf-8")
        request = urllib.request.Request(url, data=data, method=method, headers={
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "Content-Type": "application/json",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = response.read()
        return json.loads(payload) if payload else None

    def all(self, path: str, **query) -> list:
        result = []
        for page in range(1, MAX_API_PAGES + 1):
            rows = self.request(path, per_page=100, page=page, **query)
            result.extend(rows)
            if len(rows) < 100:
                return result
        raise AdmissionError("GitHub pagination limit reached")


def invalidate_stale(api: GitHub, new_main: str, details_url: str) -> dict:
    new_main = require_hex(new_main, HEX40, "new main")
    invalidated = []
    for pull in api.all("pulls", state="open", base="main"):
        head = pull.get("head")
        base = pull.get("base")
        if (not isinstance(head, dict) or not isinstance(base, dict) or
                head.get("repo", {}).get("full_name") != api.repository or
                base.get("repo", {}).get("full_name") != api.repository):
            continue
        head_sha = require_hex(head.get("sha"), HEX40, "open pull request head")
        commit_data = api.request("commits/" + head_sha)
        message = commit_data.get("commit", {}).get("message", "")
        trailers = parse_trailers(message)
        recorded_base = trailers.get(TRAILER_BASE)
        if recorded_base is None or recorded_base == new_main:
            continue
        require_hex(recorded_base, HEX40, "stale integration base")
        api.request("check-runs", method="POST", body={
            "name": REQUIRED_CHECK_NAME,
            "head_sha": head_sha,
            "status": "completed",
            "conclusion": "failure",
            "details_url": details_url,
            "output": {
                "title": "Trusted native-retirement integration is stale",
                "summary": (
                    "Main advanced from `" + recorded_base + "` to `" + new_main +
                    "`. Dispatch the trusted integration writer again; do not merge "
                    "this previously attested head."
                ),
            },
        })
        invalidated.append({
            "pull_request": pull.get("number"),
            "head": head_sha,
            "old_base": recorded_base,
        })
    return {
        "schema": SCHEMA,
        "status": "invalidated" if invalidated else "current",
        "main": new_main,
        "pull_requests": invalidated,
    }


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    check = subparsers.add_parser("check")
    check.add_argument("--repo-root", type=Path, required=True)
    check.add_argument("--base", required=True)
    check.add_argument("--head", required=True)
    check.add_argument("--current-main", required=True)
    check.add_argument("--status-json", type=Path)
    check.add_argument("--repository", help="Repository for live merge-group publication verification")
    check.add_argument(
        "--event", choices=("pull_request", "merge_group", "push", "workflow_dispatch"),
        required=True,
    )
    check.add_argument("--allow-pending", action="store_true")

    source = subparsers.add_parser("source-candidate")
    source.add_argument("--repo-root", type=Path, required=True)
    source.add_argument("--base", required=True)
    source.add_argument("--head", required=True)
    source.add_argument("--repository", required=True)

    invalidate = subparsers.add_parser("invalidate-stale")
    invalidate.add_argument("--repository", required=True)
    invalidate.add_argument("--new-main", required=True)
    invalidate.add_argument("--details-url", required=True)
    return result


def main(argv=None) -> int:
    arguments = parser().parse_args(argv)
    try:
        if arguments.command == "check":
            api = (GitHub(arguments.repository, os.environ.get("GH_TOKEN", ""))
                   if arguments.event == "merge_group" and arguments.repository else None)
            report = check_event(
                arguments.repo_root, arguments.base, arguments.head,
                arguments.current_main, arguments.status_json, arguments.event,
                arguments.allow_pending, api,
            )
        elif arguments.command == "source-candidate":
            api = GitHub(arguments.repository, os.environ.get("GH_TOKEN", ""))
            report = source_candidate(arguments.repo_root, arguments.base, arguments.head, api)
        else:
            api = GitHub(arguments.repository, os.environ["GH_TOKEN"])
            report = invalidate_stale(api, arguments.new_main, arguments.details_url)
        print(canonical_json(report), end="")
        return 0
    except (AdmissionError, integration.IntegrationError, KeyError, OSError,
            TypeError, ValueError) as error:
        print("native-retirement merge admission failure: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
