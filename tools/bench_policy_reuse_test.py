#!/usr/bin/env python3
"""Offline evidence, fallback/finalization and workflow-wiring tests for #2462."""

from copy import deepcopy
from contextlib import redirect_stderr, redirect_stdout
import fnmatch
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest import mock

import bench_policy_reuse as reuse

ROOT = Path(__file__).resolve().parents[1]
SHA = "a" * 40
MAIN_ID, SOURCE_ID, JOB_ID = 202, 101, 303
WORKFLOW = (ROOT / reuse.WORKFLOW_PATH).read_bytes()


def run_record(run_id, event, branch, created, updated):
    repository = {"id": reuse.REPOSITORY_ID, "full_name": reuse.REPOSITORY}
    return {"id": run_id, "workflow_id": reuse.WORKFLOW_ID, "path": reuse.WORKFLOW_PATH,
            "repository": dict(repository), "head_repository": dict(repository),
            "head_sha": SHA, "head_commit": {"id": SHA}, "event": event,
            "head_branch": branch, "run_attempt": 1, "created_at": created,
            "updated_at": updated, "status": "completed", "conclusion": "success"}


class FakeApi:
    def __init__(self):
        self.main = run_record(MAIN_ID, "push", "main", "2026-10-03T10:46:55Z", "2026-10-03T10:46:55Z")
        self.main.update(status="in_progress", conclusion=None)
        self.source = run_record(SOURCE_ID, "merge_group", "gh-readonly-queue/main/pr-2268-example",
                                 "2026-10-03T10:20:56Z", "2026-10-03T10:21:50Z")
        names = ("Set up job", "Machine specifications", "Checkout", "Record actual checkout identity",
                 "Prove stateless validation survives merge bursts") + reuse.REQUIRED_STEPS + (reuse.FINISH_STEP, "Complete job")
        self.job = {"id": JOB_ID, "run_id": SOURCE_ID, "run_attempt": 1,
                    "head_sha": SHA, "head_branch": self.source["head_branch"],
                    "workflow_name": reuse.JOB_NAME, "name": reuse.JOB_NAME,
                    "status": "completed", "conclusion": "success", "runner_id": 123,
                    "labels": ["ubuntu-latest"], "started_at": "2026-10-03T10:20:59Z",
                    "completed_at": "2026-10-03T10:21:50Z", "steps": [
                        {"name": name, "number": i + 1, "status": "completed",
                         "conclusion": "skipped" if name == reuse.FINISH_STEP else "success"}
                        for i, name in enumerate(names)]}
        self.runs = {"total_count": 1, "workflow_runs": [self.source]}
        self.jobs = {"total_count": 1, "jobs": [self.job]}
        self.blob = {"type": "file", "path": reuse.WORKFLOW_PATH, "size": len(WORKFLOW),
                     "sha": hashlib.sha1(b"blob " + str(len(WORKFLOW)).encode() + b"\0" + WORKFLOW).hexdigest()}
        self.calls = []
        self.hook = None

    def get(self, path, **query):
        self.calls.append((path, query))
        if self.hook:
            self.hook(self, path, len(self.calls))
        if path == f"actions/runs/{MAIN_ID}":
            value = self.main
        elif path == f"actions/runs/{SOURCE_ID}":
            value = self.source
        elif path == f"actions/workflows/{reuse.WORKFLOW_ID}/runs":
            assert query == dict(head_sha=SHA, event="merge_group", per_page=100, page=1)
            value = self.runs
        elif path == f"actions/runs/{SOURCE_ID}/attempts/1/jobs":
            assert query == dict(per_page=100, page=1)
            value = self.jobs
        elif path == "contents/" + reuse.WORKFLOW_PATH:
            assert query == {"ref": SHA}
            value = self.blob
        else:
            raise AssertionError("unexpected endpoint: " + path)
        return deepcopy(value)


