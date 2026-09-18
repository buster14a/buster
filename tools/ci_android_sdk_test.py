#!/usr/bin/env python3
"""Hermetic regression tests for the bounded Android SDK installer."""
from __future__ import annotations

import os
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_android_sdk  # noqa: E402
import mobile_coverage  # noqa: E402

PACKAGES = (
    "emulator",
    "platforms;android-35",
    "system-images;android-35;google_apis;x86_64",
)

FAKE_SDKMANAGER = r'''#!/usr/bin/env python3
from pathlib import Path
import os
import sys

root = Path(os.environ["ANDROID_SDK_ROOT"])
state = Path(os.environ["FAKE_SDKMANAGER_STATE"])
mode = os.environ["FAKE_SDKMANAGER_MODE"]
count = int(state.read_text(encoding="utf-8")) + 1 if state.exists() else 1
state.write_text(str(count), encoding="utf-8")
packages = sys.argv[sys.argv.index("--install") + 1:]


def write(path, data="fixture"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def valid(package):
    directory = root.joinpath(*package.split(";"))
    directory.mkdir(parents=True, exist_ok=True)
    if package == "emulator":
        for name in ("emulator", "emulator.exe"):
            executable = directory / name
            write(executable)
            executable.chmod(0o755)
    elif package.startswith("platforms;"):
        write(directory / "android.jar")
        write(directory / "source.properties")
    elif package.startswith("system-images;"):
        for name in ("source.properties", "system.img", "ramdisk.img", "kernel-ranchu"):
            write(directory / name)


def partial_image():
    directory = root / "system-images" / "android-35" / "google_apis" / "x86_64"
    write(directory / "source.properties")
    write(root / ".temp" / "current-download" / "fragment")


if mode == "success":
    for package in packages:
        valid(package)
    print("fake sdkmanager success")
    raise SystemExit(0)
if mode == "retry":
    if count == 1:
        partial_image()
        print("Warning: package preparation failed: Premature EOF.")
        raise SystemExit(1)
    if (root / ".temp" / "current-download").exists() or (root / "system-images" / "android-35" / "google_apis" / "x86_64").exists():
        print("unsafe cleanup boundary: stale partial state survived")
        raise SystemExit(9)
    for package in packages:
        valid(package)
    print("fake sdkmanager retry success")
    raise SystemExit(0)
if mode == "permanent":
    partial_image()
    print("Warning: package preparation failed: Premature EOF.")
    raise SystemExit(1)
if mode == "corrupt-success":
    partial_image()
    print("fake sdkmanager claimed success with a partial image")
    raise SystemExit(0)
raise SystemExit(2)
'''


class AndroidWorkflowContractTests(unittest.TestCase):
    def test_mobile_summary_tracks_sdk_setup_before_payload(self):
        lanes = mobile_coverage._workflow_mobile_lanes(ROOT / ".github/workflows/ci.yml")
        self.assertEqual(
            {(lane["os"], lane["arch"]) for lane in lanes},
            {("android", "x86_64"), ("ios", "x86_64"), ("ios", "aarch64")},
        )


class AndroidSdkInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.sdk = self.root / "sdk"
        self.logs = self.root / "logs"
        self.sdk.mkdir()
        self.fake = self.root / "fake_sdkmanager.py"
        self.fake.write_text(textwrap.dedent(FAKE_SDKMANAGER), encoding="utf-8")
        self.state = self.root / "state.txt"
        self.unrelated = self.sdk / "platform-tools" / "adb"
        self.unrelated.parent.mkdir(parents=True)
        self.unrelated.write_text("keep", encoding="utf-8")
        self.preexisting_temp = self.sdk / ".temp" / "preexisting" / "keep"
        self.preexisting_temp.parent.mkdir(parents=True)
        self.preexisting_temp.write_text("keep", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def run_installer(self, mode):
        environment = dict(os.environ)
        environment.update(
            {
                "FAKE_SDKMANAGER_MODE": mode,
                "FAKE_SDKMANAGER_STATE": str(self.state),
            }
        )
        return ci_android_sdk.install(
            [sys.executable, str(self.fake)],
            self.sdk,
            PACKAGES,
            self.logs,
            attempts=3,
            timeout_seconds=5,
            backoff_seconds=0,
            environment=environment,
        )

    def attempts(self):
        return int(self.state.read_text(encoding="utf-8"))

    def assert_unrelated_state_preserved(self):
        self.assertEqual(self.unrelated.read_text(encoding="utf-8"), "keep")
        self.assertEqual(self.preexisting_temp.read_text(encoding="utf-8"), "keep")

    def test_immediate_success(self):
        self.assertEqual(self.run_installer("success"), 0)
        self.assertEqual(self.attempts(), 1)
        self.assertFalse(ci_android_sdk.validate_packages(self.sdk.resolve(), PACKAGES))
        transcript = (self.logs / "android-sdk-install.log").read_text(encoding="utf-8")
        self.assertIn("ANDROID_SDK_INSTALL_RESULT status=success attempts=1", transcript)
        for package in PACKAGES:
            self.assertIn(package, transcript)
        self.assert_unrelated_state_preserved()

    def test_premature_eof_is_cleaned_and_retried(self):
        self.assertEqual(self.run_installer("retry"), 0)
        self.assertEqual(self.attempts(), 2)
        transcript = (self.logs / "android-sdk-install.log").read_text(encoding="utf-8")
        self.assertIn("Premature EOF", transcript)
        self.assertIn("ANDROID_SDK_RETRY next_attempt=2", transcript)
        self.assertIn("ANDROID_SDK_INSTALL_RESULT status=success attempts=2", transcript)
        self.assertFalse((self.sdk / ".temp" / "current-download").exists())
        self.assert_unrelated_state_preserved()

    def test_permanent_failure_stops_at_fixed_bound(self):
        self.assertEqual(self.run_installer("permanent"), 1)
        self.assertEqual(self.attempts(), 3)
        transcript = (self.logs / "android-sdk-install.log").read_text(encoding="utf-8")
        self.assertIn("ANDROID_SDK_INSTALL_RESULT status=failure attempts=3", transcript)
        self.assertEqual(transcript.count("Premature EOF"), 3)
        for attempt in range(1, 4):
            self.assertTrue((self.logs / f"android-sdk-install.attempt-{attempt}.log").is_file())
        self.assertFalse((self.sdk / "system-images" / "android-35" / "google_apis" / "x86_64").exists())
        self.assert_unrelated_state_preserved()

    def test_zero_exit_cannot_accept_partial_package(self):
        self.assertEqual(self.run_installer("corrupt-success"), 1)
        self.assertEqual(self.attempts(), 3)
        transcript = (self.logs / "android-sdk-install.log").read_text(encoding="utf-8")
        self.assertIn("status=0", transcript)
        self.assertIn("validation=failure", transcript)
        self.assertNotIn("ANDROID_SDK_INSTALL_RESULT status=success", transcript)
        self.assert_unrelated_state_preserved()

    @unittest.skipIf(os.name == "nt", "ordinary Windows users cannot reliably create symlinks")
    def test_cleanup_unlinks_requested_symlink_without_following_target(self):
        external = self.root / "external"
        external.mkdir()
        sentinel = external / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")
        candidate = self.sdk / "system-images" / "android-35" / "google_apis" / "x86_64"
        candidate.parent.mkdir(parents=True)
        candidate.symlink_to(external, target_is_directory=True)
        log = self.root / "cleanup.log"
        with log.open("w", encoding="utf-8") as stream:
            ci_android_sdk._cleanup_invalid_packages(
                self.sdk.resolve(),
                {PACKAGES[-1]: ["fixture"]},
                stream,
            )
        self.assertFalse(candidate.exists())
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")


if __name__ == "__main__":
    unittest.main()
