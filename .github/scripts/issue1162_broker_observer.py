#!/usr/bin/env python3
"""Bounded reference-free systemd broker lifecycle observer for issue #1162.

This file is deliberately a diagnostic helper, not a workflow or service
controller. It subscribes to the system manager before the caller starts the
broker socket, records only broker-instance lifecycle signals, and attempts
bounded property/process snapshots. It never starts, stops, signals, pauses, or
retains a unit. A fixed `systemctl show --all` property query can ask PID 1 to
load an inactive manager object; that possibility is recorded and no object
retention or service-start guarantee is claimed. Missing or raced snapshots
remain incomplete; the offline journal consumer must establish population.

The pull-mode sd-bus calls and UnitNew/UnitRemoved behavior were checked
against upstream systemd v255:
https://github.com/systemd/systemd/blob/v255/man/sd_bus_process.xml
https://github.com/systemd/systemd/blob/v255/src/libsystemd/sd-bus/sd-bus.c
https://github.com/systemd/systemd/blob/v255/src/libsystemd/sd-bus/bus-match.c
https://github.com/systemd/systemd/blob/v255/src/core/socket.c
https://github.com/systemd/systemd/blob/v255/src/core/dbus.c
https://github.com/systemd/systemd/blob/v255/src/core/manager.c
"""
from __future__ import annotations

import argparse
import ctypes
import errno
import hashlib
import json
import os
import re
import select
import signal
import stat
import subprocess
import sys
import tempfile
import time
import uuid
from collections import deque
from pathlib import Path
from typing import Callable


SCHEMA = "issue1162-broker-observer-v1"
SYSTEMCTL = "/usr/bin/systemctl"
UNIT_RE = re.compile(
    r"buster-bench-systemd-broker@"
    r"(0|[1-9][0-9]{0,19})-"  # Accept=yes connection instance counter
    r"(0|[1-9][0-9]{0,9})-"   # AF_UNIX peer PID
    r"(0|[1-9][0-9]{0,9})"    # AF_UNIX peer UID
    r"\.service\Z"
)
HEX32 = re.compile(r"[0-9a-f]{32}\Z")
UNIQUE_BUS_NAME = re.compile(r":[0-9]+\.[0-9]+\Z")
DECIMAL = re.compile(r"(?:0|[1-9][0-9]{0,19})\Z")
FDINFO_INODE = re.compile(rb"^ino:\s*([0-9]+)\s*$", re.MULTILINE)
PREEXEC_EXECUTABLES = frozenset(("/lib/systemd/systemd-executor",
                                  "/usr/lib/systemd/systemd-executor"))
BROKER_EXECUTABLE = "/usr/local/libexec/buster-bench-systemd-broker"
SUPERSEDABLE_CAPTURE_STATES = frozenset(("transient_proc_disappeared",
                                         "transient_proc_interrupted",
                                         "transient_capture_deadline",
                                         "known_preexec_executable_transition"))

MAX_RUN_SECONDS = 4000.0
MAX_EVENTS = 512
MAX_EVENTS_PER_UNIT = 128
# Attempt 21 observed ~46 unrelated manager messages/second over 89 seconds.
# Bound *all* traffic, including ignored messages, by both burst and sustained
# rates. A 4000-second run has at most 400 complete ten-second windows and one
# boundary second; the derived lifetime bound is a safety backstop, not a
# replacement for either window check.
MAX_DISPATCHES_PER_SECOND = 512
DISPATCH_WINDOW_SECONDS = 10
MAX_DISPATCHES_PER_WINDOW = 2560
MAX_DISPATCH_BUCKETS = int(MAX_RUN_SECONDS) + 1
MAX_BUS_DISPATCHES = (int(MAX_RUN_SECONDS) // DISPATCH_WINDOW_SECONDS *
                      MAX_DISPATCHES_PER_WINDOW + MAX_DISPATCHES_PER_SECOND)
MAX_DISPATCHES_PER_DRAIN = 64
MAX_GENERATIONS = 128
MAX_CONCURRENT_QUERIES = 1
MAX_SNAPSHOTS_PER_GENERATION = 6
MAX_PRESTOP_SNAPSHOTS_PER_GENERATION = 5
MAX_PROVISIONAL_CAPTURES_PER_GENERATION = 2
MAX_PROPERTY_SNAPSHOTS = MAX_GENERATIONS * MAX_SNAPSHOTS_PER_GENERATION
MAX_PROPERTIES = 512
MAX_PROPERTY_NAME_BYTES = 128
MAX_SHOW_BYTES = 256 * 1024
MAX_STDERR_BYTES = 32 * 1024
MAX_EVIDENCE_BYTES = 16 * 1024 * 1024
MAX_READY_BYTES = 64 * 1024
MAX_SUMMARY_BYTES = 4 * 1024 * 1024
MAX_DISPATCH_DIAGNOSTIC_BYTES = 32 * 1024
MAX_NONCANONICAL_SAMPLES = 4
SUMMARY_RESERVE_BYTES = MAX_READY_BYTES + MAX_SUMMARY_BYTES
SHOW_TIMEOUT_SECONDS = 2.0
TERMINAL_PHASE_SECONDS = 30.0
SELECT_QUANTUM_SECONDS = 0.10
MAX_PROC_STAT_BYTES = 1024 * 1024
MAX_PROC_STATUS_BYTES = 1024 * 1024
MAX_PROC_MOUNTINFO_BYTES = 16 * 1024 * 1024
MAX_PROC_CGROUP_BYTES = 1024 * 1024
MAX_FDINFO_BYTES = 32 * 1024

BASE_UNIT_PATH = "/org/freedesktop/systemd1/unit"
BASE_UNIT_PATH_ENCODED_BROKER_PREFIX = (
    "/org/freedesktop/systemd1/unit/"
    "buster_2dbench_2dsystemd_2dbroker_40"
)
TEMPLATE_METADATA_UNIT = "buster-bench-systemd-broker@internal.service"
TEMPLATE_METADATA_PATH = BASE_UNIT_PATH_ENCODED_BROKER_PREFIX + "internal_2eservice"
MATCH_RULES = (
    "type='signal',sender='org.freedesktop.systemd1',"
    "path='/org/freedesktop/systemd1',"
    "interface='org.freedesktop.systemd1.Manager',member='UnitNew'",
    "type='signal',sender='org.freedesktop.systemd1',"
    "path='/org/freedesktop/systemd1',"
    "interface='org.freedesktop.systemd1.Manager',member='UnitRemoved'",
    "type='signal',sender='org.freedesktop.systemd1',"
    "path='/org/freedesktop/systemd1',"
    "interface='org.freedesktop.systemd1.Manager',member='Reloading'",
    "type='signal',sender='org.freedesktop.systemd1',"
    "path_namespace='/org/freedesktop/systemd1/unit',"
    "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
)


class ObserverError(RuntimeError):
    pass


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise ObserverError(reason)


def _text(raw: bytes | None, label: str, limit: int = 4096) -> str:
    require(raw is not None and 0 < len(raw) <= limit and b"\0" not in raw,
            f"invalid {label}")
    try:
        value = raw.decode("utf-8", "strict")
    except UnicodeDecodeError as exc:
        raise ObserverError(f"non-UTF-8 {label}") from exc
    return value


def parse_broker_unit(unit: str) -> tuple[int, int, int]:
    match = UNIT_RE.fullmatch(unit)
    require(match is not None, "non-canonical broker instance unit")
    counter, peer_pid, peer_uid = (int(value) for value in match.groups())
    require(counter <= (1 << 64) - 1 and peer_pid <= (1 << 31) - 1 and
            peer_uid <= (1 << 32) - 1,
            "broker instance identity field is outside its systemd/kernel range")
    return counter, peer_pid, peer_uid


def systemctl_show_argv(unit: str) -> list[str]:
    parse_broker_unit(unit)
    return [SYSTEMCTL, "--system", "show", "--all", "--no-pager", "--", unit]


def normalized_boot_id(raw: str) -> tuple[str, str]:
    require(re.fullmatch(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}", raw) is not None,
            "guest boot ID is not canonical UUID36")
    parsed = uuid.UUID(raw)
    require(str(parsed) == raw, "guest boot ID UUID is not canonical")
    normalized = raw.replace("-", "")
    require(HEX32.fullmatch(normalized) is not None, "normalized guest boot ID is invalid")
    return normalized, raw


