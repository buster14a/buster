#!/usr/bin/env python3
"""Regressions for tools/ci_workflow_tools.py, the concurrent suite runner."""
import io
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_workflow_tools as runner

# A fixture suite waits for every named peer's marker. Deadlines only bound a
# broken runner; a correct one never waits for them.
SUITE = """import os, pathlib, sys, time
name = pathlib.Path(__file__).stem
print('SUITE fixture ' + name + ' argv=' + ' '.join(sys.argv[1:]))
pathlib.Path(name + '.started').touch()
deadline = time.monotonic() + 30
for peer in {peers!r}:
    while not pathlib.Path(peer + '.started').exists():
        if time.monotonic() > deadline:
            print('peer never started: ' + peer)
            sys.exit(3)
        time.sleep(0.01)
time.sleep({sleep})
print('SUITE done ' + name)
sys.exit({status})
"""


class RunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.logs = self.root / "logs"

    def suite(self, name, status=0, peers=(), sleep=0):
        (self.root / (name + ".py")).write_text(SUITE.format(peers=list(peers), status=status, sleep=sleep),
                                                encoding="utf-8")
        return name + ".py", name + ".log"

    def run_suites(self, suites, jobs, heartbeat=30):
        output = io.StringIO()
        status = runner.run(suites, self.logs, jobs, cwd=self.root, output=output, heartbeat=heartbeat)
        return status, output.getvalue()

    def test_lanes_run_concurrently_and_every_log_is_recorded(self):
        # a and b can only finish if they run at the same time; c needs a lane
        # to be freed, so it also proves the queue drains past the lane count.
        suites = [self.suite("a", peers=["b"]), self.suite("b", peers=["a"]), self.suite("c")]
        status, text = self.run_suites(suites, jobs=2)
        self.assertEqual(status, 0, text)
        self.assertIn("WORKFLOW_TOOLS_START suites=3 jobs=2", text)
        for suite, log in suites:
            name = suite[:-3]
            self.assertIn(f"SUITE fixture {name} argv=-v", (self.logs / log).read_text())
            self.assertRegex(text, rf"SUITE_END path={re.escape(suite)} result=success elapsed_seconds=\d+ ")
            self.assertRegex(text, rf"SUITE_DURATION seconds=\d+ result=success path={re.escape(suite)}\n")
        self.assertRegex(text, r"WORKFLOW_TOOLS_END result=success elapsed_seconds=\d+ ")

    def test_each_suite_log_is_one_block_before_its_end_line(self):
        suites = [self.suite("a", peers=["b"]), self.suite("b", peers=["a"])]
        status, text = self.run_suites(suites, jobs=2)
        self.assertEqual(status, 0, text)
        for name in ("a", "b"):
            block = re.search(rf"SUITE fixture {name} argv=-v\nSUITE done {name}\nSUITE_END path={name}\.py ", text)
            self.assertIsNotNone(block, text)

    def test_failure_runs_every_suite_and_returns_first_failure_in_order(self):
        suites = [self.suite("a"), self.suite("b", status=7), self.suite("c"), self.suite("d", status=5)]
        status, text = self.run_suites(suites, jobs=1)
        self.assertEqual(status, 7, text)
        self.assertEqual(re.findall(r"SUITE_START path=(\S+)", text), ["a.py", "b.py", "c.py", "d.py"])
        self.assertIn("SUITE_END path=b.py result=failure status=7 ", text)
        self.assertIn("SUITE_END path=d.py result=failure status=5 ", text)
        self.assertIn("SUITE done c", (self.logs / "c.log").read_text())
        self.assertIn("WORKFLOW_TOOLS_END result=failure failed=b.py,d.py ", text)

    def test_missing_suite_fails_without_hiding_the_others(self):
        suites = [("missing.py", "missing.log"), self.suite("a")]
        status, text = self.run_suites(suites, jobs=2)
        self.assertNotEqual(status, 0, text)
        self.assertIn("SUITE_END path=missing.py result=failure", text)
        self.assertIn("SUITE_END path=a.py result=success", text)

    def test_heartbeat_names_running_suites(self):
        suites = [self.suite("a", sleep=1)]
        status, text = self.run_suites(suites, jobs=1, heartbeat=0.2)
        self.assertEqual(status, 0, text)
        self.assertRegex(text, r"SUITE_RUNNING path=a\.py elapsed_seconds=\d+\n")

    def test_statuses_fit_a_shell_exit_code(self):
        self.assertEqual(runner.exit_status(7), 7)
        self.assertEqual(runner.exit_status(-9), 1)
        self.assertEqual(runner.exit_status(0xC0000005), 1)


class CommandLineTests(unittest.TestCase):
    def command(self, *arguments, environment=None):
        return subprocess.run([sys.executable, str(ROOT / "tools/ci_workflow_tools.py"), *arguments],
                              cwd=ROOT, env=dict(os.environ, **(environment or {})),
                              capture_output=True, text=True, timeout=60)

    def test_rejects_malformed_duplicate_and_empty_arguments(self):
        with tempfile.TemporaryDirectory() as temporary:
            for arguments in (["tools/a.py"], ["tools/a.py="], ["=a.log"], ["tools/a.py=sub/a.log"],
                              ["tools/a.py=a.log", "tools/a.py=b.log"], ["tools/a.py=a.log", "tools/b.py=a.log"],
                              ["--jobs", "0", "tools/a.py=a.log"], []):
                with self.subTest(arguments=arguments):
                    result = self.command("--log-directory", temporary, *arguments)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            result = self.command("--log-directory", temporary, "tools/a.py=a.log", environment={"BUSTER_TEST_JOBS": "x"})
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_jobs_default_honors_buster_test_jobs(self):
        self.assertEqual(runner.default_jobs({}), runner.DEFAULT_JOBS)
        self.assertEqual(runner.default_jobs({"BUSTER_TEST_JOBS": "5"}), 5)
        self.assertEqual(runner.default_jobs({"BUSTER_TEST_JOBS": "-1"}), 0)


if __name__ == "__main__":
    unittest.main()
