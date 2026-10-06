#!/usr/bin/env python3
"""Offline checks of the main-commit compiler comparison authorization (#2752)."""

from __future__ import annotations

import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize_compiler  # noqa: E402

REPOSITORY = "buster14a/buster"
HEAD, BASE, PULL_HEAD = "a" * 40, "b" * 40, "c" * 40
HEAD_TREE, BASE_TREE = "d" * 40, "e" * 40
OTHER = {"login": "github-actions[bot]", "id": 41898282}


def records() -> dict:
    return {
        "run": {"id": 91, "path": authorize_compiler.REQUEST_WORKFLOW, "event": "push", "head_branch": "main",
                "status": "completed", "conclusion": "success", "head_sha": HEAD,
                "repository": {"full_name": REPOSITORY}, "head_repository": {"full_name": REPOSITORY},
                # Main is trusted code: the pusher is not the authority.
                "actor": dict(OTHER), "triggering_actor": dict(OTHER)},
        "commit": {"sha": HEAD, "parents": [{"sha": BASE}, {"sha": PULL_HEAD}], "commit": {"tree": {"sha": HEAD_TREE}}},
        "base_commit": {"sha": BASE, "commit": {"tree": {"sha": BASE_TREE}}},
        "on_main": {"status": "identical"},
        "pulls": [{"number": 2774, "merge_commit_sha": HEAD, "user": dict(OTHER)}],
    }


class AuthorizeCompilerTest(unittest.TestCase):
    def check(self, value: dict) -> tuple[list[str], dict]:
        return authorize_compiler.verify(REPOSITORY, 91, HEAD, value["run"], value["commit"], value["base_commit"],
                                         value["on_main"], value["pulls"])

    def test_landed_merge_commit_is_measured_against_its_first_parent(self) -> None:
        # Bot-authored pull requests are measured once they land on main.
        self.assertEqual(self.check(records()), ([], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                      "pull": "2774", "pull_head": PULL_HEAD}))

    def test_direct_push_and_older_main_commit(self) -> None:
        value = records()
        value["commit"]["parents"] = [{"sha": BASE}]
        value["on_main"] = {"status": "ahead"}
        value["pulls"] = []
        self.assertEqual(self.check(value)[1], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                "pull": "0", "pull_head": HEAD})

    def test_every_record_field_is_required(self) -> None:
        changes = (
            ("run", "id", 92), ("run", "path", ".github/workflows/9700x-direct-request.yml"),
            ("run", "event", "merge_group"), ("run", "event", "pull_request"), ("run", "head_branch", "feature"),
            ("run", "conclusion", "failure"), ("run", "status", "queued"), ("run", "head_sha", "9" * 40),
            ("run", "repository", {"full_name": "fork/buster"}), ("run", "head_repository", {"full_name": "fork/buster"}),
            ("commit", "parents", []), ("commit", "parents", [{"sha": BASE}] * 3), ("commit", "sha", "9" * 40),
            ("commit", "commit", {}), ("base_commit", "sha", "9" * 40), ("base_commit", "commit", {"tree": {"sha": "x"}}),
            ("on_main", "status", "diverged"), ("on_main", "status", "behind"),
        )
        for record, key, value in changes:
            with self.subTest(record=record, key=key, value=value):
                changed = records()
                changed[record][key] = value
                failures, result = self.check(changed)
                self.assertTrue(failures)
                self.assertEqual(result, {})

    def test_malformed_records_fail_closed(self) -> None:
        for record in ("run", "commit", "base_commit", "on_main"):
            for value in (None, [], "text", {"message": "Not Found"}):
                with self.subTest(record=record, value=value):
                    changed = records()
                    changed[record] = value
                    failures, result = self.check(changed)
                    self.assertTrue(failures)
                    self.assertEqual(result, {})
        # An unreadable pull request list only loses the attribution.
        changed = records()
        changed["pulls"] = {"message": "Not Found"}
        self.assertEqual(self.check(changed)[1]["pull"], "0")


if __name__ == "__main__":
    unittest.main()
