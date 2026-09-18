#!/usr/bin/env python3
"""Network-free regression for the native CI Release producer profile."""

from pathlib import Path
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/ci.yml"
GENERATE = (
    '"$driver" generate --cc clang --config Release --linker DEFAULT -- '
    '-DBUSTER_DEBUG_INFO=OFF'
)


def step_body(name):
    workflow = WORKFLOW.read_text()
    block = workflow.split("      - name: " + name + "\n", 1)[1]
    return textwrap.dedent(block.split("      - name:", 1)[0])


class NativeProducerProfileTests(unittest.TestCase):
    def test_normal_generation_uses_explicit_profile_c(self):
        block = step_body("Execution-mode matrix")
        self.assertEqual(block.count(GENERATE), 1)
        self.assertNotIn("--ci", block)
        self.assertNotIn("BUSTER_FRAME_POINTERS=OFF", block)

    def test_missing_cache_recovery_uses_the_same_profile(self):
        block = step_body("Native configuration differential matrix")
        recovery = block.split("if [[ ! -f build/CMakeCache.txt ]]; then\n", 1)[1]
        recovery = recovery.split("fi\n", 1)[0]
        self.assertEqual(recovery.count(GENERATE), 1)
        self.assertNotIn("--ci", recovery)
        self.assertNotIn("BUSTER_FRAME_POINTERS=OFF", recovery)


if __name__ == "__main__":
    unittest.main()
