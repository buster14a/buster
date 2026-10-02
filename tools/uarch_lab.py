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
        --cpu 2 --output /tmp/lab [--target-minutes 15 | --runs N] [--sudo] \\
        [--skip STEP ...] [--perf PATH] [-- extra compile args]
    python3 tools/uarch_lab.py report /tmp/lab [--perf PATH]

Steps (each writes DIR/<step>/ and one report section; any can be skipped):

    env       versions, CPU model/microcode, governor/EPP, SMT, paranoid,
              `perf list metricgroups`, binary sha256, git revision
    timed     warm-up, then N pinned runs under `perf stat -x,` (task-clock,
              cycles, instructions, branch misses, faults), byte-compared
              outputs, `-fsource-metrics` once and `-fmetrics-out` per run if
              supported.  Wall time is the harness's own monotonic span around
              each child (runs.json `span_s`; commands.log for older output
              directories), beside the compiler's `wall_ns` and task-clock:
              perf 7.2.4 reads `duration_time` as 0 whenever it shares the
              event list, so it is not used.  Without --runs, the count is
              fixed once after PILOT_RUNS runs from their wall time so the
              whole lab lands near --target-minutes (choose_run_count)
    topdown   per discovered metric group a cheap `perf stat -M G -- true`
              dry run (groups whose uncore PMU is absent are "unavailable",
              not failures), then `perf stat -r 3 -j -M G` (JSON keeps six
              metric decimals; `-x,` is the fallback and rounds to 0.1).  A
              multiplexed group (counters running < 100%) is re-measured one
              `-M <metric>` per run; the group value stays a flagged fallback.
              Well-known ratios are recomputed from the raw counts
    timeline  one `perf stat -I 20` run plus `-fmetrics-out`: intervals
              attributed to compiler phases (WHEN), CSV and SVG/HTML chart
    sampling  `perf record --call-graph fp` per event (WHERE), every page
              fault with its data address, annotate and srcline listings;
              per-process captures, so no thread filter is needed; self
              reports sort by dso,symbol to name unresolved addresses
    ibs       (--sudo only) AMD IBS op/fetch samples and `perf mem`, recorded
              on the pinned CPU and filtered to the workload's thread ids (the
              compiler renames its main thread `main_thread`, so its exec name
              alone misses nearly every sample); `report DIR` derives these
              filtered reports from the raw *.data when they are missing
    micro     `ide bench` if the binary supports it

A step is `ok` only when its section has real data: the report re-assesses
every section from the raw files and marks it `degraded` with the reasons
(a missing or non-positive time, a report with no rows from a capture that has
samples, a failed group).  Failures are recorded in DIR/lab.json and the
report, never fatal; unsupported counters (`<not supported>`, `<not counted>`)
are NA, never zero, and a rounded perf metric never prints as an exact 0.
The phase breakdown needs a binary that accepts `-fmetrics-out=` and writes a
measured `CC_METRICS_INPUT` record; without it those sections say so.

Map (searchable symbols):
    STEPS, TIMED_EVENTS, INTERVAL_EVENTS, SAMPLE_EVENTS   step and event tables
    parse_stat, parse_stat_csv, parse_stat_json, stat_values  perf stat output
    metric_text, recompute_metric, branch_pair             metric precision
    parse_key_values, parse_cc_metrics                    compiler metric files
    parse_report, parse_annotate, parse_fault_script      perf report/annotate/script
    parse_task_report, workload_filter, parse_mem_levels  IBS workload filter
    command_spans, load_timed, timed_problems             wall time and fail-closed checks
    choose_run_count, estimate_other_compiles             --target-minutes run count
    measure_stat, last_reason, topdown_group_lines        top-down dry run, split, render
    summarize, percentile, runs_since_minimum, tenth_medians   statistics
    phase_spans, interval_table, attribute_intervals      timeline alignment
    Lab, Lab.workload, Lab.run_command                    process execution
    step_env ... step_micro, derive_ibs_reports           the steps (raw files)
    render_env ... render_micro, guarded                  report sections from raw files
    render_section, effective_status, render_report       content-based step status
    timeline_svg                                          self-contained chart
    main                                                  CLI
Tests: tools/uarch_lab_test.py (`python3 -B tools/uarch_lab_test.py`).
"""

import argparse
import filecmp
import hashlib
import html
import json
import math
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
# No duration_time: perf 7.2.4 reads it as 0 whenever it shares the event
# list with another event; wall time is the harness span (see step_timed).
TIMED_EVENTS = ["task-clock", "cycles:u", "instructions:u", "branch-misses:u",
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
# perf's text when a group needs a PMU this host lacks (Zen 5 desktops expose no
# amd_umc/amd_l3 uncore PMU to perf); such groups are unavailable, not failed.
UNAVAILABLE_PMU = re.compile(r"Unable to find PMU or event|Bad event or PMU")
UNCORE_GROUP = re.compile(r"(?i)l3|memory_controller|umc|data_fabric|^df")
# Metrics that divide by duration_time (rates per second): NA when perf read
# duration_time as 0, never a computed 0.
TIME_DIVIDED_METRICS = ("lpm_itlb_l2_reqs",)
RATE_UNIT = re.compile(r"(?i)(per_?sec(ond)?|/s(ec)?)$")
BRANCH_PAIRS = (("ex_ret_brn_misp", "ex_ret_brn"), ("branch-misses", "branches"),
                ("branch-misses", "branch-instructions"))
INSTRUCTION_EVENTS = ("instructions", "ex_ret_instr")
IBS_CAPTURES = ("ibs_op", "ibs_fetch", "mem")
TOPDOWN_CATEGORIES = ("frontend_bound", "bad_speculation", "backend_bound", "retiring")
INTERVAL_MS = 20
# Run-count planning (choose_run_count): pilot runs, clamps, and the later
# steps' cost in compile-equivalents.
PILOT_RUNS = 3
MIN_TIMED_RUNS = 10
MAX_TIMED_RUNS = 2000
TOPDOWN_COMPILES_PER_GROUP = 6
SAMPLING_COMPILES = 14
IBS_COMPILES = 6
# A group whose counters ran less of the time than this is multiplexed and is
# re-measured one metric at a time (step_topdown).
FULL_RUNNING = 99.9
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


def _metric_decimals(text):
    text = text.strip()
    return len(text.split(".", 1)[1]) if "." in text else 0


def split_metric_label(label):
    """`per_1k_instr  l1_dtlb_misses_pti` -> ('per_1k_instr', 'l1_dtlb_misses_pti').

    perf joins a metric's ScaleUnit and name with two spaces; `%  retiring`
    gives ('%', 'retiring'), `insn per cycle` gives ('', 'insn per cycle')."""
    label = label.strip()
    match = re.match(r"^(\S+)\s{2,}(\S.*)$", label)
    if match:
        return match.group(1), match.group(2).strip()
    if label.startswith("%"):
        return "%", label[1:].strip()
    return "", label


def _set_metric(row, text, label):
    value = parse_number(text)
    label = label.strip()
    if value is not None and label != "(null)" and re.search(r"[A-Za-z]", label):
        unit, name = split_metric_label(label)
        row.update(metric_value=value, metric_name=name, metric_unit=unit, metric_raw=text.strip(),
                   metric_decimals=_metric_decimals(text))


def _stat_row(moment, value_text, unit, event, line):
    return {"time": moment, "value": parse_number(value_text), "raw": value_text.strip(), "unit": unit.strip(),
            "event": event.strip(), "variance": None, "runtime": None, "pct_running": None, "metric_value": None,
            "metric_name": "", "metric_unit": "", "metric_raw": "", "metric_decimals": None, "line": line}


def parse_stat_csv(text, interval=False):
    """Rows of `perf stat -x,` output; values that perf did not count are None.

    Field order: [time,] value, unit, event, [variance%,] run time, percent
    running, metric value, metric label.  Metric-only lines (top-down) carry
    empty counter fields and the metric in the last two fields.  The metric
    column is printed with perf's display precision (often one decimal), so
    each row keeps `metric_decimals` for metric_text."""
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
        row = _stat_row(moment, fields[0], fields[1], fields[2], line.strip())
        if rest and rest[0].strip().endswith("%"):
            row["variance"] = parse_number(rest[0].strip()[:-1])
            rest = rest[1:]
        row["runtime"] = parse_number(rest[0]) if rest else None
        row["pct_running"] = parse_number(rest[1]) if len(rest) > 1 else None
        if len(fields) >= 5:
            _set_metric(row, fields[-2], fields[-1])
        if not row["event"] and not row["metric_name"]:
            continue
        rows.append(row)
    return rows


def parse_stat_json(text, interval=False):
    """Rows of `perf stat -j` output (one JSON object per line), shaped like
    parse_stat_csv's.  perf prints `metric-value` with "%f" (six decimals)."""
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if not isinstance(record, dict):
            continue
        moment = record.get("interval") if interval else None
        if interval and not isinstance(moment, (int, float)):
            continue
        row = _stat_row(moment, str(record.get("counter-value", "")), str(record.get("unit", "")),
                        str(record.get("event", "")), line)
        for key, field in (("variance", "variance"), ("runtime", "event-runtime"), ("pct_running", "pcnt-running")):
            value = record.get(field)
            row[key] = parse_number(str(value)) if value is not None else None
        _set_metric(row, str(record.get("metric-value", "")), str(record.get("metric-unit", "")))
        if not row["event"] and not row["metric_name"]:
            continue
        rows.append(row)
    return rows


