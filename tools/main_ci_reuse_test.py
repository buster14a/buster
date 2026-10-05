#!/usr/bin/env python3
"""Network-free source and current-run tests for main CI reuse."""

import copy
import contextlib
import io
import json
import urllib.error
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
    if name in inventory.ANALYZER:
        required.update(reuse.ANALYZER_STEPS)
    return {"id": len(name) + run_id, "name": name, "run_id": run_id,
            "run_attempt": 1, "head_sha": SHA, "status": status,
            "conclusion": conclusion,
            "steps": [{"name": step, "status": "completed", "conclusion": "success"}
                      for step in sorted(required)] if conclusion != "skipped" else []}


class FakeAPI:
    def __init__(self):
        self.current = run(CURRENT_ID, "push", "main", NOW - timedelta(minutes=1))
        self.source = run(SOURCE_ID, "merge_group", BRANCH, NOW - timedelta(minutes=2))
        self.jobs = [job(name, SOURCE_ID) for name in inventory.combination_jobs()]
        for index, record in enumerate(self.jobs):
            record["id"] = 1000 + index
        self.checks = []
        self.artifacts = []
        for index, (_, prefix, _) in enumerate(reuse.SOURCE_COVERAGE):
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
        for record in self.main_jobs:
            if record['name'] in reuse.DESKTOP_NAMES:
                record['steps'] = [dict(name=name, status='completed', conclusion='success')
                                   for name in reuse.CACHE_STEPS]
                record['steps'] += [dict(name=name, status='completed', conclusion='skipped')
                                    for name in reuse.VALIDATION_STEPS]
            if record['name'] in inventory.ANALYZER:
                record['steps'] = [dict(name=name, status='completed', conclusion='success')
                                   for name in reuse.ANALYZER_RECEIPT_STEPS]
                record['steps'] += [dict(name=name, status='completed', conclusion='skipped')
                                    for name in reuse.ANALYZER_STEPS]
        self.main_jobs.append(job(inventory.MAIN_REUSE_JOB, CURRENT_ID))
        self.main_jobs += [job(name, CURRENT_ID, conclusion="skipped") for name in reuse.REUSED_NAMES]
        for index, record in enumerate(self.main_jobs):
            record["id"] = 2000 + index
        self.movement = False

    def add_reconciled_metadata(self, status="in_progress", conclusion=None):
        for index, (name, prefix) in enumerate(inventory.RECONCILED_CHECK_MARKERS.items()):
            identity = 3000 + index
            row = {"id": identity, "name": name, "run_id": SOURCE_ID, "run_attempt": 1,
                   "head_sha": SHA, "status": status, "conclusion": conclusion,
                   "steps": [], "runner_id": None}
            self.jobs.append(copy.deepcopy(row))
            self.main_jobs.append(dict(row, run_id=CURRENT_ID))
            self.checks.append({"id": identity, "name": name, "head_sha": SHA,
                                "app": {"id": 15368}, "external_id": prefix + SHA,
                                "status": status, "conclusion": conclusion})

    def get(self, path, **query):
        if path == f"commits/{SHA}/check-runs" and query == {"filter": "all", "per_page": 100, "page": 1}:
            return {"total_count": len(self.checks), "check_runs": copy.deepcopy(self.checks)}
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
        if path == f"actions/workflows/{reuse.WORKFLOW_ID}/runs" and field == "workflow_runs":
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

    def test_reconciled_metadata_is_proved_and_retained_in_both_reuse_readers(self):
        for status, conclusion in (("in_progress", None), ("completed", "success"),
                                   ("completed", "failure"), ("completed", "cancelled")):
            with self.subTest(status=status, conclusion=conclusion):
                self.api = FakeAPI()
                self.api.add_reconciled_metadata(status, conclusion)
                source_evidence, main_evidence = {}, {}
                receipt = reuse.verify_source(self.api, SHA, CURRENT_ID, BLOB, NOW,
                                              diagnostics=source_evidence)
                current = reuse.verify_current_jobs(self.api, SHA, CURRENT_ID, diagnostics=main_evidence)
                self.assertEqual(len(receipt["source_jobs"]), 25)
                self.assertEqual(len(current), 19)
                self.assertEqual([row["job"] for row in source_evidence["source_reconciled_checks"]],
                                 self.api.jobs[-2:])
                self.assertEqual([row["job"] for row in main_evidence["current_reconciled_checks"]],
                                 self.api.main_jobs[-2:])
                self.assertTrue(all(row["check"]["external_id"].endswith(SHA) for row in
                                    source_evidence["source_reconciled_checks"] +
                                    main_evidence["current_reconciled_checks"]))

    def test_metadata_verdict_change_does_not_change_workload_reuse_receipt(self):
        self.api.add_reconciled_metadata()
        first = self.admit()
        for row in self.api.jobs[-2:] + self.api.main_jobs[-2:] + self.api.checks:
            row.update(status="completed", conclusion="success")
        second = self.admit()
        self.assertEqual(first, second)
        self.assertEqual(reuse.receipt_digest(first), reuse.receipt_digest(second))

    def test_unproved_metadata_fails_both_readers(self):
        changes = (("id", 9999), ("name", "Unknown metadata"), ("head_sha", "d" * 40),
                   ("app", {"id": 1}), ("external_id", "not-reviewed"))
        for field, value in changes:
            for source in (True, False):
                with self.subTest(field=field, source=source):
                    self.api = FakeAPI()
                    self.api.add_reconciled_metadata()
                    self.api.checks[0][field] = value
                    with self.assertRaises(AdmissionError):
                        if source:
                            self.admit()
                        else:
                            reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)
        for source in (True, False):
            self.api = FakeAPI()
            self.api.add_reconciled_metadata()
            self.api.checks = []
            with self.assertRaises(AdmissionError):
                if source:
                    self.admit()
                else:
                    reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)

    def test_duplicate_metadata_and_missing_workloads_still_fail_both_readers(self):
        for source in (True, False):
            for defect in ("duplicate-job", "duplicate-check", "missing-workload", "unknown-workload"):
                with self.subTest(source=source, defect=defect):
                    self.api = FakeAPI()
                    self.api.add_reconciled_metadata()
                    rows = self.api.jobs if source else self.api.main_jobs
                    if defect == "duplicate-job":
                        rows.append(copy.deepcopy(rows[-2]))
                    elif defect == "duplicate-check":
                        self.api.checks.append(dict(self.api.checks[0], id=9999))
                    elif defect == "missing-workload":
                        rows.pop(0)
                    else:
                        rows.append(dict(rows[0], name="Unknown workload", id=9999))
                    with self.assertRaises(AdmissionError):
                        if source:
                            self.admit()
                        else:
                            reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)

    def test_unrelated_duplicate_or_malformed_check_ids_fail_both_reuse_readers(self):
        for source in (True, False):
            for identity in (None, True, "9998", {"bad": 1}, 0, -1, "duplicate"):
                with self.subTest(source=source, identity=identity):
                    self.api = FakeAPI()
                    self.api.add_reconciled_metadata()
                    if identity == "duplicate":
                        self.api.checks += [{"id": 9998, "name": "Other first"},
                                            {"id": 9998, "name": "Other second"}]
                    else:
                        self.api.checks.append({"id": identity, "name": "Other"})
                    with self.assertRaises(AdmissionError):
                        if source:
                            self.admit()
                        else:
                            reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)

    def test_complete_check_proof_uses_strict_pagination(self):
        api = GitHub(reuse.REPOSITORY, "unused")
        cases = (({"total_count": 2, "check_runs": []},),
                 ({"total_count": 200, "check_runs": [{}] * 100},
                  {"total_count": 150, "check_runs": [{}] * 50}),
                 ({"total_count": 101, "check_runs": [{}] * 100},
                  {"total_count": 101, "check_runs": []}),
                 ({"total_count": True, "check_runs": []},),
                 ({"total_count": 1001, "check_runs": []},),
                 ({"total_count": 2, "check_runs": [{}]},))
        for pages in cases:
            with self.subTest(pages=pages):
                with mock.patch.object(api, "get", side_effect=pages):
                    with self.assertRaises(AdmissionError):
                        reuse.reconciled_check_inventory(api, SHA)
        with mock.patch.object(api, "get", side_effect=[
                {"total_count": 101, "check_runs": [{}] * 100},
                {"total_count": 101, "check_runs": [{}]}]) as read:
            self.assertEqual(len(reuse.reconciled_check_inventory(api, SHA)), 101)
            self.assertEqual(read.call_count, 2)
        with mock.patch.object(api, "get", side_effect=OSError("check evidence unavailable")):
            with self.assertRaises(OSError):
                reuse.reconciled_check_inventory(api, SHA)

    def test_reuse_token_has_only_read_permission_for_check_proof(self):
        text = (Path(__file__).resolve().parents[1] / reuse.WORKFLOW_PATH).read_text()
        block = text.split("\n  reuse:\n", 1)[1].split("\n  test:\n", 1)[0]
        permission = block.split("    permissions:\n", 1)[1].split("    runs-on:", 1)[0]
        self.assertIn("      checks: read\n", permission)
        self.assertNotIn("write", permission)

    def test_exact_commit_source_and_main_specific_jobs(self):
        receipt = self.admit()
        self.assertEqual(len(receipt["source_jobs"]), 25)
        self.assertEqual(len(receipt["source_artifacts"]), 25)
        self.assertEqual(receipt["source_run_id"], SOURCE_ID)
        self.assertEqual(len(reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)), 19)
        self.assertRegex(reuse.receipt_digest(receipt), r"[0-9a-f]{64}\Z")

    def test_current_split_reuse_rejects_the_historical_combined_inventory(self):
        self.assertEqual(inventory.combination_jobs(), inventory.SPLIT_COMBINATION_JOBS)
        self.assertEqual(len(inventory.combination_jobs()), 27)
        self.assertEqual(len(inventory.combination_jobs("split")), 27)
        self.assertEqual(len(reuse.RETAINED_NAMES), 19)
        self.assertNotIn("Windows x86-64 checks", reuse.RETAINED_NAMES)
        self.assertIn("Windows x86-64 sanitized-debug", reuse.RETAINED_NAMES)
        self.api.jobs = [job(name, SOURCE_ID) for name in inventory.combination_jobs("combined")]
        with self.assertRaises(AdmissionError):
            self.admit()

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

    def test_all_desktop_source_coverage_and_artifacts_are_required(self):
        for name, prefix, mandatory in reuse.DESKTOP:
            with self.subTest(name=name):
                for mutation in ("missing", "duplicate", "failure", "cancelled", "skipped"):
                    self.api = FakeAPI()
                    source = next(j for j in self.api.jobs if j['name'] == name)
                    if mutation == "missing":
                        self.api.jobs.remove(source)
                    elif mutation == "duplicate":
                        self.api.jobs.append(dict(copy.deepcopy(source), id=9999))
                    else:
                        source['conclusion'] = mutation
                    with self.subTest(mutation=mutation), self.assertRaises(AdmissionError):
                        self.admit()
                self.api = FakeAPI()
                source = next(j for j in self.api.jobs if j['name'] == name)
                source['steps'] = [s for s in source['steps'] if s['name'] != mandatory]
                with self.assertRaises(AdmissionError):
                    self.admit()
                self.api = FakeAPI()
                artifact = next(a for a in self.api.artifacts if a['name'] == f'{prefix}-{SOURCE_ID}-1')
                self.api.artifacts.append(dict(copy.deepcopy(artifact), id=9999))
                with self.assertRaises(AdmissionError):
                    self.admit()
                self.api = FakeAPI()
                self.api.artifacts = [a for a in self.api.artifacts
                                      if a['name'] != f'{prefix}-{SOURCE_ID}-1']
                with self.assertRaises(AdmissionError):
                    self.admit()

    def test_cache_only_jobs_require_cache_and_skip_validation(self):
        for name in reuse.DESKTOP_NAMES:
            for step_name in reuse.CACHE_STEPS + reuse.VALIDATION_STEPS:
                with self.subTest(name=name, step=step_name):
                    self.api = FakeAPI()
                    current = next(j for j in self.api.main_jobs if j['name'] == name)
                    step = next(s for s in current['steps'] if s['name'] == step_name)
                    step['conclusion'] = 'failure' if step_name in reuse.CACHE_STEPS else 'success'
                    with self.assertRaises(AdmissionError):
                        reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)

    def test_desktop_cache_only_workflow_boundary(self):
        text = (Path(__file__).resolve().parents[1] / reuse.WORKFLOW_PATH).read_text()
        desktop = text.split('\n  test:\n', 1)[1].split('\n  native:\n', 1)[0]
        self.assertIn('needs: [lint, reuse]', desktop)
        for name in reuse.VALIDATION_STEPS:
            block = desktop.split('      - name: ' + name + '\n', 1)[1].split('\n      - name:', 1)[0]
            condition = next(line for line in block.splitlines() if line.startswith('        if:'))
            self.assertIn("needs.reuse.outputs.reuse != 'true'", condition)
        for name in reuse.CACHE_STEPS:
            block = desktop.split('      - name: ' + name + '\n', 1)[1].split('\n      - name:', 1)[0]
            self.assertNotIn("needs.reuse.outputs.reuse != 'true'", block)
        self.assertEqual(reuse.DESKTOP_NAMES, set(inventory.SPLIT_COMBINATION_PLATFORMS))

    def test_analyzer_requires_complete_source_controls_and_main_receipt(self):
        for step_name in reuse.ANALYZER_STEPS:
            self.api = FakeAPI()
            source = next(j for j in self.api.jobs if j['name'] in inventory.ANALYZER)
            next(s for s in source['steps'] if s['name'] == step_name)['conclusion'] = 'skipped'
            with self.assertRaises(AdmissionError):
                self.admit()
        for step_name in reuse.ANALYZER_STEPS + reuse.ANALYZER_RECEIPT_STEPS:
            self.api = FakeAPI()
            current = next(j for j in self.api.main_jobs if j['name'] in inventory.ANALYZER)
            next(s for s in current['steps'] if s['name'] == step_name)['conclusion'] = 'failure'
            with self.assertRaises(AdmissionError):
                reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)
        text = (Path(__file__).resolve().parents[1] / reuse.WORKFLOW_PATH).read_text()
        analyzer = text.split('\n  analyzer:\n', 1)[1].split('\n  complete:\n', 1)[0]
        self.assertIn('needs: reuse', analyzer)
        # Fresh queue validation analyzes the exact candidate once; main may
        # reuse only that complete execution, never a retired reference step.
        self.assertNotIn("BASELINE_REVISION", analyzer)
        self.assertNotIn("--baseline-driver", analyzer)
        self.assertNotIn("inputs.analyzer_comparison", text)
        for name in reuse.ANALYZER_STEPS:
            block = analyzer.split('      - name: ' + name + '\n', 1)[1].split('\n      - name:', 1)[0]
            self.assertIn("if: ${{ needs.reuse.outputs.reuse != 'true' }}", block)

    def test_retired_analyzer_step_names_cannot_authorize_reuse(self):
        source = next(job for job in self.api.jobs if job["name"] in inventory.ANALYZER)
        old = {"Bootstrap and identify candidate build driver": "Bootstrap candidate and select reference build driver",
               "Analyze candidate and aggregate all module shards": "Compare reference analysis and aggregate all module shards"}
        for step in source["steps"]:
            step["name"] = old.get(step["name"], step["name"])
        with self.assertRaises(AdmissionError):
            self.admit()

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
            receipt = self.admit()
            with mock.patch.dict(os.environ, environment), \
                    mock.patch.object(reuse, "local_workflow_blob", return_value=BLOB), \
                    mock.patch.object(reuse, "GitHub", side_effect=OSError("API offline")):
                with mock.patch.object(sys, "argv", ["main_ci_reuse.py", "decide", "--output", str(root / "receipt")]):
                    self.assertEqual(reuse.cli(), 0)
                self.assertEqual(output.read_text(), "reuse=false\n")
                with mock.patch.object(sys, "argv", ["main_ci_reuse.py", "finish", "--output", str(root / "receipt"),
                                                     "--expected-receipt", json.dumps(receipt),
                                                     "--expected-digest", reuse.receipt_digest(receipt)]):
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
        self.assertEqual(len(reuse.RETAINED_NAMES), 19)
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
        self.assertIn("receipt: ${{ steps.admit.outputs.receipt }}", text)
        self.assertIn("SOURCE_RECEIPT: ${{ needs.reuse.outputs.receipt }}", text)
        self.assertIn('--expected-receipt "$SOURCE_RECEIPT"', text)
        self.assertIn("steps.retain.outcome == 'success'", text)
        self.assertIn("name: Retain main CI reuse decision", text)


class MainCIReuseFinishTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeAPI()
        self.sleeps = mock.patch.object(reuse.time, "sleep").start()
        self.addCleanup(mock.patch.stopall)

    def invoke(self, phase, handoff=None, *, stale=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "result.json"
            if stale:
                path.write_text('{"status":"verified","receipt":{"stale":true}}')
            environment = {"GITHUB_REPOSITORY": reuse.REPOSITORY,
                           "GITHUB_EVENT_NAME": "push", "GITHUB_REF": "refs/heads/main",
                           "GITHUB_SHA": SHA, "GITHUB_RUN_ID": str(CURRENT_ID),
                           "GITHUB_RUN_ATTEMPT": "1", "GH_TOKEN": "fixture-secret",
                           "GITHUB_OUTPUT": str(root / "outputs"),
                           "GITHUB_STEP_SUMMARY": str(root / "summary")}
            argv = ["main_ci_reuse.py", phase, "--output", str(path)]
            if handoff is not None:
                argv += ["--expected-receipt", handoff["receipt"],
                         "--expected-digest", handoff["receipt_digest"]]
            stdout, stderr = io.StringIO(), io.StringIO()
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(reuse, "local_workflow_blob", return_value=BLOB), \
                    mock.patch.object(reuse, "GitHub", return_value=self.api), \
                    mock.patch.object(reuse, "datetime", wraps=datetime) as clock, \
                    contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                clock.now.return_value = NOW
                code = reuse.cli()
            report = json.loads(path.read_text())
            outputs = (root / "outputs").read_text() if (root / "outputs").exists() else ""
            return code, report, dict(line.split("=", 1) for line in outputs.splitlines()), \
                stdout.getvalue() + stderr.getvalue() + (root / "summary").read_text()

    def decide(self):
        code, report, handoff, _ = self.invoke("decide")
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "verified")
        self.assertEqual(handoff["reuse"], "true")
        return handoff

    def assert_refused(self, result, stage=None):
        code, report, outputs, text = result
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertNotIn("receipt", report)
        self.assertNotIn("reuse", outputs)
        self.assertNotIn("Main CI reuse verified", text)
        self.assertNotIn("Queue validation verified", text)
        if stage is not None:
            self.assertEqual(report["diagnostics"]["stage"], stage)

    def list_sequence(self, sequence):
        original = self.api.pages
        reads = iter(sequence)

        def pages(path, field, **query):
            if path == reuse.DISCOVERY_PATH:
                self.assertEqual(query, {"event": "merge_group", "head_sha": SHA})
                item = next(reads)
                if isinstance(item, Exception):
                    raise item
                return copy.deepcopy(item)
            return original(path, field, **query)
        return mock.patch.object(self.api, "pages", side_effect=pages)

    def unexpanded_jobs(self):
        self.api.main_jobs = [row for row in self.api.main_jobs if row["name"] not in reuse.REUSED_NAMES]
        for index, name in enumerate((reuse.UNEXPANDED_REUSE_NAME, reuse.UNEXPANDED_REUSE_NAME,
                                      "UEFI firmware boot")):
            row = job(name, CURRENT_ID, conclusion="skipped")
            row.update(id=4000 + index, steps=[], runner_id=None)
            self.api.main_jobs.append(row)

    def test_two_phase_handoff_binds_main_and_source(self):
        handoff = self.decide()
        receipt = json.loads(handoff["receipt"])
        self.assertEqual(receipt["main_run_id"], CURRENT_ID)
        self.assertEqual(receipt["source_run_id"], SOURCE_ID)
        self.assertEqual(receipt["source_branch"], BRANCH)
        code, report, outputs, _ = self.invoke("finish", handoff)
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "verified")
        self.assertEqual(len(report["receipt"]["main_jobs"]), 19)
        self.assertEqual(outputs, {})

    def test_transient_empty_listing_is_recollected_after_direct_lookup(self):
        handoff = self.decide()
        calls = []
        original_get = self.api.get
        def get(path, **query):
            calls.append(("get", path))
            return original_get(path, **query)
        with self.list_sequence([[], [self.api.source]]), mock.patch.object(self.api, "get", side_effect=get):
            # Capture the order without replacing the observation logic.
            pages = self.api.pages
            def listed(path, field, **query):
                calls.append(("list", path))
                return pages(path, field, **query)
            with mock.patch.object(self.api, "pages", side_effect=listed):
                code, report, _, _ = self.invoke("finish", handoff)
        self.assertEqual(code, 0)
        self.assertLess(calls.index(("get", f"actions/runs/{SOURCE_ID}")),
                        calls.index(("list", reuse.DISCOVERY_PATH)))
        snapshots = report["diagnostics"]["discovery"]
        self.assertEqual([row["candidate_count"] for row in snapshots], [0, 1])
        self.assertEqual(snapshots[0]["error"]["code"], "missing-source")
        self.sleeps.assert_called_once_with(1)

    def test_persistent_missing_list_fails_and_overwrites_stale_receipt(self):
        handoff = self.decide()
        with self.list_sequence([[], [], []]):
            result = self.invoke("finish", handoff, stale=True)
        self.assert_refused(result, "source-discovery")
        report = result[1]
        self.assertEqual(report["error"]["code"], "missing-source")
        self.assertEqual(report["expected_source_run_id"], SOURCE_ID)
        self.assertEqual(len(report["diagnostics"]["discovery"]), 3)
        self.assertEqual(self.sleeps.call_args_list, [mock.call(1), mock.call(2)])

    def test_duplicate_or_replacement_run_never_retries_or_selects_green(self):
        handoff = self.decide()
        replacement = dict(self.api.source, id=SOURCE_ID + 10)
        for rows, code in (([self.api.source, replacement], "ambiguous-source"),
                           ([replacement], "source-replaced"),
                           ([self.api.source, self.api.source], "ambiguous-source")):
            with self.subTest(code=code), self.list_sequence([rows]):
                result = self.invoke("finish", handoff)
                self.assert_refused(result, "source-discovery")
                self.assertEqual(result[1]["error"]["code"], code)
        self.sleeps.assert_not_called()

    def test_source_attempt_failure_and_identity_changes_refuse_before_discovery(self):
        for field, value in (("run_attempt", 2), ("run_attempt", True),
                             ("status", "in_progress"), ("conclusion", "failure"),
                             ("conclusion", "cancelled"), ("head_sha", "d" * 40),
                             ("workflow_id", 99), ("event", "push"),
                             ("head_branch", BRANCH + "-replaced"),
                             ("updated_at", NOW.isoformat())):
            with self.subTest(field=field, value=value):
                self.api = FakeAPI()
                handoff = self.decide()
                self.api.source[field] = value
                with mock.patch.object(self.api, "pages", wraps=self.api.pages) as pages:
                    self.assert_refused(self.invoke("finish", handoff), "bound-source")
                    pages.assert_not_called()
        self.sleeps.assert_not_called()

    def test_moving_pagination_and_transient_http_reads_retry_but_definite_errors_do_not(self):
        handoff = self.decide()
        errors = [AdmissionError("API response is truncated or changed during pagination"),
                  urllib.error.HTTPError("https://api.github.com", 502, "Bad Gateway", {}, None),
                  urllib.error.HTTPError("https://api.github.com", 429, "Limited", {}, None),
                  urllib.error.URLError("temporary connection loss")]
        for error in errors:
            with self.subTest(error=error), self.list_sequence([error, [self.api.source]]):
                code, report, _, _ = self.invoke("finish", handoff)
                self.assertEqual(code, 0)
                self.assertIn("error", report["diagnostics"]["discovery"][0])
        self.sleeps.reset_mock()
        for error in (urllib.error.HTTPError("https://api.github.com", 403, "Forbidden", {}, None),
                      AdmissionError("API pagination limit reached; refusing partial evidence")):
            with self.subTest(error=error), self.list_sequence([error]):
                self.assert_refused(self.invoke("finish", handoff), "source-discovery")
        self.sleeps.assert_not_called()

    def test_malformed_or_wrong_identity_scoped_listing_fails(self):
        handoff = self.decide()
        for rows in ([None], {}, [dict(self.api.source, workflow_id=42)],
                     [dict(self.api.source, head_sha="d" * 40)]):
            with self.subTest(rows=rows), self.list_sequence([rows]):
                self.assert_refused(self.invoke("finish", handoff), "source-discovery")
        self.sleeps.assert_not_called()

    def test_attempt_movement_during_backoff_does_not_get_another_chance(self):
        handoff = self.decide()
        self.sleeps.side_effect = lambda delay: self.api.source.update(run_attempt=2)
        with self.list_sequence([[], [self.api.source]]):
            result = self.invoke("finish", handoff)
        self.assert_refused(result, "bound-source")
        self.assertEqual(len(result[1]["diagnostics"]["discovery"]), 1)
        self.assertEqual(result[1]["error"]["code"], "changed-attempt")
        self.assertEqual(result[1]["diagnostics"]["source_run"]["run_attempt"], 2)

    def test_missing_malformed_and_tampered_handoff_fails_without_api(self):
        handoff = self.decide()
        for broken in (None, dict(handoff, receipt="{}"), dict(handoff, receipt="not-json"),
                       dict(handoff, receipt_digest="fixture-secret"),
                       dict(handoff, receipt="x" * (reuse.MAX_RECEIPT_BYTES + 1))):
            with self.subTest(broken=broken is None), mock.patch.object(self.api, "get") as get:
                result = self.invoke("finish", broken)
                self.assert_refused(result, "receipt-handoff")
                self.assertNotIn("fixture-secret", json.dumps(result[1]) + result[3])
                get.assert_not_called()

    def test_handoff_cannot_cross_main_runs_attempts_workflow_or_source(self):
        handoff = self.decide()
        receipt = json.loads(handoff["receipt"])
        for field, value in (("main_run_id", CURRENT_ID + 1), ("main_attempt", 2),
                             ("source_run_id", True), ("source_attempt", True),
                             ("policy", "other-policy"), ("repository", "other/repo"),
                             ("workflow_id", 44), ("workflow_blob", "d" * 40),
                             ("head_sha", "e" * 40)):
            changed = dict(receipt, **{field: value})
            invalid = {"receipt": json.dumps(changed), "receipt_digest": reuse.receipt_digest(changed)}
            with self.subTest(field=field), mock.patch.object(self.api, "get") as get:
                self.assert_refused(self.invoke("finish", invalid), "receipt-handoff")
                get.assert_not_called()

    def test_changed_artifact_or_job_id_cannot_match_original_digest(self):
        for kind in ("artifact", "job"):
            self.api = FakeAPI()
            handoff = self.decide()
            if kind == "artifact":
                self.api.artifacts[0]["digest"] = "sha256:" + "d" * 64
            else:
                next(row for row in self.api.jobs if row["name"] in reuse.REUSED_NAMES)["id"] += 9000
            with self.subTest(kind=kind):
                self.assert_refused(self.invoke("finish", handoff), "receipt-comparison")

    def test_missing_expired_or_misbound_artifacts_remain_red(self):
        for field, value in (("expired", True), ("size_in_bytes", 0),
                             ("digest", "bad"), ("expires_at", NOW.isoformat())):
            self.api = FakeAPI()
            handoff = self.decide()
            self.api.artifacts[0][field] = value
            with self.subTest(field=field):
                self.assert_refused(self.invoke("finish", handoff), "source-artifacts")
        self.api = FakeAPI()
        handoff = self.decide()
        self.api.artifacts.pop()
        self.assert_refused(self.invoke("finish", handoff), "source-artifacts")

    def test_failed_source_or_main_jobs_emit_failure_not_success_receipts(self):
        for target in ("jobs", "main_jobs"):
            for conclusion in ("failure", "cancelled", "skipped"):
                self.api = FakeAPI()
                handoff = self.decide()
                getattr(self.api, target)[0]["conclusion"] = conclusion
                with self.subTest(target=target, conclusion=conclusion):
                    self.assert_refused(self.invoke("finish", handoff),
                                        "source-jobs" if target == "jobs" else "main-jobs")

    def test_attempt_moves_during_main_collection(self):
        handoff = self.decide()
        original = self.api.pages
        def pages(path, field, **query):
            result = original(path, field, **query)
            if path == f"actions/runs/{CURRENT_ID}/jobs":
                self.api.source["run_attempt"] = 2
            return result
        with mock.patch.object(self.api, "pages", side_effect=pages):
            self.assert_refused(self.invoke("finish", handoff), "final-source-recheck")

    def test_decision_without_proof_falls_back_and_retains_diagnostics(self):
        with self.list_sequence([[], [], []]):
            code, report, outputs, _ = self.invoke("decide")
        self.assertEqual(code, 0)
        self.assertEqual(outputs, {"reuse": "false"})
        self.assertEqual(report["status"], "unavailable")
        self.assertNotIn("receipt", report)
        self.assertEqual(report["error"]["code"], "missing-source")
        self.assertEqual(len(report["diagnostics"]["discovery"]), 3)
        self.assertEqual(self.sleeps.call_args_list, [mock.call(1), mock.call(2)])

    def test_exception_token_is_redacted_from_diagnostic_and_log(self):
        handoff = self.decide()
        with self.list_sequence([OSError("transport fixture-secret\\nwith untrusted text")]):
            result = self.invoke("finish", handoff)
        self.assert_refused(result)
        self.assertNotIn("fixture-secret", json.dumps(result[1]) + result[3])
        self.assertIn("[redacted]", result[1]["error"]["message"])

    def test_real_unexpanded_matrices_are_skips_not_duplicate_executions(self):
        handoff = self.decide()
        self.unexpanded_jobs()
        code, report, _, _ = self.invoke("finish", handoff)
        self.assertEqual(code, 0)
        self.assertEqual(len(report["receipt"]["main_jobs"]), 19)
        skipped = report["diagnostics"]["skipped_jobs"]
        self.assertEqual([row["job_id"] for row in skipped], [4000, 4001, 4002])
        self.assertTrue(all(row["conclusion"] == "skipped" for row in skipped))

    def test_missing_duplicate_or_unrecognized_skip_groups_fail(self):
        for case in ("missing", "duplicate", "unknown", "none", "duplicate-id"):
            self.api = FakeAPI()
            handoff = self.decide()
            self.unexpanded_jobs()
            if case == "missing":
                self.api.main_jobs.pop()
            elif case == "duplicate":
                self.api.main_jobs.append(dict(self.api.main_jobs[-2], id=5000))
            elif case == "unknown":
                self.api.main_jobs[-2]["name"] = "${{ matrix.unknown }}"
            elif case == "none":
                self.api.main_jobs = self.api.main_jobs[:-3]
            else:
                self.api.main_jobs[-2]["id"] = self.api.main_jobs[0]["id"]
            with self.subTest(case=case):
                self.assert_refused(self.invoke("finish", handoff), "main-jobs")

    def test_skipped_rows_must_not_have_run_or_belong_to_another_attempt(self):
        for field, value in (("head_sha", "d" * 40), ("run_id", 900), ("run_attempt", 2),
                             ("run_attempt", True), ("conclusion", "success"),
                             ("status", "in_progress"), ("steps", [{"name": "ran"}]),
                             ("runner_id", 123)):
            self.api = FakeAPI()
            handoff = self.decide()
            self.unexpanded_jobs()
            self.api.main_jobs[-2][field] = value
            with self.subTest(field=field):
                self.assert_refused(self.invoke("finish", handoff), "main-jobs")


    def assert_fallback(self, result):
        code, report, outputs, text = result
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "unavailable")
        self.assertEqual(outputs, {"reuse": "false"})
        self.assertNotIn("receipt", report)
        self.assertNotIn("Main CI reuse verified", text)

    def test_decision_recollection_preserves_the_full_proof(self):
        # Replay the previous one-read policy and the new policy on the same
        # absent-then-complete observation. This is not a hosted timing claim.
        with mock.patch.object(reuse, "DISCOVERY_DELAYS", ()), self.list_sequence([[]]):
            self.assert_fallback(self.invoke("decide"))
        self.sleeps.assert_not_called()
        with self.list_sequence([[], [self.api.source]]):
            code, report, outputs, _ = self.invoke("decide")
        self.assertEqual(code, 0)
        self.assertEqual(outputs["reuse"], "true")
        self.assertEqual(report["status"], "verified")
        self.assertEqual(len(report["receipt"]["source_jobs"]), 25)
        self.assertEqual(len(report["receipt"]["source_artifacts"]), 25)
        self.assertEqual(len(report["diagnostics"]["discovery"]), 2)
        self.sleeps.assert_called_once_with(1)

    def test_decision_recovers_only_inconclusive_discovery_reads(self):
        errors = (AdmissionError("API response is truncated or changed during pagination"),
                  reuse.APIReadError(reuse.DISCOVERY_PATH, 429, 4),
                  reuse.APIReadError(reuse.DISCOVERY_PATH, 503, 4),
                  reuse.APIReadError(reuse.DISCOVERY_PATH, None, 4))
        for error in errors:
            self.sleeps.reset_mock()
            with self.subTest(error=str(error)), self.list_sequence([error, [self.api.source]]):
                code, report, outputs, _ = self.invoke("decide")
            self.assertEqual(code, 0)
            self.assertEqual(outputs["reuse"], "true")
            self.assertEqual(len(report["diagnostics"]["discovery"]), 2)
            self.sleeps.assert_called_once_with(1)

    def test_decision_definite_rejections_do_not_retry(self):
        cases = ([self.api.source, self.api.source], [None], {},
                 [dict(self.api.source, head_sha="d" * 40)],
                 [dict(self.api.source, conclusion="failure")],
                 [dict(self.api.source, conclusion="cancelled")],
                 [dict(self.api.source, status="in_progress")],
                 [dict(self.api.source, run_attempt=2)],
                 AdmissionError("API pagination limit reached; refusing partial evidence"),
                 reuse.APIReadError(reuse.DISCOVERY_PATH, 403, 1),
                 reuse.APIReadError(reuse.DISCOVERY_PATH, 404, 1))
        for rows in cases:
            with self.subTest(rows=rows), self.list_sequence([rows]):
                self.assert_fallback(self.invoke("decide"))
        self.sleeps.assert_not_called()

    def test_decision_recollection_cannot_admit_late_stale_or_changed_source(self):
        for field, value in (("updated_at", NOW.isoformat()),
                             ("updated_at", (NOW - timedelta(hours=3)).isoformat()),
                             ("head_sha", "d" * 40), ("workflow_id", 99),
                             ("conclusion", "cancelled"), ("run_attempt", 2)):
            self.api = FakeAPI()
            self.sleeps.reset_mock()
            self.api.source[field] = value
            with self.subTest(field=field, value=value), self.list_sequence([[], [self.api.source]]):
                self.assert_fallback(self.invoke("decide"))
            self.sleeps.assert_called_once_with(1)

    def test_decision_run_movement_during_backoff_refuses_positive_outputs(self):
        for field, value in (("head_sha", "d" * 40), ("run_attempt", 2),
                             ("event", "workflow_dispatch")):
            self.api = FakeAPI()
            self.sleeps.reset_mock()
            self.sleeps.side_effect = lambda delay: self.api.current.update({field: value})
            with self.subTest(field=field), self.list_sequence([[], [self.api.source]]):
                self.assert_fallback(self.invoke("decide"))
            self.sleeps.assert_called_once_with(1)
        self.sleeps.side_effect = None

    def test_decision_recovered_listing_does_not_retry_missing_coverage(self):
        for missing in ("job", "step", "artifact", "workflow"):
            self.api = FakeAPI()
            self.sleeps.reset_mock()
            if missing == "job":
                self.api.jobs.pop()
            elif missing == "step":
                native = next(row for row in self.api.jobs if row["name"] == "Linux x86-64 native")
                native["steps"] = [row for row in native["steps"] if
                                   row["name"] != "Native configuration differential matrix"]
            elif missing == "artifact":
                self.api.artifacts.pop()
            else:
                original = self.api.get
                def get(path, **query):
                    value = original(path, **query)
                    if path == "contents/" + reuse.WORKFLOW_PATH:
                        value["sha"] = "d" * 40
                    return value
                self.api.get = get
            with self.subTest(missing=missing), self.list_sequence([[], [self.api.source]]):
                self.assert_fallback(self.invoke("decide"))
            self.sleeps.assert_called_once_with(1)

    def test_decision_exhausted_wrapped_reads_fall_back_with_http_diagnostic(self):
        errors = [reuse.APIReadError(reuse.DISCOVERY_PATH, 503, 4) for _ in range(3)]
        with self.list_sequence(errors):
            result = self.invoke("decide")
        self.assert_fallback(result)
        self.assertEqual(result[1]["error"]["http_status"], 503)
        self.assertEqual(len(result[1]["diagnostics"]["discovery"]), 3)
        self.assertEqual(self.sleeps.call_args_list, [mock.call(1), mock.call(2)])

    def test_production_reader_wrapping_is_recollected_in_both_phases(self):
        # Exercise GitHub.pages -> get -> github_read_json -> APIReadError,
        # rather than substituting a raw HTTP error that production wraps.
        for phase in ("decide", "finish"):
            self.api = FakeAPI()
            self.sleeps.reset_mock()
            handoff = self.decide() if phase == "finish" else None
            real = GitHub(reuse.REPOSITORY, "fixture-secret")
            original = self.api.pages
            def pages(path, field, **query):
                if path == reuse.DISCOVERY_PATH:
                    return real.pages(path, field, **query)
                return original(path, field, **query)
            response = mock.MagicMock()
            response.__enter__.return_value.read.return_value = json.dumps(
                {"total_count": 1, "workflow_runs": [self.api.source]}).encode()
            errors = [urllib.error.HTTPError("https://api.github.com/fixture", 503,
                                             "Unavailable", {}, None) for _ in range(4)]
            with self.subTest(phase=phase), mock.patch.object(self.api, "pages", side_effect=pages), \
                    mock.patch("urllib.request.urlopen", side_effect=errors + [response]) as transport:
                code, report, outputs, text = self.invoke(phase, handoff)
            self.assertEqual(code, 0, text)
            self.assertEqual(report["status"], "verified")
            self.assertEqual(transport.call_count, 5)
            self.assertEqual(report["diagnostics"]["discovery"][0]["error"]["http_status"], 503)
            self.assertEqual(len(report["diagnostics"]["discovery"]), 2)
            self.assertEqual(self.sleeps.call_args_list,
                             [mock.call(1), mock.call(2), mock.call(4), mock.call(1)])


if __name__ == "__main__":
    unittest.main()
