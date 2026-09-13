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
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path, PurePosixPath
import re


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
    "link_obligation", "execution_obligation", "artifact_stage",
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
    "rows", "performance_rows",
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


def _canonical_files_digest(files):
    rows = [
        {"name": item["name"], "path": item["path"], "bytes": item["bytes"],
         "sha256": item["sha256"]}
        for item in sorted(files, key=lambda item: item["name"])
    ]
    encoded = json.dumps(rows, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _canonical_json_digest(value):
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _support(value):
    value = _keys(value, ("schema", "version", "root_sha256", "files"), "support")
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
    if value["root_sha256"] != _canonical_files_digest(files):
        _fail("support.root_sha256 does not match the canonical file digest")
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
                  "execution_obligation"):
        _token(value[field], f"{name}.{field}")
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
    dimensions = ("target", "cpu", "allocator", "frontend_lowering", "PIC",
                  "artifact_stage")
    for metric in STATISTICAL_METRICS:
        eligible = [row for row in rows if row["metrics"].get(metric, False)]
        if not eligible:
            continue
        members.add(f"aggregate/{metric}")
        for dimension in dimensions:
            values = {row["identity"][dimension] for row in eligible}
            for value in values:
                members.add(f"slice/{dimension}={value}/{metric}")
        for row in eligible:
            members.add(f"cell/{row['row']}/{metric}")
    ordered = sorted(members)
    return {"members": ordered, "sha256": _canonical_json_digest(ordered)}


def _performance_rows(value, name="performance_rows"):
    raw = value
    try:
        value = json.loads(value.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"{name} is not valid UTF-8 JSON: {error}")
    value = _keys(value, ("schema", "version", "row_identity_fields", "rows"), name)
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
    return parsed, _derive_axes(parsed), _derive_statistical_family(parsed)


def _population(value, support, row_data=None):
    value = _keys(value, ("required_row_count", "required_rows_sha256", "axes",
                          "row_identity_fields", "statistical_family"), "population")
    _positive_int(value["required_row_count"], "population.required_row_count")
    _sha(value["required_rows_sha256"], "population.required_rows_sha256")
    rows = support["files"][SUPPORT_FILE_ROLES.index("performance_rows")]
    if value["required_rows_sha256"] != rows["sha256"]:
        _fail("population rows digest differs from support performance_rows")
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
    family = _keys(value["statistical_family"], ("members", "sha256"),
                   "population.statistical_family")
    members = _list(family["members"], "population.statistical_family.members")
    if any(type(member) is not str or not member for member in members):
        _fail("population.statistical_family.members must be non-empty strings")
    if members != sorted(set(members)):
        _fail("population.statistical_family.members must be unique and sorted")
    _sha(family["sha256"], "population.statistical_family.sha256")
    if family["sha256"] != _canonical_json_digest(members):
        _fail("population.statistical_family.sha256 does not match members")
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
        item = _keys(closure[kind], ("name", "sha256"),
                     f"requested_work.closure.{kind}")
        _token(item["name"], f"requested_work.closure.{kind}.name")
        _sha(item["sha256"], f"requested_work.closure.{kind}.sha256")
        matches = [candidate for candidate in by_kind[kind]
                   if candidate["name"] == item["name"]
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
    profile = _keys(value["profile"], ("id", "version", "descriptor", "digest"),
                    "execution.profile")
    _token(profile["id"], "execution.profile.id")
    _token(profile["version"], "execution.profile.version")
    _sha(profile["digest"], "execution.profile.digest")
    _artifact(profile["descriptor"], "execution.profile.descriptor")
    if profile["digest"] != profile["descriptor"]["sha256"]:
        _fail("execution.profile.digest differs from its descriptor")
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
    value = _keys(value, ("schema", "version", "relation_receipt", "replay_receipt"),
                  "provenance")
    if value["schema"] != PROVENANCE_SCHEMA:
        _fail("provenance.schema is not the approved receipt schema")
    if type(value["version"]) is not int or value["version"] != PROVENANCE_VERSION:
        _fail(f"provenance.version must be exactly {PROVENANCE_VERSION}")
    _artifact(value["relation_receipt"], "provenance.relation_receipt")
    _artifact(value["replay_receipt"], "provenance.replay_receipt")
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
        return json.loads(target.read_text(encoding="utf-8"))
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

    replay = _read_json_evidence(root, provenance["replay_receipt"],
                                  "provenance.replay_receipt")
    replay = _keys(replay, ("schema", "version", "publisher", "bundle_sha256",
                            "contract_source_commit", "contract_source_tree",
                            "candidate_source_commit", "candidate_source_tree",
                            "replayed", "result"), "replay_receipt")
    if replay["schema"] != REPLAY_SCHEMA or type(replay["version"]) is not int \
            or replay["version"] != REPLAY_VERSION:
        _fail("replay_receipt does not identify the versioned #510 replay protocol")
    if replay["publisher"] != "native-retirement-evidence-v1":
        _fail("replay_receipt.publisher must identify the #510 evidence producer")
    _sha(replay["bundle_sha256"], "replay_receipt.bundle_sha256")
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
                                                "invalid_data"), "rules.uncertainty")
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


def _all_artifacts(binding):
    support = binding["support"]
    artifacts = []
    artifacts.extend((f"support.files[{index}]", item)
                     for index, item in enumerate(support["files"]))
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
                      ("provenance.replay_receipt", provenance["replay_receipt"])))
    return artifacts


def validate(binding_path, evidence_root=None):
    """Validate one binding record and optionally its referenced evidence files."""
    binding_path = Path(binding_path)
    try:
        with binding_path.open(encoding="utf-8") as stream:
            binding = json.load(stream)
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
    if evidence_root is not None:
        for name, artifact in artifacts:
            _check_evidence(evidence_root, artifact, name)
        rows_artifact = support["files"][SUPPORT_FILE_ROLES.index("performance_rows")]
        rows = _performance_rows(_evidence_bytes(evidence_root, rows_artifact,
                                                  "support.files.performance_rows"))
        _population(binding["population"], support, rows)
        _check_provenance_evidence(evidence_root, binding, provenance)
        rows_recomputed = True
        provenance_checked = True
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
        "proof": "evidence-and-receipts" if evidence_root is not None else "structural-only",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binding", type=Path)
    parser.add_argument("--evidence-root", type=Path)
    arguments = parser.parse_args()
    print(json.dumps(validate(arguments.binding, arguments.evidence_root), sort_keys=True))


if __name__ == "__main__":
    main()
