#!/usr/bin/env python3
"""Network-free regression tests for Zig installation publication."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_zig


class ZigPublishTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.payload = b"verified test fixture"
        data = json.loads((ROOT / ".github/zig.json").read_text())
        data["sha256"]["x86_64-linux"] = hashlib.sha256(self.payload).hexdigest()
        self.manifest = self.root / "zig.json"
        self.manifest.write_text(json.dumps(data))
        self.cache = self.root / "cache"
        self.cache.mkdir()
        (self.cache / "archive").write_bytes(self.payload)
        self.installed = self.root / "install"
        self.output = self.root / "path"

    @staticmethod
    def access_error(winerror, message="Access is denied"):
        error = PermissionError(message)
        error.winerror = winerror
        return error

    def test_access_denied_retries_before_path_publication(self):
        original_rename = Path.rename
        observations = []

        def flaky_rename(path, destination):
            observations.append((path, destination, self.output.exists()))
            if len(observations) == 1:
                raise self.access_error(5)
            return original_rename(path, destination)

        with mock.patch.object(ci_zig, "_running_on_windows", return_value=True), \
                mock.patch.object(ci_zig.Path, "rename", new=flaky_rename), \
                mock.patch.object(ci_zig.time, "sleep") as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(len(observations), 2)
        self.assertFalse(observations[0][2])
        self.assertFalse(observations[1][2])
        sleep.assert_called_once_with(ci_zig.INSTALL_PUBLISH_RETRY_SECONDS)
        self.assertTrue(self.installed.is_dir())
        self.assertEqual(self.output.read_text().strip(), str(self.installed.resolve()))

    def test_access_denied_retries_are_bounded(self):
        attempts = []

        def denied_rename(path, destination):
            attempts.append((path, destination))
            raise self.access_error(5)

        with mock.patch.object(ci_zig, "_running_on_windows", return_value=True), \
                mock.patch.object(ci_zig.Path, "rename", new=denied_rename), \
                mock.patch.object(ci_zig.time, "sleep") as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            with self.assertRaises(PermissionError):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(len(attempts), ci_zig.INSTALL_PUBLISH_ATTEMPTS)
        self.assertEqual(sleep.call_count, ci_zig.INSTALL_PUBLISH_ATTEMPTS - 1)
        self.assertFalse(self.installed.exists())
        self.assertFalse(self.output.exists())

    def test_unrelated_windows_error_is_not_retried(self):
        attempts = []

        def denied_rename(path, destination):
            attempts.append((path, destination))
            raise self.access_error(32, "Sharing violation")

        with mock.patch.object(ci_zig, "_running_on_windows", return_value=True), \
                mock.patch.object(ci_zig.Path, "rename", new=denied_rename), \
                mock.patch.object(ci_zig.time, "sleep") as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            with self.assertRaises(PermissionError):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(len(attempts), 1)
        sleep.assert_not_called()
        self.assertFalse(self.installed.exists())
        self.assertFalse(self.output.exists())

    def test_destination_appearing_during_retry_is_not_overwritten(self):
        attempts = []

        def racing_rename(path, destination):
            attempts.append((path, destination))
            destination.mkdir()
            (destination / "owner").write_text("independent")
            raise self.access_error(5)

        with mock.patch.object(ci_zig, "_running_on_windows", return_value=True), \
                mock.patch.object(ci_zig.Path, "rename", new=racing_rename), \
                mock.patch.object(ci_zig.time, "sleep") as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            with self.assertRaisesRegex(ValueError, "appeared during publication"):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(len(attempts), 1)
        sleep.assert_not_called()
        self.assertEqual((self.installed / "owner").read_text(), "independent")
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
