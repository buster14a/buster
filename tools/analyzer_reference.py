#!/usr/bin/env python3
"""Candidate build-driver provenance and read-only historical selection decoding.

The candidate is compiled once with Clang dependency output. Its complete source,
executable, compiler, command and environment identities are revalidated before
one full analysis. No event or dependency change can select another execution.

The original module name and load_selection decoder remain for source-pinned
historical CI measurement readers. They do not select or launch reference work.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from typing import Any

MANIFEST_SCHEMA = "BUSTER_ANALYZER_DRIVER_PROVENANCE_V2"
SELECTION_SCHEMA = "BUSTER_ANALYZER_COMPARISON_SELECTION_V3"
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
# Only compiler/driver inputs, never credentials or unrelated runner metadata.
CONTEXT_ENVIRONMENT = (
    "PATH", "HOME", "TMPDIR", "LANG", "LC_ALL", "LC_CTYPE", "TZ",
    "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
    "LIBRARY_PATH", "LD_LIBRARY_PATH", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET",
    "CCC_OVERRIDE_OPTIONS", "CLANG_CONFIG_FILE_SYSTEM_DIR", "CLANG_CONFIG_FILE_USER_DIR",
)


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


def driver_command(compiler: Path, dependency_file: Path, driver: Path) -> list[str]:
    substitutions = {"<dependency-file>": str(dependency_file), "<driver-output>": str(driver)}
    return [str(compiler), *(substitutions.get(value, value) for value in COMPILE_PROFILE["arguments"])]


def compiler_identity() -> dict[str, str]:
    compiler_name = shutil.which(COMPILE_PROFILE["compiler"])
    if compiler_name is None:
        raise ProvenanceError("Clang is unavailable for driver context")
    compiler = Path(compiler_name).resolve(strict=True)
    ensure_regular_file(compiler)
    version = subprocess.run([str(compiler), "--version"], check=True, capture_output=True, timeout=30)
    return {
        "compiler": str(compiler),
        "compiler_sha256": sha256_bytes(compiler.read_bytes()),
        "compiler_version_sha256": sha256_bytes(version.stdout + b"\0" + version.stderr),
    }


def driver_context(root: Path, dependency_file: Path, driver: Path) -> dict[str, Any]:
    """Bind one executable used in one root; never infer cross-root equivalence.

    The bootstrap subcommand executes driver_command itself. Revalidation derives
    this context again before the candidate campaign. Environment values are only
    hashed; BUSTER_* covers the driver's repository-specific inputs.
    """
    identity = compiler_identity()
    ensure_regular_file(driver)
    if not os.access(driver, os.X_OK):
        raise ProvenanceError("candidate driver is not executable")
    environment = {
        key: value for key, value in os.environ.items()
        if key in CONTEXT_ENVIRONMENT or key.startswith(("BUSTER_", "LC_"))
    }
    encoded = json.dumps(environment, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {
        "root": str(root),
        "driver": str(driver.absolute()),
        "driver_sha256": sha256_bytes(driver.read_bytes()),
        **identity,
        "compile_command": driver_command(Path(identity["compiler"]), dependency_file.absolute(), driver.absolute()),
        "environment_sha256": sha256_bytes(encoded),
    }


def build_manifest(
    repository: Path, revision: str, root: Path, dependency_file: Path,
    driver: Path | None = None,
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
            issues.append(f"missing-policy-input:{path}")
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
        "execution_context": driver_context(root, dependency_file, driver) if driver is not None else None,
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
        "execution_context",
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
    context = manifest["execution_context"]
    if context is not None:
        context_keys = {"root", "driver", "driver_sha256", "compiler", "compiler_sha256",
                        "compiler_version_sha256", "compile_command", "environment_sha256"}
        if not isinstance(context, dict) or set(context) != context_keys:
            raise ProvenanceError(f"malformed driver execution context: {path}")
        for key in ("root", "driver", "compiler"):
            if not isinstance(context[key], str) or not Path(context[key]).is_absolute() or "\0" in context[key]:
                raise ProvenanceError(f"invalid context path {key}: {path}")
        for key in ("driver_sha256", "compiler_sha256", "compiler_version_sha256", "environment_sha256"):
            if not HEX_SHA256.fullmatch(str(context[key])):
                raise ProvenanceError(f"invalid context hash {key}: {path}")
        if not isinstance(context["compile_command"], list) or not all(
            isinstance(value, str) and "\0" not in value for value in context["compile_command"]
        ):
            raise ProvenanceError(f"invalid context compile command: {path}")
    return manifest, raw


# Historical V3 records are decoded unchanged, never produced by current CI.
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
        Path(arguments.driver),
    )
    if not manifest["complete"]:
        raise ProvenanceError("candidate provenance is incomplete: " + "; ".join(manifest["issues"]))
    write_manifest(Path(arguments.output), manifest)


def command_bootstrap(arguments: argparse.Namespace) -> None:
    root = Path(arguments.root).resolve(strict=True)
    identity = compiler_identity()
    compiler = Path(identity["compiler"])
    dependency_file = Path(arguments.depfile).absolute()
    driver = Path(arguments.driver).absolute()
    subprocess.run(driver_command(compiler, dependency_file, driver), cwd=root, check=True)
    manifest = build_manifest(Path(arguments.repository), arguments.revision, root, dependency_file, driver)
    if not manifest["complete"]:
        raise ProvenanceError("candidate provenance is incomplete: " + "; ".join(manifest["issues"]))
    if any(manifest["execution_context"][key] != value for key, value in identity.items()):
        raise ProvenanceError("Clang identity changed during driver compilation")
    write_manifest(Path(arguments.output), manifest)


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    manifest = subparsers.add_parser("manifest", help="derive a verified driver dependency manifest")
    manifest.add_argument("--repository", required=True)
    manifest.add_argument("--revision", required=True)
    manifest.add_argument("--root", required=True)
    manifest.add_argument("--depfile", required=True)
    manifest.add_argument("--output", required=True)
    manifest.add_argument("--driver", required=True, help="bind and revalidate the exact executable context")
    manifest.set_defaults(handler=command_manifest)

    bootstrap = subparsers.add_parser("bootstrap", help="compile the driver with the recorded exact profile")
    for option in ("repository", "revision", "root", "depfile", "driver", "output"):
        bootstrap.add_argument("--" + option, required=True)
    bootstrap.set_defaults(handler=command_bootstrap)

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
