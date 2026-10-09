#!/usr/bin/env python3
"""Hosted functional fixtures for #3212; no benchmark process or physical host.

Run from the repository root:
    python3 -B tools/bench_direct/sampling_qualification_test.py
Synthetic raw pairs exercise the actual trusted statistical reconstruction.
These fixtures diagnose the consumer; they are never performance evidence.
"""
from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path

import compiler_receipt
import sampling_qualification_receipt as receipt

SHA = "a" * 40
TREE = "b" * 40
HEAD = "c" * 40
FREEZE = "d" * 40
DIGEST = "e" * 64
CLOSURE = "f" * 64


def bundle(profile: str, pairs: int, binaries: dict, workload: dict, ratio: float) -> dict:
    count = pairs or 16
    records = []
    groups = []
    for index in range(1, count + 1):
        order = "AB" if index % 2 else "BA"
        spans = {"a": 2.0, "b": 2.0 * ratio}
        for variant in order.lower():
            records.append({"pair": index, "order": order, "variant": variant, "exit": 0,
                            "identical": True, "span_s": spans[variant], "counters": False,
                            "maxrss_bytes": None, "cpu_s": None})
        groups.append({"pair": index, "order": order, "metrics_a": {"wall": spans["a"]},
                       "metrics_b": {"wall": spans["b"]}})
    wall = receipt._lab.compare_series([(2.0, 2.0 * ratio)] * count, "s", "lower", 20261003,
                                       time_metric=True, floor=0.005)
    plan = {"pairs": count, "reason": f"--pairs {count}" if pairs else "frozen 10-minute adaptive comparator",
            "order": "ABBA", "fresh_copy": True}
    raw = {"version": 1, "mode": "compare", "config": dict(workload, cpu=2, pairs=pairs or None,
           target_minutes=10, warmups=1, seed=20261003, profile_steps=[], sudo=False,
           require_identical_output=False, canonical_inline_pair=False, fresh_copy=True, min_effect_percent=0.5),
           "plan": plan, "steps": {key: {"status": "ok", "elapsed_s": 1.0} for key in ("env", "prepare", "timed")},
           "variants": {key: dict(binaries[role], role=role) for key, role in (("a", "baseline"), ("b", "candidate"))}}
    summary = {"schema": receipt.LAB_SCHEMA, "cpu": 2, "command": workload["command"],
               "repo_root": workload["repo_root"], "host": {"cpu_model": "AMD Ryzen 7 9700X 8-Core Processor", "git_revision": SHA},
               "plan": dict(plan, seed=20261003, confidence=0.95, bootstrap_resamples=2000,
                            complete_pairs=count), "steps": {"env": "ok", "prepare": "ok", "timed": "ok"},
               "checks": receipt._lab.compare_checks(groups), "metrics": {"wall": wall},
               "verdict": dict(wall, metric="wall", min_effect_percent=0.5),
               "phase_metrics": {"enabled": False, "reason": "neither frozen compiler exposes phase metrics"},
               "phases": None, "counters": {"perf_stat": False, "reason": "counter unavailable"},
               "outputs_identical": ratio == 1.0}
    for role in ("baseline", "candidate"):
        summary[role] = dict(binaries[role], runs=count, failed=0, identical_runs=count, deterministic=True,
                             metrics_out_supported=False, metrics_out_enabled=False, metrics_out=False)
    return {"compare": raw, "pairs": records, "summary": summary}


