#!/usr/bin/env python3
"""Network-free source and current-run tests for main CI reuse."""

import copy
from datetime import datetime, timedelta, timezone
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import github_ci_time as inventory
import main_ci_reuse as reuse
from merge_queue_admission import AdmissionError, GitHub

SHA = "a" * 40
BLOB = "b" * 40
SOURCE_ID = 100
CURRENT_ID = 101
NOW = datetime(2026, 9, 29, 9, 0, tzinfo=timezone.utc)
BRANCH = "gh-readonly-queue/main/pr-7-abc"


def run(run_id, event, branch, when):
    return {"id": run_id, "workflow_id": reuse.WORKFLOW_ID,
            "path": reuse.WORKFLOW_PATH, "head_sha": SHA,
            "head_commit": {"id": SHA}, "head_branch": branch, "event": event,
            "run_attempt": 1, "status": "completed" if event == "merge_group" else "in_progress",
            "conclusion": "success" if event == "merge_group" else None,
            "repository": {"id": reuse.REPOSITORY_ID, "full_name": reuse.REPOSITORY},
            "head_repository": {"id": reuse.REPOSITORY_ID},
            "created_at": when.isoformat(), "updated_at": when.isoformat()}


def job(name, run_id, *, status="completed", conclusion="success"):
    required = set(inventory._required_job_steps(name))
    for candidate, _, mandatory in reuse.REUSED:
        if name == candidate:
            required.add(mandatory)
            if name.startswith("Windows") and name.endswith("native"):
                required.add("Native MSVC reference differential")
            if name.endswith("native") and not name.startswith("Windows"):
                required.add("Native configuration differential matrix")
    return {"id": len(name) + run_id, "name": name, "run_id": run_id,
            "run_attempt": 1, "head_sha": SHA, "status": status,
            "conclusion": conclusion,
            "steps": [{"name": step, "status": "completed", "conclusion": "success"}
                      for step in sorted(required)]}


class FakeAPI:
    def __init__(self):
        self.current = run(CURRENT_ID, "push", "main", NOW - timedelta(minutes=1))
        self.source = run(SOURCE_ID, "merge_group", BRANCH, NOW - timedelta(minutes=2))
        self.jobs = [job(name, SOURCE_ID) for name in inventory.COMBINATION_JOBS]
        for index, record in enumerate(self.jobs):
            record["id"] = 1000 + index
        self.artifacts = []
        for index, (_, prefix, _) in enumerate(reuse.REUSED):
            self.artifacts.append({
                "id": index + 1, "name": f"{prefix}-{SOURCE_ID}-1", "size_in_bytes": 100,
                "expired": False, "expires_at": (NOW + timedelta(days=1)).isoformat(),
                "digest": "sha256:" + "c" * 64,
                "workflow_run": {"id": SOURCE_ID, "repository_id": reuse.REPOSITORY_ID,
                                 "head_repository_id": reuse.REPOSITORY_ID,
                                 "head_sha": SHA, "head_branch": BRANCH}})
        self.main_jobs = [job(name, CURRENT_ID, status="in_progress" if name == "CI complete" else "completed",
                              conclusion=None if name == "CI complete" else "success")
                          for name in reuse.RETAINED_NAMES]
        self.main_jobs.append(job(inventory.MAIN_REUSE_JOB, CURRENT_ID))
        self.main_jobs += [job(name, CURRENT_ID, conclusion="skipped") for name in reuse.REUSED_NAMES]
        for index, record in enumerate(self.main_jobs):
            record["id"] = 2000 + index
        self.movement = False

    def get(self, path, **query):
        if path == f"actions/runs/{CURRENT_ID}":
            return copy.deepcopy(self.current)
        if path == f"actions/runs/{SOURCE_ID}":
            value = copy.deepcopy(self.source)
            if self.movement:
                value["run_attempt"] = 2
            return value
        if path == "contents/" + reuse.WORKFLOW_PATH and query == {"ref": SHA}:
            return {"type": "file", "sha": BLOB}
        raise AssertionError(path)

    def pages(self, path, field, **query):
        if path == "actions/runs" and field == "workflow_runs":
            return [copy.deepcopy(self.source)]
        if path == f"actions/runs/{SOURCE_ID}/attempts/1/jobs" and field == "jobs":
            return copy.deepcopy(self.jobs)
        if path == f"actions/runs/{SOURCE_ID}/artifacts" and field == "artifacts":
            return copy.deepcopy(self.artifacts)
        if path == f"actions/runs/{CURRENT_ID}/jobs" and field == "jobs":
            return copy.deepcopy(self.main_jobs)
        raise AssertionError((path, field, query))


