#!/usr/bin/env python3
"""Full Linux Clang/GCC shared ASan runtime configure regression (#1651, #1654)."""

import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CMAKE = shutil.which("cmake")
CLANG = shutil.which("clang")
GCC = shutil.which("gcc")

WRAPPER = r"""#!/bin/sh
for arg; do
    case "$arg" in
        -print-file-name=libclang_rt.asan-*.so|-print-file-name=libclang_rt.asan.so)
            printf '%s\n' "$arg" >> "$BUSTER_TEST_QUERY_LOG"
            if [ "$BUSTER_TEST_CLANG_RUNTIME" = missing ]; then
                printf '%s\n' "${arg#*=}"
            else
                printf '%s\n' "$BUSTER_TEST_CLANG_RUNTIME"
            fi
            exit 0
            ;;
        -print-file-name=libasan.so)
            printf '%s\n' "$arg" >> "$BUSTER_TEST_QUERY_LOG"
            printf '%s\n' "$BUSTER_TEST_GCC_RUNTIME"
            exit 0
            ;;
    esac
done
exec "$BUSTER_TEST_REAL_CC" "$@"
"""


@unittest.skipUnless(CMAKE and sys.platform.startswith("linux"), "cmake and Linux are required")
class LinuxAsanFullConfigureTests(unittest.TestCase):
    def configure(self, compiler, clang_runtime):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            wrapper = work / (Path(compiler).name + "-wrapper")
            wrapper.write_text(WRAPPER, encoding="utf-8")
            wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)

            clang_runtime_path = work / "libclang_rt.asan-x86_64.so"
            gcc_runtime_path = work / "libasan.so"
            clang_runtime_path.touch()
            gcc_runtime_path.touch()
            query_log = work / "queries.log"

            env = dict(os.environ)
            for name in ("CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "CMAKE_GENERATOR"):
                env.pop(name, None)
            env.update(
                BUSTER_TEST_REAL_CC=compiler,
                BUSTER_TEST_CLANG_RUNTIME=(
                    "missing" if clang_runtime == "missing" else str(clang_runtime_path.resolve())
                ),
                BUSTER_TEST_GCC_RUNTIME=str(gcc_runtime_path.resolve()),
                BUSTER_TEST_QUERY_LOG=str(query_log),
            )
            command = [
                CMAKE,
                "-S", str(ROOT),
                "-B", str(work / "build"),
                f"-DCMAKE_C_COMPILER={wrapper}",
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF",
                "-DBUSTER_SANITIZE=ON",
                "-DBUSTER_INCLUDE_TESTS=OFF",
                "-DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF",
                "-DBUSTER_DEVELOPER_TARGETS=OFF",
                "-DBUSTER_REQUIRE_VULKAN_SDK=OFF",
                "-DBUSTER_DEBUG_INFO=OFF",
                "-DBUSTER_FRAME_POINTERS=OFF",
            ]
            result = subprocess.run(
                command,
                cwd=ROOT,
                env=env,
                capture_output=True,
                text=True,
                timeout=180,
            )
            queries = query_log.read_text(encoding="utf-8").splitlines() if query_log.exists() else []
            return (
                result,
                queries,
                str(clang_runtime_path.resolve()),
                str(gcc_runtime_path.resolve()),
            )

    @unittest.skipUnless(CLANG, "clang is required")
    def test_clang_selects_its_shared_runtime_without_gcc_fallback(self):
        result, queries, clang_runtime, _ = self.configure(CLANG, "present")
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn(f"BUSTER_LINUX_ASAN_PRELOAD_RUNTIME: {clang_runtime}", output)
        self.assertTrue(any("libclang_rt.asan" in query for query in queries), queries)
        self.assertFalse(any(query.endswith("=libasan.so") for query in queries), queries)

    @unittest.skipUnless(CLANG, "clang is required")
    def test_missing_clang_runtime_fails_before_gcc_libasan_fallback(self):
        result, queries, _, gcc_runtime = self.configure(CLANG, "missing")
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn("Could not find Clang's Linux AddressSanitizer runtime", output)
        self.assertRegex(output, r"libclang-rt-[0-9]+-dev")
        self.assertNotIn("BUSTER_LINUX_ASAN_PRELOAD_RUNTIME: not found", output)
        self.assertNotIn(gcc_runtime, output)
        self.assertTrue(any("libclang_rt.asan" in query for query in queries), queries)
        self.assertFalse(any(query.endswith("=libasan.so") for query in queries), queries)

    @unittest.skipUnless(GCC, "gcc is required")
    def test_gcc_libasan_discovery_is_unchanged(self):
        result, queries, _, gcc_runtime = self.configure(GCC, "missing")
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn(f"BUSTER_LINUX_ASAN_PRELOAD_RUNTIME: {gcc_runtime}", output)
        self.assertTrue(any(query.endswith("=libasan.so") for query in queries), queries)
        self.assertFalse(any("libclang_rt.asan" in query for query in queries), queries)


if __name__ == "__main__":
    unittest.main()
