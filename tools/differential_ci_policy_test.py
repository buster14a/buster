#!/usr/bin/env python3
"""Offline regressions for differential CI and independent oracle predicates."""

from dataclasses import replace
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import differential_c_harness as harness
import reduce_differential_case as reducer


ROOT = Path(__file__).resolve().parents[1]


def observed_payload(line: str) -> str:
    """Return the command owned by the differential gate, unwrapping telemetry."""
    command = " ".join(line.strip().split())
    observer = '"$BUSTER_CI_PYTHON" tools/ci_native_observation.py run '
    if command.startswith(observer):
        separator = " -- "
        if separator not in command:
            raise AssertionError("native observation wrapper has no command separator")
        command = command.split(separator, 1)[1]
    return command


class DifferentialCIPolicyTests(unittest.TestCase):
    def test_native_full_corpus_requests_four_bounded_workers(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        native = workflow.split("\n  native:", 1)[1].split("\n  mobile:", 1)[0]
        step = native.split("      - name: Native configuration differential matrix", 1)[1]
        step = step.split("\n      - name:", 1)[0]
        commands = [observed_payload(line) for line in step.splitlines()
                    if '"$driver" test_differential' in line]

        self.assertEqual(commands, [
            '"$driver" test_differential --self-test',
            '"$driver" test_differential --ide build/Release/ide '
            '--out "$RUNNER_TEMP/buster-ci/differential" --sanitize-oracle --jobs 4',
        ])
        self.assertNotIn("BUSTER_TEST_JOBS", step)

    def test_runner_keeps_default_and_admission_clamps(self):
        runner = (ROOT / "tools/differential.c").read_text()
        self.assertRegex(
            runner,
            r"u32 generated = 4, seed = 1, requested_jobs = 1, jobs = 1(?:,|;)",
        )
        self.assertIn("*jobs = BUSTER_MIN(requested, BUSTER_MAX(cpus, 1U));", runner)
        self.assertIn("*jobs = BUSTER_MIN(*jobs, quota);", runner)
        self.assertIn("#if BUSTER_SINGLE_THREADED\n        *jobs = 1;", runner)
        self.assertIn('os_get_environment_variable(S8("BUSTER_TEST_JOBS"))', runner)

    def test_msvc_reference_has_bounded_timeout_samples_and_short_timeout_control(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        step = workflow.split("      - name: Native MSVC reference differential", 1)[1]
        step = step.split("\n      - name:", 1)[0]

        self.assertIn("'--reference-timeout', '60', '--strict-mir'", step)
        self.assertIn("if ($env:VS_ARCH -eq 'arm64') {\n            $MsvcArgs += @('--reference-samples', '12')", step)
        self.assertIn("$env:VS_ARCH -eq 'arm64'", step)
        self.assertIn("test_differential --self-test --reference-timeout 1", step)
        self.assertIn("DIFFERENTIAL_TIMEOUT_CONTROL reference_timeout_seconds=1 result=timeout", step)


class DifferentialOracleTests(unittest.TestCase):
    def observations(self, status=37, output=b"U0 123\n"):
        return [harness.Observation(label, True, "", 0, status, output)
                for label in ("clang-O0", "clang-O2", "ide", "ide-canon")]

    def test_matching_normal_nonzero_exit_is_valid(self):
        for status in (0, 37, 255):
            with self.subTest(status=status):
                result = harness.classify("control", 7, self.observations(status))
                self.assertEqual(result.category, "ok")

    def test_matching_crashes_and_missing_results_never_pass(self):
        for status in (-11, None, 0xC0000005):
            with self.subTest(status=status):
                result = harness.classify("control", 7, self.observations(status, b""))
                self.assertEqual(result.category, "generator")

    def test_either_incomplete_reference_invalidates_comparison(self):
        for index in (0, 1):
            for status in (-11, None, 0xC0000005):
                with self.subTest(index=index, status=status):
                    observations = self.observations()
                    observations[index] = replace(observations[index], run_returncode=status)
                    self.assertEqual(harness.classify("control", 7, observations).category, "generator")

    def test_reference_compile_failure_timeout_and_disagreement_fail_closed(self):
        for index in (0, 1):
            for changed in (
                {"compile_ok": False, "compile_returncode": 1},
                {"run_timeout": True},
                {"run_stdout": b"wrong\n"},
                {"run_returncode": 38},
            ):
                with self.subTest(index=index, changed=changed):
                    observations = self.observations()
                    observations[index] = replace(observations[index], **changed)
                    self.assertEqual(harness.classify("control", 7, observations).category, "generator")

    def test_wrong_subject_output_and_crashes_are_detected(self):
        for index in (2, 3):
            for changed, category in (
                ({"run_stdout": b"wrong\n"}, "behavior"),
                ({"run_returncode": 0}, "behavior"),
                ({"run_timeout": True}, "behavior"),
                ({"run_returncode": -11}, "run-crash"),
                ({"run_returncode": 0xC0000005}, "run-crash"),
                ({"compile_ok": False, "compile_returncode": 1,
                  "compile_output": "error: original rejection"}, "rejects"),
                ({"compile_ok": False, "compile_returncode": -11}, "ide-crash"),
            ):
                with self.subTest(index=index, changed=changed):
                    observations = self.observations()
                    observations[index] = replace(observations[index], **changed)
                    result = harness.classify("control", 7, observations)
                    self.assertEqual(result.category, category)
                    if changed.get("run_returncode") == 0xC0000005:
                        self.assertIn("runtime status 3221225477", result.detail)
                        self.assertNotIn("signal", result.detail)

    def test_both_reference_argv_match_buster_semantic_profile(self):
        modes = harness.modes(SimpleNamespace(cc="reference-compiler", ide="subject-compiler"))
        for label, command in modes[:2]:
            with self.subTest(label=label):
                for flag in ("-fwrapv", "-fno-strict-aliasing", "-funsigned-char"):
                    self.assertIn(flag, command)


class DifferentialReducerTests(unittest.TestCase):
    # These are controlled phase observations, not a second compiler oracle.
    # A reference hash exit of 37 is deliberately valid.
    def observe(self, changed=None, ide_modes=("ide", "ide-canon")):
        specifications = {
            label: (0, "", 37, b"reference\n")
            for label in ("clang-O0", "clang-O2", "ide", "ide-canon")
        }
        if changed:
            specifications.update(changed)
        calls = []
        self.compile_commands = []

        def compile_one(command, source, binary):
            label = Path(binary).name.removeprefix("candidate.")
            calls.append(label)
            self.compile_commands.append((label, command))
            return specifications[label][:2]

        def run_one(binary):
            label = Path(binary).name.removeprefix("candidate.")
            return specifications[label][2:]

        with tempfile.TemporaryDirectory() as directory:
            checker = reducer.Checker("subject-compiler", directory)
            with mock.patch.object(checker, "compile_one", side_effect=compile_one), \
                    mock.patch.object(checker, "run_one", side_effect=run_one):
                result = checker.observe("int main(void) { return 37; }\n", ide_modes=ide_modes)
        return result, calls

    def test_matching_subject_keeps_lazy_optimized_reference(self):
        result, calls = self.observe()
        self.assertEqual(result, ("ok", ""))
        self.assertEqual(calls, ["clang-O0", "ide", "ide-canon"])

    def test_bad_initial_reference_never_becomes_interesting(self):
        for status in (-11, None, "timeout", 0xC0000005):
            with self.subTest(status=status):
                result, calls = self.observe({"clang-O0": (0, "", status, b"reference\n")})
                self.assertEqual(result[0], "invalid")
                self.assertEqual(calls, ["clang-O0"])

    def test_rejection_and_compiler_crash_require_optimized_reference(self):
        subjects = (
            (1, "error: original rejection", None, b""),
            (-11, "", None, b""),
        )
        controls = (
            (1, "error: oracle rejected", None, b""),
            (None, "", None, b""),
            (0, "", -11, b"reference\n"),
            (0, "", None, b"reference\n"),
            (0, "", "timeout", b"reference\n"),
            (0, "", 0xC0000005, b"reference\n"),
            (0, "", 37, b"changed\n"),
            (0, "", 38, b"reference\n"),
        )
        for subject in subjects:
            for control in controls:
                with self.subTest(subject=subject[0], control=control):
                    result, calls = self.observe({"ide": subject, "clang-O2": control})
                    self.assertEqual(result[0], "invalid")
                    self.assertIn("clang-O2", calls)

    def test_valid_optimized_reference_preserves_divergence_categories(self):
        for subject, category in (
            ((1, "error: original rejection", None, b""), "rejects"),
            ((-11, "", None, b""), "ide-crash"),
            ((0xC0000005, "", None, b""), "ide-crash"),
            ((0, "", -11, b""), "run-crash"),
            ((0, "", 0xC0000005, b""), "run-crash"),
            ((0, "", "timeout", b""), "behavior"),
            ((0, "", 37, b"changed\n"), "behavior"),
        ):
            with self.subTest(category=category, subject=subject):
                result, calls = self.observe({"ide": subject})
                self.assertEqual(result[0], category)
                self.assertIn("clang-O2", calls)
                if category == "rejects":
                    self.assertEqual(result[1], "ide: error: original rejection")

    def test_compile_timeout_is_not_a_source_rejection(self):
        result, calls = self.observe({"ide": (None, "", None, b"")})
        self.assertEqual(result[0], "invalid")
        self.assertIn("compilation timed out", result[1])
        self.assertIn("clang-O2", calls)

    def test_actual_compile_timeout_result_cannot_preserve_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            checker = reducer.Checker("subject-compiler", directory)
            with mock.patch.object(reducer.subprocess, "run",
                                   side_effect=subprocess.TimeoutExpired(["subject-compiler"], 30)):
                self.assertEqual(checker.compile_one(["subject-compiler"], "input.c", "output"), (None, ""))

    def test_reducer_reference_argv_match_buster_semantic_profile(self):
        result, _ = self.observe({"ide": (0, "", 37, b"changed\\n")})
        self.assertEqual(result[0], "behavior")
        references = [(label, command) for label, command in self.compile_commands
                      if label.startswith("clang-")]
        self.assertEqual([label for label, _ in references], ["clang-O0", "clang-O2"])
        for label, command in references:
            with self.subTest(label=label):
                for flag in ("-fwrapv", "-fno-strict-aliasing", "-funsigned-char"):
                    self.assertIn(flag, command)

    def test_second_subject_rejection_retains_mode_and_diagnostic(self):
        result, calls = self.observe({"ide-canon": (1, "error: original rejection", None, b"")})
        self.assertEqual(result, ("rejects", "ide-canon: error: original rejection"))
        self.assertEqual(calls, ["clang-O0", "ide", "ide-canon", "clang-O2"])

    def test_second_subject_mode_obeys_same_reference_gate(self):
        result, calls = self.observe({
            "ide-canon": (1, "error: original rejection", None, b""),
            "clang-O2": (0, "", 37, b"changed\n"),
        })
        self.assertEqual(result[0], "invalid")
        self.assertEqual(calls, ["clang-O0", "ide", "ide-canon", "clang-O2"])


if __name__ == "__main__":
    unittest.main()
