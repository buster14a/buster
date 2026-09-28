#!/usr/bin/env python3
"""Network-free regression tests for the latest-stable LLVM CI installer."""
import hashlib
import io
from pathlib import Path
import re
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
        self.assertEqual(self.workflow.count('$env:PATH = "$env:BUSTER_CI_LLVM_BIN;$env:PATH"'), 2)


if __name__ == "__main__":
    unittest.main()
