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
        self.force_cancelled = []
        self.refs = [{"ref": "refs/heads/gh-readonly-queue/main/pr-1-abc",
                      "object": {"sha": "a" * 40}}]
        # Optional per-read responses, consumed in order before the live lists.
        self.ref_reads = []
        self.run_reads = []
        self.job_reads = []
        self.fresh_runs = []

    def request(self, path, *, method="GET", **query):
        if (path, method, query) == ("actions/runs/123", "GET", {}):
            result = self.fresh_runs.pop(0) if self.fresh_runs else self.runs[0]
        elif (path, method, query) == ("rulesets/" + str(recovery.RULESET_ID), "GET", {}):
            result = self.ruleset
        elif (path, method, query) == (
                "git/matching-refs/heads/gh-readonly-queue/main/", "GET", {}):
            result = self.ref_reads.pop(0) if self.ref_reads else self.refs
        else:
            raise AssertionError((path, method, query))
        return copy.deepcopy(result)

    def all(self, path, key=None, **query):
        if path == "actions/runs":
            assert key == "workflow_runs" and query == {
                "event": "merge_group", "head_sha": "a" * 40}
            result = self.run_reads.pop(0) if self.run_reads else self.runs
        elif path in ("actions/runs/123/jobs", "actions/runs/123/attempts/1/jobs"):
            # A pass reads the latest jobs; step-deadline revalidation reads
            # the watched attempt's jobs. Both consume job_reads in order.
            assert key == "jobs" and query == (
                {"filter": "latest"} if path.endswith("123/jobs") else {})
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
    def test_main_only_self_host_policy_does_not_wait_for_missing_audit(self):
        api = FakeGitHub()
        rows = api.ruleset["rules"][3]["parameters"]["required_status_checks"]
        rows[:] = [row for row in rows if row["context"] != "Linux x86-64 bootstrap evidence"]
        names = recovery.required_checks(api, api.event["repository"])
        self.assertEqual(names, recovery.POST_MERGE_REQUIRED_CHECKS)
        api.runs = [run for run in api.runs if "self-host-audit.yml" not in run["path"]]
        for run in api.runs:
            run.update(status="completed", conclusion="success")
        for check in api.checks:
            check.update(status="completed", conclusion="success")
        results = recovery.required_check_results(api, "a" * 40,
                                                  recovery.latest_group_runs(api, "a" * 40), names)
        self.assertEqual(set(results), set(names))

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

    def test_skipped_buster_job_does_not_cancel_the_group(self):
        # Merge-group run 36831519396: the main-push-only reuse job completes
        # as skipped beside healthy shards and must not trigger fail-fast.
        self.api.jobs.append({"name": "Main CI reuse decision", "status": "completed",
                              "conclusion": "skipped"})
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

    def test_reconciled_native_check_is_bound_by_exact_head_marker(self):
        check = next(row for row in self.api.checks
                     if row["name"] == "Native retirement merge admission")
        check.update(check_suite={"id": 999999}, status="completed", conclusion="failure",
                     external_id="buster-native-retirement-admission-v1:" + "b" * 40)
        self.assertIn("remain pending", self.watch())
        self.assertEqual(self.api.cancelled, [])
        check["external_id"] = "buster-native-retirement-admission-v1:" + "a" * 40
        self.assertIn("Native retirement merge admission", self.watch())
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs])
        gate = (ROOT / "tools/merge_queue_admission.py").read_text()
        self.assertIn('RETIREMENT_MARKER = "buster-native-retirement-admission-v1"', gate)

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


