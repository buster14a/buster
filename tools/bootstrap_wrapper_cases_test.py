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
import bootstrap_absolute_paths_test


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

    def test_independent_cases_fill_every_lane_and_publication_is_exclusive(self):
        for jobs in (2, scheduler.MAX_JOBS):
            active = []
            peak = []
            lock = threading.Lock()
            barrier = threading.Barrier(jobs, timeout=5)

            def exercise(case):
                with lock:
                    active.append(case.id())
                    peak.append(len(active))
                barrier.wait()
                with lock:
                    active.remove(case.id())

            def publication(case):
                case.assertEqual(active, [])

            methods = {"test_case_%d" % index: exercise for index in range(jobs)}
            methods["test_concurrent_cold_publication_uses_immutable_outputs"] = publication
            Controls = type("Controls", (unittest.TestCase,), methods)
            cases = list(unittest.defaultTestLoader.loadTestsFromTestCase(Controls))
            with self.subTest(jobs=jobs):
                summary, _ = self.run_control(cases, jobs)
                self.assertTrue(summary["success"], summary)
                self.assertEqual(max(peak), jobs)

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
                missing = scheduler.bootstrap.WrapperProcess(self, ["buster-missing-executable-2034"],
                                                             Path.cwd(), os.environ.copy())
                # Observe launch failure before cleanup can cancel the native
                # broker during startup without attempting the missing payload.
                missing.finish()

        summary, output = self.run_control([Control("test_launch")])
        self.assertFalse(summary["success"])
        # Windows first owns the native launcher, whose certificate then
        # reports the missing payload; POSIX Popen rejects it immediately.
        self.assertEqual(summary["owned_children"], 2 if os.name == "nt" else 1)
        self.assertEqual(summary["unreaped_children"], 0)
        self.assertIn("native launch did not certify completion" if os.name == "nt" else "FileNotFoundError", output)

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
        descendant_handles = []
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
            kernel.CloseHandle.argtypes = (wintypes.HANDLE,)

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
                        "Path('descendant.pid.tmp').write_text(str(p.pid)); "
                        "Path('descendant.pid.tmp').replace('descendant.pid'); time.sleep(60)")
                child = scheduler.bootstrap.WrapperProcess(self,
                    [sys.executable, "-c", code], directory.name, os.environ.copy())
                children.append(child)
                # The child publishes a closed PID file by atomic rename. File
                # existence therefore certifies complete content before parsing.
                marker = Path(directory.name) / "descendant.pid"
                try:
                    deadline = child.started + scheduler.bootstrap.WRAPPER_TIMEOUT_SECONDS
                    while (not marker.exists() and child.process.poll() is None and
                           time.monotonic() < deadline):
                        time.sleep(0.01)
                    if not marker.exists():
                        result = child.finish(timeout_seconds=0)
                        self.fail("child readiness failed: " + repr(result))
                    pid = int(marker.read_text())
                    descendants.append(pid)
                    if os.name == "nt":
                        # Retain the live witness before permitting cancellation.
                        # A completed job can already have an empty PID list.
                        handle = kernel.OpenProcess(0x100000, False, pid)
                        self.assertTrue(handle, ctypes.get_last_error())
                        descendant_handles.append(handle)
                        self.assertEqual(kernel.WaitForSingleObject(handle, 0), 258)
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
            if os.name == "nt":
                try:
                    for handle in descendant_handles:
                        # Check before fixture assertions, and close even when
                        # a later scheduler assertion fails.
                        self.assertEqual(kernel.WaitForSingleObject(handle, 0), 0)
                finally:
                    for handle in descendant_handles:
                        kernel.CloseHandle(handle)
        diagnostics = json.dumps(summary) + "\n" + output
        self.assertFalse(thread.is_alive())
        self.assertEqual(cancellation_errors, [], diagnostics)
        self.assertEqual(summary["unreaped_children"], 0, diagnostics)
        self.assertTrue(all(child.cleaned and child.stdout.closed and child.stderr.closed for child in children), diagnostics)
        survivors = {path: [str(entry) for entry in Path(path).rglob("*")]
                     for path in directories if Path(path).exists()}
        self.assertEqual(survivors, {}, diagnostics + "\nfixture survivors: " + repr(survivors))
        self.assertTrue(summary["cases"][0].get("success"), diagnostics)
        self.assertTrue(requested.is_set(), diagnostics)
        self.assertTrue(summary["cancelled"], diagnostics)
        self.assertFalse(summary["success"], diagnostics)
        self.assertEqual(len(descendants), 1)
        pid = descendants[0]
        if os.name == "nt":
            self.assertEqual(len(descendant_handles), 1, diagnostics)
            self.assertTrue(children[0].launch_terminal_record["complete"], diagnostics)
            self.assertTrue(children[0].launch_terminal_record["cancelled"], diagnostics)
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


    def test_windows_cleanup_drains_the_job_before_fixture_removal(self):
        self.check_windows_cleanup_order()

    def test_windows_job_drain_failure_still_reaps_direct_child(self):
        self.check_windows_cleanup_order(drain_error=RuntimeError("job-drain-marker"))

    def test_windows_direct_cleanup_failure_keeps_the_drain_failure(self):
        self.check_windows_cleanup_order(drain_error=RuntimeError("job-drain-marker"),
                                         direct_error=RuntimeError("direct-cleanup-marker"))

    def test_windows_missing_job_with_a_live_launcher_fails(self):
        self.check_windows_cleanup_order(record={"complete": False, "absent": True, "error": 2})

    def test_windows_launcher_drain_failure_after_command_exit_fails(self):
        self.check_windows_cleanup_order(launcher_status=scheduler.LAUNCH_DRAIN_FAILED_EXIT)

    def check_windows_cleanup_order(self, drain_error=None, direct_error=None, record=None, launcher_status=0):
        events = []
        record = record or {"complete": True, "waited": [24, 123], "foreign": [], "pending": [], "failed": []}

        # Preserve the failure inventory as cancellation moves into the native
        # owner, which acknowledges even before job creation or assignment.
        def certificate():
            events.append(("certificate",))
            if drain_error is not None:
                raise drain_error
            if record.get("absent") or launcher_status:
                raise RuntimeError("native owner did not certify cancellation")
            return record

        child = scheduler.CaseProcess.__new__(scheduler.CaseProcess)
        child.cleanup_lock = threading.RLock()
        child.cleanup_seconds = 0.0
        child.descendant_cleanup_error = None
        child.descendant_waited_pids = []
        child.job_drain = None
        child.job_name = "job-marker"
        child.launch_status_path = mock.Mock(name="terminal-path")
        child.launch_status_path.name = "terminal.json"
        child.launch_status_path.with_name.return_value.touch.side_effect = lambda **_kwargs: events.append(("cancel",))
        child.cleaned = False
        child.process = mock.Mock(pid=123, returncode=None)
        child.process.poll.return_value = None

        waits = 0
        def wait(timeout):
            nonlocal waits
            waits += 1
            events.append(("wait", timeout))
            child.process.returncode = launcher_status
            child.process.poll.return_value = launcher_status
            if direct_error is not None and waits % 2 == 0:
                raise direct_error
            return launcher_status
        child.process.wait.side_effect = wait

        with mock.patch.object(scheduler.os, "name", "nt"), \
             mock.patch.object(child, "launch_status", side_effect=certificate), \
             mock.patch.object(scheduler.time, "monotonic", return_value=100):
            expected_error = drain_error or direct_error
            failed = expected_error is not None or record.get("absent") or launcher_status
            if failed:
                with self.assertRaises(RuntimeError) as raised:
                    child.cleanup()
                for marker in (drain_error, direct_error):
                    if marker is not None:
                        self.assertIn(str(marker), str(raised.exception))
                events.append(("fixture",))
                with self.assertRaises(RuntimeError):
                    child.cleanup()  # A failed owner stays failed.
            else:
                child.cleanup()
                events.append(("fixture",))
                child.cleanup()  # Already cleaned; nothing is drained twice.
        sequence = [("cancel",), ("wait", scheduler.bootstrap.CLEANUP_TIMEOUT_SECONDS),
                    ("certificate",), ("wait", scheduler.bootstrap.CLEANUP_TIMEOUT_SECONDS), ("fixture",)]
        if not failed:
            sequence.extend([("wait", scheduler.bootstrap.CLEANUP_TIMEOUT_SECONDS),
                             ("certificate",), ("wait", scheduler.bootstrap.CLEANUP_TIMEOUT_SECONDS)])
        self.assertEqual(events, sequence)
        self.assertFalse(child.process.kill.called)

    def drain_with(self, *fixture, budget=0.2):
        # Run the real caller against a deliberately broken native drain.
        command = [os.environ[scheduler.PROCESS_TREE_ENVIRONMENT], "fixture"] + list(fixture)
        with mock.patch.object(scheduler, "process_tree_command", return_value=command):
            return scheduler.drain_job("job-marker", time.monotonic() + budget)

    @unittest.skipUnless(os.environ.get(scheduler.PROCESS_TREE_ENVIRONMENT),
                         "CI builds tools/windows_process_tree.c and exports its path")
    def test_drain_accepts_only_one_complete_timely_record(self):
        self.assertEqual(self.drain_with("late", "0")["waited"], [])
        for fixture, message in ((("silent",), "record malformed"),
                                 (("partial",), "record malformed"),
                                 (("malformed",), "record malformed"),
                                 (("repeated",), "record malformed"),
                                 (("late", "600"), "termination incomplete.*'late': True"),
                                 (("partial-late", "5000"), "exceeded its deadline"),
                                 (("sleep", "5000"), "exceeded its deadline")):
            with self.subTest(fixture=fixture):
                with self.assertRaisesRegex(RuntimeError, message):
                    self.drain_with(*fixture)

    @unittest.skipUnless(os.name == "nt", "Windows job objects")
    def test_windows_job_membership_never_admits_an_outside_process(self):
        # The reused-PID control: a live process outside the job is never
        # treated as a member, waited on or terminated by a drain.
        result = subprocess.run(scheduler.process_tree_command() + ["selftest"],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout), {"selftest": True, "member": True, "outsider_member": False,
                                                     "outsider_alive": True, "member_waited": True})

    def test_native_launch_sidecar_rejects_missing_malformed_and_late_evidence(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            child = scheduler.CaseProcess.__new__(scheduler.CaseProcess)
            child.launch_status_path = Path(directory) / "terminal.json"
            child.runtime_deadline_ns = 10_000_000_000
            child.launch_terminal_record = None
            child.process = mock.Mock(returncode=0)
            valid = {"complete": True, "runtime_timed_out": False, "cancelled": False, "root_returncode": 17,
                     "root_pid": 123, "runtime_deadline_ns": child.runtime_deadline_ns,
                     "root_finished_ns": 9_000_000_000, "cleanup_started_ns": 9_000_000_000,
                     "cleanup_finished_ns": 9_500_000_000}
            with self.assertRaisesRegex(RuntimeError, "missing or malformed"):
                child.launch_status()
            for malformed in ("{partial", json.dumps(valid) + "\n" + json.dumps(valid)):
                child.launch_status_path.write_text(malformed)
                with self.assertRaisesRegex(RuntimeError, "missing or malformed"):
                    child.launch_status()
            for changes in ({"complete": False}, {"runtime_deadline_ns": 1}, {"root_pid": True},
                            {"cleanup_finished_ns": 20_000_000_000}, {"root_finished_ns": 11_000_000_000}):
                with self.subTest(changes=changes):
                    child.launch_status_path.write_text(json.dumps(valid | changes))
                    with self.assertRaises(RuntimeError):
                        child.launch_status()
            child.launch_status_path.write_text(json.dumps(valid))
            child.require_terminal_observation = True
            child.terminal_deadline_ns = 20_000_000_000
            with mock.patch.object(scheduler.time, "monotonic_ns", return_value=20_000_000_001):
                with self.assertRaisesRegex(RuntimeError, "observed after its deadline"):
                    child.launch_status()
            child.require_terminal_observation = False
            child.process.returncode = scheduler.LAUNCH_DRAIN_FAILED_EXIT
            with self.assertRaisesRegex(RuntimeError, "disagrees with its exit"):
                child.launch_status()

    def test_native_broker_status_is_distinct_from_payload_status(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            child = scheduler.CaseProcess.__new__(scheduler.CaseProcess)
            child.launch_status_path = Path(directory) / "terminal.json"
            child.runtime_deadline_ns = 10_000_000_000
            child.process = mock.Mock(returncode=0, pid=456, args=["native-owner"])
            child.job_name = "native-job"
            child.started = child.runtime_deadline_ns / 1_000_000_000 - scheduler.bootstrap.WRAPPER_TIMEOUT_SECONDS
            child.test_id = self.id()
            child.cleanup_seconds = 0
            child.job_drain = None
            valid = {"complete": True, "runtime_timed_out": False, "cancelled": False, "root_returncode": 17,
                     "root_pid": 123, "runtime_deadline_ns": child.runtime_deadline_ns,
                     "root_finished_ns": 9_000_000_000, "cleanup_started_ns": 9_000_000_000,
                     "cleanup_finished_ns": 9_500_000_000}
            for status in (124, 125):
                with self.subTest(payload=status):
                    child.launch_terminal_record = None
                    child.launch_status_path.write_text(json.dumps(valid | {"root_returncode": status}))
                    broker_result = subprocess.CompletedProcess(["native-owner"], 0, "stdout", "stderr")
                    with mock.patch.object(scheduler.bootstrap.WrapperProcess, "finish", return_value=broker_result), \
                         mock.patch.object(scheduler.time, "monotonic_ns", return_value=9_500_000_000), \
                         contextlib.redirect_stdout(io.StringIO()):
                        result = child.finish()
                    self.assertEqual((result.returncode, result.stdout, result.stderr), (status, "stdout", "stderr"))
            child.launch_terminal_record = None
            child.process.returncode = scheduler.LAUNCH_RUNTIME_TIMEOUT_EXIT
            child.launch_status_path.write_text(json.dumps(valid | {
                "runtime_timed_out": True, "root_finished_ns": 0,
                "cleanup_started_ns": 10_000_000_000, "cleanup_finished_ns": 10_100_000_000}))
            with mock.patch.object(scheduler.bootstrap.WrapperProcess, "finish", return_value=broker_result), \
                 mock.patch.object(scheduler.time, "monotonic_ns", return_value=10_100_000_000), \
                 contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(AssertionError, "bootstrap child timed out"):
                    child.finish()

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
        for selected, jobs in (([], 1), ([cases[0], cases[0]], 2), (cases, 0), (cases, scheduler.MAX_JOBS + 1)):
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
        self.assertIn("tools/bootstrap_wrapper_cases.py --jobs %d" % scheduler.MAX_JOBS, wrappers)
        self.assertIn("tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v", wrappers)
        self.assertIn("set -euo pipefail", wrappers)
        self.assertIn("timeout-minutes: ${{ matrix.platform == 'windows' && 20 || 2 }}", wrappers)
        self.assertNotIn("continue-on-error", wrappers)
        for step in (policy, wrappers):
            self.assertIn("build.c", step)
            self.assertIn("bootstrap_process_owner", step)
            self.assertNotIn("tools/windows_process_tree.c", step)
            self.assertIn("export BUSTER_WINDOWS_PROCESS_TREE=", step)


def load_tests(loader, tests, pattern):
    tests.addTests(loader.loadTestsFromModule(bootstrap_absolute_paths_test))
    return tests


if __name__ == "__main__":
    unittest.main()
