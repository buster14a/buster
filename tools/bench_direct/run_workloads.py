#!/usr/bin/env python3
"""Compile and time the owner's pull-request workloads on the 9700X runner.

Run from trusted `main` by `.github/workflows/9700x-direct-bench.yml` (#2704).
The candidate checkout supplies only workload sources: single C files directly
under `benchmarks/9700x/` that the pull request added or modified. This file,
the compile command, the pinned CPU and the sample plan come from `main`.

A workload may bring one input file, `<name>.data` beside `<name>.c`, of at
most DATA_LIMIT bytes (#2769). It is copied read-only into the run directory
as `input.data` before the first run, and its digest is reported; changing
only the data file also selects its workload.

Each workload is built once with a fixed command, then started as two warmups
and nine measured fresh processes. Every run's exit state, wall and CPU time,
peak RSS and captured output is reported. The numbers are diagnostic process
latency from fork through wait4. Nothing here holds the service lease, so
other activity on the host is reported (load average) but not excluded.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import resource
import shutil
import signal
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

WORKLOAD_DIRECTORY = "benchmarks/9700x"
WORKLOAD_NAME = re.compile(r"benchmarks/9700x/[a-z0-9][a-z0-9_-]{0,47}\.c")
WORKLOAD_LIMIT = 4
SOURCE_LIMIT = 256 * 1024
DATA_LIMIT = 8 * 1024 * 1024
DATA_NAME = "input.data"
WARMUPS = 2
SAMPLES = 9
RUN_TIMEOUT_SECONDS = 10
COMPILE_TIMEOUT_SECONDS = 120
OUTPUT_FILE_LIMIT = 1024 * 1024
OUTPUT_SHOWN = 2000
COMPILE_FLAGS = (
    "-std=c11", "-O2", "-static", "-fwrapv", "-fno-strict-aliasing",
    "-funsigned-char", "-Wall", "-Wextra", "-Werror",
)
RUN_ENVIRONMENT = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}


def changed_workloads(candidate: Path, base: str, head: str) -> list[str]:
    """Workload sources the pull request added or modified, in sorted order."""
    listing = subprocess.run(
        ["git", "-C", str(candidate), "diff", "--name-only", "--diff-filter=AM", "-z",
         f"{base}...{head}", "--", WORKLOAD_DIRECTORY],
        check=True, capture_output=True, timeout=60).stdout.decode("utf-8")
    names = {name.removesuffix(".data") + ".c" if name.endswith(".data") else name
             for name in listing.split("\0") if name}
    return sorted(name for name in names if WORKLOAD_NAME.fullmatch(name))


def source_problem(candidate: Path, name: str) -> str:
    path = candidate / name
    problem = ""
    if path.is_symlink() or not path.is_file():
        problem = "not a regular file"
    elif path.stat().st_size > SOURCE_LIMIT:
        problem = f"larger than {SOURCE_LIMIT} bytes"
    data = data_path(candidate, name)
    if not problem and (data.exists() or data.is_symlink()):
        if data.is_symlink() or not data.is_file():
            problem = f"{data.name} is not a regular file"
        elif data.stat().st_size > DATA_LIMIT:
            problem = f"{data.name} is larger than {DATA_LIMIT} bytes"
    return problem


def data_path(candidate: Path, name: str) -> Path:
    """The optional input file beside a workload source."""
    return candidate / (name.removesuffix(".c") + ".data")


def prepare_child(cpu: int) -> None:
    os.setsid()
    os.sched_setaffinity(0, {cpu})
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (OUTPUT_FILE_LIMIT, OUTPUT_FILE_LIMIT))


def kill_group(pid: int, fired: list[bool]) -> None:
    fired.append(True)
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def run_once(program: Path, scratch: Path, cpu: int) -> dict:
    """Start one fresh process and return its outcome, timings and output."""
    log = scratch / "output.log"
    fired: list[bool] = []
    with open(log, "wb") as output, open(os.devnull, "rb") as nothing:
        started = time.monotonic_ns()
        child = subprocess.Popen(
            [str(program)], stdin=nothing, stdout=output, stderr=subprocess.STDOUT,
            cwd=scratch, env=RUN_ENVIRONMENT, close_fds=True,
            preexec_fn=lambda: prepare_child(cpu))
        watchdog = threading.Timer(RUN_TIMEOUT_SECONDS, kill_group, (child.pid, fired))
        watchdog.start()
        _, status, usage = os.wait4(child.pid, 0)
        finished = time.monotonic_ns()
        watchdog.cancel()
        watchdog.join()
        child.returncode = os.waitstatus_to_exitcode(status)
    # A descendant that outlived its leader would disturb the next sample.
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    text = log.read_bytes()
    log.unlink()
    return {
        "exit": child.returncode,
        "timed_out": bool(fired),
        "wall_ns": finished - started,
        "cpu_ns": int((usage.ru_utime + usage.ru_stime) * 1e9),
        "rss_bytes": usage.ru_maxrss * 1024,
        "output": text,
    }


def render(name: str, source_sha: str, program_sha: str, rows: list[dict], data_sha: str = "") -> tuple[list[str], bool]:
    measured = rows[WARMUPS:]
    passed = len(rows) == WARMUPS + SAMPLES and all(
        row["exit"] == 0 and not row["timed_out"] for row in rows)
    data = f", `{DATA_NAME}` sha256 `{data_sha}`" if data_sha else ", no input data"
    lines = [f"### `{name}`", "",
             f"source sha256 `{source_sha}`, executable sha256 `{program_sha}`{data}", ""]
    if measured:
        walls = [row["wall_ns"] for row in measured]
        lines += [
            f"Wall over {len(measured)} measured runs: median {statistics.median(walls) / 1e6:.3f} ms, "
            f"min {min(walls) / 1e6:.3f} ms, max {max(walls) / 1e6:.3f} ms. "
            f"CPU median {statistics.median(row['cpu_ns'] for row in measured) / 1e6:.3f} ms. "
            f"Peak RSS {max(row['rss_bytes'] for row in measured)} bytes.", ""]
    lines += ["| run | exit | timed out | wall ms | cpu ms | rss bytes | output sha256 |",
              "|---|---|---|---|---|---|---|"]
    for index, row in enumerate(rows):
        phase = f"warmup {index}" if index < WARMUPS else f"sample {index - WARMUPS}"
        lines.append(
            f"| {phase} | {row['exit']} | {int(row['timed_out'])} | {row['wall_ns'] / 1e6:.3f} | "
            f"{row['cpu_ns'] / 1e6:.3f} | {row['rss_bytes']} | "
            f"`{hashlib.sha256(row['output']).hexdigest()[:16]}` |")
    lines.append("")
    outputs = [row["output"] for row in rows]
    if outputs and all(output == outputs[0] for output in outputs):
        lines.append(f"All {len(rows)} runs printed identical output:")
        shown = [("every run", outputs[0])]
    else:
        lines.append("Runs printed differing output:")
        shown = [(f"run {index}", output) for index, output in enumerate(outputs)]
    for label, output in shown:
        text = output[:OUTPUT_SHOWN].decode("utf-8", "replace").replace("```", "'''")
        suffix = f"\n[{len(output) - OUTPUT_SHOWN} more bytes not shown]" if len(output) > OUTPUT_SHOWN else ""
        lines += ["", f"{label}:", "", "```text", text.rstrip("\n") + suffix, "```"]
    lines.append("")
    return lines, passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cpu", type=int, default=2)
    arguments = parser.parse_args()

    failures: list[str] = []
    for value in (arguments.base, arguments.head):
        if not re.fullmatch(r"[0-9a-f]{40}", value):
            failures.append("base and head must be full lowercase commit IDs")
    compiler = shutil.which(arguments.cc)
    if not compiler:
        failures.append(f"compiler not found: {arguments.cc}")
    if arguments.cpu not in os.sched_getaffinity(0):
        failures.append(f"CPU {arguments.cpu} is not available to this process")
    workloads = [] if failures else changed_workloads(arguments.candidate, arguments.base, arguments.head)
    if not failures and not workloads:
        failures.append(f"the pull request adds or modifies no workload matching {WORKLOAD_NAME.pattern}")
    if len(workloads) > WORKLOAD_LIMIT:
        failures.append(f"more than {WORKLOAD_LIMIT} workloads changed: {len(workloads)}")

    lines = ["## 9700X direct workload run", "",
             f"head `{arguments.head}`, base `{arguments.base}`, CPU {arguments.cpu}, "
             f"{WARMUPS} warmups and {SAMPLES} samples, {RUN_TIMEOUT_SECONDS} s limit per run.", "",
             "Diagnostic process latency (fork through wait4). No service lease is held; "
             "other host activity is not excluded.", ""]
    load_before = Path("/proc/loadavg").read_text(encoding="ascii").strip()
    for name in ([] if failures else workloads):
        problem = source_problem(arguments.candidate, name)
        if problem:
            failures.append(f"{name}: {problem}")
            continue
        scratch = arguments.work / Path(name).stem
        scratch.mkdir(parents=True)
        source = arguments.candidate / name
        program = scratch / "program"
        build = subprocess.run(
            [compiler, *COMPILE_FLAGS, "-o", str(program), str(source.resolve())],
            cwd=scratch, capture_output=True, timeout=COMPILE_TIMEOUT_SECONDS, check=False)
        if build.returncode != 0:
            failures.append(f"{name}: compilation failed")
            diagnostics = (build.stdout + build.stderr)[:OUTPUT_SHOWN].decode("utf-8", "replace")
            lines += [f"### `{name}`", "", "Compilation failed:", "", "```text",
                      diagnostics.replace("```", "'''").rstrip("\n"), "```", ""]
            continue
        data = data_path(arguments.candidate, name)
        data_sha = ""
        if data.is_file():
            shutil.copyfile(data, scratch / DATA_NAME)
            (scratch / DATA_NAME).chmod(0o444)
            data_sha = hashlib.sha256((scratch / DATA_NAME).read_bytes()).hexdigest()
        rows = [run_once(program, scratch, arguments.cpu) for _ in range(WARMUPS + SAMPLES)]
        section, passed = render(
            name, hashlib.sha256(source.read_bytes()).hexdigest(),
            hashlib.sha256(program.read_bytes()).hexdigest(), rows, data_sha)
        lines += section
        if not passed:
            failures.append(f"{name}: a run exited nonzero, was signalled or timed out")
    load_after = Path("/proc/loadavg").read_text(encoding="ascii").strip()
    lines += [f"Compiler: `{compiler}`. Flags: `{' '.join(COMPILE_FLAGS)}`.", "",
              f"Load average before: `{load_before}`; after: `{load_after}`.", ""]
    for failure in failures:
        lines.append(f"**FAILED:** {failure}")
    report = "\n".join(lines) + "\n"
    sys.stdout.write(report)
    if arguments.summary:
        with open(arguments.summary, "a", encoding="utf-8") as summary:
            summary.write(report)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
