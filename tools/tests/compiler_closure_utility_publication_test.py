#!/usr/bin/env python3
"""Bounded Utility publication controls; hosted data fixtures never authorize a physical run."""
import base64
import copy
import hashlib
import json
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
        self.assertIs(type(result["baseline_recipe_scope"]), str)
        self.assertEqual(result["baseline_recipe_scope"], "declared-supervised-ordinary-recipes")
        self.assertIs(result["historical_unwrapped_legacy_savings_assessed"], False)

    def test_complete_negative_and_equal_cost_remain_measured_cost_data(self):
        for total in (6000000, 7000000):
            result = publisher.utility_net_observation(3000000, 1000000, total)
            self.assertFalse(result["criterion_met"])
            self.assertEqual(result["snapshot_charged_us"], total - 3000000)
            self.assertIs(type(result["baseline_recipe_scope"]), str)
            self.assertEqual(result["baseline_recipe_scope"], "declared-supervised-ordinary-recipes")
            self.assertIs(result["historical_unwrapped_legacy_savings_assessed"], False)

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


def ordinary_series_fixture(pairs=16, fixed=False):
    from sampling_qualification_receipt import _lab
    plan = {"source_root": "/tmp/utility-source", "output_root": "/tmp/utility-output", "baseline_revision": "d" * 40}
    prefix, leg = "utility/legacy/lab/", "legacy"
    binaries = {"baseline": {"sha256": "1" * 64, "size_bytes": 100000},
                "candidate": {"sha256": "2" * 64, "size_bytes": 100001}}
    command = _lab.shell_join(["IDE"] + _lab.DEFAULT_COMPILE + ["-o", "OUT"])
    config = {"command": command, "repo_root": plan["source_root"], "cpu": 2, "perf": "perf", "pairs": pairs if fixed else None,
              "target_minutes": 10.0, "warmups": 1, "seed": 20261003, "profile_steps": [], "sudo": False,
              "require_identical_output": False, "extra": [], "canonical_inline_pair": False,
              "extra_by_variant": {"a": [], "b": []}, "fresh_copy": True, "min_effect_percent": 0.5}
    sampling = {"pairs": pairs, "order": "ABBA", "fresh_copy": True,
                "reason": "--pairs 40" if fixed else "--target-minutes 10: fixture fixed before results"}
    steps = {key: {"status": "ok"} for key in ("env", "prepare", "timed")}
    variants, summary_variants, files = {}, {}, {}
    for key, role, name in (("a", "baseline", "ide-base"), ("b", "candidate", "ide-cand")):
        path = plan["output_root"] + "/legacy-work/bin/" + name
        binary = binaries[role]
        variants[key] = {"role": role, "ide": path, "sha256": binary["sha256"], "size_bytes": binary["size_bytes"]}
        summary_variants[role] = {"path": path, "sha256": binary["sha256"], "size_bytes": binary["size_bytes"],
                                  "runs": pairs, "failed": 0, "identical_runs": pairs, "deterministic": True}
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
    for number in range(1, pairs + 1):
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
               bootstrap_resamples=2000, complete_pairs=pairs, fresh_copy=True), "cpu": 2, "command": command,
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



class MainOwnedRawReplayTests(unittest.TestCase):
    def test_fixed40_replays_original_inference_and_keeps_scientific_slowdown_report_only(self):
        args = ordinary_series_fixture(pairs=40, fixed=True)
        result = publisher.utility_series_replay(*args, expected_profile="compiler-main-40pairs-v1",
                    work_root=args[2]["output_root"] + "/legacy-work")
        self.assertEqual(result["complete_pairs"], 40)
        self.assertEqual(result["verdict"]["outcome"], "slower")
        self.assertTrue(any(row.get("flag") for row in result["checks"].values()))
        self.assertEqual(publisher.utility_series_replay(*ordinary_series_fixture())["complete_pairs"], 16)

    def test_wrong_fixed_count_config_order_hash_floor_seed_or_saved_inference_refuses(self):
        mutations = (
            ("compare.json", lambda row: row["config"].update(pairs=39)),
            ("compare.json", lambda row: row["config"].update(pairs=41)),
            ("compare.json", lambda row: row["config"].update(pairs=True)),
            ("compare.json", lambda row: row["config"].update(target_minutes=9)),
            ("compare.json", lambda row: row["config"].update(min_effect_percent=0.4)),
            ("compare.json", lambda row: row["config"].update(seed=1)),
            ("compare.json", lambda row: row["plan"].update(reason="--target-minutes 10: relabelled")),
            ("compare.json", lambda row: row["plan"].update(order="AB")),
            ("compare.json", lambda row: row["variants"]["b"].update(sha256="9" * 64)),
            ("summary.json", lambda row: row["plan"].update(seed=1)),
            ("summary.json", lambda row: row["plan"].update(confidence=0.9)),
            ("summary.json", lambda row: row["plan"].update(bootstrap_resamples=1)),
            ("summary.json", lambda row: row["plan"].update(complete_pairs=39)),
            ("summary.json", lambda row: row["verdict"].update(outcome="faster")),
            ("summary.json", lambda row: row["metrics"]["wall"].update(ratio=0.99)),
            ("pairs.json", lambda rows: rows.pop()),
            ("pairs.json", lambda rows: rows.append(dict(rows[-1]))),
            ("pairs.json", lambda rows: rows[0].update(order="BA")),
            ("pairs.json", lambda rows: rows[0].update(exit=1)),
            ("pairs.json", lambda rows: rows[0].update(identical=False)),
            ("pairs.json", lambda rows: rows[0].update(span_s=0)))
        for name, mutate in mutations:
            args = ordinary_series_fixture(pairs=40, fixed=True)
            files, prefix, plan, unused_leg, unused_binaries = args
            value = json.loads(files[prefix + name])
            mutate(value)
            files[prefix + name] = encode(value)
            with self.subTest(member=name, mutation=mutate), self.assertRaises(ValueError):
                publisher.utility_series_replay(*args, expected_profile="compiler-main-40pairs-v1",
                    work_root=plan["output_root"] + "/legacy-work")
        with self.assertRaises(ValueError):
            publisher.utility_series_replay(*ordinary_series_fixture(), expected_profile="compiler-main-40pairs-v1")
        with self.assertRaises(ValueError):
            publisher.utility_series_replay(*ordinary_series_fixture(pairs=40, fixed=True))

    def test_diagnostic_raw_data_cannot_select_a_public_main_route(self):
        files, unused_prefix, unused_plan, unused_leg, unused_binaries = ordinary_series_fixture(pairs=40, fixed=True)
        route = {"main_owned": True, "main_phase_schema": publisher.MAIN_PHASE_SCHEMA,
                 "main_preparation_policy": "legacy-rebuild", "main_measurement_revision": "a" * 40,
                 "main_profile": "compiler-main-40pairs-v1", "driver_sha256": "a" * 64}
        files["fixture-plan.json"] = encode({"diagnostic_fixture": True})
        with self.assertRaisesRegex(ValueError, "diagnostic"):
            publisher.main_owned_files(files, route)



    def test_full_ordinary_archive_population_keeps_sampling_default_and_total_bounds(self):
        import io
        import zipfile
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for index in range(2049):
                archive.writestr(f"lab/pairs/{index:04d}.csv", b"")
        payload = packed.getvalue()
        with self.assertRaises(ValueError):
            publisher.sampling_archive(payload)
        self.assertEqual(len(publisher.sampling_archive(payload, member_limit=publisher.MAIN_ARCHIVE_FILE_LIMIT)), 2049)
        for limit in (True, 0, publisher.MAIN_ARCHIVE_FILE_LIMIT + 1):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                publisher.sampling_archive(payload, member_limit=limit)

class MainRouteIdentityTests(unittest.TestCase):
    def route(self, owned=True):
        result = {key: "-" for key in publisher.MAIN_ROUTE_FIELDS}
        result.update(main_owned=owned, main_profile="compiler-main-40pairs-v1" if owned else "compiler-compare-v1",
                      main_preparation_policy="legacy-rebuild", main_phase_schema=publisher.MAIN_PHASE_SCHEMA if owned else "-",
                      main_policy_revision="a" * 40, main_measurement_revision="b" * 40 if owned else "a" * 40)
        if owned:
            for key in ("lab_sha256", "python_sha256", "driver_sha256", "compare_sha256", "receipt_sha256",
                        "owned_phase_sha256", "owned_plan_sha256"):
                result[key] = "c" * 64
            result.update(python_path="/usr/bin/python3", trusted_root="/runner/work/trusted",
                          candidate_root="/runner/work/candidate", work_root="/runner/temp/compiler-bench/work",
                          evidence_root="/runner/temp/compiler-bench/evidence")
        return result

    def original_run(self):
        return {"id": 200, "run_attempt": 3, "path": publisher.BENCH_WORKFLOW, "event": "workflow_run",
                "head_branch": "main", "head_sha": "a" * 40, "repository": {"full_name": REPOSITORY},
                "display_title": "9700X request 100.2 head " + "d" * 40}

    def test_original_attempt_three_selects_policy_P_and_frozen_H(self):
        import authorize_compiler
        class OriginalAttemptApi:
            def request(inner, path):
                self.assertEqual(path, "/actions/runs/200/attempts/3")
                return self.original_run()
        api, selected = OriginalAttemptApi(), self.route()
        with patch.object(authorize_compiler, "resolve_main_route", return_value=selected, create=True) as resolve, \
                patch.object(publisher, "main_runtime_pins") as runtime, \
                patch.object(authorize_compiler, "main_route_attempt", return_value=(self.original_run(), {"id": 100, "run_attempt": 2}, "d" * 40), create=True):
            result = publisher.main_route(api, REPOSITORY, "200", "3")
        self.assertEqual(result["main_measurement_revision"], "b" * 40)
        self.assertEqual(result["main_policy_revision"], "a" * 40)
        self.assertEqual(resolve.call_args.args, (api, REPOSITORY, self.original_run(), "3"))
        runtime.assert_called_once_with(api, selected)

    def test_wrong_original_attempt_or_policy_and_claimed_missing_proof_never_downgrades(self):
        import authorize_compiler
        for key, value in (("id", True), ("run_attempt", 1), ("run_attempt", "3"), ("head_sha", "c" * 40),
                           ("path", "other.yml"), ("repository", {"full_name": "other/repo"})):
            run = self.original_run()
            run[key] = value
            class ChangedApi:
                def request(inner, unused):
                    return run
            with self.subTest(key=key, value=value), patch.object(authorize_compiler, "resolve_main_route",
                    return_value=self.route(), create=True), patch.object(publisher, "main_runtime_pins"), self.assertRaises(ValueError):
                publisher.main_route(ChangedApi(), REPOSITORY, "200", "3")
        class Api:
            def request(inner, unused):
                return self.original_run()
        for key, value in (("main_owned", 1), ("main_policy_revision", "c" * 40),
                           ("main_phase_schema", "buster-compiler-snapshot-phases-v1"), ("driver_sha256", "-"),
                           ("trusted_root", "/runner/../trusted")):
            route = self.route()
            route[key] = value
            with self.subTest(route_key=key), patch.object(authorize_compiler, "resolve_main_route",
                    return_value=route, create=True), patch.object(publisher, "main_runtime_pins"), self.assertRaises(ValueError):
                publisher.main_route(Api(), REPOSITORY, "200", "3")
        with patch.object(authorize_compiler, "resolve_main_route", side_effect=ValueError("claimed policy missing proof"),
                          create=True), self.assertRaisesRegex(ValueError, "claimed policy"):
            publisher.main_route(Api(), REPOSITORY, "200", "3")
        with patch.object(authorize_compiler, "resolve_main_route", return_value=self.route(False), create=True), \
                patch.object(publisher, "main_runtime_pins") as runtime:
            self.assertIs(publisher.main_route(Api(), REPOSITORY, "200", "3")["main_owned"], False)
            runtime.assert_not_called()



