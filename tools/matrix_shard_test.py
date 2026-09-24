#!/usr/bin/env python3
"""Native partition/consumer controls and the exact Actions completion gate.

The C fixture embeds the actual build driver: it exports all six full policy
plans and all three selections, without running or claiming any matrix work.
Synthetic completions below mock compiler re-probes only inside these tests;
tools/coverage_manifest_test.py retains the real executable-binding controls.
"""
import copy
from collections import Counter
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_summary
import github_ci_time

C_FIXTURE = r'''
BUSTER_GLOBAL_LOCAL ProcessResult matrix_fixture_export(Arena* arena)
{
    String8 output_directory = os_get_environment_variable(S8("BUSTER_MATRIX_FIXTURE_DIRECTORY"));
    bool valid = output_directory.length && matrix_coverage_policy_self_test(arena);
    MatrixCoverageTarget targets[] = {
        {.platform = S8("linux"), .architecture = S8("x86_64")},
        {.platform = S8("linux"), .architecture = S8("aarch64"), .aarch64 = 1},
        {.platform = S8("macos"), .architecture = S8("x86_64"), .apple = 1},
        {.platform = S8("macos"), .architecture = S8("aarch64"), .apple = 1, .aarch64 = 1},
        {.platform = S8("windows"), .architecture = S8("x86_64"), .windows = 1},
        {.platform = S8("windows"), .architecture = S8("aarch64"), .windows = 1, .aarch64 = 1},
    };
    String8 shards[] = {S8("combinations"), S8("release"), S8("checks")};
    for (u32 target_i = 0; valid && target_i < BUSTER_ARRAY_LENGTH(targets); target_i += 1)
    {
        MatrixCoverageTarget target = targets[target_i];
        for (u32 shard_i = 0; shard_i < BUSTER_ARRAY_LENGTH(shards); shard_i += 1)
        {
            MatrixCoverageManifest manifest = {0};
            manifest.lane = (MatrixCoverageLane){.suite = S8("desktop"), .shard = shards[shard_i],
                .platform = target.platform, .architecture = target.architecture};
            valid = matrix_coverage_plan_build_for_target(arena, &manifest.plan, manifest.lane, target) && valid;
            valid = matrix_coverage_partition_validate(&manifest.plan) && valid;
            bool direct = target.apple && !target.aarch64;
            bool fanout = !direct && (target.apple || !target.aarch64);
            manifest.obligations = matrix_coverage_obligations_for_lane(direct, fanout, &manifest.plan, manifest.lane.shard);
            // Deliberately invalid as production evidence. These files
            // contain policy selections, not compiler capabilities/results.
            manifest.mode = S8("partition-policy-fixture");
            manifest.output_path = string_format(arena, S8("{S8}/{S8}-{S8}-{S8}.json"),
                output_directory, target.platform, target.architecture, manifest.lane.shard);
            manifest.output_path = string_duplicate_arena(arena, manifest.output_path, true);
            valid = matrix_coverage_manifest_write(arena, &manifest, true) && valid;
        }
    }
    return valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
'''


class NativePartitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(ignore_cleanup_errors=os.name == "nt")
        cls.root = Path(cls.temporary.name).resolve()
        cls.driver = cls.root / ("matrix-fixture.exe" if os.name == "nt" else "matrix-fixture")
        source = cls.root / "matrix-fixture.c"
        # Instrument only the self-test dispatch in a temporary driver copy.
        # The real entry point initializes platform/TLS/file services; every
        # policy, selection and serialization function remains production code.
        native = (ROOT / "build.c").read_text(encoding="utf-8")
        anchor = "ProcessResult process_arguments(void)"
        dispatch = "result = matrix_coverage_manifest_self_test(arena);"
        assert native.count(anchor) == 1 and native.count(dispatch) == 1
        native = native.replace(anchor, C_FIXTURE + "\n" + anchor)
        native = native.replace(dispatch, "result = matrix_fixture_export(arena);")
        source.write_text(native, encoding="utf-8")
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            raise RuntimeError("A C compiler is mandatory for native partition regression tests")
        # Match coverage_manifest_test.py's portable producer fixture flags;
        # the real hosted driver separately builds with Clang -Wall -Werror.
        command = [compiler, "-Isrc", "-I.", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", str(source)]
        if os.name == "nt":
            command.append("-lws2_32")
        command += ["-o", str(cls.driver)]
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)
        subprocess.run([str(cls.driver), "coverage_manifest_self_test"], cwd=ROOT, check=True, timeout=60,
                       env=dict(os.environ, BUSTER_MATRIX_FIXTURE_DIRECTORY=str(cls.root)))
        cls.plans = {}
        for path in cls.root.glob("*.json"):
            manifest = json.loads(path.read_text(encoding="utf-8"))
            identity = manifest["identity"]
            cls.plans[identity["platform"], identity["architecture"], identity["shard"]] = manifest
        cls.compilers = {}
        for family in ("cl", "clang", "gcc", "zig"):
            path = cls.root / ("probe-" + family)
            path.write_bytes(("compiler fixture " + family).encode("ascii"))
            cls.compilers[family] = path

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def completed_fixture(self, original):
        manifest = copy.deepcopy(original)
        manifest["mode"] = "ci"
        identity = manifest["identity"]
        identity.update(source_revision="a" * 40, repository="buster14a/buster", ref="refs/heads/fixture",
                        run_id="123", run_attempt="1", source_path=str(ROOT / "build.c"), driver_path=str(self.driver))
        for kind in ("source", "driver"):
            identity[kind + "_hash"] = hashlib.sha256(Path(identity[kind + "_path"]).read_bytes()).hexdigest()
        identity["lane_id"] = "/".join(("desktop", identity["shard"], identity["platform"], identity["architecture"],
                                          "source=" + identity["source_revision"], "run=123", "attempt=1"))
        manifest["executed"][0]["lane_id"] = identity["lane_id"]
        probes = {}
        for expected, detected in zip(manifest["expected"], manifest["detected"]):
            if expected["state"] != "required":
                continue
            family = expected["compiler"]
            architecture, platform = identity["architecture"], identity["platform"]
            target = architecture + {"linux": "-unknown-linux-gnu", "macos": "-apple-darwin", "windows": "-w64-mingw32"}[platform]
            if family == "cl":
                target = "x64" if architecture == "x86_64" else "arm64"
            probe = {"identity": "BUSTER_BUILD_COMPILER_" + {"cl": "MSVC", "clang": "CLANG", "gcc": "GNU", "zig": "ZIG"}[family],
                     "target": target, "version": {"cl": "Microsoft (R) C/C++ Optimizing Compiler Version 19.42",
                                                  "clang": "clang version 22.1.0", "gcc": "gcc (GCC) 15.2.0", "zig": "0.16.0"}[family]}
            probes[family] = probe
            path = self.compilers[family]
            detected.update(probe, path=str(path), path_hash=hashlib.sha256(path.read_bytes()).hexdigest(),
                            state="available", reason="")
        manifest["capability_probe_count"] = len(probes)
        environment = {"BUSTER_MATRIX_SHARD": identity["shard"], "BUSTER_CI_COVERAGE_PLATFORM": identity["platform"],
                       "BUSTER_CI_COVERAGE_ARCH": identity["architecture"], "GITHUB_SHA": identity["source_revision"],
                       "GITHUB_REPOSITORY": identity["repository"], "GITHUB_REF": identity["ref"],
                       "GITHUB_RUN_ID": identity["run_id"], "GITHUB_RUN_ATTEMPT": identity["run_attempt"]}
        return manifest, environment, probes

    def validate(self, manifest, environment, probes):
        paths = {"source": ROOT / "build.c", "driver": self.driver}
        with mock.patch.object(ci_summary, "_coverage_expected_path", side_effect=lambda env, identity, kind, mode: paths[kind]), \
             mock.patch.object(ci_summary, "_coverage_expected_compiler_path", side_effect=lambda env, platform, compiler: self.compilers[compiler]), \
             mock.patch.object(ci_summary, "_coverage_probe_compiler", side_effect=lambda path, compiler, env: probes[compiler]):
            return ci_summary.validate_coverage_manifest(manifest, environment)

    def test_native_full_policy_and_all_shards_keep_original_anchors(self):
        self.assertEqual(len(self.plans), 18)
        for (platform, architecture), anchor in ci_summary._COVERAGE_POLICY_ANCHORS.items():
            unsharded = self.plans[platform, architecture, "combinations"]
            policy = unsharded["policy"]
            self.assertEqual((policy["row_count"], policy["required_count"], policy["excluded_count"], policy["fingerprint"]), anchor)
            full = set(unsharded["executed"][0]["rows"])
            selections = []
            for shard in github_ci_time.COMBINATION_SHARDS:
                plan = self.plans[platform, architecture, shard]
                self.assertEqual(plan["expected"], unsharded["expected"])
                self.assertEqual(plan["policy"], policy)
                selected = set(plan["executed"][0]["rows"])
                self.assertTrue(selected)
                self.assertEqual(selected, {row["id"] for row in plan["expected"]
                                           if row["state"] == "required" and row["owner_shard"] == shard})
                selections.append(selected)
                if shard == "release":
                    self.assertEqual(len(selected), 1)
                else:
                    self.assertTrue(all(value == {"state": "not-applicable", "reason": "owned-by-release-shard"}
                                        for value in plan["obligations"].values()))
            self.assertFalse(selections[0] & selections[1])
            self.assertEqual(selections[0] | selections[1], full)

    def test_windows_aarch64_policy_is_explicit_and_nonempty(self):
        expected_anchors = {
            "x86_64": (28, 6, 22, "46ffb69c2ceae9c0"),
            "aarch64": (19, 2, 17, "709a010f922e385b"),
        }
        for architecture, anchor in expected_anchors.items():
            manifest = self.plans["windows", architecture, "combinations"]
            policy = manifest["policy"]
            self.assertEqual((policy["row_count"], policy["required_count"],
                              policy["excluded_count"], policy["fingerprint"]), anchor)
            rows = manifest["expected"]
            self.assertEqual(len({row["id"] for row in rows}), len(rows))
            required = [row for row in rows if row["state"] == "required"]
            excluded = [row for row in rows if row["state"] == "excluded"]
            self.assertEqual(len(required), anchor[1])
            self.assertEqual(len(excluded), anchor[2])
            self.assertTrue(all(row["owner_shard"] in github_ci_time.COMBINATION_SHARDS for row in required))
            self.assertTrue(all(row["exclusion"] for row in excluded))
        arm = self.plans["windows", "aarch64", "combinations"]
        self.assertEqual(Counter(row["compiler"] for row in arm["expected"] if row["state"] == "required"),
                         Counter(("clang", "cl")))

    def test_consumer_accepts_each_complete_native_selection(self):
        for key, plan in self.plans.items():
            with self.subTest(lane=key):
                manifest, environment, probes = self.completed_fixture(plan)
                self.assertEqual(self.validate(manifest, environment, probes), [])
                # Export-only test data itself can never satisfy production CI.
                self.assertTrue(ci_summary.validate_coverage_manifest(plan, environment))

    def test_consumer_rejects_missing_duplicate_foreign_and_shrunk_rows(self):
        for key, plan in self.plans.items():
            if key[2] == "combinations":
                continue
            manifest, environment, probes = self.completed_fixture(plan)
            self.assertEqual(self.validate(manifest, environment, probes), [])
            damaged = []
            bad = copy.deepcopy(manifest)
            bad["executed"][0]["rows"].pop()
            damaged.append(bad)
            bad = copy.deepcopy(manifest)
            bad["executed"][0]["rows"].append(bad["executed"][0]["rows"][0])
            damaged.append(bad)
            foreign = next(row["id"] for row in manifest["expected"] if row["state"] == "required" and row["owner_shard"] != key[2])
            bad = copy.deepcopy(manifest)
            bad["executed"][0]["rows"].append(foreign)
            damaged.append(bad)
            bad = copy.deepcopy(manifest)
            bad["partition_version"] = 2
            damaged.append(bad)
            bad = copy.deepcopy(manifest)
            bad["expected"][0]["owner_shard"] = "checks" if bad["expected"][0]["owner_shard"] == "release" else "release"
            damaged.append(bad)
            bad = copy.deepcopy(manifest)
            bad["expected"] = [row for row in bad["expected"] if row["id"] != foreign]
            bad["detected"] = [row for row in bad["detected"] if row["id"] != foreign]
            bad["policy"]["required_count"] -= 1
            bad["policy"]["row_count"] -= 1
            bad["policy"]["fingerprint"] = ci_summary._coverage_policy_fingerprint(bad["identity"], bad["expected"])
            damaged.append(bad)
            for number, bad in enumerate(damaged):
                with self.subTest(lane=key, mutation=number):
                    self.assertTrue(self.validate(bad, environment, probes))
            for expected_shard in ("all", "release" if key[2] == "checks" else "checks", "unknown"):
                self.assertTrue(self.validate(manifest, dict(environment, BUSTER_MATRIX_SHARD=expected_shard), probes))
            unsharded, _, _ = self.completed_fixture(self.plans[key[0], key[1], "combinations"])
            self.assertTrue(self.validate(unsharded, environment, probes))

    def test_invalid_selector_fails_before_discovery_or_output_mutation(self):
        sentinel = self.root / "preserve-me"
        sentinel.write_text("preserve", encoding="utf-8")
        output = self.root / "must-not-exist.json"
        for command, value in itertools.product(("test_all_combinations", "test_all_combinations_ci"), ("0/2", "Release", "checks ", "../release")):
            environment = dict(os.environ, BUSTER_MATRIX_SHARD=value, BUSTER_CI_COVERAGE_OUTPUT=str(output),
                               BUSTER_BUILD_DIRECTORY_PREFIX=str(sentinel))
            completed = subprocess.run([str(self.driver), command], cwd=ROOT, env=environment, capture_output=True, text=True, timeout=20)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("BUSTER_MATRIX_SHARD must be", completed.stdout + completed.stderr)
            self.assertNotIn("BUSTER_COMPILER_EXECUTABLE:", completed.stdout)
            self.assertFalse(output.exists())
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")


