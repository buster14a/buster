#!/usr/bin/env python3
"""Execute the 9700X dispatch recipe allowlist and wait-budget logic (#2071).

workflow_policy_test.py pins the workflow text. This test runs the literal
budget step script and the literal installed-recipe membership check from
.github/workflows/9700x-service-dispatch.yml under bash, with no gateway, so
the refusal of unknown recipes and the job-timeout cap are behaviour, not only
markers. It never contacts the service or the benchmark host.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workflow_policy_test as policy  # noqa: E402

DISPATCH_TEXT = policy.DISPATCH.read_text(encoding="utf-8")
SUBMIT = policy.job_blocks(DISPATCH_TEXT)["submit"]
BASH = shutil.which("bash")


def step_script(name: str) -> str:
    """The dedented `run: |` script of one named submit step."""
    header = f"      - name: {name}"
    start = SUBMIT.index(header)
    lines: list[str] = []
    collecting = False
    for line in SUBMIT[start + 1:]:
        if line.startswith("      - name: "):
            break
        if line == "        run: |":
            collecting = True
        elif collecting:
            if line.strip() and not line.startswith("          "):
                break
            lines.append(line)
    return textwrap.dedent("\n".join(lines)) + "\n"


BUDGET_SCRIPT = step_script("Select the reviewed recipe and its result-wait budget")
MEMBERSHIP = re.search(r'grep -Eq "([^"]*service-recipes=[^"]*)"',
                       step_script("Validate immutable request fields and installed gateway"))
JOB_TIMEOUT = policy.JOB_TIMEOUT_MINUTES * 60


@unittest.skipUnless(BASH, "bash is required to execute the workflow scripts")
class DispatchRecipeTest(unittest.TestCase):
    def run_budget(self, recipe: str, timeout: str = str(JOB_TIMEOUT)):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            output.write_text("", encoding="utf-8")
            env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "BQ_RECIPE": recipe,
                   "BQ_JOB_TIMEOUT_SECONDS": timeout, "GITHUB_OUTPUT": str(output)}
            before = int(time.time())
            completed = subprocess.run([BASH, "-c", BUDGET_SCRIPT], env=env, capture_output=True,
                                       text=True, timeout=30, check=False)
            after = int(time.time())
            text = output.read_text(encoding="utf-8")
        values = dict(line.split("=", 1) for line in text.splitlines())
        return completed, values, text, before, after

    def test_reviewed_recipes_derive_their_wait_from_the_budget(self):
        for recipe, budget in policy.REVIEWED_RECIPES:
            with self.subTest(recipe=recipe):
                completed, values, _, before, after = self.run_budget(recipe)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                wait = budget + policy.FINALIZATION_ALLOWANCE
                self.assertEqual(values["recipe"], recipe)
                self.assertEqual(int(values["runtime-budget"]), budget)
                self.assertEqual(int(values["wait-seconds"]), wait)
                job_deadline = int(values["job-deadline"])
                self.assertGreaterEqual(job_deadline, before + JOB_TIMEOUT - policy.JOB_MARGIN)
                self.assertLessEqual(job_deadline, after + JOB_TIMEOUT - policy.JOB_MARGIN)
                self.assertEqual(int(values["admission-deadline"]), job_deadline - wait)
                # The full wait after the latest admission still ends before the job timeout.
                self.assertLess(int(values["admission-deadline"]) + wait, after + JOB_TIMEOUT)

    def test_unknown_recipes_are_refused_without_outputs(self):
        for recipe in ("", "zen5-calibration-v2", "native-retirement-performance-v1", "fake-success-v1",
                       "Validate-buster-v1", "validate-buster-v1 ", "validate-buster-v1\n", " zen5-calibration-v1",
                       "$(touch pwned)", "validate-buster-v1;true", "*"):
            with self.subTest(recipe=recipe):
                completed, _, text, _, _ = self.run_budget(recipe)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("BENCH_DISPATCH_RECIPE_REFUSED", completed.stderr)
                self.assertEqual(text, "")

    def test_budget_never_exceeds_the_job_timeout(self):
        recipe, budget = policy.REVIEWED_RECIPES[-1]
        tight = budget + policy.FINALIZATION_ALLOWANCE + policy.JOB_MARGIN
        for timeout in (str(tight), str(tight - 1), "1"):
            with self.subTest(timeout=timeout):
                completed, _, text, _, _ = self.run_budget(recipe, timeout)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("BENCH_DISPATCH_BUDGET_REFUSED", completed.stderr)
                self.assertEqual(text, "")
        completed, values, _, _, _ = self.run_budget(recipe, str(tight + 1))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        for timeout in ("0", "-1", "7200s", "", "1234567", "0x10"):
            with self.subTest(timeout=timeout):
                completed, _, text, _, _ = self.run_budget(recipe, timeout)
                self.assertNotEqual(completed.returncode, 0)
                self.assertEqual(text, "")

    def test_installed_recipe_membership_is_exact(self):
        self.assertIsNotNone(MEMBERSHIP, "validation step must check service-recipes membership")
        pattern = MEMBERSHIP.group(1)
        script = 'grep -Eq "' + pattern + '" <<<"$capabilities"'
        current = ("schema=2 journal=3 legacy-journal=1 executor=supervisor pending=8 jobs=64\n"
                   "local-recipes=fake-success-v1,fake-failure-v1 service-recipes=validate-buster-v1 "
                   "blocked-recipes=native-retirement-performance-v1\n")
        cases = (
            (current, "validate-buster-v1", True),
            (current, "zen5-calibration-v1", False),
            (current.replace("service-recipes=validate-buster-v1",
                             "service-recipes=validate-buster-v1,zen5-calibration-v1"), "zen5-calibration-v1", True),
            (current.replace("service-recipes=validate-buster-v1",
                             "service-recipes=zen5-calibration-v1,validate-buster-v1"), "validate-buster-v1", True),
            ("service-recipes=validate-buster-v1-extra\n", "validate-buster-v1", False),
            ("service-recipes=xvalidate-buster-v1\n", "validate-buster-v1", False),
            ("xservice-recipes=zen5-calibration-v1\n", "zen5-calibration-v1", False),
            ("blocked-recipes=zen5-calibration-v1\n", "zen5-calibration-v1", False),
            ("local-recipes=zen5-calibration-v1 service-recipes=validate-buster-v1\n", "zen5-calibration-v1", False),
            ("service-recipes=\n", "validate-buster-v1", False),
        )
        for capabilities, recipe, expected in cases:
            with self.subTest(capabilities=capabilities, recipe=recipe):
                completed = subprocess.run([BASH, "-c", script], env={
                    "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "BQ_RECIPE": recipe,
                    "capabilities": capabilities}, capture_output=True, text=True, timeout=30, check=False)
                self.assertEqual(completed.returncode == 0, expected, completed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
