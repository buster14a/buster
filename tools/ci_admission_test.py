#!/usr/bin/env python3
"""Exercise required-check admission under every CI_ENABLED value."""

import os
from pathlib import Path
import shutil
import subprocess
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CIAdmissionTests(unittest.TestCase):
    def workflow_bash(self):
        # Windows CreateProcess can choose System32/bash.exe (WSL)
        # before PATH. Use an absolute shell path; on Windows select
        # the installed Git Bash, not an unrelated WSL distribution.
        bash = shutil.which("bash")
        if os.name == "nt":
            git = shutil.which("git")
            self.assertIsNotNone(git, "Git for Windows is a CI prerequisite")
            bash = next((str(parent / "bin/bash.exe")
                         for parent in Path(git).resolve().parents
                         if (parent / "bin/bash.exe").is_file()), None)
        self.assertIsNotNone(bash, "Bash is a CI prerequisite")
        return bash

    def test_tcc_bootstrap_uses_exact_tested_revision(self):
        workflow = (ROOT / ".github/workflows/tcc-bootstrap.yml").read_text()
        checkout = workflow.split(
            "      - name: Check out exact tested source\n", 1)[1].split(
                "\n      - name:", 1)[0]
        self.assertIn("ref: ${{ github.sha }}", checkout)
        self.assertNotIn("pull_request.head.sha", checkout)

        identity = workflow.split(
            "      - name: Record exact tested source identity\n", 1)[1].split(
                "\n      - name:", 1)[0]
        for entry in (
                'test "$tested_sha" = "$GITHUB_SHA"',
                "tested_sha=$(git rev-parse HEAD)",
                "tested_tree=$(git rev-parse 'HEAD^{tree}')",
                "build_c_blob=$(git rev-parse HEAD:build.c)",
                "tested_checkout_sha=%s",
                "tested_tree_sha=%s",
                "build_c_blob_sha=%s"):
            with self.subTest(entry=entry):
                self.assertIn(entry, identity)

        for artifact in ("buster-hosted-preparation-native-proof",
                         "buster-hosted-sampling-packet-proof"):
            names = [line for line in workflow.splitlines()
                     if line.strip().startswith("name: " + artifact)]
            with self.subTest(artifact=artifact):
                self.assertEqual(len(names), 1, workflow)
                self.assertIn("${{ github.sha }}", names[0])
                self.assertNotIn("pull_request.head.sha", names[0])

    def test_required_checks_fail_when_ci_is_disabled(self):
        aggregate = (ROOT / ".github/workflows/ci.yml").read_text().split("\n  complete:", 1)[1]
        self.assertIn("if: ${{ always() && github.server_url == 'https://github.com' }}", aggregate)
        for name in ("tcc-bootstrap.yml", "gpu-toolchains.yml",
                     "bench-service-policy.yml", "api-migration-policy.yml"):
            text = (ROOT / ".github/workflows" / name).read_text()
            events = text.split("\non:\n", 1)[1].split("\npermissions:", 1)[0]
            self.assertIn("  pull_request:\n", events)
            self.assertIn("  merge_group:\n    types: [checks_requested]", events)
            self.assertNotIn("    paths:", events)
            job = text.split("\njobs:\n", 1)[1].split("    steps:\n", 1)[0]
            self.assertIn("needs: no_code_plan", job)
            self.assertIn("!cancelled() && (needs.no_code_plan.result != 'success' || needs.no_code_plan.outputs.no_code != 'true')", job)
            self.assertIn("github.server_url == 'https://github.com'", job)
            self.assertIn("uses: ./.github/workflows/ci-no-code-plan.yml", job)
            body = text.split("      - name: Require CI admission to be enabled\n", 1)[1]
            body = textwrap.dedent(body.split("        run: |\n", 1)[1].split("      - ", 1)[0])
            for value in ("true", "false", "", "TRUE"):
                with self.subTest(workflow=name, enabled=value):
                    result = subprocess.run([self.workflow_bash(), "--noprofile", "--norc", "-c", body],
                                            env=dict(os.environ, CI_ENABLED=value),
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode == 0, value == "true", result.stdout + result.stderr)

            for event in ("pull_request", "merge_group"):
                for plan in ("failure", "cancelled", "skipped", ""):
                    with self.subTest(workflow=name, event=event, plan=plan):
                        result = subprocess.run([self.workflow_bash(), "--noprofile", "--norc", "-c", body],
                                                env=dict(os.environ, CI_ENABLED="true",
                                                         EVENT_NAME=event, PLAN_RESULT=plan),
                                                capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
