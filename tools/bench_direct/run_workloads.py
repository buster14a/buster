#!/usr/bin/env python3
"""Compile and time the owner's pull-request workloads on the 9700X runner.

Run from trusted `main` by `.github/workflows/9700x-direct-bench.yml` (#2704).
The candidate checkout supplies only workload sources: single C files directly
under `benchmarks/9700x/` that the pull request added or modified. Each is
compiled from a staged copy of that one file, so only system headers resolve;
a quoted include of a neighbouring header fails to compile (#2935). This file,
the compile command, the pinned CPU and the sample plan come from `main`.

Captured output (stdout and stderr together) keeps the first OUTPUT_CAPTURE_LIMIT
bytes per run; files a run writes are bounded separately (SCRATCH_FILE_LIMIT per
file, SCRATCH_TOTAL_LIMIT per run directory) and a violation invalidates the run
(#2936).

The source and executable digests are taken before the first run. The
executable is re-hashed after every run (outside the timed interval, which also
keeps it in the page cache) and the source once more at the end; a change marks
the affected runs invalid, stops further runs and reports both identities
(#2941). This detects changes by the same account; it does not prevent them.

A workload may bring one input file, `<name>.data` beside `<name>.c`, of at
most DATA_LIMIT bytes (#2769). It is copied read-only into the run directory
as `input.data` into a fresh directory for every warmup and sample, and its
digest is reported; changing only the data file also selects its workload.
A run that modifies or removes its `input.data` is reported invalid, and files
a run leaves behind are deleted with its directory. This resets the
filesystem state only; the OS page cache is not made cold (#2900).

Each workload is built once with a fixed command, then started as two warmups
and nine measured fresh processes. Every run's exit state, wall and CPU time,
peak RSS and captured output is reported. The numbers are diagnostic process
latency from fork through wait4. Nothing here holds the service lease, so
other activity on the host is reported (load average) but not excluded.

A SIGINT, SIGTERM or SIGHUP (a cancelled or timed-out job) takes the path of a
failing stage: the completed runs stay in the report, the interrupted stage is
named, the workloads that did not start are listed as NOT RUN, and the exit
status is 128 plus the signal number. The handlers exist only while `main`
runs. The workload and compiler process groups are killed and reaped on every
exit path, so nothing started here outlives the runner. A signal that arrives
while a report piece is being written is held until the write is complete. Each
planned workload, stage and finished run is also appended to
`<work>/progress.log` (bounded by PROGRESS_LOG_LIMIT, fsynced per line) and
echoed to stderr, so SIGKILL, which cannot be handled, still leaves the
completed samples on disk and in the job log (#2902).

The observed CPU model, read from the kernel and never from a runner label,
heads the report (#2761). On any host other than the approved Zen 5 host
nothing is compiled or run and the run fails, so a workload can only be
reported as measured on the Ryzen 7 9700X.

Map: changed_workloads, source_problem, SignalGuard, Progress, compile_bounded, run_once, run_sample,
render, Reporter, measure_workload, run_plan, main.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import os
import re
import resource
import select
import shutil
import signal
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

from compiler_receipt import APPROVED_HOST, observed_cpu_model
from workload_selection import WORKLOAD_DIRECTORY, WORKLOAD_NAME, git_changes, select as select_workloads

SOURCE_LIMIT = 256 * 1024
DATA_LIMIT = 8 * 1024 * 1024
DATA_NAME = "input.data"
WARMUPS = 2
SAMPLES = 9
RUN_TIMEOUT_SECONDS = 10
COMPILE_TIMEOUT_SECONDS = 120
# Three separate bounds (#2936): the captured transcript keeps this many bytes
# per run and discards the rest without limiting the program; each regular file
# a program writes may reach SCRATCH_FILE_LIMIT (larger writes get SIGXFSZ or
# EFBIG); and all files left in a run directory may total SCRATCH_TOTAL_LIMIT,
# checked when the run ends, after which the directory is deleted.
OUTPUT_CAPTURE_LIMIT = 1024 * 1024
SCRATCH_FILE_LIMIT = 16 * 1024 * 1024
SCRATCH_TOTAL_LIMIT = 64 * 1024 * 1024
OUTPUT_SHOWN = 2000
COMPILE_FLAGS = (
    "-std=c11", "-O2", "-static", "-fwrapv", "-fno-strict-aliasing",
    "-funsigned-char", "-Wall", "-Wextra", "-Werror",
)
RUN_ENVIRONMENT = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}
# The write-ahead progress log is bounded: a line is cut at PROGRESS_LINE_LIMIT
# bytes and nothing is added once PROGRESS_LOG_LIMIT bytes (less room for one
# closing marker) are in the file. Four workloads of a dozen lines each use
# a few kilobytes.
PROGRESS_NAME = "progress.log"
PROGRESS_LINE_LIMIT = 300
PROGRESS_LOG_LIMIT = 64 * 1024
PROGRESS_FULL = b"-- progress log limit reached, later lines are on stderr only\n"
STOP_SIGNALS = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)


class StopRequested(KeyboardInterrupt):
    """A stop signal turned into the exception the interruption path handles."""

    def __init__(self, number: int) -> None:
        super().__init__(signal.Signals(number).name)
        self.number = number


class SignalGuard:
    """Turns SIGINT, SIGTERM and SIGHUP into StopRequested, outside held sections.

    Installed only by `main` and restored when it returns, so importing this
    module or calling its functions in-process changes no handler. A signal that
    arrives inside `held()` (a report write, a process launch, child cleanup) is
    recorded and raised when the outermost section ends. After the first stop is
    raised, later signals are recorded but never raise, so the remaining
    cleanup and the final report always complete. A signal that was ignored on
    entry (for example SIGHUP under nohup) stays ignored.
    """

    def __init__(self) -> None:
        self.saved: dict[int, object] = {}
        self.reset()

    def reset(self) -> None:
        self.received = 0
        self.pending = False
        self.stopping = False
        self.depth = 0

    def handle(self, number: int, frame: object) -> None:
        if not self.received:
            self.received = number
        if self.stopping:
            pass
        elif self.depth:
            self.pending = True
        else:
            self.stopping = True
            raise StopRequested(self.received)

    def install(self) -> None:
        self.reset()
        for number in STOP_SIGNALS:
            previous = signal.getsignal(number)
            if previous != signal.SIG_IGN:
                self.saved[number] = previous
                signal.signal(number, self.handle)

    def restore(self) -> None:
        for number, previous in self.saved.items():
            signal.signal(number, signal.SIG_DFL if previous is None else previous)
        self.saved = {}

    @contextlib.contextmanager
    def held(self):
        self.depth += 1
        try:
            yield
        finally:
            self.depth -= 1
            if not self.depth and self.pending and not self.stopping:
                self.pending = False
                self.stopping = True
                raise StopRequested(self.received)


SIGNALS = SignalGuard()


class Progress:
    """Append-only write-ahead log of what the run is about to do and has done.

    Every line is written and fsynced before the next operation, and echoed to
    stderr. The file is created in the work directory when the first stage
    starts, so a run refused before any work leaves no directory behind; lines
    noted earlier are written first. A failure to write the file is remembered
    in `error` and stops the file only; the measurement is not affected by it.
    """

    def __init__(self, directory: Path) -> None:
        self.directory = directory
        self.opened = False
        self.descriptor = -1
        self.backlog: list[bytes] = []
        self.size = 0
        self.full = False
        self.error = ""
        self.started = time.monotonic()

    def open(self) -> None:
        self.opened = True
        try:
            self.directory.mkdir(parents=True, exist_ok=True)
            self.descriptor = os.open(self.directory / PROGRESS_NAME, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        except OSError as error:
            self.error = f"cannot write {PROGRESS_NAME}: {error}"
        for data in self.backlog:
            self.append(data)
        self.backlog = []

    def append(self, data: bytes) -> None:
        if self.descriptor >= 0:
            try:
                if self.full:
                    pass
                elif self.size + len(data) + len(PROGRESS_FULL) > PROGRESS_LOG_LIMIT:
                    self.full = True
                    os.write(self.descriptor, PROGRESS_FULL)
                    os.fsync(self.descriptor)
                else:
                    if os.write(self.descriptor, data) != len(data):
                        raise OSError("short write")
                    self.size += len(data)
                    os.fsync(self.descriptor)
            except OSError as error:
                self.error = f"cannot write {PROGRESS_NAME}: {error}"
                self.close()

    def note(self, text: str) -> None:
        line = f"{time.monotonic() - self.started:9.3f}s {' '.join(text.split())}"
        data = line.encode("utf-8", "replace")[:PROGRESS_LINE_LIMIT - 1].decode("utf-8", "ignore").encode("utf-8") + b"\n"
        if self.opened:
            self.append(data)
        else:
            self.backlog.append(data)
        try:
            sys.stderr.write("progress: " + data.decode("utf-8"))
            sys.stderr.flush()
        except (OSError, ValueError):
            pass

    def stage(self, name: str, stage: str) -> str:
        """Record that `stage` of workload `name` is starting; returns the stage."""
        if not self.opened:
            self.open()
        self.note(f"START {name}: {stage}")
        return stage

    def close(self) -> None:
        if self.descriptor >= 0:
            os.close(self.descriptor)
            self.descriptor = -1


def changed_workloads(candidate: Path, base: str, head: str) -> tuple[list[str], list[str]]:
    """Workload sources to measure and refusal reasons (see workload_selection)."""
    listing = subprocess.run(
        ["git", "-C", str(candidate), "diff", "--name-status", "--no-renames", "-z",
         f"{base}...{head}", "--", WORKLOAD_DIRECTORY],
        check=True, capture_output=True, timeout=60).stdout.decode("utf-8")
    return select_workloads(git_changes(listing))


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
    resource.setrlimit(resource.RLIMIT_FSIZE, (SCRATCH_FILE_LIMIT, SCRATCH_FILE_LIMIT))


def kill_group(pid: int, fired: list[bool]) -> None:
    fired.append(True)
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def drain(descriptor: int, stop: threading.Event, sink: dict) -> None:
    """Read a pipe to its end, keeping only the first OUTPUT_CAPTURE_LIMIT bytes."""
    while True:
        ready, _, _ = select.select([descriptor], [], [], 0.05)
        if ready:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                break
            sink["total"] += len(chunk)
            room = OUTPUT_CAPTURE_LIMIT - len(sink["data"])
            if room > 0:
                sink["data"] += chunk[:room]
        elif stop.is_set():
            break


def compile_bounded(command: list[str], cwd: Path) -> dict:
    """Run the compiler with stdout and stderr drained as they arrive.

    Only the first OUTPUT_CAPTURE_LIMIT bytes are kept; the total is counted. The
    compiler's process group is killed on timeout, once it has exited (#2940) and
    on any exception, including a stop signal, so it never outlives the runner.
    """
    sink = {"data": bytearray(), "total": 0}
    stop = threading.Event()
    read_end, write_end = os.pipe()
    reader = threading.Thread(target=drain, args=(read_end, stop, sink))
    reader.start()
    child = None
    returncode = None
    try:
        # A stop signal during the launch is raised once `child` is assigned.
        with SIGNALS.held():
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=write_end,
                                     stderr=subprocess.STDOUT, cwd=cwd, start_new_session=True)
        os.close(write_end)
        write_end = -1
        try:
            returncode = child.wait(timeout=COMPILE_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            returncode = None
    finally:
        with SIGNALS.held():
            if child is not None:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                if child.returncode is None:
                    child.wait()
            if write_end >= 0:
                os.close(write_end)
            stop.set()
            reader.join()
            os.close(read_end)
    if returncode is None:
        raise subprocess.TimeoutExpired(command, COMPILE_TIMEOUT_SECONDS, bytes(sink["data"]))
    return {"exit": returncode, "output": bytes(sink["data"]), "output_bytes": sink["total"]}


def run_once(program: Path, scratch: Path, cpu: int) -> dict:
    """Start one fresh process and return its outcome, timings and output.

    The child's process group is killed and the child reaped on every exit path,
    including an exception or a stop signal while waiting, so no workload
    outlives the runner (#2902).
    """
    fired: list[bool] = []
    sink = {"data": bytearray(), "total": 0}
    stop = threading.Event()
    read_end, write_end = os.pipe()
    reader = threading.Thread(target=drain, args=(read_end, stop, sink))
    reader.start()
    child = None
    watchdog = None
    reaped = False
    try:
        with open(os.devnull, "rb") as nothing:
            started = time.monotonic_ns()
            # A stop signal during the launch is raised once `child` is assigned.
            with SIGNALS.held():
                child = subprocess.Popen(
                    [str(program)], stdin=nothing, stdout=write_end, stderr=subprocess.STDOUT,
                    cwd=scratch, env=RUN_ENVIRONMENT, close_fds=True,
                    preexec_fn=lambda: prepare_child(cpu))
                os.close(write_end)
                write_end = -1
                watchdog = threading.Timer(RUN_TIMEOUT_SECONDS, kill_group, (child.pid, fired))
                watchdog.start()
            _, status, usage = os.wait4(child.pid, 0)
            reaped = True
            finished = time.monotonic_ns()
            child.returncode = os.waitstatus_to_exitcode(status)
    finally:
        with SIGNALS.held():
            if watchdog is not None:
                watchdog.cancel()
                watchdog.join()
            # A descendant that outlived its leader would disturb the next sample.
            if child is not None:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                if not reaped:
                    try:
                        os.waitpid(child.pid, 0)
                    except ChildProcessError:
                        pass
            if write_end >= 0:
                os.close(write_end)
            stop.set()
            reader.join()
            os.close(read_end)
    return {
        "exit": child.returncode,
        "timed_out": bool(fired),
        "wall_ns": finished - started,
        "cpu_ns": int((usage.ru_utime + usage.ru_stime) * 1e9),
        "rss_bytes": usage.ru_maxrss * 1024,
        "output": bytes(sink["data"]),
        "output_bytes": sink["total"],
    }


def file_sha(path: Path) -> str:
    """SHA-256 of a file, or `missing` when it cannot be read."""
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return "missing"


def scratch_bytes(directory: Path) -> int:
    """Total size of the regular files under a run directory."""
    total = 0
    for root, _, names in os.walk(directory):
        for name in names:
            try:
                total += os.lstat(os.path.join(root, name)).st_size
            except OSError:
                pass
    return total


def run_sample(program: Path, scratch: Path, cpu: int, index: int, data: bytes | None) -> dict:
    """Run once in a fresh directory holding only the declared input bytes."""
    directory = scratch / f"run-{index}"
    directory.mkdir()
    try:
        if data is not None:
            (directory / DATA_NAME).write_bytes(data)
            (directory / DATA_NAME).chmod(0o444)
        row = run_once(program, directory, cpu)
        if data is not None:
            try:
                same = (directory / DATA_NAME).read_bytes() == data
            except OSError:
                same = False
            if not same:
                row["invalid"] = f"{DATA_NAME} was modified or removed during the run"
        used = scratch_bytes(directory)
        if row["exit"] == -signal.SIGXFSZ:
            row["invalid"] = f"a file exceeded the {SCRATCH_FILE_LIMIT} byte scratch file limit (SIGXFSZ)"
        elif used > SCRATCH_TOTAL_LIMIT:
            row["invalid"] = f"run files total {used} bytes, over the {SCRATCH_TOTAL_LIMIT} byte scratch limit"
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return row


def render(name: str, source_sha: str, program_sha: str, rows: list[dict], data_sha: str = "") -> tuple[list[str], bool]:
    def valid(row: dict) -> bool:
        return row["exit"] == 0 and not row["timed_out"] and not row.get("invalid")

    passed = len(rows) == WARMUPS + SAMPLES and all(valid(row) for row in rows)
    # Failed executions never enter the latency estimator; they stay in the table.
    measured = [row for row in rows[WARMUPS:] if valid(row)]
    invalid = len(rows) - sum(valid(row) for row in rows)
    data = f", `{DATA_NAME}` sha256 `{data_sha}`" if data_sha else ", no input data"
    lines = [f"### `{name}`", ""]
    if passed:
        lines += [f"Validity: all {len(rows)} planned runs completed and were valid.", ""]
    else:
        lines += [f"**INVALID, NOT A COMPLETE MEASUREMENT:** {len(rows)} of {WARMUPS + SAMPLES} planned runs "
                  f"completed, {invalid} of them failed (nonzero exit, signal, timeout or changed input).", ""]
    lines += [f"source sha256 `{source_sha}`, executable sha256 `{program_sha}`{data}", ""]
    if measured and passed:
        walls = [row["wall_ns"] for row in measured]
        lines += [
            f"Wall over {len(measured)} measured runs: median {statistics.median(walls) / 1e6:.3f} ms, "
            f"min {min(walls) / 1e6:.3f} ms, max {max(walls) / 1e6:.3f} ms. "
            f"CPU median {statistics.median(row['cpu_ns'] for row in measured) / 1e6:.3f} ms. "
            f"Peak RSS {max(row['rss_bytes'] for row in measured)} bytes.", ""]
    elif measured:
        walls = [row["wall_ns"] for row in measured]
        lines += [
            f"Incomplete and not comparable: descriptive wall statistics over only the {len(measured)} valid "
            f"of {SAMPLES} planned samples, failed runs excluded: median "
            f"{statistics.median(walls) / 1e6:.3f} ms, min {min(walls) / 1e6:.3f} ms, "
            f"max {max(walls) / 1e6:.3f} ms.", ""]
    else:
        lines += ["No valid timed samples: no latency summary is given.", ""]
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
    cut = [str(index) for index, row in enumerate(rows) if row.get("output_bytes", 0) > len(row["output"])]
    if cut:
        lines.append(f"Captured output is limited to {OUTPUT_CAPTURE_LIMIT} bytes per run; "
                     f"runs {', '.join(cut)} printed more and were truncated (the program was not limited).")
        lines.append("")
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


class Reporter:
    """Writes each finished piece of the report at once, so a later failure keeps it."""

    def __init__(self, summary: Path | None, failures: list[str]) -> None:
        self.summary = summary
        self.failures = failures
        self.summary_failed: list[str] = []

    def publish(self, chunk: list[str]) -> None:
        text = "\n".join(chunk) + "\n"
        # A stop signal waits until the piece is complete in both places, so it
        # is never torn. The summary is appended with one O_APPEND write loop,
        # not replaced: $GITHUB_STEP_SUMMARY already holds earlier steps' text.
        with SIGNALS.held():
            sys.stdout.write(text)
            sys.stdout.flush()
            if self.summary and not self.summary_failed:
                try:
                    descriptor = os.open(self.summary, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o666)
                    try:
                        data = text.encode("utf-8")
                        while data:
                            data = data[os.write(descriptor, data):]
                        os.fsync(descriptor)
                    finally:
                        os.close(descriptor)
                except OSError as error:
                    self.summary_failed.append(f"cannot write --summary: {error}")


def measure_workload(name: str, arguments: argparse.Namespace, compiler: str, reporter: Reporter,
                     progress: Progress) -> bool:
    """Compile and time one workload; returns True when it stopped on an error or a signal.

    Either leaves the rest of the plan unrun and reported as NOT RUN.
    """
    failures = reporter.failures
    stopped = False
    problem = source_problem(arguments.candidate, name)
    if problem:
        failures.append(f"{name}: {problem}")
        progress.note(f"SKIP {name}: {problem}")
        return False
    rows: list[dict] = []
    stage = "preparing the run directory"
    scratch = arguments.work / Path(name).stem
    source = arguments.candidate / name
    program = scratch / "program"
    try:
        progress.stage(name, stage)
        scratch.mkdir(parents=True)
        stage = progress.stage(name, "staging the source")
        # Only this one file is visible to the compiler, so a quoted include of a
        # neighbouring candidate header fails instead of becoming an untracked input.
        source_bytes = source.read_bytes()
        staged = scratch / "source" / source.name
        staged.parent.mkdir()
        staged.write_bytes(source_bytes)
        stage = progress.stage(name, "compilation")
        build = compile_bounded([compiler, *COMPILE_FLAGS, "-o", str(program), str(staged)], scratch)
        if build["exit"] != 0:
            failures.append(f"{name}: compilation failed")
            progress.note(f"FAILED {name}: compilation exited {build['exit']}")
            diagnostics = build["output"][:OUTPUT_SHOWN].decode("utf-8", "replace")
            if build["output_bytes"] > OUTPUT_SHOWN:
                diagnostics += (f"\n[compiler printed {build['output_bytes']} bytes; "
                                f"at most {OUTPUT_CAPTURE_LIMIT} kept, {OUTPUT_SHOWN} shown]")
            reporter.publish([f"### `{name}`", "", "Compilation failed:", "",
                              "The source is compiled alone: only system headers resolve, not neighbouring files.", "",
                              "```text", diagnostics.replace("```", "'''").rstrip("\n"), "```", ""])
        else:
            stage = progress.stage(name, "reading the input data")
            data = data_path(arguments.candidate, name)
            data_bytes = data.read_bytes() if data.is_file() else None
            data_sha = hashlib.sha256(data_bytes).hexdigest() if data_bytes is not None else ""
            stage = progress.stage(name, "hashing the executable")
            source_sha = hashlib.sha256(source_bytes).hexdigest()
            program_sha = file_sha(program)
            stage = progress.stage(name, "measurement")
            for index in range(WARMUPS + SAMPLES):
                rows.append(run_sample(program, scratch, arguments.cpu, index, data_bytes))
                progress.note(f"RUN {name} {index + 1} of {WARMUPS + SAMPLES}: exit {rows[-1]['exit']}, "
                              f"timed out {int(rows[-1]['timed_out'])}, wall {rows[-1]['wall_ns'] / 1e6:.3f} ms")
                # Outside the timed interval; this also keeps the executable in the page cache.
                current = file_sha(program)
                if current != program_sha:
                    rows[-1]["invalid"] = f"executable sha256 changed from {program_sha} to {current}"
                    break
            stage = progress.stage(name, "reporting")
            source_now = file_sha(source)
            if source_now != source_sha:
                for row in rows:
                    row.setdefault("invalid", f"source sha256 changed from {source_sha} to {source_now} after compilation")
            section, passed = render(name, source_sha, program_sha, rows, data_sha)
            reporter.publish(section)
            progress.note(f"DONE {name}: {'valid' if passed else 'INVALID'}, {len(rows)} runs")
            if not passed:
                failures.append(f"{name}: a run exited nonzero, was signalled, timed out or invalidated its input")
            for row in rows:
                if row.get("invalid"):
                    failures.append(f"{name}: {row['invalid']}")
                    break
    except (OSError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        # Keep what finished, name the stage, and never turn it into success.
        stopped = True
        kind = ("interrupted" if isinstance(error, KeyboardInterrupt)
                else "timed out" if isinstance(error, subprocess.TimeoutExpired) else "failed")
        if isinstance(error, StopRequested):
            detail = f"{error} received"
        else:
            detail = f"{type(error).__name__}: {error}" if str(error) else type(error).__name__
        failures.append(f"{name}: {stage} {kind} ({detail}); {len(rows)} of {WARMUPS + SAMPLES} "
                        "runs completed, the remaining work was NOT RUN")
        progress.note(f"INCOMPLETE {name}: {stage} {kind} ({detail}), {len(rows)} runs completed")
        completed = [f"Completed runs (exit/timed out/wall ms): " + ", ".join(
            f"{row['exit']}/{int(row['timed_out'])}/{row['wall_ns'] / 1e6:.3f}" for row in rows), ""]
        reporter.publish([f"### `{name}`", "",
                          f"**INCOMPLETE:** {stage} {kind}: `{detail}`. {len(rows)} of {WARMUPS + SAMPLES} runs "
                          "completed; the rest was NOT RUN and no summary is given.", ""]
                         + (completed if rows else []))
    return stopped


def run_plan(arguments: argparse.Namespace, compiler: str | None, cpu_model: str, failures: list[str],
             workloads: list[str]) -> int:
    """Measure every workload in order and publish the report; returns the exit status."""
    reporter = Reporter(arguments.summary, failures)
    progress = Progress(arguments.work)
    lines = ["## 9700X direct workload run", "",
             f"head `{arguments.head}`, base `{arguments.base}`, CPU {arguments.cpu}, "
             f"{WARMUPS} warmups and {SAMPLES} samples, {RUN_TIMEOUT_SECONDS} s limit per run.", "",
             f"Observed host: `{cpu_model}`.", "",
             "Diagnostic process latency (fork through wait4). No service lease is held; "
             "other host activity is not excluded.", ""]
    load_before = "unavailable"
    position = 0
    plan = [] if failures else workloads
    try:
        reporter.publish(lines)
        load_before = Path("/proc/loadavg").read_text(encoding="ascii").strip()
        if plan:
            progress.note(f"PLAN {len(plan)} workloads, {WARMUPS} warmups and {SAMPLES} samples each: "
                          + ", ".join(plan))
        while position < len(plan):
            stopped = measure_workload(plan[position], arguments, compiler, reporter, progress)
            position += 1
            if stopped:
                break
        # From here on only the closing report is written; later signals are recorded, not raised.
        SIGNALS.stopping = True
    except KeyboardInterrupt as error:
        # A stop that arrived between workloads, outside their own handling.
        SIGNALS.stopping = True
        failures.append(f"{error} received between workloads, outside their own handling; "
                        "every workload listed as NOT RUN did not finish")
        progress.note(f"INTERRUPTED outside a workload: {error}")
    if position < len(plan):
        failures.append("NOT RUN: " + ", ".join(plan[position:]))
        progress.note("NOT RUN " + ", ".join(plan[position:]))
    try:
        load_after = Path("/proc/loadavg").read_text(encoding="ascii").strip()
    except OSError:
        load_after = "unavailable"
    lines = [f"Compiler: `{compiler}`. Flags: `{' '.join(COMPILE_FLAGS)}`.", "",
             f"Load average before: `{load_before}`; after: `{load_after}`.", ""]
    failures.extend(reporter.summary_failed)
    if progress.error:
        failures.append(progress.error)
    for failure in failures:
        lines.append(f"**FAILED:** {failure}")
    status = 128 + SIGNALS.received if SIGNALS.received else 1 if failures else 0
    progress.note(f"END exit status {status}")
    reporter.publish(lines)
    progress.close()
    return status


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
    cpu_model = observed_cpu_model()
    if not APPROVED_HOST.search(cpu_model):
        failures.append(f"observed CPU {cpu_model!r} is not the approved Zen 5 host (AMD Ryzen 7 9700X); "
                        "nothing was measured")
    for value in (arguments.base, arguments.head):
        if not re.fullmatch(r"[0-9a-f]{40}", value):
            failures.append("base and head must be full lowercase commit IDs")
    # Every path is made absolute here, before any child changes directory, so
    # the compile command and each launch name the same executable.
    arguments.candidate = Path(os.path.abspath(arguments.candidate))
    arguments.work = Path(os.path.abspath(arguments.work))
    if arguments.summary:
        arguments.summary = Path(os.path.abspath(arguments.summary))
    found = shutil.which(arguments.cc)
    compiler = os.path.abspath(found) if found else None
    if not compiler:
        failures.append(f"compiler not found: {arguments.cc}")
    if not arguments.candidate.is_dir():
        failures.append(f"--candidate is not a directory: {arguments.candidate}")
    if arguments.summary and not arguments.summary.parent.is_dir():
        failures.append(f"--summary directory does not exist: {arguments.summary.parent}")
    if arguments.cpu not in os.sched_getaffinity(0):
        failures.append(f"CPU {arguments.cpu} is not available to this process")
    workloads, problems = ([], []) if failures else changed_workloads(arguments.candidate, arguments.base, arguments.head)
    failures.extend(problems)
    if not failures and not workloads:
        failures.append(f"the pull request adds or modifies no workload matching {WORKLOAD_NAME.pattern}")

    SIGNALS.install()
    try:
        status = run_plan(arguments, compiler, cpu_model, failures, workloads)
    finally:
        SIGNALS.restore()
    return status


if __name__ == "__main__":
    sys.exit(main())
