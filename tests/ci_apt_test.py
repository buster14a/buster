#!/usr/bin/env python3
"""Offline controls for authenticated CI provisioning; no live apt mutation."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_apt
import check_action_pins


class AptInputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.lock = self.root / "lock.json"
        self.data = json.loads((ROOT / ".github/apt-inputs.json").read_text())
        self.lock.write_text(json.dumps(self.data))

    def test_lock_covers_tool_and_readline_source_package_closures(self):
        lock = ci_apt.load_lock(self.lock)
        self.assertEqual(set(lock["profiles"]["gpu"]), {
            "clang-18", "llvm-18", "lld-18", "spirv-tools", "libllvm18",
            "libclang1-18", "libclang-cpp18", "libclang-common-18-dev",
            "llvm-18-runtime", "llvm-18-linker-tools"})
        self.assertEqual(set(lock["profiles"]["readline"]), {
            "libreadline-dev", "libreadline8t64", "readline-common",
            "libncurses-dev", "libncurses6", "libncursesw6", "libtinfo6"})
        self.assertEqual(len({v for p, v in lock["profiles"]["gpu"].items() if p != "spirv-tools"}), 1)
        for name in ("libreadline8t64", "readline-common"):
            self.assertEqual(lock["profiles"]["readline"][name], lock["profiles"]["readline"]["libreadline-dev"])

    def test_rejects_mutable_malformed_or_cross_suite_lock(self):
        mutations = [
            ("snapshot", "latest"), ("snapshot", "20261315T000000Z"),
            ("snapshot", "20260915T000000Z/../current"),
            ("ubuntu", "24.04"), ("suite", "noble"), ("architecture", "arm64"),
            ("schema", 2), ("profiles", {}),
        ]
        for key, value in mutations:
            with self.subTest(key=key, value=value):
                data = copy.deepcopy(self.data)
                data[key] = value
                self.lock.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    ci_apt.load_lock(self.lock)

    def test_rejects_unpinned_or_argument_injected_packages(self):
        for name, version in (("clang-18", "latest"), ("clang-18", "18.*"),
                              ("clang-18", ""), ("--allow-unauthenticated", "1"),
                              ("clang-18", "1 --allow-unauthenticated"),
                              ("clang-18", None), ("clang-18/unstable", "1")):
            with self.subTest(name=name, version=version):
                data = copy.deepcopy(self.data)
                data["profiles"]["gpu"] = {name: version}
                self.lock.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    ci_apt.load_lock(self.lock)

    def test_sources_are_snapshot_only_and_require_ubuntu_signatures(self):
        text = ci_apt.sources(self.data)
        self.assertIn("https://snapshot.ubuntu.com/ubuntu/20260915T000000Z/", text)
        self.assertIn("Suites: resolute resolute-updates resolute-security", text)
        self.assertIn("Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg", text)
        self.assertNotIn("Trusted:", text)
        options = ci_apt.apt_options(self.root)
        settings = dict(item.split("=", 1) for item in options[1::2])
        for key in ("Dir::Etc::sourceparts", "Dir::Etc::preferencesparts"):
            self.assertEqual(settings[key], "-")
        self.assertEqual(settings["Dir::Etc::preferences"], "/dev/null")
        self.assertEqual(settings["Dir::State::lists"], str(self.root / "lists"))
        self.assertEqual(settings["Dir::Cache::archives"], str(self.root / "archives"))
        self.assertEqual(settings["APT::Update::Error-Mode"], "any")
        for key in ("APT::Get::AllowUnauthenticated", "Acquire::AllowInsecureRepositories", "Acquire::AllowDowngradeToInsecureRepositories"):
            self.assertEqual(settings[key], "false")
        self.assertEqual(settings["Acquire::https::Verify-Peer"], "true")
        self.assertEqual(settings["Acquire::https::Verify-Host"], "true")

    def test_mismatched_missing_uninstalled_and_duplicate_packages_fail(self):
        for text in ("p\t2\tamd64\tinstalled\n", "", "p\t1\tarm64\tinstalled\n",
                     "p\t1\tamd64\tunpacked\n", "p\t1\tamd64\tinstalled\np\t1\tamd64\tinstalled\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ci_apt.package_versions(text, {"p": "1"})
        self.assertEqual(ci_apt.package_versions("p\t1\tamd64\tinstalled\nq\t2\tall\tinstalled\n", {"p": "1", "q": "2"}), {"p": "1", "q": "2"})

    def test_selected_paths_must_resolve_to_pinned_payload(self):
        pinned = self.root / "pinned"
        pinned.write_text("pinned")
        alias = self.root / "alias"
        alias.symlink_to(pinned)
        shadow = self.root / "shadow"
        shadow.write_text("different")
        payload = {str(pinned): ci_apt.fact(pinned)}
        self.assertEqual(ci_apt.require_binding(alias, pinned, payload), ci_apt.fact(pinned))
        with self.assertRaisesRegex(ValueError, "unpinned"):
            ci_apt.require_binding(shadow, pinned, payload)
        with self.assertRaisesRegex(ValueError, "unpinned"):
            ci_apt.require_binding(pinned, pinned, {})

    def test_runtime_closure_rejects_missing_and_duplicate_libraries(self):
        library = self.root / "lib.so"
        library.write_text("fixture")
        line = f" lib.so => {library} (0x1234)\n"
        self.assertEqual(ci_apt.runtime_paths(line), {"lib.so": library})
        for text in ("lib.so => not found\n", "", "linux-vdso.so.1 (0x1)\n", line + line):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ci_apt.runtime_paths(text)

    def simulate_install(self, failure=None):
        commands = []
        original_read = Path.read_text
        original_is_file = Path.is_file

        def read(path, *args, **kwargs):
            if str(path) == "/etc/os-release":
                return 'ID=ubuntu\nVERSION_ID="26.04"\n'
            return original_read(path, *args, **kwargs)

        def is_file(path):
            return str(path) == ci_apt.KEYRING or original_is_file(path)

        def run(argv):
            commands.append(argv)
            if argv == ["dpkg", "--print-architecture"]:
                return "amd64\n"
            if failure and failure in argv:
                raise subprocess.CalledProcessError(100, argv)
            return ""

        with mock.patch.object(Path, "read_text", read), mock.patch.object(Path, "is_file", is_file), \
                mock.patch.object(ci_apt, "Commands", return_value=run), mock.patch.object(ci_apt, "inspect") as inspect:
            if failure:
                with self.assertRaises(subprocess.CalledProcessError):
                    ci_apt.install("gpu", self.lock, self.root / "evidence")
                inspect.assert_not_called()
            else:
                ci_apt.install("gpu", self.lock, self.root / "evidence")
                inspect.assert_called_once()
        return commands

    def test_authenticated_update_failure_never_installs_or_inspects(self):
        commands = self.simulate_install("update")
        self.assertFalse(any("install" in argv for argv in commands))
        self.assertEqual(sum("update" in argv for argv in commands), 1)

    def test_unavailable_exact_version_never_falls_back(self):
        commands = self.simulate_install("install")
        self.assertEqual(sum("install" in argv for argv in commands), 1)
        self.assertFalse((self.root / "evidence/pinned.identity.json").exists())

    def test_even_cache_hits_reinstall_every_exact_input_without_removals(self):
        commands = self.simulate_install()
        install = next(argv for argv in commands if "install" in argv)
        for flag in ("--reinstall", "--no-install-recommends", "--allow-downgrades", "--no-remove"):
            self.assertIn(flag, install)
        for name, version in self.data["profiles"]["gpu"].items():
            self.assertIn(name + "=" + version, install)

    def test_existing_evidence_is_never_reused_as_success(self):
        output = self.root / "evidence"
        output.mkdir()
        (output / "pinned.identity.json").write_text("old success")
        with mock.patch.object(ci_apt, "Commands") as commands, self.assertRaises(FileExistsError):
            ci_apt.install("gpu", self.lock, output)
        commands.assert_not_called()

    def test_command_failure_records_diagnostics_and_propagates(self):
        result = subprocess.CompletedProcess(["apt-get"], 100, "out", "signature error")
        with mock.patch.object(ci_apt.subprocess, "run", return_value=result), \
                self.assertRaises(subprocess.CalledProcessError):
            ci_apt.Commands(self.root)(["apt-get", "update"])
        self.assertEqual((self.root / "command-001.stderr").read_text(), "signature error")
        self.assertEqual(json.loads((self.root / "command-001.json").read_text())["status"], 100)

    def test_inspection_binds_llvm_runtime_and_publishes_deterministic_identities(self):
        tool = self.root / "llvm-tool"
        library = self.root / "libLLVM.so.18.1"
        tool.write_text("tool fixture")
        library.write_text("LLVM fixture")

        def run(argv):
            if argv[:2] == ["dpkg-query", "-L"]:
                return f"{library}\n{tool}\n"
            if argv[:2] == ["dpkg-query", "-W"]:
                return "p\t1\tamd64\tinstalled\n" if argv[-1] == "p" else "host\t2\n"
            self.assertEqual(argv, ["ldd", str(tool)])
            return f"libLLVM.so.18.1 => {library} (0x123)\n"

        first = self.root / "first"
        second = self.root / "second"
        for output in (first, second):
            output.mkdir()
            with mock.patch.object(ci_apt, "GPU_TOOLS", {"llvm-tool": str(tool)}), \
                    mock.patch.object(ci_apt.shutil, "which", return_value=str(tool)):
                ci_apt.inspect("gpu", {"p": "1"}, run, output)
        for name in ("pinned.identity.json", "selected.identity.json"):
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())

    def test_inspection_rejects_shadowed_llvm_library_without_success_manifest(self):
        tool = self.root / "llvm-tool"
        shadow = self.root / "shadow-llvm.so"
        tool.write_text("tool fixture")
        shadow.write_text("shadow fixture")
        output = self.root / "inspect"
        output.mkdir()

        def run(argv):
            if argv[:2] == ["dpkg-query", "-L"]:
                return str(tool) + "\n"
            if argv[:2] == ["dpkg-query", "-W"]:
                return "p\t1\tamd64\tinstalled\n"
            return f"libLLVM.so.18.1 => {shadow} (0x123)\n"

        with mock.patch.object(ci_apt, "GPU_TOOLS", {"llvm-tool": str(tool)}), \
                mock.patch.object(ci_apt.shutil, "which", return_value=str(tool)), \
                self.assertRaisesRegex(ValueError, "unpinned library"):
            ci_apt.inspect("gpu", {"p": "1"}, run, output)
        self.assertFalse((output / "pinned.identity.json").exists())
        self.assertFalse((output / "selected.identity.json").exists())

    def test_real_source_reuse_is_explicit_and_same_commit(self):
        real = (ROOT / ".github/workflows/throughput-real-source.yml").read_text()
        qualification = (ROOT / ".github/workflows/apt-inputs.yml").read_text()
        self.assertIn("workflow_call:", real)
        self.assertIn("qualify_pinned_inputs:", real)
        self.assertIn("type: boolean\n        default: false", real)
        self.assertIn("|| inputs.qualify_pinned_inputs ||", real)
        self.assertIn("github.head_ref == 'bench/423-real-source-admission-20260914'", real)
        self.assertIn("real-source:\n    needs: inputs", qualification)
        self.assertIn("uses: ./.github/workflows/throughput-real-source.yml", qualification)
        self.assertIn("qualify_pinned_inputs: true", qualification)
        self.assertNotIn("secrets: inherit", qualification)
        self.assertNotIn("actions: write", qualification)

    def test_only_reviewed_same_commit_local_workflow_is_allowed(self):
        allowed = "./.github/workflows/throughput-real-source.yml"
        self.assertEqual(check_action_pins.APPROVED_LOCAL_WORKFLOWS, {allowed})
        self.assertEqual(check_action_pins.check_text("uses: " + allowed, "case.yml"), [])
        for value in (allowed + "@main", allowed + "@" + "a" * 40,
                      "./local-action", "./.github/workflows/other.yml",
                      "./.github/workflows/../throughput-real-source.yml",
                      "${{ inputs.workflow }}", "actions/checkout@main",
                      "buster14a/buster/.github/workflows/throughput-real-source.yml@main"):
            with self.subTest(value=value):
                self.assertTrue(check_action_pins.check_text("uses: " + value, "case.yml"))
        for path in (ROOT / ".github/workflows").glob("*.yml"):
            self.assertEqual(check_action_pins.check_text(path.read_text(), path), [])

    def test_workflows_use_lock_preserve_authentication_and_gate_admission(self):
        gpu = (ROOT / ".github/workflows/gpu-toolchains.yml").read_text()
        real = (ROOT / ".github/workflows/throughput-real-source.yml").read_text()
        qualification = (ROOT / ".github/workflows/apt-inputs.yml").read_text()
        self.assertIn('python3 tools/ci_apt.py gpu --out "$RUNNER_TEMP/gpu-apt"', gpu)
        self.assertIn('python3 tools/ci_apt.py readline --out evidence/apt-readline', real)
        for workflow in (gpu, real):
            self.assertNotIn("apt-get install", workflow)
            self.assertNotIn("apt-get update", workflow)
            self.assertIn("runs-on: ubuntu-26.04", workflow)
        self.assertIn("--no-deps --require-hashes", gpu)
        self.assertIn("f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c", gpu)
        self.assertIn("4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae", real)
        self.assertIn("id: native_dependencies", real)
        self.assertIn("id: compiler\n        if: ${{ !cancelled() && steps.native_dependencies.outcome == 'success' }}", real)
        self.assertIn('manifest_file "$lua_dependency" "$PWD/evidence/apt-readline/pinned.identity.json"', real)
        self.assertIn("profile: [gpu, readline]", qualification)
        self.assertIn("python3 tests/ci_apt_test.py -v", qualification)
        self.assertIn('cmp "$RUNNER_TEMP/apt-first/pinned.identity.json" "$RUNNER_TEMP/apt-second/pinned.identity.json"', qualification)
        self.assertIn('cmp "$RUNNER_TEMP/apt-first/selected.identity.json" "$RUNNER_TEMP/apt-second/selected.identity.json"', qualification)
        for path in (".github/apt-inputs.json", "tools/ci_apt.py", "tests/ci_apt_test.py"):
            self.assertIn(path, qualification)


if __name__ == "__main__":
    unittest.main()
