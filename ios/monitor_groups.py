#!/usr/bin/env python3
"""Own the four independent hosted mobile fixture groups.

Each direct session-leader anchor retains its group until final dispatch. Its
private pipe reports payload status without reaping the anchor. Cancellation
has bounded TERM/KILL cleanup; the workflow retains its existing job deadline.
"""

import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time


TERM_GRACE_SECONDS = 1
KILL_REAP_SECONDS = 1
STATUS_LIMIT = 256
ROLES = ("signing", "install", "attached", "shared")
ROOT = Path(__file__).resolve().parents[1]


class GroupError(RuntimeError):
    pass


class Cancelled(Exception):
    def __init__(self, status):
        self.status = status


def cancel(signum, frame):
    raise Cancelled(128 + signum)


def keep_anchor(signum, frame):
    pass


def anchor(status_fd, command):
    # A handled signal resets to default in the exec'd payload. The anchor
    # itself survives TERM/INT until the owner dispatches final group KILL.
    signal.signal(signal.SIGINT, keep_anchor)
    signal.signal(signal.SIGTERM, keep_anchor)
    signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGINT, signal.SIGTERM})
    try:
        payload = subprocess.Popen(["/bin/bash", "-c", "set -euo pipefail\n" + command])
        status = payload.wait()
        if status < 0:
            status = 128 - status
    except OSError as error:
        print("error: payload launch failed: " + str(error), file=sys.stderr)
        status = 126
    os.write(status_fd, (json.dumps({"status": status}) + "\n").encode())
    os.close(status_fd)
    while True:
        signal.pause()


def start_group(role, command, evidence_root, groups):
    # Do not allow cancellation between child creation and saving its handle.
    blocked = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGTERM})
    read_fd = write_fd = None
    try:
        read_fd, write_fd = os.pipe()
        role_root = evidence_root / role
        working = role_root / "work"
        temporary = role_root / "tmp"
        working.mkdir(parents=True, exist_ok=True)
        temporary.mkdir(parents=True, exist_ok=True)
        environment = dict(os.environ, BUSTER_MOBILE_TEST_EVIDENCE_DIR=str(role_root),
                           BUSTER_MOBILE_TEST_REPO_ROOT=str(ROOT), TMPDIR=str(temporary))
        process = subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), "--anchor", str(write_fd), command],
            stdin=subprocess.DEVNULL, env=environment, cwd=working,
            pass_fds=(write_fd,), start_new_session=True,
        )
        groups[role] = {"process": process, "pid": process.pid, "fd": read_fd,
                        "bytes": bytearray(), "status": None}
    except OSError:
        if read_fd is not None:
            os.close(read_fd)
        raise
    finally:
        if write_fd is not None:
            os.close(write_fd)
        signal.pthread_sigmask(signal.SIG_SETMASK, blocked)


def collect_statuses(groups):
    selector = selectors.DefaultSelector()
    try:
        for role, group in groups.items():
            selector.register(group["fd"], selectors.EVENT_READ, role)
        while selector.get_map():
            for key, _ in selector.select():
                group = groups[key.data]
                chunk = os.read(key.fd, STATUS_LIMIT + 1 - len(group["bytes"]))
                group["bytes"].extend(chunk)
                if len(group["bytes"]) > STATUS_LIMIT:
                    raise GroupError(key.data + " status exceeds bound")
                if not chunk:
                    try:
                        record = json.loads(group["bytes"])
                        if set(record) != {"status"} or type(record["status"]) is not int or not 0 <= record["status"] <= 255:
                            raise ValueError("invalid payload status")
                    except (ValueError, TypeError) as error:
                        raise GroupError(key.data + " status incomplete or malformed") from error
                    group["status"] = record["status"]
                    selector.unregister(key.fd)
    finally:
        selector.close()


def close_groups(groups, term_first):
    errors = []
    if term_first and groups:
        for role, group in groups.items():
            try:
                os.killpg(group["pid"], signal.SIGTERM)
            except OSError as error:
                errors.append(role + " TERM dispatch: " + str(error))
        time.sleep(TERM_GRACE_SECONDS)
    for role, group in groups.items():
        # No poll/wait/communicate has released any direct anchor yet.
        try:
            os.killpg(group["pid"], signal.SIGKILL)
        except OSError as error:
            errors.append(role + " final KILL dispatch: " + str(error))
    deadline = time.monotonic() + KILL_REAP_SECONDS
    for role, group in groups.items():
        try:
            status = group["process"].wait(timeout=max(0, deadline - time.monotonic()))
            if status != -signal.SIGKILL:
                errors.append(role + " anchor exited before final KILL")
        except subprocess.TimeoutExpired:
            errors.append(role + " anchor reap deadline")
        os.close(group["fd"])
    return errors


def main():
    groups = {}
    result = 2
    completed = False
    cancelled = False
    previous = {signum: signal.signal(signum, cancel) for signum in (signal.SIGINT, signal.SIGTERM)}
    try:
        if len(sys.argv) != len(ROLES) + 1:
            raise GroupError("usage: monitor_groups.py SIGNING_COMMAND INSTALL_COMMAND ATTACHED_COMMAND SHARED_COMMAND")
        temporary = Path(os.environ["RUNNER_TEMP"]).resolve()
        for role, command in zip(ROLES, sys.argv[1:]):
            start_group(role, command, temporary / "mobile-lifecycle-evidence", groups)
        collect_statuses(groups)
        line = "IOS_LIFECYCLE_GROUPS " + " ".join("%s_status=%d" % (role, groups[role]["status"]) for role in ROLES) + "\n"
        print(line, end="", flush=True)
        (temporary / "ios-lifecycle-groups.log").write_text(line)
        result = 0
        for role in ROLES:
            if groups[role]["status"] and result == 0:
                result = groups[role]["status"]
        completed = True
    except Cancelled as error:
        result = error.status
        cancelled = True
    except (GroupError, KeyError, OSError) as error:
        print("error: " + str(error), file=sys.stderr)
    finally:
        # Further cancellation cannot interrupt final dispatch and bounded reap.
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        errors = close_groups(groups, term_first=not completed)
        for error in errors:
            print("error: " + error, file=sys.stderr)
        if errors and not cancelled:
            result = 2
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    return result


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--anchor":
        anchor(int(sys.argv[2]), sys.argv[3])
    else:
        raise SystemExit(main())
