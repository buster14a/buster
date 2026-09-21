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
        creator = row.get("creator")
        if (row.get("state") == "success" and isinstance(creator, dict) and
                creator.get("login") == "github-actions[bot]"):
            matches.append(row)
    if not matches:
        raise AdmissionError(
            "exact head lacks the trusted GitHub Actions integration attestation"
        )
    row = max(matches, key=lambda value: int(value.get("id", 0)))
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
                       status_path: Path | None, allow_pending: bool) -> dict:
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
        if status_path is None:
            raise AdmissionError("trusted integration status evidence is required")
        status_row = verify_status(load_status(status_path), head, evidence)
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


def check_event(repo: Path, base: str, head: str, current_main: str,
                status_path: Path | None, event: str, allow_pending: bool) -> dict:
    if event == "pull_request":
        return check_pull_request(
            repo, base, head, current_main, status_path, allow_pending
        )
    if event == "merge_group":
        classification = integration.classify_candidate(repo, base, head)
        changed = changed_paths(repo, base, head)
        requires_writer = classification_requires_writer(
            classification, changed, bound_sources(repo, base)
        )
        if requires_writer:
            raise AdmissionError(
                "retirement-sensitive merge groups require the serialized trusted "
                "integration path; do not admit a synthetic group that lacks its "
                "generated final tree"
            )
        return {
            "schema": SCHEMA,
            "status": "admitted",
            "mode": "ordinary-merge-group",
            "base": commit(repo, base),
            "head": commit(repo, head),
        }
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
    check.add_argument(
        "--event", choices=("pull_request", "merge_group", "push", "workflow_dispatch"),
        required=True,
    )
    check.add_argument("--allow-pending", action="store_true")

    invalidate = subparsers.add_parser("invalidate-stale")
    invalidate.add_argument("--repository", required=True)
    invalidate.add_argument("--new-main", required=True)
    invalidate.add_argument("--details-url", required=True)
    return result


def main(argv=None) -> int:
    arguments = parser().parse_args(argv)
    try:
        if arguments.command == "check":
            report = check_event(
                arguments.repo_root, arguments.base, arguments.head,
                arguments.current_main, arguments.status_json, arguments.event,
                arguments.allow_pending,
            )
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
