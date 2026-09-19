#!/usr/bin/env python3
"""Controlled cross-platform tests for the immutable TCC bootstrap cache."""
import json
import os
import platform
import re
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]


# These are harness deadlines, not compiler performance budgets. The Windows
# lane in #701 spent ~48 s in a passing wrapper test; keep Unix's tighter limit.
# See docs/ci-bootstrap-wrapper.md for the independent suite budget.
WRAPPER_TIMEOUT_SECONDS = 120 if os.name == "nt" else 20
CLEANUP_TIMEOUT_SECONDS = 10


class WrapperProcess:
    """Own one test child and its output, including assertion/timeout cleanup."""
    def __init__(self, owner, command, cwd, environment):
        self.started = time.monotonic()
        self.test_id = owner.id()
        self.cleaned = False
        # File-backed capture cannot block on pipe capacity or an inherited
        # pipe handle while another concurrent publisher is being collected.
        self.stdout = tempfile.TemporaryFile()
        owner.addCleanup(self.stdout.close)
        self.stderr = tempfile.TemporaryFile()
        owner.addCleanup(self.stderr.close)
        self.process = subprocess.Popen(command, cwd=cwd, env=environment,
                                        stdout=self.stdout, stderr=self.stderr,
                                        start_new_session=os.name != "nt")
        # Register each child immediately, before launching the next one or
        # making any assertions. unittest cleans children before fixture files.
        owner.addCleanup(self.cleanup)
        print("BOOTSTRAP_PROCESS " + json.dumps({
            "event": "start", "test": self.test_id, "pid": self.process.pid,
            "argv": command, "timeout_seconds": WRAPPER_TIMEOUT_SECONDS,
        }), flush=True)

    def cleanup(self):
        if not self.cleaned:
            try:
                if os.name == "nt":
                    if self.process.poll() is None:
                        taskkill = os.path.join(os.environ["SystemRoot"], "System32", "taskkill.exe")
                        result = subprocess.run([taskkill, "/PID", str(self.process.pid), "/T", "/F"],
                                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                                timeout=CLEANUP_TIMEOUT_SECONDS)
                        if result.returncode != 0:
                            raise RuntimeError("bootstrap process-tree cleanup failed: " +
                                               result.stdout.decode(errors="replace"))
                else:
                    try:
                        os.killpg(self.process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            finally:
                # Even a tree-cleanup error must reap the direct child. The
                # error still propagates; a failed cleanup never becomes a pass.
                if self.process.poll() is None:
                    self.process.kill()
                self.process.wait(timeout=CLEANUP_TIMEOUT_SECONDS)
                self.cleaned = True

    def finish(self, timeout_seconds=None):
        # Each concurrent child's deadline starts at launch, not when its turn
        # in the collection loop arrives. A completed child can be waited at 0.
        remaining = (max(0, self.started + WRAPPER_TIMEOUT_SECONDS - time.monotonic())
                     if timeout_seconds is None else timeout_seconds)
        timed_out = False
        try:
            self.process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            timed_out = True
        finally:
            self.cleanup()
        self.stdout.seek(0)
        self.stderr.seek(0)
        stdout = self.stdout.read().decode(errors="replace")
        stderr = self.stderr.read().decode(errors="replace")
        print("BOOTSTRAP_PROCESS " + json.dumps({
            "event": "finish", "test": self.test_id, "pid": self.process.pid,
            "observed_elapsed_seconds": round(time.monotonic() - self.started, 3),
            "returncode": self.process.returncode, "timed_out": timed_out,
        }), flush=True)
        if timed_out:
            raise AssertionError("bootstrap child timed out: " + repr(self.process.args) +
                                 "\nstdout:\n" + stdout + "\nstderr:\n" + stderr)
        return subprocess.CompletedProcess(self.process.args, self.process.returncode, stdout, stderr)


class BootstrapTimingResult(unittest.TextTestResult):
    def startTest(self, test):
        super().startTest(test)
        self.started = time.monotonic()
        print("BOOTSTRAP_TEST " + json.dumps({"event": "start", "test": test.id()}), flush=True)

    def stopTest(self, test):
        print("BOOTSTRAP_TEST " + json.dumps({
            "event": "finish", "test": test.id(),
            "elapsed_seconds": round(time.monotonic() - self.started, 3),
        }), flush=True)
        super().stopTest(test)


class BootstrapTestRunner(unittest.TextTestRunner):
    resultclass = BootstrapTimingResult


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
        return WrapperProcess(self, command, self.root.parent, active_environment).finish()

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
            processes.append(WrapperProcess(self, command, self.root, self.environment))
        for index, process in enumerate(processes):
            result = process.finish()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("cmd.exe" if os.name == "nt" else "concurrent-%d" % index, result.stdout)
        artifacts = self.cache_artifacts()
        self.assertGreaterEqual(len(artifacts), 1)
        self.assertEqual(len(artifacts), len({path.name for path in artifacts}))
        for marker in self.root.glob(".cache/bootstrap-driver/*/*/*.complete"):
            if os.name == "nt":
                data = json.loads(marker.read_text(encoding="utf-8-sig"))
                self.assertTrue((marker.parent / data["artifact"]).is_file())
            else:
                self.assertEqual(marker.read_text().splitlines()[-1], "END")


class BootstrapProcessTests(unittest.TestCase):
    def test_nonzero_status_and_both_streams_are_preserved(self):
        command = [sys.executable, "-c",
                   "import sys; print('stdout-marker'); print('stderr-marker', file=sys.stderr); sys.exit(37)"]
        result = WrapperProcess(self, command, ROOT, os.environ.copy()).finish()
        self.assertEqual(result.returncode, 37)
        self.assertEqual(result.stdout.strip(), "stdout-marker")
        self.assertEqual(result.stderr.strip(), "stderr-marker")

    def test_timeout_is_a_failure_and_reaps_the_child(self):
        child = WrapperProcess(self, [sys.executable, "-c", "import time; time.sleep(60)"],
                               ROOT, os.environ.copy())
        with self.assertRaisesRegex(AssertionError, "bootstrap child timed out"):
            child.finish(timeout_seconds=0.05)
        self.assertIsNotNone(child.process.poll())
        child.cleanup()  # A later unittest cleanup must be harmless.

    def test_collection_does_not_restart_an_expired_launch_deadline(self):
        child = WrapperProcess(self, [sys.executable, "-c", "import time; time.sleep(60)"],
                               ROOT, os.environ.copy())
        child.started -= WRAPPER_TIMEOUT_SECONDS + 1
        with self.assertRaisesRegex(AssertionError, "bootstrap child timed out"):
            child.finish()
        self.assertIsNotNone(child.process.poll())

    def test_early_failure_cleanup_reaps_all_launched_children(self):
        owner = unittest.TestCase()
        self.addCleanup(owner.doCleanups)
        children = [WrapperProcess(owner, [sys.executable, "-c", "import time; time.sleep(60)"],
                                   ROOT, os.environ.copy()) for _ in range(3)]
        # Exercise the cleanup path used when publication fails before any
        # result is collected, rather than explicitly finishing every child.
        self.assertTrue(owner.doCleanups())
        for child in children:
            self.assertIsNotNone(child.process.poll())
            self.assertTrue(child.stdout.closed)
            self.assertTrue(child.stderr.closed)


class BootstrapWorkflowTests(unittest.TestCase):
    def setUp(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        self.desktop = text.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        self.steps = dict(re.findall(r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)",
                                     self.desktop))

    def test_wrapper_step_has_an_independent_bounded_budget_and_retained_log(self):
        block = self.steps["Bootstrap wrapper regression tests"]
        self.assertIn("id: bootstrap_wrappers", block)
        self.assertIn("if: ${{ !cancelled() && steps.checkout.outcome == 'success' }}", block)
        self.assertIn("timeout-minutes: ${{ matrix.platform == 'windows' && 20 || 2 }}", block)
        self.assertIn("set -euo pipefail", block)
        self.assertIn('tests/bootstrap_wrapper_test.py -v 2>&1 | tee "$RUNNER_TEMP/buster-ci/bootstrap-wrapper.log"', block)
        self.assertNotIn("continue-on-error:", block)
        self.assertEqual(self.desktop.count("tests/bootstrap_wrapper_test.py -v"), 1)
        policy = self.steps["Workflow tool regression tests"]
        self.assertIn("timeout-minutes: ${{ matrix.lane == 'windows-aarch64' && 5 || 2 }}", policy)
        self.assertNotIn("bootstrap_wrapper_test.py", policy)
        for suite in ("tests/ci_tools_test.py", "tools/analyzer_selection_test.py", "tools/differential_ci_policy_test.py"):
            self.assertIn(suite + " -v", policy)
        self.assertIn("path: ${{ runner.temp }}/buster-ci/", self.steps["Retain desktop logs"])
        self.assertEqual(len(re.findall(r"(?m)^          - name:", self.desktop)), 6)

    def test_both_desktop_required_lists_reject_unsuccessful_wrapper_work(self):
        sys.path.insert(0, str(ROOT / "tools"))
        self.addCleanup(sys.path.pop, 0)
        import ci_summary
        summary = self.steps["Desktop result and reproduction"]
        self.assertIn("always()", summary)
        self.assertIn("tools/ci_summary.py", summary)
        expression = re.search(r"BUSTER_CI_REQUIRED: (.+)", summary).group(1)
        lists = re.findall(r"'(workflow_tools[^']*)'", expression)
        self.assertEqual(lists, ["workflow_tools bootstrap_wrappers zig combinations_unix",
                                 "workflow_tools bootstrap_wrappers zig combinations_windows"])
        for required in lists:
            for outcome in (None, "skipped", "cancelled", "failure", "timed_out", "success"):
                with self.subTest(required=required, outcome=outcome):
                    steps = {name: {"outcome": "success"} for name in required.split()}
                    if outcome is None:
                        del steps["bootstrap_wrappers"]
                    else:
                        steps["bootstrap_wrappers"] = {"outcome": outcome, "conclusion": "success"}
                    self.assertEqual(ci_summary.assess(steps, required.split()),
                                     [] if outcome == "success" else ["bootstrap_wrappers"])


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
    print("BOOTSTRAP_ENVIRONMENT " + json.dumps({
        "os": platform.system(), "machine": platform.machine(),
        "python": sys.executable, "python_version": platform.python_version(),
        "shell": (shutil.which("powershell.exe") or shutil.which("pwsh.exe"))
                 if os.name == "nt" else shutil.which("bash"),
        "wrapper_timeout_seconds": WRAPPER_TIMEOUT_SECONDS,
    }), flush=True)
    unittest.main(testRunner=BootstrapTestRunner)
