#!/usr/bin/env python3
"""Validate an immutable native-retirement performance binding.

The census validator proves that one object-census directory is internally
consistent.  This validator proves the stronger, cross-run identity contract
needed by the retirement performance gate: the approved policy, the complete
required population, both compiler subjects, their producer, the admitted
service/host, and the statistical rules are all bound before measurements are
considered.  The evidence form parses the versioned canonical row artifact,
streams typed observations, and replays the reviewed C statistics adapter from
checked-in sources; the structural form is explicitly not proof.
"""

import argparse
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sqlite3
import stat
import subprocess
import sys
import tarfile
import tempfile
from contextlib import closing

try:
    import native_retirement_performance_schema as RETIREMENT_SCHEMA
except ImportError:
    _performance_schema_spec = importlib.util.spec_from_file_location(
        "native_retirement_performance_schema",
        Path(__file__).resolve().with_name("native_retirement_performance_schema.py"))
    if _performance_schema_spec is None or _performance_schema_spec.loader is None:
        raise RuntimeError("native retirement performance schema is unavailable")
    RETIREMENT_SCHEMA = importlib.util.module_from_spec(_performance_schema_spec)
    _performance_schema_spec.loader.exec_module(RETIREMENT_SCHEMA)

try:
    import native_retirement_result_input as RESULT_INPUT
except ImportError:
    _result_input_spec = importlib.util.spec_from_file_location(
        "native_retirement_result_input",
        Path(__file__).resolve().with_name("native_retirement_result_input.py"))
    if _result_input_spec is None or _result_input_spec.loader is None:
        raise RuntimeError("native_retirement_result_input.py is required by the #615 contract")
    RESULT_INPUT = importlib.util.module_from_spec(_result_input_spec)
    sys.modules[_result_input_spec.name] = RESULT_INPUT
    _result_input_spec.loader.exec_module(RESULT_INPUT)
RESULT_INPUT_HARD_CAPS = RESULT_INPUT.HARD_CAPS


SCHEMA = "buster-native-retirement-performance-binding-v1"
DECISION_ID = "native-retirement-performance-v1"
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$")
# The reviewed C adapter reads member identities with ``%127s`` into a
# 128-byte buffer.  Keep the derived family names within that exact token
# domain instead of relying on C truncation to make malformed input fail.
ADAPTER_MEMBER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:/=-]{0,126}$")
MUTABLE_TOKENS = {"HEAD", "head", "main", "master", "latest", "tip", "UNBOUND"}

ALLOCATORS = ["none", "mir-stack", "fast", "quality"]
FRONTENDS = ["direct-ssa", "local-backed-canonical"]
PIC = ["0", "1"]
STAGES = ["object", "link", "self-host-stage1"]
TARGETS = [
    "aarch64-apple-ios", "aarch64-apple-macos", "aarch64-linux-android",
    "aarch64-pc-windows-msvc", "aarch64-unknown-linux-gnu", "aarch64-unknown-uefi",
    "x86_64-apple-ios", "x86_64-apple-macos", "x86_64-linux-android",
    "x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu", "x86_64-unknown-uefi",
]
ROW_IDENTITY_FIELDS = [
    "fixture", "target", "target_abi", "cpu", "cpu_features", "allocator",
    "frontend_lowering", "PIC", "fixture_recipe", "compile_obligation",
    "link_obligation", "execution_obligation", "diagnostic_obligation",
    "argv_evidence", "artifact_stage",
]
OUTCOMES = ["pass", "regression", "inconclusive", "invalid"]
METRICS = ["compiler_wall_time", "compiler_peak_rss", "generated_code_bytes",
           "generated_runtime"]
STATISTICAL_METRICS = ["compiler_wall_time", "compiler_peak_rss", "generated_runtime"]
AGGREGATE_THRESHOLDS = {
    "compiler_wall_time": Decimal("1.02"),
    "compiler_peak_rss": Decimal("1.02"),
    "generated_code_bytes": Decimal("1.01"),
    "generated_runtime": Decimal("1.03"),
}
CELL_THRESHOLDS = {
    "compiler_wall_time": Decimal("1.05"),
    "compiler_peak_rss": Decimal("1.05"),
    "generated_code_bytes": Decimal("1.01"),
    "generated_runtime": Decimal("1.03"),
}
SUPPORT_FILE_ROLES = [
    "support_declaration", "performance_declaration", "manifest", "inputs",
    "rows", "dependencies", "environment", "performance_rows", "validator_report",
]
PERFORMANCE_SOURCE_ROLES = [
    "support_declaration", "performance_declaration", "manifest", "inputs",
    "rows", "dependencies", "environment", "validator_report",
]
# The canonical row artifact is produced from the independent census output;
# it cannot include its own performance-declaration digest without a hash
# cycle.  The binding/population still carries that digest separately.
ROW_SOURCE_ROLES = [
    "support_declaration", "manifest", "inputs", "rows", "dependencies",
    "environment", "validator_report",
]
REQUESTED_WORK_KINDS = {
    "source", "generated", "header", "resource", "system", "sysroot", "sdk",
    "tool", "workload-input", "inputs", "dependencies", "resources", "workloads",
}
REQUIRED_WORK_CLOSURE_KINDS = (
    "inputs", "dependencies", "resources", "sysroot", "sdk", "workloads",
)
ROW_SCHEMA = "buster-native-retirement-performance-rows-v2"
ROW_VERSION = 2
PROVENANCE_SCHEMA = "buster-native-retirement-provenance-v1"
PROVENANCE_VERSION = 1
REPLAY_SCHEMA = "buster-native-retirement-performance-replay-v1"
REPLAY_VERSION = 1
LEASE_PROTOCOL = "server-authoritative-supervisor-lease-v1"
LEASE_AUTHORITY = "server-supervisor"
LEASE_ACCESS = "harness-validates-then-cloexec-before-candidate"
LEASE_CLEANUP = "server-cgroup-and-descendant-cleanup"
ROW_ELIGIBILITY_FIELDS = [
    "compiler_wall_time", "compiler_peak_rss", "generated_code_bytes",
    "generated_runtime", "runtime_oracle", "code_section",
]
SUPPORT_DECLARATION_PATH = "docs/native-retirement-support-v1.tsv"
SUPPORT_DECLARATION_SHA256 = "c61bbde58c471dc0d50853f8797e05ccd1737521d342dc7376669d90e192f5b8"
NEXT_SUPPORT_DECLARATION_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"
PROPOSED_SUPPORT_DECLARATION_SHA256 = "0d878bf0a3df9f0528803a5b08275d950f9edda57e373fd7618229dee264e427"
SUPPORT_DECLARATION_FIELDS = ["path", "role", "compile_obligation", "bytes", "sha256"]
INPUT_FIELDS = ["path", "role", "compile_obligation", "bytes", "buster_hash_64",
                "sha256", "fixture_recipe", "fixture_flags"]
ROW_FIELDS = ["row", "group", "fixture", "target", "target_abi", "cpu",
              "cpu_features", "allocator", "frontend_lowering", "PIC", "selected",
              "fixture_recipe", "compile_obligation", "link_obligation",
              "execution_obligation", "diagnostic_obligation", "argv_evidence"]
DEPENDENCY_FIELDS = ["kind", "path", "bytes", "sha256"]
ENVIRONMENT_FIELDS = ["name", "present", "value"]
VALIDATOR_REPORT_SCHEMA = "buster-native-retirement-census-validation-v1"
VALIDATOR_REPORT_VERSION = 1
PERFORMANCE_DECLARATION_SCHEMA = "buster-native-retirement-performance-population-v2"
PERFORMANCE_DECLARATION_VERSION = 2
SOURCE_SNAPSHOT_SCHEMA = "buster-native-retirement-source-snapshot-v1"
BUILD_RECEIPT_SCHEMA = "buster-native-retirement-build-receipt-v1"
SERVICE_RECEIPT_SCHEMA = "buster-native-retirement-service-receipt-v1"
PROFILE_SCHEMA = "buster-native-retirement-host-profile-v1"
QUALIFICATION_SCHEMA = "buster-native-retirement-host-qualification-v1"
AA_SCHEMA = "buster-native-retirement-aa-admission-v1"
LEASE_RECEIPT_SCHEMA = "buster-native-retirement-lease-receipt-v1"
STATISTICAL_SCOPES = ["round-1", "round-2", "pooled"]
STATISTICAL_DIMENSIONS = ["target", "cpu", "allocator", "frontend_lowering", "PIC",
                          "artifact_stage"]
RESULT_INPUT_MAX_RECORDS = RESULT_INPUT_HARD_CAPS["max_records"]
RESULT_INPUT_MAX_TOTAL_BYTES = RESULT_INPUT_HARD_CAPS.get("max_total_bytes", 16 * 1024 * 1024 * 1024)
RESULT_INPUT_MAX_TOTAL_RECORDS = 39_518_208
WORKFLOW_SCHEMA = "buster-native-retirement-performance-workflow-v1"
WORKFLOW_VERSION = 1
WORKFLOW_PHASES = ("pre_sample_plan", "post_aa_binding", "sealed_result",
                   "independent_replay")
ADMISSION_SCHEMA = "buster-native-retirement-admission-v1"
ORACLE_SCHEMA = "buster-native-retirement-oracle-v1"
RESULT_INPUT_PLAN_SCHEMA = "buster-native-retirement-result-input-plan-v2"
SEALED_RESULT_SCHEMA = "buster-native-retirement-sealed-result-v1"
RESULT_BUNDLE_SCHEMA = "buster-native-retirement-result-bundle-v1"
REPLAY_BUNDLE_SCHEMA = "buster-native-retirement-independent-replay-bundle-v1"
PUBLICATION_SCHEMA = "buster-native-retirement-performance-publication-v1"
PHASE_SCHEMA = {
    "pre_sample_plan": "buster-native-retirement-pre-sample-plan-v1",
    "post_aa_binding": "buster-native-retirement-post-aa-binding-v1",
    "independent_replay": "buster-native-retirement-independent-replay-v1",
}


def _approved_support_counts():
    """Derive the reviewed inventory cardinalities from the support declaration.

    The declaration is the source of truth for schema-2 census dimensions.  Do
    not copy its current row totals into this validator: a reviewed declaration
    revision must update the digest, while these counts follow its real rows.
    """
    declaration_path = Path(__file__).resolve().parents[1] / SUPPORT_DECLARATION_PATH
    try:
        with declaration_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
    except (OSError, UnicodeDecodeError, csv.Error) as error:
        _fail(f"cannot derive #508 support counts from {declaration_path}: {error}")
    if not rows:
        _fail(f"#508 support declaration is empty: {declaration_path}")
    inputs = len(rows)
    subjects = sum(row.get("role") == "subject" for row in rows)
    groups = subjects * len(TARGETS) * len(FRONTENDS) * len(PIC)
    object_rows = groups * len(ALLOCATORS)
    return inputs, subjects, groups, object_rows


(SUPPORT_INPUT_COUNT, SUPPORT_SUBJECT_COUNT, SUPPORT_GROUP_COUNT,
 SUPPORT_OBJECT_ROW_COUNT) = _approved_support_counts()
SUPPORT_REQUIRED_ROW_COUNT = SUPPORT_OBJECT_ROW_COUNT
SUPPORT_MIN_STAGE_ROW_COUNT = SUPPORT_OBJECT_ROW_COUNT + len(STAGES) - 1


def _fail(message):
    raise ValueError(message)


def _object(value, name):
    if type(value) is not dict:
        _fail(f"{name} must be an object")
    return value


def _list(value, name):
    if type(value) is not list:
        _fail(f"{name} must be an array")
    return value


def _keys(value, required, name, optional=()):
    value = _object(value, name)
    required = set(required)
    optional = set(optional)
    missing = required - set(value)
    unknown = set(value) - required - optional
    if missing:
        _fail(f"{name} missing fields: {', '.join(sorted(missing))}")
    if unknown:
        _fail(f"{name} has unknown fields: {', '.join(sorted(unknown))}")
    return value


def _string(value, name):
    if type(value) is not str or not value:
        _fail(f"{name} must be a non-empty string")
    return value


def _single_line_string(value, name):
    value = _string(value, name)
    if any(character in value for character in ("\x00", "\r", "\n")):
        _fail(f"{name} must be a single-line string")
    return value


def _token(value, name):
    value = _string(value, name)
    if value in MUTABLE_TOKENS or not TOKEN_RE.fullmatch(value):
        _fail(f"{name} is mutable or malformed: {value!r}")
    return value


def _adapter_member(value, name):
    """Validate one #619 member identity before it reaches ``%127s``."""
    if type(value) is not str or not ADAPTER_MEMBER_RE.fullmatch(value):
        _fail(f"{name} is not a bounded C adapter member token: {value!r}")
    return value


def _commit(value, name):
    if type(value) is not str or not COMMIT_RE.fullmatch(value):
        _fail(f"{name} must be a full lowercase 40-hex commit ID")
    return value


def _sha(value, name):
    if type(value) is not str or not SHA256_RE.fullmatch(value):
        _fail(f"{name} must be a full lowercase SHA-256 digest")
    return value


def _positive_int(value, name):
    if type(value) is not int or value <= 0:
        _fail(f"{name} must be a positive integer")
    return value


def _nonnegative_int(value, name):
    if type(value) is not int or value < 0:
        _fail(f"{name} must be a non-negative integer")
    return value


def _boolean(value, name):
    if type(value) is not bool:
        _fail(f"{name} must be a boolean")
    return value


def _json_object(pairs):
    """Reject duplicate object keys instead of accepting last-key-wins JSON."""
    value = {}
    for key, item in pairs:
        if key in value:
            _fail(f"duplicate JSON object key: {key!r}")
        value[key] = item
    return value


def _exact_list(value, expected, name):
    value = _list(value, name)
    if value != expected:
        _fail(f"{name} must be exactly {expected!r}")
    return value


def _unique_sorted_tokens(value, name, *, known=None):
    value = _list(value, name)
    if not value or any(type(item) is not str for item in value):
        _fail(f"{name} must be a non-empty string array")
    if len(set(value)) != len(value) or value != sorted(value):
        _fail(f"{name} must be unique and sorted")
    for index, item in enumerate(value):
        _token(item, f"{name}[{index}]")
        if known is not None and item not in known:
            _fail(f"{name}[{index}] is not an admitted value: {item!r}")
    return value


def _relative_path(value, name):
    value = _string(value, name)
    path = PurePosixPath(value)
    if (path.is_absolute() or ".." in path.parts or "\\" in value
            or path.as_posix() != value):
        _fail(f"{name} must be a normalized relative path")
    return value


def _artifact(value, name):
    value = _keys(value, ("path", "bytes", "sha256"), name)
    _relative_path(value["path"], f"{name}.path")
    _positive_int(value["bytes"], f"{name}.bytes")
    _sha(value["sha256"], f"{name}.sha256")
    return value


def _subject(value, name, role, dispatch, stages):
    value = _keys(value, ("role", "source_commit", "source_tree", "source_snapshot",
                          "binary", "build_receipt", "dispatch", "stage"), name)
    if value["role"] != role:
        _fail(f"{name}.role must be {role!r}")
    _commit(value["source_commit"], f"{name}.source_commit")
    _commit(value["source_tree"], f"{name}.source_tree")
    _artifact(value["source_snapshot"], f"{name}.source_snapshot")
    _artifact(value["binary"], f"{name}.binary")
    _artifact(value["build_receipt"], f"{name}.build_receipt")
    if value["dispatch"] != dispatch:
        _fail(f"{name}.dispatch must be {dispatch!r}")
    if value["stage"] not in stages:
        _fail(f"{name}.stage is not admitted: {value['stage']!r}")
    return value


def _support_file(support, role):
    """Return one role from the ordered support declaration."""
    return support["files"][SUPPORT_FILE_ROLES.index(role)]


def _properties(data, name):
    """Parse the key/value manifest emitted by native_retirement_census."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        _fail(f"{name} is not valid UTF-8 properties: {error}")
    result = {}
    for line_number, line in enumerate(text.splitlines(), 1):
        key, separator, value = line.partition("=")
        if not separator or not key or key in result:
            _fail(f"{name} has an invalid property at line {line_number}")
        _single_line_string(key, f"{name}.key[{line_number}]")
        _single_line_string(value, f"{name}.{key}")
        result[key] = value
    if not result:
        _fail(f"{name} is empty")
    return result


def _tsv(data, fields, name):
    """Parse a strict tabular artifact with a fixed header."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        _fail(f"{name} is not valid UTF-8 TSV: {error}")
    reader = csv.DictReader(io.StringIO(text), delimiter="\t", lineterminator="\n")
    if reader.fieldnames != fields:
        _fail(f"{name} header does not match the #508 schema")
    rows = []
    for index, row in enumerate(reader):
        if None in row or any(value is None for value in row.values()):
            _fail(f"{name}[{index}] is malformed")
        if any("\r" in value or "\n" in value for value in row.values()):
            _fail(f"{name}[{index}] contains a multi-line field")
        rows.append(row)
    if not rows:
        _fail(f"{name} is empty")
    return rows


def _decimal_text(value, name):
    if type(value) is not str or not value.isdigit():
        _fail(f"{name} must be a decimal integer")
    return int(value)


def _artifact_digest_map(support):
    return {role: _support_file(support, role)["sha256"]
            for role in PERFORMANCE_SOURCE_ROLES}


