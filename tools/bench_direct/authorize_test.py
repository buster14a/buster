#!/usr/bin/env python3
"""Offline checks of the direct 9700X workload authorization rules."""

from __future__ import annotations

import copy
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize  # noqa: E402

REPOSITORY = "buster14a/buster"
HEAD = "a" * 40
BASE = "b" * 40
OWNER = {"login": "davidgmbb", "id": 39247043}
OTHER = {"login": "someone-else", "id": 7}


def request_run() -> dict:
    return {
        "id": 91, "path": authorize.REQUEST_WORKFLOW, "event": "pull_request",
        "status": "completed", "conclusion": "success", "head_sha": HEAD,
        "repository": {"full_name": REPOSITORY}, "head_repository": {"full_name": REPOSITORY},
        "actor": dict(OWNER), "triggering_actor": dict(OWNER),
    }


def pull_request() -> dict:
    return {
        "state": "open", "user": dict(OWNER),
        "head": {"sha": HEAD, "repo": {"full_name": REPOSITORY}},
        "base": {"sha": BASE, "repo": {"full_name": REPOSITORY}},
    }


class AuthorizeTest(unittest.TestCase):
    def check(self, run: object, pulls: object) -> tuple[list[str], str]:
        return authorize.verify(REPOSITORY, 91, HEAD, run, pulls)

    def test_owner_pull_request_is_authorized_with_its_base(self) -> None:
        self.assertEqual(self.check(request_run(), [pull_request()]), ([], BASE))

    def test_every_request_run_field_is_required(self) -> None:
        changes = {
            "id": 92, "path": ".github/workflows/ci.yml", "event": "push", "conclusion": "failure",
            "status": "in_progress", "head_sha": "c" * 40, "repository": {"full_name": "fork/buster"},
            "head_repository": {"full_name": "fork/buster"}, "actor": dict(OTHER),
            "triggering_actor": dict(OTHER),
        }
        for key, value in changes.items():
            with self.subTest(field=key):
                run = request_run()
                run[key] = value
                failures, base = self.check(run, [pull_request()])
                self.assertTrue(failures)
                self.assertEqual(base, "")

    def test_same_login_with_another_numeric_id_is_refused(self) -> None:
        run = request_run()
        run["actor"] = {"login": "davidgmbb", "id": 8}
        self.assertIn("request actor", self.check(run, [pull_request()])[0])

    def test_pull_request_must_be_the_owners_and_from_this_repository(self) -> None:
        for path, value in ((("user",), dict(OTHER)), (("head", "repo"), {"full_name": "fork/buster"}),
                            (("base", "repo"), {"full_name": "fork/buster"}), (("base", "sha"), "main"),
                            (("state",), "closed"), (("head", "sha"), "c" * 40)):
            with self.subTest(path=path):
                pull = pull_request()
                target = pull
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                failures, base = self.check(request_run(), [pull])
                self.assertTrue(failures)
                self.assertEqual(base, "")

    def test_zero_or_several_pull_requests_are_refused(self) -> None:
        for pulls in ([], [pull_request(), pull_request()], None, {"message": "Not Found"}):
            with self.subTest(pulls=pulls):
                self.assertIn("exactly one open pull request for the head commit",
                              self.check(request_run(), copy.deepcopy(pulls))[0])

    def test_malformed_records_fail_closed(self) -> None:
        for run in (None, [], "run", {"actor": "davidgmbb"}):
            with self.subTest(run=run):
                failures, base = self.check(run, [pull_request()])
                self.assertTrue(failures)
                self.assertEqual(base, "")


class PlanTest(unittest.TestCase):
    def test_changed_files_select_workloads_and_comparison(self) -> None:
        def files(*names: str, status: str = "modified") -> list[dict]:
            return [{"filename": name, "status": status} for name in names]
        self.assertEqual(authorize.plan(files("benchmarks/9700x/a.c")), (True, False))
        self.assertEqual(authorize.plan(files("benchmarks/9700x/a.data")), (True, False))
        self.assertEqual(authorize.plan(files(authorize.COMPARE_REQUEST)), (False, True))
        self.assertEqual(authorize.plan(files("benchmarks/9700x/a.c", authorize.COMPARE_REQUEST)), (True, True))
        self.assertEqual(authorize.plan(files(authorize.COMPARE_REQUEST, status="removed")), (False, False))
        self.assertEqual(authorize.plan(files("benchmarks/9700x/nested/a.c", "src/x.c")), (False, False))
        self.assertEqual(authorize.plan(None), (False, False))

    def test_comparison_needs_a_merge_base_and_both_trees(self) -> None:
        compared = {"merge_base_commit": {"sha": BASE, "commit": {"tree": {"sha": "e" * 40}}}}
        head = {"sha": HEAD, "commit": {"tree": {"sha": "d" * 40}}}
        self.assertEqual(authorize.comparison(HEAD, compared, head),
                         ([], {"merge_base": BASE, "merge_base_tree": "e" * 40, "head_tree": "d" * 40}))
        for bad_compared, bad_head in ((None, head), ({}, head), (compared, {"sha": BASE}),
                                       ({"merge_base_commit": {"sha": HEAD, "commit": {"tree": {"sha": "e" * 40}}}}, head)):
            with self.subTest(compared=bad_compared, head=bad_head):
                failures, result = authorize.comparison(HEAD, bad_compared, bad_head)
                self.assertTrue(failures)
                self.assertEqual(result, {})


if __name__ == "__main__":
    unittest.main()
