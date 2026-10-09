#!/usr/bin/env python3
"""Exercise the direct 9700X workload harness against a disposable repository."""

from __future__ import annotations

import io
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

HARNESS = Path(__file__).resolve().with_name("run_workloads.py")
sys.path.insert(0, str(HARNESS.parent))
COMPILER = shutil.which("clang") or shutil.which("cc")
PASSING = '#include <stdio.h>\nint main(void) { puts("self-check ok checksum=2a"); return 0; }\n'
FAILING = "int main(void) { return 3; }\n"
APPROVED_CPU = "AMD Ryzen 7 9700X 8-Core Processor"
# Runs the harness with the observed CPU model replaced, so these tests do not
# depend on the host they run on; the harness itself has no override.
LAUNCHER = ("import sys; sys.path.insert(0, sys.argv[1]); import run_workloads; "
            "model = sys.argv[2]; run_workloads.observed_cpu_model = lambda: model; "
            "sys.argv = [run_workloads.__file__, *sys.argv[3:]]; sys.exit(run_workloads.main())")


# Appends its pid to a file for every run. The first three runs return at once;
# the fourth sleeps for five seconds, long enough to be signalled in flight.
HANGING = (
    '#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <time.h>\n#include <unistd.h>\n'
    'int main(void)\n{\n    FILE *log = fopen("%s", "a+");\n    int runs = 0;\n    int c;\n'
    '    if (!log) return 2;\n    fprintf(log, "%%ld\\n", (long)getpid());\n    fflush(log);\n'
    '    rewind(log);\n    while ((c = fgetc(log)) != EOF) runs += c == \'\\n\';\n    fclose(log);\n'
    '    if (runs > 3) nanosleep(&(struct timespec){5, 0}, 0);\n    puts("self-check ok");\n    return 0;\n}\n')


def alive(pid: int) -> bool:
    """True while the process exists and is not a zombie."""
    try:
        fields = Path(f"/proc/{pid}/stat").read_text(encoding="ascii").rsplit(")", 1)[1].split()
    except OSError:
        return False
    return fields[0] != "Z"


def git(repository: Path, *arguments: str) -> str:
    environment = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid",
                   "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.invalid"}
    # These disposable repositories have no housekeeping work worth detaching.
    # A completed commit must leave no Git worker racing strict fixture cleanup.
    return subprocess.run(["git", "-c", "maintenance.auto=false", "-c", "gc.auto=0",
                           "-C", str(repository), *arguments], check=True,
                          capture_output=True, text=True, env=environment).stdout.strip()


