#!/usr/bin/env python3
"""Offline tests for the fail-closed native-retirement binding validator."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "native_retirement_performance_binding",
    ROOT / "tools" / "native_retirement_performance_binding.py")
binding = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(binding)


class BindingTests(unittest.TestCase):
    def make_record(self):
        contents = {}

        def artifact(path, data):
            if isinstance(data, str):
                data = data.encode("utf-8")
            contents[path] = data
            return {"path": path, "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()}

        rows = []
        row_number = 0
        for target in ("aarch64-unknown-linux-gnu", "x86_64-unknown-linux-gnu"):
            for frontend in binding.FRONTENDS:
                for pic in binding.PIC:
                    for allocator in binding.ALLOCATORS:
                        stage = binding.STAGES[row_number % len(binding.STAGES)]
                        rows.append({
                            "row": row_number,
                            "identity": {
                                "fixture": "tests/basic_c_operations.c",
                                "target": target,
                                "target_abi": "sysv",
                                "cpu": "baseline",
                                "cpu_features": "baseline",
                                "allocator": allocator,
                                "frontend_lowering": frontend,
                                "PIC": pic,
                                "fixture_recipe": "default",
                                "compile_obligation": "strict-object-zero-fallback",
                                "link_obligation": "native-link",
                                "execution_obligation": "native-execution",
                                "artifact_stage": stage,
                            },
                            "eligibility": {
                                "compiler_wall_time": True,
                                "compiler_peak_rss": True,
                                "generated_code_bytes": True,
                                "generated_runtime": stage != "object",
                                "runtime_oracle": (
                                    "independent-native-executable-oracle"
                                    if stage != "object" else "not-applicable"),
                                "code_section": "deterministic-code-section",
                            },
                        })
                        row_number += 1
        rows_record = {
            "schema": binding.ROW_SCHEMA,
            "version": binding.ROW_VERSION,
            "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
            "rows": rows,
        }
        rows_data = (json.dumps(rows_record, sort_keys=True, separators=(",", ":"),
                                ensure_ascii=False) + "\n").encode("utf-8")
        parsed_rows = binding._performance_rows(rows_data)

        support_files = []
        for name in binding.SUPPORT_FILE_ROLES:
            data = rows_data if name == "performance_rows" else (name + "\n")
            item = artifact(f"support/{name}.data", data)
            item["name"] = name
            support_files.append(item)

        baseline_snapshot = artifact("subjects/baseline-source.tar", "baseline source\n")
        baseline_binary = artifact("subjects/baseline-ide", "baseline binary\n")
        baseline_receipt = artifact("subjects/baseline-build.json", "baseline receipt\n")
        candidate_snapshot = artifact("subjects/candidate-source.tar", "candidate source\n")
        candidate_binary = artifact("subjects/candidate-ide", "candidate binary\n")
        candidate_receipt = artifact("subjects/candidate-build.json", "candidate receipt\n")
        compiler_binary = artifact("producer/clang", "trusted clang\n")
        resource_directory = artifact("producer/resource.tar", "resource closure\n")
        build_configuration = artifact("producer/build-config.json", "release unity\n")
        build_flags = artifact("producer/build-flags.txt", "-O2 -Werror\n")
        harness_binary = artifact("measurement/harness", "native harness\n")
        statistics = artifact("measurement/statistics.py", "paired statistics\n")
        service_recipe = artifact("execution/service-recipe", "whole-job recipe\n")
        profile_descriptor = artifact("execution/profile.json", "9700x profile\n")
        qualification = artifact("execution/qualification.json", "qualified host\n")
        aa_admission = artifact("execution/aa-admission.json", "A/A admitted\n")
        lease_receipt = artifact("execution/lease-receipt.json", "supervisor lease\n")
        contract_source = artifact("contract/native-retirement-performance-contract.md",
                                   "bound contract source\n")

        closure_artifacts = {}
        for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
            closure_artifacts[kind] = artifact(f"work/{kind}.manifest", kind + " closure\n")
        requested_work_items = [
            {"kind": kind, "name": kind, "artifact": closure_artifacts[kind]}
            for kind in sorted(closure_artifacts)
        ]
        closure = {
            "manifest_sha256": support_files[
                binding.SUPPORT_FILE_ROLES.index("manifest")]["sha256"],
        }
        for kind in binding.REQUIRED_WORK_CLOSURE_KINDS:
            closure[kind] = {"name": kind, "sha256": closure_artifacts[kind]["sha256"]}

        subjects = {
            "baseline": {
                "role": "direct-baseline",
                "source_commit": "3" * 40,
                "source_tree": "4" * 40,
                "source_snapshot": baseline_snapshot,
                "binary": baseline_binary,
                "build_receipt": baseline_receipt,
                "dispatch": "direct-native",
                "stage": "pre-cutover",
            },
            "candidate": {
                "role": "mir-candidate",
                "source_commit": "5" * 40,
                "source_tree": "6" * 40,
                "source_snapshot": candidate_snapshot,
                "binary": candidate_binary,
                "build_receipt": candidate_receipt,
                "dispatch": "mir-only",
                "stage": "post-deletion",
            },
        }
        relation = {
            "schema": binding.PROVENANCE_SCHEMA,
            "version": binding.PROVENANCE_VERSION,
            "baseline": {
                "source_commit": subjects["baseline"]["source_commit"],
                "source_tree": subjects["baseline"]["source_tree"],
                "source_snapshot_sha256": baseline_snapshot["sha256"],
                "binary_sha256": baseline_binary["sha256"],
                "build_receipt_sha256": baseline_receipt["sha256"],
            },
            "candidate": {
                "source_commit": subjects["candidate"]["source_commit"],
                "source_tree": subjects["candidate"]["source_tree"],
                "source_snapshot_sha256": candidate_snapshot["sha256"],
                "binary_sha256": candidate_binary["sha256"],
                "build_receipt_sha256": candidate_receipt["sha256"],
            },
            "toolchain": {
                "compiler_binary_sha256": compiler_binary["sha256"],
                "resource_directory_sha256": resource_directory["sha256"],
                "configuration_sha256": build_configuration["sha256"],
                "flags_sha256": build_flags["sha256"],
            },
            "harness": {
                "source_commit": "7" * 40,
                "source_tree": "8" * 40,
                "binary_sha256": harness_binary["sha256"],
                "statistics_sha256": statistics["sha256"],
            },
        }
        relation_data = (json.dumps(relation, sort_keys=True, separators=(",", ":"))
                         + "\n").encode("utf-8")
        relation_receipt = artifact("provenance/relations.json", relation_data)
        replay = {
            "schema": binding.REPLAY_SCHEMA,
            "version": binding.REPLAY_VERSION,
            "publisher": "native-retirement-evidence-v1",
            "bundle_sha256": "9" * 64,
            "contract_source_commit": "1" * 40,
            "contract_source_tree": "2" * 40,
            "candidate_source_commit": subjects["candidate"]["source_commit"],
            "candidate_source_tree": subjects["candidate"]["source_tree"],
            "replayed": True,
            "result": "identity-and-evidence-replayed",
        }
        replay_data = (json.dumps(replay, sort_keys=True, separators=(",", ":"))
                       + "\n").encode("utf-8")
        replay_receipt = artifact("provenance/replay.json", replay_data)

        record = {
            "schema": binding.SCHEMA,
            "decision_id": binding.DECISION_ID,
            "contract": {
                "source_commit": "1" * 40,
                "source_tree": "2" * 40,
                "source": contract_source,
            },
            "support": {
                "schema": "native-retirement-support-v1",
                "version": "1",
                "root_sha256": binding._canonical_files_digest(support_files),
                "files": support_files,
            },
            "requested_work": {
                "schema": "native-retirement-requested-work-v1",
                "version": 1,
                "root_sha256": binding._canonical_work_digest(requested_work_items),
                "items": requested_work_items,
                "closure": closure,
            },
            "population": {
                "required_row_count": len(rows),
                "required_rows_sha256": support_files[-1]["sha256"],
                "axes": parsed_rows[1],
                "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
                "statistical_family": parsed_rows[2],
            },
            "subjects": subjects,
            "producer": {
                "toolchain": {
                    "name": "clang",
                    "version": "18.1.3",
                    "compiler_binary": compiler_binary,
                    "resource_directory": resource_directory,
                },
                "build": {
                    "configuration": build_configuration,
                    "flags": build_flags,
                    "mode": "Release",
                    "unity": True,
                    "warnings_as_errors": True,
                    "sanitizers": False,
                    "profiling": False,
                    "allocation_hooks": False,
                },
            },
            "measurement": {
                "harness_source_commit": "7" * 40,
                "harness_source_tree": "8" * 40,
                "harness_binary": harness_binary,
                "statistics_implementation": statistics,
            },
            "execution": {
                "service": {
                    "id": "retirement-9700x",
                    "version": "service-v1",
                    "recipe": service_recipe,
                },
                "host": {
                    "machine_id": "zen5-9700x-01",
                    "qualification_receipt": qualification,
                    "aa_admission_receipt": aa_admission,
                },
                "profile": {
                    "id": "zen5-9700x-native",
                    "version": "profile-v1",
                    "descriptor": profile_descriptor,
                    "digest": profile_descriptor["sha256"],
                },
                "job_ownership": binding.LEASE_PROTOCOL,
                "lease": {
                    "authority": binding.LEASE_AUTHORITY,
                    "access": binding.LEASE_ACCESS,
                    "cleanup": binding.LEASE_CLEANUP,
                    "receipt": lease_receipt,
                },
                "native_execution": "native-only",
            },
            "provenance": {
                "schema": binding.PROVENANCE_SCHEMA,
                "version": binding.PROVENANCE_VERSION,
                "relation_receipt": relation_receipt,
                "replay_receipt": replay_receipt,
            },
            "rules": {
                "thresholds": {
                    "aggregate": {
                        "compiler_wall_time": 1.02,
                        "compiler_peak_rss": 1.02,
                        "generated_code_bytes": 1.01,
                        "generated_runtime": 1.03,
                    },
                    "per_cell": {
                        "compiler_wall_time": 1.05,
                        "compiler_peak_rss": 1.05,
                        "generated_code_bytes": 1.01,
                        "generated_runtime": 1.03,
                    },
                },
                "sampling": {
                    "seed": 20260913,
                    "rounds": 2,
                    "pairs_per_round": 60,
                    "warmups_per_variant": 2,
                    "block_order": "one-AB-and-one-BA-pair",
                    "fixed_order": "seeded-cell-order-and-first-order",
                    "optional_stopping": False,
                    "outlier_deletion": False,
                    "retain_all_samples": True,
                },
                "aggregation": {
                    "ratio": "candidate-over-baseline",
                    "wall_time": "geometric-mean-cell-ratios",
                    "peak_rss": "geometric-mean-cell-ratios",
                    "code_bytes": "exact-code-section-sum-ratio",
                    "runtime": "geometric-mean-cell-ratios",
                    "cell_weight": "one-equal-weight-per-required-cell",
                    "denominator": "requested-work-from-manifest",
                    "scope": "both-rounds-and-pooled-analysis",
                    "runtime_eligibility": "independent-native-executable-oracle-only",
                    "code_bytes_scope": "deterministic-code-section-payload-only",
                },
                "uncertainty": {
                    "confidence": "one-sided-95-percent-upper-bound",
                    "simultaneous": True,
                    "family_correction": "Bonferroni",
                    "resampling": {
                        "method": "paired-block-bootstrap",
                        "resamples": 100000,
                        "block_unit": "paired-round-block",
                        "seeded": True,
                    },
                    "invalid_data": "fail-closed",
                },
                "outcomes": {
                    "allowed": list(binding.OUTCOMES),
                    "pass_requires": "all-identities-rows-oracles-rounds-bounds-and-code-bytes",
                    "only_pass_accepts": True,
                },
            },
        }
        return record, contents

    def write_record(self, directory, record):
        path = directory / "performance-binding.json"
        path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        return path

    def write_evidence(self, directory, record, contents):
        artifacts = binding._all_artifacts(record)
        artifacts.append(("contract.source", record["contract"]["source"]))
        for _name, artifact in artifacts:
            target = directory / artifact["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents[artifact["path"]])

    def test_complete_record_and_evidence_are_accepted(self):
        record, contents = self.make_record()
        with tempfile.TemporaryDirectory(prefix="retirement-binding-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            structural = binding.validate(path)
            self.assertEqual(structural["proof"], "structural-only")
            self.assertFalse(structural["rows_recomputed"])
            checked = binding.validate(path, root / "evidence")
            self.assertEqual(checked["candidate_stage"], "post-deletion")
            self.assertEqual(checked["required_rows"], 32)
            self.assertTrue(checked["evidence_checked"])
            self.assertTrue(checked["rows_recomputed"])
            self.assertTrue(checked["provenance_checked"])
            self.assertEqual(checked["proof"], "evidence-and-receipts")
            self.assertEqual(checked["artifacts"], 32)
            command = [sys.executable, str(ROOT / "tools" /
                       "native_retirement_performance_binding.py"), str(path),
                       "--evidence-root", str(root / "evidence")]
            completed = subprocess.run(command, check=False, capture_output=True,
                                       text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn('"evidence_checked": true', completed.stdout)

    def assert_rejected(self, record):
        with tempfile.TemporaryDirectory(prefix="retirement-binding-invalid-") as directory:
            path = self.write_record(Path(directory), record)
            with self.assertRaises(ValueError):
                binding.validate(path)

    def test_mutable_or_short_identity_is_rejected(self):
        record, _contents = self.make_record()
        for value in ("main", "UNBOUND", "a" * 39):
            candidate = copy.deepcopy(record)
            candidate["subjects"]["candidate"]["source_commit"] = value
            with self.subTest(value=value):
                self.assert_rejected(candidate)

    def test_threshold_changes_are_rejected(self):
        record, _contents = self.make_record()
        record["rules"]["thresholds"]["aggregate"]["compiler_wall_time"] = 1.03
        self.assert_rejected(record)

    def test_incomplete_population_and_unknown_fields_are_rejected(self):
        record, _contents = self.make_record()
        del record["support"]["files"][-1]
        self.assert_rejected(record)
        record, _contents = self.make_record()
        record["rules"]["unexpected"] = True
        self.assert_rejected(record)

    def test_two_dummy_work_items_are_rejected(self):
        record, _contents = self.make_record()
        record["requested_work"]["items"] = [
            {"kind": "source", "name": "dummy", "artifact": {
                "path": "work/dummy-source", "bytes": 1, "sha256": "a" * 64}},
            {"kind": "workload-input", "name": "dummy", "artifact": {
                "path": "work/dummy-workload", "bytes": 1, "sha256": "b" * 64}},
        ]
        record["requested_work"]["root_sha256"] = binding._canonical_work_digest(
            record["requested_work"]["items"])
        self.assert_rejected(record)

    def test_pairs_are_even_bounded_and_seeded(self):
        record, _contents = self.make_record()
        for pairs in (61, 257):
            candidate = copy.deepcopy(record)
            candidate["rules"]["sampling"]["pairs_per_round"] = pairs
            with self.subTest(pairs=pairs):
                self.assert_rejected(candidate)
        candidate = copy.deepcopy(record)
        candidate["rules"]["sampling"]["seed"] = 0
        self.assert_rejected(candidate)

    def test_inherited_lease_protocol_is_rejected(self):
        record, _contents = self.make_record()
        record["execution"]["job_ownership"] = "server-owned-whole-job-inherited-lease-v1"
        self.assert_rejected(record)

    def test_row_count_is_recomputed_not_trusted(self):
        record, contents = self.make_record()
        record["population"]["required_row_count"] = 999999
        with tempfile.TemporaryDirectory(prefix="retirement-binding-row-count-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaisesRegex(ValueError, "recomputed row count"):
                binding.validate(path, root / "evidence")

    def test_one_line_fake_row_count_is_rejected(self):
        record, contents = self.make_record()
        rows_path = record["support"]["files"][-1]["path"]
        rows_record = json.loads(contents[rows_path].decode("utf-8"))
        rows_record["rows"] = rows_record["rows"][:1]
        rows_record["row_count"] = 999999
        rows_data = (json.dumps(rows_record, sort_keys=True, separators=(",", ":"))
                     + "\n").encode("utf-8")
        contents[rows_path] = rows_data
        performance_rows = record["support"]["files"][-1]
        performance_rows["bytes"] = len(rows_data)
        performance_rows["sha256"] = hashlib.sha256(rows_data).hexdigest()
        record["support"]["root_sha256"] = binding._canonical_files_digest(
            record["support"]["files"])
        record["population"]["required_rows_sha256"] = performance_rows["sha256"]
        with tempfile.TemporaryDirectory(prefix="retirement-binding-one-line-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaisesRegex(ValueError, "unknown fields"):
                binding.validate(path, root / "evidence")

    def test_provenance_receipt_content_is_checked(self):
        record, contents = self.make_record()
        relation_path = record["provenance"]["relation_receipt"]["path"]
        relation = json.loads(contents[relation_path].decode("utf-8"))
        relation["candidate"]["binary_sha256"] = "f" * 64
        contents[relation_path] = (json.dumps(relation, sort_keys=True,
                                               separators=(",", ":")) + "\n").encode("utf-8")
        record["provenance"]["relation_receipt"]["bytes"] = len(contents[relation_path])
        record["provenance"]["relation_receipt"]["sha256"] = hashlib.sha256(
            contents[relation_path]).hexdigest()
        with tempfile.TemporaryDirectory(prefix="retirement-binding-provenance-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            self.write_evidence(root / "evidence", record, contents)
            with self.assertRaises(ValueError):
                binding.validate(path, root / "evidence")

    def test_evidence_tampering_is_rejected(self):
        record, contents = self.make_record()
        with tempfile.TemporaryDirectory(prefix="retirement-binding-tamper-") as directory:
            root = Path(directory)
            path = self.write_record(root, record)
            evidence = root / "evidence"
            self.write_evidence(evidence, record, contents)
            evidence.joinpath(record["subjects"]["candidate"]["binary"]["path"]).write_text(
                "tampered\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                binding.validate(path, evidence)


if __name__ == "__main__":
    unittest.main()
