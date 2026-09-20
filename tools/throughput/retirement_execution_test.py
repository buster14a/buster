#!/usr/bin/env python3
"""Replay native synthetic transcripts with the unchanged #568 validator.

The native self-test writes the fixtures. These tests authenticate those exact
bytes, independently derive the schedule and process identities, and join every
sample through the production replay reader. No performance result is claimed.
Run after `bench_throughput self-test`, with its output directory as argument.
"""
import copy
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_retirement_performance_binding as binding
import native_retirement_performance_binding_test as binding_tests


class NativeExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(sys.argv[1])
        cls.data = (cls.root / "retirement-execution.jsonl").read_bytes()
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
            raw_digest = "9" * 64
            receipt = put("receipt.json", {"schema": binding.EXECUTION_RECEIPT_SCHEMA, "version": 1,
                "context_sha256": binding._canonical_json_digest(binding._execution_context(record, raw_digest)),
                "execution_plan_sha256": plan["sha256"], "job_id": "job-1", "attempt": 2,
                "boot_id": "boot-123", "bound_at_ns": 1000,
                "completed_at_ns": events[-1]["finished_ns"] + 1, "invocations": 1220,
                "shards": [self.shard]})
            with sqlite3.connect(":memory:") as db:
                db.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                           "metric TEXT, baseline TEXT, candidate TEXT, PRIMARY KEY(row_id, round_id, pair_id, metric))")
                samples = {}
                for event in events:
                    if event["phase"] != "sample":
                        continue
                    metrics = {"generated_runtime": event["wall_seconds"]} if event["kind"] == "runtime" else {
                        "compiler_wall_time": event["wall_seconds"], "compiler_peak_rss": event["peak_rss_bytes"],
                        "generated_code_bytes": event["code_section_bytes"]}
                    for metric, value in metrics.items():
                        key = (event["row"], event["round"], event["pair"], metric)
                        samples.setdefault(key, {})[event["variant"]] = str(value)
                for key, values in samples.items():
                    db.execute("INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?)",
                               (*key, values["baseline"], values["candidate"]))
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