class EvidenceTests(unittest.TestCase):
    def verify(self, api):
        return reuse.verify(api, SHA, MAIN_ID, WORKFLOW)

    def assert_refused(self, api):
        with self.assertRaises((reuse.ReuseRefused, TypeError, ValueError, AttributeError)):
            self.verify(api)

    def test_complete_exact_source_binds_all_identity_and_execution(self):
        api = FakeApi()
        receipt = self.verify(api)
        self.assertEqual(receipt["source"]["run_id"], SOURCE_ID)
        self.assertEqual(receipt["main_run_id"], MAIN_ID)
        self.assertEqual(receipt["sha"], SHA)
        self.assertEqual(receipt["job"]["id"], JOB_ID)
        self.assertEqual(receipt["workflow_blob"], api.blob["sha"])
        self.assertEqual(len(api.calls), 8)
        self.assertEqual(len(receipt["job"]["steps"]), len(api.job["steps"]))

    def test_planner_bookkeeping_never_replaces_policy_execution(self):
        api = FakeApi()
        planner = dict(api.job, id=404, name="No-code plan / Classify no-code changes")
        api.jobs = {"total_count": 2, "jobs": [planner, api.job]}
        self.assertEqual(self.verify(api)["job"]["id"], JOB_ID)
        for key, value in (("status", "queued"), ("conclusion", "failure"),
                           ("head_sha", "b" * 40), ("run_attempt", 2), ("runner_id", 0),
                           ("name", "unexpected")):
            broken = deepcopy(api)
            broken.jobs["jobs"][0][key] = value
            with self.subTest(planner=key):
                self.assert_refused(broken)
        omitted = deepcopy(api)
        omitted.jobs["jobs"][1].update(conclusion="skipped", runner_id=0, steps=[])
        self.assert_refused(omitted)
        for step in reuse.WORK_STEPS:
            broken = deepcopy(api)
            next(row for row in broken.jobs["jobs"][1]["steps"] if row["name"] == step)["conclusion"] = "skipped"
            self.assert_refused(broken)

    def test_wrong_run_identity(self):
        changes = {"id": 404, "workflow_id": 404, "path": ".github/workflows/ci.yml",
                   "head_sha": "b" * 40, "head_commit": {"id": "b" * 40},
                   "repository": {"id": 404, "full_name": reuse.REPOSITORY},
                   "head_repository": {"id": reuse.REPOSITORY_ID, "full_name": "other/fork"},
                   "run_attempt": 2, "event": "workflow_dispatch", "head_branch": "other"}
        for side in ("main", "source"):
            for key, value in changes.items():
                with self.subTest(side=side, key=key):
                    api = FakeApi()
                    if side == "source" and key == "id":
                        api.hook = lambda current, path, n: current.source.update(id=404) if n == 3 else None
                    else:
                        getattr(api, side)[key] = value
                    self.assert_refused(api)

    def test_boolean_attempt_is_not_attempt_one(self):
        for side in ("main", "source"):
            api = FakeApi()
            getattr(api, side)["run_attempt"] = True
            self.assert_refused(api)

    def test_nonqueue_or_empty_queue_suffix_refused(self):
        for branch in ("main", "gh-readonly-queue/develop/pr-1-x", "gh-readonly-queue/main/", None):
            api = FakeApi()
            api.source["head_branch"] = branch
            self.assert_refused(api)

    def test_failed_cancelled_skipped_or_pending_source_refused(self):
        for conclusion in ("failure", "cancelled", "skipped", "neutral", "timed_out", None):
            api = FakeApi()
            api.source["conclusion"] = conclusion
            self.assert_refused(api)
        api = FakeApi()
        api.source["status"] = "in_progress"
        self.assert_refused(api)

    def test_main_is_still_active(self):
        for status in ("completed", "queued", "waiting", None):
            api = FakeApi()
            api.main["status"] = status
            self.assert_refused(api)

    def test_source_must_precede_main_and_not_be_stale(self):
        for date in ("2026-10-03T10:46:56Z", "2026-10-03T08:46:54Z", "2026-10-03T10:21:50", "bad", None):
            api = FakeApi()
            api.source["updated_at"] = date
            self.assert_refused(api)

    def test_two_hour_boundary_is_inclusive(self):
        api = FakeApi()
        api.main["created_at"] = "2026-10-03T12:21:50Z"
        self.verify(api)

    def test_missing_duplicate_truncated_or_malformed_listings(self):
        for field in ("runs", "jobs"):
            for count, rows in ((0, []), (1, []), (2, [1]), (True, [1]), (101, [1]), (1, [None]), (1, [1, 1])):
                api = FakeApi()
                key = "workflow_runs" if field == "runs" else "jobs"
                record = api.source if field == "runs" else api.job
                setattr(api, field, {"total_count": count, key: [record if row == 1 else row for row in rows]})
                with self.subTest(field=field, count=count, rows=rows):
                    self.assert_refused(api)

    def test_changed_workflow_blob_type_path_or_size(self):
        for key, value in (("sha", "b" * 40), ("path", "other.yml"), ("size", 0), ("type", "symlink")):
            api = FakeApi()
            api.blob[key] = value
            self.assert_refused(api)

    def test_wrong_job_identity_or_unallocated_runner(self):
        for key, value in (("id", True), ("run_id", 9), ("run_attempt", 2), ("run_attempt", True),
                           ("head_sha", "b" * 40), ("head_branch", "main"), ("name", "other"),
                           ("workflow_name", "other"), ("runner_id", 0), ("labels", ["self-hosted"]),
                           ("status", "queued"), ("conclusion", "cancelled")):
            api = FakeApi()
            api.job[key] = value
            with self.subTest(key=key, value=value):
                self.assert_refused(api)

    def test_invalid_job_execution_interval(self):
        for key, value in (("started_at", "2026-10-03T10:20:55Z"),
                           ("completed_at", "2026-10-03T10:21:51Z"),
                           ("completed_at", "2026-10-03T10:20:58Z")):
            api = FakeApi()
            api.job[key] = value
            self.assert_refused(api)

    def test_every_required_step_must_be_present_and_fresh(self):
        for name in reuse.REQUIRED_STEPS:
            api = FakeApi()
            api.job["steps"] = [step for step in api.job["steps"] if step["name"] != name]
            with self.subTest(name=name, case="missing"):
                self.assert_refused(api)
            for conclusion in ("skipped", "failure", "cancelled", None):
                api = FakeApi()
                next(step for step in api.job["steps"] if step["name"] == name)["conclusion"] = conclusion
                with self.subTest(name=name, conclusion=conclusion):
                    self.assert_refused(api)

    def test_added_declared_control_must_be_present_and_fresh(self):
        # Independent of REQUIRED_STEPS: new named workflow controls cannot
        # disappear from the source API snapshot while its job remains green.
        name = "Prove stateless validation survives merge bursts"
        for conclusion in ("missing", "skipped", "failure", "cancelled"):
            api = FakeApi()
            if conclusion == "missing":
                api.job["steps"] = [step for step in api.job["steps"] if step["name"] != name]
            else:
                next(step for step in api.job["steps"] if step["name"] == name)["conclusion"] = conclusion
            with self.subTest(conclusion=conclusion):
                self.assert_refused(api)

    def test_named_step_inventory_rejects_missing_or_duplicate_contract(self):
        for data in (WORKFLOW.replace(b"      - name: " + reuse.CONTROL_STEP.encode(), b"      - name: Other"),
                     WORKFLOW + b"      - name: " + reuse.CONTROL_STEP.encode() + b"\n"):
            with self.assertRaises(reuse.ReuseRefused):
                reuse.verify(FakeApi(), SHA, MAIN_ID, data)

    def test_no_reuse_of_reused_policy_source(self):
        api = FakeApi()
        next(step for step in api.job["steps"] if step["name"] == reuse.FINISH_STEP)["conclusion"] = "success"
        self.assert_refused(api)

    def test_duplicate_step_names_or_numbers_refused(self):
        for key in ("name", "number"):
            api = FakeApi()
            api.job["steps"][1][key] = api.job["steps"][0][key]
            self.assert_refused(api)

    def test_unexpected_failed_or_incomplete_step_refused(self):
        for status, conclusion in (("completed", "skipped"), ("completed", "failure"), ("in_progress", "success")):
            api = FakeApi()
            api.job["steps"].append(dict(name="New policy test", number=100, status=status, conclusion=conclusion))
            self.assert_refused(api)

    def test_source_movement_during_collection_refused(self):
        for call in (3, 6, 7):
            api = FakeApi()
            api.hook = lambda current, path, n, when=call: current.source.update(run_attempt=2) if n == when else None
            self.assert_refused(api)

    def test_competing_run_during_collection_refused(self):
        api = FakeApi()
        api.hook = lambda current, path, n: current.runs.update(total_count=2) if n == 6 else None
        self.assert_refused(api)

    def test_main_movement_during_collection_refused(self):
        for change in ({"run_attempt": 2}, {"status": "completed"}, {"created_at": "2026-10-03T10:46:56Z"}):
            api = FakeApi()
            api.hook = lambda current, path, n, value=change: current.main.update(value) if n == 8 else None
            self.assert_refused(api)

    def test_transport_failure_cannot_return_receipt(self):
        api = FakeApi()
        with mock.patch.object(api, "get", side_effect=OSError("offline")):
            with self.assertRaises(OSError):
                self.verify(api)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.path = self.root / "receipt.json"
        self.output = self.root / "output"
        self.summary = self.root / "summary"
        self.environment = {"GITHUB_SERVER_URL": "https://github.com", "GITHUB_REPOSITORY": reuse.REPOSITORY,
                            "GITHUB_EVENT_NAME": "push", "GITHUB_REF": "refs/heads/main",
                            "GITHUB_RUN_ATTEMPT": "1", "GITHUB_RUN_ID": str(MAIN_ID), "GITHUB_SHA": SHA,
                            "GITHUB_OUTPUT": str(self.output), "GITHUB_STEP_SUMMARY": str(self.summary)}
        self.receipt = reuse.verify(FakeApi(), SHA, MAIN_ID, WORKFLOW)

    def invoke(self, phase, receipt=None, error=None):
        with mock.patch.dict(os.environ, self.environment, clear=True), \
                mock.patch.object(reuse, "collect", return_value=receipt or self.receipt, side_effect=error) as collect, \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = reuse.cli([phase, "--receipt", str(self.path)])
        return result, collect.call_count

    def test_positive_decision_and_finish_recheck(self):
        self.assertEqual(self.invoke("decide"), (0, 1))
        self.assertEqual(self.output.read_text(), "reused=true\n")
        self.assertFalse(self.summary.exists())
        self.assertEqual(self.invoke("finish"), (0, 1))
        self.assertIn(f"/runs/{SOURCE_ID}/attempts/1", self.summary.read_text())
        self.assertIn("not again on main", self.summary.read_text())

    def test_pr_queue_dispatch_rerun_foreign_contexts_stay_fresh_without_api(self):
        for key, values in (("GITHUB_EVENT_NAME", ("pull_request", "merge_group", "workflow_dispatch", "schedule")),
                            ("GITHUB_RUN_ATTEMPT", ("2", "", "01")), ("GITHUB_REF", ("refs/tags/v1", "refs/heads/other")),
                            ("GITHUB_SERVER_URL", ("https://other.invalid",)), ("GITHUB_REPOSITORY", ("other/fork",))):
            old = self.environment[key]
            for value in values:
                self.environment[key] = value
                with self.subTest(key=key, value=value):
                    self.assertEqual(self.invoke("decide"), (0, 0))
                    self.assertEqual(json.loads(self.path.read_text())["mode"], "fresh")
            self.environment[key] = old
        self.assertNotIn("reused=true", self.output.read_text())

    def test_uncertainty_falls_back_without_retaining_secrets(self):
        for error in (OSError("secret-token"), reuse.ReuseRefused("missing source"), ValueError("malformed")):
            self.assertEqual(self.invoke("decide", error=error), (0, 1))
            self.assertEqual(json.loads(self.path.read_text())["mode"], "fresh")
            self.assertNotIn("secret-token", self.path.read_text())
        self.assertNotIn("reused=true", self.output.read_text())

    def test_failed_receipt_write_never_enables_reuse(self):
        self.path = self.root / "missing-parent" / "receipt"
        self.assertEqual(self.invoke("decide")[0], 1)
        self.assertFalse(self.output.exists())

    def test_finish_refuses_changed_job_source_or_main(self):
        self.invoke("decide")
        for key in ("job", "source", "main_run_id", "workflow_blob", "sha"):
            changed = deepcopy(self.receipt)
            changed[key] = "changed"
            with self.subTest(key=key):
                self.assertEqual(self.invoke("finish", receipt=changed)[0], 1)
                self.assertFalse(self.summary.exists())

    def test_finish_api_failure_is_fatal_not_fallback(self):
        self.invoke("decide")
        self.assertEqual(self.invoke("finish", error=OSError("offline"))[0], 1)
        self.assertFalse(self.summary.exists())

    def test_finish_missing_malformed_oversized_and_tampered_receipts(self):
        self.assertEqual(self.invoke("finish")[0], 1)
        for data in ("{}", "null", "invalid", "x" * (reuse.MAX_RECEIPT_BYTES + 1)):
            self.path.write_text(data)
            self.assertEqual(self.invoke("finish")[0], 1)
        self.invoke("decide")
        report = json.loads(self.path.read_text())
        report["sha256"] = "0" * 64
        self.path.write_text(json.dumps(report))
        self.assertEqual(self.invoke("finish")[0], 1)
        self.assertFalse(self.summary.exists())

    def test_finish_rejects_rerun_before_reading_api(self):
        self.invoke("decide")
        self.environment["GITHUB_RUN_ATTEMPT"] = "2"
        self.assertEqual(self.invoke("finish"), (1, 0))

    def test_collect_binds_actual_checkout_and_workflow(self):
        (self.root / reuse.WORKFLOW_PATH).parent.mkdir(parents=True)
        (self.root / reuse.WORKFLOW_PATH).write_bytes(WORKFLOW)
        with mock.patch.dict(os.environ, self.environment, clear=True), \
                mock.patch.object(reuse, "make_api", return_value=FakeApi()), \
                mock.patch.object(subprocess, "run", return_value=mock.Mock(stdout=SHA + "\n")):
            self.assertEqual(reuse.collect(self.root), self.receipt)
        with mock.patch.dict(os.environ, self.environment, clear=True), \
                mock.patch.object(reuse, "make_api") as api, \
                mock.patch.object(subprocess, "run", return_value=mock.Mock(stdout="b" * 40 + "\n")):
            with self.assertRaises(reuse.ReuseRefused):
                reuse.collect(self.root)
            api.assert_not_called()


