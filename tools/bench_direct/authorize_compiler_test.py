#!/usr/bin/env python3
"""Offline checks of the merge-group compiler comparison authorization (#2752)."""

from __future__ import annotations

import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize_compiler  # noqa: E402

REPOSITORY = "buster14a/buster"
HEAD, BASE, PULL_HEAD = "a" * 40, "b" * 40, "c" * 40
HEAD_TREE, BASE_TREE = "d" * 40, "e" * 40
BRANCH = "gh-readonly-queue/main/pr-2752-" + "f" * 40
OWNER = {"login": "davidgmbb", "id": 39247043}
OTHER = {"login": "someone-else", "id": 7}


def records() -> dict:
    return {
        "run": {"id": 91, "path": authorize_compiler.REQUEST_WORKFLOW, "event": "merge_group",
                "status": "completed", "conclusion": "success", "head_sha": HEAD, "head_branch": BRANCH,
                "repository": {"full_name": REPOSITORY}, "head_repository": {"full_name": REPOSITORY},
                # Queue events are started by GitHub; the actor is not the authority.
                "actor": dict(OTHER), "triggering_actor": dict(OTHER)},
        "ref": {"ref": "refs/heads/" + BRANCH, "object": {"sha": HEAD}},
        "group": {"sha": HEAD, "parents": [{"sha": BASE}, {"sha": PULL_HEAD}],
                  "commit": {"tree": {"sha": HEAD_TREE}}},
        "base_commit": {"sha": BASE, "commit": {"tree": {"sha": BASE_TREE}}},
        "pull": {"number": 2752, "state": "open", "user": dict(OWNER),
                 "head": {"sha": PULL_HEAD, "repo": {"full_name": REPOSITORY}},
                 "base": {"ref": "main", "repo": {"full_name": REPOSITORY}}},
    }


class AuthorizeCompilerTest(unittest.TestCase):
    def check(self, value: dict) -> tuple[list[str], dict]:
        return authorize_compiler.verify(REPOSITORY, 91, HEAD, BRANCH, value["run"], value["ref"], value["group"],
                                         value["base_commit"], value["pull"])

    def test_owner_group_is_authorized_with_exact_identities(self) -> None:
        self.assertEqual(self.check(records()), ([], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                      "pull": "2752", "pull_head": PULL_HEAD}))

    def test_queue_branch_names_one_pull_request(self) -> None:
        self.assertEqual(authorize_compiler.queue_pull(BRANCH), 2752)
        for branch in ("main", "gh-readonly-queue/main/pr-0-" + "f" * 40, "gh-readonly-queue/other/pr-1-" + "f" * 40,
                       "gh-readonly-queue/main/pr-1-abc", None):
            with self.subTest(branch=branch):
                self.assertIsNone(authorize_compiler.queue_pull(branch))

    def test_every_record_field_is_required(self) -> None:
        changes = (
            ("run", "id", 92), ("run", "path", ".github/workflows/9700x-direct-request.yml"),
            ("run", "event", "pull_request"), ("run", "conclusion", "failure"), ("run", "status", "queued"),
            ("run", "head_sha", "9" * 40), ("run", "head_branch", "gh-readonly-queue/main/pr-1-" + "f" * 40),
            ("run", "repository", {"full_name": "fork/buster"}), ("run", "head_repository", {"full_name": "fork/buster"}),
            ("ref", "object", {"sha": "9" * 40}), ("ref", "ref", "refs/heads/main"),
            ("group", "parents", [{"sha": BASE}]), ("group", "parents", [{"sha": BASE}, {"sha": PULL_HEAD}, {"sha": BASE}]),
            ("group", "sha", "9" * 40), ("group", "commit", {}), ("base_commit", "sha", "9" * 40),
            ("base_commit", "commit", {"tree": {"sha": "tree"}}),
            ("pull", "number", 1), ("pull", "state", "closed"), ("pull", "user", dict(OTHER)),
            ("pull", "user", {"login": "davidgmbb", "id": 8}),
            ("pull", "head", {"sha": PULL_HEAD, "repo": {"full_name": "fork/buster"}}),
            ("pull", "head", {"sha": "9" * 40, "repo": {"full_name": REPOSITORY}}),
            ("pull", "base", {"ref": "release", "repo": {"full_name": REPOSITORY}}),
            ("pull", "base", {"ref": "main", "repo": {"full_name": "fork/buster"}}),
        )
        for record, key, value in changes:
            with self.subTest(record=record, key=key, value=value):
                changed = records()
                changed[record][key] = value
                failures, result = self.check(changed)
                self.assertTrue(failures)
                self.assertEqual(result, {})

    def test_malformed_records_fail_closed(self) -> None:
        for record in ("run", "ref", "group", "base_commit", "pull"):
            for value in (None, [], "text", {"message": "Not Found"}):
                with self.subTest(record=record, value=value):
                    changed = records()
                    changed[record] = value
                    failures, result = self.check(changed)
                    self.assertTrue(failures)
                    self.assertEqual(result, {})


if __name__ == "__main__":
    unittest.main()
