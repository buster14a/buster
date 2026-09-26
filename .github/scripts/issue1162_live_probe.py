#!/usr/bin/env python3
"""Bounded, disposable #1162 live probe coordinator.

This runs only inside the hosted workflow's disposable systemd container. It
does not modify the service or queue. The one temporary payload is created
through the verified result-directory fd and removed by captured inode.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pwd
import re
import signal
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
RESULTS = STATE + "/workspaces/results"
CGROUP_ROOT = "/sys/fs/cgroup"
BENCH_SLICE_PATH = CGROUP_ROOT + "/buster.slice/buster-bench.slice"
SERVICE_CGROUP = "/system.slice/buster-bench.service"
LIVE_TEST = "/usr/local/libexec/systemd-broker-live-test"
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
HEX32 = re.compile(r"[0-9a-f]{32}\Z")


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
        stable_after = (after.st_dev, after.st_ino, after.st_mode, after.st_nlink,
                        after.st_uid, after.st_gid, after.st_size,
                        after.st_mtime_ns, after.st_ctime_ns)
        _fail(os.read(fd, 1) == b"" and stable_after == stable_before,
              f"record changed while read: {name}")
        return bytes(chunks)
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
    if unit == "buster-bench.service" or re.fullmatch(r"buster-bench-[0-9]+-[0-9]+\.service", unit):
        return "/usr/local/libexec/buster-bench-service"
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


def _open_records(job: int, request_sha: str) -> tuple[bytes, bytes, dict[str, object]]:
    service = pwd.getpwnam("buster-bench")
    queue_fd = _safe_dir(QUEUE, service.pw_uid, service.pw_gid, 0o710)
    try:
        worker = _read_at(queue_fd, f"worker-{job}", 232, service.pw_uid, service.pw_gid, 0o440)
        instance = _read_at(queue_fd, f"worker-instance-{job}", 544, service.pw_uid, service.pw_gid, 0o440)
    finally:
        os.close(queue_fd)
    boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    return worker, instance, parse_records(worker, instance, job, request_sha, boot_id)


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


def _run_probe(job: int, request_sha: str, baseline: str, subject: str,
               budget: float, output: Path) -> dict[str, object]:
    coverage = {
        "disposition": "normal-path checkpoint probe only",
        "checkpoint_discovery_window_seconds": 300,
        "captured_when_available": [
            "installed service full systemctl show and MainPID proc status/mountinfo/cgroup/executable hash",
            "exact active outer full systemctl show, invocation, MainPID, proc and cgroup identity",
            "one exact active base-build full systemctl show, invocation, MainPID, proc and cgroup identity before and after payload cleanup",
            "immutable worker and instance record bytes bound to submit request SHA-256",
            "temporary result payload metadata, inode-bound cleanup, and cleanup absence",
            "broker instance unit/process details only if still listed after the live probe",
        ],
        "not_captured_or_not_claimed": [
            "all five build-stage identities and every final build node before throughput",
            "recovery scenarios, authenticated publication/replay acceptance, or a complete deployment packet",
            "short-lived broker processes that exited before post-probe capture",
            "physical host readiness, host installation, or service/security acceptance",
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
    while time.monotonic() < discovery_deadline:
        remain = discovery_deadline - time.monotonic()
        try:
            records = _open_records(job, request_sha)
            before_outer, before_build, build_identity = _checkpoint(job, records[2], output, discovery_deadline)
            break
        except (OSError, ProbeError, subprocess.TimeoutExpired) as exc:
            last_error = str(exc)
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
            "payload_device": payload_identity[0] if payload_identity else None,
            "payload_inode": payload_identity[1] if payload_identity else None,
            "payload_absent_after_cleanup": True, "live_test_exit": probe_exit,
            "process_group_clean": process_group_clean,
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
    print(f"LIVE_PROBE_SELF_TEST checks={checks} failures=0 orchestration=temporary-fixture-not-live-proof")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--job", type=int)
    parser.add_argument("--request-sha256")
    parser.add_argument("--baseline")
    parser.add_argument("--subject")
    parser.add_argument("--budget-seconds", type=float, default=0)
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
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
    try:
        raise SystemExit(main())
    except ProbeError as exc:
        print(f"LIVE_PROBE_INCONCLUSIVE {exc}", file=sys.stderr)
        raise SystemExit(1)
