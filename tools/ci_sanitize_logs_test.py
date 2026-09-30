#!/usr/bin/env python3
"""Regressions for the CI log sanitizer and its workflow wiring."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_sanitize_logs


class SanitizeLogsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "buster-ci"
        (self.root / "nested").mkdir(parents=True)
        (self.root / "combinations.log").write_bytes(b"combinations\n")
        (self.root / "nested/binary.log").write_bytes(b"\x00\xff")
        self.outside = Path(self.temporary.name) / "cache-entry"
        self.outside.write_text("cache\n")

    def files(self):
        return {str(path.relative_to(self.root)).replace(os.sep, "/"): path.read_bytes()
                for path in self.root.rglob("*") if path.is_file()}

    @unittest.skipIf(os.name == "nt", "Symbolic link fixtures require POSIX permissions")
    def test_links_are_reported_and_regular_files_are_copied(self):
        (self.root / "cache-link").symlink_to(self.outside)
        (self.root / "directory-link").symlink_to(self.root / "nested", target_is_directory=True)
        skipped = ci_sanitize_logs.sanitize(self.root)
        self.assertEqual(sorted(entry.split(":", 1)[0] for entry in skipped), ["cache-link", "directory-link"])
        files = self.files()
        self.assertEqual(files["combinations.log"], b"combinations\n")
        self.assertEqual(files["nested/binary.log"], b"\x00\xff")
        report = files.pop(ci_sanitize_logs.REPORT).decode()
        self.assertEqual(len(report.splitlines()), 2)
        self.assertEqual(set(files), {"combinations.log", "nested/binary.log"})
        self.assertFalse(any(path.is_symlink() for path in self.root.rglob("*")))
        self.assertFalse(self.root.with_name("buster-ci-unsanitized").exists())
        self.assertEqual(self.outside.read_text(), "cache\n")

    @unittest.skipIf(os.name == "nt" or os.geteuid() == 0, "Unreadable fixtures require an unprivileged POSIX user")
    def test_unreadable_file_is_skipped_without_failing(self):
        secret = self.root / "unreadable.log"
        secret.write_text("secret\n")
        secret.chmod(0)
        skipped = ci_sanitize_logs.sanitize(self.root)
        self.assertEqual([entry.split(":", 1)[0] for entry in skipped], ["unreadable.log"])
        self.assertNotIn("unreadable.log", self.files())

    def test_missing_or_file_root_produces_an_empty_report(self):
        shutil.rmtree(self.root)
        self.assertEqual(ci_sanitize_logs.sanitize(self.root), [])
        self.assertEqual(self.files(), {ci_sanitize_logs.REPORT: b""})
        shutil.rmtree(self.root)
        self.root.write_text("not a directory\n")
        self.assertEqual(ci_sanitize_logs.main(["--root", str(self.root)]), 0)
        self.assertEqual(self.files(), {ci_sanitize_logs.REPORT: b""})

    def test_stale_unsanitized_copy_is_replaced(self):
        stale = self.root.with_name("buster-ci-unsanitized")
        stale.mkdir()
        (stale / "stale.log").write_text("stale\n")
        ci_sanitize_logs.sanitize(self.root)
        self.assertNotIn("stale.log", self.files())
        self.assertFalse(stale.exists())

    def test_workflow_sanitizes_desktop_logs_with_the_helper(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        step = desktop.split("      - name: Sanitize desktop logs\n", 1)[1].split("      - name:", 1)[0]
        self.assertIn("if: ${{ always() && steps.checkout.outcome == 'success' }}", step)
        self.assertIn('tools/ci_sanitize_logs.py --root "$RUNNER_TEMP/buster-ci"', step)
        self.assertNotIn("<<'PYTHON'", desktop)
        self.assertLess(desktop.index("Sanitize desktop logs"), desktop.index("Retain desktop logs"))


if __name__ == "__main__":
    unittest.main()
