#!/usr/bin/env python3
"""Resource observer controls; API mocks plus actual current-OS Python smoke.

No compiler, CI dispatch, payload launch or performance acceptance occurs here.
Synthetic run/source identities exist only in private test sessions.
"""

import argparse
import ctypes
import copy
import errno
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

import ci_checks_resources as resources


MEMINFO = "MemTotal: 1024 kB\nMemAvailable: 768 kB\nSwapTotal: 512 kB\nSwapFree: 500 kB\n"
PSI = "some avg10=0.00 avg60=0.00 avg300=0.00 total=12\nfull avg10=0.00 avg60=0.00 avg300=0.00 total=4\n"


def fixture_environment():
    name = {"Darwin": "macOS"}.get(platform.system(), platform.system())
    result = {"BUSTER_CI_CHECKS_RESOURCES": "1", "GITHUB_REPOSITORY": "buster14a/buster",
              "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "1", "GITHUB_JOB": "test",
              "GITHUB_SHA": "a" * 40, "GITHUB_REF": resources.REFS[0], "GITHUB_EVENT_NAME": "workflow_dispatch",
              "GITHUB_WORKFLOW_REF": "buster14a/buster/.github/workflows/ci.yml@" + resources.REFS[0],
              "GITHUB_WORKFLOW_SHA": "a" * 40, "RUNNER_ENVIRONMENT": "github-hosted",
              "RUNNER_NAME": "resource-control-fixture", "RUNNER_OS": name,
              "RUNNER_ARCH": "ARM64" if platform.machine().lower() in ("aarch64", "arm64") else "X64",
              "ImageOS": "synthetic-control", "ImageVersion": "synthetic-control"}
    return result


def fixture_args(path):
    return argparse.Namespace(session=str(path), source_revision="a" * 40, source_tree="b" * 40,
                              workflow_blob="c" * 40, role="linux-x86_64", invocation="checks")


def fixture_meta():
    return {"schema": resources.SCHEMA, "session": "d" * 32,
            "identity": resources.identity(fixture_args("unused"), resources.environment()),
            "producer_sha256": "e" * 64}


class Function:
    def __init__(self, call):
        self.call = call

    def __call__(self, *args):
        return self.call(*args)


class WindowsMock:
    def __init__(self):
        self.groups, self.error, self.enum_calls, self.closes = 1, 0, 0, []
        self.close_ok = 1
        self.memory_ok, self.memory_page_size, self.memory_available = 1, 4096, 100
        for name in ("GetSystemTimes", "GetActiveProcessorGroupCount", "GetActiveProcessorCount", "GetTickCount64",
                     "GlobalMemoryStatusEx", "K32GetPerformanceInfo", "K32EnumProcesses", "OpenProcess",
                     "K32GetProcessMemoryInfo", "CloseHandle"):
            setattr(self, name, Function(getattr(self, "call_" + name)))

    def call_GetActiveProcessorGroupCount(self):
        return self.groups

    def call_GetActiveProcessorCount(self, group):
        return 4

    def call_GetTickCount64(self):
        return 4321

    def call_GlobalMemoryStatusEx(self, value):
        value._obj.total_physical = 16 * 1024**3
        return 1

    def call_GetSystemTimes(self, idle, kernel, user):
        idle._obj.low, kernel._obj.low, user._obj.low = 60, 90, 20
        return 1

    def call_K32GetPerformanceInfo(self, output, size):
        if size != ctypes.sizeof(resources.PerformanceInfo) or output._obj.cb != size:
            raise AssertionError("wrong PERFORMANCE_INFORMATION ABI size")
        value = output._obj
        value.page_size = self.memory_page_size
        value.physical_total, value.physical_available = 1000, self.memory_available
        value.commit_total, value.commit_limit, value.commit_peak = 1200, 2000, 1800
        value.system_cache, value.kernel_total, value.kernel_paged, value.kernel_nonpaged = 10, 8, 5, 3
        return self.memory_ok

    def call_K32EnumProcesses(self, buffer, size, needed):
        self.enum_calls += 1
        pids = list(range(len(buffer))) if self.enum_calls == 1 else [0, 11, 12, 13, 14]
        for i, pid in enumerate(pids):
            buffer[i] = pid
        needed._obj.value = len(pids) * 4
        return 1

    def call_OpenProcess(self, rights, inherit, pid):
        if pid in (12, 13):
            self.error = 5 if pid == 12 else 87
            result = None
        else:
            self.error = 0
            result = (1 << 40) + pid
        return result

    def call_K32GetProcessMemoryInfo(self, handle, counters, size):
        if handle == (1 << 40) + 14:
            self.error, result = 1, 0
        else:
            counters._obj.resident, counters._obj.peak = 5 * 1024**3, 9 * 1024**3
            result = 1
        return result

    def call_CloseHandle(self, handle):
        self.closes.append(handle)
        return self.close_ok


