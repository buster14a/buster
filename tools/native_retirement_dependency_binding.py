#!/usr/bin/env python3
"""Native-retirement policy, repository snapshot, and aggregate authority.

The reviewed policy deliberately omits byte/hash fields from eligible
``repo:<source>`` records.  Those identities live only in the generated source
snapshot.  The generated C header is the sole tracked authority for the live
policy/materializer/project/ledger quartet and is parsed by Python consumers as
well as included by the C census.

The old monolithic v1 descriptor remains available only as an immutable legacy
compatibility input for archived evidence.  It is never refreshed.
"""

from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
from typing import Any, Callable, Mapping

POLICY_PATH = "docs/native-retirement-dependencies-v1.json"
LEGACY_DESCRIPTOR_PATH = "docs/native-retirement-dependencies-legacy-v1.json"
SNAPSHOT_PATH = "docs/native-retirement-repository-sources-v1.json"
BINDING_PATH = "tools/native_retirement_dependency_binding.generated.h"
POLICY_SCHEMA = "buster-native-retirement-dependency-policy-v1"
POLICY_VERSION = 1
LEGACY_SCHEMA = "buster-native-retirement-dependencies-v1"
LEGACY_VERSION = 1
SNAPSHOT_SCHEMA = "buster-native-retirement-repository-sources-v1"
SNAPSHOT_VERSION = 1
BINDING_SCHEMA = "buster-native-retirement-dependency-binding-v1"
BINDING_VERSION = 1
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")


class BindingError(ValueError):
    """The policy, generated snapshot, or generated aggregate binding is invalid."""


def _fail(message: str) -> None:
    raise BindingError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=True, indent=2) + "\n").encode("utf-8")


