#!/usr/bin/env python3
"""Regressions for the Windows CI Visual Studio shell helper and its wiring."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "tools/ci_vs_dev_shell.ps1"
INVOCATION = "& .\\tools\\ci_vs_dev_shell.ps1 "
# Launch-relative deadline for one helper step process, as before #2021.
PROBE_TIMEOUT_SECONDS = 120
# Seven simultaneous Windows PowerShell starts exceeded that per-owner deadline
# on hosted ARM64 even while the complete workflow-tools step fit its budget.
PROBE_CONCURRENT_LIMIT = 2
PROBE_CLEANUP_SECONDS = 10


def workflow_step(text, name):
    block = text.split("      - name: " + name + "\n", 1)[1]
    return block.split("\n      - name:", 1)[0]


def helper_call(step):
    start = step.index(INVOCATION) + len(INVOCATION)
    lines = [step[start:].split("\n", 1)[0]]
    rest = step[start:].split("\n", 1)[1].splitlines()
    while lines[-1].endswith("`"):
        lines.append(rest.pop(0).strip())
    return " ".join(line.rstrip("`").strip() for line in lines)


class WorkflowWiringTests(unittest.TestCase):
    def setUp(self):
        self.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

    def test_every_windows_shell_uses_the_helper_with_its_own_options(self):
        expected = {
            "Combination matrix (Windows)": (
                "-Arch $env:VS_ARCH -Component $env:VS_COMPONENT "
                "-LlvmBin $env:BUSTER_CI_LLVM_BIN -GithubEnv $env:GITHUB_ENV "
                "-Require cl, clang, gcc, zig -ClangArch $env:CLANG_ARCH"),
            "Execution-mode matrix (Windows)": (
                "-Arch $env:VS_ARCH -Component $env:VS_COMPONENT "
                "-LlvmBin $env:BUSTER_CI_LLVM_BIN -Require cl, clang -ClangArch $env:CLANG_ARCH"),
            # The MSVC reference step deliberately keeps the lighter shell.
            "Native MSVC reference differential": "-Arch $env:VS_ARCH -Component $env:VS_COMPONENT",
        }
        for name, arguments in expected.items():
            with self.subTest(step=name):
                step = workflow_step(self.workflow, name)
                self.assertIn("        shell: powershell\n", step)
                self.assertIn("VS_ARCH: ${{ matrix.vs_arch }}", step)
                self.assertIn("VS_COMPONENT: ${{ matrix.vs_component }}", step)
                self.assertEqual(step.count(INVOCATION), 1)
                self.assertEqual(helper_call(step), arguments)
                self.assertLess(step.index("$ErrorActionPreference = 'Stop'"), step.index(INVOCATION))
                self.assertEqual("CLANG_ARCH: ${{ matrix.clang_arch }}" in step, "-ClangArch" in arguments)

    def test_workflow_keeps_no_inline_copy_of_the_shell_setup(self):
        self.assertEqual(self.workflow.count(INVOCATION), 3)
        for fragment in ("vswhere", "Launch-VsDevShell", "BUSTER_CI_WINDOWS_COMPILER_PATH", "Target:\\s*"):
            self.assertNotIn(fragment, self.workflow)

    def test_helper_fails_by_throwing_and_keeps_the_host_architecture(self):
        helper = HELPER.read_text(encoding="utf-8")
        self.assertIsNone(re.search(r"(?mi)^[^#\n]*\bexit\b", helper))
        self.assertIn("-HostArch amd64 -SkipAutomaticLocation", helper)
        order = [helper.index(fragment) for fragment in (
            "Launch-VsDevShell.ps1') -Arch", '$env:PATH = "$LlvmBin;$env:PATH"',
            "PATH<<$PathDelimiter", "where.exe $Compiler", "& clang --version")]
        self.assertEqual(order, sorted(order))


class Probe:
    """One helper step process, launched without waiting for it to finish."""
    def __init__(self, shell, directory, vswhere, arguments, environment):
        directory.mkdir(parents=True)
        self.launch_log = directory / "launch.log"
        self.github_env = directory / "github-env"
        self.stdout_path = directory / "stdout.txt"
        self.stderr_path = directory / "stderr.txt"
        script = directory / "step.ps1"
        script.write_text(
            "$ErrorActionPreference = 'Stop'\n"
            f"& '{HELPER}' -VsWhere '{vswhere}' -Arch amd64 -Component fixture.component "
            + arguments.replace("<github_env>", str(self.github_env)) + "\n"
            "Write-Output \"AFTER_PATH=$env:PATH\"\n"
            "Write-Output \"AFTER_TARGET=$env:VSCMD_ARG_TGT_ARCH\"\n",
            encoding="utf-8")
        self.command = [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)]
        self.returncode = None
        self.timed_out = False
        self.stdout = self.stderr = ""
        self.started = time.monotonic()
        # File-backed capture: collecting one child can never block on another
        # child's pipe capacity or on a pipe handle inherited by a grandchild.
        self.outputs = (self.stdout_path.open("wb"), self.stderr_path.open("wb"))
        try:
            self.process = subprocess.Popen(
                self.command, env=dict(environment, FAKE_LAUNCH_LOG=str(self.launch_log)),
                stdin=subprocess.DEVNULL, stdout=self.outputs[0], stderr=self.outputs[1])
        except BaseException:
            for output in self.outputs:
                output.close()
            raise

    def finish(self):
        """Wait until this probe's own launch-relative deadline, then record it."""
        remaining = self.started + PROBE_TIMEOUT_SECONDS - time.monotonic()
        try:
            self.returncode = self.process.wait(timeout=max(remaining, 0))
        except subprocess.TimeoutExpired:
            self.timed_out = True
            self.stop()
        for output in self.outputs:
            output.close()
        self.stdout = self.stdout_path.read_text(errors="replace")
        self.stderr = self.stderr_path.read_text(errors="replace")

    def stop(self):
        if self.process.poll() is None:
            if os.name == "nt":
                taskkill = os.path.join(os.environ["SystemRoot"], "System32", "taskkill.exe")
                subprocess.run([taskkill, "/PID", str(self.process.pid), "/T", "/F"],
                               capture_output=True, timeout=PROBE_CLEANUP_SECONDS, check=False)
            if self.process.poll() is None:
                self.process.kill()
        self.process.wait(timeout=PROBE_CLEANUP_SECONDS)
        for output in self.outputs:
            output.close()


