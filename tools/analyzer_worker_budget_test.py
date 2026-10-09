#!/usr/bin/env python3
"""Native qualification controls and independent evidence-tampering tests."""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
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
        cls.clean = None
        cls.verification_error = None
        try:
            cls.clean = budget.verify(cls.evidence)
        except ValueError as error:
            cls.verification_error = str(error)
            if "process-tree sampling is" not in cls.verification_error:
                raise
        cls.alias_evidence = cls.root / "alias-evidence"
        cls.alias_evidence.mkdir()
        for name in ("host-before.json", "host-after.json"):
            (cls.alias_evidence / name).write_text("{}\n")
        cls.alias_directory = cls.root / "alias-source"
        cls.alias_directory.mkdir()
        (cls.alias_directory / "alias.c").write_text("int alias_value(void) { return 1; }\n")
        clang_path = shutil.which("clang")
        if not clang_path:
            raise RuntimeError("Clang is required for the real-context alias reader control")
        clang = str(Path(clang_path).resolve())
        cls.alias_database = cls.root / "alias-compile-commands.json"
        outputs = ("obj/Release/a-alias.o", "obj/Release/z-alias.o")
        rows = [{"directory": str(cls.alias_directory), "file": "alias.c", "output": output,
                 "arguments": [clang, "-c", "alias.c", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-o", output]}
                for output in outputs]
        cls.alias_database.write_text(json.dumps(rows))
        # Issue #3130's real-Clang alias proof is intentionally expensive in
        # the canonical unoptimized test driver; the matched O0 four-arm
        # fixture measured 131.190 seconds for the candidate. Keep this
        # reader-fixture allowance above that measurement without changing
        # analyzer, TU, workflow, or production limits.
        alias_result = subprocess.run([str(cls.driver), "clang_analyze", str(cls.alias_database), "--config", "Release",
                                       "--shards", "4", "--timeout", "2", "--quiet", "--qualify-workers",
                                       "--results", str(cls.alias_evidence / "campaign")], cwd=REPOSITORY, text=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        if alias_result.returncode:
            raise RuntimeError(alias_result.stdout)
        cls.alias_clean = None
        cls.alias_verification_error = None
        try:
            cls.alias_clean = budget.verify(cls.alias_evidence)
        except ValueError as error:
            cls.alias_verification_error = str(error)
            if "process-tree sampling is" not in cls.alias_verification_error:
                raise

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def write_database(cls, mode):
        rows = [{"directory": str(cls.root), "file": name, "output": f"obj/Release/{name}.o",
                 "arguments": [str(cls.fixture), mode if i == 0 else "-DFIXTURE_OK", "-DFIXTURE_DELAY", "-c", name,
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
        if self.clean is None:
            self.skipTest("host process-tree sampling is not complete")
        samples = self.clean["samples"]
        self.assertEqual([s["jobs"] for s in samples], [2, 4, 4, 2])
        self.assertEqual([s["eligible"] for s in samples], [4] * 4)
        self.assertEqual(len({s["inventory_sha256"] for s in samples}), 1)

    def test_relocated_artifact_replays(self):
        if self.clean is None:
            self.skipTest("host process-tree sampling is not complete")
        self.assertEqual(budget.verify(self.copy)["diagnostic_sha256"], self.clean["diagnostic_sha256"])

    def test_missing_sample_fails(self):
        if self.clean is None:
            self.skipTest("host process-tree sampling is not complete")
        shutil.rmtree(self.copy / "campaign/sample-2-jobs-4")
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_changed_diagnostic_fails(self):
        if self.clean is None:
            self.skipTest("host process-tree sampling is not complete")
        log = next((self.copy / "campaign/sample-1-jobs-4").glob("shard-*/unit-*.log"))
        log.write_bytes(b"altered diagnostic\n")
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_exact_alias_keeps_both_rows_in_the_budget_reader(self):
        data, _, _, _, selected = budget.inventory(self.alias_evidence / "campaign/sample-0-jobs-2/manifest.txt")
        self.assertEqual((selected[6], selected[7]), (1, 1) if sys.platform == "linux" else (2, 0))
        self.assertEqual([row[1] for row in selected[-1]], [0, 0] if sys.platform == "linux" else [0, 1])
        if self.alias_clean is not None and sys.platform == "linux":
            self.assertEqual([sample["eligible"] for sample in self.alias_clean["samples"]], [2] * 4)

    def test_alias_log_must_match_representative_even_with_updated_digest(self):
        copy = self.root / f"{self._testMethodName}-alias"
        shutil.copytree(self.alias_evidence, copy)
        _, _, _, _, selected = budget.inventory(copy / "campaign/sample-0-jobs-2/manifest.txt")
        units = selected[-1]
        aliases = [index for index, row in enumerate(units) if row[1] != index]
        if not aliases:
            self.skipTest("context proof is unavailable on this platform; rows execute independently")
        alias = aliases[0]
        representative = units[alias][1]
        shard = units[alias][0]
        directory = copy / "campaign/sample-0-jobs-2" / f"shard-{shard}"
        alias_log = directory / f"unit-{alias}.log"
        alias_log.write_bytes((directory / f"unit-{representative}.log").read_bytes() + b"tampered alias\n")
        report = directory / "result.txt"
        lines = report.read_text().splitlines()
        row_ordinal = next(ordinal for ordinal in range(int(lines[3])) if int(lines[8 + ordinal * 6]) == alias)
        lines[8 + row_ordinal * 6 + 5] = budget.digest(alias_log.read_bytes())
        report.write_text("\n".join(lines) + "\n")
        with self.assertRaisesRegex(ValueError, "alias diagnostics differ from representative"):
            budget.verify(copy)

    def test_missing_terminal_result_fails(self):
        if self.clean is None:
            self.skipTest("host process-tree sampling is not complete")
        next((self.copy / "campaign/sample-1-jobs-4").glob("shard-*/result.txt")).unlink()
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_wrong_worker_metrics_fail(self):
        if self.clean is None:
            self.skipTest("host process-tree sampling is not complete")
        path = self.copy / "campaign/sample-1-jobs-4/run.txt"
        path.write_text(path.read_text().replace("jobs=4", "jobs=2"))
        with self.assertRaises(ValueError):
            budget.verify(self.copy)

    def test_process_tree_status_is_explicit_and_fail_closed(self):
        run_paths = sorted((self.copy / "campaign").glob("sample-*/run.txt"))
        self.assertEqual(len(run_paths), 4)
        statuses = []
        for path in run_paths:
            run = budget.fields(path.read_text().strip(), "ANALYZE_RUN")
            tree_status = run.get("process_tree_status")
            self.assertIn(tree_status, ("complete", "incomplete", "unavailable"))
            self.assertRegex(run.get("process_tree_reason", ""), r"^[a-z0-9-]+$")
            if tree_status != "unavailable":
                self.assertGreater(int(run["samples"]), 0)
                self.assertGreater(int(run["sampled_peak_tree_rss_bytes"]), 0)
            statuses.append(run["process_tree_status"])
        if self.clean is None:
            self.assertTrue(any(status != "complete" for status in statuses), self.verification_error)
            with self.assertRaisesRegex(ValueError, "process-tree sampling"):
                budget.verify(self.copy)
        else:
            self.assertEqual(statuses, ["complete"] * 4)
            self.assertTrue(all(budget.fields(path.read_text().strip(), "ANALYZE_RUN")["process_tree_reason"] == "none"
                                for path in run_paths))
            path = run_paths[0]
            text = path.read_text()
            text = re.sub(r"process_tree_status=complete process_tree_reason=none",
                          "process_tree_status=incomplete process_tree_reason=children-unavailable", text, count=1)
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, "process-tree sampling is incomplete"):
                budget.verify(self.copy)

    def test_missing_process_tree_status_fails(self):
        path = self.copy / "campaign/sample-0-jobs-2/run.txt"
        text = path.read_text()
        text = re.sub(r" process_tree_status=[a-z]+ process_tree_reason=[a-z0-9-]+", "", text, count=1)
        path.write_text(text)
        with self.assertRaisesRegex(ValueError, "missing/invalid process-tree sampling status"):
            budget.verify(self.copy)

    def test_invalid_process_tree_status_fails(self):
        path = self.copy / "campaign/sample-0-jobs-2/run.txt"
        text = path.read_text()
        text = re.sub(r"process_tree_status=[a-z]+", "process_tree_status=unknown", text, count=1)
        path.write_text(text)
        with self.assertRaisesRegex(ValueError, "missing/invalid process-tree sampling status"):
            budget.verify(self.copy)

    def test_failed_native_arm_keeps_all_four_samples(self):
        self.write_database("-DFIXTURE_WARNING")
        try:
            destination = self.root / "failed"
            result = self.run_campaign(destination)
            self.assertNotEqual(result.returncode, 0)
            # A real failure is never qualified as an expected self-test rejection.
            self.assertIn("\nerror: analyzer shard=", result.stdout)
            self.assertNotIn("expected-error:", result.stdout)
            self.assertNotIn("expected=1", result.stdout)
            self.assertIn("ANALYZE_WORKER_SAMPLE sample=0 jobs=2", result.stdout)
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

    def test_self_test_scopes_expected_rejections(self):
        result = subprocess.run([str(self.driver), "clang_analyze", "--self-test"], cwd=REPOSITORY, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout)
        lines = result.stdout.splitlines()
        scope = None
        qualified = 0
        for line in lines[:-1]:
            if line.startswith("ANALYZE_SELF_TEST_BEGIN "):
                self.assertIsNone(scope, line)
                scope = budget.fields(line, "ANALYZE_SELF_TEST_BEGIN")
                self.assertIn(scope["expect"], ("accept", "reject"))
            elif line.startswith("ANALYZE_SELF_TEST "):
                verdict = budget.fields(line, "ANALYZE_SELF_TEST")
                self.assertEqual(verdict["status"], "pass", line)
                self.assertTrue(scope is None or scope["name"] == verdict["name"], line)
                scope = None
            else:
                # No unqualified failure may appear in a passing self-test,
                # and qualified ones only inside an announced rejection scope.
                self.assertFalse(line.startswith("error:"), line)
                status = re.search(r"^ANALYZE_[A-Z_]+ .*\bstatus=([a-z-]+)", line)
                expected = line.startswith("expected-error:") or line.endswith(" expected=1") or " expected=1 " in line
                self.assertTrue(status is None or status.group(1) == "pass" or expected, line)
                if expected:
                    self.assertTrue(scope is not None and scope["expect"] == "reject", line)
                    qualified += 1
        self.assertIsNone(scope)
        summary = budget.fields(lines[-1], "ANALYZE_SELF_TEST_RESULT")
        self.assertEqual((summary["status"], summary["failures"]), ("pass", "0"))
        self.assertGreater(int(summary["expected_rejections"]), 0)
        self.assertGreater(qualified, 0)

    def test_existing_campaign_cannot_be_reused(self):
        self.assertNotEqual(self.run_campaign(self.evidence / "campaign").returncode, 0)


if __name__ == "__main__":
    unittest.main()
