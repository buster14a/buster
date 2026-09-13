#!/usr/bin/env python3
"""Validate an immutable native-retirement performance binding.

The census validator proves that one object-census directory is internally
consistent.  This validator proves the stronger, cross-run identity contract
needed by the retirement performance gate: the approved policy, the complete
required population, both compiler subjects, their producer, the admitted
service/host, and the statistical rules are all bound before measurements are
considered.  The evidence form parses the versioned canonical row artifact and
content-checks producer/replay receipts; the structural form is explicitly not
proof.  It intentionally does not run a compiler or inspect measurements.
"""

import argparse
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess


SCHEMA = "buster-native-retirement-performance-binding-v1"
DECISION_ID = "native-retirement-performance-v1"
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$")
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
ROW_SCHEMA = "buster-native-retirement-performance-rows-v1"
ROW_VERSION = 1
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
SUPPORT_DECLARATION_SHA256 = "99af290c697fb8bf68f1fc42d1db0c8d436289678c7bca8b12f38a34d6338f44"
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
PERFORMANCE_DECLARATION_SCHEMA = "buster-native-retirement-performance-population-v1"
PERFORMANCE_DECLARATION_VERSION = 1
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
SUPPORT_INPUT_COUNT = 548
SUPPORT_SUBJECT_COUNT = 402
SUPPORT_GROUP_COUNT = 19296
SUPPORT_OBJECT_ROW_COUNT = 77184
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
    members = set()
    cell_counts = {}
    cell_digests = {}
    for scope in STATISTICAL_SCOPES:
        for metric in STATISTICAL_METRICS:
            eligible = [row for row in rows if row["metrics"].get(metric, False)]
            if not eligible:
                continue
            prefix = f"{scope}/{metric}"
            members.add(f"{prefix}/aggregate")
            for dimension in STATISTICAL_DIMENSIONS:
                values = {row["identity"][dimension] for row in eligible}
                for value in values:
                    members.add(f"{prefix}/slice/{dimension}={value}")
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
    family["sha256"] = _canonical_json_digest(family)
    return family


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
        if not metrics["compiler_wall_time"] or not metrics["compiler_peak_rss"]:
            _fail(f"{name}.rows[{index}] must measure compiler wall time and peak RSS")
        if metrics["generated_code_bytes"]:
            if eligibility["code_section"] != "deterministic-code-section":
                _fail(f"{name}.rows[{index}] has an invalid code-section obligation")
        elif eligibility["code_section"] != "not-applicable":
            _fail(f"{name}.rows[{index}] has an inapplicable code-section marker")
        if metrics["generated_runtime"]:
            if eligibility["runtime_oracle"] != "independent-native-executable-oracle":
                _fail(f"{name}.rows[{index}] has an invalid runtime oracle")
        elif eligibility["runtime_oracle"] != "not-applicable":
            _fail(f"{name}.rows[{index}] has an inapplicable runtime oracle")
        parsed.append({"row": item["row"], "identity": identity, "metrics": metrics})
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
    if members != sorted(set(members)):
        _fail("population.statistical_family.members must be unique and sorted")
    counts = _object(family["cell_counts"], "population.statistical_family.cell_counts")
    digests = _object(family["cell_identity_sha256"],
                      "population.statistical_family.cell_identity_sha256")
    expected_keys = [f"{scope}/{metric}" for scope in STATISTICAL_SCOPES
                     for metric in STATISTICAL_METRICS]
    if set(counts) != set(expected_keys) or set(digests) != set(expected_keys):
        _fail("population.statistical_family must bind every round/metric family")
    for key in expected_keys:
        _positive_int(counts[key], f"population.statistical_family.cell_counts.{key}")
        _sha(digests[key], f"population.statistical_family.cell_identity_sha256.{key}")
    _sha(family["sha256"], "population.statistical_family.sha256")
    family_without_digest = {key: family[key] for key in family if key != "sha256"}
    if family["sha256"] != _canonical_json_digest(family_without_digest):
        _fail("population.statistical_family.sha256 does not match family members")
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
                                         "warmups_per_variant", "block_order",
                                         "fixed_order", "optional_stopping",
                                         "outlier_deletion", "retain_all_samples"),
                     "rules.sampling")
    _positive_int(sampling["seed"], "rules.sampling.seed")
    if type(sampling["rounds"]) is not int or sampling["rounds"] != 2:
        _fail("rules.sampling.rounds must be exactly 2")
    _positive_int(sampling["pairs_per_round"], "rules.sampling.pairs_per_round")
    if sampling["pairs_per_round"] < 60 or sampling["pairs_per_round"] > 256:
        _fail("rules.sampling.pairs_per_round must be between 60 and 256")
    if sampling["pairs_per_round"] % 2:
        _fail("rules.sampling.pairs_per_round must be even for AB/BA blocks")
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
        if aggregation[name] != "geometric-mean-cell-ratios":
            _fail(f"rules.aggregation.{name} must use geometric mean cell ratios")
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
        if resampling["resamples"] < 100000:
            _fail("rules.uncertainty.resampling.resamples must be at least 100000")
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


