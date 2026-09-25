#!/usr/bin/env python3
"""Filesystem-backed regressions for the native-retirement v2 validator."""

import csv
import copy
import hashlib
import json
import re
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_retirement_contract as contract
import native_retirement_dependency_binding as dependency_authority
import native_retirement_materializer as materializer


def write_table(path, fields, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def read_table(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        return tuple(reader.fieldnames), list(reader)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class ContractTests(unittest.TestCase):
    def test_fixture_cpu_profiles_are_target_scoped(self):
        self.assertEqual(contract.expected_cpu("tests/basic_c_predicate_bank.c",
                                               "x86_64-unknown-linux-gnu", "baseline"),
                         "skylake-avx512")
        self.assertEqual(contract.expected_cpu("tests/basic_c_atomic_aggregate.c",
                                               "x86_64-pc-windows-msvc", "baseline"),
                         "haswell")
        self.assertEqual(contract.expected_cpu("tests/basic_c_predicate_bank.c",
                                               "aarch64-unknown-linux-gnu", "baseline"),
                         "baseline")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.shards = [self.root / "shard-0", self.root / "shard-1"]
        for index, shard in enumerate(self.shards):
            self.make_shard(shard, index)

    def tearDown(self):
        self.temporary.cleanup()

    def make_shard(self, directory, shard_index):
        directory.mkdir()
        (directory / "inputs/tests").mkdir(parents=True)
        (directory / "dependencies/resource-include").mkdir(parents=True)
        source = b"int unit(void) { return 1; }\n"
        resource = b"/* frozen resource header */\n"
        (directory / "inputs/tests/unit.c").write_bytes(source)
        (directory / "dependencies/resource-include/stddef.h").write_bytes(resource)

        contract_rows = [{"path": "tests/unit.c", "role": "subject",
                          "compile_obligation": "supported-object-zero-fallback",
                          "bytes": str(len(source)), "sha256": sha(source)}]
        write_table(directory / "support-contract.tsv", contract.SUPPORT_FIELDS, contract_rows)
        write_table(directory / "supported-gap-ledger.tsv", contract.SUPPORTED_GAP_LEDGER_FIELDS, [])
        write_table(directory / "applicability-ledger.tsv", contract.APPLICABILITY_LEDGER_FIELDS, [])
        write_table(directory / "applicability-skips.tsv", contract.APPLICABILITY_SKIP_FIELDS, [])
        input_rows = [{"path": "tests/unit.c", "role": "subject",
                       "compile_obligation": "supported-object-zero-fallback", "bytes": str(len(source)),
                       "buster_hash_64": "0", "sha256": sha(source), "fixture_recipe": "compiler-default",
                       "fixture_flags": ""}]
        write_table(directory / "inputs.tsv", contract.INPUT_FIELDS, input_rows)

        dependency_rows = [{"kind": "resource-header", "path": "stddef.h", "bytes": str(len(resource)),
                            "sha256": sha(resource)}]
        write_table(directory / "dependencies.tsv", ("kind", "path", "bytes", "sha256"), dependency_rows)
        write_table(directory / "environment.tsv", ("name", "present", "value"), [
            {"name": "LC_ALL", "present": "1", "value": "C"},
            {"name": "LANG", "present": "1", "value": "C"},
            {"name": "TZ", "present": "1", "value": "UTC"},
        ])

        closure = hashlib.sha256()
        closure.update(b"stddef.h")
        closure.update(b"\0")
        closure.update(struct.pack("<Q", len(resource)))
        closure.update(resource)
        candidate = b"candidate compiler\n"
        baseline = b"direct baseline\n"
        (directory / "candidate-ide.exe").write_bytes(candidate)
        (directory / "baseline-ide.exe").write_bytes(baseline)
        metadata = {
            "version": "2", "kind": "object-coverage", "identity_hash": "sha256",
            "support_contract": "docs/native-retirement-support-v1.tsv",
            "support_contract_sha256": sha((directory / "support-contract.tsv").read_bytes()),
            "compiler_revision_claim": "a" * 40, "baseline_revision_claim": "b" * 40,
            "compiler_hash": "0", "compiler_bytes": str(len(candidate)), "compiler_sha256": sha(candidate),
            "baseline_hash": "0", "baseline_bytes": str(len(baseline)), "baseline_sha256": sha(baseline),
            "cpu": "baseline", "resource_include_sha256": closure.hexdigest(), "sysroot": "none",
            "system_include": "none", "inputs": "1", "subjects": "1", "rows": "192",
            "profile": "self-test", "fixture_filter": "",
            "target_filter": "", "shard_index": str(shard_index), "shard_count": "2",
            "supported_gap_count": "0", "supported_gap_sha256": contract.canonical_rows_digest([]),
            "supported_gap_ledger": "docs/native-retirement-supported-gaps-v1.tsv",
            "supported_gap_ledger_sha256": sha((directory / "supported-gap-ledger.tsv").read_bytes()),
            "applicability_ledger": "docs/native-retirement-applicability-v1.tsv",
            "applicability_ledger_sha256": sha((directory / "applicability-ledger.tsv").read_bytes()),
            "applicability_ledger_entries": "0",
            "manifest_only": "0", "timeout_seconds": "30",
            "function_evidence": "all-observed-fallbacks-plus-first-fatal-diagnostic",
            "fixture_flags": "exact-path-recipes-in-inputs.tsv",
            "source_dependencies": "tracked-tests-plus-snapshotted-resource-include",
            "environment": "explicit-replacement-in-environment.tsv",
            "unfrozen_dependencies": "none-for-object-census",
            "flags": "-c -g0 -v -fwrapv -fno-strict-aliasing -funsigned-char -fverify-codegen -nostdinc -isystem SNAPSHOT",
        }
        (directory / "manifest.txt").write_text("".join(f"{key}={value}\n" for key, value in metadata.items()),
                                                encoding="utf-8")

        rows = []
        results = []
        row_number = 0
        group = 0
        for target, (abi, link, execution) in contract.TARGETS.items():
            for frontend in ("local-backed-canonical", "direct-ssa"):
                for pic in ("0", "1"):
                    for allocator in contract.ALLOCATORS:
                        selected = group % 2 == shard_index
                        row = {"row": str(row_number), "group": str(group), "fixture": "tests/unit.c",
                               "target": target, "target_abi": abi, "cpu": "baseline", "cpu_features": "generic",
                               "allocator": allocator, "frontend_lowering": frontend, "PIC": pic,
                               "selected": "1" if selected else "0", "fixture_recipe": "compiler-default",
                               "compile_obligation": "supported-object-zero-fallback", "link_obligation": link,
                               "execution_obligation": execution, "diagnostic_obligation": "none",
                               "argv_evidence": f"groups/{group}/{allocator}.argv"}
                        rows.append(row)
                        self.write_argv(directory, row)
                        if selected:
                            object_data = f"object-{row_number}\n".encode()
                            object_path = directory / "groups" / str(group) / f"{allocator}.o"
                            object_path.parent.mkdir(parents=True, exist_ok=True)
                            object_path.write_bytes(object_data)
                            results.append({"row": str(row_number), "group": str(group),
                                            "disposition": "baseline-supported" if allocator == "none" else "strict-success",
                                            "kind": "0", "status": "0", "counters_valid": "1",
                                            "target_identity_valid": "1", "function_records_valid": "1",
                                            "functions": "1", "fallbacks": "0", "baseline_functions": "1",
                                            "cpu": "baseline", "cpu_features": "generic",
                                            "object_bytes": str(len(object_data)), "object_hash": "0",
                                            "object_sha256": sha(object_data)})
                        row_number += 1
                    group += 1
        write_table(directory / "rows.tsv", contract.ROW_FIELDS, rows)
        write_table(directory / "results.tsv", contract.RESULT_FIELDS, results)

    def write_argv(self, directory, row):
        baseline = row["allocator"] == "none"
        executable = "baseline-ide.exe" if baseline else "candidate-ide.exe"
        lowering = "-ffrontend-ssa" if row["frontend_lowering"] == "direct-ssa" else "-fno-frontend-ssa"
        argv = [str(directory / executable), "cc", "-c", "-g0", "-v", "-fwrapv",
                "-fno-strict-aliasing", "-funsigned-char", "-target", row["target"], "-mcpu=baseline",
                "-fPIC" if row["PIC"] == "1" else "-fno-pic", lowering,
                "-fregister-allocator=" + row["allocator"], "-fverify-codegen",
                "-fmachine-fallback" if baseline else "-fno-machine-fallback", "-nostdinc", "-isystem",
                str(directory / "dependencies/resource-include"), "-I" + str(directory / "inputs/tests"),
                str(directory / "inputs/tests/unit.c"), "-o",
                str(directory / "groups" / row["group"] / f"{row['allocator']}.o")]
        if not baseline:
            argv.append("-fcodegen-fallback-census")
        path = directory / row["argv_evidence"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\0".join(item.encode() for item in argv) + b"\0")

    def validate(self, require_clean=True, require_clean_acceptance=False):
        output = self.root / "report.json"
        return contract.validate_shards(list(reversed(self.shards)), output, require_clean,
                                        require_clean_acceptance)

    def declare_gaps(self, identities):
        """Install the same authenticated self-test gap ledger in every shard."""
        records = []
        for fixture, target, frontend, PIC, allocator in sorted(identities):
            records.append({"fixture": fixture, "target": target, "frontend_lowering": frontend,
                            "PIC": PIC, "allocator": allocator, "admission": "admitted-supported",
                            "reason": "supported-object-zero-fallback"})
        for shard in self.shards:
            write_table(shard / "supported-gap-ledger.tsv", contract.SUPPORTED_GAP_LEDGER_FIELDS, records)
            manifest_path = shard / "manifest.txt"
            manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
            _row_fields, rows = read_table(shard / "rows.tsv")
            by_identity = {tuple(row[field] for field in ("fixture", "target", "frontend_lowering", "PIC", "allocator")): row
                           for row in rows}
            row_numbers = sorted(int(by_identity[identity]["row"]) for identity in identities)
            manifest.update({"supported_gap_count": str(len(records)),
                             "supported_gap_sha256": contract.canonical_rows_digest(row_numbers),
                             "supported_gap_ledger_sha256": sha((shard / "supported-gap-ledger.tsv").read_bytes())})
            manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")

    def install_applicability(self, records):
        """Install one identical authenticated projection in every shard."""
        input_fields, input_rows = read_table(self.shards[0] / "inputs.tsv")
        del input_fields
        subject_hash = {row["path"]: row["sha256"] for row in input_rows if row["role"] == "subject"}
        ledger_rows = []
        for fixture, target, classification, reason in sorted(records):
            ledger_rows.append({"fixture": fixture, "target": target,
                                "fixture_sha256": subject_hash[fixture],
                                "applicability": classification, "reason": reason})
        for shard in self.shards:
            path = shard / "applicability-ledger.tsv"
            write_table(path, contract.APPLICABILITY_LEDGER_FIELDS, ledger_rows)
            manifest_path = shard / "manifest.txt"
            manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
            manifest.update({"applicability_ledger_sha256": sha(path.read_bytes()),
                             "applicability_ledger_entries": str(len(ledger_rows))})
            manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")
            _row_fields, rows = read_table(shard / "rows.tsv")
            _result_fields, results = read_table(shard / "results.tsv")
            projection = {(record["fixture"], record["target"]):
                          (record["applicability"], record["reason"]) for record in ledger_rows}
            skip_rows = []
            for row in rows:
                if row["selected"] != "1":
                    continue
                skip = contract.expected_nonexecuted(
                    row, *(projection.get((row["fixture"], row["target"]), ("", ""))))
                if skip:
                    skip_class, skip_reason = skip
                    skip_rows.append({"row": row["row"], "group": row["group"], "fixture": row["fixture"],
                                      "target": row["target"], "allocator": row["allocator"],
                                      "applicability": skip_class, "reason": skip_reason})
                    result = next(item for item in results if item["row"] == row["row"])
                    result.update({"disposition": skip_class, "kind": "0", "status": "0",
                                   "counters_valid": "1", "target_identity_valid": "1",
                                   "function_records_valid": "1", "functions": "0", "fallbacks": "0",
                                   "baseline_functions": "0", "object_bytes": "0", "object_hash": "0",
                                   "object_sha256": ""})
            write_table(shard / "results.tsv", contract.RESULT_FIELDS, results)
            write_table(shard / "applicability-skips.tsv", contract.APPLICABILITY_SKIP_FIELDS, skip_rows)

    def install_single_fallback(self, shard_index, row_number, telemetry):
        """Install one row-bound fallback and its aggregate counters."""
        shard = self.shards[shard_index]
        result_fields, results = read_table(shard / "results.tsv")
        strict = next(row for row in results if row["row"] == row_number)
        strict["fallbacks"] = "1"
        write_table(shard / "results.tsv", result_fields, results)
        _row_fields, census_rows = read_table(shard / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == row_number)
        target = contract._diagnostic_target(census_row["target"])
        allocator = census_row["allocator"]
        write_table(shard / "fallback-functions.tsv", ("row", "record_valid", "telemetry"),
                    [{"row": row_number, "record_valid": "1", "telemetry": telemetry}])
        write_table(shard / "fallback-counters.tsv", ("row", "telemetry"), [
            {"row": row_number, "telemetry": f"CODEGEN_FALLBACK_REASON target={target} allocator={allocator} reason=opcode count=1 version=1 row={row_number}"},
            {"row": row_number, "telemetry": f"CODEGEN_FALLBACK opcode=7 count=1 version=1 row={row_number} target={target} allocator={allocator}"},
        ])

    def install_structured_reference_failure(self, baseline_functions="0"):
        """Make one direct group structurally unresolved, not label-unresolved."""
        shard = self.shards[0]
        fields, results = read_table(shard / "results.tsv")
        baseline = next(row for row in results if row["row"] == "0")
        baseline.update({"kind": "1", "status": "7", "counters_valid": "0",
                         "target_identity_valid": "0", "function_records_valid": "0",
                         "functions": baseline_functions, "fallbacks": "0",
                         "baseline_functions": baseline_functions, "object_bytes": "0",
                         "object_sha256": ""})
        candidates = [row for row in results if row["group"] == baseline["group"] and
                      row["row"] != baseline["row"]]
        for index, candidate in enumerate(candidates):
            # All three MIR rows have authenticated counts different from the
            # partial/zero direct count; their objects and other evidence stay
            # clean unless a caller deliberately adds a defect below.
            candidate["functions"] = str(index + 4)
            candidate["baseline_functions"] = baseline_functions
        write_table(shard / "results.tsv", fields, results)
        for name in ("fallback-functions.tsv", "fallback-counters.tsv"):
            (shard / name).unlink(missing_ok=True)

    def test_clean_complete_partition_passes(self):
        report = self.validate()
        self.assertEqual(report["profile"], contract.SELF_TEST_PROFILE)
        self.assertEqual(report["supported_gap_ledger_sha256"],
                         sha((self.shards[0] / "supported-gap-ledger.tsv").read_bytes()))
        self.assertEqual(report["rows_validated"], 192)
        self.assertEqual(report["groups"], 48)
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["baseline_dispositions"], {"baseline-supported": 48})
        self.assertTrue(report["require_clean_candidate"])
        self.assertFalse(report["require_clean_acceptance"])
        self.assertTrue(report["clean_candidate"])
        self.assertTrue(report["clean_acceptance"])
        self.assertEqual(report["rows_identity_sha256"], contract.validate(self.shards[0])["rows_identity_sha256"])
        self.assertTrue(json.loads((self.root / "report.json").read_text(encoding="utf-8"))["complete_row_partition"])
        fields, dependencies = read_table(self.shards[0] / "dependencies.tsv")
        same = b"/* same include name */\n"
        (self.shards[0] / "dependencies/resource-include/same.h").write_bytes(same)
        (self.shards[0] / "dependencies/project-include").mkdir(parents=True)
        (self.shards[0] / "dependencies/project-include/same.h").write_bytes(same)
        dependencies.extend([
            {"kind": "resource-header", "path": "same.h", "bytes": str(len(same)), "sha256": sha(same)},
            {"kind": "project-header", "path": "same.h", "bytes": str(len(same)), "sha256": sha(same)},
        ])
        write_table(self.shards[0] / "dependencies.tsv", fields, dependencies)
        with self.assertRaisesRegex(AssertionError, "global include namespace collision"):
            contract.validate(self.shards[0])

    def test_object_argv_cannot_admit_a_host_sysroot_or_system_include(self):
        manifest_path = self.shards[0] / "manifest.txt"
        manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
        manifest["sysroot"] = str(self.root / "host-sysroot")
        manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")
        with self.assertRaises(AssertionError):
            contract.validate(self.shards[0])

        manifest["sysroot"] = "none"
        manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")
        argv_path = self.shards[0] / "groups/0/none.argv"
        argv = argv_path.read_bytes().split(b"\0")[:-1]
        insertion = argv.index(b"-nostdinc") + 1
        argv[insertion:insertion] = [b"-isysroot", b"/host/sdk"]
        argv_path.write_bytes(b"\0".join(argv) + b"\0")
        with self.assertRaisesRegex(AssertionError, "argv mismatch"):
            contract.validate(self.shards[0])

    def test_clean_self_test_cannot_satisfy_production_acceptance(self):
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)
        reports = [contract.validate(shard) for shard in self.shards]
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            contract.partition_shards(reports, require_clean_acceptance=True)

    def test_manifest_cannot_omit_a_complete_group(self):
        for shard in self.shards:
            row_fields, rows = read_table(shard / "rows.tsv")
            removed = {row["row"] for row in rows[-4:]}
            write_table(shard / "rows.tsv", row_fields, rows[:-4])
            result_fields, results = read_table(shard / "results.tsv")
            write_table(shard / "results.tsv", result_fields,
                        [row for row in results if row["row"] not in removed])
            manifest_path = shard / "manifest.txt"
            manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
            manifest["rows"] = "188"
            manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")
        with self.assertRaisesRegex(AssertionError, "required census cross-product"):
            self.validate()

    def test_full_profile_claim_cannot_forge_the_production_population(self):
        manifest = dict(line.split("=", 1) for line in
                        (self.shards[0] / "manifest.txt").read_text(encoding="utf-8").splitlines())
        manifest.update({"profile": "full-census", "support_contract_sha256": contract.FULL_SUPPORT_CONTRACT_SHA256,
                         "inputs": "559", "subjects": "411", "shard_count": "4",
                         "fixture_filter": "", "target_filter": ""})
        with self.assertRaisesRegex(AssertionError, "full census subject inventory is incomplete"):
            contract.validate_profile(manifest, {"tests/unit.c": {"role": "subject"}}, 192)

    def test_exact_full_profile_shape_is_admissible(self):
        manifest = {"profile": "full-census", "support_contract": "docs/native-retirement-support-v1.tsv",
                    "support_contract_sha256": contract.FULL_SUPPORT_CONTRACT_SHA256, "inputs": "559",
                    "shard_count": "4", "fixture_filter": "", "target_filter": "", "subjects": "411"}
        inputs = {f"tests/subject-{index}.c": {"role": "subject"} for index in range(contract.FULL_SUBJECT_COUNT)}
        for digest in (contract.FULL_SUPPORT_CONTRACT_SHA256,
                       contract.NEXT_SUPPORT_CONTRACT_SHA256):
            with self.subTest(digest=digest):
                manifest["support_contract_sha256"] = digest
                self.assertEqual(contract.validate_profile(manifest, inputs, contract.FULL_ROW_COUNT),
                                 (contract.FULL_CENSUS_PROFILE, contract.FULL_SUBJECT_COUNT))
        manifest["support_contract_sha256"] = "0" * 64
        with self.assertRaises(AssertionError):
            contract.validate_profile(manifest, inputs, contract.FULL_ROW_COUNT)

    def test_checked_in_production_gap_ledger_is_canonical_and_authenticated(self):
        ledger_path = Path(__file__).resolve().parents[1] / "docs/native-retirement-supported-gaps-v1.tsv"
        fields, records = read_table(ledger_path)
        self.assertEqual(fields, contract.SUPPORTED_GAP_LEDGER_FIELDS)
        identities = [tuple(record[field] for field in contract.SUPPORTED_GAP_LEDGER_FIELDS[:5])
                      for record in records]
        self.assertEqual(len(records), contract.FULL_SUPPORTED_GAP_COUNT)
        self.assertEqual(len(set(identities)), contract.FULL_SUPPORTED_GAP_COUNT)
        self.assertEqual(identities, sorted(identities))
        self.assertEqual(contract.canonical_digest(identities),
                         "bcaa2b1a4dcb3cbcfb31871a59c636d381c57b1be52046c6c24e7b5f4b014823")
        self.assertEqual(sha(ledger_path.read_bytes()), contract.FULL_SUPPORTED_GAP_LEDGER_SHA256)
        for record in records:
            self.assertEqual(record["admission"], "admitted-supported")
            self.assertEqual(record["reason"], "supported-object-zero-fallback")
            self.assertIn(record["allocator"], contract.ALLOCATORS[1:])
            self.assertIn(record["frontend_lowering"], {"local-backed-canonical", "direct-ssa"})
            self.assertIn(record["PIC"], {"0", "1"})

    def test_checked_in_applicability_projection_is_canonical_and_authenticated(self):
        ledger_path = Path(__file__).resolve().parents[1] / "docs/native-retirement-applicability-v1.tsv"
        fields, records = read_table(ledger_path)
        self.assertEqual(fields, contract.APPLICABILITY_LEDGER_FIELDS)
        identities = [(record["fixture"], record["target"]) for record in records]
        self.assertEqual(len(records), contract.FULL_APPLICABILITY_LEDGER_COUNT)
        self.assertEqual(len(set(identities)), contract.FULL_APPLICABILITY_LEDGER_COUNT)
        self.assertEqual(identities, sorted(identities))
        self.assertEqual(sha(ledger_path.read_bytes()), contract.FULL_APPLICABILITY_LEDGER_SHA256)
        for record in records:
            self.assertIn(record["applicability"], contract.AUTHENTICATED_APPLICABILITY_CLASSES)
            fixture_path = ledger_path.parents[1] / record["fixture"]
            self.assertEqual(sha(fixture_path.read_bytes()), record["fixture_sha256"])
            self.assertRegex(record["reason"], r"^[A-Za-z0-9._-]+$")

    def test_authenticated_nonexecution_requires_structurally_valid_evidence(self):
        target = next(iter(contract.TARGETS))
        self.install_applicability({("tests/unit.c", target, "platform-inapplicable", "source-registration-test")})
        clean = self.validate(require_clean=False)
        self.assertIn(1, clean["inapplicable_rows"])
        self.assertNotIn(1, clean["candidate_failure_rows"])
        fields, results = read_table(self.shards[0] / "results.tsv")
        candidate = next(row for row in results if row["row"] == "1")
        candidate.update({"kind": "1", "status": "1", "disposition": "admitted-supported"})
        write_table(self.shards[0] / "results.tsv", fields, results)

        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

        # A producer-controlled disposition cannot select non-execution on its
        # own when the authenticated skip evidence is absent.
        self.install_applicability({("tests/unit.c", target, "platform-inapplicable", "source-registration-test")})
        fields, results = read_table(self.shards[0] / "results.tsv")
        candidate = next(row for row in results if row["row"] == "1")
        candidate["disposition"] = "strict-success"
        write_table(self.shards[0] / "results.tsv", fields, results)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_target_contract_fixtures_keep_their_applicable_object_rows(self):
        root = Path(__file__).resolve().parents[1]
        _fields, records = read_table(root / "docs/native-retirement-applicability-v1.tsv")
        projection = {(record["fixture"], record["target"]): record for record in records}
        cases = {
            "tests/basic_c_aarch64_abi_contract.c": {
                target for target in contract.TARGETS if target.startswith("aarch64-")},
            "tests/uefi_boot.c": {
                target for target in contract.TARGETS if target.endswith("-uefi")},
            "tests/issue36_target_wchar.c": {
                target for target in contract.TARGETS if not target.endswith("-uefi")},
        }
        for fixture, applicable in cases.items():
            for target in contract.TARGETS:
                with self.subTest(fixture=fixture, target=target):
                    record = projection.get((fixture, target))
                    if target in applicable:
                        self.assertIsNone(record)
                    else:
                        self.assertEqual(record["applicability"], "platform-inapplicable")
                        self.assertEqual(record["fixture_sha256"], sha((root / fixture).read_bytes()))

    def test_authenticated_admitted_supported_precedes_structured_reference_state(self):
        target = next(iter(contract.TARGETS))
        self.install_applicability({("tests/unit.c", target, "admitted-supported", "source-reviewed-residual")})
        self.install_structured_reference_failure()

        report = self.validate(require_clean=False)
        self.assertIn(0, report["reference_failure_rows"])
        self.assertIn(0, report["acceptance_failure_rows"])
        self.assertNotIn(0, report["candidate_failure_rows"])
        _fields, applicability_rows = read_table(self.root / "applicability.tsv")
        row_zero = next(row for row in applicability_rows if row["row"] == "0")
        self.assertEqual(row_zero["applicability"], "admitted-supported")
        self.assertEqual(row_zero["admission"], "admitted-supported")
        self.assertEqual(row_zero["reason"], "source-reviewed-residual")
        self.assertEqual(row_zero["reference_failure"], "1")
        self.assertFalse(report["clean_acceptance"])

    def test_authenticated_unavailable_nonexecution_remains_fail_closed(self):
        target = "x86_64-apple-ios"
        self.install_applicability({("tests/unit.c", target, "unavailable", "sdk-not-materialized")})
        report = self.validate(require_clean=False)
        _fields, applicability_rows = read_table(self.root / "applicability.tsv")
        row = next(item for item in applicability_rows if item["target"] == target and item["allocator"] == "none")
        self.assertEqual(row["applicability"], "unavailable")
        self.assertNotEqual(row["candidate_failure"], "1")
        self.assertEqual(row["acceptance_failure"], "1")
        self.assertFalse(report["clean_acceptance"])

        shard = self.shards[0]
        result_fields, results = read_table(shard / "results.tsv")
        malformed = next(item for item in results if item["row"] == row["row"])
        malformed["counters_valid"] = "0"
        write_table(shard / "results.tsv", result_fields, results)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_applicability_projection_bytes_and_fixture_identity_are_authenticated(self):
        target = next(iter(contract.TARGETS))
        self.install_applicability({("tests/unit.c", target, "unavailable", "source-registration-test")})
        path = self.shards[0] / "applicability-ledger.tsv"
        path.write_bytes(path.read_bytes().replace(b"source-registration-test", b"tampered-reason"))
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

        self.install_applicability({("tests/unit.c", target, "unavailable", "source-registration-test")})
        fields, records = read_table(path)
        records[0]["fixture_sha256"] = "0" * 64
        write_table(path, fields, records)
        for shard in self.shards:
            if shard == self.shards[0]:
                continue
            other = shard / "applicability-ledger.tsv"
            other.write_bytes(path.read_bytes())
            manifest_path = shard / "manifest.txt"
            manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
            manifest["applicability_ledger_sha256"] = sha(other.read_bytes())
            manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_applicability_projection_cannot_reclassify_declared_gap(self):
        target = next(iter(contract.TARGETS))
        identity = ("tests/unit.c", target, "local-backed-canonical", "0", "mir-stack")
        self.declare_gaps({identity})
        self.install_applicability({("tests/unit.c", target, "platform-inapplicable", "source-registration-test")})
        with self.assertRaisesRegex(AssertionError, "supported gap was reclassified"):
            self.validate(require_clean=False)

    def test_checked_in_support_contract_replays_every_tree_path(self):
        root = Path(__file__).resolve().parents[1]
        ledger_path = root / "docs/native-retirement-support-v1.tsv"
        fields, records = read_table(ledger_path)
        self.assertEqual(fields, contract.SUPPORT_FIELDS)
        self.assertEqual(len(records), 559)
        self.assertEqual(len({record["path"] for record in records}), 559)
        self.assertEqual({record["role"] for record in records},
                         {"subject", "negative-diagnostic-fixture", "support-file", "dormant-custom-language"})
        role_counts = {role: sum(record["role"] == role for record in records)
                       for role in {record["role"] for record in records}}
        self.assertEqual(role_counts, {'dormant-custom-language': 64, 'negative-diagnostic-fixture': 12, 'subject': 411, 'support-file': 72})
        for record in records:
            path = root / record["path"]
            with self.subTest(path=record["path"]):
                self.assertTrue(path.is_file())
                data = path.read_bytes()
                self.assertEqual(len(data), int(record["bytes"]))
                self.assertEqual(sha(data), record["sha256"])

    def test_candidate_function_count_must_match_actual_group_baseline(self):
        fields, results = read_table(self.shards[0] / "results.tsv")
        candidate = next(row for row in results if row["row"] == "1")
        candidate["functions"] = candidate["baseline_functions"] = "2"
        write_table(self.shards[0] / "results.tsv", fields, results)
        report = self.validate(require_clean=False)
        self.assertIn(1, report["candidate_failure_rows"])
        self.assertIn(1, report["telemetry_defect_rows"])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()

    def test_unresolved_reference_count_is_not_candidate_shape_oracle(self):
        for baseline_functions in ("0", "2"):
            with self.subTest(baseline_functions=baseline_functions):
                self.install_structured_reference_failure(baseline_functions)
                report = self.validate(require_clean=False)
                self.assertEqual(report["candidate_failure_rows"], [])
                self.assertEqual(report["reference_failure_rows"], [0, 1, 2, 3])
                self.assertEqual(report["acceptance_failure_rows"], [0, 1, 2, 3])
                self.assertEqual(report["telemetry_defect_rows"], [0])
                self.assertTrue(report["clean_candidate"])
                self.assertFalse(report["clean_acceptance"])
                # A reference-only failure must not block the independent
                # candidate gate, even though it remains an acceptance fail.
                self.assertTrue(self.validate()["clean_candidate"])

    def test_unresolved_reference_does_not_hide_candidate_evidence_defects(self):
        for defect in ("execution", "object", "telemetry", "fallback"):
            with self.subTest(defect=defect):
                self.install_structured_reference_failure()
                shard = self.shards[0]
                fields, results = read_table(shard / "results.tsv")
                candidate = next(row for row in results if row["row"] == "1")
                if defect == "execution":
                    candidate.update({"kind": "1", "status": "1"})
                    write_table(shard / "results.tsv", fields, results)
                elif defect == "object":
                    candidate.update({"object_bytes": "0", "object_sha256": ""})
                    write_table(shard / "results.tsv", fields, results)
                elif defect == "telemetry":
                    candidate["function_records_valid"] = "0"
                    write_table(shard / "results.tsv", fields, results)
                else:
                    telemetry = ("CODEGEN_FALLBACK_FUNCTION version=1 row=1 target=x86_64-linux allocator=mir-stack "
                                 "function_id=9 reason=opcode stage=selection opcode_id=7 line=3 column=2 "
                                 "source_hex=612063 function_hex=66")
                    self.install_single_fallback(0, "1", telemetry)

                report = self.validate(require_clean=False)
                self.assertEqual(report["candidate_failure_rows"], [1])
                self.assertIn(1, report["acceptance_failure_rows"])
                self.assertIn(0, report["reference_failure_rows"])
                self.assertFalse(report["clean_candidate"])
                with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
                    self.validate()

    def test_counters_must_be_canonical_unsigned_decimals(self):
        fields, results = read_table(self.shards[0] / "results.tsv")
        candidate = next(row for row in results if row["row"] == "1")
        candidate["functions"] = "01"
        write_table(self.shards[0] / "results.tsv", fields, results)
        with self.assertRaisesRegex(AssertionError, "non-canonical or out-of-range functions"):
            self.validate()

    def test_direct_reference_fallback_counters_are_row_bound(self):
        fields, results = read_table(self.shards[0] / "results.tsv")
        baseline = next(row for row in results if row["row"] == "0")
        baseline["fallbacks"] = "1"
        write_table(self.shards[0] / "results.tsv", fields, results)
        write_table(self.shards[0] / "fallback-counters.tsv", ("row", "telemetry"), [{
            "row": "0",
            "telemetry": "CODEGEN_FALLBACK_REASON target=x86_64-linux allocator=none reason=opcode count=1 version=1 row=0",
        }])
        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [0, 1, 2, 3])
        self.assertEqual(report["residual_rows"], 1)

    def test_substituted_row_identity_is_rejected(self):
        fields, rows = read_table(self.shards[1] / "rows.tsv")
        row = next(item for item in rows if item["selected"] == "1" and item["allocator"] != "none")
        row["cpu_features"] = "substituted"
        write_table(self.shards[1] / "rows.tsv", fields, rows)
        result_fields, results = read_table(self.shards[1] / "results.tsv")
        result = next(item for item in results if item["row"] == row["row"])
        result["cpu_features"] = "substituted"
        write_table(self.shards[1] / "results.tsv", result_fields, results)
        with self.assertRaisesRegex(AssertionError, "rows.tsv identity mismatch"):
            self.validate()

    def test_substituted_recipe_ledger_is_rejected(self):
        fields, rows = read_table(self.shards[1] / "inputs.tsv")
        rows[0]["fixture_flags"] = "-Dsubstituted"
        write_table(self.shards[1] / "inputs.tsv", fields, rows)
        for argv_path in (self.shards[1] / "groups").glob("*/*.argv"):
            argv = [item.decode() for item in argv_path.read_bytes().rstrip(b"\0").split(b"\0")]
            insertion = len(argv)
            if "-fcodegen-fallback-census" in argv:
                insertion = argv.index("-fcodegen-fallback-census")
            argv.insert(insertion, "-Dsubstituted")
            argv_path.write_bytes(b"\0".join(item.encode() for item in argv) + b"\0")
        with self.assertRaisesRegex(AssertionError, "fixture recipe mismatch"):
            self.validate()

    def test_dirty_disposition_fallback_and_telemetry_require_clean_gate(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        _row_fields, census_rows = read_table(self.shards[1] / "rows.tsv")
        strict_ids = {row["row"] for row in census_rows if row["selected"] == "1" and row["allocator"] != "none"}
        strict = [row for row in rows if row["row"] in strict_ids]
        strict[0]["disposition"] = "supported-native-gap-missing-telemetry"
        strict[0]["counters_valid"] = "0"
        strict[0]["function_records_valid"] = "0"
        strict[1]["kind"] = "1"
        strict[1]["status"] = "1"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(int(strict[0]["row"]), report["telemetry_defect_rows"])
        self.assertIn(int(strict[1]["row"]), report["execution_defect_rows"])
        self.assertEqual(len(report["candidate_failure_rows"]), 2)
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()
        self.assertFalse(json.loads((self.root / "report.json").read_text(encoding="utf-8"))["clean_candidate"])

    def test_supported_failure_disposition_variants_cannot_hide_clean_telemetry(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "5")
        strict["disposition"] = "supported-native-gap-missing-telemetry-v2"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertNotIn(5, report["candidate_failure_rows"])
        self.assertNotIn(5, report["acceptance_failure_rows"])
        self.assertNotIn(5, report["telemetry_defect_rows"])
        self.assertTrue(self.validate()["clean_candidate"])

    def test_baseline_disposition_is_preserved(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        baseline = next(row for row in rows if row["row"] == "0")
        baseline["disposition"] = "baseline-unresolved"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["baseline_dispositions"], {"baseline-unresolved": 1, "baseline-supported": 47})
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [])
        self.assertEqual(report["acceptance_failure_rows"], [])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_inapplicable_baseline_disposition_is_explicitly_preserved(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        baseline = next(row for row in rows if row["row"] == "128")
        census_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == "128")
        self.assertEqual(census_row["execution_obligation"], "unavailable-platform-control")
        baseline["disposition"] = "baseline-unresolved"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn("baseline-unresolved", report["baseline_dispositions"])
        self.assertIn(128, report["inapplicable_rows"])
        self.assertEqual(report["reference_failure_rows"], [])
        self.assertEqual(report["acceptance_failure_rows"], [])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_inapplicable_control_rejects_compile_and_telemetry_defects(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        baseline = next(row for row in rows if row["row"] == "128")
        census_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == "128")
        self.assertEqual(census_row["execution_obligation"], "unavailable-platform-control")
        baseline.update({"disposition": "baseline-unresolved", "kind": "1", "status": "9",
                         "counters_valid": "0", "target_identity_valid": "0",
                         "function_records_valid": "0", "fallbacks": "0", "functions": "0",
                         "object_bytes": "0", "object_sha256": ""})
        # The baseline's function count is the group reference.  Keep the
        # paired candidate evidence row-bound to that repaired reference so
        # this test isolates the reference/control defect.
        for candidate in rows:
            if candidate["group"] == baseline["group"] and candidate["row"] != baseline["row"]:
                candidate["functions"] = candidate["baseline_functions"] = "0"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(128, report["inapplicable_rows"])
        self.assertIn(128, report["reference_failure_rows"])
        self.assertIn(128, report["telemetry_defect_rows"])
        self.assertIn(128, report["execution_defect_rows"])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_inapplicable_target_does_not_excuse_candidate_telemetry(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "129")
        census_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == "129")
        self.assertEqual(census_row["execution_obligation"], "unavailable-platform-control")
        strict["target_identity_valid"] = "0"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(129, report["inapplicable_rows"])
        self.assertIn(129, report["telemetry_defect_rows"])
        self.assertIn(129, report["acceptance_failure_rows"])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_inapplicable_target_does_not_excuse_unresolved_strict_reference(self):
        reference = contract.validate(self.shards[0])
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "129")
        census_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == "129")
        self.assertEqual(census_row["execution_obligation"], "unavailable-platform-control")
        strict["disposition"] = "strict-success-baseline-unresolved"
        write_table(self.shards[0] / "results.tsv", fields, rows)

        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [])
        self.assertEqual(report["acceptance_failure_rows"], [])
        self.assertTrue(report["clean_candidate"])
        self.assertTrue(report["clean_acceptance"])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

        candidate = contract.validate(self.shards[0])
        output = self.root / "reconcile-inapplicable-reference.json"
        transition = contract.reconcile(reference, candidate, output, True)
        self.assertEqual(transition["candidate_common_failure_rows"], [])
        self.assertEqual(transition["reference_common_failure_rows"], [])
        self.assertEqual(transition["acceptance_common_failure_rows"], [])
        self.assertTrue(transition["clean_candidate"])
        self.assertTrue(transition["clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census reference profile"):
            contract.reconcile(reference, candidate, output, False, True)

    def test_inapplicable_target_does_not_excuse_combined_reference_failure(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "129")
        strict["disposition"] = "baseline-and-supported-native-gap"
        write_table(self.shards[0] / "results.tsv", fields, rows)

        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [])
        self.assertEqual(report["acceptance_failure_rows"], [])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_reference_disposition_is_preserved(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "5")
        strict["disposition"] = "strict-success-baseline-unresolved"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_dispositions"], {
            "strict-success": 143, "strict-success-baseline-unresolved": 1})
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [])
        self.assertEqual(report["acceptance_failure_rows"], [])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_candidate_gate_ignores_reference_only_failure_but_rejects_mixed_candidate_failure(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        reference_only = next(row for row in rows if row["row"] == "5")
        candidate_failure = next(row for row in rows if row["row"] == "7")
        reference_only["disposition"] = "strict-success-baseline-unresolved"
        candidate_failure["kind"] = "1"
        candidate_failure["status"] = "1"
        write_table(self.shards[1] / "results.tsv", fields, rows)

        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_failure_rows"], [7])
        self.assertEqual(report["reference_failure_rows"], [])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)
        gated_report = json.loads((self.root / "report.json").read_text(encoding="utf-8"))
        self.assertTrue(gated_report["require_clean_candidate"])
        self.assertFalse(gated_report["require_clean_acceptance"])
        self.assertEqual(gated_report["acceptance_failure_rows"], [7])

        candidate_failure["kind"] = "0"
        candidate_failure["status"] = "0"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate()
        self.assertTrue(report["clean_candidate"])
        self.assertTrue(report["clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_partition_gates_use_their_own_failure_sets(self):
        reports = [contract.validate(shard) for shard in self.shards]
        reference_only = reports[1]["outcomes"]["5"]
        reference_only.update({"candidate_failure": False, "reference_failure": True,
                               "acceptance_failure": True})
        contract.partition_shards(reports, require_clean_candidate=True)
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            contract.partition_shards(reports, require_clean_acceptance=True)

        candidate_failure = reports[1]["outcomes"]["7"]
        candidate_failure.update({"candidate_failure": True, "reference_failure": False,
                                  "acceptance_failure": True})
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            contract.partition_shards(reports, require_clean_candidate=True)
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census profile"):
            contract.partition_shards(reports, require_clean_acceptance=True)

    def test_setup_disposition_is_preserved_but_not_clean(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "1")
        strict["disposition"] = "infrastructure-or-protocol-failure"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["setup_dispositions"], {})
        self.assertTrue(self.validate()["clean_candidate"])

    def test_missing_and_duplicate_shards_are_rejected(self):
        with self.assertRaisesRegex(AssertionError, "shard count mismatch"):
            contract.validate_shards([self.shards[0]], self.root / "missing.json", True)
        with self.assertRaisesRegex(AssertionError, "shard indices are incomplete"):
            contract.validate_shards([self.shards[0], self.shards[0]], self.root / "duplicate.json", True)

    def test_reconcile_excludes_changed_fixture_bytes_and_input_ledger(self):
        candidate_dir = self.root / "candidate-copy"
        shutil.copytree(self.shards[0], candidate_dir)
        for argv_path in candidate_dir.glob("groups/*/*.argv"):
            argv_path.write_bytes(argv_path.read_bytes().replace(str(self.shards[0]).encode(),
                                                                   str(candidate_dir).encode()))
        source_path = candidate_dir / "inputs/tests/unit.c"
        source_path.write_bytes(source_path.read_bytes() + b"\n/* changed */\n")
        support_fields, support_rows = read_table(candidate_dir / "support-contract.tsv")
        input_fields, input_rows = read_table(candidate_dir / "inputs.tsv")
        source_data = source_path.read_bytes()
        for row in support_rows + input_rows:
            row["bytes"] = str(len(source_data))
            row["sha256"] = sha(source_data)
        write_table(candidate_dir / "support-contract.tsv", support_fields, support_rows)
        write_table(candidate_dir / "inputs.tsv", input_fields, input_rows)
        manifest_path = candidate_dir / "manifest.txt"
        manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
        manifest["support_contract_sha256"] = sha((candidate_dir / "support-contract.tsv").read_bytes())
        manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")

        reference = contract.validate(self.shards[0])
        candidate = contract.validate(candidate_dir)
        output = self.root / "reconcile.json"
        report = contract.reconcile(reference, candidate, output, False)
        self.assertEqual(report["common_rows"], 0)
        self.assertEqual(report["removed_rows"], len(reference["selected_rows"]))
        self.assertEqual(report["added_rows"], len(candidate["selected_rows"]))
        self.assertNotEqual(report["reference_input_ledger_sha256"], report["candidate_input_ledger_sha256"])

    def test_reconcile_keeps_common_rows_across_candidate_binary_revision(self):
        candidate_dir = self.root / "candidate-rebuilt"
        shutil.copytree(self.shards[0], candidate_dir)
        for argv_path in candidate_dir.glob("groups/*/*.argv"):
            argv_path.write_bytes(argv_path.read_bytes().replace(str(self.shards[0]).encode(),
                                                                   str(candidate_dir).encode()))
        binary_path = candidate_dir / "candidate-ide.exe"
        binary_path.write_bytes(binary_path.read_bytes() + b"\nrebuilt\n")
        manifest_path = candidate_dir / "manifest.txt"
        manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
        manifest.update({"compiler_revision_claim": "c" * 40,
                         "compiler_bytes": str(binary_path.stat().st_size),
                         "compiler_sha256": sha(binary_path.read_bytes())})
        manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")

        reference = contract.validate(self.shards[0])
        candidate = contract.validate(candidate_dir)
        output = self.root / "reconcile-rebuilt.json"
        report = contract.reconcile(reference, candidate, output, False)
        self.assertEqual(report["common_rows"], 96)
        self.assertEqual(report["removed_rows"], 0)
        self.assertEqual(report["added_rows"], 0)
        self.assertNotEqual(report["reference_compiler_sha256"], report["candidate_compiler_sha256"])
        self.assertNotEqual(report["reference_compiler_revision_claim"],
                             report["candidate_compiler_revision_claim"])

    def test_reconcile_separates_candidate_and_reference_common_failures(self):
        reference = contract.validate(self.shards[1])
        candidate = copy.deepcopy(reference)
        candidate["directory"] = str(self.root / "candidate-report")
        reference_key = next(key for key, item in candidate["identities"].items() if item["row"] == 5)
        candidate_key = next(key for key, item in candidate["identities"].items() if item["row"] == 7)
        candidate["identities"][reference_key].update({
            "disposition": "strict-success-baseline-unresolved",
            "candidate_failure": False,
            "reference_failure": True,
            "acceptance_failure": True,
        })
        candidate["identities"][candidate_key].update({
            "fallbacks": 1,
            "candidate_failure": True,
            "reference_failure": False,
            "acceptance_failure": True,
        })
        output = self.root / "reconcile-gates.json"

        report = contract.reconcile(reference, candidate, output, False)
        self.assertEqual(report["schema"], 2)
        self.assertEqual(report["candidate_common_failure_rows"], [7])
        self.assertEqual(report["reference_common_failure_rows"], [5])
        self.assertEqual(report["acceptance_common_failure_rows"], [5, 7])
        self.assertFalse(report["require_clean_candidate"])
        self.assertFalse(report["require_clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved common rows"):
            contract.reconcile(reference, candidate, output, True)
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census reference profile"):
            contract.reconcile(reference, candidate, output, False, True)

        candidate["identities"][candidate_key].update({
            "fallbacks": 0,
            "candidate_failure": False,
            "acceptance_failure": False,
        })
        report = contract.reconcile(reference, candidate, output, True)
        self.assertTrue(report["clean_candidate"])
        self.assertFalse(report["clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census reference profile"):
            contract.reconcile(reference, candidate, output, False, True)

    def test_reconcile_and_cli_acceptance_require_both_full_profiles(self):
        reference = contract.validate(self.shards[0])
        candidate = contract.validate(self.shards[1])
        output = self.root / "reconcile-profile-gate.json"
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census reference profile"):
            contract.reconcile(reference, candidate, output, False, True)

        full_reference = copy.deepcopy(reference)
        full_reference["profile"] = contract.FULL_CENSUS_PROFILE
        full_reference["manifest"] = dict(full_reference["manifest"], profile=contract.FULL_CENSUS_PROFILE)
        with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census candidate profile"):
            contract.reconcile(full_reference, candidate, output, False, True)

        saved_argv = sys.argv
        try:
            sys.argv = ["native_retirement_contract.py", "compare", str(self.shards[0]), str(self.shards[1]),
                        "--out", str(self.root / "reconcile-cli-profile-gate.json"),
                        "--require-clean-acceptance"]
            with self.assertRaisesRegex(AssertionError, "production acceptance requires full-census reference profile"):
                contract.main()
        finally:
            sys.argv = saved_argv

    def test_reconcile_keeps_common_rows_across_unrelated_input_addition(self):
        candidate_dir = self.root / "candidate-expanded"
        shutil.copytree(self.shards[0], candidate_dir)
        for argv_path in candidate_dir.glob("groups/*/*.argv"):
            argv_path.write_bytes(argv_path.read_bytes().replace(str(self.shards[0]).encode(),
                                                                   str(candidate_dir).encode()))
        extra = b"/* unrelated support input */\n"
        (candidate_dir / "inputs/tests/unused.h").write_bytes(extra)
        support_fields, support_rows = read_table(candidate_dir / "support-contract.tsv")
        input_fields, input_rows = read_table(candidate_dir / "inputs.tsv")
        support_rows.append({"path": "tests/unused.h", "role": "support-file",
                             "compile_obligation": "dependency-only", "bytes": str(len(extra)),
                             "sha256": sha(extra)})
        input_rows.append({"path": "tests/unused.h", "role": "support-file",
                           "compile_obligation": "dependency-only", "bytes": str(len(extra)),
                           "buster_hash_64": "0", "sha256": sha(extra),
                           "fixture_recipe": "compiler-default", "fixture_flags": ""})
        support_rows.sort(key=lambda row: row["path"])
        input_rows.sort(key=lambda row: row["path"])
        write_table(candidate_dir / "support-contract.tsv", support_fields, support_rows)
        write_table(candidate_dir / "inputs.tsv", input_fields, input_rows)
        manifest_path = candidate_dir / "manifest.txt"
        manifest = dict(line.split("=", 1) for line in manifest_path.read_text(encoding="utf-8").splitlines())
        manifest.update({"inputs": "2",
                         "support_contract_sha256": sha((candidate_dir / "support-contract.tsv").read_bytes())})
        manifest_path.write_text("".join(f"{key}={value}\n" for key, value in manifest.items()), encoding="utf-8")

        reference = contract.validate(self.shards[0])
        candidate = contract.validate(candidate_dir)
        output = self.root / "reconcile-expanded.json"
        report = contract.reconcile(reference, candidate, output, False)
        self.assertEqual(report["common_rows"], 96)
        self.assertEqual(report["removed_rows"], 0)
        self.assertEqual(report["added_rows"], 0)
        self.assertEqual(report["reference_rows_identity_sha256"], report["candidate_rows_identity_sha256"])
        self.assertNotEqual(report["reference_input_ledger_sha256"], report["candidate_input_ledger_sha256"])
        self.assertNotEqual(report["reference_manifest_identity_sha256"],
                             report["candidate_manifest_identity_sha256"])

    def test_applicability_is_explicit_and_validator_owned(self):
        report = self.validate(require_clean=False)
        self.assertEqual(report["applicability_classes"], list(contract.APPLICABILITY_CLASSES))
        self.assertEqual(report["applicability_rows"], 192)
        self.assertEqual(report["applicability_counts"], {
            "admitted-supported": 132,
            "platform-inapplicable": 16,
            "retained-control": 44,
            "retained-reference": 0,
            "unavailable": 0,
        })
        fields, rows = read_table(self.root / "applicability.tsv")
        self.assertEqual(fields, contract.APPLICABILITY_FIELDS)
        self.assertEqual(len(rows), 192)
        self.assertEqual([int(row["row"]) for row in rows], list(range(192)))
        self.assertEqual({row["applicability"] for row in rows},
                         {"admitted-supported", "platform-inapplicable", "retained-control"})
        self.assertTrue(all(row["applicability"] == row["admission"] for row in rows))
        self.assertTrue(all(row["reason"] and row["ownership"] and row["disposition"] for row in rows))

        # A producer cannot opt a supported row out by appending an
        # applicability field: rows.tsv has a closed, identity-bearing schema.
        row_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        census_rows[0]["applicability"] = "unavailable"
        write_table(self.shards[0] / "rows.tsv", row_fields + ("applicability",), census_rows)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_reference_classification_cannot_hide_supported_candidate_failure(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "5")
        strict.update({"disposition": "strict-success-baseline-unresolved", "kind": "1", "status": "1"})
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(5, report["candidate_failure_rows"])
        self.assertIn(5, report["acceptance_failure_rows"])
        self.assertFalse(report["clean_candidate"])
        applicability_fields, applicability_rows = read_table(self.root / "applicability.tsv")
        row = next(item for item in applicability_rows if item["row"] == "5")
        self.assertEqual(row["applicability"], "admitted-supported")
        self.assertEqual(row["candidate_failure"], "1")
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()

    def test_residual_tsv_is_bounded_and_attributed(self):
        result_fields, results = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in results if row["row"] == "5")
        strict["fallbacks"] = "1"
        write_table(self.shards[1] / "results.tsv", result_fields, results)
        path = self.shards[1] / "fallback-functions.tsv"
        telemetry = ("CODEGEN_FALLBACK_FUNCTION version=1 row=5 target=x86_64-linux allocator=mir-stack "
                     "function_id=9 reason=opcode stage=selection opcode_id=7 line=3 column=2 "
                     "source_hex=612063 function_hex=66")
        write_table(path, ("row", "record_valid", "telemetry"),
                    [{"row": "5", "record_valid": "1", "telemetry": telemetry}])
        write_table(self.shards[1] / "fallback-counters.tsv", ("row", "telemetry"), [
            {"row": "5", "telemetry": "CODEGEN_FALLBACK_REASON target=x86_64-linux allocator=mir-stack reason=opcode count=1 version=1 row=5"},
            {"row": "5", "telemetry": "CODEGEN_FALLBACK opcode=7 count=1 version=1 row=5 target=x86_64-linux allocator=mir-stack"},
        ])
        report = self.validate(require_clean=False)
        self.assertEqual(report["residual_rows"], 3)
        fields, rows = read_table(self.root / "residual.tsv")
        self.assertEqual(fields, contract.RESIDUAL_FIELDS)
        self.assertEqual(len(rows), 3)
        residual = next(row for row in rows if row["function_id"] == "9")
        self.assertEqual(residual["fixture"], "tests/unit.c")
        self.assertEqual(residual["function"], "f")
        self.assertEqual(residual["function_id"], "9")
        self.assertEqual(residual["target"], "x86_64-unknown-linux-gnu")
        self.assertEqual(residual["cpu"], "baseline")
        self.assertEqual(residual["frontend"], "local-backed-canonical")
        self.assertEqual(residual["allocator"], "mir-stack")
        self.assertEqual(residual["PIC"], "1")
        self.assertEqual(residual["reason"], "opcode")
        self.assertIn("CODEGEN_FALLBACK_FUNCTION", residual["diagnostic"])

        # A full 256-function source plus a counter must report truncation: the
        # counter is not silently dropped before the global cap is applied.
        function_records = []
        for function_id in range(contract.MAX_RESIDUAL_ROWS):
            function_records.append({"row": "5", "record_valid": "1", "telemetry": telemetry.replace("function_id=9", f"function_id={function_id}")})
        strict["fallbacks"] = str(contract.MAX_RESIDUAL_ROWS)
        write_table(self.shards[1] / "results.tsv", result_fields, results)
        write_table(path, ("row", "record_valid", "telemetry"),
                    function_records)
        write_table(self.shards[1] / "fallback-counters.tsv", ("row", "telemetry"), [
            {"row": "5", "telemetry": f"CODEGEN_FALLBACK_REASON target=x86_64-linux allocator=mir-stack reason=opcode count={contract.MAX_RESIDUAL_ROWS} version=1 row=5"},
            {"row": "5", "telemetry": "CODEGEN_FALLBACK opcode=7 count=256 version=1 row=5 target=x86_64-linux allocator=mir-stack"},
        ])
        report = self.validate(require_clean=False)
        self.assertEqual(report["residual_rows"], contract.MAX_RESIDUAL_ROWS)
        self.assertTrue(report["residual_truncated"])
        residual_bytes = (self.root / "residual.tsv").read_bytes()
        function_fields, function_rows = read_table(path)
        write_table(path, function_fields, list(reversed(function_rows)))
        self.validate(require_clean=False)
        self.assertEqual((self.root / "residual.tsv").read_bytes(), residual_bytes)

    def test_residual_diagnostics_are_row_bound_and_complete(self):
        result_fields, results = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in results if row["row"] == "5")
        _row_fields, census_rows = read_table(self.shards[1] / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == "5")
        self.assertEqual(contract._validate_counter_diagnostic(
            "CODEGEN_FALLBACK_STAGES verify=1 placement=2 encode=3 version=1 row=5 "
            "target=x86_64-linux allocator=mir-stack", census_row)["encode"], "3")
        strict["fallbacks"] = "1"
        write_table(self.shards[1] / "results.tsv", result_fields, results)
        telemetry = ("CODEGEN_FALLBACK_FUNCTION version=1 row=5 target=x86_64-linux allocator=mir-stack "
                     "function_id=9 reason=opcode stage=selection opcode_id=7 line=3 column=2 "
                     "source_hex=612063 function_hex=66")
        function_path = self.shards[1] / "fallback-functions.tsv"
        write_table(function_path, ("row", "record_valid", "telemetry"),
                    [{"row": "5", "record_valid": "1", "telemetry": telemetry}])
        write_table(self.shards[1] / "fallback-counters.tsv", ("row", "telemetry"), [
            {"row": "5", "telemetry": "CODEGEN_FALLBACK_REASON target=x86_64-linux allocator=mir-stack reason=opcode count=1 version=1 row=5"},
            {"row": "5", "telemetry": "CODEGEN_FALLBACK opcode=7 count=1 version=1 row=5 target=x86_64-linux allocator=mir-stack"},
        ])
        self.validate(require_clean=False)

        fields, rows = read_table(function_path)
        rows[0]["row"] = "7"
        write_table(function_path, fields, rows)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)
        rows[0]["row"] = "5"
        rows[0]["record_valid"] = "0"
        write_table(function_path, fields, rows)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)
        rows[0]["record_valid"] = "1"
        rows[0]["telemetry"] = telemetry.replace("source_hex=612063", "source_hex=")
        write_table(function_path, fields, rows)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_function_diagnostic_inner_row_rejects_identity_reassignment(self):
        _result_fields, results = read_table(self.shards[0] / "results.tsv")
        outer = next(row for row in results if row["row"] == "9")
        _row_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        outer_row = next(row for row in census_rows if row["row"] == "9")
        inner_row = next(row for row in census_rows if row["row"] == "5")
        self.assertEqual(outer_row["target"], inner_row["target"])
        self.assertEqual(outer_row["allocator"], inner_row["allocator"])
        self.assertNotEqual(outer_row["frontend_lowering"], inner_row["frontend_lowering"])
        self.assertNotEqual(outer_row["PIC"], inner_row["PIC"])
        self.assertNotEqual(outer_row["group"], inner_row["group"])
        outer["fallbacks"] = "1"
        write_table(self.shards[0] / "results.tsv", contract.RESULT_FIELDS, results)
        telemetry = ("CODEGEN_FALLBACK_FUNCTION version=1 row=5 target=x86_64-linux allocator=mir-stack "
                     "function_id=9 reason=opcode stage=selection opcode_id=7 line=3 column=2 "
                     "source_hex=612063 function_hex=66")
        write_table(self.shards[0] / "fallback-functions.tsv", ("row", "record_valid", "telemetry"),
                    [{"row": "9", "record_valid": "1", "telemetry": telemetry}])
        write_table(self.shards[0] / "fallback-counters.tsv", ("row", "telemetry"), [
            {"row": "9", "telemetry": "CODEGEN_FALLBACK_REASON target=x86_64-linux allocator=mir-stack reason=opcode count=1 version=1 row=9"},
            {"row": "9", "telemetry": "CODEGEN_FALLBACK opcode=7 count=1 version=1 row=9 target=x86_64-linux allocator=mir-stack"},
        ])
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_source_hex_sentinel_is_rejected(self):
        telemetry = ("CODEGEN_FALLBACK_FUNCTION version=1 row=5 target=x86_64-linux allocator=mir-stack "
                     "function_id=9 reason=opcode stage=selection opcode_id=7 line=3 column=2 "
                     "source_hex=- function_hex=66")
        self.install_single_fallback(1, "5", telemetry)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_function_hex_sentinel_is_rejected(self):
        telemetry = ("CODEGEN_FALLBACK_FUNCTION version=1 row=5 target=x86_64-linux allocator=mir-stack "
                     "function_id=9 reason=opcode stage=selection opcode_id=7 line=3 column=2 "
                     "source_hex=612063 function_hex=-")
        self.install_single_fallback(1, "5", telemetry)
        with self.assertRaises(AssertionError):
            self.validate(require_clean=False)

    def test_admitted_supported_gap_remains_candidate_owned(self):
        self.declare_gaps({("tests/unit.c", "x86_64-unknown-linux-gnu", "local-backed-canonical", "1", "mir-stack"),
                           ("tests/unit.c", "x86_64-apple-ios", "local-backed-canonical", "0", "mir-stack")})
        fields, rows = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "5")
        strict["disposition"] = "supported-native-gap"
        strict["kind"] = "1"
        strict["status"] = "1"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(5, report["candidate_failure_rows"])
        applicability_fields, applicability_rows = read_table(self.root / "applicability.tsv")
        row = next(item for item in applicability_rows if item["row"] == "5")
        self.assertEqual(row["applicability"], "admitted-supported")
        self.assertEqual(row["ownership"], "candidate-compiler")

        # The platform-control target does not turn a declared supported gap
        # into an inapplicable row.
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "129")
        strict["disposition"] = "supported-native-gap-missing-telemetry"
        strict["counters_valid"] = "0"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["supported_gap_count"], 2)
        _fields, applicability_rows = read_table(self.root / "applicability.tsv")
        row = next(item for item in applicability_rows if item["row"] == "129")
        self.assertEqual(row["applicability"], "admitted-supported")

    def make_reference_supplements(self):
        import native_retirement_reference as reference
        for directory in self.shards:
            candidate = (directory / "candidate-ide.exe").read_bytes()
            (directory / "baseline-ide.exe").write_bytes(candidate)
            manifest_path = directory / "manifest.txt"
            text = manifest_path.read_text().replace("baseline_revision_claim=" + "b" * 40,
                                                    "baseline_revision_claim=" + "a" * 40)
            text = re.sub(r"baseline_bytes=.*", "baseline_bytes=" + str(len(candidate)), text)
            text = re.sub(r"baseline_sha256=.*", "baseline_sha256=" + sha(candidate), text)
            manifest_path.write_text(text)
            fields, results = read_table(directory / "results.tsv")
            baseline = next(row for row in results if row["row"] == str(int(row["group"]) * 4))
            baseline["status"] = "1"
            write_table(directory / "results.tsv", fields, results)
            report = contract.validate(directory)
            output = directory / "reference-supplement"
            output.mkdir()
            (output / "clang.exe").write_bytes(b"test compiler identity")
            (output / "version.txt").write_bytes(b"clang version test\n")
            records = []
            for outcome in report["outcomes"].values():
                if outcome["side"] != "baseline" or not outcome["reference_failure"]:
                    continue
                row = report["selected_row_records"][str(outcome["row"]) ]
                prefix = output / row["group"]
                # The first selected groups are x86-64 ELF object controls.
                data = bytearray(64)
                data[:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<HH", data, 16, 1, 62)
                prefix.with_suffix(".o").write_bytes(data)
                for stream in ("stdout", "stderr"):
                    prefix.with_suffix("." + stream).write_bytes(b"")
                argv = reference.command(contract.read_argv(directory / row["argv_evidence"]), row,
                                         output / "clang.exe", prefix.with_suffix(".o"))
                prefix.with_suffix(".argv.json").write_text(json.dumps(argv))
                records.append({"row": row["row"], "group": row["group"], "status": 0,
                                "object_bytes": len(data), "object_sha256": sha(data),
                                "stdout_sha256": sha(b""), "stderr_sha256": sha(b""), "oracle": "clang-object"})
            (output / "manifest.json").write_text(json.dumps({
                "schema": 1, "directory": str(directory),
                "compiler_sha256": reference.digest(output / "clang.exe"),
                "version_sha256": reference.digest(output / "version.txt"),
                "census_manifest_sha256": reference.digest(directory / "manifest.txt"),
                "rows_identity_sha256": report["rows_identity_sha256"],
                "input_ledger_sha256": report["input_ledger_sha256"],
                "environment": {"LANG": "C", "LC_ALL": "C", "TZ": "UTC"}, "results": records}))

    def test_reference_supplement_preserves_direct_failures_and_candidate_gate(self):
        self.make_reference_supplements()
        report = contract.validate_shards(self.shards, self.root / "supplement.json", reference_supplements=True)
        self.assertEqual(len(report["direct_reference_failure_rows"]), 8)
        self.assertEqual(report["reference_failure_rows"], [])
        fields, results = read_table(self.shards[0] / "results.tsv")
        candidate = next(row for row in results if int(row["row"]) % 4 == 1)
        candidate["status"] = "1"
        write_table(self.shards[0] / "results.tsv", fields, results)
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            contract.validate_shards(self.shards, self.root / "candidate-failed.json", True, reference_supplements=True)

    def test_reference_supplement_rejects_missing_rows_and_tampered_argv(self):
        self.make_reference_supplements()
        output = self.shards[0] / "reference-supplement"
        manifest_path = output / "manifest.json"
        original = manifest_path.read_text()
        manifest = json.loads(original)
        manifest["results"] = []
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(AssertionError, "incomplete reference supplement"):
            contract.validate_shards(self.shards, self.root / "missing.json", reference_supplements=True)
        manifest_path.write_text(original)
        argv = next(output.glob("*.argv.json"))
        command = json.loads(argv.read_text())
        command.append("-DREPLACE_SOURCE")
        argv.write_text(json.dumps(command))
        with self.assertRaisesRegex(AssertionError, "reference argv mismatch"):
            contract.validate_shards(self.shards, self.root / "argv.json", reference_supplements=True)

    def test_reference_supplement_checks_target_object_header(self):
        self.make_reference_supplements()
        output = self.shards[0] / "reference-supplement"
        artifact = next(output.glob("*.o"))
        data = bytearray(artifact.read_bytes())
        struct.pack_into("<H", data, 18, 183)  # AArch64 bytes in an x86-64 row.
        artifact.write_bytes(data)
        manifest_path = output / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["results"][0]["object_sha256"] = sha(data)
        manifest_path.write_text(json.dumps(manifest))
        report = contract.validate_shards(self.shards, self.root / "wrong-target.json", reference_supplements=True)
        self.assertEqual(len(report["reference_failure_rows"]), 4)



class CheckedInDependencyTests(unittest.TestCase):
    def test_native_dispatch_cannot_register_retired_direct_emitter(self):
        root = Path(__file__).resolve().parents[1]
        codegen = (root / "src/buster/lib/compiler/codegen/codegen.c").read_text(encoding="utf-8")
        private = (root / "src/buster/lib/compiler/codegen/codegen_internal.h").read_text(encoding="utf-8")
        public = (root / "src/buster/lib/compiler/codegen/codegen.h").read_text(encoding="utf-8")
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        unity = (root / "src/buster/apps/ide/ide.c").read_text(encoding="utf-8")

        self.assertIn("machine_select_validated_canonical_function(", codegen)
        self.assertIn("options.register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK;", codegen)
        self.assertIn("machine_stack_placement_build(", codegen)
        self.assertIn("CODEGEN_REGISTER_ALLOCATOR_NONE", public)
        for retired in ("CCanonicalEmitter", "CCanonicalBranchPatch", "X64Builder",
                        "CodegenRegisterAllocation", "X64Evex", "CODEGEN_X64_X87_SCRATCH_SIZE",
                        "x64_emit_vector_native_memory", "x64_emit_vector_native_binary_operation",
                        "codegen_canonical_x64_metadata_vector",
                        "x64_emit_vzeroupper", "a64_emit_initialize_aggregate_result",
                        "a64_emit_copy_memory_registers(", "a64_emit_float_load_offset(",
                        "a64_emit_float_store_offset(", "x64_target_supports_native_vector(",
                        "CodegenRelocation",
                        "canonical_prep", "canonical_emit("):
            with self.subTest(retired=retired):
                self.assertNotIn(retired, codegen + private)

        # Both source graphs register the shared codegen module. Neither may
        # grow a second native emitter or link the archived reference compiler.
        self.assertIn('buster_register_module(compiler_codegen "${BUSTER_SOURCE_DIR}/compiler/codegen/codegen${COMMON_EXTENSION}")', cmake)
        self.assertIn('#include <buster/lib/compiler/codegen/codegen.c>', unity)
        for registry in (cmake, unity):
            self.assertNotRegex(registry, r'(?m)^(?:.*register_module|\s*#include)\b[^\n]*(?:direct_native|native_direct|retirement_reference)')

    def test_historical_gap_ledger_maps_to_current_row_numbers(self):
        root = Path(__file__).resolve().parents[1]
        _fields, inputs = read_table(root / "docs/native-retirement-support-v1.tsv")
        ledger_path = root / "docs/native-retirement-supported-gaps-v1.tsv"
        self.assertEqual(sha(ledger_path.read_bytes()), contract.FULL_SUPPORTED_GAP_LEDGER_SHA256)
        _fields, gaps = read_table(ledger_path)
        subjects = [row["path"] for row in inputs if row["role"] == "subject"]
        identities = [(fixture, target, frontend, pic, allocator)
                      for fixture in subjects for target in contract.TARGETS
                      for frontend in ("local-backed-canonical", "direct-ssa")
                      for pic in ("0", "1") for allocator in contract.ALLOCATORS]
        row_by_identity = {identity: index for index, identity in enumerate(identities)}
        fields = ("fixture", "target", "frontend_lowering", "PIC", "allocator")
        rows = sorted(row_by_identity[tuple(gap[field] for field in fields)] for gap in gaps)
        self.assertEqual(len(rows), contract.FULL_SUPPORTED_GAP_COUNT)
        self.assertEqual(contract.canonical_rows_digest(rows), contract.FULL_SUPPORTED_GAP_SHA256)

    def test_full_census_dimensions_match_reviewed_support_inventory(self):
        root = Path(__file__).resolve().parents[1]
        _fields, rows = read_table(root / "docs/native-retirement-support-v1.tsv")
        subjects = sum(row["role"] == "subject" for row in rows)
        groups = subjects * len(contract.TARGETS) * len(("local-backed-canonical", "direct-ssa")) * len(("0", "1"))
        counts = {"input": len(rows), "subject": subjects, "group": groups,
                  "row": groups * len(contract.ALLOCATORS)}
        self.assertEqual(subjects, contract.FULL_SUBJECT_COUNT)
        self.assertEqual(counts["row"], contract.FULL_ROW_COUNT)
        producer = (root / "tools/native_retirement_census.c").read_text(encoding="utf-8")
        for name, count in counts.items():
            with self.subTest(dimension=name):
                match = re.search(r'nrc_full_' + name + r'_count = ([0-9]+);', producer)
                self.assertIsNotNone(match)
                self.assertEqual(int(match.group(1)), count)

    def test_reviewed_dependency_identities_match_single_generated_authority(self):
        root = Path(__file__).resolve().parents[1]
        binding, resolved, snapshot = dependency_authority.load_authority(root)
        policy = (root / dependency_authority.POLICY_PATH).read_bytes()
        snapshot_raw = (root / dependency_authority.SNAPSHOT_PATH).read_bytes()
        records, _metadata = materializer.parse_manifest(resolved)
        self.assertEqual(sha(policy), binding["policy_sha256"])
        self.assertEqual(sha(snapshot_raw), binding["snapshot_sha256"])
        self.assertEqual(sha(materializer._ledger(records)), binding["ledger_sha256"])
        self.assertEqual(snapshot, contract._DEPENDENCY_SNAPSHOT)
        producer = (root / "tools/native_retirement_census.c").read_text(encoding="utf-8")
        self.assertIn('#include "native_retirement_dependency_binding.generated.h"', producer)
        for value in (binding["policy_sha256"], binding["receipt_sha256"],
                      binding["project_sha256"], binding["ledger_sha256"]):
            self.assertNotIn(value, producer)



if __name__ == "__main__":
    unittest.main()
