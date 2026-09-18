#!/usr/bin/env python3
"""Fail-closed provenance and selection for the CI Clang analyzer reference.

The candidate and historical build drivers are compiled with Clang dependency
files.  This helper normalizes those compiler-selected repository dependencies,
verifies their materialized bytes against exact Git blob identities, and binds
the comparison policy to a versioned manifest.  It never uses a changed-file
heuristic or reads dependency identities from an uncommitted tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile
from typing import Any

MANIFEST_SCHEMA = "BUSTER_ANALYZER_DRIVER_PROVENANCE_V1"
SELECTION_SCHEMA = "BUSTER_ANALYZER_COMPARISON_SELECTION_V2"
ROOT_SOURCE = "build.c"
POLICY_INPUTS = (".github/workflows/ci.yml", "tools/analyzer_reference.py")
COMPILE_PROFILE = {
    "compiler": "clang",
    "arguments": [
        "-Isrc",
        "-I.",
        "-Wall",
        "-Werror",
        "-Wno-unused-function",
        "-Wno-unused-variable",
        "-fwrapv",
        "-fno-strict-aliasing",
        "-funsigned-char",
        "-MMD",
        "-MF",
        "<dependency-file>",
        "build.c",
        "-o",
        "<driver-output>",
    ],
}
HEX_OBJECT = re.compile(r"[0-9a-f]{40}(?:[0-9a-f]{24})?\Z")
HEX_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
SAFE_EVENT = re.compile(r"[A-Za-z0-9_.-]+\Z")


class ProvenanceError(RuntimeError):
    pass


def run_git(repository: Path, *arguments: str) -> bytes:
    process = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.returncode != 0:
        detail = process.stderr.decode("utf-8", "replace").strip()
        raise ProvenanceError(f"git {' '.join(arguments)} failed: {detail}")
    return process.stdout


def resolve_revision(repository: Path, revision: str) -> tuple[str, str]:
    commit = run_git(repository, "rev-parse", "--verify", f"{revision}^{{commit}}")
    commit_text = commit.decode("ascii").strip()
    tree = run_git(repository, "rev-parse", "--verify", f"{commit_text}^{{tree}}")
    tree_text = tree.decode("ascii").strip()
    if not HEX_OBJECT.fullmatch(commit_text) or not HEX_OBJECT.fullmatch(tree_text):
        raise ProvenanceError("Git returned a malformed commit or tree identity")
    return commit_text, tree_text


def load_tree(repository: Path, revision: str) -> dict[str, dict[str, str]]:
    raw = run_git(repository, "ls-tree", "-rz", "--full-tree", revision)
    result: dict[str, dict[str, str]] = {}
    for record in raw.split(b"\0"):
        if not record:
            continue
        try:
            header, encoded_path = record.split(b"\t", 1)
            mode, kind, oid = header.decode("ascii").split(" ")
            path = encoded_path.decode("utf-8", "strict")
        except (ValueError, UnicodeDecodeError) as error:
            raise ProvenanceError("malformed Git tree entry") from error
        if not HEX_OBJECT.fullmatch(oid):
            raise ProvenanceError(f"malformed object identity for {path!r}")
        if path in result:
            raise ProvenanceError(f"duplicate Git tree path {path!r}")
        result[path] = {"path": path, "mode": mode, "type": kind, "oid": oid}
    return result


def validate_repo_path(path: str) -> str:
    if not path or "\0" in path or "\n" in path or "\r" in path or "\\" in path:
        raise ProvenanceError(f"invalid repository path {path!r}")
    pure = PurePosixPath(path)
    if pure.is_absolute() or any(part in ("", ".", "..") for part in pure.parts):
        raise ProvenanceError(f"unsafe repository path {path!r}")
    return pure.as_posix()


def hash_blob(data: bytes, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    digest.update(f"blob {len(data)}\0".encode("ascii"))
    digest.update(data)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ensure_regular_file(path: Path) -> None:
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError as error:
        raise ProvenanceError(f"missing evidence file: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise ProvenanceError(f"evidence must be a regular non-symlink file: {path}")


def path_without_symlinks(root: Path, relative: str) -> Path:
    path = root
    for part in PurePosixPath(validate_repo_path(relative)).parts:
        path = path / part
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError as error:
            raise ProvenanceError(f"materialized path is missing: {relative}") from error
        if stat.S_ISLNK(mode):
            raise ProvenanceError(f"materialized path contains a symlink: {relative}")
    return path


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() or path.is_symlink():
        ensure_regular_file(path)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass


def parse_make_dependencies(raw: bytes) -> list[str]:
    if b"\0" in raw:
        raise ProvenanceError("dependency file contains NUL data")
    try:
        text = raw.decode("utf-8", "strict")
    except UnicodeDecodeError as error:
        raise ProvenanceError("dependency file is not UTF-8") from error
    text = text.replace("\\\r\n", "").replace("\\\n", "")
    escaped = False
    separator = -1
    for index, character in enumerate(text):
        if escaped:
            escaped = False
        elif character == "\\":
            escaped = True
        elif character == ":":
            separator = index
            break
    if separator < 0:
        raise ProvenanceError("dependency file lacks a target separator")
    dependencies = text[separator + 1 :]
    words: list[str] = []
    token: list[str] = []
    index = 0
    while index < len(dependencies):
        character = dependencies[index]
        if character == "\\":
            index += 1
            if index >= len(dependencies):
                raise ProvenanceError("dependency file ends in an escape")
            token.append(dependencies[index])
        elif character == "$":
            if index + 1 >= len(dependencies) or dependencies[index + 1] != "$":
                raise ProvenanceError("dependency file contains unsupported make expansion")
            token.append("$")
            index += 1
        elif character.isspace():
            if token:
                words.append("".join(token))
                token = []
        else:
            token.append(character)
        index += 1
    if token:
        words.append("".join(token))
    if not words:
        raise ProvenanceError("dependency file names no inputs")
    return words


def normalize_dependency(root: Path, spelling: str) -> str:
    if not spelling or "\0" in spelling or "\n" in spelling or "\r" in spelling:
        raise ProvenanceError(f"invalid dependency spelling {spelling!r}")
    candidate = Path(spelling)
    if not candidate.is_absolute():
        candidate = root / candidate
    normalized = Path(os.path.normpath(str(candidate)))
    try:
        relative = normalized.relative_to(root)
    except ValueError as error:
        raise ProvenanceError(f"dependency is outside the materialized tree: {spelling}") from error
    relative_text = relative.as_posix()
    validate_repo_path(relative_text)
    return relative_text


def manifest_payload(
    dependencies: list[dict[str, str]], policy_inputs: list[dict[str, str]]
) -> dict[str, Any]:
    return {
        "compile_profile": COMPILE_PROFILE,
        "dependencies": dependencies,
        "policy_inputs": policy_inputs,
    }


def closure_fingerprint(
    dependencies: list[dict[str, str]], policy_inputs: list[dict[str, str]]
) -> str:
    payload = json.dumps(
        manifest_payload(dependencies, policy_inputs),
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode("ascii")
    return sha256_bytes(payload)


def build_manifest(
    repository: Path, revision: str, root: Path, dependency_file: Path
) -> dict[str, Any]:
    repository = repository.resolve(strict=True)
    root = root.resolve(strict=True)
    commit, tree_oid = resolve_revision(repository, revision)
    tree = load_tree(repository, commit)
    object_format = run_git(repository, "rev-parse", "--show-object-format").decode("ascii").strip()
    if object_format not in ("sha1", "sha256"):
        raise ProvenanceError(f"unsupported Git object format {object_format!r}")

    issues: list[str] = []
    dependencies: list[dict[str, str]] = []
    dependency_raw = b""
    try:
        ensure_regular_file(dependency_file)
        dependency_raw = dependency_file.read_bytes()
        spellings = parse_make_dependencies(dependency_raw)
        normalized: set[str] = set()
        for spelling in spellings:
            try:
                normalized.add(normalize_dependency(root, spelling))
            except ProvenanceError as error:
                issues.append(f"dependency-normalization:{error}")
        for path in sorted(normalized):
            entry = tree.get(path)
            if entry is None:
                issues.append(f"untracked-dependency:{path}")
                continue
            if entry["type"] != "blob" or entry["mode"] not in ("100644", "100755"):
                issues.append(f"unsupported-dependency:{path}:{entry['mode']}:{entry['type']}")
                continue
            try:
                materialized = path_without_symlinks(root, path)
                if not materialized.is_file():
                    raise ProvenanceError("not a regular file")
                if hash_blob(materialized.read_bytes(), object_format) != entry["oid"]:
                    raise ProvenanceError("bytes differ from the selected Git blob")
            except (OSError, ProvenanceError) as error:
                issues.append(f"materialization:{path}:{error}")
                continue
            dependencies.append({"path": path, "mode": entry["mode"], "oid": entry["oid"]})
        if ROOT_SOURCE not in normalized:
            issues.append(f"missing-root-source:{ROOT_SOURCE}")
    except (OSError, ProvenanceError) as error:
        issues.append(f"dependency-file:{error}")

    policy_inputs: list[dict[str, str]] = []
    for path in POLICY_INPUTS:
        entry = tree.get(path)
        if entry is None:
            policy_inputs.append({"path": path, "state": "missing"})
            continue
        if entry["type"] != "blob" or entry["mode"] not in ("100644", "100755"):
            policy_inputs.append(
                {
                    "path": path,
                    "state": "unsupported",
                    "mode": entry["mode"],
                    "type": entry["type"],
                    "oid": entry["oid"],
                }
            )
            issues.append(f"unsupported-policy-input:{path}:{entry['mode']}:{entry['type']}")
            continue
        policy_inputs.append(
            {"path": path, "state": "blob", "mode": entry["mode"], "oid": entry["oid"]}
        )
        try:
            materialized = path_without_symlinks(root, path)
            if not materialized.is_file() or hash_blob(materialized.read_bytes(), object_format) != entry["oid"]:
                raise ProvenanceError("bytes differ from the selected Git blob")
        except (OSError, ProvenanceError) as error:
            issues.append(f"policy-materialization:{path}:{error}")

    dependencies.sort(key=lambda item: item["path"])
    policy_inputs.sort(key=lambda item: item["path"])
    issues = sorted(set(issues))
    return {
        "schema": MANIFEST_SCHEMA,
        "revision": commit,
        "tree": tree_oid,
        "complete": not issues,
        "issues": issues,
        "dependency_file_sha256": sha256_bytes(dependency_raw),
        "closure_sha256": closure_fingerprint(dependencies, policy_inputs),
        **manifest_payload(dependencies, policy_inputs),
    }


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    write_atomic(path, json.dumps(manifest, sort_keys=True, indent=2).encode("utf-8") + b"\n")


def load_manifest(path: Path) -> tuple[dict[str, Any], bytes]:
    ensure_regular_file(path)
    raw = path.read_bytes()
    if not raw.endswith(b"\n") or b"\0" in raw:
        raise ProvenanceError(f"malformed manifest framing: {path}")
    try:
        manifest = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProvenanceError(f"invalid manifest JSON: {path}") from error
    required = {
        "schema",
        "revision",
        "tree",
        "complete",
        "issues",
        "dependency_file_sha256",
        "closure_sha256",
        "compile_profile",
        "dependencies",
        "policy_inputs",
    }
    if not isinstance(manifest, dict) or set(manifest) != required:
        raise ProvenanceError(f"manifest has unexpected fields: {path}")
    if manifest["schema"] != MANIFEST_SCHEMA or manifest["compile_profile"] != COMPILE_PROFILE:
        raise ProvenanceError(f"unsupported manifest schema or compile profile: {path}")
    if not isinstance(manifest["complete"], bool) or not isinstance(manifest["issues"], list):
        raise ProvenanceError(f"manifest completion fields are malformed: {path}")
    if manifest["complete"] != (len(manifest["issues"]) == 0):
        raise ProvenanceError(f"manifest completion status is inconsistent: {path}")
    if not isinstance(manifest["dependencies"], list) or not isinstance(manifest["policy_inputs"], list):
        raise ProvenanceError(f"manifest entry arrays are malformed: {path}")
    if manifest["dependencies"] != sorted(manifest["dependencies"], key=lambda item: item.get("path", "")):
        raise ProvenanceError(f"manifest dependencies are not sorted: {path}")
    if manifest["policy_inputs"] != sorted(manifest["policy_inputs"], key=lambda item: item.get("path", "")):
        raise ProvenanceError(f"manifest policy inputs are not sorted: {path}")
    expected = closure_fingerprint(manifest["dependencies"], manifest["policy_inputs"])
    if manifest["closure_sha256"] != expected:
        raise ProvenanceError(f"manifest closure fingerprint mismatch: {path}")
    if not HEX_SHA256.fullmatch(str(manifest["dependency_file_sha256"])):
        raise ProvenanceError(f"manifest dependency-file hash is malformed: {path}")
    if not HEX_OBJECT.fullmatch(str(manifest["revision"])) or not HEX_OBJECT.fullmatch(str(manifest["tree"])):
        raise ProvenanceError(f"manifest Git identities are malformed: {path}")
    return manifest, raw


def parse_bool(value: str) -> bool:
    if value == "true":
        return True
    if value == "false":
        return False
    raise ProvenanceError(f"expected true or false, got {value!r}")


def select_campaign(
    repository: Path,
    event: str,
    requested_text: str,
    candidate_revision: str,
    reference_revision: str,
    candidate_path: Path,
    reference_path: Path,
) -> bytes:
    repository = repository.resolve(strict=True)
    if not SAFE_EVENT.fullmatch(event):
        raise ProvenanceError(f"invalid event name {event!r}")
    requested = parse_bool(requested_text)
    if requested and event != "workflow_dispatch":
        raise ProvenanceError("explicit analyzer comparison is valid only for workflow_dispatch")
    candidate_commit, candidate_tree = resolve_revision(repository, candidate_revision)
    reference_commit, reference_tree = resolve_revision(repository, reference_revision)
    candidate, candidate_raw = load_manifest(candidate_path)
    reference, reference_raw = load_manifest(reference_path)
    if candidate["revision"] != candidate_commit or candidate["tree"] != candidate_tree:
        raise ProvenanceError("candidate provenance does not match the selected commit")
    if reference["revision"] != reference_commit or reference["tree"] != reference_tree:
        raise ProvenanceError("reference provenance does not match the selected commit")

    if requested:
        selection, reason = "compare", "requested"
    elif candidate_commit == reference_commit:
        if event in ("push", "workflow_dispatch"):
            selection, reason = "skip", "same-revision"
        else:
            selection, reason = "compare", "event-requires-comparison"
    elif event == "pull_request":
        if not candidate["complete"] or not reference["complete"]:
            selection, reason = "compare", "provenance-uncertain"
        elif candidate["closure_sha256"] == reference["closure_sha256"]:
            selection, reason = "skip", "unchanged-driver-closure"
        else:
            selection, reason = "compare", "changed-driver-closure"
    else:
        selection, reason = "compare", "distinct-revisions"

    fields = [
        SELECTION_SCHEMA,
        f"event={event}",
        f"requested={'true' if requested else 'false'}",
        f"candidate_revision={candidate_commit}",
        f"reference_revision={reference_commit}",
        f"candidate_tree={candidate_tree}",
        f"reference_tree={reference_tree}",
        f"candidate_closure_sha256={candidate['closure_sha256']}",
        f"reference_closure_sha256={reference['closure_sha256']}",
        f"candidate_complete={'true' if candidate['complete'] else 'false'}",
        f"reference_complete={'true' if reference['complete'] else 'false'}",
        f"candidate_manifest_sha256={sha256_bytes(candidate_raw)}",
        f"reference_manifest_sha256={sha256_bytes(reference_raw)}",
        f"selection={selection}",
        f"reason={reason}",
    ]
    return ("\n".join(fields) + "\n").encode("ascii")


SELECTION_KEYS = (
    "event",
    "requested",
    "candidate_revision",
    "reference_revision",
    "candidate_tree",
    "reference_tree",
    "candidate_closure_sha256",
    "reference_closure_sha256",
    "candidate_complete",
    "reference_complete",
    "candidate_manifest_sha256",
    "reference_manifest_sha256",
    "selection",
    "reason",
)


def load_selection(path: Path) -> dict[str, str]:
    ensure_regular_file(path)
    raw = path.read_bytes()
    if not raw.endswith(b"\n") or b"\0" in raw:
        raise ProvenanceError("selection record has invalid framing")
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise ProvenanceError("selection record is not ASCII") from error
    if not lines or lines[0] != SELECTION_SCHEMA or len(lines) != len(SELECTION_KEYS) + 1:
        raise ProvenanceError("selection record has an invalid schema or field count")
    result: dict[str, str] = {}
    for expected, line in zip(SELECTION_KEYS, lines[1:]):
        prefix = expected + "="
        if not line.startswith(prefix):
            raise ProvenanceError(f"selection record expected field {expected}")
        result[expected] = line[len(prefix) :]
    if result["selection"] not in ("compare", "skip"):
        raise ProvenanceError("selection record has an invalid selection")
    if result["requested"] not in ("true", "false") or result["candidate_complete"] not in ("true", "false") or result["reference_complete"] not in ("true", "false"):
        raise ProvenanceError("selection record has an invalid boolean")
    for key in ("candidate_revision", "reference_revision", "candidate_tree", "reference_tree"):
        if not HEX_OBJECT.fullmatch(result[key]):
            raise ProvenanceError(f"selection record has an invalid {key}")
    for key in (
        "candidate_closure_sha256",
        "reference_closure_sha256",
        "candidate_manifest_sha256",
        "reference_manifest_sha256",
    ):
        if not HEX_SHA256.fullmatch(result[key]):
            raise ProvenanceError(f"selection record has an invalid {key}")
    return result


def command_manifest(arguments: argparse.Namespace) -> None:
    manifest = build_manifest(
        Path(arguments.repository),
        arguments.revision,
        Path(arguments.root),
        Path(arguments.depfile),
    )
    write_manifest(Path(arguments.output), manifest)


def command_select(arguments: argparse.Namespace) -> None:
    record = select_campaign(
        Path(arguments.repository),
        arguments.event,
        arguments.requested,
        arguments.candidate_revision,
        arguments.reference_revision,
        Path(arguments.candidate_manifest),
        Path(arguments.reference_manifest),
    )
    write_atomic(Path(arguments.output), record)


def command_field(arguments: argparse.Namespace) -> None:
    record = load_selection(Path(arguments.record))
    if arguments.field not in SELECTION_KEYS:
        raise ProvenanceError(f"unknown selection field {arguments.field!r}")
    print(record[arguments.field])


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    manifest = subparsers.add_parser("manifest", help="derive a verified driver dependency manifest")
    manifest.add_argument("--repository", required=True)
    manifest.add_argument("--revision", required=True)
    manifest.add_argument("--root", required=True)
    manifest.add_argument("--depfile", required=True)
    manifest.add_argument("--output", required=True)
    manifest.set_defaults(handler=command_manifest)

    select = subparsers.add_parser("select", help="select comparison or candidate-only analysis")
    select.add_argument("--repository", required=True)
    select.add_argument("--event", required=True)
    select.add_argument("--requested", required=True)
    select.add_argument("--candidate-revision", required=True)
    select.add_argument("--reference-revision", required=True)
    select.add_argument("--candidate-manifest", required=True)
    select.add_argument("--reference-manifest", required=True)
    select.add_argument("--output", required=True)
    select.set_defaults(handler=command_select)

    field = subparsers.add_parser("field", help="read one validated selection-record field")
    field.add_argument("--record", required=True)
    field.add_argument("--field", required=True)
    field.set_defaults(handler=command_field)
    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        arguments.handler(arguments)
    except (OSError, ProvenanceError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
