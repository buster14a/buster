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



def encode(value):
    import json
    return (json.dumps(value, sort_keys=True, allow_nan=False) + "\n").encode()


def ordinary_series_fixture():
    from sampling_qualification_receipt import _lab
    plan = {"source_root": "/tmp/utility-source", "output_root": "/tmp/utility-output", "baseline_revision": "d" * 40}
    prefix, leg = "utility/legacy/lab/", "legacy"
    binaries = {"baseline": {"sha256": "1" * 64, "size_bytes": 100000},
                "candidate": {"sha256": "2" * 64, "size_bytes": 100001}}
    command = _lab.shell_join(["IDE"] + _lab.DEFAULT_COMPILE + ["-o", "OUT"])
    config = {"command": command, "repo_root": plan["source_root"], "cpu": 2, "perf": "perf", "pairs": None,
              "target_minutes": 10.0, "warmups": 1, "seed": 20261003, "profile_steps": [], "sudo": False,
              "require_identical_output": False, "extra": [], "canonical_inline_pair": False,
              "extra_by_variant": {"a": [], "b": []}, "fresh_copy": True, "min_effect_percent": 0.5}
    sampling = {"pairs": 10, "order": "ABBA", "fresh_copy": True, "reason": "--target-minutes 10: fixture fixed before results"}
    steps = {key: {"status": "ok"} for key in ("env", "prepare", "timed")}
    variants, summary_variants, files = {}, {}, {}
    for key, role, name in (("a", "baseline", "ide-base"), ("b", "candidate", "ide-cand")):
        path = plan["output_root"] + "/legacy-work/bin/" + name
        binary = binaries[role]
        variants[key] = {"role": role, "ide": path, "sha256": binary["sha256"], "size_bytes": binary["size_bytes"]}
        summary_variants[role] = {"path": path, "sha256": binary["sha256"], "size_bytes": binary["size_bytes"],
                                  "runs": 10, "failed": 0, "identical_runs": 10, "deterministic": True}
        meta = {"config": {"command": _lab.shell_join([path] + _lab.DEFAULT_COMPILE + ["-o", "OUT"]),
                          "cpu": 2, "perf": "perf", "repo_root": plan["source_root"], "ide": path,
                          "role": role, "extra": [], "fresh_copy": True},
                "capabilities": {"perf_stat": {"usable": False, "reason": "fixture unavailable"}},
                "collection": {"metrics_out": False}}
        files[prefix + key + "/lab.json"] = encode(meta)
        for name in ("commands.log", "wrapper-rss.log", "source-run.log", "metrics-probe.log", "warmup-0.log"):
            files[prefix + key + "/" + name] = b""
    files[prefix + "a/env/env.json"] = b"{}\n"
    records, loaded = [], []
    # Vary A/B order effect deliberately: warning flags are report-only.
    for number in range(1, 11):
        order = "AB" if number % 2 else "BA"
        for variant in order.lower():
            span = 0.01 if variant == "a" else 0.0103 if number % 2 else 0.01025
            record = {"pair": number, "order": order, "variant": variant, "exit": 0, "identical": True,
                      "span_s": span, "cpu_s": span / 2, "maxrss_bytes": None, "harness_rss_bytes": None,
                      "wrapper_rss_bytes": None, "counters": False}
            records.append(record)
            loaded.append(dict(record, values={}, lines=[], metrics={"header": {}, "inputs": []}, task_s=span / 2, cc_wall_s=None))
    paired = _lab.complete_pairs(loaded)
    metrics = {key: dict(_lab.compare_series([(pair["metrics_a"][key], pair["metrics_b"][key]) for pair in paired],
               unit, direction, 20261003, time_metric=unit == "s", floor=_lab.metric_floor(key, 0.5)), label=label)
               for key, unit, direction, label in _lab.COMPARE_METRICS}
    phase_metrics = {"enabled": False, "reason": "fixture unavailable"}
    raw = {"version": 1, "mode": "compare", "config": config, "plan": sampling, "steps": steps,
           "variants": variants, "phase_metrics": phase_metrics}
    summary = {"schema": "buster-uarch-lab-compare-v2", "plan": dict(sampling, seed=20261003, confidence=0.95,
               bootstrap_resamples=2000, complete_pairs=10, fresh_copy=True), "cpu": 2, "command": command,
               "repo_root": plan["source_root"], "method": _lab.COMPARE_METHOD,
               "steps": {key: "ok" for key in steps},
               "host": {"cpu_model": "AMD Ryzen 7 9700X 8-Core Processor", "git_revision": plan["baseline_revision"]},
               **summary_variants, "metrics": metrics, "phases": None, "checks": _lab.compare_checks(paired),
               "verdict": _lab.compare_verdict(metrics, None, 0.5), "outputs_identical": False,
               "phase_metrics": phase_metrics, "counters": {"perf_stat": False, "reason": "fixture unavailable"},
               "warnings": ["fixture perf unavailable; explanatory order/drift flags remain report-only"],
               "diagnostic_fixture": True}
    files.update({prefix + "compare.json": encode(raw), prefix + "summary.json": encode(summary),
                  prefix + "pairs.json": encode(records)})
    return files, prefix, plan, leg, binaries


