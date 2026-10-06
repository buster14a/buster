#!/usr/bin/env python3
"""Check the blocked native-retirement performance declaration's pins.

`docs/native-retirement-performance-v1.blocked` records that the decision
`native-retirement-performance-v1` is not executable and pins the exact
contract, support declaration, validator, schema and statistics it would use.
The benchmark service's suite checked these pins until the service was
removed (#2708); this keeps them from going stale.
"""

from __future__ import annotations

import hashlib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECLARATION = ROOT / "docs" / "native-retirement-performance-v1.blocked"
PINNED = ("contract", "support-declaration", "binding-validator", "binding-schema", "statistics")
FIXED = {
    "schema": "1",
    "recipe": "native-retirement-performance-v1",
    "repository": "buster14a/buster",
    "status": "blocked",
    "requires": "qualified-9700x-service,predeclared-execution-plan,bound-subjects,durable-replay",
}


class DeclarationTest(unittest.TestCase):
    def setUp(self) -> None:
        data = DECLARATION.read_bytes()
        self.assertNotIn(b"\r", data)
        self.assertTrue(data.endswith(b"\n"))
        lines = data.decode("ascii").splitlines()
        self.fields = dict(line.split("=", 1) for line in lines)
        self.assertEqual(len(self.fields), len(lines), "duplicate key")

    def test_keys_are_exactly_the_reviewed_set(self) -> None:
        expected = set(FIXED) | set(PINNED) | {name + "-sha256" for name in PINNED}
        self.assertEqual(set(self.fields), expected)
        for key, value in FIXED.items():
            self.assertEqual(self.fields[key], value)

    def test_every_pinned_file_matches_its_digest(self) -> None:
        for name in PINNED:
            with self.subTest(pin=name):
                path = ROOT / self.fields[name]
                self.assertTrue(path.is_file(), self.fields[name])
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), self.fields[name + "-sha256"])


if __name__ == "__main__":
    unittest.main()
