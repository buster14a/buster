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
        workflow = (ROOT / ".github/workflows/ci-merge-group-watch.yml").read_text()
        self.assertIn("github.event.workflow_run.event == 'merge_group'", workflow)
        self.assertIn("actions: write", workflow)
        self.assertIn("checks: read", workflow)
        self.assertIn("ref: ${{ github.sha }}", workflow)
        self.assertNotIn("ref: ${{ github.event.workflow_run.head_sha }}", workflow)


class WatchdogGitHub(FakeGitHub):
    def __init__(self):
        super().__init__()
        self.jobs = [{
            "id": 109415142502, "run_id": 123, "head_sha": "a" * 40,
            "name": "macOS x86-64 release", "status": "in_progress", "conclusion": None,
            "started_at": "2026-09-29T12:53:13Z", "completed_at": None,
            "steps": [{"number": 4, "name": recovery.WORKFLOW_TOOLS_STEP,
                       "status": "in_progress", "conclusion": None,
                       "started_at": "2026-09-29T12:53:23Z", "completed_at": None}],
        }]
        self.now = recovery.utc_timestamp("2026-09-29T13:30:00Z")
        self.forced = []
        self.sleeps = []
        self.attempt_reads = 0
        self.before_attempt_read = lambda: None
        self.before_run_read = lambda: None
        self.after_sleep = lambda: None
        self.accept_cancel = True

    def all(self, path, key=None, **query):
        if path == "actions/runs/123/attempts/1/jobs":
            assert key == "jobs" and query == {}
            self.attempt_reads += 1
            self.before_attempt_read()
            result = copy.deepcopy(self.jobs)
        else:
            result = super().all(path, key, **query)
        return result

    def request(self, path, *, method="GET", **query):
        if path == "actions/runs/123":
            assert method == "GET" and query == {}
            self.before_run_read()
            result = copy.deepcopy(self.runs[0])
        else:
            result = super().request(path, method=method, **query)
        return result

    def cancel(self, run_id, *, force=False):
        (self.forced if force else self.cancelled).append(run_id)
        return self.accept_cancel

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds
        self.after_sleep()


