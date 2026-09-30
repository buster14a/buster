#!/usr/bin/env python3
"""Run the desktop workflow-tool regression suites concurrently.

The suites are independent: each builds its fixtures under its own temporary
directory and reads the checkout without writing it. Most of their wall time
on hosted Windows is waiting on child processes (a clang build of build.c,
Windows PowerShell starts), not Python CPU time, so a few lanes overlap it.

Every suite still runs as its own `python SUITE -v` process with every
assertion. Its combined output goes to LOG_DIRECTORY/LOG while it runs and is
echoed in one block, followed by SUITE_END, when it finishes, so logs never
interleave. A failure does not stop the other suites; the runner exits with
the first failing suite's status in command-line order. SUITE_RUNNING lines
name the suites still in flight, so a step timeout shows what was running.

Entry points: run() for callers and tests, main() for the workflow step.
"""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

# The hosted desktop runners have three (macOS) or four vCPUs, and the suites'
# own children (clang, powershell.exe) need some of them.
DEFAULT_JOBS = 3
HEARTBEAT_SECONDS = 30


def utc_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def exit_status(returncode):
    # Bash truncates statuses to eight bits and signals are negative on POSIX.
    return returncode if 0 < returncode < 256 else 1


def default_jobs(environment):
    value = environment.get("BUSTER_TEST_JOBS", "")
    return int(value) if value.isdigit() else (DEFAULT_JOBS if not value else 0)


def parse_suite(text):
    suite, separator, log = text.partition("=")
    if not separator or not suite or not log or "/" in log or "\\" in log:
        raise argparse.ArgumentTypeError(f"expected SUITE=LOG with a plain log file name: {text!r}")
    return suite, log


def run(suites, log_directory, jobs, python=sys.executable, cwd=None, output=sys.stdout, heartbeat=HEARTBEAT_SECONDS):
    """Run (suite, log) pairs in at most `jobs` lanes; return the exit status."""
    log_directory = Path(log_directory)
    log_directory.mkdir(parents=True, exist_ok=True)
    lock = threading.Lock()
    pending = queue.Queue()
    for index, entry in enumerate(suites):
        pending.put((index, entry))
    running = {}
    results = [None] * len(suites)
    started_all = time.monotonic()

    def emit(text):
        with lock:
            output.write(text)
            output.flush()

    def lane():
        while True:
            try:
                index, (suite, log) = pending.get_nowait()
            except queue.Empty:
                break
            path = log_directory / log
            started = time.monotonic()
            with lock:
                running[suite] = started
                output.write(f"SUITE_START path={suite} at={utc_now()}\n")
                output.flush()
            try:
                with path.open("wb") as handle:
                    returncode = subprocess.run([python, suite, "-v"], cwd=cwd, stdin=subprocess.DEVNULL,
                                                stdout=handle, stderr=subprocess.STDOUT).returncode
            except OSError as error:
                with path.open("a", encoding="utf-8") as handle:
                    handle.write(f"failed to start {suite}: {error}\n")
                returncode = 1
            elapsed = int(time.monotonic() - started)
            results[index] = (suite, returncode, elapsed)
            text = path.read_text(encoding="utf-8", errors="replace")
            if text and not text.endswith("\n"):
                text += "\n"
            if returncode == 0:
                end = f"SUITE_END path={suite} result=success elapsed_seconds={elapsed} at={utc_now()}\n"
            else:
                end = (f"SUITE_END path={suite} result=failure status={exit_status(returncode)} "
                       f"elapsed_seconds={elapsed} at={utc_now()}\n")
            with lock:
                del running[suite]
                output.write(text + end)
                output.flush()

    emit(f"WORKFLOW_TOOLS_START suites={len(suites)} jobs={jobs} at={utc_now()}\n")
    threads = [threading.Thread(target=lane, daemon=True) for _ in range(min(jobs, len(suites)))]
    for thread in threads:
        thread.start()
    for thread in threads:
        while thread.is_alive():
            thread.join(heartbeat)
            if thread.is_alive():
                now = time.monotonic()
                with lock:
                    in_flight = sorted(running.items(), key=lambda item: item[1])
                    for suite, started in in_flight:
                        output.write(f"SUITE_RUNNING path={suite} elapsed_seconds={int(now - started)}\n")
                    output.flush()
    elapsed = int(time.monotonic() - started_all)
    failures = [(suite, returncode) for suite, returncode, _ in results if returncode != 0]
    for suite, returncode, seconds in sorted(results, key=lambda result: -result[2]):
        emit(f"SUITE_DURATION seconds={seconds} result={'success' if returncode == 0 else 'failure'} path={suite}\n")
    if failures:
        emit(f"WORKFLOW_TOOLS_END result=failure failed={','.join(suite for suite, _ in failures)} "
             f"elapsed_seconds={elapsed} at={utc_now()}\n")
        status = exit_status(failures[0][1])
    else:
        emit(f"WORKFLOW_TOOLS_END result=success elapsed_seconds={elapsed} at={utc_now()}\n")
        status = 0
    return status


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--log-directory", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=None,
                        help=f"concurrent suites (default: BUSTER_TEST_JOBS, else {DEFAULT_JOBS})")
    parser.add_argument("suites", nargs="+", type=parse_suite, metavar="SUITE=LOG")
    arguments = parser.parse_args(argv)
    jobs = arguments.jobs if arguments.jobs is not None else default_jobs(os.environ)
    if jobs < 1:
        parser.error("--jobs and BUSTER_TEST_JOBS must be at least 1")
    names = [suite for suite, _ in arguments.suites]
    logs = [log for _, log in arguments.suites]
    if len(set(names)) != len(names) or len(set(logs)) != len(logs):
        parser.error("suites and log names must be unique")
    return run(arguments.suites, arguments.log_directory, jobs)


if __name__ == "__main__":
    sys.exit(main())
