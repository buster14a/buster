#!/usr/bin/env python3
"""Scheduler failure, cancellation, admission and diagnostic controls (#2034)."""
import contextlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time
import unittest
from unittest import mock

import bootstrap_wrapper_cases as scheduler


class SchedulerTests(unittest.TestCase):
    def run_control(self, cases, jobs=2, ownership=None):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            summary = scheduler.run_cases(cases, jobs, output, ownership)
        return summary, output.getvalue()

    def test_same_case_identities_failures_and_order_at_both_budgets(self):
        class Controls(unittest.TestCase):
            def test_pass(self):
                self.assertTrue(True)

            def test_fail(self):
                self.fail("failure-marker")

            def test_error(self):
                raise RuntimeError("error-marker")

            def test_skip(self):
                self.skipTest("skip-marker")

        rows = []
        for jobs in (1, 2):
            cases = list(unittest.defaultTestLoader.loadTestsFromTestCase(Controls))
            summary, output = self.run_control(cases, jobs)
            self.assertFalse(summary["success"])
            self.assertEqual([row["test"] for row in summary["cases"]], [case.id() for case in cases])
            rows.append(summary["cases"])
            for marker in ("failure-marker", "error-marker", "skip-marker"):
                self.assertIn(marker, output)
        self.assertEqual(rows[0], rows[1])

    def test_two_independent_cases_overlap_and_publication_is_exclusive(self):
        active = []
        peak = []
        lock = threading.Lock()
        barrier = threading.Barrier(2, timeout=5)

        class Controls(unittest.TestCase):
            def exercise(self):
                with lock:
                    active.append(self.id())
                    peak.append(len(active))
                barrier.wait()
                with lock:
                    active.remove(self.id())

            def test_first(self):
                self.exercise()

            def test_second(self):
                self.exercise()

            def test_concurrent_cold_publication_uses_immutable_outputs(self):
                self.assertEqual(active, [])

        cases = list(unittest.defaultTestLoader.loadTestsFromTestCase(Controls))
        summary, _ = self.run_control(cases)
        self.assertTrue(summary["success"], summary)
        self.assertEqual(max(peak), 2)

    def test_existing_deadline_capture_and_early_cleanup_controls(self):
        cases = list(unittest.defaultTestLoader.loadTestsFromTestCase(scheduler.bootstrap.BootstrapProcessTests))
        for jobs in (1, 2):
            summary, _ = self.run_control([
                type(case)(case._testMethodName) for case in cases], jobs)
            self.assertTrue(summary["success"], summary)
            self.assertEqual(summary["unreaped_children"], 0)

    def test_failed_launch_cleans_previously_registered_children(self):
        class Control(unittest.TestCase):
            def test_launch(self):
                scheduler.bootstrap.WrapperProcess(self, [sys.executable, "-c", "import time; time.sleep(60)"],
                                                   Path.cwd(), os.environ.copy())
                scheduler.bootstrap.WrapperProcess(self, ["buster-missing-executable-2034"],
                                                   Path.cwd(), os.environ.copy())

        summary, output = self.run_control([Control("test_launch")])
        self.assertFalse(summary["success"])
        self.assertEqual(summary["owned_children"], 1)
        self.assertEqual(summary["unreaped_children"], 0)
        self.assertIn("FileNotFoundError", output)

    def test_cancellation_cleans_tree_before_fixture_removal(self):
        self.check_cancellation_tree()

    def test_cancellation_waits_for_delayed_readiness(self):
        # Exceed the former cancelling thread's independent five-second wait.
        self.check_cancellation_tree(readiness_delay=6)

    def test_cancellation_readiness_failure_retains_inner_diagnostics(self):
        with self.assertRaisesRegex(AssertionError, "readiness-failure-marker"):
            self.check_cancellation_tree(fail_readiness=True)

    def check_cancellation_tree(self, readiness_delay=0, fail_readiness=False):
        ownership = scheduler.CaseOwnership()
        launched = threading.Event()
        readiness_complete = threading.Event()
        requested = threading.Event()
        cancellation_errors = []
        directories = []
        children = []
        descendants = []

        class Control(unittest.TestCase):
            def test_child(self):
                import tempfile
                directory = tempfile.TemporaryDirectory()
                self.addCleanup(directory.cleanup)
                directories.append(directory.name)
                code = ("import subprocess, sys, time; from pathlib import Path; "
                        f"time.sleep({readiness_delay}); "
                        + ("sys.exit('readiness-failure-marker'); " if fail_readiness else "") +
                        "p=subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)']); "
                        "Path('descendant.pid').write_text(str(p.pid)); time.sleep(60)")
                child = scheduler.bootstrap.WrapperProcess(self,
                    [sys.executable, "-c", code], directory.name, os.environ.copy())
                children.append(child)
                marker = Path(directory.name) / "descendant.pid"
                try:
                    deadline = child.started + scheduler.bootstrap.WRAPPER_TIMEOUT_SECONDS
                    while (not marker.exists() and child.process.poll() is None and
                           time.monotonic() < deadline):
                        time.sleep(0.01)
                    if not marker.exists():
                        result = child.finish(timeout_seconds=0)
                        self.fail("child readiness failed: " + repr(result))
                    descendants.append(int(marker.read_text()))
                    launched.set()
                finally:
                    # Setup failure also releases the coordinator; it must not
                    # cancel a fixture whose child has already been cleaned.
                    readiness_complete.set()
                child.finish()

        def cancel():
            # Readiness uses the child's launch-relative deadline. An earlier
            # independent timer can silently expire during valid startup.
            readiness_complete.wait()
            if launched.is_set():
                requested.set()
                cancellation_errors.extend(ownership.cancel())

        thread = threading.Thread(target=cancel)
        thread.start()
        try:
            summary, output = self.run_control([Control("test_child")], ownership=ownership)
        finally:
            # Also release the coordinator if worker startup or child launch
            # failed before reaching the readiness try/finally.
            readiness_complete.set()
            thread.join(timeout=scheduler.bootstrap.CLEANUP_TIMEOUT_SECONDS)
        diagnostics = json.dumps(summary) + "\n" + output
        self.assertFalse(thread.is_alive())
        self.assertEqual(cancellation_errors, [], diagnostics)
        self.assertEqual(summary["unreaped_children"], 0, diagnostics)
        self.assertTrue(all(child.cleaned and child.stdout.closed and child.stderr.closed for child in children))
        self.assertTrue(all(not Path(path).exists() for path in directories))
        self.assertTrue(summary["cases"][0].get("success"), diagnostics)
        self.assertTrue(requested.is_set(), diagnostics)
        self.assertTrue(summary["cancelled"], diagnostics)
        self.assertFalse(summary["success"], diagnostics)
        self.assertEqual(len(descendants), 1)
        pid = descendants[0]
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
            kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
            handle = kernel.OpenProcess(0x100000, False, pid)
            if handle:
                try:
                    self.assertEqual(kernel.WaitForSingleObject(handle, 5000), 0)
                finally:
                    kernel.CloseHandle(handle)
            else:
                self.assertEqual(ctypes.get_last_error(), 87)  # Exited PID.
        else:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                pass
            else:
                # A killed orphan awaiting init's reap is no longer running.
                status = subprocess.run(["ps", "-p", str(pid), "-o", "stat="],
                                        capture_output=True, text=True, timeout=5)
                self.assertTrue(status.returncode == 1 or status.stdout.strip().startswith("Z"), status.stdout)

    def test_partial_worker_startup_failure_reports_and_cleans(self):
        launched = threading.Event()

        class Control(unittest.TestCase):
            def test_child(self):
                child = scheduler.bootstrap.WrapperProcess(self,
                    [sys.executable, "-c", "import time; time.sleep(60)"], Path.cwd(), os.environ.copy())
                launched.set()
                child.finish()

        start = threading.Thread.start
        starts = 0

        def fail_second(thread):
            nonlocal starts
            starts += 1
            if starts == 1:
                start(thread)
                self.assertTrue(thread.is_alive())
            else:
                raise RuntimeError("worker-startup-marker")

        with mock.patch.object(threading.Thread, "start", fail_second):
            summary, output = self.run_control([Control("test_child")])
        self.assertFalse(summary["success"])
        self.assertTrue(summary["cancelled"])
        self.assertEqual(summary["unreaped_children"], 0)
        self.assertIn("worker-startup-marker", output)

    @unittest.skipIf(os.name == "nt", "POSIX signal injection; cancellation ownership is checked on Windows")
    def test_signal_during_worker_registration_leaves_no_fixture_or_child(self):
        start = threading.Thread.start

        def interrupted_start(thread):
            start(thread)
            signal.raise_signal(signal.SIGTERM)

        with mock.patch.object(threading.Thread, "start", interrupted_start):
            summary, _ = self.run_control(scheduler.behavior_cases())
        self.assertTrue(summary["cancelled"])
        self.assertFalse(summary["success"])
        self.assertEqual(summary["owned_children"], 0)

    def test_cleanup_error_cannot_be_a_success(self):
        cleanup = scheduler.CaseProcess.cleanup

        def fail_cleanup(child):
            cleanup(child)
            raise RuntimeError("cleanup-error-marker")

        class Control(unittest.TestCase):
            def test_child(self):
                scheduler.bootstrap.WrapperProcess(self,
                    [sys.executable, "-c", "import time; time.sleep(60)"], Path.cwd(), os.environ.copy())

        with mock.patch.object(scheduler.CaseProcess, "cleanup", fail_cleanup):
            summary, output = self.run_control([Control("test_child")])
        self.assertFalse(summary["success"])
        self.assertEqual(summary["unreaped_children"], 0)
        self.assertIn("cleanup-error-marker", output)

    @unittest.skipIf(os.name == "nt", "Windows has no POSIX SIGTERM delivery; ownership cancellation is covered above")
    def test_sigterm_fails_and_reaps_the_child_tree(self):
        code = '''
import os, sys, unittest
from pathlib import Path
sys.path.insert(0, 'tools')
import bootstrap_wrapper_cases as s
import signal
signal.signal(signal.SIGTERM, signal.default_int_handler)
class Control(unittest.TestCase):
    def test_child(self):
        child = s.bootstrap.WrapperProcess(self, [sys.executable, '-c', 'import time; time.sleep(60)'],
                                          Path.cwd(), os.environ.copy())
        child.finish()
summary = s.run_cases([Control('test_child')], jobs=2)
sys.exit(0 if summary['success'] else 1)
'''
        process = subprocess.Popen([sys.executable, "-u", "-c", code],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.addCleanup(process.stdout.close)
        self.addCleanup(process.kill if process.poll() is None else lambda: None)
        line = process.stdout.readline()
        self.assertTrue(line.startswith("BOOTSTRAP_TEST"), line)
        line = process.stdout.readline()
        self.assertTrue(line.startswith("BOOTSTRAP_PROCESS"), line)
        process.send_signal(signal.SIGTERM)
        output, _ = process.communicate(timeout=10)
        self.assertNotEqual(process.returncode, 0)
        records = [json.loads(line.split(" ", 1)[1]) for line in output.splitlines()
                   if line.startswith("BOOTSTRAP_CASE_SUMMARY ")]
        self.assertEqual(len(records), 1, output)
        self.assertTrue(records[0]["cancelled"])
        self.assertEqual(records[0]["unreaped_children"], 0)

    def test_line_capture_keeps_concurrent_json_records_parseable(self):
        output = io.StringIO()
        lines = scheduler.LineOutput(output)
        barrier = threading.Barrier(2, timeout=5)

        def emit(index):
            for _ in range(50):
                lines.write(json.dumps({"lane": index}))
                barrier.wait()
                lines.write("\n")

        workers = [threading.Thread(target=emit, args=(i,)) for i in range(2)]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(len(records), 100)
        self.assertEqual(sum(record["lane"] for record in records), 50)

    def test_empty_duplicate_and_unbounded_admission_fail(self):
        cases = scheduler.behavior_cases()
        for selected, jobs in (([], 1), ([cases[0], cases[0]], 2), (cases, 3)):
            with self.assertRaises(ValueError):
                self.run_control(selected, jobs)

    def test_routine_ci_keeps_release_ownership_and_requires_scheduler_controls(self):
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/ci.yml").read_text()
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        policy = desktop.split("- name: Workflow tool regression tests", 1)[1].split("- name: Bootstrap wrapper regression tests", 1)[0]
        wrappers = desktop.split("- name: Bootstrap wrapper regression tests", 1)[1].split("- name: Install mold", 1)[0]
        self.assertIn("matrix.shard == 'release'", policy)
        self.assertIn("tools/bootstrap_wrapper_cases_test.py=bootstrap-case-controls.log", policy)
        self.assertIn('if [[ "$BUSTER_MATRIX_SHARD" != release ]]', wrappers)
        self.assertIn('if [[ "$RUNNER_OS" == Windows ]]', wrappers)
        self.assertIn("tools/bootstrap_wrapper_cases.py --jobs 2", wrappers)
        self.assertIn("tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v", wrappers)
        self.assertIn("set -euo pipefail", wrappers)
        self.assertIn("timeout-minutes: ${{ matrix.platform == 'windows' && 20 || 2 }}", wrappers)
        self.assertNotIn("continue-on-error", wrappers)


if __name__ == "__main__":
    unittest.main()
