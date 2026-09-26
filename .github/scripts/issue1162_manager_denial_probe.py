#!/usr/bin/env python3
"""Bounded bare-account manager denial observation in a disposable #1162 guest.

This pre-service probe addresses only direct Manager.StartUnit authorization for
three existing guest accounts. It targets a run-bound, proved-absent plain unit
name; an unrecognized result is inconclusive, never a denial inference.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import pwd
import re
import selectors
import signal
import stat
import sys
import tempfile
import time
from types import SimpleNamespace
from unittest.mock import patch

SYSTEMCTL = "/usr/bin/systemctl"
ACTORS = (("service", "buster-bench"), ("candidate", "buster-bench-candidate"),
          ("runner", "buster-github-runner"))
MAX_OUTPUT = 32768
MAX_STATUS = 16384
MAX_UNIT_PATHS = 64
MAX_UNIT_PATH_BYTES = 16384
COMMAND_SECONDS = 8.0
TOTAL_SECONDS = 90.0
PR_SET_NO_NEW_PRIVS = 38
ENV = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "SYSTEMD_COLORS": "0",
       "SYSTEMD_PAGER": "cat", "HOME": "/nonexistent"}
UNIT_PATH_TOKEN = re.compile(r"/[A-Za-z0-9_./+\-]+\Z")
UNIT_NAME = re.compile(r"buster-bench-denial-[1-9][0-9]{0,19}-[1-9][0-9]{0,19}-(?:service|candidate|runner)\.service\Z")
DECIMAL = re.compile(r"[1-9][0-9]{0,19}\Z")
DENIED_TEXT = ("Access denied", "Interactive authentication required")


class Inconclusive(RuntimeError):
    pass


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise Inconclusive(reason)


def number(value: str) -> int:
    require(DECIMAL.fullmatch(value) is not None and int(value) <= 2**64 - 1,
            "run identity is missing, noncanonical, or outside u64")
    return int(value)


def unit_name(run_id: int, run_attempt: int, actor: str) -> str:
    require(actor in dict(ACTORS) and run_id > 0 and run_attempt > 0,
            "invalid fixed manager-denial unit identity")
    name = f"buster-bench-denial-{run_id}-{run_attempt}-{actor}.service"
    require(len(name.encode("ascii")) <= 200 and UNIT_NAME.fullmatch(name) is not None,
            "manager-denial unit name exceeds bound or is templated")
    return name


def _deadline(deadline: float) -> float:
    remaining = deadline - time.monotonic()
    require(remaining > 0, "manager-denial deadline exhausted")
    return min(COMMAND_SECONDS, remaining)


def _child_exec(account: pwd.struct_passwd | None, argv: tuple[str, ...],
                stdout_write: int, stderr_write: int, status_write: int) -> None:
    # The parent never waits for exec; even a blocked initgroups is deadline-bound.
    try:
        os.setsid()
        null = os.open("/dev/null", os.O_RDONLY | os.O_CLOEXEC)
        os.dup2(null, 0)
        os.close(null)
        os.dup2(stdout_write, 1)
        os.dup2(stderr_write, 2)
        if account is not None:
            os.initgroups(account.pw_name, account.pw_gid)
            os.setresgid(account.pw_gid, account.pw_gid, account.pw_gid)
            os.setresuid(account.pw_uid, account.pw_uid, account.pw_uid)
            libc = ctypes.CDLL(None, use_errno=True)
            if libc.prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0:
                os._exit(126)
            with open("/proc/self/status", "rb") as source:
                raw = source.read(MAX_STATUS + 1)
            if not raw or len(raw) > MAX_STATUS:
                os._exit(126)
            view = memoryview(raw)
            while view:
                written = os.write(status_write, view)
                if written <= 0:
                    os._exit(126)
                view = view[written:]
        os.close(status_write)
        os.execve(SYSTEMCTL, argv, ENV)
    except BaseException:
        os._exit(126)


def run_fixed(account: pwd.struct_passwd | None, arguments: tuple[str, ...],
              deadline: float) -> dict:
    """Fixed systemctl argv, bounded pipes, and one owned process group."""
    unit_show = (len(arguments) == 10 and arguments[:9] ==
                 ("show", "-p", "Id", "-p", "LoadState", "-p", "FragmentPath", "-p", "Transient") and
                 UNIT_NAME.fullmatch(arguments[9]) is not None)
    unit_start = (len(arguments) == 2 and arguments[0] == "start" and
                  UNIT_NAME.fullmatch(arguments[1]) is not None)
    require(arguments in (("show", "-p", "Version", "-p", "UnitPath"),
                          ("show", "-p", "Version")) or unit_show or unit_start,
            "nonfixed systemctl operation")
    argv = (SYSTEMCTL, "--system", "--no-ask-password", "--no-pager", *arguments)
    stdout_read = stdout_write = stderr_read = stderr_write = -1
    status_read = status_write = -1
    pid = -1
    reaped = False
    selector = selectors.DefaultSelector()
    streams = {"stdout": bytearray(), "stderr": bytearray(), "proc_status": bytearray()}
    try:
        stdout_read, stdout_write = os.pipe2(os.O_CLOEXEC)
        stderr_read, stderr_write = os.pipe2(os.O_CLOEXEC)
        status_read, status_write = os.pipe2(os.O_CLOEXEC)
        pid = os.fork()
        if pid == 0:
            for fd in (stdout_read, stderr_read, status_read):
                os.close(fd)
            _child_exec(account, argv, stdout_write, stderr_write, status_write)
            os._exit(126)
        for fd in (stdout_write, stderr_write, status_write):
            os.close(fd)
        stdout_write = stderr_write = status_write = -1
        for label, fd in (("stdout", stdout_read), ("stderr", stderr_read),
                          ("proc_status", status_read)):
            os.set_blocking(fd, False)
            selector.register(fd, selectors.EVENT_READ, label)
        until = time.monotonic() + _deadline(deadline)
        while selector.get_map():
            remaining = until - time.monotonic()
            if remaining <= 0:
                raise Inconclusive("fixed systemctl command timed out")
            for event, _ in selector.select(min(0.25, remaining)):
                label = event.data
                block = os.read(event.fd, 4096)
                if not block:
                    selector.unregister(event.fd)
                else:
                    streams[label].extend(block)
                    require(len(streams["stdout"]) + len(streams["stderr"]) <= MAX_OUTPUT and
                            len(streams["proc_status"]) <= MAX_STATUS,
                            "fixed systemctl output or child status exceeds bound")
        while True:
            waited, raw_status = os.waitpid(pid, os.WNOHANG)
            if waited:
                reaped = True
                break
            require(time.monotonic() < until and time.monotonic() < deadline,
                    "fixed systemctl command timed out")
            time.sleep(0.025)
        require(os.WIFEXITED(raw_status), "fixed systemctl command did not exit normally")
        if account is not None:
            require(bool(streams["proc_status"]), "child credential status missing")
        else:
            require(not streams["proc_status"], "root child unexpectedly wrote credential status")
        return {"argv": list(argv), "exit": os.WEXITSTATUS(raw_status),
                "stdout": streams["stdout"].decode("utf-8", "replace"),
                "stderr": streams["stderr"].decode("utf-8", "replace"),
                "proc_status": bytes(streams["proc_status"]).decode("ascii", "replace"),
                "timed_out": False}
    finally:
        selector.close()
        if pid > 0 and not reaped:
            try:
                if os.getpgid(pid) == pid:
                    os.killpg(pid, signal.SIGKILL)
                else:
                    os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)
        for fd in (stdout_read, stdout_write, stderr_read, stderr_write,
                   status_read, status_write):
            if fd >= 0:
                os.close(fd)


def properties(result: dict, keys: tuple[str, ...]) -> dict[str, str]:
    require(result["exit"] == 0 and not result["stderr"] and
            len(result["stdout"].encode()) <= MAX_OUTPUT, "manager read-only show failed")
    values: dict[str, str] = {}
    for line in result["stdout"].splitlines():
        key, separator, value = line.partition("=")
        require(separator == "=" and key in keys and key not in values,
                "manager property response malformed or duplicated")
        values[key] = value
    require(set(values) == set(keys), "manager property response missing an exact field")
    return values


def manager_state(result: dict) -> dict:
    values = properties(result, ("Version", "UnitPath"))
    require(re.fullmatch(r"[0-9][!-~]{0,127}", values["Version"]) is not None,
            "manager Version missing or malformed")
    raw = values["UnitPath"]
    require(0 < len(raw.encode()) <= MAX_UNIT_PATH_BYTES and
            not any(char in raw for char in (":", "'", '"', "\\")) and
            all(ord(char) >= 0x20 and ord(char) < 0x7f for char in raw),
            "manager UnitPath rendering missing or malformed")
    paths = raw.split()
    require(0 < len(paths) <= MAX_UNIT_PATHS and len(paths) == len(set(paths)) and
            all(UNIT_PATH_TOKEN.fullmatch(path) is not None and
                all(part not in ("", ".", "..") for part in path.split("/")[1:])
                for path in paths), "manager UnitPath has an unsafe or ambiguous entry")
    return {"version": values["Version"], "unit_paths": paths}


def _open_directory_nofollow(path: str) -> int:
    current = os.open("/", os.O_PATH | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in path.split("/")[1:]:
            next_fd = os.open(part, os.O_PATH | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                              dir_fd=current)
            os.close(current)
            current = next_fd
        result = current
        current = -1
        return result
    finally:
        if current >= 0:
            os.close(current)


def unit_path_absence(unit: str, paths: list[str]) -> list[dict]:
    observations = []
    for path in paths:
        try:
            parent = _open_directory_nofollow(path)
        except FileNotFoundError:
            observations.append({"path": path, "directory": "absent"})
            continue
        try:
            before = os.fstat(parent)
            try:
                os.stat(unit, dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise Inconclusive(f"fixed unit name exists in manager UnitPath: {path}")
            after = os.fstat(parent)
            reopened = _open_directory_nofollow(path)
            try:
                current = os.fstat(reopened)
                require((before.st_dev, before.st_ino) == (after.st_dev, after.st_ino) ==
                        (current.st_dev, current.st_ino),
                        f"manager UnitPath directory raced: {path}")
                observations.append({"path": path, "entry": "absent",
                                     "directory_dev": before.st_dev,
                                     "directory_ino": before.st_ino})
            finally:
                os.close(reopened)
        finally:
            os.close(parent)
    return observations


def absent_unit(result: dict, unit: str, paths: list[str]) -> dict:
    values = properties(result, ("Id", "LoadState", "FragmentPath", "Transient"))
    require(values == {"Id": unit, "LoadState": "not-found", "FragmentPath": "", "Transient": "no"},
            "fixed unit name is loaded, transient, aliased, or has a fragment")
    return {"properties": values, "unit_paths": unit_path_absence(unit, paths)}


def status_identity(raw: str, account: pwd.struct_passwd, expected_groups: set[int]) -> dict:
    require(0 < len(raw.encode()) <= MAX_STATUS and "\x00" not in raw,
            "child credential status absent or oversized")
    fields = {}
    for line in raw.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            require(key not in fields, "duplicate child proc status field")
            fields[key] = value.strip()
    for key, expected in (("Uid", account.pw_uid), ("Gid", account.pw_gid)):
        values = fields.get(key, "").split()
        require(len(values) == 4 and all(value.isdecimal() and int(value) == expected for value in values),
                f"child {key} is not the exact dropped identity")
    groups = fields.get("Groups", "").split()
    require(bool(groups) and all(value.isdecimal() for value in groups) and
            set(map(int, groups)) == expected_groups,
            "child supplementary groups differ from initgroups")
    require(fields.get("NoNewPrivs") == "1" and
            re.fullmatch(r"[0-9a-fA-F]+", fields.get("CapEff", "")) is not None and
            int(fields["CapEff"], 16) == 0, "child NNP or effective capabilities missing")
    return {"uid": account.pw_uid, "gid": account.pw_gid,
            "groups": sorted(expected_groups), "no_new_privs": 1, "cap_eff": 0}


def denial(result: dict, unit: str) -> str:
    output = result["stderr"]
    lines = output.splitlines()
    expected = f"Failed to start {unit}: "
    require(result["exit"] != 0 and result["exit"] is not None and not result["timed_out"] and
            UNIT_NAME.fullmatch(unit) is not None and len(lines) == 1 and
            lines[0].startswith(expected) and lines[0][len(expected):].rstrip(".") in DENIED_TEXT and
            not result["stdout"].strip(),
            "direct manager result is not an explicit authorization denial")
    return "AccessDenied" if "Access denied" in output else "InteractiveAuthenticationRequired"


def _manager_command() -> tuple[str, ...]:
    return ("show", "-p", "Version", "-p", "UnitPath")


def _unit_command(unit: str) -> tuple[str, ...]:
    return ("show", "-p", "Id", "-p", "LoadState", "-p", "FragmentPath", "-p", "Transient", unit)


def observe(run_id: int, run_attempt: int, report: dict | None = None) -> dict:
    if report is None:
        report = {"verdict": "INCONCLUSIVE", "run_id": run_id,
                  "run_attempt": run_attempt, "actors": {}}
    require(os.geteuid() == 0, "manager denial probe requires guest root")
    info = os.stat(SYSTEMCTL, follow_symlinks=False)
    require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1 and
            info.st_mode & 0o111 and info.st_mode & 0o6022 == 0,
            "fixed systemctl executable identity is unsafe")
    deadline = time.monotonic() + TOTAL_SECONDS
    state = manager_state(run_fixed(None, _manager_command(), deadline))
    service_gid = pwd.getpwnam("buster-bench").pw_gid
    report.update({"manager": state, "actors": {}, "limitations": [
        "bare-account pre-service policy only; stage namespaces and live units are not tested"]})
    for label, username in ACTORS:
        account = pwd.getpwnam(username)
        require(account.pw_uid not in (0,) and account.pw_gid not in (0,) and
                account.pw_name == username, f"invalid guest account identity: {username}")
        expected_groups = set(os.getgrouplist(username, account.pw_gid))
        require(0 < len(expected_groups) <= 128 and account.pw_gid in expected_groups and
                (label == "service" or service_gid not in expected_groups),
                f"unexpected supplementary group authority: {username}")
        unit = unit_name(run_id, run_attempt, label)
        record = {"unit": unit}
        report["actors"][label] = record
        pre = absent_unit(run_fixed(None, _unit_command(unit), deadline), unit, state["unit_paths"])
        record["pre"] = pre
        version_result = run_fixed(account, ("show", "-p", "Version"), deadline)
        child = status_identity(version_result["proc_status"], account, expected_groups)
        version = properties(version_result, ("Version",))["Version"]
        require(version == state["version"], "same-principal read-only manager bus visibility missing")
        record.update({"read_only_version": version, "read_only_child": child})
        start_result = run_fixed(account, ("start", unit), deadline)
        start_child = status_identity(start_result["proc_status"], account, expected_groups)
        record.update({"start_child": start_child, "start_exit": start_result["exit"],
                       "start_stderr": start_result["stderr"][:4096]})
        after_state = manager_state(run_fixed(None, _manager_command(), deadline))
        require(after_state == state, "manager version or UnitPath changed across denial observation")
        post = absent_unit(run_fixed(None, _unit_command(unit), deadline), unit, state["unit_paths"])
        record["post"] = post
        record["explicit_denial"] = denial(start_result, unit)
    report["verdict"] = "BARE_ACCOUNT_MANAGER_DENIAL_OBSERVED"
    return report


def _write_private(output: str, report: dict) -> None:
    fd = _open_directory_nofollow(output)
    try:
        info = os.fstat(fd)
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and stat.S_IMODE(info.st_mode) == 0o700,
                "manager-denial output directory is not private root-owned")
        content = (json.dumps(report, sort_keys=True, indent=2) + "\n").encode()
        require(len(content) <= 256 * 1024, "manager-denial report exceeds bound")
        result = os.open("manager-denial-observation.json", os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         os.O_CLOEXEC | os.O_NOFOLLOW, 0o600, dir_fd=fd)
        try:
            view = memoryview(content)
            while view:
                count = os.write(result, view)
                require(count > 0, "short manager-denial evidence write")
                view = view[count:]
            os.fsync(result)
        finally:
            os.close(result)
    finally:
        os.close(fd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--run-id")
    parser.add_argument("--run-attempt")
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    require(args.run_id and args.run_attempt and args.output and args.output.startswith("/") and
            all(part not in ("", ".", "..") for part in args.output.split("/")[1:]),
            "run identity and canonical absolute output are required")
    report = {"verdict": "INCONCLUSIVE", "run_id": args.run_id,
              "run_attempt": args.run_attempt}
    try:
        observe(number(args.run_id), number(args.run_attempt), report)
    except (Inconclusive, OSError, ValueError, KeyError) as exc:
        report["reason"] = str(exc)[:1024]
    _write_private(args.output, report)
    print(report["verdict"], report.get("reason", ""), flush=True)
    return 0 if report["verdict"] == "BARE_ACCOUNT_MANAGER_DENIAL_OBSERVED" else 1


def self_test() -> None:
    checks = 0
    def reject(callback, phrase: str) -> None:
        nonlocal checks
        try:
            callback()
        except (Inconclusive, OSError, ValueError) as exc:
            assert phrase in str(exc), (phrase, str(exc))
        else:
            raise AssertionError(f"expected rejection: {phrase}")
        checks += 1
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        path1, path2 = root / "unitpath1", root / "unitpath2"
        path1.mkdir()
        path2.mkdir()
        unit = unit_name(123, 2, "runner")
        manager = {"exit": 0, "stdout": f"Version=255.4\nUnitPath={path1} {path2}\n", "stderr": ""}
        state = manager_state(manager)
        assert state["unit_paths"] == [str(path1), str(path2)]
        checks += 1
        unit_show = {"exit": 0, "stdout": f"Id={unit}\nLoadState=not-found\nFragmentPath=\nTransient=no\n",
                     "stderr": ""}
        assert len(absent_unit(unit_show, unit, state["unit_paths"])["unit_paths"]) == 2
        checks += 1
        for bad in ("UnitPath=/a:/b", "UnitPath='/a'", "UnitPath=/a\\x20/b",
                    "UnitPath=/a/../b", "UnitPath=", "UnitPath=relative"):
            reject(lambda bad=bad: manager_state({**manager, "stdout": "Version=255\n" + bad + "\n"}),
                   "UnitPath")
        (path2 / unit).write_bytes(b"planted")
        reject(lambda: absent_unit(unit_show, unit, state["unit_paths"]), "exists in manager UnitPath")
        (path2 / unit).unlink()
        (path2 / unit).symlink_to("nonexistent")
        reject(lambda: absent_unit(unit_show, unit, state["unit_paths"]), "exists in manager UnitPath")
        (path2 / unit).unlink()
        (path1 / "alias").symlink_to(path2, target_is_directory=True)
        reject(lambda: unit_path_absence(unit, [str(path1 / "alias")]), "Not a directory")
        original_open = _open_directory_nofollow
        open_calls = []
        def raced_open(path):
            open_calls.append(path)
            return original_open(str(path1 if len(open_calls) == 1 else path2))
        with patch(__name__ + "._open_directory_nofollow", side_effect=raced_open):
            reject(lambda: unit_path_absence(unit, [str(path1)]), "directory raced")
        reject(lambda: absent_unit({**unit_show, "stdout": unit_show["stdout"].replace(
            "LoadState=not-found", "LoadState=loaded")}, unit, state["unit_paths"]), "loaded")
        reject(lambda: absent_unit({**unit_show, "stdout": unit_show["stdout"].replace(
            "Transient=no", "Transient=yes")}, unit, state["unit_paths"]), "transient")
        account = SimpleNamespace(pw_uid=65002, pw_gid=65002)
        status = ("Uid:\t65002 65002 65002 65002\nGid:\t65002 65002 65002 65002\n"
                  "Groups:\t65002\nCapEff:\t0000000000000000\nNoNewPrivs:\t1\n")
        assert status_identity(status, account, {65002})["no_new_privs"] == 1
        checks += 1
        reject(lambda: status_identity(status.replace("NoNewPrivs:\t1", "NoNewPrivs:\t0"),
                                       account, {65002}), "NNP")
        reject(lambda: status_identity(status.replace("Uid:\t65002", "Uid:\t65003"),
                                       account, {65002}), "Uid")
        reject(lambda: status_identity(status.replace("Gid:\t65002", "Gid:\t65003"),
                                       account, {65002}), "Gid")
        reject(lambda: status_identity(status.replace("CapEff:\t0000000000000000",
                                                     "CapEff:\t0000000000000001"),
                                       account, {65002}), "NNP")
        reject(lambda: status_identity(status, account, {65002, 65000}), "supplementary")
        deny = {"exit": 1, "stdout": "", "stderr": f"Failed to start {unit}: Access denied\n",
                "timed_out": False}
        assert denial(deny, unit) == "AccessDenied"
        checks += 1
        assert denial({**deny, "stderr": f"Failed to start {unit}: Interactive authentication required\n"}, unit) == \
               "InteractiveAuthenticationRequired"
        checks += 1
        for error in ("Unit not found", "Failed to connect to bus: Access denied",
                      "generic failure", f"Failed to start {unit}: Access denied; UnitNotFound",
                      "Failed to start other.service: Access denied"):
            reject(lambda error=error: denial({**deny, "stderr": error}, unit), "not an explicit")
        reject(lambda: denial({**deny, "exit": 0}, unit), "not an explicit")
        for bad in ("0", "01", "-1", "1;touch /tmp/x", str(2**64)):
            reject(lambda bad=bad: number(bad), "run identity")
        executable = root / "systemctl"
        executable.write_text("#!/bin/sh\nprintf 'Version=255.4\\n'\n", encoding="ascii")
        executable.chmod(0o755)
        with patch(__name__ + ".SYSTEMCTL", str(executable)):
            actual = run_fixed(None, ("show", "-p", "Version"), time.monotonic() + 2)
            assert actual["exit"] == 0 and actual["stdout"] == "Version=255.4\n"
            checks += 1
            with open("/proc/self/status", encoding="ascii") as source:
                effective = next(int(line.split()[1], 16) for line in source
                                 if line.startswith("CapEff:"))
            if os.geteuid() == 0 and effective & ((1 << 6) | (1 << 7)) == ((1 << 6) | (1 << 7)):
                # Exercise the real fork/drop/status path without contacting a manager.
                fixture_account = pwd.getpwnam("nobody")
                root.chmod(0o755)
                dropped = run_fixed(fixture_account, ("show", "-p", "Version"),
                                    time.monotonic() + 2)
                assert dropped["exit"] == 0 and dropped["stdout"] == "Version=255.4\n"
                expected = set(os.getgrouplist(fixture_account.pw_name, fixture_account.pw_gid))
                assert status_identity(dropped["proc_status"], fixture_account, expected)["cap_eff"] == 0
                checks += 1
            reject(lambda: run_fixed(None, ("start", "other.service"), time.monotonic() + 2),
                   "nonfixed systemctl operation")
            executable.write_text("#!/bin/sh\nsleep 3\n", encoding="ascii")
            with patch(__name__ + ".COMMAND_SECONDS", 0.05):
                reject(lambda: run_fixed(None, ("show", "-p", "Version"),
                                         time.monotonic() + 2), "timed out")
            executable.write_text("#!/bin/sh\nhead -c 40000 /dev/zero\n", encoding="ascii")
            reject(lambda: run_fixed(None, ("show", "-p", "Version"),
                                     time.monotonic() + 2), "exceeds bound")
        # Exercise the production orchestration with fixed command fixtures;
        # no manager endpoint is touched by this self-test.
        actors = {name: SimpleNamespace(pw_name=name, pw_uid=65000 + index,
                                        pw_gid=65000 + index)
                  for index, (_, name) in enumerate(ACTORS)}
        fake_status = lambda uid, gid: (f"Uid:\t{uid} {uid} {uid} {uid}\n"
                                         f"Gid:\t{gid} {gid} {gid} {gid}\n"
                                         f"Groups:\t{gid}\nCapEff:\t0\nNoNewPrivs:\t1\n")
        real_systemctl_stat = os.stat
        def fixture_stat(owner_uid: int):
            def stat_for_fixture(path, *args, **kwargs):
                info = real_systemctl_stat(path, *args, **kwargs)
                if os.fspath(path) == str(executable) and kwargs.get("follow_symlinks") is False:
                    return SimpleNamespace(st_mode=info.st_mode, st_uid=owner_uid,
                                            st_nlink=info.st_nlink)
                return info
            return stat_for_fixture

        def run_orchestration_checks(command_mock, commands, fake_command) -> None:
            nonlocal checks
            report = observe(123, 2)
            assert report["verdict"] == "BARE_ACCOUNT_MANAGER_DENIAL_OBSERVED"
            assert len(commands) == 1 + 3 * 5
            checks += 1
            def no_unit(account, arguments, deadline):
                result = fake_command(account, arguments, deadline)
                return {**result, "stderr": "Unit not found\n"} if arguments[0] == "start" else result
            command_mock.side_effect = no_unit
            reject(lambda: observe(123, 2), "not an explicit authorization denial")
            command_mock.side_effect = fake_command
            def changed_path(account, arguments, deadline):
                result = fake_command(account, arguments, deadline)
                if arguments == _manager_command() and account is None:
                    changed_path.calls += 1
                    if changed_path.calls == 2:
                        return {**result, "stdout": f"Version=255.4\nUnitPath={path1}\n"}
                return result
            changed_path.calls = 0
            command_mock.side_effect = changed_path
            reject(lambda: observe(123, 2), "UnitPath changed")
            command_mock.side_effect = fake_command
            executable.chmod(0o777)
            reject(lambda: observe(123, 2), "executable identity is unsafe")
            executable.chmod(0o755)
            executable.unlink()
            executable.symlink_to(path1 / "nonexistent")
            reject(lambda: observe(123, 2), "executable identity is unsafe")

        runner_owner_uid = real_systemctl_stat(executable, follow_symlinks=False).st_uid
        if runner_owner_uid == 0:
            runner_owner_uid = 12345
        require(runner_owner_uid != 0, "runner-owned fixture UID was not simulated")
        for owner_uid in (0, runner_owner_uid):
            try:
                executable.unlink()
            except FileNotFoundError:
                pass
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
            executable.chmod(0o755)
            commands = []
            def fake_command(account, arguments, deadline):
                commands.append((account.pw_name if account else "root", arguments))
                if arguments == _manager_command():
                    return manager
                if arguments[0] == "show" and arguments[-1].endswith(".service"):
                    return {**unit_show, "stdout": unit_show["stdout"].replace(unit, arguments[-1])}
                if arguments == ("show", "-p", "Version"):
                    return {"exit": 0, "stdout": "Version=255.4\n", "stderr": "",
                            "proc_status": fake_status(account.pw_uid, account.pw_gid)}
                assert arguments[0] == "start" and account is not None
                return {**deny, "stderr": f"Failed to start {arguments[1]}: Access denied\n",
                        "proc_status": fake_status(account.pw_uid, account.pw_gid)}
            with patch("os.stat", side_effect=fixture_stat(owner_uid)):
                observed_owner = os.stat(executable, follow_symlinks=False).st_uid
                assert observed_owner == owner_uid
                checks += 1
                with patch(__name__ + ".run_fixed", side_effect=fake_command) as command_mock, \
                     patch(__name__ + ".SYSTEMCTL", str(executable)), \
                     patch("os.geteuid", return_value=0), \
                     patch.object(pwd, "getpwnam", side_effect=lambda name: actors[name]), \
                     patch("os.getgrouplist", side_effect=lambda name, gid: [gid]):
                    if owner_uid == 0:
                        run_orchestration_checks(command_mock, commands, fake_command)
                    else:
                        reject(lambda: observe(123, 2),
                               "fixed systemctl executable identity is unsafe")
                        # Adapt the runner-owned fixture to the root-owned
                        # executable view expected inside the disposable guest.
                        # The adapter stays active for every positive and
                        # negative orchestration control.
                        with patch("os.stat", side_effect=fixture_stat(0)):
                            run_orchestration_checks(command_mock, commands, fake_command)
    print(f"MANAGER_DENIAL_SELF_TEST checks={checks} failures=0 fixtures-only-not-live-proof")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Inconclusive as exc:
        print(f"MANAGER_DENIAL_INCONCLUSIVE {exc}", file=sys.stderr)
        raise SystemExit(1)
