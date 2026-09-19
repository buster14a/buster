#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import stat
import tempfile
from typing import Any

from native_retirement_candidate_core import (
    ACCEPTANCE_SCHEMA,
    CandidateError,
    MAX_RECEIPTS,
    READ_CHUNK,
    REQUIRED_GATES,
    _digest,
    _fail,
    _integer,
    canonical_bytes,
    load_json,
    validate_identity,
)
from native_retirement_candidate_receipt import validate_receipt

def _open_artifact(root: Path, relative: str) -> tuple[int, os.stat_result, int]:
    required = ("O_DIRECTORY", "O_NOFOLLOW")
    if any(not hasattr(os, name) for name in required) or os.open not in os.supports_dir_fd:
        _fail("artifact verification requires descriptor-relative no-follow support")
    root_status = root.lstat()
    if not stat.S_ISDIR(root_status.st_mode):
        _fail("artifact root must be one real directory")
    directories: list[int] = []
    success = False
    descriptor = -1
    try:
        current = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        directories.append(current)
        components = relative.split("/")
        for component in components[:-1]:
            current = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=current)
            directories.append(current)
        descriptor = os.open(components[-1], os.O_RDONLY | os.O_NOFOLLOW, dir_fd=current)
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            _fail(f"artifact {relative}: must be one regular, non-linked file")
        success = True
        return descriptor, status, directories[-1]
    except CandidateError:
        raise
    except OSError as error:
        raise CandidateError(f"artifact {relative}: cannot open without following links: {error}") from error
    finally:
        if not success and descriptor >= 0:
            os.close(descriptor)
        retained = 1 if success else 0
        for directory in reversed(directories[:len(directories) - retained]):
            os.close(directory)


def _verify_artifact(root: Path, relative: str, expected_bytes: int, expected_digest: str) -> None:
    descriptor, opened, parent = _open_artifact(root, relative)
    count = 0
    digest = hashlib.sha256()
    try:
        while True:
            chunk = os.read(descriptor, READ_CHUNK)
            if not chunk:
                break
            count += len(chunk)
            digest.update(chunk)
            if count > expected_bytes:
                _fail(f"artifact {relative}: larger than declared")
        final = os.fstat(descriptor)
        entry = os.stat(relative.split("/")[-1], dir_fd=parent, follow_symlinks=False)
        stable = ("st_dev", "st_ino", "st_mode", "st_nlink", "st_size", "st_mtime_ns", "st_ctime_ns")
        if any(getattr(opened, field) != getattr(final, field) or getattr(opened, field) != getattr(entry, field) for field in stable):
            _fail(f"artifact {relative}: changed or was replaced during verification")
    finally:
        os.close(descriptor)
        os.close(parent)
    if count != expected_bytes or opened.st_size != expected_bytes:
        _fail(f"artifact {relative}: byte count differs")
    if digest.hexdigest() != expected_digest:
        _fail(f"artifact {relative}: SHA-256 differs")


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".partial-", dir=path.parent)
    temporary_path = Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, path)
        try:
            directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        except OSError:
            pass
    finally:
        temporary_path.unlink(missing_ok=True)


def _receipt_paths(directory: Path) -> list[Path]:
    try:
        with os.scandir(directory) as scan:
            entries = sorted(scan, key=lambda entry: entry.name)
    except OSError as error:
        raise CandidateError(f"cannot list receipts directory: {error}") from error
    paths: list[Path] = []
    for entry in entries:
        if entry.name.endswith(".json"):
            if entry.is_symlink() or not entry.is_file(follow_symlinks=False):
                _fail(f"receipt {entry.name}: must be a regular non-link")
            paths.append(Path(entry.path))
    if not paths:
        _fail("no receipts found")
    if len(paths) > MAX_RECEIPTS:
        _fail(f"too many receipts: {len(paths)} > {MAX_RECEIPTS}")
    return paths


def verify(identity_path: Path, receipts_directory: Path, artifact_root: Path, output: Path) -> dict[str, Any]:
    identity_document = load_json(identity_path)
    identity = validate_identity(identity_document)
    if not receipts_directory.is_dir():
        _fail(f"missing receipts directory: {receipts_directory}")
    receipts: dict[str, str] = {}
    artifact_paths: set[str] = set()
    for path in _receipt_paths(receipts_directory):
        document = load_json(path)
        try:
            gate, artifact_path = validate_receipt(document, identity)
        except CandidateError as error:
            raise CandidateError(f"{path.name}: {error}") from error
        if gate in receipts:
            _fail(f"duplicate gate receipt: {gate}")
        if artifact_path in artifact_paths:
            _fail(f"duplicate artifact path: {artifact_path}")
        _verify_artifact(
            artifact_root,
            artifact_path,
            _integer(document, "artifact_bytes", 1),
            _digest(document, "artifact_sha256"),
        )
        artifact_paths.add(artifact_path)
        receipts[gate] = hashlib.sha256(canonical_bytes(document)).hexdigest()
    missing = sorted(REQUIRED_GATES - receipts.keys())
    if missing:
        _fail("missing required gates: " + ", ".join(missing))
    acceptance = {
        "schema": ACCEPTANCE_SCHEMA,
        **identity,
        "required_gates": sorted(REQUIRED_GATES),
        "receipt_sha256": {gate: receipts[gate] for gate in sorted(receipts)},
    }
    _atomic_write(output, canonical_bytes(acceptance))
    return acceptance
