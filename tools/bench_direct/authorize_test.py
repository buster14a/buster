#!/usr/bin/env python3
"""Offline checks of the direct 9700X workload authorization rules."""

from __future__ import annotations

import copy
import unittest
import tempfile
from unittest import mock
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize  # noqa: E402
import compiler_receipt  # noqa: E402
import workflow_policy_test as policy  # noqa: E402

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

    def test_non_pr_event_relays_are_refused(self) -> None:
        for event in ("push", "merge_group", "workflow_dispatch", "workflow_call", "repository_dispatch"):
            with self.subTest(event=event):
                run = dict(request_run(), event=event)
                self.assertIn("request event", self.check(run, [pull_request()])[0])

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


class FreshRequestTest(unittest.TestCase):
    """A cumulative PR request cannot authorize an unrelated new head."""

    @staticmethod
    def records(names: list[str], parent: str = BASE) -> dict:
        return {"status": "ahead", "base_commit": {"sha": parent}, "merge_base_commit": {"sha": parent},
                "files": [{"filename": name, "status": "modified", "patch": "@@ -1 +1 @@\n-old request\n+fresh request"}
                          for name in names]}

    def delta(self, names: list[str]) -> tuple[list[dict], list[str]]:
        return authorize.request_delta(HEAD, {"sha": HEAD, "parents": [{"sha": BASE}]}, [self.records(names)])

    def test_exact_head_affirmative_requests(self) -> None:
        for name, expected in (("benchmarks/9700x/a.c", (True, False, [])),
                               ("benchmarks/9700x/a.data", (True, False, [])),
                               (authorize.COMPARE_REQUEST, (False, True, [])),
                               (authorize.SCALING_REQUEST, (False, True, [])),
                               ("src/compiler.c", (False, False, []))):
            with self.subTest(name=name):
                files, failures = self.delta([name])
                self.assertEqual(failures, [])
                self.assertEqual(authorize.plan(files), expected)

    def test_inherited_request_in_either_merge_parent_is_not_fresh(self) -> None:
        other = "c" * 40
        commit = {"sha": HEAD, "parents": [{"sha": BASE}, {"sha": other}]}
        request = authorize.COMPARE_REQUEST
        for comparisons in ([self.records([request]), self.records(["src/x.c"], other)],
                            [self.records(["src/x.c"]), self.records([request], other)]):
            with self.subTest(comparisons=comparisons):
                files, failures = authorize.request_delta(HEAD, commit, comparisons)
                self.assertEqual((authorize.plan(files), failures), ((False, False, []), []))
        files, failures = authorize.request_delta(HEAD, commit, [
            self.records([request]), self.records([request], other)])
        self.assertEqual((authorize.plan(files), failures), ((False, True, []), []))


    def test_merge_of_old_request_histories_needs_a_new_common_request_line(self) -> None:
        other = "c" * 40
        commit = {"sha": HEAD, "parents": [{"sha": BASE}, {"sha": other}]}
        first, second = self.records([authorize.COMPARE_REQUEST]), self.records([authorize.COMPARE_REQUEST], other)
        first["files"][0]["patch"] = "@@ -1 +1,2 @@\n existing-feature\n+inherited-main"
        second["files"][0]["patch"] = "@@ -1 +1,2 @@\n+existing-feature\n inherited-main"
        files, failures = authorize.request_delta(HEAD, commit, [first, second])
        self.assertEqual((files, failures), ([], []))
        first["files"][0]["patch"] += "\n+fresh-head-request"
        second["files"][0]["patch"] += "\n+fresh-head-request"
        files, failures = authorize.request_delta(HEAD, commit, [first, second])
        self.assertEqual((authorize.plan(files), failures), ((False, True, []), []))
        # Missing patch data fails closed instead of assuming a new request.
        del first["files"][0]["patch"]
        files, failures = authorize.request_delta(HEAD, commit, [first, second])
        self.assertTrue(failures)
        self.assertEqual(files, [])

    def test_malformed_or_capped_provenance_fails_closed(self) -> None:
        commit = {"sha": HEAD, "parents": [{"sha": BASE}]}
        good = self.records([authorize.COMPARE_REQUEST])
        changes = (
            (None, [good]), (dict(commit, sha=BASE), [good]),
            (dict(commit, parents=[]), []), (dict(commit, parents=[{"sha": BASE}] * 3), [good] * 3),
            (dict(commit, parents=[{"sha": []}]), [good]),
            (commit, []), (commit, [dict(good, status="diverged")]),
            (commit, [dict(good, base_commit={"sha": HEAD})]),
            (commit, [dict(good, merge_base_commit={"sha": HEAD})]),
            (commit, [dict(good, files=None)]),
            (commit, [dict(good, files=InventoryTest.rows(authorize.COMPARE_FILE_LIMIT))]),
            (commit, [dict(good, files=[{"filename": authorize.COMPARE_REQUEST}])]),
            (commit, [dict(good, files=good["files"] * 2)]),
        )
        for changed, comparisons in changes:
            with self.subTest(commit=changed, comparisons=comparisons):
                files, failures = authorize.request_delta(HEAD, changed, comparisons)
                self.assertTrue(failures)
                self.assertEqual(files, [])
        files, failures = self.delta([f"src/f{index}.c" for index in range(299)])
        self.assertEqual((len(files), failures), (299, []))

    def test_main_entry_binds_fresh_request_or_emits_no_host_plan(self) -> None:
        # Exercise the real entrypoint with trusted API records: a stale
        # cumulative PR request must not leak true host outputs.
        for fresh in (False, True):
            with self.subTest(fresh=fresh), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "output"
                pull = dict(pull_request(), number=42, changed_files=1)
                commit = {"sha": HEAD, "parents": [{"sha": BASE}], "commit": {"tree": {"sha": "d" * 40}}}
                compared = self.records([authorize.COMPARE_REQUEST] if fresh else ["src/x.c"])
                compared["merge_base_commit"] = {"sha": BASE, "commit": {"tree": {"sha": "e" * 40}}}
                prefix = f"/repos/{REPOSITORY}"
                replies = {
                    f"{prefix}/actions/runs/91": request_run(),
                    f"{prefix}/commits/{HEAD}/pulls?per_page=100": [pull],
                    f"{prefix}/pulls/42": pull,
                    f"{prefix}/pulls/42/files?per_page=100&page=1": [
                        {"filename": authorize.COMPARE_REQUEST, "status": "modified"}],
                    f"{prefix}/commits/{HEAD}": commit,
                    f"{prefix}/compare/{BASE}...{HEAD}": compared,
                }
                environment = {"BQ_REPOSITORY": REPOSITORY, "BQ_REQUEST_RUN_ID": "91",
                               "BQ_HEAD_COMMIT": HEAD, "BQ_RUN_ATTEMPT": "1",
                               "GH_TOKEN": "offline", "GITHUB_OUTPUT": str(output)}
                with mock.patch.dict(authorize.os.environ, environment, clear=True), \
                        mock.patch.object(authorize, "fetch", side_effect=lambda path, token: replies[path]):
                    self.assertEqual(authorize.main(), 0)
                values = dict(line.split("=", 1) for line in output.read_text().splitlines())
                self.assertEqual(values["request_head"], HEAD)
                self.assertEqual(values["compare"], str(fresh).lower())
                self.assertEqual(values["workloads"], "false")



