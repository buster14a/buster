#!/usr/bin/env python3
"""Paired same-source full object compilations; service timings stay separate."""
import hashlib
import json
import os
import pathlib
import platform
import statistics
import subprocess
import sys

before, after, source, output = map(lambda p: pathlib.Path(p).resolve(), sys.argv[1:5])
cpus = sorted(os.sched_getaffinity(0))
os.sched_setaffinity(0, {cpus[0]})
common = ["cc", "-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1",
          "-DBUSTER_INCLUDE_TESTS=0", "-g0", "-march=native",
          "-fregister-allocator=fast", "-c", "src/buster/apps/ide/ide.c"]
report = {"platform": platform.platform(), "cpu": cpus[0], "pairs": 6,
          "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
          "same_source_root": str(source), "common_argv": common,
          "binary_sha256": {label: hashlib.sha256(binary.read_bytes()).hexdigest()
                            for label, binary in (("baseline", before), ("candidate", after))},
          "samples": []}
for pair in range(-1, report["pairs"]):
    sequence = (("baseline", before), ("candidate", after))
    if pair % 2:
        sequence = tuple(reversed(sequence))
    for label, binary in sequence:
        stem = output.parent / f"compiler-{pair}-{label}"
        artifact = stem.with_suffix(".o")
        timing = stem.with_suffix(".time")
        argv = ["/usr/bin/time", "-f", "%e %U %S %M", "-o", str(timing),
                str(binary), *common, "-o", str(artifact)]
        with stem.with_suffix(".stdout").open("wb") as stdout, stem.with_suffix(".stderr").open("wb") as stderr:
            run = subprocess.run(argv, cwd=source, stdout=stdout, stderr=stderr, timeout=300)
        fields = timing.read_text().splitlines()[-1].split()
        row = {"pair": pair, "version": label, "argv": argv, "returncode": run.returncode,
               "wall_seconds": float(fields[0]), "user_seconds": float(fields[1]),
               "system_seconds": float(fields[2]), "peak_rss_kib": int(fields[3])}
        if run.returncode == 0:
            row["object_sha256"] = hashlib.sha256(artifact.read_bytes()).hexdigest()
            row["object_bytes"] = artifact.stat().st_size
        report["samples"].append(row)
        output.write_text(json.dumps(report, indent=2) + "\n")
        if run.returncode:
            raise SystemExit(f"failed compile: {stem}; retained all evidence")
        artifact.unlink()
assert len({row["object_sha256"] for row in report["samples"]}) == 1
summary = {}
for label in ("baseline", "candidate"):
    rows = [row for row in report["samples"] if row["pair"] >= 0 and row["version"] == label]
    summary[label] = {key: {"median": statistics.median(row[key] for row in rows),
                            "min": min(row[key] for row in rows),
                            "max": max(row[key] for row in rows)}
                      for key in ("wall_seconds", "user_seconds", "peak_rss_kib")}
print(json.dumps(summary, indent=2))
