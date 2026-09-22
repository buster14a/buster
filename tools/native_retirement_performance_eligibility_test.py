#!/usr/bin/env python3
"""Regression tests for #929's trusted sparse eligibility projection."""

import ast
import copy
import csv
from pathlib import Path

import native_retirement_contract as census
import hashlib
import json
import unittest

import native_retirement_performance_binding as binding
import native_retirement_performance_schema as schema


class RetirementEligibilityTests(unittest.TestCase):
    def report(self):
        rows_by_class = {name: [] for name in schema.APPLICABILITY_CLASSES}
        rows_by_class["admitted-supported"] = [0, 4]
        rows_by_class["retained-control"] = [1]
        rows_by_class["platform-inapplicable"] = [2]
        rows_by_class["unavailable"] = [3]
        counts = {name: len(rows) for name, rows in rows_by_class.items()}
        return {
            "profile": "full-census",
            "applicability_classes": list(schema.APPLICABILITY_CLASSES),
            "admission_classes": list(schema.APPLICABILITY_CLASSES),
            "applicability_counts": counts,
            "admission_counts": dict(counts),
            "applicability_rows_by_class": rows_by_class,
            "admission_rows_by_class": copy.deepcopy(rows_by_class),
            "applicability_rows": 5,
            "applicability_skip_rows": [1, 2, 3],
            "global_identity_unique": True,
        }

    def test_shared_schema_is_exact_production_62_field_report(self):
        self.assertEqual(len(schema.VALIDATOR_REPORT_FIELDS), 62)
        self.assertEqual(len(set(schema.VALIDATOR_REPORT_FIELDS)), 62)
        self.assertIn("applicability_rows_by_class", schema.VALIDATOR_REPORT_FIELDS)
        self.assertIn("artifact_defect_rows", schema.VALIDATOR_REPORT_FIELDS)
        self.assertIn("profile", schema.VALIDATOR_REPORT_FIELDS)

    def test_schema_matches_actual_producer_without_changing_authority(self):
        source = Path(census.__file__).read_text()
        tree = ast.parse(source)
        function = next(node for node in tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == "validate_shards")
        result = next(node.value for node in ast.walk(function)
                      if isinstance(node, ast.Assign)
                      and any(isinstance(target, ast.Name) and target.id == "result"
                              for target in node.targets))
        self.assertEqual(tuple(ast.literal_eval(key) for key in result.keys),
                         schema.VALIDATOR_REPORT_FIELDS)
        self.assertEqual(census.APPLICABILITY_FIELDS, schema.APPLICABILITY_FIELDS)
        self.assertEqual(census.APPLICABILITY_SKIP_FIELDS, schema.APPLICABILITY_SKIP_FIELDS)

    def test_measured_direct_reference_control_is_not_a_skip(self):
        report = self.report()
        report["applicability_skip_rows"] = [2, 3]
        by_row, skipped = binding._validator_projection(report, 5)
        self.assertEqual(by_row[1], "retained-control")
        self.assertNotIn(1, skipped)
        row = {"compile_obligation": census.SUPPORTED_OBJECT_OBLIGATION}
        self.assertIsNone(census.expected_nonexecuted(row, "", ""))

    def test_actual_frozen_population_preserves_all_six_controls(self):
        root = Path(__file__).resolve().parents[1]
        data = (root / binding.SUPPORT_DECLARATION_PATH).read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), census.FULL_SUPPORT_CONTRACT_SHA256)
        with (root / binding.SUPPORT_DECLARATION_PATH).open() as stream:
            subjects = [row for row in csv.DictReader(stream, delimiter="\t")
                        if row["role"] == "subject"]
        with (root / "docs/native-retirement-applicability-v1.tsv").open() as stream:
            ledger = {(row["fixture"], row["target"]): row
                      for row in csv.DictReader(stream, delimiter="\t")}
        controls = set()
        skipped = set()
        rows_by_class = {name: [] for name in schema.APPLICABILITY_CLASSES}
        rows = []
        for subject in subjects:
            for target in binding.TARGETS:
                entry = ledger.get((subject["path"], target), {})
                classification = entry.get("applicability", "")
                reason = entry.get("reason", "")
                for frontend in binding.FRONTENDS:
                    for pic in binding.PIC:
                        for allocator in binding.ALLOCATORS:
                            index = len(rows)
                            nonexecuted = census.expected_nonexecuted(subject, classification, reason)
                            group = nonexecuted[0] if nonexecuted else "admitted-supported"
                            rows_by_class[group].append(index)
                            if nonexecuted:
                                skipped.add(index)
                            if subject["compile_obligation"] == census.NON_OBJECT_CONTROL_OBLIGATION:
                                controls.add(subject["path"])
                            rows.append({"row": index, "metrics": {
                                "compiler_wall_time": not bool(nonexecuted),
                                "generated_runtime": False}})
        self.assertEqual(len(controls), 6)
        self.assertEqual(len(rows_by_class["retained-control"]), 1152)
        self.assertEqual(len(rows), census.FULL_ROW_COUNT)
        counts = {key: len(value) for key, value in rows_by_class.items()}
        report = {"profile": "full-census", "applicability_classes": list(schema.APPLICABILITY_CLASSES),
                  "admission_classes": list(schema.APPLICABILITY_CLASSES),
                  "applicability_counts": counts, "admission_counts": counts,
                  "applicability_rows_by_class": rows_by_class,
                  "admission_rows_by_class": rows_by_class, "applicability_rows": len(rows),
                  "applicability_skip_rows": sorted(skipped), "global_identity_unique": True}
        by_row, actual_skips = binding._validator_projection(report, len(rows))
        self.assertEqual(actual_skips, skipped)
        selected = set()
        count = 0
        for event in binding._execution_schedule(rows, {"seed": 7, "warmups_per_variant": 2,
                                                       "rounds": 2, "pairs_per_round": 2}):
            selected.add(event["row"])
            count += 1
        self.assertEqual(selected, set(by_row) - skipped)
        self.assertEqual(count, len(selected) * 12)

    def test_projection_retains_complete_audit_and_sparse_measurement_set(self):
        by_row, skipped = binding._validator_projection(self.report(), 5)
        self.assertEqual(set(by_row), set(range(5)))
        self.assertEqual(skipped, {1, 2, 3})
        self.assertEqual({row for row in by_row if row not in skipped}, {0, 4})

    def test_candidate_cannot_supply_missing_duplicate_or_supported_skip(self):
        cases = []
        missing = self.report()
        missing["applicability_rows_by_class"]["admitted-supported"] = [0]
        missing["applicability_counts"]["admitted-supported"] = 1
        missing["admission_rows_by_class"]["admitted-supported"] = [0]
        missing["admission_counts"]["admitted-supported"] = 1
        cases.append(missing)
        duplicate = self.report()
        duplicate["applicability_rows_by_class"]["retained-control"] = [1, 1]
        duplicate["applicability_counts"]["retained-control"] = 2
        duplicate["admission_rows_by_class"]["retained-control"] = [1, 1]
        duplicate["admission_counts"]["retained-control"] = 2
        cases.append(duplicate)
        supported_skip = self.report()
        supported_skip["applicability_skip_rows"] = [0, 1, 2, 3]
        cases.append(supported_skip)
        for candidate in cases:
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                binding._validator_projection(candidate, 5)

    def test_row_parser_accepts_authenticated_untimed_control(self):
        base_identity = {
            "fixture": "tests/control.c", "target": "x86_64-unknown-linux-gnu",
            "target_abi": "systemv-x86_64", "cpu": "baseline",
            "cpu_features": "baseline", "allocator": "none",
            "frontend_lowering": "direct-ssa", "PIC": "0",
            "fixture_recipe": "compiler-default",
            "compile_obligation": "registered-non-object-control",
            "link_obligation": "semantic-gate-509",
            "execution_obligation": "semantic-gate-509",
            "diagnostic_obligation": "none", "argv_evidence": "groups/0/none.argv",
            "artifact_stage": "object",
        }
        rows = []
        for row_id, stage in enumerate(("object", "link")):
            identity = dict(base_identity)
            identity["fixture"] = f"tests/eligible-{row_id}.c"
            identity["compile_obligation"] = "supported-object-zero-fallback"
            identity["artifact_stage"] = stage
            eligibility = {
                "compiler_wall_time": True, "compiler_peak_rss": True,
                "generated_code_bytes": True,
                "generated_runtime": stage == "link",
                "runtime_oracle": ("independent-native-executable-oracle"
                                   if stage == "link" else "not-applicable"),
                "code_section": "deterministic-code-section",
            }
            rows.append({"row": row_id, "identity": identity,
                         "eligibility": eligibility})
        rows.append({
            "row": 2,
            "identity": base_identity,
            "eligibility": {
                "compiler_wall_time": False, "compiler_peak_rss": False,
                "generated_code_bytes": False, "generated_runtime": False,
                "runtime_oracle": "not-applicable", "code_section": "not-applicable",
            },
        })
        zero_identity = dict(base_identity)
        zero_identity["fixture"] = "tests/empty-code.c"
        zero_identity["compile_obligation"] = "supported-object-zero-fallback"
        rows.append({
            "row": 3,
            "identity": zero_identity,
            "eligibility": {
                "compiler_wall_time": True, "compiler_peak_rss": True,
                "generated_code_bytes": False, "generated_runtime": False,
                "runtime_oracle": "not-applicable",
                "code_section": "deterministic-zero-baseline-code-section",
            },
        })
        value = {
            "schema": binding.ROW_SCHEMA, "version": binding.ROW_VERSION,
            "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
            "sources": {name: "a" * 64 for name in binding.ROW_SOURCE_ROLES},
            "rows": rows,
        }
        data = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
        parsed, _axes, family, _sources = binding._performance_rows_with_sources(data)
        self.assertFalse(parsed[2]["metrics"]["compiler_wall_time"])
        self.assertNotIn("compiler_wall_time/cell/row=2", family["members"])
        self.assertFalse(parsed[3]["metrics"]["generated_code_bytes"])
        self.assertEqual(parsed[3]["eligibility"]["code_section"],
                         "deterministic-zero-baseline-code-section")

    def test_execution_schedule_excludes_authenticated_untimed_rows(self):
        rows = [
            {"row": 0, "metrics": {"compiler_wall_time": True,
                                    "generated_runtime": False}},
            {"row": 1, "metrics": {"compiler_wall_time": False,
                                    "generated_runtime": False}},
            {"row": 2, "metrics": {"compiler_wall_time": True,
                                    "generated_runtime": True}},
        ]
        sampling = {"seed": 7, "warmups_per_variant": 0,
                    "rounds": 1, "pairs_per_round": 2}
        schedule = list(binding._execution_schedule(rows, sampling))
        compiler_rows = {item["row"] for item in schedule
                         if item["kind"] == "compiler"}
        runtime_rows = {item["row"] for item in schedule
                        if item["kind"] == "runtime"}
        self.assertEqual(compiler_rows, {0, 2})
        self.assertEqual(runtime_rows, {2})
        self.assertNotIn(1, {item["row"] for item in schedule})

    def test_zero_candidate_code_is_retained_but_zero_baseline_has_no_ratio(self):
        row = {"row": 0, "metrics": {
            "compiler_wall_time": False, "compiler_peak_rss": False,
            "generated_code_bytes": True, "generated_runtime": False}}
        value = {
            "record_id": "row-0/round-0/pair-0", "row": 0,
            "round": 0, "pair": 0,
            "measurements": {
                "generated_code_bytes": {"baseline": 1, "candidate": 0},
            },
        }
        seen = [0]
        binding._consume_result_record(
            value, {0: 0}, {0: row}, 1, 1, 0, seen, hashlib.sha256())
        self.assertEqual(seen, [1])
        value["measurements"]["generated_code_bytes"]["baseline"] = 0
        with self.assertRaises(ValueError):
            binding._consume_result_record(
                value, {0: 0}, {0: row}, 1, 1, 0, [0], hashlib.sha256())

    def test_zero_code_payload_is_exact_not_padded(self):
        empty = hashlib.sha256(b"").hexdigest()
        self.assertEqual(binding._bounded_code_bytes(0, "empty"), 0)
        self.assertEqual(len(empty), 64)
        with self.assertRaises(ValueError):
            binding._bounded_code_bytes(0, "baseline", positive=True)


if __name__ == "__main__":
    unittest.main()
