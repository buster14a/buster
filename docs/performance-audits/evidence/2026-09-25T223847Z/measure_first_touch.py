#!/usr/bin/env python3
"""Paired first-touch census of two frozen `ide` compilers on one command.

Records, per run, the child's peak RSS, minor faults, the system-wide
`thp_fault_alloc` delta from /proc/vmstat (2 MiB zero-filled pages when
transparent huge pages are active), the transparent-huge-page mode and the
output object's SHA-256. Runs alternate AB/BA in fixed blocks so order effects
are visible. This is a deterministic page-work census, not a timing harness:
it prints wall/user/system time only as unvalidated diagnostics, and it must
not be used as performance acceptance. Timing belongs to the admitted
matched-build dedicated-host route (docs/agents/benchmarking.md).

Usage:
  measure_first_touch.py --baseline /abs/ide-base --candidate /abs/ide-cand \
      --pairs 4 --output census.json -- cc -Isrc -Ibuild/generated \
      -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -c src/buster/apps/ide/ide.c

The compiler command after `--` must not contain `-o`; the script appends a
fresh output path for every run. Run from the repository root.
"""
import argparse
import hashlib
import json
import os
import sys
import tempfile
import time


def vmstat(key):
    with open("/proc/vmstat") as handle:
        for line in handle:
            name, value = line.split()
            if name == key:
                return int(value)
    return None


def thp_mode():
    try:
        with open("/sys/kernel/mm/transparent_hugepage/enabled") as handle:
            text = handle.read().strip()
        return text[text.index("[") + 1:text.index("]")]
    except (OSError, ValueError):
        return "unavailable"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_once(compiler, arguments, directory, label, index):
    output = os.path.join(directory, f"{label}-{index}.o")
    command = [compiler] + arguments + ["-o", output]
    thp_before = vmstat("thp_fault_alloc")
    start = time.perf_counter()
    pid = os.fork()
    if pid == 0:
        os.execv(compiler, command)
    _, status, usage = os.wait4(pid, 0)
    wall = time.perf_counter() - start
    thp_after = vmstat("thp_fault_alloc")
    record = {
        "label": label,
        "index": index,
        "exit_status": os.waitstatus_to_exitcode(status),
        "maxrss_kib": usage.ru_maxrss,
        "minor_faults": usage.ru_minflt,
        "major_faults": usage.ru_majflt,
        "thp_fault_alloc_delta": None if thp_before is None or thp_after is None else thp_after - thp_before,
        "object_sha256": sha256(output) if os.path.exists(output) else None,
        "diagnostic_wall_s": round(wall, 4),
        "diagnostic_user_s": round(usage.ru_utime, 4),
        "diagnostic_system_s": round(usage.ru_stime, 4),
    }
    if os.path.exists(output):
        os.unlink(output)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--pairs", type=int, default=4)
    parser.add_argument("--output", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    arguments = options.command[1:] if options.command[:1] == ["--"] else options.command
    if not arguments or "-o" in arguments:
        parser.error("pass the compiler arguments after `--`, without -o")
    compilers = {"baseline": os.path.abspath(options.baseline), "candidate": os.path.abspath(options.candidate)}
    records = []
    with tempfile.TemporaryDirectory(prefix="first-touch-") as directory:
        for pair in range(options.pairs):
            order = ["baseline", "candidate"] if pair % 2 == 0 else ["candidate", "baseline"]
            for label in order:
                records.append(run_once(compilers[label], arguments, directory, label, pair))
    summary = {}
    for label in compilers:
        rows = [r for r in records if r["label"] == label]
        summary[label] = {
            "compiler": compilers[label],
            "compiler_sha256": sha256(compilers[label]),
            "runs": len(rows),
            "all_exit_zero": all(r["exit_status"] == 0 for r in rows),
            "object_sha256": sorted({r["object_sha256"] for r in rows if r["object_sha256"]}),
            "minor_faults": sorted(r["minor_faults"] for r in rows),
            "maxrss_kib": sorted(r["maxrss_kib"] for r in rows),
            "thp_fault_alloc_delta": sorted(r["thp_fault_alloc_delta"] for r in rows if r["thp_fault_alloc_delta"] is not None),
        }
    result = {
        "schema": "buster-first-touch-census-v1",
        "thp_mode": thp_mode(),
        "cwd": os.getcwd(),
        "arguments": arguments,
        "identical_objects": len(set(summary["baseline"]["object_sha256"]) | set(summary["candidate"]["object_sha256"])) == 1,
        "summary": summary,
        "records": records,
        "note": "Deterministic page-work census; wall/user/system fields are diagnostics, not acceptance timing.",
    }
    with open(options.output, "w") as handle:
        json.dump(result, handle, indent=1)
    print(json.dumps({k: result[k] for k in ("thp_mode", "identical_objects", "summary")}, indent=1))
    return 0 if result["identical_objects"] and all(s["all_exit_zero"] for s in summary.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
