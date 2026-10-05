#!/usr/bin/env python3
"""Fail-closed regressions for aarch64_assembler_census.py (#2673).

Run: python3 tools/aarch64_assembler_census_test.py -v
Uses stub executables only; no LLVM or Buster build is needed.
"""
from __future__ import annotations

import io
import contextlib
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aarch64_assembler_census as census  # noqa: E402


def stub(directory: Path, name: str, body: str) -> str:
    path = directory / name
    path.write_text("#!/bin/sh\n" + body + "\n")
    path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return str(path)


class CensusTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.fixtures = self.root / "fixtures"
        self.fixtures.mkdir()
        # The "compiler" writes one constant instruction line as its listing.
        self.clang = stub(self.root, "clang", 'out=""; while [ $# -gt 0 ]; do if [ "$1" = -o ]; then out="$2"; fi; shift; done; printf "\\tadd\\tx0, x1, #1\\n" > "$out"')
        self.assembler = stub(self.root, "assembler", 'out=""; while [ $# -gt 0 ]; do if [ "$1" = -o ]; then out="$2"; fi; shift; done; : > "$out"')
        self.objdump = stub(self.root, "objdump", 'printf "   0:\\tadd x0, x1, #0x1\\n"')
        self.failing = stub(self.root, "failing", "exit 1")

    def tearDown(self):
        self.temporary.cleanup()

    def run_census(self, *extra):
        arguments = ["--ide", self.assembler, "--clang", self.clang, "--llvm-mc", self.assembler,
                     "--llvm-objdump", self.objdump, "--fixtures", str(self.fixtures), "--jobs", "1", *extra]
        with contextlib.redirect_stdout(io.StringIO()) as output:
            status = census.main(arguments)
        return status, output.getvalue()

    def test_empty_fixture_directory_fails(self):
        status, output = self.run_census()
        self.assertEqual(status, 1)
        self.assertIn("empty corpus", output)
        self.assertIn("no fixture compiled", output)

    def test_matching_observation_passes(self):
        (self.fixtures / "a.c").write_text("int f(void) { return 1; }\n")
        status, output = self.run_census()
        self.assertEqual(status, 0, output)
        self.assertIn("same: 1", output)

    def test_observer_failure_is_not_a_match(self):
        (self.fixtures / "a.c").write_text("int f(void) { return 1; }\n")
        status, output = self.run_census("--llvm-objdump", self.failing)
        self.assertEqual(status, 1)
        self.assertIn("observer-failed: 1", output)
        self.assertIn("no line was compared", output)

    def test_failed_compilation_is_reported(self):
        (self.fixtures / "a.c").write_text("int f(void) { return 1; }\n")
        status, output = self.run_census("--clang", self.failing)
        self.assertEqual(status, 1)
        self.assertIn("fixture did not compile", output)

    def test_allowlist_matches_only_documented_operand_shape(self):
        self.assertTrue(census.documented_refusal("fmla\tv0.4s, v20.4s, v21.s[0]"))
        self.assertTrue(census.documented_refusal("fmls v0.2d, v1.2d, v2.d[1]"))
        self.assertFalse(census.documented_refusal("fmla v0.4s, v1.4s, v2.4s"))
        self.assertFalse(census.documented_refusal("fmla s0, s1, v2.s[0]"))
        self.assertFalse(census.documented_refusal("stp x29, x30, [sp, #-16]!"))


if __name__ == "__main__":
    os.environ.setdefault("LC_ALL", "C")
    unittest.main()
