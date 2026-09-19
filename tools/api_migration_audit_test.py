#!/usr/bin/env python3
"""Offline tests for the staged API-migration compatibility audit."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/api_migration_audit.py"
SPEC = importlib.util.spec_from_file_location("api_migration_audit", SCRIPT)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class ApiMigrationAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / ".github").mkdir()
        (self.root / "src/lib").mkdir(parents=True)
        (self.root / "src/old").mkdir(parents=True)
        (self.root / "src/new").mkdir(parents=True)
        (self.root / "src/lib/api.h").write_text("void legacy_open(void);\n", encoding="utf-8")
        (self.root / "src/lib/api.c").write_text("void legacy_open(void) {}\n", encoding="utf-8")
        (self.root / "src/old/caller.c").write_text("void f(void) { legacy_open(); }\n", encoding="utf-8")
        (self.root / "src/new/caller.c").write_text("void g(void) { explicit_open(); }\n", encoding="utf-8")
        self.manifest = {
            "schema": AUDIT.SCHEMA,
            "migrations": [{
                "id": "issue-123-explicit-open",
                "compatibility_symbol": "legacy_open",
                "owner_issue": 123,
                "removal_issue": 124,
                "removal_condition": "Remove after every caller uses explicit_open.",
                "scan_globs": ["src/**/*.c", "src/**/*.h"],
                "compatibility_owners": {"src/lib/api.c": 1, "src/lib/api.h": 1},
                "allowed_callers": {"src/old/caller.c": 1},
            }],
        }
        self.write_manifest()

    def write_manifest(self):
        (self.root / AUDIT.DEFAULT_MANIFEST).write_text(json.dumps(self.manifest, indent=2) + "\n", encoding="utf-8")

    def errors(self):
        return AUDIT.audit_repository(self.root)

    def test_partially_migrated_tree_is_valid(self):
        self.assertEqual(self.errors(), [])

    def test_new_old_style_caller_is_rejected(self):
        (self.root / "src/new/caller.c").write_text("void g(void) { legacy_open(); }\n", encoding="utf-8")
        self.assertTrue(any("unregistered use" in error for error in self.errors()))

    def test_registered_counts_must_move_with_callers(self):
        (self.root / "src/old/caller.c").write_text("void f(void) { explicit_open(); }\n", encoding="utf-8")
        self.assertTrue(any("registry requires 1" in error for error in self.errors()))

    def test_zero_caller_shim_must_be_removed(self):
        self.manifest["migrations"][0]["allowed_callers"] = {}
        self.write_manifest()
        self.assertTrue(any("no old callers remain" in error for error in self.errors()))

    def test_removal_has_distinct_tracked_owner(self):
        self.manifest["migrations"][0]["removal_issue"] = 123
        self.write_manifest()
        self.assertTrue(any("distinct from owner_issue" in error for error in self.errors()))

    def test_unmatched_registered_path_is_rejected(self):
        self.manifest["migrations"][0]["allowed_callers"] = {"tools/caller.c": 1}
        self.write_manifest()
        self.assertTrue(any("not matched by scan_globs" in error for error in self.errors()))

    def test_duplicate_json_keys_are_rejected(self):
        path = self.root / AUDIT.DEFAULT_MANIFEST
        path.write_text('{"schema":"BUSTER_API_MIGRATIONS_V1","schema":"other","migrations":[]}\n', encoding="utf-8")
        self.assertTrue(any("duplicate JSON key" in error for error in self.errors()))

    def test_cli(self):
        valid = subprocess.run(
            [sys.executable, str(SCRIPT), "--root", str(self.root)], capture_output=True, text=True, check=False
        )
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertIn("1 active migration", valid.stdout)
        (self.root / "src/new/caller.c").write_text("legacy_open();\n", encoding="utf-8")
        invalid = subprocess.run(
            [sys.executable, str(SCRIPT), "--root", str(self.root)], capture_output=True, text=True, check=False
        )
        self.assertEqual(invalid.returncode, 1)
        self.assertIn("unregistered use", invalid.stderr)


class RepositoryWiringTest(unittest.TestCase):
    def test_repository_registry(self):
        self.assertEqual(AUDIT.audit_repository(ROOT), [])

    def test_policy_workflow_runs_audit_and_tests(self):
        workflow = (ROOT / ".github/workflows/api-migration-policy.yml").read_text(encoding="utf-8")
        self.assertIn("python3 -B tools/api_migration_audit.py", workflow)
        self.assertIn("python3 -B tools/api_migration_audit_test.py -v", workflow)


if __name__ == "__main__":
    unittest.main()
