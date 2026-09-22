#!/usr/bin/env python3
"""Deterministic phase/coverage joins plus real native observer failure controls."""
import copy
import json
import os
from pathlib import Path
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
        common = dict(id=name, epoch_us=1, pid=10 + len(plan["tasks"]), start_us=start, argv=["fixture", name])
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
    write(root, "plan.json", plan)
    write(root, "terminal.json", dict(epoch_us=1, terminal_us=400, result=0))
    return coverage


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
    MatrixCoverageTarget target = {.platform = direct ? S8("macos") : S8("windows"), .architecture = S8("x86_64"),
                                    .windows = !direct, .apple = direct};
    MatrixCoverageManifest coverage = {0};
    coverage.lane = matrix_coverage_lane_create(arena, checks ? S8("checks") : S8("release"));
    coverage.lane.platform = target.platform;
    coverage.lane.architecture = target.architecture;
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
                            .configuration_types = tree.configuration_types, .sanitize = tree.sanitize, .fuzz_available = tree.fuzz_available};
            String8 passthrough[] = {S8("-DBUSTER_FIXTURE=ON")};
            gen.cmake_arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(passthrough);
            gen = matrix_phase_tree(arena, gen, &coverage, tree);
            ok = ok && gen.cmake_arguments.length == 5 && string_equal(gen.cmake_arguments.pointer[0], passthrough[0]);
            String8 names[] = {S8("-DBUSTER_MATRIX_PHASE_DRIVER="), S8("-DBUSTER_MATRIX_PHASE_ROOT="),
                               S8("-DBUSTER_MATRIX_PHASE_TREE="), S8("-DBUSTER_MATRIX_PHASE_EPOCH=")};
            for (u32 a = 0; ok && a < BUSTER_ARRAY_LENGTH(names); a += 1)
            {
                ok = string_starts_with_sequence(gen.cmake_arguments.pointer[a + 1], names[a]);
            }
            ProcessRun* configure = run_add(arena, step_add(arena));
            String8* argv = arena_allocate(arena, String8, 1);
            argv[0] = S8("fixture-cmake");
            configure->arguments = (SliceString8){.pointer = argv, .length = 1};
            matrix_phase_wrap(arena, configure, matrix_phase_find_tree(gen.build_directory), S8("configure"), S8(""), 0);
            trees[count].build_directory = gen.build_directory;
            trees[count].parallel_jobs = 1;
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
        matrix_phase.outer_jobs = matrix_superbuild_outer_jobs(4, count);
        MatrixSuperbuildSelfHostPlan self_host = {.enabled = !checks, .tree_index = 0, .pool_jobs = 1, .build_directory = trees[0].build_directory};
        ok = matrix_superbuild_manifest_write(arena, path_join(arena, matrix_phase.root, S8("matrix.cmake")), S8("/fixture"),
                   matrix_phase.driver, trees, count, matrix_phase.outer_jobs, combinations, self_host, false, false) && ok;
        ProcessRun* run = run_add(arena, step_add(arena));
        String8* args = arena_allocate(arena, String8, 3);
        args[0] = S8("fixture-cmake"); args[1] = S8("--build"); args[2] = S8("superbuild");
        run->arguments = (SliceString8){.pointer = args, .length = 3};
    }
    ok = matrix_phase_plan(arena, direct) && ok;
    coverage.output_path = path_join(arena, matrix_phase.root, S8("coverage.json"));
    ok = matrix_coverage_manifest_write(arena, &coverage, true) && ok;
    program.build_graph = (BuildGraph){0};
    return ok ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
'''


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

    def worker(self, code, timeout=0):
        root = Path(tempfile.mkdtemp(dir=self.root))
        write(root, "plan.json", {})
        command = [str(self.driver), "matrix_phase_run", str(root), "fixture", "1", str(timeout), "--", sys.executable, "-c", code]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=10)
        return result, phases.read(next(root.glob("*.end.json")))

    def test_real_plan_serializer_direct_pooled_release_checks(self):
        for direct in (False, True):
            for checks in (False, True):
                root = Path(tempfile.mkdtemp(dir=self.root))
                env = dict(os.environ, BUSTER_MATRIX_PHASE_OUTPUT=str(root), BUSTER_PHASE_FIXTURE_DIRECT=str(int(direct)),
                           BUSTER_PHASE_FIXTURE_CHECKS=str(int(checks)), GITHUB_SHA="a" * 40)
                subprocess.run([str(self.driver), "coverage_manifest_self_test"], cwd=ROOT, env=env, check=True, capture_output=True, timeout=30)
                plan = phases.read(root / "plan.json")
                coverage = phases.read(root / "coverage.json")
                trees, tasks = phases.validate_plan(plan, coverage, env)
                self.assertTrue(trees and tasks)
                if not direct:
                    self.assertIn("matrix_phase_run", (root / "matrix.cmake").read_text())

    def test_real_success_failure_and_exit_word(self):
        success, record = self.worker("pass")
        self.assertEqual(success.returncode, 0)
        self.assertEqual(record["state"], "success")
        failed, record = self.worker("raise SystemExit(7)")
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(record["state"], "failure")
        self.assertEqual(record["platform_status"], 7 if os.name == "nt" else 7 << 8)

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
