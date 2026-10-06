#!/usr/bin/env python3
"""Assemble one offline checks sample from retained desktop observations.

Depends on the prospective cohort reader introduced by #2427. No network,
build, binary execution, dispatch, or performance acceptance occurs here.

Input schema buster-ci-checks-sample-input-v1 uses the existing qualification
variant/cohort/run/conditions/desktops fields. Each desktop contains job,
coverage/result/phases digest references and phase_directory, but no tests:
runtime manifests are derived from actual coverage, journals and sidecars.
All references, including selected_tools inside conditions, remain relative
to the input directory. Missing conditions remain failures, never guesses.

Usage: python3 tools/ci_checks_sample.py INPUT.json --output sample-123.json
The output name must be a fresh JSON filename in the input directory. Its
object can be inserted in samples of a campaign in that SAME directory; keep
the returned cohort declaration unchanged. The original retained files are
read-only. Existing qualification.sample validates the complete assembled
sample before publication. Resource/reliability review remains pending.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

import ci_checks_qualification as qualification

SCHEMA = "buster-ci-checks-sample-input-v1"


def reference(root, path):
    qualification.require(path.is_file() and not path.is_symlink(), "missing regular retained file: " + str(path))
    return {"path": path.relative_to(root).as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def write_json(path, value):
    created = False
    try:
        with path.open("x", encoding="utf-8") as stream:
            created = True
            stream.write(json.dumps(value, indent=2) + "\n")
    except BaseException:
        if created:
            path.unlink()
        raise


def invocation_mode(event):
    # The native partition command falls back to ordinary tests below four
    # workers. Its argv alone cannot establish grouped execution.
    workers = qualification.units.integer(int(event["test_jobs"]), "native test quota", 1)
    argv = event["argv"]
    partitioned = len(argv) > 1 and argv[1] == "test_units_partitioned"
    return "groups" if partitioned and workers >= 4 else "serial"


def desktop(root, item, condition, output, number):
    qualification.require(set(item) == {"job", "coverage", "result", "phases", "phase_directory"},
                          "desktop input requires exact retained fields and no caller-supplied tests")
    coverage, result, summary = (qualification.record(root, item[key]) for key in ("coverage", "result", "phases"))
    environment = qualification.phase_environment(summary, result.get("metadata", {}))
    rows, selected = qualification.policy_rows(coverage, environment)
    detected = coverage.get("detected", [])
    qualification.require(isinstance(detected, list) and len(detected) == len(rows) and
                          Counter(row["id"] for row in detected) == Counter(rows.keys()), "missing or duplicate capability row")
    capabilities = {row["id"]: row for row in detected}
    phase_directory = root / item["phase_directory"]
    report = qualification.phases.analyze(phase_directory, coverage, environment)
    qualification.require(report == summary and result.get("matrix_phases") == summary,
                          "retained phase summary differs from replay/result")
    identity = coverage["identity"]
    tests = []
    for row_id in sorted(selected):
        row = rows[row_id]
        if row["execution"] == "runtime":
            tree = next(tree for tree in report["trees"] if row_id in tree["rows"])
            task_id = qualification.phases.task_id(tree["id"], "test", row["configuration"])
            events = [event for event in report["events"] if event["id"] == task_id]
            qualification.require(len(events) == 1, "missing or duplicate runtime event: " + task_id)
            event = events[0]
            sidecar = phase_directory.parent / "unit-observations" / task_id
            observation_path = sidecar / "observation.json"
            observation = qualification.record(root, reference(root, observation_path))
            inventory_path = qualification.retained(sidecar, {"path": "inventory.log", "sha256": observation.get("inventory_sha256")})
            log_path = qualification.retained(sidecar, {"path": "test.log", "sha256": observation.get("log_sha256")})
            mode = invocation_mode(event)
            inventory_rows, inventory_profile, inventory_primary = qualification.unit_campaign.inventory_proof(inventory_path)
            qualification.require(inventory_profile is not None, "Missing measured native host profile")
            manifest = {"schema": "buster-ci-unit-tests-measure-v1", "arm": "candidate" if mode == "groups" else "baseline",
                        "mode": mode, "exit_code": observation.get("test_result"),
                        "test_workers": int(event["test_jobs"]), "elapsed_us": event["end_us"] - event["child_start_us"],
                        "inventory": inventory_rows,
                        "log": Path(os.path.relpath(log_path, output)).as_posix(),
                        "identity": {"source_revision": identity["source_revision"], "binary_sha256": observation.get("binary_sha256"),
                            "runner_image": {key: condition[key] for key in ("image_os", "image_version", "runner")},
                            "platform": identity["platform"], "architecture": identity["architecture"],
                            "configuration": row["configuration"], "sanitize": row["sanitize"], "fuzz": row["fuzz"],
                            "table_audits": row["unity"] if report["scheduler"] == "pooled" else True,
                            "toolchain": {key: capabilities[row_id][key] for key in qualification.CAP_KEYS},
                            "cpu_budget": report["cpu_budget"],
                            "native_host_profile": inventory_profile}}
            if mode == "groups":
                manifest["primary_module"] = inventory_primary
            path = output / (str(number) + "-" + str(len(tests)) + ".json")
            write_json(path, manifest)
            tests.append({"row_id": row_id, "manifest": reference(root, path),
                          "observation": reference(root, observation_path), "log_sha256": observation["log_sha256"]})
    return dict(item, tests=tests)


def assemble(path, output_name):
    path = Path(path)
    qualification.require(path.is_file() and not path.is_symlink(), "input must be a regular retained manifest")
    root = path.resolve().parent
    specification = qualification.phases.read(path)
    qualification.require(isinstance(specification, dict) and specification.get("schema") == SCHEMA, "unknown assembler input schema")
    fields = {"schema", "cohort", "variant", "run", "conditions", "desktops"}
    qualification.require(set(specification) == fields, "input requires a strict declared cohort and exact sample fields")
    declaration = qualification.cohort(specification)
    run = qualification.record(root, specification["run"])
    qualification.require(all(run.get(key) == declaration[key] for key in ("head_sha", "workflow_blob_sha")),
                          "run differs from declared cohort source/workflow pins")
    qualification.timing(run, specification["variant"], declaration["name"])
    conditions, _ = qualification.conditions(root, specification["conditions"], run)
    inputs = specification["desktops"]
    names = qualification.cohort_desktop_jobs(specification["variant"])
    qualification.require(isinstance(inputs, list) and Counter(item["job"] for item in inputs) == Counter(names),
                          "missing or duplicate exact desktop artifact")
    qualification.require(isinstance(output_name, str) and re.fullmatch(r"[a-z0-9][a-z0-9_-]*\.json", output_name),
                          "output must be a JSON filename in the input directory")
    sample_path = root / output_name
    output = root / (sample_path.stem + "-tests")
    qualification.require(not sample_path.exists() and not sample_path.is_symlink() and
                          not output.exists() and not output.is_symlink(), "output already exists")
    output.mkdir()
    succeeded = False
    try:
        item = {key: specification[key] for key in ("variant", "run", "conditions")}
        item["desktops"] = [desktop(root, retained, conditions[retained["job"]], output, number)
                            for number, retained in enumerate(inputs)]
        observed = qualification.sample(root, item, declaration["name"])
        write_json(sample_path, item)
        succeeded = True
        result = {"schema": "buster-ci-checks-assembled-sample-v1", "sample": reference(root, sample_path),
                  "cohort": declaration, "run_id": observed["run_id"], "variant": observed["variant"],
                  "sample_validated": True, "performance_accepted": False, "resource_review": "pending"}
    finally:
        if not succeeded:
            shutil.rmtree(output)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    status = 0
    try:
        print(json.dumps(assemble(args.input, args.output), indent=2))
    except (OSError, ValueError, KeyError, TypeError, AttributeError, StopIteration) as error:
        print("Checks sample assembly failed: " + str(error), file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
