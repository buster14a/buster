#!/usr/bin/env python3
"""Offline history-gate regressions; run by Native retirement history CI."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_retirement_inventory as inventory


def sha(data):
    return hashlib.sha256(data).hexdigest()


def expected(data=b"original archive"):
    return {"id": 1, "name": "native-retirement-test", "size": len(data), "sha256": sha(data),
            "run": 10, "workflow_commit": "a" * 40, "candidate_commit": "b" * 40,
            "candidate_tree": "c" * 40, "expires_at_observed": "2026-09-19T19:21:24Z"}


def asset(name, data, identity=1):
    return {"id": identity, "name": name, "size": len(data), "state": "uploaded",
            "digest": "sha256:" + sha(data)}


class HistoryTests(unittest.TestCase):
    def test_whole_archive(self):
        data = b"original archive"
        item = expected(data)
        part = asset(item["name"] + ".zip", data)
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / part["name"]).write_bytes(data)
            selected = inventory.select_parts(item, [part])
            self.assertEqual(inventory.verify_parts(item, selected, directory),
                             {"size": len(data), "sha256": sha(data)})

    def test_split_archive_is_ordered_and_bound_to_original(self):
        item = expected(b"firstsecond")
        parts = [asset(item["name"] + ".zip.part-00", b"first"),
                 asset(item["name"] + ".zip.part-01", b"second", 2)]
        with tempfile.TemporaryDirectory() as directory:
            for part, data in zip(parts, (b"first", b"second")):
                (Path(directory) / part["name"]).write_bytes(data)
            selected = inventory.select_parts(item, list(reversed(parts)))
            self.assertEqual(inventory.verify_parts(item, selected, directory)["sha256"], item["sha256"])

    def test_self_consistent_replacement_is_not_original_evidence(self):
        # Existing per-asset checks are necessary but not the original-artifact anchor.
        item = expected(b"original")
        replacement = b"replaced"
        part = asset(item["name"] + ".zip", replacement)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / part["name"]
            path.write_bytes(replacement)
            inventory.archive.checked(path, part["size"], part["digest"][7:])
            with self.assertRaisesRegex(ValueError, "original Actions"):
                inventory.verify_parts(item, [part], directory)

    def test_modified_download_fails_asset_check(self):
        item = expected(b"original")
        part = asset(item["name"] + ".zip", b"original")
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / part["name"]).write_bytes(b"replaced")
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                inventory.verify_parts(item, [part], directory)

    def test_missing_and_ambiguous_parts(self):
        item = expected(b"ab")
        prefix = item["name"] + ".zip"
        bad = [[asset(prefix + ".part-01", b"ab")],
               [asset(prefix, b"ab"), asset(prefix + ".part-00", b"ab")],
               [asset(prefix + ".part-00", b"a"), asset(prefix + ".part-000", b"b")],
               [asset(prefix + ".part-bad", b"ab")],
               [asset(prefix + ".part-00", b"a")]]
        for parts in bad:
            with self.subTest(parts=parts), self.assertRaises(ValueError):
                inventory.select_parts(item, parts)
        self.assertEqual(inventory.select_parts(item, []), [])
        with self.assertRaisesRegex(ValueError, "no durable"):
            inventory.verify_parts(item, [], ".")

    def test_expiration_is_live_metadata_not_source_substitution(self):
        item = expected()
        actual = {"id": item["id"], "name": item["name"], "size_in_bytes": item["size"],
                  "digest": "sha256:" + item["sha256"], "workflow_run": {"id": 10, "head_sha": "a" * 40},
                  "expired": True, "expires_at": "2026-09-18T00:00:00Z"}
        self.assertEqual(inventory.origin_errors(item, actual), [])
        changed = copy.deepcopy(actual)
        changed["workflow_run"]["head_sha"] = "d" * 40
        self.assertIn("Actions producing run/revision differs from frozen identity", inventory.origin_errors(item, changed))
        for value in (None, "invalid", "2026-09-19T00:00:00"):
            changed = dict(actual, expires_at=value)
            self.assertTrue(inventory.origin_errors(item, changed))

    def test_origin_byte_identity_mismatch(self):
        item = expected()
        self.assertTrue(inventory.origin_errors(item, {}))

    def test_catalog_rejects_duplicates_and_missing_identities(self):
        catalog = {"schema": inventory.SCHEMA, "repository": inventory.REPOSITORY,
                   "durable_releases": [{"tag": "test", "artifact_ids": [1]}],
                   "artifacts": [expected()]}
        self.assertEqual(len(inventory.check_catalog(catalog)), 1)
        self.assertEqual(inventory.destination_tags(catalog, catalog["artifacts"]), {1: "test"})
        for artifacts in ([], [expected(), expected()], [dict(expected(), candidate_commit="main")],
                          [dict(expected(), sha256="unknown")]):
            with self.subTest(artifacts=artifacts), self.assertRaises(ValueError):
                inventory.check_catalog(dict(catalog, artifacts=artifacts))
        for releases in ([], [{"tag": "test", "artifact_ids": []}],
                         [{"tag": "test", "artifact_ids": [1, 1]}],
                         [{"tag": "bad tag", "artifact_ids": [1]}]):
            with self.subTest(releases=releases), self.assertRaises(ValueError):
                inventory.destination_tags(dict(catalog, durable_releases=releases), catalog["artifacts"])

    def test_recorded_history_keeps_original_newer_and_failed_bundles(self):
        path = Path(__file__).resolve().parents[1] / "docs/performance-audits/evidence/2026-09-12-retirement/history-catalog.json"
        items = {item["id"]: item for item in inventory.check_catalog(json.loads(path.read_text(encoding="utf-8")))}
        self.assertTrue({10304875115, 10304761452, 10304213369, 10303964052, 10304642722,
                         10305878954, 10305707825, 10308181383, 10307414648} <= set(items))
        self.assertEqual(items[10304761452]["recorded_outcome"], "strict-packaging-failed-raw-retained")
        self.assertEqual(items[10308181383]["expires_at_observed"], "2026-09-20T00:14:29Z")
        self.assertIsNone(items[10308181383]["candidate_binary_sha256"])
        self.assertNotEqual(items[10303964052]["sha256"], items[10305878954]["sha256"])


if __name__ == "__main__":
    unittest.main()
