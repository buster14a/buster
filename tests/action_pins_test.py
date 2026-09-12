#!/usr/bin/env python3
"""Offline policy and repository wiring checks for GitHub action references."""

import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/check_action_pins.py"
SPEC = importlib.util.spec_from_file_location("action_pins", SCRIPT)
PINS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PINS)
PIN = "actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683"


class ActionPinsTest(unittest.TestCase):
    def test_approved_scalar_spellings(self):
        for value in (PIN, f"'{PIN}'", f'"{PIN}"', PIN + " # reviewed"):
            for prefix in ("  uses: ", "  - uses: "):
                with self.subTest(value=value, prefix=prefix):
                    self.assertEqual(PINS.check_text(prefix + value, "case.yml"), [])

    def test_unreviewed_references(self):
        for value in (
            PIN.rsplit("@", 1)[0] + "@v4", PIN[:-1], PIN[:-1] + "A",
            PIN[:-40] + "0" * 40, PIN.replace("actions/", "other/"),
            "https://github.com/" + PIN, PIN.replace("/checkout@", "/checkout/subdir@"),
            "actions/checkout@" + "a" * 40, "./local-action", "docker://image@sha256:" + "a" * 64,
            "${{ vars.ACTION }}", "", "|", ">", "*reference", "&reference " + PIN,
            PIN + " extra", f'"{PIN}\\n"',
        ):
            with self.subTest(value=value):
                self.assertTrue(PINS.check_text("  - uses: " + value, "case.yml"))

    def test_unsupported_yaml_forms_are_rejected(self):
        for text in (
            '"uses": ' + PIN, "'uses': " + PIN, '"u\\u0073es": ' + PIN,
            "uses : " + PIN, "- {uses: " + PIN + "}", "? uses\n: " + PIN,
            "step: &shared\n  uses: " + PIN, "- *shared", "<<: *shared",
            "\tuses: " + PIN,
            '!!str "uses": checkout@main', '!!str "uses\n  ": checkout@main',
            '"uses\n  ": checkout@main', "steps: [uses: checkout@main]",
            "steps: [[uses: checkout@main]]", "steps: [!!map uses: checkout@main]",
        ):
            with self.subTest(text=text):
                self.assertTrue(PINS.check_text(text, "case.yml"))

    def test_scripts_and_comments_are_not_action_steps(self):
        text = "# uses: ignored\nsteps:\n  - run: |\n      echo 'uses: data'\n      echo '${array[*]}'\n\n  - uses: " + PIN
        self.assertEqual(PINS.check_text(text, "case.yml"), [])

    def test_block_end_does_not_hide_next_action(self):
        text = "- run: >- # command\n    echo ok\n- uses: checkout@main"
        errors = PINS.check_text(text, "case.yml")
        self.assertEqual(len(errors), 1)
        self.assertIn("case.yml:3:", errors[0])

    def test_inline_comments_preserve_quoted_hashes(self):
        self.assertEqual(PINS.without_comment("name: 'text # value' # note"), "name: 'text # value'")

    def test_expression_values_are_allowed(self):
        self.assertEqual(PINS.check_text("if: ${{ !cancelled() && vars.ENABLED == 'true' }}", "case.yml"), [])

    def test_scalar_lists_and_empty_mappings_are_allowed(self):
        for value in ("on: [push]", "branches: [main]", "runner: [linux, windows, macos]", "needs: []",
                      "tags: ['**']", "workflows: [Buster CI]", "permissions: {}", "paths:\n  - '.github/workflows/ci.yml'"):
            with self.subTest(value=value):
                self.assertEqual(PINS.check_text(value, "case.yml"), [])

    def test_repository_workflows(self):
        workflows = sorted((ROOT / ".github/workflows").glob("*.yml"))
        workflows += sorted((ROOT / ".github/workflows").glob("*.yaml"))
        self.assertTrue(workflows)
        for path in workflows:
            with self.subTest(path=path):
                self.assertEqual(PINS.check_text(path.read_text(), path), [])

    def test_github_lint_checks_policy(self):
        github = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("python3 tools/check_action_pins.py", github)
        self.assertIn("python3 tests/action_pins_test.py", github)
        self.assertNotIn(".forgejo/", github)

    def test_default_discovery_outside_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([sys.executable, str(SCRIPT)], cwd=temporary, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertIn(b"GitHub workflows", result.stdout)

    def test_cli_and_missing_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "workflow.yml"
            path.write_text("uses: " + PIN)
            valid = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True)
            self.assertEqual(valid.returncode, 0)
            path.write_text("uses: checkout@main")
            invalid = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True)
            self.assertEqual(invalid.returncode, 1)
            path.unlink()
            missing = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True)
            self.assertEqual(missing.returncode, 1)


if __name__ == "__main__":
    unittest.main()
