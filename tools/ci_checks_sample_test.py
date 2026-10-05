#!/usr/bin/env python3
"""Pure retained-evidence controls; no CI campaign, compiler or binary runs."""
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import ci_checks_sample as sample
import ci_checks_qualification as qualification
import ci_checks_qualification_test as fixtures


class SampleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.fixture = fixtures.QualificationTests()
        self.fixture.root = self.root

    def generated(self, item, condition):
        output = self.root / "derived"
        output.mkdir()
        return sample.desktop(self.root, {key: value for key, value in item.items() if key != "tests"}, condition, output, 0)

    def validate(self, item, condition):
        return qualification.desktop(self.root, item, self.fixture.run_fixture(), condition, "combined-overlap")

    def rewrite(self, item, name, value):
        item[name] = fixtures.reference(self.root, item[name]["path"], value)

    def test_actual_serial_runtime_rows_preserve_census_and_compile_only_rows(self):
        item, coverage, condition = self.fixture.complete_desktop()
        before = self.validate(item, condition)
        generated = self.generated(item, condition)
        self.assertEqual(before, self.validate(generated, condition))
        runtime = {row["id"] for row in coverage["expected"] if row["execution"] == "runtime" and row["id"] in before["selected"]}
        self.assertEqual(Counter(row["row_id"] for row in generated["tests"]), Counter(runtime))
        self.assertEqual(len(runtime), 2)

    def test_canonical_and_direct_runtime_keep_their_actual_audit_policy(self):
        for direct in (False, True):
            with self.subTest(direct=direct), tempfile.TemporaryDirectory() as directory:
                self.root = self.fixture.root = Path(directory)
                item, _, condition = self.fixture.complete_desktop(release=True, direct=direct)
                generated = self.generated(item, condition)
                self.assertEqual(self.validate(item, condition), self.validate(generated, condition))
                manifest = qualification.record(self.root, generated["tests"][0]["manifest"])
                self.assertTrue(manifest["identity"]["table_audits"])

    def partitioned(self, item, workers):
        directory = self.root / item["phase_directory"]
        summary = qualification.record(self.root, item["phases"])
        event = next(event for event in summary["events"] if event["phase"] == "test")
        argv = ["fixture-driver", "test_units_partitioned", event["argv"][0]]
        for suffix in ("start", "end"):
            path = directory / (event["id"] + "." + str(event["pid"]) + "." + suffix + ".json")
            value = qualification.phases.read(path)
            value["argv"] = argv
            if suffix == "end":
                value["test_jobs"] = str(workers)
            path.write_text(json.dumps(value) + "\n")
        observation_path = directory.parent / "unit-observations" / event["id"] / "observation.json"
        observation = qualification.phases.read(observation_path)
        observation["argv"] = argv
        observation_path.write_text(json.dumps(observation) + "\n")
        plan_path = directory / "plan.json"
        plan = qualification.phases.read(plan_path)
        test_task = next(task for task in plan["tasks"] if task["id"] == event["id"])
        parent = next(task for task in plan["tasks"] if task["tree"] == test_task["tree"] and task["phase"] == "validation" and task["configuration"] == test_task["configuration"])
        for quota_task in (test_task, parent):
            quota_task["inner_jobs"] = workers
        for path in directory.glob(parent["id"] + ".*.end.json"):
            value = qualification.phases.read(path)
            value["test_jobs"] = str(workers)
            path.write_text(json.dumps(value) + "\n")
        plan_path.write_text(json.dumps(plan) + "\n")
        return event, observation_path

    def replay(self, item):
        result = qualification.record(self.root, item["result"])
        coverage = qualification.record(self.root, item["coverage"])
        summary = qualification.phases.analyze(self.root / item["phase_directory"], coverage, result["metadata"])
        result["matrix_phases"] = summary
        self.rewrite(item, "phases", summary)
        self.rewrite(item, "result", result)

    def test_partition_command_with_low_quota_uses_real_serial_fallback(self):
        item, _, condition = self.fixture.complete_desktop()
        self.partitioned(item, 3)
        self.replay(item)
        generated = self.generated(item, condition)
        self.validate(generated, condition)
        modes = [qualification.record(self.root, row["manifest"])["mode"] for row in generated["tests"]]
        self.assertEqual(modes, ["serial", "serial"])

    def test_four_worker_partition_requires_and_validates_complete_group_proof(self):
        item, _, condition = self.fixture.complete_desktop()
        event, observation_path = self.partitioned(item, 4)
        observation = qualification.phases.read(observation_path)
        inventory = qualification.unit_campaign.inventory(observation_path.parent / "inventory.log")
        lines = [f"CI_UNIT_PLAN_V1 binary_sha256={'e' * 64} source_revision={'a' * 40} workers=4 groups=2 group_workers=2"]
        for group, index, count in (("driver", 0, 1), ("rest", 1, 2)):
            for row in inventory:
                owner = "driver" if row["name"] == "compiler_driver_tests" else "rest"
                lines.append(f"CI_UNIT_MODULE_V1 index={row['index']} module={row['name']} table_audit={int(row['table_audit'])} enabled={int(not row['table_audit'])} selected={int(not row['table_audit'] and owner == group)} group={owner}")
            lines += [f"TEST_MODULE_TIMING index={index} module={inventory[index]['name']} duration_ns=1 passed={count} failed=0 assertions={count} status=pass",
                      f"CI_UNIT_BATCH_V1 group={group} modules=1 modules_passed=1 assertions={count} passed={count} failed=0 external=0 external_passed=0 status=pass",
                      f"[{count}/{count}] Unit tests (1 of 3 modules selected)", "[1/1] Module tests", "[0/0] External tests",
                      f"CI_UNIT_PROCESS_V1 group={group} workers=2 elapsed_us=6 start_us=1 end_us=7 exit=0 native_status=0 timed_out=0 capture_failed=0 cleanup_failed=0 status=pass"]
        lines.append("CI_UNIT_PARTITION_V1 workers=4 groups=2 modules=2 assertions=3 passed=3 failed=0 elapsed_us=10 status=pass")
        log_path = observation_path.parent / "test.log"
        log_path.write_text("\n".join(lines) + "\n")
        observation["log_sha256"] = hashlib.sha256(log_path.read_bytes()).hexdigest()
        observation_path.write_text(json.dumps(observation) + "\n")
        self.replay(item)
        generated = self.generated(item, condition)
        result = self.validate(generated, condition)
        manifest = next(qualification.record(self.root, row["manifest"]) for row in generated["tests"] if qualification.record(self.root, row["observation"])["id"] == event["id"])
        self.assertEqual(manifest["mode"], "groups")
        self.assertEqual(manifest["test_workers"], 4)
        self.assertEqual(len(result["census"]), 2)
        log_path.write_text("\n".join(lines[1:]) + "\n")
        observation["log_sha256"] = hashlib.sha256(log_path.read_bytes()).hexdigest()
        observation_path.write_text(json.dumps(observation) + "\n")
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            broken = sample.desktop(self.root, {key: value for key, value in item.items() if key != "tests"}, condition, Path(directory), 0)
            with self.assertRaisesRegex(ValueError, "parent/process"):
                self.validate(broken, condition)

    def test_raw_diagnostic_bytes_are_preserved_but_invalid_proof_bytes_fail(self):
        item, _, condition = self.fixture.complete_desktop()
        test = item["tests"][0]
        observation_path = self.root / test["observation"]["path"]
        observation = qualification.phases.read(observation_path)
        log_path = observation_path.parent / "test.log"
        raw = b"command: --\xff\xc2\n" + log_path.read_bytes()
        log_path.write_bytes(raw)
        observation["log_sha256"] = hashlib.sha256(raw).hexdigest()
        observation_path.write_text(json.dumps(observation) + "\n")
        generated = self.generated(item, condition)
        self.validate(generated, condition)
        self.assertEqual(log_path.read_bytes(), raw)
        raw += b"TEST_MODULE_TIMING\xff index=0\n"
        log_path.write_bytes(raw)
        observation["log_sha256"] = hashlib.sha256(raw).hexdigest()
        observation_path.write_text(json.dumps(observation) + "\n")
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            invalid = sample.desktop(self.root, {key: value for key, value in item.items() if key != "tests"}, condition, Path(directory), 0)
            with self.assertRaisesRegex(ValueError, "Invalid UTF-8"):
                self.validate(invalid, condition)

    def test_duplicate_capabilities_and_changed_native_journals_are_refused(self):
        item, coverage, condition = self.fixture.complete_desktop()
        coverage["detected"].append(copy.deepcopy(coverage["detected"][0]))
        self.rewrite(item, "coverage", coverage)
        with self.assertRaisesRegex(ValueError, "duplicate capability"):
            self.generated(item, condition)
        coverage["detected"].pop()
        self.rewrite(item, "coverage", coverage)
        terminal = self.root / item["phase_directory"] / "terminal.json"
        value = qualification.phases.read(terminal)
        value["result"] = 1
        terminal.write_text(json.dumps(value) + "\n")
        with tempfile.TemporaryDirectory(dir=self.root) as directory, self.assertRaises(ValueError):
            sample.desktop(self.root, {key: value for key, value in item.items() if key != "tests"}, condition, Path(directory), 0)

    def test_stale_log_digest_and_wrong_native_source_run_or_command_fail(self):
        for change in ("log", "source_revision", "run_id", "argv"):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                self.root = self.fixture.root = Path(directory)
                item, _, condition = self.fixture.complete_desktop()
                observation_path = self.root / item["tests"][0]["observation"]["path"]
                observation = qualification.phases.read(observation_path)
                if change == "log":
                    log = observation_path.parent / "test.log"
                    log.write_bytes(log.read_bytes() + b"extra diagnostic\n")
                else:
                    observation[change] = ["wrong-command", "test"] if change == "argv" else "f" * 40
                    observation_path.write_text(json.dumps(observation) + "\n")
                before = {file: file.read_bytes() for file in self.root.rglob("*") if file.is_file()}
                with self.assertRaises(ValueError):
                    generated = self.generated(item, condition)
                    self.validate(generated, condition)
                self.assertTrue(all(file.read_bytes() == raw for file, raw in before.items()))

    def input(self):
        run = self.fixture.run_fixture()
        steps = ("Combination matrix (Windows)", "Combination matrix (Linux, macOS)", "Install verified Zig", "Desktop result and reproduction", "Retain desktop logs", "Workflow tool regression tests", "Bootstrap wrapper regression tests", "Execution-mode matrix (Windows)", "Execution-mode matrix", "Native configuration differential matrix", "Test (iOS simulator)", "Test (Android)", "Validate every GitHub workflow", "Require every shard", "Verify every desktop partition exists", "Build compiler and boot both architectures in all allocators", "Exercise analyzer failure and coverage controls", "Compare reference analysis and aggregate all module shards")
        for job in run["jobs"]:
            job["steps"] = [{"name": name, "conclusion": "success"} for name in steps]
        run["head_branch"] = "codex/2120-evidence-v2-combined-overlap"
        conditions = self.fixture.conditions_fixture(run)
        entries = []
        for shard in ("release", "checks"):
            directory = self.root / shard
            directory.mkdir()
            self.fixture.root = directory
            item, _, _ = self.fixture.complete_desktop(shard=shard)
            entry = {key: value for key, value in item.items() if key != "tests"}
            for key in ("coverage", "result", "phases"):
                entry[key] = dict(entry[key], path=shard + "/" + entry[key]["path"])
            entry["phase_directory"] = shard + "/" + entry["phase_directory"]
            entries.append(entry)
        self.fixture.root = self.root
        value = {"schema": sample.SCHEMA, "cohort": {"name": qualification.PROSPECTIVE_COHORT, "head_sha": "a" * 40, "workflow_blob_sha": "b" * 40},
                 "variant": "combined-overlap", "run": fixtures.reference(self.root, "run.json", run),
                 "conditions": fixtures.reference(self.root, "conditions.json", conditions), "desktops": entries}
        path = self.root / "input.json"
        path.write_text(json.dumps(value) + "\n")
        return path, value

    def test_final_strict_sample_replay_with_all_job_conditions_and_multiple_artifacts(self):
        path, value = self.input()
        validated_conditions = qualification.conditions(self.root, value["conditions"], qualification.record(self.root, value["run"]))
        # Validate all21 job conditions before narrowing desktop enumeration to
        # one complete synthetic platform. Reuse that validated condition tuple;
        # strict sample/desktop/journal readers remain real. This is no CI proof.
        names = ("Windows x86-64 release", "Windows x86-64 checks")
        with mock.patch.object(qualification, "cohort_desktop_jobs", return_value=names), mock.patch.object(qualification, "conditions", return_value=validated_conditions):
            result = sample.assemble(path, "sample-123.json")
            item = qualification.record(self.root, result["sample"])
            observed = qualification.sample(self.root, item, qualification.PROSPECTIVE_COHORT)
        self.assertEqual(observed["run_id"], 123)
        self.assertEqual(len(item["desktops"]), 2)
        self.assertFalse(result["performance_accepted"])
        self.assertEqual(result["resource_review"], "pending")

    def test_unknown_conditions_and_declared_pin_drift_cannot_publish(self):
        path, value = self.input()
        value["cohort"]["head_sha"] = "c" * 40
        path.write_text(json.dumps(value) + "\n")
        with self.assertRaisesRegex(ValueError, "declared cohort"):
            sample.assemble(path, "sample.json")
        value["cohort"]["head_sha"] = "a" * 40
        conditions = qualification.record(self.root, value["conditions"])
        conditions["jobs"]["Workflow lint"]["image_os"] = "unknown"
        value["conditions"] = fixtures.reference(self.root, "conditions.json", conditions)
        path.write_text(json.dumps(value) + "\n")
        with self.assertRaisesRegex(ValueError, "runner image"):
            sample.assemble(path, "sample.json")
        self.assertFalse((self.root / "sample.json").exists())
        self.assertFalse((self.root / "sample-tests").exists())

    def test_final_validation_failure_cleans_only_own_new_output(self):
        path, value = self.input()
        validated_conditions = qualification.conditions(self.root, value["conditions"], qualification.record(self.root, value["run"]))
        before = {file: file.read_bytes() for file in self.root.rglob("*") if file.is_file()}
        with mock.patch.object(qualification, "cohort_desktop_jobs", return_value=("Windows x86-64 release", "Windows x86-64 checks")), mock.patch.object(qualification, "conditions", return_value=validated_conditions), mock.patch.object(qualification, "sample", side_effect=ValueError("strict rejection")):
            with self.assertRaisesRegex(ValueError, "strict rejection"):
                sample.assemble(path, "sample.json")
        self.assertFalse((self.root / "sample.json").exists())
        self.assertFalse((self.root / "sample-tests").exists())
        self.assertEqual(before, {file: file.read_bytes() for file in self.root.rglob("*") if file.is_file()})

    def test_exclusive_open_collision_and_write_close_failure_preserve_ownership(self):
        path = self.root / "sample.json"
        path.write_bytes(b"other writer")
        with self.assertRaises(FileExistsError):
            sample.write_json(path, {})
        self.assertEqual(path.read_bytes(), b"other writer")
        original = Path.open
        path.unlink()
        class FailedClose:
            def __enter__(self):
                self.stream = original(path, "x", encoding="utf-8")
                return self.stream
            def __exit__(self, *args):
                self.stream.close()
                raise OSError("flush failed")
        with mock.patch.object(Path, "open", return_value=FailedClose()):
            with self.assertRaisesRegex(OSError, "flush failed"):
                sample.write_json(path, {})
        self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main()
