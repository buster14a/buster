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


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def table(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


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
    contract = table(contract_path)
    inputs = table(directory / "inputs.tsv")
    assert len(inputs) == int(manifest["inputs"]) == len(contract)
    assert [row["path"] for row in inputs] == sorted(row["path"] for row in inputs)
    assert len({row["path"] for row in inputs}) == len(inputs)
    contract_fields = ("path", "role", "compile_obligation", "bytes", "sha256")
    for approved, row in zip(contract, inputs):
        assert tuple(approved[field] for field in contract_fields) == tuple(row[field] for field in contract_fields)
        exact_sha(directory / "inputs" / relative_path(row["path"]), row["bytes"], row["sha256"])
    actual_files = {path.relative_to(directory / "inputs").as_posix()
                    for path in (directory / "inputs").rglob("*") if path.is_file()}
    assert actual_files == {row["path"] for row in inputs}, "input snapshot and ledger differ"
    return {row["path"]: row for row in inputs}


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
    inputs = validate_inputs(directory, manifest)
    validate_dependencies(directory, manifest)
    validate_environment(directory)
    rows = table(directory / "rows.tsv")
    results = table(directory / "results.tsv")
    assert len(rows) == int(manifest["rows"])
    assert [int(row["row"]) for row in rows] == list(range(len(rows)))
    assert all(int(row["group"]) == index // len(ALLOCATORS) for index, row in enumerate(rows))
    for index, row in enumerate(rows):
        assert row["allocator"] == ALLOCATORS[index % len(ALLOCATORS)]
        assert row["fixture"] in inputs and inputs[row["fixture"]]["role"] == "subject"
        assert row["compile_obligation"] == "supported-object-zero-fallback"
        assert row["diagnostic_obligation"] == "none"
        assert row["cpu"] == manifest["cpu"] and row["cpu_features"]
        assert row["frontend_lowering"] in {"direct-ssa", "local-backed-canonical"}
        assert row["PIC"] in {"0", "1"} and row["selected"] in {"0", "1"}
        assert tuple(row[field] for field in ("target_abi", "link_obligation", "execution_obligation")) == TARGETS[row["target"]]
    by_row = {row["row"]: row for row in rows}
    by_result = {row["row"]: row for row in results}
    assert len(by_result) == len(results), "duplicate result row"
    assert set(by_result) == {key for key, row in by_row.items() if row["selected"] == "1"}, "selected/result mismatch"
    recipes = {path: shlex.split(row["fixture_flags"]) for path, row in inputs.items()}
    identities = {}
    for key, result in by_result.items():
        row = by_row[key]
        validate_argv(directory, manifest, row, recipes)
        assert result["group"] == row["group"]
        assert result["cpu"] in {"", row["cpu"]}
        assert result["cpu_features"] in {"", row["cpu_features"]}
        if result["target_identity_valid"] == "1":
            assert result["cpu"] == row["cpu"] and result["cpu_features"] == row["cpu_features"]
        size = int(result["object_bytes"])
        if size:
            exact_sha(directory / "groups" / row["group"] / (row["allocator"] + ".o"), size,
                      result["object_sha256"])
        else:
            assert result["object_sha256"] == ""
        if result["disposition"] in SUCCESS:
            assert result["counters_valid"] == result["target_identity_valid"] == "1"
            assert int(result["fallbacks"]) == 0 and size > 0
            if row["allocator"] != "none":
                assert result["function_records_valid"] == "1"
        identity = tuple(row[field] for field in IDENTITY_FIELDS)
        assert identity not in identities
        identities[identity] = {"disposition": result["disposition"], "functions": int(result["functions"]),
                                "fallbacks": int(result["fallbacks"]), "row": int(key)}
    return {"directory": str(directory), "rows": len(results), "identities": identities}


def reconcile(reference, candidate, output, require_clean):
    old, new = reference["identities"], candidate["identities"]
    common = sorted(set(old) & set(new))
    transitions = Counter((old[key]["disposition"], new[key]["disposition"]) for key in common)
    report = {"schema": 1, "reference": reference["directory"], "candidate": candidate["directory"],
              "common_rows": len(common), "removed_rows": len(set(old) - set(new)),
              "added_rows": len(set(new) - set(old)),
              "transitions": [{"from": before, "to": after, "rows": count}
                              for (before, after), count in sorted(transitions.items())],
              "candidate_common_failures": sum(new[key]["disposition"] not in SUCCESS or new[key]["fallbacks"]
                                               for key in common)}
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if require_clean:
        assert report["candidate_common_failures"] == 0, "candidate has unresolved common rows"
    return report


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("directories", nargs="+", type=Path)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("reference", type=Path)
    compare_parser.add_argument("candidate", type=Path)
    compare_parser.add_argument("--out", required=True, type=Path)
    compare_parser.add_argument("--require-clean-candidate", action="store_true")
    arguments = parser.parse_args()
    if arguments.command == "validate":
        reports = [validate(directory) for directory in arguments.directories]
        print(json.dumps({"validated": [{"directory": report["directory"], "rows": report["rows"]}
                                        for report in reports]}))
    else:
        print(json.dumps(reconcile(validate(arguments.reference), validate(arguments.candidate),
                                   arguments.out, arguments.require_clean_candidate), sort_keys=True))


if __name__ == "__main__":
    main()
