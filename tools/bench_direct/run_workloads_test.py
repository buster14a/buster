#!/usr/bin/env python3
"""Exercise the direct 9700X workload harness against a disposable repository."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().with_name("run_workloads.py")
COMPILER = shutil.which("clang") or shutil.which("cc")
PASSING = '#include <stdio.h>\nint main(void) { puts("self-check ok checksum=2a"); return 0; }\n'
FAILING = "int main(void) { return 3; }\n"


def git(repository: Path, *arguments: str) -> str:
    environment = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid",
                   "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.invalid"}
    return subprocess.run(["git", "-C", str(repository), *arguments], check=True,
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

    def test_compile_error_fails_without_running(self) -> None:
        result = self.run_harness(self.commit({"benchmarks/9700x/bad.c": "int main(void) { return x; }\n"}))
        self.assertEqual(result.returncode, 1)
        self.assertIn("Compilation failed:", result.stdout)
        self.assertNotIn("| sample ", result.stdout)


if __name__ == "__main__":
    unittest.main()
