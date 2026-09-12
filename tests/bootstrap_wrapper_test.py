#!/usr/bin/env python3
"""Controlled cross-platform tests for the immutable TCC bootstrap cache."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


FAKE_TCC = r'''#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import sys
import time

arguments = sys.argv[1:]
log = Path(os.environ["BUSTER_FAKE_TCC_LOG"])
with log.open("ab", buffering=0) as output:
    output.write((" ".join(arguments) + "\n").encode())
if os.environ.get("BUSTER_FAKE_TCC_DELAY"):
    time.sleep(float(os.environ["BUSTER_FAKE_TCC_DELAY"]))
if "-MF" in arguments:
    dependency = Path(arguments[arguments.index("-MF") + 1])
    dependency.write_text("bootstrap: build.c src/buster/lib/base.h src/buster/lib/header\\ with\\ spaces.h\n")
if "-o" in arguments:
    destination = Path(arguments[arguments.index("-o") + 1])
    if os.environ.get("BUSTER_FAKE_TCC_FAIL") and ".tmp" in destination.name:
        sys.exit(int(os.environ["BUSTER_FAKE_TCC_FAIL"]))
    if os.environ.get("BUSTER_FAKE_TCC_MUTATE") and ".tmp" in destination.name:
        with Path("src/buster/lib/base.h").open("a") as source:
            source.write("#define BUSTER_CHANGED_DURING_COMPILE 1\n")
    shutil.copyfile(os.environ["BUSTER_FAKE_DRIVER"], destination)
    destination.chmod(0o755)
'''


class BootstrapWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="buster bootstrap wrapper ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "repository with spaces"
        (self.root / "tools").mkdir(parents=True)
        (self.root / "src/buster/lib").mkdir(parents=True)
        (self.root / "bin").mkdir()
        for name in ("build.sh", "build.ps1"):
            shutil.copy2(ROOT / name, self.root / name)
        for name in ("bootstrap_driver.sh", "bootstrap_driver.ps1"):
            shutil.copy2(ROOT / "tools" / name, self.root / "tools" / name)
        (self.root / "build.c").write_text("#include <buster/lib/base.h>\nint main(void) { return 0; }\n")
        self.dependency = self.root / "src/buster/lib/base.h"
        self.dependency.write_text("#define BUSTER_FAKE 1\n")
        (self.root / "src/buster/lib/header with spaces.h").write_text("#define BUSTER_SPACES 1\n")
        self.fake_tcc = self.root / "fake_tcc.py"
        self.fake_tcc.write_text(FAKE_TCC)
        self.log = self.root / "tcc.log"
        self.log.write_text("")
        self.environment = os.environ.copy()
        self.environment.update({
            "BUSTER_FAKE_TCC_LOG": str(self.log),
            "PATH": str(self.root / "bin") + os.pathsep + self.environment.get("PATH", ""),
        })
        if os.name == "nt":
            self.shell = shutil.which("powershell.exe") or shutil.which("pwsh.exe")
            self.assertIsNotNone(self.shell)
            driver = Path(shutil.which("where.exe"))
            python = Path(sys.executable)
            (self.root / "bin/tcc.cmd").write_text(
                '@echo off\r\n"%s" "%s" %%*\r\nexit /b %%ERRORLEVEL%%\r\n' % (python, self.fake_tcc)
            )
            self.environment["BUSTER_FAKE_DRIVER"] = str(driver)
        else:
            self.shell = None
            driver = self.root / "fake-driver"
            driver.write_text("#!/usr/bin/env bash\nprintf 'driver:%s\\n' \"$*\"\nexit \"${BUSTER_FAKE_DRIVER_STATUS:-0}\"\n")
            driver.chmod(0o755)
            tcc = self.root / "bin/tcc"
            tcc.write_text('#!/usr/bin/env bash\nexec "%s" "%s" "$@"\n' % (sys.executable, self.fake_tcc))
            tcc.chmod(0o755)
            self.environment["BUSTER_FAKE_DRIVER"] = str(driver)

    def run_wrapper(self, *arguments, environment=None):
        active_environment = self.environment.copy()
        if environment:
            active_environment.update(environment)
        if os.name == "nt":
            command = [self.shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(self.root / "build.ps1"), *arguments]
        else:
            command = ["bash", str(self.root / "build.sh"), *arguments]
        return subprocess.run(command, cwd=self.root.parent, env=active_environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def launch_count(self):
        return len(self.log.read_text().splitlines())

    def success_arguments(self, marker):
        if os.name == "nt":
            return ("cmd.exe",)
        return (marker, "two words")

    def assert_driver_ran(self, result, marker):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cmd.exe" if os.name == "nt" else marker, result.stdout)

    def cache_artifacts(self):
        platform = "windows" if os.name == "nt" else "posix"
        cache = self.root / ".cache/bootstrap-driver" / platform
        return [path for path in cache.rglob("build-*") if not path.name.endswith(".complete")]

    def test_cold_then_warm_reuse_and_argument_forwarding(self):
        arguments = self.success_arguments("forwarded-marker")
        self.assert_driver_ran(self.run_wrapper(*arguments), "forwarded-marker")
        self.assertEqual(self.launch_count(), 2)
        self.assert_driver_ran(self.run_wrapper(*arguments), "forwarded-marker")
        self.assertEqual(self.launch_count(), 2)

    def test_dependency_and_compiler_identity_invalidate_reuse(self):
        arguments = self.success_arguments("identity-marker")
        self.assert_driver_ran(self.run_wrapper(*arguments), "identity-marker")
        self.dependency.write_text("#define BUSTER_FAKE 2\n")
        self.assert_driver_ran(self.run_wrapper(*arguments), "identity-marker")
        self.assertEqual(self.launch_count(), 4)
        tcc = self.root / "bin" / ("tcc.cmd" if os.name == "nt" else "tcc")
        with tcc.open("a") as output:
            output.write("\n")
        self.assert_driver_ran(self.run_wrapper(*arguments), "identity-marker")
        self.assertEqual(self.launch_count(), 6)

    def test_effective_flag_change_invalidates_reuse(self):
        arguments = self.success_arguments("flags-marker")
        self.assert_driver_ran(self.run_wrapper(*arguments), "flags-marker")
        helper = self.root / "tools" / ("bootstrap_driver.ps1" if os.name == "nt" else "bootstrap_driver.sh")
        text = helper.read_text()
        if os.name == "nt":
            text = text.replace('"-g", "-lws2_32", "-MD"', '"-g", "-Wno-unused-function", "-lws2_32", "-MD"')
        else:
            text = text.replace("-Wno-unused-function -g -MD", "-Wno-unused-function -g -Wno-unused-variable -MD")
        helper.write_text(text)
        self.assert_driver_ran(self.run_wrapper(*arguments), "flags-marker")
        self.assertEqual(self.launch_count(), 4)

    def test_corrupt_and_incomplete_entries_are_never_executed(self):
        arguments = self.success_arguments("integrity-marker")
        self.assert_driver_ran(self.run_wrapper(*arguments), "integrity-marker")
        for artifact in self.cache_artifacts():
            artifact.write_bytes(b"corrupt")
        self.assert_driver_ran(self.run_wrapper(*arguments), "integrity-marker")
        self.assertEqual(self.launch_count(), 4)
        for artifact in self.cache_artifacts():
            artifact.write_bytes(b"corrupt-again")
        marker = next(self.root.glob(".cache/bootstrap-driver/*/*/*.complete"))
        marker.with_name("incomplete.complete").write_text("{\"version\": 1")
        self.assert_driver_ran(self.run_wrapper(*arguments), "integrity-marker")
        self.assertEqual(self.launch_count(), 6)

    def test_required_rebuild_failure_propagates_without_stale_execution(self):
        arguments = self.success_arguments("failure-marker")
        self.assert_driver_ran(self.run_wrapper(*arguments), "failure-marker")
        self.dependency.write_text("#define BUSTER_FAKE 3\n")
        result = self.run_wrapper(*arguments, environment={"BUSTER_FAKE_TCC_FAIL": "23"})
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertNotIn("failure-marker", result.stdout)
        self.assertEqual(self.launch_count(), 4)
        cache_root = self.root / ".cache/bootstrap-driver"
        self.assertEqual(list(cache_root.rglob(".bootstrap-*")), [])

    def test_source_change_during_compile_is_not_published(self):
        result = self.run_wrapper(*self.success_arguments("mutation-marker"), environment={"BUSTER_FAKE_TCC_MUTATE": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("mutation-marker", result.stdout)
        self.assertEqual(list(self.root.glob(".cache/bootstrap-driver/*/*/*.complete")), [])

    def test_cache_ownership_failure_propagates_before_execution(self):
        platform = "windows" if os.name == "nt" else "posix"
        cache_parent = self.root / ".cache/bootstrap-driver"
        cache_parent.mkdir(parents=True)
        (cache_parent / platform).write_text("not a directory")
        result = self.run_wrapper(*self.success_arguments("ownership-marker"))
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("ownership-marker", result.stdout)
        self.assertEqual(self.launch_count(), 0)

    def test_cached_driver_exit_status_is_preserved(self):
        arguments = self.success_arguments("status-marker")
        self.assert_driver_ran(self.run_wrapper(*arguments), "status-marker")
        if os.name == "nt":
            result = self.run_wrapper("definitely-not-a-real-buster-bootstrap-command")
            self.assertNotEqual(result.returncode, 0)
        else:
            result = self.run_wrapper(*arguments, environment={"BUSTER_FAKE_DRIVER_STATUS": "37"})
            self.assertEqual(result.returncode, 37)
        self.assertEqual(self.launch_count(), 2)

    def test_concurrent_cold_publication_uses_immutable_outputs(self):
        self.environment["BUSTER_FAKE_TCC_DELAY"] = "0.05"
        processes = []
        for index in range(6):
            arguments = self.success_arguments("concurrent-%d" % index)
            if os.name == "nt":
                command = [self.shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(self.root / "build.ps1"), *arguments]
            else:
                command = ["bash", str(self.root / "build.sh"), *arguments]
            processes.append(subprocess.Popen(command, cwd=self.root, env=self.environment, text=True,
                                              stdout=subprocess.PIPE, stderr=subprocess.PIPE))
        for index, process in enumerate(processes):
            stdout, stderr = process.communicate(timeout=20)
            self.assertEqual(process.returncode, 0, stderr)
            self.assertIn("cmd.exe" if os.name == "nt" else "concurrent-%d" % index, stdout)
        artifacts = self.cache_artifacts()
        self.assertGreaterEqual(len(artifacts), 1)
        self.assertEqual(len(artifacts), len({path.name for path in artifacts}))
        for marker in self.root.glob(".cache/bootstrap-driver/*/*/*.complete"):
            if os.name == "nt":
                data = json.loads(marker.read_text(encoding="utf-8-sig"))
                self.assertTrue((marker.parent / data["artifact"]).is_file())
            else:
                self.assertEqual(marker.read_text().splitlines()[-1], "END")


class BootstrapBuildGraphTests(unittest.TestCase):
    def test_recursive_targets_use_the_selected_immutable_driver(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        driver = (ROOT / "build.c").read_text()
        self.assertIn('set(BUSTER_CLANG_ANALYZE_DRIVER "${BUSTER_BUILD_DRIVER}")', cmake)
        self.assertEqual(cmake.count('"${BUSTER_BUILD_DRIVER}"'), 3)
        self.assertIn('S8("BUSTER_BUILD_DRIVER"), build_running_driver(arena)', driver)
        self.assertNotIn('os_path_absolute(arena, S8("build/build.exe"), true)', driver)
        self.assertNotIn('os_path_absolute(arena, S8("build/build"), true)', driver)


if __name__ == "__main__":
    unittest.main()
