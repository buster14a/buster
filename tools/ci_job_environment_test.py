#!/usr/bin/env python3
"""Pure environment, atomic custody and exact workflow wiring controls."""
import ast
import contextlib
import io
import json
import os
from pathlib import Path
import re
import stat
import tempfile
import unittest
from unittest import mock

import ci_job_environment as evidence

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = {"BUSTER_CI_CONDITIONS_EVIDENCE": "1", "GITHUB_REPOSITORY": "buster14a/buster",
               "GITHUB_SHA": "a" * 40, "GITHUB_RUN_ID": "123", "GITHUB_RUN_ATTEMPT": "1", "GITHUB_JOB": "lint",
               "BUSTER_CI_RUNNER": "ubuntu-26.04", "RUNNER_OS": "Linux", "RUNNER_ARCH": "X64",
               "RUNNER_NAME": "actual-instance-7", "ImageOS": "ubuntu26", "ImageVersion": "20260927.149.1",
               "GITHUB_WORKFLOW": "CI", "GITHUB_WORKFLOW_REF": "buster14a/buster/.github/workflows/ci.yml@refs/heads/main",
               "GITHUB_WORKFLOW_SHA": "b" * 40}


@unittest.skipUnless(os.name == "posix" and hasattr(os, "O_NOFOLLOW"), "receipt publication callers run on Ubuntu")
class JobEnvironmentTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "receipts/job-environment.json"

    def invoke(self, environment=None, arguments=None):
        with mock.patch.object(evidence.os, "environ", ENVIRONMENT if environment is None else environment), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = evidence.main(["--output", str(self.output)] if arguments is None else arguments)
        return result

    def test_disabled_gate_has_zero_parsing_environment_census_and_filesystem_work(self):
        for flag in (None, "", "0", "true", "01", " 1", "1 "):
            environment = {} if flag is None else {"BUSTER_CI_CONDITIONS_EVIDENCE": flag}
            with self.subTest(flag=flag), mock.patch.dict(os.environ, environment, clear=True), \
                 mock.patch.object(evidence.argparse, "ArgumentParser", side_effect=AssertionError("parsed")), \
                 mock.patch.object(evidence, "collect", side_effect=AssertionError("collected")), \
                 mock.patch.object(evidence, "write_receipt", side_effect=AssertionError("wrote")), \
                 mock.patch.object(evidence.os, "open", side_effect=AssertionError("opened")), \
                 mock.patch.object(evidence.os, "mkdir", side_effect=AssertionError("mkdir")):
                self.assertEqual(evidence.main(["malformed"]), 0)
        self.assertFalse(self.output.exists())

    def test_complete_receipt_keeps_exact_binding_and_only_whitelisted_environment(self):
        environment = {**ENVIRONMENT, "GITHUB_TOKEN": "secret-token", "AWS_SECRET_ACCESS_KEY": "secret-key", "UNRELATED": "private-value"}
        self.assertEqual(self.invoke(environment), 0)
        raw = self.output.read_bytes()
        receipt = json.loads(raw)
        self.assertEqual(receipt["schema"], evidence.SCHEMA)
        self.assertEqual(receipt["status"], "complete")
        self.assertEqual({key: receipt[key] for key, _ in evidence.BINDINGS},
                         {key: environment[name] for key, name in evidence.BINDINGS})
        self.assertEqual(receipt["environment"], {key: environment[key] for key in evidence.ENV_KEYS})
        self.assertEqual(receipt["invalid_bindings"], [])
        self.assertEqual(receipt["unavailable_fields"], [])
        self.assertNotIn("job_id", receipt)
        self.assertNotIn("workflow_blob_sha", receipt)
        self.assertNotIn("toolchains", receipt)
        self.assertNotIn("caches", receipt)
        self.assertFalse(any(value.encode() in raw for value in ("secret-token", "secret-key", "private-value")))
        self.assertTrue(stat.S_ISREG(self.output.lstat().st_mode))
        self.assertEqual(self.output.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.output.stat().st_nlink, 1)
        self.assertLessEqual(len(raw), evidence.MAX_RECEIPT_BYTES)

    def test_each_missing_or_malformed_binding_is_retained_incomplete_not_complete(self):
        invalid = {"GITHUB_REPOSITORY": "no-owner", "GITHUB_SHA": "A" * 40,
                   "GITHUB_RUN_ID": "0", "GITHUB_RUN_ATTEMPT": "01", "GITHUB_JOB": "job with spaces"}
        for key, bad in invalid.items():
            for value in (None, "", bad):
                environment = dict(ENVIRONMENT)
                if value is None:
                    environment.pop(key)
                else:
                    environment[key] = value
                with self.subTest(key=key, value=value):
                    self.assertEqual(self.invoke(environment), 1)
                    receipt = json.loads(self.output.read_bytes())
                    self.assertEqual(receipt["status"], "incomplete")
                    binding = next(name for name, field in evidence.BINDINGS if field == key)
                    self.assertEqual(receipt[binding], value)
                    self.assertIn(binding, receipt["invalid_bindings"])
                    self.output.unlink()

    def test_missing_blank_padded_unknown_and_actual_images_roundtrip_without_inference(self):
        for key in ("ImageOS", "ImageVersion", "BUSTER_CI_RUNNER", "RUNNER_NAME"):
            for value in (None, "", "  ", "\t\n ", "unknown", " unknown ", "UnAvAiLaBlE", " missing ",
                          "pending", " Pending ", "PeNdInG"):
                environment = dict(ENVIRONMENT)
                if value is None:
                    environment.pop(key)
                else:
                    environment[key] = value
                with self.subTest(key=key, value=value):
                    self.assertEqual(self.invoke(environment), 0)
                    receipt = json.loads(self.output.read_bytes())
                    self.assertEqual(receipt["environment"][key], value)
                    self.assertEqual(receipt["status"], "incomplete")
                    self.assertIn(key, receipt["unavailable_fields"])
                    self.output.unlink()
        environment = {**ENVIRONMENT, "ImageVersion": "  actual-version  "}
        self.assertEqual(evidence.collect(environment)["environment"]["ImageVersion"], "  actual-version  ")

    def test_frozen_consumer_sentinels_require_complete_receipts_before_image_projection(self):
        tree = ast.parse((ROOT / "tools/ci_checks_qualification.py").read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "known")
        namespace = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), "frozen qualifier known", "exec"), namespace)
        known = namespace["known"]
        for sentinel in ("unknown", "missing", "unavailable", "pending"):
            self.assertFalse(known(sentinel))
            padded = " " + sentinel.title() + " "
            # The frozen consumer keeps its old exact string semantics. The
            # receipt's explicit status must prevent the padded value reaching
            # conditions as an apparently known observed image.
            self.assertTrue(known(padded))
            for key in ("ImageOS", "ImageVersion"):
                with self.subTest(key=key, value=padded):
                    receipt = evidence.collect({**ENVIRONMENT, key: padded})
                    self.assertEqual(receipt["status"], "incomplete")
                    self.assertEqual(receipt["environment"][key], padded)
                    self.assertIn(key, receipt["unavailable_fields"])

    def test_workflow_commit_and_requested_label_are_separate_actual_provenance(self):
        receipt = evidence.collect(ENVIRONMENT)
        self.assertNotEqual(receipt["source_revision"], receipt["environment"]["GITHUB_WORKFLOW_SHA"])
        self.assertNotEqual(receipt["environment"]["BUSTER_CI_RUNNER"], receipt["environment"]["RUNNER_NAME"])
        receipt = evidence.collect({**ENVIRONMENT, "GITHUB_WORKFLOW_SHA": "not-a-commit"})
        self.assertEqual(receipt["status"], "incomplete")
        self.assertIn("GITHUB_WORKFLOW_SHA", receipt["unavailable_fields"])

    def test_malformed_utf8_controls_and_utf8_byte_limits_fail_before_output_io(self):
        for value in ("\ud800", "line\nsecret", "x\0secret", "x" * 513, "😀" * 129):
            environment = {**ENVIRONMENT, "ImageVersion": value}
            with self.subTest(value=repr(value)), mock.patch.object(evidence, "write_receipt", side_effect=AssertionError("wrote")):
                self.assertEqual(self.invoke(environment), 1)
        self.assertFalse(self.output.exists())

    def test_aggregate_serialized_bound_precedes_output_io(self):
        environment = {**ENVIRONMENT, **{key: "\t" * 512 for key in evidence.ENV_KEYS}}
        with mock.patch.object(evidence, "write_receipt", side_effect=AssertionError("wrote")):
            self.assertEqual(self.invoke(environment), 1)
        with mock.patch.object(evidence.os, "open", side_effect=AssertionError("opened")), \
             self.assertRaisesRegex(evidence.EvidenceError, "byte bound"):
            evidence.write_receipt(str(self.output), b"x" * (evidence.MAX_RECEIPT_BYTES + 1))

    def test_unknown_arguments_and_missing_output_do_not_write(self):
        for args in (["--unknown", "x"], []):
            with self.subTest(args=args), self.assertRaises(SystemExit), \
                 mock.patch.object(evidence, "write_receipt", side_effect=AssertionError("wrote")):
                self.invoke(arguments=args)

    def test_output_paths_refuse_traversal_relative_nonjson_and_oversize_without_open(self):
        for value in ("relative.json", str(self.root / "../out.json"), str(self.root / "./x") + "/../out.json",
                      str(self.root / "out.txt"), "/" + "x" * 2048 + ".json", "/tmp/\0out.json"):
            with self.subTest(value=value), mock.patch.object(evidence.os, "open", side_effect=AssertionError("opened")), \
                 self.assertRaises(evidence.EvidenceError):
                evidence.write_receipt(value, b"{}\n")

    def test_existing_regular_symlink_directory_and_fifo_targets_are_not_replaced(self):
        self.output.parent.mkdir()
        external = self.root / "external.json"
        external.write_bytes(b"outside")
        for kind in ("regular", "symlink", "directory", "fifo"):
            with self.subTest(kind=kind):
                if kind == "regular":
                    self.output.write_bytes(b"original")
                elif kind == "symlink":
                    self.output.symlink_to(external)
                elif kind == "directory":
                    self.output.mkdir()
                else:
                    os.mkfifo(self.output)
                before = self.output.lstat()
                self.assertEqual(self.invoke(), 1)
                self.assertEqual(self.output.lstat().st_ino, before.st_ino)
                self.assertEqual(external.read_bytes(), b"outside")
                self.output.rmdir() if kind == "directory" else self.output.unlink()

    def test_parent_symlink_and_parent_swap_do_not_follow_outside_destination(self):
        outside = self.root / "outside"
        outside.mkdir()
        self.output.parent.symlink_to(outside, target_is_directory=True)
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((outside / self.output.name).exists())
        self.output.parent.unlink()
        self.output.parent.mkdir()
        real_open = evidence.os.open
        def swapping_open(path, flags, *args, **kwargs):
            if path == "receipts":
                self.output.parent.rename(self.root / "old-parent")
                self.output.parent.symlink_to(outside, target_is_directory=True)
            return real_open(path, flags, *args, **kwargs)
        with mock.patch.object(evidence.os, "open", side_effect=swapping_open):
            self.assertEqual(self.invoke(), 1)
        self.assertFalse((outside / self.output.name).exists())

    def test_atomic_no_clobber_race_and_failed_publish_clean_only_owned_temporary(self):
        self.output.parent.mkdir()
        real_link = evidence.os.link
        def racing_link(*args, **kwargs):
            self.output.write_bytes(b"racer")
            return real_link(*args, **kwargs)
        with mock.patch.object(evidence.os, "link", side_effect=racing_link):
            self.assertEqual(self.invoke(), 1)
        self.assertEqual(self.output.read_bytes(), b"racer")
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])
        self.output.unlink()
        with mock.patch.object(evidence.os, "link", side_effect=OSError("synthetic failure")):
            self.assertEqual(self.invoke(), 1)
        self.assertEqual(list(self.output.parent.iterdir()), [])

    def test_exclusive_temp_collision_preserves_foreign_file(self):
        self.output.parent.mkdir()
        foreign = self.output.parent / ".ci-job-environment-fixed.tmp"
        foreign.write_bytes(b"foreign")
        with mock.patch.object(evidence.secrets, "token_hex", return_value="fixed"):
            self.assertEqual(self.invoke(), 1)
        self.assertEqual(foreign.read_bytes(), b"foreign")
        self.assertFalse(self.output.exists())


