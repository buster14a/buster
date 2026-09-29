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


class SupplementDispositionTests(unittest.TestCase):
    """Option 3 of the census disposition (decision recorded in #36)."""

    def setUp(self):
        from native_retirement_contract_test import ContractTests, read_table
        self.fixture = ContractTests()
        self.fixture.setUp()
        self.resolved = self.fixture.make_failed_direct_reference_supplements()
        self.report = census.validate_shards(self.fixture.shards, self.fixture.root / "report.json",
                                             True, reference_supplements=True)
        _fields, self.rows = read_table(self.fixture.shards[0] / "rows.tsv")
        self.skips = set(self.report["applicability_skip_rows"])

    def tearDown(self):
        self.fixture.tearDown()

    def rule(self, report):
        return binding._supplement_resolved_rows(report, self.rows, self.skips)

    def rehash_supplement(self, shard, mutation):
        """Rewrite one shard's supplement manifest and rebind its report digest."""
        path = self.fixture.shards[shard] / "reference-supplement/manifest.json"
        manifest = json.loads(path.read_text())
        mutation(manifest)
        path.write_text(json.dumps(manifest))
        report = copy.deepcopy(self.report)
        report["reference_supplement_sha256"][shard] = hashlib.sha256(path.read_bytes()).hexdigest()
        return report

    def test_supplement_set_is_accepted_and_those_rows_are_compiler_ineligible(self):
        self.assertEqual(self.resolved, [0, 4])
        self.assertEqual(self.rule(self.report), self.resolved)
        by_row = {row: name for name, ids in self.report["applicability_rows_by_class"].items()
                  for row in ids}
        evidence = binding._check_validator_projection_evidence(
            self.fixture.root, self.report, self.rows, by_row, self.skips)
        binding._replay_validator_report(self.fixture.root, self.report, evidence)
        proofs = binding._check_reference_supplements(self.fixture.root, self.report, self.rows,
                                                      self.resolved)
        self.assertEqual(set(proofs), {0, 4})
        for shard, row in enumerate(self.resolved):
            self.assertEqual(proofs[row]["reason"], binding.SUPPLEMENT_INELIGIBLE_REASON)
            self.assertEqual(proofs[row]["decision"], binding.SUPPLEMENT_DISPOSITION_DECISION)
            self.assertEqual(proofs[row]["supplement_manifest_sha256"],
                             self.report["reference_supplement_sha256"][shard])
        expected_object, _classes, _reasons = disposition_inputs(self.rows)
        parsed = [disposition_row(index, row, "object", index not in self.resolved)
                  for index, row in enumerate(self.rows)]
        parsed.append(disposition_row(len(parsed), self.rows[1], "link", True))
        eligible, applicability, objects = binding._derive_compiler_eligibility(
            parsed, expected_object, self.skips, proofs, by_row, evidence["reasons"])
        self.assertEqual(set(range(len(parsed))) - eligible, {0, 4})
        self.assertEqual(objects, len(self.rows) - 2)
        for row in self.resolved:
            self.assertFalse(applicability[row]["compiler_eligible"])
            self.assertEqual(applicability[row]["compiler_ineligible_reason"],
                             binding.SUPPLEMENT_INELIGIBLE_REASON)
            self.assertEqual(applicability[row]["supplement"], proofs[row])
            self.assertEqual(applicability[row]["reason"],
                             "direct-reference-unresolved-clang-control-passed")
        # Correctness and MIR rows are unchanged: the group's MIR rows stay eligible.
        self.assertTrue({1, 2, 3, 5, 6, 7} <= eligible)
        timed = {row["row"] for row in binding._timed_rows(parsed)}
        self.assertFalse({0, 4} & timed)
        self.assertEqual(len(timed), 16 - 2 + 1)
        self.assertEqual({row["row"] for row in parsed if row["metrics"]["generated_code_bytes"]},
                         eligible)

        # The rows are never silently dropped: an artifact that keeps them
        # eligible, or a stage row carrying their identity, is rejected.
        kept = copy.deepcopy(parsed)
        kept[0] = disposition_row(0, self.rows[0], "object", True)
        with self.assertRaisesRegex(ValueError, "supplement provenance"):
            binding._derive_compiler_eligibility(kept, expected_object, self.skips, proofs,
                                                 by_row, evidence["reasons"])
        stage = parsed + [disposition_row(len(parsed), self.rows[4], "link", False)]
        with self.assertRaisesRegex(ValueError, "stage row carries a supplement-resolved"):
            binding._derive_compiler_eligibility(stage, expected_object, self.skips, proofs,
                                                 by_row, evidence["reasons"])

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

    def test_supplement_inventory_and_digest_must_match(self):
        unsupplemented = copy.deepcopy(self.report)
        unsupplemented["reference_supplement_sha256"] = []
        with self.assertRaisesRegex(ValueError, "without an independent reference supplement"):
            self.rule(unsupplemented)
        short = copy.deepcopy(self.report)
        short["reference_supplement_sha256"] = short["reference_supplement_sha256"][:1]
        with self.assertRaisesRegex(ValueError, "do not cover every census shard"):
            self.rule(short)
        partial = copy.deepcopy(self.report)
        partial["direct_reference_failure_rows"] = [0, 1, 2, 4, 5, 6, 7]
        with self.assertRaisesRegex(ValueError, "do not cover the group's executed rows"):
            self.rule(partial)

        mismatched = copy.deepcopy(self.report)
        mismatched["reference_supplement_sha256"][0] = "0" * 64
        with self.assertRaisesRegex(ValueError, "manifest digest differs"):
            binding._check_reference_supplements(self.fixture.root, mismatched, self.rows,
                                                 self.resolved)
        with self.assertRaisesRegex(ValueError, "inventory differs"):
            binding._check_reference_supplements(self.fixture.root, self.report, self.rows, [0])

        def forge_object(manifest):
            manifest["results"][0]["object_sha256"] = "1" * 64
        with self.assertRaisesRegex(ValueError, "object differs"):
            binding._check_reference_supplements(self.fixture.root,
                                                 self.rehash_supplement(0, forge_object),
                                                 self.rows, self.resolved)

    def test_right_set_with_a_failed_supplement_is_rejected(self):
        def fail_control(manifest):
            manifest["results"][0]["status"] = 1
        claimed = self.rehash_supplement(1, fail_control)
        with self.assertRaisesRegex(ValueError, "did not pass"):
            binding._check_reference_supplements(self.fixture.root, claimed, self.rows,
                                                 self.resolved)
        # The honest census of that supplement retains the unresolved reference.
        report = census.validate_shards(self.fixture.shards, self.fixture.root / "failed.json",
                                        True, reference_supplements=True)
        self.assertEqual(report["reference_failure_rows"], [4, 5, 6, 7])
        with self.assertRaisesRegex(ValueError, "contains reference_failure_rows"):
            self.rule(report)
        by_row = {row: name for name, ids in claimed["applicability_rows_by_class"].items()
                  for row in ids}
        evidence = binding._check_validator_projection_evidence(
            self.fixture.root, report, self.rows, by_row, self.skips)
        with self.assertRaisesRegex(ValueError, "replay differs"):
            binding._replay_validator_report(self.fixture.root, claimed, evidence)


