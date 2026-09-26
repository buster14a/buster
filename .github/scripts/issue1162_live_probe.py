#!/usr/bin/env python3
"""Bounded, disposable #1162 live probe coordinator.

This runs only inside the hosted workflow's disposable systemd container. It
does not modify the service or queue. The one temporary payload is created
through the verified result-directory fd and removed by captured inode.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import io
import json
import os
import pwd
import re
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

STATE = "/var/lib/buster-bench"
QUEUE = STATE + "/queue"
WORKSPACES = STATE + "/workspaces"
RESULTS = STATE + "/workspaces/results"
LEASE_PARENT = STATE + "/lease"
LEASE_RECEIPT_DIR = "/etc/buster-bench"
LEASE_RECEIPT_NAME = "systemd-broker-lease.identity"
CGROUP_ROOT = "/sys/fs/cgroup"
BENCH_SLICE_PATH = CGROUP_ROOT + "/buster.slice/buster-bench.slice"
SERVICE_CGROUP = "/system.slice/buster-bench.service"
LIVE_TEST = "/usr/local/libexec/systemd-broker-live-test"
ROOT_UID = 0
ROOT_GID = 0
TERMINAL_STAGE_SUFFIXES = ("base-generate", "base-build", "candidate-generate",
                           "candidate-build", "throughput")
TERMINAL_SYSTEMD_PROPERTIES = (
    "Id,LoadState,ActiveState,MainPID,InvocationID,CollectMode,Result,ControlGroup")
MAX_TERMINAL_SYSTEMD_OUTPUT = 16384
MAX_ACTIVE_LEASE_FDS = 256
MAX_ACTIVE_LEASE_MATCHES = 8
MAX_ACTIVE_LEASE_FDINFO = 16384
MAX_ACTIVE_LEASE_LOCKS = 4 * 1024 * 1024
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
HEX32 = re.compile(r"[0-9a-f]{32}\Z")
HEX40 = re.compile(r"[0-9a-f]{40}\Z")


class ProbeError(RuntimeError):
    pass


def _fail(condition: bool, message: str) -> None:
    if not condition:
        raise ProbeError(message)


def _mode(info: os.stat_result) -> int:
    return stat.S_IMODE(info.st_mode)


def is_root() -> bool:
    return os.geteuid() == 0


def in_disposable_container() -> bool:
    return Path("/.dockerenv").exists() or Path("/run/.containerenv").exists()


def _read_at(directory: int, name: str, size: int, uid: int, gid: int, mode: int) -> bytes:
    fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    try:
        before = os.fstat(fd)
        _fail(stat.S_ISREG(before.st_mode) and before.st_uid == uid and before.st_gid == gid and
              _mode(before) == mode and before.st_nlink == 1 and before.st_size == size,
              f"invalid record metadata: {name}")
        stable_before = (before.st_dev, before.st_ino, before.st_mode, before.st_nlink,
                         before.st_uid, before.st_gid, before.st_size,
                         before.st_mtime_ns, before.st_ctime_ns)
        chunks = bytearray()
        while len(chunks) < size:
            block = os.read(fd, size - len(chunks))
            _fail(bool(block), f"short record: {name}")
            chunks.extend(block)
        after = os.fstat(fd)
        path_after = os.stat(name, dir_fd=directory, follow_symlinks=False)
        stable_after = (after.st_dev, after.st_ino, after.st_mode, after.st_nlink,
                        after.st_uid, after.st_gid, after.st_size,
                        after.st_mtime_ns, after.st_ctime_ns)
        stable_path = (path_after.st_dev, path_after.st_ino, path_after.st_mode, path_after.st_nlink,
                       path_after.st_uid, path_after.st_gid, path_after.st_size,
                       path_after.st_mtime_ns, path_after.st_ctime_ns)
        _fail(os.read(fd, 1) == b"" and stable_after == stable_before and stable_path == stable_after,
              f"record changed while read: {name}")
        return bytes(chunks)
    finally:
        os.close(fd)


def _read_variable_at(directory: int, name: str, maximum: int,
                      uid: int, gid: int, mode: int) -> bytes:
    return _read_variable_snapshot(directory, name, maximum, uid, gid, mode)[0]


def _read_variable_snapshot(directory: int, name: str, maximum: int,
                            uid: int, gid: int, mode: int) -> tuple[bytes, dict[str, int]]:
    fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    try:
        before = os.fstat(fd)
        _fail(stat.S_ISREG(before.st_mode) and before.st_uid == uid and before.st_gid == gid and
              _mode(before) == mode and before.st_nlink == 1 and 0 < before.st_size <= maximum,
              f"invalid file metadata: {name}")
        stable_before = (before.st_dev, before.st_ino, before.st_mode, before.st_nlink,
                         before.st_uid, before.st_gid, before.st_size,
                         before.st_mtime_ns, before.st_ctime_ns)
        chunks = bytearray()
        while len(chunks) < before.st_size:
            block = os.read(fd, before.st_size - len(chunks))
            _fail(bool(block), f"short file: {name}")
            chunks.extend(block)
        after = os.fstat(fd)
        path_after = os.stat(name, dir_fd=directory, follow_symlinks=False)
        stable_after = _stable_stat_tuple(after)
        stable_path = _stable_stat_tuple(path_after)
        _fail(os.read(fd, 1) == b"" and stable_after == stable_before and stable_path == stable_after,
              f"file changed while read: {name}")
        identity = {"device": after.st_dev, "inode": after.st_ino, "mode": _mode(after),
                    "uid": after.st_uid, "gid": after.st_gid, "links": after.st_nlink,
                    "size": after.st_size}
        return bytes(chunks), identity
    finally:
        os.close(fd)


def _read_bounded_file(path: str, maximum: int) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        before = os.fstat(fd)
        _fail(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and 0 <= before.st_size <= maximum,
              f"invalid bounded input file: {path}")
        data = bytearray()
        while len(data) <= maximum:
            block = os.read(fd, min(65536, maximum + 1 - len(data)))
            if not block:
                break
            data.extend(block)
        after = os.fstat(fd)
        _fail(len(data) <= maximum and before.st_dev == after.st_dev and before.st_ino == after.st_ino and
              before.st_mode == after.st_mode and before.st_nlink == after.st_nlink and
              before.st_uid == after.st_uid and before.st_gid == after.st_gid and
              before.st_size == after.st_size and before.st_mtime_ns == after.st_mtime_ns and
              before.st_ctime_ns == after.st_ctime_ns,
              f"input file changed or exceeded bound: {path}")
        return bytes(data)
    finally:
        os.close(fd)


def _text_field(raw: bytes, start: int, end: int, label: str, expected: str | None = None) -> str:
    field = raw[start:end]
    terminator = field.find(b"\0")
    _fail(terminator >= 0 and not any(field[terminator:]), f"invalid terminated field: {label}")
    try:
        value = field[:terminator].decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProbeError(f"non-ASCII {label}") from exc
    if expected is not None:
        _fail(value == expected, f"{label} mismatch")
    return value


def parse_records(worker: bytes, instance: bytes, job: int, request_sha: str,
                  boot_id: str) -> dict[str, object]:
    _fail(len(worker) == 232 and worker[:16] == b"BQWORKER00000001", "invalid worker record format")
    _fail(len(instance) == 544 and instance[:16] == b"BQINSTANCE000002", "invalid instance record format")
    expected_digest = request_sha.encode("ascii", "strict")
    _fail(len(expected_digest) == 64 and HEX64.fullmatch(request_sha) is not None,
          "invalid submit request digest")
    for label, raw in (("worker", worker), ("instance", instance)):
        _fail(int.from_bytes(raw[16:24], "little") == job, f"{label} job mismatch")
        _fail(int.from_bytes(raw[24:32], "little") > 0, f"{label} has zero attempt")
        _fail(raw[32:96] == expected_digest, f"{label} request digest mismatch")
        _fail(raw[96:132].decode("ascii") == boot_id and raw[132] == 0, f"{label} boot mismatch")
        _text_field(raw, 136, 232, f"{label} unit", f"buster-bench-{job}-{int.from_bytes(raw[24:32], 'little')}.service")
    token = int.from_bytes(worker[24:32], "little")
    _fail(int.from_bytes(instance[24:32], "little") == token, "worker and instance attempt mismatch")
    invocation = instance[232:264].decode("ascii")
    _fail(HEX32.fullmatch(invocation) is not None and instance[264] == 0, "invalid invocation id")
    cgroup = _text_field(instance, 288, 480, "cgroup")
    expected_unit = f"buster-bench-{job}-{token}.service"
    _fail(cgroup == f"/buster.slice/buster-bench.slice/{expected_unit}" and
          ".." not in cgroup.split("/"),
          "unexpected outer cgroup path")
    cgroup_ids = tuple(int.from_bytes(instance[start:start + 8], "little")
                       for start in (272, 280, 480, 488, 496, 504))
    _fail(all(cgroup_ids), "zero cgroup identity")
    return {"job": job, "attempt": token, "request_sha256": request_sha,
            "boot_id": boot_id, "outer_unit": f"buster-bench-{job}-{token}.service",
            "outer_invocation": invocation, "outer_cgroup": cgroup,
            "outer_cgroup_device": cgroup_ids[0], "outer_cgroup_inode": cgroup_ids[1],
            "cgroup_root_device": cgroup_ids[2], "cgroup_root_inode": cgroup_ids[3],
            "slice_device": cgroup_ids[4], "slice_inode": cgroup_ids[5],
            "worker_sha256": hashlib.sha256(worker).hexdigest(),
            "instance_sha256": hashlib.sha256(instance).hexdigest()}


def _safe_dir(path: str, uid: int, gid: int, mode: int) -> int:
    current = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in Path(path).parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=current)
            os.close(current)
            current = child
        info = os.fstat(current)
        _fail(stat.S_ISDIR(info.st_mode) and info.st_uid == uid and info.st_gid == gid and _mode(info) == mode,
              f"directory identity mismatch: {path}")
        result = current
        current = -1
        return result
    finally:
        if current >= 0:
            os.close(current)


def _write_private(output: Path, name: str, content: bytes) -> None:
    fd = os.open(output / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                 0o600)
    try:
        view = memoryview(content)
        while view:
            written = os.write(fd, view)
            _fail(written > 0, f"short evidence write: {name}")
            view = view[written:]
        os.fsync(fd)
    finally:
        os.close(fd)


RESULT_FIRST_LINE = re.compile(
    r"job=([0-9]+) token=([0-9]+) sequence=([0-9]+) phase=([a-z-]+) outcome=([a-z-]+) "
    r"validity=not-evaluated cancel-requested=([01]) reconciliation=([01]) pending=([0-9]{1,10}) "
    r"retained=([0-9]{1,10}) request-sha256=([0-9a-f]{64}) failure=([a-z0-9-]+)\Z")


def parse_result_receipt(raw: bytes, expected_job: int, request_sha: str) -> dict[str, object]:
    _fail(0 < len(raw) <= 4096 and raw.endswith(b"\n") and not raw.endswith(b"\n\n"),
          "result receipt is empty, oversized, or not a single newline-terminated record")
    _fail(b"\0" not in raw and b"\r" not in raw, "result receipt has invalid control bytes")
    try:
        lines = raw[:-1].decode("ascii").split("\n")
    except UnicodeDecodeError as exc:
        raise ProbeError("result receipt is not ASCII") from exc
    _fail(len(lines) == 6, "result receipt must contain exactly six lines")
    first = RESULT_FIRST_LINE.fullmatch(lines[0])
    _fail(first is not None, "result receipt first line is malformed or has duplicate/missing fields")
    fields = first.groups()
    job, token, sequence = int(fields[0]), int(fields[1]), int(fields[2])
    _fail(job == expected_job and job > 0, "result receipt job mismatch")
    _fail(token > 0, "result receipt token is zero")
    _fail(int(fields[7]) <= 0xFFFFFFFF and int(fields[8]) <= 0xFFFFFFFF,
          "result receipt pending/retained count exceeds the C formatter range")
    _fail(fields[3] == "finished" and fields[4] == "succeeded", "result receipt is not finished and succeeded")
    _fail(fields[5] == "0" and fields[6] == "0" and fields[10] == "ok",
          "successful result receipt has cancellation, reconciliation, or failure state")
    _fail(fields[9] == request_sha and HEX64.fullmatch(request_sha) is not None,
          "result receipt request digest mismatch")
    _fail(lines[1] == "result-bound=1 statistical-decision=not-evaluated",
          "result receipt is not bound")
    expected_root = f"{RESULTS}/job-{job}-attempt-{token}"
    _fail(lines[2] == f"result-root={expected_root}", "result receipt root is not the canonical attempt leaf")
    digest_values: dict[str, str] = {}
    for line, name in zip(lines[3:], ("manifest-sha256", "bundle-sha256", "full-result-sha256")):
        match = re.fullmatch(rf"{name}=([0-9a-f]{{64}})", line)
        _fail(match is not None, f"result receipt {name} is missing or malformed")
        digest_values[name] = match.group(1)
    return {"job": job, "token": token, "sequence": sequence, "request_sha256": request_sha,
            "result_root": expected_root, **digest_values}


def check_result_file(path: str, expected_job: int, request_sha: str) -> tuple[int, str]:
    raw = _read_bounded_file(path, 4096)
    receipt = parse_result_receipt(raw, expected_job, request_sha)
    return int(receipt["token"]), str(receipt["full-result-sha256"])


def _systemd(unit: str, timeout: float) -> tuple[dict[str, str], str]:
    try:
        done = subprocess.run(["/usr/bin/systemctl", "show", "--all", "--no-pager", unit],
                              check=False, capture_output=True, text=True, timeout=max(0.2, timeout))
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ProbeError(f"systemctl show failed for {unit}: {exc}") from exc
    _fail(done.returncode == 0, f"systemctl show exit {done.returncode} for {unit}: {done.stderr.strip()}")
    props: dict[str, str] = {}
    for line in done.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            props[key] = value
    return props, done.stdout


def _remaining(deadline: float, reserve: float = 0.0) -> float:
    value = deadline - time.monotonic() - reserve
    _fail(value > 0, "submit-relative live-probe deadline exhausted")
    return value


def _bounded_file(path: Path, limit: int, deadline: float, reserve: float = 0.0) -> bytes:
    _remaining(deadline, reserve)
    with path.open("rb") as source:
        data = source.read(limit + 1)
    _fail(len(data) <= limit, f"capture exceeds {limit} bytes: {path}")
    _remaining(deadline, reserve)
    return data


def _proc_stat_identity(raw: bytes, name: str) -> tuple[str, int, int]:
    try:
        line = raw.decode("ascii")
        tail = line[line.rfind(")") + 2:].split()
        _fail(len(tail) > 19, f"truncated proc stat for {name}")
        return tail[0], int(tail[2]), int(tail[19])
    except (UnicodeDecodeError, ValueError) as exc:
        raise ProbeError(f"invalid proc stat for {name}") from exc


def _expected_executable(unit: str) -> str | None:
    if unit == "buster-bench.service":
        return "/usr/local/libexec/buster-bench-service"
    # The outer service entry adopts the lease, then execs the fixed recipe
    # driver before any build-stage checkpoint can be observed.
    if re.fullmatch(r"buster-bench-[0-9]+-[0-9]+\.service", unit):
        return "/usr/local/libexec/buster-bench-build"
    if re.fullmatch(r"buster-bench-[0-9]+-[0-9]+-(base-generate|base-build|candidate-generate|candidate-build)\.service", unit):
        return "/usr/local/libexec/buster-bench-build"
    if re.fullmatch(r"buster-bench-[0-9]+-[0-9]+-throughput\.service", unit):
        return "/usr/local/libexec/buster-bench-throughput"
    if unit.startswith("buster-bench-systemd-broker@") and unit.endswith(".service"):
        return "/usr/local/libexec/buster-bench-systemd-broker"
    return None


def _hash_path(path: str, deadline: float, reserve: float = 0.0) -> str:
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        _fail(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_gid == 0 and
              _mode(info) == 0o755 and info.st_nlink == 1 and
              info.st_size <= 128 * 1024 * 1024,
              f"invalid executable file: {path}")
        digest = hashlib.sha256()
        while block := os.read(fd, 1024 * 1024):
            _remaining(deadline, reserve)
            digest.update(block)
        return digest.hexdigest()
    finally:
        os.close(fd)


def _capture_process(pid: int, name: str, output: Path, deadline: float,
                     expected_cgroup: str, expected_executable: str,
                     reserve: float = 0.0) -> dict[str, object]:
    proc = Path(f"/proc/{pid}")
    stat_before = _proc_stat_identity(_bounded_file(proc / "stat", 1024 * 1024, deadline, reserve), name)
    status = _bounded_file(proc / "status", 1024 * 1024, deadline, reserve)
    mountinfo = _bounded_file(proc / "mountinfo", 16 * 1024 * 1024, deadline, reserve)
    cgroup = _bounded_file(proc / "cgroup", 1024 * 1024, deadline, reserve)
    status_pid = next((line.split(":", 1)[1].strip() for line in status.decode("ascii").splitlines()
                       if line.startswith("Pid:")), "")
    _fail(status_pid == str(pid), f"proc status PID changed for {name}")
    cgroup_paths = []
    for line in cgroup.decode("ascii").splitlines():
        fields = line.split(":", 2)
        if len(fields) == 3 and fields[0] == "0" and fields[1] == "":
            cgroup_paths.append(fields[2])
    _fail(cgroup_paths == [expected_cgroup], f"process cgroup mismatch for {name}")
    exe_link = os.readlink(proc / "exe")
    _fail(exe_link == expected_executable, f"process executable path mismatch for {name}: {exe_link}")
    exe_fd = os.open(proc / "exe", os.O_RDONLY | os.O_CLOEXEC)
    try:
        exe_info = os.fstat(exe_fd)
        _fail(exe_info.st_size <= 128 * 1024 * 1024, "process executable exceeds capture bound")
        digest = hashlib.sha256()
        while block := os.read(exe_fd, 1024 * 1024):
            _remaining(deadline, reserve)
            digest.update(block)
    finally:
        os.close(exe_fd)
    stat_after = _proc_stat_identity(_bounded_file(proc / "stat", 1024 * 1024, deadline, reserve), name)
    _fail(stat_before == stat_after, f"PID or process identity changed during capture for {name}")
    disk_sha = _hash_path(expected_executable, deadline, reserve)
    _fail(digest.hexdigest() == disk_sha, f"running and installed executable hashes differ for {name}")
    stem = re.sub(r"[^a-zA-Z0-9_.-]", "_", name)
    (output / f"{stem}.proc-status").write_bytes(status)
    (output / f"{stem}.proc-mountinfo").write_bytes(mountinfo)
    (output / f"{stem}.proc-cgroup").write_bytes(cgroup)
    return {"pid": pid, "state": stat_after[0], "process_group": stat_after[1],
            "starttime_ticks": stat_after[2], "exe": exe_link, "expected_exe": expected_executable,
            "exe_sha256": digest.hexdigest(), "status_bytes": len(status),
            "exe_hash_matches_installed": True, "mountinfo_bytes": len(mountinfo),
            "cgroup": cgroup.decode("utf-8", "replace").strip(), "cgroup_matches_unit": True}


def capture_unit(unit: str, output: Path, label: str, timeout: float,
                 deadline: float | None = None, reserve: float = 0.2) -> dict[str, object]:
    if deadline is not None:
        timeout = min(timeout, _remaining(deadline, reserve=reserve))
    props, raw = _systemd(unit, timeout)
    (output / f"{label}.systemctl-show").write_text(raw)
    capture: dict[str, object] = {"unit": unit, "properties": props}
    pid_text = props.get("MainPID", "0")
    pid = int(pid_text) if pid_text.isdecimal() else 0
    if pid > 0:
        try:
            expected_executable = _expected_executable(unit)
            _fail(expected_executable is not None, f"no expected executable mapping for {unit}")
            expected_cgroup = props.get("ControlGroup", "")
            _fail(expected_cgroup.startswith("/") and ".." not in expected_cgroup.split("/"),
                  f"invalid systemd ControlGroup for {unit}")
            capture["process"] = _capture_process(pid, label, output,
                deadline or (time.monotonic() + timeout), expected_cgroup, expected_executable, reserve)
        except OSError as exc:
            capture["process_capture"] = f"unavailable: {exc}"
    else:
        capture["process_capture"] = "unavailable: MainPID is zero"
    cgroup = props.get("ControlGroup", "")
    if cgroup:
        path = Path(CGROUP_ROOT) / cgroup.lstrip("/")
        try:
            info = path.stat(follow_symlinks=False)
            _fail(stat.S_ISDIR(info.st_mode), f"systemd ControlGroup is not a directory: {path}")
            members = _bounded_file(path / "cgroup.procs", 1024 * 1024,
                                    deadline or (time.monotonic() + timeout), reserve).decode("ascii").splitlines()
            events = _bounded_file(path / "cgroup.events", 1024 * 1024,
                                   deadline or (time.monotonic() + timeout), reserve).decode("ascii")
            populated = next((line.split()[1] for line in events.splitlines()
                              if line.startswith("populated ") and len(line.split()) == 2), "")
            capture["unit_cgroup"] = {"path": str(path), "device": info.st_dev,
                                      "inode": info.st_ino, "mode": _mode(info),
                                      "main_pid_member": pid > 0 and str(pid) in members,
                                      "populated": populated}
        except (OSError, ProbeError) as exc:
            capture["unit_cgroup"] = {"path": str(path), "unavailable": str(exc)}
    for label_name, path in (("cgroup_root", CGROUP_ROOT), ("bench_slice", BENCH_SLICE_PATH)):
        try:
            info = Path(path).stat(follow_symlinks=False)
            capture[label_name] = {"path": path, "device": info.st_dev, "inode": info.st_ino,
                                   "mode": _mode(info)}
        except OSError as exc:
            capture[label_name] = {"path": path, "unavailable": str(exc)}
    (output / f"{label}.capture.json").write_text(json.dumps(capture, sort_keys=True, indent=2) + "\n")
    return capture


def validate_outer(snapshot: dict[str, object], identity: dict[str, object]) -> None:
    props = snapshot["properties"]
    _fail(props.get("ActiveState") == "active" and props.get("MainPID", "0").isdecimal() and
          int(props["MainPID"]) > 0, "outer service is not active")
    _fail(props.get("InvocationID") == identity["outer_invocation"], "outer invocation mismatch")
    _fail(props.get("ControlGroup") == identity["outer_cgroup"], "outer cgroup path mismatch")
    cg = snapshot.get("unit_cgroup", {})
    _fail(cg.get("device") == identity["outer_cgroup_device"] and
          cg.get("inode") == identity["outer_cgroup_inode"] and cg.get("main_pid_member") is True and
          cg.get("populated") == "1", "outer cgroup inode or process membership mismatch")
    root_info = Path(CGROUP_ROOT).stat(follow_symlinks=False)
    slice_info = Path(BENCH_SLICE_PATH).stat(follow_symlinks=False)
    _fail((root_info.st_dev, root_info.st_ino) ==
          (identity["cgroup_root_device"], identity["cgroup_root_inode"]), "cgroup root identity mismatch")
    _fail((slice_info.st_dev, slice_info.st_ino) ==
          (identity["slice_device"], identity["slice_inode"]), "service slice identity mismatch")
    proc = snapshot.get("process", {})
    _fail(proc.get("pid") == int(props["MainPID"]) and
          proc.get("cgroup_matches_unit") is True and
          proc.get("exe_hash_matches_installed") is True, "outer proc identity or executable capture missing")


def validate_service(snapshot: dict[str, object]) -> None:
    props = snapshot["properties"]
    _fail(props.get("ActiveState") == "active" and props.get("MainPID", "0").isdecimal() and
          int(props["MainPID"]) > 0 and props.get("ControlGroup") == SERVICE_CGROUP,
          "installed service is not active in its expected system cgroup")
    cg = snapshot.get("unit_cgroup", {})
    _fail(isinstance(cg.get("device"), int) and isinstance(cg.get("inode"), int) and
          cg.get("main_pid_member") is True and cg.get("populated") == "1",
          "installed service cgroup identity or membership is unavailable")
    proc = snapshot.get("process", {})
    _fail(proc.get("pid") == int(props["MainPID"]) and
          proc.get("cgroup") == f"0::{SERVICE_CGROUP}" and
          proc.get("cgroup_matches_unit") is True and
          proc.get("exe_hash_matches_installed") is True,
          "installed service process or executable identity is incomplete")


def outer_capture_identity(snapshot: dict[str, object]) -> dict[str, object]:
    props = snapshot["properties"]
    proc = snapshot.get("process", {})
    cg = snapshot.get("unit_cgroup", {})
    return {"main_pid": int(props["MainPID"]), "invocation": props["InvocationID"],
            "cgroup": props["ControlGroup"], "process_starttime_ticks": proc["starttime_ticks"],
            "device": cg["device"], "inode": cg["inode"]}


def validate_build(snapshot: dict[str, object], expected_unit: str) -> dict[str, object]:
    props = snapshot["properties"]
    _fail(props.get("ActiveState") == "active", "base-build stage is not active")
    pid_text = props.get("MainPID", "0")
    _fail(pid_text.isdecimal() and int(pid_text) > 0, "base-build MainPID is not live")
    invocation = props.get("InvocationID", "")
    _fail(HEX32.fullmatch(invocation) is not None and invocation != "0" * 32,
          "base-build invocation id is invalid")
    cg = snapshot.get("unit_cgroup", {})
    _fail(isinstance(cg.get("device"), int) and isinstance(cg.get("inode"), int) and
          cg.get("main_pid_member") is True and cg.get("populated") == "1",
          "base-build cgroup identity unavailable")
    proc = snapshot.get("process", {})
    expected_cgroup = f"/buster.slice/buster-bench.slice/{expected_unit}"
    _fail(props.get("ControlGroup") == expected_cgroup and proc.get("cgroup") == f"0::{expected_cgroup}",
          "base-build ControlGroup is outside the expected service slice")
    _fail(proc.get("pid") == int(pid_text) and proc.get("cgroup_matches_unit") is True and
          proc.get("exe_hash_matches_installed") is True,
          "base-build proc identity, cgroup membership, or executable capture missing")
    return {"invocation": invocation, "main_pid": int(pid_text),
            "cgroup": props.get("ControlGroup", ""), "device": cg["device"], "inode": cg["inode"],
            "process_starttime_ticks": proc["starttime_ticks"]}


def same_build(before: dict[str, object], after: dict[str, object]) -> bool:
    return before == after


def _group_has_live_process(pgid: int, deadline: float) -> bool:
    scanned = 0
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        _remaining(deadline)
        scanned += 1
        _fail(scanned <= 10000, "process inventory exceeded bound")
        try:
            raw = (entry / "stat").read_text()
            tail = raw[raw.rfind(")") + 2:].split()
            if len(tail) > 2 and int(tail[2]) == pgid and tail[0] not in ("Z", "X"):
                return True
        except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError):
            continue
    return False


def _signal_probe_group(pgid: int, sig: int) -> None:
    try:
        os.killpg(pgid, sig)
    except ProcessLookupError:
        pass


def _terminate_owned_group(process: subprocess.Popen, grace: float = 0.5) -> tuple[str, str, bool]:
    """Terminate and reap only the private session created for the live probe."""
    _signal_probe_group(process.pid, signal.SIGTERM)
    stdout: str | bytes = ""
    stderr: str | bytes = ""
    try:
        stdout, stderr = process.communicate(timeout=grace)
    except subprocess.TimeoutExpired as exc:
        stdout, stderr = exc.output or "", exc.stderr or ""
    except Exception as exc:
        stderr = f"owned-group initial wait failed: {exc}\n"
    try:
        live = _group_has_live_process(process.pid, time.monotonic() + 0.75)
    except Exception:
        live = True
    if process.poll() is None:
        live = True
    if live:
        _signal_probe_group(process.pid, signal.SIGKILL)
    try:
        more_out, more_err = process.communicate(timeout=1.0)
        stdout, stderr = more_out or stdout, more_err or stderr
    except subprocess.TimeoutExpired as exc:
        stdout, stderr = exc.output or stdout, exc.stderr or stderr
        _signal_probe_group(process.pid, signal.SIGKILL)
    except Exception as exc:
        stderr = f"{stderr}\nowned-group reap failed: {exc}\n"
        _signal_probe_group(process.pid, signal.SIGKILL)
    try:
        clean = not _group_has_live_process(process.pid, time.monotonic() + 1.0)
    except Exception:
        clean = False
    if isinstance(stdout, bytes):
        stdout = stdout.decode("utf-8", "replace")
    if isinstance(stderr, bytes):
        stderr = stderr.decode("utf-8", "replace")
    return stdout, stderr, clean


def run_probe_process(command: list[str], env: dict[str, str], timeout: float,
                      deadline: float) -> dict[str, object]:
    process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True)
    timed_out = False
    leaked_children = False
    try:
        try:
            stdout, stderr = process.communicate(timeout=min(timeout, _remaining(deadline, reserve=6.0)))
        except subprocess.TimeoutExpired:
            timed_out = True
            stdout, stderr, group_clean = _terminate_owned_group(process)
        else:
            active_group = _group_has_live_process(process.pid, deadline)
            leaked_children = active_group
            if active_group:
                stdout, stderr, group_clean = _terminate_owned_group(process)
            else:
                group_clean = True
    except BaseException as exc:
        stdout, stderr, group_clean = _terminate_owned_group(process)
        if not isinstance(exc, Exception):
            raise
        stderr = f"{stderr}\nprobe runner exception: {exc}\n"
        leaked_children = True
        process_error = str(exc)
    else:
        process_error = None
    if isinstance(stdout, bytes):
        stdout = stdout.decode("utf-8", "replace")
    if isinstance(stderr, bytes):
        stderr = stderr.decode("utf-8", "replace")
    return {"exit": 124 if timed_out else process.returncode, "timed_out": timed_out,
            "stdout": stdout, "stderr": stderr, "group_clean": group_clean,
            "leaked_children": leaked_children, "runner_error": process_error}


def _open_records(job: int, request_sha: str, output: Path | None = None,
                  record_prefix: str = "") -> tuple[bytes, bytes, dict[str, object]]:
    service = pwd.getpwnam("buster-bench")
    queue_fd = _safe_dir(QUEUE, service.pw_uid, service.pw_gid, 0o710)
    try:
        worker = _read_at(queue_fd, f"worker-{job}", 232, service.pw_uid, service.pw_gid, 0o440)
        instance = _read_at(queue_fd, f"worker-instance-{job}", 544, service.pw_uid, service.pw_gid, 0o440)
    finally:
        os.close(queue_fd)
    if output is not None:
        _write_private(output, f"{record_prefix}worker-record.bin", worker)
        _write_private(output, f"{record_prefix}instance-record.bin", instance)
    boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    return worker, instance, parse_records(worker, instance, job, request_sha, boot_id)


def _open_dir_nofollow(path: str) -> int:
    _fail(path.startswith("/"), f"directory path is not absolute: {path}")
    current = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in Path(path).parts[1:]:
            _fail(part not in ("", ".", ".."), f"unsafe directory component in {path}")
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=current)
            os.close(current)
            current = child
        result = current
        current = -1
        return result
    finally:
        if current >= 0:
            os.close(current)


def _write_json_private(output: Path, name: str, value: object) -> None:
    _write_private(output, name, (json.dumps(value, sort_keys=True, indent=2) + "\n").encode("utf-8"))


def _cleanup_identity(raw: bytes, job: int, attempt: int, request_sha: str,
                      workspaces_device: int, workspaces_inode: int) -> dict[str, int]:
    _fail(len(raw) == 128 and raw[:16] == b"BQCLEANUP0000001", "invalid cleanup record format")
    _fail(int.from_bytes(raw[16:24], "little") == job and
          int.from_bytes(raw[24:32], "little") == attempt, "cleanup record job/attempt mismatch")
    _fail(int.from_bytes(raw[32:40], "little") == workspaces_device and
          int.from_bytes(raw[40:48], "little") == workspaces_inode,
          "cleanup record workspace-parent identity mismatch")
    workspace_device = int.from_bytes(raw[48:56], "little")
    workspace_inode = int.from_bytes(raw[56:64], "little")
    _fail(workspace_device > 0 and workspace_inode > 0, "cleanup record has zero attempt-workspace identity")
    _fail(raw[64:128] == request_sha.encode("ascii"), "cleanup record request digest mismatch")
    return {"workspaces_device": workspaces_device, "workspaces_inode": workspaces_inode,
            "attempt_workspace_device": workspace_device, "attempt_workspace_inode": workspace_inode}


def _canonical_lease_receipt(raw: bytes) -> tuple[int, int]:
    _fail(len(raw) <= 128, "lease identity receipt exceeds bound")
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProbeError("lease identity receipt is not ASCII") from exc
    match = re.fullmatch(r"device=([1-9][0-9]*)\ninode=([1-9][0-9]*)\n", text)
    _fail(match is not None, "lease identity receipt is malformed or non-canonical")
    device, inode = int(match.group(1)), int(match.group(2))
    _fail(device <= 0xFFFFFFFFFFFFFFFF and inode <= 0xFFFFFFFFFFFFFFFF,
          "lease identity receipt value exceeds u64")
    return device, inode


def _stable_stat_tuple(info: os.stat_result) -> tuple[int, ...]:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink,
            info.st_uid, info.st_gid, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def _lease_snapshot(lease_dir_fd: int, lock_fd: int, service_uid: int, service_gid: int,
                    expected_device: int, expected_inode: int) -> dict[str, int]:
    info = os.fstat(lock_fd)
    _fail(stat.S_ISREG(info.st_mode) and info.st_uid == service_uid and info.st_gid == service_gid and
          _mode(info) == 0o640 and info.st_nlink == 1 and
          (info.st_dev, info.st_ino) == (expected_device, expected_inode),
          "stable lease fd identity or metadata mismatch")
    path_info = os.stat("host.lock", dir_fd=lease_dir_fd, follow_symlinks=False)
    _fail(stat.S_ISREG(path_info.st_mode) and _stable_stat_tuple(path_info) == _stable_stat_tuple(info),
          "stable lease path does not identify the opened inode")
    return {"device": info.st_dev, "inode": info.st_ino, "mode": _mode(info),
            "uid": info.st_uid, "gid": info.st_gid, "links": info.st_nlink}


def _matching_lease_locks(raw: bytes, device: int, inode: int) -> list[dict[str, object]]:
    """Parse a bounded /proc/locks snapshot without attributing an OFD to a PID."""
    _fail(len(raw) <= MAX_ACTIVE_LEASE_LOCKS and (not raw or raw.endswith(b"\n")),
          "incomplete /proc/locks lease snapshot")
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ProbeError("non-ASCII /proc/locks lease snapshot") from exc
    matches: list[dict[str, object]] = []
    for line in lines:
        identity = re.search(r"(?<!\S)([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9]+)(?=\s|$)", line)
        if identity is None or \
           (int(identity.group(1), 16), int(identity.group(2), 16), int(identity.group(3))) != \
           (os.major(device), os.minor(device), inode):
            continue
        match = re.fullmatch(
            r"([0-9]+):\s+(->\s+)?(\S+)\s+(\S+)\s+(\S+)\s+(-?[0-9]+)\s+"
            r"([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9]+)\s+([0-9]+)\s+(EOF|[0-9]+)", line)
        _fail(match is not None, "unrecognized matching /proc/locks record")
        matches.append({"raw": line, "waiting": bool(match.group(2)), "type": match.group(3),
                        "class": match.group(4), "access": match.group(5),
                        "reported_pid": int(match.group(6)), "start": int(match.group(10)),
                        "end": match.group(11)})
        _fail(len(matches) <= MAX_ACTIVE_LEASE_MATCHES, "matching lease locks exceed bound")
    _fail(any(not row["waiting"] and row["type"] == "FLOCK" and
              row["class"] == "ADVISORY" and row["access"] == "WRITE" and
              row["start"] == 0 and row["end"] == "EOF" for row in matches),
          "exclusive advisory host lease absent from /proc/locks")
    return matches


def capture_active_lease(service_snapshot: dict[str, object], boot_id: str,
                         service_uid: int, service_gid: int, output: Path,
                         deadline: float) -> dict[str, object]:
    """One passive service-FD observation; inode equality cannot prove shared OFD."""
    props = service_snapshot["properties"]
    process = service_snapshot["process"]
    pid = int(props["MainPID"])
    invocation = props.get("InvocationID", "")
    start_ticks = process["starttime_ticks"]
    _fail(HEX32.fullmatch(invocation) is not None and invocation != "0" * 32 and
          isinstance(start_ticks, int) and start_ticks > 0,
          "active service invocation or process start ticks missing")
    proc = Path(f"/proc/{pid}")

    def process_identity(label: str) -> None:
        stat_raw = _bounded_file(proc / "stat", 1024 * 1024, deadline, reserve=65.0)
        _write_private(output, f"active-lease-service-stat-{label}.txt", stat_raw)
        state, group, ticks = _proc_stat_identity(stat_raw, "active lease service")
        _fail(stat_raw.startswith(f"{pid} (".encode("ascii")) and state not in ("Z", "X") and
              group == process["process_group"] and ticks == start_ticks,
              "active lease service PID or start ticks changed")
        cgroup_raw = _bounded_file(proc / "cgroup", 1024 * 1024, deadline, reserve=65.0)
        _write_private(output, f"active-lease-service-cgroup-{label}.txt", cgroup_raw)
        _fail(cgroup_raw.decode("ascii").splitlines() == [f"0::{SERVICE_CGROUP}"],
              "active lease service cgroup changed")
        boot_raw = _bounded_file(Path("/proc/sys/kernel/random/boot_id"), 128,
                                 deadline, reserve=65.0)
        _write_private(output, f"active-lease-boot-{label}.txt", boot_raw)
        _fail(boot_raw.decode("ascii").strip() == boot_id,
              "active lease boot differs from worker record")

    process_identity("before")
    receipt_fd = lease_dir_fd = lock_fd = -1
    try:
        receipt_fd = _safe_dir(LEASE_RECEIPT_DIR, ROOT_UID, ROOT_GID, 0o555)
        receipt_raw, receipt_info = _read_variable_snapshot(
            receipt_fd, LEASE_RECEIPT_NAME, 128, ROOT_UID, ROOT_GID, 0o444)
        _write_private(output, "active-lease-root-receipt.txt", receipt_raw)
        receipt_device, receipt_inode = _canonical_lease_receipt(receipt_raw)
        lease_dir_fd = _safe_dir(LEASE_PARENT, service_uid, service_gid, 0o710)
        # O_PATH pins only metadata. Never open the target lease for data or flock it.
        lock_fd = os.open("host.lock", os.O_PATH | os.O_CLOEXEC | os.O_NOFOLLOW,
                          dir_fd=lease_dir_fd)
        lock_info = _lease_snapshot(lease_dir_fd, lock_fd, service_uid, service_gid,
                                    receipt_device, receipt_inode)

        descriptors: list[dict[str, object]] = []
        scanned = 0
        with os.scandir(proc / "fd") as entries:
            for entry in entries:
                _remaining(deadline, reserve=65.0)
                if not entry.name.isdecimal():
                    continue
                scanned += 1
                _fail(scanned <= MAX_ACTIVE_LEASE_FDS, "active service descriptor count exceeds bound")
                fd_path = proc / "fd" / entry.name
                fd_info = os.stat(fd_path)
                if (fd_info.st_dev, fd_info.st_ino) != (receipt_device, receipt_inode):
                    continue
                _fail(len(descriptors) < MAX_ACTIVE_LEASE_MATCHES,
                      "matching active service descriptors exceed bound")
                link = os.readlink(fd_path)
                fdinfo_raw = _bounded_file(proc / "fdinfo" / entry.name,
                                           MAX_ACTIVE_LEASE_FDINFO, deadline, reserve=65.0)
                second_info = os.stat(fd_path)
                _fail(_stable_stat_tuple(fd_info) == _stable_stat_tuple(second_info) and
                      os.readlink(fd_path) == link,
                      "active service lease descriptor changed during read")
                fdinfo_text = fdinfo_raw.decode("ascii")
                reported_inodes = re.findall(r"^ino:\s*([0-9]+)\s*$", fdinfo_text, re.MULTILINE)
                _fail(len(reported_inodes) <= 1 and
                      (not reported_inodes or int(reported_inodes[0]) == receipt_inode),
                      "active service fdinfo inode mismatch")
                _write_private(output, f"active-lease-service-fd-{entry.name}.fdinfo", fdinfo_raw)
                descriptors.append({"fd_number": int(entry.name), "readlink": link,
                                    "device": fd_info.st_dev, "inode": fd_info.st_ino,
                                    "uid": fd_info.st_uid, "gid": fd_info.st_gid,
                                    "mode": _mode(fd_info), "links": fd_info.st_nlink,
                                    "fdinfo_bytes": len(fdinfo_raw)})
        _fail(bool(descriptors), "active service has no descriptor matching the fixed host lease")
        locks_raw = _bounded_file(Path("/proc/locks"), MAX_ACTIVE_LEASE_LOCKS,
                                  deadline, reserve=65.0)
        _write_private(output, "active-lease-proc-locks.txt", locks_raw)
        matching_locks = _matching_lease_locks(locks_raw, receipt_device, receipt_inode)

        _fail(_lease_snapshot(lease_dir_fd, lock_fd, service_uid, service_gid,
                              receipt_device, receipt_inode) == lock_info,
              "fixed host lease metadata changed during active capture")
        _same_directory_path(LEASE_PARENT, lease_dir_fd, service_uid, service_gid, 0o710)
        receipt_after, receipt_info_after = _read_variable_snapshot(
            receipt_fd, LEASE_RECEIPT_NAME, 128, ROOT_UID, ROOT_GID, 0o444)
        _fail(receipt_after == receipt_raw and receipt_info_after == receipt_info,
              "root lease receipt changed during active capture")
        _same_directory_path(LEASE_RECEIPT_DIR, receipt_fd, ROOT_UID, ROOT_GID, 0o555)
    finally:
        for descriptor in (lock_fd, lease_dir_fd, receipt_fd):
            if descriptor >= 0:
                os.close(descriptor)

    process_identity("after")
    after_props, after_raw = _systemd("buster-bench.service",
                                      min(4.0, _remaining(deadline, reserve=65.0)))
    _write_private(output, "active-lease-service-after.systemctl-show", after_raw.encode("utf-8"))
    _fail(after_props.get("ActiveState") == "active" and
          after_props.get("MainPID") == str(pid) and
          after_props.get("InvocationID") == invocation and
          after_props.get("ControlGroup") == SERVICE_CGROUP,
          "active service identity changed during lease observation")
    result = {"disposition": "PASSIVE_ACTIVE_LEASE_SNAPSHOT", "boot_id": boot_id,
              "service_main_pid": pid, "service_starttime_ticks": start_ticks,
              "service_invocation": invocation, "service_cgroup": SERVICE_CGROUP,
              "lease_path": f"{LEASE_PARENT}/host.lock", "lease": lock_info,
              "root_receipt": receipt_info, "descriptors_scanned": scanned,
              "matching_service_descriptors": descriptors, "matching_proc_locks": matching_locks,
              "limitation": "one bounded readback; matching inode and /proc/locks do not prove the "
                            "same open-file description or continuous coordinator ownership"}
    _write_json_private(output, "active-lease-snapshot.json", result)
    return result


def _same_directory_path(path: str, pinned_fd: int, uid: int, gid: int, mode: int) -> dict[str, int]:
    current_fd = _safe_dir(path, uid, gid, mode)
    try:
        pinned = os.fstat(pinned_fd)
        current = os.fstat(current_fd)
        _fail(stat.S_ISDIR(current.st_mode) and
              (current.st_dev, current.st_ino, current.st_mode, current.st_uid, current.st_gid) ==
              (pinned.st_dev, pinned.st_ino, pinned.st_mode, pinned.st_uid, pinned.st_gid),
              f"fixed directory path no longer names the pinned directory: {path}")
        return {"device": current.st_dev, "inode": current.st_ino,
                "mode": _mode(current), "uid": current.st_uid, "gid": current.st_gid}
    finally:
        os.close(current_fd)


def _terminal_directory_paths(workspaces_fd: int, results_fd: int, result_leaf_fd: int,
                              lease_dir_fd: int, receipt_dir_fd: int,
                              service_uid: int, service_gid: int, candidate_gid: int,
                              job: int, attempt: int, output: Path) -> dict[str, object]:
    paths = (
        ("workspaces", WORKSPACES, workspaces_fd, service_uid, candidate_gid, 0o2710),
        ("results", RESULTS, results_fd, service_uid, service_gid, 0o710),
        ("result_leaf", f"{RESULTS}/job-{job}-attempt-{attempt}", result_leaf_fd,
         service_uid, service_gid, 0o700),
        ("lease_parent", LEASE_PARENT, lease_dir_fd, service_uid, service_gid, 0o710),
        ("lease_receipt_parent", LEASE_RECEIPT_DIR, receipt_dir_fd, ROOT_UID, ROOT_GID, 0o555),
    )
    evidence: dict[str, object] = {}
    failures = []
    for label, path, pinned_fd, uid, gid, mode in paths:
        try:
            evidence[label] = _same_directory_path(path, pinned_fd, uid, gid, mode)
        except (ProbeError, OSError) as exc:
            evidence[label] = {"valid": False, "error": f"{type(exc).__name__}: {exc}"}
            failures.append(f"{label}: {exc}")
    _write_json_private(output, "terminal-directory-paths.json", evidence)
    _fail(not failures, "fixed directory path identity changed: " + "; ".join(failures))
    return evidence


def _mountinfo_has_cgroup2(raw: bytes, mountpoint: str) -> bool:
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ProbeError("mountinfo is not ASCII") from exc
    for line in lines:
        before, separator, after = line.partition(" - ")
        left, right = before.split(), after.split()
        if separator and len(left) >= 5 and right and right[0] == "cgroup2":
            decoded = re.sub(r"\\([0-7]{3})", lambda match: chr(int(match.group(1), 8)), left[4])
            if decoded == mountpoint:
                return True
    return False


def _check_terminal_unit(unit: str, output: Path, deadline: float,
                         identity: dict[str, object], outer: bool = False) -> dict[str, str]:
    stem = re.sub(r"[^A-Za-z0-9_.-]", "_", unit)
    timeout = min(3.0, _remaining(deadline, reserve=1.0))
    command = ["/usr/bin/systemctl", "show", "--all", "--no-pager",
               f"--property={TERMINAL_SYSTEMD_PROPERTIES}", unit]
    try:
        done = subprocess.run(command, check=False, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout if isinstance(exc.stdout, bytes) else (exc.stdout or "").encode()
        stderr = exc.stderr if isinstance(exc.stderr, bytes) else (exc.stderr or "").encode()
        _write_private(output, f"terminal-unit-{stem}.stdout", stdout)
        _write_private(output, f"terminal-unit-{stem}.stderr", stderr)
        _write_json_private(output, f"terminal-unit-{stem}.status.json",
                            {"unit": unit, "exit": None, "timed_out": True, "error": str(exc)})
        raise ProbeError(f"bounded systemd query timed out for {unit}") from exc
    except OSError as exc:
        _write_private(output, f"terminal-unit-{stem}.stdout", b"")
        _write_private(output, f"terminal-unit-{stem}.stderr", str(exc).encode("utf-8", "replace"))
        _write_json_private(output, f"terminal-unit-{stem}.status.json",
                            {"unit": unit, "exit": None, "timed_out": False, "error": str(exc)})
        raise ProbeError(f"bounded systemd query failed for {unit}: {exc}") from exc
    stdout = done.stdout.encode("utf-8", "replace") if isinstance(done.stdout, str) else done.stdout
    stderr = done.stderr.encode("utf-8", "replace") if isinstance(done.stderr, str) else done.stderr
    output_oversized = len(stdout) > MAX_TERMINAL_SYSTEMD_OUTPUT or len(stderr) > MAX_TERMINAL_SYSTEMD_OUTPUT
    _write_private(output, f"terminal-unit-{stem}.stdout", stdout[:MAX_TERMINAL_SYSTEMD_OUTPUT])
    _write_private(output, f"terminal-unit-{stem}.stderr", stderr[:MAX_TERMINAL_SYSTEMD_OUTPUT])
    _write_json_private(output, f"terminal-unit-{stem}.status.json",
                        {"unit": unit, "exit": done.returncode, "timed_out": False,
                         "output_oversized": output_oversized,
                         "stdout_bytes": len(stdout), "stderr_bytes": len(stderr)})
    _fail(not output_oversized, f"systemd query output exceeded bound for {unit}")
    _fail(done.returncode == 0, f"systemd query for {unit} returned {done.returncode}")
    try:
        text = stdout.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProbeError(f"systemd query for {unit} was not ASCII") from exc
    properties: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        _fail(separator and key and key not in properties, f"malformed or duplicate systemd property for {unit}")
        properties[key] = value
    _fail(properties.get("Id") == unit, f"systemd returned a different unit identity for {unit}")
    if outer:
        load_state = properties.get("LoadState")
        if load_state == "not-found":
            return properties
        _fail(load_state == "loaded" and properties.get("ActiveState") == "inactive" and
              properties.get("MainPID") == "0" and
              properties.get("InvocationID") == identity["outer_invocation"] and
              properties.get("CollectMode") == "inactive" and
              properties.get("Result") == "success" and
              properties.get("ControlGroup", "") in ("", identity["outer_cgroup"]),
              "outer unit is active, changed invocation, or not collected/inactive")
    else:
        _fail(properties.get("LoadState") == "not-found",
              f"stage unit is still loaded or not collected: {unit}")
    return properties


def _terminal_absence_snapshot(workspaces_fd: int, results_fd: int, label: str,
                               output: Path, job: int, attempt: int) -> dict[str, object]:
    checks = ((workspaces_fd, f"job-{job}-attempt-{attempt}", "attempt_workspace"),
              (results_fd, ".lease-handoff", "result_leaf_lease_handoff"),
              (results_fd, "payload", "result_leaf_payload"))
    evidence: dict[str, object] = {"snapshot": label, "checks": []}
    failures = []
    for directory_fd, name, label_name in checks:
        try:
            info = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            check = {"name": label_name, "path_component": name, "absent": False,
                     "mode": _mode(info), "device": info.st_dev, "inode": info.st_ino,
                     "symlink": stat.S_ISLNK(info.st_mode)}
            failures.append(f"{label_name} is present")
        except FileNotFoundError:
            check = {"name": label_name, "path_component": name, "absent": True}
        except OSError as exc:
            check = {"name": label_name, "path_component": name, "absent": False,
                     "error": f"{type(exc).__name__}: {exc}"}
            failures.append(f"{label_name} could not be checked")
        evidence["checks"].append(check)
    _write_json_private(output, f"terminal-paths-{label}.json", evidence)
    _fail(not failures, "; ".join(failures))
    return evidence


def _terminal_cgroup_snapshot(output: Path, identity: dict[str, object], label: str) -> dict[str, object]:
    root_fd = parent_fd = slice_fd = -1
    try:
        root_fd = _open_dir_nofollow(CGROUP_ROOT)
        root_info = os.fstat(root_fd)
        parent_fd = os.open("buster.slice", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=root_fd)
        slice_fd = os.open("buster-bench.slice", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                           dir_fd=parent_fd)
        slice_info = os.fstat(slice_fd)
        _write_json_private(output, f"terminal-cgroup-identities-{label}.json", {
            "root": CGROUP_ROOT, "root_device": root_info.st_dev, "root_inode": root_info.st_ino,
            "slice": BENCH_SLICE_PATH, "slice_device": slice_info.st_dev, "slice_inode": slice_info.st_ino,
            "expected_root_device": identity["cgroup_root_device"],
            "expected_root_inode": identity["cgroup_root_inode"],
            "expected_slice_device": identity["slice_device"],
            "expected_slice_inode": identity["slice_inode"],
        })
        _fail((root_info.st_dev, root_info.st_ino) ==
              (identity["cgroup_root_device"], identity["cgroup_root_inode"]),
              "cgroup root device/inode differs from immutable instance record")
        _fail((slice_info.st_dev, slice_info.st_ino) ==
              (identity["slice_device"], identity["slice_inode"]),
              "benchmark slice device/inode differs from immutable instance record")
        leaves = [identity["outer_unit"]] + [
            f"buster-bench-{identity['job']}-{identity['attempt']}-{suffix}.service"
            for suffix in TERMINAL_STAGE_SUFFIXES]
        checks: list[dict[str, object]] = []
        failures = []
        for unit in leaves:
            try:
                info = os.stat(unit, dir_fd=slice_fd, follow_symlinks=False)
                checks.append({"unit": unit, "absent": False, "device": info.st_dev,
                               "inode": info.st_ino, "mode": _mode(info),
                               "symlink": stat.S_ISLNK(info.st_mode)})
                failures.append(f"expected job cgroup leaf remains: {unit}")
            except FileNotFoundError:
                checks.append({"unit": unit, "absent": True})
            except OSError as exc:
                checks.append({"unit": unit, "absent": False,
                               "error": f"{type(exc).__name__}: {exc}"})
                failures.append(f"cannot prove cgroup leaf absence: {unit}")
        snapshot = {"snapshot": label, "filesystem": "cgroup2", "root": CGROUP_ROOT,
                    "root_device": root_info.st_dev, "root_inode": root_info.st_ino,
                    "slice": BENCH_SLICE_PATH, "slice_device": slice_info.st_dev,
                    "slice_inode": slice_info.st_ino, "units": checks,
                    "recursive_absence_basis": "all six exact unit cgroup leaves are absent on the recorded cgroup2 slice"}
        _write_json_private(output, f"terminal-cgroups-{label}.json", snapshot)
        _fail(not failures, "; ".join(failures))
        return snapshot
    finally:
        for descriptor in (slice_fd, parent_fd, root_fd):
            if descriptor >= 0:
                os.close(descriptor)


def _terminal_proof(job: int, attempt: int, request_sha: str, baseline: str, subject: str,
                    lease_device: int, lease_inode: int, budget: float, output: Path) -> dict[str, object]:
    _fail(is_root() and in_disposable_container(), "not root in the disposable container")
    _fail(job > 0 and attempt > 0 and HEX64.fullmatch(request_sha) is not None,
          "invalid terminal job, attempt, or request digest")
    _fail(HEX40.fullmatch(baseline) is not None and HEX40.fullmatch(subject) is not None,
          "terminal source revisions are malformed")
    _fail(lease_device > 0 and lease_inode > 0, "pre-submit lease identity is missing")
    _fail(0 < budget <= 60.0, "terminal proof budget must be within 60 seconds")
    deadline = time.monotonic() + budget
    service = pwd.getpwnam("buster-bench")
    candidate = pwd.getpwnam("buster-bench-candidate")

    worker, instance, identity = _open_records(job, request_sha, output, "terminal-")
    _fail(identity["attempt"] == attempt, "terminal attempt differs from durable worker record")
    _write_json_private(output, "terminal-record-identities.json", {
        "job": job, "attempt": attempt, "request_sha256": request_sha,
        "boot_id": identity["boot_id"], "outer_unit": identity["outer_unit"],
        "outer_invocation": identity["outer_invocation"], "outer_cgroup": identity["outer_cgroup"],
        "worker_record_bytes": len(worker), "worker_record_sha256": identity["worker_sha256"],
        "instance_record_bytes": len(instance), "instance_record_sha256": identity["instance_sha256"],
    })

    queue_fd = _safe_dir(QUEUE, service.pw_uid, service.pw_gid, 0o710)
    try:
        cleanup_raw = _read_at(queue_fd, f"cleanup-{job}", 128, service.pw_uid, service.pw_gid, 0o400)
    finally:
        os.close(queue_fd)
    _write_private(output, "terminal-cleanup-record.bin", cleanup_raw)

    workspaces_fd = results_fd = result_leaf_fd = lease_dir_fd = lease_lock_fd = receipt_dir_fd = -1
    cgroup_snapshots: list[dict[str, object]] = []
    try:
        workspaces_fd = _safe_dir(WORKSPACES, service.pw_uid, candidate.pw_gid, 0o2710)
        workspaces_info = os.fstat(workspaces_fd)
        _write_json_private(output, "terminal-workspaces-parent.json", {
            "path": WORKSPACES, "device": workspaces_info.st_dev, "inode": workspaces_info.st_ino,
            "uid": workspaces_info.st_uid, "gid": workspaces_info.st_gid, "mode": _mode(workspaces_info),
            "cleanup_record_parent_device": int.from_bytes(cleanup_raw[32:40], "little"),
            "cleanup_record_parent_inode": int.from_bytes(cleanup_raw[40:48], "little"),
        })
        cleanup_identity = _cleanup_identity(cleanup_raw, job, attempt, request_sha,
                                              workspaces_info.st_dev, workspaces_info.st_ino)
        _write_json_private(output, "terminal-cleanup-identity.json", cleanup_identity)

        results_fd = _safe_dir(RESULTS, service.pw_uid, service.pw_gid, 0o710)
        result_leaf_name = f"job-{job}-attempt-{attempt}"
        result_leaf_fd = os.open(result_leaf_name,
                                 os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                                 dir_fd=results_fd)
        result_leaf_info = os.fstat(result_leaf_fd)
        _write_json_private(output, "terminal-result-leaf.json", {
            "path": f"{RESULTS}/{result_leaf_name}", "device": result_leaf_info.st_dev,
            "inode": result_leaf_info.st_ino, "uid": result_leaf_info.st_uid,
            "gid": result_leaf_info.st_gid, "mode": _mode(result_leaf_info),
        })
        _fail(stat.S_ISDIR(result_leaf_info.st_mode) and result_leaf_info.st_uid == service.pw_uid and
              result_leaf_info.st_gid == service.pw_gid and _mode(result_leaf_info) == 0o700,
              "terminal result leaf identity mismatch")
        _terminal_absence_snapshot(workspaces_fd, result_leaf_fd, "before", output, job, attempt)

        receipt_dir_fd = _safe_dir(LEASE_RECEIPT_DIR, ROOT_UID, ROOT_GID, 0o555)
        receipt_raw, receipt_identity = _read_variable_snapshot(
            receipt_dir_fd, LEASE_RECEIPT_NAME, 128, ROOT_UID, ROOT_GID, 0o444)
        _write_private(output, "terminal-lease-receipt.txt", receipt_raw)
        _write_json_private(output, "terminal-lease-receipt-identity.json", receipt_identity)
        receipt_device, receipt_inode = _canonical_lease_receipt(receipt_raw)
        _fail((receipt_device, receipt_inode) == (lease_device, lease_inode),
              "pre-submit lease identity differs from the installed root receipt")
        lease_dir_fd = _safe_dir(LEASE_PARENT, service.pw_uid, service.pw_gid, 0o710)
        lease_lock_fd = os.open("host.lock", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                                dir_fd=lease_dir_fd)
        lease_fd_info = os.fstat(lease_lock_fd)
        lease_path_info = os.stat("host.lock", dir_fd=lease_dir_fd, follow_symlinks=False)
        _write_json_private(output, "terminal-lease-open.json", {
            "fd": {"device": lease_fd_info.st_dev, "inode": lease_fd_info.st_ino,
                   "uid": lease_fd_info.st_uid, "gid": lease_fd_info.st_gid,
                   "mode": _mode(lease_fd_info), "links": lease_fd_info.st_nlink},
            "path": {"device": lease_path_info.st_dev, "inode": lease_path_info.st_ino,
                     "uid": lease_path_info.st_uid, "gid": lease_path_info.st_gid,
                     "mode": _mode(lease_path_info), "links": lease_path_info.st_nlink,
                     "symlink": stat.S_ISLNK(lease_path_info.st_mode)},
        })
        lease_start = _lease_snapshot(lease_dir_fd, lease_lock_fd, service.pw_uid, service.pw_gid,
                                      lease_device, lease_inode)
        _write_json_private(output, "terminal-lease-start.json", {
            **lease_start, "root_receipt_device": receipt_device,
            "root_receipt_inode": receipt_inode,
            "lease_continuity": "unproven; this is a terminal identity snapshot, no flock or /proc/locks inference",
        })

        mountinfo = _read_bounded_file("/proc/self/mountinfo", 4 * 1024 * 1024)
        _write_private(output, "terminal-mountinfo.txt", mountinfo)
        _fail(_mountinfo_has_cgroup2(mountinfo, CGROUP_ROOT),
              "recorded cgroup root is not an exact cgroup2 mountpoint")
        cgroup_snapshots.append(_terminal_cgroup_snapshot(output, identity, "before"))

        units = [str(identity["outer_unit"])] + [
            f"buster-bench-{job}-{attempt}-{suffix}.service" for suffix in TERMINAL_STAGE_SUFFIXES]
        unit_states = []
        for unit in units:
            unit_states.append(_check_terminal_unit(unit, output, deadline, identity,
                                                    outer=(unit == identity["outer_unit"])))

        _terminal_absence_snapshot(workspaces_fd, result_leaf_fd, "after", output, job, attempt)
        cgroup_snapshots.append(_terminal_cgroup_snapshot(output, identity, "after"))
        records_after = _open_records(job, request_sha)
        _fail(records_after[0] == worker and records_after[1] == instance,
              "durable worker/instance record changed during terminal proof")
        queue_fd = _safe_dir(QUEUE, service.pw_uid, service.pw_gid, 0o710)
        try:
            cleanup_after = _read_at(queue_fd, f"cleanup-{job}", 128,
                                     service.pw_uid, service.pw_gid, 0o400)
        finally:
            os.close(queue_fd)
        _fail(cleanup_after == cleanup_raw, "durable cleanup record changed during terminal proof")
        receipt_after, receipt_identity_after = _read_variable_snapshot(
            receipt_dir_fd, LEASE_RECEIPT_NAME, 128, ROOT_UID, ROOT_GID, 0o444)
        _fail(receipt_after == receipt_raw and receipt_identity_after == receipt_identity,
              "root-owned lease identity receipt changed during terminal proof")
        lease_end = _lease_snapshot(lease_dir_fd, lease_lock_fd, service.pw_uid, service.pw_gid,
                                    lease_device, lease_inode)
        _fail(lease_end == lease_start, "stable lease identity changed during terminal proof")
        directory_paths = _terminal_directory_paths(workspaces_fd, results_fd, result_leaf_fd,
                                                     lease_dir_fd, receipt_dir_fd, service.pw_uid,
                                                     service.pw_gid, candidate.pw_gid, job, attempt,
                                                     output)
        proof = {"disposition": "TERMINAL_SNAPSHOT_PASS", "job": job, "attempt": attempt,
                 "request_sha256": request_sha, "boot_id": identity["boot_id"],
                 "baseline": baseline, "subject": subject,
                 "worker_sha256": identity["worker_sha256"],
                 "instance_sha256": identity["instance_sha256"],
                 "cleanup_sha256": hashlib.sha256(cleanup_raw).hexdigest(),
                 "result_leaf": f"{RESULTS}/{result_leaf_name}",
                 "result_leaf_device": result_leaf_info.st_dev,
                 "result_leaf_inode": result_leaf_info.st_ino,
                 "cleanup_identity": cleanup_identity,
                 "lease_receipt_identity": receipt_identity,
                 "lease_start": lease_start, "lease_end": lease_end,
                 "directory_paths": directory_paths,
                 "lease_continuity": "unproven; observations do not establish continuous ownership",
                 "lock_held_by_job": "unproven; no flock or /proc/locks inspection was performed",
                 "cgroup_snapshots": cgroup_snapshots,
                 "systemd_unit_states": [
                     {"unit": unit, "load_state": props.get("LoadState"),
                      "active_state": props.get("ActiveState"),
                      "main_pid": props.get("MainPID"),
                      "invocation_id": props.get("InvocationID")}
                     for unit, props in zip(units, unit_states)],
                 "limitations": ["terminal snapshot only", "continuous lease ownership not proven",
                                 "full all-stage lifecycle/recovery acceptance remains pending"]}
        _remaining(deadline)
        _write_json_private(output, "terminal-proof.json", proof)
        return proof
    finally:
        for descriptor in (lease_lock_fd, lease_dir_fd, receipt_dir_fd,
                           result_leaf_fd, results_fd, workspaces_fd):
            if descriptor >= 0:
                os.close(descriptor)


def _checkpoint(job: int, identity: dict[str, object], output: Path,
                deadline: float) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    outer = capture_unit(str(identity["outer_unit"]), output, "outer-before", 8.0, deadline)
    validate_outer(outer, identity)
    build_unit = f"buster-bench-{job}-{identity['attempt']}-base-build.service"
    build = capture_unit(build_unit, output, "base-build-before", 8.0, deadline)
    build_identity = validate_build(build, build_unit)
    return outer, build, {"unit": build_unit, **build_identity}


def _cleanup_payload(result_fd: int, entry_identity: tuple[int, int] | None,
                     created: bool) -> bool:
    if not created:
        return True
    if entry_identity is None:
        return False
    try:
        current = os.stat("payload", dir_fd=result_fd, follow_symlinks=False)
    except FileNotFoundError:
        return True
    except OSError:
        return False
    if not stat.S_ISREG(current.st_mode) or (current.st_dev, current.st_ino) != entry_identity or \
       current.st_nlink != 1:
        return False
    try:
        os.unlink("payload", dir_fd=result_fd)
        try:
            os.stat("payload", dir_fd=result_fd, follow_symlinks=False)
        except FileNotFoundError:
            return True
        return False
    except OSError:
        return False


def _payload_absent(result_fd: int) -> bool | None:
    if result_fd < 0:
        return None
    try:
        os.stat("payload", dir_fd=result_fd, follow_symlinks=False)
        return False
    except FileNotFoundError:
        return True
    except OSError:
        return False


def _record_discovery_failure(output: Path, failures: dict[str, object], reason: str,
                              records: tuple[bytes, bytes, dict[str, object]] | None) -> None:
    """Retain bounded first observations when later retries overwrite snapshots."""
    reason = reason[:1024]
    counts = failures["reason_counts"]
    if reason in counts or len(counts) < 16:
        counts[reason] = counts.get(reason, 0) + 1
    else:
        failures["other_reason_count"] += 1
    failures["attempts"] += 1
    failures["last_reason"] = reason
    if records is not None and not failures["first_records_retained"]:
        _write_private(output, "discovery-worker-record.bin", records[0])
        _write_private(output, "discovery-instance-record.bin", records[1])
        failures["first_records_retained"] = True
    for label in ("outer-before", "base-build-before"):
        saved = output / f"discovery-first-{label}.systemctl-show"
        current = output / f"{label}.systemctl-show"
        if not saved.exists() and current.exists():
            with current.open("rb") as source:
                raw = source.read(65537)
            _write_private(output, saved.name, raw[:65536])
            failures["first_snapshots"][label] = {"reason": reason,
                                                "truncated": len(raw) > 65536}
    (output / "discovery-failures.json").write_text(
        json.dumps(failures, sort_keys=True, indent=2) + "\n")


def _run_probe(job: int, request_sha: str, baseline: str, subject: str,
               budget: float, output: Path) -> dict[str, object]:
    import issue1162_workspace_observer as workspace_observer
    coverage = {
        "disposition": "normal-path checkpoint probe only",
        "checkpoint_discovery_window_seconds": 300,
        "captured_when_available": [
            "installed service full systemctl show and MainPID proc status/mountinfo/cgroup/executable hash",
            "one passive active-service lease path, descriptor/fdinfo, and /proc/locks snapshot bound to the root receipt",
            "exact active outer full systemctl show, invocation, MainPID, proc and cgroup identity",
            "one exact active base-build full systemctl show, invocation, MainPID, proc and cgroup identity before and after payload cleanup",
            "immutable worker and instance record bytes bound to submit request SHA-256",
            "complete settled sealed-source metadata and exact fixed workspace path modes at a live base-build checkpoint",
            "temporary result payload metadata, inode-bound cleanup, and cleanup absence",
            "broker instance unit/process details only if still listed after the live probe",
        ],
        "not_captured_or_not_claimed": [
            "all five build-stage identities and every final build node before throughput",
            "recovery scenarios, authenticated publication/replay acceptance, or a complete deployment packet",
            "short-lived broker processes that exited before post-probe capture",
            "physical host readiness, host installation, or service/security acceptance",
            "continuous coordinator FD retention or shared open-file-description identity from inode matching",
            "instantaneous materializer mkdir/chmod calls or rehashing every sealed source byte",
        ],
        "full_acceptance": "pending",
    }
    (output / "coverage.json").write_text(json.dumps(coverage, sort_keys=True, indent=2) + "\n")
    _fail(is_root() and in_disposable_container(), "not root in the disposable container")
    _fail(HEX64.fullmatch(request_sha) is not None, "submit digest missing or malformed")
    _fail(re.fullmatch(r"[0-9]+", str(job)) is not None and job > 0, "invalid job id")
    service = pwd.getpwnam("buster-bench")
    job_deadline = time.monotonic() + max(0.0, budget)
    discovery_deadline = min(job_deadline - 100.0, time.monotonic() + 300.0)
    last_error = "active base-build checkpoint not observed"
    records: tuple[bytes, bytes, dict[str, object]] | None = None
    before_outer: dict[str, object] | None = None
    before_build: dict[str, object] | None = None
    build_identity: dict[str, object] | None = None
    discovery_failures = {"job": job, "request_sha256": request_sha, "attempts": 0,
                          "reason_counts": {}, "other_reason_count": 0,
                          "first_records_retained": False, "first_snapshots": {}}
    while time.monotonic() < discovery_deadline:
        remain = discovery_deadline - time.monotonic()
        try:
            records = _open_records(job, request_sha)
            before_outer, before_build, build_identity = _checkpoint(job, records[2], output, discovery_deadline)
            break
        except (OSError, ProbeError, subprocess.TimeoutExpired) as exc:
            last_error = str(exc)
            _record_discovery_failure(output, discovery_failures, last_error, records)
            records = None
            before_outer = before_build = build_identity = None
            time.sleep(min(1.0, max(0.0, discovery_deadline - time.monotonic())))
    _fail(records is not None and before_outer is not None and before_build is not None and
          build_identity is not None, last_error)

    worker, instance, outer_identity = records
    _write_private(output, "worker-record.bin", worker)
    _write_private(output, "instance-record.bin", instance)
    _write_private(output, "identity-records.json", (json.dumps({
        "worker_record": {"bytes": len(worker), "sha256": hashlib.sha256(worker).hexdigest()},
        "instance_record": {"bytes": len(instance), "sha256": hashlib.sha256(instance).hexdigest()},
        "job": job, "attempt": outer_identity["attempt"],
        "request_sha256": request_sha,
    }, sort_keys=True, indent=2) + "\n").encode("utf-8"))
    service_snapshot = capture_unit("buster-bench.service", output, "service", 8.0,
                                    job_deadline, reserve=75.0)
    service_props = service_snapshot["properties"]
    validate_service(service_snapshot)
    active_lease = capture_active_lease(service_snapshot, str(outer_identity["boot_id"]),
                                        service.pw_uid, service.pw_gid, output, job_deadline)
    outer_live_start = capture_unit(str(outer_identity["outer_unit"]), output, "outer-live-start", 8.0,
                                    job_deadline, reserve=75.0)
    validate_outer(outer_live_start, outer_identity)
    _fail(outer_capture_identity(before_outer) == outer_capture_identity(outer_live_start),
          "outer checkpoint changed before payload creation")
    build_unit = str(build_identity["unit"])
    base_build_live_start = capture_unit(build_unit, output, "base-build-live-start", 8.0,
                                         job_deadline, reserve=75.0)
    live_build_identity = validate_build(base_build_live_start, build_unit)
    initial_build_identity = {key: value for key, value in build_identity.items() if key != "unit"}
    _fail(same_build(initial_build_identity, live_build_identity),
          "base-build checkpoint changed before payload creation")
    workspace_snapshot = workspace_observer.base_build_workspace(outer_identity, baseline, subject,
                                                                  job_deadline)
    _write_json_private(output, "base-build-workspace.json", workspace_snapshot)
    workspace_outer = capture_unit(str(outer_identity["outer_unit"]), output,
                                   "outer-after-workspace", 8.0, job_deadline, reserve=75.0)
    validate_outer(workspace_outer, outer_identity)
    _fail(outer_capture_identity(outer_live_start) == outer_capture_identity(workspace_outer),
          "outer invocation or cgroup advanced during workspace capture")
    workspace_build = capture_unit(build_unit, output, "base-build-after-workspace", 8.0,
                                   job_deadline, reserve=75.0)
    _fail(same_build(live_build_identity, validate_build(workspace_build, build_unit)),
          "base-build invocation or cgroup advanced during workspace capture")
    attempt = int(outer_identity["attempt"])
    results_fd = _safe_dir(RESULTS, service.pw_uid, service.pw_gid, 0o710)
    result_fd = -1
    payload_created = False
    payload_identity: tuple[int, int] | None = None
    probe_exit = 125
    cleanup_ok = True
    process_group_clean = False
    process_leaked_children = False
    probe_timed_out = False
    run_error: BaseException | None = None
    run_traceback = None
    receipt_error: BaseException | None = None
    result_path = f"job-{job}-attempt-{attempt}"
    try:
        result_fd = os.open(result_path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=results_fd)
        dir_info = os.fstat(result_fd)
        _fail(stat.S_ISDIR(dir_info.st_mode) and dir_info.st_uid == service.pw_uid and
              dir_info.st_gid == service.pw_gid and _mode(dir_info) == 0o700,
              "result directory identity mismatch")
        try:
            os.stat("payload", dir_fd=result_fd, follow_symlinks=False)
            raise ProbeError("payload already exists; refusing to overwrite")
        except FileNotFoundError:
            pass
        payload_fd = os.open("payload", os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC |
                             os.O_NOFOLLOW, 0o600, dir_fd=result_fd)
        payload_created = True
        try:
            initial = os.fstat(payload_fd)
            _fail(stat.S_ISREG(initial.st_mode) and initial.st_nlink == 1,
                  "created payload is not a regular single-link file")
            payload_identity = (initial.st_dev, initial.st_ino)
            os.fchown(payload_fd, service.pw_uid, service.pw_gid)
            os.fchmod(payload_fd, 0o400)
            info = os.fstat(payload_fd)
            _fail(stat.S_ISREG(info.st_mode) and info.st_uid == service.pw_uid and
                  info.st_gid == service.pw_gid and _mode(info) == 0o400 and info.st_nlink == 1,
                  "created payload metadata mismatch")
            _fail((info.st_dev, info.st_ino) == payload_identity, "payload inode changed during metadata setup")
        finally:
            os.close(payload_fd)
        env = dict(os.environ, BUSTER_BROKER_LIVE_TEST="1")
        run = run_probe_process([LIVE_TEST, str(job), str(attempt), baseline, subject], env,
                                min(30.0, _remaining(job_deadline, reserve=60.0)), job_deadline)
        probe_exit = int(run["exit"])
        process_group_clean = bool(run["group_clean"])
        process_leaked_children = bool(run["leaked_children"])
        probe_timed_out = bool(run["timed_out"])
        (output / "live-test.stdout").write_text(str(run["stdout"]))
        (output / "live-test.stderr").write_text(str(run["stderr"]))
    except BaseException as exc:
        run_error = exc
        run_traceback = exc.__traceback__
    finally:
        try:
            cleanup_ok = _cleanup_payload(result_fd, payload_identity, payload_created)
        except BaseException as exc:
            cleanup_ok = False
            if run_error is None:
                run_error = exc
                run_traceback = exc.__traceback__
        payload_absent = _payload_absent(result_fd)
        receipt = {
            "created": payload_created, "device": payload_identity[0] if payload_identity else None,
            "inode": payload_identity[1] if payload_identity else None,
            "cleanup_proven": cleanup_ok, "payload_absent": payload_absent,
        }
        try:
            _write_private(output, "payload-cleanup.json",
                           (json.dumps(receipt, sort_keys=True, indent=2) + "\n").encode("utf-8"))
        except BaseException as exc:
            receipt_error = exc
        close_errors = []
        for descriptor in (result_fd, results_fd):
            if descriptor >= 0:
                try:
                    os.close(descriptor)
                except BaseException as exc:
                    close_errors.append(str(exc))
        if close_errors:
            if run_error is None:
                run_error = ProbeError("failed to close pinned result directory descriptors: " + "; ".join(close_errors))
                run_traceback = run_error.__traceback__
            else:
                print("result directory close error: " + "; ".join(close_errors), file=sys.stderr)
    if run_error is not None:
        if receipt_error is not None:
            print(f"payload cleanup receipt write failed: {receipt_error}", file=sys.stderr)
        raise run_error.with_traceback(run_traceback)
    if receipt_error is not None:
        raise ProbeError(f"payload cleanup receipt write failed: {receipt_error}") from receipt_error
    _fail(cleanup_ok and payload_absent is True,
          "owned payload cleanup or payload absence could not be proven; evidence retained")

    _remaining(job_deadline)
    records_after = _open_records(job, request_sha)
    _fail(records_after[0] == worker and records_after[1] == instance,
          "worker/instance record changed during live probe")
    after_outer = capture_unit(str(outer_identity["outer_unit"]), output, "outer-after-cleanup", 8.0,
                               job_deadline, reserve=8.0)
    validate_outer(after_outer, outer_identity)
    _fail(outer_capture_identity(outer_live_start) == outer_capture_identity(after_outer),
          "outer invocation, MainPID, process, or cgroup advanced during live probe")
    after_build = capture_unit(build_unit, output, "base-build-after-cleanup", 8.0, job_deadline)
    after_build_identity = validate_build(after_build, build_unit)
    _fail(same_build(live_build_identity, after_build_identity),
          "base-build invocation, MainPID, process, or cgroup advanced before cleanup checkpoint")
    # Broker capture is optional post-probe evidence. It never substitutes for
    # the before/after outer and base-build checkpoint just verified above.
    broker_coverage: dict[str, object] = {
        "listed_units": [], "captures": [],
        "limitation": "only instances still listed after the live probe are captured; exited instances are not inferred",
    }
    try:
        remaining = _remaining(job_deadline, reserve=25.0)
        listed = subprocess.run(["/usr/bin/systemctl", "list-units", "--all", "--plain",
                                 "--no-legend", "buster-bench-systemd-broker@*.service"],
                                check=False, capture_output=True, text=True,
                                timeout=min(5.0, remaining))
        (output / "broker-units-observed.txt").write_text(listed.stdout + listed.stderr)
        _fail(listed.returncode == 0, f"broker unit inventory exit={listed.returncode}")
        broker_units = []
        for line in listed.stdout.splitlines():
            fields = line.split()
            unit = fields[0] if fields else ""
            if unit.startswith("buster-bench-systemd-broker@") and unit.endswith(".service"):
                broker_units.append(unit)
        broker_coverage["listed_units"] = broker_units
        _fail(len(broker_units) <= 8, "broker unit inventory exceeded capture bound")
        for index, unit in enumerate(broker_units):
            reserve = 15.0 + 4.0 * (len(broker_units) - index - 1)
            captured = capture_unit(unit, output, f"broker-{index}", 4.0,
                                    job_deadline, reserve=reserve)
            process_present = isinstance(captured.get("process"), dict)
            cgroup_present = isinstance(captured.get("unit_cgroup"), dict) and \
                             "inode" in captured.get("unit_cgroup", {})
            broker_coverage["captures"].append({"unit": unit, "process_capture": process_present,
                                                  "cgroup_capture": cgroup_present,
                                                  "process_capture_reason": captured.get("process_capture")})
    except (ProbeError, OSError, subprocess.SubprocessError) as exc:
        broker_coverage["capture_gap"] = str(exc)
    (output / "broker-capture-coverage.json").write_text(
        json.dumps(broker_coverage, sort_keys=True, indent=2) + "\n")
    _fail(process_group_clean and not process_leaked_children,
          "live probe subprocess group cleanup was incomplete or a child leaked")
    _fail(not probe_timed_out, "live probe timed out; probe process group termination recorded")
    _fail(probe_exit == 0, f"live probe exit {probe_exit}")
    return {"valid": True, "job": job, "attempt": attempt,
            "outer_invocation": outer_identity["outer_invocation"],
            "worker_sha256": outer_identity["worker_sha256"],
            "instance_sha256": outer_identity["instance_sha256"],
            "request_sha256": request_sha, "base_build": after_build_identity,
            "base_build_workspace_artifact": "base-build-workspace.json",
            "payload_device": payload_identity[0] if payload_identity else None,
            "payload_inode": payload_identity[1] if payload_identity else None,
            "payload_absent_after_cleanup": True, "live_test_exit": probe_exit,
            "process_group_clean": process_group_clean,
            "active_lease_snapshot": active_lease,
            "broker_capture_coverage": broker_coverage, "full_acceptance": "pending"}


def self_test() -> None:
    boot = "12345678-1234-1234-1234-123456789abc"
    digest = "a" * 64

    def record(magic: bytes, job: int, token: int, request: str,
               cgroup_ids: tuple[int, int, int, int, int, int] = (1, 2, 3, 4, 5, 6)) -> bytearray:
        value = bytearray(232 if magic.startswith(b"BQWORKER") else 544)
        value[:16] = magic
        value[16:24] = job.to_bytes(8, "little")
        value[24:32] = token.to_bytes(8, "little")
        value[32:96] = request.encode()
        value[96:132] = boot.encode()
        value[136:136 + len(f"buster-bench-{job}-{token}.service")] = f"buster-bench-{job}-{token}.service".encode()
        if magic.startswith(b"BQINSTANCE"):
            value[232:264] = b"b" * 32
            value[272:280] = (1).to_bytes(8, "little")
            value[280:288] = (2).to_bytes(8, "little")
            cgroup = b"/buster.slice/buster-bench.slice/buster-bench-7-2.service"
            value[288:288 + len(cgroup)] = cgroup
            for offset, number in zip((272, 280, 480, 488, 496, 504), cgroup_ids):
                value[offset:offset + 8] = number.to_bytes(8, "little")
        return value

    worker = bytes(record(b"BQWORKER00000001", 7, 2, digest))
    instance = bytes(record(b"BQINSTANCE000002", 7, 2, digest))
    checks = 0

    def rejects(callable_obj, *args) -> None:
        nonlocal checks
        try:
            callable_obj(*args)
        except ProbeError:
            checks += 1
            return
        raise AssertionError("expected fail-closed rejection")

    parsed = parse_records(worker, instance, 7, digest, boot)
    assert parsed["attempt"] == 2  # Submit's token=0 is never treated as attempt.
    checks += 1
    # The production worker-unit entry execs the recipe driver before builds;
    # the long-lived daemon retains the service executable.
    assert _expected_executable("buster-bench.service") == "/usr/local/libexec/buster-bench-service"
    assert _expected_executable("buster-bench-7-2.service") == "/usr/local/libexec/buster-bench-build"
    assert _expected_executable("buster-bench-7-2-base-build.service") == "/usr/local/libexec/buster-bench-build"
    assert _expected_executable("buster-bench-unrelated.service") is None
    checks += 4
    with tempfile.TemporaryDirectory() as temp:
        output = Path(temp)
        failures = {"job": 7, "request_sha256": digest, "attempts": 0,
                    "reason_counts": {}, "other_reason_count": 0,
                    "first_records_retained": False, "first_snapshots": {}}
        (output / "outer-before.systemctl-show").write_text("ActiveState=active\nMainPID=310\n")
        _record_discovery_failure(output, failures, "first executable mismatch", (worker, instance, parsed))
        (output / "outer-before.systemctl-show").write_text("ActiveState=inactive\nMainPID=0\n")
        for index in range(20):
            _record_discovery_failure(output, failures, "later reason " + str(index), None)
        retained = json.loads((output / "discovery-failures.json").read_text())
        assert retained["attempts"] == 21 and len(retained["reason_counts"]) == 16
        assert retained["other_reason_count"] == 5 and retained["last_reason"] == "later reason 19"
        assert (output / "discovery-worker-record.bin").read_bytes() == worker
        assert (output / "discovery-instance-record.bin").read_bytes() == instance
        assert (output / "discovery-first-outer-before.systemctl-show").read_text() == "ActiveState=active\nMainPID=310\n"
        checks += 1  # Later terminal observations cannot erase the first live failure.
        (output / "base-build-before.systemctl-show").write_bytes(b"x" * 70000)
        _record_discovery_failure(output, failures, "r" * 2000, None)
        assert len((output / "discovery-first-base-build-before.systemctl-show").read_bytes()) == 65536
        assert failures["first_snapshots"]["base-build-before"]["truncated"]
        assert len(failures["last_reason"]) == 1024
        checks += 1
    service_ok = {"properties": {"ActiveState": "active", "MainPID": "210",
                                 "ControlGroup": SERVICE_CGROUP},
                  "unit_cgroup": {"device": 7, "inode": 8, "main_pid_member": True,
                                  "populated": "1"},
                  "process": {"pid": 210, "cgroup": f"0::{SERVICE_CGROUP}",
                              "cgroup_matches_unit": True, "exe_hash_matches_installed": True}}
    validate_service(service_ok)
    checks += 1
    wrong_service = json.loads(json.dumps(service_ok))
    wrong_service["properties"]["ControlGroup"] = "/unexpected/buster-bench.service"
    rejects(validate_service, wrong_service)
    zero_token_worker = bytes(record(b"BQWORKER00000001", 7, 0, digest))
    zero_token_instance = bytes(record(b"BQINSTANCE000002", 7, 0, digest))
    rejects(parse_records, zero_token_worker, zero_token_instance, 7, digest, boot)
    rejects(parse_records, worker, instance, 8, digest, boot)
    rejects(parse_records, worker, instance, 7, "c" * 64, boot)
    stale_instance = bytearray(instance)
    stale_instance[24:32] = (3).to_bytes(8, "little")
    rejects(parse_records, worker, bytes(stale_instance), 7, digest, boot)
    try:
        validate_build({"properties": {"ActiveState": "inactive"}},
                       "buster-bench-7-2-base-build.service")
    except ProbeError:
        checks += 1
    else:
        raise AssertionError("missing active build checkpoint was accepted")
    before = {"invocation": "x", "main_pid": 3, "cgroup": "/a", "device": 4,
              "inode": 5, "process_starttime_ticks": 6}
    after = dict(before, invocation="y")
    assert not same_build(before, after)
    checks += 1  # Stage advance is an invalid validation checkpoint.
    with tempfile.TemporaryDirectory() as temp:
        results = Path(temp) / "results"
        results.mkdir(mode=0o710)
        results.chmod(0o710)
        result_fd = _safe_dir(str(results), os.getuid(), os.getgid(), 0o710)
        try:
            opened = os.fstat(result_fd)
            assert stat.S_ISDIR(opened.st_mode) and _mode(opened) == 0o710
            checks += 1  # Pinned directory fd is returned and retained by the caller.
        finally:
            os.close(result_fd)
        queue = Path(temp) / "queue"
        queue.mkdir(mode=0o710)
        queue.chmod(0o710)
        queue_fd = os.open(queue, os.O_RDONLY | os.O_DIRECTORY)
        try:
            record_fd = os.open("worker-fixture", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o440,
                                dir_fd=queue_fd)
            os.write(record_fd, worker)
            os.fchown(record_fd, os.getuid(), os.getgid())
            os.fchmod(record_fd, 0o440)
            os.close(record_fd)
            assert _read_at(queue_fd, "worker-fixture", 232, os.getuid(), os.getgid(), 0o440) == worker
            checks += 1  # atime changes do not invalidate an unchanged immutable record.
        finally:
            os.close(queue_fd)
    with tempfile.TemporaryDirectory() as temp:
        fd = os.open(temp, os.O_RDONLY | os.O_DIRECTORY)
        try:
            obj = os.open("payload", os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600, dir_fd=fd)
            incomplete = os.fstat(obj)
            os.close(obj)
            assert _cleanup_payload(fd, (incomplete.st_dev, incomplete.st_ino), True)
            checks += 1  # Failed chmod setup still removes only the known owned inode.
            obj = os.open("payload", os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o400, dir_fd=fd)
            info = os.fstat(obj)
            os.close(obj)
            assert _cleanup_payload(fd, (info.st_dev, info.st_ino), True)
            checks += 1
            obj = os.open("payload", os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o400, dir_fd=fd)
            os.close(obj)
            wrong = os.stat("payload", dir_fd=fd, follow_symlinks=False)
            assert not _cleanup_payload(fd, (wrong.st_dev, wrong.st_ino + 1), True)
            assert os.stat("payload", dir_fd=fd, follow_symlinks=False).st_ino == wrong.st_ino
            checks += 1  # Never unlink a replacement inode.
            os.unlink("payload", dir_fd=fd)
        finally:
            os.close(fd)
    with tempfile.TemporaryDirectory() as temp:
        child = [sys.executable, "-c",
                 "import subprocess,sys,time; subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)']); time.sleep(60)"]
        timeout_result = run_probe_process(child, dict(os.environ), 0.2, time.monotonic() + 8.0)
        assert timeout_result["timed_out"] and timeout_result["exit"] == 124
        assert timeout_result["group_clean"] and not timeout_result["leaked_children"]
        checks += 1  # Timeout kills and reaps only the probe's process group.

    def orchestration_case(stage_advance: bool = False, replace_payload: bool = False,
                           timed_out: bool = False, service_wrong_cgroup: bool = False,
                           fail_setup: str | None = None) -> tuple[bool, str, bool, int,
                                                                  dict[str, object] | None, bool]:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cgroup_root = root / "cgroup"
            slice_path = cgroup_root / "buster.slice" / "buster-bench.slice"
            outer_cgroup = slice_path / "buster-bench-7-2.service"
            base_cgroup = slice_path / "buster-bench-7-2-base-build.service"
            outer_cgroup.mkdir(parents=True)
            base_cgroup.mkdir()
            root_stat = cgroup_root.stat()
            slice_stat = slice_path.stat()
            outer_stat = outer_cgroup.stat()
            ids = (outer_stat.st_dev, outer_stat.st_ino, root_stat.st_dev, root_stat.st_ino,
                   slice_stat.st_dev, slice_stat.st_ino)
            fixture_worker = bytes(record(b"BQWORKER00000001", 7, 2, digest, ids))
            fixture_instance = bytes(record(b"BQINSTANCE000002", 7, 2, digest, ids))
            identity = parse_records(fixture_worker, fixture_instance, 7, digest, boot)
            results_root = root / "results"
            results_root.mkdir(mode=0o710)
            results_root.chmod(0o710)
            result_dir = results_root / "job-7-attempt-2"
            result_dir.mkdir(mode=0o700)
            result_dir.chmod(0o700)
            output = root / "evidence"
            output.mkdir(mode=0o700)
            owner = os.getuid()
            group = os.getgid()
            process_calls = 0

            def fake_capture(unit, destination, label, timeout, deadline=None, reserve=0.2):
                if unit == "buster-bench.service":
                    props = {"ActiveState": "active", "MainPID": "210", "InvocationID": "d" * 32,
                             "ControlGroup": "/unexpected/buster-bench.service" if service_wrong_cgroup
                             else SERVICE_CGROUP}
                    cginfo = {"device": 700, "inode": 800, "path": "/service",
                              "main_pid_member": True, "populated": "1"}
                    proc = {"pid": 210, "starttime_ticks": 210,
                            "cgroup": "0::" + props["ControlGroup"],
                            "cgroup_matches_unit": True,
                            "exe_hash_matches_installed": True}
                elif "base-build" in unit:
                    advanced = stage_advance and label == "base-build-after-cleanup"
                    props = {"ActiveState": "active", "MainPID": "411" if advanced else "410",
                             "InvocationID": "e" * 32 if advanced else "c" * 32,
                             "ControlGroup": "/buster.slice/buster-bench.slice/buster-bench-7-2-base-build.service"}
                    build_stat = base_cgroup.stat()
                    cginfo = {"device": build_stat.st_dev, "inode": build_stat.st_ino,
                              "path": str(base_cgroup), "main_pid_member": True, "populated": "1"}
                    proc = {"pid": int(props["MainPID"]), "starttime_ticks": int(props["MainPID"]),
                            "cgroup": "0::" + props["ControlGroup"],
                            "cgroup_matches_unit": True, "exe_hash_matches_installed": True}
                else:
                    props = {"ActiveState": "active", "MainPID": "310", "InvocationID": "b" * 32,
                             "ControlGroup": "/buster.slice/buster-bench.slice/buster-bench-7-2.service"}
                    outer_info = outer_cgroup.stat()
                    cginfo = {"device": outer_info.st_dev, "inode": outer_info.st_ino,
                              "path": str(outer_cgroup), "main_pid_member": True, "populated": "1"}
                    proc = {"pid": 310, "starttime_ticks": 310, "cgroup": "0::" + props["ControlGroup"],
                            "cgroup_matches_unit": True,
                            "exe_hash_matches_installed": True}
                return {"unit": unit, "properties": props, "unit_cgroup": cginfo, "process": proc}

            def fake_process(command, env, timeout, deadline):
                nonlocal process_calls
                process_calls += 1
                payload = result_dir / "payload"
                info = payload.stat(follow_symlinks=False)
                assert stat.S_ISREG(info.st_mode) and _mode(info) == 0o400 and info.st_nlink == 1
                if replace_payload:
                    payload.unlink()
                    os.symlink("owned-by-fixture", payload)
                return {"exit": 124 if timed_out else 0, "timed_out": timed_out,
                        "stdout": "fixture live-test stdout\n", "stderr": "fixture live-test stderr\n",
                        "group_clean": True, "leaked_children": False}

            def fake_subprocess_run(arguments, **kwargs):
                assert arguments[1:3] == ["list-units", "--all"]
                return subprocess.CompletedProcess(arguments, 0, "", "")

            disposition = ""
            cleanup_absent = False
            fake_account = SimpleNamespace(pw_uid=owner, pw_gid=group)
            original_fchmod, original_fchown = os.fchmod, os.fchown
            def maybe_fchmod(fd, mode):
                if fail_setup == "fchmod" and mode == 0o400:
                    raise OSError("fixture fchmod failure")
                return original_fchmod(fd, mode)
            def maybe_fchown(fd, uid, gid):
                if fail_setup == "fchown":
                    raise OSError("fixture fchown failure")
                return original_fchown(fd, uid, gid)
            with patch(__name__ + ".is_root", return_value=True), \
                 patch(__name__ + ".in_disposable_container", return_value=True), \
                 patch(__name__ + ".pwd.getpwnam", return_value=fake_account), \
                 patch(__name__ + ".CGROUP_ROOT", str(cgroup_root)), \
                 patch(__name__ + ".BENCH_SLICE_PATH", str(slice_path)), \
                 patch(__name__ + ".RESULTS", str(results_root)), \
                 patch(__name__ + "._open_records", return_value=(fixture_worker, fixture_instance, identity)), \
                 patch(__name__ + ".capture_unit", side_effect=fake_capture), \
                 patch("issue1162_workspace_observer.base_build_workspace",
                       return_value={"fixture": "source walk exercised by workspace observer self-test"}), \
                 patch(__name__ + ".capture_active_lease", return_value={"fixture": "passive-only"}), \
                 patch(__name__ + ".run_probe_process", side_effect=fake_process), \
                 patch(__name__ + ".subprocess.run", side_effect=fake_subprocess_run), \
                 patch(__name__ + ".os.fchmod", side_effect=maybe_fchmod), \
                 patch(__name__ + ".os.fchown", side_effect=maybe_fchown):
                try:
                    _run_probe(7, digest, "a" * 40, "b" * 40, 180.0, output)
                    disposition = "pass"
                except (ProbeError, OSError) as exc:
                    disposition = str(exc)
            cleanup_absent = not (result_dir / "payload").exists() and not (result_dir / "payload").is_symlink()
            receipt_path = output / "payload-cleanup.json"
            receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else None
            records_retained = (output / "worker-record.bin").read_bytes() == fixture_worker and \
                               (output / "instance-record.bin").read_bytes() == fixture_instance
            if replace_payload and (result_dir / "payload").is_symlink():
                (result_dir / "payload").unlink()
            return disposition == "pass", disposition, cleanup_absent, process_calls, receipt, records_retained

    positive, _, positive_cleanup, positive_process_calls, positive_receipt, records_retained = orchestration_case()
    assert positive and positive_cleanup and positive_process_calls == 1 and positive_receipt and \
           positive_receipt["cleanup_proven"] and positive_receipt["payload_absent"], \
           (positive, positive_process_calls, positive_receipt)
    assert records_retained
    checks += 1  # Positive orchestration retains parsed records and cleanup receipt privately.
    wrong_service, wrong_service_reason, wrong_service_cleanup, wrong_service_calls, _, _ = \
        orchestration_case(service_wrong_cgroup=True)
    assert not wrong_service and "expected system cgroup" in wrong_service_reason
    assert wrong_service_cleanup and wrong_service_calls == 0
    checks += 1  # Wrong service cgroup stops before fixture creation or C probe invocation.
    advanced, advanced_reason, advanced_cleanup, _, _, _ = orchestration_case(stage_advance=True)
    assert not advanced and "advanced" in advanced_reason and advanced_cleanup
    checks += 1  # Stage advance fails validation after exact fixture cleanup.
    timed, timeout_reason, timed_cleanup, _, _, _ = orchestration_case(timed_out=True)
    assert not timed and "timed out" in timeout_reason and timed_cleanup
    checks += 1  # Probe timeout is distinct from fixture cleanup and job outcome.
    replaced, replacement_reason, replacement_preserved, _, _, _ = orchestration_case(replace_payload=True)
    assert not replaced and "cleanup" in replacement_reason and not replacement_preserved
    checks += 1  # Cleanup refuses to unlink a replacement symlink.
    for failed_setup in ("fchown", "fchmod"):
        failed, reason, absent, calls, receipt, _ = orchestration_case(fail_setup=failed_setup)
        assert not failed and "fixture " + failed_setup + " failure" in reason
        assert absent and calls == 0 and receipt is not None and receipt["created"] and \
               receipt["cleanup_proven"] and receipt["payload_absent"] is True and \
               isinstance(receipt["device"], int) and \
               isinstance(receipt["inode"], int)
        checks += 1  # Actual setup failure still emits its receipt and removes the exact owned inode.

    def invoke_cli(arguments: list[str]) -> tuple[int, str, str]:
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch("sys.argv", ["issue1162_live_probe.py", *arguments]), \
             patch("sys.stdout", stdout), patch("sys.stderr", stderr):
            try:
                code = main()
            except (ProbeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
                print(str(exc), file=stderr)
                code = 1
        return code, stdout.getvalue(), stderr.getvalue()

    full_sha, manifest_sha, bundle_sha = "c" * 64, "d" * 64, "e" * 64
    result_text = (f"job=7 token=2 sequence=9 phase=finished outcome=succeeded validity=not-evaluated "
                   f"cancel-requested=0 reconciliation=0 pending=3 retained=5 request-sha256={digest} failure=ok\n"
                   "result-bound=1 statistical-decision=not-evaluated\n"
                   f"result-root={RESULTS}/job-7-attempt-2\nmanifest-sha256={manifest_sha}\n"
                   f"bundle-sha256={bundle_sha}\nfull-result-sha256={full_sha}\n").encode("ascii")
    with tempfile.TemporaryDirectory() as temp:
        result_file = Path(temp) / "result.txt"
        result_file.write_bytes(result_text)
        code, stdout, stderr = invoke_cli(["--check-result", str(result_file), "--job", "7",
                                           "--request-sha256", digest])
        assert code == 0 and stdout == f"2 {full_sha}\n" and not stderr
        checks += 1  # The CLI emits token/full SHA only after validating all six lines.
        invalid_receipts = (
            (result_text, 8, digest),
            (result_text.replace(b"token=2 sequence", b"token=2 token=2 sequence"), 7, digest),
            (result_text.replace(b"pending=3", b"pending=nope"), 7, digest),
            (result_text.replace(b"retained=5", b"retained=4294967296"), 7, digest),
            (result_text.replace(b"cancel-requested=0", b"cancel-requested=1"), 7, digest),
            (result_text.replace(b"reconciliation=0", b"reconciliation=1"), 7, digest),
            (result_text.replace(b"failure=ok", b"failure=worker-failed"), 7, digest),
            (result_text, 7, "f" * 64),
            (result_text.replace(b"result-bound=1", b"result-bound=0"), 7, digest),
            (result_text + b"extra\n", 7, digest),
        )
        for index, (raw, expected_job, expected_digest) in enumerate(invalid_receipts):
            result_file.write_bytes(raw)
            code, stdout, _ = invoke_cli(["--check-result", str(result_file), "--job", str(expected_job),
                                          "--request-sha256", expected_digest])
            assert code != 0 and stdout == "", f"invalid check-result fixture {index} was accepted"
            checks += 1
        result_file.write_bytes(b"x" * 4097)
        code, stdout, _ = invoke_cli(["--check-result", str(result_file), "--job", "7",
                                      "--request-sha256", digest])
        assert code != 0 and stdout == ""
        checks += 1  # The receipt reader rejects an oversized file without output.

    def terminal_case(fault: str | None = None) -> tuple[bool, str, dict[str, object] | None, bool, bool]:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            service_uid, service_gid = os.getuid(), os.getgid()
            candidate_gid = os.getgid()
            state = root / "state"
            queue = state / "queue"
            workspaces = state / "workspaces"
            results = workspaces / "results"
            lease_parent = state / "lease"
            receipt_dir = root / "etc" / "buster-bench"
            cgroup_root = root / "cgroup"
            slice_path = cgroup_root / "buster.slice" / "buster-bench.slice"
            def make_directory(path: Path, mode: int) -> None:
                path.mkdir(parents=True, exist_ok=True)
                path.chmod(mode)
            make_directory(queue, 0o710)
            make_directory(workspaces, 0o2710)
            make_directory(results, 0o710)
            result_leaf = results / "job-7-attempt-2"
            make_directory(result_leaf, 0o750 if fault == "result-mode" else 0o700)
            make_directory(lease_parent, 0o710)
            make_directory(receipt_dir, 0o755)
            make_directory(slice_path, 0o755)

            lease_path = lease_parent / "host.lock"
            lease_path.write_bytes(b"")
            lease_path.chmod(0o640)
            lease_stat = lease_path.stat()
            receipt_device = lease_stat.st_dev + (1 if fault == "receipt-mismatch" else 0)
            receipt_inode = lease_stat.st_ino
            receipt_file = receipt_dir / LEASE_RECEIPT_NAME
            receipt_file.write_text(f"device={receipt_device}\ninode={receipt_inode}\n", encoding="ascii")
            receipt_file.chmod(0o444)
            receipt_dir.chmod(0o555)

            cgroup_root_stat, slice_stat = cgroup_root.stat(), slice_path.stat()
            recorded_root_inode = cgroup_root_stat.st_ino + (1 if fault == "cgroup-root-mismatch" else 0)
            recorded_slice_inode = slice_stat.st_ino + (1 if fault == "cgroup-slice-mismatch" else 0)
            record_ids = (111, 222, cgroup_root_stat.st_dev, recorded_root_inode,
                          slice_stat.st_dev, recorded_slice_inode)
            fixture_worker = bytes(record(b"BQWORKER00000001", 7, 2, digest, record_ids))
            fixture_instance = bytes(record(b"BQINSTANCE000002", 7, 2, digest, record_ids))
            if fault == "record-request-mismatch":
                fixture_worker = bytes(record(b"BQWORKER00000001", 7, 2, "f" * 64, record_ids))
            if fault == "record-boot-mismatch":
                changed = bytearray(fixture_instance)
                changed[96:132] = b"00000000-0000-0000-0000-000000000000"
                fixture_instance = bytes(changed)

            def create_file(path: Path, content: bytes, mode: int) -> None:
                fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, mode)
                try:
                    view = memoryview(content)
                    while view:
                        view = view[os.write(fd, view):]
                    os.fchown(fd, service_uid, service_gid)
                    os.fchmod(fd, mode)
                finally:
                    os.close(fd)
            create_file(queue / "worker-7", fixture_worker, 0o440)
            create_file(queue / "worker-instance-7", fixture_instance, 0o440)
            workspace_info = workspaces.stat()
            cleanup = bytearray(128)
            cleanup[:16] = b"BQCLEANUP0000001"
            cleanup[16:24] = (7).to_bytes(8, "little")
            cleanup[24:32] = (2).to_bytes(8, "little")
            cleanup[32:40] = (workspace_info.st_dev + (1 if fault == "cleanup-parent-mismatch" else 0)).to_bytes(8, "little")
            cleanup[40:48] = workspace_info.st_ino.to_bytes(8, "little")
            cleanup[48:56] = workspace_info.st_dev.to_bytes(8, "little")
            cleanup[56:64] = (9001).to_bytes(8, "little")
            cleanup[64:128] = digest.encode("ascii")
            if fault == "cleanup-request-mismatch":
                cleanup[64] = ord("f")
            if fault != "cleanup-missing":
                cleanup_mode = 0o444 if fault == "cleanup-mode" else 0o400
                if fault == "cleanup-symlink":
                    (queue / "cleanup-7").symlink_to("missing-record")
                else:
                    create_file(queue / "cleanup-7", bytes(cleanup), cleanup_mode)

            if fault == "workspace-symlink":
                (workspaces / "job-7-attempt-2").symlink_to("missing-workspace")
            elif fault == "workspace-present":
                make_directory(workspaces / "job-7-attempt-2", 0o700)
            if fault == "result-socket":
                try:
                    sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                    sock.bind(str(result_leaf / ".lease-handoff"))
                    sock.close()
                except OSError:
                    # Some test sandboxes deny AF_UNIX creation; the checker
                    # rejects every present nofollow inode at this path.
                    (result_leaf / ".lease-handoff").write_bytes(b"socket-path-occupied")
            elif fault == "result-socket-symlink":
                (result_leaf / ".lease-handoff").symlink_to("missing-socket")
            if fault == "result-payload":
                (result_leaf / "payload").write_bytes(b"leftover")

            live_leaf = None
            if fault == "live-descendant":
                live_leaf = slice_path / "buster-bench-7-2-base-build.service"
                make_directory(live_leaf, 0o755)
                (live_leaf / "cgroup.procs").write_text("\n", encoding="ascii")
                (live_leaf / "cgroup.events").write_text("populated 0\n", encoding="ascii")

            expected_boot = boot
            output = root / "evidence"
            owner = SimpleNamespace(pw_uid=service_uid, pw_gid=service_gid)
            candidate_account = SimpleNamespace(pw_uid=service_uid, pw_gid=candidate_gid)
            original_open = os.open
            def guarded_open(path, flags, *positional, **keywords):
                if fault == "permission" and path == "cleanup-7":
                    raise PermissionError("injected cleanup record permission denial")
                return original_open(path, flags, *positional, **keywords)
            original_read_text = Path.read_text
            def read_text(path, *positional, **keywords):
                if str(path) == "/proc/sys/kernel/random/boot_id":
                    return expected_boot + "\n"
                return original_read_text(path, *positional, **keywords)
            original_bounded = _read_bounded_file
            mountinfo = f"1 0 0:77 / {cgroup_root} rw - cgroup2 cgroup rw\n".encode("ascii")
            def bounded_file(path, maximum):
                if path == "/proc/self/mountinfo":
                    return mountinfo
                return original_bounded(path, maximum)

            manager_calls = 0
            def manager(command, **kwargs):
                nonlocal manager_calls
                manager_calls += 1
                unit = command[-1]
                if manager_calls == 1 and fault == "workspace-parent-replaced":
                    retired = workspaces.with_name("workspaces-retired")
                    workspaces.rename(retired)
                    make_directory(workspaces, 0o2710)
                elif manager_calls == 1 and fault == "result-parent-replaced":
                    retired = results.with_name("results-retired")
                    results.rename(retired)
                    make_directory(results, 0o710)
                elif manager_calls == 1 and fault == "result-leaf-replaced":
                    retired = result_leaf.with_name("result-leaf-retired")
                    result_leaf.rename(retired)
                    make_directory(result_leaf, 0o700)
                elif manager_calls == 1 and fault == "lease-parent-replaced":
                    retired = lease_parent.with_name("lease-retired")
                    lease_parent.rename(retired)
                    make_directory(lease_parent, 0o710)
                    replacement = lease_parent / "host.lock"
                    replacement.write_bytes(b"")
                    replacement.chmod(0o640)
                elif manager_calls == 1 and fault == "receipt-parent-replaced":
                    retired = receipt_dir.with_name("receipt-retired")
                    receipt_dir.rename(retired)
                    make_directory(receipt_dir, 0o755)
                    replacement = receipt_dir / LEASE_RECEIPT_NAME
                    replacement.write_text(f"device={receipt_device}\ninode={receipt_inode}\n", encoding="ascii")
                    replacement.chmod(0o444)
                    receipt_dir.chmod(0o555)
                if fault == "manager-error" and unit.endswith("base-build.service"):
                    return subprocess.CompletedProcess(command, 1, "", "injected manager failure")
                if fault == "manager-timeout" and unit.endswith("base-generate.service"):
                    raise subprocess.TimeoutExpired(command, kwargs["timeout"], output="partial", stderr="stalled")
                if unit == "buster-bench-7-2.service":
                    if fault == "outer-gone":
                        props = {"Id": unit, "LoadState": "not-found"}
                    elif fault == "outer-active":
                        props = {"Id": unit, "LoadState": "loaded", "ActiveState": "active",
                                 "MainPID": "123", "InvocationID": "b" * 32,
                                 "CollectMode": "inactive", "Result": "running",
                                 "ControlGroup": "/buster.slice/buster-bench.slice/" + unit}
                    elif fault == "outer-wrong-invocation":
                        props = {"Id": unit, "LoadState": "loaded", "ActiveState": "inactive",
                                 "MainPID": "0", "InvocationID": "c" * 32,
                                 "CollectMode": "inactive", "Result": "success",
                                 "ControlGroup": "/buster.slice/buster-bench.slice/" + unit}
                    else:
                        props = {"Id": unit, "LoadState": "loaded", "ActiveState": "inactive",
                                 "MainPID": "0", "InvocationID": "b" * 32,
                                 "CollectMode": "inactive", "Result": "success",
                                 "ControlGroup": "/buster.slice/buster-bench.slice/" + unit}
                        if fault == "outer-result-failure":
                            props["Result"] = "exit-code"
                        elif fault == "outer-result-missing":
                            props.pop("Result")
                elif fault == "stage-loaded" and unit.endswith("base-build.service"):
                    props = {"Id": unit, "LoadState": "loaded", "ActiveState": "inactive"}
                else:
                    props = {"Id": unit, "LoadState": "not-found", "ActiveState": "inactive"}
                stdout = "".join(f"{key}={value}\n" for key, value in props.items())
                if fault == "lease-replaced-during" and manager_calls == 1:
                    lease_path.unlink()
                    lease_path.write_bytes(b"")
                    lease_path.chmod(0o640)
                return subprocess.CompletedProcess(command, 0, stdout, "")

            arguments = ["--terminal", "--job", "7", "--attempt", "2", "--request-sha256", digest,
                         "--baseline", "a" * 40, "--subject", "b" * 40,
                         "--lease-device", str(lease_stat.st_dev), "--lease-inode", str(lease_stat.st_ino),
                         "--budget-seconds", "60", "--output", str(output)]
            if fault == "attempt-mismatch":
                arguments[arguments.index("2")] = "3"
            if fault == "deadline":
                arguments[arguments.index("60")] = "0.5"
            with ExitStack() as stack:
                stack.enter_context(patch(__name__ + ".is_root", return_value=True))
                stack.enter_context(patch(__name__ + ".in_disposable_container", return_value=True))
                stack.enter_context(patch(__name__ + ".pwd.getpwnam", side_effect=lambda name:
                                          owner if name == "buster-bench" else candidate_account))
                for name, value in (("QUEUE", str(queue)), ("WORKSPACES", str(workspaces)),
                                    ("RESULTS", str(results)), ("LEASE_PARENT", str(lease_parent)),
                                    ("LEASE_RECEIPT_DIR", str(receipt_dir)),
                                    ("CGROUP_ROOT", str(cgroup_root)), ("BENCH_SLICE_PATH", str(slice_path)),
                                    ("ROOT_UID", service_uid), ("ROOT_GID", service_gid)):
                    stack.enter_context(patch(__name__ + "." + name, value))
                stack.enter_context(patch(__name__ + ".Path.read_text", read_text))
                stack.enter_context(patch(__name__ + "._read_bounded_file", side_effect=bounded_file))
                stack.enter_context(patch(__name__ + ".subprocess.run", side_effect=manager))
                stack.enter_context(patch(__name__ + ".os.open", side_effect=guarded_open))
                code, stdout, _ = invoke_cli(arguments)
            response = json.loads(stdout) if stdout else {}
            proof_file = output / "terminal-proof.json"
            proof = json.loads(proof_file.read_text()) if proof_file.exists() else None
            failure_receipt = (output / "disposition.json").exists()
            manager_capture_saved = bool(list(output.glob("terminal-unit-*.stdout")))
            return code == 0, response.get("reason", ""), proof, failure_receipt, manager_capture_saved

    success, _, terminal_proof, _, _ = terminal_case()
    assert success and terminal_proof is not None and \
           terminal_proof["disposition"] == "TERMINAL_SNAPSHOT_PASS" and \
           terminal_proof["lease_continuity"].startswith("unproven")
    checks += 1  # Real --terminal entrypoint on a temporary, fully bound terminal fixture.
    gone, _, gone_proof, _, _ = terminal_case("outer-gone")
    assert gone and gone_proof is not None
    checks += 1  # A collected outer unit is acceptable when the recorded cgroup leaf is absent.
    terminal_failures = (
        "attempt-mismatch", "record-request-mismatch", "record-boot-mismatch", "cleanup-missing",
        "cleanup-mode", "cleanup-symlink", "cleanup-parent-mismatch", "cleanup-request-mismatch",
        "workspace-symlink", "workspace-present", "result-socket", "result-socket-symlink",
            "result-payload", "result-mode", "receipt-mismatch", "lease-replaced-during",
            "workspace-parent-replaced", "result-parent-replaced", "result-leaf-replaced",
            "lease-parent-replaced", "receipt-parent-replaced", "outer-result-failure",
            "outer-result-missing", "deadline",
        "cgroup-root-mismatch", "cgroup-slice-mismatch", "live-descendant", "permission",
        "outer-active", "outer-wrong-invocation", "stage-loaded", "manager-error", "manager-timeout",
    )
    for fault in terminal_failures:
        accepted, reason, _, failure_receipt, manager_capture_saved = terminal_case(fault)
        assert not accepted and failure_receipt, \
            f"terminal fault {fault} was accepted or lost its failure receipt"
        checks += 1
        if fault in ("manager-error", "manager-timeout"):
            assert manager_capture_saved
            checks += 1  # Failed manager response or timeout retained raw stdout/stderr evidence.
        elif fault in ("result-socket", "result-socket-symlink"):
            assert "result_leaf_lease_handoff is present" in reason
        elif fault == "live-descendant":
            assert "expected job cgroup leaf remains" in reason
    print(f"LIVE_PROBE_SELF_TEST checks={checks} failures=0 orchestration=temporary-fixture-not-live-proof")


def lease_self_test() -> None:
    """Only a private temporary file and this process's own advisory flock."""
    import fcntl

    checks = 0
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        lease_dir = root / "lease"
        lease_dir.mkdir(mode=0o710)
        lease_dir.chmod(0o710)
        receipt_dir = root / "receipt"
        receipt_dir.mkdir(mode=0o755)
        lock_path = lease_dir / "host.lock"
        fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o640)
        try:
            os.fchmod(fd, 0o640)
            info = os.fstat(fd)
            receipt = receipt_dir / LEASE_RECEIPT_NAME
            receipt.write_text(f"device={info.st_dev}\ninode={info.st_ino}\n", encoding="ascii")
            receipt.chmod(0o444)
            receipt_dir.chmod(0o555)
            lock_line = (f"1: FLOCK  ADVISORY  WRITE {os.getpid()} "
                         f"{os.major(info.st_dev):02x}:{os.minor(info.st_dev):02x}:{info.st_ino} 0 EOF\n")
            assert len(_matching_lease_locks(lock_line.encode(), info.st_dev, info.st_ino)) == 1
            checks += 1
            for invalid in (b"", lock_line.replace("FLOCK", "POSIX").encode(),
                            lock_line.replace("1: FLOCK", "1: -> FLOCK").encode(),
                            lock_line.replace(f":{info.st_ino} ", f":{info.st_ino + 1} ").encode(),
                            lock_line.replace(" ADVISORY ", " BROKEN FIELDS ").encode(),
                            b"malformed lock record\n"):
                try:
                    _matching_lease_locks(invalid, info.st_dev, info.st_ino)
                except ProbeError:
                    checks += 1
                else:
                    raise AssertionError("invalid or absent lease lock was accepted")
            try:
                _matching_lease_locks(lock_line.encode() * (MAX_ACTIVE_LEASE_MATCHES + 1),
                                      info.st_dev, info.st_ino)
            except ProbeError:
                checks += 1
            else:
                raise AssertionError("unbounded lease locks were accepted")

            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            # /proc can be mounted from the host PID namespace in this scratch runtime.
            own_stat = (Path("/proc/self/stat")).read_bytes()
            pid = int(own_stat.split(b" ", 1)[0])
            _, group, ticks = _proc_stat_identity(own_stat, "lease fixture")
            cgroup = Path("/proc/self/cgroup").read_text(encoding="ascii").strip()
            assert cgroup.startswith("0::/")
            service_cgroup = cgroup[3:]
            invocation = "d" * 32
            props = {"ActiveState": "active", "MainPID": str(pid),
                     "InvocationID": invocation, "ControlGroup": service_cgroup}
            snapshot = {"properties": props,
                        "process": {"starttime_ticks": ticks, "process_group": group}}
            boot = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
            with patch(__name__ + ".LEASE_PARENT", str(lease_dir)), \
                 patch(__name__ + ".LEASE_RECEIPT_DIR", str(receipt_dir)), \
                 patch(__name__ + ".ROOT_UID", os.getuid()), \
                 patch(__name__ + ".ROOT_GID", os.getgid()), \
                 patch(__name__ + ".SERVICE_CGROUP", service_cgroup):
                output = root / "evidence"
                output.mkdir(mode=0o700)
                with patch(__name__ + "._systemd", return_value=(props, "fixture active service\n")):
                    result = capture_active_lease(snapshot, boot, os.getuid(), os.getgid(),
                                                  output, time.monotonic() + 80.0)
                assert any(row["fd_number"] == fd for row in result["matching_service_descriptors"])
                assert result["matching_proc_locks"] and \
                       (output / f"active-lease-service-fd-{fd}.fdinfo").exists() and \
                       (output / "active-lease-proc-locks.txt").exists()
                checks += 1

                bounded = root / "bounded"
                bounded.mkdir(mode=0o700)
                with patch(__name__ + ".MAX_ACTIVE_LEASE_FDS", 0):
                    try:
                        capture_active_lease(snapshot, boot, os.getuid(), os.getgid(),
                                             bounded, time.monotonic() + 80.0)
                    except ProbeError as exc:
                        assert "descriptor count exceeds bound" in str(exc)
                        checks += 1
                    else:
                        raise AssertionError("descriptor inventory bound was ignored")

                denied = root / "denied"
                denied.mkdir(mode=0o700)
                with patch(__name__ + ".os.scandir", side_effect=PermissionError("fixture fd denial")):
                    try:
                        capture_active_lease(snapshot, boot, os.getuid(), os.getgid(),
                                             denied, time.monotonic() + 80.0)
                    except PermissionError as exc:
                        assert "fixture fd denial" in str(exc)
                        checks += 1
                    else:
                        raise AssertionError("descriptor permission denial was ignored")

                raced = root / "raced"
                raced.mkdir(mode=0o700)
                changed = dict(props, InvocationID="e" * 32)
                with patch(__name__ + "._systemd", return_value=(changed, "fixture raced service\n")):
                    try:
                        capture_active_lease(snapshot, boot, os.getuid(), os.getgid(),
                                             raced, time.monotonic() + 80.0)
                    except ProbeError as exc:
                        assert "identity changed" in str(exc)
                        checks += 1
                    else:
                        raise AssertionError("changed active service invocation was accepted")

                fcntl.flock(fd, fcntl.LOCK_UN)
                missing = root / "missing"
                missing.mkdir(mode=0o700)
                with patch(__name__ + "._systemd", return_value=(props, "fixture active service\n")):
                    try:
                        capture_active_lease(snapshot, boot, os.getuid(), os.getgid(),
                                             missing, time.monotonic() + 80.0)
                    except ProbeError as exc:
                        assert "host lease absent" in str(exc)
                        checks += 1
                    else:
                        raise AssertionError("missing advisory host lease was accepted")
        finally:
            os.close(fd)
    print(f"LIVE_PROBE_LEASE_SELF_TEST checks={checks} failures=0 scope=own-temporary-file")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--lease-self-test", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--terminal", action="store_true")
    parser.add_argument("--check-result")
    parser.add_argument("--job", type=int)
    parser.add_argument("--attempt", type=int)
    parser.add_argument("--request-sha256")
    parser.add_argument("--baseline")
    parser.add_argument("--subject")
    parser.add_argument("--lease-device", type=int)
    parser.add_argument("--lease-inode", type=int)
    parser.add_argument("--budget-seconds", type=float, default=0)
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.lease_self_test:
        lease_self_test()
        return 0
    if args.check_result is not None:
        _fail(not args.run and not args.terminal and args.output is None,
              "--check-result cannot be combined with a probe mode or output directory")
        _fail(args.job is not None and args.job > 0 and args.request_sha256 is not None,
              "--check-result requires a positive --job and --request-sha256")
        token, full_sha = check_result_file(args.check_result, args.job, args.request_sha256)
        print(f"{token} {full_sha}")
        return 0
    if args.terminal:
        _fail(not args.run and args.output is not None and args.job is not None and
              args.attempt is not None and args.request_sha256 is not None and
              args.baseline is not None and args.subject is not None and
              args.lease_device is not None and args.lease_inode is not None,
              "--terminal requires job, attempt, request, sources, lease identity, and output")
        output = Path(args.output)
        output.mkdir(mode=0o700, parents=False, exist_ok=False)
        try:
            result = _terminal_proof(args.job, args.attempt, args.request_sha256,
                                     args.baseline, args.subject, args.lease_device,
                                     args.lease_inode, args.budget_seconds, output)
            code = 0
        except (ProbeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
            result = {"valid": False, "disposition": "TERMINAL_PROOF_INCONCLUSIVE",
                      "reason": str(exc), "lease_continuity": "unproven",
                      "full_acceptance": "pending"}
            code = 1
        _write_json_private(output, "disposition.json", result)
        print(json.dumps(result, sort_keys=True), flush=True)
        return code
    _fail(args.run and args.output is not None, "--run and --output are required")
    output = Path(args.output)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    result: dict[str, object]
    try:
        result = _run_probe(args.job, args.request_sha256, args.baseline, args.subject,
                            args.budget_seconds, output)
        result["disposition"] = "LIVE_PROBE_PASS"
        code = 0
    except (ProbeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        result = {"valid": False, "disposition": "LIVE_PROBE_INCONCLUSIVE", "reason": str(exc)}
        code = 1
    (output / "disposition.json").write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(json.dumps(result, sort_keys=True), flush=True)
    return code


if __name__ == "__main__":
    sys.modules.setdefault("issue1162_live_probe", sys.modules[__name__])
    try:
        raise SystemExit(main())
    except ProbeError as exc:
        print(f"LIVE_PROBE_INCONCLUSIVE {exc}", file=sys.stderr)
        raise SystemExit(1)
