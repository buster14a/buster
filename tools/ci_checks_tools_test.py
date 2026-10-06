#!/usr/bin/env python3
"""Hermetic selected-tool controls; no compiler or qualification run executes."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

import ci_checks_tools as observer

ENVIRONMENT = {"BUSTER_CI_CONDITIONS_EVIDENCE": "1", "GITHUB_REPOSITORY": "buster14a/buster",
               "GITHUB_SHA": "a" * 40, "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "1", "GITHUB_JOB": "lint"}


class SelectedToolTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.executable = self.root / "selected.exe"
        self.executable.write_bytes(b"synthetic selected executable\n")
        self.executable.chmod(0o755)
        self.output = self.root / "receipts" / "selected.json"
        self.cache = self.root / "CMakeCache.txt"

    def arguments(self, tool="go", cache=False):
        selection = ["--cmake-cache", str(self.cache)] if cache else ["--executable", str(self.executable)]
        return ["--tool", tool, *selection, "--output", str(self.output)]

    def invoke(self, arguments=None, environment=None, version="go version go1.26.0 linux/amd64\n", side_effect=None):
        with mock.patch.dict(os.environ, ENVIRONMENT if environment is None else environment, clear=True), \
                mock.patch.object(observer, "version_output", return_value=version, side_effect=side_effect) as probe:
            result = observer.main(self.arguments() if arguments is None else arguments)
        receipt = json.loads(self.output.read_text(encoding="utf-8"))
        return result, receipt, probe

    def test_success_keeps_binding_and_paths_out_of_comparable_fields(self):
        result, receipt, probe = self.invoke()
        self.assertEqual(result, 0)
        self.assertEqual(receipt["schema"], "buster-ci-selected-tool-v1")
        self.assertEqual(receipt["status"], "observed")
        for key, name in observer.BINDINGS:
            self.assertEqual(receipt[key], ENVIRONMENT[name])
        self.assertEqual(receipt["comparable"], {"sha256": hashlib.sha256(self.executable.read_bytes()).hexdigest(),
                                                 "version": "go version go1.26.0 linux/amd64\n"})
        resolved = str(self.executable.resolve())
        self.assertEqual(receipt["selection"], {"kind": "executable", "requested": str(self.executable),
                                                "path": str(self.executable), "resolved_path": resolved})
        self.assertEqual(receipt["command"], [resolved, "version"])
        probe.assert_called_once_with([resolved, "version"])

    def test_alternate_valid_bindings_are_retained_exactly(self):
        environment = {**ENVIRONMENT, "GITHUB_REPOSITORY": "different_owner/repository", "GITHUB_SHA": "b" * 40,
                       "GITHUB_RUN_ID": "987654", "GITHUB_RUN_ATTEMPT": "2", "GITHUB_JOB": "ios_test_ci"}
        result, receipt, _ = self.invoke(environment=environment)
        self.assertEqual(result, 0)
        self.assertEqual({key: receipt[key] for key, _ in observer.BINDINGS},
                         {key: environment[name] for key, name in observer.BINDINGS})

    def test_disabled_gate_has_no_parsing_probes_or_output(self):
        for flag in (None, "", "0", "true", "01"):
            environment = {} if flag is None else {"BUSTER_CI_CONDITIONS_EVIDENCE": flag}
            with self.subTest(flag=flag), mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(observer.argparse, "ArgumentParser", side_effect=AssertionError("parsed")), \
                    mock.patch.object(observer, "binding", side_effect=AssertionError("bound")), \
                    mock.patch.object(observer, "observe", side_effect=AssertionError("probed")), \
                    mock.patch.object(observer, "write_receipt", side_effect=AssertionError("wrote")), \
                    mock.patch.object(observer.subprocess, "Popen", side_effect=AssertionError("spawned")):
                self.assertEqual(observer.main(["invalid arguments"]), 0)
        self.assertFalse(self.output.exists())

    def test_missing_or_invalid_bindings_remain_unknown_without_a_probe(self):
        bad = {"GITHUB_REPOSITORY": "no-owner", "GITHUB_SHA": "a" * 39, "GITHUB_RUN_ID": "0",
               "GITHUB_RUN_ATTEMPT": "01", "GITHUB_JOB": "job with spaces"}
        for name, invalid in bad.items():
            for value in (None, invalid):
                environment = dict(ENVIRONMENT)
                if value is None:
                    del environment[name]
                else:
                    environment[name] = value
                with self.subTest(name=name, value=value):
                    result, receipt, probe = self.invoke(environment=environment)
                    self.assertEqual(result, 1)
                    self.assertEqual(receipt["status"], "unknown")
                    self.assertIn("binding", receipt["reason"])
                    self.assertNotIn("comparable", receipt)
                    probe.assert_not_called()

    def test_cmake_selection_uses_the_configured_tool_and_fixed_command(self):
        for kind in ("FILEPATH", "STRING"):
            self.cache.write_text(f"// selected generator\nCMAKE_MAKE_PROGRAM:{kind}={self.executable}\n", encoding="utf-8")
            with self.subTest(kind=kind), mock.patch.object(observer.shutil, "which", side_effect=AssertionError("PATH lookup")):
                result, receipt, probe = self.invoke(self.arguments("ninja", cache=True), version="1.13.2\n")
            self.assertEqual(result, 0)
            self.assertEqual(receipt["selection"]["kind"], "cmake-cache")
            self.assertEqual(receipt["selection"]["cmake_cache"], str(self.cache.resolve()))
            probe.assert_called_once_with([str(self.executable.resolve()), "--version"])

    def test_bad_cmake_selection_never_runs_a_probe(self):
        records = ("", "CMAKE_MAKE_PROGRAM:FILEPATH=relative/ninja\n", "CMAKE_MAKE_PROGRAM:FILEPATH=\n",
                   f"CMAKE_MAKE_PROGRAM:INTERNAL={self.executable}\n",
                   f"CMAKE_MAKE_PROGRAM:FILEPATH={self.executable}\nCMAKE_MAKE_PROGRAM:STRING={self.executable}\n",
                   f"CMAKE_MAKE_PROGRAM:FILEPATH={self.root}\n")
        for record in records:
            self.cache.write_text(record, encoding="utf-8")
            with self.subTest(record=record):
                result, receipt, probe = self.invoke(self.arguments("ninja", cache=True))
                self.assertEqual(result, 1)
                self.assertEqual(receipt["status"], "unknown")
                self.assertNotIn("comparable", receipt)
                probe.assert_not_called()
        self.cache.write_text(f"CMAKE_MAKE_PROGRAM:FILEPATH={self.executable}\n", encoding="utf-8")
        result, receipt, probe = self.invoke(self.arguments("adb", cache=True))
        self.assertEqual(result, 1)
        self.assertIn("only for Ninja", receipt["reason"])
        probe.assert_not_called()

    def test_cache_read_is_bounded_and_requires_utf8_and_a_regular_file(self):
        self.cache.write_bytes(b"#" * 65)
        with mock.patch.object(observer, "MAX_CACHE_BYTES", 64), self.assertRaisesRegex(observer.EvidenceError, "bound"):
            observer.cmake_selection(self.cache)
        self.cache.write_bytes(b"\xff\n")
        with self.assertRaisesRegex(observer.EvidenceError, "UTF-8"):
            observer.cmake_selection(self.cache)
        with self.assertRaisesRegex(observer.EvidenceError, "regular"):
            observer.cmake_selection(self.root)

    def test_adb_selection_only_runs_version(self):
        result, receipt, probe = self.invoke(self.arguments("adb"), version="Android Debug Bridge version 1.0.41\n")
        self.assertEqual(result, 0)
        self.assertEqual(receipt["tool"], "adb")
        probe.assert_called_once_with([str(self.executable.resolve()), "version"])

    def test_named_executable_keeps_actual_selected_path(self):
        args = self.arguments()
        args[3] = "go"
        with mock.patch.object(observer.shutil, "which", return_value=str(self.executable)) as lookup:
            result, receipt, _ = self.invoke(args)
        self.assertEqual(result, 0)
        self.assertEqual(receipt["selection"]["requested"], "go")
        self.assertEqual(receipt["selection"]["path"], str(self.executable))
        lookup.assert_called_once_with("go")

    def test_unavailable_executable_is_unknown(self):
        with mock.patch.object(observer.shutil, "which", return_value=None):
            result, receipt, probe = self.invoke()
        self.assertEqual(result, 1)
        self.assertEqual(receipt["status"], "unknown")
        self.assertNotIn("comparable", receipt)
        probe.assert_not_called()

    def test_binary_content_change_and_same_bytes_replacement_fail(self):
        for same_bytes in (False, True):
            def replace(_command):
                replacement = self.root / "replacement.exe"
                replacement.write_bytes(self.executable.read_bytes() if same_bytes else b"different executable\n")
                replacement.chmod(0o755)
                os.replace(replacement, self.executable)
                return "synthetic version\n"

            with self.subTest(same_bytes=same_bytes):
                result, receipt, _ = self.invoke(side_effect=replace)
                self.assertEqual(result, 1)
                self.assertEqual(receipt["status"], "failed")
                self.assertIn("changed", receipt["reason"])
                self.assertNotIn("comparable", receipt)

    def test_stable_symlink_is_observed_and_retargeting_fails(self):
        link = self.root / "link.exe"
        try:
            link.symlink_to(self.executable)
        except OSError:
            self.skipTest("symlink creation is unavailable")
        args = self.arguments()
        args[3] = str(link)
        result, receipt, probe = self.invoke(args)
        self.assertEqual(result, 0)
        self.assertEqual(receipt["selection"]["path"], str(link))
        self.assertEqual(receipt["selection"]["resolved_path"], str(self.executable.resolve()))
        probe.assert_called_once_with([str(self.executable.resolve()), "version"])
        replacement = self.root / "other.exe"
        replacement.write_bytes(self.executable.read_bytes())
        replacement.chmod(0o755)

        def retarget(_command):
            link.unlink()
            link.symlink_to(replacement)
            return "synthetic version\n"

        result, receipt, _ = self.invoke(args, side_effect=retarget)
        self.assertEqual(result, 1)
        self.assertEqual(receipt["status"], "failed")
        self.assertNotIn("comparable", receipt)

    def test_failed_version_receipts_are_not_comparable(self):
        for reason in ("version command exceeded deadline", "version output exceeds observation bound", "version command exited with status 7"):
            with self.subTest(reason=reason):
                result, receipt, _ = self.invoke(side_effect=observer.EvidenceError(reason, "failed"))
                self.assertEqual(result, 1)
                self.assertEqual(receipt["status"], "failed")
                self.assertEqual(receipt["reason"], reason)
                self.assertNotIn("comparable", receipt)

    def test_version_capture_retains_exact_combined_utf8_output(self):
        command = [sys.executable, "-c", "import os; os.write(1, b'first\\n'); os.write(2, b'second\\n')"]
        self.assertEqual(observer.version_output(command), "first\nsecond\n")

    def test_version_deadline_output_bound_nonzero_empty_and_invalid_utf8(self):
        cases = (("import time; time.sleep(5)", "deadline", 0.05),
                 ("import os,time; os.write(1,b'version\\n'); os.close(1); os.close(2); time.sleep(5)", "deadline", 0.1),
                 (f"import os; os.write(1, b'x' * {observer.MAX_OUTPUT_BYTES + 1})", "bound", 5),
                 ("import sys; print('version'); sys.exit(7)", "status 7", 5),
                 ("print('   ')", "empty", 5), ("import os; os.write(1, b'\\xff')", "UTF-8", 5))
        for script, message, timeout in cases:
            with self.subTest(message=message), self.assertRaisesRegex(observer.EvidenceError, message):
                observer.version_output([sys.executable, "-c", script], timeout=timeout)
        command = [sys.executable, "-c", f"import os; os.write(1, b'x' * {observer.MAX_OUTPUT_BYTES})"]
        self.assertEqual(len(observer.version_output(command)), observer.MAX_OUTPUT_BYTES)

    @unittest.skipUnless(os.name == "posix", "process-group cleanup is POSIX-only")
    def test_deadline_closes_descendant_pipe_after_the_leader_exits(self):
        script = "import subprocess, sys; subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(5)'])"
        stopped = []
        original_stop = observer.stop_process

        def stop(child):
            self.assertIsNone(child.returncode, "leader must retain unreaped cleanup ownership")
            self.assertEqual(os.getpgid(child.pid), child.pid)
            stopped.append(child)
            original_stop(child)
            self.assertEqual(child.returncode, 0, "leader had already exited before cleanup")

        started = time.monotonic()
        with mock.patch.object(observer, "stop_process", side_effect=stop), \
                self.assertRaisesRegex(observer.EvidenceError, "deadline"):
            observer.version_output([sys.executable, "-c", script], timeout=0.5)
        self.assertEqual(len(stopped), 1)
        self.assertTrue(stopped[0].stdout.closed, "descendant must release the inherited capture pipe")
        self.assertLess(time.monotonic() - started, 1.5, "cleanup must finish before the descendant's sleep")

    @unittest.skipUnless(os.name == "posix", "process-group cleanup is POSIX-only")
    def test_escaped_pipe_holder_never_releases_leader_ownership_before_killpg(self):
        pid_file = self.root / "escaped.pid"
        release_file = self.root / "release"
        holder = ("import pathlib, time\n"
                  f"release = pathlib.Path({str(release_file)!r})\n"
                  "deadline = time.monotonic() + 5\n"
                  "while not release.exists() and time.monotonic() < deadline:\n    time.sleep(0.01)\n")
        script = ("import pathlib, subprocess, sys; "
                  f"child = subprocess.Popen([sys.executable, '-c', {holder!r}], start_new_session=True); "
                  f"pathlib.Path({str(pid_file)!r}).write_text(str(child.pid))")
        stopped = []
        original_stop = observer.stop_process

        def stop(child):
            self.assertIsNone(child.returncode, "escaped holder cannot release leader's unreaped PID reservation")
            self.assertEqual(os.getpgid(child.pid), child.pid)
            stopped.append(child)
            original_stop(child)
            self.assertEqual(child.returncode, 0, "leader exited normally before group cleanup")
            escaped = int(pid_file.read_text())
            self.assertEqual(os.getpgid(escaped), escaped, "escaped child remains outside the owned group")
            release_file.touch()  # The fixture, rather than the observer, closes the escaped holder.

        try:
            with mock.patch.object(observer, "stop_process", side_effect=stop), \
                    mock.patch.object(observer.subprocess.Popen, "poll", side_effect=AssertionError("premature reap")), \
                    self.assertRaisesRegex(observer.EvidenceError, "deadline"):
                observer.version_output([sys.executable, "-c", script], timeout=0.5)
            self.assertEqual(len(stopped), 1)
            self.assertTrue(stopped[0].stdout.closed)
        finally:
            release_file.touch()


if __name__ == "__main__":
    unittest.main()
