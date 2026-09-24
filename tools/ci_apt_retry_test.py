#!/usr/bin/env python3
"""Offline failure and recovery controls for pinned apt snapshot transport."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import ci_apt


class SnapshotRetryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.lock = self.root / "lock.json"
        self.data = json.loads((ci_apt.ROOT / ".github/apt-inputs.json").read_text())
        self.lock.write_text(json.dumps(self.data))
        self.snapshot = "https://snapshot.ubuntu.com/ubuntu/" + self.data["snapshot"] + "/"

    def failed_fetch(self, code=503, url=None, extra=""):
        url = url or self.snapshot + "pool/universe/l/llvm-toolchain-18/clang-18.deb"
        return subprocess.CalledProcessError(
            100, ["apt-get"], output="",
            stderr=f"E: Failed to fetch {url}  {code} Service Unavailable\n"
                   "E: Unable to fetch some archives, maybe run apt update or try with --fix-missing?\n" + extra)

    def simulate_install(self, update_failures=0, install_failures=0):
        commands = []
        original_read = Path.read_text
        original_is_file = Path.is_file

        def read(path, *args, **kwargs):
            if str(path) == "/etc/os-release":
                return 'ID=ubuntu\nVERSION_ID="26.04"\n'
            return original_read(path, *args, **kwargs)

        def is_file(path):
            return str(path) == ci_apt.KEYRING or original_is_file(path)

        def run(argv):
            commands.append(argv)
            if argv == ["dpkg", "--print-architecture"]:
                return "amd64\n"
            if "update" in argv and sum("update" in cmd for cmd in commands) <= update_failures:
                raise self.failed_fetch()
            if "install" in argv and sum("install" in cmd for cmd in commands) <= install_failures:
                raise self.failed_fetch()
            return ""

        with mock.patch.object(Path, "read_text", read), mock.patch.object(Path, "is_file", is_file), \
                mock.patch.object(ci_apt, "Commands", return_value=run), \
                mock.patch.object(ci_apt, "inspect") as inspect, \
                mock.patch.object(ci_apt.time, "sleep") as sleep:
            if update_failures >= 3 or install_failures >= 3:
                with self.assertRaises(subprocess.CalledProcessError):
                    ci_apt.install("gpu", self.lock, self.root / "evidence")
            else:
                ci_apt.install("gpu", self.lock, self.root / "evidence")
            counts = (sum("update" in cmd for cmd in commands),
                      sum("install" in cmd for cmd in commands))
            inspected = inspect.call_count
            waits = [call.args[0] for call in sleep.call_args_list]
        return commands, counts, inspected, waits

    def test_update_and_install_recover_using_the_same_signed_pin(self):
        commands, counts, inspected, waits = self.simulate_install(update_failures=1, install_failures=1)
        self.assertEqual((counts, inspected, waits), ((2, 2), 1, [30, 30]))
        updates = [cmd for cmd in commands if "update" in cmd]
        installs = [cmd for cmd in commands if "install" in cmd]
        self.assertEqual(updates[0], updates[1])
        self.assertEqual(installs[0], installs[1])
        self.assertIn("APT::Update::Error-Mode=any", " ".join(updates[0]))
        self.assertIn("--no-remove", installs[0])
        self.assertTrue(all(f"{name}={version}" in installs[0]
                            for name, version in self.data["profiles"]["gpu"].items()))
        sources = (self.root / "evidence/snapshot.sources").read_text()
        self.assertIn(self.snapshot, sources)
        self.assertIn("Signed-By: " + ci_apt.KEYRING, sources)

    def test_exhausted_update_never_installs_or_inspects(self):
        _, counts, inspected, waits = self.simulate_install(update_failures=3)
        self.assertEqual((counts, inspected, waits), ((3, 0), 0, [30, 60]))
        self.assertFalse((self.root / "evidence/pinned.identity.json").exists())

    def test_exhausted_install_never_inspects(self):
        _, counts, inspected, waits = self.simulate_install(install_failures=3)
        self.assertEqual((counts, inspected, waits), ((1, 3), 0, [30, 60]))
        self.assertFalse((self.root / "evidence/selected.identity.json").exists())

    def test_nontransient_or_mixed_failures_are_not_retried(self):
        for error in (
            self.failed_fetch(code=404),
            self.failed_fetch(url="https://archive.ubuntu.com/ubuntu/pkg.deb"),
            self.failed_fetch(extra="E: The repository is not signed.\n"),
            self.failed_fetch(extra="E: Failed to fetch " + self.snapshot + "pkg.deb  404 Not Found\n"),
            self.failed_fetch(extra="W: GPG error: missing key\n"),
            subprocess.CalledProcessError(100, ["apt-get"], stderr="E: Version '1' for 'clang-18' was not found\n"),
        ):
            with self.subTest(error=error.stderr):
                commands = []

                def run(argv):
                    commands.append(argv)
                    raise error

                with mock.patch.object(ci_apt.time, "sleep") as sleep, \
                        self.assertRaises(subprocess.CalledProcessError):
                    ci_apt.run_snapshot_apt(run, ["apt-get", "update"], self.data)
                self.assertEqual(commands, [["apt-get", "update"]])
                sleep.assert_not_called()

    def test_each_attempt_retains_status_and_diagnostics(self):
        results = [subprocess.CompletedProcess(["apt-get"], 100, "", self.failed_fetch().stderr),
                   subprocess.CompletedProcess(["apt-get"], 100, "", self.failed_fetch(code=502).stderr),
                   subprocess.CompletedProcess(["apt-get"], 0, "done\n", "")]
        with mock.patch.object(ci_apt.subprocess, "run", side_effect=results) as process, \
                mock.patch.object(ci_apt.time, "sleep") as sleep:
            output = ci_apt.run_snapshot_apt(ci_apt.Commands(self.root), ["apt-get", "install"], self.data)
        self.assertEqual(output, "done\n")
        self.assertEqual(process.call_count, 3)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [30, 60])
        self.assertEqual([json.loads((self.root / f"command-{i:03}.json").read_text())["status"]
                          for i in range(1, 4)], [100, 100, 0])
        self.assertIn("503", (self.root / "command-001.stderr").read_text())
        self.assertIn("502", (self.root / "command-002.stderr").read_text())
        self.assertEqual((self.root / "command-003.stdout").read_text(), "done\n")


if __name__ == "__main__":
    unittest.main()