def parse_stat(text, interval=False):
    """parse_stat_json or parse_stat_csv, whichever format `text` is in."""
    for line in (text or "").splitlines():
        if line.strip() and not line.startswith("#"):
            if line.lstrip().startswith("{"):
                return parse_stat_json(text, interval)
            break
    return parse_stat_csv(text or "", interval)


def stat_values(rows):
    """Normalized event name -> value (None when perf could not count it)."""
    values = {}
    for row in rows:
        if row["event"]:
            values[normalize_event(row["event"])] = row["value"]
    return values


def stat_lines(rows):
    """Normalized event name -> the raw output line (quoted in problem notes)."""
    return {normalize_event(row["event"]): row["line"] for row in rows if row["event"]}


def metric_rows(rows):
    """The first row of each distinct metric, in output order."""
    chosen, seen = [], set()
    for row in rows:
        name = row["metric_name"]
        if name and name not in seen:
            seen.add(name)
            chosen.append(row)
    return chosen


def stat_metrics(rows):
    return [(row["metric_name"], row["metric_value"], row["metric_unit"]) for row in metric_rows(rows)]


def significant(value, digits=4):
    if value is None or not math.isfinite(value):
        return "NA"
    if value == 0:
        return "0"
    if abs(value) >= 10 ** digits:
        return format(value, ",.0f")
    return "%.*g" % (digits, value)


def metric_text(value, decimals):
    """A perf metric value at the precision perf printed it with.

    `-x,` metric columns carry one to three decimals, so a CSV value is shown
    with "rounded to 0.1" and a printed 0.0 becomes "< 0.05", never an exact
    zero; JSON values (six decimals) print with four significant digits."""
    if value is None or not math.isfinite(value):
        return "NA"
    if decimals is None:
        return significant(value)
    step = 10.0 ** -decimals
    if value == 0:
        return "< %g (perf printed %.*f; rounded to %g)" % (step / 2, decimals, 0.0, step)
    if decimals >= 6:
        return significant(value)
    return "%.*f (rounded to %g)" % (decimals, value, step)


def branch_pair(values):
    """(mispredicted, branches, names) from raw counts in one output, or None."""
    for misses, branches in BRANCH_PAIRS:
        if values.get(misses) is not None and values.get(branches):
            return values[misses], values[branches], misses, branches
    return None


def instruction_count(values, fallback=None):
    """(count, label): instructions from the same output, else the fallback."""
    for event in INSTRUCTION_EVENTS:
        if values.get(event):
            return values[event], event
    return fallback


def recompute_metric(row, values, instructions, alone=False):
    """(value, formula) for a metric the raw counts of the same output
    determine, else None.  instructions is instruction_count's result.

    perf attaches `metric-value` to whichever event prints first, and many
    Zen 5 metrics sum several events, so the event on the metric's line is
    not its numerator.  A per-1k-instruction metric is recomputed only from
    a run that measured that metric alone (alone=True), where every
    non-instruction event belongs to it; a group run cannot assign events
    to metrics without the metric expression and yields None.  A per-branch
    rate is the named mispredict/branch pair, which is exact either way."""
    unit = row["metric_unit"]
    if unit == "per_branch":
        pair = branch_pair(values)
        if pair:
            return pair[0] / pair[1], "%s / %s" % (pair[2], pair[3])
    if unit == "per_1k_instr" and alone and instructions and instructions[0]:
        events = sorted(event for event, value in values.items()
                        if value is not None and event not in INSTRUCTION_EVENTS and event != "duration_time")
        if events:
            total = sum(values[event] for event in events)
            return total * 1000.0 / instructions[0], "(%s) / %s x 1000" % (" + ".join(events), instructions[1])
    return None


def time_divided(row):
    """True for a metric that divides by duration_time (a per-second rate)."""
    return (row["metric_name"] in TIME_DIVIDED_METRICS or bool(RATE_UNIT.search(row["metric_unit"] or ""))
            or normalize_event(row["event"]) == "duration_time")


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
            name = match.group(3)
            # `--sort dso,symbol`: keep the dso only to name an unresolved
            # address ("ide 0x18f5ce" rather than an anonymous hex value).
            with_dso = re.match(r"^(\S+)\s+\[[.kgu]\]\s+(.*)$", name)
            if with_dso:
                name = with_dso.group(2)
                if re.match(r"^0x[0-9a-f]+\b", name):
                    name = "%s %s" % (with_dso.group(1), re.sub(r"^0x0*(?=[0-9a-f])", "0x", name))
            name = re.sub(r"^\[[.kgu]\]\s+", "", name)
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


TASK_LINE = re.compile(r"^\s*(\d+(?:\.\d+)?)%\s+(\d+):(\S(?:.*?\S)?)\s{2,}(\S.*?)\s*$")


def parse_task_report(text):
    """`perf report --sort pid,dso` -> [(percent, tid, comm, dso)].

    perf's "pid" sort key prints `tid:comm` (the thread's last command name)."""
    rows = []
    for line in (text or "").splitlines():
        if line.startswith("#"):
            continue
        match = TASK_LINE.match(line)
        if match:
            rows.append((float(match.group(1)), int(match.group(2)), match.group(3), match.group(4)))
    return rows


def workload_filter(task_rows, basename):
    """perf report options that keep the workload's samples of a system-wide capture.

    The workload's threads are the tids with samples in its own binary; their
    command names (plus the exec name, before the compiler renames its main
    thread `main_thread`) exclude pre-exec sudo/taskset samples of the same
    tid.  Without such a tid, fall back to the command names alone."""
    chosen = [row for row in task_rows if os.path.basename(row[3]) == basename]
    if chosen:
        tids = sorted({row[1] for row in chosen})
        comms = sorted({row[2] for row in chosen} | {basename[:15]})
        return {"method": "tid", "tids": tids, "comms": comms,
                "options": ["--tid", ",".join(str(tid) for tid in tids), "--comms", ",".join(comms)]}
    comms = [basename[:15], "main_thread"]
    return {"method": "comm-fallback", "tids": [], "comms": comms, "options": ["--comms", ",".join(comms)]}


def parse_mem_levels(text):
    """`perf mem report -n --sort=mem` -> [(weighted percent, samples, level)]."""
    rows = []
    for line in (text or "").splitlines():
        if line.startswith("#"):
            continue
        match = re.match(r"^\s*(\d+\.\d+)%\s+(\d+)\s+(\S.*?)\s*$", line)
        if match:
            rows.append((float(match.group(1)), int(match.group(2)), match.group(3)))
    return rows


COMMAND_LINE = re.compile(r"^(?P<argv>.*) exit=(?P<exit>-?\d+) (?P<seconds>\d+(?:\.\d+)?)s$")


def command_spans(text):
    """{timed run number: seconds} from commands.log, whose lines end with the
    harness's monotonic span (`... exit=0 1.602s`); the last line per run wins."""
    spans = {}
    for line in (text or "").splitlines():
        match = COMMAND_LINE.match(line.rstrip())
        if not match or " stat " not in match.group("argv"):
            continue
        run = re.search(r"timed/run-(\d+)\.csv", match.group("argv"))
        if run:
            spans[int(run.group(1))] = float(match.group("seconds"))
    return spans


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


MMAP_LINE = re.compile(r"(?:(\d+\.\d+):\s+)?PERF_RECORD_MMAP2? \d+/\d+: \[(0x[0-9a-f]+)\((0x[0-9a-f]+)\) @ [^\]]*\]: "
                       r"(?:([rwxps-]{4}) )?(.*\S)\s*$")
