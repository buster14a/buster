#!/usr/bin/env python3
"""Micro-architecture lab: where, when, what and how many for one compile.

Ownership: compiler performance research tooling (docs/agents/benchmarking.md,
"Micro-architecture lab").  Python 3 standard library only; Linux `perf`.

It runs one workload -- by default the stage-1 self-host compile of the unity
`src/buster/apps/ide/ide.c` by a Release `ide` -- pinned to one CPU through a
fixed sequence of steps, keeps every raw file in an output directory, and
renders `report.md` from those raw files alone, so `report DIR` reproduces the
report without re-running anything.

    python3 tools/uarch_lab.py run --ide build/Release/ide --repo-root . \\
        --cpu 2 --output /tmp/lab [--runs 30] [--sudo] [--skip STEP ...] \\
        [--perf PATH] [-- extra compile args]
    python3 tools/uarch_lab.py report /tmp/lab

Steps (each writes DIR/<step>/ and one report section; any can be skipped):

    env       versions, CPU model/microcode, governor/EPP, SMT, paranoid,
              `perf list metricgroups`, binary sha256, git revision
    timed     warm-up, then N pinned runs under `perf stat -x,` (time, cycles,
              instructions, branch misses, faults), byte-compared outputs,
              `-fsource-metrics` once and `-fmetrics-out` per run if supported
    topdown   `perf stat -r 3 -M GROUP` per discovered metric group
    timeline  one `perf stat -I 20` run plus `-fmetrics-out`: intervals
              attributed to compiler phases (WHEN), CSV and SVG/HTML chart
    sampling  `perf record --call-graph fp` per event (WHERE), every page
              fault with its data address, annotate and srcline listings
    ibs       (--sudo only) AMD IBS op/fetch samples and `perf mem`
    micro     `ide bench` if the binary supports it

Failures of a step are recorded in DIR/lab.json and the report, never fatal;
unsupported counters (`<not supported>`, `<not counted>`) are NA, never zero.
The phase breakdown needs a binary that accepts `-fmetrics-out=` and writes a
measured `CC_METRICS_INPUT` record; without it those sections say so.

Map (searchable symbols):
    STEPS, TIMED_EVENTS, INTERVAL_EVENTS, SAMPLE_EVENTS   step and event tables
    parse_stat_csv, stat_values, normalize_event          perf stat -x output
    parse_key_values, parse_cc_metrics                    compiler metric files
    parse_report, parse_annotate, parse_fault_script      perf report/annotate/script
    summarize, percentile, runs_since_minimum, tenth_medians   statistics
    phase_spans, interval_table, attribute_intervals      timeline alignment
    Lab, Lab.workload, Lab.run_command                    process execution
    step_env ... step_micro                               the steps (raw files)
    render_env ... render_micro, render_report            report from raw files
    timeline_svg                                          self-contained chart
    main                                                  CLI
Tests: tools/uarch_lab_test.py (`python3 -B tools/uarch_lab_test.py`).
"""

import argparse
import filecmp
import hashlib
import html
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

STEPS = ("env", "timed", "topdown", "timeline", "sampling", "ibs", "micro")
DEFAULT_COMPILE = ["cc", "-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1",
                   "-DBUSTER_INCLUDE_TESTS=0", "-g", "src/buster/apps/ide/ide.c", "-lm"]
TIMED_EVENTS = ["duration_time", "task-clock", "cycles:u", "instructions:u", "branch-misses:u",
                "page-faults", "minor-faults", "major-faults"]
# At most six hardware events so a Zen 5 core (six PMCs) need not multiplex.
INTERVAL_EVENTS = ["cycles:u", "instructions:u", "branch-misses:u", "L1-icache-load-misses:u",
                   "L1-dcache-load-misses:u", "dTLB-load-misses:u", "task-clock",
                   "minor-faults", "major-faults"]
SAMPLE_EVENTS = [("cycles", "cycles:u"), ("branch-misses", "branch-misses:u"),
                 ("l1i-misses", "L1-icache-load-misses:u"), ("l1d-misses", "L1-dcache-load-misses:u"),
                 ("dtlb-misses", "dTLB-load-misses:u")]
PHASES = ("read", "preprocess", "parse", "analysis", "ir", "codegen", "object", "emit")
# Metric groups worth one `perf stat -M` each, matched case-insensitively
# against `perf list metricgroups`; the first pattern ranks first.
GROUP_PATTERNS = [r"^pipelinel1$", r"^pipelinel2$", r"^(topdownl1|tmal1)$", r"branch|brmispredict",
                  r"decod|op_?cache|dsb|fetch|frontend|icmiss|icache", r"cache|^l[123]", r"tlb", r"mem"]
GROUP_EXCLUDE = r"uncore|data_fabric|power|smt|server|soc|^io"
TOPDOWN_CATEGORIES = ("frontend_bound", "bad_speculation", "backend_bound", "retiring")
INTERVAL_MS = 20
COMMAND_TIMEOUT = 900
SRCLINE_TIMEOUT = 300


# ---------------------------------------------------------------- parsers

def parse_number(text):
    text = text.strip()
    if not text or text.startswith("<"):
        return None
    try:
        return float(text)
    except ValueError:
        return None


def normalize_event(name):
    """`cycles:u` -> `cycles`, `cpu_core/cycles/u` -> `cycles`."""
    name = name.strip()
    if "/" in name:
        parts = [part for part in name.split("/") if part]
        name = parts[1] if len(parts) > 1 else (parts[0] if parts else name)
    return re.sub(r":[a-zA-Z]+$", "", name)


def parse_stat_csv(text, interval=False):
    """Rows of `perf stat -x,` output; values that perf did not count are None.

    Field order: [time,] value, unit, event, [variance%,] run time, percent
    running, metric value, metric label.  Metric-only lines (top-down) carry
    empty counter fields and the metric in the last two fields."""
    rows = []
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split(",")
        moment = None
        if interval:
            moment = parse_number(fields[0])
            if moment is None:
                continue
            fields = fields[1:]
        if len(fields) < 3:
            continue
        rest = fields[3:]
        variance = None
        if rest and rest[0].strip().endswith("%"):
            variance = parse_number(rest[0].strip()[:-1])
            rest = rest[1:]
        row = {"time": moment, "value": parse_number(fields[0]), "raw": fields[0].strip(),
               "unit": fields[1].strip(), "event": fields[2].strip(), "variance": variance,
               "runtime": parse_number(rest[0]) if rest else None,
               "pct_running": parse_number(rest[1]) if len(rest) > 1 else None,
               "metric_value": None, "metric_name": "", "metric_unit": ""}
        if len(fields) >= 5:
            value = parse_number(fields[-2])
            label = fields[-1].strip()
            if value is not None and re.search(r"[A-Za-z]", label):
                unit = ""
                if label.startswith("%"):
                    unit, label = "%", label[1:].strip()
                row["metric_value"], row["metric_name"], row["metric_unit"] = value, label, unit
        if not row["event"] and not row["metric_name"]:
            continue
        rows.append(row)
    return rows


def stat_values(rows):
    """Normalized event name -> value (None when perf could not count it)."""
    values = {}
    for row in rows:
        if row["event"]:
            values[normalize_event(row["event"])] = row["value"]
    return values


def stat_metrics(rows):
    metrics = []
    seen = set()
    for row in rows:
        name = row["metric_name"]
        if name and name not in seen:
            seen.add(name)
            metrics.append((name, row["metric_value"], row["metric_unit"]))
    return metrics


def multiplex_percent(rows):
    """Smallest percent-running over counted events (100 = no multiplexing)."""
    values = [row["pct_running"] for row in rows if row["event"] and row["value"] is not None
              and row["pct_running"] is not None]
    return min(values) if values else None


def parse_key_values(text):
    """`group.field=value` lines (-fsource-metrics)."""
    values = {}
    for line in text.splitlines():
        if "=" in line:
            key, _, value = line.strip().partition("=")
            values[key] = _number_or_text(value)
    return values


def _number_or_text(value):
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    if re.fullmatch(r"-?\d+\.\d*", value):
        return float(value)
    return value


def parse_cc_metrics(text):
    """`-fmetrics-out` records: {'header': {...}, 'inputs': [{...}, ...]}."""
    result = {"header": {}, "inputs": []}
    for line in text.splitlines():
        words = line.split()
        if not words:
            continue
        record = {}
        for word in words[1:]:
            key, sep, value = word.partition("=")
            if sep:
                record[key] = _number_or_text(value)
        if words[0] == "CC_METRICS":
            result["header"] = record
        elif words[0] == "CC_METRICS_INPUT":
            result["inputs"].append(record)
    return result