class SupplementDispositionShapeTests(unittest.TestCase):
    """The real census shape: 276 rows, 69 per shard, 24 on the timed host."""

    HOST_FIXTURES = ("tests/basic_c_asm_goto_identity.c", "tests/basic_c_compiler_barrier_fallback.c",
                     "tests/basic_c_wide_vector_abi.c", "tests/differential/native_aggregate_host.c",
                     "tests/differential/win64_vector.c", "tests/differential/win64_wide.c")
    F128_FIXTURE = "tests/host_aarch64_float_to_f128.c"

    def test_real_shape_population_delta(self):
        root = Path(__file__).resolve().parents[1]
        with (root / binding.SUPPORT_DECLARATION_PATH).open() as stream:
            subjects = [row["path"] for row in csv.DictReader(stream, delimiter="\t")
                        if row["role"] == "subject"]
        for fixture in self.HOST_FIXTURES + (self.F128_FIXTURE,):
            self.assertIn(fixture, subjects)
        targets = list(census.TARGETS)
        foreign = [target for target in targets if target != binding.NATIVE_TIMED_TARGET]
        aarch64 = [target for target in targets if target.startswith("aarch64-")]
        # 69 (fixture, target) pairs, each four groups (one per shard):
        # 58 retained-reference (6 on the host), 6 admitted-supported, 5
        # platform-inapplicable.
        pairs = {(fixture, binding.NATIVE_TIMED_TARGET): "retained-reference"
                 for fixture in self.HOST_FIXTURES}
        pairs.update({(self.F128_FIXTURE, target): "admitted-supported" for target in aarch64})
        others = [fixture for fixture in subjects
                  if fixture not in self.HOST_FIXTURES and fixture != self.F128_FIXTURE]
        for index in range(57):
            pairs[(others[index], foreign[index % len(foreign)])] = (
                "platform-inapplicable" if index < 5 else "retained-reference")
        self.assertEqual(len(pairs), 69)
        census_rows = []
        for fixture in subjects:
            recipe = census.expected_fixture_recipe(fixture)[0]
            for target in targets:
                abi, link, execution = census.TARGETS[target]
                cpu = census.expected_cpu(fixture, target, "baseline")
                for frontend in ("local-backed-canonical", "direct-ssa"):
                    for pic in ("0", "1"):
                        for allocator in census.ALLOCATORS:
                            index = len(census_rows)
                            census_rows.append({
                                "row": str(index), "group": str(index // 4), "fixture": fixture,
                                "target": target, "target_abi": abi, "cpu": cpu,
                                "cpu_features": cpu, "allocator": allocator,
                                "frontend_lowering": frontend, "PIC": pic,
                                "fixture_recipe": recipe,
                                "compile_obligation": "supported-object-zero-fallback",
                                "link_obligation": link, "execution_obligation": execution,
                                "diagnostic_obligation": "none",
                                "argv_evidence": f"groups/{index // 4}/{allocator}.argv"})
        self.assertEqual(len(census_rows), census.FULL_ROW_COUNT)
        direct = sorted(int(row["row"]) for row in census_rows
                        if (row["fixture"], row["target"]) in pairs)
        resolved = [row for row in direct if row % 4 == 0]
        report = {"direct_reference_failure_rows": direct, "shards": 4,
                  "reference_supplement_sha256": ["a" * 64, "b" * 64, "c" * 64, "d" * 64],
                  **{field: resolved for field in binding.SUPPLEMENT_DEFECT_FIELDS},
                  **{field: [] for field in binding.ALWAYS_FATAL_DEFECT_FIELDS}}
        self.assertEqual(binding._supplement_resolved_rows(report, census_rows, set()), resolved)
        self.assertEqual(len(resolved), 276)
        self.assertEqual([sum(int(census_rows[row]["group"]) % 4 == shard for row in resolved)
                          for shard in range(4)], [69] * 4)
        classes = [pairs[(census_rows[row]["fixture"], census_rows[row]["target"])]
                   for row in resolved]
        self.assertEqual({name: classes.count(name) for name in set(classes)},
                         {"retained-reference": 232, "admitted-supported": 24,
                          "platform-inapplicable": 20})
        self.assertEqual({census_rows[row]["target"] for row in resolved}, set(targets))

        proofs = {row: {"reason": binding.SUPPLEMENT_INELIGIBLE_REASON} for row in resolved}
        expected_object, by_row, reasons = disposition_inputs(census_rows)
        before = [disposition_row(index, row, "object", True)
                  for index, row in enumerate(census_rows)]
        after = [disposition_row(index, row, "object", index not in proofs)
                 for index, row in enumerate(census_rows)]
        eligible, _applicability, _objects = binding._derive_compiler_eligibility(
            after, expected_object, set(), proofs, by_row, reasons)
        self.assertEqual(len(eligible), census.FULL_ROW_COUNT - 276)
        timed_before = {row["row"] for row in binding._timed_rows(before)}
        timed_after = {row["row"] for row in binding._timed_rows(after)}
        code_before = {row["row"] for row in before if row["metrics"]["generated_code_bytes"]}
        code_after = {row["row"] for row in after if row["metrics"]["generated_code_bytes"]}
        removed_timed = timed_before - timed_after
        removed_code = code_before - code_after
        self.assertEqual(len(removed_timed), 24)
        self.assertEqual(len(removed_code), 276)
        self.assertEqual(len(removed_code - removed_timed), 252)
        self.assertTrue(all(census_rows[row]["allocator"] == "none" for row in removed_code))
        # The six real host fixtures use compiler-default: no timed batch group
        # vanishes. (Untimed group counts depend on which foreign fixtures are
        # affected, which this synthetic shape does not reproduce.)
        self.assertEqual(len(binding._batch_groups(before)), len(binding._batch_groups(after)))
        print(json.dumps({"proof": "supplement-disposition-real-shape",
                          "resolved_rows": len(resolved), "timed_rows_removed": len(removed_timed),
                          "untimed_code_byte_rows_removed": len(removed_code - removed_timed),
                          "code_byte_rows_removed": len(removed_code),
                          "timed_rows_before": len(timed_before), "timed_rows_after": len(timed_after),
                          "batch_groups": len(binding._batch_groups(after))}, sort_keys=True))


if __name__ == "__main__":
    unittest.main()
