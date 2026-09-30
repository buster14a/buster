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
WORKFLOW = ROOT / ".github/workflows/native-retirement-admission.yml"
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
        self.assertNotIn("github.event.merge_group.head_sha", candidate)
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
        self.assertNotIn("wait-base", enforce)
        self.assertIn('--status-json "$RUNNER_TEMP/native-retirement-status.json"', enforce)
        self.assertNotIn("$GITHUB_WORKSPACE/validation", enforce)
        self.assertNotIn("--allow-pending", enforce)

    def test_group_check_has_only_the_trusted_reconciler_as_producer(self):
        # A merge_group trigger here would create a second (or skipped) group
        # check; the reconciler owns the exact merge-group check (#1811).
        events = WORKFLOW.read_text().split("\non:\n", 1)[1].split("\npermissions:", 1)[0]
        self.assertNotIn("merge_group", events)
        self.assertNotIn("workflow_dispatch", events)
        self.assertNotIn("wait-base", self.admission)
        policy = (ROOT / ".github/workflows/api-migration-policy.yml").read_text()
        self.assertNotIn("  native-retirement-admission:\n", policy)

    def test_rebinding_waits_on_trusted_main_before_classifying_second_group(self):
        workflow = (ROOT / ".github/workflows/native-retirement-rebind.yml").read_text()
        repository = workflow.split("  repository:\n", 1)[1]
        self.assertIn("    timeout-minutes: 310\n", repository)
        self.assertIn("(github.event_name == 'pull_request' || github.event_name == 'merge_group') && 'main'", repository)
        self.assertNotIn("github.event.pull_request.base.sha", repository)
        checkout = repository.index("      - name: Check out the previously trusted rebinder revision")
        pull_admission = repository.index("      - name: Reject feature-owned generated state")
        reconstruction = repository.index("      - name: Reconstruct the exact candidate or group state ephemerally")
        contract = repository.index("      - name: Run independent contract suite")
        wait = repository.index("      - name: Wait for the queued predecessor to land")
        admission = repository.index("      - name: Admit the landed merge group with trusted tools")
        freshness = repository.index("      - name: Require exact current generated state")
        # PRs are admitted against live main before the slow reconstruction.
        # Groups reconstruct speculatively (#1893); only the cheap wait and
        # the trusted gate run after the predecessor has landed.
        self.assertLess(checkout, pull_admission)
        self.assertLess(pull_admission, reconstruction)
        self.assertLess(reconstruction, contract)
        self.assertLess(contract, wait)
        self.assertLess(wait, admission)
        self.assertLess(admission, freshness)
        self.assertIn("if: ${{ github.event_name == 'pull_request' }}",
                      repository[pull_admission:reconstruction].split("      - name: Check out pinned", 1)[0])
        self.assertIn("if: ${{ github.event_name == 'merge_group' }}", repository[admission:freshness])
        self.assertNotIn("wait-base", repository[reconstruction:wait])
        first_wait = repository[wait:admission]
        self.assertIn("if: ${{ github.event_name == 'merge_group' }}", first_wait)
        self.assertIn("GH_TOKEN: ${{ github.token }}", first_wait)
        self.assertIn("trusted/tools/merge_queue_admission.py wait-base", first_wait)
        self.assertIn('--trusted-root "$GITHUB_WORKSPACE/trusted"', first_wait)
        self.assertIn('--wait-seconds 18000', first_wait)
        self.assertNotIn("continue-on-error", first_wait)

    @staticmethod
    def rebind_step(name: str) -> str:
        workflow = (ROOT / ".github/workflows/native-retirement-rebind.yml").read_text()
        block = workflow.split("      - name: " + name + "\n", 1)[1].split("      - name:", 1)[0]
        return textwrap.dedent(block.split("        run: |\n", 1)[1])

    def test_reconstruction_tolerates_only_a_behind_committed_pair(self):
        script = self.rebind_step("Reconstruct the exact candidate or group state ephemerally")
        fake = textwrap.dedent("""\
            import os, pathlib, sys
            command = sys.argv[1]
            root = pathlib.Path(sys.argv[sys.argv.index("--repo-root") + 1])
            header = root / "tools/native_retirement_dependency_binding.generated.h"
            if command == "refresh":
                header.write_text("#define FRESH 1\\n")
                if os.environ.get("SMUGGLE"):
                    (root / "README.md").write_text("changed by refresh\\n")
                print("{}")
                sys.exit(0)
            fresh = header.read_text() == "#define FRESH 1\\n"
            print("{}")
            sys.exit(0 if fresh else int(os.environ.get("STALE_EXIT", "2")))
            """)
        cases = (
            ("push", "2", "", 0, "false", True),
            ("merge_group", "2", "", 0, "false", False),
            ("push", "1", "", 1, None, False),
            ("pull_request", "2", "1", 1, "false", False),
        )
        for event, stale_exit, smuggle, code, current, warned in cases:
            with self.subTest(event=event, stale_exit=stale_exit, smuggle=smuggle), \
                    tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                candidate = root / "candidate"
                (candidate / "tools").mkdir(parents=True)
                (root / "trusted/tools").mkdir(parents=True)
                git(root, "init", "-q", "-b", "main", str(candidate))
                (candidate / "README.md").write_text("base\n")
                (candidate / "tools/native_retirement_dependency_binding.generated.h").write_text("#define OLD 1\n")
                for tool in (root / "trusted/tools", candidate / "tools"):
                    (tool / "native_retirement_rebind.py").write_text(fake)
                git(candidate, "add", ".")
                git(candidate, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-q", "-m", "base")
                output = root / "output"
                output.touch()
                body = script.replace("${{ github.event_name }}", event)
                result = subprocess.run(
                    ["bash", "-e", "-o", "pipefail", "-c", body], cwd=root,
                    env={**os.environ, "GITHUB_WORKSPACE": str(root), "RUNNER_TEMP": str(root),
                         "GITHUB_OUTPUT": str(output), "STALE_EXIT": stale_exit, "SMUGGLE": smuggle},
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(result.returncode, code, result.stderr + result.stdout)
                if current is not None:
                    self.assertIn("committed_current=" + current, output.read_text())
                self.assertEqual("catch-up is pending" in result.stdout, warned)

    def test_only_catch_up_heads_may_carry_a_behind_pair(self):
        script = self.rebind_step("Require exact current generated state for attested non-catch-up heads")
        cases = (
            ("trusted-integration", "false", "false", 1),
            ("trusted-integration-merge-group", "false", "false", 1),
            ("trusted-integration-merge-group", "true", "false", 0),
            ("trusted-integration", "false", "true", 0),
            ("ordinary-bound-merge-group", "false", "false", 0),
            ("ordinary-bound", "false", "false", 0),
        )
        for mode, catch_up, current, code in cases:
            with self.subTest(mode=mode, catch_up=catch_up, current=current):
                result = subprocess.run(
                    ["bash", "-e", "-o", "pipefail", "-c", script],
                    env={**os.environ, "MODE": mode, "CATCH_UP": catch_up, "COMMITTED_CURRENT": current},
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(result.returncode, code, result.stderr)

    def reserved_root_candidate(self, root: Path, track_external: bool) -> tuple[Path, str, str]:
        candidate = root / "candidate"
        git(root, "init", "-q", "-b", "main", str(candidate))
        git(candidate, "config", "user.name", "Admission Test")
        git(candidate, "config", "user.email", "test@example.invalid")
        (candidate / "README.md").write_text("base\n")
        git(candidate, "add", ".")
        git(candidate, "commit", "-q", "-m", "base")
        base = git(candidate, "rev-parse", "HEAD")
        (candidate / "README.md").write_text("feature\n")
        if track_external:
            (candidate / "external").mkdir()
            (candidate / "external/owned.txt").write_text("candidate-owned\n")
        git(candidate, "add", ".")
        git(candidate, "commit", "-q", "-m", "feature")
        head = git(candidate, "rev-parse", "HEAD")
        git(candidate, "remote", "add", "origin", str(candidate))
        return candidate, base, head

    def test_reserved_roots_are_rejected_before_pinned_materialization(self):
        workflow = (ROOT / ".github/workflows/native-retirement-rebind.yml").read_text()
        repository = workflow.split("  repository:\n", 1)[1]
        reject = repository.index("      - name: Reject candidate-controlled checkout destinations")
        self.assertLess(repository.index("      - name: Check out exact candidate or merge revision"), reject)
        self.assertLess(reject, repository.index("      - name: Check out pinned cJSON closure"))
        self.assertNotIn("if:", repository[reject:].split("        run: |\n", 1)[0])
        script = self.rebind_step("Reject candidate-controlled checkout destinations")
        for tracked, code in ((False, 0), (True, 1)):
            with self.subTest(tracked=tracked), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.reserved_root_candidate(root, tracked)
                result = subprocess.run(
                    ["bash", "-e", "-o", "pipefail", "-c", script],
                    env={**os.environ, "GITHUB_WORKSPACE": str(root)},
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(result.returncode, code, result.stderr)

    def test_group_admission_classifies_despite_materialized_closures(self):
        # Pinned closures already occupy candidate/external when a landed
        # group is admitted; only a candidate-tracked root may be rejected.
        script = self.rebind_step("Admit the landed merge group with trusted tools")
        gate = textwrap.dedent("""\
            import json, sys
            print(json.dumps({"mode": "ordinary-merge-group", "status": "admitted"}))
            """)
        for tracked, code in ((False, 0), (True, 1)):
            with self.subTest(tracked=tracked), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                candidate, base, head = self.reserved_root_candidate(root, tracked)
                (candidate / "external/cjson").mkdir(parents=True)
                (candidate / "external/cjson/cJSON.c").write_text("pinned\n")
                tools = root / "trusted/tools"
                tools.mkdir(parents=True)
                (tools / "native_retirement_merge_gate.py").write_text(gate)
                (tools / "native_retirement_integration.py").write_bytes(
                    (ROOT / "tools/native_retirement_integration.py").read_bytes())
                temp = root / "temp"
                temp.mkdir()
                result = subprocess.run(
                    ["bash", "-e", "-o", "pipefail", "-c", script], cwd=root,
                    env={**os.environ, "GITHUB_WORKSPACE": str(root), "RUNNER_TEMP": str(temp),
                         "GITHUB_OUTPUT": str(temp / "output"), "TRUSTED_REF": base,
                         "CANDIDATE_HEAD": head, "EVENT_NAME": "merge_group",
                         "GITHUB_REPOSITORY": "owner/repo"},
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(result.returncode, code, result.stderr)
                if code == 0:
                    self.assertIn('"kind":"ordinary"', result.stdout.replace(" ", ""))
                    self.assertEqual(git(candidate, "worktree", "list", "--porcelain").count("worktree "), 1)
                else:
                    self.assertIn("reserved materialization root 'external'", result.stderr)

    def test_main_advancing_between_candidate_and_trusted_checkouts_is_admitted(self):
        # candidate/ is fetched before trusted/ resolves main. A push landing
        # between the two checkouts leaves trusted/ equal to live main, so the
        # re-resolution loop never runs, yet the gate resolves that main inside
        # candidate/ (#2010). The stub gate performs exactly that resolution.
        gate = textwrap.dedent("""\
            import json, subprocess, sys
            arguments = sys.argv[2:]
            value = dict(zip(arguments[::2], arguments[1::2]))
            subprocess.run(["git", "-C", value["--repo-root"], "rev-parse", "--verify",
                            value["--base"] + "^{commit}"], check=True, capture_output=True)
            assert value["--base"] == value["--current-main"], value
            print(json.dumps({"status": "admitted", "mode": "trusted-integration", "base": value["--base"]}))
            """)
        steps = (
            (WORKFLOW, "Enforce trusted native-retirement integration"),
            (ROOT / ".github/workflows/native-retirement-rebind.yml",
             "Reject feature-owned generated state and classify trust transitions"),
        )
        for path, name in steps:
            block = path.read_text().split("      - name: " + name + "\n", 1)[1].split("      - name:", 1)[0]
            script = textwrap.dedent(block.split("        run: |\n", 1)[1])
            for advanced in (True, False):
                with self.subTest(workflow=path.name, advanced=advanced), \
                        tempfile.TemporaryDirectory() as directory:
                    workspace = Path(directory)
                    origin = workspace / "origin"
                    git(workspace, "init", "-q", "-b", "main", str(origin))
                    git(origin, "config", "user.name", "Admission Test")
                    git(origin, "config", "user.email", "test@example.invalid")
                    (origin / "source.txt").write_text("base\n")
                    git(origin, "add", ".")
                    git(origin, "commit", "-q", "-m", "base")
                    git(origin, "checkout", "-q", "-b", "feature")
                    (origin / "source.txt").write_text("feature\n")
                    git(origin, "commit", "-q", "-am", "feature")
                    head = git(origin, "rev-parse", "HEAD")
                    git(origin, "checkout", "-q", "main")
                    candidate = workspace / "candidate"
                    git(workspace, "clone", "-q", "--no-checkout", str(origin), str(candidate))
                    git(candidate, "checkout", "-q", "--detach", head)
                    if advanced:
                        (origin / "other.txt").write_text("landed meanwhile\n")
                        git(origin, "add", ".")
                        git(origin, "commit", "-q", "-m", "landed between checkouts")
                    main = git(origin, "rev-parse", "main")
                    trusted = workspace / "trusted"
                    git(workspace, "clone", "-q", "--branch", "main", str(origin), str(trusted))
                    (trusted / "tools").mkdir()
                    (trusted / "tools/native_retirement_merge_gate.py").write_text(gate)
                    bin_directory = workspace / "bin"
                    bin_directory.mkdir()
                    (bin_directory / "gh").write_text("#!/bin/sh\nprintf '[[]]\\n'\n")
                    (bin_directory / "gh").chmod(0o755)
                    temp = workspace / "temp"
                    temp.mkdir()
                    result = subprocess.run(
                        ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", script],
                        cwd=workspace,
                        env={**os.environ, "PATH": str(bin_directory) + os.pathsep + os.environ["PATH"],
                             "GITHUB_WORKSPACE": str(workspace), "RUNNER_TEMP": str(temp),
                             "GITHUB_OUTPUT": str(temp / "output"),
                             "GITHUB_REPOSITORY": "owner/repo", "EVENT_NAME": "pull_request",
                             "HEAD_SHA": head, "CANDIDATE_HEAD": head},
                        capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('"base": "' + main + '"', result.stdout)
                    self.assertNotIn("re-resolving trusted PR policy", result.stdout)
                    self.assertEqual(git(trusted, "rev-parse", "HEAD"), main)
                    self.assertEqual(git(candidate, "rev-parse", "HEAD"), head)
                    self.assertEqual(git(candidate, "status", "--porcelain"), "")

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
