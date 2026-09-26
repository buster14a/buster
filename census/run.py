#!/usr/bin/env python3
"""Scaling census runner for GitHub-hosted runners.

Builds every pinned ref of plan.json twice in separate trees from a
`git worktree` of that exact commit -- a counting build
(BUSTER_BENCH_ALLOCATIONS=ON) and an uninstrumented one, both with tests off
-- plus, when requested, the base commit with census_patch.py applied. Each
experiment compiles deterministic generated C families with every listed
ref: the counting binary supplies exact ir_construction.* work counts and the
object SHA-256; the uninstrumented binary supplies repeated wall times and,
where perf_event_open is available, user-space instruction counts. Real
workloads (self-host unity source, SQLite, Lua, cJSON) run the same way.

plan["census_refs"] names further refs to build with census_patch.py
--lenient; experiments and workloads address them as "NAME@census".

usage: run.py PLAN.json OUTDIR
"""
import concurrent.futures
import hashlib
import importlib.util
import json
import os
import platform
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def run(argv, cwd=None, log=None, check=True):
    process = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    if log:
        with open(log, "a") as handle:
            handle.write("$ " + " ".join(argv) + "\n" + process.stdout + process.stderr + f"[exit {process.returncode}]\n")
    if check and process.returncode:
        raise SystemExit(f"command failed ({process.returncode}): {' '.join(argv)}\n{process.stderr[-2000:]}")
    return process


def build(repo, out, name, commit, variant, lenient):
    source = os.path.join(out, "src", f"{name}-{variant}")
    log = os.path.join(out, "logs", f"build-{name}-{variant}.log")
    if os.path.exists(source):
        shutil.rmtree(source)
    run(["git", "-C", repo, "worktree", "add", "--detach", source, commit], log=log)
    tree = run(["git", "-C", source, "rev-parse", "HEAD^{tree}"]).stdout.strip()
    if variant == "census":
        run([sys.executable, os.path.join(HERE, "census_patch.py"), source] + (["--lenient"] if lenient else []), log=log)
    driver = os.path.join(out, "tmp", f"driver-{name}-{variant}")
    run(["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-fwrapv",
         "-fno-strict-aliasing", "-funsigned-char", "build.c", "-o", driver], cwd=source, log=log)
    counting = "ON" if variant in ("count", "census") else "OFF"
    run([driver, "generate", "--cc", "clang", "--config", "Release", "--no-sanitize", "--no-fuzz", "--no-lto",
         "--linker", "DEFAULT", "--", f"-DBUSTER_BENCH_ALLOCATIONS={counting}", "-DBUSTER_INCLUDE_TESTS=OFF"],
        cwd=source, log=log)
    run([driver, "build", "--config", "Release", "-t", "ide"], cwd=source, log=log)
    binary = os.path.join(out, "bin", f"{name}-{variant}")
    shutil.copy2(os.path.join(source, "build", "Release", "ide"), binary)
    compile_commands = os.path.join(source, "build", "compile_commands.json")
    if os.path.exists(compile_commands):
        shutil.copy2(compile_commands, os.path.join(out, "logs", f"compile_commands-{name}-{variant}.json"))
    return {"name": name, "variant": variant, "commit": commit, "tree": tree, "binary": binary,
            "binary_sha256": sha256(binary), "source": source}


def metrics(path):
    values = {}
    if os.path.exists(path):
        for line in open(path):
            key, _, value = line.strip().partition("=")
            if key.startswith("ir_construction."):
                try:
                    values[key[len("ir_construction."):]] = int(value)
                except ValueError:
                    pass
    return values


def icount_argv(out):
    helper = os.path.join(out, "tmp", "icount")
    return [helper] if os.path.exists(helper) else []


