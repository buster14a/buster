#!/usr/bin/env python3
"""Opt-in external resource witness for #2120/#2119, never a payload launcher.

start/stop control only this observer. Session owns exclusive bounded receipts;
LinuxReader/WindowsReader/DarwinReader read OS counters and readable resident
sets; observe writes sequential scan evidence. See docs/ci-checks-resources.md.
No metric here constitutes resource acceptance or an exact simultaneous peak.
"""

import argparse
import ctypes
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import secrets
import stat
import subprocess
import sys
import time


SCHEMA = "buster-ci-checks-resources-v1"
CADENCE_NS = 200_000_000
HANDSHAKE_SECONDS = 10
MAX_SECONDS = 6 * 60 * 60
MAX_SAMPLES = MAX_SECONDS * 5 + 2
MAX_JOURNAL_BYTES = 64 * 1024 * 1024
MAX_RECEIPT_BYTES = 64 * 1024
MAX_PIDS = 65536
MAX_COUNTER = (1 << 64) - 1
REFS = tuple("refs/heads/codex/2120-evidence-v2-" + v for v in
             ("combined-overlap", "combined-all-builds", "split-overlap"))
ENV_KEYS = ("GITHUB_REPOSITORY", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB",
            "GITHUB_SHA", "GITHUB_REF", "GITHUB_EVENT_NAME", "GITHUB_WORKFLOW_REF",
            "GITHUB_WORKFLOW_SHA", "RUNNER_ENVIRONMENT", "RUNNER_NAME", "RUNNER_OS",
            "RUNNER_ARCH", "ImageOS", "ImageVersion")
LIMITATIONS = ("includes-os-background-and-observer", "readable-processes-only",
               "sequential-non-atomic-scan", "shared-pages-counted-repeatedly",
               "short-lived-processes-may-be-missed", "not-exact-simultaneous-peak",
               "not-a-guaranteed-lower-bound", "not-total-physical-memory")


class EvidenceError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def bounded_text(path, limit=MAX_RECEIPT_BYTES):
    with open(path, "rb") as handle:
        data = handle.read(limit + 1)
    require(len(data) <= limit, "OS text exceeds read limit")
    return data.decode("ascii", "strict")


def unknown(error="unavailable", status="error"):
    return {"status": status, "error": error}


def safe_fact(call):
    try:
        value = call()
    except (OSError, ValueError, EvidenceError):
        value = "unknown"
    return value


def checked_path(path):
    value = Path(os.path.abspath(path))
    for node in (value.anchor, *value.parts[1:]):
        if node == value.anchor:
            current = Path(node)
        else:
            current = current / node
        info = current.lstat()
        require(not stat.S_ISLNK(info.st_mode) and not
                getattr(info, "st_file_attributes", 0) & 0x400, "symlink/reparse path refused")
        require(stat.S_ISDIR(info.st_mode), "session path is not a directory")
    return value


