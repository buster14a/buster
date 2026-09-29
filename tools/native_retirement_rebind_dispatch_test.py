#!/usr/bin/env python3
"""Offline exact-queue fixtures for the short rebind dispatcher."""

from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import merge_queue_admission as admission
import native_retirement_rebind_dispatch as dispatch


class Reader:
    def __init__(self, main, refs):
        self.main, self.refs, self.runs = main, dict(refs), []

    def get(self, path, **_query):
        sha = self.main if path == "git/ref/heads/main" else self.refs[path.removeprefix("git/ref/")]
        return {"object": {"sha": sha}}

    def pages(self, path, field, **query):
        assert path == "actions/workflows/native-retirement-rebind.yml/runs"
        assert field == "workflow_runs" and query == {"event": "workflow_dispatch", "branch": "main"}
        return list(self.runs)


class Writer:
    def __init__(self):
        self.sent = []

    def dispatch(self, candidate):
        self.sent.append(dict(candidate))


class DispatchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.origin, work = root / "origin.git", root / "work"
        admission.git(root, "init", "--bare", "-b", "main", str(self.origin))
        admission.git(root, "init", "-b", "main", str(work))
        admission.git(work, "config", "user.name", "Rebind fixture")
        admission.git(work, "config", "user.email", "rebind@example.invalid")
        for name in ("a.c", "b.c"):
            (work / name).write_text("base\n")
        admission.git(work, "add", ".")
        admission.git(work, "commit", "-qm", "M")
        self.main = admission.git(work, "rev-parse", "HEAD")
        prs = []
        for name in ("a.c", "b.c"):
            admission.git(work, "checkout", "-q", "-b", name, self.main)
            (work / name).write_text("changed\n")
            admission.git(work, "commit", "-qam", name)
            prs.append(admission.git(work, "rev-parse", "HEAD"))
        tree = admission.git(work, "merge-tree", "--write-tree", self.main, prs[0])
        self.g1 = admission.git(work, "commit-tree", tree, "-p", self.main, "-p", prs[0], "-m", "G1")
        tree = admission.git(work, "merge-tree", "--write-tree", self.g1, prs[1])
        self.g2 = admission.git(work, "commit-tree", tree, "-p", self.g1, "-p", prs[1], "-m", "G2")
        self.ref1, self.ref2 = admission.QUEUE_REF_PREFIX + "first", admission.QUEUE_REF_PREFIX + "second"
        admission.git(work, "push", "-q", str(self.origin), f"{self.main}:refs/heads/main",
                      f"{self.g1}:{self.ref1}", f"{self.g2}:{self.ref2}")
        self.work = work
        self.trusted = root / "trusted"
        admission.git(root, "clone", "-q", str(self.origin), str(self.trusted))
        self.reader = Reader(self.main, {self.ref1.removeprefix("refs/"): self.g1,
                                         self.ref2.removeprefix("refs/"): self.g2})
        self.writer = Writer()
        self.args = SimpleNamespace(repo_root=self.trusted, repository="buster14a/buster")

    def tearDown(self):
        self.directory.cleanup()

    def reconcile(self):
        return dispatch.reconcile(self.args, self.reader, self.writer)

    def test_first_group_dispatches_once_then_second_after_landing(self):
        with patch.object(admission.time, "sleep", side_effect=AssertionError("dispatch must not sleep")):
            result = self.reconcile()
        self.assertEqual([row["status"] for row in result["groups"]], ["dispatched", "waiting"])
        self.assertEqual([row["head"] for row in self.writer.sent], [self.g1])
        self.reader.runs = [{"display_title": "Native rebind " + self.g1,
                             "path": dispatch.WORKFLOW_PATH, "event": "workflow_dispatch",
                             "head_branch": "main", "repository": {"full_name": self.args.repository},
                             "id": 17, "run_attempt": 1, "status": "in_progress"}]
        self.assertEqual(self.reconcile()["groups"][0]["status"], "present")
        self.assertEqual(len(self.writer.sent), 1)
        admission.git(self.work, "push", "-q", "-f", str(self.origin), f"{self.g1}:refs/heads/main")
        self.reader.main = self.g1
        self.assertEqual(self.reconcile()["groups"][1]["status"], "stale-policy")
        admission.git(self.trusted, "fetch", "-q", "origin", "main")
        admission.git(self.trusted, "checkout", "-q", "--detach", self.g1)
        self.assertEqual(self.reconcile()["groups"][1]["status"], "dispatched")
        self.assertEqual([row["head"] for row in self.writer.sent], [self.g1, self.g2])

    def test_replaced_ref_fails_closed(self):
        self.reader.refs[self.ref1.removeprefix("refs/")] = "f" * 40
        result = self.reconcile()
        self.assertEqual(result["groups"][0]["status"], "retry")
        self.assertEqual(self.writer.sent, [])

    def test_same_name_run_from_wrong_workflow_never_suppresses_work(self):
        self.reader.runs = [{"display_title": "Native rebind " + self.g1,
                             "path": ".github/workflows/unrelated.yml", "event": "workflow_dispatch",
                             "head_branch": "main", "repository": {"full_name": self.args.repository},
                             "id": 20}]
        result = self.reconcile()
        self.assertEqual(result["groups"][0]["status"], "retry")
        self.assertEqual(self.writer.sent, [])

    def test_worker_workflow_uses_main_only_and_zero_second_preflight(self):
        root = Path(__file__).resolve().parents[1]
        worker = (root / ".github/workflows/native-retirement-rebind.yml").read_text()
        controller = (root / ".github/workflows/native-retirement-rebind-dispatch.yml").read_text()
        self.assertNotIn("  merge_group:\n", worker)
        self.assertIn("run-name: ${{ github.event_name == 'workflow_dispatch'", worker)
        self.assertIn("--wait-seconds 0", worker)
        self.assertNotIn("--wait-seconds 18000", worker)
        self.assertIn("ref: main", controller)
        self.assertIn("timeout-minutes: 5", controller)
        self.assertIn("actions: write", controller)
        self.assertNotIn("checks: write", controller)
        self.assertIn("cancel-in-progress: false", controller)


if __name__ == "__main__":
    unittest.main()