def measured_input(metrics):
    """The first measured input record, or None."""
    for record in (metrics or {}).get("inputs", []):
        if record.get("measured") == 1 and "start_ns" in record:
            return record
    return None


def parse_report(text):
    """`perf report --stdio` -> {event: [(percent, self_percent|None, entry)]}.

    Handles `--no-children` (one percent) and `--children` (children, self)
    tables; callchain lines are ignored."""
    sections = {}
    current = sections.setdefault("", [])
    entry = re.compile(r"^\s*(\d+\.\d+)%\s+(?:(\d+\.\d+)%\s+)?(.*\S)\s*$")
    for line in text.splitlines():
        if line.startswith("#"):
            match = re.search(r"of event '([^']+)'", line)
            if match:
                current = sections.setdefault(match.group(1), [])
            continue
        match = entry.match(line)
        if match:
            name = re.sub(r"^\[[.kgu]\]\s+", "", match.group(3))
            # perf >= 6.x may append "IPC [IPC Coverage]" columns ("-  -" without LBR).
            name = re.sub(r"\s{2,}(?:-|\d+\.\d+)\s+(?:-|\[\s*\d+\.\d+%\])$", "", name)
            self_percent = float(match.group(2)) if match.group(2) else None
            current.append((float(match.group(1)), self_percent, name))
    if not sections[""]:
        del sections[""]
    return sections


def report_entries(text):
    """All entries of a report, sections concatenated."""
    rows = []
    for values in parse_report(text).values():
        rows.extend(values)
    return rows


def parse_annotate(text, limit=10):
    """Hottest instructions in `perf annotate --stdio`: [(percent, address, text)]."""
    rows = []
    pattern = re.compile(r"^\s*(\d+\.\d+)\s*[:│]\s*([0-9a-f]+):\s*(.*\S)\s*$")
    for line in text.splitlines():
        match = pattern.match(line)
        if match and float(match.group(1)) > 0:
            rows.append((float(match.group(1)), match.group(2), re.sub(r"\s+", " ", match.group(3))))
    rows.sort(key=lambda row: -row[0])
    return rows[:limit]


MMAP_LINE = re.compile(r"PERF_RECORD_MMAP2? \d+/\d+: \[(0x[0-9a-f]+)\((0x[0-9a-f]+)\) @ [^\]]*\]: "
                       r"(?:([rwxps-]{4}) )?(.*\S)\s*$")
# `time: addr [addr-symbol] ip symbol`; perf prints the data address's own
# symbol when it resolves one, so that column is optional.
FAULT_LINE = re.compile(r"^\s*(?:\S+\s+\d+\s+(?:\[\d+\]\s+)?)?(\d+\.\d+):\s+(?:\S+:\s+)?"
                        r"([0-9a-f]+)\s+(?:\S+\s+)??([0-9a-f]+)\s*(.*)$")


def parse_fault_script(text):
    """`perf script -F time,addr,ip,sym --show-mmap-events` -> (mmaps, faults).

    mmaps: [(start, end, name, protection)]; faults: [(time, addr, ip, sym)]."""
    mmaps, faults = [], []
    for line in text.splitlines():
        if "PERF_RECORD_" in line:
            match = MMAP_LINE.search(line)
            if match:
                start, length = int(match.group(1), 16), int(match.group(2), 16)
                mmaps.append((start, start + length, match.group(4), match.group(3) or ""))
            continue
        match = FAULT_LINE.match(line)
        if match:
            symbol = re.sub(r"\+0x[0-9a-f]+$", "", match.group(4).strip()) or "[unknown]"
            faults.append((float(match.group(1)), int(match.group(2), 16), int(match.group(3), 16), symbol))
    return mmaps, faults


def classify_address(address, mmaps):
    """Region label for a faulting data address; later mappings win."""
    for start, end, name, protection in reversed(mmaps):
        if start <= address < end:
            size = (end - start) / 1048576
            label = "anon" if name.startswith("//anon") or not name else name
            return "%s %s @0x%x (%.1f MiB)" % (label, protection, start, size)
    return "not an mmap seen by perf (brk heap/stack/pre-exec) @%#x GiB-region" % (address >> 30 << 30)


# ---------------------------------------------------------------- statistics

def percentile(sorted_values, fraction):
    if not sorted_values:
        return None
    position = (len(sorted_values) - 1) * fraction
    low = int(position)
    high = min(low + 1, len(sorted_values) - 1)
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (position - low)


def summarize(values):
    values = sorted(value for value in values if value is not None)
    if not values:
        return None
    median = statistics.median(values)
    return {"n": len(values), "min": values[0], "p10": percentile(values, 0.10), "median": median,
            "p90": percentile(values, 0.90), "max": values[-1], "mean": statistics.fmean(values),
            "mad": statistics.median(abs(value - median) for value in values)}


def runs_since_minimum(values):
    best, index = None, None
    for position, value in enumerate(values):
        if value is not None and (best is None or value < best):
            best, index = value, position
    return None if index is None else len(values) - 1 - index


