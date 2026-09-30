#!/usr/bin/env python3
"""Regression tests for the bounded default-branch preflight refresh."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = Path(__file__).with_name("merge_conflict_preflight_refresh.py")
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "merge-conflict-preflight.yml"
REGRESSION_WORKFLOW_PATH = (REPO_ROOT / ".github" / "workflows" /
                            "merge-conflict-preflight-regression.yml")
SPEC = importlib.util.spec_from_file_location("merge_conflict_preflight_refresh", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
REFRESH = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = REFRESH
SPEC.loader.exec_module(REFRESH)
PREFLIGHT = REFRESH.preflight


class SnapshotRefreshTests(unittest.TestCase):
    MAIN = "a" * 40
    MOVED_MAIN = "b" * 40
    HEAD_ONE = "1" * 40
    HEAD_ONE_MOVED = "3" * 40
    HEAD_TWO = "2" * 40
    HEAD_THREE = "4" * 40

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="buster-preflight-snapshot-")
        self.root = Path(self.temporary.name)
        self.output = self.root / "reports"
        self.api = mock.Mock()
        self.api.queue_membership.return_value = {}
        self.event = {"repository": {"default_branch": "main"}}

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def row(number: int, head: str, base: str = "main") -> dict:
        return {"number": number, "head": {"sha": head}, "base": {"ref": base}}

    @classmethod
    def identity(cls, number: int, head: str) -> REFRESH.PullIdentity:
        return REFRESH.PullIdentity(number, head, "main")

    @classmethod
    def report(cls, head: str, blocking: bool = False, clean: bool = True) -> dict:
        return {
            "main": {"sha": cls.MAIN},
            "head": {"sha": head},
            "merge": {"clean": clean},
            "outcome": {"blocking": blocking},
            "candidate_changes": {
                "generated_or_integration_owned_retirement_paths": [],
            },
        }

    @staticmethod
    def resolution(identities: list[REFRESH.PullIdentity]) -> REFRESH.HeadResolution:
        return REFRESH.HeadResolution(
            {identity.key: identity.head for identity in identities}, {}, 1, 0)

    def run_refresh(self) -> tuple[int, dict]:
        status = REFRESH.refresh_event(
            self.root, self.api, self.event, self.output, None,
            PREFLIGHT.STATUS_CONTEXT)
        report = json.loads((self.output / "refresh.json").read_text(encoding="utf-8"))
        return status, report

    def test_stable_snapshot_pins_main_and_avoids_per_pull_identity_reads(self) -> None:
        identities = [
            self.identity(1, self.HEAD_ONE),
            self.identity(2, self.HEAD_TWO),
            self.identity(3, self.HEAD_THREE),
        ]
        rows = [self.row(identity.number, identity.head) for identity in identities]
        self.api.open_pull_requests.side_effect = [rows, rows]
        with (mock.patch.object(REFRESH, "_fetch_main",
                                side_effect=[self.MAIN, self.MAIN]) as fetch_main,
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)) as fetch_heads,
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  self.report(identity.head),
              ) as analyze):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 0)
        self.assertTrue(refresh["coverage_complete"])
        self.assertEqual(refresh["strategy"], "pinned-main-batched-head-snapshot-v1")
        self.assertEqual(refresh["inventory_reads"], 2)
        self.assertEqual(refresh["main_fetches"], 2)
        self.assertEqual(refresh["head_batch_fetches"], 1)
        self.assertEqual(fetch_main.call_count, 2)
        self.assertEqual(fetch_heads.call_count, 1)
        self.assertEqual(analyze.call_count, 3)
        self.assertEqual(self.api.publish_status.call_count, 3)
        self.api.pull_request.assert_not_called()
        self.api.previous_status.assert_not_called()

    def test_moved_head_reconciles_only_the_changed_identity(self) -> None:
        first = [self.identity(1, self.HEAD_ONE), self.identity(2, self.HEAD_TWO)]
        second = [self.identity(1, self.HEAD_ONE_MOVED), self.identity(2, self.HEAD_TWO)]
        first_rows = [self.row(identity.number, identity.head) for identity in first]
        second_rows = [self.row(identity.number, identity.head) for identity in second]
        self.api.open_pull_requests.side_effect = [first_rows, second_rows, second_rows]
        resolutions = [self.resolution(first), self.resolution([second[0]])]
        with (mock.patch.object(
                  REFRESH, "_fetch_main",
                  side_effect=[self.MAIN, self.MAIN, self.MAIN],
              ),
              mock.patch.object(REFRESH, "_fetch_heads", side_effect=resolutions),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  self.report(identity.head),
              ) as analyze):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 0)
        self.assertTrue(refresh["coverage_complete"])
        self.assertEqual(analyze.call_count, 3)
        published_heads = [call.args[0] for call in self.api.publish_status.call_args_list]
        self.assertEqual(published_heads.count(self.HEAD_TWO), 1)
        self.assertEqual(
            set(published_heads), {self.HEAD_ONE, self.HEAD_ONE_MOVED, self.HEAD_TWO})
        self.assertEqual(
            {(row["pull_request"], row["head"]) for row in refresh["completed"]},
            {(1, self.HEAD_ONE_MOVED), (2, self.HEAD_TWO)},
        )
        self.assertEqual(
            {(row["pull_request"], row["head"]) for row in refresh["superseded"]},
            {(1, self.HEAD_ONE)},
        )

    def test_main_movement_never_reports_mixed_main_coverage_complete(self) -> None:
        identities = [self.identity(1, self.HEAD_ONE), self.identity(2, self.HEAD_TWO)]
        self.api.open_pull_requests.return_value = [
            self.row(identity.number, identity.head) for identity in identities]
        with (mock.patch.object(
                  REFRESH, "_fetch_main", side_effect=[self.MAIN, self.MOVED_MAIN]),
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  self.report(identity.head),
              )):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 2)
        self.assertFalse(refresh["coverage_complete"])
        self.assertEqual(refresh["completed"], [])
        self.assertEqual(refresh["not_attempted"], [1, 2])
        self.assertEqual(len(refresh["superseded_main_results"]), 2)
        self.assertEqual(self.api.open_pull_requests.call_count, 1)
        self.assertIn("default branch moved", refresh["failed"][0]["error"]["message"])

    def test_systemic_api_failure_stops_without_hammering_rechecks(self) -> None:
        identities = [
            self.identity(1, self.HEAD_ONE),
            self.identity(2, self.HEAD_TWO),
            self.identity(3, self.HEAD_THREE),
        ]
        self.api.open_pull_requests.return_value = [
            self.row(identity.number, identity.head) for identity in identities]
        systemic = PREFLIGHT.ApiRequestError("HTTP 429 rate limit", 1, True, True)
        with (mock.patch.object(REFRESH, "_fetch_main", return_value=self.MAIN) as fetch_main,
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=[self.report(self.HEAD_ONE), systemic],
              ) as analyze):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 2)
        self.assertEqual(fetch_main.call_count, 1)
        self.assertEqual(self.api.open_pull_requests.call_count, 1)
        self.assertEqual(analyze.call_count, 2)
        self.assertEqual([row["pull_request"] for row in refresh["completed"]], [1])
        self.assertEqual(refresh["failed"][0]["pull_request"], 2)
        self.assertEqual(refresh["not_attempted"], [3])
        self.assertEqual(self.api.publish_status.call_count, 1)

    def test_ambiguous_status_write_fails_closed_and_later_heads_continue(self) -> None:
        identities = [
            self.identity(1, self.HEAD_ONE),
            self.identity(2, self.HEAD_TWO),
            self.identity(3, self.HEAD_THREE),
        ]
        rows = [self.row(identity.number, identity.head) for identity in identities]
        self.api.open_pull_requests.side_effect = [rows, rows]
        ambiguous = PREFLIGHT.ApiRequestError(
            "status POST connection reset", 1, False, False)
        self.api.publish_status.side_effect = [ambiguous, None, None]
        with (mock.patch.object(REFRESH, "_fetch_main",
                                side_effect=[self.MAIN, self.MAIN]),
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  self.report(identity.head),
              )):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 2)
        self.assertFalse(refresh["coverage_complete"])
        self.assertEqual(
            [row["pull_request"] for row in refresh["completed"]], [2, 3])
        self.assertEqual(refresh["failed"][0]["pull_request"], 1)
        self.assertEqual(refresh["failed"][0]["stage"], "publish")
        self.assertEqual(refresh["failed"][0]["status_publication"], "unknown")
        self.assertEqual(self.api.publish_status.call_count, 3)

    def test_budget_exhaustion_retains_remaining_not_attempted_identities(self) -> None:
        identities = [
            self.identity(1, self.HEAD_ONE),
            self.identity(2, self.HEAD_TWO),
            self.identity(3, self.HEAD_THREE),
        ]
        self.api.open_pull_requests.return_value = [
            self.row(identity.number, identity.head) for identity in identities]
        budget = PREFLIGHT.RefreshBudgetError(
            "merge-conflict refresh time budget exhausted")
        resolution = REFRESH.HeadResolution(
            {identities[0].key: identities[0].head},
            {identities[1].key: budget, identities[2].key: budget},
            1, 0)
        with (mock.patch.object(REFRESH, "_fetch_main", return_value=self.MAIN) as fetch_main,
              mock.patch.object(REFRESH, "_fetch_heads", return_value=resolution),
              mock.patch.object(REFRESH, "_analyze_identity",
                                return_value=self.report(self.HEAD_ONE))):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 2)
        self.assertFalse(refresh["coverage_complete"])
        self.assertEqual(fetch_main.call_count, 1)
        self.assertEqual(self.api.open_pull_requests.call_count, 1)
        self.assertEqual(
            [row["pull_request"] for row in refresh["completed"]], [1])
        self.assertEqual(refresh["not_attempted"], [2, 3])
        self.assertEqual(refresh["failed"][0]["scope"], "refresh")

    def test_complete_snapshot_with_conflicts_remains_green(self) -> None:
        identities = [self.identity(1, self.HEAD_ONE), self.identity(2, self.HEAD_TWO)]
        rows = [self.row(identity.number, identity.head) for identity in identities]
        self.api.open_pull_requests.side_effect = [rows, rows]
        with (mock.patch.object(REFRESH, "_fetch_main",
                                side_effect=[self.MAIN, self.MAIN]),
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  self.report(identity.head, blocking=True),
              )):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 0)
        self.assertTrue(refresh["coverage_complete"])
        self.assertEqual(refresh["blocking_count"], 2)

    def test_queued_conflicted_pull_is_named_for_dequeue_without_changing_outcome(self) -> None:
        identities = [
            self.identity(1, self.HEAD_ONE),
            self.identity(2, self.HEAD_TWO),
            self.identity(3, self.HEAD_THREE),
        ]
        rows = [self.row(identity.number, identity.head) for identity in identities]
        self.api.open_pull_requests.side_effect = [rows, rows]
        # #1 is queued and conflicts, #2 is queued and clean, #3 conflicts unqueued.
        self.api.queue_membership.return_value = {
            1: (self.HEAD_ONE, True),
            2: (self.HEAD_TWO, True),
            3: (self.HEAD_THREE, False),
        }
        reports = {
            self.HEAD_ONE: self.report(self.HEAD_ONE, blocking=True, clean=False),
            self.HEAD_TWO: self.report(self.HEAD_TWO),
            self.HEAD_THREE: self.report(self.HEAD_THREE, blocking=True, clean=False),
        }
        with (mock.patch.object(REFRESH, "_fetch_main",
                                side_effect=[self.MAIN, self.MAIN]),
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  reports[identity.head],
              )):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 0)
        self.assertTrue(refresh["coverage_complete"])
        self.assertEqual(refresh["blocking_count"], 2)
        self.api.queue_membership.assert_called_once_with("main")
        self.assertEqual(refresh["merge_queue"], {
            "lookup": "complete",
            "error": None,
            "queued_pull_requests": [1, 2],
            "dequeue_required": [1],
        })
        published = {call.args[0]: call.args[1] for call in self.api.publish_status.call_args_list}
        self.assertEqual(published[self.HEAD_ONE]["merge_queue"]["action"],
                         PREFLIGHT.QUEUE_LOCKED_ACTION)
        self.assertTrue(published[self.HEAD_TWO]["merge_queue"]["branch_locked_by_queue"])
        self.assertIsNone(published[self.HEAD_TWO]["merge_queue"]["action"])
        self.assertEqual(published[self.HEAD_THREE]["merge_queue"]["membership"],
                         PREFLIGHT.QUEUE_NOT_QUEUED)
        self.assertIsNone(published[self.HEAD_THREE]["merge_queue"]["action"])

    def test_unavailable_queue_lookup_is_advisory_and_marks_membership_unknown(self) -> None:
        identities = [self.identity(1, self.HEAD_ONE), self.identity(2, self.HEAD_TWO)]
        rows = [self.row(identity.number, identity.head) for identity in identities]
        self.api.open_pull_requests.side_effect = [rows, rows]
        self.api.queue_membership.side_effect = PREFLIGHT.ApiRequestError(
            "POST /graphql HTTP 403: Resource not accessible by integration", 1, False, False)
        with (mock.patch.object(REFRESH, "_fetch_main",
                                side_effect=[self.MAIN, self.MAIN]),
              mock.patch.object(REFRESH, "_fetch_heads",
                                return_value=self.resolution(identities)),
              mock.patch.object(
                  REFRESH, "_analyze_identity",
                  side_effect=lambda _repo, _api, _main, identity, _context:
                  self.report(identity.head, blocking=identity.number == 1,
                              clean=identity.number != 1),
              )):
            status, refresh = self.run_refresh()
        self.assertEqual(status, 0)
        self.assertTrue(refresh["coverage_complete"])
        self.assertEqual(refresh["merge_queue"]["lookup"], "unavailable")
        self.assertIn("Resource not accessible", refresh["merge_queue"]["error"])
        self.assertEqual(refresh["merge_queue"]["dequeue_required"], [])
        self.assertEqual(self.api.publish_status.call_count, 2)
        conflicted = self.api.publish_status.call_args_list[0].args[1]
        self.assertEqual(conflicted["merge_queue"]["membership"], PREFLIGHT.QUEUE_UNKNOWN)
        self.assertEqual(conflicted["merge_queue"]["action"], PREFLIGHT.QUEUE_UNKNOWN_ACTION)
        self.assertIsNone(self.api.publish_status.call_args_list[1].args[1]["merge_queue"]["action"])

    def test_queue_summary_names_locked_pulls_and_unavailable_lookups(self) -> None:
        summary = self.root / "summary.md"
        REFRESH._write_queue_summary({
            "lookup": "complete", "error": None,
            "queued_pull_requests": [1521, 1523], "dequeue_required": [1521, 1523],
        }, summary)
        REFRESH._write_queue_summary({
            "lookup": "unavailable", "error": "HTTP 403",
            "queued_pull_requests": [], "dequeue_required": [],
        }, summary)
        text = summary.read_text(encoding="utf-8")
        self.assertIn("Queued PR(s) that conflict with main: #1521, #1523.", text)
        self.assertIn("(GH006)", text)
        self.assertIn("dequeue each one before pushing its resolution", text)
        self.assertIn("Merge-queue membership unavailable: HTTP 403", text)

    def test_queue_summary_is_not_attempted_before_main_is_resolved(self) -> None:
        self.api.open_pull_requests.side_effect = PREFLIGHT.ApiRequestError(
            "GET /pulls HTTP 500", 3, True, False)
        status, refresh = self.run_refresh()
        self.assertEqual(status, 2)
        self.assertEqual(refresh["merge_queue"]["lookup"], "not_attempted")
        self.api.queue_membership.assert_not_called()

    def test_analyze_skips_previous_status_but_keeps_retirement_attestation(self) -> None:
        identity = self.identity(1, self.HEAD_ONE)
        ordinary = self.report(identity.head)
        generated = self.report(identity.head)
        generated["candidate_changes"][
            "generated_or_integration_owned_retirement_paths"] = ["generated.json"]
        attested = self.report(identity.head)
        self.api.retirement_status.return_value = {"sha": identity.head, "statuses": []}
        with mock.patch.object(PREFLIGHT, "analyze",
                               side_effect=[ordinary, generated, attested]) as analyze:
            self.assertIs(REFRESH._analyze_identity(
                self.root, self.api, self.MAIN, identity, PREFLIGHT.STATUS_CONTEXT), ordinary)
            self.assertIs(REFRESH._analyze_identity(
                self.root, self.api, self.MAIN, identity, PREFLIGHT.STATUS_CONTEXT), attested)
        self.api.previous_status.assert_not_called()
        self.api.retirement_status.assert_called_once_with(identity.head)
        self.assertEqual(analyze.call_args_list[0].args, (self.root, self.MAIN, identity.head))
        self.assertIsNone(analyze.call_args_list[2].args[3])

    def test_head_fetches_are_bounded_batches_with_scoped_fallback(self) -> None:
        identities = [self.identity(index + 1, f"{index + 1:040x}") for index in range(65)]
        with mock.patch.object(
                REFRESH, "_fetch_head_batch",
                side_effect=lambda _repo, batch, _deadline:
                {identity.key: identity.head for identity in batch},
        ) as fetch_batch:
            resolution = REFRESH._fetch_heads(self.root, identities, None)
        self.assertEqual([len(call.args[1]) for call in fetch_batch.call_args_list], [32, 32, 1])
        self.assertEqual(resolution.batch_fetches, 3)
        self.assertEqual(resolution.fallback_fetches, 0)
        self.assertEqual(len(resolution.resolved), 65)

        first, second = identities[:2]
        with (mock.patch.object(REFRESH, "_fetch_head_batch",
                                side_effect=PREFLIGHT.PreflightError("missing ref")),
              mock.patch.object(PREFLIGHT, "_fetch_ref",
                                side_effect=[first.head, second.head]) as fetch_ref):
            fallback = REFRESH._fetch_heads(self.root, [first, second], None)
        self.assertEqual(fallback.batch_fetches, 1)
        self.assertEqual(fallback.fallback_fetches, 2)
        self.assertEqual(fetch_ref.call_count, 2)
        self.assertEqual(fallback.failed, {})

    def test_inventory_rejects_duplicate_numbers_and_wrong_base(self) -> None:
        with self.assertRaisesRegex(PREFLIGHT.PreflightError, "repeats"):
            REFRESH._inventory([
                self.row(1, self.HEAD_ONE), self.row(1, self.HEAD_TWO)], "main")
        with self.assertRaisesRegex(PREFLIGHT.PreflightError, "expected 'main'"):
            REFRESH._inventory([self.row(1, self.HEAD_ONE, "other")], "main")


class WorkflowRoutingTests(unittest.TestCase):
    def test_trusted_workflow_routes_only_default_branch_sweeps_to_snapshot_tool(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        regression = REGRESSION_WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("tools/merge_conflict_preflight_refresh.py", workflow)
        self.assertIn("github.event_name == 'push'", workflow)
        self.assertIn("github.event_name == 'workflow_dispatch'", workflow)
        self.assertIn("tools/merge_conflict_preflight.py github-event", workflow)
        self.assertIn("github.event_name != 'push'", workflow)
        self.assertIn("merge_conflict_preflight_refresh_test.py -v", regression)


if __name__ == "__main__":
    unittest.main()
