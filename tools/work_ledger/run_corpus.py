#!/usr/bin/env python3
"""Run the work-ledger corpus against one subject compiler pair.

A subject is two builds of the same source: a ledger build compiled with
BUSTER_BENCH_ALLOCATIONS=1 (it writes `work.*` counters through
`-fsource-metrics`) and a plain build compiled with it off. Every corpus unit
is compiled by both. The plain build's output is the subject's artifact; the
ledger build must produce the same bytes, which proves the counters observe
without changing behaviour. Optionally the plain build is run under callgrind
for an exact instruction total.

The corpus is frozen by its inputs, not by the subject: the self-host unit is
always the pinned tree given by --self-host-root, so a subject that changes the
compiler's own source still compiles the same program. Every unit pins
`-march=znver3`, because the default CPU model follows the host and would make
outputs differ between runners (and under valgrind's CPUID).

Results are written as one JSON document; tools/work_ledger/ledger_report.py
turns one or more of them into the mechanism ledger and subject comparisons.
This script measures work and identity; it never times anything for a verdict.
"""

import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import sys
import time
from pathlib import Path

LUA_UNITS = [
    "lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject", "lopcodes",
    "lparser", "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "lauxlib", "lbaselib", "ldblib",
    "liolib", "lmathlib", "loslib", "ltablib", "lstrlib", "lutf8lib", "loadlib", "lcorolib", "linit", "lua",
]
CPU = "-march=znver3"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def corpus_units(args) -> list[dict]:
    units = []
    if args.self_host_root:
        root = Path(args.self_host_root)
        units.append({"family": "selfhost", "name": "ide", "cwd": str(root), "output": "exe",
                      "argv": ["cc", "-Isrc", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g", CPU,
                               "src/buster/apps/ide/ide.c", "-lm"]})
        if args.fixtures:
            for line in Path(args.fixtures).read_text().splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    units.append({"family": "fixtures", "name": Path(line).stem, "cwd": str(root), "output": "obj",
                                  "argv": ["cc", "-g0", CPU, "-c", line]})
    if args.sqlite_root:
        root = Path(args.sqlite_root)
        for source in ["sqlite3.c", "shell.c"]:
            units.append({"family": "sqlite", "name": Path(source).stem, "cwd": str(root), "output": "obj",
                          "argv": ["cc", "-g0", "-O2", "-I.", "-DSQLITE_THREADSAFE=1", "-DSQLITE_ENABLE_MATH_FUNCTIONS",
                                   "-DSQLITE_ENABLE_COLUMN_METADATA", CPU, "-c", source]})
    if args.lua_root:
        root = Path(args.lua_root)
        for unit in LUA_UNITS:
            if (root / f"{unit}.c").exists():
                units.append({"family": "lua", "name": unit, "cwd": str(root), "output": "obj",
                              "argv": ["cc", "-g0", "-O2", "-I.", "-DLUA_USE_LINUX", CPU, "-c", f"{unit}.c"]})
    if args.cjson_root:
        root = Path(args.cjson_root)
        for unit in ["cJSON", "cJSON_Utils"]:
            units.append({"family": "cjson", "name": unit, "cwd": str(root), "output": "obj",
                          "argv": ["cc", "-g0", "-O2", "-I.", CPU, "-c", f"{unit}.c"]})
    if args.only:
        wanted = set(args.only.split(","))
        units = [unit for unit in units if unit["family"] in wanted]
    return units


def run(argv: list[str], cwd: str) -> dict:
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    process = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    elapsed = time.monotonic() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return {
        "status": process.returncode,
        "stderr": process.stderr[-4000:],
        "wall_seconds": elapsed,
        "user_seconds": after.ru_utime - before.ru_utime,
        "system_seconds": after.ru_stime - before.ru_stime,
        "minor_faults": after.ru_minflt - before.ru_minflt,
        # ru_maxrss of children is the maximum over every child so far, so it
        # is only meaningful when this child raised it; keep the raw value.
        "children_maxrss_kb": after.ru_maxrss,
    }


def parse_metrics(path: Path) -> dict:
    metrics = {}
    if path.exists():
        for line in path.read_text().splitlines():
            key, _, value = line.partition("=")
            if value.isdigit():
                metrics[key] = int(value)
    return metrics


