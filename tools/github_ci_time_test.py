#!/usr/bin/env python3
"""Queue/runner-assignment tests for tools/github_ci_time.py (#1805) and the
macOS runner demand of auxiliary workflows (#1825)."""
import argparse
import copy
from datetime import datetime, timezone
import io
from pathlib import Path
import re
import sys
import textwrap
import unittest
from unittest import mock
import urllib.parse

import github_ci_time
import merge_queue_admission as admission

ROOT = Path(__file__).resolve().parents[1]


class ChecksLayoutCLITests(unittest.TestCase):
    def test_gate_cli_keeps_the_combined_default_and_accepts_explicit_split(self):
        for arguments, expected in (([], "combined"), (["--checks-layout", "combined"], "combined"),
                                    (["--checks-layout", "split"], "split")):
            with self.subTest(arguments=arguments):
                with mock.patch.object(sys, "argv", ["github_ci_time.py", "require-jobs", *arguments]), \
                        mock.patch.object(github_ci_time, "require_jobs", return_value={"success": True}) as gate, \
                        mock.patch.object(sys, "stdout", io.StringIO()):
                    self.assertEqual(github_ci_time.main(), 0)
                self.assertEqual(gate.call_args.args[0].checks_layout, expected)

    def test_unknown_layout_cannot_relax_the_inventory_gate(self):
        for layout in ("all", "mixed", "sanitized-debug", ""):
            with self.subTest(layout=layout):
                with self.assertRaises(ValueError):
                    github_ci_time.combination_jobs(layout)
                with mock.patch.object(sys, "argv", ["github_ci_time.py", "require-jobs", "--checks-layout", layout]), \
                        mock.patch.object(github_ci_time, "require_jobs") as gate, \
                        mock.patch.object(sys, "stderr", io.StringIO()):
                    with self.assertRaises(SystemExit) as failure:
                        github_ci_time.main()
                self.assertEqual(failure.exception.code, 2)
                gate.assert_not_called()