def _canonical_files_digest(files):
    rows = [
        {"name": item["name"], "path": item["path"], "bytes": item["bytes"],
         "sha256": item["sha256"]}
        for item in sorted(files, key=lambda item: item["name"])
    ]
    encoded = json.dumps(rows, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _support_root_digest(files, validator, closure):
    """Digest support files together with validator and closure descriptors."""
    descriptors = list(files)
    descriptors.append({"name": "validator.source", **validator["source"]})
    descriptors.extend({"name": f"closure.{kind}", **closure[kind]["artifact"]}
                       for kind in REQUIRED_WORK_CLOSURE_KINDS)
    return _canonical_files_digest(descriptors)


def _canonical_json_digest(value):
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _process_instance_digest(job_id, attempt, boot_id, pid, process_start_token):
    """Bind one supervisor-observed OS process instance, not merely a PID.

    PIDs may be reused after exit.  The admitted service therefore supplies a
    boot-scoped process-start token (for example the Linux proc start time plus
    its transient-unit invocation identity).  The independently authenticated
    receipt binds that token; this digest makes accidental field substitution
    and persistent-worker reuse machine-checkable without treating a PID as a
    globally unique identifier.
    """
    return _canonical_json_digest({
        "job_id": job_id,
        "attempt": attempt,
        "boot_id": boot_id,
        "pid": pid,
        "process_start_token": process_start_token,
    })


def _family_invocation_digest(family):
    """Digest one #619 C call per scope-free logical family member."""
    calls = [{"member": member, "scopes": list(STATISTICAL_SCOPES)}
             for member in family["members"]]
    return _canonical_json_digest(calls)


def _support(value):
    value = _keys(value, ("schema", "version", "root_sha256", "files", "validator",
                          "closure"),
                  "support")
    if value["schema"] != "native-retirement-support-v1":
        _fail("support.schema is not the approved support declaration")
    _token(value["version"], "support.version")
    _sha(value["root_sha256"], "support.root_sha256")
    files = _list(value["files"], "support.files")
    if len(files) != len(SUPPORT_FILE_ROLES):
        _fail("support.files does not contain the complete declaration set")
    names = []
    for index, item in enumerate(files):
        item = _keys(item, ("name", "path", "bytes", "sha256"),
                     f"support.files[{index}]")
        name = _string(item["name"], f"support.files[{index}].name")
        if name not in SUPPORT_FILE_ROLES:
            _fail(f"support.files has unknown role: {name!r}")
        names.append(name)
        _artifact({"path": item["path"], "bytes": item["bytes"],
                   "sha256": item["sha256"]}, f"support.files[{index}]")
    if names != SUPPORT_FILE_ROLES:
        _fail(f"support.files must be ordered as {SUPPORT_FILE_ROLES!r}")
    validator = _keys(value["validator"], ("name", "version", "source_commit",
                                            "source_tree", "source"),
                      "support.validator")
    if validator["name"] != "native_retirement_contract.py":
        _fail("support.validator.name must identify the independent #508 validator")
    if type(validator["version"]) is not int or validator["version"] != 1:
        _fail("support.validator.version must be exactly 1")
    _commit(validator["source_commit"], "support.validator.source_commit")
    _commit(validator["source_tree"], "support.validator.source_tree")
    _artifact(validator["source"], "support.validator.source")
    closure = _keys(value["closure"], REQUIRED_WORK_CLOSURE_KINDS,
                    "support.closure")
    for kind in REQUIRED_WORK_CLOSURE_KINDS:
        entry = _keys(closure[kind], ("name", "artifact"),
                      f"support.closure.{kind}")
        _token(entry["name"], f"support.closure.{kind}.name")
        _artifact(entry["artifact"], f"support.closure.{kind}.artifact")
    if value["root_sha256"] != _support_root_digest(files, validator, closure):
        _fail("support.root_sha256 does not match the complete support digest")
    return value


def _row_identity(value, name):
    value = _keys(value, ROW_IDENTITY_FIELDS, name)
    _relative_path(value["fixture"], f"{name}.fixture")
    if value["target"] not in TARGETS:
        _fail(f"{name}.target is not an admitted target: {value['target']!r}")
    _token(value["target_abi"], f"{name}.target_abi")
    _token(value["cpu"], f"{name}.cpu")
    _single_line_string(value["cpu_features"], f"{name}.cpu_features")
    if value["allocator"] not in ALLOCATORS:
        _fail(f"{name}.allocator is not an admitted allocator")
    if value["frontend_lowering"] not in FRONTENDS:
        _fail(f"{name}.frontend_lowering is not an admitted frontend")
    if value["PIC"] not in PIC:
        _fail(f"{name}.PIC is not an admitted PIC mode")
    if value["artifact_stage"] not in STAGES:
        _fail(f"{name}.artifact_stage is not an admitted artifact stage")
    for field in ("fixture_recipe", "compile_obligation", "link_obligation",
                  "execution_obligation", "diagnostic_obligation"):
        _token(value[field], f"{name}.{field}")
    _relative_path(value["argv_evidence"], f"{name}.argv_evidence")
    return value


def _native_runtime_required(row, native_target):
    """Derive native-runtime applicability from frozen row/host identities."""
    identity = row["identity"]
    return identity["execution_obligation"] == "semantic-gate-509" \
        and identity["artifact_stage"] in {"link", "self-host-stage1"} \
        and identity["target"] == native_target


def _derive_axes(rows):
    def ordered(values, expected):
        present = set(values)
        return [item for item in expected if item in present]

    axes = {
        "allocators": ordered((row["identity"]["allocator"] for row in rows), ALLOCATORS),
        "frontends": ordered((row["identity"]["frontend_lowering"] for row in rows), FRONTENDS),
        "targets": sorted({row["identity"]["target"] for row in rows}),
        "cpus": sorted({row["identity"]["cpu"] for row in rows}),
        "PIC": ordered((row["identity"]["PIC"] for row in rows), PIC),
        "stages": ordered((row["identity"]["artifact_stage"] for row in rows), STAGES),
    }
    return axes


def _derive_statistical_family(rows):
    """Build #619's dense, scope-free logical member map.

    One ``TpRetirementSeries`` call consumes one aggregate/slice/cell member
    and returns round-1, round-2, and pooled bounds together.  Consequently
    scope names are metadata on the family, never prefixes that create three
    independent calls for the same member.
    """
    members = set()
    cell_counts = {}
    cell_digests = {}
    for metric in STATISTICAL_METRICS:
        eligible = [row for row in rows if row["metrics"].get(metric, False)]
        if not eligible:
            _fail(f"statistical family has no eligible rows for {metric}")
        prefix = metric
        members.add(f"{prefix}/aggregate")
        for dimension in STATISTICAL_DIMENSIONS:
            values = {row["identity"][dimension] for row in eligible}
            for value in values:
                members.add(f"{prefix}/slice/{dimension}={value}")
        for row in eligible:
            members.add(f"{prefix}/cell/row={row['row']}")
        cell_digest = hashlib.sha256()
        for row in eligible:
            identity = [row["identity"][field] for field in ROW_IDENTITY_FIELDS]
            encoded = json.dumps({"row": row["row"], "identity": identity},
                                 sort_keys=True, separators=(",", ":"),
                                 ensure_ascii=False).encode("utf-8")
            cell_digest.update(encoded)
            cell_digest.update(b"\n")
        cell_counts[prefix] = len(eligible)
        cell_digests[prefix] = cell_digest.hexdigest()
    ordered = sorted(members)
    family = {
        "scopes": list(STATISTICAL_SCOPES),
        "dimensions": list(STATISTICAL_DIMENSIONS),
        "metrics": list(STATISTICAL_METRICS),
        "members": ordered,
        "cell_counts": cell_counts,
        "cell_identity_sha256": cell_digests,
    }
    for index, member in enumerate(ordered):
        _adapter_member(member, f"statistical family member[{index}]")
    family["sha256"] = _canonical_json_digest(family)
    return family


def _family_member_counts(family):
    """Derive #619's two per-scope partitions from the canonical family map.

    Bootstrap members are every aggregate/slice identity in a scope across
    the three variable metrics.  Exact-cell members are the sum of the three
    metric cell counts in that same scope.  The counts are derived from the
    member identities and eligibility rows, never from a producer total.
    """
    bootstrap_members = [member for member in family["members"]
                         if member.endswith("/aggregate") or "/slice/" in member]
    bootstrap = len(bootstrap_members)
    cells = sum(family["cell_counts"][metric] for metric in STATISTICAL_METRICS)
    if not 0 < bootstrap <= 80:
        _fail("statistical family bootstrap member count exceeds #619 cap")
    if not 0 < cells <= 300000:
        _fail("statistical family exact-cell count exceeds #619 cap")
    counts = {scope: {"bootstrap": bootstrap, "cells": cells}
              for scope in STATISTICAL_SCOPES}
    return {"bootstrap_members_per_scope": bootstrap,
            "cell_members_per_scope": cells,
            "by_scope": counts}


def _performance_rows_with_sources(value, name="performance_rows"):
    raw = value
    try:
        value = json.loads(value.decode("utf-8"), object_pairs_hook=_json_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"{name} is not valid UTF-8 JSON: {error}")
    value = _keys(value, ("schema", "version", "row_identity_fields", "sources", "rows"),
                  name)
    if value["schema"] != ROW_SCHEMA:
        _fail(f"{name}.schema is not the approved performance-row schema")
    if type(value["version"]) is not int or value["version"] != ROW_VERSION:
        _fail(f"{name}.version must be exactly {ROW_VERSION}")
    canonical = (json.dumps(value, sort_keys=True, separators=(",", ":"),
                            ensure_ascii=False) + "\n").encode("utf-8")
    if raw != canonical:
        _fail(f"{name} is not canonical JSON serialization")
    _exact_list(value["row_identity_fields"], ROW_IDENTITY_FIELDS,
                f"{name}.row_identity_fields")
    sources = _keys(value["sources"], ROW_SOURCE_ROLES,
                    f"{name}.sources")
    for role in ROW_SOURCE_ROLES:
        _sha(sources[role], f"{name}.sources.{role}")
    rows = _list(value["rows"], f"{name}.rows")
    if not rows:
        _fail(f"{name}.rows must contain the complete required population")
    parsed = []
    identities = set()
    for index, item in enumerate(rows):
        item = _keys(item, ("row", "identity", "eligibility"),
                     f"{name}.rows[{index}]")
        if type(item["row"]) is not int or item["row"] != index:
            _fail(f"{name}.rows must have contiguous zero-based row IDs")
        identity = _row_identity(item["identity"], f"{name}.rows[{index}].identity")
        identity_key = tuple(identity[field] for field in ROW_IDENTITY_FIELDS)
        if identity_key in identities:
            _fail(f"{name}.rows[{index}] duplicates a row identity")
        identities.add(identity_key)
        eligibility = _keys(item["eligibility"], ROW_ELIGIBILITY_FIELDS,
                            f"{name}.rows[{index}].eligibility")
        metrics = {}
        for metric in METRICS:
            metrics[metric] = _boolean(eligibility[metric],
                                       f"{name}.rows[{index}].eligibility.{metric}")
        compile_eligible = metrics["compiler_wall_time"]
        if metrics["compiler_peak_rss"] is not compile_eligible:
            _fail(f"{name}.rows[{index}] must keep wall-time and peak-RSS eligibility paired")
        if not compile_eligible:
            if metrics["generated_code_bytes"] or metrics["generated_runtime"] \
                    or eligibility["code_section"] != "not-applicable" \
                    or eligibility["runtime_oracle"] != "not-applicable":
                _fail(f"{name}.rows[{index}] gives metrics to an authenticated untimed row")
        elif metrics["generated_code_bytes"]:
            if eligibility["code_section"] != "deterministic-code-section":
                _fail(f"{name}.rows[{index}] has an invalid code-section obligation")
        elif eligibility["code_section"] not in {
                "deterministic-zero-baseline-code-section", "not-applicable"}:
            _fail(f"{name}.rows[{index}] has an invalid zero/inapplicable code marker")
        if metrics["generated_runtime"]:
            if not compile_eligible \
                    or eligibility["runtime_oracle"] != "independent-native-executable-oracle":
                _fail(f"{name}.rows[{index}] has an invalid runtime oracle")
        elif eligibility["runtime_oracle"] != "not-applicable":
            _fail(f"{name}.rows[{index}] has an inapplicable runtime oracle")
        # Retain the source eligibility markers for the admission/oracle join.
        # The canonical row artifact is still the only source of row order and
        # identities; this copy lets the evidence phase prove that those
        # markers were derived from independent execution records rather than
        # supplied by the binding caller.
        parsed.append({"row": item["row"], "identity": identity,
                       "metrics": metrics, "eligibility": eligibility})
    return parsed, _derive_axes(parsed), _derive_statistical_family(parsed), sources


def _performance_rows(value, name="performance_rows"):
    """Parse canonical rows while retaining the original three-value API."""
    parsed, axes, family, _sources = _performance_rows_with_sources(value, name)
    return parsed, axes, family


def _population(value, support, row_data=None):
    value = _keys(value, ("required_row_count", "required_rows_sha256", "axes",
                          "row_identity_fields", "statistical_family",
                          "source_digests"), "population")
    _positive_int(value["required_row_count"], "population.required_row_count")
    _sha(value["required_rows_sha256"], "population.required_rows_sha256")
    rows = support["files"][SUPPORT_FILE_ROLES.index("performance_rows")]
    if value["required_rows_sha256"] != rows["sha256"]:
        _fail("population rows digest differs from support performance_rows")
    source_digests = _keys(value["source_digests"], PERFORMANCE_SOURCE_ROLES,
                           "population.source_digests")
    for role in PERFORMANCE_SOURCE_ROLES:
        _sha(source_digests[role], f"population.source_digests.{role}")
    if source_digests != _artifact_digest_map(support):
        _fail("population.source_digests do not identify the complete #508 output")
    axes = _keys(value["axes"], ("allocators", "frontends", "targets", "cpus",
                                  "PIC", "stages"), "population.axes")
    _exact_list(axes["allocators"], ALLOCATORS, "population.axes.allocators")
    _exact_list(axes["frontends"], FRONTENDS, "population.axes.frontends")
    _exact_list(axes["PIC"], PIC, "population.axes.PIC")
    _exact_list(axes["stages"], STAGES, "population.axes.stages")
    _unique_sorted_tokens(axes["targets"], "population.axes.targets", known=TARGETS)
    _unique_sorted_tokens(axes["cpus"], "population.axes.cpus")
    _exact_list(value["row_identity_fields"], ROW_IDENTITY_FIELDS,
                "population.row_identity_fields")
    family = _keys(value["statistical_family"], ("scopes", "dimensions", "metrics",
                                                  "members", "cell_counts",
                                                  "cell_identity_sha256", "sha256"),
                   "population.statistical_family")
    _exact_list(family["scopes"], STATISTICAL_SCOPES,
                "population.statistical_family.scopes")
    _exact_list(family["dimensions"], STATISTICAL_DIMENSIONS,
                "population.statistical_family.dimensions")
    _exact_list(family["metrics"], STATISTICAL_METRICS,
                "population.statistical_family.metrics")
    members = _list(family["members"], "population.statistical_family.members")
    if any(type(member) is not str or not member for member in members):
        _fail("population.statistical_family.members must be non-empty strings")
    for index, member in enumerate(members):
        _adapter_member(member, f"population.statistical_family.members[{index}]")
    if members != sorted(set(members)):
        _fail("population.statistical_family.members must be unique and sorted")
    counts = _object(family["cell_counts"], "population.statistical_family.cell_counts")
    digests = _object(family["cell_identity_sha256"],
                      "population.statistical_family.cell_identity_sha256")
    expected_keys = list(STATISTICAL_METRICS)
    if set(counts) != set(expected_keys) or set(digests) != set(expected_keys):
        _fail("population.statistical_family must bind every round/metric family")
    for key in expected_keys:
        _positive_int(counts[key], f"population.statistical_family.cell_counts.{key}")
        _sha(digests[key], f"population.statistical_family.cell_identity_sha256.{key}")
    _sha(family["sha256"], "population.statistical_family.sha256")
    family_without_digest = {key: family[key] for key in family if key != "sha256"}
    if family["sha256"] != _canonical_json_digest(family_without_digest):
        _fail("population.statistical_family.sha256 does not match family members")
    _family_member_counts(family)
    if row_data is not None:
        parsed, expected_axes, expected_family = row_data
        if value["required_row_count"] != len(parsed):
            _fail("population.required_row_count is not the recomputed row count")
        if axes != expected_axes:
            _fail("population.axes do not match the canonical performance rows")
        if family != expected_family:
            _fail("population.statistical_family does not match canonical rows")
    return value


def _canonical_work_digest(items):
    rows = [
        {"kind": item["kind"], "name": item["name"], "path": item["artifact"]["path"],
         "bytes": item["artifact"]["bytes"], "sha256": item["artifact"]["sha256"]}
        for item in sorted(items, key=lambda item: (item["kind"], item["name"]))
    ]
    encoded = json.dumps(rows, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _requested_work(value, support):
    value = _keys(value, ("schema", "version", "root_sha256", "items", "closure"),
                  "requested_work")
    if value["schema"] != "native-retirement-requested-work-v1":
        _fail("requested_work.schema is not the approved closure schema")
    if type(value["version"]) is not int or value["version"] != 1:
        _fail("requested_work.version must be exactly 1")
    _sha(value["root_sha256"], "requested_work.root_sha256")
    items = _list(value["items"], "requested_work.items")
    if not items:
        _fail("requested_work.items must contain the frozen closure")
    identities = []
    for index, item in enumerate(items):
        item = _keys(item, ("kind", "name", "artifact"),
                     f"requested_work.items[{index}]")
        if item["kind"] not in REQUESTED_WORK_KINDS:
            _fail(f"requested_work.items[{index}].kind is not admitted")
        _token(item["name"], f"requested_work.items[{index}].name")
        _artifact(item["artifact"], f"requested_work.items[{index}].artifact")
        identities.append((item["kind"], item["name"]))
    if len(set(identities)) != len(identities) or identities != sorted(identities):
        _fail("requested_work.items must be unique and sorted by kind/name")
    if value["root_sha256"] != _canonical_work_digest(items):
        _fail("requested_work.root_sha256 does not match the canonical closure digest")
    by_kind = {}
    for item in items:
        by_kind.setdefault(item["kind"], []).append(item)
    missing = [kind for kind in REQUIRED_WORK_CLOSURE_KINDS if kind not in by_kind]
    if missing:
        _fail("requested_work.items omit required #508 closure kinds: "
              + ", ".join(missing))
    closure = _keys(value["closure"], ("manifest_sha256",) + REQUIRED_WORK_CLOSURE_KINDS,
                     "requested_work.closure")
    manifest = support["files"][SUPPORT_FILE_ROLES.index("manifest")]
    _sha(closure["manifest_sha256"], "requested_work.closure.manifest_sha256")
    if closure["manifest_sha256"] != manifest["sha256"]:
        _fail("requested_work.closure.manifest_sha256 differs from #508 manifest")
    for kind in REQUIRED_WORK_CLOSURE_KINDS:
        item = _keys(closure[kind], ("name", "bytes", "sha256"),
                     f"requested_work.closure.{kind}")
        _token(item["name"], f"requested_work.closure.{kind}.name")
        _positive_int(item["bytes"], f"requested_work.closure.{kind}.bytes")
        _sha(item["sha256"], f"requested_work.closure.{kind}.sha256")
        expected = support["closure"][kind]
        if (item["name"], item["bytes"], item["sha256"]) != \
                (expected["name"], expected["artifact"]["bytes"],
                 expected["artifact"]["sha256"]):
            _fail(f"requested_work.closure.{kind} differs from #508 closure output")
        matches = [candidate for candidate in by_kind[kind]
                   if candidate["name"] == item["name"]
                   and candidate["artifact"]["bytes"] == item["bytes"]
                   and candidate["artifact"]["sha256"] == item["sha256"]]
        if len(matches) != 1:
            _fail(f"requested_work.closure.{kind} does not identify exactly one item")
    return value


def _producer(value):
    value = _keys(value, ("toolchain", "build"), "producer")
    toolchain = _keys(value["toolchain"], ("name", "version", "compiler_binary",
                                            "resource_directory"), "producer.toolchain")
    if toolchain["name"] != "clang":
        _fail("producer.toolchain.name must be clang")
    _token(toolchain["version"], "producer.toolchain.version")
    _artifact(toolchain["compiler_binary"], "producer.toolchain.compiler_binary")
    _artifact(toolchain["resource_directory"],
              "producer.toolchain.resource_directory")
    build = _keys(value["build"], ("configuration", "flags", "mode", "unity",
                                    "warnings_as_errors", "sanitizers",
                                    "profiling", "allocation_hooks"),
                  "producer.build")
    _artifact(build["configuration"], "producer.build.configuration")
    _artifact(build["flags"], "producer.build.flags")
    if build["mode"] != "Release":
        _fail("producer.build.mode must be Release")
    _boolean(build["unity"], "producer.build.unity")
    _boolean(build["warnings_as_errors"], "producer.build.warnings_as_errors")
    for name in ("sanitizers", "profiling", "allocation_hooks"):
        if build[name]:
            _fail(f"producer.build.{name} must be false for acceptance")
    if not build["unity"] or not build["warnings_as_errors"]:
        _fail("producer build must be warnings-as-errors Release unity")
    return value


def _measurement(value):
    value = _keys(value, ("harness_source_commit", "harness_source_tree",
                          "harness_binary", "statistics_implementation"),
                  "measurement")
    _commit(value["harness_source_commit"], "measurement.harness_source_commit")
    _commit(value["harness_source_tree"], "measurement.harness_source_tree")
    _artifact(value["harness_binary"], "measurement.harness_binary")
    _artifact(value["statistics_implementation"],
              "measurement.statistics_implementation")
    return value


def _execution(value):
    value = _keys(value, ("service", "host", "profile", "job_ownership", "lease",
                          "native_execution"), "execution")
    service = _keys(value["service"], ("id", "version", "recipe"),
                     "execution.service")
    _token(service["id"], "execution.service.id")
    _token(service["version"], "execution.service.version")
    _artifact(service["recipe"], "execution.service.recipe")
    host = _keys(value["host"], ("machine_id", "qualification_receipt",
                                  "aa_admission_receipt"), "execution.host")
    _token(host["machine_id"], "execution.host.machine_id")
    _artifact(host["qualification_receipt"],
              "execution.host.qualification_receipt")
    _artifact(host["aa_admission_receipt"],
              "execution.host.aa_admission_receipt")
    profile = _keys(value["profile"], ("id", "version", "machine_id", "descriptor", "digest"),
                    "execution.profile")
    _token(profile["id"], "execution.profile.id")
    _token(profile["version"], "execution.profile.version")
    _token(profile["machine_id"], "execution.profile.machine_id")
    _sha(profile["digest"], "execution.profile.digest")
    _artifact(profile["descriptor"], "execution.profile.descriptor")
    if profile["digest"] != profile["descriptor"]["sha256"]:
        _fail("execution.profile.digest differs from its descriptor")
    if profile["machine_id"] != host["machine_id"]:
        _fail("execution.profile.machine_id differs from execution.host.machine_id")
    if value["job_ownership"] != LEASE_PROTOCOL:
        _fail("execution.job_ownership is not the admitted supervisor lease protocol")
    lease = _keys(value["lease"], ("authority", "access", "cleanup", "receipt"),
                  "execution.lease")
    if lease["authority"] != LEASE_AUTHORITY:
        _fail("execution.lease.authority must be server-supervisor")
    if lease["access"] != LEASE_ACCESS:
        _fail("execution.lease.access must make the lease inaccessible to candidates")
    if lease["cleanup"] != LEASE_CLEANUP:
        _fail("execution.lease.cleanup must remove the candidate cgroup and descendants")
    _artifact(lease["receipt"], "execution.lease.receipt")
    if value["native_execution"] != "native-only":
        _fail("execution.native_execution must be native-only")
    return value


def _provenance(value):
    value = _keys(value, ("schema", "version", "relation_receipt", "replay_receipt",
                          "replay_bundle", "census_receipt", "strict_receipt"),
                  "provenance")
    if value["schema"] != PROVENANCE_SCHEMA:
        _fail("provenance.schema is not the approved receipt schema")
    if type(value["version"]) is not int or value["version"] != PROVENANCE_VERSION:
        _fail(f"provenance.version must be exactly {PROVENANCE_VERSION}")
    _artifact(value["relation_receipt"], "provenance.relation_receipt")
    _artifact(value["replay_receipt"], "provenance.replay_receipt")
    _artifact(value["replay_bundle"], "provenance.replay_bundle")
    _artifact(value["census_receipt"], "provenance.census_receipt")
    _artifact(value["strict_receipt"], "provenance.strict_receipt")
    return value


def _read_json_evidence(root, artifact, name):
    root = Path(root).resolve()
    relative = PurePosixPath(artifact["path"])
    target = root.joinpath(*relative.parts)
    try:
        target.resolve().relative_to(root)
    except ValueError:
        _fail(f"{name}.path escapes evidence root")
    try:
        return json.loads(target.read_text(encoding="utf-8"),
                          object_pairs_hook=_json_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"{name} is not a readable JSON receipt: {error}")


def _read_trusted_json_evidence(root, artifact, name, trusted_sha256, byte_cap):
    """Hash and parse one bounded trusted receipt from the same open stream."""
    root = Path(root).resolve()
    relative = PurePosixPath(artifact["path"])
    target = root.joinpath(*relative.parts)
    try:
        target.resolve().relative_to(root)
    except ValueError:
        _fail(f"{name}.path escapes evidence root")
    cursor = root
    for part in relative.parts:
        cursor /= part
        if cursor.is_symlink():
            _fail(f"{name}.path contains a symbolic link")
    try:
        with target.open("rb") as stream:
            metadata = os.fstat(stream.fileno())
            if not stat.S_ISREG(metadata.st_mode):
                _fail(f"{name} is not a regular file")
            data = stream.read(byte_cap + 1)
    except OSError as error:
        _fail(f"{name} is not readable: {error}")
    if len(data) > byte_cap:
        _fail(f"{name} exceeds the bounded metadata size")
    if len(data) != artifact["bytes"]:
        _fail(f"{name} byte count does not match evidence")
    digest = hashlib.sha256(data).hexdigest()
    if digest != artifact["sha256"] or digest != trusted_sha256:
        _fail(f"{name} bytes do not match the independently trusted digest")
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=_json_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"{name} is not a readable JSON receipt: {error}")


def _check_provenance_evidence(root, binding, provenance):
    relation = _read_json_evidence(root, provenance["relation_receipt"],
                                   "provenance.relation_receipt")
    relation = _keys(relation, ("schema", "version", "baseline", "candidate",
                                "toolchain", "harness"), "relation_receipt")
    if relation["schema"] != PROVENANCE_SCHEMA:
        _fail("relation_receipt.schema is not the approved provenance schema")
    if type(relation["version"]) is not int or relation["version"] != PROVENANCE_VERSION:
        _fail("relation_receipt.version is not the approved provenance version")
    for role in ("baseline", "candidate"):
        receipt = _keys(relation[role], ("source_commit", "source_tree",
                                         "source_snapshot_sha256", "binary_sha256",
                                         "build_receipt_sha256"),
                        f"relation_receipt.{role}")
        subject = binding["subjects"][role]
        _commit(receipt["source_commit"], f"relation_receipt.{role}.source_commit")
        _commit(receipt["source_tree"], f"relation_receipt.{role}.source_tree")
        _sha(receipt["source_snapshot_sha256"],
             f"relation_receipt.{role}.source_snapshot_sha256")
        _sha(receipt["binary_sha256"], f"relation_receipt.{role}.binary_sha256")
        _sha(receipt["build_receipt_sha256"],
             f"relation_receipt.{role}.build_receipt_sha256")
        expected = {
            "source_commit": subject["source_commit"],
            "source_tree": subject["source_tree"],
            "source_snapshot_sha256": subject["source_snapshot"]["sha256"],
            "binary_sha256": subject["binary"]["sha256"],
            "build_receipt_sha256": subject["build_receipt"]["sha256"],
        }
        if receipt != expected:
            _fail(f"relation_receipt.{role} does not match bound subject")

    toolchain = _keys(relation["toolchain"], ("compiler_binary_sha256",
                                               "resource_directory_sha256",
                                               "configuration_sha256", "flags_sha256"),
                      "relation_receipt.toolchain")
    expected_toolchain = {
        "compiler_binary_sha256": binding["producer"]["toolchain"]
        ["compiler_binary"]["sha256"],
        "resource_directory_sha256": binding["producer"]["toolchain"]
        ["resource_directory"]["sha256"],
        "configuration_sha256": binding["producer"]["build"]
        ["configuration"]["sha256"],
        "flags_sha256": binding["producer"]["build"]["flags"]["sha256"],
    }
    for name, digest in toolchain.items():
        _sha(digest, f"relation_receipt.toolchain.{name}")
    if toolchain != expected_toolchain:
        _fail("relation_receipt.toolchain does not match bound producer")

    harness = _keys(relation["harness"], ("source_commit", "source_tree",
                                          "binary_sha256", "statistics_sha256"),
                    "relation_receipt.harness")
    _commit(harness["source_commit"], "relation_receipt.harness.source_commit")
    _commit(harness["source_tree"], "relation_receipt.harness.source_tree")
    _sha(harness["binary_sha256"], "relation_receipt.harness.binary_sha256")
    _sha(harness["statistics_sha256"], "relation_receipt.harness.statistics_sha256")
    expected_harness = {
        "source_commit": binding["measurement"]["harness_source_commit"],
        "source_tree": binding["measurement"]["harness_source_tree"],
        "binary_sha256": binding["measurement"]["harness_binary"]["sha256"],
        "statistics_sha256": binding["measurement"]["statistics_implementation"]["sha256"],
    }
    if harness != expected_harness:
        _fail("relation_receipt.harness does not match bound measurement implementation")

    census = _read_json_evidence(root, provenance["census_receipt"],
                                 "provenance.census_receipt")
    census = _keys(census, ("schema", "success", "release", "github_run_id",
                            "rows_validated", "inputs_validated", "joined_files",
                            "joined_tree_sha256", "candidate_binary_sha256",
                            "archived_direct_oracle_sha256", "rebuilt_direct_oracle_sha256",
                            "rebuilt_matches_archived", "validator_report_sha256"),
                   "census_replay_receipt")
    if census["schema"] != "buster-native-retirement-census-replay-v1" \
            or census["success"] is not True:
        _fail("#510 census replay receipt is not successful")
    for name in ("joined_tree_sha256", "candidate_binary_sha256",
                 "archived_direct_oracle_sha256", "rebuilt_direct_oracle_sha256",
                 "validator_report_sha256"):
        _sha(census[name], f"census_replay_receipt.{name}")
    if census["candidate_binary_sha256"] != binding["subjects"]["candidate"]["binary"]["sha256"]:
        _fail("#510 census receipt candidate binary differs from the binding")
    if census["validator_report_sha256"] != \
            binding["support"]["files"][SUPPORT_FILE_ROLES.index("validator_report")]["sha256"]:
        _fail("#510 census receipt does not bind the independent validator report")
    if type(census["rows_validated"]) is not int or census["rows_validated"] != SUPPORT_OBJECT_ROW_COUNT:
        _fail("#510 census receipt row count is not the complete #508 population")
    if type(census["inputs_validated"]) is not int or census["inputs_validated"] != SUPPORT_INPUT_COUNT:
        _fail("#510 census receipt input count is not the complete #508 population")
    _boolean(census["rebuilt_matches_archived"], "census_replay_receipt.rebuilt_matches_archived")
    if not census["rebuilt_matches_archived"]:
        _fail("#510 census replay did not reproduce the archived oracle")
    if type(census["release"]) is not str or not census["release"] \
            or type(census["github_run_id"]) is not str or not census["github_run_id"]:
        _fail("#510 census replay receipt lacks publication identity")
    if type(census["joined_files"]) is not int or census["joined_files"] <= 0:
        _fail("#510 census replay receipt lacks joined-file evidence")

    strict = _read_json_evidence(root, provenance["strict_receipt"],
                                 "provenance.strict_receipt")
    strict = _keys(strict, ("schema", "success", "release", "github_run_id",
                            "archive_sha256", "archive_size", "candidate_binary_sha256",
                            "recorded_summary", "replayed_summary", "cases",
                            "configurations"), "strict_replay_receipt")
    if strict["schema"] != "buster-native-retirement-strict-replay-v1" \
            or strict["success"] is not True:
        _fail("#510 strict replay receipt is not successful")
    _sha(strict["archive_sha256"], "strict_replay_receipt.archive_sha256")
    _positive_int(strict["archive_size"], "strict_replay_receipt.archive_size")
    if strict["candidate_binary_sha256"] != binding["subjects"]["candidate"]["binary"]["sha256"]:
        _fail("#510 strict receipt candidate binary differs from the binding")
    if type(strict["cases"]) is not int or strict["cases"] <= 0 \
            or type(strict["configurations"]) is not int or strict["configurations"] <= 0:
        _fail("#510 strict replay receipt lacks configuration evidence")
    if strict["recorded_summary"] != strict["replayed_summary"]:
        _fail("#510 strict replay summary differs from the recorded result")
    if type(strict["release"]) is not str or not strict["release"] \
            or type(strict["github_run_id"]) is not str or not strict["github_run_id"]:
        _fail("#510 strict replay receipt lacks publication identity")
    _evidence_bytes(root, provenance["replay_bundle"], "provenance.replay_bundle")

    replay = _read_json_evidence(root, provenance["replay_receipt"],
                                 "provenance.replay_receipt")
    replay = _keys(replay, ("schema", "version", "publisher", "bundle_sha256",
                            "published_bundle_sha256", "downloaded_bundle_sha256",
                            "census_receipt_sha256", "strict_receipt_sha256",
                            "contract_source_commit", "contract_source_tree",
                            "candidate_source_commit", "candidate_source_tree",
                            "replayed", "result"), "replay_receipt")
    if replay["schema"] != REPLAY_SCHEMA or type(replay["version"]) is not int \
            or replay["version"] != REPLAY_VERSION:
        _fail("replay_receipt does not identify the versioned #510 replay protocol")
    if replay["publisher"] != "native-retirement-evidence-v1":
        _fail("replay_receipt.publisher must identify the #510 evidence producer")
    _sha(replay["bundle_sha256"], "replay_receipt.bundle_sha256")
    _sha(replay["published_bundle_sha256"], "replay_receipt.published_bundle_sha256")
    _sha(replay["downloaded_bundle_sha256"], "replay_receipt.downloaded_bundle_sha256")
    _sha(replay["census_receipt_sha256"], "replay_receipt.census_receipt_sha256")
    _sha(replay["strict_receipt_sha256"], "replay_receipt.strict_receipt_sha256")
    bundle_sha = provenance["replay_bundle"]["sha256"]
    if (replay["bundle_sha256"], replay["published_bundle_sha256"],
            replay["downloaded_bundle_sha256"]) != (bundle_sha, bundle_sha, bundle_sha):
        _fail("replay receipt does not bind the downloaded #510 bundle bytes")
    if replay["census_receipt_sha256"] != provenance["census_receipt"]["sha256"] \
            or replay["strict_receipt_sha256"] != provenance["strict_receipt"]["sha256"]:
        _fail("replay receipt does not bind the #510 receipt bytes")
    _commit(replay["contract_source_commit"], "replay_receipt.contract_source_commit")
    _commit(replay["contract_source_tree"], "replay_receipt.contract_source_tree")
    _commit(replay["candidate_source_commit"], "replay_receipt.candidate_source_commit")
    _commit(replay["candidate_source_tree"], "replay_receipt.candidate_source_tree")
    _boolean(replay["replayed"], "replay_receipt.replayed")
    if not replay["replayed"] or replay["result"] != "identity-and-evidence-replayed":
        _fail("replay_receipt does not record a completed independent replay")
    contract = binding["contract"]
    candidate = binding["subjects"]["candidate"]
    if replay["contract_source_commit"] != contract["source_commit"] \
            or replay["contract_source_tree"] != contract["source_tree"] \
            or replay["candidate_source_commit"] != candidate["source_commit"] \
            or replay["candidate_source_tree"] != candidate["source_tree"]:
        _fail("replay_receipt does not match the bound contract and candidate")


def _decimal(value, expected, name):
    if type(value) not in (int, float) or isinstance(value, bool):
        _fail(f"{name} must be a JSON number")
    try:
        actual = Decimal(str(value))
    except InvalidOperation:
        _fail(f"{name} is not a finite decimal")
    if actual != expected:
        _fail(f"{name} changes the approved limit: expected {expected}")
    return value


def _rules(value):
    value = _keys(value, ("thresholds", "sampling", "aggregation", "uncertainty",
                          "outcomes"), "rules")
    thresholds = _keys(value["thresholds"], ("aggregate", "per_cell"),
                        "rules.thresholds")
    for name, expected in (("aggregate", AGGREGATE_THRESHOLDS),
                           ("per_cell", CELL_THRESHOLDS)):
        values = _keys(thresholds[name], METRICS, f"rules.thresholds.{name}")
        for metric, limit in expected.items():
            _decimal(values[metric], limit, f"rules.thresholds.{name}.{metric}")

    sampling = _keys(value["sampling"], ("seed", "rounds", "pairs_per_round",
                                         "resamples", "bootstrap_members_per_scope",
                                         "cell_members_per_scope", "frozen_before_samples",
                                         "warmups_per_variant", "block_order",
                                         "fixed_order", "optional_stopping",
                                         "outlier_deletion", "retain_all_samples"),
                     "rules.sampling")
    _positive_int(sampling["seed"], "rules.sampling.seed")
    if sampling["seed"] > (1 << 64) - 1:
        _fail("rules.sampling.seed must fit the #619 uint64 domain")
    if type(sampling["rounds"]) is not int or sampling["rounds"] != 2:
        _fail("rules.sampling.rounds must be exactly 2")
    _positive_int(sampling["pairs_per_round"], "rules.sampling.pairs_per_round")
    if sampling["pairs_per_round"] < 60 or sampling["pairs_per_round"] > 256:
        _fail("rules.sampling.pairs_per_round must be between 60 and 256")
    if sampling["pairs_per_round"] % 2:
        _fail("rules.sampling.pairs_per_round must be even for AB/BA blocks")
    _positive_int(sampling["resamples"], "rules.sampling.resamples")
    if sampling["resamples"] < 100000 or sampling["resamples"] > 1000000:
        _fail("rules.sampling.resamples must be between 100000 and 1000000")
    _positive_int(sampling["bootstrap_members_per_scope"],
                  "rules.sampling.bootstrap_members_per_scope")
    if sampling["bootstrap_members_per_scope"] > 80:
        _fail("rules.sampling.bootstrap_members_per_scope exceeds #619 cap")
    _positive_int(sampling["cell_members_per_scope"],
                  "rules.sampling.cell_members_per_scope")
    if sampling["cell_members_per_scope"] > 300000:
        _fail("rules.sampling.cell_members_per_scope exceeds #619 cap")
    if sampling["frozen_before_samples"] is not True:
        _fail("rules.sampling.frozen_before_samples must be true")
    _boolean(sampling["frozen_before_samples"], "rules.sampling.frozen_before_samples")
    if type(sampling["warmups_per_variant"]) is not int or sampling["warmups_per_variant"] != 2:
        _fail("rules.sampling.warmups_per_variant must be exactly 2")
    if sampling["block_order"] != "one-AB-and-one-BA-pair":
        _fail("rules.sampling.block_order is not the approved paired order")
    if sampling["fixed_order"] != "seeded-cell-order-and-first-order":
        _fail("rules.sampling.fixed_order must be predeclared")
    _boolean(sampling["optional_stopping"], "rules.sampling.optional_stopping")
    _boolean(sampling["outlier_deletion"], "rules.sampling.outlier_deletion")
    _boolean(sampling["retain_all_samples"], "rules.sampling.retain_all_samples")
    if sampling["optional_stopping"] or sampling["outlier_deletion"]:
        _fail("optional stopping and outlier deletion are forbidden")
    if not sampling["retain_all_samples"]:
        _fail("all samples must be retained")

    aggregation = _keys(value["aggregation"], ("ratio", "wall_time", "peak_rss",
                                                "code_bytes", "runtime", "cell_weight",
                                                "denominator", "scope", "runtime_eligibility",
                                                "code_bytes_scope"), "rules.aggregation")
    if aggregation["ratio"] != "candidate-over-baseline":
        _fail("rules.aggregation.ratio is not candidate/baseline")
    for name in ("wall_time", "peak_rss", "runtime"):
        if aggregation[name] != "median-of-two-pair-block-geometric-means":
            _fail(f"rules.aggregation.{name} must use #619 block geometric means")
    if aggregation["code_bytes"] != "exact-code-section-sum-ratio":
        _fail("rules.aggregation.code_bytes must use exact code-section bytes")
    if aggregation["cell_weight"] != "one-equal-weight-per-required-cell":
        _fail("rules.aggregation.cell_weight changes required-cell weighting")
    if aggregation["denominator"] != "requested-work-from-manifest":
        _fail("rules.aggregation.denominator must be the manifest denominator")
    if aggregation["scope"] != "both-rounds-and-pooled-analysis":
        _fail("rules.aggregation.scope must include both rounds and pooled analysis")
    if aggregation["runtime_eligibility"] != "independent-native-executable-oracle-only":
        _fail("rules.aggregation.runtime_eligibility is not native-oracle-only")
    if aggregation["code_bytes_scope"] != "deterministic-code-section-payload-only":
        _fail("rules.aggregation.code_bytes_scope must be deterministic code sections")

    uncertainty = _keys(value["uncertainty"], ("confidence", "simultaneous",
                                                "family_correction", "resampling",
                                                "invalid_data", "family"), "rules.uncertainty")
    if uncertainty["confidence"] != "one-sided-95-percent-upper-bound":
        _fail("rules.uncertainty.confidence is not one-sided 95 percent")
    if not uncertainty["simultaneous"]:
        _fail("rules.uncertainty must be simultaneous")
    _boolean(uncertainty["simultaneous"], "rules.uncertainty.simultaneous")
    if uncertainty["family_correction"] != "Bonferroni":
        _fail("rules.uncertainty.family_correction must be Bonferroni")
    resampling = _keys(uncertainty["resampling"], ("method", "resamples",
                                                    "block_unit", "seeded"),
                       "rules.uncertainty.resampling")
    _boolean(resampling["seeded"], "rules.uncertainty.resampling.seeded")
    if resampling["method"] == "paired-block-bootstrap":
        _positive_int(resampling["resamples"], "rules.uncertainty.resampling.resamples")
        if resampling["resamples"] < 100000 or resampling["resamples"] > 1000000:
            _fail("rules.uncertainty.resampling.resamples must be between 100000 and 1000000")
        if resampling["block_unit"] != "paired-round-block" or not resampling["seeded"]:
            _fail("paired bootstrap must use seeded paired-round blocks")
    elif resampling["method"] == "not-used":
        if resampling["resamples"] != 0 or resampling["block_unit"] != "not-applicable" \
                or resampling["seeded"]:
            _fail("unused resampling must be explicitly not-applicable")
    else:
        _fail("rules.uncertainty.resampling.method is not predeclared")
    if uncertainty["invalid_data"] != "fail-closed":
        _fail("rules.uncertainty.invalid_data must fail closed")
    family = _keys(uncertainty["family"], ("scopes", "dimensions", "metrics",
                                           "simultaneous", "pic_slices"),
                   "rules.uncertainty.family")
    _exact_list(family["scopes"], STATISTICAL_SCOPES,
                "rules.uncertainty.family.scopes")
    _exact_list(family["dimensions"], STATISTICAL_DIMENSIONS,
                "rules.uncertainty.family.dimensions")
    _exact_list(family["metrics"], STATISTICAL_METRICS,
                "rules.uncertainty.family.metrics")
    _boolean(family["simultaneous"], "rules.uncertainty.family.simultaneous")
    if not family["simultaneous"]:
        _fail("statistical family members must be simultaneous")
    if family["pic_slices"] != "approved-as-required-slices":
        _fail("PIC slices must be explicitly approved in the statistical family")

    outcomes = _keys(value["outcomes"], ("allowed", "pass_requires", "only_pass_accepts"),
                     "rules.outcomes")
    _exact_list(outcomes["allowed"], OUTCOMES, "rules.outcomes.allowed")
    if outcomes["pass_requires"] != "all-identities-rows-oracles-rounds-bounds-and-code-bytes":
        _fail("rules.outcomes.pass_requires is incomplete")
    if outcomes["only_pass_accepts"] is not True:
        _fail("rules.outcomes.only_pass_accepts must be true")
    return value


def _check_sampling_family(population, rules):
    """Bind the #619 replay plan to independently derived family cardinalities."""
    family = population["statistical_family"]
    counts = _family_member_counts(family)
    sampling = rules["sampling"]
    uncertainty = rules["uncertainty"]["resampling"]
    if sampling["bootstrap_members_per_scope"] != counts["bootstrap_members_per_scope"]:
        _fail("sampling bootstrap member count differs from the derived family")
    if sampling["cell_members_per_scope"] != counts["cell_members_per_scope"]:
        _fail("sampling exact-cell count differs from the derived family")
    if sampling["resamples"] != uncertainty["resamples"]:
        _fail("sampling and uncertainty resample counts differ")
    return counts


def _check_evidence(root, artifact, name):
    root = Path(root).resolve()
    relative = PurePosixPath(artifact["path"])
    target = root.joinpath(*relative.parts)
    try:
        target.resolve().relative_to(root)
    except ValueError:
        _fail(f"{name}.path escapes evidence root")
    cursor = root
    for part in relative.parts:
        cursor /= part
        if cursor.is_symlink():
            _fail(f"{name}.path contains a symbolic link")
    if not target.is_file() or target.is_symlink():
        _fail(f"{name} is missing or is a symbolic link: {artifact['path']}")
    if target.stat().st_size != artifact["bytes"]:
        _fail(f"{name} byte count does not match evidence")
    digest = hashlib.sha256()
    with target.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != artifact["sha256"]:
        _fail(f"{name} digest does not match evidence")


def _evidence_bytes(root, artifact, name):
    _check_evidence(root, artifact, name)
    root = Path(root).resolve()
    relative = PurePosixPath(artifact["path"])
    return root.joinpath(*relative.parts).read_bytes()



def _bounded_code_bytes(value, name, *, positive=False):
    if type(value) is not int or value < (1 if positive else 0) or value > (1 << 63) - 1:
        qualifier = "positive " if positive else "nonnegative "
        _fail(f"{name} must be a {qualifier}bounded integer")
    return value


def _row_ids(value, name, row_count):
    rows = _list(value, name)
    for index, row in enumerate(rows):
        if type(row) is not int or not 0 <= row < row_count:
            _fail(f"{name}[{index}] is outside the complete census population")
    if rows != sorted(set(rows)):
        _fail(f"{name} must be sorted and unique")
    return rows


def _tsv_rows(data, fields, name, *, allow_empty=False):
    """Parse a strict TSV, optionally permitting a header-only table."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        _fail(f"{name} is not valid UTF-8 TSV: {error}")
    expected = list(fields)
    reader = csv.DictReader(io.StringIO(text), delimiter="\t", lineterminator="\n")
    if reader.fieldnames != expected:
        _fail(f"{name} header does not match the reviewed schema")
    rows = []
    for index, row in enumerate(reader):
        if None in row or any(value is None for value in row.values()):
            _fail(f"{name}[{index}] is malformed")
        if any("\r" in value or "\n" in value for value in row.values()):
            _fail(f"{name}[{index}] contains a multi-line field")
        rows.append(row)
    if not rows and not allow_empty:
        _fail(f"{name} is empty")
    return rows


def _validator_projection(report, row_count):
    # Validate the producer's complete applicability/admission partition.
    if report["profile"] != "full-census":
        _fail("#508 validator report is not the production full-census profile")
    _exact_list(report["applicability_classes"], list(RETIREMENT_SCHEMA.APPLICABILITY_CLASSES),
                "validator_report.applicability_classes")
    _exact_list(report["admission_classes"], list(RETIREMENT_SCHEMA.APPLICABILITY_CLASSES),
                "validator_report.admission_classes")
    counts = _keys(report["applicability_counts"], RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                   "validator_report.applicability_counts")
    admission_counts = _keys(report["admission_counts"], RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                             "validator_report.admission_counts")
    rows_by_class = _keys(report["applicability_rows_by_class"],
                          RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                          "validator_report.applicability_rows_by_class")
    admission_rows = _keys(report["admission_rows_by_class"],
                           RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                           "validator_report.admission_rows_by_class")
    by_row = {}
    for classification in RETIREMENT_SCHEMA.APPLICABILITY_CLASSES:
        rows = _row_ids(rows_by_class[classification],
                        f"validator_report.applicability_rows_by_class.{classification}",
                        row_count)
        _nonnegative_int(counts[classification],
                         f"validator_report.applicability_counts.{classification}")
        _nonnegative_int(admission_counts[classification],
                         f"validator_report.admission_counts.{classification}")
        if counts[classification] != len(rows):
            _fail("validator report applicability count differs from its authenticated row set")
        if admission_counts[classification] != counts[classification] \
                or admission_rows[classification] != rows:
            _fail("validator report admission projection differs from applicability")
        for row in rows:
            if row in by_row:
                _fail("validator report applicability classes overlap")
            by_row[row] = classification
    if set(by_row) != set(range(row_count)) or report["applicability_rows"] != row_count:
        _fail("validator report applicability projection is not a complete row partition")
    skip_rows = _row_ids(report["applicability_skip_rows"],
                         "validator_report.applicability_skip_rows", row_count)
    permitted_skips = {
        row for row, classification in by_row.items()
        if classification in {"retained-control", "platform-inapplicable", "unavailable"}
    }
    # Allocator none may be a measured direct-reference retained control.
    # Only the independently replayed skip ledger determines non-execution.
    if not set(skip_rows) <= permitted_skips:
        _fail("validator report skips an admitted executed row")
    if report["global_identity_unique"] is not True:
        _fail("validator report does not prove global row identity uniqueness")
    return by_row, set(skip_rows)


def _report_evidence_path(root, value, name, *, directory=False):
    _string(value, name)
    root = Path(root).resolve()
    candidate = Path(value)
    if candidate.is_absolute():
        candidate = Path(value)
    else:
        relative = _relative_path(value, name)
        candidate = root.joinpath(*PurePosixPath(relative).parts)
    try:
        candidate.relative_to(root)
    except ValueError:
        _fail(f"{name} escapes the evidence root")
    relative = _relative_path(candidate.relative_to(root).as_posix(), name)
    cursor = root
    for part in PurePosixPath(relative).parts:
        cursor /= part
        if cursor.is_symlink():
            _fail(f"{name} contains a symbolic link")
    exists = candidate.is_dir() if directory else candidate.is_file()
    if not exists or candidate.is_symlink():
        _fail(f"{name} is missing, has the wrong type, or is a symbolic link")
    return candidate


def _check_validator_projection_evidence(root, report, census_rows, by_row, skip_rows):
    if report["applicability_evidence"] != report["applicability_tsv"]:
        _fail("validator report applicability aliases identify different files")
    if report["residual_evidence"] != report["residual_tsv"]:
        _fail("validator report residual aliases identify different files")
    applicability_path = _report_evidence_path(
        root, report["applicability_evidence"], "validator_report.applicability_evidence")
    skip_path = _report_evidence_path(
        root, report["applicability_skip_evidence"],
        "validator_report.applicability_skip_evidence")
    residual_path = _report_evidence_path(
        root, report["residual_evidence"], "validator_report.residual_evidence")
    if hashlib.sha256(applicability_path.read_bytes()).hexdigest() != report["applicability_sha256"]:
        _fail("validator report applicability digest differs from its evidence")
    if hashlib.sha256(residual_path.read_bytes()).hexdigest() != report["residual_sha256"]:
        _fail("validator report residual digest differs from its evidence")

    applicability = _tsv_rows(
        applicability_path.read_bytes(), RETIREMENT_SCHEMA.APPLICABILITY_FIELDS,
        "applicability.tsv")
    if len(applicability) != len(census_rows):
        _fail("applicability.tsv is not the complete census population")
    reasons = {}
    candidate_failures = []
    reference_failures = []
    acceptance_failures = []
    for index, item in enumerate(applicability):
        if int(item["row"]) != index:
            _fail("applicability.tsv rows are not contiguous")
        source = census_rows[index]
        expected = {
            "group": source["group"], "fixture": source["fixture"], "target": source["target"],
            "cpu": source["cpu"], "frontend": source["frontend_lowering"],
            "allocator": source["allocator"], "PIC": source["PIC"],
        }
        if any(item[key] != value for key, value in expected.items()):
            _fail("applicability.tsv identity differs from rows.tsv")
        if item["applicability"] != by_row[index] or item["admission"] != by_row[index]:
            _fail("applicability.tsv classification differs from the report partition")
        for field, target in (("candidate_failure", candidate_failures),
                              ("reference_failure", reference_failures),
                              ("acceptance_failure", acceptance_failures)):
            if item[field] not in {"0", "1"}:
                _fail(f"applicability.tsv {field} is not boolean")
            if item[field] == "1":
                target.append(index)
        reasons[index] = item["reason"]
    if candidate_failures != report["candidate_failure_rows"] \
            or reference_failures != report["reference_failure_rows"] \
            or acceptance_failures != report["acceptance_failure_rows"]:
        _fail("applicability.tsv failure projection differs from the validator report")

    skips = _tsv_rows(
        skip_path.read_bytes(), RETIREMENT_SCHEMA.APPLICABILITY_SKIP_FIELDS,
        "applicability-skips.tsv", allow_empty=True)
    if [int(item["row"]) for item in skips] != sorted(skip_rows):
        _fail("applicability-skips.tsv differs from the authenticated skip row set")
    for item in skips:
        row = int(item["row"])
        source = census_rows[row]
        expected = {"group": source["group"], "fixture": source["fixture"],
                    "target": source["target"], "allocator": source["allocator"],
                    "applicability": by_row[row], "reason": reasons[row]}
        # Non-object source obligations determine skip provenance even when
        # final applicability describes unavailable native execution instead.
        # The census replay authenticates both projections independently.
        if source["compile_obligation"] == "registered-non-object-control":
            expected["applicability"] = "retained-control"
            expected["reason"] = source["compile_obligation"]
        if any(item[key] != value for key, value in expected.items()):
            _fail("applicability-skips.tsv is not row-bound to trusted source evidence")
    artifacts = []
    for path in (applicability_path, skip_path, residual_path):
        data = path.read_bytes()
        artifacts.append({"path": path.relative_to(Path(root).resolve()).as_posix(),
                          "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return {
        "artifacts": artifacts,
        "applicability_bytes": applicability_path.read_bytes(),
        "skip_bytes": skip_path.read_bytes(),
        "residual_bytes": residual_path.read_bytes(),
        "reasons": reasons,
    }


TARGET_ABIS = {
    "aarch64-apple-ios": "darwin-aarch64",
    "aarch64-apple-macos": "darwin-aarch64",
    "aarch64-linux-android": "aapcs64",
    "aarch64-pc-windows-msvc": "windows-aarch64",
    "aarch64-unknown-linux-gnu": "aapcs64",
    "aarch64-unknown-uefi": "aapcs64",
    "x86_64-apple-ios": "systemv-x86_64",
    "x86_64-apple-macos": "systemv-x86_64",
    "x86_64-linux-android": "systemv-x86_64",
    "x86_64-pc-windows-msvc": "win64-x86_64",
    "x86_64-unknown-linux-gnu": "systemv-x86_64",
    "x86_64-unknown-uefi": "win64-x86_64",
}


def _replay_validator_report(root, validator_report, projection_evidence):
    directories = _list(validator_report["directories"],
                        "validator_report.directories")
    if not directories:
        _fail("#508 validator report must name the validated census shard directories")
    directory_paths = [
        _report_evidence_path(root, directory,
                              f"validator_report.directories[{index}]", directory=True)
        for index, directory in enumerate(directories)
    ]
    # Re-run the repository's actual schema-2 validator over the bound shard
    # directories.  The receipt above is only accepted when every report
    # field (including both candidate and acceptance gates) equals this fresh
    # recomputation; a copied ``clean_*`` claim cannot pass.
    validator_program = Path(__file__).resolve().with_name("native_retirement_contract.py")
    if not validator_program.is_file():
        _fail("#508 schema-2 validator source is unavailable for replay")
    with tempfile.TemporaryDirectory(prefix="retirement-census-replay-") as replay_dir:
        output = Path(replay_dir) / "validator-report.json"
        command = [sys.executable, str(validator_program), "validate-shards",
                   *(str(path) for path in directory_paths), "--out", str(output)]
        if validator_report["require_clean_candidate"]:
            command.append("--require-clean-candidate")
        if validator_report["require_clean_acceptance"]:
            command.append("--require-clean-acceptance")
        if validator_report["reference_supplement_sha256"]:
            command.append("--reference-supplements")
        replay = subprocess.run(command, check=False, capture_output=True, text=True,
                                cwd=Path(root).resolve())
        expected_success = not (
            validator_report["require_clean_candidate"]
            and validator_report["candidate_failure_rows"]) and not (
            validator_report["require_clean_acceptance"]
            and validator_report["acceptance_failure_rows"])
        if (replay.returncode == 0) is not expected_success or not output.is_file():
            _fail("#508 schema-2 validator replay status differs from retained failures")
        try:
            recomputed = json.loads(output.read_text(encoding="utf-8"),
                                    object_pairs_hook=_json_object)
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            _fail(f"#508 schema-2 validator replay output is invalid: {error}")
        path_fields = set(RETIREMENT_SCHEMA.PATH_FIELDS)
        for field, expected in validator_report.items():
            if field in path_fields:
                continue
            if recomputed.get(field) != expected:
                _fail(f"#508 schema-2 validator replay differs in {field}")
        _keys(recomputed, RETIREMENT_SCHEMA.VALIDATOR_REPORT_FIELDS, "replayed validator report")
        replay_files = {
            "applicability_evidence": projection_evidence["applicability_bytes"],
            "applicability_skip_evidence": projection_evidence["skip_bytes"],
            "residual_evidence": projection_evidence["residual_bytes"],
        }
        for field, expected_bytes in replay_files.items():
            replay_path = Path(recomputed[field])
            if not replay_path.is_file() or replay_path.read_bytes() != expected_bytes:
                _fail(f"#508 schema-2 validator replay differs in {field}")


def _check_census_cpu_axes(census_rows, axes):
    # These rows are authenticated by the mandatory census replay. Preserve
    # their exact CPU profiles, including target-scoped fixture overrides.
    if axes["cpus"] != sorted({row["cpu"] for row in census_rows}):
        _fail("canonical performance CPU profiles differ from the census")


def _check_support_output(root, binding, row_data, native_target=None):
    """Cross-check every performance row against the independent #508 output.

    The support declaration and validator report are intentionally separate
    artifacts.  A record that merely rewrites six files and recomputes their
    counts cannot pass: the declaration is pinned by digest, the validator
    report is checked against the generated census files, and every object row
    is joined to the canonical performance population.
    """
    support = binding["support"]
    support_declaration = _support_file(support, "support_declaration")
    if support_declaration["path"] != SUPPORT_DECLARATION_PATH:
        _fail("#508 support declaration path is not the frozen declaration")
    support_sha256 = support_declaration["sha256"]
    if support_sha256 not in (SUPPORT_DECLARATION_SHA256,
                             NEXT_SUPPORT_DECLARATION_SHA256,
                             PROPOSED_SUPPORT_DECLARATION_SHA256):
        _fail("#508 support declaration digest is not the approved immutable input")
    declaration_data = _evidence_bytes(root, support_declaration,
                                       "support.files.support_declaration")
    if hashlib.sha256(declaration_data).hexdigest() != support_sha256:
        _fail("#508 support declaration bytes changed")
    declaration = _tsv(declaration_data, SUPPORT_DECLARATION_FIELDS,
                       "support_declaration")
    declaration_inputs = len(declaration)
    declaration_subjects = sum(row["role"] == "subject" for row in declaration)
    declaration_groups = declaration_subjects * len(TARGETS) * len(FRONTENDS) * len(PIC)
    declaration_object_rows = declaration_groups * len(ALLOCATORS)
    if [row["path"] for row in declaration] != sorted(row["path"] for row in declaration):
        _fail("#508 support declaration paths are not sorted")
    if len({row["path"] for row in declaration}) != len(declaration):
        _fail("#508 support declaration contains duplicate paths")
    subjects = {row["path"] for row in declaration if row["role"] == "subject"}
    if len(subjects) != declaration_subjects:
        _fail("#508 support declaration subject count is inconsistent")

    manifest_artifact = _support_file(support, "manifest")
    manifest_data = _evidence_bytes(root, manifest_artifact, "support.files.manifest")
    manifest = _properties(manifest_data, "manifest")
    required_manifest = {
        "version", "kind", "identity_hash", "support_contract",
        "support_contract_sha256", "inputs", "rows", "manifest_only",
        "environment", "unfrozen_dependencies", "sysroot", "system_include",
        "resource_include_sha256", "compiler_revision_claim",
        "baseline_revision_claim", "compiler_hash", "compiler_bytes",
        "compiler_sha256", "baseline_hash", "baseline_bytes", "baseline_sha256",
        "cpu",
    }
    missing = required_manifest - set(manifest)
    if missing:
        _fail("#508 manifest omits: " + ", ".join(sorted(missing)))
    if manifest["version"] != "2" or manifest["kind"] != "object-coverage":
        _fail("#508 manifest is not object-census v2")
    if manifest["identity_hash"] != "sha256" or manifest["manifest_only"] != "0":
        _fail("#508 manifest is not an executed, SHA-256 census")
    if manifest["support_contract"] != SUPPORT_DECLARATION_PATH:
        _fail("#508 manifest does not identify the frozen support declaration")
    if manifest["support_contract_sha256"] != support_sha256:
        _fail("#508 manifest support declaration digest differs")
    if manifest["environment"] != "explicit-replacement-in-environment.tsv":
        _fail("#508 manifest lacks the explicit replacement environment")
    if manifest["unfrozen_dependencies"] != "none-for-object-census":
        _fail("#508 manifest records unfrozen dependencies")
    if manifest["sysroot"] != "none" or manifest["system_include"] != "none":
        _fail("#508 object census has an unbound sysroot or system include")
    if _decimal_text(manifest["inputs"], "manifest.inputs") != declaration_inputs:
        _fail("#508 manifest input count differs from the declaration")
    if _decimal_text(manifest["rows"], "manifest.rows") != declaration_object_rows:
        _fail("#508 manifest row count is not the complete object cross-product")
    for key in ("compiler_hash", "baseline_hash"):
        _decimal_text(manifest[key], f"manifest.{key}")
    for key in ("compiler_bytes", "baseline_bytes"):
        _positive_int(_decimal_text(manifest[key], f"manifest.{key}"),
                      f"manifest.{key}")
    _sha(manifest["compiler_sha256"], "manifest.compiler_sha256")
    _sha(manifest["baseline_sha256"], "manifest.baseline_sha256")
    candidate_binary = binding["subjects"]["candidate"]["binary"]
    baseline_binary = binding["subjects"]["baseline"]["binary"]
    if (manifest["compiler_bytes"], manifest["compiler_sha256"]) != \
            (str(candidate_binary["bytes"]), candidate_binary["sha256"]):
        _fail("#508 manifest compiler identity differs from the bound candidate binary")
    if (manifest["baseline_bytes"], manifest["baseline_sha256"]) != \
            (str(baseline_binary["bytes"]), baseline_binary["sha256"]):
        _fail("#508 manifest baseline identity differs from the bound direct binary")
    _token(manifest["cpu"], "manifest.cpu")
    for key in ("compiler_revision_claim", "baseline_revision_claim"):
        _commit(manifest[key], f"manifest.{key}")
    _sha(manifest["resource_include_sha256"], "manifest.resource_include_sha256")

    inputs_artifact = _support_file(support, "inputs")
    inputs = _tsv(_evidence_bytes(root, inputs_artifact, "support.files.inputs"),
                  INPUT_FIELDS, "inputs.tsv")
    if len(inputs) != declaration_inputs:
        _fail("#508 inputs.tsv is incomplete")
    if [row["path"] for row in inputs] != sorted(row["path"] for row in inputs):
        _fail("#508 inputs.tsv paths are not sorted")
    inputs_by_path = {row["path"]: row for row in inputs}
    if len(inputs_by_path) != len(inputs):
        _fail("#508 inputs.tsv contains duplicate paths")
    for index, (approved, row) in enumerate(zip(declaration, inputs)):
        if tuple(approved[field] for field in SUPPORT_DECLARATION_FIELDS) != \
                tuple(row[field] for field in SUPPORT_DECLARATION_FIELDS):
            _fail(f"#508 inputs.tsv differs from the declaration at row {index}")
        _decimal_text(row["bytes"], f"inputs.tsv[{index}].bytes")
        if not row["buster_hash_64"].isdigit():
            _fail(f"inputs.tsv[{index}].buster_hash_64 is not a decimal hash")
        _sha(row["sha256"], f"inputs.tsv[{index}].sha256")
        _token(row["fixture_recipe"], f"inputs.tsv[{index}].fixture_recipe")

    dependencies_artifact = _support_file(support, "dependencies")
    dependencies = _tsv(
        _evidence_bytes(root, dependencies_artifact, "support.files.dependencies"),
        DEPENDENCY_FIELDS, "dependencies.tsv")
    dependency_paths = []
    for index, row in enumerate(dependencies):
        if row["kind"] != "resource-header":
            _fail(f"dependencies.tsv[{index}] is not a resource-header row")
        dependency_paths.append(row["path"])
        _relative_path(row["path"], f"dependencies.tsv[{index}].path")
        _decimal_text(row["bytes"], f"dependencies.tsv[{index}].bytes")
        _sha(row["sha256"], f"dependencies.tsv[{index}].sha256")
    if dependency_paths != sorted(dependency_paths) \
            or len(set(dependency_paths)) != len(dependency_paths):
        _fail("dependencies.tsv paths are not sorted and unique")

    environment_artifact = _support_file(support, "environment")
    environment = _tsv(
        _evidence_bytes(root, environment_artifact, "support.files.environment"),
        ENVIRONMENT_FIELDS, "environment.tsv")
    environment_map = {row["name"]: (row["present"], row["value"]) for row in environment}
    if len(environment_map) != len(environment):
        _fail("environment.tsv contains duplicate names")
    if set(environment_map) not in ({"LC_ALL", "LANG", "TZ"},
                                    {"LC_ALL", "LANG", "TZ", "SystemRoot", "TEMP", "TMP"}):
        _fail("environment.tsv does not describe the explicit POSIX/Windows environment")
    for name, expected in (("LC_ALL", ("1", "C")), ("LANG", ("1", "C")),
                           ("TZ", ("1", "UTC"))):
        if environment_map[name] != expected:
            _fail(f"environment.tsv.{name} is not frozen")
    if set(environment_map) != {"LC_ALL", "LANG", "TZ"}:
        if environment_map["SystemRoot"][0] != "1" or not environment_map["SystemRoot"][1]:
            _fail("environment.tsv.SystemRoot is not explicit")
        for name in ("TEMP", "TMP"):
            if environment_map[name][0] != "1" or not Path(environment_map[name][1]).name == "temporary":
                _fail(f"environment.tsv.{name} is not explicit")

    rows_artifact = _support_file(support, "rows")
    census_rows = _tsv(_evidence_bytes(root, rows_artifact, "support.files.rows"),
                       ROW_FIELDS, "rows.tsv")
    if len(census_rows) != declaration_object_rows:
        _fail("#508 rows.tsv is not the complete object population")
    if [int(row["row"]) for row in census_rows] != list(range(len(census_rows))):
        _fail("#508 rows.tsv row IDs are not contiguous")
    expected_object = {}
    for index, row in enumerate(census_rows):
        if int(row["group"]) != index // len(ALLOCATORS):
            _fail(f"rows.tsv[{index}] has an invalid group")
        if row["allocator"] != ALLOCATORS[index % len(ALLOCATORS)]:
            _fail(f"rows.tsv[{index}] has an invalid allocator order")
        if row["fixture"] not in subjects:
            _fail(f"rows.tsv[{index}] names a non-subject fixture")
        if row["target"] not in TARGET_ABIS or row["target_abi"] != TARGET_ABIS[row["target"]]:
            _fail(f"rows.tsv[{index}] has an invalid target ABI")
        if row["cpu"] == "" or not row["cpu_features"]:
            _fail(f"rows.tsv[{index}] lacks explicit CPU identity")
        # The census validator authenticates target-scoped fixture CPU recipes
        # during independent replay below; manifest.cpu is only the fallback.
        if row["PIC"] not in PIC or row["selected"] not in {"0", "1"}:
            _fail(f"rows.tsv[{index}] has an invalid PIC/selection field")
        for field in ("fixture_recipe", "compile_obligation", "link_obligation",
                      "execution_obligation", "diagnostic_obligation"):
            _token(row[field], f"rows.tsv[{index}].{field}")
        _relative_path(row["argv_evidence"], f"rows.tsv[{index}].argv_evidence")
        if not row["argv_evidence"].endswith(".argv"):
            _fail(f"rows.tsv[{index}] does not identify a frozen argv artifact")
        input_row = inputs_by_path[row["fixture"]]
        if (row["fixture_recipe"], row["compile_obligation"]) != \
                (input_row["fixture_recipe"], input_row["compile_obligation"]):
            _fail(f"rows.tsv[{index}] fixture recipe/obligation differs from inputs.tsv")
        identity = tuple(row[field] for field in ROW_IDENTITY_FIELDS if field != "artifact_stage")
        expected_object[identity] = row
    if len(expected_object) != len(census_rows):
        _fail("#508 rows.tsv contains duplicate row identities")

    parsed, axes, family, sources = row_data
    if {role: sources[role] for role in ROW_SOURCE_ROLES} != \
            {role: _artifact_digest_map(support)[role] for role in ROW_SOURCE_ROLES}:
        _fail("canonical performance rows do not bind every #508 source digest")
    object_rows = [row for row in parsed if row["identity"]["artifact_stage"] == "object"]
    if len(object_rows) != declaration_object_rows:
        _fail("canonical performance rows omit #508 object rows")
    seen_object = set()
    for row in object_rows:
        identity = tuple(row["identity"][field] for field in ROW_IDENTITY_FIELDS
                         if field != "artifact_stage")
        if identity not in expected_object:
            _fail("canonical performance rows contain an object identity absent from #508")
        if identity in seen_object:
            _fail("canonical performance rows duplicate a #508 object identity")
        seen_object.add(identity)
        source = expected_object[identity]
        if row["identity"]["argv_evidence"] != source["argv_evidence"]:
            _fail("canonical performance row argv evidence differs from #508")
    if seen_object != set(expected_object):
        _fail("canonical performance rows do not cover every #508 object identity")
    min_stage_rows = declaration_object_rows + len(STAGES) - 1
    if len(parsed) < min_stage_rows:
        _fail("canonical performance rows do not contain the complete stage population")
    if axes["allocators"] != ALLOCATORS or axes["frontends"] != FRONTENDS \
            or axes["PIC"] != PIC or axes["stages"] != STAGES \
            or axes["targets"] != sorted(TARGETS):
        _fail("canonical performance rows do not cover the approved axes")
    _check_census_cpu_axes(census_rows, axes)

    performance_artifact = _support_file(support, "performance_declaration")
    performance = _read_json_evidence(root, performance_artifact,
                                      "support.files.performance_declaration")
    performance = _keys(performance, (
        "schema", "version", "support_declaration_sha256", "manifest_sha256",
        "inputs_sha256", "rows_sha256", "dependencies_sha256", "environment_sha256",
        "validator_report_sha256", "performance_rows_sha256", "object_row_count",
        "required_row_count", "axes", "row_identity_fields", "statistical_family",
        "runtime_eligibility", "code_section_eligibility"),
        "performance_declaration")
    if performance["schema"] != PERFORMANCE_DECLARATION_SCHEMA \
            or performance["version"] != PERFORMANCE_DECLARATION_VERSION:
        _fail("performance declaration schema/version is not approved")
    expected_digests = _artifact_digest_map(support)
    expected_digests["performance_rows"] = _support_file(support, "performance_rows")["sha256"]
    for role, field in (("support_declaration", "support_declaration_sha256"),
                        ("manifest", "manifest_sha256"), ("inputs", "inputs_sha256"),
                        ("rows", "rows_sha256"), ("dependencies", "dependencies_sha256"),
                        ("environment", "environment_sha256"),
                        ("validator_report", "validator_report_sha256"),
                        ("performance_rows", "performance_rows_sha256")):
        _sha(performance[field], f"performance_declaration.{field}")
        if performance[field] != expected_digests[role]:
            _fail(f"performance declaration {field} differs from #508 output")
    if performance["object_row_count"] != declaration_object_rows:
        _fail("performance declaration object row count is not #508's full count")
    if performance["required_row_count"] < min_stage_rows \
            or performance["required_row_count"] != len(parsed):
        _fail("performance declaration required row count is not recomputed")
    if performance["axes"] != axes or performance["statistical_family"] != family:
        _fail("performance declaration axes/family differ from canonical rows")
    _exact_list(performance["row_identity_fields"], ROW_IDENTITY_FIELDS,
                "performance_declaration.row_identity_fields")
    if performance["runtime_eligibility"] != "independent-native-executable-oracle-only":
        _fail("performance declaration runtime eligibility is not native-only")
    if performance["code_section_eligibility"] != "deterministic-code-section-payload-only":
        _fail("performance declaration code-section eligibility is not deterministic")

    validator_artifact = _support_file(support, "validator_report")
    validator_report = _read_json_evidence(root, validator_artifact,
                                           "support.files.validator_report")
    # This is the actual schema-2 report emitted by native_retirement_contract.py
    # validate-shards.  Do not replace it with a caller-invented success receipt:
    # candidate and acceptance gates have distinct meanings and remain visible.
    validator_report = _keys(
        validator_report, RETIREMENT_SCHEMA.VALIDATOR_REPORT_FIELDS,
        "validator_report")
    if validator_report["schema"] != 2 \
            or validator_report["complete_row_partition"] is not True:
        _fail("#508 validator report is not a complete schema-2 partition")
    applicability_by_row, applicability_skip_rows = _validator_projection(
        validator_report, declaration_object_rows)
    projection_evidence = _check_validator_projection_evidence(
        root, validator_report, census_rows, applicability_by_row,
        applicability_skip_rows)
    if validator_report["rows_validated"] != declaration_object_rows \
            or validator_report["groups"] != declaration_groups \
            or validator_report["shards"] <= 0:
        _fail("validator report counts are not the complete #508 inventory")
    if validator_report["support_contract_sha256"] != manifest["support_contract_sha256"] \
            or validator_report["resource_include_sha256"] != manifest["resource_include_sha256"] \
            or validator_report["compiler_sha256"] != manifest["compiler_sha256"] \
            or validator_report["baseline_sha256"] != manifest["baseline_sha256"]:
        _fail("schema-2 validator report does not bind the manifest identities")
    for field in ("require_clean_candidate", "require_clean_acceptance",
                  "clean_candidate", "clean_acceptance"):
        _boolean(validator_report[field], f"schema-2 validator report.{field}")
    if not validator_report["require_clean_candidate"] \
            or not validator_report["clean_candidate"]:
        _fail("schema-2 validator report does not enforce a clean candidate")
    for field in ("candidate_failure_rows", "reference_failure_rows",
                  "fallback_defect_rows", "telemetry_defect_rows",
                  "execution_defect_rows", "artifact_defect_rows",
                  "unexpected_failure_rows"):
        if validator_report[field]:
            _fail(f"schema-2 validator report contains {field}")
    unavailable_rows = validator_report["applicability_rows_by_class"]["unavailable"]
    if validator_report["acceptance_failure_rows"] != unavailable_rows:
        _fail("schema-2 acceptance failures are not exactly authenticated unavailable rows")
    if validator_report["clean_acceptance"] != (
            not validator_report["acceptance_failure_rows"]):
        _fail("schema-2 clean_acceptance disagrees with retained failure evidence")
    _replay_validator_report(root, validator_report, projection_evidence)
    compiler_eligible_rows = set()
    performance_applicability = {}
    eligible_object_rows = 0
    for row in parsed:
        identity = tuple(row["identity"][field] for field in ROW_IDENTITY_FIELDS
                         if field != "artifact_stage")
        source = expected_object.get(identity)
        if source is None:
            _fail("canonical performance row has no schema-2 census identity")
        census_row = int(source["row"])
        compiler_eligible = census_row not in applicability_skip_rows
        if row["metrics"]["compiler_wall_time"] is not compiler_eligible \
                or row["metrics"]["compiler_peak_rss"] is not compiler_eligible:
            _fail("canonical compiler eligibility differs from authenticated skip provenance")
        if not compiler_eligible:
            if any(row["metrics"][metric] for metric in METRICS) \
                    or row["eligibility"]["runtime_oracle"] != "not-applicable" \
                    or row["eligibility"]["code_section"] != "not-applicable":
                _fail("authenticated non-executed row contains measurement eligibility")
        else:
            compiler_eligible_rows.add(row["row"])
            if row["identity"]["artifact_stage"] == "object":
                eligible_object_rows += 1
        performance_applicability[row["row"]] = {
            "census_row": census_row,
            "classification": applicability_by_row[census_row],
            "reason": projection_evidence["reasons"][census_row],
            "compiler_eligible": compiler_eligible,
        }

    # Keep the byte-level identities available to the workflow validator.  A
    # result-input plan must bind the actual schema-2 artifacts, not a digest
    # recomputed from the parsed manifest or a caller-provided row total.
    return {"manifest": manifest, "inputs": inputs, "rows": census_rows,
            "dependencies": dependencies, "environment": environment,
            "sources": sources, "axes": axes, "family": family,
            "support_declaration_sha256": support_declaration["sha256"],
            "manifest_sha256": manifest_artifact["sha256"],
            "rows_sha256": rows_artifact["sha256"],
            "performance_rows_sha256": _support_file(support, "performance_rows")["sha256"],
            "validator_report_sha256": validator_artifact["sha256"],
            "object_row_count": declaration_object_rows,
            "eligible_object_row_count": eligible_object_rows,
            "compiler_eligible_rows": compiler_eligible_rows,
            "performance_applicability": performance_applicability,
            "projection_artifacts": projection_evidence["artifacts"],
            "applicability_sha256": validator_report["applicability_sha256"],
            "applicability_skip_rows": sorted(applicability_skip_rows),
            "group_count": declaration_groups}


def _workflow(value):
    """Validate the four non-circular workflow phase descriptors."""
    value = _keys(value, ("schema", "version", "phases", "records"), "workflow")
    if value["schema"] != WORKFLOW_SCHEMA or value["version"] != WORKFLOW_VERSION:
        _fail("workflow schema/version is not approved")
    phases = _keys(value["phases"], WORKFLOW_PHASES, "workflow.phases")
    records = _keys(value["records"], ("admission", "oracle", "result_input_plan"),
                    "workflow.records")
    for name, artifact in (*((phase, phases[phase]) for phase in WORKFLOW_PHASES),
                           *((name, records[name]) for name in records)):
        _artifact(artifact, f"workflow.{name}")
    return value


def _workflow_phase(root, artifact, schema, name):
    value = _read_json_evidence(root, artifact, f"workflow.{name}")
    value = _keys(value, ("schema", "version", "status"), f"workflow.{name}",
                    optional=("support_declaration_sha256", "manifest_sha256",
                              "rows_sha256", "family_sha256", "seed", "rounds",
                              "pairs_per_round", "resamples",
                              "bootstrap_members_per_scope", "cell_members_per_scope",
                              "result_input_plan_sha256", "pre_sample_plan_sha256",
                              "aa_admission_sha256", "post_aa_binding_sha256",
                              "sealed_result_sha256",
                              "result_bundle", "seal", "replay_bundle", "execution_plan",
                              "publication_receipt"))
    if value["schema"] != schema or value["version"] != 1:
        _fail(f"workflow.{name} schema/version is not approved")
    return value


def _result_input_plan(root, artifact, support_output, population, rules):
    value = _read_json_evidence(root, artifact, "workflow.result_input_plan")
    value = _keys(value, ("schema", "version", "source_manifest_sha256",
                          "source_rows_sha256", "identity_field", "coordinate_schema",
                          "sample_population", "eligible_population", "object_row_count",
                          "sample_row_count",
                          "rounds", "pairs_per_round", "records_per_row",
                          "required_records", "max_records_per_manifest", "manifest_count",
                          "manifests", "predeclared"), "result_input_plan")
    if value["schema"] != RESULT_INPUT_PLAN_SCHEMA or value["version"] != 1:
        _fail("result-input plan schema/version is not approved")
    if value["identity_field"] != "record_id":
        _fail("result-input plan must use the #615 record_id identity")
    if value["coordinate_schema"] != "row-round-pair-v1":
        _fail("result-input plan coordinate schema is not approved")
    if value["sample_population"] != \
            "trusted-census-eligible-performance-rows-with-required-metrics":
        _fail("result-input plan must sample every authenticated eligible row")
    if value["eligible_population"] != \
            "authenticated-applicability-minus-nonexecuted-rows":
        _fail("result-input plan must bind the trusted applicability projection")
    if value["source_manifest_sha256"] != support_output["manifest_sha256"]:
        _fail("result-input plan does not bind the actual schema-2 manifest")
    if value["source_rows_sha256"] != support_output["rows_sha256"]:
        _fail("result-input plan does not bind the canonical rows artifact")
    object_row_count = support_output["object_row_count"]
    if value["object_row_count"] != object_row_count:
        _fail("result-input plan object row count is not derived from schema-2 rows")
    _positive_int(value["sample_row_count"], "result_input_plan.sample_row_count")
    sampling = rules["sampling"]
    expected_records = value["sample_row_count"] * sampling["rounds"] * sampling["pairs_per_round"]
    if value["rounds"] != sampling["rounds"] \
            or value["pairs_per_round"] != sampling["pairs_per_round"] \
            or value["records_per_row"] != sampling["rounds"] * sampling["pairs_per_round"]:
        _fail("result-input plan does not bind the frozen sample dimensions")
    if value["required_records"] != expected_records:
        _fail("result-input plan record count does not cover every object row")
    if expected_records > RESULT_INPUT_MAX_TOTAL_RECORDS:
        _fail("result-input plan exceeds the immutable total-record ceiling")
    if value["max_records_per_manifest"] != RESULT_INPUT_MAX_RECORDS:
        _fail("result-input plan does not use #615's immutable record cap")
    if value["predeclared"] is not True:
        _fail("result-input plan must be predeclared")
    _boolean(value["predeclared"], "result_input_plan.predeclared")
    manifests = _list(value["manifests"], "result_input_plan.manifests")
    if value["manifest_count"] != len(manifests) or not manifests:
        _fail("result-input plan manifest count is inconsistent")
    expected_manifest_count = (expected_records + RESULT_INPUT_MAX_RECORDS - 1) // RESULT_INPUT_MAX_RECORDS
    if value["manifest_count"] != expected_manifest_count:
        _fail("result-input plan must use the canonical minimal bounded shard count")
    identities = set()
    paths = set()
    total = 0
    for index, item in enumerate(manifests):
        item = _keys(item, ("identity", "path", "start_record", "records"),
                     f"result_input_plan.manifests[{index}]")
        _token(item["identity"], f"result_input_plan.manifests[{index}].identity")
        _relative_path(item["path"], f"result_input_plan.manifests[{index}].path")
        _nonnegative_int(item["start_record"],
                         f"result_input_plan.manifests[{index}].start_record")
        _positive_int(item["records"], f"result_input_plan.manifests[{index}].records")
        if item["records"] > RESULT_INPUT_MAX_RECORDS:
            _fail("result-input manifest exceeds #615's immutable record cap")
        expected_partition_records = (RESULT_INPUT_MAX_RECORDS
                                      if index + 1 < expected_manifest_count
                                      else expected_records - total)
        if item["records"] != expected_partition_records:
            _fail("result-input manifest partitions must fill every cap-sized shard before the final shard")
        if item["identity"] in identities or item["path"] in paths:
            _fail("result-input manifests must have unique identities and paths")
        if item["start_record"] != total:
            _fail("result-input manifests must be contiguous predeclared partitions")
        identities.add(item["identity"])
        paths.add(item["path"])
        total += item["records"]
    if total != expected_records:
        _fail("result-input manifests do not cover every required record")
    return value


def _result_manifest_descriptors(value, result_plan):
    manifests = _list(value, "sealed_result_bundle.result_manifests")
    planned = result_plan["manifests"]
    if len(manifests) != len(planned):
        _fail("sealed result manifest count differs from the pre-sample partition plan")
    total_bytes = 0
    validated = []
    for index, (item, partition) in enumerate(zip(manifests, planned)):
        item = _keys(item, ("identity", "path", "bytes", "sha256", "start_record",
                            "records", "input_bytes"),
                     f"sealed_result_bundle.result_manifests[{index}]")
        _token(item["identity"],
               f"sealed_result_bundle.result_manifests[{index}].identity")
        _artifact({"path": item["path"], "bytes": item["bytes"],
                   "sha256": item["sha256"]},
                  f"sealed_result_bundle.result_manifests[{index}]")
        _positive_int(item["input_bytes"],
                      f"sealed_result_bundle.result_manifests[{index}].input_bytes")
        for field in ("identity", "path", "start_record", "records"):
            if item[field] != partition[field]:
                _fail("sealed result manifest differs from the pre-sample partition plan")
        total_bytes += item["input_bytes"]
        validated.append(item)
    if total_bytes > RESULT_INPUT_MAX_TOTAL_BYTES * len(validated):
        _fail("sealed result aggregate input bytes exceed the bounded shard policy")
    return validated


# Execution evidence is a separate trust boundary from result-file integrity.
# The caller obtains the receipt digest independently from the admitted control
# service; neither the result bundle nor the receipt may choose that trust root.
EXECUTION_PLAN_SCHEMA = "buster-native-retirement-execution-plan-v2"
EXECUTION_RECEIPT_SCHEMA = "buster-native-retirement-execution-receipt-v1"
EXECUTION_SCHEDULE = "tp-retirement-block-schedule-v2"
EXECUTION_LINE_CAP = 8192
EXECUTION_RECEIPT_BYTE_CAP = 1024 * 1024
EXECUTION_SHARD_CAP = 4096


def _execution_mix64(value):
    mask = (1 << 64) - 1
    value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & mask
    value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & mask
    return value ^ (value >> 31)


def _execution_block_schedule(seed, round_id, block, count):
    """Exact replay of #619's tp_retirement_block_schedule, not CI tp_run."""
    value = seed ^ 0x6e61746976652d31
    value ^= _execution_mix64(1 + 0x100000001b3)
    value ^= _execution_mix64(round_id + 0x9e3779b97f4a7c15)
    value ^= _execution_mix64(block + 0xd1b54a32d192ed03)
    value ^= _execution_mix64(count + 0x94d049bb133111eb)
    state = _execution_mix64(value)

    def bounded(bound):
        nonlocal state
        threshold = (1 << 64) % bound
        while True:
            state = (state + 0x9e3779b97f4a7c15) & ((1 << 64) - 1)
            number = _execution_mix64(state)
            if number >= threshold:
                return number % bound

    first = [bounded(2) for _ in range(count)]
    orders = [list(range(count)), list(range(count))]
    for order in orders:
        for remaining in range(count, 1, -1):
            index = bounded(remaining)
            order[remaining - 1], order[index] = order[index], order[remaining - 1]
    return first, orders


def _execution_schedule(rows, sampling):
    """Replay warmups and #619's uint64 seeded blocked/shuffled schedule.

    Compilation and eligible native runtime are separate serial campaigns.
    Each campaign uses the same frozen schedule seed; neither includes a PMU
    or allocation diagnostic replay. Memory is O(rows), not O(invocations).
    """
    seed = sampling["seed"]
    if type(seed) is not int or not 1 <= seed <= (1 << 64) - 1:
        _fail("execution schedule seed must fit the #619 uint64 domain")
    sequence = 0
    for kind in ("compiler", "runtime"):
        selected = sorted(
            row["row"] for row in rows
            if (kind == "compiler" and row["metrics"]["compiler_wall_time"])
            or (kind == "runtime" and row["metrics"]["generated_runtime"]))
        if len(selected) > 100000:
            _fail("execution schedule exceeds the #619 cell limit")
        for row in selected:
            for repeat in range(sampling["warmups_per_variant"]):
                for variant in ("baseline", "candidate"):
                    yield {"sequence": sequence, "kind": kind, "phase": "warmup",
                           "row": row, "round": None, "pair": None,
                           "warmup": repeat, "position": None, "variant": variant}
                    sequence += 1
        for round_id in range(sampling["rounds"]):
            for block in range(sampling["pairs_per_round"] // 2):
                first, orders = _execution_block_schedule(seed, round_id, block, len(selected))
                for pair_in_block, order in enumerate(orders):
                    for index in order:
                        for position in range(2):
                            variant = ("baseline", "candidate")[first[index] ^ pair_in_block ^ position]
                            yield {"sequence": sequence, "kind": kind, "phase": "sample",
                                   "row": selected[index], "round": round_id,
                                   "pair": block * 2 + pair_in_block, "warmup": None,
                                   "position": position, "variant": variant}
                            sequence += 1


def _execution_context(binding, raw_measurements_sha256):
    """Only pre-existing identities: never hash a receipt into itself."""
    phases = binding["workflow"]["phases"]
    return {
        "pre_sample_plan_sha256": phases["pre_sample_plan"]["sha256"],
        "post_aa_binding_sha256": phases["post_aa_binding"]["sha256"],
        "support_root_sha256": binding["support"]["root_sha256"],
        "baseline": binding["subjects"]["baseline"],
        "candidate": binding["subjects"]["candidate"],
        "measurement": binding["measurement"],
        "execution": binding["execution"],
        "admission_sha256": binding["workflow"]["records"]["admission"]["sha256"],
        "oracle_sha256": binding["workflow"]["records"]["oracle"]["sha256"],
        "raw_measurements_sha256": raw_measurements_sha256,
    }


def _check_execution_plan(root, descriptor, binding, parsed, sampling,
                          admitted_cpu, native_target):
    _artifact(descriptor, "execution_plan")
    plan = _read_json_evidence(root, descriptor, "execution_plan")
    plan = _keys(plan, ("schema", "version", "schedule", "seed", "rounds",
                        "pairs_per_round", "warmups_per_variant", "cpu",
                        "performance_rows_sha256", "rows"), "execution_plan")
    if plan["schema"] != EXECUTION_PLAN_SCHEMA or type(plan["version"]) is not int \
            or plan["version"] != 1 or plan["schedule"] != EXECUTION_SCHEDULE:
        _fail("execution plan schema/version/schedule is not supported")
    for key in ("seed", "rounds", "pairs_per_round", "warmups_per_variant"):
        if type(plan[key]) is not int or plan[key] != sampling[key]:
            _fail(f"execution plan.{key} differs from the frozen sampling policy")
    if not 1 <= plan["seed"] <= (1 << 64) - 1:
        _fail("execution schedule seed must fit the #619 uint64 domain")
    _nonnegative_int(plan["cpu"], "execution_plan.cpu")
    if type(admitted_cpu) is not int or admitted_cpu < 0 \
            or plan["cpu"] != admitted_cpu:
        _fail("execution plan CPU differs from the admitted host profile")
    if native_target not in TARGETS:
        _fail("admitted host profile does not identify a supported native target")
    performance_rows = _support_file(binding["support"], "performance_rows")
    if plan["performance_rows_sha256"] != performance_rows["sha256"]:
        _fail("execution plan does not bind the canonical performance rows")
    oracle = _read_json_evidence(root, binding["workflow"]["records"]["oracle"],
                                 "workflow.records.oracle")
    oracle_records = _list(oracle.get("records"), "execution oracle records")
    oracle_by_row = {}
    for item in oracle_records:
        if type(item) is not dict or type(item.get("row")) is not int \
                or item["row"] in oracle_by_row:
            _fail("execution oracle records have missing/duplicate row identities")
        oracle_by_row[item["row"]] = item
    contracts = _list(plan["rows"], "execution_plan.rows")
    rows = sorted(parsed, key=lambda row: row["row"])
    if len(contracts) != len(rows) or set(oracle_by_row) != {row["row"] for row in rows}:
        _fail("execution plan/oracles do not cover the complete canonical population")
    for contract, row in zip(contracts, rows):
        _keys(contract, ("row", "identity_sha256", "oracle_sha256", "baseline", "candidate"),
              "execution_plan.row")
        if type(contract["row"]) is not int or contract["row"] != row["row"] \
                or contract["identity_sha256"] != _canonical_json_digest(row["identity"]) \
                or contract["oracle_sha256"] != _canonical_json_digest(oracle_by_row[row["row"]]):
            _fail("execution plan row is not joined to its frozen identity and oracle")
        # Eligibility is not a caller-controlled escape hatch. Check the
        # bound independent oracle before any invocation reaches statistics;
        # the wider census/source joins remain part of workflow validation.
        oracle_item = _keys(oracle_by_row[row["row"]], (
            "row", "code_section_status", "code_section_bytes", "code_section_sha256",
            "runtime_oracle_status", "runtime_exit_code", "native_runtime"),
            "execution_plan.oracle")
        code_observed = oracle_item["code_section_status"] == "parsed-deterministic"
        if code_observed:
            _bounded_code_bytes(oracle_item["code_section_bytes"],
                                "execution oracle code-section size")
            _sha(oracle_item["code_section_sha256"], "execution oracle code-section digest")
            if oracle_item["code_section_bytes"] == 0 \
                    and oracle_item["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                _fail("zero-byte execution oracle does not bind the empty payload")
        elif oracle_item["code_section_status"] != "not-applicable" \
                or oracle_item["code_section_bytes"] is not None \
                or oracle_item["code_section_sha256"] is not None:
            _fail("inapplicable execution oracle must use explicit null code observations")
        code_eligible = code_observed and oracle_item["code_section_bytes"] > 0
        compile_eligible = row["metrics"]["compiler_wall_time"]
        if type(oracle_item["native_runtime"]) is not bool \
                or (compile_eligible and type(oracle_item["runtime_exit_code"]) is not int) \
                or (not compile_eligible and oracle_item["runtime_exit_code"] is not None):
            _fail("execution oracle native/status fields have invalid types")
        runtime_required = (row["metrics"]["compiler_wall_time"]
                            and _native_runtime_required(row, native_target))
        if oracle_item["native_runtime"] is not runtime_required:
            _fail("execution oracle native-runtime applicability differs from the frozen row obligation")
        if runtime_required:
            if oracle_item["runtime_oracle_status"] != "passed-native" \
                    or oracle_item["runtime_exit_code"] != 0:
                _fail("required native runtime lacks a passing independent oracle")
        elif oracle_item["runtime_oracle_status"] != "not-applicable" \
                or oracle_item["runtime_exit_code"] != (-1 if compile_eligible else None):
            _fail("inapplicable runtime oracle contradicts the frozen row obligation")
        runtime_eligible = runtime_required
        expected_code_marker = (
            "deterministic-code-section" if code_eligible
            else "deterministic-zero-baseline-code-section" if code_observed
            else "not-applicable")
        if row["metrics"]["generated_code_bytes"] is not code_eligible \
                or row["metrics"]["generated_runtime"] is not runtime_eligible \
                or row["eligibility"]["code_section"] != expected_code_marker:
            _fail("execution eligibility is not derived from the independent oracle")
        for variant in ("baseline", "candidate"):
            side = _keys(contract[variant], ("compiler_command_sha256", "artifact_sha256",
                          "code_section_sha256", "code_section_bytes",
                          "runtime_command_sha256", "runtime_output_sha256"),
                         f"execution_plan.row.{variant}")
            compile_eligible = row["metrics"]["compiler_wall_time"]
            for key in ("compiler_command_sha256", "artifact_sha256"):
                if compile_eligible:
                    _sha(side[key], f"execution_plan.row.{variant}.{key}")
                elif side[key] is not None:
                    _fail("untimed row compiler evidence must be explicitly null")
            if code_observed:
                _sha(side["code_section_sha256"], "execution plan code-section identity")
                _bounded_code_bytes(
                    side["code_section_bytes"], "execution plan code-section size",
                    positive=(variant == "baseline" and code_eligible))
                if side["code_section_bytes"] == 0 \
                        and side["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                    _fail("zero-byte execution plan payload does not bind empty bytes")
                if variant == "baseline" and (
                        side["code_section_bytes"] != oracle_item["code_section_bytes"]
                        or side["code_section_sha256"] != oracle_item["code_section_sha256"]):
                    _fail("execution plan baseline code fact differs from the independent oracle")
            elif side["code_section_sha256"] is not None or side["code_section_bytes"] is not None:
                _fail("ineligible code-section evidence must be explicitly absent")
            for key in ("runtime_command_sha256", "runtime_output_sha256"):
                if row["metrics"]["generated_runtime"]:
                    _sha(side[key], f"execution_plan.row.{variant}.{key}")
                elif side[key] is not None:
                    _fail("ineligible runtime evidence must be explicitly absent")
    return plan


def _execution_trace_records(root, shards, expected_count):
    """Parse bounded canonical JSONL and hash the SAME bytes that are consumed."""
    seen_paths = set()
    total = 0
    if not shards or len(shards) > min(expected_count, EXECUTION_SHARD_CAP):
        _fail("execution transcript shard population is invalid")
    for index, shard in enumerate(shards):
        _keys(shard, ("path", "bytes", "sha256", "records"), "execution trace shard")
        descriptor = {key: shard[key] for key in ("path", "bytes", "sha256")}
        _artifact(descriptor, "execution trace shard")
        _positive_int(shard["records"], "execution trace shard.records")
        if total + shard["records"] > expected_count \
                or shard["bytes"] > shard["records"] * EXECUTION_LINE_CAP:
            _fail("execution transcript exceeds the derived invocation/byte bound")
        if shard["path"] in seen_paths:
            _fail("execution transcript contains duplicate shard paths")
        seen_paths.add(shard["path"])
        # Common path checks reject escapes/symlinks. The second pass hashes
        # its own bytes as well, so a replaced shard cannot change observations
        # while retaining the checked descriptor's digest.
        _check_evidence(root, descriptor, f"execution trace shard {index}")
        path = Path(root).joinpath(*PurePosixPath(shard["path"]).parts)
        digest = hashlib.sha256()
        size = 0
        with path.open("rb") as stream:
            for _ in range(shard["records"]):
                line = stream.readline(EXECUTION_LINE_CAP + 1)
                if not line.endswith(b"\n") or len(line) > EXECUTION_LINE_CAP:
                    _fail("execution transcript line is missing, truncated, or oversized")
                size += len(line)
                digest.update(line)
                try:
                    value = json.loads(line.decode("utf-8"), object_pairs_hook=_json_object)
                except (ValueError, UnicodeError) as error:
                    _fail(f"invalid execution transcript JSON: {error}")
                canonical = (json.dumps(value, sort_keys=True, separators=(",", ":"),
                                         ensure_ascii=False) + "\n").encode("utf-8")
                if line != canonical:
                    _fail("execution transcript is not canonical JSONL")
                yield value
            if stream.read(1):
                _fail("execution transcript contains undeclared extra invocations")
        if size != shard["bytes"] or digest.hexdigest() != shard["sha256"]:
            _fail("consumed execution transcript differs from its authenticated digest")
        total += shard["records"]
    if total != expected_count:
        _fail("execution transcript omits required invocations")


def _check_execution_transcript(root, descriptor, plan_descriptor, binding, parsed,
                                 sampling, sample_db, raw_measurements_sha256,
                                 trusted_receipt_sha256, admitted_cpu,
                                 native_target):
    """Join independent supervisor evidence to every warmup and timed sample.

    The receipt digest is an OUT-OF-BAND caller input. Hashes supplied only by
    the result producer establish integrity, not execution authority. This
    function executes no code from the evidence bundle.
    """
    if trusted_receipt_sha256 is None:
        _fail("independently obtained trusted execution receipt digest is required")
    _sha(trusted_receipt_sha256, "trusted execution receipt digest")
    _artifact(descriptor, "execution_receipt")
    if descriptor["bytes"] > EXECUTION_RECEIPT_BYTE_CAP:
        _fail("execution receipt exceeds the bounded metadata size")
    if descriptor["sha256"] != trusted_receipt_sha256:
        _fail("execution receipt is not the independently trusted service receipt")
    receipt = _read_trusted_json_evidence(
        root, descriptor, "execution_receipt", trusted_receipt_sha256,
        EXECUTION_RECEIPT_BYTE_CAP)
    _keys(receipt, ("schema", "version", "context_sha256", "execution_plan_sha256",
                    "job_id", "attempt", "boot_id", "bound_at_ns", "completed_at_ns",
                    "invocations", "shards"), "execution_receipt")
    if receipt["schema"] != EXECUTION_RECEIPT_SCHEMA or type(receipt["version"]) is not int \
            or receipt["version"] != 1:
        _fail("execution receipt schema/version is not supported")
    if receipt["context_sha256"] != _canonical_json_digest(
            _execution_context(binding, raw_measurements_sha256)) \
            or receipt["execution_plan_sha256"] != plan_descriptor["sha256"]:
        _fail("execution receipt is not joined to this job's frozen binding and samples")
    for key in ("job_id", "boot_id"):
        _token(receipt[key], f"execution receipt.{key}")
    for key in ("attempt", "bound_at_ns", "completed_at_ns", "invocations"):
        _positive_int(receipt[key], f"execution receipt.{key}")
    if receipt["completed_at_ns"] <= receipt["bound_at_ns"]:
        _fail("execution receipt completion must follow pre-sample binding")
    plan = _check_execution_plan(root, plan_descriptor, binding, parsed, sampling,
                                 admitted_cpu, native_target)
    contracts = {item["row"]: item for item in plan["rows"]}
    row_by_id = {row["row"]: row for row in parsed}
    campaigns = (sum(row["metrics"]["compiler_wall_time"] for row in parsed)
                 + sum(row["metrics"]["generated_runtime"] for row in parsed))
    expected_count = campaigns * 2 * (sampling["warmups_per_variant"]
                                      + sampling["rounds"] * sampling["pairs_per_round"])
    if receipt["invocations"] != expected_count:
        _fail("execution receipt invocation count omits warmups or complete paired sampling")
    shards = _list(receipt["shards"], "execution receipt.shards")
    expected_order = iter(_execution_schedule(parsed, sampling))
    last_end = receipt["bound_at_ns"]
    count = 0
    process_instances = set()
    observations = _execution_trace_records(root, shards, expected_count)
    try:
        for value in observations:
            expected = next(expected_order, None)
            if expected is None:
                _fail("execution transcript contains an unexpected invocation")
            _keys(value, tuple(expected) + ("pid", "process_start_token",
                  "process_instance_sha256", "cpu", "started_ns", "finished_ns",
                  "exit_code", "signal", "timed_out", "cancelled", "executable_sha256",
                  "command_sha256", "output_sha256", "code_section_sha256",
                  "code_section_bytes", "wall_seconds", "peak_rss_bytes"),
                  "execution invocation")
            for key, identity in expected.items():
                if type(value[key]) is not type(identity) or value[key] != identity:
                    _fail(f"execution invocation.{key} differs from the frozen seeded schedule")
            for key in ("pid", "started_ns", "finished_ns"):
                _positive_int(value[key], f"execution invocation.{key}")
            _token(value["process_start_token"],
                   "execution invocation.process_start_token")
            _sha(value["process_instance_sha256"],
                 "execution invocation.process_instance_sha256")
            expected_process = _process_instance_digest(
                receipt["job_id"], receipt["attempt"], receipt["boot_id"],
                value["pid"], value["process_start_token"])
            if value["process_instance_sha256"] != expected_process:
                _fail("execution invocation process identity is not supervisor-bound")
            if expected_process in process_instances:
                _fail("execution transcript reuses a process instance across invocations")
            process_instances.add(expected_process)
            if value["started_ns"] <= last_end or value["finished_ns"] <= value["started_ns"] \
                    or value["finished_ns"] >= receipt["completed_at_ns"]:
                _fail("execution invocations overlap or violate the bound job interval")
            last_end = value["finished_ns"]
            if type(value["cpu"]) is not int or value["cpu"] != plan["cpu"]:
                _fail("execution invocation does not use its admitted CPU")
            if type(value["exit_code"]) is not int or value["exit_code"] != 0 \
                    or type(value["signal"]) is not int or value["signal"] != 0 \
                    or value["timed_out"] is not False or value["cancelled"] is not False:
                _fail("execution invocation did not complete successfully")
            side = contracts[value["row"]][value["variant"]]
            row = row_by_id[value["row"]]
            compiler = value["kind"] == "compiler"
            expected_binary = (binding["subjects"][value["variant"]]["binary"]["sha256"]
                               if compiler else side["artifact_sha256"])
            expected_output = side["artifact_sha256"] if compiler else side["runtime_output_sha256"]
            expected_command = side["compiler_command_sha256" if compiler else "runtime_command_sha256"]
            if value["executable_sha256"] != expected_binary \
                    or value["output_sha256"] != expected_output \
                    or value["command_sha256"] != expected_command:
                _fail("execution invocation binary, command, or oracle output is mismatched")
            for key in ("code_section_sha256", "code_section_bytes"):
                expected_value = side[key] if compiler else None
                if type(value[key]) is not type(expected_value) or value[key] != expected_value:
                    _fail("execution invocation code-section evidence is mismatched")
            seconds = value["wall_seconds"]
            if type(seconds) not in (int, float) or seconds <= 0 \
                    or (type(seconds) is float and not math.isfinite(seconds)):
                _fail("execution invocation wall time is not finite and positive")
            # Both values come from the same monotonic process interval. A
            # one-nanosecond tolerance permits decimal float serialization,
            # not substituting a different measured process or interval.
            elapsed_ns = value["finished_ns"] - value["started_ns"]
            if abs(Decimal(str(seconds)) * 1_000_000_000 - elapsed_ns) > 1:
                _fail("execution invocation wall time differs from its process interval")
            if compiler:
                _positive_int(value["peak_rss_bytes"], "execution invocation.peak_rss_bytes")
            elif value["peak_rss_bytes"] is not None:
                _fail("runtime evidence cannot masquerade as compiler peak RSS")
            if value["phase"] == "sample":
                samples = sample_db.execute(
                    "SELECT metric, baseline, candidate FROM samples "
                    "WHERE row_id=? AND round_id=? AND pair_id=?",
                    (value["row"], value["round"], value["pair"])).fetchall()
                selected_metrics = ({"compiler_wall_time": seconds,
                                     "compiler_peak_rss": value["peak_rss_bytes"],
                                     "generated_code_bytes": value["code_section_bytes"]}
                                    if compiler else {"generated_runtime": seconds})
                selected_metrics = {key: number for key, number in selected_metrics.items()
                                    if row["metrics"][key]}
                actual = {key: baseline if value["variant"] == "baseline" else candidate
                          for key, baseline, candidate in samples if key in selected_metrics}
                if set(actual) != set(selected_metrics) or any(
                        Decimal(actual[key]) != Decimal(str(number))
                        for key, number in selected_metrics.items()):
                    _fail("result sample is not the authenticated invocation's measurement")
            count += 1
    finally:
        observations.close()
    if count != expected_count or next(expected_order, None) is not None:
        _fail("execution transcript is missing required warmups or timed invocations")
    return {"receipt_sha256": trusted_receipt_sha256, "invocations": count,
            "job_id": receipt["job_id"], "attempt": receipt["attempt"]}


def _consume_result_record(value, row_ordinals, row_by_id, rounds, pairs,
                           start_record, seen, measurement_digest, sample_db=None):
    """Validate one streamed #615 record without retaining the population.

    ``start_record`` and ``seen`` form a predeclared contiguous partition.  A
    globally disjoint and complete row/round/pair join therefore needs only
    O(number-of-canonical-rows) memory, never a set of tens of millions of
    coordinates.
    """
    value = _keys(value, ("record_id", "row", "round", "pair", "measurements"),
                  "result-input record")
    if type(value["row"]) is not int or type(value["round"]) is not int \
            or type(value["pair"]) is not int:
        _fail("result-input coordinates must be integers")
    if value["row"] not in row_ordinals \
            or not 0 <= value["round"] < rounds \
            or not 0 <= value["pair"] < pairs:
        _fail("result-input record is outside the complete eligible population")
    expected_id = f"row-{value['row']}/round-{value['round']}/pair-{value['pair']}"
    if value["record_id"] != expected_id:
        _fail("result-input record identity is not its frozen coordinate")
    linear = ((row_ordinals[value["row"]] * rounds + value["round"]) * pairs
              + value["pair"])
    if linear != start_record + seen[0]:
        _fail("result-input manifests are not disjoint contiguous partitions")
    expected_metrics = {metric for metric in METRICS
                        if row_by_id[value["row"]]["metrics"].get(metric, False)}
    measurements = _keys(value["measurements"], expected_metrics,
                          "result-input measurements")
    for metric in expected_metrics:
        pair_value = _keys(measurements[metric], ("baseline", "candidate"),
                           f"result-input measurements.{metric}")
        for side in ("baseline", "candidate"):
            sample = pair_value[side]
            if metric == "generated_code_bytes":
                minimum = 1 if side == "baseline" else 0
                if type(sample) is not int or sample < minimum or sample > (1 << 63) - 1:
                    _fail("generated code-byte observations require a positive baseline and "
                          "a nonnegative candidate")
                continue
            if type(sample) not in (int, float) or isinstance(sample, bool) \
                    or not math.isfinite(float(sample)) or sample <= 0:
                _fail("result-input measurements must be finite positive numbers")
    encoded = (json.dumps(value, sort_keys=True, separators=(",", ":"),
                          ensure_ascii=False) + "\n").encode("utf-8")
    measurement_digest.update(encoded)
    if sample_db is not None:
        for metric in expected_metrics:
            pair_value = value["measurements"][metric]
            sample_db.execute(
                "INSERT INTO samples(row_id, round_id, pair_id, metric, baseline, candidate) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                (value["row"], value["round"], value["pair"], metric,
                 str(pair_value["baseline"]), str(pair_value["candidate"])))
    seen[0] += 1


def _family_member_indexes(family):
    bootstrap_members = sorted(member for member in family["members"]
                               if member.endswith("/aggregate") or "/slice/" in member)
    cell_members = sorted(member for member in family["members"]
                          if "/cell/" in member)
    return ({member: ordinal for ordinal, member in enumerate(bootstrap_members)},
            {member: ordinal for ordinal, member in enumerate(cell_members)})


def _check_adapter_series(root, artifact, family, parsed, rules, sample_db):
    """Parse every C-adapter series and join ratios back to raw samples."""
    _check_evidence(root, artifact, "sealed_result_bundle.adapter_input")
    bootstrap_index, cell_index = _family_member_indexes(family)
    row_by_id = {row["row"]: row for row in parsed}
    expected_members = family["members"]
    rounds = rules["sampling"]["rounds"]
    pairs = rules["sampling"]["pairs_per_round"]
    expected_resamples = rules["sampling"]["resamples"]
    target = Path(root).resolve() / PurePosixPath(artifact["path"])
    try:
        stream = target.open(encoding="utf-8")
    except OSError as error:
        _fail(f"sealed_result_bundle.adapter_input is unreadable: {error}")
    with stream:
        line = stream.readline()
        header = re.fullmatch(
            r"version=(\d+) seed=(\d+) bootstrap_members=(\d+) cell_members=(\d+) "
            r"pairs=(\d+) resamples=(\d+) frozen=(\d+) members=(\d+)\n?", line)
        if not header:
            _fail("statistics adapter input header is malformed")
        version, seed, bootstrap_count, cell_count, input_pairs, input_resamples, frozen, members = map(
            int, header.groups())
        expected_counts = _family_member_counts(family)
        if (version != 1 or seed != rules["sampling"]["seed"]
                or bootstrap_count != expected_counts["bootstrap_members_per_scope"]
                or cell_count != expected_counts["cell_members_per_scope"]
                or input_pairs != pairs or input_resamples != expected_resamples
                or frozen != 1 or members != len(expected_members)):
            _fail("statistics adapter input header does not bind #619's frozen plan")
        seen_members = []
        for member_number in range(members):
            line = stream.readline()
            fields = line.rstrip("\n").split()
            if len(fields) != 8:
                _fail("statistics adapter member header is malformed")
            values = {}
            for field in fields:
                key, separator, value = field.partition("=")
                if not separator or key in values:
                    _fail("statistics adapter member header has duplicate/malformed fields")
                values[key] = value
            if set(values) != {"member", "metric", "kind", "family", "cells",
                                "pairs", "resamples", "limit"}:
                _fail("statistics adapter member header fields differ")
            member = values["member"]
            _adapter_member(member, "statistics adapter member")
            metric_name = member.split("/", 1)[0]
            if member not in expected_members or metric_name not in STATISTICAL_METRICS:
                _fail("statistics adapter names an unbound family member")
            if seen_members and member <= seen_members[-1]:
                _fail("statistics adapter family members are not unique and sorted")
            seen_members.append(member)
            try:
                metric_index = int(values["metric"])
                kind = int(values["kind"])
                family_index = int(values["family"])
                cells = int(values["cells"])
                member_pairs = int(values["pairs"])
                member_resamples = int(values["resamples"])
                limit = Decimal(values["limit"])
            except (ValueError, InvalidOperation):
                _fail("statistics adapter member header contains invalid numbers")
            is_cell = "/cell/" in member
            index_map = cell_index if is_cell else bootstrap_index
            expected_index = index_map[member]
            expected_kind = 1 if is_cell else 0
            expected_limit = CELL_THRESHOLDS[metric_name] if is_cell else AGGREGATE_THRESHOLDS[metric_name]
            if (metric_index != STATISTICAL_METRICS.index(metric_name)
                    or kind != expected_kind or family_index != expected_index
                    or member_pairs != pairs
                    or member_resamples != (0 if is_cell else expected_resamples)
                    or limit != expected_limit):
                _fail("statistics adapter member header changes #619's member map or limit")
            if is_cell:
                try:
                    row_id = int(member.rsplit("=", 1)[1])
                except (ValueError, IndexError):
                    _fail("statistics adapter cell identity is malformed")
                selected_rows = [row_id]
            elif member.endswith("/aggregate"):
                selected_rows = sorted(row["row"] for row in parsed
                                       if row["metrics"].get(metric_name, False))
            else:
                dimension, value = member.split("/slice/", 1)[1].split("=", 1)
                if dimension not in STATISTICAL_DIMENSIONS:
                    _fail("statistics adapter slice dimension is not approved")
                selected_rows = sorted(row["row"] for row in parsed
                                       if row["metrics"].get(metric_name, False)
                                       and str(row["identity"][dimension]) == value)
            if cells != len(selected_rows) or not selected_rows:
                _fail("statistics adapter member cell count differs from eligibility")
            for row_id in selected_rows:
                if row_id not in row_by_id or not row_by_id[row_id]["metrics"].get(metric_name, False):
                    _fail("statistics adapter member includes an ineligible row")
                expected = sample_db.execute(
                    "SELECT round_id, pair_id, baseline, candidate FROM samples "
                    "WHERE row_id=? AND metric=? ORDER BY round_id, pair_id",
                    (row_id, metric_name)).fetchall()
                if len(expected) != rounds * pairs:
                    _fail("statistics adapter member is missing raw samples")
                for round_id, pair_id, baseline_text, candidate_text in expected:
                    if metric_name == "generated_code_bytes":
                        try:
                            baseline = int(baseline_text)
                            candidate = int(candidate_text)
                        except (TypeError, ValueError):
                            _fail("raw code-byte observations are not integers")
                        if baseline <= 0 or candidate <= 0 \
                                or baseline > (1 << 63) - 1 \
                                or candidate > (1 << 63) - 1:
                            _fail("raw code-byte observations exceed the bounded integer domain")
                    else:
                        try:
                            baseline = float(baseline_text)
                            candidate = float(candidate_text)
                        except (TypeError, ValueError):
                            _fail("raw sample observations are not numeric")
                    ratio_line = stream.readline()
                    ratio_match = re.fullmatch(r"ratio=([^\s]+)\n?", ratio_line)
                    if not ratio_match:
                        _fail("statistics adapter ratio line is malformed")
                    try:
                        ratio = float(ratio_match.group(1))
                    except ValueError:
                        _fail("statistics adapter ratio is not numeric")
                    try:
                        expected_ratio = candidate / baseline
                    except (OverflowError, ZeroDivisionError):
                        _fail("raw sample ratio cannot be represented")
                    if not math.isfinite(ratio) or ratio <= 0 or ratio != expected_ratio:
                        _fail("statistics adapter ratio differs from raw candidate/baseline observations")
            if stream.readline() != "end\n":
                _fail("statistics adapter member does not terminate at its declared cells")
        if stream.readline():
            _fail("statistics adapter input has trailing unbound members")
    if seen_members != expected_members:
        _fail("statistics adapter input does not cover the dense family exactly once")


def _code_bytes_summary(parsed, sample_db, rounds, pairs):
    rows = [row for row in parsed if row["metrics"].get("generated_code_bytes", False)]
    ratios = []
    baseline_total = 0
    candidate_total = 0
    per_cell_pass = True
    for row in rows:
        values = sample_db.execute(
            "SELECT baseline, candidate FROM samples WHERE row_id=? AND metric=? "
            "ORDER BY round_id, pair_id", (row["row"], "generated_code_bytes")).fetchall()
        if len(values) != rounds * pairs:
            _fail("code-byte summary is missing required raw samples")
        try:
            first = (int(values[0][0]), int(values[0][1]))
        except (TypeError, ValueError):
            _fail("code-byte observations are not bounded integers")
        for baseline_text, candidate_text in values:
            try:
                pair = (int(baseline_text), int(candidate_text))
            except (TypeError, ValueError):
                _fail("code-byte observations are not bounded integers")
            if pair != first:
                _fail("deterministic code-byte observations vary across pairs")
        baseline_total += first[0]
        candidate_total += first[1]
        per_cell_pass = per_cell_pass and first[1] * 100 <= first[0] * 101
        ratios.append({"row": row["row"], "ratio": first[1] / first[0]})
    summary = {
        "rows": len(rows),
        "aggregate_ratio": float(Decimal(candidate_total) / Decimal(baseline_total)),
        "per_cell_max_ratio": max(item["ratio"] for item in ratios),
        "aggregate_pass": candidate_total * 100 <= baseline_total * 101,
        "per_cell_pass": per_cell_pass,
        "ratios_sha256": _canonical_json_digest(ratios),
    }
    return summary


def _compile_trusted_retirement_adapter(temp_root, repository_root=None,
                                        expected_commit=None, expected_tree=None):
    """Build the reviewed in-tree statistics adapter in a private directory.

    Evidence may describe a harness binary, but it must never cause this
    validator to execute that binary.  Replay uses only the checked-in
    ``throughput.c``/``shared.c`` sources from the independently identified
    repository checkout.  When the source identity is supplied, the checkout
    must be clean and its exact commit/tree must match before any source is
    read or compiled.  The resulting executable digest is returned so the
    replay bundle can bind the exact executable that actually produced its
    bytes.
    """
    if (expected_commit is None) != (expected_tree is None):
        _fail("trusted #619 adapter source identity must include commit and tree")
    source_root = Path(repository_root).resolve() if repository_root is not None \
        else Path(__file__).resolve().parents[1]
    if expected_commit is not None:
        _commit(expected_commit, "trusted #619 adapter source commit")
        _commit(expected_tree, "trusted #619 adapter source tree")
        # Evidence is deliberately materialized beside the checkout before
        # replay.  Untracked evidence cannot affect this build because every
        # source byte is read from the verified commit with ``git cat-file``;
        # tracked index/worktree drift still fails closed.
        status = _git_run(source_root, ["status", "--porcelain",
                                        "--untracked-files=no"],
                          "trusted #619 adapter checkout status")
        if status:
            _fail("trusted #619 adapter checkout must be clean")
        actual_commit = _git_run(source_root, ["rev-parse", "HEAD"],
                                 "trusted #619 adapter checkout commit")
        actual_tree = _git_run(source_root, ["rev-parse", "HEAD^{tree}"],
                               "trusted #619 adapter checkout tree")
        if actual_commit != expected_commit or actual_tree != expected_tree:
            _fail("trusted #619 adapter checkout does not match the bound source identity")
    verified_source = expected_commit is not None
    blob_cache = {}

    def committed_blob(relative):
        """Read one source blob from the already verified immutable commit."""
        key = relative.as_posix()
        if key in blob_cache:
            return blob_cache[key]
        revision_path = f"{expected_commit}:{key}"
        command = ["git", "-C", str(source_root), "cat-file", "-t", revision_path]
        try:
            type_process = subprocess.run(command, check=False, capture_output=True,
                                          text=True, timeout=10,
                                          env=_git_environment())
        except (OSError, subprocess.SubprocessError) as error:
            _fail(f"cannot inspect trusted #619 source blob {key}: {error}")
        if type_process.returncode != 0 or type_process.stdout.strip() != "blob":
            blob_cache[key] = None
            return None
        blob = _git_blob(source_root, revision_path,
                         f"trusted #619 source blob {key}")
        blob_cache[key] = blob
        return blob

    def working_tree_blob(relative):
        path = source_root.joinpath(*relative.parts)
        if path.is_symlink():
            _fail(f"trusted #619 adapter source is a symbolic link: {relative}")
        if not path.is_file():
            return None
        try:
            return path.read_bytes()
        except OSError as error:
            _fail(f"trusted #619 adapter source is unreadable: {error}")

    def source_blob(relative):
        blob = committed_blob(relative) if verified_source else working_tree_blob(relative)
        if blob is None and relative in initial_sources:
            _fail(f"trusted #619 adapter source is unavailable: {relative}")
        return blob

    initial_sources = {PurePosixPath("tools", "throughput", name)
                       for name in ("throughput.c", "shared.c")}
    source_files = []
    pending = list(initial_sources)
    include_re = re.compile(r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]")
    seen = set()
    while pending:
        relative = pending.pop()
        if relative in seen:
            continue
        seen.add(relative)
        data = source_blob(relative)
        if data is None:
            # System headers are intentionally outside the reviewed source
            # closure; every repository-local include is resolved below.
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as error:
            _fail(f"trusted #619 adapter source is unreadable: {error}")
        source_files.append((relative, data))
        for line in text.splitlines():
            match = include_re.match(line)
            if not match:
                continue
            include = match.group(1)
            if "\\" in include:
                continue
            candidates = [relative.parent / include, PurePosixPath(include),
                          PurePosixPath("src") / include]
            for candidate in candidates:
                if candidate.is_absolute() or ".." in candidate.parts:
                    continue
                if source_blob(candidate) is not None:
                    pending.append(candidate)
                    break
    source_files.sort(key=lambda item: item[0].as_posix())
    source_descriptors = []
    for relative, data in source_files:
        relative_name = relative.as_posix()
        source_descriptors.append({
            "name": relative_name, "path": relative_name, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    source_digest = _canonical_files_digest(source_descriptors)
    compile_root = source_root
    if verified_source:
        compile_root = Path(temp_root) / "trusted-source-closure"
        for relative, data in source_files:
            target = compile_root.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    compiler_name = shutil.which("clang") or shutil.which("cc")
    if compiler_name is None:
        _fail("no trusted C compiler is available for #619 adapter replay")
    # Preserve argv[0] for command-name-sensitive compiler dispatchers.
    # Reading bytes follows a symlink, but invoking its target may run a
    # different command (for example, swiftly instead of clang).
    compiler = Path(os.path.abspath(compiler_name))
    try:
        compiler_bytes = compiler.read_bytes()
        version_process = subprocess.run([str(compiler), "--version"], check=False,
                                         capture_output=True)
    except OSError as error:
        _fail(f"trusted #619 compiler is unreadable: {error}")
    if version_process.returncode != 0:
        _fail("trusted #619 compiler version query failed")
    compile_flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fwrapv",
                     "-fno-strict-aliasing", "-funsigned-char", "-Isrc",
                     "-DBUSTER_SINGLE_THREADED=1", "tools/throughput/throughput.c",
                     "tools/throughput/shared.c", "-lm"]
    build_command = " ".join([compiler.name, *compile_flags])
    toolchain_digest = _canonical_json_digest({
        "compiler_name": compiler.name,
        "compiler_sha256": hashlib.sha256(compiler_bytes).hexdigest(),
        "version_sha256": hashlib.sha256(version_process.stdout + version_process.stderr).hexdigest(),
        "build_command": build_command,
    })
    executable = Path(temp_root) / "bench_throughput-retirement-replay"
    command = [str(compiler), *compile_flags, "-o", str(executable)]
    process = subprocess.run(command, check=False, capture_output=True, text=True,
                             cwd=compile_root)
    if process.returncode != 0 or not executable.is_file() or executable.is_symlink():
        _fail("trusted #619 adapter compilation failed")
    digest = hashlib.sha256()
    with executable.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return executable, digest.hexdigest(), source_digest, toolchain_digest, build_command


def _sealed_closure_files(root, binding, support_output, records, phases,
                          result_plan, result_bundle_descriptor, result_bundle,
                          adapter_result):
    """Derive the exact pre-replay closure that a sealed result must contain."""
    files = []

    def add(name, artifact):
        if type(artifact) is not dict or any(field not in artifact
                                             for field in ("path", "bytes", "sha256")):
            _fail(f"{name} is missing its artifact descriptor")
        descriptor = {field: artifact[field] for field in ("path", "bytes", "sha256")}
        _artifact(descriptor, name)
        files.append({"name": name, **descriptor})

    # Retain every pre-replay identity already required by the binding: the
    # contract, #508 closure, both subjects/builds, producer/toolchain,
    # trusted measurement data, host/service receipts and provenance.  The
    # helper's stable names also make the archive closure reviewable without
    # accepting a caller-selected subset.
    add("contract.source", binding["contract"]["source"])
    for name, artifact in _all_artifacts(binding):
        if name in {"workflow.phases.sealed_result",
                    "workflow.phases.independent_replay"}:
            continue
        add(name, artifact)
    # _all_artifacts already includes the pre-sample/post-AA records.  The
    # outer sealed-result phase contains this seal, so it is deliberately
    # excluded above to avoid a digest cycle; the outer phase binds the seal
    # root and is added to the independent archive below.
    # ``phases`` is the workflow descriptor map, whose sealed-result entry is
    # only an artifact descriptor.  The parsed result-bundle descriptor is
    # passed separately and is the identity that belongs in the closure.
    for index, artifact in enumerate(support_output.get("projection_artifacts", [])):
        add(f"census.projection.{index}", artifact)
    add("workflow.result_bundle", result_bundle_descriptor)
    add("workflow.adapter_input", result_bundle["adapter_input"])
    add("workflow.adapter_result", adapter_result)
    pre = _read_json_evidence(root, phases["pre_sample_plan"], "pre_sample_plan")
    add("workflow.execution_plan", pre["execution_plan"])
    add("workflow.execution_receipt", result_bundle["execution_receipt"])
    execution_receipt = _read_json_evidence(
        root, result_bundle["execution_receipt"], "execution_receipt")
    for index, shard in enumerate(execution_receipt["shards"]):
        add(f"execution.shard.{index}", shard)
    for index, item in enumerate(result_bundle["result_manifests"]):
        add(f"result_input.manifest.{item['identity']}",
            {key: item[key] for key in ("path", "bytes", "sha256")})
        manifest = _read_json_evidence(
            root, {"path": item["path"], "bytes": item["bytes"],
                   "sha256": item["sha256"]},
            f"result_input_plan.manifests[{index}]")
        manifest = _keys(manifest, ("schema", "version", "identity_field", "shards"),
                         f"result_input_plan.manifests[{index}]")
        if manifest["schema"] != RESULT_INPUT.MANIFEST_SCHEMA \
                or manifest["version"] != 1 or manifest["identity_field"] != "record_id":
            _fail("result-input manifest schema differs from #615")
        shards = _list(manifest["shards"],
                       f"result_input_plan.manifests[{index}].shards")
        for shard_index, shard in enumerate(shards):
            shard = _keys(shard, ("identity", "path", "bytes", "sha256"),
                          f"result_input_plan.manifests[{index}].shards[{shard_index}]")
            _artifact({key: shard[key] for key in ("path", "bytes", "sha256")},
                      f"result_input_plan.manifests[{index}].shards[{shard_index}]")
            add(f"result_input.shard.{shard['identity']}",
                {key: shard[key] for key in ("path", "bytes", "sha256")})
    paths = [item["path"] for item in files]
    names = [item["name"] for item in files]
    if len(set(paths)) != len(paths) or len(set(names)) != len(names):
        _fail("sealed result closure contains duplicate artifact identities")
    return sorted(files, key=lambda item: item["name"])


def _open_verified_archive(root, artifact, name):
    """Open, hash and rewind one archive descriptor on the same owned fd.

    The initial descriptor check is useful for ordinary diagnostics, but it is
    not a byte-binding primitive: a path can be replaced between that check
    and ``tarfile.open``.  Hashing the descriptor after opening it (and then
    handing that exact stream to tarfile) closes that replacement window.
    """
    root = Path(root).resolve()
    relative = PurePosixPath(artifact["path"])
    archive_path = root.joinpath(*relative.parts)
    try:
        archive_path.resolve().relative_to(root)
    except ValueError:
        _fail(f"{name}.path escapes evidence root")
    cursor = root
    for part in relative.parts:
        cursor /= part
        if cursor.is_symlink():
            _fail(f"{name}.path contains a symbolic link")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    file_descriptor = None
    source_stream = None
    snapshot = None
    try:
        file_descriptor = os.open(str(archive_path), flags)
        source_stream = os.fdopen(file_descriptor, "rb")
        file_descriptor = None
        metadata = os.fstat(source_stream.fileno())
        if not stat.S_ISREG(metadata.st_mode):
            _fail(f"{name} is not a regular file")
        if metadata.st_size != artifact["bytes"]:
            _fail(f"{name} byte count does not match the opened archive")
        snapshot = tempfile.TemporaryFile(mode="w+b")
        digest = hashlib.sha256()
        total = 0
        for chunk in iter(lambda: source_stream.read(1024 * 1024), b""):
            total += len(chunk)
            if total > artifact["bytes"]:
                _fail(f"{name} grew while its bytes were being verified")
            snapshot.write(chunk)
            digest.update(chunk)
        if total != artifact["bytes"]:
            _fail(f"{name} was truncated while its bytes were being verified")
        if digest.hexdigest() != artifact["sha256"]:
            _fail(f"{name} digest does not match the opened archive")
        source_stream.close()
        source_stream = None
        snapshot.seek(0)
        return snapshot
    except OSError as error:
        if source_stream is not None:
            source_stream.close()
        elif file_descriptor is not None:
            os.close(file_descriptor)
        if snapshot is not None:
            snapshot.close()
        _fail(f"{name} cannot be opened as a regular archive: {error}")
    except Exception:
        if source_stream is not None:
            source_stream.close()
        elif file_descriptor is not None:
            os.close(file_descriptor)
        if snapshot is not None:
            snapshot.close()
        raise


def _extract_downloaded_bundle(root, bundle_artifact, publication_id, expected_files,
                               temp_root):
    """Own and stream the independent replay archive."""
    _check_evidence(root, bundle_artifact,
                    "independent_replay_bundle.downloaded_bundle")
    max_archive_bytes = sum(item["bytes"] for item in expected_files) + 64 * 1024 * 1024
    if bundle_artifact["bytes"] > max_archive_bytes:
        _fail("independent replay archive exceeds its predeclared closure bound")
    archive_stream = None
    archive = None
    try:
        archive_stream = _open_verified_archive(
            root, bundle_artifact, "independent_replay_bundle.downloaded_bundle")
        archive = tarfile.open(fileobj=archive_stream, mode="r|*")
        return _extract_downloaded_bundle_stream(
            archive, publication_id, expected_files, temp_root, max_archive_bytes)
    except (tarfile.TarError, OSError) as error:
        _fail(f"independent replay downloaded bundle is not a tar archive: {error}")
    finally:
        if archive is not None:
            archive.close()
        if archive_stream is not None:
            archive_stream.close()


def _extract_downloaded_bundle_stream(archive, publication_id, expected_files,
                                      temp_root, max_archive_bytes):
    """Parse one archive stream without retaining its members in memory."""
    try:
        member = archive.next()
        if member is None or member.name != "bundle-manifest.json" \
                or not member.isfile() or member.issym() or member.islnk():
            _fail("independent replay archive must begin with bundle-manifest.json")
        manifest_file = archive.extractfile(member)
        if manifest_file is None or member.size > 16 * 1024 * 1024:
            _fail("independent replay bundle manifest is unavailable or too large")
        manifest_bytes = manifest_file.read()
        if len(manifest_bytes) != member.size:
            _fail("independent replay bundle manifest size is inconsistent")
        try:
            manifest = json.loads(manifest_bytes.decode("utf-8"),
                                  object_pairs_hook=_json_object)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            _fail(f"independent replay bundle manifest is invalid: {error}")
    except (tarfile.TarError, OSError) as error:
        _fail(f"independent replay archive stream failed: {error}")
    manifest = _keys(manifest, ("schema", "version", "publication_id", "files",
                                "root_sha256"), "independent_replay_bundle.manifest")
    if manifest["schema"] != "buster-native-retirement-independent-bundle-manifest-v1" \
            or manifest["version"] != 1 \
            or manifest["publication_id"] != publication_id:
        _fail("independent replay bundle manifest identity is not bound")
    listed = []
    for index, item in enumerate(_list(manifest["files"],
                                      "independent_replay_bundle.manifest.files")):
        item = _keys(item, ("path", "bytes", "sha256"),
                     f"independent_replay_bundle.manifest.files[{index}]")
        _artifact(item, f"independent_replay_bundle.manifest.files[{index}]")
        if item["path"] == "bundle-manifest.json":
            _fail("independent replay manifest must not list itself")
        listed.append(item)
    if len({item["path"] for item in listed}) != len(listed):
        _fail("independent replay manifest lists duplicate paths")
    expected = {item["path"]: item for item in expected_files}
    if set(expected) != {item["path"] for item in listed}:
        _fail("independent replay archive does not contain the sealed closure exactly")
    listed_by_path = {item["path"]: item for item in listed}
    for path, expected_item in expected.items():
        item = listed_by_path[path]
        if (item["path"], item["bytes"], item["sha256"]) != \
                (expected_item["path"], expected_item["bytes"], expected_item["sha256"]):
            _fail("independent replay archive item differs from the sealed closure")
    canonical = _canonical_files_digest(
        [{"name": item["path"], **item} for item in listed])
    if manifest["root_sha256"] != canonical:
        _fail("independent replay archive manifest root digest is incorrect")
    extracted_root = Path(temp_root) / "downloaded"
    extracted_root.mkdir(parents=True)
    seen = set()
    total_bytes = len(manifest_bytes)
    try:
        while True:
            member = archive.next()
            if member is None:
                break
            path = PurePosixPath(member.name)
            if member.isdir() or member.issym() or member.islnk() \
                    or path.is_absolute() or ".." in path.parts \
                    or path.as_posix() != member.name or not member.isfile():
                _fail("independent replay archive contains an unsafe or non-file member")
            if member.name == "bundle-manifest.json" or member.name not in expected \
                    or member.name in seen:
                _fail("independent replay archive contains an unexpected or duplicate file")
            expected_item = expected[member.name]
            if member.size != expected_item["bytes"]:
                _fail("independent replay archive member size differs from its sealed descriptor")
            target = extracted_root.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            extracted = archive.extractfile(member)
            if extracted is None:
                _fail("independent replay archive member is unreadable")
            written = 0
            with target.open("wb") as output:
                for chunk in iter(lambda: extracted.read(1024 * 1024), b""):
                    written += len(chunk)
                    total_bytes += len(chunk)
                    if total_bytes > max_archive_bytes:
                        _fail("independent replay archive stream exceeds its closure bound")
                    digest.update(chunk)
                    output.write(chunk)
            if written != member.size or digest.hexdigest() != expected_item["sha256"]:
                _fail("independent replay archive member digest differs from its sealed descriptor")
            seen.add(member.name)
    except (tarfile.TarError, OSError) as error:
        _fail(f"independent replay archive stream failed: {error}")
    if seen != set(expected):
        _fail("independent replay archive omitted a sealed closure file")
    return extracted_root


def _check_workflow_evidence(root, binding, workflow, support_output, row_data,
                             population, rules, repository_root=None,
                             trusted_execution_receipt_sha256=None,
                             admitted_cpu=None, native_target=None):
    """Own the streamed sample index for the complete workflow check.

    ``TemporaryDirectory`` and ``sqlite3.Connection`` are both explicit
    resources here.  In particular, an invalid result-input manifest must not
    leave either resource for the interpreter finalizer: CI runs this
    validator with ``ResourceWarning`` promoted to an error.
    """
    with tempfile.TemporaryDirectory(prefix="retirement-sample-index-") as sample_store:
        with closing(sqlite3.connect(str(Path(sample_store) / "samples.sqlite3"))) as sample_db:
            sample_db.execute("PRAGMA journal_mode=OFF")
            sample_db.execute("PRAGMA synchronous=OFF")
            sample_db.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                              "metric TEXT, baseline TEXT, candidate TEXT, "
                              "PRIMARY KEY(row_id, round_id, pair_id, metric))")
            return _check_workflow_evidence_open(root, binding, workflow, support_output,
                                                 row_data, population, rules, sample_db,
                                                 repository_root,
                                                 trusted_execution_receipt_sha256,
                                                 admitted_cpu, native_target)


def _check_workflow_evidence_open(root, binding, workflow, support_output, row_data,
                                  population, rules, sample_db, repository_root=None,
                                  trusted_execution_receipt_sha256=None,
                                  admitted_cpu=None, native_target=None):
    """Check the four non-circular workflow artifacts and their joins.

    The phase records deliberately carry only identities and frozen plan
    references.  They do not contain a verdict: structural completion is
    evidence that the workflow reached a phase, while acceptance remains a
    later statistical decision over the sealed result.
    """
    phases = workflow["phases"]
    records = workflow["records"]
    parsed, _axes, family = row_data[:3]
    sampling = rules["sampling"]

    result_plan = _result_input_plan(root, records["result_input_plan"],
                                     support_output, population, rules)
    sampled_rows = sorted(row["row"] for row in parsed
                          if any(row["metrics"].get(metric, False)
                                 for metric in METRICS))
    row_by_id = {row["row"]: row for row in parsed}
    row_ordinals = {row: ordinal for ordinal, row in enumerate(sampled_rows)}
    expected_records = len(sampled_rows) * result_plan["rounds"] * result_plan["pairs_per_round"]
    if result_plan["sample_row_count"] != len(sampled_rows):
        _fail("result-input plan does not cover every eligible canonical row")
    if expected_records != result_plan["required_records"]:
        _fail("result-input plan population is not the complete eligible row set")
    if RESULT_INPUT is None:
        _fail("#615 result-input verifier is unavailable")

    # The pre-sample plan fixes only the partition identities and coordinate
    # ranges.  Manifest/shard bytes do not exist until measurements finish,
    # so their authenticated descriptors belong to the sealed result.
    sealed = _workflow_phase(root, phases["sealed_result"], SEALED_RESULT_SCHEMA,
                             "sealed_result")
    sealed = _keys(sealed, ("schema", "version", "status",
                            "post_aa_binding_sha256", "result_input_plan_sha256",
                            "family_sha256", "result_bundle", "seal"),
                   "workflow.sealed_result")
    if sealed["status"] != "sealed-for-independent-replay":
        _fail("workflow.sealed_result must be sealed for independent replay")
    if sealed["post_aa_binding_sha256"] != phases["post_aa_binding"]["sha256"]:
        _fail("workflow.sealed_result does not bind post-AA planning")
    if sealed["result_input_plan_sha256"] != records["result_input_plan"]["sha256"]:
        _fail("workflow.sealed_result does not bind the result-input plan")
    if sealed["family_sha256"] != family["sha256"]:
        _fail("workflow.sealed_result does not bind the statistical family")
    _artifact(sealed["result_bundle"], "workflow.sealed_result.result_bundle")
    _check_evidence(root, sealed["result_bundle"], "workflow.sealed_result.result_bundle")
    result_bundle = _read_json_evidence(root, sealed["result_bundle"],
                                        "workflow.sealed_result.result_bundle")
    result_bundle = _keys(result_bundle, (
        "schema", "version", "source_rows_sha256", "result_input_plan_sha256",
        "family_sha256", "result_manifests", "raw_measurements_sha256",
        "member_invocations_sha256", "member_count", "scopes_per_member",
        "adapter_input", "code_bytes_summary", "execution_receipt"), "sealed_result_bundle")
    if result_bundle["schema"] != RESULT_BUNDLE_SCHEMA or result_bundle["version"] != 1:
        _fail("sealed result bundle schema/version is not approved")
    if result_bundle["source_rows_sha256"] != support_output["rows_sha256"] \
            or result_bundle["result_input_plan_sha256"] != records["result_input_plan"]["sha256"] \
            or result_bundle["family_sha256"] != family["sha256"]:
        _fail("sealed result bundle does not bind the frozen inputs")
    result_manifests = _result_manifest_descriptors(
        result_bundle["result_manifests"], result_plan)

    measurement_digest = hashlib.sha256()
    for index, item in enumerate(result_manifests):
        manifest_descriptor = {"path": item["path"], "bytes": item["bytes"],
                               "sha256": item["sha256"]}
        _check_evidence(root, manifest_descriptor,
                        f"result_input_plan.manifests[{index}]")

        seen = [0]

        def consume(_shard_identity, _shard_record, value):
            _consume_result_record(value, row_ordinals, row_by_id,
                                    result_plan["rounds"], result_plan["pairs_per_round"],
                                    item["start_record"], seen, measurement_digest, sample_db)

        try:
            receipt = RESULT_INPUT.verify(root, item["path"],
                                         RESULT_INPUT.Limits(
                                             max_records=RESULT_INPUT_MAX_RECORDS),
                                         record_consumer=consume)
        except Exception as error:
            _fail(f"#615 result-input manifest verification failed: {error}")
        if receipt["manifest"]["path"] != item["path"] \
                or receipt["manifest"]["bytes"] <= 0 \
                or receipt["records"] != item["records"] \
                or receipt["input_bytes"] != item["input_bytes"] \
                or seen[0] != item["records"]:
            _fail("sealed result manifest count/digest differs from #615's streamed receipt")
    if sum(item["records"] for item in result_plan["manifests"]) != expected_records:
        _fail("result-input manifests omit required row/round/pair coordinates")
    sample_db.commit()

    expected_sources = {
        "support_declaration_sha256": support_output["support_declaration_sha256"],
        "manifest_sha256": support_output["manifest_sha256"],
        "rows_sha256": support_output["rows_sha256"],
        "family_sha256": family["sha256"],
        "seed": sampling["seed"],
        "rounds": sampling["rounds"],
        "pairs_per_round": sampling["pairs_per_round"],
        "resamples": sampling["resamples"],
        "bootstrap_members_per_scope": sampling["bootstrap_members_per_scope"],
        "cell_members_per_scope": sampling["cell_members_per_scope"],
        "result_input_plan_sha256": records["result_input_plan"]["sha256"],
    }

    pre = _workflow_phase(root, phases["pre_sample_plan"],
                          PHASE_SCHEMA["pre_sample_plan"], "pre_sample_plan")
    pre = _keys(pre, ("schema", "version", "status",
                      "support_declaration_sha256", "manifest_sha256", "rows_sha256",
                      "family_sha256", "seed", "rounds", "pairs_per_round", "resamples",
                      "bootstrap_members_per_scope", "cell_members_per_scope",
                      "result_input_plan_sha256", "execution_plan"), "workflow.pre_sample_plan")
    if pre["status"] != "frozen-before-samples":
        _fail("workflow.pre_sample_plan must be frozen before samples")
    for field, expected in expected_sources.items():
        if pre[field] != expected:
            _fail(f"workflow.pre_sample_plan.{field} does not bind the frozen plan")

    post = _workflow_phase(root, phases["post_aa_binding"],
                           PHASE_SCHEMA["post_aa_binding"], "post_aa_binding")
    post = _keys(post, ("schema", "version", "status",
                        "support_declaration_sha256", "manifest_sha256", "rows_sha256",
                        "family_sha256", "seed", "rounds", "pairs_per_round", "resamples",
                        "bootstrap_members_per_scope", "cell_members_per_scope",
                        "result_input_plan_sha256", "pre_sample_plan_sha256",
                        "aa_admission_sha256", "execution_plan"), "workflow.post_aa_binding")
    if post["status"] != "bound-after-aa-before-samples":
        _fail("workflow.post_aa_binding must be bound after AA and before samples")
    for field, expected in expected_sources.items():
        if post[field] != expected:
            _fail(f"workflow.post_aa_binding.{field} does not bind the frozen plan")
    if post["pre_sample_plan_sha256"] != phases["pre_sample_plan"]["sha256"]:
        _fail("workflow.post_aa_binding does not bind pre-sample planning")
    aa_artifact = binding["execution"]["host"]["aa_admission_receipt"]
    if post["aa_admission_sha256"] != aa_artifact["sha256"]:
        _fail("workflow.post_aa_binding does not bind the admitted AA receipt")

    if result_bundle["raw_measurements_sha256"] != measurement_digest.hexdigest():
        _fail("sealed result bundle does not bind streamed measurement bytes")
    if result_bundle["member_invocations_sha256"] != _family_invocation_digest(family):
        _fail("sealed result bundle does not bind the scope-free #619 call map")
    if result_bundle["member_count"] != len(family["members"]):
        _fail("sealed result bundle member count differs from the derived family")
    if result_bundle["scopes_per_member"] != len(STATISTICAL_SCOPES):
        _fail("sealed result bundle must emit all three #619 scopes per call")
    _artifact(result_bundle["adapter_input"], "sealed_result_bundle.adapter_input")
    _check_evidence(root, result_bundle["adapter_input"],
                    "sealed_result_bundle.adapter_input")
    code_summary = _code_bytes_summary(parsed, sample_db,
                                       result_plan["rounds"], result_plan["pairs_per_round"])
    if result_bundle["code_bytes_summary"] != code_summary:
        _fail("sealed result bundle code-byte summary differs from raw observations")
    if post["execution_plan"] != pre["execution_plan"]:
        _fail("post-AA execution plan differs from its pre-sample plan")
    _check_execution_transcript(
        root, result_bundle["execution_receipt"], pre["execution_plan"], binding,
        parsed, sampling, sample_db, measurement_digest.hexdigest(),
        trusted_execution_receipt_sha256, admitted_cpu, native_target)
    _check_adapter_series(root, result_bundle["adapter_input"], family, parsed,
                          rules, sample_db)
    seal = _keys(sealed["seal"], ("schema", "version", "files", "root_sha256"),
                 "workflow.sealed_result.seal")
    if seal["schema"] != "buster-native-retirement-result-seal-v1" \
            or seal["version"] != 1:
        _fail("workflow.sealed_result.seal schema/version is not approved")
    files = _list(seal["files"], "workflow.sealed_result.seal.files")
    if not files:
        _fail("workflow.sealed_result.seal.files must not be empty")
    seal_files = []
    for index, item in enumerate(files):
        item = _keys(item, ("name", "path", "bytes", "sha256"),
                     f"workflow.sealed_result.seal.files[{index}]")
        _string(item["name"], f"workflow.sealed_result.seal.files[{index}].name")
        _artifact({"path": item["path"], "bytes": item["bytes"],
                   "sha256": item["sha256"]},
                  f"workflow.sealed_result.seal.files[{index}]")
        _check_evidence(root, {"path": item["path"], "bytes": item["bytes"],
                               "sha256": item["sha256"]},
                        f"workflow.sealed_result.seal.files[{index}]")
        seal_files.append(item)
    _sha(seal["root_sha256"], "workflow.sealed_result.seal.root_sha256")
    if seal["root_sha256"] != _canonical_files_digest(seal_files):
        _fail("workflow.sealed_result.seal does not match sealed artifact bytes")

    independent = _workflow_phase(root, phases["independent_replay"],
                                  PHASE_SCHEMA["independent_replay"],
                                  "independent_replay")
    independent = _keys(independent, ("schema", "version", "status",
                                      "sealed_result_sha256", "replay_bundle",
                                      "publication_receipt"),
                         "workflow.independent_replay")
    if independent["status"] != "independently-replayed":
        _fail("workflow.independent_replay must record independent replay")
    if independent["sealed_result_sha256"] != phases["sealed_result"]["sha256"]:
        _fail("workflow.independent_replay does not bind the sealed result")
    _artifact(independent["replay_bundle"],
              "workflow.independent_replay.replay_bundle")
    _artifact(independent["publication_receipt"],
              "workflow.independent_replay.publication_receipt")
    _check_evidence(root, independent["publication_receipt"],
                    "workflow.independent_replay.publication_receipt")
    _check_evidence(root, independent["replay_bundle"],
                    "workflow.independent_replay.replay_bundle")
    replay_bundle = _read_json_evidence(root, independent["replay_bundle"],
                                        "workflow.independent_replay.replay_bundle")
    replay_bundle = _keys(replay_bundle, (
        "schema", "version", "sealed_result_sha256", "raw_measurements_sha256",
        "family_sha256", "member_invocations_sha256", "member_count",
        "adapter_command", "adapter_build_command", "adapter_toolchain_sha256",
        "adapter_source_sha256",
        "code_bytes_summary_sha256", "publication_id", "published_bundle_sha256",
        "downloaded_bundle_sha256", "downloaded_bundle", "adapter_result",
        "publication_receipt"),
        "independent_replay_bundle")
    if replay_bundle["schema"] != REPLAY_BUNDLE_SCHEMA or replay_bundle["version"] != 1:
        _fail("independent replay bundle schema/version is not approved")
    if replay_bundle["sealed_result_sha256"] != sealed["result_bundle"]["sha256"] \
            or replay_bundle["raw_measurements_sha256"] != measurement_digest.hexdigest() \
            or replay_bundle["family_sha256"] != family["sha256"] \
            or replay_bundle["member_invocations_sha256"] != _family_invocation_digest(family) \
            or replay_bundle["member_count"] != len(family["members"]):
        _fail("independent replay bundle does not bind the sealed inputs")
    if replay_bundle["adapter_command"] != "bench_throughput retirement-replay --input SERIES_FILE --output RESULT_JSON":
        _fail("independent replay did not use the approved C statistics adapter")
    _sha(replay_bundle["adapter_source_sha256"],
         "independent_replay_bundle.adapter_source_sha256")
    _single_line_string(replay_bundle["adapter_build_command"],
                        "independent_replay_bundle.adapter_build_command")
    _sha(replay_bundle["adapter_toolchain_sha256"],
         "independent_replay_bundle.adapter_toolchain_sha256")
    trusted_source_root = Path(repository_root).resolve() if repository_root is not None \
        else Path(__file__).resolve().parents[1]
    trusted_statistics_source = trusted_source_root / "tools" / "throughput" / "retirement_stats.h"
    if (binding["measurement"]["statistics_implementation"]["path"]
            != "tools/throughput/retirement_stats.h"):
        _fail("independent replay must bind the reviewed retirement_stats.h source")
    try:
        trusted_statistics_digest = hashlib.sha256(
            trusted_statistics_source.read_bytes()).hexdigest()
    except OSError as error:
        _fail(f"trusted #619 statistics source is unreadable: {error}")
    if trusted_statistics_digest != binding["measurement"]["statistics_implementation"]["sha256"]:
        _fail("bound retirement_stats.h differs from the trusted checkout")
    _sha(replay_bundle["code_bytes_summary_sha256"],
         "independent_replay_bundle.code_bytes_summary_sha256")
    if replay_bundle["code_bytes_summary_sha256"] != _canonical_json_digest(code_summary):
        _fail("independent replay code-byte summary is not the sealed summary")
    _token(replay_bundle["publication_id"],
           "independent_replay_bundle.publication_id")
    _sha(replay_bundle["published_bundle_sha256"],
         "independent_replay_bundle.published_bundle_sha256")
    _sha(replay_bundle["downloaded_bundle_sha256"],
         "independent_replay_bundle.downloaded_bundle_sha256")
    _artifact(replay_bundle["downloaded_bundle"],
              "independent_replay_bundle.downloaded_bundle")
    _check_evidence(root, replay_bundle["downloaded_bundle"],
                    "independent_replay_bundle.downloaded_bundle")
    if replay_bundle["published_bundle_sha256"] == sealed["result_bundle"]["sha256"]:
        _fail("independent replay publication must be a distinct immutable bundle")
    if replay_bundle["published_bundle_sha256"] != replay_bundle["downloaded_bundle_sha256"] \
            or replay_bundle["downloaded_bundle_sha256"] != replay_bundle["downloaded_bundle"]["sha256"]:
        _fail("independent replay downloaded bytes do not match the published immutable bundle")
    _artifact(replay_bundle["adapter_result"], "independent_replay_bundle.adapter_result")
    _check_evidence(root, replay_bundle["adapter_result"],
                    "independent_replay_bundle.adapter_result")
    _artifact(replay_bundle["publication_receipt"],
              "independent_replay_bundle.publication_receipt")
    if replay_bundle["publication_receipt"] != independent["publication_receipt"]:
        _fail("independent replay publication receipt is not phase-bound")
    _check_evidence(root, replay_bundle["publication_receipt"],
                    "independent_replay_bundle.publication_receipt")
    publication = _read_json_evidence(
        root, replay_bundle["publication_receipt"],
        "independent_replay_bundle.publication_receipt")
    publication = _keys(publication, (
        "schema", "version", "publisher", "release", "run_id", "service_id",
        "publication_id", "sealed_result_sha256", "published_bundle_sha256",
        "downloaded_bundle_sha256", "replay_result"),
        "performance_publication_receipt")
    if publication["schema"] != PUBLICATION_SCHEMA or publication["version"] != 1:
        _fail("performance publication receipt schema/version is not approved")
    for field in ("publisher", "release", "run_id", "service_id", "publication_id"):
        _token(publication[field], f"performance_publication_receipt.{field}")
    if publication["publisher"] != "native-retirement-evidence-v1" \
            or publication["service_id"] != binding["execution"]["service"]["id"]:
        _fail("performance publication receipt identity is not bound")
    _sha(publication["sealed_result_sha256"],
         "performance_publication_receipt.sealed_result_sha256")
    _sha(publication["published_bundle_sha256"],
         "performance_publication_receipt.published_bundle_sha256")
    _sha(publication["downloaded_bundle_sha256"],
         "performance_publication_receipt.downloaded_bundle_sha256")
    if (publication["publication_id"], publication["sealed_result_sha256"],
            publication["published_bundle_sha256"],
            publication["downloaded_bundle_sha256"]) != \
            (replay_bundle["publication_id"], sealed["result_bundle"]["sha256"],
             replay_bundle["published_bundle_sha256"],
             replay_bundle["downloaded_bundle_sha256"]):
        _fail("performance publication receipt does not bind the replay archive")
    if publication["replay_result"] != "independently-replayed":
        _fail("performance publication receipt does not record independent replay")
    expected_seal_files = _sealed_closure_files(
        root, binding, support_output, records, phases, result_plan,
        sealed["result_bundle"], result_bundle, replay_bundle["adapter_result"])
    if sorted(seal_files, key=lambda item: item["name"]) != expected_seal_files:
        _fail("workflow.sealed_result.seal does not enumerate the exact sealed closure")
    downloaded_files = expected_seal_files + [{
        "name": "workflow.phases.sealed_result",
        "path": phases["sealed_result"]["path"],
        "bytes": phases["sealed_result"]["bytes"],
        "sha256": phases["sealed_result"]["sha256"],
    }]
    with tempfile.TemporaryDirectory(prefix="retirement-independent-bundle-") as replay_root:
        extracted_root = _extract_downloaded_bundle(
            root, replay_bundle["downloaded_bundle"], replay_bundle["publication_id"],
            downloaded_files, replay_root)
        with tempfile.TemporaryDirectory(prefix="retirement-statistics-replay-") as adapter_dir:
            fresh_output = Path(adapter_dir) / "statistics-replay.json"
            (adapter_executable, _adapter_binary_digest, adapter_source_digest,
             adapter_toolchain_digest, adapter_build_command) = \
                _compile_trusted_retirement_adapter(
                    adapter_dir, trusted_source_root,
                    binding["measurement"]["harness_source_commit"],
                    binding["measurement"]["harness_source_tree"])
            if adapter_source_digest != replay_bundle["adapter_source_sha256"]:
                _fail("independent replay adapter source closure differs from the reviewed checkout")
            if adapter_toolchain_digest != replay_bundle["adapter_toolchain_sha256"] \
                    or adapter_build_command != replay_bundle["adapter_build_command"]:
                _fail("independent replay adapter toolchain/build recipe is not bound")
            trusted_input = extracted_root.joinpath(
                *PurePosixPath(result_bundle["adapter_input"]["path"]).parts)
            command = [str(adapter_executable),
                       "retirement-replay", "--input", str(trusted_input),
                       "--output", str(fresh_output)]
            replay_process = subprocess.run(command, check=False, capture_output=True,
                                            text=True, cwd=trusted_source_root)
            if replay_process.returncode != 0 or not fresh_output.is_file():
                _fail("#619 C statistics adapter replay failed")
            trusted_result = extracted_root.joinpath(
                *PurePosixPath(replay_bundle["adapter_result"]["path"]).parts)
            if fresh_output.read_bytes() != trusted_result.read_bytes():
                _fail("#619 C statistics adapter replay differs from independently downloaded output")
    adapter_result = _read_json_evidence(root, replay_bundle["adapter_result"],
                                         "independent_replay_bundle.adapter_result")
    adapter_result = _keys(adapter_result, ("schema", "version", "members"),
                           "independent_replay_bundle.adapter_result")
    if adapter_result["schema"] != "buster-native-retirement-statistics-replay-v1" \
            or adapter_result["version"] != 1:
        _fail("independent replay adapter output schema/version is not approved")
    calls = _list(adapter_result["members"],
                  "independent_replay_bundle.adapter_result.members")
    if len(calls) != len(family["members"]):
        _fail("independent replay adapter did not invoke every family member once")
    call_ids = []
    bootstrap_index, cell_index = _family_member_indexes(family)
    for index, call in enumerate(calls):
        call = _keys(call, ("member", "metric", "kind", "family_index", "outcome",
                             "valid", "resampled", "resamples", "tail_alpha",
                             "round", "pooled"),
                     f"independent_replay_bundle.adapter_result.members[{index}]")
        _string(call["member"], f"adapter_result.members[{index}].member")
        if call["member"] not in family["members"]:
            _fail("independent replay adapter named an unbound family member")
        if call["member"] in call_ids:
            _fail("independent replay adapter invoked a family member twice")
        call_ids.append(call["member"])
        metric_name = call["member"].split("/", 1)[0]
        if metric_name not in STATISTICAL_METRICS:
            _fail("independent replay adapter named an unknown metric member")
        expected_metric = STATISTICAL_METRICS.index(metric_name)
        is_cell = "/cell/" in call["member"]
        expected_index = cell_index if is_cell else bootstrap_index
        expected_kind = 1 if is_cell else 0
        if call["metric"] != expected_metric or call["kind"] != expected_kind \
                or call["family_index"] != expected_index[call["member"]]:
            _fail("independent replay adapter member index/kind differs from #619 map")
        if call["valid"] is not True:
            _fail("independent replay adapter emitted an invalid member result")
        _boolean(call["resampled"], f"adapter_result.members[{index}].resampled")
        if type(call["metric"]) is not int or type(call["kind"]) is not int \
                or type(call["family_index"]) is not int \
                or type(call["resamples"]) is not int:
            _fail("independent replay adapter member indexes/counts must be integers")
        if is_cell:
            if call["resampled"] or call["resamples"] != 0:
                _fail("exact-cell #619 calls must not claim bootstrap resampling")
        elif not call["resampled"] or call["resamples"] != sampling["resamples"]:
            _fail("bootstrap #619 calls must use the frozen resample count")
        expected_member_count = (sampling["cell_members_per_scope"] if is_cell
                                 else sampling["bootstrap_members_per_scope"])
        expected_tail_alpha = 0.05 / (2.0 * len(STATISTICAL_SCOPES) * 2.0
                                      * expected_member_count)
        if type(call["tail_alpha"]) not in (int, float) \
                or isinstance(call["tail_alpha"], bool) \
                or not math.isfinite(float(call["tail_alpha"])) \
                or call["tail_alpha"] != expected_tail_alpha:
            _fail("independent replay adapter tail alpha differs from #619 family correction")
        expected_limit = (CELL_THRESHOLDS[metric_name] if is_cell
                          else AGGREGATE_THRESHOLDS[metric_name])
        rounds = _list(call["round"], f"adapter_result.members[{index}].round")
        if len(rounds) != len(STATISTICAL_SCOPES) - 1:
            _fail("independent replay adapter must emit both round scopes")
        bounds = []
        for round_index, bound in enumerate(rounds):
            bound = _keys(bound, ("estimate", "lower", "upper"),
                          f"adapter_result.members[{index}].round[{round_index}]")
            bounds.append(bound)
        pooled = _keys(call["pooled"], ("estimate", "lower", "upper"),
                       f"adapter_result.members[{index}].pooled")
        bounds.append(pooled)
        for bound_index, bound in enumerate(bounds):
            for field in ("estimate", "lower", "upper"):
                number = bound[field]
                if type(number) not in (int, float) or isinstance(number, bool) \
                        or not math.isfinite(float(number)):
                    _fail(f"adapter_result.members[{index}] bound {bound_index}.{field} is not finite")
            if bound["estimate"] <= 0 or bound["lower"] < 0 \
                    or bound["upper"] <= 0 or bound["lower"] > bound["upper"]:
                _fail(f"adapter_result.members[{index}] has an invalid #619 bound")
        passed = all(bound["upper"] <= float(expected_limit) for bound in bounds)
        regressed = all(bound["lower"] > float(expected_limit) for bound in bounds)
        expected_outcome = "pass" if passed else "regression" if regressed else "inconclusive"
        if call["outcome"] != expected_outcome:
            _fail("independent replay adapter outcome is not derived from all #619 bounds")
    if sorted(call_ids) != family["members"]:
        _fail("independent replay adapter family ordering differs from the derived map")

    admission = _read_json_evidence(root, records["admission"],
                                    "workflow.records.admission")
    admission = _keys(admission, ("schema", "version", "source_manifest_sha256",
                                  "source_rows_sha256", "records"),
                      "workflow.records.admission")
    if admission["schema"] != ADMISSION_SCHEMA or admission["version"] != 1:
        _fail("admission record schema/version is not approved")
    if admission["source_manifest_sha256"] != support_output["manifest_sha256"] \
            or admission["source_rows_sha256"] != support_output["rows_sha256"]:
        _fail("admission records do not bind the actual schema-2 source artifacts")
    admission_records = _list(admission["records"], "workflow.records.admission.records")
    admission_by_row = {}
    object_admissions = 0
    for index, item in enumerate(admission_records):
        item = _keys(item, ("row", "census_row", "identity", "artifact_stage",
                            "requested_obligation", "status", "exit_code", "timed_out",
                            "native_compiler", "artifact_kind", "artifact_bytes",
                            "artifact_sha256"),
                     f"workflow.records.admission.records[{index}]")
        if type(item["row"]) is not int or not 0 <= item["row"] < len(parsed):
            _fail("admission record row is outside the canonical population")
        if item["row"] in admission_by_row:
            _fail("admission records duplicate a canonical row")
        if type(item["census_row"]) is not int \
                or not 0 <= item["census_row"] < len(support_output["rows"]):
            _fail("admission record census_row is outside schema-2 rows")
        if item["artifact_stage"] not in STAGES:
            _fail("admission record artifact_stage is not approved")
        identity = _row_identity(item["identity"],
                                 f"workflow.records.admission.records[{index}].identity")
        if identity["artifact_stage"] != item["artifact_stage"]:
            _fail("admission identity stage differs from its stage field")
        source = support_output["rows"][item["census_row"]]
        for field in ROW_IDENTITY_FIELDS:
            if field == "artifact_stage":
                continue
            if identity[field] != source[field]:
                _fail("admission identity is not derived from schema-2 rows")
        expected_compile = item["row"] in support_output["compiler_eligible_rows"]
        _boolean(item["timed_out"],
                 f"workflow.records.admission.records[{index}].timed_out")
        _boolean(item["native_compiler"],
                 f"workflow.records.admission.records[{index}].native_compiler")
        if expected_compile:
            if item["requested_obligation"] != "compiler-wall-time-and-peak-rss" \
                    or item["status"] != "completed" or item["exit_code"] != 0 \
                    or item["timed_out"] or not item["native_compiler"]:
                _fail("eligible admission record is not a successful native compiler execution")
            if item["artifact_kind"] not in {
                    "object", "linked-executable", "self-host-stage1"}:
                _fail("eligible admission record artifact kind is not approved")
            _positive_int(item["artifact_bytes"],
                          f"workflow.records.admission.records[{index}].artifact_bytes")
            _sha(item["artifact_sha256"],
                 f"workflow.records.admission.records[{index}].artifact_sha256")
            object_admissions += item["artifact_stage"] == "object"
        else:
            if item["requested_obligation"] is not None \
                    or item["status"] != "not-applicable" \
                    or item["exit_code"] is not None \
                    or item["timed_out"] or item["native_compiler"] \
                    or item["artifact_kind"] is not None \
                    or item["artifact_bytes"] is not None \
                    or item["artifact_sha256"] is not None:
                _fail("authenticated non-executed admission must use explicit null observations")
        admission_by_row[item["row"]] = item
    if set(admission_by_row) != set(range(len(parsed))):
        _fail("admission records do not cover every canonical performance row")
    if object_admissions != support_output["eligible_object_row_count"]:
        _fail("admission records do not cover every authenticated eligible object row")

    oracle = _read_json_evidence(root, records["oracle"], "workflow.records.oracle")
    oracle = _keys(oracle, ("schema", "version", "source_manifest_sha256",
                            "source_rows_sha256", "records"),
                   "workflow.records.oracle")
    if oracle["schema"] != ORACLE_SCHEMA or oracle["version"] != 1:
        _fail("oracle record schema/version is not approved")
    if oracle["source_manifest_sha256"] != support_output["manifest_sha256"] \
            or oracle["source_rows_sha256"] != support_output["rows_sha256"]:
        _fail("oracle records do not bind the actual schema-2 source artifacts")
    oracle_records = _list(oracle["records"], "workflow.records.oracle.records")
    oracle_by_row = {}
    for index, item in enumerate(oracle_records):
        item = _keys(item, ("row", "code_section_status", "code_section_bytes",
                            "code_section_sha256", "runtime_oracle_status",
                            "runtime_exit_code", "native_runtime"),
                     f"workflow.records.oracle.records[{index}]")
        if type(item["row"]) is not int or not 0 <= item["row"] < len(parsed):
            _fail("oracle record row is outside the canonical population")
        if item["row"] in oracle_by_row:
            _fail("oracle records duplicate a canonical row")
        expected_compile = item["row"] in support_output["compiler_eligible_rows"]
        if expected_compile:
            if item["code_section_status"] != "parsed-deterministic":
                _fail("eligible oracle record code section was not independently parsed")
            _bounded_code_bytes(
                item["code_section_bytes"],
                f"workflow.records.oracle.records[{index}].code_section_bytes")
            _sha(item["code_section_sha256"],
                 f"workflow.records.oracle.records[{index}].code_section_sha256")
            if item["code_section_bytes"] == 0 \
                    and item["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                _fail("zero-byte oracle record does not bind the empty payload")
        else:
            if item["code_section_status"] != "not-applicable" \
                    or item["code_section_bytes"] is not None \
                    or item["code_section_sha256"] is not None:
                _fail("untimed oracle record must retain explicit null code observations")
        if item["runtime_oracle_status"] not in {"passed-native", "not-applicable"}:
            _fail("oracle record runtime status is not approved")
        if item["runtime_oracle_status"] == "passed-native" and item["runtime_exit_code"] != 0:
            _fail("passing native oracle must have exit code zero")
        if item["runtime_oracle_status"] == "not-applicable" \
                and item["runtime_exit_code"] != (-1 if expected_compile else None):
            _fail("inapplicable runtime oracle has an invalid absent observation")
        _boolean(item["native_runtime"],
                 f"workflow.records.oracle.records[{index}].native_runtime")
        if item["native_runtime"] is not \
                (item["runtime_oracle_status"] == "passed-native"):
            _fail("oracle native-runtime flag contradicts its runtime status")
        if native_target is not None:
            runtime_required = (expected_compile
                                and _native_runtime_required(parsed[item["row"]], native_target))
            if item["native_runtime"] is not runtime_required:
                _fail("oracle native-runtime applicability differs from the frozen row obligation")
        oracle_by_row[item["row"]] = item
    if set(oracle_by_row) != set(range(len(parsed))):
        _fail("oracle records do not cover every canonical performance row")

    for row in parsed:
        admission_item = admission_by_row[row["row"]]
        oracle_item = oracle_by_row[row["row"]]
        if admission_item["identity"] != row["identity"]:
            _fail("canonical row identity differs from the admitted execution row")
        compile_eligible = row["row"] in support_output["compiler_eligible_rows"]
        code_parsed = oracle_item["code_section_status"] == "parsed-deterministic"
        code_ratio_eligible = (
            code_parsed and compile_eligible and oracle_item["code_section_bytes"] > 0)
        expected = {
            "compiler_wall_time": compile_eligible,
            "compiler_peak_rss": compile_eligible,
            "generated_code_bytes": code_ratio_eligible,
            "generated_runtime": oracle_item["native_runtime"] and compile_eligible,
            "runtime_oracle": ("independent-native-executable-oracle"
                                if oracle_item["runtime_oracle_status"] == "passed-native"
                                else "not-applicable"),
            "code_section": (
                "deterministic-code-section" if code_ratio_eligible
                else "deterministic-zero-baseline-code-section"
                if code_parsed and compile_eligible else "not-applicable"),
        }
        if row["eligibility"] != expected:
            _fail("canonical row eligibility is not derived from admission and oracle records")
    return {"result_input_plan": result_plan, "admission": admission,
            "oracle": oracle}


def _all_artifacts(binding):
    support = binding["support"]
    artifacts = []
    artifacts.extend((f"support.files[{index}]", item)
                     for index, item in enumerate(support["files"]))
    artifacts.append(("support.validator.source", support["validator"]["source"]))
    work = binding["requested_work"]
    artifacts.extend((f"requested_work.items[{index}].artifact", item["artifact"])
                     for index, item in enumerate(work["items"]))
    for subject_name in ("baseline", "candidate"):
        subject = binding["subjects"][subject_name]
        artifacts.extend(((f"subjects.{subject_name}.source_snapshot", subject["source_snapshot"]),
                          (f"subjects.{subject_name}.binary", subject["binary"]),
                          (f"subjects.{subject_name}.build_receipt", subject["build_receipt"])))
    producer = binding["producer"]
    artifacts.extend((("producer.toolchain.compiler_binary",
                       producer["toolchain"]["compiler_binary"]),
                      ("producer.toolchain.resource_directory",
                       producer["toolchain"]["resource_directory"]),
                      ("producer.build.configuration", producer["build"]["configuration"]),
                      ("producer.build.flags", producer["build"]["flags"])))
    measurement = binding["measurement"]
    artifacts.extend((("measurement.harness_binary", measurement["harness_binary"]),
                      ("measurement.statistics_implementation",
                       measurement["statistics_implementation"])))
    execution = binding["execution"]
    artifacts.extend((("execution.service.recipe", execution["service"]["recipe"]),
                      ("execution.profile.descriptor", execution["profile"]["descriptor"]),
                      ("execution.host.qualification_receipt",
                       execution["host"]["qualification_receipt"]),
                      ("execution.host.aa_admission_receipt",
                       execution["host"]["aa_admission_receipt"]),
                      ("execution.lease.receipt", execution["lease"]["receipt"])))
    provenance = binding["provenance"]
    artifacts.extend((("provenance.relation_receipt", provenance["relation_receipt"]),
                      ("provenance.replay_receipt", provenance["replay_receipt"]),
                      ("provenance.replay_bundle", provenance["replay_bundle"]),
                      ("provenance.census_receipt", provenance["census_receipt"]),
                      ("provenance.strict_receipt", provenance["strict_receipt"])))
    workflow = binding["workflow"]
    artifacts.extend((f"workflow.phases.{name}", workflow["phases"][name])
                     for name in WORKFLOW_PHASES)
    artifacts.extend((f"workflow.records.{name}", workflow["records"][name])
                     for name in ("admission", "oracle", "result_input_plan"))
    return artifacts


def _check_execution_evidence(root, binding):
    """Validate the structured #437 service, host and lease receipts."""
    execution = binding["execution"]
    service = _read_json_evidence(root, execution["service"]["recipe"],
                                  "execution.service.recipe")
    service = _keys(service, ("schema", "version", "service_id", "service_version",
                              "whole_job", "phases", "supervisor_authoritative",
                              "lease_protocol", "cgroup_cleanup", "descendant_cleanup"),
                    "service_receipt")
    if service["schema"] != SERVICE_RECEIPT_SCHEMA or service["version"] != 1:
        _fail("service receipt schema/version is not the admitted #437 protocol")
    if service["service_id"] != execution["service"]["id"] \
            or service["service_version"] != execution["service"]["version"]:
        _fail("service receipt identity differs from the bound service")
    _exact_list(service["phases"], ["preparation", "build", "tests", "measurement",
                                     "finalization", "cleanup"], "service_receipt.phases")
    for field in ("whole_job", "supervisor_authoritative", "cgroup_cleanup",
                  "descendant_cleanup"):
        if not service[field]:
            _fail(f"service receipt.{field} is required")
        _boolean(service[field], f"service_receipt.{field}")
    if service["lease_protocol"] != LEASE_PROTOCOL:
        _fail("service receipt does not use the supervisor lease protocol")

    profile = _read_json_evidence(root, execution["profile"]["descriptor"],
                                  "execution.profile.descriptor")
    profile = _keys(profile, ("schema", "version", "profile_id", "profile_version",
                              "machine_id", "logical_cpu", "native_target",
                              "native_only", "whole_host_isolation", "lease_protocol"),
                    "profile_receipt")
    if profile["schema"] != PROFILE_SCHEMA or profile["version"] != 1:
        _fail("host profile schema/version is not the admitted #437 profile")
    expected_profile = execution["profile"]
    if (profile["profile_id"], profile["profile_version"], profile["machine_id"]) != \
            (expected_profile["id"], expected_profile["version"],
             expected_profile["machine_id"]):
        _fail("host profile identity differs from the binding")
    for field in ("native_only", "whole_host_isolation"):
        _boolean(profile[field], f"profile_receipt.{field}")
        if not profile[field]:
            _fail(f"profile receipt.{field} is required")
    if profile["lease_protocol"] != LEASE_PROTOCOL:
        _fail("host profile does not identify the supervisor lease protocol")
    _nonnegative_int(profile["logical_cpu"], "profile_receipt.logical_cpu")
    if profile["native_target"] not in TARGETS:
        _fail("host profile native target is not supported")

    host = execution["host"]
    qualification = _read_json_evidence(root, host["qualification_receipt"],
                                        "execution.host.qualification_receipt")
    qualification = _keys(qualification, (
        "schema", "version", "machine_id", "profile_id", "profile_version",
        "logical_cpu", "native_target", "qualified", "whole_host_isolation",
        "lease_protocol"),
        "qualification_receipt")
    if qualification["schema"] != QUALIFICATION_SCHEMA or qualification["version"] != 1:
        _fail("host qualification schema/version is not the admitted #437 receipt")
    if (qualification["machine_id"], qualification["profile_id"],
            qualification["profile_version"]) != (host["machine_id"], expected_profile["id"],
                                                   expected_profile["version"]):
        _fail("host qualification identity differs from the binding")
    for field in ("qualified", "whole_host_isolation"):
        _boolean(qualification[field], f"qualification_receipt.{field}")
        if not qualification[field]:
            _fail(f"qualification receipt.{field} is required")
    if qualification["lease_protocol"] != LEASE_PROTOCOL:
        _fail("host qualification does not identify the supervisor lease protocol")
    _nonnegative_int(qualification["logical_cpu"],
                     "qualification_receipt.logical_cpu")
    if qualification["native_target"] not in TARGETS:
        _fail("host qualification native target is not supported")
    if (qualification["logical_cpu"], qualification["native_target"]) != \
            (profile["logical_cpu"], profile["native_target"]):
        _fail("host qualification CPU/target differs from the admitted profile")

    admission = _read_json_evidence(root, host["aa_admission_receipt"],
                                    "execution.host.aa_admission_receipt")
    admission = _keys(admission, (
        "schema", "version", "machine_id", "profile_id", "profile_version", "service_id",
        "logical_cpu", "native_target", "admitted", "native_only",
        "baseline_source_commit", "baseline_source_tree", "lease_protocol"),
        "aa_admission_receipt")
    if admission["schema"] != AA_SCHEMA or admission["version"] != 1:
        _fail("A/A admission schema/version is not the admitted #437 receipt")
    if (admission["machine_id"], admission["profile_id"], admission["profile_version"],
            admission["service_id"]) != (host["machine_id"], expected_profile["id"],
                                           expected_profile["version"], execution["service"]["id"]):
        _fail("A/A admission identity differs from the binding")
    for field in ("admitted", "native_only"):
        _boolean(admission[field], f"aa_admission_receipt.{field}")
        if not admission[field]:
            _fail(f"A/A admission receipt.{field} is required")
    _commit(admission["baseline_source_commit"],
            "aa_admission_receipt.baseline_source_commit")
    _commit(admission["baseline_source_tree"], "aa_admission_receipt.baseline_source_tree")
    baseline = binding["subjects"]["baseline"]
    if (admission["baseline_source_commit"], admission["baseline_source_tree"]) != \
            (baseline["source_commit"], baseline["source_tree"]):
        _fail("A/A admission baseline does not match the bound direct subject")
    if admission["lease_protocol"] != LEASE_PROTOCOL:
        _fail("A/A admission does not identify the supervisor lease protocol")
    _nonnegative_int(admission["logical_cpu"], "aa_admission_receipt.logical_cpu")
    if admission["native_target"] not in TARGETS:
        _fail("A/A admission native target is not supported")
    if (admission["logical_cpu"], admission["native_target"]) != \
            (profile["logical_cpu"], profile["native_target"]):
        _fail("A/A admission CPU/target differs from the admitted profile")

    lease = _read_json_evidence(root, execution["lease"]["receipt"],
                                "execution.lease.receipt")
    lease = _keys(lease, ("schema", "version", "authority", "access", "cleanup",
                          "owner", "candidate_can_access", "cloexec_before_candidate",
                          "cgroup_cleanup", "descendant_cleanup"), "lease_receipt")
    if lease["schema"] != LEASE_RECEIPT_SCHEMA or lease["version"] != 1:
        _fail("lease receipt schema/version is not the admitted #437 protocol")
    expected_lease = execution["lease"]
    for field, expected in (("authority", expected_lease["authority"]),
                            ("access", expected_lease["access"]),
                            ("cleanup", expected_lease["cleanup"])):
        if lease[field] != expected:
            _fail(f"lease receipt.{field} differs from the binding")
    if lease["owner"] != "server-supervisor":
        _fail("lease receipt.owner must be server-supervisor")
    for field in ("candidate_can_access", "cloexec_before_candidate", "cgroup_cleanup",
                  "descendant_cleanup"):
        _boolean(lease[field], f"lease_receipt.{field}")
    if lease["candidate_can_access"] or not lease["cloexec_before_candidate"] \
            or not lease["cgroup_cleanup"] or not lease["descendant_cleanup"]:
        _fail("lease receipt does not prove inaccessible supervisor ownership and cleanup")
    return {"logical_cpu": profile["logical_cpu"],
            "native_target": profile["native_target"]}


def _check_subject_receipts(root, binding):
    """Check source-snapshot and build receipt relations for both subjects."""
    producer = binding["producer"]
    expected_toolchain = {
        "compiler_binary_sha256": producer["toolchain"]["compiler_binary"]["sha256"],
        "resource_directory_sha256": producer["toolchain"]["resource_directory"]["sha256"],
        "configuration_sha256": producer["build"]["configuration"]["sha256"],
        "flags_sha256": producer["build"]["flags"]["sha256"],
    }
    for role in ("baseline", "candidate"):
        subject = binding["subjects"][role]
        snapshot = _read_json_evidence(root, subject["source_snapshot"],
                                       f"subjects.{role}.source_snapshot")
        snapshot = _keys(snapshot, ("schema", "version", "source_commit", "source_tree",
                                    "snapshot_kind"), f"{role}_source_snapshot")
        if snapshot["schema"] != SOURCE_SNAPSHOT_SCHEMA or snapshot["version"] != 1:
            _fail(f"{role} source snapshot schema/version is not approved")
        _commit(snapshot["source_commit"], f"{role}_source_snapshot.source_commit")
        _commit(snapshot["source_tree"], f"{role}_source_snapshot.source_tree")
        if (snapshot["source_commit"], snapshot["source_tree"]) != \
                (subject["source_commit"], subject["source_tree"]):
            _fail(f"{role} source snapshot does not match its subject")
        if snapshot["snapshot_kind"] != "git-tree-with-submodules-and-generated-inputs":
            _fail(f"{role} source snapshot kind is not the frozen source closure")

        receipt = _read_json_evidence(root, subject["build_receipt"],
                                      f"subjects.{role}.build_receipt")
        receipt = _keys(receipt, (
            "schema", "version", "source_commit", "source_tree",
            "source_snapshot_sha256", "binary_sha256", "compiler_binary_sha256",
            "resource_directory_sha256", "configuration_sha256", "flags_sha256",
            "relation"), f"{role}_build_receipt")
        if receipt["schema"] != BUILD_RECEIPT_SCHEMA or receipt["version"] != 1:
            _fail(f"{role} build receipt schema/version is not approved")
        _commit(receipt["source_commit"], f"{role}_build_receipt.source_commit")
        _commit(receipt["source_tree"], f"{role}_build_receipt.source_tree")
        for field in ("source_snapshot_sha256", "binary_sha256", *expected_toolchain):
            _sha(receipt[field], f"{role}_build_receipt.{field}")
        expected = {
            "source_commit": subject["source_commit"],
            "source_tree": subject["source_tree"],
            "source_snapshot_sha256": subject["source_snapshot"]["sha256"],
            "binary_sha256": subject["binary"]["sha256"],
            **expected_toolchain,
        }
        if any(receipt[key] != value for key, value in expected.items()):
            _fail(f"{role} build receipt does not bind source, binary and producer")
        if receipt["relation"] != "git-source-snapshot-to-trusted-build-to-binary":
            _fail(f"{role} build receipt relation is not source-to-binary")


def _git_environment():
    """Return Git's identity-only environment with replacement objects disabled."""
    git_environment = os.environ.copy()
    git_environment["GIT_NO_REPLACE_OBJECTS"] = "1"
    return git_environment


def _git_run(repository_root, arguments, name):
    command = ["git", "-C", str(Path(repository_root).resolve()), *arguments]
    try:
        completed = subprocess.run(command, check=False, capture_output=True,
                                   text=True, timeout=10, env=_git_environment())
    except (OSError, subprocess.SubprocessError) as error:
        _fail(f"cannot verify {name} in immutable checkout: {error}")
    if completed.returncode != 0:
        _fail(f"git cannot resolve {name}: {completed.stderr.strip()}")
    return completed.stdout.strip()


def _git_blob(repository_root, revision_path, name):
    """Read a Git blob without text normalization or whitespace stripping."""
    command = ["git", "-C", str(Path(repository_root).resolve()), "show", revision_path]
    try:
        completed = subprocess.run(command, check=False, capture_output=True,
                                   timeout=10, env=_git_environment())
    except (OSError, subprocess.SubprocessError) as error:
        _fail(f"cannot verify {name} in immutable checkout: {error}")
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        _fail(f"git cannot resolve {name}: {detail}")
    return completed.stdout


def _check_git_identities(repository_root, binding):
    """Verify every bound commit/tree and source blob in a local checkout."""
    repository_root = Path(repository_root).resolve()
    if not repository_root.is_dir():
        _fail("repository_root is not a directory")
    status = _git_run(repository_root, ["status", "--porcelain",
                                       "--untracked-files=all"],
                      "immutable checkout status")
    if status:
        _fail("repository_root must be a clean immutable checkout")
    identities = [("contract", binding["contract"]["source_commit"],
                   binding["contract"]["source_tree"]),
                  ("baseline", binding["subjects"]["baseline"]["source_commit"],
                   binding["subjects"]["baseline"]["source_tree"]),
                  ("candidate", binding["subjects"]["candidate"]["source_commit"],
                   binding["subjects"]["candidate"]["source_tree"]),
                  ("harness", binding["measurement"]["harness_source_commit"],
                   binding["measurement"]["harness_source_tree"]),
                  ("validator", binding["support"]["validator"]["source_commit"],
                   binding["support"]["validator"]["source_tree"])]
    for name, commit, tree in identities:
        _git_run(repository_root, ["cat-file", "-e", f"{commit}^{{commit}}"],
                 f"{name} commit {commit}")
        actual_tree = _git_run(repository_root, ["rev-parse", f"{commit}^{{tree}}"],
                               f"{name} commit tree")
        if actual_tree != tree:
            _fail(f"{name} commit does not resolve to bound tree")
        _git_run(repository_root, ["cat-file", "-e", f"{tree}^{{tree}}"],
                 f"{name} tree {tree}")

    contract = binding["contract"]
    if contract["source"]["path"] != "docs/native-retirement-performance-contract.md":
        _fail("contract source artifact path is not the versioned contract")
    blob = _git_blob(repository_root,
                     f"{contract['source_commit']}:{contract['source']['path']}",
                     "contract source blob")
    if hashlib.sha256(blob).hexdigest() != contract["source"]["sha256"]:
        _fail("contract source artifact does not match its bound commit")
    validator = binding["support"]["validator"]
    if validator["source"]["path"] != "tools/native_retirement_contract.py":
        _fail("validator source artifact path is not native_retirement_contract.py")
    blob = _git_blob(repository_root,
                     f"{validator['source_commit']}:{validator['source']['path']}",
                     "validator source blob")
    if hashlib.sha256(blob).hexdigest() != validator["source"]["sha256"]:
        _fail("validator source artifact does not match its bound commit")


def validate(binding_path, evidence_root=None, repository_root=None,
             trusted_execution_receipt_sha256=None):
    """Validate one binding record and optionally its referenced evidence files."""
    binding_path = Path(binding_path)
    try:
        with binding_path.open(encoding="utf-8") as stream:
            binding = json.load(stream, object_pairs_hook=_json_object)
    except (OSError, json.JSONDecodeError) as error:
        _fail(f"cannot read binding record {binding_path}: {error}")
    binding = _keys(binding, ("schema", "decision_id", "contract", "support",
                              "requested_work", "population", "subjects", "producer",
                              "measurement", "execution", "provenance", "rules",
                              "workflow"),
                    "binding")
    if binding["schema"] != SCHEMA:
        _fail(f"binding.schema must be {SCHEMA!r}")
    if binding["decision_id"] != DECISION_ID:
        _fail(f"binding.decision_id must be {DECISION_ID!r}")

    contract = _keys(binding["contract"], ("source_commit", "source_tree", "source"),
                     "contract")
    _commit(contract["source_commit"], "contract.source_commit")
    _commit(contract["source_tree"], "contract.source_tree")
    _artifact(contract["source"], "contract.source")
    support = _support(binding["support"])
    _requested_work(binding["requested_work"], support)
    _population(binding["population"], support)
    workflow = _workflow(binding["workflow"])

    subjects = _keys(binding["subjects"], ("baseline", "candidate"), "subjects")
    baseline = _subject(subjects["baseline"], "subjects.baseline", "direct-baseline",
                        "direct-native", {"pre-cutover"})
    candidate = _subject(subjects["candidate"], "subjects.candidate", "mir-candidate",
                         "mir-only", {"mir-only-cutover", "post-deletion"})
    if baseline["source_commit"] == candidate["source_commit"]:
        _fail("baseline and candidate source commits must be distinct immutable IDs")
    if baseline["source_tree"] == candidate["source_tree"]:
        _fail("baseline and candidate source trees must be distinct immutable IDs")

    _producer(binding["producer"])
    _measurement(binding["measurement"])
    _execution(binding["execution"])
    provenance = _provenance(binding["provenance"])
    rules = _rules(binding["rules"])
    _check_sampling_family(binding["population"], rules)
    artifacts = _all_artifacts(binding)
    artifacts.append(("contract.source", contract["source"]))
    paths = [artifact["path"] for _name, artifact in artifacts]
    if len(paths) != len(set(paths)):
        _fail("binding artifact paths must be unique")
    rows_recomputed = False
    provenance_checked = False
    support_checked = False
    execution_checked = False
    git_checked = False
    bundle_checked = False
    if evidence_root is not None:
        for name, artifact in artifacts:
            _check_evidence(evidence_root, artifact, name)
        execution_facts = _check_execution_evidence(evidence_root, binding)
        execution_checked = True
        rows_artifact = support["files"][SUPPORT_FILE_ROLES.index("performance_rows")]
        rows = _performance_rows_with_sources(
            _evidence_bytes(evidence_root, rows_artifact,
                            "support.files.performance_rows"))
        _population(binding["population"], support, rows[:3])
        support_output = _check_support_output(
            evidence_root, binding, rows, execution_facts["native_target"])
        _check_workflow_evidence(evidence_root, binding, workflow, support_output,
                                 rows, binding["population"], rules,
                                 repository_root, trusted_execution_receipt_sha256,
                                 execution_facts["logical_cpu"],
                                 execution_facts["native_target"])
        support_checked = True
        _check_subject_receipts(evidence_root, binding)
        _check_provenance_evidence(evidence_root, binding, provenance)
        rows_recomputed = True
        provenance_checked = True
        bundle_checked = True
        if repository_root is not None:
            _check_git_identities(repository_root, binding)
            git_checked = True
    if evidence_root is not None and repository_root is None:
        proof = "evidence-and-receipts-checked-without-independent-git"
    elif evidence_root is not None:
        proof = "independent-evidence-and-receipts-checked"
    else:
        proof = "structural-only"
    return {
        "schema": binding["schema"],
        "decision_id": binding["decision_id"],
        "baseline_commit": baseline["source_commit"],
        "candidate_commit": candidate["source_commit"],
        "candidate_stage": candidate["stage"],
        "required_rows": binding["population"]["required_row_count"],
        "artifacts": len(artifacts),
        "evidence_checked": evidence_root is not None,
        "rows_recomputed": rows_recomputed,
        "provenance_checked": provenance_checked,
        "support_checked": support_checked,
        "execution_checked": execution_checked,
        "invocations_checked": execution_checked,
        "bundle_checked": bundle_checked,
        "git_checked": git_checked,
        "proof": proof,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binding", type=Path)
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument("--repository-root", type=Path,
                        help="immutable checkout used to verify commit/tree/source identities")
    parser.add_argument("--trusted-execution-receipt-sha256",
                        help="receipt digest obtained independently from the admitted control service; "
                             "required with evidence, never copied from an untrusted bundle")
    arguments = parser.parse_args()
    print(json.dumps(validate(arguments.binding, arguments.evidence_root,
                               arguments.repository_root,
                               arguments.trusted_execution_receipt_sha256), sort_keys=True))


if __name__ == "__main__":
    main()