def tenth_medians(values):
    values = [value for value in values if value is not None]
    count = len(values)
    if count < 10:
        return [statistics.median(values)] if values else []
    return [statistics.median(values[part * count // 10:(part + 1) * count // 10]) for part in range(10)]


def ratio(numerator, denominator, scale=1.0):
    if numerator is None or denominator in (None, 0):
        return None
    return numerator * scale / denominator


# ---------------------------------------------------------------- timeline alignment

def phase_spans(metrics, total_seconds):
    """[(name, start_s, end_s)] in process time from `-fmetrics-out` records.

    Offsets count from the compiler's metrics origin (after argument parsing),
    taken as perf's time zero; the start-up before it is inside 'exit/slack'
    as part of the alignment error that `alignment_error` reports."""
    record = measured_input(metrics)
    if record is None:
        return []
    spans = [("startup", 0.0, record["start_ns"] / 1e9)]
    cursor = record["start_ns"]
    for phase in PHASES:
        length = record.get(phase + "_ns", 0) or 0
        spans.append((phase, cursor / 1e9, (cursor + length) / 1e9))
        cursor += length
    end = record.get("end_ns", cursor)
    if end > cursor:
        spans.append(("unaccounted", cursor / 1e9, end / 1e9))
    wall = metrics["header"].get("wall_ns", end)
    if wall > end:
        spans.append(("link+finish", end / 1e9, wall / 1e9))
    if total_seconds is not None and total_seconds > wall / 1e9:
        spans.append(("exit/slack", wall / 1e9, total_seconds))
    return [span for span in spans if span[2] > span[1]]


def alignment_error(metrics, total_seconds):
    """Seconds of process lifetime outside the compiler's own clock."""
    wall = (metrics or {}).get("header", {}).get("wall_ns")
    if wall is None or total_seconds is None:
        return None
    return max(0.0, total_seconds - wall / 1e9)


def interval_table(rows):
    """Interval rows -> [{'t0', 't1', 'values': {event: value|None}, 'pct': {...}}]."""
    table = {}
    for row in rows:
        if row["time"] is None or not row["event"]:
            continue
        entry = table.setdefault(row["time"], {"values": {}, "pct": {}})
        entry["values"][normalize_event(row["event"])] = row["value"]
        entry["pct"][normalize_event(row["event"])] = row["pct_running"]
    intervals, previous = [], 0.0
    for moment in sorted(table):
        intervals.append({"t0": previous, "t1": moment, **table[moment]})
        previous = moment
    return intervals


def attribute_intervals(intervals, spans):
    """Split each interval's counts across spans in proportion to overlap.

    Returns {span: {event: value|None}}; None when no overlapping interval
    had a counted value for that event."""
    result = {name: {} for name, _, _ in spans}
    for interval in intervals:
        width = interval["t1"] - interval["t0"]
        if width <= 0:
            continue
        for name, start, end in spans:
            overlap = min(end, interval["t1"]) - max(start, interval["t0"])
            if overlap <= 0:
                continue
            share = overlap / width
            for event, value in interval["values"].items():
                current = result[name].get(event)
                if value is None:
                    result[name].setdefault(event, None)
                else:
                    result[name][event] = (current or 0.0) + value * share
    return result


# ---------------------------------------------------------------- execution

class Lab:
    def __init__(self, output, perf="perf", cpu=None, ide=None, repo_root=".", extra=(), sudo=False):
        self.output = os.path.abspath(output)
        self.perf = perf
        self.cpu = cpu
        self.ide = ide
        self.repo_root = os.path.abspath(repo_root)
        self.extra = list(extra)
        self.sudo = sudo
        self.environment = dict(os.environ, LC_ALL="C", LANG="C")
        self.meta = load_meta(self.output)

    def path(self, *parts):
        full = os.path.join(self.output, *parts)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        return full

    def directory(self, name):
        full = os.path.join(self.output, name)
        os.makedirs(full, exist_ok=True)
        return full

    def pin(self):
        return ["taskset", "-c", str(self.cpu)] if self.cpu is not None else []

    def workload(self, output_path, flags=()):
        return [self.ide] + DEFAULT_COMPILE + self.extra + list(flags) + ["-o", output_path]

    def run_command(self, argv, log=None, timeout=COMMAND_TIMEOUT, stdout_path=None):
        """Run argv in the repository root; returns (exit status, stdout, stderr)."""
        started = time.monotonic()
        try:
            completed = subprocess.run(argv, cwd=self.repo_root, env=self.environment, timeout=timeout,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            status, out, err = completed.returncode, completed.stdout, completed.stderr
        except FileNotFoundError as error:
            status, out, err = 127, b"", str(error).encode()
        except subprocess.TimeoutExpired as error:
            status, out, err = 124, error.stdout or b"", (error.stderr or b"") + b"\ntimeout\n"
        out_text = out.decode("utf-8", "replace")
        err_text = err.decode("utf-8", "replace")
        if stdout_path:
            with open(stdout_path, "w") as handle:
                handle.write(out_text)
        with open(self.path("commands.log"), "a") as handle:
            handle.write("%s exit=%d %.3fs\n" % (shell_join(argv), status, time.monotonic() - started))
        if log:
            with open(log, "w") as handle:
                handle.write("$ %s\nexit=%d\n--- stdout\n%s\n--- stderr\n%s" % (
                    shell_join(argv), status, out_text[-200000:], err_text[-200000:]))
        return status, out_text, err_text

    def save_meta(self):
        with open(self.path("lab.json"), "w") as handle:
            json.dump(self.meta, handle, indent=2, sort_keys=True)


def shell_join(argv):
    return " ".join(re.sub(r"^(.*[^\w@%+=:,./#-].*)$", r"'\1'", word) if word else "''" for word in argv)


def load_meta(output):
    try:
        with open(os.path.join(output, "lab.json")) as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {"version": 1, "config": {}, "steps": {}, "capabilities": {}}


def read_text(path):
    try:
        with open(path, errors="replace") as handle:
            return handle.read()
    except OSError:
        return None


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text)[:80]


# ---------------------------------------------------------------- steps (raw files)

def step_env(lab):
    facts = {"command": shell_join(lab.workload("OUT")), "cpu": lab.cpu, "date": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    probes = {"perf_version": [lab.perf, "--version"], "kernel": ["uname", "-srvm"], "clang": ["clang", "--version"],
              "git_revision": ["git", "rev-parse", "HEAD"], "git_dirty_files": ["git", "status", "--porcelain"]}
    for key, argv in probes.items():
        status, out, err = lab.run_command(argv, timeout=60)
        text = out.strip() if status == 0 else "NA (%s)" % (err.strip().splitlines() or ["exit %d" % status])[-1]
        facts[key] = len(text.splitlines()) if key == "git_dirty_files" and status == 0 else text.splitlines()[0] if text else ""
    status, out, _ = lab.run_command(["lscpu"], timeout=60)
    with open(lab.path("env", "lscpu.txt"), "w") as handle:
        handle.write(out)
    match = re.search(r"^Model name:\s*(.*)$", out, re.M)
    facts["cpu_model"] = match.group(1).strip() if match else "NA"
    cpuinfo = read_text("/proc/cpuinfo") or ""
    match = re.search(r"^microcode\s*:\s*(\S+)", cpuinfo, re.M)
    facts["microcode"] = match.group(1) if match else "NA"
    sysfs = {"governor": "cpufreq/scaling_governor", "epp": "cpufreq/energy_performance_preference",
             "scaling_driver": "cpufreq/scaling_driver", "cur_khz": "cpufreq/scaling_cur_freq",
             "max_khz": "cpufreq/scaling_max_freq", "smt_siblings": "topology/thread_siblings_list"}
    cpu = lab.cpu if lab.cpu is not None else 0
    for key, relative in sysfs.items():
        facts[key] = (read_text("/sys/devices/system/cpu/cpu%d/%s" % (cpu, relative)) or "NA").strip()
    for key, path in {"smt_active": "/sys/devices/system/cpu/smt/active", "boost": "/sys/devices/system/cpu/cpufreq/boost",
                      "perf_event_paranoid": "/proc/sys/kernel/perf_event_paranoid",
                      "nmi_watchdog": "/proc/sys/kernel/nmi_watchdog", "kernel_cmdline": "/proc/cmdline",
                      "loadavg": "/proc/loadavg"}.items():
        facts[key] = (read_text(path) or "NA").strip()
    facts["ide"] = lab.ide
    facts["ide_sha256"] = sha256_file(lab.ide) if os.path.isfile(lab.ide) else "NA"
    facts["ibs_pmus"] = [name for name in ("ibs_op", "ibs_fetch") if os.path.isdir("/sys/bus/event_source/devices/" + name)]
    status, out, err = lab.run_command([lab.perf, "list", "metricgroups"], timeout=120)
    with open(lab.path("env", "metricgroups.txt"), "w") as handle:
        handle.write(out + err)
    facts["metric_groups"] = discover_groups(out) if status == 0 else []
    with open(lab.path("env", "env.json"), "w") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
    return "%d metric groups selected" % len(facts["metric_groups"])


def discover_groups(text, limit=16):
    """Select metric groups from `perf list metricgroups` by GROUP_PATTERNS."""
    names = []
    for line in text.splitlines():
        word = line.split(":")[0].strip()
        if not word or " " in word or word.lower() in ("metric groups", "list of pre-defined events"):
            continue
        if line.startswith((" ", "\t")) or word in names:
            continue
        names.append(word)
    # Round robin over the patterns so every subsystem gets a group before
    # any pattern gets its second one.
    queues = [[name for name in names if re.search(pattern, name.lower()) and not re.search(GROUP_EXCLUDE, name.lower())]
              for pattern in GROUP_PATTERNS]
    chosen = []
    while len(chosen) < limit and any(queues):
        for queue in queues:
            while queue and queue[0] in chosen:
                queue.pop(0)
            if queue and len(chosen) < limit:
                chosen.append(queue.pop(0))
    return chosen


def compile_flags(lab, metrics_path):
    return ["-fmetrics-out=" + metrics_path] if metrics_path and lab.meta["capabilities"].get("metrics_out") else []


def step_timed(lab, runs, warmups):
    directory = lab.directory("timed")
    out = os.path.join(directory, "out.exe")
    capabilities = lab.meta["capabilities"]
    # Work denominators (and the -fsource-metrics probe) from a separate run.
    source = os.path.join(directory, "source.metrics")
    status, _, _ = lab.run_command(lab.pin() + lab.workload(out, ["-fsource-metrics=" + source]), log=os.path.join(directory, "source-run.log"))
    capabilities["source_metrics"] = status == 0 and os.path.exists(source)
    if status != 0:
        status, _, err = lab.run_command(lab.pin() + lab.workload(out), log=os.path.join(directory, "plain-run.log"))
        if status != 0:
            raise RuntimeError("workload fails: " + err.strip()[-400:])
    probe = os.path.join(directory, "probe.ccmetrics")
    status, _, _ = lab.run_command(lab.pin() + lab.workload(out, ["-fmetrics-out=" + probe]), log=os.path.join(directory, "metrics-probe.log"))
    capabilities["metrics_out"] = status == 0 and os.path.exists(probe)
    capabilities["metrics_out_measured"] = bool(capabilities["metrics_out"] and measured_input(parse_cc_metrics(read_text(probe) or "")))
    lab.save_meta()
    for index in range(warmups):
        started = time.monotonic()
        status, _, err = lab.run_command(lab.pin() + lab.workload(out, compile_flags(lab, os.path.join(directory, "warmup.ccmetrics"))),
                                         log=os.path.join(directory, "warmup-%d.log" % index))
        if status != 0:
            raise RuntimeError("warm-up failed: " + err.strip()[-400:])
        print("[timed] warm-up %.2f s; %d timed runs take about %.1f min" % (
            time.monotonic() - started, runs, (time.monotonic() - started) * runs / 60), flush=True)
    reference = os.path.join(directory, "reference.exe")
    shutil.copyfile(out, reference)
    records, best = [], None
    for index in range(1, runs + 1):
        csv_path = os.path.join(directory, "run-%04d.csv" % index)
        flags = compile_flags(lab, os.path.join(directory, "run-%04d.ccmetrics" % index))
        status, out_text, err = lab.run_command(lab.pin() + [lab.perf, "stat", "-x,", "-o", csv_path, "-e", ",".join(TIMED_EVENTS), "--"]
                                                + lab.workload(out, flags))
        identical = status == 0 and filecmp.cmp(reference, out, shallow=False)
        if status != 0:
            with open(os.path.join(directory, "run-%04d.err" % index), "w") as handle:
                handle.write(out_text[-20000:] + err[-20000:])
        seconds = ratio(stat_values(parse_stat_csv(read_text(csv_path) or "")).get("duration_time"), 1e9)
        if seconds is not None and status == 0:
            best = seconds if best is None else min(best, seconds)
        records.append({"run": index, "exit": status, "identical": identical})
        print("[timed] %d/%d %s s (min %s)%s" % (index, runs, fmt(seconds, ".4f"), fmt(best, ".4f"),
                                                 "" if identical else " OUTPUT DIFFERS" if status == 0 else " FAILED"), flush=True)
    with open(os.path.join(directory, "runs.json"), "w") as handle:
        json.dump(records, handle, indent=1)
    return "%d runs, %d identical outputs" % (runs, sum(record["identical"] for record in records))


def step_topdown(lab, groups):
    if not groups:
        return "no metric groups discovered (see env/metricgroups.txt)"
    done = []
    for group in groups:
        base = lab.path("topdown", safe_name(group))
        status, _, _ = lab.run_command(lab.pin() + [lab.perf, "stat", "-r", "3", "-x,", "-o", base + ".csv", "-M", group, "--"]
                                       + lab.workload(lab.path("topdown", "out.exe")), log=base + ".log")
        done.append({"group": group, "exit": status})
        print("[topdown] %s exit=%d" % (group, status), flush=True)
    with open(lab.path("topdown", "groups.json"), "w") as handle:
        json.dump(done, handle, indent=1)
    return "%d/%d groups measured" % (sum(entry["exit"] == 0 for entry in done), len(done))


def step_timeline(lab):
    metrics = lab.path("timeline", "run.ccmetrics")
    flags = compile_flags(lab, metrics)
    status, _, err = lab.run_command(lab.pin() + [lab.perf, "stat", "-I", str(INTERVAL_MS), "-x,", "-o", lab.path("timeline", "interval.csv"),
                                                  "-e", ",".join(INTERVAL_EVENTS), "--"] + lab.workload(lab.path("timeline", "out.exe"), flags),
                                     log=lab.path("timeline", "interval.log"))
    if status != 0:
        raise RuntimeError("interval run failed: " + err.strip()[-400:])
    status2, _, _ = lab.run_command(lab.pin() + [lab.perf, "stat", "-I", str(INTERVAL_MS), "-x,", "-o", lab.path("timeline", "pipeline-l1.csv"),
                                                 "-M", "PipelineL1", "--"] + lab.workload(lab.path("timeline", "out.exe"), compile_flags(lab, lab.path("timeline", "pipeline-l1.ccmetrics"))),
                                    log=lab.path("timeline", "pipeline-l1.log"))
    return "interval run ok; PipelineL1 interval run exit=%d" % status2


def step_sampling(lab):
    directory = lab.directory("sampling")
    out = os.path.join(directory, "out.exe")
    recorded = []
    events = list(SAMPLE_EVENTS)
    for index, (name, event) in enumerate(events):
        data = os.path.join(directory, name + ".data")
        status, _, _ = lab.run_command(lab.pin() + [lab.perf, "record", "-q", "-F", "2999", "-e", event, "--call-graph", "fp", "-o", data, "--"]
                                       + lab.workload(out), log=os.path.join(directory, name + ".record.log"))
        if status != 0 and name == "cycles":
            name, event, data = "cpu-clock", "cpu-clock:u", os.path.join(directory, "cpu-clock.data")
            status, _, _ = lab.run_command(lab.pin() + [lab.perf, "record", "-q", "-F", "2999", "-e", event, "--call-graph", "fp", "-o", data, "--"]
                                           + lab.workload(out), log=os.path.join(directory, name + ".record.log"))
        print("[sampling] %s exit=%d" % (event, status), flush=True)
        if status != 0:
            continue
        recorded.append(name)
        report = [lab.perf, "report", "-i", data, "--stdio", "--no-inline"]
        lab.run_command(report + ["--no-children", "--sort", "symbol", "-g", "none", "--percent-limit", "0.2"],
                        stdout_path=os.path.join(directory, name + ".self.txt"), log=os.path.join(directory, name + ".self.log"))
        if name in ("cycles", "cpu-clock"):
            lab.run_command(report + ["--children", "--sort", "symbol", "-g", "caller", "--percent-limit", "2"],
                            stdout_path=os.path.join(directory, name + ".children.txt"), log=os.path.join(directory, name + ".children.log"))
            lab.run_command(report + ["--no-children", "--sort", "srcline", "-g", "none", "--percent-limit", "0.3"], timeout=SRCLINE_TIMEOUT,
                            stdout_path=os.path.join(directory, name + ".srcline.txt"), log=os.path.join(directory, name + ".srcline.log"))
        top = {"cycles": 5, "cpu-clock": 5, "branch-misses": 3}.get(name, 0)
        symbols = [entry[2] for entry in report_entries(read_text(os.path.join(directory, name + ".self.txt")) or "")][:top]
        for rank, symbol in enumerate(symbols, 1):
            lab.run_command([lab.perf, "annotate", "-i", data, "--stdio", symbol], timeout=300,
                            stdout_path=lab.path("sampling", "annotate", "%s-%d-%s.txt" % (name, rank, safe_name(symbol))),
                            log=lab.path("sampling", "annotate", "%s-%d.log" % (name, rank)))
    data = os.path.join(directory, "faults.data")
    status, _, _ = lab.run_command(lab.pin() + [lab.perf, "record", "-q", "-e", "page-faults:u", "-c", "1", "-d", "--call-graph", "fp", "-o", data, "--"]
                                   + lab.workload(out), log=os.path.join(directory, "faults.record.log"))
    print("[sampling] page-faults exit=%d" % status, flush=True)
    if status == 0:
        recorded.append("faults")
        report = [lab.perf, "report", "-i", data, "--stdio", "--no-inline", "--no-children", "--sort", "symbol"]
        lab.run_command(report + ["-g", "none", "--percent-limit", "0.2"], stdout_path=os.path.join(directory, "faults.self.txt"))
        lab.run_command(report + ["-g", "caller", "--percent-limit", "1"], stdout_path=os.path.join(directory, "faults.callers.txt"))
        lab.run_command([lab.perf, "script", "-i", data, "-F", "time,addr,ip,sym", "--hide-call-graph", "--no-inline", "--show-mmap-events"],
                        stdout_path=os.path.join(directory, "faults.script.txt"), log=os.path.join(directory, "faults.script.log"))
    if not recorded:
        raise RuntimeError("no event could be recorded")
    return "recorded: " + ", ".join(recorded)


def step_ibs(lab):
    if not lab.sudo:
        return None
    present = [name for name in ("ibs_op", "ibs_fetch") if os.path.isdir("/sys/bus/event_source/devices/" + name)]
    if not present:
        raise RuntimeError("no ibs_op/ibs_fetch PMU in /sys/bus/event_source/devices")
    if subprocess.call(["sudo", "-v"]) != 0:
        raise RuntimeError("sudo -v failed")
    directory = lab.directory("ibs")
    drop = ["sudo", "-u", "#%d" % os.getuid(), "-g", "#%d" % os.getgid(), "--"]
    cpu = ["-C", str(lab.cpu)] if lab.cpu is not None else ["-a"]
    command = drop + lab.pin() + lab.workload(os.path.join(directory, "out.exe"))
    comm = os.path.basename(lab.ide)[:15]
    notes = []
    for pmu in present:
        data = os.path.join(directory, pmu + ".data")
        status, _, _ = lab.run_command(["sudo", lab.perf, "record", "-q", "-e", pmu + "//", "-o", data] + cpu + ["--"] + command,
                                       log=os.path.join(directory, pmu + ".record.log"))
        notes.append("%s exit=%d" % (pmu, status))
    data = os.path.join(directory, "mem.data")
    status, _, _ = lab.run_command(["sudo", lab.perf, "mem", "record", "-o", data] + cpu + ["--"] + command,
                                   log=os.path.join(directory, "mem.record.log"))
    notes.append("perf mem exit=%d" % status)
    lab.run_command(["sudo", "chown", "-R", "%d:%d" % (os.getuid(), os.getgid()), directory])
    for pmu in present:
        lab.run_command([lab.perf, "report", "-i", os.path.join(directory, pmu + ".data"), "--stdio", "--no-inline", "--comm", comm,
                         "--no-children", "--sort", "symbol", "-g", "none", "--percent-limit", "0.3"],
                        stdout_path=os.path.join(directory, pmu + ".self.txt"), log=os.path.join(directory, pmu + ".report.log"))
    lab.run_command([lab.perf, "mem", "report", "-i", data, "--stdio", "--comm", comm, "--sort=mem,sym", "--percent-limit", "0.3"],
                    stdout_path=os.path.join(directory, "mem.report.txt"), log=os.path.join(directory, "mem.report.log"))
    return "; ".join(notes)


def step_micro(lab):
    status, out, err = lab.run_command(lab.pin() + [lab.ide, "bench"], log=lab.path("micro", "bench.log"),
                                       stdout_path=lab.path("micro", "bench.txt"))
    if status != 0 or "BENCH_C_FRONTEND" not in out:
        lab.meta["capabilities"]["bench"] = False
        return "unsupported: `ide bench` printed no BENCH_C_FRONTEND line (exit %d)" % status
    lab.meta["capabilities"]["bench"] = True
    return "ok"


# ---------------------------------------------------------------- report rendering

def fmt(value, spec=",.0f"):
    if value is None:
        return "NA"
    return format(value, spec)


def percent(value):
    return "NA" if value is None else "%.1f%%" % (value * 100)


def table(header, rows):
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(str(cell).replace("|", "\\|") for cell in row) + " |" for row in rows]
    return lines


def fenced(text, limit=60):
    lines = (text or "").rstrip("\n").splitlines()
    clipped = lines[:limit] + (["... (%d more lines in the raw file)" % (len(lines) - limit)] if len(lines) > limit else [])
    return ["```text"] + clipped + ["```"]


def top_entries(text, limit):
    return [entry for entry in report_entries(text or "")][:limit]


def render_env(directory, findings):
    facts = json.loads(read_text(os.path.join(directory, "env", "env.json")) or "{}")
    if not facts:
        return ["No environment record."]
    keys = ["date", "command", "cpu", "cpu_model", "microcode", "kernel", "perf_version", "clang", "governor", "epp",
            "scaling_driver", "cur_khz", "max_khz", "boost", "smt_active", "smt_siblings", "perf_event_paranoid",
            "nmi_watchdog", "loadavg", "kernel_cmdline", "ide", "ide_sha256", "git_revision", "git_dirty_files", "ibs_pmus"]
    rows = [(key, "`%s`" % facts.get(key, "NA")) for key in keys]
    lines = table(["fact", "value"], rows)
    lines += ["", "Metric groups selected for the top-down step: %s (all groups: `env/metricgroups.txt`)." % (
        ", ".join(facts.get("metric_groups") or []) or "none")]
    if facts.get("nmi_watchdog", "0").strip() == "1":
        lines.append("The NMI watchdog holds one PMC; groups of six hardware events will multiplex.")
    return lines


def load_timed(directory):
    base = os.path.join(directory, "timed")
    records = json.loads(read_text(os.path.join(base, "runs.json")) or "[]")
    runs = []
    for record in records:
        values = stat_values(parse_stat_csv(read_text(os.path.join(base, "run-%04d.csv" % record["run"])) or ""))
        metrics = parse_cc_metrics(read_text(os.path.join(base, "run-%04d.ccmetrics" % record["run"])) or "")
        runs.append({**record, "values": values, "metrics": metrics})
    return runs


def render_timed(directory, findings):
    runs = load_timed(directory)
    good = [run for run in runs if run["exit"] == 0]
    if not good:
        return ["No successful timed run."]
    source = parse_key_values(read_text(os.path.join(directory, "timed", "source.metrics")) or "")
    work_bytes = source.get("lexed.translated_bytes")
    tokens = source.get("preprocessed.tokens")

    def column(name):
        return [run["values"].get(name) for run in good]
    wall = [ratio(value, 1e9) for value in column("duration_time")]
    summary = summarize(wall)
    instructions, cycles = summarize(column("instructions")), summarize(column("cycles"))
    task = summarize(column("task-clock"))
    lines = ["%d runs (%d failed), %d byte-identical to the warm-up output%s." % (
        len(runs), len(runs) - len(good), sum(run["identical"] for run in runs),
        "" if all(run["identical"] for run in good) else " **(nondeterministic output: see timed/runs.json)**")]
    if summary:
        lines += [""] + table(["wall (s)", "min", "p10", "median", "p90", "max", "MAD", "mean"],
                              [["", *(fmt(summary[key], ".4f") for key in ("min", "p10", "median", "p90", "max", "mad", "mean"))]])
        tenths = tenth_medians(wall)
        lines += ["", "- min-vs-median gap: %s; MAD/median: %s; runs since last new minimum: %s" % (
            percent(ratio(summary["median"] - summary["min"], summary["min"])), percent(ratio(summary["mad"], summary["median"])),
            runs_since_minimum(wall)),
                  "- median per tenth of the series (drift): " + " ".join(fmt(value, ".4f") for value in tenths)]
    median_ipc = ratio(instructions and instructions["median"], cycles and cycles["median"])
    per_run_ipc = summarize([ratio(run["values"].get("instructions"), run["values"].get("cycles")) for run in good])
    ghz = summarize([ratio(run["values"].get("cycles"), run["values"].get("task-clock"), 1e-6) for run in good])
    lines += ["- instructions:u median %s, spread (max-min)/median %s" % (
        fmt(instructions and instructions["median"]),
        percent(ratio(instructions and instructions["max"] - instructions["min"], instructions and instructions["median"]))),
        "- cycles:u median %s; IPC %s (per-run median %s); effective clock %s GHz (cycles / task-clock); task-clock median %s ms" % (
        fmt(cycles and cycles["median"]), fmt(median_ipc, ".3f"), fmt(per_run_ipc and per_run_ipc["median"], ".3f"),
        fmt(ghz and ghz["median"], ".3f"), fmt(task and task["median"], ".1f"))]
    misses = column("branch-misses")
    lines.append("- branch-misses:u median %s (MPKI %s); first 3 %s, last 3 %s (a falling trend means the predictor is learning across runs)" % (
        fmt(summarize(misses) and summarize(misses)["median"]),
        fmt(ratio(summarize(misses) and summarize(misses)["median"], instructions and instructions["median"], 1000), ".2f"),
        [fmt(value) for value in misses[:3]], [fmt(value) for value in misses[-3:]]))
    for name in ("page-faults", "minor-faults", "major-faults"):
        stats = summarize(column(name))
        lines.append("- %s per run: median %s, min %s, max %s" % (name, fmt(stats and stats["median"]), fmt(stats and stats["min"]), fmt(stats and stats["max"])))
    if work_bytes and summary:
        lines.append("- work (-fsource-metrics): %s translated bytes, %s code lines, %s tokens; min %s ns/byte, %s MB/s; median %s ns/byte, %s MB/s" % (
            fmt(work_bytes), fmt(source.get("lexed.code_lines")), fmt(tokens), fmt(summary["min"] * 1e9 / work_bytes, ".3f"),
            fmt(work_bytes / summary["min"] / 1e6, ".2f"), fmt(summary["median"] * 1e9 / work_bytes, ".3f"), fmt(work_bytes / summary["median"] / 1e6, ".2f")))
        lines.append("- instructions per byte %s, per token %s" % (fmt(ratio(instructions and instructions["median"], work_bytes), ".3f"),
                                                                  fmt(ratio(instructions and instructions["median"], tokens), ".3f")))
    else:
        lines.append("- work denominators: NA (binary without -fsource-metrics)")
    records = [measured_input(run["metrics"]) for run in good]
    records = [record for record in records if record]
    if records:
        total = statistics.median(record["total_ns"] for record in records)
        rows = []
        medians = {}
        for phase in PHASES:
            medians[phase] = statistics.median(record.get(phase + "_ns", 0) for record in records)
            rows.append([phase, fmt(medians[phase] / 1e6, ",.2f"), percent(ratio(medians[phase], total))])
        rows.append(["input total", fmt(total / 1e6, ",.2f"), "100.0%"])
        headers = [run["metrics"]["header"] for run in good if run["metrics"]["header"]]
        lines += ["", "Per-phase median over %d `-fmetrics-out` records:" % len(records), ""] + table(["phase", "median ms", "share of input"], rows)
        lines.append("")
        lines.append("- arena peak median %s bytes; arena retained median %s; peak RSS median %s bytes; driver wall_ns median %s ms" % (
            fmt(statistics.median(record.get("arena_peak_bytes", 0) for record in records)),
            fmt(statistics.median(record.get("arena_retained_bytes", 0) for record in records)),
            fmt(statistics.median(header.get("peak_rss_bytes", 0) for header in headers) if headers else None),
            fmt(statistics.median(header.get("wall_ns", 0) for header in headers) / 1e6 if headers else None, ",.1f")))
        slowest = max(PHASES, key=lambda phase: medians[phase])
        findings.append("Slowest phase (timed median): **%s** %.1f ms = %s of the input (timed/run-*.ccmetrics)" % (
            slowest, medians[slowest] / 1e6, percent(ratio(medians[slowest], total))))
    else:
        lines += ["", "Phase breakdown: NA -- the binary %s." % _metrics_reason(directory)]
    if summary:
        findings.append("Wall min %.4f s / median %.4f s, IPC %s, %s MB/s at the minimum (timed/)" % (
            summary["min"], summary["median"], fmt(median_ipc, ".3f"), fmt(work_bytes / summary["min"] / 1e6, ".2f") if work_bytes else "NA"))
    return lines


def _metrics_reason(directory):
    capabilities = load_meta(directory).get("capabilities", {})
    if not capabilities.get("metrics_out"):
        return "does not accept -fmetrics-out= (see timed/metrics-probe.log)"
    if not capabilities.get("metrics_out_measured"):
        return "accepts -fmetrics-out= but wrote no measured CC_METRICS_INPUT record for this invocation (timed/probe.ccmetrics)"
    return "wrote no measured record in this step"


def render_topdown(directory, findings):
    groups = json.loads(read_text(os.path.join(directory, "topdown", "groups.json")) or "[]")
    if not groups:
        return ["No metric group measured."]
    lines = []
    for entry in groups:
        base = os.path.join(directory, "topdown", safe_name(entry["group"]))
        rows = parse_stat_csv(read_text(base + ".csv") or "")
        metrics = stat_metrics(rows)
        multiplex = multiplex_percent(rows)
        lines += ["", "**%s** (exit %d, counters running %s of the time%s)" % (
            entry["group"], entry["exit"], "NA" if multiplex is None else "%.1f%%" % multiplex,
            "" if multiplex is None or multiplex >= 99.9 else ", multiplexed"), ""]
        if metrics:
            lines += table(["metric", "value"], [(name, "%s %s" % (fmt(value, ",.3f"), unit)) for name, value, unit in metrics])
        else:
            log = read_text(base + ".log") or ""
            reason = [line for line in log.splitlines() if line.strip() and not line.startswith(("$", "---", "exit="))]
            lines.append("NA (%s)" % (reason[-1].strip() if reason else "no metric values"))
        if re.fullmatch(r"(?i)pipelinel1|topdownl1|tmal1", entry["group"]) and metrics:
            category = dominant_category(metrics)
            if category:
                findings.append("Dominant top-down level-1 category: **%s** %s%% (topdown/%s.csv)" % (category[0], fmt(category[1], ".1f"), safe_name(entry["group"])))
    return lines


def dominant_category(metrics):
    best = None
    for name, value, _ in metrics:
        for category in TOPDOWN_CATEGORIES:
            if name.lower().endswith(category) and value is not None and (best is None or value > best[1]):
                best = (category, value)
    return best


def timeline_data(directory):
    rows = parse_stat_csv(read_text(os.path.join(directory, "timeline", "interval.csv")) or "", interval=True)
    intervals = interval_table(rows)
    metrics = parse_cc_metrics(read_text(os.path.join(directory, "timeline", "run.ccmetrics")) or "")
    total = intervals[-1]["t1"] if intervals else None
    return intervals, metrics, phase_spans(metrics, total), total


def render_timeline(directory, findings):
    intervals, metrics, spans, total = timeline_data(directory)
    if not intervals:
        return ["No interval data."]
    events = [normalize_event(event) for event in INTERVAL_EVENTS]
    with open(os.path.join(directory, "timeline", "intervals.csv"), "w") as handle:
        handle.write("t0_s,t1_s," + ",".join(events) + ",ipc\n")
        for interval in intervals:
            values = interval["values"]
            cells = ["" if values.get(event) is None else "%g" % values[event] for event in events]
            ipc = ratio(values.get("instructions"), values.get("cycles"))
            handle.write("%.6f,%.6f,%s,%s\n" % (interval["t0"], interval["t1"], ",".join(cells), "" if ipc is None else "%.4f" % ipc))
    svg = timeline_svg(intervals, spans)
    with open(os.path.join(directory, "timeline", "timeline.svg"), "w") as handle:
        handle.write(svg)
    with open(os.path.join(directory, "timeline", "timeline.html"), "w") as handle:
        handle.write(timeline_html(svg, spans))
    multiplex = [min((pct for pct in interval["pct"].values() if pct is not None), default=100.0) for interval in intervals]
    lines = ["%d intervals of %d ms over %.3f s; smallest percent-running %.1f%%. Files: timeline/intervals.csv, timeline/timeline.svg, timeline/timeline.html, raw timeline/interval.csv." % (
        len(intervals), INTERVAL_MS, total, min(multiplex))]
    if not spans:
        lines.append("Phase attribution: NA -- the binary %s." % _metrics_reason(directory))
        return lines
    error = alignment_error(metrics, total)
    lines.append("Phase attribution splits each interval in proportion to its overlap with each phase. Alignment: perf's time zero is "
                 "the workload exec, the compiler's clock starts after argument parsing; %s ms of the run lie outside the compiler's "
                 "clock (start-up + teardown), which bounds how early the phase boundaries can be drawn. Interval granularity adds up "
                 "to %d ms of smearing at each boundary." % (fmt(error and error * 1e3, ".2f"), INTERVAL_MS))
    attributed = attribute_intervals(intervals, spans)
    rows = []
    for name, start, end in spans:
        values = attributed[name]
        instructions = values.get("instructions")
        rows.append([name, fmt(start * 1e3, ",.1f"), fmt((end - start) * 1e3, ",.1f"), fmt(instructions),
                     fmt(ratio(instructions, values.get("cycles")), ".3f"),
                     fmt(ratio(values.get("branch-misses"), instructions, 1000), ".2f"),
                     fmt(ratio(values.get("L1-icache-load-misses"), instructions, 1000), ".2f"),
                     fmt(ratio(values.get("L1-dcache-load-misses"), instructions, 1000), ".2f"),
                     fmt(ratio(values.get("dTLB-load-misses"), instructions, 1000), ".2f"),
                     fmt(values.get("minor-faults")), fmt(values.get("major-faults")),
                     fmt(ratio(values.get("minor-faults"), (end - start) * 1e3), ",.0f")])
    lines += [""] + table(["phase", "start ms", "ms", "instructions", "IPC", "br MPKI", "L1I MPKI", "L1D MPKI", "dTLB MPKI",
                           "minor faults", "major faults", "faults/ms"], rows)
    faulting = [(attributed[name].get("minor-faults") or 0, name) for name, _, _ in spans]
    if any(count for count, _ in faulting):
        count, name = max(faulting)
        findings.append("Most minor faults by phase: **%s** (%s faults, timeline phase table)" % (name, fmt(count)))
    lines += ["", "The `-M PipelineL1` interval run is kept raw: timeline/pipeline-l1.csv."]
    return lines


def timeline_svg(intervals, spans, width=960):
    """Two stacked panels sharing the time axis: IPC, then minor faults per interval."""
    total = max(interval["t1"] for interval in intervals) or 1.0
    left, right, panel, gap, top = 56, 16, 130, 46, 34
    height = top + 2 * panel + gap + 40
    plot = width - left - right

    def x(seconds):
        return left + plot * seconds / total
    ipc = [ratio(interval["values"].get("instructions"), interval["values"].get("cycles")) for interval in intervals]
    faults = [interval["values"].get("minor-faults") for interval in intervals]
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="system-ui,sans-serif" font-size="11">' % (width, height, width, height),
             '<style>.ink{fill:#0b0b0b}.muted{fill:#52514e}.band{fill:#52514e;opacity:.08}.axis{stroke:#c3c2b7}'
             '.ipc{stroke:#2a78d6;fill:none;stroke-width:2}.bar{fill:#eb6834}'
             '@media (prefers-color-scheme: dark){.ink{fill:#fff}.muted{fill:#c3c2b7}.band{fill:#c3c2b7;opacity:.10}'
             '.axis{stroke:#52514e}.ipc{stroke:#3987e5}.bar{fill:#d95926}}</style>']
    for index, (name, start, end) in enumerate(spans):
        if index % 2 == 0:
            parts.append('<rect class="band" x="%.1f" y="%d" width="%.1f" height="%d"/>' % (x(start), top, max(0.5, x(end) - x(start)), 2 * panel + gap))
        if x(end) - x(start) > len(name) * 6.5 + 6:
            parts.append('<text class="muted" x="%.1f" y="%d" text-anchor="middle">%s</text>' % ((x(start) + x(end)) / 2, top - 6, html.escape(name)))
    panels = [("IPC (instructions:u / cycles:u)", ipc, top, "line"), ("minor faults per %d ms interval" % INTERVAL_MS, faults, top + panel + gap, "bar")]
    for title, values, base, kind in panels:
        known = [value for value in values if value is not None]
        parts.append('<text class="ink" x="%d" y="%d" font-weight="600">%s</text>' % (left, base + 12, html.escape(title)))
        parts.append('<line class="axis" x1="%d" x2="%d" y1="%d" y2="%d"/>' % (left, width - right, base + panel, base + panel))
        if not known:
            parts.append('<text class="muted" x="%d" y="%d">NA (not counted on this machine)</text>' % (left + 8, base + panel / 2))
            continue
        peak = max(known) or 1.0
        parts.append('<text class="muted" x="%d" y="%d" text-anchor="end">%s</text>' % (left - 6, base + 22, fmt(peak, ".2f" if kind == "line" else ",.0f")))
        parts.append('<text class="muted" x="%d" y="%d" text-anchor="end">0</text>' % (left - 6, base + panel))

        def y(value):
            return base + panel - (panel - 20) * value / peak
        if kind == "line":
            points = ["%.1f,%.1f" % ((x(interval["t0"]) + x(interval["t1"])) / 2, y(value)) for interval, value in zip(intervals, values) if value is not None]
            parts.append('<polyline class="ipc" points="%s"/>' % " ".join(points))
        for interval, value in zip(intervals, values):
            if value is None:
                continue
            x0, x1 = x(interval["t0"]), x(interval["t1"])
            label = "%.0f-%.0f ms: %s" % (interval["t0"] * 1e3, interval["t1"] * 1e3, fmt(value, ".3f" if kind == "line" else ",.0f"))
            if kind == "bar":
                parts.append('<rect class="bar" x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="1"><title>%s</title></rect>' % (
                    x0 + 1, y(value), max(0.5, x1 - x0 - 2), base + panel - y(value), label))
            else:
                parts.append('<rect x="%.1f" y="%d" width="%.1f" height="%d" fill="transparent"><title>%s</title></rect>' % (x0, base, max(0.5, x1 - x0), panel, label))
    for tick in range(6):
        seconds = total * tick / 5
        parts.append('<text class="muted" x="%.1f" y="%d" text-anchor="middle">%.2f s</text>' % (x(seconds), height - 14, seconds))
    parts.append("</svg>")
    return "\n".join(parts)


def timeline_html(svg, spans):
    rows = "".join("<tr><td>%s</td><td>%.1f</td><td>%.1f</td></tr>" % (html.escape(name), start * 1e3, (end - start) * 1e3) for name, start, end in spans)
    return ("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Compile timeline</title><style>:root{color-scheme:light dark}body{margin:16px;font-family:system-ui,sans-serif;"
            "background:#fcfcfb;color:#0b0b0b}svg{max-width:100%%;height:auto}td,th{padding:2px 10px;text-align:right}"
            "@media (prefers-color-scheme: dark){body{background:#1a1a19;color:#fff}}</style></head><body>"
            "<h1>Compile timeline</h1>%s<table><tr><th>phase</th><th>start ms</th><th>ms</th></tr>%s</table></body></html>\n") % (svg, rows)


def fault_summary(text, buckets=20):
    mmaps, faults = parse_fault_script(text or "")
    if not faults:
        return None
    regions = {}
    for _, address, _, _ in faults:
        label = classify_address(address, mmaps)
        regions[label] = regions.get(label, 0) + 1
    first, last = min(fault[0] for fault in faults), max(fault[0] for fault in faults)
    width = max((last - first) / buckets, 1e-9)
    timeline = [0] * buckets
    for moment, _, _, _ in faults:
        timeline[min(buckets - 1, int((moment - first) / width))] += 1
    return {"count": len(faults), "regions": sorted(regions.items(), key=lambda item: -item[1]),
            "bucket_ms": width * 1e3, "timeline": timeline, "span_ms": (last - first) * 1e3}


def render_sampling(directory, findings):
    base = os.path.join(directory, "sampling")
    lines = ["Samples carry skid (none of these events is precise on AMD without IBS): read symbols as reliable and lines as "
             "\"this loop\". Branch-miss samples land after the mispredicted branch; `tools/branch_miss_survey.py` uses LBR "
             "for exact branches. Percentages are shares of the whole capture."]
    labels = {"cycles": "cycles", "cpu-clock": "cpu-clock (cycles:u unsupported; time-based fallback)", "branch-misses": "branch misses",
              "l1i-misses": "L1I misses", "l1d-misses": "L1D misses", "dtlb-misses": "dTLB misses"}
    found = False
    for name, label in labels.items():
        text = read_text(os.path.join(base, name + ".self.txt"))
        if text is None:
            if name != "cpu-clock":
                lines += ["", "**%s**: NA (%s)" % (label, _record_failure(os.path.join(base, name + ".record.log")))]
            continue
        found = True
        entries = top_entries(text, 15)
        sampled = [event for event in parse_report(text) if event]
        if sampled and normalize_event(sampled[0]) != normalize_event(dict(SAMPLE_EVENTS).get(name, sampled[0])):
            label += " (perf fell back to event '%s')" % sampled[0]
        lines += ["", "**Top self symbols by %s** (sampling/%s.self.txt)" % (label, name), ""]
        lines += table(["share", "symbol"], [("%.2f%%" % entry[0], "`%s`" % entry[2]) for entry in entries]) if entries else ["no samples"]
        if entries:
            findings.append("Hot by %s: %s (sampling/%s.self.txt)" % (label.replace(" (cycles:u unsupported; time-based fallback)", ""), ", ".join(
                "`%s` %.1f%%" % (entry[2], entry[0]) for entry in entries[:5]), name))
        annotations = sorted(file for file in os.listdir(os.path.join(base, "annotate")) if file.startswith(name + "-") and file.endswith(".txt")) \
            if os.path.isdir(os.path.join(base, "annotate")) else []
        for file in annotations:
            hottest = parse_annotate(read_text(os.path.join(base, "annotate", file)) or "", 5)
            lines += ["", "Hottest instructions in sampling/annotate/%s:" % file, ""]
            lines += table(["share of symbol", "address", "instruction"], [("%.2f%%" % row[0], row[1], "`%s`" % row[2]) for row in hottest]) if hottest else ["NA (annotate produced no instruction lines; see the matching .log)"]
        children = read_text(os.path.join(base, name + ".children.txt"))
        if children is not None:
            entries = [entry for entry in report_entries(children) if entry[1] is not None][:20]
            lines += ["", "**Inclusive (children) time** (call trees in sampling/%s.children.txt)" % name, ""]
            lines += table(["children", "self", "symbol"], [("%.2f%%" % entry[0], "%.2f%%" % entry[1], "`%s`" % entry[2]) for entry in entries])
        srcline = read_text(os.path.join(base, name + ".srcline.txt"))
        if srcline is not None:
            entries = top_entries(srcline, 20)
            lines += ["", "**Top source lines** (sampling/%s.srcline.txt)" % name, ""]
            lines += table(["share", "line"], [("%.2f%%" % entry[0], "`%s`" % entry[2]) for entry in entries]) if entries else ["NA (see %s.srcline.log)" % name]
    text = read_text(os.path.join(base, "faults.self.txt"))
    if text is None:
        lines += ["", "**Page faults**: NA (%s)" % _record_failure(os.path.join(base, "faults.record.log"))]
    else:
        found = True
        entries = top_entries(text, 15)
        lines += ["", "**Page faults by symbol** (every fault recorded; callers in sampling/faults.callers.txt)", ""]
        lines += table(["share", "symbol"], [("%.2f%%" % entry[0], "`%s`" % entry[2]) for entry in entries])
        if entries:
            findings.append("Top fault sites: %s (sampling/faults.callers.txt)" % ", ".join("`%s` %.1f%%" % (entry[2], entry[0]) for entry in entries[:5]))
        summary = fault_summary(read_text(os.path.join(base, "faults.script.txt")))
        if summary:
            root = load_meta(directory).get("config", {}).get("repo_root", "")
            lines += ["", "%s faults over %.1f ms. Faulting data addresses by mapping (protection as mapped: perf does not see "
                      "mprotect, so a `---p` reservation that faults was committed later):" % (fmt(summary["count"]), summary["span_ms"]), ""]
            lines += table(["faults", "share", "region"], [(fmt(count), percent(count / summary["count"]), "`%s`" % (label.replace(root + "/", "") if root else label))
                                                           for label, count in summary["regions"][:12]])
            lines += ["", "Fault timeline (%.1f ms buckets from the first fault): %s" % (summary["bucket_ms"], " ".join(str(count) for count in summary["timeline"]))]
            if summary["regions"]:
                findings.append("Most-faulted region: `%s` (%s of faults)" % (summary["regions"][0][0], percent(summary["regions"][0][1] / summary["count"])))
    if not found:
        lines.append("No capture produced a report.")
    return lines


def _record_failure(log_path):
    text = read_text(log_path)
    if text is None:
        return "not recorded"
    reason = [line.strip() for line in text.splitlines() if line.strip() and not line.startswith(("$", "---", "exit="))]
    return reason[-1][:200] if reason else "failed"


def render_ibs(directory, findings):
    base = os.path.join(directory, "ibs")
    if not os.path.isdir(base):
        return ["Not run (needs --sudo and an AMD IBS PMU)."]
    lines = ["IBS tags individual micro-ops (op) and fetches, so attribution is precise; recorded system-wide on the pinned CPU and "
             "filtered to the compiler's command name."]
    for name in ("ibs_op", "ibs_fetch"):
        text = read_text(os.path.join(base, name + ".self.txt"))
        entries = top_entries(text, 15)
        lines += ["", "**%s top symbols** (ibs/%s.self.txt)" % (name, name), ""]
        lines += table(["share", "symbol"], [("%.2f%%" % entry[0], "`%s`" % entry[2]) for entry in entries]) if entries else ["NA (%s)" % _record_failure(os.path.join(base, name + ".record.log"))]
    text = read_text(os.path.join(base, "mem.report.txt"))
    lines += ["", "**perf mem: load source by symbol** (ibs/mem.report.txt)", ""]
    lines += fenced("\n".join(line for line in (text or "").splitlines() if line.strip() and not line.startswith("#")), 30) if text else ["NA (%s)" % _record_failure(os.path.join(base, "mem.record.log"))]
    return lines


def render_micro(directory, findings):
    text = read_text(os.path.join(directory, "micro", "bench.txt"))
    if not text or "BENCH_C_FRONTEND" not in text:
        return ["NA (`ide bench` unsupported or not run)."]
    lines = [line for line in text.splitlines() if "BENCH" in line]
    return ["In-process repetition of the C frontend on tests/basic_c_operations.c. Caveat (Lemire): repeating one identical "
            "input lets the branch predictor learn it, which flatters branchy code; do not compare it with a fresh-process compile."] + fenced("\n".join(lines))


RENDERERS = {"env": ("1. Environment", render_env), "timed": ("2. Timed runs", render_timed),
             "topdown": ("3. Top-down metric groups", render_topdown), "timeline": ("4. Timeline (WHEN)", render_timeline),
             "sampling": ("5. Sampling (WHERE)", render_sampling), "ibs": ("6. IBS and perf mem (sudo)", render_ibs),
             "micro": ("7. Micro-benchmark", render_micro)}


def render_report(directory):
    meta = load_meta(directory)
    findings, body = [], []
    for step in STEPS:
        title, renderer = RENDERERS[step]
        body += ["", "## " + title, ""]
        state = meta.get("steps", {}).get(step)
        if state and state["status"] != "ok":
            body.append("Step status: **%s** %s" % (state["status"], state.get("note", "")))
        if state is None or state["status"] in ("ok", "failed"):
            try:
                body += renderer(directory, findings)
            except Exception as error:  # a malformed raw file must not hide the other sections
                body.append("Rendering failed: %s: %s" % (type(error).__name__, error))
    config = meta.get("config", {})
    head = ["# Micro-architecture lab report", "",
            "Workload: `%s`, pinned to CPU %s, %s timed runs. Raw files are beside this report; `python3 tools/uarch_lab.py report %s` "
            "re-renders it." % (config.get("command", "NA"), config.get("cpu", "NA"), config.get("runs", "NA"), directory), "",
            "## Findings to investigate", "", "Data pointers, not conclusions; each names the raw file behind it.", ""]
    head += ["- " + finding for finding in findings] or ["- none (no step produced data)"]
    head += ["", "## Steps", ""] + table(["step", "status", "elapsed s", "note"], [
        (step, meta.get("steps", {}).get(step, {}).get("status", "not run"), fmt(meta.get("steps", {}).get(step, {}).get("elapsed_s"), ".1f"),
         meta.get("steps", {}).get(step, {}).get("note", "")) for step in STEPS])
    text = "\n".join(head + body) + "\n"
    with open(os.path.join(directory, "report.md"), "w") as handle:
        handle.write(text)
    return text


# ---------------------------------------------------------------- CLI

def command_run(arguments):
    ide = os.path.abspath(arguments.ide)
    if not os.path.isfile(ide):
        sys.exit("uarch_lab: no ide binary at %s" % ide)
    if arguments.cpu is not None and arguments.cpu >= 0 and not shutil.which("taskset"):
        sys.exit("uarch_lab: taskset not found (pass --cpu -1 to run unpinned)")
    cpu = arguments.cpu if arguments.cpu is not None and arguments.cpu >= 0 else None
    extra = arguments.extra[1:] if arguments.extra[:1] == ["--"] else arguments.extra
    lab = Lab(arguments.output, arguments.perf, cpu, ide, arguments.repo_root, extra, arguments.sudo)
    lab.meta["config"] = {"command": shell_join(lab.workload("OUT")), "cpu": cpu, "runs": arguments.runs, "perf": arguments.perf,
                          "repo_root": lab.repo_root, "skip": arguments.skip, "sudo": arguments.sudo}
    lab.save_meta()
    print("uarch_lab: output %s; at ~2 s per compile expect about %.0f min" % (lab.output, (arguments.runs + 70) * 2.2 / 60), flush=True)
    started = time.monotonic()
    for step in STEPS:
        if step in arguments.skip or (step == "ibs" and not arguments.sudo):
            lab.meta["steps"][step] = {"status": "skipped", "elapsed_s": 0.0, "note": "--skip" if step in arguments.skip else "needs --sudo"}
            lab.save_meta()
            continue
        print("uarch_lab: step %s ..." % step, flush=True)
        step_started = time.monotonic()
        try:
            if step == "env":
                note = step_env(lab)
            elif step == "timed":
                note = step_timed(lab, arguments.runs, arguments.warmups)
            elif step == "topdown":
                facts = json.loads(read_text(os.path.join(lab.output, "env", "env.json")) or "{}")
                groups = facts.get("metric_groups")
                if groups is None:
                    status, out, _ = lab.run_command([lab.perf, "list", "metricgroups"], timeout=120)
                    groups = discover_groups(out) if status == 0 else []
                note = step_topdown(lab, groups)
            elif step == "timeline":
                if "timed" in arguments.skip and "metrics_out" not in lab.meta["capabilities"]:
                    probe = lab.path("timeline", "probe.ccmetrics")
                    status, _, _ = lab.run_command(lab.pin() + lab.workload(lab.path("timeline", "out.exe"), ["-fmetrics-out=" + probe]))
                    lab.meta["capabilities"]["metrics_out"] = status == 0 and os.path.exists(probe)
                note = step_timeline(lab)
            elif step == "sampling":
                note = step_sampling(lab)
            elif step == "ibs":
                note = step_ibs(lab)
            else:
                note = step_micro(lab)
            state = {"status": "ok", "note": note or ""}
        except Exception as error:  # optional steps never abort the lab
            state = {"status": "failed", "note": "%s: %s" % (type(error).__name__, error)}
        state["elapsed_s"] = round(time.monotonic() - step_started, 1)
        lab.meta["steps"][step] = state
        lab.save_meta()
        print("uarch_lab: step %s %s in %.1f s %s" % (step, state["status"], state["elapsed_s"], state["note"]), flush=True)
    lab.meta["total_s"] = round(time.monotonic() - started, 1)
    lab.save_meta()
    render_report(lab.output)
    print("uarch_lab: total %.1f s; report %s" % (lab.meta["total_s"], os.path.join(lab.output, "report.md")))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="run the steps and render the report")
    run.add_argument("--ide", default="build/Release/ide")
    run.add_argument("--repo-root", default=".")
    run.add_argument("--cpu", type=int, default=2, help="CPU to pin to (-1: unpinned)")
    run.add_argument("--output", required=True)
    run.add_argument("--runs", type=int, default=30)
    run.add_argument("--warmups", type=int, default=1)
    run.add_argument("--perf", default="perf")
    run.add_argument("--sudo", action="store_true", help="enable the IBS / perf mem step")
    run.add_argument("--skip", nargs="*", default=[], choices=STEPS)
    run.add_argument("extra", nargs=argparse.REMAINDER, help="-- extra compile arguments")
    report = commands.add_parser("report", help="re-render DIR/report.md from raw files")
    report.add_argument("directory")
    arguments = parser.parse_args(argv)
    if arguments.command == "run":
        command_run(arguments)
    else:
        print(render_report(os.path.abspath(arguments.directory)), end="")


if __name__ == "__main__":
    main()
