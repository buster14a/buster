#!/usr/bin/env python3
"""Offline diagnostic comparison of full and partitioned unit-test invocations.

This is a measurement-review helper, not CI admission or a CI-complete gate.
`sample MANIFEST` validates one retained invocation; `compare CAMPAIGN` requires
at least three alternating baseline/candidate samples with identical identity.
`screen CAMPAIGN` accepts exactly one alternating pair and makes no admission
claim; it is a cheap diagnostic before collecting the larger campaign.
Manifest format: schema="buster-ci-unit-tests-measure-v1", arm=baseline|candidate,
mode=serial|groups, identity={source_revision,binary_sha256,runner_image,platform,
architecture,configuration,sanitize,fuzz,table_audits,toolchain,cpu_budget},
inventory=[{index,name,table_audit}], log="relative/path", exit_code=0,
test_workers=<actual invocation BUSTER_TEST_JOBS, not the overall CPU budget>,
elapsed_us=<positive outer native phase wall interval>. Group mode validates
CI_UNIT_*_V1 wall/process intervals first, then accepts an optional elapsed_us
covering the same CMake/Ninja lifecycle as the serial baseline. That outer
interval must contain the native group interval, which is retained separately.
Campaign format: schema="buster-ci-unit-tests-campaign-v1", samples=[manifest
paths in execution order]. All paths resolve against their declaring JSON file.
Binary hashes/source/image are recorded provenance, not independently attested
by this parser. An optional binary path verifies the actual retained file hash.
Raw logs are never rewritten: human diagnostic bytes may be non-UTF-8, while
every recognizable machine-proof line must retain valid UTF-8. BOM-marked
UTF-16 logs retain their strict decoding contract.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys


IDENTITY_KEYS = ("source_revision", "binary_sha256", "runner_image", "platform", "architecture",
                 "configuration", "sanitize", "fuzz", "table_audits", "toolchain", "cpu_budget")
MODULE = re.compile(r"^TEST_MODULE_TIMING index=(\d+) module=(\S+) duration_ns=(\d+) "
                    r"passed=(\d+) failed=(\d+) assertions=(\d+) status=(pass|fail)$")
TERMINAL = re.compile(r"^\[(\d+)/(\d+)\] (Unit|Module|External) tests"
                      r"(?: \((\d+) of (\d+) modules selected\))?$")
PREFIX = re.compile(r"^\ufeff?\d{4}-\d{2}-\d{2}T\S+Z ")
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
INVALID_UTF8 = re.compile(r"[\udc80-\udcff]")
PROOF_MARKERS = ("TEST_MODULE_TIMING", "CI_UNIT_", "UNIT_TEST_", "Unit tests", "Module tests", "External tests")


class EvidenceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def integer(value, name, minimum=0):
    require(type(value) is int and value >= minimum, f"{name}: expected integer >= {minimum}")
    return value


def record(line, prefix):
    require(line.startswith(prefix + " "), f"Malformed {prefix} record")
    fields = {}
    for item in line[len(prefix) + 1:].split():
        require("=" in item, f"Malformed {prefix} field: {item}")
        key, value = item.split("=", 1)
        require(key and value and key not in fields, f"Duplicate/empty {prefix} field: {key}")
        fields[key] = value
    return fields


def number(fields, name):
    value = fields.get(name, "")
    require(value.isascii() and value.isdecimal(), f"Missing/malformed native integer {name}")
    return int(value)


def possible_proof_marker(line, marker):
    # Invalid octets may replace or interrupt marker letters. Classify that
    # claim before decoding/normalizing could hide an extra failed record.
    positions = {0: 0}
    claimed = False
    # This cheap necessary condition avoids running the marker matcher over
    # long binary diagnostic spans with no literal proof information at all.
    if sum(character in marker for character in line) > len(marker) // 2:
        for character in line:
            next_positions = {0: 0}
            if INVALID_UTF8.fullmatch(character):
                for position, count in positions.items():
                    for end in range(position, len(marker) + 1):
                        next_positions[end] = max(next_positions.get(end, 0), count)
            else:
                for position, count in positions.items():
                    if position < len(marker) and marker[position] == character:
                        next_positions[position + 1] = max(next_positions.get(position + 1, 0), count + 1)
            # An arbitrary human octet alone does not claim a proof marker.
            # More than half the marker must remain literally and in order.
            if next_positions.get(len(marker), 0) > len(marker) // 2:
                claimed = True
                break
            positions = {position: count for position, count in next_positions.items() if position < len(marker)}
    return claimed


def malformed_proof_claim(line):
    # Inspect the original line as well as its display-normalized form. In
    # particular, invalid bytes inside an ANSI/Actions prefix cannot disappear.
    return any(possible_proof_marker(candidate, marker) for candidate in (line, ANSI.sub("", line)) for marker in PROOF_MARKERS)


def read_log(path):
    raw = path.read_bytes()
    utf16 = raw.startswith((b"\xff\xfe", b"\xfe\xff"))
    text = raw.decode("utf-16") if utf16 else raw.decode("utf-8-sig", errors="surrogateescape")
    lines = []
    for number, line in enumerate(text.splitlines(), 1):
        require(not INVALID_UTF8.search(line) or not malformed_proof_claim(line),
                f"Invalid UTF-8 in machine proof record at log line {number}")
        lines.append(PREFIX.sub("", ANSI.sub("", line)).strip())
    return lines


def inventory_rows(manifest):
    rows = manifest.get("inventory")
    require(isinstance(rows, list) and rows, "Missing independent module inventory")
    names = set()
    indices = set()
    for row in rows:
        integer(row.get("index"), "inventory.index")
        require(isinstance(row.get("name"), str) and re.fullmatch(r"[A-Za-z0-9_]+", row["name"]), "Invalid inventory name")
        require(type(row.get("table_audit")) is bool, "Missing explicit table_audit inventory flag")
        require(row["name"] not in names and row["index"] not in indices, "Duplicate inventory name/index")
        names.add(row["name"])
        indices.add(row["index"])
    require(indices == set(range(len(rows))), "Inventory must retain every canonical index")
    return {row["name"]: row for row in rows}


def parse_modules(lines):
    modules = {}
    indices = set()
    for line in lines:
        if line.startswith("TEST_MODULE_TIMING"):
            match = MODULE.fullmatch(line)
            require(match is not None, "Malformed module timing record")
            index, name, duration, passed, failed, assertions, status = match.groups()
            row = dict(index=int(index), name=name, duration_ns=int(duration), passed=int(passed),
                       failed=int(failed), assertions=int(assertions), status=status)
            require(name not in modules and row["index"] not in indices, "Duplicate executed module name/index")
            require(row["passed"] + row["failed"] == row["assertions"], f"Inconsistent counts: {name}")
            require(status == "pass" and row["failed"] == 0, f"Failed module: {name}")
            modules[name] = row
            indices.add(row["index"])
    require(modules, "Missing module timing records")
    return modules


def parse_terminals(lines, modules, registered_count, selected):
    terminals = {}
    for line in lines:
        if re.match(r"^\[\d+/\d+\] (?:Unit|Module|External) tests", line):
            match = TERMINAL.fullmatch(line)
            require(match is not None, "Malformed terminal test summary")
            passed, total, kind, selection_count, registry_count = match.groups()
            require(kind not in terminals, f"Duplicate terminal {kind} summary")
            require(passed == total, f"Failed terminal {kind} summary")
            if kind == "Unit":
                if selected:
                    require(selection_count is not None and int(selection_count) == len(modules) and
                            int(registry_count) == registered_count, "Selected summary inventory mismatch")
                else:
                    require(selection_count is None, "Baseline must be an ordinary full invocation")
            terminals[kind] = {"passed": int(passed), "total": int(total)}
    require(set(terminals) == {"Unit", "Module", "External"}, "Missing terminal test summaries")
    require(terminals["Unit"]["total"] == sum(row["assertions"] for row in modules.values()), "Terminal assertion count mismatch")
    require(terminals["Module"]["total"] == len(modules), "Terminal module count mismatch")
    return terminals


def exact_modules(modules, expected, inventory):
    require(set(modules) == set(expected), "Executed module union differs from expected inventory")
    for name, row in modules.items():
        require(row["index"] == inventory[name]["index"], f"Canonical index mismatch: {name}")


def identity(manifest, directory):
    value = manifest.get("identity", {})
    require(isinstance(value, dict), "Invalid identity")
    require(all(key in value for key in IDENTITY_KEYS), "Incomplete binary/source/runner/configuration identity")
    require(re.fullmatch(r"[0-9a-f]{40}", str(value["source_revision"])) is not None, "Unresolved exact source revision")
    require(re.fullmatch(r"[0-9a-f]{64}", str(value["binary_sha256"])) is not None, "Unresolved exact binary SHA-256")
    for key in ("runner_image", "platform", "architecture", "configuration", "toolchain"):
        require(bool(value[key]), f"Missing identity {key}")
    for key in ("sanitize", "fuzz", "table_audits"):
        require(type(value[key]) is bool, f"Identity {key} must be boolean")
    integer(value["cpu_budget"], "cpu_budget", 1)
    if manifest.get("binary"):
        digest = hashlib.sha256((directory / manifest["binary"]).read_bytes()).hexdigest()
        require(digest == value["binary_sha256"], "Retained binary hash differs from identity")
    return value


def native_records(lines, prefix):
    return [record(line, prefix) for line in lines if line.startswith(prefix)]


def validate_groups(lines, inventory, expected, provenance, inventory_primary):
    plans = native_records(lines, "CI_UNIT_PLAN_V1")
    partitions = native_records(lines, "CI_UNIT_PARTITION_V1")
    processes = native_records(lines, "CI_UNIT_PROCESS_V1")
    require(len(plans) == len(partitions) == 1 and len(processes) == 2, "Missing/duplicate native parent/process records")
    plan, partition = plans[0], partitions[0]
    require(plan.get("binary_sha256") == provenance["binary_sha256"] and
            plan.get("source_revision") == provenance["source_revision"], "Parent binary/source identity mismatch")
    primary_module = plan.get("primary_module")
    require(primary_module in {"c_frontend_tests", "compiler_driver_tests"} and primary_module in inventory and
            primary_module in expected, "Unsupported/missing primary module anchor")
    require(primary_module == inventory_primary, "Plan primary module differs from independent inventory")
    expected_primary = "c_frontend_tests" if provenance["platform"] == "windows" and provenance["architecture"] == "x86_64" else "compiler_driver_tests"
    require(primary_module == expected_primary, "Plan primary module differs from platform policy")
    require(number(plan, "workers") == provenance["cpu_budget"] and number(plan, "groups") == 2, "Parent plan CPU/group mismatch")
    group_workers = number(plan, "group_workers")
    require(group_workers >= 1 and group_workers * 2 <= provenance["cpu_budget"], "Declared child quota exceeds CPU budget")
    starts = [i for i, line in enumerate(lines) if line.startswith("CI_UNIT_MODULE_V1 ") and number(record(line, "CI_UNIT_MODULE_V1"), "index") == 0]
    require(len(starts) == 2, "Missing/duplicate complete child inventory blocks")
    boundaries = starts + [len(lines)]
    groups = {}
    combined = {}
    external_total = 0
    for begin, end in zip(boundaries, boundaries[1:]):
        block = lines[begin:end]
        batches = native_records(block, "CI_UNIT_BATCH_V1")
        require(len(batches) == 1, "Missing/duplicate child terminal batch record")
        batch = batches[0]
        group = batch.get("group")
        require(group in {"primary", "rest"} and group not in groups, "Unknown/duplicate child group")
        declared = native_records(block, "CI_UNIT_MODULE_V1")
        require(len(declared) == len(inventory), "Incomplete registered child inventory")
        seen = set()
        selected = []
        for row in declared:
            name = row.get("module")
            require(name in inventory and name not in seen, "Unknown/duplicate declared module")
            seen.add(name)
            require(number(row, "index") == inventory[name]["index"], "Declared canonical index mismatch")
            require(number(row, "table_audit") == int(inventory[name]["table_audit"]), "Declared table-audit flag mismatch")
            require(number(row, "enabled") == int(name in expected), "Disabled audit inventory mismatch")
            owner = "primary" if name == primary_module else "rest"
            require(row.get("group") == owner, "Declared module owner mismatch")
            require(number(row, "selected") == int(name in expected and owner == group), "Child selected inventory mismatch")
            if number(row, "selected"):
                selected.append(name)
        modules = parse_modules(block)
        exact_modules(modules, selected, inventory)
        terminals = parse_terminals(block, modules, len(inventory), True)
        assertions = sum(row["assertions"] for row in modules.values())
        require(batch.get("status") == "pass" and number(batch, "failed") == 0 and
                number(batch, "modules") == number(batch, "modules_passed") == len(modules) and
                number(batch, "assertions") == number(batch, "passed") == assertions and
                number(batch, "external") == number(batch, "external_passed") == terminals["External"]["total"],
                "Child terminal batch counts/status mismatch")
        require(not set(combined).intersection(modules), "Duplicate module across child groups")
        combined.update(modules)
        external_total += terminals["External"]["total"]
        groups[group] = {"modules": sorted(modules), "assertions": assertions}
    require(set(groups) == {"primary", "rest"}, "Missing required child group")
    exact_modules(combined, expected, inventory)
    process_names = set()
    intervals = []
    for process in processes:
        group = process.get("group")
        require(group in groups and group not in process_names, "Unknown/duplicate process group")
        process_names.add(group)
        require(process.get("status") == "pass", "Child process failed")
        for key in ("exit", "native_status", "timed_out", "capture_failed", "cleanup_failed"):
            require(number(process, key) == 0, f"Child process did not complete cleanly: {key}")
        require(number(process, "workers") == group_workers, "Child process quota differs from plan")
        duration = number(process, "elapsed_us")
        require(duration > 0, "Missing positive child elapsed time")
        start, finish = number(process, "start_us"), number(process, "end_us")
        require(finish > start and finish - start == duration, "Child native interval mismatch")
        intervals.append((start, 1, group_workers))
        intervals.append((finish, 0, -group_workers))
        groups[group]["elapsed_us"] = duration
        groups[group]["test_workers"] = group_workers
    concurrent = peak = 0
    for _, _, change in sorted(intervals):
        concurrent += change
        peak = max(peak, concurrent)
    require(peak <= provenance["cpu_budget"], "Observed child concurrency exceeds CPU budget")
    require(partition.get("status") == "pass" and number(partition, "groups") == 2 and
            number(partition, "workers") == provenance["cpu_budget"] and number(partition, "failed") == 0 and
            number(partition, "modules") == len(combined) and
            number(partition, "assertions") == number(partition, "passed") == sum(row["assertions"] for row in combined.values()),
            "Parent partition terminal counts/status mismatch")
    wall = number(partition, "elapsed_us")
    require(wall > 0 and wall >= max(group["elapsed_us"] for group in groups.values()), "Parent wall interval does not contain child work")
    return combined, wall, sum(group["elapsed_us"] for group in groups.values()), peak, external_total, groups


def validate_sample(path):
    path = Path(path).resolve()
    manifest = json.loads(path.read_text())
    require(manifest.get("schema") == "buster-ci-unit-tests-measure-v1", "Unknown sample schema")
    require(manifest.get("arm") in {"baseline", "candidate"}, "Unknown comparison arm")
    require(manifest.get("mode") in {"serial", "groups"}, "Unknown invocation mode")
    require(type(manifest.get("exit_code")) is int and manifest["exit_code"] == 0, "Invocation exit status is missing or failed")
    provenance = identity(manifest, path.parent)
    test_workers = manifest.get("test_workers")
    if test_workers is not None:
        integer(test_workers, "test_workers", 1)
        require(test_workers <= provenance["cpu_budget"], "Invocation test workers exceed CPU budget")
    inventory = inventory_rows(manifest)
    expected = {name for name, row in inventory.items() if provenance["table_audits"] or not row["table_audit"]}
    require(expected, "Empty effective module inventory")
    lines = read_log(path.parent / manifest["log"])
    if manifest["mode"] == "serial":
        require(not any(line.startswith("CI_UNIT_") for line in lines), "Baseline must retain ordinary full serial path")
        modules = parse_modules(lines)
        exact_modules(modules, expected, inventory)
        terminals = parse_terminals(lines, modules, len(inventory), False)
        wall = integer(manifest.get("elapsed_us"), "elapsed_us", 1)
        native_group_wall = None
        runner_work, peak, external, groups = wall, test_workers, terminals["External"]["total"], {}
    else:
        modules, wall, runner_work, peak, external, groups = validate_groups(lines, inventory, expected, provenance, manifest.get("primary_module"))
        require(test_workers is None or test_workers == provenance["cpu_budget"], "Candidate invocation workers differ from native parent plan")
        test_workers = provenance["cpu_budget"]
        native_group_wall = wall
        if "elapsed_us" in manifest:
            wall = integer(manifest["elapsed_us"], "elapsed_us", 1)
            require(wall >= native_group_wall, "Outer wall interval does not contain native group work")
    return {"schema": "buster-ci-unit-tests-observation-v1", "manifest": str(path), "arm": manifest["arm"],
            "mode": manifest["mode"], "identity": provenance, "inventory": list(inventory.values()),
            "skipped_table_audits": sorted(set(inventory) - expected), "modules": modules,
            "assertions": sum(row["assertions"] for row in modules.values()), "external": external,
            "wall_us": wall, "native_group_wall_us": native_group_wall,
            "test_workers": test_workers,
            "summed_child_wall_us": runner_work, "peak_declared_child_workers": peak,
            "groups": groups, "isolated_invocation_valid": True, "ci_complete": False,
            "performance_accepted": False, "provenance_authority": "Recorded identities; optional retained-binary hash check"}


def compare(path, screening=False):
    path = Path(path).resolve()
    campaign = json.loads(path.read_text())
    require(campaign.get("schema") == "buster-ci-unit-tests-campaign-v1", "Unknown campaign schema")
    samples = [validate_sample(path.parent / item) for item in campaign.get("samples", [])]
    counts = collections.Counter(sample["arm"] for sample in samples)
    if screening:
        require(len(samples) == 2 and counts["baseline"] == counts["candidate"] == 1, "Screening requires exactly one sample per arm")
    else:
        require(len(samples) >= 6, "At least three alternating samples per arm are required")
        require(counts["baseline"] >= 3 and counts["candidate"] >= 3, "Insufficient samples in one arm")
    require(all(a["arm"] != b["arm"] for a, b in zip(samples, samples[1:])), "Sample execution order must alternate arms")
    reference = samples[0]
    for sample in samples:
        require(sample["identity"] == reference["identity"], "Binary/source/image/configuration/CPU identity differs")
        require(sample["inventory"] == reference["inventory"] and
                sample["skipped_table_audits"] == reference["skipped_table_audits"], "Registered/audit inventory differs")
        require(set(sample["modules"]) == set(reference["modules"]), "Module union differs between arms")
        require(sample["external"] == reference["external"], "External assertion population differs")
        for name in sample["modules"]:
            for key in ("index", "assertions", "passed", "failed", "status"):
                require(sample["modules"][name][key] == reference["modules"][name][key], f"Module count/status differs: {name}.{key}")
        require(sample["mode"] == ("serial" if sample["arm"] == "baseline" else "groups"), "Arm invocation mode mismatch")
    arms = {}
    for arm in ("baseline", "candidate"):
        chosen = [sample for sample in samples if sample["arm"] == arm]
        require(len({sample["test_workers"] for sample in chosen}) == 1, "Invocation worker allocation changes within one comparison arm")
        arms[arm] = {"n": len(chosen), "median_wall_us": statistics.median(sample["wall_us"] for sample in chosen),
                     "test_workers": chosen[0]["test_workers"],
                     "peak_declared_child_workers_samples": [sample["peak_declared_child_workers"] for sample in chosen],
                     "native_group_wall_us_samples": [sample["native_group_wall_us"] for sample in chosen],
                     "median_summed_child_wall_us": statistics.median(sample["summed_child_wall_us"] for sample in chosen),
                     "wall_us_samples": [sample["wall_us"] for sample in chosen],
                     "modules": {name: {"duration_ns_samples": [sample["modules"][name]["duration_ns"] for sample in chosen],
                                         "median_duration_ns": statistics.median(sample["modules"][name]["duration_ns"] for sample in chosen)}
                                 for name in sorted(reference["modules"])}}
    return {"schema": "buster-ci-unit-tests-comparison-v1", "identity": reference["identity"], "arms": arms,
            "candidate_wall_ratio": arms["candidate"]["median_wall_us"] / arms["baseline"]["median_wall_us"],
            "candidate_summed_child_wall_ratio": arms["candidate"]["median_summed_child_wall_us"] / arms["baseline"]["median_summed_child_wall_us"],
            "measurement_review_ready": not screening and all(sample["test_workers"] is not None for sample in samples), "screening_valid": screening,
            "ci_complete": False, "performance_accepted": False,
            "limits": ["Isolated unit-test intervals exclude workflow queues, build, setup and artifact transfer",
                       "Summed child wall is process-wall sum on one runner, not runner occupancy or CPU time; worker budgets are declared quotas",
                       "Missing baseline test_workers leaves its actual worker allocation unknown; exact assertion equality is still required",
                       "Alternating observations do not prove general runner/image equivalence or statistical significance"],
            "samples": samples}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("sample", "screen", "compare"))
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate_sample(args.input) if args.command == "sample" else compare(args.input, args.command == "screen")
    except (EvidenceError, OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        result = {"schema": "buster-ci-unit-tests-error-v1", "error": str(error), "ci_complete": False,
                  "performance_accepted": False, "measurement_review_ready": False}
        status = 1
    else:
        status = 0
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        sys.stdout.write(text)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
