#!/usr/bin/env python3
"""Finite and offline GNU timeout selection controls, without a simulator."""

import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import gnu_timeout


ROOT = Path(__file__).resolve().parents[1]
BANNER = "timeout (GNU coreutils) 9.7\n"


@unittest.skipUnless(os.name == "posix", "POSIX timer probe custody required")
class TimeoutSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="buster-gnu-timeout-")
        self.root = Path(self.temporary.name)
        self.evidence = io.StringIO()

    def tearDown(self):
        self.temporary.cleanup()

    def provider(self, name, body=None, directory=None):
        directory = self.root if directory is None else directory
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / name
        body = "sys.stdout.write(" + repr(BANNER) + ")\n" if body is None else body
        path.write_text("#!" + sys.executable + "\nimport os,pathlib,sys,time\n" + body, encoding="utf-8")
        path.chmod(0o700)
        return path

    def select(self, search_path=None):
        return gnu_timeout.select_timeout(search_path=str(self.root) if search_path is None else search_path,
                                          evidence=self.evidence)

    def test_prefixed_gnu_is_selected_before_unverified_bare_timer(self):
        preferred = self.provider("gnutimeout")
        marker = self.root / "bare-probed"
        self.provider("timeout", "pathlib.Path(" + repr(str(marker)) + ").touch()\nprint('timeout (uutils coreutils) 0.8')\n")
        self.assertEqual(self.select(), str(preferred.resolve()))
        self.assertFalse(marker.exists())
        record = json.loads(self.evidence.getvalue().removeprefix("BUSTER_IOS_TIMEOUT "))
        self.assertEqual(record, {"path": str(preferred.resolve()), "version": "9.7"})

    def test_gtimeout_and_verified_bare_fallbacks(self):
        for name in ("gtimeout", "timeout"):
            with self.subTest(provider=name), tempfile.TemporaryDirectory(dir=self.root) as directory:
                path = self.provider(name, directory=Path(directory))
                self.assertEqual(self.select(directory), str(path.resolve()))

    def test_unknown_preferred_provider_cannot_prevent_verified_fallback(self):
        self.provider("gnutimeout", "print('unverified timer')\n")
        valid = self.provider("gtimeout")
        self.assertEqual(self.select(), str(valid.resolve()))

    def test_selected_alias_resolves_once_to_absolute_path_with_spaces(self):
        directory = self.root / "tools with spaces"
        timer = self.provider("real timer", directory=directory)
        (self.root / "gtimeout").symlink_to(timer)
        self.assertEqual(self.select(), str(timer.resolve()))

    def test_only_version_is_executed_with_c_locale(self):
        recorded = self.root / "arguments.json"
        self.provider("timeout", "import json\npathlib.Path(" + repr(str(recorded)) + ").write_text(json.dumps([sys.argv[1:],os.environ['LC_ALL'],os.environ['LANG']]))\nsys.stdout.write(" + repr(BANNER) + ")\n")
        self.select()
        self.assertEqual(json.loads(recorded.read_text()), [["--version"], "C", "C"])

    def test_missing_and_non_executable_providers_refuse(self):
        with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "no candidate found"):
            self.select()
        path = self.provider("timeout")
        path.chmod(0o600)
        with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "no candidate found"):
            self.select()

    def test_unknown_malformed_duplicate_binary_and_nonzero_output_refuse(self):
        cases = (
            ("non-gnu", "print('timeout (uutils coreutils) 0.8')\n", "GNU timeout banner"),
            ("wrong-program", "print('echo (GNU coreutils) 9.7')\n", "GNU timeout banner"),
            ("malformed-version", "print('timeout (GNU coreutils) unknown')\n", "GNU timeout banner"),
            ("prefixed-noise", "print('noise');sys.stdout.write(" + repr(BANNER) + ")\n", "GNU timeout banner"),
            ("duplicate", "sys.stdout.write(" + repr(BANNER + BANNER) + ")\n", "GNU timeout banner"),
            ("binary", "sys.stdout.buffer.write(b'\\xff')\n", "not UTF-8"),
            ("nul", "sys.stdout.write(" + repr(BANNER + "\x00") + ")\n", "GNU timeout banner"),
            ("stderr", "sys.stdout.write(" + repr(BANNER) + ");sys.stderr.write('warning')\n", "wrote stderr"),
            ("nonzero", "sys.stdout.write(" + repr(BANNER) + ");sys.exit(17)\n", "exited 17"),
        )
        for name, body, reason in cases:
            with self.subTest(case=name), tempfile.TemporaryDirectory(dir=self.root) as directory:
                self.provider("timeout", body, Path(directory))
                with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, reason):
                    self.select(directory)

    def test_oversized_stdout_or_stderr_is_refused_while_reading(self):
        for stream in ("stdout", "stderr"):
            with self.subTest(stream=stream), tempfile.TemporaryDirectory(dir=self.root) as directory:
                self.provider("timeout", "sys." + stream + ".write('X'*100000)\n", Path(directory))
                with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "exceeds bound"):
                    self.select(directory)

    def test_hanging_version_probe_is_bounded_without_candidate_timer(self):
        marker = self.root / "after-hang"
        self.provider("timeout", "time.sleep(.6);pathlib.Path(" + repr(str(marker)) + ").touch()\n")
        started = time.monotonic()
        with mock.patch.object(gnu_timeout, "VERSION_TIMEOUT_SECONDS", .1):
            with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "deadline"):
                self.select()
        self.assertLess(time.monotonic() - started, 1)
        time.sleep(.65)
        self.assertFalse(marker.exists())

    def test_inherited_pipe_writer_remains_inside_owned_probe_cleanup(self):
        marker = self.root / "descendant-after-bound"
        self.provider("timeout", "child=os.fork()\nif child==0:\n time.sleep(.6);pathlib.Path(" + repr(str(marker)) + ").touch();os._exit(0)\nsys.stdout.write(" + repr(BANNER) + ")\n")
        with mock.patch.object(gnu_timeout, "VERSION_TIMEOUT_SECONDS", .1):
            with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "deadline"):
                self.select()
        time.sleep(.65)
        self.assertFalse(marker.exists())

    def test_control_character_path_refuses_without_execution(self):
        directory = self.root / "invalid\npath"
        marker = self.root / "must-not-probe"
        self.provider("timeout", "pathlib.Path(" + repr(str(marker)) + ").touch()\n", directory)
        with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "control character"):
            self.select(str(directory))
        self.assertFalse(marker.exists())

    def test_launch_failure_refuses(self):
        self.provider("timeout")
        with mock.patch.object(gnu_timeout.subprocess, "Popen", side_effect=OSError("launch refused")):
            with self.assertRaisesRegex(gnu_timeout.TimeoutSelectionError, "probe unavailable"):
                self.select()

    def test_cleanup_failure_cannot_fall_back_to_another_provider(self):
        self.provider("gnutimeout")
        self.provider("gtimeout")
        with mock.patch.object(gnu_timeout, "probe_version", side_effect=gnu_timeout.ProbeCleanupError("cleanup refused")) as probe:
            with self.assertRaisesRegex(gnu_timeout.ProbeCleanupError, "cleanup refused"):
                self.select()
        self.assertEqual(probe.call_count, 1)
        self.assertEqual(self.evidence.getvalue(), "")

    def test_cli_has_only_absolute_path_on_stdout_and_provider_evidence_on_stderr(self):
        timer = self.provider("gtimeout")
        result = subprocess.run([sys.executable, str(ROOT / "ios/gnu_timeout.py")],
                                env=dict(os.environ, PATH=str(self.root)), capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, str(timer.resolve()) + "\n")
        self.assertEqual(json.loads(result.stderr.removeprefix("BUSTER_IOS_TIMEOUT ")),
                         {"path": str(timer.resolve()), "version": "9.7"})

    def test_cli_missing_provider_fails_without_stdout(self):
        result = subprocess.run([sys.executable, str(ROOT / "ios/gnu_timeout.py")],
                                env=dict(os.environ, PATH=str(self.root)), capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("no positively verified GNU timeout", result.stderr)

    def test_cli_cleanup_failure_has_distinct_fatal_status(self):
        with mock.patch.object(gnu_timeout.sys, "argv", ["gnu_timeout.py"]), \
                mock.patch.object(gnu_timeout, "select_timeout", side_effect=gnu_timeout.ProbeCleanupError("cleanup refused")), \
                mock.patch.object(gnu_timeout.sys, "stderr", new=io.StringIO()) as errors, \
                mock.patch.object(gnu_timeout.sys, "stdout", new=io.StringIO()) as output:
            self.assertEqual(gnu_timeout.main(), 2)
        self.assertEqual(output.getvalue(), "")
        self.assertIn("cleanup refused", errors.getvalue())

    def test_launcher_refuses_unknown_provider_before_invoking_native_tools(self):
        marker = self.root / "must-not-run-native"
        body = "pathlib.Path(" + repr(str(marker)) + ").touch()\n"
        self.provider("xcrun", body)
        self.provider("codesign", body)
        self.provider("timeout", "print('timeout (uutils coreutils) 0.8')\n")
        (self.root / "python3").symlink_to(sys.executable)
        (self.root / "dirname").symlink_to(shutil.which("dirname"))
        app = self.root / "ide.app"
        app.mkdir()
        result = subprocess.run(["/bin/bash", str(ROOT / "ios/launch_simulator.sh"), "--batch", "Test", str(app)],
                                env=dict(os.environ, PATH=str(self.root)), capture_output=True, text=True, timeout=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no positively verified GNU timeout", result.stderr)
        self.assertFalse(marker.exists())

    def test_both_consumers_use_shared_selection_and_keep_timer_arguments(self):
        launcher = (ROOT / "ios/launch_simulator.sh").read_text()
        bridge = (ROOT / "ios/lifecycle_capture_bridge_test.py").read_text()
        self.assertIn('timeout_bin=$(python3 "$(dirname "${BASH_SOURCE[0]}")/gnu_timeout.py")', launcher)
        self.assertIn("TIMEOUT = gnu_timeout.select_timeout()", bridge)
        self.assertNotIn('shutil.which("timeout")', bridge)
        self.assertIn('"$timeout_bin" --signal=KILL "$((seconds + 10 + monitor_command_timeout_seconds))s"', launcher)
        self.assertIn('"$timeout_bin" --kill-after=10s "${launch_timeout_seconds}s"', launcher)
        self.assertIn('TIMEOUT, "--signal=KILL", "%ds" % capture_seconds', bridge)
        self.assertIn('entered_ns + 13_300_000_000', bridge)


if __name__ == "__main__":
    unittest.main()
