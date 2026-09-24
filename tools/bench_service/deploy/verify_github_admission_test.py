#!/usr/bin/env python3
"""Adversarial read-back fixtures for the benchmark environment preflight."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from verify_github_admission import verify


class AdmissionReadbackTest(unittest.TestCase):
    def setUp(self):
        self.environment = {
            "name": "benchmark-9700x",
            "deployment_branch_policy": {
                "protected_branches": False, "custom_branch_policies": True},
            "protection_rules": [{"type": "required_reviewers",
                                  "prevent_self_review": True,
                                  "reviewers": [{"type": "User", "reviewer": {
                                      "login": "davidgmbb", "id": 39247043}}]}],
        }
        self.branches = {"total_count": 1, "branch_policies": [
            {"name": "main", "type": "branch"}]}
        self.variable = {"name": "BENCH_SERVICE_DISPATCH_ENABLED",
                         "value": "false"}

    def check(self):
        verify(self.environment, self.branches, self.variable)

    def test_environment_and_disabled_admission(self):
        self.environment["protection_rules"].clear()
        with self.assertRaises(ValueError):
            self.check()
        self.environment["protection_rules"] = [{"type": "required_reviewers",
                                                  "prevent_self_review": True,
                                                  "reviewers": [{"type": "User", "reviewer": {
                                                      "login": "davidgmbb", "id": 39247043}}]}]
        reviewer = self.environment["protection_rules"][0]
        reviewer["prevent_self_review"] = False
        with self.assertRaises(ValueError):
            self.check()
        reviewer["prevent_self_review"] = True
        reviewer["reviewers"][0]["reviewer"]["login"] = "buster14a14a"
        with self.assertRaises(ValueError):
            self.check()
        reviewer["reviewers"][0]["reviewer"]["login"] = "davidgmbb"
        reviewer["reviewers"].append({"type": "User", "reviewer": {
            "login": "buster14a14a", "id": 1}})
        with self.assertRaises(ValueError):
            self.check()
        reviewer["reviewers"].pop()
        self.branches["branch_policies"].append({"name": "*", "type": "branch"})
        with self.assertRaises(ValueError):
            self.check()
        self.branches["branch_policies"].pop()
        self.variable["value"] = "true"
        with self.assertRaises(ValueError):
            self.check()

    def test_rejects_environment_and_branch_drift(self):
        self.environment["name"] = "unprotected"
        with self.assertRaises(ValueError):
            self.check()
        self.environment["name"] = "benchmark-9700x"
        self.branches["branch_policies"][0]["name"] = "release"
        with self.assertRaises(ValueError):
            self.check()

    def test_installer_preserves_existing_dispatch_state(self):
        installer = Path(__file__).with_name("configure_github_admission.sh")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mock = root / "gh"
            log = root / "calls"
            mock.write_text(
                '#!/bin/sh\n'
                'printf "%s\\n" "$*" >>"$GH_CALLS"\n'
                'if [ "$1" = auth ]; then exit 0; fi\n'
                'if [ "$1" = api ] && [ "$4" = '
                'repos/buster14a/buster/actions/variables/BENCH_SERVICE_DISPATCH_ENABLED ]; then\n'
                '  if [ "$GH_VALUE" = absent ]; then exit 1; fi\n'
                '  printf \'{"name":"BENCH_SERVICE_DISPATCH_ENABLED","value":"%s"}\\n\' "$GH_VALUE"\n'
                '  exit 0\n'
                'fi\n'
                'exit 1\n', encoding="utf-8")
            mock.chmod(0o755)
            for value in ("true", "absent", "false"):
                with self.subTest(value=value):
                    log.write_text("", encoding="utf-8")
                    env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}",
                               GH_CALLS=str(log), GH_VALUE=value)
                    result = subprocess.run(
                        ["bash", str(installer), "buster14a/buster"], env=env,
                        capture_output=True, text=True, check=False)
                    self.assertNotEqual(result.returncode, 0)
                    calls = log.read_text(encoding="utf-8")
                    self.assertNotIn("variable set", calls)
                    self.assertNotIn("--method", calls)
                    if value == "false":
                        self.assertIn("rulesets", calls)
                    else:
                        self.assertNotIn("rulesets", calls)


if __name__ == "__main__":
    unittest.main()
