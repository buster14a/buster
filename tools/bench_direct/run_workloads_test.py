#!/usr/bin/env python3
"""Exercise the direct 9700X workload harness against a disposable repository."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
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
        real_run, real_once = subprocess.run, run_workloads.run_once

        def fake_run(command, **keywords):
            if command[0] != "git":
                seen.append(command[command.index("-o") + 1])
            return real_run(command, **keywords)

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
                    patch.object(run_workloads.subprocess, "run", fake_run), \
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

    def test_every_run_starts_without_predecessor_files(self) -> None:
        marker = ('#include <stdio.h>\nint main(void) { FILE* f = fopen("marker", "rb"); int existed = f != 0;\n'
                  '  if (f) fclose(f); else { f = fopen("marker", "wb"); if (f) fclose(f); }\n'
                  '  printf("existed=%d\\n", existed); return 0; }\n')
        result = self.run_harness(self.commit({"benchmarks/9700x/marker.c": marker}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("All 11 runs printed identical output:", result.stdout)
        self.assertIn("existed=0", result.stdout)
        self.assertEqual(os.listdir(self.root / "work" / "marker"), ["program"])

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