class MainRuntimeAndArchiveTests(unittest.TestCase):
    def fixture(self):
        route = MainRouteIdentityTests().route()
        route.update(executor_run="200", executor_attempt="3", request_run="100", request_attempt="2",
                     request_head="d" * 40)
        ownership = {"driver_path": route["trusted_root"] + "/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-fixture"}
        paths = {"python": route["python_path"], "driver": ownership["driver_path"],
                 **{role: route["trusted_root"] + "/" + path for role, path in
                    (("lab", "tools/uarch_lab.py"), ("compare", "tools/bench_direct/compiler_compare.py"),
                     ("receipt", "tools/bench_direct/compiler_receipt.py"),
                     ("owned_phase", "tools/bench_direct/compiler_owned_phase.py"),
                     ("owned_plan", "tools/bench_direct/compiler_owned_plan.py"))}}
        route["runtime_source_bytes"] = {path[len(route["trusted_root"]) + 1:]: 10
                                        for role, path in paths.items() if role not in ("python", "driver")}
        record = {"schema": "buster-compiler-main-runtime-v1", "measurement_revision": route["main_measurement_revision"],
                  "policy_revision": route["main_policy_revision"], "executor_run": "200", "executor_attempt": "3",
                  "request_run": "100", "request_attempt": "2", "request_head": "d" * 40,
                  **{key: route[key] for key in ("trusted_root", "candidate_root", "work_root", "evidence_root")}}
        for role, path in paths.items():
            record.update({role + "_path": path, role + "_sha256": route[role + "_sha256"], role + "_bytes": "10"})
        return route, ownership, record

    def test_runtime_same_path_changed_python_bytes_and_rebound_attempt_or_H_refuses(self):
        route, ownership, record = self.fixture()
        def files(row):
            return {"main-runtime.tsv": "".join(key + "\t" + value + "\n" for key, value in row.items()).encode()}
        self.assertEqual(publisher.main_runtime_record(files(record), route, ownership), record)
        self.assertEqual(len(record), 33)
        for key, value in (("python_sha256", "f" * 64), ("driver_sha256", "f" * 64),
                           ("lab_sha256", "f" * 64), ("lab_bytes", "11"),
                           ("measurement_revision", "f" * 40), ("policy_revision", "f" * 40),
                           ("executor_attempt", "1"), ("request_attempt", "1"), ("work_root", "/other"),
                           ("python_bytes", "True"), ("owned_plan_path", "/other/helper.py")):
            altered = dict(record, **{key: value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                publisher.main_runtime_record(files(altered), route, ownership)
        with self.assertRaises(ValueError):
            publisher.main_runtime_record({}, route, ownership)

    def archive(self, members, executable=False):
        import io
        import stat
        import zipfile
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, raw in members.items():
                item = zipfile.ZipInfo(name)
                item.external_attr = (stat.S_IFREG | (0o700 if executable else 0o600)) << 16
                archive.writestr(item, raw)
        return packed.getvalue()

    def test_fixed_nested_evidence_and_native_proof_duplicates_are_exact_data_only(self):
        members = {"evidence/receipt.json": b"{}", "evidence/lab/pairs.json": b"[]",
                   "evidence/main-owner.json": b"owner", "evidence.native/main-owner.json": b"owner"}
        decoded = publisher.main_archive_files(self.archive(members))
        self.assertEqual(decoded, {"receipt.json": b"{}", "lab/pairs.json": b"[]", "main-owner.json": b"owner"})
        for changes in ({"receipt.json": b"{}"}, {"other/data": b"x"},
                        {"evidence.native/main-owner.json": b"changed"}, {"evidence.native/unlisted.sh": b"data"}):
            altered = dict(members, **changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                publisher.main_archive_files(self.archive(altered))
        with self.assertRaises(ValueError):
            publisher.main_archive_files(self.archive({"receipt.json": b"{}"}, executable=True))


    def test_complete_native_clock_must_fit_fresh_API_upper_including_preentry(self):
        import compiler_owned_phase as native
        route = MainRouteIdentityTests().route()
        route.update(executor_run="200", executor_attempt="3", request_run="100", request_attempt="2", request_head="d" * 40)
        identity = {"pull": "1", "pull_head": "f" * 40, "base": "b" * 40, "base_tree": "c" * 40,
                    "head": "d" * 40, "head_tree": "e" * 40, "request_run_id": "100", "run_id": "200", "run_attempt": "3"}
        ownership = {"driver_path": "/trusted/driver", "phases": [{"bridge_wall_us": 30000000}]}
        row = dict(job(), name=publisher.COMPARE_JOBS["main"], run_attempt=3, head_sha=route["main_policy_revision"],
                   status="completed", conclusion="success", created_at="2025-10-09T08:53:00Z",
                   completed_at="2025-10-09T08:54:00Z")
        class Api:
            def pages(inner, path, field):
                self.assertEqual(path, "/actions/runs/200/attempts/3/jobs")
                self.assertEqual(field, "jobs")
                return [row]
        for entry, expected_valid in ((10000000, True), (40000000, False)):
            record = {"schema": "buster-compiler-main-clock-v1",
                      "physical_job_clock_sha256": hashlib.sha256(b"clock").hexdigest(),
                      "job_elapsed_at_native_entry_us": str(entry), "native_elapsed_at_owner_admission_us": "0",
                      "remaining_us": str(5280000000 - entry), "timeout_seconds": str((5280000000 - entry) // 1000000)}
            files = {"main-clock.tsv": "".join(key + "\t" + value + "\n" for key, value in record.items()).encode(),
                     "physical-job-clock.tsv": b"clock", "main-owner.json": b"diagnostic-owner",
                     "main-owner.json.argv": b"argv", "main-owner.json.bootstrap.complete": b"bootstrap",
                     "main-owner.json.stdout": b"", "main-owner.json.stderr": b""}
            # Isolate the joined public/native wall rule; native command/proof
            # validity is independently exercised by the actual native fixture.
            with self.subTest(preentry=entry), patch.object(native, "read_record", return_value={"duration_us": 30000000}), \
                    patch.object(native, "validate_record", return_value=[]), patch.object(native, "validate_bootstrap", return_value=[]), \
                    patch.object(native, "command_bytes", return_value=b"argv"), \
                    patch.object(publisher, "physical_clock_binding", return_value={"observed_pre_entry_us": 10000000}):
                if expected_valid:
                    result = publisher.main_owner_files(Api(), files, route, {"identity": identity}, ownership)
                    self.assertEqual(result["native_owner_wall_us"], 30000000)
                    self.assertEqual(result["accounting"]["native_packet_wall_us"], 40000000)
                else:
                    with self.assertRaisesRegex(ValueError, "occupancy"):
                        publisher.main_owner_files(Api(), files, route, {"identity": identity}, ownership)


class MainPhysicalClockTests(unittest.TestCase):
    def test_main_automatic_push_and_original_owner_rerun_use_exact_attempt_clock(self):
        import authorize_compiler
        for attempt in (1, 3):
            with self.subTest(attempt=attempt), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                run = dict(execution(), run_attempt=attempt, actor={"login": "merge-bot", "id": 17})
                row = dict(job(), name=publisher.COMPARE_JOBS["main"], run_attempt=attempt)
                env = dict(environment(root), BQ_PHYSICAL_CLOCK_KIND="main", GITHUB_JOB="compare",
                           GITHUB_RUN_ATTEMPT=str(attempt))
                class Api:
                    def request(inner, path):
                        self.assertEqual(path, f"/actions/runs/200/attempts/{attempt}")
                        return run
                    def pages(inner, path, field):
                        self.assertEqual(path, f"/actions/runs/200/attempts/{attempt}/jobs")
                        self.assertEqual(field, "jobs")
                        return [row]
                with patch.object(publisher, "Api", return_value=Api()) as constructor, \
                        patch.object(authorize_compiler, "main_route_attempt",
                                     return_value=(run, {"id": 100, "run_attempt": 1}, HEAD), create=True), \
                        patch("time.time_ns", side_effect=[(START + 10000000) * 1000, (START + 10100000) * 1000]), \
                        patch("time.monotonic_ns", side_effect=[1000000000, 1100000000]):
                    self.assertEqual(publisher.physical_clock_data(env), 0)
                    constructor.assert_called_once_with(REPOSITORY, "", response_limit=128 * 1024)
                clock = publisher.sampling_tsv((root / "compiler-physical-job-clock.tsv").read_bytes())
                self.assertEqual(clock["kind"], "main")
                self.assertEqual(clock["run_attempt"], str(attempt))
                self.assertEqual(clock["policy_trusted_revision"], REVISION)
                self.assertEqual(clock["job_name"], publisher.COMPARE_JOBS["main"])


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
        self.assertIs(type(result["baseline_recipe_scope"]), str)
        self.assertEqual(result["baseline_recipe_scope"], "declared-supervised-ordinary-recipes")
        self.assertIs(result["historical_unwrapped_legacy_savings_assessed"], False)
        self.assertIn("Baseline recipe scope: declared-supervised-ordinary-recipes.", row["output"]["summary"])
        self.assertIn("Historical unwrapped legacy savings assessed: false.", row["output"]["summary"])
        self.assertEqual(result["qualification_state"], "unqualified")
        self.assertFalse(result["routine_profile_enabled"])
        self.assertFalse(result["default_activated"])
        self.assertIsNone(result["accounting"]["physical_job_wall_upper_us"])
        self.assertIsNone(result["accounting"]["native_owner_wall_us"])
        self.assertEqual(result["authenticated_attempt_history"][0]["state"], "incomplete")
        self.assertTrue(result["problems"])



class UtilityOwnedBoundaryTests(unittest.TestCase):
    def fixture(self, leg):
        plan = {"source_root": "/tmp/utility-source", "output_root": "/tmp/utility-output",
                "trusted_root": "/trusted", "trusted_revision": "e" * 40, "native_driver_sha256": "d" * 64,
                "pull_head": "f" * 40, "baseline_revision": "b" * 40, "baseline_tree": "c" * 40,
                "candidate_revision": "a" * 40, "candidate_tree": "9" * 40}
        host = {"native_driver": "/trusted/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-fixture",
                "bootstrap_marker_sha256": "8" * 64, "python": "/usr/bin/python3",
                "trusted_lab": "/trusted/tools/uarch_lab.py"}
        work, evidence = plan["output_root"] + "/" + leg + "-work", plan["output_root"] + "/" + leg + "-evidence"
        ownership = {"schema": "buster-compiler-utility-phases-v1", "owned_preflight": True,
                     "driver_path": host["native_driver"], "driver_sha256": plan["native_driver_sha256"],
                     "trusted_root": plan["trusted_root"], "trusted_revision": plan["trusted_revision"],
                     "candidate_root": plan["source_root"], "work_root": work, "evidence_root": evidence,
                     "binaries_root": work + "/bin", "directory": evidence + "/owned-phases",
                     "python_path": host["python"], "lab_path": host["trusted_lab"],
                     "bootstrap_marker_sha256": host["bootstrap_marker_sha256"],
                     "phases": [{"file": "0001.json"}]}
        row = {"leg": leg, "preparation_policy": "legacy-rebuild" if leg == "legacy" else "snapshot-v1"}
        receipt = {"preparation_policy": row["preparation_policy"], "phase_ownership": ownership}
        prefix = "utility/" + leg + "/ordinary/"
        files = {prefix + "receipt.json": encode(receipt), "utility/" + leg + "/lab/summary.json": b"{}",
                 "utility/" + leg + "/throughput/summary.json": b"{}",
                 "utility/" + leg + "/throughput/metadata.json": b"{}"}
        for suffix in ("", ".argv", ".stdout", ".stderr", ".bootstrap.complete"):
            files[prefix + "owned-phases/0001.json" + suffix] = b""
        return {"repository": REPOSITORY, "pull": "1", "request_id": "1", "run_id": "1", "plan": plan}, files, host, row, receipt

    def test_each_leg_requires_the_trusted_utility_schema_preflight_and_exact_pins(self):
        for leg in ("legacy", "snapshot"):
            for label in ("stripped", "downgraded", "preflight", "cross-leg", "driver", "python", "bootstrap"):
                authority, files, host, row, receipt = self.fixture(leg)
                owned = receipt["phase_ownership"]
                if label == "stripped":
                    receipt.pop("phase_ownership")
                elif label == "downgraded":
                    owned["schema"] = "buster-compiler-snapshot-phases-v1"
                elif label == "preflight":
                    owned["owned_preflight"] = 1
                elif label == "cross-leg":
                    owned["work_root"] = authority["plan"]["output_root"] + "/other-work"
                else:
                    owned[{"driver": "driver_path", "python": "python_path",
                           "bootstrap": "bootstrap_marker_sha256"}[label]] += "-changed"
                files["utility/" + leg + "/ordinary/receipt.json"] = encode(receipt)
                with self.subTest(leg=leg, control=label), patch.object(publisher, "decide") as decide, \
                        self.assertRaisesRegex(ValueError, "ownership schema"):
                    publisher.utility_ordinary_leg(authority, files, host, row, [])
                decide.assert_not_called()

    def test_each_leg_requires_the_complete_declared_raw_population_before_measurement_replay(self):
        for leg in ("legacy", "snapshot"):
            for label in ("missing", "extra", "duplicate"):
                authority, files, host, row, receipt = self.fixture(leg)
                prefix = "utility/" + leg + "/ordinary/"
                if label == "missing":
                    files.pop(prefix + "owned-phases/0001.json.stderr")
                elif label == "extra":
                    files[prefix + "owned-phases/9999.json"] = b""
                else:
                    receipt["phase_ownership"]["phases"].append({"file": "0001.json"})
                    files[prefix + "receipt.json"] = encode(receipt)
                with self.subTest(leg=leg, control=label), patch.object(publisher, "decide") as decide, \
                        self.assertRaisesRegex(ValueError, "owned-phase"):
                    publisher.utility_ordinary_leg(authority, files, host, row, [])
                decide.assert_not_called()

    def test_default_ordinary_legacy_remains_valid_and_utility_requires_explicit_complete_proof(self):
        import compiler_test as ordinary
        corpus = ordinary.corpus("regression")
        original = ordinary.receipt()
        self.assertEqual(publisher.decide(ordinary.EXPECTED, True, "success", original,
                         ordinary.summary(), "report-only", corpus)[0], "success")
        with patch.object(publisher, "validate_closure", wraps=publisher.validate_closure) as validate:
            self.assertEqual(publisher.decide(ordinary.EXPECTED, True, "success", original,
                             ordinary.summary(), "report-only", corpus,
                             expected_phase_schema="buster-compiler-utility-phases-v1")[0], "failure")
        self.assertEqual(validate.call_args.kwargs["expected_phase_schema"], "buster-compiler-utility-phases-v1")

    def test_trusted_utility_legacy_route_replays_full_owned_population_and_preserves_regression_data(self):
        import compiler_test as ordinary
        from compiler_owned_phase_test import population, corpus_bundle
        owned, raw = population(utility_policy="legacy-rebuild")
        current = ordinary.receipt()
        identity = dict(current["identity"], **owned.pop("identity"))
        identity["trusted_revision"] = owned["phase_ownership"]["trusted_revision"]
        current.update(owned, identity=identity)
        expected = dict(identity, **current["coverage"])
        corpus = ordinary.corpus("regression")
        corpus["closure"] = {"owned_phases": raw, "owned_throughput": corpus_bundle()}
        conclusion, unused_title, problems = publisher.decide(expected, True, "success", current,
            ordinary.summary(), "report-only", corpus, expected_phase_schema="buster-compiler-utility-phases-v1")
        self.assertEqual((conclusion, problems), ("success", []))
        for label in ("stripped", "downgraded", "missing-preflight"):
            changed = copy.deepcopy(current)
            if label == "stripped":
                changed.pop("phase_ownership")
            elif label == "downgraded":
                changed["phase_ownership"]["schema"] = "buster-compiler-snapshot-phases-v1"
            else:
                changed["phase_ownership"]["owned_preflight"] = False
            with self.subTest(control=label):
                self.assertEqual(publisher.decide(expected, True, "success", changed,
                    ordinary.summary(), "report-only", corpus,
                    expected_phase_schema="buster-compiler-utility-phases-v1")[0], "failure")



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
            compare += ["--main-owned-phases", "--main-profile", "compiler-compare-v1", "--closure-driver", host["native_driver"]]
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
                publisher.utility_ordinary_leg(authority, files, host, row, phases,
                    expected_phase_schema="buster-compiler-main-owned-phases-v1")
            # This guarded hosted-only proof replays data on the actual CPU.
            # The normal host and admission boundaries remain refusing above.
            with patch.object(publisher, "host_problem", return_value=""):
                results[leg] = publisher.utility_ordinary_leg(authority, files, host, row, phases,
                    expected_phase_schema="buster-compiler-main-owned-phases-v1")
            self.assertEqual(results[leg]["state"], "complete")
            self.assertEqual(results[leg]["series"]["complete_pairs"], 16)
            self.assertEqual(results[leg]["series"]["verdict"]["outcome"], "slower")
            self.assertEqual(results[leg]["throughput"]["complete_timed_samples"], 960)
            self.assertEqual(results[leg]["throughput"]["diagnostic_samples"], 0)
            ordinary = publisher.sampling_json(files, f"utility/{leg}/ordinary/receipt.json")
            self.assertEqual(ordinary["state"], "measured")
            self.assertEqual(ordinary["reasons"], [])
            ownership = ordinary["phase_ownership"]
            self.assertEqual(ownership["schema"], "buster-compiler-main-owned-phases-v1")
            self.assertIs(ownership["owned_preflight"], True)
            self.assertEqual(ownership["state"], "complete")
            self.assertEqual(sum(phase["kind"] == "run" for phase in ownership["phases"]),
                             11 if leg == "legacy" else 12)
            self.assertTrue(any(phase["kind"] == "capture" for phase in ownership["phases"]))
            ordinary_prefix = f"utility/{leg}/ordinary/"
            for label in ("stripped", "downgraded", "no-preflight", "missing-raw", "extra-raw",
                          "missing-core", "changed-command", "unproven-manager", "exit125", "budget"):
                changed = dict(files)
                altered = copy.deepcopy(ordinary)
                population = altered["phase_ownership"]
                first = next(phase for phase in population["phases"] if phase["kind"] == "run")
                stem = ordinary_prefix + "owned-phases/" + first["file"]
                if label == "stripped":
                    altered.pop("phase_ownership")
                elif label == "downgraded":
                    population["schema"] = "buster-compiler-snapshot-phases-v1"
                elif label == "no-preflight":
                    population["owned_preflight"] = False
                elif label == "missing-raw":
                    changed.pop(stem + ".stdout")
                elif label == "extra-raw":
                    changed[ordinary_prefix + "owned-phases/9999.json"] = b"{}"
                elif label == "missing-core":
                    population["phases"].remove(first)
                    population["count"] -= 1
                    for suffix in ("", ".argv", ".stdout", ".stderr", ".bootstrap.complete"):
                        changed.pop(stem + suffix)
                elif label == "budget":
                    first["bridge_wall_us"] = int(next(phase["wall_us"] for phase in phases
                        if phase["phase"] == leg + "-ordinary-compare")) + 1
                else:
                    native = json.loads(changed[stem])
                    if label == "changed-command":
                        from compiler_owned_phase import command_bytes
                        first["argv"] = ["/bin/true"]
                        changed[stem + ".argv"] = command_bytes(first["argv"])
                        native["command_sha256"] = hashlib.sha256(changed[stem + ".argv"]).hexdigest()
                    elif label == "unproven-manager":
                        native["manager_terminal"] = 0
                        native["cleanup_proven"] = False
                    else:
                        native["exit_status"] = 125 << 8
                        native["state"] = "failed"
                    changed[stem] = encode(native)
                    first["receipt_sha256"] = hashlib.sha256(changed[stem]).hexdigest()
                changed[ordinary_prefix + "receipt.json"] = encode(altered)
                with self.subTest(leg=leg, ownership_tamper=label), \
                        patch.object(publisher, "host_problem", return_value=""), self.assertRaises(ValueError):
                    publisher.utility_ordinary_leg(authority, changed, host, row, phases,
                        expected_phase_schema="buster-compiler-main-owned-phases-v1")

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
        print("UTILITY_NATIVE_DATA_REPLAY legs=2 owned_legs=2 ownership_negatives=20 selfhost_slower=2 corpus_samples=1920 "
              "raw_zip_bound=complete physical_job_cost=unavailable qualification=unqualified")


class UtilityNativeFailureReplay(unittest.TestCase):
    def test_real_escaped_lab_exit125_remains_failed_with_no_next_measurement(self):
        import io
        import stat
        import zipfile
        import compiler_owned_phase as contract
        directory_label = os.environ.get("BUSTER_UTILITY_NATIVE_NEGATIVE_EXPORT")
        if not directory_label:
            self.skipTest("run --utility-native-negative-export DIR after the hosted native failed Utility fixture")
        evidence = Path(directory_label) / "evidence"
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
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
            for name, data in members.items():
                item = zipfile.ZipInfo(name)
                item.external_attr = (stat.S_IFREG | 0o600) << 16
                zipped.writestr(item, data)
        with patch.object(zipfile.ZipFile, "extract", side_effect=AssertionError("extraction")), \
                patch.object(zipfile.ZipFile, "extractall", side_effect=AssertionError("extraction")):
            files = publisher.preparation_archive(packed.getvalue())
        self.assertEqual(files, members)
        marker = publisher.sampling_json(files, "fixture-plan.json")
        self.assertEqual(marker["schema"], "buster-compiler-closure-utility-fixture-v1")
        self.assertIs(marker["diagnostic_fixture"], True)
        self.assertIs(marker["physical_qualification"], False)
        self.assertEqual(marker["qualification_state"], "unqualified")
        leg = marker["diagnostic_lab_case"]
        self.assertIn(leg, ("legacy", "snapshot"))
        expected = marker["expected"]
        with self.assertRaisesRegex(ValueError, "diagnostic"):
            publisher.utility_validate(None, {}, files)
        self.assertNotIn("physical-job-clock.tsv", files)
        self.assertFalse(any(name.rsplit("/", 1)[-1] == "cleanup-uncertain" for name in files))
        terminal = publisher.sampling_tsv(files["utility.tsv"])
        self.assertEqual(terminal["process_state"], "failed")
        self.assertEqual(terminal["complete_legs"], "0" if leg == "legacy" else "1")
        self.assertEqual(terminal["cleanup_proven"], "true")
        self.assertEqual(terminal["exported"], "false")
        self.assertEqual(terminal["net_utility"], "unavailable")
        owner = publisher.sampling_tsv(files["owner.tsv"])
        self.assertEqual(owner["process_state"], "failed")
        for key in ("timed_out", "cleanup_failed", "cancelled"):
            self.assertEqual(owner[key], "0")
        for key in ("manager_launch_attempted", "manager_wait_observed", "manager_cleanup_proven"):
            self.assertEqual(owner[key], "1")
        phases = publisher.sampling_tsv(files["controller.tsv"], True)
        failed = [phase for phase in phases if phase["state"] != "complete"]
        self.assertEqual(len(failed), 1)
        self.assertEqual(failed[0], phases[-1])
        self.assertEqual(failed[0]["phase"], leg + "-ordinary-compare")
        self.assertNotEqual(failed[0]["exit_status"], "0")
        self.assertEqual(len(phases), 10 if leg == "legacy" else 15)
        if leg == "legacy":
            self.assertFalse(any(phase["phase"].startswith("snapshot-") for phase in phases))
            self.assertFalse(any(name.startswith("utility/snapshot/") for name in files))
        prefix = "utility/" + leg + "/ordinary/"
        ordinary = publisher.sampling_json(files, prefix + "receipt.json")
        self.assertEqual(ordinary["state"], "failed")
        self.assertTrue(ordinary["reasons"])
        ownership = ordinary["phase_ownership"]
        self.assertEqual(ownership["schema"], "buster-compiler-main-owned-phases-v1")
        self.assertIs(ownership["owned_preflight"], True)
        self.assertEqual(ownership["state"], "failed")
        self.assertEqual(ownership["driver_sha256"], expected["native_driver_sha256"])
        rows = ownership["phases"]
        labs = [row for row in rows if row["phase"] == "lab" and row["kind"] == "run"]
        self.assertEqual(len(labs), 1)
        lab = labs[0]
        self.assertEqual(lab, rows[-1])
        self.assertFalse(any(row["phase"] in ("throughput", "validate") for row in rows))
        self.assertFalse(any(name.startswith("utility/" + leg + "/throughput/") or
                             name.startswith(prefix + "throughput/") for name in files))
        stem = prefix + "owned-phases/" + lab["file"]
        native = contract.read_record(files[stem])
        stdout, stderr = files[stem + ".stdout"], files[stem + ".stderr"]
        self.assertEqual(files[stem + ".argv"], contract.command_bytes(lab["argv"]))
        self.assertEqual(hashlib.sha256(files[stem]).hexdigest(), lab["receipt_sha256"])
        self.assertEqual(native["state"], "failed")
        self.assertEqual(native["exit_status"], 125 << 8)
        self.assertTrue(os.WIFEXITED(native["exit_status"]))
        self.assertEqual(os.waitstatus_to_exitcode(native["exit_status"]), 125)
        self.assertIs(native["cleanup_proven"], True)
        self.assertGreaterEqual(native["cleanup_signalled"], 1)
        self.assertGreaterEqual(native["cleanup_reaped"], 1)
        for key in ("launch_attempted", "manager_launched", "manager_terminal"):
            self.assertEqual(native[key], 1)
        for key in ("timed_out", "cancelled", "capture_failed", "output_truncated",
                    "reservation_retained", "ownership_lost", "tree_cleanup_failed"):
            self.assertEqual(native[key], 0)
        self.assertIn(b"COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_LAB125", stdout)
        self.assertRegex(stdout.decode("utf-8"), r"parent_pid=[1-9][0-9]*")
        self.assertRegex(stdout.decode("utf-8"), r"escaped_pid=[1-9][0-9]*")
        self.assertIn(b"term_ignored=1", stdout)
        self.assertIn(b"exit=125", stdout)
        self.assertIn(b"physical_qualification=false", stdout)
        args = (native, lab["argv"], lab["cwd"], lab["timeout"], ownership["driver_sha256"], stdout, stderr)
        self.assertEqual(contract.validate_record(*args, nominal=False,
                         receipt_path=ownership["directory"] + "/" + lab["file"]), [])
        self.assertTrue(contract.validate_record(*args, nominal=True,
                        receipt_path=ownership["directory"] + "/" + lab["file"]))
        self.assertEqual(contract.validate_bootstrap(native, files[stem + ".bootstrap.complete"], ownership), [])
        bundle = {row["file"]: {label: files.get(prefix + "owned-phases/" + row["file"] + suffix)
                  for label, suffix in (("receipt", ""), ("command", ".argv"), ("stdout", ".stdout"),
                                        ("stderr", ".stderr"), ("bootstrap", ".bootstrap.complete"))}
                  for row in rows}
        self.assertTrue(publisher.validate_closure(ordinary, {"owned_phases": bundle, "owned_throughput": {}},
                        expected_policy=ordinary["preparation_policy"],
                        expected_phase_driver_sha256=expected["native_driver_sha256"],
                        expected_trusted_revision=expected["trusted_revision"], require_owned_phases=True,
                        expected_phase_schema="buster-compiler-main-owned-phases-v1", expected_profile="compiler-compare-v1",
                        require_owned_preflight=True))
        print(f"UTILITY_NATIVE_FAILED_DATA_REPLAY leg={leg} raw_exit=32000 exit=125 escaped_cleanup_proven=1 "
              "no_next_measurement=1 raw_zip_bound=retained publication_refused=1 qualification=unqualified")




class MainOwnedNativeFortyReplay(unittest.TestCase):
    def test_actual_native_stock_lab_fixed40_raw_population_and_owned_proof(self):
        import io
        import stat
        import zipfile
        import compiler_owned_phase as contract
        from compiler_preparation import parse_argv
        label = os.environ.get("BUSTER_MAIN_OWNED_NATIVE_EXPORT")
        if not label:
            self.skipTest("run --main-owned-native-export DIR after the actual stock-lab native fixed40 fixture")
        directory, members = Path(label), {}
        evidence = directory / "evidence"
        for path in evidence.rglob("*"):
            self.assertFalse(path.is_symlink(), str(path))
            if path.is_dir():
                continue
            self.assertTrue(path.is_file(), str(path))
            self.assertLessEqual(path.stat().st_size, publisher.ANALYZER_MEMBER_LIMIT)
            members[path.relative_to(evidence).as_posix()] = path.read_bytes()
        self.assertLessEqual(len(members), publisher.MAIN_ARCHIVE_FILE_LIMIT)
        self.assertLessEqual(sum(map(len, members.values())), publisher.ARTIFACT_LIMIT)
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, raw in members.items():
                item = zipfile.ZipInfo(name)
                item.external_attr = (stat.S_IFREG | 0o600) << 16
                archive.writestr(item, raw)
        with patch.object(zipfile.ZipFile, "extract", side_effect=AssertionError("extraction")), \
                patch.object(zipfile.ZipFile, "extractall", side_effect=AssertionError("extraction")):
            files = publisher.sampling_archive(packed.getvalue(), member_limit=publisher.MAIN_ARCHIVE_FILE_LIMIT)
        self.assertEqual(files, members)
        marker = publisher.sampling_json(files, "fixture-plan.json")
        for key, value in (("schema", "buster-compiler-main-forty-fixture-v1"), ("diagnostic_fixture", True),
                           ("actual_lab", True), ("diagnostic_perf", "unavailable"), ("physical_qualification", False), ("qualification_state", "unqualified"),
                           ("main_profile", "compiler-main-40pairs-v1"), ("preparation_policy", "snapshot-v1"),
                           ("phase_schema", publisher.MAIN_PHASE_SCHEMA),
                           ("corpus_data", "fixed-diagnostic-full-original-profile")):
            self.assertIs(type(marker[key]), type(value))
            self.assertEqual(marker[key], value)
        expected = marker["expected"]
        route = {"main_owned": True, "main_profile": marker["main_profile"], "main_preparation_policy": "snapshot-v1",
                 "main_phase_schema": publisher.MAIN_PHASE_SCHEMA, "main_measurement_revision": expected["trusted_revision"],
                 "driver_sha256": expected["native_driver_sha256"], "python_path": expected["python"]}
        with self.assertRaisesRegex(ValueError, "diagnostic"):
            publisher.main_owned_files(files, route)
        self.assertNotIn("physical-job-clock.tsv", files)
        self.assertFalse(any(name.rsplit("/", 1)[-1] == "cleanup-uncertain" for name in files))
        exported = {name: raw for name, raw in files.items()
                    if name.startswith("main40/") or name in ("baseline-adapter.c", "candidate-adapter.c", "source-workload.c", "diagnostic-tools.tsv")}
        publisher.utility_export_manifest(files["export.manifest.tsv"], exported,
                                          expected_schema="BUSTER_COMPILER_MAIN_FORTY_EXPORT_V1")
        for member, pin in (("baseline-adapter.c", "baseline_adapter_sha256"),
                            ("candidate-adapter.c", "candidate_adapter_sha256"), ("source-workload.c", "workload_sha256")):
            self.assertEqual(hashlib.sha256(files[member]).hexdigest(), expected[pin])
        for path, pin in (("python", "python_sha256"), ("trusted_lab", "trusted_lab_sha256"),
                          ("native_driver", "native_driver_sha256")):
            self.assertEqual(hashlib.sha256(Path(expected[path]).read_bytes()).hexdigest(), expected[pin])
        diagnostic_tools = files["diagnostic-tools.tsv"].decode("ascii").splitlines()
        tool_names = ("sh,bash,env,git,clang,clang++,ld,ld.lld,ld.gold,lld,ninja,cmake,python3,tcc,taskset,uname,lscpu,true,cat,mkdir,chmod,cp,mv,rm,readlink,realpath,dirname,basename,sed,grep,cut,tr,cmp,ls,head,tail,stat,tee,sort,awk,date,wc,find,xargs,touch,sleep,make,gcc,cc,g++,c++,ar,ranlib,nm,objdump,readelf,as,ldd,sha256sum,du,pwd,ln,printf,install,getconf,nproc").split(",")
        tool_root = directory.resolve() / "diagnostic-bin"
        self.assertEqual(diagnostic_tools[:3], ["BUSTER_MAIN_FORTY_DIAGNOSTIC_TOOLS_V1",
                                               "perf\tunavailable\t-\t-", "PATH\t" + str(tool_root) + "\t-\t-"])
        self.assertEqual(len(diagnostic_tools), len(tool_names) + 3)
        self.assertFalse((tool_root / "perf").exists())
        self.assertFalse((tool_root / "perf").is_symlink())
        required_tools = {"sh", "bash", "env", "git", "clang", "ld", "ninja", "cmake", "python3", "taskset", "uname", "lscpu", "true"}
        for raw, name in zip(diagnostic_tools[3:], tool_names):
            fields = raw.split("\t")
            self.assertEqual(len(fields), 4)
            self.assertEqual(fields[:2], ["tool", name])
            target, digest = fields[2:]
            if target == "-":
                self.assertEqual(digest, "-")
                self.assertNotIn(name, required_tools)
                self.assertFalse((tool_root / name).exists())
                self.assertFalse((tool_root / name).is_symlink())
            else:
                self.assertEqual(str(Path(target).resolve(strict=True)), target)
                self.assertTrue(Path(target).is_file())
                self.assertTrue((tool_root / name).is_symlink())
                self.assertEqual((tool_root / name).resolve(strict=True), Path(target))
                hasher = hashlib.sha256()
                with Path(target).open("rb") as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        hasher.update(chunk)
                self.assertEqual(hasher.hexdigest(), digest)
        ordinary = publisher.sampling_json(files, "main40/ordinary/receipt.json")
        ownership = ordinary["phase_ownership"]
        self.assertEqual(ordinary["state"], "measured")
        self.assertEqual(ordinary["reasons"], [])
        self.assertEqual(ownership["schema"], publisher.MAIN_PHASE_SCHEMA)
        self.assertIs(ownership["owned_preflight"], True)
        self.assertEqual(sum(row["kind"] == "run" for row in ownership["phases"]), 12)
        actual_cpu = ordinary["host"]["cpu_model"]
        self.assertNotIn("9700X", actual_cpu)
        self.assertTrue(publisher.host_problem(ordinary))
        roots = {"candidate_root": expected["root"], "trusted_root": expected["trusted_root"],
                 "work_root": expected["output"] + "/main40-work",
                 "evidence_root": expected["output"] + "/main40-evidence"}
        complete = dict(files)
        for name, raw in files.items():
            if name.startswith(("main40/lab/", "main40/throughput/")):
                duplicate = "main40/ordinary/" + name[len("main40/"):]
                if duplicate in complete:
                    self.assertEqual(complete[duplicate], raw)
                complete[duplicate] = raw
        receipt, summary, throughput = publisher.main_owned_files(complete, route,
            prefix="main40/ordinary/", expected_roots=roots, diagnostic=True, expected_cpu_model=actual_cpu)
        self.assertEqual(throughput["main_owned_lab"]["complete_pairs"], 40)
        self.assertEqual(summary["plan"]["pairs"], 40)
        self.assertEqual(summary["plan"]["complete_pairs"], 40)
        self.assertEqual(summary["plan"]["reason"], "--pairs 40")
        records = publisher.sampling_json(files, "main40/lab/pairs.json", False)
        self.assertEqual(len(records), 80)
        self.assertIs(summary["counters"]["perf_stat"], False)
        self.assertTrue(all(row.get("counters") is False for row in records))
        self.assertEqual({row["pair"] for row in records}, set(range(1, 41)))
        references = [files["main40/" + role + "-reference.bin"] for role in ("baseline", "candidate")]
        self.assertTrue(all(raw.startswith(b"\x7fELF") for raw in references))
        self.assertIs(summary["outputs_identical"], references[0] == references[1])
        provider = Path(expected["trusted_root"]) / "tools/bench_direct/compiler_closure_utility_diagnostic.py"
        self.assertEqual(str(provider.resolve(strict=True)), str(provider))
        provider_raw = provider.read_bytes()
        self.assertEqual(hashlib.sha256(provider_raw).hexdigest(),
                         "1dc5136f7458718aae9afb8499367c68be32c52793aaee4e49f6983f13801be6")
        command = [expected["python"], "-B", str(provider),
                   "--candidate", expected["root"], "--lab", expected["trusted_lab"], "--work", roots["work_root"],
                   "--evidence", roots["evidence_root"], "--summary", expected["output"] + "/main40.md",
                   "--closure-policy", "snapshot-v1", "--main-owned-phases", "--main-profile", "compiler-main-40pairs-v1",
                   "--closure-driver", expected["native_driver"], "--mode", "main", "--repository", REPOSITORY,
                   "--ref", "refs/heads/main", "--pull", "1", "--pull-head", expected["pull_head"],
                   "--base", expected["base"], "--base-tree", expected["base_tree"], "--head", expected["head"],
                   "--head-tree", expected["head_tree"], "--trusted-revision", expected["trusted_revision"],
                   "--request-run-id", "1", "--run-id", "1", "--run-attempt", "1"]
        entry = [expected["native_driver"], "compiler_profile_qualification", "--self-test-main-forty-native-export",
                 str(directory), expected["python"], expected["trusted_lab"], "--owned-main-forty-fixture-worker"]
        for stem, argv, timeout in (("entry.json", entry, 480), ("manager.json", command, 420)):
            native = contract.read_record(files[stem])
            self.assertEqual(parse_argv(files[stem + ".argv"]), argv)
            self.assertEqual(contract.validate_record(native, argv, expected["trusted_root"], timeout,
                expected["native_driver_sha256"], files[stem + ".stdout"], files[stem + ".stderr"],
                receipt_path=str(evidence / stem)), [])
            self.assertEqual(contract.validate_bootstrap(native, files[stem + ".bootstrap.complete"], ownership), [])
        manager = contract.read_record(files["manager.json"])
        self.assertLessEqual(sum(row["bridge_wall_us"] for row in ownership["phases"]), manager["duration_us"])
        for name, mutate in (("pairs.json", lambda rows: rows.pop()),
                             ("pairs.json", lambda rows: rows.append(copy.deepcopy(rows[-1]))),
                             ("compare.json", lambda row: row["config"].update(seed=1)),
                             ("compare.json", lambda row: row["variants"]["a"].update(sha256="f" * 64)),
                             ("summary.json", lambda row: row["metrics"]["wall"].update(ratio=99))):
            altered = dict(complete)
            member = "main40/ordinary/lab/" + name
            value = json.loads(altered[member])
            mutate(value)
            altered[member] = encode(value)
            with self.subTest(member=name, mutation=mutate), self.assertRaises(ValueError):
                publisher.main_owned_files(altered, route, prefix="main40/ordinary/", expected_roots=roots,
                                           diagnostic=True, expected_cpu_model=actual_cpu)
        print("MAIN_OWNED_NATIVE_DATA_REPLAY actual_lab=1 complete_pairs=40 raw_members=80 owned_core=12 "
              "negative_controls=5 physical_job_cost=unavailable qualification=unqualified")



def retained_transport(authority, kind):
    """Synthetic byte joins for the reader; native type/ownership tests are separate."""
    authority = dict(authority)
    request = dict(authority.get("request", {}))
    request.setdefault("id", int(authority.get("request_id", "100")))
    request.setdefault("run_attempt", 1)
    request.setdefault("head_sha", HEAD)
    executor = dict(authority["executor"])
    executor.setdefault("id", int(authority.get("run_id", "200")))
    executor.setdefault("run_attempt", 1)
    authority.update(request=request, executor=executor, request_id=str(request["id"]), run_id=str(executor["id"]),
                     head=request["head_sha"])
    current_facts = {"pull_state": "closed", "request_head": request["head_sha"], "trusted_revision": executor["head_sha"],
                     "owner_login": OWNER["login"], "owner_id": str(OWNER["id"]), "parent_count": "1",
                     "fresh_parent_0": "fixed-original-request-marker", "fresh_parent_1": "-"}
    original_facts = dict(current_facts, pull_state="open")
    encode = lambda values: b"".join((key + "\t" + value + "\n").encode("ascii") for key, value in values.items())
    names = ("request.txt", "allowlist.tsv", "facts.tsv", "history.tsv",
             *(("freeze.tsv", "parent-freeze.tsv", "acquisition-plan.tsv") if kind == "sampling" else ("plan.tsv",)))
    current = {name: name.encode("ascii") + b"\n" for name in names}
    current["request.txt"] = (current_facts["fresh_parent_0"] + "\n").encode("ascii")
    current["facts.tsv"] = encode(current_facts)
    original = dict(current, **{"facts.tsv": encode(original_facts)})
    current_sha, original_sha = (hashlib.sha256(records["facts.tsv"]).hexdigest() for records in (current, original))
    proof = {"schema": "synthetic-reader-api-proof", "repository": REPOSITORY,
             "policy_revision": executor["head_sha"], "request_run_id": str(request["id"]), "request_run_attempt": "1",
             "request_head": request["head_sha"], "executor_run_id": str(executor["id"]), "executor_run_attempt": "1",
             "executor_head": executor["head_sha"]}
    hashed = {"allowlist.tsv": "allowlist_sha256", "facts.tsv": "facts_sha256", "history.tsv": "history_sha256",
              "freeze.tsv" if kind == "sampling" else "plan.tsv": "freeze_sha256"}
    if kind == "sampling":
        hashed.update({"parent-freeze.tsv": "parent_freeze_sha256", "acquisition-plan.tsv": "acquisition_sha256"})
    proof.update({label: hashlib.sha256(current[name]).hexdigest() for name, label in hashed.items()})
    api_raw = encode(proof)
    admitted = dict(authority["admitted"], **{kind + "_historical_api_sha256": hashlib.sha256(api_raw).hexdigest(),
                                            kind + "_policy_revision": executor["head_sha"],
                                            ("sampling_freeze_sha256" if kind == "sampling" else kind + "_plan_sha256"): proof["freeze_sha256"]})
    if kind == "sampling" and admitted.get("sampling_phase") != "acquire":
        admitted["sampling_campaign_parent"] = proof["parent_freeze_sha256"]
        authority["freeze"] = {"campaign_parent": proof["parent_freeze_sha256"]}
    return dict(authority, admitted=admitted, facts=current_facts, raw=current, raw_original=original,
                historical_records={"api": api_raw}, native_api_proof=proof,
                historical_original_facts_binding={"historical_original_facts_valid": "true", "current_facts_sha256": current_sha,
                    "original_facts_sha256": original_sha, "historical_execution_authority": "false", "qualification": "unqualified"})


class HistoricalSamplingDataTests(unittest.TestCase):
    def test_original_attempt_and_latest_rerun_guard(self):
        original = {"id": 200, "run_attempt": 1, "head_sha": REVISION}
        class Api:
            def __init__(self, latest=1):
                self.latest, self.calls = latest, []
            def request(self, path):
                self.calls.append(path)
                if path == "/actions/runs/200/attempts/1":
                    return original
                if path == "/actions/runs/200":
                    return dict(original, run_attempt=self.latest)
                raise AssertionError(path)
        api = Api()
        self.assertIs(publisher.sampling_review_attempt(api, "200"), original)
        self.assertEqual(api.calls, ["/actions/runs/200/attempts/1", "/actions/runs/200"])
        with self.assertRaises(ValueError):
            publisher.sampling_review_attempt(Api(2), "200")
        for value in (True, 200, "0200", "0"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                publisher.sampling_review_attempt(Api(), value)

    def test_historical_review_never_accepts_physical_admission(self):
        self.assertFalse(publisher.sampling_reviewed({}))
        self.assertTrue(publisher.sampling_reviewed({"historical_review": True,
            "admitted": {"sampling_historical_valid": "true", "sampling_historical_execution_authority": "false",
                         "sampling_historical_qualification": "unqualified"}}))
        for altered in ({"historical_review": 1}, {"historical_review": True, "admitted": {}},
                        {"historical_review": True, "admitted": {"sampling_historical_valid": True}},
                        {"historical_review": True, "admitted": {"sampling_historical_valid": "true",
                                                               "sampling_admitted": "false"}}):
            with self.subTest(altered=altered), self.assertRaises(ValueError):
                publisher.sampling_reviewed(altered)

    def test_original_acquisition_uses_its_own_policy_and_facts(self):
        request = {"id": 100, "run_attempt": 1, "head_sha": HEAD}
        executor = execution()
        executor.update(status="completed", conclusion="success")
        context = {"sha256": "c" * 64, "revision": "d" * 40, "phase": "pilot", "packet": 0}
        previous = {"phase": "acquire", "packet": "0", "campaign": context["sha256"],
                    "freeze_revision": context["revision"], "state": "complete",
                    "request_run_attempt": "1", "executor_run_attempt": "1",
                    "request_run_id": "100", "executor_run_id": "200"}
        old = {"historical_review": True, "admitted": {"sampling_historical_valid": "true", "sampling_historical_execution_authority": "false",
                             "sampling_historical_qualification": "unqualified"},
               "history": [], "request": request, "executor": executor, "repository": REPOSITORY,
               "head": HEAD, "request_id": "100", "run_id": "200", "acquisition_plan_bytes": b"original\n",
               "facts": {"pull_state": "closed", "policy": "original-acquisition"}}
        current = dict(old, history=[previous], facts={"pull_state": "open", "policy": "later"},
                       historical_acquisition=old)
        class Api:
            def request(self, path):
                if path in ("/actions/runs/100/attempts/1", "/actions/runs/100"):
                    return request
                if path in ("/actions/runs/200/attempts/1", "/actions/runs/200"):
                    return executor
                raise AssertionError(path)
        prepared, acquired = {"files": {}}, {"raw": True}
        with patch.object(publisher, "sampling_plan", return_value=dict(context, phase="acquire", packet=0)), \
                patch.object(publisher, "sampling_read_artifact", return_value=({}, {})) as read, \
                patch.object(publisher, "bind_historical_transport", side_effect=lambda authority, files, kind: authority), \
                patch.object(publisher, "sampling_prepared", return_value=prepared), \
                patch.object(publisher, "sampling_host", return_value={}), \
                patch.object(publisher, "sampling_phase_proofs", return_value=({"physical_packet_wall_us": "1"}, {})), \
                patch.object(publisher, "sampling_host_job", return_value={}), \
                patch.object(publisher, "sampling_job_accounting", return_value={}), \
                patch.object(publisher, "physical_clock_binding", return_value={}), \
                patch.object(publisher, "sampling_acquisition", return_value=acquired), \
                patch.object(publisher, "sampling_source_hashes"):
            self.assertEqual(publisher.sampling_prior_acquisition(Api(), current, context), (prepared, acquired))
            self.assertIs(read.call_args.args[1], old)
            self.assertEqual(read.call_args.args[1]["facts"]["policy"], "original-acquisition")
            with self.assertRaises(ValueError):
                publisher.sampling_prior_acquisition(Api(), dict(current, historical_acquisition=None), context)
            with self.assertRaises(ValueError):
                publisher.sampling_prior_acquisition(Api(), dict(current, historical_acquisition=dict(old, history=[previous])), context)

    def test_original_open_transport_preserves_current_closed_api_observation(self):
        for kind in ("sampling", "preparation", "utility"):
            with self.subTest(kind=kind):
                authority = {"historical_review": True, "executor": {"head_sha": REVISION},
                             "admitted": {kind + "_historical_valid": "true", kind + "_historical_execution_authority": "false",
                                          kind + "_historical_qualification": "unqualified"}}
                authority = retained_transport(authority, kind)
                before = copy.deepcopy(authority)
                self.assertEqual(publisher.historical_transport(authority, authority["raw_original"], kind), authority["raw_original"])
                self.assertEqual(authority, before)
                self.assertEqual(authority["facts"]["pull_state"], "closed")
                self.assertEqual(publisher.sampling_tsv(authority["raw_original"]["facts.tsv"])["pull_state"], "open")
                for mutate in ("binding", "api_hash", "api_bytes", "facts_sha", "current_facts", "missing_original", "extra_member", "state", "owner", "head", "policy", "history"):
                    changed = copy.deepcopy(authority)
                    files = dict(changed["raw_original"])
                    if mutate == "binding":
                        changed["historical_original_facts_binding"]["historical_original_facts_valid"] = True
                    elif mutate == "api_hash":
                        changed["admitted"][kind + "_historical_api_sha256"] = "f" * 64
                    elif mutate == "api_bytes":
                        changed["historical_records"]["api"] = changed["historical_records"]["api"].decode()
                    elif mutate == "facts_sha":
                        changed["native_api_proof"]["facts_sha256"] = "f" * 64
                    elif mutate == "current_facts":
                        changed["facts"]["owner_id"] = "1"
                    elif mutate == "missing_original":
                        del changed["raw_original"]
                    elif mutate == "extra_member":
                        changed["raw_original"]["script.py"] = b"never executed"
                    elif mutate == "history":
                        files["history.tsv"] += b"future attempt\n"
                        changed["raw_original"]["history.tsv"] = files["history.tsv"]
                    else:
                        facts = publisher.sampling_tsv(files["facts.tsv"])
                        key = {"state": "pull_state", "owner": "owner_id", "head": "request_head", "policy": "trusted_revision"}[mutate]
                        facts[key] = {"state": "closed", "owner": "1", "head": "c" * 40, "policy": "d" * 40}[mutate]
                        raw = b"".join((name + "\t" + value + "\n").encode("ascii") for name, value in facts.items())
                        changed["raw_original"]["facts.tsv"] = files["facts.tsv"] = raw
                        # Even a coherently rebound duplicated original hash cannot change the immutable identity.
                        changed["historical_original_facts_binding"]["original_facts_sha256"] = hashlib.sha256(raw).hexdigest()
                    with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                        publisher.historical_transport(changed, files, kind)
                for name in authority["raw"]:
                    changed = copy.deepcopy(authority)
                    changed["raw"][name] += b"both-copies-changed\n"
                    changed["raw_original"][name] = changed["raw"][name]
                    with self.subTest(both_copies=name), self.assertRaises(ValueError):
                        publisher.historical_transport(changed, changed["raw_original"], kind)
        live = {"raw": {"facts.tsv": b"original live bytes"}}
        self.assertIs(publisher.historical_transport(live, {}, "utility"), live["raw"])
        self.assertEqual(publisher.historical_transport({}, {}, "sampling"), {})

    def test_retained_transport_rejoins_actual_member_manifest_without_execution(self):
        authority, result, artifact = CampaignFactsDataTests().authority_and_result("utility", 0, 45)
        record = json.loads(publisher.campaign_json(publisher.campaign_transport_record(authority, "utility")))
        self.assertNotEqual(list(record["native_api_proof"]), list(authority["native_api_proof"]))
        restored = publisher.campaign_restore_transport(authority, record, artifact["verified_member_manifest"], "utility")
        self.assertEqual(restored["raw_original"], authority["raw_original"])
        self.assertEqual(restored["facts"]["pull_state"], "closed")
        for name in record["original"]:
            changed = copy.deepcopy(record)
            changed["original"][name] = base64.b64encode(b"changed").decode()
            with self.subTest(name=name), self.assertRaises(ValueError):
                publisher.campaign_restore_transport(authority, changed, artifact["verified_member_manifest"], "utility")
        with self.assertRaises(ValueError):
            publisher.campaign_restore_transport(authority, record, b"unrelated member manifest\n", "utility")

    def test_verified_archive_identity_is_opt_in_and_type_sensitive(self):
        payload = b"immutable ZIP bytes"
        files = {"b/raw": b"second", "a/raw": b"first"}
        digest = hashlib.sha256(payload).hexdigest()
        row = {"id": 400, "size_in_bytes": len(payload), "digest": "sha256:" + digest}
        self.assertIs(publisher.campaign_artifact_identity(payload, row, files, False), row)
        retained = publisher.campaign_artifact_identity(payload, row, files, True)
        manifest = b"".join((name + "\t" + hashlib.sha256(raw).hexdigest() + "\t" + str(len(raw)) + "\n").encode()
                            for name, raw in sorted(files.items()))
        self.assertEqual(retained["verified_zip_sha256"], digest)
        self.assertEqual(retained["verified_zip_bytes"], len(payload))
        self.assertEqual(retained["verified_member_manifest_sha256"], hashlib.sha256(manifest).hexdigest())
        self.assertEqual(retained["verified_member_count"], 2)
        for altered in (dict(row, digest=None), dict(row, digest="sha256:" + "f" * 64),
                        dict(row, size_in_bytes=len(payload) + 1), dict(row, size_in_bytes=float(len(payload)))):
            with self.subTest(altered=altered), self.assertRaises(ValueError):
                publisher.campaign_artifact_identity(payload, altered, files, True)
        with self.assertRaises(ValueError):
            publisher.campaign_artifact_identity(payload, row, files, 1)



class CampaignTerminalDataTests(unittest.TestCase):
    def authority(self, phase="confirm", packet=0, index=4, *, no_executor=False, job=None, artifact=None, padding=0):
        from sampling_qualification_receipt import schedule
        authority, unused_result, unused_artifact = CampaignFactsDataTests().authority_and_result(phase, packet, index)
        authority["request"]["conclusion"] = "failure"
        prefix = "sampling" if phase in ("acquire", "pilot", "confirm") else phase
        execution = authority["executor"]
        execution["conclusion"] = "failure"
        state = "cancelled" if job and job["conclusion"] == "cancelled" else "failed"
        context = "-"
        if no_executor:
            context, state = "9" * 40, "hostless"
            authority.update(executor=None, run_id="-")
            authority["raw"]["allowlist.tsv"] = b""
            authority["facts"]["trusted_revision"] = "-"
            authority["raw"]["facts.tsv"] = b"".join((key + "\t" + value + "\n").encode("ascii") for key, value in authority["facts"].items())
            for key in ("policy_revision", "executor_run_id", "executor_run_attempt", "executor_head"):
                authority["native_api_proof"][key] = "-"
        raw, proof = authority["raw"], authority["native_api_proof"]
        hashed = {"allowlist.tsv": "allowlist_sha256", "facts.tsv": "facts_sha256", "history.tsv": "history_sha256",
                  "freeze.tsv" if prefix == "sampling" else "plan.tsv": "freeze_sha256"}
        if prefix == "sampling":
            hashed.update({"parent-freeze.tsv": "parent_freeze_sha256", "acquisition-plan.tsv": "acquisition_sha256"})
        proof.update({label: hashlib.sha256(raw[name]).hexdigest() for name, label in hashed.items()})
        api_raw = b"".join((key + "\t" + value + "\n").encode("ascii") for key, value in proof.items())
        native = dict(authority["admitted"])
        native.update({prefix + "_historical_terminal_valid": "true", prefix + "_historical_valid": "false",
                       prefix + "_historical_measurement_valid": "false", prefix + "_historical_execution_authority": "false",
                       prefix + "_historical_qualification": "unqualified", prefix + "_policy_revision": "-" if no_executor else execution["head_sha"],
                       prefix + "_historical_api_sha256": hashlib.sha256(api_raw).hexdigest()})
        family = schedule(phase, packet)["family"] if prefix == "sampling" else phase
        observations = {"/actions/runs/" + authority["request_id"] + "/attempts/1": authority["request"]}
        if not no_executor:
            observations["/actions/runs/" + authority["run_id"] + "/attempts/1"] = execution
        if job is not None:
            observations["/actions/runs/" + authority["run_id"] + "/attempts/1/jobs?per_page=100&page=1"] = {"jobs": [job]}
        expected_artifact_name = "buster-9700x-" + prefix + "-" + authority["head"] + "-1"
        inventory = [artifact] if artifact is not None else []
        artifact_pages = []
        if not no_executor:
            path = "/actions/runs/" + authority["run_id"] + "/artifacts?per_page=100&page=1"
            artifact_pages = [path]
            observations[path] = {"total_count": len(inventory), "artifacts": inventory}
        if padding:
            observations["/bounded-retained-api-test-observation"] = "x" * padding
        artifact_selection = {"expected_name": expected_artifact_name, "executor_run": authority["run_id"],
                              "complete": True, "pages": artifact_pages, "matching_ids": [artifact["id"]] if artifact else [],
                              "selected_id": artifact["id"] if artifact else None}
        selection = {"revision": context if no_executor else execution["head_sha"],
                     "allowlist_path": "docs/compiler-sampling-allowlist-v1.tsv", "allowlist_sha256": proof["allowlist_sha256"],
                     "freeze_revision": native["sampling_freeze_revision" if prefix == "sampling" else prefix + "_plan_revision"],
                     "freeze_sha256": proof["freeze_sha256"], "parent_freeze_revision": "-", "parent_freeze_sha256": proof.get("parent_freeze_sha256", "-"),
                     "acquisition_sha256": proof.get("acquisition_sha256", "-"), "history_since": "-"}
        envelope = publisher.campaign_json({"schema": "buster-compiler-historical-terminal-api-envelope-v1", "repository": REPOSITORY,
            "kind": prefix, "request_run": authority["request_id"], "executor_run": authority["run_id"], "context_revision": context,
            "api_observations": observations, "native_api_proof": proof, "review_context_selection": selection,
            "artifact_inventory_selection": artifact_selection}, max_bytes=publisher.CAMPAIGN_REPLAY_LIMIT)
        terminal = {"schema": "buster-compiler-historical-terminal-v1", "kind": prefix,
                    "phase": "qualify" if phase == "preparation" else phase, "packet": str(packet), "family": family,
                    "executor_inventory_count": "0" if no_executor else "1", "selected_executor_inventory_id": authority["run_id"],
                    "physical_job_id": "-" if job is None else str(job["id"]), "physical_job_state": "-" if job is None else job["status"],
                    "physical_job_conclusion": "-" if job is None else job["conclusion"],
                    "physical_job_started_at": "-" if job is None or job.get("started_at") is None else job["started_at"],
                    "physical_job_completed_at": "-" if job is None or job.get("completed_at") is None else job["completed_at"],
                    "terminal_state": state, "terminal_api_sha256": hashlib.sha256(envelope).hexdigest(),
                    "terminal_api_bytes": str(len(envelope)), "context_revision": context,
                    "context_main_relation": "ahead" if no_executor else "-"}
        native.update({prefix + "_historical_request_run_id": authority["request_id"], prefix + "_historical_request_run_attempt": "1",
                       prefix + "_historical_request_head": authority["head"], prefix + "_historical_request_conclusion": "failure",
                       prefix + "_historical_executor_run_id": authority["run_id"], prefix + "_historical_executor_run_attempt": "-" if no_executor else "1",
                       prefix + "_historical_executor_conclusion": "-" if no_executor else "failure", prefix + "_historical_context_revision": context,
                       prefix + "_historical_terminal_state": state, prefix + "_historical_terminal_api_sha256": terminal["terminal_api_sha256"],
                       prefix + "_historical_terminal_api_bytes": str(len(envelope))})
        for field in ("id", "state", "conclusion", "started_at", "completed_at"):
            native[prefix + "_historical_physical_job_" + field] = terminal["physical_job_" + field]
        authority.update(historical_terminal_review=True, admitted=native, historical_context_revision=context,
            terminal_proof=terminal, terminal_api_envelope=envelope, terminal_api_sha256=terminal["terminal_api_sha256"],
            selected_physical_job=job, terminal_artifact_inventory=inventory, selected_artifact=artifact,
            expected_artifact_name=expected_artifact_name, artifact_inventory_complete=True,
            historical_records={"api": api_raw, "envelope": envelope,
                "terminal": b"".join((key + "\t" + value + "\n").encode("ascii") for key, value in terminal.items())})
        authority.pop("raw_original")
        authority.pop("historical_original_facts_binding")
        return authority

    def test_known_failed_and_true_hostless_are_chargeable_data_without_zip_or_measurement(self):
        with patch.object(publisher, "sampling_read_artifact", side_effect=AssertionError("ZIP fallback")), \
                patch.object(publisher, "utility_read_artifact", side_effect=AssertionError("ZIP fallback")), \
                patch.object(publisher, "sampling_validate", side_effect=AssertionError("measurement")):
            known = publisher.campaign_ingest_terminal("confirm", 0, self.authority())
            missing = publisher.campaign_ingest_terminal("utility", 0, self.authority("utility", 0, 45, no_executor=True))
        self.assertEqual(known["fact"]["state"], "failed")
        self.assertEqual(known["fact"]["artifact_id"], "-")
        self.assertEqual(known["fact"]["artifact_bytes"], "0")
        self.assertEqual(known["fact"]["native_wall_us"], "-")
        self.assertEqual(missing["fact"]["state"], "hostless")
        self.assertEqual(missing["fact"]["executor_run"], "-")
        self.assertEqual(missing["fact"]["executor_attempt"], "-")
        self.assertEqual(missing["fact"]["policy_revision"], "-")
        self.assertEqual(missing["fact"]["job_wall_us"], "-")
        self.assertEqual(missing["fact"]["corpus_cells"], "0")
        ingested = [None] * 46
        ingested[4], ingested[45] = known, missing
        result = publisher.campaign_assemble_facts(ingested)
        self.assertEqual(result["criteria"], b"")
        self.assertEqual(len(result["raw_replays"]), 2)
        self.assertTrue(result["native_inventory_review_required"])
        self.assertFalse(result["physical_qualification"])
        self.assertEqual(json.loads(result["raw_replays"]["utility-0.json"])["historical_context_revision"], "9" * 40)

    def test_terminal_known_cost_and_cancelled_null_timestamps_preserve_actual_job(self):
        base = self.authority()
        job = {"id": 600, "run_id": int(base["run_id"]), "run_attempt": 1, "head_sha": base["executor"]["head_sha"],
               "name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "failure",
               "created_at": "2026-10-01T00:00:00Z", "started_at": "2026-10-01T00:00:01Z", "completed_at": "2026-10-01T00:00:01Z"}
        item = publisher.campaign_ingest_terminal("confirm", 0, self.authority(job=job))
        self.assertEqual(item["fact"]["job_wall_us"], "2000000")
        cancelled = dict(job, conclusion="cancelled", started_at=None, completed_at=None)
        item = publisher.campaign_ingest_terminal("confirm", 0, self.authority(job=cancelled))
        self.assertEqual(item["fact"]["state"], "cancelled")
        self.assertEqual(item["fact"]["job_wall_us"], "-")
        self.assertEqual(json.loads(item["raw_replay"])["job"]["id"], 600)
        ingested = [None] * 46
        ingested[4] = item
        self.assertIn("confirm-0.json", publisher.campaign_assemble_facts(ingested)["raw_replays"])

    def test_terminal_guard_rejects_measurement_flags_slot_changes_and_fabricated_no_executor(self):
        authority = self.authority()
        for key, value in (("sampling_historical_terminal_valid", True), ("sampling_historical_valid", "true"),
                           ("sampling_historical_execution_authority", "true"), ("sampling_historical_measurement_valid", "true"),
                           ("sampling_family", "AA"), ("sampling_packet", "1"), ("sampling_policy_revision", "f" * 40),
                           ("sampling_historical_terminal_api_sha256", "f" * 64), ("sampling_admitted", "false")):
            changed = copy.deepcopy(authority)
            changed["admitted"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                publisher.campaign_ingest_terminal("confirm", 0, changed)
        missing = self.authority(no_executor=True)
        for mutate in ("policy", "attempt", "context", "inventory", "envelope"):
            changed = copy.deepcopy(missing)
            if mutate == "policy":
                changed["admitted"]["sampling_policy_revision"] = changed["historical_context_revision"]
            elif mutate == "attempt":
                changed["admitted"]["sampling_historical_executor_run_attempt"] = "1"
            elif mutate == "context":
                changed["historical_context_revision"] = "-"
            elif mutate == "inventory":
                changed["terminal_proof"]["executor_inventory_count"] = "1"
            else:
                changed["terminal_api_envelope"] += b" "
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                publisher.campaign_ingest_terminal("confirm", 0, changed)

    def test_terminal_partial_zip_retains_true_api_digest_and_manifest_without_measurement(self):
        import io
        import stat
        import zipfile
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            info = zipfile.ZipInfo("failed-native-proof.json")
            info.external_attr = (stat.S_IFREG | 0o600) << 16
            archive.writestr(info, b'{"state":"failed","exit":125}\n')
        raw_zip = packed.getvalue()
        base = self.authority()
        artifact = {"id": 777, "name": base["expected_artifact_name"], "expired": False, "size_in_bytes": len(raw_zip),
                    "digest": "sha256:" + hashlib.sha256(raw_zip).hexdigest(),
                    "workflow_run": {"id": int(base["run_id"]), "head_sha": base["executor"]["head_sha"]}}
        authority = self.authority(artifact=artifact)
        with patch.object(publisher, "sampling_validate", side_effect=AssertionError("terminal science")), \
                patch.object(publisher, "utility_validate", side_effect=AssertionError("terminal science")):
            item = publisher.campaign_ingest_terminal("confirm", 0, authority, raw_zip=raw_zip)
        self.assertEqual(item["fact"]["state"], "failed")
        self.assertEqual(item["fact"]["artifact_id"], "777")
        self.assertEqual(item["fact"]["artifact_sha256"], hashlib.sha256(raw_zip).hexdigest())
        self.assertEqual(item["fact"]["artifact_bytes"], str(len(raw_zip)))
        self.assertEqual(item["fact"]["native_wall_us"], "-")
        self.assertIn(b"failed-native-proof.json\t", item["raw_manifest"])
        ingested = [None] * 46
        ingested[4] = item
        self.assertEqual(publisher.campaign_assemble_facts(ingested)["criteria"], b"")
        for changed in (None, raw_zip + b"changed"):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                publisher.campaign_ingest_terminal("confirm", 0, authority, raw_zip=changed)
        with self.assertRaises(ValueError):
            publisher.campaign_ingest_terminal("confirm", 0, self.authority(), raw_zip=raw_zip)
        with self.assertRaises(ValueError):
            publisher.campaign_ingest_terminal("confirm", 0, self.authority(artifact=dict(artifact, expired=True)), raw_zip=raw_zip)
        altered = copy.deepcopy(item)
        record = json.loads(item["raw_replay"])
        record["verified_artifact"]["verified_zip_sha256"] = "f" * 64
        changed = publisher.campaign_json(record, max_bytes=publisher.CAMPAIGN_REPLAY_LIMIT)
        altered["raw_replay"] = changed
        altered["fact"]["raw_replay_sha256"] = hashlib.sha256(changed).hexdigest()
        ingested[4] = altered
        with self.assertRaises(ValueError):
            publisher.campaign_assemble_facts(ingested)

    def test_terminal_zero_artifact_requires_complete_original_api_inventory(self):
        authority = self.authority()
        for mutate in ("complete", "inventory", "selected", "expected_name"):
            changed = copy.deepcopy(authority)
            if mutate == "complete":
                changed["artifact_inventory_complete"] = False
            elif mutate == "inventory":
                changed["terminal_artifact_inventory"] = [{"id": 777, "name": changed["expected_artifact_name"]}]
            elif mutate == "selected":
                changed["selected_artifact"] = {"id": 777}
            else:
                changed["expected_artifact_name"] = "another attempt"
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                publisher.campaign_ingest_terminal("confirm", 0, changed)

    def test_terminal_native_envelope_cap_survives_base64_replay(self):
        authority = self.authority(padding=6 * 1024 * 1024 + 4096)
        self.assertLessEqual(len(authority["terminal_api_envelope"]), 8 * 1024 * 1024)
        item = publisher.campaign_ingest_terminal("confirm", 0, authority)
        self.assertGreater(len(item["raw_replay"]), publisher.MEMBER_LIMIT)
        self.assertLessEqual(len(item["raw_replay"]), publisher.CAMPAIGN_REPLAY_LIMIT)
        ingested = [None] * 46
        ingested[4] = item
        self.assertIn("confirm-0.json", publisher.campaign_assemble_facts(ingested)["raw_replays"])
        with self.assertRaises(ValueError):
            publisher.campaign_json({"padding": "x" * (publisher.MEMBER_LIMIT + 1)})

    def test_terminal_final_retained_inputs_and_archive_bind_actual_envelope(self):
        item = publisher.campaign_ingest_terminal("confirm", 0, self.authority())
        fact = item["fact"]
        archived = dict(item["archive"], archive_kind="library", archive_reference="libfile_" + "3" * 32, archive_version="0",
                        archive_sha256=fact["terminal_api_sha256"], archive_bytes=fact["terminal_api_bytes"], archive_receipt_sha256="4" * 64)
        self.assertEqual(publisher.campaign_archive_row(fact, archived), archived)
        for target in ("api_envelope", "terminal_proof", "current_transport", "job", "state"):
            changed = copy.deepcopy(item)
            record = json.loads(changed["raw_replay"])
            if target == "current_transport":
                record[target]["current"]["history.tsv"] = base64.b64encode(b"changed-prefix").decode()
            elif target == "job":
                record["job"] = {"id": 999}
            elif target == "state":
                changed["fact"]["state"] = "complete"
            else:
                record[target] = base64.b64encode(b"changed").decode()
            replay = publisher.campaign_json(record)
            changed["raw_replay"] = replay
            changed["fact"]["raw_replay_sha256"] = hashlib.sha256(replay).hexdigest()
            ingested = [None] * 46
            ingested[4] = changed
            with self.subTest(target=target), self.assertRaises(ValueError):
                publisher.campaign_assemble_facts(ingested)


class CampaignFactsDataTests(unittest.TestCase):
    def authority_and_result(self, phase, packet, index):
        from sampling_qualification_receipt import schedule
        prefix = "sampling" if phase in ("acquire", "pilot", "confirm") else phase
        admitted = {prefix + "_trusted_revision": REVISION, prefix + "_historical_valid": "true",
                    ("sampling_freeze_revision" if prefix == "sampling" else prefix + "_plan_revision"): "c" * 40,
                    ("sampling_freeze_sha256" if prefix == "sampling" else prefix + "_plan_sha256"): "d" * 64}
        admitted.update({prefix + "_historical_execution_authority": "false", prefix + "_historical_qualification": "unqualified",
                         prefix + "_phase": "qualify" if phase == "preparation" else phase,
                         prefix + "_packet": str(packet), prefix + "_family": schedule(phase, packet)["family"] if prefix == "sampling" else phase,
                         prefix + "_policy_revision": "e" * 40})
        if prefix == "sampling":
            plan = schedule(phase, packet)
            admitted.update(sampling_policy_revision="e" * 40, sampling_campaign_parent="f" * 64,
                            sampling_phase=phase, sampling_packet=str(packet), sampling_family=plan["family"])
        request, executor = {"id": index + 100, "run_attempt": 1, "head_sha": HEAD}, {"id": index + 200, "run_attempt": 1, "head_sha": "e" * 40}
        authority = {"historical_review": True, "repository": REPOSITORY, "admitted": admitted,
                     "request_id": str(request["id"]), "run_id": str(executor["id"]), "request": request,
                     "executor": executor, "freeze": {"campaign_parent": "f" * 64},
                     "facts": {"pull_state": "closed"}}
        result = {"qualification_state": "unqualified", "packet_state": "complete-valid-research", "problems": [],
                  "phase": "qualify" if phase == "preparation" else phase, "packet": packet,
                  "accounting": {"physical_job_wall_upper_us": 100000000, "native_owner_wall_us": 90000000}}
        if prefix == "sampling":
            plan = schedule(phase, packet)
            result["series"] = [{"profile": profile, "ordinal": ordinal, "outcome": "no detectable difference",
                                 "uncertainty": {"ci_low": 1.021, "ci_high": 1.024}}
                                for profile, ordinal, pairs in plan["slots"]]
        elif phase == "preparation":
            result.update(packet_state="complete-negative-research",
                series={label: {"ci_low": 0.994, "ci_high": 1.006}
                        for label in ("legacy/immutable-aa", "snapshot/immutable-aa", "snapshot/cross-build-aa")},
                control_failures=["complete scientific negative"])
        else:
            result.update(packet_state="complete-negative-research",
                          utility_observation=publisher.utility_net_observation(10000000, 8000000, 30000000))
        authority = retained_transport(authority, prefix)
        manifest = b"".join((name + "\t" + hashlib.sha256(raw).hexdigest() + "\t" + str(len(raw)) + "\n").encode("ascii")
                            for name, raw in sorted(authority["raw_original"].items()))
        artifact = {"id": index + 300, "verified_zip_sha256": "1" * 64, "verified_zip_bytes": index + 1000,
                    "verified_member_manifest_sha256": hashlib.sha256(manifest).hexdigest(),
                    "verified_member_manifest": manifest}
        if phase == "preparation":
            result["campaign_aa_corpus_regressions"] = {label: 0 for label in result["series"]}
        if phase in ("preparation", "utility"):
            result["campaign_criteria"] = publisher.campaign_criteria_fragment(phase, result)
        return authority, result, artifact

    def test_ppm_never_rounds_a_borderline_interval_into_acceptance(self):
        import math
        self.assertEqual(publisher.campaign_ppm(1.02, 1.025)[:2], (20000, 25000))
        self.assertEqual(publisher.campaign_ppm(math.nextafter(1.02, 0), 1.025)[0], 19999)
        self.assertEqual(publisher.campaign_ppm(1.02, math.nextafter(1.025, math.inf))[1], 25001)
        self.assertEqual(publisher.campaign_ppm(1.0, math.nextafter(1.02, math.inf))[2], 10001)
        self.assertLess(publisher.campaign_ppm(0.99, 1.01)[0], 0)
        for first, last in ((True, 1.0), (1.0, False), (float("nan"), 1), (1, float("inf")), (2, 1), (0, 1)):
            with self.subTest(first=first, last=last), self.assertRaises(ValueError):
                publisher.campaign_ppm(first, last)

    def test_complete_negative_utility_and_aa_are_data_not_qualification(self):
        authority, result, artifact = self.authority_and_result("utility", 0, 45)
        fact, replay = publisher.campaign_complete_fact("utility", 0, authority, artifact, result)
        self.assertEqual(fact["state"], "complete")
        self.assertEqual(fact["corpus_cells"], "24")
        self.assertFalse(json.loads(replay)["validation"]["utility_observation"]["criterion_met"])
        self.assertEqual(fact["raw_replay_sha256"], hashlib.sha256(replay).hexdigest())
        with self.assertRaises(ValueError):
            publisher.campaign_complete_fact("utility", 0, authority, artifact, dict(result, packet_state="incomplete"))

    def test_raw_slot_order_cannot_be_relabelled(self):
        authority, result, artifact = self.authority_and_result("pilot", 1, 2)
        fact, unused = publisher.campaign_complete_fact("pilot", 1, authority, artifact, result)
        self.assertEqual(fact["slots"], "unchanged,unchanged,unchanged")
        self.assertEqual(fact["slot_validations"], "valid,valid,valid")
        changed = copy.deepcopy(result)
        changed["series"].reverse()
        with self.assertRaises(ValueError):
            publisher.campaign_complete_fact("pilot", 1, authority, artifact, changed)
        changed = copy.deepcopy(result)
        changed["problems"] = ["raw output changed"]
        with self.assertRaises(ValueError):
            publisher.campaign_complete_fact("pilot", 1, authority, artifact, changed)

    def test_archive_is_joined_to_exact_zip_and_not_opaque_uri(self):
        authority, result, artifact = self.authority_and_result("utility", 0, 45)
        fact, unused = publisher.campaign_complete_fact("utility", 0, authority, artifact, result)
        missing = publisher.campaign_archive_row(fact, None)
        self.assertEqual(missing["archive_reference"], "-")
        saved = dict(missing, archive_kind="library", archive_reference="libfile_" + "3" * 32, archive_version="0",
                     archive_sha256=fact["artifact_sha256"], archive_bytes=fact["artifact_bytes"],
                     archive_receipt_sha256="4" * 64)
        self.assertEqual(publisher.campaign_archive_row(fact, saved), saved)
        for changed in (dict(saved, archive_reference="https://signed.example/zip"),
                        dict(saved, artifact_id="999"), dict(saved, archive_version=0),
                        dict(saved, archive_sha256="5" * 64), dict(saved, archive_bytes="1"),
                        dict(saved, archive_receipt_sha256="-")):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                publisher.campaign_archive_row(fact, changed)

    def test_original_ingestions_retain_46_facts_and_final_assembly_reads_no_zip_or_api(self):
        plan = [("acquire", 0), *(("pilot", i) for i in range(3)), *(("confirm", i) for i in range(40)),
                ("preparation", 0), ("utility", 0)]
        reviewed, results, artifacts = [], {}, {}
        prep_files = {"qualification/" + label + "-throughput/summary.json": b'{"confirmed_regressions":0}'
                      for label in ("legacy/immutable-aa", "snapshot/immutable-aa", "snapshot/cross-build-aa")}
        for index, (phase, packet) in enumerate(plan):
            authority, result, artifact = self.authority_and_result(phase, packet, index)
            reviewed.append(authority)
            results[authority["run_id"]] = result
            artifacts[authority["run_id"]] = artifact
        def read(unused_api, authority, *, retain_archive_identity=False):
            self.assertIs(retain_archive_identity, True)
            return dict(prep_files, **authority["raw_original"]), artifacts[authority["run_id"]]
        def validate(unused_api, authority, unused_files):
            return results[authority["run_id"]]
        with patch.object(publisher, "sampling_read_artifact", side_effect=read) as sr, \
                patch.object(publisher, "preparation_read_artifact", side_effect=read) as pr, \
                patch.object(publisher, "utility_read_artifact", side_effect=read) as ur, \
                patch.object(publisher, "sampling_validate", side_effect=validate) as sv, \
                patch.object(publisher, "preparation_validate", side_effect=validate) as pv, \
                patch.object(publisher, "utility_validate", side_effect=validate) as uv, \
                patch.object(publisher, "bind_historical_transport", side_effect=lambda authority, files, kind: authority):
            # Each call represents an independently authenticated original-attempt
            # ingestion while its ZIP is retained, never final-aggregate refetch.
            ingested = [publisher.campaign_ingest_packet(object(), phase, packet, authority)
                        for (phase, packet), authority in zip(plan, reviewed)]
            self.assertEqual((sr.call_count, pr.call_count, ur.call_count, sv.call_count, pv.call_count, uv.call_count),
                             (44, 1, 1, 44, 1, 1))
            results[reviewed[20]["run_id"]]["packet_state"] = "incomplete"
            with patch.object(publisher, "campaign_audit_job", side_effect=ValueError("unavailable")):
                invalid = publisher.campaign_ingest_packet(object(), "confirm", 16, reviewed[20])
        with patch.object(publisher, "sampling_read_artifact", side_effect=AssertionError("final ZIP refetch")), \
                patch.object(publisher, "preparation_read_artifact", side_effect=AssertionError("final ZIP refetch")), \
                patch.object(publisher, "utility_read_artifact", side_effect=AssertionError("final ZIP refetch")), \
                patch.object(publisher, "sampling_host_job", side_effect=AssertionError("final API lookup")):
            result = publisher.campaign_assemble_facts(ingested)
            self.assertEqual(len(result["raw_replays"]), 46)
            self.assertEqual(len(result["raw_manifests"]), 46)
            rows = result["facts"].decode().splitlines()
            self.assertEqual(len(rows), 47)
            self.assertEqual(rows[0].split("\t"), list(publisher.CAMPAIGN_FACT_FIELDS))
            self.assertEqual(rows[-1].split("\t")[17], "complete")
            self.assertFalse(result["physical_qualification"])
            self.assertFalse(result["archive_storage_assessed"])
            criterion = dict(line.split("\t") for line in result["criteria"].decode().splitlines())
            self.assertEqual(criterion["utility_job_wall_us"], "30000000")
            self.assertEqual(criterion["utility_legacy_wall_us"], "10000000")
            self.assertLess(int(criterion["legacy_immutable_aa_low_ppm"]), -5000)
            self.assertEqual(criterion["aa_corpus_regressions"], "0")
            altered = copy.deepcopy(ingested)
            altered[10]["fact"]["executor_run"] = ingested[9]["fact"]["executor_run"]
            with self.assertRaises(ValueError):
                publisher.campaign_assemble_facts(altered)
            altered = copy.deepcopy(ingested)
            altered[10]["raw_manifest"] += b"changed"
            with self.assertRaises(ValueError):
                publisher.campaign_assemble_facts(altered)
            altered = copy.deepcopy(ingested)
            altered[10]["raw_replay"] += b" "
            with self.assertRaises(ValueError):
                publisher.campaign_assemble_facts(altered)
            for index, key in ((44, "legacy_immutable_aa_low_ppm"), (45, "utility_job_wall_us")):
                altered = copy.deepcopy(ingested)
                record = json.loads(altered[index]["raw_replay"])
                record["validation"]["campaign_criteria"][key] = "0"
                changed_replay = publisher.campaign_json(record)
                altered[index]["raw_replay"] = changed_replay
                altered[index]["fact"]["raw_replay_sha256"] = hashlib.sha256(changed_replay).hexdigest()
                with self.subTest(index=index, key=key), self.assertRaises(ValueError):
                    publisher.campaign_assemble_facts(altered)
            retained = list(ingested)
            retained[20] = invalid
            assembled = publisher.campaign_assemble_facts(retained)
            invalid_row = assembled["facts"].decode().splitlines()[21].split("\t")
            self.assertEqual(invalid_row[17], "invalid")
            self.assertEqual(invalid_row[15:17], ["-", "-"])
            self.assertEqual(invalid_row[18:20], ["-", "-"])
            self.assertEqual(len(assembled["raw_replays"]), 46)
            self.assertIn("incomplete", json.loads(assembled["raw_replays"]["confirm-16.json"])["validation_error"])

    def test_accounting_requires_positive_u64_int_without_coercion(self):
        authority, result, artifact = self.authority_and_result("utility", 0, 45)
        for key in ("physical_job_wall_upper_us", "native_owner_wall_us"):
            for value in (True, False, "90000000", 90000000.0, 0, -1, 1 << 64):
                changed = copy.deepcopy(result)
                changed["accounting"][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    publisher.campaign_complete_fact("utility", 0, authority, artifact, changed)

    def test_invalid_raw_keeps_verified_zip_and_unknown_native_wall(self):
        authority, result, artifact = self.authority_and_result("confirm", 0, 4)
        actual_job = {"id": 600, "run_id": int(authority["run_id"]), "run_attempt": 1, "head_sha": authority["executor"]["head_sha"],
                      "name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "failure",
                      "created_at": "2026-10-01T00:00:00Z", "started_at": "2026-10-01T00:00:01Z",
                      "completed_at": "2026-10-01T00:02:00Z"}
        with patch.object(publisher, "campaign_audit_job", return_value=actual_job):
            row, replay = publisher.campaign_invalid_fact(object(), "confirm", 0, authority, artifact, ValueError("bad raw pair"))
        self.assertEqual(row["state"], "invalid")
        self.assertEqual(row["job_wall_us"], "121000000")
        self.assertEqual(row["native_wall_us"], "-")
        self.assertEqual(row["artifact_id"], str(artifact["id"]))
        self.assertEqual(row["slots"], "-")
        self.assertFalse(json.loads(replay)["execution_authority"])
        with self.assertRaises(ValueError):
            publisher.campaign_invalid_fact(object(), "confirm", 0, dict(authority, historical_review=False),
                                            artifact, ValueError("bad raw pair"))

    def test_retained_invalid_job_cannot_change_source_with_recomputed_replay_digest(self):
        authority, unused_result, artifact = self.authority_and_result("confirm", 0, 4)
        job = {"id": 600, "run_id": int(authority["run_id"]), "run_attempt": 1, "head_sha": authority["executor"]["head_sha"],
               "name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "failure",
               "created_at": "2026-10-01T00:00:00Z", "started_at": "2026-10-01T00:00:01Z", "completed_at": "2026-10-01T00:01:01Z"}
        with patch.object(publisher, "campaign_audit_job", return_value=job):
            row, replay = publisher.campaign_invalid_fact(object(), "confirm", 0, authority, artifact, ValueError("raw invalid"))
        item = {"schema": "buster-compiler-campaign-ingestion-v1", "phase": "confirm", "packet": 0,
                "fact": row, "archive": publisher.campaign_archive_row(row, None), "raw_replay": replay,
                "raw_manifest": artifact["verified_member_manifest"], "qualification_state": "unqualified",
                "physical_qualification": False, "archive_storage_assessed": False}
        ingested = [None] * 46
        ingested[4] = item
        self.assertIn("confirm-0.json", publisher.campaign_assemble_facts(ingested)["raw_replays"])
        for key, value in (("head_sha", "f" * 40), ("run_id", 999), ("run_attempt", 2), ("id", True)):
            altered = copy.deepcopy(item)
            record = json.loads(replay)
            record["job"][key] = value
            changed = publisher.campaign_json(record)
            altered["raw_replay"] = changed
            altered["fact"]["raw_replay_sha256"] = hashlib.sha256(changed).hexdigest()
            ingested[4] = altered
            with self.subTest(key=key), self.assertRaises(ValueError):
                publisher.campaign_assemble_facts(ingested)

    def test_invalid_audit_retains_known_job_cost_without_runner_eligibility(self):
        authority, unused_result, artifact = self.authority_and_result("confirm", 0, 4)
        observed = {"id": 600, "run_id": int(authority["run_id"]), "run_attempt": 1, "head_sha": authority["executor"]["head_sha"],
                    "name": publisher.SAMPLING_HOST_JOB,
                    "status": "completed", "conclusion": "failure",
                    "created_at": "2026-10-01T00:00:00Z", "started_at": "2026-10-01T00:00:01Z",
                    "completed_at": "2026-10-01T00:01:01Z"}
        class Api:
            def __init__(self, row):
                self.row = row
            def pages(self, path, field):
                self_path = "/actions/runs/" + authority["run_id"] + "/attempts/1/jobs"
                if path != self_path or field != "jobs":
                    raise AssertionError((path, field))
                return [self.row]
        for runner in ({}, {"runner_id": None, "runner_name": "", "labels": ["wrong-label"]}):
            job = dict(observed, **runner)
            self.assertEqual(publisher.campaign_audit_job(Api(job), "confirm", authority), job)
            row, replay = publisher.campaign_invalid_fact(Api(job), "confirm", 0, authority, artifact, ValueError("invalid raw"))
            self.assertEqual(row["job_wall_us"], "62000000")
            self.assertEqual(json.loads(replay)["job"], job)
            self.assertEqual(row["native_wall_us"], "-")
        cancelled = dict(observed, conclusion="cancelled", started_at=None, completed_at=None)
        row, replay = publisher.campaign_invalid_fact(Api(cancelled), "confirm", 0, authority, artifact, ValueError("cancelled raw"))
        self.assertEqual(row["job_wall_us"], "-")
        self.assertEqual(json.loads(replay)["job"]["id"], 600)
        self.assertEqual(json.loads(replay)["job"]["conclusion"], "cancelled")
        for changed in (dict(observed, id=True), dict(observed, run_id=True), dict(observed, run_attempt=True),
                        dict(observed, run_attempt=2), dict(observed, head_sha="f" * 40), dict(observed, name="other job")):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                publisher.campaign_audit_job(Api(changed), "confirm", authority)

    def test_terminal_api_same_second_is_conservative_and_null_stamps_remain_unknown(self):
        authority, unused_result, artifact = self.authority_and_result("confirm", 0, 4)
        observed = {"id": 600, "run_id": int(authority["run_id"]), "run_attempt": 1, "head_sha": authority["executor"]["head_sha"],
                    "name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "cancelled",
                    "created_at": "2026-10-01T00:00:00Z", "started_at": "2026-10-01T00:00:01Z",
                    "completed_at": "2026-10-01T00:00:01Z"}
        self.assertEqual(publisher.campaign_api_wall(observed), 2000000)
        self.assertEqual(publisher.campaign_invalid_row("confirm", 0, authority, artifact, observed)["job_wall_us"], "2000000")
        for key in ("started_at", "completed_at"):
            changed = dict(observed, **{key: None})
            row = publisher.campaign_invalid_row("confirm", 0, authority, artifact, changed)
            self.assertEqual(row["job_wall_us"], "-")
            self.assertEqual(row["executor_run"], authority["run_id"])
            self.assertEqual(row["state"], "invalid")
            with self.assertRaises(ValueError):
                publisher.campaign_api_wall(changed)

    def test_invalid_cost_retains_failed_and_over_budget_api_wall(self):
        authority, unused_result, artifact = self.authority_and_result("confirm", 0, 4)
        observed = {"id": 600, "run_id": int(authority["run_id"]), "run_attempt": 1, "head_sha": authority["executor"]["head_sha"],
                    "name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "failure",
                    "created_at": "2026-10-01T00:00:00Z", "started_at": "2026-10-01T00:00:01Z",
                    "completed_at": "2026-10-01T01:00:01Z"}
        row = publisher.campaign_invalid_row("confirm", 0, authority, artifact, observed)
        self.assertEqual(row["job_wall_us"], "3602000000")
        self.assertEqual(row["native_wall_us"], "-")
        self.assertEqual(row["state"], "invalid")
        with self.assertRaises(ValueError):
            publisher.campaign_api_wall(dict(observed, run_attempt=True))
        with self.assertRaises(ValueError):
            publisher.campaign_api_wall(dict(observed, completed_at="2026-09-30T00:00:01Z"))

    def test_original_scope_mismatch_refuses_before_zip_or_invalid_retention(self):
        authority, unused_result, unused_artifact = self.authority_and_result("pilot", 1, 2)
        with patch.object(publisher, "sampling_read_artifact", side_effect=AssertionError("scope rebound to ZIP")):
            with self.assertRaises(ValueError):
                publisher.campaign_ingest_packet(object(), "pilot", 2, authority)
        missing = copy.deepcopy(authority)
        missing["executor"].pop("head_sha")
        missing["admitted"].pop("sampling_policy_revision")
        with patch.object(publisher, "sampling_read_artifact", side_effect=AssertionError("missing P before ZIP")), self.assertRaises(ValueError):
            publisher.campaign_ingest_packet(object(), "pilot", 1, missing)
        for phase in ("preparation", "utility"):
            original, unused_result, unused_artifact = self.authority_and_result(phase, 0, 44)
            prefix = phase
            for key, value in ((prefix + "_phase", "pilot"), (prefix + "_packet", "1"),
                               (prefix + "_family", "aa"), (prefix + "_policy_revision", "0" * 40)):
                changed = copy.deepcopy(original)
                changed["admitted"][key] = value
                with self.subTest(phase=phase, key=key), \
                        patch.object(publisher, phase + "_read_artifact", side_effect=AssertionError("scope rebound to ZIP")), \
                        self.assertRaises(ValueError):
                    publisher.campaign_ingest_packet(object(), phase, 0, changed)

    def test_prerequisite_review_flags_are_distinct_and_cannot_be_live_admission(self):
        for phase in ("preparation", "utility"):
            authority, unused_result, unused_artifact = self.authority_and_result(phase, 0, 44)
            self.assertTrue(publisher.campaign_reviewed(authority, phase))
            for key, value in ((phase + "_historical_valid", "false"),
                               (phase + "_historical_execution_authority", "true"),
                               (phase + "_historical_qualification", "qualified"),
                               (phase + "_admitted", "false")):
                changed = copy.deepcopy(authority)
                changed["admitted"][key] = value
                with self.subTest(phase=phase, key=key), self.assertRaises(ValueError):
                    publisher.campaign_ingest_packet(object(), phase, 0, changed)

    def test_not_run_is_explicit_and_does_not_invent_an_attempt(self):
        result = publisher.campaign_assemble_facts([None] * 46)
        self.assertEqual(result["criteria"], b"")
        self.assertEqual(result["raw_replays"], {})
        rows = result["facts"].decode().splitlines()[1:]
        self.assertEqual(len(rows), 46)
        self.assertTrue(all(row.split("\t")[17] == "not_run" for row in rows))
        self.assertTrue(all(row.split("\t")[3:7] == ["-"] * 4 for row in rows))
        with self.assertRaises(ValueError):
            publisher.campaign_assemble_facts([None] * 45)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--utility-native-export":
        os.environ["BUSTER_UTILITY_NATIVE_EXPORT"] = sys.argv[2]
        unittest.main(argv=[sys.argv[0]], defaultTest="UtilityNativeExportReplay")
    elif len(sys.argv) == 3 and sys.argv[1] == "--utility-native-negative-export":
        os.environ["BUSTER_UTILITY_NATIVE_NEGATIVE_EXPORT"] = sys.argv[2]
        unittest.main(argv=[sys.argv[0]], defaultTest="UtilityNativeFailureReplay")
    elif len(sys.argv) == 3 and sys.argv[1] == "--main-owned-native-export":
        os.environ["BUSTER_MAIN_OWNED_NATIVE_EXPORT"] = sys.argv[2]
        unittest.main(argv=[sys.argv[0]], defaultTest="MainOwnedNativeFortyReplay")
    else:
        unittest.main()
