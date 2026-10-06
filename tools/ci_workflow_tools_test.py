#!/usr/bin/env python3
"""Regressions for tools/ci_workflow_tools.py, the concurrent suite runner."""
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_workflow_tools as runner

# A fixture suite waits for every named peer's marker. Deadlines only bound a
# broken runner; a correct one never waits for them.
SUITE = """import os, pathlib, sys, time
name = pathlib.Path(__file__).stem
print('SUITE fixture ' + name + ' argv=' + ' '.join(sys.argv[1:]))
pathlib.Path(name + '.started').touch()
deadline = time.monotonic() + 30
for peer in {peers!r}:
    while not pathlib.Path(peer + '.started').exists():
        if time.monotonic() > deadline:
            print('peer never started: ' + peer)
            sys.exit(3)
        time.sleep(0.01)
time.sleep({sleep})
print('SUITE done ' + name)
sys.exit({status})
"""


class RunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.logs = self.root / "logs"

    def suite(self, name, status=0, peers=(), sleep=0):
        (self.root / (name + ".py")).write_text(SUITE.format(peers=list(peers), status=status, sleep=sleep),
                                                encoding="utf-8")
        return name + ".py", name + ".log"

    def run_suites(self, suites, jobs, heartbeat=30):
        output = io.StringIO()
        status = runner.run(suites, self.logs, jobs, cwd=self.root, output=output, heartbeat=heartbeat)
        return status, output.getvalue()

    def test_lanes_run_concurrently_and_every_log_is_recorded(self):
        # a and b can only finish if they run at the same time; c needs a lane
        # to be freed, so it also proves the queue drains past the lane count.
        suites = [self.suite("a", peers=["b"]), self.suite("b", peers=["a"]), self.suite("c")]
        status, text = self.run_suites(suites, jobs=2)
        self.assertEqual(status, 0, text)
        self.assertIn("WORKFLOW_TOOLS_START suites=3 jobs=2", text)
        for suite, log in suites:
            name = suite[:-3]
            self.assertIn(f"SUITE fixture {name} argv=-v", (self.logs / log).read_text())
            self.assertRegex(text, rf"SUITE_END path={re.escape(suite)} result=success elapsed_seconds=\d+ ")
            self.assertRegex(text, rf"SUITE_DURATION seconds=\d+ result=success path={re.escape(suite)}\n")
        self.assertRegex(text, r"WORKFLOW_TOOLS_END result=success elapsed_seconds=\d+ ")

    def test_each_suite_log_is_one_block_before_its_end_line(self):
        suites = [self.suite("a", peers=["b"]), self.suite("b", peers=["a"])]
        status, text = self.run_suites(suites, jobs=2)
        self.assertEqual(status, 0, text)
        for name in ("a", "b"):
            block = re.search(rf"SUITE fixture {name} argv=-v\nSUITE done {name}\nSUITE_END path={name}\.py ", text)
            self.assertIsNotNone(block, text)

    def test_failure_runs_every_suite_and_returns_first_failure_in_order(self):
        suites = [self.suite("a"), self.suite("b", status=7), self.suite("c"), self.suite("d", status=5)]
        status, text = self.run_suites(suites, jobs=1)
        self.assertEqual(status, 7, text)
        self.assertEqual(re.findall(r"SUITE_START path=(\S+)", text), ["a.py", "b.py", "c.py", "d.py"])
        self.assertIn("SUITE_END path=b.py result=failure status=7 ", text)
        self.assertIn("SUITE_END path=d.py result=failure status=5 ", text)
        self.assertIn("SUITE done c", (self.logs / "c.log").read_text())
        self.assertIn("WORKFLOW_TOOLS_END result=failure failed=b.py,d.py ", text)

    def test_missing_suite_fails_without_hiding_the_others(self):
        suites = [("missing.py", "missing.log"), self.suite("a")]
        status, text = self.run_suites(suites, jobs=2)
        self.assertNotEqual(status, 0, text)
        self.assertIn("SUITE_END path=missing.py result=failure", text)
        self.assertIn("SUITE_END path=a.py result=success", text)

    def test_heartbeat_names_running_suites(self):
        suites = [self.suite("a", sleep=1)]
        status, text = self.run_suites(suites, jobs=1, heartbeat=0.2)
        self.assertEqual(status, 0, text)
        self.assertRegex(text, r"SUITE_RUNNING path=a\.py elapsed_seconds=\d+\n")

    def test_statuses_fit_a_shell_exit_code(self):
        self.assertEqual(runner.exit_status(7), 7)
        self.assertEqual(runner.exit_status(-9), 1)
        self.assertEqual(runner.exit_status(0xC0000005), 1)


