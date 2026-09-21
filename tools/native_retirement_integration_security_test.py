#!/usr/bin/env python3
"""Adversarial path-boundary tests for native-retirement integration."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("native_retirement_integration.py")
SPEC = importlib.util.spec_from_file_location("native_retirement_integration", MODULE_PATH)
integration = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = integration
SPEC.loader.exec_module(integration)


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", os.fspath(repo), *arguments],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise AssertionError(
            "git failed: " + " ".join(arguments) + "\n" + result.stderr
        )
    return result.stdout.strip()


class ReservedMaterializationRootTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_absent_reserved_roots_are_accepted(self):
        integration.reject_reserved_materialization_roots(self.root)

    def test_broken_symlink_reserved_roots_fail_closed(self):
        for relative in integration.RESERVED_MATERIALIZATION_ROOTS:
            with self.subTest(relative=relative):
                path = self.root / relative
                path.symlink_to(self.root / "missing-target", target_is_directory=True)
                with self.assertRaisesRegex(
                        integration.IntegrationError, "reserved materialization root"):
                    integration.reject_reserved_materialization_roots(self.root)
                path.unlink()

    def test_existing_file_or_directory_reserved_roots_fail_closed(self):
        for relative, make in (
                ("external", lambda path: path.mkdir()),
                (".native-retirement-trusted", lambda path: path.write_text("owned\n"))):
            with self.subTest(relative=relative):
                path = self.root / relative
                make(path)
                with self.assertRaisesRegex(
                        integration.IntegrationError, "reserved materialization root"):
                    integration.reject_reserved_materialization_roots(self.root)
                if path.is_dir():
                    path.rmdir()
                else:
                    path.unlink()

    def test_exact_combined_tree_rejects_checkout_redirection(self):
        repo = self.root / "repo"
        repo.mkdir()
        git(repo, "init", "-b", "main")
        git(repo, "config", "user.name", "Test User")
        git(repo, "config", "user.email", "test@example.invalid")
        (repo / "README").write_text("base\n")
        git(repo, "add", "README")
        git(repo, "commit", "-m", "base")
        base = git(repo, "rev-parse", "HEAD")

        git(repo, "checkout", "-b", "candidate")
        (repo / "external").symlink_to("../trusted", target_is_directory=True)
        git(repo, "add", "external")
        git(repo, "commit", "-m", "redirect pinned checkout")
        head = git(repo, "rev-parse", "HEAD")

        worktree = self.root / "combined"
        with self.assertRaisesRegex(
                integration.IntegrationError, "reserved materialization root"):
            integration.stage(repo, worktree, base, head, "ordinary", False)
        self.assertFalse(worktree.exists())


class WorkflowLayoutTests(unittest.TestCase):
    def test_contract_keeps_trusted_checkout_outside_candidate_tree(self):
        root = Path(__file__).resolve().parents[1]
        text = (root / ".github/workflows/native-retirement-contract.yml").read_text()
        self.assertIn("path: candidate", text)
        self.assertIn("path: trusted", text)
        self.assertNotIn("path: .native-retirement-trusted", text)
        guard = text.index("Reject candidate-controlled checkout destinations")
        first_external = text.index("Check out pinned cJSON closure")
        self.assertLess(guard, first_external)
        self.assertIn("path: candidate/external/cjson", text)
        self.assertIn(
            '"$GITHUB_WORKSPACE/trusted/tools/native_retirement_rebind.py" refresh',
            text,
        )

    def test_security_workflow_is_read_only_and_cross_platform(self):
        root = Path(__file__).resolve().parents[1]
        text = (root / ".github/workflows/native-retirement-integration-security.yml").read_text()
        self.assertIn("permissions:\n  contents: read", text)
        self.assertNotIn("contents: write", text)
        self.assertIn("runner: [ubuntu-26.04, macos-26]", text)
        self.assertIn("native_retirement_integration_security_test.py -v", text)


if __name__ == "__main__":
    unittest.main()