class JobEnvironmentWorkflowTests(unittest.TestCase):
    def test_exact_six_roles_collect_after_checkout_only_under_unchanged_optin(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        roles = dict(re.findall(r"^  ([a-z_]+):\n(.*?)(?=^  [a-z_]+:\n|\Z)", workflow, re.M | re.S))
        selected = {name for name, body in roles.items() if "Retain actual job environment" in body}
        self.assertEqual(selected, {"lint", "queue_lint", "reuse", "uefi", "analyzer", "complete"})
        for role in selected:
            body = roles[role]
            step = body.split("      - name: Retain actual job environment\n", 1)[1].split("      - name:", 1)[0]
            with self.subTest(role=role):
                self.assertLess(body.index("actions/checkout@"), body.index("Retain actual job environment"))
                self.assertEqual(body.count("        id: checkout\n"), 1)
                self.assertRegex(body, r"      - uses: actions/checkout@[^\n]+\n        id: checkout\n"
                                      r"(?:        [^\n]*\n|          [^\n]*\n)*"
                                      r"      - name: Retain actual job environment\n")
                guard = re.search(r"^        if: (.+)$", step, re.M).group(1)
                self.assertEqual(guard, "${{ always() && steps.checkout.outcome == 'success' && env.BUSTER_CI_CONDITIONS_EVIDENCE == '1' }}")
                # Inspect the actual workflow predicate: always() keeps failure
                # diagnostics available, but failed/skipped/cancelled checkouts
                # cannot execute a producer against partial or stale source.
                outcomes = re.findall(r"steps\.checkout\.outcome == '([^']+)'", guard)
                optins = re.findall(r"env\.BUSTER_CI_CONDITIONS_EVIDENCE == '([^']+)'", guard)
                self.assertEqual(outcomes, ["success"])
                self.assertEqual(optins, ["1"])
                for outcome in ("success", "failure", "cancelled", "skipped"):
                    for optin in ("1", "0", "", None):
                        self.assertEqual(outcome == outcomes[0] and optin == optins[0],
                                         outcome == "success" and optin == "1")
                self.assertIn("    runs-on: ubuntu-26.04\n", body)
                self.assertIn("          BUSTER_CI_RUNNER: ubuntu-26.04\n", step)
                self.assertIn("          BUSTER_CI_CONDITIONS_EVIDENCE: ${{ env.BUSTER_CI_CONDITIONS_EVIDENCE }}\n", step)
                self.assertIn("python3 -B tools/ci_job_environment.py --output", step)
                self.assertNotIn("GH_TOKEN", step)
                self.assertNotIn("continue-on-error", step)
        gate = re.search(r"^  BUSTER_CI_CONDITIONS_EVIDENCE: (.+)$", workflow, re.M).group(1)
        self.assertIn("github.event_name == 'workflow_dispatch'", gate)
        for prefix in ("codex/ci-checks-", "codex/2120-evidence-v2-"):
            for variant in ("combined-overlap", "combined-all-builds", "split-overlap"):
                self.assertIn("refs/heads/" + prefix + variant, gate)
        self.assertEqual(gate.count("github.ref =="), 6)
        self.assertIn("github.event_name == 'push' && github.ref == 'refs/heads/main'", roles["reuse"])

    def test_receipts_use_existing_failure_retained_role_artifacts_and_complete_inventory(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        expected = (("Retain lint log", "${{ runner.temp }}/job-environment.json", "if: ${{ !cancelled() }}"),
                    ("Retain main CI reuse decision", "${{ runner.temp }}/job-environment.json", "if: always()"),
                    ("Retain firmware execution evidence", "${{ runner.temp }}/buster-ci/job-environment.json", "if: always()"),
                    ("Retain analyzer inventory, results and measurements", "${{ runner.temp }}/buster-analyzer/", "if: always()"),
                    ("Retain desktop partition inventory", "${{ runner.temp }}/job-environment.json", "if: ${{ always() }}"))
        for name, path, guard in expected:
            block = workflow.split("      - name: " + name + "\n", 1)[1].split("      - name:", 1)[0]
            with self.subTest(role=name):
                self.assertIn(path, block)
                self.assertIn(guard, block)
        self.assertIn("          python3 -B tools/ci_job_environment_test.py -v\n", workflow)
        self.assertIn("            tools/ci_android_sdk_test.py=android-sdk-installer-test.log\n", workflow)
        complete = workflow.split("\n  complete:\n", 1)[1]
        self.assertIn("needs: [lint, queue_lint, test, native, mobile, uefi, analyzer, reuse]", complete)
        self.assertIn("${{ runner.temp }}/desktop-partitions.json", complete)
        self.assertIn("${{ runner.temp }}/main-ci-reuse-finish.json", complete)


if __name__ == "__main__":
    unittest.main()
