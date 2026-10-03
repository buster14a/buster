#!/usr/bin/env python3
"""Network-free regression tests for the latest-stable LLVM CI installer."""
import hashlib
import io
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_llvm


def asset(name, payload=b"x"):
    return {"name": name, "size": len(payload), "browser_download_url": f"https://example.invalid/{name}",
            "digest": "sha256:" + hashlib.sha256(payload).hexdigest()}


def release(tag, names, prerelease=False, draft=False):
    return {"tag_name": tag, "prerelease": prerelease, "draft": draft, "assets": [asset(name) for name in names]}


class SelectReleaseTests(unittest.TestCase):
    def test_picks_highest_stable_release_by_version_not_listing_order(self):
        releases = [
            release("llvmorg-22.1.9", ["LLVM-22.1.9-Linux-X64.tar.zst"]),
            release("llvmorg-24.1.0-rc1", ["LLVM-24.1.0-rc1-Linux-X64.tar.zst"], prerelease=True),
            release("llvmorg-23.1.2", ["LLVM-23.1.2-Linux-X64.tar.zst"]),
            release("llvmorg-23.1.3", ["LLVM-23.1.3-Linux-X64.tar.zst"], draft=True),
        ]
        version, chosen = ci_llvm.select_release(releases, "x86_64-linux", ("tar.zst", "tar.xz"))
        self.assertEqual(version, (23, 1, 2))
        self.assertEqual(chosen["name"], "LLVM-23.1.2-Linux-X64.tar.zst")

    def test_skips_newest_release_until_it_publishes_this_target(self):
        releases = [
            release("llvmorg-23.1.3", ["LLVM-23.1.3-Linux-X64.tar.zst"]),
            release("llvmorg-23.1.2", ["clang+llvm-23.1.2-aarch64-pc-windows-msvc.tar.zst"]),
        ]
        version, _ = ci_llvm.select_release(releases, "aarch64-windows", ("tar.zst",))
        self.assertEqual(version, (23, 1, 2))

    def test_falls_back_to_xz_without_zstd(self):
        names = ["LLVM-23.1.2-Linux-ARM64.tar.zst", "LLVM-23.1.2-Linux-ARM64.tar.xz"]
        _, chosen = ci_llvm.select_release([release("llvmorg-23.1.2", names)], "aarch64-linux",
                                           ci_llvm.archive_formats(False))
        self.assertEqual(chosen["name"], "LLVM-23.1.2-Linux-ARM64.tar.xz")

    def test_rejects_releases_before_avx10_fix(self):
        releases = [release("llvmorg-21.1.8", ["LLVM-21.1.8-Linux-X64.tar.zst"])]
        with self.assertRaisesRegex(ValueError, "AVX10"):
            ci_llvm.select_release(releases, "x86_64-linux", ("tar.zst",))

    def test_pin_selects_exact_release(self):
        releases = [release(f"llvmorg-{v}", [f"LLVM-{v}-Linux-X64.tar.zst"]) for v in ("23.1.2", "22.1.0")]
        version, _ = ci_llvm.select_release(releases, "x86_64-linux", ("tar.zst",), pinned=(22, 1, 0))
        self.assertEqual(version, (22, 1, 0))

    def test_requires_published_digest(self):
        bad = release("llvmorg-23.1.2", ["LLVM-23.1.2-Linux-X64.tar.zst"])
        bad["assets"][0]["digest"] = None
        with self.assertRaisesRegex(ValueError, "digest"):
            ci_llvm.select_release([bad], "x86_64-linux", ("tar.zst",))

    def test_macos_is_not_an_upstream_target(self):
        with self.assertRaisesRegex(ValueError, "no upstream"):
            ci_llvm.select_release([], "aarch64-macos", ("tar.zst",))


