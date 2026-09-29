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
                "path": ".github/workflows/" + filename + "@main",
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
        suites = {recovery.workflow_file(run["path"]).removeprefix(".github/workflows/"): run["check_suite_id"]
                  for run in self.runs}
        self.checks = [
            {"id": 1000 + index, "name": name, "head_sha": "a" * 40,
             "app": {"id": recovery.GITHUB_ACTIONS_APP_ID},
             "check_suite": {"id": suites[recovery.REQUIRED_WORKFLOW_PATHS[name]]},
             "status": "in_progress", "conclusion": None}
            for index, name in enumerate(self.names)
        ]
        self.cancelled = []
        self.refs = [{"ref": "refs/heads/gh-readonly-queue/main/pr-1-abc",
                      "object": {"sha": "a" * 40}}]

    def request(self, path, *, method="GET", **query):
        if (path, method, query) == ("rulesets/" + str(recovery.RULESET_ID), "GET", {}):
            result = self.ruleset
        elif (path, method, query) == (
                "git/matching-refs/heads/gh-readonly-queue/main/", "GET", {}):
            result = self.refs
        else:
            raise AssertionError((path, method, query))
        return copy.deepcopy(result)

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

    def test_workflow_run_path_normalizes_branch_qualified_identity(self):
        self.assertEqual(recovery.workflow_file(".github/workflows/ci.yml@main"),
                         recovery.WORKFLOW_PATH)
        self.assertEqual(recovery.workflow_file(".github/workflows/ci.yml@refs/heads/main"),
                         recovery.WORKFLOW_PATH)
        self.assertIsNone(recovery.workflow_file(".github/workflows/ci.yml@"))

    def watch(self):
        return recovery.watch(self.api, self.api.event)

    def test_buster_job_failure_cancels_only_active_exact_head_runs(self):
        self.api.jobs[0].update(status="completed", conclusion="failure")
        self.api.runs[-1].update(status="completed", conclusion="success")
        message = self.watch()
        self.assertIn("Linux x86-64 release", message)
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs[:-1]])

    def test_other_required_check_fails_while_buster_is_healthy(self):
        check = next(row for row in self.api.checks if row["name"] == "Canonical TCC bootstrap")
        check.update(status="completed", conclusion="failure")
        self.api.runs[2].update(status="completed", conclusion="failure")
        self.assertIn("Canonical TCC bootstrap", self.watch())
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs
                                              if run["status"] != "completed"])

    def test_optional_failure_does_not_cancel_required_work(self):
        self.api.checks.append({
            "id": 2000, "name": "GPU Metal consumer", "head_sha": "a" * 40,
            "app": {"id": recovery.GITHUB_ACTIONS_APP_ID},
            "check_suite": {"id": self.api.runs[3]["check_suite_id"]},
            "status": "completed", "conclusion": "failure",
        })
        self.assertIn("remain pending", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_buster_success_does_not_end_watch_while_other_checks_run(self):
        self.api.runs[0].update(status="completed", conclusion="success")
        self.api.jobs[0].update(status="completed", conclusion="success")
        self.assertIn("remain pending", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_all_required_checks_succeed(self):
        self.api.runs[0].update(status="completed", conclusion="success")
        self.api.jobs[0].update(status="completed", conclusion="success")
        for run in self.api.runs:
            run.update(status="completed", conclusion="success")
        for check in self.api.checks:
            check.update(status="completed", conclusion="success")
        self.assertIn("All required", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_unrelated_check_suite_cannot_trigger_cancellation(self):
        check = next(row for row in self.api.checks if row["name"] == "Canonical TCC bootstrap")
        check.update(status="completed", conclusion="failure", check_suite={"id": 999999})
        self.assertIn("remain pending", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_changed_event_identity_cannot_cancel(self):
        self.api.event["workflow_run"]["workflow_id"] = 999
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_other_required_workflow_completion_catches_failure(self):
        self.api.event["action"] = "completed"
        self.api.event["workflow_run"] = copy.deepcopy(self.api.runs[2])
        self.api.checks[2].update(status="completed", conclusion="failure")
        self.api.runs[2].update(status="completed", conclusion="failure")
        self.assertIn(self.api.names[2], self.watch())
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs
                                              if run["status"] != "completed"])

    def test_old_check_from_new_workflow_attempt_does_not_cancel(self):
        check = self.api.checks[2]
        check.update(status="completed", conclusion="failure")
        self.api.runs[2]["run_attempt"] = 2
        self.assertIn("remain pending", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_reconciled_check_requires_exact_head_marker(self):
        check = next(row for row in self.api.checks if row["name"] == "Main integration admission")
        check.update(check_suite={"id": 999999}, status="completed", conclusion="failure",
                     external_id="buster-merge-queue-admission-v1:" + "b" * 40)
        self.assertIn("remain pending", self.watch())
        check["external_id"] = "buster-merge-queue-admission-v1:" + "a" * 40
        self.assertIn("Main integration admission", self.watch())
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs])

    def test_sweep_catches_early_buster_failure_and_is_idempotent(self):
        self.api.jobs[0].update(status="completed", conclusion="failure")
        event = {"repository": self.api.event["repository"]}
        self.assertIn("Linux x86-64 release", recovery.watch(self.api, event, "schedule"))
        self.api.runs = [dict(run, status="completed") for run in self.api.runs]
        self.api.cancelled.clear()
        recovery.watch(self.api, event, "schedule")
        self.assertEqual(self.api.cancelled, [])

    def test_replaced_or_missing_ref_never_cancels(self):
        self.api.jobs[0].update(status="completed", conclusion="failure")
        self.api.refs[0]["object"]["sha"] = "b" * 40
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_new_attempt_during_final_read_never_cancels(self):
        self.api.jobs[0].update(status="completed", conclusion="failure")
        original = self.api.all
        count = 0

        def changing_runs(path, key=None, **query):
            nonlocal count
            if path == "actions/runs":
                count += 1
                if count == 2:
                    self.api.runs[0]["run_attempt"] = 2
            return original(path, key, **query)

        self.api.all = changing_runs
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_watcher_uses_trusted_checkout_and_job_permissions(self):
        workflow = (ROOT / ".github/workflows/ci-merge-group-watch.yml").read_text()
        watch = workflow.split("\n  watch-merge-group:", 1)[1]
        self.assertIn("github.event.workflow_run.event == 'merge_group'", watch)
        self.assertIn("startsWith(github.event.workflow_run.path, '.github/workflows/ci.yml@')", watch)
        self.assertIn("actions: write", watch)
        self.assertIn("checks: read", watch)
        self.assertIn("ref: ${{ github.sha }}", watch)
        self.assertIn("timeout-minutes: 5", watch)
        self.assertNotIn("sleep", watch)
        self.assertNotIn("github.event.workflow_run.head_sha", watch)
        self.assertIn("- cron: '13-59/15 * * * *'", workflow)


if __name__ == "__main__":
    unittest.main()