def fixture(phase: str = "confirm", packet: int = 0) -> tuple:
    plan = receipt.schedule(phase, packet)
    family = plan["family"]
    identity = {"schema": receipt.SCHEMA, "phase": phase, "packet": str(packet), "campaign": DIGEST,
                "reservation_seconds": str(plan["reservation_seconds"]), "family": family,
                "trials": str(len(plan["slots"])), "base": SHA, "base_tree": TREE, "request_head": HEAD,
                "trusted_revision": FREEZE,
                "baseline_revision": SHA, "candidate_revision": SHA if family == "aa" else HEAD,
                "baseline_sha256": DIGEST, "candidate_sha256": DIGEST if family == "aa" else "a" * 64,
                "lab_sha256": DIGEST, "protocol_sha256": DIGEST, "python_sha256": DIGEST,
                "driver_sha256": DIGEST, "closure_sha256": CLOSURE, "freeze_sha256": DIGEST,
                "cpu": "2", "warmups": "1", "seed": "20261003", "floor_percent": "0.5",
                "fresh_copy": "true", "routine_enabled": "false", "evidence_class": receipt.EVIDENCE_CLASS}
    binaries = {role: {"sha256": identity[role + "_sha256"], "revision": identity[role + "_revision"],
                        "size_bytes": 10000} for role in ("baseline", "candidate")}
    workload = {"command": "IDE cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT",
                "repo_root": "/trusted/frozen-source", "perf": "perf", "extra": [], "extra_by_variant": {"a": [], "b": []}}
    rows, series = [], {}
    for index, slot in enumerate(plan["slots"]):
        rows.append({"trial": str(index), "profile": slot[0], "family": family, "ordinal": str(slot[1]),
                     "pairs": str(slot[2]), "wall_us": str(round((slot[2] or 16) * (4.0 if family == "aa" else 4.042) * 1000000) + 1000000), "user_cpu_us": "0", "system_cpu_us": "0",
                     "peak_rss_bytes": "0", "cpu_status": "3", "memory_status": "3",
                     "exit_status": "0", "timed_out": "0", "cleanup_failed": "0", "capture_failed": "0",
                     "closure_before": CLOSURE, "closure_after": CLOSURE, "state": "process-complete-unvalidated"})
        series[index] = bundle(slot[0], slot[2], binaries, workload, 1.0 if family == "aa" else 1.021)
    occupancy = sum(int(row["wall_us"]) for row in rows) + 20000
    terminal = {"physical_packet_wall_us": str(occupancy), "prep_us": "10000",
                "captured_input_files_unchanged": "true", "within_reservation": "true", "process_state": "complete",
                "qualification_state": "unvalidated", "queue_delay": "unavailable"}
    history = []
    for history_phase, total in (("pilot", packet + 1 if phase == "pilot" else 3),
                                 ("confirm", packet + 1 if phase == "confirm" else 0)):
        for index in range(total):
            history.append({"phase": history_phase, "packet": index, "run_id": str(1000 + len(history)),
                            "run_attempt": "1", "state": "complete",
                            "reservation_seconds": receipt.schedule(history_phase, index)["reservation_seconds"],
                            "physical_packet_wall_us": 1000000})
    history[-1]["physical_packet_wall_us"] = occupancy
    trusted = {"authenticated": True,
               "request": {"repository": "buster14a/buster", "actor": "davidgmbb", "owner": "davidgmbb",
                           "selector": receipt.REQUEST_SELECTORS[phase], "request_run_id": "998",
                           "request_head": HEAD, "freeze_revision": FREEZE, "phase": phase, "packet": packet,
                           "campaign": DIGEST},
               "executor": {"repository": "buster14a/buster", "request_run_id": "998", "run_id": history[-1]["run_id"],
                            "run_attempt": "1", "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor",
                            "physical_packet_wall_us": occupancy, "queue_delay_seconds": None},
               "identity": copy.deepcopy(identity), "binaries": binaries, "workload_config": workload,
               "attempts": history}
    return identity, rows, terminal, series, trusted


class SamplingQualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.confirm = fixture()
        cls.pilot = fixture("pilot", 1)
        cls.ab = fixture("confirm", 1)

    def test_schedule_is_independent_and_complete(self):
        counts = {"aa": 0, "ab1": 0, "ab2": 0}
        comparator_packets, first = [], 0
        reserved = 0
        ordinals = {"aa": [], "ab1": [], "ab2": []}
        for packet in range(40):
            plan = receipt.schedule("confirm", packet)
            reserved += plan["reservation_seconds"]
            for index, (profile, ordinal, pairs) in enumerate(plan["slots"]):
                if profile == receipt.SHORT:
                    self.assertEqual(pairs, 40)
                    counts[plan["family"]] += 1
                    ordinals[plan["family"]].append(ordinal)
                else:
                    self.assertEqual(profile, receipt.LONG)
                    comparator_packets.append(packet)
                    first += index == 0
        self.assertEqual(counts, {"aa": 80, "ab1": 39, "ab2": 39})
        self.assertEqual(comparator_packets, [0, 1, 3, 4, 5, 7, 8, 9, 11])
        self.assertEqual(first, 5)
        self.assertEqual(reserved, 42720)
        for family in counts:
            self.assertEqual(ordinals[family], list(range(counts[family])))
        self.assertEqual(receipt.schedule("confirm", 40), {})
        self.assertEqual(receipt.schedule("pilot", 3), {})
        self.assertEqual(receipt.schedule("confirm", True), {})
        self.assertEqual(sum(receipt.schedule("pilot", packet)["reservation_seconds"] for packet in range(3)), 9000)

    def test_complete_raw_packets_remain_unqualified(self):
        for original in (self.confirm, self.pilot, self.ab):
            shown = receipt.validate_packet(*copy.deepcopy(original))
            self.assertEqual(shown["packet_state"], "complete-valid-research", shown["problems"])
            self.assertEqual(shown["qualification_state"], "unqualified")
            self.assertIs(shown["routine_profile_enabled"], False)
            self.assertIsNone(shown["queue_delay_seconds"])
            self.assertTrue(shown["outstanding_qualification"])

    def test_real_unavailability_is_not_zero_or_failure(self):
        shown = receipt.validate_packet(*copy.deepcopy(self.confirm))
        for stream in shown["series"]:
            self.assertIsNone(stream["phases"])
            self.assertIsNone(stream["user_cpu_us"])
            self.assertIsNone(stream["system_cpu_us"])
            self.assertIsNone(stream["peak_rss_bytes"])
            self.assertIs(stream["phase_metrics"]["enabled"], False)
            self.assertIs(stream["counters"]["perf_stat"], False)
        self.assertIsNone(shown["queue_delay_seconds"])

    def _reject(self, change, original=None):
        data = list(copy.deepcopy(original or self.confirm))
        change(data)
        shown = receipt.validate_packet(*data)
        self.assertEqual(shown["packet_state"], "incomplete")
        self.assertEqual(shown["qualification_state"], "unqualified")
        self.assertIs(shown["routine_profile_enabled"], False)
        self.assertTrue(shown["problems"])
        return shown

    def test_each_raw_configuration_tampering_is_rejected(self):
        for field, value in (("pairs", 39), ("target_minutes", 1), ("warmups", 0), ("cpu", 0),
                             ("seed", 123), ("profile_steps", ["topdown"]), ("fresh_copy", False),
                             ("min_effect_percent", 2), ("sudo", True), ("extra", ["-O2"]),
                             ("canonical_inline_pair", True), ("require_identical_output", True),
                             ("extra_by_variant", {"a": [], "b": ["-O2"]})):
            with self.subTest(field=field):
                self._reject(lambda data: data[3][1]["compare"]["config"].__setitem__(field, value))
        self._reject(lambda data: data[3][1]["compare"]["config"].__setitem__("unknown_profile_control", True))

    def test_profile_plan_and_inference_tampering_is_rejected(self):
        mutations = [
            lambda d: d[1][1].__setitem__("profile", receipt.LARGE),
            lambda d: d[1][1].__setitem__("ordinal", "8"),
            lambda d: d[1][1].__setitem__("family", "ab2"),
            lambda d: d[1][1].__setitem__("pairs", "80"),
            lambda d: d[3][1]["compare"]["plan"].__setitem__("pairs", 39),
            lambda d: d[3][1]["compare"]["plan"].__setitem__("order", "AB"),
            lambda d: d[3][1]["summary"]["plan"].__setitem__("confidence", 0.90),
            lambda d: d[3][1]["summary"]["plan"].__setitem__("bootstrap_resamples", 999),
            lambda d: d[3][1]["summary"]["plan"].__setitem__("complete_pairs", 39),
            lambda d: d[3][1]["summary"]["metrics"]["wall"].__setitem__("ci_low", 0.95),
            lambda d: d[3][1]["summary"]["checks"]["drift"].__setitem__("checked", False),
            lambda d: d[3][1]["summary"]["checks"]["order_effect"].__setitem__("flag", True),
            lambda d: d[3][1]["summary"]["verdict"].__setitem__("outcome", "faster"),
            lambda d: d[3][1]["summary"]["verdict"].__setitem__("min_effect_percent", 2),
            lambda d: d[3][1]["summary"]["verdict"].__setitem__("n", 999),
            lambda d: d[3][1]["summary"]["host"].__setitem__("git_revision", HEAD),
        ]
        for index, change in enumerate(mutations):
            with self.subTest(index=index):
                self._reject(change)

    def test_raw_samples_reject_hash_count_output_and_order_tampering(self):
        mutations = [
            lambda d: d[3][1]["compare"]["variants"]["a"].__setitem__("sha256", "0" * 64),
            lambda d: d[3][1]["compare"]["variants"]["b"].__setitem__("size_bytes", 1),
            lambda d: d[3][1]["summary"]["baseline"].__setitem__("identical_runs", 39),
            lambda d: d[3][1]["pairs"].pop(),
            lambda d: d[3][1]["pairs"].append(d[3][1]["pairs"][0]),
            lambda d: d[3][1]["pairs"][0].__setitem__("identical", False),
            lambda d: d[3][1]["pairs"][0].__setitem__("pair", 2),
            lambda d: d[3][1]["pairs"][0].__setitem__("variant", "b"),
            lambda d: d[3][1]["pairs"][0].__setitem__("order", "BA"),
            lambda d: d[3][1]["pairs"][0].__setitem__("span_s", 0),
            lambda d: d[3][1]["pairs"][0].__setitem__("span_s", float("nan")),
            lambda d: d[3][1]["pairs"][0].__setitem__("exit", True),
            lambda d: d[3][1]["compare"]["steps"]["prepare"].__setitem__("status", "failed"),
            lambda d: d[3][1]["summary"]["steps"].__setitem__("timed", "skipped"),
            lambda d: d[3][1]["summary"]["host"].__setitem__("cpu_model", "GitHub hosted x86_64"),
        ]
        for index, change in enumerate(mutations):
            with self.subTest(index=index):
                self._reject(change)

    def test_missing_invalid_failed_cancelled_not_run_are_incomplete(self):
        for state in ("failed", "cancelled", "invalid", "not_run", "process-complete", None):
            with self.subTest(state=state):
                self._reject(lambda data: data[1][1].__setitem__("state", state))
        for flag in ("exit_status", "timed_out", "cleanup_failed", "capture_failed"):
            self._reject(lambda data: data[1][1].__setitem__(flag, "1"))
        self._reject(lambda data: data[3][1].__setitem__("compare", None))
        self._reject(lambda data: data[3][1].__setitem__("pairs", None))
        self._reject(lambda data: data[3][1].__setitem__("summary", None))
        self._reject(lambda data: data[3].pop(1))
        self._reject(lambda data: data[1].pop())

    def test_identity_closure_and_occupancy_bindings_fail_closed(self):
        mutations = [
            lambda d: d[0].__setitem__("campaign", "1" * 64),
            lambda d: d[0].__setitem__("base", "1" * 40),
            lambda d: d[0].__setitem__("trusted_revision", "1" * 40),
            lambda d: d[0].__setitem__("cpu", "1"),
            lambda d: d[0].__setitem__("routine_enabled", "true"),
            lambda d: d[1][1].__setitem__("closure_before", "1" * 64),
            lambda d: d[1][1].__setitem__("closure_after", "1" * 64),
            lambda d: d[2].__setitem__("captured_input_files_unchanged", "false"),
            lambda d: d[2].__setitem__("qualification_state", "qualified"),
            lambda d: d[2].__setitem__("physical_packet_wall_us", "0"),
            lambda d: d[2].__setitem__("prep_us", "1000001"),
            lambda d: d[4]["executor"].__setitem__("physical_packet_wall_us", 1000001),
        ]
        for index, change in enumerate(mutations):
            with self.subTest(index=index):
                self._reject(change)

    def test_only_authenticated_fresh_owner_requests_can_join(self):
        mutations = [
            lambda d: d[4].__setitem__("authenticated", False),
            lambda d: d[4]["request"].__setitem__("actor", "untrusted-user"),
            lambda d: d[4]["request"].__setitem__("selector", "profile: compiler-compare-v1"),
            lambda d: d[4]["request"].__setitem__("freeze_revision", ""),
            lambda d: d[4]["request"].__setitem__("request_head", SHA),
            lambda d: d[4]["executor"].__setitem__("request_run_id", "997"),
            lambda d: d[4]["executor"].__setitem__("repository", "attacker/buster"),
            lambda d: d[4]["executor"].__setitem__("cpu_model", "Zen 5 runner label"),
            lambda d: d[4]["executor"].__setitem__("run_attempt", "2"),
            lambda d: d[4]["attempts"].append(copy.deepcopy(d[4]["attempts"][-1])),
            lambda d: d[4]["attempts"][0].__setitem__("reservation_seconds", 0),
            lambda d: d[4]["attempts"][-1].__setitem__("state", "cancelled"),
            lambda d: d[4]["attempts"].pop(0),
        ]
        for index, change in enumerate(mutations):
            with self.subTest(index=index):
                self._reject(change)

    def test_prior_failures_are_retained_without_campaign_success(self):
        data = list(copy.deepcopy(self.confirm))
        data[4]["attempts"][0]["state"] = "failed"
        shown = receipt.validate_packet(*data)
        self.assertEqual(shown["authenticated_attempt_history"][0]["state"], "failed")
        self.assertEqual(shown["qualification_state"], "unqualified")
        self.assertTrue(shown["outstanding_qualification"])

    def test_historical_profile_contract_is_unchanged(self):
        self.assertEqual(compiler_receipt.PROFILE["name"], "compiler-compare-v1")
        self.assertEqual(compiler_receipt.PROFILE["target_minutes"], 10)
        self.assertEqual(compiler_receipt.PROFILE["warmups"], 1)
        self.assertEqual(compiler_receipt.THROUGHPUT_PROFILE["modes"], ["fast", "quality"])
        self.assertEqual(compiler_receipt.THROUGHPUT_PROFILE["workloads"],
                         ["tiny_startup", "large_function", "many_functions", "symbol_table", "control_flow", "backend_pressure"])

    def test_duplicate_tsv_and_missing_packet_data_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "identity.tsv"
            path.write_text("schema\tone\nschema\ttwo\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                receipt.read_tsv(path)
            shown = receipt.read_packet(root, copy.deepcopy(self.confirm[4]))
            self.assertEqual(shown["packet_state"], "incomplete")
            self.assertEqual(shown["qualification_state"], "unqualified")


if __name__ == "__main__":
    unittest.main()
