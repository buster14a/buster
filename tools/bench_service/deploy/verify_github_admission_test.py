#!/usr/bin/env python3
"""Adversarial read-back fixtures for benchmark admission."""

import copy
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from verify_github_admission import verify


class AdmissionReadbackTest(unittest.TestCase):
    def setUp(self):
        self.policy = {
            "name": "Benchmark dispatch main protection",
            "enforcement": "active",
            "bypass_actors": [],
            "rules": [{"type": "required_status_checks", "parameters": {
                "strict_required_status_checks_policy": False}}],
        }
        self.ruleset = copy.deepcopy(self.policy)
        self.ruleset.update(id=42, source_type="Repository",
                            current_user_can_bypass="never")
        self.environment = {
            "name": "benchmark-9700x",
            "deployment_branch_policy": {
                "protected_branches": False, "custom_branch_policies": True},
            "protection_rules": [],
        }
        self.branches = {"total_count": 1, "branch_policies": [
            {"name": "main", "type": "branch"}]}
        self.variable = {"name": "BENCH_SERVICE_DISPATCH_ENABLED",
                         "value": "false"}

    def check(self):
        verify(self.policy, self.ruleset, self.environment, self.branches,
               self.variable)

    def test_valid_and_fail_closed_drift(self):
        self.check()
        for field, value in (("current_user_can_bypass", "always"),
                             ("bypass_actors", [{"actor_id": 1}])):
            with self.subTest(field=field):
                original = self.ruleset[field]
                self.ruleset[field] = value
                with self.assertRaises(ValueError):
                    self.check()
                self.ruleset[field] = original
        self.ruleset["rules"][0]["parameters"][
            "strict_required_status_checks_policy"] = True
        with self.assertRaises(ValueError):
            self.check()

    def test_environment_and_disabled_admission(self):
        self.environment["protection_rules"].append({
            "type": "required_reviewers", "reviewers": [{"type": "User"}]})
        with self.assertRaises(ValueError):
            self.check()
        self.environment["protection_rules"].clear()
        self.branches["branch_policies"].append({"name": "*", "type": "branch"})
        with self.assertRaises(ValueError):
            self.check()
        self.branches["branch_policies"].pop()
        self.variable["value"] = "true"
        with self.assertRaises(ValueError):
            self.check()

    def test_rejects_policy_and_branch_drift(self):
        self.policy["rules"].append({"type": "non_fast_forward"})
        with self.assertRaises(ValueError):
            self.check()
        self.policy["rules"].pop()
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
