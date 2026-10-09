#!/usr/bin/env python3
"""Stateless validation scheduling contract; see docs/ci-stateless-concurrency.md."""

from pathlib import Path
import re
import unittest
import subprocess
import shlex
import os
import signal
import contextlib
import shutil
import tempfile

from lifecycle_pipeline_test import LifecyclePipelineTests

import workflow_policy_test as policy

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ("bench-service-policy",)


def scalar(token, context):
    token = token.strip()
    if token in context:
        value = context[token]
    elif token in ("true", "false"):
        value = token == "true"
    elif re.fullmatch(r"'[a-z_]+'", token):
        value = token[1:-1]
    else:
        raise AssertionError("Unsupported concurrency operand: " + token)
    return value


def expression(text, context):
    # Evaluate only the checked-in scalar/equality/AND/OR subset, without eval.
    result = False
    for alternative in text.split("||"):
        for operand in alternative.split("&&"):
            left, equality, right = operand.partition("==")
            result = scalar(left, context)
            if equality:
                result = result == scalar(right, context)
            if not result:
                break
        if result:
            break
    return result


class StatelessConcurrencyTests(unittest.TestCase):
    def context(self, event="push", run_id=101, pr=7, ref=None):
        return {
            "github.event_name": event,
            "github.run_id": run_id,
            # Duplicate invocations of one SHA must also survive.
            "github.sha": "a" * 40,
            "github.event.pull_request.number": pr if event == "pull_request" else "",
            "github.ref": ref or ("refs/heads/gh-readonly-queue/main/pr-7-example"
                                  if event == "merge_group" else "refs/heads/main"),
        }

    def fields(self, name):
        text = (ROOT / ".github/workflows" / (name + ".yml")).read_text(encoding="utf-8")
        block = re.search(r"(?ms)^concurrency:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
        self.assertIsNotNone(block)
        lines = [line.strip() for line in block.group(1).splitlines()
                 if line.strip() and not line.lstrip().startswith("#")]
        fields = dict(line.split(": ", 1) for line in lines)
        self.assertEqual(len(lines), len(fields), "Duplicate concurrency key")
        self.assertEqual(set(fields), {"group", "cancel-in-progress"})
        return fields

    def group(self, fields, context):
        return re.sub(r"\$\{\{(.*?)\}\}",
                      lambda match: str(expression(match.group(1), context)),
                      fields["group"]).lower()

    def cancels(self, fields, context):
        text = fields["cancel-in-progress"]
        if text.startswith("$" + "{{"):
            text = text[3:-2]
        return bool(expression(text, context))

    def assert_retained(self, fields, event):
        contexts = [self.context(event, run_id) for run_id in (101, 102, 103)]
        for context in contexts:
            self.assertFalse(self.cancels(fields, context))
        self.assertEqual(len({self.group(fields, context) for context in contexts}), 3,
                         "A third invocation must not replace a pending main run")

    def test_three_main_invocations_are_retained(self):
        for name in WORKFLOWS:
            with self.subTest(workflow=name):
                self.assert_retained(self.fields(name), "push")

    def test_benchmark_manual_invocations_are_retained_and_isolated(self):
        fields = self.fields(WORKFLOWS[0])
        self.assert_retained(fields, "workflow_dispatch")
        self.assertNotEqual(self.group(fields, self.context("push")),
                            self.group(fields, self.context("workflow_dispatch")))

    def test_candidate_coalescing_and_cancellation_are_preserved(self):
        for name in WORKFLOWS:
            fields = self.fields(name)
            for event in ("pull_request", "merge_group"):
                with self.subTest(workflow=name, event=event):
                    contexts = [self.context(event, run_id) for run_id in (101, 102, 103)]
                    self.assertEqual(len({self.group(fields, c) for c in contexts}), 1)
                    for context in contexts:
                        self.assertEqual(self.cancels(fields, context), name == WORKFLOWS[0])

    def test_distinct_workflows_events_prs_and_queue_refs_are_isolated(self):
        groups = []
        for name in WORKFLOWS:
            fields = self.fields(name)
            contexts = [self.context("push"), self.context("workflow_dispatch"),
                        self.context("pull_request", pr=7), self.context("pull_request", pr=8),
                        self.context("merge_group"),
                        self.context("merge_group", ref="refs/heads/gh-readonly-queue/main/pr-8-next")]
            groups.extend(self.group(fields, context) for context in contexts)
        self.assertEqual(len(groups), len(set(groups)))

    def test_legacy_active_cancellation_is_rejected(self):
        fields = {"group": "legacy-${{ github.ref }}", "cancel-in-progress": "true"}
        with self.assertRaises(AssertionError):
            self.assert_retained(fields, "push")

    def test_false_alone_still_loses_pending_invocations(self):
        for key in ("github.ref", "github.sha"):
            fields = {"group": "legacy-${{ " + key + " }}", "cancel-in-progress": "false"}
            with self.subTest(key=key), self.assertRaises(AssertionError):
                self.assert_retained(fields, "push")

    def test_short_check_writer_queue_retains_and_bounds_pending_jobs(self):
        # GitHub queue:max admits 100 pending jobs; it cannot promise infinite
        # delivery. This model exercises the saturation boundary explicitly.
        pending, cancelled = [], []
        for job in range(1, 102):
            if len(pending) < 100:
                pending.append(job)
            else:
                cancelled.append(job)
        self.assertEqual(pending, list(range(1, 101)))
        self.assertEqual(cancelled, [101])
        expected = ("      group: buster-9700x-check-writer\n"
                    "      cancel-in-progress: false\n"
                    "      queue: max\n")
        for filename, count in (("9700x-direct-bench.yml", 10),
                                ("9700x-compiler-request.yml", 1), ("9700x-lifecycle.yml", 1)):
            text = (ROOT / ".github/workflows" / filename).read_text(encoding="utf-8")
            self.assertEqual(text.count(expected), count)

    def test_terminal_replay_policy_rejects_authority_or_scope_drift(self):
        workflow = (ROOT / ".github/workflows/9700x-lifecycle.yml").read_text(encoding="utf-8")
        errors = []
        policy.check_lifecycle(errors, workflow)
        self.assertEqual(errors, [])
        # Exercise the production exact allowlist, not a second policy model.
        replacements = (
            ("github.ref == 'refs/heads/main'", "github.ref != ''"),
            ("github.actor == 'davidgmbb'", "github.actor != ''"),
            ("github.actor_id == '39247043'", "github.actor_id != ''"),
            ("github.triggering_actor == 'davidgmbb'", "github.triggering_actor != ''"),
            ("ref: ${{ github.sha }}", "ref: main"),
            ("LC_ATTEMPT: ${{ github.event.workflow_run.run_attempt || inputs.run_attempt }}",
             "LC_ATTEMPT: 1"),
            ("      checks: write", "      actions: write"),
            ("      queue: max", "      queue: single"),
            ('recover "$LC_RUN_ID" "$LC_ATTEMPT"', 'recover "$LC_RUN_ID" 1'),
            ("          set -o pipefail", "          :"),
            ("    runs-on: ubuntu-24.04", "    runs-on: self-hosted"),
            ("    types: [completed]", "    types: [requested, completed]"),
        )
        for original, changed in replacements:
            with self.subTest(original=original):
                self.assertIn(original, workflow)
                errors = []
                policy.check_lifecycle(errors, workflow.replace(original, changed, 1))
                self.assertTrue(errors)

    def test_actual_physical_guard_blocks_unknown_active_and_dangling_aliases(self):
        physical = policy.job_blocks(policy.DIRECT.read_text())
        for job in ("bench", "compare-pull", "compare", "sampling", "preparation", "utility"):
            self.assertEqual(policy.run_scripts(physical[job])[0], list(policy.CLEANUP_GUARD_SCRIPT))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            unknown, active, sentinel = root / "unknown", root / "active", root / "continued"
            recipe = "\n".join(policy.CLEANUP_GUARD_SCRIPT).replace(
                "/tmp/buster-9700x-cleanup-unknown-v1", str(unknown)).replace(
                "/tmp/buster-9700x-cleanup-active-v1", str(active))
            command = recipe + "\nprintf continued > " + str(sentinel) + "\n"
            self.assertEqual(subprocess.run(["bash", "-c", command], capture_output=True).returncode, 0)
            self.assertTrue(sentinel.is_file())
            sentinel.unlink()
            for path in (unknown, active):
                for kind in ("directory", "file", "dangling"):
                    with self.subTest(path=path.name, kind=kind):
                        if kind == "directory":
                            path.mkdir()
                        elif kind == "file":
                            path.write_text("retained")
                        else:
                            path.symlink_to(root / "absent-target")
                        result = subprocess.run(["bash", "-c", command], capture_output=True)
                        self.assertEqual(result.returncode, 1)
                        self.assertFalse(sentinel.exists())
                        self.assertTrue(path.exists() or path.is_symlink())
                        if kind == "directory":
                            path.rmdir()
                        else:
                            path.unlink()

    def test_regression_runs_in_benchmark_policy(self):
        text = (ROOT / ".github/workflows/bench-service-policy.yml").read_text(encoding="utf-8")
        self.assertIn("run: python3 -B tools/bench_direct/workflow_concurrency_test.py -v", text)



class TccProofAggregateTests(unittest.TestCase):
    def gate(self):
        text = (ROOT / ".github/workflows/tcc-bootstrap.yml").read_text(encoding="utf-8")
        block = re.search(r"(?ms)^  validation:\n(.*?)(?=^  [a-z_]+:\n|\Z)", text)
        self.assertIsNotNone(block)
        self.assertIn("name: Canonical TCC bootstrap", block.group(1))
        self.assertIn("needs: [no_code_plan, bootstrap, ordinary, utility]", block.group(1))
        self.assertIn("if: ${{ always() && github.server_url == 'https://github.com' }}", block.group(1))
        self.assertIn("timeout-minutes: 2", block.group(1))
        self.assertEqual(text.count("timeout-minutes: 10"), 3)
        script = block.group(1).split("        run: |\n", 1)[1]
        return "\n".join(line[10:] for line in script.splitlines())

    def test_ordinary_proof_keeps_its_full_recipe_and_bound(self):
        text = (ROOT / ".github/workflows/tcc-bootstrap.yml").read_text(encoding="utf-8")
        block = re.search(r"(?ms)^  ordinary:\n(.*?)(?=^  [a-z_]+:\n|\Z)", text)
        self.assertIsNotNone(block)
        ordinary = block.group(1)
        for entry in ("needs: no_code_plan", "runs-on: ubuntu-24.04", "timeout-minutes: 10",
                      "BQ_REQUIRE_DISTINCT_GROUP: '1'", "ref: ${{ github.sha }}",
                      'test "$tested_sha" = "$GITHUB_SHA"',
                      "ref: 0fb54300b56512754221d80adda85ddb9815bceb",
                      './build.sh compiler_closure driver-path > "$RUNNER_TEMP/ordinary-native-driver.txt"',
                      '[[ "${#drivers[@]}" == 1 && "${drivers[0]}" == /* && -x "${drivers[0]}" ]]',
                      'python3 -B tools/bench_direct/compiler_ordinary_fixture_test.py --native-driver "$driver" --export "$RUNNER_TEMP/ordinary-bridge-diagnostic"',
                      "name: buster-hosted-ordinary-phase-proof-${{ github.sha }}-${{ github.run_attempt }}",
                      "${{ runner.temp }}/ordinary-bridge-diagnostic",
                      "if: ${{ always() }}", "retention-days: 90"):
            with self.subTest(entry=entry):
                self.assertIn(entry, ordinary)
        self.assertEqual(text.count("python3 -B tools/bench_direct/compiler_ordinary_fixture_test.py "), 1)
        self.assertEqual(text.count("name: buster-hosted-ordinary-phase-proof-"), 1)
        self.assertNotIn("self-hosted", ordinary)
        self.assertNotIn("pull_request.head.sha", ordinary)

    def execute(self, **overrides):
        environment = {"PATH": "/usr/bin:/bin", "EVENT_NAME": "pull_request",
                       "PLAN_RESULT": "success", "NO_CODE": "false",
                       "NATIVE_RESULT": "success", "UTILITY_RESULT": "success", "ORDINARY_RESULT": "success",
                       "CI_ENABLED": "true"}
        environment.update(overrides)
        return subprocess.run(["bash", "-c", self.gate()], env=environment,
                              capture_output=True, timeout=5).returncode

    def test_complete_proofs_and_trusted_no_code_are_the_only_successes(self):
        for event in ("pull_request", "merge_group", "push"):
            with self.subTest(event=event):
                self.assertEqual(self.execute(EVENT_NAME=event), 0)
        self.assertEqual(self.execute(NO_CODE="true", NATIVE_RESULT="skipped",
                                      UTILITY_RESULT="skipped", ORDINARY_RESULT="skipped"), 0)
        self.assertEqual(self.execute(EVENT_NAME="push", PLAN_RESULT="skipped"), 0)

    def test_each_incomplete_proof_fails_the_required_aggregate(self):
        for key in ("NATIVE_RESULT", "UTILITY_RESULT", "ORDINARY_RESULT"):
            for result in ("failure", "cancelled", "timed_out", "skipped", ""):
                with self.subTest(key=key, result=result):
                    self.assertEqual(self.execute(**{key: result}), 1)
        self.assertEqual(self.execute(NATIVE_RESULT="cancelled", UTILITY_RESULT="cancelled", ORDINARY_RESULT="cancelled"), 1)
        self.assertEqual(self.execute(CI_ENABLED="false"), 1)

    def test_no_code_requires_successful_trusted_classification_and_all_skips(self):
        for event in ("pull_request", "merge_group"):
            for plan in ("failure", "cancelled", "skipped", ""):
                with self.subTest(event=event, plan=plan):
                    self.assertEqual(self.execute(EVENT_NAME=event, PLAN_RESULT=plan, NO_CODE="true",
                                                  NATIVE_RESULT="skipped", UTILITY_RESULT="skipped", ORDINARY_RESULT="skipped"), 1)
        for key in ("NATIVE_RESULT", "UTILITY_RESULT", "ORDINARY_RESULT"):
            for result in ("success", "failure", "cancelled", ""):
                with self.subTest(key=key, result=result):
                    values = {"NO_CODE": "true", "NATIVE_RESULT": "skipped", "UTILITY_RESULT": "skipped", "ORDINARY_RESULT": "skipped",
                              key: result}
                    self.assertEqual(self.execute(**values), 1)
        self.assertEqual(self.execute(NO_CODE="True", NATIVE_RESULT="skipped", UTILITY_RESULT="skipped", ORDINARY_RESULT="skipped"), 1)


class PhysicalPreentryReservationTests(unittest.TestCase):
    @contextlib.contextmanager
    def private_root(self):
        root = Path(tempfile.mkdtemp(prefix="buster-preentry-fixture-"))
        self.private_children_quiet = True
        try:
            yield root
        finally:
            if self.private_children_quiet:
                shutil.rmtree(root)
            else:
                print("PREENTRY_PRIVATE_ROOT_RETAINED " + str(root))

    def complete_child(self, child):
        self.private_children_quiet = False
        try:
            output, errors = child.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            # Only this diagnostic child owns its new process group. Preserve
            # its reservation while terminating and reaping the failed fixture.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            _, errors = child.communicate(timeout=5)
            self.private_children_quiet = True
            self.fail("Private pre-entry child exceeded its bound:\n" + errors.decode())
        self.private_children_quiet = True
        # Only fixed dummy context and private paths are traced. Retain each
        # command's observed wall timestamp for both pass and failure.
        print(errors.decode(), end="")
        return output, errors

    def run_private(self, command, cwd=None):
        child = subprocess.Popen(["bash", "-c", command], env=self.environment(), cwd=cwd,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        output, errors = self.complete_child(child)
        return subprocess.CompletedProcess(child.args, child.returncode, output, errors)

    def recipe(self, root):
        # Redirect the fixed parent-directory fsync independently; replacing
        # the /tmp prefix would also corrupt the owner and ACTIVE paths.
        lines = [line[10:] for line in policy.PREENTRY_RESERVATION_SCRIPT]
        self.assertEqual(lines.count("sync /tmp"), 1)
        lines = ["sync " + shlex.quote(str(root)) if line == "sync /tmp" else line
                 for line in lines]
        return "set -euo pipefail\nPS4='+preentry ${EPOCHREALTIME} '\nset -x\n" + "\n".join(
            lines).replace(
                "/tmp/buster-9700x-cleanup-unknown-v1", str(root / "unknown")).replace(
                "/tmp/buster-9700x-cleanup-active-v1", str(root / "active"))

    def environment(self):
        return {"PATH": "/usr/bin:/bin", "BQ_RUN_ID": "200", "BQ_REQUEST_RUN_ID": "100",
                "GITHUB_RUN_ID": "200", "GITHUB_RUN_ATTEMPT": "1", "BQ_HEAD_COMMIT": "a" * 40,
                "GITHUB_SHA": "b" * 40, "GITHUB_REPOSITORY": "buster14a/buster",
                "GITHUB_JOB": "utility"}

    def record(self, root):
        content = (root / "active/owner.tsv").read_text(encoding="ascii")
        rows = [line.split("\t") for line in content.splitlines()]
        self.assertTrue(content.endswith("\n"))
        self.assertEqual([row[0] for row in rows], [
            "schema", "owner_pid", "owner_start_ticks", "boot_id", "request_run_id",
            "executor_run_id", "executor_attempt", "request_head", "repository", "job",
            "policy_revision"])
        self.assertEqual(len(rows), 11)
        self.assertLessEqual(len(content), 4095)
        self.assertEqual((root / "active").stat().st_mode & 0o777, 0o700)
        self.assertEqual((root / "active/owner.tsv").stat().st_mode & 0o777, 0o600)
        self.assertEqual((root / "active/owner.tsv").stat().st_uid, (root / "active").stat().st_uid)
        return dict(rows)

    def test_reservation_syncs_only_the_owner_file_and_directory(self):
        expected = (
            "          sync /tmp/buster-9700x-cleanup-active-v1/owner.tsv",
            "          sync /tmp/buster-9700x-cleanup-active-v1",
            "          sync /tmp",
        )
        self.assertEqual(tuple(line for line in policy.PREENTRY_RESERVATION_SCRIPT
                               if line.lstrip().startswith("sync ")), expected)
        workflow = policy.DIRECT.read_text(encoding="utf-8")
        self.assertEqual(workflow.count(expected[0]), 4)
        self.assertEqual(workflow.count(expected[1] + "\n"), 4)
        self.assertEqual(workflow.count(expected[2] + "\n"), 4)
        self.assertNotIn("sync -f /tmp/buster-9700x-cleanup-active-v1", workflow)

    def test_all_new_physical_entries_claim_before_bootstrap_and_exec(self):
        physical = policy.job_blocks(policy.DIRECT.read_text(encoding="utf-8"))
        for job in ("sampling", "preparation", "utility", "compare"):
            scripts = policy.run_scripts(physical[job])
            script = scripts[-1] if job == "compare" else scripts[2]
            text = "\n".join(script)
            claim = text.index("mkdir -m 0700 /tmp/buster-9700x-cleanup-active-v1")
            bootstrap = text.index("trusted/build.sh compiler_closure driver-path")
            execution = text.index('exec "${drivers[0]}" compiler_profile_qualification')
            self.assertLess(claim, bootstrap)
            self.assertLess(bootstrap, execution)
            self.assertEqual(script.count(policy.PREENTRY_RESERVATION_SCRIPT[0]), 1)
            self.assertNotIn("rm ", text)
            self.assertNotIn("rmdir ", text)
            self.assertNotIn("trap ", text)
        self.assertEqual(policy.run_scripts(physical["compare"])[0], list(policy.CLEANUP_GUARD_SCRIPT))
        self.assertIn(policy.COMPILER_RUN_SCRIPT, policy.run_scripts(physical["compare"]))

    def test_failed_bootstrap_keeps_the_real_claim_and_blocks_next_entry(self):
        with self.private_root() as root:
            first = self.run_private(self.recipe(root) + "\n/bin/false\n")
            self.assertEqual(first.returncode, 1, first.stderr.decode())
            record = self.record(root)
            self.assertEqual(record["schema"], "buster-9700x-preentry-active-v1")
            self.assertEqual(record["request_run_id"], "100")
            self.assertEqual(record["policy_revision"], "b" * 40)
            before = (root / "active/owner.tsv").read_bytes()
            second = self.run_private(self.recipe(root) + "\nprintf next > continued\n", cwd=root)
            self.assertEqual(second.returncode, 1)
            self.assertFalse((root / "continued").exists())
            self.assertEqual((root / "active/owner.tsv").read_bytes(), before)

    def test_actual_exec_preserves_the_record_owner_pid_and_start_ticks(self):
        verify = r"""
declare -A record
while IFS=$'\t' read -r key value; do record["$key"]="$value"; done < "$1/active/owner.tsv"
read -r current_stat < "/proc/$$/stat"
read -r -a fields <<< "${current_stat##*) }"
[[ "${record[owner_pid]}" == "$$" && "${record[owner_start_ticks]}" == "${fields[19]}" ]]
[[ "${record[executor_run_id]}" == "$GITHUB_RUN_ID" && "${record[job]}" == "$GITHUB_JOB" ]]
"""
        with self.private_root() as root:
            command = self.recipe(root) + "\nexec /bin/bash -euo pipefail -c " + \
                shlex.quote(verify) + " verify " + shlex.quote(str(root)) + "\n"
            child = subprocess.Popen(["bash", "-c", command], env=self.environment(),
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            _, errors = self.complete_child(child)
            self.assertEqual(child.returncode, 0, errors.decode())
            self.assertEqual(self.record(root)["owner_pid"], str(child.pid))

    def test_hard_killed_preentry_remains_consumed_without_shell_cleanup(self):
        with self.private_root() as root:
            child = subprocess.Popen(["bash", "-c", self.recipe(root) + '\nkill -KILL "$$"\n'],
                                     env=self.environment(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            self.complete_child(child)
            self.assertEqual(child.returncode, -9)
            self.assertEqual(self.record(root)["owner_pid"], str(child.pid))
            refusal = self.run_private(self.recipe(root))
            self.assertEqual(refusal.returncode, 1)
            self.assertTrue((root / "active/owner.tsv").is_file())


if __name__ == "__main__":
    unittest.main()