def _boot_id() -> tuple[str, str]:
    fd = os.open("/proc/sys/kernel/random/boot_id", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        raw = os.read(fd, 64)
        require(len(raw) <= 63 and os.read(fd, 1) == b"", "boot ID file exceeds bound")
    finally:
        os.close(fd)
    return normalized_boot_id(raw.decode("ascii").strip())


def _self_start_ticks() -> int:
    fd = os.open("/proc/self/stat", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        raw = bytearray()
        while len(raw) <= MAX_PROC_STAT_BYTES:
            block = os.read(fd, min(65536, MAX_PROC_STAT_BYTES + 1 - len(raw)))
            if not block:
                break
            raw.extend(block)
        require(0 < len(raw) <= MAX_PROC_STAT_BYTES and os.read(fd, 1) == b"",
                "observer /proc/self/stat exceeded bound")
    finally:
        os.close(fd)
    try:
        line = raw.decode("ascii", "strict")
        left = line.find("(")
        right = line.rfind(")")
        require(left > 0 and right > left and int(line[:left].strip()) == os.getpid(),
                "observer /proc/self/stat PID mismatch")
        tail = line[right + 2:].split()
        require(len(tail) > 19 and tail[0] in "RSDZTtXxKWPI", "observer proc stat fields are invalid")
        value = int(tail[19])
        require(value > 0, "observer process start ticks are invalid")
        return value
    except (UnicodeDecodeError, ValueError) as exc:
        raise ObserverError("observer /proc/self/stat is malformed") from exc


def _atomic_json_at(directory_fd: int, name: str, value: object, maximum: int) -> int:
    require(re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", name) is not None, "unsafe evidence filename")
    content = (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode()
    require(len(content) <= maximum, "atomic JSON record exceeds its byte bound")
    temporary = f".{name}.{os.getpid()}.tmp"
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                 0o600, dir_fd=directory_fd)
    try:
        view = memoryview(content)
        while view:
            count = os.write(fd, view)
            require(count > 0, "short atomic JSON write")
            view = view[count:]
        os.fsync(fd)
    finally:
        os.close(fd)
    try:
        os.link(temporary, name, src_dir_fd=directory_fd, dst_dir_fd=directory_fd,
                follow_symlinks=False)
        os.unlink(temporary, dir_fd=directory_fd)
        os.fsync(directory_fd)
    except BaseException:
        try:
            os.unlink(temporary, dir_fd=directory_fd)
        except OSError:
            pass
        raise
    return len(content)


class EvidenceWriter:
    def __init__(self, output: str):
        require(os.path.isabs(output), "--output must be absolute")
        current = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            for part in Path(output).parts[1:]:
                require(part not in ("", ".", ".."), "unsafe output path component")
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                                dir_fd=current)
                os.close(current)
                current = child
            info = os.fstat(current)
            require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and
                    stat.S_IMODE(info.st_mode) == 0o700,
                    "output directory must be root-owned mode 0700")
            require(os.listdir(current) == [], "output directory is not fresh")
            os.mkdir("captures", mode=0o700, dir_fd=current)
            self.root_fd = current
            current = -1
        finally:
            if current >= 0:
                os.close(current)
        self.capture_fd = os.open("captures", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                                  dir_fd=self.root_fd)
        self.event_fd = os.open("events.jsonl", os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                                os.O_CLOEXEC | os.O_NOFOLLOW, 0o600, dir_fd=self.root_fd)
        self.capture_bytes = 0
        self.event_count = 0
        self.event_bytes = 0
        self.artifact_bytes = 0

    def _charge(self, count: int) -> None:
        require(count >= 0 and self.artifact_bytes + count <=
                MAX_EVIDENCE_BYTES - SUMMARY_RESERVE_BYTES,
                "total evidence byte bound exceeded")
        self.artifact_bytes += count

    def event(self, value: dict[str, object]) -> int:
        require(self.event_count < MAX_EVENTS, "manager event count exceeds bound")
        line = (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode()
        require(len(line) <= 16 * 1024, "manager event record exceeds line bound")
        self._charge(len(line))
        view = memoryview(line)
        while view:
            count = os.write(self.event_fd, view)
            require(count > 0, "short manager event write")
            view = view[count:]
        os.fsync(self.event_fd)
        self.event_count += 1
        self.event_bytes += len(line)
        return len(line)

    def capture(self, name: str, content: bytes, maximum: int) -> dict[str, object]:
        require(re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", name) is not None,
                "unsafe capture filename")
        require(len(content) <= maximum, "capture exceeds per-file byte bound")
        self._charge(len(content))
        fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                     0o600, dir_fd=self.capture_fd)
        try:
            view = memoryview(content)
            while view:
                count = os.write(fd, view)
                require(count > 0, "short capture write")
                view = view[count:]
            os.fsync(fd)
        finally:
            os.close(fd)
        self.capture_bytes += len(content)
        return {"path": f"captures/{name}", "sha256": hashlib.sha256(content).hexdigest(),
                "bytes": len(content)}

    def capture_existing(self, name: str, maximum: int) -> dict[str, object]:
        require(re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", name) is not None,
                "unsafe existing capture filename")
        fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=self.capture_fd)
        try:
            before = os.fstat(fd)
            require(stat.S_ISREG(before.st_mode) and before.st_uid == 0 and
                    stat.S_IMODE(before.st_mode) == 0o600 and before.st_nlink == 1 and
                    0 <= before.st_size <= maximum,
                    "existing process capture has invalid metadata")
            content = bytearray()
            while len(content) <= maximum:
                part = os.read(fd, min(65536, maximum + 1 - len(content)))
                if not part:
                    break
                content.extend(part)
            after = os.fstat(fd)
            require(len(content) <= maximum and (before.st_dev, before.st_ino, before.st_size,
                    before.st_mtime_ns, before.st_ctime_ns) ==
                    (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                    "existing process capture changed or exceeded bound")
            require(len(content) == before.st_size, "existing process capture was short-read")
            raw = bytes(content)
        finally:
            os.close(fd)
        self._charge(len(raw))
        self.capture_bytes += len(raw)
        return {"path": f"captures/{name}", "sha256": hashlib.sha256(raw).hexdigest(),
                "bytes": len(raw)}

    def capture_path(self, name: str, path: Path, maximum: int,
                     exit_code: int | None = 0, timed_out: bool = False) -> dict[str, object]:
        fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            before = os.fstat(fd)
            require(stat.S_ISREG(before.st_mode) and before.st_uid == 0 and
                    stat.S_IMODE(before.st_mode) == 0o600 and before.st_nlink == 1 and
                    0 <= before.st_size <= maximum,
                    "temporary process capture has invalid metadata")
            content = bytearray()
            while len(content) <= maximum:
                part = os.read(fd, min(65536, maximum + 1 - len(content)))
                if not part:
                    break
                content.extend(part)
            after = os.fstat(fd)
            stable_before = (before.st_dev, before.st_ino, before.st_mode, before.st_nlink,
                             before.st_uid, before.st_gid, before.st_size,
                             before.st_mtime_ns, before.st_ctime_ns)
            stable_after = (after.st_dev, after.st_ino, after.st_mode, after.st_nlink,
                            after.st_uid, after.st_gid, after.st_size,
                            after.st_mtime_ns, after.st_ctime_ns)
            require(len(content) <= maximum and len(content) == before.st_size and
                    stable_before == stable_after,
                    "temporary process capture changed or exceeded bound")
        finally:
            os.close(fd)
        reference = self.capture(name, bytes(content), maximum)
        return {**reference, "exit": exit_code, "timed_out": timed_out}

    def publish_ready(self, value: dict[str, object]) -> None:
        self.artifact_bytes += _atomic_json_at(self.root_fd, "ready.json", value, MAX_READY_BYTES)

    def publish_summary(self, value: dict[str, object]) -> None:
        self.artifact_bytes += _atomic_json_at(self.root_fd, "summary.json", value, MAX_SUMMARY_BYTES)

    def close(self) -> None:
        for descriptor in (self.event_fd, self.capture_fd, self.root_fd):
            try:
                os.close(descriptor)
            except OSError:
                pass


class SdBus:
    """Minimal typed ctypes surface; no Python callback enters libsystemd."""

    def __init__(self):
        try:
            self.lib = ctypes.CDLL("libsystemd.so.0")
            self.libc = ctypes.CDLL(None)
        except OSError as exc:
            raise ObserverError(f"libsystemd unavailable: {exc}") from exc
        self._bind()
        self.bus = ctypes.c_void_p()
        self.slots: list[ctypes.c_void_p] = []
        self.armed = False
        self.manager_sender: str | None = None
        self.connection_unique_name: str | None = None
        self.last_dispatch_kind = "unclassified_without_message"

    def _function(self, name: str, arguments: list[object], result: object) -> object:
        function = getattr(self.lib, name)
        function.argtypes = arguments
        function.restype = result
        return function

    def _bind(self) -> None:
        pvoid = ctypes.c_void_p
        ppvoid = ctypes.POINTER(pvoid)
        self.open_system = self._function("sd_bus_open_system", [ppvoid], ctypes.c_int)
        self.get_unique_name = self._function("sd_bus_get_unique_name",
                                              [pvoid, ctypes.POINTER(ctypes.c_char_p)], ctypes.c_int)
        self.add_match = self._function("sd_bus_add_match", [pvoid, ppvoid, ctypes.c_char_p,
                                  pvoid, pvoid], ctypes.c_int)
        self.new_method_call = self._function("sd_bus_message_new_method_call",
            [pvoid, ppvoid, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
             ctypes.c_char_p], ctypes.c_int)
        self.call = self._function("sd_bus_call", [pvoid, pvoid, ctypes.c_uint64,
                                pvoid, ppvoid], ctypes.c_int)
        self.process = self._function("sd_bus_process", [pvoid, ppvoid], ctypes.c_int)
        self.bus_fd = self._function("sd_bus_get_fd", [pvoid], ctypes.c_int)
        self.bus_events = self._function("sd_bus_get_events", [pvoid], ctypes.c_int)
        self.bus_timeout = self._function("sd_bus_get_timeout", [pvoid,
                                      ctypes.POINTER(ctypes.c_uint64)], ctypes.c_int)
        self.message_unref = self._function("sd_bus_message_unref", [pvoid], pvoid)
        self.slot_unref = self._function("sd_bus_slot_unref", [pvoid], pvoid)
        self.bus_unref = self._function("sd_bus_unref", [pvoid], pvoid)
        self.get_type = self._function("sd_bus_message_get_type", [pvoid,
                                    ctypes.POINTER(ctypes.c_uint8)], ctypes.c_int)
        self.get_member = self._function("sd_bus_message_get_member", [pvoid], ctypes.c_char_p)
        self.get_interface = self._function("sd_bus_message_get_interface", [pvoid], ctypes.c_char_p)
        self.get_path = self._function("sd_bus_message_get_path", [pvoid], ctypes.c_char_p)
        self.get_sender = self._function("sd_bus_message_get_sender", [pvoid], ctypes.c_char_p)
        self.get_signature = self._function("sd_bus_message_get_signature", [pvoid, ctypes.c_int], ctypes.c_char_p)
        self.message_read = self._function("sd_bus_message_read", [pvoid, ctypes.c_char_p], ctypes.c_int)
        self.enter_container = self._function("sd_bus_message_enter_container",
                                      [pvoid, ctypes.c_char, ctypes.c_char_p], ctypes.c_int)
        self.exit_container = self._function("sd_bus_message_exit_container", [pvoid], ctypes.c_int)
        self.message_skip = self._function("sd_bus_message_skip", [pvoid, ctypes.c_char_p], ctypes.c_int)
        self.peek_type = self._function("sd_bus_message_peek_type", [pvoid,
                                      ctypes.POINTER(ctypes.c_char), ctypes.POINTER(ctypes.c_char_p)], ctypes.c_int)
        self.read_basic = self._function("sd_bus_message_read_basic", [pvoid, ctypes.c_char, pvoid], ctypes.c_int)
        self.path_encode = self._function("sd_bus_path_encode", [ctypes.c_char_p, ctypes.c_char_p,
                                      ctypes.POINTER(pvoid)], ctypes.c_int)
        self.libc.free.argtypes = [pvoid]
        self.libc.free.restype = None

    @staticmethod
    def _check(result: int, operation: str) -> None:
        require(result >= 0, f"libsystemd {operation} failed ({result})")

    def arm(self) -> None:
        self._check(self.open_system(ctypes.byref(self.bus)), "sd_bus_open_system")
        require(bool(self.bus.value), "sd_bus_open_system returned a null connection")
        unique_name = ctypes.c_char_p()
        self._check(self.get_unique_name(self.bus, ctypes.byref(unique_name)),
                    "sd_bus_get_unique_name")
        self.connection_unique_name = _text(unique_name.value, "observer connection unique name", 64)
        require(UNIQUE_BUS_NAME.fullmatch(self.connection_unique_name) is not None,
                "observer connection has no unique bus name")
        for rule in MATCH_RULES:
            slot = ctypes.c_void_p()
            self._check(self.add_match(self.bus, ctypes.byref(slot), rule.encode("ascii"), None, None),
                        "sd_bus_add_match")
            require(bool(slot.value), "sd_bus_add_match returned a null slot")
            self.slots.append(slot)
        method = ctypes.c_void_p()
        self._check(self.new_method_call(self.bus, ctypes.byref(method),
                    b"org.freedesktop.systemd1", b"/org/freedesktop/systemd1",
                    b"org.freedesktop.systemd1.Manager", b"Subscribe"),
                    "sd_bus_message_new_method_call")
        reply = ctypes.c_void_p()
        try:
            self._check(self.call(self.bus, method, 2_000_000, None, ctypes.byref(reply)),
                        "Manager.Subscribe")
            require(bool(reply.value), "Manager.Subscribe returned a null reply")
            sender = self._message_text(self.get_sender, reply, "Manager.Subscribe reply sender")
            require(UNIQUE_BUS_NAME.fullmatch(sender) is not None,
                    "Manager.Subscribe reply has no unique manager owner")
            self.manager_sender = sender
        finally:
            if method.value:
                self.message_unref(method)
            if reply.value:
                self.message_unref(reply)
        self.armed = True

    def object_path(self, unit: str) -> str:
        parse_broker_unit(unit)
        path = ctypes.c_void_p()
        self._check(self.path_encode(BASE_UNIT_PATH.encode("ascii"), unit.encode("ascii"),
                                    ctypes.byref(path)), "sd_bus_path_encode")
        require(bool(path.value), "sd_bus_path_encode returned null")
        try:
            return _text(ctypes.string_at(path), "encoded unit object path", 4096)
        finally:
            self.libc.free(path)

    @staticmethod
    def _message_text(function: Callable, message: ctypes.c_void_p, label: str) -> str:
        return _text(function(message), label)

    @staticmethod
    def _signal_metadata(sender: str, interface: str, member: str,
                         path: str, signature: str) -> str:
        # Only bounded header fields, never a message body, enter rejection text.
        def bounded(value: str) -> str:
            encoded = json.dumps(value[:128], ensure_ascii=True)
            return encoded if len(encoded) <= 256 else json.dumps(value[:16] + "...", ensure_ascii=True)

        return " ".join(f"{key}={bounded(value)}"
                        for key, value in (("sender", sender), ("interface", interface),
                                           ("member", member), ("path", path),
                                           ("signature", signature)))

    def _read_unit_path(self, message: ctypes.c_void_p) -> tuple[str, str]:
        unit = ctypes.c_char_p()
        path = ctypes.c_char_p()
        self._check(self.message_read(message, b"so", ctypes.byref(unit), ctypes.byref(path)),
                    "read UnitNew/UnitRemoved body")
        return _text(unit.value, "manager unit name", 255), _text(path.value, "manager unit path", 4096)

    def _read_string(self, message: ctypes.c_void_p, label: str) -> str:
        value = ctypes.c_char_p()
        self._check(self.message_read(message, b"s", ctypes.byref(value)), "read D-Bus string")
        return _text(value.value, label, 1024)

    def _read_invalidated(self, message: ctypes.c_void_p) -> list[str]:
        result = self.enter_container(message, b"a", b"s")
        self._check(result, "enter invalidated property array")
        names: list[str] = []
        while True:
            value = ctypes.c_char_p()
            result = self.message_read(message, b"s", ctypes.byref(value))
            self._check(result, "read invalidated property")
            if result == 0:
                break
            require(len(names) < MAX_PROPERTIES, "invalidated property count exceeds bound")
            names.append(_text(value.value, "invalidated property name", MAX_PROPERTY_NAME_BYTES))
        self._check(self.exit_container(message), "exit invalidated property array")
        return names

    def _read_changed(self, message: ctypes.c_void_p,
                      metadata: str) -> tuple[str, list[str], list[int], list[str]]:
        interface = self._read_string(message, "PropertiesChanged interface")
        require(interface in ("org.freedesktop.systemd1.Unit", "org.freedesktop.systemd1.Service"),
                "unexpected interface in unit PropertiesChanged; " + metadata +
                "; unit_interface=" + json.dumps(interface[:128], ensure_ascii=True))
        self._check(self.enter_container(message, b"a", b"{sv}"), "enter changed property array")
        names: list[str] = []
        main_pids: list[int] = []
        while True:
            result = self.enter_container(message, b"e", b"sv")
            self._check(result, "enter changed property dictionary entry")
            if result == 0:
                break
            name = self._read_string(message, "changed property name")
            require(len(names) < MAX_PROPERTIES and name not in names,
                    "changed property count or uniqueness bound failed")
            names.append(name)
            type_name = ctypes.c_char()
            contents = ctypes.c_char_p()
            self._check(self.peek_type(message, ctypes.byref(type_name), ctypes.byref(contents)),
                        "peek changed property variant")
            require(type_name.value == b"v" and contents.value is not None and
                    0 < len(contents.value) <= 128,
                    "changed property is not a bounded variant")
            signature = contents.value
            self._check(self.enter_container(message, b"v", signature), "enter changed property variant")
            inner_type = ctypes.c_char()
            inner_contents = ctypes.c_char_p()
            self._check(self.peek_type(message, ctypes.byref(inner_type), ctypes.byref(inner_contents)),
                        "peek variant payload")
            if name == "MainPID":
                require(inner_type.value == b"u" and signature == b"u",
                        "MainPID property does not have systemd uint32 type")
                value = ctypes.c_uint32()
                self._check(self.read_basic(message, b"u", ctypes.byref(value)), "read MainPID")
                main_pids.append(value.value)
            else:
                self._check(self.message_skip(message, signature), "skip property variant payload")
            self._check(self.exit_container(message), "exit changed property variant")
            self._check(self.exit_container(message), "exit changed property dictionary entry")
        self._check(self.exit_container(message), "exit changed property array")
        invalidated = self._read_invalidated(message)
        return interface, names, main_pids, invalidated

    def process_one(self) -> tuple[int, dict[str, object] | None]:
        self.last_dispatch_kind = "unclassified_without_message"
        message = ctypes.c_void_p()
        result = int(self.process(self.bus, ctypes.byref(message)))
        require(result >= 0, f"sd_bus_process failed ({result})")
        if not message.value:
            if result > 0:
                self.last_dispatch_kind = "positive_null"
            return result, None
        try:
            message_type = ctypes.c_uint8()
            self._check(self.get_type(message, ctypes.byref(message_type)), "get message type")
            require(message_type.value == 4, "matched D-Bus message is not a signal")
            member = self._message_text(self.get_member, message, "signal member")
            interface = self._message_text(self.get_interface, message, "signal interface")
            path = self._message_text(self.get_path, message, "signal object path")
            sender = self._message_text(self.get_sender, message, "signal sender")
            signature = self._message_text(lambda m: self.get_signature(m, 1), message, "signal signature")
            metadata = self._signal_metadata(sender, interface, member, path, signature)
            # The bus daemon may address NameAcquired directly to this connection
            # without a match rule. It is not a system manager lifecycle signal.
            if (sender == "org.freedesktop.DBus" and
                    interface == "org.freedesktop.DBus" and member == "NameAcquired" and
                    path == "/org/freedesktop/DBus" and signature == "s"):
                acquired = self._read_string(message, "observer NameAcquired name")
                require(acquired == self.connection_unique_name,
                        f"unexpected observer NameAcquired name; {metadata}")
                self.last_dispatch_kind = "observer_control"
                return result, None
            require(UNIQUE_BUS_NAME.fullmatch(sender) is not None and
                    sender == self.manager_sender,
                    f"system manager unique sender changed after Subscribe; {metadata}")
            if member == "Reloading":
                require(interface == "org.freedesktop.systemd1.Manager" and
                        path == "/org/freedesktop/systemd1" and signature == "b",
                        f"unexpected Manager.Reloading signal metadata; {metadata}")
                value = ctypes.c_int()
                self._check(self.message_read(message, b"b", ctypes.byref(value)),
                            "read Manager.Reloading boolean")
                require(value.value in (0, 1), "invalid Manager.Reloading boolean")
                return result, {"member": member, "interface": interface, "path": path,
                                "sender": sender, "reloading": bool(value.value)}
            if member in ("UnitNew", "UnitRemoved"):
                require(interface == "org.freedesktop.systemd1.Manager" and
                        path == "/org/freedesktop/systemd1" and signature == "so",
                        f"unexpected Manager unit-signal metadata; {metadata}")
                unit, unit_path = self._read_unit_path(message)
                return result, {"member": member, "interface": interface, "path": path,
                                "sender": sender, "signature": signature,
                                "unit": unit, "object_path": unit_path}
            if member == "PropertiesChanged":
                require(interface == "org.freedesktop.DBus.Properties" and
                        path.startswith(BASE_UNIT_PATH + "/") and signature == "sa{sv}as",
                        f"unexpected unit PropertiesChanged metadata; {metadata}")
                # The match covers all systemd units. Only broker-prefix paths
                # enter the broker-specific body parser; malformed broker paths
                # still reach the lifecycle's fail-closed orphan handling.
                if not path.startswith(BASE_UNIT_PATH_ENCODED_BROKER_PREFIX):
                    self.last_dispatch_kind = "ignored_unrelated_properties"
                    return result, None
                changed_interface, names, pids, invalidated = self._read_changed(message, metadata)
                return result, {"member": member, "interface": interface, "path": path,
                                "sender": sender, "unit_interface": changed_interface,
                                "changed_properties": names, "main_pids": pids,
                                "invalidated_properties": invalidated}
            raise ObserverError(f"unexpected signal delivered by observer match; {metadata}")
        finally:
            self.message_unref(message)

    def wait_ready(self, timeout: float) -> None:
        descriptor = int(self.bus_fd(self.bus))
        require(descriptor >= 0, "sd_bus_get_fd returned invalid descriptor")
        events = int(self.bus_events(self.bus))
        read_set = [descriptor] if events & select.POLLIN else []
        write_set = [descriptor] if events & select.POLLOUT else []
        select.select(read_set, write_set, [], timeout)

    def close(self) -> None:
        for slot in self.slots:
            if slot.value:
                self.slot_unref(slot)
        self.slots.clear()
        if self.bus.value:
            self.bus_unref(self.bus)
            self.bus = ctypes.c_void_p()


class Lifecycle:
    def __init__(self, unit: str, path: str, generation: int, first_seq: int, first_ns: int):
        self.unit = unit
        self.object_path = path
        self.generation = generation
        self.first_event_seq = first_seq
        self.last_event_seq = first_seq
        self.first_monotonic_ns = first_ns
        self.last_monotonic_ns = first_ns
        self.last_snapshot_finished_monotonic_ns = 0
        self.removed_event_seq: int | None = None
        self.removed_monotonic_ns: int | None = None
        self.removed = False
        self.triggers: set[str] = set()
        self.main_pid_values: set[int] = set()
        self.last_mainpid_zero_event_seq: int | None = None
        self.snapshots: list[dict[str, object]] = []
        self.provisional_captures: list[dict[str, object]] = []
        self.retained_failed_terminal: dict[str, object] | None = None
        self.incomplete_reasons: list[str] = []
        self.manager_incomplete_reasons: list[str] = []
        self.process_incomplete_reasons: list[str] = []
        self.preliminary_property_misses: list[dict[str, object]] = []
        self.invocation_id: str | None = None
        self.main_pid = 0
        self.process_started = False
        self.start_ticks: int | None = None
        self.cgroup: str | None = None
        self.exe: str | None = None
        self.exe_sha256: str | None = None
        self.result: str | None = None
        self.substate: str | None = None
        self.exec_main_status: str | None = None

    def mark_incomplete(self, reason: str) -> None:
        if reason not in self.incomplete_reasons:
            self.incomplete_reasons.append(reason)

    def mark_manager_incomplete(self, reason: str) -> None:
        self.mark_incomplete(reason)
        if reason not in self.manager_incomplete_reasons:
            self.manager_incomplete_reasons.append(reason)

    def mark_process_incomplete(self, reason: str) -> None:
        self.mark_incomplete(reason)
        if reason not in self.process_incomplete_reasons:
            self.process_incomplete_reasons.append(reason)

    def accept_invocation(self, invocation_id: str) -> bool:
        if not HEX32.fullmatch(invocation_id) or invocation_id == "0" * 32:
            return False
        if self.invocation_id is not None and self.invocation_id != invocation_id:
            return False
        self.invocation_id = invocation_id
        return True

    @staticmethod
    def positive_pid_identity(snapshot: dict[str, object]) -> tuple[object, ...] | None:
        invocation = snapshot.get("invocation_id")
        pid = snapshot.get("main_pid")
        started = snapshot.get("exec_main_start_timestamp_monotonic")
        cgroup = snapshot.get("cgroup")
        ticks = snapshot.get("start_ticks")
        if (not isinstance(invocation, str) or HEX32.fullmatch(invocation) is None or
                invocation == "0" * 32 or not isinstance(pid, int) or pid <= 0 or
                not isinstance(started, int) or started <= 0 or
                not isinstance(cgroup, str) or not cgroup.startswith("/") or
                not isinstance(ticks, int) or ticks <= 0):
            return None
        return (snapshot.get("boot_id"), snapshot.get("unit"),
                snapshot.get("object_path"), snapshot.get("generation"),
                invocation, pid, started, cgroup, ticks)

    def as_json(self, boot_id: str, boot_id_raw: str, event_loss: bool = False) -> dict[str, object]:
        full_earlier = [s for s in self.snapshots if bool(s.get("complete")) and
                        bool(s.get("manager_properties_complete")) and
                        bool(s.get("process_capture_complete")) and
                        self.positive_pid_identity(s) is not None]
        def redundant_unstarted(snapshot: dict[str, object]) -> bool:
            return (snapshot.get("post_removal_unstarted") is True and
                    self.removed_event_seq is not None and
                    self.removed_monotonic_ns is not None and
                    isinstance(snapshot.get("trigger_event_seq"), int) and
                    snapshot["trigger_event_seq"] < self.removed_event_seq and
                    isinstance(snapshot.get("started_monotonic_ns"), int) and
                    snapshot["started_monotonic_ns"] >= self.removed_monotonic_ns and
                    any(isinstance(w.get("finished_monotonic_ns"), int) and
                        0 < w["finished_monotonic_ns"] <=
                            self.removed_monotonic_ns for w in full_earlier))
        terminal = self.retained_failed_terminal
        terminal_valid = (terminal is not None and
                          bool(terminal.get("manager_properties_complete")) and
                          not terminal.get("incomplete_reasons") and
                          isinstance(terminal.get("finished_monotonic_ns"), int) and
                          len(self.snapshots) + 1 <= MAX_SNAPSHOTS_PER_GENERATION)
        if self.removed:
            closed = (self.removed_event_seq is not None and
                      (terminal is None or
                       (terminal_valid and self.removed_monotonic_ns is not None and
                        int(terminal["finished_monotonic_ns"]) <= self.removed_monotonic_ns)))
        else:
            closed = (terminal_valid and terminal["event_seq"] == self.last_event_seq)
        manager_complete = (closed and bool(self.snapshots) and
                            not self.manager_incomplete_reasons and not event_loss and
                            any(bool(s.get("manager_properties_complete")) for s in self.snapshots) and
                            all(bool(s.get("manager_properties_complete")) or
                                bool(s.get("preliminary_property_miss")) or
                                redundant_unstarted(s) for s in self.snapshots))
        positive_snapshots = [s for s in self.snapshots if isinstance(s.get("main_pid"), int) and
                              int(s["main_pid"]) > 0]
        witness_identities = {identity for s in positive_snapshots
                              if bool(s.get("complete")) and
                              bool(s.get("manager_properties_complete")) and
                              bool(s.get("process_capture_complete")) and
                              (identity := self.positive_pid_identity(s)) is not None}
        # An incomplete positive-PID proc read can be superseded only by a
        # complete independent witness of precisely the same process start.
        # A missing start-ticks read is deliberately not supersedable.
        positive_identities_match = (len(witness_identities) == 1 and
            all(self.positive_pid_identity(s) in witness_identities
                for s in positive_snapshots) and
            all(bool(s.get("complete")) or
                (s.get("process_capture_state") in SUPERSEDABLE_CAPTURE_STATES and
                 bool(s.get("manager_properties_complete")) and
                 not bool(s.get("process_capture_complete")))
                for s in positive_snapshots))
        process_reasons = list(self.process_incomplete_reasons)
        if len(witness_identities) == 1:
            witness_pid = next(iter(witness_identities))[5]
            if any(pid > 0 and pid != witness_pid for pid in self.main_pid_values):
                process_reasons.append("mainpid_signal_conflicts_with_process_witness")
        if not positive_identities_match:
            process_reasons.append("positive_mainpid_without_exact_full_process_witness")
        process_complete = (manager_complete and positive_identities_match and
                            not process_reasons)
        incomplete_reasons = list(self.incomplete_reasons)
        for reason in process_reasons:
            if reason not in incomplete_reasons:
                incomplete_reasons.append(reason)
        return {"boot_id": boot_id, "boot_id_raw": boot_id_raw, "unit": self.unit,
                "object_path": self.object_path, "generation": self.generation,
                "invocation_id": self.invocation_id, "main_pid": self.main_pid,
                "process_started": self.process_started, "start_ticks": self.start_ticks,
                "cgroup": self.cgroup, "exe": self.exe, "exe_sha256": self.exe_sha256,
                "result": self.result, "substate": self.substate,
                "exec_main_status": self.exec_main_status,
                "first_event_seq": self.first_event_seq, "last_event_seq": self.last_event_seq,
                "removed_event_seq": self.removed_event_seq,
                "first_event_monotonic_ns": self.first_monotonic_ns,
                "last_event_monotonic_ns": self.last_monotonic_ns,
                "removed_monotonic_ns": self.removed_monotonic_ns,
                "last_snapshot_finished_monotonic_ns": self.last_snapshot_finished_monotonic_ns,
                "manager_lifecycle_complete": manager_complete,
                "process_complete": process_complete,
                "process_start_seen": self.process_started or
                                      any(value > 0 for value in self.main_pid_values),
                "complete": process_complete,
                "manager_incomplete_reasons": list(self.manager_incomplete_reasons),
                "process_incomplete_reasons": process_reasons,
                "preliminary_property_misses": self.preliminary_property_misses,
                "incomplete_reasons": incomplete_reasons,
                "snapshots": self.snapshots,
                "provisional_captures": self.provisional_captures,
                "retained_failed_terminal": terminal}


def _parse_properties(raw: bytes, unit: str) -> dict[str, str]:
    try:
        text = raw.decode("utf-8", "strict")
    except UnicodeDecodeError as exc:
        raise ObserverError("systemctl show output is not UTF-8") from exc
    properties: dict[str, list[str]] = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        require(bool(sep) and re.fullmatch(r"[A-Za-z][A-Za-z0-9]+", key) is not None,
                "malformed systemctl show property line")
        require(len(properties) < MAX_PROPERTIES or key in properties,
                "systemctl property count exceeds bound")
        properties.setdefault(key, []).append(value)
    selected = ("Id", "LoadState", "ActiveState", "SubState", "MainPID", "InvocationID",
                "ControlGroup", "ExecMainStartTimestampMonotonic", "ExecMainStatus", "Result")
    result: dict[str, str] = {}
    for key in selected:
        values = properties.get(key, [])
        require(len(values) == 1, f"systemctl show property missing or duplicated: {key}")
        result[key] = values[0]
    require(result["Id"] == unit, "systemctl show unit identity mismatch")
    require(result["LoadState"] == "loaded", "systemctl show reports unit not loaded")
    require(DECIMAL.fullmatch(result["MainPID"]) is not None and
            int(result["MainPID"]) <= (1 << 31) - 1, "invalid systemctl MainPID")
    require(DECIMAL.fullmatch(result["ExecMainStartTimestampMonotonic"]) is not None,
            "invalid systemctl execution timestamp")
    require(re.fullmatch(r"[0-9]+", result["ExecMainStatus"]) is not None,
            "invalid systemctl ExecMainStatus")
    require(result["InvocationID"] == "" or
            re.fullmatch(r"[0-9a-f]{32}", result["InvocationID"]) is not None,
            "invalid systemctl InvocationID")
    control_group = result["ControlGroup"]
    require(control_group == "" or (control_group.startswith("/") and ".." not in control_group.split("/")),
            "invalid systemctl ControlGroup")
    return result


def _parse_terminal_properties(raw: bytes, unit: str) -> dict[str, str]:
    # Parse the complete all-show first, retaining its existing uniqueness and
    # identity checks; the terminal protocol exports only these selected keys.
    base = _parse_properties(raw, unit)
    extras = ("CollectMode", "ExecMainPID", "ExecMainCode",
              "ExecMainExitTimestampMonotonic")
    text = raw.decode("utf-8", "strict")
    values: dict[str, list[str]] = {key: [] for key in extras}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if separator and key in values:
            values[key].append(value)
    for key in extras:
        require(len(values[key]) == 1, f"terminal show property missing or duplicated: {key}")
    for key in ("ExecMainPID", "ExecMainCode", "ExecMainExitTimestampMonotonic"):
        require(DECIMAL.fullmatch(values[key][0]) is not None,
                f"invalid terminal show property: {key}")
    require(int(values["ExecMainPID"][0]) <= (1 << 31) - 1,
            "terminal ExecMainPID exceeds PID bound")
    selected = ("Id", "LoadState", "CollectMode", "ActiveState", "SubState",
                "MainPID", "InvocationID", "ExecMainPID", "ExecMainCode",
                "ExecMainStatus", "ExecMainStartTimestampMonotonic",
                "ExecMainExitTimestampMonotonic", "Result")
    combined = {**base, **{key: values[key][0] for key in extras}}
    return {key: combined[key] for key in selected}


def _safe_stop_file(writer: EvidenceWriter) -> bool:
    try:
        fd = os.open("stop", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                     dir_fd=writer.root_fd)
    except FileNotFoundError:
        return False
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and
                stat.S_IMODE(info.st_mode) == 0o600 and info.st_nlink == 1 and info.st_size == 0,
                "invalid observer stop marker")
        require(os.read(fd, 1) == b"", "observer stop marker is not empty")
    finally:
        os.close(fd)
    return True


