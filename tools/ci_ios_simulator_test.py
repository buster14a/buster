#!/usr/bin/env python3
"""Finite JSON/parser controls only; no simctl or native command is executed."""
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import ci_ios_simulator as simulator

ROOT = Path(__file__).resolve().parents[1]
UDID = "12345678-1234-1234-1234-123456789ABC"
RUNTIME = "com.apple.CoreSimulator.SimRuntime.iOS-26-5"
DEVICE = "com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro"


class SimulatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = dict(BUSTER_CI_CONDITIONS_EVIDENCE="1", RUNNER_TEMP=str(self.root),
                                GITHUB_REPOSITORY="buster14a/buster", GITHUB_SHA="a" * 40,
                                GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="1", GITHUB_JOB="mobile")

    def parser(self, data, *, evidence=True):
        launcher = Path(os.environ.get("BUSTER_IOS_SELECTION_TEST_LAUNCHER", str(ROOT / "ios/launch_simulator.sh")))
        source = launcher.read_text()
        start = source.index("xcrun simctl list devices available -j 2>/dev/null | python3 -c '")
        code = source[start:].split("python3 -c '", 1)[1].split("' \"$device_name\"", 1)[0]
        environment = dict(os.environ, **self.environment)
        if not evidence:
            environment.pop("BUSTER_CI_CONDITIONS_EVIDENCE", None)
        result = subprocess.run([sys.executable, "-B", "-c", code, "buster-ci", str(ROOT / "tools")],
                                input=json.dumps(data), text=True, capture_output=True,
                                env=environment, timeout=5)
        return result

    def data(self, device=None):
        selected = dict(name="buster-ci", udid=UDID, isAvailable=True, deviceTypeIdentifier=DEVICE)
        if device is not None:
            selected.update(device)
        return {"devices": {RUNTIME: [selected]}}

    def retained(self):
        path = self.root / "buster-ci/ios-simulator-selection.json"
        self.assertTrue(path.is_file(), "selected runtime/device witness is absent")
        return json.loads(path.read_text())

    def test_discovery_retains_actual_selected_record_without_changing_stdout(self):
        result = self.parser(self.data())
        self.assertEqual((result.returncode, result.stdout), (0, UDID + "\n"))
        value = self.retained()
        self.assertEqual(value["status"], "observed")
        self.assertEqual(value["selection"], dict(kind="name-reuse", udid=UDID, runtime=RUNTIME, device_type=DEVICE))
        self.assertEqual(value["source_revision"], "a" * 40)
        self.assertEqual(value["job"], "mobile")

    def test_first_match_and_existing_availability_policy_are_preserved(self):
        earlier = "com.apple.CoreSimulator.SimRuntime.iOS-18-2"
        data = self.data()
        data["devices"] = {earlier: [dict(name="buster-ci", udid=UDID, deviceTypeIdentifier=DEVICE)],
                           RUNTIME: [dict(name="buster-ci", udid="87654321-4321-4321-4321-CBA987654321", isAvailable=True, deviceTypeIdentifier=DEVICE)]}
        result = self.parser(data)
        self.assertEqual((result.returncode, result.stdout), (0, UDID + "\n"))
        self.assertEqual(self.retained()["selection"]["runtime"], earlier)

    def test_unavailable_match_and_no_match_do_not_write_a_selection(self):
        result = self.parser(self.data(dict(isAvailable=False)))
        self.assertEqual((result.returncode, result.stdout), (1, ""))
        self.assertFalse((self.root / "buster-ci").exists())

    def test_disabled_retention_has_no_io_or_added_stdout(self):
        result = self.parser(self.data(), evidence=False)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, UDID + "\n", ""))
        self.assertFalse((self.root / "buster-ci").exists())
        with mock.patch.object(simulator.environment_tools, "write_receipt", side_effect=AssertionError("I/O")):
            simulator.retain("created", UDID, RUNTIME, DEVICE, {})

    def test_missing_actual_device_type_retains_unknown_and_preserves_stdout(self):
        data = self.data()
        del data["devices"][RUNTIME][0]["deviceTypeIdentifier"]
        result = self.parser(data)
        self.assertEqual((result.returncode, result.stdout), (0, UDID + "\n"))
        self.assertEqual(self.retained()["status"], "unknown")
        self.assertIsNone(self.retained()["selection"]["device_type"])

    def test_created_and_explicit_paths_do_not_guess_selection_fields(self):
        created = simulator.receipt("created", UDID, RUNTIME, DEVICE, self.environment)
        self.assertEqual(created["status"], "observed")
        for values in (("", ""), (RUNTIME, DEVICE)):
            explicit = simulator.receipt("explicit", UDID, *values, self.environment)
            self.assertEqual(explicit["status"], "unknown")
        for key, value in (("udid", "FAKE-UDID"), ("runtime", "latest"), ("device_type", "iPhone")):
            fields = dict(udid=UDID, runtime=RUNTIME, device_type=DEVICE)
            fields[key] = value
            self.assertEqual(simulator.receipt("created", **fields, environment=self.environment)["status"], "unknown")

    def test_created_and_explicit_cli_emit_no_selection_stdout(self):
        for kind, expected in (("created", "observed"), ("explicit", "unknown")):
            with self.subTest(kind=kind):
                directory = self.root / kind
                directory.mkdir()
                environment = dict(os.environ, **self.environment)
                environment["RUNNER_TEMP"] = str(directory)
                result = subprocess.run([sys.executable, "-B", str(ROOT / "tools/ci_ios_simulator.py"),
                                         "--kind", kind, "--udid", UDID, "--runtime", RUNTIME, "--device-type", DEVICE],
                                        env=environment, capture_output=True, text=True, timeout=5)
                self.assertEqual((result.returncode, result.stdout), (0, ""))
                value = json.loads((directory / "buster-ci/ios-simulator-selection.json").read_text())
                self.assertEqual(value["status"], expected)

    def test_bindings_and_bounded_fields_fail_as_unknown_or_absent(self):
        environment = dict(self.environment, GITHUB_SHA="unavailable")
        self.assertEqual(simulator.receipt("created", UDID, RUNTIME, DEVICE, environment)["status"], "unknown")
        with mock.patch("sys.stderr", new_callable=io.StringIO):
            simulator.retain("created", UDID, RUNTIME, "x" * 513, self.environment)
        self.assertFalse((self.root / "buster-ci").exists())

    def test_existing_receipt_is_not_overwritten_and_discovery_stays_unchanged(self):
        first = self.parser(self.data())
        self.assertEqual(first.returncode, 0)
        path = self.root / "buster-ci/ios-simulator-selection.json"
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        second = self.parser(self.data(dict(udid="87654321-4321-4321-4321-CBA987654321")))
        self.assertEqual((second.returncode, second.stdout), (0, "87654321-4321-4321-4321-CBA987654321\n"))
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)
        self.assertIn("retention failed", second.stderr)


if __name__ == "__main__":
    unittest.main()