# Run 36571009755's macOS x86-64 release step started here and was still in
# progress over 30 minutes later; its 5-minute budget plus grace ends 13:08:23.
INCIDENT_STEP_START = "2026-09-29T12:53:23Z"
INCIDENT_JOB_ID = 109415142502
T = recovery.github_time
GRACE_POLLS = recovery.FORCE_CANCEL_GRACE_SECONDS // recovery.STEP_DEADLINE_POLL_SECONDS


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
        ]
        self.now = T("2026-09-29T13:23:23Z")
        self.log = []
        self.sleeps = []
        self.after_sleep = []

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds
        if self.after_sleep:
            self.after_sleep.pop(0)()

    def watch(self, event_name="workflow_run"):
        event = (self.api.event if event_name == "workflow_run"
                 else {"repository": self.api.event["repository"]})
        return recovery.watch(self.api, event, event_name, clock=lambda: self.now,
                              sleep_fn=self.sleep, log=self.log)

    def complete_cancelled(self):
        self.stuck.update(status="completed", conclusion="cancelled")
        self.stuck["steps"][-1].update(status="completed", conclusion="cancelled")
        self.api.runs[0].update(status="completed", conclusion="cancelled")

    def records(self, action):
        return [line for line in self.log if " action=" + action + " " in line]

    def test_incident_cancels_then_force_cancels_only_the_exact_run(self):
        message = self.watch()
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(self.api.force_cancelled, [123])
        self.assertEqual(self.sleeps, [recovery.STEP_DEADLINE_POLL_SECONDS] * GRACE_POLLS)
        [evidence] = self.records("cancel-requesting")
        [line] = self.records("cancel-requested")
        [force_evidence] = self.records("force-cancel-requesting")
        [forced] = self.records("force-cancel-requested")
        self.assertEqual([self.log.index(item) for item in (evidence, line, force_evidence, forced)],
                         [0, 1, 2, 3])
        self.assertEqual(evidence.replace("cancel-requesting", "cancel-requested"), line)
        self.assertTrue(line.startswith("STEP_DEADLINE_V1 action=cancel-requested "))
        for field in ("run=123", "attempt=1", "head=" + "a" * 40, "job=" + str(INCIDENT_JOB_ID),
                      'job_name="macOS x86-64 release"', "step=4",
                      'step_name="Workflow tool regression tests"',
                      'started_at="2026-09-29T12:53:23Z"', "budget_seconds=300",
                      "grace_seconds=600", "deadline=2026-09-29T13:08:23Z",
                      "elapsed_seconds=1800"):
            self.assertIn(" " + field, line)
        self.assertIn(" elapsed_seconds=" + str(1800 + recovery.FORCE_CANCEL_GRACE_SECONDS),
                      forced)
        self.assertEqual(message, "Step deadline force-cancel-requested for Buster CI run 123 "
                                  "attempt 1: macOS x86-64 release job 109415142502.")

    def test_sweep_applies_the_same_deadline(self):
        message = self.watch("schedule")
        self.assertTrue(message.startswith("a" * 40 + ": Step deadline force-cancel-requested"))
        self.assertEqual((self.api.cancelled, self.api.force_cancelled), ([123], [123]))

    def test_every_desktop_lane_deadline_boundary(self):
        run = self.api.runs[0]
        start = T(INCIDENT_STEP_START)
        budgets = dict(recovery.WORKFLOW_TOOLS_BUDGET_SECONDS,
                       **recovery.HISTORICAL_WORKFLOW_TOOLS_BUDGET_SECONDS)
        for name, budget in budgets.items():
            with self.subTest(name=name):
                job = desktop_job(name, 7)
                limit = start + budget + recovery.STEP_DEADLINE_GRACE_SECONDS
                self.assertEqual(recovery.step_deadline_candidates(run, [job], limit), ([], []))
                overdue, refused = recovery.step_deadline_candidates(run, [job], limit + 1)
                self.assertEqual((len(overdue), refused), (1, []))
                self.assertEqual(overdue[0]["elapsed_seconds"],
                                 budget + recovery.STEP_DEADLINE_GRACE_SECONDS + 1)

    def test_future_step_start_does_not_trigger_deadline(self):
        self.now = T(INCIDENT_STEP_START) - 1
        self.assertEqual(recovery.step_deadline_candidates(self.api.runs[0], self.api.jobs,
                                                           self.now), ([], []))

    def test_finished_run_does_not_reuse_stale_step_metadata(self):
        for status, conclusion in (("completed", "success"), ("completed", None),
                                   ("in_progress", "cancelled")):
            with self.subTest(status=status, conclusion=conclusion):
                run = dict(self.api.runs[0], status=status, conclusion=conclusion)
                self.assertEqual(recovery.step_deadline_candidates(run, self.api.jobs, self.now),
                                 ([], []))

    def test_watch_boundary_second(self):
        self.now = T("2026-09-29T13:08:23Z")
        self.assertIn("remain pending", self.watch())
        self.assertEqual(self.api.cancelled, [])
        self.now += 1
        self.watch()
        self.assertEqual(self.api.cancelled, [123])

    def test_linux_budget_is_not_the_macos_budget(self):
        self.stuck["name"] = "Linux x86-64 release"
        self.now = T("2026-09-29T13:05:24Z")
        self.watch()
        self.assertEqual(self.api.cancelled, [123])
        self.assertIn(" budget_seconds=120 ", self.records("cancel-requested")[0])

    def test_step_progressing_between_reads_is_refused(self):
        progressed = copy.deepcopy(self.api.jobs)
        progressed[0]["steps"][-1].update(status="completed", conclusion="success")
        self.api.job_reads = [copy.deepcopy(self.api.jobs), progressed]
        self.assertIn("Step deadline refused-progressed", self.watch())
        self.assertEqual((self.api.cancelled, self.sleeps), ([], []))
        self.assertEqual(len(self.records("refused-progressed")), 1)

    def test_restarted_step_between_reads_is_refused(self):
        restarted = copy.deepcopy(self.api.jobs)
        restarted[0]["steps"][-1]["started_at"] = "2026-09-29T13:20:00Z"
        self.api.job_reads = [copy.deepcopy(self.api.jobs), restarted]
        self.assertIn("refused-progressed", self.watch())
        self.assertEqual(self.api.cancelled, [])

    def test_replaced_job_or_step_between_reads_is_refused(self):
        for key, value in (("job", INCIDENT_JOB_ID + 1), ("step", 5)):
            with self.subTest(key=key):
                self.setUp()
                replaced = copy.deepcopy(self.api.jobs)
                if key == "job":
                    replaced[0]["id"] = value
                else:
                    replaced[0]["steps"][-1]["number"] = value
                self.api.job_reads = [copy.deepcopy(self.api.jobs), replaced]
                self.watch()
                self.assertEqual(self.api.cancelled, [])
                self.assertEqual(len(self.records("refused-progressed")), 1)

    def test_changed_identity_before_mutation_is_refused(self):
        for change in ("attempt", "head", "replaced", "ref", "completed"):
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
                elif change == "ref":
                    moved = copy.deepcopy(self.api.refs)
                    moved[0]["ref"] += "-replaced"
                    self.api.ref_reads = [copy.deepcopy(self.api.refs), moved]
                else:
                    self.api.fresh_runs = [dict(fresh, status="completed", conclusion="failure")]
                message = self.watch()
                self.assertEqual(self.api.cancelled, [])
                expected = "refused-progressed" if change == "completed" else "refused-changed"
                self.assertIn("Step deadline " + expected, message)
                self.assertEqual(len(self.records(expected)), 1)

    def test_stale_or_foreign_job_identity_is_refused(self):
        for key, value in (("run_attempt", 2), ("run_id", 124), ("head_sha", "b" * 40),
                           ("id", None), ("id", "109415142502"), ("id", True), ("id", 0),
                           ("completed_at", "2026-09-29T12:54:00Z")):
            with self.subTest(key=key, value=value):
                self.setUp()
                self.stuck[key] = value
                self.assertIn("remain pending", self.watch())
                self.assertEqual(self.api.cancelled, [])
                [line] = self.records("refused")
                self.assertIn("identity does not match", line)

    def test_invalid_step_identity_or_completion_is_refused(self):
        for key, value in (("number", 0), ("number", True), ("number", None),
                           ("completed_at", "2026-09-29T12:54:00Z")):
            with self.subTest(key=key, value=value):
                self.setUp()
                self.stuck["steps"][-1][key] = value
                self.watch()
                self.assertEqual(self.api.cancelled, [])
                [line] = self.records("refused")
                self.assertIn("identity does not match", line)

    def test_concluded_in_progress_job_is_refused(self):
        # watch() itself rejects this contradiction, so check the policy alone.
        job = dict(self.stuck, conclusion="success")
        overdue, [refusal] = recovery.step_deadline_candidates(self.api.runs[0], [job], self.now)
        self.assertEqual(overdue, [])
        self.assertIn("identity does not match", refusal["reason"])

    def test_duplicate_job_is_not_cancellation_authority(self):
        self.api.jobs.append(copy.deepcopy(self.stuck))
        self.watch()
        self.assertEqual(self.api.cancelled, [])
        self.assertEqual(len(self.records("refused")), 2)

    def test_duplicate_or_concluded_step_is_refused(self):
        duplicate = copy.deepcopy(self.stuck["steps"][-1])
        self.stuck["steps"].append(dict(duplicate, number=5, status="queued"))
        self.watch()
        self.assertEqual(self.api.cancelled, [])
        self.setUp()
        self.stuck["steps"][-1]["conclusion"] = "failure"
        self.watch()
        self.assertEqual(self.api.cancelled, [])
        self.assertEqual(len(self.records("refused")), 1)

    def test_malformed_start_timestamps_are_refused(self):
        for key, value in (("step", None), ("step", ""), ("step", "not-a-time"),
                           ("step", "not-a-dateZ"), ("step", "2026-09-29Tnot-a-timeZ"),
                           ("step", "2026-09-29T12:53:23"), ("step", 1790000000),
                           ("step", "2026-02-30T12:53:23Z"), ("job", None),
                           ("job", "2026-09-29T13:00:00Z")):
            with self.subTest(key=key, value=value):
                self.setUp()
                if key == "step":
                    self.stuck["steps"][-1]["started_at"] = value
                else:
                    self.stuck["started_at"] = value
                self.watch()
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
            desktop_job("Clang analyzer shards", 10),
            desktop_job("unknown release", 11),
            # Only the policy step is budgeted, not other steps of a release lane.
            dict(desktop_job("macOS x86-64 release", 12), steps=[
                {"name": "Combination matrix (Linux, macOS)", "number": 4,
                 "status": "in_progress", "conclusion": None,
                 "started_at": INCIDENT_STEP_START}]),
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
        message = self.watch()
        self.assertEqual((self.api.cancelled, self.api.force_cancelled), ([123], []))
        self.assertEqual(self.sleeps, [recovery.STEP_DEADLINE_POLL_SECONDS])
        self.assertIn("Step deadline step-stopped for Buster CI run 123 attempt 1", message)
        self.assertEqual(len(self.records("step-stopped")), 1)
        # The cancelled run's completion event (or the sweep) then stops the
        # other active exact-head required workflows; completed results stay.
        self.assertIn("macOS x86-64 release", self.watch("schedule"))
        self.assertEqual(self.api.cancelled, [123] + [run["id"] for run in self.api.runs[1:]])
        self.assertEqual(self.api.force_cancelled, [])

    def test_fail_fast_cancel_is_escalated_in_the_same_pass(self):
        self.api.jobs.append(dict(desktop_job("macOS AArch64 release", 300),
                                  status="completed", conclusion="failure"))
        message = self.watch()
        # Fail-fast's normal cancel of run 123 is the deadline's normal cancel.
        self.assertEqual(self.api.cancelled, [run["id"] for run in self.api.runs])
        self.assertEqual(self.api.force_cancelled, [123])
        self.assertEqual(self.records("cancel-requesting"), [])
        self.assertEqual(len(self.records("cancel-requested")), 1)
        self.assertIn("Merge-group fail-fast observed macOS AArch64 release", message)
        self.assertIn("Step deadline force-cancel-requested", message)

    def test_fail_fast_cancel_that_stops_the_step_is_not_escalated(self):
        self.api.jobs.append(dict(desktop_job("macOS AArch64 release", 300),
                                  status="completed", conclusion="failure"))
        self.api.fresh_runs = [dict(self.api.runs[0], status="completed", conclusion="cancelled")]
        message = self.watch()
        self.assertEqual(self.api.force_cancelled, [])
        self.assertEqual(self.sleeps, [])
        self.assertIn("Step deadline step-stopped", message)

    def test_force_cancel_waits_for_the_full_grace(self):
        # The step stops only at the final poll: no force-cancel.
        self.after_sleep = [lambda: None] * (GRACE_POLLS - 1) + [self.complete_cancelled]
        self.watch()
        self.assertEqual(self.api.force_cancelled, [])
        self.assertEqual(sum(self.sleeps), recovery.FORCE_CANCEL_GRACE_SECONDS)
        self.setUp()
        self.watch()
        self.assertEqual(self.api.force_cancelled, [123])
        self.assertEqual(sum(self.sleeps), recovery.FORCE_CANCEL_GRACE_SECONDS)

    def test_changed_run_after_normal_cancel_is_not_force_cancelled(self):
        for change in ({"id": 999}, {"run_attempt": 2}):
            with self.subTest(change=change):
                self.setUp()
                self.after_sleep = [lambda: self.api.runs[0].update(change)]
                self.assertIn("Step deadline refused-changed", self.watch())
                self.assertEqual((self.api.cancelled, self.api.force_cancelled), ([123], []))
                self.assertEqual(len(self.records("cancel-requested")), 1)
                self.assertEqual(len(self.records("refused-changed")), 1)
        self.setUp()
        # A different head in the exact-head query is malformed and fails visibly.
        self.after_sleep = [lambda: self.api.runs[0].update(head_sha="b" * 40)]
        with self.assertRaises(ValueError):
            self.watch()
        self.assertEqual((self.api.cancelled, self.api.force_cancelled), ([123], []))

    def test_cancel_conflict_records_finished_run_without_escalation(self):
        self.api.cancel = lambda run_id: False
        self.assertIn("Step deadline run-already-finished", self.watch())
        self.assertEqual((self.api.force_cancelled, self.sleeps), ([], []))
        self.assertEqual(len(self.records("run-already-finished")), 1)

    def test_force_conflict_records_finished_run(self):
        self.api.force_cancel = lambda run_id: False
        self.assertIn("Step deadline run-already-finished", self.watch())
        self.assertEqual(len(self.records("force-cancel-requesting")), 1)
        self.assertEqual(len(self.records("run-already-finished")), 1)

    def test_api_errors_fail_visibly_without_further_mutation(self):
        error = urllib.error.HTTPError("https://api.github.com", 500, "error", {}, io.BytesIO())
        with mock.patch.object(self.api, "cancel", side_effect=error):
            with self.assertRaises(urllib.error.HTTPError):
                self.watch()
        self.assertEqual((self.api.cancelled, self.api.force_cancelled), ([], []))
        self.assertEqual(len(self.records("cancel-requesting")), 1)
        self.assertEqual(self.records("cancel-requested"), [])
        self.setUp()
        with mock.patch.object(self.api, "force_cancel", side_effect=error):
            with self.assertRaises(urllib.error.HTTPError):
                self.watch()
        self.assertEqual(self.api.cancelled, [123])
        self.assertEqual(len(self.records("cancel-requested")), 1)
        self.assertEqual(len(self.records("force-cancel-requesting")), 1)
        self.assertEqual(self.records("force-cancel-requested"), [])
        self.setUp()
        unavailable = urllib.error.HTTPError("u", 503, "unavailable", {}, io.BytesIO())
        live_all = self.api.all

        def revalidation_fails(path, key=None, **query):
            if "/attempts/" in path:
                raise unavailable
            return live_all(path, key, **query)
        with mock.patch.object(self.api, "all", side_effect=revalidation_fails):
            with self.assertRaises(urllib.error.HTTPError):
                self.watch()
        self.assertEqual(self.api.cancelled, [])
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
                for code in (403, 429, 503):
                    failure = urllib.error.HTTPError("u", code, "error", {}, io.BytesIO())
                    with mock.patch.object(api, "request", side_effect=failure):
                        with self.assertRaises(urllib.error.HTTPError):
                            method(123)

    def test_in_pass_grace_fits_the_watcher_timeout(self):
        workflow = (ROOT / ".github/workflows/ci-merge-group-watch.yml").read_text()
        minutes = int(re.search(r"^    timeout-minutes: (\d+)$", workflow, re.M).group(1))
        self.assertEqual(recovery.FORCE_CANCEL_GRACE_SECONDS % recovery.STEP_DEADLINE_POLL_SECONDS, 0)
        # Leave at least two minutes of the job for checkout and API reads.
        self.assertLessEqual(recovery.FORCE_CANCEL_GRACE_SECONDS + 120, 60 * minutes)

    def test_budgets_mirror_the_ci_workflow_step(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        test_job = workflow.split("\n  test:\n", 1)[1].split("\n  native:\n", 1)[0]
        suffix = re.search(r"^    name: \$\{\{ matrix\.name \}\} \$\{\{ matrix\.shard \}\}(.*)$",
                           test_job, re.M).group(1)
        # Merge groups see the bare "<lane> <shard>" name the budgets key on;
        # a suffix may only apply to pull_request runs (draft deferral).
        self.assertTrue(suffix == "" or (
            suffix.startswith("${{ github.event_name == 'pull_request' && ") and
            suffix.endswith(" || '' }}")), suffix)
        # Ordinary split jobs retain one Release owner per lane; only these
        # exact manual comparison refs use the historical combined matrix.
        combined_refs = (
            "refs/heads/codex/ci-checks-combined-overlap",
            "refs/heads/codex/ci-checks-combined-all-builds",
            "refs/heads/codex/2120-evidence-v2-combined-overlap",
            "refs/heads/codex/2120-evidence-v2-combined-all-builds",
        )
        dispatch_guard = "github.event_name == 'workflow_dispatch' && (" + " || ".join(
            "github.ref == '" + ref + "'" for ref in combined_refs) + ")"
        shard_expression = re.search(r"^        shard: \$\{\{ fromJSON\((.+)\) \}\}$",
                                     test_job, re.M).group(1)
        self.assertEqual(shard_expression, dispatch_guard + " && '[\"release\", \"checks\"]' || "
                         "'[\"release\", \"checks\", \"sanitized-release\", \"portability\"]'")
        # Evaluate the pinned expression for the event this watcher owns.
        selector = shard_expression.replace("github.event_name", "event").replace("github.ref", "ref")
        selector = selector.replace("&&", "and").replace("||", "or")
        for ref in combined_refs + ("refs/heads/gh-readonly-queue/main/pr-2440-abc",):
            with self.subTest(event="merge_group", ref=ref):
                shards = json.loads(eval(selector, {"__builtins__": {}}, {"event": "merge_group", "ref": ref}))
                self.assertEqual(shards, ["release", "checks", "sanitized-release", "portability"])
        lanes = re.findall(r"^          - name: (.+)\n(?:            \w+: .+\n)*?"
                           r"            os: (\w+)$", test_job, re.M)
        self.assertEqual(workflow.count("- name: " + recovery.WORKFLOW_TOOLS_STEP + "\n"), 1)
        step = re.search(r"      - name: " + recovery.WORKFLOW_TOOLS_STEP + r"\n"
                         r"((?:        .*\n)+)", test_job).group(1)
        self.assertIn("if: ${{ matrix.shard == 'release' &&", step)
        timeout = re.search(r"^        timeout-minutes: \$\{\{ \(matrix\.os == 'windows' \|\| "
                            r"matrix\.os == 'macos'\) && (\d+) \|\| (\d+) \}\}$", step, re.M)
        slow, fast = int(timeout.group(1)), int(timeout.group(2))
        self.assertEqual(len(lanes), 5)
        self.assertEqual(recovery.WORKFLOW_TOOLS_BUDGET_SECONDS, {
            name + " release": 60 * (slow if os in ("windows", "macos") else fast)
            for name, os in lanes})
        self.assertEqual(recovery.HISTORICAL_WORKFLOW_TOOLS_BUDGET_SECONDS,
                         {"macOS x86-64 release": 5 * 60})


if __name__ == "__main__":
    unittest.main()
