#!/usr/bin/env python3
"""Isolated regression tests for configure-evidence retention (no toolchain)."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import ci_configure_evidence as evidence


class ConfigureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.output = self.root / "artifacts" / "configure"
        self.tree = "build-ci_on-cc_zig-sanitize_off-fuzz_available_off-configs_Debug"

    def write(self, relative, data=b"diagnostic\n"):
        path = self.build / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def collect(self, **kwargs):
        return evidence.collect(self.build, self.output, environment={"GITHUB_RUN_ID": "17", "SECRET": "not-copied"}, **kwargs)

    def link(self, target, destination):
        try:
            destination.symlink_to(target, target_is_directory=target.is_dir())
        except OSError as error:
            self.skipTest(f"host does not permit symlinks: {error}")

    def test_raw_bytes_hashes_and_allowlisted_identity(self):
        data = b"\xff\xfeC\x00a\x00c\x00h\x00e\x00\r\x00\n\x00"
        self.write(f"{self.tree}/CMakeCache.txt", data)
        self.write(f"{self.tree}/CMakeFiles/CMakeConfigureLog.yaml")
        self.write(f"{self.tree}/cmake-profile.json", b"[]")
        result = self.collect(profile_requested=True)
        captured = [item for item in result["files"] if item["status"] == "captured"]
        cache = next(item for item in captured if item["path"].endswith("CMakeCache.txt"))
        self.assertEqual(cache["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual((self.output / cache["path"]).read_bytes(), data)
        self.assertEqual(result["identity"], {"GITHUB_RUN_ID": "17"})
        self.assertEqual(result["configure_logs_captured"], 1)
        self.assertEqual(result["profiles_captured"], 1)
        self.assertTrue(result["profile_requested"])
        self.assertEqual(json.loads((self.output / "manifest.json").read_text()), result)

    def test_does_not_capture_objects_unrelated_trees_or_environment(self):
        self.write(f"{self.tree}/subject.o")
        self.write("unrelated/CMakeCache.txt")
        result = self.collect()
        self.assertEqual(result["captured_bytes"], 0)
        self.assertEqual(result["tree_count"], 1)
        self.assertFalse(result["profile_requested"])
        self.assertFalse(any(path.name == "subject.o" for path in self.output.rglob("*")))

    def test_failed_configure_missing_files_remain_missing(self):
        self.write(f"{self.tree}/CMakeFiles/CMakeConfigureLog.yaml", b"failed probe")
        result = self.collect(profile_requested=True)
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["profiles_captured"], 0)
        self.assertEqual(next(item for item in result["files"] if item["path"].endswith("cmake-profile.json"))["status"], "missing")
        self.assertNotIn("passed", json.dumps(result))

    def test_missing_build_root_is_not_empty_success(self):
        self.build.rmdir()
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["tree_count"], 0)

    def test_empty_build_root_is_not_empty_success(self):
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["tree_count"], 0)

    def test_linked_build_root_refused(self):
        actual = self.root / "actual-build"
        actual.mkdir()
        self.build.rmdir()
        self.link(actual, self.build)
        with self.assertRaisesRegex(ValueError, "linked"):
            self.collect()

    def test_windows_root_reparse_point_refused(self):
        with mock.patch.object(Path, "lstat", return_value=mock.Mock(st_mode=0o40755, st_file_attributes=0x400)):
            with self.assertRaisesRegex(ValueError, "linked"):
                self.collect()

    def test_rejects_existing_output(self):
        self.output.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.collect()

    def test_rejects_output_within_build_tree(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            evidence.collect(self.build, self.build / "evidence")

    def test_file_limit_is_bounded_and_reported(self):
        self.write(f"{self.tree}/CMakeCache.txt", b"12345")
        result = self.collect(file_limit=4)
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_total_limit_and_exact_boundary(self):
        self.write(f"{self.tree}/CMakeCache.txt", b"1234")
        self.write(f"{self.tree}/CMakeFiles/CMakeConfigureLog.yaml", b"1234")
        result = self.collect(file_limit=4, total_limit=4)
        self.assertEqual(result["captured_bytes"], 4)
        self.assertTrue(result["errors"])

    def test_file_symlink_refused(self):
        private = self.root / "private"
        private.write_text("must not be copied")
        path = self.write(f"{self.tree}/CMakeCache.txt")
        path.unlink()
        self.link(private, path)
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_directory_symlink_refused(self):
        private = self.root / "private"
        private.mkdir()
        (private / "CMakeConfigureLog.yaml").write_text("must not be copied")
        tree = self.build / self.tree
        tree.mkdir()
        self.link(private, tree / "CMakeFiles")
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_tree_symlink_refused(self):
        private = self.root / "private"
        private.mkdir()
        (private / "CMakeCache.txt").write_text("must not be copied")
        self.link(private, self.build / self.tree)
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_windows_reparse_point_refused(self):
        with mock.patch.object(Path, "lstat", return_value=mock.Mock(st_mode=0o100644, st_file_attributes=0x400)):
            with self.assertRaisesRegex(ValueError, "reparse"):
                evidence.regular_path(self.build, "CMakeCache.txt")

    def test_nonregular_file_and_traversal_refused(self):
        (self.build / "directory").mkdir()
        for relative in ("directory", "../private", str(self.root / "private")):
            with self.assertRaises(ValueError):
                evidence.regular_path(self.build, relative)

    def test_root_entry_bound(self):
        self.write("a")
        self.write("b")
        with mock.patch.object(evidence, "MAX_ROOT_ENTRIES", 1):
            result = self.collect()
        self.assertTrue(result["errors"])

    def test_tree_bound(self):
        self.write(f"{self.tree}/CMakeCache.txt")
        self.write("build-ci_on-cc_clang-other/CMakeCache.txt")
        with mock.patch.object(evidence, "MAX_TREES", 1):
            result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_cli_propagates_collection_failure(self):
        self.build.rmdir()
        run = subprocess.run([sys.executable, evidence.__file__, "--build-root", str(self.build), "--output", str(self.output)],
                             capture_output=True, text=True, check=False)
        self.assertEqual(run.returncode, 1)
        self.assertTrue((self.output / "manifest.json").is_file())


if __name__ == "__main__":
    unittest.main()
