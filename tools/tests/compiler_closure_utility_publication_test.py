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
        raw = (b"native_entry_wall_us\t3\nphysical_packet_wall_us\t10\n"
               b"wall_scope\tpublic-platform-job-start-lower-through-child-cleanup-before-terminal-publication\n")
        with patch.object(publisher, "utility_job", side_effect=ValueError("platform unavailable")):
            observed = publisher.utility_observed_costs(None, {}, {"owner.tsv": raw})
            partial = publisher.utility_observed_costs(None, {}, {"owner.tsv": b"physical_packet_wall_us\t10\n"})
        self.assertEqual(observed["native_owner_wall_us"], 3)
        self.assertEqual(observed["native_platform_start_wall_us"], 10)
        self.assertEqual(observed["unvalidated_native_packet_wall_us"], 10)
        self.assertFalse(observed["native_clock_observations_validated"])
        self.assertIsNone(observed["physical_job_wall_upper_us"])
        self.assertIsNone(partial["native_owner_wall_us"])
        self.assertIsNone(partial["native_platform_start_wall_us"])
        self.assertEqual(partial["unvalidated_native_packet_wall_us"], 10)


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
    sampling = {"pairs": 16, "order": "ABBA", "fresh_copy": True, "reason": "--target-minutes 10: fixture fixed before results"}
    steps = {key: {"status": "ok"} for key in ("env", "prepare", "timed")}
    variants, summary_variants, files = {}, {}, {}
    for key, role, name in (("a", "baseline", "ide-base"), ("b", "candidate", "ide-cand")):
        path = plan["output_root"] + "/legacy-work/bin/" + name
        binary = binaries[role]
        variants[key] = {"role": role, "ide": path, "sha256": binary["sha256"], "size_bytes": binary["size_bytes"]}
        summary_variants[role] = {"path": path, "sha256": binary["sha256"], "size_bytes": binary["size_bytes"],
                                  "runs": 16, "failed": 0, "identical_runs": 16, "deterministic": True}
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
    # Eight samples per order support the declared sign-test interval.
    # Vary A/B order effect deliberately: warning flags are report-only.
    for number in range(1, 17):
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
               bootstrap_resamples=2000, complete_pairs=16, fresh_copy=True), "cpu": 2, "command": command,
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
        self.assertEqual(result["complete_pairs"], 16)
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


        # Fresh API objects must preserve the committed alias's exact AB1 tree.
        plan = {"baseline_revision": "a" * 40, "baseline_tree": "1" * 40,
                "candidate_revision": "b" * 40, "candidate_tree": "2" * 40, "pull_head": "c" * 40}
        for arm in ({"sha": "c" * 40, "tree": {"sha": "3" * 40}}, {"sha": "d" * 40, "tree": {"sha": "2" * 40}},
                    {"sha": "c" * 40, "tree": None}):
            class CommitsApi:
                def request(self, path):
                    if path.endswith(plan["baseline_revision"]):
                        return {"sha": plan["baseline_revision"], "tree": {"sha": plan["baseline_tree"]}}
                    if path.endswith(plan["candidate_revision"]):
                        return {"sha": plan["candidate_revision"], "tree": {"sha": plan["candidate_tree"]},
                                "parents": [{"sha": plan["baseline_revision"]}, {"sha": plan["pull_head"]}]}
                    if path.endswith(plan["pull_head"]):
                        return arm
                    raise AssertionError("invalid AB1 tree must refuse before tool metadata API")
            with self.subTest(arm_tree=arm), self.assertRaisesRegex(ValueError, "AB1 source tree"):
                publisher.utility_source_identity(CommitsApi(), {"plan": plan, "admitted": {}}, {})
        for attempt in (None, "1", True, 1.0, 0, -1, 2):
            class JobsApi:
                def pages(self, path, field):
                    value = job()
                    value.update(status="completed", conclusion="success", run_attempt=attempt)
                    return [value]
            with self.subTest(completed_job_attempt=attempt), self.assertRaises(ValueError):
                publisher.utility_job(JobsApi(), {"run_id": "200", "executor": execution()})
        class WrongJobHead:
            def pages(self, path, field):
                return [dict(job(), head_sha="c" * 40)]
        with self.assertRaises(ValueError):
            publisher.utility_job(WrongJobHead(), {"run_id": "200", "executor": execution()})


    def test_actual_api_request_attempt_not_caller_first_attempt_label(self):
        import authorize
        for attempt in (None, "1", True, 1.0, 0, -1, 2):
            with self.subTest(attempt=attempt), patch.object(publisher, "Api") as constructor, patch.object(authorize, "verify") as verify:
                request = {"run_attempt": attempt}
                constructor.return_value.request.side_effect = [execution(), request]
                with self.assertRaises(ValueError):
                    publisher.utility_authority(self.env())
                verify.assert_not_called()



