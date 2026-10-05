#!/usr/bin/env python3
"""Focused controls for step-scoped Intel-macOS resource observations."""

import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import ci_runner_resources as resources


SCRIPT = Path(__file__).with_name("ci_runner_resources.py")


class RunnerResourceTests(unittest.TestCase):
    def test_signal_handler_only_records_stop_request(self):
        stop_event = mock.Mock()
        stop_event.set.side_effect = AssertionError("signal handler must not acquire a lock")
        with mock.patch.object(resources, "STOP", stop_event):
            resources.stop(signal.SIGTERM, None)
            self.assertIs(resources.STOP, True)
        stop_event.set.assert_not_called()

    def test_wait_interval_observes_stop_within_poll_bound(self):
        request_stop = lambda _seconds: resources.stop(signal.SIGTERM, None)
        with mock.patch.object(resources, "STOP", False), \
             mock.patch.object(resources.time, "monotonic", side_effect=[0, 1]), \
             mock.patch.object(resources.time, "sleep", side_effect=request_stop) as sleep:
            resources.wait_interval(30)
        sleep.assert_called_once_with(resources.STOP_POLL_SECONDS)

    def test_tree_excludes_unrelated_processes_and_sampler_subtree(self):
        listing = """100 1 2.0 1000 bash
110 100 30.5 4000 /usr/bin/clang
120 110 12.0 3000 /tmp/program
130 100 1.0 2000 /usr/bin/python3
131 130 99.0 90000 /bin/ps
200 1 80.0 50000 /tmp/unrelated
"""
        result = resources.process_tree(listing, 100, 130)
        self.assertEqual(result["status"], "observed")
        self.assertEqual(result["count"], 3)
        self.assertEqual(result["cpu_percent"], 44.5)
        self.assertEqual(result["rss_kib"], 8000)
        self.assertEqual([p["name"] for p in result["largest"]],
                         ["clang", "program", "bash"])
        self.assertEqual(resources.process_tree(listing, 999, 130),
                         {"status": "parent-unavailable"})

    def test_unavailable_measurements_are_explicitly_unknown(self):
        with mock.patch.object(resources, "command_output", return_value=(None, "timeout")):
            memory = resources.memory_observation()
        self.assertEqual(memory["free_percent"], "unknown")
        self.assertEqual(memory["swap_used_mib"], "unknown")
        self.assertEqual(memory["pageouts_cumulative"], "unknown")
        self.assertTrue(all(memory[k] == "timeout" for k in
                            ("pressure_status", "swap_status", "pageouts_status")))

    def test_mac_memory_parsing_preserves_units_and_probe_status(self):
        responses = [("The system has 17179869184 bytes.\nSystem-wide memory free percentage: 19%\n", None),
                     ("total = 2048.00M  used = 1.25G  free = 768.00M\n", None),
                     ("Mach Virtual Memory Statistics: (page size of 4096 bytes)\nPageouts: 77.\n", None)]
        with mock.patch.object(resources, "command_output", side_effect=responses):
            memory = resources.memory_observation()
        self.assertEqual(memory["free_percent"], 19)
        self.assertEqual(memory["swap_used_mib"], 1280.0)
        self.assertEqual(memory["pageouts_cumulative"], 77)
        self.assertEqual(memory["swap_status"], "observed")

    def test_live_and_file_record_and_clean_shutdown(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "resources.jsonl"
            child = subprocess.Popen([sys.executable, str(SCRIPT), "--parent-pid", str(os.getpid()),
                                      "--phase", "native-modes", "--log", str(log), "--interval", "1"],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                first = child.stdout.readline()
                self.assertTrue(first.startswith("CI_RESOURCE_SAMPLE "), first)
                record = json.loads(first.removeprefix("CI_RESOURCE_SAMPLE "))
                self.assertEqual(record["phase"], "native-modes")
                self.assertIn(record["process_tree"]["status"], ("observed", "parent-unavailable"))
                self.assertGreaterEqual(record["sampling_ms"], 0)
                child.send_signal(signal.SIGTERM)
                out, err = child.communicate(timeout=8)
                self.assertEqual(child.returncode, 0, err)
                self.assertIn("CI_RESOURCE_END phase=native-modes samples=1", out)
                self.assertEqual(log.read_text(encoding="utf-8"), first + out)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate()

    def test_step_cleanup_preserves_payload_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "failed-step.jsonl"
            script = f"""set -euo pipefail
{shlex.quote(sys.executable)} {shlex.quote(str(SCRIPT))} --parent-pid "$$" \\
  --phase failed-step --log {shlex.quote(str(log))} &
sampler=$!
trap 'kill "$sampler" 2>/dev/null || true; wait "$sampler" 2>/dev/null || true' EXIT
ready=0
deadline=$((SECONDS + 10))
while (( SECONDS < deadline )); do
  if [[ -f {shlex.quote(str(log))} ]]; then
    while IFS= read -r line; do
      if [[ "$line" == 'CI_RESOURCE_SAMPLE '* ]]; then ready=1; break; fi
    done < {shlex.quote(str(log))}
  fi
  if [[ $ready -eq 1 ]]; then break; fi
  if ! kill -0 "$sampler" 2>/dev/null; then
    echo 'sampler exited before its first complete sample' >&2
    exit 1
  fi
  sleep 0.05
done
if [[ $ready -ne 1 ]]; then
  echo 'sampler did not publish its first complete sample before the fixture deadline' >&2
  exit 1
fi
exit 7
"""
            child = subprocess.Popen(["bash", "-c", script], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, start_new_session=True)
            try:
                out, err = child.communicate(timeout=12)
            except BaseException:
                # The test owns its shell, sampler and any in-flight probes;
                # killing only the shell could leave descendants holding pipes.
                if child.returncode is None:
                    try:
                        os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                child.communicate(timeout=2)
                raise
            result = subprocess.CompletedProcess(child.args, child.returncode, out, err)
            self.assertEqual(result.returncode, 7, result.stderr)
            self.assertIn("CI_RESOURCE_SAMPLE ", result.stdout)
            self.assertEqual(log.read_text(encoding="utf-8"), result.stdout)
            samples = [line for line in result.stdout.splitlines() if line.startswith("CI_RESOURCE_SAMPLE ")]
            first = json.loads(samples[0].removeprefix("CI_RESOURCE_SAMPLE "))
            self.assertEqual(first["phase"], "failed-step")
            self.assertTrue(result.stdout.splitlines()[-1].startswith(
                f"CI_RESOURCE_END phase=failed-step samples={len(samples)} elapsed_seconds="))


if __name__ == "__main__":
    unittest.main()
