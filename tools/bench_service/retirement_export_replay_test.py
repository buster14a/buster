#!/usr/bin/env python3
"""Failure-first tests for the offline retirement export handoff."""

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import retirement_export_replay as replay
import retirement_lane_f as lane_f


def c_define(root, relative, name):
    """Evaluate one integer #define made only of digits and operators."""
    text = (Path(root) / relative).read_text(encoding="utf-8")
    match = re.search(rf"^#define {name} (.+)$", text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"{relative} lacks #define {name}")
    expression = re.sub(r"(?<=[0-9])(ull|u)\b", "", match.group(1)).replace("UINT64_C", "")
    if not re.fullmatch(r"[0-9 ()*+<]+", expression):
        raise AssertionError(f"{name} is not a plain integer expression: {expression}")
    return eval(expression)  # noqa: S307 - digits and operators only


class ExportReplayTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="retirement-export-replay-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.destination = self.root / "published"
        self.destination.mkdir(mode=0o700)
        self.archive = self.root / "download.bqexport"
        self.job = 42
        self.attempt = 19
        self.full_digest = "a" * 64
        self.bytes = b"archive\0payload"
        self.make_archive()

    def make_archive(self, recipe=replay.RECIPE, outcome=1, validity=1):
        receipt = bytearray(replay.RECEIPT_BYTES)
        receipt[:8] = b"BQEXP001"
        receipt[8:16] = self.job.to_bytes(8, "little")
        receipt[16:24] = self.attempt.to_bytes(8, "little")
        receipt[24:32] = len(self.bytes).to_bytes(8, "little")
        receipt[32:40] = len(self.bytes).to_bytes(8, "little")
        receipt[40:44] = (1).to_bytes(4, "little")
        receipt[44:48] = (1).to_bytes(4, "little")
        receipt[240:304] = self.full_digest.encode()
        receipt[304:368] = hashlib.sha256(self.bytes).hexdigest().encode()
        receipt[560:608] = recipe.ljust(48, b"\0")
        receipt[1012:1016] = outcome.to_bytes(4, "little")
        receipt[1016:1020] = validity.to_bytes(4, "little")
        receipt[1020:1024] = (7).to_bytes(4, "little")
        self.receipt = bytes(receipt)
        self.receipt_sha256 = hashlib.sha256(self.receipt).hexdigest()
        self.archive.write_bytes(self.receipt + self.bytes)

    def open_source(self, **changes):
        return replay.archive_source(
            self.archive, changes.get("receipt", self.receipt_sha256),
            changes.get("job", self.job), changes.get("attempt", self.attempt),
            changes.get("full", self.full_digest))

    def test_out_of_band_identity_is_required_before_publication(self):
        for changes in ({"receipt": "b" * 64}, {"job": 43},
                        {"attempt": 20}, {"full": "c" * 64}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.open_source(**changes)
        self.make_archive(recipe=b"validate-buster-v1")
        with self.assertRaisesRegex(ValueError, "not the retirement recipe"):
            self.open_source()
        self.assertEqual(list(self.destination.iterdir()), [])

    def test_truncation_and_archive_mutation_cannot_publish(self):
        self.archive.write_bytes(self.receipt + self.bytes[:-1])
        with self.assertRaisesRegex(ValueError, "incomplete"):
            self.open_source()
        self.make_archive()
        source, metadata, receipt = self.open_source()
        try:
            with self.archive.open("r+b") as output:
                output.seek(-1, 2)
                output.write(b"X")
            with self.assertRaisesRegex(ValueError, "differs"):
                replay.publish_test_copy(source, metadata, receipt, self.destination,
                                         self.job, self.attempt)
        finally:
            replay.os.close(source)
        self.assertTrue((self.destination / "retirement-42-19.pending").is_file())
        self.assertFalse((self.destination / "retirement-42-19.bqexport").exists())

    def test_byte_for_byte_publication_is_exclusive_and_read_back(self):
        source, metadata, receipt = self.open_source()
        try:
            published = replay.publish_test_copy(source, metadata, receipt,
                                                 self.destination, self.job, self.attempt)
            self.assertEqual(Path(published).read_bytes(), self.archive.read_bytes())
            self.assertFalse((self.destination / "retirement-42-19.pending").exists())
            with self.assertRaisesRegex(ValueError, "already exists"):
                replay.publish_test_copy(source, metadata, receipt, self.destination,
                                         self.job, self.attempt)
        finally:
            replay.os.close(source)

    def test_modified_source_during_copy_preserves_interrupted_attempt(self):
        original = replay.copy_and_hash

        def modify(source, target, receipt, size):
            value = original(source, target, receipt, size)
            with self.archive.open("r+b") as output:
                output.seek(-1, 2)
                output.write(b"X")
            return value

        source, metadata, receipt = self.open_source()
        try:
            with mock.patch.object(replay, "copy_and_hash", modify):
                with self.assertRaisesRegex(ValueError, "changed"):
                    replay.publish_test_copy(source, metadata, receipt, self.destination,
                                             self.job, self.attempt)
        finally:
            replay.os.close(source)
        self.assertTrue((self.destination / "retirement-42-19.pending").is_file())
        self.assertFalse((self.destination / "retirement-42-19.bqexport").exists())

    def test_unsafe_binding_and_failed_attempt_never_enter_performance_replay(self):
        for value in ("../record.json", "/record.json", "a//record.json",
                      "a/./record.json"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                replay.relative_binding_path(value)
        self.make_archive(outcome=2)
        arguments = mock.Mock(
            download=self.archive, destination=self.root / "fresh",
            test_publication=self.destination, bench_service=self.root / "trusted-utility",
            repository_root=self.root, binding=replay.COMPOSER_BINDING_PATH, job=self.job,
            attempt=self.attempt, full_result_sha256=self.full_digest,
            export_receipt_sha256=self.receipt_sha256,
            trusted_execution_receipt_sha256="b" * 64,
            consume_published=False, publish_only=False)
        with mock.patch.object(replay.subprocess, "run") as unpack:
            with self.assertRaisesRegex(ValueError, "no performance replay"):
                replay.replay(arguments)
            unpack.assert_called_once()
        self.assertTrue((self.destination / "retirement-42-19.bqexport").exists())

    def test_separate_consumer_retrieves_published_bytes_before_unpack(self):
        clean = self.root / "clean-consumer"
        clean.mkdir(mode=0o700)
        arguments = mock.Mock(
            download=self.archive, destination=self.root / "fresh",
            test_publication=self.destination, retrieval=None,
            bench_service=self.root / "trusted-utility", repository_root=self.root,
            binding=replay.COMPOSER_BINDING_PATH, job=self.job, attempt=self.attempt,
            full_result_sha256=self.full_digest,
            export_receipt_sha256=self.receipt_sha256,
            trusted_execution_receipt_sha256="b" * 64,
            consume_published=False, publish_only=True)
        with mock.patch.object(replay.subprocess, "run") as unpack:
            replay.replay(arguments)
            unpack.assert_not_called()
        published = self.destination / "retirement-42-19.bqexport"
        arguments.download = published
        arguments.test_publication = None
        arguments.retrieval = clean
        arguments.consume_published = True
        arguments.publish_only = False
        with mock.patch.object(replay.subprocess, "run",
                               side_effect=subprocess.CalledProcessError(1, "unpack")) as unpack:
            with self.assertRaises(subprocess.CalledProcessError):
                replay.replay(arguments)
            retrieved = clean / published.name
            self.assertEqual(retrieved.read_bytes(), published.read_bytes())
            self.assertEqual(unpack.call_args.args[0][2], str(retrieved))
        with self.assertRaisesRegex(ValueError, "already exists"):
            replay.replay(arguments)

    def test_retrieval_rejects_substitution_and_same_directory(self):
        source, metadata, receipt = self.open_source()
        try:
            published = Path(replay.publish_test_copy(source, metadata, receipt,
                                                      self.destination, self.job, self.attempt))
        finally:
            replay.os.close(source)
        source, metadata, receipt = replay.archive_source(
            published, self.receipt_sha256, self.job, self.attempt, self.full_digest)
        try:
            with self.assertRaisesRegex(ValueError, "separate private directory"):
                replay.retrieve_test_copy(source, metadata, receipt, self.destination,
                                          self.job, self.attempt, published)
            clean = self.root / "clean"
            clean.mkdir(mode=0o700)
            published.chmod(0o600)
            with published.open("r+b") as output:
                output.seek(-1, 2)
                output.write(b"X")
            with self.assertRaisesRegex(ValueError, "differs"):
                replay.retrieve_test_copy(source, metadata, receipt, clean,
                                          self.job, self.attempt, published)
            self.assertTrue((clean / "retirement-42-19.pending").exists())
            self.assertFalse((clean / "retirement-42-19.bqexport").exists())
        finally:
            replay.os.close(source)

    def test_publisher_and_consumer_are_separate_cli_processes(self):
        command = [sys.executable, str(Path(replay.__file__).resolve())]
        identities = ["--bench-service", str(self.root / "reviewed-service"),
                      "--repository-root", str(self.root), "--binding", replay.COMPOSER_BINDING_PATH,
                      "--job", str(self.job), "--attempt", str(self.attempt),
                      "--full-result-sha256", self.full_digest,
                      "--export-receipt-sha256", self.receipt_sha256,
                      "--trusted-execution-receipt-sha256", "b" * 64]
        first = subprocess.run(command + [str(self.archive), "--publish-only",
                                         "--test-publication", str(self.destination)] + identities,
                               check=True, capture_output=True, text=True)
        self.assertIn('"test_publication"', first.stdout)
        published = self.destination / "retirement-42-19.bqexport"
        self.assertEqual(published.read_bytes(), self.archive.read_bytes())
        consumer = tempfile.TemporaryDirectory(prefix="retirement-independent-consumer-")
        self.addCleanup(consumer.cleanup)
        clean_root = Path(consumer.name)
        clean = clean_root / "retrieval"
        clean.mkdir(mode=0o700)
        wrong = identities.copy()
        wrong[wrong.index("--export-receipt-sha256") + 1] = "c" * 64
        second = subprocess.run(command + [str(published), str(clean_root / "new-result"),
                                          "--consume-published", "--retrieval", str(clean),
                                          "--lane-f", str(clean_root / "lane-f")] + wrong,
                                cwd=clean_root, env={"PATH": os.environ.get("PATH", ""),
                                                     "LANG": "C"},
                                check=False, capture_output=True, text=True)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("independently supplied digest", second.stderr)
        self.assertEqual(list(clean.iterdir()), [])
        self.assertFalse((clean_root / "new-result").exists())

    def test_receipt_derived_six_copy_capacity_from_real_fields(self):
        ledger = replay.capacity_ledger(self.receipt)
        copies = ledger["copies"]
        exported = replay.RECEIPT_BYTES + len(self.bytes)
        self.assertEqual(copies["retained_service_result_bytes"], len(self.bytes))
        self.assertEqual(copies["extracted_clean_replay_bytes"], len(self.bytes))
        self.assertEqual(copies["sealed_service_spool_bytes"],
                         exported + replay.SPOOL_INDEX_BYTES)
        for stage in ("gateway_download_bytes", "immutable_test_publication_bytes",
                      "fresh_retrieval_bytes"):
            self.assertEqual(copies[stage], exported)
        self.assertEqual(ledger["logical_six_copy_file_bytes"], sum(copies.values()))

    def test_capacity_upper_bound_accounts_for_six_copies(self):
        receipt = bytearray(self.receipt)
        receipt[24:32] = replay.ARCHIVE_CAP.to_bytes(8, "little")
        receipt[32:40] = replay.RESULT_FILE_CAP.to_bytes(8, "little")
        receipt[40:44] = replay.ENTRY_CAP.to_bytes(4, "little")
        receipt[44:48] = replay.ENTRY_CAP.to_bytes(4, "little")
        ledger = replay.capacity_ledger(receipt)
        self.assertEqual(ledger["logical_six_copy_file_bytes"], 824805176192)
        self.assertEqual(ledger["copies"]["sealed_service_spool_bytes"],
                         137582487424)

    def test_matching_external_digest_of_malformed_inventory_never_publishes(self):
        command = [sys.executable, str(Path(replay.__file__).resolve()),
                   str(self.archive), "--publish-only", "--test-publication",
                   str(self.destination), "--bench-service", str(self.root / "reviewed-service"),
                   "--repository-root", str(self.root), "--binding", replay.COMPOSER_BINDING_PATH,
                   "--job", str(self.job), "--attempt", str(self.attempt),
                   "--full-result-sha256", self.full_digest,
                   "--trusted-execution-receipt-sha256", "b" * 64]
        for offset, size, invalid in ((32, 8, len(self.bytes) + 1),
                                      (40, 4, 0), (44, 4, replay.ENTRY_CAP + 1)):
            with self.subTest(offset=offset):
                receipt = bytearray(self.receipt)
                receipt[offset:offset + size] = invalid.to_bytes(size, "little")
                self.archive.write_bytes(receipt + self.bytes)
                pinned_digest = hashlib.sha256(receipt).hexdigest()
                result = subprocess.run(command + ["--export-receipt-sha256", pinned_digest],
                                        capture_output=True, text=True, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid retirement capacity inventory", result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(list(self.destination.iterdir()), [])

    def test_export_limits_match_their_c_definitions(self):
        root = Path(replay.__file__).resolve().parents[2]
        worker = "tools/bench_service/worker_linux.h"
        entries = c_define(root, worker, "BQ_WORKER_BUNDLE_ENTRY_CAP")
        path = c_define(root, "tools/bench_service/queue.h", "BQ_PATH_CAP")
        self.assertEqual(entries, replay.ENTRY_CAP)
        self.assertEqual(path, replay.PATH_CAP)
        self.assertEqual(c_define(root, worker, "BQ_WORKER_BUNDLE_FILE_CAP"), replay.FILE_CAP)
        payload = c_define(root, worker, "BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP")
        self.assertEqual(payload, replay.RESULT_FILE_CAP)
        self.assertEqual(c_define(root, worker, "BQ_WORKER_BUNDLE_CAP"), replay.BUNDLE_INDEX_CAP)
        self.assertEqual(c_define(root, "tools/bench_service/export.c", "BQ_EXPORT_CONTROL_RESERVE"),
                         replay.EXPORT_CONTROL_RESERVE)
        self.assertEqual(c_define(root, "tools/throughput/retirement_store.h", "TP_RETIREMENT_STORE_TOTAL_BYTES"),
                         replay.RESULT_FILE_CAP)
        self.assertEqual(c_define(root, "tools/throughput/retirement_execution.h", "TP_RETIREMENT_RECEIPT_BYTES"),
                         replay.STORE_RECEIPT_BYTES)
        self.assertEqual(c_define(root, "tools/throughput/retirement_samples.h", "TP_RETIREMENT_CODE_RECORD_BYTES_MAX"),
                         replay.CODE_RECORD_BYTES_MAX)
        # BQ_EXPORT_RETIREMENT_TOTAL_CAP in export.c.
        self.assertEqual(payload + replay.BUNDLE_INDEX_CAP + replay.EXPORT_CONTROL_RESERVE +
                         entries * (replay.ENTRY_HEADER_BYTES + path), replay.ARCHIVE_CAP)
        self.assertEqual(c_define(root, "tools/bench_service/export.c", "BQ_EXPORT_RECEIPT_CAP"), replay.RECEIPT_BYTES)

    def test_service_job_label_matches_the_c_format(self):
        root = Path(replay.__file__).resolve().parents[2]
        store = (root / "tools/throughput/retirement_store.h").read_text(encoding="utf-8")
        match = re.search(r"tp_retirement_store_job_label\(char output\[TP_RETIREMENT_STORE_TOKEN_CAPACITY\], "
                          r"uint64_t job\)\s*\{[^}]*snprintf\(output, TP_RETIREMENT_STORE_TOKEN_CAPACITY, "
                          r"\"([^\"]*)\" PRIu64", store)
        self.assertIsNotNone(match, "tp_retirement_store_job_label no longer formats job-<id>")
        self.assertEqual(match.group(1).replace("%", "{}"), replay.SERVICE_JOB_LABEL)
        # The campaign binding labels its receipt through the same helper.
        campaign = (root / "tools/bench_service/retirement_campaign_binding.h").read_text(encoding="utf-8")
        self.assertRegex(campaign, r"bq_retirement_campaign_job_label\(char output\[129\], uint64_t job_id\)"
                                   r"\s*\{[^}]*tp_retirement_store_job_label\(output, job_id\)")
        self.assertEqual(replay.SERVICE_JOB_LABEL.format(42), "job-42")

    def test_composer_constants_match_the_composer_source(self):
        root = Path(replay.__file__).resolve().parents[2]
        source = root / "tools/bench_service/retirement_compose.c"
        if not source.is_file():
            self.skipTest("the #1879 composer is not in this tree yet; its bounds are mirrored from 975b467")
        pairs = {"TP_COMPOSE_RATIO_LINE_BYTES": replay.COMPOSE_RATIO_LINE_BYTES,
                 "TP_COMPOSE_MEMBER_LINE_BYTES": replay.COMPOSE_MEMBER_LINE_BYTES,
                 "TP_COMPOSE_SERIES_HEADER_BYTES": replay.COMPOSE_SERIES_HEADER_BYTES,
                 "TP_COMPOSE_SERIES_END_BYTES": replay.COMPOSE_SERIES_END_BYTES,
                 "TP_COMPOSE_REPLAY_MEMBER_BYTES": replay.COMPOSE_REPLAY_MEMBER_BYTES,
                 "TP_COMPOSE_REPLAY_FIXED_BYTES": replay.COMPOSE_REPLAY_FIXED_BYTES,
                 "TP_COMPOSE_MANIFEST_FIXED_BYTES": replay.COMPOSE_MANIFEST_FIXED_BYTES,
                 "TP_COMPOSE_MANIFEST_SHARD_BYTES": replay.COMPOSE_MANIFEST_SHARD_BYTES,
                 "TP_COMPOSE_BUNDLE_BYTES": replay.COMPOSE_BUNDLE_BYTES,
                 "TP_COMPOSE_SEAL_FIXED_BYTES": replay.COMPOSE_SEAL_FIXED_BYTES,
                 "TP_COMPOSE_SEAL_ENTRY_BYTES": replay.COMPOSE_SEAL_ENTRY_BYTES,
                 "TP_COMPOSE_RETAINED_LINE_BYTES": replay.COMPOSE_RETAINED_LINE_BYTES,
                 "TP_COMPOSE_SERIES_LINE_BYTES": replay.COMPOSE_SERIES_LINE_BYTES,
                 "TP_COMPOSE_SERIES_MANIFEST_FIXED_BYTES": replay.COMPOSE_SERIES_MANIFEST_FIXED_BYTES,
                 "TP_COMPOSE_SERIES_MANIFEST_LINE_BYTES": replay.COMPOSE_SERIES_MANIFEST_LINE_BYTES}
        for name, value in pairs.items():
            self.assertEqual(c_define(root, "tools/bench_service/retirement_compose.c", name), value, name)
        store = (root / "tools/throughput/retirement_store.h").read_text(encoding="utf-8")
        match = re.search(r'^#define TP_RETIREMENT_RETAINED_MANIFEST_HEADER "((?:[^"\\]|\\.)*)"$', store, re.MULTILINE)
        self.assertIsNotNone(match)
        header_text = match.group(1).encode().decode("unicode_escape")
        self.assertEqual(len(header_text) + 1, replay.RETAINED_MANIFEST_HEADER_SIZE)
        header = "tools/bench_service/retirement_compose.h"
        self.assertEqual(c_define(root, header, "TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS"), replay.COMPOSE_FIXED_OUTPUTS)
        self.assertEqual(c_define(root, header, "TP_RETIREMENT_COMPOSE_BOOTSTRAP_MEMBERS"),
                         replay.COMPOSE_BOOTSTRAP_MEMBERS)
        self.assertEqual(c_define(root, header, "TP_RETIREMENT_COMPOSE_CELL_MEMBERS"), replay.COMPOSE_CELL_MEMBERS)
        self.assertEqual(c_define(root, header, "TP_RETIREMENT_COMPOSE_SERIES_SHARDS"), replay.COMPOSE_SERIES_SHARDS)

    def test_composer_bounds_mirror_the_composer_refusals(self):
        """The layouts of retirement_compose_tests.c's test_budget_and_settle."""
        # (#1880) The large family's series exceeds one file and is bounded
        # as 16 shards instead of being refused.
        large = {"pairs_per_round": 254, "timed_rows": 4000, "runtime_rows": 1, "object_groups": 1}
        sharded = replay.composer_bounds(large, 1, 1)
        self.assertEqual(sharded["refusals"], [])
        self.assertEqual(sharded["outputs"]["adapter_input_series"], 1_042_875_980)
        self.assertEqual(sharded["series_shards"], 16)
        self.assertEqual(sharded["files"], 2 + replay.COMPOSE_FIXED_OUTPUTS + 16)
        self.assertEqual(sharded["outputs"]["adapter_input_manifest"],
                         replay.COMPOSE_SERIES_MANIFEST_FIXED_BYTES + 16 * replay.COMPOSE_SERIES_MANIFEST_LINE_BYTES)
        small = dict(large, pairs_per_round=60, timed_rows=2)
        bounds = replay.composer_bounds(small, 1, 1)
        self.assertEqual(bounds["refusals"], [])
        self.assertEqual((bounds["series_shards"], bounds["files"]), (1, 2 + replay.COMPOSE_FIXED_OUTPUTS + 1))
        self.assertEqual(bounds["outputs"]["sealed_result"],
                         replay.COMPOSE_SEAL_FIXED_BYTES + (1 + replay.ENTRY_CAP) * replay.COMPOSE_SEAL_ENTRY_BYTES)
        no_runtime = replay.composer_bounds(dict(small, runtime_rows=0), 1, 1)
        self.assertTrue(any("no cell" in reason for reason in no_runtime["refusals"]))

    def test_a1_ledger_counts_composer_outputs_and_series_shards(self):
        root = Path(replay.__file__).resolve().parents[2]
        report = replay.a1_capacity_report(root, require_committed=False)
        scenarios = report["scenarios"]
        prior = report["prior_closure_entries_minimum"]
        self.assertEqual(prior, 40)
        self.assertEqual(report["code_rows_upper_bound"], 78914)
        self.assertTrue(scenarios)
        for name, ledger in scenarios.items():
            with self.subTest(name=name):
                composer = ledger["composer"]
                self.assertFalse(ledger["export_binds_before_store"])
                self.assertEqual(ledger["largest_file_bytes"]["metrics_shard"], replay.FILE_CAP)
                self.assertLessEqual(ledger["largest_file_bytes"]["untimed_record_file"], replay.FILE_CAP)
                self.assertEqual(ledger["untimed_record_files"], 1)
                # Every composer output is counted: manifests, code records,
                # the adapter input's series shards and manifest (#1880),
                # adapter output, bundle, receipt, retained manifest and seal.
                self.assertEqual(set(composer["outputs"]), {
                    "result_input_manifests", "code_records", "adapter_input_series", "adapter_input_manifest",
                    "adapter_output", "result_bundle", "execution_receipt", "retained_manifest", "sealed_result"})
                self.assertEqual(composer["outputs"]["retained_manifest"],
                                 replay.RETAINED_MANIFEST_HEADER_SIZE + replay.ENTRY_CAP * replay.COMPOSE_RETAINED_LINE_BYTES)
                self.assertEqual(ledger["entries_left_for_projections_logs_and_directories"],
                                 replay.ENTRY_CAP - replay.WORKER_CONTROL_ENTRIES - prior - ledger["owned_files"])
                # (#1880) The series is above 64 MiB at A1 scale, stored as
                # 7 (60 pairs) or 26 (254 pairs) shards of at most one file.
                pairs, runtime = ledger["pairs_per_round"], ledger["runtime_rows"]
                series, shards = {(60, 2): (406_664_536, 7), (254, 2): (1_710_443_864, 26),
                                  (60, 0): (406_602_576, 7), (254, 0): (1_710_183_248, 26)}[pairs, runtime]
                self.assertEqual(composer["outputs"]["adapter_input_series"], series)
                self.assertEqual(composer["series_shards"], shards)
                self.assertEqual(composer["files"], 2 + replay.COMPOSE_FIXED_OUTPUTS + shards)
                self.assertEqual(ledger["largest_file_bytes"]["adapter_input_shard"], replay.FILE_CAP)
                self.assertNotIn("adapter_input_series", ledger["largest_file_bytes"])
                self.assertTrue(ledger["per_file_fits"])
                self.assertFalse(any("#1880" in reason or "single-file" in reason for reason in ledger["refusals"]))
                self.assertTrue(ledger["model_only"])
        # Every runtime-bearing scenario but 254 pairs at 16 KiB fits; with no
        # runtime-eligible row generated runtime has no #619 cell.
        self.assertEqual({name for name, ledger in scenarios.items() if ledger["fits"]},
                         {name for name in scenarios if name.endswith("runtime-2")} -
                         {"pairs-254/per-input-16384/runtime-2"})
        for name in scenarios:
            if name.endswith("runtime-0"):
                self.assertTrue(any("no cell" in reason for reason in scenarios[name]["refusals"]))
        rows = {name: (ledger["owned_files"], ledger["entries_left_for_projections_logs_and_directories"],
                       ledger["owned_bytes_upper_bound"])
                for name, ledger in scenarios.items() if name.endswith("runtime-2")}
        # Against the single-file ledger: one more file per shard beyond the
        # first, and the manifest's bytes.
        self.assertEqual(rows["pairs-60/per-input-4096/runtime-2"], (477, 3576, 15_909_122_949 + 512 + 7 * 256))
        self.assertEqual(rows["pairs-254/per-input-4096/runtime-2"],
                         (1838, 2215, 62_554_729_637 + 512 + 26 * 256))
        self.assertEqual(report["six_copy_ceiling"]["logical_six_copy_file_bytes"], 824805176192)

    def test_ledger_rejects_oversized_untimed_record_file(self):
        root = Path(replay.__file__).resolve().parents[2]
        sys.path.insert(0, str(root / "tools" / "throughput"))
        import retirement_capacity as capacity
        limits = capacity.source_limits(root)
        model = capacity.build_report(root, require_committed=False)["scenarios"][
            "pairs-60/per-input-4096/runtime-2"]
        huge = dict(model, untimed_batches=replay.FILE_CAP // limits["untimed_record_bytes_max"] + 1)
        ledger = replay.a1_export_ledger(huge, limits, 1, replay.prior_closure_entries())
        self.assertGreater(ledger["largest_file_bytes"]["untimed_record_file"], replay.FILE_CAP)
        self.assertFalse(ledger["per_file_fits"])
        self.assertFalse(ledger["fits"])

    def test_composer_fixes_the_binding_location(self):
        # The worker-unit producer's BQ_RETIREMENT_WORKER_BINDING_PATH.
        root = Path(replay.__file__).resolve().parents[2]
        source = (root / "tools/bench_service/retirement_worker_compose.c").read_text(encoding="utf-8")
        match = re.search(r'^#define BQ_RETIREMENT_WORKER_BINDING_PATH "([^"]+)"$', source, re.MULTILINE)
        self.assertIsNotNone(match)
        self.assertEqual(replay.COMPOSER_BINDING_PATH, match.group(1))
        self.assertEqual(str(replay.composer_binding_path(replay.COMPOSER_BINDING_PATH)),
                         replay.COMPOSER_BINDING_PATH)
        for other in ("any/record.json", "other/" + replay.COMPOSER_BINDING_PATH):
            with self.subTest(other=other), self.assertRaisesRegex(ValueError, "composer"):
                replay.composer_binding_path(other)
        with self.assertRaises(ValueError):
            replay.composer_binding_path("../binding.json")
        with mock.patch.object(replay, "COMPOSER_BINDING_PATH", None):
            self.assertEqual(str(replay.composer_binding_path("any/record.json")), "any/record.json")

    def lane_f_fixture(self):
        """A composed record with both late phases pending, its sealed
        result, and lane F's directory with the matching final binding."""
        result = self.root / "result"
        lane_f = self.root / "lane-f"
        result.mkdir(mode=0o700)
        lane_f.mkdir(mode=0o700)
        sealed = b'{"sealed":1}\n'
        (result / "retirement-sealed-result.json").write_bytes(sealed)
        pending = dict(replay.PENDING_DESCRIPTOR)
        composed = {"subjects": {"candidate": "c"}, "workflow": {"phases": {
            "sealed_result": dict(pending, path="retirement-sealed-result.json"),
            "independent_replay": dict(pending, path="retirement-independent-replay.json")}}}
        (result / replay.COMPOSER_BINDING_PATH).write_text(json.dumps(composed))
        replayed = b'{"replayed":1}\n'
        (lane_f / "retirement-independent-replay.json").write_bytes(replayed)
        final = json.loads(json.dumps(composed))
        final["workflow"]["phases"]["sealed_result"] = {
            "path": "retirement-sealed-result.json", "bytes": len(sealed),
            "sha256": hashlib.sha256(sealed).hexdigest()}
        final["workflow"]["phases"]["independent_replay"] = {
            "path": "retirement-independent-replay.json", "bytes": len(replayed),
            "sha256": hashlib.sha256(replayed).hexdigest()}
        return result, lane_f, composed, final

    def test_final_binding_is_the_composed_record_with_lane_f_phases(self):
        result, lane_f, composed, final = self.lane_f_fixture()
        (lane_f / replay.FINAL_BINDING_NAME).write_text(json.dumps(final))
        path = replay.lane_f_import(lane_f, result)
        self.assertEqual(path, result / replay.FINAL_BINDING_NAME)
        self.assertEqual(replay.final_binding_check(result, path), final)
        # A second import collides with the files already placed.
        with self.assertRaisesRegex(ValueError, "collides"):
            replay.lane_f_import(lane_f, result)
        mutations = {
            "other field": lambda value: value["subjects"].__setitem__("candidate", "other"),
            "sealed digest": lambda value: value["workflow"]["phases"]["sealed_result"].__setitem__("sha256", "f" * 64),
            "pending replay": lambda value: value["workflow"]["phases"]["independent_replay"].update(
                replay.PENDING_DESCRIPTOR),
            "replay elsewhere": lambda value: value["workflow"]["phases"]["independent_replay"].__setitem__(
                "path", "elsewhere.json"),
        }
        for name, mutate in mutations.items():
            changed = json.loads(json.dumps(final))
            mutate(changed)
            other = self.root / f"final-{len(name)}.json"
            other.write_text(json.dumps(changed))
            with self.subTest(mutation=name), self.assertRaises(ValueError):
                replay.final_binding_check(result, other)
        # The composed record itself can never stand in for the final one.
        with self.assertRaises(ValueError):
            replay.final_binding_check(result, result / replay.COMPOSER_BINDING_PATH)
        # A composed record whose sealed phase is already filled is refused.
        (result / replay.COMPOSER_BINDING_PATH).chmod(0o600)
        (result / replay.COMPOSER_BINDING_PATH).write_text(json.dumps(final))
        with self.assertRaisesRegex(ValueError, "pending"):
            replay.final_binding_check(result, path)

    def test_lane_f_directory_holds_only_regular_files_and_the_final_binding(self):
        result, lane_f, _composed, final = self.lane_f_fixture()
        with self.assertRaisesRegex(ValueError, "lacks the final binding"):
            replay.lane_f_import(lane_f, result)
        (lane_f / replay.FINAL_BINDING_NAME).write_text(json.dumps(final))
        os.symlink("retirement-independent-replay.json", lane_f / "link.json")
        with self.assertRaisesRegex(ValueError, "regular files"):
            replay.lane_f_import(lane_f, result)
        (lane_f / "link.json").unlink()
        (lane_f / replay.COMPOSER_BINDING_PATH).write_text("{}")
        with self.assertRaisesRegex(ValueError, "collides"):
            replay.lane_f_import(lane_f, result)

    def lane_f_result(self):
        """An unpacked composed result: the pending record, a sealed result
        whose seal enumerates its closure, and the result bundle."""
        result = self.root / "composed"
        result.mkdir(mode=0o700)

        def put(path, data):
            (result / path).write_bytes(data)
            return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

        closure = {"workflow.adapter_input": put("retirement-series.manifest.json", b'{"series":1}\n'),
                   "workflow.adapter_result": put("retirement-statistics.json", b'{"members":[]}\n'),
                   "workflow.untimed_batches": put("retirement-untimed-batches.jsonl", b'{"batch":0}\n'),
                   "contract.source": put("contract.md", b"contract\n")}
        closure["workflow.result_bundle"] = put("retirement-result-bundle.json", lane_f.canonical({
            "adapter_input": closure["workflow.adapter_input"], "raw_measurements_sha256": "1" * 64,
            "family_sha256": "2" * 64, "member_invocations_sha256": "3" * 64, "member_count": 4,
            "code_bytes_summary": {"rows": 1},
            "untimed_batches": dict(closure["workflow.untimed_batches"], records=1)}))
        files = [dict(artifact, name=name) for name, artifact in sorted(closure.items())]
        put("retirement-sealed-result.json", lane_f.canonical({
            "result_bundle": closure["workflow.result_bundle"],
            "seal": {"files": files, "root_sha256": replay.binding._canonical_files_digest(files)}}))
        pending = dict(replay.PENDING_DESCRIPTOR)
        composed = {"execution": {"service": {"id": "service-f"}},
                    "measurement": {"harness_source_commit": "4" * 40, "harness_source_tree": "5" * 40},
                    "workflow": {"phases": {
                        "sealed_result": dict(pending, path="retirement-sealed-result.json"),
                        "independent_replay": dict(pending, path="retirement-independent-replay.json")}}}
        put(replay.COMPOSER_BINDING_PATH, lane_f.canonical(composed))
        return result

    @staticmethod
    def lane_f_main(arguments):
        with contextlib.redirect_stdout(io.StringIO()):
            return lane_f.main(arguments)

    def lane_f_bind(self, result, name, **changes):
        """Publish (here: copy) lane F's archive and bind from the copy; the
        adapter replay is exercised on the worker unit's real result."""
        archive = self.root / f"{name}.tar"
        self.assertEqual(self.lane_f_main(["bundle", str(result), str(archive), "--publication-id", "pub-f"]), 0)
        published = hashlib.sha256(archive.read_bytes()).hexdigest()
        arguments = {"downloaded": archive, "published_sha256": published, "publication_id": "pub-f",
                     "release": "retirement-v1", "run_id": "run-f", "repository_root": self.root}
        arguments.update(changes)
        with mock.patch.object(lane_f, "adapter_replay", return_value=("6" * 64, "7" * 64, "clang -O2")):
            return lane_f.bind(result, self.root / name, **arguments)

    def test_lane_f_writer_round_trips_through_the_checker(self):
        result = self.lane_f_result()
        written = self.lane_f_bind(result, "lane-f")
        directory = self.root / "lane-f"
        self.assertFalse((self.root / "lane-f.pending").exists())
        self.assertEqual(sorted(path.name for path in directory.iterdir()), sorted([
            replay.FINAL_BINDING_NAME, lane_f.BUNDLE_NAME, lane_f.PUBLICATION_NAME, lane_f.REPLAY_BUNDLE_NAME,
            "retirement-independent-replay.json"]))
        destination = self.root / "destination"
        shutil.copytree(result, destination)
        final = replay.final_binding_check(destination, replay.lane_f_import(directory, destination))
        self.assertEqual(final["workflow"]["phases"]["independent_replay"], written["independent_replay"])
        # The records have exactly the validator's fields and bindings.
        binding = replay.binding
        phase = binding._workflow_phase(destination, written["independent_replay"],
                                        binding.PHASE_SCHEMA["independent_replay"], "independent_replay")
        self.assertEqual(phase["sealed_result_sha256"], final["workflow"]["phases"]["sealed_result"]["sha256"])
        bundle = binding._read_json_evidence(destination, phase["replay_bundle"], "replay bundle")
        self.assertEqual(bundle["publication_receipt"], phase["publication_receipt"])
        self.assertEqual(bundle["downloaded_bundle"], written["downloaded_bundle"])
        publication = binding._read_json_evidence(destination, phase["publication_receipt"], "publication")
        self.assertEqual(publication["service_id"], "service-f")
        self.assertEqual(publication["sealed_result_sha256"], bundle["sealed_result_sha256"])
        # The downloaded archive passes the validator's own extractor.
        state = lane_f.composed_state(result)
        with tempfile.TemporaryDirectory(dir=self.root) as scratch:
            extracted = binding._extract_downloaded_bundle(destination, bundle["downloaded_bundle"], "pub-f",
                                                           state["closure"], scratch)
            self.assertEqual((extracted / "retirement-statistics.json").read_bytes(), b'{"members":[]}\n')

    def test_lane_f_writer_is_deterministic(self):
        result = self.lane_f_result()
        self.lane_f_bind(result, "first")
        self.lane_f_bind(result, "second")
        for path in (self.root / "first").iterdir():
            with self.subTest(file=path.name):
                self.assertEqual(path.read_bytes(), (self.root / "second" / path.name).read_bytes())
        self.assertEqual((self.root / "first.tar").read_bytes(), (self.root / "second.tar").read_bytes())

    def test_lane_f_writer_fails_closed(self):
        result = self.lane_f_result()
        # The publisher's digest is the authority, never the downloaded file.
        with self.assertRaisesRegex(ValueError, "publisher's digest"):
            self.lane_f_bind(result, "wrong-digest", published_sha256="8" * 64)
        self.assertTrue((self.root / "wrong-digest.pending").is_dir())
        self.assertFalse((self.root / "wrong-digest").exists())
        # A download that differs from the archive the unpacked result derives.
        archive = self.root / "other-publication.tar"
        self.assertEqual(self.lane_f_main(["bundle", str(result), str(archive), "--publication-id", "other-f"]), 0)
        with self.assertRaisesRegex(ValueError, "derived from the unpacked export"):
            self.lane_f_bind(result, "forged", downloaded=archive,
                             published_sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertFalse((self.root / "forged").exists())
        # A sealed file of the export tampered with after the publication:
        # neither the archive nor lane F's directory is derived from it.
        archive = self.root / "before-tampering.tar"
        self.assertEqual(self.lane_f_main(["bundle", str(result), str(archive), "--publication-id", "pub-f"]), 0)
        target = result / "retirement-statistics.json"
        original = target.read_bytes()
        target.write_bytes(b'{"members":[2]}\n')
        self.assertEqual(self.lane_f_main(["bundle", str(result), str(self.root / "tampered.tar"),
                                           "--publication-id", "pub-f"]), 1)
        with self.assertRaisesRegex(ValueError, "does not match evidence"), \
                mock.patch.object(lane_f, "adapter_replay") as adapter:
            lane_f.bind(result, self.root / "tampered", archive, hashlib.sha256(archive.read_bytes()).hexdigest(),
                        "pub-f", "retirement-v1", "run-f", self.root)
        adapter.assert_not_called()
        self.assertFalse((self.root / "tampered").exists())
        target.write_bytes(original)
        # A missing or already filled phase.
        record_path = result / replay.COMPOSER_BINDING_PATH
        record = json.loads(record_path.read_bytes())
        for phase, value in (("independent_replay", None),
                             ("sealed_result", {"path": "retirement-sealed-result.json", "bytes": 1,
                                                "sha256": "9" * 64})):
            changed = json.loads(json.dumps(record))
            if value is None:
                del changed["workflow"]["phases"][phase]
            else:
                changed["workflow"]["phases"][phase] = value
            record_path.write_bytes(lane_f.canonical(changed))
            with self.subTest(phase=phase), self.assertRaisesRegex(ValueError, "pending descriptor"):
                lane_f.composed_state(result)
        record_path.write_bytes(lane_f.canonical(record))
        # An existing lane F directory is never replaced.
        (self.root / "exists").mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.lane_f_bind(result, "exists")

    def test_lane_f_replay_pins_the_validator_before_running(self):
        result = self.lane_f_result()
        self.lane_f_bind(result, "lane-f")
        repository = Path(replay.__file__).resolve().parents[2]
        validator = Path(replay.binding.__file__).resolve()
        clean = self.root / "clean"
        verdict = self.root / "verdict.json"
        with mock.patch.object(lane_f.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "pinned SHA-256"):
                lane_f.replay_lane_f(result, self.root / "lane-f", clean, repository, "a" * 64, validator,
                                     "b" * 64, verdict)
            with self.assertRaisesRegex(ValueError, "pinned checkout"):
                lane_f.replay_lane_f(result, self.root / "lane-f", clean, self.root, "a" * 64, validator,
                                     hashlib.sha256(validator.read_bytes()).hexdigest(), verdict)
            run.assert_not_called()
        self.assertFalse(clean.exists())
        self.assertFalse(verdict.exists())
        # Without lane F's final binding nothing is validated either.
        (self.root / "lane-f" / replay.FINAL_BINDING_NAME).chmod(0o600)
        (self.root / "lane-f" / replay.FINAL_BINDING_NAME).unlink()
        with mock.patch.object(lane_f.subprocess, "run") as run, \
                self.assertRaisesRegex(ValueError, "lacks the final binding"):
            lane_f.replay_lane_f(result, self.root / "lane-f", clean, repository, "a" * 64, validator,
                                 hashlib.sha256(validator.read_bytes()).hexdigest(), verdict)
        run.assert_not_called()
        self.assertFalse(verdict.exists())

    def test_publish_only_capacity_output_does_not_claim_replay(self):
        command = [sys.executable, str(Path(replay.__file__).resolve()),
                   str(self.archive), "--publish-only", "--test-publication",
                   str(self.destination), "--bench-service", str(self.root / "reviewed-service"),
                   "--repository-root", str(self.root), "--binding", replay.COMPOSER_BINDING_PATH,
                   "--job", str(self.job), "--attempt", str(self.attempt),
                   "--full-result-sha256", self.full_digest,
                   "--trusted-execution-receipt-sha256", "b" * 64,
                   "--export-receipt-sha256", self.receipt_sha256]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        output = json.loads(result.stdout)
        self.assertEqual(output["transferred_bytes"], replay.RECEIPT_BYTES + len(self.bytes))
        self.assertEqual(output["receipt_derived_capacity"], replay.capacity_ledger(self.receipt))
        self.assertNotIn("replay", output)


if __name__ == "__main__":
    unittest.main()
