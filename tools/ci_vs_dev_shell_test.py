#!/usr/bin/env python3
"""Regressions for the Windows CI Visual Studio shell helper and its wiring."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "tools/ci_vs_dev_shell.ps1"
INVOCATION = "& .\\tools\\ci_vs_dev_shell.ps1 "


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


@unittest.skipUnless(os.name == "nt", "The Visual Studio shell exists only on Windows runners")
class HelperBehaviorTests(unittest.TestCase):
    def setUp(self):
        self.shell = shutil.which("powershell.exe") or shutil.which("pwsh.exe")
        self.assertIsNotNone(self.shell)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        tools = self.root / "vs/Common7/Tools"
        tools.mkdir(parents=True)
        self.vs_bin = self.root / "vs/bin"
        self.vs_bin.mkdir()
        (tools / "Launch-VsDevShell.ps1").write_text(
            "param([string]$Arch, [string]$HostArch, [switch]$SkipAutomaticLocation)\n"
            "Add-Content -LiteralPath $env:FAKE_LAUNCH_LOG -Value \"arch=$Arch host=$HostArch skip=$SkipAutomaticLocation\"\n"
            "$env:VSCMD_ARG_TGT_ARCH = $Arch\n"
            f"$env:PATH = '{self.vs_bin};' + $env:PATH\n",
            encoding="utf-8")
        for compiler in ("cl", "gcc", "zig"):
            (self.vs_bin / f"{compiler}.cmd").write_text("@echo off\r\n")
        self.llvm_bin = self.root / "llvm/bin"
        self.llvm_bin.mkdir(parents=True)
        (self.llvm_bin / "clang.cmd").write_text(
            "@echo off\r\necho clang version fixture\r\necho Target: %FAKE_CLANG_TARGET%\r\n")
        self.vswhere = self.root / "vswhere.cmd"
        self.vswhere.write_text(
            "@echo off\r\nif defined FAKE_VS_PATH echo %FAKE_VS_PATH%\r\nexit /b %FAKE_VSWHERE_STATUS%\r\n")
        self.github_env = self.root / "github-env"
        self.launch_log = self.root / "launch.log"
        self.environment = dict(
            os.environ,
            FAKE_VS_PATH=str(self.root / "vs"),
            FAKE_VSWHERE_STATUS="0",
            FAKE_CLANG_TARGET="x86_64-pc-windows-msvc",
            FAKE_LAUNCH_LOG=str(self.launch_log),
        )

    def run_step(self, arguments, **environment):
        script = self.root / "step.ps1"
        script.write_text(
            "$ErrorActionPreference = 'Stop'\n"
            f"& '{HELPER}' -VsWhere '{self.vswhere}' -Arch amd64 -Component fixture.component {arguments}\n"
            "Write-Output \"AFTER_PATH=$env:PATH\"\n"
            "Write-Output \"AFTER_TARGET=$env:VSCMD_ARG_TGT_ARCH\"\n",
            encoding="utf-8")
        return subprocess.run(
            [self.shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
            env=dict(self.environment, **environment), capture_output=True, text=True, timeout=120)

    def test_combination_shell_exports_path_and_probes_every_compiler(self):
        result = self.run_step(
            f"-LlvmBin '{self.llvm_bin}' -GithubEnv '{self.github_env}' "
            "-Require cl, clang, gcc, zig -ClangArch x86_64")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.launch_log.read_text().split(), ["arch=amd64", "host=amd64", "skip=True"])
        after = re.search(r"AFTER_PATH=(.*)", result.stdout).group(1)
        self.assertTrue(after.startswith(f"{self.llvm_bin};{self.vs_bin};"), after)
        self.assertIn("AFTER_TARGET=amd64", result.stdout)
        self.assertIn("Target: x86_64-pc-windows-msvc", result.stdout)
        exported = self.github_env.read_text().splitlines()
        self.assertEqual(exported[0], "PATH<<BUSTER_CI_WINDOWS_COMPILER_PATH")
        self.assertEqual(exported[1], after)
        self.assertEqual(exported[2:], ["BUSTER_CI_WINDOWS_COMPILER_PATH", "VSCMD_ARG_TGT_ARCH=amd64"])

    def test_msvc_shell_leaves_llvm_and_clang_alone(self):
        result = self.run_step("", FAKE_CLANG_TARGET="wrong-target")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(str(self.llvm_bin), result.stdout)
        self.assertNotIn("clang version fixture", result.stdout)
        self.assertFalse(self.github_env.exists())

    def test_failures_stop_the_calling_step(self):
        cases = {
            "wrong clang target": ({"FAKE_CLANG_TARGET": "aarch64-pc-windows-msvc"},
                                   f"-LlvmBin '{self.llvm_bin}' -Require cl, clang -ClangArch x86_64"),
            "missing compiler": ({}, f"-LlvmBin '{self.llvm_bin}' -Require buster-fixture-missing-compiler"),
            "no installation": ({"FAKE_VS_PATH": ""}, ""),
            "vswhere failure": ({"FAKE_VSWHERE_STATUS": "3"}, ""),
        }
        for name, (environment, arguments) in cases.items():
            with self.subTest(case=name):
                result = self.run_step(arguments, **environment)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("AFTER_PATH=", result.stdout)
        missing = self.root / "missing-vswhere.cmd"
        self.vswhere = missing
        result = self.run_step("")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("vswhere was not found", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
