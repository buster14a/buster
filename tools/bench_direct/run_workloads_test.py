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
COMPILER = shutil.which("clang") or shutil.which("cc")
PASSING = '#include <stdio.h>\nint main(void) { puts("self-check ok checksum=2a"); return 0; }\n'
FAILING = "int main(void) { return 3; }\n"


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

    def run_harness(self, head: str) -> subprocess.CompletedProcess:
        cpu = min(os.sched_getaffinity(0))
        return subprocess.run(
            [sys.executable, "-B", str(HARNESS), "--candidate", str(self.repository),
             "--base", self.base, "--head", head, "--work", str(self.root / "work"),
             "--summary", str(self.root / "summary.md"), "--cc", COMPILER, "--cpu", str(cpu)],
            capture_output=True, text=True, check=False)

    def test_reports_every_run_and_its_output(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/sort_check.c": PASSING}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("All 11 runs printed identical output:", result.stdout)
        self.assertIn("self-check ok checksum=2a", result.stdout)
        self.assertEqual(result.stdout.count("| sample "), 9)
        self.assertEqual(result.stdout.count("| warmup "), 2)
        self.assertEqual((self.root / "summary.md").read_text(encoding="utf-8"), result.stdout)

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
