#!/usr/bin/env python3
"""Deterministic phase/coverage joins plus real native observer failure controls."""
import copy
import json
import ntpath
import os
from pathlib import Path
import posixpath
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import ci_matrix_phases as phases

ROOT = Path(__file__).resolve().parents[1]


def write(root, name, data):
    (root / name).write_text(json.dumps(data) + "\n", encoding="utf-8")


def fixture(root, direct=False):
    identity = dict(lane_id="fixture", source_revision="a" * 40, source_tree="b" * 40, source_hash="c" * 64,
                    driver_hash="d" * 64, repository="buster14a/buster", run_id="123", run_attempt="1", workflow="CI", job="test",
                    platform="macos" if direct else "windows", architecture="x86_64", shard="checks")
    coverage = dict(identity=identity.copy(), phase="complete", expected=[], detected=[], obligations={})
    plan = dict(schema=phases.SCHEMA, epoch_us=1, identity=identity, scheduler="direct" if direct else "pooled",
                outer_jobs=1 if direct else 4, logical_cpus=4, cpu_budget=4, cpu_time="unknown", peak_rss="unknown", trees=[], tasks=[])
    for i, (compiler, config) in enumerate((("clang", "Debug"), ("clang", "Release"), ("cl", "Debug"), ("gcc", "Debug"), ("zig", "Debug"))):
        name, row = f"tree{i}", f"row{i}"
        coverage["expected"].append(dict(id=row, compiler=compiler, configuration=config, state="required", owner_shard="checks", sanitize=compiler == "clang", fuzz=False, unity=False))
        coverage["detected"].append(dict(id=row, compiler=compiler, path=compiler, path_hash="e" * 64, identity=compiler, version="1", target="fixture"))
        plan["trees"].append(dict(id=name, rows=[row], build_directory=f"build/{name}", compiler=compiler, compiler_path=compiler,
                                 compiler_sha256="e" * 64, compiler_identity=compiler, compiler_version="1", target="fixture", configurations=config,
                                 sanitize=int(compiler == "clang"), fuzz=0, lto=False, generator="Ninja Multi-Config", linker="DEFAULT"))
    def task(tree, phase, config, start, end, dep="ready", pool=""):
        name = phases.task_id(tree, phase, config)
        plan["tasks"].append(dict(id=name, tree=tree, phase=phase, configuration=config, dependency=dep, pool_edge=pool, inner_jobs=1, argv=[]))
        argv = ["fixture", name]
        if phase in ("build", "validation", "post_test"):
            argv = ["fixture-cmake", "--build", f"build/{tree}", "--parallel", "1"]
        elif phase == "test":
            argv = [f"build/{tree}/{config}/ide" + ("" if direct else ".exe"), "test"]
        common = dict(id=name, epoch_us=1, pid=10 + len(plan["tasks"]), start_us=start, argv=argv)
        if phase == "evidence":
            common["authority"] = "driver_callback"
        write(root, f"{name}.{common['pid']}.start.json", dict(common, state="running"))
        write(root, f"{name}.{common['pid']}.end.json", dict(common, state="success", child_start_us=start, end_us=end, publication_start_us=end,
              result=0, platform_status=0, spawned=1, timed_out=0, termination_requested=0, forcibly_terminated=0, cpu_time="unknown", peak_rss="unknown", test_jobs="1", ctest_jobs="not-applicable"))
        if dep == "ready":
            write(root, f"{name}.ready.json", dict(epoch_us=1, ready_us=start))
    for i in range(5):
        task(f"tree{i}", "configure", "", 2 if i < 4 else 10, 10 if i < 4 else 15)
    if not direct:
        task("matrix", "scheduler", "", 20, 300)
    for i, tree in enumerate(plan["trees"]):
        name, config = tree["id"], tree["configurations"]
        if not direct:
            task(name, "build", "", 30 if i < 4 else 130, 130 if i < 4 else 180, "scheduler", f"build-{name}")
        elif i >= 2:
            task(name, "build", config, 180 + 50 * i, 190 + 50 * i)
        if i < 2:
            start, end = 180 + (50 if direct else 40) * i, 210 + (50 if direct else 40) * i
            task(name, "validation", config, start, end, "ready" if direct else phases.task_id(name, "build"), "" if direct else f"validation-{name}")
            task(name, "test", config, start + 5, end - 5, "nested")
    task("matrix", "evidence", "coverage", 390, 395)
    write(root, "plan.json", plan)
    write(root, "terminal.json", dict(epoch_us=1, terminal_us=400, result=0))
    return coverage


def resource_fixture(platform="windows", status="observed"):
    windows = platform == "windows"
    scope = "process" if windows else "process-and-waited-descendants"
    values = (12000, 3000, 64 * 1024 * 1024) if status == "observed" else ("unknown",) * 3
    error = 5 if status == "error" else 0
    return dict(schema=phases.RESOURCE_SCHEMA,
                cpu=dict(status=status, source="get-process-times" if windows else "wait4", scope=scope,
                         unit="microseconds", user=values[0], system=values[1], error=error),
                peak_memory=dict(status=status, source="k32-get-process-memory-info" if windows else "wait4-ru_maxrss",
                                 scope=scope, unit="bytes", kind="peak-working-set" if windows else "largest-individual-high-water",
                                 value=values[2], error=error))


class PhaseValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.coverage = fixture(self.root)

    def mutate(self, pattern, change):
        path = next(self.root.glob(pattern))
        value = phases.read(path)
        change(value)
        write(self.root, path.name, value)

    def check(self):
        return phases.analyze(self.root, self.coverage)

    def test_resource_witness_replays_in_events_without_aggregate_claim(self):
        witness = resource_fixture()
        self.mutate("tree0-test-Debug.*.end.json", lambda p: p.update(resources=witness))
        summary = self.check()
        event = next(e for e in summary["events"] if e["id"] == "tree0-test-Debug")
        self.assertEqual(event["resources"], witness)
        self.assertEqual(event["cpu_time"], "unknown")
        self.assertEqual(event["peak_rss"], "unknown")
        self.assertTrue(all(t["cpu_time"] == t["peak_rss"] == "unknown" for t in summary["trees"]))
        self.assertEqual(summary, self.check())

    def test_callback_resource_witness_fails_closed(self):
        self.mutate("matrix-evidence-coverage.*.end.json", lambda p: p.update(resources=resource_fixture()))
        with self.assertRaisesRegex(ValueError, "callback"):
            self.check()

    def test_concurrent_completion_stable_and_second_wave(self):
        first = self.check()
        self.assertTrue(first["complete"])
        self.assertEqual(first["max_observed_pool_overlap"], 4)
        fifth = next(t for t in first["trees"] if t["id"] == "tree4")
        self.assertEqual(fifth["wait_us"], 110)
        self.assertEqual(fifth["admission_order"], 5)
        self.assertEqual(first["trees"][0]["id"], "tree1")
        self.assertEqual(first["predictions"]["orders_evaluated"], 120)
        self.assertFalse(first["predictions"]["acceptance"])
        self.assertEqual(first, self.check())

    def test_detected_capability_duplicates_are_rejected_before_projection(self):
        self.assertTrue(self.check()["complete"])
        original = copy.deepcopy(self.coverage["detected"])
        conflicting = dict(original[0], version="conflicting")
        changes = {
            "identical-extra": original + [copy.deepcopy(original[0])],
            "conflicting-overwritten": [conflicting] + original,
            "identical-same-length": original[:1] + [copy.deepcopy(original[0])] + original[2:],
            "conflicting-same-length": original[:1] + [conflicting] + original[2:],
        }
        for name, detected in changes.items():
            with self.subTest(change=name):
                self.coverage["detected"] = detected
                with self.assertRaisesRegex(ValueError, "detected capability"):
                    self.check()

    def test_detected_capability_census_rejects_missing_foreign_and_malformed_records(self):
        original = copy.deepcopy(self.coverage["detected"])
        changes = [None, False, 1, "records", {}, [], original[:-1],
                   original[:-1] + [dict(original[-1], id="foreign")]]
        for row in (None, False, 1, "record", [], {}, {"id": None}, {"id": False},
                    {"id": 1}, {"id": []}, {"id": {}}, {"id": ""}):
            changes.append(original[:-1] + [row])
        for detected in changes:
            with self.subTest(detected=detected):
                self.coverage["detected"] = detected
                with self.assertRaisesRegex(ValueError, "detected capability"):
                    self.check()
        self.coverage.pop("detected")
        with self.assertRaisesRegex(ValueError, "detected capability"):
            self.check()

    def test_absent_admission_preserves_overlap_and_explicit_policy(self):
        self.assertEqual(self.check()["test_admission"], "overlap")
        self.mutate("plan.json", lambda p: p.update(test_admission="overlap"))
        self.assertEqual(self.check()["test_admission"], "overlap")

    def test_unknown_admission_policy_fails_closed(self):
        for policy in ("weighted", "", None, False, ["all-builds"]):
            with self.subTest(policy=policy):
                self.mutate("plan.json", lambda p: p.update(test_admission=policy))
                with self.assertRaisesRegex(ValueError, "unknown test admission"):
                    self.check()

    def test_admission_policy_matches_explicit_current_job(self):
        self.assertTrue(phases.analyze(self.root, self.coverage, {"BUSTER_MATRIX_TEST_ADMISSION": ""})["complete"])
        self.assertTrue(phases.analyze(self.root, self.coverage, {"BUSTER_MATRIX_TEST_ADMISSION": "overlap"})["complete"])
        with self.assertRaisesRegex(ValueError, "current job mismatch: test admission"):
            phases.analyze(self.root, self.coverage, {"BUSTER_MATRIX_TEST_ADMISSION": "all-builds"})
        self.mutate("plan.json", lambda p: p.update(test_admission="all-builds"))
        self.assertTrue(phases.analyze(self.root, self.coverage, {"BUSTER_MATRIX_TEST_ADMISSION": "all-builds"})["complete"])
        with self.assertRaisesRegex(ValueError, "current job mismatch: test admission"):
            phases.analyze(self.root, self.coverage, {"BUSTER_MATRIX_TEST_ADMISSION": "overlap"})

    def test_all_builds_admission_is_windows_grouped_checks_only(self):
        plan = phases.read(self.root / "plan.json")
        plan["test_admission"] = "all-builds"
        for key, value in (("platform", "linux"), ("architecture", "aarch64"), ("shard", "release"),
                           ("shard", "sanitized-debug"), ("shard", "sanitized-release"), ("shard", "portability")):
            with self.subTest(key=key, value=value):
                candidate, coverage = copy.deepcopy(plan), copy.deepcopy(self.coverage)
                candidate["identity"][key] = coverage["identity"][key] = value
                with self.assertRaisesRegex(ValueError, "only valid for pooled Windows"):
                    phases.validate_plan(candidate, coverage, {})
        plan["scheduler"] = "direct"
        with self.assertRaisesRegex(ValueError, "only valid for pooled Windows"):
            phases.validate_plan(plan, self.coverage, {})

    def test_all_builds_admission_waits_for_compile_only_tree(self):
        self.mutate("plan.json", lambda p: p.update(test_admission="all-builds"))
        report = self.check()
        enqueue = next(e for e in report["timeline"] if e["task"] == phases.task_id("tree0", "validation", "Debug") and e["event"] == "enqueue")
        self.assertEqual(enqueue["time_us"], 180)
        self.mutate("tree0-validation-Debug.*.start.json", lambda v: v.update(start_us=179))
        self.mutate("tree0-validation-Debug.*.end.json", lambda v: v.update(start_us=179, child_start_us=179))
        with self.assertRaisesRegex(ValueError, "dependency overlap"):
            self.check()

    def test_all_builds_admission_retains_serialized_test_dependency(self):
        self.mutate("plan.json", lambda p: p.update(test_admission="all-builds"))
        self.chain(phases.task_id("tree0", "validation", "Debug"))
        report = self.check()
        enqueue = next(e for e in report["timeline"] if e["task"] == phases.task_id("tree1", "validation", "Release") and e["event"] == "enqueue")
        self.assertEqual(enqueue["time_us"], 210)
        self.assertEqual(report["predictions"]["current_model_us"], 210)

    def test_grouped_checks_and_separate_shards_join_same_rows(self):
        plan = phases.read(self.root / "plan.json")
        for row in self.coverage["expected"]:
            row["owner_shard"] = ("sanitized-debug" if row["configuration"] == "Debug" else "sanitized-release") if row["compiler"] == "clang" else "portability"
        self.assertEqual(len(phases.validate_plan(plan, self.coverage, {})[0]), 5)
        for shard, count in (("sanitized-debug", 1), ("sanitized-release", 1), ("portability", 3)):
            with self.subTest(shard=shard):
                candidate, coverage = copy.deepcopy(plan), copy.deepcopy(self.coverage)
                candidate["identity"]["shard"] = coverage["identity"]["shard"] = shard
                rows = {row["id"] for row in coverage["expected"] if row["owner_shard"] == shard}
                candidate["trees"] = [tree for tree in candidate["trees"] if set(tree["rows"]) <= rows]
                trees = {tree["id"] for tree in candidate["trees"]}
                candidate["tasks"] = [task for task in candidate["tasks"] if task["tree"] in trees or task["tree"] == "matrix"]
                candidate["outer_jobs"] = count
                self.assertEqual(len(phases.validate_plan(candidate, coverage, {})[0]), count)
                candidate["trees"].pop()
                with self.assertRaisesRegex(ValueError, "uniquely owned/exhaustive|invalid tree/task cardinality"):
                    phases.validate_plan(candidate, coverage, {})

    def test_unknown_desktop_shard_fails_closed(self):
        self.mutate("plan.json", lambda p: p["identity"].update(shard="checks-unknown"))
        self.coverage["identity"]["shard"] = "checks-unknown"
        with self.assertRaisesRegex(ValueError, "unknown desktop shard"):
            self.check()

    def producer_path_fixture(self, platform):
        paths = ntpath if platform == "windows" else posixpath
        source_directory = "D:/runner/work/buster/buster" if platform == "windows" else "/runner/work/buster/buster"
        self.coverage["identity"].update(platform=platform, source_path=paths.join(source_directory, "build.c"))
        self.mutate("plan.json", lambda p: p["identity"].update(platform=platform))
        for pattern in ("tree*-test-*.*.start.json", "tree*-test-*.*.end.json"):
            for path in self.root.glob(pattern):
                record = phases.read(path)
                relative = record["argv"][0]
                if platform != "windows":
                    relative = relative.removesuffix(".exe")
                record["argv"][0] = paths.join(source_directory, relative)
                write(self.root, path.name, record)

    def test_unix_journal_replays_from_foreign_working_directory(self):
        self.producer_path_fixture("linux")
        with mock.patch.object(phases.os, "getcwd", return_value="/different/checkout"):
            self.assertTrue(self.check()["complete"])

    def test_windows_journal_replays_with_native_path_and_case_rules(self):
        self.producer_path_fixture("windows")
        self.mutate("plan.json", lambda p: p["trees"][0].update(build_directory="build\\tree0"))
        for pattern in ("tree0-test-*.start.json", "tree0-test-*.end.json"):
            self.mutate(pattern, lambda v: v["argv"].__setitem__(0, v["argv"][0].upper().replace("/", "\\")))
        with mock.patch.object(phases.os, "getcwd", return_value="/different/checkout"):
            self.assertTrue(self.check()["complete"])

    def test_foreign_source_binding_rejects_another_checkout(self):
        for platform in ("windows", "linux"):
            with self.subTest(platform=platform):
                self.coverage = fixture(self.root)
                self.producer_path_fixture(platform)
                for pattern in ("tree0-test-*.start.json", "tree0-test-*.end.json"):
                    self.mutate(pattern, lambda v: v["argv"].__setitem__(0, v["argv"][0].replace("runner", "another")))
                with self.assertRaisesRegex(ValueError, "test executable/tree mismatch"):
                    self.check()

    def test_declared_source_path_requires_native_absolute_path(self):
        for platform, paths in (("windows", (None, False, [], "", "build.c", "C:build.c", "/unix/build.c")),
                                ("linux", (None, False, [], "", "build.c", "C:/source/build.c"))):
            for source_path in paths:
                with self.subTest(platform=platform, source_path=source_path):
                    self.coverage = fixture(self.root)
                    self.coverage["identity"].update(platform=platform, source_path=source_path)
                    self.mutate("plan.json", lambda p: p["identity"].update(platform=platform))
                    with self.assertRaisesRegex(ValueError, "source path must be absolute"):
                        self.check()

    def test_missing_source_path_retains_local_fixture_paths(self):
        self.assertNotIn("source_path", self.coverage["identity"])
        self.assertTrue(self.check()["complete"])

    def test_apple_sanitizer_shards_keep_shared_tree(self):
        plan = phases.read(self.root / "plan.json")
        for shard in ("sanitized-debug", "sanitized-release"):
            with self.subTest(shard=shard):
                candidate, coverage = copy.deepcopy(plan), copy.deepcopy(self.coverage)
                candidate["identity"].update(platform="macos", shard=shard)
                coverage["identity"].update(platform="macos", shard=shard)
                with self.assertRaisesRegex(ValueError, "Apple sanitizer trees require grouped checks"):
                    phases.validate_plan(candidate, coverage, {})

    def test_nested_setup_conserves_enclosing_child_phase_time(self):
        for direct in (False, True):
            for setup_us in (0, 4):
                with self.subTest(direct=direct, setup_us=setup_us), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    coverage = fixture(root, direct=direct)
                    original = phases.analyze(root, coverage)
                    parent_path = next(root.glob("tree0-validation-Debug.*.end.json"))
                    parent = phases.read(parent_path)
                    parent["child_start_us"] += 2
                    write(root, parent_path.name, parent)
                    child_path = next(root.glob("tree0-test-Debug.*.end.json"))
                    child = phases.read(child_path)
                    child["child_start_us"] += setup_us
                    write(root, child_path.name, child)
                    report = phases.analyze(root, coverage)
                    tree = next(value for value in report["trees"] if value["id"] == "tree0")
                    outer = [event for event in report["events"] if event["tree"] == "tree0" and event["phase"] != "test"]
                    observed_us = sum(event["end_us"] - event["child_start_us"] for event in outer)
                    self.assertEqual(sum(tree["elapsed_us"].values()), observed_us)
                    self.assertEqual(tree["elapsed_us"]["test"], 20 - setup_us)
                    self.assertEqual(tree["elapsed_us"]["post_test"], 5)
                    self.assertEqual(tree["elapsed_us"]["build"], (100 if not direct else 0) + 3 + setup_us)
                    self.assertEqual(report["predictions"], original["predictions"])
                    self.assertEqual(set(tree["elapsed_us"]), set(original["trees"][0]["elapsed_us"]))
                    print("PHASE_ACCOUNTING_CONTROL " + json.dumps(dict(
                        scheduler=report["scheduler"], nested_setup_us=setup_us,
                        exclusive_child_us=observed_us, elapsed_us=tree["elapsed_us"]), sort_keys=True))

    def test_positive_nested_setup_keeps_failure_and_missing_records_fatal(self):
        self.mutate("tree0-test-*.end.json", lambda value: value.update(child_start_us=value["start_us"] + 4))
        self.assertTrue(self.check()["complete"])
        path = next(self.root.glob("tree0-test-*.end.json"))
        original = phases.read(path)
        for changed in (dict(original, state="failure", result=1), dict(original, platform_status=256),
                        dict(original, child_start_us=original["end_us"] + 1)):
            write(self.root, path.name, changed)
            with self.assertRaises(ValueError):
                self.check()
        write(self.root, path.name, original)
        path.unlink()
        with self.assertRaisesRegex(ValueError, "failed/cancelled/interrupted publication"):
            self.check()

    def test_direct_and_pooled_same_phase_schema(self):
        pooled = self.check()
        with tempfile.TemporaryDirectory() as temp:
            coverage = fixture(Path(temp), direct=True)
            direct = phases.analyze(Path(temp), coverage)
        self.assertEqual(set(pooled["trees"][0]["elapsed_us"]), set(direct["trees"][0]["elapsed_us"]))
        self.assertEqual(direct["scheduler"], "direct")

    def test_missing_tree_or_phase(self):
        next(self.root.glob("tree4-build-*.start.json")).unlink()
        with self.assertRaisesRegex(ValueError, "never admitted"):
            self.check()

    def test_duplicate_admission(self):
        path = next(self.root.glob("tree4-build-*.start.json"))
        shutil.copyfile(path, self.root / "tree4-build-all.999.start.json")
        with self.assertRaisesRegex(ValueError, "admitted twice"):
            self.check()

    def test_unknown_and_mismatched_identity(self):
        self.mutate("tree4-build-*.end.json", lambda v: v.update(id="tree99-build-all"))
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.check()

    def test_unknown_file(self):
        write(self.root, "tree99-build-all.12.end.json", {})
        with self.assertRaisesRegex(ValueError, "unknown/partial"):
            self.check()

    def test_failure_timeout_cancellation_and_interruption(self):
        path = next(self.root.glob("tree0-configure-*.end.json"))
        original = phases.read(path)
        for phase in ("configure", "build", "test"):
            target = next(self.root.glob(f"tree0-{phase}-*.end.json"))
            value = phases.read(target)
            for state in ("failure", "timeout", "cancelled"):
                write(self.root, target.name, dict(value, state=state, result=1))
                with self.assertRaisesRegex(ValueError, "unsuccessful"):
                    self.check()
            write(self.root, target.name, value)
        path.write_text('{"partial":', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "interrupted publication"):
            self.check()
        write(self.root, path.name, original)
        path.unlink()
        with self.assertRaisesRegex(ValueError, "interrupted publication"):
            self.check()

    def test_child_exit_authority_not_label(self):
        self.mutate("tree0-test-*.end.json", lambda v: v.update(platform_status=256))
        with self.assertRaisesRegex(ValueError, "exit authority"):
            self.check()

    def test_callback_exit_authority_is_separate(self):
        self.mutate("matrix-evidence-coverage.*.end.json", lambda v: v.update(authority="process"))
        with self.assertRaisesRegex(ValueError, "callback exit authority"):
            self.check()

    def test_nonmonotonic_and_impossible_overlap(self):
        self.mutate("tree0-test-*.end.json", lambda v: v.update(end_us=1))
        with self.assertRaisesRegex(ValueError, "non-monotonic"):
            self.check()

    def test_nested_test_cannot_escape_parent(self):
        self.mutate("tree0-test-*.end.json", lambda v: v.update(end_us=280, publication_start_us=280))
        with self.assertRaisesRegex(ValueError, "test overlap"):
            self.check()

    def test_fifth_tree_cannot_be_admitted_before_slot(self):
        for kind in ("start", "end"):
            self.mutate(f"tree4-build-*.{kind}.json", lambda v: v.update(start_us=40))
        self.mutate("tree4-build-*.end.json", lambda v: v.update(child_start_us=40))
        with self.assertRaisesRegex(ValueError, "pool overlap"):
            self.check()

    def test_rows_uniquely_owned_and_exhaustive(self):
        self.mutate("plan.json", lambda p: p["trees"][4].update(rows=["row3"]))
        with self.assertRaises(ValueError):
            self.check()

    def test_missing_task_in_plan_not_empty_success(self):
        self.mutate("plan.json", lambda p: p["tasks"].pop())
        with self.assertRaisesRegex(ValueError, "task identities"):
            self.check()

    def test_command_for_another_tree_cannot_claim_this_identity(self):
        for kind in ("start", "end"):
            self.mutate(f"tree0-test-*.{kind}.json", lambda v: v.update(argv=["build/tree1/Debug/ide.exe", "test"]))
        with self.assertRaisesRegex(ValueError, "executable/tree mismatch"):
            self.check()

    def test_recorded_inner_ninja_quota_must_match_plan(self):
        for kind in ("start", "end"):
            self.mutate(f"tree0-build-*.{kind}.json", lambda v: v["argv"].__setitem__(-1, "99"))
        with self.assertRaisesRegex(ValueError, "Ninja quota mismatch"):
            self.check()

    def test_source_job_compiler_and_resource_binding(self):
        for key in ("GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB"):
            with self.assertRaisesRegex(ValueError, "job mismatch"):
                phases.analyze(self.root, self.coverage, {key: "changed"})
        self.mutate("plan.json", lambda p: p["trees"][0].update(compiler_version="changed"))
        with self.assertRaisesRegex(ValueError, "compiler mismatch"):
            self.check()

    def test_duplicate_json_key(self):
        path = self.root / "terminal.json"
        path.write_text('{"result":0,"result":1}\n')
        with self.assertRaisesRegex(ValueError, "duplicate JSON"):
            self.check()

    def test_missing_pool_identity_cannot_hide_overlap(self):
        def change(plan):
            next(t for t in plan["tasks"] if t["pool_edge"]).update(pool_edge="", dependency="ready")
        self.mutate("plan.json", change)
        with self.assertRaisesRegex(ValueError, "admission/dependency"):
            self.check()

    def test_test_worker_quota_mismatch(self):
        self.mutate("tree0-validation-*.end.json", lambda v: v.update(test_jobs="17"))
        with self.assertRaisesRegex(ValueError, "quota mismatch"):
            self.check()

    def chain(self, after):
        def change(plan):
            next(t for t in plan["tasks"] if t["id"] == phases.task_id("tree1", "validation", "Release"))["after"] = after
        self.mutate("plan.json", change)

    def test_serialized_test_phase_waits_for_previous_tree(self):
        self.chain(phases.task_id("tree0", "validation", "Debug"))
        report = self.check()
        event = next(e for e in report["timeline"] if e["task"] == phases.task_id("tree1", "validation", "Release") and e["event"] == "enqueue")
        self.assertEqual(event["time_us"], 210)
        self.assertEqual(report["predictions"]["current_model_us"], 160)
        def early(value):
            value["start_us"] = value["child_start_us"] = 205
        self.mutate("tree1-validation-Release.*.start.json", lambda v: v.update(start_us=205))
        self.mutate("tree1-validation-Release.*.end.json", early)
        with self.assertRaisesRegex(ValueError, "dependency overlap"):
            self.check()

    def test_serialized_edge_must_name_another_trees_last_test_phase(self):
        for after, message in ((phases.task_id("tree1", "build"), "another tree"), (phases.task_id("tree0", "build"), "another tree"),
                               ("missing", "another tree")):
            with self.subTest(after=after):
                self.coverage = fixture(self.root)
                self.chain(after)
                with self.assertRaisesRegex(ValueError, message):
                    self.check()

    def test_partitioned_test_runner_command_is_the_trees_ide(self):
        runner = ["build/build.exe", "test_units_partitioned", "build/tree0/Debug/ide.exe"]
        for pattern in ("tree0-test-Debug.*.start.json", "tree0-test-Debug.*.end.json"):
            self.mutate(pattern, lambda v: v.update(argv=list(runner)))
        self.assertTrue(self.check()["complete"])
        for pattern in ("tree0-test-Debug.*.start.json", "tree0-test-Debug.*.end.json"):
            self.mutate(pattern, lambda v: v.update(argv=runner[:2] + ["build/tree1/Release/ide.exe"]))
        with self.assertRaisesRegex(ValueError, "test executable/tree mismatch"):
            self.check()

    def test_missing_coverage_terminal(self):
        self.coverage["phase"] = "planned"
        with self.assertRaisesRegex(ValueError, "coverage is incomplete"):
            self.check()

    def test_driver_failure_never_complete(self):
        self.mutate("terminal.json", lambda p: p.update(result=1))
        with self.assertRaisesRegex(ValueError, "failed/incomplete"):
            self.check()

    def test_summary_cannot_report_success_when_evidence_missing(self):
        import ci_summary
        env = dict(RUNNER_TEMP=str(self.root), BUSTER_CI_STEPS=json.dumps({"combinations": {"outcome": "success"}}),
                   BUSTER_CI_REQUIRED="combinations", BUSTER_MATRIX_PHASE_OUTPUT=str(self.root / "missing"))
        self.assertEqual(ci_summary.write_report(env), 1)
        result = json.loads((self.root / "buster-ci/result.json").read_text())
        self.assertFalse(result["success"])
        self.assertIn("matrix_phases", result["unsatisfied_steps"])


