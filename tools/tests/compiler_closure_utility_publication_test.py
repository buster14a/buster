#!/usr/bin/env python3
"""Bounded Utility publication controls; hosted data fixtures never authorize a physical run."""
import base64
import copy
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench_direct"))
import compiler_publish as publisher

OWNER = {"login": "davidgmbb", "id": 39247043}
REPOSITORY = "buster14a/buster"
REVISION, HEAD = "a" * 40, "b" * 40
START = 1760000000000000


def execution():
    return {"id": 200, "run_attempt": 1, "path": publisher.BENCH_WORKFLOW, "event": "workflow_run",
            "head_branch": "main", "head_sha": REVISION, "repository": {"full_name": REPOSITORY},
            "head_repository": {"full_name": REPOSITORY}, "actor": OWNER, "triggering_actor": OWNER,
            "display_title": f"9700X request 100.1 head {HEAD}"}


def job(kind="utility"):
    return {"id": 300, "run_id": 200, "run_attempt": 1, "head_sha": REVISION, "name": {
        "utility": publisher.UTILITY_HOST_JOB, "sampling": publisher.SAMPLING_HOST_JOB,
        "preparation": publisher.PREPARATION_HOST_JOB}[kind], "status": "in_progress", "conclusion": None,
        "runner_id": 400, "runner_name": "approved-worker",
        "labels": ["self-hosted", "Linux", "X64", "buster-zen5", "ryzen-9700x"],
        "started_at": "2025-10-09T08:53:20Z"}


def environment(root, kind="utility"):
    return {"BQ_PHYSICAL_CLOCK_KIND": kind, "GITHUB_REPOSITORY": REPOSITORY, "GITHUB_RUN_ID": "200",
            "GITHUB_RUN_ATTEMPT": "1", "GITHUB_SHA": REVISION, "GITHUB_JOB": kind,
            "RUNNER_NAME": "approved-worker", "BQ_REQUEST_RUN_ID": "100", "BQ_HEAD_COMMIT": HEAD,
            "RUNNER_TEMP": str(root), "GITHUB_ENV": str(root / "github.env"), "GH_TOKEN": "must-not-be-used"}


class FakeApi:
    def __init__(self, run=None, jobs=None):
        self.run, self.jobs = execution() if run is None else run, [job()] if jobs is None else jobs

    def request(self, path):
        if path != "/actions/runs/200":
            raise AssertionError("unexpected public request: " + path)
        return self.run

    def pages(self, path, field):
        if path != "/actions/runs/200/attempts/1/jobs" or field != "jobs":
            raise AssertionError("unexpected public job listing")
        return self.jobs


