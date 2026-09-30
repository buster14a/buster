#!/usr/bin/env python3
"""Assemble an offline unit-test campaign from retained native observations.

This tool never launches a build or test. The native driver owns invocation,
deadlines, exit status and timing. This postprocessor checks its observations,
queries the independent inventory log, hashes the observed binary and delegates
module/count comparisons to ci_unit_tests_measure.py. A valid campaign is
diagnostic evidence; it never certifies CI completion or accepts a speedup.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

import ci_unit_tests_measure as measure


def inventory(path):
    lines = measure.read_log(path)
    declared = measure.native_records(lines, "CI_UNIT_MODULE_V1")
    rows = []
    fields = {"index", "module", "table_audit", "enabled", "selected", "group"}
    for position, row in enumerate(declared):
        measure.require(set(row) == fields, "Inventory query has missing or unexpected fields")
        index = measure.number(row, "index")
        audit = measure.number(row, "table_audit")
        measure.require(index == position, "Inventory query is not in canonical index order")
        measure.require(audit in (0, 1), "Inventory query has an invalid table-audit flag")
        measure.require(measure.number(row, "enabled") == 1 - audit and
                        measure.number(row, "selected") == 0, "Inventory query executed modules or changed audit policy")
        owner = "driver" if row["module"] == "compiler_driver_tests" else "rest"
        measure.require(row["group"] == owner, "Inventory query has an invalid module owner")
        rows.append({"index": index, "name": row["module"], "table_audit": bool(audit)})
    validated = measure.inventory_rows({"inventory": rows})
    measure.require("compiler_driver_tests" in validated and
                    not validated["compiler_driver_tests"]["table_audit"], "Inventory query lacks an enabled driver module")
    measure.require(any(not row["table_audit"] and row["name"] != "compiler_driver_tests" for row in rows),
                    "Inventory query lacks enabled rest modules")
    batches = measure.native_records(lines, "CI_UNIT_BATCH_V1")
    measure.require(len(batches) == 1, "Inventory query lacks one terminal batch record")
    batch = batches[0]
    counts = ("modules", "modules_passed", "assertions", "passed", "failed", "external", "external_passed")
    measure.require(set(batch) == {"group", "status", *counts} and
                    batch["group"] == "inventory" and batch["status"] == "inventory", "Inventory query did not complete cleanly")
    for key in counts:
        measure.require(measure.number(batch, key) == 0, "Inventory query ran tests or reported failure")
    measure.require(not any(line.startswith("TEST_MODULE_TIMING") for line in lines), "Inventory query executed timed modules")
    measure.require(not any(line.startswith("CI_UNIT_") and
                            not line.startswith(("CI_UNIT_MODULE_V1 ", "CI_UNIT_BATCH_V1 ")) for line in lines),
                    "Inventory query contains unexpected native proof records")
    terminals = measure.parse_terminals(lines, {}, len(rows), True)
    measure.require(terminals["External"]["total"] == 0, "Inventory query executed external tests")
    return rows


def load_record(path):
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    measure.require(isinstance(value, dict), f"Invalid native phase object: {path.name}")
    return value


def native_phase(directory, identifier, test_jobs):
    ends = list(directory.glob(identifier + ".*.end.json"))
    starts = list(directory.glob(identifier + ".*.start.json"))
    measure.require(len(ends) == len(starts) == 1, f"{identifier}: missing or duplicate native phase records")
    start, end = load_record(starts[0]), load_record(ends[0])
    common = ("id", "epoch_us", "pid", "start_us", "argv")
    measure.require(all(key in start and key in end and start[key] == end[key] for key in common),
                    f"{identifier}: native phase start/end identity differs")
    measure.require(end["id"] == identifier and type(end["epoch_us"]) is int and end["epoch_us"] == 1,
                    f"{identifier}: unexpected phase identity/epoch")
    pid = measure.integer(end["pid"], "phase.pid", 1)
    measure.require(starts[0].name == f"{identifier}.{pid}.start.json" and
                    ends[0].name == f"{identifier}.{pid}.end.json", f"{identifier}: phase filename/pid differs")
    measure.require(isinstance(end["argv"], list) and end["argv"] and
                    all(isinstance(argument, str) and argument for argument in end["argv"]),
                    f"{identifier}: missing native invocation")
    measure.require(start.get("state") == "running" and end.get("state") == "success",
                    f"{identifier}: native phase failed")
    for key in ("result", "platform_status", "timed_out", "termination_requested", "forcibly_terminated"):
        measure.require(measure.integer(end.get(key), "phase." + key) == 0,
                        f"{identifier}: unsuccessful subprocess or cleanup ({key})")
    measure.require(type(end.get("spawned")) is int and end["spawned"] == 1,
                    f"{identifier}: native subprocess was not spawned")
    measure.require(end.get("test_jobs") == str(test_jobs), f"{identifier}: subprocess test quota differs")
    launched = measure.integer(end["start_us"], "phase.start_us", 1)
    child = measure.integer(end.get("child_start_us"), "phase.child_start_us", 1)
    finished = measure.integer(end.get("end_us"), "phase.end_us", 1)
    published = measure.integer(end.get("publication_start_us"), "phase.publication_start_us", 1)
    measure.require(launched <= child < finished <= published, f"{identifier}: invalid native timing interval")
    return {"id": identifier, "elapsed_us": finished - child, "start": start, "end": end}


def provenance(directory, binary, platform, environment):
    revision = environment.get("BUSTER_TEST_SOURCE_REVISION", "")
    measure.require(re.fullmatch(r"[0-9a-f]{40}", revision) is not None, "Missing exact source revision")
    measure.require(environment.get("BUSTER_TEST_JOBS") == "2" and
                    environment.get("BUSTER_TEST_TABLE_AUDITS") == "0", "Campaign CPU/audit environment differs")
    image_os, image_version = environment.get("ImageOS", ""), environment.get("ImageVersion", "")
    measure.require(image_os.strip() and image_version.strip(), "Missing runner image identity")
    architecture = environment.get("RUNNER_ARCH", "")
    measure.require(architecture.lower() in {"x64", "amd64", "x86_64"}, "Campaign requires a recorded x86-64 runner")
    runner_os = environment.get("RUNNER_OS")
    measure.require(runner_os is None or runner_os.lower() == platform, "Runner platform differs from campaign")
    toolchain = (directory / "toolchain.txt").read_text(encoding="utf-8-sig").strip()
    measure.require(toolchain and len(toolchain) <= 65536, "Missing or oversized toolchain observation")
    digest = hashlib.sha256()
    with binary.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    measure.require(binary.stat().st_size > 0, "Observed test binary is empty")
    value = {"source_revision": revision, "binary_sha256": digest.hexdigest(),
             "runner_image": f"{image_os}/{image_version}", "platform": platform, "architecture": "x86_64",
             "configuration": "Debug", "sanitize": True, "fuzz": True, "table_audits": False,
             "toolchain": toolchain, "cpu_budget": 4}
    return value


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def assemble(directory, binary, platform, pairs, environment):
    measure.require(pairs in (1, 3), "Campaign requires one screening pair or three comparison pairs")
    measure.require(directory.is_dir(), "Campaign evidence directory does not exist")
    identities = provenance(directory, binary, platform, environment)
    rows = inventory(directory / "inventory.log")
    samples = []
    phases = []
    for number in range(1, pairs * 2 + 1):
        arm = "baseline" if number % 2 else "candidate"
        identifier = f"sample-{number}-{arm}"
        test_workers = 2 if arm == "baseline" else 4
        phase = native_phase(directory / "phases", identifier, test_workers)
        phases.append(phase)
        manifest = {"schema": "buster-ci-unit-tests-measure-v1", "arm": arm,
                    "mode": "serial" if arm == "baseline" else "groups", "identity": identities,
                    "inventory": rows, "log": identifier + ".log", "exit_code": 0,
                    "elapsed_us": phase["elapsed_us"], "test_workers": test_workers, "native_phase": phase}
        path = directory / (identifier + ".json")
        write_json(path, manifest)
        measure.validate_sample(path)
        samples.append(path.name)
    campaign = directory / "campaign.json"
    write_json(campaign, {"schema": "buster-ci-unit-tests-campaign-v1", "samples": samples})
    comparison = measure.compare(campaign, screening=pairs == 1)
    comparison["native_phases"] = phases
    comparison["binary_observation"] = {"sha256": identities["binary_sha256"], "size_bytes": binary.stat().st_size}
    comparison["inventory_observation"] = "Independent inventory.log query of the same observed binary"
    comparison["outer_wall_interval"] = "Native end_us minus child_start_us around each complete CMake target invocation"
    return comparison


def summary(result):
    text = "### Isolated unit-test campaign\n\n"
    if "error" in result:
        message = str(result["error"]).replace("\n", " ").replace("`", "'")
        text += f"Evidence validation failed: `{message}`.\n"
    else:
        identity = result["identity"]
        baseline, candidate = result["arms"]["baseline"], result["arms"]["candidate"]
        kind = "One-pair screening" if result["screening_valid"] else "Three-pair comparison"
        text += f"{kind} on {identity['platform']} ({identity['runner_image']}), source `{identity['source_revision']}`.\n\n"
        text += "| Arm | Samples | Median outer wall |\n| --- | ---: | ---: |\n"
        for name, arm in (("Baseline", baseline), ("Candidate", candidate)):
            text += f"| {name} | {arm['n']} | {arm['median_wall_us'] / 1000000:.3f} s |\n"
        text += f"\nCandidate/baseline outer wall ratio: **{result['candidate_wall_ratio']:.3f}**. Exact module and assertion counts agree.\n"
        text += "\nThe available runner budget is four workers. Baseline modules use two workers; candidate launches two isolated groups with two workers each.\n"
    text += "\nDiagnostic evidence only. `ci_complete=false`; `performance_accepted=false`. Queues, build/setup, artifact transfer and general CI throughput are outside these intervals.\n"
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--platform", required=True, choices=("linux", "windows"))
    parser.add_argument("--pairs", required=True, type=int, choices=(1, 3))
    args = parser.parse_args(argv)
    try:
        result = assemble(args.directory, args.binary, args.platform, args.pairs, os.environ)
        status = 0
    except (measure.EvidenceError, OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError, AttributeError) as error:
        result = {"schema": "buster-ci-unit-tests-error-v1", "error": str(error), "ci_complete": False,
                  "performance_accepted": False, "measurement_review_ready": False}
        status = 1
    try:
        write_json(args.directory / "comparison.json", result)
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as stream:
                stream.write(summary(result))
    except OSError as error:
        sys.stderr.write(f"Could not publish campaign evidence: {error}\n")
        status = 1
    if "error" in result:
        sys.stderr.write(f"Campaign evidence rejected: {result['error']}\n")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