def compile_once(binary, argv_tail, out, tag, with_metrics, counter):
    work = os.path.join(out, "work")
    obj = os.path.join(work, tag + ".o")
    metric_path = os.path.join(work, tag + ".metrics")
    for stale in (obj, metric_path):
        if os.path.exists(stale):
            os.remove(stale)
    argv = [binary, "cc"] + argv_tail + ["-o", obj]
    if with_metrics:
        argv.append(f"-fsource-metrics={metric_path}")
    if counter:
        argv = icount_argv(out) + argv
    start = time.monotonic()
    process = subprocess.run(argv, capture_output=True, text=True)
    elapsed = time.monotonic() - start
    instructions = None
    for line in process.stderr.splitlines():
        if line.startswith("instructions="):
            value = line.split("=", 1)[1]
            instructions = int(value) if value.isdigit() else None
    return {
        "status": process.returncode,
        "seconds": round(elapsed, 6),
        "instructions": instructions,
        "stderr_tail": "\n".join(line for line in process.stderr.splitlines() if not line.startswith("instructions="))[-600:],
        "object_sha256": sha256(obj) if os.path.exists(obj) else None,
        "counters": metrics(metric_path) if with_metrics else None,
    }


def load_generator(path):
    spec = importlib.util.spec_from_file_location(os.path.basename(path)[:-3], path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    plan = json.load(open(sys.argv[1]))
    out = os.path.abspath(sys.argv[2])
    for directory in ("src", "bin", "logs", "work", "tmp", "inputs"):
        os.makedirs(os.path.join(out, directory), exist_ok=True)
    repo = run(["git", "rev-parse", "--show-toplevel"]).stdout.strip()
    subprocess.run(["cc", "-O2", "-o", os.path.join(out, "tmp", "icount"), os.path.join(HERE, "icount.c")], check=False)
    host = {
        "platform": platform.platform(), "machine": platform.machine(),
        "cpu": next((line.split(":", 1)[1].strip() for line in open("/proc/cpuinfo") if line.startswith("model name")), None),
        "cpus": os.cpu_count(),
        "clang": run(["clang", "--version"]).stdout.splitlines()[0],
        "cmake": run(["cmake", "--version"]).stdout.splitlines()[0],
        "ninja": run(["ninja", "--version"]).stdout.strip(),
        "census_head": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
    }
    refs = dict(plan["refs"])
    jobs = []
    for name, commit in refs.items():
        for variant in plan.get("variants", ["count", "plain"]):
            jobs.append((name, commit, variant))
    if plan.get("census_ref"):
        jobs.append(("census", refs[plan["census_ref"]], "census"))
    for name in plan.get("census_refs", []):
        jobs.append((name, refs[name], "census"))
    builds = {}
    prebuilt = os.environ.get("CENSUS_PREBUILT")
    if prebuilt:
        # Local dry runs reuse already-built binaries: {"base-count": {"binary", "source"}, ...}.
        for key, value in json.load(open(prebuilt)).items():
            name, _, variant = key.rpartition("-")
            builds[key] = {"name": name, "variant": variant, "commit": refs.get(name, refs.get(plan.get("census_ref"), "")),
                           "tree": "prebuilt", "binary": value["binary"], "binary_sha256": sha256(value["binary"]),
                           "source": value["source"]}
        jobs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=plan.get("build_jobs", 3)) as pool:
        futures = {pool.submit(build, repo, out, name, commit, variant, variant == "census" and name != "census"): (name, variant)
                   for name, commit, variant in jobs}
        for future in concurrent.futures.as_completed(futures):
            name, variant = futures[future]
            builds[f"{name}-{variant}"] = future.result()
            print("built", name, variant, builds[f"{name}-{variant}"]["binary_sha256"], flush=True)
    results = open(os.path.join(out, "results.jsonl"), "w")

    def emit(record):
        line = json.dumps(record, sort_keys=True, separators=(",", ":"))
        results.write(line + "\n")
        results.flush()
        # The job log is the durable copy when artifact storage is unreachable:
        # drop only zero counters there, and keep every nonzero one.
        compact = json.loads(line)
        for key in ("count",):
            if isinstance(compact.get(key), dict) and compact[key].get("counters"):
                compact[key]["counters"] = {k: v for k, v in compact[key]["counters"].items() if v}
        print("CENSUS_JSON " + json.dumps(compact, sort_keys=True, separators=(",", ":")), flush=True)

    emit({"kind": "host", **host})
    for key, value in sorted(builds.items()):
        emit({"kind": "build", **{k: v for k, v in value.items() if k not in ("binary", "source")}})
    repeats = plan.get("timing_repeats", 3)
    for experiment in plan.get("experiments", []):
        generator = load_generator(os.path.join(HERE, experiment["generator"]))
        for row in experiment["grid"]:
            source = os.path.join(out, "inputs", f"{experiment['name']}-" + "-".join(str(p) for p in row) + ".c")
            with open(source, "w") as handle:
                handle.write(generator.emit(*row))
            source_hash = sha256(source)
            for flags in experiment.get("flag_sets", [["-g0", "-c"]]):
                for ref in experiment["refs"]:
                    tag = f"{experiment['name']}-{'-'.join(str(p) for p in row)}-{ref}-{'_'.join(f.strip('-') for f in flags)}"
                    record = {"kind": "experiment", "experiment": experiment["name"], "row": row, "flags": flags,
                              "ref": ref, "source_sha256": source_hash}
                    census = ref == "census" or ref.endswith("@census")
                    count_key = "census-census" if ref == "census" else f"{ref[:-len('@census')]}-census" if census else f"{ref}-count"
                    counted = compile_once(builds[count_key]["binary"], flags + [source], out, tag.replace("@", "_") + "-count", True, False)
                    record["count"] = counted
                    timings = []
                    timed_refs = repeats if not census and "plain" in plan.get("variants", ["count", "plain"]) else 0
                    for repeat in range(timed_refs):
                        timed = compile_once(builds[f"{ref}-plain"]["binary"], flags + [source], out, tag + "-plain", False, True)
                        timings.append({k: timed[k] for k in ("status", "seconds", "instructions", "object_sha256")})
                    record["plain"] = timings
                    emit(record)
                    print(experiment["name"], row, ref, flags, counted["status"],
                          {k: v for k, v in (counted["counters"] or {}).items() if k in experiment.get("show", [])},
                          [t["seconds"] for t in timings], flush=True)
    for workload in plan.get("workloads", []):
        argv_tail = [arg.replace("$BASE_SRC", builds[f"{plan['workload_source']}-count"]["source"]).replace("$INPUTS", os.environ.get("CENSUS_INPUTS", ""))
                     for arg in workload["argv"]]
        for ref in workload["refs"]:
            census = ref == "census" or ref.endswith("@census")
            variants = ["census"] if census else ["count"]
            for variant in variants:
                key = "census-census" if ref == "census" else f"{ref[:-len('@census')]}-census" if census else f"{ref}-{variant}"
                binary = builds[key]["binary"]
                tag = f"workload-{workload['name']}-{ref.replace('@', '_')}-{variant}"
                counted = compile_once(binary, argv_tail, out, tag, True, False)
                record = {"kind": "workload", "workload": workload["name"], "ref": ref, "variant": variant,
                          "argv": argv_tail, "count": counted}
                timings = []
                if not census and "plain" in plan.get("variants", ["count", "plain"]):
                    for repeat in range(workload.get("timing_repeats", repeats)):
                        timed = compile_once(builds[f"{ref}-plain"]["binary"], argv_tail, out, tag + "-plain", False, True)
                        timings.append({k: timed[k] for k in ("status", "seconds", "instructions", "object_sha256")})
                record["plain"] = timings
                emit(record)
                print(workload["name"], ref, variant, counted["status"], counted["object_sha256"], [t["seconds"] for t in timings], flush=True)
    results.close()


if __name__ == "__main__":
    main()
