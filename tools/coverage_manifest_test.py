#!/usr/bin/env python3
"""End-to-end tests for the driver-owned desktop coverage contract."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT / "tools"))
import ci_summary


class CoverageManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Windows can retain a just-executed fixture image briefly (notably on
        # ARM64 hosted runners).  The runner-owned parent still scopes it, but
        # cleanup must not turn a completed portability probe into a failure.
        cls.temporary = tempfile.TemporaryDirectory(ignore_cleanup_errors=os.name == "nt")
        cls.root = Path(cls.temporary.name)
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            raise unittest.SkipTest("gcc or clang is required for the C producer/JSON consumer test")
        cls.compiler = compiler
        cls.driver = cls.root / ("coverage-driver.exe" if os.name == "nt" else "coverage-driver")
        command = [compiler, "-Isrc", "-I.", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "build.c"]
        if os.name == "nt":
            command.append("-lws2_32")
        command += ["-o", str(cls.driver)]
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def produce(self):
        output = self.root / "coverage.json"
        environment = os.environ.copy()
        environment["BUSTER_CI_COVERAGE_OUTPUT"] = str(output)
        result = subprocess.run([str(self.driver), "coverage_manifest_self_test"], cwd=ROOT, env=environment,
                                capture_output=True, text=True, check=True, timeout=60)
        self.assertIn("policy_lanes=6", result.stdout)
        return json.loads(output.read_text(encoding="utf-8"))

    def consumer_environment(self, manifest):
        return {
            "BUSTER_CI_COVERAGE_EXPECTED_DRIVER_PATH": manifest["identity"]["driver_path"],
            "BUSTER_CI_COVERAGE_PLATFORM": manifest["identity"]["platform"],
            "BUSTER_CI_COVERAGE_ARCH": manifest["identity"]["architecture"],
        }

    def test_c_producer_is_consumed_and_summary_retains_identities(self):
        manifest = self.produce()
        consumer_environment = self.consumer_environment(manifest)
        self.assertEqual(ci_summary.validate_coverage_manifest(manifest, consumer_environment, expected_mode="self-test"), [])
        self.assertEqual(manifest["policy"]["version"], 1)
        self.assertEqual(len(manifest["policy"]["fingerprint"]), 16)
        environment = {"RUNNER_TEMP": str(self.root), "BUSTER_CI_STEPS": '{"matrix":{"outcome":"success"}}',
                       "BUSTER_CI_REQUIRED": "matrix", "BUSTER_CI_COVERAGE_OUTPUT": str(self.root / "coverage.json"),
                       "BUSTER_CI_COVERAGE_REQUIRED": "1", **consumer_environment}
        self.assertEqual(ci_summary.write_report(environment, expected_coverage_mode="self-test"), 0)
        summary = (self.root / "buster-ci" / "summary.md").read_text(encoding="utf-8")
        for row in manifest["expected"]:
            self.assertIn(row["id"], summary)
            if row["exclusion"]:
                self.assertIn(row["exclusion"], summary)
        self.assertIn("Lane identity:", summary)
        self.assertIn("Source identity:", summary)
        self.assertIn("Driver identity:", summary)
        self.assertIn("Lane obligation reasons:", summary)
        self.assertIn("Completion evidence: driver-complete", summary)

    def test_msvc_probe_uses_source_bearing_no_object_argv(self):
        directory = Path(tempfile.mkdtemp(dir=self.root))
        fake_cl = directory / "cl.exe"
        source = directory / "fake_cl.c"
        source.write_text('#include <stdio.h>\n#include <string.h>\n'
                          'int main(int argc, char **argv) {\n'
                          '  const char *expected[] = {"/Bv", "/EP", "/TC", "tests/build_compiler_identity.h"};\n'
                          '  if (argc != 5) return 86;\n'
                          '  for (int i = 0; i != 4; ++i) if (strcmp(argv[i + 1], expected[i])) return 86;\n'
                          '  fputs("preprocessed identity must not win\\n", stdout);\n'
                          '  fputs("Microsoft (R) C/C++ Optimizing Compiler Version 19.42\\r\\n", stderr);\n'
                          '  fputs("C:\\\\Path-With-Case\\\\cl.exe: Version 19.42.0\\r\\n", stderr);\n'
                          '  return 0;\n}\n', encoding="utf-8")
        subprocess.run([self.compiler, str(source), "-o", str(fake_cl)], check=True, timeout=60)
        probe = ci_summary._coverage_probe_compiler(fake_cl, "cl", {"VSCMD_ARG_TGT_ARCH": "x64"})
        self.assertEqual(probe, {"identity": "BUSTER_BUILD_COMPILER_MSVC", "target": "x64",
                                 "version": "Microsoft (R) C/C++ Optimizing Compiler Version 19.42"})

    def test_negative_controls_are_fail_closed(self):
        manifest = self.produce()
        # Bind expectations to the original fixture, never to a mutated candidate.
        consumer_environment = self.consumer_environment(manifest)
        controls = []

        for phase in ("planned", "stale"):
            stale = copy.deepcopy(manifest)
            stale["phase"] = phase
            controls.append(("stale", stale, 'coverage manifest is not complete'))

        for status in ("skipped", "cancelled", "failure"):
            incomplete = copy.deepcopy(manifest)
            incomplete["executed"][0]["status"] = status
            controls.append(("incomplete", incomplete, 'coverage execution record is not a successful lane completion'))

        duplicate_completion = copy.deepcopy(manifest)
        duplicate_completion["executed"].append(copy.deepcopy(duplicate_completion["executed"][0]))
        controls.append(("duplicate_completion", duplicate_completion, 'coverage execution must contain exactly one lane completion record'))

        duplicate_row = copy.deepcopy(manifest)
        duplicate_row["expected"].append(copy.deepcopy(duplicate_row["expected"][0]))
        controls.append(("duplicate_row", duplicate_row, 'coverage expected row identity is missing or duplicated'))

        duplicate_detected = copy.deepcopy(manifest)
        duplicate_detected["detected"].append(copy.deepcopy(duplicate_detected["detected"][0]))
        controls.append(("duplicate_detected", duplicate_detected, 'coverage detected row identity is missing or duplicated'))

        partial_execution = copy.deepcopy(manifest)
        partial_execution["executed"][0]["rows"] = partial_execution["executed"][0]["rows"][:-1]
        controls.append(("partial_execution", partial_execution, 'coverage execution does not prove exactly the required rows'))

        malformed_execution = copy.deepcopy(manifest)
        malformed_execution["executed"] = [{"lane_id": manifest["identity"]["lane_id"], "status": "success",
                                              "evidence": "driver-complete", "rows": [{"row": "not-an-id"}]}]
        controls.append(("malformed_execution", malformed_execution, 'coverage execution does not prove exactly the required rows'))

        missing_row = copy.deepcopy(manifest)
        required_id = next(row["id"] for row in missing_row["expected"] if row["state"] == "required")
        missing_row["expected"] = [row for row in missing_row["expected"] if row["id"] != required_id]
        controls.append(("missing_row", missing_row, 'coverage expected and detected row sets differ'))

        coordinated_shrink = copy.deepcopy(manifest)
        required_id = next(row["id"] for row in coordinated_shrink["expected"] if row["state"] == "required")
        excluded_id = next(row["id"] for row in coordinated_shrink["expected"] if row["state"] == "excluded")
        for key in ("expected", "detected"):
            coordinated_shrink[key] = [row for row in coordinated_shrink[key] if row["id"] not in (required_id, excluded_id)]
        coordinated_shrink["executed"][0]["rows"].remove(required_id)
        coordinated_shrink["policy"]["row_count"] -= 2
        coordinated_shrink["policy"]["required_count"] -= 1
        coordinated_shrink["policy"]["excluded_count"] -= 1
        coordinated_shrink["policy"]["fingerprint"] = ci_summary._coverage_policy_fingerprint(
            coordinated_shrink["identity"], coordinated_shrink["expected"])
        controls.append(("coordinated_shrink", coordinated_shrink, 'coverage self-test policy counts do not match the fixed fixture contract'))

        execution_none = copy.deepcopy(manifest)
        next(row for row in execution_none["expected"] if row["state"] == "required")["execution"] = "none"
        controls.append(("execution_none", execution_none, 'coverage required execution is malformed: ' + required_id))

        compiler_alias = copy.deepcopy(manifest)
        available = next(row for row in compiler_alias["detected"] if row["state"] == "available")
        available["identity"] = "BUSTER_BUILD_COMPILER_CLANG" if available["compiler"] == "gcc" else "BUSTER_BUILD_COMPILER_GNU"
        controls.append(("compiler_alias", compiler_alias, 'coverage compiler identity does not match logical family: ' + required_id))

        family_rewrite = copy.deepcopy(manifest)
        row = next(row for row in family_rewrite["expected"] if row["state"] == "required")
        row["compiler"] = "clang" if row["compiler"] != "clang" else "gcc"
        controls.append(("family_rewrite", family_rewrite, 'coverage expected row identity is not semantic: ' + required_id))

        target_spoof = copy.deepcopy(manifest)
        target = next(row for row in target_spoof["detected"] if row["state"] == "available")
        target["target"] = "aarch64-linux-gnu" if "x86_64" in manifest["identity"]["architecture"] else "x86_64-linux-gnu"
        controls.append(("target_spoof", target_spoof, 'coverage compiler target does not match lane: ' + required_id))

        target_conflict = copy.deepcopy(manifest)
        target = next(row for row in target_conflict["detected"] if row["state"] == "available")
        target["target"] = "aarch64-x86_64-linux-gnu" if "x86_64" in manifest["identity"]["architecture"] else "x86_64-aarch64-linux-gnu"
        controls.append(("target_conflict", target_conflict, 'coverage compiler target does not match lane: ' + required_id))

        plausible_target = copy.deepcopy(manifest)
        target = next(row for row in plausible_target["detected"] if row["state"] == "available")
        # Retain a plausible lane target but guarantee a change on every host.
        target["target"] += "-coverage-spoof"
        controls.append(("plausible_target", plausible_target, 'coverage compiler probe disagrees with detected identity: ' + required_id))

        version_spoof = copy.deepcopy(manifest)
        version = next(row for row in version_spoof["detected"] if row["state"] == "available")
        version["version"] = "fabricated compiler version"
        controls.append(("version_spoof", version_spoof, 'coverage compiler version does not match logical family: ' + required_id))

        plausible_version = copy.deepcopy(manifest)
        version = next(row for row in plausible_version["detected"] if row["state"] == "available")
        version["version"] += " (coverage-spoof)"
        controls.append(("plausible_version", plausible_version, 'coverage compiler probe disagrees with detected identity: ' + required_id))

        executable_spoof = copy.deepcopy(manifest)
        executable = next(row for row in executable_spoof["detected"] if row["state"] == "available")
        executable["path"], executable["path_hash"] = str(self.root / "fabricated-compiler"), "0" * 64
        controls.append(("executable_spoof", executable_spoof, 'coverage compiler executable cannot be read: ' + required_id))

        fabricated_obligations = copy.deepcopy(manifest)
        fabricated_obligations["obligations"]["self_host"] = {"state": "scheduled", "reason": "canonical-release-fanout"}
        fabricated_obligations["obligations"]["fixed_point"] = {"state": "scheduled", "reason": "canonical-release-fanout"}
        controls.append(("fabricated_obligations", fabricated_obligations, 'coverage self_host obligation does not match the independent lane policy'))

        source_spoof = copy.deepcopy(manifest)
        source_spoof["identity"]["source_hash"] = "0" * 64
        controls.append(("source_spoof", source_spoof, 'coverage source hash does not match source path'))

        missing_fingerprint = copy.deepcopy(manifest)
        missing_fingerprint["policy"].pop("fingerprint")
        controls.append(("missing_fingerprint", missing_fingerprint, 'coverage policy fingerprint is malformed'))

        evidence_spoof = copy.deepcopy(manifest)
        evidence_spoof["executed"][0]["evidence"] = "configuration-only"
        controls.append(("evidence_spoof", evidence_spoof, 'coverage execution evidence is malformed'))

        no_execution = copy.deepcopy(manifest)
        no_execution["executed"] = []
        controls.append(("no_execution", no_execution, 'coverage execution must contain exactly one lane completion record'))

        separate_lane_claim = copy.deepcopy(manifest)
        separate_lane_claim["executed"][0]["categories"] = ["analysis"]
        controls.append(("separate_lane_claim", separate_lane_claim, 'desktop completion cannot satisfy native or analyzer obligations'))

        native_obligation = copy.deepcopy(manifest)
        native_obligation["obligations"]["native"] = {"state": "scheduled", "reason": "wrong-lane"}
        controls.append(("native_obligation", native_obligation, 'coverage obligations must describe only the combinations lane'))

        disabled_obligation = copy.deepcopy(manifest)
        disabled_obligation["obligations"].pop("unity_analysis")
        controls.append(("disabled_obligation", disabled_obligation, 'coverage obligations must describe only the combinations lane'))

        if os.name != "nt":
            real_hash_path = Path("/etc/hosts")
            real_hash = hashlib.sha256(real_hash_path.read_bytes()).hexdigest()
            source_rebind = copy.deepcopy(manifest)
            source_rebind["identity"]["source_path"] = str(real_hash_path.resolve())
            source_rebind["identity"]["source_hash"] = real_hash
            controls.append(("source_rebind", source_rebind, 'coverage source_path is outside the consumer checkout expectation'))

            driver_rebind = copy.deepcopy(manifest)
            driver_path = Path("/usr/bin/true")
            driver_rebind["identity"]["driver_path"] = str(driver_path.resolve())
            driver_rebind["identity"]["driver_hash"] = hashlib.sha256(driver_path.read_bytes()).hexdigest()
            controls.append(("driver_rebind", driver_rebind, 'coverage driver_path is outside the consumer checkout expectation'))

            compiler_rebind = copy.deepcopy(manifest)
            available = next(row for row in compiler_rebind["detected"] if row["state"] == "available")
            compiler_rebind_path = Path("/usr/bin/true")
            available["path"] = str(compiler_rebind_path.resolve())
            available["path_hash"] = hashlib.sha256(compiler_rebind_path.read_bytes()).hexdigest()
            controls.append(("compiler_rebind", compiler_rebind, 'coverage compiler path is not the consumer-selected executable: ' + required_id))

        for name, candidate, diagnostic in controls:
            with self.subTest(control=name):
                # A no-op mutation or an unrelated path failure is not evidence.
                self.assertNotEqual(candidate, manifest)
                self.assertEqual(ci_summary.validate_coverage_manifest(
                    manifest, consumer_environment, expected_mode="self-test"), [])
                errors = ci_summary.validate_coverage_manifest(candidate, consumer_environment, expected_mode="self-test")
                self.assertIn(diagnostic, errors)

    def test_runner_identity_controls_start_from_valid_fixture(self):
        manifest = self.produce()
        consumer_environment = self.consumer_environment(manifest)
        platform = manifest["identity"]["platform"]
        architecture = manifest["identity"]["architecture"]
        controls = (
            ({"BUSTER_CI_COVERAGE_PLATFORM": "linux" if platform == "windows" else "windows"}, "platform"),
            ({"BUSTER_CI_COVERAGE_ARCH": "aarch64" if architecture == "x86_64" else "x86_64"}, "architecture"),
        )
        for overrides, field in controls:
            with self.subTest(field=field):
                self.assertEqual(ci_summary.validate_coverage_manifest(
                    manifest, consumer_environment, expected_mode="self-test"), [])
                environment = {**consumer_environment, **overrides}
                self.assertNotEqual(environment, consumer_environment)
                self.assertIn(f"coverage identity does not match {field}", ci_summary.validate_coverage_manifest(
                    manifest, environment, expected_mode="self-test"))

    def test_production_report_rejects_fixture_and_local_modes(self):
        manifest = self.produce()
        consumer_environment = self.consumer_environment(manifest)
        diagnostic = "coverage manifest mode does not match consumer expectation"
        self.assertEqual(ci_summary.validate_coverage_manifest(
            manifest, consumer_environment, expected_mode="self-test"), [])
        self.assertIn(diagnostic, ci_summary.validate_coverage_manifest(manifest, consumer_environment))
        environment = {"RUNNER_TEMP": str(self.root), "BUSTER_CI_STEPS": '{"matrix":{"outcome":"success"}}',
                       "BUSTER_CI_REQUIRED": "matrix", "BUSTER_CI_COVERAGE_OUTPUT": str(self.root / "coverage.json"),
                       **consumer_environment}
        for mode in ("self-test", "local"):
            candidate = copy.deepcopy(manifest)
            candidate["mode"] = mode
            for required in ("1", "0"):
                with self.subTest(mode=mode, required=required):
                    # A supplied path is mandatory evidence even without the flag.
                    (self.root / "coverage.json").write_text(json.dumps(candidate), encoding="utf-8")
                    report_environment = {**environment, "BUSTER_CI_COVERAGE_REQUIRED": required}
                    self.assertEqual(ci_summary.write_report(report_environment), 1)
                    report = json.loads((self.root / "buster-ci" / "result.json").read_text(encoding="utf-8"))
                    self.assertFalse(report["success"])
                    self.assertEqual(report["unsatisfied_steps"], ["coverage"])
                    self.assertIn(diagnostic, report["coverage_errors"])
        # The explicit test-only caller remains usable after the rejected reports.
        (self.root / "coverage.json").write_text(json.dumps(manifest), encoding="utf-8")
        self.assertEqual(ci_summary.write_report(environment, expected_coverage_mode="self-test"), 0)
        # Fixture path overrides in the environment must not authorize the CLI.
        child_environment = {**os.environ, **environment, "BUSTER_CI_COVERAGE_REQUIRED": "1"}
        result = subprocess.run([sys.executable, str(ROOT / "tools" / "ci_summary.py")], cwd=ROOT,
                                env=child_environment, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = json.loads((self.root / "buster-ci" / "result.json").read_text(encoding="utf-8"))
        self.assertIn(diagnostic, report["coverage_errors"])

    def test_missing_manifest_and_evidence_write_failure_fail(self):
        environment = {"RUNNER_TEMP": str(self.root), "BUSTER_CI_STEPS": '{"matrix":{"outcome":"success"}}',
                       "BUSTER_CI_REQUIRED": "matrix", "BUSTER_CI_COVERAGE_REQUIRED": "1"}
        self.assertEqual(ci_summary.write_report(environment), 1)
        blocked = self.root / "blocked"
        blocked.write_text("not a directory", encoding="utf-8")
        child_environment = os.environ.copy()
        child_environment["BUSTER_CI_COVERAGE_OUTPUT"] = str(blocked / "coverage.json")
        result = subprocess.run([str(self.driver), "coverage_manifest_self_test"], cwd=ROOT, env=child_environment,
                                capture_output=True, text=True, timeout=60)
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
