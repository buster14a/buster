#!/usr/bin/env python3
"""Exercise the production APK dependency graph, not an Android runtime.

CMake and Ninja are real; aapt2, jar and signing are controlled ZIP-producing
stand-ins. BUSTER_ANDROID_APK_GRAPH can select a preserved baseline graph for
before/after evidence. The normal mobile suite always uses the repository graph.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OLD_FIXTURE_TIME = 946684800  # 2000-01-01: old, but valid in a ZIP directory.
GRAPH = Path(os.environ.get("BUSTER_ANDROID_APK_GRAPH", ROOT / "cmake/AndroidApk.cmake")).resolve()

FAKE_TOOL = r'''import os
from pathlib import Path
import sys
import zipfile

mode, *args = sys.argv[1:]
if mode == "aapt2":
    assert args[0] == "link", args
    assets = Path(args[args.index("-A") + 1])
    output = Path(args[args.index("-o") + 1])
    with zipfile.ZipFile(output, "w") as archive:
        for path in sorted(assets.rglob("*")):
            if path.is_file():
                archive.write(path, "assets/" + path.relative_to(assets).as_posix())
    with open(os.environ["BUSTER_APK_TEST_LOG"], "a", encoding="utf-8") as log:
        log.write("package\n")
elif mode == "jar":
    assert len(args) == 5 and args[0] == "uf" and args[2] == "-C", args
    with zipfile.ZipFile(args[1], "a") as archive:
        archive.write(Path(args[3]) / args[4], args[4])
else:
    raise AssertionError(mode)
'''


def cmake_quote(value):
    return '"' + str(value).replace('\\', '/').replace('"', '\\"').replace(';', '\\;') + '"'


class ApkGraphCases:
    def setUp(self):
        for tool in ("cmake", "ninja", "bash"):
            self.assertIsNotNone(shutil.which(tool), f"required tool not found: {tool}")
        self.assertTrue(GRAPH.is_file(), f"APK graph not found: {GRAPH}")
        self.temporary = tempfile.TemporaryDirectory(prefix="buster apk assets ")
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / "source tree"
        self.build_dir = Path(self.temporary.name) / "build tree"
        self.source.mkdir()
        tests = self.source / "tests"
        (tests / "nested directory").mkdir(parents=True)
        (tests / "basic.c").write_text("int answer(void) { return 42; }\n", encoding="utf-8")
        (tests / "nested directory/value.h").write_text("#define VALUE 42\n", encoding="utf-8")
        (tests / "preserved.bbb").write_text("dormant corpus\n", encoding="utf-8")
        self.native = self.source / "libide.so"
        self.native.write_bytes(b"controlled native input\n")
        self.manifest = self.source / "AndroidManifest.xml"
        self.manifest.write_text("<manifest/>\n", encoding="utf-8")
        self.sign = self.source / "sign.sh"
        # The production rule's signing adapter receives five arguments.
        self.sign.write_text('set -eu\ncp "$4" "$5"\n', encoding="utf-8")
        self.log = self.source / "package.log"
        self.environment = dict(os.environ, BUSTER_APK_TEST_LOG=str(self.log))
        fake = self.source / "fake_tool.py"
        fake.write_text(FAKE_TOOL, encoding="utf-8")
        for mode in ("aapt2", "jar"):
            wrapper = self.source / mode
            wrapper.write_text(
                "#!/bin/sh\nexec " + shlex.quote(sys.executable) + " " + shlex.quote(str(fake))
                + " " + mode + ' "$@"\n', encoding="utf-8")
            wrapper.chmod(0o755)
        settings = {
            "BUSTER_APK": self.build_dir / "buster.apk",
            "BUSTER_APK_STAGE": self.build_dir / "apk",
            "BUSTER_ANDROID_MANIFEST": self.manifest,
            "BUSTER_ANDROID_SIGN_SCRIPT": self.sign,
            "BUSTER_AAPT2": self.source / "aapt2",
            "BUSTER_JAR": self.source / "jar",
            "BUSTER_ANDROID_JAR": self.source / "android.jar",
            "BUSTER_BASH_EXECUTABLE": shutil.which("bash"),
            "BUSTER_APKSIGNER": "controlled-apksigner",
            "BUSTER_ZIPALIGN": "controlled-zipalign",
            "BUSTER_KEYSTORE": self.source / "debug.keystore",
            "ANDROID_ABI": "x86_64",
        }
        project = [
            "cmake_minimum_required(VERSION 3.17)",
            "project(android_apk_assets NONE)",
            "add_library(ide SHARED IMPORTED)",
            "set_target_properties(ide PROPERTIES IMPORTED_LOCATION " + cmake_quote(self.native) + ")",
            "set(BUSTER_ANDROID_SHADER_ASSET_COMMANDS)",
            "set(BUSTER_ANDROID_SHADER_ASSET_DEPENDS)",
        ]
        project += [f"set({key} {cmake_quote(value)})" for key, value in settings.items()]
        project.append("include(" + cmake_quote(GRAPH) + ")")
        (self.source / "CMakeLists.txt").write_text("\n".join(project) + "\n", encoding="utf-8")
        self.run_command("cmake", "--warn-uninitialized", "-Werror=dev", "-S", str(self.source),
                         "-B", str(self.build_dir), "-G", "Ninja Multi-Config")
        self.build_apk()

    def run_command(self, *arguments):
        result = subprocess.run(arguments, env=self.environment, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False, timeout=60)
        self.assertEqual(result.returncode, 0, " ".join(arguments) + "\n" + result.stdout)

    def build_apk(self):
        self.run_command("cmake", "--build", str(self.build_dir), "--config", self.configuration,
                         "--target", "apk", "--parallel", "1")

    def package_count(self):
        return len(self.log.read_text(encoding="utf-8").splitlines())

    def contents(self):
        with zipfile.ZipFile(self.build_dir / "buster.apk") as archive:
            result = {name: archive.read(name) for name in archive.namelist()}
        return result

    def change(self, path, contents):
        # Avoid equal timestamps on coarse filesystems; this is not a timing benchmark.
        time.sleep(1.05)
        path.write_bytes(contents)

    def test_initial_contents_and_noop(self):
        contents = self.contents()
        self.assertEqual(contents["assets/tests/basic.c"], (self.source / "tests/basic.c").read_bytes())
        self.assertIn("assets/tests/nested directory/value.h", contents)
        self.assertEqual(contents["lib/x86_64/libide.so"], self.native.read_bytes())
        self.assertFalse(any(name.endswith(".bbb") for name in contents), contents.keys())
        before = (self.build_dir / "buster.apk").stat().st_mtime_ns
        self.build_apk()
        self.assertEqual(self.package_count(), 1, "unchanged inputs must not repackage")
        self.assertEqual((self.build_dir / "buster.apk").stat().st_mtime_ns, before)

    def test_fixture_edit(self):
        native_before = (self.native.read_bytes(), self.native.stat().st_mtime_ns)
        edited = b"int answer(void) { return 73; }\n"
        self.change(self.source / "tests/basic.c", edited)
        self.build_apk()
        self.assertEqual(self.contents()["assets/tests/basic.c"], edited, "fixture-only edit packaged stale bytes")
        self.assertEqual(self.package_count(), 2)
        self.assertEqual((self.native.read_bytes(), self.native.stat().st_mtime_ns), native_before)
        self.build_apk()
        self.assertEqual(self.package_count(), 2)

    def test_old_timestamp_addition(self):
        added = self.source / "tests/nested directory/added.h"
        added.write_bytes(b"#define ADDED 1\n")
        os.utime(added, (OLD_FIXTURE_TIME, OLD_FIXTURE_TIME))
        self.build_apk()
        self.assertEqual(self.contents().get("assets/tests/nested directory/added.h"), added.read_bytes())
        self.assertEqual(self.package_count(), 2)

    def test_removal(self):
        (self.source / "tests/nested directory/value.h").unlink()
        self.build_apk()
        self.assertNotIn("assets/tests/nested directory/value.h", self.contents(), "deleted fixture survived packaging")
        self.assertEqual(self.package_count(), 2)

    def test_rename(self):
        old = self.source / "tests/nested directory/value.h"
        renamed = old.with_name("renamed.h")
        old.rename(renamed)
        os.utime(renamed, (OLD_FIXTURE_TIME, OLD_FIXTURE_TIME))
        self.build_apk()
        contents = self.contents()
        self.assertNotIn("assets/tests/nested directory/value.h", contents)
        self.assertEqual(contents.get("assets/tests/nested directory/renamed.h"), renamed.read_bytes())
        self.assertEqual(self.package_count(), 2)

    def test_existing_package_dependencies(self):
        for path in (self.native, self.manifest, self.sign):
            with self.subTest(input=path.name):
                count = self.package_count()
                self.change(path, path.read_bytes() + b"\n")
                self.build_apk()
                self.assertEqual(self.package_count(), count + 1)
        self.assertEqual(self.contents()["lib/x86_64/libide.so"], self.native.read_bytes())

    def test_dormant_edit_is_not_a_package_input(self):
        self.change(self.source / "tests/preserved.bbb", b"still dormant\n")
        self.build_apk()
        self.assertEqual(self.package_count(), 1)
        self.assertFalse(any(name.endswith(".bbb") for name in self.contents()))

    def test_empty_active_inventory(self):
        (self.source / "tests/basic.c").unlink()
        (self.source / "tests/nested directory/value.h").unlink()
        self.build_apk()
        self.assertEqual(set(self.contents()), {"lib/x86_64/libide.so"})
        self.assertEqual(self.package_count(), 2)
        self.build_apk()
        self.assertEqual(self.package_count(), 2)


class DebugApkTests(ApkGraphCases, unittest.TestCase):
    configuration = "Debug"


class ReleaseApkTests(ApkGraphCases, unittest.TestCase):
    configuration = "Release"


if __name__ == "__main__":
    unittest.main(verbosity=2)