class CommandLineTests(unittest.TestCase):
    def command(self, *arguments, environment=None):
        return subprocess.run([sys.executable, str(ROOT / "tools/ci_workflow_tools.py"), *arguments],
                              cwd=ROOT, env=dict(os.environ, **(environment or {})),
                              capture_output=True, text=True, timeout=60)

    def test_rejects_malformed_duplicate_and_empty_arguments(self):
        with tempfile.TemporaryDirectory() as temporary:
            for arguments in (["tools/a.py"], ["tools/a.py="], ["=a.log"], ["tools/a.py=sub/a.log"],
                              ["tools/a.py=a.log", "tools/a.py=b.log"], ["tools/a.py=a.log", "tools/b.py=a.log"],
                              ["--jobs", "0", "tools/a.py=a.log"], []):
                with self.subTest(arguments=arguments):
                    result = self.command("--log-directory", temporary, *arguments)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            result = self.command("--log-directory", temporary, "tools/a.py=a.log", environment={"BUSTER_TEST_JOBS": "x"})
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_jobs_default_honors_buster_test_jobs(self):
        self.assertEqual(runner.default_jobs({}), runner.DEFAULT_JOBS)
        self.assertEqual(runner.default_jobs({"BUSTER_TEST_JOBS": "5"}), 5)
        self.assertEqual(runner.default_jobs({"BUSTER_TEST_JOBS": "-1"}), 0)


