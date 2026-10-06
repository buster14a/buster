#!/usr/bin/env python3
"""Full workflow-policy test entry across the frozen topology transition.

Ownership: CI policy test selection only; no trust or publication authority.
load_tests retains every frozen CI/action test on the legacy workflow and
delegates to the complete maintained suite after queue_lint is declared.
The frozen support files and their reviewed byte identities stay unchanged.
"""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_tests(loader, tests, pattern):
    workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    suite = unittest.TestSuite()
    if "\n  queue_lint:\n" in workflow:
        module = load_module("current_workflow_tests", "tools/ci_workflow_tools_test.py")
        # Before the transition this module contains only runner tests. Never
        # substitute that smaller suite for the frozen policy/action coverage.
        for name in ("CurrentWorkflowPolicyTests", "CurrentActionPinsTests"):
            case = getattr(module, name, None)
            if not isinstance(case, type) or not issubclass(case, unittest.TestCase):
                raise ValueError("queue lint requires the complete maintained policy suite: " + name)
        suite.addTests(loader.loadTestsFromModule(module, pattern=pattern))
    else:
        for name, path in (("legacy_workflow_tests", "tests/ci_tools_test.py"),
                           ("legacy_action_tests", "tests/action_pins_test.py")):
            suite.addTests(loader.loadTestsFromModule(load_module(name, path), pattern=pattern))
    return suite


if __name__ == "__main__":
    unittest.main()
