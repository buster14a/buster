#!/usr/bin/env python3
"""Fixed hosted diagnostic data provider; it never launches or measures a process.

The native fixture owns builds, phase containment and all production receipts.
This writer supplies the complete historical lab/corpus data population with
tiny fixed values, marks every JSON unqualified, and refuses hardware admission.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import pathlib
import stat
import sys

import compiler_preparation as preparation
import compiler_receipt
from sampling_qualification_receipt import _lab

MARKER = b"BUSTER_COMPILER_PREPARATION_DIAGNOSTIC_ONLY_V1\n"
DIAGNOSTIC = {"diagnostic_fixture": True, "qualification_state": "unqualified"}
PAIRS = 16
RAW_HEADER = ("round,pair,order,variant,job,wall_seconds,user_seconds,system_seconds,"
              "peak_rss_bytes,cycles,instructions,branches,branch_misses,cache_references,"
              "cache_misses,arena_calls,arena_bytes,output_bytes,source_bytes,source_lines,"
              "source_functions,output_sha256,minor_faults,major_faults,"
              "voluntary_context_switches,involuntary_context_switches").split(",")


def encode(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()


def write_json(path, value):
    path.write_bytes(encode(dict(value, **DIAGNOSTIC)))


def host():
    raw = pathlib.Path("/proc/cpuinfo").read_text()
    models = [line.partition(":")[2].strip() for line in raw.splitlines() if line.startswith("model name")]
    if not models or any("AMD Ryzen 7 9700X" in model for model in models):
        raise ValueError("diagnostic provider refuses the approved physical benchmark host")
    if any(key.startswith("BQ_PREPARATION_") for key in os.environ):
        raise ValueError("diagnostic provider refuses preparation admission")
    return raw, models[0]


def binary(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= 16 * 1024 * 1024:
            raise ValueError("diagnostic frozen binary is not a bounded regular file")
        raw = bytearray()
        while len(raw) <= before.st_size:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                break
            raw.extend(chunk)
        after = os.fstat(descriptor)
        if len(raw) != before.st_size or (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != \
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise ValueError("diagnostic frozen binary changed during observation")
    finally:
        os.close(descriptor)
    return {"path": str(path), "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}


def check_root(root):
    if not root.is_absolute() or root.resolve() != root or (root / ".compiler-preparation-fixture").read_bytes() != MARKER:
        raise ValueError("diagnostic provider requires the private native fixture checkout")


def output_directory(path, created):
    if not path.is_absolute() or path.parent.resolve() != path.parent:
        raise ValueError("diagnostic output is not canonical")
    if created:
        if not path.is_dir() or path.is_symlink() or any(path.iterdir()):
            raise ValueError("diagnostic lab output must be fresh")
    else:
        path.mkdir()


def compare(args, cpuinfo, cpu_model):
    root, output = pathlib.Path(args.repo_root), pathlib.Path(args.output)
    check_root(root)
    output_directory(output, True)
    baseline, candidate = binary(pathlib.Path(args.baseline)), binary(pathlib.Path(args.candidate))
    base_receipt = json.loads((pathlib.Path(args.baseline).parent.parent / "baseline.binary.json").read_bytes())
    if base_receipt["sha256"] != baseline["sha256"]:
        raise ValueError("diagnostic baseline freeze receipt disagrees with actual bytes")
    config = {"command": preparation.WORKLOAD_COMMAND, "repo_root": str(root), "cpu": 2, "perf": "perf",
              "pairs": None, "target_minutes": 10.0, "warmups": 1, "seed": 20261003,
              "profile_steps": [], "sudo": False, "require_identical_output": args.require_identical_output,
              "extra": [], "canonical_inline_pair": False, "extra_by_variant": {"a": [], "b": []},
              "fresh_copy": True, "min_effect_percent": 0.5}
    reason = ("--target-minutes 10: 0.000000040 s per pair (median of 2 pilot pairs), 0.0 min elapsed, "
              "profile steps about 0 compile-equivalents (0.0 min) -> 16 pairs "
              "(clamped to 10..1000, whole ABBA blocks)")
    plan = {"pairs": PAIRS, "order": "ABBA", "fresh_copy": True, "reason": reason}
    variants = {side: {"role": role, "ide": item["path"], "sha256": item["sha256"], "size_bytes": item["bytes"]}
                for side, role, item in zip(("a", "b"), ("baseline", "candidate"), (baseline, candidate))}
    raw = {"version": 1, "mode": "compare", "config": config, "plan": plan,
           "variants": variants, "steps": {name: {"status": "ok"} for name in ("env", "prepare", "timed")}}
    pairs, grouped = [], []
    ratio = 1.0 if args.require_identical_output else 1.021
    for index in range(1, PAIRS + 1):
        order = "AB" if index % 2 else "BA"
        spans = {"a": 2e-8, "b": 2e-8 * ratio}
        for side in order.lower():
            pairs.append(dict(pair=index, order=order, variant=side, exit=0, identical=True,
                              span_s=spans[side], **DIAGNOSTIC))
        grouped.append({"pair": index, "order": order,
                        "metrics_a": {"wall": spans["a"]}, "metrics_b": {"wall": spans["b"]}})
    wall = _lab.compare_series([(item["metrics_a"]["wall"], item["metrics_b"]["wall"]) for item in grouped],
                               "s", "lower", 20261003, time_metric=True, floor=0.005)
    summary = {"schema": compiler_receipt.LAB_SCHEMA, "command": preparation.WORKLOAD_COMMAND, "repo_root": str(root),
               "cpu": 2, "host": {"cpu_model": cpu_model, "git_revision": base_receipt["commit"], **DIAGNOSTIC},
               "steps": {"env": "ok", "prepare": "ok", "timed": "ok"},
               "plan": dict(plan, seed=20261003, confidence=0.95, bootstrap_resamples=2000, complete_pairs=PAIRS),
               "verdict": dict(wall, metric="wall", min_effect_percent=0.5), "metrics": {"wall": wall},
               "checks": _lab.compare_checks(grouped), "warnings": [], "outputs_identical": True}
    for role, identity in zip(("baseline", "candidate"), (baseline, candidate)):
        summary[role] = {"sha256": identity["sha256"], "runs": PAIRS, "failed": 0,
                         "deterministic": True, "identical_runs": PAIRS}
    write_json(output / "compare.json", raw)
    write_json(output / "summary.json", summary)
    (output / "pairs.json").write_bytes(encode(pairs))
    (output / "pairs").mkdir()
    for item in pairs:
        stem = output / "pairs" / f"{item['pair']:04}-{item['variant']}"
        stem.with_suffix(".csv").write_text("metric,value\nwall," + str(item["span_s"]) + "\n")
        stem.with_suffix(".ccmetrics").write_text("diagnostic_fixture=1\nwall=" + str(item["span_s"]) + "\n")
        stem.with_suffix(".log").write_text("fixed diagnostic data; no compiler measurement\n")
        stem.with_suffix(".err").write_bytes(b"")
    for side in ("a", "b"):
        directory = output / side
        directory.mkdir()
        (directory / "env").mkdir()
        (directory / "env" / "cpuinfo.txt").write_text(cpuinfo)
        write_json(directory / "lab.json", {"version": 1, "role": variants[side]["role"], "variant": variants[side]})
        (directory / "stdout.log").write_text("fixed diagnostic data; no compiler measurement\n")
        (directory / "stderr.log").write_bytes(b"")


def corpus(args, cpuinfo, cpu_model):
    root, output = pathlib.Path.cwd().resolve(), pathlib.Path(args.output)
    check_root(root)
    output_directory(output, False)
    baseline, candidate = binary(pathlib.Path(args.baseline)), binary(pathlib.Path(args.candidate))
    profile = compiler_receipt.THROUGHPUT_PROFILE
    jobs = [(name, mode) for name in profile["workloads"] for mode in profile["modes"]]
    comparisons = [{"name": name + "/" + mode, "medians": {},
                    "tests": [{"metric": metric, "round": number, "median_ratio": 1.0, "regression": False}
                              for metric in ("wall_seconds", "peak_rss_bytes") for number in range(2)],
                    "decision": "no substantial regression detected"} for name, mode in jobs]
    summary = {"schema": 2, "guard_enabled": True, "comparisons": comparisons, "confirmed_regressions": 0,
               "inconclusive_cases": 0, "valid": True}
    metadata = {"schema": 2, "profile": "ci", "pairs_per_round": 20, "rounds": 2, "warmups": 2, "cpu": 2,
                "workloads": list(profile["workloads"]), "host": {"cpu_model": cpu_model, **DIAGNOSTIC},
                "compiler_provenance": [dict(item, revision_label=revision)
                    for item, revision in zip((baseline, candidate), (args.baseline_id, args.candidate_id))]}
    write_json(output / "summary.json", summary)
    write_json(output / "metadata.json", metadata)
    (output / "inputs").mkdir()
    (output / "artifacts").mkdir()
    sample_stream, telemetry_stream = io.StringIO(), io.StringIO()
    sample_writer, telemetry_writer = csv.writer(sample_stream, lineterminator="\n"), csv.writer(telemetry_stream, lineterminator="\n")
    sample_writer.writerow(RAW_HEADER)
    telemetry_writer.writerow(RAW_HEADER)
    commands, capabilities = [], []
    for job, (name, mode) in enumerate(jobs):
        source = "/* fixed diagnostic fixture */\nint main(void) { return 0; }\n"
        (output / "inputs" / (name + "-" + mode + ".c")).write_text(source)
        digest = hashlib.sha256(b"fixed diagnostic output\n").hexdigest()
        for number in range(2):
            for pair in range(20):
                for variant in range(2):
                    row = [number, pair, pair % 2, variant, job, 2e-8, 0, 0, 4096,
                           *["NA"] * 8, 24, len(source), 2, 1, digest, *["NA"] * 4]
                    if len(row) != len(RAW_HEADER):
                        raise ValueError("diagnostic corpus raw row shape changed")
                    sample_writer.writerow(row)
                    identity = f"r{number}-p{pair}-v{variant}-j{job}"
                    commands.append({"sample": identity, "command": ["diagnostic-provider", name, mode],
                                     "exit": 0, **DIAGNOSTIC})
                    capabilities.append({"sample": identity, "pmu": "diagnostic-unavailable", **DIAGNOSTIC})
                    (output / "artifacts" / (identity + ".log")).write_text("fixed diagnostic data\n")
                    (output / "artifacts" / (identity + ".err")).write_bytes(b"")
                    (output / "artifacts" / (identity + ".metrics")).write_text("wall_seconds=0.00000002\n")
        for repeat in range(2):
            for variant in range(2):
                telemetry_writer.writerow([0, repeat, repeat % 2, variant, job, 2e-8, 0, 0, 4096,
                    *["NA"] * 8, 24, len(source), 2, 1, digest, *["NA"] * 4])
    (output / "samples.csv").write_text(sample_stream.getvalue())
    (output / "telemetry.csv").write_text(telemetry_stream.getvalue())
    (output / "jobs.tsv").write_text("".join(f"{job}\t{name}/{mode}\n" for job, (name, mode) in enumerate(jobs)))
    (output / "commands.jsonl").write_bytes(b"".join(encode(item) for item in commands))
    (output / "capabilities.jsonl").write_bytes(b"".join(encode(item) for item in capabilities))
    names = ("samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl", "capabilities.jsonl")
    completion = f"schema=2 jobs={len(jobs)} pairs=20 rounds=2 guard=1\n"
    completion += "".join(hashlib.sha256((output / name).read_bytes()).hexdigest() + " " + name + "\n" for name in names)
    (output / "complete.txt").write_text(completion)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("compare", "run"))
    for name in ("baseline", "candidate", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--repo-root")
    parser.add_argument("--baseline-id")
    parser.add_argument("--candidate-id")
    parser.add_argument("--cpu", choices=("2",), required=True)
    parser.add_argument("--target-minutes", choices=("10",))
    parser.add_argument("--warmups", choices=("1", "2"), required=True)
    parser.add_argument("--profile", choices=("ci",))
    parser.add_argument("--mode", choices=("all",))
    parser.add_argument("--pairs", choices=("20",))
    parser.add_argument("--timeout", choices=("120",))
    parser.add_argument("--require-identical-output", action="store_true")
    args = parser.parse_args()
    cpuinfo, cpu_model = host()
    if args.command == "compare":
        if args.target_minutes != "10" or args.warmups != "1" or not args.repo_root or any(
                getattr(args, name) is not None for name in ("baseline_id", "candidate_id", "profile", "mode", "pairs", "timeout")):
            raise ValueError("diagnostic compare fixed production argv changed")
        compare(args, cpuinfo, cpu_model)
    else:
        if args.warmups != "2" or args.profile != "ci" or args.mode != "all" or args.pairs != "20" or \
                args.timeout != "120" or not args.baseline_id or not args.candidate_id or args.repo_root or args.target_minutes:
            raise ValueError("diagnostic corpus fixed production argv changed")
        corpus(args, cpuinfo, cpu_model)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        print("diagnostic provider refused: " + str(error), file=sys.stderr)
        sys.exit(1)