class PhysicalClockTests(unittest.TestCase):
    def call(self, root, kind="utility", run=None, rows=None, env_patch=None, times=None, elapsed=100000000):
        env = environment(root, kind)
        env.update(env_patch or {})
        fake = FakeApi(run, [job(kind)] if rows is None else rows)
        with patch.object(publisher, "Api", return_value=fake) as constructor, \
                patch("time.time_ns", side_effect=times or [(START + 10000000) * 1000, (START + 10100000) * 1000]), \
                patch("time.monotonic_ns", side_effect=[1000000000, 1000000000 + elapsed]):
            code = publisher.physical_clock_data(env)
            constructor.assert_called_once_with(REPOSITORY, "", response_limit=128 * 1024)
        return code, env

    def test_all_three_routes_emit_exact_tokenless_clock_record(self):
        for kind in ("sampling", "preparation", "utility"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                code, env = self.call(root, kind)
                self.assertEqual(code, 0)
                raw = (root / "compiler-physical-job-clock.tsv").read_bytes()
                row = publisher.sampling_tsv(raw)
                self.assertEqual(len(row), 18)
                self.assertEqual(row["schema"], "buster-compiler-physical-job-clock-v1")
                self.assertEqual(row["kind"], kind)
                self.assertEqual(int(row["started_unix_us"]), START)
                self.assertEqual(int(row["start_lower_unix_us"]), START - 1000000)
                self.assertEqual(row["observer_monotonic_elapsed_us"], "100000")
                emitted = dict(line.split("=", 1) for line in (root / "github.env").read_text().splitlines())
                self.assertEqual(base64.b64decode(emitted["BQ_PHYSICAL_JOB_DATA"], validate=True), raw)
                self.assertEqual(emitted["BQ_PHYSICAL_JOB_DATA_FILE"], str(root / "compiler-physical-job-clock.tsv"))
                self.assertNotIn("must-not-be-used", raw.decode())

    def test_actual_executor_provenance_refused_before_output(self):
        mutations = {"id": 201, "run_attempt": 2, "path": "wrong.yml", "event": "workflow_dispatch",
                     "head_branch": "other", "head_sha": "c" * 40, "actor": {"login": "davidgmbb", "id": 1},
                     "triggering_actor": {"login": "other", "id": 39247043},
                     "head_repository": {"full_name": "other/repo"}, "display_title": "copied old title"}
        for key, value in mutations.items():
            with self.subTest(key=key), tempfile.TemporaryDirectory() as temporary:
                run = execution()
                run[key] = value
                root = Path(temporary)
                with self.assertRaises(ValueError):
                    self.call(root, run=run)
                self.assertFalse((root / "compiler-physical-job-clock.tsv").exists())

    def test_active_job_runner_start_and_attempt_are_actual_api_facts(self):
        mutations = {"id": True, "run_id": 201, "run_attempt": "1", "head_sha": "c" * 40,
                     "status": "completed", "conclusion": "success", "runner_id": 0, "runner_name": "other",
                     "labels": ["self-hosted"], "started_at": "unavailable"}
        for key, value in mutations.items():
            with self.subTest(key=key), tempfile.TemporaryDirectory() as temporary:
                row = job()
                row[key] = value
                with self.assertRaises(ValueError):
                    self.call(Path(temporary), rows=[row])

    def test_duplicate_platform_job_is_not_resolved_to_one(self):
        with tempfile.TemporaryDirectory() as temporary, self.assertRaises(ValueError):
            self.call(Path(temporary), rows=[job(), job()])

    def test_missing_string_boolean_float_and_rerun_attempts_are_refused(self):
        for attempt in (None, "1", True, 1.0, 0, -1, 2):
            with self.subTest(attempt=attempt), tempfile.TemporaryDirectory() as temporary:
                run = execution()
                run["run_attempt"] = attempt
                with self.assertRaises(ValueError):
                    self.call(Path(temporary), run=run)

    def test_clock_jump_stale_and_long_observer_refuse_before_native_admission(self):
        controls = [([(START + 10000000) * 1000, (START + 12100000) * 1000], 100000000),
                    ([(START + 5401000000) * 1000, (START + 5401100000) * 1000], 100000000),
                    ([(START + 10000000) * 1000, (START + 131000000) * 1000], 121000000000)]
        for times, elapsed in controls:
            with self.subTest(times=times), tempfile.TemporaryDirectory() as temporary, self.assertRaises(ValueError):
                self.call(Path(temporary), times=times, elapsed=elapsed)

    def test_fixed_clock_record_cannot_be_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.call(root)
            raw = (root / "compiler-physical-job-clock.tsv").read_bytes()
            with self.assertRaises(FileExistsError):
                self.call(root)
            self.assertEqual((root / "compiler-physical-job-clock.tsv").read_bytes(), raw)


class UtilityCostTests(unittest.TestCase):
    def test_all_physical_residual_is_charged_to_snapshot(self):
        result = publisher.utility_net_observation(3000000, 1000000, 5000000)
        self.assertEqual(result["physical_residual_us"], 1000000)
        self.assertEqual(result["residual_charged_to_legacy_us"], 0)
        self.assertEqual(result["snapshot_charged_us"], 2000000)
        self.assertTrue(result["criterion_met"])
        self.assertIsNone(result["hosted_api_publication_us"])
        self.assertFalse(result["general_workload_savings_assessed"])

    def test_complete_negative_and_equal_cost_remain_measured_cost_data(self):
        for total in (6000000, 7000000):
            result = publisher.utility_net_observation(3000000, 1000000, total)
            self.assertFalse(result["criterion_met"])
            self.assertEqual(result["snapshot_charged_us"], total - 3000000)

    def test_unknown_zero_bool_negative_overflow_or_inconsistent_clocks_refuse(self):
        for values in ((None, 1, 3), (0, 1, 3), (True, 1, 3), (-1, 1, 3), (1 << 64, 1, 3),
                       (3, 2, 4), (1, 1, 5400000001)):
            with self.subTest(values=values), self.assertRaises(ValueError):
                publisher.utility_net_observation(*values)


class UtilityManifestTests(unittest.TestCase):
    def fixture(self):
        members = {"ordinary/receipt.json": b"{}", "lab/pairs.json": b"[]"}
        rows = ["BUSTER_COMPILER_CLOSURE_UTILITY_EXPORT_V1", "D\tordinary\t-\t0\t493", "D\tlab\t-\t0\t448"]
        rows += [f"F\t{name}\t{hashlib.sha256(raw).hexdigest()}\t{len(raw)}\t384" for name, raw in members.items()]
        return ("\n".join(rows) + "\n").encode(), members

    def test_native_inventory_covers_exact_regular_members_and_directories(self):
        raw, members = self.fixture()
        result = publisher.utility_export_manifest(raw, members)
        self.assertEqual(set(result), {"ordinary", "lab", *members})

    def test_hash_size_mode_missing_extra_duplicate_and_path_tampering_refuse(self):
        raw, members = self.fixture()
        changed = [
            (raw.replace(hashlib.sha256(b"{}").hexdigest().encode(), b"a" * 64), members),
            (raw.replace(b"\t2\t384", b"\t3\t384", 1), members),
            (raw.replace(b"\t384", b"\t493", 1), members),
            (raw, {"ordinary/receipt.json": b"{}"}),
            (raw, dict(members, extra=b"")),
            (raw + raw.splitlines()[-1] + b"\n", members),
            (raw.replace(b"ordinary/receipt.json", b"../receipt.json"), members),
            (raw.replace(b"D\tlab\t-\t0\t448\n", b""), members)]
        for manifest, data in changed:
            with self.subTest(manifest=manifest), self.assertRaises(ValueError):
                publisher.utility_export_manifest(manifest, data)


if __name__ == "__main__":
    unittest.main()
