#!/usr/bin/env python3
"""Administrator read-back of the fixed main ruleset and bypass list."""

import copy
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
EXPECTED = ROOT / ".github/main-merge-queue.ruleset.json"
VERIFIER = Path(__file__).with_name("verify_github_queue.py")


class MainRulesetReadbackTest(unittest.TestCase):
    def setUp(self):
        self.actual = json.loads(EXPECTED.read_text(encoding="utf-8"))
        self.actual.update(id=22537199, source_type="Repository",
                           source="buster14a/buster", current_user_can_bypass="always")

    def check(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Path(directory) / "live.json"
            live.write_text(json.dumps(self.actual), encoding="utf-8")
            result = subprocess.run(["python3", "-B", str(VERIFIER), str(EXPECTED),
                                     str(live), "buster14a/buster", "22537199"],
                                    capture_output=True, text=True, check=False)
        return result

    def test_administrator_and_readonly_responses(self):
        self.assertEqual(self.check().returncode, 0)
        self.actual["current_user_can_bypass"] = "never"
        self.assertEqual(self.check().returncode, 0)

    def test_added_bypass_or_changed_merge_queue_is_rejected(self):
        original = copy.deepcopy(self.actual)
        self.actual["bypass_actors"].append({"actor_id": 1, "actor_type": "User",
                                              "bypass_mode": "always"})
        self.assertNotEqual(self.check().returncode, 0)
        self.actual = copy.deepcopy(original)
        self.actual["rules"][-1]["parameters"]["max_entries_to_merge"] = 2
        self.assertNotEqual(self.check().returncode, 0)
        self.actual = copy.deepcopy(original)
        self.actual["source"] = "other/repo"
        self.assertNotEqual(self.check().returncode, 0)


if __name__ == "__main__":
    unittest.main()
