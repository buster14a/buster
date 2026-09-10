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
import textwrap
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
        self.assertEqual(output.read_text().strip(), str((self.root / "install").resolve()))

    @unittest.skipIf(os.name == "nt", "Creating symlinks requires Windows developer privileges")
    def test_install_path_resolves_symlinked_temporary_directory(self):
        real = self.root / "real"
        real.mkdir()
        alias = self.root / "alias"
        alias.symlink_to(real, target_is_directory=True)
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        with mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, cache, alias / "install", output)
        self.assertEqual(output.read_text().strip(), str((real / "install").resolve()))

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
        self.assertEqual(sorted(names), sorted(github_ci_time.PLATFORMS + github_ci_time.MOBILE))
        self.assertIn("fail-fast: false", text)
        self.assertIn("test_all_combinations_ci --verbose=1", text)
        self.assertIn("test_mode_matrix --config Release", text)
        self.assertIn("./android/test_ci.sh --all", text)
        self.assertIn("./ios/test_ci.sh --all", text)
        self.assertNotRegex(text, r"(?m)^\s*continue-on-error:")
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
        condition = re.search(r"id: modes\n        if: (.+)", text).group(1)
        self.assertIn("!cancelled()", condition)
        self.assertNotIn("steps.combinations_", condition)
        mobile = text.split("\n  mobile:", 1)[1].split("\n  complete:", 1)[0]
        self.assertNotIn("needs:", mobile)
        self.assertIn("needs: [lint, test, mobile]", text)
        self.assertIn("github.run_id", text.split("concurrency:", 1)[1].split("permissions:", 1)[0])

    def test_platform_and_bootstrap_events_cover_the_same_revisions(self):
        expected = ("  pull_request:", "  push:", "    branches: [main]",
                    "    tags: ['**']", "  merge_group:",
                    "    types: [checks_requested]", "  workflow_dispatch:")
        for name in ("ci.yml", "self-host-audit.yml"):
            with self.subTest(workflow=name):
                text = (ROOT / ".github/workflows" / name).read_text()
                block = re.search(r"(?ms)^on:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
                self.assertIsNotNone(block)
                lines = tuple(line.rstrip() for line in block.group(1).splitlines()
                              if line.strip() and not line.lstrip().startswith("#"))
                self.assertEqual(lines, expected)
                # Default checkout is the PR/merge-group merge revision, not
                # an independently selected head or a stale branch ref.
                self.assertNotRegex(text, r"(?m)^\s+(ref|repository):")

    def test_bootstrap_cancellation_is_isolated_by_workflow_and_event(self):
        suffix = ("${{ github.workflow }}-${{ github.event_name }}-"
                  "${{ github.event_name == 'pull_request' && github.event.pull_request.number || "
                  "github.event_name == 'merge_group' && github.ref || github.run_id }}")
        cancel = "${{ github.event_name == 'pull_request' || github.event_name == 'merge_group' }}"
        for name, prefix in (("ci.yml", "ci-"), ("self-host-audit.yml", "bootstrap-")):
            with self.subTest(workflow=name):
                text = (ROOT / ".github/workflows" / name).read_text()
                block = re.search(r"(?ms)^concurrency:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
                self.assertIsNotNone(block)
                lines = tuple(line.strip() for line in block.group(1).splitlines()
                              if line.strip() and not line.lstrip().startswith("#"))
                self.assertEqual(lines, ("group: " + prefix + suffix, "cancel-in-progress: " + cancel))

    def test_bootstrap_keeps_every_native_gate_in_order(self):
        text = (ROOT / ".github/workflows/self-host-audit.yml").read_text()
        commands = re.findall(r"(?m)^        run: '\"\$RUNNER_TEMP/buster-build\" (.+)'$", text)
        self.assertEqual(commands, [
            "self_host_audit_self_test",
            "generate --cc clang --ci --linker DEFAULT",
            "test_self_host --config Release",
            "test_self_host_audit --config Release",
            "build --config Release -t test_all",
        ])
        gates = text[text.index("      - name: Test the bootstrap checker"):].split(
            "      - name: Retain stage evidence even on failure", 1)[0]
        self.assertNotRegex(gates, r"(?m)^\s*continue-on-error:")
        blocks = re.findall(r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)", gates)
        self.assertEqual([name for name, _ in blocks], [
            "Test the bootstrap checker",
            "Configure production compiler",
            "Preserve ordinary bootstrap and alternate-backend gates",
            "Verify each generation and repeat",
            "Run compiler regressions",
            "Check the bootstrap probe against independent compiler oracles",
        ])
        evidence_gates = {
            "Run compiler regressions",
            "Check the bootstrap probe against independent compiler oracles",
        }
        for name, block in blocks:
            with self.subTest(gate=name):
                conditions = re.findall(r"(?m)^        if: (.+)$", block)
                # These two independent results survive an audit failure, but
                # cannot run before ordinary bootstrap or after cancellation.
                expected = (["${{ !cancelled() && steps.ordinary_bootstrap.outcome == 'success' }}"]
                            if name in evidence_gates else [])
                self.assertEqual(conditions, expected)
        self.assertIn("        id: ordinary_bootstrap\n", dict(blocks)[
            "Preserve ordinary bootstrap and alternate-backend gates"])
        oracle = dict(blocks)["Check the bootstrap probe against independent compiler oracles"]
        self.assertIn('"$RUNNER_TEMP/buster-build" test_differential --self-test', oracle)
        self.assertIn('"$RUNNER_TEMP/buster-build" test_differential --ide build/Release/ide '
                      '--cc clang --source tests/self_host_bootstrap_probe.c --sanitize-oracle '
                      '--out build/self-host-audit/probe-oracle', oracle)
        self.assertNotIn("needs:", text)
        self.assertIn("name: Linux x86-64 bootstrap evidence", text)
        self.assertIn("runs-on: ubuntu-26.04", text)
        self.assertIn("timeout-minutes: 30", text)
        self.assertNotIn("secrets.", text)
        self.assertNotRegex(text, r"(?m)^\s*[^#\n]+: write$")
        artifact = text.split("      - name: Retain stage evidence even on failure", 1)[1]
        self.assertIn("name: bootstrap-evidence-${{ github.sha }}-${{ github.run_id }}-${{ github.run_attempt }}", artifact)
        self.assertIn("if: ${{ !cancelled() }}", artifact)
        self.assertNotIn("always()", artifact)

    def test_actual_aggregate_rejects_missing_skipped_cancelled_and_failed_shards(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        aggregate = text.split("\n  complete:", 1)[1]
        self.assertIn("needs: [lint, test, mobile]", aggregate)
        self.assertIn("always()", aggregate)
        # Execute the workflow's real shell body, not a Python copy of its
        # predicate. Subshells contain its exit statements; all 125 outcomes
        # include empty/missing dependency results as well as terminal states.
        body = aggregate.split("        run: |\n", 1)[1]
        body = textwrap.dedent(body)
        with tempfile.TemporaryDirectory() as temporary:
            gate = Path(temporary) / "aggregate.sh"
            gate.write_bytes(body.encode("utf-8"))
            environment = dict(os.environ, BUSTER_CI_GATE=gate.as_posix(),
                               GITHUB_STEP_SUMMARY=(Path(temporary) / "summary.md").as_posix())
            script = r"""
set -eu
checked=0
for LINT_RESULT in success failure cancelled skipped ''; do
  for DESKTOP_RESULT in success failure cancelled skipped ''; do
    for MOBILE_RESULT in success failure cancelled skipped ''; do
      export LINT_RESULT DESKTOP_RESULT MOBILE_RESULT
      actual=0
      ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
      if [[ "$LINT_RESULT" == success && "$DESKTOP_RESULT" == success && "$MOBILE_RESULT" == success ]]; then
        [[ "$actual" -eq 0 ]] || exit 1
      else
        [[ "$actual" -ne 0 ]] || exit 1
      fi
      checked=$((checked + 1))
    done
  done
done
printf '%s\n' "$checked"
"""
            # Windows CreateProcess can choose System32/bash.exe (WSL)
            # before PATH. Use an absolute shell path; on Windows select
            # the installed Git Bash, not an unrelated WSL distribution.
            bash = shutil.which("bash")
            if os.name == "nt":
                git = shutil.which("git")
                self.assertIsNotNone(git, "Git for Windows is a CI prerequisite")
                bash = next((str(parent / "bin/bash.exe")
                             for parent in Path(git).resolve().parents
                             if (parent / "bin/bash.exe").is_file()), None)
            self.assertIsNotNone(bash, "Bash is a CI prerequisite")
            result = subprocess.run([bash, "--noprofile", "--norc", "-c", script], env=environment,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), "125")

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

    def sharded_sample(self):
        run = self.sample()
        for job in run["jobs"]:
            job["steps"] = [step for step in job["steps"] if not step["name"].startswith("Test (")]
        for name in github_ci_time.MOBILE + ("Workflow lint", "CI complete"):
            job = copy.deepcopy(run["jobs"][0])
            job["name"] = name
            step = ("Test (Android)" if name.startswith("Android") else
                    "Test (iOS simulator)" if name.startswith("iOS") else
                    "Validate every GitHub workflow" if name == "Workflow lint" else "Require every shard")
            job["steps"] = [{"name": step, "conclusion": "success"}]
            run["jobs"].append(job)
        return run

    def test_sharded_matrix_includes_mobile_lint_and_aggregate_cost(self):
        sample, reason = github_ci_time.measure(self.sharded_sample())
        self.assertIsNone(reason)
        self.assertEqual(sample["runner_seconds"], 660)

    def test_sharded_matrix_rejects_missing_or_skipped_work(self):
        for index in range(len(github_ci_time.SHARDED_JOBS)):
            run = self.sharded_sample()
            run["jobs"][index]["steps"] = []
            self.assertIsNone(github_ci_time.measure(run)[0])
            run = self.sharded_sample()
            run["jobs"].pop(index)
            self.assertIsNone(github_ci_time.measure(run)[0])

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
