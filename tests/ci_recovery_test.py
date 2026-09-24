#!/usr/bin/env python3
"""Offline regression scenarios for the privileged CI recovery decision."""

import copy
import importlib.util
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "recovery", ROOT / ".github/scripts/recover-ci.py")
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)


class FakeGitHub:
    def __init__(self):
        self.run = {
            "id": 123, "workflow_id": 456, "head_sha": "a" * 40,
            "head_branch": "fix/example", "path": ".github/workflows/ci.yml",
            "head_repository": {"full_name": "buster14a/buster"},
            "event": "push", "status": "completed", "conclusion": "cancelled",
            "run_attempt": 1,
        }
        self.event = {
            "repository": {"full_name": "buster14a/buster", "default_branch": "main"},
            "workflow_run": copy.deepcopy(self.run),
        }
        self.pr = {
            "number": 252, "state": "open", "labels": [],
            "head": {"repo": {"full_name": "buster14a/buster"},
                     "sha": self.run["head_sha"], "ref": self.run["head_branch"]},
        }
        self.prs = [self.pr]
        self.jobs = [
            {"name": "Linux", "status": "completed", "conclusion": "cancelled"},
            {"name": "Windows", "status": "completed", "conclusion": "success"},
        ]
        self.runs = [copy.deepcopy(self.run)]
        self.final_run = self.run
        self.final_pr = self.pr
        self.reads = 0
        self.posts = []

    def request(self, path, *, method="GET", **query):
        if method == "POST":
            self.posts.append(path)
            result = None
        elif path == "pulls/252":
            result = self.final_pr
        elif path == "actions/runs/123":
            self.reads += 1
            result = self.run if self.reads == 1 else self.final_run
        else:
            raise AssertionError((method, path, query))
        return copy.deepcopy(result)

    def all(self, path, key=None, **query):
        if path == "pulls":
            assert query == {"state": "open", "head": "buster14a:fix/example"}
            result = self.prs
        elif path == "actions/runs/123/jobs":
            assert query == {"filter": "latest"}
            result = self.jobs
        elif path == "actions/workflows/456/runs":
            assert query == {"branch": "fix/example"}
            result = self.runs
        else:
            raise AssertionError((path, key, query))
        return copy.deepcopy(result)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeGitHub()

    def recover(self):
        return recovery.recover(self.api, self.api.event)

    def assert_skipped(self):
        with self.assertRaises(recovery.SkipRecovery):
            self.recover()
        self.assertEqual(self.api.posts, [])

    def test_current_push_retries_only_unfinished_jobs(self):
        self.assertIn("1 cancelled jobs", self.recover())
        self.assertEqual(self.api.posts, ["actions/runs/123/rerun-failed-jobs"])

    def test_current_pr_event(self):
        self.api.run["event"] = "pull_request"
        self.recover()
        self.assertEqual(len(self.api.posts), 1)

    def test_failed_aggregate_after_cancellation_is_retried(self):
        self.api.run["conclusion"] = "failure"
        self.api.jobs.append({"name": "CI complete", "status": "completed", "conclusion": "failure"})
        self.recover()
        self.assertEqual(len(self.api.posts), 1)

    def test_platform_failures_and_timeouts_stay_visible(self):
        for conclusion in ("failure", "timed_out", "action_required", "neutral", None):
            with self.subTest(conclusion=conclusion):
                self.api = FakeGitHub()
                self.api.jobs[1]["conclusion"] = conclusion
                self.assert_skipped()

    def test_no_cancelled_jobs(self):
        self.api.jobs[0]["conclusion"] = "success"
        self.assert_skipped()

    def test_incomplete_jobs(self):
        self.api.jobs[0]["status"] = "in_progress"
        self.assert_skipped()

    def test_run_filters(self):
        for key, value in (
                ("event", "workflow_dispatch"), ("event", "merge_group"),
                ("head_branch", "main"), ("path", ".github/workflows/other.yml"),
                ("status", "queued"), ("conclusion", "success"),
                ("run_attempt", 2), ("head_sha", "b" * 40), ("workflow_id", 789),
                ("head_repository", {"full_name": "external/buster"})):
            with self.subTest(key=key, value=value):
                self.api = FakeGitHub()
                self.api.run[key] = value
                self.assert_skipped()

    def test_exhausted_event(self):
        self.api.run["run_attempt"] = 2
        self.api.event["workflow_run"]["run_attempt"] = 2
        self.assert_skipped()

    def test_missing_or_ambiguous_pr(self):
        for prs in ([], [self.api.pr, self.api.pr]):
            self.api.prs = prs
            self.assert_skipped()

    def test_closed_pr(self):
        self.api.pr["state"] = "closed"
        self.assert_skipped()

    def test_superseded_commit(self):
        self.api.pr["head"]["sha"] = "b" * 40
        self.assert_skipped()

    def test_opt_out(self):
        self.api.pr["labels"] = [{"name": "ci-no-retry"}]
        self.assert_skipped()

    def test_newer_runs_including_success_and_queued(self):
        for status in ("queued", "pending", "in_progress", "completed"):
            self.api = FakeGitHub()
            self.api.runs.append(dict(self.api.run, id=124, status=status))
            self.assert_skipped()

    def test_older_active_same_commit(self):
        self.api.runs.append(dict(self.api.run, id=122, status="queued"))
        self.assert_skipped()

    def test_historical_completed_run_does_not_block(self):
        self.api.runs.append(dict(self.api.run, id=122))
        self.recover()
        self.assertEqual(len(self.api.posts), 1)

    def test_push_while_reading_jobs(self):
        self.api.final_pr = copy.deepcopy(self.api.pr)
        self.api.final_pr["head"]["sha"] = "b" * 40
        self.assert_skipped()

    def test_manual_retry_while_reading_jobs(self):
        self.api.final_run = dict(self.api.run, run_attempt=2)
        self.assert_skipped()

    def test_api_error_never_posts(self):
        with mock.patch.object(self.api, "all", side_effect=OSError("API unavailable")):
            with self.assertRaises(OSError):
                self.recover()
        self.assertEqual(self.api.posts, [])

    def test_pagination_includes_later_jobs(self):
        api = recovery.GitHub("buster14a/buster", "unused")
        with mock.patch.object(api, "request", side_effect=[
                {"jobs": ["success"] * 100}, {"jobs": ["failure"]}]) as request:
            self.assertEqual(api.all("jobs", "jobs")[-1], "failure")
            self.assertEqual(request.call_count, 2)

    def test_pagination_limit_fails_closed(self):
        api = recovery.GitHub("buster14a/buster", "unused")
        with mock.patch.object(api, "request", return_value=[0] * 100):
            with self.assertRaises(recovery.SkipRecovery):
                api.all("pulls")


