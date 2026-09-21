#!/usr/bin/env python3
"""Keep workflow regressions separate from exact admission inputs (#932).

Use real local Git histories and execute the workflow's regression command.
The candidate predates the suite, while the synthetic merge has both the new
suite and the feature. No GitHub API, credentials or third-party modules are
needed; these tests do not publish an admission result.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


WORKFLOW = Path(__file__).resolve().parents[1] / ".github/workflows/api-migration-policy.yml"
SUITE = "tools/native_retirement_merge_gate_test.py"


def git(root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", os.fspath(root), *arguments],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise AssertionError("git failed: " + " ".join(arguments) + "\n" + result.stderr)
    return result.stdout.strip()


class WorkflowRevisionTests(unittest.TestCase):
    def setUp(self):
        self.admission = WORKFLOW.read_text().split("  native-retirement-admission:\n", 1)[1]

    def step(self, name: str) -> str:
        return self.admission.split("      - name: " + name + "\n", 1)[1].split("      - name:", 1)[0]

    def test_regression_checkout_is_the_immutable_event_revision(self):
        checkout = self.step("Check out immutable workflow regression revision")
        self.assertIn("ref: ${{ github.sha }}\n", checkout)
        self.assertIn("path: regression\n", checkout)
        self.assertIn("persist-credentials: false\n", checkout)
        self.assertNotIn("        if:", checkout)
        self.assertNotIn("refs/heads/main", checkout)
        self.assertNotIn("pull_request.head.sha", checkout)
        for name in ("Test native-retirement merge admission", "Test native-retirement workflow revision"):
            with self.subTest(step=name):
                step = self.step(name)
                self.assertIn("working-directory: regression\n", step)
                self.assertNotIn("continue-on-error", step)
                self.assertNotIn("        if:", step)
        self.assertIn("run: python3 -B " + SUITE + " -v\n", self.step("Test native-retirement merge admission"))
        self.assertIn("run: python3 -B tools/native_retirement_workflow_revision_test.py -v\n",
                      self.step("Test native-retirement workflow revision"))

    def test_exact_candidate_and_trusted_base_remain_separate(self):
        candidate = self.step("Check out exact candidate")
        trusted = self.step("Check out the previously trusted admission policy")
        enforcement = self.step("Enforce trusted native-retirement integration")
        self.assertIn("ref: ${{ github.event.pull_request.head.sha || github.event.merge_group.head_sha || github.sha }}\n", candidate)
        self.assertIn("path: candidate\n", candidate)
        self.assertIn("ref: ${{ github.event.pull_request.base.sha || github.event.merge_group.base_sha }}\n", trusted)
        self.assertIn("path: trusted\n", trusted)
        self.assertIn('--repo-root "$GITHUB_WORKSPACE/candidate"', enforcement)
        self.assertIn('tool="$GITHUB_WORKSPACE/trusted/tools/native_retirement_merge_gate.py"', enforcement)
        self.assertIn('--head "$HEAD_SHA"', enforcement)
        self.assertIn('--base "$BASE_SHA"', enforcement)
        self.assertIn('--current-main "$current_main"', enforcement)
        self.assertNotIn("regression/", enforcement)

    def exercise_revision(self, *, combined: bool, exit_code: int = 0) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            git(root, "init", "-b", "main", os.fspath(source))
            git(source, "config", "user.name", "Test User")
            git(source, "config", "user.email", "test@example.invalid")
            (source / "README.txt").write_text("base\n")
            git(source, "add", ".")
            git(source, "commit", "-m", "before admission suite")
            git(source, "checkout", "-b", "feature")
            (source / "README.txt").write_text("feature\n")
            git(source, "commit", "-am", "ordinary feature")
            head = git(source, "rev-parse", "HEAD")
            git(source, "checkout", "main")
            (source / "tools").mkdir()
            (source / "main-only.txt").write_text("policy\n")
            expected = "feature\n" if combined else "base\n"
            (source / SUITE).write_text(
                "from pathlib import Path\n"
                "assert Path('main-only.txt').read_text() == 'policy\\n'\n"
                f"assert Path('README.txt').read_text() == {expected!r}\n"
                f"raise SystemExit({exit_code})\n"
            )
            git(source, "add", ".")
            git(source, "commit", "-m", "introduce workflow suite")
            base = git(source, "rev-parse", "HEAD")
            if combined:
                git(source, "merge", "--no-ff", "-m", "immutable event merge", "feature")
            event = git(source, "rev-parse", "HEAD")
            for name, revision in (("candidate", head), ("trusted", base), ("regression", event)):
                git(source, "worktree", "add", "--detach", os.fspath(root / name), revision)
            step = self.step("Test native-retirement merge admission")
            working_directory = step.split("        working-directory: ", 1)[1].splitlines()[0]
            command = step.split("        run: ", 1)[1].splitlines()[0]
            # Reproduce the original failure independently: the real feature
            # head has neither the new suite nor the new main-only input.
            self.assertFalse((root / "candidate" / SUITE).exists())
            before = git(root / "candidate", "rev-parse", "HEAD")
            original = subprocess.run(["bash", "-e", "-c", command], cwd=root / "candidate", capture_output=True, text=True)
            self.assertEqual(original.returncode, 2, original.stderr)
            result = subprocess.run(["bash", "-e", "-c", command], cwd=root / working_directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, exit_code, result.stderr)
            self.assertEqual(git(root / "candidate", "rev-parse", "HEAD"), before)
            self.assertEqual(before, head)
            self.assertEqual(git(source, "rev-parse", "feature"), head)
            self.assertEqual(git(root / "candidate", "status", "--porcelain"), "")
            self.assertEqual(git(root / "trusted", "rev-parse", "HEAD"), base)
            self.assertEqual(git(root / "regression", "rev-parse", "HEAD"), event)

    def test_behind_main_pull_request_runs_new_suite_without_updating_head(self):
        self.exercise_revision(combined=True)

    def test_push_or_dispatch_runs_its_own_immutable_revision(self):
        self.exercise_revision(combined=False)

    def test_real_regression_failure_is_not_suppressed(self):
        self.exercise_revision(combined=True, exit_code=19)


if __name__ == "__main__":
    unittest.main()