PLAN_FIXTURE = r'''
BUSTER_GLOBAL_LOCAL ProcessResult matrix_phase_fixture(Arena* arena)
{
    bool direct = environment_flag_is_on(S8("BUSTER_PHASE_FIXTURE_DIRECT"));
    bool checks = environment_flag_is_on(S8("BUSTER_PHASE_FIXTURE_CHECKS"));
    String8 shard = os_get_environment_variable(S8("BUSTER_PHASE_FIXTURE_SHARD"));
    if (!shard.length) { shard = checks ? S8("checks") : S8("release"); }
    checks = !string_equal(shard, S8("release"));
    bool linux_fixture = environment_flag_is_on(S8("BUSTER_PHASE_FIXTURE_LINUX"));
    MatrixCoverageTarget target = {.platform = direct ? S8("macos") : S8("windows"), .architecture = S8("x86_64"),
                                    .windows = !direct, .apple = direct};
    if (linux_fixture) { target.platform = S8("linux"); target.windows = 0; }
    MatrixCoverageManifest coverage = {0};
    coverage.lane = matrix_coverage_lane_create(arena, shard);
    coverage.lane.platform = target.platform;
    coverage.lane.architecture = target.architecture;
    // These serializer controls can describe a different platform than the
    // executing host; give that synthetic identity its own path syntax.
    if (!BUSTER_WINDOWS && target.windows) { coverage.lane.source_path = S8("C:/phase-fixture/build.c"); }
    if (BUSTER_WINDOWS && !target.windows) { coverage.lane.source_path = S8("/phase-fixture/build.c"); }
    coverage.mode = S8("phase-plan-fixture");
    bool ok = matrix_coverage_plan_build_for_target(arena, &coverage.plan, coverage.lane, target);
    coverage.obligations = matrix_coverage_obligations_for_lane(direct, !direct && !checks, &coverage.plan, coverage.lane.shard);
    for (u32 i = 0; i < BUILD_COMPILER_COUNT; i += 1)
    {
        coverage.capabilities[i] = (MatrixCoverageCapability){.path = build_compilers[i], .executable_hash = S8("fixture-sha"),
            .identity = build_compilers[i], .target = S8("fixture"), .version = S8("fixture"), .available = 1};
    }
    ok = matrix_phase_begin(arena, &coverage, direct) && ok;
    MatrixTestCombination combinations[16] = {0};
    MatrixTestTree trees[8] = {0};
    u32 count = 0, combo_count = 0;
    for (u32 i = 0; i < coverage.plan.tree_count; i += 1)
    {
        MatrixCoverageTreePlan tree = coverage.plan.trees[i];
        if (matrix_coverage_row_selected(coverage.plan.rows[tree.row_indices[0]], coverage.lane.shard))
        {
            Generate gen = {.build_directory = string_format(arena, S8("build/fixture-{u32}"), count), .compiler = tree.compiler,
                            .configuration_types = tree.configuration_types, .sanitize = tree.sanitize, .fuzz_available = tree.fuzz_available,
                            .ci = 1, .link_libc = 1, .include_tests = 1};
            String8 passthrough[] = {S8("-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY")};
            gen.cmake_arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(passthrough);
            CmakeBuildOptions release = {.optimize = 1, .optimize_set = 1};
            bool canonical = build_artifact_fanout_is_canonical(gen, release);
            gen = matrix_phase_tree(arena, gen, &coverage, tree);
            ok = ok && canonical == build_artifact_fanout_is_canonical(gen, release) &&
                 canonical == (tree.compiler == BUILD_COMPILER_CLANG && !tree.sanitize) &&
                 gen.cmake_arguments.length == 1 && gen.phase_arguments.length == 4 &&
                 string_equal(gen.cmake_arguments.pointer[0], passthrough[0]);
            String8 names[] = {S8("-DBUSTER_MATRIX_PHASE_DRIVER="), S8("-DBUSTER_MATRIX_PHASE_ROOT="),
                               S8("-DBUSTER_MATRIX_PHASE_TREE="), S8("-DBUSTER_MATRIX_PHASE_EPOCH=")};
            for (u32 a = 0; ok && a < BUSTER_ARRAY_LENGTH(names); a += 1)
            {
                ok = string_starts_with_sequence(gen.phase_arguments.pointer[a], names[a]);
            }
            ProcessRun* configure = run_add(arena, step_add(arena));
            String8* argv = arena_allocate(arena, String8, 1);
            argv[0] = S8("fixture-cmake");
            configure->arguments = (SliceString8){.pointer = argv, .length = 1};
            matrix_phase_wrap(arena, configure, matrix_phase_find_tree(gen.build_directory), S8("configure"), S8(""), 0);
            trees[count].build_directory = gen.build_directory;
            trees[count].parallel_jobs = 1;
            trees[count].runs_tests = tree.compiler == BUILD_COMPILER_CLANG;
            for (u32 r = 0; r < tree.row_count; r += 1)
            {
                MatrixCoverageRow row = coverage.plan.rows[tree.row_indices[r]];
                trees[count].combination_indices[r] = combo_count;
                trees[count].combination_count += 1;
                trees[count].unity_only = row.unity;
                trees[count].unity_analysis_scheduled = row.unity && coverage.obligations.unity_analysis_scheduled;
                combinations[combo_count++] = (MatrixTestCombination){.build_directory = gen.build_directory,
                    .compiler = tree.compiler, .options = {.config = row.configuration, .optimize = row.optimize}, .run_tests = tree.compiler == BUILD_COMPILER_CLANG};
                if (direct)
                {
                    String8 commands[] = {S8("fixture-cmake"), S8("--build"), gen.build_directory, S8("--config"), row.configuration,
                                          S8("--target"), tree.compiler == BUILD_COMPILER_CLANG ? S8("test_all") : S8("ide")};
                    if (row.unity)
                    {
                        ProcessRun* build = run_add(arena, step_add(arena));
                        String8* build_args = arena_allocate(arena, String8, 7);
                        memcpy(build_args, commands, sizeof(commands));
                        build_args[6] = S8("ide");
                        build->arguments = (SliceString8){.pointer = build_args, .length = 7};
                    }
                    ProcessRun* run = run_add(arena, step_add(arena));
                    String8* args = arena_allocate(arena, String8, 7);
                    memcpy(args, commands, sizeof(commands));
                    run->arguments = (SliceString8){.pointer = args, .length = 7};
                    if (row.unity) { clang_analyze_command_add(arena, gen.build_directory, (CmakeBuildOptions){.config = row.configuration}); }
                }
            }
            count += 1;
        }
    }
    if (!direct)
    {
        if (!checks)
        {
            BuildArtifactFanout* fanout = arena_allocate(arena, BuildArtifactFanout, 1);
            *fanout = (BuildArtifactFanout){.build_directory = trees[0].build_directory};
            ProcessRun* capture = run_add(arena, step_add(arena));
            *capture = (ProcessRun){.callback = build_artifact_fanout_capture_action, .callback_data = fanout};
            ProcessRun* clean = run_add(arena, step_add(arena));
            String8* argv = arena_allocate(arena, String8, 7);
            argv[0] = S8("fixture-cmake"); argv[1] = S8("--build"); argv[2] = trees[0].build_directory;
            argv[3] = S8("--config"); argv[4] = S8("Release"); argv[5] = S8("--target"); argv[6] = S8("clean");
            clean->arguments = (SliceString8){.pointer = argv, .length = 7};
            ProcessRun* cleaned = run_add(arena, step_add(arena));
            *cleaned = (ProcessRun){.callback = build_artifact_fanout_clean_action, .callback_data = fanout};
            if (linux_fixture)
            {
                X86CompletionCensusPlan* census = arena_allocate(arena, X86CompletionCensusPlan, 1);
                *census = (X86CompletionCensusPlan){.fanout = *fanout};
                ProcessRun* validate = run_add(arena, step_add(arena));
                *validate = (ProcessRun){.callback = x86_completion_census_existing_validate_action, .callback_data = census};
                ProcessRun* prepare = run_add(arena, step_add(arena));
                *prepare = (ProcessRun){.callback = x86_completion_census_prepare_output_action, .callback_data = census};
                ProcessRun* payload = run_add(arena, step_add(arena));
                String8* args = arena_allocate(arena, String8, 2);
                args[0] = path_join(arena, trees[0].build_directory, S8("Release/ide"));
                args[1] = S8("x86_64_completion_census");
                payload->arguments = (SliceString8){.pointer = args, .length = 2};
            }
        }
        u32 fixture_threads = (u32)environment_positive_u64_or(S8("BUSTER_MATRIX_THREADS"), os_get_logical_thread_count());
        matrix_phase.outer_jobs = matrix_superbuild_outer_jobs(fixture_threads, count);
        // The production quotas, including serialized checks test phases.
        matrix_superbuild_allocate_jobs(trees, count, fixture_threads, checks ? 0 : 1);
        MatrixSuperbuildSelfHostPlan self_host = {.enabled = !checks, .tree_index = 0, .pool_jobs = 1, .build_directory = trees[0].build_directory};
        ok = matrix_superbuild_manifest_write(arena, path_join(arena, matrix_phase.root, S8("matrix.cmake")), S8("/fixture"),
                   matrix_phase.driver, trees, count, matrix_phase.outer_jobs, combinations, self_host, false, false) && ok;
        ProcessRun* run = run_add(arena, step_add(arena));
        String8* args = arena_allocate(arena, String8, 3);
        args[0] = S8("fixture-cmake"); args[1] = S8("--build"); args[2] = S8("superbuild");
        run->arguments = (SliceString8){.pointer = args, .length = 3};
    }
    coverage.output_path = path_join(arena, matrix_phase.root, S8("coverage.json"));
    matrix_coverage_completion_add(arena, &coverage);
    ok = matrix_phase_plan(arena, direct) && ok;
    ProcessRun* callback = program.build_graph.last_step->last_process;
    ok = matrix_phase_ready(arena, program.build_graph.last_step) && ok;
    ok = matrix_phase_callback(arena, callback, false, PROCESS_RESULT_SUCCESS) && ok;
    ok = matrix_coverage_manifest_write(arena, &coverage, true) && ok;
    ok = matrix_phase_callback(arena, callback, true, PROCESS_RESULT_SUCCESS) && ok;
    program.build_graph = (BuildGraph){0};
    return ok ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
'''


