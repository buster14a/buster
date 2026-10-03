#!/usr/bin/env python3
"""Run hosted mobile orchestration with finite leaves, without native tools."""

import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/ios-monitor-tests.yml"
LEAVES = {
    "signing": "python3 ios/hosted_signing_budget_test.py",
    "install": "bash ios/install_lifecycle_test.sh",
    "attached": "/bin/bash ios/launch_diagnostics_mock_test.sh",
    "shared": "bash tests/mobile_ci_scripts_test.sh",
}
FINITE_LEAF = r"""
import json, os, pathlib, signal, sys, time
stage = sys.argv[1]
root = pathlib.Path(os.environ['CONTROL_ROOT'])
statuses = json.loads(os.environ['CONTROL_STATUSES'])
def record(kind):
    (root / (stage + '.' + kind)).write_text(json.dumps({
        'stage': stage, 'time': time.monotonic_ns(),
        'evidence': os.environ['BUSTER_MOBILE_TEST_EVIDENCE_DIR']}))
def cancel(signum, frame):
    record('cancelled')
    if stage not in os.environ.get('CONTROL_IGNORE_TERM', '').split(','):
        sys.exit(0)  # Successful leaf cancellation cannot clear caller status.
signal.signal(signal.SIGTERM, cancel)
record('entered')
print(stage + ' fixture entered', flush=True)
if stage in ('signing', 'install'):
    peer = 'install' if stage == 'signing' else 'signing'
    deadline = time.monotonic() + 2
    while not (root / (peer + '.entered')).exists() and time.monotonic() < deadline:
        time.sleep(.01)
    if not (root / (peer + '.entered')).exists():
        print('independent groups did not overlap', flush=True)
        sys.exit(91)
delays = json.loads(os.environ.get('CONTROL_DELAYS', '{}'))
time.sleep(float(delays.get(stage, os.environ.get('CONTROL_DELAY', '.04'))))
record('done')
print(stage + ' fixture done', flush=True)
sys.exit(statuses.get(stage, 0))
"""
FAKE_PREREQUISITE = r"""
import json, os, pathlib, sys
root = pathlib.Path(os.environ['CONTROL_ROOT'])
with (root / 'prerequisites.jsonl').open('a') as log:
    log.write(json.dumps([pathlib.Path(sys.argv[0]).name, sys.argv[1:]]) + '\n')
if pathlib.Path(sys.argv[0]).name == 'python3':
    marker = root / 'chooser-probed'
    status = 0 if marker.exists() else int(os.environ['CONTROL_PROVIDER_STATUS'])
    marker.touch()
    sys.exit(status)
"""


def workflow_script(name):
    lines = WORKFLOW.read_text().splitlines()
    start = lines.index("      - name: " + name)
    script = []
    collecting = False
    for line in lines[start + 1:]:
        if collecting:
            if line and not line.startswith("          "):
                break
            script.append(line)
        elif line == "        run: |":
            collecting = True
    if not collecting or not script:
        raise AssertionError("missing workflow script: " + name)
    return textwrap.dedent("\n".join(script)) + "\n"


