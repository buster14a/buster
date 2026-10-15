#!/usr/bin/env python3
"""Own #2034 case admission; reuse the frozen bootstrap tests and child owner.

run_cases owns a persistent bounded worker gang and ordered result slots.
CaseProcess adds cancellation ownership and timing to the existing child owner.
On Windows, tools/windows_process_tree.c owns each case's job and enforces the
frozen runtime/cleanup budgets; this adapter forwards cancellation and evidence.
The six-writer publication scenario runs alone after the independent cases.
No behavior assertions, fake compiler or shell selection live here.
"""
import argparse
import io
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
import bootstrap_wrapper_test as bootstrap

EXCLUSIVE_TEST = "test_concurrent_cold_publication_uses_immutable_outputs"
# Hosted Windows Release runners have four logical CPUs; four lanes use them
# without oversubscribing (#3912). Until build.ps1 imported its modules by name
# (#3979), each AArch64 launch spent ~22.6 s CPU scanning PSModulePath.
MAX_JOBS = 4


class LineOutput:
    """Keep concurrent timing records intact without buffering a whole case."""
    def __init__(self, stream):
        self.stream = stream
        self.lock = threading.Lock()
        self.local = threading.local()

    def write(self, text):
        pending = getattr(self.local, "pending", "") + text
        lines = pending.splitlines(keepends=True)
        self.local.pending = ""
        with self.lock:
            for line in lines:
                if line.endswith("\n"):
                    self.stream.write(line)
                else:
                    self.local.pending = line
        return len(text)

    def flush(self):
        with self.lock:
            self.stream.flush()


class CaseOwnership:
    def __init__(self):
        self.lock = threading.RLock()
        self.cancelled = threading.Event()
        self.children = set()
        self.peak_live_children = 0

    def cancel(self):
        # Registration and cancellation share a lock: a child launched during
        # interruption is either in this set or sees the cancelled event.
        self.cancelled.set()
        errors = []
        with self.lock:
            for child in list(self.children):
                try:
                    child.cleanup()
                except Exception as error:
                    errors.append(repr(error))
        return errors



PROCESS_TREE_ENVIRONMENT = "BUSTER_WINDOWS_PROCESS_TREE"
# Time for the drain record to arrive after the helper's own budget. A record
# that arrives after the cleanup deadline is still a failure.
DRAIN_RECORD_GRACE_SECONDS = 1
# windows_process_tree.c: the drain found no job, and a finished command's
# job could not be emptied.
DRAIN_ABSENT_EXIT = 3
LAUNCH_DRAIN_FAILED_EXIT = 125
LAUNCH_RUNTIME_TIMEOUT_EXIT = 124


def process_tree_command():
    path = os.environ.get(PROCESS_TREE_ENVIRONMENT)
    if not path or not os.path.isfile(path):
        raise RuntimeError("bootstrap case ownership requires " + PROCESS_TREE_ENVIRONMENT +
                           ", the executable built from tools/windows_process_tree.c")
    return [path]


