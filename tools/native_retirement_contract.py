#!/usr/bin/env python3
"""Independently validate and reconcile native-retirement census v2 evidence.

Applicability/admission is derived here as schema-2 evidence so the compiler
producer cannot classify away a candidate failure.
"""

import argparse
import csv
import hashlib
import json
import shlex
import struct
from collections import Counter
from pathlib import Path

import native_retirement_dependency_binding as dependency_authority
import native_retirement_materializer as dependency_materializer


ALLOCATORS = ("none", "mir-stack", "fast", "quality")
HOSTED_FIXTURES = frozenset("tests/" + name + ".c" for name in (
    "basic_c_target_headers", "basic_cjson_roundtrip", "basic_doom_headless",
    "basic_lz4_roundtrip", "basic_stb_compat", "basic_yyjson_roundtrip", "basic_zlib_compat"))
FULL_CENSUS_PROFILE = "full-census"
SELF_TEST_PROFILE = "self-test"
FULL_SUBJECT_COUNT = 411
FULL_ROW_COUNT = 78912
FULL_SHARD_COUNT = 4
FULL_SUPPORTED_GAP_COUNT = 192
# This is the SHA-256 of the canonical JSON list of full-census row numbers
# declared as supported-native gaps by the frozen support ledger.  The value is
# deliberately part of the validator contract; a producer cannot change it by
# renaming a result disposition or by editing a manifest claim.
FULL_SUPPORTED_GAP_SHA256 = "0f531b1cf7c7922ea891e15703971bcb2ddf95f398f628e0b2681831d7cbf81e"
FULL_SUPPORT_CONTRACT_SHA256 = "c61bbde58c471dc0d50853f8797e05ccd1737521d342dc7376669d90e192f5b8"
NEXT_SUPPORT_CONTRACT_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"
PROPOSED_SUPPORT_CONTRACT_SHA256 = "0d878bf0a3df9f0528803a5b08275d950f9edda57e373fd7618229dee264e427"
SUPPORTED_OBJECT_OBLIGATION = "supported-object-zero-fallback"
NON_OBJECT_CONTROL_OBLIGATION = "registered-non-object-control"
# Applicability is a validator-owned projection of the immutable row identity
# and observed result.  It deliberately does not appear in rows.tsv/results.tsv:
# those files are producer evidence, while this projection is derived here so a
# child cannot opt a supported cell out by supplying a convenient label.
APPLICABILITY_CLASSES = ("admitted-supported", "retained-control", "retained-reference",
                         "platform-inapplicable", "unavailable")
AUTHENTICATED_APPLICABILITY_CLASSES = ("admitted-supported", "platform-inapplicable", "unavailable")
MAX_RESIDUAL_ROWS = 256
SUPPORTED_GAP_LEDGER_FIELDS = ("fixture", "target", "frontend_lowering", "PIC", "allocator", "admission", "reason")
FULL_SUPPORTED_GAP_LEDGER_SHA256 = "e67ef103035b1b99e97ae640de2ef0b7a84add2705758cb2431a4855b303dfc3"
APPLICABILITY_LEDGER_FIELDS = ("fixture", "target", "fixture_sha256", "applicability", "reason")
FULL_APPLICABILITY_LEDGER_COUNT = 374
FULL_APPLICABILITY_LEDGER_SHA256 = "934be981e866fe3dbbdb4a5b9e551c052b4546487bb04245fac24bb271be78fa"
LEGACY_DEPENDENCY_DESCRIPTOR_SHA256 = "33be3c1582858afb570298ae49db193293e7ec2008d3a6b85f03df3485dea803"
LEGACY_DEPENDENCY_RECEIPT_SHA256 = "dc14e25a42f9000071d46c776f43282852bcebbf392a91089afe6b7e46aed55d"
LEGACY_DEPENDENCY_PROJECT_SHA256 = "542c978ad5f8252917fb0fd93cdd318ac8fcca9db14ffa1093edb606a8d637a2"
LEGACY_DEPENDENCY_LEDGER_SHA256 = "fa98a21ebeeede091e8810034b315d12c66f629ba2d5e2c4225b5f96fc1ce48a"
_DEPENDENCY_ROOT = Path(__file__).resolve().parents[1]
_DEPENDENCY_BINDING, _DEPENDENCY_RESOLVED_MANIFEST, _DEPENDENCY_SNAPSHOT = dependency_authority.load_authority(_DEPENDENCY_ROOT)
FULL_DEPENDENCY_DESCRIPTOR_SHA256 = _DEPENDENCY_BINDING["policy_sha256"]
FULL_DEPENDENCY_RECEIPT_SHA256 = _DEPENDENCY_BINDING["receipt_sha256"]
FULL_DEPENDENCY_PROJECT_SHA256 = _DEPENDENCY_BINDING["project_sha256"]
FULL_DEPENDENCY_LEDGER_SHA256 = _DEPENDENCY_BINDING["ledger_sha256"]
FULL_EXTERNAL_CHECKOUTS = (
    {"name": "cjson", "repository": "DaveGamble/cJSON", "revision": "c859b25da02955fef659d658b8f324b5cde87be3", "path": "external/cjson"},
    {"name": "doom", "repository": "ozkl/doomgeneric", "revision": "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284", "path": "external/doom"},
    {"name": "lz4", "repository": "lz4/lz4", "revision": "ebb370ca83af193212df4dcbadcc5d87bc0de2f0", "path": "external/lz4"},
    {"name": "yyjson", "repository": "ibireme/yyjson", "revision": "8b4a38dc994a110abaec8a400615567bd996105f", "path": "external/yyjson"},
    {"name": "stb", "repository": "nothings/stb", "revision": "2c980bb59875b0d32144a71867fbdebb2f77cd20", "path": "external/stb"},
    {"name": "zlib", "repository": "madler/zlib", "revision": "51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf", "path": "external/zlib"},
    {"name": "musl", "repository": "ifduyue/musl", "revision": "9fa28ece75d8a2191de7c5bb53bed224c5947417", "path": "external/musl"},
)
FULL_EXTERNAL_GENERATED = (
    {"name": "musl-x86_64", "checkout": "musl", "revision": "9fa28ece75d8a2191de7c5bb53bed224c5947417", "path": "external/musl-generated/x86_64", "generator": "sed:tools/mkalltypes.sed+arch/x86_64/bits/alltypes.h.in+include/alltypes.h.in;syscall-sed"},
    {"name": "musl-aarch64", "checkout": "musl", "revision": "9fa28ece75d8a2191de7c5bb53bed224c5947417", "path": "external/musl-generated/aarch64", "generator": "sed:tools/mkalltypes.sed+arch/aarch64/bits/alltypes.h.in+include/alltypes.h.in;syscall-sed"},
)
FULL_ARCHIVED_INPUT_SHA256 = "bef841ade0921ffe9293440171b1d0d8dd6c3cf798f2535d8790b4ad26542500"
FULL_ARCHIVED_FIXTURE_MAP_SHA256 = "8d79504f67d48fd27698c6897b00fc9347dd60a538a6198e53e42970c799bc4f"
FULL_ARCHIVED_ROW_SHA256 = "9604102b75a14631aeb1d6a3652d36506a05928a0046c52cc50a00b942826ce6"
APPLICABILITY_FIELDS = ("row", "group", "fixture", "target", "cpu", "frontend", "allocator", "PIC",
                        "applicability", "admission", "disposition", "reason", "ownership",
                        "candidate_failure", "reference_failure", "acceptance_failure")
APPLICABILITY_SKIP_FIELDS = ("row", "group", "fixture", "target", "allocator", "applicability", "reason")
RESIDUAL_FIELDS = ("row", "group", "fixture", "function", "function_id", "target", "cpu", "frontend",
                   "allocator", "PIC", "applicability", "admission", "disposition", "reason", "ownership",
                   "diagnostic", "stage", "opcode_id", "source_hex", "function_hex", "line", "column")
TARGETS = {
    "x86_64-unknown-linux-gnu": ("systemv-x86_64", "semantic-gate-509", "semantic-gate-509"),
    "aarch64-unknown-linux-gnu": ("aapcs64", "semantic-gate-509", "semantic-gate-509"),
    "x86_64-pc-windows-msvc": ("win64-x86_64", "semantic-gate-509", "semantic-gate-509"),
    "aarch64-pc-windows-msvc": ("windows-aarch64", "semantic-gate-509", "semantic-gate-509"),
    "x86_64-apple-macos": ("systemv-x86_64", "semantic-gate-509", "semantic-gate-509"),
    "aarch64-apple-macos": ("darwin-aarch64", "semantic-gate-509", "semantic-gate-509"),
    "x86_64-linux-android": ("systemv-x86_64", "semantic-gate-509", "semantic-gate-509"),
    "aarch64-linux-android": ("aapcs64", "semantic-gate-509", "semantic-gate-509"),
    "x86_64-apple-ios": ("systemv-x86_64", "semantic-gate-509", "unavailable-platform-control"),
    "aarch64-apple-ios": ("darwin-aarch64", "semantic-gate-509", "semantic-gate-509"),
    "x86_64-unknown-uefi": ("win64-x86_64", "semantic-gate-509", "semantic-gate-509"),
    "aarch64-unknown-uefi": ("aapcs64", "semantic-gate-509", "semantic-gate-509"),
}
IDENTITY_FIELDS = ("fixture", "target", "target_abi", "cpu", "cpu_features", "allocator",
                   "frontend_lowering", "PIC", "fixture_recipe", "compile_obligation",
                   "link_obligation", "execution_obligation", "diagnostic_obligation")
INPUT_FIELDS = ("path", "role", "compile_obligation", "bytes", "buster_hash_64", "sha256",
                "fixture_recipe", "fixture_flags")
SUPPORT_FIELDS = ("path", "role", "compile_obligation", "bytes", "sha256")
ROW_FIELDS = ("row", "group", "fixture", "target", "target_abi", "cpu", "cpu_features", "allocator",
              "frontend_lowering", "PIC", "selected", "fixture_recipe", "compile_obligation", "link_obligation",
              "execution_obligation", "diagnostic_obligation", "argv_evidence")
# ``selected`` is intentionally shard-local; every other rows.tsv field is
# part of the cross-shard identity map and digest.
ROW_IDENTITY_FIELDS = tuple(field for field in ROW_FIELDS if field != "selected")
RESULT_FIELDS = ("row", "group", "disposition", "kind", "status", "counters_valid", "target_identity_valid",
                 "function_records_valid", "functions", "fallbacks", "baseline_functions", "cpu", "cpu_features",
                 "object_bytes", "object_hash", "object_sha256")
FIXTURE_RECIPES = {
    "tests/basic_c_constexpr.c": ("c23", ("-std=c23",)),
    "tests/basic_c_constexpr_leaf.c": ("c23", ("-std=c23",)),
    "tests/basic_c_nullptr.c": ("c23", ("-std=c23",)),
    "tests/basic_c_typeof.c": ("c23", ("-std=c23",)),
    "tests/basic_c_dialect.c": ("c23-dialect-assertions",
                                 ("-std=c23", "-DEXPECTED_STDC_VERSION=202311L", "-DEXPECTED_GNU=0")),
    "tests/basic_c_predicate_bank.c": ("x86-avx512", ()),
    "tests/basic_c_atomic_aggregate.c": ("x86-cx16", ()),
}
FIXTURE_X86_CPUS = {
    "tests/basic_c_predicate_bank.c": "skylake-avx512",
    "tests/basic_c_atomic_aggregate.c": "haswell",
}