class UtilitySeriesTests(unittest.TestCase):
    def test_complete_detected_slowdown_flags_cross_arm_difference_and_na_are_report_only(self):
        args = ordinary_series_fixture()
        result = publisher.utility_series_replay(*args)
        self.assertEqual(result["verdict"]["outcome"], "slower")
        self.assertEqual(result["complete_pairs"], 10)
        self.assertFalse(result["outputs_identical"])
        self.assertFalse(result["counters"]["perf_stat"])
        self.assertIsNone(result["phases"])
        self.assertTrue(any(row.get("flag") for row in result["checks"].values()))

    def test_raw_profile_inference_counts_binary_and_pair_tampering_are_incomplete(self):
        import json
        for name, mutate in (
                ("compare.json", lambda row: row["config"].update(pairs=40)),
                ("compare.json", lambda row: row["config"].update(require_identical_output=True)),
                ("compare.json", lambda row: row["config"].update(seed=1)),
                ("compare.json", lambda row: row["variants"]["a"].update(sha256="9" * 64)),
                ("summary.json", lambda row: row["plan"].update(bootstrap_resamples=1)),
                ("summary.json", lambda row: row["verdict"].update(outcome="faster")),
                ("summary.json", lambda row: row["metrics"]["wall"].update(ratio=0.99)),
                ("pairs.json", lambda rows: rows.pop()),
                ("pairs.json", lambda rows: rows[0].update(exit=1)),
                ("pairs.json", lambda rows: rows[0].update(identical=False)),
                ("pairs.json", lambda rows: rows[0].update(span_s=0)),
                ("pairs.json", lambda rows: rows[0].update(cpu_s=True))):
            with self.subTest(name=name, mutate=mutate):
                args = ordinary_series_fixture()
                files, prefix, unused_plan, unused_leg, unused_binaries = args
                row = json.loads(files[prefix + name])
                mutate(row)
                files[prefix + name] = encode(row)
                with self.assertRaises(ValueError):
                    publisher.utility_series_replay(*args)

    def test_claimed_optional_counter_or_compiler_metrics_requires_real_raw_file(self):
        import json
        args = ordinary_series_fixture()
        files, prefix, unused_plan, unused_leg, unused_binaries = args
        meta = json.loads(files[prefix + "a/lab.json"])
        meta["collection"]["metrics_out"] = True
        files[prefix + "a/lab.json"] = encode(meta)
        with self.assertRaises(ValueError):
            publisher.utility_series_replay(*args)

    def test_hosted_diagnostic_marker_can_never_enter_production_publication(self):
        files, unused_prefix, unused_plan, unused_leg, unused_binaries = ordinary_series_fixture()
        with self.assertRaisesRegex(ValueError, "diagnostic"):
            publisher.utility_validate(None, {}, files)


