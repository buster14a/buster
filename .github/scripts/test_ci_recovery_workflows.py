#!/usr/bin/env python3
"""Workflow selection and attribution checks kept outside the frozen corpus."""

import importlib.util
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "recovery", ROOT / ".github/scripts/recover-ci.py")
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)


class LifecycleWorkflowTests(unittest.TestCase):
    def test_filtered_handlers_and_source_change_tests(self):
        workflows = ROOT / ".github/workflows"
        recover = (workflows / "ci-recovery.yml").read_text()
        watch = (workflows / "ci-merge-group-watch.yml").read_text()
        regressions = (workflows / "ci-recovery-tests.yml").read_text()

        # Each upstream delivery creates at most one handler check. Ordinary
        # PR starts and main completions do not create handler workflows.
        self.assertIn("types: [completed]", recover)
        self.assertIn("branches-ignore:\n      - main\n      - 'gh-readonly-queue/main/**'", recover)
        self.assertEqual(re.findall(r"^  ([\w-]+):$", recover.split("\njobs:\n", 1)[1], re.M),
                         ["recover"])
        self.assertIn("types: [in_progress, completed]", watch)
        self.assertIn("branches:\n      - 'gh-readonly-queue/main/**'", watch)
        self.assertIn("- cron: '13-59/15 * * * *'", watch)
        self.assertIn("workflow_dispatch:", watch)
        for name in ("Buster CI", "Self-host fixed point", "TCC bootstrap",
                     "GPU toolchain acceptance", "Benchmark service workflow policy",
                     "API migration policy", "Main integration admission"):
            self.assertIn("      - " + name + "\n", watch)
        self.assertEqual(re.findall(r"^  ([\w-]+):$", watch.split("\njobs:\n", 1)[1], re.M),
                         ["watch-merge-group"])
        self.assertIn("group: ci-recovery-${{ github.event.workflow_run.id }}", recover)
        self.assertIn("group: ci-recovery-${{ github.event.workflow_run.id || github.run_id }}", watch)
        for workflow in (recover, watch):
            self.assertIn("ref: ${{ github.sha }}", workflow)
            self.assertIn("github.event.workflow_run.head_repository.full_name == github.repository", workflow)
            self.assertIn("run-name:", workflow)
            self.assertNotIn("\n  test:\n", workflow)
        self.assertNotIn("workflow_run:", regressions)
        self.assertIn("persist-credentials: false", regressions)
        for path in ("ci-recovery.yml", "ci-merge-group-watch.yml", "ci-recovery-tests.yml"):
            self.assertEqual(regressions.count("'.github/workflows/" + path + "'"), 2)
        for path in (".github/scripts/recover-ci.py", ".github/scripts/test_merge_queue_fail_fast.py",
                     "tests/ci_recovery_test.py", ".github/scripts/test_ci_recovery_workflows.py"):
            self.assertEqual(regressions.count("'" + path + "'"), 2)

    def test_summary_distinguishes_upstream_and_trusted_handler(self):
        event = {
            "action": "completed",
            "workflow_run": {
                "id": 123, "run_attempt": 1, "event": "pull_request",
                "head_branch": "fix/<retained>", "head_sha": "a" * 40,
            },
        }
        summary = recovery.lifecycle_summary(
            "Buster CI lifecycle no action", "No action: failed job <test>", event,
            "buster14a/buster", "b" * 40, 234, 2)
        self.assertIn("/actions/runs/123/attempts/1", summary)
        self.assertIn("/actions/runs/234/attempts/2", summary)
        self.assertIn("<code>fix/&lt;retained&gt;</code>", summary)
        self.assertIn("<code>" + "a" * 40 + "</code>", summary)
        self.assertIn("<code>" + "b" * 40 + "</code>", summary)
        self.assertIn("No action: failed job &lt;test&gt;", summary)

    def test_sweep_summary_identifies_trusted_handler(self):
        summary = recovery.lifecycle_summary(
            "Merge-group CI fail-fast", "No live merge groups.",
            {"schedule": "13-59/15 * * * *"}, "buster14a/buster", "b" * 40, 234, 1)
        self.assertIn("Trigger: <code>13-59/15 * * * *</code>", summary)
        self.assertIn("/actions/runs/234/attempts/1", summary)
        self.assertIn("No live merge groups.", summary)


if __name__ == "__main__":
    unittest.main()
