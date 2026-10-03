#!/usr/bin/env python3
"""Disposable cloud-only A/B using Buster's existing measurement harness."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import time

BASE_SHA = "3d350c52f54b77048f100326d8ee7569b8cde4dd"
BUILD_DIRECTORY = "build/throughput-ci-compiler"


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def capture(argv, cwd):
    process = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, check=False)
    return {"argv": argv, "exit_code": process.returncode, "output": process.stdout}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run(argv, cwd, evidence, name, timeout):
    entry = {"name": name, "argv": [str(value) for value in argv], "cwd": str(cwd)}
    with (evidence / "orchestration.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(entry) + "\n")
    print(f"{name}: {entry['argv']}", flush=True)
    start = time.monotonic()
    with (evidence / f"{name}.log").open("wb") as stream:
        process = subprocess.run(entry["argv"], cwd=cwd, stdout=stream,
                                 stderr=subprocess.STDOUT, timeout=timeout, check=False)
    entry["exit_code"] = process.returncode
    entry["orchestration_elapsed_seconds"] = time.monotonic() - start
    write_json(evidence / f"{name}.result.json", entry)
    if process.returncode:
        raise RuntimeError(f"{name} failed with exit {process.returncode}; see retained log")


def summarize(root, evidence):
    jobs = {}
    for line in (root / "jobs.tsv").read_text(encoding="utf-8").splitlines():
        number, name = line.split("\t")
        jobs[int(number)] = name
    with (root / "samples.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    report = []
    for job, name in jobs.items():
        selected = [row for row in rows if int(row["job"]) == job]
        hashes = [{row["output_sha256"] for row in selected if int(row["variant"]) == variant}
                  for variant in range(2)]
        if len(selected) != 80 or len(hashes[0]) != 1 or hashes[0] != hashes[1]:
            raise RuntimeError(f"incomplete or byte-different A/B series: {name}")
        samples = {}
        for row in selected:
            key = (int(row["round"]), int(row["pair"]), int(row["variant"]))
            if key in samples:
                raise RuntimeError(f"duplicate timing sample: {name} {key}")
            samples[key] = row
        metrics = {}
        for metric in ("wall_seconds", "peak_rss_bytes"):
            variants = [[float(row[metric]) for row in selected if int(row["variant"]) == variant]
                        for variant in range(2)]
            ratios = [float(samples[(round_number, pair, 1)][metric]) /
                      float(samples[(round_number, pair, 0)][metric])
                      for round_number in range(2) for pair in range(20)]
            metrics[metric] = {
                "baseline_median": statistics.median(variants[0]),
                "candidate_median": statistics.median(variants[1]),
                "median_paired_ratio": statistics.median(ratios),
                "paired_change_percent": (statistics.median(ratios) - 1.0) * 100.0,
            }
        report.append({"job": job, "name": name, "samples_per_variant": 40,
                       "identical_output_sha256": next(iter(hashes[0])), "metrics": metrics})
    native = json.loads((root / "summary.json").read_text(encoding="utf-8"))
    if not native.get("valid"):
        raise RuntimeError("existing harness reported an invalid bundle")
    write_json(evidence / "paired-summary.json",
               {"evidence_class": "same-runner cloud diagnostic; all samples retained",
                "guard_enabled": False, "comparisons": report,
                "uncertainty_source": "results/summary.json per-round native median-ratio confidence intervals and MAD",
                "rss_scope": "OS per-child peak, not sum of process-tree peaks",
                "performance_claim": "no automatic speedup or equivalence claim"})
    lines = [
        "# Frozen-source cloud comparison",
        "",
        "Two fixed rounds of 20 adjacent AB/BA pairs per job, two warmups per variant; every sample retained.",
        "The existing harness pins all measured compiler children to one allowed CPU and checks immutable inputs, binaries, A/B bytes and each frozen-source stage 2 == stage 3 fixed point.",
        "This shared GitHub runner is a cloud diagnostic. Read the native per-round intervals and dispersion before judging small changes.",
        "",
        "| Job | Base / candidate wall (ms) | Paired wall change | Base / candidate peak (MiB) | Paired RSS change |",
        "|---|---:|---:|---:|---:|",
    ]
    for item in report:
        wall = item["metrics"]["wall_seconds"]
        rss = item["metrics"]["peak_rss_bytes"]
        lines.append(f"| {item['name']} | {wall['baseline_median'] * 1000:.3f} / {wall['candidate_median'] * 1000:.3f} | {wall['paired_change_percent']:+.2f}% | {rss['baseline_median'] / 1048576:.2f} / {rss['candidate_median'] / 1048576:.2f} | {rss['paired_change_percent']:+.2f}% |")
    lines.extend(["", "RSS is each compiler child's OS high-water mark. No peaks are added together.",
                  "Stage 1 measures the trusted Clang-built compilers on the same frozen unity source. Stage 2 measures their frozen-source self-built outputs.",
                  "Raw samples, commands, hashes, hardware and unavailable counters remain in the artifact.", ""])
    (evidence / "paired-summary.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--workspace", default=".")
    args = parser.parse_args()
    workspace = Path(args.workspace).resolve()
    baseline = workspace / "baseline"
    candidate = workspace / "candidate"
    evidence = workspace / "cloud-perf-evidence"
    evidence.mkdir(exist_ok=False)
    settings = {
        "baseline_sha": BASE_SHA, "candidate_sha": args.candidate_sha,
        "rounds": 2, "pairs_per_round": 20, "warmups_per_variant": 2,
        "seed": 20260907, "cpu": "auto: first allowed CPU, all child trials pinned",
        "selected_workloads": ["tiny_startup/fast", "self_host_stage1/fast", "self_host_stage2/fast"],
        "profile": "smoke", "allocator": "fast", "timeout_seconds_per_compile": 120,
        "frozen_source_root": str(baseline),
        "frozen_generated_root": str(baseline / BUILD_DIRECTORY / "generated"),
        "timed_compilers": "trusted Clang Release, BUSTER_UNITY_BUILD=OFF, BUSTER_INCLUDE_TESTS=OFF, BUSTER_BENCH_ALLOCATIONS=OFF",
        "measurement": "existing tools/throughput native fork/exec/wait4 wall and per-child OS RSS",
        "require_identical_ab_output": True,
        "filtering": "none; all unfavorable cases and every raw sample retained",
        "host": "GitHub-hosted ubuntu-26.04 only; no physical 9700X operations",
    }
    write_json(evidence / "predeclared-settings.json", settings)
    hardware = {
        "platform": platform.platform(), "uname": list(platform.uname()),
        "logical_cpus": os.cpu_count(), "allowed_cpus": sorted(os.sched_getaffinity(0)),
        "runner_context": {name: os.environ.get(name) for name in (
            "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_SHA", "GITHUB_REF",
            "RUNNER_NAME", "RUNNER_OS", "RUNNER_ARCH")},
        "tools": [capture([tool, "--version"], workspace) for tool in ("clang", "cmake", "ninja")],
    }
    write_json(evidence / "hardware.json", hardware)
    for name in ("cpuinfo", "meminfo"):
        shutil.copyfile(Path("/proc") / name, evidence / f"{name}.txt")
    revisions = {}
    for name, root, expected in (("baseline", baseline, BASE_SHA),
                                 ("candidate", candidate, args.candidate_sha)):
        commit = capture(["git", "rev-parse", "HEAD"], root)
        tree = capture(["git", "rev-parse", "HEAD^{tree}"], root)
        if commit["exit_code"] or commit["output"].strip() != expected or tree["exit_code"]:
            raise RuntimeError(f"{name} checkout is not the exact declared source")
        revisions[name] = {"commit": commit["output"].strip(), "tree": tree["output"].strip()}
    write_json(evidence / "revisions.json", revisions)
    driver = evidence / "build-driver"
    run(["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function",
         "-Wno-unused-variable", "build.c", "-o", driver],
        candidate, evidence, "bootstrap-build-driver", 180)
    # Match bench_throughput_ci_add: one candidate driver, baseline then candidate,
    # no shared build cache, and the same uninstrumented Release policy.
    for name, root in (("baseline", baseline), ("candidate", candidate)):
        run([driver, "generate", "--build-directory", BUILD_DIRECTORY, "--ci", "--cc", "clang", "--",
             "-DBUSTER_INCLUDE_TESTS=OFF", "-DBUSTER_UNITY_BUILD=OFF",
             "-DBUSTER_BENCH_ALLOCATIONS=OFF", "-DCMAKE_LINKER_TYPE=DEFAULT"],
            root, evidence, f"configure-{name}", 600)
        run([driver, "build", "--build-directory", BUILD_DIRECTORY, "--config", "Release",
             "-t", "ide", "--", "-j2"],
            root, evidence, f"build-{name}", 1200)
    binaries = [baseline / BUILD_DIRECTORY / "Release/ide",
                candidate / BUILD_DIRECTORY / "Release/ide"]
    write_json(evidence / "trusted-binaries.json",
               [{"source": name, "path": str(path), "sha256": digest(path),
                 "bytes": path.stat().st_size} for name, path in zip(("baseline", "candidate"), binaries)])
    results = evidence / "results"
    argv = [driver, "bench_throughput", "run", "--baseline", binaries[0],
            "--candidate", binaries[1], "--baseline-id", BASE_SHA,
            "--candidate-id", args.candidate_sha, "--output", results,
            "--profile", "smoke", "--workload", "tiny_startup", "--mode", "fast",
            "--pairs", "20", "--warmups", "2", "--seed", "20260907", "--timeout", "120", "--cpu", "auto",
            "--self-host-root", baseline,
            "--self-host-generated", baseline / BUILD_DIRECTORY / "generated",
            "--require-identical-output", "--no-guard"]
    run(argv, candidate, evidence, "frozen-unity-measurement", 3600)
    summarize(results, evidence)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Cloud probe failed: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