class MemberFilterTests(unittest.TestCase):
    def test_keeps_compiler_linker_archiver_and_resource_tree(self):
        for name in ("LLVM-23/bin/clang", "LLVM-23/bin/clang-23", "LLVM-23/bin/ld.lld", "LLVM-23/bin/llvm-ar",
                     "x/bin/clang.exe", "x/bin/lld-link.exe", "x/bin/libxml2.dll",
                     "LLVM-23/lib/clang/23/include/stddef.h"):
            self.assertIsNotNone(ci_llvm.kept_member(name), name)

    def test_drops_everything_else(self):
        for name in ("LLVM-23/bin/flang-23", "LLVM-23/bin/mlir-opt", "LLVM-23/bin/clang-tidy",
                     "LLVM-23/lib/libLLVMCore.a", "LLVM-23/include/llvm/IR/Module.h",
                     "LLVM-23/../bin/clang", "/abs/bin/clang", "LLVM-23/bin/sub/clang"):
            self.assertIsNone(ci_llvm.kept_member(name), name)

    def test_extract_writes_only_kept_members_and_links(self):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:xz") as archive:
            for name, data in (("top/bin/clang-23", b"#!clang"), ("top/bin/mlir-opt", b"big"),
                               ("top/lib/clang/23/include/stddef.h", b"h")):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o755
                archive.addfile(info, io.BytesIO(data))
            link = tarfile.TarInfo("top/bin/clang")
            link.type = tarfile.SYMTYPE
            link.linkname = "clang-23"
            archive.addfile(link)
            escape = tarfile.TarInfo("top/bin/clang-cl")
            escape.type = tarfile.SYMTYPE
            escape.linkname = "../../../etc/passwd"
            archive.addfile(escape)
        buffer.seek(0)
        with tempfile.TemporaryDirectory() as temporary:
            install = Path(temporary) / "llvm"
            ci_llvm.extract(buffer, install, "r|xz")
            files = sorted(str(path.relative_to(install)).replace("\\", "/")
                           for path in install.rglob("*") if not path.is_dir())
            self.assertEqual(files, ["bin/clang", "bin/clang-23", "lib/clang/23/include/stddef.h"])
            self.assertEqual((install / "bin/clang").read_bytes(), b"#!clang")


