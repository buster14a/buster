#!/usr/bin/env python3
"""Offline Android job-summary regressions; no SDK or emulator is required."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_summary


class AndroidSummaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "buster-ci").mkdir()
        self.log = self.root / "buster-ci" / "android.log"
        self.environment = {
            "RUNNER_TEMP": str(self.root),
            "GITHUB_STEP_SUMMARY": str(self.root / "step-summary.md"),
            "BUSTER_CI_REQUIRED": "android",
            "BUSTER_CI_STEPS": json.dumps({"android": {"outcome": "failure"}}),
        }

    def report(self, log, outcome="failure"):
        if log is not None:
            self.log.write_bytes(log.encode("utf-8") if isinstance(log, str) else log)
        self.environment["BUSTER_CI_STEPS"] = json.dumps({"android": {"outcome": outcome}})
        with contextlib.redirect_stdout(io.StringIO()):
            status = ci_summary.write_report(self.environment)
        output = self.root / "buster-ci"
        return (status, json.loads((output / "result.json").read_text()),
                (output / "summary.md").read_text())

    @staticmethod
    def configuration(config, status=0, reader=10, producer=143, phase="monitor"):
        return (f"ANDROID_MONITOR_RESULT config={config} reader_status={reader} producer_status={producer} timeout_seconds=60\n"
                f"ANDROID_PAYLOAD_RESULT config={config} phase={phase} status={status}\n"
                f"ANDROID_CONFIG_RESULT config={config} status={status}\n")

    @staticmethod
    def final(payload=0, cleanup="0", status=0, config="none", phase="tests"):
        return (f"ANDROID_BATCH_RESULT phase={phase} config={config} status={payload} cleanup_status=not-run\n"
                f"ANDROID_CI_RESULT phase=tests payload_status={payload} cleanup_status={cleanup} status={status}\n")

    def passing_log(self):
        return self.configuration("Debug") + self.configuration("Release") + self.final()

    def test_debug_timeout_is_visible_despite_release_success(self):
        log = (self.configuration("Debug", status=1, reader=0, producer=124) +
               "BUSTER_ANDROID_TEST_RESULT:0\nAndroid compiler tests passed\n" +
               self.configuration("Release") + self.final(payload=1, cleanup="not-run", status=1, config="Debug"))
        status, report, summary = self.report(log)
        self.assertEqual(status, 1)
        self.assertFalse(report["success"])
        self.assertEqual(report["unsatisfied_steps"], ["android"])
        self.assertEqual(report["android"]["configurations"]["Debug"]["config"]["status"], "1")
        self.assertIn("| Debug | 1 | monitor | 1 | 0 / 124 | 60 |", summary)
        self.assertIn("| Release | 0 | monitor | 0 | 10 / 143 | 60 |", summary)
        self.assertIn("failed before final emulator cleanup", summary)
        self.assertIn("first failed configuration=Debug", summary)
        self.assertEqual((self.root / "step-summary.md").read_text(), summary)

    def test_cleanup_only_failure_is_explicit(self):
        log = self.configuration("Debug") + self.configuration("Release") + self.final(cleanup="23", status=1)
        status, report, summary = self.report(log)
        self.assertEqual(status, 1)
        self.assertEqual(report["android"]["ci"]["cleanup_status"], "23")
        self.assertIn("required emulator cleanup failed after a successful payload", summary)
        self.assertIn("cleanup_status=23", summary)
        self.assertNotIn("failed before final emulator cleanup", summary)

    def test_combined_failure_does_not_blame_cleanup_alone(self):
        log = self.configuration("Debug", status=1) + self.configuration("Release") + self.final(payload=1, cleanup="23", status=1, config="Debug")
        _, _, summary = self.report(log)
        self.assertIn("failed before final emulator cleanup", summary)
        self.assertIn("cleanup_status=23", summary)
        self.assertNotIn("after a successful payload", summary)

    def test_passing_configurations_and_stopped_logcat_are_reported(self):
        status, report, summary = self.report(self.passing_log(), "success")
        self.assertEqual(status, 0)
        self.assertTrue(report["success"])
        self.assertEqual(report["android"]["warnings"], [])
        self.assertIn("| Debug | 0 | monitor | 0 | 10 / 143 | 60 |", summary)
        self.assertNotIn("failed before", summary)
        self.assertNotIn("cleanup failed", summary)

    def test_cancellation_keeps_unstarted_release_visible(self):
        for code in (130, 143):
            with self.subTest(code=code):
                log = (f"ANDROID_PAYLOAD_RESULT config=Debug phase=monitor status={code}\n"
                       f"ANDROID_CONFIG_RESULT config=Debug status={code}\n"
                       "ANDROID_CONFIG_RESULT config=Release status=not-run\n" +
                       self.final(payload=code, cleanup="23", status=code, config="Debug"))
                status, _, summary = self.report(log, "cancelled")
                self.assertEqual(status, 1)
                self.assertIn("| Release | not-run | missing | missing | missing / missing | missing |", summary)
                self.assertIn(f"payload_status={code}", summary)

    def test_early_build_failure_does_not_invent_test_results(self):
        log = ("ANDROID_CONFIG_RESULT config=Debug status=not-run\n"
               "ANDROID_CONFIG_RESULT config=Release status=not-run\n" +
               self.final(payload=17, status=17, config="Debug", phase="build"))
        _, report, summary = self.report(log)
        self.assertEqual(report["android"]["batch"]["phase"], "build")
        self.assertIn("phase=build", summary)
        self.assertIn("| Debug | not-run | missing | missing | missing / missing | missing |", summary)

    def test_start_failure_without_batch_has_final_status(self):
        _, _, summary = self.report("ANDROID_CI_RESULT phase=start payload_status=23 cleanup_status=not-run status=23\n")
        self.assertIn("phase=start", summary)
        self.assertIn("Batch result: missing", summary)
        self.assertIn("failed before final emulator cleanup", summary)

    def test_missing_and_empty_logs_are_explicit(self):
        for log in (None, ""):
            with self.subTest(log=log):
                status, report, summary = self.report(log)
                self.assertEqual(status, 1)
                self.assertIn("android", report)
                self.assertIn("| Debug | missing |", summary)
                self.assertIn("| Release | missing |", summary)
                self.assertIn("Final CI result: missing", summary)

    def test_monitor_eof_without_a_terminal_marker_remains_failed(self):
        log = (self.configuration("Debug", status=1, reader=0, producer=0) +
               self.configuration("Release") + self.final(payload=1, status=1, config="Debug"))
        status, _, summary = self.report(log)
        self.assertEqual(status, 1)
        self.assertIn("| Debug | 1 | monitor | 1 | 0 / 0 | 60 |", summary)

    def test_install_failure_and_ordinary_payload_failure_remain_failed(self):
        for phase, code in (("install", 23), ("launch", 17), ("wait-device", 1)):
            with self.subTest(phase=phase):
                log = (f"ANDROID_PAYLOAD_RESULT config=Debug phase={phase} status={code}\n"
                       f"ANDROID_CONFIG_RESULT config=Debug status={code}\n" +
                       self.configuration("Release") + self.final(payload=1, status=1, config="Debug"))
                status, _, summary = self.report(log)
                self.assertEqual(status, 1)
                self.assertIn(f"| Debug | {code} | {phase} | {code} |", summary)

    def test_read_failure_leaves_the_existing_step_failure_intact(self):
        self.log.write_text(self.passing_log())
        original_open = Path.open

        def failing_open(path, *args, **kwargs):
            if path == self.log:
                raise PermissionError("private diagnostic text must not be copied")
            return original_open(path, *args, **kwargs)

        with mock.patch.object(Path, "open", failing_open):
            status, report, summary = self.report(None)
        self.assertEqual(status, 1)
        self.assertIn("unavailable", summary)
        self.assertNotIn("private diagnostic", json.dumps(report))

    def test_step_outcomes_remain_authoritative(self):
        for outcome in ("success", "failure", "cancelled", "skipped", "missing"):
            for log in ("", self.passing_log(), self.final(payload=23, status=23)):
                with self.subTest(outcome=outcome, log=log):
                    status, report, _ = self.report(log, outcome)
                    self.assertEqual(status, 0 if outcome == "success" else 1)
                    self.assertEqual(report["success"], outcome == "success")

    def test_non_android_summary_is_unchanged(self):
        self.environment["BUSTER_CI_REQUIRED"] = "ios"
        self.environment["BUSTER_CI_STEPS"] = json.dumps({"ios": {"outcome": "success"}})
        with mock.patch.object(ci_summary, "_android_diagnostics", side_effect=AssertionError("not an Android lane")):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ci_summary.write_report(self.environment), 0)
        output = self.root / "buster-ci"
        self.assertNotIn("android", json.loads((output / "result.json").read_text()))
        self.assertNotIn("Android configuration", (output / "summary.md").read_text())

    def test_echoes_logcat_and_emulator_text_cannot_supply_results(self):
        log = ("printf 'ANDROID_CONFIG_RESULT config=Debug status=0\\n'\n"
               "2026-09-16T12:00:00Z ANDROID_CONFIG_RESULT config=Release status=0\n"
               "09-16 12:00:00.000 123 456 I buster: ANDROID_CONFIG_RESULT config=Debug status=0\n"
               "BUSTER_ANDROID_TEST_RESULT:0\nERROR | stop: Not implemented\n")
        _, report, summary = self.report(log)
        self.assertEqual(report["android"]["configurations"]["Debug"], {})
        self.assertIn("Final CI result: missing", summary)
        self.assertNotIn("required emulator cleanup failed", summary)

    def test_duplicate_records_are_ambiguous_not_last_wins(self):
        log = "ANDROID_CONFIG_RESULT config=Debug status=1\n" + self.passing_log()
        _, report, summary = self.report(log)
        self.assertIsNone(report["android"]["configurations"]["Debug"]["config"])
        self.assertIn("ambiguous", summary)
        self.assertIn("| Debug | missing | monitor | 0 |", summary)

    def test_malformed_records_and_untrusted_values_are_not_copied(self):
        log = ("ANDROID_CONFIG_RESULT config=Debug status=0 extra=secret\n"
               "ANDROID_CONFIG_RESULT config=Release status=999\n"
               "ANDROID_PAYLOAD_RESULT config=Debug phase=<script>secret</script> status=0\n"
               "ANDROID_CI_RESULT phase=tests payload_status=0 cleanup_status=0 status=0 status=1\n")
        _, report, summary = self.report(log)
        self.assertTrue(report["android"]["warnings"])
        self.assertNotIn("secret", json.dumps(report))
        self.assertNotIn("<script>", summary)
        self.assertIn("Final CI result: missing", summary)

    def test_crlf_and_binary_noise_do_not_hide_valid_records(self):
        log = b"\xff\xfeignored\n" + self.passing_log().replace("\n", "\r\n").encode()
        _, report, _ = self.report(log, "success")
        self.assertEqual(report["android"]["ci"]["status"], "0")

    def test_long_line_fragments_do_not_become_records(self):
        log = "x" * ci_summary._ANDROID_LOG_LINE_BYTES + "ANDROID_CONFIG_RESULT config=Debug status=0\n" + self.passing_log()
        _, report, _ = self.report(log, "success")
        self.assertEqual(report["android"]["configurations"]["Debug"]["config"]["status"], "0")
        self.assertFalse(any("ambiguous" in warning for warning in report["android"]["warnings"]))

    def test_unterminated_last_record_is_not_accepted(self):
        _, report, _ = self.report("ANDROID_CI_RESULT phase=tests payload_status=0 cleanup_status=0 status=0")
        self.assertIsNone(report["android"]["ci"])
        self.assertTrue(report["android"]["warnings"])

    def test_scan_budget_is_bounded_and_truncation_is_visible(self):
        with mock.patch.object(ci_summary, "_ANDROID_LOG_MAX_BYTES", 64):
            _, report, summary = self.report("x" * 128 + "\n" + self.passing_log())
        self.assertIsNone(report["android"]["ci"])
        self.assertIn("truncated", summary)

    def test_directory_in_place_of_log_is_unavailable(self):
        self.log.mkdir()
        _, report, summary = self.report(None)
        self.assertIsNone(report["android"]["ci"])
        self.assertIn("unavailable", summary)

    def test_workflow_schedules_and_retains_the_summary_regressions(self):
        workflow = (ROOT / ".github/workflows/ios-monitor-tests.yml").read_text()
        self.assertEqual(workflow.count("- 'tools/ci_summary.py'"), 2)
        self.assertIn('python3 android/ci_summary_test.py -v 2>&1 | tee "$RUNNER_TEMP/android-summary.log"', workflow)
        self.assertIn("${{ runner.temp }}/android-summary.log", workflow)


if __name__ == "__main__":
    unittest.main()
