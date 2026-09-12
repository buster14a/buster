"""Run from the repository root after building the two diagnostic lexer hosts."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

out = Path(sys.argv[1]).resolve()
cpu = min(os.sched_getaffinity(0))
os.sched_setaffinity(0, {cpu})
rows = []
for pair in range(-1, 12):
    order = ("baseline", "candidate") if pair % 2 else ("candidate", "baseline")
    for variant in order:
        result = subprocess.run([str(out / ("bench-" + variant)), "bench"], capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            if line.startswith("BENCH_UTF8_ASCII"):
                row = {key: int(value) for key, value in (field.split("=") for field in line.split()[1:])}
                row.update(pair=pair, variant=variant, cpu=cpu)
                rows.append(row)
with (out / "lexer-benchmark-raw.jsonl").open("w") as stream:
    for row in rows:
        stream.write(json.dumps(row) + "\n")
summary = {}
for scalar in (0, 1):
    selected = [row for row in rows if row["scalar"] == scalar and row["pair"] >= 0]
    summary[str(scalar)] = {
        variant: statistics.median(row["median_ns"] for row in selected if row["variant"] == variant)
        for variant in ("baseline", "candidate")
    }
    ratios = []
    for pair in range(12):
        values = {row["variant"]: row["median_ns"] for row in selected if row["pair"] == pair}
        ratios.append(values["candidate"] / values["baseline"])
    summary[str(scalar)]["median_paired_ratio"] = statistics.median(ratios)
summary["binaries"] = {
    variant: hashlib.sha256((out / ("bench-" + variant)).read_bytes()).hexdigest()
    for variant in ("baseline", "candidate")
}
input_bytes = Path("tests/basic_c_operations.c").read_bytes()
assert input_bytes.isascii()
summary["input_sha256"] = hashlib.sha256(input_bytes).hexdigest()
summary["input_bytes"] = len(input_bytes) * 64
summary["cpu"] = cpu
summary["candidate_source_sha256"] = hashlib.sha256(Path("src/buster/lib/compiler/frontend/c/c_source.c").read_bytes()).hexdigest()
(out / "lexer-benchmark-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
