#!/usr/bin/env python3
"""Deterministic metadata-race tests for the off-host PR request resolver."""

from __future__ import annotations

import copy
import unittest

from resolve_pr_request import REPOSITORY, ResolutionError, resolve_pr

BASE = "a" * 40
HEAD = "b" * 40
OTHER = "c" * 40
BASE_TREE = "d" * 40
HEAD_TREE = "e" * 40


def pull(base: str = BASE, head: str = HEAD) -> dict:
    return {
        "number": 1190,
        "state": "open",
        "base": {"sha": base, "ref": "main", "repo": {"full_name": REPOSITORY}},
        "head": {"sha": head, "repo": {"full_name": REPOSITORY}},
    }


class Fetch:
    def __init__(self, first: dict | None = None, second: dict | None = None) -> None:
        self.pulls = [first or pull(), second or first or pull()]
        self.reads = 0
        self.commits = {
            BASE: {"sha": BASE, "commit": {"tree": {"sha": BASE_TREE}}},
            HEAD: {"sha": HEAD, "commit": {"tree": {"sha": HEAD_TREE}}},
            OTHER: {"sha": OTHER, "commit": {"tree": {"sha": "f" * 40}}},
        }

    def __call__(self, path: str) -> dict:
        if path.endswith("/pulls/1190"):
            result = self.pulls[min(self.reads, 1)]
            self.reads += 1
        else:
            result = self.commits[path.rsplit("/", 1)[-1]]
        return copy.deepcopy(result)


class ResolverTest(unittest.TestCase):
    def test_stable_immutable_record_and_new_attempt(self) -> None:
        first = resolve_pr(Fetch(), 1190, "try-1")
        retry = resolve_pr(Fetch(), 1190, "try-1")
        second_attempt = resolve_pr(Fetch(), 1190, "try-2")
        self.assertEqual(first, retry)
        self.assertNotEqual(first["request_sha256"], second_attempt["request_sha256"])
        self.assertEqual(first["candidate_commit"], HEAD)
        self.assertEqual(first["candidate_tree"], HEAD_TREE)
        self.assertEqual(first["baseline_tree"], BASE_TREE)
        self.assertFalse(first["dispatch_ready"])

    def test_explicit_baseline_is_fixed(self) -> None:
        record = resolve_pr(Fetch(), 1190, "fixed", OTHER)
        self.assertEqual(record["pr_base_commit"], BASE)
        self.assertEqual(record["baseline_commit"], OTHER)

    def test_push_and_base_advance_fail(self) -> None:
        with self.assertRaisesRegex(ResolutionError, "changed during resolution"):
            resolve_pr(Fetch(second=pull(head=OTHER)), 1190, "push")
        with self.assertRaisesRegex(ResolutionError, "changed during resolution"):
            resolve_pr(Fetch(second=pull(base=OTHER)), 1190, "advance")

    def test_fork_deleted_ref_and_wrong_repository_fail(self) -> None:
        fork = pull()
        fork["head"]["repo"]["full_name"] = "untrusted/fork"
        with self.assertRaisesRegex(ResolutionError, "fork"):
            resolve_pr(Fetch(first=fork), 1190, "fork")
        deleted = pull()
        deleted["head"]["repo"] = None
        with self.assertRaisesRegex(ResolutionError, "fork"):
            resolve_pr(Fetch(first=deleted), 1190, "deleted")
        wrong = pull()
        wrong["base"]["repo"]["full_name"] = "someone/else"
        with self.assertRaisesRegex(ResolutionError, "base repository"):
            resolve_pr(Fetch(first=wrong), 1190, "wrong")

    def test_substituted_commit_or_tree_fails(self) -> None:
        fetch = Fetch()
        fetch.commits[HEAD]["sha"] = OTHER
        with self.assertRaisesRegex(ResolutionError, "substituted"):
            resolve_pr(fetch, 1190, "substitution")
        fetch = Fetch()
        fetch.commits[HEAD]["commit"]["tree"]["sha"] = "NOT-A-TREE"
        with self.assertRaisesRegex(ResolutionError, "invalid commit tree"):
            resolve_pr(fetch, 1190, "tree")

    def test_invalid_key_and_closed_pr_fail(self) -> None:
        with self.assertRaisesRegex(ResolutionError, "idempotency key"):
            resolve_pr(Fetch(), 1190, "bad key")
        closed = pull()
        closed["state"] = "closed"
        with self.assertRaisesRegex(ResolutionError, "closed"):
            resolve_pr(Fetch(first=closed), 1190, "closed")


if __name__ == "__main__":
    unittest.main()
