#!/usr/bin/env python3
"""Deterministic tests for the matched-cohort Zig cache policy."""

import hashlib
import json
import re
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import ci_zig_cache


class ZigCachePolicyTests(unittest.TestCase):
    manifest_hash = "1" * 64

    def policy(self, *, event="workflow_dispatch", ref="refs/heads/main",
               mode="ordinary", namespace="", shard="release"):
        return ci_zig_cache.resolve_policy(
            event,
            ref,
            "main",
            mode,
            namespace,
            "Linux",
            "X64",
            "x86_64-linux",
            self.manifest_hash,
            shard,
        )

    def test_ordinary_events_keep_the_existing_exact_key_and_save_policy(self):
        expected = "zig-archive-v1-Linux-X64-x86_64-linux-" + self.manifest_hash
        cases = (
            ("pull_request", "refs/pull/7/merge", False),
            ("push", "refs/heads/main", True),
            ("push", "refs/tags/v1", False),
            ("merge_group", "refs/heads/gh-readonly-queue/main/pr-7", False),
            ("workflow_dispatch", "refs/heads/main", False),
        )
        for event, ref, saves in cases:
            for shard in ("release", "checks"):
                with self.subTest(event=event, ref=ref, shard=shard):
                    policy = self.policy(event=event, ref=ref, shard=shard)
                    self.assertEqual(policy.key, expected)
                    self.assertEqual(policy.save, saves)
                    self.assertFalse(policy.require_hit)
                    self.assertFalse(policy.publication_proof_required)

    def test_cohort_modes_are_dispatch_only(self):
        for event in ("pull_request", "push", "merge_group"):
            for mode in ("prime", "read"):
                with self.subTest(event=event, mode=mode):
                    with self.assertRaisesRegex(ValueError, "workflow_dispatch"):
                        self.policy(
                            event=event,
                            mode=mode,
                            namespace="issue709-control-v1",
                        )

    def test_modes_and_namespaces_fail_closed(self):
        invalid = (
            "", "-leading", "trailing-", "double--hyphen", "Uppercase",
            "under_score", "slash/value", "space value", "a" * 49,
        )
        for namespace in invalid:
            with self.subTest(namespace=namespace):
                with self.assertRaises(ValueError):
                    self.policy(mode="prime", namespace=namespace)
        accepted = "a" * ci_zig_cache.NAMESPACE_MAX_LENGTH
        self.assertEqual(
            self.policy(mode="read", namespace=accepted).namespace,
            accepted,
        )
        with self.assertRaisesRegex(ValueError, "ordinary.*empty"):
            self.policy(namespace="issue709-control-v1")
        with self.assertRaisesRegex(ValueError, "unsupported"):
            self.policy(mode="mutable", namespace="issue709-control-v1")

    def test_arm_keys_differ_only_by_namespace_suffix(self):
        control = self.policy(mode="prime", namespace="issue709-control-v1")
        candidate = self.policy(mode="prime", namespace="issue709-candidate-v1")
        base = "zig-archive-v1-Linux-X64-x86_64-linux-" + self.manifest_hash
        self.assertEqual(control.key, base + "-cohort-issue709-control-v1")
        self.assertEqual(candidate.key, base + "-cohort-issue709-candidate-v1")
        self.assertNotEqual(control.key, candidate.key)

    def test_prime_has_one_publisher_and_read_never_saves(self):
        release = self.policy(
            mode="prime",
            namespace="issue709-control-v1",
            shard="release",
        )
        checks = self.policy(
            mode="prime",
            namespace="issue709-control-v1",
            shard="checks",
        )
        read = self.policy(mode="read", namespace="issue709-control-v1")
        self.assertTrue(release.save)
        self.assertTrue(release.publication_proof_required)
        self.assertFalse(checks.save)
        self.assertFalse(checks.publication_proof_required)
        self.assertFalse(read.save)
        self.assertTrue(read.require_hit)

    def test_restore_gate_rejects_miss_and_substitution(self):
        key = self.policy(mode="read", namespace="issue709-control-v1").key
        self.assertTrue(
            ci_zig_cache.require_restored_population(
                "read", key, "true", key, key,
            )
        )
        with self.assertRaisesRegex(ValueError, "exact frozen-population hit"):
            ci_zig_cache.require_restored_population(
                "read", key, "false", key, "",
            )
        with self.assertRaisesRegex(ValueError, "unexpected key"):
            ci_zig_cache.require_restored_population(
                "read", key, "true", key, key + "-other",
            )
        with self.assertRaisesRegex(ValueError, "primary key"):
            ci_zig_cache.require_restored_population(
                "prime", key, "false", "", "",
            )


class ZigCacheEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.payload = b"verified cohort archive"
        manifest_data = json.loads((ROOT / ".github/zig.json").read_text())
        manifest_data["sha256"]["x86_64-linux"] = hashlib.sha256(self.payload).hexdigest()
        manifest_data["size"]["x86_64-linux"] = len(self.payload)
        self.manifest = self.root / "zig.json"
        self.manifest.write_text(json.dumps(manifest_data), encoding="utf-8")
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.evidence_path = self.root / "zig-cache.json"
        self.summary_path = self.root / "summary.md"
        self.key = ""

    def make_evidence(self, mode, namespace="issue709-control-v1", shard="release"):
        if mode == "ordinary":
            namespace = ""
        policy = ci_zig_cache.resolve_policy(
            "workflow_dispatch",
            "refs/heads/control",
            "main",
            mode,
            namespace,
            "Linux",
            "X64",
            "x86_64-linux",
            "1" * 64,
            shard,
        )
        self.key = policy.key
        evidence = ci_zig_cache.create_evidence(
            policy,
            self.manifest,
            event_name="workflow_dispatch",
            ref="refs/heads/control",
            default_branch="main",
            repository="buster14a/buster",
            source_sha="2" * 40,
            run_id="123",
            run_attempt="1",
            shard=shard,
            runner_label="ubuntu-26.04",
            runner_os="Linux",
            runner_arch="X64",
            target="x86_64-linux",
            manifest_hash="1" * 64,
        )
        ci_zig_cache._write_json(self.evidence_path, evidence)
        return evidence

    def record(self, **overrides):
        arguments = {
            "restore_outcome": "success",
            "cache_hit": "false",
            "primary_key": self.key,
            "matched_key": "",
            "install_outcome": "success",
            "save_outcome": "success",
            "proof_restore_outcome": "success",
            "proof_cache_hit": "true",
            "proof_primary_key": self.key,
            "proof_matched_key": self.key,
            "summary_path": self.summary_path,
        }
        arguments.update(overrides)
        return ci_zig_cache.record_evidence(
            self.evidence_path,
            self.manifest,
            self.cache,
            **arguments,
        )

    def test_hit_is_reported_without_a_publication_claim(self):
        self.make_evidence("read")
        evidence = self.record(
            cache_hit="true",
            matched_key=self.key,
            save_outcome="skipped",
            proof_restore_outcome="skipped",
            proof_cache_hit="",
            proof_primary_key="",
            proof_matched_key="",
        )
        self.assertEqual(evidence["restore"]["cache_hit"], "hit")
        self.assertEqual(evidence["restore"]["matched_key"], self.key)
        self.assertEqual(evidence["publication"]["state"], "existing-hit-verified")
        self.assertEqual(evidence["publication"]["proof_cache_hit"], "not-run")
        self.assertTrue(evidence["usable_population"])

    def test_prime_miss_requires_digest_verified_exact_re_restore(self):
        self.make_evidence("prime")
        (self.cache / "archive").write_bytes(self.payload)
        evidence = self.record()
        self.assertEqual(evidence["restore"]["cache_hit"], "miss")
        self.assertEqual(evidence["publication"]["state"], "published-and-verified")
        self.assertEqual(evidence["publication"]["proof_matched_key"], self.key)
        self.assertTrue(evidence["publication"]["archive_verified"])
        self.assertTrue(evidence["usable_population"])
        retained = json.loads(self.evidence_path.read_text())
        self.assertEqual(retained, evidence)
        self.assertIn("published-and-verified", self.summary_path.read_text())

    def test_prime_miss_retains_unverified_publication_failure(self):
        self.make_evidence("prime")
        with self.assertRaisesRegex(ValueError, "frozen-population hit"):
            self.record(
                proof_cache_hit="false",
                proof_matched_key="",
            )
        retained = json.loads(self.evidence_path.read_text())
        self.assertEqual(retained["publication"]["state"], "publication-unverified")
        self.assertEqual(retained["publication"]["proof_cache_hit"], "miss")
        self.assertFalse(retained["usable_population"])

    def test_substituted_publication_key_is_rejected(self):
        self.make_evidence("prime")
        with self.assertRaisesRegex(ValueError, "unexpected key"):
            self.record(proof_matched_key=self.key + "-other")
        retained = json.loads(self.evidence_path.read_text())
        self.assertEqual(retained["publication"]["state"], "publication-unverified")

    def test_corrupt_publication_is_not_usable(self):
        self.make_evidence("prime")
        (self.cache / "archive").write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            self.record()
        retained = json.loads(self.evidence_path.read_text())
        self.assertEqual(retained["publication"]["state"], "publication-corrupt")
        self.assertFalse(retained["usable_population"])

    def test_read_miss_retains_failure_and_never_saves(self):
        initial = self.make_evidence("read")
        self.assertFalse(initial["policy"]["save_authorized"])
        with self.assertRaisesRegex(ValueError, "frozen exact key"):
            self.record(
                install_outcome="skipped",
                save_outcome="skipped",
                proof_restore_outcome="skipped",
                proof_cache_hit="",
                proof_primary_key="",
                proof_matched_key="",
            )
        retained = json.loads(self.evidence_path.read_text())
        self.assertEqual(retained["publication"]["state"], "frozen-population-miss")
        self.assertEqual(retained["save"]["step_outcome"], "not-requested")
        self.assertFalse(retained["usable_population"])

    def test_non_owner_prime_miss_records_no_publication(self):
        self.make_evidence("prime", shard="checks")
        evidence = self.record(
            save_outcome="skipped",
            proof_restore_outcome="skipped",
            proof_cache_hit="",
            proof_primary_key="",
            proof_matched_key="",
        )
        self.assertEqual(evidence["publication"]["state"], "non-owner-miss-not-published")
        self.assertFalse(evidence["save"]["authorized"])
        self.assertFalse(evidence["usable_population"])

    def test_ordinary_miss_does_not_claim_a_usable_population(self):
        self.make_evidence("ordinary")
        evidence = self.record(
            save_outcome="skipped",
            proof_restore_outcome="skipped",
            proof_cache_hit="",
            proof_primary_key="",
            proof_matched_key="",
        )
        self.assertEqual(evidence["publication"]["state"], "ordinary-miss-not-published")
        self.assertFalse(evidence["usable_population"])


