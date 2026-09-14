#!/usr/bin/env python3
"""Adversarial tests for policy-neutral streaming evidence ingestion."""

import errno
import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_retirement_result_input as result_input


def encoded(records):
    return b"".join(json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"
                    for record in records)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class ResultInputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        # macOS exposes /tmp through a symlink. Production correctly rejects
        # symlinked root components, so fixtures use the physical path.
        self.root = Path(os.path.realpath(self.temporary.name))
        (self.root / "nested").mkdir()
        self.shards = [
            {"identity": "shard-a", "path": "a.jsonl"},
            {"identity": "shard-b", "path": "nested/b.jsonl"},
        ]
        self.write_shard(0, [{"record_id": "a-0", "value": "one"},
                             {"record_id": "a-1", "value": "two"}])
        self.write_shard(1, [{"record_id": "b-0", "value": "three"}])
        self.write_manifest()
        self.limits = result_input.Limits(
            max_manifest_bytes=4096,
            max_total_bytes=16384,
            max_shard_bytes=4096,
            max_shards=4,
            max_records=8,
            max_record_bytes=512,
            max_nesting=8,
            max_string_bytes=128,
            max_path_bytes=128,
        )

    def tearDown(self):
        self.temporary.cleanup()

    def write_shard(self, index, records=None, raw=None):
        if raw is None:
            raw = encoded(records)
        path = self.root / self.shards[index]["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        self.shards[index]["bytes"] = len(raw)
        self.shards[index]["sha256"] = digest(raw)

    def write_manifest(self, shards=None, identity_field="record_id", extra=None):
        value = {
            "schema": result_input.MANIFEST_SCHEMA,
            "version": 1,
            "identity_field": identity_field,
            "shards": self.shards if shards is None else shards,
        }
        if extra:
            value.update(extra)
        raw = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"
        (self.root / "manifest.json").write_bytes(raw)
        return raw

    def verify(self, limits=None, after_stream=None, root=None, manifest="manifest.json"):
        return result_input.verify(root or self.root, manifest, limits or self.limits,
                                   _after_stream=after_stream)

    def assert_invalid(self, pattern, **arguments):
        with self.assertRaisesRegex(result_input.IntegrityError, pattern):
            self.verify(**arguments)

    def test_valid_receipt_is_deterministic_and_integrity_only(self):
        consumed = []
        first = result_input.verify(
            self.root, "manifest.json", self.limits,
            lambda shard, index, value: consumed.append((shard, index, value["record_id"])))
        second = self.verify()
        self.assertEqual(first, second)
        self.assertEqual(first["schema"], result_input.RECEIPT_SCHEMA)
        self.assertEqual(first["scope"], "integrity-only")
        self.assertEqual(first["records"], 3)
        self.assertEqual([item["identity"] for item in first["shards"]],
                         ["shard-a", "shard-b"])
        self.assertEqual(consumed, [("shard-a", 0, "a-0"), ("shard-a", 1, "a-1"),
                                    ("shard-b", 0, "b-0")])
        self.assertNotIn("verdict", first)
        self.assertNotIn("statistics", first)
        self.assertNotIn("accepted", first)

    def test_root_and_file_symlinks_are_rejected(self):
        linked_root = self.root.parent / (self.root.name + "-link")
        os.symlink(self.root, linked_root)
        try:
            with self.assertRaisesRegex(result_input.IntegrityError, "symlink|trusted root"):
                self.verify(root=linked_root)
        finally:
            linked_root.unlink()

        target = self.root / "a-target"
        target.write_bytes((self.root / "a.jsonl").read_bytes())
        (self.root / "a.jsonl").unlink()
        os.symlink("a-target", self.root / "a.jsonl")
        self.assert_invalid("symlink|regular file")

    def test_manifest_path_is_contained_and_nofollow(self):
        self.assert_invalid("canonical relative path", manifest="../manifest.json")
        target = self.root / "manifest-target.json"
        (self.root / "manifest.json").rename(target)
        os.symlink(target.name, self.root / "manifest.json")
        self.assert_invalid("symlink|regular file")

    def test_symlinked_ancestor_is_rejected(self):
        original = self.root / "nested"
        moved = self.root / "physical"
        original.rename(moved)
        os.symlink("physical", original)
        self.assert_invalid("symlink|directory")

    def test_hardlinks_are_rejected_by_single_link_policy(self):
        original = self.root / "a.jsonl"
        linked = self.root / "hardlink.jsonl"
        os.link(original, linked)
        self.shards[0]["path"] = "hardlink.jsonl"
        self.write_manifest()
        self.assert_invalid("hard link|link count")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation unavailable")
    def test_fifo_is_rejected_without_blocking(self):
        path = self.root / "a.jsonl"
        path.unlink()
        os.mkfifo(path)
        self.shards[0]["bytes"] = 0
        self.shards[0]["sha256"] = digest(b"")
        self.write_manifest()
        self.assert_invalid("regular file")

    @unittest.skipUnless(Path("/dev/null").exists(), "device fixture unavailable")
    def test_device_identity_is_rejected(self):
        with self.assertRaisesRegex(result_input.IntegrityError, "regular file"):
            result_input._regular_identity(os.stat("/dev/null"), "device fixture")
        self.assertTrue(stat.S_ISCHR(os.stat("/dev/null").st_mode))

    def test_parent_absolute_and_noncanonical_paths_are_rejected(self):
        for path in ("../outside.jsonl", "/absolute.jsonl", "nested/../a.jsonl",
                     "nested//b.jsonl", "./a.jsonl", "nested\\b.jsonl"):
            with self.subTest(path=path):
                self.shards[0]["path"] = path
                self.write_manifest()
                self.assert_invalid("canonical relative path")
                self.shards[0]["path"] = "a.jsonl"

    def test_directory_entry_replacement_after_read_is_rejected(self):
        def replace_file(identity, _path):
            if identity == "shard-a":
                replacement = self.root / "replacement"
                replacement.write_bytes((self.root / "a.jsonl").read_bytes())
                os.replace(replacement, self.root / "a.jsonl")

        self.assert_invalid("replaced during verification", after_stream=replace_file)

    def test_truncation_after_read_is_rejected(self):
        def truncate(identity, _path):
            if identity == "shard-a":
                os.truncate(self.root / "a.jsonl", 1)

        self.assert_invalid("changed during verification", after_stream=truncate)

    def test_growth_after_read_is_rejected(self):
        def grow(identity, _path):
            if identity == "shard-a":
                with (self.root / "a.jsonl").open("ab") as stream:
                    stream.write(b"x")

        self.assert_invalid("changed during verification", after_stream=grow)

    def test_declared_size_digest_and_byte_limits_are_enforced(self):
        self.shards[0]["bytes"] += 1
        self.write_manifest()
        self.assert_invalid("declared byte count")

        self.setUp_clean_shards()
        self.shards[0]["sha256"] = "0" * 64
        self.write_manifest()
        self.assert_invalid("SHA-256")

        self.setUp_clean_shards()
        limits = replace(self.limits, max_shard_bytes=self.shards[0]["bytes"] - 1)
        self.assert_invalid("shard byte limit", limits=limits)

        manifest = (self.root / "manifest.json").read_bytes()
        total = len(manifest) + sum(item["bytes"] for item in self.shards)
        limits = replace(self.limits, max_total_bytes=total - 1)
        self.assert_invalid("total byte limit", limits=limits)

    def setUp_clean_shards(self):
        self.shards[0]["path"] = "a.jsonl"
        self.write_shard(0, [{"record_id": "a-0", "value": "one"},
                             {"record_id": "a-1", "value": "two"}])
        self.write_shard(1, [{"record_id": "b-0", "value": "three"}])
        self.write_manifest()

    def test_manifest_and_shard_count_limits_are_enforced(self):
        manifest_size = (self.root / "manifest.json").stat().st_size
        self.assert_invalid("manifest.*byte limit",
                            limits=replace(self.limits, max_manifest_bytes=manifest_size - 1))

        third = {"identity": "shard-c", "path": "c.jsonl"}
        raw = encoded([{"record_id": "c-0"}])
        (self.root / "c.jsonl").write_bytes(raw)
        third.update(bytes=len(raw), sha256=digest(raw))
        self.shards.append(third)
        self.write_manifest()
        self.assert_invalid("shard count limit", limits=replace(self.limits, max_shards=2))

    def test_record_count_and_record_byte_limits_are_enforced(self):
        self.assert_invalid("record count limit", limits=replace(self.limits, max_records=2))
        size = max(len(line) for line in (self.root / "a.jsonl").read_bytes().splitlines())
        self.assert_invalid("record byte limit", limits=replace(self.limits, max_record_bytes=size - 1))

    def test_invalid_utf8_and_json_are_rejected(self):
        for raw, pattern in ((b'{"record_id":"bad-\xff"}\n', "UTF-8"),
                             (b'{"record_id":"bad",}\n', "JSON")):
            with self.subTest(pattern=pattern):
                self.write_shard(0, raw=raw)
                self.write_manifest()
                self.assert_invalid(pattern)

    def test_surrogate_escapes_are_controlled_integrity_errors(self):
        raw_cases = (
            b'{"record_id":"\\ud800"}\n',
            b'{"record_id":"valid","value":"\\udfff"}\n',
            b'{"record_id":"valid","\\ud800":1}\n',
        )
        for raw in raw_cases:
            with self.subTest(raw=raw):
                self.write_shard(0, raw=raw)
                self.write_manifest()
                self.assert_invalid("Unicode scalar|string")

        self.setUp_clean_shards()
        self.write_manifest(identity_field="\ud800")
        self.assert_invalid("Unicode scalar|string")
        self.setUp_clean_shards()
        self.assert_invalid("Unicode scalar", manifest="\ud800")
        self.assert_invalid("Unicode scalar", root="/\ud800")

    def test_surrogate_cli_failure_has_no_traceback(self):
        self.write_shard(0, raw=b'{"record_id":"\\ud800"}\n')
        self.write_manifest()
        process = subprocess.run(
            [sys.executable, str(Path(result_input.__file__)), str(self.root), "manifest.json"],
            text=True, capture_output=True, timeout=5, check=False)
        self.assertEqual(process.returncode, 1)
        self.assertEqual(process.stdout, "")
        self.assertIn("Unicode scalar", process.stderr)
        self.assertNotIn("Traceback", process.stderr)

    def test_nesting_and_string_limits_are_enforced_before_consumption(self):
        nested = b'{"record_id":"deep","v":{"a":{"b":{"c":1}}}}\n'
        self.write_shard(0, raw=nested)
        self.write_manifest()
        self.assert_invalid("nesting limit", limits=replace(self.limits, max_nesting=3))

        self.write_shard(0, [{"record_id": "wide", "value": "x" * 129}])
        self.write_manifest()
        self.assert_invalid("string byte limit")

    def test_duplicate_shard_identity_and_path_are_rejected(self):
        duplicate_identity = [dict(self.shards[0]), dict(self.shards[1])]
        duplicate_identity[1]["identity"] = duplicate_identity[0]["identity"]
        self.write_manifest(duplicate_identity)
        self.assert_invalid("duplicate shard identity")

        duplicate_path = [dict(self.shards[0]), dict(self.shards[1])]
        duplicate_path[1]["path"] = duplicate_path[0]["path"]
        self.write_manifest(duplicate_path)
        self.assert_invalid("duplicate shard path")

    def test_duplicate_record_identity_across_shards_is_rejected(self):
        self.write_shard(1, [{"record_id": "a-0", "value": "three"}])
        self.write_manifest()
        self.assert_invalid("duplicate record identity")

    def test_missing_or_nonstring_record_identity_is_rejected(self):
        for record in ({"value": "missing"}, {"record_id": 7}):
            with self.subTest(record=record):
                self.write_shard(0, [record])
                self.write_manifest()
                self.assert_invalid("record identity")

    def test_cross_shard_substitution_is_rejected(self):
        self.write_shard(0, [{"record_id": "a-0", "value": "aaaa"}])
        self.write_shard(1, [{"record_id": "b-0", "value": "bbbb"}])
        self.write_manifest()
        a = self.root / self.shards[0]["path"]
        b = self.root / self.shards[1]["path"]
        a_bytes, b_bytes = a.read_bytes(), b.read_bytes()
        self.assertEqual(len(a_bytes), len(b_bytes))
        a.write_bytes(b_bytes)
        b.write_bytes(a_bytes)
        self.assert_invalid("SHA-256")

    def test_root_entry_replacement_after_last_stream_is_rejected(self):
        moved = self.root.with_name(self.root.name + "-old")

        def replace_root(identity, _path):
            if identity == "shard-b":
                self.root.rename(moved)
                self.root.mkdir()

        try:
            self.assert_invalid("directory.*replaced|trusted root", after_stream=replace_root)
        finally:
            if moved.exists():
                self.root.rmdir()
                moved.rename(self.root)

    def test_nested_ancestor_replacement_after_stream_is_rejected(self):
        nested = self.root / "nested"
        moved = self.root / "nested-old"

        def replace_nested(identity, _path):
            if identity == "shard-b":
                nested.rename(moved)
                nested.mkdir()

        try:
            self.assert_invalid("directory.*replaced|ancestor", after_stream=replace_nested)
        finally:
            if moved.exists():
                nested.rmdir()
                moved.rename(nested)

    def test_nested_ancestor_unlink_from_namespace_is_rejected(self):
        nested = self.root / "nested"
        moved = self.root / "nested-unlinked"

        def unlink_nested(identity, _path):
            if identity == "shard-b":
                nested.rename(moved)

        try:
            self.assert_invalid("unlinked or replaced", after_stream=unlink_nested)
        finally:
            if moved.exists():
                moved.rename(nested)

    @unittest.skipUnless(Path("/proc/self/fd").is_dir(),
                         "descriptor leak accounting requires procfs")
    def test_post_open_unlink_does_not_leak_directory_descriptors(self):
        real_open = os.open
        opened = []

        root_parent = self.root / "root-open-race"
        root_parent.mkdir()
        root_target = root_parent / "victim"
        relative_target = self.root / "relative-open-race"
        root_descriptor = real_open(self.root, result_input._directory_flags())
        before = len(os.listdir("/proc/self/fd"))

        def open_then_unlink(target, component):
            def race(path, flags, mode=0o777, *, dir_fd=None):
                descriptor = real_open(path, flags, mode, dir_fd=dir_fd)
                if path == component:
                    target.rmdir()
                    opened.append(descriptor)
                return descriptor
            return race

        try:
            for _ in range(64):
                root_target.mkdir()
                with mock.patch.object(result_input, "_platform_supported"), \
                        mock.patch.object(result_input.os, "open",
                                          side_effect=open_then_unlink(root_target, "victim")):
                    with self.assertRaisesRegex(result_input.IntegrityError, "unlinked"):
                        result_input._open_root(root_target)

                relative_target.mkdir()
                with mock.patch.object(
                        result_input.os, "open",
                        side_effect=open_then_unlink(relative_target, "relative-open-race")):
                    with self.assertRaisesRegex(result_input.IntegrityError, "unlinked"):
                        result_input._open_regular(
                            root_descriptor, "relative-open-race/leaf", "race fixture")

            after = len(os.listdir("/proc/self/fd"))
            leaked = []
            for descriptor in set(opened):
                try:
                    os.fstat(descriptor)
                except OSError as error:
                    self.assertEqual(error.errno, errno.EBADF)
                else:
                    leaked.append(descriptor)
        finally:
            for descriptor in set(opened):
                try:
                    os.close(descriptor)
                except OSError as error:
                    if error.errno != errno.EBADF:
                        raise
            os.close(root_descriptor)
            if root_target.exists():
                root_target.rmdir()
            if relative_target.exists():
                relative_target.rmdir()
            root_parent.rmdir()

        self.assertEqual(after, before)
        self.assertEqual(leaked, [])

    def test_record_streams_across_fixed_read_chunks(self):
        value = "x" * (result_input.READ_CHUNK_BYTES + 17)
        self.write_shard(0, [{"record_id": "large", "value": value}])
        self.write_shard(1, [])
        self.write_manifest()
        limits = replace(self.limits,
                         max_total_bytes=4 * result_input.READ_CHUNK_BYTES,
                         max_shard_bytes=2 * result_input.READ_CHUNK_BYTES,
                         max_record_bytes=2 * result_input.READ_CHUNK_BYTES,
                         max_string_bytes=2 * result_input.READ_CHUNK_BYTES)
        receipt = self.verify(limits=limits)
        self.assertEqual(receipt["records"], 1)

    def test_each_limit_has_an_immutable_api_and_cli_cap(self):
        self.assertEqual(result_input.Limits(), result_input.Limits().checked())
        with self.assertRaises(TypeError):
            result_input.HARD_CAPS["max_record_bytes"] = 2 * 1024 * 1024
        for field, cap in result_input.HARD_CAPS.items():
            excessive = replace(result_input.Limits(), **{field: cap + 1})
            with self.subTest(field=field, interface="api"):
                with self.assertRaisesRegex(result_input.IntegrityError, "immutable hard cap"):
                    result_input.verify(self.root, "manifest.json", excessive)
            with self.subTest(field=field, interface="cli"):
                option = "--" + field.replace("_", "-")
                process = subprocess.run(
                    [sys.executable, str(Path(result_input.__file__)), option, str(cap + 1),
                     str(self.root), "manifest.json"],
                    text=True, capture_output=True, timeout=5, check=False)
                self.assertEqual(process.returncode, 1)
                self.assertEqual(process.stdout, "")
                self.assertIn("immutable hard cap", process.stderr)
                self.assertNotIn("Traceback", process.stderr)

        smaller = replace(result_input.Limits(), max_record_bytes=256)
        self.assertEqual(smaller, smaller.checked())

    def test_duplicate_json_object_keys_are_rejected(self):
        self.write_shard(0, raw=b'{"record_id":"one","record_id":"two"}\n')
        self.write_manifest()
        self.assert_invalid("duplicate JSON key")

    def test_manifest_cannot_name_itself_as_a_shard(self):
        manifest = (self.root / "manifest.json").read_bytes()
        self.shards[0] = {"identity": "shard-a", "path": "manifest.json",
                          "bytes": len(manifest), "sha256": digest(manifest)}
        self.write_manifest()
        self.assert_invalid("manifest cannot also be a shard")


if __name__ == "__main__":
    unittest.main()
