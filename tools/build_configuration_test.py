#!/usr/bin/env python3
"""Focused build cwd, Android SDK and Apple target-architecture regressions."""

import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CMAKE = shutil.which("cmake")
CLANG = shutil.which("clang")


def cmake_script(directory, module, body):
    script = directory / "selection.cmake"
    script.write_text(f'cmake_minimum_required(VERSION 3.17)\ninclude("{ROOT.as_posix()}/cmake/{module}")\n' + body)
    return subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, timeout=30)


@unittest.skipUnless(CMAKE, "cmake is required")
class AndroidSdkSelectionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="buster sdk selection ")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.sdk = self.work / "sdk"

    def build_tools(self, versions, override=""):
        for version in versions:
            (self.sdk / "build-tools" / version).mkdir(parents=True)
        return cmake_script(self.work, "AndroidSdkSelection.cmake", f'''
buster_android_build_tools(selected "{self.sdk.as_posix()}" "{override}")
message("selected=${{selected}}")
''')

    def jars(self, platforms, requested="android-31", override=""):
        for name in platforms:
            directory = self.sdk / "platforms" / name
            directory.mkdir(parents=True)
            (directory / "android.jar").touch()
        return cmake_script(self.work, "AndroidSdkSelection.cmake", f'''
buster_android_platform_jar(selected "{self.sdk.as_posix()}" "{requested}" "{override}")
message("selected=${{selected}}")
''')

    def test_numeric_api_order_and_requested_platform(self):
        result = self.jars(["android-9", "android-34", "android-35", "android-bad"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("platforms/android-35/android.jar", result.stderr)
        result = self.jars([], requested="android-9")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("platforms/android-9/android.jar", result.stderr)
        explicit = self.work / "explicit.jar"
        explicit.touch()
        result = self.jars([], override=explicit.as_posix())
        self.assertIn(f"selected={explicit.as_posix()}", result.stderr)

    def test_numeric_build_tools_and_stable_preference(self):
        result = self.build_tools(["9.0.0", "35.0.9", "35.0.10", "99.0.0-rc1", "bad"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("build-tools/35.0.10", result.stderr)
        explicit = self.work / "explicit tools"
        explicit.mkdir()
        result = self.build_tools([], override=explicit.as_posix())
        self.assertIn(f"selected={explicit.as_posix()}", result.stderr)

    def test_preview_version_stage_and_number(self):
        result = self.build_tools(["9.0.0-rc99", "35.0.0-alpha99", "35.0.0-beta99", "35.0.0-rc9", "35.0.0-rc10"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("build-tools/35.0.0-rc10", result.stderr)

    def test_missing_malformed_and_invalid_overrides(self):
        result = self.build_tools(["bad", "35", "35.0.0-dev1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No valid Android build-tools version directory", result.stderr)
        result = self.jars(["android-bad", "android-35-preview"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No valid android.jar", result.stderr)
        result = self.build_tools([], override=(self.work / "missing").as_posix())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("BUSTER_ANDROID_BUILD_TOOLS_DIR", result.stderr)
        result = self.jars([], override=(self.work / "missing.jar").as_posix())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("BUSTER_ANDROID_JAR", result.stderr)


@unittest.skipUnless(CMAKE, "cmake is required")
class AppleArchitectureTests(unittest.TestCase):
    def test_target_and_host_policy(self):
        cases = [
            ("Darwin", "arm64", "arm64", "x86_64", "OFF", "x86_64", "ON"),
            ("Darwin", "x86_64", "x86_64", "arm64", "OFF", "arm64", "ON"),
            ("Darwin", "arm64", "aarch64", "arm64", "OFF", "arm64", "OFF"),
            ("Darwin", "AMD64", "x86_64", "x86_64", "OFF", "x86_64", "OFF"),
            ("Darwin", "arm64", "arm64", "", "OFF", "arm64", "OFF"),
            ("iOS", "", "arm64", "arm64", "ON", "arm64", "ON"),
            ("Linux", "x86_64", "x86_64", "arm64", "OFF", "x86_64", "OFF"),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            for system, processor, host, requested, cross, expected, expected_cross in cases:
                with self.subTest(system=system, host=host, requested=requested):
                    result = cmake_script(work, "TargetArchitecture.cmake", f'''
buster_target_architecture(target cross "{system}" "{processor}" "{host}" "{requested}" "{cross}")
if (NOT target STREQUAL "{expected}" OR NOT cross STREQUAL "{expected_cross}")
    message(FATAL_ERROR "unexpected target=${{target}} cross=${{cross}}")
endif()
''')
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_universal_and_unsupported_architecture_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            for requested, diagnostic in [("arm64;x86_64", "universal Apple builds are unsupported"), ("armv7", "Unsupported Buster Apple target architecture")]:
                result = cmake_script(work, "TargetArchitecture.cmake", f'buster_target_architecture(target cross Darwin arm64 arm64 "{requested}" OFF)\n')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, " ".join(result.stderr.split()))

    @unittest.skipUnless(sys.platform == "darwin" and CLANG, "native Apple Clang/SDK required")
    def test_hosted_macos_cross_architecture_object(self):
        target = "x86_64" if platform.machine() in ("arm64", "aarch64") else "arm64"
        module = "x86_64.c" if target == "x86_64" else "aarch64.c"
        other_module = "aarch64.c" if target == "x86_64" else "x86_64.c"
        with tempfile.TemporaryDirectory(prefix="buster apple arch ") as temporary:
            build = Path(temporary) / "build"
            command = [CMAKE, "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                       f"-DCMAKE_C_COMPILER={CLANG}", "-DCMAKE_BUILD_TYPE=Debug",
                       f"-DCMAKE_OSX_ARCHITECTURES={target}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                       "-DBUSTER_UNITY_BUILD=OFF", "-DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF",
                       "-DBUSTER_DEVELOPER_TARGETS=OFF", "-DBUSTER_REQUIRE_VULKAN_SDK=OFF",
                       "-DBUSTER_COMPILE_SHADERS=OFF"]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            rows = json.loads((build / "compile_commands.json").read_text())
            ide_rows = [row for row in rows if "ide.dir" in row["command"]]
            modules = [row for row in ide_rows if Path(row["file"]).name == module]
            self.assertTrue(modules, f"missing target module {module}")
            self.assertFalse([row for row in ide_rows if Path(row["file"]).name == other_module])
            self.assertTrue(all("-march=native" not in row["command"] for row in ide_rows))
            self.assertEqual(bool([row for row in ide_rows if Path(row["file"]).name == "x86_64_test.c"]), target == "x86_64")
            row = modules[0]
            compile_command = shlex.split(row["command"])
            object_file = Path(row["directory"]) / compile_command[compile_command.index("-o") + 1]
            object_file.parent.mkdir(parents=True, exist_ok=True)
            result = subprocess.run(compile_command, cwd=row["directory"], capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            magic, cpu = struct.unpack("<II", object_file.read_bytes()[:8])
            self.assertEqual(magic, 0xFEEDFACF)
            self.assertEqual(cpu, 0x01000007 if target == "x86_64" else 0x0100000C)


@unittest.skipUnless(CLANG, "clang is required")
class BuildProcessDirectoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="buster cwd regression ")
        cls.work = Path(cls.temporary.name)
        cls.executable = cls.work / ("fixture.exe" if os.name == "nt" else "fixture")
        command = [CLANG, "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
                   "-fwrapv", "-fno-strict-aliasing", "-funsigned-char",
                   "tools/build_process_directory_fixture.c", "-o", str(cls.executable)]
        if os.name == "nt":
            command += ["-Wno-microsoft-enum-forward-reference", "-lws2_32"]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
        if result.returncode:
            cls.temporary.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_fixture(self, mode, target):
        caller = self.work / self._testMethodName
        caller.mkdir()
        result = subprocess.run([str(self.executable), mode, str(target)], cwd=caller,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return caller, result.stdout + result.stderr

    def test_missing_directory_never_launches_child(self):
        caller, output = self.run_fixture("missing", self.work / "nonexistent")
        self.assertFalse((caller / "child-marker").exists())
        self.assertIn("enter the requested working directory", output)

    def test_non_directory_never_launches_child(self):
        target = self.work / "plain-file"
        target.touch()
        caller, output = self.run_fixture("missing", target)
        self.assertFalse((caller / "child-marker").exists())
        self.assertIn("enter the requested working directory", output)

    def test_inaccessible_directory_never_launches_child(self):
        target = self.work / "inaccessible-target"
        target.mkdir()
        caller, output = self.run_fixture("entry-failure", target)
        self.assertFalse((caller / "child-marker").exists())
        self.assertFalse((target / "child-marker").exists())
        self.assertIn("enter the requested working directory", output)

    def test_failed_original_capture_never_launches_child(self):
        target = self.work / "capture-target"
        target.mkdir()
        caller, output = self.run_fixture("capture-failure", target)
        self.assertFalse((caller / "child-marker").exists())
        self.assertFalse((target / "child-marker").exists())
        self.assertIn("capture the build driver's working directory", output)

    def test_valid_directory_and_caller_restoration(self):
        target = self.work / "valid-target"
        target.mkdir()
        caller, output = self.run_fixture("valid", target)
        self.assertTrue((target / "child-marker").exists())
        self.assertTrue((caller / "child-marker").exists())
        self.assertNotIn("error:", output)


if __name__ == "__main__":
    unittest.main()