def probe_plan_run(shell, root, llvm_bin, plan, environment, probes):
    """Start at most two independent helper owners; preserve all plan slots."""
    entries = list(plan.items())
    for start in range(0, len(entries), PROBE_CONCURRENT_LIMIT):
        batch = []
        for index in range(start, min(start + PROBE_CONCURRENT_LIMIT, len(entries))):
            name, (vswhere, overrides, arguments) = entries[index]
            probe = Probe(shell, root / f"probe-{index}", vswhere,
                          arguments.replace("<llvm_bin>", str(llvm_bin)),
                          dict(environment, **overrides))
            # The caller registers cleanup of this shared dictionary before
            # admission, including the partial batch on a later launch failure.
            probes[name] = probe
            batch.append((name, probe))
            print(f"VS_PROBE_START case={name!r} slot={index} live_limit={PROBE_CONCURRENT_LIMIT}", flush=True)
        for name, probe in batch:
            probe.finish()
            print(f"VS_PROBE_END case={name!r} returncode={probe.returncode} timed_out={int(probe.timed_out)} "
                  f"elapsed_seconds={time.monotonic() - probe.started:.3f}", flush=True)


@unittest.skipUnless(os.name == "nt", "The Visual Studio shell exists only on Windows runners")
class HelperBehaviorTests(unittest.TestCase):
    # Each step is still a separate powershell.exe process with its own script,
    # launch log and GITHUB_ENV file, exactly as a workflow step runs it. Every
    # Windows PowerShell start costs about 23 s on the hosted AArch64 runner,
    # so two owners overlap per batch. Seven simultaneous starts exceeded each
    # owner's 120 s deadline on ARM64 while other suites also compiled fixtures.
    # All seven cases and their launch-relative deadlines remain intact.
    FAILURES = {
        "wrong clang target": ({"FAKE_CLANG_TARGET": "aarch64-pc-windows-msvc"},
                               "-LlvmBin '<llvm_bin>' -Require cl, clang -ClangArch x86_64"),
        "missing compiler": ({}, "-LlvmBin '<llvm_bin>' -Require buster-fixture-missing-compiler"),
        "no installation": ({"FAKE_VS_PATH": ""}, ""),
        "vswhere failure": ({"FAKE_VSWHERE_STATUS": "3"}, ""),
    }

    @classmethod
    def setUpClass(cls):
        shell = shutil.which("powershell.exe") or shutil.which("pwsh.exe")
        if shell is None:
            raise AssertionError("neither powershell.exe nor pwsh.exe is on PATH")
        temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temporary.cleanup)
        cls.root = Path(temporary.name)
        tools = cls.root / "vs/Common7/Tools"
        tools.mkdir(parents=True)
        cls.vs_bin = cls.root / "vs/bin"
        cls.vs_bin.mkdir()
        (tools / "Launch-VsDevShell.ps1").write_text(
            "param([string]$Arch, [string]$HostArch, [switch]$SkipAutomaticLocation)\n"
            "Add-Content -LiteralPath $env:FAKE_LAUNCH_LOG -Value \"arch=$Arch host=$HostArch skip=$SkipAutomaticLocation\"\n"
            "$env:VSCMD_ARG_TGT_ARCH = $Arch\n"
            f"$env:PATH = '{cls.vs_bin};' + $env:PATH\n",
            encoding="utf-8")
        for compiler in ("cl", "gcc", "zig"):
            (cls.vs_bin / f"{compiler}.cmd").write_text("@echo off\r\n")
        cls.llvm_bin = cls.root / "llvm/bin"
        cls.llvm_bin.mkdir(parents=True)
        (cls.llvm_bin / "clang.cmd").write_text(
            "@echo off\r\necho clang version fixture\r\necho Target: %FAKE_CLANG_TARGET%\r\n")
        vswhere = cls.root / "vswhere.cmd"
        vswhere.write_text(
            "@echo off\r\nif defined FAKE_VS_PATH echo %FAKE_VS_PATH%\r\nexit /b %FAKE_VSWHERE_STATUS%\r\n")
        environment = dict(
            os.environ,
            FAKE_VS_PATH=str(cls.root / "vs"),
            FAKE_VSWHERE_STATUS="0",
            FAKE_CLANG_TARGET="x86_64-pc-windows-msvc",
        )
        plan = {
            "combination": (vswhere, {}, (
                "-LlvmBin '<llvm_bin>' -GithubEnv '<github_env>' "
                "-Require cl, clang, gcc, zig -ClangArch x86_64")),
            "msvc": (vswhere, {"FAKE_CLANG_TARGET": "wrong-target"}, ""),
            "missing vswhere": (cls.root / "missing-vswhere.cmd", {}, ""),
        }
        for name, (overrides, arguments) in cls.FAILURES.items():
            plan[name] = (vswhere, overrides, arguments)
        cls.probes = {}
        # Registered before the first launch and run before the fixture
        # directory is removed, so an early failure never leaks a child.
        cls.addClassCleanup(cls.stop_probes)
        probe_plan_run(shell, cls.root, cls.llvm_bin, plan, environment, cls.probes)

    @classmethod
    def stop_probes(cls):
        first_error = None
        for probe in cls.probes.values():
            try:
                probe.stop()
            except BaseException as error:
                # Failure of one cleanup must not abandon other owned children.
                if first_error is None:
                    first_error = error
        if first_error is not None:
            raise first_error

    def probe(self, name):
        probe = self.probes[name]
        self.assertFalse(probe.timed_out, f"{probe.command} exceeded {PROBE_TIMEOUT_SECONDS}s: "
                                          + probe.stdout + probe.stderr)
        return probe

    def test_combination_shell_exports_path_and_probes_every_compiler(self):
        result = self.probe("combination")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.launch_log.read_text().split(), ["arch=amd64", "host=amd64", "skip=True"])
        after = re.search(r"AFTER_PATH=(.*)", result.stdout).group(1)
        self.assertTrue(after.startswith(f"{self.llvm_bin};{self.vs_bin};"), after)
        self.assertIn("AFTER_TARGET=amd64", result.stdout)
        self.assertIn("Target: x86_64-pc-windows-msvc", result.stdout)
        exported = result.github_env.read_text().splitlines()
        self.assertEqual(exported[0], "PATH<<BUSTER_CI_WINDOWS_COMPILER_PATH")
        self.assertEqual(exported[1], after)
        self.assertEqual(exported[2:], ["BUSTER_CI_WINDOWS_COMPILER_PATH", "VSCMD_ARG_TGT_ARCH=amd64"])

    def test_msvc_shell_leaves_llvm_and_clang_alone(self):
        result = self.probe("msvc")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(str(self.llvm_bin), result.stdout)
        self.assertNotIn("clang version fixture", result.stdout)
        self.assertFalse(result.github_env.exists())

    def test_failures_stop_the_calling_step(self):
        for name in self.FAILURES:
            with self.subTest(case=name):
                result = self.probe(name)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("AFTER_PATH=", result.stdout)
        result = self.probe("missing vswhere")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("vswhere was not found", result.stdout + result.stderr)


