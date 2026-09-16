#!/usr/bin/env python3
"""Offline tests for the pinned external census closure boundary."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_retirement_external as external
import native_retirement_materializer as materializer


class ExternalClosureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.docs = self.root / "docs"
        self.docs.mkdir()
        self.checkout = self.root / "external" / "fixture"
        self.checkout.mkdir(parents=True)
        self._git("init", "-q")
        self._git("config", "user.email", "native-retirement@example.invalid")
        self._git("config", "user.name", "Native retirement test")
        self.header = self.checkout / "header.h"
        self.header.write_bytes(b"#define EXTERNAL_TEST 1\n")
        self._git("add", "header.h")
        self._git("commit", "-q", "-m", "fixture")
        self.revision = self._git("rev-parse", "HEAD")
        self._git("remote", "add", "origin", "https://github.com/example/fixture.git")
        self.record = {
            "kind": "project-header", "source": "external/fixture/header.h",
            "provenance": f"github/example/fixture/{self.revision}/header.h",
            "destination": "dependencies/project-include/external.h",
            "bytes": self.header.stat().st_size,
            "sha256": hashlib.sha256(self.header.read_bytes()).hexdigest(),
        }
        self.declaration = {"name": "fixture", "repository": "example/fixture",
                            "revision": self.revision, "path": "external/fixture"}

    def tearDown(self):
        self.temporary.cleanup()

    def _git(self, *arguments):
        result = subprocess.run(["git", "-C", os.fspath(self.checkout), *arguments],
                                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return result.stdout.strip()

    def _manifest(self, **extra):
        value = {"schema": materializer.SCHEMA, "version": materializer.VERSION,
                 "source_root": "..", "files": [self.record],
                 "external_checkouts": [self.declaration]}
        value.update(extra)
        path = self.docs / "dependencies.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_exact_revision_and_tracked_source_prepare(self):
        result = external.prepare(self._manifest(), self.root)
        self.assertEqual(result["checkouts"], 1)
        self.assertEqual(result["records"], 1)

    def test_revision_mutation_fails_closed(self):
        manifest = self._manifest(external_checkouts=[dict(self.declaration, revision="0" * 40)])
        with self.assertRaisesRegex(external.ExternalClosureError, "at .* expected"):
            external.prepare(manifest, self.root)

    def test_dirty_source_tamper_fails_closed(self):
        self.header.write_bytes(b"tampered\n")
        with self.assertRaisesRegex(external.ExternalClosureError, "dirty"):
            external.prepare(self._manifest(), self.root)

    def test_symlinked_checkout_path_fails_closed(self):
        real = self.root / "real-checkout"
        self.checkout.rename(real)
        self.checkout.symlink_to(real, target_is_directory=True)
        with self.assertRaisesRegex((external.ExternalClosureError, materializer.MaterializationError), "symlink"):
            external.prepare(self._manifest(), self.root)

    def test_generated_tamper_is_not_overwritten(self):
        target = self.root / "external" / "generated" / "header.h"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"old\n")
        with self.assertRaisesRegex(external.ExternalClosureError, "tamper"):
            external._publish_generated(target, b"new\n", "test generated")

    def test_repository_musl_arch_headers_are_target_specific_bits(self):
        descriptor_path = Path(__file__).resolve().parents[1] / "docs" / "native-retirement-dependencies-v1.json"
        descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
        records = descriptor["projects"]
        for arch in ("x86_64", "aarch64"):
            arch_records = [record for record in records
                            if record["source"].startswith(f"external/musl/arch/{arch}/bits/")]
            self.assertTrue(arch_records)
            for record in arch_records:
                self.assertIn(f"/musl/{arch}/include/bits/", record["destination"])
        generic = [record for record in records
                   if record["source"].startswith("external/musl/arch/generic/bits/")]
        self.assertTrue(generic)
        self.assertTrue(all("/musl/include/bits/" in record["destination"] for record in generic))


if __name__ == "__main__":
    unittest.main()
