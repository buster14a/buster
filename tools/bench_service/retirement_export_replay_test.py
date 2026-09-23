#!/usr/bin/env python3
"""Failure-first tests for the offline retirement export handoff."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import retirement_export_replay as replay


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
            repository_root=self.root, binding="record.json", job=self.job,
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
            binding="record.json", job=self.job, attempt=self.attempt,
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
                      "--repository-root", str(self.root), "--binding", "record.json",
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
        clean = self.root / "clean-consumer"
        clean.mkdir(mode=0o700)
        wrong = identities.copy()
        wrong[wrong.index("--export-receipt-sha256") + 1] = "c" * 64
        second = subprocess.run(command + [str(published), str(self.root / "new-result"),
                                          "--consume-published", "--retrieval", str(clean)] + wrong,
                                check=False, capture_output=True, text=True)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("independently supplied digest", second.stderr)
        self.assertEqual(list(clean.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
