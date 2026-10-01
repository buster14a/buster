#!/usr/bin/env python3
"""Offline regressions for the bounded local artifact-upload retry.

Inspect the action's restricted block-style steps and execute its Bash reporting
bodies. This covers failures after upload-artifact starts; runner dependency
resolution before composite execution is tracked separately in #1790.
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import check_action_pins


ROOT = Path(__file__).resolve().parents[1]
ACTION = ROOT / ".github/actions/native-artifact-upload/action.yml"
UPLOAD_PIN = "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"
FAILED_FIRST = "${{ !cancelled() && steps.artifact_upload.outcome == 'failure' }}"
RECOVERED = ("${{ !cancelled() && steps.artifact_upload.outcome == 'failure' && "
             "steps.artifact_upload_retry.outcome == 'success' }}")
FAILED_BOTH = ("${{ always() && !cancelled() && steps.artifact_upload.outcome == 'failure' && "
               "steps.artifact_upload_retry.outcome == 'failure' }}")
UPLOAD_INPUTS = {
    "name": "${{ inputs.name }}",
    "path": "${{ inputs.path }}",
    "compression-level": "${{ inputs.compression-level }}",
    "if-no-files-found": "${{ inputs.if-no-files-found }}",
    "retention-days": "${{ inputs.retention-days }}",
}


def scalar(block, key, indent=6):
    matches = re.findall(rf"(?m)^{' ' * indent}{re.escape(key)}: (.+)$", block)
    if len(matches) > 1:
        raise AssertionError(f"duplicate {key} in action step")
    return matches[0] if matches else None


def steps(text):
    # The pin scanner enforces restricted YAML; inspect each actual step rather
    # than allowing a condition, pin or overwrite in another step to satisfy it.
    starts = [match.start() for match in re.finditer(r"(?m)^    - name: .+$", text)]
    ends = starts[1:] + [len(text)]
    return [text[start:end] for start, end in zip(starts, ends)]


def script(block):
    value = scalar(block, "run")
    if value == "|":
        body = block.split("      run: |\n", 1)[1]
        lines = []
        for line in body.splitlines():
            if line.strip() and not line.startswith("        "):
                raise AssertionError("unexpected reporting-script indentation")
            lines.append(line[8:] if line.strip() else "")
        value = "\n".join(lines) + "\n"
    if value is None:
        raise AssertionError("missing reporting script")
    return value


class ArtifactUploadTests(unittest.TestCase):
    def setUp(self):
        self.text = ACTION.read_text(encoding="utf-8")
        self.steps = steps(self.text)
        self.assertEqual(len(self.steps), 5, "one upload, backoff, one retry and two reports")
        self.initial, self.backoff, self.retry, self.recovered, self.failed = self.steps

    def test_exactly_two_reviewed_upload_attempts(self):
        self.assertEqual(check_action_pins.check_text(self.text, ACTION), [])
        self.assertIn("runs:\n  using: composite\n  steps:\n", self.text)
        uploads = [step for step in self.steps if scalar(step, "uses") is not None]
        self.assertEqual(uploads, [self.initial, self.retry])
        self.assertEqual([scalar(step, "uses") for step in uploads], [UPLOAD_PIN, UPLOAD_PIN])
        self.assertEqual(scalar(self.initial, "id"), "artifact_upload")
        self.assertEqual(scalar(self.retry, "id"), "artifact_upload_retry")

    def test_only_initial_failure_can_be_ignored(self):
        self.assertEqual(scalar(self.initial, "continue-on-error"), "true")
        for step in self.steps[1:]:
            with self.subTest(step=step.splitlines()[0]):
                self.assertIn(scalar(step, "continue-on-error"), (None, "false"))

    def test_failure_guards_and_cancellation_suppression(self):
        self.assertEqual(scalar(self.initial, "if"), "${{ !cancelled() }}")
        # outcome retains the first failure despite continue-on-error; conclusion
        # would become success and silently suppress the required second attempt.
        self.assertEqual(scalar(self.backoff, "if"), FAILED_FIRST)
        self.assertEqual(scalar(self.retry, "if"), FAILED_FIRST)
        self.assertEqual(scalar(self.recovered, "if"), RECOVERED)
        # always() makes the terminal report reachable after the blocking retry.
        self.assertEqual(scalar(self.failed, "if"), FAILED_BOTH)

    def test_retry_replaces_the_same_artifact_with_identical_options(self):
        for step, expected in ((self.initial, UPLOAD_INPUTS),
                               (self.retry, dict(UPLOAD_INPUTS, overwrite="true"))):
            with self.subTest(step=scalar(step, "id")):
                self.assertEqual(step.count("      with:\n"), 1)
                pairs = re.findall(r"(?m)^        ([a-z][a-z-]*): (.+)$", step)
                self.assertEqual(len(pairs), len(expected))
                self.assertEqual(dict(pairs), expected)

    def execute_report(self, step, backoff=False):
        self.assertEqual(scalar(step, "shell"), "bash")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            summary = root / "summary.md"
            sleep_record = root / "sleep.txt"
            environment = dict(os.environ, GITHUB_STEP_SUMMARY=str(summary),
                               ARTIFACT_LABEL="native", ARTIFACT_TITLE="Native")
            if backoff:
                # Run the actual Bash body without waiting or contacting Actions.
                sleeper = root / "sleep"
                sleeper.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$*" >> "$SLEEP_RECORD"\n',
                                   encoding="utf-8")
                sleeper.chmod(0o755)
                environment["PATH"] = str(root) + os.pathsep + environment["PATH"]
                environment["SLEEP_RECORD"] = str(sleep_record)
            process = subprocess.run(["bash", "-euo", "pipefail", "-c", script(step)],
                                     env=environment, capture_output=True, text=True, timeout=5)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertTrue(summary.is_file(), "report must append to GITHUB_STEP_SUMMARY")
            result = (process.stdout, summary.read_text(encoding="utf-8"),
                      sleep_record.read_text(encoding="utf-8") if sleep_record.exists() else None)
        return result

    def test_backoff_warns_and_waits_once_for_fifteen_seconds(self):
        self.assertEqual(scalar(self.backoff, "ARTIFACT_TITLE", 8), "${{ inputs.label_title }}")
        stdout, summary, delay = self.execute_report(self.backoff, backoff=True)
        self.assertIn("::warning::Native artifact upload failed; retrying once after 15 seconds.", stdout)
        self.assertIn("Initial upload failed; retrying once after a 15-second backoff.", summary)
        self.assertEqual(delay, "15\n")

    def test_successful_retry_records_recovery(self):
        self.assertEqual(scalar(self.recovered, "ARTIFACT_LABEL", 8), "${{ inputs.label }}")
        stdout, summary, delay = self.execute_report(self.recovered)
        self.assertEqual(stdout, "")
        self.assertIn("Retry succeeded; native evidence was retained.", summary)
        self.assertIsNone(delay)

    def test_failed_retry_emits_error_and_records_lost_evidence(self):
        self.assertEqual(scalar(self.failed, "ARTIFACT_TITLE", 8), "${{ inputs.label_title }}")
        self.assertEqual(scalar(self.failed, "ARTIFACT_LABEL", 8), "${{ inputs.label }}")
        stdout, summary, delay = self.execute_report(self.failed)
        self.assertIn("::error::Native artifact upload retry failed; evidence was not retained.", stdout)
        self.assertIn("native artifact upload retry failed; evidence was not retained.", summary)
        self.assertNotIn("Retry succeeded", summary)
        self.assertIsNone(delay)

    def test_required_workflow_lint_runs_this_suite(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        lint = workflow.split("\n  lint:\n", 1)[1].split("\n  desktop:\n", 1)[0]
        validation = lint.split("      - name: Validate approved action references\n", 1)[1]
        validation = validation.split("\n      - ", 1)[0]
        self.assertIn("          set -euo pipefail\n", validation)
        self.assertEqual(validation.count("          python3 tools/ci_artifact_upload_test.py\n"), 1)


if __name__ == "__main__":
    unittest.main()