# `time: addr [addr-symbol] ip symbol`; perf prints the data address's own
# symbol when it resolves one, so that column is optional.
FAULT_LINE = re.compile(r"^\s*(?:\S+\s+\d+\s+(?:\[\d+\]\s+)?)?(\d+\.\d+):\s+(?:\S+:\s+)?"
                        r"([0-9a-f]+)\s+(?:\S+\s+)??([0-9a-f]+)\s*(.*)$")
TRANSIENT_FILES = "transient file mappings (mapped, then replaced at the same address; e.g. sources/headers the preprocessor reads)"


def parse_fault_script(text):
    """`perf script -F time,addr,ip,sym --show-mmap-events` -> (mmaps, faults).

    mmaps: [(time, start, end, name, protection)] in record order (time None
    when perf printed none); faults: [(time, addr, ip, sym)]."""
    mmaps, faults = [], []
    for line in text.splitlines():
        if "PERF_RECORD_" in line:
            match = MMAP_LINE.search(line)
            if match:
                start, length = int(match.group(2), 16), int(match.group(3), 16)
                moment = float(match.group(1)) if match.group(1) else None
                mmaps.append((moment, start, start + length, match.group(5), match.group(4) or ""))
            continue
        match = FAULT_LINE.match(line)
        if match:
            symbol = re.sub(r"\+0x[0-9a-f]+$", "", match.group(4).strip()) or "[unknown]"
            faults.append((float(match.group(1)), int(match.group(2), 16), int(match.group(3), 16), symbol))
    return mmaps, faults


def classify_address(address, mmaps, moment=None):
    """Region label for a faulting data address at time `moment`.

    perf records mmaps but not munmaps, and the compiler maps hundreds of
    sources and headers at the same few addresses, so the mapping that held
    an address at fault time is the latest one covering it that started at
    or before the fault; a later mapping at the same address implies the
    earlier one ended.  A file mapping superseded that way is reported as
    one transient-file region instead of under whichever file came last.
    Without a moment the last covering mapping wins (old behaviour)."""
    chosen = None
    for index in range(len(mmaps) - 1, -1, -1):
        when, start, end, name, protection = mmaps[index]
        if start <= address < end and (moment is None or when is None or when <= moment):
            chosen = index
            break
    if chosen is None:
        return "not an mmap seen by perf (brk heap/stack/pre-exec) @%#x GiB-region" % (address >> 30 << 30)
    when, start, end, name, protection = mmaps[chosen]
    is_file = bool(name) and not name.startswith("//anon") and not name.startswith("[")
    superseded = any(later[1] < end and start < later[2] for later in mmaps[chosen + 1:])
    if is_file and superseded and moment is not None:
        return TRANSIENT_FILES
    size = (end - start) / 1048576
    label = "anon" if name.startswith("//anon") or not name else name
    return "%s %s @0x%x (%.1f MiB)" % (label, protection, start, size)


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
        self.last_elapsed = None

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
        self.last_elapsed = time.monotonic() - started
        out_text = out.decode("utf-8", "replace")
        err_text = err.decode("utf-8", "replace")
        if stdout_path:
            with open(stdout_path, "w") as handle:
                handle.write(out_text)
        with open(self.path("commands.log"), "a") as handle:
            handle.write("%s exit=%d %.3fs\n" % (shell_join(argv), status, self.last_elapsed))
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


def estimate_other_compiles(groups, skip, sudo):
    """Rough cost of the steps after `timed`, in compile-equivalents (one
    compile's wall time), for choose_run_count."""
    cost = {"topdown": TOPDOWN_COMPILES_PER_GROUP * (len(groups) if groups is not None else 8),
            "timeline": 3, "sampling": SAMPLING_COMPILES, "ibs": IBS_COMPILES if sudo else 0, "micro": 3}
    return sum(value for step, value in cost.items() if step not in skip)


def choose_run_count(per_run, elapsed, target_minutes, other_compiles, pilot=PILOT_RUNS):
    """(timed-run count, reason) so the whole lab lands near target_minutes.

    Fixed once after the pilot runs from their wall time alone, never
    adapted to the measured results."""
    remaining = target_minutes * 60.0 - elapsed - other_compiles * per_run
    count = min(MAX_TIMED_RUNS, max(MIN_TIMED_RUNS, pilot + int(max(0.0, remaining) / per_run)))
    reason = ("--target-minutes %g: %.3f s per run (median of %d pilot runs), %.1f min elapsed, later steps about %d "
              "compile-equivalents (%.1f min) -> %d timed runs (clamped to %d..%d)") % (
        target_minutes, per_run, pilot, elapsed / 60, other_compiles, other_compiles * per_run / 60, count,
        MIN_TIMED_RUNS, MAX_TIMED_RUNS)
    return count, reason


def task_seconds(rows):
    """task-clock in seconds (perf prints msec), or None."""
    for row in rows:
        if normalize_event(row["event"]) == "task-clock" and row["value"] is not None:
            return row["value"] / (1e9 if row["unit"] == "ns" else 1e3)
    return None


def step_timed(lab, runs, warmups, target_minutes=15.0, lab_started=None, other_compiles=0):
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
        status, _, err = lab.run_command(lab.pin() + lab.workload(out, compile_flags(lab, os.path.join(directory, "warmup.ccmetrics"))),
                                         log=os.path.join(directory, "warmup-%d.log" % index))
        if status != 0:
            raise RuntimeError("warm-up failed: " + err.strip()[-400:])
        print("[timed] warm-up %.2f s" % lab.last_elapsed, flush=True)
    reference = os.path.join(directory, "reference.exe")
    shutil.copyfile(out, reference)
    started = lab_started if lab_started is not None else time.monotonic()
    count = runs
    plan = {"runs": runs, "reason": "--runs %d" % runs} if runs is not None else None
    records, best, index = [], None, 0
    while count is None or index < count:
        index += 1
        csv_path = os.path.join(directory, "run-%04d.csv" % index)
        metrics_path = os.path.join(directory, "run-%04d.ccmetrics" % index)
        status, out_text, err = lab.run_command(lab.pin() + [lab.perf, "stat", "-x,", "-o", csv_path, "-e", ",".join(TIMED_EVENTS), "--"]
                                                + lab.workload(out, compile_flags(lab, metrics_path)))
        span = lab.last_elapsed
        identical = status == 0 and filecmp.cmp(reference, out, shallow=False)
        if status != 0:
            with open(os.path.join(directory, "run-%04d.err" % index), "w") as handle:
                handle.write(out_text[-20000:] + err[-20000:])
        rows = parse_stat(read_text(csv_path) or "")
        task = task_seconds(rows)
        wall_ns = parse_cc_metrics(read_text(metrics_path) or "")["header"].get("wall_ns")
        if status == 0:
            best = span if best is None else min(best, span)
        records.append({"run": index, "exit": status, "identical": identical, "span_s": round(span, 6)})
        warning = "" if task is not None and task > 0 else " TASK-CLOCK %s: %s" % (
            "MISSING" if task is None else "NON-POSITIVE", stat_lines(rows).get("task-clock", "no task-clock line in " + csv_path))
        print("[timed] %d/%s span %.4f s, task-clock %s s, compiler wall %s s (min span %s)%s%s" % (
            index, count if count is not None else "?", span, fmt(task, ".4f"), fmt(ratio(wall_ns, 1e9) if isinstance(wall_ns, (int, float)) else None, ".4f"),
            fmt(best, ".4f"), "" if identical else " OUTPUT DIFFERS" if status == 0 else " FAILED", warning), flush=True)
        if count is None and index == PILOT_RUNS:
            spans = [record["span_s"] for record in records if record["exit"] == 0]
            per_run = statistics.median(spans) if spans else 2.2
            count, reason = choose_run_count(per_run, time.monotonic() - started, target_minutes, other_compiles)
            plan = {"runs": count, "reason": reason}
            print("[timed] %s" % reason, flush=True)
    lab.meta["config"]["runs"] = count
    lab.save_meta()
    with open(os.path.join(directory, "plan.json"), "w") as handle:
        json.dump(plan, handle, indent=1)
    with open(os.path.join(directory, "runs.json"), "w") as handle:
        json.dump(records, handle, indent=1)
    return "%d runs (%s), %d identical outputs" % (count, plan["reason"] if runs is not None else "--target-minutes %g" % target_minutes,
                                                   sum(record["identical"] for record in records))


