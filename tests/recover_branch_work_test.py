#!/usr/bin/env python3
"""Offline recovery tests using local Git fixtures; no remote service or updates."""
import contextlib
import hashlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

TOOL = pathlib.Path(__file__).resolve().parents[1] / "tools" / "recover_branch_work.py"
spec = importlib.util.spec_from_file_location("recover_branch_work", TOOL)
recovery = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = recovery
spec.loader.exec_module(recovery)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.repo = self.root / "source"
        recovery.command(["git", "init", "-q", "-b", "main", str(self.repo)])
        self.g("config", "user.name", "Recovery fixture author")
        self.g("config", "user.email", "recovery-test@example.invalid")
        (self.repo / "value.txt").write_bytes(b"base\n")
        self.commit("base")
        (self.repo / "value.txt").write_bytes(b"base\nchange\n")
        self.tip = self.commit("candidate")
        self.record = {
            "destination": "salvage/fixture/value.txt", "commit": self.tip,
            "source_path": "value.txt", "method": "blob", "bytes": 12,
            "git_blob": self.g("rev-parse", self.tip + ":value.txt"),
            "sha256": hashlib.sha256(b"base\nchange\n").hexdigest(),
            "status": "historical-unvalidated",
        }

    def g(self, *args):
        return recovery.git(self.repo, *args)

    def commit(self, message):
        self.g("add", "--all")
        self.g("commit", "-q", "-m", message)
        return self.g("rev-parse", "HEAD")

    def manifest(self, records=None):
        path = self.root / "manifest.json"
        recovery.write_json(path, {"schema_version": 1, "repository": recovery.REPOSITORY,
                                   "records": records if records is not None else [self.record]})
        return path

    def export(self, records=None, output="export"):
        return recovery.export_salvage(self.repo, self.root / output, self.manifest(records))

    def bundle(self):
        path = self.root / "source.bundle"
        self.g("bundle", "create", str(path), "--all")
        return path

    def test_exact_bytes_and_inert_filename(self):
        rows = self.export()
        destination = self.root / "export" / rows[0]["destination"]
        self.assertEqual(destination.name, "value.txt.txt")
        self.assertEqual(destination.read_bytes(), b"base\nchange\n")
        self.assertEqual(rows[0]["sha256"], self.record["sha256"])

    def test_wrong_blob_size_and_digest_are_rejected(self):
        for field, value in (("git_blob", "0" * 40), ("bytes", 1), ("sha256", "0" * 64)):
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.export([dict(self.record, **{field: value})], field)
            self.assertFalse((self.root / field / "exported.json").exists())

    def test_missing_source_path_is_rejected(self):
        with self.assertRaises(RuntimeError):
            self.export([dict(self.record, source_path="absent.txt")])

    def test_unknown_or_unreachable_commit_is_rejected(self):
        for commit in ("0" * 40, "not-a-commit"):
            with self.subTest(commit=commit), self.assertRaises(RuntimeError):
                self.export([dict(self.record, commit=commit)], commit)

    def test_destination_escape_and_duplicates_are_rejected(self):
        for index, name in enumerate(("../escape", "/escape", "salvage/../../escape", "salvage/a\\escape", "salvage/C:escape")):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                self.export([dict(self.record, destination=name)], str(index))
        with self.assertRaises(RuntimeError):
            self.export([self.record, self.record])

    def test_unknown_export_method_is_rejected(self):
        with self.assertRaises(RuntimeError):
            self.export([dict(self.record, method="unknown")])

    def test_empty_and_wrong_repository_manifests_are_rejected(self):
        with self.assertRaises(RuntimeError):
            self.export([])
        path = self.manifest()
        document = json.loads(path.read_text())
        document["repository"] = "another/repository"
        recovery.write_json(path, document)
        with self.assertRaises(RuntimeError):
            recovery.export_salvage(self.repo, self.root / "wrong", path)

    def test_existing_output_is_not_overwritten(self):
        destination = self.root / "export"
        destination.mkdir()
        sentinel = destination / "keep.txt"
        sentinel.write_text("keep this")
        with self.assertRaises(RuntimeError):
            self.export()
        self.assertEqual(sentinel.read_text(), "keep this")

    def test_corrupt_encoded_data_is_copied_not_decoded(self):
        rows = self.export([dict(self.record, destination="salvage/fixture/corrupt.b64",
                                 status="known-corrupt-encoded-data")])
        self.assertEqual(rows[0]["status"], "known-corrupt-encoded-data")
        self.assertEqual((self.root / "export" / rows[0]["destination"]).read_bytes(), b"base\nchange\n")

    def test_python_source_is_not_executed(self):
        data = b"raise RuntimeError('historical source must not execute')\n"
        (self.repo / "historical.py").write_bytes(data)
        commit = self.commit("historical source")
        record = dict(self.record, destination="salvage/fixture/historical.py", commit=commit,
                      source_path="historical.py", git_blob=self.g("rev-parse", commit + ":historical.py"),
                      bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        rows = self.export([record])
        self.assertTrue(rows[0]["destination"].endswith(".py.txt"))
        self.assertEqual((self.root / "export" / rows[0]["destination"]).read_bytes(), data)

    def test_complete_patch_retains_author_and_commit(self):
        record = {"destination": "salvage/fixture/change.patch", "commit": self.tip, "method": "commit-patch",
                  "git_tree": self.g("rev-parse", self.tip + "^{tree}"), "status": "historical-unvalidated"}
        rows = self.export([record])
        patch = (self.root / "export" / rows[0]["destination"]).read_bytes()
        self.assertIn(b"From: Recovery fixture author <recovery-test@example.invalid>", patch)
        self.assertIn(self.tip.encode(), patch)
        self.assertIn(b"+change", patch)
        recovery.command(["git", "apply", "--numstat"], data=patch)

    def test_wrong_commit_tree_is_rejected(self):
        record = {"destination": "salvage/fixture/change.patch", "commit": self.tip, "method": "commit-patch",
                  "git_tree": "0" * 40, "status": "historical-unvalidated"}
        with self.assertRaises(RuntimeError):
            self.export([record])

    def test_bundle_recovery_leaves_source_unchanged(self):
        bundle = self.bundle()
        digest = recovery.sha256(bundle)
        refs = self.g("show-ref")
        rows = recovery.recover(bundle, self.root / "recovered", self.manifest())
        self.assertEqual(len(rows), 1)
        self.assertEqual(recovery.sha256(bundle), digest)
        self.assertEqual(self.g("show-ref"), refs)
        evidence = json.loads((self.root / "recovered/recovery.json").read_text())
        self.assertTrue(evidence["complete"])
        self.assertEqual(evidence["source_bundle_sha256"], digest)

    def test_failed_recovery_does_not_claim_completion(self):
        manifest = self.manifest([dict(self.record, sha256="0" * 64)])
        with self.assertRaises(RuntimeError):
            recovery.recover(self.bundle(), self.root / "recovered", manifest)
        evidence = json.loads((self.root / "recovered/recovery.json").read_text())
        self.assertFalse(evidence["complete"])
        self.assertIn("checksum", evidence["error"])

    def test_input_must_be_a_local_file(self):
        with self.assertRaises(RuntimeError):
            recovery.recover(self.repo, self.root / "directory")
        with self.assertRaises(FileNotFoundError):
            recovery.recover(self.root / "missing.bundle", self.root / "missing")

    def test_command_line_has_no_apply_mode(self):
        arguments = ["recover_branch_work.py", "--bundle", str(self.bundle()), "--output", str(self.root / "unused"), "--apply"]
        errors = io.StringIO()
        with mock.patch.object(sys, "argv", arguments), contextlib.redirect_stderr(errors):
            with self.assertRaises(SystemExit) as error:
                recovery.main()
            self.assertEqual(error.exception.code, 2)
        self.assertIn("unrecognized arguments: --apply", errors.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
