#!/usr/bin/env python3
"""Issue #1912: matched cloud builds, actual placement census, and cache simulation.

Only a disposable GitHub-hosted checkout is accepted. Timings/cache simulation
are diagnostic; this does not admit a 9700X performance result.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


SOURCE = Path("src/buster/lib/compiler/codegen/register_allocator_fast.c")
POPULATION = re.compile(r"CACHE_SLOT_POPULATION n=(\d+) s=(\d+) k=(\d+) coalesce=(\d+) block_count=(\d+) block_candidate=(\d+)")
COLOR = re.compile(r"CACHE_SLOT_COLOR n=(\d+) s=(\d+) k=(\d+)")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--injector", type=Path, required=True)
    args = parser.parse_args()
    if os.environ.get("GITHUB_ACTIONS") != "true" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        parser.error("Requires a disposable GitHub-hosted runner")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    root = Path.cwd()
    driver = out / "driver"
    summary = {"baseline": args.baseline, "head": args.head, "acceptance": False, "builds": {}, "workloads": {}}

    def run(label, command, timeout=1200, required=True):
        started = time.perf_counter_ns()
        with (out / (label + ".log")).open("wb") as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=timeout, check=False)
        record = {"label": label, "command": [str(x) for x in command], "returncode": process.returncode,
                  "seconds": (time.perf_counter_ns() - started) / 1e9}
        with (out / "commands.jsonl").open("a") as log:
            log.write(json.dumps(record) + "\n")
        print(json.dumps(record), flush=True)
        if required and process.returncode:
            print((out / (label + ".log")).read_text(errors="replace")[-12000:], flush=True)
            raise RuntimeError(label + " failed")
        return record

    def build(label, revision, instrument=False):
        run(label + "-checkout", ["git", "checkout", "--detach", revision])
        if instrument:
            run(label + "-inject", ["python3", str(args.injector), str(SOURCE)])
            shutil.copyfile(SOURCE, out / (label + "-allocator.c"))
        run(label + "-bootstrap", ["clang", "-Isrc", "-I.", "-Wall", "-Werror", "-Wno-unused-function",
                                  "-Wno-unused-variable", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char",
                                  "build.c", "-o", str(driver)])
        run(label + "-generate", [str(driver), "generate", "--ci", "--no-sanitize", "--no-fuzz", "--no-lto",
                                 "--linker", "DEFAULT", "--", "-DBUSTER_DEBUG_INFO=OFF"])
        run(label + "-build", [str(driver), "build", "--config", "Release", "-t", "ide"])
        binary = out / (label + "-ide")
        shutil.copyfile(root / "build/Release/ide", binary)
        summary["builds"][label] = {"source_revision": revision, "binary_sha256": digest(binary), "instrumented": instrument}
        if not instrument:
            run(label + "-self-host", [str(driver), "test_self_host", "--config", "Release"])
        run(label + "-discard-overlay", ["git", "checkout", "--", str(SOURCE)])
        return binary

    run("identity", ["bash", "-c", "git rev-parse HEAD 'HEAD^{tree}'; uname -a; lscpu; clang --version; valgrind --version"])
    clean = {}
    probes = {}
    for name, revision in (("baseline", args.baseline), ("candidate", args.head)):
        clean[name] = build(name, revision)
        probes[name] = build(name + "-probe", revision, instrument=True)
    run("restore-head", ["git", "checkout", "--detach", args.head])
    shutil.copyfile(clean["candidate"], root / "build/Release/ide")
    run("machine-tests", [str(clean["candidate"]), "test", "--ci=1", "--verbose=1", "--module=machine_tests"])
    run("mode-matrix", [str(driver), "test_mode_matrix", "--config", "Release"])
    # Restore a clean build before mode-matrix/self-host consumers can run it.
    # The retained clean binaries above, never the probes, supply all timing.
    small = out / "controls.c"
    small.write_text("int sink(volatile int *p) { return *p; }\n"
                     "int fixed(int x) { volatile int a[2] = {x, x+1}; return sink(a); }\n"
                     "int leaf(int x) { return x+3; }\n"
                     "int main(void) { return fixed(2)+leaf(1)-6; }\n")
    target = out / "subject"
    specs = {"unity": ["-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0",
                        "-g", "src/buster/apps/ide/ide.c", "-lm"],
             "controls": ["-g", str(small)]}
    for name, flags in specs.items():
        cell = {"populations": {}, "intervals": {}, "hashes": {}, "timings": []}
        for variant in ("baseline", "candidate"):
            command = [str(clean[variant]), "cc", "-fregister-allocator=fast", "-fverify-codegen", *flags, "-o", str(target)]
            run(name + "-" + variant + "-clean", command)
            cell["hashes"][variant] = digest(target)
            run(name + "-" + variant + "-probe", [str(probes[variant]), *command[1:]])
            if digest(target) != cell["hashes"][variant]:
                raise RuntimeError("Instrumentation changed output")
            text = (out / (name + "-" + variant + "-probe.log")).read_text(errors="replace")
            cell["populations"][variant] = [list(map(int, match)) for match in POPULATION.findall(text)]
            cell["intervals"][variant] = [list(map(int, match)) for match in COLOR.findall(text)]
        if not cell["populations"]["baseline"] or cell["populations"]["baseline"] != cell["populations"]["candidate"]:
            raise RuntimeError("Placement populations missing or changed")
        if cell["hashes"]["baseline"] != cell["hashes"]["candidate"]:
            raise RuntimeError("Clean A/B output changed")
        empty = [row for row in cell["intervals"]["baseline"] if row[2] == 0]
        positive = lambda rows: Counter(tuple(row) for row in rows if row[2] != 0)
        if any(row[2] == 0 for row in cell["intervals"]["candidate"]) or positive(cell["intervals"]["baseline"]) != positive(cell["intervals"]["candidate"]):
            raise RuntimeError("Empty guard omitted a nonempty call or retained an empty call")
        populations = cell["populations"]["baseline"]
        if Counter((row[0], row[1]) for row in populations if row[2] == 0) != Counter((n, s) for n, s, k in empty):
            raise RuntimeError("Empty population/colorer census mismatch")
        cell["removed_work"] = {"calls": len(empty), "requested_bytes": sum(4*n+28 for n, s, k in empty),
                                "histogram_prefix_iterations": sum(n+2 for n, s, k in empty),
                                "slot_predicate_visits": sum(2*s for n, s, k in empty),
                                "redundant_sentinel_clear_bytes": sum(4*max(s, 1) for n, s, k in empty),
                                "empty_machine_rows": sum(n for n, s, k in empty),
                                "all_placement_machine_rows": sum(row[0] for row in populations)}
        for trial in range(7):
            for variant in (("baseline", "candidate") if trial % 2 == 0 else ("candidate", "baseline")):
                rss = out / (name + "-" + variant + "-" + str(trial) + ".rss")
                record = run(name + "-" + variant + "-time-" + str(trial),
                             ["/usr/bin/time", "-f", "%M", "-o", str(rss), str(clean[variant]), "cc",
                              "-fregister-allocator=fast", "-fverify-codegen", *flags, "-o", str(target)])
                record["rss_kib"] = int(rss.read_text().strip())
                cell["timings"].append({"trial": trial, "variant": variant, **record})
                if digest(target) != cell["hashes"][variant]:
                    raise RuntimeError("Timing output changed")
        if name == "controls":
            run("execute-controls", [str(target)])
        cell["cachegrind"] = {}
        for variant in ("baseline", "candidate"):
            cache = out / (name + "-" + variant + ".cachegrind")
            run(name + "-" + variant + "-cachegrind", ["valgrind", "--tool=cachegrind", "--cache-sim=yes",
                "--branch-sim=no", "--I1=32768,8,64", "--D1=32768,8,64", "--LL=33554432,16,64",
                "--cachegrind-out-file=" + str(cache), str(clean[variant]), "cc", "-fregister-allocator=fast",
                "-fverify-codegen", *flags, "-o", str(target)], timeout=1800)
            text = cache.read_text()
            events = re.search(r"^events: (.*)$", text, re.MULTILINE).group(1).split()
            values = list(map(int, re.search(r"^summary: (.*)$", text, re.MULTILINE).group(1).split()))
            cell["cachegrind"][variant] = dict(zip(events, values))
            if digest(target) != cell["hashes"][variant]:
                raise RuntimeError("Cache simulation changed output")
        summary["workloads"][name] = cell
        (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(json.dumps({"workload": name, "removed_work": cell["removed_work"], "cachegrind": cell["cachegrind"]}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