def callgrind_total(path: Path) -> int | None:
    total = None
    if path.exists():
        with open(path, errors="replace") as handle:
            for line in handle:
                match = re.match(r"^(summary|totals):\s+(\d+)", line)
                if match:
                    total = int(match.group(2))
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--subject", required=True, help="label recorded with the results")
    parser.add_argument("--ledger-compiler", required=True)
    parser.add_argument("--plain-compiler", required=True)
    parser.add_argument("--self-host-root")
    parser.add_argument("--fixtures", help="file listing fixture paths relative to --self-host-root")
    parser.add_argument("--sqlite-root")
    parser.add_argument("--lua-root")
    parser.add_argument("--cjson-root")
    parser.add_argument("--only", help="comma-separated families to run")
    parser.add_argument("--callgrind", default="", help="comma-separated families to run under callgrind")
    parser.add_argument("--callgrind-every", type=int, default=1, help="callgrind every Nth unit of a family")
    parser.add_argument("--output", required=True, help="directory for artifacts and results.json")
    args = parser.parse_args()

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    ledger = str(Path(args.ledger_compiler).resolve())
    plain = str(Path(args.plain_compiler).resolve())
    callgrind_families = set(filter(None, args.callgrind.split(",")))
    units = corpus_units(args)
    family_index = {}
    results = {"subject": args.subject, "ledger_compiler_sha256": sha256(Path(ledger)),
               "plain_compiler_sha256": sha256(Path(plain)), "cpu_flag": CPU, "units": []}
    failures = 0
    for unit in units:
        stem = f"{unit['family']}-{unit['name']}"
        suffix = "" if unit["output"] == "exe" else ".o"
        plain_out = output / "artifacts" / f"{stem}{suffix}"
        ledger_out = output / "ledger-artifacts" / f"{stem}{suffix}"
        metrics_path = output / "metrics" / f"{stem}.metrics"
        for path in (plain_out, ledger_out, metrics_path):
            path.parent.mkdir(parents=True, exist_ok=True)
        record = {"family": unit["family"], "name": unit["name"], "argv": unit["argv"]}
        ledger_run = run([ledger] + unit["argv"] + [f"-fsource-metrics={metrics_path.resolve()}", "-o", str(ledger_out.resolve())], unit["cwd"])
        plain_run = run([plain] + unit["argv"] + ["-o", str(plain_out.resolve())], unit["cwd"])
        record["ledger_run"] = ledger_run
        record["plain_run"] = plain_run
        record["metrics"] = parse_metrics(metrics_path)
        ok = ledger_run["status"] == 0 and plain_run["status"] == 0 and plain_out.exists() and ledger_out.exists()
        record["plain_sha256"] = sha256(plain_out) if plain_out.exists() else None
        record["ledger_sha256"] = sha256(ledger_out) if ledger_out.exists() else None
        # A unit both builds reject the same way is a consistent, recorded
        # failure of the subject, not an identity failure of the ledger.
        record["compiled"] = ok
        record["identical"] = (ok and record["plain_sha256"] == record["ledger_sha256"]) or \
            (ledger_run["status"] != 0 and ledger_run["status"] == plain_run["status"])
        index = family_index.get(unit["family"], 0)
        family_index[unit["family"]] = index + 1
        if ok and unit["family"] in callgrind_families and index % max(1, args.callgrind_every) == 0:
            profile = output / "callgrind" / f"{stem}.out"
            profile.parent.mkdir(parents=True, exist_ok=True)
            scratch = output / "callgrind" / f"{stem}{suffix}"
            grind = run(["valgrind", "--tool=callgrind", "--cache-sim=no", "--branch-sim=no",
                         f"--callgrind-out-file={profile.resolve()}", plain] + unit["argv"] + ["-o", str(scratch.resolve())],
                        unit["cwd"])
            record["callgrind_status"] = grind["status"]
            record["callgrind_ir"] = callgrind_total(profile)
            record["callgrind_output_identical"] = scratch.exists() and sha256(scratch) == record["plain_sha256"]
            if scratch.exists():
                scratch.unlink()
        if not record["identical"]:
            failures += 1
            print(f"FAIL {stem}: ledger={ledger_run['status']} plain={plain_run['status']} identical={record['identical']}",
                  file=sys.stderr)
        results["units"].append(record)
        print(f"{stem}: status={plain_run['status']} identical={record['identical']} "
              f"ir={record.get('callgrind_ir')}", flush=True)
    results["failures"] = failures
    (output / "results.json").write_text(json.dumps(results, indent=1, sort_keys=True))
    print(f"WORK_LEDGER_CORPUS subject={args.subject} units={len(units)} failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
