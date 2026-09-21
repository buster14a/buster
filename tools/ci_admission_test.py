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

    def test_required_checks_fail_when_ci_is_disabled(self):
        aggregate = (ROOT / ".github/workflows/ci.yml").read_text().split("\n  complete:", 1)[1]
        self.assertIn("if: ${{ always() && github.server_url == 'https://github.com' }}", aggregate)
        for name in ("self-host-audit.yml", "tcc-bootstrap.yml", "gpu-toolchains.yml",
                     "bench-service-policy.yml", "api-migration-policy.yml"):
            text = (ROOT / ".github/workflows" / name).read_text()
            events = text.split("\non:\n", 1)[1].split("\npermissions:", 1)[0]
            self.assertIn("  pull_request:\n", events)
            self.assertIn("  merge_group:\n    types: [checks_requested]", events)
            self.assertNotIn("    paths:", events)
            job = text.split("\njobs:\n", 1)[1].split("    steps:\n", 1)[0]
            self.assertIn("if: ${{ github.server_url == 'https://github.com' }}", job)
            body = text.split("      - name: Require CI admission to be enabled\n", 1)[1]
            body = textwrap.dedent(body.split("        run: |\n", 1)[1].split("      - ", 1)[0])
            for value in ("true", "false", "", "TRUE"):
                with self.subTest(workflow=name, enabled=value):
                    result = subprocess.run([self.workflow_bash(), "--noprofile", "--norc", "-c", body],
                                            env=dict(os.environ, CI_ENABLED=value),
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode == 0, value == "true", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
