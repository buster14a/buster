#!/usr/bin/env python3
"""Network-free tests for fail-closed metamorphic CI evidence retention."""

from __future__ import annotations

import json
import os
from pathlib import Path
import tempfile
import unittest

import ci_metamorphic_evidence as evidence
import ci_summary


class MetamorphicEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.workspace = self.root / "workspace"
        self.workspace.mkdir()
        self.output = self.root / "retained" / "metamorphic"
        self.log = self.root / "combinations.log"
        self.environment = {
            "GITHUB_REPOSITORY": "buster14a/buster",
            "GITHUB_SHA": "a" * 40,
            "GITHUB_REF": "refs/heads/main",
            "GITHUB_RUN_ID": "123",
            "GITHUB_RUN_ATTEMPT": "2",
            "RUNNER_OS": "Windows",
            "RUNNER_ARCH": "X64",
            "ImageOS": "windows2025",
            "ImageVersion": "test-image",
            "BUSTER_MATRIX_SHARD": "checks",
            "BUSTER_CI_COVERAGE_PLATFORM": "windows",
            "BUSTER_CI_COVERAGE_ARCH": "x86_64",
        }

    @staticmethod
    def outcome(*, phase=1, launched=1, result=0, platform_status=0, timed_out=0,
                command='argv[0]=build/build-checks-ci_on-cc_clang-sanitize_on-fuzz_available_on-configs_Debug/Debug/ide.exe\nargv[1]=cc',
                stderr=""):
        return (f"phase={phase} launched={launched} result={result} platform_status={platform_status} timed_out={timed_out}\n"
                f"{command}\nstdout:\n\nstderr:\n{stderr}").encode()

    def bundle(self, name="failure-0", observed_base=None):
        path = self.workspace / "build" / name
        (path / "minimized").mkdir(parents=True)
        (path / "reproducer.txt").write_text(
            "seed=1 target=windows-x64 allocator=none mask=1024\n"
            "minimized_mask=1024 terms=1 rounds=1 salt=1 factor=1 inputs=8\n"
            "reducer_replays=0 signature_preserved=1\ncompiler=ide.exe\n",
            encoding="utf-8")
        (path / "observed-base.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
        (path / "observed-transformed.c").write_text("int helper(void) { return 0; }\n", encoding="utf-8")
        values = {
            "observed-base.log": observed_base or self.outcome(result=1, platform_status=7),
            "observed-transformed.log": self.outcome(phase=3),
            "base.log": self.outcome(result=1, platform_status=7),
            "transformed.log": self.outcome(phase=3),
            "minimized/base.log": self.outcome(result=1, platform_status=7),
            "minimized/transformed.log": self.outcome(phase=3),
        }
        for relative, data in values.items():
            destination = path / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (path / "observed-base.exe").write_bytes(b"MZ fixture")
        return path

    @staticmethod
    def marker(bundle="build/failure-0"):
        return f"METAMORPHIC_FAILURE seed=1 mask=1024 target=windows-x64 allocator=0 bundle={bundle}\n"

    def collect(self):
        return evidence.collect(self.log, self.workspace, self.output, self.environment)

    def manifest(self):
        index = json.loads((self.output / "index.json").read_text(encoding="utf-8"))
        path = self.output / index["bundles"][0]["path"] / "manifest.json"
        return index, json.loads(path.read_text(encoding="utf-8"))

    def test_empty_utf8_log_publishes_an_atomic_zero_marker_index(self):
        self.log.write_text("all tests passed\n", encoding="utf-8")
        result = self.collect()
        self.assertEqual(result["marker_count"], 0)
        self.assertTrue((self.output / "index.json").is_file())
        self.assertFalse(any(self.output.parent.glob("metamorphic.staging-*")))

    def test_utf16_log_retains_complete_bundle_and_exact_provenance(self):
        self.bundle()
        self.log.write_bytes(self.marker().encode("utf-16"))
        index = self.collect()
        self.assertEqual(index["source_log"]["encoding"], "utf-16-le")
        _, manifest = self.manifest()
        self.assertEqual(manifest["metadata"]["source_sha"], "a" * 40)
        self.assertEqual(manifest["marker"]["allocator_name"], "none")
        self.assertEqual(manifest["row"]["configuration"], "Debug")
        self.assertTrue(manifest["row"]["sanitize"])
        self.assertTrue(manifest["row"]["fuzz_available"])
        self.assertEqual(manifest["outcomes"]["observed-base"]["classification"], "process-failure")
        self.assertEqual(manifest["outcomes"]["observed-transformed"]["classification"], "success")
        retained = self.output / index["bundles"][0]["path"] / "bundle" / "observed-base.exe"
        self.assertEqual(retained.read_bytes(), b"MZ fixture")

    def test_failure_modes_are_distinct(self):
        cases = (
            (self.outcome(timed_out=1, result=1), "timeout"),
            (self.outcome(launched=0, result=4), "launch-failure"),
            (self.outcome(result=1, stderr="compiler reported success without a nonempty requested output\n"), "missing-output"),
            (self.outcome(result=2), "wait-failure"),
            (self.outcome(result=3), "process-crash"),
        )
        for number, (outcome, expected) in enumerate(cases):
            with self.subTest(expected=expected):
                root = self.root / str(number)
                root.mkdir()
                self.workspace = root / "workspace"
                self.workspace.mkdir()
                self.output = root / "retained" / "metamorphic"
                self.log = root / "combinations.log"
                self.bundle(observed_base=outcome)
                self.log.write_text(self.marker(), encoding="utf-8")
                self.collect()
                _, manifest = self.manifest()
                self.assertEqual(manifest["outcomes"]["observed-base"]["classification"], expected)

    def test_missing_or_incomplete_advertised_bundle_fails_without_partial_publication(self):
        for incomplete in (False, True):
            with self.subTest(incomplete=incomplete):
                root = self.root / ("incomplete" if incomplete else "missing")
                root.mkdir()
                self.workspace = root / "workspace"
                self.workspace.mkdir()
                self.output = root / "retained" / "metamorphic"
                self.log = root / "combinations.log"
                if incomplete:
                    bundle = self.bundle()
                    (bundle / "observed-base.log").unlink()
                self.log.write_text(self.marker(), encoding="utf-8")
                with self.assertRaises(evidence.EvidenceError):
                    self.collect()
                self.assertFalse(self.output.exists())
                self.assertFalse(any(self.output.parent.glob("metamorphic.staging-*")))

    @unittest.skipIf(os.name == "nt", "Creating symlinks requires Windows developer privileges")
    def test_bundle_symlink_is_rejected(self):
        bundle = self.bundle()
        (bundle / "escape").symlink_to(self.root / "outside")
        self.log.write_text(self.marker(), encoding="utf-8")
        with self.assertRaisesRegex(evidence.EvidenceError, "symbolic link"):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_marker_and_reproducer_must_agree(self):
        bundle = self.bundle()
        (bundle / "reproducer.txt").write_text("seed=2 target=windows-x64 allocator=none mask=1024\n", encoding="utf-8")
        self.log.write_text(self.marker(), encoding="utf-8")
        with self.assertRaisesRegex(evidence.EvidenceError, "marker/reproducer mismatch"):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_desktop_summary_records_missing_advertised_evidence_as_required_failure(self):
        runner_temp = self.root / "runner"
        diagnostics = runner_temp / "buster-ci"
        diagnostics.mkdir(parents=True)
        (diagnostics / "combinations.log").write_text(self.marker(), encoding="utf-8")
        environment = dict(
            self.environment,
            RUNNER_TEMP=str(runner_temp),
            GITHUB_WORKSPACE=str(self.workspace),
            BUSTER_CI_REQUIRED="combinations_windows",
            BUSTER_CI_STEPS=json.dumps({"combinations_windows": {"outcome": "failure"}}),
        )
        status = ci_summary.write_report(environment)
        self.assertEqual(status, 1)
        report = json.loads((diagnostics / "result.json").read_text(encoding="utf-8"))
        self.assertIn("metamorphic_evidence", report["unsatisfied_steps"])
        self.assertEqual(report["steps"]["metamorphic_evidence"]["outcome"], "failure")
        self.assertTrue((diagnostics / "metamorphic-evidence-error.json").is_file())
        self.assertFalse((diagnostics / "metamorphic").exists())


if __name__ == "__main__":
    unittest.main()
