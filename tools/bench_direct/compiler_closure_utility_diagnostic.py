#!/usr/bin/env python3
"""Fixed hosted diagnostic data provider; it never launches or measures a process.

The native fixture owns builds, phase containment and all production receipts.
This writer supplies the complete historical lab/corpus data population with
tiny fixed values, marks every JSON unqualified, and refuses hardware admission.
"""
from __future__ import annotations

import argparse
import csv
import datetime
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

MARKER = b"BUSTER_COMPILER_CLOSURE_UTILITY_DIAGNOSTIC_ONLY_V1\n"
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
    if any(key.startswith("BQ_") and not (key == "BQ_REQUIRE_DISTINCT_GROUP" and value == "1") for key,value in os.environ.items()):
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
    if not root.is_absolute() or root.resolve() != root or (root / ".compiler-utility-fixture").read_bytes() != MARKER:
        raise ValueError("diagnostic provider requires the private native fixture checkout")


def output_directory(path, created):
    if not path.is_absolute() or path.parent.resolve() != path.parent:
        raise ValueError("diagnostic output is not canonical")
    if created:
        if not path.exists():
            path.mkdir()
        if not path.is_dir() or path.is_symlink() or any(path.iterdir()):
            raise ValueError("diagnostic lab output must be fresh")
    else:
        path.mkdir()


def compare(args, cpuinfo, cpu_model):
    root, output = pathlib.Path(args.repo_root), pathlib.Path(args.output)
    check_root(root)
    output_directory(output, True)
    baseline, candidate = binary(pathlib.Path(args.baseline)), binary(pathlib.Path(args.candidate))
    revision = (root / ".git/HEAD").read_text().strip()
    if len(revision) != 40 or any(character not in "0123456789abcdef" for character in revision):
        raise ValueError("diagnostic ordinary source must be detached")
    config = {"command": _lab.shell_join(["IDE"] + _lab.DEFAULT_COMPILE + ["-o", "OUT"]),
              "repo_root": str(root), "cpu": 2, "perf": "perf", "pairs": None, "target_minutes": 10.0,
              "warmups": 1, "seed": 20261003, "profile_steps": [], "sudo": False,
              "require_identical_output": args.require_identical_output, "extra": [],
              "canonical_inline_pair": False, "extra_by_variant": {"a": [], "b": []},
              "fresh_copy": True, "min_effect_percent": 0.5}
    reason = ("--target-minutes 10: fixed hosted diagnostic population of 16 pairs; "
              "no hardware measurement or adaptive performance qualification")
    plan = {"pairs": PAIRS, "order": "ABBA", "fresh_copy": True, "reason": reason}
    variants = {side: {"role": role, "ide": item["path"], "sha256": item["sha256"], "size_bytes": item["bytes"]}
                for side, role, item in zip(("a", "b"), ("baseline", "candidate"), (baseline, candidate))}
    unavailable = "fixed diagnostic provider did not collect hardware counters or compiler phase metrics"
    phase_metrics = {"enabled": False, "reason": unavailable}
    raw = {"version": 1, "mode": "compare", "config": config, "plan": plan, "variants": variants,
           "phase_metrics": phase_metrics, "outputs_identical": True,
           "steps": {name: {"status": "ok", "note": "fixed diagnostic data", "elapsed_s": 0.0}
                     for name in ("env", "prepare", "timed")}}
    (output / "pairs").mkdir()
    pairs = []
    ratio = 1.0 if args.require_identical_output else 1.021
    for index in range(1, PAIRS + 1):
        order = _lab.abba_order(index)
        for side in order.lower():
            span = 2e-8 * (ratio if side == "b" else 1.0)
            pairs.append({"pair": index, "order": order, "variant": side, "exit": 0, "identical": True,
                          "span_s": span, "maxrss_bytes": None, "harness_rss_bytes": None,
                          "wrapper_rss_bytes": None, "cpu_s": None, "counters": False, **DIAGNOSTIC})
            stem = output / "pairs" / f"{index:04d}-{side}"
            # Genuine absence is NA in the production parser, not an invented
            # counter/phase zero. The fixed span is explicitly diagnostic.
            stem.with_suffix(".csv").write_bytes(b"")
            stem.with_suffix(".ccmetrics").write_bytes(b"")
            stem.with_suffix(".log").write_text("fixed diagnostic data; no compiler measurement\n")
            stem.with_suffix(".err").write_bytes(b"")
    for side in ("a", "b"):
        directory = output / side
        directory.mkdir()
        (directory / "env").mkdir()
        write_json(directory / "env" / "env.json", {"cpu_model": cpu_model, "git_revision": revision,
                                                     "git_dirty_files": []})
        # This is retained data only; no lscpu subprocess is launched.
        (directory / "env" / "lscpu.txt").write_text("diagnostic CPU source: /proc/cpuinfo\n" + cpuinfo)
        (directory / "env" / "cpuinfo.txt").write_text(cpuinfo)
        variant = variants[side]
        own_config = {"command": _lab.shell_join([variant["ide"]] + _lab.DEFAULT_COMPILE + ["-o", "OUT"]),
                      "cpu": 2, "perf": "perf", "repo_root": str(root), "ide": variant["ide"],
                      "role": variant["role"], "extra": [], "fresh_copy": True}
        write_json(directory / "lab.json", {"version": 1, "config": own_config,
            "capabilities": {"perf_stat": {"usable": False, "reason": unavailable},
                             "metrics_out": False, "metrics_out_measured": False, "source_metrics": False,
                             "wrapper_rss_bytes": None},
            "collection": {"metrics_out": False}})
        for name in ("commands.log", "wrapper-rss.log", "source-run.log", "metrics-probe.log", "warmup-0.log"):
            (directory / name).write_text("fixed diagnostic data; no measurement command was launched\n")
    write_json(output / "compare.json", raw)
    (output / "pairs.json").write_bytes(encode(pairs))
    # This trusted production formatter reads data only here: no references or
    # profile captures exist, so it launches no process. It computes every
    # metric, availability fact, label, warning and inference from the raw data.
    write_json(output / "summary.json", _lab.compare_summary(str(output)))


