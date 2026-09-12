#!/usr/bin/env python3
"""Exercise the production iOS fixture graph without requiring Apple tools."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
OLD_FIXTURE_TIME = 946684800
GRAPH = Path(os.environ.get(
    "BUSTER_IOS_ASSET_GRAPH", ROOT / "cmake/IOSBundleAssets.cmake")).resolve()


def cmake_quote(value):
    return '"' + str(value).replace('\\', '/').replace('"', '\\"').replace(';', '\\;') + '"'


class IosAssetGraphCases:
    def setUp(self):
        for tool in ("cmake", "ninja"):
            self.assertIsNotNone(shutil.which(tool), f"required tool not found: {tool}")
        self.assertTrue(GRAPH.is_file(), f"iOS asset graph not found: {GRAPH}")
        self.temporary = tempfile.TemporaryDirectory(prefix="buster ios assets ")
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / "source tree"
        self.build_dir = Path(self.temporary.name) / "build tree"
        tests = self.source / "tests"
        (tests / "nested directory").mkdir(parents=True)
        (tests / "basic.c").write_text("int answer(void) { return 42; }\n", encoding="utf-8")
        (tests / "nested directory/value.h").write_text(
            "#define VALUE 42\n", encoding="utf-8")
        (tests / "preserved.bbb").write_text("dormant corpus\n", encoding="utf-8")
        self.native_input = self.source / "native input"
        self.native_input.write_bytes(b"controlled native input\n")
        self.native = self.build_dir / "native" / "ide"
        project = [
            "cmake_minimum_required(VERSION 3.17)",
            "project(ios_bundle_assets NONE)",
            "add_custom_command(",
            "    OUTPUT " + cmake_quote(self.native),
            "    COMMAND ${CMAKE_COMMAND} -E make_directory " + cmake_quote(self.native.parent),
            "    COMMAND ${CMAKE_COMMAND} -E copy_if_different",
            "        " + cmake_quote(self.native_input) + " " + cmake_quote(self.native),
            "    DEPENDS " + cmake_quote(self.native_input),
            "    VERBATIM)",
            "add_custom_target(ide DEPENDS " + cmake_quote(self.native) + ")",
            "include(" + cmake_quote(GRAPH) + ")",
            "buster_add_ios_test_assets(ide",
            "    SOURCE_DIR " + cmake_quote(tests),
            "    BUNDLE_CONTENT_DIR",
            "        " + cmake_quote(self.build_dir / "bundles/$<CONFIG>/ide.app") + ")",
        ]
        (self.source / "CMakeLists.txt").write_text(
            "\n".join(project) + "\n", encoding="utf-8")
        self.run_command(
            "cmake", "--warn-uninitialized", "-Werror=dev", "-S", str(self.source),
            "-B", str(self.build_dir), "-G", "Ninja Multi-Config")
        self.build_assets()

    def run_command(self, *arguments, expect_success=True):
        result = subprocess.run(
            arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False, timeout=60)
        if expect_success:
            self.assertEqual(result.returncode, 0, " ".join(arguments) + "\n" + result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def build_assets(self, expect_success=True):
        return self.run_command(
            "cmake", "--build", str(self.build_dir), "--config", self.configuration,
            "--target", "ide", "--parallel", "1", expect_success=expect_success)

    def bundle_tests(self):
        return self.build_dir / "bundles" / self.configuration / "ide.app" / "tests"

    def contents(self):
        root = self.bundle_tests()
        return {
            path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*") if path.is_file()
        }

    def native_state(self):
        return self.native.read_bytes(), self.native.stat().st_mtime_ns

    def change(self, path, contents):
        time.sleep(1.05)
        path.write_bytes(contents)

    def test_initial_contents_and_noop(self):
        contents = self.contents()
        self.assertEqual(contents["basic.c"], (self.source / "tests/basic.c").read_bytes())
        self.assertIn("nested directory/value.h", contents)
        self.assertFalse(any(name.endswith(".bbb") for name in contents), contents.keys())
        native_before = self.native_state()
        fixture_before = (self.bundle_tests() / "basic.c").stat().st_mtime_ns
        result = self.build_assets()
        self.assertNotIn("Staging active test fixtures", result.stdout)
        self.assertEqual(self.native_state(), native_before)
        self.assertEqual((self.bundle_tests() / "basic.c").stat().st_mtime_ns, fixture_before)

    def test_fixture_edit_without_native_rebuild(self):
        native_before = self.native_state()
        edited = b"int answer(void) { return 73; }\n"
        self.change(self.source / "tests/basic.c", edited)
        result = self.build_assets()
        self.assertIn("Staging active test fixtures", result.stdout)
        self.assertEqual(self.contents()["basic.c"], edited)
        self.assertEqual(self.native_state(), native_before)
        result = self.build_assets()
        self.assertNotIn("Staging active test fixtures", result.stdout)

    def test_old_timestamp_addition(self):
        added = self.source / "tests/nested directory/added fixture.h"
        added.write_bytes(b"#define ADDED 1\n")
        os.utime(added, (OLD_FIXTURE_TIME, OLD_FIXTURE_TIME))
        self.build_assets()
        self.assertEqual(self.contents().get("nested directory/added fixture.h"), added.read_bytes())

    def test_removal(self):
        (self.source / "tests/nested directory/value.h").unlink()
        self.build_assets()
        self.assertNotIn("nested directory/value.h", self.contents())

    def test_rename(self):
        old = self.source / "tests/nested directory/value.h"
        renamed = old.with_name("renamed fixture.h")
        old.rename(renamed)
        os.utime(renamed, (OLD_FIXTURE_TIME, OLD_FIXTURE_TIME))
        self.build_assets()
        contents = self.contents()
        self.assertNotIn("nested directory/value.h", contents)
        self.assertEqual(contents.get("nested directory/renamed fixture.h"), renamed.read_bytes())

    def test_dormant_edit_is_not_a_bundle_input(self):
        native_before = self.native_state()
        fixture_before = (self.bundle_tests() / "basic.c").stat().st_mtime_ns
        self.change(self.source / "tests/preserved.bbb", b"still dormant\n")
        result = self.build_assets()
        self.assertNotIn("Staging active test fixtures", result.stdout)
        self.assertEqual(self.native_state(), native_before)
        self.assertEqual((self.bundle_tests() / "basic.c").stat().st_mtime_ns, fixture_before)
        self.assertFalse(any(name.endswith(".bbb") for name in self.contents()))

    def test_empty_active_inventory(self):
        (self.source / "tests/basic.c").unlink()
        (self.source / "tests/nested directory/value.h").unlink()
        self.build_assets()
        self.assertEqual(self.contents(), {})
        result = self.build_assets()
        self.assertNotIn("Staging active test fixtures", result.stdout)

    def test_copy_failure_propagates(self):
        self.change(self.source / "tests/basic.c", b"force restage\n")
        bundle_config = self.build_dir / "bundles" / self.configuration
        shutil.rmtree(bundle_config)
        bundle_config.write_bytes(b"blocks the app directory\n")
        self.build_assets(expect_success=False)


class DebugIosAssetTests(IosAssetGraphCases, unittest.TestCase):
    configuration = "Debug"


class ReleaseIosAssetTests(IosAssetGraphCases, unittest.TestCase):
    configuration = "Release"


if __name__ == "__main__":
    unittest.main(verbosity=2)