class WorkflowTests(unittest.TestCase):
    def test_existing_job_required_triggers_and_read_only_permissions(self):
        text = WORKFLOW.decode()
        self.assertIn("  pull_request:\n", text)
        self.assertIn("  merge_group:\n    types: [checks_requested]\n", text)
        self.assertIn("  push:\n    branches: [main]\n", text)
        self.assertIn("  workflow_dispatch:\n", text)
        self.assertNotRegex(text, r"(?m)^\s+(paths|paths-ignore):")
        self.assertEqual(re.findall(r"(?m)^  (\w+):$", text.split("\njobs:\n")[1]), ["no_code_plan", "policy"])
        self.assertIn("    name: " + reuse.JOB_NAME + "\n", text)
        self.assertIn("    permissions:\n      contents: read\n      actions: read\n", text)
        self.assertNotIn(": write", text)
        self.assertNotIn("continue-on-error:", text)
        self.assertIn("    timeout-minutes: 5\n", text)
        self.assertIn("          persist-credentials: false\n", text)

    def test_all_and_only_original_work_steps_are_gated(self):
        text = WORKFLOW.decode()
        blocks = {}
        for block in re.split(r"(?m)^      - ", text)[1:]:
            first = block.splitlines()[0]
            if first.startswith("name: "):
                name = first.removeprefix("name: ")
                self.assertNotIn(name, blocks)
                blocks[name] = block
        gated = [name for name, block in blocks.items()
                 if "        if: ${{ steps.reuse.outputs.reused != 'true' }}\n" in block]
        self.assertEqual(gated, list(reuse.WORK_STEPS))
        expected = set(reuse.REQUIRED_STEPS) | {reuse.FINISH_STEP}
        self.assertTrue(expected <= set(blocks))
        self.assertIn("Prove stateless validation survives merge bursts", blocks)
        for name in set(blocks) - set(reuse.WORK_STEPS) - {reuse.FINISH_STEP}:
            self.assertNotIn("        if:", blocks[name])
        self.assertIn("exit 1", blocks["Require CI admission to be enabled"])
        self.assertIn("python3 -B tools/bench_policy_reuse_test.py -v", blocks[reuse.CONTROL_STEP])
        self.assertIn("        id: reuse\n", blocks[reuse.DECISION_STEP])
        self.assertIn(" decide --receipt \"$RUNNER_TEMP/bench-policy-reuse.json\"", blocks[reuse.DECISION_STEP])
        self.assertIn("        if: ${{ steps.reuse.outputs.reused == 'true' }}\n", blocks[reuse.FINISH_STEP])
        self.assertIn(" finish --receipt \"$RUNNER_TEMP/bench-policy-reuse.json\"", blocks[reuse.FINISH_STEP])
        self.assertEqual(list(blocks)[-1], reuse.FINISH_STEP)

    def test_reconciler_filters_upstream_branch_not_its_own_main_ref(self):
        text = (ROOT / ".github/workflows/merge-queue-reconcile.yml").read_text()
        events = text.split("\non:\n", 1)[1].split("\npermissions:", 1)[0]
        upstream = events.split("  workflow_run:\n", 1)[1].split("  push:\n", 1)[0]
        self.assertIn("    branches: ['gh-readonly-queue/main/**']\n", upstream)
        self.assertIn("    types: [completed]\n", upstream)
        self.assertEqual(re.findall(r"(?m)^      - (.+)$", upstream), [
            "Buster CI", "TCC bootstrap", "GPU toolchain acceptance",
            "Benchmark service workflow policy", "API migration policy", "Native retirement rebinding"])
        pattern = "gh-readonly-queue/main/**"
        for branch, expected in (("gh-readonly-queue/main/pr-1-a", True), ("gh-readonly-queue/main/pr-2-b/c", True),
                                 ("main", False), ("codex/2462-benchmark-policy-reuse", False),
                                 ("gh-readonly-queue/develop/pr-1-a", False)):
            self.assertEqual(fnmatch.fnmatchcase(branch, pattern), expected)
        for marker in ("  push:\n    branches: [main]\n", "  schedule:\n", "  workflow_dispatch:\n"):
            self.assertIn(marker, events)
        for marker in ("github.event.workflow_run.event == 'merge_group'", "          ref: main\n",
                       "  cancel-in-progress: false\n", "python3 -B tools/merge_queue_admission.py reconcile",
                       "    timeout-minutes: 10\n"):
            self.assertIn(marker, text)


if __name__ == "__main__":
    unittest.main()