class UtilityAuthorityTests(unittest.TestCase):
    def env(self):
        return {"BQ_REPOSITORY": REPOSITORY, "BQ_HEAD_COMMIT": HEAD, "BQ_REQUEST_RUN_ID": "100", "BQ_RUN_ID": "200",
                "BQ_RUN_ATTEMPT": "1", "BQ_REQUEST_ATTEMPT": "1", "GITHUB_RUN_ID": "200", "GITHUB_RUN_ATTEMPT": "1",
                "GITHUB_REPOSITORY": REPOSITORY, "GITHUB_SHA": REVISION, "GH_TOKEN": "hosted-only"}

    def test_executor_owner_repo_workflow_join_is_checked_before_request_verification(self):
        import authorize
        mutations = {"id": 201, "run_attempt": 2, "path": "wrong.yml", "event": "workflow_dispatch",
                     "head_branch": "other", "head_sha": "c" * 40, "actor": {"login": "davidgmbb", "id": 1},
                     "triggering_actor": {"login": "other", "id": 39247043},
                     "head_repository": {"full_name": "other/repo"}, "display_title": "copied old title"}
        for key, value in mutations.items():
            with self.subTest(key=key):
                run = execution()
                run[key] = value
                with patch.object(publisher, "Api") as constructor, patch.object(authorize, "verify") as verify:
                    constructor.return_value.request.return_value = run
                    with self.assertRaises(ValueError):
                        publisher.utility_authority(self.env())
                    verify.assert_not_called()

    def test_actual_api_request_attempt_not_caller_first_attempt_label(self):
        import authorize
        for attempt in (None, "1", True, 1.0, 0, -1, 2):
            with self.subTest(attempt=attempt), patch.object(publisher, "Api") as constructor, patch.object(authorize, "verify") as verify:
                request = {"run_attempt": attempt}
                constructor.return_value.request.side_effect = [execution(), request]
                with self.assertRaises(ValueError):
                    publisher.utility_authority(self.env())
                verify.assert_not_called()



class ClockBindingTests(PhysicalClockTests):
    def test_every_native_clock_rebinds_actual_platform_start_and_raw_digest(self):
        for kind in ("sampling", "preparation", "utility"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.call(root, kind)
                raw = (root / "compiler-physical-job-clock.tsv").read_bytes()
                platform = job(kind)
                platform.update(status="completed", conclusion="success", completed_at="2025-10-09T08:53:40Z")
                authority = {"repository": REPOSITORY, "run_id": "200", "executor": execution()}
                result = publisher.physical_clock_binding(authority, {"physical-job-clock.tsv": raw}, platform,
                                                          kind, platform["name"])
                self.assertEqual(result["record_sha256"], hashlib.sha256(raw).hexdigest())
                self.assertEqual(result["observed_pre_entry_us"], 11100000)

    def test_clock_record_cannot_relabel_route_attempt_runner_policy_or_api_start(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.call(root)
            raw = (root / "compiler-physical-job-clock.tsv").read_bytes()
            original = publisher.sampling_tsv(raw)
            platform = job()
            platform.update(status="completed", conclusion="success", completed_at="2025-10-09T08:53:40Z")
            authority = {"repository": REPOSITORY, "run_id": "200", "executor": execution()}
            changes = {"kind": "preparation", "run_id": "201", "run_attempt": "2", "runner_id": "401",
                       "runner_name": "other", "policy_trusted_revision": "c" * 40,
                       "started_at": "2025-10-09T08:53:21Z", "started_unix_us": str(START + 1000000),
                       "start_lower_unix_us": str(START), "observer_started_unix_us": str(START - 1),
                       "observer_finished_unix_us": str(START + 50000000),
                       "observer_monotonic_elapsed_us": "120000001", "unknown": "extra"}
            for key, value in changes.items():
                with self.subTest(key=key):
                    row = dict(original, **{key: value})
                    changed = "".join(name + "\t" + str(item) + "\n" for name, item in row.items()).encode()
                    with self.assertRaises(ValueError):
                        publisher.physical_clock_binding(authority, {"physical-job-clock.tsv": changed}, platform,
                                                         "utility", publisher.UTILITY_HOST_JOB)


if __name__ == "__main__":
    unittest.main()
