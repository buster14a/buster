#!/usr/bin/env python3
"""Tests for deterministic native-retirement dependency rebinding."""

import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import native_retirement_materializer as materializer
import native_retirement_rebind as rebind


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def canonical_json(value):
    return (json.dumps(value, ensure_ascii=True, indent=2) + "\n").encode("utf-8")


class NativeRetirementRebindTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        (self.root / "docs").mkdir()
        (self.root / "tools").mkdir()
        (self.root / "src").mkdir()
        (self.root / "external/vendor").mkdir(parents=True)
        (self.root / "sdk-headers/test").mkdir(parents=True)
        self.alpha = self.root / "src/alpha.h"
        self.beta = self.root / "src/beta.h"
        self.external = self.root / "external/vendor/external.h"
        self.resource = self.root / "external/vendor/resource.h"
        self.sdk = self.root / "sdk-headers/test/sdk.h"
        self.alpha.write_bytes(b"#define LEFT 1\n#define MIDDLE 1\n#define RIGHT 1\n")
        self.beta.write_bytes(b"#define BETA 1\n")
        self.external.write_bytes(b"#define EXTERNAL_PIN 1\n")
        self.resource.write_bytes(b"#define RESOURCE_PIN 1\n")
        self.sdk.write_bytes(b"#define SDK_PIN 1\n")
        self._write_sdk_manifest()
        self._write_descriptor()
        self._write_bindings(self._identities())

    def tearDown(self):
        self.temporary.cleanup()

    def _record(self, source, destination):
        data = (self.root / source).read_bytes()
        if source.startswith("src/"):
            provenance = f"repo:{source}"
        elif source.startswith("sdk-headers/"):
            provenance = f"sdk/{'2' * 64}/include/sdk.h"
        else:
            provenance = f"github/example/vendor/{'1' * 40}/{Path(source).name}"
        return {
            "source": source,
            "provenance": provenance,
            "destination": destination,
            "bytes": len(data),
            "sha256": sha256(data),
        }

    def _sdk_manifest(self):
        data = self.sdk.read_bytes()
        return {
            "version": 1,
            "archives": [{
                "name": "sdk",
                "url": "https://example.invalid/sdk.tar.xz",
                "sha256": "2" * 64,
                "bytes": 100,
            }],
            "files": [{
                "archive": "sdk",
                "member": "include/sdk.h",
                "source": "sdk-headers/test/sdk.h",
                "bytes": len(data),
                "sha256": sha256(data),
            }],
        }

    def _write_sdk_manifest(self):
        (self.root / rebind.SDK_MANIFEST_PATH).write_bytes(canonical_json(self._sdk_manifest()))

    def _manifest(self):
        return {
            "schema": materializer.SCHEMA,
            "version": materializer.VERSION,
            "source_root": "..",
            "metadata": [],
            "external_checkouts": [{
                "name": "vendor",
                "repository": "example/vendor",
                "revision": "1" * 40,
                "path": "external/vendor",
            }],
            "external_generated": [],
            "projects": [
                self._record("src/alpha.h", "dependencies/project-include/alpha.h"),
                self._record("src/beta.h", "dependencies/project-include/beta.h"),
                self._record("external/vendor/external.h", "dependencies/project-include/external.h"),
                self._record("sdk-headers/test/sdk.h", "dependencies/project-include/sdk.h"),
            ],
            "resources": [
                self._record("external/vendor/resource.h", "dependencies/resource-include/resource.h"),
            ],
        }

    def _write_descriptor(self, manifest=None):
        if manifest is None:
            manifest = self._manifest()
        (self.root / rebind.DESCRIPTOR_PATH).write_bytes(canonical_json(manifest))

    def _identities(self):
        descriptor = (self.root / rebind.DESCRIPTOR_PATH).read_bytes()
        manifest = json.loads(descriptor)
        output = self.root / "initial-materialized"
        receipt = materializer.materialize(
            manifest,
            self.root,
            output,
            descriptor_sha256=sha256(descriptor),
            descriptor_path=rebind.DESCRIPTOR_PATH,
        )
        receipt_raw = (output / "dependency-manifest.json").read_bytes()
        ledger_raw = (output / "dependencies.tsv").read_bytes()
        identities = {
            "descriptor_sha256": sha256(descriptor),
            "receipt_sha256": sha256(receipt_raw),
            "project_sha256": receipt["project_include_sha256"],
            "ledger_sha256": sha256(ledger_raw),
        }
        import shutil
        shutil.rmtree(output)
        return identities

    def _write_bindings(self, identities):
        document = f"""# Fixture census

The binding values are frozen in both the C producer and the independent
validator: descriptor SHA-256
`{identities['descriptor_sha256']}`, materializer
receipt SHA-256
`{identities['receipt_sha256']}`, project
closure SHA-256
`{identities['project_sha256']}`, and
materializer ledger SHA-256
`{identities['ledger_sha256']}`.
"""
        c_source = "".join((
            f'BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_descriptor_sha256 = S8_INITIALIZER("{identities["descriptor_sha256"]}");\n',
            f'BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_receipt_sha256 = S8_INITIALIZER("{identities["receipt_sha256"]}");\n',
            f'BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_project_sha256 = S8_INITIALIZER("{identities["project_sha256"]}");\n',
            f'BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_ledger_sha256 = S8_INITIALIZER("{identities["ledger_sha256"]}");\n',
        ))
        python_source = "".join((
            f'FULL_DEPENDENCY_DESCRIPTOR_SHA256 = "{identities["descriptor_sha256"]}"\n',
            f'FULL_DEPENDENCY_RECEIPT_SHA256 = "{identities["receipt_sha256"]}"\n',
            f'FULL_DEPENDENCY_PROJECT_SHA256 = "{identities["project_sha256"]}"\n',
            f'FULL_DEPENDENCY_LEDGER_SHA256 = "{identities["ledger_sha256"]}"\n',
            'FULL_EXTERNAL_CHECKOUTS = (\n',
            '    {"name": "vendor", "repository": "example/vendor", "revision": "' + "1" * 40 + '", "path": "external/vendor"},\n',
            ')\n',
            'FULL_EXTERNAL_GENERATED = ()\n',
        ))
        (self.root / rebind.DOCUMENT_PATH).write_text(document, encoding="utf-8")
        (self.root / rebind.C_BINDING_PATH).write_text(c_source, encoding="utf-8")
        (self.root / rebind.PYTHON_BINDING_PATH).write_text(python_source, encoding="utf-8")

    def _snapshot(self):
        return {path: (self.root / path).read_bytes() for path in rebind.TARGET_PATHS}

    def _tree_snapshot(self):
        return {
            path.relative_to(self.root).as_posix(): path.read_bytes()
            for path in sorted(self.root.rglob("*")) if path.is_file()
        }

    def _descriptor_records(self):
        descriptor = json.loads((self.root / rebind.DESCRIPTOR_PATH).read_text(encoding="utf-8"))
        return {record["source"]: record for record in descriptor["projects"]}

    def test_check_is_read_only_and_refresh_is_idempotent(self):
        self.alpha.write_bytes(b"#define LEFT 2\n#define MIDDLE 1\n#define RIGHT 1\n")
        before = self._snapshot()
        tree_before = self._tree_snapshot()
        checked = rebind.run("check", self.root)
        self.assertEqual(checked["status"], "stale")
        self.assertEqual([row["source"] for row in checked["source_changes"]], ["src/alpha.h"])
        self.assertEqual(checked["files_changed"], list(rebind.TARGET_PATHS))
        self.assertEqual(self._snapshot(), before)
        self.assertEqual(self._tree_snapshot(), tree_before)

        refreshed = rebind.run("refresh", self.root)
        self.assertEqual(refreshed["status"], "refreshed")
        current = rebind.run("check", self.root)
        self.assertEqual(current["status"], "current")
        self.assertEqual(current["files_changed"], [])
        after = self._snapshot()
        second = rebind.run("refresh", self.root)
        self.assertEqual(second["status"], "current")
        self.assertEqual(self._snapshot(), after)

    def test_combines_changes_to_different_project_headers(self):
        branch_alpha = b"#define LEFT 10\n#define MIDDLE 1\n#define RIGHT 1\n"
        branch_beta = b"#define BETA 20\n"
        self.alpha.write_bytes(branch_alpha)
        self.beta.write_bytes(branch_beta)
        report = rebind.run("refresh", self.root)
        self.assertEqual([row["source"] for row in report["source_changes"]],
                         ["src/alpha.h", "src/beta.h"])
        records = self._descriptor_records()
        self.assertEqual(records["src/alpha.h"]["sha256"], sha256(branch_alpha))
        self.assertEqual(records["src/beta.h"]["sha256"], sha256(branch_beta))
        self.assertEqual(rebind.run("check", self.root)["status"], "current")

    def test_combines_distinct_regions_of_one_project_header(self):
        base = self.alpha.read_bytes()
        branch_left = base.replace(b"LEFT 1", b"LEFT 30")
        branch_right = base.replace(b"RIGHT 1", b"RIGHT 40")
        merged = branch_left.replace(b"RIGHT 1", b"RIGHT 40")
        self.assertNotEqual(merged, branch_left)
        self.assertNotEqual(merged, branch_right)
        self.alpha.write_bytes(merged)
        rebind.run("refresh", self.root)
        record = self._descriptor_records()["src/alpha.h"]
        self.assertEqual(record["bytes"], len(merged))
        self.assertEqual(record["sha256"], sha256(merged))
        self.assertIn(b"LEFT 30", merged)
        self.assertIn(b"RIGHT 40", merged)

    def test_pinned_external_and_sdk_drift_fail_without_partial_writes(self):
        for path in (self.external, self.sdk):
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.write_bytes(original + b"// drift\n")
                before = self._snapshot()
                with self.assertRaisesRegex(ValueError, "identity mismatch"):
                    rebind.run("refresh", self.root)
                self.assertEqual(self._snapshot(), before)
                path.write_bytes(original)

    def test_missing_project_source_fails_without_partial_writes(self):
        self.alpha.unlink()
        before = self._snapshot()
        with self.assertRaisesRegex(ValueError, "missing repository project source"):
            rebind.run("refresh", self.root)
        self.assertEqual(self._snapshot(), before)

    def test_changed_external_pin_declaration_fails_without_partial_writes(self):
        descriptor = json.loads((self.root / rebind.DESCRIPTOR_PATH).read_text(encoding="utf-8"))
        descriptor["external_checkouts"][0]["revision"] = "3" * 40
        self._write_descriptor(descriptor)
        before = self._snapshot()
        with self.assertRaisesRegex(rebind.RebindError, "external checkout pins differ"):
            rebind.run("refresh", self.root)
        self.assertEqual(self._snapshot(), before)

    def test_changed_sdk_binding_fails_without_partial_writes(self):
        descriptor = json.loads((self.root / rebind.DESCRIPTOR_PATH).read_text(encoding="utf-8"))
        for record in descriptor["projects"]:
            if record["source"] == "sdk-headers/test/sdk.h":
                record["sha256"] = "4" * 64
                break
        else:
            self.fail("SDK dependency record was not present")
        self._write_descriptor(descriptor)
        before = self._snapshot()
        with self.assertRaisesRegex(rebind.RebindError, "SDK dependency binding mismatch"):
            rebind.run("refresh", self.root)
        self.assertEqual(self._snapshot(), before)

    def test_source_change_after_preparation_aborts_before_writes(self):
        self.alpha.write_bytes(b"#define LEFT 100\n#define MIDDLE 1\n#define RIGHT 1\n")
        plan = rebind.prepare(self.root)
        before = self._snapshot()
        self.alpha.write_bytes(b"#define LEFT 101\n#define MIDDLE 1\n#define RIGHT 1\n")
        with self.assertRaisesRegex(rebind.RebindError, "changed after validation"):
            rebind.apply(plan)
        self.assertEqual(self._snapshot(), before)

    def test_unexpected_descriptor_structure_fails_without_partial_writes(self):
        descriptor = json.loads((self.root / rebind.DESCRIPTOR_PATH).read_text(encoding="utf-8"))
        descriptor["unexpected"] = []
        self._write_descriptor(descriptor)
        before = self._snapshot()
        with self.assertRaisesRegex(rebind.RebindError, "unexpected fields"):
            rebind.run("refresh", self.root)
        self.assertEqual(self._snapshot(), before)

    def test_inconsistent_binding_declarations_fail_without_partial_writes(self):
        path = self.root / rebind.PYTHON_BINDING_PATH
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        for index, value in enumerate(lines):
            if value.startswith("FULL_DEPENDENCY_PROJECT_SHA256"):
                digest = value.split('"')[1]
                replacement = ("0" if digest[0] != "0" else "1") + digest[1:]
                lines[index] = value.replace(digest, replacement)
                break
        else:
            self.fail("project binding declaration was not present")
        path.write_text("".join(lines), encoding="utf-8")
        before = self._snapshot()
        with self.assertRaisesRegex(rebind.RebindError, "inconsistent project_sha256"):
            rebind.run("refresh", self.root)
        self.assertEqual(self._snapshot(), before)

    def test_refresh_accepts_a_combined_descriptor_with_stale_aggregate_bindings(self):
        self.alpha.write_bytes(b"#define LEFT 80\n#define MIDDLE 1\n#define RIGHT 90\n")
        # Model a clean descriptor merge whose aggregate declarations still name
        # one parent. The refresher must reconstruct the combined-tree bindings.
        self._write_descriptor()
        before = self._snapshot()
        declared = (self.root / rebind.PYTHON_BINDING_PATH).read_text(encoding="utf-8").splitlines()[0].split('"')[1]
        self.assertNotEqual(sha256(before[rebind.DESCRIPTOR_PATH]), declared)
        report = rebind.run("refresh", self.root)
        self.assertEqual(report["status"], "refreshed")
        self.assertEqual(report["source_changes"], [])
        self.assertEqual(rebind.run("check", self.root)["status"], "current")

    def test_cli_check_reports_stale_with_distinct_exit_status(self):
        self.alpha.write_bytes(b"#define LEFT 110\n#define MIDDLE 1\n#define RIGHT 1\n")
        with mock.patch("sys.stdout") as stdout:
            self.assertEqual(rebind.main(["check", "--repo-root", str(self.root)]), 2)
        payload = json.loads("".join(call.args[0] for call in stdout.write.call_args_list))
        self.assertEqual(payload["mode"], "check")
        self.assertEqual(payload["status"], "stale")

    def test_preparation_is_deterministic(self):
        self.alpha.write_bytes(b"#define LEFT 50\n#define MIDDLE 1\n#define RIGHT 60\n")
        first = rebind.prepare(self.root)
        second = rebind.prepare(self.root)
        self.assertEqual(first.replacements, second.replacements)
        self.assertEqual(first.report, second.report)
        self.assertEqual(self._snapshot(), first.originals)

    def test_failed_multi_file_write_rolls_back(self):
        self.alpha.write_bytes(b"#define LEFT 70\n#define MIDDLE 1\n#define RIGHT 1\n")
        plan = rebind.prepare(self.root)
        before = self._snapshot()
        real_replace = os.replace
        calls = 0

        def fail_second(source, destination):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected replacement failure")
            return real_replace(source, destination)

        with mock.patch.object(rebind.os, "replace", side_effect=fail_second):
            with self.assertRaisesRegex(rebind.RebindError, "rolled back"):
                rebind.apply(plan)
        self.assertEqual(self._snapshot(), before)


if __name__ == "__main__":
    unittest.main()
