#!/usr/bin/env python3
"""Network-free tests of cache integrity and failure-summary contracts."""
import copy
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_summary
import ci_zig
import github_ci_time


class ZigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest_data = json.loads((ROOT / ".github/zig.json").read_text())
        self.payload = b"verified test fixture"
        digest = hashlib.sha256(self.payload).hexdigest()
        self.manifest_data["sha256"]["x86_64-linux"] = digest
        self.manifest = self.root / "zig.json"
        self.manifest.write_text(json.dumps(self.manifest_data))

    def test_manifest_covers_every_platform(self):
        for target in ci_zig.TARGETS:
            version, digest = ci_zig.load_pin(ROOT / ".github/zig.json", target)
            self.assertEqual(version, "0.16.0")
            self.assertEqual(len(digest), 64)

    def test_reject_incomplete_or_mutable_pins(self):
        for field, value in (("version", "latest"), ("version", "../bad"), ("sha256", {})):
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(self.manifest_data)
                data[field] = value
                self.manifest.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    ci_zig.load_pin(self.manifest, "x86_64-linux")
        with self.assertRaises(ValueError):
            ci_zig.load_pin(ROOT / ".github/zig.json", "../../evil")

    def test_all_digests_are_validated(self):
        self.manifest_data["sha256"]["aarch64-windows"] = "not-a-digest"
        self.manifest.write_text(json.dumps(self.manifest_data))
        with self.assertRaises(ValueError):
            ci_zig.load_pin(self.manifest, "x86_64-linux")

    def test_corrupt_cache_never_executes_or_downloads(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(b"poisoned cache")
        with mock.patch.object(ci_zig, "download_archive") as download, mock.patch.object(ci_zig.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install")
            download.assert_not_called()
            run.assert_not_called()

    def test_hit_revalidates_and_installs(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        with mock.patch.object(ci_zig, "download_archive") as download, mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
            self.assertEqual(run.call_count, 2)
            download.assert_not_called()
        self.assertEqual(output.read_text().strip(), str(self.root / "install"))

    def test_bad_download_never_extracts(self):
        def bad_download(url, destination):
            Path(destination).write_bytes(b"bad download")
        with mock.patch.object(ci_zig, "download_archive", side_effect=bad_download), mock.patch.object(ci_zig.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, self.root / "cache", self.root / "install")
            run.assert_not_called()

    def test_network_retries_are_bounded_and_partial_is_removed(self):
        target = self.root / "archive"
        with mock.patch.object(ci_zig.urllib.request, "urlopen", side_effect=OSError("offline")) as request, mock.patch.object(ci_zig.time, "sleep"):
            with self.assertRaises(OSError):
                ci_zig.download_archive("https://ziglang.org/test", target)
            self.assertEqual(request.call_count, 3)
            self.assertFalse(target.exists())
            self.assertFalse(target.with_name("archive.part").exists())

    def test_extraction_failure_does_not_publish_path(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        with mock.patch.object(ci_zig.subprocess, "run", side_effect=subprocess.CalledProcessError(2, "tar")):
            with self.assertRaises(subprocess.CalledProcessError):
                ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
        self.assertFalse(output.exists())
        self.assertFalse((self.root / "install").exists())

    def test_installation_is_not_overwritten(self):
        installed = self.root / "install"
        installed.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            ci_zig.install("x86_64-linux", self.manifest, self.root / "cache", installed)


class SummaryTests(unittest.TestCase):
    def test_missing_skipped_cancelled_and_failed_are_not_success(self):
        for outcome in (None, "skipped", "cancelled", "failure"):
            with self.subTest(outcome=outcome):
                steps = {} if outcome is None else {"test": {"outcome": outcome}}
                self.assertEqual(ci_summary.assess(steps, ["test"]), ["test"])

    def test_continue_on_error_cannot_disguise_failure(self):
        steps = {"test": {"outcome": "failure", "conclusion": "success"}}
        self.assertEqual(ci_summary.assess(steps, ["test"]), ["test"])

    def test_platform_inapplicable_steps_can_be_skipped(self):
        steps = {"test": {"outcome": "success"}, "windows": {"outcome": "skipped"}}
        self.assertEqual(ci_summary.assess(steps, ["test"]), [])

    def test_report_escapes_metadata_and_does_not_copy_secrets(self):
        with tempfile.TemporaryDirectory() as temporary:
            environment = {"RUNNER_TEMP": temporary, "BUSTER_CI_STEPS": '{"test":{"outcome":"success"}}',
                           "BUSTER_CI_REQUIRED": "test", "GITHUB_REF": "<script>alert(1)</script>",
                           "GITHUB_TOKEN": "must-not-appear", "BUSTER_CI_REPRO": "echo '<b>'"}
            self.assertEqual(ci_summary.write_report(environment), 0)
            directory = Path(temporary) / "buster-ci"
            text = (directory / "summary.md").read_text()
            self.assertIn("&lt;script&gt;", text)
            self.assertNotIn("<script>", text)
            self.assertNotIn("must-not-appear", text + (directory / "result.json").read_text())
            environment["BUSTER_CI_STEPS"] = "{}"
            self.assertEqual(ci_summary.write_report(environment), 1)
            self.assertFalse(json.loads((directory / "result.json").read_text())["success"])

    def test_required_list_must_be_explicit(self):
        with self.assertRaises(ValueError):
            ci_summary.write_report({"BUSTER_CI_STEPS": "{}"})



class WorkflowPolicyTests(unittest.TestCase):
    def test_all_six_platforms_and_commands_remain(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        names = re.findall(r"^          - name: (.+)$", text, re.M)
        self.assertEqual(sorted(names), sorted(github_ci_time.PLATFORMS))
        self.assertIn("fail-fast: false", text)
        self.assertIn("test_all_combinations_ci --verbose=1", text)
        self.assertIn("test_mode_matrix --config Release", text)
        self.assertIn("./android/test_ci.sh --all", text)
        self.assertIn("./ios/test_ci.sh --all", text)
        self.assertNotIn("continue-on-error:", text)
        self.assertNotIn("BUSTER_INCLUDE_TESTS=OFF", text)

    def test_integrity_and_security_policy(self):
        for path in (ROOT / ".github/workflows").glob("*.yml"):
            with self.subTest(path=path.name):
                text = path.read_text()
                self.assertNotIn("pull_request_target", text)
                self.assertIn("contents: read", text)
                self.assertIn("persist-credentials: false", text)
                self.assertIn("concurrency:", text)
                self.assertIn("timeout-minutes:", text)
                for action in re.findall(r"uses:\s*(\S+)", text):
                    self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertNotIn("restore-keys:", text)
        self.assertNotIn("install-vulkan-sdk", text)
        self.assertIn("hashFiles('.github/zig.json')", text)
        self.assertIn("UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1", text)
        self.assertNotIn("detect_leaks=0", text)

    def test_independent_suites_are_not_guarded_by_prior_test_success(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        for step in ("mode_matrix", "kvm", "android_sdk", "android", "ios"):
            condition = re.search(r"id: " + step + r"\n        if: (.+)", text).group(1)
            self.assertIn("!cancelled()", condition)
            self.assertNotIn("steps.combination_", condition)
            self.assertNotIn("steps.mode_matrix", condition)
        self.assertIn("github.run_id", text.split("concurrency:", 1)[1].split("permissions:", 1)[0])

    @unittest.skipIf(os.name == "nt", "The failure-propagation probe uses the Unix Clang driver")
    def test_recoverable_ubsan_error_is_fatal_with_ci_environment(self):
        compiler = shutil.which("clang")
        self.assertIsNotNone(compiler, "Clang is a CI prerequisite")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "probe.c"
            executable = root / "probe"
            source.write_text("int main(void) { volatile int x = 2147483647; volatile int y = x + 1; (void)y; return 0; }\n")
            subprocess.run([compiler, "-fsanitize=undefined", "-fsanitize-recover=all", str(source), "-o", str(executable)], check=True, timeout=30, capture_output=True)
            recovering = subprocess.run([str(executable)], env=dict(os.environ, UBSAN_OPTIONS="halt_on_error=0"), capture_output=True, text=True, timeout=30)
            self.assertEqual(recovering.returncode, 0)
            self.assertIn("runtime error", recovering.stderr)
            environment = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            result = subprocess.run([str(executable)], env=environment, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("runtime error", result.stderr)


class TimingTests(unittest.TestCase):
    def sample(self, identity=1, duration=60, revision="a"):
        run = {"id": identity, "head_sha": "source", "workflow_blob_sha": revision,
               "status": "completed", "conclusion": "success", "run_attempt": 1,
               "created_at": "2026-09-07T12:00:00Z", "jobs": []}
        from datetime import datetime, timedelta, timezone
        start = datetime(2026, 9, 7, 12, 0, 10, tzinfo=timezone.utc)
        finish = start + timedelta(seconds=duration)
        for name in github_ci_time.PLATFORMS:
            steps = ["Combination matrix (Windows)" if name.startswith("Windows") else "Combination matrix (Linux, macOS)"]
            if not name.startswith("Windows"):
                steps.append("Execution-mode matrix")
            if name.startswith("macOS"):
                steps.append("Test (iOS simulator)")
            if name == "Linux x86-64":
                steps.append("Test (Android)")
            run["jobs"].append({"name": name, "conclusion": "success", "run_attempt": 1,
                                "started_at": start.isoformat(), "completed_at": finish.isoformat(),
                                "labels": [name], "steps": [{"name": step, "conclusion": "success"} for step in steps]})
        return run

    def test_known_median_and_queue_are_separate(self):
        data = {"runs": [self.sample(1, 40), self.sample(2, 60), self.sample(3, 80)]}
        result = github_ci_time.summarize(data)
        row = result["cohorts"][0]
        self.assertEqual(row["n"], 3)
        self.assertEqual(row["medians"]["elapsed_seconds"], 70)
        self.assertEqual(row["medians"]["execution_span_seconds"], 60)
        self.assertEqual(row["medians"]["runner_seconds"], 360)
        self.assertEqual(row["medians"]["initial_queue_seconds"], 10)

    def test_failures_cancellations_reruns_and_partial_coverage_are_excluded(self):
        samples = [self.sample(index) for index in range(6)]
        samples[0]["conclusion"] = "failure"
        samples[1]["conclusion"] = "cancelled"
        samples[2]["run_attempt"] = 2
        samples[3]["jobs"].pop()
        samples[4]["jobs"][0]["steps"].pop()
        samples[5]["status"] = "in_progress"
        report = github_ci_time.summarize({"runs": samples})
        self.assertFalse(report["cohorts"])
        self.assertEqual(sum(report["excluded"].values()), 6)

    def test_different_workflows_or_runners_never_share_a_median(self):
        changed_runner = self.sample(3)
        changed_runner["jobs"][0]["labels"] = ["other-image"]
        report = github_ci_time.summarize({"runs": [self.sample(1), self.sample(2, revision="b"), changed_runner]})
        self.assertEqual(len(report["cohorts"]), 3)

    def test_duplicate_observations_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            github_ci_time.summarize({"runs": [self.sample(), self.sample()]})

    def test_missing_times_and_workflow_identity_are_not_imputed(self):
        samples = [self.sample(1), self.sample(2)]
        samples[0]["jobs"][0]["completed_at"] = None
        samples[1].pop("workflow_blob_sha")
        report = github_ci_time.summarize({"runs": samples})
        self.assertFalse(report["cohorts"])
        self.assertEqual(sum(report["excluded"].values()), 2)


if __name__ == "__main__":
    unittest.main()