def _check_support_output(root, binding, row_data):
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
    if support_declaration["sha256"] != SUPPORT_DECLARATION_SHA256:
        _fail("#508 support declaration digest is not the approved immutable input")
    declaration_data = _evidence_bytes(root, support_declaration,
                                       "support.files.support_declaration")
    if hashlib.sha256(declaration_data).hexdigest() != SUPPORT_DECLARATION_SHA256:
        _fail("#508 support declaration bytes changed")
    declaration = _tsv(declaration_data, SUPPORT_DECLARATION_FIELDS,
                       "support_declaration")
    if len(declaration) != SUPPORT_INPUT_COUNT:
        _fail("#508 support declaration input count changed")
    if [row["path"] for row in declaration] != sorted(row["path"] for row in declaration):
        _fail("#508 support declaration paths are not sorted")
    if len({row["path"] for row in declaration}) != len(declaration):
        _fail("#508 support declaration contains duplicate paths")
    subjects = {row["path"] for row in declaration if row["role"] == "subject"}
    if len(subjects) != SUPPORT_SUBJECT_COUNT:
        _fail("#508 support declaration subject count changed")

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
    if manifest["support_contract_sha256"] != SUPPORT_DECLARATION_SHA256:
        _fail("#508 manifest support declaration digest differs")
    if manifest["environment"] != "explicit-replacement-in-environment.tsv":
        _fail("#508 manifest lacks the explicit replacement environment")
    if manifest["unfrozen_dependencies"] != "none-for-object-census":
        _fail("#508 manifest records unfrozen dependencies")
    if manifest["sysroot"] != "none" or manifest["system_include"] != "none":
        _fail("#508 object census has an unbound sysroot or system include")
    if _decimal_text(manifest["inputs"], "manifest.inputs") != SUPPORT_INPUT_COUNT:
        _fail("#508 manifest input count differs from the declaration")
    if _decimal_text(manifest["rows"], "manifest.rows") != SUPPORT_OBJECT_ROW_COUNT:
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
    if len(inputs) != SUPPORT_INPUT_COUNT:
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
    if len(census_rows) != SUPPORT_OBJECT_ROW_COUNT:
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
        if row["cpu"] != manifest["cpu"]:
            _fail(f"rows.tsv[{index}] CPU differs from the manifest")
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
    if len(object_rows) != SUPPORT_OBJECT_ROW_COUNT:
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
    if len(parsed) < SUPPORT_MIN_STAGE_ROW_COUNT:
        _fail("canonical performance rows do not contain the complete stage population")
    if axes["allocators"] != ALLOCATORS or axes["frontends"] != FRONTENDS \
            or axes["PIC"] != PIC or axes["stages"] != STAGES \
            or axes["targets"] != sorted(TARGETS):
        _fail("canonical performance rows do not cover the approved axes")
    if len(axes["cpus"]) != 1:
        _fail("canonical performance rows must bind one explicit CPU profile")

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
    if performance["object_row_count"] != SUPPORT_OBJECT_ROW_COUNT:
        _fail("performance declaration object row count is not #508's full count")
    if performance["required_row_count"] < SUPPORT_MIN_STAGE_ROW_COUNT \
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
    validator_report = _keys(validator_report, (
        "schema", "version", "success", "validator_source_commit", "validator_source_tree",
        "support_declaration_sha256", "manifest_sha256", "inputs_sha256", "rows_sha256",
        "dependencies_sha256", "environment_sha256", "inputs", "groups", "rows"),
        "validator_report")
    if validator_report["schema"] != VALIDATOR_REPORT_SCHEMA \
            or validator_report["version"] != VALIDATOR_REPORT_VERSION \
            or not validator_report["success"]:
        _fail("#508 validator report is not a successful v1 validation")
    validator = support["validator"]
    if validator_report["validator_source_commit"] != validator["source_commit"] \
            or validator_report["validator_source_tree"] != validator["source_tree"]:
        _fail("validator report source does not match the bound independent validator")
    for role, field in (("support_declaration", "support_declaration_sha256"),
                        ("manifest", "manifest_sha256"), ("inputs", "inputs_sha256"),
                        ("rows", "rows_sha256"), ("dependencies", "dependencies_sha256"),
                        ("environment", "environment_sha256")):
        _sha(validator_report[field], f"validator_report.{field}")
        if validator_report[field] != expected_digests[role]:
            _fail(f"validator report {field} differs from #508 output")
    if validator_report["inputs"] != SUPPORT_INPUT_COUNT \
            or validator_report["groups"] != SUPPORT_GROUP_COUNT \
            or validator_report["rows"] != SUPPORT_OBJECT_ROW_COUNT:
        _fail("validator report counts are not the complete #508 inventory")
    return {"manifest": manifest, "inputs": inputs, "rows": census_rows,
            "dependencies": dependencies, "environment": environment,
            "sources": sources, "axes": axes, "family": family}


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
                              "machine_id", "native_only", "whole_host_isolation",
                              "lease_protocol"), "profile_receipt")
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

    host = execution["host"]
    qualification = _read_json_evidence(root, host["qualification_receipt"],
                                        "execution.host.qualification_receipt")
    qualification = _keys(qualification, (
        "schema", "version", "machine_id", "profile_id", "profile_version",
        "qualified", "whole_host_isolation", "lease_protocol"),
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

    admission = _read_json_evidence(root, host["aa_admission_receipt"],
                                    "execution.host.aa_admission_receipt")
    admission = _keys(admission, (
        "schema", "version", "machine_id", "profile_id", "profile_version", "service_id",
        "admitted", "native_only", "baseline_source_commit", "baseline_source_tree",
        "lease_protocol"), "aa_admission_receipt")
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


def _git_run(repository_root, arguments, name):
    command = ["git", "-C", str(Path(repository_root).resolve()), *arguments]
    try:
        completed = subprocess.run(command, check=False, capture_output=True,
                                   text=True, timeout=10)
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
                                   timeout=10)
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


