#!/usr/bin/env python3
"""Evidence-integrity controls for the opt-in offline unit-test measurement tool."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location("ci_unit_tests_measure", Path(__file__).with_name("ci_unit_tests_measure.py"))
MEASURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MEASURE)

IDENTITY = dict(source_revision="a" * 40, binary_sha256="b" * 64, runner_image="windows-2025/fixture-image",
                platform="windows", architecture="x86_64", configuration="Debug", sanitize=True,
                fuzz=True, table_audits=False, toolchain="fixture clang", cpu_budget=4)
INVENTORY = [dict(index=0, name="c_frontend_tests", table_audit=False),
             dict(index=1, name="compiler_driver_tests", table_audit=False),
             dict(index=2, name="table_tests", table_audit=True)]


def module(index, name, count, duration=100):
    return f"TEST_MODULE_TIMING index={index} module={name} duration_ns={duration} passed={count} failed=0 assertions={count} status=pass\n"


def terminal(count, modules, selected=False):
    selection = f" ({modules} of 3 modules selected)" if selected else ""
    return f"[{count}/{count}] Unit tests{selection}\n[{modules}/{modules}] Module tests\n[0/0] External tests\n"


def serial_log():
    return module(0, "c_frontend_tests", 11) + module(1, "compiler_driver_tests", 7) + terminal(18, 2)


def group_log(group, duration, start=100):
    lines = []
    for row in INVENTORY:
        owner = "driver" if row["name"] == "compiler_driver_tests" else "rest"
        enabled = not row["table_audit"]
        selected = enabled and owner == group
        lines.append(f"CI_UNIT_MODULE_V1 index={row['index']} module={row['name']} table_audit={int(row['table_audit'])} enabled={int(enabled)} selected={int(selected)} group={owner}\n")
    name, index, count = ("compiler_driver_tests", 1, 7) if group == "driver" else ("c_frontend_tests", 0, 11)
    lines.append(module(index, name, count))
    lines.append(f"CI_UNIT_BATCH_V1 group={group} modules=1 modules_passed=1 assertions={count} passed={count} failed=0 external=0 external_passed=0 status=pass\n")
    lines.append(terminal(count, 1, True))
    lines.append(f"CI_UNIT_PROCESS_V1 group={group} workers=2 elapsed_us={duration} start_us={start} end_us={start + duration} exit=0 native_status=0 timed_out=0 capture_failed=0 cleanup_failed=0 status=pass\n")
    return "".join(lines)


def parallel_log():
    plan = f"CI_UNIT_PLAN_V1 binary_sha256={'b' * 64} source_revision={'a' * 40} workers=4 groups=2 group_workers=2\n"
    partition = "CI_UNIT_PARTITION_V1 workers=4 groups=2 modules=2 assertions=18 passed=18 failed=0 elapsed_us=1000 status=pass\n"
    return plan + group_log("driver", 700) + group_log("rest", 600) + partition


class MeasurementTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def sample(self, name="sample", arm="baseline", log=None, identity=None):
        grouped = arm == "candidate"
        text = parallel_log() if grouped else serial_log()
        if log is not None:
            text = log
        (self.root / f"{name}.log").write_text(text)
        manifest = dict(schema="buster-ci-unit-tests-measure-v1", arm=arm, mode="groups" if grouped else "serial",
                        identity=copy.deepcopy(IDENTITY if identity is None else identity), inventory=copy.deepcopy(INVENTORY),
                        log=f"{name}.log", exit_code=0, test_workers=4 if grouped else 2,
                        elapsed_us=1000 if grouped else 1500)
        path = self.root / f"{name}.json"
        path.write_text(json.dumps(manifest))
        return path

    def mutate(self, path, change):
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))

    def test_serial_and_groups_preserve_exact_inventory_without_ci_claim(self):
        baseline = MEASURE.validate_sample(self.sample())
        candidate = MEASURE.validate_sample(self.sample("candidate", "candidate"))
        self.assertEqual(baseline["assertions"], candidate["assertions"])
        self.assertEqual(candidate["peak_declared_child_workers"], 4)
        self.assertEqual(candidate["summed_child_wall_us"], 1300)
        self.assertEqual(candidate["skipped_table_audits"], ["table_tests"])
        self.assertFalse(candidate["ci_complete"])
        self.assertFalse(candidate["performance_accepted"])

    def test_actual_workers_are_separate_from_overall_budget(self):
        baseline = MEASURE.validate_sample(self.sample())
        candidate = MEASURE.validate_sample(self.sample("candidate", "candidate"))
        self.assertEqual(baseline["identity"]["cpu_budget"], 4)
        self.assertEqual(baseline["test_workers"], 2)
        self.assertEqual(baseline["peak_declared_child_workers"], 2)
        self.assertEqual(candidate["test_workers"], 4)
        self.assertEqual(candidate["groups"]["driver"]["test_workers"], 2)
        path = self.sample()
        self.mutate(path, lambda row: row.pop("test_workers"))
        self.assertIsNone(MEASURE.validate_sample(path)["peak_declared_child_workers"])
        self.mutate(path, lambda row: row.update(test_workers=5))
        with self.assertRaisesRegex(MEASURE.EvidenceError, "exceed CPU budget"):
            MEASURE.validate_sample(path)
        path = self.sample(arm="candidate")
        self.mutate(path, lambda row: row.update(test_workers=2))
        with self.assertRaisesRegex(MEASURE.EvidenceError, "native parent plan"):
            MEASURE.validate_sample(path)

    def test_windows_utf16_and_actions_prefix_are_read(self):
        path = self.sample()
        text = "".join("2026-09-30T12:00:00.0000000Z " + line + "\r\n" for line in serial_log().splitlines())
        (self.root / "sample.log").write_bytes(text.encode("utf-16"))
        self.assertEqual(MEASURE.validate_sample(path)["assertions"], 18)

    def test_missing_terminal_fails(self):
        with self.assertRaisesRegex(MEASURE.EvidenceError, "Missing terminal"):
            MEASURE.validate_sample(self.sample(log=serial_log().replace("[0/0] External tests\n", "")))

    def test_duplicate_module_fails(self):
        with self.assertRaisesRegex(MEASURE.EvidenceError, "Duplicate executed"):
            MEASURE.validate_sample(self.sample(log=serial_log() + module(0, "c_frontend_tests", 11)))

    def test_canonical_index_mismatch_fails(self):
        with self.assertRaisesRegex(MEASURE.EvidenceError, "Canonical index"):
            MEASURE.validate_sample(self.sample(log=serial_log().replace("index=1", "index=7")))

    def test_terminal_assertion_mismatch_fails(self):
        with self.assertRaisesRegex(MEASURE.EvidenceError, "assertion count"):
            MEASURE.validate_sample(self.sample(log=serial_log().replace("[18/18]", "[19/19]")))

    def test_failed_module_and_missing_exit_proof_fail(self):
        bad = serial_log().replace("passed=11 failed=0", "passed=10 failed=1").replace("status=pass", "status=fail", 1)
        with self.assertRaisesRegex(MEASURE.EvidenceError, "Failed module"):
            MEASURE.validate_sample(self.sample(log=bad))
        path = self.sample()
        self.mutate(path, lambda row: row.pop("exit_code"))
        with self.assertRaisesRegex(MEASURE.EvidenceError, "exit status"):
            MEASURE.validate_sample(path)

    def test_missing_group_and_batch_fail(self):
        log = parallel_log().replace(group_log("rest", 600), "")
        with self.assertRaises(MEASURE.EvidenceError):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))
        log = parallel_log().replace("CI_UNIT_BATCH_V1 group=driver", "REMOVED group=driver")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "batch record"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))

    def test_disabled_audit_and_unselected_module_proofs_fail(self):
        log = parallel_log().replace("module=table_tests table_audit=1 enabled=0", "module=table_tests table_audit=1 enabled=1")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "audit inventory"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))
        log = parallel_log().replace("module=c_frontend_tests table_audit=0 enabled=1 selected=0", "module=c_frontend_tests table_audit=0 enabled=1 selected=1")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "selected inventory"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))

        log = parallel_log().replace("module=table_tests table_audit=1", "module=table_tests table_audit=0")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "table-audit flag"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))

    def test_every_process_failure_dimension_fails(self):
        for name in ("exit", "native_status", "timed_out", "capture_failed", "cleanup_failed"):
            with self.subTest(name=name), self.assertRaises(MEASURE.EvidenceError):
                log = parallel_log().replace(f"{name}=0", f"{name}=1", 1)
                MEASURE.validate_sample(self.sample(arm="candidate", log=log))

    def test_cpu_oversubscription_and_interval_mismatch_fail(self):
        log = parallel_log().replace("group_workers=2", "group_workers=3")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "CPU budget"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))

    def test_fair_outer_timer_requires_containment_and_retains_native_wall(self):
        path = self.sample(arm="candidate")
        self.mutate(path, lambda row: row.update(elapsed_us=1250))
        result = MEASURE.validate_sample(path)
        self.assertEqual(result["wall_us"], 1250)
        self.assertEqual(result["native_group_wall_us"], 1000)
        self.mutate(path, lambda row: row.pop("elapsed_us"))
        self.assertEqual(MEASURE.validate_sample(path)["wall_us"], 1000)
        self.mutate(path, lambda row: row.update(elapsed_us=999))
        with self.assertRaisesRegex(MEASURE.EvidenceError, "Outer wall interval"):
            MEASURE.validate_sample(path)
        log = parallel_log().replace("end_us=800", "end_us=801")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "interval mismatch"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))

    def test_parent_identity_and_partition_counts_fail(self):
        log = parallel_log().replace("binary_sha256=" + "b" * 64, "binary_sha256=" + "c" * 64)
        with self.assertRaisesRegex(MEASURE.EvidenceError, "identity mismatch"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))
        log = parallel_log().replace("groups=2 modules=2 assertions=18", "groups=2 modules=2 assertions=19")
        with self.assertRaisesRegex(MEASURE.EvidenceError, "terminal counts"):
            MEASURE.validate_sample(self.sample(arm="candidate", log=log))

    def campaign(self, arms=None):
        arms = arms or ["baseline", "candidate"] * 3
        paths = [self.sample(f"sample{i}", arm).name for i, arm in enumerate(arms)]
        path = self.root / "campaign.json"
        path.write_text(json.dumps(dict(schema="buster-ci-unit-tests-campaign-v1", samples=paths)))
        return path

    def test_three_alternating_pairs_compare_without_admission(self):
        result = MEASURE.compare(self.campaign())
        self.assertTrue(result["measurement_review_ready"])
        self.assertAlmostEqual(result["candidate_wall_ratio"], 2 / 3)
        self.assertAlmostEqual(result["candidate_summed_child_wall_ratio"], 1300 / 1500)
        self.assertFalse(result["ci_complete"])
        self.assertFalse(result["performance_accepted"])

    def test_campaign_non_alternating_or_insufficient_samples_fail(self):
        for arms in (["baseline", "candidate"] * 2, ["baseline"] * 3 + ["candidate"] * 3):
            with self.subTest(arms=arms), self.assertRaises(MEASURE.EvidenceError):
                MEASURE.compare(self.campaign(arms))

    def test_one_pair_screen_retains_limited_evidence_status(self):
        path = self.campaign(["baseline", "candidate"])
        result = MEASURE.compare(path, screening=True)
        self.assertTrue(result["screening_valid"])
        self.assertFalse(result["measurement_review_ready"])
        self.assertFalse(result["performance_accepted"])
        self.assertEqual(result["arms"]["baseline"]["n"], 1)
        with self.assertRaises(MEASURE.EvidenceError):
            MEASURE.compare(path)
        with self.assertRaises(MEASURE.EvidenceError):
            MEASURE.compare(self.campaign(), screening=True)

    def test_different_binary_image_configuration_and_counts_fail(self):
        for field, value in (("binary_sha256", "c" * 64), ("runner_image", "other"), ("configuration", "Release")):
            with self.subTest(field=field), self.assertRaises(MEASURE.EvidenceError):
                path = self.campaign()
                self.mutate(self.root / "sample2.json", lambda row: row["identity"].update({field: value}))
                MEASURE.compare(path)
        path = self.campaign()
        log = self.root / "sample2.log"
        log.write_text(serial_log().replace("passed=11 failed=0 assertions=11", "passed=12 failed=0 assertions=12").replace("[18/18]", "[19/19]"))
        with self.assertRaisesRegex(MEASURE.EvidenceError, "Module count/status differs"):
            MEASURE.compare(path)

    def test_cli_returns_structured_failure(self):
        path = self.sample(log="truncated\n")
        output = self.root / "result.json"
        self.assertEqual(MEASURE.main(["sample", str(path), "--output", str(output)]), 1)
        self.assertFalse(json.loads(output.read_text())["measurement_review_ready"])


if __name__ == "__main__":
    unittest.main()
