#!/usr/bin/env python3
"""Materialize the authenticated, offline dependency closure for the census.

The census itself is intentionally a C producer.  This small boundary owns the
part of the input contract that cannot be inferred from a compiler invocation:
which repository bytes are admitted, where they are placed, and which names
belong to the generated evidence protocol.  The manifest is an input, never a
download recipe.  Every source byte is checked before publication and the
published tree is written through a temporary sibling, so a failed or
interrupted invocation cannot leave a partly usable dependency tree.

The supported manifest shape is ``buster-native-retirement-dependencies-v1``::

    {
      "schema": "buster-native-retirement-dependencies-v1",
      "version": 1,
      "files": [{
        "kind": "project-header",
        "source": "src/buster/lib/base.h",
        "provenance": "repo:src/buster/lib/base.h",
        "destination": "dependencies/project-include/buster/lib/base.h",
        "bytes": 123,
        "sha256": "..."
      }],
      "source_root": "..",
      "metadata": [{"name": "dependency-inputs.tsv"}]
    }

``dependencies`` is accepted as an alias for ``files`` because the first
archived descriptors used that name. Resource and project destinations must use
their fixed compiler include roots; their include-relative names are kept in
one namespace while validating. This is what lets the materializer reject a
resource/project collision before either one is published. A descriptor may
carry an ``archived_replay`` row projection; its fixture inputs, project-header
mapping, expanded row identities, and digests are verified before publication.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import stat
import sys
import tempfile


SCHEMA = "buster-native-retirement-dependencies-v1"
VERSION = 1
CHUNK = 1024 * 1024

# These names are created by the census, contract validator, or the workflow
# wrapper.  A dependency must never be able to replace one of them.  The
# basename check deliberately covers nested destinations too: generated
# evidence is sometimes staged below a per-shard directory.
GENERATED_METADATA_NAMES = frozenset({
    "evidence-candidate.txt",
    "manifest.txt",
    "manifest.json",
    "dependency-manifest.json",
    "dependency-descriptor.json",
    "dependency-policy.json",
    "dependency-source-snapshot.json",
    "dependency-resolved-descriptor.json",
    "dependency-receipt.json",
    "dependency-materializer.tsv",
    "dependency-files.tsv",
    "dependencies.tsv",
    "resources.tsv",
    "projects.tsv",
    "inputs.tsv",
    "rows.tsv",
    "results.tsv",
    "summary.txt",
    "environment.tsv",
    "support-contract.tsv",
    "supported-gap-ledger.tsv",
    "processes.tsv",
    "fallback-counters.tsv",
    "fallback-functions.tsv",
    "applicability.tsv",
    "applicability-skips.tsv",
    "residual.tsv",
    "common-row-transitions.json",
    "census-validation-v2.json",
    "candidate-ide.exe",
    "baseline-ide.exe",
    "fixture-revision",
    "worktree-status",
})

_KIND_ALIASES = {
    "resource": "resource-header",
    "resource-header": "resource-header",
    "resource_include": "resource-header",
    "resource-include": "resource-header",
    "project": "project-header",
    "project-header": "project-header",
    "project_include": "project-header",
    "project-include": "project-header",
}

INCLUDE_ROOTS = {
    "resource-header": "dependencies/resource-include",
    "project-header": "dependencies/project-include",
}

ARCHIVED_TARGETS = (
    "x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
    "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
    "x86_64-apple-macos", "aarch64-apple-macos",
    "x86_64-linux-android", "aarch64-linux-android",
    "x86_64-apple-ios", "aarch64-apple-ios",
    "x86_64-unknown-uefi", "aarch64-unknown-uefi",
)
ARCHIVED_FRONTENDS = ("local-backed-canonical", "direct-ssa")
ARCHIVED_PICS = ("0", "1")
ARCHIVED_ALLOCATORS = ("mir-stack", "fast", "quality")
SUPPORT_CONTRACT_SHA256 = "c61bbde58c471dc0d50853f8797e05ccd1737521d342dc7376669d90e192f5b8"
NEXT_SUPPORT_CONTRACT_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"
PROPOSED_SUPPORT_CONTRACT_SHA256 = "0d878bf0a3df9f0528803a5b08275d950f9edda57e373fd7618229dee264e427"
NETWORK_PROVENANCE = re.compile(
    r"^(?:[a-z][a-z0-9+.-]*:|[^/\\:@]+@[^/\\:]+:|[^/\\:]+:[^/\\].*)",
    re.IGNORECASE,
)
EXTERNAL_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
GITHUB_REPOSITORY = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
GIT_REVISION = re.compile(r"^[0-9a-f]{40}$")


class MaterializationError(ValueError):
    """An offline dependency descriptor failed closed."""


@dataclass(frozen=True)
class Dependency:
    kind: str
    source: str
    provenance: str
    destination: str
    bytes: int
    sha256: str
    fixture: str = ""


def _fail(message):
    raise MaterializationError(message)


def _read_no_follow(path, field):
    path = Path(path)
    absolute = Path(os.path.abspath(os.fspath(path)))
    current = Path(absolute.anchor)
    descriptor = None
    try:
        parts = absolute.relative_to(current).parts
        for part in parts[:-1]:
            current /= part
            info = current.lstat()
            if stat.S_ISLNK(info.st_mode):
                _fail(f"{field} contains a symlink: {path}")
            if not stat.S_ISDIR(info.st_mode):
                _fail(f"{field} parent is not a directory: {current}")
        current /= parts[-1]
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            _fail(f"{field} is not a real file: {path}")
        descriptor = os.open(os.fspath(current), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        before = os.fstat(descriptor)
        data = bytearray()
        while True:
            block = os.read(descriptor, CHUNK)
            if not block:
                break
            data.extend(block)
        after = os.fstat(descriptor)
        if _identity_tuple(before) != _identity_tuple(after):
            _fail(f"{field} changed while reading: {path}")
    except OSError as error:
        _fail(f"cannot read {field} {path}: {error}")
    finally:
        if descriptor is not None:
            os.close(descriptor)
    return bytes(data)


def _read_json(path):
    try:
        data = _read_no_follow(path, "dependency manifest")
        value = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read dependency manifest {path}: {error}")
    if not isinstance(value, dict):
        _fail("dependency manifest must be a JSON object")
    return value


def _canonical_relative(value, field):
    if not isinstance(value, str) or not value or "\x00" in value:
        _fail(f"{field} must be a non-empty relative path")
    # Normalize Windows descriptors as well as POSIX descriptors.  We still
    # reject absolute drive/UNC spellings rather than silently relocating them.
    value = value.replace("\\", "/")
    if "://" in value:
        _fail(f"{field} cannot name a network resource")
    if value.startswith("/") or (len(value) >= 2 and value[1] == ":"):
        _fail(f"{field} must be a relative path: {value!r}")
    normalized = posixpath.normpath(value)
    if normalized in ("", ".") or normalized == ".." or normalized.startswith("../"):
        _fail(f"{field} escapes its root: {value!r}")
    if any(ord(character) < 0x20 or ord(character) == 0x7f for character in normalized):
        _fail(f"{field} contains a control character")
    # ``normpath`` can preserve a repeated slash at the beginning; do not let
    # that become an absolute destination on a different host.
    if normalized.startswith("/") or any(part in ("", ".", "..") for part in normalized.split("/")):
        _fail(f"{field} is not canonical: {value!r}")
    return normalized


def _canonical_descriptor_relative(value, field):
    """Canonicalize a path relative to the descriptor itself.

    ``source_root`` is intentionally allowed to be ``..`` because the checked
    in descriptor lives below the repository root. It still needs the same
    spelling discipline as include-relative paths: accepting ``a/../b``
    would make the descriptor identity depend on a host path resolver.
    """
    if not isinstance(value, str) or not value or "\x00" in value:
        _fail(f"{field} must be a non-empty descriptor-relative path")
    value = value.replace("\\", "/")
    if "://" in value or value.startswith("/") or (len(value) >= 2 and value[1] == ":"):
        _fail(f"{field} must be descriptor-relative: {value!r}")
    normalized = posixpath.normpath(value)
    if normalized != value or normalized == "" or any(
            ord(character) < 0x20 or ord(character) == 0x7f for character in normalized):
        _fail(f"{field} is not canonical: {value!r}")
    if any(part == "" for part in value.split("/")):
        _fail(f"{field} is not canonical: {value!r}")
    return normalized


def _external_name(value, field):
    if not isinstance(value, str) or not EXTERNAL_NAME.fullmatch(value):
        _fail(f"{field} must be a simple external dependency name")
    return value


def _external_revision(value, field):
    if not isinstance(value, str) or not GIT_REVISION.fullmatch(value):
        _fail(f"{field} must be a lowercase 40-digit Git revision")
    return value


def _external_declarations(manifest):
    """Validate the immutable external checkout closure without touching disk.

    Checkouts are deliberately declarations, not fetch instructions.  The
    workflow obtains them with pinned ``actions/checkout`` steps; the separate
    external verifier proves that the local bytes are at those revisions before
    this materializer reads any source file.
    """
    raw_checkouts = manifest.get("external_checkouts", [])
    raw_generated = manifest.get("external_generated", [])
    if not isinstance(raw_checkouts, list) or not isinstance(raw_generated, list):
        _fail("external_checkouts and external_generated must be lists")
    checkouts = []
    names = set()
    paths = set()
    for index, value in enumerate(raw_checkouts):
        if not isinstance(value, dict):
            _fail(f"external checkout {index} is not an object")
        name = _external_name(value.get("name"), f"external checkout {index} name")
        repository = value.get("repository")
        if not isinstance(repository, str) or not GITHUB_REPOSITORY.fullmatch(repository):
            _fail(f"external checkout {index} repository is not owner/name")
        revision = _external_revision(value.get("revision"), f"external checkout {index} revision")
        path = _canonical_relative(value.get("path"), f"external checkout {index} path")
        if not path.startswith("external/") or path != f"external/{name}":
            _fail(f"external checkout {index} path must be external/{name}")
        if name in names or path in paths:
            _fail(f"external checkout {index} is duplicated")
        names.add(name)
        paths.add(path)
        checkouts.append({"name": name, "repository": repository,
                          "revision": revision, "path": path})
    generated = []
    generated_names = set()
    generated_paths = set()
    checkout_names = {item["name"] for item in checkouts}
    for index, value in enumerate(raw_generated):
        if not isinstance(value, dict):
            _fail(f"external generated closure {index} is not an object")
        name = _external_name(value.get("name"), f"external generated closure {index} name")
        checkout = _external_name(value.get("checkout"), f"external generated closure {index} checkout")
        if checkout not in checkout_names:
            _fail(f"external generated closure {index} names an undeclared checkout: {checkout}")
        revision = _external_revision(value.get("revision"), f"external generated closure {index} revision")
        checkout_revision = next(item["revision"] for item in checkouts if item["name"] == checkout)
        if revision != checkout_revision:
            _fail(f"external generated closure {index} revision does not match checkout: {checkout}")
        path = _canonical_relative(value.get("path"), f"external generated closure {index} path")
        if not path.startswith("external/") or path.startswith("external/" + checkout + "/"):
            _fail(f"external generated closure {index} path must be separate from checkout: {path}")
        generator = value.get("generator")
        _safe_text(generator, f"external generated closure {index} generator", allow_empty=False)
        if name in generated_names or path in generated_paths or path in paths:
            _fail(f"external generated closure {index} is duplicated")
        generated_names.add(name)
        generated_paths.add(path)
        generated.append({"name": name, "checkout": checkout, "revision": revision,
                          "path": path, "generator": generator})
    return tuple(checkouts), tuple(generated)


def _validate_external_records(records, checkouts, generated):
    checkout_paths = {item["name"]: item["path"] for item in checkouts}
    generated_paths = {item["name"]: item["path"] for item in generated}
    for record in records:
        if not record.source.startswith("external/"):
            continue
        if any(record.source == path or record.source.startswith(path + "/")
               for path in checkout_paths.values()):
            continue
        if any(record.source == path or record.source.startswith(path + "/")
               for path in generated_paths.values()):
            continue
        _fail(f"external dependency source is not in the declared closure: {record.source}")


def _safe_text(value, field, allow_empty=True):
    """Validate a single non-path ledger field before it is serialized."""
    if not isinstance(value, str) or (not allow_empty and not value):
        _fail(f"{field} must be text")
    if any(ord(character) < 0x20 or ord(character) == 0x7f for character in value):
        _fail(f"{field} contains a control character")
    return value


def _canonical_provenance(value):
    if not isinstance(value, str) or not value or "\x00" in value:
        _fail("provenance must be a non-empty offline identity")
    prefix = ""
    body = value
    if value.startswith("repo:"):
        prefix, body = "repo:", value[5:]
    if "://" in body or NETWORK_PROVENANCE.match(body):
        _fail("network provenance is not allowed")
    body = _canonical_relative(body, "provenance")
    return prefix + body


def _sha256(value, field="sha256"):
    if not isinstance(value, str) or len(value) != 64 or any(character not in "0123456789abcdef" for character in value):
        _fail(f"{field} must be a lowercase SHA-256")
    return value


def _bounded_bytes(value):
    if type(value) is not int or value < 0 or value > (1 << 63) - 1:
        _fail("bytes must be a non-negative bounded integer")
    return value


def _field(record, *names, default=None):
    for name in names:
        if name in record:
            return record[name]
    return default


def _kind(value):
    if not isinstance(value, str):
        _fail("dependency kind is required")
    result = _KIND_ALIASES.get(value.lower())
    if result is None:
        _fail(f"unsupported dependency kind: {value!r}")
    return result


def _include_relative(kind, destination):
    root = INCLUDE_ROOTS[kind] + "/"
    if not destination.startswith(root):
        _fail(f"{kind} destination has the wrong include root: {destination}")
    relative = destination[len(root):]
    if not relative:
        _fail(f"{kind} destination has no include-relative path")
    return relative


def _record(value, index, default_kind=None):
    if not isinstance(value, dict):
        _fail(f"dependency record {index} is not an object")
    kind = _field(value, "kind", "type", default=default_kind)
    if kind is None:
        _fail(f"dependency record {index} has no kind")
    source = _field(value, "source", "source_path", "repo_path", "path")
    destination = _field(value, "destination", "dest", "destination_path")
    provenance = _field(value, "provenance", "identity", "source_identity", default=source)
    size = _field(value, "bytes", "size")
    digest = _field(value, "sha256", "source_sha256", "digest")
    fixture = _field(value, "fixture", "subject", default="")
    fixture = _safe_text(fixture, f"dependency record {index} fixture")
    if source is None or destination is None or provenance is None or size is None or digest is None:
        _fail(f"dependency record {index} is missing source, destination, provenance, bytes, or sha256")
    normalized_kind = _kind(kind)
    normalized_destination = _canonical_relative(destination, f"record {index} destination")
    return Dependency(
        kind=normalized_kind,
        source=_canonical_relative(source, f"record {index} source"),
        provenance=_canonical_provenance(provenance),
        destination=normalized_destination,
        bytes=_bounded_bytes(size),
        sha256=_sha256(digest, f"record {index} sha256"),
        fixture=fixture,
    )


def _metadata_records(value):
    if value is None:
        return []
    if isinstance(value, dict):
        value = [{"name": key, **(item if isinstance(item, dict) else {})} for key, item in value.items()]
    if not isinstance(value, list):
        _fail("metadata must be a list or object")
    result = []
    for index, item in enumerate(value):
        if isinstance(item, str):
            name = item
        elif isinstance(item, dict):
            name = _field(item, "name", "destination", "path")
        else:
            _fail(f"metadata record {index} is not an object")
        if name is None:
            _fail(f"metadata record {index} has no name")
        result.append(_canonical_relative(name, f"metadata {index}"))
    return result


def parse_manifest(manifest):
    """Validate and canonicalize a descriptor without touching the filesystem."""
    if not isinstance(manifest, dict):
        _fail("dependency manifest must be an object")
    if manifest.get("schema") != SCHEMA or manifest.get("version") != VERSION:
        _fail("unsupported dependency manifest schema")
    external_checkouts, external_generated = _external_declarations(manifest)
    raw = manifest.get("files", manifest.get("dependencies"))
    if raw is None:
        # The split form is convenient for hand-authored archives.  It is
        # canonicalized into one namespace before collision checking.
        raw = []
        for kind, key in (("resource-header", "resources"), ("project-header", "projects")):
            values = manifest.get(key, [])
            if not isinstance(values, list):
                _fail(f"{key} must be a list")
            raw.extend(dict(item, kind=kind) for item in values)
    if not isinstance(raw, list) or not raw:
        _fail("dependency manifest contains no files")
    records = [_record(item, index) for index, item in enumerate(raw)]

    generated = _metadata_records(manifest.get("metadata"))
    generated.extend(_metadata_records(manifest.get("generated_metadata")))
    generated_paths = set()
    for name in generated:
        if name in generated_paths:
            _fail(f"metadata collision: {name}")
        generated_paths.add(name)

    by_destination = {}
    by_provenance = {}
    by_include = {}
    canonical = []
    for record in records:
        if PurePosixPath(record.destination).name in GENERATED_METADATA_NAMES:
            _fail(f"destination reserves generated metadata name: {record.destination}")
        if record.destination in generated_paths:
            _fail(f"metadata collision at destination: {record.destination}")
        _include_relative(record.kind, record.destination)
        previous_destination = by_destination.get(record.destination)
        previous_provenance = by_provenance.get(record.provenance)
        # Repeating one authenticated resource is useful when several archived
        # rows include it.  It is admitted only when the entire identity agrees;
        # the canonical list then keeps one copy, making output order stable.
        if previous_destination is not None:
            if previous_destination.kind != record.kind:
                raise MaterializationError(
                    f"identical resource/project collision at destination {record.destination}")
            if previous_destination != record:
                _fail(f"normalized duplicate destination: {record.destination}")
            continue
        if previous_provenance is not None:
            if previous_provenance != record:
                _fail(f"normalized duplicate provenance: {record.provenance}")
            continue
        include = _include_relative(record.kind, record.destination)
        previous_include = by_include.get(include)
        if previous_include is not None:
            if previous_include != record:
                _fail(f"global include namespace collision: {include}")
            continue
        by_destination[record.destination] = record
        by_provenance[record.provenance] = record
        by_include[include] = record
        canonical.append(record)
    # Metadata names are also a namespace: two spellings that normalize to one
    # name were rejected above, and a dependency cannot hide one.
    canonical.sort(key=lambda item: (item.destination, item.kind, item.provenance))
    _validate_external_records(canonical, external_checkouts, external_generated)
    return tuple(canonical), tuple(sorted(generated_paths))


def _source_path(root, source):
    root = Path(root)
    if not root.is_absolute():
        _fail("source root must be absolute")
    _assert_directory_no_follow(root, "source root")
    if root.is_symlink() or not root.is_dir():
        _fail("source root must be a real directory")
    relative = PurePosixPath(source)
    path = root.joinpath(*relative.parts)
    # Refuse symlinked ancestors and the leaf.  ``resolve`` alone is not
    # sufficient because it would make a symlink look like an authenticated
    # repository path.
    current = root
    for part in relative.parts:
        current = current / part
        try:
            info = current.lstat()
        except OSError as error:
            _fail(f"missing authenticated dependency source {source}: {error}")
        if stat.S_ISLNK(info.st_mode):
            _fail(f"dependency source contains a symlink: {source}")
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(root.resolve())
    except (OSError, ValueError) as error:
        _fail(f"dependency source escapes source root: {source}: {error}")
    info = path.stat(follow_symlinks=False)
    if not stat.S_ISREG(info.st_mode):
        _fail(f"dependency source is not a regular file: {source}")
    if info.st_nlink != 1:
        _fail(f"dependency source is hard-linked: {source}")
    return path


def _assert_directory_no_follow(path, field):
    path = Path(os.path.abspath(os.fspath(path)))
    current = Path(path.anchor)
    try:
        parts = path.relative_to(current).parts
    except ValueError as error:
        _fail(f"invalid {field}: {path}: {error}")
    for part in parts:
        current /= part
        try:
            info = current.lstat()
        except OSError as error:
            _fail(f"cannot inspect {field} {current}: {error}")
        if stat.S_ISLNK(info.st_mode):
            _fail(f"{field} contains a symlink: {current}")
        if not stat.S_ISDIR(info.st_mode):
            _fail(f"{field} is not a directory: {current}")


def _identity_tuple(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def _open_source_descriptor(root, source):
    root = Path(root)
    flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
    directory = None
    current = None
    descriptor = None
    try:
        directory = os.open(os.fspath(root), flags)
        current = directory
        parts = PurePosixPath(source).parts
        for part in parts[:-1]:
            next_directory = os.open(part, flags, dir_fd=current)
            if current != directory:
                os.close(current)
            current = next_directory
        descriptor = os.open(parts[-1], os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0), dir_fd=current)
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode):
            _fail(f"dependency source is not a regular file: {source}")
        if info.st_nlink != 1:
            _fail(f"dependency source is hard-linked: {source}")
        result = descriptor
        descriptor = None
    except OSError as error:
        _fail(f"cannot open authenticated dependency source {source}: {error}")
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if current is not None and current != directory:
            os.close(current)
        if directory is not None:
            os.close(directory)
    return result


def _read_descriptor(descriptor):
    digest = hashlib.sha256()
    size = 0
    os.lseek(descriptor, 0, os.SEEK_SET)
    while True:
        block = os.read(descriptor, CHUNK)
        if not block:
            break
        size += len(block)
        digest.update(block)
    return size, digest.hexdigest()


def _identity(path):
    descriptor = None
    try:
        descriptor = os.open(os.fspath(path), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode):
            _fail(f"identity path is not a regular file: {path}")
        size, digest = _read_descriptor(descriptor)
        after = os.fstat(descriptor)
        if _identity_tuple(info) != _identity_tuple(after):
            _fail(f"identity changed while reading: {path}")
    except OSError as error:
        _fail(f"cannot read identity {path}: {error}")
    finally:
        if descriptor is not None:
            os.close(descriptor)
    return size, digest


def _ensure_output_parent(parent):
    parent = Path(parent)
    if not parent.is_absolute():
        _fail("materializer output parent must be absolute")
    current = Path(parent.anchor)
    try:
        parts = parent.relative_to(current).parts
    except ValueError as error:
        _fail(f"invalid materializer output parent: {parent}: {error}")
    for part in parts:
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            try:
                current.mkdir()
                info = current.lstat()
            except OSError as error:
                _fail(f"cannot create materializer output parent {current}: {error}")
        except OSError as error:
            _fail(f"cannot inspect materializer output parent {current}: {error}")
        if stat.S_ISLNK(info.st_mode):
            _fail(f"materializer output parent contains a symlink: {current}")
        if not stat.S_ISDIR(info.st_mode):
            _fail(f"materializer output parent is not a directory: {current}")


def _copy_verified(source_root, source_name, destination, expected_bytes, expected_sha256):
    descriptor = _open_source_descriptor(source_root, source_name)
    try:
        try:
            before = os.fstat(descriptor)
            actual_bytes, actual_sha256 = _read_descriptor(descriptor)
            after = os.fstat(descriptor)
            if _identity_tuple(before) != _identity_tuple(after):
                _fail(f"authenticated source changed while reading: {source_name}")
        except OSError as error:
            _fail(f"cannot read authenticated dependency source {source_name}: {error}")
        if actual_bytes != expected_bytes or actual_sha256 != expected_sha256:
            _fail(f"authenticated source identity mismatch: {source_name}")
        _ensure_output_parent(destination.parent)
        destination_descriptor = None
        try:
            destination_descriptor = os.open(
                os.fspath(destination), os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
            os.lseek(descriptor, 0, os.SEEK_SET)
            while True:
                block = os.read(descriptor, CHUNK)
                if not block:
                    break
                view = memoryview(block)
                while view:
                    written = os.write(destination_descriptor, view)
                    view = view[written:]
            final = os.fstat(descriptor)
            if _identity_tuple(before) != _identity_tuple(final):
                _fail(f"authenticated source changed while publishing: {source_name}")
        except OSError as error:
            _fail(f"cannot publish authenticated dependency {destination}: {error}")
        finally:
            if destination_descriptor is not None:
                os.close(destination_descriptor)
    finally:
        os.close(descriptor)
    copied_bytes, copied_sha256 = _identity(destination)
    if copied_bytes != expected_bytes or copied_sha256 != expected_sha256:
        _fail(f"published dependency identity mismatch: {destination}")


def _closure(records):
    digest = hashlib.sha256()
    for record in records:
        payload = json.dumps({
            "kind": record.kind,
            "source": record.source,
            "provenance": record.provenance,
            "destination": record.destination,
            "bytes": record.bytes,
            "sha256": record.sha256,
            "fixture": record.fixture,
        }, sort_keys=True, separators=(",", ":")).encode("utf-8")
        digest.update(len(payload).to_bytes(8, "little"))
        digest.update(payload)
    return digest.hexdigest()


def _include_closure(records, kind, source_root):
    digest = hashlib.sha256()
    for record in records:
        if record.kind != kind:
            continue
        relative = _include_relative(kind, record.destination)
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(record.bytes.to_bytes(8, "little"))
        descriptor = _open_source_descriptor(source_root, record.source)
        try:
            content_digest = hashlib.sha256()
            size = 0
            before = os.fstat(descriptor)
            while True:
                block = os.read(descriptor, CHUNK)
                if not block:
                    break
                size += len(block)
                content_digest.update(block)
                digest.update(block)
            after = os.fstat(descriptor)
            if _identity_tuple(before) != _identity_tuple(after) or size != record.bytes or content_digest.hexdigest() != record.sha256:
                _fail(f"authenticated source identity mismatch: {record.source}")
        except OSError as error:
            _fail(f"cannot read authenticated dependency source {record.source}: {error}")
        finally:
            os.close(descriptor)
    return digest.hexdigest()


def _ledger(records):
    lines = ["kind\tdestination\tsource\tprovenance\tbytes\tsha256\tfixture\n"]
    lines.extend(
        f"{record.kind}\t{record.destination}\t{record.source}\t{record.provenance}\t"
        f"{record.bytes}\t{record.sha256}\t{record.fixture}\n"
        for record in records
    )
    return "".join(lines).encode("utf-8")


def _canonical_digest(value):
    encoded = json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _archived_replay(manifest, records):
    value = manifest.get("archived_replay")
    if value is None:
        return None
    if not isinstance(value, dict) or value.get("schema") != "buster-native-archived-replay-v1" or value.get("version") != 1:
        _fail("archived_replay schema is unsupported")
    axes = value.get("axes")
    if not isinstance(axes, dict):
        _fail("archived_replay.axes must be an object")
    expected_axes = {
        "targets": ARCHIVED_TARGETS,
        "frontend_lowering": ARCHIVED_FRONTENDS,
        "PIC": ARCHIVED_PICS,
        "allocators": ARCHIVED_ALLOCATORS,
    }
    for name, expected in expected_axes.items():
        if tuple(axes.get(name, ())) != expected:
            _fail(f"archived_replay.axes.{name} is not the authenticated matrix")
    fixtures = value.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        _fail("archived_replay.fixtures must be a non-empty row projection")
    project_paths = {
        _include_relative(record.kind, record.destination): record
        for record in records if record.kind == "project-header"
    }
    rows = []
    fixture_map = []
    seen_fixtures = set()
    for fixture_index, item in enumerate(fixtures):
        if not isinstance(item, dict):
            _fail(f"archived_replay fixture {fixture_index} is not an object")
        fixture = item.get("fixture")
        input_sha256 = item.get("input_sha256")
        headers = item.get("project_headers")
        closed_targets = item.get("closed_targets", [])
        pending_targets = item.get("ios_pending_targets", [])
        if not isinstance(fixture, str):
            _fail("archived_replay fixture identities are not unique")
        fixture = _canonical_relative(fixture, f"archived_replay fixture {fixture_index}")
        if fixture in seen_fixtures:
            _fail("archived_replay fixture identities are not unique")
        seen_fixtures.add(fixture)
        if not isinstance(input_sha256, str):
            _fail(f"archived_replay fixture {fixture} has no input identity")
        input_sha256 = _sha256(input_sha256, f"archived_replay fixture {fixture} input_sha256")
        if not isinstance(headers, list) or not headers:
            _fail(f"archived_replay fixture {fixture} has no project-header mapping")
        normalized_headers = []
        for header in headers:
            header = _canonical_relative(header, f"archived_replay fixture {fixture} project header")
            if header not in project_paths:
                _fail(f"archived_replay fixture {fixture} maps an unauthenticated project header: {header}")
            if header in normalized_headers:
                _fail(f"archived_replay fixture {fixture} repeats a project header: {header}")
            normalized_headers.append(header)
        normalized_headers.sort()
        if not isinstance(closed_targets, list) or not isinstance(pending_targets, list):
            _fail(f"archived_replay fixture {fixture} disposition selectors must be lists")
        if set(closed_targets) & set(pending_targets):
            _fail(f"archived_replay fixture {fixture} has overlapping dispositions")
        if any(target not in ARCHIVED_TARGETS for target in closed_targets + pending_targets):
            _fail(f"archived_replay fixture {fixture} names an unknown target")
        if closed_targets and not normalized_headers:
            _fail(f"archived_replay fixture {fixture} closes rows without project headers")
        fixture_map.append({"fixture": fixture, "input_sha256": input_sha256,
                            "project_headers": normalized_headers,
                            "closed_targets": sorted(closed_targets),
                            "ios_pending_targets": sorted(pending_targets)})
        for target in ARCHIVED_TARGETS:
            if target in closed_targets:
                disposition = "repo-owned-project-header"
            elif target in pending_targets:
                disposition = "ios-simd-pending-targetconditionals"
            else:
                disposition = "diagnostic"
            for frontend in ARCHIVED_FRONTENDS:
                for pic in ARCHIVED_PICS:
                    for allocator in ARCHIVED_ALLOCATORS:
                        rows.append({"row": len(rows), "fixture": fixture, "target": target,
                                     "frontend_lowering": frontend, "PIC": pic,
                                     "allocator": allocator, "disposition": disposition,
                                     "project_headers": normalized_headers if disposition == "repo-owned-project-header" else []})
    projection = value.get("projection")
    if not isinstance(projection, dict):
        _fail("archived_replay.projection must be an authenticated object")
    expected_counts = {
        "fixtures": len(fixture_map),
        "mir_candidate_rows": sum(1 for row in rows),
        "repo_owned_project_header_rows_closed": sum(row["disposition"] == "repo-owned-project-header" for row in rows),
        "remaining_diagnostic_rows": sum(row["disposition"] != "repo-owned-project-header" for row in rows),
        "ios_simd_rows_pending_targetconditionals": sum(row["disposition"] == "ios-simd-pending-targetconditionals" for row in rows),
    }
    for field, actual in expected_counts.items():
        if projection.get(field) != actual:
            _fail(f"archived_replay projection count mismatch: {field}")
    row_identity_sha256 = _canonical_digest(rows)
    input_identity_sha256 = _canonical_digest({item["fixture"]: item["input_sha256"] for item in fixture_map})
    fixture_map_sha256 = _canonical_digest(fixture_map)
    if projection.get("row_identity_sha256") != row_identity_sha256:
        _fail("archived_replay row identity digest mismatch")
    if projection.get("input_identity_sha256") != input_identity_sha256:
        _fail("archived_replay input identity digest mismatch")
    if projection.get("fixture_map_sha256") != fixture_map_sha256:
        _fail("archived_replay fixture mapping digest mismatch")
    return {"schema": value["schema"], "version": value["version"],
            "axes": {name: list(expected) for name, expected in expected_axes.items()},
            "projection": expected_counts,
            "row_identity_sha256": row_identity_sha256, "input_identity_sha256": input_identity_sha256,
            "fixture_map_sha256": fixture_map_sha256, "fixtures": fixture_map, "rows": rows}


def _verify_archived_fixture_inputs(replay, source_root):
    contract = Path(source_root) / "docs" / "native-retirement-support-v1.tsv"
    contract_data = _read_no_follow(contract, "support contract")
    contract_sha256 = hashlib.sha256(contract_data).hexdigest()
    if not contract_data or contract_sha256 not in (
            SUPPORT_CONTRACT_SHA256, NEXT_SUPPORT_CONTRACT_SHA256,
            PROPOSED_SUPPORT_CONTRACT_SHA256):
        _fail("archived replay support contract identity mismatch")
    approved = {}
    try:
        lines = contract_data.decode("utf-8").splitlines()
    except UnicodeError as error:
        _fail(f"archived replay support contract is not UTF-8: {error}")
    for line in lines:
        fields = line.split("\t")
        if fields and fields[0] != "path":
            if len(fields) != 5 or fields[0] in approved:
                _fail("archived replay support contract is malformed")
            approved[fields[0]] = (fields[1], fields[3], fields[4])
    for fixture in replay["fixtures"]:
        source = _source_path(source_root, fixture["fixture"])
        actual_bytes, actual_sha256 = _identity(source)
        approved_identity = approved.get(fixture["fixture"])
        if (approved_identity is None or approved_identity[0] != "subject" or
                approved_identity[2] != fixture["input_sha256"]):
            _fail(f"archived replay fixture is not an authenticated subject: {fixture['fixture']}")
        if str(actual_bytes) != approved_identity[1] or actual_sha256 != fixture["input_sha256"]:
            _fail(f"archived replay fixture identity mismatch: {fixture['fixture']}")


def materialize(manifest, source_root, output, descriptor_sha256=None, descriptor_path=None, authority_files=None):
    """Verify and atomically publish a dependency tree.

    ``output`` must not exist.  The return value is a deterministic receipt;
    no network or subprocess is used by this function.
    """
    records, metadata = parse_manifest(manifest)
    external_checkouts, external_generated = _external_declarations(manifest)
    archived_replay = _archived_replay(manifest, records)
    output = Path(output)
    if not output.is_absolute():
        _fail("materializer output must be absolute")
    try:
        output.lstat()
    except FileNotFoundError:
        pass
    except OSError as error:
        _fail(f"cannot inspect materializer output: {output}: {error}")
    else:
        _fail(f"materializer output already exists: {output}")
    _ensure_output_parent(output.parent)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        if archived_replay is not None:
            _verify_archived_fixture_inputs(archived_replay, source_root)
        for record in records:
            _source_path(source_root, record.source)
            destination = temporary / PurePosixPath(record.destination)
            _copy_verified(source_root, record.source, destination, record.bytes, record.sha256)
        ledger = _ledger(records)
        (temporary / "dependencies.tsv").write_bytes(ledger)
        receipt = {
            "schema": SCHEMA,
            "version": VERSION,
            "files": len(records),
            "resource_headers": sum(record.kind == "resource-header" for record in records),
            "project_headers": sum(record.kind == "project-header" for record in records),
            "metadata_names": list(metadata),
            "closure_sha256": _closure(records),
            "ledger_sha256": hashlib.sha256(ledger).hexdigest(),
            "resource_include_sha256": _include_closure(records, "resource-header", source_root),
            "project_include_sha256": _include_closure(records, "project-header", source_root),
            "files_by_destination": [record.destination for record in records],
            "external_checkouts": list(external_checkouts),
            "external_generated": list(external_generated),
        }
        receipt["external_closure_sha256"] = _canonical_digest({
            "external_checkouts": receipt["external_checkouts"],
            "external_generated": receipt["external_generated"],
        })
        if descriptor_sha256 is not None:
            receipt["descriptor_sha256"] = _sha256(descriptor_sha256, "descriptor_sha256")
        if descriptor_path is not None:
            receipt["descriptor_path"] = _canonical_relative(descriptor_path, "descriptor_path")
        if archived_replay is not None:
            receipt["archived_replay"] = archived_replay
        if authority_files is not None:
            allowed = {
                "dependency-policy.json",
                "dependency-source-snapshot.json",
                "dependency-resolved-descriptor.json",
            }
            if not isinstance(authority_files, dict) or set(authority_files) != allowed:
                _fail("materializer authority files are missing or unexpected")
            for name in sorted(allowed):
                data = authority_files[name]
                if not isinstance(data, bytes) or not data:
                    _fail(f"materializer authority file is not non-empty bytes: {name}")
                (temporary / name).write_bytes(data)
        receipt_bytes = (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")
        (temporary / "dependency-manifest.json").write_bytes(receipt_bytes)
        # The generated names are reserved even for the materializer's own
        # output; they are never admitted as source records above.
        os.replace(temporary, output)
        temporary = None
        return receipt
    except Exception:
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)
        raise


def _descriptor_source_root(manifest_path, manifest, source_root):
    declared = manifest.get("source_root")
    if declared is None:
        return Path(source_root)
    declared = _canonical_descriptor_relative(declared, "manifest source_root")
    descriptor_parent = Path(os.path.abspath(os.fspath(manifest_path))).parent
    derived = Path(os.path.abspath(os.fspath(descriptor_parent / PurePosixPath(declared))))
    supplied = Path(os.path.abspath(os.fspath(source_root)))
    if derived != supplied:
        _fail(f"source root is not the descriptor-relative root: {source_root}")
    _assert_directory_no_follow(supplied, "descriptor-relative source root")
    return supplied


def materialize_file(manifest_path, source_root, output):
    manifest_path = Path(manifest_path)
    raw = _read_no_follow(manifest_path, "dependency manifest")
    try:
        manifest = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read dependency manifest {manifest_path}: {error}")
    if not isinstance(manifest, dict):
        _fail("dependency manifest must be a JSON object")
    root = _descriptor_source_root(manifest_path, manifest, source_root)
    label = os.path.relpath(os.path.abspath(os.fspath(manifest_path)), os.path.abspath(os.fspath(root)))
    descriptor_label = _canonical_relative(label, "descriptor_path")
    if manifest.get("schema") == "buster-native-retirement-dependency-policy-v1":
        import native_retirement_dependency_binding as authority
        policy = authority.parse_policy(raw)
        snapshot_raw = _read_no_follow(root / PurePosixPath(authority.SNAPSHOT_PATH), "repository-source snapshot")
        snapshot = authority.parse_snapshot(snapshot_raw, raw, policy)
        legacy_raw = _read_no_follow(root / PurePosixPath(authority.LEGACY_DESCRIPTOR_PATH), "legacy dependency descriptor")
        resolved_raw = authority.render_resolved_descriptor(policy, snapshot, legacy_raw)
        resolved = authority.strict_json(resolved_raw, "resolved dependency descriptor")
        return materialize(
            resolved,
            root,
            output,
            hashlib.sha256(resolved_raw).hexdigest(),
            descriptor_label,
            authority_files={
                "dependency-policy.json": raw,
                "dependency-source-snapshot.json": snapshot_raw,
                "dependency-resolved-descriptor.json": resolved_raw,
            },
        )
    descriptor_sha256 = hashlib.sha256(raw).hexdigest()
    return materialize(manifest, root, output, descriptor_sha256, descriptor_label)


def _self_test():
    """Small offline protocol check used by CI before a real checkout run."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory).resolve()
        source = root / "repo" / "src" / "header.h"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"#define MATERIALIZER_SELF_TEST 1\n")
        size, digest = _identity(source)
        manifest = {
            "schema": SCHEMA,
            "version": VERSION,
            "files": [{"kind": "project-header", "source": "src/header.h",
                        "provenance": "repo:src/header.h",
                        "destination": "dependencies/project-include/header.h",
                        "bytes": size, "sha256": digest}],
        }
        output = root / "out"
        receipt = materialize(manifest, root / "repo", output)
        if receipt["project_headers"] != 1 or (output / "dependencies/project-include/header.h").read_bytes() != source.read_bytes():
            _fail("materializer self-test publication mismatch")
        try:
            bad = dict(manifest, files=[dict(manifest["files"][0], sha256="0" * 64)])
            materialize(bad, root / "repo", root / "bad")
        except MaterializationError:
            pass
        else:
            _fail("materializer self-test admitted a digest mismatch")
    return {"schema": SCHEMA, "self_test": True}


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("materialize", "self-test"))
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--output", type=Path)
    return parser


def main(argv=None):
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "self-test":
            receipt = _self_test()
        else:
            if not arguments.manifest or not arguments.source_root or not arguments.output:
                raise MaterializationError("materialize requires --manifest, --source-root, and --output")
            receipt = materialize_file(arguments.manifest, arguments.source_root, arguments.output)
        print(json.dumps(receipt, sort_keys=True))
        return 0
    except (MaterializationError, OSError, ValueError, TypeError) as error:
        print(f"native-retirement materializer failure: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
