#!/usr/bin/env python3
"""Read-only merge-conflict preflight against an exact default-branch commit.

The core path uses only git plumbing.  It never checks out, rebases, merges, or
updates a pull-request branch.  GitHub orchestration runs this trusted script
from the default branch, fetches immutable objects into private local refs, and
publishes a commit status whose description binds the result to exact SHAs.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import time
import tempfile

import native_retirement_merge_gate as retirement_gate
from typing import Sequence
import urllib.error
import urllib.parse
import urllib.request


REPORT_SCHEMA = "buster-merge-conflict-preflight-v1"
STATUS_SCHEMA = "v1"
STATUS_CONTEXT = "merge-conflict-preflight"
HEX_OBJECT = re.compile(r"[0-9a-f]{40,64}\Z")
STATUS_DESCRIPTION = re.compile(
    r"v1 m=(?P<main>[0-9a-f]{40}) h=(?P<head>[0-9a-f]{40}) "
    r"o=(?P<outcome>[1-4]) c=(?P<state>clean|conflicted)\Z"
)

# These are the generated and reviewed authorities installed by #862-#865.
# Keep this list synchronized with tools/native_retirement_integration.py.  The
# preflight deliberately runs from trusted current main rather than importing
# candidate code.
GENERATED_RETIREMENT_PATHS = frozenset((
    "docs/native-retirement-repository-sources-v1.json",
    "tools/native_retirement_dependency_binding.generated.h",
))
RETIREMENT_TRUST_PATHS = frozenset((
    ".github/workflows/api-migration-policy.yml",
    ".github/workflows/native-retirement-contract.yml",
    ".github/workflows/native-retirement-integration.yml",
    ".github/workflows/native-retirement-rebind.yml",
    ".gitattributes",
    "tools/native_retirement_contract.py",
    "tools/native_retirement_dependency_binding.py",
    "tools/native_retirement_external.py",
    "tools/native_retirement_integration.py",
    "tools/native_retirement_merge_gate.py",
    "tools/native_retirement_materializer.py",
    "tools/native_retirement_rebind.py",
    "tools/native_retirement_rebind_contract.py",
    "tools/native_retirement_sdks.py",
))
RETIREMENT_POLICY_SCHEMA_PATHS = frozenset((
    "docs/native-retirement-dependencies-legacy-v1.json",
    "docs/native-retirement-dependencies-v1.json",
    "docs/native-retirement-sdks-v1.json",
    "tools/native_retirement_census.c",
))

OUTCOME_GENERATED = 1
OUTCOME_SOURCE = 2
OUTCOME_CLEAN = 3
OUTCOME_POLICY = 4
MAX_STABLE_ATTEMPTS = 3
MAX_API_PAGES = 10


class PreflightError(Exception):
    """A malformed repository, merge-tree result, event, or API response."""


@dataclass(frozen=True)
class GitResult:
    command: tuple[str, ...]
    returncode: int
    stdout: bytes
    stderr: bytes


@dataclass(frozen=True)
class StageEntry:
    path: str
    mode: str
    object_id: str
    stage: int


@dataclass(frozen=True)
class MergeMessage:
    paths: tuple[str, ...]
    short: str
    detail: str


@dataclass(frozen=True)
class MergeTreeResult:
    clean: bool
    tree: str
    stages: tuple[StageEntry, ...]
    messages: tuple[MergeMessage, ...]


@dataclass(frozen=True)
class PreviousResult:
    main: str
    head: str
    outcome: int | None = None


class GitHubApi:
    def __init__(self, repository: str, token: str, api_url: str) -> None:
        if not repository or "/" not in repository:
            raise PreflightError(f"invalid GitHub repository: {repository!r}")
        if not token:
            raise PreflightError("GITHUB_TOKEN is required for GitHub event orchestration")
        self.repository = repository
        self.token = token
        self.api_url = api_url.rstrip("/")

    def _request(self, method: str, path: str, payload: dict | None = None):
        url = self.api_url + path
        data = None
        headers = {
            "Accept": "application/vnd.github+json",
            "Authorization": "Bearer " + self.token,
            "User-Agent": "buster-merge-conflict-preflight",
            "X-GitHub-Api-Version": "2022-11-28",
        }
        if payload is not None:
            data = json.dumps(payload, sort_keys=True).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                body = response.read()
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")
            raise PreflightError(f"GitHub API {method} {path} failed: HTTP {error.code}: {detail}") from error
        except urllib.error.URLError as error:
            raise PreflightError(f"GitHub API {method} {path} failed: {error}") from error
        if not body:
            return None
        try:
            return json.loads(body)
        except json.JSONDecodeError as error:
            raise PreflightError(f"GitHub API {method} {path} returned invalid JSON") from error

    def open_pull_requests(self, base: str) -> list[dict]:
        pulls: list[dict] = []
        for page in range(1, MAX_API_PAGES + 1):
            query = urllib.parse.urlencode({
                "state": "open",
                "base": base,
                "per_page": 100,
                "page": page,
                "sort": "updated",
                "direction": "desc",
            })
            value = self._request("GET", f"/repos/{self.repository}/pulls?{query}")
            if not isinstance(value, list):
                raise PreflightError("GitHub open-pull-request response is not a list")
            pulls.extend(value)
            if len(value) < 100:
                break
        else:
            raise PreflightError(f"open pull-request pagination exceeded {MAX_API_PAGES} pages")
        return pulls

    def pull_request(self, number: int) -> dict:
        value = self._request("GET", f"/repos/{self.repository}/pulls/{number}")
        if not isinstance(value, dict):
            raise PreflightError(f"GitHub pull request #{number} response is not an object")
        return value

    def retirement_status(self, head: str) -> dict:
        rows = []
        for page in range(1, MAX_API_PAGES + 1):
            value = self._request("GET", f"/repos/{self.repository}/statuses/{head}?per_page=100&page={page}")
            if not isinstance(value, list):
                raise PreflightError("GitHub retirement status response is not a list")
            rows.extend(value)
            if len(value) < 100:
                break
        else:
            raise PreflightError("retirement status pagination limit reached")
        return {"sha": head, "statuses": rows}

    def previous_status(self, head: str, context: str) -> PreviousResult | None:
        value = self._request("GET", f"/repos/{self.repository}/commits/{head}/status")
        statuses = value.get("statuses") if isinstance(value, dict) else None
        if not isinstance(statuses, list):
            raise PreflightError("GitHub combined-status response omits statuses")
        previous = None
        for status in statuses:
            if isinstance(status, dict) and status.get("context") == context:
                description = status.get("description")
                match = STATUS_DESCRIPTION.fullmatch(description) if isinstance(description, str) else None
                if match and match.group("head") == head:
                    previous = PreviousResult(
                        main=match.group("main"),
                        head=match.group("head"),
                        outcome=int(match.group("outcome")),
                    )
                    break
        return previous

    def publish_status(self, head: str, report: dict, context: str, target_url: str) -> None:
        outcome = report["outcome"]["number"]
        clean = report["merge"]["clean"]
        blocking = report["outcome"]["blocking"]
        description = (
            f"{STATUS_SCHEMA} m={report['main']['sha']} h={report['head']['sha']} "
            f"o={outcome} c={'clean' if clean else 'conflicted'}"
        )
        if len(description) > 140:
            raise PreflightError("commit-status description exceeds GitHub's 140-character limit")
        payload = {
            "state": "failure" if blocking else "success",
            "context": context,
            "description": description,
            "target_url": target_url,
        }
        self._request("POST", f"/repos/{self.repository}/statuses/{head}", payload)


def _decode_path(value: bytes) -> str:
    return value.decode("utf-8", "surrogateescape")


def _canonical_path(path: str) -> str:
    candidate = PurePosixPath(path)
    if candidate.is_absolute() or not candidate.parts or any(
            part in ("", ".", "..") for part in candidate.parts):
        raise PreflightError(f"non-canonical repository path: {path!r}")
    return candidate.as_posix()


def _git(repo: Path, *arguments: str, check: bool = True) -> GitResult:
    environment = os.environ.copy()
    environment.update({
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_OPTIONAL_LOCKS": "0",
        "GIT_TERMINAL_PROMPT": "0",
    })
    command = (
        "git", "-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false",
        "-C", os.fspath(repo), *arguments,
    )
    process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
    result = GitResult(tuple(command), process.returncode, process.stdout, process.stderr)
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        raise PreflightError(f"git {' '.join(arguments)} failed: {detail or 'exit ' + str(result.returncode)}")
    return result


def _commit(repo: Path, revision: str) -> str:
    value = _git(repo, "rev-parse", "--verify", revision + "^{commit}").stdout.decode().strip()
    if not HEX_OBJECT.fullmatch(value):
        raise PreflightError(f"invalid commit identity for {revision!r}: {value!r}")
    return value


def _tree(repo: Path, revision: str) -> str:
    value = _git(repo, "rev-parse", "--verify", revision + "^{tree}").stdout.decode().strip()
    if not HEX_OBJECT.fullmatch(value):
        raise PreflightError(f"invalid tree identity for {revision!r}: {value!r}")
    return value


def _object_exists(repo: Path, revision: str) -> bool:
    return _git(repo, "cat-file", "-e", revision + "^{commit}", check=False).returncode == 0


def _is_ancestor(repo: Path, older: str, newer: str) -> bool:
    result = _git(repo, "merge-base", "--is-ancestor", older, newer, check=False)
    if result.returncode not in (0, 1):
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        raise PreflightError(f"git merge-base --is-ancestor failed: {detail}")
    return result.returncode == 0


def _merge_bases(repo: Path, main: str, head: str) -> tuple[str, ...]:
    result = _git(repo, "merge-base", "--all", main, head, check=False)
    if result.returncode == 1 and not result.stdout.strip():
        return ()
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        raise PreflightError(f"git merge-base --all failed: {detail}")
    values = tuple(sorted(line for line in result.stdout.decode().splitlines() if line))
    if any(not HEX_OBJECT.fullmatch(value) for value in values):
        raise PreflightError(f"invalid merge-base output: {values!r}")
    return values


def _parse_stage_entry(value: bytes) -> StageEntry:
    metadata, separator, raw_path = value.partition(b"\t")
    if not separator:
        raise PreflightError(f"malformed merge-tree stage record: {value!r}")
    pieces = metadata.decode("ascii", "strict").split(" ")
    if len(pieces) != 3 or pieces[2] not in ("1", "2", "3"):
        raise PreflightError(f"malformed merge-tree stage metadata: {metadata!r}")
    mode, object_id, stage = pieces
    if not re.fullmatch(r"[0-7]{6}", mode) or not HEX_OBJECT.fullmatch(object_id):
        raise PreflightError(f"invalid merge-tree stage metadata: {metadata!r}")
    return StageEntry(_canonical_path(_decode_path(raw_path)), mode, object_id, int(stage))


def _parse_merge_tree(output: bytes, returncode: int) -> MergeTreeResult:
    fields = output.split(b"\0")
    if len(fields) < 2:
        raise PreflightError("merge-tree did not emit the expected NUL-delimited header")
    tree = fields[0].decode("ascii", "strict")
    if not HEX_OBJECT.fullmatch(tree):
        raise PreflightError(f"merge-tree emitted an invalid tree identity: {tree!r}")
    index = 1
    stages: list[StageEntry] = []
    while index < len(fields) and fields[index]:
        stages.append(_parse_stage_entry(fields[index]))
        index += 1
    if index >= len(fields):
        raise PreflightError("merge-tree output omitted the conflict/message separator")
    index += 1
    messages: list[MergeMessage] = []
    while index < len(fields) and fields[index]:
        try:
            path_count = int(fields[index].decode("ascii", "strict"))
        except ValueError as error:
            raise PreflightError(f"invalid merge-tree message path count: {fields[index]!r}") from error
        index += 1
        if path_count < 0 or index + path_count + 2 > len(fields):
            raise PreflightError("truncated merge-tree message record")
        paths = tuple(_canonical_path(_decode_path(value)) for value in fields[index:index + path_count])
        index += path_count
        short = fields[index].decode("utf-8", "replace")
        detail = fields[index + 1].decode("utf-8", "replace").rstrip("\n")
        index += 2
        messages.append(MergeMessage(paths, short, detail))
    if returncode not in (0, 1):
        raise PreflightError(f"merge-tree failed with exit {returncode}")
    clean = returncode == 0
    if clean and stages:
        raise PreflightError("merge-tree returned success with unmerged stage entries")
    if not clean and not stages and not any(message.short.startswith("CONFLICT") for message in messages):
        raise PreflightError("merge-tree returned a conflict without conflict records")
    return MergeTreeResult(clean, tree, tuple(stages), tuple(messages))


def _merge_tree(repo: Path, main: str, head: str) -> MergeTreeResult:
    result = _git(repo, "merge-tree", "--write-tree", "--messages", "-z", main, head, check=False)
    if result.returncode not in (0, 1):
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        raise PreflightError(f"git merge-tree failed: {detail or 'exit ' + str(result.returncode)}")
    return _parse_merge_tree(result.stdout, result.returncode)


def _changed_paths(repo: Path, merge_bases: Sequence[str], head: str) -> dict[str, tuple[str, ...]]:
    statuses: dict[str, set[str]] = {}
    bases = merge_bases or ()
    for base in bases:
        result = _git(repo, "diff", "--name-status", "--no-renames", "-z", base, head)
        fields = result.stdout.split(b"\0")
        index = 0
        while index < len(fields) and fields[index]:
            status = fields[index].decode("ascii", "strict")
            if index + 1 >= len(fields) or not fields[index + 1]:
                raise PreflightError("truncated git diff --name-status output")
            path = _canonical_path(_decode_path(fields[index + 1]))
            statuses.setdefault(path, set()).add(status)
            index += 2
    return {path: tuple(sorted(values)) for path, values in sorted(statuses.items())}


def _raw_conflict_kind(short: str, detail: str) -> str | None:
    # In NUL-delimited mode Git may use the generic short token
    # ``CONFLICT (contents)`` while the human detail preserves the useful
    # add/add or modify/delete classification. Prefer that specific detail.
    match = re.match(r"CONFLICT \(([^)]+)\):", detail)
    if match is None:
        match = re.fullmatch(r"CONFLICT \(([^)]+)\)", short)
    return match.group(1).lower() if match else None


def _normalized_conflict_kind(raw: str) -> str:
    aliases = {
        "content": "modify/modify",
        "contents": "modify/modify",
        "add/add": "add/add",
        "modify/delete": "modify/delete",
        "rename/delete": "rename/delete",
        "rename/rename": "rename/rename",
        "rename/add": "rename/add",
        "file/directory": "file/directory",
        "directory/file": "file/directory",
        "submodule": "submodule",
    }
    return aliases.get(raw, raw.replace(" ", "-"))


def _fallback_stage_kind(stages: set[int]) -> str:
    if stages == {2, 3}:
        return "add/add"
    if stages == {1, 2}:
        return "modify/delete"
    if stages == {1, 3}:
        return "delete/modify"
    if stages == {1, 2, 3}:
        return "modify/modify"
    return "unmerged"


def _path_flags(path: str) -> dict[str, bool]:
    generated = path in GENERATED_RETIREMENT_PATHS
    policy = path in RETIREMENT_POLICY_SCHEMA_PATHS
    trust = path in RETIREMENT_TRUST_PATHS
    return {
        "generated_or_integration_owned_retirement_artifact": generated,
        "retirement_policy_or_schema": policy,
        "retirement_trust_path": trust,
        "retirement_policy_schema_or_trust_path": policy or trust,
    }


def _conflicts(merge: MergeTreeResult) -> tuple[list[dict], list[dict]]:
    stages_by_path: dict[str, set[int]] = {}
    for entry in merge.stages:
        stages_by_path.setdefault(entry.path, set()).add(entry.stage)
    kinds_by_path: dict[str, set[str]] = {}
    records: list[dict] = []
    for message in merge.messages:
        raw = _raw_conflict_kind(message.short, message.detail)
        if raw is None:
            continue
        normalized = _normalized_conflict_kind(raw)
        paths = tuple(sorted(set(message.paths)))
        records.append({
            "classification": normalized,
            "git_classification": raw,
            "paths": list(paths),
            "message": message.detail,
        })
        for path in paths:
            kinds_by_path.setdefault(path, set()).add(normalized)
    all_paths = set(stages_by_path) | set(kinds_by_path)
    path_records: list[dict] = []
    for path in sorted(all_paths):
        kinds = kinds_by_path.setdefault(path, set())
        if not kinds and path in stages_by_path:
            kinds.add(_fallback_stage_kind(stages_by_path[path]))
        flags = _path_flags(path)
        path_records.append({
            "path": path,
            "classifications": sorted(kinds),
            "stages": sorted(stages_by_path.get(path, set())),
            **flags,
            "genuine_source_overlap": not (
                flags["generated_or_integration_owned_retirement_artifact"] or
                flags["retirement_policy_schema_or_trust_path"]
            ),
        })
    records.sort(key=lambda record: (record["classification"], tuple(record["paths"]), record["message"]))
    return records, path_records


def _previous_state(repo: Path, previous: PreviousResult | None, main: str, head: str) -> dict:
    result = {
        "available": previous is not None,
        "same_head": None,
        "main_advanced_since_last_authoritative_result": None,
        "advance_kind": "unknown",
        "main_sha": previous.main if previous else None,
        "head_sha": previous.head if previous else None,
        "outcome_number": previous.outcome if previous else None,
    }
    if previous is not None:
        same_head = previous.head == head
        result["same_head"] = same_head
        if not same_head:
            result["advance_kind"] = "candidate-head-changed"
        elif previous.main == main:
            result["main_advanced_since_last_authoritative_result"] = False
            result["advance_kind"] = "unchanged"
        elif not _object_exists(repo, previous.main):
            result["advance_kind"] = "previous-main-object-unavailable"
        else:
            result["main_advanced_since_last_authoritative_result"] = True
            result["advance_kind"] = (
                "fast-forward" if _is_ancestor(repo, previous.main, main) else "rewritten-or-diverged"
            )
    return result


def analyze(repo: Path, main_revision: str, head_revision: str,
            previous: PreviousResult | None = None, retirement_status: dict | None = None) -> dict:
    started = time.monotonic_ns()
    repo = repo.resolve()
    main = _commit(repo, main_revision)
    head = _commit(repo, head_revision)
    merge_bases = _merge_bases(repo, main, head)
    if not merge_bases:
        raise PreflightError("current main and candidate head have no merge base")
    merge = _merge_tree(repo, main, head)
    changed = _changed_paths(repo, merge_bases, head)
    conflict_records, conflict_paths = _conflicts(merge)
    generated_changed = sorted(path for path in changed if path in GENERATED_RETIREMENT_PATHS)
    policy_changed = sorted(path for path in changed if path in RETIREMENT_POLICY_SCHEMA_PATHS)
    trust_changed = sorted(path for path in changed if path in RETIREMENT_TRUST_PATHS)
    generated_conflicts = sorted(
        record["path"] for record in conflict_paths
        if record["generated_or_integration_owned_retirement_artifact"]
    )
    policy_trust_conflicts = sorted(
        record["path"] for record in conflict_paths
        if record["retirement_policy_schema_or_trust_path"]
    )
    source_conflicts = sorted(
        record["path"] for record in conflict_paths if record["genuine_source_overlap"]
    )
    stale = not _is_ancestor(repo, main, head)
    attestation = {"verified": False, "reason": "No live trusted integration evidence supplied"}
    if generated_changed and merge.clean and retirement_status is not None:
        try:
            # This module is loaded beside the trusted preflight, never from the PR tree.
            with tempfile.TemporaryDirectory(prefix="preflight-attestation-") as temporary:
                status_path = Path(temporary) / "status.json"
                status_path.write_text(json.dumps(retirement_status))
                verified = retirement_gate.check_pull_request(
                    repo, main, head, main, status_path, False)
            if verified.get("mode") != "trusted-integration":
                raise retirement_gate.AdmissionError("head is not a trusted integration")
            attestation = {"verified": True, "record": verified}
        except (retirement_gate.AdmissionError, retirement_gate.integration.IntegrationError,
                ValueError, KeyError, TypeError) as error:
            attestation = {"verified": False, "reason": str(error)}
    if (generated_changed and not attestation["verified"]) or generated_conflicts:
        outcome_number = OUTCOME_GENERATED
        outcome_kind = "generated-integration-owned-workflow-violation"
        action = (
            "Remove generated retirement artifacts from the candidate. Do not hand-resolve hashes; "
            "let ephemeral validation and the serialized integration writer reconstruct them."
        )
    elif policy_trust_conflicts:
        outcome_number = OUTCOME_POLICY
        outcome_kind = "policy-schema-trust-boundary-overlap"
        action = (
            "Use the reviewed native-retirement bootstrap/policy transition path. Do not auto-resolve "
            "or let candidate-modified trust code approve its own result."
        )
    elif not merge.clean:
        outcome_number = OUTCOME_SOURCE
        outcome_kind = "genuine-source-overlap"
        action = (
            "Choose an intentional ordering, rebase, or explicit stack and resolve the named source paths; "
            "the preflight will not select either side."
        )
    else:
        outcome_number = OUTCOME_CLEAN
        outcome_kind = "clean-stale-requires-combined-head-validation" if stale else "clean-current-main"
        action = (
            "Validate the exact combined tree reported here before admission; rerun if main advances."
            if stale else
            "Continue ordinary validation on this exact head; rerun if main or the head advances."
        )
    previous_state = _previous_state(repo, previous, main, head)
    elapsed_ms = (time.monotonic_ns() - started) / 1_000_000.0
    report = {
        "schema": REPORT_SCHEMA,
        "authoritative_for_exact_identities": True,
        "main": {"sha": main, "tree": _tree(repo, main)},
        "head": {"sha": head, "tree": _tree(repo, head)},
        "merge_bases": [
            {"sha": base, "tree": _tree(repo, base)} for base in merge_bases
        ],
        "merge": {
            "clean": merge.clean,
            "state": "clean" if merge.clean else "conflicted",
            "combined_tree": merge.tree if merge.clean else None,
            "merge_tree_output_tree": merge.tree,
            "head_contains_current_main": not stale,
            "candidate_is_stale_against_current_main": stale,
            "conflicting_paths": [record["path"] for record in conflict_paths],
            "conflicts": conflict_records,
            "path_details": conflict_paths,
        },
        "trusted_retirement_integration": attestation,
        "candidate_changes": {
            "paths": [
                {"path": path, "statuses": list(statuses), **_path_flags(path)}
                for path, statuses in changed.items()
            ],
            "generated_or_integration_owned_retirement_paths": generated_changed,
            "retirement_policy_or_schema_paths": policy_changed,
            "retirement_trust_paths": trust_changed,
        },
        "overlap": {
            "generated_or_integration_owned_retirement_paths": generated_conflicts,
            "retirement_policy_schema_or_trust_paths": policy_trust_conflicts,
            "genuine_source_paths": source_conflicts,
        },
        "previous_authoritative_result": previous_state,
        "outcome": {
            "number": outcome_number,
            "kind": outcome_kind,
            "blocking": outcome_number in (OUTCOME_GENERATED, OUTCOME_SOURCE, OUTCOME_POLICY),
            "action": action,
        },
        "cost": {
            "elapsed_ms": round(elapsed_ms, 3),
            "method": "read-only git rev-parse/merge-base/diff/merge-tree plumbing",
            "configured_or_built_compiler": False,
        },
    }
    return report


def report_markdown(report: dict, title: str | None = None) -> str:
    heading = title or "Merge-conflict preflight"
    previous = report["previous_authoritative_result"]
    lines = [
        f"### {heading}",
        "",
        f"- Result: **{report['merge']['state']}**; classification "
        f"**{report['outcome']['number']} — {report['outcome']['kind']}**.",
        f"- Main: `{report['main']['sha']}` (tree `{report['main']['tree']}`).",
        f"- Head: `{report['head']['sha']}` (tree `{report['head']['tree']}`).",
        "- Merge base(s): " + ", ".join(
            f"`{entry['sha']}` (tree `{entry['tree']}`)" for entry in report["merge_bases"]
        ) + ".",
    ]
    if report["merge"]["combined_tree"]:
        lines.append(f"- Exact combined tree: `{report['merge']['combined_tree']}`.")
    if previous["available"]:
        advanced = previous["main_advanced_since_last_authoritative_result"]
        advanced_text = "yes" if advanced is True else "no" if advanced is False else "unknown"
        lines.append(
            f"- Main advanced since the last authoritative result for this head: **{advanced_text}** "
            f"({previous['advance_kind']}; previous `{previous['main_sha']}`)."
        )
    else:
        lines.append("- No prior authoritative result was available for this exact head.")
    paths = report["merge"]["path_details"]
    if paths:
        lines.extend(("", "| Conflicting path | Git classification | Retirement ownership |", "|---|---|---|"))
        for record in paths:
            ownership: list[str] = []
            if record["generated_or_integration_owned_retirement_artifact"]:
                ownership.append("generated/integration-owned")
            if record["retirement_policy_or_schema"]:
                ownership.append("policy/schema")
            if record["retirement_trust_path"]:
                ownership.append("trust path")
            if record["genuine_source_overlap"]:
                ownership.append("genuine source")
            lines.append(
                f"| `{record['path']}` | {', '.join(record['classifications']) or 'unmerged'} | "
                f"{', '.join(ownership)} |"
            )
    generated = report["candidate_changes"]["generated_or_integration_owned_retirement_paths"]
    if generated and not report["trusted_retirement_integration"]["verified"]:
        lines.extend(("", "Candidate-owned generated paths: " + ", ".join(f"`{path}`" for path in generated) + "."))
    lines.extend((
        "",
        "**Required response:** " + report["outcome"]["action"],
        "",
        f"Measured plumbing time: `{report['cost']['elapsed_ms']:.3f} ms`; no compiler configure, build, or test ran.",
    ))
    return "\n".join(lines) + "\n"


def _write_report(report: dict, output: Path | None, summary: Path | None,
                  title: str | None = None) -> None:
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if output is None:
        sys.stdout.write(serialized)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(serialized, encoding="utf-8")
    if summary is not None:
        summary.parent.mkdir(parents=True, exist_ok=True)
        with summary.open("a", encoding="utf-8") as stream:
            stream.write(report_markdown(report, title))


def _event(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PreflightError(f"cannot read GitHub event payload {path}: {error}") from error
    if not isinstance(value, dict):
        raise PreflightError("GitHub event payload is not an object")
    return value


def _fetch_ref(repo: Path, source: str, destination: str) -> str:
    _git(repo, "fetch", "--no-tags", "--force", "origin", f"+{source}:{destination}")
    return _commit(repo, destination)


def _target_url() -> str:
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com").rstrip("/")
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    return f"{server}/{repository}/actions/runs/{run_id}" if repository and run_id else server


def _pull_identity(value: dict) -> tuple[int, str, str]:
    try:
        number = int(value["number"])
        head = str(value["head"]["sha"])
        base = str(value["base"]["ref"])
    except (KeyError, TypeError, ValueError) as error:
        raise PreflightError("pull-request response omits number/head SHA/base ref") from error
    if not HEX_OBJECT.fullmatch(head):
        raise PreflightError(f"pull request #{number} has invalid head SHA {head!r}")
    return number, head, base


def _analyze_stable_pull(repo: Path, api: GitHubApi, number: int, context: str) -> tuple[dict, str]:
    report = None
    head = ""
    for _ in range(MAX_STABLE_ATTEMPTS):
        pull_before = api.pull_request(number)
        number_before, expected_head, base = _pull_identity(pull_before)
        if number_before != number:
            raise PreflightError(f"pull-request API returned #{number_before} while resolving #{number}")
        main_ref = "refs/merge-conflict-preflight/main"
        head_ref = f"refs/merge-conflict-preflight/pr-{number}"
        main = _fetch_ref(repo, f"refs/heads/{base}", main_ref)
        head = _fetch_ref(repo, f"refs/pull/{number}/head", head_ref)
        if head != expected_head:
            continue
        previous = api.previous_status(head, context)
        candidate = analyze(repo, main, head, previous)
        if candidate["candidate_changes"]["generated_or_integration_owned_retirement_paths"]:
            candidate = analyze(repo, main, head, previous, api.retirement_status(head))
        pull_after = api.pull_request(number)
        _, current_head, current_base = _pull_identity(pull_after)
        current_main = _fetch_ref(repo, f"refs/heads/{current_base}", main_ref)
        if current_head == head and current_base == base and current_main == main:
            report = candidate
            break
    if report is None:
        raise PreflightError(f"pull request #{number} or its base moved during {MAX_STABLE_ATTEMPTS} preflight attempts")
    return report, head


def _github_pull_event(repo: Path, api: GitHubApi, event: dict, report_dir: Path,
                       summary: Path | None, context: str) -> bool:
    pull = event.get("pull_request")
    if not isinstance(pull, dict) or not isinstance(pull.get("number"), int):
        raise PreflightError("pull request event omits pull_request.number")
    number = pull["number"]
    report, head = _analyze_stable_pull(repo, api, number, context)
    output = report_dir / f"pr-{number}-{head}.json"
    _write_report(report, output, summary, f"PR #{number} merge-conflict preflight")
    api.publish_status(head, report, context, _target_url())
    return bool(report["outcome"]["blocking"])



def _workflow_run_pull_number(event: dict) -> int:
    run = event.get("workflow_run")
    if not isinstance(run, dict) or run.get("event") != "pull_request":
        raise PreflightError("workflow_run event is not for a pull_request workflow")
    pulls = run.get("pull_requests")
    if (not isinstance(pulls, list) or len(pulls) != 1 or
            not isinstance(pulls[0], dict)):
        raise PreflightError("workflow_run event must identify exactly one pull request")
    number = pulls[0].get("number")
    if not isinstance(number, int) or number <= 0:
        raise PreflightError("workflow_run pull request has an invalid number")
    return number


def _github_workflow_run_event(repo: Path, api: GitHubApi, event: dict,
                               report_dir: Path, summary: Path | None,
                               context: str) -> bool:
    number = _workflow_run_pull_number(event)
    report, head = _analyze_stable_pull(repo, api, number, context)
    output = report_dir / f"pr-{number}-{head}.json"
    _write_report(report, output, summary, f"PR #{number} merge-conflict preflight")
    api.publish_status(head, report, context, _target_url())
    return bool(report["outcome"]["blocking"])

def _github_push_event(repo: Path, api: GitHubApi, event: dict, report_dir: Path,
                       summary: Path | None, context: str) -> bool:
    repository = event.get("repository")
    default_branch = repository.get("default_branch") if isinstance(repository, dict) else None
    if not isinstance(default_branch, str) or not default_branch:
        default_branch = "main"
    pulls = api.open_pull_requests(default_branch)
    blocking_count = 0
    if summary is not None:
        summary.parent.mkdir(parents=True, exist_ok=True)
        with summary.open("a", encoding="utf-8") as stream:
            stream.write(f"## Default-branch conflict refresh for {len(pulls)} open PR(s)\n\n")
    for pull in pulls:
        number, _, base = _pull_identity(pull)
        if base != default_branch:
            continue
        report, head = _analyze_stable_pull(repo, api, number, context)
        output = report_dir / f"pr-{number}-{head}.json"
        _write_report(report, output, summary, f"PR #{number}")
        api.publish_status(head, report, context, _target_url())
        blocking_count += int(report["outcome"]["blocking"])
    if summary is not None:
        with summary.open("a", encoding="utf-8") as stream:
            stream.write(
                f"\nRefresh completed; {blocking_count} PR(s) received a blocking preflight status. "
                "Open-PR conflicts do not make the new main commit itself fail.\n"
            )
    return False


def _github_merge_group_event(repo: Path, api: GitHubApi, event: dict, report_dir: Path,
                              summary: Path | None, context: str) -> bool:
    group = event.get("merge_group")
    if not isinstance(group, dict):
        raise PreflightError("merge_group event omits merge_group")
    head_sha = group.get("head_sha")
    head_ref = group.get("head_ref")
    base_ref = group.get("base_ref")
    if not isinstance(head_sha, str) or not HEX_OBJECT.fullmatch(head_sha):
        raise PreflightError("merge_group event has an invalid head_sha")
    if not isinstance(head_ref, str) or not head_ref.startswith("refs/"):
        raise PreflightError("merge_group event has an invalid head_ref")
    if not isinstance(base_ref, str) or not base_ref.startswith("refs/heads/"):
        raise PreflightError("merge_group event has an invalid base_ref")
    local_main = "refs/merge-conflict-preflight/main"
    local_head = "refs/merge-conflict-preflight/merge-group"
    main = _fetch_ref(repo, base_ref, local_main)
    fetched_head = _fetch_ref(repo, head_ref, local_head)
    if fetched_head != head_sha:
        raise PreflightError(
            f"merge-group head moved: event {head_sha}, fetched {fetched_head}; refusing a stale result"
        )
    report = analyze(repo, main, fetched_head, api.previous_status(fetched_head, context))
    current_main = _fetch_ref(repo, base_ref, local_main)
    if current_main != main:
        report = analyze(repo, current_main, fetched_head, api.previous_status(fetched_head, context))
    output = report_dir / f"merge-group-{fetched_head}.json"
    _write_report(report, output, summary, "Merge-group conflict preflight")
    api.publish_status(fetched_head, report, context, _target_url())
    return bool(report["outcome"]["blocking"])


def github_event(repo: Path, event_path: Path, repository: str, report_dir: Path,
                 summary: Path | None, context: str, api_url: str) -> int:
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    if not event_name:
        raise PreflightError("GITHUB_EVENT_NAME is required")
    api = GitHubApi(repository, os.environ.get("GITHUB_TOKEN", ""), api_url)
    event = _event(event_path)
    if event_name == "workflow_run":
        blocking = _github_workflow_run_event(repo, api, event, report_dir, summary, context)
    elif event_name in ("push", "workflow_dispatch"):
        blocking = _github_push_event(repo, api, event, report_dir, summary, context)
    elif event_name == "merge_group":
        blocking = _github_merge_group_event(repo, api, event, report_dir, summary, context)
    else:
        raise PreflightError(f"unsupported GitHub event: {event_name!r}")
    return 1 if blocking else 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    analyze_parser = subparsers.add_parser("analyze", help="analyze two exact local commit identities")
    analyze_parser.add_argument("--repo", type=Path, default=Path("."))
    analyze_parser.add_argument("--main", required=True)
    analyze_parser.add_argument("--head", required=True)
    analyze_parser.add_argument("--previous-main")
    analyze_parser.add_argument("--previous-head")
    analyze_parser.add_argument("--previous-outcome", type=int, choices=(1, 2, 3, 4))
    analyze_parser.add_argument("--output", type=Path)
    analyze_parser.add_argument("--summary", type=Path)
    analyze_parser.add_argument("--fail-on-blocking", action="store_true")

    github_parser = subparsers.add_parser("github-event", help="handle a trusted GitHub Actions event")
    github_parser.add_argument("--repo", type=Path, default=Path("."))
    github_parser.add_argument("--event-path", type=Path, required=True)
    github_parser.add_argument("--repository", required=True)
    github_parser.add_argument("--report-dir", type=Path, required=True)
    github_parser.add_argument("--summary", type=Path)
    github_parser.add_argument("--context", default=STATUS_CONTEXT)
    github_parser.add_argument("--api-url", default=os.environ.get("GITHUB_API_URL", "https://api.github.com"))
    return parser


def main() -> int:
    parser = _parser()
    arguments = parser.parse_args()
    try:
        if arguments.command == "analyze":
            previous = None
            if bool(arguments.previous_main) != bool(arguments.previous_head):
                raise PreflightError("--previous-main and --previous-head must be supplied together")
            if arguments.previous_main:
                previous = PreviousResult(
                    main=arguments.previous_main,
                    head=arguments.previous_head,
                    outcome=arguments.previous_outcome,
                )
            report = analyze(arguments.repo, arguments.main, arguments.head, previous)
            _write_report(report, arguments.output, arguments.summary)
            status = 1 if arguments.fail_on_blocking and report["outcome"]["blocking"] else 0
        else:
            status = github_event(
                arguments.repo,
                arguments.event_path,
                arguments.repository,
                arguments.report_dir,
                arguments.summary,
                arguments.context,
                arguments.api_url,
            )
    except PreflightError as error:
        print(f"merge-conflict-preflight: error: {error}", file=sys.stderr)
        status = 2
    return status


if __name__ == "__main__":
    raise SystemExit(main())