class ClockBindingTests(unittest.TestCase):
    call = PhysicalClockTests.call
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



class UtilityCheckTests(unittest.TestCase):
    def authority(self):
        return {"repository": REPOSITORY, "head": HEAD, "request_id": "100", "run_id": "200", "history": [],
                "admitted": {"utility_plan_sha256": "c" * 64, "utility_plan_revision": REVISION}}

    def api(self, authority, status=None, lost=False, invisible=False):
        class ChecksApi:
            def __init__(self):
                self.rows, self.posts, self.patches = [], 0, 0
            def pages(self, path, field):
                return [] if invisible else self.rows
            def request(self, path, fields, method=""):
                if path == "/check-runs":
                    self.posts += 1
                    row = dict(fields, id=300, app={"id": 15368})
                    self.rows.append(row)
                    if lost:
                        raise publisher.urllib.error.URLError("ambiguous lost creation response")
                    return row
                self.patches += 1
                self.rows[0].update(fields)
                return self.rows[0]
        api = ChecksApi()
        if status:
            api.rows.append({"id": 300, "name": publisher.UTILITY_CHECK_NAME, "head_sha": authority["head"],
                             "external_id": publisher.utility_check_marker(authority), "app": {"id": 15368},
                             "status": status, "conclusion": "failure" if status == "completed" else None})
        return api

    def test_exact_recovery_marker_protocol_and_unique_attempt_joins(self):
        authority = self.authority()
        self.assertEqual(publisher.utility_check_marker(authority),
            "buster-compiler-closure-utility-v1:" + "c" * 64 + ":utility:0:100:200:1")
        summary = publisher.utility_summary(authority)
        for line in ("Lifecycle protocol: closure-utility-terminal-native-v1.",
                     "Request run 100 attempt 1: https://github.com/buster14a/buster/actions/runs/100/attempts/1",
                     "Workflow run 200 attempt 1: https://github.com/buster14a/buster/actions/runs/200/attempts/1"):
            self.assertEqual(summary.count(line), 1)

    def test_terminal_and_running_owned_checks_never_rewind(self):
        authority = self.authority()
        for status in ("completed", "in_progress"):
            with self.subTest(status=status):
                api = self.api(authority, status)
                row = publisher.utility_write(api, authority, {"status": "queued"})
                self.assertEqual(row["status"], status)
                self.assertEqual((api.posts, api.patches), (0, 0))
                if status == "completed":
                    self.assertEqual(row["conclusion"], "failure")

    def test_lost_creation_response_uses_one_owned_lookup_without_second_post(self):
        authority = self.authority()
        api = self.api(authority, lost=True)
        self.assertEqual(publisher.utility_write(api, authority, {"status": "queued"})["id"], 300)
        self.assertEqual(api.posts, 1)
        api = self.api(authority, lost=True, invisible=True)
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            publisher.utility_write(api, authority, {"status": "queued"})
        self.assertEqual(api.posts, 1)

    def test_wrong_owner_attempt_identity_duplicates_and_absent_creation_boundary_refuse(self):
        authority = self.authority()
        api = self.api(authority, "queued")
        original = copy.deepcopy(api.rows[0])
        for key, value in (("head_sha", "d" * 40), ("name", publisher.SAMPLING_CHECK_NAME),
                           ("external_id", original["external_id"].replace(":200:1", ":201:1")),
                           ("app", {"id": 1}), ("id", True)):
            with self.subTest(key=key):
                self.assertFalse(publisher.utility_owned(dict(original, **{key: value}), authority))
        api.rows.append(dict(original, id=301))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            publisher.utility_checks(api, authority)
        empty = self.api(authority)
        with patch.object(publisher, "utility_authority", return_value=(empty, authority)), \
                patch.object(publisher, "utility_read_artifact") as artifact:
            with self.assertRaisesRegex(ValueError, "existing"):
                publisher.utility_publish({})
            artifact.assert_not_called()
        self.assertEqual(empty.posts, 0)

    def test_invalid_artifact_closes_only_preexisting_owned_row_with_unknown_costs(self):
        import json
        authority = self.authority()
        api = self.api(authority, "queued")
        unavailable = {"physical_job_wall_upper_us": None, "native_owner_wall_us": None}
        with patch.object(publisher, "utility_authority", return_value=(api, authority)), \
                patch.object(publisher, "utility_read_artifact", side_effect=ValueError("corrupt bounded ZIP")), \
                patch.object(publisher, "utility_observed_costs", return_value=unavailable), \
                patch.object(publisher, "utility_validate") as validate:
            self.assertEqual(publisher.utility_publish({"BQ_UTILITY_RESULT": "success"}), 1)
            validate.assert_not_called()
        self.assertEqual((api.posts, api.patches), (0, 1))
        row = api.rows[0]
        self.assertEqual((row["status"], row["conclusion"]), ("completed", "failure"))
        self.assertEqual(row["output"]["title"], "Incomplete unqualified utility packet")
        result = json.loads("\n".join(row["output"]["text"].splitlines()[1:-1]))
        self.assertEqual(result["packet_state"], "incomplete")
        self.assertEqual(result["qualification_state"], "unqualified")
        self.assertFalse(result["routine_profile_enabled"])
        self.assertFalse(result["default_activated"])
        self.assertIsNone(result["accounting"]["physical_job_wall_upper_us"])
        self.assertIsNone(result["accounting"]["native_owner_wall_us"])
        self.assertEqual(result["authenticated_attempt_history"][0]["state"], "incomplete")
        self.assertTrue(result["problems"])