class ZigCacheWorkflowTests(unittest.TestCase):
    def test_workflow_uses_bounded_ref_derived_cache_controls(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        events = text.split("on:\n", 1)[1].split("\n\n", 1)[0]
        self.assertNotIn("zig_cache_mode:", events)
        self.assertNotIn("zig_cache_namespace:", events)

        self.assertIn('if [[ "$GITHUB_EVENT_NAME" == workflow_dispatch ]]', text)
        self.assertIn("refs/heads/ci-cohort-prime-*)", text)
        self.assertIn("refs/heads/ci-cohort-read-*)", text)
        self.assertIn(
            "namespace=${GITHUB_REF#refs/heads/ci-cohort-prime-}",
            text,
        )
        self.assertIn(
            "namespace=${GITHUB_REF#refs/heads/ci-cohort-read-}",
            text,
        )
        self.assertIn('--mode "$mode"', text)
        self.assertIn('--namespace "$namespace"', text)
        self.assertIn("tools/ci_zig_cache.py check-restore", text)
        self.assertIn("tools/ci_zig_cache.py record", text)
        self.assertIn("key: ${{ steps.zig_cache_policy.outputs.key }}", text)
        self.assertNotIn("restore-keys:", text)
        self.assertEqual(
            text.count("path: ${{ runner.temp }}/zig-archive/archive"),
            3,
        )
        self.assertIn("steps.zig_cache_policy.outputs.save == 'true'", text)
        self.assertIn(
            "steps.zig_cache_policy.outputs.publication_proof_required == 'true'",
            text,
        )
        self.assertIn('rm -f "$RUNNER_TEMP/zig-archive/archive"', text)
        self.assertIn("zig-cache.json", text)
        concurrency = text.split("concurrency:", 1)[1].split("permissions:", 1)[0]
        self.assertIn("github.run_id", concurrency)

        cache_section = text.split("# Exact-key archive cache only:", 1)[1].split(
            "- name: Application compilers", 1
        )[0]
        cache_paths = [
            line.strip()
            for line in cache_section.splitlines()
            if line.strip().startswith("path:")
        ]
        self.assertEqual(
            cache_paths,
            ["path: ${{ runner.temp }}/zig-archive/archive"] * 3,
        )
        for forbidden in ("CMakeCache", "build/", "result.json"):
            self.assertNotIn(forbidden, cache_section)

    def test_desktop_lifecycle_names_every_cache_prerequisite(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        desktop = text.split("\n  test:", 1)[1].split("\n  native:", 1)[0]

        workflow_tools = desktop.split(
            "- name: Workflow tool regression tests", 1
        )[1].split("- name: Bootstrap wrapper regression tests", 1)[0]
        self.assertIn("run_suite tools/ci_zig_cache_test.py zig-cache-policy-test.log", workflow_tools)

        bootstrap = desktop.split(
            "- name: Bootstrap wrapper regression tests", 1
        )[1].split("- name: Install mold", 1)[0]
        self.assertIn(
            "if: ${{ !cancelled() && steps.checkout.outcome == 'success' && "
            "steps.zig_cache_policy.outcome == 'success' }}",
            bootstrap,
        )
        self.assertIn(
            "tests/bootstrap_wrapper_test.py BootstrapWrapperTests -v",
            bootstrap,
        )

        required = next(
            line.strip()
            for line in desktop.splitlines()
            if line.strip().startswith("BUSTER_CI_REQUIRED:")
        )
        lists = re.findall(r"'([^']*workflow_tools[^']*)'", required)
        self.assertEqual(
            lists,
            [
                "zig_cache_policy workflow_tools bootstrap_wrappers "
                "zig_cache_gate zig zig_cache_evidence combinations_unix",
                "zig_cache_policy workflow_tools bootstrap_wrappers "
                "zig_cache_gate zig zig_cache_evidence combinations_windows",
            ],
        )

    def test_workflow_uses_split_exact_cache_actions(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        cache = text.split("# Exact-key archive cache only:", 1)[1].split(
            "- name: Application compilers", 1
        )[0]
        restore = (
            "uses: actions/cache/restore@"
            "0057852bfaa89a56745cba8c7296529d2fc39830"
        )
        save = (
            "uses: actions/cache/save@"
            "0057852bfaa89a56745cba8c7296529d2fc39830"
        )
        self.assertEqual(cache.count(restore), 2)
        self.assertEqual(cache.count(save), 1)
        self.assertNotIn("restore-keys:", cache)

    def test_no_second_workflow_owns_the_cohort_interface(self):
        owners = []
        for path in (ROOT / ".github/workflows").glob("*.yml"):
            text = path.read_text(encoding="utf-8")
            if "refs/heads/ci-cohort-prime-*" in text:
                owners.append(path.name)
        self.assertEqual(owners, ["ci.yml"])

    def test_documented_709_sequence_and_evidence_are_concrete(self):
        text = (ROOT / "docs/ci-workflow-audit.md").read_text(encoding="utf-8")
        for required in (
            "ci-cohort-prime-issue709-control-v1",
            "ci-cohort-read-issue709-control-v1",
            "ci-cohort-prime-issue709-candidate-v1",
            "ci-cohort-read-issue709-candidate-v1",
            "`prime`",
            "`read`",
            "`cmake_profile=false`",
            "zig-cache.json",
            "effective_key",
            "published-and-verified",
            "existing-hit-verified",
            "at least three",
        ):
            with self.subTest(required=required):
                self.assertIn(required, text)


if __name__ == "__main__":
    unittest.main()
