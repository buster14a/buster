#!/usr/bin/env python3
"""Regression tests for admission self-tests on clean, older feature heads.

The combined tree supplies tests; it never supplies merge-admission authority.
The real workflow shell body is also executed against a temporary Git history
where main introduces the tests after the feature branch has diverged (#932).
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/api-migration-policy.yml"
SUITES = (
    "native_retirement_merge_gate_test.py",
    "native_retirement_merge_gate_workflow_test.py",
)


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", str(repo), *arguments],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise AssertionError(result.stderr or result.stdout)
    return result.stdout.strip()


class AdmissionWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.admission = WORKFLOW.read_text().split(
            "  native-retirement-admission:\n", 1
        )[1]

    def step(self, name: str) -> str:
        matches = re.findall(
            r"^      - name: " + re.escape(name) + r"\n(.*?)(?=^      - name:|\Z)",
            self.admission, re.MULTILINE | re.DOTALL,
        )
        self.assertEqual(len(matches), 1, name)
        return matches[0]

    def script(self) -> str:
        step = self.step("Test native-retirement merge admission")
        return textwrap.dedent(step.split("        run: |\n", 1)[1])

    def enforce_script(self) -> str:
        step = self.step("Enforce trusted native-retirement integration")
        return textwrap.dedent(step.split("        run: |\n", 1)[1])

    def test_selftests_use_immutable_combined_checkout_not_old_head(self):
        checkout = self.step("Check out exact combined validation tree")
        self.assertIn("          ref: ${{ github.sha }}\n", checkout)
        self.assertIn("          path: validation\n", checkout)
        self.assertIn("          fetch-depth: 1\n", checkout)
        self.assertIn("          persist-credentials: false\n", checkout)
        self.assertNotIn("        if:", checkout)
        test = self.step("Test native-retirement merge admission")
        self.assertIn("        shell: bash\n", test)
        self.assertIn("        working-directory: validation\n", test)
        self.assertNotIn("        if:", test)
        self.assertNotIn("continue-on-error", test)
        self.assertEqual(self.script().strip().splitlines(), [
            "python3 -B tools/" + name + " -v" for name in SUITES
        ])

    def test_exact_candidate_and_live_main_keep_admission_authority(self):
        candidate = self.step("Check out exact candidate")
        self.assertIn("github.event.pull_request.head.sha", candidate)
        self.assertIn("github.event.merge_group.head_sha", candidate)
        self.assertIn("          path: candidate\n", candidate)
        self.assertIn("          persist-credentials: false\n", candidate)
        trusted = self.step("Check out the independently trusted admission policy")
        self.assertIn("          ref: main\n", trusted)
        self.assertNotIn("github.event.pull_request.base.sha", trusted)
        self.assertIn("          path: trusted\n", trusted)
        self.assertIn("          persist-credentials: false\n", trusted)
        enforce = self.step("Enforce trusted native-retirement integration")
        self.assertIn('tool="$GITHUB_WORKSPACE/trusted/tools/native_retirement_merge_gate.py"', enforce)
        self.assertIn('--repo-root "$GITHUB_WORKSPACE/candidate"', enforce)
        self.assertIn('--base "$BASE_SHA"', enforce)
        self.assertIn('--head "$HEAD_SHA"', enforce)
        self.assertIn('--current-main "$current_main"', enforce)
        self.assertIn('trusted/tools/merge_queue_admission.py" wait-base', enforce)
        self.assertIn('--wait-seconds 18000', enforce)
        self.assertIn('--wait-seconds 0', enforce)
        self.assertNotIn("$GITHUB_WORKSPACE/validation", enforce)
        self.assertNotIn("--allow-pending", enforce)

    def test_first_queue_group_uses_older_trusted_gate_and_rejects_speculative_base(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            trusted = workspace / "trusted"
            git(workspace, "init", "-b", "main", str(trusted))
            git(trusted, "config", "user.name", "Admission Test")
            git(trusted, "config", "user.email", "test@example.invalid")
            (trusted / "tools").mkdir()
            # This represents main before this PR: its admission CLI has no
            # wait-base command, while its native gate can check a landed base.
            (trusted / "tools/merge_queue_admission.py").write_text(
                "raise SystemExit(2)\n"
            )
            (trusted / "tools/native_retirement_merge_gate.py").write_text(
                "import sys\n"
                "assert sys.argv[1] == 'check', sys.argv\n"
                "print('trusted native gate passed')\n"
            )
            git(trusted, "add", "tools")
            git(trusted, "commit", "-m", "older trusted policy")
            base = git(trusted, "rev-parse", "HEAD")
            git(workspace, "clone", str(trusted), str(workspace / "candidate"))
            (workspace / "event.json").write_text("{}")
            env = {**os.environ, "GITHUB_WORKSPACE": str(workspace),
                   "RUNNER_TEMP": str(workspace), "GITHUB_EVENT_PATH": str(workspace / "event.json"),
                   "GITHUB_REPOSITORY": "owner/repo", "GITHUB_SHA": "a" * 40,
                   "EVENT_NAME": "merge_group", "HEAD_SHA": "a" * 40}

            def enforce(base_sha: str) -> subprocess.CompletedProcess:
                return subprocess.run(
                    ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail",
                     "-c", self.enforce_script()], cwd=workspace,
                    env={**env, "BASE_SHA": base_sha},
                    capture_output=True, text=True, check=False,
                )

            first = enforce(base)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertIn("trusted native gate passed", first.stdout)
            later = enforce("b" * 40)
            self.assertNotEqual(later.returncode, 0)
            self.assertIn("requires trusted main with wait-base", later.stderr)
            self.assertNotIn("trusted native gate passed", later.stdout)

    def test_rebinding_waits_on_trusted_main_before_classifying_second_group(self):
        workflow = (ROOT / ".github/workflows/native-retirement-rebind.yml").read_text()
        repository = workflow.split("  repository:\n", 1)[1]
        self.assertIn("    timeout-minutes: 310\n", repository)
        self.assertIn("(github.event_name == 'pull_request' || github.event_name == 'merge_group') && 'main'", repository)
        self.assertNotIn("github.event.pull_request.base.sha", repository)
        checkout = repository.index("      - name: Check out the previously trusted rebinder revision")
        wait = repository.index("      - name: Wait for the queued predecessor to land")
        admission = repository.index("      - name: Reject feature-owned generated state")
        self.assertLess(checkout, wait)
        self.assertLess(wait, admission)
        first_wait = repository[wait:admission]
        self.assertIn("if: ${{ github.event_name == 'merge_group' }}", first_wait)
        self.assertIn("GH_TOKEN: ${{ github.token }}", first_wait)
        self.assertIn("trusted/tools/merge_queue_admission.py wait-base", first_wait)
        self.assertIn('--trusted-root "$GITHUB_WORKSPACE/trusted"', first_wait)
        self.assertIn('--wait-seconds 18000', first_wait)
        self.assertNotIn("continue-on-error", first_wait)

    def history(self, root: Path) -> tuple[Path, Path, Path, str, str]:
        repo = root / "repo"
        git(root, "init", "-b", "main", str(repo))
        git(repo, "config", "user.name", "Admission Test")
        git(repo, "config", "user.email", "test@example.invalid")
        (repo / "source.txt").write_text("base\n")
        git(repo, "add", ".")
        git(repo, "commit", "-m", "base before admission rollout")
        git(repo, "checkout", "-b", "feature")
        (repo / "source.txt").write_text("feature\n")
        git(repo, "commit", "-am", "unrelated feature")
        head = git(repo, "rev-parse", "HEAD")
        git(repo, "checkout", "main")
        (repo / "tools").mkdir()
        for index, name in enumerate(SUITES):
            # These stand-ins exercise the workflow, not the admission algorithm
            # (whose full existing test suite remains independently mandatory).
            (repo / "tools" / name).write_text(
                "import os\nfrom pathlib import Path\n"
                "value = Path('source.txt').read_text()\n"
                "assert value == 'feature\\n', value\n"
                f"with Path('trace').open('a') as trace: trace.write('{index}:' + value)\n"
                f"raise SystemExit({index + 7} if os.environ.get('FAIL_SUITE') == '{index}' else 0)\n"
            )
        git(repo, "add", "tools")
        git(repo, "commit", "-m", "introduce admission tests on main")
        base = git(repo, "rev-parse", "HEAD")
        candidate = root / "candidate"
        git(repo, "worktree", "add", "--detach", str(candidate), head)
        git(repo, "merge", "--no-commit", "--no-ff", head)
        tree = git(repo, "write-tree")
        combined = git(repo, "commit-tree", tree, "-p", base, "-p", head,
                       "-m", "immutable combined validation revision")
        validation = root / "validation"
        git(repo, "worktree", "add", "--detach", str(validation), combined)
        return repo, candidate, validation, base, head

    def run_script(self, directory: Path, fail: str = "") -> subprocess.CompletedProcess:
        return subprocess.run(
            ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", self.script()],
            cwd=directory, env={**os.environ, "FAIL_SUITE": fail},
            capture_output=True, text=True, check=False,
        )

    def test_clean_old_head_uses_new_main_tests_without_branch_update(self):
        with tempfile.TemporaryDirectory() as directory:
            repo, candidate, validation, base, head = self.history(Path(directory))
            self.assertFalse((candidate / "tools" / SUITES[0]).exists())
            old = self.run_script(candidate)
            self.assertEqual(old.returncode, 2, old.stderr)
            actual = self.run_script(validation)
            self.assertEqual(actual.returncode, 0, actual.stderr)
            self.assertEqual((validation / "trace").read_text(), "0:feature\n1:feature\n")
            self.assertEqual(git(repo, "rev-parse", "feature"), head)
            self.assertEqual(git(repo, "rev-parse", "main"), base)
            self.assertEqual(git(candidate, "rev-parse", "HEAD"), head)

    def test_existing_admission_test_failure_is_not_hidden(self):
        with tempfile.TemporaryDirectory() as directory:
            _, _, validation, _, _ = self.history(Path(directory))
            result = self.run_script(validation, "0")
            self.assertEqual(result.returncode, 7, result.stderr)
            self.assertEqual((validation / "trace").read_text(), "0:feature\n")

    def test_new_workflow_test_failure_is_not_hidden(self):
        with tempfile.TemporaryDirectory() as directory:
            _, _, validation, _, _ = self.history(Path(directory))
            result = self.run_script(validation, "1")
            self.assertEqual(result.returncode, 8, result.stderr)
            self.assertEqual((validation / "trace").read_text(), "0:feature\n1:feature\n")


if __name__ == "__main__":
    unittest.main()
