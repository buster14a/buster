#!/usr/bin/env python3
"""Retain an opt-in selected simulator witness without another simctl query.

The launcher preserves its original first available name match and UUID-only
stdout, passing the runtime key/deviceTypeIdentifier from that same record.
The CLI retains already-observed create arguments/UUID, or an honest unknown
for an explicit UUID whose runtime/device type were never discovered. Evidence
failure never supplies defaults or changes the existing selection result.
"""
import argparse
import os
from pathlib import Path
import re
import sys

import ci_job_environment as environment_tools

SCHEMA = "buster-ci-ios-simulator-selection-v1"
UUID = re.compile(r"[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}\Z")
RUNTIME = re.compile(r"com\.apple\.CoreSimulator\.SimRuntime\.[A-Za-z0-9-]+\Z")
DEVICE_TYPE = re.compile(r"com\.apple\.CoreSimulator\.SimDeviceType\.[A-Za-z0-9-]+\Z")


def receipt(kind, udid, runtime, device_type, environment):
    binding, invalid = environment_tools.actions_binding(environment)
    selection = {"kind": kind, "udid": udid, "runtime": runtime, "device_type": device_type}
    for key, value in selection.items():
        environment_tools.bounded_value(value, key)
    observed = kind in ("created", "name-reuse") and not invalid and all(
        isinstance(selection[key], str) and pattern.fullmatch(selection[key])
        for key, pattern in (("udid", UUID), ("runtime", RUNTIME), ("device_type", DEVICE_TYPE)))
    return {"schema": SCHEMA, "status": "observed" if observed else "unknown", **binding,
            "selection": selection, "invalid_bindings": invalid,
            "reason": "none" if observed else "selected runtime/device identity was not observed"}


def retain(kind, udid, runtime, device_type, environment):
    if environment.get("BUSTER_CI_CONDITIONS_EVIDENCE") == "1":
        try:
            value = receipt(kind, udid, runtime, device_type, environment)
            output = str(Path(environment.get("RUNNER_TEMP", "")) / "buster-ci" / "ios-simulator-selection.json")
            environment_tools.write_receipt(output, environment_tools.serialize(value))
            print("CI_IOS_SIMULATOR_SELECTION status=" + value["status"], file=sys.stderr)
        except (ValueError, OSError) as error:
            reason = str(error) if isinstance(error, environment_tools.EvidenceError) else "invalid or unavailable selection evidence"
            print("CI_IOS_SIMULATOR_SELECTION retention failed: " + reason, file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("created", "explicit"), required=True)
    parser.add_argument("--udid", required=True)
    parser.add_argument("--runtime", default="")
    parser.add_argument("--device-type", default="")
    args = parser.parse_args(argv)
    retain(args.kind, args.udid, args.runtime, args.device_type, os.environ)
    return 0


if __name__ == "__main__":
    sys.exit(main())
