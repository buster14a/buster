#!/usr/bin/env python3
"""Paired baseline/candidate compile timing for one or more workloads.

Each workload is compiled by the baseline compiler and the candidate compiler
in alternating order (B C, C B, B C, ...) for one warmup pair and N measured
pairs. Every child runs pinned to one CPU with `taskset`; wall time and
user+sys CPU time come from wait4. Output objects are hashed on every run and
must be identical across all runs of both compilers for the workload to count.

The report gives, per workload: the separate medians of each compiler, the
median of the per-pair candidate/baseline ratios, and the min/max of those
ratios. These are descriptive observations on a shared hosted VM, not an
acceptance verdict. Operation counts are measured separately by the work
ledger; this script only times.
"""

import argparse
import hashlib
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def timed_run(argv, cwd, cpu):
    command = ["taskset", "-c", str(cpu)] + argv if cpu is not None else argv
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    _, status, usage = os.wait4(process.pid, 0)
    wall = time.monotonic() - started
    stderr = process.stderr.read().decode(errors="replace")
    return {"status": status, "wall": wall, "cpu": usage.ru_utime + usage.ru_stime, "maxrss_kb": usage.ru_maxrss, "stderr": stderr[-2000:]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--workloads", required=True, help="JSON file: list of {name, cwd, argv (without -o), output_suffix}")
    parser.add_argument("--pairs", type=int, default=12)
    parser.add_argument("--cpu", type=int, default=None, help="CPU to pin children to (taskset); omit for no pinning")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    workloads = json.loads(Path(args.workloads).read_text())
    compilers = {"baseline": str(Path(args.baseline).resolve()), "candidate": str(Path(args.candidate).resolve())}
    report = {"pairs": args.pairs, "cpu": args.cpu, "compilers": {k: {"path": v, "sha256": sha256(v)} for k, v in compilers.items()}, "workloads": []}
    failures = 0
    for workload in workloads:
        samples = {"baseline": [], "candidate": []}
        hashes = {"baseline": set(), "candidate": set()}
        rows = []
        for pair in range(args.pairs + 1):
            order = ["baseline", "candidate"] if pair % 2 == 0 else ["candidate", "baseline"]
            for subject in order:
                out = output / f"{workload['name']}-{subject}{workload.get('output_suffix', '.o')}"
                run = timed_run([compilers[subject]] + workload["argv"] + ["-o", str(out.resolve())], workload["cwd"], args.cpu)
                if run["status"] != 0 or not out.exists():
                    failures += 1
                    print(f"FAIL {workload['name']} {subject} pair={pair} status={run['status']}\n{run['stderr']}", file=sys.stderr)
                    continue
                hashes[subject].add(sha256(out))
                if pair > 0:
                    samples[subject].append(run)
                rows.append({"pair": pair, "subject": subject, "warmup": pair == 0, **{k: run[k] for k in ("wall", "cpu", "maxrss_kb")}})
        identical = len(hashes["baseline"]) == 1 and hashes["baseline"] == hashes["candidate"]
        if not identical:
            failures += 1
        summary = {"name": workload["name"], "identical_output": identical, "object_sha256": sorted(hashes["baseline"] | hashes["candidate"]), "rows": rows}
        for metric in ("wall", "cpu"):
            base = [s[metric] for s in samples["baseline"]]
            cand = [s[metric] for s in samples["candidate"]]
            ratios = [c / b for b, c in zip(base, cand) if b > 0]
            if base and cand and ratios:
                summary[metric] = {
                    "baseline_median": statistics.median(base),
                    "candidate_median": statistics.median(cand),
                    "baseline_min": min(base),
                    "candidate_min": min(cand),
                    "paired_ratio_median": statistics.median(ratios),
                    "paired_ratio_min": min(ratios),
                    "paired_ratio_max": max(ratios),
                }
        report["workloads"].append(summary)
        wall = summary.get("wall", {})
        cpu = summary.get("cpu", {})
        print(f"{workload['name']}: identical={identical} wall B/C median {wall.get('baseline_median', 0):.4f}/{wall.get('candidate_median', 0):.4f} "
              f"paired C/B {wall.get('paired_ratio_median', 0):.4f} [{wall.get('paired_ratio_min', 0):.4f}, {wall.get('paired_ratio_max', 0):.4f}]; "
              f"cpu B/C median {cpu.get('baseline_median', 0):.4f}/{cpu.get('candidate_median', 0):.4f} paired C/B {cpu.get('paired_ratio_median', 0):.4f}",
              flush=True)
    report["failures"] = failures
    (output / "timing.json").write_text(json.dumps(report, indent=1, sort_keys=True))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
