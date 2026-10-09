#!/usr/bin/env python3
"""Workflow selection and attribution checks kept outside the frozen corpus."""

import importlib.util
import io
import json
import socket
from pathlib import Path
import re
import unittest
from unittest import mock
import urllib.error


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "recovery", ROOT / ".github/scripts/recover-ci.py")
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)


class LifecycleWorkflowTests(unittest.TestCase):
    def test_filtered_handlers_and_source_change_tests(self):
        workflows = ROOT / ".github/workflows"
        recover = (workflows / "ci-recovery.yml").read_text()
        watch = (workflows / "ci-merge-group-watch.yml").read_text()
        regressions = (workflows / "ci-recovery-tests.yml").read_text()

        # Each upstream delivery creates at most one handler check. Ordinary
        # PR starts and main completions do not create handler workflows.
        self.assertIn("types: [completed]", recover)
        self.assertIn("branches-ignore:\n      - main\n      - 'gh-readonly-queue/main/**'", recover)
        self.assertEqual(re.findall(r"^  ([\w-]+):$", recover.split("\njobs:\n", 1)[1], re.M),
                         ["recover"])
        self.assertIn("types: [in_progress, completed]", watch)
        self.assertIn("branches:\n      - 'gh-readonly-queue/main/**'", watch)
        self.assertIn("- cron: '13-59/15 * * * *'", watch)
        self.assertIn("workflow_dispatch:", watch)
        self.assertNotIn("Self-host fixed point", watch)
        for name in ("Buster CI", "TCC bootstrap",
                     "GPU toolchain acceptance", "Benchmark service workflow policy",
                     "API migration policy", "Main integration admission"):
            self.assertIn("      - " + name + "\n", watch)
        self.assertEqual(re.findall(r"^  ([\w-]+):$", watch.split("\njobs:\n", 1)[1], re.M),
                         ["watch-merge-group"])
        self.assertIn("group: ci-recovery-${{ github.event.workflow_run.id }}", recover)
        self.assertIn("group: ci-recovery-${{ github.event.workflow_run.id || github.run_id }}", watch)
        for workflow in (recover, watch):
            self.assertIn("ref: ${{ github.sha }}", workflow)
            self.assertIn("github.event.workflow_run.head_repository.full_name == github.repository", workflow)
            self.assertIn("run-name:", workflow)
            self.assertNotIn("\n  test:\n", workflow)
        self.assertNotIn("workflow_run:", regressions)
        self.assertIn("persist-credentials: false", regressions)
        # ci.yml owns the step budgets that the watcher's deadline mirrors.
        for path in ("ci-recovery.yml", "ci-merge-group-watch.yml", "ci-recovery-tests.yml",
                     "ci.yml"):
            self.assertEqual(regressions.count("'.github/workflows/" + path + "'"), 2)
        for path in (".github/scripts/recover-ci.py", ".github/scripts/test_merge_queue_fail_fast.py",
                     "tests/ci_recovery_test.py", ".github/scripts/test_ci_recovery_workflows.py"):
            self.assertEqual(regressions.count("'" + path + "'"), 2)

    def test_summary_distinguishes_upstream_and_trusted_handler(self):
        event = {
            "action": "completed",
            "workflow_run": {
                "id": 123, "run_attempt": 1, "event": "pull_request",
                "head_branch": "fix/<retained>", "head_sha": "a" * 40,
            },
        }
        summary = recovery.lifecycle_summary(
            "Buster CI lifecycle no action", "No action: failed job <test>", event,
            "buster14a/buster", "b" * 40, 234, 2)
        self.assertIn("/actions/runs/123/attempts/1", summary)
        self.assertIn("/actions/runs/234/attempts/2", summary)
        self.assertIn("<code>fix/&lt;retained&gt;</code>", summary)
        self.assertIn("<code>" + "a" * 40 + "</code>", summary)
        self.assertIn("<code>" + "b" * 40 + "</code>", summary)
        self.assertIn("No action: failed job &lt;test&gt;", summary)
        self.assertNotIn("Controller records", summary)

    def test_summary_retains_controller_records(self):
        event = {
            "action": "in_progress",
            "workflow_run": {
                "id": 123, "run_attempt": 1, "event": "merge_group",
                "head_branch": "gh-readonly-queue/main/pr-1-abc", "head_sha": "a" * 40,
            },
        }
        records = ['STEP_DEADLINE_V1 action=cancel-requested job_name="macOS <x86-64> release"',
                   "STEP_DEADLINE_V1 action=step-stopped run=123"]
        summary = recovery.lifecycle_summary(
            "Buster CI merge-group watcher", "Decision", event, "buster14a/buster",
            "b" * 40, 234, 1, records)
        self.assertEqual(summary.count("Controller records"), 1)
        self.assertIn("- <code>STEP_DEADLINE_V1 action=cancel-requested job_name=&quot;macOS "
                      "&lt;x86-64&gt; release&quot;</code>\n", summary)
        self.assertTrue(summary.endswith("- <code>STEP_DEADLINE_V1 action=step-stopped run=123</code>\n"))

    def test_sweep_summary_identifies_trusted_handler(self):
        summary = recovery.lifecycle_summary(
            "Merge-group CI fail-fast", "No live merge groups.",
            {"schedule": "13-59/15 * * * *"}, "buster14a/buster", "b" * 40, 234, 1)
        self.assertIn("Trigger: <code>13-59/15 * * * *</code>", summary)
        self.assertIn("/actions/runs/234/attempts/1", summary)
        self.assertIn("No live merge groups.", summary)



class TransportTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.sleeps = []
        self.api = recovery.GitHub("buster14a/buster", "secret-token",
                                   clock=lambda: self.now, sleep_fn=self.sleep)

    def sleep(self, delay):
        self.sleeps.append(delay)
        self.now += delay

    def response(self, value):
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = json.dumps(value).encode()
        return response

    def http(self, code):
        return urllib.error.HTTPError("https://secret-token.invalid/?secret", code,
                                      "secret-token", {}, io.BytesIO())

    def test_get_transient_http_and_transport_recover(self):
        failures = [self.http(code) for code in (500, 502, 503, 504)]
        failures += [TimeoutError("secret-token"), ConnectionResetError("secret-token"),
                     urllib.error.URLError(socket.gaierror(socket.EAI_AGAIN, "secret-token"))]
        for error in failures:
            with self.subTest(error=type(error).__name__):
                self.setUp()
                with mock.patch.object(recovery.urllib.request, "urlopen",
                                       side_effect=[error, self.response({"jobs": []})]) as request:
                    self.assertEqual(self.api.request("actions/runs/123/jobs"), {"jobs": []})
                self.assertEqual(request.call_count, 2)
                self.assertEqual(self.sleeps, [1])
                self.assertEqual(self.api.read_epoch, 1)
                self.assertEqual(request.call_args.kwargs["timeout"], recovery.API_REQUEST_SECONDS)

    def test_exhaustion_is_sanitized_non_success(self):
        with mock.patch.object(recovery.urllib.request, "urlopen",
                               side_effect=[self.http(500) for _ in range(3)]) as request:
            with self.assertRaises(recovery.APIUnavailable) as error:
                self.api.request("actions/runs/123/jobs", secret="secret-token")
        self.assertEqual(request.call_count, 3)
        self.assertEqual(self.sleeps, [1, 2])
        text = str(error.exception)
        for field in ("method=GET", "endpoint=actions/runs/123/jobs", "attempts=3",
                      "elapsed_seconds=3", "status=500"):
            self.assertIn(field, text)
        self.assertNotIn("secret", text)
        self.assertIsNone(error.exception.__cause__)

    def test_permanent_errors_and_rate_limits_are_single_attempt(self):
        # Rate-limit recovery is deliberately deferred to a later event/sweep.
        for code in (400, 401, 403, 404, 409, 422, 429, 501):
            with self.subTest(code=code):
                self.setUp()
                with mock.patch.object(recovery.urllib.request, "urlopen",
                                       side_effect=self.http(code)) as request:
                    with self.assertRaises(recovery.APIUnavailable):
                        self.api.request("actions/runs")
                self.assertEqual(request.call_count, 1)
                self.assertEqual(self.sleeps, [])
        with mock.patch.object(recovery.urllib.request, "urlopen",
                               side_effect=urllib.error.URLError("permanent TLS error")) as request:
            with self.assertRaises(recovery.APIUnavailable):
                self.api.request("actions/runs")
        self.assertEqual(request.call_count, 1)

    def test_invalid_json_and_inventory_are_not_retried(self):
        response = self.response(None)
        response.__enter__.return_value.read.return_value = b"{"
        with mock.patch.object(recovery.urllib.request, "urlopen", return_value=response) as request:
            with self.assertRaises(json.JSONDecodeError):
                self.api.all("actions/runs", "workflow_runs")
        self.assertEqual(request.call_count, 1)
        for value in ({"workflow_runs": {}}, {"workflow_runs": [0] * 101}):
            with mock.patch.object(recovery.urllib.request, "urlopen",
                                   return_value=self.response(value)) as request:
                with self.assertRaises(ValueError):
                    self.api.all("actions/runs", "workflow_runs")
            self.assertEqual(request.call_count, 1)

    def test_later_page_failure_does_not_return_truncated_inventory(self):
        with mock.patch.object(recovery.urllib.request, "urlopen", side_effect=[
                self.response({"jobs": [0] * 100}), self.http(500),
                self.http(500), self.http(500)]) as request:
            with self.assertRaises(recovery.APIUnavailable):
                self.api.all("actions/runs/123/jobs", "jobs")
        self.assertEqual(request.call_count, 4)
        self.assertIn("page=2", request.call_args.args[0].full_url)

    def test_deadline_shared_across_pages_and_grace(self):
        def first_page(request, timeout):
            self.assertEqual(timeout, recovery.API_REQUEST_SECONDS)
            self.now = recovery.API_PASS_SECONDS - 0.5
            return self.response({"jobs": [0] * 100})
        with mock.patch.object(recovery.urllib.request, "urlopen",
                               side_effect=[first_page]) as request:
            # Use a callable so the first page consumes the pass's budget.
            request.side_effect = first_page
            self.assertEqual(len(self.api.request("jobs")["jobs"]), 100)
        with mock.patch.object(recovery.urllib.request, "urlopen",
                               side_effect=self.http(500)) as request:
            with self.assertRaises(recovery.APIUnavailable):
                self.api.all("actions/runs/123/jobs", "jobs")
        self.assertEqual(request.call_count, 1)
        self.assertEqual(request.call_args.kwargs["timeout"], 0.5)
        self.assertEqual(self.sleeps, [])
        with self.assertRaises(recovery.APIUnavailable):
            self.api.reserve_sleep(recovery.STEP_DEADLINE_POLL_SECONDS)
        self.now = recovery.API_PASS_SECONDS
        with mock.patch.object(recovery.urllib.request, "urlopen") as request:
            with self.assertRaises(recovery.APIUnavailable):
                self.api.request("actions/runs/123/cancel", method="POST")
        request.assert_not_called()

    def test_success_after_deadline_is_not_accepted(self):
        def late_response(request, timeout):
            self.now = recovery.API_PASS_SECONDS
            return self.response({"jobs": []})
        with mock.patch.object(recovery.urllib.request, "urlopen", side_effect=late_response):
            with self.assertRaises(recovery.APIUnavailable):
                self.api.request("jobs")

    def test_all_mutation_endpoints_remain_single_attempt(self):
        for endpoint in ("cancel", "force-cancel", "rerun-failed-jobs"):
            for error in (self.http(500), TimeoutError("uncertain write")):
                with self.subTest(endpoint=endpoint, error=type(error).__name__):
                    self.setUp()
                    with mock.patch.object(recovery.urllib.request, "urlopen",
                                           side_effect=error) as request:
                        with self.assertRaises(recovery.APIUnavailable):
                            self.api.request("actions/runs/123/" + endpoint, method="POST")
                    self.assertEqual(request.call_count, 1)
                    self.assertEqual(self.sleeps, [])

    def test_cancel_conflict_is_not_retried(self):
        with mock.patch.object(recovery.urllib.request, "urlopen",
                               side_effect=self.http(409)) as request:
            self.assertFalse(self.api.cancel(123))
        self.assertEqual(request.call_count, 1)
        self.assertEqual(self.sleeps, [])

    @unittest.skipUnless(hasattr(recovery.signal, "setitimer"), "POSIX controller deadline")
    def test_open_and_read_alarm_is_armed_and_restored(self):
        handler = recovery.signal.getsignal(recovery.signal.SIGALRM)
        with mock.patch.object(recovery.signal, "setitimer", wraps=recovery.signal.setitimer) as timer:
            with mock.patch.object(recovery.urllib.request, "urlopen",
                                   return_value=self.response({"jobs": []})):
                self.api.request("jobs")
        self.assertEqual(timer.call_args_list, [
            mock.call(recovery.signal.ITIMER_REAL, recovery.API_REQUEST_SECONDS),
            mock.call(recovery.signal.ITIMER_REAL, 0)])
        self.assertEqual(recovery.signal.getsignal(recovery.signal.SIGALRM), handler)

    @unittest.skipUnless(hasattr(recovery.signal, "setitimer"), "POSIX controller deadline")
    def test_alarm_timeout_during_body_read_is_bounded(self):
        response = self.response(None)
        response.__enter__.return_value.read.side_effect = TimeoutError("API request deadline")
        with mock.patch.object(recovery.urllib.request, "urlopen", return_value=response) as request:
            with self.assertRaises(recovery.APIUnavailable):
                self.api.request("jobs")
        self.assertEqual(request.call_count, 3)
        self.assertEqual(self.sleeps, [1, 2])
        self.assertEqual(recovery.signal.getitimer(recovery.signal.ITIMER_REAL), (0.0, 0.0))

    def test_error_summary_retains_handler_and_upstream_identities(self):
        summary = recovery.lifecycle_summary(
            "CI lifecycle controller error", str(self.api.unavailable("jobs", "GET", 3, "500")),
            {"action": "completed", "workflow_run": {
                "id": 123, "run_attempt": 1, "event": "merge_group",
                "head_branch": "gh-readonly-queue/main/pr-1", "head_sha": "a" * 40}},
            "buster14a/buster", "b" * 40, 234, 2)
        self.assertIn("CI_API_V1", summary)
        self.assertIn("/actions/runs/123/attempts/1", summary)
        self.assertIn("/actions/runs/234/attempts/2", summary)
        self.assertIn("b" * 40, summary)


if __name__ == "__main__":
    unittest.main()
