#!/usr/bin/env python3
"""Offline controls for throughput request retention and diagnostic source selection."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import re
import unittest

# Expose the main-push audit through the existing workflow-lint entry point.
from ci_workflow_concurrency_test import MainPushConcurrencyTests


ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github/workflows"
PR_WORKFLOW = WORKFLOWS / "compiler-throughput.yml"
REQUEST_WORKFLOW = WORKFLOWS / "compiler-throughput-requests.yml"


def top_level_section(source, key):
    match = re.search(rf"^{re.escape(key)}:\s*\n(.*?)(?=^[a-z][a-z-]*:|\Z)",
                      source, re.MULTILINE | re.DOTALL)
    if match is None:
        raise AssertionError(f"missing top-level {key} section")
    return match.group(1)


class CompilerThroughputWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pr = PR_WORKFLOW.read_text(encoding="utf-8")
        cls.requests = REQUEST_WORKFLOW.read_text(encoding="utf-8")

    def test_pr_latest_head_and_same_commit_reuse(self):
        triggers = top_level_section(self.pr, "on")
        concurrency = top_level_section(self.pr, "concurrency")
        self.assertIn("  pull_request:\n", triggers)
        self.assertIn("  workflow_call:\n", triggers)
        self.assertIn("      baseline_ref:\n", triggers)
        self.assertIn("        default: main\n", triggers)
        self.assertIn("        type: string\n", triggers)
        self.assertNotIn("workflow_dispatch:", triggers)
        self.assertNotIn("schedule:", triggers)
        self.assertIn("github.event_name == 'pull_request' && github.event.pull_request.number || github.run_id", concurrency)
        self.assertIn("  cancel-in-progress: ${{ github.event_name == 'pull_request' }}", concurrency)
        self.assertNotIn("queue:", concurrency)

    def test_manual_and_schedule_share_a_bounded_non_replacing_queue(self):
        triggers = top_level_section(self.requests, "on")
        concurrency = top_level_section(self.requests, "concurrency")
        jobs = top_level_section(self.requests, "jobs")
        self.assertIn("  workflow_dispatch:\n", triggers)
        self.assertIn("  schedule:\n", triggers)
        self.assertIn("        default: main\n", triggers)
        self.assertIn("  group: compiler-throughput-requests\n", concurrency)
        self.assertIn("  queue: max\n", concurrency)
        self.assertIn("  cancel-in-progress: false\n", concurrency)
        self.assertIn("uses: ./.github/workflows/compiler-throughput.yml", jobs)
        self.assertIn("baseline_ref: ${{ inputs.baseline_ref || 'main' }}", jobs)
        self.assertNotIn("secrets: inherit", self.requests)
        self.assertEqual(top_level_section(self.requests, "permissions").split("#", 1)[0].strip(),
                         "contents: read")

    def test_three_overlapping_requests_and_overflow(self):
        # GitHub's max policy retains 100 pending runs after the active one.
        # Both event sources use the one literal group above, so this models
        # manual -> manual -> schedule -> manual against a long active run.
        concurrency = top_level_section(self.requests, "concurrency")
        self.assertIn("  queue: max\n", concurrency)
        self.assertNotIn("${{", re.search(r"^  group: (.*)$", concurrency,
                                          re.MULTILINE).group(1))
        pending = []
        rejected = []
        for request in ["manual-1", "manual-2", "schedule-1", "manual-3"]:
            if len(pending) < 100:
                pending.append(request)
            else:
                rejected.append(request)
        self.assertEqual(pending, ["manual-1", "manual-2", "schedule-1", "manual-3"])
        for index in range(97):
            request = f"manual-{index + 4}"
            if len(pending) < 100:
                pending.append(request)
            else:
                rejected.append(request)
        self.assertEqual(len(pending), 100)
        self.assertEqual(rejected, ["manual-100"])
        self.assertEqual(pending[:4], ["manual-1", "manual-2", "schedule-1", "manual-3"])

    def test_only_reviewed_queue_uses_actionlint_exception(self):
        reviewed = {
            REQUEST_WORKFLOW,
            WORKFLOWS / "native-retirement-catch-up.yml",
            WORKFLOWS / "native-retirement-automation.yml",
            WORKFLOWS / "merge-queue-reconcile.yml",
            WORKFLOWS / "pages.yml",
        }
        check_writers = {
            WORKFLOWS / "9700x-direct-bench.yml": {"start-pull", "publish-pull", "start-compiler", "publish-compiler",
                                                     "sampling-queue", "sampling-publish",
                                                     "preparation-queue", "preparation-publish",
                                                     "utility-queue", "utility-publish"},
            WORKFLOWS / "9700x-compiler-request.yml": {"announce"},
            WORKFLOWS / "9700x-lifecycle.yml": {"reconcile"},
        }
        reviewed.update(check_writers)
        queued = {path for path in WORKFLOWS.glob("*.yml")
                  if re.search(r"^\s+queue:", path.read_text(), re.MULTILINE)}
        self.assertEqual(queued, reviewed)
        for path in reviewed:
            with self.subTest(workflow=path.name):
                text = path.read_text()
                if path in check_writers:
                    jobs = dict(re.findall(r"^  ([a-z0-9_-]+):\n(.*?)(?=^  [a-z0-9_-]+:|\Z)",
                                           top_level_section(text, "jobs"), re.MULTILINE | re.DOTALL))
                    locked = {name for name, block in jobs.items() if re.search(r"^      queue:", block, re.MULTILINE)}
                    self.assertEqual(locked, check_writers[path])
                    expected = ("    concurrency:\n      group: buster-9700x-check-writer\n"
                                "      cancel-in-progress: false\n      queue: max\n")
                    for name in locked:
                        self.assertIn(expected, jobs[name])
                    self.assertEqual(len(re.findall(r"^\s+queue:", text, re.MULTILINE)), len(locked))
                    continue
                concurrency = top_level_section(text, "concurrency")
                policy = "single" if path.name == "merge-queue-reconcile.yml" else "max"
                self.assertIn(f"  queue: {policy}\n", concurrency)
                self.assertIn("  cancel-in-progress: false\n", concurrency)
                self.assertEqual(len(re.findall(r"^\s+queue:", text, re.MULTILINE)), 1)
                group = re.search(r"^  group: (.*)$", concurrency, re.MULTILINE).group(1)
                if path.name == "pages.yml":
                    self.assertEqual(group, "pages-${{ github.event_name == 'pull_request' && "
                                     "github.event.pull_request.number || github.ref }}")
                else:
                    self.assertNotIn("${{", group)
        ci = (WORKFLOWS / "ci.yml").read_text(encoding="utf-8")
        self.assertIn("python3 tools/compiler_throughput_workflow_test.py", ci)
        self.assertIn("-ignore 'unexpected key \"queue\" for \"concurrency\" section'", ci)


class DiagnosticSourceTest(unittest.TestCase):
    # Real Git histories with synthetic compiler output; no performance claim.

    @classmethod
    def setUpClass(cls):
        cls.workflow = PR_WORKFLOW.read_text(encoding="utf-8")
        cls.census = cls.workflow.split("\n  direct-ssa-census:\n", 1)[1]

    @staticmethod
    def script(job, name):
        block = job.split("      - name: " + name + "\n", 1)[1].split("\n      - ", 1)[0]
        return "\n".join(line[10:] for line in block.split("        run: |\n", 1)[1].splitlines()) + "\n"

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "Diagnostic fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        (self.repo / ".github/workflows").mkdir(parents=True)
        self.workflow_path = self.repo / ".github/workflows/compiler-throughput.yml"
        # Replace the whole run body while retaining workflow/source provenance.
        tiny = self.script(self.census, "Verify production tiny helper call removal")
        old = self.workflow.replace("\n".join("          " + line for line in tiny.rstrip("\n").splitlines()),
                                    "          printf 'old main: tiny option not introduced yet\\n'")
        self.workflow_path.write_text(old)
        self.base = self.commit("old main without option")
        self.git("checkout", "-q", "-b", "feature")
        (self.repo / "feature.txt").write_text("old-base feature remains present\n")
        self.head = self.commit("feature from old base")
        self.git("checkout", "-q", "main")
        self.workflow_path.write_text(self.workflow)
        (self.repo / "supports-options").write_text("canonical-inline\n")
        self.main = self.commit("main adds option and its diagnostic proof")
        self.git("merge", "--no-ff", "-q", "feature", "-m", "PR test merge")
        self.merge = self.git("rev-parse", "HEAD")
        executable = self.repo / "build/Release/ide"
        executable.parent.mkdir(parents=True)
        executable.write_text("#!" + sys.executable + "\n" + r'''
import pathlib
import sys
args = sys.argv[1:]
if not pathlib.Path("supports-options").is_file():
    sys.stderr.write("cc: error: unsupported option: -fno-canonical-inline\n")
    sys.exit(1)
mode = "off" if "-fno-canonical-inline" in args else "on"
if "-fcanonical-inline-module-growth=1048576" in args:
    mode = "lifted"
defect = pathlib.Path("defect").read_text() if pathlib.Path("defect").exists() else ""
counts = {"off": (3, 3), "on": (0, 0), "lifted": (0, 0)}[mode]
if defect == "no-removal":
    counts = (3, 3)
assembly = ""
for helper, count in zip(("c_ir_field_index_bucket", "machine_x64_reject"), counts):
    if defect != "missing-definition":
        assembly += helper + ":\n"
    assembly += ("    call " + helper + "\n") * count
pathlib.Path(args[args.index("-o") + 1]).write_text(assembly)
print("IR_INLINE synthetic orchestration control")
''')
        executable.chmod(0o755)

    def git(self, *args):
        result = subprocess.run(["git", "-c", "maintenance.auto=false", "-c", "gc.auto=0", *args],
                                cwd=self.repo, text=True, capture_output=True, check=True)
        return result.stdout.strip()

    def commit(self, message):
        self.git("add", ".")
        self.git("commit", "-q", "-m", message)
        return self.git("rev-parse", "HEAD")

    def run_script(self, script, **extra):
        environment = dict(os.environ, CENSUS_EVENT_SHA=self.merge, CENSUS_WORKFLOW_SHA=self.merge,
                           CENSUS_PR_HEAD_SHA=self.head, CENSUS_PR_BASE_SHA=self.main)
        environment.update(extra)
        return subprocess.run(["bash", "-c", script], cwd=self.repo, env=environment,
                              text=True, capture_output=True)

    def test_diagnostic_checkout_is_event_revision_only(self):
        ref = re.search(r"^          ref: (.+)$", self.census, re.MULTILINE).group(1)
        self.assertEqual(ref, "${{ github.sha }}")
        self.assertIn("          persist-credentials: false\n", self.census)
        self.assertEqual(top_level_section(self.workflow, "permissions").strip(), "contents: read")
        self.assertNotIn("pull_request_target:", top_level_section(self.workflow, "on"))
        # Timing and harness continue to evaluate the feature head separately.
        self.assertEqual(self.workflow.split("\n  direct-ssa-census:", 1)[0].count(
            "ref: ${{ github.event.pull_request.head.sha || github.sha }}"), 2)
        self.assertIn("steps.direct.outcome == 'success' && steps.reference.outcome == 'success'", self.census)
        self.assertNotIn("continue-on-error:", self.census)

    def test_old_base_before_and_after_option_adding_main(self):
        bind = self.script(self.census, "Bind diagnostic source to its workflow revision")
        tiny = self.script(self.census, "Verify production tiny helper call removal")
        self.git("checkout", "-q", "feature")
        before_workflow = self.workflow_path.read_text()
        before_job = before_workflow.split("\n  direct-ssa-census:\n", 1)[1]
        before = self.run_script(bind, CENSUS_EVENT_SHA=self.head, CENSUS_WORKFLOW_SHA=self.head)
        self.assertEqual(before.returncode, 0, before.stderr)
        self.assertEqual(self.run_script(self.script(before_job, "Verify production tiny helper call removal")).returncode, 0)
        # The source contract rejects a new workflow paired with that old checkout.
        mismatch = self.run_script(bind)
        self.assertNotEqual(mismatch.returncode, 0)
        self.assertIn("no proof was run", mismatch.stderr)
        # Reproduce the original bug: newer proof against the unchanged old head.
        wrong = self.run_script(tiny)
        self.assertNotEqual(wrong.returncode, 0)
        self.assertIn("unsupported option: -fno-canonical-inline",
                      (self.repo / "direct-ssa-census-evidence/tiny-off.stderr.log").read_text())
        self.git("checkout", "-q", self.merge)
        bound = self.run_script(bind)
        self.assertEqual(bound.returncode, 0, bound.stderr)
        self.assertIn("source_sha=" + self.merge, bound.stdout)
        self.assertIn("pr_head_sha=" + self.head, bound.stdout)
        self.assertIn("workflow_blob=" + self.git("rev-parse", "HEAD:.github/workflows/compiler-throughput.yml"), bound.stdout)
        self.assertTrue((self.repo / "feature.txt").is_file())
        proof = self.run_script(tiny)
        self.assertEqual(proof.returncode, 0, proof.stderr)
        self.assertIn("INLINE_PRODUCTION_CALLS", proof.stdout)
        self.assertTrue((self.repo / "direct-ssa-census-evidence/production-tiny-helper-calls.json").is_file())

    def test_source_mismatch_fails_and_retains_actual_identity(self):
        bind = self.script(self.census, "Bind diagnostic source to its workflow revision")
        for variable in ("CENSUS_EVENT_SHA", "CENSUS_WORKFLOW_SHA"):
            with self.subTest(variable=variable):
                result = self.run_script(bind, **{variable: self.head})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("no proof was run", result.stderr)
                record = (self.repo / "direct-ssa-census-evidence/source.txt").read_text()
                self.assertIn("source_sha=" + self.merge, record)
        # A reusable non-PR caller has empty PR fields, still the same source contract.
        result = self.run_script(bind, CENSUS_PR_HEAD_SHA="", CENSUS_PR_BASE_SHA="")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_production_proof_still_rejects_missing_definitions_and_no_removal(self):
        (self.repo / "direct-ssa-census-evidence").mkdir()
        tiny = self.script(self.census, "Verify production tiny helper call removal")
        for defect in ("missing-definition", "no-removal"):
            with self.subTest(defect=defect):
                (self.repo / "defect").write_text(defect)
                result = self.run_script(tiny)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("named production helpers were not removed", result.stderr)


if __name__ == "__main__":
    unittest.main()
