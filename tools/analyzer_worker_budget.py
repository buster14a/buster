#!/usr/bin/env python3
"""Read-only hosted context and independent evidence replay for #2033.

The native build driver owns all execution. This reader never schedules analysis,
changes an inventory or admits a production default.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ORDER = (2, 4, 4, 2)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Record:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def line(self):
        end = self.data.index(b"\n", self.at)
        value = self.data[self.at:end]
        self.at = end + 1
        return value

    def number(self):
        line = self.line()
        require(re.fullmatch(rb"[0-9]+", line), "invalid unsigned record field")
        return int(line)

    def string(self):
        size = self.number()
        end = self.at + size
        require(end < len(self.data) and self.data[end:end + 1] == b"\n", "truncated string")
        value = self.data[self.at:end]
        self.at = end + 1
        return value

    def done(self):
        require(self.at == len(self.data), "unexpected trailing record bytes")


def regular(path):
    require(path.is_file() and not path.is_symlink(), f"missing/unsafe evidence: {path}")
    return path.read_bytes()


def fields(line, prefix):
    require(line.startswith(prefix + " "), f"missing {prefix}")
    pairs = re.findall(r"([a-z_0-9]+)=(.*?)(?= [a-z_0-9]+=|$)", line[len(prefix) + 1:])
    require(len(dict(pairs)) == len(pairs), "duplicate metric field")
    return dict(pairs)


def inventory(path):
    data = regular(path)
    record = Record(data)
    require(record.line() == b"BUSTER_CLANG_ANALYZE_PLAN_V1", "wrong manifest version")
    root_start = record.at
    result_root = record.string()
    root_end = record.at
    config, clang = record.string(), record.string()
    shards, timeout, count, excluded = (record.number() for _ in range(4))
    require(count > 0 and shards >= 4 and timeout > 0, "empty/invalid inventory")
    database = record.string()
    units = []
    for index in range(count):
        require(record.number() == index, "noncanonical unit index")
        shard = record.number()
        module, source = record.string(), record.string()
        command = tuple(record.string() for _ in range(record.number()))
        require(shard < shards and module and source and command, "invalid unit")
        units.append((shard, module, source, command))
    record.done()
    require(len(set((row[2], row[3]) for row in units)) == count, "duplicate unit identity")
    # The run directory is the only deliberately changing manifest field.
    invariant = (config, clang, shards, timeout, count, excluded, database, tuple(units))
    return data, root_start, root_end, result_root, invariant


def verify(root):
    campaign = root / "campaign"
    lines = regular(campaign / "qualification.txt").decode().splitlines()
    require(lines[0] == "BUSTER_ANALYZE_WORKER_QUALIFICATION_V1" and len(lines) == 5, "incomplete qualification")
    expected_dirs = {f"sample-{i}-jobs-{jobs}" for i, jobs in enumerate(ORDER)}
    require({p.name for p in campaign.iterdir()} == expected_dirs | {"qualification.txt"}, "unexpected campaign entries")
    common = None
    common_logs = None
    recorded_campaign = None
    samples = []
    for index, jobs in enumerate(ORDER):
        sample = fields(lines[index + 1], "ANALYZE_WORKER_SAMPLE")
        require(int(sample["sample"]) == index and int(sample["jobs"]) == jobs and sample["status"] == "pass", "failed/wrong sample")
        arm = campaign / f"sample-{index}-jobs-{jobs}"
        require(arm.is_dir() and not arm.is_symlink(), "unsafe arm")
        data, start, end, result_root, selected = inventory(arm / "manifest.txt")
        config, clang, shards, timeout, count, excluded, database, units = selected
        recorded_arm = Path(result_root.decode())
        require(recorded_arm.is_absolute() and recorded_arm.name == arm.name, "wrong arm directory")
        require(recorded_campaign is None or recorded_campaign == recorded_arm.parent, "campaign directory changed")
        recorded_campaign = recorded_arm.parent
        canonical_root = str(recorded_campaign).encode()
        canonical = data[:start] + str(len(canonical_root)).encode() + b"\n" + canonical_root + b"\n" + data[end:]
        require(digest(canonical) == sample["inventory_sha256"], "wrong frozen inventory hash")
        require(int(sample["eligible"]) == count and int(sample["host_logical_cpus"]) >= 4, "wrong useful work/CPU count")
        require(common is None or common == selected, "inventory/commands/deadlines changed between arms")
        common = selected
        require({p.name for p in arm.iterdir()} == {"manifest.txt", "run.txt"} | {f"shard-{s}" for s in range(shards)}, "unexpected arm entries")
        logs = {}
        shard_wall = []
        unit_wall = 0
        for shard in range(shards):
            directory = arm / f"shard-{shard}"
            require(directory.is_dir() and not directory.is_symlink(), "unsafe shard directory")
            report = Record(regular(directory / "result.txt"))
            require(report.line() == b"BUSTER_CLANG_ANALYZE_RESULT_V1", "wrong result version")
            require(report.line().decode() == digest(data) and report.number() == shard, "stale/wrong shard")
            rows, duration, rss = (report.number() for _ in range(3))
            shard_wall.append(duration)
            expected = {i for i, unit in enumerate(units) if unit[0] == shard}
            seen = set()
            for _ in range(rows):
                unit, status, elapsed = (report.number() for _ in range(3))
                fingerprint = report.line().decode()
                require(unit in expected and unit not in seen and status == 0, "failed/duplicate/unexpected TU")
                log = regular(directory / f"unit-{unit}.log")
                require(digest(log) == fingerprint, "missing/changed diagnostic log")
                seen.add(unit)
                logs[unit] = fingerprint
                unit_wall += elapsed
            report.done()
            require(seen == expected, "omitted TU")
            require({p.name for p in directory.iterdir()} == {"result.txt"} | {f"unit-{i}.log" for i in expected}, "unexpected shard entries")
        require(common_logs is None or common_logs == logs, "diagnostic checksums changed between arms")
        common_logs = logs
        run = fields(regular(arm / "run.txt").decode().strip(), "ANALYZE_RUN")
        require(int(run["jobs"]) == jobs and run["status"] == "pass" and run["results"] == result_root.decode(), "failed/wrong run")
        tree_status = run.get("process_tree_status")
        tree_reason = run.get("process_tree_reason")
        require(tree_status in ("complete", "incomplete", "unavailable") and tree_reason and
                re.fullmatch(r"[a-z0-9-]+", tree_reason), "missing/invalid process-tree sampling status")
        require((tree_status == "complete") == (tree_reason == "none"), "inconsistent process-tree sampling status/reason")
        require(tree_status == "complete", f"process-tree sampling is {tree_status}: {tree_reason}")
        require(0 < int(run["peak_pending_workers"]) <= jobs and int(run["samples"]) > 0 and int(run["sampled_peak_tree_rss_bytes"]) > 0, "unavailable concurrency/memory")
        samples.append({"sample": index, "jobs": jobs, "eligible": count,
                        "elapsed_us": int(sample["elapsed_us"]), "children_cpu_us": int(sample["children_cpu_us"]),
                        "run_elapsed_us": int(run["elapsed_us"]), "peak_pending_workers": int(run["peak_pending_workers"]),
                        "peak_live_processes": int(run["peak_live_processes"]),
                        "sampled_peak_tree_rss_bytes": int(run["sampled_peak_tree_rss_bytes"]),
                        "shard_wall_us": shard_wall, "sum_unit_wall_us": unit_wall,
                        "inventory_sha256": sample["inventory_sha256"]})
    return {"proof": "offline-evidence-replay", "production_default_admitted": False,
            "samples": samples, "diagnostic_sha256": digest(json.dumps(common_logs, sort_keys=True).encode()),
            "host_before": json.loads(regular(root / "host-before.json")),
            "host_after": json.loads(regular(root / "host-after.json"))}


def host():
    def read(path):
        try:
            return Path(path).read_text()
        except OSError:
            return None
    # Retain all ancestors: a root 'max' does not override a nested quota.
    cgroup = read("/proc/self/cgroup")
    relative = next((row[3:] for row in (cgroup or "").splitlines() if row.startswith("0::")), None)
    groups = {}
    if relative is not None and ".." not in Path(relative).parts:
        directory = Path("/sys/fs/cgroup") / relative.lstrip("/")
        while directory == Path("/sys/fs/cgroup") or Path("/sys/fs/cgroup") in directory.parents:
            groups[str(directory)] = {name: read(directory / name) for name in (
                "cpu.max", "cpuset.cpus.effective", "cpu.stat", "memory.max", "memory.current",
                "memory.peak", "memory.events", "memory.swap.current", "memory.swap.max")}
            directory = directory.parent
    return {"logical_cpus": os.cpu_count(), "affinity_cpus": sorted(os.sched_getaffinity(0)),
            "meminfo": read("/proc/meminfo"), "cgroup": cgroup, "cgroup_limits": groups,
            "cpu_pressure": read("/proc/pressure/cpu"), "memory_pressure": read("/proc/pressure/memory"),
            "lscpu": subprocess.check_output(["lscpu"], text=True), "uname": list(os.uname())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("host", "verify"))
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    if args.command == "host":
        args.path.write_text(json.dumps(host(), indent=2) + "\n")
    else:
        print(json.dumps(verify(args.path), indent=2))


if __name__ == "__main__":
    main()
