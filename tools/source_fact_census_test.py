#!/usr/bin/env python3
"""Offline checks for tools/source_fact_census.py; no compiler is run."""

import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import source_fact_census  # noqa: E402


class SourceFactCensusTest(unittest.TestCase):
    def test_corpora_are_deterministic_and_scale(self):
        for family, build in source_fact_census.FAMILIES.items():
            first = build(1)
            self.assertEqual(first, build(1), family)
            self.assertGreater(len(build(2)), len(first), family)
            self.assertTrue(first.endswith("\n"), family)
            self.assertNotIn("#include", first, family)

    def test_manifest_hashes_match_files(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = source_fact_census.generate(Path(directory), [1])
            self.assertEqual(len(manifest), len(source_fact_census.FAMILIES))
            for entry in manifest:
                data = Path(entry["path"]).read_bytes()
                self.assertEqual(entry["bytes"], len(data))
                self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())

    def test_parse_census_keeps_only_census_fields(self):
        text = "version=1\nunique.bytes=10\nc_census.version=1\nc_census.lex_calls=3\nc_census.lower.string_decodes=7\n"
        self.assertEqual(source_fact_census.parse_census(text), {"lex_calls": 3, "lower.string_decodes": 7})


if __name__ == "__main__":
    unittest.main()
