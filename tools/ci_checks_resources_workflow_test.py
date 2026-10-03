#!/usr/bin/env python3
"""Guard the opt-in observer's boundary around unchanged CI payloads."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_summary_core
REFS = (
    "refs/heads/codex/2120-evidence-v2-combined-overlap",
    "refs/heads/codex/2120-evidence-v2-combined-all-builds",
    "refs/heads/codex/2120-evidence-v2-split-overlap",
)


def step(workflow, name):
    return workflow.split("      - name: " + name + "\n", 1)[1].split("\n      - name: ", 1)[0]


def script(block):
    lines = block.split("        run: |\n", 1)[1].splitlines()
    result = []
    for line in lines:
        if line and not line.startswith("          "):
            break
        result.append(line)
    return textwrap.dedent("\n".join(result)).rstrip() + "\n"


class ResourceWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        cls.desktop = cls.workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]

    def test_existing_payload_scripts_are_byte_identical(self):
        pins = {
            "Combination matrix (Linux, macOS)": "fe8bc88b204bd19f1b94dfe7fad5697e37d39867143bc505ff5c26850658fe34",
            "Combination matrix (Windows)": "982b6445a766bf35663e7428f6c6a949b4979ff6c35c78236fbe9e8c6397ee30",
        }
        for name, expected in pins.items():
            with self.subTest(name=name):
                self.assertEqual(hashlib.sha256(script(step(self.desktop, name)).encode()).hexdigest(), expected)

    def test_separate_lifecycle_finishes_before_sanitize_or_upload(self):
        names = ("Start checks resource observation", "Combination matrix (Linux, macOS)",
                 "Combination matrix (Windows)", "Stop checks resource observation",
                 "Collect CMake configure evidence", "Sanitize desktop logs", "Retain desktop logs")
        positions = [self.desktop.index("      - name: " + name + "\n") for name in names]
        self.assertEqual(positions, sorted(positions))
        for name in (names[0], names[3]):
            block = step(self.desktop, name)
            self.assertIn("        timeout-minutes: 2\n", block)
            self.assertNotIn("continue-on-error:", block)
            self.assertNotIn("|| true", script(block))

    def test_selector_has_only_prospective_dispatch_names(self):
        expression = re.search(r"^      BUSTER_CI_CHECKS_RESOURCES: (.+)$", self.desktop, re.M).group(1)
        expected = "${{ github.event_name == 'workflow_dispatch' && (" + " || ".join(
            "github.ref == '" + ref + "'" for ref in REFS) + ") && '1' || '0' }}"
        self.assertEqual(expression, expected)
        # GitHub expression comparisons ignore case. The source helper separately
        # refuses wrong-case identities; these names never enable ordinary refs.
        selected = lambda event, ref: event.casefold() == "workflow_dispatch" and ref.casefold() in {r.casefold() for r in REFS}
        for ref in REFS:
            self.assertTrue(selected("workflow_dispatch", ref))
            for event in ("pull_request", "merge_group", "push"):
                self.assertFalse(selected(event, ref))
            self.assertFalse(selected("workflow_dispatch", ref + "-lookalike"))
        for ref in ("refs/heads/main", "refs/tags/v1", "refs/heads/codex/ci-checks-combined-overlap",
                    "refs/heads/codex/ci-checks-combined-all-builds", "refs/heads/codex/ci-checks-split-overlap"):
            self.assertFalse(selected("workflow_dispatch", ref))

    def test_payload_remains_independent_of_readiness_failure(self):
        for name in ("Combination matrix (Linux, macOS)", "Combination matrix (Windows)"):
            predicate = re.search(r"^        if: (.+)$", step(self.desktop, name), re.M).group(1)
            self.assertIn("!cancelled()", predicate)
            self.assertNotIn("checks_resources", predicate)
            self.assertNotIn("success()", predicate)
        start = step(self.desktop, "Start checks resource observation")
        stop = step(self.desktop, "Stop checks resource observation")
        self.assertIn("!cancelled()", start)
        self.assertIn("steps.checkout.outcome == 'success'", start)
        self.assertIn("always()", stop)
        self.assertNotIn("checks_resources_start.outcome", stop)

    def test_source_binding_and_interpreter_survive_vs_path_export(self):
        start = script(step(self.desktop, "Start checks resource observation"))
        stop = script(step(self.desktop, "Stop checks resource observation"))
        for pin in ("--source-revision \"$(git rev-parse HEAD)\"", "--source-tree \"$(git rev-parse 'HEAD^{tree}')\"",
                    "--workflow-blob \"$(git rev-parse 'HEAD:.github/workflows/ci.yml')\""):
            self.assertIn(pin, start)
        self.assertIn("pathlib.Path(sys.executable).resolve().as_posix()", start)
        self.assertIn('>> "$GITHUB_ENV"', start)
        self.assertIn('"$BUSTER_CI_RESOURCES_PYTHON" tools/ci_checks_resources.py start', start)
        self.assertIn('"${BUSTER_CI_RESOURCES_PYTHON:-$BUSTER_CI_PYTHON}" tools/ci_checks_resources.py stop', stop)
        self.assertIn('--role "${{ matrix.lane }}" --invocation "${{ matrix.shard }}"', start)
        session = '--session "$RUNNER_TEMP/buster-ci/runner-resources"'
        self.assertIn(session, start)
        self.assertIn(session, stop)

    def test_observer_is_required_only_when_opted_in(self):
        summary = step(self.desktop, "Desktop result and reproduction")
        required = re.search(r"^          BUSTER_CI_REQUIRED: (.+)$", summary, re.M).group(1)
        self.assertTrue(required.startswith("${{ format('{0}{1}', "))
        self.assertTrue(required.endswith(", env.BUSTER_CI_CHECKS_RESOURCES == '1' && ' checks_resources_start checks_resources_stop' || '') }}"))
        for suffix in ("combinations_unix", "combinations_windows"):
            self.assertIn("zig_cache_policy workflow_tools bootstrap_wrappers zig_cache_gate zig zig_cache_evidence " + suffix, required)
            self.assertIn("zig_cache_policy zig_cache_gate zig zig_cache_evidence " + suffix, required)

    def test_interpreter_probe_does_not_leave_windows_cr_in_bash_substitution(self):
        start = script(step(self.desktop, "Start checks resource observation"))
        code = re.search(r"BUSTER_CI_RESOURCES_PYTHON=\$\(.*? -c '([^']+)'\)", start).group(1)
        # Simulate native Windows text stdout before using the exact workflow
        # probe. Bash command substitution would remove LF but leave CR.
        translation = "import io,sys; sys.stdout=io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', newline='\\r\\n'); "
        result = subprocess.run([sys.executable, "-c", translation + code], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, Path(sys.executable).resolve().as_posix().encode("utf-8"))
        self.assertNotIn(b"\r", result.stdout)
        self.assertNotIn(b"\n", result.stdout)

    def test_payload_success_cannot_hide_failed_or_missing_observer(self):
        required = ["combinations_unix", "checks_resources_start", "checks_resources_stop"]
        success = {name: {"outcome": "success"} for name in required}
        self.assertEqual(ci_summary_core.assess(success, required), [])
        for name in required[1:]:
            for outcome in ("missing", "skipped", "failure", "cancelled"):
                with self.subTest(name=name, outcome=outcome):
                    outcomes = dict(success)
                    if outcome == "missing":
                        del outcomes[name]
                    else:
                        outcomes[name] = {"outcome": outcome}
                    self.assertIn(name, ci_summary_core.assess(outcomes, required))
        ordinary = {"combinations_unix": {"outcome": "success"},
                    "checks_resources_start": {"outcome": "skipped"},
                    "checks_resources_stop": {"outcome": "skipped"}}
        self.assertEqual(ci_summary_core.assess(ordinary, ["combinations_unix"]), [])


if __name__ == "__main__":
    unittest.main()
