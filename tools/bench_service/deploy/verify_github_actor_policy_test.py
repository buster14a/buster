#!/usr/bin/env python3
"""Reject additions to the fixed benchmark requester list and trigger scope."""

import copy
import json
import unittest
from pathlib import Path

from verify_github_actor_policy import verify


EXPECTED = json.loads((Path(__file__).resolve().parents[3] /
                       ".github/benchmark-actions-policy.json").read_text())


class ActorPolicyTest(unittest.TestCase):
    def setUp(self):
        self.actual = copy.deepcopy(EXPECTED)
        self.actual["source"] = "buster14a/buster"

    def check(self):
        verify(EXPECTED, self.actual, "buster14a/buster")

    def test_exact_five_actors_and_manual_event(self):
        self.check()
        self.actual["rules"][0]["parameters"]["allowed_actors"].reverse()
        self.check()

    def test_additional_duplicate_or_retyped_actor_is_rejected(self):
        actors = self.actual["rules"][0]["parameters"]["allowed_actors"]
        for changed in (actors + [{"id": 9, "type": "User"}],
                        actors + [actors[0]],
                        actors[:-1],
                        [{**actors[0], "type": "User"}] + actors[1:]):
            with self.subTest(changed=changed):
                self.actual["rules"][0]["parameters"]["allowed_actors"] = changed
                with self.assertRaisesRegex(ValueError, "actors differ"):
                    self.check()

    def test_scope_event_enforcement_and_identity_drift(self):
        for path, value in (("conditions", {"workflow_path": {"include": ["*"], "exclude": []}}),
                            ("enforcement", "disabled"), ("id", 9),
                            ("source", "other/repo")):
            with self.subTest(path=path):
                original = self.actual[path]
                self.actual[path] = value
                with self.assertRaises(ValueError):
                    self.check()
                self.actual[path] = original
        self.actual["rules"][1]["parameters"]["allowed_events"] = ["workflow_dispatch", "push"]
        with self.assertRaisesRegex(ValueError, "events differ"):
            self.check()


if __name__ == "__main__":
    unittest.main()
