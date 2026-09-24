#!/usr/bin/env python3
"""Read-only comparison of the protected benchmark environment and switch.

The administrator captures three GET responses during preflight. The caller
also checks the existing main ruleset, requester policy, administrator
permission and restricted runner group. This check does not authorize host
provisioning or workflow dispatch.
"""

import argparse
import json
import sys
from pathlib import Path


def require(condition, description):
    if not condition:
        raise ValueError(description)


def matching(expected, actual, path):
    require(type(expected) is type(actual), f"{path}: type differs")
    if isinstance(expected, dict):
        for key, value in expected.items():
            require(key in actual, f"{path}.{key}: missing")
            matching(value, actual[key], f"{path}.{key}")
    elif isinstance(expected, list):
        require(len(expected) == len(actual), f"{path}: length differs")
        for index, value in enumerate(expected):
            matching(value, actual[index], f"{path}[{index}]")
    else:
        require(expected == actual, f"{path}: value differs")


def verify(environment, branches, variable):
    require(environment.get("name") == "benchmark-9700x", "environment differs")
    matching({"protected_branches": False, "custom_branch_policies": True},
             environment.get("deployment_branch_policy"), "deployment policy")
    rules = environment.get("protection_rules")
    require(isinstance(rules, list), "environment protection rules missing")
    reviewers = [rule for rule in rules if rule.get("type") == "required_reviewers"]
    require(len(reviewers) == 1, "environment must require one administrator reviewer")
    require(reviewers[0].get("prevent_self_review") is True,
            "environment must prevent self-review")
    matching([{"type": "User", "reviewer": {"login": "davidgmbb", "id": 39247043}}],
             reviewers[0].get("reviewers"), "environment reviewers")
    require(branches.get("total_count") == 1 and
            len(branches.get("branch_policies", [])) == 1,
            "environment must have exactly one deployment branch")
    matching({"name": "main", "type": "branch"},
             branches["branch_policies"][0], "deployment branch")
    matching({"name": "BENCH_SERVICE_DISPATCH_ENABLED", "value": "false"},
             variable, "dispatch variable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("environment", type=Path)
    parser.add_argument("branches", type=Path)
    parser.add_argument("variable", type=Path)
    args = parser.parse_args()
    try:
        inputs = [json.loads(path.read_text(encoding="utf-8")) for path in
                  (args.environment, args.branches, args.variable)]
        verify(*inputs)
    except (ValueError, OSError, TypeError, KeyError) as error:
        print(f"BENCH_ADMISSION_FAIL {error}", file=sys.stderr)
        return 1
    print("BENCH_ADMISSION_PASS reviewed controls installed; host unverified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
