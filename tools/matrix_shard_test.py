#!/usr/bin/env python3
"""Native partition/consumer controls and the exact Actions completion gate.

The C fixture embeds the actual build driver: it exports all six full policy
plans and all six selections, without running or claiming any matrix work.
Synthetic completions below mock compiler re-probes only inside these tests;
tools/coverage_manifest_test.py retains the real executable-binding controls.
"""
import contextlib
import copy
from collections import Counter
import hashlib
import io
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
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_summary
import github_ci_time

MATRIX_OWNERS = ("release", "sanitized-release", "portability")
MATRIX_SELECTIONS = ("combinations", "checks") + MATRIX_OWNERS


class AppleCIPolicyTests(unittest.TestCase):
    """#1986 retires Intel Apple scheduling, not compiler target support."""

    def test_active_workflows_have_no_intel_apple_runner_or_target_lane(self):
        paths = list((ROOT / ".github/workflows").glob("*.yml"))
        paths += list((ROOT / ".forgejo/workflows").glob("*.yml"))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(workflow=path.name):
                text = path.read_text(encoding="utf-8")
                # Reject old Intel labels, Rosetta launches and dedicated Apple
                # target entries, including in default manual/auxiliary jobs.
                self.assertNotRegex(text, r"\bmacos-(?:\d+-intel|1[0-3](?:-large)?|(?:14|15)-large|latest-large)\b")
                self.assertNotRegex(text, r"\barch\s+-(?:x86_64|i386)\b")
                self.assertNotRegex(text, r"(?mi)^\s*(?:os|platform): (?:macos|ios)\n\s*arch: x86_64$")
                self.assertNotRegex(text, r"(?mi)^\s*(?:target|slug|lane): (?:macos|ios)-x86_64$")
                self.assertNotRegex(text, r"(?mi)^\s*(?:target|zig_target): x86_64-(?:apple-|macos)")
        evidence = (ROOT / ".github/workflows/native-retirement-evidence.yml").read_text()
        entries = re.findall(r"^          - name: (.+ native)$", evidence, re.M)
        self.assertEqual(Counter(entries), Counter(github_ci_time.NATIVE))

    def test_main_inventory_has_no_mobile_application_jobs(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertNotIn("\n  mobile:", workflow)
        self.assertNotIn("needs.mobile", workflow)
        self.assertTrue(set(github_ci_time.MOBILE).isdisjoint(github_ci_time.combination_jobs()))

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
    String8 shards[] = {S8("combinations"), S8("checks"), S8("release"),
        S8("sanitized-release"), S8("portability")};
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
        self.assertEqual(len(self.plans), 6 * len(MATRIX_SELECTIONS))
        for (platform, architecture), anchor in ci_summary._COVERAGE_POLICY_ANCHORS.items():
            unsharded = self.plans[platform, architecture, "combinations"]
            policy = unsharded["policy"]
            self.assertEqual((policy["row_count"], policy["required_count"], policy["excluded_count"], policy["fingerprint"]), anchor)
            full = set(unsharded["executed"][0]["rows"])
            selections = {}
            for shard in MATRIX_SELECTIONS:
                plan = self.plans[platform, architecture, shard]
                self.assertEqual(plan["expected"], unsharded["expected"])
                self.assertEqual(plan["policy"], policy)
                selected = set(plan["executed"][0]["rows"])
                self.assertEqual(selected, {row["id"] for row in plan["expected"]
                                           if row["state"] == "required" and
                                           (shard == "combinations" or row["owner_shard"] == shard or
                                            (shard == "checks" and row["owner_shard"] != "release"))})
                selections[shard] = selected
                if shard == "release":
                    self.assertEqual(len(selected), 1)
                elif shard != "combinations":
                    self.assertTrue(all(value == {"state": "not-applicable", "reason": "owned-by-release-shard"}
                                        for value in plan["obligations"].values()))
            for left, right in itertools.combinations(MATRIX_OWNERS, 2):
                self.assertFalse(selections[left] & selections[right])
            self.assertEqual(set().union(*(selections[shard] for shard in MATRIX_OWNERS)), full)
            self.assertEqual(selections["checks"], set().union(*(selections[shard] for shard in MATRIX_OWNERS[1:])))
            self.assertFalse(selections["release"] & selections["checks"])
            self.assertEqual(selections["release"] | selections["checks"], full)
            self.assertTrue(selections["checks"])

    def test_windows_aarch64_policy_is_explicit_and_nonempty(self):
        expected_anchors = {
            "x86_64": (28, 6, 22, "46cdc6107ec8e0e9"),
            "aarch64": (19, 2, 17, "76a3bd5939220a42"),
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
            self.assertTrue(all(row["owner_shard"] in MATRIX_OWNERS for row in required))
            self.assertTrue(all(row["exclusion"] for row in excluded))
        arm = self.plans["windows", "aarch64", "combinations"]
        self.assertEqual(Counter(row["compiler"] for row in arm["expected"] if row["state"] == "required"),
                         Counter(("clang", "cl")))
        self.assertEqual(self.plans["windows", "aarch64", "sanitized-release"]["executed"][0]["rows"], [])
        self.assertEqual(self.plans["windows", "aarch64", "checks"]["executed"][0]["rows"],
                         self.plans["windows", "aarch64", "portability"]["executed"][0]["rows"])

    def test_sanitized_debug_is_build_only_and_sanitized_release_owns_runtime_and_fuzz(self):
        # #2657: sanitized Debug moves to compile-link portability coverage.
        # The checks-enabled sanitized Release tree owns sanitizer runtime and,
        # where libFuzzer exists, the fuzz-enabled sanitized binary.
        for (platform, architecture, shard), manifest in self.plans.items():
            if shard != "combinations":
                continue
            with self.subTest(lane=(platform, architecture)):
                required = [row for row in manifest["expected"] if row["state"] == "required"]
                fuzz_host = platform != "macos" and (platform, architecture) != ("windows", "aarch64")
                sanitize_host = (platform, architecture) != ("windows", "aarch64")
                runtime = [row for row in required if row["execution"] == "runtime"]
                self.assertTrue(all(row["compiler"] == "clang" and row["optimize"] for row in runtime))
                self.assertEqual(len(runtime), 2 if sanitize_host else 1)
                for row in required:
                    if not row["optimize"]:
                        self.assertEqual((row["execution"], row["fuzz"], row["owner_shard"]), ("compile-link", False, "portability"))
                debug = [row for row in required if row["sanitize"] and not row["optimize"]]
                release = [row for row in required if row["sanitize"] and row["optimize"]]
                self.assertEqual(len(debug), int(sanitize_host))
                self.assertEqual([(row["execution"], row["fuzz"], row["owner_shard"]) for row in release],
                                 [("runtime", fuzz_host, "sanitized-release")] if sanitize_host else [])
                self.assertNotIn("sanitized-debug", {row["owner_shard"] for row in manifest["expected"]})

    def test_consumer_rejects_sanitized_debug_runtime_or_fuzz_claims(self):
        manifest, environment, probes = self.completed_fixture(self.plans["linux", "x86_64", "portability"])
        self.assertEqual(self.validate(manifest, environment, probes), [])
        index = next(i for i, row in enumerate(manifest["expected"])
                     if row["state"] == "required" and row["sanitize"] and not row["optimize"])
        for field, value in (("execution", "runtime"), ("fuzz", True), ("owner_shard", "sanitized-debug")):
            with self.subTest(field=field):
                bad = copy.deepcopy(manifest)
                row = bad["expected"][index]
                old_id = row["id"]
                row[field] = value
                if field != "owner_shard":
                    # Re-sign every derived identity: semantic checks, not the
                    # fingerprint alone, must reject a Debug runtime claim.
                    row["id"] = ci_summary._coverage_row_id(bad["identity"], row)
                    for detected in bad["detected"]:
                        if detected["id"] == old_id:
                            detected["id"] = row["id"]
                    bad["executed"][0]["rows"] = [row["id"] if row_id == old_id else row_id for row_id in bad["executed"][0]["rows"]]
                    bad["policy"]["fingerprint"] = ci_summary._coverage_policy_fingerprint(bad["identity"], bad["expected"])
                self.assertTrue(self.validate(bad, environment, probes))
        self.assertTrue(self.validate(manifest, dict(environment, BUSTER_MATRIX_SHARD="sanitized-debug"), probes))

    def test_consumer_accepts_each_complete_native_selection(self):
        for key, plan in self.plans.items():
            with self.subTest(lane=key):
                manifest, environment, probes = self.completed_fixture(plan)
                errors = self.validate(manifest, environment, probes)
                self.assertEqual(errors, [] if manifest["executed"][0]["rows"] else ["coverage shard has no required configurations"])
                # Export-only test data itself can never satisfy production CI.
                self.assertTrue(ci_summary.validate_coverage_manifest(plan, environment))

    def test_summary_counts_grouped_and_split_work_without_hiding_other_rows(self):
        for key, plan in self.plans.items():
            with self.subTest(lane=key):
                summary = "\n".join(ci_summary._coverage_summary(plan, []))
                selected = len(plan["executed"][0]["rows"])
                self.assertIn(f"Selected shard: {key[2]}; required here: {selected}.", summary)
                for row in plan["expected"]:
                    self.assertIn(row["id"], summary)

    def test_consumer_rejects_missing_duplicate_foreign_and_shrunk_rows(self):
        for key, plan in self.plans.items():
            if key[2] == "combinations" or not plan["executed"][0]["rows"]:
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
            selected = set(manifest["executed"][0]["rows"])
            foreign = next(row["id"] for row in manifest["expected"] if row["state"] == "required" and row["id"] not in selected)
            bad = copy.deepcopy(manifest)
            bad["executed"][0]["rows"].append(foreign)
            damaged.append(bad)
            for version in (0, 1, 2, 4, True, 3.0, "3"):
                bad = copy.deepcopy(manifest)
                bad["partition_version"] = version
                damaged.append(bad)
            bad = copy.deepcopy(manifest)
            bad["expected"][0]["owner_shard"] = "checks" if bad["expected"][0]["owner_shard"] == "release" else "release"
            damaged.append(bad)
            bad = copy.deepcopy(manifest)
            original_owner = bad["expected"][0]["owner_shard"]
            bad["expected"][0]["owner_shard"] = next(owner for owner in MATRIX_OWNERS if owner != original_owner)
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
            for expected_shard in ("all", "unknown") + tuple(shard for shard in MATRIX_SELECTIONS if shard != key[2]):
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
    # Both release steps qualify the native process owner, then tee this log.
    OWNER_LOGS = {"Workflow tool regression tests": "workflow-tools-process-owner.log",
                  "Bootstrap wrapper regression tests": "bootstrap-wrapper-process-owner.log"}
    OWNER_INVOCATION = '"$process_owner_driver" bootstrap_process_owner_test |'
    # The helper command's input and discovery closure, besides the driver.
    OWNER_SOURCES = ("tools/windows_process_tree.c", "tools/windows_process_tree_test.c",
                     "src/buster/lib/base.h", "src/buster/lib/system_headers.h", "tests/build_compiler_identity.h")
    # The Windows child deadline of the wrapper harness. The real step bounds
    # this command only by its step timeout; uncontended it takes about 23 s,
    # but here it shares the CPUs with the other two suite lanes and exceeded
    # the 30 s replay deadline on a slower hosted Windows x64 runner (#3912).
    OWNER_QUALIFICATION_TIMEOUT_SECONDS = 120

    @classmethod
    def setUpClass(cls):
        # Compile one real native authority. The replay copies this binary for
        # the shell bootstrap only.
        cls.driver_temporary = tempfile.TemporaryDirectory(ignore_cleanup_errors=os.name == "nt")
        cls.driver = Path(cls.driver_temporary.name).resolve() / "build.exe"
        compiler = shutil.which("clang")
        if not compiler:
            raise RuntimeError("Native bootstrap helper controls require Clang")
        command = [compiler, "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
                   "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "build.c", "-o", str(cls.driver)]
        if os.name == "nt":
            command += ["-Wno-microsoft-enum-forward-reference", "-lws2_32"]
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)
        cls.owner_record = cls.driver.parent / "process-owner.out"
        cls.qualify_process_owner()

    @classmethod
    def process_owner_inputs(cls, root=ROOT):
        clang = shutil.which("clang")
        status = os.stat(clang)
        paths = (cls.driver,) + tuple(root / relative for relative in cls.OWNER_SOURCES)
        return (clang, status.st_size, status.st_mtime_ns) + tuple(hashlib.sha256(path.read_bytes()).hexdigest() for path in paths)

    @classmethod
    def qualify_process_owner(cls):
        # Each replay used to rerun this identical qualification: two helper
        # compiles, the Windows selftest and 306 native controls, about 20 s
        # per replay on hosted Windows (#3912). Run the step's exact command
        # once on the real closure and replay its verified output. Changed
        # inputs requalify before the next replay.
        for relative in cls.OWNER_SOURCES:
            (cls.driver.parent / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, cls.driver.parent / relative)
        inputs = cls.process_owner_inputs(cls.driver.parent)
        command = [str(cls.driver), "bootstrap_process_owner_test"]
        try:
            completed = subprocess.run(command, cwd=cls.driver.parent, capture_output=True,
                                       timeout=cls.OWNER_QUALIFICATION_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"{command} timed out after {cls.OWNER_QUALIFICATION_TIMEOUT_SECONDS} s: "
                               f"{error.stdout!r} {error.stderr!r}") from error
        prefix = b"BOOTSTRAP_PROCESS_OWNER "
        helpers = [line[len(prefix):].rstrip(b"\r").decode() for line in completed.stdout.splitlines() if line.startswith(prefix)]
        if completed.returncode or len(helpers) != 1 or not Path(helpers[0]).is_file():
            output = (completed.stdout + completed.stderr).decode(errors="replace")
            raise RuntimeError(f"{command} failed with status {completed.returncode}:\n{output}")
        cls.owner_record.write_bytes(completed.stdout)
        cls.owner_inputs, cls.owner_helper = inputs, helpers[0]

    @classmethod
    def tearDownClass(cls):
        cls.driver_temporary.cleanup()

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
        # The enclosing real step exports its own helper; replays must export theirs.
        self.environment = {key: value for key, value in os.environ.items() if key != "BUSTER_WINDOWS_PROCESS_TREE"}
        self.environment.update(BUSTER_CI_PYTHON=Path(sys.executable).as_posix(),
                                RUNNER_TEMP=self.root.as_posix(), BUSTER_MATRIX_SHARD="checks",
                                ZIG_TARGET="fixture", FIXTURE_EXIT="0", RUNNER_OS="Linux",
                                MATRIX_WORKFLOW_DRIVER=self.driver.as_posix(),
                                MATRIX_PROCESS_OWNER_RECORD=self.owner_record.as_posix())

    def run_step(self, name):
        body = self.steps[name].split("        run: |\n", 1)[1]
        script = "\n".join(line[10:] for line in body.splitlines() if line.startswith("          "))
        if name in self.OWNER_LOGS:
            # Intercept only the hosted driver bootstrap and, for the exact
            # qualified command, its native process-owner qualification; the
            # step still tees, parses and exports the verified output.
            script = 'clang() { cp "$MATRIX_WORKFLOW_DRIVER" "${@: -1}"; }\n' + script
            if self.OWNER_INVOCATION in script:
                if self.process_owner_inputs() != self.owner_inputs:
                    self.qualify_process_owner()
                script = script.replace(self.OWNER_INVOCATION, 'cat "$MATRIX_PROCESS_OWNER_RECORD" |')
        try:
            result = subprocess.run([self.shell, "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", script],
                                    cwd=self.root, env=self.environment, text=True, capture_output=True, timeout=30)
        except subprocess.TimeoutExpired as error:
            # A timeout can precede SUITE_START while the native helper is
            # built/qualified. Retain both captures, not just suite markers.
            try:
                limit = 4096
                print(f"WORKFLOW_STEP_TIMEOUT step={name[:256]!r} "
                      f"FIXTURE_EXIT={self.environment.get('FIXTURE_EXIT', '')[:128]!r} timeout_seconds=30", file=sys.stderr)
                for label, value in (("stdout", error.stdout), ("stderr", error.stderr)):
                    if isinstance(value, bytes):
                        value = value.decode("utf-8", errors="replace")
                    text = value if value is not None else "[unavailable]"
                    if len(text) > limit:
                        text = (text[:limit // 2] + f"\n[truncated {len(text) - limit} characters]\n" + text[-limit // 2:])
                    print(f"WORKFLOW_STEP_TIMEOUT {label}:\n{text or '[empty]'}", file=sys.stderr)
                owner_log = self.OWNER_LOGS.get(name)
                if owner_log:
                    try:
                        with (self.root / "buster-ci" / owner_log).open("rb") as stream:
                            size = stream.seek(0, os.SEEK_END)
                            stream.seek(max(0, size - limit))
                            tail = stream.read(limit).decode("utf-8", errors="replace")
                        omitted = f"[truncated {size - limit} bytes]\n" if size > limit else ""
                        print(f"WORKFLOW_STEP_TIMEOUT {owner_log}:\n{omitted}{tail or '[empty]'}", file=sys.stderr)
                    except OSError as log_error:
                        print(f"WORKFLOW_STEP_TIMEOUT {owner_log}: [unavailable: {type(log_error).__name__}]", file=sys.stderr)
            except Exception:
                # Secondary diagnostic I/O must never replace the timeout.
                pass
            raise
        return result

    def test_workflow_tools_keep_platform_budget_and_all_suites(self):
        block = self.steps["Workflow tool regression tests"]
        self.assertIn("timeout-minutes: ${{ (matrix.os == 'windows' || matrix.os == 'macos') && 5 || 2 }}", block)
        self.assertIn("matrix.shard == 'release'", block)
        self.assertIn("set -euo pipefail", block)
        self.assertNotIn("continue-on-error:", block)
        expected = {
            "tools/ci_admission_test.py", "tools/main_ci_reuse_test.py",
            "tools/ci_zig_test.py",
            "tools/ci_zig_cache_test.py",
            "tools/analyzer_selection_test.py", "tools/coverage_manifest_test.py",
            "tools/matrix_shard_test.py", "tools/differential_ci_policy_test.py",
            "tools/native_producer_profile_test.py", "tools/native_target_compatibility_test.py",
            "tools/ci_llvm_test.py",
            "tools/ci_configure_evidence_test.py",
            "tools/ci_matrix_phases_test.py", "tools/ci_matrix_phases_bridge_test.py",
            "tools/ci_native_observation_test.py", "tools/ci_sanitize_logs_test.py",
            "tools/matrix_unit_observation_test.py",
            "tools/github_ci_time_test.py", "tools/ci_vs_dev_shell_test.py",
            "tools/ci_workflow_tools_test.py",
            "tools/ci_checks_resources_test.py", "tools/ci_checks_resources_workflow_test.py",
            "tools/ci_ebpf_kernel_oracle_test.py",
            "tools/bootstrap_wrapper_cases_test.py",
        }
        suites = re.findall(r'^            ([^ =]+\.py)=[^ =]+\.log$', block, re.M)
        self.assertEqual(set(suites), expected)
        self.assertEqual(len(suites), len(expected))
        self.assertIn("suites+=(tools/ci_runner_resources_test.py=runner-resources-test.log)", block)
        self.assertIn('"$BUSTER_CI_PYTHON" tools/ci_workflow_tools.py --jobs 3 --log-directory "$RUNNER_TEMP/buster-ci" "${suites[@]}"', block)
        self.assertNotIn("|| true", block)

    def test_workflow_tools_record_all_suites_and_report_failure(self):
        expected = re.findall(r'^            ([^ =]+\.py)=([^ =]+\.log)$',
                              self.steps["Workflow tool regression tests"], re.M)
        runner = self.root / "tools/ci_workflow_tools.py"
        runner.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "tools/ci_workflow_tools.py", runner)
        for suite, _ in expected:
            path = self.root / suite
            path.parent.mkdir(parents=True, exist_ok=True)
            # Every suite receives the helper path the step exported.
            path.write_text("import os, sys\nprint('SUITE fixture', os.environ.get('BUSTER_WINDOWS_PROCESS_TREE'))\n"
                            "sys.exit(int(os.environ['FIXTURE_EXIT']) if __file__.endswith('ci_admission_test.py') else 0)\n",
                            encoding="utf-8")
        for code in (0, 7):
            with self.subTest(exit_code=code):
                shutil.rmtree(self.root / "buster-ci", ignore_errors=True)
                self.environment["FIXTURE_EXIT"] = str(code)
                result = self.run_step("Workflow tool regression tests")
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                # A failure no longer hides the remaining suites' evidence.
                self.assertEqual(sorted(re.findall(r'SUITE_START path=([^ ]+)', result.stdout)),
                                 sorted(suite for suite, _ in expected))
                for suite, log in expected:
                    status = "failure status=7" if code and suite.endswith("ci_admission_test.py") else "success"
                    self.assertIn(f"SUITE_END path={suite} result={status} ", result.stdout)
                    self.assertIn(f"SUITE fixture {self.owner_helper}\n", (self.root / "buster-ci" / log).read_text())
                self.assertEqual("WORKFLOW_TOOLS_END result=success" in result.stdout, code == 0)
                self.assertEqual("WORKFLOW_TOOLS_END result=failure failed=tools/ci_admission_test.py " in result.stdout,
                                 code != 0)

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
        tools = self.root / "tools"
        tools.mkdir()
        # Like Windows case execution, refuse to start without the exported helper.
        fixture = (
            "import os, pathlib, sys\nhelper = os.environ.get('BUSTER_WINDOWS_PROCESS_TREE', '')\n"
            "print('BOOTSTRAP_TEST helper', helper)\nos.path.isfile(helper) or sys.exit(125)\n"
            "pathlib.Path('wrapper-called').touch()\n"
            "print('BOOTSTRAP_TEST fixture', pathlib.Path(__file__).name, sys.argv[1:])\n"
            "sys.exit(int(os.environ['FIXTURE_EXIT']))\n")
        (tests / "bootstrap_wrapper_test.py").write_text(fixture)
        (tools / "bootstrap_wrapper_cases.py").write_text(fixture)
        selections = (("Linux", "bootstrap_wrapper_test.py", "BootstrapWrapperTests"),
                      ("Windows", "bootstrap_wrapper_cases.py", "--jobs"))
        for platform, entry, argument in selections:
            noncanonical = tuple((shard, 7) for shard in MATRIX_SELECTIONS if shard not in ("combinations", "release"))
            for shard, code in noncanonical + (("release", 0), ("release", 7)):
                with self.subTest(platform=platform, shard=shard, exit_code=code):
                    marker = self.root / "wrapper-called"
                    if marker.exists():
                        marker.unlink()
                    shutil.rmtree(self.root / "buster-ci", ignore_errors=True)
                    self.environment.update(BUSTER_MATRIX_SHARD=shard, FIXTURE_EXIT=str(code),
                                            RUNNER_OS=platform)
                    result = self.run_step("Bootstrap wrapper regression tests")
                    self.assertEqual(result.returncode, code if shard == "release" else 0, result.stdout + result.stderr)
                    self.assertEqual(marker.exists(), shard == "release")
                    if shard != "release":
                        self.assertIn("owned-by-release-shard", result.stdout)
                        self.assertFalse((self.root / "buster-ci/bootstrap-wrapper.log").exists())
                    else:
                        log = (self.root / "buster-ci/bootstrap-wrapper.log").read_text()
                        self.assertIn(f"BOOTSTRAP_TEST helper {self.owner_helper}\n", log)
                        self.assertIn(f"BOOTSTRAP_TEST fixture {entry}", log)
                        self.assertIn(argument, log)
                        self.assertIn("'4'" if platform == "Windows" else "'-v'", log)

    def test_process_owner_record_is_bound_to_its_inputs_and_fails_closed(self):
        name = "Bootstrap wrapper regression tests"
        self.assertEqual([self.steps[step].count(self.OWNER_INVOCATION) for step in self.OWNER_LOGS], [1, 1])
        # A changed invocation runs for real; changed inputs requalify first.
        changed = self.steps[name].replace("bootstrap_process_owner_test |", "bootstrap_process_owner_test --changed |")
        with mock.patch.dict(self.steps, {name: changed}), mock.patch.object(subprocess, "run") as run:
            self.run_step(name)
        self.assertIn("bootstrap_process_owner_test --changed |", run.call_args.args[0][-1])
        self.assertNotIn("MATRIX_PROCESS_OWNER_RECORD", run.call_args.args[0][-1])
        with mock.patch.object(type(self), "owner_inputs", ()), mock.patch.object(type(self), "qualify_process_owner") as qualify, \
             mock.patch.object(subprocess, "run") as run:
            self.run_step(name)
        qualify.assert_called_once_with()
        self.assertIn('cat "$MATRIX_PROCESS_OWNER_RECORD" |', run.call_args.args[0][-1])
        # Only the verified record exports an existing helper to the suite.
        (self.root / "tests").mkdir()
        (self.root / "tests/bootstrap_wrapper_test.py").write_text(
            "import os, sys\nsys.exit(0 if os.path.isfile(os.environ.get('BUSTER_WINDOWS_PROCESS_TREE', '')) else 125)\n")
        self.environment.update(BUSTER_MATRIX_SHARD="release", RUNNER_OS="Linux")
        valid = self.owner_record.read_bytes()
        records = {"valid": (valid, 0), "missing": (None, 1),
                   "corrupt": (b"".join(line for line in valid.splitlines(True) if not line.startswith(b"BOOTSTRAP_PROCESS_OWNER ")), 125),
                   "stale": (valid.replace(self.owner_helper.encode(), str(self.root / "deleted-helper").encode()), 125)}
        for kind, (content, status) in records.items():
            with self.subTest(record=kind):
                record = self.root / f"{kind}.out"
                if content is not None:
                    record.write_bytes(content)
                self.environment["MATRIX_PROCESS_OWNER_RECORD"] = record.as_posix()
                result = self.run_step(name)
                self.assertEqual(result.returncode, status, result.stdout + result.stderr)


class WorkflowStepTimeoutEvidenceTests(unittest.TestCase):
    """Synthetic timeouts retain evidence without waiting or spawning a shell."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.fixture = WorkflowSetupTests()
        self.fixture.root = self.root
        self.fixture.shell = "fixture-bash"
        self.fixture.environment = {"FIXTURE_EXIT": "7"}
        self.fixture.steps = {"Workflow tool regression tests": "        run: |\n          printf fixture\n"}
        self.owner_log = self.root / "buster-ci/workflow-tools-process-owner.log"

    def timeout_output(self, error):
        output = io.StringIO()
        with mock.patch.object(subprocess, "run", side_effect=error) as run, contextlib.redirect_stderr(output):
            with self.assertRaises(subprocess.TimeoutExpired) as caught:
                self.fixture.run_step("Workflow tool regression tests")
        self.assertIs(caught.exception, error)
        run.assert_called_once()
        self.assertEqual(run.call_args.kwargs, dict(cwd=self.root, env=self.fixture.environment,
                                                   text=True, capture_output=True, timeout=30))
        self.assertEqual(run.call_args.args[0][:-1], ["fixture-bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c"])
        return output.getvalue()

    def test_partial_bytes_and_text_capture_and_existing_helper_log(self):
        self.fixture.environment["FIXTURE_EXIT"] = "0"
        self.owner_log.parent.mkdir()
        self.owner_log.write_bytes(b"helper qualification progress\xff\n")
        error = subprocess.TimeoutExpired("fixture", 30, output=b"SUITE_START path=fixture\xff\n", stderr="compiler progress\n")
        text = self.timeout_output(error)
        self.assertIn("step='Workflow tool regression tests' FIXTURE_EXIT='0' timeout_seconds=30", text)
        self.assertIn("stdout:\nSUITE_START path=fixture\ufffd", text)
        self.assertIn("stderr:\ncompiler progress", text)
        self.assertIn("workflow-tools-process-owner.log:\nhelper qualification progress\ufffd", text)

    def test_large_captures_and_log_are_bounded_and_keep_final_progress(self):
        self.owner_log.parent.mkdir()
        self.owner_log.write_bytes(b"omitted-log-start" + b"L" * 9000 + b"helper-tail")
        error = subprocess.TimeoutExpired("fixture", 30, output="stdout-head" + "X" * 9000 + "stdout-tail",
                                          stderr=b"stderr-head" + b"Y" * 9000 + b"stderr-tail")
        text = self.timeout_output(error)
        for marker in ("stdout-head", "stdout-tail", "stderr-head", "stderr-tail", "helper-tail"):
            self.assertIn(marker, text)
        self.assertNotIn("omitted-log-start", text)
        self.assertEqual(text.count("[truncated "), 3)
        self.assertLess(len(text), 3 * 4096 + 1024)

    def test_missing_and_empty_captures_and_log_preserve_timeout(self):
        for stdout, stderr in ((None, None), ("", b"")):
            for log_present in (False, True):
                with self.subTest(stdout=stdout, stderr=stderr, log_present=log_present):
                    if log_present:
                        self.owner_log.parent.mkdir(exist_ok=True)
                        self.owner_log.write_bytes(b"")
                    else:
                        self.owner_log.unlink(missing_ok=True)
                    text = self.timeout_output(subprocess.TimeoutExpired("fixture", 30, output=stdout, stderr=stderr))
                    self.assertIn("[unavailable]" if stdout is None else "[empty]", text)
                    self.assertIn("workflow-tools-process-owner.log:\n[empty]" if log_present else
                                  "workflow-tools-process-owner.log: [unavailable: FileNotFoundError]", text)

    def test_helper_log_read_error_is_secondary(self):
        with mock.patch.object(Path, "open", side_effect=PermissionError("fixture denied")):
            text = self.timeout_output(subprocess.TimeoutExpired("fixture", 30, output="partial", stderr=None))
        self.assertIn("stdout:\npartial", text)
        self.assertIn("[unavailable: PermissionError]", text)

    def test_diagnostic_write_error_does_not_replace_original_timeout(self):
        error = subprocess.TimeoutExpired("fixture", 30, output="partial")
        with mock.patch.object(subprocess, "run", side_effect=error) as run:
            with mock.patch.object(sys, "stderr", mock.Mock(write=mock.Mock(side_effect=OSError("fixture closed")))):
                with self.assertRaises(subprocess.TimeoutExpired) as caught:
                    self.fixture.run_step("Workflow tool regression tests")
        self.assertIs(caught.exception, error)
        run.assert_called_once()

    def test_completed_step_returns_unchanged_without_diagnostic_io(self):
        result = subprocess.CompletedProcess("fixture", 7, stdout="expected failure", stderr="control")
        with mock.patch.object(subprocess, "run", return_value=result) as run, mock.patch.object(Path, "open") as opened:
            with contextlib.redirect_stderr(io.StringIO()) as output:
                actual = self.fixture.run_step("Workflow tool regression tests")
        self.assertIs(actual, result)
        self.assertEqual(output.getvalue(), "")
        run.assert_called_once()
        opened.assert_not_called()


class CompletionGateTests(unittest.TestCase):
    def sample(self, checks_layout=github_ci_time.DEFAULT_CHECKS_LAYOUT, names=None):
        jobs = []
        # Historical layouts pass their frozen names; every desktop name gets
        # the desktop steps of its own historical or current owner.
        desktop_names = set(github_ci_time.SPLIT_COMBINATION_PLATFORMS + github_ci_time.COMBINATION_PLATFORMS +
                            github_ci_time.MACOS_SPLIT_COMBINATION_PLATFORMS)
        for number, name in enumerate(names or github_ci_time.combination_jobs(checks_layout)):
            steps = []
            if name in desktop_names:
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
                         "steps": [{"name": step, "status": "completed", "conclusion": "success"} for step in steps]})
        return jobs

    def check(self, jobs, attempt=1, checks_layout=github_ci_time.DEFAULT_CHECKS_LAYOUT):
        return github_ci_time.validate_required_jobs(jobs, 123, attempt, "a" * 40,
                                                     expected_names=github_ci_time.combination_jobs(checks_layout))

    def test_all_twenty_one_jobs_and_exact_ten_desktop_shards(self):
        self.assertEqual(len(github_ci_time.COMBINATION_JOBS), 19)
        self.assertEqual(len(github_ci_time.COMBINATION_PLATFORMS), 10)
        self.assertEqual(self.check(self.sample("combined"), checks_layout="combined"), [])

    def test_split_inventory_is_exact_and_cannot_mix_with_combined_checks(self):
        jobs = self.sample("split")
        # #2657: four split platforms x (release, sanitized-release,
        # portability) plus Windows AArch64 release/checks.
        self.assertEqual(len(github_ci_time.SPLIT_COMBINATION_JOBS), 23)
        self.assertEqual(len(github_ci_time.SPLIT_COMBINATION_PLATFORMS), 14)
        self.assertEqual(len(github_ci_time.MACOS_SPLIT_COMBINATION_JOBS), 29)
        self.assertFalse(any(name.endswith(" sanitized-debug") for name in github_ci_time.SPLIT_COMBINATION_JOBS))
        self.assertEqual(self.check(jobs, checks_layout="split"), [])
        self.assertTrue(self.check(jobs, checks_layout="combined"))
        self.assertTrue(self.check(self.sample("combined"), checks_layout="split"))
        for name in github_ci_time.SPLIT_COMBINATION_PLATFORMS:
            if name in github_ci_time.COMBINATION_PLATFORMS:
                continue
            with self.subTest(sibling=name):
                index = next(i for i, job in enumerate(jobs) if job["name"] == name)
                missing = copy.deepcopy(jobs)
                missing.pop(index)
                self.assertTrue(self.check(missing, checks_layout="split"))
                duplicate = copy.deepcopy(jobs)
                duplicate.append(copy.deepcopy(duplicate[index]))
                self.assertTrue(self.check(duplicate, checks_layout="split"))
                for conclusion in ("failure", "cancelled", "skipped", None):
                    failed = copy.deepcopy(jobs)
                    failed[index]["conclusion"] = conclusion
                    self.assertTrue(self.check(failed, checks_layout="split"))
                for step_index in range(len(jobs[index]["steps"])):
                    incomplete = copy.deepcopy(jobs)
                    incomplete[index]["steps"].pop(step_index)
                    self.assertTrue(self.check(incomplete, checks_layout="split"))
                hybrid = copy.deepcopy(jobs)
                hybrid[index]["name"] = name.rsplit(" ", 1)[0] + " checks"
                self.assertTrue(self.check(hybrid, checks_layout="split"))
                # A removed full-runtime sanitized Debug job is foreign now,
                # whether added beside or substituted for a current owner.
                stale = copy.deepcopy(jobs)
                stale.append(dict(copy.deepcopy(jobs[index]), id=9999, name=name.rsplit(" ", 1)[0] + " sanitized-debug"))
                self.assertTrue(self.check(stale, checks_layout="split"))
                stale[index]["name"] = name.rsplit(" ", 1)[0] + " sanitized-debug"
                stale.pop()
                self.assertTrue(self.check(stale, checks_layout="split"))

    def test_split_sibling_partial_reruns_use_the_latest_attempt(self):
        jobs = self.sample("split")
        for name in github_ci_time.SPLIT_COMBINATION_PLATFORMS:
            if name in github_ci_time.COMBINATION_PLATFORMS:
                continue
            with self.subTest(sibling=name):
                current_gate = dict(jobs[-1], run_attempt=2)
                newer = copy.deepcopy(next(job for job in jobs if job["name"] == name))
                newer.update(run_attempt=2, conclusion="failure")
                history = jobs + [current_gate, newer]
                latest = github_ci_time.latest_run_jobs(history, 123, 2, "a" * 40)
                self.assertTrue(self.check(latest, attempt=2, checks_layout="split"))
                newer["conclusion"] = "success"
                latest = github_ci_time.latest_run_jobs(history, 123, 2, "a" * 40)
                self.assertEqual(self.check(latest, attempt=2, checks_layout="split"), [])
                with self.assertRaises(ValueError):
                    github_ci_time.latest_run_jobs(history + [newer], 123, 2, "a" * 40)

    def test_split_api_gate_accepts_ordinary_runs_and_exact_split_dispatch_refs(self):
        jobs = self.sample("split")
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40,
               "event": "workflow_dispatch", "head_branch": "codex/ci-checks-split-overlap"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1, checks_layout="split")
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, {"total_count": len(jobs), "jobs": jobs}]):
            self.assertTrue(github_ci_time.require_jobs(args)["success"])
        for event in ("push", "pull_request", "merge_group", "workflow_dispatch"):
            for branch in ("main", "v1.0", "codex/ci-checks-split-overlap",
                           "codex/2120-evidence-v2-split-overlap", "codex/ci-checks-combined-overlap"):
                if event == "workflow_dispatch" and branch in github_ci_time.COMBINED_QUALIFICATION_BRANCHES:
                    continue
                with self.subTest(event=event, branch=branch), mock.patch.object(
                        github_ci_time, "api_get", side_effect=[dict(run, event=event, head_branch=branch),
                                                               {"total_count": len(jobs), "jobs": jobs}]):
                    self.assertTrue(github_ci_time.require_jobs(args)["success"])
        for branch in github_ci_time.COMBINED_QUALIFICATION_BRANCHES:
            args.event_ref = "refs/heads/" + branch
            with self.subTest(branch=branch), mock.patch.object(github_ci_time, "api_get", return_value=dict(run, head_branch=branch)) as fetch:
                with self.assertRaisesRegex(ValueError, "event and exact qualification ref"):
                    github_ci_time.require_jobs(args)
                self.assertEqual(fetch.call_count, 1)
            args.event_ref = "refs/tags/" + branch
            with self.subTest(tag=branch), mock.patch.object(github_ci_time, "api_get", side_effect=[
                    dict(run, head_branch=branch), {"total_count": len(jobs), "jobs": jobs}]):
                self.assertTrue(github_ci_time.require_jobs(args)["success"])

    def test_combined_api_gate_keeps_exact_manual_cohorts_only(self):
        jobs = self.sample("combined")
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40,
               "event": "workflow_dispatch"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1, checks_layout="combined")
        for branch in github_ci_time.COMBINED_QUALIFICATION_BRANCHES:
            args.event_ref = "refs/heads/" + branch
            with self.subTest(branch=branch), mock.patch.object(github_ci_time, "api_get", side_effect=[
                    dict(run, head_branch=branch), {"total_count": len(jobs), "jobs": jobs}]):
                self.assertTrue(github_ci_time.require_jobs(args)["success"])
            args.event_ref = "refs/tags/" + branch
            with self.subTest(tag=branch), mock.patch.object(github_ci_time, "api_get", return_value=dict(run, head_branch=branch)):
                with self.assertRaisesRegex(ValueError, "event and exact qualification ref"):
                    github_ci_time.require_jobs(args)
            args.event_ref = "refs/heads/" + branch
            for event in ("push", "pull_request", "merge_group"):
                with self.subTest(event=event, branch=branch), mock.patch.object(
                        github_ci_time, "api_get", return_value=dict(run, event=event, head_branch=branch)) as fetch:
                    with self.assertRaisesRegex(ValueError, "event and exact qualification ref"):
                        github_ci_time.require_jobs(args)
                    self.assertEqual(fetch.call_count, 1)
        for branch in (None, "main", "codex/ci-checks-combined-overlap-extra",
                       "codex/ci-checks-split-overlap", "codex/2120-evidence-v2-split-overlap"):
            args.event_ref = "refs/heads/" + branch if branch else None
            with self.subTest(branch=branch), mock.patch.object(github_ci_time, "api_get", return_value=dict(run, head_branch=branch)):
                with self.assertRaisesRegex(ValueError, "event and exact qualification ref"):
                    github_ci_time.require_jobs(args)

    def test_split_api_gate_rejects_wrong_workflow_or_execution_identity(self):
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1, checks_layout="split")
        for branch in ("codex/ci-checks-split-overlap", "codex/2120-evidence-v2-split-overlap"):
            run = dict(id=123, run_attempt=1, path=".github/workflows/ci.yml", head_sha="a" * 40,
                       event="workflow_dispatch", head_branch=branch)
            for field, value in (("id", 124), ("run_attempt", 2), ("path", ""),
                                 ("path", ".github/workflows/other.yml"), ("path", ".github/workflows/ci.yml-extra"),
                                 ("head_sha", "unknown"), ("head_sha", True)):
                with self.subTest(branch=branch, field=field, value=value), mock.patch.object(
                        github_ci_time, "api_get", return_value=dict(run, **{field: value})) as fetch:
                    with self.assertRaisesRegex(ValueError, "identity does not match|no exact source identity"):
                        github_ci_time.require_jobs(args)
                    self.assertEqual(fetch.call_count, 1)

    def test_prospective_split_api_gate_does_not_admit_missing_duplicate_or_foreign_jobs(self):
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1, checks_layout="split")
        run = dict(id=123, run_attempt=1, path=".github/workflows/ci.yml", head_sha="a" * 40,
                   event="workflow_dispatch", head_branch="codex/2120-evidence-v2-split-overlap")
        for mutation in ("missing", "duplicate", "foreign-source", "combined-inventory"):
            jobs = self.sample("split")
            if mutation == "missing":
                jobs.pop(0)
            elif mutation == "duplicate":
                jobs.append(copy.deepcopy(jobs[0]))
            elif mutation == "foreign-source":
                jobs[0]["head_sha"] = "b" * 40
            else:
                jobs = self.sample("combined")
            batch = {"total_count": len(jobs), "jobs": jobs}
            with self.subTest(mutation=mutation), mock.patch.object(
                    github_ci_time, "api_get", side_effect=[run] + [batch] * 4), \
                    mock.patch.object(github_ci_time.time, "sleep"):
                report = github_ci_time.require_jobs(args)
            self.assertFalse(report["success"])
            self.assertTrue(report["errors"])

    def test_missing_duplicate_failed_cancelled_and_skipped_jobs_fail(self):
        total = len(github_ci_time.combination_jobs())
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
            if job["name"] not in github_ci_time.SPLIT_COMBINATION_PLATFORMS + github_ci_time.NATIVE:
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
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        responses = [run, {"total_count": total, "jobs": jobs[:10]}, {"total_count": total, "jobs": jobs[10:]}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses) as fetch:
            result = github_ci_time.require_jobs(args)
            self.assertTrue(result["success"])
            self.assertEqual({key: result["job_metadata"][key] for key in
                              ("snapshot_attempts", "refreshes", "refresh_budget_seconds", "final_page_size")},
                             {"snapshot_attempts": 1, "refreshes": 0, "refresh_budget_seconds": 30.0,
                              "final_page_size": 100})
            self.assertEqual([lookup["outcome"] for lookup in result["job_metadata"]["lookups"]], ["ok"] * 3)
            job = next(item for item in result["jobs"] if item["name"] == "Linux x86-64 release")
            self.assertEqual(job["run_id"], 123)
            self.assertEqual(job["head_sha"], "a" * 40)
            step = next(item for item in job["required_steps"] if item["name"] == "Desktop result and reproduction")
            self.assertEqual(step["observed"], [{"status": "completed", "conclusion": "success"}])
            self.assertIn("filter=all", fetch.call_args_list[1].args[1])
            self.assertIn("page=2", fetch.call_args_list[2].args[1])
        for last in ({"total_count": total, "jobs": []}, {"total_count": total - 1, "jobs": jobs[10:]}):
            with mock.patch.object(github_ci_time, "api_get", side_effect=responses[:2] + [last]):
                with self.assertRaises(ValueError):
                    github_ci_time.require_jobs(args)
        with mock.patch.object(github_ci_time, "api_get", side_effect=[dict(run, run_attempt=2)]):
            with self.assertRaises(ValueError):
                github_ci_time.require_jobs(args)

    def test_delayed_job_status_is_refreshed_before_the_gate_decides(self):
        jobs = self.sample()
        pending = copy.deepcopy(jobs)
        pending[0].update(status="in_progress", conclusion=None)
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        responses = [run, {"total_count": len(jobs), "jobs": pending},
                     {"total_count": len(jobs), "jobs": jobs}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 2)
        self.assertEqual(fetch.call_count, 3)
        sleep.assert_called_once_with(1.0)

    def test_stale_in_progress_step_record_is_refreshed_for_exact_run_and_head(self):
        jobs = self.sample()
        pending = copy.deepcopy(jobs)
        target = next(job for job in pending if job["name"] == "macOS AArch64 sanitized-release")
        step = next(step for step in target["steps"] if step["name"] == "Desktop result and reproduction")
        step.update(status="in_progress", conclusion=None)
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        responses = [run, {"total_count": len(jobs), "jobs": pending},
                     {"total_count": len(jobs), "jobs": jobs}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses), \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 2)
        sleep.assert_called_once_with(1.0)

    def test_terminal_failure_is_not_retried_or_replaced_by_a_later_green_snapshot(self):
        jobs = self.sample()
        failed = copy.deepcopy(jobs)
        failed[0].update(conclusion="failure")
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, {"total_count": len(jobs), "jobs": failed}]) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertFalse(result["success"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 1)
        self.assertEqual(fetch.call_count, 2)
        sleep.assert_not_called()
        self.assertTrue(any("required job did not complete successfully" in error for error in result["errors"]))

    def test_partial_rerun_shadow_never_borrows_an_older_green_steps_record(self):
        first_attempt = self.sample()
        target_name = "macOS AArch64 sanitized-release"
        shadow = copy.deepcopy(next(job for job in first_attempt if job["name"] == target_name))
        shadow.update(id=101, run_attempt=2, steps=[])
        current_gate = copy.deepcopy(next(job for job in first_attempt if job["name"] == "CI complete"))
        current_gate.update(id=102, run_attempt=2)
        history = first_attempt + [current_gate, shadow]
        run = {"id": 123, "run_attempt": 2, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=2)
        batch = {"total_count": len(history), "jobs": history}
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run] + [batch] * 4) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        # After every refresh the shadow's job-level success is accepted with a
        # recorded waiver (#4102); attempt 1's step records are never used.
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["step_proof_waived"], [target_name])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 4)
        self.assertEqual(fetch.call_count, 5)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [1.0, 2.0, 4.0])
        target = next(job for job in result["jobs"] if job["name"] == target_name)
        self.assertEqual(target["run_attempt"], 2)
        self.assertEqual(target["run_id"], 123)
        self.assertEqual(target["head_sha"], "a" * 40)
        self.assertTrue(target["step_proof_waived"])
        self.assertTrue(all(step["matching_records"] == 0 for step in target["required_steps"]))
        shadow["steps"] = [{"name": "Desktop result and reproduction", "status": "completed", "conclusion": "skipped"}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, batch]), \
                mock.patch.object(github_ci_time.time, "sleep"):
            result = github_ci_time.require_jobs(args)
        self.assertFalse(result["success"])
        self.assertEqual(result["step_proof_waived"], [])
        missing = [error for error in result["errors"]
                   if target_name in error and "lacks unique completion proof" in error]
        self.assertEqual(len(missing), 3)
        self.assertIn(f"{target_name}: required step 'Desktop result and reproduction' concluded 'skipped'; "
                      "success required", result["errors"])

    def test_metadata_refresh_budget_prevents_an_extra_sleep_or_snapshot(self):
        jobs = self.sample()
        pending = copy.deepcopy(jobs)
        pending[0].update(status="in_progress", conclusion=None)
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        batch = {"total_count": len(jobs), "jobs": pending}
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, batch]) as fetch, \
                mock.patch.object(github_ci_time.time, "monotonic", side_effect=[100.0, 100.0, 100.0, 129.5]), \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertFalse(result["success"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 1)
        self.assertEqual(fetch.call_count, 2)
        sleep.assert_not_called()
        self.assertTrue(any("refresh budget expired before another exact-run snapshot" in error
                            for error in result["errors"]))

    def test_completed_job_with_missing_steps_refreshes_exact_snapshot(self):
        jobs = self.sample()
        incomplete = copy.deepcopy(jobs)
        target = next(job for job in incomplete if job["name"] == "Windows x86-64 sanitized-release")
        target["steps"] = []
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        batch = {"total_count": len(jobs), "jobs": incomplete}
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, batch,
                                                                          {"total_count": len(jobs), "jobs": jobs}]) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 2)
        self.assertEqual(fetch.call_count, 3)
        sleep.assert_called_once_with(1.0)

    def gate_inputs(self, run_attempt=1):
        run = {"id": 123, "run_attempt": run_attempt, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=run_attempt)
        return run, args

    @staticmethod
    def http_error(code):
        return urllib.error.HTTPError("https://api.github.com/", code, "Bad Gateway" if code >= 500 else "Forbidden",
                                      {}, None)

    def test_jobs_page_5xx_recovers_on_a_later_smaller_page_snapshot(self):
        jobs = self.sample()
        run, args = self.gate_inputs()
        batch = {"total_count": len(jobs), "jobs": jobs[:github_ci_time.JOB_PAGE_FALLBACK_SIZE]}
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, self.http_error(502), batch]) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 2)
        self.assertEqual(result["job_metadata"]["final_page_size"], github_ci_time.JOB_PAGE_FALLBACK_SIZE)
        self.assertIn("per_page=100&page=1", fetch.call_args_list[1].args[1])
        self.assertIn(f"per_page={github_ci_time.JOB_PAGE_FALLBACK_SIZE}&page=1", fetch.call_args_list[2].args[1])
        self.assertEqual([lookup["outcome"] for lookup in result["job_metadata"]["lookups"]],
                         ["ok", "HTTP 502 Bad Gateway", "ok"])
        sleep.assert_called_once_with(1.0)

    def test_persistent_jobs_page_5xx_exhausts_the_snapshots_and_fails_closed(self):
        run, args = self.gate_inputs()
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run] + [self.http_error(503)] * 4) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertFalse(result["success"])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 4)
        self.assertEqual(fetch.call_count, 5)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [1.0, 2.0, 4.0])
        self.assertEqual(result["jobs"], [])
        self.assertEqual(len(result["errors"]), 1)
        self.assertIn("unavailable: HTTP 503", result["errors"][0])
        self.assertIn("exact run/head proof unresolved after 4 snapshots", result["errors"][0])

    def test_jobs_page_5xx_never_borrows_an_earlier_snapshot(self):
        jobs = self.sample()
        pending = copy.deepcopy(jobs)
        pending[0].update(status="in_progress", conclusion=None)
        run, args = self.gate_inputs()
        responses = [run, {"total_count": len(jobs), "jobs": pending}] + [self.http_error(502)] * 3
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses), \
                mock.patch.object(github_ci_time.time, "sleep"):
            result = github_ci_time.require_jobs(args)
        self.assertFalse(result["success"])
        self.assertEqual(result["jobs"], [])
        self.assertTrue(all("HTTP 502" in error for error in result["errors"]))

    def test_client_errors_fail_immediately_without_retry(self):
        jobs = self.sample()
        run, args = self.gate_inputs()
        for responses in ([run, self.http_error(403)], [self.http_error(404)]):
            with mock.patch.object(github_ci_time, "api_get", side_effect=responses + [
                    {"total_count": len(jobs), "jobs": jobs}]) as fetch, \
                    mock.patch.object(github_ci_time.time, "sleep") as sleep:
                with self.assertRaises(urllib.error.HTTPError):
                    github_ci_time.require_jobs(args)
            self.assertEqual(fetch.call_count, len(responses))
            sleep.assert_not_called()

    def test_transient_run_read_failure_recovers_within_the_budget(self):
        jobs = self.sample()
        run, args = self.gate_inputs()
        responses = [urllib.error.URLError("connection reset"), TimeoutError("read timed out"), run,
                     {"total_count": len(jobs), "jobs": jobs}]
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses), \
                mock.patch.object(github_ci_time.time, "sleep") as sleep:
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [1.0, 2.0])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 1)
        with mock.patch.object(github_ci_time, "api_get", side_effect=[self.http_error(502)] * 4) as fetch, \
                mock.patch.object(github_ci_time.time, "sleep"):
            with self.assertRaisesRegex(OSError, "run 123 metadata unavailable after 4 reads"):
                github_ci_time.require_jobs(args)
        self.assertEqual(fetch.call_count, 4)

    def run_gate_main(self, responses, output, summary):
        stderr = io.StringIO()
        argv = ["github_ci_time.py", "require-jobs", "--repository", "buster14a/buster", "--run-id", "123",
                "--run-attempt", "1", "--output", str(output)]
        with mock.patch.object(github_ci_time, "api_get", side_effect=responses), \
                mock.patch.object(github_ci_time.time, "sleep"), \
                mock.patch.object(sys, "argv", argv), \
                mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(summary)}), \
                contextlib.redirect_stderr(stderr):
            status = github_ci_time.main()
        return status, stderr.getvalue()

    def test_unsuccessful_gate_with_output_prints_its_errors(self):
        jobs = self.sample()
        jobs[0]["conclusion"] = "failure"
        run, _ = self.gate_inputs()
        with tempfile.TemporaryDirectory() as temporary:
            output, summary = Path(temporary) / "gate.json", Path(temporary) / "summary.md"
            status, log = self.run_gate_main([run, {"total_count": len(jobs), "jobs": jobs}], output, summary)
            report = json.loads(output.read_text(encoding="utf-8"))
            summary_text = summary.read_text(encoding="utf-8")
            self.assertEqual(status, 1)
            self.assertFalse(report["success"])
            self.assertIn(f"run 123 attempt 1 head {'a' * 40}; 1 snapshots, 0 refreshes, 0 failed API reads", log)
            self.assertNotIn("CI timing failed", log)
            self.assertIn("## CI complete inventory gate unsuccessful", summary_text)
            for error in report["errors"]:
                self.assertIn(f"  - {error}", log)
                self.assertIn(error, summary_text)
            status, log = self.run_gate_main([run, self.http_error(403)], Path(temporary) / "raised.json", summary)
            self.assertEqual(status, 1)
            self.assertIn("CI timing failed: HTTP Error 403", log)
            self.assertNotIn("inventory gate unsuccessful", log)
            self.assertFalse((Path(temporary) / "raised.json").exists())

    def test_read_timeout_falls_back_to_smaller_pages_and_keeps_budget_for_more_snapshots(self):
        # #4102: one 30 s jobs read used the whole 30 s budget. Each read now has
        # a bounded share and a timeout drops to the 5xx page size, as in #1984.
        jobs = self.sample()
        run, args = self.gate_inputs()
        clock = [1000.0]
        timeouts = []
        page = {"total_count": len(jobs), "jobs": jobs[:github_ci_time.JOB_PAGE_FALLBACK_SIZE]}
        responses = [run, TimeoutError("The read operation timed out"),
                     urllib.error.URLError(TimeoutError("timed out")), page]

        def read(repository, path, token, timeout):
            timeouts.append(timeout)
            response = responses.pop(0)
            if isinstance(response, OSError):
                clock[0] += timeout
                raise response
            clock[0] += 0.5
            return response

        def sleep(seconds):
            clock[0] += seconds

        with mock.patch.object(github_ci_time, "api_get", side_effect=read) as fetch, \
                mock.patch.object(github_ci_time.time, "monotonic", side_effect=lambda: clock[0]), \
                mock.patch.object(github_ci_time.time, "sleep", side_effect=sleep):
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["step_proof_waived"], [])
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 3)
        self.assertEqual(result["job_metadata"]["final_page_size"], github_ci_time.JOB_PAGE_FALLBACK_SIZE)
        self.assertEqual(result["job_metadata"]["refresh_budget_seconds"], 30.0)
        self.assertIn("per_page=100&page=1", fetch.call_args_list[1].args[1])
        self.assertIn(f"per_page={github_ci_time.JOB_PAGE_FALLBACK_SIZE}&page=1", fetch.call_args_list[2].args[1])
        self.assertIn(f"per_page={github_ci_time.JOB_PAGE_FALLBACK_SIZE}&page=1", fetch.call_args_list[3].args[1])
        self.assertEqual([lookup["outcome"] for lookup in result["job_metadata"]["lookups"]],
                         ["ok", "TimeoutError: The read operation timed out",
                          "URLError: <urlopen error timed out>", "ok"])
        self.assertTrue(all(timeout <= github_ci_time.JOB_METADATA_READ_TIMEOUT_SECONDS for timeout in timeouts))
        self.assertLessEqual(github_ci_time.JOB_METADATA_READ_TIMEOUT_SECONDS,
                             github_ci_time.JOB_METADATA_REFRESH_BUDGET_SECONDS / 3)
        self.assertEqual(github_ci_time.API_TIMEOUT_SECONDS, 30.0)
        self.assertLess(clock[0] - 1000.0, github_ci_time.JOB_METADATA_REFRESH_BUDGET_SECONDS)

    def test_successful_job_with_empty_or_pending_steps_is_accepted_on_job_conclusion_with_warning(self):
        jobs = self.sample()
        empty = next(job for job in jobs if job["name"] == "Linux x86-64 sanitized-release")
        empty["steps"] = []
        stale = next(job for job in jobs if job["name"] == "macOS AArch64 native")
        stale["steps"][0].update(status="pending", conclusion=None)
        stale["steps"][1].update(status="in_progress", conclusion=None)
        run, _ = self.gate_inputs()
        batch = {"total_count": len(jobs), "jobs": jobs}
        with tempfile.TemporaryDirectory() as temporary:
            output, summary = Path(temporary) / "gate.json", Path(temporary) / "summary.md"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status, log = self.run_gate_main([run] + [batch] * 4, output, summary)
            report = json.loads(output.read_text(encoding="utf-8"))
            summary_text = summary.read_text(encoding="utf-8")
        self.assertEqual(status, 0)
        self.assertTrue(report["success"], report["errors"])
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["step_proof_waived"], ["Linux x86-64 sanitized-release", "macOS AArch64 native"])
        self.assertEqual(report["job_metadata"]["snapshot_attempts"], 4)
        self.assertEqual(report["job_metadata"]["step_proof_waiver_snapshot"], 4)
        for name in report["step_proof_waived"]:
            line = f"CI complete inventory gate: step proof unavailable, accepted on job conclusion: {name}"
            self.assertIn(line + "\n", log)
            self.assertIn(f"::warning title=Step proof unavailable::{line}", stdout.getvalue())
            self.assertIn(f"- {name}", summary_text)
        waived = [job for job in report["jobs"] if job.get("step_proof_waived")]
        self.assertEqual(sorted(job["name"] for job in waived), report["step_proof_waived"])
        self.assertNotIn("inventory gate unsuccessful", log)
        # A later snapshot with full proof needs no waiver.
        with mock.patch.object(github_ci_time, "api_get", side_effect=[run, batch,
                                                                          {"total_count": len(jobs),
                                                                           "jobs": self.sample()}]), \
                mock.patch.object(github_ci_time.time, "sleep"):
            result = github_ci_time.require_jobs(self.gate_inputs()[1])
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["step_proof_waived"], [])

    def test_stale_step_waiver_never_covers_failed_steps_jobs_or_missing_jobs(self):
        run, args = self.gate_inputs()
        target_name = "Windows x86-64 release"

        def gate(jobs):
            batch = {"total_count": len(jobs), "jobs": jobs}
            with mock.patch.object(github_ci_time, "api_get", side_effect=[run] + [batch] * 4), \
                    mock.patch.object(github_ci_time.time, "sleep"):
                return github_ci_time.require_jobs(args)

        def variant(change):
            jobs = self.sample()
            target = next(job for job in jobs if job["name"] == target_name)
            # Every other required step of the target is stale, so only `change` can block the waiver.
            for step in target["steps"]:
                step.update(status="pending", conclusion=None)
            change(jobs, target)
            return jobs

        def failed_required(jobs, target):
            target["steps"][0].update(status="completed", conclusion="failure")

        def skipped_required(jobs, target):
            target["steps"][0].update(status="completed", conclusion="skipped")

        def failed_other_step(jobs, target):
            target["steps"].append({"name": "Set up job", "status": "completed", "conclusion": "failure"})

        def missing_required(jobs, target):
            target["steps"].pop(0)

        def duplicate_required(jobs, target):
            target["steps"].append(dict(target["steps"][0]))

        def completed_without_conclusion(jobs, target):
            target["steps"][0].update(status="completed", conclusion=None)

        def failed_job(jobs, target):
            target.update(conclusion="failure", steps=[])

        def cancelled_job(jobs, target):
            target.update(conclusion="cancelled", steps=[])

        def running_job(jobs, target):
            target.update(status="in_progress", conclusion=None, steps=[])

        def missing_job(jobs, target):
            jobs.remove(target)
            jobs[0]["steps"] = []

        def unknown_job(jobs, target):
            jobs.append(dict(copy.deepcopy(target), id=9999, name="Unknown workload", steps=[]))

        def foreign_job(jobs, target):
            target.update(head_sha="b" * 40, steps=[])

        for change in (failed_required, skipped_required, failed_other_step, missing_required, duplicate_required,
                       completed_without_conclusion, failed_job, cancelled_job, running_job, missing_job,
                       unknown_job, foreign_job):
            with self.subTest(change=change.__name__):
                result = gate(variant(change))
                self.assertFalse(result["success"])
                self.assertEqual(result["step_proof_waived"], [])
                self.assertIsNone(result["job_metadata"]["step_proof_waiver_snapshot"])
        # The pure validator and the strict reuse path keep their exact-step proof.
        stale = variant(lambda jobs, target: None)
        self.assertTrue(self.check(stale))
        self.assertEqual(github_ci_time.step_proof_waivers(stale), [target_name])
        self.assertEqual(github_ci_time.validate_required_jobs(stale, 123, 1, "a" * 40, accept_job_conclusion=True,
                                                               expected_names=github_ci_time.combination_jobs()), [])

    def test_stale_waiver_judges_only_the_newest_complete_snapshot(self):
        jobs = self.sample()
        stale = copy.deepcopy(jobs)
        next(job for job in stale if job["name"] == "Linux AArch64 release")["steps"] = []
        run, args = self.gate_inputs()
        batch = {"total_count": len(jobs), "jobs": stale}
        with mock.patch.object(github_ci_time, "api_get",
                               side_effect=[run, batch] + [self.http_error(502)] * 3), \
                mock.patch.object(github_ci_time.time, "sleep"):
            result = github_ci_time.require_jobs(args)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["step_proof_waived"], ["Linux AArch64 release"])
        self.assertEqual(result["job_metadata"]["step_proof_waiver_snapshot"], 1)
        self.assertEqual(result["job_metadata"]["snapshot_attempts"], 4)
        self.assertEqual(result["raw_jobs"], stale)
        running = copy.deepcopy(stale)
        running[1].update(status="in_progress", conclusion=None)
        with mock.patch.object(github_ci_time, "api_get",
                               side_effect=[run, batch, {"total_count": len(jobs), "jobs": running}] +
                               [self.http_error(502)] * 2), \
                mock.patch.object(github_ci_time.time, "sleep"):
            result = github_ci_time.require_jobs(args)
        self.assertFalse(result["success"])
        self.assertEqual(result["step_proof_waived"], [])

    def lost_runner_job(self, jobs):
        target = next(job for job in jobs if job["name"] == "macOS AArch64 sanitized-release")
        target.update(conclusion="failure", started_at="2026-09-28T15:17:35Z", completed_at="2026-09-28T16:04:37Z",
                      runner_name="GitHub Actions 1000119276", labels=["macos-26"])
        target["steps"] = [
            {"name": "Install verified Zig", "status": "completed", "conclusion": "success",
             "started_at": "2026-09-28T15:17:51Z", "completed_at": "2026-09-28T15:18:13Z"},
            {"name": "Combination matrix (Linux, macOS)", "status": "in_progress", "conclusion": None,
             "started_at": "2026-09-28T15:18:19Z", "completed_at": None},
            {"name": "Desktop result and reproduction", "status": "pending", "conclusion": None,
             "started_at": None, "completed_at": None},
            {"name": "Retain desktop logs", "status": "pending", "conclusion": None,
             "started_at": None, "completed_at": None}]
        return target

    def test_interruption_evidence_classifies_only_api_visible_facts(self):
        job = self.lost_runner_job(self.sample())
        lost = [{"message": github_ci_time.RUNNER_LOST_MESSAGE + " Anything in your workflow ..."}]
        record = github_ci_time.interruption_evidence(job, lost)
        self.assertEqual(record["classification"], "runner-communication-lost")
        self.assertEqual(record["active_steps"], [{"name": "Combination matrix (Linux, macOS)",
                                                   "started_at": "2026-09-28T15:18:19Z"}])
        self.assertEqual(record["pending_steps"], 2)
        self.assertEqual(record["last_progress_at"], "2026-09-28T15:18:19Z")
        self.assertEqual(record["silent_seconds"], 2778.0)
        self.assertEqual(record["runner_name"], "GitHub Actions 1000119276")
        self.assertEqual(github_ci_time.interruption_evidence(job, [{"message": "Process completed with exit code 1."}])
                         ["classification"], "unterminated-step")
        unavailable = github_ci_time.interruption_evidence(job, None, "annotation read failed: HTTP 403")
        self.assertEqual(unavailable["classification"], "annotation-unavailable")
        self.assertIsNone(unavailable["annotations"])
        job["completed_at"] = None
        self.assertIsNone(github_ci_time.interruption_evidence(job, lost)["silent_seconds"])
        job["conclusion"] = "cancelled"
        self.assertIsNone(github_ci_time.interruption_evidence(job, lost))
        ordinary = self.sample()[0]
        ordinary["conclusion"] = "failure"
        self.assertIsNone(github_ci_time.interruption_evidence(ordinary, lost))

    def test_gate_retains_interruption_evidence_without_changing_its_verdict(self):
        jobs = self.sample()
        target = self.lost_runner_job(jobs)
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": "push"}
        args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1)
        batch = {"total_count": len(jobs), "jobs": jobs}
        lost = [{"message": github_ci_time.RUNNER_LOST_MESSAGE}]
        for annotations, classification in ((lost, "runner-communication-lost"),
                                            (OSError("HTTP 403"), "annotation-unavailable"),
                                            ({"message": "unexpected"}, "annotation-unavailable")):
            with mock.patch.object(github_ci_time, "api_get", side_effect=[run, batch, annotations]) as fetch, \
                    mock.patch.object(github_ci_time.time, "sleep") as sleep, \
                    mock.patch("sys.stderr") as stderr:
                result = github_ci_time.require_jobs(args)
            self.assertFalse(result["success"])
            sleep.assert_not_called()
            self.assertEqual(fetch.call_count, 3)
            self.assertIn(f"check-runs/{target['id']}/annotations", fetch.call_args_list[2].args[1])
            self.assertTrue(any("required job did not complete successfully" in error for error in result["errors"]))
            record = next(job for job in result["jobs"] if job["name"] == "macOS AArch64 sanitized-release")["interruption"]
            self.assertEqual(record["classification"], classification)
            self.assertIn("CI_RUNNER_INTERRUPTION", "".join(call.args[0] for call in stderr.write.call_args_list))
            self.assertTrue(all("interruption" not in job for job in result["jobs"] if job["name"] != target["name"]))

    def test_workflow_expands_exact_cross_product_and_keeps_gate_wiring(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        lanes = re.search(r"^        lane: \[([^]]+)\]$", desktop, re.M).group(1).split(", ")
        shard_expression = re.search(r"^        shard: \$\{\{ fromJSON\((.+)\) \}\}$", desktop, re.M).group(1)
        exclude_expression = re.search(r"^        exclude: \$\{\{ fromJSON\((.+)\) \}\}$", desktop, re.M).group(1)
        shard_variants = [json.loads(value) for value in re.findall(r"'([^']*)'", shard_expression) if value.startswith("[")]
        exclude_variants = [json.loads(value) for value in re.findall(r"'([^']*)'", exclude_expression) if value.startswith("[")]
        self.assertEqual(len(shard_variants), 2)
        self.assertEqual(len(exclude_variants), 2)
        dispatch_guard = "github.event_name == 'workflow_dispatch' && (" + " || ".join(
            "github.ref == 'refs/heads/" + branch + "'" for branch in github_ci_time.COMBINED_QUALIFICATION_BRANCHES) + ")"
        self.assertIn(dispatch_guard, shard_expression)
        self.assertIn(dispatch_guard, exclude_expression)
        for expression in (shard_expression, exclude_expression):
            self.assertRegex(expression, re.escape(dispatch_guard) + r" && '\[[^']*\]' \|\| '\[[^']*\]'\Z")
        includes = re.findall(r"^          - name: (.+)\n            lane: (.+)\n", desktop, re.M)
        self.assertEqual(Counter(lanes), Counter(f"{platform}-{arch}" for platform, arch in ci_summary._COVERAGE_POLICY_ANCHORS if (platform, arch) != ("macos", "x86_64")))
        self.assertEqual(Counter(lane for _, lane in includes), Counter(lanes))
        self.assertEqual(Counter(shard_variants[0]), Counter(github_ci_time.COMBINATION_SHARDS))
        self.assertEqual(exclude_variants[0], [])
        for variant, expected in ((0, github_ci_time.COMBINATION_PLATFORMS),
                                  (1, github_ci_time.SPLIT_COMBINATION_PLATFORMS)):
            expanded = [f"{name} {shard}" for name, lane in includes for shard in shard_variants[variant]
                        if {"lane": lane, "shard": shard} not in exclude_variants[variant]]
            self.assertEqual(Counter(expanded), Counter(expected))
        self.assertIn("name: ${{ matrix.name }} ${{ matrix.shard }}", desktop)
        self.assertIn("BUSTER_MATRIX_SHARD: ${{ matrix.shard }}", desktop)
        self.assertIn("matrix.shard == 'release'", desktop)
        self.assertIn("name: desktop-${{ matrix.os }}-${{ matrix.arch }}-${{ matrix.shard }}-", desktop)
        native = workflow.split("\n  native:", 1)[1].split("\n  uefi:", 1)[0]
        native_names = re.findall(r"^          - name: (.+ native)$", native, re.M)
        self.assertEqual(Counter(native_names), Counter(github_ci_time.NATIVE))
        self.assertIn("Execution-mode matrix (Windows)", native)
        self.assertIn("test_mode_matrix --config Release", native)
        aggregate = workflow.split("\n  complete:", 1)[1]
        self.assertIn("actions: read", aggregate)
        self.assertIn("checks: read", aggregate)
        self.assertIn("github_ci_time.py require-jobs", aggregate)
        self.assertIn("Verify every desktop partition exists", aggregate)
        self.assertIn("needs: [lint, queue_lint, test, native, uefi, analyzer, reuse, no_code_plan]", aggregate)
        self.assertIn('--checks-layout "$BUSTER_CI_CHECKS_LAYOUT"', aggregate)

    def test_default_split_keeps_only_combined_and_barrier_dispatch_overrides(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        inputs = workflow.split("  workflow_dispatch:", 1)[1].split("\n# Supersede", 1)[0]
        self.assertNotIn("      checks_layout:\n", inputs)
        self.assertNotIn("      test_admission:\n", inputs)
        guard = "github.event_name == 'workflow_dispatch' && (" + " || ".join(
            "github.ref == 'refs/heads/" + branch + "'" for branch in github_ci_time.COMBINED_QUALIFICATION_BRANCHES) + ")"
        self.assertIn("BUSTER_CI_CHECKS_LAYOUT: ${{ " + guard + " && 'combined' || 'split' }}", workflow)
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        windows = desktop.split("      - name: Combination matrix (Windows)\n", 1)[1].split("      - name:", 1)[0]
        admission = re.search(r"^          BUSTER_MATRIX_TEST_ADMISSION: (.+)$", windows, re.M).group(1)
        self.assertEqual(workflow.count("BUSTER_MATRIX_TEST_ADMISSION:"), 1)
        self.assertEqual(admission, "${{ github.event_name == 'workflow_dispatch' && matrix.arch == 'x86_64' && "
                         "matrix.shard == 'checks' && (github.ref == 'refs/heads/codex/ci-checks-combined-all-builds' || "
                         "github.ref == 'refs/heads/codex/2120-evidence-v2-combined-all-builds') && 'all-builds' || 'overlap' }}")
        capture = ("${{ github.event_name == 'workflow_dispatch' && "
                   "(github.ref == 'refs/heads/codex/ci-checks-combined-overlap' || "
                   "github.ref == 'refs/heads/codex/ci-checks-combined-all-builds' || "
                   "github.ref == 'refs/heads/codex/ci-checks-split-overlap' || "
                   "github.ref == 'refs/heads/codex/2120-evidence-v2-combined-overlap' || "
                   "github.ref == 'refs/heads/codex/2120-evidence-v2-combined-all-builds' || "
                   "github.ref == 'refs/heads/codex/2120-evidence-v2-split-overlap') && '1' || '0' }}")
        self.assertEqual(workflow.count("BUSTER_CI_CHECKS_EVIDENCE:"), 2)
        for title in ("Combination matrix (Linux, macOS)", "Combination matrix (Windows)"):
            step = desktop.split(f"      - name: {title}\n", 1)[1].split("      - name:", 1)[0]
            observed = re.search(r"^          BUSTER_CI_CHECKS_EVIDENCE: (.+)$", step, re.M).group(1)
            self.assertEqual(observed, capture)
        self.assertEqual(re.search(r"^  BUSTER_CI_CONDITIONS_EVIDENCE: (.+)$", workflow, re.M).group(1), capture)
        self.assertEqual(workflow.count("BUSTER_CI_CONDITIONS_EVIDENCE:"), 13)
        self.assertEqual(re.findall(r"^          BUSTER_CI_CONDITIONS_EVIDENCE: (.+)$", workflow, re.M),
                         ["${{ env.BUSTER_CI_CONDITIONS_EVIDENCE }}"] * 12)

    def timing_sample(self, checks_layout="combined", names=None):
        jobs = self.sample(checks_layout, names)
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
        return run

    def test_timing_includes_every_new_shard_and_rejects_partial_runs(self):
        for layout, job_count in (("combined", 19), ("historical-split", 27), ("macos-split", 29), ("split", 23)):
            with self.subTest(layout=layout):
                run = (self.historical_split_timing_sample() if layout == "historical-split" else
                       self.macos_split_timing_sample() if layout == "macos-split" else self.timing_sample(layout))
                measurement, reason = github_ci_time.measure(run)
                self.assertIsNone(reason)
                self.assertEqual(measurement["runner_seconds"], job_count * 60)
                self.assertEqual(measurement["elapsed_seconds"], 70)
                self.assertEqual(set(measurement["job_queue_seconds"].values()), {5})
                for index in range(len(run["jobs"])):
                    bad = copy.deepcopy(run)
                    bad["jobs"].pop(index)
                    self.assertIsNone(github_ci_time.measure(bad)[0])
                self.assertIsNone(github_ci_time.measure(dict(run, run_attempt=2))[0])

    def historical_split_timing_sample(self):
        # The pre-#2659 27-job layout: macOS AArch64 still had grouped checks.
        run = self.timing_sample("split", github_ci_time.HISTORICAL_SPLIT_COMBINATION_JOBS)
        self.assertEqual(sum(job["name"].endswith(" sanitized-debug") for job in run["jobs"]), 3)
        self.assertTrue(self.check(run["jobs"], checks_layout="split"))
        return run

    def macos_split_timing_sample(self):
        # The pre-#2657 29-job layout: a full-runtime sanitized-debug job on
        # each split platform. A timing cohort only, never current evidence.
        run = self.timing_sample("split", github_ci_time.MACOS_SPLIT_COMBINATION_JOBS)
        self.assertEqual(sum(job["name"].endswith(" sanitized-debug") for job in run["jobs"]), 4)
        self.assertTrue(self.check(run["jobs"], checks_layout="split"))
        return run

    def test_combined_and_split_timing_cohorts_stay_separate_on_the_same_workflow_blob(self):
        combined = self.timing_sample()
        historical = self.historical_split_timing_sample()
        historical["id"] = 125
        macos_split = self.macos_split_timing_sample()
        macos_split["id"] = 126
        split = self.timing_sample("split")
        split["id"] = 124
        summary = github_ci_time.summarize({"runs": [combined, historical, macos_split, split]})
        self.assertEqual(summary["excluded"], {})
        self.assertEqual(len(summary["cohorts"]), 4)
        self.assertEqual({row["n"] for row in summary["cohorts"]}, {1})
        self.assertEqual({row["medians"]["runner_seconds"] for row in summary["cohorts"]},
                         {19 * 60, 27 * 60, 29 * 60, 23 * 60})


class DraftMacosDeferralTests(unittest.TestCase):
    """#1825: draft pull requests defer macOS runners; nothing else may."""

    PREDICATE = ("github.event_name == 'pull_request' && github.event.pull_request.draft && "
                 "github.run_attempt == '1' && startsWith(matrix.runner, 'macos-')")

    def sample(self, deferred=True):
        jobs = CompletionGateTests.sample(self)
        if deferred:
            for job in jobs:
                if job["name"] in github_ci_time.MACOS_RUNNER_JOBS:
                    job["name"] += github_ci_time.DEFERRED_SUFFIX
                    job["steps"] = [{"name": github_ci_time.DEFERRAL_STEP, "status": "completed",
                                     "conclusion": "success"}]
        return jobs

    def check(self, jobs, draft, attempt=1):
        return github_ci_time.validate_required_jobs(jobs, 123, attempt, "a" * 40, draft,
                                                     expected_names=github_ci_time.combination_jobs())

    def workflow_jobs(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        return {
            "test": workflow.split("\n  test:\n", 1)[1].split("\n  native:\n", 1)[0],
            "native": workflow.split("\n  native:\n", 1)[1].split("\n  uefi:\n", 1)[0],
        }

    def test_exactly_the_four_macos_runner_jobs_are_deferrable(self):
        jobs = self.workflow_jobs()
        expected = []
        desktop = re.findall(r"^          - name: (.+)\n            lane: .+\n            runner: (.+)$", jobs["test"], re.M)
        expected += [f"{name} {shard}" for name, runner in desktop if runner.startswith("macos-")
                     for shard in ("release",) + github_ci_time.SPLIT_CHECK_SHARDS]
        for job in ("native",):
            entries = re.findall(r"^          - name: (.+)\n            runner: (.+)$", jobs[job], re.M)
            expected += [name for name, runner in entries if runner.startswith("macos-")]
        self.assertEqual(len(expected), 4)
        self.assertEqual(Counter(expected), Counter(github_ci_time.MACOS_RUNNER_JOBS))

    def test_first_attempt_draft_accepts_deferred_macos_lanes_only(self):
        self.assertEqual(self.check(self.sample(), draft=True), [])
        self.assertEqual(self.check(self.sample(deferred=False), draft=True), [])
        self.assertEqual(self.check(self.sample(deferred=False), draft=False), [])
        errors = self.check(self.sample(), draft=False)
        self.assertEqual(sum("only the first attempt of a draft pull-request run" in error for error in errors), 4)
        # A cancelled no-op rerun by rerun-failed-jobs runs the real lane
        # instead; a deferral record from a later attempt is never accepted.
        jobs = self.sample()
        for job in jobs:
            if job["name"] in ("CI complete", "macOS AArch64 native" + github_ci_time.DEFERRED_SUFFIX):
                job["run_attempt"] = 2
        errors = self.check(jobs, draft=True, attempt=2)
        self.assertEqual(len(errors), 1)
        self.assertIn("macOS AArch64 native (deferred for draft PR): only the first attempt", errors[0])
        for job in jobs:
            job["run_attempt"] = 1 if job["name"] != "CI complete" else 2
        self.assertEqual(self.check(jobs, draft=True, attempt=2), [])

    def test_deferral_needs_its_successful_step_and_a_macos_identity(self):
        for mutate in ("fail", "drop", "linux", "duplicate"):
            with self.subTest(mutate=mutate):
                jobs = self.sample()
                target = next(job for job in jobs if job["name"].startswith("macOS AArch64 native"))
                if mutate == "fail":
                    target["steps"][0]["conclusion"] = "failure"
                elif mutate == "drop":
                    target["steps"] = []
                elif mutate == "linux":
                    linux = next(job for job in jobs if job["name"] == "Linux AArch64 native")
                    linux["name"] += github_ci_time.DEFERRED_SUFFIX
                else:
                    jobs.append(dict(copy.deepcopy(target), name="macOS AArch64 native"))
                self.assertTrue(self.check(jobs, draft=True))

    def test_rerun_all_jobs_replaces_the_deferral_with_the_real_lane(self):
        first = self.sample()
        second = [dict(copy.deepcopy(job), id=job["id"] + 100, run_attempt=2) for job in self.sample(deferred=False)]
        latest = github_ci_time.latest_run_jobs(first + second, 123, 2, "a" * 40)
        self.assertFalse(any(github_ci_time.deferred_base_name(job["name"]) for job in latest))
        self.assertEqual(self.check(latest, draft=True, attempt=2), [])
        failed = next(job for job in second if job["name"] == "macOS AArch64 sanitized-release")
        failed["conclusion"] = "failure"
        latest = github_ci_time.latest_run_jobs(first + second, 123, 2, "a" * 40)
        self.assertTrue(self.check(latest, draft=True, attempt=2))
        deferred = next(job for job in first if github_ci_time.deferred_base_name(job["name"]))
        clash = dict(copy.deepcopy(deferred), id=999, name=github_ci_time.deferred_base_name(deferred["name"]))
        with self.assertRaises(ValueError):
            github_ci_time.latest_run_jobs(first + [clash], 123, 1, "a" * 40)

    def rerun_failed_inventory(self, attempts, deferred=True):
        """Run 36717332363's shape (#2052): attempt 1's CI complete failed, then
        "Re-run failed jobs" re-stamped every retained success under each later
        attempt with a new id but the original timing and steps."""
        first = self.sample(deferred)
        for number, job in enumerate(first):
            job.update(started_at=f"2026-09-30T12:{number:02}:04Z", completed_at=f"2026-09-30T12:{number:02}:09Z")
            for step in job["steps"]:
                step.update(started_at=job["started_at"], completed_at=job["completed_at"])
            if job["name"] == "CI complete":
                job.update(status="completed", conclusion="failure", steps=[])
        inventory = list(first)
        for attempt in range(2, attempts + 1):
            for job in first:
                copied = dict(copy.deepcopy(job), id=job["id"] + 1000 * attempt, run_attempt=attempt)
                if job["name"] == "CI complete":
                    copied.update(status="in_progress" if attempt == attempts else "completed",
                                  conclusion=None if attempt == attempts else "failure",
                                  started_at=f"2026-09-30T{12 + attempt}:00:00Z", completed_at=None)
                inventory.append(copied)
        return inventory

    def gate_rerun(self, inventory, attempts, draft):
        payload = {"pull_request": {"draft": draft, "head": {"sha": "a" * 40}}}
        run = {"id": 123, "run_attempt": attempts, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40,
               "event": "pull_request"}
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "event.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=attempts,
                                   event_name="pull_request", event_path=str(path))
            snapshots = len(github_ci_time.JOB_METADATA_REFRESH_DELAYS_SECONDS) + 1
            responses = [run] + [{"total_count": len(inventory), "jobs": inventory}] * snapshots
            with mock.patch.object(github_ci_time, "api_get", side_effect=responses), \
                    mock.patch.object(github_ci_time.time, "sleep"):
                result = github_ci_time.require_jobs(args)
        return result

    def test_rerun_failed_jobs_carries_attempt_one_deferrals_forward(self):
        # #2052: the re-stamped copies are judged as attempt 1 judged the originals.
        for attempts in (2, 3):
            with self.subTest(attempts=attempts):
                inventory = self.rerun_failed_inventory(attempts)
                latest = github_ci_time.latest_run_jobs(inventory, 123, attempts, "a" * 40)
                deferred = [job for job in latest if github_ci_time.deferred_base_name(job["name"])]
                self.assertEqual(len(deferred), 4)
                self.assertEqual({job["run_attempt"] for job in deferred}, {1})
                self.assertEqual(self.check(latest, draft=True, attempt=attempts), [])
                result = self.gate_rerun(inventory, attempts, draft=True)
                self.assertTrue(result["success"], result["errors"])
                self.assertEqual(result["deferred_macos_jobs"], sorted(github_ci_time.MACOS_RUNNER_JOBS))
                # Non-draft control: the same carried deferrals never substitute for macOS.
                result = self.gate_rerun(inventory, attempts, draft=False)
                self.assertFalse(result["success"])
                self.assertEqual(sum("only the first attempt of a draft pull-request run" in error
                                     for error in result["errors"]), 4)
        # Non-draft control without deferrals: an ordinary partial rerun still passes.
        result = self.gate_rerun(self.rerun_failed_inventory(2, deferred=False), 2, draft=False)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["deferred_macos_jobs"], [])

    def test_rerun_failed_jobs_still_fails_closed(self):
        def target(inventory, attempt):
            return next(job for job in inventory if job["run_attempt"] == attempt
                        and job["name"] == "macOS AArch64 native" + github_ci_time.DEFERRED_SUFFIX)
        for mutate in ("reran", "failed", "cancelled", "skipped", "step", "untimed", "gap", "linux"):
            with self.subTest(mutate=mutate):
                inventory = self.rerun_failed_inventory(3)
                if mutate == "reran":
                    # A deferral that executed again in a later attempt is not a copy.
                    target(inventory, 3).update(started_at="2026-09-30T20:00:00Z", completed_at="2026-09-30T20:00:05Z")
                elif mutate in ("failed", "cancelled", "skipped"):
                    for attempt in (1, 2, 3):
                        target(inventory, attempt)["conclusion"] = "failure" if mutate == "failed" else mutate
                elif mutate == "step":
                    target(inventory, 3)["steps"][0]["conclusion"] = "skipped"
                elif mutate == "untimed":
                    for attempt in (1, 2, 3):
                        target(inventory, attempt).update(started_at=None, completed_at=None)
                elif mutate == "gap":
                    # Attempt 2 carried a different record; attempt 3 cannot hide it.
                    target(inventory, 2)["completed_at"] = "2026-09-30T13:30:00Z"
                else:
                    linux = [job for job in inventory if job["name"] == "Linux AArch64 native"]
                    for job in linux:
                        job["name"] += github_ci_time.DEFERRED_SUFFIX
                latest = github_ci_time.latest_run_jobs(inventory, 123, 3, "a" * 40)
                self.assertTrue(self.check(latest, draft=True, attempt=3))
                self.assertFalse(self.gate_rerun(inventory, 3, draft=True)["success"])
        # A non-deferred failure retained from attempt 1 is not rescued either.
        inventory = self.rerun_failed_inventory(2)
        for job in inventory:
            if job["name"] == "Linux x86-64 release":
                job["conclusion"] = "failure"
        self.assertFalse(self.gate_rerun(inventory, 2, draft=True)["success"])

    def gate(self, payload, event="pull_request", event_name="pull_request", deferred=True):
        jobs = self.sample(deferred)
        run = {"id": 123, "run_attempt": 1, "path": ".github/workflows/ci.yml", "head_sha": "a" * 40, "event": event}
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "event.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            args = SimpleNamespace(repository="buster14a/buster", run_id=123, run_attempt=1,
                                   event_name=event_name, event_path=str(path))
            with mock.patch.object(github_ci_time, "api_get", side_effect=[run, {"total_count": len(jobs), "jobs": jobs}]):
                result = github_ci_time.require_jobs(args)
        return result

    def test_gate_binds_the_draft_flag_to_this_runs_own_payload_and_head(self):
        draft = {"pull_request": {"draft": True, "head": {"sha": "a" * 40}}}
        result = self.gate(draft)
        self.assertTrue(result["success"], result["errors"])
        self.assertTrue(result["draft_pull_request"])
        self.assertEqual(result["deferred_macos_jobs"], sorted(github_ci_time.MACOS_RUNNER_JOBS))
        ready = {"pull_request": {"draft": False, "head": {"sha": "a" * 40}}}
        for payload, event, event_name in (
                (ready, "pull_request", "pull_request"),
                ({"pull_request": {"draft": True, "head": {"sha": "b" * 40}}}, "pull_request", "pull_request"),
                ({"pull_request": {"draft": "true", "head": {"sha": "a" * 40}}}, "pull_request", "pull_request"),
                (draft, "merge_group", "merge_group"),
                (draft, "merge_group", "pull_request"),
                (draft, "push", "push"),
                ({}, "pull_request", "pull_request")):
            with self.subTest(event=event, event_name=event_name, payload=payload):
                result = self.gate(payload, event, event_name)
                self.assertFalse(result["success"])
                self.assertFalse(result["draft_pull_request"])
        result = self.gate(ready, deferred=False)
        self.assertTrue(result["success"], result["errors"])
        self.assertEqual(result["deferred_macos_jobs"], [])

    def test_deferred_runs_are_never_timing_samples(self):
        jobs = self.sample()
        for job in jobs:
            job.update(status="completed", conclusion="success")
        run = {"id": 123, "head_sha": "a" * 40, "workflow_blob_sha": "b" * 40, "run_attempt": 1,
               "status": "completed", "conclusion": "success", "jobs": jobs}
        self.assertEqual(github_ci_time.measure(run), (None, "incomplete-or-different-matrix"))

    def test_deferrals_are_reported_on_the_pull_request(self):
        with tempfile.TemporaryDirectory() as temporary:
            summary = Path(temporary) / "summary.md"
            data = {"success": True, "deferred_macos_jobs": ["iOS AArch64", "macOS AArch64 native"]}
            with mock.patch("builtins.print") as printed:
                github_ci_time.report_deferrals(data, str(summary), notice=True)
            self.assertIn("::notice title=macOS lanes deferred::", printed.call_args.args[0])
            text = summary.read_text(encoding="utf-8")
            self.assertIn("(accepted)", text)
            self.assertIn("- macOS AArch64 native", text)
            unrelated = dict(data, success=False, errors=["Linux x86-64 release: required job did not complete"])
            with mock.patch("builtins.print") as printed:
                github_ci_time.report_deferrals(unrelated, None, notice=True)
            self.assertIn("(accepted by CI complete)", printed.call_args.args[0])
            rejected = dict(unrelated, errors=["macOS AArch64 native" + github_ci_time.DEFERRED_SUFFIX + ": only the first attempt"])
            with mock.patch("builtins.print") as printed:
                github_ci_time.report_deferrals(rejected, None, notice=True)
            self.assertIn("(not accepted by CI complete)", printed.call_args.args[0])
            with mock.patch("builtins.print") as printed:
                github_ci_time.report_deferrals({"success": True, "deferred_macos_jobs": []}, str(summary), notice=True)
            printed.assert_not_called()

    @staticmethod
    def skips_in_a_deferred_lane(condition):
        """A top-level conjunct needs an earlier step's success or a non-macOS lane."""
        expression = condition.strip()
        if expression.startswith("${{") and expression.endswith("}}"):
            expression = expression[3:-2]
        previous = None
        while previous != expression:
            previous, expression = expression, re.sub(r"\([^()]*\)", "", expression)
        conjuncts = [] if "||" in expression else [part.strip() for part in expression.split("&&")]
        return any(re.fullmatch(r"steps\.\w+\.outcome == 'success'|matrix\.os == '(?:android|linux)'|"
                                r"matrix\.platform == 'windows'", part) for part in conjuncts)

    def test_every_step_after_checkout_skips_in_a_deferred_lane(self):
        self.assertTrue(self.skips_in_a_deferred_lane(
            "${{ !cancelled() && steps.zig.outcome == 'success' && (matrix.os == 'macos' || steps.llvm.outcome == 'success') }}"))
        for condition in ("${{ !cancelled() && steps.pack.outcome != 'success' }}", "${{ matrix.os == 'ios' }}",
                          "${{ always() || steps.checkout.outcome == 'success' }}", "always()"):
            self.assertFalse(self.skips_in_a_deferred_lane(condition), condition)
        for job, text in self.workflow_jobs().items():
            steps = re.findall(r"(?ms)^      - (?:name: ([^\n]+)|uses: [^\n]+)\n(.*?)(?=^      - |\Z)", text)
            self.assertEqual([name for name, _ in steps[:4]], [
                "Machine specifications", github_ci_time.DEFERRAL_STEP, "Checkout",
                "Record actual checkout identity"])
            # Every allocated runner reports before draft deferral; source
            # identity follows the exact checkout predicate and cannot run on
            # the cheap Linux deferral host for a macOS workload.
            self.assertNotRegex(steps[0][1], r"(?m)^        if:")
            self.assertIn("uses: buster14a/buster/.github/actions/machine-specifications@", steps[0][1])
            checkout_guard = re.search(r"^        if: (.+)$", steps[2][1], re.M).group(1)
            source_guard = re.search(r"^        if: (.+)$", steps[3][1], re.M).group(1)
            self.assertEqual(source_guard, checkout_guard)
            self.assertEqual(checkout_guard, "${{ !startsWith(matrix.runner, 'macos-') || runner.os == 'macOS' }}")
            self.assertIn("mode: source", steps[3][1])
            for step_name, body in steps[4:]:
                with self.subTest(job=job, step=step_name):
                    condition = re.search(r"^        if: (.+)$", body, re.M)
                    self.assertIsNotNone(condition)
                    if step_name == "Retain unpacked native logs":
                        # tests/ci_tools_test.py pins this condition. In a
                        # deferred lane it finds no files and uploads nothing.
                        self.assertEqual(condition.group(1), "${{ !cancelled() && steps.pack.outcome != 'success' }}")
                        self.assertIn("if-no-files-found: ignore", body)
                        continue
                    self.assertTrue(self.skips_in_a_deferred_lane(condition.group(1)), condition.group(1))

    def test_workflow_defers_only_macos_runners_on_first_attempt_draft_runs(self):
        for job, text in self.workflow_jobs().items():
            with self.subTest(job=job):
                self.assertIn(f"    runs-on: ${{{{ {self.PREDICATE} && 'ubuntu-26.04' || matrix.runner }}}}\n", text)
                name = re.search(r"^    name: (.+)$", text, re.M).group(1)
                self.assertTrue(name.endswith(f"${{{{ {self.PREDICATE} && '{github_ci_time.DEFERRED_SUFFIX}' || '' }}}}"))
                # Only main-push reuse and trusted no-code planning gate these
                # lanes; merge-group lint runs beside desktop (#3912).
                self.assertEqual(re.findall(r"^    needs: .*$", text, re.M), ["    needs: [reuse, no_code_plan]"])
                step = text.split(f"      - name: {github_ci_time.DEFERRAL_STEP}\n", 1)[1].split("\n      - name:", 1)[0]
                self.assertIn("if: ${{ startsWith(matrix.runner, 'macos-') && runner.os != 'macOS' }}", step)
                self.assertIn("DEFERRAL_AUTHORIZED: ${{ github.event_name == 'pull_request' && "
                              "github.event.pull_request.draft && github.run_attempt == '1' }}", step)
                self.assertIn('if [[ "$DEFERRAL_AUTHORIZED" != true ]]; then', step)
                self.assertIn("exit 1", step)
                self.assertIn("if: ${{ !startsWith(matrix.runner, 'macos-') || runner.os == 'macOS' }}",
                              text.split("      - name: Checkout\n", 1)[1].split("\n      - name:", 1)[0])


if __name__ == "__main__":
    unittest.main()
