#!/usr/bin/env python3
"""Bounded, best-effort host observations for Intel-macOS CI steps.

The step shell owns this process and stops it on exit. Each sample goes to the
live Actions log and the existing job artifact; neither survives every runner
loss. Only process names, never argv, environment or file contents, are logged.
"""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import threading
import time


INTERVAL_SECONDS = 30
COMMAND_TIMEOUT_SECONDS = 2
STOP = threading.Event()


def command_output(argv):
    try:
        result = subprocess.run(argv, capture_output=True, text=True,
                                timeout=COMMAND_TIMEOUT_SECONDS, check=False)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    except OSError:
        return None, "unavailable"
    return (result.stdout, None) if result.returncode == 0 else (None, "failed")


def process_tree(output, parent_pid, sampler_pid):
    """Aggregate only descendants of the step shell, excluding this sampler."""
    rows = {}
    for line in output.splitlines():
        parts = line.strip().split(maxsplit=4)
        if len(parts) != 5:
            continue
        try:
            pid, ppid = int(parts[0]), int(parts[1])
            cpu, rss = float(parts[2]), int(parts[3])
        except ValueError:
            continue
        if pid > 0 and ppid >= 0 and cpu >= 0 and rss >= 0:
            name = re.sub(r"[^A-Za-z0-9._-]", "_", Path(parts[4]).name)[:48]
            rows[pid] = (ppid, cpu, rss, name)
    if parent_pid not in rows:
        return {"status": "parent-unavailable"}
    included = {parent_pid}
    excluded = {sampler_pid}
    for _ in range(len(rows)):
        before = len(included) + len(excluded)
        for pid, (ppid, _, _, _) in rows.items():
            if ppid in excluded:
                excluded.add(pid)
            elif ppid in included and pid not in excluded:
                included.add(pid)
        if len(included) + len(excluded) == before:
            break
    included.difference_update(excluded)
    top = sorted(((pid, rows[pid]) for pid in included),
                 key=lambda item: (-item[1][2], item[0]))[:3]
    return {"status": "observed", "count": len(included),
            "cpu_percent": round(sum(rows[pid][1] for pid in included), 1),
            "rss_kib": sum(rows[pid][2] for pid in included),
            "largest": [{"pid": pid, "name": row[3], "rss_kib": row[2]}
                        for pid, row in top]}


def memory_observation():
    pressure, pressure_error = command_output(["memory_pressure", "-Q"])
    free_percent = re.search(r"System-wide memory free percentage:\s*(\d+)%", pressure or "")
    swap, swap_error = command_output(["sysctl", "-n", "vm.swapusage"])
    swap_used = re.search(r"\bused\s*=\s*([\d.]+)([KMGT])\b", swap or "")
    pages, pages_error = command_output(["vm_stat"])
    pageouts = re.search(r"^Pageouts:\s*([\d.]+)\.", pages or "", re.MULTILINE)
    unit = {"K": 1 / 1024, "M": 1, "G": 1024, "T": 1024 * 1024}
    return {
        "free_percent": int(free_percent.group(1)) if free_percent else "unknown",
        "pressure_status": pressure_error or ("observed" if free_percent else "unparseable"),
        "swap_used_mib": round(float(swap_used.group(1)) * unit[swap_used.group(2)], 1)
                         if swap_used else "unknown",
        "swap_status": swap_error or ("observed" if swap_used else "unparseable"),
        "pageouts_cumulative": int(float(pageouts.group(1))) if pageouts else "unknown",
        "pageouts_status": pages_error or ("observed" if pageouts else "unparseable"),
    }


def sample(parent_pid, phase, temp_directory, elapsed_seconds):
    started = time.monotonic()
    processes, process_error = command_output(
        ["ps", "-axo", "pid=,ppid=,%cpu=,rss=,comm="])
    tree = process_tree(processes, parent_pid, os.getpid()) if processes is not None else \
        {"status": process_error}
    try:
        disk = shutil.disk_usage(temp_directory)
        disk_free_mib = disk.free // (1024 * 1024)
    except OSError:
        disk_free_mib = "unknown"
    try:
        load_1m = round(os.getloadavg()[0], 2)
    except OSError:
        load_1m = "unknown"
    record = {"schema": "buster-ci-runner-resources-v1", "phase": phase,
              "at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
              "elapsed_seconds": round(elapsed_seconds, 1),
              "process_tree": tree, "load_1m": load_1m,
              "logical_cpus": os.cpu_count() or "unknown",
              "memory": memory_observation(), "disk_free_mib": disk_free_mib}
    record["sampling_ms"] = round((time.monotonic() - started) * 1000, 1)
    return record


def stop(_signum, _frame):
    STOP.set()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent-pid", type=int, required=True)
    parser.add_argument("--phase", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--interval", type=float, default=INTERVAL_SECONDS)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    if args.parent_pid <= 0 or not re.fullmatch(r"[a-z0-9-]+", args.phase) or args.interval < 1:
        parser.error("invalid parent PID, phase or sampling interval")
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    count = 0
    with args.log.open("a", encoding="utf-8", buffering=1) as output:
        while not STOP.is_set():
            record = sample(args.parent_pid, args.phase, os.environ.get("RUNNER_TEMP", "."),
                            time.monotonic() - start)
            line = "CI_RESOURCE_SAMPLE " + json.dumps(record, sort_keys=True, separators=(",", ":"))
            print(line, flush=True)
            output.write(line + "\n")
            count += 1
            if args.once:
                break
            STOP.wait(args.interval)
        line = f"CI_RESOURCE_END phase={args.phase} samples={count} elapsed_seconds={time.monotonic() - start:.1f}"
        print(line, flush=True)
        output.write(line + "\n")


if __name__ == "__main__":
    main()