def corpus(args, cpuinfo, cpu_model):
    root, output = pathlib.Path.cwd().resolve(), pathlib.Path(args.output)
    check_root(root)
    output_directory(output, False)
    baseline, candidate = binary(pathlib.Path(args.baseline)), binary(pathlib.Path(args.candidate))
    profile = compiler_receipt.THROUGHPUT_PROFILE
    jobs = [(name, mode) for name in profile["workloads"] for mode in profile["modes"]]
    if len(jobs) != 12:
        raise ValueError("diagnostic historical corpus cell population changed")
    (output / "inputs").mkdir()
    (output / "artifacts").mkdir()
    source = b"/* fixed diagnostic fixture; no measured compilation */\nint main(void) { return 0; }\n"
    source_lines, source_functions = source.count(b"\n"), 1
    job_metadata = []
    for job, (name, mode) in enumerate(jobs):
        path = output / "inputs" / (name + ".c")
        path.write_bytes(source)
        job_metadata.append({"job": job, "name": name, "mode": mode, "artifact": "object", "source": str(path),
                             "sha256": hashlib.sha256(source).hexdigest(), "bytes": len(source),
                             "physical_lines": source_lines, "defined_functions": source_functions})
    wall_a, wall_b = 2e-8, 2e-8 * 1.21
    rss = 4096
    alpha = 0.01 / (len(jobs) * 2)
    # The fixed 1.21 ratio exceeds the relative margin but its tiny absolute
    # difference does not exceed the original 2ms margin. No regression is
    # detected by the exact paired sign test; the synthetic sum stays bounded
    # below the real provider phase duration without invented measurement time.
    wall_ratio = wall_b / wall_a
    metric_names = ("wall_seconds", "cpu_seconds", "peak_rss_bytes", "cycles", "instructions", "branches",
                    "branch_misses", "cache_references", "cache_misses", "arena_calls", "arena_bytes",
                    "output_bytes", "lines_per_second", "functions_per_second", "bytes_per_second",
                    "minor_faults", "major_faults", "voluntary_context_switches", "involuntary_context_switches")
    output_bytes = b"fixed diagnostic output\n"
    digest = hashlib.sha256(output_bytes).hexdigest()
    medians = {name: [None, None] for name in metric_names}
    medians.update(wall_seconds=[wall_a, wall_b], cpu_seconds=[0.0, 0.0], peak_rss_bytes=[rss, rss],
                   output_bytes=[len(output_bytes), len(output_bytes)],
                   lines_per_second=[source_lines / wall_a, source_lines / wall_b],
                   functions_per_second=[source_functions / wall_a, source_functions / wall_b],
                   bytes_per_second=[len(source) / wall_a, len(source) / wall_b])
    comparisons = []
    for name, mode in jobs:
        tests = []
        for metric in ("wall_seconds", "peak_rss_bytes"):
            regression = False
            ratio = wall_ratio if metric == "wall_seconds" else 1.0
            for number in range(2):
                tests.append({"metric": metric, "round": number, "median_ratio": ratio,
                              "ci_low": ratio, "ci_high": ratio, "baseline_relative_mad": 0.0,
                              "candidate_relative_mad": 0.0, "margin_exceedances": 20 if regression else 0,
                              "pairs": 20, "p_value": 2.0 ** -20 if regression else 1.0,
                              "regression": regression})
        comparisons.append({"name": name + "/" + mode, "medians": dict(medians), "tests": tests,
                            "decision": "inconclusive"})
    summary = {"schema": 2, "guard_enabled": True, "family_alpha": 0.01, "per_test_alpha": alpha,
               "comparisons": comparisons, "telemetry": [], "confirmed_regressions": 0,
               "inconclusive_cases": len(jobs), "valid": True}
    retained_environment = ("PATH", "CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "CPATH", "C_INCLUDE_PATH",
                            "LIBRARY_PATH", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "BUSTER_SINGLE_THREADED",
                            "BUSTER_TEST_JOBS", "ASAN_OPTIONS", "UBSAN_OPTIONS", "LD_PRELOAD",
                            "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion")
    metadata = {"schema": 2, "created_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                "profile": "ci", "seed": 20260907, "scale": 1, "input_schema": 1,
                "pairs_per_round": 20, "rounds": 2, "warmups": 2, "cpu": 2,
                "workloads": list(profile["workloads"]), "host": {"cpu_model": cpu_model, **DIAGNOSTIC},
                "cache_policy": "warm-filesystem-new-process", "clock": "monotonic", "flags": [],
                "allocation_compilers": [], "environment": {name: os.environ.get(name) for name in retained_environment},
                "wall_scope": "process launch through wait completion; no PMU or allocation instrumentation",
                "cpu_scope": "OS child user plus system CPU time",
                "rss_scope": "OS per-child peak; not concurrent tree sum",
                "pmu_scope": "separate diagnostic replay; user-space inherited hardware events; >=90% running required",
                "allocation_scope": "separate explicitly instrumented compiler replay; arena calls and requested bytes",
                "process_diagnostics_scope": "Linux wait4 child usage; faults and context switches, not PMU events; copied after timing; other platforms unavailable; diagnostic only",
                "compiler_provenance": [dict(item, revision_label=revision)
                    for item, revision in zip((baseline, candidate), (args.baseline_id, args.candidate_id))],
                "jobs": job_metadata}
    write_json(output / "summary.json", summary)
    (output / "summary.md").write_text("# Compiler throughput diagnostic fixture\n\n"
        "DIAGNOSTIC-UNQUALIFIED: fixed data, no hardware measurements.\n"
        "12 inconclusive corpus cells; no confirmed regressions.\n")
    write_json(output / "metadata.json", metadata)
    (output / "cpuinfo.txt").write_text(cpuinfo)
    sample_stream, telemetry_stream = io.StringIO(), io.StringIO()
    sample_writer = csv.writer(sample_stream, lineterminator="\n")
    telemetry_writer = csv.writer(telemetry_stream, lineterminator="\n")
    sample_writer.writerow(RAW_HEADER)
    telemetry_writer.writerow(RAW_HEADER)
    commands, capabilities = [], []

    def data_command(job, variant, identity):
        name, mode = jobs[job]
        path = job_metadata[job]["source"]
        metrics = output / "artifacts" / (identity + ".metrics")
        artifact = output / "artifacts" / (name + "-" + mode + "-" + str(variant) + ".o")
        compiler = (baseline, candidate)[variant]["path"]
        argv = [compiler, "cc", "-g0", "-O0", "-fsource-metrics=" + str(metrics), "-c",
                "-fregister-allocator=" + mode, path, "-o", str(artifact)]
        commands.append({"sample": identity, "cwd": str(root), "argv": argv, **DIAGNOSTIC})
        capabilities.append({"sample": identity, "exit_code": 0, "signal": 0, "timeout": 0, "launch_error": 0,
            "pmu": {name: {"errno": 0, "running_fraction": None} for name in
                    ("cycles", "instructions", "branches", "branch_misses", "cache_references", "cache_misses")},
            **DIAGNOSTIC})
        (output / "artifacts" / (identity + ".log")).write_text("fixed diagnostic data; command was not executed\n")
        (output / "artifacts" / (identity + ".err")).write_bytes(b"")
        metrics.write_text(f"lexed.translated_bytes={len(source)}\nlexed.translated_lines={source_lines}\n")

    for job in range(len(jobs)):
        for warmup in range(2):
            for variant in range(2):
                data_command(job, variant, f"warmup-j{job}-w{warmup}-v{variant}")
    # Reconstruct the native fixed PRNG ordering as data only. This does not
    # execute or schedule a process: POSITION records each synthetic paired
    # row's native 0/1 position and the seeded AB/BA block layout.
    random_state = metadata["seed"]

    def data_random():
        nonlocal random_state
        random_state ^= (random_state << 13) & 0xffffffff
        random_state ^= random_state >> 17
        random_state ^= (random_state << 5) & 0xffffffff
        random_state &= 0xffffffff
        return random_state

    block_first = [0] * len(jobs)
    for number in range(2):
        for pair in range(20):
            order = list(range(len(jobs)))
            for job in range(len(jobs)):
                if not pair % 2:
                    block_first[job] = data_random() & 1
            for remaining in range(len(jobs), 1, -1):
                swap = data_random() % remaining
                order[remaining - 1], order[swap] = order[swap], order[remaining - 1]
            for job in order:
                first = block_first[job] ^ (pair % 2)
                for position in range(2):
                    variant = first ^ position
                    wall = (wall_a, wall_b)[variant]
                    row = [number, pair, position, variant, job, wall, 0.0, 0.0, rss,
                           *["NA"] * 8, len(output_bytes), len(source), source_lines, source_functions,
                           digest, *["NA"] * 4]
                    if len(row) != len(RAW_HEADER):
                        raise ValueError("diagnostic corpus raw row shape changed")
                    sample_writer.writerow(row)
                    data_command(job, variant, f"timing-r{number}-p{pair}-j{job}-v{variant}")
    if len(commands) != 1008 or len(capabilities) != 1008:
        raise ValueError("diagnostic complete timing/warmup command population changed")
    # No PMU/allocation replay is claimed: retain canonical header-only CSV.
    (output / "samples.csv").write_text(sample_stream.getvalue())
    (output / "telemetry.csv").write_text(telemetry_stream.getvalue())
    (output / "jobs.tsv").write_text("".join(f"{job}\t{name}/{mode}\n" for job, (name, mode) in enumerate(jobs)))
    (output / "commands.jsonl").write_bytes(b"".join(encode(item) for item in commands))
    (output / "capabilities.jsonl").write_bytes(b"".join(encode(item) for item in capabilities))
    names = ("samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl", "capabilities.jsonl")
    completion = f"schema=2 jobs={len(jobs)} pairs=20 rounds=2 guard=1\n"
    completion += "".join(hashlib.sha256((output / name).read_bytes()).hexdigest() + " " + name + "\n" for name in names)
    (output / "complete.txt").write_text(completion)
    return 0


def provider_main():
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
    args = parser.parse_args(sys.argv[1:] if sys.argv[1] != "--emit-corpus" else sys.argv[2:])
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
        return corpus(args, cpuinfo, cpu_model)
    return 0



def ordinary_main():
    """Test-only entry to the unchanged ordinary main/writers, not a scheduler."""
    host()
    import compiler_compare as compare
    from unittest import mock
    actual_writer = compare.write_receipt
    def diagnostic_receipt(receipt, evidence):
        receipt.update(DIAGNOSTIC)
        return actual_writer(receipt, evidence)
    # The protected physical path remains untouched. This private file rejects
    # that host and all authority; candidate builds/scheduling stay production.
    with mock.patch.object(compare, "host_problem", return_value=None), \
            mock.patch.object(compare, "write_receipt", side_effect=diagnostic_receipt):
        return compare.main(sys.argv[1:])


if __name__ == "__main__":
    try:
        if len(sys.argv)>1 and sys.argv[1] in ("compare", "run", "--emit-corpus"):
            sys.exit(provider_main())
        sys.exit(ordinary_main())
    except (OSError, ValueError, KeyError) as error:
        print("diagnostic provider refused: " + str(error), file=sys.stderr)
        sys.exit(1)
