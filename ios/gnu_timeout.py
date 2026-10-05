#!/usr/bin/env python3
"""Select the GNU timer used by iOS launcher and caller-clock controls.

select_timeout resolves one executable and verifies its bounded --version
output. The CLI prints only that absolute path on stdout; retained provider
evidence goes to stderr. Probe clocks and cleanup never trust the candidate.
CLI refusal is 1; unresolved owned-probe cleanup is fatal status 2.
"""

import json
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import subprocess
import sys
import time


CANDIDATES = ("gnutimeout", "gtimeout", "timeout")
VERSION_TIMEOUT_SECONDS = 2
VERSION_KILL_WAIT_SECONDS = 1
VERSION_OUTPUT_LIMIT = 4096
VERSION_BANNER = re.compile(r"(?:timeout|gtimeout|gnutimeout) \(GNU coreutils\) ([0-9]+(?:\.[0-9]+)+(?:-[A-Za-z0-9.]+)?)")


class TimeoutSelectionError(RuntimeError):
    pass


class ProbeCleanupError(TimeoutSelectionError):
    pass


def probe_version(path):
    output = {"stdout": bytearray(), "stderr": bytearray()}
    process = None
    selector = selectors.DefaultSelector()
    try:
        process = subprocess.Popen(
            [path, "--version"], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=dict(os.environ, LC_ALL="C", LANG="C"), start_new_session=True,
        )
        deadline = time.monotonic() + VERSION_TIMEOUT_SECONDS
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")
        selector.register(process.stderr, selectors.EVENT_READ, "stderr")
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutSelectionError("version probe deadline")
            for key, _ in selector.select(remaining):
                retained = sum(len(value) for value in output.values())
                chunk = os.read(key.fileobj.fileno(), VERSION_OUTPUT_LIMIT + 1 - retained)
                if chunk:
                    output[key.data].extend(chunk)
                    if sum(len(value) for value in output.values()) > VERSION_OUTPUT_LIMIT:
                        raise TimeoutSelectionError("version output exceeds bound")
                else:
                    selector.unregister(key.fileobj)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutSelectionError("version probe deadline")
        status = process.wait(timeout=remaining)
        if status != 0:
            raise TimeoutSelectionError("version probe exited %d" % status)
        if output["stderr"]:
            raise TimeoutSelectionError("version probe wrote stderr")
    except subprocess.TimeoutExpired as error:
        raise TimeoutSelectionError("version probe deadline") from error
    except OSError as error:
        raise TimeoutSelectionError("version probe unavailable: " + str(error)) from error
    finally:
        selector.close()
        if process is not None:
            # No poll/reap occurs before refusing a blocked pipe/probe. Its
            # unreaped direct session leader still owns this process-group ID.
            try:
                if process.returncode is None:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    except OSError as error:
                        raise ProbeCleanupError("version probe cleanup failed: " + str(error)) from error
                    try:
                        process.wait(timeout=VERSION_KILL_WAIT_SECONDS)
                    except subprocess.TimeoutExpired as error:
                        raise ProbeCleanupError("version probe cleanup deadline") from error
            finally:
                process.stdout.close()
                process.stderr.close()
    try:
        text = output["stdout"].decode("utf-8")
    except UnicodeDecodeError as error:
        raise TimeoutSelectionError("version output is not UTF-8") from error
    lines = text.splitlines()
    banner = VERSION_BANNER.fullmatch(lines[0]) if lines else None
    if banner is None or "\x00" in text or any(VERSION_BANNER.fullmatch(line) for line in lines[1:]):
        raise TimeoutSelectionError("version output is not one GNU timeout banner")
    return banner.group(1)


def select_timeout(*, search_path=None, evidence=None):
    if os.name != "posix":
        raise TimeoutSelectionError("GNU timer selection requires POSIX probe custody")
    selected = None
    failures = []
    seen = set()
    for name in CANDIDATES:
        candidate = shutil.which(name, path=search_path)
        if candidate is not None:
            try:
                path = str(Path(candidate).resolve(strict=True))
                if any(ord(character) < 32 or ord(character) == 127 for character in path):
                    raise TimeoutSelectionError("timer path contains a control character")
                if path not in seen:
                    seen.add(path)
                    version = probe_version(path)
                    selected = (path, version)
            except (TimeoutSelectionError, OSError, RuntimeError) as error:
                if isinstance(error, ProbeCleanupError):
                    raise
                failures.append(name + ": " + str(error))
        if selected is not None:
            break
    if selected is None:
        raise TimeoutSelectionError("no positively verified GNU timeout: " + ("; ".join(failures) or "no candidate found"))
    path, version = selected
    stream = sys.stderr if evidence is None else evidence
    print("BUSTER_IOS_TIMEOUT " + json.dumps({"path": path, "version": version}, sort_keys=True), file=stream)
    return path


def main():
    result = 1
    try:
        if len(sys.argv) != 1:
            raise TimeoutSelectionError("usage: gnu_timeout.py")
        print(select_timeout())
        result = 0
    except ProbeCleanupError as error:
        print("error: " + str(error), file=sys.stderr)
        result = 2
    except TimeoutSelectionError as error:
        print("error: " + str(error), file=sys.stderr)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
