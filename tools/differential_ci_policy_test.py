#!/usr/bin/env python3
"""Offline regression for the hosted differential worker policy."""

from pathlib import Path
import unittest


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


if __name__ == "__main__":
    unittest.main()
