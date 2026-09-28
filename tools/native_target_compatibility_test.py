#!/usr/bin/env python3
"""Regression for the legacy Clang AVX10 -march=native exception (#1501)."""

import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "cmake/NativeTargetCompatibility.cmake"
CMAKELISTS = ROOT / "CMakeLists.txt"
FLAG = "-Wno-error=invalid-feature-combination"
CMAKE = shutil.which("cmake")
HOST_CC = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")

# Simulates host diagnostics around a real compiler. -march=native and the
# Clang-only diagnostic options are consumed so the real compiler never sees them.
WRAPPER = r"""#!/bin/sh
native=0; fatal=0; tolerant=0
for arg; do
    shift
    case "$arg" in
        -march=native) native=1; continue ;;
        -Werror=unknown-warning-option) continue ;;
        -Werror=invalid-feature-combination) fatal=1; continue ;;
        -Wno-error=invalid-feature-combination) tolerant=1; continue ;;
    esac
    set -- "$@" "$arg"
done
case "$BUSTER_TEST_AVX10_MODE" in
    affected)
        if [ "$native" = 1 ]; then
            if [ "$tolerant" = 1 ]; then
                echo "warning: invalid feature combination: +avx10.1-256; will be promoted to avx10.1-512 [-Winvalid-feature-combination]" >&2
            else
                echo "error: invalid feature combination: +avx10.1-256; will be promoted to avx10.1-512 [-Werror,-Winvalid-feature-combination]" >&2
                exit 1
            fi
        fi ;;
    unsupported)
        if [ "$fatal" = 1 ] || [ "$tolerant" = 1 ]; then
            echo "error: unknown warning option [-Werror,-Wunknown-warning-option]" >&2
            exit 1
        fi ;;
    broken)
        if [ "$native" = 1 ]; then
            echo "error: unrelated -march=native failure" >&2
            exit 1
        fi ;;
esac
exec "$BUSTER_TEST_REAL_CC" "$@"
"""


def run_script(body):
    with tempfile.TemporaryDirectory() as work:
        script = Path(work) / "policy.cmake"
        script.write_text(f'include("{MODULE.as_posix()}")\n' + textwrap.dedent(body))
        result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, check=True)
        return result.stderr


@unittest.skipUnless(CMAKE, "cmake is required")
class PolicyTests(unittest.TestCase):
    def candidate(self, compiler_id, is_zig, version, processor, cross):
        output = run_script(f"""
            buster_native_avx10_exception_candidate(out "{compiler_id}" "{is_zig}" "{version}" "{processor}" "{cross}")
            message("RESULT=${{out}}")
        """)
        return output.strip().rsplit("RESULT=", 1)[1]

    def flags(self, candidate, fatal, tolerant):
        output = run_script(f"""
            buster_native_avx10_exception_flags(out "{candidate}" "{fatal}" "{tolerant}")
            message("RESULT=${{out}}")
        """)
        return output.strip().rsplit("RESULT=", 1)[1]

    def test_legacy_native_x86_clang_is_candidate(self):
        for version in ("18.1.3", "20.1.8", "21.1.8", "22.0.0"):
            for processor in ("x86_64", "AMD64", "amd64"):
                self.assertEqual(self.candidate("Clang", "OFF", version, processor, "OFF"), "ON")

    def test_unaffected_configurations_are_not_candidates(self):
        cases = (
            ("Clang", "OFF", "22.1.0", "x86_64", "OFF"),
            ("Clang", "OFF", "23.1.0", "x86_64", "OFF"),
            ("Clang", "OFF", "18.1.3", "aarch64", "OFF"),
            ("Clang", "OFF", "18.1.3", "ARM64", "OFF"),
            ("Clang", "OFF", "18.1.3", "x86_64", "ON"),
            ("Clang", "ON", "18.1.3", "x86_64", "OFF"),
            ("AppleClang", "OFF", "17.0.0", "x86_64", "OFF"),
            ("GNU", "OFF", "13.2.0", "x86_64", "OFF"),
            ("MSVC", "OFF", "19.44", "AMD64", "OFF"),
            ("TinyCC", "OFF", "0.9.28", "x86_64", "OFF"),
        )
        for case in cases:
            with self.subTest(case=case):
                self.assertEqual(self.candidate(*case), "OFF")

    def test_only_the_demonstrated_diagnostic_grants_the_exception(self):
        self.assertEqual(self.flags("ON", "", "1"), FLAG)
        self.assertEqual(self.flags("ON", "1", "1"), "")
        self.assertEqual(self.flags("ON", "", ""), "")
        self.assertEqual(self.flags("OFF", "", "1"), "")

    def test_policy_keeps_the_warning_and_is_independent_of_optional_warnings(self):
        text = CMAKELISTS.read_text()
        self.assertNotIn("-Wno-invalid-feature-combination", text)
        self.assertNotIn("-Wno-invalid-feature-combination", MODULE.read_text())
        call = text.index("buster_native_target_compatibility_flags(")
        optional = text.index("if (C_COMPILER_CLANG_FAMILY AND BUSTER_CHECK_OPTIONAL_WARNINGS)")
        optional_end = text.index("\nendif()\n", optional)
        self.assertFalse(optional < call < optional_end)
        module_code = "\n".join(line for line in MODULE.read_text().splitlines() if not line.lstrip().startswith("#"))
        self.assertNotIn("BUSTER_CHECK_OPTIONAL_WARNINGS", module_code)


@unittest.skipUnless(CMAKE and HOST_CC and os.name == "posix", "cmake, a host C compiler and POSIX sh are required")
class ProbeTests(unittest.TestCase):
    def configure(self, mode, version="18.1.3", processor="x86_64", optional_warnings="OFF"):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            wrapper = work / "cc-wrapper"
            wrapper.write_text(WRAPPER)
            wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
            (work / "CMakeLists.txt").write_text(textwrap.dedent(f"""
                cmake_minimum_required(VERSION 3.17)
                project(native_target_compatibility C)
                option(BUSTER_CHECK_OPTIONAL_WARNINGS "" {optional_warnings})
                include("{MODULE.as_posix()}")
                buster_native_target_compatibility_flags(out Clang OFF "{version}" "{processor}" OFF)
                file(WRITE "${{CMAKE_BINARY_DIR}}/result.txt" "${{out}}")
            """))
            env = dict(os.environ, BUSTER_TEST_AVX10_MODE=mode, BUSTER_TEST_REAL_CC=HOST_CC, CC=str(wrapper))
            env.pop("CFLAGS", None)
            subprocess.run(
                [CMAKE, "-S", str(work), "-B", str(work / "build")],
                env=env, capture_output=True, text=True, check=True,
            )
            return (work / "build/result.txt").read_text()

    def test_affected_host_keeps_warning_nonfatal(self):
        self.assertEqual(self.configure("affected"), FLAG)
        self.assertEqual(self.configure("affected", optional_warnings="ON"), FLAG)

    def test_unaffected_host_gets_no_exception(self):
        self.assertEqual(self.configure("unaffected"), "")

    def test_unsupported_option_gets_no_exception(self):
        self.assertEqual(self.configure("unsupported"), "")

    def test_unrelated_native_failure_stays_fatal(self):
        self.assertEqual(self.configure("broken"), "")

    def test_fixed_compiler_or_other_architecture_is_not_probed(self):
        self.assertEqual(self.configure("affected", version="22.1.0"), "")
        self.assertEqual(self.configure("affected", processor="aarch64"), "")


if __name__ == "__main__":
    unittest.main()
