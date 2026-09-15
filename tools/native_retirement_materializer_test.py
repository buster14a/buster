#!/usr/bin/env python3
"""Offline tests for the native-retirement dependency materializer."""

import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_retirement_materializer as materializer


def digest(data):
    return hashlib.sha256(data).hexdigest()


class MaterializerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(os.path.realpath(self.temporary.name))
        self.source_root = self.root / "repo"
        self.source_root.mkdir()
        self.header = self.source_root / "src" / "buster" / "lib" / "header.h"
        self.header.parent.mkdir(parents=True)
        self.header.write_bytes(b"#define ANSWER 42\n")
        self.records = [self.record("src/buster/lib/header.h", "dependencies/project-include/buster/lib/header.h",
                                    "project-header")]

    def tearDown(self):
        self.temporary.cleanup()

    def record(self, source, destination, kind="project-header", provenance=None, data=None):
        path = self.source_root / Path(source)
        if data is not None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        content = path.read_bytes()
        return {"kind": kind, "source": source, "provenance": provenance or "repo:" + source,
                "destination": destination, "bytes": len(content), "sha256": digest(content)}

    def manifest(self, records=None, **extra):
        value = {"schema": materializer.SCHEMA, "version": materializer.VERSION,
                 "files": self.records if records is None else records}
        value.update(extra)
        return value

    def test_authenticated_project_file_is_published_with_receipt(self):
        output = self.root / "out"
        receipt = materializer.materialize(self.manifest(), self.source_root, output)
        self.assertEqual(receipt["project_headers"], 1)
        self.assertEqual((output / "dependencies/project-include/buster/lib/header.h").read_bytes(), self.header.read_bytes())
        self.assertTrue((output / "dependencies.tsv").is_file())
        self.assertTrue((output / "dependency-manifest.json").is_file())

    def test_source_size_or_digest_mismatch_fails_without_output(self):
        record = dict(self.records[0], sha256="0" * 64)
        output = self.root / "out"
        with self.assertRaisesRegex(materializer.MaterializationError, "identity mismatch"):
            materializer.materialize(self.manifest([record]), self.source_root, output)
        self.assertFalse(output.exists())
        self.assertEqual(list(self.root.glob(".out.*")), [])
        stable_record = self.records[0]
        original_read = materializer._read_descriptor
        changed = [False]

        def change_after_read(descriptor):
            value = original_read(descriptor)
            if not changed[0]:
                changed[0] = True
                self.header.write_bytes(b"#define ANSWER 43\n")
            return value

        with mock.patch.object(materializer, "_read_descriptor", side_effect=change_after_read):
            with self.assertRaisesRegex(materializer.MaterializationError, "changed|identity mismatch"):
                materializer.materialize(self.manifest([stable_record]), self.source_root, self.root / "toctou")
        self.assertFalse((self.root / "toctou").exists())

    def test_missing_source_fails_closed(self):
        record = dict(self.records[0], source="src/missing.h", provenance="repo:src/missing.h")
        output = self.root / "out"
        with self.assertRaisesRegex(materializer.MaterializationError, "missing authenticated"):
            materializer.materialize(self.manifest([record]), self.source_root, output)
        self.assertFalse(output.exists())

    def test_symlink_source_is_rejected(self):
        linked = self.source_root / "src" / "buster" / "lib" / "linked.h"
        linked.symlink_to(self.header)
        record = self.record("src/buster/lib/linked.h", "dependencies/project-include/buster/lib/linked.h")
        output = self.root / "out"
        with self.assertRaisesRegex(materializer.MaterializationError, "symlink"):
            materializer.materialize(self.manifest([record]), self.source_root, output)
        self.assertFalse(output.exists())
        hardlinked = self.source_root / "src" / "buster" / "lib" / "hardlinked.h"
        os.link(self.header, hardlinked)
        hardlink_record = self.record("src/buster/lib/hardlinked.h", "dependencies/project-include/buster/lib/hardlinked.h")
        with self.assertRaisesRegex(materializer.MaterializationError, "hard-linked"):
            materializer.materialize(self.manifest([hardlink_record]), self.source_root, self.root / "hardlink")

    def test_normalized_duplicate_destinations_are_rejected(self):
        second_path = self.source_root / "src" / "other.h"
        second_path.parent.mkdir(parents=True, exist_ok=True)
        second_path.write_bytes(b"other\n")
        second = self.record("src/other.h", "dependencies/project-include/./buster/lib/header.h")
        with self.assertRaisesRegex(materializer.MaterializationError, "duplicate destination"):
            materializer.parse_manifest(self.manifest(self.records + [second]))

    def test_normalized_duplicate_provenance_is_rejected(self):
        second = dict(self.records[0], destination="dependencies/project-include/other.h")
        with self.assertRaisesRegex(materializer.MaterializationError, "duplicate provenance"):
            materializer.parse_manifest(self.manifest(self.records + [second]))

    def test_fixture_labels_cannot_inject_ledger_rows(self):
        for fixture in ("fixture\tother", "fixture\nother", "fixture\x1fother"):
            with self.subTest(fixture=repr(fixture)), self.assertRaisesRegex(
                    materializer.MaterializationError, "fixture.*control"):
                materializer.parse_manifest(self.manifest([dict(self.records[0], fixture=fixture)]))

    def test_identical_resource_project_collision_is_rejected(self):
        second = dict(self.records[0], kind="resource-header")
        with self.assertRaisesRegex(materializer.MaterializationError, "wrong include root"):
            materializer.parse_manifest(self.manifest(self.records + [second]))
        resource = self.record("src/buster/lib/header.h", "dependencies/resource-include/same.h", "resource-header",
                              provenance="repo:resource/same.h")
        project = dict(self.records[0], destination="dependencies/project-include/same.h")
        with self.assertRaisesRegex(materializer.MaterializationError, "global include namespace collision"):
            materializer.parse_manifest(self.manifest([resource, project]))

    def test_exact_same_resource_is_deduplicated_deterministically(self):
        duplicate = dict(self.records[0], source="src\\buster\\lib\\header.h",
                         destination="dependencies\\project-include\\buster\\lib\\header.h",
                         provenance="repo:src\\buster\\lib\\header.h")
        records, _metadata = materializer.parse_manifest(self.manifest(self.records + [duplicate]))
        self.assertEqual(len(records), 1)
        first = materializer.materialize(self.manifest(self.records + [duplicate]), self.source_root, self.root / "first")
        second = materializer.materialize(self.manifest([duplicate, self.records[0]]), self.source_root, self.root / "second")
        self.assertEqual(first, second)
        self.assertEqual((self.root / "first/dependencies.tsv").read_bytes(),
                         (self.root / "second/dependencies.tsv").read_bytes())

    def test_generated_metadata_names_are_reserved(self):
        for destination in ("generated/manifest.txt", "dependencies/project-include/dependency-receipt.json"):
            record = dict(self.records[0], destination=destination)
            with self.subTest(destination=destination), self.assertRaisesRegex(
                    materializer.MaterializationError, "reserves generated metadata"):
                materializer.parse_manifest(self.manifest([record]))
        for destination in ("arbitrary/same.h", "dependencies/resource-include/same.h"):
            with self.subTest(destination=destination), self.assertRaisesRegex(
                    materializer.MaterializationError, "wrong include root"):
                materializer.parse_manifest(self.manifest([dict(self.records[0], destination=destination)]))

    def test_metadata_collisions_are_rejected(self):
        with self.assertRaisesRegex(materializer.MaterializationError, "metadata collision"):
            materializer.parse_manifest(self.manifest(metadata=[{"name": "metadata/foo"},
                                                               {"name": "metadata/./foo"}]))

    def test_existing_output_is_never_overwritten(self):
        output = self.root / "out"
        output.mkdir()
        marker = output / "keep"
        marker.write_bytes(b"keep")
        with self.assertRaisesRegex(materializer.MaterializationError, "already exists"):
            materializer.materialize(self.manifest(), self.source_root, output)
        self.assertEqual(marker.read_bytes(), b"keep")
        real_parent = self.root / "real-parent"
        real_parent.mkdir()
        linked_parent = self.root / "linked-parent"
        linked_parent.symlink_to(real_parent, target_is_directory=True)
        with self.assertRaisesRegex(materializer.MaterializationError, "output parent.*symlink"):
            materializer.materialize(self.manifest(), self.source_root, linked_parent / "out")

    def test_self_test_and_receipt_are_stable(self):
        self.assertEqual(materializer._self_test(), {"schema": materializer.SCHEMA, "self_test": True})
        for provenance in ("https://example.invalid/header.h", "repo:/absolute.h", "ssh://example.invalid/header.h",
                           "scp://example.invalid/header.h", "git@example.invalid:repo/header.h",
                           "ssh:example.invalid/repo/header.h"):
            with self.subTest(provenance=provenance), self.assertRaises(materializer.MaterializationError):
                materializer.parse_manifest(self.manifest([dict(self.records[0], provenance=provenance)]))
        receipt = materializer.materialize(self.manifest(), self.source_root, self.root / "out")
        recorded = json.loads((self.root / "out/dependency-manifest.json").read_text())
        self.assertEqual(recorded, receipt)
        self.assertEqual(receipt["files_by_destination"], ["dependencies/project-include/buster/lib/header.h"])
        descriptor_root = self.root / "descriptor-repo"
        descriptor_header = descriptor_root / "src" / "buster" / "lib" / "header.h"
        descriptor_header.parent.mkdir(parents=True)
        descriptor_header.write_bytes(b"#define ANSWER 42\n")
        descriptor_path = descriptor_root / "docs" / "dependencies.json"
        descriptor_path.parent.mkdir(parents=True)
        descriptor_record = {
            "kind": "project-header", "source": "src/buster/lib/header.h",
            "provenance": "repo:src/buster/lib/header.h",
            "destination": "dependencies/project-include/buster/lib/header.h",
            "bytes": descriptor_header.stat().st_size, "sha256": digest(descriptor_header.read_bytes()),
        }
        descriptor = {"schema": materializer.SCHEMA, "version": materializer.VERSION,
                      "source_root": "..", "files": [descriptor_record]}
        descriptor_path.write_text(json.dumps(descriptor), encoding="utf-8")
        archived = materializer.materialize_file(descriptor_path, descriptor_root, self.root / "archived")
        self.assertEqual(archived["descriptor_path"], "docs/dependencies.json")
        self.assertEqual(archived["descriptor_sha256"], digest(descriptor_path.read_bytes()))
        archived_again = materializer.materialize_file(descriptor_path, descriptor_root, self.root / "archived-again")
        self.assertEqual(archived_again, archived)
        repository = Path(__file__).resolve().parents[1]
        production_descriptor = json.loads(
            (repository / "docs/native-retirement-dependencies-v1.json").read_text(encoding="utf-8"))
        production_records, _metadata = materializer.parse_manifest(production_descriptor)
        replay = materializer._archived_replay(production_descriptor, production_records)
        materializer._verify_archived_fixture_inputs(replay, repository)
        self.assertEqual(replay["projection"], {
            "fixtures": 28, "mir_candidate_rows": 4032,
            "repo_owned_project_header_rows_closed": 264,
            "remaining_diagnostic_rows": 3768,
            "ios_simd_rows_pending_targetconditionals": 24})
        self.assertEqual(replay["row_identity_sha256"],
                         "9604102b75a14631aeb1d6a3652d36506a05928a0046c52cc50a00b942826ce6")
        self.assertEqual(len(replay["rows"]), 4032)
        production_descriptor["archived_replay"]["projection"]["row_identity_sha256"] = "0" * 64
        with self.assertRaisesRegex(materializer.MaterializationError, "row identity digest"):
            materializer._archived_replay(production_descriptor, production_records)
        with self.assertRaisesRegex(materializer.MaterializationError, "descriptor-relative root"):
            materializer.materialize_file(repository / "docs/native-retirement-dependencies-v1.json",
                                          self.root, self.root / "alternate-root")

    def test_descriptor_source_root_must_use_canonical_spelling(self):
        descriptor = self.root / "repo" / "docs" / "dependencies.json"
        descriptor.parent.mkdir(parents=True)
        descriptor.write_text(json.dumps(self.manifest(source_root=".././repo")), encoding="utf-8")
        with self.assertRaisesRegex(materializer.MaterializationError, "source_root is not canonical"):
            materializer.materialize_file(descriptor, self.root, self.root / "noncanonical")

        descriptor.write_text(json.dumps(self.manifest(source_root="..")), encoding="utf-8")
        receipt = materializer.materialize_file(descriptor, self.root / "repo", self.root / "canonical")
        self.assertEqual(receipt["project_headers"], 1)

    def test_external_records_require_an_authenticated_checkout_declaration(self):
        external = self.source_root / "external" / "fixture" / "header.h"
        external.parent.mkdir(parents=True)
        external.write_bytes(b"external\n")
        record = self.record("external/fixture/header.h", "dependencies/project-include/external.h",
                             provenance="github/example/fixture/" + "0" * 40 + "/header.h")
        with self.assertRaisesRegex(materializer.MaterializationError, "declared closure"):
            materializer.parse_manifest(self.manifest([record]))

    def test_external_declarations_are_bound_in_receipt(self):
        external = self.source_root / "external" / "fixture" / "header.h"
        external.parent.mkdir(parents=True)
        external.write_bytes(b"external\n")
        record = self.record("external/fixture/header.h", "dependencies/project-include/external.h",
                             provenance="github/example/fixture/" + "0" * 40 + "/header.h")
        declaration = {"name": "fixture", "repository": "example/fixture",
                       "revision": "0" * 40, "path": "external/fixture"}
        receipt = materializer.materialize(self.manifest([record], external_checkouts=[declaration]),
                                           self.source_root, self.root / "external-out")
        self.assertEqual(receipt["external_checkouts"], [declaration])
        self.assertEqual(receipt["external_generated"], [])
        self.assertEqual(len(receipt["external_closure_sha256"]), 64)

    def test_external_declaration_rejects_revision_and_path_mutation(self):
        declaration = {"name": "fixture", "repository": "example/fixture",
                       "revision": "F" * 40, "path": "external/not-fixture"}
        with self.assertRaisesRegex(materializer.MaterializationError, "revision"):
            materializer.parse_manifest(self.manifest(external_checkouts=[declaration]))
        declaration["revision"] = "0" * 40
        with self.assertRaisesRegex(materializer.MaterializationError, "path"):
            materializer.parse_manifest(self.manifest(external_checkouts=[declaration]))


if __name__ == "__main__":
    unittest.main()
