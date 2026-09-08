"""Compare trusted Clang-built compilers on frozen source and an alias corpus.

Usage: python3 this.py BASELINE CANDIDATE FROZEN_SOURCE_ROOT OUTPUT_DIRECTORY
Uses only Python's standard library; intended for an otherwise idle Linux host.
"""

import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys
import time


before, after, frozen, output = map(Path, sys.argv[1:])
root = frozen.resolve()
out = output.resolve()
out.mkdir(parents=True, exist_ok=True)
compilers = {"before": str(before.resolve()), "after": str(after.resolve())}
leaves = [f"r{int(f'{i:06b}'[::-1], 2)}" for i in range(64)]
while len(leaves) > 1:
    leaves = [f"({leaves[i]} + {leaves[i + 1]})" for i in range(0, len(leaves), 2)]
body = "".join(f"unsigned long c{i} = s + {i + 1};\n" for i in range(64))
body += "".join(f"alias_touch(&c{i});\n" for i in range(64))
body += "".join(f"unsigned long r{i} = c{i};\n" for i in range(64))
body += f"return {leaves[0]};\n"
stress = out / "pressure.c"
stress.write_text("extern void alias_touch(unsigned long *p);\n" + "".join(
    f"unsigned long alias_pressure_{i}(unsigned long s) {{\n{body}}}\n"
    for i in range(128)))
cases = {}
for mode in ("quality", "fast"):
    flag = f"-fregister-allocator={mode}"
    cases[f"pressure_{mode}"] = (["-c", flag, str(stress)], 8)
    cases[f"machine_{mode}"] = (["-c", flag, "-Isrc",
        "src/buster/lib/compiler/codegen/machine.c"], 8)
cases["unity_fast"] = (["-fregister-allocator=fast", "-Isrc", "-Ibuild/generated",
    "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g",
    "src/buster/apps/ide/ide.c", "-lm"], 6)
rows = []
for case, (arguments, pairs) in cases.items():
    artifact = out / (case + ".out")
    for sample in range(pairs + 1):
        labels = ("before", "after") if sample % 2 == 0 else ("after", "before")
        for label in labels:
            command = [compilers[label], "cc", *arguments, "-o", str(artifact)]
            with (out / "last.log").open("wb") as log:
                start = time.perf_counter_ns()
                process = subprocess.Popen(command, cwd=root, stdout=log, stderr=log)
                _, status, usage = os.wait4(process.pid, 0)
                process.returncode = os.waitstatus_to_exitcode(status)
                wall = (time.perf_counter_ns() - start) / 1e9
            if process.returncode:
                raise RuntimeError((case, label, (out / "last.log").read_text()))
            digest = hashlib.sha256()
            with artifact.open("rb") as binary:
                for chunk in iter(lambda: binary.read(65536), b""):
                    digest.update(chunk)
            if sample:
                rows.append(dict(case=case, compiler=label, sample=sample,
                    wall_s=wall, user_s=usage.ru_utime, sys_s=usage.ru_stime,
                    peak_rss_kib=usage.ru_maxrss, bytes=artifact.stat().st_size,
                    sha256=digest.hexdigest()))
    print(case, "complete", flush=True)
summary = {}
randomizer = random.Random(41)
for case in cases:
    summary[case] = {}
    for label in compilers:
        data = [r for r in rows if r["case"] == case and r["compiler"] == label]
        summary[case][label] = {
            f"median_{key}": statistics.median(r[key] for r in data)
            for key in ("wall_s", "user_s", "sys_s", "peak_rss_kib")}
        summary[case][label].update(bytes=data[0]["bytes"],
            min_wall_s=min(r["wall_s"] for r in data),
            max_wall_s=max(r["wall_s"] for r in data))
    before_rows = [r for r in rows if r["case"] == case and r["compiler"] == "before"]
    after_rows = [r for r in rows if r["case"] == case and r["compiler"] == "after"]
    ratios = [a["wall_s"] / b["wall_s"] for b, a in zip(before_rows, after_rows)]
    bootstrap = sorted(statistics.median(randomizer.choices(ratios, k=len(ratios)))
        for _ in range(10000))
    summary[case].update(median_paired_wall_ratio=statistics.median(ratios),
        paired_bootstrap_95=[bootstrap[249], bootstrap[9749]],
        identical_output=len({r["sha256"] for r in rows if r["case"] == case}) == 1)
result = dict(compilers=compilers, frozen_source_root=str(root),
    affinity=sorted(os.sched_getaffinity(0)), summary=summary, samples=rows)
(out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(summary, indent=2))
