#!/usr/bin/env python3
"""Compare the existing Actions policy with the reviewed dispatch allowlist."""

import json
import sys
from pathlib import Path


def verify(expected, actual, repository):
    for key in ("id", "name", "target", "source_type", "enforcement", "conditions"):
        if actual.get(key) != expected[key]:
            raise ValueError(f"Actions policy {key} differs")
    if actual.get("source") != repository:
        raise ValueError("Actions policy repository source differs")

    rules = actual.get("rules")
    if not isinstance(rules, list) or len(rules) != 2:
        raise ValueError("Actions policy rules differ")
    by_type = {rule.get("type"): rule.get("parameters") for rule in rules
               if isinstance(rule, dict)}
    if set(by_type) != {"restrict_actions_actors", "restrict_action_events"}:
        raise ValueError("Actions policy rules differ")
    if by_type["restrict_action_events"] != {"allowed_events": ["workflow_dispatch"]}:
        raise ValueError("Actions policy events differ")

    actors = by_type["restrict_actions_actors"]
    expected_actors = expected["rules"][0]["parameters"]["allowed_actors"]
    if not isinstance(actors, dict) or set(actors) != {"allowed_actors"}:
        raise ValueError("Actions policy actors differ")
    actors = actors["allowed_actors"]
    if not isinstance(actors, list) or len(actors) != len(expected_actors) or any(
        not isinstance(actor, dict) or set(actor) != {"id", "type"} or
        type(actor["id"]) is not int or not isinstance(actor["type"], str)
        for actor in actors
    ):
        raise ValueError("Actions policy actors differ")
    if {(actor["id"], actor["type"]) for actor in actors} != {
        (actor["id"], actor["type"]) for actor in expected_actors
    }:
        raise ValueError("Actions policy actors differ")


def main():
    try:
        expected = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
        actual = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
        verify(expected, actual, sys.argv[3])
    except (ValueError, OSError, TypeError, KeyError, IndexError) as error:
        print(f"BENCH_ACTOR_POLICY_FAIL {error}", file=sys.stderr)
        return 1
    print("BENCH_ACTOR_POLICY_PASS fixed workflow, event and requesters match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
