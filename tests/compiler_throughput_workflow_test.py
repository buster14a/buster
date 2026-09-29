#!/usr/bin/env python3
"""Offline regression for hosted compiler-throughput request retention."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github/workflows"
PR_WORKFLOW = WORKFLOWS / "compiler-throughput.yml"
REQUEST_WORKFLOW = WORKFLOWS / "compiler-throughput-requests.yml"


def top_level_section(source, key):
    match = re.search(rf"^{re.escape(key)}:\s*\n(.*?)(?=^[a-z][a-z-]*:|\Z)",
                      source, re.MULTILINE | re.DOTALL)
    if match is None:
        raise AssertionError(f"missing top-level {key} section")
    return match.group(1)


class CompilerThroughputWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pr = PR_WORKFLOW.read_text(encoding="utf-8")
        cls.requests = REQUEST_WORKFLOW.read_text(encoding="utf-8")

    def test_pr_latest_head_and_same_commit_reuse(self):
        triggers = top_level_section(self.pr, "on")
        concurrency = top_level_section(self.pr, "concurrency")
        self.assertIn("  pull_request:\n", triggers)
        self.assertIn("  workflow_call:\n", triggers)
        self.assertIn("      baseline_ref:\n", triggers)
        self.assertIn("        default: main\n", triggers)
        self.assertIn("        type: string\n", triggers)
        self.assertNotIn("workflow_dispatch:", triggers)
        self.assertNotIn("schedule:", triggers)
        self.assertIn("github.event_name == 'pull_request' && github.event.pull_request.number || github.run_id", concurrency)
        self.assertIn("  cancel-in-progress: ${{ github.event_name == 'pull_request' }}", concurrency)
        self.assertNotIn("queue:", concurrency)

    def test_manual_and_schedule_share_a_bounded_non_replacing_queue(self):
        triggers = top_level_section(self.requests, "on")
        concurrency = top_level_section(self.requests, "concurrency")
        jobs = top_level_section(self.requests, "jobs")
        self.assertIn("  workflow_dispatch:\n", triggers)
        self.assertIn("  schedule:\n", triggers)
        self.assertIn("        default: main\n", triggers)
        self.assertIn("  group: compiler-throughput-requests\n", concurrency)
        self.assertIn("  queue: max\n", concurrency)
        self.assertIn("  cancel-in-progress: false\n", concurrency)
        self.assertIn("uses: ./.github/workflows/compiler-throughput.yml", jobs)
        self.assertIn("baseline_ref: ${{ inputs.baseline_ref || 'main' }}", jobs)
        self.assertNotIn("secrets: inherit", self.requests)
        self.assertEqual(top_level_section(self.requests, "permissions").split("#", 1)[0].strip(),
                         "contents: read")

    def test_three_overlapping_requests_and_overflow(self):
        # GitHub's max policy retains 100 pending runs after the active one.
        # Both event sources use the one literal group above, so this models
        # manual -> manual -> schedule -> manual against a long active run.
        concurrency = top_level_section(self.requests, "concurrency")
        self.assertIn("  queue: max\n", concurrency)
        self.assertNotIn("${{", re.search(r"^  group: (.*)$", concurrency,
                                          re.MULTILINE).group(1))
        pending = []
        rejected = []
        for request in ["manual-1", "manual-2", "schedule-1", "manual-3"]:
            if len(pending) < 100:
                pending.append(request)
            else:
                rejected.append(request)
        self.assertEqual(pending, ["manual-1", "manual-2", "schedule-1", "manual-3"])
        for index in range(97):
            request = f"manual-{index + 4}"
            if len(pending) < 100:
                pending.append(request)
            else:
                rejected.append(request)
        self.assertEqual(len(pending), 100)
        self.assertEqual(rejected, ["manual-100"])
        self.assertEqual(pending[:4], ["manual-1", "manual-2", "schedule-1", "manual-3"])

    def test_only_reviewed_queue_uses_actionlint_exception(self):
        queued = [path for path in WORKFLOWS.glob("*.yml")
                  if re.search(r"^\s+queue:", path.read_text(), re.MULTILINE)]
        self.assertEqual(queued, [REQUEST_WORKFLOW])
        ci = (WORKFLOWS / "ci.yml").read_text(encoding="utf-8")
        self.assertIn("python3 tests/compiler_throughput_workflow_test.py", ci)
        self.assertIn("-ignore 'unexpected key \"queue\" for \"concurrency\" section'", ci)


if __name__ == "__main__":
    unittest.main()
