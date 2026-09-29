#!/usr/bin/env python3
"""Regression tests for #929's trusted sparse eligibility projection."""

import ast
import copy
import csv
from pathlib import Path

import native_retirement_contract as census
import hashlib
import json
import os
import unittest
from unittest import mock
import tempfile

import native_retirement_performance_binding as binding
import native_retirement_performance_schema as schema


def planned_row(row, source, compiler_eligible, stage="object", runtime=False):
    """A schedule-shaped row carrying the identity fields A1 groups by."""
    identity = {field: source[field] for field in
                ("fixture", "target", "cpu", "cpu_features", "allocator",
                 "frontend_lowering", "PIC", "fixture_recipe")}
    identity["artifact_stage"] = stage
    return {"row": row, "identity": identity,
            "metrics": {"compiler_wall_time": compiler_eligible,
                        "generated_runtime": runtime}}


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

    def test_actual_validator_report_and_evidence_use_the_production_schema(self):
        from native_retirement_contract_test import ContractTests, read_table
        fixture = ContractTests()
        fixture.setUp()
        try:
            report = fixture.validate()
            binding._keys(report, schema.VALIDATOR_REPORT_FIELDS, "production report")
            _, rows = read_table(fixture.shards[0] / "rows.tsv")
            by_row = {row: name for name, ids in report["applicability_rows_by_class"].items()
                      for row in ids}
            evidence = binding._check_validator_projection_evidence(
                fixture.root, report, rows, by_row, set(report["applicability_skip_rows"]))
            self.assertEqual(len(evidence["artifacts"]), 3)
            self.assertEqual(len(by_row), len(rows))
            self.assertTrue(report["applicability_rows_by_class"]["retained-control"])
            self.assertEqual(report["applicability_skip_rows"], [])
            for artifact in evidence["artifacts"]:
                binding._check_evidence(fixture.root, artifact, "census projection")
        finally:
            fixture.tearDown()

    def test_actual_validator_report_replays_absolute_and_relative_directories(self):
        from native_retirement_contract_test import ContractTests, read_table, write_table
        fixture = ContractTests()
        fixture.setUp()
        try:
            report = fixture.validate()
            _, rows = read_table(fixture.shards[0] / "rows.tsv")
            by_row = {row: name for name, ids in report["applicability_rows_by_class"].items()
                      for row in ids}
            evidence = binding._check_validator_projection_evidence(
                fixture.root, report, rows, by_row, set(report["applicability_skip_rows"]))
            # No patched validator, population, subprocess, or success receipt.
            binding._replay_validator_report(fixture.root, report, evidence)
            relative = copy.deepcopy(report)
            relative["directories"] = [Path(path).relative_to(fixture.root).as_posix()
                                       for path in report["directories"]]
            binding._replay_validator_report(fixture.root, relative, evidence)
            changed = copy.deepcopy(report)
            changed["compiler_sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "replay differs in compiler_sha256"):
                binding._replay_validator_report(fixture.root, changed, evidence)
            changed_evidence = dict(evidence, skip_bytes=evidence["skip_bytes"] + b"\n")
            with self.assertRaisesRegex(ValueError, "replay differs in applicability_skip_evidence"):
                binding._replay_validator_report(fixture.root, report, changed_evidence)
            fields, changed_rows = read_table(fixture.shards[0] / "rows.tsv")
            changed_rows[0]["cpu"] = "unapproved-profile"
            write_table(fixture.shards[0] / "rows.tsv", fields, changed_rows)
            with self.assertRaisesRegex(ValueError, "replay status differs"):
                binding._replay_validator_report(fixture.root, report, evidence)
        finally:
            fixture.tearDown()

    def test_actual_reference_supplements_are_replayed_and_authenticated(self):
        from native_retirement_contract_test import ContractTests, read_table
        fixture = ContractTests()
        fixture.setUp()
        try:
            fixture.make_reference_supplements()
            report = census.validate_shards(fixture.shards, fixture.root / "report.json",
                                             reference_supplements=True)
            _, rows = read_table(fixture.shards[0] / "rows.tsv")
            by_row = {row: name for name, ids in report["applicability_rows_by_class"].items()
                      for row in ids}
            evidence = binding._check_validator_projection_evidence(
                fixture.root, report, rows, by_row, set(report["applicability_skip_rows"]))
            self.assertTrue(report["direct_reference_failure_rows"])
            self.assertFalse(report["reference_failure_rows"])
            binding._replay_validator_report(fixture.root, report, evidence)
            changed = copy.deepcopy(report)
            changed["reference_supplement_sha256"][0] = "0" * 64
            with self.assertRaisesRegex(ValueError, "replay differs in reference_supplement_sha256"):
                binding._replay_validator_report(fixture.root, changed, evidence)
            manifest = fixture.shards[0] / "reference-supplement/manifest.json"
            manifest.unlink()
            with self.assertRaisesRegex(ValueError, "replay status differs"):
                binding._replay_validator_report(fixture.root, report, evidence)
        finally:
            fixture.tearDown()

    def test_nonobject_skip_preserves_distinct_platform_applicability(self):
        from native_retirement_contract_test import write_table
        source = {"row": "0", "group": "0", "fixture": "tests/basic_c_macro_options.c",
                  "target": "x86_64-apple-ios", "allocator": "none", "cpu": "baseline",
                  "frontend_lowering": "direct-ssa", "PIC": "0",
                  "compile_obligation": census.NON_OBJECT_CONTROL_OBLIGATION,
                  "execution_obligation": "unavailable-platform-control"}
        classification, admission, reason, _owner = census.classify_applicability(source, {}, {})
        skip_class, skip_reason = census.expected_nonexecuted(source, "", "")
        self.assertNotEqual(classification, skip_class)
        app = {"row": "0", "group": "0", "fixture": source["fixture"],
               "target": source["target"], "cpu": source["cpu"], "frontend": "direct-ssa",
               "allocator": "none", "PIC": "0", "applicability": classification,
               "admission": admission, "reason": reason, "ownership": _owner,
               "candidate_failure": "0", "reference_failure": "0", "acceptance_failure": "0"}
        skip = {key: source[key] for key in ("row", "group", "fixture", "target", "allocator")}
        skip.update(applicability=skip_class, reason=skip_reason)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_table(root / "app.tsv", schema.APPLICABILITY_FIELDS, [app])
            write_table(root / "skip.tsv", schema.APPLICABILITY_SKIP_FIELDS, [skip])
            (root / "residual.tsv").write_text("residual test evidence\n")
            report = {"applicability_evidence": "app.tsv", "applicability_tsv": "app.tsv",
                      "applicability_skip_evidence": "skip.tsv", "residual_evidence": "residual.tsv",
                      "residual_tsv": "residual.tsv", "candidate_failure_rows": [],
                      "reference_failure_rows": [], "acceptance_failure_rows": [],
                      "applicability_sha256": hashlib.sha256((root / "app.tsv").read_bytes()).hexdigest(),
                      "residual_sha256": hashlib.sha256((root / "residual.tsv").read_bytes()).hexdigest()}
            binding._check_validator_projection_evidence(root, report, [source], {0: classification}, {0})
            skip["reason"] = reason
            write_table(root / "skip.tsv", schema.APPLICABILITY_SKIP_FIELDS, [skip])
            with self.assertRaisesRegex(ValueError, "not row-bound"):
                binding._check_validator_projection_evidence(root, report, [source], {0: classification}, {0})

    @unittest.skipUnless(os.environ.get("BUSTER_RETIREMENT_CENSUS_ROOT"),
                         "set BUSTER_RETIREMENT_CENSUS_ROOT for the downloaded full census")
    def test_downloaded_full_census_replays_and_schedules_every_eligible_row(self):
        root = Path(os.environ["BUSTER_RETIREMENT_CENSUS_ROOT"]).resolve()
        recorded_root = Path(os.environ["BUSTER_RETIREMENT_RECORDED_ROOT"])
        report = json.loads((root / "evidence/census-validation-v2.json").read_text())
        # Relocate paths only. Retain all report facts and every evidence byte.
        def relocate(value):
            path = Path(value)
            return str(root / path.relative_to(recorded_root)) if path.is_absolute() else value
        for key in schema.PATH_FIELDS:
            report[key] = ([relocate(value) for value in report[key]]
                           if isinstance(report[key], list) else relocate(report[key]))
        binding._keys(report, schema.VALIDATOR_REPORT_FIELDS, "downloaded report")
        self.assertEqual(report["support_contract_sha256"], census.FULL_SUPPORT_CONTRACT_SHA256)
        self.assertEqual(report["rows_validated"], census.FULL_ROW_COUNT)
        self.assertTrue(report["require_clean_candidate"] and report["clean_candidate"])
        self.assertTrue(report["require_clean_acceptance"] and report["clean_acceptance"])
        with (Path(report["directories"][0]) / "rows.tsv").open() as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        self.assertEqual(len(rows), census.FULL_ROW_COUNT)
        by_row, skipped = binding._validator_projection(report, len(rows))
        evidence = binding._check_validator_projection_evidence(root, report, rows, by_row, skipped)
        binding._replay_validator_report(root, report, evidence)
        cpus = sorted({row["cpu"] for row in rows})
        binding._check_census_cpu_axes(rows, {"cpus": cpus})
        controls = {row["fixture"] for row in rows
                    if row["compile_obligation"] == census.NON_OBJECT_CONTROL_OBLIGATION}
        self.assertEqual(len(controls), 6)
        self.assertEqual(sum(row["fixture"] in controls for row in rows), 1152)
        self.assertTrue(all(int(row["row"]) in skipped for row in rows if row["fixture"] in controls))
        planned = [planned_row(int(row["row"]), row, int(row["row"]) not in skipped)
                   for row in rows]
        sampling = {"seed": 7, "warmups_per_variant": 2, "rounds": 2, "pairs_per_round": 60}
        # (A1) The timed projection is derived from the replayed census: the
        # compiler-eligible rows on the pinned native host.  Its batch groups
        # partition it and are the compiler campaign's scheduled cells.
        timed = {row["row"] for row in binding._timed_rows(planned)}
        self.assertEqual(timed, {int(row["row"]) for row in rows
                                 if int(row["row"]) not in skipped
                                 and row["target"] == binding.NATIVE_TIMED_TARGET})
        groups = binding._batch_groups(planned)
        members = [row for group in groups for row in group["rows"]]
        self.assertEqual(sorted(members), sorted(timed))
        self.assertEqual(len(members), len(set(members)))
        for group in groups:
            self.assertEqual(len({tuple(rows[row][field] for field in binding.BATCH_GROUP_KEY_FIELDS)
                                  for row in group["rows"]}), 1)
        counts = [0] * len(groups)
        for event in binding._execution_schedule(planned, sampling):
            self.assertEqual(event["kind"], "compiler")
            counts[event["group"]] += 1
        self.assertEqual(counts, [244] * len(groups))
        print(json.dumps({"proof": "full-census-compiler-schedule-and-independent-replay",
                          "rows": len(rows), "untimed_rows": len(skipped),
                          "compiler_eligible_rows": len(rows) - len(skipped),
                          "native_timed_rows": len(timed), "batch_groups": len(groups),
                          "scheduled_batch_invocations": sum(counts), "cpu_profiles": cpus,
                          "performance_measurements_executed": False}, sort_keys=True))

    def test_report_directories_are_existing_contained_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "shard").mkdir()
            (root / "file").write_text("not a directory")
            for path in ("missing", "file", "../outside", str(root.parent)):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    binding._report_evidence_path(root, path, "shard", directory=True)
            try:
                (root / "alias").symlink_to(root / "shard", target_is_directory=True)
            except OSError:
                pass  # Windows may not grant symlink creation to this test process.
            else:
                with self.assertRaisesRegex(ValueError, "symbolic link"):
                    binding._report_evidence_path(root, "alias", "shard", directory=True)

    def test_frozen_population_cpu_axes_include_target_scoped_recipes(self):
        root = Path(__file__).resolve().parents[1]
        with (root / binding.SUPPORT_DECLARATION_PATH).open() as stream:
            subjects = [row for row in csv.DictReader(stream, delimiter="\t")
                        if row["role"] == "subject"]
        rows = [{"cpu": census.expected_cpu(subject["path"], target, "baseline")}
                for subject in subjects for target in binding.TARGETS
                for frontend in binding.FRONTENDS for pic in binding.PIC
                for allocator in binding.ALLOCATORS]
        self.assertEqual(len(rows), census.FULL_ROW_COUNT)
        cpus = sorted({row["cpu"] for row in rows})
        self.assertEqual(cpus, ["baseline", "haswell", "skylake-avx512"])
        binding._check_census_cpu_axes(rows, {"cpus": cpus})
        for altered in (["baseline"], cpus + ["znver5"]):
            with self.subTest(cpus=altered), self.assertRaisesRegex(ValueError, "differ from the census"):
                binding._check_census_cpu_axes(rows, {"cpus": altered})

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
        self.assertIn(hashlib.sha256(data).hexdigest(),
                      (census.FULL_SUPPORT_CONTRACT_SHA256,
                       census.NEXT_SUPPORT_CONTRACT_SHA256))
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
                            recipe = census.expected_fixture_recipe(subject["path"])[0]
                            cpu = census.expected_cpu(subject["path"], target, "baseline")
                            rows.append(planned_row(index, {
                                "fixture": subject["path"], "target": target, "cpu": cpu,
                                "cpu_features": cpu, "allocator": allocator,
                                "frontend_lowering": frontend, "PIC": pic,
                                "fixture_recipe": recipe}, not bool(nonexecuted)))
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
        # (A1) Only the native host is timed; its eligible rows form the
        # declared five recipe groups in each of the 16 configurations.
        native = {row["row"] for row in rows
                  if row["identity"]["target"] == binding.NATIVE_TIMED_TARGET}
        groups = binding._batch_groups(rows)
        self.assertEqual({row for group in groups for row in group["rows"]},
                         (set(by_row) - skipped) & native)
        self.assertEqual(len(groups), 16 * 5)
        self.assertEqual({group["identity"]["fixture_recipe"] for group in groups},
                         {"compiler-default", "c23", "c23-dialect-assertions",
                          "x86-avx512", "x86-cx16"})
        self.assertTrue(all(group["kind"] == binding.OBJECT_BATCH_GROUP for group in groups))
        selected = set()
        count = 0
        for event in binding._execution_schedule(rows, {"seed": 7, "warmups_per_variant": 2,
                                                       "rounds": 2, "pairs_per_round": 2}):
            selected.add(event["group"])
            count += 1
        self.assertEqual(selected, set(range(len(groups))))
        self.assertEqual(count, len(groups) * 12)

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
        source = {"fixture": "tests/a.c", "target": binding.NATIVE_TIMED_TARGET,
                  "cpu": "baseline", "cpu_features": "baseline", "allocator": "none",
                  "frontend_lowering": "direct-ssa", "PIC": "0",
                  "fixture_recipe": "compiler-default"}
        cross = dict(source, target="aarch64-unknown-linux-gnu")
        rows = [
            planned_row(0, source, True),
            planned_row(1, dict(source, fixture="tests/b.c"), False),
            planned_row(2, source, True, stage="link", runtime=True),
            planned_row(3, dict(cross, fixture="tests/c.c"), True),
        ]
        sampling = {"seed": 7, "warmups_per_variant": 0,
                    "rounds": 1, "pairs_per_round": 2}
        schedule = list(binding._execution_schedule(rows, sampling))
        groups = binding._batch_groups(rows)
        self.assertEqual([group["rows"] for group in groups], [[0], [2]])
        compiler_groups = {item["group"] for item in schedule
                           if item["kind"] == "compiler"}
        runtime_rows = {item["row"] for item in schedule
                        if item["kind"] == "runtime"}
        self.assertEqual(compiler_groups, {0, 1})
        self.assertEqual(runtime_rows, {2})
        self.assertFalse({1, 3} & {row for group in groups for row in group["rows"]})
        self.assertEqual(len(schedule), (2 + 1) * 2 * 2)

    def test_zero_candidate_code_is_retained_but_zero_baseline_has_no_ratio(self):
        # (A1) Code bytes come from the once-measured per-row code records.
        rows = [{"row": 0, "metrics": {"generated_code_bytes": True}},
                {"row": 1, "metrics": {"generated_code_bytes": False}}]
        summary = binding._code_bytes_summary(rows, {0: (1, 0), 1: (0, 0)})
        self.assertEqual((summary["rows"], summary["aggregate_ratio"]), (1, 0.0))
        self.assertTrue(summary["aggregate_pass"] and summary["per_cell_pass"])
        with self.assertRaises(ValueError):
            binding._code_bytes_summary(rows, {0: (0, 0)})

    def test_zero_code_payload_is_exact_not_padded(self):
        empty = hashlib.sha256(b"").hexdigest()
        self.assertEqual(binding._bounded_code_bytes(0, "empty"), 0)
        self.assertEqual(len(empty), 64)
        with self.assertRaises(ValueError):
            binding._bounded_code_bytes(0, "baseline", positive=True)


