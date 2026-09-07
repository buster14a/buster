#!/usr/bin/env python3
"""Pair identical query traces against genuine old/new production ABI services."""
import json
import os
import pathlib
import platform
import statistics
import subprocess
import sys

before, after, output = map(pathlib.Path, sys.argv[1:4])
# Pin both processes to one permitted logical CPU to reduce migration noise.
# This does not isolate a shared VM or promise an idle physical core.
cpus = sorted(os.sched_getaffinity(0))
chosen_cpu = cpus[0]
os.sched_setaffinity(0, {chosen_cpu})
result = {"platform": platform.platform(), "cpu": chosen_cpu, "pairs": 12,
          "warmups_per_case_and_binary": 1, "samples": []}
for case in ("dense", "sparse"):
    for binary in (before, after):
        subprocess.run([str(binary), case], check=True, capture_output=True)
    for pair in range(result["pairs"]):
        sequence = (before, after) if pair % 2 == 0 else (after, before)
        for binary in sequence:
            row = json.loads(subprocess.check_output([str(binary), case], text=True))
            row["pair"] = pair
            row["binary"] = str(binary)
            result["samples"].append(row)
    rows = [row for row in result["samples"] if row["case"] == case]
    assert len({row["fingerprint"] for row in rows}) == 1
    assert len({row["checksum"] for row in rows}) == 1
    assert all(row["warm_misses"] == 0 for row in rows)
    for version in (1, 0):
        selected = [row for row in rows if row["before"] == version]
        phases = {
            "prepare_ns_per_compilation": [r["preparation_ns"] / r["cold_iterations"] for r in selected],
            "first_trace_ns_per_compilation": [r["first_queries_ns"] / r["cold_iterations"] for r in selected],
            "cold_total_ns_per_compilation": [(r["preparation_ns"] + r["first_queries_ns"]) / r["cold_iterations"] for r in selected],
            "warm_ns_per_query": [r["warm_ns"] / r["warm_queries"] for r in selected],
        }
        print(json.dumps({"case": case, "before": version, "timings": {
            key: {"median": statistics.median(values), "min": min(values), "max": max(values)}
            for key, values in phases.items()}}))
output.write_text(json.dumps(result, indent=2) + "\n")
