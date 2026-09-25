#!/usr/bin/env python3
"""Offline regressions for the merge-parent preservation CI guard."""

from __future__ import annotations

import json
import os
import subprocess
import sys
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


def write_event(repository: Path, base: str, head: str | None, base_ref: str | None = "main") -> str:
    path = repository.parent / "event.json"
    path.write_text(json.dumps({"pull_request": {
        "base": {"sha": base, "ref": base_ref}, "head": {"sha": head},
    }}), encoding="utf-8")
    return str(path)


def pr_fixture(root: Path) -> tuple[Path, str, str, str, str]:
    repository = new_repository(root)
    event_base = commit_file(repository, "base.c", "int base_value;\n", "event base")
    guard.run_git(repository, ["checkout", "-b", "feature", event_base])
    source_head = commit_file(repository, "feature.c", "int feature_value;\n", "PR source")
    guard.run_git(repository, ["checkout", "main"])
    actual_base = commit_file(repository, "main.c", "int main_value;\n", "main advanced")
    tree = git(repository, "merge-tree", "--write-tree", actual_base, source_head).splitlines()[0]
    candidate = merge_with_tree(repository, actual_base, source_head, tree)
    guard.run_git(repository, ["update-ref", "refs/remotes/origin/main", actual_base])
    guard.run_git(repository, ["checkout", "--detach", candidate])
    return repository, event_base, actual_base, source_head, candidate


def run_cli(repository: Path, candidate: str, base: str, event_path: str,
            event_name: str = "pull_request") -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment.update(GITHUB_EVENT_PATH=event_path, GITHUB_SHA=candidate,
                       GITHUB_EVENT_NAME=event_name, MERGE_PARENT_BASE_SHA=base)
    # Exactly the production workflow argv; the runner supplies the event path.
    return subprocess.run([
        sys.executable, "-B", str(ROOT / "tools/merge_parent_preservation.py"),
        "--repository", str(repository), "--candidate", candidate,
        "--base", base, "--event-name", event_name,
    ], env=environment, capture_output=True, text=True, timeout=60, check=False)


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

            guard.run_git(repository, ["update-ref", "refs/remotes/origin/main", current_base])
            guard.run_git(repository, ["checkout", "--detach", candidate])
            event_path = write_event(repository, current_base, feature_head)
            report = guard.check_history(repository, candidate, current_base, "pull_request", event_path=event_path)

        self.assertEqual(report["base"], current_base)
        self.assertFalse(report["findings"])


class PullRequestBindingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repository, self.event_base, self.actual_base, self.source_head, self.candidate = pr_fixture(Path(temporary.name))
        self.event_path = write_event(self.repository, self.event_base, self.source_head)

    def check(self):
        return guard.check_history(self.repository, self.candidate, self.event_base,
                                   "pull_request", event_path=self.event_path)

    def test_issue_1189_stale_event_base_binds_actual_synthetic_parent(self):
        self.assertNotEqual(self.event_base, self.actual_base)
        self.assertFalse(guard.is_ancestor(self.repository, self.actual_base, self.source_head))
        report = self.check()
        self.assertEqual(report["base"], self.actual_base)
        self.assertEqual(report["event_base"], self.event_base)
        self.assertEqual(report["source_head"], self.source_head)
        self.assertEqual(report["candidate_tree"], guard.commit_details(self.repository, self.candidate)[0])
        self.assertFalse(report["findings"])
        self.assertEqual(report["merge_count"], 1)
        result = run_cli(self.repository, self.candidate, self.event_base, self.event_path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"base={self.actual_base}", result.stdout)
        self.assertIn(f"event-base={self.event_base}", result.stdout)
        self.assertEqual(git(self.repository, "rev-parse", "feature"), self.source_head)
        self.assertEqual(git(self.repository, "rev-parse", "HEAD"), self.candidate)

    def test_main_advancing_after_candidate_keeps_immutable_range(self):
        guard.run_git(self.repository, ["checkout", "main"])
        newer = commit_file(self.repository, "later.c", "int later;\n", "later main")
        guard.run_git(self.repository, ["update-ref", "refs/remotes/origin/main", newer])
        guard.run_git(self.repository, ["checkout", "--detach", self.candidate])
        report = self.check()
        self.assertEqual(report["base"], self.actual_base)
        self.assertEqual(report["base_tip"], newer)
        self.assertFalse(report["findings"])

    def test_wrong_source_head_fails_even_when_event_base_is_current(self):
        self.event_base = self.actual_base
        self.event_path = write_event(self.repository, self.event_base, self.event_base)
        with self.assertRaisesRegex(guard.MergePreservationError, "second parent"):
            self.check()

    def test_wrong_first_parent_on_side_branch_fails(self):
        guard.run_git(self.repository, ["checkout", "-b", "side", self.event_base])
        side = commit_file(self.repository, "side.c", "int side;\n", "side branch")
        tree = git(self.repository, "merge-tree", "--write-tree", side, self.source_head).splitlines()[0]
        self.candidate = merge_with_tree(self.repository, side, self.source_head, tree)
        guard.run_git(self.repository, ["checkout", "--detach", self.candidate])
        with self.assertRaisesRegex(guard.MergePreservationError, "first-parent history"):
            self.check()

    def test_side_parent_reachable_from_main_is_not_a_main_parent(self):
        tree = guard.commit_details(self.repository, self.candidate)[0]
        main_merge = merge_with_tree(self.repository, self.actual_base, self.source_head, tree, "main merge")
        guard.run_git(self.repository, ["update-ref", "refs/remotes/origin/main", main_merge])
        self.candidate = merge_with_tree(self.repository, self.source_head, self.actual_base, tree, "wrong order")
        self.event_path = write_event(self.repository, self.event_base, self.actual_base)
        guard.run_git(self.repository, ["checkout", "--detach", self.candidate])
        self.assertTrue(guard.is_ancestor(self.repository, self.source_head, main_merge))
        with self.assertRaisesRegex(guard.MergePreservationError, "first-parent history"):
            self.check()

    def test_candidate_older_than_event_base_fails(self):
        guard.run_git(self.repository, ["checkout", "main"])
        newer = commit_file(self.repository, "later.c", "int later;\n", "new event base")
        guard.run_git(self.repository, ["update-ref", "refs/remotes/origin/main", newer])
        guard.run_git(self.repository, ["checkout", "--detach", self.candidate])
        self.event_base = newer
        self.event_path = write_event(self.repository, newer, self.source_head)
        with self.assertRaisesRegex(guard.MergePreservationError, "not an ancestor"):
            self.check()

    def test_requested_base_must_match_event(self):
        self.event_base = self.actual_base
        with self.assertRaisesRegex(guard.MergePreservationError, "requested base"):
            self.check()

    def test_candidate_must_match_actual_checkout(self):
        guard.run_git(self.repository, ["checkout", "--detach", self.source_head])
        with self.assertRaisesRegex(guard.MergePreservationError, "checked-out HEAD"):
            self.check()

    def test_shallow_checkout_fails_closed(self):
        (self.repository / ".git/shallow").write_text(self.event_base + "\n", encoding="utf-8")
        with self.assertRaisesRegex(guard.MergePreservationError, "full-history checkout"):
            self.check()

    def test_missing_base_branch_does_not_fall_back_to_candidate_parent(self):
        guard.run_git(self.repository, ["update-ref", "-d", "refs/remotes/origin/main"])
        with self.assertRaisesRegex(guard.MergePreservationError, "refs/remotes/origin/main"):
            self.check()

    def test_missing_or_malformed_event_fails_closed(self):
        for contents in ("not json", "null", "[]", "{}", '{"pull_request": null}'):
            with self.subTest(contents=contents):
                Path(self.event_path).write_text(contents, encoding="utf-8")
                with self.assertRaisesRegex(guard.MergePreservationError, "missing or malformed"):
                    self.check()
        Path(self.event_path).unlink()
        with self.assertRaisesRegex(guard.MergePreservationError, "missing or malformed"):
            self.check()
        self.event_path = ""
        with self.assertRaisesRegex(guard.MergePreservationError, "GITHUB_EVENT_PATH"):
            self.check()

    def test_invalid_event_identifiers_fail_closed(self):
        for base, head, ref, diagnostic in (
            ("main", self.source_head, "main", "full commit SHAs"),
            (self.event_base, None, "main", "full commit SHAs"),
            (self.event_base, self.source_head, "main~1", "base ref is invalid"),
            (self.event_base, self.source_head, "", "base ref is invalid"),
            (self.event_base, self.source_head, None, "base ref is invalid"),
        ):
            with self.subTest(base=base, head=head, ref=ref):
                self.event_path = write_event(self.repository, base, head, ref)
                with self.assertRaisesRegex(guard.MergePreservationError, diagnostic):
                    self.check()

    def test_stale_payload_merge_commit_field_is_not_candidate_authority(self):
        path = Path(self.event_path)
        event = json.loads(path.read_text(encoding="utf-8"))
        event["pull_request"]["merge_commit_sha"] = self.event_base
        path.write_text(json.dumps(event), encoding="utf-8")
        self.assertEqual(self.check()["candidate"], self.candidate)

    def test_single_parent_root_and_octopus_candidates_fail(self):
        tree = guard.commit_details(self.repository, self.candidate)[0]
        octopus = git(self.repository, "commit-tree", tree, "-p", self.actual_base,
                      "-p", self.source_head, "-p", self.event_base, "-m", "octopus")
        for candidate in (self.source_head, self.event_base, octopus):
            with self.subTest(candidate=candidate):
                self.candidate = candidate
                guard.run_git(self.repository, ["checkout", "--detach", candidate])
                with self.assertRaisesRegex(guard.MergePreservationError, "two-parent merge"):
                    self.check()

    def test_stale_event_does_not_hide_dropped_source_history(self):
        dropped = merge_with_tree(self.repository, self.source_head, self.actual_base,
                                  self.source_head, "discard main changes")
        self.candidate = merge_with_tree(self.repository, self.actual_base, dropped, dropped)
        self.event_path = write_event(self.repository, self.event_base, dropped)
        guard.run_git(self.repository, ["checkout", "--detach", self.candidate])
        report = self.check()
        self.assertEqual(report["findings"][0]["merge"], dropped)
        self.assertTrue(report["findings"][0]["missing"])
        result = run_cli(self.repository, self.candidate, self.event_base, self.event_path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Merge drops second-parent changes", result.stdout)

    def test_synthetic_candidate_discarding_source_still_fails(self):
        self.candidate = merge_with_tree(self.repository, self.actual_base, self.source_head, self.actual_base)
        guard.run_git(self.repository, ["checkout", "--detach", self.candidate])
        self.assertEqual(self.check()["findings"][0]["merge"], self.candidate)

    def test_binding_errors_have_cli_exit_two(self):
        self.event_path = write_event(self.repository, self.event_base, self.event_base)
        result = run_cli(self.repository, self.candidate, self.event_base, self.event_path)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("second parent", result.stdout)
        self.assertNotIn("No newly introduced merge", result.stdout)

    def test_merge_group_exact_base_control_ignores_pr_event_path(self):
        result = run_cli(self.repository, self.candidate, self.actual_base, "/missing-event.json", "merge_group")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_merge_group_wrong_base_and_malformed_candidate_still_fail(self):
        for candidate, base, diagnostic in (
            (self.candidate, self.event_base, "first parent does not equal"),
            (self.source_head, self.event_base, "two-parent merge"),
            (self.candidate, "", "missing its exact base"),
        ):
            with self.subTest(candidate=candidate, base=base):
                with self.assertRaisesRegex(guard.MergePreservationError, diagnostic):
                    guard.check_history(self.repository, candidate, base, "merge_group")


if __name__ == "__main__":
    unittest.main()