def disposition_row(index, source, stage, eligible):
    """A canonical performance row for ``source`` with the derived eligibility."""
    identity = {field: source[field] for field in binding.ROW_IDENTITY_FIELDS
                if field != "artifact_stage"}
    identity["artifact_stage"] = stage
    runtime = eligible and binding._native_runtime_required(
        {"identity": identity}, binding.NATIVE_TIMED_TARGET)
    return {"row": index, "identity": identity,
            "metrics": {"compiler_wall_time": eligible, "compiler_peak_memory": eligible,
                        "generated_code_bytes": eligible, "generated_runtime": runtime},
            "eligibility": {
                "compiler_wall_time": eligible, "compiler_peak_rss": eligible,
                "generated_code_bytes": eligible, "generated_runtime": runtime,
                "runtime_oracle": ("independent-native-executable-oracle" if runtime
                                   else "not-applicable"),
                "code_section": ("deterministic-code-section" if eligible
                                 else "not-applicable")}}


def disposition_inputs(census_rows):
    expected_object = {tuple(row[field] for field in binding.ROW_IDENTITY_FIELDS
                             if field != "artifact_stage"): row for row in census_rows}
    by_row = {index: "admitted-supported" for index in range(len(census_rows))}
    reasons = {index: "supported-object-zero-fallback" for index in range(len(census_rows))}
    return expected_object, by_row, reasons


