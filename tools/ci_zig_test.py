#!/usr/bin/env python3
"""Network-free regression tests for Zig installation publication."""
import hashlib
import io
import json
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_zig


class _ZigFixture(unittest.TestCase):
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


class ZigPublishTests(_ZigFixture):
    def test_access_denied_retries_before_path_publication(self):
        original_rename = Path.rename
        observations = []

        def flaky_rename(path, destination):
            observations.append((path, destination, self.output.exists()))
            if len(observations) <= 4:
                raise self.access_error(5)
            return original_rename(path, destination)

        with mock.patch.object(ci_zig, "_running_on_windows", return_value=True), \
                mock.patch.object(ci_zig.Path, "rename", new=flaky_rename), \
                mock.patch.object(ci_zig.time, "sleep") as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run, \
                redirect_stderr(io.StringIO()) as diagnostics:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(len(observations), 5)
        self.assertTrue(all(not published for _, _, published in observations))
        self.assertEqual(sleep.call_args_list, [mock.call(delay) for delay in (0.25, 0.5, 1, 2)])
        for attempt, delay in enumerate((0.25, 0.5, 1, 2), 1):
            self.assertIn(f"attempt={attempt} next_attempt={attempt + 1} winerror=5 sleep_seconds={delay:g}",
                          diagnostics.getvalue())
        self.assertEqual(run.call_count, 2)
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
        self.assertEqual(len(attempts), 7)
        self.assertEqual(sleep.call_args_list,
                         [mock.call(delay) for delay in (0.25, 0.5, 1, 2, 4, 4)])
        self.assertEqual(sum(call.args[0] for call in sleep.call_args_list), 11.75)
        self.assertEqual(run.call_count, 2)
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
        self.assertEqual(run.call_count, 2)
        sleep.assert_not_called()
        self.assertFalse(self.installed.exists())
        self.assertFalse(self.output.exists())

    def test_destination_appearing_while_sleeping_stops_before_another_rename(self):
        attempts = []

        def denied_rename(path, destination):
            attempts.append((path, destination))
            raise self.access_error(5)

        def create_owner(delay):
            self.installed.mkdir()
            (self.installed / "owner").write_text("independent")

        with mock.patch.object(ci_zig, "_running_on_windows", return_value=True), \
                mock.patch.object(ci_zig.Path, "rename", new=denied_rename), \
                mock.patch.object(ci_zig.time, "sleep", side_effect=create_owner) as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            with self.assertRaisesRegex(ValueError, "appeared during publication"):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(len(attempts), 1)
        sleep.assert_called_once_with(0.25)
        self.assertEqual(run.call_count, 2)
        self.assertEqual((self.installed / "owner").read_text(), "independent")
        self.assertFalse(self.output.exists())

    def test_non_windows_access_denied_is_not_retried(self):
        with mock.patch.object(ci_zig, "_running_on_windows", return_value=False), \
                mock.patch.object(ci_zig.Path, "replace", side_effect=self.access_error(5)) as replace, \
                mock.patch.object(ci_zig.time, "sleep") as sleep, \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            with self.assertRaises(PermissionError):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        self.assertEqual(replace.call_count, 1)
        sleep.assert_not_called()
        self.assertEqual(run.call_count, 2)
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


