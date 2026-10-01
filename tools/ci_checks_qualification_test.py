#!/usr/bin/env python3
"""Synthetic parser controls only; these fixtures are not CI qualification runs."""
from collections import Counter
import copy
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import ci_checks_qualification as qualification
import ci_matrix_phases as phases
import ci_matrix_phases_test as phase_tests
import github_ci_time as github


def reference(root, name, value):
    path = root / name
    path.write_text(json.dumps(value) + "\n", encoding="utf-8")
    return {"path": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_fixture(self, variant="combined-overlap", reuse=False):
        names = list(github.combination_jobs("split" if variant == "split-overlap" else "combined"))
        if reuse:
            names.append(github.MAIN_REUSE_JOB)
        origin = datetime(2026, 10, 1, tzinfo=timezone.utc)
        stamp = lambda seconds: (origin + timedelta(seconds=seconds)).isoformat()
        jobs = [dict(id=i + 1, name=name, status="completed", conclusion="success", run_attempt=1, labels=["synthetic"],
                     created_at=stamp(1), started_at=stamp(10), completed_at=stamp(110), steps=[])
                for i, name in enumerate(names)]
        return dict(id=123, path=".github/workflows/ci.yml", event="workflow_dispatch", head_branch="codex/ci-checks-" + variant,
                    status="completed", conclusion="success", run_attempt=1, head_sha="a" * 40, workflow_blob_sha="b" * 40,
                    created_at=stamp(0), jobs=jobs)

    def measured(self):
        return {"job_seconds": {"Windows x86-64 checks": 100}, "elapsed_seconds": 110, "runner_seconds": 2100}

    def test_exact_inventory_and_optional_reuse_is_counted(self):
        for variant, expected in (("combined-overlap", 21), ("split-overlap", 27)):
            for reuse in (False, True):
                with self.subTest(variant=variant, reuse=reuse), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                    result = qualification.timing(self.run_fixture(variant, reuse), variant)
                    self.assertEqual(result["job_count"], expected + int(reuse))
                    self.assertEqual(result["runner_seconds"], 100 * (expected + int(reuse)))
                    self.assertEqual(result["elapsed_seconds"], 110)
                    self.assertEqual(result["initial_queue_seconds"], 10)
                    self.assertEqual(set(result["job_queue_seconds"].values()), {9})

    def test_only_optional_first_attempt_skipped_reuse_has_zero_runner_cost(self):
        for variant, expected in (("combined-overlap", 21), ("split-overlap", 27)):
            run = self.run_fixture(variant, reuse=True)
            run["jobs"][-1].update(conclusion="skipped", created_at=None, started_at=None, completed_at=None, labels=[])
            with self.subTest(variant=variant), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                result = qualification.timing(run, variant)
                self.assertEqual(result["job_count"], expected + 1)
                self.assertEqual(result["runner_seconds"], expected * 100)
                self.assertEqual(result["elapsed_seconds"], 110)
                self.assertEqual(result["skipped_metadata_jobs"], [github.MAIN_REUSE_JOB])
                self.assertNotIn(github.MAIN_REUSE_JOB, result["job_queue_seconds"])
            for change in (dict(run_attempt=2), dict(conclusion="failure"), dict(conclusion="cancelled"), dict(status="in_progress")):
                invalid = copy.deepcopy(run)
                invalid["jobs"][-1].update(change)
                with self.subTest(change=change), mock.patch.object(github, "measure", return_value=(self.measured(), None)), self.assertRaises(ValueError):
                    qualification.timing(invalid, variant)
            run["jobs"][0].update(conclusion="skipped")
            with mock.patch.object(github, "measure", return_value=(self.measured(), None)), self.assertRaises(ValueError):
                qualification.timing(run, variant)

    def test_skipped_reuse_has_no_assigned_runner_conditions(self):
        run = self.run_fixture(reuse=True)
        run["jobs"][-1].update(conclusion="skipped", created_at=None, started_at=None, completed_at=None, labels=[])
        jobs = {job["name"]: dict(job_id=job["id"], image_os="fixture", image_version="1", runner="synthetic", toolchains={"fixture": "1"}, caches={})
                for job in run["jobs"] if job["name"] != github.MAIN_REUSE_JOB}
        value = dict(schema="buster-ci-checks-conditions-v1", run_id=123, jobs=jobs)
        _, skipped = qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        _, absent = qualification.conditions(self.root, reference(self.root, "conditions.json", value), self.run_fixture())
        self.assertEqual(skipped, absent)
        self.assertNotIn(github.MAIN_REUSE_JOB, skipped)
        value["jobs"][github.MAIN_REUSE_JOB] = dict(job_id=22, image_os="fixture", image_version="1", runner="synthetic", toolchains={}, caches={})
        with self.assertRaisesRegex(ValueError, "exact-job"):
            qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)
        del value["jobs"][github.MAIN_REUSE_JOB]
        del value["jobs"][run["jobs"][0]["name"]]
        with self.assertRaisesRegex(ValueError, "exact-job"):
            qualification.conditions(self.root, reference(self.root, "conditions.json", value), run)

    def test_missing_duplicate_failed_or_partial_jobs_never_qualify(self):
        mutations = (lambda r: r["jobs"].pop(), lambda r: r["jobs"].append(r["jobs"][0]),
                     lambda r: r["jobs"][0].update(conclusion="skipped"),
                     lambda r: r["jobs"][0].update(created_at=None),
                     lambda r: r["jobs"][0].update(run_attempt=2),
                     lambda r: r.update(head_branch="main"))
        for mutation in mutations:
            run = self.run_fixture()
            mutation(run)
            with self.subTest(mutation=mutation), mock.patch.object(github, "measure", return_value=(self.measured(), None)):
                with self.assertRaises((ValueError, TypeError)):
                    qualification.timing(run, "combined-overlap")

    def test_native_success_requirements_are_not_replaced_by_timestamps(self):
        with self.assertRaisesRegex(ValueError, "ineligible GitHub attempt"):
            qualification.timing(self.run_fixture(), "combined-overlap")

    def test_missing_unknown_and_unmatched_conditions_fail(self):
        run = self.run_fixture("split-overlap")
        jobs = {job["name"]: dict(job_id=job["id"], image_os="fixture", image_version="1", runner="synthetic", toolchains={"fixture": "1"}, caches={}) for job in run["jobs"]}
        value = dict(schema="buster-ci-checks-conditions-v1", run_id=123, jobs=jobs)
        good = reference(self.root, "conditions.json", value)
        _, normalized = qualification.conditions(self.root, good, run)
        self.assertEqual(len(normalized), 21)
        for change in (lambda v: v["jobs"].pop("Workflow lint"),
                       lambda v: v["jobs"]["Linux x86-64 portability"].update(image_version="unknown"),
                       lambda v: v["jobs"]["Linux x86-64 portability"].update(image_version="2"),
                       lambda v: v["jobs"]["Workflow lint"].pop("caches")):
            invalid = copy.deepcopy(value)
            change(invalid)
            with self.subTest(change=change), self.assertRaises(ValueError):
                qualification.conditions(self.root, reference(self.root, "invalid.json", invalid), run)

    def test_digest_binding_and_empty_campaign_stay_pending(self):
        item = reference(self.root, "retained.json", {"sample": 1})
        (self.root / item["path"]).write_text("{}\n")
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            qualification.record(self.root, item)
        path = self.root / "campaign.json"
        reference(self.root, path.name, dict(schema=qualification.SCHEMA, repository="buster14a/buster", samples=[]))
        report = qualification.qualify(path)
        self.assertEqual(report["status"], "pending")
        self.assertFalse(report["performance_accepted"])

    def observation(self, variant, identity):
        windows, wall = {"combined-overlap": (100, 1000), "combined-all-builds": (90, 990), "split-overlap": (80, 850)}[variant]
        return dict(variant=variant, run_id=identity, head_sha="a" * 40, workflow_blob_sha="b" * 40,
                    conditions={"same": True}, platforms={"same": True}, timing=dict(elapsed_seconds=wall, runner_seconds=105 if variant != "combined-overlap" else 100,
                    job_seconds={"Windows x86-64 checks": windows}, initial_queue_seconds=5, job_queue_seconds={}))

    def campaign(self):
        path = self.root / "campaign.json"
        samples = [dict(variant=variant, synthetic_id=i + 1) for i, variant in enumerate(qualification.VARIANTS * 3)]
        reference(self.root, path.name, dict(schema=qualification.SCHEMA, repository="buster14a/buster", samples=samples))
        return path

    def test_contract_boundaries_and_runner_growth_are_enforced(self):
        def collect(root, item):
            return self.observation(item["variant"], item["synthetic_id"])
        with mock.patch.object(qualification, "sample", side_effect=collect):
            report = qualification.qualify(self.campaign())
        self.assertEqual(report["status"], "pending")
        self.assertEqual(report["timing_status"], "accepted")
        self.assertTrue(report["timing_contract_met"])
        self.assertFalse(report["performance_accepted"])
        self.assertEqual(report["resource_review"], "pending")
        self.assertTrue(report["pending_reviews"])
        self.assertTrue(all(issue["status"] == "pending" for issue in report["issues"].values()))
        self.assertEqual(report["issues"]["2119"]["time_ratio"], .90)
        self.assertEqual(report["issues"]["2120"]["time_ratio"], .85)
        def costly(root, item):
            result = collect(root, item)
            if result["variant"] == "split-overlap":
                result["timing"]["runner_seconds"] = 105.1
            return result
        with mock.patch.object(qualification, "sample", side_effect=costly):
            report = qualification.qualify(self.campaign())
        self.assertEqual(report["status"], "rejected")
        self.assertEqual(report["timing_status"], "rejected")
        self.assertFalse(report["timing_contract_met"])
        self.assertFalse(report["performance_accepted"])
        self.assertEqual(report["issues"]["2120"]["status"], "rejected")

    def test_changed_census_source_images_and_repeated_runs_stay_pending(self):
        for key in ("head_sha", "workflow_blob_sha", "conditions", "platforms", "run_id"):
            def collect(root, item):
                result = self.observation(item["variant"], item["synthetic_id"])
                if item["synthetic_id"] == 9:
                    result[key] = 1 if key == "run_id" else "changed"
                return result
            with self.subTest(key=key), mock.patch.object(qualification, "sample", side_effect=collect):
                result = qualification.qualify(self.campaign())
                self.assertEqual(result["status"], "pending")
                self.assertFalse(result["performance_accepted"])

    def test_archived_native_journal_failure_cannot_be_hidden_by_complete_summary(self):
        directory = self.root / "matrix-phases"
        directory.mkdir()
        coverage = phase_tests.fixture(directory)
        coverage.update(kind="desktop-matrix-coverage", mode="ci")
        result = dict(success=True, metadata=dict(GITHUB_SHA="a" * 40, GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="1"))
        summary = phases.analyze(directory, coverage)
        result["matrix_phases"] = summary
        item = dict(job="Windows x86-64 checks", phase_directory=directory.name, tests=[],
                    coverage=reference(self.root, "coverage.json", coverage), result=reference(self.root, "result.json", result),
                    phases=reference(self.root, "phases.json", summary))
        next(directory.glob("*.end.json")).unlink()
        with self.assertRaisesRegex(ValueError, "interrupted publication"):
            qualification.desktop(self.root, item, self.run_fixture(), {}, "combined-overlap")

    def complete_desktop(self, release=False, direct=False):
        directory = self.root / "matrix-phases"
        directory.mkdir()
        coverage = phase_tests.fixture(directory, direct=direct)
        coverage.update(kind="desktop-matrix-coverage", mode="ci")
        coverage["identity"]["suite"] = "desktop"
        plan = phases.read(directory / "plan.json")
        for row, cap, tree in zip(coverage["expected"], coverage["detected"], plan["trees"]):
            row.update(optimize=row["configuration"] == "Release", execution="runtime" if row["compiler"] == "clang" else "compile-link", exclusion="")
            if release and row["compiler"] == "clang" and row["configuration"] == "Release":
                row.update(sanitize=False, unity=True)
                tree["sanitize"] = 0
            row["owner_shard"] = qualification.coverage_tools._coverage_row_owner(row)
            row["id"] = qualification.coverage_tools._coverage_row_id(coverage["identity"], row)
            cap["id"] = row["id"]
            tree["rows"] = [row["id"]]
        if release:
            coverage["identity"]["shard"] = plan["identity"]["shard"] = "release"
            plan["trees"] = [tree for tree in plan["trees"] if tree["id"] == "tree1"]
            plan["tasks"] = [task for task in plan["tasks"] if task["tree"] in ("tree1", "matrix")]
            plan["outer_jobs"] = 1
            kept = {task["id"] for task in plan["tasks"]}
            for path in directory.iterdir():
                if path.name not in ("plan.json", "terminal.json") and path.name.split(".", 1)[0] not in kept:
                    path.unlink()
        phase_tests.write(directory, "plan.json", plan)
        coverage["policy"] = dict(version=1, row_count=5, required_count=5, excluded_count=0,
                                  fingerprint=qualification.coverage_tools._coverage_policy_fingerprint(coverage["identity"], coverage["expected"]))
        selected = qualification.coverage_tools._coverage_selected_ids({r["id"]: r for r in coverage["expected"]}, "release" if release else "checks")
        coverage["executed"] = [dict(lane_id="fixture", status="success", evidence="driver-complete", rows=sorted(selected))]
        condition = dict(image_os="fixture", image_version="1", runner="synthetic", caches={"BUSTER_CI_ZIG_CACHE_HIT": "false"})
        meta = dict(GITHUB_SHA="a" * 40, GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="1", ImageOS="fixture", ImageVersion="1", BUSTER_CI_RUNNER="synthetic", BUSTER_CI_ZIG_CACHE_HIT="false")
        summary = phases.analyze(directory, coverage, meta)
        tests = []
        pairs = [(row, cap) for row, cap in zip(coverage["expected"], coverage["detected"]) if row["id"] in selected and row["execution"] == "runtime"]
        for i, (row, cap) in enumerate(pairs):
            tree = next(t for t in summary["trees"] if row["id"] in t["rows"])
            event = next(e for e in summary["events"] if e["id"] == phases.task_id(tree["id"], "test", row["configuration"]))
            sidecar = self.root / "unit-observations" / event["id"]
            sidecar.mkdir(parents=True)
            inventory = [dict(index=0, name="compiler_driver_tests", table_audit=False), dict(index=1, name="fixture", table_audit=False), dict(index=2, name="table_audit_fixture", table_audit=True)]
            lines = [f"CI_UNIT_MODULE_V1 index={r['index']} module={r['name']} table_audit={int(r['table_audit'])} enabled={int(not r['table_audit'])} selected=0 group={'driver' if r['index'] == 0 else 'rest'}" for r in inventory]
            lines += ["CI_UNIT_BATCH_V1 group=inventory modules=0 modules_passed=0 assertions=0 passed=0 failed=0 external=0 external_passed=0 status=inventory", "[0/0] Unit tests (0 of 3 modules selected)", "[0/0] Module tests", "[0/0] External tests"]
            inventory_path = sidecar / "inventory.log"
            inventory_path.write_text("\n".join(lines) + "\n")
            log = sidecar / "test.log"
            timing = "TEST_MODULE_TIMING index=0 module=compiler_driver_tests duration_ns=1 passed=1 failed=0 assertions=1 status=pass\nTEST_MODULE_TIMING index=1 module=fixture duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
            if release or direct:
                timing += "TEST_MODULE_TIMING index=2 module=table_audit_fixture duration_ns=1 passed=1 failed=0 assertions=1 status=pass\n"
            log.write_text(timing + ("[4/4] Unit tests\n[3/3] Module tests\n" if release or direct else "[3/3] Unit tests\n[2/2] Module tests\n") + "[0/0] External tests\n")
            unit = dict(schema="buster-ci-unit-tests-measure-v1", arm="baseline", mode="serial", exit_code=0, test_workers=1, elapsed_us=20,
                        inventory=inventory, log=str(log.relative_to(self.root)),
                        identity=dict(source_revision="a" * 40, binary_sha256="e" * 64, runner_image={k: condition[k] for k in ("image_os", "image_version", "runner")},
                                      platform="macos" if direct else "windows", architecture="x86_64", configuration=row["configuration"], sanitize=row["sanitize"], fuzz=row["fuzz"], table_audits=release or direct,
                                      toolchain={k: cap[k] for k in qualification.CAP_KEYS}, cpu_budget=4))
            receipt = {k: event[k] for k in ("id", "epoch_us", "pid", "argv")}
            receipt.update(schema="buster-desktop-unit-observation-v1", source_revision="a" * 40, run_id="123", run_attempt="1", binary_path=event["argv"][0], binary_sha256="e" * 64,
                           inventory_file="inventory.log", log_file="test.log", inventory_sha256=hashlib.sha256(inventory_path.read_bytes()).hexdigest(),
                           log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(), binary_unchanged=True, capture_complete=True, test_result=0)
            tests.append(dict(row_id=row["id"], manifest=reference(self.root, f"unit{i}.json", unit),
                              observation=reference(self.root, str((sidecar / "observation.json").relative_to(self.root)), receipt), log_sha256=receipt["log_sha256"]))
        result = dict(success=True, metadata=meta, matrix_phases=summary)
        item = dict(job=("macOS x86-64" if direct else "Windows x86-64") + (" release" if release else " checks"), phase_directory=directory.name, tests=tests,
                    coverage=reference(self.root, "coverage.json", coverage), result=reference(self.root, "result.json", result), phases=reference(self.root, "phases.json", summary))
        return item, coverage, condition

    def test_complete_census_replays_and_nonreproducible_binary_hashes_do_not_change_population(self):
        item, coverage, condition = self.complete_desktop()
        before = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        directory = self.root / item["phase_directory"]
        plan = phases.read(directory / "plan.json")
        plan["identity"]["driver_hash"] = coverage["identity"]["driver_hash"] = "f" * 64
        phase_tests.write(directory, "plan.json", plan)
        result = qualification.record(self.root, item["result"])
        summary = phases.analyze(directory, coverage, result["metadata"])
        result["matrix_phases"] = summary
        item.update(coverage=reference(self.root, "coverage.json", coverage), result=reference(self.root, "result.json", result), phases=reference(self.root, "phases.json", summary))
        for i, test in enumerate(item["tests"]):
            unit = qualification.record(self.root, test["manifest"])
            unit["identity"]["binary_sha256"] = "f" * 64
            test["manifest"] = reference(self.root, f"unit{i}.json", unit)
            receipt = qualification.record(self.root, test["observation"])
            receipt["binary_sha256"] = "f" * 64
            test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        after = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertEqual(before, after)
        self.assertEqual(len(after["census"]), 2)
        item["tests"].pop()
        with self.assertRaisesRegex(ValueError, "runtime assertion census"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_native_receipt_identity_completion_binary_and_task_path_are_required(self):
        item, _, condition = self.complete_desktop()
        receipt = qualification.record(self.root, item["tests"][0]["observation"])
        changes = {"schema": "wrong", "id": "foreign-task", "pid": 999, "epoch_us": 2, "argv": ["other-binary"],
                   "source_revision": "f" * 40, "run_id": "999", "run_attempt": "2", "binary_path": "other-binary",
                   "binary_sha256": "f" * 64, "binary_unchanged": False, "capture_complete": False, "test_result": 1,
                   "inventory_file": "other.log", "log_file": "other.log"}
        for key, value in changes.items():
            invalid = copy.deepcopy(item)
            changed = dict(receipt, **{key: value})
            test = invalid["tests"][0]
            test["observation"] = reference(self.root, test["observation"]["path"], changed)
            with self.subTest(key=key), self.assertRaises(ValueError):
                qualification.desktop(self.root, invalid, self.run_fixture(), condition, "combined-overlap")
        invalid = copy.deepcopy(item)
        invalid["tests"][0]["observation"] = reference(self.root, "foreign-observation.json", receipt)
        with self.assertRaisesRegex(ValueError, "exact native task sidecar"):
            qualification.desktop(self.root, invalid, self.run_fixture(), condition, "combined-overlap")

    def test_same_bytes_from_another_log_cannot_replace_native_log(self):
        item, _, condition = self.complete_desktop()
        test = item["tests"][0]
        unit = qualification.record(self.root, test["manifest"])
        original = self.root / unit["log"]
        substitute = self.root / "substitute.log"
        substitute.write_bytes(original.read_bytes())
        unit["log"] = substitute.name
        test["manifest"] = reference(self.root, test["manifest"]["path"], unit)
        with self.assertRaisesRegex(ValueError, "native test log"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_native_inventory_digest_and_independent_query_are_checked(self):
        item, _, condition = self.complete_desktop()
        test = item["tests"][0]
        receipt = qualification.record(self.root, test["observation"])
        inventory_path = (self.root / test["observation"]["path"]).parent / "inventory.log"
        original = inventory_path.read_text()
        inventory_path.write_text(original.replace("index=2", "index=3"))
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        receipt["inventory_sha256"] = hashlib.sha256(inventory_path.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "canonical index"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        inventory_path.write_text("\n".join(line for line in original.splitlines() if "index=2 " not in line).replace("of 3 modules", "of 2 modules") + "\n")
        receipt["inventory_sha256"] = hashlib.sha256(inventory_path.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "independent inventory"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_observed_binary_paths_use_original_windows_and_posix_source_roots(self):
        item, coverage, _ = self.complete_desktop()
        test = item["tests"][0]
        manifest_path = qualification.retained(self.root, test["manifest"])
        unit = qualification.units.validate_sample(manifest_path)
        receipt = qualification.record(self.root, test["observation"])
        summary = qualification.record(self.root, item["phases"])
        original_event = next(e for e in summary["events"] if e["id"] == receipt["id"])
        cases = (("windows", r"C:\retained\producer\build.c", r"build\tree0\Debug\ide.exe", r"c:\RETAINED\PRODUCER\build\tree0\Debug\ide.exe"),
                 ("linux", "/retained/producer/build.c", "build/tree0/Debug/ide", "/retained/producer/build/tree0/Debug/ide"))
        for platform, source, binary, observed in cases:
            identity = dict(coverage["identity"], platform=platform, source_path=source)
            event = dict(original_event, argv=[binary, "test"])
            value = dict(receipt, argv=event["argv"], binary_path=observed)
            test["observation"] = reference(self.root, test["observation"]["path"], value)
            with self.subTest(platform=platform):
                self.assertEqual(qualification.observation(self.root, item, test, unit, manifest_path, event, identity), value)
            value["binary_path"] = observed.replace("Debug", "Release")
            test["observation"] = reference(self.root, test["observation"]["path"], value)
            with self.subTest(wrong_tree=platform), self.assertRaisesRegex(ValueError, "binary/path mismatch"):
                qualification.observation(self.root, item, test, unit, manifest_path, event, identity)

    def test_canonical_release_executes_audits_after_audit_disabled_inventory_query(self):
        item, _, condition = self.complete_desktop(release=True)
        result = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        census = next(iter(result["census"].values()))
        self.assertEqual(census["skipped_table_audits"], [])
        self.assertEqual(set(census["modules"]), {"compiler_driver_tests", "fixture", "table_audit_fixture"})
        self.assertEqual(sum(m["assertions"] for m in census["modules"].values()), 4)
        test = item["tests"][0]
        unit = qualification.record(self.root, test["manifest"])
        unit["identity"]["table_audits"] = False
        test["manifest"] = reference(self.root, test["manifest"]["path"], unit)
        log = self.root / unit["log"]
        log.write_text("\n".join(line for line in log.read_text().splitlines() if "module=table_audit_fixture" not in line).replace("[4/4] Unit", "[3/3] Unit").replace("[3/3] Module", "[2/2] Module") + "\n")
        receipt = qualification.record(self.root, test["observation"])
        receipt["log_sha256"] = test["log_sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "table audit policy"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")

    def test_direct_runtime_keeps_default_audits_even_without_unity(self):
        item, _, condition = self.complete_desktop(direct=True)
        result = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertTrue(all(not row["unity"] for row in result["rows"]))
        self.assertTrue(all(not census["skipped_table_audits"] and "table_audit_fixture" in census["modules"] for census in result["census"].values()))
        test = item["tests"][0]
        unit = qualification.record(self.root, test["manifest"])
        unit["identity"]["table_audits"] = False
        test["manifest"] = reference(self.root, test["manifest"]["path"], unit)
        log = self.root / unit["log"]
        log.write_text("\n".join(line for line in log.read_text().splitlines() if "module=table_audit_fixture" not in line).replace("[4/4] Unit", "[3/3] Unit").replace("[3/3] Module", "[2/2] Module") + "\n")
        receipt = qualification.record(self.root, test["observation"])
        receipt["log_sha256"] = test["log_sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
        test["observation"] = reference(self.root, test["observation"]["path"], receipt)
        with self.assertRaisesRegex(ValueError, "table audit policy"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")


if __name__ == "__main__":
    unittest.main()