# The bytes tools/native_retirement_census.c writes for an unfiltered run:
# "fixture_filter={S8}\ntarget_filter={S8}\nshard_index={u32}..." with both
# filters empty.
UNFILTERED_CENSUS_MANIFEST_LINES = b"\nfixture_filter=\ntarget_filter=\nshard_index="


def canonical_json(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
            + "\n").encode("utf-8")


class CensusManifestPropertiesTests(unittest.TestCase):
    """#1891: the census manifest's empty filters parse; no other value may be empty."""

    def manifest(self):
        from native_retirement_contract_test import ContractTests
        fixture = ContractTests()
        fixture.setUp()
        try:
            return (fixture.shards[0] / "manifest.txt").read_bytes()
        finally:
            fixture.tearDown()

    def test_manifest_filters_match_the_census_writer(self):
        source = (Path(__file__).resolve().parent / "native_retirement_census.c").read_text()
        self.assertIn('"fixture_filter={S8}\\ntarget_filter={S8}\\nshard_index=', source)
        self.assertEqual(binding.MANIFEST_OPTIONAL_EMPTY_KEYS, ("fixture_filter", "target_filter"))

    def test_unfiltered_census_manifest_parses(self):
        data = self.manifest()
        self.assertIn(UNFILTERED_CENSUS_MANIFEST_LINES, data)
        properties = binding._properties(data, "manifest")
        self.assertEqual((properties["fixture_filter"], properties["target_filter"]), ("", ""))

    def test_empty_value_for_any_other_key_is_rejected(self):
        lines = self.manifest().decode().splitlines()
        keys = [line.split("=", 1)[0] for line in lines]
        self.assertGreater(len(keys), 20)
        for index, key in enumerate(keys):
            if key in binding.MANIFEST_OPTIONAL_EMPTY_KEYS:
                continue
            changed = list(lines)
            changed[index] = key + "="
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "non-empty string"):
                binding._properties(("\n".join(changed) + "\n").encode(), "manifest")
        # A present filter value still has to be a single line.
        for key in binding.MANIFEST_OPTIONAL_EMPTY_KEYS:
            with self.subTest(key=key), self.assertRaises(ValueError):
                binding._properties(f"version=2\n{key}=a\x00b\n".encode(), "manifest")