DRAFT_PREDICATE = ("github.event_name == 'pull_request' && github.event.pull_request.draft && "
                   "github.run_attempt == '1'")


class MainCIReuseTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeAPI()

    def admit(self):
        return reuse.verify_source(self.api, SHA, CURRENT_ID, BLOB, NOW)

    def test_exact_commit_source_and_main_specific_jobs(self):
        receipt = self.admit()
        self.assertEqual(len(receipt["source_jobs"]), 8)
        self.assertEqual(len(receipt["source_artifacts"]), 8)
        self.assertEqual(receipt["source_run_id"], SOURCE_ID)
        self.assertEqual(len(reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)), 13)
        self.assertRegex(reuse.receipt_digest(receipt), r"[0-9a-f]{64}\Z")

    def test_wrong_identity_policy_and_event_fall_back(self):
        cases = (("current", "head_sha", "d" * 40),
                 ("current", "event", "workflow_dispatch"),
                 ("current", "run_attempt", 2),
                 ("source", "workflow_id", 1),
                 ("source", "head_sha", "d" * 40),
                 ("source", "head_branch", "feature"),
                 ("source", "run_attempt", 2),
                 ("source", "status", "in_progress"),
                 ("source", "conclusion", "failure"))
        for target, field, value in cases:
            with self.subTest(target=target, field=field):
                self.api = FakeAPI()
                getattr(self.api, target)[field] = value
                with self.assertRaises(AdmissionError):
                    self.admit()
        self.api = FakeAPI()
        self.api.source["repository"]["full_name"] = "another/repo"
        with self.assertRaises(AdmissionError):
            self.admit()
        self.api = FakeAPI()
        with self.assertRaisesRegex(AdmissionError, "workflow revision"):
            reuse.verify_source(self.api, SHA, CURRENT_ID, "d" * 40, NOW)

    def test_missing_failed_cancelled_and_skipped_coverage(self):
        for conclusion in (None, "failure", "cancelled", "skipped"):
            with self.subTest(conclusion=conclusion):
                self.api = FakeAPI()
                self.api.jobs[0]["conclusion"] = conclusion
                with self.assertRaises(AdmissionError):
                    self.admit()
        self.api = FakeAPI()
        self.api.jobs.pop(0)
        with self.assertRaises(AdmissionError):
            self.admit()
        self.api = FakeAPI()
        native = next(j for j in self.api.jobs if j["name"] == "Linux x86-64 native")
        native["steps"] = [s for s in native["steps"] if s["name"] != "Native configuration differential matrix"]
        with self.assertRaises(AdmissionError):
            self.admit()

    def test_expired_missing_and_wrong_artifacts(self):
        for field, value in (("expired", True), ("size_in_bytes", 0),
                             ("digest", "bad"), ("expires_at", (NOW - timedelta(seconds=1)).isoformat())):
            with self.subTest(field=field):
                self.api = FakeAPI()
                self.api.artifacts[0][field] = value
                with self.assertRaises(AdmissionError):
                    self.admit()
        self.api = FakeAPI()
        self.api.artifacts.pop()
        with self.assertRaises(AdmissionError):
            self.admit()
        self.api = FakeAPI()
        self.api.artifacts[0]["workflow_run"]["head_sha"] = "d" * 40
        with self.assertRaises(AdmissionError):
            self.admit()

    def test_stale_source_and_attempt_movement(self):
        self.api.source["updated_at"] = (NOW - timedelta(hours=3)).isoformat()
        with self.assertRaises(AdmissionError):
            self.admit()
        self.api = FakeAPI()
        self.api.movement = True
        with self.assertRaises(AdmissionError):
            self.admit()

    def test_current_recheck_refuses_missing_red_or_executed_jobs(self):
        for conclusion in ("failure", "cancelled", "skipped"):
            with self.subTest(conclusion=conclusion):
                self.api = FakeAPI()
                self.api.main_jobs[0]["conclusion"] = conclusion
                with self.assertRaises(AdmissionError):
                    reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)
        self.api = FakeAPI()
        self.api.main_jobs.pop(0)
        with self.assertRaises(AdmissionError):
            reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)
        self.api = FakeAPI()
        self.api.main_jobs[-1]["conclusion"] = "success"
        with self.assertRaises(AdmissionError):
            reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)

    def test_api_uncertainty_and_incomplete_pagination_fall_back(self):
        with mock.patch.object(self.api, "pages", side_effect=OSError("API unavailable")):
            with self.assertRaises(OSError):
                self.admit()
        with mock.patch.object(self.api, "pages", side_effect=AdmissionError("incomplete pagination")):
            with self.assertRaisesRegex(AdmissionError, "pagination"):
                self.admit()

    def test_decision_falls_back_but_finish_fails_closed_on_api_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "output"
            environment = dict(os.environ, GITHUB_REPOSITORY=reuse.REPOSITORY,
                               GITHUB_EVENT_NAME="push", GITHUB_REF="refs/heads/main",
                               GITHUB_SHA=SHA, GITHUB_RUN_ID=str(CURRENT_ID),
                               GITHUB_RUN_ATTEMPT="1", GH_TOKEN="fixture",
                               GITHUB_OUTPUT=str(output), GITHUB_STEP_SUMMARY=str(root / "summary"))
            with mock.patch.dict(os.environ, environment), mock.patch.object(reuse, "GitHub", side_effect=OSError("API offline")):
                with mock.patch.object(sys, "argv", ["main_ci_reuse.py", "decide", "--output", str(root / "receipt")]):
                    self.assertEqual(reuse.cli(), 0)
                self.assertEqual(output.read_text(), "reuse=false\n")
                with mock.patch.object(sys, "argv", ["main_ci_reuse.py", "finish", "--output", str(root / "receipt")]):
                    self.assertEqual(reuse.cli(), 1)

    def test_real_pagination_reader_rejects_partial_or_moving_totals(self):
        api = object.__new__(GitHub)
        api.get = mock.Mock(side_effect=[{"total_count": 101, "jobs": [{}] * 100},
                                             {"total_count": 102, "jobs": [{}]}])
        with self.assertRaisesRegex(AdmissionError, "truncated"):
            api.pages("actions/runs/1/jobs", "jobs")
        api.get = mock.Mock(return_value={"total_count": 100, "jobs": [{}] * 100})
        with self.assertRaisesRegex(AdmissionError, "pagination limit"):
            api.pages("actions/runs/1/jobs", "jobs")

    def test_workflow_wiring_and_inventory_match_policy(self):
        text = (Path(__file__).resolve().parents[1] / reuse.WORKFLOW_PATH).read_text()
        self.assertEqual(reuse.REUSED_NAMES,
                         set(inventory.NATIVE + inventory.MOBILE + inventory.UEFI))
        self.assertEqual(len(reuse.RETAINED_NAMES), 13)
        for key in ("native", "mobile", "uefi"):
            header = re.split(r"\n  [a-z][a-z_]*:\n", text.split(f"\n  {key}:\n", 1)[1], maxsplit=1)[0]
            self.assertIn("needs: reuse", header)
            self.assertIn("needs.reuse.outputs.reuse != 'true'", header)
            self.assertNotIn("GITHUB_EVENT_NAME", header)
            # Draft deferral and queue fail-fast are false on both push and
            # merge_group, so they cannot make the reused jobs event-dependent.
            header = header.replace(DRAFT_PREDICATE, "")
            self.assertNotIn("github.event_name", re.sub(r"^      fail-fast:.*$", "", header, flags=re.M))
        self.assertIn("name: Main CI reuse decision", text)
        self.assertIn("main_ci_reuse.py finish", text)
        self.assertIn("needs.reuse.outputs.reuse == 'true'", text)


if __name__ == "__main__":
    unittest.main()
