#!/usr/bin/env python3
"""Hermetic regression tests for the bounded Android SDK installer."""
from __future__ import annotations

import os
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

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
if mode == "unavailable-emulator-update":
    if "emulator" in packages:
        print("Warning: Android Emulator archive is unavailable.")
        raise SystemExit(1)
    for package in packages:
        valid(package)
    raise SystemExit(0)
if mode == "partial-progress":
    if count == 1:
        valid("emulator")
        valid("platforms;android-35")
        partial_image()
        raise SystemExit(1)
    if "emulator" in packages or "platforms;android-35" in packages:
        print("valid packages were unnecessarily requested again")
        raise SystemExit(9)
    for package in packages:
        valid(package)
    raise SystemExit(0)
if mode == "complete-but-failed":
    for package in packages:
        valid(package)
    raise SystemExit(1)
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
            {("android", "x86_64"), ("ios", "aarch64")},
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

    def preinstall(self, package):
        names = {
            "emulator": ("emulator", "emulator.exe"),
            "platforms;android-35": ("android.jar", "source.properties"),
            "system-images;android-35;google_apis;x86_64": ("source.properties", "system.img", "ramdisk.img", "kernel-ranchu"),
        }
        directory = self.sdk.joinpath(*package.split(";"))
        directory.mkdir(parents=True)
        for name in names[package]:
            path = directory / name
            path.write_text("preinstalled", encoding="utf-8")
            if package == "emulator":
                path.chmod(0o755)

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

    def test_valid_emulator_is_preserved_when_only_image_is_missing(self):
        self.preinstall("emulator")
        self.preinstall("platforms;android-35")
        self.assertEqual(self.run_installer("unavailable-emulator-update"), 0)
        self.assertEqual(self.attempts(), 1)
        self.assertEqual((self.sdk / "emulator" / "emulator").read_text(encoding="utf-8"), "preinstalled")
        self.assertEqual((self.sdk / "platforms" / "android-35" / "android.jar").read_text(encoding="utf-8"), "preinstalled")
        transcript = (self.logs / "android-sdk-install.attempt-1.log").read_text(encoding="utf-8")
        command = next(line for line in transcript.splitlines() if line.startswith("ANDROID_SDK_COMMAND "))
        self.assertIn("--verbose", command)
        self.assertNotIn("--install emulator", command)
        self.assertNotIn("platforms;android-35", command)
        self.assertIn(PACKAGES[-1], command)
        self.assertFalse(ci_android_sdk.validate_packages(self.sdk.resolve(), PACKAGES))
        self.assert_unrelated_state_preserved()

    def test_retry_keeps_packages_completed_by_failed_attempt(self):
        self.assertEqual(self.run_installer("partial-progress"), 0)
        self.assertEqual(self.attempts(), 2)
        transcript = (self.logs / "android-sdk-install.attempt-2.log").read_text(encoding="utf-8")
        command = next(line for line in transcript.splitlines() if line.startswith("ANDROID_SDK_COMMAND "))
        self.assertNotIn("--install emulator", command)
        self.assertNotIn("platforms;android-35", command)
        self.assertIn(PACKAGES[-1], command)
        self.assertFalse(ci_android_sdk.validate_packages(self.sdk.resolve(), PACKAGES))
        self.assert_unrelated_state_preserved()

    def test_complete_preinstalled_sdk_never_invokes_sdkmanager(self):
        for package in PACKAGES:
            self.preinstall(package)
        self.assertEqual(self.run_installer("permanent"), 0)
        self.assertFalse(self.state.exists())
        transcript = (self.logs / "android-sdk-install.log").read_text(encoding="utf-8")
        self.assertIn("status=success attempts=0 source=preinstalled", transcript)
        self.assert_unrelated_state_preserved()

    def test_nonzero_installer_status_cannot_accept_valid_packages(self):
        self.assertEqual(self.run_installer("complete-but-failed"), 1)
        self.assertEqual(self.attempts(), 1)
        transcript = (self.logs / "android-sdk-install.log").read_text(encoding="utf-8")
        self.assertIn("status=1 timed_out=no validation=success", transcript)
        self.assertIn("ANDROID_SDK_INSTALL_RESULT status=failure attempts=1", transcript)
        self.assertNotIn("ANDROID_SDK_RETRY", transcript)
        self.assertNotIn("ANDROID_SDK_INSTALL_RESULT status=success", transcript)
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


