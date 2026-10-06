#!/usr/bin/env python3
"""Native same-binary qualification receipts; fixtures compile no ide sources."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_unit_tests_campaign as campaign
FAKE_IDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define PRIMARY_MODULE "c_frontend_tests"
#else
#define PRIMARY_MODULE "compiler_driver_tests"
#endif
int main(int argc, char** argv)
{
    const char* group = getenv("BUSTER_TEST_MODULE_GROUP");
    const char* mode = getenv("OBSERVATION_FIXTURE_MODE");
    int inventory = group && !strcmp(group, "inventory");
    int primary = group && !strcmp(group, "primary");
    int rest = group && !strcmp(group, "rest");
    int result = 0;
    if (argc < 2 || strcmp(argv[1], "test")) { result = 6; }
    else if (inventory && mode && !strcmp(mode, "query-failure")) { result = 8; }
    else
    {
        if (inventory && mode && !strcmp(mode, "query-hang"))
        {
#ifdef _WIN32
            Sleep(5000);
#else
            sleep(5);
#endif
        }
        if (inventory && mode && !strcmp(mode, "query-overflow"))
        {
            for (int i = 0; i < 32768; ++i) { puts("inventory-overflow"); }
        }
        if (inventory || primary || rest)
        {
            printf("CI_UNIT_MODULE_V1 index=0 module=" PRIMARY_MODULE " table_audit=0 enabled=1 selected=%d group=primary\n", primary);
            printf("CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected=%d group=rest\n", rest);
        }
        if (inventory)
        {
            if (!getenv("BUSTER_TEST_TABLE_AUDITS") || strcmp(getenv("BUSTER_TEST_TABLE_AUDITS"), "0")) { result = 9; }
            puts("CI_UNIT_BATCH_V1 group=inventory modules=0 modules_passed=0 assertions=0 passed=0 failed=0 external=0 external_passed=0 status=inventory");
            puts("[0/0] Unit tests (0 of 2 modules selected)\n[0/0] Module tests\n[0/0] External tests");
        }
        else
        {
            if (!rest) { puts("TEST_MODULE_TIMING index=0 module=" PRIMARY_MODULE " duration_ns=1 passed=2 failed=0 assertions=2 status=pass"); }
            if (!primary) { puts("TEST_MODULE_TIMING index=1 module=other_tests duration_ns=1 passed=2 failed=0 assertions=2 status=pass"); }
            if (primary || rest)
            {
                printf("CI_UNIT_BATCH_V1 group=%s modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n", group);
                puts("[2/2] Unit tests (1 of 2 modules selected)\n[1/1] Module tests\n[0/0] External tests");
            }
            else { puts("[4/4] Unit tests\n[2/2] Module tests\n[0/0] External tests"); }
            puts("OBSERVATION_ACTUAL_TEST");
            fprintf(stderr, "OBSERVATION_STDERR\n");
            if (mode && !strcmp(mode, "test-overflow"))
            {
                for (int i = 0; i < 32768; ++i) { puts("test-overflow"); }
            }
            if (mode && !strcmp(mode, "test-failure")) { result = 7; }
        }
    }
    return result;
}
'''


class MatrixUnitObservationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(ignore_cleanup_errors=os.name == "nt")
        cls.directory = Path(cls.temporary.name)
        compiler = shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            raise RuntimeError("native observer controls require a C compiler")
        suffix = ".exe" if os.name == "nt" else ""
        cls.driver = cls.directory / ("driver" + suffix)
        cls.ide = cls.directory / ("fixture-ide" + suffix)
        source = cls.directory / "driver.c"
        phase = (ROOT / "tools/matrix_phase.c").read_text()
        phase = phase.replace("#define MATRIX_UNIT_INVENTORY_LIMIT BUSTER_MB(1)", "#define MATRIX_UNIT_INVENTORY_LIMIT BUSTER_KB(8)")
        phase = phase.replace("#define MATRIX_UNIT_LOG_LIMIT BUSTER_MB(64)", "#define MATRIX_UNIT_LOG_LIMIT BUSTER_KB(64)")
        phase = phase.replace("#define MATRIX_UNIT_QUERY_TIMEOUT_US (30ull * 1000000ull)", "#define MATRIX_UNIT_QUERY_TIMEOUT_US 100000ull")
        native = (ROOT / "build.c").read_text().replace('#include "tools/matrix_phase.c"', phase)
        source.write_text(native)
        command = [compiler, "-Isrc", "-I.", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
                   "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", str(source), "-o", str(cls.driver)]
        if os.name == "nt":
            command.append("-lws2_32")
            if "clang" in Path(compiler).name:
                command.append("-Wno-microsoft-enum-forward-reference")
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)
        fake = cls.directory / "ide.c"
        fake.write_text(FAKE_IDE)
        subprocess.run([compiler, str(fake), "-o", str(cls.ide)], cwd=ROOT, check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_worker(self, *, enabled=True, partitioned=False, mode="", collision=False):
        directory = Path(tempfile.mkdtemp(dir=self.directory))
        root = directory / "matrix-phases"
        root.mkdir()
        (root / "plan.json").write_text("{}")
        task = "clang-tree-test-Release"
        observation = directory / "unit-observations" / task
        if collision:
            observation.mkdir(parents=True)
            (observation / "observation.json").write_text("immutable-earlier-proof")
        argv = [str(self.driver), "test_units_partitioned", str(self.ide)] if partitioned else [str(self.ide), "test", "--verbose=1", "--ci=1"]
        environment = dict(os.environ, BUSTER_CI_CHECKS_EVIDENCE="1" if enabled else "0",
                           GITHUB_SHA="a" * 40, GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="1",
                           BUSTER_TEST_SOURCE_REVISION="a" * 40, BUSTER_TEST_JOBS="4" if partitioned else "2",
                           BUSTER_TEST_TABLE_AUDITS="1", OBSERVATION_FIXTURE_MODE=mode)
        result = subprocess.run([str(self.driver), "matrix_phase_run", str(root), task, "1", "0", "--", *argv],
                                cwd=ROOT, env=environment, capture_output=True, timeout=30)
        end = json.loads(next(root.glob("*.end.json")).read_text())
        return result, end, observation, argv

    def test_direct_serial_and_real_nested_partitions_bind_observed_bytes(self):
        for partitioned in (False, True):
            with self.subTest(partitioned=partitioned):
                result, end, directory, argv = self.run_worker(partitioned=partitioned)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                receipt = json.loads((directory / "observation.json").read_text())
                self.assertEqual(receipt["schema"], "buster-desktop-unit-observation-v1")
                self.assertEqual((receipt["id"], receipt["pid"], receipt["epoch_us"], receipt["argv"]),
                                 (end["id"], end["pid"], end["epoch_us"], argv))
                self.assertEqual((receipt["source_revision"], receipt["run_id"], receipt["run_attempt"]), ("a" * 40, "123", "1"))
                self.assertEqual(receipt["binary_sha256"], hashlib.sha256(self.ide.read_bytes()).hexdigest())
                self.assertEqual(receipt["binary_bytes"], self.ide.stat().st_size)
                self.assertTrue(receipt["binary_unchanged"])
                self.assertTrue(receipt["capture_complete"])
                self.assertEqual(receipt["test_result"], 0)
                for field, filename in (("inventory_sha256", "inventory.log"), ("log_sha256", "test.log")):
                    self.assertEqual(receipt[field], hashlib.sha256((directory / filename).read_bytes()).hexdigest())
                self.assertIn(b"selected=0", (directory / "inventory.log").read_bytes())
                self.assertEqual(len(campaign.inventory(directory / "inventory.log")), 2)
                self.assertIn(b"OBSERVATION_ACTUAL_TEST", result.stdout)
                if partitioned:
                    self.assertIn(b"CI_UNIT_PARTITION_V1 groups=2 workers=4", (directory / "test.log").read_bytes())
                else:
                    self.assertIn(b"OBSERVATION_STDERR", result.stderr)

    def test_disabled_capture_preserves_direct_streams_and_has_no_sidecars(self):
        result, end, directory, _ = self.run_worker(enabled=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(end["state"], "success")
        self.assertFalse(directory.exists())
        self.assertIn(b"OBSERVATION_ACTUAL_TEST", result.stdout)
        self.assertIn(b"OBSERVATION_STDERR", result.stderr)

    def test_query_failure_and_overflow_fail_journal_but_preserve_actual_execution(self):
        for mode in ("query-failure", "query-overflow", "query-hang"):
            with self.subTest(mode=mode):
                result, end, directory, _ = self.run_worker(mode=mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(end["state"], "failure")
                self.assertIn(b"OBSERVATION_ACTUAL_TEST", (directory / "test.log").read_bytes())
                self.assertFalse(json.loads((directory / "observation.json").read_text())["binary_unchanged"])

    def test_actual_failure_and_capture_overflow_cannot_be_green(self):
        for mode in ("test-failure", "test-overflow"):
            with self.subTest(mode=mode):
                result, end, directory, _ = self.run_worker(mode=mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(end["state"], "failure")
                receipt = json.loads((directory / "observation.json").read_text())
                if mode == "test-failure":
                    self.assertNotEqual(receipt["test_result"], 0)
                else:
                    self.assertFalse(receipt["capture_complete"])

    def test_earlier_receipt_cannot_be_replaced(self):
        result, end, directory, _ = self.run_worker(collision=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(end["state"], "failure")
        self.assertEqual((directory / "observation.json").read_text(), "immutable-earlier-proof")
        self.assertFalse((directory / "test.log").exists())


if __name__ == "__main__":
    unittest.main()