class BrokerObserver:
    @staticmethod
    def _new_dispatch_diagnostics() -> dict[str, object]:
        # These five disjoint counters sum to bus_dispatch_count. Unit-signal
        # counters below are subsets of positive_with_message, not a partition.
        return {"positive_with_message": 0, "positive_null": 0,
                "ignored_unrelated_properties": 0, "observer_control": 0,
                "unclassified_without_message": 0,
                "subsets": {"ignored_unrelated_unit_new": 0,
                            "ignored_unrelated_unit_removed": 0,
                            "noncanonical_broker_unit_signals": 0},
                "noncanonical_samples": [], "noncanonical_samples_omitted": 0}

    def __init__(self, bus: SdBus, writer: EvidenceWriter, boot_id: str, boot_id_raw: str,
                 deadline: float, run_id: str | None, run_attempt: int | None):
        self.bus = bus
        self.writer = writer
        self.boot_id = boot_id
        self.boot_id_raw = boot_id_raw
        self.deadline = deadline
        self.run_id = run_id
        self.run_attempt = run_attempt
        self.event_seq = 0
        self.bus_dispatch_count = 0
        self.dispatch_diagnostics = self._new_dispatch_diagnostics()
        self.dispatch_buckets: list[dict[str, int]] = []
        self.dispatch_window: deque[tuple[int, int]] = deque()
        self.dispatch_window_total = 0
        self.last_dispatch_monotonic_ns = 0
        self.template_metadata_events: list[dict[str, object]] = []
        self.bus_budget_exhausted = False
        self.units: list[Lifecycle] = []
        self.current_by_path: dict[str, Lifecycle] = {}
        self.last_by_path: dict[str, Lifecycle] = {}
        self.generation_by_unit: dict[str, int] = {}
        self.event_count_by_unit: dict[str, int] = {}
        self.queue: deque[tuple[Lifecycle, str, int]] = deque()
        self.current_query: dict[str, object] | None = None
        self.global_reasons: list[str] = []
        self.event_loss_detected = False
        self.ready_ns = 0
        self.stop_seen = False
        self.stop_monotonic_ns: int | None = None
        self.terminal_phase_started_monotonic_ns: int | None = None
        self.terminal_phase_finished_monotonic_ns: int | None = None
        self.snapshot_number = 0
        self.provisional_number = 0
        self.observer_pid = 0
        self.observer_start_ticks = 0

    def mark_global(self, reason: str, event_loss: bool = False) -> None:
        if reason not in self.global_reasons:
            self.global_reasons.append(reason[:512])
        self.event_loss_detected = self.event_loss_detected or event_loss

    def _record_dispatch(self, message: dict[str, object] | None) -> bool:
        now_ns = time.monotonic_ns()
        self.last_dispatch_monotonic_ns = now_ns
        self.bus_dispatch_count += 1
        kind = ("positive_with_message" if message is not None else
                getattr(self.bus, "last_dispatch_kind", "unclassified_without_message"))
        if kind not in ("positive_with_message", "positive_null",
                        "ignored_unrelated_properties", "observer_control",
                        "unclassified_without_message"):
            kind = "unclassified_without_message"
        self.dispatch_diagnostics[kind] += 1
        second = max(0, (now_ns - self.ready_ns) // 1_000_000_000)
        if not self.dispatch_buckets or self.dispatch_buckets[-1]["second"] != second:
            self.dispatch_buckets.append({"second": second, "count": 0,
                                          "first_monotonic_ns": now_ns,
                                          "last_monotonic_ns": now_ns})
            self.dispatch_window.append((second, 0))
        bucket = self.dispatch_buckets[-1]
        bucket["count"] += 1
        bucket["last_monotonic_ns"] = now_ns
        window_second, window_count = self.dispatch_window.pop()
        self.dispatch_window.append((window_second, window_count + 1))
        self.dispatch_window_total += 1
        # A real sliding ten-second interval can intersect eleven aligned
        # second buckets. Charging all eleven is deliberately conservative;
        # the bounded ledger can then be independently replayed without a
        # per-message timestamp artifact approaching the evidence byte cap.
        while self.dispatch_window and second - self.dispatch_window[0][0] > DISPATCH_WINDOW_SECONDS:
            self.dispatch_window_total -= self.dispatch_window.popleft()[1]
        valid = (now_ns >= self.ready_ns and second < MAX_DISPATCH_BUCKETS and
                 bucket["count"] <= MAX_DISPATCHES_PER_SECOND and
                 self.dispatch_window_total <= MAX_DISPATCHES_PER_WINDOW and
                 self.bus_dispatch_count <= MAX_BUS_DISPATCHES)
        if not valid:
            self.bus_budget_exhausted = True
            self.mark_global("manager_bus_dispatch_rate_exceeded", event_loss=True)
        return valid

    def _record_noncanonical_unit(self, message: dict[str, object]) -> None:
        diagnostics = self.dispatch_diagnostics
        diagnostics["subsets"]["noncanonical_broker_unit_signals"] += 1
        samples = diagnostics["noncanonical_samples"]
        if len(samples) >= MAX_NONCANONICAL_SAMPLES:
            diagnostics["noncanonical_samples_omitted"] += 1
            return
        # The sender and manager signal header were authenticated by SdBus.
        # Retain only the two bounded identity arguments, never property data.
        sample = {key: message[key] for key in
                  ("sender", "interface", "member", "path", "signature",
                   "unit", "object_path")}
        for key, byte_limit in (("unit", 255), ("object_path", 4096)):
            value = str(sample[key]).encode("utf-8")
            if len(value) > byte_limit:
                sample[key] = value[:64].decode("utf-8", "replace")
                sample[key + "_truncated"] = True
                sample[key + "_utf8_bytes"] = len(value)
                sample[key + "_sha256"] = hashlib.sha256(value).hexdigest()
        candidate = {**diagnostics, "noncanonical_samples": [*samples, sample]}
        if len(json.dumps(candidate, ensure_ascii=True, separators=(",", ":")).encode()) <= \
                MAX_DISPATCH_DIAGNOSTIC_BYTES:
            samples.append(sample)
        else:
            diagnostics["noncanonical_samples_omitted"] += 1

    def _emit(self, body: dict[str, object], unit: str | None = None) -> tuple[int, int]:
        self.event_seq += 1
        if unit is not None:
            count = self.event_count_by_unit.get(unit, 0) + 1
            require(count <= MAX_EVENTS_PER_UNIT, "per-unit manager event count exceeds bound")
            self.event_count_by_unit[unit] = count
        query = self.current_query
        query_context = None
        if query is not None:
            current = query.get("lifecycle")
            if isinstance(current, Lifecycle):
                query_context = {"unit": current.unit, "generation": current.generation,
                                 "started_monotonic_ns": query.get("started_monotonic_ns")}
        event_ns = time.monotonic_ns()
        event = {"schema": SCHEMA, "boot_id": self.boot_id, "boot_id_raw": self.boot_id_raw,
                 "seq": self.event_seq, "monotonic_ns": event_ns,
                 "bus_dispatch_ordinal": self.bus_dispatch_count,
                 "bus_dispatch_monotonic_ns": getattr(self, "last_dispatch_monotonic_ns", event_ns),
                 "observer_show_query_inflight": query_context, **body}
        self.writer.event(event)
        return self.event_seq, event_ns

    def _schedule(self, lifecycle: Lifecycle, trigger: str, seq: int) -> None:
        if lifecycle.removed:
            lifecycle.mark_manager_incomplete("snapshot_trigger_after_unit_removed")
            return
        if trigger in lifecycle.triggers:
            return
        if len(lifecycle.triggers) >= MAX_PRESTOP_SNAPSHOTS_PER_GENERATION:
            lifecycle.mark_manager_incomplete("snapshot_attempt_count_exceeds_bound")
            return
        lifecycle.triggers.add(trigger)
        self.queue.append((lifecycle, trigger, seq))

    @staticmethod
    def _record_unstarted_query(lifecycle: Lifecycle,
                                snapshot: dict[str, object], reason: str) -> None:
        snapshot["incomplete_reasons"].append(reason)
        snapshot["finished_monotonic_ns"] = time.monotonic_ns()
        lifecycle.last_snapshot_finished_monotonic_ns = int(snapshot["finished_monotonic_ns"])
        removed_seq = lifecycle.removed_event_seq
        removed_ns = lifecycle.removed_monotonic_ns
        eligible = (reason == "unit_removed_before_snapshot" and
                    isinstance(removed_seq, int) and isinstance(removed_ns, int) and
                    int(snapshot["trigger_event_seq"]) < removed_seq and
                    int(snapshot["started_monotonic_ns"]) >= removed_ns and
                    any(bool(s.get("complete")) and
                        bool(s.get("manager_properties_complete")) and
                        bool(s.get("process_capture_complete")) and
                        Lifecycle.positive_pid_identity(s) is not None and
                        isinstance(s.get("finished_monotonic_ns"), int) and
                        0 < s["finished_monotonic_ns"] <= removed_ns
                        for s in lifecycle.snapshots if s is not snapshot))
        if reason == "unit_removed_before_snapshot":
            snapshot["post_removal_unstarted"] = True
        if eligible:
            lifecycle.mark_incomplete(reason)
        else:
            lifecycle.mark_manager_incomplete(reason)

    def _schedule_preexec_retry(self, lifecycle: Lifecycle, pid: int,
                                seq: int, event_ns: int) -> None:
        # A later same-PID Service signal can expose the completed Type=exec
        # transition after the first three immediate snapshots saw executor.
        # A retry does not turn a partial into proof; as_json still requires an
        # exact full witness for every positive-PID snapshot identity.
        if (pid <= 0 or lifecycle.removed or
                self.stop_seen or time.monotonic() >= self.deadline or
                any(trigger.startswith("preexec-retry:") for trigger in lifecycle.triggers) or
                len(lifecycle.triggers) >= MAX_PRESTOP_SNAPSHOTS_PER_GENERATION or
                not lifecycle.snapshots or lifecycle.manager_incomplete_reasons or
                lifecycle.process_incomplete_reasons or
                (self.current_query is not None and
                 self.current_query.get("lifecycle") is lifecycle) or
                any(row is lifecycle for row, _trigger, _seq in self.queue)):
            return
        latest = lifecycle.snapshots[-1]
        identity = Lifecycle.positive_pid_identity(latest)
        if (latest.get("process_capture_state") != "known_preexec_executable_transition" or
                latest.get("active_state") != "activating" or latest.get("substate") != "start" or
                latest.get("initial_exe_observed") not in PREEXEC_EXECUTABLES or
                latest.get("main_pid") != pid or identity is None or
                latest.get("finished_monotonic_ns", event_ns) >= event_ns or
                latest.get("trigger_event_seq", seq) >= seq or
                (lifecycle.last_mainpid_zero_event_seq is not None and
                 lifecycle.last_mainpid_zero_event_seq >= latest.get("trigger_event_seq", seq)) or
                lifecycle.invocation_id != latest.get("invocation_id") or
                lifecycle.main_pid != pid or lifecycle.cgroup != latest.get("cgroup")):
            return
        positive = [row for row in lifecycle.snapshots
                    if isinstance(row.get("main_pid"), int) and row["main_pid"] > 0]
        if any(Lifecycle.positive_pid_identity(row) != identity or
               (not row.get("complete") and
                row.get("process_capture_state") not in SUPERSEDABLE_CAPTURE_STATES)
               for row in positive):
            return
        self._schedule(lifecycle, f"preexec-retry:{len(lifecycle.snapshots)}", seq)

    def _unit_signal(self, message: dict[str, object]) -> None:
        member = str(message["member"])
        unit = str(message["unit"])
        path = str(message["object_path"])
        if unit == TEMPLATE_METADATA_UNIT:
            # An Accept=yes socket loads this fixed, non-peer template while
            # opening its listener for SELinux-label metadata. This paired
            # signal alone is not process evidence. Preserve the manager pair
            # for independent PID1/journal reconciliation.
            previous = self.template_metadata_events
            event_ns = time.monotonic_ns()
            ordinal = self.bus_dispatch_count
            valid_header = (path == TEMPLATE_METADATA_PATH and
                message.get("sender") == self.bus.manager_sender and
                message.get("interface") == "org.freedesktop.systemd1.Manager" and
                message.get("path") == "/org/freedesktop/systemd1" and
                message.get("signature") == "so")
            valid_order = (not self.units and not self.stop_seen and
                self.ready_ns < event_ns and 0 < ordinal <= MAX_BUS_DISPATCHES and
                ((not previous and member == "UnitNew") or
                 (len(previous) == 1 and member == "UnitRemoved" and
                  previous[0]["bus_dispatch_ordinal"] < ordinal and
                  previous[0]["monotonic_ns"] < event_ns)))
            if not valid_header or not valid_order:
                self._record_noncanonical_unit(message)
                self.mark_global("invalid_template_metadata_signal", event_loss=True)
                return
            previous.append({"boot_id": self.boot_id, "boot_id_raw": self.boot_id_raw,
                "unit": unit, "object_path": path, "sender": message["sender"],
                "member": member, "interface": message["interface"],
                "path": message["path"], "signature": message["signature"],
                "bus_dispatch_ordinal": ordinal,
                "bus_dispatch_monotonic_ns": self.last_dispatch_monotonic_ns,
                "monotonic_ns": event_ns})
            return
        try:
            parse_broker_unit(unit)
        except ObserverError:
            if (unit.startswith("buster-bench-systemd-broker@") or
                    path.startswith(BASE_UNIT_PATH_ENCODED_BROKER_PREFIX)):
                self._record_noncanonical_unit(message)
                self.mark_global("noncanonical_broker_unit_signal", event_loss=True)
            else:
                key = ("ignored_unrelated_unit_new" if member == "UnitNew" else
                       "ignored_unrelated_unit_removed")
                self.dispatch_diagnostics["subsets"][key] += 1
            return
        expected = self.bus.object_path(unit)
        require(path == expected, "manager unit signal path does not encode its exact unit name")
        if member == "UnitNew":
            previous = self.current_by_path.get(path)
            require(previous is None or previous.removed, "duplicate UnitNew without UnitRemoved")
            require(len(self.units) < MAX_GENERATIONS,
                    "broker lifecycle generation population exceeds bound")
            generation = self.generation_by_unit.get(unit, 0) + 1
            self.generation_by_unit[unit] = generation
            seq, event_ns = self._emit({"event": "UnitNew", "member": member,
                              "interface": message["interface"], "sender": message["sender"],
                              "unit": unit, "object_path": path,
                              "generation": generation}, unit)
            if self.template_metadata_events and \
                    self.template_metadata_events[-1]["monotonic_ns"] >= event_ns:
                self.mark_global("template_metadata_not_before_broker", event_loss=True)
            lifecycle = Lifecycle(unit, path, generation, seq, event_ns)
            self.units.append(lifecycle)
            self.current_by_path[path] = lifecycle
            self.last_by_path[path] = lifecycle
            self._schedule(lifecycle, "unit-new", seq)
            return
        lifecycle = self.current_by_path.get(path)
        if lifecycle is None:
            previous = self.last_by_path.get(path)
            seq, event_ns = self._emit({"event": "UnitRemoved", "member": member,
                              "interface": message["interface"], "sender": message["sender"],
                              "unit": unit, "object_path": path,
                              "generation": previous.generation if previous else None,
                              "orphan": True}, unit)
            if previous is not None:
                previous.last_event_seq = seq
                previous.last_monotonic_ns = event_ns
                previous.mark_manager_incomplete("orphan_unit_removed")
            else:
                self.mark_global("orphan_unit_removed", event_loss=True)
            return
        seq, event_ns = self._emit({"event": "UnitRemoved", "member": member,
                          "interface": message["interface"], "sender": message["sender"],
                          "unit": unit, "object_path": path,
                          "generation": lifecycle.generation}, unit)
        lifecycle.removed = True
        lifecycle.removed_event_seq = seq
        lifecycle.last_event_seq = seq
        lifecycle.last_monotonic_ns = event_ns
        lifecycle.removed_monotonic_ns = lifecycle.last_monotonic_ns
        self.current_by_path.pop(path, None)
        query = self.current_query
        if query is not None and query.get("lifecycle") is lifecycle:
            query["cancel_reason"] = "unit_removed_during_snapshot"

    def _property_signal(self, message: dict[str, object]) -> None:
        path = str(message["path"])
        if path == TEMPLATE_METADATA_PATH:
            self.mark_global("template_metadata_properties_changed", event_loss=True)
            return
        lifecycle = self.current_by_path.get(path)
        if lifecycle is None:
            if path.startswith(BASE_UNIT_PATH_ENCODED_BROKER_PREFIX):
                previous = self.last_by_path.get(path)
                self.mark_global("orphan_broker_properties_changed", event_loss=True)
                if previous is not None:
                    previous.mark_manager_incomplete("properties_changed_outside_live_generation")
            return
        changed = list(message["changed_properties"])
        invalidated = list(message["invalidated_properties"])
        main_pids = list(message["main_pids"])
        seq, event_ns = self._emit({"event": "PropertiesChanged", "member": "PropertiesChanged",
                          "interface": "org.freedesktop.DBus.Properties",
                          "unit_interface": message["unit_interface"],
                          "sender": message["sender"], "unit": lifecycle.unit,
                          "object_path": path, "generation": lifecycle.generation,
                          "changed_properties": changed,
                          "invalidated_properties": invalidated,
                          "main_pids": main_pids}, lifecycle.unit)
        lifecycle.last_event_seq = seq
        lifecycle.last_monotonic_ns = event_ns
        self._schedule(lifecycle, "properties-first", seq)
        if "MainPID" in changed or "MainPID" in invalidated:
            for pid in main_pids or [None]:
                key = -1 if pid is None else int(pid)
                if key == 0:
                    lifecycle.last_mainpid_zero_event_seq = seq
                if key not in lifecycle.main_pid_values:
                    lifecycle.main_pid_values.add(key)
                    self._schedule(lifecycle, "mainpid:" + str(key), seq)
        if (message["unit_interface"] == "org.freedesktop.systemd1.Service" and
                "MainPID" in changed and len(main_pids) == 1 and main_pids[0] > 0):
            self._capture_positive_signal(lifecycle, int(main_pids[0]), seq, event_ns)
            self._schedule_preexec_retry(lifecycle, int(main_pids[0]), seq, event_ns)

    def _capture_positive_signal(self, lifecycle: Lifecycle, pid: int,
                                 seq: int, event_ns: int) -> None:
        # The authenticated signal is only a hint until an independently
        # successful all-show binds PID, cgroup, invocation and exec start.
        # Repeated signals are eligible for one retry only after a transient
        # or known pre-exec capture failure; no timer or broker control exists.
        prior = lifecycle.provisional_captures
        if (lifecycle.removed or self.stop_seen or time.monotonic() >= self.deadline or
                len(prior) >= MAX_PROVISIONAL_CAPTURES_PER_GENERATION or
                (prior and (prior[-1]["main_pid"] != pid or
                 prior[-1]["capture_state"] not in SUPERSEDABLE_CAPTURE_STATES))):
            return
        self.provisional_number += 1
        label = f"signal-{self.provisional_number:06d}"
        record: dict[str, object] = {"trigger_event_seq": seq,
            "signal_monotonic_ns": event_ns, "started_monotonic_ns": time.monotonic_ns(),
            "finished_monotonic_ns": None, "main_pid": pid,
            "start_ticks": None, "cgroup": None, "proc_cgroup_raw": None,
            "initial_exe_observed": None, "exe": None, "exe_sha256": None,
            "proc_capture": {}, "socket_fd0": None, "capture_complete": False,
            "capture_state": None, "capture_error": None,
            "bound_snapshot_index": None}
        prior.append(record)
        try:
            import issue1162_live_probe as live_probe
            raw_cgroup = live_probe._bounded_file(Path(f"/proc/{pid}/cgroup"),
                                                    MAX_PROC_CGROUP_BYTES, self.deadline, reserve=0.0)
            cgroups = raw_cgroup.decode("ascii", "strict").splitlines()
            require(len(cgroups) == 1 and cgroups[0].startswith("0::/") and
                    ".." not in cgroups[0][3:].split("/"),
                    "signal process has no single canonical cgroup-v2 path")
            record["cgroup"] = cgroups[0][3:]
            ref = self.writer.capture(f"{label}.proc-cgroup-signal", raw_cgroup,
                                      MAX_PROC_CGROUP_BYTES)
            record["proc_capture"]["proc-cgroup-signal"] = {
                **ref, "exit": 0, "timed_out": False}
            self._capture_process(lifecycle, record, pid, self.provisional_number,
                                  label=label, bind_lifecycle=False)
            record["capture_complete"] = True
            record["capture_state"] = "full_process_witness"
        except (OSError, ObserverError, ValueError, RuntimeError, UnicodeError) as exc:
            record["capture_state"] = self._capture_failure_state(
                exc, record, pid, self.provisional_number, label=label,
                signal_phase=True)
            record["capture_error"] = f"{type(exc).__name__}:{str(exc)[:300]}"
            if record["capture_state"] == "identity_contradiction_or_capture_error":
                lifecycle.mark_process_incomplete("signal_process_identity_contradiction_or_capture_error")
        finally:
            record["finished_monotonic_ns"] = time.monotonic_ns()

    def handle_message(self, message: dict[str, object]) -> None:
        member = str(message["member"])
        if member in ("UnitNew", "UnitRemoved"):
            self._unit_signal(message)
        elif member == "PropertiesChanged":
            self._property_signal(message)
        elif member == "Reloading":
            self._emit({"event": "Reloading", "member": member,
                        "interface": message["interface"], "path": message["path"],
                        "sender": message["sender"],
                        "reloading": message["reloading"]})
            self.mark_global("manager_reloading_during_observation", event_loss=True)
        else:
            raise ObserverError("unrecognized systemd observer message")

    def _current_process_events(self) -> None:
        for _ in range(MAX_DISPATCHES_PER_DRAIN):
            result, message = self.bus.process_one()
            if result == 0:
                break
            if not self._record_dispatch(message):
                return
            if message is not None:
                self.handle_message(message)

    def _start_next_query(self) -> None:
        if self.current_query is not None:
            return
        # A positive PID has a much shorter live interval than redundant
        # UnitNew/first-property reads. The fixed queue stays bounded by the
        # existing per-generation trigger and snapshot limits.
        for index, (_row, trigger, _seq) in enumerate(self.queue):
            if trigger.startswith("mainpid:") and trigger != "mainpid:0" and trigger != "mainpid:-1":
                self.queue.rotate(-index)
                break
        while self.queue:
            lifecycle, trigger, seq = self.queue.popleft()
            snapshot: dict[str, object] = {"boot_id": self.boot_id,
                "boot_id_raw": self.boot_id_raw, "unit": lifecycle.unit,
                "object_path": lifecycle.object_path, "generation": lifecycle.generation,
                "trigger": trigger, "trigger_event_seq": seq,
                "started_monotonic_ns": time.monotonic_ns(), "complete": False,
                "manager_object_reload_possible": True,
                "process_start_observed_before_query": any(value > 0 for value in lifecycle.main_pid_values) or
                                                       lifecycle.process_started,
                "manager_properties_complete": False, "process_capture_complete": False,
                "incomplete_reasons": [], "main_pid": 0, "process_started": False,
                "invocation_id": None, "invocation_id_observed": None,
                "start_ticks": None, "cgroup": None, "exe": None, "exe_sha256": None,
                "result": None, "active_state": None, "substate": None,
                "exec_main_status": None,
                "raw_show": None, "raw_stderr": None, "proc_capture": {},
                "socket_fd0": None, "systemctl_exit": None,
                "systemctl_timed_out": False, "systemctl_timeout_reason": None,
                "systemctl_cancelled": None}
            lifecycle.snapshots.append(snapshot)
            if lifecycle.removed:
                reason = "unit_removed_before_snapshot"
                self._record_unstarted_query(lifecycle, snapshot, reason)
                continue
            require(self.snapshot_number < MAX_PROPERTY_SNAPSHOTS,
                    "total broker property snapshots exceed bound")
            self.snapshot_number += 1
            args = systemctl_show_argv(lifecycle.unit)
            env = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C",
                   "SYSTEMD_COLORS": "0", "SYSTEMD_PAGER": "cat", "PAGER": "cat"}
            try:
                process = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd="/",
                    close_fds=True, start_new_session=True, shell=False, bufsize=0)
            except OSError as exc:
                reason = f"systemctl_spawn_failed:{type(exc).__name__}"
                snapshot["incomplete_reasons"].append(reason)
                lifecycle.mark_manager_incomplete(reason)
                if snapshot["process_start_observed_before_query"]:
                    lifecycle.mark_process_incomplete(reason)
                snapshot["systemctl_exit"] = None
                snapshot["systemctl_timed_out"] = False
                snapshot["finished_monotonic_ns"] = time.monotonic_ns()
                lifecycle.last_snapshot_finished_monotonic_ns = int(snapshot["finished_monotonic_ns"])
                continue
            assert process.stdout is not None and process.stderr is not None
            os.set_blocking(process.stdout.fileno(), False)
            os.set_blocking(process.stderr.fileno(), False)
            self.current_query = {"lifecycle": lifecycle, "snapshot": snapshot,
                "process": process, "stdout": bytearray(), "stderr": bytearray(),
                "open": {process.stdout.fileno(): "stdout", process.stderr.fileno(): "stderr"},
                "deadline": time.monotonic() + SHOW_TIMEOUT_SECONDS,
                "started_monotonic_ns": snapshot["started_monotonic_ns"],
                "capture_number": self.snapshot_number, "cancel_reason": None,
                "limit_reason": None}
            return

    def _start_terminal_query(self, lifecycle: Lifecycle,
                              phase_deadline: float) -> None:
        require(self.stop_seen and not lifecycle.removed and self.current_query is None,
                "terminal query requires a retained generation after normal stop")
        require(len(lifecycle.snapshots) < MAX_SNAPSHOTS_PER_GENERATION and
                self.snapshot_number < MAX_PROPERTY_SNAPSHOTS,
                "terminal query would exceed shared snapshot bound")
        self.snapshot_number += 1
        terminal: dict[str, object] = {
            "boot_id": self.boot_id, "boot_id_raw": self.boot_id_raw,
            "unit": lifecycle.unit, "object_path": lifecycle.object_path,
            "generation": lifecycle.generation,
            "manager_sender": self.bus.manager_sender,
            "event_seq": lifecycle.last_event_seq,
            "started_monotonic_ns": time.monotonic_ns(),
            "finished_monotonic_ns": None,
            "raw_show": None, "raw_stderr": None,
            "systemctl_exit": None, "systemctl_timed_out": False,
            "systemctl_timeout_reason": None, "systemctl_cancelled": None,
            "manager_properties_complete": False,
            "incomplete_reasons": [], "properties": None}
        lifecycle.retained_failed_terminal = terminal
        args = systemctl_show_argv(lifecycle.unit)
        env = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C",
               "SYSTEMD_COLORS": "0", "SYSTEMD_PAGER": "cat", "PAGER": "cat"}
        try:
            process = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd="/",
                close_fds=True, start_new_session=True, shell=False, bufsize=0)
        except OSError as exc:
            reason = f"terminal_systemctl_spawn_failed:{type(exc).__name__}"
            terminal["incomplete_reasons"].append(reason)
            lifecycle.mark_manager_incomplete(reason)
            terminal["finished_monotonic_ns"] = time.monotonic_ns()
            return
        assert process.stdout is not None and process.stderr is not None
        os.set_blocking(process.stdout.fileno(), False)
        os.set_blocking(process.stderr.fileno(), False)
        self.current_query = {"lifecycle": lifecycle, "snapshot": terminal,
            "terminal": True, "process": process,
            "stdout": bytearray(), "stderr": bytearray(),
            "open": {process.stdout.fileno(): "stdout", process.stderr.fileno(): "stderr"},
            "deadline": min(time.monotonic() + SHOW_TIMEOUT_SECONDS,
                            phase_deadline, self.deadline),
            "capture_number": self.snapshot_number, "cancel_reason": None,
            "limit_reason": None}

    def _finish_terminal_query(self, query: dict[str, object]) -> None:
        lifecycle = query["lifecycle"]
        terminal = query["snapshot"]
        process = query["process"]
        assert isinstance(lifecycle, Lifecycle) and isinstance(terminal, dict)
        assert isinstance(process, subprocess.Popen)
        status = process.poll()
        limit_reason = query.get("limit_reason")
        timed_out = limit_reason in ("systemctl_timeout", "terminal_phase_deadline_exceeded",
                                     "observer_budget_expired")
        terminal["systemctl_exit"] = status
        terminal["systemctl_timed_out"] = timed_out
        terminal["systemctl_timeout_reason"] = limit_reason if timed_out else None
        terminal["systemctl_cancelled"] = query.get("cancel_reason")
        capture_number = int(query["capture_number"])
        reason: str | None = None
        try:
            terminal["raw_show"] = self.writer.capture(
                f"show-{capture_number:06d}.txt", bytes(query["stdout"]), MAX_SHOW_BYTES)
            terminal["raw_stderr"] = self.writer.capture(
                f"show-{capture_number:06d}.stderr", bytes(query["stderr"]), MAX_STDERR_BYTES)
            terminal["raw_show"].update({"exit": status, "timed_out": timed_out})
            terminal["raw_stderr"].update({"exit": status, "timed_out": timed_out})
        except (ObserverError, OSError) as exc:
            reason = f"terminal_raw_capture_failed:{type(exc).__name__}:{str(exc)[:300]}"
        if limit_reason:
            reason = reason or str(limit_reason)
        elif query.get("cancel_reason"):
            reason = reason or str(query["cancel_reason"])
        elif status != 0:
            reason = reason or f"terminal_systemctl_exit_{status}"
        elif query["stderr"]:
            reason = reason or "terminal_systemctl_stderr_not_empty"
        elif reason is None:
            try:
                properties = _parse_terminal_properties(bytes(query["stdout"]), lifecycle.unit)
                terminal["properties"] = properties
                full = [s for s in lifecycle.snapshots if bool(s.get("complete")) and
                        bool(s.get("manager_properties_complete")) and
                        bool(s.get("process_capture_complete")) and
                        Lifecycle.positive_pid_identity(s) is not None]
                identities = {Lifecycle.positive_pid_identity(s) for s in full}
                require(len(identities) == 1,
                        "retained terminal lacks unique earlier full process witness")
                witness = full[0]
                require(properties["LoadState"] == "loaded" and
                        properties["CollectMode"] == "inactive" and
                        properties["ActiveState"] == "failed" and
                        properties["SubState"] == "failed" and
                        properties["MainPID"] == "0" and
                        properties["Result"] == "exit-code" and
                        properties["ExecMainCode"] == "1" and
                        properties["ExecMainStatus"] == "1" and
                        properties["InvocationID"] == witness["invocation_id"] and
                        int(properties["ExecMainPID"]) == witness["main_pid"] and
                        int(properties["ExecMainStartTimestampMonotonic"]) ==
                            witness["exec_main_start_timestamp_monotonic"] and
                        int(properties["ExecMainExitTimestampMonotonic"]) >
                            int(properties["ExecMainStartTimestampMonotonic"]) and
                        lifecycle.invocation_id == witness["invocation_id"] and
                        lifecycle.main_pid == witness["main_pid"] and
                        terminal["event_seq"] == lifecycle.last_event_seq and
                        not lifecycle.removed,
                        "retained terminal failed state or process identity differs")
            except (ObserverError, UnicodeDecodeError, ValueError, KeyError) as exc:
                reason = f"terminal_properties_invalid:{type(exc).__name__}:{str(exc)[:300]}"
        terminal["finished_monotonic_ns"] = time.monotonic_ns()
        if reason:
            terminal["incomplete_reasons"].append(reason)
            lifecycle.mark_manager_incomplete(reason)
        else:
            terminal["manager_properties_complete"] = True
        self.current_query = None

    def _stop_query(self, query: dict[str, object], reason: str) -> None:
        process = query["process"]
        assert isinstance(process, subprocess.Popen)
        # poll() may reap an already exited child. Never address its old PID as
        # a process group after that point: the numeric PID may be reused.
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                try:
                    process.kill()
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=0.5)
                except subprocess.TimeoutExpired:
                    query["limit_reason"] = "systemctl_client_reap_timeout"
        # A child that has already exited can leave inherited pipes open in a
        # descendant. Bound the stopped query without signalling an unverified
        # process group or waiting for that descendant.
        for descriptor in list(query["open"]):
            query["open"].pop(descriptor, None)
            for stream in (process.stdout, process.stderr):
                if stream is not None and not stream.closed and stream.fileno() == descriptor:
                    stream.close()
                    break
        query["cancel_reason"] = reason

    def _read_query_pipes(self, query: dict[str, object], ready: list[int]) -> None:
        process = query["process"]
        assert isinstance(process, subprocess.Popen)
        streams = query["open"]
        output = query["stdout"]
        errors = query["stderr"]
        assert isinstance(streams, dict) and isinstance(output, bytearray) and isinstance(errors, bytearray)
        for descriptor in ready:
            kind = streams.get(descriptor)
            if kind is None:
                continue
            try:
                block = os.read(descriptor, 65536)
            except BlockingIOError:
                continue
            if not block:
                streams.pop(descriptor, None)
                for stream in (process.stdout, process.stderr):
                    if stream is not None and not stream.closed and stream.fileno() == descriptor:
                        stream.close()
                        break
                continue
            buffer = output if kind == "stdout" else errors
            limit = MAX_SHOW_BYTES if kind == "stdout" else MAX_STDERR_BYTES
            if len(buffer) + len(block) > limit:
                query["limit_reason"] = "systemctl_stdout_limit" if kind == "stdout" else "systemctl_stderr_limit"
                self._stop_query(query, str(query["limit_reason"]))
                remain = max(0, limit - len(buffer))
                buffer.extend(block[:remain])
            else:
                buffer.extend(block)

    @staticmethod
    def _stable_exe_identity(before: tuple[str, int, int], first_exe: str,
                             after: tuple[str, int, int], second_exe: str,
                             expected_ticks: int) -> bool:
        return (before[0] not in ("Z", "X", "x") and after[0] not in ("Z", "X", "x") and
                before[1:] == after[1:] and before[2] == expected_ticks and
                expected_ticks > 0 and first_exe == second_exe and
                0 < len(first_exe) <= 4096)

    def _read_stable_process_exe(self, pid: int, expected_ticks: int, label: str) -> str:
        import issue1162_live_probe as live_probe
        proc = Path(f"/proc/{pid}")
        before = live_probe._proc_stat_identity(
            live_probe._bounded_file(proc / "stat", MAX_PROC_STAT_BYTES, self.deadline, reserve=0.0),
            label)
        first_exe = os.readlink(proc / "exe")
        after = live_probe._proc_stat_identity(
            live_probe._bounded_file(proc / "stat", MAX_PROC_STAT_BYTES, self.deadline, reserve=0.0),
            label)
        second_exe = os.readlink(proc / "exe")
        if self._stable_exe_identity(before, first_exe, after, second_exe, expected_ticks):
            return first_exe
        # Type=exec can transition from systemd-executor to the pinned broker
        # between the two readlinks without changing PID/start ticks. Retry
        # exactly once and accept only a now-stable broker image.
        require(before[0] not in ("Z", "X", "x") and after[0] not in ("Z", "X", "x") and
                before[1:] == after[1:] and before[2] == expected_ticks and expected_ticks > 0 and
                first_exe in PREEXEC_EXECUTABLES and second_exe == BROKER_EXECUTABLE,
                "process executable or start ticks changed around read")
        retry_before = live_probe._proc_stat_identity(
            live_probe._bounded_file(proc / "stat", MAX_PROC_STAT_BYTES, self.deadline, reserve=0.0),
            label)
        retry_first_exe = os.readlink(proc / "exe")
        retry_after = live_probe._proc_stat_identity(
            live_probe._bounded_file(proc / "stat", MAX_PROC_STAT_BYTES, self.deadline, reserve=0.0),
            label)
        retry_second_exe = os.readlink(proc / "exe")
        require(retry_first_exe == BROKER_EXECUTABLE and
                self._stable_exe_identity(retry_before, retry_first_exe,
                                          retry_after, retry_second_exe, expected_ticks),
                "executor-to-broker executable transition did not stabilize")
        return BROKER_EXECUTABLE

    def _recheck_known_preexec(self, pid: int, snapshot: dict[str, object],
                               label: str) -> bool:
        try:
            current_exe = self._read_stable_process_exe(pid, int(snapshot["start_ticks"]), label)
        except (OSError, ObserverError, ValueError, RuntimeError):
            return False
        return current_exe == snapshot.get("initial_exe_observed") and \
               current_exe in PREEXEC_EXECUTABLES

    def _capture_failure_state(self, exc: Exception, snapshot: dict[str, object],
                               pid: int, capture_number: int, label: str | None = None,
                               signal_phase: bool = False) -> str:
        # The known systemd executor may occupy Type=exec's MainPID before it
        # enters the fixed broker image. It is preliminary only when the full
        # show still reports the start phase and the independently read ticks
        # later bind to a full broker witness. A different executable, cgroup,
        # hash, socket FD, or process identity is a hard contradiction.
        label = label or f"broker-{capture_number:06d}"
        expected_preexec = f"process executable path mismatch for {label}: "
        preexec_error = (str(exc) == expected_preexec + str(snapshot.get("initial_exe_observed")) or
                         str(exc) == f"process cgroup mismatch for {label}")
        if (type(exc).__name__ == "ProbeError" and
                (signal_phase or (snapshot.get("active_state") == "activating" and
                                  snapshot.get("substate") == "start")) and
                isinstance(snapshot.get("start_ticks"), int) and
                snapshot.get("initial_exe_observed") in PREEXEC_EXECUTABLES and
                preexec_error and self._recheck_known_preexec(pid, snapshot, label)):
            return "known_preexec_executable_transition"
        if type(exc).__name__ == "ProbeError" and str(exc) == \
                "submit-relative live-probe deadline exhausted":
            return "transient_capture_deadline"
        proc_prefix = f"/proc/{pid}/"
        if (isinstance(exc, OSError) and exc.errno in (errno.ENOENT, errno.ESRCH,
                errno.ENOTDIR, errno.EINTR) and isinstance(exc.filename, str) and
                exc.filename.startswith(proc_prefix)):
            if exc.errno == errno.EINTR:
                return "transient_proc_interrupted"
            if not Path(f"/proc/{pid}/stat").exists():
                return "transient_proc_disappeared"
        return "identity_contradiction_or_capture_error"

    def _finish_query(self, query: dict[str, object]) -> None:
        if query.get("terminal"):
            self._finish_terminal_query(query)
            return
        lifecycle = query["lifecycle"]
        snapshot = query["snapshot"]
        process = query["process"]
        assert isinstance(lifecycle, Lifecycle) and isinstance(snapshot, dict)
        assert isinstance(process, subprocess.Popen)
        stdout = bytes(query["stdout"])
        stderr = bytes(query["stderr"])
        capture_number = int(query["capture_number"])
        name = f"show-{capture_number:06d}.txt"
        stderr_name = f"show-{capture_number:06d}.stderr"
        status = process.poll()
        limit_reason = query.get("limit_reason")
        timed_out = limit_reason in ("systemctl_timeout", "observer_budget_expired")
        snapshot["systemctl_exit"] = status
        snapshot["systemctl_timed_out"] = bool(timed_out)
        snapshot["systemctl_timeout_reason"] = limit_reason if timed_out else None
        snapshot["systemctl_cancelled"] = query.get("cancel_reason")
        raw_capture_error: str | None = None
        try:
            raw_show = self.writer.capture(name, stdout, MAX_SHOW_BYTES)
            raw_stderr = self.writer.capture(stderr_name, stderr, MAX_STDERR_BYTES)
            snapshot["raw_show"] = {**raw_show, "exit": status, "timed_out": bool(timed_out)}
            snapshot["raw_stderr"] = {**raw_stderr, "exit": status, "timed_out": bool(timed_out)}
        except (ObserverError, OSError) as exc:
            raw_capture_error = f"systemctl_raw_capture_failed:{type(exc).__name__}:{str(exc)[:300]}"
        stdout_property: dict[str, str] | None = None
        manager_failure: str | None = None
        process_failure: str | None = None
        if limit_reason:
            manager_failure = str(limit_reason)
        elif query.get("cancel_reason"):
            manager_failure = str(query["cancel_reason"])
        elif status != 0:
            manager_failure = f"systemctl_exit_{status}"
        else:
            try:
                stdout_property = _parse_properties(stdout, lifecycle.unit)
            except ObserverError as exc:
                manager_failure = str(exc)
        if snapshot["raw_show"] is None or snapshot["raw_stderr"] is None:
            raw_capture_error = raw_capture_error or "systemctl_raw_capture_unavailable"
        if stdout_property is not None:
            invocation = stdout_property["InvocationID"]
            pid = int(stdout_property["MainPID"])
            exec_start = int(stdout_property["ExecMainStartTimestampMonotonic"])
            # The unit object may expose an old InvocationID or start timestamp
            # before this Accept=yes process obtains a nonzero MainPID.
            started = pid > 0
            invocation_ok = True
            if started:
                invocation_ok = lifecycle.accept_invocation(invocation)
                if not invocation_ok:
                    process_failure = "invocation_id_changed_or_invalid_within_manager_generation"
                if exec_start == 0:
                    process_failure = process_failure or "positive_mainpid_without_exec_start"
            if lifecycle.start_ticks is None:
                lifecycle.main_pid = pid
                lifecycle.cgroup = stdout_property["ControlGroup"]
            lifecycle.process_started = lifecycle.process_started or started
            lifecycle.result = stdout_property["Result"]
            snapshot["active_state"] = stdout_property["ActiveState"]
            lifecycle.substate = stdout_property["SubState"]
            lifecycle.exec_main_status = stdout_property["ExecMainStatus"]
            snapshot.update({"invocation_id_observed": invocation,
                "invocation_id": invocation if started and invocation_ok else None,
                "main_pid": pid, "process_started": started,
                "manager_properties_complete": True,
                "cgroup": stdout_property["ControlGroup"],
                "result": lifecycle.result, "substate": lifecycle.substate,
                "exec_main_status": lifecycle.exec_main_status,
                "exec_main_start_timestamp_monotonic": exec_start})
            if pid > 0:
                try:
                    provisional = next((row for row in lifecycle.provisional_captures
                        if row["main_pid"] == pid and row["capture_complete"] is True and
                        row["bound_snapshot_index"] is None), None)
                    if provisional is not None:
                        require(provisional["cgroup"] == snapshot["cgroup"] and
                                exec_start > 0 and exec_start * 1000 <=
                                    provisional["signal_monotonic_ns"] and
                                provisional["finished_monotonic_ns"] <= time.monotonic_ns() and
                                provisional["exe"] == BROKER_EXECUTABLE and
                                isinstance(provisional["exe_sha256"], str) and
                                re.fullmatch(r"[0-9a-f]{64}", provisional["exe_sha256"]) is not None and
                                isinstance(provisional["socket_fd0"], dict) and
                                provisional["socket_fd0"].get("stable") is True and
                                provisional["socket_fd0"].get("start_ticks_before") ==
                                    provisional["start_ticks"] ==
                                    provisional["socket_fd0"].get("start_ticks_after") and
                                provisional["socket_fd0"].get("target") ==
                                    f"socket:[{provisional['socket_fd0'].get('inode')}]" and
                                (lifecycle.start_ticks is None or
                                 lifecycle.start_ticks == provisional["start_ticks"]),
                                "provisional process differs from successful manager show")
                        for field in ("start_ticks", "initial_exe_observed", "proc_cgroup_raw",
                                      "exe", "exe_sha256", "proc_capture", "socket_fd0"):
                            snapshot[field] = provisional[field]
                        snapshot["provisional_event_seq"] = provisional["trigger_event_seq"]
                        provisional["bound_snapshot_index"] = next(index for index, row in
                            enumerate(lifecycle.snapshots) if row is snapshot)
                        if lifecycle.start_ticks is None:
                            lifecycle.start_ticks = int(snapshot["start_ticks"])
                            lifecycle.main_pid = pid
                            lifecycle.cgroup = str(snapshot["cgroup"])
                            lifecycle.exe = str(snapshot["exe"])
                            lifecycle.exe_sha256 = str(snapshot["exe_sha256"])
                    else:
                        self._capture_process(lifecycle, snapshot, pid, capture_number)
                    snapshot["process_capture_complete"] = bool(invocation_ok and exec_start > 0)
                except (OSError, ObserverError, ValueError, RuntimeError) as exc:
                    snapshot["process_capture_state"] = self._capture_failure_state(
                        exc, snapshot, pid, capture_number)
                    process_failure = process_failure or (
                        f"process_capture_failed:{type(exc).__name__}:{str(exc)[:300]}")
            elif lifecycle.process_started:
                if lifecycle.start_ticks is None:
                    process_failure = process_failure or "mainpid_event_not_live_at_property_snapshot"
                snapshot["process_capture_state"] = "process_started_but_mainpid_zero"
            else:
                if snapshot["process_start_observed_before_query"]:
                    if lifecycle.start_ticks is None:
                        process_failure = process_failure or "mainpid_event_not_live_at_property_snapshot"
                    snapshot["process_capture_state"] = "mainpid_event_not_live_at_property_snapshot"
                elif snapshot.get("trigger") == "unit-new" and not started:
                    snapshot["preliminary_property_miss"] = True
                    snapshot["preliminary_property_miss_reason"] = "unit_new_before_process_start"
                    snapshot["process_capture_state"] = "no_process_started_at_snapshot"
                    lifecycle.preliminary_property_misses.append({
                        "unit": lifecycle.unit, "object_path": lifecycle.object_path,
                        "generation": lifecycle.generation,
                        "trigger_event_seq": snapshot["trigger_event_seq"],
                        "reason": "unit_new_before_process_start",
                        "monotonic_ns": snapshot["started_monotonic_ns"]})
                else:
                    snapshot["process_capture_state"] = "no_process_started_at_snapshot"
        if raw_capture_error:
            manager_failure = manager_failure or raw_capture_error
            snapshot["manager_properties_complete"] = False
        if lifecycle.removed:
            manager_failure = "unit_removed_during_snapshot"
            snapshot["manager_properties_complete"] = False
        if manager_failure:
            snapshot["incomplete_reasons"].append(manager_failure)
            if raw_capture_error or lifecycle.removed:
                lifecycle.mark_manager_incomplete(manager_failure)
            elif snapshot["process_start_observed_before_query"]:
                lifecycle.mark_manager_incomplete(manager_failure)
                lifecycle.mark_process_incomplete(manager_failure)
            else:
                lifecycle.mark_manager_incomplete(manager_failure)
        if process_failure:
            snapshot["incomplete_reasons"].append(process_failure)
            # A raced positive-PID proc capture remains in evidence. It is
            # resolved only if as_json finds a complete witness with the exact
            # independently read start ticks and manager identity. Other
            # failures (invocation, start time, or unverified PID-zero) are hard.
            if not (snapshot["main_pid"] > 0 and
                    snapshot.get("process_capture_state") in SUPERSEDABLE_CAPTURE_STATES and
                    process_failure.startswith("process_capture_failed:")):
                lifecycle.mark_process_incomplete(process_failure)
        snapshot["complete"] = bool(snapshot["process_capture_complete"] and
                                      snapshot["manager_properties_complete"] and
                                      not process_failure and not manager_failure)
        snapshot["finished_monotonic_ns"] = time.monotonic_ns()
        lifecycle.last_snapshot_finished_monotonic_ns = int(snapshot["finished_monotonic_ns"])
        self.current_query = None

    def _capture_process(self, lifecycle: Lifecycle, snapshot: dict[str, object],
                         pid: int, capture_number: int, label: str | None = None,
                         bind_lifecycle: bool = True) -> None:
        try:
            import issue1162_live_probe as live_probe
        except ImportError as exc:
            raise ObserverError("required issue1162_live_probe sibling helper is unavailable") from exc
        require(pid > 0 and isinstance(snapshot.get("cgroup"), str) and
                snapshot["cgroup"].startswith("/"),
                "live MainPID has no valid systemd ControlGroup")
        expected_exe = BROKER_EXECUTABLE
        label = label or f"broker-{capture_number:06d}"
        references = dict(snapshot.get("proc_capture", {}))
        proc_path = Path(f"/proc/{pid}")
        initial_raw = live_probe._bounded_file(
            proc_path / "stat", MAX_PROC_STAT_BYTES, self.deadline, reserve=0.0)
        initial_stat = live_probe._proc_stat_identity(
            initial_raw, label)
        require(initial_stat[2] > 0, "positive MainPID has invalid proc start ticks")
        snapshot["start_ticks"] = initial_stat[2]
        first_ref = self.writer.capture(f"{label}.proc-stat-before", initial_raw,
                                        MAX_PROC_STAT_BYTES)
        references["proc-stat-before"] = {**first_ref, "exit": 0, "timed_out": False}
        snapshot["proc_capture"] = dict(references)
        snapshot["initial_exe_observed"] = self._read_stable_process_exe(
            pid, initial_stat[2], label)
        with tempfile.TemporaryDirectory(prefix="issue1162-broker-proc-") as temp_dir:
            old_umask = os.umask(0o077)
            try:
                proc = live_probe._capture_process(pid, label, Path(temp_dir),
                    self.deadline, str(snapshot["cgroup"]), expected_exe, reserve=0.0)
            finally:
                os.umask(old_umask)
            require(int(proc["starttime_ticks"]) == initial_stat[2],
                    "positive MainPID start ticks changed during capture")
            require(proc["cgroup"] == f"0::{snapshot['cgroup']}",
                    "proc cgroup raw identity differs from manager ControlGroup")
            snapshot["proc_cgroup_raw"] = str(proc["cgroup"])
            snapshot["exe"] = str(proc["exe"])
            snapshot["exe_sha256"] = str(proc["exe_sha256"])
            # The sibling helper writes three individually bounded files into
            # this private, short-lived directory. Copy only verified captures
            # into the aggregate-limited evidence directory.
            for suffix, maximum in (("proc-status", MAX_PROC_STATUS_BYTES),
                                     ("proc-mountinfo", MAX_PROC_MOUNTINFO_BYTES),
                                     ("proc-cgroup", MAX_PROC_CGROUP_BYTES)):
                filename = f"{label}.{suffix}"
                references[suffix] = self.writer.capture_path(
                    filename, Path(temp_dir) / filename, maximum)
                snapshot["proc_capture"] = dict(references)
        fd0 = self._capture_fd0(pid, int(proc["starttime_ticks"]), label, capture_number)
        snapshot["proc_capture"].update({"fd0_link": fd0["link_ref"],
                                         "fd0_info": fd0["fdinfo_ref"]})
        snapshot["socket_fd0"] = fd0["identity"]
        final_raw = live_probe._bounded_file(
            proc_path / "stat", MAX_PROC_STAT_BYTES, self.deadline, reserve=0.0)
        final_stat = live_probe._proc_stat_identity(final_raw, label)
        require(final_stat[0] not in ("Z", "X", "x") and
                final_stat[1:] == initial_stat[1:],
                "positive MainPID identity changed after FD-0 capture")
        final_ref = self.writer.capture(f"{label}.proc-stat-after", final_raw,
                                        MAX_PROC_STAT_BYTES)
        snapshot["proc_capture"]["proc-stat-after"] = {
            **final_ref, "exit": 0, "timed_out": False}
        snapshot["start_ticks"] = int(proc["starttime_ticks"])
        snapshot["main_pid"] = int(proc["pid"])
        if bind_lifecycle and lifecycle.start_ticks is None:
            lifecycle.start_ticks = int(proc["starttime_ticks"])
            lifecycle.main_pid = int(proc["pid"])
            lifecycle.cgroup = str(snapshot["cgroup"])
            lifecycle.exe = str(proc["exe"])
            lifecycle.exe_sha256 = str(proc["exe_sha256"])

    def _capture_fd0(self, pid: int, expected_ticks: int, label: str,
                     capture_number: int) -> dict[str, object]:
        try:
            import issue1162_live_probe as live_probe
        except ImportError as exc:
            raise ObserverError("required issue1162_live_probe sibling helper is unavailable") from exc
        proc = Path(f"/proc/{pid}")
        before = live_probe._proc_stat_identity(
            live_probe._bounded_file(proc / "stat", MAX_PROC_STAT_BYTES, self.deadline), label)
        require(before[0] not in ("Z", "X", "x") and before[2] == expected_ticks and expected_ticks > 0,
                "FD-0 readback process is not live or startticks changed before capture")
        link_before = os.readlink(proc / "fd/0")
        target_stat = os.stat(proc / "fd/0", follow_symlinks=True)
        fdinfo = live_probe._bounded_file(proc / "fdinfo/0", MAX_FDINFO_BYTES, self.deadline)
        links = FDINFO_INODE.findall(fdinfo)
        require(stat.S_ISSOCK(target_stat.st_mode) and
                re.fullmatch(r"socket:\[[0-9]+\]", link_before) is not None and
                len(links) == 1 and int(links[0]) == target_stat.st_ino,
                "accepted socket FD 0 readback is inconsistent")
        after = live_probe._proc_stat_identity(
            live_probe._bounded_file(proc / "stat", MAX_PROC_STAT_BYTES, self.deadline), label)
        link_after = os.readlink(proc / "fd/0")
        target_after = os.stat(proc / "fd/0", follow_symlinks=True)
        require(after[0] not in ("Z", "X", "x") and after[1:] == before[1:] and
                link_after == link_before and
                (target_stat.st_dev, target_stat.st_ino, target_stat.st_mode) ==
                (target_after.st_dev, target_after.st_ino, target_after.st_mode),
                "accepted socket FD 0 changed during capture")
        link_bytes = (link_before + "\n").encode("ascii", "strict")
        link_ref = self.writer.capture(f"{label}.proc-fd0-link",
                                       link_bytes, 4096)
        fdinfo_ref = self.writer.capture(f"{label}.proc-fd0-info",
                                         fdinfo, MAX_FDINFO_BYTES)
        link_ref.update({"exit": 0, "timed_out": False})
        fdinfo_ref.update({"exit": 0, "timed_out": False})
        return {"identity": {"fd": 0, "target": link_before, "device": target_stat.st_dev,
                "inode": target_stat.st_ino, "mode": target_stat.st_mode,
                "fdinfo_inode": int(links[0]), "start_ticks_before": before[2],
                "start_ticks_after": after[2], "stable": True},
                "link_ref": link_ref, "fdinfo_ref": fdinfo_ref}

    def _poll_once(self) -> None:
        query = self.current_query
        read_fds: list[int] = []
        write_fds: list[int] = []
        fd = int(self.bus.bus_fd(self.bus.bus))
        events = int(self.bus.bus_events(self.bus.bus))
        if events & select.POLLIN:
            read_fds.append(fd)
        if events & select.POLLOUT:
            write_fds.append(fd)
        if query is not None:
            read_fds.extend(int(value) for value in query["open"].keys())
        now = time.monotonic()
        remaining = max(0.0, self.deadline - now)
        timeout = min(SELECT_QUANTUM_SECONDS, remaining)
        bus_timeout = ctypes.c_uint64((1 << 64) - 1)
        result = int(self.bus.bus_timeout(self.bus.bus, ctypes.byref(bus_timeout)))
        require(result >= 0, "sd_bus_get_timeout failed")
        if bus_timeout.value != (1 << 64) - 1:
            timeout = min(timeout, max(0.0, bus_timeout.value / 1_000_000.0 - now))
        if query is not None:
            timeout = min(timeout, max(0.0, float(query["deadline"]) - now))
        ready_read, ready_write, _ = select.select(read_fds, write_fds, [], timeout)
        if fd in ready_read or fd in ready_write:
            self._current_process_events()
        query = self.current_query
        if query is not None:
            self._read_query_pipes(query, [value for value in ready_read if value != fd])
            now = time.monotonic()
            if query.get("cancel_reason") and (query["process"].poll() is None or query["open"]):
                self._stop_query(query, str(query["cancel_reason"]))
            elif now >= float(query["deadline"]) and (query["process"].poll() is None or query["open"]):
                query["limit_reason"] = "systemctl_timeout"
                self._stop_query(query, "systemctl_timeout")
            process = query["process"]
            if process.poll() is not None and not query["open"]:
                self._finish_query(query)

    def _check_stop(self) -> bool:
        self.stop_seen = _safe_stop_file(self.writer)
        if self.stop_seen and self.stop_monotonic_ns is None:
            self.stop_monotonic_ns = time.monotonic_ns()
        return self.stop_seen

    def _settle_unstarted_queue(self) -> None:
        while self.queue:
            lifecycle, trigger, seq = self.queue.popleft()
            reason = ("unit_removed_before_snapshot" if lifecycle.removed else
                      "observer_stopped_before_snapshot")
            snapshot: dict[str, object] = {
                "boot_id": self.boot_id, "boot_id_raw": self.boot_id_raw,
                "unit": lifecycle.unit, "object_path": lifecycle.object_path,
                "generation": lifecycle.generation, "trigger": trigger,
                "trigger_event_seq": seq, "manager_object_reload_possible": True,
                "started_monotonic_ns": time.monotonic_ns(),
                "complete": False, "incomplete_reasons": [],
                "manager_properties_complete": False,
                "process_capture_complete": False,
                "invocation_id": None, "main_pid": 0,
                "process_started": False, "start_ticks": None,
                "cgroup": None, "exe": None, "exe_sha256": None,
                "result": None, "active_state": None, "substate": None,
                "exec_main_status": None,
                "raw_show": None, "raw_stderr": None, "proc_capture": {},
                "socket_fd0": None, "systemctl_exit": None,
                "systemctl_timed_out": False, "systemctl_timeout_reason": None,
                "systemctl_cancelled": None}
            lifecycle.snapshots.append(snapshot)
            self._record_unstarted_query(lifecycle, snapshot, reason)

    def _run_terminal_phase(self) -> None:
        require(self.stop_seen and self.stop_monotonic_ns is not None and
                self.current_query is None and not self.queue,
                "terminal phase requires a settled normal stop")
        self.terminal_phase_started_monotonic_ns = time.monotonic_ns()
        phase_deadline = min(self.deadline,
                             self.stop_monotonic_ns / 1_000_000_000 + TERMINAL_PHASE_SECONDS)
        # Leave one second for bounded cancellation/reap within the 30-second
        # stop-to-finish allowance. No manager or unit state is mutated.
        work_deadline = phase_deadline - 1.0
        for lifecycle in self.units:
            if lifecycle.removed:
                continue
            if time.monotonic() >= work_deadline:
                self.mark_global("terminal_phase_deadline_exceeded", event_loss=True)
                break
            if len(lifecycle.snapshots) >= MAX_SNAPSHOTS_PER_GENERATION:
                lifecycle.mark_manager_incomplete("terminal_query_slot_unavailable")
                continue
            self._current_process_events()
            if self.bus_budget_exhausted or self.global_reasons or self.event_loss_detected:
                break
            if lifecycle.removed:
                continue
            self._start_terminal_query(lifecycle, work_deadline)
            while self.current_query is not None:
                self._current_process_events()
                query = self.current_query
                if query is None:
                    break
                if (self.bus_budget_exhausted or self.global_reasons or
                        self.event_loss_detected or time.monotonic() >= work_deadline):
                    reason = ("manager_stream_incomplete_during_terminal" if
                              self.global_reasons or self.event_loss_detected else
                              "manager_bus_dispatch_budget_exceeded" if
                              self.bus_budget_exhausted else
                              "terminal_phase_deadline_exceeded")
                    query["limit_reason"] = reason
                    self._stop_query(query, reason)
                    self._finish_query(query)
                    if reason == "terminal_phase_deadline_exceeded":
                        self.mark_global("terminal_phase_deadline_exceeded", event_loss=True)
                    break
                self._poll_once()
            if self.bus_budget_exhausted or self.global_reasons or self.event_loss_detected:
                break
        self.terminal_phase_finished_monotonic_ns = time.monotonic_ns()
        if self.terminal_phase_finished_monotonic_ns - self.stop_monotonic_ns > \
                int(TERMINAL_PHASE_SECONDS * 1_000_000_000):
            self.mark_global("terminal_phase_deadline_exceeded", event_loss=True)

    def _drain_final_events(self) -> None:
        # A stop marker bounds new work, but there may still be queued manager
        # messages. Claim a complete stream only after observing an empty bus.
        final_deadline = min(self.deadline, time.monotonic() + 0.25)
        for _ in range(MAX_DISPATCHES_PER_DRAIN):
            if time.monotonic() >= final_deadline:
                self.mark_global("manager_final_drain_deadline_exceeded", event_loss=True)
                break
            result, message = self.bus.process_one()
            if result == 0:
                break
            if not self._record_dispatch(message):
                break
            if message is not None:
                self.handle_message(message)
        else:
            self.mark_global("manager_final_drain_slice_exceeded", event_loss=True)

    def _check_template_metadata_completion(self) -> None:
        if len(self.template_metadata_events) == 1:
            self.mark_global("template_metadata_unpaired", event_loss=True)

    def run(self) -> dict[str, object]:
        require(bool(getattr(self.bus, "armed", False)),
                "observer readiness requires installed matches and Manager.Subscribe ack")
        require(UNIQUE_BUS_NAME.fullmatch(str(getattr(self.bus, "manager_sender", ""))) is not None,
                "observer readiness requires a frozen unique manager sender")
        require(UNIQUE_BUS_NAME.fullmatch(str(getattr(self.bus, "connection_unique_name", ""))) is not None,
                "observer readiness requires its own frozen unique bus name")
        self.ready_ns = time.monotonic_ns()
        self.observer_pid = os.getpid()
        self.observer_start_ticks = _self_start_ticks()
        ready = {"schema": SCHEMA, "kind": "READY", "boot_id": self.boot_id,
                 "boot_id_raw": self.boot_id_raw, "run_id": self.run_id,
                 "run_attempt": self.run_attempt, "observer_pid": self.observer_pid,
                 "observer_start_ticks": self.observer_start_ticks, "match_ack": True,
                 "manager_sender": self.bus.manager_sender,
                 "subscribe_ack": True, "ready_monotonic_ns": self.ready_ns,
                 "population_complete": False,
                 "population_scope": "requires independent trusted PID1 journal reconciliation",
                 "manager_object_reload_possible": True}
        self.writer.publish_ready(ready)
        while True:
            if self._check_stop():
                break
            now = time.monotonic()
            if now >= self.deadline:
                self.mark_global("observer_budget_expired", event_loss=True)
                if self.current_query is not None:
                    self.current_query["limit_reason"] = "observer_budget_expired"
                    self._stop_query(self.current_query, "observer_budget_expired")
                break
            self._current_process_events()
            if self.bus_budget_exhausted:
                break
            self._start_next_query()
            self._poll_once()
        if self.current_query is not None:
            query = self.current_query
            self._stop_query(query, "observer_stopped_during_snapshot")
            self._read_query_pipes(query, list(query["open"].keys()))
            self._finish_query(query)
        self._settle_unstarted_queue()
        if (self.stop_seen and not self.global_reasons and not self.event_loss_detected and
                time.monotonic() < self.deadline):
            self._run_terminal_phase()
        # The bus stays in the select loop through terminal capture, then its
        # final bounded empty drain is the horizon for every terminal claim.
        self._drain_final_events()
        self._settle_unstarted_queue()
        self._check_template_metadata_completion()
        units = [row.as_json(self.boot_id, self.boot_id_raw, self.event_loss_detected)
                 for row in self.units]
        event_stream_complete = not self.event_loss_detected
        manager_observation_complete = (bool(units) and self.stop_seen and
            not self.global_reasons and all(bool(row["manager_lifecycle_complete"]) for row in units))
        process_complete = (bool(units) and self.stop_seen and not self.global_reasons and
                            all(bool(row["process_complete"]) for row in units))
        require(len(json.dumps(self.dispatch_diagnostics, ensure_ascii=True,
                               separators=(",", ":")).encode()) <=
                MAX_DISPATCH_DIAGNOSTIC_BYTES,
                "dispatch diagnostic summary exceeds bound")
        return {"schema": SCHEMA, "kind": "SUMMARY", "boot_id": self.boot_id,
                "boot_id_raw": self.boot_id_raw, "run_id": self.run_id,
                "run_attempt": self.run_attempt, "observer_pid": self.observer_pid,
                "observer_start_ticks": self.observer_start_ticks,
                "manager_sender": self.bus.manager_sender,
                "ready_monotonic_ns": self.ready_ns,
                "ended_monotonic_ns": time.monotonic_ns(), "stop_seen": self.stop_seen,
                "stop_monotonic_ns": self.stop_monotonic_ns,
                "terminal_phase_started_monotonic_ns": self.terminal_phase_started_monotonic_ns,
                "terminal_phase_finished_monotonic_ns": self.terminal_phase_finished_monotonic_ns,
                "event_count": self.writer.event_count, "event_bytes": self.writer.event_bytes,
                "bus_dispatch_count": self.bus_dispatch_count,
                "dispatch_buckets": self.dispatch_buckets,
                "dispatch_diagnostics": self.dispatch_diagnostics,
                "template_metadata_events": self.template_metadata_events,
                "capture_bytes": self.writer.capture_bytes,
                "event_loss_detected": self.event_loss_detected,
                "event_stream_complete": event_stream_complete,
                "population_complete": False,
                "population_scope": "not established; reconcile all expected starts from trusted PID1 journal",
                "manager_object_reload_possible": True,
                "manager_observation_complete": manager_observation_complete,
                "process_complete": process_complete,
                "observer_complete": process_complete,
                "global_incomplete_reasons": list(self.global_reasons),
                "limits": {"max_events": MAX_EVENTS, "max_events_per_unit": MAX_EVENTS_PER_UNIT,
                           "max_bus_dispatches": MAX_BUS_DISPATCHES,
                           "max_dispatches_per_second": MAX_DISPATCHES_PER_SECOND,
                           "dispatch_window_seconds": DISPATCH_WINDOW_SECONDS,
                           "max_dispatches_per_window": MAX_DISPATCHES_PER_WINDOW,
                           "max_dispatch_buckets": MAX_DISPATCH_BUCKETS,
                           "max_dispatches_per_drain": MAX_DISPATCHES_PER_DRAIN,
                           "max_generations": MAX_GENERATIONS,
                           "max_concurrent_queries": MAX_CONCURRENT_QUERIES,
                           "max_snapshots_per_generation": MAX_SNAPSHOTS_PER_GENERATION,
                           "max_prestop_snapshots_per_generation": MAX_PRESTOP_SNAPSHOTS_PER_GENERATION,
                           "max_provisional_captures_per_generation":
                               MAX_PROVISIONAL_CAPTURES_PER_GENERATION,
                           "max_property_snapshots": MAX_PROPERTY_SNAPSHOTS,
                           "max_show_bytes": MAX_SHOW_BYTES, "max_stderr_bytes": MAX_STDERR_BYTES,
                           "max_evidence_bytes": MAX_EVIDENCE_BYTES,
                           "max_ready_bytes": MAX_READY_BYTES,
                           "max_summary_bytes": MAX_SUMMARY_BYTES,
                           "summary_reserve_bytes": SUMMARY_RESERVE_BYTES,
                           "show_timeout_seconds": SHOW_TIMEOUT_SECONDS,
                           "terminal_phase_seconds": TERMINAL_PHASE_SECONDS,
                           "max_run_seconds": MAX_RUN_SECONDS},
                "units": units}