class FakeWatchGitHub:
    def __init__(self):
        self.run = {
            "id": 123, "workflow_id": 456, "head_sha": "a" * 40,
            "head_branch": "gh-readonly-queue/main/pr-1-abc",
            "path": ".github/workflows/ci.yml",
            "head_repository": {"full_name": "buster14a/buster"},
            "event": "merge_group", "status": "in_progress", "conclusion": None,
            "run_attempt": 1,
        }
        self.event = {
            "action": "in_progress",
            "repository": {"full_name": "buster14a/buster", "default_branch": "main"},
            "workflow_run": copy.deepcopy(self.run),
        }
        self.jobs = [
            {"name": "Linux x86-64 release", "status": "completed", "conclusion": "failure"},
            {"name": "Linux AArch64 release", "status": "in_progress", "conclusion": None},
        ]
        self.runs = [
            copy.deepcopy(self.run),
            dict(self.run, id=124, workflow_id=457, status="queued"),
            dict(self.run, id=125, workflow_id=458, status="completed", conclusion="success"),
        ]
        self.cancelled = []

    def request(self, path, *, method="GET", **query):
        if method != "GET" or path != "actions/runs/123" or query:
            raise AssertionError((method, path, query))
        return copy.deepcopy(self.run)

    def all(self, path, key=None, **query):
        if path == "actions/runs/123/jobs":
            self.assert_query(query, {"filter": "latest"})
            return copy.deepcopy(self.jobs)
        if path == "actions/runs":
            self.assert_query(query, {"event": "merge_group", "head_sha": "a" * 40})
            return copy.deepcopy(self.runs)
        raise AssertionError((path, key, query))

    def cancel(self, run_id):
        self.cancelled.append(run_id)
        return True

    @staticmethod
    def assert_query(actual, expected):
        if actual != expected:
            raise AssertionError((actual, expected))


class MergeQueueFailFastTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeWatchGitHub()

    def watch(self, max_probes=1):
        return recovery.watch(self.api, self.api.event, sleep_fn=lambda _seconds: None,
                              max_probes=max_probes)

    def test_first_failed_job_cancels_all_active_exact_head_runs(self):
        message = self.watch()
        self.assertEqual(self.api.cancelled, [123, 124])
        self.assertIn("Linux x86-64 release", message)
        self.assertIn("2 exact-head run(s)", message)

    def test_cancelled_job_is_also_terminal_failure(self):
        self.api.jobs[0]["conclusion"] = "cancelled"
        self.watch()
        self.assertEqual(self.api.cancelled, [123, 124])

    def test_successful_completed_run_does_not_cancel(self):
        self.api.run["status"] = "completed"
        self.api.run["conclusion"] = "success"
        for job in self.api.jobs:
            job["status"] = "completed"
            job["conclusion"] = "success"
        self.assertIn("completed successfully", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_active_run_without_failure_stays_bounded(self):
        for job in self.api.jobs:
            job["status"] = "in_progress"
            job["conclusion"] = None
        with self.assertRaises(TimeoutError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_watcher_rejects_non_merge_group_runs(self):
        self.api.event["workflow_run"]["event"] = "pull_request"
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_source_identity_change_fails_closed(self):
        self.api.run["head_sha"] = "b" * 40
        with self.assertRaises(recovery.SkipRecovery):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_exact_head_query_cannot_return_another_source(self):
        self.api.runs[1]["head_sha"] = "b" * 40
        with self.assertRaises(ValueError):
            self.watch()

    def test_completed_job_without_conclusion_is_malformed(self):
        self.api.jobs[0]["conclusion"] = None
        with self.assertRaises(ValueError):
            self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_workflow_runs_watcher_from_trusted_default_branch(self):
        workflow = (ROOT / ".github/workflows/ci-recovery.yml").read_text()
        self.assertIn("types: [in_progress, completed]", workflow)
        watch = workflow.split("\n  watch-merge-group:", 1)[1].split("\n  recover:", 1)[0]
        self.assertIn("github.event.action == 'in_progress'", watch)
        self.assertIn("github.event.workflow_run.event == 'merge_group'", watch)
        self.assertIn("actions: write", watch)
        self.assertIn("ref: ${{ github.sha }}", watch)
        self.assertIn("recover-ci.py watch", watch)
        self.assertNotIn("github.event.workflow_run.head_sha", watch)


if __name__ == "__main__":
    unittest.main()
