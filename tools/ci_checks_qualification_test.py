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
        self.assertEqual(report["status"], "accepted")
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

    def complete_desktop(self):
        directory = self.root / "matrix-phases"
        directory.mkdir()
        coverage = phase_tests.fixture(directory)
        coverage.update(kind="desktop-matrix-coverage", mode="ci")
        coverage["identity"]["suite"] = "desktop"
        plan = phases.read(directory / "plan.json")
        for row, cap, tree in zip(coverage["expected"], coverage["detected"], plan["trees"]):
            row.update(optimize=row["configuration"] == "Release", execution="runtime" if row["compiler"] == "clang" else "compile-link", exclusion="")
            row["owner_shard"] = qualification.coverage_tools._coverage_row_owner(row)
            row["id"] = qualification.coverage_tools._coverage_row_id(coverage["identity"], row)
            cap["id"] = row["id"]
            tree["rows"] = [row["id"]]
        phase_tests.write(directory, "plan.json", plan)
        coverage["policy"] = dict(version=1, row_count=5, required_count=5, excluded_count=0,
                                  fingerprint=qualification.coverage_tools._coverage_policy_fingerprint(coverage["identity"], coverage["expected"]))
        coverage["executed"] = [dict(lane_id="fixture", status="success", evidence="driver-complete", rows=[r["id"] for r in coverage["expected"]])]
        condition = dict(image_os="fixture", image_version="1", runner="synthetic", caches={"BUSTER_CI_ZIG_CACHE_HIT": "false"})
        meta = dict(GITHUB_SHA="a" * 40, GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="1", ImageOS="fixture", ImageVersion="1", BUSTER_CI_RUNNER="synthetic", BUSTER_CI_ZIG_CACHE_HIT="false")
        summary = phases.analyze(directory, coverage, meta)
        tests = []
        for i, (row, cap) in enumerate(zip(coverage["expected"][:2], coverage["detected"][:2])):
            log = self.root / f"test{i}.log"
            log.write_text("TEST_MODULE_TIMING index=0 module=fixture duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n[2/2] Unit tests\n[1/1] Module tests\n[0/0] External tests\n")
            unit = dict(schema="buster-ci-unit-tests-measure-v1", arm="baseline", mode="serial", exit_code=0, test_workers=1, elapsed_us=20,
                        inventory=[dict(index=0, name="fixture", table_audit=False)], log=log.name,
                        identity=dict(source_revision="a" * 40, binary_sha256="e" * 64, runner_image={k: condition[k] for k in ("image_os", "image_version", "runner")},
                                      platform="windows", architecture="x86_64", configuration=row["configuration"], sanitize=row["sanitize"], fuzz=row["fuzz"], table_audits=False,
                                      toolchain={k: cap[k] for k in qualification.CAP_KEYS}, cpu_budget=4))
            tests.append(dict(row_id=row["id"], manifest=reference(self.root, f"unit{i}.json", unit), log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
        result = dict(success=True, metadata=meta, matrix_phases=summary)
        item = dict(job="Windows x86-64 checks", phase_directory=directory.name, tests=tests,
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
        after = qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")
        self.assertEqual(before, after)
        self.assertEqual(len(after["census"]), 2)
        item["tests"].pop()
        with self.assertRaisesRegex(ValueError, "runtime assertion census"):
            qualification.desktop(self.root, item, self.run_fixture(), condition, "combined-overlap")


if __name__ == "__main__":
    unittest.main()
