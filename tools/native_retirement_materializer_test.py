#!/usr/bin/env python3
"""Offline tests for the native-retirement dependency materializer.

Extracted from #645's preserved donor. MaterializerTests retains its reusable
cases; ArchivedReplayTests supplies its own small corpus instead of requiring
the donor's live 28-fixture descriptor and historical support ledger.
"""

import copy
import hashlib
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_retirement_dependency_binding as dependency_authority
import native_retirement_materializer as materializer


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical_digest(value):
    return digest(json.dumps(value, ensure_ascii=True, sort_keys=True,
                             separators=(",", ":")).encode("utf-8"))


class MaterializerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(os.path.realpath(self.temporary.name))
        self.source_root = self.root / "repo"
        self.source_root.mkdir()
        self.header = self.source_root / "src" / "buster" / "lib" / "header.h"
        self.header.parent.mkdir(parents=True)
        self.header.write_bytes(b"#define ANSWER 42\n")
        self.records = [self.record("src/buster/lib/header.h", "dependencies/project-include/buster/lib/header.h",
                                    "project-header")]

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
        for field, value in (("sha256", "0" * 64), ("bytes", self.records[0]["bytes"] + 1)):
            record = dict(self.records[0], **{field: value})
            output = self.root / "out"
            with self.subTest(field=field), self.assertRaisesRegex(materializer.MaterializationError, "identity mismatch"):
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
        _binding, production_descriptor, _snapshot = dependency_authority.load_authority(repository)
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
            materializer.materialize_file(descriptor_path, self.root, self.root / "alternate-root")

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

    def test_cli_materialize_and_failure_exit_status(self):
        descriptor = self.source_root / "dependencies.json"
        descriptor.write_text(json.dumps(self.manifest()), encoding="utf-8")
        output = self.root / "cli-out"
        command = [sys.executable, materializer.__file__, "materialize", "--manifest", str(descriptor),
                   "--source-root", str(self.source_root), "--output", str(output)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["project_headers"], 1)
        self.assertEqual((output / self.records[0]["destination"]).read_bytes(), self.header.read_bytes())
        before = (output / "dependency-manifest.json").read_bytes()
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("already exists", result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertEqual((output / "dependency-manifest.json").read_bytes(), before)


class ArchivedReplayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.header = self.root / "src" / "header.h"
        self.header.parent.mkdir()
        self.header.write_bytes(b"#define ANSWER 42\n")
        self.fixture = self.root / "tests" / "fixture.c"
        self.fixture.parent.mkdir()
        self.fixture.write_bytes(b"int main(void) { return 0; }\n")
        self.contract = self.root / "docs" / "native-retirement-support-v1.tsv"
        self.contract.parent.mkdir()
        self.contract.write_text(
            "path\trole\tobligation\tbytes\tsha256\n"
            f"tests/fixture.c\tsubject\tcompile\t{self.fixture.stat().st_size}\t{digest(self.fixture.read_bytes())}\n",
            encoding="utf-8")
        targets = ["x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
                   "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
                   "x86_64-apple-macos", "aarch64-apple-macos",
                   "x86_64-linux-android", "aarch64-linux-android",
                   "x86_64-apple-ios", "aarch64-apple-ios",
                   "x86_64-unknown-uefi", "aarch64-unknown-uefi"]
        axes = {"targets": targets, "frontend_lowering": ["local-backed-canonical", "direct-ssa"],
                "PIC": ["0", "1"], "allocators": ["mir-stack", "fast", "quality"]}
        item = {"fixture": "tests/fixture.c", "input_sha256": digest(self.fixture.read_bytes()),
                "project_headers": ["header.h"], "closed_targets": [targets[0]],
                "ios_pending_targets": ["aarch64-apple-ios"]}
        self.rows = []
        for target, frontend, pic, allocator in itertools.product(
                targets, axes["frontend_lowering"], axes["PIC"], axes["allocators"]):
            disposition = ("repo-owned-project-header" if target == targets[0] else
                           "ios-simd-pending-targetconditionals" if target == "aarch64-apple-ios" else "diagnostic")
            self.rows.append({"row": len(self.rows), "fixture": "tests/fixture.c", "target": target,
                              "frontend_lowering": frontend, "PIC": pic, "allocator": allocator,
                              "disposition": disposition,
                              "project_headers": ["header.h"] if target == targets[0] else []})
        self.counts = {"fixtures": 1, "mir_candidate_rows": 144,
                       "repo_owned_project_header_rows_closed": 12,
                       "remaining_diagnostic_rows": 132,
                       "ios_simd_rows_pending_targetconditionals": 12}
        projection = dict(self.counts,
                          row_identity_sha256=canonical_digest(self.rows),
                          input_identity_sha256=canonical_digest({"tests/fixture.c": item["input_sha256"]}),
                          fixture_map_sha256=canonical_digest([item]))
        self.manifest = {"schema": materializer.SCHEMA, "version": 1,
                         "files": [{"kind": "project-header", "source": "src/header.h",
                                    "provenance": "repo:src/header.h",
                                    "destination": "dependencies/project-include/header.h",
                                    "bytes": self.header.stat().st_size, "sha256": digest(self.header.read_bytes())}],
                         "archived_replay": {"schema": "buster-native-archived-replay-v1", "version": 1,
                                             "axes": axes, "fixtures": [item], "projection": projection}}
        self.records, _metadata = materializer.parse_manifest(self.manifest)

    def test_projection_and_authenticated_publication_are_stable(self):
        replay = materializer._archived_replay(self.manifest, self.records)
        self.assertEqual(replay["projection"], self.counts)
        self.assertEqual(replay["rows"], self.rows)
        # A synthetic fixture needs its own trust anchor. Production's pin and
        # validator are untouched; the unpatched rejection is tested below.
        with mock.patch.object(materializer, "SUPPORT_CONTRACT_SHA256", digest(self.contract.read_bytes())):
            materializer._verify_archived_fixture_inputs(replay, self.root)
            first = materializer.materialize(self.manifest, self.root, self.root / "first")
            second = materializer.materialize(self.manifest, self.root, self.root / "second")
        self.assertEqual(first, second)
        self.assertEqual(first["archived_replay"]["rows"], self.rows)
        self.assertEqual((self.root / "first/dependencies.tsv").read_bytes(),
                         (self.root / "second/dependencies.tsv").read_bytes())
        self.assertEqual((self.root / "first/dependencies/project-include/header.h").read_bytes(), self.header.read_bytes())

    def test_all_projection_counts_and_digests_reject_mutation(self):
        for field, value in self.manifest["archived_replay"]["projection"].items():
            changed = copy.deepcopy(self.manifest)
            changed["archived_replay"]["projection"][field] = value + 1 if isinstance(value, int) else "0" * 64
            with self.subTest(field=field), self.assertRaisesRegex(materializer.MaterializationError, "mismatch"):
                materializer._archived_replay(changed, self.records)

    def test_unpatched_historical_pin_rejects_other_contract(self):
        self.assertIn(digest((Path(__file__).resolve().parents[1] /
                              "docs/native-retirement-support-v1.tsv").read_bytes()),
                      (materializer.SUPPORT_CONTRACT_SHA256,
                       materializer.NEXT_SUPPORT_CONTRACT_SHA256))
        with self.assertRaisesRegex(materializer.MaterializationError, "support contract identity mismatch"):
            materializer.materialize(self.manifest, self.root, self.root / "wrong-contract")
        self.assertFalse((self.root / "wrong-contract").exists())
        self.assertEqual(list(self.root.glob(".wrong-contract.*")), [])

    def test_fixture_drift_rejects_publication(self):
        pin = digest(self.contract.read_bytes())
        self.fixture.write_bytes(b"int main(void) { return 1; }\n")
        with mock.patch.object(materializer, "SUPPORT_CONTRACT_SHA256", pin):
            with self.assertRaisesRegex(materializer.MaterializationError, "fixture identity mismatch"):
                materializer.materialize(self.manifest, self.root, self.root / "drift")
        self.assertFalse((self.root / "drift").exists())
        self.assertEqual(list(self.root.glob(".drift.*")), [])

    def test_axis_and_header_mapping_mutations_reject(self):
        for axis in self.manifest["archived_replay"]["axes"]:
            changed = copy.deepcopy(self.manifest)
            changed["archived_replay"]["axes"][axis].pop()
            with self.subTest(axis=axis), self.assertRaisesRegex(materializer.MaterializationError, "authenticated matrix"):
                materializer._archived_replay(changed, self.records)
        changed = copy.deepcopy(self.manifest)
        changed["archived_replay"]["fixtures"][0]["project_headers"] = ["missing.h"]
        with self.assertRaisesRegex(materializer.MaterializationError, "unauthenticated project header"):
            materializer._archived_replay(changed, self.records)


if __name__ == "__main__":
    unittest.main()
