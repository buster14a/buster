#!/usr/bin/env python3
"""Filesystem-backed regressions for the native-retirement v2 validator."""

import csv
import copy
import hashlib
import json
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_retirement_contract as contract


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
            "system_include": "none", "inputs": "1", "rows": "192", "fixture_filter": "",
            "target_filter": "", "shard_index": str(shard_index), "shard_count": "2",
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

    def test_clean_complete_partition_passes(self):
        report = self.validate()
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

    def test_counters_must_be_canonical_unsigned_decimals(self):
        fields, results = read_table(self.shards[0] / "results.tsv")
        candidate = next(row for row in results if row["row"] == "1")
        candidate["functions"] = "01"
        write_table(self.shards[0] / "results.tsv", fields, results)
        with self.assertRaisesRegex(AssertionError, "non-canonical or out-of-range functions"):
            self.validate()

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
        strict[1]["fallbacks"] = "1"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(int(strict[0]["row"]), report["telemetry_defect_rows"])
        self.assertIn(int(strict[1]["row"]), report["fallback_defect_rows"])
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
        self.assertIn(5, report["candidate_failure_rows"])
        self.assertIn(5, report["acceptance_failure_rows"])
        self.assertNotIn(5, report["telemetry_defect_rows"])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()

    def test_baseline_disposition_is_preserved(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        baseline = next(row for row in rows if row["row"] == "0")
        baseline["disposition"] = "baseline-unresolved"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["baseline_dispositions"], {"baseline-unresolved": 1, "baseline-supported": 47})
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [0])
        self.assertEqual(report["acceptance_failure_rows"], [0])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
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
        self.assertEqual(report["reference_failure_rows"], [128])
        self.assertEqual(report["acceptance_failure_rows"], [128])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_inapplicable_control_rejects_compile_and_telemetry_defects(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        baseline = next(row for row in rows if row["row"] == "128")
        census_fields, census_rows = read_table(self.shards[0] / "rows.tsv")
        census_row = next(row for row in census_rows if row["row"] == "128")
        self.assertEqual(census_row["execution_obligation"], "unavailable-platform-control")
        baseline.update({"disposition": "baseline-unresolved", "kind": "1", "status": "9",
                         "counters_valid": "0", "target_identity_valid": "0",
                         "function_records_valid": "0", "fallbacks": "3", "functions": "0",
                         "object_bytes": "0", "object_sha256": ""})
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertIn(128, report["inapplicable_rows"])
        self.assertIn(128, report["reference_failure_rows"])
        self.assertIn(128, report["fallback_defect_rows"])
        self.assertIn(128, report["telemetry_defect_rows"])
        self.assertIn(128, report["execution_defect_rows"])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
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
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
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
        self.assertEqual(report["reference_failure_rows"], [129])
        self.assertEqual(report["acceptance_failure_rows"], [129])
        self.assertTrue(report["clean_candidate"])
        self.assertFalse(report["clean_acceptance"])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            self.validate(require_clean=False, require_clean_acceptance=True)

        candidate = contract.validate(self.shards[0])
        output = self.root / "reconcile-inapplicable-reference.json"
        transition = contract.reconcile(reference, candidate, output, True)
        self.assertEqual(transition["candidate_common_failure_rows"], [])
        self.assertEqual(transition["reference_common_failure_rows"], [129])
        self.assertEqual(transition["acceptance_common_failure_rows"], [129])
        self.assertTrue(transition["clean_candidate"])
        self.assertFalse(transition["clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved common rows"):
            contract.reconcile(reference, candidate, output, False, True)

    def test_inapplicable_target_does_not_excuse_combined_reference_failure(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "129")
        strict["disposition"] = "baseline-and-supported-native-gap"
        write_table(self.shards[0] / "results.tsv", fields, rows)

        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_failure_rows"], [129])
        self.assertEqual(report["reference_failure_rows"], [129])
        self.assertEqual(report["acceptance_failure_rows"], [129])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_reference_disposition_is_preserved(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "5")
        strict["disposition"] = "strict-success-baseline-unresolved"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["reference_dispositions"], {"strict-success-baseline-unresolved": 1})
        self.assertEqual(report["candidate_failure_rows"], [])
        self.assertEqual(report["reference_failure_rows"], [5])
        self.assertEqual(report["acceptance_failure_rows"], [5])
        self.assertTrue(self.validate()["clean_candidate"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_candidate_gate_ignores_reference_only_failure_but_rejects_mixed_candidate_failure(self):
        fields, rows = read_table(self.shards[1] / "results.tsv")
        reference_only = next(row for row in rows if row["row"] == "5")
        candidate_failure = next(row for row in rows if row["row"] == "7")
        reference_only["disposition"] = "strict-success-baseline-unresolved"
        candidate_failure["fallbacks"] = "1"
        write_table(self.shards[1] / "results.tsv", fields, rows)

        report = self.validate(require_clean=False)
        self.assertEqual(report["candidate_failure_rows"], [7])
        self.assertEqual(report["reference_failure_rows"], [5])
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            self.validate()
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            self.validate(require_clean=False, require_clean_acceptance=True)
        gated_report = json.loads((self.root / "report.json").read_text(encoding="utf-8"))
        self.assertFalse(gated_report["require_clean_candidate"])
        self.assertTrue(gated_report["require_clean_acceptance"])
        self.assertEqual(gated_report["acceptance_failure_rows"], [5, 7])

        candidate_failure["fallbacks"] = "0"
        write_table(self.shards[1] / "results.tsv", fields, rows)
        report = self.validate()
        self.assertTrue(report["clean_candidate"])
        self.assertFalse(report["clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            self.validate(require_clean=False, require_clean_acceptance=True)

    def test_partition_gates_use_their_own_failure_sets(self):
        reports = [contract.validate(shard) for shard in self.shards]
        reference_only = reports[1]["outcomes"]["5"]
        reference_only.update({"candidate_failure": False, "reference_failure": True,
                               "acceptance_failure": True})
        contract.partition_shards(reports, require_clean_candidate=True)
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            contract.partition_shards(reports, require_clean_acceptance=True)

        candidate_failure = reports[1]["outcomes"]["7"]
        candidate_failure.update({"candidate_failure": True, "reference_failure": False,
                                  "acceptance_failure": True})
        with self.assertRaisesRegex(AssertionError, "candidate has unresolved rows"):
            contract.partition_shards(reports, require_clean_candidate=True)
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved rows"):
            contract.partition_shards(reports, require_clean_acceptance=True)

    def test_setup_disposition_is_preserved_but_not_clean(self):
        fields, rows = read_table(self.shards[0] / "results.tsv")
        strict = next(row for row in rows if row["row"] == "1")
        strict["disposition"] = "infrastructure-or-protocol-failure"
        write_table(self.shards[0] / "results.tsv", fields, rows)
        report = self.validate(require_clean=False)
        self.assertEqual(report["setup_dispositions"], {"infrastructure-or-protocol-failure": 1})
        with self.assertRaises(AssertionError):
            self.validate()

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
        self.assertEqual(report["removed_rows"], reference["rows"])
        self.assertEqual(report["added_rows"], candidate["rows"])
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
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved common rows"):
            contract.reconcile(reference, candidate, output, False, True)

        candidate["identities"][candidate_key].update({
            "fallbacks": 0,
            "candidate_failure": False,
            "acceptance_failure": False,
        })
        report = contract.reconcile(reference, candidate, output, True)
        self.assertTrue(report["clean_candidate"])
        self.assertFalse(report["clean_acceptance"])
        with self.assertRaisesRegex(AssertionError, "acceptance has unresolved common rows"):
            contract.reconcile(reference, candidate, output, False, True)

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

if __name__ == "__main__":
    unittest.main()
