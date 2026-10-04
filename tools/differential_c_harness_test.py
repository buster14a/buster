#!/usr/bin/env python3
"""Offline native-mode and oracle controls for the Python differential tools."""

import argparse
import os
import tempfile
import unittest

import differential_c_harness as harness
import reduce_differential_case as reducer


class RecordingChecker(reducer.Checker):
    def __init__(self, work_directory, behaviors=None):
        super().__init__("/subject/ide", work_directory)
        self.commands = []
        self.behaviors = behaviors or {}

    def compile_one(self, command, source_path, binary_path):
        self.commands.append((os.path.basename(binary_path).removeprefix("candidate."), command))
        return 0, ""

    def run_one(self, binary_path):
        label = os.path.basename(binary_path).removeprefix("candidate.")
        return self.behaviors.get(label, (0, b"reference\n"))


class DifferentialNativeModeTests(unittest.TestCase):
    def test_harness_commands_cover_both_explicit_native_allocators(self):
        arguments = argparse.Namespace(ide="/subject/ide", cc="clang")
        self.assertEqual(harness.modes(arguments), [
            ("clang-O0", ["clang", "-O0", "-w"]),
            ("clang-O2", ["clang", "-O2", "-w"]),
            ("ide-fast", ["/subject/ide", "cc", "-fregister-allocator=fast"]),
            ("ide-quality", ["/subject/ide", "cc", "-fregister-allocator=quality"]),
        ])

    def test_harness_attributes_quality_divergence_and_preserves_oracle_control(self):
        observations = [harness.Observation(label, True, "", 0, run_returncode=0, run_stdout=b"reference\n")
                        for label in ("clang-O0", "clang-O2", "ide-fast", "ide-quality")]
        observations[3].run_stdout = b"quality divergence\n"
        result = harness.classify("enum_sizeof", 1, observations)
        self.assertEqual((result.category, result.detail), ("behavior", "ide-quality: exit/stdout differ"))
        observations[1].run_stdout = b"invalid generator\n"
        self.assertEqual(harness.classify("enum_sizeof", 1, observations).category, "generator")

    def test_reducer_observes_both_allocators_without_retired_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            checker = RecordingChecker(directory)
            self.assertEqual(checker.observe("int main(void) { return 0; }\n"), ("ok", ""))
            self.assertEqual(checker.commands, [
                ("clang-O0", ["clang", "-O0", "-w"]),
                ("ide-fast", ["/subject/ide", "cc", "-fregister-allocator=fast"]),
                ("ide-quality", ["/subject/ide", "cc", "-fregister-allocator=quality"]),
            ])
            with self.assertRaises(KeyError):
                checker.observe("int main(void) { return 0; }\n", ide_modes=("ide-canon",))

    def test_reducer_pins_quality_and_requires_clang_agreement(self):
        with tempfile.TemporaryDirectory() as directory:
            checker = RecordingChecker(directory, {"ide-quality": (1, b"wrong\n")})
            observed = checker.observe("int main(void) { return 0; }\n", ide_modes=("ide-quality",))
            self.assertEqual(observed, ("behavior", "ide-quality differs"))
            self.assertEqual([label for label, _command in checker.commands], ["clang-O0", "ide-quality", "clang-O2"])
            checker.behaviors["clang-O2"] = (0, b"invalid generator\n")
            self.assertEqual(checker.observe("int main(void) { return 0; }\n", ide_modes=("ide-quality",)),
                             ("invalid", "clang modes disagree"))


if __name__ == "__main__":
    unittest.main()
