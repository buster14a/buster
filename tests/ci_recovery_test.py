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


if __name__ == "__main__":
    unittest.main()