class ProbeSchedulingTests(unittest.TestCase):
    def plan(self):
        names = ["combination", "msvc", "missing vswhere", *HelperBehaviorTests.FAILURES]
        return {name: (Path("vswhere"), {}, "") for name in names}

    def run_fake_plan(self, timeout_slot=None, launch_error_slot=None):
        live = []
        events = []
        probes = {}

        class FakeProbe:
            def __init__(self, shell, directory, vswhere, arguments, environment):
                self.slot = int(directory.name.split("-")[1])
                if self.slot == launch_error_slot:
                    raise OSError("fixture launch failure")
                self.returncode = None
                self.timed_out = False
                self.started = 0
                live.append(self)
                events.append(("start", self.slot, len(live)))

            def finish(self):
                self.timed_out = self.slot == timeout_slot
                self.returncode = -1 if self.timed_out else 0
                live.remove(self)
                events.append(("finish", self.slot, len(live)))

            def stop(self):
                if self in live:
                    live.remove(self)
                    events.append(("stop", self.slot, len(live)))

        with mock.patch.object(sys.modules[__name__], "Probe", FakeProbe), mock.patch("builtins.print"):
            try:
                probe_plan_run("shell", Path("fixture"), Path("llvm"), self.plan(), {}, probes)
            except OSError as error:
                events.append(("error", str(error), len(live)))
            finally:
                owner = type("Owner", (), {"probes": probes})
                HelperBehaviorTests.stop_probes.__func__(owner)
        return probes, events, live

    def test_two_owner_peak_and_complete_case_inventory(self):
        probes, events, live = self.run_fake_plan()
        self.assertEqual(PROBE_CONCURRENT_LIMIT, 2)
        self.assertEqual(list(probes), list(self.plan()))
        self.assertEqual([event[1] for event in events if event[0] == "finish"], list(range(7)))
        self.assertEqual(max(event[2] for event in events), 2)
        self.assertFalse(live)

    def test_timeout_does_not_drop_later_cases(self):
        probes, events, live = self.run_fake_plan(timeout_slot=0)
        self.assertTrue(probes["combination"].timed_out)
        self.assertEqual(len([event for event in events if event[0] == "finish"]), 7)
        self.assertTrue(all(probe.returncode == 0 for name, probe in probes.items() if name != "combination"))
        self.assertFalse(live)

    def test_launch_failure_cleans_every_already_owned_child(self):
        probes, events, live = self.run_fake_plan(launch_error_slot=3)
        self.assertEqual(len(probes), 3)
        self.assertIn(("error", "fixture launch failure", 1), events)
        self.assertIn(("stop", 2, 0), events)
        self.assertFalse(live)

    def test_original_launch_deadline_is_not_reset_at_collection(self):
        probe = Probe.__new__(Probe)
        probe.started = 100
        probe.timed_out = False
        probe.process = mock.Mock()
        probe.process.wait.return_value = 0
        probe.outputs = (mock.Mock(), mock.Mock())
        probe.stdout_path = mock.Mock()
        probe.stderr_path = mock.Mock()
        with mock.patch("time.monotonic", return_value=190):
            probe.finish()
        probe.process.wait.assert_called_once_with(timeout=30)
        self.assertFalse(probe.timed_out)
        self.assertEqual(PROBE_TIMEOUT_SECONDS, 120)

    def test_expired_owner_is_stopped_before_capture_read(self):
        probe = Probe.__new__(Probe)
        probe.started = 100
        probe.timed_out = False
        probe.process = mock.Mock()
        probe.process.wait.side_effect = subprocess.TimeoutExpired("probe", 0)
        probe.stop = mock.Mock()
        probe.outputs = (mock.Mock(), mock.Mock())
        probe.stdout_path = mock.Mock()
        probe.stderr_path = mock.Mock()
        with mock.patch("time.monotonic", return_value=221):
            probe.finish()
        probe.process.wait.assert_called_once_with(timeout=0)
        probe.stop.assert_called_once_with()
        self.assertTrue(probe.timed_out)

    def test_cleanup_failure_still_stops_other_owners_and_fails(self):
        first, second = mock.Mock(), mock.Mock()
        first.stop.side_effect = OSError("fixture cleanup failure")
        owner = type("Owner", (), {"probes": {"first": first, "second": second}})
        with self.assertRaisesRegex(OSError, "fixture cleanup failure"):
            HelperBehaviorTests.stop_probes.__func__(owner)
        second.stop.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
