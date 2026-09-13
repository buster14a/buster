#!/usr/bin/env python3
"""Independently validate and reconcile native-retirement census v2 evidence."""

import argparse
import csv
import hashlib
import json
import shlex
import struct
from collections import Counter
from pathlib import Path


ALLOCATORS = ("none", "mir-stack", "fast", "quality")
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
SUCCESS = {"baseline-supported", "strict-success", "strict-empty-unit"}
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
}


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
    assert rows and all(row["kind"] == "resource-header" for row in rows)
    assert [row["path"] for row in rows] == sorted(row["path"] for row in rows)
    assert len({row["path"] for row in rows}) == len(rows)
    closure = hashlib.sha256()
    expected_files = set()
    root = directory / "dependencies" / "resource-include"
    for row in rows:
        relative = relative_path(row["path"])
        path = root / relative
        exact_sha(path, row["bytes"], row["sha256"])
        data = path.read_bytes()
        closure.update(row["path"].encode())
        closure.update(b"\0")
        closure.update(struct.pack("<Q", len(data)))
        closure.update(data)
        expected_files.add(relative.as_posix())
    actual_files = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
    assert actual_files == expected_files, "resource snapshot and ledger differ"
    assert closure.hexdigest() == manifest["resource_include_sha256"]


def validate_inputs(directory, manifest):
    contract_path = directory / "support-contract.tsv"
    assert sha256(contract_path) == manifest["support_contract_sha256"]
    contract_fields, contract = table_with_fields(contract_path)
    input_fields, inputs = table_with_fields(directory / "inputs.tsv")
    assert contract_fields == SUPPORT_FIELDS
    assert input_fields == INPUT_FIELDS
    assert len(inputs) == int(manifest["inputs"]) == len(contract)
    assert [row["path"] for row in contract] == sorted(row["path"] for row in contract)
    assert len({row["path"] for row in contract}) == len(contract)
    assert [row["path"] for row in inputs] == sorted(row["path"] for row in inputs)
    assert len({row["path"] for row in inputs}) == len(inputs)
    for approved, row in zip(contract, inputs):
        assert tuple(approved[field] for field in SUPPORT_FIELDS) == tuple(row[field] for field in SUPPORT_FIELDS)
        exact_sha(directory / "inputs" / relative_path(row["path"]), row["bytes"], row["sha256"])
        recipe_name, recipe_flags = expected_fixture_recipe(row["path"])
        assert row["fixture_recipe"] == recipe_name and tuple(shlex.split(row["fixture_flags"])) == recipe_flags, \
            f"fixture recipe mismatch for {row['path']}"
    actual_files = {path.relative_to(directory / "inputs").as_posix()
                    for path in (directory / "inputs").rglob("*") if path.is_file()}
    assert actual_files == {row["path"] for row in inputs}, "input snapshot and ledger differ"
    return {row["path"]: row for row in inputs}, ledger_map(inputs)


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
                "-mcpu=" + manifest["cpu"], "-fPIC" if row["PIC"] == "1" else "-fno-pic",
                lowering, "-fregister-allocator=" + row["allocator"], "-fverify-codegen",
                "-fmachine-fallback" if baseline else "-fno-machine-fallback", "-nostdinc",
                "-isystem", str(recorded_root / "dependencies" / "resource-include"),
                "-I" + str(recorded_root / "inputs" / "tests"),
                str(recorded_root / "inputs" / row["fixture"]), "-o",
                str(recorded_root / "groups" / row["group"] / (row["allocator"] + ".o"))]
    expected.extend(recipes[row["fixture"]])
    if not baseline:
        expected.append("-fcodegen-fallback-census")
    assert argv == expected, f"argv mismatch for row {row['row']}"


