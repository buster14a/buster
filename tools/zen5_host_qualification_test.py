"""Offline regression tests for the Zen 5 host qualification record."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
from typing import Any

import zen5_host_qualification as capture_tool
from zen5_qualification_common import (
    RESULT_SCHEMA,
    QualificationError,
    canonical_bytes,
    environment_contract,
    group_expression,
    load_manifest,
    map_perf_entries,
    parse_cpuinfo,
    parse_perf_json,
    sha256_bytes,
    validate_manifest,
)
from zen5_qualification_replay import compare_results, validate_result

def run_self_test() -> int:
    assert capture_tool.MAX_CAPTURE_BYTES > 0
    assert capture_tool.manifest_path().name == "zen5_pmu_events_v1.json"
    assert capture_tool.canonical_bytes({"a": 1}) == b'{"a":1}\n'
    manifest, manifest_digest = load_manifest()
    assert not validate_manifest(manifest)
    assert group_expression(manifest["groups"][0]).startswith("{instructions:u,cycles:u")
    assert int(manifest["groups"][1]["events"][1]["config_hex"], 0) == 0xF864

    perf_rows = [
        {
            "counter-value": "15806850033.250000",
            "unit": "",
            "event": "instructions:u",
            "event-runtime": "1000000000",
            "pcnt-running": "100.00",
        },
        {
            "counter-value": "5713400806.000000",
            "unit": "",
            "event": "cycles:u",
            "event-runtime": "1000000000.000000",
            "pcnt-running": "99.75",
        },
        {
            "counter-value": "2992656435",
            "unit": "",
            "event": "branches:u",
            "event-runtime": "1000000000",
            "pcnt-running": "100.00",
        },
        {
            "counter-value": "51290153",
            "unit": "",
            "event": "branch-misses:u",
            "event-runtime": "1000000000",
            "pcnt-running": "100.00",
        },
    ]
    text = "\n".join(json.dumps(row) + "," for row in perf_rows)
    entries = parse_perf_json(text)
    observations, problems = map_perf_entries(manifest["groups"][0], entries, 0.9)
    assert not problems
    assert observations["instructions"]["count"] == 15806850033.25
    assert observations["cycles"]["count"] == 5713400806
    assert observations["cycles"]["event_runtime_ns"] == 1000000000
    assert observations["cycles"]["running_fraction"] == 0.9975

    unsupported = list(perf_rows)
    unsupported[3] = {
        "counter-value": "<not supported>",
        "unit": "",
        "event": "branch-misses:u",
        "event-runtime": "0",
        "pcnt-running": "0.00",
    }
    observations, problems = map_perf_entries(manifest["groups"][0], unsupported, 0.9)
    assert observations["branch_misses"]["count"] is None
    assert any("required event status is unsupported" in problem for problem in problems)

    low_running = list(perf_rows)
    low_running[1] = dict(low_running[1], **{"pcnt-running": "89.99"})
    _, problems = map_perf_entries(manifest["groups"][0], low_running, 0.9)
    assert any("below 0.900000" in problem for problem in problems)

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "proc").mkdir()
        (root / "proc/cpuinfo").write_text(
            "processor : 0\nvendor_id : AuthenticAMD\ncpu family : 26\nmodel : 68\n"
            "model name : AMD Ryzen 7 9700X 8-Core Processor\nstepping : 0\nmicrocode : 0xb404038\nflags : avx512f\n\n",
            encoding="utf-8",
        )
        fields = parse_cpuinfo((root / "proc/cpuinfo").read_text(encoding="utf-8"), 0)
        assert fields["model"] == "68"
        try:
            parse_cpuinfo((root / "proc/cpuinfo").read_text(encoding="utf-8"), 1)
            raise AssertionError("missing processor accepted")
        except QualificationError:
            pass

    contract = {"kernel": "a", "microcode": "b"}
    left = {
        "schema": RESULT_SCHEMA,
        "environment_contract": contract,
        "environment_fingerprint_sha256": sha256_bytes(canonical_bytes(contract)),
        "host": {"boot_id": "one"},
    }
    right = json.loads(json.dumps(left))
    assert not compare_results(left, right)["requalification_required"]
    right["environment_contract"]["kernel"] = "c"
    right["environment_fingerprint_sha256"] = sha256_bytes(canonical_bytes(right["environment_contract"]))
    comparison = compare_results(left, right)
    assert comparison["requalification_required"]
    assert comparison["changes"][0]["path"] == "kernel"

    host = {
        "hostname": "benchpress",
        "selected_cpu": 2,
        "cpu": {
            "vendor_id": "AuthenticAMD",
            "family": 26,
            "model": 68,
            "model_name": "AMD Ryzen 7 9700X 8-Core Processor",
            "stepping": 0,
            "microcode": "0xb404038",
            "flags_sha256": "1" * 64,
        },
        "kernel": {"release": "6.18.50-2-lts", "version": "test", "machine": "x86_64"},
        "boot_id": "00000000-0000-0000-0000-000000000001",
        "perf_version": "perf version 7.2.4",
        "perf_event_paranoid": "2",
        "kptr_restrict": "1",
        "topology": {
            "core_id": "2",
            "physical_package_id": "0",
            "thread_siblings_list": "2",
            "smt_active": "0",
        },
        "power_policy": {
            "boost": "1",
            "scaling_driver": "amd-pstate-epp",
            "scaling_governor": "powersave",
            "energy_performance_preference": "balance_performance",
            "scaling_min_freq": "600000",
            "scaling_max_freq": "5570000",
        },
        "firmware": {
            "bios_vendor": "test",
            "bios_version": "test-v1",
            "bios_date": "09/19/2026",
            "board_name": "test",
            "board_version": "test",
            "product_name": "test",
        },
        "memory": {"mem_total_bytes": 64 * 1024 * 1024 * 1024, "edac": None},
        "transparent_hugepage": "always [madvise] never",
    }
    workload = {
        "working_directory": "/immutable",
        "argv": ["/immutable/compiler", "input.c", "-o", "output.o"],
        "selected_cpu": 2,
        "executable": {"path": "/immutable/compiler", "size": 1, "mode": 0o555, "sha256": "2" * 64},
        "inputs_before": [],
        "inputs_after": [],
        "declared_output_artifacts": [],
    }
    result_groups: list[dict[str, Any]] = []
    for group_index, group in enumerate(manifest["groups"]):
        group_runs: list[dict[str, Any]] = []
        for repeat in range(manifest["repeats"]):
            rows = [
                {
                    "counter-value": str(1000 + group_index * 100 + event_index * 10 + repeat),
                    "unit": "",
                    "event": event["output_names"][0],
                    "event-runtime": "1000000",
                    "pcnt-running": "100.00",
                }
                for event_index, event in enumerate(group["events"])
            ]
            raw_perf = json.dumps(rows, sort_keys=True, separators=(",", ":"))
            replay, replay_problems = map_perf_entries(group, rows, float(manifest["minimum_running_fraction"]))
            assert not replay_problems
            group_runs.append(
                {
                    "repeat": repeat,
                    "invocation": [
                        "perf",
                        "stat",
                        "--json-output",
                        "--no-big-num",
                        "--output",
                        f"/tmp/perf-{group_index}-{repeat}.json",
                        "-e",
                        group_expression(group),
                        "--",
                        "taskset",
                        "-c",
                        "2",
                        *workload["argv"],
                    ],
                    "exit_status": 0,
                    "stdout_size": 0,
                    "stdout_sha256": sha256_bytes(b""),
                    "stderr_size": 0,
                    "stderr_sha256": sha256_bytes(b""),
                    "perf_json": raw_perf,
                    "perf_json_sha256": sha256_bytes(raw_perf.encode("utf-8")),
                    "observations": replay,
                    "output_artifacts": [],
                    "valid": True,
                    "invalid_reasons": [],
                }
            )
        result_groups.append({"id": group["id"], "definition": group, "runs": group_runs})
    environment = environment_contract(host, [])
    valid_result = {
        "schema": RESULT_SCHEMA,
        "version": 1,
        "created_utc": "2026-09-19T00:00:00Z",
        "purpose": manifest["purpose"],
        "event_manifest": "/immutable/zen5_pmu_events_v1.json",
        "event_manifest_sha256": manifest_digest,
        "tool_sha256": "3" * 64,
        "repository": {"root": "/immutable", "revision": "4" * 40, "tree": "5" * 40, "status": ""},
        "host": host,
        "environment_contract": environment,
        "environment_fingerprint_sha256": sha256_bytes(canonical_bytes(environment)),
        "workload": workload,
        "groups": result_groups,
        "optional_attribution": [
            {
                "id": item["id"],
                "required": False,
                "available": False,
                "pmu_type": None,
                "format": None,
                "caps": None,
                "cpumask": None,
                "measurement_status": "not-requested",
                "count": None,
                "running_fraction": None,
                "scope": item["scope"],
                "semantics": item["semantics"],
            }
            for item in manifest["optional_attribution"]
        ],
        "ordinary_timing_trials_separate": True,
        "statistical_decision": "not-evaluated",
        "qualification_status": "pmu-qualified",
        "invalid_reasons": [],
    }
    assert not validate_result(valid_result, manifest, manifest_digest)

    invalid_result = json.loads(json.dumps(valid_result))
    first_run = invalid_result["groups"][0]["runs"][0]
    invalid_rows = json.loads(first_run["perf_json"])
    invalid_rows[-1]["counter-value"] = "<not supported>"
    invalid_rows[-1]["event-runtime"] = "0"
    invalid_rows[-1]["pcnt-running"] = "0.00"
    raw_invalid = json.dumps(invalid_rows, sort_keys=True, separators=(",", ":"))
    replay, replay_problems = map_perf_entries(
        manifest["groups"][0], invalid_rows, float(manifest["minimum_running_fraction"])
    )
    assert len(replay_problems) == 1
    first_run["perf_json"] = raw_invalid
    first_run["perf_json_sha256"] = sha256_bytes(raw_invalid.encode("utf-8"))
    first_run["observations"] = replay
    first_run["valid"] = False
    first_run["invalid_reasons"] = replay_problems
    invalid_result["qualification_status"] = "invalid"
    invalid_result["invalid_reasons"] = [f"core-execution repeat 0: {replay_problems[0]}"]
    assert not validate_result(invalid_result, manifest, manifest_digest)
    invalid_result["qualification_status"] = "pmu-qualified"
    assert any("pmu-qualified result" in problem for problem in validate_result(invalid_result, manifest, manifest_digest))

    print("zen5_host_qualification self-test passed")
    return 0


