#!/usr/bin/env python3
"""CI summary entry point with fail-closed metamorphic evidence retention."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys

import ci_summary_core as _core
import ci_metamorphic_evidence as _metamorphic


_core_write_report = _core.write_report


def write_report(environment, *, expected_coverage_mode="ci"):
    effective = dict(environment)
    try:
        _metamorphic.collect_from_environment(effective)
    except (_metamorphic.EvidenceError, OSError, UnicodeError, ValueError) as error:
        runner_temp = Path(effective["RUNNER_TEMP"])
        try:
            _metamorphic.record_error(runner_temp / "buster-ci" / "metamorphic", effective, error)
        except OSError as record_error:
            print(f"Metamorphic evidence error record failed: {record_error}", file=sys.stderr)
        print(f"Metamorphic evidence retention failed: {error}", file=sys.stderr)
        steps = json.loads(effective.get("BUSTER_CI_STEPS", "{}"))
        if not isinstance(steps, dict):
            steps = {}
        steps["metamorphic_evidence"] = {"outcome": "failure", "conclusion": "failure"}
        effective["BUSTER_CI_STEPS"] = json.dumps(steps)
        required = effective.get("BUSTER_CI_REQUIRED", "").split()
        if "metamorphic_evidence" not in required:
            required.append("metamorphic_evidence")
        effective["BUSTER_CI_REQUIRED"] = " ".join(required)
    return _core_write_report(effective, expected_coverage_mode=expected_coverage_mode)


def main():
    status = 1
    try:
        status = write_report(os.environ)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"CI summary failed: {error}", file=sys.stderr)
    return status


_core.write_report = write_report
_core.main = main
sys.modules[__name__] = _core

if __name__ == "__main__":
    sys.exit(main())
