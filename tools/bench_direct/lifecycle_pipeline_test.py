#!/usr/bin/env python3
"""Execute lifecycle CI's producer pipeline; a failed fixture must fail CI."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LifecyclePipelineTests(unittest.TestCase):
    def recipe(self):
        workflow = (ROOT / ".github/workflows/bench-service-policy.yml").read_text(encoding="utf-8")
        match = re.search(r"(?ms)^      - name: Prove the direct 9700X workflow gate\n(.*?)(?=^      - |\Z)", workflow)
        self.assertIsNotNone(match)
        step = match.group(1)
        self.assertIn("        shell: bash\n", step)
        script = step.split("        run: |\n", 1)[1]
        lines = [line[10:] for line in script.splitlines() if line.startswith("          ")]
        producer = next(i for i, line in enumerate(lines) if '"$RUNNER_TEMP/9700x-lifecycle" --self-test | tee ' in line)
        # Keep the actual settings and pipeline. Compilation is outside this
        # regression; the temporary executable below injects a fixture failure.
        prefix = [line for line in lines[:producer] if not line.startswith("clang ")]
        self.assertIn("set -o pipefail", prefix)
        return prefix + [lines[producer], 'printf continued > "$RUNNER_TEMP/continued"']

    def execute(self, recipe, explicit_bash):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "9700x-lifecycle"
            binary.write_text("#!/usr/bin/env bash\nprintf 'synthetic fixture failure\\n'\nexit 2\n", encoding="utf-8")
            binary.chmod(0o700)
            script = root / "step.sh"
            script.write_text("\n".join(recipe) + "\n", encoding="utf-8")
            # GitHub's explicit shell:bash enables pipefail; its implicit Bash
            # default is -e only. Exercise both against the same producer pipe.
            command = ["bash", "--noprofile", "--norc", "-e"]
            if explicit_bash:
                command += ["-o", "pipefail"]
            result = subprocess.run(command + [str(script)],
                                    env=dict(os.environ, RUNNER_TEMP=temporary),
                                    text=True, capture_output=True, check=False)
            evidence = (root / "9700x-lifecycle-fixtures.jsonl").read_text(encoding="utf-8")
            return result.returncode, (root / "continued").exists(), evidence

    def test_failed_native_producer_stops_following_validators(self):
        status, continued, evidence = self.execute(self.recipe(), True)
        self.assertEqual(status, 2)
        self.assertFalse(continued)
        self.assertEqual(evidence, "synthetic fixture failure\n")

    def test_legacy_implicit_shell_masked_producer_failure(self):
        legacy = [line for line in self.recipe() if line != "set -o pipefail"]
        status, continued, evidence = self.execute(legacy, False)
        self.assertEqual(status, 0)
        self.assertTrue(continued)
        self.assertEqual(evidence, "synthetic fixture failure\n")


if __name__ == "__main__":
    unittest.main()