class DownloadTests(unittest.TestCase):
    def fake_urlopen(self, payload):
        class Response(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False
        return mock.patch.object(ci_llvm.urllib.request, "urlopen", return_value=Response(payload))

    def test_accepts_matching_digest(self):
        with tempfile.TemporaryDirectory() as temporary, self.fake_urlopen(b"payload"):
            destination = Path(temporary) / "a"
            ci_llvm.download(asset("a", b"payload"), destination, None)
            self.assertEqual(destination.read_bytes(), b"payload")

    def test_rejects_digest_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary, self.fake_urlopen(b"tampered"):
            expected = asset("a", b"payload")
            expected["size"] = len(b"tampered")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_llvm.download(expected, Path(temporary) / "a", None)

    def test_rejects_oversized_archive(self):
        with tempfile.TemporaryDirectory() as temporary, self.fake_urlopen(b"payload-too-long"):
            with self.assertRaisesRegex(ValueError, "exceeds"):
                ci_llvm.download(asset("a", b"payload"), Path(temporary) / "a", None)


class FetchReleasesTests(unittest.TestCase):
    def response(self, payload):
        class Response(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False
        return Response(payload)

    def test_retries_a_timed_out_listing(self):
        outcomes = [TimeoutError("The read operation timed out"), self.response(b'[{"tag_name": "llvmorg-22.1.0"}]')]
        with mock.patch.object(ci_llvm.urllib.request, "urlopen", side_effect=outcomes) as urlopen, \
                mock.patch.object(ci_llvm.time, "sleep") as sleep:
            self.assertEqual(ci_llvm.fetch_releases(None), [{"tag_name": "llvmorg-22.1.0"}])
        self.assertEqual(urlopen.call_count, 2)
        sleep.assert_called_once_with(5)

    def test_gives_up_after_bounded_attempts(self):
        with mock.patch.object(ci_llvm.urllib.request, "urlopen", side_effect=TimeoutError("stalled")) as urlopen, \
                mock.patch.object(ci_llvm.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "could not list LLVM releases: stalled"):
                ci_llvm.fetch_releases(None)
        self.assertEqual(urlopen.call_count, ci_llvm.DOWNLOAD_ATTEMPTS)


class RuntimeTests(unittest.TestCase):
    def test_verifies_package_before_extraction_and_preserves_soname_links(self):
        for target, triplet in (("x86_64-linux", "x86_64-linux-gnu"),
                                ("aarch64-linux", "aarch64-linux-gnu")):
            with tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                install = work / "install"
                events = []

                def download(chosen, package, token):
                    self.assertEqual(chosen, ci_llvm.ICU_RUNTIME_ASSETS[target])
                    events.append("verified")
                    package.write_bytes(b"verified package")

                def extract(command, **kwargs):
                    self.assertEqual(events, ["verified"])
                    self.assertEqual(command[:2], ["dpkg-deb", "--extract"])
                    staging = Path(command[-1])
                    libraries = staging / "usr/lib" / triplet
                    libraries.mkdir(parents=True)
                    for name in ("libicui18n", "libicuuc", "libicudata"):
                        (libraries / (name + ".so.70.1")).write_bytes(b"runtime")
                        try:
                            (libraries / (name + ".so.70")).symlink_to(name + ".so.70.1")
                        except OSError:
                            # Windows hosts may lack symlink privilege; staging is Linux-only.
                            (libraries / (name + ".so.70")).write_bytes(b"runtime")
                    notices = staging / "usr/share/doc/libicu70"
                    notices.mkdir(parents=True)
                    (notices / "copyright").write_text("upstream notices")
                    events.append("extracted")

                with mock.patch.object(ci_llvm, "download", side_effect=download), \
                        mock.patch.object(ci_llvm.subprocess, "run", side_effect=extract):
                    ci_llvm.provision_linux_runtime(target, work, install, None)
                self.assertEqual(events, ["verified", "extracted"])
                if sys.platform != "win32":
                    self.assertTrue((install / "lib/libicui18n.so.70").is_symlink())
                self.assertEqual((install / "lib/libicui18n.so.70").read_bytes(), b"runtime")
                self.assertEqual((install / "share/licenses/icu70/copyright").read_text(), "upstream notices")
                self.assertFalse((work / "icu-runtime").exists())

    def test_bad_runtime_digest_never_reaches_extractor(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(ci_llvm, "download", side_effect=ValueError("checksum mismatch")), \
                mock.patch.object(ci_llvm.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_llvm.provision_linux_runtime("x86_64-linux", Path(temporary), Path(temporary) / "install", None)
            run.assert_not_called()

    def test_windows_does_not_download_linux_runtime(self):
        with mock.patch.object(ci_llvm, "download") as download:
            ci_llvm.provision_linux_runtime("aarch64-windows", Path("/unused"), Path("/unused"), None)
        download.assert_not_called()


class ReadinessTests(unittest.TestCase):
    def test_requires_native_linker_and_archiver_on_both_platform_families(self):
        for target, suffix, linker in (("x86_64-linux", "", "ld.lld"),
                                       ("aarch64-linux", "", "ld.lld"),
                                       ("x86_64-windows", ".exe", "lld-link"),
                                       ("aarch64-windows", ".exe", "lld-link")):
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                with mock.patch.object(ci_llvm.subprocess, "run", return_value=
                                       subprocess.CompletedProcess([], 0, "clang version 23.1.2\n", "")) as run:
                    ci_llvm.validate_tools(target, directory, (23, 1, 2))
                self.assertEqual([call.args[0][0] for call in run.call_args_list],
                                 [str(directory / (tool + suffix)) for tool in ("clang", "llvm-ar", linker)])
                self.assertTrue(all(call.kwargs["timeout"] == 30 for call in run.call_args_list))

    def test_healthy_clang_cannot_hide_missing_linker_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            linker = str(Path(temporary) / "ld.lld")
            outcomes = [subprocess.CompletedProcess([], 0, "clang version 23.1.2\n", ""),
                        subprocess.CompletedProcess([], 0, "LLVM version 23.1.2\n", ""),
                        subprocess.CalledProcessError(127, [linker, "--version"],
                                                      stderr="libicui18n.so.70: cannot open shared object file")]
            with mock.patch.object(ci_llvm.subprocess, "run", side_effect=outcomes):
                with self.assertRaisesRegex(RuntimeError, "ld.lld.*libicui18n.so.70"):
                    ci_llvm.validate_tools("x86_64-linux", Path(temporary), (23, 1, 2))

    def test_missing_executable_timeout_and_wrong_clang_version_fail(self):
        for outcome in (FileNotFoundError("missing tool"), subprocess.TimeoutExpired("clang", 30),
                        subprocess.CompletedProcess([], 0, "clang version 22.1.0\n", "")):
            with mock.patch.object(ci_llvm.subprocess, "run", side_effect=
                                   [outcome] if isinstance(outcome, BaseException) else None,
                                   return_value=outcome):
                with self.assertRaises(RuntimeError):
                    ci_llvm.validate_tools("x86_64-linux", Path("/unused/bin"), (23, 1, 2))

    def test_installed_extra_linker_frontends_are_probed(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for tool in ("ld64.lld", "lld-link", "wasm-ld"):
                (directory / tool).touch()
            with mock.patch.object(ci_llvm.subprocess, "run", return_value=
                                   subprocess.CompletedProcess([], 0, "clang version 23.1.2\n", "")) as run:
                ci_llvm.validate_tools("x86_64-linux", directory, (23, 1, 2))
            self.assertEqual(len(run.call_args_list), 6)

    def test_failed_readiness_cannot_publish_github_path_or_environment(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            path_file, env_file = directory / "path", directory / "env"
            with mock.patch.object(ci_llvm, "fetch_releases", return_value=[]), \
                    mock.patch.object(ci_llvm, "install_llvm", side_effect=RuntimeError("linker loader failure")):
                with self.assertRaisesRegex(RuntimeError, "linker loader failure"):
                    ci_llvm.main(["--target", "x86_64-linux", "--work-directory", str(directory),
                                  "--github-path", str(path_file), "--github-env", str(env_file)])
            self.assertFalse(path_file.exists())
            self.assertFalse(env_file.exists())


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

    def test_native_and_matrix_jobs_install_latest_llvm_outside_macos(self):
        steps = re.findall(r"- name: Install latest stable LLVM\n        id: llvm\n        if: \$\{\{ (.*?) \}\}", self.workflow)
        self.assertEqual(len(steps), 2)
        for condition in steps:
            self.assertIn("matrix.os != 'macos'", condition)
        self.assertEqual(self.workflow.count("tools/ci_llvm.py --target \"$LLVM_TARGET\""), 2)

    def test_compiler_steps_require_installed_llvm(self):
        for step_id in ("tools_unix", "combinations_windows", "modes", "modes_windows", "differential"):
            match = re.search(rf"        id: {step_id}\n(?:        [^\n]*\n)*?        if: \$\{{\{{ (.*?) \}}\}}", self.workflow)
            self.assertIsNotNone(match, step_id)
            self.assertIn("steps.llvm.outcome == 'success'", match.group(1), step_id)

    def test_windows_prefers_installed_llvm_over_image_llvm(self):
        self.assertNotIn(r"$env:ProgramFiles\LLVM\bin", self.workflow)
        self.assertEqual(self.workflow.count("-LlvmBin $env:BUSTER_CI_LLVM_BIN"), 2)
        helper = (ROOT / "tools/ci_vs_dev_shell.ps1").read_text(encoding="utf-8")
        launch = helper.index("Launch-VsDevShell.ps1') -Arch")
        self.assertGreater(helper.index('$env:PATH = "$LlvmBin;$env:PATH"'), launch)


if __name__ == "__main__":
    unittest.main()
