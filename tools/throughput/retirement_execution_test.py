#!/usr/bin/env python3
"""Replay native synthetic transcripts with the unchanged #568 validator.

The native self-test writes the fixtures. These tests authenticate those exact
bytes, independently derive the schedule and process identities, and join every
sample through the production replay reader. No performance result is claimed.
Run after `bench_throughput self-test`, with its output directory as argument.
"""
import copy
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_retirement_performance_binding as binding
import native_retirement_performance_binding_test as binding_tests
import native_retirement_result_input as result_input


class NativeExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(sys.argv[1])
        cls.data = (cls.root / "retirement-execution.jsonl").read_bytes()
        cls.sample_data = (cls.root / "retirement-samples-0000.jsonl").read_bytes()
        cls.shard = {"path": "retirement-execution.jsonl", "bytes": len(cls.data),
                     "sha256": hashlib.sha256(cls.data).hexdigest(), "records": 1220}

    def test_exact_native_bytes_pass_canonical_reader(self):
        events = list(binding._execution_trace_records(self.root, [self.shard], 1220))
        rows = [{"row": i, "metrics": {"generated_runtime": i != 1}} for i in range(3)]
        sampling = {"seed": 1, "rounds": 2, "pairs_per_round": 60, "warmups_per_variant": 2}
        expected = list(binding._execution_schedule(rows, sampling))
        self.assertEqual(len(events), len(expected))
        for event, identity in zip(events, expected):
            self.assertEqual({key: event[key] for key in identity}, identity)
            self.assertEqual(event["process_instance_sha256"], binding._process_instance_digest(
                "job-1", 2, "boot-123", event["pid"], event["process_start_token"]))

    def test_exact_nanosecond_serialization_is_canonical(self):
        lines = (self.root / "retirement-seconds.tsv").read_text().splitlines()
        self.assertEqual(len(lines), 110001)
        for line in lines:
            nanoseconds, text = line.split("\t")
            value = json.loads(text)
            self.assertEqual(json.dumps(value, separators=(",", ":")), text)
            self.assertEqual(binding.Decimal(str(value)) * 1_000_000_000, int(nanoseconds))

    def test_native_sample_bytes_and_manifest_are_canonical(self):
        path = self.root / "retirement-samples.manifest.json"
        manifest_bytes = path.read_bytes()
        manifest = json.loads(manifest_bytes)
        self.assertEqual(manifest_bytes, (json.dumps(manifest, sort_keys=True,
            separators=(",", ":")) + "\n").encode())
        self.assertEqual(manifest, {
            "schema": result_input.MANIFEST_SCHEMA, "version": 1,
            "identity_field": "record_id", "shards": [{
                "identity": "samples-0000", "path": "retirement-samples-0000.jsonl",
                "bytes": len(self.sample_data), "sha256": hashlib.sha256(self.sample_data).hexdigest()}]})
        for ordinal, line in enumerate(self.sample_data.splitlines(keepends=True)):
            value = json.loads(line)
            self.assertEqual(line, (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())
            self.assertEqual((value["row"], value["round"], value["pair"]),
                             (ordinal // 120, ordinal // 60 % 2, ordinal % 60))
            self.assertEqual("generated_runtime" in value["measurements"], value["row"] != 1)
        self.assertEqual(ordinal + 1, 360)

    @unittest.skipUnless(os.name == "posix", "production no-follow result reader requires POSIX")
    def test_native_sample_shards_pass_production_integrity_reader(self):
        receipt = result_input.verify(self.root.resolve(), "retirement-samples.manifest.json")
        self.assertEqual(receipt["records"], 360)
        self.assertEqual(receipt["scope"], "integrity-only")
        self.assertEqual(receipt["shards"][0]["sha256"], hashlib.sha256(self.sample_data).hexdigest())

    @unittest.skipUnless(os.name == "posix", "production no-follow result reader requires POSIX")
    def test_native_sample_boundary_and_applicability(self):
        root = (self.root / "retirement-samples-boundary").resolve()
        rows = {row: {"row": row, "metrics": {"compiler_peak_rss": True,
            "compiler_wall_time": True, "generated_code_bytes": row % 3 != 2,
            "generated_runtime": row % 3 != 1}} for row in range(274)}
        ordinal_map = {row: row for row in rows}
        digest, seen = hashlib.sha256(), [0]

        def consume(_shard, _ordinal, value):
            binding._consume_result_record(value, ordinal_map, rows, 2, 60, 0, seen, digest)

        receipt = result_input.verify(root, "retirement-samples.manifest.json", record_consumer=consume)
        self.assertEqual(receipt["records"], 32880)
        self.assertEqual(seen[0], 32880)
        self.assertEqual([shard["records"] for shard in receipt["shards"]], [32768, 112])
        expected = hashlib.sha256()
        for shard in receipt["shards"]:
            with (root / shard["path"]).open("rb") as stream:
                for line in stream:
                    value = json.loads(line)
                    self.assertEqual(line, (json.dumps(value, sort_keys=True,
                        separators=(",", ":")) + "\n").encode())
                    expected.update(line)
        self.assertEqual(digest.hexdigest(), expected.hexdigest())

    def test_native_manifest_full_cap_partitions(self):
        # These are explicitly synthetic descriptor-only fixtures: this checks
        # maximum-capacity manifest layout, not 39 million collected samples.
        counts, number = [], 0
        for partition in range(3):
            path = f"retirement-partition-{partition}.manifest.json"
            data = (self.root / path).read_bytes()
            value = json.loads(data)
            self.assertEqual(data, (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())
            identity, shards, _bytes = result_input._manifest(value, path, len(data), result_input.Limits())
            self.assertEqual(identity, "record_id")
            records = 0
            for shard in shards:
                self.assertEqual(shard["identity"], f"samples-{number:04d}")
                self.assertEqual(shard["path"], f"retirement-samples-{number:04d}.jsonl")
                self.assertEqual(shard["bytes"] % 500, 0)
                records += shard["bytes"] // 500
                number += 1
            counts.append(records)
        self.assertEqual(counts, [16777216, 16777216, 5963776])
        self.assertEqual(number, 1206)
        self.assertEqual(sum(counts), 39518208)

    def test_full_invocation_replay_joins_native_observations(self):
        events = list(binding._execution_trace_records(self.root, [self.shard], 1220))
        template = binding_tests.BindingTests._series_join_fixture()[0][0]
        rows = []
        oracles = []
        for i in range(3):
            row = copy.deepcopy(template)
            row["row"] = i
            row["identity"]["fixture"] = f"tests/native-execution-{i}.c"
            row["identity"]["artifact_stage"] = "object" if i == 1 else "link"
            row["metrics"]["generated_runtime"] = i != 1
            rows.append(row)
            oracles.append({"row": i, "code_section_status": "parsed-deterministic",
                            "code_section_bytes": 32, "code_section_sha256": "d" * 64,
                            "runtime_oracle_status": "not-applicable" if i == 1 else "passed-native",
                            "runtime_exit_code": -1 if i == 1 else 0, "native_runtime": i != 1})
        rules = binding_tests.BindingTests._rules()
        rules["sampling"].update(seed=1, rounds=2, pairs_per_round=60, warmups_per_variant=2)
        with tempfile.TemporaryDirectory(prefix="native-execution-replay-") as directory:
            root = Path(directory)

            def put(name, value):
                data = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()
                (root / name).write_bytes(data)
                return {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

            artifact = {"path": "placeholder", "bytes": 1, "sha256": "a" * 64}
            record = {
                "subjects": {side: {"binary": artifact} for side in ("baseline", "candidate")},
                "support": {"root_sha256": "3" * 64,
                            "files": [{"name": role, **artifact} for role in binding.SUPPORT_FILE_ROLES]},
                "measurement": {"harness_binary": artifact}, "execution": {"service": "synthetic"},
                "rules": rules,
                "workflow": {"phases": {name: artifact for name in ("pre_sample_plan", "post_aa_binding")},
                             "records": {"admission": artifact, "oracle": put("oracle.json", {"records": oracles})}},
            }
            contracts = []
            for row, oracle in zip(rows, oracles):
                runtime = row["metrics"]["generated_runtime"]
                side = {"compiler_command_sha256": "b" * 64, "artifact_sha256": "c" * 64,
                        "code_section_sha256": "d" * 64, "code_section_bytes": 32,
                        "runtime_command_sha256": "b" * 64 if runtime else None,
                        "runtime_output_sha256": "c" * 64 if runtime else None}
                contracts.append({"row": row["row"],
                                  "identity_sha256": binding._canonical_json_digest(row["identity"]),
                                  "oracle_sha256": binding._canonical_json_digest(oracle),
                                  "baseline": side, "candidate": side})
            plan = put("plan.json", {"schema": binding.EXECUTION_PLAN_SCHEMA, "version": 1,
                "schedule": binding.EXECUTION_SCHEDULE, "seed": 1, "rounds": 2,
                "pairs_per_round": 60, "warmups_per_variant": 2, "cpu": 2,
                "performance_rows_sha256": "a" * 64, "rows": contracts})
            (root / self.shard["path"]).write_bytes(self.data)
            raw_digest = hashlib.sha256(self.sample_data).hexdigest()
            receipt = put("receipt.json", {"schema": binding.EXECUTION_RECEIPT_SCHEMA, "version": 1,
                "context_sha256": binding._canonical_json_digest(binding._execution_context(record, raw_digest)),
                "execution_plan_sha256": plan["sha256"], "job_id": "job-1", "attempt": 2,
                "boot_id": "boot-123", "bound_at_ns": 1000,
                "completed_at_ns": events[-1]["finished_ns"] + 1, "invocations": 1220,
                "shards": [self.shard]})
            with closing(sqlite3.connect(":memory:")) as db:
                db.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                           "metric TEXT, baseline TEXT, candidate TEXT, PRIMARY KEY(row_id, round_id, pair_id, metric))")
                row_by_id = {row["row"]: row for row in rows}
                row_ordinals = {row["row"]: ordinal for ordinal, row in enumerate(rows)}
                measurement_digest, seen = hashlib.sha256(), [0]
                for line in self.sample_data.splitlines():
                    binding._consume_result_record(json.loads(line), row_ordinals, row_by_id,
                        2, 60, 0, seen, measurement_digest, db)
                self.assertEqual(seen[0], 360)
                self.assertEqual(measurement_digest.hexdigest(), raw_digest)
                result = binding._check_execution_transcript(root, receipt, plan, record, rows,
                    rules["sampling"], db, raw_digest, receipt["sha256"], 2, "x86_64-unknown-linux-gnu")
                self.assertEqual(result["invocations"], 1220)
                # Numeric samples cannot be silently replaced with positive values.
                db.execute("UPDATE samples SET baseline='99' WHERE row_id=0 AND round_id=0 AND pair_id=0")
                with self.assertRaisesRegex(ValueError, "not the authenticated invocation"):
                    binding._check_execution_transcript(root, receipt, plan, record, rows,
                        rules["sampling"], db, raw_digest, receipt["sha256"], 2, "x86_64-unknown-linux-gnu")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: retirement_execution_test.py NATIVE_TEST_OUTPUT_DIRECTORY")
    unittest.main(argv=[sys.argv[0]], verbosity=2)
