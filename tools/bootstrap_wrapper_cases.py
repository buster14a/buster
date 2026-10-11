#!/usr/bin/env python3
"""Own #2034 case admission; reuse the frozen bootstrap tests and child owner.

run_cases owns a persistent bounded worker gang and ordered result slots.
CaseProcess adds cancellation ownership and timing to the existing child owner.
The six-writer publication scenario runs alone after the independent cases.
No test assertions, fake compiler, shell selection or deadlines live here.
"""
import argparse
import io
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import sys
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
import bootstrap_wrapper_test as bootstrap

EXCLUSIVE_TEST = "test_concurrent_cold_publication_uses_immutable_outputs"


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



class WindowsFixtureTree:
    """Retain descendant identities before taskkill; wait for termination, not its request."""
    def __init__(self, pid, parent_handle):
        import ctypes
        from ctypes import wintypes

        class ProcessEntry(ctypes.Structure):
            _fields_ = [
                ("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260),
            ]

        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel = self.kernel
        kernel.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
        kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        kernel.Process32FirstW.argtypes = (wintypes.HANDLE, ctypes.POINTER(ProcessEntry))
        kernel.Process32FirstW.restype = wintypes.BOOL
        kernel.Process32NextW.argtypes = kernel.Process32FirstW.argtypes
        kernel.Process32NextW.restype = wintypes.BOOL
        kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel.CloseHandle.restype = wintypes.BOOL
        kernel.GetProcessTimes.argtypes = (wintypes.HANDLE,) + (ctypes.POINTER(wintypes.FILETIME),) * 4
        kernel.GetProcessTimes.restype = wintypes.BOOL
        kernel.GetSystemTimePreciseAsFileTime.argtypes = (ctypes.POINTER(wintypes.FILETIME),)
        kernel.GetSystemTimePreciseAsFileTime.restype = None
        self.handles = []
        self.parent_created = self.creation_time(parent_handle)
        cut = wintypes.FILETIME()
        kernel.GetSystemTimePreciseAsFileTime(ctypes.byref(cut))
        self.snapshot_started = (cut.dwHighDateTime << 32) | cut.dwLowDateTime
        snapshot = kernel.CreateToolhelp32Snapshot(2, 0)  # TH32CS_SNAPPROCESS.
        if snapshot == wintypes.HANDLE(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            parents = {}
            entry = ProcessEntry()
            entry.dwSize = ctypes.sizeof(entry)
            present = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
            while present:
                parents[entry.th32ProcessID] = entry.th32ParentProcessID
                present = kernel.Process32NextW(snapshot, ctypes.byref(entry))
            if ctypes.get_last_error() != 18:  # ERROR_NO_MORE_FILES.
                raise ctypes.WinError(ctypes.get_last_error())
            owned = {pid}
            pending = set(parents) - owned
            while pending:
                children = {child for child in pending if parents[child] in owned}
                if not children:
                    break
                owned.update(children)
                pending.difference_update(children)
            for child in sorted(owned - {pid}):
                # Query creation identity as well as waiting. A numeric snapshot
                # PID can be reused before OpenProcess acquires a native handle.
                handle = kernel.OpenProcess(0x101000, False, child)  # SYNCHRONIZE | QUERY_LIMITED_INFORMATION.
                if handle:
                    try:
                        created = self.creation_time(handle)
                    except BaseException:
                        kernel.CloseHandle(handle)
                        raise
                    self.retain_handle(child, handle, created)
                elif ctypes.get_last_error() != 87:  # ERROR_INVALID_PARAMETER: exited PID.
                    raise ctypes.WinError(ctypes.get_last_error())
        except BaseException:
            self.close()
            raise
        finally:
            if not kernel.CloseHandle(snapshot):
                error = ctypes.WinError(ctypes.get_last_error())
                self.close()
                raise error

    def creation_time(self, handle):
        import ctypes
        from ctypes import wintypes
        created, exited, kernel_time, user_time = (wintypes.FILETIME() for _ in range(4))
        if not self.kernel.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(exited),
                                           ctypes.byref(kernel_time), ctypes.byref(user_time)):
            raise ctypes.WinError(ctypes.get_last_error())
        return (created.dwHighDateTime << 32) | created.dwLowDateTime

    def retain_handle(self, pid, handle, created):
        # Controlled fixtures publish readiness after spawning their descendants.
        # Reject a replacement created after snapshot admission, or a stale parent
        # PID from before our retained direct owner existed. Never wait on either.
        if created < self.parent_created or created > self.snapshot_started:
            self.kernel.CloseHandle(handle)
            raise RuntimeError("bootstrap descendant identity changed during capture: " + str(pid))
        self.handles.append((pid, handle))

    def wait_until(self, deadline):
        pending = []
        failed = []
        for pid, handle in self.handles:
            milliseconds = max(0, int((deadline - time.monotonic()) * 1000))
            result = self.kernel.WaitForSingleObject(handle, milliseconds)
            if result == 258:  # WAIT_TIMEOUT.
                pending.append(pid)
            elif result != 0:  # WAIT_OBJECT_0 alone certifies termination.
                failed.append((pid, result))
        if pending or failed:
            raise RuntimeError("bootstrap descendant termination incomplete: " +
                               repr({"pending_pids": pending, "wait_failures": failed}))

    def close(self):
        failures = []
        for pid, handle in self.handles:
            if not self.kernel.CloseHandle(handle):
                failures.append(pid)
        self.handles.clear()
        if failures:
            raise RuntimeError("bootstrap descendant handle closure failed: " + repr(failures))


class CaseProcess(bootstrap.WrapperProcess):
    ownership = None

    def __init__(self, owner, command, cwd, environment):
        self.cleanup_lock = threading.RLock()
        self.ownership = type(self).ownership
        self.cleanup_seconds = 0.0
        self.descendant_cleanup_error = None
        self.descendant_waited_pids = []
        with self.ownership.lock:
            if self.ownership.cancelled.is_set():
                raise RuntimeError("bootstrap case execution cancelled before launch")
            super().__init__(owner, command, cwd, environment)
            self.ownership.children.add(self)
            self.ownership.peak_live_children = max(self.ownership.peak_live_children,
                sum(child.process.poll() is None for child in self.ownership.children))
        print("BOOTSTRAP_LAUNCH " + json.dumps({
            "test": self.test_id, "pid": self.process.pid,
            "elapsed_seconds": time.monotonic() - self.started,
        }), flush=True)

    def cleanup(self):
        # finish() and cancellation may reach the same child concurrently.
        # Never acquire the ownership lock here: launch registers cleanup
        # under that lock, and cancellation walks the already owned set.
        with self.cleanup_lock:
            if self.descendant_cleanup_error is not None:
                raise self.descendant_cleanup_error
            started = time.monotonic()
            tree = None
            capture_error = None
            try:
                # taskkill requests descendant termination. Waiting only for
                # the direct child can remove fixtures while a descendant still
                # holds its inherited Windows current-directory handle (#2985).
                # Capture native handles first; no new PID signal target is added.
                if not self.cleaned and os.name == "nt" and self.process.poll() is None:
                    try:
                        tree = WindowsFixtureTree(self.process.pid, self.process._handle)
                    except Exception as error:
                        capture_error = error
                try:
                    super().cleanup()
                finally:
                    if tree is not None:
                        tree.wait_until(started + bootstrap.CLEANUP_TIMEOUT_SECONDS)
                        self.descendant_waited_pids.extend(pid for pid, _ in tree.handles)
                if capture_error is not None:
                    raise capture_error
            except Exception as error:
                self.descendant_cleanup_error = error
                self.cleaned = False
                raise
            finally:
                if tree is not None:
                    tree.close()
                self.cleanup_seconds += time.monotonic() - started

    def finish(self, timeout_seconds=None):
        started = time.monotonic()
        try:
            result = super().finish(timeout_seconds)
        finally:
            print("BOOTSTRAP_COLLECTION " + json.dumps({
                "test": self.test_id, "pid": self.process.pid,
                "elapsed_seconds": time.monotonic() - started,
                "cleanup_seconds": self.cleanup_seconds,
            }), flush=True)
        return result


def behavior_cases():
    cases = list(unittest.defaultTestLoader.loadTestsFromTestCase(bootstrap.BootstrapWrapperTests))
    return cases


def run_cases(cases, jobs=1, stream=None, ownership=None):
    if jobs not in (1, 2):
        raise ValueError("bootstrap case jobs must be 1 or 2")
    if not cases or len({case.id() for case in cases}) != len(cases):
        raise ValueError("bootstrap cases must be nonempty and unique")
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
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=1)
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