class UtilityNativeExportReplay(unittest.TestCase):
    def test_actual_native_ordinary_exports_are_complete_data_and_unqualified(self):
        import io
        import json
        import stat
        import zipfile
        from compiler_preparation import parse_argv
        directory_label = os.environ.get("BUSTER_UTILITY_NATIVE_EXPORT")
        if not directory_label:
            self.skipTest("run --utility-native-export DIR after the hosted native Utility writer fixture")
        directory = Path(directory_label)
        evidence = directory / "evidence"
        members = {}
        for path in evidence.rglob("*"):
            self.assertFalse(path.is_symlink(), str(path))
            if path.is_dir():
                continue
            self.assertTrue(path.is_file(), str(path))
            self.assertLessEqual(path.stat().st_size, publisher.PREPARATION_MEMBER_LIMIT)
            members[path.relative_to(evidence).as_posix()] = path.read_bytes()
        self.assertLessEqual(len(members), publisher.PREPARATION_FILE_LIMIT)
        self.assertLessEqual(sum(map(len, members.values())), publisher.PREPARATION_ARCHIVE_LIMIT)
        marker = json.loads(members["fixture-plan.json"])
        self.assertEqual(marker["schema"], "buster-compiler-closure-utility-fixture-v1")
        self.assertIs(marker["diagnostic_fixture"], True)
        self.assertIs(marker["physical_qualification"], False)
        self.assertEqual(marker["qualification_state"], "unqualified")
        expected = marker["expected"]
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
            for name, data in members.items():
                item = zipfile.ZipInfo(name)
                item.external_attr = (stat.S_IFREG | 0o600) << 16
                zipped.writestr(item, data)
        with patch.object(zipfile.ZipFile, "extract", side_effect=AssertionError("extraction")), \
                patch.object(zipfile.ZipFile, "extractall", side_effect=AssertionError("extraction")):
            files = publisher.preparation_archive(packed.getvalue())
        # A private producer proof deliberately has no platform job observation.
        # The public boundary rejects it before it could acquire any authority.
        self.assertNotIn("physical-job-clock.tsv", files)
        with self.assertRaisesRegex(ValueError, "diagnostic"):
            publisher.utility_validate(None, {}, files)
        host = json.loads(files["host.json"])
        self.assertEqual(host["schema"], "buster-compiler-closure-utility-host-v1")
        self.assertEqual(host["state"], "complete")
        self.assertEqual(host["observed_from"], "/proc/cpuinfo")
        self.assertNotIn("9700X", host["cpu_model"])
        self.assertGreater(host["logical_processor_records"], 0)
        for key in ("trusted_lab", "python", "native_driver", "native_driver_sha256", "bootstrap_marker_sha256"):
            self.assertEqual(host[key], expected[key])
        self.assertEqual(host["measurement_trusted_revision"], expected["trusted_revision"])
        for path_key, hash_key in (("trusted_lab", "trusted_lab_sha256"), ("python", "python_sha256"),
                                   ("native_driver", "native_driver_sha256"), ("comparator", "comparator_sha256"),
                                   ("receipt_adapter", "receipt_sha256"), ("owned_phase", "owned_phase_sha256")):
            self.assertEqual(hashlib.sha256(Path(host[path_key]).read_bytes()).hexdigest(), host[hash_key])
        claim = publisher.sampling_tsv(files["claim.tsv"])
        self.assertEqual(claim["schema"], "buster-compiler-closure-utility-diagnostic-claim-v1")
        self.assertEqual(claim["diagnostic_fixture"], "true")
        self.assertEqual(claim["physical_qualification"], "false")
        self.assertEqual(claim["qualification_state"], "unqualified")
        self.assertEqual(claim["physical_job_cost"], "unavailable")
        self.assertEqual(claim["clock_scope"], "native-diagnostic-only")
        for key, label in (("source_root", "root"), ("output_root", "output"), ("trusted_root", "trusted_root"),
                           ("trusted_revision", "trusted_revision"), ("native_driver_sha256", "native_driver_sha256")):
            self.assertEqual(claim[key], expected[label])
        self.assertEqual(claim["evidence"], str(evidence))
        plan = {"source_root": expected["root"], "output_root": expected["output"],
                "baseline_revision": expected["base"], "baseline_tree": expected["base_tree"],
                "candidate_revision": expected["head"], "candidate_tree": expected["head_tree"],
                "pull_head": expected["pull_head"], "trusted_revision": expected["trusted_revision"],
                "trusted_root": expected["trusted_root"], "native_driver_sha256": expected["native_driver_sha256"]}
        authority = {"repository": REPOSITORY, "request_id": "1", "run_id": "1", "pull": "1", "plan": plan}
        owner_raw = files["owner.tsv"]
        owner = publisher.sampling_tsv(owner_raw)
        plan_sha = hashlib.sha256(files["claim.tsv"]).hexdigest()
        wanted = {"schema": "buster-compiler-closure-utility-owner-v1", "phase": "utility", "packet": "0",
                  "plan_sha256": plan_sha, "wall_scope": "native-diagnostic-entry-through-child-cleanup-before-terminal-publication",
                  "process_state": "complete", "timed_out": "0", "cleanup_failed": "0", "within_reservation": "true",
                  "cancelled": "0", "qualification_state": "unvalidated", "default_activated": "false",
                  "job_elapsed_at_native_entry_us": "0", "physical_job_clock_sha256": "unavailable",
                  "manager_launch_attempted": "1", "manager_wait_observed": "1", "manager_cleanup_proven": "1"}
        self.assertEqual(set(owner), set(wanted) | {"physical_packet_wall_us", "native_entry_wall_us"})
        for key, value in wanted.items():
            self.assertEqual(owner[key], value)
        owner_wall = publisher.sampling_integer(owner["physical_packet_wall_us"], True)
        self.assertEqual(owner["physical_packet_wall_us"], owner["native_entry_wall_us"])
        publication = publisher.sampling_tsv(files["owner-publication.tsv"])
        self.assertEqual(publication["schema"], "buster-compiler-closure-utility-owner-publication-v1")
        self.assertEqual(publication["owner_sha256"], hashlib.sha256(owner_raw).hexdigest())
        self.assertEqual(publication["scope"], "native-diagnostic-entry-through-owner-publication")
        self.assertEqual(publication["observation_publication_us"], "unavailable")
        self.assertEqual(publication["within_reservation"], "true")
        self.assertEqual(int(publication["initial_scope_us"]), owner_wall)
        self.assertEqual(int(publication["observed_wall_us"]), owner_wall + int(publication["publication_us"]))
        self.assertLessEqual(int(publication["observed_wall_us"]), 5400 * 1000000)
        terminal = publisher.sampling_tsv(files["utility.tsv"])
        for key, value in {"schema": "buster-compiler-closure-utility-controller-v1", "phase": "utility", "packet": "0",
                           "plan_sha256": plan_sha, "process_state": "complete", "qualification_state": "unvalidated",
                           "default_activated": "false", "cleanup_proven": "true", "source_root": expected["root"],
                           "output_root": expected["output"], "tools_before": "true", "tools_after": "true",
                           "exported": "true", "complete_legs": "2", "terminal_publication_us": "unavailable",
                           "clock_scope": "bootstrap-through-export-hashfinalization",
                           "utility_charge_policy": "all-physical-residual-to-snapshot", "net_utility": "unavailable"}.items():
            self.assertEqual(terminal[key], value)
        self.assertLessEqual(publisher.sampling_integer(terminal["duration_us"], True), owner_wall)
        authority["admitted"] = {"utility_plan_sha256": plan_sha}
        phases = publisher.sampling_tsv(files["controller.tsv"], True)
        git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null"]
        commands = [
            ("trusted-harness-pin", git + ["-C", expected["trusted_root"], "rev-parse", "HEAD"]),
            ("trusted-harness-clean", git + ["-C", expected["trusted_root"], "diff", "--quiet", "--exit-code", "HEAD", "--"]),
            ("baseline-tree", git + ["-C", expected["root"], "rev-parse", expected["base"] + "^{tree}"]),
            ("candidate-tree", git + ["-C", expected["root"], "rev-parse", expected["head"] + "^{tree}"]),
            ("candidate-first-parent", git + ["-C", expected["root"], "rev-parse", expected["head"] + "^1"])]
        identity = ["--mode", "main", "--repository", REPOSITORY, "--ref", "refs/heads/main",
                    "--pull", "1", "--pull-head", expected["pull_head"], "--base", expected["base"],
                    "--base-tree", expected["base_tree"], "--head", expected["head"], "--head-tree", expected["head_tree"],
                    "--trusted-revision", expected["trusted_revision"], "--request-run-id", "1", "--run-id", "1", "--run-attempt", "1"]
        for leg, policy in (("legacy", "legacy-rebuild"), ("snapshot", "snapshot-v1")):
            compare = [host["python"], "-B", host["comparator"], "--candidate", expected["root"],
                       "--lab", host["trusted_lab"], "--work", expected["output"] + "/" + leg + "-work",
                       "--evidence", expected["output"] + "/" + leg + "-evidence", "--summary", expected["output"] + "/" + leg + ".md",
                       "--closure-policy", policy]
            if leg == "snapshot":
                compare += ["--closure-driver", host["native_driver"]]
            commands += [
                (leg + "-reset-checkout", git + ["-C", expected["root"], "checkout", "--quiet", "--detach", expected["head"]]),
                (leg + "-reset-tracked-source", git + ["-C", expected["root"], "reset", "--hard", "--quiet", expected["head"]]),
                (leg + "-reset-build-cache", git + ["-C", expected["root"], "clean", "-fdx"]),
                (leg + "-trusted-bootstrap", [expected["trusted_root"] + "/build.sh", "compiler_profile_qualification", "--plan"]),
                (leg + "-ordinary-compare", compare + identity)]
        self.assertEqual(len(phases), len(commands))
        proofs = {"owner-supervision.tsv": owner_wall}
        for index, (phase, (name, command)) in enumerate(zip(phases, commands), 1):
            self.assertEqual(phase["stage"], str(index))
            self.assertEqual(phase["phase"], name)
            self.assertEqual(phase["state"], "complete")
            for key in ("exit_status", "timed_out", "cleanup_failed", "cancelled"):
                self.assertEqual(phase[key], "0")
            stem = f"controller-{index}-{name}"
            self.assertEqual(parse_argv(files[stem + ".argv"]), command)
            for stream in ("stdout", "stderr"):
                self.assertIsInstance(files[stem + "." + stream + ".log"], bytes)
            proofs[stem + "-supervision.tsv"] = publisher.sampling_integer(phase["wall_us"], True)
        self.assertLessEqual(sum(int(phase["wall_us"]) for phase in phases), int(terminal["duration_us"]))
        self.assertEqual({name for name in files if name.endswith("-supervision.tsv")}, set(proofs))
        for name, bound in proofs.items():
            proof = publisher.sampling_supervision(files[name])
            self.assertLessEqual(publisher.sampling_integer(proof["wall_us"], True), bound)
        legs = publisher.utility_leg_records(authority, files, host, terminal, phases)
        results = {}
        for row in legs:
            leg = row["leg"]
            for variant in ("a", "b"):
                cpuinfo = files[f"utility/{leg}/lab/{variant}/env/cpuinfo.txt"].decode("ascii")
                models = [line.partition(":")[2].strip() for line in cpuinfo.splitlines() if line.startswith("model name")]
                self.assertTrue(models)
                self.assertEqual(len(models), host["logical_processor_records"])
                self.assertEqual(set(models), {host["cpu_model"]})
            ordinary = publisher.sampling_json(files, f"utility/{leg}/ordinary/receipt.json")
            self.assertIs(ordinary["diagnostic_fixture"], True)
            self.assertEqual(ordinary["qualification_state"], "unqualified")
            self.assertEqual(ordinary["host"]["cpu_model"], host["cpu_model"])
            self.assertTrue(publisher.host_problem(ordinary))
            with self.assertRaisesRegex(ValueError, "approved Zen 5 host"):
                publisher.utility_ordinary_leg(authority, files, host, row, phases)
            # This guarded hosted-only proof replays data on the actual CPU.
            # The normal host and admission boundaries remain refusing above.
            with patch.object(publisher, "host_problem", return_value=""):
                results[leg] = publisher.utility_ordinary_leg(authority, files, host, row, phases)
            self.assertEqual(results[leg]["state"], "complete")
            self.assertEqual(results[leg]["series"]["complete_pairs"], 16)
            self.assertEqual(results[leg]["series"]["verdict"]["outcome"], "slower")
            self.assertEqual(results[leg]["throughput"]["complete_timed_samples"], 960)
            self.assertEqual(results[leg]["throughput"]["diagnostic_samples"], 0)
            ordinary = publisher.sampling_json(files, f"utility/{leg}/ordinary/receipt.json")
            self.assertEqual(ordinary["state"], "measured")
            self.assertEqual(ordinary["reasons"], [])
            if leg == "legacy":
                self.assertNotIn("phase_ownership", ordinary)
            else:
                self.assertTrue(ordinary["phase_ownership"]["phases"])

            if leg == "legacy":
                prefix = "utility/legacy/throughput/"
                binaries = ordinary["binaries"]
                raw_names = ("samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl", "capabilities.jsonl")
                def rebound(changed, name, value):
                    changed[prefix + name] = value
                    changed[prefix + "complete.txt"] = (
                        "schema=2 jobs=12 pairs=20 rounds=2 guard=1\n" +
                        "".join(hashlib.sha256(changed[prefix + label]).hexdigest() + " " + label + "\n"
                                for label in raw_names)).encode("ascii")
                    return changed
                def altered_json(name, mutate):
                    changed = dict(files)
                    value = json.loads(changed[prefix + name])
                    mutate(value)
                    return rebound(changed, name, encode(value))
                def altered_lines(name, mutate):
                    changed = dict(files)
                    values = [json.loads(line) for line in changed[prefix + name].splitlines()]
                    mutate(values)
                    return rebound(changed, name, b"".join(encode(value) for value in values))
                changes = [
                    ("seed", altered_json("metadata.json", lambda value: value.update(seed=1))),
                    ("scale", altered_json("metadata.json", lambda value: value.update(scale=2))),
                    ("input-schema", altered_json("metadata.json", lambda value: value.update(input_schema=True))),
                    ("artifact", altered_json("metadata.json", lambda value: value["jobs"][0].update(artifact="assembly"))),
                    ("wrong-variant", altered_lines("commands.jsonl", lambda values: values[0]["argv"].__setitem__(0,
                        plan["output_root"] + "/legacy-work/bin/ide-cand"))),
                    ("wrong-input", altered_lines("commands.jsonl", lambda values: values[0]["argv"].__setitem__(7, "/other/input.c"))),
                    ("wrong-mode", altered_lines("commands.jsonl", lambda values: values[0]["argv"].__setitem__(6, "-fregister-allocator=quality"))),
                    ("extra-flag", altered_lines("commands.jsonl", lambda values: values[0]["argv"].insert(4, "-O3"))),
                    ("bad-status", altered_lines("capabilities.jsonl", lambda values: values[0].update(exit_code=1))),
                    ("missing-command", altered_lines("commands.jsonl", lambda values: values.pop()))]
                for label, changed in changes:
                    with self.subTest(raw_corpus_tamper=label), self.assertRaises(ValueError):
                        publisher.utility_corpus_raw(changed, prefix, plan, leg, binaries, expected_cpu_model=host["cpu_model"])

        # No API job, publication tail or utility criterion is fabricated.
        self.assertEqual(terminal["net_utility"], "unavailable")
        print("UTILITY_NATIVE_DATA_REPLAY legs=2 selfhost_slower=2 corpus_samples=1920 "
              "raw_zip_bound=complete physical_job_cost=unavailable qualification=unqualified")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--utility-native-export":
        os.environ["BUSTER_UTILITY_NATIVE_EXPORT"] = sys.argv[2]
        unittest.main(argv=[sys.argv[0]], defaultTest="UtilityNativeExportReplay")
    else:
        unittest.main()
