#!/usr/bin/env python3
"""Native qualification controls and independent evidence-tampering tests."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("budget", Path(__file__).with_name("analyzer_worker_budget.py"))
budget = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(budget)
REPOSITORY = Path(__file__).resolve().parent.parent


class WorkerBudgetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="buster-worker-budget-")
        cls.root = Path(cls.temporary.name)
        cls.driver = Path(os.environ.get("BUSTER_ANALYZER_TEST_DRIVER", REPOSITORY / "build/analyzer-driver")).resolve()
        if not cls.driver.is_file():
            raise RuntimeError("build the native analyzer driver before running these tests")
        cls.fixture = cls.root / "fixture"
        subprocess.run(["clang", str(REPOSITORY / "tools/clang_analyze_fixture.c"), "-o", str(cls.fixture)], check=True)
        cls.database = cls.root / "compile_commands.json"
        cls.write_database("-DFIXTURE_OK")
        cls.evidence = cls.root / "evidence"
        cls.evidence.mkdir()
        for name in ("host-before.json", "host-after.json"):
            (cls.evidence / name).write_text("{}\n")
        result = cls.run_campaign(cls.evidence / "campaign")
        if result.returncode:
            raise RuntimeError(result.stdout)
        cls.clean = budget.verify(cls.evidence)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def write_database(cls, mode):
        rows = [{"directory": str(cls.root), "file": name, "output": f"obj/Release/{name}.o",
                 "arguments": [str(cls.fixture), mode if i == 0 else "-DFIXTURE_OK", "-c", name,
                               "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-o", f"obj/Release/{name}.o"]}
                for i, name in enumerate(("alpha.c", "alpha_test.c", "beta.c", "gamma.c"))]
        cls.database.write_text(json.dumps(rows))

    @classmethod
    def run_campaign(cls, result, *extra):
        return subprocess.run([str(cls.driver), "clang_analyze", str(cls.database), "--config", "Release",
                               "--shards", "4", "--timeout", "2", "--quiet", "--qualify-workers",
                               "--results", str(result), *extra], cwd=REPOSITORY, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)

    def setUp(self):
        self.copy = self.root / self._testMethodName
        shutil.copytree(self.evidence, self.copy)

    def test_counterbalanced_complete_inventory(self):
        samples = self.clean["samples"]
        self.assertEqual([s["jobs"] for s in samples], [2, 4, 4, 2])
        self.assertEqual([s["eligible"] for s in samples], [4] * 4)
        self.assertEqual(len({s["inventory_sha256"] for s in samples}), 1)

    def test_relocated_artifact_replays(self):
        self.assertEqual(budget.verify(self.copy)["diagnostic_sha256"], self.clean["diagnostic_sha256"])

    def test_missing_sample_fails(self):
        shutil.rmtree(self.copy / "campaign/sample-2-jobs-4")
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_changed_diagnostic_fails(self):
        log = next((self.copy / "campaign/sample-1-jobs-4").glob("shard-*/unit-*.log"))
        log.write_bytes(b"altered diagnostic\n")
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_missing_terminal_result_fails(self):
        next((self.copy / "campaign/sample-1-jobs-4").glob("shard-*/result.txt")).unlink()
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_wrong_worker_metrics_fail(self):
        path = self.copy / "campaign/sample-1-jobs-4/run.txt"
        path.write_text(path.read_text().replace("jobs=4", "jobs=2"))
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_failed_native_arm_keeps_all_four_samples(self):
        self.write_database("-DFIXTURE_WARNING")
        try:
            destination = self.root / "failed"
            result = self.run_campaign(destination)
            self.assertNotEqual(result.returncode, 0)
            lines = (destination / "qualification.txt").read_text().splitlines()
            self.assertEqual(len(lines), 5)
            self.assertTrue(all("status=fail" in line for line in lines[1:]))
            self.assertEqual(len(list(destination.glob("sample-*/shard-*/unit-*.log"))), 16)
        finally:
            self.write_database("-DFIXTURE_OK")

    def test_incompatible_modes_fail_before_launch(self):
        for index, extra in enumerate((("--prepare",), ("--aggregate",), ("--shard", "0"),
                                       ("--baseline-driver", str(self.driver)), ("--self-test",), ("--jobs", "1"))):
            destination = self.root / f"invalid-{index}"
            result = self.run_campaign(destination, *extra)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(destination.exists())

    def test_existing_campaign_cannot_be_reused(self):
        self.assertNotEqual(self.run_campaign(self.evidence / "campaign").returncode, 0)


if __name__ == "__main__":
    unittest.main()
