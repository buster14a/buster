#!/usr/bin/env python3
"""Preflight compatibility with attested writer heads, using real Git objects."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


MODULE_PATH = Path(__file__).with_name("native_retirement_merge_gate.py")
SPEC = importlib.util.spec_from_file_location("native_retirement_merge_gate", MODULE_PATH)
gate = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = gate
SPEC.loader.exec_module(gate)


def git(repo: Path, *arguments: str, input_text: str | None = None) -> str:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", os.fspath(repo), *arguments],
        input=input_text, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise AssertionError(
            "git failed: " + " ".join(arguments) + "\n" +
            (result.stderr or result.stdout)
        )
    return result.stdout.strip()


class Repository:
    def __init__(self, root: Path):
        self.repo = root / "repo"
        git(root, "init", "-b", "main", os.fspath(self.repo))
        git(self.repo, "config", "user.name", "Test User")
        git(self.repo, "config", "user.email", "test@example.invalid")
        files = {
            "src/buster/lib/value.c": "int value = 1;\n",
            "README.md": "base\n",
            "docs/native-retirement-repository-sources-v1.json": json.dumps({
                "schema": "buster-native-retirement-repository-sources-v1",
                "records": [{
                    "source": "src/buster/lib/value.c",
                    "bytes": 15,
                    "sha256": "1" * 64,
                }],
            }, indent=2) + "\n",
            "tools/native_retirement_dependency_binding.generated.h": "#define OLD 1\n",
        }
        for relative, content in files.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", "base")
        self.base = git(self.repo, "rev-parse", "HEAD")

    def branch(self, name: str, changes: dict[str, str]) -> str:
        git(self.repo, "checkout", "-B", name, self.base)
        for relative, content in changes.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", name)
        return git(self.repo, "rev-parse", "HEAD")

    def integration(self, candidate: str, kind: str = "ordinary") -> tuple[str, str]:
        git(self.repo, "checkout", "-B", "integrated", self.base)
        git(self.repo, "merge", "--no-commit", "--no-ff", candidate)
        snapshot = self.repo / "docs/native-retirement-repository-sources-v1.json"
        header = self.repo / "tools/native_retirement_dependency_binding.generated.h"
        snapshot.write_text(snapshot.read_text().replace('"1" * 64', '"2" * 64'))
        # Ensure the fixture actually differs without depending on the textual
        # form of the JSON serializer above.
        snapshot.write_text(snapshot.read_text() + "\n")
        header.write_text("#define NEW 1\n")
        git(self.repo, "add", os.fspath(snapshot.relative_to(self.repo)),
            os.fspath(header.relative_to(self.repo)))
        final_tree = git(self.repo, "write-tree")
        evidence = "e" * 64
        message = (
            "trusted integration\n\n"
            f"{gate.TRAILER_EVIDENCE}: {evidence}\n"
            f"{gate.TRAILER_BASE}: {self.base}\n"
            f"{gate.TRAILER_CANDIDATE}: {candidate}\n"
            f"{gate.TRAILER_FINAL_TREE}: {final_tree}\n"
            f"{gate.TRAILER_KIND}: {kind}\n"
        )
        commit = git(
            self.repo, "commit-tree", final_tree, "-p", self.base, "-p", candidate,
            input_text=message,
        )
        git(self.repo, "reset", "--hard", commit)
        return commit, evidence


def status_file(root: Path, head: str, evidence: str,
                creator: str = "github-actions[bot]", state: str = "success") -> Path:
    path = root / "status.json"
    path.write_text(json.dumps({
        "sha": head,
        "statuses": [{
            "id": 42,
            "context": gate.STATUS_CONTEXT,
            "state": state,
            "description": "evidence " + evidence[:16],
            "target_url": "https://github.com/buster14a/buster/actions/runs/1",
            "creator": {"login": creator},
        }],
    }))
    return path


import copy
from unittest import mock
import merge_conflict_preflight as preflight


class AttestedPreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.fixture = Repository(self.root)
        self.candidate = self.fixture.branch("source", {"src/buster/lib/value.c": "int value = 2;\n"})
        self.head, self.evidence = self.fixture.integration(self.candidate)
        self.status = json.loads(status_file(self.root, self.head, self.evidence).read_text())

    def analyze(self, status):
        return preflight.analyze(self.fixture.repo, self.fixture.base, self.head,
                                 retirement_status=status)

    def test_attested_generated_delta_is_clean_and_reported(self):
        report = self.analyze(self.status)
        self.assertFalse(report["outcome"]["blocking"])
        self.assertTrue(report["trusted_retirement_integration"]["verified"])
        self.assertEqual(len(report["candidate_changes"]["generated_or_integration_owned_retirement_paths"]), 2)

    def test_missing_wrong_head_creator_digest_and_failure_stay_blocked(self):
        variants = [None]
        for key, value in (("state", "failure"), ("creator", {"login": "author"}),
                           ("description", "wrong evidence")):
            status = copy.deepcopy(self.status)
            status["statuses"][0][key] = value
            variants.append(status)
        status = copy.deepcopy(self.status)
        status["sha"] = self.candidate
        variants.append(status)
        for status in variants:
            with self.subTest(status=status):
                self.assertTrue(self.analyze(status)["outcome"]["blocking"])

    def test_stale_main_does_not_reuse_attestation(self):
        main = self.fixture.branch("advanced", {"README.md": "new main\n"})
        report = preflight.analyze(self.fixture.repo, main, self.head, retirement_status=self.status)
        self.assertTrue(report["outcome"]["blocking"])

    def test_non_generated_post_candidate_change_is_rejected(self):
        git(self.fixture.repo, "checkout", "--detach", self.head)
        (self.fixture.repo / "README.md").write_text("extra source\n")
        git(self.fixture.repo, "add", ".")
        tree = git(self.fixture.repo, "write-tree")
        message = git(self.fixture.repo, "show", "-s", "--format=%B", self.head)
        old_tree = git(self.fixture.repo, "rev-parse", self.head + "^{tree}")
        self.head = git(self.fixture.repo, "commit-tree", tree, "-p", self.fixture.base,
                        "-p", self.candidate, input_text=message.replace(old_tree, tree))
        self.status["sha"] = self.head
        self.assertTrue(self.analyze(self.status)["outcome"]["blocking"])

    def test_manual_generated_edit_with_status_stays_blocked(self):
        self.head = self.fixture.branch("manual", {"tools/native_retirement_dependency_binding.generated.h": "bad\n"})
        self.status["sha"] = self.head
        self.assertTrue(self.analyze(self.status)["outcome"]["blocking"])

    def test_status_pagination_retains_creator_and_exact_head(self):
        api = preflight.GitHubApi("buster14a/buster", "test", "https://api.github.com")
        with mock.patch.object(api, "_request", side_effect=[[{}] * 100, self.status["statuses"]]) as request:
            value = api.retirement_status(self.head)
        self.assertEqual(value["sha"], self.head)
        self.assertEqual(value["statuses"][-1], self.status["statuses"][0])
        self.assertEqual(request.call_count, 2)


if __name__ == "__main__":
    unittest.main()