@unittest.skipUnless(COMPILER and sys.platform == "linux", "needs a C compiler on Linux")
class DirectWorkloadTest(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        self.repository = self.root / "candidate"
        self.repository.mkdir()
        git(self.repository, "init", "-q")
        (self.repository / "README").write_text("base\n", encoding="utf-8")
        git(self.repository, "add", "-A")
        git(self.repository, "commit", "-q", "-m", "base")
        self.base = git(self.repository, "rev-parse", "HEAD")

    def commit(self, files: dict[str, str]) -> str:
        for name, text in files.items():
            path = self.repository / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        git(self.repository, "add", "-A")
        git(self.repository, "commit", "-q", "-m", "head")
        return git(self.repository, "rev-parse", "HEAD")

    def run_harness(self, head: str, cpu_model: str = APPROVED_CPU, work: str = "",
                    cwd: Path | None = None, cc: str = "", candidate: str = "") -> subprocess.CompletedProcess:
        cpu = min(os.sched_getaffinity(0))
        return subprocess.run(
            [sys.executable, "-B", "-c", LAUNCHER, str(HARNESS.parent), cpu_model,
             "--candidate", candidate or str(self.repository),
             "--base", self.base, "--head", head, "--work", work or str(self.root / "work"),
             "--summary", str(self.root / "summary.md"), "--cc", cc or COMPILER, "--cpu", str(cpu)],
            capture_output=True, text=True, check=False, cwd=cwd)

    def test_reports_every_run_and_its_output(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/sort_check.c": PASSING}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"Observed host: `{APPROVED_CPU}`.", result.stdout)
        self.assertIn("All 11 runs printed identical output:", result.stdout)
        self.assertIn("self-check ok checksum=2a", result.stdout)
        self.assertEqual(result.stdout.count("| sample "), 9)
        self.assertEqual(result.stdout.count("| warmup "), 2)
        self.assertEqual((self.root / "summary.md").read_text(encoding="utf-8"), result.stdout)

    def test_relative_work_path_from_another_directory(self) -> None:
        head = self.commit({"benchmarks/9700x/sort_check.c": PASSING})
        for label, work in (("relative", "rel work/nested"), ("dotted", "./dot work")):
            with self.subTest(label=label):
                cwd = self.root / f"cwd {label}"
                cwd.mkdir()
                result = self.run_harness(head, work=work, cwd=cwd)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue((cwd / work / "sort_check" / "program").is_file())
                self.assertFalse((cwd / work / "sort_check" / Path(work)).exists())

    def test_relative_compiler_path(self) -> None:
        head = self.commit({"benchmarks/9700x/sort_check.c": PASSING})
        cwd = Path(COMPILER).parent
        result = self.run_harness(head, cwd=cwd, cc="./" + Path(COMPILER).name)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"Compiler: `{COMPILER}`", result.stdout)

    def test_compile_and_launches_use_one_absolute_executable(self) -> None:
        import run_workloads
        head = self.commit({"benchmarks/9700x/sort_check.c": PASSING})
        cwd = self.root / "elsewhere"
        cwd.mkdir()
        seen: list[str] = []
        real_compile, real_once = run_workloads.compile_bounded, run_workloads.run_once

        def fake_compile(command, cwd):
            seen.append(command[command.index("-o") + 1])
            return real_compile(command, cwd)

        def fake_once(program, scratch, cpu):
            seen.append(str(program))
            return real_once(program, scratch, cpu)

        arguments = ["run_workloads.py", "--candidate", str(self.repository), "--base", self.base,
                     "--head", head, "--work", "rel work", "--cpu", str(min(os.sched_getaffinity(0))),
                     "--cc", COMPILER]
        previous = os.getcwd()
        os.chdir(cwd)
        try:
            with open(os.devnull, "w") as quiet, patch.object(sys, "argv", arguments), \
                    patch.object(run_workloads, "observed_cpu_model", lambda: APPROVED_CPU), \
                    patch.object(run_workloads, "compile_bounded", fake_compile), \
                    patch.object(run_workloads, "run_once", fake_once), \
                    patch.object(sys, "stdout", quiet):
                status = run_workloads.main()
        finally:
            os.chdir(previous)
        self.assertEqual(status, 0)
        self.assertEqual(len(seen), 1 + 11)
        self.assertEqual(set(seen), {str(cwd / "rel work" / "sort_check" / "program")})

    def test_invalid_paths_fail_before_measuring(self) -> None:
        head = self.commit({"benchmarks/9700x/sort_check.c": PASSING})
        cases = (({"candidate": str(self.root / "missing")}, "--candidate is not a directory"),
                 ({"cc": "no-such-compiler-2934"}, "compiler not found: no-such-compiler-2934"))
        for keywords, reason in cases:
            with self.subTest(reason=reason):
                result = self.run_harness(head, **keywords)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn(reason, result.stdout)
                self.assertNotIn("| sample ", result.stdout)

    def test_other_or_unknown_host_measures_nothing(self) -> None:
        head = self.commit({"benchmarks/9700x/sort_check.c": PASSING})
        for model in ("AMD EPYC 7763 64-Core Processor", "AMD Ryzen 7 7700X 8-Core Processor", "NA", ""):
            with self.subTest(model=model):
                result = self.run_harness(head, model)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn(f"Observed host: `{model}`.", result.stdout)
                self.assertIn("is not the approved Zen 5 host (AMD Ryzen 7 9700X); nothing was measured",
                              result.stdout)
                self.assertNotIn("| sample ", result.stdout)
                self.assertFalse((self.root / "work").exists())

    def test_nonzero_exit_fails(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/broken.c": FAILING}))
        self.assertEqual(result.returncode, 1)
        self.assertIn("**FAILED:** benchmarks/9700x/broken.c: a run exited nonzero", result.stdout)

    def test_ignores_files_outside_the_workload_contract(self) -> None:
        result = self.run_harness(self.commit({
            "benchmarks/9700x/nested/inner.c": PASSING,
            "benchmarks/9700x/Makefile": "all:\n\ttrue\n",
            "tools/other.c": PASSING,
        }))
        self.assertEqual(result.returncode, 1)
        self.assertIn("adds or modifies no workload", result.stdout)
        self.assertFalse((self.root / "work").exists())

    def test_input_data_is_provided_and_reported(self) -> None:
        reader = ('#include <stdio.h>\nint main(void) { FILE* f = fopen("input.data", "rb"); int c = 0, n = 0;\n'
                  '  if (!f) return 2; while ((c = fgetc(f)) != EOF) n += c; printf("sum=%d\\n", n); return 0; }\n')
        self.commit({"benchmarks/9700x/replay.c": reader, "benchmarks/9700x/replay.data": "AB"})
        result = self.run_harness(git(self.repository, "rev-parse", "HEAD"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("sum=131", result.stdout)
        self.assertIn("`input.data` sha256", result.stdout)
        # Changing only the data selects its workload again.
        self.base = git(self.repository, "rev-parse", "HEAD")
        shutil.rmtree(self.root / "work")
        result = self.run_harness(self.commit({"benchmarks/9700x/replay.data": "ABC"}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("sum=198", result.stdout)

    def run_in_process(self, head: str, *patches, cc: str = "", work: str = "") -> tuple[int, str]:
        """Run main() here so faults can be injected; returns status and stdout."""
        import contextlib
        import io
        import run_workloads
        arguments = ["run_workloads.py", "--candidate", str(self.repository), "--base", self.base,
                     "--head", head, "--work", work or str(self.root / "work"),
                     "--summary", str(self.root / "summary.md"),
                     "--cpu", str(min(os.sched_getaffinity(0))), "--cc", cc or COMPILER]
        captured = io.StringIO()
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(sys, "argv", arguments))
            stack.enter_context(patch.object(run_workloads, "observed_cpu_model", lambda: APPROVED_CPU))
            stack.enter_context(patch.object(sys, "stdout", captured))
            for entered in patches:
                stack.enter_context(entered)
            status = run_workloads.main()
        return status, captured.getvalue()

    def test_compile_timeout_keeps_earlier_workload_results(self) -> None:
        import run_workloads
        wrapper = self.root / "slow-cc"
        wrapper.write_text(f'#!/bin/sh\ncase "$*" in *zz_slow*) exec sleep 30;; esac\nexec {COMPILER} "$@"\n',
                           encoding="utf-8")
        wrapper.chmod(0o755)
        head = self.commit({"benchmarks/9700x/aa_ok.c": PASSING, "benchmarks/9700x/zz_slow.c": PASSING})
        status, out = self.run_in_process(
            head, patch.object(run_workloads, "COMPILE_TIMEOUT_SECONDS", 3), cc=str(wrapper))
        self.assertEqual(status, 1)
        self.assertEqual(out.count("| sample "), 9)
        self.assertIn("**INCOMPLETE:** compilation timed out", out)
        self.assertIn("benchmarks/9700x/zz_slow.c: compilation timed out", out)
        self.assertEqual((self.root / "summary.md").read_text(encoding="utf-8"), out)

    def test_compiler_that_cannot_launch_is_a_failed_stage(self) -> None:
        wrapper = self.root / "broken-cc"
        wrapper.write_text("#!/nonexistent/interpreter\n", encoding="utf-8")
        wrapper.chmod(0o755)
        status, out = self.run_in_process(
            self.commit({"benchmarks/9700x/aa_ok.c": PASSING}), cc=str(wrapper))
        self.assertEqual(status, 1)
        self.assertIn("compilation failed (", out)
        self.assertIn("0 of 11 runs completed", out)
        self.assertNotIn("| sample ", out)

    def test_unavailable_work_directory_is_reported(self) -> None:
        blocker = self.root / "blocker"
        blocker.write_text("file", encoding="utf-8")
        status, out = self.run_in_process(
            self.commit({"benchmarks/9700x/aa_ok.c": PASSING}), work=str(blocker / "work"))
        self.assertEqual(status, 1)
        self.assertIn("preparing the run directory failed", out)
        self.assertIn("NOT RUN", out)

    def test_input_copy_failure_is_reported(self) -> None:
        self.commit({"benchmarks/9700x/aa_ok.c": PASSING, "benchmarks/9700x/aa_ok.data": "AB"})
        head = git(self.repository, "rev-parse", "HEAD")
        real = Path.read_bytes

        def failing(path):
            if path.name.endswith(".data"):
                raise OSError(5, "injected read failure")
            return real(path)

        status, out = self.run_in_process(head, patch.object(Path, "read_bytes", failing))
        self.assertEqual(status, 1)
        self.assertIn("reading the input data failed", out)
        self.assertIn("injected read failure", out)
        self.assertNotIn("| sample ", out)

    def test_interruption_keeps_completed_runs_and_marks_the_rest_not_run(self) -> None:
        import run_workloads
        real = run_workloads.run_sample

        def interrupted(program, scratch, cpu, index, data):
            if index == 4:
                raise KeyboardInterrupt
            return real(program, scratch, cpu, index, data)

        head = self.commit({"benchmarks/9700x/aa_ok.c": PASSING, "benchmarks/9700x/bb_ok.c": PASSING})
        status, out = self.run_in_process(head, patch.object(run_workloads, "run_sample", interrupted))
        self.assertEqual(status, 1)
        self.assertIn("measurement interrupted", out)
        self.assertIn("4 of 11 runs completed", out)
        self.assertIn("NOT RUN: benchmarks/9700x/bb_ok.c", out)
        self.assertNotIn("Wall over", out)

    def start_harness(self, head: str, cc: str = "") -> subprocess.Popen:
        """Start the harness as its own process so it can be signalled."""
        cpu = min(os.sched_getaffinity(0))
        process = subprocess.Popen(
            [sys.executable, "-B", "-c", LAUNCHER, str(HARNESS.parent), APPROVED_CPU,
             "--candidate", str(self.repository), "--base", self.base, "--head", head,
             "--work", str(self.root / "work"), "--summary", str(self.root / "summary.md"),
             "--cc", cc or COMPILER, "--cpu", str(cpu)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            # A background shell starts jobs with SIGINT ignored, which Python keeps.
            preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL))
        self.addCleanup(process.kill)
        self.addCleanup(process.communicate)
        return process

    def wait_for_lines(self, path: Path, count: int) -> list[int]:
        """The pids a workload or fake compiler has written, once there are `count`."""
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            text = path.read_text(encoding="ascii") if path.exists() else ""
            if len(text.split()) >= count:
                return [int(word) for word in text.split()]
            time.sleep(0.02)
        self.fail(f"{path} did not reach {count} lines")

    def hanging_head(self) -> tuple[str, Path]:
        pids = self.root / "pids"
        head = self.commit({"benchmarks/9700x/aa_hang.c": HANGING % pids,
                            "benchmarks/9700x/bb_ok.c": PASSING})
        return head, pids

    def test_stop_signal_keeps_completed_runs_and_leaves_no_workload(self) -> None:
        head, pids = self.hanging_head()
        for number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            with self.subTest(signal=number.name):
                pids.unlink(missing_ok=True)
                shutil.rmtree(self.root / "work", ignore_errors=True)
                (self.root / "summary.md").unlink(missing_ok=True)
                process = self.start_harness(head)
                running = self.wait_for_lines(pids, 4)[-1]
                self.assertTrue(alive(running))
                process.send_signal(number)
                out, err = process.communicate(timeout=60)
                self.assertEqual(process.returncode, 128 + number, out + err)
                self.assertFalse(alive(running), "the signalled workload outlived the runner")
                self.assertIn(f"**INCOMPLETE:** measurement interrupted: `{number.name} received`. "
                              "3 of 11 runs completed", out)
                self.assertIn("Completed runs (exit/timed out/wall ms): 0/0/", out)
                self.assertIn("NOT RUN: benchmarks/9700x/bb_ok.c", out)
                self.assertNotIn("Wall over", out)
                self.assertEqual((self.root / "summary.md").read_text(encoding="utf-8"), out)
                log = (self.root / "work" / "progress.log").read_text(encoding="utf-8")
                self.assertIn("PLAN 2 workloads", log)
                self.assertIn("START benchmarks/9700x/aa_hang.c: measurement", log)
                self.assertEqual(log.count(" RUN benchmarks/9700x/aa_hang.c "), 3)
                self.assertIn("INCOMPLETE benchmarks/9700x/aa_hang.c: measurement interrupted", log)
                self.assertTrue(log.rstrip().endswith(f"END exit status {128 + number}"), log)
                self.assertIn("progress: ", err)

    def test_stop_signal_during_compilation_kills_the_compiler(self) -> None:
        pids = self.root / "pids"
        wrapper = self.root / "hang-cc"
        wrapper.write_text(f'#!/bin/sh\necho $$ >> {pids}\nexec sleep 60\n', encoding="utf-8")
        wrapper.chmod(0o755)
        process = self.start_harness(self.commit({"benchmarks/9700x/aa_ok.c": PASSING,
                                                  "benchmarks/9700x/bb_ok.c": PASSING}), cc=str(wrapper))
        compiler = self.wait_for_lines(pids, 1)[0]
        process.send_signal(signal.SIGTERM)
        out, err = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 143, out + err)
        self.assertFalse(alive(compiler), "the compiler outlived the runner")
        self.assertIn("**INCOMPLETE:** compilation interrupted: `SIGTERM received`. 0 of 11 runs completed", out)
        self.assertIn("NOT RUN: benchmarks/9700x/bb_ok.c", out)

    def test_progress_log_survives_sigkill(self) -> None:
        # SIGKILL cannot be handled: no INCOMPLETE section or NOT RUN list, and
        # the workload keeps running. The write-ahead log still holds what finished.
        head, pids = self.hanging_head()
        process = self.start_harness(head)
        running = self.wait_for_lines(pids, 4)[-1]
        self.addCleanup(lambda: os.killpg(running, signal.SIGKILL) if alive(running) else None)
        process.kill()
        process.communicate(timeout=60)
        self.assertEqual(process.returncode, -signal.SIGKILL)
        log = (self.root / "work" / "progress.log").read_text(encoding="utf-8")
        self.assertIn("PLAN 2 workloads", log)
        self.assertEqual(log.count(" RUN benchmarks/9700x/aa_hang.c "), 3)
        self.assertNotIn("INCOMPLETE", log)
        self.assertNotIn("**INCOMPLETE:**", (self.root / "summary.md").read_text(encoding="utf-8"))

    def test_stop_signal_in_process_restores_handlers_and_reports(self) -> None:
        import run_workloads
        real = run_workloads.run_sample
        seen: dict[str, object] = {}
        before = {number: signal.getsignal(number) for number in run_workloads.STOP_SIGNALS}

        def stopped(program, scratch, cpu, index, data):
            if index == 2:
                seen["inside"] = signal.getsignal(signal.SIGTERM)
                os.kill(os.getpid(), signal.SIGTERM)
                time.sleep(5)
            return real(program, scratch, cpu, index, data)

        head = self.commit({"benchmarks/9700x/aa_ok.c": PASSING, "benchmarks/9700x/bb_ok.c": PASSING})
        status, out = self.run_in_process(head, patch.object(run_workloads, "run_sample", stopped))
        self.assertEqual(status, 128 + signal.SIGTERM)
        self.assertEqual(seen["inside"], run_workloads.SIGNALS.handle)
        self.assertIn("2 of 11 runs completed", out)
        self.assertIn("SIGTERM received", out)
        self.assertIn("NOT RUN: benchmarks/9700x/bb_ok.c", out)
        self.assertEqual({number: signal.getsignal(number) for number in run_workloads.STOP_SIGNALS}, before)

    def test_ignored_signal_stays_ignored(self) -> None:
        import run_workloads
        previous = signal.signal(signal.SIGHUP, signal.SIG_IGN)
        self.addCleanup(signal.signal, signal.SIGHUP, previous)
        seen: list[object] = []
        real = run_workloads.measure_workload

        def watching(*arguments):
            seen.append(signal.getsignal(signal.SIGHUP))
            return real(*arguments)

        status, _ = self.run_in_process(self.commit({"benchmarks/9700x/aa_ok.c": PASSING}),
                                        patch.object(run_workloads, "measure_workload", watching))
        self.assertEqual(status, 0)
        self.assertEqual(seen, [signal.SIG_IGN])
        self.assertEqual(signal.getsignal(signal.SIGHUP), signal.SIG_IGN)

    def test_stop_signal_waits_for_a_report_write_to_finish(self) -> None:
        import run_workloads
        guard = run_workloads.SIGNALS
        guard.install()
        self.addCleanup(guard.restore)
        summary = self.root / "torn.md"
        reporter = run_workloads.Reporter(summary, [])
        real = sys.stdout

        class Interrupting(io.StringIO):
            def write(self, text):
                os.kill(os.getpid(), signal.SIGTERM)
                time.sleep(0.05)
                return super().write(text)

        captured = Interrupting()
        with patch.object(sys, "stdout", captured), self.assertRaises(run_workloads.StopRequested):
            reporter.publish(["one", "two"])
        self.assertIs(sys.stdout, real)
        self.assertEqual(captured.getvalue(), "one\ntwo\n")
        self.assertEqual(summary.read_text(encoding="utf-8"), "one\ntwo\n")
        # After the first stop, further signals are recorded and never raise.
        os.kill(os.getpid(), signal.SIGHUP)
        time.sleep(0.05)
        self.assertEqual(guard.received, signal.SIGTERM)

    def test_progress_log_is_bounded(self) -> None:
        import run_workloads
        progress = run_workloads.Progress(self.root / "log dir")
        self.addCleanup(progress.close)
        with patch.object(sys, "stderr", io.StringIO()) as echoed:
            for index in range(5000):
                progress.stage("benchmarks/9700x/aa_ok.c", f"stage {index} " + "x" * 1000)
        data = (self.root / "log dir" / run_workloads.PROGRESS_NAME).read_bytes()
        self.assertLessEqual(len(data), run_workloads.PROGRESS_LOG_LIMIT)
        self.assertTrue(data.endswith(run_workloads.PROGRESS_FULL))
        self.assertEqual(data.count(run_workloads.PROGRESS_FULL), 1)
        lines = data.splitlines()
        self.assertTrue(all(len(line) < run_workloads.PROGRESS_LINE_LIMIT for line in lines))
        self.assertEqual(echoed.getvalue().count("\n"), 5000)
        self.assertEqual(progress.error, "")

    def test_unwritable_progress_log_fails_the_run_but_keeps_measuring(self) -> None:
        import run_workloads
        real = os.open

        def refusing(path, *arguments, **keywords):
            if str(path).endswith(run_workloads.PROGRESS_NAME):
                raise OSError(13, "injected denial")
            return real(path, *arguments, **keywords)

        status, out = self.run_in_process(self.commit({"benchmarks/9700x/aa_ok.c": PASSING}),
                                          patch.object(os, "open", refusing))
        self.assertEqual(status, 1)
        self.assertEqual(out.count("| sample "), 9)
        self.assertIn("**FAILED:** cannot write progress.log: [Errno 13] injected denial", out)

    def test_render_does_not_summarize_failed_runs(self) -> None:
        import run_workloads

        def row(exit_code=0, timed_out=False, wall=1_000_000):
            return {"exit": exit_code, "timed_out": timed_out, "wall_ns": wall, "cpu_ns": wall,
                    "rss_bytes": 4096, "output": b"x"}

        def shaped(overrides: dict[int, dict]) -> list[dict]:
            return [overrides.get(index, row()) for index in range(11)]

        cases = {
            "all-success": ({}, True),
            "failed warmup": ({0: row(1)}, False),
            "one signal": ({5: row(-11)}, False),
            "one timeout": ({6: row(-9, True)}, False),
            "mixed timed failures": ({3: row(2), 7: row(1)}, False),
        }
        for label, (overrides, expected) in cases.items():
            with self.subTest(label=label):
                lines, passed = run_workloads.render("w.c", "s", "p", shaped(overrides))
                text = "\n".join(lines)
                self.assertEqual(passed, expected)
                self.assertEqual(text.count("| sample ") + text.count("| warmup "), 11)
                if expected:
                    self.assertIn("Validity: all 11 planned runs", text)
                    self.assertIn("Wall over 9 measured runs", text)
                else:
                    self.assertLess(text.index("INVALID, NOT A COMPLETE MEASUREMENT"),
                                    text.index("Incomplete and not comparable"))
                    self.assertNotIn("Wall over", text)
        lines, passed = run_workloads.render("w.c", "s", "p", [row(3) for _ in range(11)])
        text = "\n".join(lines)
        self.assertFalse(passed)
        self.assertIn("No valid timed samples: no latency summary is given.", text)
        self.assertNotIn("median", text)
        # Failed samples are excluded from the partial estimator.
        lines, _ = run_workloads.render("w.c", "s", "p", shaped({4: row(1, wall=1)}))
        self.assertIn("only the 8 valid of 9 planned samples", "\n".join(lines))

    def test_immediately_failing_workload_has_no_latency_headline(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/broken.c": FAILING}))
        self.assertEqual(result.returncode, 1)
        self.assertIn("No valid timed samples", result.stdout)
        self.assertNotIn("Wall over", result.stdout)
        self.assertEqual(result.stdout.count("| sample "), 9)

    @staticmethod
    def writer(files: int, size: int, ignore_xfsz: bool = False) -> str:
        return ('#include <signal.h>\n#include <stdio.h>\n#include <string.h>\n#include <unistd.h>\n'
                '#include <fcntl.h>\nstatic char block[%d];\n'
                'int main(void) {\n%s  memset(block, 7, sizeof block); int short_writes = 0;\n'
                '  for (int i = 0; i < %d; i++) { char name[16]; snprintf(name, sizeof name, "f%%d", i);\n'
                '    int fd = open(name, O_WRONLY | O_CREAT, 0644); if (fd < 0) return 3;\n'
                '    long done = 0; while (done < (long)sizeof block) { long n = write(fd, block + done, sizeof block - done);\n'
                '      if (n <= 0) { short_writes++; break; } done += n; } close(fd); }\n'
                '  printf("short_writes=%%d\\n", short_writes); return 0; }\n'
                % (size, "  signal(SIGXFSZ, SIG_IGN);\n" if ignore_xfsz else "", files))

    def test_scratch_files_are_bounded_separately_from_the_transcript(self) -> None:
        import run_workloads
        limits = (patch.object(run_workloads, "SCRATCH_FILE_LIMIT", 65536),
                  patch.object(run_workloads, "SCRATCH_TOTAL_LIMIT", 100000))
        cases = (
            ("boundary", self.writer(1, 65536), 0, "short_writes=0"),
            ("over", self.writer(1, 65537), 1, "a file exceeded the 65536 byte scratch file limit (SIGXFSZ)"),
            ("many", self.writer(3, 60000), 1, "run files total 180000 bytes, over the 100000 byte scratch limit"),
            ("efbig", self.writer(1, 65537, True), 0, "short_writes=1"),
        )
        for label, source, status, expected in cases:
            with self.subTest(label=label):
                shutil.rmtree(self.root / "work", ignore_errors=True)
                head = self.commit({f"benchmarks/9700x/{label}.c": source})
                code, out = self.run_in_process(head, *limits)
                self.assertEqual(code, status, out)
                self.assertIn(expected, out)
                self.base = head
                self.assertEqual(sorted(os.listdir(self.root / "work" / label)), ["program", "source"])

    def test_quiet_two_mebibyte_file_is_not_a_transcript_overflow(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/bigfile.c": self.writer(1, 2 * 1024 * 1024)}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("short_writes=0", result.stdout)

    def test_noisy_output_is_truncated_without_limiting_the_program(self) -> None:
        import run_workloads
        noisy = ('#include <stdio.h>\nint main(void) { for (int i = 0; i < 40000; i++) '
                 'fputs("0123456789abcdef", stdout); fputs("END\\n", stderr); return 0; }\n')
        head = self.commit({"benchmarks/9700x/noisy.c": noisy})
        code, out = self.run_in_process(head, patch.object(run_workloads, "OUTPUT_CAPTURE_LIMIT", 1024))
        self.assertEqual(code, 0, out)
        self.assertIn("Captured output is limited to 1024 bytes per run", out)
        self.assertIn("truncated", out)

    def fake_compiler(self, body: str) -> str:
        path = self.root / "noisy-cc"
        path.write_text(body, encoding="utf-8")
        path.chmod(0o755)
        return str(path)

    def test_compiler_diagnostics_are_bounded_while_running(self) -> None:
        import run_workloads
        import time
        script = ("#!/usr/bin/env python3\nimport os, subprocess, sys\n"
                  "for _ in range(30):\n    os.write(1, b'o' * 100000)\n    os.write(2, b'e' * 100000)\n"
                  "subprocess.Popen(['sleep', '30'])\nsys.exit(3)\n")
        cc = self.fake_compiler(script)
        started = time.monotonic()
        with patch.object(run_workloads, "OUTPUT_CAPTURE_LIMIT", 4096):
            result = run_workloads.compile_bounded([cc], self.root)
        self.assertLess(time.monotonic() - started, 10)
        self.assertEqual(result["exit"], 3)
        self.assertEqual(len(result["output"]), 4096)
        self.assertEqual(result["output_bytes"], 6_000_000)

    def test_large_compiler_diagnostics_do_not_fail_a_good_compile_or_hide_a_bad_one(self) -> None:
        import run_workloads
        noise = "import sys; sys.stderr.write('w' * 3000000)"
        good = self.fake_compiler(f'#!/bin/sh\npython3 -c "{noise}"\nexec {COMPILER} "$@"\n')
        head = self.commit({"benchmarks/9700x/aa_ok.c": PASSING})
        with patch.object(run_workloads, "OUTPUT_CAPTURE_LIMIT", 4096):
            status, out = self.run_in_process(head, cc=good)
        self.assertEqual(status, 0, out)
        bad = self.fake_compiler(f'#!/bin/sh\npython3 -c "{noise}"\nexit 1\n')
        shutil.rmtree(self.root / "work")
        with patch.object(run_workloads, "OUTPUT_CAPTURE_LIMIT", 4096):
            status, out = self.run_in_process(head, cc=bad)
        self.assertEqual(status, 1)
        self.assertIn("Compilation failed:", out)
        self.assertIn("compiler printed 3000000 bytes; at most 4096 kept, 2000 shown", out)
        self.assertNotIn("INCOMPLETE", out)

    def test_changed_or_deleted_executable_invalidates_the_series(self) -> None:
        delete = '#include <unistd.h>\nint main(void) { unlink("../program"); return 0; }\n'
        replace = ('#include <fcntl.h>\n#include <unistd.h>\nint main(void) { unlink("../program");\n'
                   '  int fd = open("../program", O_WRONLY | O_CREAT, 0755); if (fd >= 0) { write(fd, "x", 1); close(fd); }\n'
                   '  return 0; }\n')
        for label, source, current in (("delete", delete, "missing"), ("replace", replace, None)):
            with self.subTest(label=label):
                shutil.rmtree(self.root / "work", ignore_errors=True)
                head = self.commit({f"benchmarks/9700x/{label}.c": source})
                result = self.run_harness(head)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("executable sha256 changed from ", result.stdout)
                if current:
                    self.assertIn(f" to {current}", result.stdout)
                self.assertIn("INVALID, NOT A COMPLETE MEASUREMENT", result.stdout)
                self.assertIn("1 of 11 planned runs completed", result.stdout)
                self.assertNotIn("Wall over", result.stdout)
                self.base = head

    def test_source_changed_after_compilation_is_invalid(self) -> None:
        import run_workloads
        head = self.commit({"benchmarks/9700x/aa_ok.c": PASSING})
        real = run_workloads.compile_bounded

        def compile_then_edit(command, cwd):
            result = real(command, cwd)
            (self.repository / "benchmarks/9700x/aa_ok.c").write_text(PASSING + "/* edited */\n", encoding="utf-8")
            return result

        status, out = self.run_in_process(head, patch.object(run_workloads, "compile_bounded", compile_then_edit))
        self.assertEqual(status, 1, out)
        self.assertIn("source sha256 changed from ", out)
        self.assertIn("after compilation", out)
        self.assertNotIn("Wall over", out)

    def test_matching_identities_are_reported(self) -> None:
        import hashlib
        result = self.run_harness(self.commit({"benchmarks/9700x/sort_check.c": PASSING}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        digest = hashlib.sha256((self.root / "work" / "sort_check" / "program").read_bytes()).hexdigest()
        self.assertIn(f"executable sha256 `{digest}`", result.stdout)
        self.assertIn(f"source sha256 `{hashlib.sha256(PASSING.encode()).hexdigest()}`", result.stdout)
        self.assertIn("Validity: all 11 planned runs", result.stdout)

    def test_every_run_starts_without_predecessor_files(self) -> None:
        marker = ('#include <stdio.h>\nint main(void) { FILE* f = fopen("marker", "rb"); int existed = f != 0;\n'
                  '  if (f) fclose(f); else { f = fopen("marker", "wb"); if (f) fclose(f); }\n'
                  '  printf("existed=%d\\n", existed); return 0; }\n')
        result = self.run_harness(self.commit({"benchmarks/9700x/marker.c": marker}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("All 11 runs printed identical output:", result.stdout)
        self.assertIn("existed=0", result.stdout)
        self.assertEqual(sorted(os.listdir(self.root / "work" / "marker")), ["program", "source"])

    def test_neighbouring_local_headers_are_not_compile_inputs(self) -> None:
        user = '#include "helper.h"\nint main(void) { return RESULT; }\n'
        head = self.commit({"benchmarks/9700x/uses_header.c": user, "benchmarks/9700x/helper.h": "#define RESULT 0\n"})
        result = self.run_harness(head)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Compilation failed", result.stdout)
        self.assertIn("helper.h", result.stdout)
        self.assertNotIn("| sample ", result.stdout)
        # Sibling headers elsewhere, and system headers, stay irrelevant to a standalone source.
        shutil.rmtree(self.root / "work")
        self.base = head
        result = self.run_harness(self.commit({"benchmarks/9700x/standalone.c": PASSING, "benchmarks/9700x/helper.h": "x\n"}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_input_mutation_is_reported_invalid(self) -> None:
        mutate = ('#include <stdio.h>\n#include <sys/stat.h>\n#include <unistd.h>\n'
                  'int main(void) { chmod("input.data", 0644); unlink("input.data");\n'
                  '  FILE* f = fopen("input.data", "wb"); if (f) { fputc(90, f); fclose(f); } return 0; }\n')
        self.commit({"benchmarks/9700x/mutate.c": mutate, "benchmarks/9700x/mutate.data": "AB"})
        result = self.run_harness(git(self.repository, "rev-parse", "HEAD"))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("input.data was modified or removed during the run", result.stdout)

    def test_missing_input_is_visible_to_the_program(self) -> None:
        reader = '#include <stdio.h>\nint main(void) { FILE* f = fopen("input.data", "rb"); return f ? 0 : 2; }\n'
        result = self.run_harness(self.commit({"benchmarks/9700x/noinput.c": reader}))
        self.assertEqual(result.returncode, 1)
        self.assertIn("a run exited nonzero", result.stdout)
        self.assertIn("no input data", result.stdout)

    def test_oversized_data_fails_without_running(self) -> None:
        self.commit({"benchmarks/9700x/big.c": PASSING})
        (self.repository / "benchmarks/9700x/big.data").write_bytes(b"\0" * (8 * 1024 * 1024 + 1))
        git(self.repository, "add", "-A")
        git(self.repository, "commit", "-q", "-m", "big data")
        result = self.run_harness(git(self.repository, "rev-parse", "HEAD"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("big.data is larger than", result.stdout)
        self.assertNotIn("| sample ", result.stdout)
        self.assertFalse((self.root / "work").exists())

    def selection(self, head: str) -> tuple[list[str], list[str]]:
        import run_workloads
        return run_workloads.changed_workloads(self.repository, self.base, head)

    def advance(self, *, remove: tuple[str, ...] = (), rename: tuple[tuple[str, str], ...] = (),
                write: dict[str, str] | None = None) -> str:
        """Commit a change on top of the current checkout."""
        for name in remove:
            git(self.repository, "rm", "-q", name)
        for old, new in rename:
            git(self.repository, "mv", old, new)
        return self.commit(write or {})

    def test_selection_covers_every_change_kind(self) -> None:
        directory = "benchmarks/9700x/"
        self.base = self.commit({directory + "first.c": PASSING, directory + "first.data": "A",
                                 directory + "other.c": PASSING})
        cases = {
            "modified source": (dict(write={directory + "first.c": PASSING + "\n"}), (["benchmarks/9700x/first.c"], False)),
            "deleted sidecar": (dict(remove=(directory + "first.data",)), (["benchmarks/9700x/first.c"], False)),
            "renamed source": (dict(rename=((directory + "first.c", directory + "second.c"),)),
                               (["benchmarks/9700x/second.c"], False)),
            "renamed data": (dict(rename=((directory + "first.data", directory + "other.data"),)),
                             (["benchmarks/9700x/first.c", "benchmarks/9700x/other.c"], False)),
            "deleted source": (dict(remove=(directory + "other.c",)), ([], False)),
            "deleted source and its sidecar edit": (dict(remove=(directory + "first.c",),
                                                         write={directory + "first.data": "B"}), ([], False)),
            "data only": (dict(write={directory + "first.data": "B"}), (["benchmarks/9700x/first.c"], False)),
            "mixed": (dict(remove=(directory + "other.c",), write={directory + "new.c": PASSING,
                                                                    directory + "first.data": "B"}),
                      (["benchmarks/9700x/first.c", "benchmarks/9700x/new.c"], False)),
            "invalid added name": (dict(write={directory + "Bad.c": PASSING}), ([], True)),
            "invalid renamed-to name": (dict(rename=((directory + "other.c", directory + "Other.c"),)), ([], True)),
        }
        for name, (change, (workloads, refused)) in cases.items():
            with self.subTest(case=name):
                git(self.repository, "checkout", "-q", "--detach", self.base)
                found, problems = self.selection(self.advance(**change))
                self.assertEqual((found, bool(problems)), (workloads, refused))
                if refused:
                    self.assertIn("unsupported workload filename", problems[0])

    def test_invalid_name_is_refused_before_building(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/Bad.c": PASSING,
                                               "benchmarks/9700x/good.c": PASSING}))
        self.assertEqual(result.returncode, 1)
        self.assertIn("Bad.c: unsupported workload filename", result.stdout)
        self.assertNotIn("| sample ", result.stdout)
        self.assertFalse((self.root / "work").exists())

    def test_fixture_git_does_not_launch_automatic_maintenance(self) -> None:
        # Force housekeeping even for this tiny repository. Git's trace records
        # child launches before detachment, so this needs no scheduling race.
        git(self.repository, "config", "maintenance.auto", "true")
        git(self.repository, "config", "maintenance.geometric-repack.auto", "-1")
        git(self.repository, "config", "maintenance.loose-objects.enabled", "true")
        git(self.repository, "config", "maintenance.loose-objects.auto", "1")
        git(self.repository, "config", "gc.auto", "1")
        trace = self.root / "git-trace.jsonl"
        with patch.dict(os.environ, {"GIT_TRACE2_EVENT": str(trace)}):
            head = self.commit({"benchmarks/9700x/check.c": PASSING})
        events = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines()]
        self.assertTrue(any(event.get("event") == "start" for event in events))
        children = [event for event in events if event.get("event") == "child_start"]
        housekeeping = [event for event in children
                        if "maintenance" in event.get("argv", []) or "gc" in event.get("argv", [])]
        self.assertEqual(housekeeping, [])
        self.assertEqual(git(self.repository, "show", f"{head}:benchmarks/9700x/check.c"), PASSING.strip())

    def test_compile_error_fails_without_running(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/bad.c": "int main(void) { return x; }\n"}))
        self.assertEqual(result.returncode, 1)
        self.assertIn("Compilation failed:", result.stdout)
        self.assertNotIn("| sample ", result.stdout)


if __name__ == "__main__":
    unittest.main()
