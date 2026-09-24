#!/usr/bin/env python3
"""Offline checks for the trusted merge-queue cancellation controller."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "recovery", ROOT / ".github/scripts/recover-ci.py")
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)

class FakeGitHub:
    def __init__(self):
        self.ruleset = json.loads((ROOT / ".github/main-merge-queue.ruleset.json").read_text())
        self.ruleset.update(id=recovery.RULESET_ID, source_type="Repository",
                            source="buster14a/buster")
        self.names = [row["context"] for rule in self.ruleset["rules"]
                      if rule["type"] == "required_status_checks"
                      for row in rule["parameters"]["required_status_checks"]]
        self.runs = []
        for index, filename in enumerate(dict.fromkeys(recovery.REQUIRED_WORKFLOW_PATHS.values())):
            self.runs.append({
                "id": 123 + index, "workflow_id": 456 + index,
                "check_suite_id": 700 + index, "head_sha": "a" * 40,
                "head_branch": "gh-readonly-queue/main/pr-1-abc",
                "path": ".github/workflows/" + filename,
                "head_repository": {"full_name": "buster14a/buster"},
                "event": "merge_group", "status": "in_progress", "conclusion": None,
                "run_attempt": 1,
            })
        self.event = {
            "action": "in_progress",
            "repository": {"full_name": "buster14a/buster", "default_branch": "main"},
            "workflow_run": copy.deepcopy(self.runs[0]),
        }
        self.jobs = [
            {"name": "Linux x86-64 release", "status": "in_progress", "conclusion": None},
        ]
        suites = {run["path"].removeprefix(".github/workflows/"): run["check_suite_id"]
                  for run in self.runs}
        self.checks = [
            {"id": 1000 + index, "name": name, "head_sha": "a" * 40,
             "app": {"id": recovery.GITHUB_ACTIONS_APP_ID},
             "check_suite": {"id": suites[recovery.REQUIRED_WORKFLOW_PATHS[name]]},
             "status": "in_progress", "conclusion": None}
            for index, name in enumerate(self.names)
        ]
        self.cancelled = []

    def request(self, path, *, method="GET", **query):
        if (path, method, query) != ("rulesets/" + str(recovery.RULESET_ID), "GET", {}):
            raise AssertionError((path, method, query))
        return copy.deepcopy(self.ruleset)

    def all(self, path, key=None, **query):
        if path == "actions/runs":
            assert key == "workflow_runs" and query == {
                "event": "merge_group", "head_sha": "a" * 40}
            result = self.runs
        elif path == "actions/runs/123/jobs":
            assert key == "jobs" and query == {"filter": "latest"}
            result = self.jobs
        elif path == "commits/" + "a" * 40 + "/check-runs":
            assert key == "check_runs" and query == {"filter": "all"}
            result = self.checks
        else:
            raise AssertionError((path, key, query))
        return copy.deepcopy(result)

    def cancel(self, run_id):
        self.cancelled.append(run_id)
        return True


class MergeQueueFailFastTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeGitHub()
        self.assertEqual(set(self.api.names), set(recovery.REQUIRED_WORKFLOW_PATHS))

    def watch(self, max_probes=1):
        return recovery.watch(self.api, self.api.event, sleep_fn=lambda _seconds: None,
                              max_probes=max_probes)

    def test_buster_job_failure_cancels_only_active_exact_head_runs(self):
        self.api.jobs[0].update(status="completed", conclusion="failure")
        self.api.runs[-1].update(status="completed", conclusion="success")
        message = self.watch()
        self.assertIn("Linux x86-64 release", message)
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs[:-1]])

    def test_other_required_check_fails_while_buster_is_healthy(self):
        check = next(row for row in self.api.checks if row["name"] == "Canonical TCC bootstrap")
        check.update(status="completed", conclusion="failure")
        self.assertIn("Canonical TCC bootstrap", self.watch())
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs])

    def test_optional_failure_does_not_cancel_required_work(self):
        self.api.checks.append({
            "id": 2000, "name": "GPU Metal consumer", "head_sha": "a" * 40,
            "app": {"id": recovery.GITHUB_ACTIONS_APP_ID},
            "check_suite": {"id": self.api.runs[3]["check_suite_id"]},
            "status": "completed", "conclusion": "failure",
        })
        with self.assertRaises(TimeoutError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_buster_success_does_not_end_watch_while_other_checks_run(self):
        self.api.runs[0].update(status="completed", conclusion="success")
        self.api.jobs[0].update(status="completed", conclusion="success")
        with self.assertRaises(TimeoutError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_all_required_checks_succeed(self):
        self.api.runs[0].update(status="completed", conclusion="success")
        self.api.jobs[0].update(status="completed", conclusion="success")
        for check in self.api.checks:
            check.update(status="completed", conclusion="success")
        self.assertIn("All required", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_unrelated_check_suite_cannot_trigger_cancellation(self):
        check = next(row for row in self.api.checks if row["name"] == "Canonical TCC bootstrap")
        check.update(status="completed", conclusion="failure", check_suite={"id": 999999})
        with self.assertRaises(TimeoutError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_changed_event_identity_cannot_cancel(self):
        self.api.event["workflow_run"]["workflow_id"] = 999
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_watcher_uses_trusted_checkout_and_job_permissions(self):
        workflow = (ROOT / ".github/workflows/ci-recovery.yml").read_text()
        watch = workflow.split("\n  watch-merge-group:", 1)[1].split("\n  recover:", 1)[0]
        self.assertIn("github.event.workflow_run.event == 'merge_group'", watch)
        self.assertIn("actions: write", watch)
        self.assertIn("checks: read", watch)
        self.assertIn("ref: ${{ github.sha }}", watch)
        self.assertNotIn("github.event.workflow_run.head_sha", watch)


if __name__ == "__main__":
    unittest.main()