def last_reason(text):
    """The most telling line of a perf log: an unavailable-PMU line, else the last one."""
    lines = [line.strip() for line in (text or "").splitlines()
             if line.strip() and not line.startswith(("$", "---", "exit="))]
    for line in lines:
        if UNAVAILABLE_PMU.search(line) and not line.startswith("\\"):
            return line[:240]
    return lines[-1][:240] if lines else "no output"


def measure_stat(lab, base, selector, repeats):
    """`perf stat -r N -j -M selector` into base.json (base.csv via `-x,` when
    the JSON is unreadable); returns (exit, format, rows)."""
    status, _, _ = lab.run_command(lab.pin() + [lab.perf, "stat", "-r", str(repeats), "-j", "-o", base + ".json", "-M", selector, "--"]
                                   + lab.workload(lab.path("topdown", "out.exe")), log=base + ".log")
    rows = parse_stat_json(read_text(base + ".json") or "")
    if status == 0 and rows:
        return status, "json", rows
    if status != 0:
        return status, "json", rows
    status, _, _ = lab.run_command(lab.pin() + [lab.perf, "stat", "-r", str(repeats), "-x,", "-o", base + ".csv", "-M", selector, "--"]
                                   + lab.workload(lab.path("topdown", "out.exe")), log=base + ".log")
    return status, "csv", parse_stat_csv(read_text(base + ".csv") or "")