def _prepare_observer(args: argparse.Namespace) -> tuple[EvidenceWriter, SdBus, str, str, float]:
    require(os.geteuid() == 0, "observer must run as root inside its disposable guest")
    require(args.budget_seconds > 0 and args.budget_seconds <= MAX_RUN_SECONDS,
            "--budget-seconds is outside the fixed positive bound")
    require(args.run_id is not None and args.run_attempt is not None,
            "--run-id and --run-attempt are required to freeze observer identity")
    require(re.fullmatch(r"[1-9][0-9]{0,18}", args.run_id) is not None and
            int(args.run_id) <= (1 << 63) - 1,
            "--run-id must be a canonical positive decimal string")
    require(re.fullmatch(r"[1-9][0-9]{0,4}", args.run_attempt) is not None and
            int(args.run_attempt) <= 10_000,
            "--run-attempt must be a canonical positive bounded decimal string")
    output = EvidenceWriter(args.output)
    try:
        boot_id, boot_id_raw = _boot_id()
        bus = SdBus()
        deadline = time.monotonic() + args.budget_seconds
        bus.arm()  # match installation acks precede the Subscribe ack
        return output, bus, boot_id, boot_id_raw, deadline
    except BaseException:
        output.close()
        raise


def _self_test() -> None:
    checks = 0

    assert parse_broker_unit("buster-bench-systemd-broker@0-622-65000.service") == (0, 622, 65000)
    assert parse_broker_unit("buster-bench-systemd-broker@3-700-65000.service") == (3, 700, 65000)
    assert parse_broker_unit("buster-bench-systemd-broker@5-727-0.service") == (5, 727, 0)
    checks += 3
    for invalid in ("buster-bench-systemd-broker@00-622-65000.service",
                    "buster-bench-systemd-broker@0-0622-65000.service",
                    "buster-bench-systemd-broker@0-622-065000.service",
                    "buster-bench-systemd-broker@0-622-65000.service;id",
                    "buster-bench-systemd-broker@0-622-65000.socket",
                    "other@0-622-65000.service",
                    "buster-bench-systemd-broker@0-unknown.service"):
        try:
            parse_broker_unit(invalid)
        except ObserverError:
            checks += 1
        else:
            raise AssertionError("unsafe broker unit was accepted")

    boot, raw = normalized_boot_id("00112233-4455-6677-8899-aabbccddeeff")
    assert boot == "00112233445566778899aabbccddeeff" and raw == "00112233-4455-6677-8899-aabbccddeeff"
    checks += 1
    for invalid in ("00112233445566778899aabbccddeeff", "00112233-4455-6677-8899-AABBCCDDEEFF",
                    "00112233-4455-6677-8899-aabbccddeefg"):
        try:
            normalized_boot_id(invalid)
        except ObserverError:
            checks += 1
        else:
            raise AssertionError("noncanonical boot ID was accepted")

    # Match acknowledgements must precede Subscribe and readiness must not be
    # published if either synchronous operation fails.
    class FixtureArm:
        def __init__(self, fail_at: str | None = None):
            self.calls: list[str] = []
            self.fail_at = fail_at

        def arm(self) -> None:
            self.calls.append("connect")
            for index, _rule in enumerate(MATCH_RULES):
                self.calls.append(f"match-{index}")
                if self.fail_at == f"match-{index}":
                    raise ObserverError("fixture match failure")
            self.calls.append("subscribe")
            if self.fail_at == "subscribe":
                raise ObserverError("fixture subscribe failure")

    good = FixtureArm()
    good.arm()
    assert good.calls == ["connect", "match-0", "match-1", "match-2", "match-3",
                          "subscribe"]
    checks += 1
    for fail_at in ("match-0", "match-1", "match-2", "match-3", "subscribe"):
        failed = FixtureArm(fail_at)
        try:
            failed.arm()
        except ObserverError:
            assert "subscribe" not in failed.calls if fail_at.startswith("match-") else True
            checks += 1
        else:
            raise AssertionError("missing observer readiness acknowledgement was accepted")

    # The connection name is read once from the completed handshake before
    # match installation or Subscribe; malformed names cannot arm observation.
    def arm_fixture(unique_name: bytes) -> tuple[SdBus, list[str]]:
        fixture_bus = object.__new__(SdBus)
        fixture_bus.bus = ctypes.c_void_p()
        fixture_bus.slots = []
        fixture_bus.armed = False
        fixture_bus.manager_sender = None
        fixture_bus.connection_unique_name = None
        calls: list[str] = []
        def open_bus(pointer: object) -> int:
            calls.append("connect")
            pointer._obj.value = 1
            return 0

        def get_unique_name(_bus: object, pointer: object) -> int:
            calls.append("unique")
            pointer._obj.value = unique_name
            return 0

        def add_match(_bus: object, pointer: object, _rule: bytes,
                      _callback: object, _userdata: object) -> int:
            calls.append("match")
            pointer._obj.value = 2
            return 0

        def new_method(_bus: object, pointer: object, *_args: object) -> int:
            calls.append("subscribe-method")
            pointer._obj.value = 3
            return 0

        def call_method(_bus: object, _method: object, _timeout: int,
                        _error: object, pointer: object) -> int:
            calls.append("subscribe-call")
            pointer._obj.value = 4
            return 0

        fixture_bus.open_system = open_bus
        fixture_bus.get_unique_name = get_unique_name
        fixture_bus.add_match = add_match
        fixture_bus.new_method_call = new_method
        fixture_bus.call = call_method
        fixture_bus.get_sender = lambda _reply: b":1.0"
        fixture_bus.message_unref = lambda _message: calls.append("unref")
        return fixture_bus, calls

    armed_bus, calls = arm_fixture(b":1.7")
    armed_bus.arm()
    assert armed_bus.armed and armed_bus.connection_unique_name == ":1.7"
    assert armed_bus.manager_sender == ":1.0"
    assert calls == ["connect", "unique", "match", "match", "match", "match",
                     "subscribe-method", "subscribe-call", "unref", "unref"]
    checks += 1
    malformed_bus, calls = arm_fixture(b"org.freedesktop.DBus")
    try:
        malformed_bus.arm()
    except ObserverError:
        assert not malformed_bus.armed and calls == ["connect", "unique"]
        checks += 1
    else:
        raise AssertionError("non-unique observer connection name was accepted")

    # Exercise the actual pull-mode dispatch seam. A direct bus-daemon signal
    # may bypass the manager match rules, while manager signals remain pinned
    # to the unique Subscribe reply sender. No live bus is opened here.
    def dispatch_fixture(member: str, interface: str, path: str, sender: str,
                         signature: str, body: str = ":1.7") -> tuple[SdBus, list[bool], list[bytes]]:
        fixture_bus = object.__new__(SdBus)
        fixture_bus.bus = ctypes.c_void_p(1)
        fixture_bus.manager_sender = ":1.0"
        fixture_bus.connection_unique_name = ":1.7"
        released: list[bool] = []
        reads: list[bytes] = []
        def read_name(_message: object, read_signature: bytes, pointer: object) -> int:
            reads.append(read_signature)
            pointer._obj.value = body.encode("ascii")
            return 1

        fixture_bus.process = lambda _bus, pointer: (setattr(pointer._obj, "value", 3) or 1)
        fixture_bus.get_type = lambda _message, pointer: (setattr(pointer._obj, "value", 4) or 0)
        fixture_bus.get_member = lambda _message: member.encode("ascii")
        fixture_bus.get_interface = lambda _message: interface.encode("ascii")
        fixture_bus.get_path = lambda _message: path.encode("ascii")
        fixture_bus.get_sender = lambda _message: sender.encode("ascii")
        fixture_bus.get_signature = lambda _message, _flag: signature.encode("ascii")
        fixture_bus.message_read = read_name
        fixture_bus._read_unit_path = lambda _message: (
            "buster-bench-systemd-broker@0-741-65000.service",
            "/org/freedesktop/systemd1/unit/fixture")
        fixture_bus.message_unref = lambda _message: released.append(True)
        return fixture_bus, released, reads

    bus_header = ("NameAcquired", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                  "org.freedesktop.DBus", "s")
    bus_fixture, released, reads = dispatch_fixture(*bus_header)
    assert bus_fixture.process_one() == (1, None)
    assert released == [True] and reads == [b"s"]
    checks += 1
    manager_header = ("UnitNew", "org.freedesktop.systemd1.Manager",
                      "/org/freedesktop/systemd1", ":1.0", "so")
    manager_fixture, released, reads = dispatch_fixture(*manager_header)
    result, message = manager_fixture.process_one()
    assert result == 1 and message is not None
    assert message["sender"] == ":1.0" and message["member"] == "UnitNew"
    assert released == [True] and reads == []
    checks += 1
    for header, body, was_read in (
            (("NameAcquired", "org.freedesktop.DBus", "/org/freedesktop/DBus",
              ":1.9", "s"), ":1.7", False),
            (("NameAcquired", "org.freedesktop.systemd1.Manager", "/org/freedesktop/DBus",
              "org.freedesktop.DBus", "s"), ":1.7", False),
            (("NameAcquired", "org.freedesktop.DBus", "/org/freedesktop/Other",
              "org.freedesktop.DBus", "s"), ":1.7", False),
            (("NameAcquired", "org.freedesktop.DBus", "/org/freedesktop/DBus",
              "org.freedesktop.DBus", "as"), ":1.7", False),
            (bus_header, "secret-unexpected-name", True),
            (("NameLost", "org.freedesktop.DBus", "/org/freedesktop/DBus",
              "org.freedesktop.DBus", "s"), ":1.7", False),
            (("UnitNew", "org.freedesktop.systemd1.Manager",
              "/org/freedesktop/systemd1", ":1.9", "so"), ":1.7", False),
            (("UnitNew", "org.freedesktop.systemd1.Manager",
              "/org/freedesktop/systemd1", ":1.0", "s"), ":1.7", False),
            (("PropertiesChanged", "org.freedesktop.DBus.Properties",
              BASE_UNIT_PATH + "/fixture", ":1.9", "sa{sv}as"), ":1.7", False)):
        rejected, released, reads = dispatch_fixture(*header, body=body)
        try:
            rejected.process_one()
        except ObserverError as exc:
            reason = str(exc)
            assert all(field + "=" in reason for field in
                       ("sender", "interface", "member", "path", "signature"))
            assert body not in reason and len(reason.encode("utf-8")) < 4096
            assert released == [True] and reads == ([b"s"] if was_read else [])
            checks += 1
        else:
            raise AssertionError("untrusted bus or manager signal was accepted")
    # The unit namespace match also receives legal Socket signals from other
    # units. Keep their bodies out of the broker-only parser after validating
    # the frozen manager sender and the complete PropertiesChanged header.
    property_header = ("PropertiesChanged", "org.freedesktop.DBus.Properties",
                       BASE_UNIT_PATH + "/avahi_2ddaemon_2esocket", ":1.0", "sa{sv}as")
    unrelated, released, reads = dispatch_fixture(*property_header,
                                                    body="org.freedesktop.systemd1.Socket")
    assert unrelated.process_one() == (1, None)
    assert released == [True] and reads == []
    checks += 1
    brokerish_header = ("PropertiesChanged", "org.freedesktop.DBus.Properties",
                        BASE_UNIT_PATH_ENCODED_BROKER_PREFIX + "noncanonical",
                        ":1.0", "sa{sv}as")
    brokerish, released, reads = dispatch_fixture(*brokerish_header,
                                                  body="org.freedesktop.systemd1.Socket")
    try:
        brokerish.process_one()
    except ObserverError as exc:
        reason = str(exc)
        assert "unit_interface=\"org.freedesktop.systemd1.Socket\"" in reason
        assert "path=\"" + brokerish_header[2] + "\"" in reason
        assert len(reason.encode("utf-8")) < 4096
        assert released == [True] and reads == [b"s"]
        checks += 1
    else:
        raise AssertionError("noncanonical broker path escaped body validation")
    long_metadata = SdBus._signal_metadata("\U0001f62e" * 1000, "org.freedesktop.DBus",
                                           "NameAcquired", "/x" * 1000, "s" * 1000)
    assert len(long_metadata.encode("utf-8")) < 1500
    assert all(field + "=" in long_metadata for field in
               ("sender", "interface", "member", "path", "signature"))
    checks += 1

    # systemctl subprocess controls use an actual fake executable but never
    # open a manager bus or address the host systemd instance.
    # The disposable systemd guest mounts /tmp noexec; executable fixtures
    # therefore live in /var/tmp without relaxing a guest mount policy.
    with tempfile.TemporaryDirectory(prefix="issue1162-broker-observer-selftest-",
                                     dir="/var/tmp") as temp:
        root = Path(temp)
        fake = root / "systemctl-fixture.py"
        fake.write_text("#!/usr/bin/env python3\n"
            "import os,sys,time\n"
            "mode=os.environ.get('FIXTURE_MODE','ok')\n"
            "if mode=='timeout': time.sleep(2)\n"
            "elif mode=='size': sys.stdout.write('X' * 2048)\n"
            "elif mode=='error': sys.stderr.write('fixture-error\\n'); sys.exit(7)\n"
            "else: sys.stdout.write('Id='+sys.argv[-1]+'\\nLoadState=loaded\\nActiveState=active\\n'"
            "+'SubState=running\\nMainPID=0\\nInvocationID='+'a'*32+'\\nControlGroup=/system.slice/x\\n'"
            "+'ExecMainStartTimestampMonotonic=0\\nExecMainStatus=0\\nResult=success\\n')\n",
            encoding="ascii")
        fake.chmod(0o700)
        unit = "buster-bench-systemd-broker@0-622-65000.service"
        fake.chmod(0o600)
        try:
            _run_fake_command([str(fake), "show", "--", unit],
                              {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}, 1.0, 4096)
        except PermissionError as exc:
            assert exc.errno == errno.EACCES
            checks += 1
        else:
            raise AssertionError("non-executable/noexec-style fixture unexpectedly ran")
        finally:
            fake.chmod(0o700)
        for mode, timeout, output_bound, expected, marker in (
                ("ok", 1.0, 4096, 0, "Id="),
                ("timeout", 0.05, 4096, None, "timeout"),
                ("size", 1.0, 128, None, "limit"),
                ("error", 1.0, 4096, 7, "fixture-error")):
            env = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "FIXTURE_MODE": mode}
            result = _run_fake_command([str(fake), "show", "--all", "--no-pager", "--", unit],
                                       env, timeout, output_bound)
            if mode == "ok":
                assert result["exit"] == expected and marker in result["stdout"].decode()
            elif mode == "timeout":
                assert result["timed_out"] and marker in str(result["reason"])
            elif mode == "size":
                assert result["limit_hit"] and marker in str(result["reason"])
            else:
                assert result["exit"] == expected and marker in result["stderr"].decode()
            checks += 1
        injection = "buster-bench-systemd-broker@0-622-65000.service;id"
        try:
            parse_broker_unit(injection)
        except ObserverError:
            checks += 1
        else:
            raise AssertionError("command argument injection unit was accepted")

    # Deterministic lifecycle fixtures cover reorder, dedupe, removal during
    # capture and invocation mismatch without constructing a live sd-bus.
    lifecycle = Lifecycle("buster-bench-systemd-broker@0-622-65000.service",
                          "/org/freedesktop/systemd1/unit/buster_2dbench_2dsystemd_2dbroker_400_2d622_2d65000_2eservice",
                          1, 1, 10)
    observer = object.__new__(BrokerObserver)
    observer.queue = deque()
    observer.snapshot_number = 0
    lifecycle.triggers = set()
    BrokerObserver._schedule(observer, lifecycle, "unit-new", 1)
    BrokerObserver._schedule(observer, lifecycle, "unit-new", 2)
    assert len(observer.queue) == 1 and lifecycle.triggers == {"unit-new"}
    checks += 1
    lifecycle.removed = True
    before = len(observer.queue)
    BrokerObserver._schedule(observer, lifecycle, "properties-first", 3)
    assert len(observer.queue) == before and "snapshot_trigger_after_unit_removed" in lifecycle.incomplete_reasons
    checks += 1
    lifecycle2 = Lifecycle(lifecycle.unit, lifecycle.object_path, 2, 4, 20)
    lifecycle2.invocation_id = "a" * 32
    candidate_id = "b" * 32
    assert candidate_id != lifecycle2.invocation_id
    lifecycle2.mark_incomplete("invocation_id_changed_within_manager_generation")
    assert not lifecycle2.as_json("0" * 32, "00000000-0000-0000-0000-000000000000")["complete"]
    checks += 1
    path_mismatch = "/org/freedesktop/systemd1/unit/not-a-broker"
    assert path_mismatch != lifecycle.object_path
    checks += 1

    # A full witness may account for an earlier positive-PID proc race only
    # when that partial retained the same independent start ticks and every
    # manager identity component. A later PID-zero property read is harmless.
    witness = {"boot_id": boot, "unit": lifecycle.unit,
        "object_path": lifecycle.object_path, "generation": 3,
        "invocation_id": "b" * 32, "main_pid": 741, "process_started": True,
        "exec_main_start_timestamp_monotonic": 912345,
        "cgroup": "/system.slice/broker.instance", "start_ticks": 5812,
        "manager_properties_complete": True, "process_capture_complete": True,
        "complete": True}
    partial = {**witness, "process_capture_complete": False, "complete": False,
               "process_capture_state": "known_preexec_executable_transition",
               "active_state": "activating", "substate": "start",
               "incomplete_reasons": ["process_capture_failed:ProbeError:known preexec image"]}
    ended = {"boot_id": boot, "unit": lifecycle.unit,
             "object_path": lifecycle.object_path, "generation": 3,
             "main_pid": 0, "process_started": False,
             "manager_properties_complete": True, "complete": False}
    completed = Lifecycle(lifecycle.unit, lifecycle.object_path, 3, 5, 30)
    completed.removed = True
    completed.removed_event_seq = 10
    completed.removed_monotonic_ns = 100
    completed.process_started = True
    completed.snapshots = [partial, witness, ended]
    row = completed.as_json(boot, raw)
    assert row["process_complete"] and row["snapshots"][0] is partial
    assert not partial["complete"] and not ended["complete"]
    checks += 1
    completed.main_pid_values = {741, 742}
    conflicting_signal = completed.as_json(boot, raw)
    assert not conflicting_signal["process_complete"]
    assert "mainpid_signal_conflicts_with_process_witness" in conflicting_signal["process_incomplete_reasons"]
    checks += 1
    completed.main_pid_values = {0, 741}
    assert completed.as_json(boot, raw)["process_complete"]
    checks += 1
    for contradiction in ("process cgroup mismatch", "process executable path mismatch",
                          "running and installed executable hashes differ",
                          "accepted socket FD 0 readback is inconsistent",
                          "positive MainPID start ticks changed during capture"):
        bad_partial = {**partial, "process_capture_state": "identity_contradiction_or_capture_error",
                       "incomplete_reasons": [f"process_capture_failed:ProbeError:{contradiction}"]}
        completed.snapshots = [bad_partial, witness, ended]
        assert not completed.as_json(boot, raw)["process_complete"], contradiction
        checks += 1
    completed.snapshots = [partial, witness, ended]
    for key, changed in (("boot_id", "f" * 32), ("unit", "other.service"),
                         ("object_path", path_mismatch), ("generation", 4),
                         ("invocation_id", "c" * 32), ("main_pid", 742),
                         ("exec_main_start_timestamp_monotonic", 912346),
                         ("cgroup", "/system.slice/other"),
                         ("start_ticks", 5813), ("start_ticks", None),
                         ("exec_main_start_timestamp_monotonic", 0)):
        completed.snapshots = [{**partial, key: changed}, witness, ended]
        assert not completed.as_json(boot, raw)["process_complete"], key
        checks += 1
    completed.snapshots = [partial, witness, ended]
    completed.mark_process_incomplete("invocation_id_changed_or_invalid_within_manager_generation")
    assert not completed.as_json(boot, raw)["process_complete"]
    checks += 1

    # A stale inactive object may advertise an old invocation and execution
    # timestamp. It cannot bind the generation until a positive MainPID show.
    class FixtureWriter:
        def capture(self, name: str, content: bytes, maximum: int) -> dict[str, object]:
            assert len(content) <= maximum
            return {"path": f"captures/{name}", "bytes": len(content),
                    "sha256": hashlib.sha256(content).hexdigest()}

    # Exercise the production poll/drain/finish path with real local pipes.
    # An EOF on one stream closes it; a later EOF on the other must never call
    # fileno() on the first closed Python file object. The stdin barrier makes
    # the two ordered EOF cases span separate poll cycles deterministically.
    class PipeWriter(FixtureWriter):
        def __init__(self):
            self.contents: dict[str, bytes] = {}

        def capture(self, name: str, content: bytes, maximum: int) -> dict[str, object]:
            self.contents[name] = content
            return super().capture(name, content, maximum)

    pipe_show = (f"Id={lifecycle.unit}\nLoadState=loaded\nActiveState=activating\n"
                 "SubState=start\nMainPID=0\nInvocationID=" + "a" * 32 + "\n"
                 "ControlGroup=/system.slice/broker.instance\n"
                 "ExecMainStartTimestampMonotonic=0\nExecMainStatus=0\nResult=success\n").encode()
    pipe_error = b"fixture-stderr\n"
    def pipe_case(mode: str) -> None:
        if mode in ("stdout-first", "stderr-first"):
            first = 1 if mode == "stdout-first" else 2
            second = 2 if first == 1 else 1
            bodies = {1: pipe_show, 2: pipe_error}
            code = (f"import os\nos.write({first}, {bodies[first]!r})\nos.close({first})\n"
                    f"os.read(0, 1)\nos.write({second}, {bodies[second]!r})\nos.close({second})\n")
        elif mode == "simultaneous":
            code = f"import os\nos.write(1, {pipe_show!r})\nos.write(2, {pipe_error!r})\n"
        else:
            code = "import os\nos.write(1, b'partial-show\\n')\nos.read(0, 1)\n"
        client = subprocess.Popen([sys.executable, "-c", code],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            close_fds=True, start_new_session=True, bufsize=0)
        assert client.stdout is not None and client.stderr is not None and client.stdin is not None
        stdout_fd, stderr_fd = client.stdout.fileno(), client.stderr.fileno()
        os.set_blocking(stdout_fd, False)
        os.set_blocking(stderr_fd, False)
        bus_read, bus_write = os.pipe()
        class QuietBus:
            bus = ctypes.c_void_p(1)
            bus_fd = lambda self, _bus: bus_read
            bus_events = lambda self, _bus: select.POLLIN
            bus_timeout = lambda self, _bus, pointer: (
                setattr(pointer._obj, "value", (1 << 64) - 1) or 0)

        reader = object.__new__(BrokerObserver)
        reader.bus = QuietBus()
        reader.deadline = time.monotonic() + 2.0
        reader.writer = PipeWriter()
        row = Lifecycle(lifecycle.unit, lifecycle.object_path, 5, 1, 1)
        snap = {"raw_show": None, "raw_stderr": None, "process_capture_complete": False,
                "manager_properties_complete": False, "incomplete_reasons": [], "main_pid": 0,
                "process_start_observed_before_query": False, "trigger": "unit-new",
                "trigger_event_seq": 1, "started_monotonic_ns": time.monotonic_ns()}
        row.snapshots.append(snap)
        query = {"lifecycle": row, "snapshot": snap, "process": client,
                 "stdout": bytearray(), "stderr": bytearray(),
                 "open": {stdout_fd: "stdout", stderr_fd: "stderr"},
                 "deadline": time.monotonic() + 1.5, "capture_number": 1,
                 "cancel_reason": None, "limit_reason": None}
        reader.current_query = query
        stop_started: float | None = None
        try:
            if mode == "simultaneous":
                client.wait(timeout=1.0)
            else:
                first_stream = client.stderr if mode == "stderr-first" else client.stdout
                for _ in range(20):
                    reader._poll_once()
                    if first_stream.closed or (mode in ("cancel", "deadline") and query["stdout"]):
                        break
                else:
                    raise AssertionError("first local pipe did not become readable")
                if mode in ("stdout-first", "stderr-first"):
                    assert first_stream.closed and reader.current_query is query
                    client.stdin.write(b"x")
                    client.stdin.close()
                elif mode == "cancel":
                    stop_started = time.monotonic()
                    query["cancel_reason"] = "unit_removed_during_snapshot"
                else:
                    stop_started = time.monotonic()
                    query["deadline"] = time.monotonic() - 1.0
            for _ in range(30):
                if reader.current_query is None:
                    break
                reader._poll_once()
            assert reader.current_query is None and not query["open"]
            assert client.stdout.closed and client.stderr.closed
            assert client.poll() is not None
            assert reader.writer.contents["show-000001.txt"] == (
                b"partial-show\n" if mode in ("cancel", "deadline") else pipe_show)
            assert reader.writer.contents["show-000001.stderr"] == (
                b"" if mode in ("cancel", "deadline") else pipe_error)
            if mode in ("cancel", "deadline"):
                assert stop_started is not None and time.monotonic() - stop_started < \
                       SHOW_TIMEOUT_SECONDS + 1.0
                assert not snap["manager_properties_complete"]
                if mode == "cancel":
                    assert snap["systemctl_cancelled"] == "unit_removed_during_snapshot"
                    assert not snap["systemctl_timed_out"]
                else:
                    assert snap["systemctl_timed_out"] and snap["systemctl_timeout_reason"] == \
                           "systemctl_timeout"
            else:
                assert snap["systemctl_exit"] == 0 and snap["manager_properties_complete"]
                assert not snap["systemctl_timed_out"] and snap["systemctl_cancelled"] is None
        finally:
            if reader.current_query is not None:
                reader._stop_query(query, "fixture_cleanup")
            if not client.stdin.closed:
                client.stdin.close()
            if client.poll() is None:
                client.kill()
                client.wait(timeout=1.0)
            for stream in (client.stdout, client.stderr):
                if not stream.closed:
                    stream.close()
            os.close(bus_read)
            os.close(bus_write)

    for pipe_mode in ("stdout-first", "stderr-first", "simultaneous", "cancel", "deadline"):
        pipe_case(pipe_mode)
        checks += 1

    fixture_observer = object.__new__(BrokerObserver)
    fixture_observer.writer = FixtureWriter()
    fixture_observer.boot_id = boot
    fixture_observer.boot_id_raw = raw
    fixture_observer.current_query = None
    fixture_lifecycle = Lifecycle(lifecycle.unit, lifecycle.object_path, 4, 7, 40)
    show = (f"Id={lifecycle.unit}\nLoadState=loaded\nActiveState=activating\n"
            "SubState=start\nMainPID=0\nInvocationID=" + "a" * 32 + "\n"
            "ControlGroup=/system.slice/broker.instance\n"
            "ExecMainStartTimestampMonotonic=912345\nExecMainStatus=0\nResult=success\n")
    client = subprocess.Popen(["/usr/bin/true"], stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
    client.wait(timeout=1.0)
    initial = {"raw_show": None, "raw_stderr": None, "process_capture_complete": False,
               "manager_properties_complete": False, "incomplete_reasons": [],
               "main_pid": 0, "process_start_observed_before_query": False,
               "trigger": "unit-new", "trigger_event_seq": 7,
               "started_monotonic_ns": time.monotonic_ns()}
    fixture_lifecycle.snapshots.append(initial)
    query = {"lifecycle": fixture_lifecycle, "snapshot": initial, "process": client,
             "stdout": bytearray(show.encode()), "stderr": bytearray(),
             "capture_number": 1, "limit_reason": None, "cancel_reason": None}
    fixture_observer.current_query = query
    fixture_observer._finish_query(query)
    assert initial["manager_properties_complete"] and initial["preliminary_property_miss"]
    assert fixture_lifecycle.invocation_id is None and not fixture_lifecycle.process_started
    checks += 1
    def fixture_capture(row: Lifecycle, snap: dict[str, object], pid: int, number: int) -> None:
        assert pid == 741 and number == 2
        snap.update({"start_ticks": 5812, "exe": "/usr/local/libexec/buster-bench-systemd-broker",
                     "exe_sha256": "f" * 64, "socket_fd0": {"stable": True},
                     "proc_capture": {"proc-status": {"bytes": 1}}})
        row.start_ticks = 5812
        row.main_pid = pid
        row.cgroup = str(snap["cgroup"])
        row.exe = str(snap["exe"])
        row.exe_sha256 = str(snap["exe_sha256"])

    fixture_observer._capture_process = fixture_capture
    positive_show = show.replace("MainPID=0", "MainPID=741").replace(
        "InvocationID=" + "a" * 32, "InvocationID=" + "b" * 32)
    for number, source in ((2, positive_show), (3, show)):
        client = subprocess.Popen(["/usr/bin/true"], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
        client.wait(timeout=1.0)
        snap = {"boot_id": boot, "unit": lifecycle.unit,
                "object_path": lifecycle.object_path, "generation": 4,
                "raw_show": None, "raw_stderr": None,
                "process_capture_complete": False, "manager_properties_complete": False,
                "incomplete_reasons": [], "main_pid": 0,
                "process_start_observed_before_query": number == 3,
                "trigger": "mainpid:741", "trigger_event_seq": number + 7,
                "started_monotonic_ns": time.monotonic_ns()}
        fixture_lifecycle.snapshots.append(snap)
        query = {"lifecycle": fixture_lifecycle, "snapshot": snap, "process": client,
                 "stdout": bytearray(source.encode()), "stderr": bytearray(),
                 "capture_number": number, "limit_reason": None, "cancel_reason": None}
        fixture_observer.current_query = query
        fixture_observer._finish_query(query)
    fixture_lifecycle.removed = True
    fixture_lifecycle.removed_event_seq = 20
    fixture_lifecycle.removed_monotonic_ns = time.monotonic_ns()
    assert fixture_lifecycle.invocation_id == "b" * 32
    assert fixture_lifecycle.main_pid == 741 and fixture_lifecycle.start_ticks == 5812
    assert fixture_lifecycle.snapshots[1]["complete"]
    assert fixture_lifecycle.snapshots[2]["main_pid"] == 0
    assert fixture_lifecycle.snapshots[2]["incomplete_reasons"] == []
    assert fixture_lifecycle.as_json(boot, raw)["process_complete"]
    checks += 1
    class ProbeError(RuntimeError):
        pass

    executor = "/usr/lib/systemd/systemd-executor"
    for failure, active_state, substate, initial_exe, reread_exe, supersedable in (
            (ProbeError("process executable path mismatch for broker-000004: "
                        "/usr/lib/systemd/systemd-executor"),
             "activating", "start", executor, executor, True),
            (ProbeError("process cgroup mismatch for broker-000004"),
             "activating", "start", executor, executor, True),
            (ProbeError("process cgroup mismatch for broker-000004"),
             "activating", "start", executor, BROKER_EXECUTABLE, False),
            (ProbeError("process cgroup mismatch for broker-000004"),
             "activating", "start", BROKER_EXECUTABLE, executor, False),
            (ProbeError("process executable path mismatch for broker-000004: "
                        "/usr/local/bin/other"),
             "activating", "start", "/usr/local/bin/other", "/usr/local/bin/other", False),
            (ProbeError("process executable path mismatch for broker-000004: "
                        "/usr/lib/systemd/systemd-executor"),
             "active", "running", executor, executor, False),
            (ProbeError("running and installed executable hashes differ for broker-000004"),
             "active", "running", BROKER_EXECUTABLE, BROKER_EXECUTABLE, False),
            (ObserverError("accepted socket FD 0 readback is inconsistent"),
             "active", "running", BROKER_EXECUTABLE, BROKER_EXECUTABLE, False),
            (ObserverError("positive MainPID start ticks changed during capture"),
             "active", "running", BROKER_EXECUTABLE, BROKER_EXECUTABLE, False)):
        case_lifecycle = Lifecycle(lifecycle.unit, lifecycle.object_path, 4, 10, 50)
        snap = {"boot_id": boot, "unit": lifecycle.unit,
                "object_path": lifecycle.object_path, "generation": 4,
                "raw_show": None, "raw_stderr": None,
                "process_capture_complete": False, "manager_properties_complete": False,
                "incomplete_reasons": [], "main_pid": 0,
                "process_start_observed_before_query": False,
                "trigger": "mainpid:741", "trigger_event_seq": 10,
                "started_monotonic_ns": time.monotonic_ns()}
        case_lifecycle.snapshots.append(snap)
        client = subprocess.Popen(["/usr/bin/true"], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
        client.wait(timeout=1.0)
        case_show = positive_show.replace("ActiveState=activating", f"ActiveState={active_state}")
        case_show = case_show.replace("SubState=start", f"SubState={substate}")
        query = {"lifecycle": case_lifecycle, "snapshot": snap, "process": client,
                 "stdout": bytearray(case_show.encode()), "stderr": bytearray(),
                 "capture_number": 4, "limit_reason": None, "cancel_reason": None}
        def failed_capture(_row: Lifecycle, row_snapshot: dict[str, object],
                           _pid: int, _number: int) -> None:
            row_snapshot["start_ticks"] = 5812
            row_snapshot["initial_exe_observed"] = initial_exe
            raise failure

        fixture_observer._capture_process = failed_capture
        fixture_observer._read_stable_process_exe = lambda _pid, _ticks, _label: reread_exe
        fixture_observer.current_query = query
        fixture_observer._finish_query(query)
        case_lifecycle.snapshots.append({**witness, "generation": 4})
        case_lifecycle.removed = True
        case_lifecycle.removed_event_seq = 20
        case_lifecycle.removed_monotonic_ns = time.monotonic_ns()
        assert case_lifecycle.as_json(boot, raw)["process_complete"] == supersedable
        assert (snap["process_capture_state"] in SUPERSEDABLE_CAPTURE_STATES) == supersedable
        checks += 1
    class FixtureProcReader:
        @staticmethod
        def _bounded_file(path: Path, maximum: int, deadline: float,
                          reserve: float = 0.0) -> bytes:
            assert str(path) == "/proc/741/stat" and maximum == MAX_PROC_STAT_BYTES
            return b"fixture-stat"

        @staticmethod
        def _proc_stat_identity(_raw: bytes, _label: str) -> tuple[str, int, int]:
            return "S", 1, 5812

    original_probe_module = sys.modules.get("issue1162_live_probe")
    original_readlink = os.readlink
    fixture_observer.deadline = time.monotonic() + 5.0
    try:
        sys.modules["issue1162_live_probe"] = FixtureProcReader()
        links = iter((executor, BROKER_EXECUTABLE, BROKER_EXECUTABLE, BROKER_EXECUTABLE))
        os.readlink = lambda _path: next(links)
        assert BrokerObserver._read_stable_process_exe(
            fixture_observer, 741, 5812, "broker-fixture") == BROKER_EXECUTABLE
        checks += 1
        links = iter((executor, "/usr/local/bin/other"))
        try:
            BrokerObserver._read_stable_process_exe(fixture_observer, 741, 5812,
                                                     "broker-fixture")
        except ObserverError:
            checks += 1
        else:
            raise AssertionError("unknown executable transition was accepted")
    finally:
        os.readlink = original_readlink
        if original_probe_module is None:
            sys.modules.pop("issue1162_live_probe", None)
        else:
            sys.modules["issue1162_live_probe"] = original_probe_module
    reaped = subprocess.Popen(["/usr/bin/true"], stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, start_new_session=True)
    reaped.wait(timeout=1.0)
    assert reaped.stdout is not None and reaped.stderr is not None
    stopped = {"process": reaped,
               "open": {reaped.stdout.fileno(): "stdout", reaped.stderr.fileno(): "stderr"}}
    def refuse_killpg(_pid: int, _signal: int) -> None:
        raise AssertionError("attempted to signal a reaped client's numeric PID")

    original_killpg = os.killpg
    try:
        os.killpg = refuse_killpg
        fixture_observer._stop_query(stopped, "fixture_client_already_reaped")
    finally:
        os.killpg = original_killpg
    assert stopped["open"] == {} and reaped.stdout.closed and reaped.stderr.closed
    assert stopped["cancel_reason"] == "fixture_client_already_reaped"
    checks += 1
    class FixtureEventWriter:
        def __init__(self):
            self.rows: list[dict[str, object]] = []

        def event(self, record: dict[str, object]) -> int:
            self.rows.append(record)
            return 1

    class FixturePathBus:
        def object_path(self, _unit: str) -> str:
            return lifecycle.object_path

    # Run authenticated-shaped manager records through the production pull
    # dispatcher. These are fixtures, never a claim about a live manager.
    class FixtureMetadataBus:
        manager_sender = ":1.4"

        def __init__(self, messages: list[dict[str, object]]):
            self.messages = deque(messages)

        def process_one(self) -> tuple[int, dict[str, object] | None]:
            if self.messages:
                return 1, self.messages.popleft()
            return 0, None

        def object_path(self, _unit: str) -> str:
            return lifecycle.object_path

    manager_record = {"interface": "org.freedesktop.systemd1.Manager",
                      "path": "/org/freedesktop/systemd1", "signature": "so",
                      "sender": ":1.4"}
    template_new = {**manager_record, "member": "UnitNew",
                    "unit": TEMPLATE_METADATA_UNIT,
                    "object_path": TEMPLATE_METADATA_PATH}
    template_removed = {**template_new, "member": "UnitRemoved"}
    canonical_new = {**manager_record, "member": "UnitNew",
                     "unit": lifecycle.unit, "object_path": lifecycle.object_path}

    def template_fixture(messages: list[dict[str, object]]) -> BrokerObserver:
        fixture = BrokerObserver(FixtureMetadataBus(messages), FixtureEventWriter(),
                                 boot, raw, time.monotonic() + 5.0, "1", 1)
        fixture.ready_ns = time.monotonic_ns() - 1_000_000
        fixture._current_process_events()
        return fixture

    paired = template_fixture([template_new, template_removed, canonical_new])
    retained = paired.template_metadata_events
    expected_template_keys = {"boot_id", "boot_id_raw", "unit", "object_path",
                              "sender", "member", "interface", "path", "signature",
                              "bus_dispatch_ordinal", "bus_dispatch_monotonic_ns",
                              "monotonic_ns"}
    assert len(retained) == 2 and all(set(item) == expected_template_keys for item in retained)
    assert [item["member"] for item in retained] == ["UnitNew", "UnitRemoved"]
    assert [item["bus_dispatch_ordinal"] for item in retained] == [1, 2]
    assert paired.ready_ns < retained[0]["monotonic_ns"] < \
           retained[1]["monotonic_ns"] < paired.units[0].first_monotonic_ns
    assert paired.bus_dispatch_count == 3 and paired.event_seq == 1
    assert len(paired.units) == 1 and not paired.global_reasons
    checks += 1
    empty = template_fixture([canonical_new])
    assert not empty.template_metadata_events and not empty.global_reasons
    checks += 1
    partial = template_fixture([template_new])
    partial._check_template_metadata_completion()
    assert len(partial.template_metadata_events) == 1 and \
           partial.global_reasons == ["template_metadata_unpaired"]
    checks += 1
    rejected_templates = (
        ("reversed", [template_removed]),
        ("repeated", [template_new, template_removed, template_new]),
        ("late", [canonical_new, template_new, template_removed]),
        ("wrong-path", [{**template_new, "object_path": BASE_UNIT_PATH + "/other"}]),
        ("wrong-name", [{**template_new, "unit": TEMPLATE_METADATA_UNIT + "x"}]),
        ("wrong-sender", [{**template_new, "sender": ":1.9"}]),
        ("wrong-interface", [{**template_new, "interface": "org.freedesktop.DBus.Properties"}]),
        ("wrong-signature", [{**template_new, "signature": "s"}]),
        ("malformed-prefix-wrong-path", [{**template_new,
            "unit": "buster-bench-systemd-broker@bad.service",
            "object_path": BASE_UNIT_PATH + "/other"}]),
        ("template-properties", [template_new, template_removed,
            {"member": "PropertiesChanged", "path": TEMPLATE_METADATA_PATH,
             "sender": ":1.4", "unit_interface": "org.freedesktop.systemd1.Service",
             "changed_properties": [], "invalidated_properties": [], "main_pids": []}]),
    )
    for label, messages in rejected_templates:
        rejected_template = template_fixture(messages)
        assert rejected_template.event_loss_detected and rejected_template.global_reasons, label
        assert len(rejected_template.template_metadata_events) <= 2, label
        checks += 1
    over_budget = BrokerObserver(FixtureMetadataBus([template_new]), FixtureEventWriter(),
                                 boot, raw, time.monotonic() + 5.0, "1", 1)
    over_budget.ready_ns = time.monotonic_ns() - 1_000_000
    over_budget.bus_dispatch_count = MAX_BUS_DISPATCHES
    over_budget._current_process_events()
    assert over_budget.bus_budget_exhausted and not over_budget.template_metadata_events
    assert over_budget.global_reasons == ["manager_bus_dispatch_rate_exceeded"]
    checks += 1

    event_observer = object.__new__(BrokerObserver)
    event_observer.bus = FixturePathBus()
    event_observer.writer = FixtureEventWriter()
    event_observer.boot_id = boot
    event_observer.boot_id_raw = raw
    event_observer.event_seq = 0
    event_observer.event_count_by_unit = {}
    event_observer.current_query = None
    event_observer.units = []
    event_observer.current_by_path = {}
    event_observer.last_by_path = {}
    event_observer.generation_by_unit = {}
    event_observer.queue = deque()
    event_observer.global_reasons = []
    event_observer.event_loss_detected = False
    event_observer.dispatch_diagnostics = BrokerObserver._new_dispatch_diagnostics()
    event_observer.bus_dispatch_count = 1
    event_observer._capture_positive_signal = lambda *_args: None
    event_observer.template_metadata_events = []
    event_observer.deadline = time.monotonic() + 5.0
    event_observer.stop_seen = False
    event_observer.handle_message({"member": "UnitNew", "interface": "org.freedesktop.systemd1.Manager",
        "sender": ":1.4", "unit": lifecycle.unit, "object_path": lifecycle.object_path})
    event_observer.handle_message({"member": "PropertiesChanged",
        "unit_interface": "org.freedesktop.systemd1.Service", "sender": ":1.4",
        "path": lifecycle.object_path, "changed_properties": ["MainPID"],
        "invalidated_properties": [], "main_pids": [741]})
    event_observer.handle_message({"member": "UnitRemoved",
        "interface": "org.freedesktop.systemd1.Manager", "sender": ":1.4",
        "unit": lifecycle.unit, "object_path": lifecycle.object_path})
    event_lifecycle = event_observer.units[0]
    rows = event_observer.writer.rows
    assert event_lifecycle.first_monotonic_ns == rows[0]["monotonic_ns"]
    assert event_lifecycle.last_monotonic_ns == rows[2]["monotonic_ns"]
    assert event_lifecycle.removed_monotonic_ns == rows[2]["monotonic_ns"]
    assert event_lifecycle.main_pid_values == {741}
    checks += 1
    for reloading in (True, False):
        event_observer.handle_message({"member": "Reloading",
            "interface": "org.freedesktop.systemd1.Manager",
            "path": "/org/freedesktop/systemd1", "sender": ":1.4",
            "reloading": reloading})
        assert rows[-1]["event"] == "Reloading" and rows[-1]["reloading"] is reloading
        assert event_observer.event_loss_detected
        assert "manager_reloading_during_observation" in event_observer.global_reasons
        checks += 1
    # Attempt 20's @3 sequence: three same-generation, same-PID queries all
    # completed in the known systemd-executor pre-exec phase, then the manager
    # emitted another Service MainPID=760 signal after the third completion.
    # The first/unique MainPID triggers are already consumed at that point.
    retry_unit = "buster-bench-systemd-broker@3-758-65000.service"
    retry_path = (BASE_UNIT_PATH_ENCODED_BROKER_PREFIX +
                  "3_2d758_2d65000_2eservice")
    retry_row = Lifecycle(retry_unit, retry_path, 1, 40, time.monotonic_ns() - 80_000_000)
    retry_row.triggers = {"unit-new", "properties-first", "mainpid:760"}
    # An initial MainPID=0 preceded the positive PID and cannot prohibit a
    # later positive pre-exec capture merely through unordered set membership.
    retry_row.main_pid_values = {0, 760}
    retry_row.last_mainpid_zero_event_seq = 39
    retry_row.invocation_id = "a" * 32
    retry_row.main_pid = 760
    retry_row.cgroup = "/system.slice/broker.instance"
    retry_row.snapshots = [{"boot_id": boot, "unit": retry_unit,
                            "object_path": retry_path, "generation": 1,
                            "trigger": trigger, "trigger_event_seq": seq,
                            "manager_properties_complete": True,
                            "process_capture_complete": False, "complete": False,
                            "process_capture_state": "known_preexec_executable_transition",
                            "active_state": "activating", "substate": "start",
                            "main_pid": 760, "invocation_id": "a" * 32,
                            "exec_main_start_timestamp_monotonic": 120570000,
                            "cgroup": "/system.slice/broker.instance", "start_ticks": 12056,
                            "initial_exe_observed": "/usr/lib/systemd/systemd-executor",
                            "finished_monotonic_ns": time.monotonic_ns() - 20_000_000}
                           for trigger, seq in (("unit-new", 40), ("properties-first", 41),
                                                ("mainpid:760", 41))]
    retry_observer = object.__new__(BrokerObserver)
    retry_observer.bus = FixturePathBus()
    retry_observer.writer = FixtureEventWriter()
    retry_observer.boot_id = boot
    retry_observer.boot_id_raw = raw
    retry_observer.event_seq = 46
    retry_observer.event_count_by_unit = {}
    retry_observer.current_query = None
    retry_observer.current_by_path = {retry_path: retry_row}
    retry_observer.queue = deque()
    retry_observer.deadline = time.monotonic() + 5.0
    retry_observer.global_reasons = []
    retry_observer.event_loss_detected = False
    retry_observer.dispatch_diagnostics = BrokerObserver._new_dispatch_diagnostics()
    retry_observer.bus_dispatch_count = 1
    retry_observer._capture_positive_signal = lambda *_args: None
    retry_observer.stop_seen = False
    later_service = {"member": "PropertiesChanged", "path": retry_path,
                     "unit_interface": "org.freedesktop.systemd1.Service",
                     "sender": ":1.4", "changed_properties": ["MainPID"],
                     "invalidated_properties": [], "main_pids": [760]}
    retry_observer.handle_message(later_service)
    assert len(retry_observer.queue) == 1
    assert retry_observer.queue[0][0] is retry_row and \
           retry_observer.queue[0][1] == "preexec-retry:3"
    assert len(retry_row.snapshots) == 3 and len(retry_row.triggers) == 4
    checks += 1
    retry_observer.handle_message(later_service)
    assert len(retry_observer.queue) == 1 and len(retry_row.snapshots) == 3
    checks += 1
    retry_observer.queue.clear()
    retry_row.snapshots.append({**retry_row.snapshots[-1],
                                "trigger": "preexec-retry:3",
                                "trigger_event_seq": retry_observer.event_seq,
                                "finished_monotonic_ns": time.monotonic_ns() - 1_000_000})
    retry_observer.handle_message(later_service)
    assert not retry_observer.queue and len(retry_row.triggers) == 4
    checks += 1
    retry_row.triggers.remove("preexec-retry:3")
    retry_observer.queue.clear()
    retry_row.snapshots[0]["start_ticks"] += 1
    retry_observer.handle_message(later_service)
    assert not retry_observer.queue  # An earlier positive capture had another identity.
    retry_row.snapshots[0]["start_ticks"] -= 1
    checks += 1
    retry_row.snapshots[0]["invocation_id"] = "b" * 32
    retry_observer.handle_message(later_service)
    assert not retry_observer.queue
    retry_row.snapshots[0]["invocation_id"] = "a" * 32
    checks += 1
    for blocked in ("hard", "zero", "removed", "deadline", "stop", "full", "cap"):
        saved_deadline = retry_observer.deadline
        saved_stop = retry_observer.stop_seen
        saved_zero_seq = retry_row.last_mainpid_zero_event_seq
        saved_snapshot = retry_row.snapshots[-1]
        saved_triggers = retry_row.triggers.copy()
        if blocked == "hard":
            retry_row.process_incomplete_reasons.append("identity_contradiction")
        elif blocked == "zero":
            retry_row.last_mainpid_zero_event_seq = retry_observer.event_seq + 1
        elif blocked == "removed":
            retry_row.removed = True
        elif blocked == "deadline":
            retry_observer.deadline = time.monotonic() - 1.0
        elif blocked == "stop":
            retry_observer.stop_seen = True
        elif blocked == "full":
            retry_row.snapshots[-1] = {**saved_snapshot, "complete": True,
                                       "process_capture_state": "full_process_witness"}
        else:
            retry_row.triggers.update(("bounded-extra-1", "bounded-extra-2"))
        retry_observer._schedule_preexec_retry(retry_row, 760,
                                               retry_observer.event_seq + 1,
                                               time.monotonic_ns())
        assert not retry_observer.queue, blocked
        retry_observer.deadline = saved_deadline
        retry_observer.stop_seen = saved_stop
        retry_row.snapshots[-1] = saved_snapshot
        retry_row.triggers = saved_triggers
        retry_row.process_incomplete_reasons.clear()
        retry_row.last_mainpid_zero_event_seq = saved_zero_seq
        retry_row.removed = False
        checks += 1
    retry_row.triggers.add("preexec-retry:3")
    retry_row.main_pid_values = {760}
    retry_row.last_mainpid_zero_event_seq = None
    retry_observer.handle_message({**later_service, "main_pids": [0]})
    assert retry_row.last_mainpid_zero_event_seq == retry_observer.event_seq
    assert len(retry_observer.queue) == 1 and retry_observer.queue[0][1] == "mainpid:0"
    assert len(retry_row.triggers) == MAX_PRESTOP_SNAPSHOTS_PER_GENERATION
    retry_observer.queue.clear()
    retry_observer.handle_message(later_service)
    assert not retry_observer.queue  # Terminal zero precedes this positive signal.
    checks += 1
    retry_observer.bus.last_dispatch_kind = "positive_null"
    retry_observer.bus_dispatch_count = 0
    retry_observer.dispatch_buckets = []
    retry_observer.dispatch_window = deque()
    retry_observer.dispatch_window_total = 0
    retry_observer.ready_ns = time.monotonic_ns() - 100_000_000
    retry_observer._record_dispatch(None)
    retry_observer.bus.last_dispatch_kind = "observer_control"
    retry_observer._record_dispatch(None)
    retry_observer.bus.last_dispatch_kind = "ignored_unrelated_properties"
    retry_observer._record_dispatch(None)
    retry_observer._record_dispatch({"member": "UnitNew"})
    counters = retry_observer.dispatch_diagnostics
    assert sum(counters[key] for key in ("positive_with_message", "positive_null",
           "ignored_unrelated_properties", "observer_control",
           "unclassified_without_message")) == retry_observer.bus_dispatch_count == 4
    checks += 1
    for member in ("UnitNew", "UnitRemoved"):
        retry_observer._unit_signal({"member": member, "unit": "unrelated.socket",
            "object_path": "/org/freedesktop/systemd1/unit/unrelated_2esocket"})
    assert counters["subsets"]["ignored_unrelated_unit_new"] == 1
    assert counters["subsets"]["ignored_unrelated_unit_removed"] == 1
    checks += 1
    rejected = {"member": "UnitNew", "interface": "org.freedesktop.systemd1.Manager",
                "path": "/org/freedesktop/systemd1", "signature": "so", "sender": ":1.4",
                "unit": "buster-bench-systemd-broker@notcanonical.service",
                "object_path": retry_path + "_2dbad"}
    for _ in range(MAX_NONCANONICAL_SAMPLES + 1):
        retry_observer._unit_signal(rejected)
    assert counters["subsets"]["noncanonical_broker_unit_signals"] == 5
    assert len(counters["noncanonical_samples"]) == MAX_NONCANONICAL_SAMPLES
    assert counters["noncanonical_samples_omitted"] == 1
    assert counters["noncanonical_samples"][0] == rejected
    assert "noncanonical_broker_unit_signal" in retry_observer.global_reasons
    assert len(json.dumps(counters).encode()) <= MAX_DISPATCH_DIAGNOSTIC_BYTES
    checks += 1
    bounded_observer = object.__new__(BrokerObserver)
    bounded_observer.dispatch_diagnostics = BrokerObserver._new_dispatch_diagnostics()
    bounded_observer._record_noncanonical_unit({**rejected, "unit": "x" * 256})
    bounded_sample = bounded_observer.dispatch_diagnostics["noncanonical_samples"][0]
    assert bounded_sample["unit_truncated"] is True
    assert bounded_sample["unit_utf8_bytes"] == 256
    assert bounded_sample["unit_sha256"] == hashlib.sha256(b"x" * 256).hexdigest()
    checks += 1
    # A real local subprocess and production pipe/terminal adapters exercise
    # the retained failed state; no system manager or service is contacted.
    terminal_show = (f"Id={lifecycle.unit}\nLoadState=loaded\nCollectMode=inactive\n"
        "ActiveState=failed\nSubState=failed\nMainPID=0\nResult=exit-code\n"
        "InvocationID=" + "b" * 32 + "\nExecMainPID=741\nExecMainCode=1\n"
        "ExecMainStatus=1\nExecMainStartTimestampMonotonic=912345\n"
        "ExecMainExitTimestampMonotonic=912346\n"
        "ControlGroup=/system.slice/broker.instance\n")
    terminal_witness = {"boot_id": boot, "unit": lifecycle.unit,
        "object_path": lifecycle.object_path, "generation": 9,
        "invocation_id": "b" * 32, "main_pid": 741,
        "exec_main_start_timestamp_monotonic": 912345,
        "cgroup": "/system.slice/broker.instance", "start_ticks": 5812,
        "complete": True, "manager_properties_complete": True,
        "process_capture_complete": True,
        "finished_monotonic_ns": time.monotonic_ns() - 10_000_000}
    class TerminalWriter:
        def __init__(self):
            self.contents: dict[str, bytes] = {}

        def capture(self, filename: str, content: bytes,
                    maximum: int) -> dict[str, object]:
            assert len(content) <= maximum and filename not in self.contents
            self.contents[filename] = content
            return {"path": "captures/" + filename, "bytes": len(content),
                    "sha256": hashlib.sha256(content).hexdigest()}

    terminal_bus_read, terminal_bus_write = os.pipe()
    class TerminalBus:
        bus = ctypes.c_void_p(1)
        manager_sender = ":1.4"
        bus_fd = lambda self, _bus: terminal_bus_read
        bus_events = lambda self, _bus: select.POLLIN
        bus_timeout = lambda self, _bus, pointer: (
            setattr(pointer._obj, "value", (1 << 64) - 1) or 0)
        process_one = lambda self: (0, None)

    original_show_argv = systemctl_show_argv
    try:
        for changed, accepted in ((None, True),
                                  ("CollectMode=manual", False),
                                  ("InvocationID=" + "c" * 32, False),
                                  ("ExecMainPID=742", False),
                                  ("ExecMainStartTimestampMonotonic=912344", False),
                                  ("ExecMainStatus=0", False),
                                  ("ExecMainExitTimestampMonotonic=912345", False)):
            terminal_raw = terminal_show if changed is None else terminal_show.replace(
                changed.split("=", 1)[0] + "=" +
                (terminal_show.split(changed.split("=", 1)[0] + "=", 1)[1].split("\n", 1)[0]),
                changed)
            globals()["systemctl_show_argv"] = lambda _unit, payload=terminal_raw.encode(): [
                sys.executable, "-c",
                "import sys;sys.stdout.buffer.write(" + repr(payload) + ")"]
            terminal_row = Lifecycle(lifecycle.unit, lifecycle.object_path, 9, 2,
                                     time.monotonic_ns() - 50_000_000)
            terminal_row.last_event_seq = 5
            terminal_row.snapshots = [{**terminal_witness}]
            terminal_row.invocation_id = "b" * 32
            terminal_row.main_pid = 741
            terminal_row.process_started = True
            terminal_row.main_pid_values = {0, 741}
            terminal_observer = object.__new__(BrokerObserver)
            terminal_observer.bus = TerminalBus()
            terminal_observer.writer = TerminalWriter()
            terminal_observer.boot_id = boot
            terminal_observer.boot_id_raw = raw
            terminal_observer.deadline = time.monotonic() + 5.0
            terminal_observer.stop_seen = True
            terminal_observer.stop_monotonic_ns = time.monotonic_ns()
            terminal_observer.terminal_phase_started_monotonic_ns = None
            terminal_observer.terminal_phase_finished_monotonic_ns = None
            terminal_observer.units = [terminal_row]
            terminal_observer.queue = deque()
            terminal_observer.current_query = None
            terminal_observer.snapshot_number = 1
            terminal_observer.bus_dispatch_count = 0
            terminal_observer.dispatch_diagnostics = BrokerObserver._new_dispatch_diagnostics()
            terminal_observer.dispatch_buckets = []
            terminal_observer.dispatch_window = deque()
            terminal_observer.dispatch_window_total = 0
            terminal_observer.ready_ns = time.monotonic_ns() - 100_000_000
            terminal_observer.bus_budget_exhausted = False
            terminal_observer.global_reasons = []
            terminal_observer.event_loss_detected = False
            terminal_observer._run_terminal_phase()
            terminal_record = terminal_row.retained_failed_terminal
            assert terminal_record is not None and terminal_record["manager_properties_complete"] == accepted
            assert terminal_record["event_seq"] == 5 and terminal_observer.snapshot_number == 2
            assert len(terminal_row.snapshots) == 1
            assert terminal_observer.writer.contents["show-000002.txt"] == terminal_raw.encode()
            assert terminal_observer.terminal_phase_finished_monotonic_ns - \
                terminal_observer.stop_monotonic_ns < 30_000_000_000
            assert terminal_row.as_json(boot, raw)["process_complete"] == accepted
            checks += 1
            if accepted:
                terminal_row.last_event_seq = 6
                assert not terminal_row.as_json(boot, raw)["manager_lifecycle_complete"]
                terminal_row.removed = True
                terminal_row.removed_event_seq = 6
                terminal_row.removed_monotonic_ns = int(terminal_record["finished_monotonic_ns"]) + 1
                assert terminal_row.as_json(boot, raw)["process_complete"]
                assert terminal_row.retained_failed_terminal is terminal_record
                checks += 1
        stderr_row = Lifecycle(lifecycle.unit, lifecycle.object_path, 9, 2,
                               time.monotonic_ns() - 50_000_000)
        stderr_row.last_event_seq = 5
        stderr_row.snapshots = [{**terminal_witness}]
        stderr_row.invocation_id = "b" * 32
        stderr_row.main_pid = 741
        stderr_row.process_started = True
        globals()["systemctl_show_argv"] = lambda _unit: [
            sys.executable, "-c", "import sys;sys.stdout.buffer.write(" +
            repr(terminal_show.encode()) +
            ");sys.stderr.buffer.write(b'terminal-stderr\\n')"]
        terminal_observer.units = [stderr_row]
        terminal_observer.writer = TerminalWriter()
        terminal_observer.snapshot_number = 2
        terminal_observer.stop_monotonic_ns = time.monotonic_ns()
        terminal_observer._run_terminal_phase()
        assert terminal_observer.writer.contents["show-000003.stderr"] == b"terminal-stderr\n"
        assert stderr_row.retained_failed_terminal["raw_show"] is not None
        assert stderr_row.retained_failed_terminal["raw_stderr"] is not None
        assert not stderr_row.retained_failed_terminal["manager_properties_complete"]
        assert "terminal_systemctl_stderr_not_empty" in \
               stderr_row.retained_failed_terminal["incomplete_reasons"]
        checks += 1
        globals()["systemctl_show_argv"] = lambda _unit: [
            sys.executable, "-c",
            "import sys;sys.stdout.buffer.write(" + repr(terminal_show.encode()) + ")"]
        terminal_row = Lifecycle(lifecycle.unit, lifecycle.object_path, 9, 2,
                                 time.monotonic_ns() - 50_000_000)
        terminal_row.last_event_seq = 5
        terminal_row.snapshots = [{**terminal_witness, "complete": False}]
        terminal_row.invocation_id = "b" * 32
        terminal_row.main_pid = 741
        terminal_observer.units = [terminal_row]
        terminal_observer.writer = TerminalWriter()
        terminal_observer.snapshot_number = 2
        terminal_observer.global_reasons = []
        terminal_observer.stop_monotonic_ns = time.monotonic_ns()
        terminal_observer._run_terminal_phase()
        assert terminal_row.retained_failed_terminal is not None
        assert not terminal_row.retained_failed_terminal["manager_properties_complete"]
        assert any("unique earlier full process witness" in reason for reason in
                   terminal_row.retained_failed_terminal["incomplete_reasons"])
        checks += 1
        cancelled_row = Lifecycle(lifecycle.unit, lifecycle.object_path, 9, 2,
                                  time.monotonic_ns() - 50_000_000)
        cancelled_row.last_event_seq = 5
        cancelled_row.snapshots = [{**terminal_witness}]
        cancelled_row.invocation_id = "b" * 32
        cancelled_row.main_pid = 741
        terminal_observer.units = [cancelled_row]
        terminal_observer.writer = TerminalWriter()
        terminal_observer.snapshot_number = 2
        terminal_observer._start_terminal_query(cancelled_row, time.monotonic() + 3.0)
        cancelled_query = terminal_observer.current_query
        assert cancelled_query is not None
        terminal_observer._stop_query(cancelled_query, "unit_removed_during_snapshot")
        terminal_observer._finish_query(cancelled_query)
        assert cancelled_row.retained_failed_terminal["systemctl_cancelled"] == \
               "unit_removed_during_snapshot"
        assert not cancelled_row.retained_failed_terminal["manager_properties_complete"]
        assert cancelled_row.manager_incomplete_reasons
        checks += 1
        terminal_observer.stop_seen = False
        try:
            terminal_observer._start_terminal_query(cancelled_row, time.monotonic() + 3.0)
        except ObserverError:
            checks += 1
        else:
            raise AssertionError("pre-stop terminal query was accepted")
        terminal_observer.stop_seen = True
        # Authenticated removal before a queued, never-started query preserves
        # the record and reason; only an earlier complete witness makes it redundant.
        removed_row = Lifecycle(lifecycle.unit, lifecycle.object_path, 10, 1,
                                time.monotonic_ns() - 30_000_000)
        removed_row.snapshots = [{**terminal_witness, "generation": 10}]
        removed_row.invocation_id = "b" * 32
        removed_row.main_pid = 741
        removed_row.process_started = True
        removed_row.removed = True
        removed_row.removed_event_seq = 5
        removed_row.removed_monotonic_ns = time.monotonic_ns() - 1_000_000
        missing = {"trigger_event_seq": 3, "started_monotonic_ns": time.monotonic_ns(),
                   "incomplete_reasons": [], "raw_show": None, "raw_stderr": None,
                   "proc_capture": {}, "systemctl_exit": None,
                   "systemctl_timed_out": False, "systemctl_timeout_reason": None,
                   "systemctl_cancelled": None, "main_pid": 0}
        removed_row.snapshots.append(missing)
        BrokerObserver._record_unstarted_query(removed_row, missing,
                                                "unit_removed_before_snapshot")
        assert missing["post_removal_unstarted"] is True
        assert missing["incomplete_reasons"] == ["unit_removed_before_snapshot"]
        assert removed_row.as_json(boot, raw)["process_complete"]
        checks += 1
        missing["trigger_event_seq"] = removed_row.removed_event_seq
        assert not removed_row.as_json(boot, raw)["manager_lifecycle_complete"]
        missing["trigger_event_seq"] = 3
        checks += 1
        removed_row.snapshots[0]["complete"] = False
        assert not removed_row.as_json(boot, raw)["process_complete"]
        checks += 1
        # Reserving the sixth slot and the stop-origin deadline both fail
        # closed without launching a seventh or late terminal query.
        terminal_row.retained_failed_terminal = None
        terminal_row.removed = False
        terminal_row.manager_incomplete_reasons.clear()
        terminal_row.snapshots = [{**terminal_witness} for _ in range(6)]
        terminal_observer.global_reasons = []
        terminal_observer.units = [terminal_row]
        terminal_observer._run_terminal_phase()
        assert terminal_row.retained_failed_terminal is None
        assert "terminal_query_slot_unavailable" in terminal_row.manager_incomplete_reasons
        checks += 1
        terminal_row.snapshots = [{**terminal_witness}]
        terminal_row.manager_incomplete_reasons.clear()
        terminal_observer.stop_monotonic_ns = time.monotonic_ns() - 31_000_000_000
        terminal_observer.global_reasons = []
        terminal_observer._run_terminal_phase()
        assert terminal_row.retained_failed_terminal is None
        assert "terminal_phase_deadline_exceeded" in terminal_observer.global_reasons
        checks += 1
    finally:
        globals()["systemctl_show_argv"] = original_show_argv
        os.close(terminal_bus_read)
        os.close(terminal_bus_write)
    class FixtureFinalBus:
        def __init__(self, messages: int):
            self.remaining = messages

        def process_one(self) -> tuple[int, None]:
            if self.remaining:
                self.remaining -= 1
                return 1, None
            return 0, None

    for queued, expected_loss in ((0, False), (MAX_DISPATCHES_PER_DRAIN - 1, False),
                                  (MAX_DISPATCHES_PER_DRAIN, True)):
        final_observer = object.__new__(BrokerObserver)
        final_observer.bus = FixtureFinalBus(queued)
        final_observer.deadline = time.monotonic() + 5.0
        final_observer.bus_dispatch_count = 0
        final_observer.dispatch_diagnostics = BrokerObserver._new_dispatch_diagnostics()
        final_observer.dispatch_buckets = []
        final_observer.dispatch_window = deque()
        final_observer.dispatch_window_total = 0
        final_observer.ready_ns = time.monotonic_ns() - 100_000_000
        final_observer.bus_budget_exhausted = False
        final_observer.global_reasons = []
        final_observer.event_loss_detected = False
        final_observer._drain_final_events()
        assert final_observer.event_loss_detected == expected_loss
        assert final_observer.bus_dispatch_count == queued
        checks += 1
    import issue1162_live_probe as state_probe
    def fd0_state_fixture(pid, ticks, _cgroup, _executable, _output, deadline):
        # The state fixture runs on an ordinary hosted runner account. Exercise
        # the real FD-0 readback with the existing bounded in-memory writer;
        # production EvidenceWriter's root-owned directory rule belongs only
        # to the guest observer's evidence path.
        writer = FixtureWriter()
        reader = object.__new__(BrokerObserver)
        reader.writer, reader.deadline = writer, deadline
        result = reader._capture_fd0(pid, ticks, "state-fixture", 1)
        assert result["identity"]["stable"] and result["identity"]["start_ticks_after"] == ticks
    checks += state_probe._self_test_live_state_capture(fd0_state_fixture)
    for before_state, after_state in (("S", "R"), ("R", "S")):
        assert BrokerObserver._stable_exe_identity((before_state, 123, 456), BROKER_EXECUTABLE,
            (after_state, 123, 456), BROKER_EXECUTABLE, 456)
        checks += 1
    for state in ("Z", "X", "x"):
        assert not BrokerObserver._stable_exe_identity((state, 123, 456), BROKER_EXECUTABLE,
            ("R", 123, 456), BROKER_EXECUTABLE, 456)
        assert not BrokerObserver._stable_exe_identity(("S", 123, 456), BROKER_EXECUTABLE,
            (state, 123, 456), BROKER_EXECUTABLE, 456)
        checks += 2
    # Use the production accounting seam with a virtual monotonic clock. The
    # 300-second, 46 unrelated messages/second case exceeds the former 4096
    # lifetime cap without approaching either new rate limit.
    original_clock = time.monotonic_ns
    clock = [10_000_000_000]
    try:
        time.monotonic_ns = lambda: clock[0]
        rate_bus = FixtureMetadataBus([])
        rate_bus.last_dispatch_kind = "ignored_unrelated_properties"
        rate_observer = BrokerObserver(rate_bus, FixtureEventWriter(), boot, raw,
                                       time.monotonic() + 5.0, "1", 1)
        rate_observer.ready_ns = clock[0]
        for second in range(300):
            clock[0] = rate_observer.ready_ns + second * 1_000_000_000 + 1_000
            for _ in range(46):
                assert rate_observer._record_dispatch(None)
            assert rate_observer._record_dispatch({"member": "UnitNew"})
        assert rate_observer.bus_dispatch_count == 300 * 47 > 4096
        assert len(rate_observer.dispatch_buckets) == 300 and not rate_observer.global_reasons
        checks += 1
        for pattern, maximum in (([513], 512), ([512] * 5 + [1], 2560)):
            clock[0] += 20_000_000_000
            overloaded = BrokerObserver(rate_bus, FixtureEventWriter(), boot, raw,
                                        time.monotonic() + 5.0, "1", 1)
            overloaded.ready_ns = clock[0]
            for second, amount in enumerate(pattern):
                clock[0] = overloaded.ready_ns + second * 1_000_000_000 + 1_000
                for number in range(amount):
                    accepted = overloaded._record_dispatch(None)
                    assert accepted == (not (second == len(pattern) - 1 and number == amount - 1))
            assert overloaded.bus_budget_exhausted and overloaded.event_loss_detected
            assert overloaded.global_reasons == ["manager_bus_dispatch_rate_exceeded"]
            assert overloaded.bus_dispatch_count == maximum + 1
            checks += 1
    finally:
        time.monotonic_ns = original_clock
    # A signal capture happens synchronously before the queued show can finish.
    # The deliberately held query is a local exited client with synthetic raw
    # output; the capture seam receives real authenticated-shaped signal data.
    class SignalWriter(FixtureEventWriter, FixtureWriter):
        def capture_path(self, name: str, path: Path, maximum: int,
                         exit_code: int | None = 0,
                         timed_out: bool = False) -> dict[str, object]:
            return {**self.capture(name, path.read_bytes(), maximum),
                    "exit": exit_code, "timed_out": timed_out}

    class SignalProbe:
        @staticmethod
        def _bounded_file(path: Path, _maximum: int, _deadline: float,
                          reserve: float = 0.0) -> bytes:
            assert str(path) == "/proc/741/cgroup" and reserve == 0.0
            return b"0::/system.slice/broker.instance\n"

    signal_writer = SignalWriter()
    signal_observer = BrokerObserver(FixtureMetadataBus([]), signal_writer, boot, raw,
                                     time.monotonic() + 5.0, "1", 1)
    signal_observer.ready_ns = time.monotonic_ns() - 1_000_000
    signal_observer.bus_dispatch_count = 1
    signal_observer.handle_message(canonical_new)
    signal_row = signal_observer.units[0]
    order: list[str] = []
    def signal_capture(_row: Lifecycle, capture: dict[str, object], pid: int,
                       _number: int, label: str, bind_lifecycle: bool) -> None:
        assert pid == 741 and label.startswith("signal-") and not bind_lifecycle
        order.append("proc-before-show")
        capture.update({"start_ticks": 5812, "main_pid": pid,
                        "initial_exe_observed": BROKER_EXECUTABLE,
                        "proc_cgroup_raw": "0::/system.slice/broker.instance",
                        "exe": BROKER_EXECUTABLE, "exe_sha256": "f" * 64,
                        "socket_fd0": {"inode": 123, "target": "socket:[123]",
                                       "stable": True, "start_ticks_before": 5812,
                                       "start_ticks_after": 5812},
                        "proc_capture": {**capture["proc_capture"],
                                         "proc-status": {"bytes": 1}}})
    signal_observer._capture_process = signal_capture
    saved_probe = sys.modules.get("issue1162_live_probe")
    try:
        sys.modules["issue1162_live_probe"] = SignalProbe()
        signal_observer.bus_dispatch_count = 2
        signal_observer.handle_message({"member": "PropertiesChanged",
            "unit_interface": "org.freedesktop.systemd1.Service", "sender": ":1.4",
            "path": signal_row.object_path, "changed_properties": ["MainPID"],
            "invalidated_properties": [], "main_pids": [741]})
    finally:
        if saved_probe is None:
            sys.modules.pop("issue1162_live_probe", None)
        else:
            sys.modules["issue1162_live_probe"] = saved_probe
    assert order == ["proc-before-show"] and len(signal_row.provisional_captures) == 1
    signal_record = signal_row.provisional_captures[0]
    assert signal_record["capture_complete"] and signal_record["bound_snapshot_index"] is None
    checks += 1
    # The exact raw manager identity binds the provisional capture; a changed
    # PID/cgroup/start, executable or socket cannot be promoted by a late show.
    signal_show = positive_show.replace("ExecMainStartTimestampMonotonic=912345",
                                        "ExecMainStartTimestampMonotonic=1")
    def finish_signal_case(changed: str | None = None) -> tuple[dict[str, object], Lifecycle]:
        case = Lifecycle(signal_row.unit, signal_row.object_path, 1, 1,
                         signal_row.first_monotonic_ns)
        case.provisional_captures = [dict(signal_record)]
        capture = case.provisional_captures[0]
        if changed == "cgroup":
            capture["cgroup"] = "/system.slice/other"
        elif changed == "ticks":
            case.start_ticks = 5813
        elif changed == "exe":
            capture["exe"] = "/usr/local/bin/other"
        elif changed == "socket":
            capture["socket_fd0"] = {"inode": 456}
        client = subprocess.Popen(["/usr/bin/true"], stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
        client.wait(timeout=1.0)
        snap = {"raw_show": None, "raw_stderr": None, "process_capture_complete": False,
                "manager_properties_complete": False, "incomplete_reasons": [],
                "main_pid": 0, "process_start_observed_before_query": True,
                "trigger": "mainpid:741", "trigger_event_seq": 2,
                "started_monotonic_ns": time.monotonic_ns()}
        case.snapshots.append(snap)
        query = {"lifecycle": case, "snapshot": snap, "process": client,
                 "stdout": bytearray(signal_show.encode()), "stderr": bytearray(),
                 "capture_number": 1, "limit_reason": None, "cancel_reason": None}
        signal_observer.current_query = query
        signal_observer._finish_query(query)
        order.append("show-finished")
        return snap, case
    snap, case = finish_signal_case()
    assert order == ["proc-before-show", "show-finished"] and snap["complete"]
    assert snap["provisional_event_seq"] == 2 and case.provisional_captures[0]["bound_snapshot_index"] == 0
    checks += 1
    for altered in ("cgroup", "ticks"):
        snap, case = finish_signal_case(altered)
        assert not snap["complete"] and case.process_incomplete_reasons
        assert case.provisional_captures[0]["bound_snapshot_index"] is None
        checks += 1
    # Executable/socket alterations remain visible to offline artifact checks
    # even when the manager fields themselves match.
    for altered in ("exe", "socket"):
        snap, case = finish_signal_case(altered)
        assert not snap["complete"] and case.process_incomplete_reasons
        assert case.provisional_captures[0]["bound_snapshot_index"] is None
        checks += 1
    # Exercise the shared real capture adapter with fake bounded /proc reads.
    # It retains the first stat on a later PID-reuse race and requires an
    # independently stable final stat after FD0, without binding a lifecycle.
    class ProcessProbe:
        def __init__(self):
            self.stat_reads = 0
            self.reuse = False
            self.wrong_cgroup = False

        def _bounded_file(self, path: Path, maximum: int, _deadline: float,
                          reserve: float = 0.0) -> bytes:
            assert str(path) == "/proc/741/stat" and maximum == MAX_PROC_STAT_BYTES
            self.stat_reads += 1
            return b"stat-reused" if self.reuse and self.stat_reads >= 2 else b"stat-stable"

        @staticmethod
        def _proc_stat_identity(raw: bytes, _label: str) -> tuple[str, int, int]:
            return ("R", 1, 5813) if raw == b"stat-reused" else ("S", 1, 5812)

        def _capture_process(self, pid: int, label: str, output: Path,
                             _deadline: float, cgroup: str, exe: str,
                             reserve: float = 0.0) -> dict[str, object]:
            assert pid == 741 and cgroup == "/system.slice/broker.instance"
            assert exe == BROKER_EXECUTABLE and reserve == 0.0
            for suffix, data in (("proc-status", b"Pid:\t741\n"),
                                 ("proc-mountinfo", b"1 1 0:1 / / ro - tmpfs tmpfs ro\n"),
                                 ("proc-cgroup", b"0::/system.slice/broker.instance\n")):
                (output / f"{label}.{suffix}").write_bytes(data)
            return {"starttime_ticks": 5812, "pid": 741,
                    "cgroup": "0::/system.slice/other" if self.wrong_cgroup else
                              "0::/system.slice/broker.instance",
                    "exe": BROKER_EXECUTABLE, "exe_sha256": "f" * 64}

    process_reader = object.__new__(BrokerObserver)
    process_reader.writer = SignalWriter()
    process_reader.deadline = time.monotonic() + 5.0
    process_reader._read_stable_process_exe = lambda *_args: BROKER_EXECUTABLE
    process_reader._capture_fd0 = lambda *_args: {
        "identity": {"fd": 0, "inode": 123, "target": "socket:[123]",
                     "stable": True, "start_ticks_before": 5812,
                     "start_ticks_after": 5812},
        "link_ref": {"bytes": 1}, "fdinfo_ref": {"bytes": 1}}
    probe = ProcessProbe()
    saved_probe = sys.modules.get("issue1162_live_probe")
    try:
        sys.modules["issue1162_live_probe"] = probe
        stable = {"cgroup": "/system.slice/broker.instance", "proc_capture": {}}
        process_reader._capture_process(signal_row, stable, 741, 5,
                                        label="signal-000005", bind_lifecycle=False)
        assert set(stable["proc_capture"]) == {
            "proc-stat-before", "proc-stat-after", "proc-status", "proc-mountinfo",
            "proc-cgroup", "fd0_link", "fd0_info"}
        assert stable["start_ticks"] == 5812 and signal_row.start_ticks is None
        checks += 1
        probe.stat_reads, probe.reuse = 0, True
        reused = {"cgroup": "/system.slice/broker.instance", "proc_capture": {}}
        try:
            process_reader._capture_process(signal_row, reused, 741, 6,
                                            label="signal-000006", bind_lifecycle=False)
        except ObserverError:
            assert "proc-stat-before" in reused["proc_capture"] and \
                   "proc-stat-after" not in reused["proc_capture"]
            checks += 1
        else:
            raise AssertionError("PID reuse after FD0 was accepted")
        probe.stat_reads, probe.reuse, probe.wrong_cgroup = 0, False, True
        wrong = {"cgroup": "/system.slice/broker.instance", "proc_capture": {}}
        try:
            process_reader._capture_process(signal_row, wrong, 741, 7,
                                            label="signal-000007", bind_lifecycle=False)
        except ObserverError:
            assert "proc-stat-before" in wrong["proc_capture"] and \
                   "proc-status" not in wrong["proc_capture"]
            checks += 1
        else:
            raise AssertionError("process cgroup changed before FD0")
    finally:
        if saved_probe is None:
            sys.modules.pop("issue1162_live_probe", None)
        else:
            sys.modules["issue1162_live_probe"] = saved_probe
    print(f"BROKER_OBSERVER_SELF_TEST checks={checks} failures=0 fixtures-only-no-live-bus")


def _run_fake_command(argv: list[str], env: dict[str, str], timeout: float,
                      output_limit: int) -> dict[str, object]:
    require(argv and os.path.isabs(argv[0]) and timeout > 0 and output_limit > 0,
            "invalid fake-command test configuration")
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, env=env, cwd="/", close_fds=True, start_new_session=True,
        shell=False, bufsize=0)
    assert process.stdout is not None and process.stderr is not None
    stdout_fd = process.stdout.fileno()
    stderr_fd = process.stderr.fileno()
    os.set_blocking(stdout_fd, False)
    os.set_blocking(stderr_fd, False)
    streams = {stdout_fd: bytearray(), stderr_fd: bytearray()}
    open_fds = set(streams)
    deadline = time.monotonic() + timeout
    timed_out = False
    limit_hit = False
    while open_fds or process.poll() is None:
        if time.monotonic() >= deadline and process.poll() is None:
            timed_out = True
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=1.0)
        readable, _, _ = select.select(list(open_fds), [], [], min(0.02, max(0.0, deadline - time.monotonic())))
        for descriptor in readable:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                open_fds.remove(descriptor)
                continue
            if len(streams[descriptor]) + len(chunk) > output_limit:
                limit_hit = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=1.0)
                chunk = chunk[:max(0, output_limit - len(streams[descriptor]))]
            streams[descriptor].extend(chunk)
        if timed_out or limit_hit:
            for descriptor in tuple(open_fds):
                open_fds.remove(descriptor)
            break
    result = {"exit": process.wait(), "stdout": bytes(streams[stdout_fd]),
            "stderr": bytes(streams[stderr_fd]), "timed_out": timed_out,
            "limit_hit": limit_hit,
            "reason": "timeout" if timed_out else "limit" if limit_hit else None}
    process.stdout.close()
    process.stderr.close()
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--budget-seconds", type=float, default=0.0)
    parser.add_argument("--output")
    parser.add_argument("--run-id")
    parser.add_argument("--run-attempt")
    args = parser.parse_args()
    if args.self_test:
        require(not args.run and args.output is None, "--self-test cannot be combined with --run/--output")
        _self_test()
        return 0
    require(args.run and args.output is not None, "--run and --output are required")
    writer, bus, boot_id, boot_id_raw, deadline = _prepare_observer(args)
    observer = BrokerObserver(bus, writer, boot_id, boot_id_raw, deadline,
                              args.run_id, int(args.run_attempt) if args.run_attempt else None)
    try:
        summary = observer.run()
        writer.publish_summary(summary)
        print(json.dumps({"schema": SCHEMA, "kind": "SUMMARY", "process_complete": summary["process_complete"],
                          "population_complete": False, "units": len(summary["units"]),
                          "event_loss_detected": summary["event_loss_detected"],
                          "global_incomplete_reasons": summary["global_incomplete_reasons"]},
                         sort_keys=True), flush=True)
        return 0 if summary["process_complete"] else 1
    except (ObserverError, OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        failure = {"schema": SCHEMA, "kind": "SUMMARY", "boot_id": boot_id,
                   "boot_id_raw": boot_id_raw, "run_id": args.run_id,
                   "run_attempt": int(args.run_attempt) if args.run_attempt else None,
                   "observer_pid": observer.observer_pid,
                   "observer_start_ticks": observer.observer_start_ticks,
                   "manager_sender": bus.manager_sender,
                   "ready_monotonic_ns": observer.ready_ns,
                   "ended_monotonic_ns": time.monotonic_ns(), "stop_seen": False,
                   "observer_complete": False, "manager_observation_complete": False,
                   "process_complete": False, "event_loss_detected": True,
                   "event_stream_complete": False, "population_complete": False,
                   "global_incomplete_reasons": [f"observer_exception:{type(exc).__name__}:{str(exc)[:400]}"]}
        try:
            writer.publish_summary(failure)
        except (ObserverError, OSError):
            pass
        print(f"BROKER_OBSERVER_INCOMPLETE {failure['global_incomplete_reasons'][0]}", file=sys.stderr)
        return 1
    finally:
        bus.close()
        writer.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ObserverError as exc:
        print(f"BROKER_OBSERVER_INCOMPLETE {exc}", file=sys.stderr)
        raise SystemExit(1)
