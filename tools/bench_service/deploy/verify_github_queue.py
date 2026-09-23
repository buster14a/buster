#!/usr/bin/env python3
"""Require the live main ruleset to retain the reviewed merge-queue policy."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def compare(expected: object, actual: object, path: str) -> None:
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            raise ValueError(f"{path}: expected object")
        for key, value in expected.items():
            if key not in actual:
                raise ValueError(f"{path}.{key}: missing")
            compare(value, actual[key], f"{path}.{key}")
    elif isinstance(expected, list):
        if not isinstance(actual, list) or len(expected) != len(actual):
            raise ValueError(f"{path}: list differs")
        for index, value in enumerate(expected):
            compare(value, actual[index], f"{path}[{index}]")
    elif type(expected) is not type(actual) or expected != actual:
        raise ValueError(f"{path}: value differs")


def main() -> int:
    expected = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    actual = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
    try:
        if actual.get("id") != int(sys.argv[4]):
            raise ValueError("main ruleset: ID differs from repository lookup")
        if actual.get("source_type") != "Repository" or actual.get("source") != sys.argv[3]:
            raise ValueError("main ruleset: repository source differs")
        if actual.get("current_user_can_bypass") != "never":
            raise ValueError("main ruleset: caller can bypass")
        compare(expected, actual, "main ruleset")
    except ValueError as error:
        print(f"QUEUE_POLICY_FAIL {error}", file=sys.stderr)
        return 1
    print("QUEUE_POLICY_PASS live main ruleset matches reviewed queue policy")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