@unittest.skipUnless(os.name == "posix", "POSIX child-group control required")
class MonitorWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="buster-monitor-workflow-")
        self.root = Path(self.temporary.name)
        self.leaf = self.root / "finite_leaf.py"
        self.leaf.write_text(textwrap.dedent(FINITE_LEAF))
        self.script = workflow_script("Exercise mobile script and attached monitor lifecycles")
        for stage, command in LEAVES.items():
            self.assertEqual(self.script.count(command), 1)
            replacement = shlex.join([sys.executable, str(self.leaf), stage])
            self.script = self.script.replace(command, replacement)

    def tearDown(self):
        self.temporary.cleanup()

    def environment(self, statuses=None, delay=".04"):
        return dict(os.environ, RUNNER_TEMP=str(self.root), CONTROL_ROOT=str(self.root),
                    CONTROL_STATUSES=json.dumps({} if statuses is None else statuses),
                    CONTROL_DELAY=delay)

    def run_groups(self, statuses=None):
        return subprocess.run(["/bin/bash", "-c", self.script], cwd=ROOT,
                              env=self.environment(statuses), capture_output=True, text=True, timeout=5)

    def record(self, stage, kind="done"):
        return json.loads((self.root / (stage + "." + kind)).read_text())

    def test_success_overlaps_independent_groups_and_keeps_chain_order_and_evidence(self):
        result = self.run_groups()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for stage in LEAVES:
            self.assertTrue((self.root / (stage + ".done")).exists(), stage)
        signing = self.record("signing")
        install = self.record("install")
        attached = self.record("attached")
        shared = self.record("shared")
        self.assertLess(self.record("signing", "entered")["time"], install["time"])
        self.assertLess(self.record("install", "entered")["time"], signing["time"])
        self.assertLess(install["time"], self.record("attached", "entered")["time"])
        self.assertLess(attached["time"], self.record("shared", "entered")["time"])
        self.assertEqual(signing["evidence"], str(self.root / "mobile-lifecycle-evidence/signing"))
        for record in (install, attached, shared):
            self.assertEqual(record["evidence"], str(self.root / "mobile-lifecycle-evidence/lifecycle"))
        self.assertIn("signing_status=0 lifecycle_status=0", result.stdout)

    def test_both_failures_are_waited_and_retained_without_running_later_chain_leaves(self):
        result = self.run_groups({"signing": 23, "install": 17})
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
        self.assertIn("signing_status=23 lifecycle_status=17", result.stdout)
        for stage, log in (("signing", "ios-signing-budget.log"), ("install", "ios-install-lifecycle.log")):
            self.assertTrue((self.root / (stage + ".done")).exists())
            self.assertIn(stage + " fixture done", (self.root / log).read_text())
        self.assertFalse((self.root / "attached.entered").exists())
        self.assertFalse((self.root / "shared.entered").exists())

    def test_attached_failure_retains_signing_success_and_stops_shared_leaf(self):
        result = self.run_groups({"attached": 31})
        self.assertEqual(result.returncode, 31, result.stdout + result.stderr)
        self.assertIn("signing_status=0 lifecycle_status=31", result.stdout)
        self.assertTrue((self.root / "signing.done").exists())
        self.assertTrue((self.root / "attached.done").exists())
        self.assertFalse((self.root / "shared.entered").exists())

    def test_int_and_term_join_both_groups_without_clearing_cancellation(self):
        for signum, expected in ((signal.SIGINT, 130), (signal.SIGTERM, 143)):
            with self.subTest(signal=signum):
                for stage in LEAVES:
                    for kind in ("entered", "done", "cancelled"):
                        (self.root / (stage + "." + kind)).unlink(missing_ok=True)
                process = subprocess.Popen(["/bin/bash", "-c", self.script], cwd=ROOT,
                                           env=self.environment(delay=".7"), stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE, text=True, start_new_session=True)
                try:
                    deadline = time.monotonic() + 2
                    while not all((self.root / (stage + ".entered")).exists() for stage in ("signing", "install")):
                        if process.poll() is not None or time.monotonic() >= deadline:
                            self.fail("finite cancellation leaves were not both admitted")
                        time.sleep(.01)
                    os.kill(process.pid, signum)
                    output, errors = process.communicate(timeout=3)
                    self.assertEqual(process.returncode, expected, output + errors)
                    self.assertNotIn("IOS_LIFECYCLE_GROUPS", output)
                    for stage in ("signing", "install"):
                        self.assertTrue((self.root / (stage + ".cancelled")).exists(), output + errors)
                    self.assertFalse((self.root / "attached.entered").exists())
                finally:
                    if process.poll() is None:
                        os.kill(process.pid, signal.SIGTERM)
                        process.communicate(timeout=3)

    def test_provider_install_is_conditional_and_cleanup_failure_is_fatal(self):
        script = workflow_script("Ensure mobile test prerequisites are available")
        tools = self.root / "tools"
        tools.mkdir()
        for name in ("python3", "sudo"):
            path = tools / name
            path.write_text("#!" + sys.executable + "\n" + textwrap.dedent(FAKE_PREREQUISITE))
            path.chmod(0o700)
        for status in (0, 1, 2):
            with self.subTest(first_probe_status=status):
                for name in ("chooser-probed", "prerequisites.jsonl"):
                    (self.root / name).unlink(missing_ok=True)
                env = dict(self.environment(), RUNNER_OS="Linux", PATH=str(tools) + os.pathsep + os.environ["PATH"],
                           CONTROL_PROVIDER_STATUS=str(status))
                result = subprocess.run(["/bin/bash", "-c", script], cwd=ROOT, env=env,
                                        capture_output=True, text=True, timeout=3)
                calls = [json.loads(line) for line in (self.root / "prerequisites.jsonl").read_text().splitlines()]
                chooser = ["python3", ["ios/gnu_timeout.py"]]
                expected = [chooser]
                if status == 1:
                    expected += [["sudo", ["apt-get", "update"]],
                                 ["sudo", ["apt-get", "install", "--no-install-recommends", "gnu-coreutils"]]]
                if status != 2:
                    expected += [chooser]
                self.assertEqual(calls, expected)
                self.assertEqual(result.returncode, 2 if status == 2 else 0, result.stderr)

    def test_finished_signing_anchor_remains_owned_during_term_ignoring_lifecycle_cancellation(self):
        env = dict(self.environment(), CONTROL_DELAYS=json.dumps({"install": 1.7}), CONTROL_IGNORE_TERM="install")
        process = subprocess.Popen(["/bin/bash", "-c", self.script], cwd=ROOT, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
        try:
            deadline = time.monotonic() + 2
            while not (self.root / "signing.done").exists():
                if process.poll() is not None or time.monotonic() >= deadline:
                    self.fail("signing did not finish before cancellation")
                time.sleep(.01)
            started = time.monotonic()
            os.kill(process.pid, signal.SIGTERM)
            output, errors = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 143, output + errors)
            self.assertLess(time.monotonic() - started, 1.6)
            self.assertTrue((self.root / "install.cancelled").exists(), output + errors)
            self.assertFalse((self.root / "install.done").exists())
            self.assertFalse((self.root / "attached.entered").exists())
            self.assertNotIn("dispatch:", errors)
            time.sleep(.8)  # Past finite leaf completion, detect ineffective KILL.
            self.assertFalse((self.root / "install.done").exists())
        finally:
            if process.poll() is None:
                os.kill(process.pid, signal.SIGTERM)
                process.communicate(timeout=3)

    def test_registration_keeps_cap_individual_waits_android_gate_and_all_logs(self):
        source = WORKFLOW.read_text()
        script = workflow_script("Exercise mobile script and attached monitor lifecycles")
        self.assertIn("    timeout-minutes: 10", source.split("  fake-tools:", 1)[1])
        self.assertIn("exec python3 ios/monitor_groups.py", script)
        runner = (ROOT / "ios/monitor_groups.py").read_text()
        self.assertIn('"pid": process.pid', runner)
        self.assertLess(runner.index('os.killpg(group["pid"], signal.SIGKILL)'),
                        runner.index('group["process"].wait('))
        self.assertEqual(source.count("if: ${{ !cancelled() && steps.lifecycle.outcome == 'success' }}"), 2)
        self.assertIn("python3 ios/gnu_timeout_test.py -v", source)
        self.assertIn("python3 ios/monitor_workflow_test.py -v", source)
        for log in ("mobile-lifecycle", "ios-signing-budget", "ios-install-lifecycle", "ios-launch-diagnostics",
                    "ios-gnu-timeout", "ios-monitor-workflow", "ios-lifecycle-groups", "android-status", "android-summary"):
            self.assertIn("${{ runner.temp }}/" + log + ".log", source)


if __name__ == "__main__":
    unittest.main()
