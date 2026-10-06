#!/usr/bin/env python3
"""Offline checks of the main-commit compiler comparison authorization (#2752)."""

from __future__ import annotations

import unittest
import urllib.error
from pathlib import Path
from unittest import mock
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize_compiler  # noqa: E402

REPOSITORY = "buster14a/buster"
HEAD, BASE, PULL_HEAD = "a" * 40, "b" * 40, "c" * 40
HEAD_TREE, BASE_TREE = "d" * 40, "e" * 40
OLDER, OLDER_TREE = "f" * 40, "1" * 40
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
    def check(self, value: dict, chain: list[str] | None = None) -> tuple[list[str], dict]:
        return authorize_compiler.verify(REPOSITORY, 91, HEAD, value["run"], value["commit"], value["base_commit"],
                                         value["on_main"], value["pulls"], chain)

    def test_landed_merge_commit_is_measured_against_its_first_parent(self) -> None:
        # Bot-authored pull requests are measured once they land on main.
        self.assertEqual(self.check(records()), ([], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                      "pull": "2774", "pull_head": PULL_HEAD,
                                                      "first_parent": BASE, "range": "1"}))

    def test_range_baseline_must_be_on_the_first_parent_chain(self) -> None:
        value = records()
        value["base_commit"] = {"sha": OLDER, "commit": {"tree": {"sha": OLDER_TREE}}}
        failures, result = self.check(value, [BASE, OLDER])
        self.assertEqual(failures, [])
        self.assertEqual((result["base"], result["base_tree"], result["first_parent"], result["range"]),
                         (OLDER, OLDER_TREE, BASE, "2"))
        # Off the chain (the pull request side, or beyond the bounded chain), or a chain that does not
        # start at the first parent, is refused.
        for chain in (None, [BASE], [PULL_HEAD, OLDER], [OLDER]):
            with self.subTest(chain=chain):
                failures, result = self.check(value, chain)
                self.assertIn("baseline commit is on the first-parent chain", failures)
                self.assertEqual(result, {})

    def test_choose_base_takes_the_nearest_valid_measurement(self) -> None:
        def row(sha: str, conclusion: str = "success", status: str = "completed", app: int = 15368) -> dict:
            return {"id": 1, "name": "9700X compiler benchmark", "head_sha": sha, "status": status,
                    "conclusion": conclusion, "app": {"id": app},
                    "external_id": f"buster-9700x-compiler-main-v1:{sha}:91.1:1"}
        chain = [BASE, PULL_HEAD, OLDER]
        self.assertEqual(authorize_compiler.choose_base(chain, {BASE: [row(BASE)], OLDER: [row(OLDER)]}), BASE)
        # Not measured, failed, still open or foreign checks do not count; the nearest valid one does.
        checks = {BASE: [row(BASE, "skipped"), row(BASE, "failure"), row(BASE, None, "in_progress"),
                         row(BASE, app=1)],
                  PULL_HEAD: [dict(row(PULL_HEAD), head_sha=OLDER)], OLDER: [row(OLDER)]}
        self.assertEqual(authorize_compiler.choose_base(chain, checks), OLDER)
        # A pre-#2803 single check counts; nothing measured falls back to the first parent.
        legacy = dict(row(PULL_HEAD), external_id=f"buster-9700x-compiler-main-v1:{PULL_HEAD}")
        self.assertEqual(authorize_compiler.choose_base(chain, {PULL_HEAD: [legacy]}), PULL_HEAD)
        self.assertEqual(authorize_compiler.choose_base(chain, {}), BASE)

    def test_direct_push_and_older_main_commit(self) -> None:
        value = records()
        value["commit"]["parents"] = [{"sha": BASE}]
        value["on_main"] = {"status": "ahead"}
        value["pulls"] = []
        self.assertEqual(self.check(value)[1], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                "pull": "0", "pull_head": HEAD, "first_parent": BASE, "range": "1"})

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

    def test_main_chain_reads_the_listing_and_stops_at_the_first_measurement(self) -> None:
        listing = [{"sha": HEAD, "parents": [{"sha": BASE}, {"sha": PULL_HEAD}]},
                   {"sha": PULL_HEAD, "parents": [{"sha": OLDER}]},
                   {"sha": BASE, "parents": [{"sha": OLDER}]}, {"sha": OLDER, "parents": [{"sha": "2" * 40}]}]
        measured = {"id": 1, "name": "9700X compiler benchmark", "head_sha": OLDER, "status": "completed",
                    "conclusion": "success", "app": {"id": 15368},
                    "external_id": f"buster-9700x-compiler-main-v1:{OLDER}:90.1:1"}
        reads: list[str] = []

        def fetch(path: str, token: str) -> object:
            reads.append(path)
            if path.startswith("/repos/x/y/commits?"):
                return listing
            return {"check_runs": [measured] if f"/commits/{OLDER}/" in path else []}
        commit = records()["commit"]
        with mock.patch.object(authorize_compiler, "fetch", side_effect=fetch):
            self.assertEqual(authorize_compiler.main_chain("/repos/x/y", "t", HEAD, commit),
                             ([BASE, OLDER, "2" * 40], OLDER))
        self.assertEqual(len([path for path in reads if "/check-runs?" in path]), 2)

        def broken(path: str, token: str) -> object:
            raise urllib.error.URLError("unreachable")
        with mock.patch.object(authorize_compiler, "fetch", side_effect=broken), mock.patch("sys.stderr"):
            self.assertEqual(authorize_compiler.main_chain("/repos/x/y", "t", HEAD, commit), ([BASE], BASE))
        self.assertEqual(authorize_compiler.main_chain("/repos/x/y", "t", HEAD, {"parents": []}), ([], ""))


if __name__ == "__main__":
    unittest.main()
