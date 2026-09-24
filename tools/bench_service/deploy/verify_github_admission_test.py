#!/usr/bin/env python3
"""Adversarial read-back fixtures for benchmark admission."""

import copy
import unittest

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


if __name__ == "__main__":
    unittest.main()