class StepWatchdogTests(unittest.TestCase):
    def setUp(self):
        self.api = WatchdogGitHub()

    def watch(self):
        return recovery.watch(self.api, self.api.event, sleep_fn=self.api.sleep,
                              now_fn=lambda: self.api.now, max_probes=1)

    def select(self):
        return recovery.stalled_tool_steps(self.api.runs[0], self.api.jobs, self.api.now)

    def test_incident_requests_normal_then_force_for_only_stuck_buster_run(self):
        message = self.watch()
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.forced, [123])
        self.assertEqual(self.api.sleeps, [recovery.STEP_CANCEL_GRACE_SECONDS])
        self.assertEqual(self.api.attempt_reads, 2)
        for value in ('109415142502', 'macOS x86-64 release', '"attempt": 1',
                      '"budget_seconds": 300', '"deadline_seconds": 420',
                      '2026-09-29T12:53:23Z', 'a' * 40, 'terminal result not yet verified'):
            self.assertIn(value, message)

    def test_all_six_lane_deadline_boundaries(self):
        started = recovery.utc_timestamp(self.api.jobs[0]["steps"][0]["started_at"])
        self.assertEqual(len(recovery.WORKFLOW_TOOLS_TIMEOUT_SECONDS), 6)
        for name, budget in recovery.WORKFLOW_TOOLS_TIMEOUT_SECONDS.items():
            with self.subTest(lane=name):
                self.api.jobs[0]["name"] = name
                self.api.now = started + budget + recovery.STEP_REPORTING_GRACE_SECONDS - 0.01
                self.assertEqual(self.select(), [])
                self.api.now += 0.01
                self.assertEqual(len(self.select()), 1)

    def test_queued_job_is_not_stalled(self):
        self.api.jobs[0].update(status="queued", started_at=None)
        self.assertEqual(self.select(), [])

    def test_completed_job_is_not_stalled(self):
        self.api.jobs[0].update(status="completed", conclusion="success")
        self.assertEqual(self.select(), [])

    def test_completed_run_does_not_reuse_stale_step_metadata(self):
        self.api.runs[0].update(status="completed", conclusion="success")
        self.assertEqual(self.select(), [])

    def test_pending_or_completed_step_is_not_stalled(self):
        for status in ("pending", "completed"):
            with self.subTest(status=status):
                self.api.jobs[0]["steps"][0]["status"] = status
                self.assertEqual(self.select(), [])

    def test_unrelated_jobs_and_long_build_steps_are_not_budgeted(self):
        for name in ("Clang analyzer shards", "macOS x86-64 checks", "unknown release"):
            self.api.jobs[0]["name"] = name
            self.assertEqual(self.select(), [])
        self.api.jobs[0]["name"] = "macOS x86-64 release"
        self.api.jobs[0]["steps"][0]["name"] = "Combination matrix (Linux, macOS)"
        self.assertEqual(self.select(), [])

    def test_malformed_missing_and_naive_timestamps_fail_closed(self):
        for value in (None, "", "not-a-dateZ", "2026-09-29T12:53:23", 123):
            with self.subTest(timestamp=value):
                self.api.jobs[0]["steps"][0]["started_at"] = value
                with self.assertRaises(ValueError):
                    self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_future_step_does_not_trigger_deadline(self):
        self.api.now = recovery.utc_timestamp("2026-09-29T12:53:22Z")
        self.assertEqual(self.select(), [])

    def test_step_cannot_predate_job(self):
        self.api.jobs[0]["started_at"] = "2026-09-29T12:53:24Z"
        with self.assertRaises(ValueError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_invalid_job_identity_or_terminal_metadata_never_cancels(self):
        original = copy.deepcopy(self.api.jobs[0])
        for change in ({"id": None}, {"id": True}, {"run_id": 999}, {"head_sha": "b" * 40},
                       {"conclusion": "success"}, {"completed_at": "2026-09-29T12:54:00Z"}):
            with self.subTest(change=change):
                self.api.jobs[0] = {**original, **change}
                with self.assertRaises(ValueError):
                    self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_invalid_step_identity_or_terminal_metadata_never_cancels(self):
        original = copy.deepcopy(self.api.jobs[0]["steps"][0])
        for change in ({"number": 0}, {"number": True}, {"conclusion": "success"},
                       {"completed_at": "2026-09-29T12:54:00Z"}):
            with self.subTest(change=change):
                self.api.jobs[0]["steps"][0] = {**original, **change}
                with self.assertRaises(ValueError):
                    self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_duplicate_metadata_is_not_cancellation_authority(self):
        self.api.jobs.append(copy.deepcopy(self.api.jobs[0]))
        with self.assertRaises(ValueError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_step_progress_between_discovery_and_confirmation_prevents_cancel(self):
        def progress():
            self.api.jobs[0]["steps"][0].update(status="completed", conclusion="success")
        self.api.before_attempt_read = progress
        with self.assertRaises(TimeoutError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])
        self.assertEqual(self.api.forced, [])

    def test_replaced_job_step_or_start_does_not_reuse_discovery_proof(self):
        def replace_job():
            self.api.jobs[0]["id"] += 1
        def replace_step():
            self.api.jobs[0]["steps"][0]["number"] += 1
        def replace_start():
            self.api.jobs[0]["steps"][0]["started_at"] = "2026-09-29T12:53:24Z"
        for change in (replace_job, replace_step, replace_start):
            with self.subTest(change=change.__name__):
                self.api = WatchdogGitHub()
                self.api.before_attempt_read = change
                with self.assertRaises(TimeoutError):
                    self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_last_run_read_changed_attempt_prevents_cancel(self):
        self.api.before_run_read = lambda: self.api.runs[0].update(run_attempt=2)
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_last_run_read_completion_prevents_cancel(self):
        self.api.before_run_read = lambda: self.api.runs[0].update(status="completed", conclusion="success")
        with self.assertRaises(TimeoutError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_normal_cancellation_completion_prevents_force(self):
        self.api.after_sleep = lambda: self.api.runs[0].update(status="completed", conclusion="cancelled")
        self.assertIn("no force cancellation", self.watch())
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.forced, [])

    def test_step_progress_after_normal_cancel_prevents_force(self):
        self.api.after_sleep = lambda: self.api.jobs[0]["steps"][0].update(status="completed", conclusion="success")
        self.assertIn("no force cancellation", self.watch())
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.forced, [])

    def test_new_attempt_after_normal_cancel_is_not_force_cancelled(self):
        self.api.after_sleep = lambda: self.api.runs[0].update(run_attempt=2)
        self.assertIn("identity changed", self.watch())
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.forced, [])

    def test_new_run_after_normal_cancel_is_not_force_cancelled(self):
        self.api.after_sleep = lambda: self.api.runs[0].update(id=999)
        self.assertIn("identity changed", self.watch())
        self.assertEqual(self.api.forced, [])

    def test_new_head_after_normal_cancel_is_not_force_cancelled(self):
        self.api.after_sleep = lambda: self.api.runs[0].update(head_sha="b" * 40)
        with self.assertRaises(ValueError):
            self.watch()
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.forced, [])

    def test_normal_cancel_conflict_does_not_authorize_force(self):
        self.api.accept_cancel = False
        self.assertIn("conflicted", self.watch())
        self.assertEqual(self.api.forced, [])
        self.assertEqual(self.api.sleeps, [])

    def test_changed_event_head_and_non_queue_events_never_cancel(self):
        for field, value in (("head_sha", "b" * 40), ("run_attempt", 2),
                             ("event", "pull_request"), ("event", "push"),
                             ("event", "workflow_dispatch"), ("head_branch", "main")):
            with self.subTest(field=field, value=value):
                self.api = WatchdogGitHub()
                self.api.event["workflow_run"][field] = value
                with self.assertRaises((recovery.SkipRecovery, ValueError, AssertionError)):
                    self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_api_read_error_does_not_authorize_cancellation(self):
        def fail():
            raise recovery.urllib.error.HTTPError("test", 503, "unavailable", {}, None)
        self.api.before_attempt_read = fail
        with self.assertRaises(recovery.urllib.error.HTTPError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_force_api_error_propagates_without_retry(self):
        calls = []
        def cancel(run_id, *, force=False):
            calls.append((run_id, force))
            if force:
                raise recovery.urllib.error.HTTPError("test", 403, "forbidden", {}, None)
            return True
        self.api.cancel = cancel
        with self.assertRaises(recovery.urllib.error.HTTPError):
            self.watch()
        self.assertEqual(calls, [(123, False), (123, True)])

    def test_cancel_http_contract(self):
        api = recovery.GitHub("buster14a/buster", "test-token-not-used")
        calls = []
        def request(path, *, method="GET", **query):
            calls.append((path, method, query))
        api.request = request
        self.assertTrue(api.cancel(123))
        self.assertTrue(api.cancel(123, force=True))
        self.assertEqual(calls, [("actions/runs/123/cancel", "POST", {}),
                                 ("actions/runs/123/force-cancel", "POST", {})])
        for code in (409, 403, 429, 503):
            def fail(path, *, method="GET", **query):
                raise recovery.urllib.error.HTTPError("test", code, "error", {}, None)
            api.request = fail
            for force in (False, True):
                with self.subTest(code=code, force=force):
                    if code == 409:
                        self.assertFalse(api.cancel(123, force=force))
                    else:
                        with self.assertRaises(recovery.urllib.error.HTTPError):
                            api.cancel(123, force=force)


if __name__ == "__main__":
    unittest.main()
