#!/usr/bin/env python3
"""Regression tests for neutral, convergent main-push maintenance."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import sys


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = Path(__file__).with_name("main_push_maintenance.py")
SPEC = importlib.util.spec_from_file_location("main_push_maintenance", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MAINTENANCE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MAINTENANCE
SPEC.loader.exec_module(MAINTENANCE)


class SequenceApi:
    repository = "buster14a/buster"

    def __init__(self, heads: list[str]):
        self.heads = list(heads)
        self.last = self.heads[-1]

    def request(self, path: str, **_kwargs):
        if path != "git/ref/heads/main":
            raise AssertionError(path)
        value = self.heads.pop(0) if self.heads else self.last
        self.last = value
        return {"object": {"sha": value}}


class ReconcileTests(unittest.TestCase):
    A = "a" * 40
    B = "b" * 40
    C = "c" * 40

    @staticmethod
    def action(status: int = 0, retryable: bool = False):
        calls = []

        def run(main: str, _index: int):
            calls.append(main)
            return MAINTENANCE.ActionResult(
                status, main, retryable, {"main": main, "status": status}
            )

        return calls, run

    def test_push_already_superseded_is_a_green_noop(self):
        calls, action = self.action()
        result = MAINTENANCE.reconcile(self.A, lambda: self.C, action)
        self.assertEqual(result.status, 0)
        self.assertEqual(result.report["status"], "superseded")
        self.assertEqual(result.report["current_main"], self.C)
        self.assertEqual(calls, [])

    def test_three_rapid_pushes_converge_to_the_latest_revision(self):
        calls, action = self.action()
        heads = iter((self.A, self.C, self.C))
        result = MAINTENANCE.reconcile(self.A, lambda: next(heads), action)
        self.assertEqual(result.status, 0)
        self.assertEqual(result.report["status"], "current")
        self.assertEqual(result.report["current_main"], self.C)
        self.assertEqual(calls, [self.A, self.C])
        self.assertEqual(
            [(row["requested_main"], row["observed_main"])
             for row in result.report["attempts"]],
            [(self.A, self.C), (self.C, self.C)],
        )

        middle_calls, middle_action = self.action()
        middle = MAINTENANCE.reconcile(self.B, lambda: self.C, middle_action)
        self.assertEqual(middle.status, 0)
        self.assertEqual(middle.report["status"], "superseded")
        self.assertEqual(middle_calls, [])

        latest_calls, latest_action = self.action()
        latest_heads = iter((self.C, self.C))
        latest = MAINTENANCE.reconcile(
            self.C, lambda: next(latest_heads), latest_action
        )
        self.assertEqual(latest.status, 0)
        self.assertEqual(latest_calls, [self.C])

    def test_real_action_failure_is_not_reclassified_as_superseded(self):
        calls, action = self.action(status=7, retryable=False)
        heads = iter((self.A, self.B))
        result = MAINTENANCE.reconcile(self.A, lambda: next(heads), action)
        self.assertEqual(result.status, 7)
        self.assertEqual(result.report["status"], "failed")
        self.assertEqual(calls, [self.A])

    def test_only_explicit_default_branch_movement_is_retryable(self):
        movement = {
            "failed": [{
                "scope": "default_branch",
                "stage": "recheck",
                "error": {"message": (
                    "default branch moved during snapshot refresh: " +
                    self.A + " -> " + self.B
                )},
            }],
        }
        self.assertTrue(MAINTENANCE._movement_only(movement))
        failure = json.loads(json.dumps(movement))
        failure["failed"][0]["scope"] = "pull_request"
        self.assertFalse(MAINTENANCE._movement_only(failure))
        failure = json.loads(json.dumps(movement))
        failure["failed"].append({
            "scope": "refresh", "stage": "publish",
            "error": {"message": "status POST failed"},
        })
        self.assertFalse(MAINTENANCE._movement_only(failure))


class ActionIntegrationTests(unittest.TestCase):
    A = "a" * 40
    C = "c" * 40

    def test_refresh_promotes_only_the_stable_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report_dir = root / "reports"
            summary = root / "summary.md"
            api = SequenceApi([self.A, self.C, self.C])
            calls = []

            def refresh_event(_repo, _api, _event, attempt_dir, attempt_summary,
                              _context):
                calls.append(attempt_dir.name)
                attempt_summary.write_text("attempt " + attempt_dir.name + "\n")
                if len(calls) == 1:
                    payload = {
                        "main": self.A,
                        "failed": [{
                            "scope": "default_branch",
                            "stage": "recheck",
                            "error": {"message": (
                                "default branch moved during snapshot refresh: " +
                                self.A + " -> " + self.C
                            )},
                        }],
                    }
                    status = 2
                else:
                    payload = {"main": self.C, "failed": [], "coverage_complete": True}
                    status = 0
                (attempt_dir / "refresh.json").write_text(json.dumps(payload))
                (attempt_dir / "result.json").write_text(json.dumps({"main": payload["main"]}))
                return status

            with mock.patch.object(
                    MAINTENANCE.refresh, "refresh_event", side_effect=refresh_event):
                status = MAINTENANCE.refresh_main_push(
                    root, api, {"repository": {"default_branch": "main"}},
                    self.A, report_dir, summary,
                    MAINTENANCE.preflight.STATUS_CONTEXT,
                )
            self.assertEqual(status, 0)
            self.assertEqual(calls, ["attempt-1", "attempt-2"])
            self.assertEqual(json.loads((report_dir / "refresh.json").read_text())["main"],
                             self.C)
            self.assertEqual(json.loads((report_dir / "result.json").read_text())["main"],
                             self.C)
            maintenance = json.loads(
                (report_dir / "main-push-maintenance.json").read_text())
            self.assertEqual(maintenance["status"], "current")
            self.assertEqual(maintenance["current_main"], self.C)
            self.assertIn("attempt attempt-2", summary.read_text())
            self.assertNotIn("attempt attempt-1", summary.read_text())

    def test_native_invalidation_replays_after_main_moves(self):
        api = SequenceApi([self.A, self.C, self.C])
        calls = []

        def invalidate(_api, main, _details):
            calls.append(main)
            return {
                "schema": MAINTENANCE.retirement.SCHEMA,
                "status": "invalidated",
                "main": main,
                "pull_requests": [{"pull_request": 1}],
            }

        with mock.patch.object(
                MAINTENANCE.retirement, "invalidate_stale", side_effect=invalidate):
            status, report = MAINTENANCE.invalidate_main_push(
                api, self.A, "https://github.com/buster14a/buster/actions/runs/1"
            )
        self.assertEqual(status, 0)
        self.assertEqual(calls, [self.A, self.C])
        self.assertEqual(report["main"], self.C)
        self.assertEqual(report["maintenance"]["status"], "current")

    def test_superseded_native_invalidation_does_not_write(self):
        api = SequenceApi([self.C])
        with mock.patch.object(MAINTENANCE.retirement, "invalidate_stale") as invalidate:
            status, report = MAINTENANCE.invalidate_main_push(
                api, self.A, "https://github.com/buster14a/buster/actions/runs/1"
            )
        self.assertEqual(status, 0)
        invalidate.assert_not_called()
        self.assertEqual(report["status"], "superseded")
        self.assertEqual(report["main"], self.C)


class WorkflowPolicyTests(unittest.TestCase):
    def test_push_concurrency_is_exact_sha_and_non_cancelling(self):
        api_workflow = (ROOT / ".github/workflows/api-migration-policy.yml").read_text()
        preflight_workflow = (
            ROOT / ".github/workflows/merge-conflict-preflight.yml").read_text()
        for workflow in (api_workflow, preflight_workflow):
            self.assertIn("github.event_name == 'push' && github.sha", workflow)
            self.assertIn(
                "cancel-in-progress: ${{ github.event_name != 'push' }}", workflow)
        self.assertIn("github.event.pull_request.number", api_workflow)
        self.assertIn("github.event.merge_group.head_sha", api_workflow)
        self.assertIn("github.event.workflow_run.id", preflight_workflow)
        self.assertIn("github.event.merge_group.head_sha", preflight_workflow)

        shas = ("1" * 40, "2" * 40, "3" * 40)
        api_groups = {"api-migration-policy-push-" + sha for sha in shas}
        preflight_groups = {
            "merge-conflict-preflight-push-" + sha for sha in shas
        }
        self.assertEqual(len(api_groups), 3)
        self.assertEqual(len(preflight_groups), 3)

    def test_workflows_route_push_side_effects_through_reconciler(self):
        api_workflow = (ROOT / ".github/workflows/api-migration-policy.yml").read_text()
        preflight_workflow = (
            ROOT / ".github/workflows/merge-conflict-preflight.yml").read_text()
        regression = (
            ROOT / ".github/workflows/merge-conflict-preflight-regression.yml").read_text()
        self.assertIn(
            "tools/main_push_maintenance.py invalidate-native-retirement",
            api_workflow,
        )
        self.assertIn(
            "tools/main_push_maintenance.py refresh-merge-conflicts",
            preflight_workflow,
        )
        self.assertIn("tools/main_push_maintenance_test.py -v", api_workflow)
        self.assertIn("tools/main_push_maintenance_test.py -v", regression)
        self.assertIn("tools/merge_conflict_preflight_refresh.py", preflight_workflow)
        self.assertIn("github.event_name == 'workflow_dispatch'", preflight_workflow)


if __name__ == "__main__":
    unittest.main()
