"""Paired -E controls; native process measurement comes from platform.h."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("baseline")
parser.add_argument("candidate")
parser.add_argument("collector")
parser.add_argument("output")
args = parser.parse_args()
root = Path.cwd()
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
binaries = {name: str(Path(getattr(args, name)).resolve()) for name in ("baseline", "candidate")}
collector = str(Path(args.collector).resolve())
cases = {
    "call_abi": ["tests/basic_c_call_abi.c"],
    "operations": ["tests/basic_c_operations.c"],
    "compiler": ["-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "src/buster/apps/ide/ide.c"],
}
provenance = {
    "binaries": {name: {"path": path, "sha256": sha256(path)} for name, path in binaries.items()},
    "collector_sha256": sha256(collector),
    "platform_header_sha256": sha256("tools/throughput/platform.h"),
    "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff"])).hexdigest(),
    "uname": list(os.uname()),
    "cpuinfo": Path("/proc/cpuinfo").read_text().split("\n\n")[0],
    "affinity": sorted(os.sched_getaffinity(0)),
    "pairs": 8,
    "warmups_per_variant": 1,
}
(out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
rows = []
with (out / "raw.jsonl").open("w") as raw:
    for case, flags in cases.items():
        output_hash = None
        for pair in range(-1, 8):
            order = ("baseline", "candidate") if pair % 2 else ("candidate", "baseline")
            for variant in order:
                artifact = out / "preprocessed.i"
                log = out / "compiler.log"
                command = [binaries[variant], "cc", "-E", *flags, "-o", str(artifact)]
                measured = subprocess.run([collector, str(log), str(root), *command], text=True, capture_output=True, check=True)
                row = json.loads(measured.stdout)
                row.update(case=case, pair=pair, variant=variant, command=command,
                           output_sha256=sha256(artifact), output_bytes=artifact.stat().st_size,
                           log_sha256=sha256(log), log_bytes=log.stat().st_size)
                if output_hash is None:
                    output_hash = row["output_sha256"]
                if row["output_sha256"] != output_hash:
                    raise RuntimeError(f"Output mismatch in {case}, {variant}, pair {pair}")
                rows.append(row)
                raw.write(json.dumps(row) + "\n")
                raw.flush()
summary = {}
for case in cases:
    summary[case] = {}
    for variant in binaries:
        population = [r for r in rows if r["case"] == case and r["variant"] == variant and r["pair"] >= 0]
        summary[case][variant] = {
            field: {"min": min(r[field] for r in population), "median": statistics.median(r[field] for r in population),
                    "max": max(r[field] for r in population)}
            for field in ("wall_seconds", "user_seconds", "system_seconds", "peak_rss_bytes")
        }
    ratios = []
    for pair in range(8):
        values = {r["variant"]: r["wall_seconds"] for r in rows if r["case"] == case and r["pair"] == pair}
        ratios.append(values["candidate"] / values["baseline"])
    summary[case]["paired_wall_ratio"] = {"min": min(ratios), "median": statistics.median(ratios), "max": max(ratios)}
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
