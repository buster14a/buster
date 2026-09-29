#!/usr/bin/env python3
"""Offline checks for the trusted merge-queue cancellation controller."""

import copy
import importlib.util
import io
import json
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
        self.force_cancelled = []
        # Optional per-read responses, consumed in order before the live lists.
        self.run_reads = []
        self.job_reads = []
        self.fresh_runs = []

    def request(self, path, *, method="GET", **query):
        if (path, method, query) == ("actions/runs/123", "GET", {}):
            result = self.fresh_runs.pop(0) if self.fresh_runs else self.runs[0]
        elif (path, method, query) == ("rulesets/" + str(recovery.RULESET_ID), "GET", {}):
            result = self.ruleset
        else:
            raise AssertionError((path, method, query))
        return copy.deepcopy(result)

    def all(self, path, key=None, **query):
        if path == "actions/runs":
            assert key == "workflow_runs" and query == {
                "event": "merge_group", "head_sha": "a" * 40}
            result = self.run_reads.pop(0) if self.run_reads else self.runs
        elif path == "actions/runs/123/jobs":
            assert key == "jobs" and query == {"filter": "latest"}
            result = self.job_reads.pop(0) if self.job_reads else self.jobs
        elif path == "commits/" + "a" * 40 + "/check-runs":
            assert key == "check_runs" and query == {"filter": "all"}
            result = self.checks
        else:
            raise AssertionError((path, key, query))
        return copy.deepcopy(result)

    def cancel(self, run_id):
        self.cancelled.append(run_id)
        return True

    def force_cancel(self, run_id):
        self.force_cancelled.append(run_id)
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


# Run 36571009755's macOS x86-64 release step started here and was still in
# progress over 30 minutes later; its 5-minute budget plus grace ends 13:08:23.
INCIDENT_STEP_START = "2026-09-29T12:53:23Z"
INCIDENT_JOB_ID = 109415142502
T = recovery.github_time


def desktop_job(name, job_id, *, status="in_progress", step_status="in_progress",
                step_started=INCIDENT_STEP_START, job_started="2026-09-29T12:40:00Z"):
    step_conclusion = None if step_status != "completed" else "success"
    return {
        "id": job_id, "run_id": 123, "run_attempt": 1, "head_sha": "a" * 40,
        "name": name, "status": status,
        "conclusion": None if status != "completed" else "success",
        "started_at": job_started,
        "steps": [
            {"name": "Set up job", "number": 1, "status": "completed",
             "conclusion": "success", "started_at": job_started},
            {"name": "Checkout", "number": 2, "status": "completed",
             "conclusion": "success", "started_at": job_started},
            {"name": recovery.WORKFLOW_TOOLS_STEP, "number": 4, "status": step_status,
             "conclusion": step_conclusion, "started_at": step_started},
        ],
    }


class StepDeadlineTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeGitHub()
        self.stuck = desktop_job("macOS x86-64 release", INCIDENT_JOB_ID)
        self.api.jobs = [self.stuck] + [
            desktop_job(name, 200 + index, status="completed", step_status="completed")
            for index, name in enumerate(recovery.WORKFLOW_TOOLS_BUDGET_SECONDS)
            if name != "macOS x86-64 release"
        ]
        self.now = T("2026-09-29T13:23:23Z")
        self.log = []
        self.after_sleep = []

    def sleep(self, seconds):
        self.now += seconds
        if self.after_sleep:
            self.after_sleep.pop(0)()

    def watch(self, max_probes=1):
        return recovery.watch(self.api, self.api.event, sleep_fn=self.sleep,
                              max_probes=max_probes, clock=lambda: self.now, log=self.log)

    def watch_until_timeout(self, max_probes=1):
        with self.assertRaises(TimeoutError):
            self.watch(max_probes)

    def complete_cancelled(self, run_status="completed"):
        self.stuck.update(status="completed", conclusion="cancelled")
        self.stuck["steps"][-1].update(status="completed", conclusion="cancelled")
        self.api.runs[0].update(status=run_status,
                                conclusion="cancelled" if run_status == "completed" else None)

    def records(self, action):
        return [line for line in self.log if " action=" + action + " " in line]

    def test_incident_requests_normal_cancel_of_only_the_exact_run(self):
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.force_cancelled, [])
        [line] = self.records("cancel-requested")
        for field in ("run=123", "attempt=1", "head=" + "a" * 40, "job=" + str(INCIDENT_JOB_ID),
                      'job_name="macOS x86-64 release"', "step=4",
                      'step_name="Workflow tool regression tests"',
                      'started_at="2026-09-29T12:53:23Z"', "budget_seconds=300",
                      "grace_seconds=600", "deadline=2026-09-29T13:08:23Z",
                      "elapsed_seconds=1800"):
            self.assertIn(" " + field, line)

    def test_every_desktop_lane_deadline_boundary(self):
        run = self.api.runs[0]
        start = T(INCIDENT_STEP_START)
        for name, budget in recovery.WORKFLOW_TOOLS_BUDGET_SECONDS.items():
            with self.subTest(name=name):
                job = desktop_job(name, 7)
                limit = start + budget + recovery.STEP_DEADLINE_GRACE_SECONDS
                self.assertEqual(recovery.step_deadline_candidates(run, [job], limit), ([], []))
                overdue, refused = recovery.step_deadline_candidates(run, [job], limit + 1)
                self.assertEqual((len(overdue), refused), (1, []))
                self.assertEqual(overdue[0]["elapsed_seconds"],
                                 budget + recovery.STEP_DEADLINE_GRACE_SECONDS + 1)

    def test_watch_boundary_and_progress_across_probes(self):
        self.now = T("2026-09-29T13:08:23Z")
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [])
        self.now += 1
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [123])

    def test_linux_budget_is_not_the_macos_budget(self):
        self.stuck["name"] = "Linux x86-64 release"
        self.now = T("2026-09-29T13:05:24Z")
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [123])
        self.assertIn(" budget_seconds=120 ", self.records("cancel-requested")[0])

    def test_step_progressing_between_reads_is_refused(self):
        progressed = copy.deepcopy(self.api.jobs)
        progressed[0]["steps"][-1].update(status="completed", conclusion="success")
        self.api.job_reads = [copy.deepcopy(self.api.jobs), progressed]
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [])
        self.assertEqual(len(self.records("refused-progressed")), 1)

    def test_restarted_step_between_reads_is_refused(self):
        restarted = copy.deepcopy(self.api.jobs)
        restarted[0]["steps"][-1]["started_at"] = "2026-09-29T13:20:00Z"
        self.api.job_reads = [copy.deepcopy(self.api.jobs), restarted]
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [])

    def test_changed_attempt_head_or_replacement_before_mutation_is_refused(self):
        for change in ("attempt", "head", "replaced", "completed"):
            with self.subTest(change=change):
                self.setUp()
                fresh = copy.deepcopy(self.api.runs[0])
                if change == "attempt":
                    self.api.fresh_runs = [dict(fresh, run_attempt=2)]
                elif change == "head":
                    self.api.fresh_runs = [dict(fresh, head_sha="b" * 40)]
                elif change == "replaced":
                    newer = copy.deepcopy(self.api.runs)
                    newer.append(dict(fresh, id=999, check_suite_id=999))
                    self.api.run_reads = [copy.deepcopy(self.api.runs), newer]
                else:
                    self.api.fresh_runs = [dict(fresh, status="completed", conclusion="failure")]
                if change == "completed":
                    self.watch_until_timeout()
                else:
                    with self.assertRaises(recovery.SkipRecovery):
                        self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_stale_or_foreign_job_identity_is_refused_once(self):
        for key, value in (("run_attempt", 2), ("run_id", 124), ("head_sha", "b" * 40),
                           ("id", None), ("id", "109415142502")):
            with self.subTest(key=key, value=value):
                self.setUp()
                self.stuck[key] = value
                self.watch_until_timeout(max_probes=3)
                self.assertEqual(self.api.cancelled, [])
                [line] = self.records("refused")
                self.assertIn("identity does not match", line)

    def test_duplicate_or_concluded_step_is_refused(self):
        duplicate = copy.deepcopy(self.stuck["steps"][-1])
        self.stuck["steps"].append(dict(duplicate, number=5, status="queued"))
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [])
        self.setUp()
        self.stuck["steps"][-1]["conclusion"] = "failure"
        self.watch_until_timeout()
        self.assertEqual(self.api.cancelled, [])
        self.assertEqual(len(self.records("refused")), 1)

    def test_malformed_start_timestamps_are_refused(self):
        for key, value in (("step", None), ("step", ""), ("step", "not-a-time"),
                           ("step", "2026-09-29T12:53:23"), ("step", 1790000000),
                           ("step", "2026-02-30T12:53:23Z"), ("job", None),
                           ("job", "2026-09-29T13:00:00Z")):
            with self.subTest(key=key, value=value):
                self.setUp()
                if key == "step":
                    self.stuck["steps"][-1]["started_at"] = value
                else:
                    self.stuck["started_at"] = value
                self.watch_until_timeout()
                self.assertEqual(self.api.cancelled, [])
                [line] = self.records("refused")
                self.assertIn("malformed job or step start timestamp", line)

    def test_offsets_and_fractions_are_parsed(self):
        self.assertEqual(T("2026-09-29T05:53:23.000-07:00"), T(INCIDENT_STEP_START))
        self.assertEqual(T("2026-09-29T12:53:23+00:00"), T(INCIDENT_STEP_START))
        self.assertEqual(recovery.utc_text(T(INCIDENT_STEP_START)), INCIDENT_STEP_START)

    def test_queued_completed_and_healthy_long_jobs_are_never_targeted(self):
        run = self.api.runs[0]
        jobs = [
            desktop_job("macOS x86-64 release", 1, status="queued", step_status="queued"),
            desktop_job("macOS x86-64 release", 2, status="waiting", step_status="pending"),
            # A step reported as running still needs its job to be running.
            desktop_job("macOS AArch64 release", 9, status="queued"),
            desktop_job("Windows x86-64 release", 3, status="completed", step_status="completed"),
            # Four hours into a healthy build: the policy step succeeded long ago.
            desktop_job("Linux x86-64 release", 4, step_status="completed"),
            desktop_job("Linux AArch64 release", 5, step_status="queued", step_started=None),
            desktop_job("macOS x86-64 checks", 6),
            desktop_job("Linux x86-64 native", 7),
            dict(desktop_job("Windows AArch64 release", 8), steps=None),
            {"name": "CI complete", "status": "queued", "conclusion": None},
        ]
        self.assertEqual(recovery.step_deadline_candidates(run, jobs, self.now + 5 * 3600),
                         ([], []))

    def test_main_tag_manual_and_pr_runs_are_never_targeted(self):
        for key, value in (("event", "push"), ("event", "workflow_dispatch"),
                           ("event", "pull_request"), ("head_branch", "main"),
                           ("head_branch", "v1.0")):
            with self.subTest(key=key, value=value):
                self.setUp()
                self.api.event["workflow_run"][key] = value
                self.api.runs[0][key] = value
                with self.assertRaises(recovery.SkipRecovery):
                    self.watch()
                self.assertEqual(self.api.cancelled, [])

    def test_normal_cancellation_hands_off_to_fail_fast(self):
        self.after_sleep = [self.complete_cancelled]
        message = self.watch(max_probes=3)
        self.assertEqual(self.api.force_cancelled, [])
        # The deadline cancels run 123; fail-fast then stops the other active
        # exact-head required workflows. Completed results are left alone.
        self.assertEqual(self.api.cancelled, [123] + [run["id"] for run in self.api.runs[1:]])
        self.assertIn("macOS x86-64 release", message)
        self.assertIn("Step deadline step-stopped for Buster CI run 123 attempt 1", message)
        self.assertEqual(len(self.records("step-stopped")), 1)

    def test_fail_fast_waits_for_the_stuck_step_then_escalates(self):
        grace_probes = recovery.FORCE_CANCEL_GRACE_SECONDS // recovery.WATCH_SECONDS
        # Normal cancellation stops the healthy jobs but not the stuck step.
        cancelled_job = dict(desktop_job("macOS AArch64 release", 300),
                             status="completed", conclusion="cancelled")
        self.after_sleep = [lambda: self.api.jobs.append(cancelled_job)]
        # Fail-fast fires on the second probe; the force-cancel probe returns.
        message = self.watch(max_probes=grace_probes + 1)
        self.assertEqual(self.api.force_cancelled, [123])
        self.assertEqual(self.api.cancelled.count(123), 2)
        self.assertEqual(len(self.records("force-cancel-requested")), 1)
        self.assertIn("Step deadline force-cancel-requested", message)
        self.assertIn("macOS AArch64 release", message)

    def test_force_cancel_waits_for_the_full_grace(self):
        grace_probes = recovery.FORCE_CANCEL_GRACE_SECONDS // recovery.WATCH_SECONDS
        self.watch_until_timeout(max_probes=grace_probes)
        self.assertEqual(self.api.force_cancelled, [])
        self.setUp()
        self.watch_until_timeout(max_probes=grace_probes + 1)
        self.assertEqual(self.api.force_cancelled, [123])
        [line] = self.records("force-cancel-requested")
        self.assertIn(" elapsed_seconds=2100", line)

    def test_force_cancel_revalidates_the_exact_attempt(self):
        grace_probes = recovery.FORCE_CANCEL_GRACE_SECONDS // recovery.WATCH_SECONDS
        progressed = copy.deepcopy(self.api.jobs)
        progressed[0]["steps"][-1].update(status="completed", conclusion="cancelled")
        # Initial read and revalidation, then one read per probe until the
        # final force-cancel revalidation observes the step has stopped.
        self.api.job_reads = [copy.deepcopy(self.api.jobs)] * (grace_probes + 2) + [progressed]
        self.watch_until_timeout(max_probes=grace_probes + 1)
        self.assertEqual(self.api.force_cancelled, [])
        self.assertEqual(len(self.records("step-stopped")), 1)
        self.setUp()
        self.api.fresh_runs = [copy.deepcopy(self.api.runs[0]),
                               dict(self.api.runs[0], run_attempt=2)]
        with self.assertRaises(recovery.SkipRecovery):
            self.watch(max_probes=grace_probes + 1)
        self.assertEqual(self.api.force_cancelled, [])

    def test_cancel_conflict_records_finished_run_without_escalation(self):
        self.api.cancel = lambda run_id: False
        self.watch_until_timeout(max_probes=12)
        self.assertEqual(self.api.force_cancelled, [])
        self.assertEqual(len(self.records("run-already-finished")), 1)

    def test_api_errors_fail_visibly_without_further_mutation(self):
        error = urllib.error.HTTPError("https://api.github.com", 500, "error", {}, io.BytesIO())
        with mock.patch.object(self.api, "cancel", side_effect=error):
            with self.assertRaises(urllib.error.HTTPError):
                self.watch()
        self.assertEqual((self.api.cancelled, self.api.force_cancelled), ([], []))
        self.setUp()
        grace_probes = recovery.FORCE_CANCEL_GRACE_SECONDS // recovery.WATCH_SECONDS
        with mock.patch.object(self.api, "force_cancel", side_effect=error):
            with self.assertRaises(urllib.error.HTTPError):
                self.watch(max_probes=grace_probes + 1)
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(len(self.records("cancel-requested")), 1)
        self.setUp()
        with mock.patch.object(self.api, "all", side_effect=OSError("API unavailable")):
            with self.assertRaises(OSError):
                self.watch()
        self.assertEqual(self.api.cancelled, [])

    def test_client_uses_documented_cancel_endpoints_and_tolerates_conflict(self):
        api = recovery.GitHub("buster14a/buster", "unused")
        for method, endpoint in ((api.cancel, "cancel"), (api.force_cancel, "force-cancel")):
            with self.subTest(endpoint=endpoint):
                with mock.patch.object(api, "request") as request:
                    self.assertTrue(method(123))
                    request.assert_called_once_with("actions/runs/123/" + endpoint, method="POST")
                conflict = urllib.error.HTTPError("u", 409, "conflict", {}, io.BytesIO())
                with mock.patch.object(api, "request", side_effect=conflict):
                    self.assertFalse(method(123))
                failure = urllib.error.HTTPError("u", 403, "forbidden", {}, io.BytesIO())
                with mock.patch.object(api, "request", side_effect=failure):
                    with self.assertRaises(urllib.error.HTTPError):
                        method(123)

    def test_budgets_mirror_the_ci_workflow_step(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        test_job = workflow.split("\n  test:\n", 1)[1].split("\n  native:\n", 1)[0]
        self.assertIn("    name: ${{ matrix.name }} ${{ matrix.shard }}\n", test_job)
        self.assertIn("        shard: [release, checks]\n", test_job)
        lanes = re.findall(r"^          - name: (.+)\n(?:            \w+: .+\n)*?"
                           r"            os: (\w+)$", test_job, re.M)
        self.assertEqual(workflow.count("- name: " + recovery.WORKFLOW_TOOLS_STEP + "\n"), 1)
        step = re.search(r"      - name: " + recovery.WORKFLOW_TOOLS_STEP + r"\n"
                         r"((?:        .*\n)+)", test_job).group(1)
        self.assertIn("if: ${{ matrix.shard == 'release' &&", step)
        timeout = re.search(r"^        timeout-minutes: \$\{\{ \(matrix\.os == 'windows' \|\| "
                            r"matrix\.os == 'macos'\) && (\d+) \|\| (\d+) \}\}$", step, re.M)
        slow, fast = int(timeout.group(1)), int(timeout.group(2))
        self.assertEqual(len(lanes), 6)
        self.assertEqual(recovery.WORKFLOW_TOOLS_BUDGET_SECONDS, {
            name + " release": 60 * (slow if os in ("windows", "macos") else fast)
            for name, os in lanes})


if __name__ == "__main__":
    unittest.main()
