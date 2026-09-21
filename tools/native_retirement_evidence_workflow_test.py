#!/usr/bin/env python3
"""Ordering/authority regression for the temporary census binding workflow.

Kept outside tests/: that directory is itself the immutable #508 input corpus.
Rebinding and materializer behavior have their existing dedicated native and
Python suites; this test covers which revision executes them and when.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EvidenceWorkflowTests(unittest.TestCase):
    def test_trusted_classification_precedes_reserved_directory_creation(self):
        text = (ROOT / ".github/workflows/native-retirement-evidence.yml").read_text()
        census = text.split("\n  strict_differential:", 1)[0]
        self.assertIn("TRUSTED_REF: ${{ github.event.pull_request.base.sha || github.sha }}", census)
        self.assertIn("CANDIDATE_HEAD: ${{ github.event.pull_request.head.sha || github.sha }}", census)
        self.assertIn("ref: ${{ env.TRUSTED_REF }}\n          path: trusted", census)
        self.assertLess(census.index("trusted/tools/native_retirement_integration.py classify"),
                        census.index("path: candidate/external/"))
        self.assertIn('--base "$TRUSTED_REF" --head "$CANDIDATE_HEAD"', census)

    def test_one_temporary_authority_precedes_all_consumers(self):
        text = (ROOT / ".github/workflows/native-retirement-evidence.yml").read_text()
        bootstrap = text.split("      - name: Bootstrap candidate build driver", 1)[1].split(
            "      - name: Build candidate with hosted Clang exception", 1)[0]
        ordered = ["../trusted/tools/native_retirement_external.py prepare",
                   "../trusted/tools/native_retirement_rebind.py refresh",
                   "../trusted/tools/native_retirement_rebind.py check",
                   "cp docs/native-retirement-repository-sources-v1.json ../validation/docs/",
                   "cp tools/native_retirement_dependency_binding.generated.h ../validation/tools/",
                   "tools/native_retirement_materializer.py materialize",
                   'build.c -o "$RUNNER_TEMP/candidate-build"']
        positions = [bootstrap.index(command) for command in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("set -euo pipefail", bootstrap)
        self.assertIn('test -z "$unexpected"', bootstrap)
        self.assertNotIn("python3 tools/native_retirement_rebind.py refresh", bootstrap)
        self.assertNotIn("git add", bootstrap)
        self.assertNotIn("git commit", bootstrap)
        self.assertIn("tee evidence/rebinding/refresh.json", bootstrap)
        self.assertIn("tee evidence/rebinding/check.json", bootstrap)
        self.assertIn("if [[ '${{ github.event_name }}' == pull_request ]]; then\n"
                      "            python3 ../trusted/tools/native_retirement_rebind.py refresh", bootstrap)


if __name__ == "__main__":
    unittest.main(verbosity=2)