class SupplementDispositionTests(unittest.TestCase):
    """Option 3 of the census disposition (decision recorded in #36).

    The fixture's two failed direct references (rows 0 and 4) are resolved by
    genuine supplements; ``self.pin`` is this fixture's explicit test-only pin.
    """

    def setUp(self):
        from native_retirement_contract_test import ContractTests, read_table
        self.fixture = ContractTests()
        self.fixture.setUp()
        # An unfiltered census writes both filters empty, exactly as
        # native_retirement_census.c's manifest writer does (#1891).
        for shard in self.fixture.shards:
            self.assertIn(UNFILTERED_CENSUS_MANIFEST_LINES, (shard / "manifest.txt").read_bytes())
        # Authenticated skips (census rows 128..143) beside the resolved rows.
        self.fixture.install_applicability({("tests/unit.c", "x86_64-apple-ios",
                                             "platform-inapplicable", "source-registration-test")})
        self.resolved = self.fixture.make_failed_direct_reference_supplements()
        self.report_path = self.fixture.root / "report.json"
        self.report = census.validate_shards(self.fixture.shards, self.report_path,
                                             True, reference_supplements=True)
        _fields, self.rows = read_table(self.fixture.shards[0] / "rows.tsv")
        self.skips = set(self.report["applicability_skip_rows"])
        self.pin = (len(self.resolved), binding._supplement_identity_digest(self.rows, self.resolved))

    def tearDown(self):
        self.fixture.tearDown()

    def rule(self, report, approved=None):
        return binding._supplement_resolved_rows(report, self.rows, self.skips,
                                                 self.pin if approved is None else approved)

    def supplements(self, report):
        return binding._check_reference_supplements(self.fixture.root, report, self.rows,
                                                    self.resolved)

    def manifest_path(self, shard):
        return self.fixture.shards[shard] / "reference-supplement/manifest.json"

    def rehash_supplement(self, shard, mutation, report=None):
        """Rewrite one shard's supplement manifest and rebind its report digest."""
        path = self.manifest_path(shard)
        manifest = json.loads(path.read_text())
        mutation(manifest)
        path.write_text(json.dumps(manifest))
        report = copy.deepcopy(self.report if report is None else report)
        report["reference_supplement_sha256"][shard] = hashlib.sha256(path.read_bytes()).hexdigest()
        return report

    # --- The end-to-end support check -------------------------------------

    def support_binding(self, population=None):
        """A binding whose support set is this genuine supplement census."""
        root = self.fixture.root
        shard = self.fixture.shards[0]
        declaration = root / binding.SUPPORT_DECLARATION_PATH
        declaration.parent.mkdir(parents=True, exist_ok=True)
        declaration.write_bytes((shard / "support-contract.tsv").read_bytes())
        performance = root / "performance"
        performance.mkdir(exist_ok=True)

        def descriptor(path):
            data = path.read_bytes()
            return {"path": path.relative_to(root).as_posix(), "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()}

        files = {"support_declaration": descriptor(declaration),
                 "manifest": descriptor(shard / "manifest.txt"),
                 "inputs": descriptor(shard / "inputs.tsv"),
                 "rows": descriptor(shard / "rows.tsv"),
                 "dependencies": descriptor(shard / "dependencies.tsv"),
                 "environment": descriptor(shard / "environment.tsv"),
                 "validator_report": descriptor(self.report_path)}
        resolved = set(self.resolved)
        rows = [disposition_row(index, source, "object",
                                index not in self.skips and index not in resolved)
                for index, source in enumerate(self.rows)]
        for stage, census_row in (("link", 1), ("self-host-stage1", 2)):
            rows.append(disposition_row(len(rows), self.rows[census_row], stage, True))
        for row in rows:
            del row["metrics"]
        if population is not None:
            population(rows)
        record = {"schema": binding.ROW_SCHEMA, "version": binding.ROW_VERSION,
                  "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
                  "sources": {role: files[role]["sha256"] for role in binding.ROW_SOURCE_ROLES},
                  "rows": rows}
        rows_path = performance / "rows.json"
        rows_path.write_bytes(canonical_json(record))
        files["performance_rows"] = descriptor(rows_path)
        row_data = binding._performance_rows_with_sources(rows_path.read_bytes())
        parsed, axes, family, _sources = row_data
        declaration_record = {
            "schema": binding.PERFORMANCE_DECLARATION_SCHEMA,
            "version": binding.PERFORMANCE_DECLARATION_VERSION,
            **{f"{role}_sha256": files[role]["sha256"] for role in (
                "support_declaration", "manifest", "inputs", "rows", "dependencies",
                "environment", "validator_report", "performance_rows")},
            "object_row_count": len(self.rows), "required_row_count": len(parsed),
            "axes": axes, "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
            "statistical_family": family,
            "runtime_eligibility": "independent-native-executable-oracle-only",
            "code_section_eligibility": "deterministic-code-section-payload-only"}
        declaration_path = performance / "declaration.json"
        declaration_path.write_bytes(canonical_json(declaration_record))
        files["performance_declaration"] = descriptor(declaration_path)
        manifest = dict(line.split("=", 1) for line in
                        (shard / "manifest.txt").read_text().splitlines())
        subject = {"binary": {"bytes": int(manifest["compiler_bytes"]),
                              "sha256": manifest["compiler_sha256"]}}
        record = {"support": {"files": [dict(files[role], name=role)
                                        for role in binding.SUPPORT_FILE_ROLES]},
                  "subjects": {"candidate": subject, "baseline": subject}}
        return record, row_data

    def check_support(self, record, row_data, approved="pin"):
        """Run the unmodified _check_support_output over this fixture census.

        Only identities are substituted: the fixture's support declaration
        digest, its self-test census profile and (option 3) its explicit
        test-only pin of the approved set.
        """
        declaration = record["support"]["files"][
            binding.SUPPORT_FILE_ROLES.index("support_declaration")]["sha256"]
        approved_sets = {"self-test": self.pin} if approved == "pin" else approved
        with mock.patch.object(binding, "SUPPORT_DECLARATION_SHA256", declaration), \
                mock.patch.object(binding, "CENSUS_PROFILE", "self-test"), \
                mock.patch.object(binding, "APPROVED_SUPPLEMENT_SETS", approved_sets):
            return binding._check_support_output(self.fixture.root, record, row_data,
                                                 binding.NATIVE_TIMED_TARGET)

    def test_support_output_accepts_the_pinned_supplement_set_end_to_end(self):
        record, row_data = self.support_binding()
        output = self.check_support(record, row_data)
        self.assertEqual(output["supplement_resolved_rows"], self.resolved)
        self.assertEqual(self.skips, set(range(128, 144)))
        for row in self.skips:
            self.assertEqual(output["performance_applicability"][row]["compiler_ineligible_reason"],
                             binding.NONEXECUTED_INELIGIBLE_REASON)
        self.assertEqual(sorted(set(range(len(self.rows) + 2)) - output["compiler_eligible_rows"]),
                         sorted(self.skips | set(self.resolved)))
        for shard, row in enumerate(self.resolved):
            applicability = output["performance_applicability"][row]
            self.assertFalse(applicability["compiler_eligible"])
            self.assertEqual(applicability["compiler_ineligible_reason"],
                             binding.SUPPLEMENT_INELIGIBLE_REASON)
            self.assertEqual(applicability["supplement"]["supplement_manifest_sha256"],
                             self.report["reference_supplement_sha256"][shard])
            self.assertEqual(applicability["reason"],
                             "direct-reference-unresolved-clang-control-passed")
        # MIR rows of the resolved groups keep every obligation.
        self.assertTrue({1, 2, 3, 5, 6, 7} <= output["compiler_eligible_rows"])
        self.assertEqual(output["eligible_object_row_count"],
                         len(self.rows) - len(self.skips) - len(self.resolved))

    def test_support_output_refuses_a_filtered_census_manifest(self):
        manifest = self.fixture.shards[0] / "manifest.txt"
        manifest.write_bytes(manifest.read_bytes().replace(
            UNFILTERED_CENSUS_MANIFEST_LINES,
            b"\nfixture_filter=tests/unit.c\ntarget_filter=\nshard_index="))
        record, row_data = self.support_binding()
        with self.assertRaisesRegex(ValueError, "manifest is filtered"):
            self.check_support(record, row_data)

    def test_support_output_refuses_an_unpinned_or_different_set(self):
        record, row_data = self.support_binding()
        with self.assertRaisesRegex(ValueError, "no approved supplement-resolved set is pinned"):
            self.check_support(record, row_data, approved={})
        with self.assertRaisesRegex(ValueError, "not exactly the approved set"):
            self.check_support(record, row_data, approved={"self-test": (
                1, binding._supplement_identity_digest(self.rows, [0]))})

    def test_support_output_refuses_a_resolved_row_kept_timed(self):
        def keep_timed(rows):
            eligibility = rows[0]["eligibility"]
            eligibility.update(compiler_wall_time=True, compiler_peak_rss=True,
                               generated_code_bytes=True, code_section="deterministic-code-section")
        record, row_data = self.support_binding(keep_timed)
        with self.assertRaisesRegex(ValueError, "supplement provenance"):
            self.check_support(record, row_data)

    def test_support_output_refuses_a_stage_row_on_a_resolved_identity(self):
        def stage_identity(rows):
            row = disposition_row(len(rows), self.rows[4], "link", False)
            del row["metrics"]
            rows.append(row)
        record, row_data = self.support_binding(stage_identity)
        with self.assertRaisesRegex(ValueError, "stage row carries a supplement-resolved"):
            self.check_support(record, row_data)

    def test_support_output_refuses_a_rehashed_supplement_missing_one_record(self):
        def drop(manifest):
            manifest["results"].pop()
        self.report = self.rehash_supplement(1, drop)
        self.report_path.write_text(json.dumps(self.report, indent=2, sort_keys=True) + "\n")
        record, row_data = self.support_binding()
        # The census replay rejects the forged report before the binding's own
        # inventory check, which the next test isolates.
        with self.assertRaisesRegex(ValueError, "replay"):
            self.check_support(record, row_data)

    # --- The support-check rule (b) and the pin ----------------------------

    def test_pinned_supplement_set_is_accepted(self):
        self.assertEqual(self.resolved, [0, 4])
        self.assertEqual(self.rule(self.report), self.resolved)

    def test_set_outside_the_pin_is_refused(self):
        for approved in ((1, binding._supplement_identity_digest(self.rows, [0])),
                         (2, binding._supplement_identity_digest(self.rows, [0, 8])),
                         (3, binding._supplement_identity_digest(self.rows, [0, 4, 8]))):
            with self.subTest(approved=approved), \
                    self.assertRaisesRegex(ValueError, "not exactly the approved set"):
                self.rule(self.report, approved)
        with self.assertRaisesRegex(ValueError, "no approved supplement-resolved set is pinned"):
            binding._supplement_resolved_rows(self.report, self.rows, self.skips)
        full = dict(self.report, profile="full-census")
        with self.assertRaisesRegex(ValueError, "not exactly the approved set"):
            binding._supplement_resolved_rows(full, self.rows, self.skips)

    def test_every_defect_outside_the_supplement_set_stays_fatal(self):
        cases = (
            ("telemetry_defect_rows", lambda rows: rows + [8], "not exactly the supplement"),
            ("artifact_defect_rows", lambda rows: rows[:-1], "not exactly the supplement"),
            ("execution_defect_rows", lambda rows: sorted(rows + [1]), "not exactly the supplement"),
            ("telemetry_defect_rows", lambda rows: sorted(rows + [5]), "not exactly the supplement"),
            ("execution_defect_rows", lambda rows: [], "not exactly the supplement"),
            ("fallback_defect_rows", lambda rows: [0], "contains fallback_defect_rows"),
            ("candidate_failure_rows", lambda rows: [1], "contains candidate_failure_rows"),
            ("reference_failure_rows", lambda rows: [0], "contains reference_failure_rows"),
            ("unexpected_failure_rows", lambda rows: [1], "contains unexpected_failure_rows"),
        )
        for field, change, message in cases:
            report = copy.deepcopy(self.report)
            report[field] = change(report[field])
            with self.subTest(field=field, value=report[field]), \
                    self.assertRaisesRegex(ValueError, message):
                self.rule(report)

    def test_direct_reference_set_is_group_closed(self):
        orphan = copy.deepcopy(self.report)
        orphan["direct_reference_failure_rows"] = sorted(orphan["direct_reference_failure_rows"] + [9])
        with self.assertRaisesRegex(ValueError, "has no failed allocator-none reference"):
            self.rule(orphan)
        partial = copy.deepcopy(self.report)
        partial["direct_reference_failure_rows"] = [0, 1, 2, 4, 5, 6, 7]
        with self.assertRaisesRegex(ValueError, "do not cover the group's executed rows"):
            self.rule(partial)

    def test_supplement_digest_count_is_one_per_shard_exactly_when_rows_resolve(self):
        unsupplemented = copy.deepcopy(self.report)
        unsupplemented["reference_supplement_sha256"] = []
        with self.assertRaisesRegex(ValueError, "without an independent reference supplement"):
            self.rule(unsupplemented)
        short = copy.deepcopy(self.report)
        short["reference_supplement_sha256"] = short["reference_supplement_sha256"][:1]
        with self.assertRaisesRegex(ValueError, "do not cover every census shard exactly"):
            self.rule(short)
        # With nothing resolved, no supplement digest may be retained (as in C).
        empty = copy.deepcopy(self.report)
        for field in ("direct_reference_failure_rows",) + binding.SUPPLEMENT_DEFECT_FIELDS:
            empty[field] = []
        with self.assertRaisesRegex(ValueError, "do not cover every census shard exactly"):
            self.rule(empty, (0, hashlib.sha256(b"").hexdigest()))
        empty["reference_supplement_sha256"] = []
        self.assertEqual(self.rule(empty, (0, hashlib.sha256(b"").hexdigest())), [])
        self.assertEqual(binding._supplement_resolved_rows(empty, self.rows, self.skips), [])

    # --- Per-row supplement authentication ----------------------------------

    def test_supplement_records_are_authenticated(self):
        proofs = self.supplements(self.report)
        self.assertEqual(set(proofs), {0, 4})
        for shard, row in enumerate(self.resolved):
            self.assertEqual(proofs[row]["reason"], binding.SUPPLEMENT_INELIGIBLE_REASON)
            self.assertEqual(proofs[row]["decision"], binding.SUPPLEMENT_DISPOSITION_DECISION)
            self.assertEqual(proofs[row]["supplement_manifest_sha256"],
                             self.report["reference_supplement_sha256"][shard])
        mismatched = copy.deepcopy(self.report)
        mismatched["reference_supplement_sha256"][0] = "0" * 64
        with self.assertRaisesRegex(ValueError, "manifest digest differs"):
            self.supplements(mismatched)
        with self.assertRaisesRegex(ValueError, "inventory differs"):
            binding._check_reference_supplements(self.fixture.root, self.report, self.rows, [0])

    def test_supplement_manifest_must_bind_the_census_identities(self):
        for field in ("rows_identity_sha256", "input_ledger_sha256"):
            original = self.manifest_path(0).read_text()
            with self.subTest(field=field):
                report = self.rehash_supplement(0, lambda manifest: manifest.__setitem__(field, "1" * 64))
                with self.assertRaisesRegex(ValueError, "does not bind the census identities"):
                    self.supplements(report)
            self.manifest_path(0).write_text(original)

    def test_supplement_record_must_bind_its_group_and_shard(self):
        original = self.manifest_path(0).read_text()
        report = self.rehash_supplement(0, lambda manifest: manifest["results"][0].__setitem__("group", "2"))
        with self.assertRaisesRegex(ValueError, "not bound to its census group and shard"):
            self.supplements(report)
        self.manifest_path(0).write_text(original)
        # Move shard 0's record into shard 1's manifest (with its object).
        record = json.loads(original)["results"][0]
        source = self.manifest_path(0).parent / "0.o"
        (self.manifest_path(1).parent / "0.o").write_bytes(source.read_bytes())
        report = self.rehash_supplement(0, lambda manifest: manifest["results"].clear())
        report = self.rehash_supplement(1, lambda manifest: manifest["results"].insert(0, record),
                                        report)
        with self.assertRaisesRegex(ValueError, "not bound to its census group and shard"):
            self.supplements(report)

    def test_supplement_object_needs_the_target_header(self):
        import struct
        data = bytearray(64)
        data[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HH", data, 16, 1, 183)  # AArch64 for an x86-64 row.
        (self.manifest_path(0).parent / "0.o").write_bytes(data)
        digest = hashlib.sha256(bytes(data)).hexdigest()
        report = self.rehash_supplement(0, lambda manifest: manifest["results"][0].update(
            object_bytes=len(data), object_sha256=digest))
        with self.assertRaisesRegex(ValueError, "object differs from its record or target header"):
            self.supplements(report)

    def test_rehashed_supplement_missing_one_record_is_refused(self):
        report = self.rehash_supplement(1, lambda manifest: manifest["results"].pop())
        with self.assertRaisesRegex(ValueError, "inventory differs"):
            self.supplements(report)

    def test_failed_supplement_control_is_refused(self):
        claimed = self.rehash_supplement(1, lambda manifest: manifest["results"][0].__setitem__("status", 1))
        with self.assertRaisesRegex(ValueError, "did not pass"):
            self.supplements(claimed)
        # The honest census of that supplement retains the unresolved reference.
        report = census.validate_shards(self.fixture.shards, self.fixture.root / "failed.json",
                                        True, reference_supplements=True)
        self.assertEqual(report["reference_failure_rows"], [4, 5, 6, 7])
        with self.assertRaisesRegex(ValueError, "contains reference_failure_rows"):
            self.rule(report)

    # --- Eligibility derivation (a) -----------------------------------------

    def test_resolved_rows_become_compiler_ineligible_with_the_reason(self):
        proofs = self.supplements(self.report)
        expected_object, by_row, reasons = disposition_inputs(self.rows)
        parsed = [disposition_row(index, row, "object", index not in self.resolved)
                  for index, row in enumerate(self.rows)]
        parsed.append(disposition_row(len(parsed), self.rows[1], "link", True))
        eligible, applicability, objects = binding._derive_compiler_eligibility(
            parsed, expected_object, set(), proofs, by_row, reasons)
        self.assertEqual(set(range(len(parsed))) - eligible, {0, 4})
        self.assertEqual(objects, len(self.rows) - 2)
        for row in self.resolved:
            self.assertEqual(applicability[row]["compiler_ineligible_reason"],
                             binding.SUPPLEMENT_INELIGIBLE_REASON)
            self.assertEqual(applicability[row]["supplement"], proofs[row])
        timed = {row["row"] for row in binding._timed_rows(parsed)}
        self.assertFalse({0, 4} & timed)
        self.assertEqual(len(timed), 16 - 2 + 1)


class ApprovedSupplementSetTests(unittest.TestCase):
    """The approved set: the 276 rows of census run 36336216460 / job 108667445262.

    Its 69 (fixture, target) pairs are each resolved in all four
    (frontend, PIC) configurations. The job log's report binds the checked-in
    support declaration, whose validated cross-product fixes the row order.
    """

    PAIRS = (
        ("tests/basic_c_aarch64_float_to_f128.c", "aarch64-linux-android"),
        ("tests/basic_c_aarch64_float_to_f128.c", "aarch64-unknown-linux-gnu"),
        ("tests/basic_c_aarch64_float_to_f128.c", "aarch64-unknown-uefi"),
        ("tests/basic_c_asm.c", "aarch64-apple-ios"),
        ("tests/basic_c_asm.c", "aarch64-apple-macos"),
        ("tests/basic_c_asm.c", "aarch64-linux-android"),
        ("tests/basic_c_asm.c", "aarch64-pc-windows-msvc"),
        ("tests/basic_c_asm.c", "aarch64-unknown-linux-gnu"),
        ("tests/basic_c_asm.c", "aarch64-unknown-uefi"),
        ("tests/basic_c_asm_goto_identity.c", "aarch64-apple-ios"),
        ("tests/basic_c_asm_goto_identity.c", "aarch64-apple-macos"),
        ("tests/basic_c_asm_goto_identity.c", "aarch64-linux-android"),
        ("tests/basic_c_asm_goto_identity.c", "aarch64-pc-windows-msvc"),
        ("tests/basic_c_asm_goto_identity.c", "aarch64-unknown-linux-gnu"),
        ("tests/basic_c_asm_goto_identity.c", "aarch64-unknown-uefi"),
        ("tests/basic_c_asm_goto_identity.c", "x86_64-apple-ios"),
        ("tests/basic_c_asm_goto_identity.c", "x86_64-apple-macos"),
        ("tests/basic_c_asm_goto_identity.c", "x86_64-linux-android"),
        ("tests/basic_c_asm_goto_identity.c", "x86_64-pc-windows-msvc"),
        ("tests/basic_c_asm_goto_identity.c", "x86_64-unknown-linux-gnu"),
        ("tests/basic_c_asm_goto_identity.c", "x86_64-unknown-uefi"),
        ("tests/basic_c_asm_goto_range.c", "aarch64-apple-ios"),
        ("tests/basic_c_asm_goto_range.c", "aarch64-apple-macos"),
        ("tests/basic_c_asm_goto_range.c", "aarch64-linux-android"),
        ("tests/basic_c_asm_goto_range.c", "aarch64-pc-windows-msvc"),
        ("tests/basic_c_asm_goto_range.c", "aarch64-unknown-linux-gnu"),
        ("tests/basic_c_asm_goto_range.c", "aarch64-unknown-uefi"),
        ("tests/basic_c_compiler_barrier_fallback.c", "x86_64-apple-ios"),
        ("tests/basic_c_compiler_barrier_fallback.c", "x86_64-apple-macos"),
        ("tests/basic_c_compiler_barrier_fallback.c", "x86_64-linux-android"),
        ("tests/basic_c_compiler_barrier_fallback.c", "x86_64-pc-windows-msvc"),
        ("tests/basic_c_compiler_barrier_fallback.c", "x86_64-unknown-linux-gnu"),
        ("tests/basic_c_compiler_barrier_fallback.c", "x86_64-unknown-uefi"),
        ("tests/basic_c_created_nan_sign.c", "aarch64-linux-android"),
        ("tests/basic_c_created_nan_sign.c", "aarch64-unknown-linux-gnu"),
        ("tests/basic_c_created_nan_sign.c", "aarch64-unknown-uefi"),
        ("tests/basic_c_signbit_images.c", "aarch64-linux-android"),
        ("tests/basic_c_signbit_images.c", "aarch64-unknown-linux-gnu"),
        ("tests/basic_c_signbit_images.c", "aarch64-unknown-uefi"),
        ("tests/basic_c_wide_vector_abi.c", "x86_64-apple-ios"),
        ("tests/basic_c_wide_vector_abi.c", "x86_64-apple-macos"),
        ("tests/basic_c_wide_vector_abi.c", "x86_64-linux-android"),
        ("tests/basic_c_wide_vector_abi.c", "x86_64-pc-windows-msvc"),
        ("tests/basic_c_wide_vector_abi.c", "x86_64-unknown-linux-gnu"),
        ("tests/basic_c_wide_vector_abi.c", "x86_64-unknown-uefi"),
        ("tests/basic_stb_compat.c", "aarch64-pc-windows-msvc"),
        ("tests/differential/native_aggregate_host.c", "x86_64-unknown-linux-gnu"),
        ("tests/differential/win64_aligned.c", "aarch64-unknown-uefi"),
        ("tests/differential/win64_aligned.c", "x86_64-pc-windows-msvc"),
        ("tests/differential/win64_aligned.c", "x86_64-unknown-uefi"),
        ("tests/differential/win64_vector.c", "x86_64-apple-ios"),
        ("tests/differential/win64_vector.c", "x86_64-apple-macos"),
        ("tests/differential/win64_vector.c", "x86_64-linux-android"),
        ("tests/differential/win64_vector.c", "x86_64-pc-windows-msvc"),
        ("tests/differential/win64_vector.c", "x86_64-unknown-linux-gnu"),
        ("tests/differential/win64_vector.c", "x86_64-unknown-uefi"),
        ("tests/differential/win64_wide.c", "aarch64-unknown-uefi"),
        ("tests/differential/win64_wide.c", "x86_64-apple-ios"),
        ("tests/differential/win64_wide.c", "x86_64-apple-macos"),
        ("tests/differential/win64_wide.c", "x86_64-linux-android"),
        ("tests/differential/win64_wide.c", "x86_64-pc-windows-msvc"),
        ("tests/differential/win64_wide.c", "x86_64-unknown-linux-gnu"),
        ("tests/differential/win64_wide.c", "x86_64-unknown-uefi"),
        ("tests/host_aarch64_float_to_f128.c", "aarch64-apple-ios"),
        ("tests/host_aarch64_float_to_f128.c", "aarch64-apple-macos"),
        ("tests/host_aarch64_float_to_f128.c", "aarch64-linux-android"),
        ("tests/host_aarch64_float_to_f128.c", "aarch64-pc-windows-msvc"),
        ("tests/host_aarch64_float_to_f128.c", "aarch64-unknown-linux-gnu"),
        ("tests/host_aarch64_float_to_f128.c", "aarch64-unknown-uefi"),
    )
    CENSUS_DECLARATION_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"
    HOST_FIXTURES = ["tests/basic_c_asm_goto_identity.c", "tests/basic_c_compiler_barrier_fallback.c",
                     "tests/basic_c_wide_vector_abi.c", "tests/differential/native_aggregate_host.c",
                     "tests/differential/win64_vector.c", "tests/differential/win64_wide.c"]

    def census(self):
        """The approved census row order: the checked-in declaration's cross-product."""
        root = Path(__file__).resolve().parents[1]
        declaration = (root / binding.SUPPORT_DECLARATION_PATH).read_bytes()
        # The declaration the job-log report binds (support_contract_sha256).
        self.assertEqual(hashlib.sha256(declaration).hexdigest(), self.CENSUS_DECLARATION_SHA256)
        with (root / binding.SUPPORT_DECLARATION_PATH).open() as stream:
            subjects = [row["path"] for row in csv.DictReader(stream, delimiter="\t")
                        if row["role"] == "subject"]
        rows = []
        for fixture in subjects:
            recipe = census.expected_fixture_recipe(fixture)[0]
            for target in census.TARGETS:
                abi, link, execution = census.TARGETS[target]
                cpu = census.expected_cpu(fixture, target, "baseline")
                for frontend in ("local-backed-canonical", "direct-ssa"):
                    for pic in ("0", "1"):
                        for allocator in census.ALLOCATORS:
                            index = len(rows)
                            rows.append({
                                "row": str(index), "group": str(index // 4), "fixture": fixture,
                                "target": target, "target_abi": abi, "cpu": cpu,
                                "cpu_features": cpu, "allocator": allocator,
                                "frontend_lowering": frontend, "PIC": pic,
                                "fixture_recipe": recipe,
                                "compile_obligation": "supported-object-zero-fallback",
                                "link_obligation": link, "execution_obligation": execution,
                                "diagnostic_obligation": "none",
                                "argv_evidence": f"groups/{index // 4}/{allocator}.argv"})
        self.assertEqual(len(rows), census.FULL_ROW_COUNT)
        return rows

    def report(self, rows, resolved):
        resolved_groups = {row // 4 for row in resolved}
        direct = sorted(index for index in range(len(rows)) if index // 4 in resolved_groups)
        return {"profile": "full-census", "shards": 4,
                "direct_reference_failure_rows": direct,
                "reference_supplement_sha256": ["a" * 64, "b" * 64, "c" * 64, "d" * 64],
                **{field: resolved for field in binding.SUPPLEMENT_DEFECT_FIELDS},
                **{field: [] for field in binding.ALWAYS_FATAL_DEFECT_FIELDS}}

    def approved_rows(self, rows):
        pairs = set(self.PAIRS)
        return [index for index, row in enumerate(rows)
                if row["allocator"] == "none" and (row["fixture"], row["target"]) in pairs]

    def test_pinned_set_is_the_job_log_census_set(self):
        rows = self.census()
        resolved = self.approved_rows(rows)
        self.assertEqual(len(self.PAIRS), 69)
        self.assertEqual(binding.APPROVED_SUPPLEMENT_SETS["full-census"],
                         (276, binding._supplement_identity_digest(rows, resolved)))
        self.assertEqual(binding._supplement_resolved_rows(self.report(rows, resolved), rows, set()),
                         resolved)
        self.assertEqual([sum(int(rows[row]["group"]) % 4 == shard for row in resolved)
                          for shard in range(4)], [69] * 4)
        self.assertEqual(len({rows[row]["target"] for row in resolved}), 12)
        host = sorted({rows[row]["fixture"] for row in resolved
                       if rows[row]["target"] == binding.NATIVE_TIMED_TARGET})
        self.assertEqual(host, self.HOST_FIXTURES)
        # One row more or less is a different set, which needs a new decision.
        extra = sorted(resolved + [next(index for index, row in enumerate(rows)
                                        if row["allocator"] == "none" and index not in resolved)])
        for changed in (resolved[1:], extra):
            with self.subTest(rows=len(changed)), \
                    self.assertRaisesRegex(ValueError, "not exactly the approved set"):
                binding._supplement_resolved_rows(self.report(rows, changed), rows, set())

    def test_approved_set_population_delta(self):
        rows = self.census()
        resolved = self.approved_rows(rows)
        proofs = {row: {"reason": binding.SUPPLEMENT_INELIGIBLE_REASON} for row in resolved}
        expected_object, by_row, reasons = disposition_inputs(rows)
        before = [disposition_row(index, row, "object", True) for index, row in enumerate(rows)]
        after = [disposition_row(index, row, "object", index not in proofs)
                 for index, row in enumerate(rows)]
        eligible, _applicability, _objects = binding._derive_compiler_eligibility(
            after, expected_object, set(), proofs, by_row, reasons)
        self.assertEqual(len(eligible), census.FULL_ROW_COUNT - 276)
        timed_before = {row["row"] for row in binding._timed_rows(before)}
        timed_after = {row["row"] for row in binding._timed_rows(after)}
        code_before = {row["row"] for row in before if row["metrics"]["generated_code_bytes"]}
        code_after = {row["row"] for row in after if row["metrics"]["generated_code_bytes"]}
        removed_timed = timed_before - timed_after
        removed_code = code_before - code_after
        # At most 276 code-byte ratio rows (a zero-baseline row was never one).
        self.assertEqual(len(removed_timed), 24)
        self.assertEqual(len(removed_code), 276)
        self.assertEqual(len(removed_code - removed_timed), 252)
        self.assertTrue(all(rows[row]["allocator"] == "none" for row in removed_code))
        # The six host fixtures use compiler-default: no timed batch group vanishes.
        self.assertEqual(len(binding._batch_groups(before)), len(binding._batch_groups(after)))


if __name__ == "__main__":
    unittest.main()
