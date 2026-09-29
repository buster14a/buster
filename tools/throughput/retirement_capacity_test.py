#!/usr/bin/env python3
"""Tests for the A1 campaign capacity model (retirement_capacity.py).

The model is arithmetic over the checked-in sources. These tests pin its
derivation against the C collector's own capacity figures
(retirement_campaign_test.h) and show that a realistic A1 campaign fits the
4,096-entry and 128 GiB store caps once per-batch metrics artifacts are
packed into shards. No performance or admission claim follows.
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/throughput"))
sys.path.insert(0, str(ROOT / "tools"))
import retirement_capacity as capacity  # noqa: E402


def c_fixture_groups(per_configuration=(416, 4, 1, 1, 1), members=None):
    """The realistic A1 shape of retirement_campaign_test.h (411 members per
    configuration, 16 configurations, two stage singletons)."""
    members = members or (406, 2, 1, 1, 1)
    groups = [{"kind": "object", "inputs": inputs, "members": count}
              for _ in range(16) for inputs, count in zip(per_configuration, members)]
    groups.extend({"kind": "singleton", "inputs": 1, "members": 1} for _ in range(2))
    untimed = [{"kind": "object", "inputs": inputs, "members": inputs}
               for _ in range(11 * 16) for inputs in per_configuration]
    return groups, untimed


class CapacityModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = capacity.source_limits(ROOT)
        cls.report = capacity.build_report(ROOT, require_committed=False)

    def test_source_limits_are_the_unchanged_caps(self):
        source = self.source
        self.assertEqual((source["store_files"], source["store_total_bytes"]), (4096, 128 * 1024 ** 3))
        self.assertEqual(source["store_file_bytes"], 64 * 1024 * 1024)
        self.assertEqual((source["minimum_pairs"], source["maximum_pairs"]), (60, 254))
        self.assertEqual(source["sample_total_records"], 39_518_208)
        self.assertEqual(source["sample_partitions"], 3)
        self.assertEqual((source["command_arguments"], source["command_bytes"]), (256, 65536))
        self.assertEqual((source["metrics_line_bytes"], source["metrics_artifact_bytes"]),
                         (1024 * 1024, 64 * 1024 * 1024))
        self.assertEqual(source["metrics_shard_bytes"], source["store_file_bytes"])
        self.assertEqual((source["input_list_bytes"], source["input_list_arguments"]),
                         (4 * 1024 * 1024, 65536))
        self.assertEqual(source["untimed_batches_per_group"], 4)
        self.assertEqual(source["transcript_record_bytes_max"], 1018)
        self.assertEqual(source["untimed_record_bytes_max"], 747)
        self.assertIn("sum_u(4*batch(n_u))", source["budget_derivation"])

    def test_invocations_follow_the_a1_formula(self):
        # (G + U) * 2 * (warmups + rounds * pairs) per stage.
        groups, untimed = c_fixture_groups()
        for pairs in (60, 254):
            for runtime in (0, 2):
                model = capacity.campaign_model(groups, runtime, pairs, untimed, 4096, self.source)
                self.assertEqual(model["invocations_per_stage"], (82 + runtime) * 2 * (2 + 2 * pairs))
                self.assertEqual(model["metrics_artifacts_per_stage"], 80 * 2 * (2 + 2 * pairs))
                self.assertEqual(model["row_samples_per_stage"], (16 * 411 + 2) * 2 * pairs)
                self.assertEqual(model["batch_samples_per_stage"], 80 * 2 * pairs)
                self.assertEqual(model["untimed_batches"], 880 * 4)

    def test_model_matches_the_c_collector_capacity(self):
        # The same figures retirement_campaign_test.h pins for
        # tp_retirement_campaign_capacity.
        groups, untimed = c_fixture_groups()
        expected = {(60, 4096): (465, 15492729152), (254, 4096): (1805, 60859132512),
                    (254, 8192): (3525, 118631213664), (60, 16384): (1785, 59736607040)}
        for (pairs, per_input), (files, total) in expected.items():
            model = capacity.campaign_model(groups, 2, pairs, untimed, per_input, self.source)
            with self.subTest(pairs=pairs, per_input=per_input):
                self.assertEqual((model["payload_files"], model["payload_bytes_upper_bound"]), (files, total))
                self.assertTrue(model["fits"])
        rejected = capacity.campaign_model(groups, 2, 254, untimed, 16384, self.source)
        self.assertFalse(rejected["fits"])
        self.assertFalse(rejected["entries_fit"] or rejected["bytes_fit"])
        model = capacity.campaign_model(groups, 2, 254, untimed, 4096, self.source)
        self.assertEqual((model["metrics_shards_per_stage_upper_bound"],
                          model["untimed_metrics_shards_upper_bound"]), (854, 38))
        self.assertEqual(capacity.maximum_fitting_per_input(groups, 2, 254, untimed, self.source), 9472)
        self.assertEqual(capacity.maximum_fitting_per_input(groups, 2, 60, untimed, self.source), 37888)

    def test_one_entry_per_metrics_artifact_could_not_fit(self):
        # Before M4 each object batch wrote its own store entry; a realistic
        # campaign needs tens of thousands, far above the 4,096-entry store.
        groups, untimed = c_fixture_groups()
        for pairs in (60, 254):
            model = capacity.campaign_model(groups, 2, pairs, untimed, 4096, self.source)
            self.assertGreater(model["entries_if_each_metrics_artifact_were_a_store_file"],
                               10 * self.source["store_files"])
            self.assertLess(model["payload_files"], model["entry_cap_after_minimum_external_entries"])

    def test_metrics_shard_bound_is_the_greedy_rotation_bound(self):
        cap = self.source["metrics_shard_bytes"]
        self.assertEqual(capacity.metrics_shard_bound(244, 12992512, cap), 2)
        self.assertEqual(capacity.metrics_shard_bound(4, 3 * (cap // 2 - 1024) + 16, cap), 4)
        self.assertEqual(capacity.metrics_shard_bound(1000, 10 * cap, cap), 20)
        self.assertEqual(capacity.metrics_shard_bound(0, 0, cap), 0)
        # Two consecutive greedy shards always exceed one shard's capacity, so
        # n shards of T bytes satisfy floor(n / 2) * cap < T.
        for total in (1, cap, cap + 1, 7 * cap - 3):
            bound = capacity.metrics_shard_bound(10 ** 9, total, cap)
            self.assertLess((bound // 2 - 1) * cap, total)

    def test_inputs_are_validated(self):
        groups, untimed = c_fixture_groups()
        for pairs in (58, 61, 256):
            with self.subTest(pairs=pairs), self.assertRaises(ValueError):
                capacity.campaign_model(groups, 2, pairs, untimed, 4096, self.source)
        with self.assertRaises(ValueError):
            capacity.campaign_model(groups, 3, 60, untimed, 4096, self.source)  # Runtime in an object group.
        with self.assertRaises(ValueError):
            capacity.campaign_model([], 0, 60, untimed, 4096, self.source)
        with self.assertRaises(ValueError):  # One artifact above the 64 MiB cap.
            capacity.campaign_model(groups, 2, 60, untimed, 1 << 20, self.source)
        with self.assertRaises(ValueError):
            capacity.metrics_artifact_bound(self.source["batch_inputs"] + 1, 1, 4096, self.source)

    def test_report_derives_the_groups_from_the_support_declaration(self):
        population = self.report["population"]
        self.assertEqual(population["native_host_configurations"], 16)
        self.assertEqual(population["timed_object_groups"], 80)
        self.assertEqual(population["stage_singleton_groups"], 2)
        self.assertEqual([group["recipe"] for group in population["recipe_groups_per_configuration"]],
                         ["compiler-default", "c23", "c23-dialect-assertions", "x86-avx512", "x86-cx16"])
        self.assertEqual(sum(group["members"] for group in population["recipe_groups_per_configuration"]),
                         405)
        self.assertEqual(population["timed_rows_envelope"], 405 * 16 + 2)
        self.assertEqual(population["untimed_groups"], 11 * 16 * 5)
        self.assertLessEqual(population["largest_batch_inputs"], self.source["batch_inputs"])
        self.assertLessEqual(population["largest_input_list_bytes"], self.source["input_list_bytes"])
        self.assertTrue(population["batch_argv_is_constant_in_inputs"])

    def test_report_scenarios_fit_at_the_254_pair_maximum(self):
        scenarios = self.report["scenarios"]
        for per_input in (4096, 8192):
            for runtime in (0, 2):
                model = scenarios[f"pairs-254/per-input-{per_input}/runtime-{runtime}"]
                with self.subTest(per_input=per_input, runtime=runtime):
                    self.assertTrue(model["fits"])
                    self.assertLessEqual(model["payload_files"], 4093)
                    self.assertLessEqual(model["payload_bytes_upper_bound"], 128 * 1024 ** 3)
        self.assertFalse(scenarios["pairs-254/per-input-16384/runtime-2"]["fits"])
        self.assertTrue(scenarios["pairs-60/per-input-16384/runtime-2"]["fits"])
        maxima = self.report["maximum_fitting_per_input_metrics_bound"]
        self.assertGreaterEqual(maxima["pairs-254"], 8192)

    def test_report_keeps_the_recipe_blocked_and_the_budget_unpinned(self):
        self.assertEqual(self.report["source"]["profile_status"], "blocked")
        self.assertFalse(self.report["source"]["campaign_budget_pinned"])
        budget = self.report["reviewed_budget"]
        self.assertFalse(budget["pinned"])
        self.assertFalse(budget["one_hour_worker_budget_applies"])
        self.assertFalse(budget["illustrative_rates_are_a_reviewed_bound"])
        self.assertEqual(budget["compiler_batches_both_stages"], 82 * 2 * 2 * (2 + 2 * 254))
        self.assertEqual(budget["untimed_batches"], 3520)
        self.assertFalse(self.report["evidence"]["fixture_or_model_is_acceptance"])
        text = capacity.render_text(self.report)
        self.assertIn("verdict=fits", text)
        self.assertIn("campaign_budget_pinned=false", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