class ReconciledInventoryTests(unittest.TestCase):
    """#2388: replay the hosted row shape without relaxing workload proof."""

    RUN = 37027518864
    HEAD = "f466463ea852ef00478ef1aa5f56bfe9f8a6a7fb"

    def setUp(self):
        self.run = {"id": self.RUN, "run_attempt": 1, "head_sha": self.HEAD,
                    "path": ".github/workflows/ci.yml", "event": "merge_group"}
        self.jobs = []
        for index, name in enumerate(github_ci_time.COMBINATION_JOBS):
            self.jobs.append({"id": 1000 + index, "name": name, "run_id": self.RUN,
                              "run_attempt": 1, "head_sha": self.HEAD,
                              "status": "in_progress" if name == "CI complete" else "completed",
                              "conclusion": None if name == "CI complete" else "success",
                              "steps": [{"name": step, "status": "completed", "conclusion": "success"}
                                        for step in github_ci_time._required_job_steps(name)]})
        self.checks = []
        for identity, (name, prefix) in zip((110906094381, 110906108065),
                                          github_ci_time.RECONCILED_CHECK_MARKERS.items()):
            self.jobs.append({"id": identity, "name": name, "run_id": self.RUN,
                              "run_attempt": 1, "head_sha": self.HEAD, "status": "in_progress",
                              "conclusion": None, "steps": [], "labels": [], "runner_id": None})
            self.checks.append({"id": identity, "name": name, "head_sha": self.HEAD,
                                "app": {"id": 15368}, "external_id": prefix + self.HEAD,
                                "status": "in_progress", "conclusion": None})

    def gate(self, jobs=None, checks=None):
        jobs = self.jobs if jobs is None else jobs
        checks = self.checks if checks is None else checks
        def reply(repository, path, token, **kwargs):
            if path == f"actions/runs/{self.RUN}":
                return self.run
            if path.startswith(f"actions/runs/{self.RUN}/jobs?"):
                return {"total_count": len(jobs), "jobs": jobs}
            if path.startswith(f"commits/{self.HEAD}/check-runs?"):
                return {"total_count": len(checks), "check_runs": checks}
            if path.startswith("check-runs/") and "/annotations" in path:
                return []
            raise AssertionError(path)
        arguments = argparse.Namespace(repository="buster14a/buster", run_id=self.RUN, run_attempt=1,
                                       checks_layout="combined", event_name=None, event_path=None)
        with mock.patch.object(github_ci_time, "api_get", side_effect=reply), \
                mock.patch.object(github_ci_time.time, "sleep"):
            result = github_ci_time.require_jobs(arguments)
        return result

    def test_metadata_contract_matches_existing_trusted_publisher(self):
        self.assertEqual(github_ci_time.GITHUB_ACTIONS_APP_ID, admission.GITHUB_ACTIONS_APP_ID)
        self.assertEqual(github_ci_time.RECONCILED_CHECK_MARKERS,
                         {admission.CONTEXT: admission.SCHEMA + ":",
                          admission.RETIREMENT_CONTEXT: admission.RETIREMENT_MARKER + ":"})

    def test_pending_success_and_failed_metadata_are_retained_outside_workloads(self):
        for status, conclusion in (("in_progress", None), ("completed", "success"),
                                   ("completed", "failure"), ("completed", "cancelled")):
            with self.subTest(status=status, conclusion=conclusion):
                self.setUp()
                for row in self.jobs[-2:] + self.checks:
                    row.update(status=status, conclusion=conclusion)
                result = self.gate()
                self.assertTrue(result["success"], result["errors"])
                self.assertEqual(len(result["jobs"]), len(github_ci_time.COMBINATION_JOBS))
                self.assertEqual([record["job"] for record in result["reconciled_checks"]], self.jobs[-2:])
                self.assertEqual([record["check"] for record in result["reconciled_checks"]], self.checks)
                self.assertEqual(result["job_metadata"]["snapshot_attempts"], 1)

    def test_no_name_or_empty_step_exemption_without_unique_provenance(self):
        changes = (("id", 1), ("name", "Unknown admission"), ("head_sha", "b" * 40),
                   ("app", {"id": 1}), ("external_id", "unproved"),
                   ("external_id", github_ci_time.RECONCILED_CHECK_MARKERS["Main integration admission"] + "b" * 40))
        for field, value in changes:
            with self.subTest(field=field, value=value):
                checks = copy.deepcopy(self.checks)
                checks[0][field] = value
                self.assertFalse(self.gate(checks=checks)["success"])
        for checks in ([], self.checks + [copy.deepcopy(self.checks[0])],
                       self.checks + [dict(self.checks[0], id=9999)]):
            self.assertFalse(self.gate(checks=checks)["success"])

    def test_unrelated_check_rows_cannot_hide_omissions_with_duplicate_or_malformed_ids(self):
        duplicate = self.checks + [{"id": 9998, "name": "Other first"},
                                   {"id": 9998, "name": "Other second"}]
        self.assertFalse(self.gate(checks=duplicate)["success"])
        for identity in (None, True, "9998", {"bad": 1}, 0, -1):
            with self.subTest(identity=identity):
                self.assertFalse(self.gate(checks=self.checks + [{"id": identity, "name": "Other"}])["success"])
        self.assertTrue(self.gate(checks=self.checks + [{"id": 9998, "name": "Other"}])["success"])

    def test_wrong_run_attempt_execution_and_duplicate_rows_remain_failures(self):
        for field, value in (("id", None), ("id", True), ("id", {"bad": 1}),
                             ("run_id", 1), ("head_sha", "b" * 40), ("run_attempt", 2),
                             ("run_attempt", True), ("steps", [{"name": "Executed"}]), ("runner_id", 42)):
            with self.subTest(field=field, value=value):
                jobs = copy.deepcopy(self.jobs)
                jobs[-2][field] = value
                self.assertFalse(self.gate(jobs=jobs)["success"])
        for duplicate in (copy.deepcopy(self.jobs[-2]), dict(self.jobs[-2], id=9999),
                          dict(self.jobs[0], id=self.jobs[-2]["id"])):
            self.assertFalse(self.gate(jobs=self.jobs + [duplicate])["success"])

    def test_missing_unknown_failed_workloads_and_required_steps_remain_failures(self):
        controls = [self.jobs[1:], self.jobs + [dict(self.jobs[0], name="Unknown workload", id=9999)]]
        for field, value in (("conclusion", "failure"), ("run_attempt", 2), ("steps", [])):
            jobs = copy.deepcopy(self.jobs)
            jobs[0][field] = value
            controls.append(jobs)
        for jobs in controls:
            self.assertFalse(self.gate(jobs=jobs)["success"])

    def test_partial_moving_or_excessive_check_snapshots_are_refused(self):
        first = {"total_count": 2, "check_runs": self.checks[:1]}
        cases = ((first, {"total_count": 2, "check_runs": []}),
                 ({"total_count": 200, "check_runs": [{}] * 100},
                  {"total_count": 150, "check_runs": [{}] * 50}),
                 (first, {"total_count": 3, "check_runs": self.checks[1:]}),
                 ({"total_count": 1001, "check_runs": self.checks},),
                 ({"total_count": True, "check_runs": self.checks},))
        for pages in cases:
            with self.subTest(pages=pages):
                with mock.patch.object(github_ci_time, "api_get", side_effect=pages):
                    with self.assertRaises(ValueError):
                        github_ci_time._gate_reconciled_checks(
                            "buster14a/buster", self.HEAD, None, github_ci_time.time.monotonic() + 30, [])
        with mock.patch.object(github_ci_time, "api_get", side_effect=[
                {"total_count": 101, "check_runs": [{}] * 100},
                {"total_count": 101, "check_runs": [{}]}]) as read:
            self.assertEqual(len(github_ci_time._gate_reconciled_checks(
                "buster14a/buster", self.HEAD, None, github_ci_time.time.monotonic() + 30, [])), 101)
            self.assertEqual(read.call_count, 2)
        with mock.patch.object(github_ci_time, "api_get") as read:
            with self.assertRaises(ValueError):
                github_ci_time._gate_reconciled_checks(
                    "buster14a/buster", self.HEAD, None, github_ci_time.time.monotonic() - 1, [])
            read.assert_not_called()

    def test_api_failure_has_no_proof_and_retries_within_existing_snapshot_budget(self):
        with mock.patch.object(github_ci_time, "_gate_reconciled_checks",
                               side_effect=ValueError("check read unresolved")):
            result = self.gate()
        self.assertFalse(result["success"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 4)
        self.assertEqual(result["reconciled_checks"], [])
        self.assertTrue(any("check read unresolved" in error for error in result["errors"]))


class QueueTimingTests(unittest.TestCase):
    """#1805: runner assignment is measured from runner identity, not placeholder timestamps."""

    def job(self, identity, created, started=None, completed=None, runner=0, labels=("macos-26",), status="completed"):
        return {"id": identity, "run_id": 1, "run_attempt": 1, "head_sha": "a" * 40, "name": f"job {identity}",
                "status": status, "conclusion": "success" if status == "completed" and runner else None,
                "created_at": created, "started_at": started, "completed_at": completed,
                "labels": list(labels), "runner_id": runner, "runner_name": "", "runner_group_name": ""}

    def data(self, jobs, observed="2026-09-29T12:00:00Z"):
        run = {"id": 1, "path": ".github/workflows/ci.yml", "event": "merge_group", "status": "in_progress",
               "conclusion": None, "created_at": "2026-09-29T08:00:00Z", "updated_at": observed,
               "selected_by": ["created"], "observed_at": observed,
               "attempts": {"1": {"run_started_at": "2026-09-29T08:00:00Z"}}, "jobs": jobs}
        return {"kind": "queue", "repository": "o/r", "fetched_at": observed,
                "window": {"since": "2026-09-29T08:00:00Z", "until": "2026-09-29T12:00:00Z"}, "runs": [run]}

    def test_assigned_job_splits_dependency_assignment_and_execution(self):
        report = github_ci_time.queue_summarize(self.data([self.job(
            1, "2026-09-29T08:01:00Z", "2026-09-29T08:11:00Z", "2026-09-29T08:16:00Z", runner=7)]))
        group = report["groups"][0]
        self.assertEqual((group["labels"], group["event"], group["family"]), ("macos-26", "merge_group", "macos"))
        self.assertEqual(group["dependency_wait_seconds"]["p50"], 60)
        self.assertEqual(group["assignment_wait_seconds"]["p50"], 600)
        self.assertEqual(group["execution_seconds"]["p50"], 300)
        self.assertEqual(group["runner_seconds"], 300)

    def test_placeholder_start_of_unassigned_job_is_not_execution(self):
        queued = self.job(2, "2026-09-29T11:00:00Z", "2026-09-29T11:00:05Z", status="queued")
        group = github_ci_time.queue_summarize(self.data([queued]))["groups"][0]
        self.assertEqual(group["assignment_wait_seconds"], {"n": 0})
        self.assertEqual(group["execution_seconds"], {"n": 0})
        self.assertEqual(group["queued_at_observation"], 1)
        self.assertEqual(group["oldest_ready"]["job_id"], 2)
        self.assertEqual(group["oldest_ready"]["age_seconds"], 3600)

    def test_occupancy_plateau_while_jobs_wait(self):
        jobs = [self.job(1, "2026-09-29T09:30:00Z", "2026-09-29T09:30:00Z", "2026-09-29T10:30:00Z", runner=1),
                self.job(2, "2026-09-29T09:30:00Z", "2026-09-29T09:30:00Z", "2026-09-29T10:30:00Z", runner=2),
                self.job(3, "2026-09-29T09:40:00Z", "2026-09-29T10:30:00Z", "2026-09-29T10:40:00Z", runner=3),
                self.job(4, "2026-09-29T09:40:00Z", "2026-09-29T09:40:00Z", "2026-09-29T09:50:00Z",
                         labels=("ubuntu-26.04",), runner=4)]
        occupancy = github_ci_time.queue_summarize(self.data(jobs))["occupancy_by_family"]
        # Measurement starts after the 90-minute warm-up; job 3 waits 09:40-10:30 with two runners busy.
        self.assertEqual(occupancy["macos"]["measured_from"], "2026-09-29T09:30:00+00:00")
        self.assertEqual(occupancy["macos"]["occupied_while_waiting_seconds"], {"2": 3000.0})
        self.assertEqual(occupancy["macos"]["max_occupied_while_waiting"], 2)
        self.assertEqual(occupancy["ubuntu"]["seconds_with_waiting_jobs"], 0)

    def test_duplicate_job_attempt_is_rejected(self):
        job = self.job(1, "2026-09-29T08:01:00Z")
        with self.assertRaises(ValueError):
            github_ci_time.queue_summarize(self.data([job, dict(job)]))

    def test_historical_listing_refuses_partial_pages_but_live_listing_records_drift(self):
        pages = [{"total_count": 3, "items": [1, 2]}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=lambda *args, **kwargs: pages[0]):
            with self.assertRaises(ValueError):
                github_ci_time._complete_pages("o/r", "x", "items", None, 1000, False)
            self.assertEqual(github_ci_time._complete_pages("o/r", "x", "items", None, 1000, True), ([1, 2], 3))
        with mock.patch.object(github_ci_time, "api_get", return_value={"total_count": 1001, "items": []}):
            with self.assertRaises(ValueError):
                github_ci_time._complete_pages("o/r", "x", "items", None, 1000, True)

    def test_created_windows_split_disjointly_under_search_limit(self):
        def reply(repository, path, token):
            left, right = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)["created"][0].split("..")
            span = (github_ci_time.timestamp(right) - github_ci_time.timestamp(left)).total_seconds()
            return {"total_count": int(span)}
        since = datetime(2026, 9, 29, 8, tzinfo=timezone.utc)
        until = datetime(2026, 9, 29, 9, tzinfo=timezone.utc)
        with mock.patch.object(github_ci_time, "api_get", side_effect=reply):
            windows = github_ci_time._created_windows("o/r", since, until, None)
        self.assertEqual(windows[0][0], since)
        self.assertEqual(windows[-1][1], until)
        for (_, right), (left, _) in zip(windows, windows[1:]):
            self.assertEqual((left - right).total_seconds(), 1)
        self.assertTrue(all((right - left).total_seconds() <= 1000 for left, right in windows))


