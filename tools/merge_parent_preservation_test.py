#!/usr/bin/env python3
"""Offline regressions for the merge-parent preservation CI guard."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path
import unittest

import merge_parent_preservation as guard


ROOT = Path(__file__).resolve().parents[1]


def git(repository: Path, *arguments: str) -> str:
    return guard.git_text(repository, *arguments)


def commit_file(repository: Path, path: str, contents: str, message: str) -> str:
    destination = repository / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(contents, encoding="utf-8")
    guard.run_git(repository, ["add", path])
    guard.run_git(repository, ["commit", "-m", message])
    return git(repository, "rev-parse", "HEAD")


def new_repository(root: Path) -> Path:
    repository = root / "repo"
    repository.mkdir()
    guard.run_git(repository, ["init", "-b", "main"])
    guard.run_git(repository, ["config", "user.name", "Merge guard test"])
    guard.run_git(repository, ["config", "user.email", "merge-guard@example.invalid"])
    return repository


def merge_with_tree(repository: Path, first_parent: str, second_parent: str,
                    tree_source: str, message: str = "test merge") -> str:
    tree = tree_source if git(repository, "cat-file", "-t", tree_source) == "tree" else \
        git(repository, "show", "-s", "--format=%T", tree_source)
    result = guard.run_git(repository, [
        "commit-tree", tree, "-p", first_parent, "-p", second_parent, "-m", message,
    ])
    return result.stdout.decode("utf-8").strip()


class MergeParentPreservationTests(unittest.TestCase):
    def test_ci_complete_remains_required_without_strict_branch_freshness(self):
        policy = ROOT / ".github/main-merge-queue.ruleset.json"
        if not policy.is_file():
            self.skipTest("repository ruleset fixture is unavailable")
        ruleset = json.loads(policy.read_text(encoding="utf-8"))
        required = [
            row["context"]
            for rule in ruleset["rules"] if rule["type"] == "required_status_checks"
            for row in rule["parameters"]["required_status_checks"]
        ]
        strict = next(
            rule["parameters"]["strict_required_status_checks_policy"]
            for rule in ruleset["rules"] if rule["type"] == "required_status_checks"
        )
        self.assertIn("CI complete", required)
        self.assertFalse(strict)

    def test_issue_1041_first_parent_equal_merge_is_detected(self):
        with tempfile.TemporaryDirectory() as temporary:
            repository = new_repository(Path(temporary))
            base = commit_file(repository, "base.c", "int base_value;\n", "base")
            first_parent = commit_file(repository, "feature.c", "int feature_value;\n", "feature change")
            guard.run_git(repository, ["checkout", "-b", "main-side", base])
            second_parent = commit_file(repository, "restored_fix.c", "int restored_fix;\n", "second-parent fix")
            discarded = merge_with_tree(repository, first_parent, second_parent, first_parent,
                                        "merge current main while discarding its changes")

            report = guard.check_history(repository, discarded, base)

        self.assertEqual(report["merge_count"], 1)
        self.assertEqual(len(report["findings"]), 1)
        finding = report["findings"][0]
        self.assertEqual(finding["merge"], discarded)
        self.assertEqual(finding["kind"], "discarded-second-parent-changes")
        self.assertTrue(finding["missing"])

    def test_exact_issue_1041_merge_is_a_regression_when_history_is_available(self):
        if guard.run_git(ROOT, ["cat-file", "-e", f"{guard.KNOWN_ISSUE_1041_MERGE}^{{commit}}"], check=False).returncode != 0:
            self.skipTest("full repository history is unavailable")
        finding = guard.inspect_merge(ROOT, guard.KNOWN_ISSUE_1041_MERGE)
        self.assertIsNotNone(finding)
        self.assertEqual(finding["kind"], "discarded-second-parent-changes")
        self.assertTrue(finding["missing"])

    def test_ordinary_merge_with_second_parent_changes_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            repository = new_repository(Path(temporary))
            base = commit_file(repository, "base.c", "int base_value;\n", "base")
            first_parent = commit_file(repository, "feature.c", "int feature_value;\n", "feature change")
            guard.run_git(repository, ["checkout", "-b", "main-side", base])
            second_parent = commit_file(repository, "fix.c", "int fixed_value;\n", "second-parent fix")
            merge_tree = git(repository, "merge-tree", "--write-tree", first_parent, second_parent).splitlines()[0]
            merge = merge_with_tree(repository, first_parent, second_parent, merge_tree)

            finding = guard.inspect_merge(repository, merge)

        self.assertIsNone(finding)

    def test_patch_equivalent_change_already_in_first_parent_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            repository = new_repository(Path(temporary))
            base = commit_file(repository, "base.c", "int base_value;\n", "base")
            guard.run_git(repository, ["checkout", "-b", "main-side", base])
            second_parent = commit_file(repository, "fix.c", "int fixed_value;\n", "second-parent fix")
            guard.run_git(repository, ["checkout", "-b", "feature", base])
            guard.run_git(repository, ["cherry-pick", second_parent])
            first_parent = commit_file(repository, "feature.c", "int feature_value;\n", "feature change")
            discarded = merge_with_tree(repository, first_parent, second_parent, first_parent)

            finding = guard.inspect_merge(repository, discarded)

        self.assertIsNone(finding)

    def test_second_parent_with_no_net_tree_change_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            repository = new_repository(Path(temporary))
            base = commit_file(repository, "base.c", "int base_value;\n", "base")
            first_parent = commit_file(repository, "feature.c", "int feature_value;\n", "feature change")
            guard.run_git(repository, ["checkout", "-b", "main-side", base])
            guard.run_git(repository, ["commit", "--allow-empty", "-m", "empty second-parent commit"])
            second_parent = git(repository, "rev-parse", "HEAD")
            discarded = merge_with_tree(repository, first_parent, second_parent, first_parent)

            finding = guard.inspect_merge(repository, discarded)

        self.assertIsNone(finding)

    def test_old_merge_already_in_base_is_not_reaudited(self):
        with tempfile.TemporaryDirectory() as temporary:
            repository = new_repository(Path(temporary))
            base = commit_file(repository, "base.c", "int base_value;\n", "base")
            first_parent = commit_file(repository, "feature.c", "int feature_value;\n", "feature change")
            guard.run_git(repository, ["checkout", "-b", "main-side", base])
            second_parent = commit_file(repository, "fix.c", "int fixed_value;\n", "second-parent fix")
            historical_loss = merge_with_tree(repository, first_parent, second_parent, first_parent,
                                              "historical discarded merge")
            guard.run_git(repository, ["checkout", "-b", "after-loss", historical_loss])
            current_base = commit_file(repository, "later.c", "int later_value;\n", "later main commit")
            candidate = commit_file(repository, "candidate.c", "int candidate_value;\n", "candidate")

            report = guard.check_history(repository, candidate, current_base)
            introduced_merges = guard.merge_commits_in_range(repository, candidate, current_base)

        self.assertNotIn(historical_loss, introduced_merges)
        self.assertFalse(report["findings"])

    def test_pull_request_candidate_accepts_stale_branch_head(self):
        with tempfile.TemporaryDirectory() as temporary:
            repository = new_repository(Path(temporary))
            old_base = commit_file(repository, "base.c", "int base_value;\n", "old base")
            guard.run_git(repository, ["checkout", "-b", "feature", old_base])
            feature_head = commit_file(repository, "feature.c", "int feature_value;\n", "stale feature")
            guard.run_git(repository, ["checkout", "main"])
            current_base = commit_file(repository, "main.c", "int main_value;\n", "main advanced")
            candidate_tree = git(repository, "merge-tree", "--write-tree", current_base, feature_head).splitlines()[0]
            candidate = merge_with_tree(repository, current_base, feature_head, candidate_tree,
                                        "GitHub PR candidate merge")

            report = guard.check_history(repository, candidate, current_base, "pull_request")

        self.assertEqual(report["base"], current_base)
        self.assertFalse(report["findings"])


if __name__ == "__main__":
    unittest.main()
