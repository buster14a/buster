#!/usr/bin/env python3
"""Exercise mobile packaging of source-tree compiler driver fixtures."""

import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER_FIXTURE_RELATIVE = Path(
    "src/buster/tests/compiler/driver/fixtures/aarch64_platform_large_va_arg.c")
DRIVER_FIXTURE_BYTES = (ROOT / DRIVER_FIXTURE_RELATIVE).read_bytes()


def load_harness(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load test harness: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


APK = load_harness("android_apk_assets_harness", ROOT / "tests/android_apk_assets_test.py")
IOS = load_harness("ios_bundle_assets_harness", ROOT / "tests/ios_bundle_assets_test.py")
ANDROID_ASSET_PATH = "assets/" + DRIVER_FIXTURE_RELATIVE.as_posix()


class AndroidDriverFixtureCases:
    def setUp(self):
        super().setUp()
        if self._testMethodName.startswith("test_driver_fixture_"):
            self.driver_fixture_source = self.source / DRIVER_FIXTURE_RELATIVE
            self.driver_fixture_source.parent.mkdir(parents=True, exist_ok=True)
            self.driver_fixture_source.write_bytes(DRIVER_FIXTURE_BYTES)
            fixture_time = getattr(APK, "OLD_FIXTURE_TIME", 946684800)
            os.utime(self.driver_fixture_source, (fixture_time, fixture_time))
            self.build_apk()

    def test_driver_fixture_present(self):
        self.assertEqual(self.contents().get(ANDROID_ASSET_PATH), DRIVER_FIXTURE_BYTES)

    def test_driver_fixture_edit_is_repackaged(self):
        edited = DRIVER_FIXTURE_BYTES + b"\n/* asset invalidation */\n"
        package_count = self.package_count()
        self.change(self.driver_fixture_source, edited)
        self.build_apk()
        self.assertEqual(self.contents().get(ANDROID_ASSET_PATH), edited)
        self.assertEqual(self.package_count(), package_count + 1)
        self.build_apk()
        self.assertEqual(self.package_count(), package_count + 1)

    def test_driver_fixture_removal_is_repackaged(self):
        package_count = self.package_count()
        self.driver_fixture_source.unlink()
        self.build_apk()
        self.assertNotIn(ANDROID_ASSET_PATH, self.contents())
        self.assertEqual(self.package_count(), package_count + 1)


class IosDriverFixtureCases:
    def setUp(self):
        super().setUp()
        if self._testMethodName.startswith("test_driver_fixture_"):
            self.driver_fixture_source = self.source / DRIVER_FIXTURE_RELATIVE
            self.driver_fixture_source.parent.mkdir(parents=True, exist_ok=True)
            self.driver_fixture_source.write_bytes(DRIVER_FIXTURE_BYTES)
            fixture_time = getattr(IOS, "OLD_FIXTURE_TIME", 946684800)
            os.utime(self.driver_fixture_source, (fixture_time, fixture_time))
            self.build_assets()

    def driver_fixture_bundle_path(self):
        return (self.build_dir / "bundles" / self.configuration / "ide.app"
                / DRIVER_FIXTURE_RELATIVE)

    def test_driver_fixture_present(self):
        self.assertEqual(self.driver_fixture_bundle_path().read_bytes(), DRIVER_FIXTURE_BYTES)

    def test_driver_fixture_edit_is_repackaged(self):
        native_before = self.native_state()
        edited = DRIVER_FIXTURE_BYTES + b"\n/* asset invalidation */\n"
        self.change(self.driver_fixture_source, edited)
        result = self.build_assets()
        self.assertIn("Staging active test fixtures", result.stdout)
        self.assertEqual(self.driver_fixture_bundle_path().read_bytes(), edited)
        self.assertEqual(self.native_state(), native_before)
        result = self.build_assets()
        self.assertNotIn("Staging active test fixtures", result.stdout)

    def test_driver_fixture_removal_is_repackaged(self):
        self.driver_fixture_source.unlink()
        self.build_assets()
        self.assertFalse(self.driver_fixture_bundle_path().exists())


class DebugAndroidDriverFixtureTests(AndroidDriverFixtureCases, APK.ApkGraphCases, unittest.TestCase):
    configuration = "Debug"


class ReleaseAndroidDriverFixtureTests(AndroidDriverFixtureCases, APK.ApkGraphCases, unittest.TestCase):
    configuration = "Release"


class DebugIosDriverFixtureTests(IosDriverFixtureCases, IOS.IosAssetGraphCases, unittest.TestCase):
    configuration = "Debug"


class ReleaseIosDriverFixtureTests(IosDriverFixtureCases, IOS.IosAssetGraphCases, unittest.TestCase):
    configuration = "Release"


if __name__ == "__main__":
    unittest.main(verbosity=2)
