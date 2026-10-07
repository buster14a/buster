#!/usr/bin/env python3
"""Offline checks of the direct 9700X workload authorization rules."""

from __future__ import annotations

import copy
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize  # noqa: E402
import compiler_receipt  # noqa: E402

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


class InventoryTest(unittest.TestCase):
    """The changed-file inventory is complete or the request fails closed (#2939)."""

    @staticmethod
    def rows(count: int, start: int = 0) -> list[dict]:
        return [{"filename": f"src/f{start + index}.c", "status": "modified"} for index in range(count)]

    def run_pages(self, pages: list[object], expected: object) -> tuple[list[dict], list[str], list[int]]:
        asked: list[int] = []

        def fetch_page(page: int) -> object:
            asked.append(page)
            return pages[page - 1] if page <= len(pages) else []
        files, failures = authorize.inventory(fetch_page, expected)
        return files, failures, asked

    def test_complete_inventories(self) -> None:
        for pages, count in (([[]], 0), ([self.rows(1)], 1), ([self.rows(37)], 37),
                             ([self.rows(100), self.rows(100, 100), self.rows(5, 200)], 205),
                             ([self.rows(100), self.rows(100, 100)], 200)):
            with self.subTest(count=count):
                files, failures, _ = self.run_pages(pages, count)
                self.assertEqual((len(files), failures), (count, []))

    def test_full_final_budget_page_is_complete_only_when_counted(self) -> None:
        pages = [self.rows(100, 100 * page) for page in range(authorize.FILE_PAGES)]
        full = authorize.FILE_PAGES * 100
        files, failures, asked = self.run_pages(pages, full)
        self.assertEqual((len(files), failures, asked[-1]), (full, [], authorize.FILE_PAGES))
        files, failures, asked = self.run_pages(pages, full + 1)
        self.assertTrue(failures)
        self.assertEqual(len(asked), authorize.FILE_PAGES)

    def test_incomplete_or_malformed_inventories_fail(self) -> None:
        good = self.rows(100)
        cases = {
            "short of the count": ([self.rows(10)], 11),
            "more than the count": ([self.rows(10)], 9),
            "malformed after valid pages": ([good, {"message": "boom"}], 150),
            "non-list first page": ([None], 1),
            "bad row": ([[{"filename": "a.c"}]], 1),
            "bad row type": ([["a.c"]], 1),
            "empty name": ([[{"filename": "", "status": "added"}]], 1),
            "rename without previous name": ([[{"filename": "a.c", "status": "renamed"}]], 1),
            "duplicate rows": ([[good[0], dict(good[0])]], 2),
            "duplicate across pages": ([good, self.rows(1)], 101),
            "missing count": ([self.rows(1)], None),
            "boolean count": ([self.rows(1)], True),
            "negative count": ([[]], -1),
            "premature empty page": ([good, []], 150),
        }
        for name, (pages, expected) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.run_pages(pages, expected)[1])

    def test_marker_outside_the_available_prefix_is_never_an_empty_plan(self) -> None:
        # The comparison request lies on page 2, past what a truncated read keeps.
        pages = [self.rows(100), self.rows(99, 100) + [{"filename": authorize.COMPARE_REQUEST, "status": "added"}]]
        files, failures, _ = self.run_pages(pages, 200)
        self.assertEqual(failures, [])
        self.assertEqual(authorize.plan(files), (False, True, []))
        files, failures, _ = self.run_pages(pages[:1], 200)
        self.assertTrue(failures)


class PlanTest(unittest.TestCase):
    def test_changed_files_select_workloads_and_comparison(self) -> None:
        def files(*names: str, status: str = "modified") -> list[dict]:
            return [{"filename": name, "status": status} for name in names]
        self.assertEqual(authorize.plan(files("benchmarks/9700x/a.c")), (True, False, []))
        self.assertEqual(authorize.plan(files("benchmarks/9700x/a.data")), (True, False, []))
        self.assertEqual(authorize.plan(files(authorize.COMPARE_REQUEST)), (False, True, []))
        self.assertEqual(authorize.plan(files("benchmarks/9700x/a.c", authorize.COMPARE_REQUEST)), (True, True, []))
        self.assertEqual(authorize.plan(files(authorize.COMPARE_REQUEST, status="removed")), (False, False, []))
        self.assertEqual(authorize.plan(files("benchmarks/9700x/nested/a.c", "src/x.c")), (False, False, []))
        self.assertEqual(authorize.plan(None), (False, False, []))
        # A scaling request runs inside a compiler comparison (#424).
        self.assertEqual(authorize.plan(files(authorize.SCALING_REQUEST)), (False, True, []))
        self.assertEqual(authorize.plan(files(authorize.SCALING_REQUEST, status="removed")), (False, False, []))
        self.assertEqual(authorize.SCALING_REQUEST, compiler_receipt.SCALING_REQUEST)

    def test_selection_matches_the_executor_contract(self) -> None:
        def plan(*rows: tuple) -> tuple[bool, bool, list[str]]:
            return authorize.plan([{"filename": name, "status": status, **({"previous_filename": old} if old else {})}
                                   for status, name, old in rows])
        a, b = "benchmarks/9700x/first.c", "benchmarks/9700x/second.c"
        self.assertEqual(plan(("removed", "benchmarks/9700x/a.data", "")), (True, False, []))
        self.assertEqual(plan(("renamed", b, a)), (True, False, []))
        self.assertEqual(plan(("removed", a, "")), (False, False, []))
        self.assertEqual(plan(("removed", a, ""), ("modified", "benchmarks/9700x/first.data", "")), (False, False, []))
        self.assertEqual(plan(("renamed", "benchmarks/9700x/b.data", "benchmarks/9700x/a.data")), (True, False, []))
        self.assertEqual(plan(("modified", "benchmarks/9700x/a.data", "")), (True, False, []))
        self.assertEqual(plan(("added", a, ""), ("removed", "benchmarks/9700x/x.c", "")), (True, False, []))
        for bad in ("benchmarks/9700x/Upper.c", "benchmarks/9700x/has space.c", "benchmarks/9700x/.c",
                    "benchmarks/9700x/" + "a" * 49 + ".c", "benchmarks/9700x/Bad.data"):
            with self.subTest(name=bad):
                workloads, compare, problems = plan(("added", bad, ""))
                self.assertEqual((workloads, compare, len(problems)), (False, False, 1))
        self.assertEqual(plan(("removed", "benchmarks/9700x/Upper.c", ""))[2], [])
        self.assertEqual(plan(("renamed", a, "benchmarks/9700x/Upper.c"))[2], [])
        self.assertEqual(len(plan(("renamed", "benchmarks/9700x/x.c", "benchmarks/9700x/Upper.c"),
                                  ("added", "benchmarks/9700x/Bad.c", ""))[2]), 1)
        self.assertEqual(len(plan(("weird", a, ""))[2]), 1)
        self.assertEqual(len(plan(*[("added", f"benchmarks/9700x/w{index}.c", "") for index in range(5)])[2]), 1)

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
