#!/usr/bin/env python3
"""Failure tests for the retirement export handoff on real throughput output.

Run after `bench_throughput self-test`, with its output directory (normally
build/throughput-tool-tests) as the only argument. Every fixture here is the
native self-test's own bytes: the A1 batch execution records and the 64 MiB
metrics shard they index, the untimed code-artifact batches and their shard,
the two-shard execution receipt, and the two-shard numeric sample manifest.
The tests reorder, drop, truncate, forge or substitute those bytes and require
the production readers (native_retirement_performance_binding.py,
native_retirement_result_input.py) and this lane's publication path to reject
them. A final test carries real output through the offline test publication,
separate retrieval and unpack format up to the production binding validator.
It stops there by design: no composer-produced binding record exists yet
(retirement_export_replay.COMPOSER_BINDING_PATH). Nothing here is a full
service bundle, a performance result or #512 evidence.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import retirement_export_replay as replay  # noqa: E402
import native_retirement_performance_binding as binding  # noqa: E402
import native_retirement_result_input as result_input  # noqa: E402

ROOT = None
REPOSITORY = HERE.parents[1]
TIMED_EXECUTION = "retirement-measured-batch-execution.jsonl"
TIMED_SHARD = "retirement-metrics-mb-0000.txt"
UNTIMED_RECORDS = "retirement-untimed-batches.jsonl"
UNTIMED_SHARD = "retirement-metrics-untimed-0000.txt"
RECEIPT = "retirement-invocation-receipt.json"
SAMPLES = "retirement-samples-boundary"


def _canonical_line(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _descriptor(root, path):
    data = (Path(root) / path).read_bytes()
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _timed_artifacts(root):
    data = (root / TIMED_EXECUTION).read_bytes()
    shard = {"path": TIMED_EXECUTION, "bytes": len(data),
             "sha256": hashlib.sha256(data).hexdigest(), "records": 244}
    return [event["metrics_artifact"]
            for event in binding._execution_trace_records(root, [shard], 244)]


def _untimed_artifacts(root):
    records = binding._read_jsonl_evidence(root, dict(_descriptor(root, UNTIMED_RECORDS), records=4),
                                           "untimed batch records")
    return [record["metrics_artifact"] for record in records]


def _stream(tracker, artifacts):
    for index, artifact in enumerate(artifacts):
        tracker.stream(artifact, f"real metrics artifact {index}")
    return tracker.finish()


def _write_archive(path, root, names, job, attempt, full_sha256, outcome=1, validity=1):
    """Wrap real files in the documented BQEXP001 receipt + archive layout."""
    body = bytearray()
    file_bytes = 0
    for name in sorted(names, key=lambda item: item.encode()):
        data = (root / name).read_bytes()
        encoded = name.encode()
        body += struct.pack("<IIQ", 2, len(encoded), len(data)) + encoded + data
        file_bytes += len(data)
    receipt = bytearray(replay.RECEIPT_BYTES)
    receipt[:8] = b"BQEXP001"
    struct.pack_into("<QQQQII", receipt, 8, job, attempt, len(body), file_bytes, len(names), len(names))
    receipt[240:304] = full_sha256.encode()
    receipt[304:368] = hashlib.sha256(body).hexdigest().encode()
    receipt[560:608] = replay.RECIPE.ljust(48, b"\0")
    struct.pack_into("<III", receipt, 1012, outcome, validity, 7)
    path.write_bytes(bytes(receipt) + bytes(body))
    return hashlib.sha256(receipt).hexdigest()


# A test double for `bench_service unpack-export`: it checks the pinned
# receipt and archive digest and reconstructs regular files in order. The
# reviewed native unpacker additionally runs the worker's bundle validators.
UNPACKER = r'''
import hashlib, os, struct, sys
_, command, archive, destination, receipt_sha256 = sys.argv
assert command == "unpack-export"
data = open(archive, "rb").read()
receipt, body = data[:1024], data[1024:]
assert hashlib.sha256(receipt).hexdigest() == receipt_sha256
assert receipt[304:368] == hashlib.sha256(body).hexdigest().encode()
os.mkdir(destination, 0o700)
offset, previous = 0, b""
while offset < len(body):
    kind, length, size = struct.unpack_from("<IIQ", body, offset)
    offset += 16
    name = body[offset:offset + length]
    offset += length
    assert kind == 2 and b"/" not in name and name > previous
    previous = name
    with open(os.path.join(destination, name.decode()), "xb") as output:
        output.write(body[offset:offset + size])
    offset += size
assert offset == len(body)
'''


class RealThroughputOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = ROOT
        for name in (TIMED_EXECUTION, TIMED_SHARD, UNTIMED_RECORDS, UNTIMED_SHARD, RECEIPT):
            if not (cls.root / name).is_file():
                raise RuntimeError(f"run `bench_throughput self-test` first: {name} is missing")

    def setUp(self):
        # #615's reader revalidates every ancestor directory's metadata after
        # streaming; keep the work tree beside the throughput output, away
        # from a shared temporary directory that other processes modify.
        self.temporary = tempfile.TemporaryDirectory(prefix="retirement-export-real-", dir=self.root.parent)
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def copy(self, *names):
        target = self.work / "evidence"
        target.mkdir(exist_ok=True)
        for name in names:
            shutil.copyfile(self.root / name, target / name)
        return target

    # -- A1 64 MiB metrics shards ------------------------------------------------

    def test_real_timed_and_untimed_metrics_shards_tile_in_record_order(self):
        timed = binding._MetricsShards(self.root)
        self.assertEqual([item["path"] for item in _stream(timed, _timed_artifacts(self.root))],
                         [TIMED_SHARD])
        untimed = timed.fork()
        self.assertEqual([item["path"] for item in _stream(untimed, _untimed_artifacts(self.root))],
                         [UNTIMED_SHARD])

    def test_reordered_metrics_artifacts_are_rejected(self):
        artifacts = _timed_artifacts(self.root)
        artifacts[3], artifacts[4] = artifacts[4], artifacts[3]
        with self.assertRaisesRegex(ValueError, "not packed contiguously"):
            _stream(binding._MetricsShards(self.root), artifacts)
        untimed = _untimed_artifacts(self.root)
        untimed.reverse()
        with self.assertRaisesRegex(ValueError, "not packed contiguously"):
            _stream(binding._MetricsShards(self.root), untimed)

    def test_missing_metrics_artifact_or_shard_is_rejected(self):
        artifacts = _timed_artifacts(self.root)
        with self.assertRaisesRegex(ValueError, "not packed contiguously"):
            _stream(binding._MetricsShards(self.root), artifacts[:7] + artifacts[8:])
        with self.assertRaisesRegex(ValueError, "not all streamed|outside its authenticated"):
            _stream(binding._MetricsShards(self.root), artifacts[:-1])
        evidence = self.copy(UNTIMED_SHARD)
        (evidence / UNTIMED_SHARD).unlink()
        with self.assertRaisesRegex(ValueError, "shard is missing"):
            _stream(binding._MetricsShards(evidence), _untimed_artifacts(self.root))

    def test_truncated_or_extended_metrics_shard_is_rejected(self):
        evidence = self.copy(TIMED_SHARD)
        shard = evidence / TIMED_SHARD
        data = shard.read_bytes()
        shard.write_bytes(data[:-1])
        with self.assertRaisesRegex(ValueError, "outside its metrics shard|authenticated digest"):
            _stream(binding._MetricsShards(evidence), _timed_artifacts(self.root))
        shard.write_bytes(data + b"CC_METRICS smuggled\n")
        with self.assertRaisesRegex(ValueError, "outside its authenticated artifacts"):
            _stream(binding._MetricsShards(evidence), _timed_artifacts(self.root))

    def test_out_of_sequence_or_shared_metrics_shards_are_rejected(self):
        evidence = self.copy(TIMED_SHARD)
        renamed = "retirement-metrics-mb-0001.txt"
        (evidence / TIMED_SHARD).rename(evidence / renamed)
        artifacts = [dict(item, path=renamed) for item in _timed_artifacts(self.root)]
        with self.assertRaisesRegex(ValueError, "does not continue its writer's metrics shard sequence"):
            _stream(binding._MetricsShards(evidence), artifacts)
        # Timed and untimed writers never share a shard or a tag.
        timed = binding._MetricsShards(self.root)
        _stream(timed, _timed_artifacts(self.root))
        untimed = timed.fork()
        with self.assertRaisesRegex(ValueError, "fresh metrics shard"):
            untimed.add(_timed_artifacts(self.root)[0], "untimed reuse of a timed shard")
        foreign = dict(_untimed_artifacts(self.root)[0], path="retirement-metrics-mb-0007.txt")
        with self.assertRaisesRegex(ValueError, "reuses another writer's metrics shard tag"):
            timed.fork().add(foreign, "untimed batch with the timed tag")

    def test_real_shards_fit_one_export_entry_each(self):
        """A full metrics or transcript shard is admissible at the export cap."""
        limits = __import__("retirement_capacity").source_limits(REPOSITORY)
        self.assertEqual(limits["metrics_shard_bytes"], replay.FILE_CAP)
        self.assertEqual(limits["transcript_bytes_per_shard"], replay.FILE_CAP)
        self.assertEqual(limits["store_total_bytes"], replay.RESULT_FILE_CAP)
        self.assertLessEqual((self.root / TIMED_SHARD).stat().st_size, replay.FILE_CAP)
        self.assertLessEqual((self.root / "retirement-shard-0.jsonl").stat().st_size, replay.FILE_CAP)
        # The real untimed record file is far below its model bound and the cap.
        records = (self.root / UNTIMED_RECORDS).read_bytes().splitlines(keepends=True)
        self.assertTrue(all(len(line) <= limits["untimed_record_bytes_max"] for line in records))

    # -- execution receipt transcript shards ---------------------------------------

    def receipt(self):
        return json.loads((self.root / RECEIPT).read_bytes())

    def test_missing_duplicate_or_reordered_transcript_shards_are_rejected(self):
        receipt = self.receipt()
        shards, total = receipt["shards"], receipt["invocations"]
        with self.assertRaisesRegex(ValueError, "omits required invocations"):
            list(binding._execution_trace_records(self.root, shards[:1], total))
        with self.assertRaisesRegex(ValueError, "duplicate shard paths"):
            list(binding._execution_trace_records(self.root, [shards[1], shards[1]], total))
        # Paths swapped, declared record counts left in place.
        swapped = [dict(shards[0], path=shards[1]["path"]), dict(shards[1], path=shards[0]["path"])]
        with self.assertRaisesRegex(ValueError, "byte count|truncated|extra invocations"):
            list(binding._execution_trace_records(self.root, swapped, total))
        # Whole descriptors reordered: every byte authenticates, but the first
        # observation is not sequence 0. _check_execution_transcript compares
        # each record's `sequence` (first) with the frozen seeded schedule.
        records = binding._execution_trace_records(self.root, [shards[1], shards[0]], total)
        try:
            self.assertNotEqual(next(records)["sequence"], 0)
        finally:
            records.close()

    # -- numeric sample shards -----------------------------------------------------

    def sample_root(self):
        source = self.root / SAMPLES
        if not (source / "retirement-samples.manifest.json").is_file():
            self.skipTest("boundary sample manifest is missing")
        target = self.work / SAMPLES
        shutil.copytree(source, target)
        return target

    def stream_samples(self, root):
        rows = {row: {"row": row, "metrics": {"compiler_peak_memory": True, "compiler_wall_time": True,
                                               "generated_runtime": row % 3 != 1}} for row in range(1093)}
        ordinals = {row: row for row in rows}
        digest, seen = hashlib.sha256(), [0]

        def consume(_shard, _ordinal, value):
            binding._consume_result_record(value, ordinals, rows, 2, 60, 0, seen, digest)

        receipt = result_input.verify(root.resolve(), "retirement-samples.manifest.json",
                                      record_consumer=consume)
        return receipt, seen[0]

    def rewrite_manifest(self, root, shards):
        manifest = root / "retirement-samples.manifest.json"
        value = json.loads(manifest.read_bytes())
        value["shards"] = shards
        manifest.write_bytes(_canonical_line(value))
        return value

    def test_reordered_or_missing_sample_shards_are_rejected(self):
        root = self.sample_root()
        receipt, seen = self.stream_samples(root)
        planned = 131160
        self.assertEqual((receipt["records"], seen), (planned, planned))
        shards = json.loads((root / "retirement-samples.manifest.json").read_bytes())["shards"]
        # #615 streams shards in identity order, so a permuted list is the
        # same population; reordering means relabelling which bytes come first.
        self.rewrite_manifest(root, [shards[1], shards[0]])
        self.assertEqual(self.stream_samples(root)[1], planned)
        relabelled = [dict(shards[1], identity=shards[0]["identity"]),
                      dict(shards[0], identity=shards[1]["identity"])]
        self.rewrite_manifest(root, relabelled)
        with self.assertRaisesRegex(ValueError, "not disjoint contiguous partitions"):
            self.stream_samples(root)
        # Dropping the last shard leaves a self-consistent manifest; the
        # sealed-bundle join (_check_workflow_evidence) requires the planned
        # record count, so the short population cannot be sealed.
        self.rewrite_manifest(root, shards[:1])
        receipt, seen = self.stream_samples(root)
        self.assertNotEqual(receipt["records"], planned)
        self.assertNotEqual(seen, planned)
        self.rewrite_manifest(root, shards)
        (root / shards[1]["path"]).unlink()
        with self.assertRaises(ValueError):
            self.stream_samples(root)

    # -- forged self-consistent bundle ---------------------------------------------

    def evidence_chain(self, receipt_bytes):
        """record -> sealed result -> result bundle -> execution receipt, each
        descriptor matching its own bytes (a self-consistent bundle)."""
        root = self.work / "result"
        root.mkdir(exist_ok=True)
        (root / "execution-receipt.json").write_bytes(receipt_bytes)
        bundle = {"execution_receipt": _descriptor(root, "execution-receipt.json")}
        (root / "result-bundle.json").write_bytes(_canonical_line(bundle))
        sealed = {"result_bundle": _descriptor(root, "result-bundle.json")}
        (root / "sealed-result.json").write_bytes(_canonical_line(sealed))
        record = {"workflow": {"phases": {"sealed_result": _descriptor(root, "sealed-result.json")}}}
        (root / "binding.json").write_bytes(_canonical_line(record))
        return root

    def test_forged_self_consistent_bundle_needs_the_external_authority(self):
        genuine = (self.root / RECEIPT).read_bytes()
        authority = hashlib.sha256(genuine).hexdigest()
        value = json.loads(genuine)
        self.assertEqual((value["job_id"], value["attempt"]), ("job-1", 2))
        root = self.evidence_chain(genuine)
        joined = replay.authenticated_attempt_join(root, root / "binding.json", 1, 2, authority)
        self.assertEqual(joined["invocations"], value["invocations"])
        # The service labels the job; a bare numeric job ID is not its label.
        self.assertEqual(replay.SERVICE_JOB_LABEL.format(1), value["job_id"])
        for job, attempt in ((2, 2), (1, 3)):
            with self.subTest(job=job, attempt=attempt), \
                    self.assertRaisesRegex(ValueError, "does not identify the exported job"):
                replay.authenticated_attempt_join(root, root / "binding.json", job, attempt, authority)
        # A forged attempt with every in-bundle descriptor recomputed.
        forged = _canonical_line(dict(value, attempt=3))
        root = self.evidence_chain(forged)
        self.assertEqual(json.loads((root / "result-bundle.json").read_bytes())["execution_receipt"]["sha256"],
                         hashlib.sha256(forged).hexdigest())
        with self.assertRaisesRegex(ValueError, "independently trusted digest"):
            replay.authenticated_attempt_join(root, root / "binding.json", 1, 3, authority)

    def test_forged_self_consistent_export_never_publishes(self):
        names = [UNTIMED_RECORDS, UNTIMED_SHARD, RECEIPT]
        genuine = self.work / "genuine.bqexport"
        authority = _write_archive(genuine, self.root, names, 1, 2, "a" * 64)
        forgery_root = self.copy(*names)
        with (forgery_root / UNTIMED_SHARD).open("r+b") as stream:
            stream.seek(0)
            stream.write(b"X")
        forged = self.work / "forged.bqexport"
        forged_digest = _write_archive(forged, forgery_root, names, 1, 2, "a" * 64)
        self.assertNotEqual(forged_digest, authority)
        publication = self.work / "publication"
        publication.mkdir(mode=0o700)
        command = [sys.executable, str(Path(replay.__file__).resolve()), str(forged), "--publish-only",
                   "--test-publication", str(publication), "--bench-service", str(self.work / "unused"),
                   "--repository-root", str(REPOSITORY), "--binding", "binding.json", "--job", "1",
                   "--attempt", "2", "--full-result-sha256", "a" * 64,
                   "--export-receipt-sha256", authority,
                   "--trusted-execution-receipt-sha256", hashlib.sha256(
                       (self.root / RECEIPT).read_bytes()).hexdigest()]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("independently supplied digest", result.stderr)
        self.assertEqual(list(publication.iterdir()), [])

    # -- real bytes through publication, retrieval and unpack ---------------------

    def test_real_output_reaches_the_production_binding_validator(self):
        """Publish, retrieve in a separate consumer and unpack real output.

        The chain must stop at the production binding validator: these real
        artifacts carry no composer-produced binding record, so no replay may
        be reported. This is the hook E's composer fills.
        """
        names = [TIMED_EXECUTION, TIMED_SHARD, UNTIMED_RECORDS, UNTIMED_SHARD, RECEIPT,
                 "retirement-shard-0.jsonl", "retirement-shard-1.jsonl"]
        staged = self.evidence_chain((self.root / RECEIPT).read_bytes())
        for name in names:
            shutil.copyfile(self.root / name, staged / name)
        staged_names = sorted(path.name for path in staged.iterdir())
        archive = self.work / "download.bqexport"
        export_sha = _write_archive(archive, staged, staged_names, 1, 2, "a" * 64)
        unpacker = self.work / "reviewed-unpacker"
        unpacker.write_text(f"#!{sys.executable}\n{UNPACKER}")
        unpacker.chmod(stat.S_IRWXU)
        publication = self.work / "publication"
        publication.mkdir(mode=0o700)
        identities = ["--bench-service", str(unpacker), "--repository-root", str(REPOSITORY),
                      "--binding", "binding.json", "--job", "1", "--attempt", "2",
                      "--full-result-sha256", "a" * 64, "--export-receipt-sha256", export_sha,
                      "--trusted-execution-receipt-sha256",
                      hashlib.sha256((self.root / RECEIPT).read_bytes()).hexdigest()]
        command = [sys.executable, str(Path(replay.__file__).resolve())]
        first = subprocess.run(command + [str(archive), "--publish-only", "--test-publication",
                                          str(publication)] + identities,
                               capture_output=True, text=True, check=True)
        published = publication / "retirement-1-2.bqexport"
        self.assertEqual(json.loads(first.stdout)["test_publication"], str(published))
        consumer = self.work / "consumer"
        retrieval = consumer / "retrieval"
        retrieval.mkdir(parents=True, mode=0o700)
        second = subprocess.run(command + [str(published), str(consumer / "result"), "--consume-published",
                                           "--retrieval", str(retrieval)] + identities,
                                cwd=consumer, env={"PATH": os.environ.get("PATH", ""), "LANG": "C"},
                                capture_output=True, text=True, check=False)
        self.assertEqual((retrieval / published.name).read_bytes(), archive.read_bytes())
        for name in staged_names:
            self.assertEqual((consumer / "result" / name).read_bytes(), (staged / name).read_bytes())
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("retirement export replay failed", second.stderr)
        self.assertNotIn("verified-without-admission", second.stdout)
        self.assertIn('"retrieved_export"', second.stdout)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: retirement_export_replay_real_test.py THROUGHPUT_TEST_OUTPUT_DIRECTORY")
    ROOT = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(REPOSITORY / "tools" / "throughput"))
    unittest.main(argv=[sys.argv[0]], verbosity=2)
