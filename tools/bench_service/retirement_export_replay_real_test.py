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
them. The last tests carry real output through the CLI's offline test
publication, separate retrieval and unpack format, with the join record at
the composer's fixed binding path (retirement_export_replay.COMPOSER_BINDING_PATH).
With the real binding validator the chain stops there, because that minimal
join record is no complete binding; with a validator test double the CLI's
authenticated_attempt_join accepts the genuine receipt and refuses a wrong
attempt or a forged receipt.

WorkerUnitResultJoinTests read the worker-unit producer's composed job-82
result (#881 PR 3), which the preparation runner of `bench_service self-test`
exports to build/bench-service-tools/retirement-worker-unit-result (skipped
when absent; `--worker-unit DIRECTORY`, which build.c runs right after that
runner, runs only them and fails when it is absent): its binding sits at
COMPOSER_BINDING_PATH with the sealed-result phase pending, which the
validator's evidence check refuses until lane F's final binding names the
composed sealed result; over that final binding the join accepts the producer
authority's receipt digest for job 82 and refuses another job or attempt,
another trust root and a tampered receipt. Lane F's production writer
(retirement_lane_f.py) derives a deterministic independent-replay archive
from that result; its adapter replay refuses the fixture's placeholder harness
commit and, with only that source pin relaxed, the preparation runner's
stand-in statistics. Over the writer's directory the real validator, pinned
by the profile, and the export CLI without any validator double both refuse
the job-82 binding explicitly. The result holds the context evidence the
binding names (support files and the other prior-closure artifacts) as flat
entries (#1998, closing gap N8): lane F's replay lays them out and the
validator refuses the fixture's unapproved #508 support declaration; the
export CLI does not lay them out and refuses the support declaration's
binding path as missing. Nothing here is a full service bundle, a
performance result or #512 evidence.
"""
import contextlib
import copy
import hashlib
import io
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
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import retirement_export_replay as replay  # noqa: E402
import native_retirement_performance_binding as binding  # noqa: E402
import native_retirement_performance_binding_test as binding_tests  # noqa: E402
import native_retirement_result_input as result_input  # noqa: E402
import retirement_lane_f as lane_f  # noqa: E402

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


def _pending_record(sealed_path, replay_path, extra=None):
    """A composed record whose two late phases are the composer's pending
    descriptors (retirement_worker_compose.c)."""
    record = dict(extra or {})
    record["workflow"] = {"phases": {
        "sealed_result": dict(replay.PENDING_DESCRIPTOR, path=sealed_path),
        "independent_replay": dict(replay.PENDING_DESCRIPTOR, path=replay_path)}}
    return record


def _lane_f(directory, result, mutate=None):
    """Lane F's directory for `result`: the composed record with its sealed
    phase naming the composed sealed result and its independent-replay phase
    naming a stand-in replay record placed beside it (lane F's publication
    and replay evidence are not produced here)."""
    directory = Path(directory)
    directory.mkdir(mode=0o700)
    record = json.loads((Path(result) / replay.COMPOSER_BINDING_PATH).read_bytes())
    phases = record["workflow"]["phases"]
    sealed = (Path(result) / phases["sealed_result"]["path"]).read_bytes()
    stand_in = _canonical_line({"stand_in": "lane F independent replay"})
    (directory / phases["independent_replay"]["path"]).write_bytes(stand_in)
    phases["sealed_result"] = {"path": phases["sealed_result"]["path"], "bytes": len(sealed),
                               "sha256": hashlib.sha256(sealed).hexdigest()}
    phases["independent_replay"] = {"path": phases["independent_replay"]["path"], "bytes": len(stand_in),
                                    "sha256": hashlib.sha256(stand_in).hexdigest()}
    if mutate:
        mutate(record)
    (directory / replay.FINAL_BINDING_NAME).write_bytes(_canonical_line(record))
    return directory


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
        # Only the Linux descriptor-bound throughput self-test produces these
        # artifacts; on Linux a missing artifact is still an error.
        if sys.platform != "linux":
            raise unittest.SkipTest("the retirement producer's real output requires Linux")
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

    @staticmethod
    def receipt_schedule():
        """The production seeded schedule the self-test's receipt follows.

        tools/throughput/tests.c's test_retirement_shards runs 272 singleton
        compiler groups, no runtime rows, seed 1, two rounds of 60 pairs and
        two warmups (66,368 invocations); _execution_schedule derives the same
        order from 272 link rows through _batch_groups.
        """
        template = binding_tests.BindingTests._series_join_fixture()[0][0]
        rows = []
        for index in range(272):
            row = copy.deepcopy(template)
            row["row"] = index
            row["identity"]["fixture"] = f"tests/receipt-{index}.c"
            row["identity"]["artifact_stage"] = "link"
            row["metrics"]["generated_runtime"] = False
            rows.append(row)
        return binding._execution_schedule(rows, {"seed": 1, "rounds": 2, "pairs_per_round": 60,
                                                  "warmups_per_variant": 2})

    def first_schedule_deviation(self, shards, total):
        """Walk the transcript as _check_execution_transcript does: every
        record's schedule keys must equal the frozen seeded schedule's."""
        expected = iter(self.receipt_schedule())
        records = binding._execution_trace_records(self.root, shards, total)
        try:
            for index, value in enumerate(records):
                frozen = next(expected)
                if any(type(value[key]) is not type(identity) or value[key] != identity
                       for key, identity in frozen.items()):
                    return index
        finally:
            records.close()
        return None

    def test_missing_duplicate_or_reordered_transcript_shards_are_rejected(self):
        receipt = self.receipt()
        shards, total = receipt["shards"], receipt["invocations"]
        self.assertEqual(sum(1 for _ in self.receipt_schedule()), total)
        self.assertIsNone(self.first_schedule_deviation(shards, total))
        with self.assertRaisesRegex(ValueError, "omits required invocations"):
            list(binding._execution_trace_records(self.root, shards[:1], total))
        with self.assertRaisesRegex(ValueError, "duplicate shard paths"):
            list(binding._execution_trace_records(self.root, [shards[1], shards[1]], total))
        # Paths swapped, declared record counts left in place.
        swapped = [dict(shards[0], path=shards[1]["path"]), dict(shards[1], path=shards[0]["path"])]
        with self.assertRaisesRegex(ValueError, "byte count|truncated|extra invocations"):
            list(binding._execution_trace_records(self.root, swapped, total))
        # Whole descriptors reordered: every byte authenticates, but the very
        # first observation already deviates from the frozen schedule.
        self.assertEqual(self.first_schedule_deviation([shards[1], shards[0]], total), 0)

    # -- numeric sample shards -----------------------------------------------------

    def sample_root(self):
        source = self.root / SAMPLES
        if not (source / "retirement-samples.manifest.json").is_file():
            raise AssertionError(f"{source} lacks the self-test's boundary sample manifest")
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

    @staticmethod
    def sealed_descriptor(root, receipt):
        """The sealed-bundle manifest descriptor an honest composer writes."""
        return {"identity": "rows-0", **_descriptor(root, "retirement-samples.manifest.json"),
                "start_record": 0, "records": receipt["records"], "input_bytes": receipt["input_bytes"]}

    def test_reordered_or_missing_sample_shards_are_rejected(self):
        root = self.sample_root()
        receipt, seen = self.stream_samples(root)
        planned = [{"identity": "rows-0", "path": "retirement-samples.manifest.json",
                    "start_record": 0, "records": 131160}]
        self.assertEqual((receipt["records"], seen), (131160, 131160))
        # The pre-sample partition plan joins the intact population.
        joined = binding._result_manifest_descriptors([self.sealed_descriptor(root, receipt)], planned)
        self.assertEqual(joined[0]["records"], 131160)
        shards = json.loads((root / "retirement-samples.manifest.json").read_bytes())["shards"]
        # #615 streams shards in identity order, so a permuted list is the
        # same population; reordering means relabelling which bytes come first.
        self.rewrite_manifest(root, [shards[1], shards[0]])
        self.assertEqual(self.stream_samples(root)[1], 131160)
        relabelled = [dict(shards[1], identity=shards[0]["identity"]),
                      dict(shards[0], identity=shards[1]["identity"])]
        self.rewrite_manifest(root, relabelled)
        with self.assertRaisesRegex(ValueError, "not disjoint contiguous partitions"):
            self.stream_samples(root)
        # Dropping the last shard leaves a self-consistent #615 manifest; the
        # sealed-bundle join against the frozen partition plan refuses it.
        self.rewrite_manifest(root, shards[:1])
        short, _seen = self.stream_samples(root)
        with self.assertRaisesRegex(ValueError, "differs from the pre-sample partition plan"):
            binding._result_manifest_descriptors([self.sealed_descriptor(root, short)], planned)
        self.rewrite_manifest(root, shards)
        (root / shards[1]["path"]).unlink()
        with self.assertRaises(ValueError):
            self.stream_samples(root)

    # -- forged self-consistent bundle ---------------------------------------------

    def evidence_chain(self, receipt_bytes, name="result"):
        """record -> sealed result -> result bundle -> execution receipt, each
        descriptor matching its own bytes (a self-consistent bundle)."""
        root = self.work / name
        root.mkdir(exist_ok=True)
        (root / "execution-receipt.json").write_bytes(receipt_bytes)
        bundle = {"execution_receipt": _descriptor(root, "execution-receipt.json")}
        (root / "result-bundle.json").write_bytes(_canonical_line(bundle))
        sealed = {"result_bundle": _descriptor(root, "result-bundle.json")}
        (root / "sealed-result.json").write_bytes(_canonical_line(sealed))
        record = _pending_record("sealed-result.json", "independent-replay.json")
        (root / replay.COMPOSER_BINDING_PATH).write_bytes(_canonical_line(record))
        return root

    def final_record(self, root):
        """Lane F's final binding over an evidence chain."""
        return _lane_f(Path(str(root) + "-lane-f"), root) / replay.FINAL_BINDING_NAME

    def test_forged_self_consistent_bundle_needs_the_external_authority(self):
        genuine = (self.root / RECEIPT).read_bytes()
        authority = hashlib.sha256(genuine).hexdigest()
        value = json.loads(genuine)
        self.assertEqual((value["job_id"], value["attempt"]), ("job-1", 2))
        root = self.evidence_chain(genuine)
        final = self.final_record(root)
        joined = replay.authenticated_attempt_join(root, final, 1, 2, authority)
        self.assertEqual(joined["invocations"], value["invocations"])
        # The service labels the job; a bare numeric job ID is not its label.
        self.assertEqual(replay.SERVICE_JOB_LABEL.format(1), value["job_id"])
        for job, attempt in ((2, 2), (1, 3)):
            with self.subTest(job=job, attempt=attempt), \
                    self.assertRaisesRegex(ValueError, "does not identify the exported job"):
                replay.authenticated_attempt_join(root, final, job, attempt, authority)
        # A forged attempt with every in-bundle descriptor recomputed.
        forged = _canonical_line(dict(value, attempt=3))
        root = self.evidence_chain(forged, "forged")
        final = self.final_record(root)
        self.assertEqual(json.loads((root / "result-bundle.json").read_bytes())["execution_receipt"]["sha256"],
                         hashlib.sha256(forged).hexdigest())
        with self.assertRaisesRegex(ValueError, "independently trusted digest"):
            replay.authenticated_attempt_join(root, final, 1, 3, authority)

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
                   "--repository-root", str(REPOSITORY), "--binding", replay.COMPOSER_BINDING_PATH, "--job", "1",
                   "--attempt", "2", "--full-result-sha256", "a" * 64,
                   "--export-receipt-sha256", authority,
                   "--trusted-execution-receipt-sha256", hashlib.sha256(
                       (self.root / RECEIPT).read_bytes()).hexdigest()]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("export receipt differs from the independently supplied digest", result.stderr)
        self.assertEqual(list(publication.iterdir()), [])

    # -- real bytes through publication, retrieval and unpack ---------------------

    def run_chain(self, receipt_bytes, attempt=2, validator=None, name="chain"):
        """Publish real output, then retrieve and replay it in a separate
        consumer process, entirely through the CLI's main().

        The native `bench_service unpack-export` cannot run on these bytes:
        it requires a receipt the service itself sealed for a finalized
        service result (request, principal and profile digests) and the
        worker's full-result binding over its manifest and bundle index, and
        the blocked retirement recipe cannot finalize a service result here.
        UNPACKER is the stand-in. ``validator`` optionally replaces the
        production binding validator's path (a test double); by default the
        real validator runs.
        """
        names = [TIMED_EXECUTION, TIMED_SHARD, UNTIMED_RECORDS, UNTIMED_SHARD,
                 "retirement-shard-0.jsonl", "retirement-shard-1.jsonl"]
        staged = self.evidence_chain(receipt_bytes, name + "-staged")
        for item in names:
            shutil.copyfile(self.root / item, staged / item)
        staged_names = sorted(path.name for path in staged.iterdir())
        lane_f = _lane_f(self.work / f"{name}-lane-f", staged)
        archive = self.work / f"{name}.bqexport"
        export_sha = _write_archive(archive, staged, staged_names, 1, attempt, "a" * 64)
        unpacker = self.work / f"{name}-unpacker"
        unpacker.write_text(f"#!{sys.executable}\n{UNPACKER}")
        unpacker.chmod(stat.S_IRWXU)
        publication = self.work / f"{name}-publication"
        publication.mkdir(mode=0o700)
        identities = ["--bench-service", str(unpacker), "--repository-root", str(REPOSITORY),
                      "--binding", replay.COMPOSER_BINDING_PATH, "--job", "1", "--attempt", str(attempt),
                      "--full-result-sha256", "a" * 64, "--export-receipt-sha256", export_sha,
                      "--trusted-execution-receipt-sha256",
                      hashlib.sha256((self.root / RECEIPT).read_bytes()).hexdigest()]
        launcher = ("import sys; sys.path.insert(0, sys.argv[1]); import retirement_export_replay as r; "
                    "sys.argv = sys.argv[2:]; r.binding.__file__ = sys.argv.pop(1) or r.binding.__file__; "
                    "sys.exit(r.main())")
        command = [sys.executable, "-c", launcher, str(HERE), str(Path(replay.__file__).resolve()),
                   str(validator or "")]
        first = subprocess.run(command + [str(archive), "--publish-only", "--test-publication",
                                          str(publication)] + identities,
                               capture_output=True, text=True, check=True)
        published = publication / f"retirement-1-{attempt}.bqexport"
        self.assertEqual(json.loads(first.stdout)["test_publication"], str(published))
        consumer = self.work / f"{name}-consumer"
        retrieval = consumer / "retrieval"
        retrieval.mkdir(parents=True, mode=0o700)
        second = subprocess.run(command + [str(published), str(consumer / "result"), "--consume-published",
                                           "--retrieval", str(retrieval), "--lane-f", str(lane_f)] + identities,
                                cwd=consumer, env={"PATH": os.environ.get("PATH", ""), "LANG": "C"},
                                capture_output=True, text=True, check=False)
        self.assertEqual((retrieval / published.name).read_bytes(), archive.read_bytes())
        for item in staged_names:
            self.assertEqual((consumer / "result" / item).read_bytes(), (staged / item).read_bytes())
        self.assertIn('"retrieved_export"', second.stdout)
        return second

    def test_real_output_stops_at_the_production_binding_validator(self):
        """Lane F's final binding over the minimal join record is no complete
        binding, so the real validator must refuse it and no replay may be
        reported."""
        second = self.run_chain((self.root / RECEIPT).read_bytes())
        self.assertEqual(second.returncode, 1)
        self.assertRegex(second.stderr, r"retirement export replay failed: production binding validator "
                                        r"refused the final binding: ValueError: ")
        self.assertNotIn("verified-without-admission", second.stdout)

    def write_validator_double(self):
        """Stands in for the binding validator only, never for the join."""
        path = self.work / "validator-double.py"
        proof = {"proof": "independent-evidence-and-receipts-checked", "rows_recomputed": True,
                 "support_checked": True, "execution_checked": True, "bundle_checked": True,
                 "git_checked": True, "required_rows": 0}
        path.write_text(f"import json\nprint(json.dumps({proof!r}))\n")
        return path

    def test_cli_joins_the_real_receipt_to_the_exported_attempt(self):
        """Past the validator, the CLI's authenticated_attempt_join decides."""
        validator = self.write_validator_double()
        genuine = (self.root / RECEIPT).read_bytes()
        joined = self.run_chain(genuine, validator=validator, name="joined")
        self.assertEqual(joined.returncode, 0, joined.stderr)
        self.assertEqual(json.loads(joined.stdout.splitlines()[-1])["replay"], "verified-without-admission")
        wrong_attempt = self.run_chain(genuine, attempt=3, validator=validator, name="wrong-attempt")
        self.assertEqual(wrong_attempt.returncode, 1)
        self.assertIn("service execution receipt does not identify the exported job and attempt",
                      wrong_attempt.stderr)
        forged = _canonical_line(dict(json.loads(genuine), attempt=3))
        forged_run = self.run_chain(forged, attempt=3, validator=validator, name="forged")
        self.assertEqual(forged_run.returncode, 1)
        self.assertIn("execution_receipt bytes do not match the independently trusted digest", forged_run.stderr)


# The worker-unit producer's composed result, exported by the preparation
# runner (retirement_worker_unit_tests.h, bq_prep_worker_unit_export).
WORKER_UNIT_RESULT = REPOSITORY / "build" / "bench-service-tools" / "retirement-worker-unit-result"
# With --worker-unit (build.c runs it right after the preparation runner),
# a missing result is a failure, not a skip.
WORKER_UNIT_REQUIRED = False
# Lane F's publication identity for the worker unit's independent-replay archive.
PUBLICATION_ID = "worker-unit-lane-f"
# The context evidence files the producer publishes flat (#1998: all 39 of
# the #511 record, BQ_PREP_WORKER_UNIT_EVIDENCE).
WORKER_UNIT_EVIDENCE = 39


class WorkerUnitResultJoinTests(unittest.TestCase):
    """authenticated_attempt_join over the worker unit's own result root."""

    @classmethod
    def setUpClass(cls):
        if not (WORKER_UNIT_RESULT / "result" / replay.COMPOSER_BINDING_PATH).is_file():
            message = f"run `bench_service self-test` first: {WORKER_UNIT_RESULT} is missing"
            if WORKER_UNIT_REQUIRED:
                raise RuntimeError(message)
            raise unittest.SkipTest(message)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="retirement-worker-unit-join-",
                                                     dir=WORKER_UNIT_RESULT.parent)
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.result = self.work / "result"
        shutil.copytree(WORKER_UNIT_RESULT / "result", self.result)
        for path in self.result.iterdir():
            path.chmod(0o600)
        authority = sorted((WORKER_UNIT_RESULT / "authority").glob("authority-*.txt"))
        self.assertEqual(len(authority), 1)
        lines = authority[0].read_text(encoding="ascii").split("\n")
        # BQ-RETIREMENT-AUTHORITY-V3: job label, attempt, plan, context, receipt.
        self.label, self.attempt, self.trusted = lines[1], int(lines[2]), lines[5]
        self.job = int(self.label[len("job-"):])
        self.assertEqual(replay.SERVICE_JOB_LABEL.format(self.job), self.label)

    def final_binding(self, name="lane-f", mutate=None):
        """Lane F's final binding (_lane_f): the composed record with its
        sealed-result phase naming the composed sealed result."""
        return _lane_f(self.work / name, self.result, mutate) / replay.FINAL_BINDING_NAME

    def test_composed_binding_is_at_the_fixed_path(self):
        self.assertEqual(str(replay.composer_binding_path(replay.COMPOSER_BINDING_PATH)),
                         replay.COMPOSER_BINDING_PATH)
        record = json.loads((self.result / replay.COMPOSER_BINDING_PATH).read_bytes())
        self.assertEqual(record["decision_id"], binding.DECISION_ID)
        # The pending sealed-result phase names the composed sealed result's
        # path but no digest of it: the validator's evidence check refuses
        # it, so only lane F's final binding can pass the replay.
        pending = record["workflow"]["phases"]["sealed_result"]
        self.assertEqual(pending["path"], "retirement-sealed-result.json")
        with self.assertRaises(ValueError):
            binding._check_evidence(self.result, pending, "workflow.phases.sealed_result")
        binding._check_evidence(self.result, json.loads(self.final_binding().read_bytes())["workflow"]["phases"][
            "sealed_result"], "workflow.phases.sealed_result")

    def test_final_binding_check_holds_the_composed_record(self):
        path = self.final_binding()
        replay.final_binding_check(self.result, path)
        # Any other change than lane F's two phases is refused, and the
        # composed record is never a final binding.
        changed = self.final_binding("changed", lambda record: record["subjects"]["candidate"]["binary"].__setitem__(
            "sha256", "f" * 64))
        with self.assertRaisesRegex(ValueError, "beyond lane F's phases"):
            replay.final_binding_check(self.result, changed)
        with self.assertRaises(ValueError):
            replay.final_binding_check(self.result, self.result / replay.COMPOSER_BINDING_PATH)

    def run_cli(self, lane_f, attempt=None):
        """The worker unit's result through the CLI's publication, separate
        retrieval, unpack (UNPACKER), lane F import, final-binding check,
        the real production validator and the join."""
        attempt = self.attempt if attempt is None else attempt
        names = sorted(path.name for path in self.result.iterdir())
        archive = self.work / "worker-unit.bqexport"
        if not archive.exists():
            self.export_sha = _write_archive(archive, self.result, names, self.job, self.attempt, "a" * 64)
        unpacker = self.work / "unpacker"
        if not unpacker.exists():
            unpacker.write_text(f"#!{sys.executable}\n{UNPACKER}")
            unpacker.chmod(stat.S_IRWXU)
        consumer = Path(tempfile.mkdtemp(prefix="consumer-", dir=self.work))
        publication, retrieval = consumer / "publication", consumer / "retrieval"
        publication.mkdir(mode=0o700)
        retrieval.mkdir(mode=0o700)
        identities = ["--bench-service", str(unpacker), "--repository-root", str(REPOSITORY),
                      "--binding", replay.COMPOSER_BINDING_PATH, "--job", str(self.job), "--attempt", str(attempt),
                      "--full-result-sha256", "a" * 64, "--export-receipt-sha256", self.export_sha,
                      "--trusted-execution-receipt-sha256", self.trusted]
        command = [sys.executable, str(Path(replay.__file__).resolve())]
        subprocess.run(command + [str(archive), "--publish-only", "--test-publication", str(publication)] +
                       identities, capture_output=True, text=True, check=True)
        published = publication / f"retirement-{self.job}-{self.attempt}.bqexport"
        return subprocess.run(command + [str(published), str(consumer / "result"), "--consume-published",
                                         "--retrieval", str(retrieval), "--lane-f", str(lane_f)] + identities,
                              cwd=consumer, env={"PATH": os.environ.get("PATH", ""), "LANG": "C"},
                              capture_output=True, text=True, check=False)

    # -- lane F's production writer over the worker unit's result ----------------

    def writer_bundle(self, name):
        archive = self.work / f"{name}.tar"
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(lane_f.main(["bundle", str(self.result), str(archive),
                                          "--publication-id", PUBLICATION_ID]), 0)
        self.assertEqual(json.loads(output.getvalue())["sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
        return archive

    def writer_lane_f(self, name):
        """Lane F's directory from the production writer. Its adapter replay
        is replaced here because the job-82 fixture's sealed adapter result
        is the preparation runner's stand-in (bq_prep_worker_unit_adapter)
        and its harness commit a placeholder: the real replay refuses both
        (test_writer_refuses_the_fixture_adapter_evidence)."""
        archive = self.writer_bundle(name)
        with mock.patch.object(lane_f, "adapter_replay", return_value=("6" * 64, "7" * 64, "clang -O2")):
            lane_f.bind(self.result, self.work / name, archive, hashlib.sha256(archive.read_bytes()).hexdigest(),
                        PUBLICATION_ID, "retirement-v1", f"run-{self.job}", REPOSITORY, *self.harness())
        return self.work / name

    def harness(self):
        """The record's harness identity, trusted here as the operator's."""
        measurement = json.loads((self.result / replay.COMPOSER_BINDING_PATH).read_bytes())["measurement"]
        return measurement["harness_source_commit"], measurement["harness_source_tree"]

    def test_writer_bundle_is_deterministic_and_extracts(self):
        first, second = self.writer_bundle("first"), self.writer_bundle("second")
        self.assertEqual(first.read_bytes(), second.read_bytes())
        state = lane_f.composed_state(self.result)
        descriptor = {"path": first.name, "bytes": first.stat().st_size,
                      "sha256": hashlib.sha256(first.read_bytes()).hexdigest()}
        extracted = binding._extract_downloaded_bundle(self.work, descriptor, PUBLICATION_ID, state["closure"],
                                                       self.work / "extracted")
        # The producer publishes the context evidence flat (#1998): the
        # archive holds each at its binding path, from its flat entry.
        flat = 0
        for item in state["closure"]:
            source = self.result / item["path"]
            if not source.exists():
                source = self.result / lane_f.evidence_name(item["path"])
                flat += 1
            self.assertEqual((extracted / item["path"]).read_bytes(), source.read_bytes())
        self.assertEqual(flat, WORKER_UNIT_EVIDENCE)

    def test_writer_refuses_the_fixture_adapter_evidence(self):
        """The production adapter replay rebuilds the reviewed adapter at the
        record's harness commit: the fixture's placeholder commit is refused.
        With only that source pin relaxed, the real adapter's output differs
        from the fixture's stand-in statistics, which is refused too. Neither
        promotes a lane F directory."""
        archive = self.writer_bundle("bundle")
        published = hashlib.sha256(archive.read_bytes()).hexdigest()
        checkout = self.work / "checkout"
        checkout.mkdir()
        for arguments in (["init", "--quiet"], ["add", "."], ["-c", "user.name=lane-f", "-c", "user.email=f@invalid",
                                                          "commit", "--quiet", "-m", "clean checkout"]):
            (checkout / "file").write_text("clean\n")
            subprocess.run(["git", "-C", str(checkout)] + arguments, check=True, capture_output=True)
        with self.assertRaisesRegex(ValueError, "differ from the trusted harness identity"):
            lane_f.bind(self.result, self.work / "untrusted", archive, published, PUBLICATION_ID, "retirement-v1",
                        "run-f", checkout, "0" * 40, self.harness()[1])
        with self.assertRaisesRegex(ValueError, "does not match the bound source identity"):
            lane_f.bind(self.result, self.work / "pinned", archive, published, PUBLICATION_ID, "retirement-v1",
                        "run-f", checkout, *self.harness())
        self.assertFalse((self.work / "pinned").exists())
        compile_adapter = binding._compile_trusted_retirement_adapter

        def working_tree_adapter(temp_root, repository_root=None, _commit=None, _tree=None):
            return compile_adapter(temp_root, repository_root)

        with mock.patch.object(binding, "_compile_trusted_retirement_adapter", working_tree_adapter), \
                self.assertRaisesRegex(ValueError, "adapter replay differs from the sealed adapter result"):
            lane_f.bind(self.result, self.work / "relaxed", archive, published, PUBLICATION_ID, "retirement-v1",
                        "run-f", REPOSITORY, *self.harness())
        self.assertFalse((self.work / "relaxed").exists())

    def test_real_validator_refuses_the_writer_output_for_missing_context_evidence(self):
        """(Gap N8, closed by #1998) The writer's final binding round-trips
        through lane F's import and check, and the context evidence the
        binding names (support, subjects, producer and provenance files) is
        in the worker unit's result as flat entries, which lane F lays out
        at their binding paths (all WORKER_UNIT_EVIDENCE of them). The real
        validator, pinned by the profile, then refuses the fixture's own
        support declaration, which is not the approved #508 input. The
        refusal is recorded in the verdict, deterministically."""
        directory = self.writer_lane_f("lane-f")
        validator, validator_sha256 = lane_f.profile_validator_pin(REPOSITORY)
        self.assertEqual(validator.resolve(), Path(binding.__file__).resolve())
        closure_sha256 = lane_f.validator_closure(REPOSITORY, validator, validator_sha256)[2]
        verdicts = []
        for name in ("clean-1", "clean-2"):
            verdict = lane_f.replay_lane_f(self.result, directory, self.work / name, REPOSITORY, self.trusted,
                                           validator, validator_sha256, closure_sha256,
                                           self.work / f"{name}.verdict.json")
            verdicts.append((self.work / f"{name}.verdict.json").read_bytes())
            self.assertEqual(verdict["verdict"], "refused")
            self.assertEqual((verdict["validator"]["path"], verdict["validator"]["sha256"],
                              verdict["validator"]["closure_sha256"]),
                             ("tools/native_retirement_performance_binding.py", validator_sha256, closure_sha256))
            self.assertEqual(verdict["evidence_layout"]["entries"], WORKER_UNIT_EVIDENCE)
            self.assertEqual(verdict["refusal"],
                             "ValueError: #508 support declaration digest is not the approved immutable input")
        self.assertEqual(verdicts[0], verdicts[1])
        with self.assertRaisesRegex(ValueError, "pinned SHA-256"):
            lane_f.replay_lane_f(self.result, directory, self.work / "clean-3", REPOSITORY, self.trusted,
                                 validator, "f" * 64, closure_sha256, self.work / "clean-3.verdict.json")

    def test_cli_replays_the_worker_unit_result_through_lane_f(self):
        """The export CLI with the writer's directory and the real validator:
        the CLI validates the unpacked result without lane F's flat-evidence
        layout (retirement_lane_f.evidence_layout), so the replay stops at
        the first binding path the result holds only as a flat entry (the
        support declaration), and a final binding that changes anything else
        is refused before it."""
        refused = self.run_cli(self.writer_lane_f("lane-f"))
        self.assertEqual(refused.returncode, 1)
        self.assertIn("support.files[0] is missing or is a symbolic link: " + binding.SUPPORT_DECLARATION_PATH,
                      refused.stderr)
        self.assertNotIn("verified-without-admission", refused.stdout)
        changed = self.final_binding("changed", lambda record: record["rules"]["sampling"].__setitem__("seed", 8))
        refused = self.run_cli(changed.parent)
        self.assertEqual(refused.returncode, 1)
        self.assertIn("final binding differs from the composed record", refused.stderr)

    def test_join_accepts_the_producer_authority_receipt(self):
        path = self.final_binding()
        joined = replay.authenticated_attempt_join(self.result, path, self.job, self.attempt, self.trusted)
        self.assertEqual((joined["job_id"], joined["attempt"]), (self.label, self.attempt))
        for job, attempt in ((self.job + 1, self.attempt), (self.job, self.attempt + 1)):
            with self.subTest(job=job, attempt=attempt), \
                    self.assertRaisesRegex(ValueError, "does not identify the exported job"):
                replay.authenticated_attempt_join(self.result, path, job, attempt, self.trusted)
        with self.assertRaisesRegex(ValueError, "independently trusted digest"):
            replay.authenticated_attempt_join(self.result, path, self.job, self.attempt, "f" * 64)

    def test_tampered_receipt_is_refused(self):
        path = self.final_binding()
        receipt = self.result / "retirement-execution-receipt.json"
        value = json.loads(receipt.read_bytes())
        receipt.write_bytes(_canonical_line(dict(value, attempt=value["attempt"] + 1)))
        with self.assertRaises(ValueError):
            replay.authenticated_attempt_join(self.result, path, self.job, self.attempt, self.trusted)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--worker-unit":
        # Only the worker unit's exported result (bench_service self-test).
        WORKER_UNIT_RESULT = Path(sys.argv[2]).resolve()
        WORKER_UNIT_REQUIRED = True
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(WorkerUnitResultJoinTests)
        sys.exit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
    if len(sys.argv) != 2:
        raise SystemExit("usage: retirement_export_replay_real_test.py THROUGHPUT_TEST_OUTPUT_DIRECTORY\n"
                         "       retirement_export_replay_real_test.py --worker-unit WORKER_UNIT_RESULT")
    ROOT = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(REPOSITORY / "tools" / "throughput"))
    unittest.main(argv=[sys.argv[0]], verbosity=2)