class DarwinMock:
    def __init__(self):
        self.task, self.elements, self.cpu_count, self.enum_calls = 7, 8, 2, 0
        self.releases, self.ports, self.release_code = [], [], 0
        self.memory_code, self.memory_count, self.memory_page_size, self.memory_page_code = 0, 40, 16384, 0
        self.port_code = 0
        self.buffer = (resources.U32 * 8)(0x80000010, 20, 30, 0, 40, 50, 60, 0)
        for name in ("mach_host_self", "host_processor_info", "vm_deallocate", "mach_port_deallocate",
                     "proc_listpids", "proc_pidinfo", "sysctlbyname", "host_page_size", "host_statistics64"):
            setattr(self, name, Function(getattr(self, "call_" + name)))

    def call_mach_host_self(self):
        return 9

    def call_host_processor_info(self, host, flavor, count, output, elements):
        count._obj.value, elements._obj.value = self.cpu_count, self.elements
        ctypes.cast(output, ctypes.POINTER(ctypes.POINTER(resources.U32)))[0] = self.buffer
        return 0

    def call_vm_deallocate(self, task, address, size):
        self.releases.append((task, address, size))
        return self.release_code

    def call_mach_port_deallocate(self, task, port):
        self.ports.append((task, port))
        return self.port_code

    def call_host_page_size(self, host, size):
        size._obj.value = self.memory_page_size
        return self.memory_page_code

    def call_host_statistics64(self, host, flavor, output, count):
        if flavor != 4 or count._obj.value != 40 or ctypes.sizeof(output._obj) != 160:
            raise AssertionError("wrong HOST_VM_INFO64 ABI/count")
        count._obj.value = self.memory_count
        value = output._obj
        for i, (name, kind) in enumerate(resources.VmStatistics64._fields_):
            setattr(value, name, i + 1)
        return self.memory_code

    def call_proc_listpids(self, kind, detail, buffer, size):
        self.enum_calls += 1
        pids = list(range(1, len(buffer) + 1)) if self.enum_calls == 1 else [11, 12, 13, 14]
        for i, pid in enumerate(pids):
            buffer[i] = pid
        return len(pids) * 4

    def call_proc_pidinfo(self, pid, flavor, arg, output, size):
        if pid in (12, 13):
            ctypes.set_errno(errno.EPERM if pid == 12 else errno.ESRCH)
            result = 0
        elif pid == 14:
            result = 8
        else:
            output._obj.resident = 5 * 1024**3
            result = ctypes.sizeof(resources.TaskInfo)
        return result

    def call_sysctlbyname(self, name, output, size, new, new_size):
        if name == b"hw.memsize":
            output._obj.value = 16 * 1024**3
            return 0
        return 1


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        # Darwin's ordinary /var temporary spelling can itself be a symlink.
        self.root = Path(self.temporary.name).resolve()
        self.env = mock.patch.dict(os.environ, fixture_environment())
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.temporary.cleanup()

    def test_default_off_does_not_create_directory(self):
        path = self.root / "disabled"
        with mock.patch.dict(os.environ, {"BUSTER_CI_CHECKS_RESOURCES": "0"}):
            self.assertEqual(resources.start(fixture_args(path)), {"status": "disabled"})
        self.assertFalse(path.exists())

    def test_exact_event_ref_workflow_source_and_client_binding(self):
        args = fixture_args(self.root / "session")
        good = resources.identity(args, resources.environment())
        for key, value in (("GITHUB_REF", resources.REFS[0].upper()), ("GITHUB_REF", resources.REFS[0] + "-extra"),
                           ("GITHUB_REF", "refs/heads/codex/ci-checks-combined-overlap"),
                           ("GITHUB_EVENT_NAME", "push"), ("GITHUB_REPOSITORY", "foreign/buster"),
                           ("GITHUB_WORKFLOW_SHA", "f" * 40), ("GITHUB_WORKFLOW_REF", "foreign.yml"),
                           ("RUNNER_ENVIRONMENT", "self-hosted"), ("GITHUB_SHA", "f" * 40)):
            with self.subTest(key=key, value=value), mock.patch.dict(os.environ, {key: value}):
                with self.assertRaises(resources.EvidenceError):
                    resources.identity(args, resources.environment())
        meta = fixture_meta()
        resources.validate_client(meta, resources.environment())
        for key in ("GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB", "RUNNER_NAME"):
            with self.subTest(key=key), mock.patch.dict(os.environ, {key: "different"}):
                with self.assertRaises(resources.EvidenceError):
                    resources.validate_client(meta, resources.environment())
        self.assertEqual(good["workflow_blob"], "c" * 40)
        with mock.patch.dict(os.environ, {"SECRET_TOKEN": "do-not-retain", "PATH": "private-secret-path"}):
            self.assertNotIn("do-not-retain", json.dumps(resources.environment()))
            self.assertNotIn("private-secret-path", json.dumps(resources.environment()))

    def test_receipt_publication_is_atomic_and_never_replaces(self):
        session = resources.Session(self.root / "atomic", create=True)
        entered, release = threading.Event(), threading.Event()
        real_sync = os.fsync
        errors = []
        def paused_sync(fd):
            entered.set()
            self.assertTrue(release.wait(2))
            real_sync(fd)
        def write():
            try:
                session.write("ready.json", {"complete": True})
            except BaseException as error:
                errors.append(error)
        try:
            with mock.patch.object(resources.os, "fsync", paused_sync):
                thread = threading.Thread(target=write)
                thread.start()
                self.assertTrue(entered.wait(2))
                self.assertIsNone(session.optional("ready.json"))
                release.set()
                thread.join(3)
                self.assertFalse(thread.is_alive())
            self.assertEqual(errors, [])
            self.assertEqual(session.read("ready.json"), {"complete": True})
            with self.assertRaises(FileExistsError):
                session.write("ready.json", {"complete": False})
            self.assertEqual(session.read("ready.json"), {"complete": True})
            self.assertFalse((session.path / "pendingready.json").exists())
            (session.path / "stop.json").write_bytes(b"{")
            with self.assertRaises(ValueError):
                session.optional("stop.json")
        finally:
            release.set()
            session.close()

    def test_exclusive_directory_and_symlink_refusal(self):
        session = resources.Session(self.root / "exclusive", create=True)
        session.close()
        with self.assertRaises(FileExistsError):
            resources.Session(self.root / "exclusive", create=True)
        link = self.root / "link"
        try:
            link.symlink_to(self.root / "exclusive", target_is_directory=True)
        except OSError:
            self.skipTest("host does not permit test symlinks")
        with self.assertRaises(resources.EvidenceError):
            resources.Session(link)
        session = resources.Session(self.root / "exclusive")
        try:
            (session.path / "ready.json").symlink_to(self.root / "outside")
            with self.assertRaises(OSError):
                session.read("ready.json")
        finally:
            session.close()

    @unittest.skipUnless(hasattr(os, "mkfifo"), "POSIX FIFO control")
    def test_nonregular_receipt_cannot_block(self):
        session = resources.Session(self.root / "fifo", create=True)
        try:
            os.mkfifo(session.path / "ready.json")
            before = time.monotonic()
            with self.assertRaises(resources.EvidenceError):
                session.read("ready.json")
            self.assertLess(time.monotonic() - before, 1)
        finally:
            session.close()

    def test_failed_readiness_cleans_only_owned_observer_and_stays_failed(self):
        process = mock.Mock(returncode=-9)
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired("observer", 1), -9]
        path = self.root / "startup"
        with mock.patch.object(resources.subprocess, "Popen", return_value=process), \
             mock.patch.object(resources, "wait_receipt", side_effect=resources.EvidenceError("not ready")):
            with self.assertRaises(resources.EvidenceError):
                resources.start(fixture_args(path))
        process.terminate.assert_called_once_with()
        process.kill.assert_called_once_with()
        self.assertEqual(process.wait.call_count, 2)
        session = resources.Session(path)
        try:
            failed = session.read("startup.json")
            self.assertEqual(failed["cleanup"]["action"], "kill")
            with self.assertRaises(resources.EvidenceError):
                resources.stop(argparse.Namespace(session=str(path)))
        finally:
            session.close()

    def test_failure_receipt_and_malformed_metadata_are_not_success(self):
        path = self.root / "dual"
        session = resources.Session(path, create=True)
        try:
            meta = fixture_meta()
            session.write("session.json", meta)
            session.write("terminal.json", {"schema": resources.SCHEMA, "session": meta["session"],
                                           "status": "complete", "measurement_status": "observed"})
            session.write("failure.json", {"schema": resources.SCHEMA, "session": meta["session"], "status": "error"})
            with self.assertRaises(resources.EvidenceError):
                resources.stop(argparse.Namespace(session=str(path)))
            for bad in ({}, {"schema": resources.SCHEMA, "session": "d" * 32, "identity": "bad"}):
                with self.assertRaises(resources.EvidenceError):
                    resources.validate_client(bad, resources.environment())
        finally:
            session.close()

    def test_journal_sample_counts_gaps_limits_and_terminal_unknown(self):
        path = self.root / "journal"
        session = resources.Session(path, create=True)
        meta = fixture_meta()
        class Reader:
            def __init__(self):
                self.calls, self.ticks = 0, 0
            def facts(self):
                return {"cpu_count": 1}
            def cpu(self):
                self.ticks += 1
                return {"source": "windows-get-system-times", "frequency": 10_000_000, "width": 64,
                        "cpu_count": 1, "counters": {"user": self.ticks, "kernel": self.ticks, "idle": 0}}
            def rss(self):
                self.calls += 1
                if self.calls == 2:
                    session.write("stop.json", {"schema": resources.SCHEMA, "session": meta["session"]})
                return resources.scan_result("control", [11, 12], lambda pid: 16 if pid == 11 else 32)
            def memory(self):
                return resources.linux_memory(MEMINFO, PSI)
        try:
            end = resources.observe(session, meta, Reader(), max_seconds=2)
            rows = [json.loads(line) for line in (path / "journal.jsonl").read_text().splitlines()]
            self.assertEqual([row["event"] for row in rows], ["start", "sample", "sample", "end"])
            self.assertEqual(end["samples"], 2)
            self.assertEqual(end["maximum_observed_scan_sum_bytes"], 48)
            self.assertGreater(end["maximum_gap_ns"], 0)
            self.assertEqual(rows[1]["rss"]["gap_ns"], "unknown")
            self.assertGreaterEqual(rows[2]["rss"]["duration_ns"], 0)
            self.assertIn("not-a-guaranteed-lower-bound", rows[0]["limitations"])
            self.assertEqual(session.read("terminal.json"), end)
            self.assertEqual(end["os_memory"]["status"], "observed")
            self.assertEqual(end["os_memory"]["observations"], 4)
            self.assertEqual(end["os_memory"]["cumulative_change"]["counters"], {"some": 0, "full": 0})
            self.assertGreater(end["os_memory"]["maximum_gap_ns"], 0)
            self.assertEqual(rows[0]["os_memory"]["gap_ns"], "unknown")
        finally:
            session.close()
        with mock.patch.object(resources, "MAX_JOURNAL_BYTES", 1):
            small = resources.Session(self.root / "small", create=True)
            try:
                with self.assertRaises(resources.EvidenceError):
                    resources.observe(small, meta, Reader())
                self.assertIsNone(small.optional("ready.json"))
            finally:
                small.close()

    def test_memory_journal_keeps_partial_errors_and_intermediate_counter_drift_separate(self):
        for case in ("partial", "error", "drift"):
            with self.subTest(case=case):
                path = self.root / ("memory-" + case)
                session = resources.Session(path, create=True)
                meta = fixture_meta()
                class Reader:
                    def __init__(self):
                        self.ticks, self.calls = 0, 0
                    def facts(self):
                        return {"cpu_count": 1}
                    def cpu(self):
                        self.ticks += 1
                        return {"source": "windows-get-system-times", "frequency": 10_000_000,
                                "width": 64, "cpu_count": 1,
                                "counters": {"user": self.ticks, "kernel": self.ticks, "idle": 0}}
                    def rss(self):
                        session.write("stop.json", {"schema": resources.SCHEMA, "session": meta["session"]})
                        return resources.scan_result("control", [1], lambda pid: 10)
                    def memory(self):
                        self.calls += 1
                        if case == "error" and self.calls == 2:
                            raise OSError("fixture memory API read failure")
                        value = resources.linux_memory(MEMINFO, None if case == "partial" else PSI)
                        if case == "drift":
                            value["activity"]["counters"]["some"] = (12, 11, 13)[self.calls - 1]
                        return value
                try:
                    end = resources.observe(session, meta, Reader(), max_seconds=2)
                    rows = [json.loads(line) for line in (path / "journal.jsonl").read_text().splitlines()]
                    self.assertEqual(end["status"], "complete")
                    self.assertEqual(end["measurement_status"], "observed")
                    self.assertEqual(end["os_memory"]["status"], "partial" if case == "partial" else "error")
                    self.assertEqual(end["os_memory"]["observations"], 3)
                    if case == "partial":
                        self.assertEqual(end["os_memory"]["partial_observations"], 3)
                        self.assertEqual(end["os_memory"]["cumulative_change"]["status"], "unsupported")
                    elif case == "error":
                        self.assertEqual(rows[1]["os_memory"]["snapshot"], "unknown")
                        self.assertEqual(end["os_memory"]["error_observations"], 1)
                    else:
                        self.assertEqual(end["os_memory"]["comparison_error_observations"], 1)
                        self.assertIn("decreased/wrapped", end["os_memory"]["comparison_error"])
                        self.assertEqual(end["os_memory"]["cumulative_change"]["counters"]["some"], 1)
                    self.assertEqual(session.read("terminal.json"), end)
                finally:
                    session.close()