class Session:
    def __init__(self, path, create=False):
        require(Path(path).is_absolute() and ".." not in Path(path).parts, "absolute nontraversal session path required")
        self.path = Path(os.path.abspath(path))
        if create:
            checked_path(self.path.parent)
            self.path.mkdir(mode=0o700)
        checked_path(self.path)
        self.fd = None
        if os.name != "nt":
            self.fd = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def open(self, name, write=False):
        require(re.fullmatch(r"[a-z]+\.(json|jsonl)", name), "invalid receipt name")
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL if write else os.O_RDONLY
        flags |= getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
        if self.fd is None:
            checked_path(self.path)
            fd = os.open(self.path / name, flags, 0o600)
        else:
            fd = os.open(name, flags, 0o600, dir_fd=self.fd)
        try:
            info = os.fstat(fd)
            require(stat.S_ISREG(info.st_mode), "receipt is not a regular file")
            if self.fd is None:
                named = (self.path / name).lstat()
                require(not stat.S_ISLNK(named.st_mode) and not
                        getattr(named, "st_file_attributes", 0) & 0x400,
                        "symlink/reparse receipt refused")
                require((info.st_dev, info.st_ino) == (named.st_dev, named.st_ino),
                        "receipt changed while opening")
        except BaseException:
            os.close(fd)
            raise
        return os.fdopen(fd, "wb" if write else "rb")

    def write(self, name, value):
        data = encode(value)
        require(len(data) <= MAX_RECEIPT_BYTES, "receipt exceeds limit")
        stage = "pending" + name
        handle = self.open(stage, write=True)
        try:
            with handle:
                handle.write(data)
                handle.flush()
                os.fsync(handle.fileno())
            if self.fd is None:
                checked_path(self.path)
                os.link(self.path / stage, self.path / name, follow_symlinks=False)
            else:
                os.link(stage, name, src_dir_fd=self.fd, dst_dir_fd=self.fd, follow_symlinks=False)
                os.fsync(self.fd)
        finally:
            if self.fd is None:
                (self.path / stage).unlink()
            else:
                os.unlink(stage, dir_fd=self.fd)

    def read(self, name):
        with self.open(name) as handle:
            data = handle.read(MAX_RECEIPT_BYTES + 1)
        require(len(data) <= MAX_RECEIPT_BYTES, "receipt exceeds limit")
        result = json.loads(data)
        require(isinstance(result, dict), "receipt must be an object")
        return result

    def optional(self, name):
        try:
            result = self.read(name)
        except FileNotFoundError:
            result = None
        return result