def drain_job(name, deadline):
    """Terminate every member of a case job and wait for each; doubt fails."""
    budget = max(0, int((deadline - time.monotonic()) * 1000))
    try:
        result = subprocess.run(process_tree_command() + ["drain", name, str(budget)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                timeout=budget / 1000 + DRAIN_RECORD_GRACE_SECONDS)
    except subprocess.TimeoutExpired:
        raise RuntimeError("bootstrap job drain exceeded its deadline: " + name) from None
    late = time.monotonic() > deadline
    record = None
    if result.stdout.endswith("\n") and len(result.stdout.splitlines()) == 1:
        try:
            record = json.loads(result.stdout)
        except ValueError:
            pass
    diagnostic = repr({"job": name, "exit": result.returncode, "late": late,
                       "stdout": result.stdout, "stderr": result.stderr})
    if not isinstance(record, dict):
        raise RuntimeError("bootstrap job drain record malformed: " + diagnostic)
    absent = result.returncode == DRAIN_ABSENT_EXIT and record.get("absent") is True
    complete = (result.returncode == 0 and record.get("complete") is True and
                isinstance(record.get("waited"), list) and record.get("pending") == [] and
                record.get("failed") == [])
    if late or not (absent or complete):
        raise RuntimeError("bootstrap descendant termination incomplete: " + diagnostic)
    return record


class CaseProcess(bootstrap.WrapperProcess):
    ownership = None

    def __init__(self, owner, command, cwd, environment):
        self.cleanup_lock = threading.RLock()
        self.ownership = type(self).ownership
        self.cleanup_seconds = 0.0
        self.descendant_cleanup_error = None
        self.descendant_waited_pids = []
        self.job_drain = None
        self.job_name = None
        self.launch_status_path = None
        self.runtime_deadline_ns = None
        self.launch_terminal_record = None
        if os.name == "nt":
            # The launcher assigns the suspended command to a fresh job before
            # it runs, so every descendant is a kernel-tracked member (#3690).
            self.job_name = "Local\\buster-bootstrap-case-%d-%s" % (os.getpid(), uuid.uuid4().hex)
            clock = {"QueryPerformanceCounter()": "qpc", "GetTickCount64()": "tick"}.get(
                time.get_clock_info("monotonic").implementation)
            if clock is None:
                raise RuntimeError("bootstrap native deadline requires a known Windows monotonic clock")
            status_directory = tempfile.TemporaryDirectory(prefix="buster-case-status-")
            owner.addCleanup(status_directory.cleanup)
            self.launch_status_path = Path(status_directory.name) / "terminal.json"
            # Set the native deadline before launcher startup. Its separate
            # cleanup allowance must not extend the payload's frozen budget.
            self.runtime_deadline_ns = time.monotonic_ns() + int(bootstrap.WRAPPER_TIMEOUT_SECONDS * 1_000_000_000)
            command = process_tree_command() + [
                "launch", self.job_name, str(int(bootstrap.CLEANUP_TIMEOUT_SECONDS * 1000)),
                "--deadline-ns", str(self.runtime_deadline_ns), "--clock", clock,
                "--status-path", str(self.launch_status_path),
                subprocess.list2cmdline(command)]
        with self.ownership.lock:
            if self.ownership.cancelled.is_set():
                raise RuntimeError("bootstrap case execution cancelled before launch")
            super().__init__(owner, command, cwd, environment)
            self.ownership.children.add(self)
            self.ownership.peak_live_children = max(self.ownership.peak_live_children,
                sum(child.process.poll() is None for child in self.ownership.children))
        print("BOOTSTRAP_LAUNCH " + json.dumps({
            "test": self.test_id, "pid": self.process.pid, "job": self.job_name,
            "elapsed_seconds": time.monotonic() - self.started,
        }), flush=True)

    def launch_status(self):
        if self.launch_terminal_record is not None:
            return self.launch_terminal_record
        try:
            record = json.loads(self.launch_status_path.read_text())
        except (AttributeError, OSError, ValueError) as error:
            raise RuntimeError("bootstrap native launch record missing or malformed") from error
        integer_fields = ("runtime_deadline_ns", "root_returncode", "root_pid", "root_finished_ns",
                          "cleanup_started_ns", "cleanup_finished_ns")
        if (not isinstance(record, dict) or record.get("complete") is not True or
                record.get("runtime_deadline_ns") != self.runtime_deadline_ns or
                type(record.get("runtime_timed_out")) is not bool or
                type(record.get("cancelled")) is not bool or
                any(type(record.get(field)) is not int for field in integer_fields)):
            raise RuntimeError("bootstrap native launch did not certify completion: " + repr(record))
        cleanup_budget_ns = int(bootstrap.CLEANUP_TIMEOUT_SECONDS * 1_000_000_000)
        cleanup_limit_ns = min(record["cleanup_started_ns"], self.runtime_deadline_ns) + cleanup_budget_ns
        if (not 0 <= record["root_pid"] <= 0xFFFFFFFF or
                not -1 <= record["root_returncode"] <= 0xFFFFFFFF or
                not 0 <= record["root_finished_ns"] <= record["cleanup_started_ns"] or
                not record["cleanup_started_ns"] <= record["cleanup_finished_ns"] <= cleanup_limit_ns or
                (not (record["runtime_timed_out"] or record["cancelled"]) and
                 (not record["root_pid"] or record["root_returncode"] < 0)) or
                (not record["runtime_timed_out"] and
                 record["root_finished_ns"] > self.runtime_deadline_ns)):
            raise RuntimeError("bootstrap native launch record violates its deadlines: " + repr(record))
        observation_deadline_ns = getattr(self, "cleanup_observation_deadline_ns", None)
        if getattr(self, "require_terminal_observation", False):
            observation_deadline_ns = min(observation_deadline_ns, self.terminal_deadline_ns) if observation_deadline_ns is not None else self.terminal_deadline_ns
        if observation_deadline_ns is not None and time.monotonic_ns() > observation_deadline_ns:
            raise RuntimeError("bootstrap native launch terminal record observed after its deadline")
        expected = LAUNCH_RUNTIME_TIMEOUT_EXIT if record["runtime_timed_out"] else 0
        if self.process.returncode != expected:
            raise RuntimeError("bootstrap native launch status disagrees with its exit: " + repr(record))
        self.launch_terminal_record = record
        return record

    def cleanup(self):
        # finish() and cancellation may reach the same child concurrently.
        # Never acquire the ownership lock here: launch registers cleanup
        # under that lock, and cancellation walks the already owned set.
        with self.cleanup_lock:
            if self.descendant_cleanup_error is not None:
                raise self.descendant_cleanup_error
            started = time.monotonic()
            drain_error = None
            try:
                if os.name == "nt":
                    deadline = started + bootstrap.CLEANUP_TIMEOUT_SECONDS
                    terminal_deadline_ns = getattr(self, "terminal_deadline_ns", None)
                    if terminal_deadline_ns is not None:
                        deadline = min(deadline, terminal_deadline_ns / 1_000_000_000)
                    try:
                        if not self.cleaned and self.process.poll() is None:
                            self.cleanup_observation_deadline_ns = int(deadline * 1_000_000_000)
                            # Let the owner acknowledge cancellation even before
                            # it creates/assigns the payload. An external drain
                            # can race that startup and miss the new member.
                            self.launch_status_path.with_name(self.launch_status_path.name + ".cancel").touch(exist_ok=False)
                        self.process.wait(timeout=max(0.0, deadline - time.monotonic()))
                        self.launch_status()
                    except Exception as error:
                        drain_error = error
                    finally:
                        # A failed native owner is still reaped, within the
                        # same cleanup allowance; it never becomes certified.
                        try:
                            if self.process.poll() is None:
                                self.process.kill()
                            self.process.wait(timeout=max(0.0, deadline - time.monotonic()))
                        except Exception as error:
                            if drain_error is not None:
                                error = RuntimeError("bootstrap descendant cleanup failures: " +
                                                     repr([drain_error, error]))
                            drain_error = error
                    self.cleaned = drain_error is None
                else:
                    try:
                        super().cleanup()
                    except Exception as error:
                        drain_error = error
                if drain_error is not None:
                    raise drain_error
            except Exception as error:
                self.descendant_cleanup_error = error
                self.cleaned = False
                raise
            finally:
                self.cleanup_seconds += time.monotonic() - started

    def finish(self, timeout_seconds=None):
        started = time.monotonic()
        try:
            if self.job_name is not None and timeout_seconds is None:
                # The native owner enforces the original payload deadline.
                # Collection also allows its original, separately bounded
                # cleanup; explicit shorter waits still use the frozen reap.
                self.terminal_deadline_ns = self.runtime_deadline_ns + int(bootstrap.CLEANUP_TIMEOUT_SECONDS * 1_000_000_000)
                observed_ns = time.monotonic_ns()
                legacy_deadline_ns = int((self.started + bootstrap.WRAPPER_TIMEOUT_SECONDS) * 1_000_000_000)
                if legacy_deadline_ns < self.runtime_deadline_ns:
                    # Preserve the frozen expired-launch control as a strictly
                    # shorter caller wait; it cannot extend the native budget.
                    self.terminal_deadline_ns = legacy_deadline_ns + int(bootstrap.CLEANUP_TIMEOUT_SECONDS * 1_000_000_000)
                    timeout_seconds = max(0.0, (legacy_deadline_ns - observed_ns) / 1_000_000_000)
                else:
                    timeout_seconds = max(0.0, (self.terminal_deadline_ns - observed_ns) / 1_000_000_000)
                self.require_terminal_observation = observed_ns < self.terminal_deadline_ns
            result = super().finish(timeout_seconds)
            if self.job_name is not None:
                record = self.launch_status()
                if record["runtime_timed_out"]:
                    raise AssertionError("bootstrap child timed out: " + repr(self.process.args) +
                                         "\nstdout:\n" + result.stdout + "\nstderr:\n" + result.stderr)
                result.returncode = record["root_returncode"]
        finally:
            print("BOOTSTRAP_COLLECTION " + json.dumps({
                "test": self.test_id, "pid": self.process.pid,
                "elapsed_seconds": time.monotonic() - started,
                "cleanup_seconds": self.cleanup_seconds,
                "job_drain": self.job_drain,
                "native_launch": self.launch_terminal_record,
            }), flush=True)
        return result


def behavior_cases():
    cases = list(unittest.defaultTestLoader.loadTestsFromTestCase(bootstrap.BootstrapWrapperTests))
    return cases


def run_cases(cases, jobs=1, stream=None, ownership=None):
    if jobs not in range(1, MAX_JOBS + 1):
        raise ValueError("bootstrap case jobs must be between 1 and %d" % MAX_JOBS)
    if not cases or len({case.id() for case in cases}) != len(cases):
        raise ValueError("bootstrap cases must be nonempty and unique")
    if os.name == "nt":
        process_tree_command()  # Refuse before any lane launches a case.
    stream = sys.stdout if stream is None else stream
    ownership = CaseOwnership() if ownership is None else ownership
    independent = [i for i, case in enumerate(cases) if case._testMethodName != EXCLUSIVE_TEST]
    exclusive = [i for i, case in enumerate(cases) if case._testMethodName == EXCLUSIVE_TEST]
    results = [None] * len(cases)
    claim_lock = threading.Lock()
    next_case = 0
    worker_errors = []
    lanes_done = threading.Condition()
    completed_lanes = 0
    startup = threading.Event()
    original_stdout = sys.stdout
    original_process = bootstrap.WrapperProcess
    sys.stdout = LineOutput(original_stdout)
    bootstrap.WrapperProcess = CaseProcess
    CaseProcess.ownership = ownership
    started = time.monotonic()
    cancelled = False
    cleanup_errors = []

    def execute(index):
        output = io.StringIO()
        result = bootstrap.BootstrapTestRunner(stream=output, verbosity=2).run(cases[index])
        results[index] = (result, output.getvalue())

    def lane():
        nonlocal next_case, completed_lanes
        try:
            startup.wait()
            while not ownership.cancelled.is_set():
                with claim_lock:
                    index = independent[next_case] if next_case < len(independent) else None
                    next_case += 1
                if index is None:
                    break
                execute(index)
        except BaseException as error:
            with claim_lock:
                worker_errors.append(repr(error))
            ownership.cancel()
        finally:
            with lanes_done:
                completed_lanes += 1
                lanes_done.notify_all()

    workers = []
    try:
        # Defer signal exceptions until every started thread is registered.
        # The startup gate keeps those threads from launching a fixture first.
        previous_signals = {}
        try:
            if threading.current_thread() is threading.main_thread():
                for number in (signal.SIGINT, signal.SIGTERM):
                    previous_signals[number] = signal.getsignal(number)
                    signal.signal(number, lambda _number, _frame: ownership.cancelled.set())
            for _ in range(jobs):
                if not ownership.cancelled.is_set():
                    worker = threading.Thread(target=lane, name="bootstrap-case-lane")
                    worker.start()
                    workers.append(worker)
        finally:
            for number, handler in previous_signals.items():
                signal.signal(number, handler)
        startup.set()
        # Wait on our completion condition before joining. Interrupting a
        # live Thread.join can invalidate its bookkeeping on CPython; child
        # cleanup and module restoration must wait for actual lane completion.
        with lanes_done:
            while completed_lanes < len(workers):
                lanes_done.wait(timeout=0.1)
        for index in exclusive:
            if not ownership.cancelled.is_set():
                execute(index)
    except BaseException as error:
        worker_errors.append(repr(error))
        cancelled = True
        cleanup_errors.extend(ownership.cancel())
    finally:
        # Also reap on worker/startup errors. Restore shared module state only
        # after every lane has stopped using it and every child is cleaned.
        if ownership.cancelled.is_set():
            cancelled = True
            cleanup_errors.extend(ownership.cancel())
        startup.set()
        with lanes_done:
            while completed_lanes < len(workers):
                lanes_done.wait(timeout=0.1)
        for worker in workers:
            worker.join()
        with ownership.lock:
            for child in ownership.children:
                if not child.cleaned:
                    try:
                        child.cleanup()
                    except Exception as error:
                        cleanup_errors.append(repr(error))
        bootstrap.WrapperProcess = original_process
        CaseProcess.ownership = None
        sys.stdout = original_stdout
    rows = []
    for case, collected in zip(cases, results):
        row = {"test": case.id(), "completed": collected is not None}
        if collected is not None:
            result, output = collected
            stream.write(output)
            row.update(tests=result.testsRun, failures=len(result.failures),
                       errors=len(result.errors), skipped=len(result.skipped),
                       success=result.wasSuccessful())
        rows.append(row)
    success = (not cancelled and not worker_errors and not cleanup_errors and
               all(row.get("success") and row.get("tests") == 1 and
                   row.get("skipped") == 0 for row in rows))
    summary = {"jobs": jobs, "elapsed_seconds": time.monotonic() - started,
               "success": success, "cancelled": cancelled, "cases": rows,
               "worker_errors": worker_errors, "cleanup_errors": cleanup_errors,
               "owned_children": len(ownership.children),
               "peak_live_children": ownership.peak_live_children,
               "unreaped_children": sum(child.process.poll() is None for child in ownership.children)}
    # Release the registry cycle and native Popen handles between repeated
    # suite runs; all timing/cleanup facts above have already been collected.
    ownership.children.clear()
    stream.write("BOOTSTRAP_CASE_SUMMARY " + json.dumps(summary) + "\n")
    stream.flush()
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, choices=range(1, MAX_JOBS + 1), default=1)
    args = parser.parse_args()
    print("BOOTSTRAP_ENVIRONMENT " + json.dumps({
        "os": platform.system(), "machine": platform.machine(),
        "python": sys.executable, "python_version": platform.python_version(),
        "shell": (shutil.which("powershell.exe") or shutil.which("pwsh.exe"))
                 if os.name == "nt" else shutil.which("bash"),
        "image_os": os.environ.get("ImageOS"), "image_version": os.environ.get("ImageVersion"),
        "source": os.environ.get("GITHUB_SHA"),
        "wrapper_timeout_seconds": bootstrap.WRAPPER_TIMEOUT_SECONDS,
    }), flush=True)
    signal.signal(signal.SIGTERM, signal.default_int_handler)
    summary = run_cases(behavior_cases(), jobs=args.jobs)
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