# The support declaration freezes these files byte-for-byte. Retain their
# original tests; replace only assertions owning the superseded topology.
def frozen_suite(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_frozen_ci = frozen_suite("frozen_ci_tools", "tests/ci_tools_test.py")
_frozen_actions = frozen_suite("frozen_action_pins", "tests/action_pins_test.py")


class CurrentWorkflowPolicyTests(_frozen_ci.WorkflowPolicyTests):
    def test_ordinary_desktop_has_no_full_lint_ancestor(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        blocks = dict(re.findall(r"(?ms)^  (\w+):\n(.*?)(?=^  \w+:|\Z)",
                                 text.split("\njobs:\n", 1)[1]))
        desktop = blocks["test"].split("    steps:", 1)[0]
        self.assertIn("needs: [queue_lint, reuse]", desktop)
        self.assertIn("github.event_name != 'merge_group' || needs.queue_lint.result == 'success'", desktop)
        self.assertNotIn("needs.lint", desktop)
        self.assertIn("needs: reuse", blocks["native"])
        for root in ("queue_lint", "reuse"):
            self.assertNotRegex(blocks[root], r"(?m)^    needs:")
        self.assertIn("github.event_name == 'merge_group'", blocks["queue_lint"])
        self.assertIn("github.event_name != 'merge_group'", blocks["lint"])
        self.assertEqual(blocks["lint"].split("    steps:\n", 1)[1],
                         blocks["queue_lint"].split("    steps:\n", 1)[1])
        aggregate = blocks["complete"]
        self.assertIn("github.event_name == 'merge_group' && needs.queue_lint.result || needs.lint.result", aggregate)
        self.assertIn("github.event_name == 'merge_group' && needs.lint.result || needs.queue_lint.result", aggregate)

    def test_independent_suites_are_not_guarded_by_prior_test_success(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        condition = re.search(r"id: modes\n        if: (.+)", text).group(1)
        self.assertIn("!cancelled()", condition)
        self.assertNotIn("steps.combinations_", condition)
        mobile = text.split("\n  mobile:", 1)[1].split("\n  complete:", 1)[0]
        self.assertIn("needs: reuse", mobile)
        self.assertNotIn("needs: test", mobile)
        self.assertIn("needs: [lint, queue_lint, test, native, mobile, uefi, analyzer, reuse]", text)
        self.assertIn("github.run_id", text.split("concurrency:", 1)[1].split("permissions:", 1)[0])

    def test_actual_aggregate_rejects_missing_skipped_cancelled_and_failed_shards(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        aggregate = text.split("\n  complete:", 1)[1]
        self.assertIn("needs: [lint, queue_lint, test, native, mobile, uefi, analyzer, reuse]", aggregate)
        self.assertIn("always()", aggregate)
        # Execute the workflow's real shell body, not a Python copy of its
        # predicate. Exercise all 625 existing shard outcomes with UEFI/analyzer
        # green, then reject unavailable/unsuccessful UEFI and analyzer results.
        body = aggregate.split("        run: |\n", 1)[1]
        body = textwrap.dedent(body)
        with tempfile.TemporaryDirectory() as temporary:
            gate = Path(temporary) / "aggregate.sh"
            gate.write_bytes(body.encode("utf-8"))
            environment = dict(os.environ, BUSTER_CI_GATE=gate.as_posix(),
                               GITHUB_STEP_SUMMARY=(Path(temporary) / "summary.md").as_posix())
            script = r"""
set -eu
checked=0
UEFI_RESULT=success ANALYZER_RESULT=success REUSE_REQUESTED=false REUSE_REVERIFIED=skipped INACTIVE_LINT_RESULT=skipped
export UEFI_RESULT ANALYZER_RESULT REUSE_REQUESTED REUSE_REVERIFIED INACTIVE_LINT_RESULT
for LINT_RESULT in success failure cancelled skipped ''; do
  for DESKTOP_RESULT in success failure cancelled skipped ''; do
    for NATIVE_RESULT in success failure cancelled skipped ''; do
     for MOBILE_RESULT in success failure cancelled skipped ''; do
      export LINT_RESULT DESKTOP_RESULT NATIVE_RESULT MOBILE_RESULT
      actual=0
      ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
      if [[ "$LINT_RESULT" == success && "$DESKTOP_RESULT" == success && "$NATIVE_RESULT" == success && "$MOBILE_RESULT" == success ]]; then
        [[ "$actual" -eq 0 ]] || exit 1
      else
        [[ "$actual" -ne 0 ]] || exit 1
      fi
      checked=$((checked + 1))
     done
    done
  done
done
LINT_RESULT=success DESKTOP_RESULT=success NATIVE_RESULT=success MOBILE_RESULT=success
export LINT_RESULT DESKTOP_RESULT NATIVE_RESULT MOBILE_RESULT
for UEFI_RESULT in failure cancelled skipped ''; do
  export UEFI_RESULT
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  [[ "$actual" -ne 0 ]] || exit 1
  checked=$((checked + 1))
done
UEFI_RESULT=success
export UEFI_RESULT
for ANALYZER_RESULT in failure cancelled skipped ''; do
  export ANALYZER_RESULT
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  [[ "$actual" -ne 0 ]] || exit 1
  checked=$((checked + 1))
done
ANALYZER_RESULT=success REUSE_REQUESTED=true
NATIVE_RESULT=skipped MOBILE_RESULT=skipped UEFI_RESULT=skipped
export ANALYZER_RESULT REUSE_REQUESTED NATIVE_RESULT MOBILE_RESULT UEFI_RESULT
for REUSE_REVERIFIED in success failure cancelled skipped ''; do
  export REUSE_REVERIFIED
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  if [[ "$REUSE_REVERIFIED" == success ]]; then
    [[ "$actual" -eq 0 ]] || exit 1
  else
    [[ "$actual" -ne 0 ]] || exit 1
  fi
  checked=$((checked + 1))
done
REUSE_REVERIFIED=success
export REUSE_REVERIFIED
for NATIVE_RESULT in success failure cancelled ''; do
  export NATIVE_RESULT
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  [[ "$actual" -ne 0 ]] || exit 1
  checked=$((checked + 1))
done
REUSE_REQUESTED=false NATIVE_RESULT=success MOBILE_RESULT=success UEFI_RESULT=success
export REUSE_REQUESTED NATIVE_RESULT MOBILE_RESULT UEFI_RESULT
for INACTIVE_LINT_RESULT in success failure cancelled ''; do
  export INACTIVE_LINT_RESULT
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  [[ "$actual" -ne 0 ]] || exit 1
  checked=$((checked + 1))
done
printf '%s\n' "$checked"
"""
            # Windows CreateProcess can choose System32/bash.exe (WSL)
            # before PATH. Use an absolute shell path; on Windows select
            # the installed Git Bash, not an unrelated WSL distribution.
            bash = shutil.which("bash")
            if os.name == "nt":
                git = shutil.which("git")
                self.assertIsNotNone(git, "Git for Windows is a CI prerequisite")
                bash = next((str(parent / "bin/bash.exe")
                             for parent in Path(git).resolve().parents
                             if (parent / "bin/bash.exe").is_file()), None)
            self.assertIsNotNone(bash, "Bash is a CI prerequisite")
            result = subprocess.run([bash, "--noprofile", "--norc", "-c", script], env=environment,
                                    capture_output=True, text=True, timeout=120 if os.name == "nt" else 30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), "646")


class CurrentActionPinsTests(_frozen_actions.ActionPinsTest):
    def test_github_lint_checks_policy(self):
        github = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("python3 tools/check_action_pins.py", github)
        self.assertIn("python3 -B tools/ci_workflow_policy_test.py", github)
        self.assertNotIn(".forgejo/", github)

    def test_buster_ci_uses_native_node24_artifact_action(self):
        github = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertNotIn(_frozen_actions.UPLOAD_ARTIFACT_V4_PIN, github)
        self.assertEqual(github.count(_frozen_actions.UPLOAD_ARTIFACT_V7_PIN), 9)
        self.assertEqual(_frozen_actions.PINS.APPROVED["actions/upload-artifact"],
                         {_frozen_actions.UPLOAD_ARTIFACT_V4_SHA,
                          _frozen_actions.UPLOAD_ARTIFACT_V7_SHA})


def load_tests(loader, tests, pattern):
    # Every non-policy frozen test stays executable. The inherited current
    # policy classes above retain all unaffected assertions and negative cases.
    for value in vars(_frozen_ci).values():
        if (isinstance(value, type) and issubclass(value, unittest.TestCase) and
                value.__module__ == _frozen_ci.__name__ and
                value is not _frozen_ci.WorkflowPolicyTests):
            tests.addTests(loader.loadTestsFromTestCase(value))
    return tests


if __name__ == "__main__":
    unittest.main()