def encode(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def environment():
    result = {key: os.environ.get(key, "unknown") for key in ENV_KEYS}
    require(all(len(value) <= 512 and not any(ord(c) < 32 for c in value)
                for value in result.values()), "invalid environment observation")
    return result


def guard(env):
    require(os.environ.get("BUSTER_CI_CHECKS_RESOURCES") == "1", "resource observer is not opted in")
    require(env["GITHUB_EVENT_NAME"] == "workflow_dispatch" and env["GITHUB_REF"] in REFS,
            "resource observer requires an exact prospective dispatch ref")
    require(env["GITHUB_REPOSITORY"] == "buster14a/buster" and
            env["RUNNER_ENVIRONMENT"] == "github-hosted", "unexpected repository/runner environment")
    require(env["GITHUB_WORKFLOW_REF"] == "buster14a/buster/.github/workflows/ci.yml@" + env["GITHUB_REF"]
            and env["GITHUB_WORKFLOW_SHA"] == env["GITHUB_SHA"], "workflow source/ref mismatch")
    require(env["RUNNER_NAME"] != "unknown" and env["RUNNER_OS"] in ("Linux", "Windows", "macOS")
            and env["RUNNER_ARCH"] in ("X64", "ARM64"), "missing runner identity")


def identity(args, env):
    guard(env)
    for name in ("source_revision", "source_tree", "workflow_blob"):
        require(re.fullmatch(r"[0-9a-f]{40}", getattr(args, name)), "invalid " + name)
    require(args.source_revision == env["GITHUB_SHA"], "source revision differs from GitHub SHA")
    require(all(re.fullmatch(r"[1-9][0-9]*", env[k]) for k in ("GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT")),
            "missing run/attempt identity")
    require(re.fullmatch(r"[A-Za-z0-9_-]{1,80}", env["GITHUB_JOB"]), "invalid GitHub job key")
    require(re.fullmatch(r"[a-z0-9_-]{1,80}", args.role) and
            re.fullmatch(r"[a-z0-9-]{1,80}", args.invocation), "invalid matrix role/invocation")
    result = {"source_revision": args.source_revision, "source_tree": args.source_tree,
              "workflow_blob": args.workflow_blob, "environment": env,
              "role": args.role, "invocation": args.invocation,
              "workflow_path": ".github/workflows/ci.yml"}
    return result


def validate_client(meta, env):
    guard(env)
    require(meta.get("schema") == SCHEMA and isinstance(meta.get("session"), str) and
            re.fullmatch(r"[0-9a-f]{32}", meta["session"]) and isinstance(meta.get("identity"), dict),
            "invalid session metadata")
    saved = meta["identity"].get("environment")
    require(isinstance(saved, dict), "invalid stored environment")
    keys = ("GITHUB_REPOSITORY", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB",
            "GITHUB_SHA", "GITHUB_REF", "GITHUB_EVENT_NAME", "GITHUB_WORKFLOW_REF", "GITHUB_WORKFLOW_SHA",
            "RUNNER_ENVIRONMENT", "RUNNER_NAME",
            "RUNNER_OS", "RUNNER_ARCH")
    require(all(saved.get(k) == env[k] for k in keys),
            "stop client belongs to a different source/run/runner")


def scan_result(source, pids, read_one):
    require(len(pids) <= MAX_PIDS and len(set(pids)) == len(pids), "invalid PID enumeration")
    result = {"source": source, "status": "observed", "enumerated": len(pids),
              "read": 0, "denied": 0, "vanished": 0, "errors": 0, "resident_bytes": 0}
    for pid in pids:
        try:
            value = read_one(pid)
            require(type(value) is int and 0 <= value <= MAX_COUNTER, "invalid resident size")
            candidate = result["resident_bytes"] + value
            require(candidate <= MAX_COUNTER, "resident sum overflow")
            result["resident_bytes"] = candidate
            result["read"] += 1
        except (OSError, ValueError, EvidenceError) as error:
            code = getattr(error, "errno", None)
            key = "denied" if code in (errno.EACCES, errno.EPERM) else \
                  "vanished" if code in (errno.ENOENT, errno.ESRCH) else "errors"
            result[key] += 1
    if result["read"] == 0:
        result.update(status="error", resident_bytes="unknown")
    elif result["denied"] or result["vanished"] or result["errors"]:
        result["status"] = "partial"
    return result


class LinuxReader:
    def facts(self):
        result = {"cpu_count": os.cpu_count(), "physical_memory_bytes": safe_fact(
                    lambda: os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")),
                  "boot_identity": safe_fact(lambda: bounded_text("/proc/sys/kernel/random/boot_id", 80).strip()),
                  "pid_namespace": safe_fact(lambda: os.readlink("/proc/self/ns/pid")),
                  "proc_mounts": safe_fact(lambda: [line for line in bounded_text("/proc/mounts").splitlines()
                                                   if line.split()[1] == "/proc"])}
        return result

    def cpu(self):
        fields = bounded_text("/proc/stat").splitlines()[0].split()
        require(fields[0] == "cpu" and len(fields) >= 8, "invalid /proc/stat CPU row")
        values = [int(v) for v in fields[1:]]
        names = ("user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal", "guest", "guest_nice")
        require(len(values) <= len(names) and all(0 <= v <= MAX_COUNTER for v in values), "invalid CPU counters")
        result = {"source": "linux-proc-stat", "frequency": os.sysconf("SC_CLK_TCK"),
                  "width": 64, "cpu_count": os.cpu_count(), "counters": dict(zip(names, values))}
        return result

    def rss(self):
        pids = []
        with os.scandir("/proc") as entries:
            for entry in entries:
                if entry.name.isdecimal():
                    require(len(pids) < MAX_PIDS, "PID enumeration exceeds limit")
                    pids.append(int(entry.name))
        page_bytes = os.sysconf("SC_PAGE_SIZE")
        def read_one(pid):
            fields = bounded_text("/proc/" + str(pid) + "/statm", 256).split()
            require(len(fields) >= 2, "invalid statm")
            return int(fields[1]) * page_bytes
        return scan_result("linux-proc-statm", pids, read_one)


U32 = ctypes.c_uint32
I32 = ctypes.c_int32
U64 = ctypes.c_uint64
SIZE = ctypes.c_size_t
PTR = ctypes.c_void_p


class FileTime(ctypes.Structure):
    _fields_ = [("low", U32), ("high", U32)]


class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", U32), ("faults", U32)] + [(name, SIZE) for name in
               ("peak", "resident", "peak_paged", "paged", "peak_nonpaged", "nonpaged", "commit", "peak_commit")]


def bind(library, name, args, result):
    function = getattr(library, name)
    function.argtypes, function.restype = args, result
    return function


class WindowsReader:
    def __init__(self, library=None):
        self.api = library if library is not None else ctypes.WinDLL("kernel32", use_last_error=True)
        bind(self.api, "GetSystemTimes", [ctypes.POINTER(FileTime)] * 3, I32)
        bind(self.api, "GetActiveProcessorGroupCount", [], ctypes.c_uint16)
        bind(self.api, "GetActiveProcessorCount", [ctypes.c_uint16], U32)
        bind(self.api, "GetTickCount64", [], U64)
        bind(self.api, "GlobalMemoryStatusEx", [PTR], I32)
        bind(self.api, "K32EnumProcesses", [ctypes.POINTER(U32), U32, ctypes.POINTER(U32)], I32)
        bind(self.api, "OpenProcess", [U32, I32, U32], PTR)
        bind(self.api, "K32GetProcessMemoryInfo", [PTR, ctypes.POINTER(MemoryCounters), U32], I32)
        bind(self.api, "CloseHandle", [PTR], I32)

    def facts(self):
        class MemoryStatus(ctypes.Structure):
            _fields_ = [("length", U32), ("load", U32)] + [(n, U64) for n in
                       ("total_physical", "available_physical", "total_page", "available_page",
                        "total_virtual", "available_virtual", "available_extended")]
        memory = MemoryStatus()
        memory.length = ctypes.sizeof(memory)
        success = self.api.GlobalMemoryStatusEx(ctypes.byref(memory))
        result = {"cpu_count": int(self.api.GetActiveProcessorCount(0xffff)),
                  "processor_groups": int(self.api.GetActiveProcessorGroupCount()),
                  "physical_memory_bytes": int(memory.total_physical) if success else "unknown",
                  "boot_uptime_ms": int(self.api.GetTickCount64())}
        return result

    def cpu(self):
        groups, count = int(self.api.GetActiveProcessorGroupCount()), int(self.api.GetActiveProcessorCount(0xffff))
        require(groups != 0 and count != 0, "processor-group query failed")
        require(groups == 1 and count <= 64, "unsupported GetSystemTimes processor-group coverage")
        idle, kernel, user = FileTime(), FileTime(), FileTime()
        require(self.api.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)),
                "GetSystemTimes failed")
        counters = {name: int(value.high) * (1 << 32) + int(value.low) for name, value in
                    (("idle", idle), ("kernel", kernel), ("user", user))}
        result = {"source": "windows-get-system-times", "frequency": 10_000_000,
                  "width": 64, "cpu_count": count, "processor_groups": groups, "counters": counters}
        return result

    def rss(self):
        capacity, done, pids = 256, False, []
        while not done:
            require(capacity <= MAX_PIDS, "PID enumeration exceeds limit")
            buffer, needed = (U32 * capacity)(), U32()
            require(self.api.K32EnumProcesses(buffer, ctypes.sizeof(buffer), ctypes.byref(needed)),
                    "EnumProcesses failed")
            require(needed.value % 4 == 0 and needed.value <= ctypes.sizeof(buffer), "invalid PID byte count")
            done = needed.value < ctypes.sizeof(buffer)
            if done:
                pids = [int(buffer[i]) for i in range(needed.value // 4)]
            else:
                capacity *= 2
        def read_one(pid):
            if pid == 0:
                raise OSError(errno.EACCES, "System Idle Process is not readable")
            handle = self.api.OpenProcess(0x1000, 0, pid)
            if not handle:
                code = ctypes.get_last_error()
                mapped = errno.EACCES if code == 5 else errno.ESRCH if code in (87, 1168) else errno.EIO
                raise OSError(mapped, "OpenProcess failed")
            try:
                counters = MemoryCounters()
                counters.cb = ctypes.sizeof(counters)
                okay = self.api.K32GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
                code = ctypes.get_last_error() if not okay else 0
                if not okay:
                    raise OSError(errno.EACCES if code == 5 else errno.ESRCH if code in (87, 1168) else errno.EIO,
                                  "GetProcessMemoryInfo failed")
                value = int(counters.resident)
            finally:
                require(self.api.CloseHandle(handle), "process handle release failed")
            return value
        return scan_result("windows-k32-working-set-size", pids, read_one)


class TaskInfo(ctypes.Structure):
    _fields_ = [(n, U64) for n in ("virtual", "resident", "total_user", "total_system", "threads_user", "threads_system")] + \
               [("field" + str(i), I32) for i in range(12)]


class DarwinReader:
    def __init__(self, library=None):
        self.api = library if library is not None else ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
        bind(self.api, "mach_host_self", [], U32)
        bind(self.api, "host_processor_info", [U32, I32, ctypes.POINTER(U32),
                                               ctypes.POINTER(ctypes.POINTER(U32)), ctypes.POINTER(U32)], I32)
        bind(self.api, "vm_deallocate", [U32, SIZE, SIZE], I32)
        bind(self.api, "mach_port_deallocate", [U32, U32], I32)
        bind(self.api, "proc_listpids", [U32, U32, PTR, I32], I32)
        bind(self.api, "proc_pidinfo", [I32, I32, U64, PTR, I32], I32)
        bind(self.api, "sysctlbyname", [ctypes.c_char_p, PTR, ctypes.POINTER(SIZE), PTR, SIZE], I32)
        self.task = U32.in_dll(self.api, "mach_task_self_").value if library is None else library.task

    def sysctl(self, name, kind):
        value, size = kind(), SIZE(ctypes.sizeof(kind))
        require(self.api.sysctlbyname(name.encode("ascii"), ctypes.byref(value), ctypes.byref(size), None, 0) == 0
                and size.value == ctypes.sizeof(kind), "sysctl failed")
        return int(value.value)

    def facts(self):
        class Timeval(ctypes.Structure):
            _fields_ = [("seconds", ctypes.c_long), ("microseconds", I32)]
        value, size = Timeval(), SIZE(ctypes.sizeof(Timeval))
        okay = self.api.sysctlbyname(b"kern.boottime", ctypes.byref(value), ctypes.byref(size), None, 0)
        result = {"cpu_count": os.cpu_count(), "physical_memory_bytes": safe_fact(lambda: self.sysctl("hw.memsize", U64)),
                  "boot_time_seconds": int(value.seconds) if okay == 0 and size.value == ctypes.sizeof(Timeval) else "unknown"}
        return result

    def cpu(self):
        host, count, elements, output = self.api.mach_host_self(), U32(), U32(), ctypes.POINTER(U32)()
        try:
            code = self.api.host_processor_info(host, 2, ctypes.byref(count), ctypes.byref(output), ctypes.byref(elements))
            require(code == 0, "host_processor_info failed")
            require(0 < count.value <= 1024 and elements.value == count.value * 4 and bool(output),
                    "invalid processor CPU array")
            counters = {str(i): {name: int(output[i * 4 + j]) for j, name in
                                enumerate(("user", "system", "idle", "nice"))} for i in range(count.value)}
            result = {"source": "darwin-host-processor-info", "frequency": 100, "width": 32,
                      "cpu_count": count.value, "counters": counters}
        finally:
            array_released = 0
            if output:
                array_released = self.api.vm_deallocate(self.task, ctypes.cast(output, PTR).value, elements.value * 4)
            port_released = self.api.mach_port_deallocate(self.task, host)
            require(array_released == 0 and port_released == 0, "CPU array/host port release failed")
        return result

    def rss(self):
        capacity, done, pids = 256, False, []
        while not done:
            require(capacity <= MAX_PIDS, "PID enumeration exceeds limit")
            buffer = (I32 * capacity)()
            ctypes.set_errno(0)
            size = self.api.proc_listpids(1, 0, buffer, ctypes.sizeof(buffer))
            require(0 < size <= ctypes.sizeof(buffer) and size % 4 == 0, "proc_listpids failed or malformed")
            done = size < ctypes.sizeof(buffer)
            if done:
                pids = [int(buffer[i]) for i in range(size // 4)]
                require(all(pid >= 0 for pid in pids), "invalid PID enumeration")
            else:
                capacity *= 2
        def read_one(pid):
            info = TaskInfo()
            ctypes.set_errno(0)
            size = self.api.proc_pidinfo(pid, 4, 0, ctypes.byref(info), ctypes.sizeof(info))
            if size != ctypes.sizeof(info):
                raise OSError(ctypes.get_errno() or errno.EIO, "proc_pidinfo failed or short")
            return int(info.resident)
        return scan_result("darwin-proc-pidtaskinfo", pids, read_one)


def reader():
    name = platform.system()
    if name == "Linux":
        result = LinuxReader()
    elif name == "Windows":
        result = WindowsReader()
    elif name == "Darwin":
        result = DarwinReader()
    else:
        raise EvidenceError("unsupported observer OS")
    return result


def cpu_read(source):
    begin = time.monotonic_ns()
    try:
        result = source.cpu()
        result["status"] = "observed"
    except (OSError, ValueError, EvidenceError) as error:
        result = unknown(type(error).__name__ + ":" + str(error)[:160])
        result.update(source="unknown", frequency="unknown", width="unknown", cpu_count="unknown", counters="unknown")
    result.update(begin_ns=begin, end_ns=time.monotonic_ns(), scope="observed-os-instance")
    return result


def cpu_delta(first, last):
    require(first.get("status") == last.get("status") == "observed", "CPU endpoints unavailable")
    require(all(first[k] == last[k] for k in ("source", "frequency", "width", "cpu_count")), "CPU coverage changed")
    frequency, width, count = first["frequency"], first["width"], first["cpu_count"]
    require(type(frequency) is int and frequency > 0 and type(count) is int and count > 0, "invalid CPU units/coverage")
    elapsed = (last["end_ns"] - first["begin_ns"]) / 1_000_000_000
    require(0 <= elapsed <= MAX_SECONDS + HANDSHAKE_SECONDS, "invalid CPU observation window")
    limit = math.ceil(elapsed * frequency) + 2
    def difference(a, b, maximum):
        require(a.keys() == b.keys(), "CPU counter fields changed")
        value = {}
        for key in a:
            require(type(a[key]) is type(b[key]) is int and 0 <= a[key] < (1 << width)
                    and 0 <= b[key] < (1 << width), "invalid raw CPU counter")
            delta = (b[key] - a[key]) & ((1 << width) - 1) if width == 32 else b[key] - a[key]
            # iowait can decrease; it is preserved as a signed diagnostic, not CPU execution.
            require(key == "iowait" or delta >= 0, "CPU counter decreased")
            if width == 32:
                require(delta <= maximum, "impossible CPU counter wrap/delta")
            value[key] = delta
        return value
    if first["source"] == "darwin-host-processor-info":
        require(first["counters"].keys() == last["counters"].keys(), "processor inventory changed")
        raw = {cpu: difference(first["counters"][cpu], last["counters"][cpu], limit)
               for cpu in first["counters"]}
        busy = sum(sum(v[k] for k in ("user", "system", "nice")) for v in raw.values())
    else:
        raw = difference(first["counters"], last["counters"], limit * count)
        keys = ("user", "nice", "system", "irq", "softirq")
        busy = raw["user"] + raw["kernel"] - raw["idle"] if first["source"] == "windows-get-system-times" else sum(raw[k] for k in keys)
    require(busy >= 0 and (width != 32 or busy <= limit * count), "impossible busy CPU total")
    result = {"status": "observed", "source": first["source"], "scope": "observed-os-instance",
              "unit": "nanoseconds", "busy": busy * 1_000_000_000 // frequency,
              "raw_delta": raw, "counter_frequency": frequency, "cpu_count": count,
              "elapsed_capacity_ns": int(elapsed * count * 1_000_000_000)}
    return result


def rss_read(source, previous=None):
    begin = time.monotonic_ns()
    try:
        result = source.rss()
    except (OSError, ValueError, EvidenceError) as error:
        result = unknown(type(error).__name__ + ":" + str(error)[:160])
        result.update(source="unknown", resident_bytes="unknown", enumerated="unknown", read="unknown",
                      denied="unknown", vanished="unknown", errors="unknown")
    result.update(begin_ns=begin, end_ns=time.monotonic_ns(), unit="bytes",
                  scope="all-readable-processes", kind="sequential-resident-set-scan-sum",
                  gap_ns=begin - previous if previous is not None else "unknown")
    result["duration_ns"] = result["end_ns"] - begin
    return result


def matching(value, meta):
    require(value.get("schema") == SCHEMA and value.get("session") == meta["session"], "receipt session mismatch")


def observe(session, meta, source, max_seconds=MAX_SECONDS):
    with session.open("journal.jsonl", write=True) as handle:
        written = 0
        def append(value):
            nonlocal written
            data = encode(value)
            require(written + len(data) <= MAX_JOURNAL_BYTES - MAX_RECEIPT_BYTES, "journal byte limit reached")
            handle.write(data)
            handle.flush()
            written += len(data)
        start_ns = time.monotonic_ns()
        first = cpu_read(source)
        facts = source.facts()
        append({"schema": SCHEMA, "event": "start", "session": meta["session"],
                "identity": meta["identity"], "producer_sha256": meta["producer_sha256"],
                "observer_pid": os.getpid(), "wall_time_ns": time.time_ns(), "monotonic_ns": start_ns,
                "cadence_ns": CADENCE_NS, "maximum_seconds": max_seconds,
                "limits": {"samples": MAX_SAMPLES, "journal_bytes": MAX_JOURNAL_BYTES, "pids": MAX_PIDS},
                "limitations": LIMITATIONS, "os": {"system": platform.system(), "release": platform.release(),
                                                  "machine": platform.machine(), "facts": facts}, "cpu": first})
        count, previous, maximum, gaps, errors, partial = 0, None, None, 0, 0, 0
        stop_status, reason = "error", "maximum-observer-duration"
        done = False
        while not done:
            sample = rss_read(source, previous)
            append({"schema": SCHEMA, "event": "sample", "session": meta["session"],
                    "sequence": count, "rss": sample})
            count += 1
            previous = sample["begin_ns"]
            if sample["gap_ns"] != "unknown":
                gaps = max(gaps, sample["gap_ns"])
            if type(sample["resident_bytes"]) is int:
                maximum = sample["resident_bytes"] if maximum is None else max(maximum, sample["resident_bytes"])
            errors += sample["status"] == "error"
            partial += sample["status"] == "partial"
            if count == 1:
                session.write("ready.json", {"schema": SCHEMA, "session": meta["session"],
                                            "observer_pid": os.getpid(), "monotonic_ns": time.monotonic_ns()})
            request = session.optional("stop.json")
            if request is not None:
                matching(request, meta)
                stop_status, reason, done = "complete", "client-stop", True
            elif count >= MAX_SAMPLES or (time.monotonic_ns() - start_ns) >= max_seconds * 1_000_000_000:
                done = True
            if not done:
                remaining = CADENCE_NS - (time.monotonic_ns() - previous)
                if remaining > 0:
                    time.sleep(remaining / 1_000_000_000)
        last = cpu_read(source)
        try:
            total = cpu_delta(first, last)
        except EvidenceError as error:
            total = unknown(str(error))
            total.update(busy="unknown", unit="nanoseconds", scope="observed-os-instance")
        measurement = "error" if errors or total["status"] != "observed" else "partial" if partial else "observed"
        terminal = {"schema": SCHEMA, "event": "end", "session": meta["session"], "status": stop_status,
                    "reason": reason, "measurement_status": measurement, "monotonic_ns": time.monotonic_ns(),
                    "wall_time_ns": time.time_ns(), "samples": count, "maximum_gap_ns": gaps,
                    "error_samples": errors, "partial_samples": partial,
                    "maximum_observed_scan_sum_bytes": maximum if maximum is not None else "unknown",
                    "cpu_final": last, "cpu": total}
        data = encode(terminal)
        require(written + len(data) <= MAX_JOURNAL_BYTES, "terminal exceeds journal limit")
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    session.write("terminal.json", terminal)
    return terminal


def wait_receipt(session, name, meta, process=None):
    end, result = time.monotonic() + HANDSHAKE_SECONDS, None
    while result is None and time.monotonic() < end:
        result = session.optional(name)
        failure = session.optional("failure.json")
        require(failure is None, "observer failed before receipt")
        require(process is None or process.poll() is None or result is not None, "observer exited before readiness")
        if result is None:
            time.sleep(0.02)
    require(result is not None, "observer handshake timed out")
    matching(result, meta)
    return result


def cleanup_owned(process):
    action = "already-exited"
    if process.poll() is None:
        action = "terminate"
        process.terminate()
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            action = "kill"
            process.kill()
            process.wait(timeout=1)
    return {"action": action, "returncode": process.returncode}


def start(args):
    env = environment()
    result = {"status": "disabled"}
    if os.environ.get("BUSTER_CI_CHECKS_RESOURCES", "0") != "0":
        bound = identity(args, env)
        script = Path(__file__).absolute()
        producer = hashlib.sha256(script.read_bytes()).hexdigest()
        session = Session(args.session, create=True)
        process = None
        try:
            meta = {"schema": SCHEMA, "session": secrets.token_hex(16), "identity": bound,
                    "producer_sha256": producer, "observer_executable": os.path.abspath(sys.executable)}
            session.write("session.json", meta)
            options = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL,
                       "stderr": subprocess.DEVNULL, "close_fds": True}
            if os.name == "nt":
                options["creationflags"] = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                options["start_new_session"] = True
            process = subprocess.Popen([sys.executable, "-B", str(script), "_observe", "--session", str(session.path)], **options)
            ready = wait_receipt(session, "ready.json", meta, process)
            result = {"status": "ready", "session_path": str(session.path), **ready}
        except (OSError, ValueError, EvidenceError) as error:
            cleanup = {"action": "not-launched"}
            if process is not None:
                try:
                    cleanup = cleanup_owned(process)
                except (OSError, subprocess.TimeoutExpired) as cleanup_error:
                    cleanup = {"action": "error", "error": type(cleanup_error).__name__}
            session.write("startup.json", {"schema": SCHEMA, "session": meta["session"], "status": "error",
                                           "error": type(error).__name__ + ":" + str(error)[:160], "cleanup": cleanup})
            raise
        finally:
            session.close()
    return result


def stop(args):
    session = Session(args.session)
    try:
        meta = session.read("session.json")
        validate_client(meta, environment())
        require(session.optional("startup.json") is None and session.optional("failure.json") is None,
                "session contains observer/startup failure")
        terminal = session.optional("terminal.json")
        if terminal is None:
            request = session.optional("stop.json")
            if request is None:
                session.write("stop.json", {"schema": SCHEMA, "session": meta["session"],
                                           "requested_ns": time.monotonic_ns()})
            else:
                matching(request, meta)
            terminal = wait_receipt(session, "terminal.json", meta)
        matching(terminal, meta)
        require(terminal["status"] == "complete" and terminal["measurement_status"] != "error",
                "observer did not complete with readable resource evidence")
    finally:
        session.close()
    return terminal


def worker(args):
    session = Session(args.session)
    meta = {}
    try:
        meta = session.read("session.json")
        validate_client(meta, environment())
        require(meta["producer_sha256"] == hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "producer changed")
        observe(session, meta, reader())
    except (OSError, ValueError, KeyError, TypeError, EvidenceError) as error:
        try:
            session.write("failure.json", {"schema": SCHEMA, "session": meta.get("session", "unknown"),
                                           "status": "error", "error": type(error).__name__ + ":" + str(error)[:160]})
        except (OSError, EvidenceError):
            pass
        raise
    finally:
        session.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("start", "stop", "_observe"):
        command = commands.add_parser(name)
        command.add_argument("--session", required=True)
        if name == "start":
            for flag in ("source-revision", "source-tree", "workflow-blob", "role", "invocation"):
                command.add_argument("--" + flag, required=True)
    args = parser.parse_args(argv)
    code = 0
    try:
        if args.command == "start":
            result = start(args)
        elif args.command == "stop":
            result = stop(args)
        else:
            worker(args)
            result = None
        if result is not None:
            print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    except (OSError, ValueError, KeyError, EvidenceError) as error:
        print("resource observer: " + type(error).__name__ + ":" + str(error)[:200], file=sys.stderr)
        code = 1
    return code


if __name__ == "__main__":
    sys.exit(main())