class ResourceWitnessTests(unittest.TestCase):
    def test_supported_platforms_and_unavailable_statuses(self):
        for platform in ("windows", "linux", "macos"):
            for status in ("observed", "unknown", "unsupported", "error"):
                with self.subTest(platform=platform, status=status):
                    phases.validate_resources(resource_fixture(platform, status), platform)

    def test_malformed_schema_and_partial_metrics_fail_closed(self):
        witness = resource_fixture()
        for value in (None, [], {}, dict(witness, schema="other"), dict(witness, extra=0),
                      dict(witness, cpu=None), dict(witness, peak_memory=[])):
            with self.subTest(value=value), self.assertRaises(ValueError):
                phases.validate_resources(value, "windows")
        for metric in ("cpu", "peak_memory"):
            for key in witness[metric]:
                malformed = copy.deepcopy(witness)
                del malformed[metric][key]
                with self.subTest(metric=metric, key=key), self.assertRaises(ValueError):
                    phases.validate_resources(malformed, "windows")

    def test_source_scope_unit_kind_and_status_fail_closed(self):
        for metric, key, value in (("cpu", "source", "wait4"), ("cpu", "scope", "tree"),
                                   ("cpu", "unit", "seconds"), ("cpu", "status", "estimated"),
                                   ("peak_memory", "source", "job-accounting"), ("peak_memory", "scope", "tree"),
                                   ("peak_memory", "unit", "KiB"), ("peak_memory", "kind", "peak-commit")):
            malformed = resource_fixture()
            malformed[metric][key] = value
            with self.subTest(metric=metric, key=key), self.assertRaises(ValueError):
                phases.validate_resources(malformed, "windows")
        with self.assertRaises(ValueError):
            phases.validate_resources(resource_fixture("linux"), "windows")

    def test_numeric_types_ranges_and_missing_measurements_fail_closed(self):
        for metric, field in (("cpu", "user"), ("cpu", "system"), ("peak_memory", "value")):
            for value in (True, -1, 1.5, "1", "unknown", None, 2 ** 64):
                malformed = resource_fixture()
                malformed[metric][field] = value
                with self.subTest(metric=metric, field=field, value=value), self.assertRaises(ValueError):
                    phases.validate_resources(malformed, "windows")
            for status in ("unknown", "error", "unsupported"):
                malformed = resource_fixture(status=status)
                malformed[metric][field] = 0
                with self.subTest(metric=metric, status=status), self.assertRaises(ValueError):
                    phases.validate_resources(malformed, "windows")
        for status, error in (("observed", 1), ("unknown", 1), ("unsupported", 1), ("error", 0),
                              ("error", True), ("error", -1), ("error", 2 ** 32)):
            malformed = resource_fixture(status=status)
            malformed["cpu"]["error"] = error
            with self.subTest(status=status, error=error), self.assertRaises(ValueError):
                phases.validate_resources(malformed, "windows")


class NativeObserverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(ignore_cleanup_errors=os.name == "nt")
        cls.root = Path(cls.temp.name)
        supplied = None
        cls.driver = Path(supplied).resolve() if supplied else cls.root / ("driver.exe" if os.name == "nt" else "driver")
        if not supplied:
            compiler = shutil.which("clang") or shutil.which("gcc")
            if not compiler:
                raise RuntimeError("native observer controls require a C compiler")
            source = cls.root / "phase-fixture.c"
            native = (ROOT / "build.c").read_text(encoding="utf-8")
            native = native.replace("ProcessResult process_arguments(void)", PLAN_FIXTURE + "\nProcessResult process_arguments(void)")
            native = native.replace("result = matrix_coverage_manifest_self_test(arena);", "result = matrix_phase_fixture(arena);")
            source.write_text(native, encoding="utf-8")
            command = [compiler, "-Isrc", "-I.", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", str(source), "-o", str(cls.driver)]
            if os.name == "nt":
                command.append("-lws2_32")
            subprocess.run(command, cwd=ROOT, check=True, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def worker(self, code, timeout=0, evidence=False):
        root = Path(tempfile.mkdtemp(dir=self.root))
        write(root, "plan.json", {})
        command = [str(self.driver), "matrix_phase_run", str(root), "fixture", "1", str(timeout), "--", sys.executable, "-c", code]
        env = dict(os.environ, BUSTER_CI_CHECKS_EVIDENCE="1" if evidence else "0")
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=10)
        return result, phases.read(next(root.glob("*.end.json")))

    def test_real_plan_serializer_direct_pooled_release_checks(self):
        for direct, linux_fixture, budget in ((False, False, 3), (True, False, 4), (False, True, 4)):
            for checks in (False, True):
                root = Path(tempfile.mkdtemp(dir=self.root))
                env = dict(os.environ, BUSTER_MATRIX_PHASE_OUTPUT=str(root), BUSTER_PHASE_FIXTURE_DIRECT=str(int(direct)),
                           BUSTER_PHASE_FIXTURE_CHECKS=str(int(checks)), BUSTER_PHASE_FIXTURE_LINUX=str(int(linux_fixture)),
                           BUSTER_MATRIX_THREADS=str(budget), GITHUB_SHA="a" * 40)
                subprocess.run([str(self.driver), "coverage_manifest_self_test"], cwd=ROOT, env=env, check=True, capture_output=True, timeout=30)
                plan = phases.read(root / "plan.json")
                coverage = phases.read(root / "coverage.json")
                self.assertEqual(plan["identity"]["source_tree"], subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip())
                trees, tasks = phases.validate_plan(plan, coverage, env)
                self.assertTrue(trees and tasks)
                callback = phases.read(next(root.glob("matrix-evidence-coverage.*.end.json")))
                self.assertEqual(callback["authority"], "driver_callback")
                self.assertEqual(callback["result"], 0)
                self.assertLessEqual(callback["start_us"], callback["end_us"])
                if not direct:
                    self.assertIn("matrix_phase_run", (root / "matrix.cmake").read_text())

    def test_real_separate_checks_shard_serializers(self):
        for linux_fixture in (False, True):
            for shard, count in (("sanitized-debug", 1), ("sanitized-release", 1), ("portability", 2 if linux_fixture else 3)):
                with self.subTest(linux=linux_fixture, shard=shard):
                    root = Path(tempfile.mkdtemp(dir=self.root))
                    env = dict(os.environ, BUSTER_MATRIX_PHASE_OUTPUT=str(root), BUSTER_PHASE_FIXTURE_DIRECT="0",
                               BUSTER_PHASE_FIXTURE_SHARD=shard, BUSTER_PHASE_FIXTURE_LINUX=str(int(linux_fixture)),
                               BUSTER_MATRIX_THREADS="4", BUSTER_MATRIX_TEST_ADMISSION="overlap", GITHUB_SHA="a" * 40)
                    subprocess.run([str(self.driver), "coverage_manifest_self_test"], cwd=ROOT, env=env, check=True, capture_output=True, timeout=30)
                    plan = phases.read(root / "plan.json")
                    coverage = phases.read(root / "coverage.json")
                    trees, tasks = phases.validate_plan(plan, coverage, env)
                    self.assertEqual(plan["identity"]["shard"], shard)
                    self.assertEqual(len(trees), count)
                    validations = [task for task in tasks.values() if task["phase"] == "validation"]
                    self.assertEqual(len(validations), 0 if shard == "portability" else 1)
                    if validations:
                        self.assertEqual(validations[0]["inner_jobs"], 4)

    def test_real_admission_manifests_and_ninja_dependencies(self):
        for admission in ("overlap", "all-builds"):
            with self.subTest(admission=admission):
                root = Path(tempfile.mkdtemp(dir=self.root))
                env = dict(os.environ, BUSTER_MATRIX_PHASE_OUTPUT=str(root), BUSTER_PHASE_FIXTURE_DIRECT="0",
                           BUSTER_PHASE_FIXTURE_CHECKS="1", BUSTER_PHASE_FIXTURE_LINUX="0",
                           BUSTER_MATRIX_THREADS="4", BUSTER_MATRIX_TEST_ADMISSION=admission, GITHUB_SHA="a" * 40)
                subprocess.run([str(self.driver), "coverage_manifest_self_test"], cwd=ROOT, env=env, check=True, capture_output=True, timeout=30)
                plan = phases.read(root / "plan.json")
                coverage = phases.read(root / "coverage.json")
                trees, tasks = phases.validate_plan(plan, coverage, env)
                self.assertEqual(plan["test_admission"], admission)
                manifest = root / "matrix.cmake"
                self.assertIn("BUSTER_SUPERBUILD_TEST_ADMISSION", manifest.read_text())
                graph = root / "graph"
                subprocess.run(["cmake", "-S", str(ROOT / "cmake/superbuild"), "-B", str(graph), "-G", "Ninja",
                                f"-DBUSTER_SUPERBUILD_MATRIX_FILE={manifest}"], cwd=ROOT, check=True, capture_output=True, timeout=30)
                tests = [task for task in tasks.values() if task["phase"] == "validation"]
                self.assertEqual(len(tests), 2)
                previous = None
                for task in tests:
                    index = task["tree"].removeprefix("tree")
                    target = "buster_test_" + index
                    query = subprocess.check_output(["ninja", "-C", str(graph), "-t", "query", target], cwd=ROOT, text=True, timeout=30)
                    names = {line.strip() for line in query.splitlines()}
                    self.assertEqual("buster_compile" in names, admission == "all-builds")
                    if previous:
                        self.assertIn(previous, names)
                    previous = target

    def test_real_success_failure_and_exit_word(self):
        success, record = self.worker("pass")
        self.assertEqual(success.returncode, 0)
        self.assertEqual(record["state"], "success")
        failed, record = self.worker("raise SystemExit(7)")
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(record["state"], "failure")
        self.assertEqual(record["platform_status"], 7 if os.name == "nt" else 7 << 8)

    def test_resource_witness_is_opt_in_and_measures_actual_child(self):
        code = "import time\ndata=bytearray(32*1024*1024)\nfor page in range(0,len(data),4096): data[page]=1\ndeadline=time.process_time()+0.08\nwhile time.process_time()<deadline: pass"
        ordinary, ordinary_record = self.worker(code)
        self.assertEqual(ordinary.returncode, 0)
        self.assertNotIn("resources", ordinary_record)
        observed, record = self.worker(code, evidence=True)
        self.assertEqual(observed.returncode, 0)
        platform = "windows" if os.name == "nt" else "macos" if sys.platform == "darwin" else "linux"
        phases.validate_resources(record["resources"], platform)
        cpu, memory = record["resources"]["cpu"], record["resources"]["peak_memory"]
        self.assertEqual(cpu["status"], "observed")
        self.assertGreaterEqual(cpu["user"] + cpu["system"], 50000)
        self.assertEqual(memory["status"], "observed")
        self.assertGreaterEqual(memory["value"], 32 * 1024 * 1024)
        self.assertEqual(record["cpu_time"], "unknown")
        self.assertEqual(record["peak_rss"], "unknown")
        print("MATRIX_PHASE_RESOURCE_CHILD " + json.dumps(record["resources"], sort_keys=True))

    @unittest.skipIf(os.name == "nt", "wait4 accounting control is POSIX-only")
    def test_waited_descendant_accounting_retains_its_scope(self):
        child = "import time\ndata=bytearray(64*1024*1024)\nfor page in range(0,len(data),4096): data[page]=1\ndeadline=time.process_time()+0.1\nwhile time.process_time()<deadline: pass"
        code = f"import subprocess,sys; subprocess.run([sys.executable,'-c',{child!r}],check=True)"
        result, record = self.worker(code, evidence=True)
        self.assertEqual(result.returncode, 0)
        resources = record["resources"]
        phases.validate_resources(resources, "macos" if sys.platform == "darwin" else "linux")
        self.assertEqual(resources["cpu"]["scope"], "process-and-waited-descendants")
        self.assertGreaterEqual(resources["cpu"]["user"] + resources["cpu"]["system"], 80000)
        self.assertEqual(resources["peak_memory"]["kind"], "largest-individual-high-water")
        self.assertGreaterEqual(resources["peak_memory"]["value"], 64 * 1024 * 1024)
        print("MATRIX_PHASE_RESOURCE_DESCENDANT " + json.dumps(resources, sort_keys=True))

    def test_resource_query_preserves_nonzero_exit_and_timeout(self):
        failed, record = self.worker("raise SystemExit(7)", evidence=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(record["state"], "failure")
        self.assertEqual(record["platform_status"], 7 if os.name == "nt" else 7 << 8)
        platform = "windows" if os.name == "nt" else "macos" if sys.platform == "darwin" else "linux"
        phases.validate_resources(record["resources"], platform)
        timed_out, record = self.worker("import time; time.sleep(5)", timeout=1, evidence=True)
        self.assertNotEqual(timed_out.returncode, 0)
        self.assertEqual(record["state"], "timeout")
        self.assertEqual(record["timed_out"], 1)
        phases.validate_resources(record["resources"], platform)

    def test_real_deadline(self):
        failed, record = self.worker("import time; time.sleep(5)", timeout=1)
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(record["state"], "timeout")
        self.assertEqual(record["timed_out"], 1)

    def test_observer_overhead_measurement(self):
        samples = []
        for _ in range(5):
            start = time.perf_counter_ns()
            result, record = self.worker("pass")
            elapsed = (time.perf_counter_ns() - start) // 1000
            self.assertEqual(result.returncode, 0)
            samples.append(dict(enclosing_us=elapsed, child_us=record["end_us"] - record["child_start_us"],
                                observer_upper_bound_us=elapsed - (record["end_us"] - record["child_start_us"])))
        print("MATRIX_PHASE_OBSERVER_OVERHEAD " + json.dumps(samples, sort_keys=True))
        # No timing threshold on a shared runner; actual samples are retained.


if __name__ == "__main__":
    unittest.main()