def classify_result(row, result, expected_baseline_functions):
    """Classify an executed row without hiding its producer disposition.

    The producer's disposition is evidence, not a stable API for the gate:
    in particular, missing-telemetry rows have appeared with more than one
    disposition spelling.  The candidate gate therefore derives defects from
    the structured telemetry fields and keeps the original disposition in the
    returned record.
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
    baseline_count_defect = baseline_functions != expected_baseline_functions
    # The object census does not observe native program execution.  Preserve
    # this row-level obligation as metadata, while keeping every compiler,
    # process, object, fallback and telemetry check below active.
    inapplicable = row["execution_obligation"] == "unavailable-platform-control"
    execution_defect = result["kind"] != "0" or result["status"] != "0"
    fallback_defect = fallbacks != 0
    if allocator == "none":
        setup = disposition.startswith("infrastructure-") or disposition.startswith("setup-")
        baseline_success_defect = disposition == "baseline-supported" and (
            bool(telemetry_defects) or execution_defect or fallback_defect or object_bytes == 0 or
            baseline_count_defect or functions != expected_baseline_functions)
        telemetry_defect = bool(telemetry_defects) or baseline_success_defect or baseline_count_defect
        reference_failure = not setup and (disposition != "baseline-supported" or baseline_success_defect)
        candidate_failure = setup
        return {
            "side": "setup" if setup else "baseline",
            "disposition": disposition,
            "candidate_failure": candidate_failure,
            "reference_failure": reference_failure,
            "acceptance_failure": candidate_failure or reference_failure,
            "inapplicable": inapplicable,
            "fallback_defect": fallback_defect,
            "telemetry_defect": telemetry_defect,
            "execution_defect": execution_defect,
            "telemetry_fields": telemetry_defects,
        }

    function_shape_defect = ((disposition in {"strict-success", "strict-success-baseline-unresolved"} and functions == 0)
                            or (disposition == "strict-empty-unit" and functions != 0))
    baseline_shape_defect = ((disposition == "strict-success" and baseline_functions != functions)
                             or (disposition == "strict-empty-unit" and baseline_functions != 0))
    artifact_defect = disposition in {"strict-success", "strict-success-baseline-unresolved", "strict-empty-unit"} and object_bytes == 0
    telemetry_defect = (bool(telemetry_defects) or function_shape_defect or baseline_shape_defect or artifact_defect or
                        baseline_count_defect)
    candidate_failure = fallback_defect or telemetry_defect or execution_defect
    reference_failure = False

    if disposition == "strict-success-baseline-unresolved":
        side = "reference"
        reference_failure = not inapplicable
    elif disposition.startswith("baseline-and-"):
        # Preserve the reference failure while still rejecting the unresolved
        # candidate side when the MIR leg did not produce a supported result.
        side = "reference"
        candidate_failure = True
        reference_failure = not inapplicable
    elif disposition in {"strict-success", "strict-empty-unit"} and not candidate_failure:
        side = "candidate"
    elif disposition.startswith("infrastructure-") or disposition.startswith("setup-"):
        side = "setup"
        candidate_failure = True
    else:
        side = "candidate"
        candidate_failure = True
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
        "telemetry_fields": telemetry_defects,
    }


def validate(directory):
    directory = directory.resolve()
    manifest = properties(directory / "manifest.txt")
    assert manifest["version"] == "2" and manifest["manifest_only"] == "0"
    assert manifest["identity_hash"] == "sha256"
    assert manifest["environment"] == "explicit-replacement-in-environment.tsv"
    assert manifest["unfrozen_dependencies"] == "none-for-object-census"
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
        assert row["compile_obligation"] == "supported-object-zero-fallback"
        assert row["diagnostic_obligation"] == "none"
        assert row["cpu"] == manifest["cpu"] and row["cpu_features"]
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
        if size:
            exact_sha(directory / "groups" / row["group"] / (row["allocator"] + ".o"), size,
                      result["object_sha256"])
        else:
            assert result["object_sha256"] == ""
        outcome = classify_result(row, result, baseline_by_group[row["group"]])
        outcome.update({"row": int(key), "group": int(row["group"]), "allocator": row["allocator"],
                        "fallbacks": int(result["fallbacks"]), "functions": int(result["functions"])})
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
                                "execution_defect": outcome["execution_defect"]}
    ordered_outcomes = {key: outcomes[key] for key in sorted(outcomes, key=int)}
    ordered_identities = {key: identities[key] for key in sorted(identities)}
    return {
        "directory": str(directory),
        "rows": len(results),
        "selected_rows": {int(key) for key in by_result},
        "manifest": manifest,
        "rows_identity": row_identities,
        "rows_identity_fields": ROW_IDENTITY_FIELDS,
        "rows_identity_sha256": rows_identity_sha256,
        "input_ledger": input_ledger,
        "input_ledger_fields": tuple(field for field in INPUT_FIELDS if field != "path"),
        "input_ledger_sha256": input_ledger_sha256,
        "manifest_identity_sha256": manifest_identity_sha256,
        "identities": ordered_identities,
        "outcomes": ordered_outcomes,
    }


def partition_shards(reports, require_clean_candidate=False):
    """Prove that validated shard reports form one complete row partition."""
    assert reports, "at least one census shard is required"
    manifests = [report["manifest"] for report in reports]
    first = manifests[0]
    count = len(reports)
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
    assert len(identities) == len(expected_rows), "shards do not cover unique census identities"
    candidate_failures = sorted(item["row"] for item in outcomes.values() if item["candidate_failure"])
    acceptance_failures = sorted(item["row"] for item in outcomes.values() if item["acceptance_failure"])
    if require_clean_candidate:
        assert not acceptance_failures, f"candidate acceptance has unresolved rows: {acceptance_failures[:8]}"
    return first, selected_rows, {key: outcomes[key] for key in sorted(outcomes, key=int)}


def validate_shards(directories, output, require_clean_candidate=False):
    """Validate every v2 shard and prove that their selected rows partition it.

    A per-shard result can be internally consistent while the overall census
    silently omits a shard or executes one row twice.  Keep this check here,
    beside the v2 row validator, so the acceptance workflow cannot mistake
    four independently valid partial reports for a complete inventory.
    """
    reports = sorted((validate(directory) for directory in directories),
                     key=lambda report: int(report["manifest"]["shard_index"]))
    # Always write the aggregate, including failed dispositions, before
    # applying the optional gate.  Failed evidence must remain inspectable.
    first, selected_rows, outcomes = partition_shards(reports)
    count = len(reports)
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
    result = {
        "schema": 2,
        "directories": [report["directory"] for report in reports],
        "shards": count,
        "rows_validated": len(selected_rows),
        "groups": len(selected_rows) // len(ALLOCATORS),
        "compiler_revision_claim": first["compiler_revision_claim"],
        "baseline_revision_claim": first["baseline_revision_claim"],
        "compiler_sha256": first["compiler_sha256"],
        "baseline_sha256": first["baseline_sha256"],
        "support_contract_sha256": first["support_contract_sha256"],
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
        "candidate_failure_rows": candidate_failures,
        "reference_failure_rows": reference_failures,
        "acceptance_failure_rows": acceptance_failures,
        "inapplicable_rows": inapplicable_rows,
        "fallback_defect_rows": fallback_defects,
        "telemetry_defect_rows": telemetry_defects,
        "execution_defect_rows": execution_defects,
        "require_clean_candidate": require_clean_candidate,
        "clean_candidate": not candidate_failures,
        "clean_acceptance": not acceptance_failures,
        "complete_row_partition": True,
    }
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if require_clean_candidate:
        assert not acceptance_failures, f"candidate acceptance has unresolved rows: {acceptance_failures[:8]}"
    return result


def self_test():
    """Exercise the completeness guard without requiring compiler artifacts."""
    manifest = {"shard_count": "4", "rows": "8", "provenance": "fixed"}
    reports = []
    for index in range(4):
        reports.append({"manifest": dict(manifest, shard_index=str(index)),
                        "selected_rows": {index, index + 4}, "identities": {index, index + 4}})
    for report in reports:
        report["rows_identity"] = {str(key): (str(key),) for key in range(8)}
        report["rows_identity_sha256"] = canonical_map_digest(report["rows_identity"])
        report["input_ledger"] = {"tests/unit.c": ("subject",)}
        report["input_ledger_sha256"] = canonical_map_digest(report["input_ledger"])
        report["manifest_identity_sha256"] = manifest_identity_digest(report["manifest"])
        report["outcomes"] = {str(key): {"row": key, "candidate_failure": False,
                                          "reference_failure": False, "acceptance_failure": False,
                                          "inapplicable": False,
                                          "fallback_defect": False, "telemetry_defect": False,
                                          "execution_defect": False,
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


def reconcile(reference, candidate, output, require_clean):
    old, new = reference["identities"], candidate["identities"]
    common = sorted(set(old) & set(new))
    transitions = Counter((old[key]["disposition"], new[key]["disposition"]) for key in common)
    reference_manifest = reference["manifest"]
    candidate_manifest = candidate["manifest"]
    report = {"schema": 1, "reference": reference["directory"], "candidate": candidate["directory"],
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
              "candidate_common_failures": sum(new[key].get("acceptance_failure",
                                                               new[key]["disposition"] not in SUCCESS or
                                                               new[key]["fallbacks"] or
                                                               new[key].get("telemetry_defect", False) or
                                                               new[key].get("candidate_failure", False))
                                               for key in common)}
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if require_clean:
        assert report["candidate_common_failures"] == 0, "candidate has unresolved common rows"
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
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("reference", type=Path)
    compare_parser.add_argument("candidate", type=Path)
    compare_parser.add_argument("--out", required=True, type=Path)
    compare_parser.add_argument("--require-clean-candidate", action="store_true")
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
                                         arguments.require_clean_candidate), sort_keys=True))
    else:
        print(json.dumps(reconcile(validate(arguments.reference), validate(arguments.candidate),
                                   arguments.out, arguments.require_clean_candidate), sort_keys=True))


if __name__ == "__main__":
    main()
