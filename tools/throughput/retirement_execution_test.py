#!/usr/bin/env python3
"""Replay native synthetic transcripts with the unchanged #568 validator.

The native self-test writes the fixtures. These tests authenticate those exact
bytes, independently derive the schedule and process identities, and join every
sample through the production replay reader. No performance result is claimed.
(A1) Compiler invocations are batch processes of frozen batch groups; object
batches carry per-input metrics artifacts as byte ranges of metrics shards and
name their inputs through a digest-bound `@file` response file; row and batch
samples are separate #615 populations; code bytes are a once-per-row record
set; untimed code-artifact batches have their own sealed records and shards.
Run after `bench_throughput self-test`, with its output directory as argument.
"""
import copy
from contextlib import closing
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_retirement_performance_binding as binding
import native_retirement_performance_binding_test as binding_tests
import native_retirement_result_input as result_input

EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()


def _canonical_line(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _native_rows(row_ids=(0, 6, 10), object_rows=(6,)):
    """Timed native rows: object rows form batch groups, others are link rows."""
    template = binding_tests.BindingTests._series_join_fixture()[0][0]
    rows = []
    for index, row_id in enumerate(row_ids):
        row = copy.deepcopy(template)
        row["row"] = row_id
        row["identity"]["fixture"] = f"tests/native-execution-{index}.c"
        row["identity"]["artifact_stage"] = "object" if row_id in object_rows else "link"
        row["metrics"]["generated_runtime"] = row_id not in object_rows
        rows.append(row)
    return rows


def _slice(root, artifact):
    with (root / artifact["path"]).open("rb") as stream:
        stream.seek(artifact["offset"])
        return stream.read(artifact["bytes"])


def _boundary_object_groups(rows):
    """Replay the C boundary layout: row % 3 == 1 rows fill open object groups
    of at most four members; every other row is a singleton link group."""
    groups, open_group, open_members, ordinals = 0, None, 0, {}
    for row in range(rows):
        if row % 3 == 1 and open_group is not None and open_members < 4:
            open_members += 1
            continue
        if row % 3 == 1:
            open_group, open_members = groups, 1
            ordinals[groups] = len(ordinals)
        groups += 1
    return ordinals


class NativeExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(sys.argv[1])
        cls.data = (cls.root / "retirement-execution.jsonl").read_bytes()
        cls.sample_data = (cls.root / "retirement-samples-0000.jsonl").read_bytes()
        cls.batch_data = (cls.root / "retirement-batches-0000.jsonl").read_bytes()
        cls.shard = {"path": "retirement-execution.jsonl", "bytes": len(cls.data),
                     "sha256": hashlib.sha256(cls.data).hexdigest(), "records": 1220}

    @unittest.skipUnless(sys.platform == "linux", "descriptor-bound execution requires Linux")
    def test_real_measured_commands_outputs_and_numeric_joins(self):
        commands = {}
        escaped = (self.root / "retirement-measured-command-escaped.json").read_bytes()
        self.assertEqual(escaped, json.dumps(json.loads(escaped), sort_keys=True, separators=(",", ":")).encode())
        self.assertEqual(hashlib.sha256(escaped).hexdigest(),
            (self.root / "retirement-measured-command-escaped.sha256").read_text())
        for kind in ("compiler", "runtime"):
            data = (self.root / f"retirement-measured-command-{kind}.json").read_bytes()
            command = json.loads(data)
            self.assertEqual(data, json.dumps(command, sort_keys=True, separators=(",", ":")).encode())
            self.assertEqual(command["environment"], ["LC_ALL=C", "TP_RETIREMENT_TEST=explicit"])
            commands[kind] = hashlib.sha256(data).hexdigest()
        data = (self.root / "retirement-measured-execution.jsonl").read_bytes()
        shard = {"path": "retirement-measured-execution.jsonl", "bytes": len(data),
                 "sha256": hashlib.sha256(data).hexdigest(), "records": 488}
        events = list(binding._execution_trace_records(self.root, [shard], 488))
        # One timed native link row: a singleton batch group with runtime.
        rows = _native_rows(row_ids=(0,), object_rows=())
        schedule = binding._execution_schedule(
            rows, {"seed": 1, "rounds": 2, "pairs_per_round": 60, "warmups_per_variant": 2})
        output_digest = hashlib.sha256(b"fixture-code\n").hexdigest()
        artifact = (self.root / "retirement-artifact-1-1.bin").read_bytes()
        self.assertEqual(artifact[:7], b"\x7fELF\x02\x01\x01")
        section_table = struct.unpack_from("<Q", artifact, 40)[0]
        code_offset, code_bytes = struct.unpack_from("<QQ", artifact, section_table + 64 + 24)
        self.assertEqual(artifact[code_offset:code_offset + code_bytes], b"fixture-code\n")
        artifact_digest = hashlib.sha256(artifact).hexdigest()
        batch_digest = binding._batch_output_digest([artifact_digest])
        measured = {}
        identities = set()
        previous_end = 0
        for event, expected in zip(events, schedule, strict=True):
            for key, value in expected.items():
                self.assertEqual(event[key], value)
            self.assertEqual(event["command_sha256"], commands[event["kind"]])
            self.assertEqual(event["output_sha256"],
                             batch_digest if event["kind"] == "compiler" else output_digest)
            self.assertIsNone(event["code_section_bytes"])
            self.assertIsNone(event["code_section_sha256"])
            self.assertIsNone(event["metrics_artifact"])
            self.assertEqual(event["exit_code"], 0)
            self.assertGreater(event["started_ns"], previous_end)
            elapsed = event["finished_ns"] - event["started_ns"]
            self.assertGreater(elapsed, 0)
            self.assertEqual(Decimal(str(event["wall_seconds"])) * 1_000_000_000, elapsed)
            previous_end = event["finished_ns"]
            self.assertNotIn(event["process_instance_sha256"], identities)
            identities.add(event["process_instance_sha256"])
            if event["phase"] == "sample":
                measured[event["round"], event["pair"], event["variant"], event["kind"]] = event
        lines = (self.root / "retirement-measured-samples.jsonl").read_bytes().splitlines(keepends=True)
        self.assertEqual(len(lines), 120)
        for ordinal, line in enumerate(lines):
            sample = json.loads(line)
            self.assertEqual(line, _canonical_line(sample))
            self.assertEqual((sample["row"], sample["round"], sample["pair"]), (0, ordinal // 60, ordinal % 60))
            metrics = sample["measurements"]
            self.assertEqual(set(metrics), {"compiler_peak_memory", "compiler_wall_time", "generated_runtime"})
            for variant in ("baseline", "candidate"):
                compiler = measured[sample["round"], sample["pair"], variant, "compiler"]
                runtime = measured[sample["round"], sample["pair"], variant, "runtime"]
                self.assertEqual(metrics["compiler_wall_time"][variant], compiler["wall_seconds"])
                self.assertEqual(metrics["compiler_peak_memory"][variant], compiler["peak_rss_bytes"])
                self.assertEqual(metrics["generated_runtime"][variant], runtime["wall_seconds"])
        # Code bytes are measured once per (variant, row) from the frozen
        # artifact and its byte-identical reproduction, through the
        # production code-record reader.
        code = (self.root / "retirement-measured-code.jsonl").read_bytes()
        record = json.loads(code)
        self.assertEqual(code, _canonical_line(record))
        for variant in ("baseline", "candidate"):
            self.assertEqual(record[variant], {
                "artifact_sha256": artifact_digest, "code_section_bytes": 13,
                "code_section_sha256": output_digest, "reproduction_sha256": artifact_digest})
        side = {"artifact_sha256": artifact_digest, "code_section_bytes": 13,
                "code_section_sha256": output_digest}
        facts = binding._check_code_records(
            self.root, {"path": "retirement-measured-code.jsonl", "bytes": len(code),
                        "sha256": hashlib.sha256(code).hexdigest(), "records": 1},
            rows, {0: {"baseline": side, "candidate": side}})
        self.assertEqual(facts, {0: (13, 13)})

    @unittest.skipUnless(sys.platform == "linux", "descriptor-bound execution requires Linux")
    def test_real_measured_object_batches_join_per_input_metrics(self):
        """Real batch processes: every object and metrics file is checked."""
        command = (self.root / "retirement-measured-command-batch.json").read_bytes()
        data = (self.root / "retirement-measured-batch-execution.jsonl").read_bytes()
        shard = {"path": "retirement-measured-batch-execution.jsonl", "bytes": len(data),
                 "sha256": hashlib.sha256(data).hexdigest(), "records": 244}
        events = list(binding._execution_trace_records(self.root, [shard], 244))
        rows = _native_rows(row_ids=(0, 1), object_rows=(0, 1))
        for index, row in enumerate(rows):
            row["identity"]["fixture"] = ("tests/alpha.c", "tests/beta.c")[index]
        schedule = binding._execution_schedule(
            rows, {"seed": 1, "rounds": 2, "pairs_per_round": 60, "warmups_per_variant": 2})
        artifact_digest = hashlib.sha256((self.root / "retirement-artifact-1-1.bin").read_bytes()).hexdigest()
        control = {"fixture": "tests/control.c", "row": None, "status": "rejected",
                   "error": "driver.analysis", "diagnostic_sha256": "b" * 64, "object_sha256": None}
        contract = {"members": [{"row": row, "diagnostic_sha256": EMPTY_SHA256} for row in (0, 1)],
                    "controls": [control], "metrics_bytes_max": 1 << 20,
                    "configuration": {"allocator": "none", "frontend_lowering": "direct-ssa", "PIC": "0"}}
        # The batch argv names its inputs only through the response file,
        # whose leaf is the SHA-256 of the canonical list of the frozen order.
        argv = json.loads(command)["argv"]
        listing = binding._input_list_bytes(["tests/alpha.c", "tests/beta.c", "tests/control.c"], "list")
        leaf = f"retirement-inputs-{hashlib.sha256(listing).hexdigest()}.rsp"
        self.assertEqual([item for item in argv if item.startswith("@")], ["@" + leaf])
        self.assertFalse({"tests/alpha.c", "tests/beta.c", "tests/control.c"} & set(argv))
        self.assertLessEqual(len(argv), 256)
        self.assertEqual((self.root / "retirement-measured" / leaf).read_bytes(), listing)
        self.assertEqual(binding._response_file_arguments(listing, "list"),
                         [b"tests/alpha.c", b"tests/beta.c", b"tests/control.c"])
        shards = binding._MetricsShards(self.root)
        row_contracts = {row: {variant: {"artifact_sha256": artifact_digest}
                               for variant in ("baseline", "candidate")} for row in (0, 1)}
        row_by_id = {row["row"]: row for row in rows}
        members_by_coordinate = {}
        paths = set()
        for event, expected in zip(events, schedule, strict=True):
            for key, value in expected.items():
                self.assertEqual(event[key], value)
            self.assertEqual(event["command_sha256"], hashlib.sha256(command).hexdigest())
            self.assertEqual(event["exit_code"], 1)
            frozen = binding._frozen_batch_inputs(contract, row_contracts, row_by_id, event["variant"],
                                                  "artifact_sha256")
            self.assertEqual(event["output_sha256"],
                             binding._batch_output_digest([artifact_digest, artifact_digest, None]))
            descriptor = event["metrics_artifact"]
            self.assertEqual(descriptor["path"], "retirement-metrics-mb-0000.txt")
            self.assertNotIn(descriptor["sha256"], paths)
            paths.add(descriptor["sha256"])
            feed = shards.add(descriptor, "real batch metrics")
            members = binding._check_batch_metrics(
                self.root, descriptor, contract, frozen, 1, binding.NATIVE_TIMED_TARGET,
                event["finished_ns"] - event["started_ns"], "real batch metrics", feed)
            self.assertEqual(set(members), {0, 1})
            if event["phase"] == "sample":
                members_by_coordinate[event["round"], event["pair"], event["variant"]] = (members, event)
        # All 244 artifacts tile one shard exactly, in record order.
        self.assertEqual([item["path"] for item in shards.finish()], ["retirement-metrics-mb-0000.txt"])
        row_lines = (self.root / "retirement-measured-batch-rows.jsonl").read_bytes().splitlines()
        batch_lines = (self.root / "retirement-measured-batch-batches.jsonl").read_bytes().splitlines()
        self.assertEqual((len(row_lines), len(batch_lines)), (240, 120))
        for line in row_lines:
            sample = json.loads(line)
            for variant in ("baseline", "candidate"):
                members, _event = members_by_coordinate[sample["round"], sample["pair"], variant]
                interval, arena = members[sample["row"]]
                self.assertEqual(Decimal(str(sample["measurements"]["compiler_wall_time"][variant])) * 10**9,
                                 interval)
                self.assertEqual(sample["measurements"]["compiler_peak_memory"][variant], arena)
        for line in batch_lines:
            sample = json.loads(line)
            self.assertEqual(sample["group"], 0)
            for variant in ("baseline", "candidate"):
                _members, event = members_by_coordinate[sample["round"], sample["pair"], variant]
                self.assertEqual(sample["measurements"]["compiler_batch_wall_time"][variant],
                                 event["wall_seconds"])
                self.assertEqual(sample["measurements"]["compiler_batch_peak_rss"][variant],
                                 event["peak_rss_bytes"])

    def test_independent_artifact_payloads(self):
        """Decode the C fixtures without importing the producer's parser."""
        for machine in (1, 2):
            for format_id in (1, 2, 3, 4):
                with self.subTest(machine=machine, format=format_id):
                    data = (self.root / f"retirement-artifact-{format_id}-{machine}.bin").read_bytes()
                    if format_id == 1:
                        self.assertEqual(struct.unpack_from("<H", data, 18)[0], 62 if machine == 1 else 183)
                        table = struct.unpack_from("<Q", data, 40)[0]
                        offset, size = struct.unpack_from("<QQ", data, table + 64 + 24)
                    elif format_id in (2, 3):
                        header = struct.unpack_from("<I", data, 60)[0] + 4 if format_id == 3 else 0
                        self.assertEqual(struct.unpack_from("<H", data, header)[0], 0x8664 if machine == 1 else 0xaa64)
                        section = header + 20 + struct.unpack_from("<H", data, header + 16)[0]
                        size, offset = struct.unpack_from("<II", data, section + 16)
                        if format_id == 3:
                            virtual_size = struct.unpack_from("<I", data, section + 8)[0]
                            self.assertGreater(size, virtual_size)
                            size = virtual_size
                    else:
                        self.assertEqual(struct.unpack_from("<I", data, 4)[0], 0x01000007 if machine == 1 else 0x0100000c)
                        size, offset = struct.unpack_from("<QI", data, 32 + 72 + 40)
                    self.assertEqual(data[offset:offset + size], b"fixture-code\n")

    def test_exact_native_bytes_pass_canonical_reader(self):
        events = list(binding._execution_trace_records(self.root, [self.shard], 1220))
        rows = _native_rows()
        sampling = {"seed": 1, "rounds": 2, "pairs_per_round": 60, "warmups_per_variant": 2}
        expected = list(binding._execution_schedule(rows, sampling))
        self.assertEqual([group["rows"] for group in binding._batch_groups(rows)], [[0], [6], [10]])
        self.assertEqual(len(events), len(expected))
        offset = 0
        for event, identity in zip(events, expected):
            self.assertEqual({key: event[key] for key in identity}, identity)
            self.assertEqual(event["process_instance_sha256"], binding._process_instance_digest(
                "job-1", 2, "boot-123", event["pid"], event["process_start_token"]))
            if event["kind"] == "compiler" and event["group"] == 1:
                descriptor = event["metrics_artifact"]
                data = _slice(self.root, descriptor)
                self.assertEqual(descriptor, {"bytes": len(data), "offset": offset,
                                              "path": "retirement-metrics-rec-0000.txt",
                                              "sha256": hashlib.sha256(data).hexdigest()})
                offset += len(data)
            else:
                self.assertIsNone(event["metrics_artifact"])
        self.assertEqual((self.root / "retirement-metrics-rec-0000.txt").stat().st_size, offset)

    def test_maximum_serialized_records_fit_the_shard_byte_cap(self):
        transcript_line = (self.root / "retirement-execution-max.jsonl").read_bytes()
        transcript_shard = {"path": "retirement-execution-max.jsonl",
            "bytes": len(transcript_line), "sha256": hashlib.sha256(transcript_line).hexdigest(),
            "records": 1}
        transcript = list(binding._execution_trace_records(self.root, [transcript_shard], 1))
        self.assertEqual(len(transcript), 1)
        event = transcript[0]
        self.assertEqual(len(transcript_line), 1017)
        self.assertEqual(event["sequence"], 101999999)
        self.assertEqual((event["kind"], event["group"], event["row"]), ("compiler", 99999, None))
        self.assertEqual((event["round"], event["pair"], event["variant"]), (1, 253, "candidate"))
        self.assertEqual((event["pid"], event["process_start_token"]),
                         (2**64 - 1, str(2**64 - 1)))
        self.assertEqual((event["started_ns"], event["finished_ns"]),
                         (2**64 - 1 - 86399999999999, 2**64 - 1))
        self.assertIsNone(event["code_section_bytes"])
        self.assertIsNone(event["code_section_sha256"])
        self.assertEqual(event["exit_code"], 255)
        self.assertEqual(event["peak_rss_bytes"], 2**53 - 1)
        self.assertEqual(event["wall_seconds"], 86399.999999999)
        self.assertEqual(event["cpu"], 2**31 - 1)
        for key in ("command_sha256", "executable_sha256", "output_sha256", "process_instance_sha256"):
            self.assertEqual(len(event[key]), 64)
        self.assertEqual(event["process_instance_sha256"], binding._process_instance_digest(
            "a" * 128, 2**64 - 1, "b" * 128, 2**64 - 1, str(2**64 - 1)))
        self.assertEqual(event["metrics_artifact"], {
            "bytes": 33554432, "offset": 33554432,
            "path": "retirement-metrics-abcdefgh-2047.txt", "sha256": "e" * 64})
        self.assertEqual(binding._metrics_artifact(event["metrics_artifact"], "max"), event["metrics_artifact"])
        self.assertNotIn(b'"job_id"', transcript_line)
        self.assertNotIn(b'"boot_id"', transcript_line)

        warmup_line = (self.root / "retirement-execution-max-warmup.jsonl").read_bytes()
        warmup_shard = {"path": "retirement-execution-max-warmup.jsonl",
            "bytes": len(warmup_line), "sha256": hashlib.sha256(warmup_line).hexdigest(),
            "records": 1}
        warmup = list(binding._execution_trace_records(self.root, [warmup_shard], 1))[0]
        self.assertEqual(len(warmup_line), 1018)
        self.assertEqual((warmup["sequence"], warmup["group"], warmup["phase"], warmup["warmup"]),
                         (399999, 99999, "warmup", 1))
        self.assertIsNone(warmup["round"])
        self.assertIsNone(warmup["pair"])
        self.assertIsNone(warmup["position"])
        self.assertIsNone(warmup["code_section_bytes"])
        self.assertEqual(warmup["exit_code"], 255)
        self.assertEqual(warmup["peak_rss_bytes"], 2**53 - 1)
        self.assertEqual(warmup["metrics_artifact"], event["metrics_artifact"])
        self.assertEqual(warmup["process_instance_sha256"], event["process_instance_sha256"])

        runtime_line = (self.root / "retirement-execution-max-runtime.jsonl").read_bytes()
        runtime_shard = {"path": "retirement-execution-max-runtime.jsonl",
            "bytes": len(runtime_line), "sha256": hashlib.sha256(runtime_line).hexdigest(),
            "records": 1}
        runtime = list(binding._execution_trace_records(self.root, [runtime_shard], 1))[0]
        self.assertLess(len(runtime_line), len(transcript_line))
        self.assertEqual(runtime["sequence"], 134217719)
        self.assertEqual((runtime["kind"], runtime["row"], runtime["group"]), ("runtime", 99999, None))
        self.assertIsNone(runtime["code_section_bytes"])
        self.assertIsNone(runtime["peak_rss_bytes"])
        self.assertIsNone(runtime["metrics_artifact"])
        self.assertEqual(runtime["exit_code"], 0)
        self.assertEqual(runtime["process_instance_sha256"], binding._process_instance_digest(
            "a" * 128, 2**64 - 1, "b" * 128, 2**64 - 1, str(2**64 - 1)))

        sample_line = (self.root / "retirement-samples-max.jsonl").read_bytes()
        self.assertEqual(len(sample_line), 330)
        sample = json.loads(sample_line)
        self.assertEqual(sample_line, _canonical_line(sample))
        self.assertEqual((sample["row"], sample["round"], sample["pair"]), (99999, 1, 253))
        measurements = sample["measurements"]
        self.assertEqual(set(measurements), set(binding.ROW_SAMPLE_METRICS))
        self.assertEqual(measurements["compiler_peak_memory"], {
            "baseline": 2**53 - 1, "candidate": 2**53 - 1})
        self.assertEqual(measurements["compiler_wall_time"], {
            "baseline": 86399.999999999, "candidate": 86399.999999999})
        self.assertEqual(measurements["generated_runtime"], {
            "baseline": 86399.999999999, "candidate": 86399.999999999})
        rows = {99999: {"row": 99999, "metrics": {
            "compiler_peak_memory": True, "compiler_wall_time": True,
            "generated_code_bytes": True, "generated_runtime": True}}}
        seen, digest = [0], hashlib.sha256()
        binding._consume_result_record(sample, {99999: 0}, rows, 2, 254, 507, seen, digest)
        self.assertEqual(seen, [1])
        self.assertEqual(digest.hexdigest(), hashlib.sha256(sample_line).hexdigest())

        batch_line = (self.root / "retirement-batches-max.jsonl").read_bytes()
        self.assertEqual(len(batch_line), 266)
        batch = json.loads(batch_line)
        self.assertEqual(batch_line, _canonical_line(batch))
        self.assertEqual((batch["group"], batch["round"], batch["pair"]), (99999, 1, 253))
        self.assertEqual(batch["measurements"], {
            "compiler_batch_peak_rss": {"baseline": 2**53 - 1, "candidate": 2**53 - 1},
            "compiler_batch_wall_time": {"baseline": 86399.999999999, "candidate": 86399.999999999}})
        seen, digest = [0], hashlib.sha256()
        binding._consume_batch_record(batch, {99999: 0}, 2, 254, 507, seen, digest)
        self.assertEqual(seen, [1])
        self.assertEqual(digest.hexdigest(), hashlib.sha256(batch_line).hexdigest())

        # These are source-domain line upper bounds: each serializer fixture
        # sets all its independently bounded fields to their maximal width.
        # A complete transcript shard has at most 65536 records and a numeric
        # shard at most 131072; the native cap for either is 64 MiB.
        # Object-batch warmups are one byte longer than the sampled record:
        # three null schedule fields outweigh their shorter sequence number.
        transcript_upper = 1018 * 65536
        sample_upper = 330 * 131072
        batch_upper = 266 * 131072
        self.assertEqual((transcript_upper, sample_upper, batch_upper), (66715648, 43253760, 34865152))
        self.assertLessEqual(transcript_upper, 67108864)
        self.assertLessEqual(sample_upper, 67108864)
        self.assertLessEqual(batch_upper, 67108864)

    def test_native_receipt_binds_two_complete_transcript_shards(self):
        data = (self.root / "retirement-invocation-receipt.json").read_bytes()
        receipt = json.loads(data)
        self.assertEqual(data, _canonical_line(receipt))
        self.assertEqual(receipt["schema"], binding.EXECUTION_RECEIPT_SCHEMA)
        self.assertEqual(receipt["context_sha256"], "b" * 64)
        self.assertEqual(receipt["execution_plan_sha256"], "a" * 64)
        self.assertEqual((receipt["job_id"], receipt["attempt"], receipt["boot_id"]),
                         ("job-1", 2, "boot-123"))
        self.assertEqual(receipt["invocations"], 66368)
        self.assertEqual([shard["records"] for shard in receipt["shards"]], [65536, 832])
        records = binding._execution_trace_records(self.root, receipt["shards"], 66368)
        try:
            for index, record in enumerate(records):
                self.assertEqual(record["sequence"], index)
                self.assertGreater(record["started_ns"], receipt["bound_at_ns"])
                self.assertLess(record["finished_ns"], receipt["completed_at_ns"])
        finally:
            records.close()

    def test_exact_nanosecond_serialization_is_canonical(self):
        lines = (self.root / "retirement-seconds.tsv").read_text().splitlines()
        self.assertEqual(len(lines), 110001)
        for line in lines:
            nanoseconds, text = line.split("\t")
            value = json.loads(text)
            self.assertEqual(json.dumps(value, separators=(",", ":")), text)
            self.assertEqual(binding.Decimal(str(value)) * 1_000_000_000, int(nanoseconds))

    def test_native_sample_bytes_and_manifest_are_canonical(self):
        for prefix, data in (("samples", self.sample_data), ("batches", self.batch_data)):
            path = self.root / f"retirement-{prefix}.manifest.json"
            manifest_bytes = path.read_bytes()
            manifest = json.loads(manifest_bytes)
            self.assertEqual(manifest_bytes, _canonical_line(manifest))
            self.assertEqual(manifest, {
                "schema": result_input.MANIFEST_SCHEMA, "version": 1,
                "identity_field": "record_id", "shards": [{
                    "identity": f"{prefix}-0000", "path": f"retirement-{prefix}-0000.jsonl",
                    "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}]})
        for ordinal, line in enumerate(self.sample_data.splitlines(keepends=True)):
            value = json.loads(line)
            self.assertEqual(line, _canonical_line(value))
            self.assertEqual((value["row"], value["round"], value["pair"]),
                             ((0, 6, 10)[ordinal // 120], ordinal // 60 % 2, ordinal % 60))
            self.assertEqual("generated_runtime" in value["measurements"], value["row"] != 6)
            self.assertNotIn("generated_code_bytes", value["measurements"])
        self.assertEqual(ordinal + 1, 360)
        for ordinal, line in enumerate(self.batch_data.splitlines(keepends=True)):
            value = json.loads(line)
            self.assertEqual(line, _canonical_line(value))
            self.assertEqual((value["group"], value["round"], value["pair"]), (1, ordinal // 60, ordinal % 60))
            self.assertEqual(set(value["measurements"]), set(binding.BATCH_METRICS))
        self.assertEqual(ordinal + 1, 120)

    @unittest.skipUnless(os.name == "posix", "production no-follow result reader requires POSIX")
    def test_native_sample_shards_pass_production_integrity_reader(self):
        for prefix, data, records in (("samples", self.sample_data, 360), ("batches", self.batch_data, 120)):
            receipt = result_input.verify(self.root.resolve(), f"retirement-{prefix}.manifest.json")
            self.assertEqual(receipt["records"], records)
            self.assertEqual(receipt["scope"], "integrity-only")
            self.assertEqual(receipt["shards"][0]["sha256"], hashlib.sha256(data).hexdigest())

    @unittest.skipUnless(os.name == "posix", "production no-follow result reader requires POSIX")
    def test_native_sample_boundary_and_applicability(self):
        root = (self.root / "retirement-samples-boundary").resolve()
        rows = {row: {"row": row, "metrics": {"compiler_peak_memory": True,
            "compiler_wall_time": True, "generated_runtime": row % 3 != 1}} for row in range(1093)}
        ordinal_map = {row: row for row in rows}
        digest, seen = hashlib.sha256(), [0]

        def consume(_shard, _ordinal, value):
            binding._consume_result_record(value, ordinal_map, rows, 2, 60, 0, seen, digest)

        receipt = result_input.verify(root, "retirement-samples.manifest.json", record_consumer=consume)
        self.assertEqual(receipt["records"], 131160)
        self.assertEqual(seen[0], 131160)
        self.assertEqual([shard["records"] for shard in receipt["shards"]], [131072, 88])
        expected = hashlib.sha256()
        for shard in receipt["shards"]:
            with (root / shard["path"]).open("rb") as stream:
                for line in stream:
                    value = json.loads(line)
                    self.assertEqual(line, _canonical_line(value))
                    expected.update(line)
        self.assertEqual(digest.hexdigest(), expected.hexdigest())
        # The object groups of the same layout stream as the batch population.
        group_ordinals = _boundary_object_groups(1093)
        self.assertEqual(len(group_ordinals), 91)
        batch_digest, batch_seen = hashlib.sha256(), [0]

        def consume_batch(_shard, _ordinal, value):
            binding._consume_batch_record(value, group_ordinals, 2, 60, 0, batch_seen, batch_digest)

        receipt = result_input.verify(root, "retirement-batches.manifest.json", record_consumer=consume_batch)
        self.assertEqual((receipt["records"], batch_seen[0]), (10920, 10920))
        self.assertEqual(batch_digest.hexdigest(), hashlib.sha256(
            (root / receipt["shards"][0]["path"]).read_bytes()).hexdigest())

    def test_native_manifest_full_cap_partitions(self):
        # These are explicitly synthetic descriptor-only fixtures: this checks
        # maximum-capacity manifest layout, not 39 million collected samples.
        # (A1) The union of both populations fills the three-partition bound:
        # two full row partitions, then the batch partition.
        counts, number = [], 0
        for partition, (prefix, first) in enumerate((("samples", 0), ("samples", 128), ("batches", 0))):
            path = f"retirement-partition-{partition}.manifest.json"
            data = (self.root / path).read_bytes()
            value = json.loads(data)
            self.assertEqual(data, _canonical_line(value))
            identity, shards, _bytes = result_input._manifest(value, path, len(data), result_input.Limits())
            self.assertEqual(identity, "record_id")
            records = 0
            for index, shard in enumerate(shards):
                self.assertEqual(shard["identity"], f"{prefix}-{first + index:04d}")
                self.assertEqual(shard["path"], f"retirement-{prefix}-{first + index:04d}.jsonl")
                self.assertEqual(shard["bytes"] % 500, 0)
                records += shard["bytes"] // 500
                number += 1
            counts.append(records)
        self.assertEqual(counts, [16777216, 16777216, 5963776])
        self.assertEqual(number, 302)
        self.assertEqual(sum(counts), 39518208)
        self.assertEqual(sum(counts), binding.RESULT_INPUT_MAX_TOTAL_RECORDS)
        self.assertEqual(len(counts), binding.RESULT_INPUT_MAX_PARTITIONS)

    def test_full_invocation_replay_joins_native_observations(self):
        events = list(binding._execution_trace_records(self.root, [self.shard], 1220))
        rows = _native_rows()
        oracles = []
        for row in rows:
            native = row["metrics"]["generated_runtime"]
            oracles.append({"row": row["row"], "code_section_status": "parsed-deterministic",
                            "code_section_bytes": 32, "code_section_sha256": "d" * 64,
                            "runtime_oracle_status": "passed-native" if native else "not-applicable",
                            "runtime_exit_code": 0 if native else -1, "native_runtime": native})
        rules = binding_tests.BindingTests._rules()
        rules["sampling"].update(seed=1, rounds=2, pairs_per_round=60, warmups_per_variant=2)
        with tempfile.TemporaryDirectory(prefix="native-execution-replay-") as directory:
            root = Path(directory)

            def put(name, value):
                data = _canonical_line(value)
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
            groups = binding._batch_groups(rows)
            row_by_row = {row["row"]: row for row in rows}
            contracts = []
            for row, oracle in zip(rows, oracles):
                runtime = row["metrics"]["generated_runtime"]
                side = {"compiler_command_sha256": "b" * 64, "artifact_sha256": "c" * 64,
                        "reproduction_sha256": None,
                        "code_section_sha256": "d" * 64, "code_section_bytes": 32,
                        "runtime_command_sha256": "b" * 64 if runtime else None,
                        "runtime_output_sha256": "c" * 64 if runtime else None}
                contracts.append({"row": row["row"],
                                  "identity_sha256": binding._canonical_json_digest(row["identity"]),
                                  "oracle_sha256": binding._canonical_json_digest(oracle),
                                  "group": next(group["group"] for group in groups if row["row"] in group["rows"]),
                                  "baseline": side, "candidate": side})
            group_contracts = []
            for group in groups:
                identity = group["identity"]
                object_group = group["kind"] == binding.OBJECT_BATCH_GROUP
                group_contracts.append({
                    "group": group["group"], "kind": group["kind"], "target": identity["target"],
                    "configuration": {field: identity[field] for field in ("allocator", "frontend_lowering", "PIC")},
                    "recipe": {field: identity[field] for field in ("fixture_recipe", "cpu", "cpu_features")},
                    "members": [{"row": row, "diagnostic_sha256": EMPTY_SHA256 if object_group else None}
                                for row in group["rows"]],
                    "controls": [],
                    "input_list_sha256": hashlib.sha256(binding._input_list_bytes(
                        [f"tests/native-execution-{rows.index(row_by_row[item])}.c"
                         for item in group["rows"]], "list")).hexdigest() if object_group else None,
                    "metrics_bytes_max": 4096 + 16384 * len(group["rows"]) if object_group else None,
                    "baseline": {"command_sha256": "b" * 64, "exit_status": 0},
                    "candidate": {"command_sha256": "b" * 64, "exit_status": 0}})
            plan = put("plan.json", {"schema": binding.EXECUTION_PLAN_SCHEMA, "version": 1,
                "schedule": binding.EXECUTION_SCHEDULE, "seed": 1, "rounds": 2,
                "pairs_per_round": 60, "warmups_per_variant": 2, "cpu": 2,
                "native_target": binding.NATIVE_TIMED_TARGET,
                "performance_rows_sha256": "a" * 64, "campaign_budget": self.campaign_budget(),
                "rows": contracts, "groups": group_contracts, "untimed_groups": []})
            (root / self.shard["path"]).write_bytes(self.data)
            metrics_paths = []
            for event in events:
                if event["metrics_artifact"] is not None:
                    metrics_paths.append(event["metrics_artifact"]["path"])
            self.assertEqual((len(metrics_paths), set(metrics_paths)),
                             (244, {"retirement-metrics-rec-0000.txt"}))
            shutil.copyfile(self.root / metrics_paths[0], root / metrics_paths[0])
            raw = hashlib.sha256(self.sample_data + self.batch_data).hexdigest()
            receipt = put("receipt.json", {"schema": binding.EXECUTION_RECEIPT_SCHEMA, "version": 1,
                "context_sha256": binding._canonical_json_digest(binding._execution_context(record, raw)),
                "execution_plan_sha256": plan["sha256"], "job_id": "job-1", "attempt": 2,
                "boot_id": "boot-123", "bound_at_ns": 1000,
                "completed_at_ns": events[-1]["finished_ns"] + 1, "invocations": 1220,
                "shards": [self.shard]})
            with closing(sqlite3.connect(":memory:")) as db:
                db.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                           "metric TEXT, baseline TEXT, candidate TEXT, PRIMARY KEY(row_id, round_id, pair_id, metric))")
                db.execute("CREATE TABLE batch_samples(group_id INTEGER, round_id INTEGER, pair_id INTEGER, "
                           "metric TEXT, baseline TEXT, candidate TEXT, "
                           "PRIMARY KEY(group_id, round_id, pair_id, metric))")
                row_by_id = {row["row"]: row for row in rows}
                row_ordinals = {row["row"]: ordinal for ordinal, row in enumerate(rows)}
                measurement_digest, seen = hashlib.sha256(), [0]
                for line in self.sample_data.splitlines():
                    binding._consume_result_record(json.loads(line), row_ordinals, row_by_id,
                        2, 60, 0, seen, measurement_digest, db)
                batch_seen = [0]
                for line in self.batch_data.splitlines():
                    binding._consume_batch_record(json.loads(line), {1: 0}, 2, 60, 0, batch_seen,
                        measurement_digest, db)
                self.assertEqual((seen[0], batch_seen[0]), (360, 120))
                self.assertEqual(measurement_digest.hexdigest(), raw)

                def check():
                    return binding._check_execution_transcript(root, receipt, plan, record, rows,
                        rules["sampling"], db, raw, receipt["sha256"], 2, binding.NATIVE_TIMED_TARGET)

                result = check()
                self.assertEqual(result["invocations"], 1220)
                # Numeric samples cannot be silently replaced with positive
                # values: a singleton process sample, an object member's
                # per-input sample, or a batch process sample.
                for table, where in (("samples", "row_id=0"), ("samples", "row_id=6"),
                                     ("batch_samples", "group_id=1")):
                    original = db.execute(f"SELECT metric, baseline FROM {table} WHERE {where} "
                                          "AND round_id=0 AND pair_id=0").fetchall()
                    db.execute(f"UPDATE {table} SET baseline='99' WHERE {where} AND round_id=0 AND pair_id=0")
                    with self.subTest(table=table, where=where), \
                            self.assertRaisesRegex(ValueError, "not the authenticated invocation"):
                        check()
                    for metric, baseline in original:
                        db.execute(f"UPDATE {table} SET baseline=? WHERE {where} AND round_id=0 "
                                   "AND pair_id=0 AND metric=?", (baseline, metric))
                self.assertEqual(check()["invocations"], 1220)
                # A replaced metrics artifact no longer matches its descriptor.
                target = root / metrics_paths[0]
                data = target.read_bytes()
                target.write_bytes(data.replace(b" keep_going=1 ", b" keep_going=0 "))
                with self.assertRaisesRegex(ValueError, "per-input metrics 4 differs from its authenticated"):
                    check()

    def campaign_budget(self):
        """The C test budget's canonical record, as the plan binds it. Only the
        Linux measurement self-test produces it, so the replay that binds it is
        Linux-only too; on Linux a missing record is still an error."""
        path = self.root / "retirement-campaign-budget.txt"
        if sys.platform != "linux" and not path.exists():
            self.skipTest("the campaign budget producer requires Linux")
        record = path.read_text(encoding="ascii")
        return {"record": record, "sha256": hashlib.sha256(record.encode("ascii")).hexdigest()}

    @unittest.skipUnless(sys.platform == "linux", "descriptor-bound execution requires Linux")
    def test_campaign_budget_record(self):
        """(L7) The producer's canonical budget record parses in the validator,
        which rejects any other digest, spelling, order or missing bound."""
        budget = self.campaign_budget()
        scalars = binding._campaign_budget(budget)
        self.assertEqual((scalars["metrics-header-bytes"], scalars["metrics-input-bytes"]), (4096, 16384))
        record = budget["record"]
        edits = (("metrics-input-bytes=16384", "metrics-input-bytes=016384"),
                 ("\nbatch=1:40000000", "\nbatch=4:40000000"),
                 ("singleton=link:45000000\n", ""),
                 ("untimed-singleton=link:60000000\n", ""),
                 ("\nsingleton=link:45000000\nsingleton=self-host-stage1:900000000",
                  "\nsingleton=self-host-stage1:900000000\nsingleton=link:45000000"),
                 ("derivation=fixed", "derivation=other"),
                 ("schema=tp-retirement-campaign-budget-v2", "schema=tp-retirement-campaign-budget-v1"))
        for old, new in edits:
            with self.subTest(old=old):
                self.assertIn(old, record)
                changed = record.replace(old, new, 1)
                with self.assertRaises(ValueError):
                    binding._campaign_budget({"record": changed,
                                              "sha256": hashlib.sha256(changed.encode()).hexdigest()})
        with self.assertRaisesRegex(ValueError, "digest of its record"):
            binding._campaign_budget(dict(budget, sha256="e" * 64))

    @unittest.skipUnless(sys.platform == "linux", "descriptor-bound execution requires Linux")
    def test_real_untimed_code_artifact_batches(self):
        """Untimed production and reproduction batches: sealed records and shards."""
        data = (self.root / "retirement-untimed-batches.jsonl").read_bytes()
        descriptor = {"path": "retirement-untimed-batches.jsonl", "bytes": len(data),
                      "sha256": hashlib.sha256(data).hexdigest(), "records": 4}
        records = binding._read_jsonl_evidence(self.root, descriptor, "untimed batch records")
        artifact_digest = hashlib.sha256((self.root / "retirement-artifact-1-1.bin").read_bytes()).hexdigest()
        control = {"fixture": "tests/control.c", "row": None, "status": "rejected",
                   "error": "driver.analysis", "diagnostic_sha256": "b" * 64, "object_sha256": None}
        contract = {"members": [{"row": row, "diagnostic_sha256": EMPTY_SHA256} for row in (0, 1)],
                    "controls": [control], "metrics_bytes_max": 4096 + 3 * 16384,
                    "configuration": {"allocator": "none", "frontend_lowering": "direct-ssa", "PIC": "0"}}
        rows = _native_rows(row_ids=(0, 1), object_rows=(0, 1))
        for index, row in enumerate(rows):
            row["identity"]["fixture"] = ("tests/alpha.c", "tests/beta.c")[index]
        row_contracts = {row: {variant: {"artifact_sha256": artifact_digest, "reproduction_sha256": artifact_digest}
                               for variant in ("baseline", "candidate")} for row in (0, 1)}
        shards = binding._MetricsShards(self.root)
        self.assertEqual([(record["variant"], record["purpose"]) for record in records],
                         [(variant, purpose) for variant in ("baseline", "candidate")
                          for purpose in binding.UNTIMED_BATCH_PURPOSES])
        instances = set()
        previous = 0
        for record in records:
            self.assertEqual(set(record), set(binding.UNTIMED_BATCH_FIELDS))
            self.assertEqual((record["group"], record["exit_status"]), (0, 1))
            self.assertEqual(record["process_instance_sha256"], binding._process_instance_digest(
                "job-1", 2, "boot-123", record["pid"], record["process_start_token"]))
            self.assertNotIn(record["process_instance_sha256"], instances)
            instances.add(record["process_instance_sha256"])
            self.assertGreater(record["started_ns"], previous)
            previous = record["finished_ns"]
            field = "artifact_sha256" if record["purpose"] == "production" else "reproduction_sha256"
            frozen = binding._frozen_batch_inputs(contract, row_contracts, {row["row"]: row for row in rows},
                                                  record["variant"], field)
            self.assertEqual(record["output_sha256"],
                             binding._batch_output_digest([artifact_digest, artifact_digest, None]))
            feed = shards.add(record["metrics_artifact"], "untimed metrics")
            members = binding._check_batch_metrics(
                self.root, record["metrics_artifact"], contract, frozen, 1, binding.NATIVE_TIMED_TARGET,
                record["finished_ns"] - record["started_ns"], "untimed batch metrics", feed)
            self.assertEqual(set(members), {0, 1})
        self.assertEqual([item["path"] for item in shards.finish()], ["retirement-metrics-untimed-0000.txt"])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: retirement_execution_test.py NATIVE_TEST_OUTPUT_DIRECTORY")
    unittest.main(argv=[sys.argv[0]], verbosity=2)