@unittest.skipUnless(os.name == "posix" and hasattr(os, "O_NOFOLLOW"), "selected Android image observation runs on Ubuntu")
class AndroidSystemImageRevisionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sdk = self.root / "sdk"
        self.sdk.mkdir()
        self.logs = self.root / "logs"
        self.image = PACKAGES[-1]
        self.properties = self.sdk.joinpath(*self.image.split(";")) / "source.properties"
        self.environment = {"BUSTER_CI_CONDITIONS_EVIDENCE": "1", "BUSTER_ANDROID_SYSTEM_IMAGE": self.image,
                            "GITHUB_REPOSITORY": "buster14a/buster", "GITHUB_SHA": "a" * 40,
                            "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "1", "GITHUB_JOB": "mobile"}

    def populate(self, package, revision="9"):
        directory = self.sdk.joinpath(*package.split(";"))
        directory.mkdir(parents=True, exist_ok=True)
        if package == "emulator":
            path = directory / "emulator"
            path.write_bytes(b"synthetic executable")
            path.chmod(0o755)
        else:
            for name in ("system.img", "ramdisk.img", "kernel-ranchu"):
                (directory / name).write_bytes(b"synthetic image")
            (directory / "source.properties").write_text("Pkg.Desc=not a revision\nPkg.Revision=" + revision + "\n")

    def observe(self, environment=None, packages=None):
        combined = io.StringIO()
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            ci_android_sdk._retain_system_image_revision(self.sdk, (self.image,) if packages is None else packages,
                                                        self.environment if environment is None else environment,
                                                        "preinstalled", combined)
        prefix = "ANDROID_SDK_SYSTEM_IMAGE_REVISION "
        self.assertEqual(combined.getvalue(), stdout.getvalue())
        self.assertTrue(combined.getvalue().startswith(prefix))
        return json.loads(combined.getvalue()[len(prefix):])

    def install(self, packages=None, environment=None):
        with contextlib.redirect_stdout(io.StringIO()):
            return ci_android_sdk.install(["mock-sdkmanager"], self.sdk,
                                          (self.image,) if packages is None else packages, self.logs,
                                          attempts=1, timeout_seconds=5, backoff_seconds=0,
                                          environment=self.environment if environment is None else environment)

    def retained(self):
        prefix = "ANDROID_SDK_SYSTEM_IMAGE_REVISION "
        rows = [line[len(prefix):] for line in (self.logs / "android-sdk-install.log").read_text().splitlines()
                if line.startswith(prefix)]
        self.assertEqual(len(rows), 1)
        return json.loads(rows[0])

    def test_disabled_explicit_environment_overrides_ambient_optin_and_has_no_extra_read(self):
        self.populate(self.image)
        for flag in (None, "", "0", "true", "01"):
            environment = {} if flag is None else {"BUSTER_CI_CONDITIONS_EVIDENCE": flag}
            with self.subTest(flag=flag), mock.patch.dict(os.environ, self.environment), \
                 mock.patch.object(ci_android_sdk, "_system_image_revision", side_effect=AssertionError("extra read")), \
                 mock.patch.object(ci_android_sdk, "_run_attempt", side_effect=AssertionError("installer")):
                self.assertEqual(self.install(environment=environment), 0)
                self.assertNotIn("ANDROID_SDK_SYSTEM_IMAGE_REVISION", (self.logs / "android-sdk-install.log").read_text())

    def test_preinstalled_receipt_binds_actual_metadata_revision_and_does_not_probe_sdkmanager(self):
        self.populate(self.image)
        data = self.properties.read_bytes()
        with mock.patch.object(ci_android_sdk, "_run_attempt", side_effect=AssertionError("reinstalled")):
            self.assertEqual(self.install(), 0)
        receipt = self.retained()
        self.assertEqual(receipt["status"], "observed")
        self.assertEqual(receipt["revision"], "9")
        self.assertEqual(receipt["source"], "preinstalled")
        self.assertEqual(receipt["package"], self.image)
        self.assertEqual(receipt["source_properties"], {"path": str(self.properties.resolve()), "bytes": len(data),
                                                      "sha256": hashlib.sha256(data).hexdigest()})
        for key, env in (("repository", "GITHUB_REPOSITORY"), ("source_revision", "GITHUB_SHA"),
                         ("run_id", "GITHUB_RUN_ID"), ("run_attempt", "GITHUB_RUN_ATTEMPT"), ("job", "GITHUB_JOB")):
            self.assertEqual(receipt[key], self.environment[env])
        self.assertNotIn("Pkg.Desc", json.dumps(receipt))

    def test_successful_mock_install_observes_actual_revision_before_return(self):
        def attempt(command, packages, sdk_root, attempt_directory, log_path, timeout_seconds, environment):
            self.assertEqual(packages, (self.image,))
            self.populate(self.image, "17")
            log_path.write_text("mock sdkmanager zero exit\n")
            return 0, False
        with mock.patch.object(ci_android_sdk, "_run_attempt", side_effect=attempt):
            self.assertEqual(self.install(), 0)
        receipt = self.retained()
        self.assertEqual((receipt["status"], receipt["revision"], receipt["source"]), ("observed", "17", "sdkmanager"))

    def test_valid_image_is_not_reinstalled_when_another_requested_package_needs_repair(self):
        self.populate(self.image, "23")
        def attempt(command, packages, sdk_root, attempt_directory, log_path, timeout_seconds, environment):
            self.assertEqual(packages, ("emulator",))
            self.populate("emulator")
            log_path.write_text("mock emulator repair\n")
            return 0, False
        with mock.patch.object(ci_android_sdk, "_run_attempt", side_effect=attempt):
            self.assertEqual(self.install(packages=("emulator", self.image)), 0)
        self.assertEqual(self.retained()["revision"], "23")
        self.assertIn("ANDROID_SDK_PREINSTALL package=" + self.image + " status=success",
                      (self.logs / "android-sdk-install.log").read_text())

    def test_nonzero_installer_status_never_publishes_observed_success_revision(self):
        def attempt(command, packages, sdk_root, attempt_directory, log_path, timeout_seconds, environment):
            self.populate(self.image)
            log_path.write_text("mock nonzero sdkmanager\n")
            return 1, False
        with mock.patch.object(ci_android_sdk, "_run_attempt", side_effect=attempt), \
             mock.patch.object(ci_android_sdk, "_retain_system_image_revision", side_effect=AssertionError("observed failure")), \
             contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self.install(), 1)
        self.assertNotIn("ANDROID_SDK_SYSTEM_IMAGE_REVISION", (self.logs / "android-sdk-install.log").read_text())

    def test_missing_duplicate_malformed_utf8_and_oversized_metadata_are_unknown(self):
        self.populate(self.image)
        for data in (b"Pkg.Desc=secret-property-value\n", b"Pkg.Revision=0\n", b"Pkg.Revision=-1\n",
                     b"Pkg.Revision=9.0\n", b"Pkg.Revision=9\nPkg.Revision=10\n", b"Pkg.Revision\n",
                     b"Pkg.Revision=9\n\xff", b"x" * (ci_android_sdk._SYSTEM_IMAGE_METADATA_BYTES + 1)):
            with self.subTest(data=data[:50]):
                self.properties.write_bytes(data)
                receipt = self.observe()
                self.assertEqual(receipt["status"], "unknown")
                self.assertNotIn("revision", receipt)
                self.assertNotIn("secret-property-value", json.dumps(receipt))
        self.properties.unlink()
        self.assertEqual(self.observe()["status"], "unknown")

    def test_symlink_nonregular_and_parent_symlink_metadata_are_unknown_without_following(self):
        self.populate(self.image)
        outside = self.root / "outside.properties"
        outside.write_text("Pkg.Revision=999\n")
        self.properties.unlink()
        self.properties.symlink_to(outside)
        self.assertEqual(self.observe()["status"], "unknown")
        self.properties.unlink()
        os.mkfifo(self.properties)
        self.assertEqual(self.observe()["status"], "unknown")
        self.properties.unlink()
        directory = self.properties.parent
        directory.rename(self.root / "outside-image")
        directory.symlink_to(self.root / "outside-image", target_is_directory=True)
        self.assertEqual(self.observe()["status"], "unknown")
        self.assertEqual(outside.read_text(), "Pkg.Revision=999\n")

    def test_actual_metadata_replacement_during_read_stays_unknown(self):
        self.populate(self.image)
        original = ci_android_sdk.os.fstat
        calls = 0
        def changed(descriptor):
            nonlocal calls
            calls += 1
            if calls == 2:
                replacement = self.properties.with_name("replacement")
                replacement.write_text("Pkg.Revision=10\n")
                replacement.replace(self.properties)
            return original(descriptor)
        with mock.patch.object(ci_android_sdk.os, "fstat", side_effect=changed):
            receipt = self.observe()
        self.assertEqual(receipt["status"], "unknown")
        self.assertIn("changed", receipt["reason"])

    def test_invalid_binding_wrong_request_and_multiple_images_do_not_read_metadata(self):
        self.populate(self.image)
        invalid = ({**self.environment, "GITHUB_RUN_ID": "0"},
                   {**self.environment, "GITHUB_SHA": ""},
                   {**self.environment, "BUSTER_ANDROID_SYSTEM_IMAGE": "system-images;android-36;google_apis;x86_64"})
        with mock.patch.object(ci_android_sdk, "_system_image_revision", side_effect=AssertionError("read")):
            for environment in invalid:
                with self.subTest(environment=environment):
                    self.assertEqual(self.observe(environment)["status"], "unknown")
            self.assertEqual(self.observe(packages=(self.image, "system-images;android-36;google_apis;x86_64"))["status"], "unknown")

    def test_unknown_revision_does_not_change_structural_validation_or_install_success(self):
        self.populate(self.image)
        self.properties.write_text("Pkg.Desc=present but no revision\n")
        self.assertEqual(ci_android_sdk.validate_package(self.sdk, self.image), [])
        with mock.patch.object(ci_android_sdk, "_run_attempt", side_effect=AssertionError("reinstalled")):
            self.assertEqual(self.install(), 0)
        self.assertEqual(self.retained()["status"], "unknown")

    def test_revision_receipt_is_bounded_and_does_not_capture_unrelated_environment(self):
        self.populate(self.image)
        environment = {**self.environment, "GITHUB_TOKEN": "secret-token", "UNRELATED": "secret-property"}
        receipt = self.observe(environment)
        self.assertNotIn("secret-token", json.dumps(receipt))
        self.assertNotIn("secret-property", json.dumps(receipt))
        with mock.patch.object(ci_android_sdk, "_SYSTEM_IMAGE_RECEIPT_BYTES", 256):
            receipt = self.observe()
        self.assertEqual(receipt["status"], "unknown")
        self.assertLessEqual(len(json.dumps(receipt).encode()), 256)


if __name__ == "__main__":
    unittest.main()