class WorkflowSetupTests(unittest.TestCase):
    """Run the actual step bodies with controlled tools and empty log roots."""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.shell = shutil.which("bash")
        if os.name == "nt":
            # Match the existing CI tools harness: System32 bash is WSL,
            # not the Git Bash used by the workflow's shell: bash steps.
            git = shutil.which("git")
            self.assertIsNotNone(git, "Git for Windows is a CI prerequisite")
            self.shell = next((str(parent / "bin/bash.exe")
                               for parent in Path(git).resolve().parents
                               if (parent / "bin/bash.exe").is_file()), None)
        if not self.shell:
            self.fail("The desktop workflow requires bash")
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        self.steps = dict(re.findall(r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)", desktop))
        self.environment = dict(os.environ, BUSTER_CI_PYTHON=Path(sys.executable).as_posix(),
                                RUNNER_TEMP=self.root.as_posix(), BUSTER_MATRIX_SHARD="checks",
                                ZIG_TARGET="fixture", FIXTURE_EXIT="0")

    def run_step(self, name):
        body = self.steps[name].split("        run: |\n", 1)[1]
        script = "\n".join(line[10:] for line in body.splitlines() if line.startswith("          "))
        return subprocess.run([self.shell, "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", script],
                              cwd=self.root, env=self.environment, text=True, capture_output=True, timeout=30)

    def test_workflow_tools_keep_platform_budget_and_all_suites(self):
        block = self.steps["Workflow tool regression tests"]
        self.assertIn("timeout-minutes: ${{ (matrix.os == 'windows' || matrix.os == 'macos') && 5 || 2 }}", block)
        self.assertIn("matrix.shard == 'release'", block)
        self.assertIn("set -euo pipefail", block)
        self.assertNotIn("continue-on-error:", block)
        expected = {
            "tests/ci_tools_test.py", "tools/ci_admission_test.py", "tools/ci_zig_test.py",
            "tools/ci_zig_cache_test.py", "tools/ci_android_sdk_test.py",
            "tools/analyzer_selection_test.py", "tools/coverage_manifest_test.py",
            "tools/matrix_shard_test.py", "tools/differential_ci_policy_test.py",
            "tools/native_producer_profile_test.py", "tools/ci_configure_evidence_test.py",
            "tools/ci_matrix_phases_test.py", "tools/ci_matrix_phases_bridge_test.py",
            "tools/ci_native_observation_test.py",
        }
        suites = re.findall(r'^          run_suite ([^ ]+) [^ ]+\.log$', block, re.M)
        self.assertEqual(set(suites), expected)
        self.assertEqual(len(suites), len(expected))
        self.assertIn('if "$BUSTER_CI_PYTHON" "$suite" -v 2>&1 | tee', block)
        self.assertIn('return "$status"', block)
        self.assertIn('WORKFLOW_TOOLS_END result=success', block)

    def test_workflow_tools_record_all_suites_and_stop_on_failure(self):
        expected = re.findall(r'^          run_suite ([^ ]+) ([^ ]+\.log)$',
                              self.steps["Workflow tool regression tests"], re.M)
        for suite, _ in expected:
            path = self.root / suite
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("import os, sys\nprint('SUITE fixture')\n"
                            "sys.exit(int(os.environ['FIXTURE_EXIT']) if __file__.endswith('ci_admission_test.py') else 0)\n",
                            encoding="utf-8")
        for code in (0, 7):
            with self.subTest(exit_code=code):
                shutil.rmtree(self.root / "buster-ci", ignore_errors=True)
                self.environment["FIXTURE_EXIT"] = str(code)
                result = self.run_step("Workflow tool regression tests")
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                completed = expected if code == 0 else expected[:2]
                self.assertEqual(re.findall(r'SUITE_START path=([^ ]+)', result.stdout),
                                 [suite for suite, _ in completed])
                for suite, log in completed:
                    self.assertIn(f"SUITE_END path={suite} result=", result.stdout)
                    self.assertIn("SUITE fixture", (self.root / "buster-ci" / log).read_text())
                self.assertEqual("WORKFLOW_TOOLS_END result=success" in result.stdout, code == 0)

    def test_checks_zig_setup_creates_its_own_log_directory_and_propagates_failure(self):
        tools = self.root / "tools"
        tools.mkdir()
        (tools / "ci_zig.py").write_text("import os, sys\nprint('ZIG_SETUP fixture')\nsys.exit(int(os.environ['FIXTURE_EXIT']))\n")
        for code in (0, 7):
            with self.subTest(exit_code=code):
                shutil.rmtree(self.root / "buster-ci", ignore_errors=True)
                self.environment["FIXTURE_EXIT"] = str(code)
                result = self.run_step("Install verified Zig")
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                self.assertIn("ZIG_SETUP fixture", (self.root / "buster-ci/zig.log").read_text())

    def test_wrapper_suite_executes_only_in_release_and_keeps_failure_status(self):
        tests = self.root / "tests"
        tests.mkdir()
        (tests / "bootstrap_wrapper_test.py").write_text(
            "import os, pathlib, sys\npathlib.Path('wrapper-called').touch()\n"
            "print('BOOTSTRAP_TEST fixture')\nsys.exit(int(os.environ['FIXTURE_EXIT']))\n")
        for shard, code in (("checks", 7), ("release", 0), ("release", 7)):
            with self.subTest(shard=shard, exit_code=code):
                marker = self.root / "wrapper-called"
                if marker.exists():
                    marker.unlink()
                shutil.rmtree(self.root / "buster-ci", ignore_errors=True)
                self.environment.update(BUSTER_MATRIX_SHARD=shard, FIXTURE_EXIT=str(code))
                result = self.run_step("Bootstrap wrapper regression tests")
                self.assertEqual(result.returncode, 0 if shard == "checks" else code, result.stdout + result.stderr)
                self.assertEqual(marker.exists(), shard == "release")
                if shard == "checks":
                    self.assertIn("owned-by-release-shard", result.stdout)
                    self.assertFalse((self.root / "buster-ci/bootstrap-wrapper.log").exists())
                else:
                    self.assertIn("BOOTSTRAP_TEST fixture", (self.root / "buster-ci/bootstrap-wrapper.log").read_text())


class CompletionGateTests(unittest.TestCase):
    def sample(self):
        jobs = []
        for number, name in enumerate(github_ci_time.COMBINATION_JOBS):
            steps = []
            if name in github_ci_time.COMBINATION_PLATFORMS:
                steps = ["Install verified Zig", "Desktop result and reproduction", "Retain desktop logs",
                         "Combination matrix (Windows)" if name.startswith("Windows") else "Combination matrix (Linux, macOS)"]
                if name.endswith(" release"):
                    steps += ["Workflow tool regression tests", "Bootstrap wrapper regression tests"]
            elif name in github_ci_time.NATIVE:
                steps = ["Native result and reproduction",
                         "Execution-mode matrix (Windows)" if name.startswith("Windows") else "Execution-mode matrix"]
                if not name.startswith("Windows"):
                    steps.append("Native configuration differential matrix")
            jobs.append({"id": number + 1, "name": name, "run_id": 123, "run_attempt": 1, "head_sha": "a" * 40,
                         "status": "in_progress" if name == "CI complete" else "completed",
                         "conclusion": None if name == "CI complete" else "success",
                         "steps": [{"name": step, "conclusion": "success"} for step in steps]})
        return jobs

    def check(self, jobs, attempt=1):
        return github_ci_time.validate_required_jobs(jobs, 123, attempt, "a" * 40)

    def test_all_twenty_five_jobs_and_exact_twelve_desktop_shards(self):
        self.assertEqual(len(github_ci_time.COMBINATION_JOBS), 25)
        self.assertEqual(len(github_ci_time.COMBINATION_PLATFORMS), 12)
        self.assertEqual(self.check(self.sample()), [])

    def test_missing_duplicate_failed_cancelled_and_skipped_jobs_fail(self):
        total = len(github_ci_time.COMBINATION_JOBS)
        for index in range(total):
            jobs = self.sample()
            jobs.pop(index)
            self.assertTrue(self.check(jobs))
            jobs = self.sample()
            jobs.append(copy.deepcopy(jobs[index]))
            self.assertTrue(self.check(jobs))
            if jobs[index]["name"] != "CI complete":
                for conclusion in ("failure", "cancelled", "skipped", None):
                    jobs = self.sample()
                    jobs[index]["conclusion"] = conclusion
                    self.assertTrue(self.check(jobs))
        for index, job in enumerate(self.sample()):
            if job["name"] not in github_ci_time.COMBINATION_PLATFORMS + github_ci_time.NATIVE:
                continue
            for step in range(len(job["steps"])):
                for conclusion in ("failure", "cancelled", "skipped", None):
                    jobs = self.sample()
                    jobs[index]["steps"][step]["conclusion"] = conclusion
                    self.assertTrue(self.check(jobs))
                jobs = self.sample()
                jobs[index]["steps"].pop(step)
                self.assertTrue(self.check(jobs))

    def test_cross_run_source_or_future_attempt_is_not_accepted(self):
        for field, value in (("run_id", 124), ("head_sha", "b" * 40), ("run_attempt", 0), ("run_attempt", 2), ("run_attempt", True)):
            jobs = self.sample()
            jobs[0][field] = value
            self.assertTrue(self.check(jobs))
        jobs = self.sample()
        jobs[-1]["run_attempt"] = 2
        self.assertEqual(self.check(jobs, attempt=2), [])
        jobs[0]["conclusion"] = "failure"
        self.assertTrue(self.check(jobs, attempt=2))

    def test_older_green_cannot_mask_newer_failure_on_partial_reruns(self):
        jobs = self.sample()
        current_gate = dict(jobs[-1], run_attempt=2)
        newer = copy.deepcopy(jobs[0])
        newer.update(run_attempt=2, conclusion="failure")
        history = jobs + [current_gate, newer]
        latest = github_ci_time.latest_run_jobs(history, 123, 2, "a" * 40)
        self.assertTrue(self.check(latest, attempt=2))
        newer["conclusion"] = "success"
        latest = github_ci_time.latest_run_jobs(history, 123, 2, "a" * 40)
        self.assertEqual(self.check(latest, attempt=2), [])
        with self.assertRaises(ValueError):
            github_ci_time.latest_run_jobs(history + [newer], 123, 2, "a" * 40)
        with self.assertRaises(ValueError):
            github_ci_time.latest_run_jobs(history + [dict(newer, run_attempt=3)], 123, 2, "a" * 40)

    def test_api_gate_paginates_latest_jobs_and_rejects_partial_inventory(self):
        jobs = self.sample()
        total = len(jobs)
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        responses = [run, {"total_count": total, "jobs": jobs[:10]}, {"total_count": total, "jobs": jobs[10:]}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses) as fetch:
            self.assertTrue(github_ci_time.require_jobs(args)["success"])
            self.assertIn("filter=all", fetch.call_args_list[1].args[1])
            self.assertIn("page=2", fetch.call_args_list[2].args[1])
        for last in ({"total_count": total, "jobs": []}, {"total_count": total - 1, "jobs": jobs[10:]}):
            with mock.patch.object(github_ci_time, "api_get", side_effect=responses[:2] + [last]):
                with self.assertRaises(ValueError):
                    github_ci_time.require_jobs(args)
        with mock.patch.object(github_ci_time, "api_get", side_effect=[dict(run, run_attempt=2)]):
            with self.assertRaises(ValueError):
                github_ci_time.require_jobs(args)

    def test_api_gate_waits_for_unfinished_job_but_rejects_missing_final_steps(self):
        completed = self.sample()
        pending = copy.deepcopy(completed)
        windows = next(job for job in pending if job["name"] == "Windows x86-64 checks")
        windows.update(status="in_progress", conclusion=None, steps=[])
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)

        def api_get(_repository, path, _token):
            if path == "actions/runs/123":
                return run
            api_get.probes += 1
            return {"total_count": len(completed), "jobs": pending if not api_get.settles or api_get.probes == 1 else completed}

        api_get.probes = 0
        api_get.settles = True
        with mock.patch.object(github_ci_time, "api_get", side_effect=api_get), \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            self.assertTrue(github_ci_time.require_jobs(args)["success"])
            sleep.assert_called_once_with(github_ci_time.REQUIRED_JOB_SETTLE_SECONDS)

        api_get.probes = 0
        api_get.settles = False
        with mock.patch.object(github_ci_time, "api_get", side_effect=api_get), \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            self.assertFalse(github_ci_time.require_jobs(args)["success"])
            self.assertEqual(api_get.probes, github_ci_time.REQUIRED_JOB_SETTLE_PROBES)
            self.assertEqual(sleep.call_count, github_ci_time.REQUIRED_JOB_SETTLE_PROBES - 1)

        # A finished job without its required steps is not an unfinished API
        # observation. No previous green attempt may supply those steps.
        windows.update(status="completed", conclusion="success")
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, {"total_count": len(pending), "jobs": pending}]), \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            self.assertFalse(github_ci_time.require_jobs(args)["success"])
            sleep.assert_not_called()

    def test_workflow_expands_exact_cross_product_and_keeps_gate_wiring(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        lanes = re.search(r"^        lane: \[([^]]+)\]$", desktop, re.M).group(1).split(", ")
        shards = re.search(r"^        shard: \[([^]]+)\]$", desktop, re.M).group(1).split(", ")
        includes = re.findall(r"^          - name: (.+)\n            lane: (.+)\n", desktop, re.M)
        self.assertEqual(Counter(lanes), Counter(f"{platform}-{arch}" for platform, arch in ci_summary._COVERAGE_POLICY_ANCHORS))
        self.assertEqual(Counter(shards), Counter(github_ci_time.COMBINATION_SHARDS))
        self.assertEqual(Counter(lane for _, lane in includes), Counter(lanes))
        expanded = [f"{name} {shard}" for name, _ in includes for shard in shards]
        self.assertEqual(Counter(expanded), Counter(github_ci_time.COMBINATION_PLATFORMS))
        self.assertIn("name: ${{ matrix.name }} ${{ matrix.shard }}", desktop)
        self.assertIn("BUSTER_MATRIX_SHARD: ${{ matrix.shard }}", desktop)
        self.assertIn("matrix.shard == 'release'", desktop)
        self.assertIn("name: desktop-${{ matrix.os }}-${{ matrix.arch }}-${{ matrix.shard }}-", desktop)
        native = workflow.split("\n  native:", 1)[1].split("\n  mobile:", 1)[0]
        native_names = re.findall(r"^          - name: (.+ native)$", native, re.M)
        self.assertEqual(Counter(native_names), Counter(github_ci_time.NATIVE))
        self.assertIn("Execution-mode matrix (Windows)", native)
        self.assertIn("test_mode_matrix --config Release", native)
        aggregate = workflow.split("\n  complete:", 1)[1]
        self.assertIn("actions: read", aggregate)
        self.assertIn("github_ci_time.py require-jobs", aggregate)
        self.assertIn("Verify every desktop partition exists", aggregate)
        self.assertIn("needs: [lint, test, native, mobile, uefi, analyzer]", aggregate)

    def test_timing_includes_every_new_shard_and_rejects_partial_runs(self):
        jobs = self.sample()
        for job in jobs:
            job.update(status="completed", conclusion="success", labels=["fixture"],
                       created_at="2026-09-16T12:00:05Z", started_at="2026-09-16T12:00:10Z", completed_at="2026-09-16T12:01:10Z")
            name = job["name"]
            required = {
                "CI complete": ("Require every shard", "Verify every desktop partition exists"),
                "Workflow lint": ("Validate every GitHub workflow",),
                "UEFI firmware boot": ("Build compiler and boot both architectures in all allocators",),
                "Clang analyzer shards": ("Exercise analyzer failure and coverage controls", "Compare reference analysis and aggregate all module shards"),
            }.get(name, ())
            if name in github_ci_time.NATIVE:
                required = (("Execution-mode matrix (Windows)",) if name.startswith("Windows") else
                            ("Execution-mode matrix", "Native configuration differential matrix"))
            elif name in github_ci_time.MOBILE:
                required = ("Test (Android)" if name.startswith("Android") else "Test (iOS simulator)",)
            existing = {step["name"] for step in job["steps"]}
            job["steps"] += [{"name": step, "conclusion": "success"} for step in required if step not in existing]
        run = {"id": 123, "head_sha": "a" * 40, "workflow_blob_sha": "b" * 40, "run_attempt": 1,
               "status": "completed", "conclusion": "success", "created_at": "2026-09-16T12:00:00Z", "jobs": jobs}
        measurement, reason = github_ci_time.measure(run)
        self.assertIsNone(reason)
        self.assertEqual(measurement["runner_seconds"], 25 * 60)
        self.assertEqual(measurement["elapsed_seconds"], 70)
        self.assertEqual(set(measurement["job_queue_seconds"].values()), {5})
        for index in range(len(jobs)):
            bad = copy.deepcopy(run)
            bad["jobs"].pop(index)
            self.assertIsNone(github_ci_time.measure(bad)[0])
        self.assertIsNone(github_ci_time.measure(dict(run, run_attempt=2))[0])


if __name__ == "__main__":
    unittest.main()
