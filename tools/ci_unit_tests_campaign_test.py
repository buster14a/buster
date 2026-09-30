#!/usr/bin/env python3
"""Fail-closed controls for retained native unit-test campaign observations."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import ci_unit_tests_campaign as campaign


ROWS = [(0, "c_frontend_tests", False), (1, "compiler_driver_tests", False), (2, "table_tests", True)]
ENVIRONMENT = {"BUSTER_TEST_SOURCE_REVISION": "a" * 40, "BUSTER_TEST_JOBS": "2", "BUSTER_TEST_TABLE_AUDITS": "0",
               "ImageOS": "ubuntu26", "ImageVersion": "fixture-image", "RUNNER_ARCH": "X64", "RUNNER_OS": "Linux"}


def inventory_log():
    lines = []
    for index, name, audit in ROWS:
        owner = "driver" if name == "compiler_driver_tests" else "rest"
        lines.append(f"CI_UNIT_MODULE_V1 index={index} module={name} table_audit={int(audit)} enabled={int(not audit)} selected=0 group={owner}\n")
    lines.append("CI_UNIT_BATCH_V1 group=inventory modules=0 modules_passed=0 assertions=0 passed=0 failed=0 external=0 external_passed=0 status=inventory\n")
    lines.append("[0/0] Unit tests (0 of 3 modules selected)\n[0/0] Module tests\n[0/0] External tests\n")
    return "".join(lines)


def module(index, name, count):
    return f"TEST_MODULE_TIMING index={index} module={name} duration_ns=100 passed={count} failed=0 assertions={count} status=pass\n"


def terminal(count, modules, selected=False):
    selection = f" ({modules} of 3 modules selected)" if selected else ""
    return f"[{count}/{count}] Unit tests{selection}\n[{modules}/{modules}] Module tests\n[0/0] External tests\n"


def group_log(group, duration):
    lines = []
    for index, name, audit in ROWS:
        owner = "driver" if name == "compiler_driver_tests" else "rest"
        lines.append(f"CI_UNIT_MODULE_V1 index={index} module={name} table_audit={int(audit)} enabled={int(not audit)} selected={int(not audit and owner == group)} group={owner}\n")
    index, name, count = (1, "compiler_driver_tests", 7) if group == "driver" else (0, "c_frontend_tests", 11)
    lines.append(module(index, name, count))
    lines.append(f"CI_UNIT_BATCH_V1 group={group} modules=1 modules_passed=1 assertions={count} passed={count} failed=0 external=0 external_passed=0 status=pass\n")
    lines.append(terminal(count, 1, True))
    lines.append(f"CI_UNIT_PROCESS_V1 group={group} workers=2 elapsed_us={duration} start_us=100 end_us={100 + duration} exit=0 native_status=0 timed_out=0 capture_failed=0 cleanup_failed=0 status=pass\n")
    return "".join(lines)


class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.phases = self.directory / "phases"
        self.phases.mkdir()
        self.binary = self.directory / "ide"
        self.binary.write_bytes(b"controlled fixture binary")
        (self.directory / "inventory.log").write_text(inventory_log())
        (self.directory / "toolchain.txt").write_text("fixture clang\n")
        self.environment = copy.deepcopy(ENVIRONMENT)
        self.populate(1)

    def tearDown(self):
        self.temporary.cleanup()

    def populate(self, pairs, baseline_jobs="2"):
        binary_hash = hashlib.sha256(self.binary.read_bytes()).hexdigest()
        baseline = module(0, "c_frontend_tests", 11) + module(1, "compiler_driver_tests", 7) + terminal(18, 2)
        candidate = f"CI_UNIT_PLAN_V1 binary_sha256={binary_hash} source_revision={'a' * 40} workers=4 groups=2 group_workers=2\n"
        candidate += group_log("driver", 700) + group_log("rest", 600)
        candidate += "CI_UNIT_PARTITION_V1 workers=4 groups=2 modules=2 assertions=18 passed=18 failed=0 elapsed_us=1000 status=pass\n"
        for number in range(1, pairs * 2 + 1):
            arm = "baseline" if number % 2 else "candidate"
            identifier = f"sample-{number}-{arm}"
            (self.directory / (identifier + ".log")).write_text(baseline if arm == "baseline" else candidate)
            common = {"id": identifier, "epoch_us": 1, "pid": number, "start_us": 100,
                      "argv": ["cmake", "--build", "build", "--target", "test_units" if arm == "baseline" else "test_units_partitioned"]}
            start = dict(common, state="running")
            end = dict(common, state="success", child_start_us=110, end_us=2110 if arm == "baseline" else 1610,
                       publication_start_us=2200, result=0, platform_status=0, spawned=1, timed_out=0,
                       termination_requested=0, forcibly_terminated=0, test_jobs=baseline_jobs if arm == "baseline" else "4", cpu_time="unknown", peak_rss="unknown")
            (self.phases / f"{identifier}.{number}.start.json").write_text(json.dumps(start))
            (self.phases / f"{identifier}.{number}.end.json").write_text(json.dumps(end))

    def mutate_end(self, change):
        path = self.phases / "sample-1-baseline.1.end.json"
        end = json.loads(path.read_text())
        change(end)
        path.write_text(json.dumps(end))

    def assemble(self, pairs=1):
        return campaign.assemble(self.directory, self.binary, "linux", pairs, self.environment)

    def test_screening_uses_equal_outer_intervals_and_actual_binary(self):
        result = self.assemble()
        self.assertEqual(result["candidate_wall_ratio"], 0.75)
        self.assertEqual(result["samples"][1]["native_group_wall_us"], 1000)
        self.assertEqual(result["samples"][1]["wall_us"], 1500)
        self.assertEqual(result["identity"]["binary_sha256"], hashlib.sha256(self.binary.read_bytes()).hexdigest())
        self.assertEqual(len(result["native_phases"]), 2)
        self.assertTrue(result["screening_valid"])
        self.assertFalse(result["ci_complete"])
        self.assertFalse(result["performance_accepted"])

    def test_three_pairs_require_all_six_invocations(self):
        self.populate(3)
        result = self.assemble(3)
        self.assertEqual(result["arms"]["candidate"]["n"], 3)
        self.assertTrue(result["measurement_review_ready"])
        self.assertFalse(result["screening_valid"])

    def test_missing_and_duplicate_inventory_indices_are_rejected(self):
        path = self.directory / "inventory.log"
        for mutation in (inventory_log().replace("index=1", "index=0"), inventory_log().replace("index=2", "index=3")):
            with self.subTest(mutation=mutation):
                path.write_text(mutation)
                with self.assertRaisesRegex(campaign.measure.EvidenceError, "canonical index"):
                    self.assemble()

    def test_inventory_policy_selection_and_failed_query_are_rejected(self):
        path = self.directory / "inventory.log"
        mutations = (inventory_log().replace("table_audit=1 enabled=0", "table_audit=1 enabled=1"),
                     inventory_log().replace("selected=0", "selected=1", 1),
                     inventory_log().replace("status=inventory", "status=fail"))
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                path.write_text(mutation)
                with self.assertRaises(campaign.measure.EvidenceError):
                    self.assemble()

    def test_query_must_have_clean_terminal_summaries(self):
        path = self.directory / "inventory.log"
        path.write_text(inventory_log().replace("[0/0] External tests\n", ""))
        with self.assertRaisesRegex(campaign.measure.EvidenceError, "Missing terminal"):
            self.assemble()

    def test_failed_exit_and_cleanup_flags_are_rejected(self):
        for key in ("result", "platform_status", "timed_out", "termination_requested", "forcibly_terminated"):
            with self.subTest(key=key):
                self.populate(1)
                self.mutate_end(lambda end: end.update({key: 1}))
                with self.assertRaisesRegex(campaign.measure.EvidenceError, "unsuccessful subprocess"):
                    self.assemble()

    def test_stale_or_mismatched_phase_identity_is_rejected(self):
        for change in (lambda end: end.update(epoch_us=2), lambda end: end.update(pid=7),
                       lambda end: end.update(argv=["different", "invocation"])):
            with self.subTest(change=change):
                self.populate(1)
                self.mutate_end(change)
                with self.assertRaisesRegex(campaign.measure.EvidenceError, "identity differs"):
                    self.assemble()

    def test_missing_and_duplicate_phase_records_are_rejected(self):
        path = self.phases / "sample-1-baseline.1.end.json"
        duplicate = self.phases / "sample-1-baseline.7.end.json"
        duplicate.write_text(path.read_text())
        with self.assertRaisesRegex(campaign.measure.EvidenceError, "missing or duplicate"):
            self.assemble()
        duplicate.unlink()
        path.unlink()
        with self.assertRaisesRegex(campaign.measure.EvidenceError, "missing or duplicate"):
            self.assemble()

    def test_phase_wall_must_contain_native_partition(self):
        path = self.phases / "sample-2-candidate.2.end.json"
        end = json.loads(path.read_text())
        end["end_us"] = 111
        path.write_text(json.dumps(end))
        with self.assertRaisesRegex(campaign.measure.EvidenceError, "Outer wall interval"):
            self.assemble()

    def test_equal_budget_baseline_uses_four_workers(self):
        self.environment["BUSTER_UNIT_BASELINE_JOBS"] = "4"
        with self.assertRaisesRegex(campaign.measure.EvidenceError, "subprocess test quota differs"):
            self.assemble()
        self.populate(3, baseline_jobs="4")
        result = self.assemble(3)
        self.assertEqual(result["arms"]["baseline"]["test_workers"], 4)
        self.assertIn("Baseline modules use 4 workers", campaign.summary(result))

    def test_unsupported_baseline_quota_is_rejected(self):
        for value in ("", "1", "3", "8", "four"):
            with self.subTest(value=value):
                self.environment["BUSTER_UNIT_BASELINE_JOBS"] = value
                with self.assertRaisesRegex(campaign.measure.EvidenceError, "Baseline worker quota"):
                    self.assemble()

    def test_missing_runner_source_quota_or_image_identity_is_rejected(self):
        for key in ("BUSTER_TEST_SOURCE_REVISION", "BUSTER_TEST_JOBS", "BUSTER_TEST_TABLE_AUDITS", "ImageOS", "ImageVersion", "RUNNER_ARCH"):
            with self.subTest(key=key):
                self.environment = copy.deepcopy(ENVIRONMENT)
                self.environment.pop(key)
                with self.assertRaises(campaign.measure.EvidenceError):
                    self.assemble()


if __name__ == "__main__":
    unittest.main()
