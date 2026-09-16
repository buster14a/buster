#!/usr/bin/env python3
"""Unmocked checked-in support identity and current-population budget guards.

These tests do not construct acceptance evidence.  The support-output probe
must reach, and reject, an intentionally manifest-only census after verifying
the real declaration.  Partition tests exercise the current object population
plus the required stage-row lower bound, not a proposed experiment schedule.
"""

import copy
import csv
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import native_retirement_performance_binding as binding


ROOT = Path(__file__).resolve().parents[1]


class PerformanceIdentityTests(unittest.TestCase):
    @staticmethod
    def descriptor(path, data):
        return {"path": path, "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}

    def setUp(self):
        self.declaration = (ROOT / binding.SUPPORT_DECLARATION_PATH).read_bytes()
        self.rows = list(csv.DictReader(
            io.StringIO(self.declaration.decode("utf-8")), delimiter="\t"))
        self.subjects = sum(row["role"] == "subject" for row in self.rows)
        self.object_rows = (self.subjects * len(binding.TARGETS)
                            * len(binding.FRONTENDS) * len(binding.PIC)
                            * len(binding.ALLOCATORS))

    def test_checked_in_support_bytes_match_reviewed_pin(self):
        self.assertEqual(hashlib.sha256(self.declaration).hexdigest(),
                         binding.SUPPORT_DECLARATION_SHA256)

    def test_support_counts_follow_checked_in_population(self):
        self.assertEqual(binding._approved_support_counts(),
                         (len(self.rows), self.subjects,
                          self.object_rows // len(binding.ALLOCATORS),
                          self.object_rows))
        self.assertEqual(binding.SUPPORT_OBJECT_ROW_COUNT, self.object_rows)
        self.assertEqual(binding.SUPPORT_MIN_STAGE_ROW_COUNT,
                         self.object_rows + len(binding.STAGES) - 1)

    def support_probe(self, root, data=None):
        """Use real support bytes and an explicitly non-accepting next stage."""
        data = self.declaration if data is None else data
        declaration_path = root / binding.SUPPORT_DECLARATION_PATH
        declaration_path.parent.mkdir(parents=True, exist_ok=True)
        declaration_path.write_bytes(data)
        declaration = self.descriptor(binding.SUPPORT_DECLARATION_PATH, data)
        # All required keys are present so the next rejection is specifically
        # manifest_only=1, rather than a missing-key or malformed-TSV failure.
        manifest = {
            "version": "2", "kind": "object-coverage", "identity_hash": "sha256",
            "support_contract": binding.SUPPORT_DECLARATION_PATH,
            "support_contract_sha256": declaration["sha256"],
            "inputs": str(len(self.rows)), "rows": str(self.object_rows),
            "manifest_only": "1", "environment": "explicit-replacement-in-environment.tsv",
            "unfrozen_dependencies": "none-for-object-census", "sysroot": "none",
            "system_include": "none", "resource_include_sha256": "a" * 64,
            "compiler_revision_claim": "a" * 40, "baseline_revision_claim": "b" * 40,
            "compiler_hash": "1", "compiler_bytes": "1", "compiler_sha256": "c" * 64,
            "baseline_hash": "2", "baseline_bytes": "1", "baseline_sha256": "d" * 64,
            "cpu": "baseline",
        }
        manifest_data = "".join(f"{key}={value}\n" for key, value in manifest.items()).encode()
        manifest_path = root / "manifest-only.txt"
        manifest_path.write_bytes(manifest_data)
        files = [None] * len(binding.SUPPORT_FILE_ROLES)
        files[binding.SUPPORT_FILE_ROLES.index("support_declaration")] = declaration
        files[binding.SUPPORT_FILE_ROLES.index("manifest")] = self.descriptor(
            manifest_path.name, manifest_data)
        return {"support": {"files": files}}

    def test_support_output_checks_real_declaration_before_rejecting_manifest_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root)
            with self.assertRaisesRegex(ValueError, "not an executed, SHA-256 census"):
                binding._check_support_output(root, record, None)

    def test_same_population_rehashed_drift_is_not_approval(self):
        changed = bytearray(self.declaration)
        # Change one digest nibble, preserving every row, field and byte count.
        index = len(changed) - 2
        self.assertIn(chr(changed[index]), "0123456789abcdef")
        changed[index] = ord("0") if changed[index] != ord("0") else ord("1")
        self.assertEqual(len(changed), len(self.declaration))
        self.assertEqual(bytes(changed).count(b"\n"), self.declaration.count(b"\n"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root, bytes(changed))
            with self.assertRaisesRegex(ValueError, "not the approved immutable input"):
                binding._check_support_output(root, record, None)

    def test_support_output_rejects_byte_tampering_under_approved_descriptor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root)
            path = root / binding.SUPPORT_DECLARATION_PATH
            path.write_bytes(self.declaration[:-1] + b" ")
            with self.assertRaisesRegex(ValueError, "digest does not match evidence"):
                binding._check_support_output(root, record, None)

    def test_support_output_rejects_wrong_path_and_size(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.support_probe(root)
            descriptor = record["support"]["files"][
                binding.SUPPORT_FILE_ROLES.index("support_declaration")]
            for field, value, message in (
                    ("path", "other.tsv", "not the frozen declaration"),
                    ("bytes", descriptor["bytes"] - 1, "byte count does not match evidence")):
                candidate = copy.deepcopy(record)
                candidate["support"]["files"][
                    binding.SUPPORT_FILE_ROLES.index("support_declaration")][field] = value
                with self.subTest(field=field), self.assertRaisesRegex(ValueError, message):
                    binding._check_support_output(root, candidate, None)

    def check_plan(self, root, pairs):
        # This is the mandatory stage lower bound. Real population validation
        # separately joins every admitted link/self-host/workload row.
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        required = sample_rows * 2 * pairs
        cap = binding.RESULT_INPUT_MAX_RECORDS
        manifests = [{
            "identity": f"manifest-{index}", "path": f"results/manifest-{index}.json",
            "start_record": start, "records": min(cap, required - start),
        } for index, start in enumerate(range(0, required, cap))]
        support = {"manifest_sha256": "a" * 64, "rows_sha256": "b" * 64,
                   "object_row_count": self.object_rows}
        plan = {
            "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
            "source_manifest_sha256": support["manifest_sha256"],
            "source_rows_sha256": support["rows_sha256"],
            "identity_field": "record_id", "coordinate_schema": "row-round-pair-v1",
            "sample_population": "canonical-performance-rows-with-required-metrics",
            "eligible_population": "canonical-performance-rows",
            "object_row_count": self.object_rows, "sample_row_count": sample_rows,
            "rounds": 2, "pairs_per_round": pairs, "records_per_row": 2 * pairs,
            "required_records": required, "max_records_per_manifest": cap,
            "manifest_count": len(manifests), "manifests": manifests, "predeclared": True,
        }
        data = (json.dumps(plan, sort_keys=True, separators=(",", ":")) + "\n").encode()
        path = root / "plan.json"
        path.write_bytes(data)
        rules = {"sampling": {"rounds": 2, "pairs_per_round": pairs}}
        return binding._result_input_plan(root, self.descriptor(path.name, data),
                                          support, {}, rules)

    def test_current_population_and_stage_floor_fit_predeclared_partition(self):
        self.assertEqual(binding.RESULT_INPUT_MAX_TOTAL_RECORDS, 39_518_208)
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        maximum_even = min(256, binding.RESULT_INPUT_MAX_TOTAL_RECORDS // (2 * sample_rows))
        maximum_even -= maximum_even % 2
        self.assertGreaterEqual(maximum_even, 60, "current population cannot fit minimum sampling")
        with tempfile.TemporaryDirectory() as directory:
            plan = self.check_plan(Path(directory), maximum_even)
            self.assertEqual(plan["sample_row_count"], sample_rows)
            self.assertEqual(sum(part["records"] for part in plan["manifests"]),
                             sample_rows * 2 * maximum_even)

    def test_current_population_over_cap_cannot_be_rescued_by_more_shards(self):
        sample_rows = self.object_rows + len(binding.STAGES) - 1
        required = sample_rows * 2 * 256
        self.assertGreater(required, binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "immutable total-record ceiling"):
                self.check_plan(Path(directory), 256)


if __name__ == "__main__":
    unittest.main()