def _object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def strict_json(raw: bytes, label: str) -> Any:
    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=_object_pairs)
    except (UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot decode {label}: {error}")


def _exact_fields(value: Mapping[str, Any], fields: set[str], label: str) -> None:
    actual = set(value)
    if actual != fields:
        _fail(f"{label} fields differ (missing={sorted(fields-actual)}, unexpected={sorted(actual-fields)})")


def _relative(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(f"{label} must be non-empty text")
    path = PurePosixPath(value)
    if path.is_absolute() or value != path.as_posix() or any(part in ("", ".", "..") for part in path.parts):
        _fail(f"{label} is not a canonical relative path: {value!r}")
    return value


def _sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        _fail(f"{label} is not a canonical SHA-256")
    return value


def parse_policy(raw: bytes) -> dict[str, Any]:
    policy = strict_json(raw, "dependency policy")
    if not isinstance(policy, dict):
        _fail("dependency policy must be an object")
    if policy.get("schema") != POLICY_SCHEMA or policy.get("version") != POLICY_VERSION:
        _fail("unsupported dependency policy schema/version")
    legacy = policy.get("legacy_descriptor")
    if not isinstance(legacy, dict) or set(legacy) != {"path", "sha256"}:
        _fail("dependency policy must bind one legacy descriptor")
    if legacy["path"] != LEGACY_DESCRIPTOR_PATH:
        _fail("dependency policy legacy descriptor path mismatch")
    _sha256(legacy["sha256"], "dependency policy legacy descriptor sha256")
    # Live policy must not retain a second mutable copy of repository identities.
    for index, record in enumerate(policy.get("projects", ())):
        if not isinstance(record, dict):
            _fail(f"dependency policy projects[{index}] must be an object")
        source = record.get("source")
        if record.get("provenance") == f"repo:{source}":
            if "bytes" in record or "sha256" in record:
                _fail(f"repository policy record carries generated identity fields: {source}")
    return policy


def policy_from_legacy(raw: bytes) -> tuple[bytes, dict[str, Any]]:
    """Migrate the checked-in v1 descriptor without changing reviewed declarations."""
    legacy = strict_json(raw, "legacy dependency descriptor")
    if not isinstance(legacy, dict) or legacy.get("schema") != LEGACY_SCHEMA or legacy.get("version") != LEGACY_VERSION:
        _fail("unsupported legacy dependency descriptor schema/version")
    migrated = copy.deepcopy(legacy)
    policy = {}
    for key, value in migrated.items():
        if key == "schema":
            policy[key] = POLICY_SCHEMA
        elif key == "version":
            policy[key] = POLICY_VERSION
            policy["legacy_descriptor"] = {"path": LEGACY_DESCRIPTOR_PATH, "sha256": sha256_bytes(raw)}
        else:
            policy[key] = value
    for index, record in enumerate(policy.get("projects", ())):
        if not isinstance(record, dict):
            _fail(f"legacy dependency projects[{index}] must be an object")
        source = record.get("source")
        if record.get("provenance") == f"repo:{source}":
            if "bytes" not in record or "sha256" not in record:
                _fail(f"legacy repository record lacks byte/hash identity: {source}")
            del record["bytes"]
            del record["sha256"]
    rendered = canonical_json(policy)
    return rendered, parse_policy(rendered)


def repository_policy_records(policy: Mapping[str, Any]) -> tuple[dict[str, str], ...]:
    projects = policy.get("projects")
    if not isinstance(projects, list):
        _fail("dependency policy projects must be an array")
    records: list[dict[str, str]] = []
    sources: set[str] = set()
    destinations: set[str] = set()
    for index, record in enumerate(projects):
        if not isinstance(record, dict):
            _fail(f"dependency policy projects[{index}] must be an object")
        source = record.get("source")
        provenance = record.get("provenance")
        if not isinstance(source, str) or not isinstance(provenance, str) or not provenance.startswith("repo:"):
            continue
        source = _relative(source, f"dependency policy projects[{index}].source")
        destination = _relative(record.get("destination"), f"dependency policy projects[{index}].destination")
        if provenance != f"repo:{source}":
            _fail(f"repository policy provenance does not exactly name its source: {source}")
        if "bytes" in record or "sha256" in record:
            _fail(f"repository policy record carries generated identity fields: {source}")
        if source in sources or destination in destinations:
            _fail(f"duplicate repository policy source/destination: {source}")
        sources.add(source)
        destinations.add(destination)
        records.append({"source": source, "destination": destination})
    records.sort(key=lambda item: item["source"])
    return tuple(records)


def render_snapshot(policy_raw: bytes, policy: Mapping[str, Any], identity: Callable[[str], tuple[int, str]]) -> tuple[bytes, tuple[dict[str, Any], ...]]:
    records: list[dict[str, Any]] = []
    for item in repository_policy_records(policy):
        size, digest = identity(item["source"])
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            _fail(f"invalid generated byte count for {item['source']}")
        records.append({"source": item["source"], "bytes": size,
                        "sha256": _sha256(digest, f"generated SHA-256 for {item['source']}")})
    value = {"schema": SNAPSHOT_SCHEMA, "version": SNAPSHOT_VERSION,
             "policy_path": POLICY_PATH, "policy_sha256": sha256_bytes(policy_raw),
             "records": records}
    return canonical_json(value), tuple(records)


def parse_snapshot(raw: bytes, policy_raw: bytes, policy: Mapping[str, Any]) -> tuple[dict[str, Any], ...]:
    value = strict_json(raw, "repository-source snapshot")
    if not isinstance(value, dict):
        _fail("repository-source snapshot must be an object")
    _exact_fields(value, {"schema", "version", "policy_path", "policy_sha256", "records"}, "repository-source snapshot")
    if value["schema"] != SNAPSHOT_SCHEMA or value["version"] != SNAPSHOT_VERSION:
        _fail("unsupported repository-source snapshot schema/version")
    if value["policy_path"] != POLICY_PATH or value["policy_sha256"] != sha256_bytes(policy_raw):
        _fail("repository-source snapshot policy binding mismatch")
    if not isinstance(value["records"], list):
        _fail("repository-source snapshot records must be an array")
    parsed: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, record in enumerate(value["records"]):
        if not isinstance(record, dict):
            _fail(f"repository-source snapshot records[{index}] must be an object")
        _exact_fields(record, {"source", "bytes", "sha256"}, f"repository-source snapshot records[{index}]")
        source = _relative(record["source"], f"repository-source snapshot records[{index}].source")
        if source in seen:
            _fail(f"duplicate repository-source snapshot record: {source}")
        seen.add(source)
        size = record["bytes"]
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            _fail(f"invalid repository-source snapshot byte count: {source}")
        parsed.append({"source": source, "bytes": size,
                       "sha256": _sha256(record["sha256"], f"repository-source snapshot SHA-256: {source}")})
    expected = [item["source"] for item in repository_policy_records(policy)]
    actual = [item["source"] for item in parsed]
    if actual != sorted(actual):
        _fail("repository-source snapshot records are not sorted by source")
    if actual != expected:
        _fail("repository-source snapshot does not exactly cover reviewed repository policy")
    return tuple(parsed)


def resolved_manifest(policy: Mapping[str, Any], snapshot: tuple[dict[str, Any], ...]) -> dict[str, Any]:
    candidate = copy.deepcopy(policy)
    candidate["schema"] = LEGACY_SCHEMA
    candidate["version"] = LEGACY_VERSION
    candidate.pop("legacy_descriptor", None)
    identities = {item["source"]: item for item in snapshot}
    consumed: set[str] = set()
    for record in candidate.get("projects", ()):
        source = record.get("source")
        if isinstance(source, str) and record.get("provenance") == f"repo:{source}":
            generated = identities.get(source)
            if generated is None:
                _fail(f"repository-source snapshot is missing policy source: {source}")
            # Reinsert in the legacy canonical location: immediately after destination.
            rebuilt = {}
            inserted = False
            for key, value in record.items():
                rebuilt[key] = value
                if key == "destination":
                    rebuilt["bytes"] = generated["bytes"]
                    rebuilt["sha256"] = generated["sha256"]
                    inserted = True
            if not inserted:
                _fail(f"repository policy record has no destination: {source}")
            record.clear()
            record.update(rebuilt)
            consumed.add(source)
    if consumed != set(identities):
        _fail("repository-source snapshot contains an unconsumed source")
    return candidate


def render_resolved_descriptor(policy: Mapping[str, Any], snapshot: tuple[dict[str, Any], ...], legacy_raw: bytes | None = None) -> bytes:
    resolved = resolved_manifest(policy, snapshot)
    if legacy_raw is not None:
        declaration = policy.get("legacy_descriptor")
        if (not isinstance(declaration, dict) or declaration.get("path") != LEGACY_DESCRIPTOR_PATH or
                declaration.get("sha256") != sha256_bytes(legacy_raw)):
            _fail("legacy dependency descriptor does not match reviewed policy")
        legacy = strict_json(legacy_raw, "legacy dependency descriptor")
        if legacy == resolved:
            return legacy_raw
    return canonical_json(resolved)


_BINDING_STRING_FIELDS = (
    ("BUSTER_NATIVE_RETIREMENT_BINDING_SCHEMA", "schema"),
    ("BUSTER_NATIVE_RETIREMENT_POLICY_PATH", "policy_path"),
    ("BUSTER_NATIVE_RETIREMENT_POLICY_SHA256", "policy_sha256"),
    ("BUSTER_NATIVE_RETIREMENT_SNAPSHOT_PATH", "snapshot_path"),
    ("BUSTER_NATIVE_RETIREMENT_SNAPSHOT_SHA256", "snapshot_sha256"),
    ("BUSTER_NATIVE_RETIREMENT_RECEIPT_SHA256", "receipt_sha256"),
    ("BUSTER_NATIVE_RETIREMENT_PROJECT_SHA256", "project_sha256"),
    ("BUSTER_NATIVE_RETIREMENT_LEDGER_SHA256", "ledger_sha256"),
)


def render_binding(values: Mapping[str, Any]) -> bytes:
    required = {field for _macro, field in _BINDING_STRING_FIELDS} | {"version"}
    if set(values) != required or values.get("schema") != BINDING_SCHEMA or values.get("version") != BINDING_VERSION:
        _fail("aggregate binding fields/schema/version are invalid")
    if values["policy_path"] != POLICY_PATH or values["snapshot_path"] != SNAPSHOT_PATH:
        _fail("aggregate binding paths are not canonical")
    for field in ("policy_sha256", "snapshot_sha256", "receipt_sha256", "project_sha256", "ledger_sha256"):
        _sha256(values[field], f"aggregate binding {field}")
    lines = ["/* Generated by tools/native_retirement_rebind.py; do not edit. */",
             "#ifndef BUSTER_NATIVE_RETIREMENT_DEPENDENCY_BINDING_GENERATED_H",
             "#define BUSTER_NATIVE_RETIREMENT_DEPENDENCY_BINDING_GENERATED_H", ""]
    lines.append(f'#define {_BINDING_STRING_FIELDS[0][0]} "{values["schema"]}"')
    lines.append(f'#define BUSTER_NATIVE_RETIREMENT_BINDING_VERSION {values["version"]}')
    for macro, field in _BINDING_STRING_FIELDS[1:]:
        lines.append(f'#define {macro} "{values[field]}"')
    lines += ["", "#endif /* BUSTER_NATIVE_RETIREMENT_DEPENDENCY_BINDING_GENERATED_H */", ""]
    return "\n".join(lines).encode("ascii")


def parse_binding(raw: bytes) -> dict[str, Any]:
    try:
        text = raw.decode("ascii")
    except UnicodeError as error:
        _fail(f"aggregate binding is not ASCII: {error}")
    macros: dict[str, str] = {}
    for line in text.splitlines():
        match = re.fullmatch(r'#define ([A-Z0-9_]+) (?:(?:"([^"]*)")|([0-9]+))', line)
        if match is None:
            continue
        name = match.group(1)
        if name in macros:
            _fail(f"duplicate aggregate binding macro: {name}")
        macros[name] = match.group(2) if match.group(2) is not None else match.group(3)
    expected = {macro for macro, _field in _BINDING_STRING_FIELDS} | {"BUSTER_NATIVE_RETIREMENT_BINDING_VERSION"}
    if set(macros) != expected:
        _fail("aggregate binding macros are missing or unexpected")
    values = {field: macros[macro] for macro, field in _BINDING_STRING_FIELDS}
    try:
        values["version"] = int(macros["BUSTER_NATIVE_RETIREMENT_BINDING_VERSION"])
    except ValueError:
        _fail("aggregate binding version is not decimal")
    if raw != render_binding(values):
        _fail("aggregate binding is not in canonical generated form")
    return values


def verify_materialized_identities(binding: Mapping[str, Any], identities: Mapping[str, Any]) -> None:
    for field in ("policy_sha256", "receipt_sha256", "project_sha256", "ledger_sha256"):
        actual = _sha256(identities.get(field), f"independent materialization {field}")
        if binding.get(field) != actual:
            _fail(f"aggregate binding {field} does not match independent materialization")


def _read_regular(path: Path, label: str) -> bytes:
    try:
        info = path.lstat()
    except OSError as error:
        _fail(f"cannot inspect {label}: {error}")
    if path.is_symlink() or not path.is_file() or info.st_nlink != 1:
        _fail(f"{label} is not a single-link regular file")
    return path.read_bytes()


def load_policy_snapshot(root: Path) -> tuple[bytes, dict[str, Any], bytes, tuple[dict[str, Any], ...]]:
    root = Path(os.path.abspath(os.fspath(root)))
    policy_raw = _read_regular(root / PurePosixPath(POLICY_PATH), "dependency policy")
    policy = parse_policy(policy_raw)
    snapshot_raw = _read_regular(root / PurePosixPath(SNAPSHOT_PATH), "repository-source snapshot")
    snapshot = parse_snapshot(snapshot_raw, policy_raw, policy)
    return policy_raw, policy, snapshot_raw, snapshot


def load_authority(root: Path) -> tuple[dict[str, Any], dict[str, Any], tuple[dict[str, Any], ...]]:
    root = Path(os.path.abspath(os.fspath(root)))
    policy_raw, policy, snapshot_raw, snapshot = load_policy_snapshot(root)
    binding = parse_binding(_read_regular(root / PurePosixPath(BINDING_PATH), "aggregate binding"))
    if binding["policy_sha256"] != sha256_bytes(policy_raw):
        _fail("aggregate binding policy identity mismatch")
    if binding["snapshot_sha256"] != sha256_bytes(snapshot_raw):
        _fail("aggregate binding snapshot identity mismatch")
    return binding, resolved_manifest(policy, snapshot), snapshot