def validate(binding_path, evidence_root=None, repository_root=None):
    """Validate one binding record and optionally its referenced evidence files."""
    binding_path = Path(binding_path)
    try:
        with binding_path.open(encoding="utf-8") as stream:
            binding = json.load(stream, object_pairs_hook=_json_object)
    except (OSError, json.JSONDecodeError) as error:
        _fail(f"cannot read binding record {binding_path}: {error}")
    binding = _keys(binding, ("schema", "decision_id", "contract", "support",
                              "requested_work", "population", "subjects", "producer",
                              "measurement", "execution", "provenance", "rules"),
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
    _rules(binding["rules"])
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
        rows_artifact = support["files"][SUPPORT_FILE_ROLES.index("performance_rows")]
        rows = _performance_rows_with_sources(
            _evidence_bytes(evidence_root, rows_artifact,
                            "support.files.performance_rows"))
        _population(binding["population"], support, rows[:3])
        _check_support_output(evidence_root, binding, rows)
        support_checked = True
        _check_subject_receipts(evidence_root, binding)
        _check_execution_evidence(evidence_root, binding)
        execution_checked = True
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
    arguments = parser.parse_args()
    print(json.dumps(validate(arguments.binding, arguments.evidence_root,
                               arguments.repository_root), sort_keys=True))


if __name__ == "__main__":
    main()