def step_topdown(lab, groups):
    """One group per invocation after a dry run; a multiplexed group is then
    re-measured one metric per invocation (`-M <metric>`, one run each) so
    its values are not scaled estimates."""
    if not groups:
        return "no metric groups discovered (see env/metricgroups.txt)"
    done = []
    for group in groups:
        base = lab.path("topdown", safe_name(group))
        entry = {"group": group}
        status, out, err = lab.run_command([lab.perf, "stat", "-M", group, "--", "true"], log=base + ".dryrun.log", timeout=120)
        entry["dryrun_exit"] = status
        if status != 0:
            entry.update({"exit": status, "reason": last_reason(out + "\n" + err)})
            if UNAVAILABLE_PMU.search(out + err):
                entry["unavailable"] = "uncore PMU absent"
        else:
            status, form, rows = measure_stat(lab, base, group, 3)
            entry.update({"exit": status, "format": form, "pct_running": multiplex_percent(rows)})
            if status == 0 and entry["pct_running"] is not None and entry["pct_running"] < FULL_RUNNING:
                entry["split"] = []
                for metric in [row["metric_name"] for row in metric_rows(rows)]:
                    if not re.fullmatch(r"[A-Za-z0-9_.]+", metric):
                        continue
                    split_base = lab.path("topdown", safe_name(group) + ".split", safe_name(metric))
                    split_status, split_form, split_rows = measure_stat(lab, split_base, metric, 1)
                    entry["split"].append({"metric": metric, "exit": split_status, "format": split_form,
                                           "pct_running": multiplex_percent(split_rows)})
        done.append(entry)
        print("[topdown] %s exit=%s%s%s" % (group, entry["exit"], " (%s)" % entry["unavailable"] if "unavailable" in entry else "",
                                            " multiplexed %.1f%%: %d metrics re-measured alone" % (entry["pct_running"], len(entry["split"]))
                                            if "split" in entry else ""), flush=True)
    with open(lab.path("topdown", "groups.json"), "w") as handle:
        json.dump(done, handle, indent=1)
    unavailable = sum("unavailable" in entry for entry in done)
    measured = sum(entry["exit"] == 0 for entry in done)
    return "%d measured, %d unavailable (uncore PMU absent), %d failed" % (measured, unavailable, len(done) - measured - unavailable)


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
        # dso,symbol names an unresolved address by its binary (parse_report).
        lab.run_command(report + ["--no-children", "--sort", "dso,symbol", "-g", "none", "--percent-limit", "0.2"],
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
        report = [lab.perf, "report", "-i", data, "--stdio", "--no-inline", "--no-children"]
        lab.run_command(report + ["--sort", "dso,symbol", "-g", "none", "--percent-limit", "0.2"], stdout_path=os.path.join(directory, "faults.self.txt"))
        lab.run_command(report + ["--sort", "symbol", "-g", "caller", "--percent-limit", "1"], stdout_path=os.path.join(directory, "faults.callers.txt"))
        lab.run_command([lab.perf, "script", "-i", data, "-F", "time,addr,ip,sym", "--hide-call-graph", "--no-inline", "--show-mmap-events"],
                        stdout_path=os.path.join(directory, "faults.script.txt"), log=os.path.join(directory, "faults.script.log"))
    if not recorded:
        raise RuntimeError("no event could be recorded")
    return "recorded: " + ", ".join(recorded)


def write_text(path, text):
    with open(path, "w") as handle:
        handle.write(text)


def derive_ibs_reports(lab, directory, basename):
    """Workload-filtered reports from the raw ibs/*.data captures.

    Each capture is a separate workload run (its own pid), recorded on the
    pinned CPU: `perf report --sort pid,dso` finds the threads with samples in
    the workload binary (workload_filter), and the symbol / load-source
    reports keep only those threads.  `<capture>.filter.json` is written last,
    so `report DIR` re-derives whatever an older or interrupted run lacks."""
    notes = []
    for name in IBS_CAPTURES:
        data = os.path.join(directory, name + ".data")
        if not os.path.isfile(data) or os.path.exists(os.path.join(directory, name + ".filter.json")):
            continue
        status, out, _ = lab.run_command([lab.perf, "report", "-i", data, "--stdio", "--sort", "pid,dso", "-g", "none"],
                                         log=os.path.join(directory, name + ".tasks.log"))
        if status != 0:
            notes.append("%s: perf report --sort pid,dso exit=%d" % (name, status))
            continue
        write_text(os.path.join(directory, name + ".tasks.txt"), out)
        selection = workload_filter(parse_task_report(out), basename)
        if name == "mem":
            mem = [lab.perf, "mem", "report", "-i", data, "--stdio", "-n"]
            commands = {"mem.levels.txt": mem + ["--sort=mem"] + selection["options"],
                        "mem.workload.txt": mem + ["--sort=mem,sym", "--percent-limit", "0.3"] + selection["options"]}
        else:
            commands = {name + ".workload.txt": [lab.perf, "report", "-i", data, "--stdio", "--no-inline", "--no-children", "--sort",
                                                 "symbol", "-g", "none", "--percent-limit", "0.3"] + selection["options"]}
        selection["exits"] = {}
        for file, argv in commands.items():
            status, out, _ = lab.run_command(argv, log=os.path.join(directory, file[:-4] + ".log"))
            selection["exits"][file] = status
            if status == 0:
                write_text(os.path.join(directory, file), out)
        write_text(os.path.join(directory, name + ".filter.json"), json.dumps(selection, indent=1, sort_keys=True))
        notes.append("%s: %s %s" % (name, selection["method"], ",".join(str(tid) for tid in selection["tids"]) or ",".join(selection["comms"])))
    return notes


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
    notes += derive_ibs_reports(lab, directory, os.path.basename(lab.ide))
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


def guarded(lines, problems, build):
    """Append build()'s line or lines; an exception becomes one NA line and a
    problem, so one bad value never drops the rest of a section."""
    try:
        result = build()
    except Exception as error:  # a malformed raw value must not hide the other lines
        text = "line failed: %s: %s" % (type(error).__name__, error)
        lines.append("- NA (%s)" % text)
        problems.append(text)
        return
    if isinstance(result, str):
        lines.append(result)
    elif result:
        lines.extend(result)


def render_env(directory, findings, problems):
    facts = json.loads(read_text(os.path.join(directory, "env", "env.json")) or "{}")
    if not facts:
        problems.append("no env/env.json")
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


def positive(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0 else None


def load_timed(directory):
    """Timed runs with their counters and wall times.  span_s is the harness
    span from runs.json, or from commands.log for directories written before
    runs.json carried it."""
    base = os.path.join(directory, "timed")
    records = json.loads(read_text(os.path.join(base, "runs.json")) or "[]")
    spans = command_spans(read_text(os.path.join(directory, "commands.log")))
    runs = []
    for record in records:
        rows = parse_stat(read_text(os.path.join(base, "run-%04d.csv" % record["run"])) or "")
        metrics = parse_cc_metrics(read_text(os.path.join(base, "run-%04d.ccmetrics" % record["run"])) or "")
        span, source = record.get("span_s"), "runs.json"
        if span is None and record["run"] in spans:
            span, source = spans[record["run"]], "commands.log"
        wall_ns = metrics["header"].get("wall_ns")
        runs.append({**record, "values": stat_values(rows), "lines": stat_lines(rows), "metrics": metrics,
                     "span_s": span, "span_source": source if span is not None else None,
                     "cc_wall_s": ratio(wall_ns, 1e9) if isinstance(wall_ns, (int, float)) else None,
                     "task_s": task_seconds(rows)})
    return runs


def timed_problems(good):
    """Fail-closed checks: every successful run needs a positive wall time
    and task-clock; a counted zero is a perf defect, not a measurement."""
    problems = []
    for run in good:
        if positive(run["span_s"]) is None and positive(run["cc_wall_s"]) is None:
            problems.append("run %d: no positive wall time (harness span %s, compiler wall_ns %s; perf CSV `%s`)" % (
                run["run"], fmt(run["span_s"], ".3f"), fmt(run["cc_wall_s"], ".4f"),
                run["lines"].get("duration_time") or run["lines"].get("task-clock") or "no CSV"))
        task = run["values"].get("task-clock")
        if task is None or task <= 0:
            problems.append("run %d: task-clock %s: `%s`" % (run["run"], "missing" if task is None else "non-positive",
                                                             run["lines"].get("task-clock", "no task-clock line")))
        for event in ("cycles", "instructions"):
            value = run["values"].get(event)
            if value is not None and value <= 0:
                problems.append("run %d: %s counted %s: `%s`" % (run["run"], event, fmt(value), run["lines"].get(event)))
    return problems


WALL_SOURCES = (("span_s", "harness span", "taskset + perf + compile, monotonic, runs.json/commands.log"),
                ("cc_wall_s", "compiler wall_ns", "-fmetrics-out, the compiler's own clock"),
                ("task_s", "task-clock", "CPU time of the process, perf"))


def render_timed(directory, findings, problems):
    runs = load_timed(directory)
    good = [run for run in runs if run["exit"] == 0]
    if not good:
        problems.append("no successful timed run")
        return ["No successful timed run."]
    source = parse_key_values(read_text(os.path.join(directory, "timed", "source.metrics")) or "")
    work_bytes = source.get("lexed.translated_bytes")
    tokens = source.get("preprocessed.tokens")
    problems += timed_problems(good)

    def column(name):
        return [run["values"].get(name) for run in good]
    walls = {key: [positive(run[key]) for run in good] for key, _, _ in WALL_SOURCES}
    covered = [(sum(value is not None for value in walls[key]), key, label) for key, label, _ in WALL_SOURCES[:2]]
    count, primary_key, primary = max(covered, key=lambda item: item[0])
    if count == 0:
        primary_key, primary = None, None
    elif count < len(good):
        problems.append("%s covers %d of %d successful runs" % (primary, count, len(good)))
    wall = walls[primary_key] if primary_key else []
    summary = summarize(wall)
    instructions, cycles = summarize(column("instructions")), summarize(column("cycles"))
    task = summarize(column("task-clock"))
    lines = []
    plan = json.loads(read_text(os.path.join(directory, "timed", "plan.json")) or "null")
    guarded(lines, problems, lambda: "%d runs (%d failed), %d byte-identical to the warm-up output%s.%s" % (
        len(runs), len(runs) - len(good), sum(run["identical"] for run in runs),
        "" if all(run["identical"] for run in good) else " **(nondeterministic output: see timed/runs.json)**",
        " Run count: %s." % plan["reason"] if plan else ""))

    def wall_table():
        rows = []
        for key, label, origin in WALL_SOURCES:
            stats = summarize(walls[key])
            if stats is None:
                rows.append(["%s (%s)" % (label, origin), "0"] + ["NA"] * 8)
                continue
            rows.append(["%s (%s)" % (label, origin), str(stats["n"]), *(fmt(stats[name], ".4f") for name in ("min", "p10", "median", "p90", "max", "mad")),
                         percent(ratio(stats["mad"], stats["median"])), fmt(stats["mean"], ".4f")])
        sources = sorted({run["span_source"] for run in good if run["span_source"]})
        return ["", "Wall time (s); primary: **%s**%s." % (primary or "none", " (spans from %s)" % ", ".join(sources) if sources else ""), ""] + table(
            ["source", "n", "min", "p10", "median", "p90", "max", "MAD", "MAD/median", "mean"], rows) + [""]
    guarded(lines, problems, wall_table)

    def duration_line():
        present = [run for run in good if "duration_time" in run["values"]]
        zero = [run for run in present if positive(run["values"]["duration_time"]) is None]
        if not present:
            return None
        return "- duration_time: %d of %d runs read 0 or NA (perf 7.2.4 reads it as 0 whenever it shares the event list: `%s`); not used." % (
            len(zero), len(present), (zero[0] if zero else present[0])["lines"].get("duration_time")) if zero else \
            "- duration_time present in %d runs (not used; the harness span is the wall time)." % len(present)
    guarded(lines, problems, duration_line)
    if summary:
        guarded(lines, problems, lambda: "- %s: min-vs-median gap %s; MAD/median %s; runs since last new minimum: %s" % (
            primary, percent(ratio(summary["median"] - summary["min"], summary["min"])), percent(ratio(summary["mad"], summary["median"])),
            runs_since_minimum(wall)))
        guarded(lines, problems, lambda: "- median per tenth of the series (drift): " + " ".join(fmt(value, ".4f") for value in tenth_medians(wall)))
    median_ipc = ratio(instructions and instructions["median"], cycles and cycles["median"])
    guarded(lines, problems, lambda: "- instructions:u median %s, spread (max-min)/median %s" % (
        fmt(instructions and instructions["median"]),
        percent(ratio(instructions and instructions["max"] - instructions["min"], instructions and instructions["median"]))))

    def cycles_line():
        per_run_ipc = summarize([ratio(run["values"].get("instructions"), run["values"].get("cycles")) for run in good])
        ghz = summarize([ratio(run["values"].get("cycles"), run["values"].get("task-clock"), 1e-6) for run in good])
        return "- cycles:u median %s; IPC %s (per-run median %s); effective clock %s GHz (cycles / task-clock); task-clock median %s ms" % (
            fmt(cycles and cycles["median"]), fmt(median_ipc, ".3f"), fmt(per_run_ipc and per_run_ipc["median"], ".3f"),
            fmt(ghz and ghz["median"], ".3f"), fmt(task and task["median"], ".1f"))
    guarded(lines, problems, cycles_line)

    def branch_line():
        misses = column("branch-misses")
        stats = summarize(misses)
        return "- branch-misses:u median %s (MPKI %s); first 3 %s, last 3 %s (a falling trend means the predictor is learning across runs)" % (
            fmt(stats and stats["median"]), fmt(ratio(stats and stats["median"], instructions and instructions["median"], 1000), ".2f"),
            [fmt(value) for value in misses[:3]], [fmt(value) for value in misses[-3:]])
    guarded(lines, problems, branch_line)
    for name in ("page-faults", "minor-faults", "major-faults"):
        def fault_line(name=name):
            stats = summarize(column(name))
            return "- %s per run: median %s, min %s, max %s" % (name, fmt(stats and stats["median"]), fmt(stats and stats["min"]), fmt(stats and stats["max"]))
        guarded(lines, problems, fault_line)
    low, mid = summary and summary["min"], summary and summary["median"]
    if work_bytes:
        guarded(lines, problems, lambda: "- work (-fsource-metrics): %s translated bytes, %s code lines, %s tokens; at the %s minimum %s ns/byte, %s MB/s; median %s ns/byte, %s MB/s" % (
            fmt(work_bytes), fmt(source.get("lexed.code_lines")), fmt(tokens), primary or "wall", fmt(ratio(low, work_bytes, 1e9), ".3f"),
            fmt(ratio(work_bytes, low, 1e-6), ".2f"), fmt(ratio(mid, work_bytes, 1e9), ".3f"), fmt(ratio(work_bytes, mid, 1e-6), ".2f")))
        guarded(lines, problems, lambda: "- instructions per byte %s, per token %s" % (fmt(ratio(instructions and instructions["median"], work_bytes), ".3f"),
                                                                                     fmt(ratio(instructions and instructions["median"], tokens), ".3f")))
    else:
        lines.append("- work denominators: NA (binary without -fsource-metrics)")
    guarded(lines, problems, lambda: timed_phase_lines(directory, good, findings))
    if summary:
        guarded(lines, problems, lambda: findings.append("Wall (%s) min %s s / median %s s, IPC %s, %s MB/s at the minimum (timed/)" % (
            primary, fmt(low, ".4f"), fmt(mid, ".4f"), fmt(median_ipc, ".3f"), fmt(ratio(work_bytes, low, 1e-6), ".2f"))))
    return lines


def timed_phase_lines(directory, good, findings):
    records = [record for record in (measured_input(run["metrics"]) for run in good) if record]
    if not records:
        return ["", "Phase breakdown: NA -- the binary %s." % _metrics_reason(directory)]
    total = statistics.median(record["total_ns"] for record in records)
    rows, medians = [], {}
    for phase in PHASES:
        medians[phase] = statistics.median(record.get(phase + "_ns", 0) for record in records)
        rows.append([phase, fmt(ratio(medians[phase], 1e6), ",.2f"), percent(ratio(medians[phase], total))])
    rows.append(["input total", fmt(ratio(total, 1e6), ",.2f"), "100.0%"])
    headers = [run["metrics"]["header"] for run in good if run["metrics"]["header"]]
    lines = ["", "Per-phase median over %d `-fmetrics-out` records:" % len(records), ""] + table(["phase", "median ms", "share of input"], rows)
    lines += ["", "- arena peak median %s bytes; arena retained median %s; peak RSS median %s bytes" % (
        fmt(statistics.median(record.get("arena_peak_bytes", 0) for record in records)),
        fmt(statistics.median(record.get("arena_retained_bytes", 0) for record in records)),
        fmt(statistics.median(header.get("peak_rss_bytes", 0) for header in headers) if headers else None))]
    slowest = max(PHASES, key=lambda phase: medians[phase])
    findings.append("Slowest phase (timed median): **%s** %s ms = %s of the input (timed/run-*.ccmetrics)" % (
        slowest, fmt(ratio(medians[slowest], 1e6), ".1f"), percent(ratio(medians[slowest], total))))
    return lines


def _metrics_reason(directory):
    capabilities = load_meta(directory).get("capabilities", {})
    if not capabilities.get("metrics_out"):
        return "does not accept -fmetrics-out= (see timed/metrics-probe.log)"
    if not capabilities.get("metrics_out_measured"):
        return "accepts -fmetrics-out= but wrote no measured CC_METRICS_INPUT record for this invocation (timed/probe.ccmetrics)"
    return "wrote no measured record in this step"


def load_stat_file(base):
    """(rows, file) from base.json (perf stat -j) or else base.csv."""
    for suffix in (".json", ".csv"):
        rows = parse_stat(read_text(base + suffix) or "")
        if rows:
            return rows, os.path.basename(base) + suffix
    return [], None


def render_topdown(directory, findings, problems):
    groups = json.loads(read_text(os.path.join(directory, "topdown", "groups.json")) or "[]")
    if not groups:
        problems.append("no metric group measured")
        return ["No metric group measured."]
    counts = {"measured": 0, "unavailable": 0, "failed": 0}
    cache = {}

    def timed_instructions():
        if "value" not in cache:
            stats = summarize([run["values"].get("instructions") for run in load_timed(directory) if run["exit"] == 0])
            cache["value"] = (stats["median"], "instructions:u (timed median)") if stats else None
        return cache["value"]
    body = []
    for entry in groups:
        guarded(body, problems, lambda entry=entry: topdown_group_lines(directory, entry, counts, findings, problems, timed_instructions))
    if counts["measured"] == 0:
        problems.append("no metric group produced values")
    head = ["%d measured, %d unavailable (uncore PMU absent; not failures), %d failed. Percentages and ratios come from perf's "
            "metric expressions; JSON (`-j`) values keep six decimals, `-x,` values are rounded as marked. A group whose counters "
            "ran less than all of the time is multiplexed (scaled estimates) and is re-measured one metric per run; those "
            "non-multiplexed values are shown first and the group's value is kept only as a flagged fallback." % (
                counts["measured"], counts["unavailable"], counts["failed"])]
    return head + body


def topdown_group_lines(directory, entry, counts, findings, problems, timed_instructions):
    group = entry["group"]
    base = os.path.join(directory, "topdown", safe_name(group))
    log = (read_text(base + ".log") or "") + "\n" + (read_text(base + ".dryrun.log") or "")
    uncore = " Uncore/system-wide counters: not a per-process number." if UNCORE_GROUP.search(group) else ""
    if entry.get("unavailable") or (entry.get("exit") not in (0, None) and UNAVAILABLE_PMU.search(log)):
        counts["unavailable"] += 1
        return ["", "**%s**: unavailable -- uncore PMU absent on this host (`%s`).%s" % (group, entry.get("reason") or last_reason(log), uncore)]
    rows, file = load_stat_file(base)
    metrics = metric_rows(rows)
    if entry.get("exit") != 0 or not metrics:
        counts["failed"] += 1
        reason = entry.get("reason") or last_reason(log) if entry.get("exit") != 0 else "no metric values"
        problems.append("topdown %s: %s" % (group, reason))
        return ["", "**%s** (exit %s)" % (group, entry.get("exit")), "", "NA (%s)" % reason]
    counts["measured"] += 1
    values = stat_values(rows)
    multiplex = multiplex_percent(rows)
    split = {}
    for item in entry.get("split", []):
        split_rows, _ = load_stat_file(os.path.join(directory, "topdown", safe_name(group) + ".split", safe_name(item["metric"])))
        chosen = [row for row in metric_rows(split_rows) if row["metric_name"] == item["metric"]]
        if item.get("exit") == 0 and chosen:
            split[item["metric"]] = (chosen[0], stat_values(split_rows), multiplex_percent(split_rows))
    multiplexed = multiplex is not None and multiplex < FULL_RUNNING
    lines = ["", "**%s** (topdown/%s, exit %d, counters running %s of the time%s)%s" % (
        group, file, entry["exit"], "NA" if multiplex is None else "%.1f%%" % multiplex,
        (", multiplexed: %d of %d metrics re-measured alone" % (len(split), len(metrics))) if multiplexed else "", uncore), ""]
    duration = values.get("duration_time")
    duration_bad = "duration_time" in values and positive(duration) is None
    rows_out, chosen_metrics = [], []
    for row in metrics:
        own, own_values, own_multiplex = split.get(row["metric_name"], (row, values, multiplex))
        instructions = instruction_count(own_values, timed_instructions())
        if time_divided(own) and (duration_bad or ("duration_time" in own_values and positive(own_values["duration_time"]) is None)):
            value_text = "NA (unreliable: divides by duration_time, which perf read as %s)" % fmt(duration if duration_bad else own_values["duration_time"])
            chosen_metrics.append((own["metric_name"], None, own["metric_unit"]))
        else:
            value_text = "%s %s" % (metric_text(own["metric_value"], own["metric_decimals"]), own["metric_unit"])
            chosen_metrics.append((own["metric_name"], own["metric_value"], own["metric_unit"]))
        recomputed = recompute_metric(own, own_values, instructions, alone=row["metric_name"] in split)
        recomputed_text = "-"
        if recomputed:
            recomputed_text = "%s%s = %s" % (significant(recomputed[0]), " (%.2f%%)" % (recomputed[0] * 100) if own["metric_unit"] == "per_branch" else "", recomputed[1])
        if row["metric_name"] in split:
            source = "alone, running %s" % ("NA" if own_multiplex is None else "%.1f%%" % own_multiplex)
            if own_multiplex is not None and own_multiplex < FULL_RUNNING:
                source += " (still multiplexed)"
            source += "; group value %s" % metric_text(row["metric_value"], row["metric_decimals"])
        elif multiplexed:
            source = "**multiplexed group value** (counters %.1f%%), fallback" % multiplex
        else:
            source = "group"
        rows_out.append((row["metric_name"], value_text, recomputed_text, source))
    lines += table(["metric", "value", "recomputed from raw counts", "source"], rows_out)
    pair = branch_pair(values)
    if pair:
        instructions = instruction_count(values, timed_instructions())
        lines += ["", "- branch misprediction rate %.2f%% (%s / %s), branch MPKI %s (per 1000 %s)" % (
            pair[0] * 100.0 / pair[1], pair[2], pair[3], fmt(ratio(pair[0], instructions and instructions[0], 1000), ".3f"),
            instructions[1] if instructions else "instructions: NA")]
    if re.fullmatch(r"(?i)pipelinel1|topdownl1|tmal1", group):
        category = dominant_category(chosen_metrics)
        if category:
            findings.append("Dominant top-down level-1 category: **%s** %s%% (topdown/%s%s)" % (
                category[0], fmt(category[1], ".1f"), file, "; multiplexed group, see the per-metric values" if multiplexed and not split else ""))
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


def render_timeline(directory, findings, problems):
    intervals, metrics, spans, total = timeline_data(directory)
    if not intervals:
        problems.append("no interval data in timeline/interval.csv")
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
    guarded(lines, problems, lambda: timeline_phase_lines(intervals, spans, findings))
    lines += ["", "The `-M PipelineL1` interval run is kept raw: timeline/pipeline-l1.csv."]
    return lines


def timeline_phase_lines(intervals, spans, findings):
    attributed = attribute_intervals(intervals, spans)
    lines, rows = [], []
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
    for moment, address, _, _ in faults:
        label = classify_address(address, mmaps, moment)
        regions[label] = regions.get(label, 0) + 1
    first, last = min(fault[0] for fault in faults), max(fault[0] for fault in faults)
    width = max((last - first) / buckets, 1e-9)
    timeline = [0] * buckets
    for moment, _, _, _ in faults:
        timeline[min(buckets - 1, int((moment - first) / width))] += 1
    return {"count": len(faults), "regions": sorted(regions.items(), key=lambda item: -item[1]),
            "bucket_ms": width * 1e3, "timeline": timeline, "span_ms": (last - first) * 1e3}


SAMPLING_LABELS = {"cycles": "cycles", "cpu-clock": "cpu-clock (cycles:u unsupported; time-based fallback)", "branch-misses": "branch misses",
                   "l1i-misses": "L1I misses", "l1d-misses": "L1D misses", "dtlb-misses": "dTLB misses"}


def render_sampling(directory, findings, problems):
    base = os.path.join(directory, "sampling")
    lines = ["Samples carry skid (none of these events is precise on AMD without IBS): read symbols as reliable and lines as "
             "\"this loop\". Branch-miss samples land after the mispredicted branch; `tools/branch_miss_survey.py` uses LBR "
             "for exact branches. Percentages are shares of the whole capture; each capture is the workload process alone, so "
             "no thread filter applies. An unresolved address is named by its binary (`ide 0x18f5ce`)."]
    found = []
    for name, label in SAMPLING_LABELS.items():
        guarded(lines, problems, lambda name=name, label=label: sampling_capture_lines(base, name, label, findings, problems, found))
    if not any(name in found for name in ("cycles", "cpu-clock")):
        problems.append("neither cycles nor cpu-clock produced a report")
    guarded(lines, problems, lambda: fault_lines(directory, base, findings, problems, found))
    if not found:
        lines.append("No capture produced a report.")
    return lines


def sampling_capture_lines(base, name, label, findings, problems, found):
    text = read_text(os.path.join(base, name + ".self.txt"))
    if text is None:
        return [] if name == "cpu-clock" else ["", "**%s**: NA (%s)" % (label, _record_failure(os.path.join(base, name + ".record.log")))]
    found.append(name)
    entries = top_entries(text, 15)
    if not entries:
        problems.append("sampling %s: the report has no rows (sampling/%s.self.txt)" % (name, name))
    sampled = [event for event in parse_report(text) if event]
    if sampled and normalize_event(sampled[0]) != normalize_event(dict(SAMPLE_EVENTS).get(name, sampled[0])):
        label += " (perf fell back to event '%s')" % sampled[0]
    lines = ["", "**Top self symbols by %s** (sampling/%s.self.txt)" % (label, name), ""]
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
    return lines


def fault_lines(directory, base, findings, problems, found):
    text = read_text(os.path.join(base, "faults.self.txt"))
    if text is None:
        problems.append("no page-fault capture (sampling/faults.record.log)")
        return ["", "**Page faults**: NA (%s)" % _record_failure(os.path.join(base, "faults.record.log"))]
    found.append("faults")
    entries = top_entries(text, 15)
    if not entries:
        problems.append("page faults: the report has no rows (sampling/faults.self.txt)")
    lines = ["", "**Page faults by symbol** (every fault recorded; callers in sampling/faults.callers.txt)", ""]
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
    return lines


def _record_failure(log_path):
    text = read_text(log_path)
    if text is None:
        return "not recorded"
    reason = [line.strip() for line in text.splitlines() if line.strip() and not line.startswith(("$", "---", "exit="))]
    return reason[-1][:200] if reason else "failed"


def filter_description(selection):
    if not selection:
        return "filtered by exec name only (pre-fix report; the renamed main_thread is missing)"
    if selection.get("method") == "tid":
        return "threads %s (%s)" % (",".join(str(tid) for tid in selection["tids"]), ",".join(selection["comms"]))
    return "command names %s (no thread had samples in the binary)" % ",".join(selection.get("comms", []))


def render_ibs(directory, findings, problems):
    base = os.path.join(directory, "ibs")
    if not os.path.isdir(base):
        return ["Not run (needs --sudo and an AMD IBS PMU)."]
    lines = ["IBS tags individual micro-ops (op) and fetches, so attribution is precise; recorded system-wide on the pinned CPU and "
             "filtered to the workload's threads: the thread ids with samples in the workload binary, with their command names "
             "(the compiler renames its main thread `main_thread`, so its exec name alone misses nearly every sample). Shares "
             "are of the whole capture (perf's absolute percentages)."]
    for name in ("ibs_op", "ibs_fetch"):
        guarded(lines, problems, lambda name=name: ibs_capture_lines(base, name, findings, problems))
    guarded(lines, problems, lambda: mem_lines(base, findings, problems))
    return lines


def capture_has_samples(base, name):
    tasks = read_text(os.path.join(base, name + ".tasks.txt"))
    if tasks is not None:
        return bool(parse_task_report(tasks))
    data = os.path.join(base, name + ".data")
    return os.path.isfile(data) and os.path.getsize(data) > 0


def ibs_capture_lines(base, name, findings, problems):
    selection = json.loads(read_text(os.path.join(base, name + ".filter.json")) or "null")
    file = name + ".workload.txt"
    text = read_text(os.path.join(base, file))
    if text is None and read_text(os.path.join(base, name + ".self.txt")) is not None:
        file = name + ".self.txt"
        text = read_text(os.path.join(base, file))
        problems.append("ibs %s: only the pre-fix exec-name report exists; re-render with perf available (`report DIR --perf PATH`) "
                        "to derive the per-thread report from ibs/%s.data" % (name, name))
    entries = top_entries(text, 15)
    lines = ["", "**%s top symbols** (ibs/%s; %s)" % (name, file, filter_description(selection)), ""]
    if not entries:
        if capture_has_samples(base, name):
            problems.append("ibs %s: 0 report rows while the capture has samples (ibs/%s)" % (name, file))
        return lines + ["NA (%s)" % _record_failure(os.path.join(base, name + ".record.log"))]
    findings.append("Hot by %s (precise, workload threads): %s (ibs/%s)" % (name, ", ".join("`%s` %.2f%%" % (entry[2], entry[0]) for entry in entries[:3]), file))
    return lines + table(["share", "symbol"], [("%.2f%%" % entry[0], "`%s`" % entry[2]) for entry in entries])


def mem_lines(base, findings, problems):
    lines = ["", "**perf mem: load source** (ibs/mem.levels.txt; %s)" % filter_description(
        json.loads(read_text(os.path.join(base, "mem.filter.json")) or "null")), ""]
    levels = parse_mem_levels(read_text(os.path.join(base, "mem.levels.txt")))
    if levels:
        total = sum(row[1] for row in levels)
        lines += ["Shares are weighted by each sample's latency, so a large share can rest on a handful of samples: read the "
                  "sample count beside it.", ""]
        lines += table(["load source", "weighted share", "samples", "share of samples"],
                       [(row[2], "%.2f%%" % row[0], fmt(row[1]), percent(ratio(row[1], total))) for row in levels])
        findings.append("Load sources (perf mem, workload threads): %s (ibs/mem.levels.txt)" % ", ".join(
            "%s %.2f%% (%s samples)" % (row[2], row[0], fmt(row[1])) for row in levels[:3]))
    elif os.path.isfile(os.path.join(base, "mem.data")):
        if capture_has_samples(base, "mem"):
            problems.append("perf mem: no load-source rows while the capture has samples")
        lines.append("NA (%s)" % _record_failure(os.path.join(base, "mem.record.log")))
    else:
        problems.append("no perf mem capture")
        lines.append("NA (%s)" % _record_failure(os.path.join(base, "mem.record.log")))
    file = "mem.workload.txt" if os.path.exists(os.path.join(base, "mem.workload.txt")) else "mem.report.txt"
    text = read_text(os.path.join(base, file))
    if file == "mem.report.txt" and text is not None:
        problems.append("perf mem: only the pre-fix exec-name report exists; re-render with perf available")
    lines += ["", "**perf mem: load source by symbol** (ibs/%s)" % file, ""]
    lines += fenced("\n".join(line for line in (text or "").splitlines() if line.strip() and not line.startswith("#")), 30) if text else ["NA"]
    return lines


def render_micro(directory, findings, problems):
    text = read_text(os.path.join(directory, "micro", "bench.txt"))
    if not text or "BENCH_C_FRONTEND" not in text:
        problems.append("`ide bench` unsupported or not run")
        return ["NA (`ide bench` unsupported or not run)."]
    lines = [line for line in text.splitlines() if "BENCH" in line]
    return ["In-process repetition of the C frontend on tests/basic_c_operations.c. Caveat (Lemire): repeating one identical "
            "input lets the branch predictor learn it, which flatters branchy code; do not compare it with a fresh-process compile."] + fenced("\n".join(lines))


RENDERERS = {"env": ("1. Environment", render_env), "timed": ("2. Timed runs", render_timed),
             "topdown": ("3. Top-down metric groups", render_topdown), "timeline": ("4. Timeline (WHEN)", render_timeline),
             "sampling": ("5. Sampling (WHERE)", render_sampling), "ibs": ("6. IBS and perf mem (sudo)", render_ibs),
             "micro": ("7. Micro-benchmark", render_micro)}


def render_section(directory, step, findings):
    """(lines, problems) of one section; problems make an `ok` step `degraded`."""
    problems = []
    try:
        lines = RENDERERS[step][1](directory, findings, problems)
    except Exception as error:  # a malformed raw file must not hide the other sections
        lines = ["Rendering failed: %s: %s" % (type(error).__name__, error)]
        problems.append("rendering failed: %s: %s" % (type(error).__name__, error))
    return lines, problems


def effective_status(state, problems):
    """`ok` only when the section has real data: a recorded ok/degraded step is
    re-assessed from its raw files; failed and skipped stay as recorded."""
    if state is None:
        return "not run"
    if state["status"] in ("ok", "degraded"):
        return "degraded" if problems else "ok"
    return state["status"]


def problem_summary(problems, limit=1):
    if not problems:
        return ""
    return "; ".join(problems[:limit]) + (" (+%d more)" % (len(problems) - limit) if len(problems) > limit else "")


def workload_basename(meta, directory):
    config = meta.get("config", {})
    ide = config.get("ide") or json.loads(read_text(os.path.join(directory, "env", "env.json")) or "{}").get("ide") \
        or (config.get("command") or "ide").split()[0]
    return os.path.basename(ide)


def resolve_perf(meta, explicit=None):
    """The perf to derive missing reports with: --perf, the recorded one, or `perf` on PATH."""
    for candidate in (explicit, meta.get("config", {}).get("perf"), "perf"):
        if candidate and (shutil.which(candidate) or os.access(candidate, os.X_OK)):
            return candidate
    return None


def render_report(directory, perf=None):
    """report.md from the raw files.  With `perf`, reports derived from raw
    perf.data that an older run lacks (the per-thread IBS filter) are derived
    first; nothing re-runs the workload."""
    meta = load_meta(directory)
    if perf and os.path.isdir(os.path.join(directory, "ibs")):
        notes = derive_ibs_reports(Lab(directory, perf, repo_root=directory), os.path.join(directory, "ibs"), workload_basename(meta, directory))
        if notes:
            print("uarch_lab: derived IBS reports: %s" % "; ".join(notes), file=sys.stderr)
    findings, body, statuses = [], [], {}
    steps = meta.get("steps", {})
    for step in STEPS:
        body += ["", "## " + RENDERERS[step][0], ""]
        state = steps.get(step)
        if state is not None and state["status"] not in ("ok", "degraded", "failed"):
            body.append("Step status: **%s** %s" % (state["status"], state.get("note", "")))
            statuses[step] = (state["status"], [])
            continue
        lines, problems = render_section(directory, step, findings)
        status = effective_status(state, problems)
        statuses[step] = (status, problems)
        if status not in ("ok", "not run"):
            body.append("Step status: **%s** %s" % (status, state.get("note", "")))
            body += [""] + ["- %s" % problem for problem in problems[:8]] + (["- ... %d more" % (len(problems) - 8)] if len(problems) > 8 else []) + [""]
        body += lines
    config = meta.get("config", {})
    head = ["# Micro-architecture lab report", "",
            "Workload: `%s`, pinned to CPU %s, %s timed runs. Raw files are beside this report; `python3 tools/uarch_lab.py report %s` "
            "re-renders it." % (config.get("command", "NA"), config.get("cpu", "NA"), config.get("runs", "NA"), directory), "",
            "## Findings to investigate", "", "Data pointers, not conclusions; each names the raw file behind it.", ""]
    head += ["- " + finding for finding in findings] or ["- none (no step produced data)"]
    head += ["", "## Steps", ""] + table(["step", "status", "elapsed s", "note"], [
        (step, statuses[step][0], fmt(steps.get(step, {}).get("elapsed_s"), ".1f"),
         " -- ".join(part for part in (steps.get(step, {}).get("note", ""), problem_summary(statuses[step][1])) if part)) for step in STEPS])
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
                          "repo_root": lab.repo_root, "skip": arguments.skip, "sudo": arguments.sudo, "ide": ide,
                          "target_minutes": arguments.target_minutes}
    lab.save_meta()
    print("uarch_lab: output %s; %s" % (lab.output, "%d timed runs (--runs)" % arguments.runs if arguments.runs is not None else
                                        "timed-run count chosen after %d pilot runs to land near %g min" % (PILOT_RUNS, arguments.target_minutes)), flush=True)
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
                facts = json.loads(read_text(os.path.join(lab.output, "env", "env.json")) or "{}")
                note = step_timed(lab, arguments.runs, arguments.warmups, arguments.target_minutes, started,
                                  estimate_other_compiles(facts.get("metric_groups"), arguments.skip, arguments.sudo))
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
        if state["status"] == "ok":
            _, problems = render_section(lab.output, step, [])
            if problems:
                state.update(status="degraded", problems=problems[:20])
        state["elapsed_s"] = round(time.monotonic() - step_started, 1)
        lab.meta["steps"][step] = state
        lab.save_meta()
        print("uarch_lab: step %s %s in %.1f s %s" % (step, state["status"], state["elapsed_s"],
                                                     " -- ".join(part for part in (state["note"], problem_summary(state.get("problems"))) if part)), flush=True)
    lab.meta["total_s"] = round(time.monotonic() - started, 1)
    lab.save_meta()
    render_report(lab.output, lab.perf)
    print("uarch_lab: total %.1f s; report %s" % (lab.meta["total_s"], os.path.join(lab.output, "report.md")))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="run the steps and render the report")
    run.add_argument("--ide", default="build/Release/ide")
    run.add_argument("--repo-root", default=".")
    run.add_argument("--cpu", type=int, default=2, help="CPU to pin to (-1: unpinned)")
    run.add_argument("--output", required=True)
    run.add_argument("--runs", type=int, default=None, help="timed runs (overrides --target-minutes)")
    run.add_argument("--target-minutes", type=float, default=15.0,
                     help="choose the timed-run count after %d pilot runs so the whole lab takes about this long" % PILOT_RUNS)
    run.add_argument("--warmups", type=int, default=1)
    run.add_argument("--perf", default="perf")
    run.add_argument("--sudo", action="store_true", help="enable the IBS / perf mem step")
    run.add_argument("--skip", nargs="*", default=[], choices=STEPS)
    run.add_argument("extra", nargs=argparse.REMAINDER, help="-- extra compile arguments")
    report = commands.add_parser("report", help="re-render DIR/report.md from raw files")
    report.add_argument("directory")
    report.add_argument("--perf", default=None, help="perf used to derive reports an older run lacks (default: the recorded one, then PATH)")
    arguments = parser.parse_args(argv)
    if arguments.command == "run":
        command_run(arguments)
    else:
        directory = os.path.abspath(arguments.directory)
        print(render_report(directory, resolve_perf(load_meta(directory), arguments.perf)), end="")


if __name__ == "__main__":
    main()