class ZigStagingTests(_ZigFixture):
    def run_stage(self, command, **kwargs):
        self.assertEqual(kwargs, {"check": True, "capture_output": True, "text": True})
        self.assertFalse(self.output.exists())
        if command[1] == "-xf":
            staging = Path(command[4])
            self.stages.append(staging)
            self.assertEqual(list(staging.iterdir()), [])
            if len(self.stages) > 1:
                self.assertNotEqual(staging, self.stages[-2])
                self.assertFalse(self.stages[-2].parent.exists())
            (staging / "zig").write_text("controlled executable")
        else:
            self.assertEqual(command, [str(self.stages[-1] / "zig"), "version"])
        return subprocess.CompletedProcess(command, 0, stdout="0.16.0\n", stderr="")

    def test_successful_extraction_output_remains_visible(self):
        self.stages = []

        def run(command, **kwargs):
            result = self.run_stage(command, **kwargs)
            if command[1] == "-xf":
                result.stdout = "extract output\n"
                result.stderr = "extract warning\n"
            return result

        with mock.patch.object(ci_zig.subprocess, "run", side_effect=run), \
                redirect_stdout(io.StringIO()) as output, redirect_stderr(io.StringIO()) as diagnostics:
            ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        self.assertIn("extract output", output.getvalue())
        self.assertIn("extract warning", diagnostics.getvalue())
        self.assertTrue(self.output.exists())

    def test_decoder_failure_reverifies_archive_and_uses_clean_staging(self):
        self.stages = []
        original_verify = ci_zig.verify_archive
        verification_counts = []

        def verify(archive, digest):
            verification_counts.append(len(self.stages))
            original_verify(archive, digest)

        def run(command, **kwargs):
            result = self.run_stage(command, **kwargs)
            if command[1] == "-xf" and len(self.stages) == 1:
                (self.stages[-1] / "partial").write_text("discard me")
                raise subprocess.CalledProcessError(1, command, output="partial output\n", stderr="decoder failure\n")
            return result

        with mock.patch.object(ci_zig, "verify_archive", side_effect=verify), \
                mock.patch.object(ci_zig.subprocess, "run", side_effect=run) as process, \
                mock.patch.object(ci_zig, "download_archive") as download, \
                redirect_stderr(io.StringIO()) as diagnostics, redirect_stdout(io.StringIO()) as evidence:
            ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)

        self.assertEqual(verification_counts, [0, 1])
        self.assertEqual(process.call_count, 3)
        download.assert_not_called()
        self.assertEqual(evidence.getvalue().count("ZIG_SETUP_ARCHIVE_VERIFIED"), 2)
        self.assertIn("phase=extract", diagnostics.getvalue())
        self.assertIn("partial output", diagnostics.getvalue())
        self.assertIn("decoder failure", diagnostics.getvalue())
        self.assertFalse((self.installed / "partial").exists())
        self.assertEqual((self.installed / "zig").read_text(), "controlled executable")
        self.assertEqual(self.output.read_text().strip(), str(self.installed.resolve()))

    def test_version_process_failure_retries_in_a_fresh_tree(self):
        self.stages = []

        def run(command, **kwargs):
            result = self.run_stage(command, **kwargs)
            if command[1] == "version" and len(self.stages) == 1:
                raise subprocess.CalledProcessError(7, command, output="version output\n", stderr="runner fault\n")
            return result

        with mock.patch.object(ci_zig.subprocess, "run", side_effect=run) as process, \
                redirect_stderr(io.StringIO()) as diagnostics:
            ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        self.assertEqual(len(self.stages), 2)
        self.assertEqual(process.call_count, 4)
        self.assertIn("phase=version", diagnostics.getvalue())
        self.assertIn("runner fault", diagnostics.getvalue())
        self.assertTrue(self.installed.is_dir())
        self.assertTrue(self.output.exists())

    def test_permanent_stage_failures_remain_bounded_and_unpublished(self):
        for phase in ("extract", "version"):
            self.stages = []

            def run(command, **kwargs):
                result = self.run_stage(command, **kwargs)
                if (command[1] == "-xf") == (phase == "extract"):
                    raise subprocess.CalledProcessError(9, command, stderr=phase + " permanently failed\n")
                return result

            with self.subTest(phase=phase), \
                    mock.patch.object(ci_zig.subprocess, "run", side_effect=run) as process, \
                    mock.patch.object(ci_zig, "download_archive") as download, \
                    redirect_stderr(io.StringIO()) as diagnostics:
                with self.assertRaises(subprocess.CalledProcessError):
                    ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
            self.assertEqual(len(self.stages), 2)
            self.assertEqual(process.call_count, 2 if phase == "extract" else 4)
            self.assertEqual(diagnostics.getvalue().count("ZIG_SETUP_STAGE_RETRY"), 1)
            self.assertEqual(diagnostics.getvalue().count(phase + " permanently failed"), 2)
            download.assert_not_called()
            self.assertFalse(self.installed.exists())
            self.assertFalse(self.output.exists())
            self.assertEqual(list(self.root.glob("buster-zig-*")), [])

    def test_changed_archive_is_rejected_before_retry_execution(self):
        self.stages = []

        def run(command, **kwargs):
            self.run_stage(command, **kwargs)
            (self.cache / "archive").write_bytes(b"changed after initial verification")
            raise subprocess.CalledProcessError(1, command, stderr="decoder failure\n")

        with mock.patch.object(ci_zig.subprocess, "run", side_effect=run) as process, \
                mock.patch.object(ci_zig, "download_archive") as download:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        self.assertEqual(process.call_count, 1)
        download.assert_not_called()
        self.assertFalse(self.installed.exists())
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob("buster-zig-*")), [])

    def test_initial_checksum_mismatch_never_executes_a_subprocess(self):
        (self.cache / "archive").write_bytes(b"untrusted archive")
        with mock.patch.object(ci_zig.subprocess, "run") as process, \
                mock.patch.object(ci_zig, "download_archive") as download:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        process.assert_not_called()
        download.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_successful_wrong_version_is_an_identity_failure_without_retry(self):
        with mock.patch.object(ci_zig.subprocess, "run") as process:
            process.return_value = subprocess.CompletedProcess([], 0, stdout="0.15.0\n", stderr="")
            with self.assertRaisesRegex(ValueError, "pinned version"):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        self.assertEqual(process.call_count, 2)
        self.assertFalse(self.installed.exists())
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob("buster-zig-*")), [])

    def test_subprocess_launch_failure_does_not_retry(self):
        with mock.patch.object(ci_zig.subprocess, "run", side_effect=FileNotFoundError("missing tar")) as process:
            with self.assertRaises(FileNotFoundError):
                ci_zig.install("x86_64-linux", self.manifest, self.cache, self.installed, self.output)
        self.assertEqual(process.call_count, 1)
        self.assertFalse(self.installed.exists())
        self.assertFalse(self.output.exists())



if __name__ == "__main__":
    unittest.main()
