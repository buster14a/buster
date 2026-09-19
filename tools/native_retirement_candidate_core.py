#!/usr/bin/env python3
"""Bind native-retirement acceptance to one immutable MIR candidate.

Every producer emits one compact receipt plus one content-addressed artifact.
The final verifier accepts only one exact candidate commit/tree, platform-bound
trusted binary identities, frozen #508/#509 contracts, the complete gate set,
and byte-for-byte matching artifacts.  It rejects pull-request merge commits,
predecessor builds, stale compiler binaries, duplicate gates or paths, direct
native reachability, and partial publication.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
from typing import Any, Iterable

IDENTITY_SCHEMA = "buster.native-retirement.candidate.v1"
RECEIPT_SCHEMA = "buster.native-retirement.receipt.v1"
ACCEPTANCE_SCHEMA = "buster.native-retirement.acceptance.v1"
BACKEND = "mir-native-v1"
MAX_JSON_BYTES = 1024 * 1024
MAX_RECEIPTS = 64
READ_CHUNK = 64 * 1024

REQUIRED_GATES = frozenset(
    {
        "census-508",
        "strict-linux-x86_64",
        "strict-linux-aarch64",
        "strict-macos-x86_64",
        "strict-macos-aarch64",
        "strict-windows-x86_64",
        "strict-windows-aarch64",
        "sanitizer-linux-x86_64",
        "sanitizer-linux-aarch64",
        "sanitizer-macos-x86_64",
        "sanitizer-macos-aarch64",
        "sanitizer-windows-x86_64",
        "repeated-self-host-509",
        "semantic-509",
        "production-direct-reachability",
        "final-change-comparison-512",
    }
)
GATE_BINARY = {
    "census-508": "linux-x86_64",
    "strict-linux-x86_64": "linux-x86_64",
    "strict-linux-aarch64": "linux-aarch64",
    "strict-macos-x86_64": "macos-x86_64",
    "strict-macos-aarch64": "macos-aarch64",
    "strict-windows-x86_64": "windows-x86_64",
    "strict-windows-aarch64": "windows-aarch64",
    "sanitizer-linux-x86_64": "linux-x86_64",
    "sanitizer-linux-aarch64": "linux-aarch64",
    "sanitizer-macos-x86_64": "macos-x86_64",
    "sanitizer-macos-aarch64": "macos-aarch64",
    "sanitizer-windows-x86_64": "windows-x86_64",
    "repeated-self-host-509": "linux-x86_64",
    "semantic-509": "linux-x86_64",
    "production-direct-reachability": "source-verifier",
    "final-change-comparison-512": "bench-linux-x86_64",
}
REQUIRED_BINARIES = frozenset(GATE_BINARY.values())
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
TOKEN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.+-]{0,127}\Z")


class CandidateError(ValueError):
    """A structured candidate, receipt, or artifact contract violation."""


def _fail(message: str) -> None:
    raise CandidateError(message)


def _pairs_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail(f"duplicate JSON member: {key}")
        result[key] = value
    return result


def _json_constant(value: str) -> None:
    _fail(f"non-finite JSON constant is forbidden: {value}")


def _parse_json(raw: bytes, name: str) -> dict[str, Any]:
    if len(raw) > MAX_JSON_BYTES:
        _fail(f"{name}: exceeds {MAX_JSON_BYTES} bytes")
    try:
        text = raw.decode("utf-8", errors="strict")
        value = json.loads(
            text,
            object_pairs_hook=_pairs_no_duplicates,
            parse_constant=_json_constant,
        )
    except CandidateError:
        raise
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as error:
        raise CandidateError(f"{name}: invalid JSON: {error}") from error
    if not isinstance(value, dict):
        _fail(f"{name}: root must be an object")
    return value


def load_json(path: Path) -> dict[str, Any]:
    try:
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            _fail(f"{path}: must be one regular, non-linked file")
        return _parse_json(path.read_bytes(), str(path))
    except CandidateError:
        raise
    except OSError as error:
        raise CandidateError(f"cannot read {path}: {error}") from error


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode("ascii")


def _exact_keys(obj: dict[str, Any], expected: set[str], name: str) -> None:
    actual = set(obj)
    if actual != expected:
        _fail(f"{name} fields differ: missing={sorted(expected - actual)} unknown={sorted(actual - expected)}")


def _string(obj: dict[str, Any], key: str) -> str:
    value = obj.get(key)
    if not isinstance(value, str) or not value:
        _fail(f"{key}: expected non-empty string")
    return value


def _token(obj: dict[str, Any], key: str) -> str:
    value = _string(obj, key)
    if not TOKEN.fullmatch(value):
        _fail(f"{key}: expected canonical token")
    return value


def _boolean(obj: dict[str, Any], key: str) -> bool:
    value = obj.get(key)
    if not isinstance(value, bool):
        _fail(f"{key}: expected boolean")
    return value


def _integer(obj: dict[str, Any], key: str, minimum: int = 0) -> int:
    value = obj.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum or value > (1 << 63) - 1:
        _fail(f"{key}: expected integer in [{minimum}, 2^63-1]")
    return value


def _digest(obj: dict[str, Any], key: str) -> str:
    value = _string(obj, key)
    if not HEX64.fullmatch(value):
        _fail(f"{key}: expected lowercase SHA-256")
    return value


def _commit(obj: dict[str, Any], key: str) -> str:
    value = _string(obj, key)
    if not HEX40.fullmatch(value):
        _fail(f"{key}: expected full lowercase Git commit")
    return value


def _relative_path(obj: dict[str, Any], key: str) -> str:
    value = _string(obj, key)
    parts = value.split("/")
    if value.startswith("/") or value.endswith("/") or "\\" in value or any(part in ("", ".", "..") for part in parts):
        _fail(f"{key}: expected canonical relative path")
    if len(value.encode("utf-8", errors="strict")) > 4096:
        _fail(f"{key}: path is too long")
    return value


def validate_identity(identity: dict[str, Any]) -> dict[str, Any]:
    expected = {
        "schema", "candidate_commit", "candidate_tree", "checkout_commit", "candidate_source",
        "backend_identity", "census_contract_sha256", "gates_contract_sha256", "trusted_binaries",
    }
    _exact_keys(identity, expected, "candidate identity")
    if identity.get("schema") != IDENTITY_SCHEMA:
        _fail(f"schema: expected {IDENTITY_SCHEMA}")
    candidate_commit = _commit(identity, "candidate_commit")
    checkout_commit = _commit(identity, "checkout_commit")
    if checkout_commit != candidate_commit:
        _fail("checkout_commit does not equal candidate_commit; synthetic merge or stale checkout evidence is forbidden")
    source = _string(identity, "candidate_source")
    if source not in {"pull_request_head", "workflow_dispatch_exact_commit"}:
        _fail("candidate_source must identify an exact head/commit, never github.sha from a PR merge ref")
    backend = _string(identity, "backend_identity")
    if backend != BACKEND:
        _fail(f"backend_identity: expected {BACKEND}")
    candidate_tree = _commit(identity, "candidate_tree")
    binaries = identity.get("trusted_binaries")
    if not isinstance(binaries, dict):
        _fail("trusted_binaries: expected object")
    if set(binaries) != set(REQUIRED_BINARIES):
        _fail(f"trusted_binaries differ: missing={sorted(REQUIRED_BINARIES - set(binaries))} unknown={sorted(set(binaries) - REQUIRED_BINARIES)}")
    normalized: dict[str, dict[str, Any]] = {}
    for binary_id, entry in binaries.items():
        if not TOKEN.fullmatch(binary_id) or not isinstance(entry, dict):
            _fail(f"trusted_binaries.{binary_id}: invalid entry")
        _exact_keys(entry, {"kind", "target", "sha256", "bytes", "build_commit", "build_tree"}, f"trusted_binaries.{binary_id}")
        kind = _string(entry, "kind")
        if kind not in {"compiler", "verifier"}:
            _fail(f"trusted_binaries.{binary_id}.kind: expected compiler or verifier")
        if binary_id == "source-verifier" and kind != "verifier":
            _fail("source-verifier must have verifier kind")
        if binary_id != "source-verifier" and kind != "compiler":
            _fail(f"{binary_id} must have compiler kind")
        if _commit(entry, "build_commit") != candidate_commit or _commit(entry, "build_tree") != candidate_tree:
            _fail(f"trusted_binaries.{binary_id}: predecessor or unrelated build identity")
        normalized[binary_id] = {
            "kind": kind,
            "target": _token(entry, "target"),
            "sha256": _digest(entry, "sha256"),
            "bytes": _integer(entry, "bytes", 1),
            "build_commit": candidate_commit,
            "build_tree": candidate_tree,
        }
    return {
        "candidate_commit": candidate_commit,
        "candidate_tree": candidate_tree,
        "backend_identity": backend,
        "census_contract_sha256": _digest(identity, "census_contract_sha256"),
        "gates_contract_sha256": _digest(identity, "gates_contract_sha256"),
        "trusted_binaries": normalized,
    }
