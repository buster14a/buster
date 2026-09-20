#!/usr/bin/env python3
"""Trusted single-writer integration for native-retirement generated state.

Feature validation and publication use the same reviewed rebinder from the
current default-branch revision. Candidate code is never imported or executed
by this module. Publication is one lease-guarded ref update after a separate
read-only job validates the exact final tree.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request


EVIDENCE_SCHEMA = "buster-native-retirement-integration-evidence-v1"
STAGE_SCHEMA = "buster-native-retirement-integration-stage-v1"
CLASSIFICATION_SCHEMA = "buster-native-retirement-candidate-classification-v1"
GENERATED_PATHS = frozenset((
    "docs/native-retirement-repository-sources-v1.json",
    "tools/native_retirement_dependency_binding.generated.h",
))
RESERVED_MATERIALIZATION_ROOTS = (
    ".native-retirement-trusted",
    "external",
)
TRUST_IMPLEMENTATION_PATHS = frozenset((
    ".github/workflows/native-retirement-contract.yml",
    ".github/workflows/native-retirement-integration.yml",
    ".github/workflows/native-retirement-rebind.yml",
    ".gitattributes",
    "tools/native_retirement_contract.py",
    "tools/native_retirement_dependency_binding.py",
    "tools/native_retirement_external.py",
    "tools/native_retirement_integration.py",
    "tools/native_retirement_materializer.py",
    "tools/native_retirement_rebind.py",
    "tools/native_retirement_rebind_contract.py",
    "tools/native_retirement_sdks.py",
))
POLICY_SCHEMA_PATHS = frozenset((
    "docs/native-retirement-dependencies-legacy-v1.json",
    "docs/native-retirement-dependencies-v1.json",
    "docs/native-retirement-sdks-v1.json",
    "tools/native_retirement_census.c",
))
TRANSITION_KINDS = ("ordinary", "bootstrap", "policy")
TRUSTED_FILE_PATHS = (
    "tools/native_retirement_contract.py",
    "tools/native_retirement_dependency_binding.py",
    "tools/native_retirement_external.py",
    "tools/native_retirement_integration.py",
    "tools/native_retirement_materializer.py",
    "tools/native_retirement_rebind.py",
    "tools/native_retirement_rebind_contract.py",
    "tools/native_retirement_sdks.py",
)
STALE_MAIN_EXIT = 75
STALE_HEAD_EXIT = 76
MAX_API_PAGES = 10
SHA256_FIELDS = ("policy_sha256", "receipt_sha256", "project_sha256", "ledger_sha256")
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")


class IntegrationError(Exception):
    """A fail-closed candidate, repository, or publication error."""


class StaleMain(IntegrationError):
    """The default branch moved after preparation."""


class StaleHead(IntegrationError):
    """The pull-request head no longer matches the reviewed immutable head."""


@dataclass(frozen=True)
class Classification:
    kind: str
    changed_paths: tuple[str, ...]
    generated_paths: tuple[str, ...]
    implementation_paths: tuple[str, ...]
    policy_schema_paths: tuple[str, ...]

    def as_dict(self) -> dict:
        return {
            "schema": CLASSIFICATION_SCHEMA,
            "kind": self.kind,
            "changed_paths": list(self.changed_paths),
            "generated_paths": list(self.generated_paths),
            "implementation_paths": list(self.implementation_paths),
            "policy_schema_paths": list(self.policy_schema_paths),
        }


def _canonical_path(path: str) -> str:
    candidate = PurePosixPath(path)
    if candidate.is_absolute() or not candidate.parts or any(
            part in ("", ".", "..") for part in candidate.parts):
        raise IntegrationError(f"non-canonical changed path: {path!r}")
    return candidate.as_posix()


def reject_reserved_materialization_roots(root: Path) -> None:
    """Reject candidate-controlled destinations used by trusted checkouts.

    actions/checkout follows a pre-existing directory symlink while preparing
    its destination. An untrusted candidate must therefore not supply any
    top-level path into which the workflows later place trusted or pinned
    inputs. lstat() is intentional so broken symlinks also fail closed.
    """
    root = root.resolve()
    for relative in RESERVED_MATERIALIZATION_ROOTS:
        path = root / PurePosixPath(relative)
        try:
            path.lstat()
        except FileNotFoundError:
            continue
        raise IntegrationError(
            f"candidate controls reserved materialization root {relative!r}; "
            "remove it so trusted and pinned inputs can be checked out safely"
        )


def classify_paths(paths) -> Classification:
    changed = tuple(sorted({_canonical_path(path) for path in paths}))
    generated = tuple(path for path in changed if path in GENERATED_PATHS)
    implementation = tuple(path for path in changed if path in TRUST_IMPLEMENTATION_PATHS)
    policy_schema = tuple(path for path in changed if path in POLICY_SCHEMA_PATHS)
    if generated:
        kind = "generated-edit"
    elif implementation and policy_schema:
        kind = "split-required"
    elif implementation:
        kind = "bootstrap"
    elif policy_schema:
        kind = "policy"
    else:
        kind = "ordinary"
    return Classification(kind, changed, generated, implementation, policy_schema)


def enforce_classification(classification: Classification, requested_kind: str | None,
                           authorized: bool) -> None:
    if classification.generated_paths:
        joined = ", ".join(classification.generated_paths)
        raise IntegrationError(
            "integration-owned generated artifacts were edited manually: " + joined +
            "; remove them from the candidate and let ephemeral validation/the single writer regenerate them"
        )
    if classification.kind == "split-required":
        raise IntegrationError(
            "candidate changes both trusted rebinding implementation and policy/schema/consumer state; "
            "split it into a backwards-compatible bootstrap followed by a separately reviewed policy transition"
        )
    if requested_kind is not None and requested_kind != classification.kind:
        raise IntegrationError(
            f"requested transition {requested_kind!r} does not match classified candidate {classification.kind!r}"
        )
    if classification.kind != "ordinary" and not authorized:
        raise IntegrationError(
            f"{classification.kind} transition requires the trusted integration workflow and maintainer authorization"
        )


def _git(repo: Path, *arguments: str, input_text: str | None = None, check: bool = True,
         extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    environment = os.environ.copy()
    environment.update({
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_TERMINAL_PROMPT": "0",
    })
    if extra_env:
        environment.update(extra_env)
    command = [
        "git", "-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false",
        "-C", os.fspath(repo), *arguments,
    ]
    result = subprocess.run(command, input=input_text, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=environment)
    if check and result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        raise IntegrationError("git command failed: " + " ".join(arguments) + ": " + detail)
    return result


def _commit(repo: Path, revision: str) -> str:
    value = _git(repo, "rev-parse", "--verify", revision + "^{commit}").stdout.strip()
    if len(value) != 40:
        raise IntegrationError(f"invalid commit identity for {revision!r}: {value!r}")
    return value


def _tree(repo: Path, revision: str) -> str:
    value = _git(repo, "rev-parse", "--verify", revision + "^{tree}").stdout.strip()
    if len(value) != 40:
        raise IntegrationError(f"invalid tree identity for {revision!r}: {value!r}")
    return value


def changed_paths(repo: Path, base: str, head: str) -> tuple[str, ...]:
    # Disable rename collapsing so deleting or moving a trusted/policy path
    # cannot be classified only by its untrusted destination spelling.
    result = _git(repo, "diff", "--name-only", "--no-renames", "-z",
                  base + "..." + head)
    return tuple(path for path in result.stdout.split("\0") if path)


def classify_candidate(repo: Path, base: str, head: str) -> Classification:
    base_commit = _commit(repo, base)
    head_commit = _commit(repo, head)
    return classify_paths(changed_paths(repo, base_commit, head_commit))


def stage(repo: Path, worktree: Path, base: str, head: str, requested_kind: str,
          authorized: bool) -> dict:
    repo = repo.resolve()
    worktree = worktree.resolve()
    base_commit = _commit(repo, base)
    head_commit = _commit(repo, head)
    classification = classify_candidate(repo, base_commit, head_commit)
    enforce_classification(classification, requested_kind, authorized)
    if worktree.exists():
        raise IntegrationError(f"integration worktree already exists: {worktree}")
    worktree.parent.mkdir(parents=True, exist_ok=True)
    _git(repo, "worktree", "add", "--detach", os.fspath(worktree), base_commit)
    try:
        merge = _git(worktree, "merge", "--no-commit", "--no-ff", head_commit, check=False)
        if merge.returncode != 0:
            detail = merge.stderr.strip() or merge.stdout.strip() or "merge conflict"
            raise IntegrationError("candidate does not form an exact conflict-free combined tree: " + detail)
        actual_merge_head = _git(worktree, "rev-parse", "--verify", "MERGE_HEAD").stdout.strip()
        if actual_merge_head != head_commit:
            raise IntegrationError("combined tree MERGE_HEAD does not equal the immutable candidate head")
        reject_reserved_materialization_roots(worktree)
        combined_tree = _git(worktree, "write-tree").stdout.strip()
        report = {
            "schema": STAGE_SCHEMA,
            "base": {"sha": base_commit, "tree": _tree(repo, base_commit)},
            "candidate": {"sha": head_commit, "tree": _tree(repo, head_commit)},
            "combined_tree": combined_tree,
            "classification": classification.as_dict(),
        }
        return report
    except Exception:
        _git(repo, "worktree", "remove", "--force", os.fspath(worktree), check=False)
        shutil.rmtree(worktree, ignore_errors=True)
        raise


def _sha256(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise IntegrationError(f"authority file is absent or non-regular: {path}")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _require_hex(value, pattern, label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise IntegrationError(f"{label} is not a canonical digest: {value!r}")
    return value


def _validate_rebind_report(report: dict, label: str,
                            expected_statuses: tuple[str, ...]) -> None:
    if not isinstance(report, dict) or report.get("status") not in expected_statuses:
        raise IntegrationError(f"{label} has unexpected status: {report.get('status')!r}")
    identities = report.get("identities")
    if not isinstance(identities, dict):
        raise IntegrationError(f"{label} has no generated identity map")
    for name in SHA256_FIELDS:
        _require_hex(identities.get(name), HEX64, f"{label} {name}")
    _require_hex(report.get("snapshot_sha256"), HEX64, f"{label} snapshot_sha256")
    _require_hex(report.get("resolved_descriptor_sha256"), HEX64,
                 f"{label} resolved_descriptor_sha256")


def _run_rebinder(trusted_root: Path, worktree: Path, mode: str) -> dict:
    script = trusted_root / "tools/native_retirement_rebind.py"
    result = subprocess.run(
        [sys.executable, os.fspath(script), mode, "--repo-root", os.fspath(worktree)],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        cwd=trusted_root,
        env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        raise IntegrationError(f"trusted rebinder {mode} failed: {detail}")
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise IntegrationError(f"trusted rebinder emitted invalid JSON: {error}") from error
    return report


def finalize(repo: Path, worktree: Path, trusted_root: Path, trusted_revision: str,
             staged: dict) -> dict:
    repo = repo.resolve()
    worktree = worktree.resolve()
    trusted_root = trusted_root.resolve()
    if staged.get("schema") != STAGE_SCHEMA:
        raise IntegrationError("staged integration input has the wrong schema")
    base = _require_hex(staged["base"]["sha"], HEX40, "staged base")
    head = _require_hex(staged["candidate"]["sha"], HEX40, "staged candidate")
    if _commit(worktree, "HEAD") != base:
        raise IntegrationError("integration worktree base moved after staging")
    if _git(worktree, "rev-parse", "--verify", "MERGE_HEAD").stdout.strip() != head:
        raise IntegrationError("integration worktree candidate moved after staging")
    if (_tree(repo, base) != staged["base"]["tree"] or
            _tree(repo, head) != staged["candidate"]["tree"]):
        raise IntegrationError("staged base/candidate tree identity changed")
    if _git(worktree, "write-tree").stdout.strip() != staged["combined_tree"]:
        raise IntegrationError("combined tree changed after staging and before trusted reconstruction")

    refresh = _run_rebinder(trusted_root, worktree, "refresh")
    _validate_rebind_report(refresh, "trusted refresh", ("current", "refreshed"))
    changed = refresh.get("files_changed")
    if changed is not None and (not isinstance(changed, list) or
                                not set(changed) <= GENERATED_PATHS):
        raise IntegrationError("trusted refresh reported a non-generated output path")
    check = _run_rebinder(trusted_root, worktree, "check")
    _validate_rebind_report(check, "trusted check", ("current",))
    for name in ("identities", "snapshot_sha256", "resolved_descriptor_sha256"):
        if refresh.get(name) != check.get(name):
            raise IntegrationError(f"trusted refresh/check disagree on {name}")
    _git(worktree, "add", "--", *sorted(GENERATED_PATHS))
    unstaged = tuple(
        path for path in _git(worktree, "diff", "--name-only", "-z").stdout.split("\0")
        if path
    )
    if unstaged:
        raise IntegrationError("trusted rebinding modified non-indexed paths: " +
                               ", ".join(unstaged))
    final_tree = _git(worktree, "write-tree").stdout.strip()
    first_generated = {
        relative: _sha256(worktree / PurePosixPath(relative))
        for relative in sorted(GENERATED_PATHS)
    }

    second = _run_rebinder(trusted_root, worktree, "refresh")
    _validate_rebind_report(second, "trusted idempotence refresh", ("current",))
    second_unstaged = tuple(
        path for path in _git(worktree, "diff", "--name-only", "-z").stdout.split("\0")
        if path
    )
    second_generated = {
        relative: _sha256(worktree / PurePosixPath(relative))
        for relative in sorted(GENERATED_PATHS)
    }
    second_tree = _git(worktree, "write-tree").stdout.strip()
    if (second_unstaged or second_generated != first_generated or second_tree != final_tree or
            any(second.get(name) != check.get(name) for name in
                ("identities", "snapshot_sha256", "resolved_descriptor_sha256"))):
        raise IntegrationError(
            "immutable combined tree did not regenerate byte-identical native-retirement state"
        )

    trusted_commit = _commit(trusted_root, trusted_revision)
    if _commit(trusted_root, "HEAD") != trusted_commit:
        raise IntegrationError("trusted checkout is not at the recorded trusted revision")
    trusted_files = {}
    next_trusted_files = {}
    for relative in TRUSTED_FILE_PATHS:
        trusted_files[relative] = _sha256(trusted_root / PurePosixPath(relative))
        next_trusted_files[relative] = _sha256(worktree / PurePosixPath(relative))
    generated = {
        relative: _sha256(worktree / PurePosixPath(relative))
        for relative in sorted(GENERATED_PATHS)
    }

    return {
        "schema": EVIDENCE_SCHEMA,
        "base": staged["base"],
        "candidate": staged["candidate"],
        "combined_tree": staged["combined_tree"],
        "final_tree": final_tree,
        "classification": staged["classification"],
        "trusted": {
            "revision": trusted_commit,
            "tree": _tree(trusted_root, trusted_commit),
            "files": trusted_files,
        },
        "next_trusted": {
            "tree": final_tree,
            "files": next_trusted_files,
        },
        "generated": generated,
        "rebind": {
            "identities": refresh.get("identities"),
            "snapshot_sha256": refresh.get("snapshot_sha256"),
            "resolved_descriptor_sha256": refresh.get("resolved_descriptor_sha256"),
        },
    }


def canonical_json(value: dict) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def evidence_digest(evidence: dict) -> str:
    return hashlib.sha256(canonical_json(evidence).encode("utf-8")).hexdigest()


def compare_evidence(expected: dict, actual: dict) -> None:
    if expected != actual:
        raise IntegrationError(
            "independent reconstruction disagrees with prepared evidence: expected " +
            evidence_digest(expected) + ", got " + evidence_digest(actual)
        )


def _validate_evidence(evidence: dict) -> None:
    if not isinstance(evidence, dict) or evidence.get("schema") != EVIDENCE_SCHEMA:
        raise IntegrationError("publication evidence has the wrong schema")
    for label, record in (("base", evidence.get("base")),
                          ("candidate", evidence.get("candidate"))):
        if not isinstance(record, dict):
            raise IntegrationError(f"publication evidence has no {label} record")
        _require_hex(record.get("sha"), HEX40, f"evidence {label} sha")
        _require_hex(record.get("tree"), HEX40, f"evidence {label} tree")
    for label in ("combined_tree", "final_tree"):
        _require_hex(evidence.get(label), HEX40, "evidence " + label)
    classification = evidence.get("classification")
    if (not isinstance(classification, dict) or
            classification.get("schema") != CLASSIFICATION_SCHEMA or
            classification.get("kind") not in TRANSITION_KINDS):
        raise IntegrationError("publication evidence has an invalid candidate classification")
    trusted = evidence.get("trusted")
    next_trusted = evidence.get("next_trusted")
    if not isinstance(trusted, dict) or not isinstance(next_trusted, dict):
        raise IntegrationError("publication evidence has incomplete trusted authority records")
    _require_hex(trusted.get("revision"), HEX40, "evidence trusted revision")
    _require_hex(trusted.get("tree"), HEX40, "evidence trusted tree")
    _require_hex(next_trusted.get("tree"), HEX40, "evidence next trusted tree")
    if (trusted.get("revision") != evidence["base"]["sha"] or
            trusted.get("tree") != evidence["base"]["tree"]):
        raise IntegrationError("trusted authority is not the exact recorded current-main base")
    if next_trusted.get("tree") != evidence.get("final_tree"):
        raise IntegrationError("next trusted tree does not equal the exact final tree")
    file_sets = (
        ("trusted", trusted.get("files"), frozenset(TRUSTED_FILE_PATHS)),
        ("next trusted", next_trusted.get("files"), frozenset(TRUSTED_FILE_PATHS)),
        ("generated", evidence.get("generated"), GENERATED_PATHS),
    )
    for label, files, expected_paths in file_sets:
        if not isinstance(files, dict) or set(files) != expected_paths:
            raise IntegrationError(f"publication evidence has the wrong {label} file identity set")
        for path, digest in files.items():
            _canonical_path(path)
            _require_hex(digest, HEX64, f"evidence {label} file {path}")
    rebind = evidence.get("rebind")
    if not isinstance(rebind, dict) or not isinstance(rebind.get("identities"), dict):
        raise IntegrationError("publication evidence has no rebind identities")
    for name in SHA256_FIELDS:
        _require_hex(rebind["identities"].get(name), HEX64, "evidence rebind " + name)
    _require_hex(rebind.get("snapshot_sha256"), HEX64, "evidence rebind snapshot_sha256")
    _require_hex(rebind.get("resolved_descriptor_sha256"), HEX64,
                 "evidence rebind resolved_descriptor_sha256")


def _remote_ref(repo: Path, remote: str, reference: str) -> str | None:
    result = _git(repo, "ls-remote", "--refs", remote, reference)
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        return None
    if len(lines) != 1:
        raise IntegrationError(f"remote reference is ambiguous: {reference}")
    sha, name = lines[0].split("\t", 1)
    if name != reference or len(sha) != 40:
        raise IntegrationError(f"malformed remote reference result for {reference}")
    return sha


def publish(repo: Path, remote: str, branch: str, pull_request: int, evidence: dict,
            message: str) -> dict:
    repo = repo.resolve()
    _validate_evidence(evidence)
    base = evidence["base"]["sha"]
    head = evidence["candidate"]["sha"]
    final_tree = evidence["final_tree"]
    main_ref = "refs/heads/" + branch
    pull_ref = "refs/pull/" + str(pull_request) + "/head"
    if (_tree(repo, base) != evidence["base"]["tree"] or
            _tree(repo, head) != evidence["candidate"]["tree"] or
            _tree(repo, evidence["combined_tree"]) != evidence["combined_tree"] or
            _tree(repo, final_tree) != final_tree):
        raise IntegrationError(
            "publication repository does not contain the recorded immutable tree objects"
        )
    if _remote_ref(repo, remote, main_ref) != base:
        raise StaleMain("main moved after preparation; discard this result and reconstruct")
    if _remote_ref(repo, remote, pull_ref) != head:
        raise StaleHead("candidate head moved after review; dispatch again with the new exact head")
    commit_message = (
        message.rstrip() + "\n\n" +
        "Native-retirement-integration-evidence: " + evidence_digest(evidence) + "\n" +
        "Native-retirement-base: " + base + "\n" +
        "Native-retirement-candidate: " + head + "\n" +
        "Native-retirement-final-tree: " + final_tree + "\n"
    )
    commit = _git(repo, "commit-tree", final_tree, "-p", base, "-p", head,
                  input_text=commit_message, extra_env={
                      "GIT_AUTHOR_NAME": "github-actions[bot]",
                      "GIT_AUTHOR_EMAIL": "41898282+github-actions[bot]@users.noreply.github.com",
                      "GIT_COMMITTER_NAME": "github-actions[bot]",
                      "GIT_COMMITTER_EMAIL": "41898282+github-actions[bot]@users.noreply.github.com",
                  }).stdout.strip()

    # The mutation is one ref update, guarded by a second immediate base/head check.
    if _remote_ref(repo, remote, main_ref) != base:
        raise StaleMain("main moved immediately before publication; discard and reconstruct")
    if _remote_ref(repo, remote, pull_ref) != head:
        raise StaleHead("candidate head moved immediately before publication")
    pushed = _git(
        repo, "push", "--porcelain", "--force-with-lease=" + main_ref + ":" + base,
        remote, commit + ":" + main_ref, check=False,
    )
    if pushed.returncode != 0:
        current_main = _remote_ref(repo, remote, main_ref)
        current_head = _remote_ref(repo, remote, pull_ref)
        if current_main != base:
            raise StaleMain(
                "main moved during the lease-guarded publication; discard and reconstruct"
            )
        if current_head != head:
            raise StaleHead("candidate head moved during lease-guarded publication")
        detail = pushed.stderr.strip() or pushed.stdout.strip() or f"exit {pushed.returncode}"
        raise IntegrationError("lease-guarded publication failed without ref movement: " + detail)
    published = _remote_ref(repo, remote, main_ref)
    if published != commit:
        raise IntegrationError(
            "publication returned success but main does not equal the integration commit"
        )
    return {
        "status": "published",
        "commit": commit,
        "tree": final_tree,
        "evidence_sha256": evidence_digest(evidence),
    }


class GitHub:
    def __init__(self, repository: str, token: str):
        if repository.count("/") != 1 or not token:
            raise IntegrationError("GitHub repository/token is missing")
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
                break
        else:
            raise IntegrationError("GitHub pagination limit reached")
        return result

    def permission(self, login: str) -> str:
        data = self.request(
            "collaborators/" + urllib.parse.quote(login, safe="") + "/permission"
        )
        role_name = data.get("role_name")
        if isinstance(role_name, str) and role_name:
            return role_name
        permission = data.get("permission")
        if not isinstance(permission, str) or not permission:
            raise IntegrationError("GitHub returned no repository role for " + login)
        return permission


def authorize(api: GitHub, pull_request: int, expected_head: str, transition_kind: str,
              actor: str) -> dict:
    _require_hex(expected_head, HEX40, "expected PR head")
    pr = api.request("pulls/" + str(pull_request))
    if (pr["state"] != "open" or pr["draft"] or pr["base"]["ref"] != "main" or
            pr["base"]["repo"]["full_name"] != api.repository or
            pr["head"]["repo"]["full_name"] != api.repository or
            pr["head"]["sha"] != expected_head):
        raise IntegrationError(
            "PR is closed, draft, external, not based on main, or head SHA changed"
        )
    actor_permission = api.permission(actor)
    allowed = (("write", "maintain", "admin") if transition_kind == "ordinary"
               else ("maintain", "admin"))
    if actor_permission not in allowed:
        raise IntegrationError(
            f"dispatcher {actor!r} lacks permission for {transition_kind} integration"
        )

    reviewers = []
    if transition_kind != "ordinary":
        latest = {}
        for review in api.all("pulls/" + str(pull_request) + "/reviews"):
            login = review["user"]["login"]
            review_id = review.get("id")
            if not isinstance(review_id, int):
                raise IntegrationError("GitHub returned a review without a numeric identity")
            previous = latest.get(login)
            if previous is None or review_id > previous[0]:
                latest[login] = (review_id, review["state"], review.get("commit_id"))
        for login, (_review_id, state, commit_id) in sorted(latest.items()):
            if (login != pr["user"]["login"] and state == "APPROVED" and
                    commit_id == expected_head and
                    api.permission(login) in ("maintain", "admin")):
                reviewers.append(login)
        if not reviewers:
            raise IntegrationError(
                transition_kind +
                " transition requires an approving maintainer other than the PR author "
                "on the exact immutable candidate head"
            )
    return {
        "number": pull_request,
        "head": expected_head,
        "author": pr["user"]["login"],
        "dispatcher": actor,
        "dispatcher_permission": actor_permission,
        "transition_kind": transition_kind,
        "maintainer_approvals": reviewers,
    }


def _read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise IntegrationError(f"cannot read JSON {path}: {error}") from error


def _write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(canonical_json(value))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    classify = subparsers.add_parser("classify")
    classify.add_argument("--repo-root", type=Path, required=True)
    classify.add_argument("--base", required=True)
    classify.add_argument("--head", required=True)

    stage_parser = subparsers.add_parser("stage")
    stage_parser.add_argument("--repo-root", type=Path, required=True)
    stage_parser.add_argument("--worktree", type=Path, required=True)
    stage_parser.add_argument("--base", required=True)
    stage_parser.add_argument("--head", required=True)
    stage_parser.add_argument("--transition-kind", choices=TRANSITION_KINDS,
                              required=True)
    stage_parser.add_argument("--authorized", action="store_true")
    stage_parser.add_argument("--output", type=Path, required=True)

    finalize_parser = subparsers.add_parser("finalize")
    finalize_parser.add_argument("--repo-root", type=Path, required=True)
    finalize_parser.add_argument("--worktree", type=Path, required=True)
    finalize_parser.add_argument("--trusted-root", type=Path, required=True)
    finalize_parser.add_argument("--trusted-revision", required=True)
    finalize_parser.add_argument("--staged", type=Path, required=True)
    finalize_parser.add_argument("--output", type=Path, required=True)

    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("--expected", type=Path, required=True)
    compare_parser.add_argument("--actual", type=Path, required=True)

    publish_parser = subparsers.add_parser("publish")
    publish_parser.add_argument("--repo-root", type=Path, required=True)
    publish_parser.add_argument("--remote", default="origin")
    publish_parser.add_argument("--branch", default="main")
    publish_parser.add_argument("--pull-request", type=int, required=True)
    publish_parser.add_argument("--evidence", type=Path, required=True)
    publish_parser.add_argument("--message", required=True)

    authorize_parser = subparsers.add_parser("authorize")
    authorize_parser.add_argument("--pull-request", type=int, required=True)
    authorize_parser.add_argument("--expected-head", required=True)
    authorize_parser.add_argument("--transition-kind", choices=TRANSITION_KINDS,
                                  required=True)
    return parser


def main(argv=None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "classify":
            reject_reserved_materialization_roots(arguments.repo_root)
            classification = classify_candidate(
                arguments.repo_root, arguments.base, arguments.head
            )
            if classification.kind in ("generated-edit", "split-required"):
                enforce_classification(classification, None, True)
            print(canonical_json(classification.as_dict()), end="")
        elif arguments.command == "stage":
            report = stage(arguments.repo_root, arguments.worktree, arguments.base,
                           arguments.head, arguments.transition_kind,
                           arguments.authorized)
            _write_json(arguments.output, report)
            print(canonical_json(report), end="")
        elif arguments.command == "finalize":
            report = finalize(arguments.repo_root, arguments.worktree,
                              arguments.trusted_root, arguments.trusted_revision,
                              _read_json(arguments.staged))
            _write_json(arguments.output, report)
            print(canonical_json(report), end="")
        elif arguments.command == "compare":
            expected = _read_json(arguments.expected)
            actual = _read_json(arguments.actual)
            compare_evidence(expected, actual)
            print(canonical_json({
                "status": "equal",
                "evidence_sha256": evidence_digest(actual),
            }), end="")
        elif arguments.command == "publish":
            report = publish(arguments.repo_root, arguments.remote, arguments.branch,
                             arguments.pull_request, _read_json(arguments.evidence),
                             arguments.message)
            print(canonical_json(report), end="")
        elif arguments.command == "authorize":
            api = GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"])
            report = authorize(api, arguments.pull_request, arguments.expected_head,
                               arguments.transition_kind, os.environ["GITHUB_ACTOR"])
            print(canonical_json(report), end="")
        return 0
    except StaleMain as error:
        print("native-retirement integration stale main: " + str(error), file=sys.stderr)
        return STALE_MAIN_EXIT
    except StaleHead as error:
        print("native-retirement integration stale head: " + str(error), file=sys.stderr)
        return STALE_HEAD_EXIT
    except (IntegrationError, KeyError, OSError, TypeError, ValueError) as error:
        print("native-retirement integration failure: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