class SchedulingPolicyTest(unittest.TestCase):
    def test_raddebugger_event_matrix_and_exact_commit(self) -> None:
        text = policy.RADDEBUGGER.read_text()
        errors: list[str] = []
        policy.check_postmerge_diagnostics(errors, text)
        self.assertEqual(errors, [])
        for event in ("pull_request", "merge_group", "workflow_dispatch", "workflow_call",
                      "repository_dispatch", "pull_request_target"):
            with self.subTest(event=event):
                changed = text.replace("    branches: [main]", f"    branches: [main]\n\n  {event}:")
                errors = []
                policy.check_postmerge_diagnostics(errors, changed)
                self.assertTrue(errors)
        for before, after in (("branches: [main]", "branches: [feature]"),
                              ("ref: ${{ github.sha }}", "ref: ${{ github.ref }}"),
                              ("cancel-in-progress: false", "cancel-in-progress: true")):
            with self.subTest(before=before):
                errors = []
                policy.check_postmerge_diagnostics(errors, text.replace(before, after))
                self.assertTrue(errors)

    def test_automatic_marker_and_bridge_have_no_hidden_event(self) -> None:
        for path, expected in ((policy.COMPILER_REQUEST, policy.COMPILER_REQUEST_TRIGGER),
                               (policy.DIRECT, policy.DIRECT_TRIGGER),
                               (policy.DIRECT_REQUEST, policy.DIRECT_REQUEST_TRIGGER)):
            text = path.read_text()
            self.assertEqual(policy.trigger_block(text), expected)
            for event in ("merge_group", "workflow_call", "repository_dispatch", "workflow_dispatch"):
                with self.subTest(path=path.name, event=event):
                    end = text.index("\npermissions:")
                    changed = text[:end] + f"\n  {event}:\n" + text[end:]
                    self.assertNotEqual(policy.trigger_block(changed), expected)

    def test_new_direct_group_label_reusable_and_relay_routes_are_rejected(self) -> None:
        extra = policy.WORKFLOWS / "unrequested.yml"
        routes = (
            "on:\n  pull_request:\njobs:\n  bench:\n    runs-on: [self-hosted, Linux, X64]\n",
            "on:\n  merge_group:\njobs:\n  bench:\n    runs-on:\n      group: buster-9700x-service-dispatch\n",
            "on:\n  workflow_run:\n    workflows: [9700X compiler benchmark request]\n",
            "on:\n  workflow_call:\njobs:\n  bench:\n    runs-on: ryzen-9700x\n",
            "jobs:\n  bench:\n    uses: ./.github/workflows/9700x-direct-bench.yml\n",
        )
        for route in routes:
            with self.subTest(route=route):
                errors: list[str] = []
                policy.check_runner_routes(errors, {extra: route})
                self.assertTrue(errors)
        # The same audit covers composite actions, not just workflow filenames.
        errors = []
        policy.check_runner_routes(errors, {policy.ROOT / ".github/actions/relay/action.yml": routes[-1]})
        self.assertTrue(errors)
        errors = []
        policy.check_runner_routes(errors, {extra: "# Never uses self-hosted\njobs:\n  test:\n    runs-on: ubuntu-latest\n"})
        self.assertEqual(errors, [])

    def test_postmerge_checks_cannot_become_required(self) -> None:
        for context in ("9700X compiler benchmark", "9700X compiler benchmark (pull request)",
                        "RAD Debugger compatibility", "linux-x86-64"):
            with self.subTest(context=context):
                errors: list[str] = []
                policy.check_premerge_checks(errors, {"rules": [{"parameters": {
                    "required_status_checks": [{"context": context}]}}]}, "")
                self.assertTrue(errors)
        errors = []
        policy.check_premerge_checks(errors, {"rules": [{"parameters": {
            "required_status_checks": [{"context": "Benchmark service workflow policy"}]}}]}, "")
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