class PlatformTests(unittest.TestCase):
    def test_linux_memory_units_coverage_and_psi_semantics(self):
        value = resources.linux_memory(MEMINFO, PSI)
        self.assertEqual(value["status"], "observed")
        self.assertEqual(value["snapshot"]["values"]["MemAvailable"], 768 * 1024)
        self.assertEqual(value["activity"]["unit"], "microseconds")
        self.assertEqual(value["activity"]["counters"], {"some": 12, "full": 4})
        self.assertIn("estimate", value["available_kind"])
        for text in (MEMINFO + "MemTotal: 1024 kB\n", MEMINFO.replace("kB", "bytes"),
                     MEMINFO.replace("768", "1025"), MEMINFO.replace("500", "513"),
                     MEMINFO.replace("1024", str(resources.MAX_COUNTER)), MEMINFO.replace("MemAvailable", "missing")):
            with self.subTest(meminfo=text), self.assertRaises(resources.EvidenceError):
                resources.linux_memory(text, PSI)
        for text in (PSI + PSI.splitlines()[0], PSI.replace("total=4", "total=13"),
                     PSI.replace("avg10=0.00", "avg10=nan"), PSI.replace("total=12", "total=-1"),
                     PSI.replace("total=12", "total=" + str(resources.MAX_COUNTER + 1)),
                     PSI.replace("full", "other"), PSI.splitlines()[0]):
            with self.subTest(pressure=text), self.assertRaises((resources.EvidenceError, ValueError)):
                resources.linux_memory(MEMINFO, text)

    def test_linux_unavailable_psi_preserves_memory_without_zero(self):
        def text(path):
            if path == "/proc/meminfo":
                return MEMINFO
            raise PermissionError(errno.EACCES, "fixture PSI restriction")
        with mock.patch.object(resources, "bounded_text", side_effect=text):
            value = resources.memory_read(resources.LinuxReader())
        self.assertEqual(value["status"], "partial")
        self.assertEqual(value["snapshot"]["values"]["MemTotal"], 1024 * 1024)
        self.assertEqual(value["activity"]["status"], "unsupported")
        self.assertNotIn("counters", value["activity"])
        self.assertIn("[Errno 13]", value["activity"]["error"])
        with mock.patch.object(resources, "bounded_text", side_effect=OSError("unavailable")):
            unknown = resources.memory_read(resources.LinuxReader())
        self.assertEqual(unknown["status"], "error")
        self.assertEqual(unknown["snapshot"], "unknown")

    def test_windows_memory_abi_physical_and_commit_are_distinct(self):
        size = ctypes.sizeof(resources.SIZE)
        self.assertEqual(ctypes.sizeof(resources.PerformanceInfo), 104 if size == 8 else 56)
        self.assertEqual(resources.PerformanceInfo.commit_total.offset, size)
        self.assertEqual(resources.PerformanceInfo.page_size.offset, size * 10)
        api = WindowsMock()
        value = resources.WindowsReader(api).memory()
        self.assertEqual(value["status"], "observed")
        self.assertEqual(value["snapshot"]["values"]["physical_available"], 100)
        self.assertEqual(value["snapshot"]["values"]["commit_total"], 1200)
        self.assertEqual(value["snapshot"]["page_size_bytes"], 4096)
        self.assertEqual(value["commit_peak_scope"], "since-last-system-reboot")
        self.assertEqual(value["activity"]["status"], "not-applicable")
        for field, invalid in (("memory_page_size", 0), ("memory_page_size", 3000),
                               ("memory_page_size", 1 << 63), ("memory_available", 1001)):
            bad = WindowsMock()
            setattr(bad, field, invalid)
            with self.subTest(field=field, invalid=invalid), self.assertRaises(resources.EvidenceError):
                resources.WindowsReader(bad).memory()
        api.memory_ok = 0
        with mock.patch.object(ctypes, "get_last_error", return_value=5, create=True):
            value = resources.memory_read(resources.WindowsReader(api))
        self.assertEqual(value["status"], "error")
        self.assertIn("failed:5", value["error"])

    def test_darwin_memory_abi_page_units_count_and_host_release(self):
        self.assertEqual(ctypes.sizeof(resources.VmStatistics64), 160)
        self.assertEqual(resources.VmStatistics64.decompressions.offset, 96)
        self.assertEqual(resources.VmStatistics64.swapped.offset, 152)
        api = DarwinMock()
        source = resources.DarwinReader(api)
        value = source.memory()
        self.assertEqual(value["snapshot"]["page_size_bytes"], 16384)
        self.assertEqual(value["snapshot"]["values"]["free"], 1)
        self.assertEqual(value["snapshot"]["values"]["speculative"], 15)
        self.assertEqual(value["snapshot"]["values"]["compressor"], 20)
        self.assertEqual(value["activity"]["counters"]["compressions"], 17)
        self.assertEqual(api.ports, [(7, 9)])
        api.memory_count = 38
        partial = source.memory()
        self.assertEqual(partial["status"], "partial")
        self.assertEqual(partial["snapshot"]["values"]["swapped"], "unknown")
        for field, invalid in (("memory_count", 24), ("memory_count", 39), ("memory_count", 41),
                               ("memory_page_size", 0), ("memory_page_size", 3000),
                               ("memory_code", 5), ("memory_page_code", 5)):
            bad = DarwinMock()
            setattr(bad, field, invalid)
            with self.subTest(field=field, invalid=invalid), self.assertRaises(resources.EvidenceError):
                resources.DarwinReader(bad).memory()
            self.assertEqual(bad.ports, [(7, 9)])
        api = DarwinMock()
        api.port_code = 5
        self.assertEqual(resources.memory_read(resources.DarwinReader(api))["status"], "error")
        api = DarwinMock()
        original = api.call_host_statistics64
        def overflowing(host, flavor, output, count):
            result = original(host, flavor, output, count)
            output._obj.uncompressed_in_compressor = resources.MAX_COUNTER
            return result
        api.host_statistics64 = Function(overflowing)
        with self.assertRaisesRegex(resources.EvidenceError, "page-byte overflow"):
            resources.DarwinReader(api).memory()
        self.assertEqual(api.ports, [(7, 9)])

    def test_memory_cumulative_changes_refuse_wrap_and_coverage_drift(self):
        first = resources.linux_memory(MEMINFO, PSI)
        first.update(begin_ns=1, end_ns=2)
        last = copy.deepcopy(first)
        last.update(begin_ns=3, end_ns=4)
        last["activity"]["counters"] = {"some": 20, "full": 5}
        last["snapshot"]["values"]["MemAvailable"] = 600 * 1024
        self.assertEqual(resources.memory_delta(first, last)["counters"], {"some": 8, "full": 1})
        for change in (lambda v: v["activity"]["counters"].update(some=11),
                       lambda v: v["activity"]["counters"].update(some=True),
                       lambda v: v["snapshot"]["capacity"].update(MemTotal=1),
                       lambda v: v["snapshot"].update(unit="pages"),
                       lambda v: v.update(source="other"), lambda v: v.update(begin_ns=0),
                       lambda v: v["activity"]["counters"].update(other=0)):
            changed = copy.deepcopy(last)
            change(changed)
            with self.subTest(change=change), self.assertRaises(resources.EvidenceError):
                resources.memory_delta(first, changed)
        first["activity"]["counters"]["some"] = resources.MAX_COUNTER
        with self.assertRaisesRegex(resources.EvidenceError, "decreased/wrapped"):
            resources.memory_delta(first, last)

    def test_abi_sizes_and_windows_current_rss_coverage_cleanup(self):
        self.assertEqual(ctypes.sizeof(resources.FileTime), 8)
        self.assertEqual(ctypes.sizeof(resources.TaskInfo), 96)
        self.assertEqual(ctypes.sizeof(resources.MemoryCounters), 72 if ctypes.sizeof(resources.SIZE) == 8 else 40)
        api = WindowsMock()
        source = resources.WindowsReader(api)
        with mock.patch.object(resources.ctypes, "get_last_error", lambda: api.error, create=True):
            scan = source.rss()
        self.assertEqual(api.enum_calls, 2)
        self.assertEqual(scan["resident_bytes"], 5 * 1024**3)
        self.assertEqual((scan["enumerated"], scan["read"], scan["denied"], scan["vanished"], scan["errors"]), (5, 1, 2, 1, 1))
        self.assertEqual(api.closes, [(1 << 40) + 11, (1 << 40) + 14])
        self.assertEqual(scan["status"], "partial")
        api = WindowsMock()
        api.close_ok = 0
        with mock.patch.object(resources.ctypes, "get_last_error", lambda: api.error, create=True):
            failed = resources.WindowsReader(api).rss()
        self.assertEqual(failed["status"], "error")
        self.assertEqual(failed["resident_bytes"], "unknown")

    def test_windows_cpu_groups_and_kernel_idle_formula(self):
        api = WindowsMock()
        source = resources.WindowsReader(api)
        last = resources.cpu_read(source)
        first = json.loads(json.dumps(last))
        first["counters"] = {"idle": 0, "kernel": 0, "user": 0}
        self.assertEqual(resources.cpu_delta(first, last)["busy"], 5000)
        # Counter units are not a 100 ns precision promise: wall capacity is diagnostic.
        self.assertLess(resources.cpu_delta(first, last)["elapsed_capacity_ns"], 10**9)
        for groups in (0, 2):
            api.groups = groups
            self.assertEqual(resources.cpu_read(source)["status"], "error")

    def test_darwin_unsigned_ticks_current_rss_and_release_failures(self):
        api = DarwinMock()
        source = resources.DarwinReader(api)
        value = source.cpu()
        self.assertEqual(value["counters"]["0"]["user"], 0x80000010)
        self.assertEqual(value["frequency"], 100)
        self.assertEqual(api.releases[0][2], 32)
        self.assertEqual(api.ports, [(7, 9)])
        scan = source.rss()
        self.assertEqual(api.enum_calls, 2)
        self.assertEqual(scan["resident_bytes"], 5 * 1024**3)
        self.assertEqual((scan["read"], scan["denied"], scan["vanished"], scan["errors"]), (1, 1, 1, 1))
        api.elements = 7
        with self.assertRaises(resources.EvidenceError):
            source.cpu()
        self.assertEqual(len(api.releases), 2)
        self.assertEqual(len(api.ports), 2)
        api.elements, api.release_code = 8, 1
        with self.assertRaises(resources.EvidenceError):
            source.cpu()
        self.assertEqual(len(api.ports), 3)

    def test_cpu_wrap_guest_iowait_unknown_and_coverage_changes(self):
        first = {"status": "observed", "source": "darwin-host-processor-info", "frequency": 100,
                 "width": 32, "cpu_count": 1, "begin_ns": 0, "end_ns": 0,
                 "counters": {"0": {"user": 0xfffffff8, "system": 0, "idle": 0, "nice": 0}}}
        last = json.loads(json.dumps(first))
        last["begin_ns"], last["end_ns"], last["counters"]["0"]["user"] = 200_000_000, 200_000_000, 2
        self.assertEqual(resources.cpu_delta(first, last)["busy"], 100_000_000)
        last["counters"]["0"]["user"] = 10000
        with self.assertRaises(resources.EvidenceError):
            resources.cpu_delta(first, last)
        first.update(source="linux-proc-stat", width=64, cpu_count=4,
                     counters={"user": 10, "nice": 0, "system": 20, "idle": 30, "iowait": 40, "irq": 0, "softirq": 0, "guest": 5})
        last.update(source="linux-proc-stat", width=64, cpu_count=4,
                    counters={"user": 20, "nice": 0, "system": 30, "idle": 40, "iowait": 39, "irq": 0, "softirq": 0, "guest": 15})
        measured = resources.cpu_delta(first, last)
        self.assertEqual(measured["busy"], 200_000_000)
        self.assertEqual(measured["raw_delta"]["iowait"], -1)
        last["cpu_count"] = 2
        with self.assertRaises(resources.EvidenceError):
            resources.cpu_delta(first, last)
        with self.assertRaises(resources.EvidenceError):
            resources.cpu_delta(resources.unknown(), last)

    def test_missing_rss_is_unknown_and_overflow_stays_bounded(self):
        def missing(pid):
            raise OSError(errno.EACCES, "denied")
        failed = resources.scan_result("control", [1], missing)
        self.assertEqual(failed["resident_bytes"], "unknown")
        self.assertEqual(failed["denied"], 1)
        malformed = resources.scan_result("control", [1], lambda pid: int("malformed statm"))
        self.assertEqual(malformed["resident_bytes"], "unknown")
        self.assertEqual(malformed["errors"], 1)
        values = {1: resources.MAX_COUNTER, 2: 1}
        overflow = resources.scan_result("control", [1, 2], lambda pid: values[pid])
        self.assertEqual(overflow["resident_bytes"], resources.MAX_COUNTER)
        self.assertEqual(overflow["errors"], 1)
        with self.assertRaises(resources.EvidenceError):
            resources.scan_result("control", [1, 1], lambda pid: 0)

    def test_linux_enumeration_stops_at_first_excess_pid(self):
        entries = mock.MagicMock()
        consumed = []
        def generate():
            for name in ("self", "1", "2", "3", "4", "5"):
                consumed.append(name)
                yield argparse.Namespace(name=name)
        entries.__enter__.return_value = generate()
        with mock.patch.object(resources.os, "scandir", return_value=entries), \
             mock.patch.object(resources, "MAX_PIDS", 2):
            with self.assertRaises(resources.EvidenceError):
                resources.LinuxReader().rss()
        self.assertEqual(consumed, ["self", "1", "2", "3"])
        entries.__exit__.assert_called_once()

    @unittest.skipUnless(platform.system() in ("Linux", "Windows", "Darwin"), "supported OS API smoke")
    def test_actual_current_os_api_smoke(self):
        source = resources.reader()
        first = resources.cpu_read(source)
        self.assertEqual(first["status"], "observed")
        scan = resources.rss_read(source)
        self.assertIn(scan["status"], ("observed", "partial"))
        self.assertGreater(scan["read"], 0)
        self.assertGreater(scan["resident_bytes"], 0)
        memory_first = resources.memory_read(source)
        self.assertIn(memory_first["status"], ("observed", "partial"), memory_first)
        self.assertGreater(next(iter(memory_first["snapshot"]["capacity"].values())), 0)
        time.sleep(0.21)
        last = resources.cpu_read(source)
        delta = resources.cpu_delta(first, last)
        memory_last = resources.memory_read(source, memory_first["begin_ns"])
        self.assertEqual(memory_last["status"], memory_first["status"], memory_last)
        memory_change = resources.memory_delta(memory_first, memory_last)
        self.assertIn(memory_change["status"], ("observed", "not-applicable", "unsupported"))
        if memory_change["status"] == "unsupported":
            self.assertEqual(memory_first["status"], "partial")
            self.assertEqual(memory_last["activity"]["status"], "unsupported")
        self.assertGreaterEqual(delta["busy"], 0)
        print("RESOURCE_API_SMOKE " + json.dumps({"os": platform.system(), "cpu": delta, "rss": scan,
                                                  "os_memory_first": memory_first, "os_memory_last": memory_last,
                                                  "os_memory_change": memory_change}, sort_keys=True))

    @unittest.skipUnless(platform.system() in ("Linux", "Windows", "Darwin"), "supported OS sidecar smoke")
    def test_actual_detached_start_stop_and_idempotent_stop(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            path = root / "actual"
            script = Path(resources.__file__).absolute()
            env = os.environ.copy()
            env.update(fixture_environment())
            begin = [sys.executable, "-B", str(script), "start", "--session", str(path),
                     "--source-revision", "a" * 40, "--source-tree", "b" * 40, "--workflow-blob", "c" * 40,
                     "--role", "linux-x86_64", "--invocation", "checks"]
            ending = [sys.executable, "-B", str(script), "stop", "--session", str(path)]
            started = subprocess.run(begin, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
            try:
                self.assertEqual(started.returncode, 0, started.stderr.decode())
                self.assertEqual(json.loads(started.stdout)["status"], "ready")
                # This Python test touches memory; no payload subprocess or compiler is involved.
                touched = bytearray(4 * 1024**2)
                touched[::4096] = b"x" * (len(touched) // 4096)
                time.sleep(0.45)
            finally:
                stopped = subprocess.run(ending, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
            self.assertEqual(stopped.returncode, 0, stopped.stderr.decode())
            terminal = json.loads(stopped.stdout)
            self.assertEqual(terminal["status"], "complete")
            self.assertGreaterEqual(terminal["samples"], 2)
            self.assertGreater(terminal["maximum_observed_scan_sum_bytes"], 0)
            self.assertIn(terminal["os_memory"]["status"], ("observed", "partial"), terminal)
            self.assertEqual(terminal["os_memory"]["error_observations"], 0)
            self.assertEqual(terminal["os_memory"]["comparison_error_observations"], 0)
            self.assertEqual(terminal["os_memory"]["observations"], terminal["samples"] + 2)
            again = subprocess.run(ending, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
            self.assertEqual(again.returncode, 0, again.stderr.decode())
            self.assertEqual(json.loads(again.stdout), terminal)
            rows = [json.loads(line) for line in (path / "journal.jsonl").read_text().splitlines()]
            self.assertEqual(rows[0]["identity"]["source_revision"], "a" * 40)
            self.assertEqual(rows[-1], terminal)
            self.assertEqual(len([r for r in rows if r["event"] == "sample"]), terminal["samples"])
            self.assertTrue(all(row["os_memory"]["status"] in ("observed", "partial") for row in rows[:-1]))
            print("RESOURCE_SIDECAR_SMOKE synthetic_identity_only " + json.dumps(terminal, sort_keys=True))


if __name__ == "__main__":
    unittest.main()