def canonical_rows_digest(rows):
    """Hash sorted row numbers with one canonical JSON representation."""
    return canonical_digest(sorted(int(row) for row in rows))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def table(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def table_with_fields(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        return tuple(reader.fieldnames or ()), list(reader)


def write_table(path, fields, rows):
    """Write a deterministic TSV evidence table.

    The census producer writes its evidence in C, but admission/applicability
    is intentionally a Python-side projection.  Keeping one small writer here
    makes the projection byte-stable across runners and avoids locale/newline
    differences in the retained evidence.
    """
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n",
                                extrasaction="raise")
        writer.writeheader()
        writer.writerows(rows)


def canonical_digest(value):
    encoded = json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def field_map(rows, fields, key_field):
    return {row[key_field]: tuple(row[field] for field in fields if field != key_field)
            for row in sorted(rows, key=lambda row: int(row[key_field]))}


def ledger_map(rows):
    return {row["path"]: tuple(row[field] for field in INPUT_FIELDS if field != "path")
            for row in sorted(rows, key=lambda row: row["path"])}


def canonical_map_digest(values):
    return canonical_digest({key: list(value) for key, value in sorted(values.items(), key=lambda item: item[0])})


def stable_manifest(manifest):
    return {key: value for key, value in sorted(manifest.items()) if key != "shard_index"}


def manifest_identity_digest(manifest):
    return canonical_digest(stable_manifest(manifest))


def expected_fixture_recipe(path):
    return FIXTURE_RECIPES.get(path, ("compiler-default", ()))


def expected_cpu(fixture, target, fallback):
    if target.startswith("x86_64-") and fixture in FIXTURE_X86_CPUS:
        return FIXTURE_X86_CPUS[fixture]
    return fallback


def unsigned_decimal(value, bits, field):
    assert value and value.isascii() and value.isdecimal(), f"invalid {field}: {value!r}"
    number = int(value)
    assert str(number) == value and number < 1 << bits, f"non-canonical or out-of-range {field}: {value!r}"
    return number


def properties(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        assert separator and key and key not in values, f"invalid property in {path}: {line!r}"
        values[key] = value
    return values


def relative_path(value):
    path = Path(value)
    assert value and not path.is_absolute() and ".." not in path.parts, f"unsafe relative path: {value!r}"
    return path


def exact_sha(path, size, digest):
    assert path.is_file() and not path.is_symlink(), path
    assert path.stat().st_size == int(size), path
    assert sha256(path) == digest, path


def validate_environment(directory):
    rows = table(directory / "environment.tsv")
    observed = {row["name"]: (row["present"], row["value"]) for row in rows}
    assert len(observed) == len(rows), "duplicate environment entry"
    posix = {"LC_ALL": ("1", "C"), "LANG": ("1", "C"), "TZ": ("1", "UTC")}
    if set(observed) != set(posix):
        assert set(observed) == set(posix) | {"SystemRoot", "TEMP", "TMP"}, observed
        assert observed["SystemRoot"][0] == "1" and observed["SystemRoot"][1]
        for name in ("TEMP", "TMP"):
            assert observed[name][0] == "1" and Path(observed[name][1]).name == "temporary"
    assert all(observed[name] == value for name, value in posix.items()), observed


def validate_dependencies(directory, manifest):
    rows = table(directory / "dependencies.tsv")
    assert rows and all(row["kind"] in {"resource-header", "project-header"} for row in rows)
    include_namespace = {}
    for row in rows:
        path = row["path"]
        relative = relative_path(path)
        assert relative.as_posix() == path and not path.startswith("dependencies/"), \
            f"dependency path is not include-relative: {path!r}"
        if "destination" in row:
            root_name = "resource-include" if row["kind"] == "resource-header" else "project-include"
            assert row["destination"] == f"dependencies/{root_name}/{path}", \
                f"dependency kind/root mismatch: {path!r}"
        previous = include_namespace.get(path)
        assert previous is None, f"global include namespace collision: {path}"
        include_namespace[path] = row["kind"]
    for kind in ("resource-header", "project-header"):
        selected = [row for row in rows if row["kind"] == kind]
        assert [row["path"] for row in selected] == sorted(row["path"] for row in selected)
        assert len({row["path"] for row in selected}) == len(selected)
        closure = hashlib.sha256()
        expected_files = set()
        root_name = "resource-include" if kind == "resource-header" else "project-include"
        root = directory / "dependencies" / root_name
        for row in selected:
            relative = relative_path(row["path"])
            path = root / relative
            exact_sha(path, row["bytes"], row["sha256"])
            data = path.read_bytes()
            closure.update(row["path"].encode())
            closure.update(b"\0")
            closure.update(struct.pack("<Q", len(data)))
            closure.update(data)
            expected_files.add(relative.as_posix())
        actual_files = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()} if root.is_dir() else set()
        assert actual_files == expected_files, f"{kind} snapshot and ledger differ"
        manifest_key = "resource_include_sha256" if kind == "resource-header" else "project_include_sha256"
        if selected:
            assert closure.hexdigest() == manifest.get(manifest_key, "")
        else:
            assert not manifest.get(manifest_key, "")


def validate_dependency_binding(directory, manifest, profile, inputs):
    required = profile == FULL_CENSUS_PROFILE or bool(manifest.get("project_include_sha256", ""))
    if not required:
        return
    assert manifest.get("dependency_manifest") == dependency_authority.POLICY_PATH
    assert manifest.get("dependency_receipt") == "dependency-receipt.json"
    receipt_path = directory / "dependency-receipt.json"
    ledger = directory / "dependency-materializer.tsv"
    policy_path = directory / "dependency-policy.json"
    snapshot_path = directory / "dependency-source-snapshot.json"
    resolved_path = directory / "dependency-resolved-descriptor.json"
    live = policy_path.is_file() or snapshot_path.is_file() or resolved_path.is_file()
    if live:
        assert policy_path.is_file() and snapshot_path.is_file() and resolved_path.is_file()
        policy_raw = policy_path.read_bytes()
        snapshot_raw = snapshot_path.read_bytes()
        resolved_raw = resolved_path.read_bytes()
        assert sha256(policy_path) == manifest.get("dependency_manifest_sha256") == FULL_DEPENDENCY_DESCRIPTOR_SHA256
        assert sha256(snapshot_path) == manifest.get("dependency_snapshot_sha256") == _DEPENDENCY_BINDING["snapshot_sha256"]
        assert manifest.get("dependency_snapshot") == dependency_authority.SNAPSHOT_PATH
        policy = dependency_authority.parse_policy(policy_raw)
        snapshot = dependency_authority.parse_snapshot(snapshot_raw, policy_raw, policy)
        reconstructed = dependency_authority.render_resolved_descriptor(policy, snapshot)
        assert resolved_raw == reconstructed
        assert hashlib.sha256(resolved_raw).hexdigest() == manifest.get("dependency_resolved_descriptor_sha256")
        descriptor_value = dependency_authority.strict_json(resolved_raw, "evidence resolved dependency descriptor")
        records, _metadata = dependency_materializer.parse_manifest(descriptor_value)
        assert ledger.read_bytes() == dependency_materializer._ledger(records)
        assert descriptor_value == _DEPENDENCY_RESOLVED_MANIFEST
        assert snapshot == _DEPENDENCY_SNAPSHOT
        expected_receipt_sha256 = FULL_DEPENDENCY_RECEIPT_SHA256
        expected_project_sha256 = FULL_DEPENDENCY_PROJECT_SHA256
        expected_ledger_sha256 = FULL_DEPENDENCY_LEDGER_SHA256
    else:
        descriptor = directory / "dependency-descriptor.json"
        assert sha256(descriptor) == manifest.get("dependency_manifest_sha256") == LEGACY_DEPENDENCY_DESCRIPTOR_SHA256
        descriptor_value = json.loads(descriptor.read_text(encoding="utf-8"))
        expected_receipt_sha256 = LEGACY_DEPENDENCY_RECEIPT_SHA256
        expected_project_sha256 = LEGACY_DEPENDENCY_PROJECT_SHA256
        expected_ledger_sha256 = LEGACY_DEPENDENCY_LEDGER_SHA256
    assert sha256(receipt_path) == manifest.get("dependency_receipt_sha256") == expected_receipt_sha256
    assert sha256(ledger) == manifest.get("dependency_ledger_sha256") == expected_ledger_sha256
    assert descriptor_value.get("external_checkouts") == list(FULL_EXTERNAL_CHECKOUTS)
    assert descriptor_value.get("external_generated") == list(FULL_EXTERNAL_GENERATED)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    assert receipt.get("schema") == "buster-native-retirement-dependencies-v1" and receipt.get("version") == 1
    assert receipt.get("descriptor_path") == dependency_authority.POLICY_PATH
    if live:
        assert receipt.get("descriptor_sha256") == manifest.get("dependency_resolved_descriptor_sha256")
    else:
        assert receipt.get("descriptor_sha256") == LEGACY_DEPENDENCY_DESCRIPTOR_SHA256
    assert receipt.get("project_include_sha256") == expected_project_sha256
    assert receipt.get("ledger_sha256") == expected_ledger_sha256
    assert receipt.get("external_checkouts") == list(FULL_EXTERNAL_CHECKOUTS)
    assert receipt.get("external_generated") == list(FULL_EXTERNAL_GENERATED)
    assert receipt.get("external_closure_sha256") == canonical_digest({
        "external_checkouts": list(FULL_EXTERNAL_CHECKOUTS),
        "external_generated": list(FULL_EXTERNAL_GENERATED),
    })
    assert manifest.get("dependency_project_include_sha256") == expected_project_sha256
    assert manifest.get("archived_input_identity_sha256") == FULL_ARCHIVED_INPUT_SHA256
    assert manifest.get("archived_fixture_map_sha256") == FULL_ARCHIVED_FIXTURE_MAP_SHA256
    assert manifest.get("archived_row_identity_sha256") == FULL_ARCHIVED_ROW_SHA256
    replay = receipt.get("archived_replay", {})
    expected_axes = {
        "targets": list(TARGETS),
        "frontend_lowering": ["local-backed-canonical", "direct-ssa"],
        "PIC": ["0", "1"],
        "allocators": list(ALLOCATORS[1:]),
    }
    assert replay.get("axes") == expected_axes
    assert replay.get("projection") == {
        "fixtures": 28,
        "mir_candidate_rows": 4032,
        "repo_owned_project_header_rows_closed": 264,
        "remaining_diagnostic_rows": 3768,
        "ios_simd_rows_pending_targetconditionals": 24,
    }
    assert replay.get("input_identity_sha256") == FULL_ARCHIVED_INPUT_SHA256
    assert replay.get("fixture_map_sha256") == FULL_ARCHIVED_FIXTURE_MAP_SHA256
    assert replay.get("row_identity_sha256") == FULL_ARCHIVED_ROW_SHA256
    fixtures = replay.get("fixtures")
    rows = replay.get("rows")
    assert isinstance(fixtures, list) and len(fixtures) == 28
    assert isinstance(rows, list) and len(rows) == 4032
    fixture_map = {item["fixture"]: item for item in fixtures}
    assert len(fixture_map) == len(fixtures)
    project_destinations = {
        path[len("dependencies/project-include/"):]
        for path in receipt.get("files_by_destination", [])
        if path.startswith("dependencies/project-include/")
    }
    for item in fixtures:
        fixture = item["fixture"]
        assert fixture in inputs and inputs[fixture]["role"] == "subject"
        assert inputs[fixture]["sha256"] == item["input_sha256"]
        assert item["project_headers"] and set(item["project_headers"]).issubset(project_destinations)
    assert canonical_digest({item["fixture"]: item["input_sha256"] for item in fixtures}) == replay["input_identity_sha256"]
    assert canonical_digest(fixtures) == replay["fixture_map_sha256"]
    assert canonical_digest(rows) == replay["row_identity_sha256"]
    assert [row["row"] for row in rows] == list(range(len(rows)))
    assert all(row["fixture"] in fixture_map for row in rows)
    assert sum(row["disposition"] == "repo-owned-project-header" for row in rows) == 264
    assert sum(row["disposition"] != "repo-owned-project-header" for row in rows) == 3768
    assert sum(row["disposition"] == "ios-simd-pending-targetconditionals" for row in rows) == 24
    for row in rows:
        expected_headers = fixture_map[row["fixture"]]["project_headers"] if row["disposition"] == "repo-owned-project-header" else []
        assert row["project_headers"] == expected_headers


def validate_inputs(directory, manifest):
    contract_path = directory / "support-contract.tsv"
    assert sha256(contract_path) == manifest["support_contract_sha256"]
    contract_fields, contract = table_with_fields(contract_path)
    input_fields, inputs = table_with_fields(directory / "inputs.tsv")
    assert contract_fields == SUPPORT_FIELDS
    assert input_fields == INPUT_FIELDS
    assert len(inputs) == int(manifest["inputs"]) == len(contract)
    contract_by_path = {row["path"]: row for row in contract}
    input_by_path = {row["path"]: row for row in inputs}
    assert len(contract_by_path) == len(contract)
    assert len(input_by_path) == len(inputs)
    assert set(contract_by_path) == set(input_by_path)
    for path in sorted(contract_by_path):
        approved = contract_by_path[path]
        row = input_by_path[path]
        assert tuple(approved[field] for field in SUPPORT_FIELDS) == tuple(row[field] for field in SUPPORT_FIELDS)
        exact_sha(directory / "inputs" / relative_path(row["path"]), row["bytes"], row["sha256"])
        recipe_name, recipe_flags = expected_fixture_recipe(row["path"])
        assert row["fixture_recipe"] == recipe_name and tuple(shlex.split(row["fixture_flags"])) == recipe_flags, \
            f"fixture recipe mismatch for {row['path']}"
    actual_files = {path.relative_to(directory / "inputs").as_posix()
                    for path in (directory / "inputs").rglob("*") if path.is_file()}
    assert actual_files == {row["path"] for row in inputs}, "input snapshot and ledger differ"
    return input_by_path, ledger_map(inputs)


def validate_supported_gap_ledger(directory, manifest, rows, profile):
    """Authenticate the immutable list of currently declared supported gaps.

    The producer may report any disposition text, but it cannot add, remove,
    or reclassify a gap: the checked-in ledger is copied into the evidence
    directory and its exact bytes are bound by the manifest.  The expanded
    row identities are checked against the frozen rows.tsv cross-product.
    """
    path = directory / "supported-gap-ledger.tsv"
    ledger_sha256 = sha256(path)
    assert manifest.get("supported_gap_ledger") == "docs/native-retirement-supported-gaps-v1.tsv"
    assert manifest.get("supported_gap_ledger_sha256") == ledger_sha256
    if profile == FULL_CENSUS_PROFILE:
        assert ledger_sha256 == FULL_SUPPORTED_GAP_LEDGER_SHA256
    fields, records = table_with_fields(path)
    assert fields == SUPPORTED_GAP_LEDGER_FIELDS
    identities = []
    for record in records:
        assert record["admission"] == "admitted-supported"
        assert record["reason"] == "supported-object-zero-fallback"
        assert record["allocator"] in ALLOCATORS[1:]
        assert record["frontend_lowering"] in {"local-backed-canonical", "direct-ssa"}
        assert record["PIC"] in {"0", "1"}
        identity = tuple(record[field] for field in SUPPORTED_GAP_LEDGER_FIELDS[:5])
        assert identity not in identities
        identities.append(identity)
    assert identities == sorted(identities), "supported-gap ledger is not canonically sorted"
    row_by_identity = {
        tuple(row[field] for field in ("fixture", "target", "frontend_lowering", "PIC", "allocator")): row
        for row in rows
    }
    assert len(row_by_identity) == len(rows), "rows.tsv contains duplicate census identities"
    gap_row_numbers = []
    for identity in identities:
        assert identity in row_by_identity, f"supported-gap ledger references unknown row: {identity!r}"
        row = row_by_identity[identity]
        assert row["allocator"] != "none" and row["compile_obligation"] == SUPPORTED_OBJECT_OBLIGATION
        gap_row_numbers.append(int(row["row"]))
    gap_row_numbers.sort()
    if profile == FULL_CENSUS_PROFILE:
        assert len(records) == FULL_SUPPORTED_GAP_COUNT
        assert canonical_rows_digest(gap_row_numbers) == FULL_SUPPORTED_GAP_SHA256
    return set(identities), set(gap_row_numbers), ledger_sha256


def validate_applicability_ledger(directory, manifest, rows, inputs, profile, gap_identities):
    """Authenticate the immutable fixture/target applicability projection.

    This ledger is deliberately keyed by the immutable fixture identity and
    target triple, rather than by a result row or producer disposition.  A
    result can therefore report any prose it likes without changing whether
    a target-specific fixture is admitted.  The full profile binds the exact
    checked-in bytes and count; self-tests may use a smaller explicit ledger.
    """
    path = directory / "applicability-ledger.tsv"
    assert path.is_file() and not path.is_symlink(), path
    ledger_sha256 = sha256(path)
    assert manifest.get("applicability_ledger") == "docs/native-retirement-applicability-v1.tsv"
    assert manifest.get("applicability_ledger_sha256") == ledger_sha256
    fields, records = table_with_fields(path)
    assert fields == APPLICABILITY_LEDGER_FIELDS
    if profile == FULL_CENSUS_PROFILE:
        assert ledger_sha256 == FULL_APPLICABILITY_LEDGER_SHA256
        assert len(records) == FULL_APPLICABILITY_LEDGER_COUNT
    if "applicability_ledger_entries" in manifest:
        assert manifest["applicability_ledger_entries"] == str(len(records))
    input_hashes = {path: row["sha256"] for path, row in inputs.items() if row["role"] == "subject"}
    gap_pairs = {(identity[0], identity[1]) for identity in gap_identities}
    identities = []
    projection = {}
    for record in records:
        fixture = record["fixture"]
        target = record["target"]
        classification = record["applicability"]
        reason = record["reason"]
        assert fixture in input_hashes, f"applicability ledger references unknown subject: {fixture!r}"
        assert target in TARGETS, f"applicability ledger references unknown target: {target!r}"
        assert record["fixture_sha256"] == input_hashes[fixture], fixture
        assert classification in AUTHENTICATED_APPLICABILITY_CLASSES
        assert reason and reason.isascii() and all(character.isalnum() or character in "-._" for character in reason)
        identity = (fixture, target)
        assert identity not in projection, f"duplicate applicability identity: {identity!r}"
        if identity in gap_pairs:
            assert classification == "admitted-supported", \
                f"supported gap was reclassified by applicability ledger: {identity!r}"
        identities.append(identity)
        projection[identity] = (classification, reason)
    assert identities == sorted(identities), "applicability ledger is not canonically sorted"
    return projection, ledger_sha256


def expected_nonexecuted(row, authenticated_class, authenticated_reason):
    """Return the source-authenticated non-execution disposition for a row.

    Fixture/target applicability is the only source for platform/unavailable
    skips.  Whole-fixture non-object controls are independently authenticated
    by the support contract.  A result disposition never participates in this
    decision.
    """
    if row["compile_obligation"] == NON_OBJECT_CONTROL_OBLIGATION:
        return "retained-control", NON_OBJECT_CONTROL_OBLIGATION
    if authenticated_class in {"platform-inapplicable", "unavailable"}:
        return authenticated_class, authenticated_reason
    return None


def validate_skip_provenance(directory, rows, by_result, applicability_ledger):
    """Validate explicit non-executed rows against source-owned identities."""
    path = directory / "applicability-skips.tsv"
    assert path.is_file() and not path.is_symlink(), path
    fields, records = table_with_fields(path)
    assert fields == APPLICABILITY_SKIP_FIELDS
    by_row = {row["row"]: row for row in rows}
    expected = {}
    for key in by_result:
        row = by_row[key]
        auth_class, auth_reason = applicability_ledger.get((row["fixture"], row["target"]), ("", ""))
        skip = expected_nonexecuted(row, auth_class, auth_reason)
        if skip:
            expected[key] = skip
    observed = {}
    for record in records:
        key = record["row"]
        assert key in by_result, f"skip provenance references unselected row: {key}"
        assert key not in observed, f"duplicate skip provenance row: {key}"
        row = by_row[key]
        assert record["group"] == row["group"]
        assert record["fixture"] == row["fixture"]
        assert record["target"] == row["target"]
        assert record["allocator"] == row["allocator"]
        assert key in expected, f"skip provenance is not source-authenticated: {key}"
        assert (record["applicability"], record["reason"]) == expected[key]
        observed[key] = (record["applicability"], record["reason"])
    assert set(observed) == set(expected), "authenticated non-executed rows lack skip provenance"
    return expected


def validate_profile(manifest, inputs, row_count):
    """Validate the declared inventory shape before reading result labels."""
    profile = manifest.get("profile", "")
    assert profile in {FULL_CENSUS_PROFILE, SELF_TEST_PROFILE}, \
        f"unknown census profile: {profile!r}"
    subjects = sum(row["role"] == "subject" for row in inputs.values())
    if profile == FULL_CENSUS_PROFILE:
        assert manifest.get("support_contract") == "docs/native-retirement-support-v1.tsv"
        assert manifest.get("support_contract_sha256") in (
            FULL_SUPPORT_CONTRACT_SHA256, NEXT_SUPPORT_CONTRACT_SHA256,
            PROPOSED_SUPPORT_CONTRACT_SHA256)
        assert manifest.get("inputs") == "559"
        assert manifest.get("shard_count") == str(FULL_SHARD_COUNT)
        assert manifest.get("fixture_filter", "") == "" and manifest.get("target_filter", "") == ""
        assert manifest.get("subjects") == str(FULL_SUBJECT_COUNT)
        assert subjects == FULL_SUBJECT_COUNT, "full census subject inventory is incomplete"
        assert row_count == FULL_ROW_COUNT, "full census row count is incomplete"
    elif profile == SELF_TEST_PROFILE:
        # Small fixtures are intentionally accepted only with this explicit
        # profile; they may not masquerade as the full reviewed subject inventory.
        assert subjects and row_count
    return profile, subjects


def read_argv(path):
    raw = path.read_bytes()
    assert raw.endswith(b"\0") and b"\0\0" not in raw
    return [item.decode("utf-8") for item in raw[:-1].split(b"\0")]


def validate_argv(directory, manifest, row, recipes):
    argv_path = directory / relative_path(row["argv_evidence"])
    argv = read_argv(argv_path)
    baseline = row["allocator"] == "none"
    recorded_root = Path(argv[0]).parent
    assert recorded_root.is_absolute() and recorded_root.name == directory.name
    executable = "baseline-ide.exe" if baseline else "candidate-ide.exe"
    lowering = "-ffrontend-ssa" if row["frontend_lowering"] == "direct-ssa" else "-fno-frontend-ssa"
    expected = [str(recorded_root / executable), "cc", "-c", "-g0", "-v", "-fwrapv",
                "-fno-strict-aliasing", "-funsigned-char", "-target", row["target"],
                "-mcpu=" + row["cpu"], "-fPIC" if row["PIC"] == "1" else "-fno-pic",
                lowering, "-fregister-allocator=" + row["allocator"], "-fverify-codegen",
                "-fmachine-fallback" if baseline else "-fno-machine-fallback", "-nostdinc",
                "-isystem", str(recorded_root / "dependencies" / "resource-include"),
                ]
    if manifest.get("project_include_sha256", ""):
        if row["target"] in {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"}:
            arch = "x86_64" if row["target"].startswith("x86_64-") else "aarch64"
            expected.extend(["-isystem", str(recorded_root / "dependencies" / "project-include" / "musl" / arch / "include"),
                             "-isystem", str(recorded_root / "dependencies" / "project-include" / "musl" / "include")])
        if row["fixture"] in HOSTED_FIXTURES:
            sdk_root = recorded_root / "dependencies" / "project-include" / "sdk"
            target = row["target"]
            if "windows" in target:
                expected.append("-U__GNUC__")
                if target.startswith("x86_64-"):
                    expected.append("-D__x86_64=1")
                expected.extend(["-isystem", str(sdk_root / "mingw-adapter"),
                                 "-isystem", str(sdk_root / "windows")])
            elif "apple" in target:
                expected.extend(["-isystem", str(sdk_root / "darwin")])
            elif "android" in target:
                arch = target.split("-", 1)[0]
                expected.extend(["-isystem", str(sdk_root / "android" / (arch + "-linux-android")),
                                 "-isystem", str(sdk_root / "android")])
        expected.append("-I" + str(recorded_root / "inputs" / "tests"))
        expected.append("-I" + str(recorded_root / "dependencies" / "project-include"))
    else:
        expected.append("-I" + str(recorded_root / "inputs" / "tests"))
    expected.extend([str(recorded_root / "inputs" / row["fixture"]), "-o",
                str(recorded_root / "groups" / row["group"] / (row["allocator"] + ".o"))])
    expected.extend(recipes[row["fixture"]])
    if not baseline:
        expected.append("-fcodegen-fallback-census")
    assert argv == expected, f"argv mismatch for row {row['row']}"


def classify_result(row, result, expected_baseline_functions, baseline_unresolved=False, skip_expected=False):
    """Classify an executed row from structured evidence, never its label.

    ``disposition`` is retained as producer text for diagnostics only.  It is
    intentionally absent from every gate decision below: changing that free
    form string cannot turn a failed object into a reference-only row or a
    supported gap.
    """
    allocator = row["allocator"]
    disposition = result["disposition"]
    fallbacks = unsigned_decimal(result["fallbacks"], 32, "fallbacks")
    telemetry_fields = {
        "counters_valid": result["counters_valid"],
        "target_identity_valid": result["target_identity_valid"],
        "function_records_valid": result["function_records_valid"],
    }
    telemetry_defects = sorted(name for name, value in telemetry_fields.items() if value != "1")
    functions = unsigned_decimal(result["functions"], 32, "functions")
    baseline_functions = unsigned_decimal(result["baseline_functions"], 32, "baseline_functions")
    object_bytes = unsigned_decimal(result["object_bytes"], 64, "object_bytes")
    baseline_count_defect = not skip_expected and baseline_functions != expected_baseline_functions
    # The object census does not observe native program execution.  Preserve
    # this row-level obligation as metadata, while keeping every compiler,
    # process, object, fallback and telemetry check below active.
    inapplicable = row["execution_obligation"] == "unavailable-platform-control"
    execution_defect = result["kind"] != "0" or result["status"] != "0"
    fallback_defect = fallbacks != 0
    artifact_defect = object_bytes == 0
    # A successful direct reference is the function-count oracle for MIR.
    # When that reference is unresolved, its partial/zero count is not an
    # authenticated shape for the candidate: the candidate's own object and
    # telemetry remain checked, but a different authenticated function count
    # is reference-only evidence.  Keep the baseline-count copy check above
    # row-bound, so a producer cannot silently substitute another reference.
    function_shape_defect = (not skip_expected and not baseline_unresolved and
                             functions != expected_baseline_functions)
    # The direct allocator is the immutable reference side.  A bad direct
    # object is an acceptance/reference failure, not a candidate failure.  MIR
    # rows are candidate-owned regardless of the producer's disposition text.
    if skip_expected:
        # A ledger-bound non-executed row has no compiler artifact or compiler
        # telemetry by design.  Its structural result shape is authenticated
        # by validate_skip_provenance; disposition text cannot select this
        # branch because the caller supplies this flag from the ledger and
        # source obligation only.
        artifact_defect = False
    telemetry_defect = (bool(telemetry_defects) or function_shape_defect or artifact_defect or
                        baseline_count_defect)
    evidence_defect = fallback_defect or telemetry_defect or execution_defect
    structural_candidate_failure = evidence_defect
    # The free-form disposition is retained as an observation only.  The
    # direct allocator is always the reference side, and every MIR allocator
    # is candidate-owned.  Baseline resolution is derived from its structured
    # process/object/telemetry evidence and then row-bound to the MIR group.
    if skip_expected:
        candidate_failure = False
        reference_failure = False
        side = "control"
    elif allocator == "none":
        candidate_failure = False
        reference_failure = evidence_defect
        side = "baseline"
    else:
        candidate_failure = structural_candidate_failure
        reference_failure = baseline_unresolved
        side = "candidate"
    unexpected_failure = candidate_failure
    return {
        "side": side,
        "disposition": disposition,
        "candidate_failure": candidate_failure,
        "reference_failure": reference_failure,
        "acceptance_failure": candidate_failure or reference_failure,
        "inapplicable": inapplicable,
        "fallback_defect": fallback_defect,
        "telemetry_defect": telemetry_defect,
        "execution_defect": execution_defect,
        "artifact_defect": artifact_defect,
        "unexpected_failure": candidate_failure,
        "structural_candidate_failure": structural_candidate_failure,
        "telemetry_fields": telemetry_defects,
    }


def classify_applicability(row, result, outcome, baseline_unresolved=False, declared_supported_gap=False,
                           authenticated_class="", authenticated_reason=""):
    """Derive admission only from the authenticated row/support ledger.

    ``result["disposition"]`` is deliberately not inspected.  Applicability
    remains stable when a producer forges that text.  An authenticated
    fixture/target class takes precedence over baseline state; retained
    control/reference classes are derived only when no such class is present.
    """
    if declared_supported_gap:
        assert not authenticated_class or authenticated_class == "admitted-supported"
        classification = "admitted-supported"
        reason = SUPPORTED_OBJECT_OBLIGATION
        ownership = "candidate-compiler"
    elif authenticated_class == "admitted-supported":
        classification = "admitted-supported"
        reason = authenticated_reason
        ownership = "applicability-manifest"
    elif authenticated_class in {"retained-control", "retained-reference", "platform-inapplicable", "unavailable"}:
        classification = authenticated_class
        reason = authenticated_reason
        ownership = "applicability-manifest"
    elif row["execution_obligation"] == "unavailable-platform-control":
        classification = "platform-inapplicable"
        reason = "native-execution-owner-unavailable"
        ownership = "platform-execution"
    elif row["compile_obligation"] == NON_OBJECT_CONTROL_OBLIGATION:
        classification = "retained-control"
        reason = NON_OBJECT_CONTROL_OBLIGATION
        ownership = "source-registration"
    elif row["compile_obligation"] != SUPPORTED_OBJECT_OBLIGATION:
        classification = "unavailable"
        reason = "compile-obligation-not-admitted"
        ownership = "admission"
    elif baseline_unresolved:
        classification = "retained-reference"
        reason = "direct-reference-unresolved"
        ownership = "reference-compiler"
    elif row["allocator"] == "none":
        classification = "retained-control"
        reason = "direct-reference-control"
        ownership = "reference-compiler"
    else:
        classification = "admitted-supported"
        reason = SUPPORTED_OBJECT_OBLIGATION
        ownership = "candidate-compiler"
    assert classification in APPLICABILITY_CLASSES
    return classification, classification, reason, ownership


def _telemetry_fields(line):
    """Parse unique key/value tokens from an authenticated diagnostic."""
    fields = {}
    for token in line.split():
        key, separator, value = token.partition("=")
        assert separator and key and value and key not in fields, f"malformed diagnostic field: {token!r}"
        fields[key] = value
    return fields


def _decode_hex(value):
    if not value or value == "-":
        return ""
    try:
        return bytes.fromhex(value).decode("utf-8")
    except (ValueError, UnicodeDecodeError):
        return ""


def _diagnostic_target(target):
    parts = target.split("-")
    assert len(parts) >= 3
    operating_system = parts[-1]
    operating_system = {"gnu": "linux", "msvc": "windows"}.get(operating_system, operating_system)
    return parts[0] + "-" + operating_system


def _validate_function_diagnostic(diagnostic, row):
    prefix = "CODEGEN_FALLBACK_FUNCTION "
    assert diagnostic.startswith(prefix)
    fields = _telemetry_fields(diagnostic[len(prefix):])
    required = {"version", "row", "target", "allocator", "function_id", "reason", "stage", "opcode_id",
                "line", "column", "source_hex", "function_hex"}
    assert set(fields) == required, "fallback function diagnostic fields are not versioned or complete"
    assert unsigned_decimal(fields["version"], 32, "diagnostic version") == 1
    assert unsigned_decimal(fields["row"], 64, "diagnostic row") == int(row["row"])
    assert fields["target"] == _diagnostic_target(row["target"])
    assert fields["allocator"] == row["allocator"]
    unsigned_decimal(fields["function_id"], 32, "function_id")
    unsigned_decimal(fields["opcode_id"], 32, "opcode_id")
    unsigned_decimal(fields["line"], 32, "line")
    unsigned_decimal(fields["column"], 32, "column")
    assert fields["reason"] in {"target-excluded", "signature", "opcode", "selection-other", "verification",
                                 "placement", "encoding", "output-capacity", "unwind"}
    expected_stage = {"signature": "selection", "opcode": "selection", "selection-other": "selection"}
    assert fields["stage"] == expected_stage.get(fields["reason"], fields["reason"])
    for name in ("source_hex", "function_hex"):
        value = fields[name]
        assert len(value) % 2 == 0 and value and all(byte in "0123456789abcdef" for byte in value)
        assert _decode_hex(value), f"invalid UTF-8 {name} diagnostic field"
    assert unsigned_decimal(fields["line"], 32, "diagnostic line") > 0
    assert unsigned_decimal(fields["column"], 32, "diagnostic column") > 0
    return fields


def _validate_counter_diagnostic(diagnostic, row):
    """Validate known aggregate counter records and their row association."""
    assert diagnostic.startswith("CODEGEN_FALLBACK")
    if diagnostic.startswith("CODEGEN_FALLBACK_REASON "):
        fields = _telemetry_fields(diagnostic[len("CODEGEN_FALLBACK_REASON "):])
        required = {"version", "row", "target", "allocator", "reason", "count"}
        assert set(fields) == required
        assert unsigned_decimal(fields["version"], 32, "diagnostic version") == 1
        assert unsigned_decimal(fields["row"], 64, "diagnostic row") == int(row["row"])
        assert fields["target"] == _diagnostic_target(row["target"])
        assert fields["allocator"] == row["allocator"]
        assert fields["reason"] in {"target-excluded", "signature", "opcode", "selection-other", "verification",
                                     "placement", "encoding", "output-capacity", "unwind"}
        unsigned_decimal(fields["count"], 32, "diagnostic count")
    elif diagnostic.startswith("CODEGEN_FALLBACK opcode="):
        fields = _telemetry_fields(diagnostic[len("CODEGEN_FALLBACK "):])
        required = {"version", "row", "target", "allocator", "opcode", "count"}
        assert set(fields) == required
        assert unsigned_decimal(fields["version"], 32, "diagnostic version") == 1
        assert unsigned_decimal(fields["row"], 64, "diagnostic row") == int(row["row"])
        assert fields["target"] == _diagnostic_target(row["target"])
        assert fields["allocator"] == row["allocator"]
        unsigned_decimal(fields["opcode"], 32, "diagnostic opcode")
        unsigned_decimal(fields["count"], 32, "diagnostic count")
    elif diagnostic.startswith("CODEGEN_FALLBACK_STAGES "):
        fields = _telemetry_fields(diagnostic[len("CODEGEN_FALLBACK_STAGES "):])
        required = {"version", "row", "target", "allocator", "verify", "placement", "encode"}
        assert set(fields) == required
        assert unsigned_decimal(fields["version"], 32, "diagnostic version") == 1
        assert unsigned_decimal(fields["row"], 64, "diagnostic row") == int(row["row"])
        assert fields["target"] == _diagnostic_target(row["target"])
        assert fields["allocator"] == row["allocator"]
        for name in ("verify", "placement", "encode"):
            unsigned_decimal(fields[name], 32, "diagnostic " + name)
    else:
        raise AssertionError("unknown fallback counter diagnostic")
    return fields


def _residual_record(row, outcome, diagnostic="", fields=None):
    fields = fields or {}
    function_id = fields.get("function_id", "")
    function_name = _decode_hex(fields.get("function_hex", ""))
    function = function_name or function_id
    return {
        "row": str(outcome["row"]),
        "group": str(outcome["group"]),
        "fixture": row["fixture"],
        "function": function,
        "function_id": function_id,
        "target": row["target"],
        "cpu": row["cpu"],
        "frontend": row["frontend_lowering"],
        "allocator": row["allocator"],
        "PIC": row["PIC"],
        "applicability": outcome["applicability"],
        "admission": outcome["admission"],
        "disposition": outcome["disposition"],
        "reason": fields.get("reason", outcome["reason"]),
        "ownership": outcome["ownership"],
        "diagnostic": diagnostic,
        "stage": fields.get("stage", ""),
        "opcode_id": fields.get("opcode_id", ""),
        "source_hex": fields.get("source_hex", ""),
        "function_hex": fields.get("function_hex", ""),
        "line": fields.get("line", ""),
        "column": fields.get("column", ""),
    }


def _residual_sort_key(record):
    function_id = record["function_id"]
    try:
        function_number = int(function_id)
    except ValueError:
        function_number = 1 << 32
    # Every retained field participates in the key.  In particular, two
    # records with the same function ID/reason must not retain filesystem
    # iteration order as an implicit tie-breaker before the 256-row bound.
    rest = tuple(record[field] for field in RESIDUAL_FIELDS if field not in {"row", "function_id"})
    return (int(record["row"]), function_number, function_id, rest)


def observed_supported_gap_rows(outcomes):
    """Return candidate gaps from row obligations and checked evidence only."""
    return sorted(item["row"] for item in outcomes.values()
                  if item["applicability"] == "admitted-supported" and item["allocator"] != "none" and
                  not item.get("baseline_unresolved", False) and
                  item.get("structural_candidate_failure", item["candidate_failure"]))


def validate_gap_declaration(manifest, profile, rows):
    """Check the declared full-census supported-gap count and digest."""
    digest = canonical_rows_digest(rows)
    if profile == FULL_CENSUS_PROFILE:
        assert manifest.get("supported_gap_count") == str(FULL_SUPPORTED_GAP_COUNT)
        assert manifest.get("supported_gap_sha256") == FULL_SUPPORTED_GAP_SHA256
        # A full-census manifest is emitted once per shard.  Each shard carries
        # the authenticated whole-census declaration, but only the aggregate
        # validator has all selected outcomes needed to check its 192-row
        # observation.  A non-sharded full report can still be checked here.
        if manifest.get("shard_count") != str(FULL_SHARD_COUNT) or len(rows) == FULL_SUPPORTED_GAP_COUNT:
            assert len(rows) == FULL_SUPPORTED_GAP_COUNT, "full census supported-gap count changed"
            assert digest == FULL_SUPPORTED_GAP_SHA256, "full census supported-gap digest changed"
    elif "supported_gap_count" in manifest or "supported_gap_sha256" in manifest:
        assert manifest.get("supported_gap_count") == str(len(rows))
        assert manifest.get("supported_gap_sha256") == digest
    return digest


def read_residual_sources(directory, rows, outcomes):
    """Authenticate all diagnostics and return the complete source set.

    Retention is deliberately applied only by the aggregate validator, after
    every shard's source records have been authenticated and globally sorted.
    """
    residuals = []
    source_records = []
    function_counts = Counter()
    function_records = {}
    counter_records = {}
    functions_path = directory / "fallback-functions.tsv"
    if functions_path.is_file():
        fields, records = table_with_fields(functions_path)
        assert fields == ("row", "record_valid", "telemetry")
        for record in records:
            key = record["row"]
            assert key in outcomes and key in rows, "fallback record references an unselected row"
            unsigned_decimal(key, 64, "fallback row")
            assert record["record_valid"] == "1", "invalid fallback function record"
            diagnostic = record["telemetry"]
            assert diagnostic and "\t" not in diagnostic and "\n" not in diagnostic
            parsed = _validate_function_diagnostic(diagnostic, rows[key])
            function_counts[key] += 1
            function_records.setdefault(key, []).append(parsed)
            source_records.append(_residual_record(rows[key], outcomes[key], diagnostic, parsed))

    counters_path = directory / "fallback-counters.tsv"
    if counters_path.is_file():
        fields, records = table_with_fields(counters_path)
        assert fields == ("row", "telemetry")
        for record in records:
            key = record["row"]
            assert key in outcomes and key in rows, "fallback counter references an unselected row"
            unsigned_decimal(key, 64, "fallback counter row")
            diagnostic = record["telemetry"]
            assert diagnostic and "\t" not in diagnostic and "\n" not in diagnostic
            parsed = _validate_counter_diagnostic(diagnostic, rows[key])
            assert outcomes[key]["fallbacks"] != 0, "counter diagnostic has no matching fallback count"
            counter_records.setdefault(key, []).append(parsed)
            source_records.append(_residual_record(rows[key], outcomes[key], diagnostic, parsed))

    # Every function attribution is row-matched to the result's fallback
    # count.  Do this after reading the complete source so a malformed or
    # omitted tail cannot be hidden by the 256-row bound.
    for key, outcome in outcomes.items():
        if outcome["allocator"] == "none":
            # The direct reference does not request per-function census
            # records, but its verbose aggregate counters are still retained
            # when machine fallback was observed.  Keep those counters
            # row-bound and require their reason totals to explain the
            # structured fallback count; never treat the reference fallback as
            # candidate evidence.
            assert not function_counts[key], f"direct reference contains function telemetry for row {key}"
            if outcome["fallbacks"] == 0:
                assert not counter_records.get(key), f"zero-fallback reference has counter telemetry for row {key}"
            else:
                reason_total = sum(int(record["count"]) for record in counter_records.get(key, [])
                                   if "reason" in record)
                assert reason_total == outcome["fallbacks"], \
                    f"reference fallback reason counters mismatch for row {key}"
            continue
        assert function_counts[key] == outcome["fallbacks"], \
            f"fallback function attribution mismatch for row {key}"
        if outcome["fallbacks"]:
            function_ids = [int(record["function_id"]) for record in function_records[key]]
            # The producer emits source order, but the retained TSV may be
            # transported through a tool that changes record order.  Identity
            # and uniqueness are authenticated here; the aggregate validator
            # sorts the complete set before applying its retention cap.
            assert len(function_ids) == len(set(function_ids)), \
                f"duplicate fallback function diagnostic for row {key}"
            reason_counts = Counter(record["reason"] for record in function_records[key])
            opcode_reasons = {"signature", "opcode", "selection-other"}
            opcode_counts = Counter("47" if record["opcode_id"] == "4294967295" else record["opcode_id"]
                                    for record in function_records[key] if record["reason"] in opcode_reasons)
            stage_counts = Counter()
            for record in function_records[key]:
                if record["stage"] == "verification":
                    stage_counts["verify"] += 1
                elif record["stage"] == "placement":
                    stage_counts["placement"] += 1
                elif record["stage"] in {"encoding", "output-capacity", "unwind"}:
                    stage_counts["encode"] += 1
            observed_reasons = Counter()
            observed_opcodes = Counter()
            observed_stages = Counter()
            stage_records = 0
            for record in counter_records.get(key, []):
                if "reason" in record:
                    observed_reasons[record["reason"]] += int(record["count"])
                if "opcode" in record:
                    observed_opcodes[record["opcode"]] += int(record["count"])
                if "verify" in record:
                    stage_records += 1
                    for name in ("verify", "placement", "encode"):
                        observed_stages[name] += int(record[name])
            assert observed_reasons == reason_counts, f"fallback reason counters mismatch for row {key}"
            assert observed_opcodes == opcode_counts, f"fallback opcode counters mismatch for row {key}"
            assert stage_records <= 1 and observed_stages == stage_counts, \
                f"fallback stage counters mismatch for row {key}"
        else:
            assert not function_records.get(key) and not counter_records.get(key), \
                f"zero-fallback row has residual telemetry for row {key}"
    residuals.extend(source_records)

    residuals.sort(key=_residual_sort_key)
    return residuals, False


def validate(directory):
    directory = directory.resolve()
    manifest = properties(directory / "manifest.txt")
    assert manifest["version"] == "2" and manifest["manifest_only"] == "0"
    assert manifest["identity_hash"] == "sha256"
    assert manifest["environment"] == "explicit-replacement-in-environment.tsv"
    assert manifest["unfrozen_dependencies"] == "none-for-object-census"
    if manifest.get("project_include_sha256", ""):
        assert manifest["sysroot"] == "target-correct-hosted-sdks"
        assert manifest["system_include"] == "target-correct-libc-project-include"
        assert manifest["source_dependencies"] == (
            "tracked-tests-plus-snapshotted-resource-include-plus-authenticated-project-include-plus-pinned-github-closure")
    else:
        assert manifest["sysroot"] == manifest["system_include"] == "none"
    for revision in (manifest["compiler_revision_claim"], manifest["baseline_revision_claim"]):
        assert len(revision) == 40 and all(byte in "0123456789abcdef" for byte in revision)
    for name, prefix in (("candidate-ide.exe", "compiler"), ("baseline-ide.exe", "baseline")):
        exact_sha(directory / name, manifest[prefix + "_bytes"], manifest[prefix + "_sha256"])
    inputs, input_ledger = validate_inputs(directory, manifest)
    validate_dependencies(directory, manifest)
    validate_environment(directory)
    row_fields, rows = table_with_fields(directory / "rows.tsv")
    result_fields, results = table_with_fields(directory / "results.tsv")
    assert row_fields == ROW_FIELDS
    assert result_fields == RESULT_FIELDS
    profile, subject_count = validate_profile(manifest, inputs, len(rows))
    validate_dependency_binding(directory, manifest, profile, inputs)
    gap_ledger_identities, declared_gap_rows, gap_ledger_sha256 = validate_supported_gap_ledger(
        directory, manifest, rows, profile)
    applicability_ledger, applicability_ledger_sha256 = validate_applicability_ledger(
        directory, manifest, rows, inputs, profile, gap_ledger_identities)
    manifest_rows = unsigned_decimal(manifest["rows"], 64, "manifest rows")
    assert len(rows) == manifest_rows
    assert [unsigned_decimal(row["row"], 64, "row") for row in rows] == list(range(len(rows)))
    assert all(unsigned_decimal(row["group"], 64, "group") == index // len(ALLOCATORS)
               for index, row in enumerate(rows))
    for index, row in enumerate(rows):
        assert row["allocator"] == ALLOCATORS[index % len(ALLOCATORS)]
        assert row["fixture"] in inputs and inputs[row["fixture"]]["role"] == "subject"
        assert row["fixture_recipe"] == inputs[row["fixture"]]["fixture_recipe"]
        assert row["compile_obligation"] == inputs[row["fixture"]]["compile_obligation"]
        assert row["compile_obligation"] in {SUPPORTED_OBJECT_OBLIGATION, NON_OBJECT_CONTROL_OBLIGATION}
        assert row["diagnostic_obligation"] == "none"
        assert row["cpu"] == expected_cpu(row["fixture"], row["target"], manifest["cpu"]) and row["cpu_features"]
        assert row["frontend_lowering"] in {"direct-ssa", "local-backed-canonical"}
        assert row["PIC"] in {"0", "1"} and row["selected"] in {"0", "1"}
        assert tuple(row[field] for field in ("target_abi", "link_obligation", "execution_obligation")) == TARGETS[row["target"]]
    subjects = [row["path"] for row in inputs.values() if row["role"] == "subject"]
    expected_cells = [(fixture, target, frontend, pic, allocator)
                      for fixture in subjects
                      for target in TARGETS
                      for frontend in ("local-backed-canonical", "direct-ssa")
                      for pic in ("0", "1")
                      for allocator in ALLOCATORS]
    actual_cells = [(row["fixture"], row["target"], row["frontend_lowering"], row["PIC"], row["allocator"])
                    for row in rows]
    assert actual_cells == expected_cells, "rows.tsv does not equal the required census cross-product"
    row_identities = field_map(rows, ROW_IDENTITY_FIELDS, "row")
    assert len(row_identities) == len(rows), "duplicate row identity"
    rows_identity_sha256 = canonical_map_digest(row_identities)
    input_ledger_sha256 = canonical_map_digest(input_ledger)
    manifest_identity_sha256 = manifest_identity_digest(manifest)
    by_row = {row["row"]: row for row in rows}
    assert len(by_row) == len(rows), "duplicate row number"
    by_result = {row["row"]: row for row in results}
    assert len(by_result) == len(results), "duplicate result row"
    assert set(by_result) <= set(by_row), "result references unknown row"
    assert set(by_result) == {key for key, row in by_row.items() if row["selected"] == "1"}, "selected/result mismatch"
    skip_rows = validate_skip_provenance(directory, rows, by_result, applicability_ledger)
    parsed_results = {}
    for key, result in by_result.items():
        parsed_results[key] = {
            "kind": unsigned_decimal(result["kind"], 32, "kind"),
            "status": unsigned_decimal(result["status"], 32, "status"),
            "functions": unsigned_decimal(result["functions"], 32, "functions"),
            "fallbacks": unsigned_decimal(result["fallbacks"], 32, "fallbacks"),
            "baseline_functions": unsigned_decimal(result["baseline_functions"], 32, "baseline_functions"),
            "object_bytes": unsigned_decimal(result["object_bytes"], 64, "object_bytes"),
            "object_hash": unsigned_decimal(result["object_hash"], 64, "object_hash"),
        }
        for field in ("counters_valid", "target_identity_valid", "function_records_valid"):
            unsigned_decimal(result[field], 1, field)
    baseline_by_group = {
        by_row[key]["group"]: parsed_results[key]["functions"]
        for key in by_result if by_row[key]["allocator"] == "none"
    }
    selected_groups = {row["group"] for key, row in by_row.items() if key in by_result}
    assert set(baseline_by_group) == selected_groups, "selected group is missing its allocator-none baseline"
    baseline_outcomes = {}
    for key, result in by_result.items():
        row = by_row[key]
        if row["allocator"] == "none":
            auth_class, auth_reason = applicability_ledger.get((row["fixture"], row["target"]), ("", ""))
            baseline_outcomes[row["group"]] = classify_result(
                row, result, baseline_by_group[row["group"]],
                skip_expected=(key in skip_rows or bool(expected_nonexecuted(row, auth_class, auth_reason))))
    assert set(baseline_outcomes) == selected_groups, "selected group baseline evidence is incomplete"
    recipes = {path: shlex.split(row["fixture_flags"]) for path, row in inputs.items()}
    identities = {}
    outcomes = {}
    for key, result in by_result.items():
        row = by_row[key]
        assert int(row["group"]) % int(manifest["shard_count"]) == int(manifest["shard_index"])
        validate_argv(directory, manifest, row, recipes)
        assert result["group"] == row["group"]
        assert result["cpu"] in {"", row["cpu"]}
        assert result["cpu_features"] in {"", row["cpu_features"]}
        if result["target_identity_valid"] == "1":
            assert result["cpu"] == row["cpu"] and result["cpu_features"] == row["cpu_features"]
        size = parsed_results[key]["object_bytes"]
        skip = skip_rows.get(key)
        if skip:
            expected_disposition, _expected_reason = skip
            assert result["disposition"] == expected_disposition
            assert result["kind"] == "0" and result["status"] == "0"
            assert result["counters_valid"] == "1" and result["target_identity_valid"] == "1"
            assert result["function_records_valid"] == "1"
            assert all(result[field] == "0" for field in ("functions", "fallbacks", "baseline_functions",
                                                            "object_bytes", "object_hash"))
            assert result["object_sha256"] == ""
        if size:
            exact_sha(directory / "groups" / row["group"] / (row["allocator"] + ".o"), size,
                      result["object_sha256"])
        else:
            assert result["object_sha256"] == ""
        baseline_unresolved = baseline_outcomes[row["group"]]["reference_failure"]
        outcome = classify_result(row, result, baseline_by_group[row["group"]], baseline_unresolved,
                                  skip_expected=skip is not None)
        outcome.update({"row": int(key), "group": int(row["group"]), "allocator": row["allocator"],
                        "fallbacks": int(result["fallbacks"]), "functions": int(result["functions"])})
        identity = tuple(row[field] for field in ("fixture", "target", "frontend_lowering", "PIC", "allocator"))
        declared_supported_gap = identity in gap_ledger_identities
        authenticated_class, authenticated_reason = applicability_ledger.get(
            (row["fixture"], row["target"]), ("", ""))
        applicability, admission, reason, ownership = classify_applicability(row, result, outcome,
                                                                               baseline_unresolved,
                                                                               declared_supported_gap,
                                                                               authenticated_class,
                                                                               authenticated_reason)
        if applicability == "unavailable":
            # An authenticated missing-resource/profile row may be retained as
            # provenance, but it is not retirement acceptance.  Keep it
            # separate from a candidate compiler defect while making the final
            # acceptance gate fail until exact supplemental evidence closes the
            # obligation.
            outcome["acceptance_failure"] = True
        if authenticated_class == "platform-inapplicable":
            # Preserve the authenticated applicability class while retaining
            # every compiler/process/object/fallback/telemetry defect.  The
            # class is evidence ownership, not a waiver of malformed output.
            outcome["inapplicable"] = True
        outcome.update({"applicability": applicability, "admission": admission,
                        "classification": applicability, "reason": reason, "ownership": ownership,
                        "baseline_unresolved": baseline_unresolved})
        outcomes[key] = outcome
        # The matrix columns identify a cell only together with the exact
        # fixture/input and relevant resource binding.  Compiler binaries and
        # whole-inventory digests stay report-level so a rebuilt candidate or
        # unrelated fixture addition does not erase valid common rows.
        input_identity = tuple(inputs[row["fixture"]][field]
                              for field in INPUT_FIELDS if field != "path")
        identity = tuple(row[field] for field in IDENTITY_FIELDS) + input_identity + (
            manifest["resource_include_sha256"],)
        assert identity not in identities
        identities[identity] = {"disposition": result["disposition"], "functions": int(result["functions"]),
                                "fallbacks": int(result["fallbacks"]), "row": int(key),
                                "candidate_failure": outcome["candidate_failure"],
                                "reference_failure": outcome["reference_failure"],
                                "acceptance_failure": outcome["acceptance_failure"],
                                "inapplicable": outcome["inapplicable"],
                                "fallback_defect": outcome["fallback_defect"],
                                "telemetry_defect": outcome["telemetry_defect"],
                                "execution_defect": outcome["execution_defect"],
                                "artifact_defect": outcome["artifact_defect"],
                                "unexpected_failure": outcome["unexpected_failure"],
                                "baseline_unresolved": baseline_unresolved,
                                "applicability": applicability, "admission": admission,
                                "reason": reason, "ownership": ownership}
    ordered_outcomes = {key: outcomes[key] for key in sorted(outcomes, key=int)}
    ordered_identities = {key: identities[key] for key in sorted(identities)}
    supported_gap_rows = sorted(declared_gap_rows)
    supported_gap_sha256 = validate_gap_declaration(manifest, profile, supported_gap_rows)
    residuals, residual_source_truncated = read_residual_sources(directory, by_row, ordered_outcomes)
    return {
        "directory": str(directory),
        "rows": manifest_rows,
        "selected_rows": {int(key) for key in by_result},
        "selected_row_records": {key: by_row[key] for key in by_result},
        "manifest": manifest,
        "profile": profile,
        "subjects": subject_count,
        "rows_identity": row_identities,
        "rows_identity_fields": ROW_IDENTITY_FIELDS,
        "rows_identity_sha256": rows_identity_sha256,
        "input_ledger": input_ledger,
        "input_ledger_fields": tuple(field for field in INPUT_FIELDS if field != "path"),
        "input_ledger_sha256": input_ledger_sha256,
        "manifest_identity_sha256": manifest_identity_sha256,
        "identities": ordered_identities,
        "outcomes": ordered_outcomes,
        "supported_gap_rows": supported_gap_rows,
        "supported_gap_count": len(supported_gap_rows),
        "supported_gap_sha256": supported_gap_sha256,
        "declared_gap_identities": gap_ledger_identities,
        "supported_gap_ledger_sha256": gap_ledger_sha256,
        "applicability_ledger_sha256": applicability_ledger_sha256,
        "applicability_ledger_entries": len(applicability_ledger),
        "applicability_skip_rows": sorted(skip_rows),
        "applicability_skip_evidence": str(directory / "applicability-skips.tsv"),
        "residuals": residuals,
        "residual_source_truncated": residual_source_truncated,
    }


def partition_shards(reports, require_clean_candidate=False, require_clean_acceptance=False):
    """Prove that validated shard reports form one complete row partition."""
    assert reports, "at least one census shard is required"
    manifests = [report["manifest"] for report in reports]
    first = manifests[0]
    count = len(reports)
    assert all(report["profile"] == reports[0]["profile"] for report in reports), "census profile mismatch"
    if require_clean_acceptance:
        assert reports[0]["profile"] == FULL_CENSUS_PROFILE, \
            "production acceptance requires full-census profile"
    if reports[0]["profile"] == FULL_CENSUS_PROFILE:
        assert count == FULL_SHARD_COUNT, "full census requires four shards"
    indices = [int(manifest["shard_index"]) for manifest in manifests]
    assert sorted(indices) == list(range(count)), "shard indices are incomplete or duplicated"
    assert all(int(manifest["shard_count"]) == count for manifest in manifests), "shard count mismatch"
    stable = {key: value for key, value in first.items() if key != "shard_index"}
    assert all({key: value for key, value in manifest.items() if key != "shard_index"} == stable
               for manifest in manifests[1:]), "shard provenance mismatch"
    first_rows = reports[0]["rows_identity"]
    first_inputs = reports[0]["input_ledger"]
    assert all(report["rows_identity"] == first_rows for report in reports[1:]), "rows.tsv identity mismatch"
    assert all(report["input_ledger"] == first_inputs for report in reports[1:]), "inputs.tsv recipe ledger mismatch"
    assert all(report["rows_identity_sha256"] == reports[0]["rows_identity_sha256"] for report in reports[1:])
    assert all(report["input_ledger_sha256"] == reports[0]["input_ledger_sha256"] for report in reports[1:])
    assert all(report["manifest_identity_sha256"] == reports[0]["manifest_identity_sha256"]
               for report in reports[1:]), "manifest identity mismatch"
    assert all(report["applicability_ledger_sha256"] == reports[0]["applicability_ledger_sha256"]
               for report in reports[1:]), "applicability ledger mismatch"

    selected_rows = set()
    identities = set()
    outcomes = {}
    for report in reports:
        assert selected_rows.isdisjoint(report["selected_rows"]), "duplicate selected row across shards"
        selected_rows.update(report["selected_rows"])
        assert identities.isdisjoint(report["identities"]), "duplicate census identity across shards"
        identities.update(report["identities"])
        assert set(outcomes).isdisjoint(report["outcomes"]), "duplicate outcome row across shards"
        outcomes.update(report["outcomes"])
    expected_rows = set(range(int(first["rows"])))
    assert selected_rows == expected_rows, "shards do not cover the complete manifest row set"
    if reports[0]["profile"] == FULL_CENSUS_PROFILE:
        assert len(selected_rows) == FULL_ROW_COUNT, "full census selected row count is incomplete"
    assert len(identities) == len(expected_rows), "shards do not cover unique census identities"
    candidate_failures = sorted(item["row"] for item in outcomes.values() if item["candidate_failure"])
    acceptance_failures = sorted(item["row"] for item in outcomes.values() if item["acceptance_failure"])
    if require_clean_candidate:
        assert not candidate_failures, f"candidate has unresolved rows: {candidate_failures[:8]}"
    if require_clean_acceptance:
        assert not acceptance_failures, f"acceptance has unresolved rows: {acceptance_failures[:8]}"
    return first, selected_rows, {key: outcomes[key] for key in sorted(outcomes, key=int)}


def validate_shards(directories, output, require_clean_candidate=False, require_clean_acceptance=False, reference_supplements=False):
    """Validate every v2 shard and prove that their selected rows partition it.

    A per-shard result can be internally consistent while the overall census
    silently omits a shard or executes one row twice.  Keep this check here,
    beside the v2 row validator, so the acceptance workflow cannot mistake
    four independently valid partial reports for a complete inventory.
    """
    output = Path(output)
    reports = sorted((validate(directory) for directory in directories),
                     key=lambda report: int(report["manifest"]["shard_index"]))
    if require_clean_acceptance:
        assert reports[0]["profile"] == FULL_CENSUS_PROFILE, \
            "production acceptance requires full-census profile"
    # Always write the aggregate, including failed dispositions, before
    # applying the optional gate.  Failed evidence must remain inspectable.
    first, selected_rows, outcomes = partition_shards(reports)
    direct_reference_failure_rows = sorted(item["row"] for item in outcomes.values() if item["reference_failure"])
    supplement_identities = []
    if reference_supplements:
        import native_retirement_reference as reference
        assert first["baseline_revision_claim"] == first["compiler_revision_claim"], "supplement requires same-source direct reference"
        assert first["baseline_sha256"] == first["compiler_sha256"], "supplement requires same binary for direct and MIR"
        resolved = set()
        for report in reports:
            groups, identity = reference.validate(report)
            resolved.update(groups)
            supplement_identities.append(identity)
        for outcome in outcomes.values():
            if outcome["reference_failure"] and str(outcome["group"]) in resolved:
                outcome["reference_failure"] = False
                outcome["acceptance_failure"] = outcome["candidate_failure"]
                outcome["reference_resolution"] = "independent-clang-control"
                outcome["reason"] = "direct-reference-unresolved-clang-control-passed"
                outcome["ownership"] = "clang-reference"
    count = len(reports)
    row_records = {}
    residuals = []
    residual_source_truncated = False
    skip_records = []
    for report in reports:
        row_records.update(report["selected_row_records"])
        residuals.extend(report["residuals"])
        residual_source_truncated |= report["residual_source_truncated"]
        skip_path = Path(report["applicability_skip_evidence"])
        skip_fields, shard_skips = table_with_fields(skip_path)
        assert skip_fields == APPLICABILITY_SKIP_FIELDS
        skip_records.extend(shard_skips)
    assert len({row["row"] for row in skip_records}) == len(skip_records), "duplicate skip provenance across shards"
    assert set(row_records) == {str(row) for row in selected_rows}, "selected row records are incomplete"
    def counts(side):
        return {key: value for key, value in sorted(Counter(item["disposition"] for item in outcomes.values()
                                                          if item["side"] == side).items())}

    candidate_failures = sorted(item["row"] for item in outcomes.values() if item["candidate_failure"])
    reference_failures = sorted(item["row"] for item in outcomes.values() if item["reference_failure"])
    acceptance_failures = sorted(item["row"] for item in outcomes.values() if item["acceptance_failure"])
    inapplicable_rows = sorted(item["row"] for item in outcomes.values() if item["inapplicable"])
    fallback_defects = sorted(item["row"] for item in outcomes.values() if item["fallback_defect"])
    telemetry_defects = sorted(item["row"] for item in outcomes.values() if item["telemetry_defect"])
    execution_defects = sorted(item["row"] for item in outcomes.values() if item["execution_defect"])
    artifact_defects = sorted(item["row"] for item in outcomes.values() if item["artifact_defect"])
    observed_applicability = Counter(item["applicability"] for item in outcomes.values())
    assert set(observed_applicability) <= set(APPLICABILITY_CLASSES)
    applicability_counts = {classification: observed_applicability[classification]
                            for classification in APPLICABILITY_CLASSES}
    applicability_row_sets = {
        classification: sorted(item["row"] for item in outcomes.values()
                               if item["applicability"] == classification)
        for classification in APPLICABILITY_CLASSES
    }
    supported_gap_rows = sorted({row for report in reports for row in report["supported_gap_rows"]})
    supported_gap_sha256 = canonical_rows_digest(supported_gap_rows)
    if first["profile"] == FULL_CENSUS_PROFILE:
        assert len(supported_gap_rows) == FULL_SUPPORTED_GAP_COUNT
        assert supported_gap_sha256 == FULL_SUPPORTED_GAP_SHA256
        assert all(outcomes[str(row)]["applicability"] == "admitted-supported" for row in supported_gap_rows)
    elif "supported_gap_count" in first or "supported_gap_sha256" in first:
        assert first.get("supported_gap_count") == str(len(supported_gap_rows))
        assert first.get("supported_gap_sha256") == supported_gap_sha256
    unexpected_failures = sorted(item["row"] for item in outcomes.values() if item["unexpected_failure"])
    applicability_rows = []
    for key in sorted(outcomes, key=int):
        row = row_records[key]
        outcome = outcomes[key]
        applicability_rows.append({
            "row": key,
            "group": row["group"],
            "fixture": row["fixture"],
            "target": row["target"],
            "cpu": row["cpu"],
            "frontend": row["frontend_lowering"],
            "allocator": row["allocator"],
            "PIC": row["PIC"],
            "applicability": outcome["applicability"],
            "admission": outcome["admission"],
            "disposition": outcome["disposition"],
            "reason": outcome["reason"],
            "ownership": outcome["ownership"],
            "candidate_failure": str(int(outcome["candidate_failure"])),
            "reference_failure": str(int(outcome["reference_failure"])),
            "acceptance_failure": str(int(outcome["acceptance_failure"])),
        })
    residuals.sort(key=_residual_sort_key)
    residual_truncated = residual_source_truncated or len(residuals) > MAX_RESIDUAL_ROWS
    residuals = residuals[:MAX_RESIDUAL_ROWS]
    applicability_path = output.parent / "applicability.tsv"
    applicability_skip_path = output.parent / "applicability-skips.tsv"
    residual_path = output.parent / "residual.tsv"
    write_table(applicability_path, APPLICABILITY_FIELDS, applicability_rows)
    skip_records.sort(key=lambda row: int(row["row"]))
    write_table(applicability_skip_path, APPLICABILITY_SKIP_FIELDS, skip_records)
    write_table(residual_path, RESIDUAL_FIELDS, residuals)
    result = {
        "schema": 2,
        "directories": [report["directory"] for report in reports],
        "shards": count,
        "profile": first["profile"],
        "rows_validated": len(selected_rows),
        "groups": len(selected_rows) // len(ALLOCATORS),
        "compiler_revision_claim": first["compiler_revision_claim"],
        "baseline_revision_claim": first["baseline_revision_claim"],
        "compiler_sha256": first["compiler_sha256"],
        "baseline_sha256": first["baseline_sha256"],
        "support_contract_sha256": first["support_contract_sha256"],
        "supported_gap_ledger_sha256": reports[0]["supported_gap_ledger_sha256"],
        "applicability_ledger_sha256": reports[0]["applicability_ledger_sha256"],
        "applicability_ledger_entries": reports[0]["applicability_ledger_entries"],
        "resource_include_sha256": first["resource_include_sha256"],
        "manifest_identity_sha256": reports[0]["manifest_identity_sha256"],
        "rows_identity_fields": list(reports[0]["rows_identity_fields"]),
        "rows_identity_sha256": reports[0]["rows_identity_sha256"],
        "input_ledger_fields": list(reports[0]["input_ledger_fields"]),
        "input_ledger_sha256": reports[0]["input_ledger_sha256"],
        "baseline_dispositions": counts("baseline"),
        "reference_dispositions": counts("reference"),
        "setup_dispositions": counts("setup"),
        "candidate_dispositions": counts("candidate"),
        "applicability_classes": list(APPLICABILITY_CLASSES),
        "admission_classes": list(APPLICABILITY_CLASSES),
        "applicability_counts": applicability_counts,
        "admission_counts": applicability_counts,
        "applicability_rows_by_class": applicability_row_sets,
        "admission_rows_by_class": applicability_row_sets,
        "supported_gap_rows": supported_gap_rows,
        "supported_gap_count": len(supported_gap_rows),
        "supported_gap_sha256": supported_gap_sha256,
        "applicability_rows": len(applicability_rows),
        "applicability_evidence": str(applicability_path),
        "applicability_tsv": str(applicability_path),
        "applicability_sha256": sha256(applicability_path),
        "applicability_skip_rows": [int(row["row"]) for row in skip_records],
        "applicability_skip_evidence": str(applicability_skip_path),
        "residual_evidence": str(residual_path),
        "residual_tsv": str(residual_path),
        "residual_sha256": sha256(residual_path),
        "residual_rows": len(residuals),
        "residual_limit": MAX_RESIDUAL_ROWS,
        "residual_truncated": residual_truncated,
        "candidate_failure_rows": candidate_failures,
        "direct_reference_failure_rows": direct_reference_failure_rows,
        "reference_supplement_sha256": supplement_identities,
        "reference_failure_rows": reference_failures,
        "acceptance_failure_rows": acceptance_failures,
        "inapplicable_rows": inapplicable_rows,
        "fallback_defect_rows": fallback_defects,
        "telemetry_defect_rows": telemetry_defects,
        "execution_defect_rows": execution_defects,
        "artifact_defect_rows": artifact_defects,
        "unexpected_failure_rows": unexpected_failures,
        "require_clean_candidate": require_clean_candidate,
        "require_clean_acceptance": require_clean_acceptance,
        "clean_candidate": not candidate_failures,
        "clean_acceptance": not acceptance_failures,
        "complete_row_partition": True,
        "global_identity_unique": True,
    }
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    # Print source identities before failing the gate. A prefix of numeric row
    # IDs hides independent failures in later fixtures and the retained report.
    for label, failures in (("candidate", candidate_failures), ("reference", reference_failures)):
        if failures:
            by_fixture = Counter(row_records[str(row)]["fixture"] for row in failures)
            print(f"NATIVE_RETIREMENT_FAILURES side={label} rows={len(failures)} fixtures={len(by_fixture)} report={output}")
            for fixture, count in sorted(by_fixture.items()):
                print(f"  {fixture}: {count} unresolved rows")
    if require_clean_candidate:
        assert not candidate_failures, f"candidate has unresolved rows: {candidate_failures[:8]}"
    if require_clean_acceptance:
        assert not acceptance_failures, f"acceptance has unresolved rows: {acceptance_failures[:8]}"
    return result


def self_test():
    """Exercise the completeness guard without requiring compiler artifacts."""
    manifest = {"shard_count": "4", "rows": "8", "profile": SELF_TEST_PROFILE, "provenance": "fixed"}
    reports = []
    for index in range(4):
        reports.append({"manifest": dict(manifest, shard_index=str(index)),
                        "selected_rows": {index, index + 4}, "identities": {index, index + 4}})
    for report in reports:
        report["profile"] = SELF_TEST_PROFILE
        report["rows_identity"] = {str(key): (str(key),) for key in range(8)}
        report["rows_identity_sha256"] = canonical_map_digest(report["rows_identity"])
        report["input_ledger"] = {"tests/unit.c": ("subject",)}
        report["input_ledger_sha256"] = canonical_map_digest(report["input_ledger"])
        report["manifest_identity_sha256"] = manifest_identity_digest(report["manifest"])
        report["applicability_ledger_sha256"] = "self-test"
        report["applicability_ledger_entries"] = 0
        report["selected_row_records"] = {str(key): {} for key in report["selected_rows"]}
        report["residuals"] = []
        report["residual_source_truncated"] = False
        report["outcomes"] = {str(key): {"row": key, "candidate_failure": False,
                                          "reference_failure": False, "acceptance_failure": False,
                                          "inapplicable": False,
                                          "fallback_defect": False, "telemetry_defect": False,
                                          "execution_defect": False, "artifact_defect": False,
                                          "side": "candidate", "disposition": "strict-success"}
                                for key in report["selected_rows"]}
    _first, selected, _outcomes = partition_shards(reports, require_clean_candidate=True)
    assert selected == set(range(8))
    bad_partitions = (
        reports[:3],
        reports[:3] + [dict(reports[3], selected_rows={3, 4}, identities={3, 4})],
    )
    for bad in bad_partitions:
        try:
            partition_shards(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError("incomplete or duplicate shard partition was accepted")


def reconcile(reference, candidate, output, require_clean_candidate, require_clean_acceptance=False):
    reference_manifest = reference["manifest"]
    candidate_manifest = candidate["manifest"]
    if require_clean_acceptance:
        assert reference.get("profile") == FULL_CENSUS_PROFILE and reference_manifest.get("profile") == FULL_CENSUS_PROFILE, \
            "production acceptance requires full-census reference profile"
        assert candidate.get("profile") == FULL_CENSUS_PROFILE and candidate_manifest.get("profile") == FULL_CENSUS_PROFILE, \
            "production acceptance requires full-census candidate profile"
    old, new = reference["identities"], candidate["identities"]
    common = sorted(set(old) & set(new))
    transitions = Counter((old[key]["disposition"], new[key]["disposition"]) for key in common)
    applicability_transitions = Counter((old[key].get("applicability"), new[key].get("applicability"))
                                        for key in common)
    candidate_common_failure_rows = sorted(new[key]["row"] for key in common if new[key]["candidate_failure"])
    reference_common_failure_rows = sorted(new[key]["row"] for key in common if new[key]["reference_failure"])
    acceptance_common_failure_rows = sorted(new[key]["row"] for key in common if new[key]["acceptance_failure"])
    report = {"schema": 2, "reference": reference["directory"], "candidate": candidate["directory"],
              "reference_compiler_revision_claim": reference_manifest["compiler_revision_claim"],
              "candidate_compiler_revision_claim": candidate_manifest["compiler_revision_claim"],
              "reference_baseline_revision_claim": reference_manifest["baseline_revision_claim"],
              "candidate_baseline_revision_claim": candidate_manifest["baseline_revision_claim"],
              "reference_compiler_sha256": reference_manifest["compiler_sha256"],
              "candidate_compiler_sha256": candidate_manifest["compiler_sha256"],
              "reference_baseline_sha256": reference_manifest["baseline_sha256"],
              "candidate_baseline_sha256": candidate_manifest["baseline_sha256"],
              "reference_support_contract_sha256": reference_manifest["support_contract_sha256"],
              "candidate_support_contract_sha256": candidate_manifest["support_contract_sha256"],
              "reference_resource_include_sha256": reference_manifest["resource_include_sha256"],
              "candidate_resource_include_sha256": candidate_manifest["resource_include_sha256"],
              "reference_rows_identity_sha256": reference["rows_identity_sha256"],
              "candidate_rows_identity_sha256": candidate["rows_identity_sha256"],
              "reference_input_ledger_sha256": reference["input_ledger_sha256"],
              "candidate_input_ledger_sha256": candidate["input_ledger_sha256"],
              "reference_manifest_identity_sha256": reference["manifest_identity_sha256"],
              "candidate_manifest_identity_sha256": candidate["manifest_identity_sha256"],
              "common_rows": len(common), "removed_rows": len(set(old) - set(new)),
              "added_rows": len(set(new) - set(old)),
              "transitions": [{"from": before, "to": after, "rows": count}
                              for (before, after), count in sorted(transitions.items())],
              "applicability_transitions": [{"from": before, "to": after, "rows": count}
                                             for (before, after), count in sorted(applicability_transitions.items())],
              "candidate_common_failures": len(candidate_common_failure_rows),
              "candidate_common_failure_rows": candidate_common_failure_rows,
              "reference_common_failures": len(reference_common_failure_rows),
              "reference_common_failure_rows": reference_common_failure_rows,
              "acceptance_common_failures": len(acceptance_common_failure_rows),
              "acceptance_common_failure_rows": acceptance_common_failure_rows,
              "require_clean_candidate": require_clean_candidate,
              "require_clean_acceptance": require_clean_acceptance,
              "clean_candidate": not candidate_common_failure_rows,
              "clean_acceptance": not acceptance_common_failure_rows}
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if require_clean_candidate:
        assert report["candidate_common_failures"] == 0, "candidate has unresolved common rows"
    if require_clean_acceptance:
        assert report["acceptance_common_failures"] == 0, "acceptance has unresolved common rows"
    return report


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("self-test")
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("directories", nargs="+", type=Path)
    shards_parser = subparsers.add_parser("validate-shards")
    shards_parser.add_argument("directories", nargs="+", type=Path)
    shards_parser.add_argument("--out", required=True, type=Path)
    shards_parser.add_argument("--require-clean-candidate", action="store_true")
    shards_parser.add_argument("--require-clean-acceptance", action="store_true")
    shards_parser.add_argument("--reference-supplements", action="store_true")
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("reference", type=Path)
    compare_parser.add_argument("candidate", type=Path)
    compare_parser.add_argument("--out", required=True, type=Path)
    compare_parser.add_argument("--require-clean-candidate", action="store_true")
    compare_parser.add_argument("--require-clean-acceptance", action="store_true")
    arguments = parser.parse_args()
    if arguments.command == "self-test":
        self_test()
        print(json.dumps({"self_test": "passed"}))
    elif arguments.command == "validate":
        reports = [validate(directory) for directory in arguments.directories]
        print(json.dumps({"validated": [{"directory": report["directory"], "rows": report["rows"],
                                          "rows_identity_sha256": report["rows_identity_sha256"],
                                          "input_ledger_sha256": report["input_ledger_sha256"],
                                          "manifest_identity_sha256": report["manifest_identity_sha256"]}
                                        for report in reports]}))
    elif arguments.command == "validate-shards":
        print(json.dumps(validate_shards(arguments.directories, arguments.out,
                                         arguments.require_clean_candidate,
                                         arguments.require_clean_acceptance,
                                         arguments.reference_supplements), sort_keys=True))
    else:
        print(json.dumps(reconcile(validate(arguments.reference), validate(arguments.candidate),
                                   arguments.out, arguments.require_clean_candidate,
                                   arguments.require_clean_acceptance), sort_keys=True))


if __name__ == "__main__":
    main()
