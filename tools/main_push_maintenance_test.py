#!/usr/bin/env python3
"""Regression tests for neutral, ordered main-push maintenance."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "main_push_maintenance", Path(__file__).with_name("main_push_maintenance.py")
)
assert SPEC is not None and SPEC.loader is not None
M = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = M
SPEC.loader.exec_module(M)


class InventoryApi:
    repository = "buster14a/buster"

    def __init__(self, inventories=None, jobs=None):
        self.inventories = list(inventories or [[]])
        self.jobs = jobs or {}
        self.reads = 0

    def request(self, path, **_query):
        if path.startswith("actions/workflows/") and path.endswith("/runs"):
            index = min(self.reads, len(self.inventories) - 1)
            self.reads += 1
            return {"workflow_runs": self.inventories[index]}
        match = M.re.fullmatch(r"actions/runs/([1-9][0-9]*)/jobs", path)
        if match:
            return {"jobs": self.jobs.get(int(match.group(1)), [])}
        raise AssertionError(path)


class MainApi:
    repository = "buster14a/buster"

    def __init__(self, heads):
        self.heads = list(heads)
        self.last = self.heads[-1]
        self.posts = []

    def request(self, path, *, method="GET", body=None, **_query):
        if path == "git/ref/heads/main" and method == "GET":
            value = self.heads.pop(0) if self.heads else self.last
            self.last = value
            return {"object": {"sha": value}}
        if method != "GET":
            self.posts.append((path, method, body))
            return {"id": len(self.posts)}
        raise AssertionError(path)

    def all(self, path, **query):
        raise AssertionError((path, query))


def workflow_run(run_id, number, sha, status="in_progress"):
    return {
        "id": run_id, "run_number": number, "event": "push",
        "head_branch": "main", "head_sha": sha, "status": status,
    }


def workflow_job(name, status):
    return {"name": name, "status": status}


class OrderingTests(unittest.TestCase):
    A, B, C = "a" * 40, "b" * 40, "c" * 40
    WORKFLOW, JOB = "merge-conflict-preflight.yml", "Exact merge-tree preflight"

    def test_waits_for_only_the_older_target_job(self):
        older = workflow_run(101, 1, self.A)
        api = InventoryApi(
            inventories=[[older], [dict(older, status="completed")]],
            jobs={101: [workflow_job(self.JOB, "in_progress")]},
        )
        waited = M.wait_predecessors(
            api, self.WORKFLOW, 2, self.JOB, 1,
            poll=0, clear_reads=1, sleep=lambda _seconds: None,
        )
        self.assertEqual(waited, [{"id": 101, "run_number": 1, "head_sha": self.A}])
        unrelated = InventoryApi(
            inventories=[[older]],
            jobs={101: [workflow_job(self.JOB, "completed"),
                        workflow_job("unrelated", "in_progress")]},
        )
        self.assertEqual(M.wait_predecessors(
            unrelated, self.WORKFLOW, 2, self.JOB, 0,
            poll=0, clear_reads=1, sleep=lambda _seconds: None,
        ), [])

    def test_queued_job_and_exact_successor_identity(self):
        queued = workflow_run(101, 1, self.A, "queued")
        self.assertEqual(M._predecessors(
            InventoryApi([[queued]]), self.WORKFLOW, 2, self.JOB
        )[0]["id"], 101)
        api = InventoryApi([[
            workflow_run(102, 2, self.B),
            workflow_run(103, 3, self.C, "queued"),
        ]])
        self.assertEqual(M._successor(api, self.WORKFLOW, 1, self.C), {
            "id": 103, "run_number": 3, "status": "queued", "head_sha": self.C,
        })
        self.assertIsNone(M._successor(api, self.WORKFLOW, 3, self.C))


class ReconcileTests(unittest.TestCase):
    A, B, C = "a" * 40, "b" * 40, "c" * 40

    @staticmethod
    def action(calls, status=0, movement=False):
        def invoke(main):
            calls.append(main)
            return M.Action(status, main, movement, {"main": main})
        return invoke

    @staticmethod
    def successor(calls):
        def find(main):
            calls.append(main)
            return {"id": len(calls), "run_number": 10 + len(calls),
                    "status": "queued", "head_sha": main}
        return find

    def test_three_rapid_pushes_leave_latest_authoritative(self):
        actions, successors = [], []
        first = M.reconcile(
            self.A, iter((self.A, self.C)).__next__,
            self.action(actions), self.successor(successors),
        )
        middle = M.reconcile(
            self.B, lambda: self.C,
            self.action(actions), self.successor(successors),
        )
        latest = M.reconcile(
            self.C, iter((self.C, self.C)).__next__,
            self.action(actions), self.successor(successors),
        )
        self.assertEqual(
            [(row[0], row[1]["status"]) for row in (first, middle, latest)],
            [(0, "superseded-after-action"),
             (0, "superseded-before-action"), (0, "current")],
        )
        self.assertEqual(actions, [self.A, self.C])
        self.assertEqual(successors, [self.C, self.C])

    def test_only_explicit_movement_is_neutral(self):
        generic = M.reconcile(
            self.A, iter((self.A, self.B)).__next__,
            self.action([], 7, False), self.successor([]),
        )
        moved = M.reconcile(
            self.A, iter((self.A, self.B)).__next__,
            self.action([], 2, True), self.successor([]),
        )
        self.assertEqual((generic[0], generic[1]["status"]), (7, "failed"))
        self.assertEqual((moved[0], moved[1]["status"]),
                         (0, "superseded-after-action"))
        report = {"failed": [{
            "scope": "default_branch", "stage": "recheck",
            "error": {"message": "default branch moved during snapshot refresh"},
        }]}
        self.assertTrue(M._movement_only(report))
        report["failed"].append({
            "scope": "refresh", "stage": "publish",
            "error": {"message": "POST failed"},
        })
        self.assertFalse(M._movement_only(report))


class IntegrationTests(unittest.TestCase):
    A, C = "a" * 40, "c" * 40

    @staticmethod
    def ordering(successor=None):
        return (
            mock.patch.object(M, "wait_predecessors", return_value=[]),
            mock.patch.object(M, "wait_successor", return_value=successor or {
                "id": 3, "run_number": 3, "status": "queued", "head_sha": "c" * 40,
            }),
        )

    def test_refresh_promotes_current_but_retains_moved_attempt(self):
        for moved in (False, True):
            with self.subTest(moved=moved), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                reports, summary = root / "reports", root / "summary.md"
                api = MainApi([self.A, self.C if moved else self.A])

                def refresh_event(_repo, _api, _event, attempt, attempt_summary, _context):
                    failures = [] if not moved else [{
                        "scope": "default_branch", "stage": "recheck",
                        "error": {"message": "default branch moved during snapshot refresh"},
                    }]
                    payload = {"main": self.A, "failed": failures}
                    attempt_summary.write_text("attempt summary\n")
                    (attempt / "refresh.json").write_text(json.dumps(payload))
                    return 2 if moved else 0

                predecessor, successor = self.ordering()
                with predecessor, successor, mock.patch.object(
                        M.refresh, "refresh_event", side_effect=refresh_event):
                    status = M.refresh_push(
                        root, api, {"repository": {"default_branch": "main"}},
                        self.A, reports, summary, M.preflight.STATUS_CONTEXT,
                        "merge-conflict-preflight.yml", 1,
                        "Exact merge-tree preflight", 30,
                    )
                self.assertEqual(status, 0)
                maintenance = json.loads(
                    (reports / "main-push-maintenance.json").read_text())
                self.assertEqual(
                    maintenance["status"],
                    "superseded-after-action" if moved else "current",
                )
                self.assertEqual((reports / "refresh.json").exists(), not moved)
                self.assertTrue((reports / "attempt/refresh.json").exists())
                self.assertEqual("attempt summary" in summary.read_text(), not moved)

    def test_invalidation_blocks_a_stale_write_and_noops_when_superseded(self):
        api = MainApi([self.A, self.C, self.C])

        def invalidate(guarded, main, _details):
            guarded.request("check-runs", method="POST", body={"name": "gate"})
            self.fail("stale mutation succeeded")

        predecessor, successor = self.ordering()
        with predecessor, successor, mock.patch.object(
                M.retirement, "invalidate_stale", side_effect=invalidate):
            status, report = M.invalidate_push(
                api, self.A, "https://example.invalid/run/1",
                "api-migration-policy.yml", 1,
                "Native retirement merge admission", 30,
            )
        self.assertEqual(status, 0)
        self.assertEqual(api.posts, [])
        self.assertEqual(report["status"], "superseded-after-action")
        self.assertEqual(report["partial_writes"], [])

        superseded = MainApi([self.C])
        predecessor, successor = self.ordering()
        with predecessor, successor, mock.patch.object(
                M.retirement, "invalidate_stale") as invalidator:
            status, report = M.invalidate_push(
                superseded, self.A, "https://example.invalid/run/1",
                "api-migration-policy.yml", 1,
                "Native retirement merge admission", 30,
            )
        self.assertEqual(status, 0)
        invalidator.assert_not_called()
        self.assertEqual(report["status"], "superseded-before-action")


class WorkflowTests(unittest.TestCase):
    def test_exact_sha_policy_ordering_and_trust_pin(self):
        api = (ROOT / ".github/workflows/api-migration-policy.yml").read_text()
        preflight = (ROOT / ".github/workflows/merge-conflict-preflight.yml").read_text()
        regression = (
            ROOT / ".github/workflows/merge-conflict-preflight-regression.yml"
        ).read_text()
        for workflow in (api, preflight):
            self.assertIn("github.event_name == 'push' && github.sha", workflow)
            self.assertIn(
                "cancel-in-progress: ${{ github.event_name != 'push' }}", workflow
            )
        self.assertIn('--predecessor-job "Native retirement merge admission"', api)
        self.assertIn('--predecessor-job "Exact merge-tree preflight"', preflight)
        self.assertIn("actions: read", preflight)
        self.assertIn(
            "github.event_name == 'push' && github.sha || "
            "github.event.repository.default_branch", preflight,
        )
        self.assertIn("tools/main_push_maintenance_test.py -v", regression)
        digest = hashlib.sha256(
            (ROOT / "tools/main_push_maintenance.py").read_bytes()
        ).hexdigest()
        self.assertIn("MAIN_PUSH_MAINTENANCE_SHA256: " + digest, api)
        self.assertIn("sha256sum --check --strict", api)


if __name__ == "__main__":
    unittest.main()
