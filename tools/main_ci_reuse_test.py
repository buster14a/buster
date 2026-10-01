#!/usr/bin/env python3
"""Network-free source and current-run tests for main CI reuse."""

import copy
from contextlib import redirect_stderr, redirect_stdout
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
    return {"id": len(name) + run_id, "name": name, "run_id": run_id,
            "run_attempt": 1, "head_sha": SHA, "status": status,
            "conclusion": conclusion,
            "steps": None if conclusion == "skipped" else
                     [{"name": step, "status": "completed", "conclusion": "success"}
                      for step in sorted(required)]}


class FakeAPI:
    def __init__(self):
        self.current = run(CURRENT_ID, "push", "main", NOW - timedelta(minutes=1))
        self.source = run(SOURCE_ID, "merge_group", BRANCH, NOW - timedelta(minutes=2))
        self.jobs = [job(name, SOURCE_ID) for name in inventory.COMBINATION_JOBS]
        for index, record in enumerate(self.jobs):
            record["id"] = 1000 + index
        self.artifacts = []
        for index, (_, prefix, _) in enumerate(reuse.REUSED):
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
        self.main_jobs.append(job(inventory.MAIN_REUSE_JOB, CURRENT_ID))
        self.main_jobs += [job(name, CURRENT_ID, conclusion="skipped") for name in reuse.REUSED_NAMES]
        for index, record in enumerate(self.main_jobs):
            record["id"] = 2000 + index
        self.movement = False
        self.repository_rows = None
        self.scoped_rows = None
        self.scoped_snapshots = []
        self.calls = []
        self.reads = []

    def get(self, path, **query):
        self.reads.append(path)
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
        self.calls.append((path, field, query))
        if path in ("actions/runs", f"actions/workflows/{reuse.WORKFLOW_ID}/runs"):
            assert field == "workflow_runs" and query == {"event": "merge_group", "head_sha": SHA}
            rows = self.repository_rows if path == "actions/runs" else self.scoped_rows
            if path != "actions/runs" and self.scoped_snapshots:
                rows = self.scoped_snapshots.pop(0)
            return copy.deepcopy([self.source] if rows is None else rows)
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
        sleeper = mock.patch.object(reuse.time, "sleep")
        self.sleep = sleeper.start()
        self.addCleanup(sleeper.stop)

    def admit(self):
        return reuse.verify_source(self.api, SHA, CURRENT_ID, BLOB, NOW)

    def test_exact_commit_source_and_main_specific_jobs(self):
        receipt = self.admit()
        self.assertEqual(len(receipt["source_jobs"]), 8)
        self.assertEqual(len(receipt["source_artifacts"]), 8)
        self.assertEqual(receipt["source_run_id"], SOURCE_ID)
        self.assertEqual(len(reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)), 13)
        self.assertRegex(reuse.receipt_digest(receipt), r"[0-9a-f]{64}\Z")

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

    def test_api_uncertainty_and_incomplete_pagination_fall_back(self):
        with mock.patch.object(self.api, "pages", side_effect=OSError("API unavailable")):
            with self.assertRaises(OSError):
                self.admit()
        with mock.patch.object(self.api, "pages", side_effect=AdmissionError("incomplete pagination")):
            with self.assertRaisesRegex(AdmissionError, "pagination"):
                self.admit()

    def test_decision_falls_back_but_finish_fails_closed_on_api_error(self):
        proof = self.admit()
        with mock.patch.object(reuse, "GitHub", side_effect=OSError("API offline")):
            status, report, outputs, _ = self.call_cli("decide", patch_api=False)
            self.assertEqual(status, 0)
            self.assertEqual(outputs, {"reuse": "false"})
            self.assertEqual(report["error"]["kind"], "api-or-io-error")
            status, report, _, _ = self.call_cli("finish", proof, patch_api=False)
            self.assertEqual(status, 1)
            self.assertEqual(report["status"], "rejected")
            self.assertNotIn("receipt", report)

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
        self.assertEqual(len(reuse.RETAINED_NAMES), 13)
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

    def call_cli(self, phase, proof=None, *, text=None, digest=None, patch_api=True, attempt="1"):
        # Separate directories model separate Actions jobs, not shared temp files.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            environment = dict(GITHUB_REPOSITORY=reuse.REPOSITORY, GITHUB_EVENT_NAME="push",
                               GITHUB_REF="refs/heads/main", GITHUB_SHA=SHA,
                               GITHUB_RUN_ID=str(CURRENT_ID), GITHUB_RUN_ATTEMPT=attempt,
                               GH_TOKEN="fixture-token-not-a-real-secret",
                               GITHUB_OUTPUT=str(root / "outputs"),
                               GITHUB_STEP_SUMMARY=str(root / "summary"),
                               SOURCE_RECEIPT=text if text is not None else
                               (json.dumps(proof) if proof is not None else ""))
            argv = ["main_ci_reuse.py", phase, "--output", str(root / "result.json")]
            if phase == "finish":
                argv += ["--expected-digest", digest if digest is not None else
                         (reuse.receipt_digest(proof) if proof is not None else "")]
            console = io.StringIO()
            with mock.patch.dict(os.environ, environment), mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(reuse, "local_workflow_blob", return_value=BLOB), \
                    mock.patch.object(reuse, "datetime", wraps=datetime) as clock, \
                    redirect_stdout(console), redirect_stderr(console):
                clock.now.return_value = NOW
                if patch_api:
                    with mock.patch.object(reuse, "GitHub", return_value=self.api):
                        status = reuse.cli()
                else:
                    status = reuse.cli()
            report = json.loads((root / "result.json").read_text())
            outputs = dict(line.split("=", 1) for line in (root / "outputs").read_text().splitlines()) \
                if (root / "outputs").exists() else {}
            console.write((root / "summary").read_text())
        return status, report, outputs, console.getvalue()

    def assert_rejected(self, result, *, stage=None, kind=None):
        status, report, outputs, console = result
        self.assertEqual(status, 1)
        self.assertEqual(report["schema"], reuse.RESULT_SCHEMA)
        self.assertEqual(report["status"], "rejected")
        self.assertNotIn("receipt", report)
        self.assertNotEqual(outputs.get("reuse"), "true")
        self.assertNotIn("Queue validation reused", console)
        if stage:
            self.assertEqual(report["observations"]["stage"], stage)
        if kind:
            self.assertEqual(report["error"]["kind"], kind)

    def unexpanded_main_jobs(self):
        self.api.main_jobs = [row for row in self.api.main_jobs if row["name"] not in reuse.REUSED_NAMES]
        # Exact API name observed twice in incident job inventory 36835320572.
        names = [reuse.SKIPPED_MATRIX_NAME, reuse.SKIPPED_MATRIX_NAME, "UEFI firmware boot"]
        for index, name in enumerate(names):
            row = job(name, CURRENT_ID, conclusion="skipped")
            row.update(id=3000 + index, steps=None, runner_id=0, runner_name="")
            self.api.main_jobs.append(row)

    def test_two_job_roundtrip_binds_source_without_repository_rediscovery(self):
        status, decision, outputs, _ = self.call_cli("decide")
        self.assertEqual(status, 0)
        self.assertEqual(outputs["reuse"], "true")
        proof = json.loads(outputs["receipt"])
        self.assertEqual(proof, decision["receipt"])
        self.assertEqual(proof["main_run_id"], CURRENT_ID)
        self.assertEqual(proof["main_attempt"], 1)
        self.assertEqual(proof["source_run_id"], SOURCE_ID)
        self.assertEqual(proof["source_branch"], BRANCH)
        for repository_rows in ([], [self.api.source, self.api.source]):
            with self.subTest(repository_rows=len(repository_rows)):
                self.api.repository_rows = repository_rows
                self.api.calls.clear()
                self.unexpanded_main_jobs()
                # Reset the full fixture before replacing the native/mobile groups again.
                result = self.call_cli("finish", text=outputs["receipt"], digest=outputs["receipt_digest"])
                self.assertEqual(result[0], 0, result[3])
                self.assertEqual(result[1]["status"], "verified")
                self.assertEqual(len(result[1]["receipt"]["main_jobs"]), 13)
                self.assertEqual(len(result[1]["observations"]["main_skipped_jobs"]), 3)
                self.assertNotIn("actions/runs", [call[0] for call in self.api.calls])
                self.api = FakeAPI()

    def test_scoped_lookup_recovers_an_empty_snapshot_with_bounded_retry(self):
        proof = self.admit()
        self.api.scoped_snapshots = [[], [self.api.source]]
        status, report, _, console = self.call_cli("finish", proof)
        self.assertEqual(status, 0, console)
        self.sleep.assert_called_once_with(1)
        self.assertEqual([row["candidate_count"] for row in report["observations"]["queue_lookups"]], [0, 1, 1])

    def test_persistently_empty_scoped_lookup_never_uses_cached_success(self):
        proof = self.admit()
        self.api.scoped_rows = []
        result = self.call_cli("finish", proof)
        self.assert_rejected(result, stage="source-uniqueness", kind="missing-queue-run")
        self.assertEqual(self.sleep.call_args_list, [mock.call(1), mock.call(2)])
        self.assertEqual(len(result[1]["observations"]["queue_lookups"]), reuse.LOOKUP_ATTEMPTS)
        self.assertEqual(result[1]["expected_source_run_id"], SOURCE_ID)
        self.assertEqual(result[1]["expected_digest"], reuse.receipt_digest(proof))

    def test_ambiguous_replaced_and_malformed_lists_are_not_retried_to_green(self):
        for shape in ("duplicate", "another", "replacement", "malformed", "wrong-workflow"):
            with self.subTest(shape=shape):
                self.api = FakeAPI()
                proof = self.admit()
                other = dict(self.api.source, id=SOURCE_ID + 20)
                self.api.scoped_rows = {
                    "duplicate": [self.api.source, self.api.source],
                    "another": [self.api.source, other],
                    "replacement": [other], "malformed": [None],
                    "wrong-workflow": [dict(self.api.source, workflow_id=9)],
                }[shape]
                self.assert_rejected(self.call_cli("finish", proof), stage="source-uniqueness")
        self.sleep.assert_not_called()

    def test_handoff_rejects_missing_corrupt_oversized_and_duplicate_json(self):
        proof = self.admit()
        valid = json.dumps(proof)
        cases = ("", "{", "[]", " " * (reuse.MAX_RECEIPT_BYTES + 1),
                 valid[:-1] + ', "source_run_id": 100}')
        for text in cases:
            with self.subTest(text=text[:32]):
                self.assert_rejected(self.call_cli("finish", proof, text=text), stage="receipt-handoff")
        self.assert_rejected(self.call_cli("finish", proof, digest="0" * 64), stage="receipt-handoff")
        self.assert_rejected(self.call_cli("finish", proof, digest="not-a-digest"), stage="receipt-handoff")

    def test_receipt_cannot_be_replayed_for_another_context_or_source(self):
        proof = self.admit()
        changes = {"schema": "old", "policy": "another-policy", "repository": "other/repo",
                   "head_sha": "d" * 40, "main_run_id": CURRENT_ID + 1, "main_attempt": 2,
                   "workflow_id": 1, "workflow_path": ".github/workflows/other.yml",
                   "workflow_blob": "d" * 40, "source_run_id": True, "source_attempt": 2,
                   "source_branch": "main", "source_completed_at": "invalid"}
        for key, value in changes.items():
            with self.subTest(key=key):
                changed = dict(proof, **{key: value})
                # Even a matching hash cannot bypass the independently checked context.
                self.assert_rejected(self.call_cli("finish", changed), stage="receipt-handoff")

    def test_bound_source_is_revalidated_not_trusted_from_receipt(self):
        changes = {"run_attempt": 2, "status": "in_progress", "conclusion": "failure",
                   "head_sha": "d" * 40, "workflow_id": 1, "head_branch": BRANCH + "-changed",
                   "repository": {"id": reuse.REPOSITORY_ID, "full_name": "another/repo"},
                   "updated_at": NOW.isoformat()}
        for key, value in changes.items():
            with self.subTest(key=key):
                self.api = FakeAPI()
                proof = self.admit()
                self.api.source[key] = value
                self.assert_rejected(self.call_cli("finish", proof), stage="source-run")

    def test_changed_jobs_and_artifacts_fail_after_a_positive_decision(self):
        for shape in ("red-job", "missing-job", "missing-step", "job-id", "missing-artifact",
                      "expired-artifact", "artifact-id", "artifact-digest"):
            with self.subTest(shape=shape):
                self.api = FakeAPI()
                proof = self.admit()
                native = next(row for row in self.api.jobs if row["name"] == "Linux x86-64 native")
                if shape == "red-job":
                    native["conclusion"] = "failure"
                elif shape == "missing-job":
                    self.api.jobs.remove(native)
                elif shape == "missing-step":
                    native["steps"] = []
                elif shape == "job-id":
                    native["id"] = 98765
                elif shape == "missing-artifact":
                    self.api.artifacts.pop()
                elif shape == "expired-artifact":
                    self.api.artifacts[0]["expired"] = True
                elif shape == "artifact-id":
                    self.api.artifacts[0]["id"] = 98765
                else:
                    self.api.artifacts[0]["digest"] = "sha256:" + "d" * 64
                self.assert_rejected(self.call_cli("finish", proof))

    def test_failed_current_work_never_produces_a_positive_receipt_or_summary(self):
        for shape in ("failure", "missing", "wrong-head", "rerun", "reuse-failed"):
            with self.subTest(shape=shape):
                self.api = FakeAPI()
                proof = self.admit()
                if shape == "failure":
                    self.api.main_jobs[0]["conclusion"] = "failure"
                elif shape == "missing":
                    self.api.main_jobs.pop(0)
                elif shape == "wrong-head":
                    self.api.main_jobs[0]["head_sha"] = "d" * 40
                elif shape == "rerun":
                    self.api.main_jobs[0]["run_attempt"] = 2
                else:
                    next(row for row in self.api.main_jobs if row["name"] == inventory.MAIN_REUSE_JOB)["conclusion"] = "failure"
                self.assert_rejected(self.call_cli("finish", proof), stage="main-jobs")

    def test_source_rerun_during_main_collection_remains_red(self):
        proof = self.admit()
        original = reuse.verify_current_jobs
        def moved(*args, **kwargs):
            result = original(*args, **kwargs)
            self.api.movement = True
            return result
        with mock.patch.object(reuse, "verify_current_jobs", side_effect=moved):
            self.assert_rejected(self.call_cli("finish", proof), stage="source-run")

    def test_list_http_failure_retains_request_context_without_secrets(self):
        proof = self.admit()
        pages = self.api.pages
        token = "fixture-token-not-a-real-secret"
        def failed(path, field, **query):
            if path.startswith("actions/workflows/"):
                raise urllib.error.HTTPError("https://example.invalid/?token=" + token, 503,
                                             token, {}, io.BytesIO(token.encode()))
            return pages(path, field, **query)
        with mock.patch.object(self.api, "pages", side_effect=failed):
            result = self.call_cli("finish", proof)
        self.assert_rejected(result, stage="source-uniqueness", kind="api-http-error")
        self.assertIn("HTTP 503", result[1]["error"]["reason"])
        self.assertEqual(result[1]["observations"]["queue_lookups"][0]["path"],
                         f"actions/workflows/{reuse.WORKFLOW_ID}/runs")
        self.assertNotIn(token, json.dumps(result))

    def test_result_write_failure_cannot_export_a_reuse_decision(self):
        original = reuse.write_result
        calls = 0
        def fail_success(path, report):
            nonlocal calls
            calls += 1
            if report["status"] == "verified":
                raise OSError("injected write failure")
            original(path, report)
        with mock.patch.object(reuse, "write_result", side_effect=fail_success):
            status, report, outputs, console = self.call_cli("decide")
        self.assertEqual(status, 0)
        self.assertEqual(report["status"], "unavailable")
        self.assertEqual(outputs, {"reuse": "false"})
        self.assertNotIn("Queue validation reused", console)
        self.assertEqual(calls, 3)

    def test_explicit_rerun_keeps_full_validation(self):
        proof = self.admit()
        status, report, outputs, _ = self.call_cli("decide", attempt="2")
        self.assertEqual(status, 0)
        self.assertEqual(report["status"], "unavailable")
        self.assertEqual(outputs, {"reuse": "false"})
        self.assert_rejected(self.call_cli("finish", proof, attempt="2"), stage="context")

    def test_unexpanded_groups_have_exact_skip_identity_and_no_execution(self):
        self.unexpanded_main_jobs()
        observed = {}
        self.assertEqual(len(reuse.verify_current_jobs(self.api, SHA, CURRENT_ID,
                                                       observations=observed)), 13)
        self.assertEqual([row["id"] for row in observed["main_skipped_jobs"]], [3000, 3001, 3002])
        for shape in ("missing", "extra", "mixed", "runner", "steps", "failed", "wrong-head", "wrong-attempt", "duplicate-id"):
            with self.subTest(shape=shape):
                self.api = FakeAPI()
                self.unexpanded_main_jobs()
                row = self.api.main_jobs[-3]
                if shape == "missing":
                    self.api.main_jobs.remove(row)
                elif shape == "extra":
                    self.api.main_jobs.append(dict(row, id=4000))
                elif shape == "mixed":
                    self.api.main_jobs.append(dict(row, id=4000, name="Linux x86-64 native"))
                elif shape == "runner":
                    row["runner_id"] = 1
                elif shape == "steps":
                    row["steps"] = [{"name": "Set up job", "conclusion": "success"}]
                elif shape == "failed":
                    row["conclusion"] = "failure"
                elif shape == "wrong-head":
                    row["head_sha"] = "d" * 40
                elif shape == "wrong-attempt":
                    row["run_attempt"] = 2
                else:
                    row["id"] = self.api.main_jobs[-2]["id"]
                with self.assertRaises(AdmissionError):
                    reuse.verify_current_jobs(self.api, SHA, CURRENT_ID)

    def test_workflow_transports_and_retains_both_phase_results(self):
        text = (Path(__file__).resolve().parents[1] / reuse.WORKFLOW_PATH).read_text()
        self.assertIn("receipt: ${{ steps.admit.outputs.receipt }}", text)
        self.assertIn("SOURCE_RECEIPT: ${{ needs.reuse.outputs.receipt }}", text)
        self.assertNotIn("--expected-receipt ${{", text)
        self.assertIn("name: Retain main CI reuse decision\n        if: ${{ always() }}", text)
        self.assertIn("name: main-ci-reuse-${{ github.run_id }}-${{ github.run_attempt }}", text)
        aggregate = text.split("      - name: Retain desktop partition inventory", 1)[1]
        aggregate = aggregate.split("      - name: Require every shard", 1)[0]
        self.assertIn("if: ${{ always() }}", aggregate)
        self.assertIn("main-ci-reuse-finish.json", aggregate)
        self.assertIn("if-no-files-found: error", aggregate)


if __name__ == "__main__":
    unittest.main()