class MacosRunnerDemandTests(unittest.TestCase):
    """#1825: auxiliary workflows request macOS runners only where they add coverage."""

    WORKFLOWS = ROOT / ".github/workflows"

    @staticmethod
    def unix_harness(text):
        step = text.split("      - name: Native harness tests (Unix)\n", 1)[1].split("\n      - ", 1)[0]
        return textwrap.dedent(step.split("        run: |\n", 1)[1])

    @staticmethod
    def path_pattern(glob):
        parts = re.split(r"(\*\*|\*)", glob)
        return re.compile("".join(".*" if part == "**" else "[^/]*" if part == "*" else re.escape(part)
                                  for part in parts) + r"\Z")

    @staticmethod
    def include_closure(roots):
        """Every existing file a root can include, ignoring preprocessor conditions."""
        pending = [ROOT / root for root in roots]
        closure = set()
        while pending:
            path = pending.pop()
            relative = path.relative_to(ROOT).as_posix()
            if relative in closure:
                continue
            closure.add(relative)
            text = path.read_text(encoding="utf-8", errors="replace")
            for kind, name in re.findall(r'(?m)^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', text):
                candidates = [ROOT / "src" / name] if kind == "<" else [path.parent / name, ROOT / name, ROOT / "src" / name]
                found = next((candidate for candidate in candidates if candidate.is_file()), None)
                if found is not None:
                    pending.append(found.resolve())
        return closure

    def test_throughput_verdict_never_waits_for_macos(self):
        text = (self.WORKFLOWS / "compiler-throughput.yml").read_text(encoding="utf-8")
        harness = text.split("\n  harness:\n", 1)[1].split("\n  regression-guard:\n", 1)[0]
        self.assertIn("        os: [ubuntu-26.04, windows-2025]\n", harness)
        self.assertNotIn("macos", text.replace("throughput-harness-macos.yml", ""))
        self.assertEqual(text.count("    needs: harness\n"), 2)

    def test_macos_harness_is_path_filtered_ready_and_identical(self):
        throughput = (self.WORKFLOWS / "compiler-throughput.yml").read_text(encoding="utf-8")
        text = (self.WORKFLOWS / "throughput-harness-macos.yml").read_text(encoding="utf-8")
        self.assertEqual(self.unix_harness(text), self.unix_harness(throughput))
        self.assertIn("    types: [opened, synchronize, reopened, ready_for_review]\n", text)
        self.assertIn("!github.event.pull_request.draft", text)
        self.assertIn("    runs-on: macos-26\n", text)
        self.assertNotIn("merge_group", text)
        globs = re.findall(r"^      - '([^']+)'$", text.split("    paths:\n", 1)[1].split("  workflow_dispatch:", 1)[0], re.M)
        patterns = [self.path_pattern(glob) for glob in globs]
        closure = self.include_closure(("build.c", "tools/throughput/tests.c", "tools/throughput/shared.c"))
        closure.add("tools/allocation_census.py")
        self.assertIn("tools/throughput/throughput.c", closure)
        self.assertIn("src/buster/lib/os.c", closure)
        uncovered = sorted(path for path in closure if not any(pattern.match(path) for pattern in patterns))
        self.assertEqual(uncovered, [])
        self.assertFalse(any(pattern.match("src/buster/lib/compiler/frontend/c/c_gen.c") for pattern in patterns))
        self.assertFalse(any(pattern.match("docs/ci-runner-queue.md") for pattern in patterns))

    def test_materializer_merge_groups_keep_only_the_linux_leg(self):
        text = (self.WORKFLOWS / "native-retirement-materializer.yml").read_text(encoding="utf-8")
        self.assertIn("  merge_group:\n    types: [checks_requested]\n", text)
        self.assertIn("        runner: ${{ fromJSON(github.event_name == 'merge_group' && '[\"ubuntu-26.04\"]' "
                      "|| '[\"ubuntu-26.04\", \"macos-26\"]') }}\n", text)


if __name__ == "__main__":
    unittest.main()
