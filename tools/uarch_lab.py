#!/usr/bin/env python3
"""Micro-architecture lab: where, when, what and how many for one compile,
and whether a compiler change made it faster or slower (A/B compare).

Ownership: compiler performance research tooling (docs/agents/benchmarking.md,
"Micro-architecture lab").  Python 3 standard library only; Linux `perf`.

It runs one workload -- by default the stage-1 self-host compile of the unity
`src/buster/apps/ide/ide.c` by a Release `ide` -- pinned to one CPU through a
fixed sequence of steps, keeps every raw file in an output directory, and
renders `report.md` and `summary.json` from those raw files alone, so
`report DIR` reproduces both without re-running anything.

Use the session-owned worktree and session_root from
`docs/agents/workflow.md` (parallel sessions); build the trusted performance
compiler with tests off as described in `docs/agents/benchmarking.md`. Every
attempt gets a new output directory, retained until its users finish.

    python3 tools/uarch_lab.py run --ide "$session_root/src/build/Release/ide" --repo-root "$session_root/src" \\
        --cpu 2 --output "$session_root/lab-attempt-1" [--target-minutes 15 | --runs N] [--sudo] [--threads] \\
        [--skip STEP ...] [--no-fresh-copy] [--perf PATH] [-- extra compile args]
    python3 tools/uarch_lab.py compare --baseline A_IDE --candidate B_IDE \\
        --repo-root "$session_root/src" --cpu 2 --output "$session_root/ab-attempt-1" [--target-minutes 15 | --pairs N] \\
        [--profile-steps topdown,sampling,threads] [--sudo] [--seed N] \\
        [--min-effect PCT] [--no-fresh-copy] [--require-identical-output] \\
        [--perf PATH] [-- extra compile args]
    python3 tools/uarch_lab.py retirement --baseline A_IDE --candidate B_IDE \\
        --repo-root "$session_root/src" --cpu 2 --output "$session_root/retirement-attempt-1" \\
        [--target-minutes-per-cell 12 | --pairs N] [--modes none,mir-stack,fast,quality]
    python3 tools/uarch_lab.py report DIR [--perf PATH]     (run, compare or retirement)

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
    threads   (--threads / --profile-steps threads, opt-in) one Superluminal-
              style capture for Hotspot: `perf record -F 10000 --call-graph
              fp --switch-events`, with kernel stacks and scheduler wake-ups
              when perf_event_paranoid is -1, else user-only (step_threads)
    micro     `ide bench` if the binary supports it

A step is `ok` only when its section has real data: the report re-assesses
every section from the raw files and marks it `degraded` with the reasons
(a missing or non-positive time, a report with no rows from a capture that has
samples, a failed group).  Failures are recorded in DIR/lab.json and the
report, never fatal; unsupported counters (`<not supported>`, `<not counted>`)
are NA, never zero, and a rounded perf metric never prints as an exact 0.
The phase breakdown needs a binary that accepts `-fmetrics-out=` and writes a
measured `CC_METRICS_INPUT` record; without it those sections say so.

Every timed run and profile capture (both modes) executes a fresh copy of its
binary (Lab.instance, fresh_binary_copy): a new file under DIR/instances made
by read/write, fsync'd and closed before the run, deleted after it, so where
the kernel placed one copy's text in the page cache cannot bias every run of
that variant.  LAB3 (#36) measured a stable 0.5% wall offset between two
byte-identical copies run in place; --no-fresh-copy restores that behaviour.
Probes and warm-ups still run the binary in place.  `perf record` then skips
the build-id cache and the reports are derived while the copy exists.

`compare` runs both compilers on the same frozen source with the same command:
both are probed before warm-up (capabilities detected per binary). Phase
metrics are collected in warm-ups and timed pairs only when both accept
`-fmetrics-out`; otherwise neither receives it. The collection policy and
per-binary capabilities are retained separately. Each is warmed up and checked
for byte-identical output across its own runs, then timed in paired ABBA blocks
(A,B then B,A) under `perf stat`; the pair count is --pairs or is fixed once
after a 2-pair pilot block (choose_pair_count).  Per metric it reports the
median per-pair ratio B/A with an exact sign-test 95% CI (sign_test_rank), a
seeded bootstrap CI of the geometric mean, and min-vs-min; the verdict is
wall time alone against a practical floor (--min-effect, default 0.5%): faster /
slower when the whole CI lies beyond it, below-floor when the CI excludes 1.0
but reaches inside it, else no detectable difference, with order-effect and
drift checks and a warning when identical instruction counts take different
time (an instance effect).  --profile-steps re-runs
`topdown`, `sampling` (self reports only) and, with --sudo, `ibs` for both
variants and diffs metric values and per-symbol sample shares.  Layout:
DIR/compare.json, DIR/pairs.json, DIR/pairs/NNNN-{a,b}.csv|.ccmetrics,
DIR/a and DIR/b (per-variant lab directories).

summary.json is the stable machine-readable result for agents: schema
RUN_SCHEMA (`buster-uarch-lab-run-v1`, run_summary) or COMPARE_SCHEMA
(`buster-uarch-lab-compare-v2`, compare_summary); keys are documented in
docs/agents/benchmarking.md and pinned by golden-key tests.

Map (searchable symbols):
    STEPS, TIMED_EVENTS, INTERVAL_EVENTS, SAMPLE_EVENTS   step and event tables
    parse_stat, parse_stat_csv, parse_stat_json, stat_values  perf stat output
    metric_text, recompute_metric, branch_pair             metric precision
    parse_key_values, parse_cc_metrics                    compiler metric files
    parse_report, parse_annotate, parse_fault_script      perf report/annotate/script
    parse_task_report, workload_filter, parse_mem_levels  IBS workload filter
    command_spans, load_timed, timed_problems             wall time and fail-closed checks
    probe_workload, load_run, run_metrics                 capability probes, one run's values
    choose_run_count, estimate_other_compiles             --target-minutes run count
    measure_stat, last_reason, topdown_group_lines        top-down dry run, split, render
    summarize, percentile, runs_since_minimum, tenth_medians   statistics
    phase_spans, interval_table, attribute_intervals      timeline alignment
    Lab, Lab.workload, Lab.run_command                    process execution
    Lab.instance, fresh_binary_copy, Lab.record_options   fresh binary copy per run/capture
    step_env ... step_micro, sampling_capture, derive_ibs_reports   the steps (raw files)
    render_env ... render_micro, guarded                  report sections from raw files
    render_section, effective_status, render_report       content-based step status
    timeline_svg                                          self-contained chart
    run_summary, topdown_values, symbol_movers            summary.json (run) and profile diffs
    sign_test_rank, median_ci, bootstrap_geomean_ci       compare statistics
    compare_series, classify, metric_floor, compare_checks   per-metric B/A, floor outcome, order/drift
    abba_order, choose_pair_count, compare_timed          ABBA schedule and pair planning
    command_compare, prepare_variant, run_pair_member     compare execution
    compare_summary, compare_verdict, compare_warnings    summary.json (compare): verdict, warnings
    compare_markdown, mover_reading                       compare report
    main                                                  CLI
Tests: tools/uarch_lab_test.py (`python3 -B tools/uarch_lab_test.py`).
"""

import argparse
import contextlib
import filecmp
import fractions
import hashlib
import html
import json
import math
import os
import random
import re
import shutil
import signal
import statistics
import struct
import subprocess
import sys
import tempfile
import threading
import time

STEPS = ("env", "timed", "topdown", "timeline", "sampling", "ibs", "threads", "micro")
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
# summary.json schema ids: bump the suffix when a key changes meaning or goes away.
RUN_SCHEMA = "buster-uarch-lab-run-v1"
# compare-v2 (LAB3): outcomes apply the practical floor and may read
# below-floor; added plan.fresh_copy, verdict.min_effect_percent, per-row note,
# per-capture reliable/note and per-mover exceeds_bound.
COMPARE_SCHEMA = "buster-uarch-lab-compare-v2"
SUMMARY_KEYS = ("n", "min", "p10", "median", "p90", "max", "mean", "mad")
HOST_KEYS = ("date", "cpu_model", "microcode", "kernel", "perf_version", "governor", "epp", "boost", "smt_active",
             "perf_event_paranoid", "nmi_watchdog", "loadavg", "git_revision", "git_dirty_files")
# Per-run metrics shared by run's summary.json and compare: key, unit,
# better direction, label.  Unit "s" marks a time (faster/slower outcomes).
COMPARE_METRICS = (("wall", "s", "lower", "wall time (harness span)"), ("task_clock", "s", "lower", "task-clock"),
                   ("compiler_wall", "s", "lower", "compiler wall_ns"), ("instructions", "count", "lower", "instructions:u"),
                   ("cycles", "count", "lower", "cycles:u"), ("ipc", "ratio", "higher", "IPC"),
                   ("branch_misses", "count", "lower", "branch-misses:u"), ("branch_mpki", "per_1k_instr", "lower", "branch MPKI"),
                   ("page_faults", "count", "lower", "page faults"), ("minor_faults", "count", "lower", "minor faults"),
                   ("major_faults", "count", "lower", "major faults"), ("peak_rss", "bytes", "lower", "peak RSS (wait4 ru_maxrss)"))
# Peak RSS (run_measured, rss_value): a run's ru_maxrss is the maximum of the
# harness's resident RSS inherited at spawn, the taskset/perf process
# and every descendant it reaped, so it is the compiler's only when clearly
# above both the harness's resident footprint at spawn and perf's own floor
# (probe_workload measures `perf stat -- true`); a value within this factor of
# the larger is NA.
RSS_WRAPPER_MARGIN = 1.5
# Generated code bytes (code_sections): ELF section flag and type.
SHF_EXECINSTR = 0x4
SHT_NOBITS = 8
# A/B compare (command_compare): variants, ABBA pair planning, statistics.
VARIANTS = (("a", "baseline"), ("b", "candidate"))
PROFILE_STEPS = ("topdown", "sampling", "ibs", "threads")
# The opt-in `threads` step (step_threads): one Superluminal-style capture for
# Hotspot, at THREAD_SAMPLE_HZ with frame-pointer stacks and context switches.
# Each mode needs less of the host than the one before; the first that
# records wins (perf_event_paranoid -1, then 2, then no PMU).
THREAD_SAMPLE_HZ = 10000
THREAD_MODES = (
    ("full", ["-e", "cycles", "-e", "sched:sched_switch", "-e", "sched:sched_wakeup"],
     "kernel and user stacks, context switches and scheduler wake-ups (off-CPU waits)"),
    ("user", ["-e", "cycles:u"], "user-space stacks and context switches; no kernel frames or wake-ups"),
    ("cpu-clock", ["-e", "cpu-clock:u"], "software-clock user stacks and context switches; no PMU"),
)
PILOT_PAIRS = 2
MIN_PAIRS = 10
MAX_PAIRS = 1000
CONFIDENCE = 0.95
BOOTSTRAP_RESAMPLES = 2000
DEFAULT_SEED = 20261003
HOT_SYMBOLS = 10
SYMBOL_MOVERS = 10
# Practical-effect floor (--min-effect, percent): a time CI that excludes 1.0
# but reaches inside +/-floor is "below-floor", not faster/slower.  LAB3 (#36,
# Zen 5) measured a stable 0.50% wall offset between two byte-identical copies
# of one binary that the pair-to-pair CI (+/-0.09%) did not cover; LAB4, with
# a fresh binary copy per run, measured A/A 0.9995 [0.9986, 1.0003].  The 0.5%
# default floor is about six times that interval's half-width.
DEFAULT_MIN_EFFECT = 0.5
# instructions:u B/A this close to 1 counts as identical work (instance warning).
IDENTICAL_WORK = 1e-6
# Count metrics whose ratio of medians and median of per-pair ratios differ by
# more than this are bimodal: the paired ratio misleads (compare_series).
BIMODAL_DISAGREEMENT = 0.05
# Symbol movers (symbol_movers): a capture with fewer samples than this is not
# diffed; a row "exceeds bound" only past this multiple of its binomial bound
# (LAB3 A/A movers reached about 5x the bound).
MIN_MOVER_SAMPLES = 2000
MOVER_BOUND_FACTOR = 3.0
TOPDOWN_MOVERS = 30
COMPARE_METHOD = {
    "pairing": "one A run and one B run per pair, alternating ABBA blocks (A,B then B,A); the pair count is fixed before the series",
    "ratio": "median of the per-pair ratios B/A",
    "ci": "exact distribution-free 95% CI for the median ratio from sign-test order statistics (assumes only independent pairs)",
    "bootstrap": "percentile bootstrap 95% CI of the geometric-mean ratio, resampling pairs with the recorded seed (cross-check)",
    "verdict": "wall time (harness span) only: faster/slower when its whole CI lies beyond 1 -/+ the practical floor; below-floor "
               "when the CI excludes 1.0 but reaches inside the floor; no detectable difference when it contains 1.0",
    "floor": "--min-effect percent (default 0.5) applies to every time and counter outcome except instructions (exact); "
             "LAB3 measured a 0.5% per-binary-instance offset in A/A that the pair-to-pair CI does not cover",
    "fresh_copy": "each timed run and profile capture executes a new read/write copy of its binary (new inode, fsync'd) that is "
                  "deleted afterwards, so page-cache placement varies per run instead of biasing one variant (--no-fresh-copy disables)",
    "order_effect": "AB and BA pairs' median-ratio CIs must overlap",
    "drift": "first-half and second-half median-ratio CIs must overlap; per-tenth medians shown"}
# Frozen historical native-retirement gate (command_retirement): the #512 decision record
# rescoped the gate to this lab and kept the #511 per-cell limits unchanged.
RETIREMENT_SCHEMA = "buster-uarch-lab-retirement-v1"
RETIREMENT_DECISION = "https://github.com/buster14a/buster/issues/36#issuecomment-5969534074"
RETIREMENT_CONTRACT = "docs/native-retirement-performance-contract.md"
RETIREMENT_MODES = ("none", "mir-stack", "fast", "quality")
RETIREMENT_ROLES = (("baseline", "A"), ("candidate", "B"))
RUNTIME_CELL = "generated-runtime"
# The generated-runtime cell times the stage-1 compilers A and B wrote in this
# mode's cell, compiling the same source in this mode.
RUNTIME_SOURCE_MODE = "fast"
DEFAULT_CELL_MINUTES = 12.0
# #511 "Measurements and denominators", per-workload policy column: wall time
# and peak RSS upper simultaneous bound at most 1.05, generated code bytes
# exact ratio at most 1.01 in every cell, generated-program runtime upper
# simultaneous bound at most 1.03.  #511's 1.02 primary aggregate bounds for
# wall time and RSS are not in the decision record's table; the geometric mean
# of the mode cells' ratios is reported against them as a diagnostic only
# (retirement_aggregates).
RETIREMENT_AGGREGATE_LIMIT = 1.02
RETIREMENT_LIMITS = {"compiler_wall_time": 1.05, "peak_rss": 1.05, "code_bytes": 1.01, "generated_runtime": 1.03}
RETIREMENT_LIMIT_TEXT = {
    "compiler_wall_time": ("each allocator-mode cell: harness span of the timed compile (metric `wall`)", "upper 95% bound"),
    "peak_rss": ("each allocator-mode cell: wait4 ru_maxrss of the compile (metric `peak_rss`)", "upper 95% bound"),
    "code_bytes": ("each allocator-mode cell: executable-section bytes of the stage-1 output (`code_bytes`)", "exact ratio"),
    "generated_runtime": ("generated-runtime cell: harness span of the stage-1 compilers' compile (metric `wall`)", "upper 95% bound")}
RETIREMENT_EXTERNAL = [{"check": "self_host_fixed_point",
                        "how": "the decision record also requires the repository's byte-identical self-host fixed point on the candidate "
                               "tree (`./build.sh test_self_host --config Release`); the lab does not run it"}]


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
    if value == 0 and decimals >= 6:
        return "0 (to %d decimals)" % decimals
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


def valid_ns(value):
    """True for a producer-declared duration: a non-negative int or float (not a bool)."""
    return isinstance(value, (int, float)) and not isinstance(value, bool) and value >= 0 and value == value and value != float("inf")


def measured_input(metrics):
    """The first measured input record with a valid total_ns, or None."""
    for record in (metrics or {}).get("inputs", []):
        if record.get("measured") == 1 and "start_ns" in record and valid_ns(record.get("total_ns")):
            return record
    return None


def phase_ns_series(records, phase):
    """[ns per record] of a phase ("total" included), or None when any record
    lacks a valid value: absent telemetry is unavailable, never zero, and a
    partially populated series is incomplete rather than a smaller population."""
    values = [record.get(phase + "_ns") for record in records]
    return values if values and all(valid_ns(value) for value in values) else None


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
            # `--sort symbol` (IBS) prints an unresolved address bare; name it
            # as `--sort dso,symbol` does when perf knows no dso.
            if re.fullmatch(r"0x[0-9a-f]+", name):
                name = "[unknown] " + re.sub(r"^0x0*(?=[0-9a-f])", "0x", name)
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
    def __init__(self, output, perf="perf", cpu=None, ide=None, repo_root=".", extra=(), sudo=False, fresh_copy=False):
        self.output = os.path.abspath(output)
        self.perf = perf
        self.cpu = cpu
        self.ide = ide
        self.fresh_copy = fresh_copy
        # The fresh copy the workload runs inside Lab.instance(), else None (lab.ide).
        self.binary = None
        self.repo_root = os.path.abspath(repo_root)
        self.extra = list(extra)
        self.sudo = sudo
        self.environment = dict(os.environ, LC_ALL="C", LANG="C")
        self.meta = load_meta(self.output)
        self.last_elapsed = None
        self.last_maxrss = None
        self.last_harness_rss = None
        self.last_cpu_s = None
        # Whether timed runs go under `perf stat` (probe_counters): None until
        # probed, then True, or False when perf is absent or cannot count.
        self.counters = None

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
        return [self.binary or self.ide] + DEFAULT_COMPILE + self.extra + list(flags) + ["-o", output_path]

    def record_options(self):
        """Extra `perf record` options: a fresh copy's unique path would add one
        cached copy of the binary per capture to ~/.debug, so skip the build-id
        cache; reports and annotations run while the copy still exists."""
        return ["--no-buildid-cache"] if self.binary else []

    @contextlib.contextmanager
    def instance(self):
        """With fresh_copy, run the workload from a new copy of the binary
        (fresh_binary_copy) in its own directory under DIR/instances for the
        duration of the block, then delete it; else lab.ide unchanged.  The
        copy keeps the binary's basename (the IBS filter matches on it) and is
        made before any timed span starts."""
        if not self.fresh_copy:
            yield self.ide
            return
        parent = self.directory("instances")
        directory = tempfile.mkdtemp(prefix="run-", dir=parent)
        path = os.path.join(directory, os.path.basename(self.ide))
        previous = self.binary
        try:
            fresh_binary_copy(self.ide, path)
            self.binary = path
            yield path
        finally:
            self.binary = previous
            shutil.rmtree(directory, ignore_errors=True)
            with contextlib.suppress(OSError):
                os.rmdir(parent)

    def run_command(self, argv, log=None, timeout=COMMAND_TIMEOUT, stdout_path=None):
        """Run argv in the repository root; returns (exit status, stdout, stderr).
        last_elapsed is its monotonic span; last_maxrss and last_harness_rss
        are run_measured's peak RSS and resident harness floor in bytes (or None)."""
        output = None
        if "-o" in argv:
            output = argv[argv.index("-o") + 1]
            # perf's own -o is followed by -- and a compiler -o. Remove only
            # the final requested artifact, never a retained reference.
            if "--" in argv:
                workload = argv[argv.index("--") + 1:]
                output = workload[workload.index("-o") + 1] if "-o" in workload else None
            if output and os.path.isfile(output):
                os.remove(output)
        started = time.monotonic()
        usage = {}
        status, out, err, self.last_maxrss, self.last_harness_rss = run_measured(argv, self.repo_root, self.environment, timeout, usage)
        self.last_cpu_s = usage.get("cpu_s")
        if status == 0 and output and not os.path.isfile(output):
            status, err = 1, err + b"\ncompiler did not create the requested output\n"
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


def maxrss_bytes(kilobytes_or_bytes):
    """ru_maxrss in bytes: Linux reports KiB, macOS bytes."""
    return kilobytes_or_bytes * (1 if sys.platform == "darwin" else 1024)


def harness_resident_bytes():
    """Current Linux resident footprint inherited by a spawned child.
    A parent's historical ru_maxrss is not inherited by all spawn strategies;
    using it rejects valid samples after large temporary allocations."""
    value = None
    if sys.platform.startswith("linux"):
        try:
            with open("/proc/self/statm") as handle:
                value = int(handle.read().split()[1]) * os.sysconf("SC_PAGE_SIZE")
        except (OSError, ValueError, IndexError):
            pass
    return value


def run_measured(argv, cwd, environment, timeout, usage_out=None):
    """(exit status, stdout bytes, stderr bytes, peak RSS bytes, harness
    resident RSS bytes) of argv; both RSS values None where unsupported.
    A usage_out dict receives `cpu_s`, the child's user+system seconds from
    the same wait4 (None where unsupported): the CPU time measured without
    perf (#2768).

    Peak RSS is ru_maxrss from os.wait4 on the direct child.  At reap Linux
    reports the largest of that process's own high-water RSS and the ru_maxrss
    of every descendant it waited for; it is a maximum, never a sum.  The
    process's own mark survives exec. Spawn strategies can inherit the
    parent's current resident footprint, but not necessarily its historical
    high-water mark. The resident footprint at spawn is returned second.
    Under `taskset -c N perf
    stat -- ide ...` the value is max(harness, perf's own RSS, the compiler's
    RSS over all its threads); rss_value keeps only values clearly above the
    first two.  Output goes to temporary files, so a chatty child cannot
    block on a full pipe."""
    harness = harness_resident_bytes()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        try:
            process = subprocess.Popen(argv, cwd=cwd, env=environment, stdout=out, stderr=err, start_new_session=os.name == "posix")
        except FileNotFoundError as error:
            return 127, b"", str(error).encode(), None, harness
        expired = []

        def expire():
            expired.append(True)
            try:
                if os.name == "posix":
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
            except ProcessLookupError:
                pass
        timer = threading.Timer(timeout, expire)
        timer.start()
        try:
            if hasattr(os, "wait4"):
                _, wait_status, usage = os.wait4(process.pid, 0)
                process.returncode = os.waitstatus_to_exitcode(wait_status)
                rss = maxrss_bytes(usage.ru_maxrss)
                if usage_out is not None:
                    usage_out["cpu_s"] = usage.ru_utime + usage.ru_stime
            else:
                process.wait()
                rss = None
        except BaseException:
            expire()
            process.wait()
            raise
        finally:
            timer.cancel()
        out.seek(0)
        err.seek(0)
        stdout, stderr = out.read(), err.read()
    if expired:
        return 124, stdout, stderr + b"\ntimeout\n", rss, harness
    return process.returncode, stdout, stderr, rss, harness


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


def fresh_binary_copy(source, destination):
    """Copy an executable by plain read/write into a new file (a new inode, so
    new page-cache pages; never a hard link, reflink or copy_file_range, which
    could share the source's pages or extents), fsync it so no writeback runs
    during the timed span, and close it before anyone executes it (ETXTBSY)."""
    with open(source, "rb") as reader, open(destination, "xb") as writer:
        for block in iter(lambda: reader.read(1 << 20), b""):
            writer.write(block)
        writer.flush()
        os.fchmod(writer.fileno(), os.stat(source).st_mode & 0o7777)
        os.fsync(writer.fileno())


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text)[:80]


def code_sections(path):
    """Generated code bytes of one output: {format, code_bytes, file_bytes,
    sections [{name, size}], reason}.  code_bytes is the exact sum of sh_size
    over ELF sections with SHF_EXECINSTR that occupy file bytes (not
    SHT_NOBITS), ELF32 or ELF64 of either byte order.  Mach-O, PE and anything
    else are NA with a reason (#511: no validated parser, and whole-file size
    is never substituted); file_bytes stays diagnostic.  Only the header,
    section table and section-name table are read (elf_code_sections), so
    the harness's resident footprint stays small."""
    result = {"format": None, "code_bytes": None, "file_bytes": None, "sections": [], "reason": None}
    try:
        with open(path, "rb") as handle:
            result["file_bytes"] = os.fstat(handle.fileno()).st_size
            magic = handle.read(64)
            if magic[:4] in (b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
                result.update(format="mach-o", reason="no validated Mach-O code-section parser")
            elif magic[:2] == b"MZ":
                result.update(format="pe", reason="no validated PE code-section parser")
            elif magic[:4] != b"\x7fELF":
                result.update(format="unknown", reason="not an ELF, Mach-O or PE file")
            else:
                elf_code_sections(handle, magic, result)
    except OSError as error:
        result["reason"] = "unreadable: %s" % error
    return result


def elf_code_sections(handle, header_bytes, result):
    """Fill result (code_sections) from an open ELF file whose first 64
    bytes are header_bytes."""
    size = result["file_bytes"]
    wide, endian = header_bytes[4:5] == b"\x02", {b"\x01": "<", b"\x02": ">"}.get(header_bytes[5:6])
    result["format"] = "elf64" if wide else "elf32" if header_bytes[4:5] == b"\x01" else "elf"
    if result["format"] == "elf" or endian is None:
        result["reason"] = "truncated ELF header" if len(header_bytes) < 6 else "unknown ELF class or data encoding"
        return
    header = struct.Struct(endian + ("16xHHIQQQIHHHHHH" if wide else "16xHHIIIIIHHHHHH"))
    entry = struct.Struct(endian + ("IIQQQQIIQQ" if wide else "IIIIIIIIII"))
    if len(header_bytes) < header.size:
        result["reason"] = "truncated ELF header"
        return
    fields = header.unpack_from(header_bytes)
    shoff, shentsize, shnum, shstrndx = fields[5], fields[10], fields[11], fields[12]
    if shoff == 0 or shentsize < entry.size or shoff + entry.size > size:
        result["reason"] = "no usable section header table"
        return
    handle.seek(shoff)
    first = entry.unpack(handle.read(entry.size))
    if shnum == 0:  # extended numbering: the count is section 0's sh_size
        shnum = first[5]
    if shstrndx == 0xffff:  # SHN_XINDEX: the index is section 0's sh_link
        shstrndx = first[6]
    if shnum == 0 or shoff + shnum * shentsize > size:
        result["reason"] = "section header table outside the file (%d entries at %d)" % (shnum, shoff)
        return
    handle.seek(shoff)
    table_bytes = handle.read(shnum * shentsize)
    # (sh_name, sh_type, sh_flags, sh_offset, sh_size)
    sections = [tuple(entry.unpack_from(table_bytes, index * shentsize)[i] for i in (0, 1, 2, 4, 5)) for index in range(shnum)]
    names = None
    if shstrndx < shnum and sections[shstrndx][1] != SHT_NOBITS and sections[shstrndx][3] + sections[shstrndx][4] <= size:
        handle.seek(sections[shstrndx][3])
        names = handle.read(sections[shstrndx][4])
    total = 0
    for name, kind, flags, offset, length in sections:
        if flags & SHF_EXECINSTR and kind != SHT_NOBITS:
            if offset + length > size:
                result["reason"] = "executable section outside the file"
                result["sections"] = []
                return
            total += length
            label = "#%d" % name
            if names is not None and name < len(names):
                end = names.find(b"\0", name)
                label = names[name:end if end >= 0 else len(names)].decode("utf-8", "replace")
            result["sections"].append({"name": label, "size": length})
    result["code_bytes"] = total


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
    enabled = lab.meta.get("collection", {}).get("metrics_out", lab.meta["capabilities"].get("metrics_out"))
    return ["-fmetrics-out=" + metrics_path] if metrics_path and enabled else []


def estimate_other_compiles(groups, skip, sudo, threads=False):
    """Rough cost of the steps after `timed`, in compile-equivalents (one
    compile's wall time), for choose_run_count."""
    cost = {"topdown": TOPDOWN_COMPILES_PER_GROUP * (len(groups) if groups is not None else 8),
            "timeline": 3, "sampling": SAMPLING_COMPILES, "ibs": IBS_COMPILES if sudo else 0, "threads": 1 if threads else 0, "micro": 3}
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


def probe_counters(lab):
    """Decide once whether timed runs can go under `perf stat` (#2768): a
    `perf stat -- true` with TIMED_EVENTS must exit 0 and write a task-clock
    line. Without perf, or when perf cannot open events (containers with a
    strict perf_event_paranoid), timed runs execute the workload directly:
    wall time, CPU time (wait4 user+system) and peak RSS are still measured
    and every counter is NA, never zero. A usable perf that counts zero stays
    a perf defect that timed_problems degrades, not a reason to fall back."""
    if lab.counters is None:
        csv_path = lab.path("perf-probe.csv")
        status, _, err = lab.run_command([lab.perf, "stat", "-x,", "-o", csv_path, "-e", ",".join(TIMED_EVENTS), "--", "true"],
                                         timeout=60)
        task = task_seconds(parse_stat(read_text(csv_path) or "")) if status == 0 else None
        lab.counters = task is not None
        reason = "perf stat counts task-clock" if lab.counters else "perf stat unusable (%s)" % (
            "exit %d: %s" % (status, last_reason(err)) if status != 0 else "no task-clock line")
        lab.meta.setdefault("capabilities", {})["perf_stat"] = {"usable": lab.counters, "reason": reason}
        lab.save_meta()
        if not lab.counters:
            print("uarch_lab: %s; timing without perf, counters NA" % reason, flush=True)
    return lab.counters


def timed_command(lab, csv_path):
    """The pinned prefix of every timed run (the workload follows): `perf stat
    ... --` when probe_counters found it usable, else the pin alone."""
    counters = [lab.perf, "stat", "-x,", "-o", csv_path, "-e", ",".join(TIMED_EVENTS), "--"] if probe_counters(lab) else []
    return lab.pin() + counters


def probe_workload(lab, directory, out):
    """Run the workload once with `-fsource-metrics` (plain when the binary
    rejects it) and once with `-fmetrics-out`, recording both capabilities
    in lab.json; raises when the workload itself fails.  Also records the
    timed wrapper's own peak RSS (`perf stat -- true`, wrapper_rss_bytes),
    the floor rss_value compares every run against."""
    capabilities = lab.meta["capabilities"]
    status, _, _ = lab.run_command(timed_command(lab, os.path.join(directory, "wrapper.csv")) + ["true"],
                                   log=os.path.join(directory, "wrapper-rss.log"))
    capabilities["wrapper_rss_bytes"] = lab.last_maxrss if status == 0 else None
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


def step_timed(lab, runs, warmups, target_minutes=15.0, lab_started=None, other_compiles=0):
    directory = lab.directory("timed")
    out = os.path.join(directory, "out.exe")
    probe_workload(lab, directory, out)
    if warmups == 0:
        status, _, err = lab.run_command(lab.pin() + lab.workload(out, compile_flags(lab, os.path.join(directory, "reference.ccmetrics"))),
                                         log=os.path.join(directory, "reference-run.log"))
        if status != 0:
            raise RuntimeError("reference compile failed: " + err.strip()[-400:])
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
    plan = {"runs": runs, "reason": "--runs %d" % runs, "fresh_copy": lab.fresh_copy} if runs is not None else None
    records, best, index = [], None, 0
    while count is None or index < count:
        index += 1
        csv_path = os.path.join(directory, "run-%04d.csv" % index)
        metrics_path = os.path.join(directory, "run-%04d.ccmetrics" % index)
        with lab.instance():
            status, out_text, err = lab.run_command(timed_command(lab, csv_path) + lab.workload(out, compile_flags(lab, metrics_path)))
        span = lab.last_elapsed
        identical = status == 0 and filecmp.cmp(reference, out, shallow=False)
        if status != 0:
            with open(os.path.join(directory, "run-%04d.err" % index), "w") as handle:
                handle.write(out_text[-20000:] + err[-20000:])
        rows = parse_stat(read_text(csv_path) or "")
        task = task_seconds(rows) if lab.counters else lab.last_cpu_s
        wall_ns = parse_cc_metrics(read_text(metrics_path) or "")["header"].get("wall_ns")
        if status == 0:
            best = span if best is None else min(best, span)
        records.append({"run": index, "exit": status, "identical": identical, "span_s": round(span, 6),
                        "maxrss_bytes": lab.last_maxrss, "harness_rss_bytes": lab.last_harness_rss,
                        "wrapper_rss_bytes": lab.meta["capabilities"].get("wrapper_rss_bytes"),
                        "cpu_s": lab.last_cpu_s, "counters": lab.counters})
        warning = "" if task is not None and task > 0 else " TASK-CLOCK %s: %s" % (
            "MISSING" if task is None else "NON-POSITIVE", stat_lines(rows).get("task-clock", "no task-clock line in " + csv_path))
        print("[timed] %d/%s span %.4f s, task-clock %s s, compiler wall %s s (min span %s)%s%s" % (
            index, count if count is not None else "?", span, fmt(task, ".4f"), fmt(ratio(wall_ns, 1e9) if isinstance(wall_ns, (int, float)) else None, ".4f"),
            fmt(best, ".4f"), "" if identical else " OUTPUT DIFFERS" if status == 0 else " FAILED", warning), flush=True)
        if count is None and index == PILOT_RUNS:
            spans = [record["span_s"] for record in records if record["exit"] == 0]
            per_run = statistics.median(spans) if spans else 2.2
            count, reason = choose_run_count(per_run, time.monotonic() - started, target_minutes, other_compiles)
            plan = {"runs": count, "reason": reason, "fresh_copy": lab.fresh_copy}
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
    the JSON is unreadable); returns (exit, format, rows).  Each invocation
    runs its own fresh binary instance (Lab.instance)."""
    with lab.instance():
        status, _, _ = lab.run_command(lab.pin() + [lab.perf, "stat", "-r", str(repeats), "-j", "-o", base + ".json", "-M", selector, "--"]
                                       + lab.workload(lab.path("topdown", "out.exe")), log=base + ".log")
    rows = parse_stat_json(read_text(base + ".json") or "")
    if status == 0 and rows:
        return status, "json", rows
    if status != 0:
        return status, "json", rows
    with lab.instance():
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
    with lab.instance():
        status, _, err = lab.run_command(lab.pin() + [lab.perf, "stat", "-I", str(INTERVAL_MS), "-x,", "-o", lab.path("timeline", "interval.csv"),
                                                      "-e", ",".join(INTERVAL_EVENTS), "--"] + lab.workload(lab.path("timeline", "out.exe"), flags),
                                         log=lab.path("timeline", "interval.log"))
    if status != 0:
        raise RuntimeError("interval run failed: " + err.strip()[-400:])
    with lab.instance():
        status2, _, _ = lab.run_command(lab.pin() + [lab.perf, "stat", "-I", str(INTERVAL_MS), "-x,", "-o", lab.path("timeline", "pipeline-l1.csv"),
                                                     "-M", "PipelineL1", "--"] + lab.workload(lab.path("timeline", "out.exe"), compile_flags(lab, lab.path("timeline", "pipeline-l1.ccmetrics"))),
                                        log=lab.path("timeline", "pipeline-l1.log"))
    return "interval run ok; PipelineL1 interval run exit=%d" % status2


def step_sampling(lab, detail=True):
    """One `perf record` per SAMPLE_EVENTS entry plus every page fault, with
    self reports; detail=False (compare) skips the children, srcline, annotate
    and fault-address listings.  Each capture runs its own fresh binary
    instance (Lab.instance), kept until that capture's reports are written."""
    directory = lab.directory("sampling")
    out = os.path.join(directory, "out.exe")
    recorded = []
    events = list(SAMPLE_EVENTS)
    for name, event in events:
        with lab.instance():
            captured = sampling_capture(lab, directory, out, name, event, detail)
        if captured:
            recorded.append(captured)
    data = os.path.join(directory, "faults.data")
    with lab.instance():
        status, _, _ = lab.run_command(lab.pin() + [lab.perf, "record", "-q"] + lab.record_options()
                                       + ["-e", "page-faults:u", "-c", "1", "-d", "--call-graph", "fp", "-o", data, "--"]
                                       + lab.workload(out), log=os.path.join(directory, "faults.record.log"))
        print("[sampling] page-faults exit=%d" % status, flush=True)
        if status == 0:
            recorded.append("faults")
            report = [lab.perf, "report", "-i", data, "--stdio", "--no-inline", "--no-children"]
            lab.run_command(report + ["--sort", "dso,symbol", "-g", "none", "--percent-limit", "0.2"], stdout_path=os.path.join(directory, "faults.self.txt"))
        if status == 0 and detail:
            lab.run_command(report + ["--sort", "symbol", "-g", "caller", "--percent-limit", "1"], stdout_path=os.path.join(directory, "faults.callers.txt"))
            lab.run_command([lab.perf, "script", "-i", data, "-F", "time,addr,ip,sym", "--hide-call-graph", "--no-inline", "--show-mmap-events"],
                            stdout_path=os.path.join(directory, "faults.script.txt"), log=os.path.join(directory, "faults.script.log"))
    if not recorded:
        raise RuntimeError("no event could be recorded")
    return "recorded: " + ", ".join(recorded)


def sampling_capture(lab, directory, out, name, event, detail):
    """One step_sampling capture (cycles falls back to cpu-clock) and its
    reports; the recorded capture's name, or None."""
    data = os.path.join(directory, name + ".data")
    record = [lab.perf, "record", "-q"] + lab.record_options() + ["-F", "2999"]
    status, _, _ = lab.run_command(lab.pin() + record + ["-e", event, "--call-graph", "fp", "-o", data, "--"]
                                   + lab.workload(out), log=os.path.join(directory, name + ".record.log"))
    if status != 0 and name == "cycles":
        name, event, data = "cpu-clock", "cpu-clock:u", os.path.join(directory, "cpu-clock.data")
        status, _, _ = lab.run_command(lab.pin() + record + ["-e", event, "--call-graph", "fp", "-o", data, "--"]
                                       + lab.workload(out), log=os.path.join(directory, name + ".record.log"))
    print("[sampling] %s exit=%d" % (event, status), flush=True)
    if status == 0:
        report = [lab.perf, "report", "-i", data, "--stdio", "--no-inline"]
        # dso,symbol names an unresolved address by its binary (parse_report).
        lab.run_command(report + ["--no-children", "--sort", "dso,symbol", "-g", "none", "--percent-limit", "0.2"],
                        stdout_path=os.path.join(directory, name + ".self.txt"), log=os.path.join(directory, name + ".self.log"))
    if status == 0 and detail:
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
    return name if status == 0 else None


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


def step_threads(lab):
    """Opt-in Superluminal-style capture (run --threads, compare --profile-steps
    threads): `perf record -F THREAD_SAMPLE_HZ --call-graph fp --switch-events`
    with the richest THREAD_MODES entry the host permits. The binary runs in
    place, not as a fresh copy, so threads/threads.data stays resolvable:
    open it with Hotspot (`hotspot threads/threads.data`). Writes
    threads.json (mode, events, why richer modes failed), a per-thread sample
    report and, with scheduler events, `perf sched timehist --summary`."""
    directory = lab.directory("threads")
    data = os.path.join(directory, "threads.data")
    out = os.path.join(directory, "out.exe")
    record = {"hz": THREAD_SAMPLE_HZ, "data": data, "mode": None, "description": None, "events": [], "refused": []}
    for mode, events, description in THREAD_MODES:
        if record["mode"] is None:
            log = os.path.join(directory, mode + ".record.log")
            status, _, err = lab.run_command(lab.pin() + [lab.perf, "record", "-q", "-F", str(THREAD_SAMPLE_HZ), "--call-graph", "fp",
                                                          "--switch-events"] + events + ["-o", data, "--"] + lab.workload(out), log=log)
            print("[threads] %s exit=%d" % (mode, status), flush=True)
            if status == 0 and os.path.isfile(data):
                record.update(mode=mode, description=description, events=events[1::2])
            else:
                record["refused"].append({"mode": mode, "exit": status, "reason": last_reason(read_text(log) or err)})
    write_json(os.path.join(directory, "threads.json"), record)
    if record["mode"] is None:
        raise RuntimeError("no thread capture could be recorded: " + "; ".join("%s: %s" % (item["mode"], item["reason"])
                                                                             for item in record["refused"]))
    lab.run_command([lab.perf, "report", "-i", data, "--stdio", "--no-children", "--sort", "tid", "-g", "none"],
                    stdout_path=os.path.join(directory, "threads.tid.txt"), log=os.path.join(directory, "threads.tid.log"))
    if record["mode"] == "full":
        lab.run_command([lab.perf, "sched", "timehist", "-i", data, "--summary"],
                        stdout_path=os.path.join(directory, "threads.sched.txt"), log=os.path.join(directory, "threads.sched.log"))
    return "mode %s (%s); open %s with Hotspot" % (record["mode"], record["description"], data)


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
    notes = []
    # Each capture runs its own fresh instance; all stay until the filtered
    # reports, which resolve symbols through them, are derived.
    with contextlib.ExitStack() as instances:
        for pmu in present + ["mem"]:
            instances.enter_context(lab.instance())
            command = drop + lab.pin() + lab.workload(os.path.join(directory, "out.exe"))
            data = os.path.join(directory, pmu + ".data")
            record = [lab.perf, "mem", "record"] if pmu == "mem" else [lab.perf, "record", "-q", "-e", pmu + "//"]
            status, _, _ = lab.run_command(["sudo"] + record + lab.record_options() + ["-o", data] + cpu + ["--"] + command,
                                           log=os.path.join(directory, pmu + ".record.log"))
            notes.append("%s exit=%d" % ("perf mem" if pmu == "mem" else pmu, status))
        lab.run_command(["sudo", "chown", "-R", "%d:%d" % (os.getuid(), os.getgid()), directory])
        notes += derive_ibs_reports(lab, directory, os.path.basename(lab.ide))
    return "; ".join(notes)


def step_micro(lab):
    with lab.instance() as binary:
        status, out, err = lab.run_command(lab.pin() + [binary, "bench"], log=lab.path("micro", "bench.log"),
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
        span, source = record.get("span_s"), "runs.json"
        if span is None and record["run"] in spans:
            span, source = spans[record["run"]], "commands.log"
        runs.append(load_run(record, os.path.join(base, "run-%04d.csv" % record["run"]),
                             os.path.join(base, "run-%04d.ccmetrics" % record["run"]), span, source))
    return runs


def load_run(record, csv_path, metrics_path, span, source):
    """One timed run: its record plus perf counters, compiler metrics and wall times."""
    rows = parse_stat(read_text(csv_path) or "")
    metrics = parse_cc_metrics(read_text(metrics_path) or "")
    wall_ns = metrics["header"].get("wall_ns")
    # A run timed without perf (counters False) takes its CPU time from wait4.
    task = record.get("cpu_s") if record.get("counters") is False else task_seconds(rows)
    return {**record, "values": stat_values(rows), "lines": stat_lines(rows), "metrics": metrics,
            "span_s": span, "span_source": source if span is not None else None,
            "cc_wall_s": ratio(wall_ns, 1e9) if isinstance(wall_ns, (int, float)) else None,
            "task_s": task}


def run_metrics(run):
    """Per-run values of COMPARE_METRICS (None when not measured)."""
    values = run["values"]
    instructions, misses = values.get("instructions"), values.get("branch-misses")
    return {"wall": positive(run["span_s"]), "task_clock": positive(run["task_s"]), "compiler_wall": positive(run["cc_wall_s"]),
            "instructions": instructions, "cycles": values.get("cycles"), "ipc": ratio(instructions, values.get("cycles")),
            "branch_misses": misses, "branch_mpki": ratio(misses, instructions, 1000), "page_faults": values.get("page-faults"),
            "minor_faults": values.get("minor-faults"), "major_faults": values.get("major-faults"), "peak_rss": rss_value(run)}


def rss_floor(run):
    """The larger of the two non-compiler terms in a run's maxrss_bytes: the
    harness's resident RSS at spawn and the perf wrapper's own (probe), or
    None when either is unknown."""
    harness, wrapper = positive(run.get("harness_rss_bytes")), positive(run.get("wrapper_rss_bytes"))
    return max(harness, wrapper) if harness is not None and wrapper is not None else None


def rss_value(run):
    """A run's compiler peak RSS in bytes, or None.  maxrss_bytes is the
    maximum of the harness floor, the perf wrapper and the compiler
    (run_measured), so it is the compiler's only when above
    RSS_WRAPPER_MARGIN times rss_floor; without a floor (probe failed, older
    directory) it is NA."""
    value, floor = positive(run.get("maxrss_bytes")), rss_floor(run)
    return value if value is not None and floor is not None and value > RSS_WRAPPER_MARGIN * floor else None


def timed_problems(good):
    """Fail-closed checks: every successful run needs a positive wall time
    and task-clock; a counted zero is a perf defect, not a measurement."""
    problems = []
    for run in good:
        if positive(run["span_s"]) is None and positive(run["cc_wall_s"]) is None:
            problems.append("run %d: no positive wall time (harness span %s, compiler wall_ns %s; perf CSV `%s`)" % (
                run["run"], fmt(run["span_s"], ".3f"), fmt(run["cc_wall_s"], ".4f"),
                run["lines"].get("duration_time") or run["lines"].get("task-clock") or "no CSV"))
        if run.get("counters") is False:
            # Timed without perf: CPU time is wait4's user+system instead.
            if positive(run["task_s"]) is None:
                problems.append("run %d: no positive wait4 CPU time (%s)" % (run["run"], fmt(run["task_s"])))
        else:
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
        " Run count: %s." % plan["reason"] if plan else "") + (
        " Each run executed a fresh copy of the binary." if plan and plan.get("fresh_copy") else ""))

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
    guarded(lines, problems, lambda: rss_line(good))
    guarded(lines, problems, lambda: code_line(os.path.join(directory, "timed", "reference.exe")))
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


def rss_line(good):
    """Peak RSS of the timed runs (rss_value) against the perf wrapper's floor."""
    values = [rss_value(run) for run in good]
    stats = summarize([value for value in values if value is not None])
    floors = [rss_floor(run) for run in good if rss_floor(run)]
    return "- peak RSS (wait4 ru_maxrss of taskset/perf and the compiler; harness/perf floor up to %s bytes): median %s, min %s, max %s bytes%s" % (
        fmt(max(floors) if floors else None), fmt(stats and stats["median"]), fmt(stats and stats["min"]), fmt(stats and stats["max"]),
        "" if all(value is not None for value in values) else "; %d of %d runs NA (not above %gx the floor, or not measured)" % (
            sum(value is None for value in values), len(values), RSS_WRAPPER_MARGIN))


def code_line(path):
    if not os.path.isfile(path):
        return None
    code = code_sections(path)
    return "- output code sections (%s): %s bytes of %s file bytes%s" % (
        code["format"], fmt(code["code_bytes"]), fmt(code["file_bytes"]), " (%s)" % code["reason"] if code["reason"] else "")


def timed_phase_lines(directory, good, findings):
    records = [record for record in (measured_input(run["metrics"]) for run in good) if record]
    if not records:
        return ["", "Phase breakdown: NA -- the binary %s." % _metrics_reason(directory)]
    total = statistics.median(record["total_ns"] for record in records)
    rows, medians = [], {}
    for phase in PHASES:
        series = phase_ns_series(records, phase)
        medians[phase] = statistics.median(series) if series else None
        rows.append([phase, fmt(ratio(medians[phase], 1e6), ",.2f"), percent(ratio(medians[phase], total))] if series else
                    [phase, "NA (%d of %d records)" % (sum(valid_ns(record.get(phase + "_ns")) for record in records), len(records)), "NA"])
    rows.append(["input total", fmt(ratio(total, 1e6), ",.2f"), "100.0%"])
    headers = [run["metrics"]["header"] for run in good if run["metrics"]["header"]]
    lines = ["", "Per-phase median over %d `-fmetrics-out` records:" % len(records), ""] + table(["phase", "median ms", "share of input"], rows)
    lines += ["", "- arena peak median %s bytes; arena retained median %s; peak RSS median %s bytes" % (
        fmt(statistics.median(record.get("arena_peak_bytes", 0) for record in records)),
        fmt(statistics.median(record.get("arena_retained_bytes", 0) for record in records)),
        fmt(statistics.median(header.get("peak_rss_bytes", 0) for header in headers) if headers else None))]
    available = [phase for phase in PHASES if medians[phase] is not None]
    slowest = max(available, key=lambda phase: medians[phase]) if available else None
    if slowest:
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


def render_threads(directory, findings, problems):
    record = json.loads(read_text(os.path.join(directory, "threads", "threads.json")) or "{}")
    lines = []
    if not record.get("mode"):
        problems.append("no thread capture recorded")
        lines.append("NA (no thread capture; %s)." % ("; ".join("%s: %s" % (item.get("mode"), item.get("reason"))
                                                                for item in record.get("refused", [])) or "not run"))
    else:
        lines += ["Mode `%s`: %s, %d Hz, frame-pointer stacks. Open `%s` with Hotspot (`hotspot %s`) for the per-thread "
                  "timeline, off-CPU time and flame graphs; `perf script -i` exports it for the Firefox Profiler." % (
                      record["mode"], record["description"], record["hz"], os.path.relpath(record["data"], directory),
                      record["data"])]
        for item in record.get("refused", []):
            lines.append("- `%s` refused (exit %s): %s" % (item.get("mode"), item.get("exit"), item.get("reason")))
        tid = read_text(os.path.join(directory, "threads", "threads.tid.txt")) or ""
        rows = [line for line in tid.splitlines() if line.strip() and not line.startswith("#")]
        if not rows:
            problems.append("per-thread report has no rows")
        lines += ["", "Samples per thread:"] + fenced("\n".join(rows[:40]) or "no rows")
        sched = read_text(os.path.join(directory, "threads", "threads.sched.txt"))
        if sched:
            lines += ["", "Scheduler summary (`perf sched timehist --summary`):"] + fenced("\n".join(sched.splitlines()[:60]))
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
             "threads": ("6b. Thread timeline (Hotspot, opt-in)", render_threads), "micro": ("7. Micro-benchmark", render_micro)}


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
    write_json(os.path.join(directory, "summary.json"), run_summary(directory, meta, statuses, findings))
    return text


# ---------------------------------------------------------------- machine-readable summaries

def write_json(path, value):
    with open(path, "w") as handle:
        json.dump(value, handle, indent=1, sort_keys=True)
        handle.write("\n")


def host_facts(directory):
    facts = json.loads(read_text(os.path.join(directory, "env", "env.json")) or "{}")
    return {key: facts.get(key) for key in HOST_KEYS}


def topdown_values(directory):
    """[{group, metric, value, unit, source}] for every measured metric: the
    value measured alone when a multiplexed group was re-measured (`alone`),
    else the group's (`group`, or `multiplexed-group` for scaled estimates).
    A metric dividing by a zero duration_time is None, never 0."""
    values_out = []
    for entry in json.loads(read_text(os.path.join(directory, "topdown", "groups.json")) or "[]"):
        if entry.get("exit") != 0:
            continue
        group = entry["group"]
        rows, _ = load_stat_file(os.path.join(directory, "topdown", safe_name(group)))
        values, multiplex = stat_values(rows), multiplex_percent(rows)
        split = {}
        for item in entry.get("split", []):
            split_rows, _ = load_stat_file(os.path.join(directory, "topdown", safe_name(group) + ".split", safe_name(item["metric"])))
            chosen = [row for row in metric_rows(split_rows) if row["metric_name"] == item["metric"]]
            if item.get("exit") == 0 and chosen:
                split[item["metric"]] = (chosen[0], stat_values(split_rows))
        for row in metric_rows(rows):
            own, own_values = split.get(row["metric_name"], (row, values))
            value = own["metric_value"]
            if time_divided(own) and "duration_time" in own_values and positive(own_values["duration_time"]) is None:
                value = None
            source = "alone" if row["metric_name"] in split else (
                "multiplexed-group" if multiplex is not None and multiplex < FULL_RUNNING else "group")
            values_out.append({"group": group, "metric": row["metric_name"], "value": value, "unit": own["metric_unit"], "source": source})
    return values_out


def event_count(text):
    """The first `# Event count (approx.): N` of a perf report, or None."""
    match = re.search(r"^# Event count \(approx\.\):\s*(\d+)", text or "", re.M)
    return int(match.group(1)) if match else None


def sample_count(text):
    """The first `# Samples: 15K of event` of a perf report (perf rounds to
    K/M, so the count is approximate), or None."""
    match = re.search(r"^# Samples:\s*(\d+(?:\.\d+)?)([KMG]?)\s+of event", text or "", re.M)
    if not match:
        return None
    return int(float(match.group(1)) * {"": 1, "K": 1e3, "M": 1e6, "G": 1e9}[match.group(2)])


def symbol_shares(text):
    """{symbol: share percent} of a perf report (first entry per symbol)."""
    shares = {}
    for entry in report_entries(text or ""):
        shares.setdefault(entry[2], entry[0])
    return shares


UNRESOLVED_SYMBOL = re.compile(r"^(\S+) 0x[0-9a-f]+$")


def collapse_unresolved(shares):
    """Sum the shares of unresolved addresses per binary (`libc.so.6 0x18f622`,
    `[unknown] 0x...`) into one `<binary> (unresolved addresses)` row.  Adjacent
    addresses of one unsymbolised routine move together, so diffing them one
    by one makes a single shift look like several independent movers (LAB4)."""
    collapsed = {}
    for symbol, share in shares.items():
        match = UNRESOLVED_SYMBOL.match(symbol)
        key = "%s (unresolved addresses)" % match.group(1) if match else symbol
        collapsed[key] = (collapsed.get(key) or 0.0) + share if share is not None else collapsed.get(key)
    return collapsed


def whole_or_zero(value):
    """An event-count estimate that rounds to zero is zero, never `-0`."""
    return 0.0 if abs(value) < 0.5 else value


def symbol_movers(a_text, b_text, limit=SYMBOL_MOVERS, min_samples=MIN_MOVER_SAMPLES):
    """Symbols whose sample share moved most from A to B.  A symbol absent
    from one report was below its --percent-limit there (share None, counted
    as 0 in the delta).  Estimates scale a share by the report's event count.
    `noise_pp` is the 95% binomial sampling noise of the share difference,
    None when one share is unknown; it assumes independent samples and
    underestimates (LAB3 A/A movers reached about 5x it), so `exceeds_bound`
    needs |delta| > MOVER_BOUND_FACTOR x noise (`beyond_noise`: > 1x).  A
    capture pair with fewer than min_samples samples on either side is
    `reliable` False and lists no movers."""
    a_shares, b_shares = collapse_unresolved(symbol_shares(a_text)), collapse_unresolved(symbol_shares(b_text))
    a_count, b_count = event_count(a_text), event_count(b_text)
    a_samples, b_samples = sample_count(a_text), sample_count(b_text)
    reliable = bool(a_samples and b_samples and min(a_samples, b_samples) >= min_samples)
    rows = []
    for symbol in set(a_shares) | set(b_shares):
        a_share, b_share = a_shares.get(symbol), b_shares.get(symbol)
        a_estimate = ratio(a_share, 100.0 / a_count) if a_count and a_share is not None else None
        b_estimate = ratio(b_share, 100.0 / b_count) if b_count and b_share is not None else None
        noise = None
        if a_samples and b_samples and a_share is not None and b_share is not None:
            pa, pb = a_share / 100.0, b_share / 100.0
            noise = 196.0 * math.sqrt(pa * (1 - pa) / a_samples + pb * (1 - pb) / b_samples)
        delta = (b_share or 0.0) - (a_share or 0.0)
        rows.append({"symbol": symbol, "a_share": a_share, "b_share": b_share, "delta_share": delta, "noise_pp": noise,
                     "beyond_noise": None if noise is None else abs(delta) > noise,
                     "exceeds_bound": None if noise is None else abs(delta) > MOVER_BOUND_FACTOR * noise,
                     "a_estimate": a_estimate, "b_estimate": b_estimate,
                     "delta_estimate": whole_or_zero((b_estimate or 0.0) - (a_estimate or 0.0)) if a_count and b_count else None})
    rows.sort(key=lambda row: (-abs(row["delta_share"]), row["symbol"]))
    note = None if reliable else "too few samples: shares unreliable (A %s, B %s; at least %d per capture needed)" % (
        fmt(a_samples), fmt(b_samples), min_samples)
    return {"a_event_count": a_count, "b_event_count": b_count, "a_samples": a_samples, "b_samples": b_samples,
            "reliable": reliable, "note": note, "movers": rows[:limit] if reliable else []}


def run_summary(directory, meta, statuses, findings):
    """summary.json of a `run` directory (RUN_SCHEMA), from the raw files."""
    config = meta.get("config", {})
    env = json.loads(read_text(os.path.join(directory, "env", "env.json")) or "{}")
    runs = load_timed(directory)
    good = [run for run in runs if run["exit"] == 0]
    series = [run_metrics(run) for run in good]
    timed = None
    if runs:
        timed = {"runs": len(runs), "failed": len(runs) - len(good), "identical": sum(bool(run["identical"]) for run in runs),
                 "plan": json.loads(read_text(os.path.join(directory, "timed", "plan.json")) or "null"),
                 "metrics": {key: dict(summarize([values[key] for values in series]) or dict.fromkeys(SUMMARY_KEYS), unit=unit, label=label)
                             for key, unit, _, label in COMPARE_METRICS}}
    records = [record for record in (measured_input(run["metrics"]) for run in good) if record]
    phases = None
    if records:
        total = statistics.median(record["total_ns"] for record in records)
        phases = {}
        for phase in PHASES + ("total",):
            values_ns = phase_ns_series(records, phase)
            median = statistics.median(values_ns) if values_ns else None
            phases[phase] = {"median_ms": None if median is None else median / 1e6, "share": ratio(median, total),
                             "samples": sum(valid_ns(record.get(phase + "_ns")) for record in records), "records": len(records)}
    source = parse_key_values(read_text(os.path.join(directory, "timed", "source.metrics")) or "")
    wall = summarize([values["wall"] for values in series])
    work_bytes = source.get("lexed.translated_bytes") if isinstance(source.get("lexed.translated_bytes"), int) else None
    work = {"translated_bytes": work_bytes, "code_lines": source.get("lexed.code_lines"), "tokens": source.get("preprocessed.tokens"),
            "mb_per_s_median": ratio(work_bytes, wall and wall["median"], 1e-6)} if work_bytes else None
    topdown = topdown_values(directory)
    hot = {}
    for name in list(SAMPLING_LABELS) + ["faults"]:
        text = read_text(os.path.join(directory, "sampling", name + ".self.txt"))
        if text is not None:
            hot[name] = [{"symbol": entry[2], "share": entry[0]} for entry in top_entries(text, HOT_SYMBOLS)]
    for name in ("ibs_op", "ibs_fetch"):
        text = read_text(os.path.join(directory, "ibs", name + ".workload.txt"))
        if text is not None:
            hot[name] = [{"symbol": entry[2], "share": entry[0]} for entry in top_entries(text, HOT_SYMBOLS)]
    category = dominant_category([(row["metric"], row["value"], row["unit"]) for row in topdown
                                  if re.fullmatch(r"(?i)pipelinel1|topdownl1|tmal1", row["group"])])
    ide = config.get("ide") or env.get("ide")
    return {"schema": RUN_SCHEMA, "directory": directory, "command": config.get("command"), "cpu": config.get("cpu"),
            "ide": {"path": ide, "sha256": env.get("ide_sha256")}, "host": host_facts(directory),
            "capabilities": meta.get("capabilities", {}),
            "steps": {step: {"status": statuses[step][0], "problems": statuses[step][1][:8]} for step in STEPS},
            "timed": timed, "phases": phases, "work": work, "topdown": topdown,
            "dominant_topdown_category": {"category": category[0], "percent": category[1]} if category else None,
            "hot_symbols": hot, "findings": findings}


# ---------------------------------------------------------------- A/B compare: statistics

def sign_test_rank(count, confidence=CONFIDENCE):
    """(k, coverage) of the distribution-free CI [x_(k), x_(n+1-k)] (1-based
    order statistics) for the median of `count` independent values.

    Coverage is exactly 1 - 2 P(X <= k-1) for X ~ Binomial(n, 1/2), the
    largest k whose coverage is still >= confidence; (0, None) when no k
    qualifies (n <= 5 at 95%).  Only independence of the pairs is assumed."""
    alpha = fractions.Fraction(1) - fractions.Fraction(confidence).limit_denominator(1000000)
    total, cumulative, k, coverage = 2 ** count, 0, 0, None
    for below in range((count + 1) // 2):
        cumulative += math.comb(count, below)
        if 2 * cumulative > alpha * total:
            break
        k, coverage = below + 1, float(1 - fractions.Fraction(2 * cumulative, total))
    return k, coverage


def median_ci(values, confidence=CONFIDENCE):
    """(low, high, coverage) of the sign-test CI for the median, or Nones."""
    values = sorted(values)
    k, coverage = sign_test_rank(len(values), confidence)
    if k == 0:
        return None, None, None
    return values[k - 1], values[len(values) - k], coverage


def bootstrap_geomean_ci(ratios, seed, resamples=BOOTSTRAP_RESAMPLES, confidence=CONFIDENCE):
    """(low, high) percentile bootstrap CI of the geometric-mean ratio,
    resampling pairs with random.Random(seed): the same seed and data give
    the same interval."""
    logs = [math.log(value) for value in ratios if value > 0]
    if len(logs) < 2:
        return None, None
    generator = random.Random(seed)
    count = len(logs)
    means = sorted(math.fsum(generator.choices(logs, k=count)) / count for _ in range(resamples))
    tail = (1.0 - confidence) / 2
    return math.exp(percentile(means, tail)), math.exp(percentile(means, 1.0 - tail))


def classify(low, high, time_metric=False, floor=0.0):
    """Outcome of a B/A CI against the practical floor (a fraction, e.g. 0.01):
    faster/slower (time) or lower/higher when the whole CI lies beyond
    1 -/+ floor, below-floor when it excludes 1.0 but reaches inside the floor,
    no detectable difference when it contains 1.0."""
    if low is None or high is None:
        return "inconclusive"
    if low <= 1.0 <= high:
        return "no detectable difference"
    if high < 1.0:
        return ("faster" if time_metric else "lower") if high < 1.0 - floor else "below-floor"
    return ("slower" if time_metric else "higher") if low > 1.0 + floor else "below-floor"


def compare_series(series, unit, direction, seed, time_metric=False, confidence=CONFIDENCE, resamples=BOOTSTRAP_RESAMPLES, floor=0.0):
    """B-versus-A statistics of [(a, b)] per pair.  `ratio` is the median of
    the per-pair ratios b/a, the estimate the sign-test CI brackets; ratios
    need every complete pair positive (a zero count leaves them None).
    `outcome` applies the practical floor (classify); `note` flags a count
    whose ratio of medians and median ratio disagree (bimodal counts)."""
    complete = [(a, b) for a, b in series if a is not None and b is not None]
    result = {"unit": unit, "direction": direction, "n": len(complete), "a_median": None, "b_median": None, "a_min": None,
              "b_min": None, "a_mad": None, "b_mad": None, "delta": None, "ratio": None, "ci_low": None, "ci_high": None,
              "ci_coverage": None, "geomean_ratio": None, "bootstrap_ci_low": None, "bootstrap_ci_high": None,
              "ratio_of_medians": None, "min_ratio": None, "change_percent": None, "outcome": "no data", "note": None}
    if not complete:
        return result
    a_stats, b_stats = summarize([a for a, _ in complete]), summarize([b for _, b in complete])
    result.update(a_median=a_stats["median"], b_median=b_stats["median"], a_min=a_stats["min"], b_min=b_stats["min"],
                  a_mad=a_stats["mad"], b_mad=b_stats["mad"], delta=b_stats["median"] - a_stats["median"],
                  ratio_of_medians=ratio(b_stats["median"], a_stats["median"]), min_ratio=ratio(b_stats["min"], a_stats["min"]))
    if any(a <= 0 or b <= 0 for a, b in complete):
        result["outcome"] = "no ratio (a zero value)"
        return result
    ratios = [b / a for a, b in complete]
    low, high, coverage = median_ci(ratios, confidence)
    boot_low, boot_high = bootstrap_geomean_ci(ratios, seed, resamples, confidence)
    middle = statistics.median(ratios)
    result.update(ratio=middle, ci_low=low, ci_high=high, ci_coverage=coverage, change_percent=(middle - 1.0) * 100.0,
                  geomean_ratio=math.exp(statistics.fmean(math.log(value) for value in ratios)),
                  bootstrap_ci_low=boot_low, bootstrap_ci_high=boot_high, outcome=classify(low, high, time_metric, floor))
    medians = result["ratio_of_medians"]
    if unit == "count" and medians is not None and abs(medians / middle - 1.0) > BIMODAL_DISAGREEMENT:
        result["note"] = "bimodal counts: compare medians, not the paired ratio (ratio of medians %.4f, median of ratios %.4f)" % (
            medians, middle)
    return result


def abba_order(pair):
    """`AB` for odd pairs, `BA` for even ones: blocks A,B,B,A cancel a
    position effect (warm caches, frequency) and slow linear drift."""
    return "AB" if pair % 2 else "BA"


def abba_schedule(pairs):
    """[(pair, variant)] in run order for `pairs` pairs."""
    return [(pair, variant) for pair in range(1, pairs + 1) for variant in abba_order(pair).lower()]


def choose_pair_count(per_pair, per_compile, elapsed, target_minutes, other_compiles, pilot=PILOT_PAIRS):
    """(pair count, reason): fixed once after the pilot block from wall time
    alone, rounded up to whole ABBA blocks; never adapted to the results."""
    remaining = target_minutes * 60.0 - elapsed - other_compiles * per_compile
    count = min(MAX_PAIRS, max(MIN_PAIRS, pilot + int(max(0.0, remaining) / per_pair)))
    count += count % 2
    reason = ("--target-minutes %g: %.3f s per pair (median of %d pilot pairs), %.1f min elapsed, profile steps about %d "
              "compile-equivalents (%.1f min) -> %d pairs (clamped to %d..%d, whole ABBA blocks)") % (
        target_minutes, per_pair, pilot, elapsed / 60, other_compiles, other_compiles * per_compile / 60, count, MIN_PAIRS, MAX_PAIRS)
    return count, reason


def overlap_check(name, groups, labels):
    """Sign-test CIs of the median ratio in each of two subsets; flagged when
    both exist and do not overlap."""
    parts = []
    for label, values in zip(labels, groups):
        low, high, _ = median_ci(values)
        parts.append({"label": label, "n": len(values), "median": statistics.median(values) if values else None, "ci_low": low, "ci_high": high})
    first, second = parts
    checked = all(part["ci_low"] is not None for part in parts)
    flag = checked and (first["ci_high"] < second["ci_low"] or second["ci_high"] < first["ci_low"])
    return {"check": name, "checked": checked, "flag": bool(flag), "subsets": parts}


def compare_checks(pairs):
    """Order effect (AB versus BA pairs) and drift (first versus second half,
    plus per-tenth medians) of the per-pair wall ratio."""
    walls = [(pair, pair["metrics_a"]["wall"], pair["metrics_b"]["wall"]) for pair in pairs]
    walls = [(pair, b / a) for pair, a, b in walls if a and b]
    ab = [value for pair, value in walls if pair["order"] == "AB"]
    ba = [value for pair, value in walls if pair["order"] == "BA"]
    order = overlap_check("order_effect", (ab, ba), ("AB (A ran first)", "BA (B ran first)"))
    if ab and ba:
        # AB ratios carry the second-position factor p, BA ratios 1/p.
        order["second_position_factor"] = math.sqrt(statistics.median(ab) / statistics.median(ba))
    half = len(walls) // 2
    drift = overlap_check("drift", ([value for _, value in walls[:half]], [value for _, value in walls[half:]]),
                          ("first half", "second half"))
    drift["tenth_medians_ratio"] = tenth_medians([value for _, value in walls])
    drift["tenth_medians_a_wall"] = tenth_medians([pair["metrics_a"]["wall"] for pair, _ in walls])
    return {"order_effect": order, "drift": drift}


# ---------------------------------------------------------------- A/B compare: execution

def compare_labs(arguments, output, cpu, extra):
    """{variant key: Lab} with each variant's directory DIR/a, DIR/b."""
    labs = {}
    for key, role in VARIANTS:
        ide = os.path.abspath(getattr(arguments, role))
        if not os.path.isfile(ide):
            sys.exit("uarch_lab: no %s binary at %s" % (role, ide))
        lab = Lab(os.path.join(output, key), arguments.perf, cpu, ide, arguments.repo_root, extra, arguments.sudo, arguments.fresh_copy)
        lab.meta["config"] = {"command": shell_join(lab.workload("OUT")), "cpu": cpu, "perf": arguments.perf, "repo_root": lab.repo_root,
                              "ide": ide, "role": role, "fresh_copy": arguments.fresh_copy}
        lab.save_meta()
        labs[key] = lab
    return labs


def prepare_variant(lab, warmups):
    """Warm up after both probes and the shared collection policy are saved;
    the warm-up output becomes the variant's determinism reference. With no
    warm-ups, compile a reference under that policy separately from probes."""
    out = os.path.join(lab.output, "out.exe")
    if warmups == 0:
        status, _, err = lab.run_command(lab.pin() + lab.workload(out, compile_flags(lab, os.path.join(lab.output, "reference.ccmetrics"))),
                                         log=os.path.join(lab.output, "reference-run.log"))
        if status != 0:
            raise RuntimeError("reference compile failed: " + err.strip()[-400:])
    for index in range(warmups):
        status, _, err = lab.run_command(lab.pin() + lab.workload(out, compile_flags(lab, os.path.join(lab.output, "warmup.ccmetrics"))),
                                         log=os.path.join(lab.output, "warmup-%d.log" % index))
        if status != 0:
            raise RuntimeError("warm-up failed: " + err.strip()[-400:])
        print("[compare] %s warm-up %.2f s" % (lab.meta["config"]["role"], lab.last_elapsed), flush=True)
    shutil.copyfile(out, os.path.join(lab.output, "reference.exe"))


def run_pair_member(lab, directory, pair, key):
    base = os.path.join(directory, "pairs", "%04d-%s" % (pair, key))
    out = os.path.join(lab.output, "out.exe")
    with lab.instance():
        status, out_text, err = lab.run_command(timed_command(lab, base + ".csv") + lab.workload(out, compile_flags(lab, base + ".ccmetrics")))
    span = lab.last_elapsed
    if status != 0:
        write_text(base + ".err", out_text[-20000:] + err[-20000:])
    identical = status == 0 and filecmp.cmp(os.path.join(lab.output, "reference.exe"), out, shallow=False)
    return {"pair": pair, "order": abba_order(pair), "variant": key, "exit": status, "identical": identical, "span_s": round(span, 6),
            "maxrss_bytes": lab.last_maxrss, "harness_rss_bytes": lab.last_harness_rss,
            "wrapper_rss_bytes": lab.meta["capabilities"].get("wrapper_rss_bytes"),
            "cpu_s": lab.last_cpu_s, "counters": lab.counters}


def compare_timed(labs, directory, meta, arguments, started, other_compiles):
    """The paired ABBA series; the pair count comes from --pairs or is fixed
    once after the pilot block (choose_pair_count)."""
    os.makedirs(os.path.join(directory, "pairs"), exist_ok=True)
    count = arguments.pairs
    meta["plan"] = {"pairs": count, "reason": "--pairs %d" % count, "order": "ABBA", "fresh_copy": arguments.fresh_copy} if count is not None else None
    records, pair = [], 0
    while count is None or pair < count:
        pair += 1
        members = {}
        for key in abba_order(pair).lower():
            members[key] = run_pair_member(labs[key], directory, pair, key)
            records.append(members[key])
        # Written after every pair so an interrupted series still renders.
        write_json(os.path.join(directory, "pairs.json"), records)
        print("[compare] pair %d/%s %s: A %.4f s%s, B %.4f s%s, B/A %s" % (
            pair, count if count is not None else "?", abba_order(pair), members["a"]["span_s"], "" if members["a"]["exit"] == 0 else " FAILED",
            members["b"]["span_s"], "" if members["b"]["exit"] == 0 else " FAILED",
            fmt(ratio(members["b"]["span_s"], members["a"]["span_s"]), ".4f")), flush=True)
        if count is None and pair == PILOT_PAIRS:
            spans = {key: [record["span_s"] for record in records if record["variant"] == key and record["exit"] == 0] for key in ("a", "b")}
            per_compile = statistics.median(spans["a"] + spans["b"]) if spans["a"] + spans["b"] else 2.2
            per_pair = sum(statistics.median(values) if values else per_compile for values in spans.values())
            count, reason = choose_pair_count(per_pair, per_compile, time.monotonic() - started, arguments.target_minutes, other_compiles)
            meta["plan"] = {"pairs": count, "reason": reason, "order": "ABBA", "fresh_copy": arguments.fresh_copy}
            save_compare_meta(directory, meta)
            print("[compare] %s" % reason, flush=True)
    return "%d pairs (%s)" % (count, meta["plan"]["reason"])


def compare_profile_cost(groups, steps):
    per_variant = {"topdown": TOPDOWN_COMPILES_PER_GROUP * (len(groups) if groups is not None else 8),
                   "sampling": len(SAMPLE_EVENTS) + 1, "ibs": IBS_COMPILES, "threads": 1}
    return 2 * sum(per_variant[step] for step in steps)


def save_compare_meta(directory, meta):
    write_json(os.path.join(directory, "compare.json"), meta)


def load_compare_meta(directory):
    return json.loads(read_text(os.path.join(directory, "compare.json")) or "{}")


def command_compare(arguments):
    if arguments.cpu is not None and arguments.cpu >= 0 and not shutil.which("taskset"):
        sys.exit("uarch_lab: taskset not found (pass --cpu -1 to run unpinned)")
    cpu = arguments.cpu if arguments.cpu is not None and arguments.cpu >= 0 else None
    extra = arguments.extra[1:] if arguments.extra[:1] == ["--"] else arguments.extra
    steps = [step for step in (arguments.profile_steps or "").replace(" ", "").split(",") if step]
    if arguments.sudo and "ibs" not in steps:
        steps.append("ibs")
    unknown = [step for step in steps if step not in PROFILE_STEPS]
    if unknown:
        sys.exit("uarch_lab: unknown --profile-steps %s (choose from %s)" % (",".join(unknown), ",".join(PROFILE_STEPS)))
    if arguments.pairs is not None and arguments.pairs < 1:
        sys.exit("uarch_lab: --pairs must be at least 1")
    if not 0.0 <= arguments.min_effect < 100.0:
        sys.exit("uarch_lab: --min-effect must be a percentage in [0, 100)")
    if "ibs" in steps and not arguments.sudo:
        sys.exit("uarch_lab: the ibs profile step needs --sudo")
    directory = os.path.abspath(arguments.output)
    os.makedirs(directory, exist_ok=True)
    # A reused directory must not render an earlier series as this one.
    shutil.rmtree(os.path.join(directory, "pairs"), ignore_errors=True)
    for stale in ("pairs.json", "summary.json", "report.md"):
        if os.path.exists(os.path.join(directory, stale)):
            os.remove(os.path.join(directory, stale))
    labs = compare_labs(arguments, directory, cpu, extra)
    meta = {"version": 1, "mode": "compare", "steps": {}, "plan": None,
            "config": {"command": shell_join(["IDE"] + DEFAULT_COMPILE + extra + ["-o", "OUT"]), "repo_root": os.path.abspath(arguments.repo_root),
                       "cpu": cpu, "perf": arguments.perf, "pairs": arguments.pairs, "target_minutes": arguments.target_minutes,
                       "warmups": arguments.warmups, "seed": arguments.seed, "profile_steps": steps, "sudo": arguments.sudo,
                       "require_identical_output": arguments.require_identical_output, "extra": extra,
                       "fresh_copy": arguments.fresh_copy, "min_effect_percent": arguments.min_effect},
            "variants": {key: {"role": role, "ide": labs[key].ide, "sha256": sha256_file(labs[key].ide),
                               "size_bytes": os.path.getsize(labs[key].ide)} for key, role in VARIANTS}}
    save_compare_meta(directory, meta)
    print("uarch_lab: compare output %s; %s" % (directory, "%d pairs (--pairs)" % arguments.pairs if arguments.pairs is not None else
                                                "pair count chosen after %d pilot pairs to land near %g min" % (PILOT_PAIRS, arguments.target_minutes)), flush=True)
    started = time.monotonic()

    def stage(name, action):
        step_started = time.monotonic()
        try:
            state = {"status": "ok", "note": action() or ""}
        except Exception as error:  # recorded, rendered; the caller decides whether to go on
            state = {"status": "failed", "note": "%s: %s" % (type(error).__name__, error)}
        state["elapsed_s"] = round(time.monotonic() - step_started, 1)
        meta["steps"][name] = state
        save_compare_meta(directory, meta)
        print("uarch_lab: %s %s in %.1f s %s" % (name, state["status"], state["elapsed_s"], state["note"]), flush=True)
        return state["status"] == "ok"

    stage("env", lambda: step_env(labs["a"]))
    groups = json.loads(read_text(os.path.join(labs["a"].output, "env", "env.json")) or "{}").get("metric_groups")
    if steps and not probe_counters(labs["a"]):
        sys.exit("uarch_lab: --profile-steps %s need a working perf (%s); rerun without them for a wall, CPU-time and "
                 "peak-RSS comparison" % (",".join(steps), labs["a"].meta["capabilities"]["perf_stat"]["reason"]))

    def prepare():
        for key in ("a", "b"):
            probe_workload(labs[key], labs[key].output, os.path.join(labs[key].output, "out.exe"))
        unsupported = [role for key, role in VARIANTS if not labs[key].meta["capabilities"].get("metrics_out")]
        enabled = not unsupported
        meta["phase_metrics"] = {"enabled": enabled, "reason": "both compilers support -fmetrics-out" if enabled else
                                 "%s %s not support -fmetrics-out" % (" and ".join(unsupported), "do" if len(unsupported) > 1 else "does")}
        for key in ("a", "b"):
            labs[key].meta["collection"] = {"metrics_out": enabled}
            labs[key].save_meta()
        save_compare_meta(directory, meta)
        for key in ("a", "b"):
            prepare_variant(labs[key], arguments.warmups)
        identical = filecmp.cmp(os.path.join(labs["a"].output, "reference.exe"), os.path.join(labs["b"].output, "reference.exe"), shallow=False)
        meta["outputs_identical"] = identical
        return "baseline and candidate outputs %s" % ("identical" if identical else "differ")
    proceed = stage("prepare", prepare)
    if proceed and arguments.require_identical_output and not meta.get("outputs_identical"):
        meta["steps"]["timed"] = {"status": "skipped", "elapsed_s": 0.0, "note": "outputs differ (--require-identical-output)"}
        proceed = False
    if proceed:
        stage("timed", lambda: compare_timed(labs, directory, meta, arguments, started, compare_profile_cost(groups, steps)))
        for step in steps:
            def profile(step=step):
                notes = []
                for key, role in VARIANTS:
                    lab = labs[key]
                    if step == "topdown":
                        note = step_topdown(lab, groups or [])
                    elif step == "sampling":
                        note = step_sampling(lab, detail=False)
                    elif step == "threads":
                        note = step_threads(lab)
                    else:
                        note = step_ibs(lab)
                    lab.meta["steps"][step] = {"status": "ok", "note": note or ""}
                    lab.save_meta()
                    notes.append("%s: %s" % (role, note))
                return "; ".join(notes)
            stage(step, profile)
    meta["total_s"] = round(time.monotonic() - started, 1)
    save_compare_meta(directory, meta)
    render_compare(directory)
    summary = json.loads(read_text(os.path.join(directory, "summary.json")) or "{}")
    print("uarch_lab: total %.1f s; %s\nuarch_lab: report %s, summary %s" % (
        meta["total_s"], summary.get("verdict", {}).get("text", "no verdict"), os.path.join(directory, "report.md"),
        os.path.join(directory, "summary.json")))
    if not proceed:
        sys.exit("uarch_lab: compare stopped before the timed series (see report.md)")
    if not summary.get("plan", {}).get("complete_pairs"):
        # A series without one complete pair is no measurement; never exit 0.
        sys.exit("uarch_lab: no complete pair; every timed run failed (see %s)" % os.path.join(directory, "pairs"))


# ---------------------------------------------------------------- A/B compare: summary and report

def load_pairs(directory):
    records = json.loads(read_text(os.path.join(directory, "pairs.json")) or "[]")
    return [load_run(record, os.path.join(directory, "pairs", "%04d-%s.csv" % (record["pair"], record["variant"])),
                     os.path.join(directory, "pairs", "%04d-%s.ccmetrics" % (record["pair"], record["variant"])),
                     record.get("span_s"), "pairs.json") for record in records]


def complete_pairs(runs):
    """[{pair, order, a, b, metrics_a, metrics_b}] for pairs whose two runs both succeeded."""
    table_ = {}
    for run in runs:
        table_.setdefault(run["pair"], {})[run["variant"]] = run
    pairs = []
    for pair in sorted(table_):
        members = table_[pair]
        if all(key in members and members[key]["exit"] == 0 for key in ("a", "b")):
            pairs.append({"pair": pair, "order": members["a"]["order"], "a": members["a"], "b": members["b"],
                          "metrics_a": run_metrics(members["a"]), "metrics_b": run_metrics(members["b"])})
    return pairs


def compare_verdict(metrics, phases, min_effect=DEFAULT_MIN_EFFECT):
    """The headline: wall time decides against the practical floor
    (min_effect percent); counters and phases only explain.  bound_percent is
    the largest change the CI admits when no effect beyond the floor was
    established (the effect size the run could have missed)."""
    wall = metrics["wall"]
    outcome = wall["outcome"]
    result = {"metric": "wall", "outcome": outcome, "ratio": wall["ratio"], "ci_low": wall["ci_low"], "ci_high": wall["ci_high"],
              "ci_coverage": wall["ci_coverage"], "change_percent": wall["change_percent"], "bound_percent": None,
              "min_effect_percent": min_effect, "n": wall["n"]}
    if outcome in ("no detectable difference", "below-floor"):
        result["bound_percent"] = max(abs(1.0 - wall["ci_low"]), abs(wall["ci_high"] - 1.0)) * 100.0
    if outcome in ("faster", "slower"):
        text = "Candidate is %s: wall time B/A %.4f (%+.2f%%), 95%% CI [%.4f, %.4f] over %d pairs, beyond the %g%% practical floor." % (
            outcome.upper(), wall["ratio"], wall["change_percent"], wall["ci_low"], wall["ci_high"], wall["n"], min_effect)
    elif outcome == "below-floor":
        text = ("BELOW THE PRACTICAL FLOOR: wall time B/A %.4f (%+.2f%%) is below the %g%% practical floor (95%% CI [%.4f, %.4f] "
                "over %d pairs excludes 1.0 but reaches inside the floor; measurement-instance effects of ~0.5%% were seen in A/A "
                "on Zen 5, LAB3); any real change is within about +/-%.2f%%. Not evidence of a code effect: run an A/A comparison "
                "on this host before trusting a difference this small.") % (
            wall["ratio"], wall["change_percent"], min_effect, wall["ci_low"], wall["ci_high"], wall["n"], result["bound_percent"])
    elif outcome == "no detectable difference":
        text = ("NO DETECTABLE DIFFERENCE: wall time B/A %.4f, 95%% CI [%.4f, %.4f] includes 1.0 over %d pairs; any real change "
                "is within about +/-%.2f%%%s.") % (wall["ratio"], wall["ci_low"], wall["ci_high"], wall["n"], result["bound_percent"],
                                                   " (wider than the %g%% practical floor: more pairs are needed to rule out an "
                                                   "effect of that size)" % min_effect if result["bound_percent"] > min_effect else "")
    elif outcome == "inconclusive":
        text = "INCONCLUSIVE: %d complete pairs is too few for a 95%% distribution-free CI (at least 6 are needed)." % wall["n"]
    else:
        text = "NO VERDICT: no complete pair with a wall time (%s)." % outcome
    explain = []
    for key, label in (("instructions", "instructions"), ("cycles", "cycles"), ("ipc", "IPC"), ("branch_mpki", "branch MPKI"),
                       ("page_faults", "page faults")):
        if metrics[key]["outcome"] in ("lower", "higher", "below-floor"):
            explain.append("%s %+.2f%%%s" % (label, metrics[key]["change_percent"], " (below floor)" if metrics[key]["outcome"] == "below-floor" else ""))
    moves = sorted(((row["delta"], phase, row["outcome"]) for phase, row in (phases or {}).items()
                    if phase != "total" and row["outcome"] in ("faster", "slower", "below-floor")), key=lambda item: -abs(item[0]))[:3]
    if moves:
        explain.append("phases " + ", ".join("%s %+.1f ms%s" % (phase, delta, " (below floor)" if move == "below-floor" else "")
                                             for delta, phase, move in moves))
    result["explanation"] = "; ".join(explain) or "no counter or phase moved detectably"
    result["text"] = text + " Detectable moves (CI excludes 1; they explain, they do not decide): %s." % result["explanation"]
    return result


def compare_warnings(variants, outputs_identical, metrics, checks, meta):
    warnings = []
    for role, info in variants.items():
        if info["failed"]:
            warnings.append("%s: %d of %d timed runs failed (their pairs are dropped)" % (role, info["failed"], info["runs"]))
        if info["runs"] and not info["deterministic"]:
            warnings.append("%s: nondeterministic output (%d of %d successful runs byte-identical to its warm-up output)" % (
                role, info["identical_runs"], info["runs"] - info["failed"]))
        if info.get("metrics_out_enabled") is not False and not info["metrics_out"]:
            warnings.append("%s: no measured -fmetrics-out record, so no phase comparison" % role)
    phase_metrics = meta.get("phase_metrics", {})
    if phase_metrics.get("enabled") is False:
        warnings.append("phase metrics collection disabled for both variants: " + phase_metrics["reason"])
    if outputs_identical is False:
        warnings.append("baseline and candidate outputs differ (expected for a code-generation change; a pure refactor should be identical)")
    config = meta.get("config", {})
    floor = config.get("min_effect_percent", DEFAULT_MIN_EFFECT) / 100.0
    if not config.get("fresh_copy"):
        warnings.append("binaries ran in place, not as a fresh copy per run (--no-fresh-copy or an older run): a fixed per-instance "
                        "offset (0.5% in the LAB3 A/A run) is not covered by the CI")
    wall, task = metrics["wall"], metrics["task_clock"]
    if wall["outcome"] != "inconclusive" and wall["bootstrap_ci_low"] is not None and \
            classify(wall["bootstrap_ci_low"], wall["bootstrap_ci_high"], True, floor) != wall["outcome"]:
        warnings.append("wall: the bootstrap CI of the geometric mean (%s) disagrees with the sign-test verdict (%s); the effect is at the "
                        "edge of detectability" % (classify(wall["bootstrap_ci_low"], wall["bootstrap_ci_high"], True, floor), wall["outcome"]))
    instructions = metrics["instructions"]["ratio"]
    shifted = [key for key in ("wall", "cycles") if metrics[key]["ci_low"] is not None and
               (metrics[key]["ci_low"] > 1.0 or metrics[key]["ci_high"] < 1.0)]
    if instructions is not None and abs(instructions - 1.0) <= IDENTICAL_WORK and shifted:
        warnings.append("identical work, different time: instructions B/A %.6f but the %s CI excludes 1.0; likely a placement/instance "
                        "effect, not a code effect (a layout-only change can do this too; confirm with an A/A run)" % (
                            instructions, " and ".join(shifted)))
    if {wall["outcome"], task["outcome"]} == {"faster", "slower"} or (
            wall["outcome"] in ("faster", "slower") and task["outcome"] == "no detectable difference"):
        warnings.append("wall time (%s) and task-clock (%s) disagree: check for off-CPU time (I/O, page cache, scheduling)" % (
            wall["outcome"], task["outcome"]))
    if wall["outcome"] in ("no detectable difference", "below-floor"):
        for key in ("instructions", "cycles"):
            change = metrics[key]["change_percent"]
            if metrics[key]["outcome"] in ("lower", "higher") and change is not None and abs(change) >= 0.1:
                warnings.append("%s changed %+.2f%% but wall time shows %s: a proxy is not a win" % (
                    key, change, "no detectable difference" if wall["outcome"] == "no detectable difference" else "only a below-floor change"))
    for check in checks.values():
        if check["flag"]:
            low, high = check["subsets"]
            warnings.append("%s: %s median B/A %.4f [%.4f, %.4f] and %s %.4f [%.4f, %.4f] do not overlap" % (
                check["check"].replace("_", " "), low["label"], low["median"], low["ci_low"], low["ci_high"],
                high["label"], high["median"], high["ci_low"], high["ci_high"]))
    if wall["n"] % 2:
        warnings.append("odd number of complete pairs (%d): AB and BA pairs are unbalanced, so a position effect does not fully cancel" % wall["n"])
    if config.get("cpu") is None:
        warnings.append("runs were not pinned to a CPU (--cpu -1)")
    for name, state in meta.get("steps", {}).items():
        if state.get("status") not in ("ok", None):
            warnings.append("step %s %s: %s" % (name, state["status"], state.get("note", "")))
    return warnings


def compare_profile(directory, meta):
    profile = {}
    steps = meta.get("steps", {})
    roots = {key: os.path.join(directory, key) for key, _ in VARIANTS}
    if "topdown" in steps:
        a_rows = {(row["group"], row["metric"]): row for row in topdown_values(roots["a"])}
        rows = []
        for row in topdown_values(roots["b"]):
            old = a_rows.get((row["group"], row["metric"]))
            if old is None:
                continue
            delta = row["value"] - old["value"] if row["value"] is not None and old["value"] is not None else None
            rows.append({"group": row["group"], "metric": row["metric"], "unit": row["unit"], "a": old["value"], "b": row["value"],
                         "delta": delta, "relative_change_percent": ratio(delta, old["value"], 100.0) if delta is not None else None,
                         "a_source": old["source"], "b_source": row["source"]})
        rows.sort(key=lambda row: (-abs(row["relative_change_percent"] or 0.0), row["group"], row["metric"]))
        profile["topdown"] = {"status": steps["topdown"]["status"], "metrics": rows}
    for step, names, sub, suffix in (("sampling", list(SAMPLING_LABELS) + ["faults"], "sampling", ".self.txt"),
                                     ("ibs", ["ibs_op", "ibs_fetch"], "ibs", ".workload.txt")):
        if step not in steps:
            continue
        events = {}
        for name in names:
            a_text = read_text(os.path.join(roots["a"], sub, name + suffix))
            b_text = read_text(os.path.join(roots["b"], sub, name + suffix))
            if a_text is not None and b_text is not None:
                events[name] = symbol_movers(a_text, b_text)
        profile[step] = {"status": steps[step]["status"], "events": events}
    if "threads" in steps:
        captures = {}
        for key, role in VARIANTS:
            record = json.loads(read_text(os.path.join(roots[key], "threads", "threads.json")) or "{}")
            captures[role] = {"mode": record.get("mode"), "data": record.get("data"), "refused": record.get("refused", [])}
        profile["threads"] = {"status": steps["threads"]["status"], "captures": captures}
    return profile


def metric_floor(key, min_effect):
    """The practical floor of one COMPARE_METRICS key as a fraction:
    instructions:u is deterministic, so any change in it is exact (no floor)."""
    return 0.0 if key == "instructions" else min_effect / 100.0


def compare_summary(directory):
    """summary.json of a `compare` directory (COMPARE_SCHEMA), from the raw files."""
    meta = load_compare_meta(directory)
    config = meta.get("config", {})
    seed = config.get("seed", DEFAULT_SEED)
    # An older directory without the setting is rendered with today's default floor.
    min_effect = config.get("min_effect_percent", DEFAULT_MIN_EFFECT)
    runs = load_pairs(directory)
    pairs = complete_pairs(runs)
    variants = {}
    for key, role in VARIANTS:
        own = [run for run in runs if run["variant"] == key]
        good = [run for run in own if run["exit"] == 0]
        own_meta = load_meta(os.path.join(directory, key))
        capabilities = own_meta.get("capabilities", {})
        info = meta.get("variants", {}).get(key, {})
        variants[role] = {"path": info.get("ide"), "sha256": info.get("sha256"), "size_bytes": info.get("size_bytes"),
                          "runs": len(own), "failed": len(own) - len(good), "identical_runs": sum(bool(run["identical"]) for run in good),
                          "deterministic": bool(good) and all(run["identical"] for run in good),
                          "metrics_out": bool(capabilities.get("metrics_out_measured")),
                          "metrics_out_supported": capabilities.get("metrics_out"),
                          "metrics_out_enabled": own_meta.get("collection", {}).get("metrics_out"),
                          "source_metrics": bool(capabilities.get("source_metrics"))}
    references = [os.path.join(directory, key, "reference.exe") for key, _ in VARIANTS]
    outputs_identical = filecmp.cmp(*references, shallow=False) if all(map(os.path.isfile, references)) else meta.get("outputs_identical")
    metrics = {key: dict(compare_series([(pair["metrics_a"][key], pair["metrics_b"][key]) for pair in pairs], unit, direction, seed,
                                        time_metric=unit == "s", floor=metric_floor(key, min_effect)), label=label)
               for key, unit, direction, label in COMPARE_METRICS}
    phase_records = [(measured_input(pair["a"]["metrics"]), measured_input(pair["b"]["metrics"])) for pair in pairs]
    phase_records = [(a, b) for a, b in phase_records if a and b]
    phases = None
    if phase_records:
        phases = {}
        for phase in PHASES + ("total",):
            a_series, b_series = (phase_ns_series([pair[side] for pair in phase_records], phase) for side in (0, 1))
            pairs_ms = [(a / 1e6, b / 1e6) for a, b in zip(a_series, b_series)] if a_series and b_series else [(None, None)] * len(phase_records)
            phases[phase] = compare_series(pairs_ms, "ms", "lower", seed, time_metric=True, floor=min_effect / 100.0)
    checks = compare_checks(pairs)
    verdict = compare_verdict(metrics, phases, min_effect)
    code = code_bytes_summary(references)
    counters = compare_counters(directory)
    return {"schema": COMPARE_SCHEMA, "directory": directory, "command": config.get("command"), "repo_root": config.get("repo_root"),
            "cpu": config.get("cpu"), "host": host_facts(os.path.join(directory, "a")),
            "baseline": variants["baseline"], "candidate": variants["candidate"], "outputs_identical": outputs_identical,
            "phase_metrics": meta.get("phase_metrics", {"enabled": None, "reason": "collection policy not recorded (older comparison)"}),
            "counters": counters,
            "code_bytes": code,
            "plan": dict(meta.get("plan") or {}, seed=seed, confidence=CONFIDENCE, bootstrap_resamples=BOOTSTRAP_RESAMPLES,
                         complete_pairs=len(pairs), fresh_copy=bool(config.get("fresh_copy"))),
            "method": COMPARE_METHOD, "verdict": verdict, "metrics": metrics, "phases": phases, "checks": checks,
            "profile": compare_profile(directory, meta),
            "steps": {name: state.get("status") for name, state in meta.get("steps", {}).items()},
            "warnings": compare_warnings(variants, outputs_identical, metrics, checks, meta) + rss_warnings(runs) +
                        ([] if counters["perf_stat"] is not False else
                         ["%s: timed without perf; wall time, wait4 CPU time and peak RSS are measured, every "
                          "counter is NA" % counters["reason"]])}


def compare_counters(directory):
    """Whether both variants were timed under `perf stat` (probe_counters):
    perf_stat True, False (counters NA) or None (not recorded: older output)."""
    states = [load_meta(os.path.join(directory, key)).get("capabilities", {}).get("perf_stat") for key, _ in VARIANTS]
    usable = None
    reason = "not recorded (older comparison)"
    if all(isinstance(state, dict) for state in states):
        usable = all(state.get("usable") is True for state in states)
        reason = "; ".join(sorted({str(state.get("reason")) for state in states}))
    return {"perf_stat": usable, "reason": reason}


def code_bytes_summary(references):
    """Generated code bytes of A's and B's reference outputs (code_sections):
    exact, so ratio is b_value / a_value with no interval; None when either
    is NA or the baseline payload is zero (no denominator, #511)."""
    a, b = (code_sections(path) if os.path.isfile(path) else None for path in references)
    a_value, b_value = a and a["code_bytes"], b and b["code_bytes"]
    note = None
    if a is None or b is None:
        note = "no reference output for %s" % " and ".join(role for (_, role), info in zip(VARIANTS, (a, b)) if info is None)
    elif a_value is None or b_value is None:
        note = "; ".join("%s: %s %s" % (role, info["format"], info["reason"]) for (_, role), info in zip(VARIANTS, (a, b)) if info["reason"])
    elif a_value == 0:
        note = "zero baseline code payload: no ratio denominator"
    return {"a_value": a_value, "b_value": b_value, "ratio": b_value / a_value if a_value and b_value is not None else None,
            "a_format": a and a["format"], "b_format": b and b["format"], "a_file_bytes": a and a["file_bytes"],
            "b_file_bytes": b and b["file_bytes"], "a_sections": a and a["sections"], "b_sections": b and b["sections"], "note": note}


def rss_warnings(runs):
    """A warning per variant whose successful runs have a peak RSS NA (rss_value)."""
    warnings = []
    for key, role in VARIANTS:
        good = [run for run in runs if run["variant"] == key and run["exit"] == 0]
        missing = [run for run in good if rss_value(run) is None]
        if missing:
            warnings.append("%s: peak RSS NA in %d of %d runs (not above %gx the harness/perf floor of %s bytes, or not measured)" % (
                role, len(missing), len(good), RSS_WRAPPER_MARGIN, fmt(rss_floor(missing[0]))))
    return warnings


def value_text(value, unit):
    if value is None:
        return "NA"
    if unit == "s":
        return "%.4f" % value
    if unit == "ms":
        return "%.2f" % value
    if unit in ("count", "bytes"):
        return fmt(value)
    return "%.4f" % value


def ratio_cells(row):
    return ["NA" if row["ratio"] is None else "%.4f" % row["ratio"],
            "NA" if row["ci_low"] is None else "[%.4f, %.4f]" % (row["ci_low"], row["ci_high"]),
            "NA" if row["change_percent"] is None else "%+.2f%%" % row["change_percent"]]


def compare_markdown(summary):
    verdict, plan = summary["verdict"], summary["plan"]
    lines = ["# A/B compile-time comparison", "", "**%s**" % verdict["text"], ""]
    for role in ("baseline", "candidate"):
        info = summary[role]
        lines.append("- %s (%s): `%s` sha256 `%s`; %d runs, %d failed, %s; -fmetrics-out support %s, enabled collection %s" % (
            role, "A" if role == "baseline" else "B", info["path"], info["sha256"], info["runs"], info["failed"],
            "deterministic output" if info["deterministic"] else "**nondeterministic output**",
            {True: "yes", False: "no", None: "unknown"}[info.get("metrics_out_supported")],
            {True: "yes", False: "no", None: "unknown"}[info.get("metrics_out_enabled")]))
    phase_metrics = summary["phase_metrics"]
    lines.append("- phase metrics collection: %s; %s" % (
        {True: "enabled", False: "disabled", None: "unknown"}[phase_metrics["enabled"]], phase_metrics["reason"]))
    code = summary.get("code_bytes") or {}
    lines += ["- outputs of A and B: %s" % {True: "byte-identical", False: "differ", None: "NA"}[summary["outputs_identical"]],
              "- generated code bytes (executable sections, exact): A %s, B %s, B/A %s%s" % (
                  fmt(code.get("a_value")), fmt(code.get("b_value")), fmt(code.get("ratio"), ".6f"),
                  "; " + code["note"] if code.get("note") else ""),
              "- workload `%s` in `%s`, %s; %s complete pairs in ABBA order (%s)" % (
                  summary["command"], summary["repo_root"], "unpinned" if summary["cpu"] is None else "pinned to CPU %s" % summary["cpu"],
                  plan.get("complete_pairs"), plan.get("reason", "NA")),
              "- binary instances: %s; practical floor %g%% (--min-effect)" % (
                  "a fresh copy per timed run and capture" if plan.get("fresh_copy") else "**run in place** (no fresh copy per run)",
                  verdict.get("min_effect_percent", DEFAULT_MIN_EFFECT)),
              "- machine-readable: `summary.json` (schema `%s`); `python3 tools/uarch_lab.py report %s` re-renders both files" % (
                  summary["schema"], summary["directory"]), ""]
    lines += ["## Warnings", ""] + (["- " + warning for warning in summary["warnings"]] or ["- none"])
    lines += ["", "## Metrics (B/A per pair)", "",
              "`B/A` is the median of the per-pair ratios; its 95%% CI uses sign-test order statistics (distribution-free; "
              "coverage %s). The geometric mean has a seeded bootstrap CI (seed %s, %d resamples). The verdict uses wall time only." % (
                  "NA" if summary["metrics"]["wall"]["ci_coverage"] is None else "%.4f" % summary["metrics"]["wall"]["ci_coverage"],
                  plan["seed"], plan["bootstrap_resamples"]),
              "Outcomes need the whole CI beyond the %g%% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside "
              "it. instructions:u is exact (no floor)." % verdict.get("min_effect_percent", DEFAULT_MIN_EFFECT), ""]
    rows = []
    for key, row in summary["metrics"].items():
        rows.append([row["label"], row["unit"], value_text(row["a_median"], row["unit"]), value_text(row["b_median"], row["unit"])]
                    + ratio_cells(row) + ["NA" if row["geomean_ratio"] is None else "%.4f" % row["geomean_ratio"],
                                          "NA" if row["bootstrap_ci_low"] is None else "[%.4f, %.4f]" % (row["bootstrap_ci_low"], row["bootstrap_ci_high"]),
                                          "NA" if row["min_ratio"] is None else "%.4f" % row["min_ratio"], row["outcome"]])
    lines += table(["metric", "unit", "A median", "B median", "B/A", "95% CI", "change", "geomean B/A", "bootstrap 95% CI",
                    "min B / min A", "outcome"], rows)
    notes = ["- %s: %s" % (row["label"], row["note"]) for row in summary["metrics"].values() if row.get("note")]
    if notes:
        lines += [""] + notes
    lines += ["", "## Phases (-fmetrics-out, median ms)", ""]
    if summary["phases"]:
        lines += table(["phase", "A ms", "B ms", "delta ms", "B/A", "95% CI", "change", "outcome"],
                       [[phase, value_text(row["a_median"], "ms"), value_text(row["b_median"], "ms"),
                         "NA" if row["delta"] is None else "%+.2f" % row["delta"]] + ratio_cells(row) + [row["outcome"]]
                        for phase, row in summary["phases"].items()])
    else:
        lines.append("NA -- phase metrics collection disabled for both variants: %s." % phase_metrics["reason"] if phase_metrics["enabled"] is False else
                     "NA -- a variant wrote no measured `-fmetrics-out` record.")
    lines += ["", "## Checks", ""]
    for check in summary["checks"].values():
        parts = ", ".join("%s n=%d median %s CI %s" % (part["label"], part["n"], fmt(part["median"], ".4f"),
                                                        "NA" if part["ci_low"] is None else "[%.4f, %.4f]" % (part["ci_low"], part["ci_high"]))
                          for part in check["subsets"])
        lines.append("- %s: %s; %s" % (check["check"].replace("_", " "), "**FLAGGED**" if check["flag"] else "ok" if check["checked"] else
                                       "not checked (too few pairs per subset)", parts))
    order, drift = summary["checks"]["order_effect"], summary["checks"]["drift"]
    if order.get("second_position_factor"):
        lines.append("- second run of a pair vs first: x%.4f (ABBA cancels it in the overall ratio)" % order["second_position_factor"])
    lines.append("- per-tenth median B/A: %s; per-tenth median A wall: %s" % (
        " ".join("%.4f" % value for value in drift["tenth_medians_ratio"]) or "NA",
        " ".join("%.4f" % value for value in drift["tenth_medians_a_wall"]) or "NA"))
    profile = summary["profile"]
    if "topdown" in profile:
        lines += ["", "## Why: top-down deltas (status %s)" % profile["topdown"]["status"], "",
                  "Each value as the run mode reports it: measured alone when its group multiplexed, else the group value.", ""]
        lines += table(["group", "metric", "unit", "A", "B", "delta", "change", "sources"],
                       [[row["group"], row["metric"], row["unit"], significant(row["a"]), significant(row["b"]), significant(row["delta"]),
                         "NA" if row["relative_change_percent"] is None else "%+.2f%%" % row["relative_change_percent"],
                         "%s/%s" % (row["a_source"], row["b_source"])] for row in profile["topdown"]["metrics"][:TOPDOWN_MOVERS]])
    for step in ("sampling", "ibs"):
        if step not in profile:
            continue
        lines += ["", "## Why: symbol share movers, %s (status %s)" % (step, profile[step]["status"]), "",
                  "Movers from a single capture per variant are hints only, never evidence of a code effect. Shares are of each "
                  "variant's own capture, so one symbol's change shifts the others; the estimate scales a share by the capture's "
                  "event count. NA: below the report's percent limit. The binomial bound is the 95%% sampling noise of the share "
                  "difference and underestimates (LAB3 A/A movers reached about 5x it), so a row reads `exceeds bound` only past "
                  "%gx the bound; a row with one share NA has no bound (`-`). A capture with fewer than %s samples on either side "
                  "is not diffed. Trust only moves that agree with the phase table." % (MOVER_BOUND_FACTOR, fmt(MIN_MOVER_SAMPLES)), ""]
        for name, movers in profile[step]["events"].items():
            moved = [row for row in movers["movers"] if row["delta_share"]]
            lines += ["", "**%s** (samples A %s, B %s; event count A %s, B %s)" % (
                name, fmt(movers["a_samples"]), fmt(movers["b_samples"]), fmt(movers["a_event_count"]), fmt(movers["b_event_count"])), ""]
            if movers.get("reliable") is False:
                lines.append("%s; movers not listed." % movers["note"])
                continue
            if not moved:
                lines.append("No symbol share moved.")
                continue
            lines += table(["symbol", "A share", "B share", "delta share", "95% binomial bound", "reading", "delta estimate"],
                           [["`%s`" % row["symbol"], "NA" if row["a_share"] is None else "%.2f%%" % row["a_share"],
                             "NA" if row["b_share"] is None else "%.2f%%" % row["b_share"], "%+.2f pp" % row["delta_share"],
                             "-" if row["noise_pp"] is None else "+/-%.2f pp" % row["noise_pp"],
                             mover_reading(row), fmt(row["delta_estimate"], "+,.0f")] for row in moved])
    lines += ["", "## Method", ""] + ["- %s: %s" % item for item in sorted(summary["method"].items())]
    lines += ["", "Raw files: `compare.json` (config, plan, step states), `pairs.json` and `pairs/NNNN-{a,b}.csv|.ccmetrics` "
              "(one per run), `a/` and `b/` (probes, warm-up, reference output, profile steps)."]
    return "\n".join(lines) + "\n"


def mover_reading(row):
    """`exceeds bound` (past MOVER_BOUND_FACTOR x the binomial bound), `within
    Nx bound`, or `-` for a row without a bound (one share NA)."""
    if row["exceeds_bound"] is None:
        return "-"
    return "exceeds bound" if row["exceeds_bound"] else "within %gx bound" % MOVER_BOUND_FACTOR


def render_compare(directory):
    summary = compare_summary(directory)
    write_json(os.path.join(directory, "summary.json"), summary)
    text = compare_markdown(summary)
    write_text(os.path.join(directory, "report.md"), text)
    return text


# ---------------------------------------------------------------- native-retirement gate (#512)

def limit_outcome(low, high, limit):
    """#511 verdict of one interval metric against its limit: PASS when the
    upper 95% bound is at most the limit, FAIL when even the lower bound
    exceeds it, INCONCLUSIVE when the interval crosses it or is missing."""
    if low is None or high is None:
        return "INCONCLUSIVE", "no 95% CI (too few complete pairs or no measured values)"
    if high <= limit:
        return "PASS", "upper bound %.4f <= %g" % (high, limit)
    if low > limit:
        return "FAIL", "lower bound %.4f > %g" % (low, limit)
    return "INCONCLUSIVE", "CI [%.4f, %.4f] crosses %g" % (low, high, limit)


def exact_outcome(value, limit, note=None):
    """#511 verdict of an exact (deterministic) ratio against its limit."""
    if value is None:
        return "INCONCLUSIVE", note or "no exact ratio"
    return ("PASS", "%.6f <= %g" % (value, limit)) if value <= limit else ("FAIL", "%.6f > %g" % (value, limit))


def interval_check(row, limit, label, expected_pairs=None):
    outcome, reason = limit_outcome(row and row.get("ci_low"), row and row.get("ci_high"), limit)
    if not expected_pairs or not row or row.get("n") != expected_pairs:
        outcome, reason = "INCONCLUSIVE", "metric population is incomplete (%s of %s planned pairs)" % (row and row.get("n"), expected_pairs)
    return {"label": label, "kind": "interval", "ratio": row and row.get("ratio"), "ci_low": row and row.get("ci_low"),
            "ci_high": row and row.get("ci_high"), "ci_coverage": row and row.get("ci_coverage"), "n": row and row.get("n"),
            "a_value": row and row.get("a_median"), "b_value": row and row.get("b_median"), "limit": limit,
            "outcome": outcome, "reason": reason}


def correctness_check(label, passed, reason):
    """passed: True, False or None (not established)."""
    outcome = {True: "PASS", False: "FAIL", None: "INCONCLUSIVE"}[passed]
    return {"label": label, "kind": "correctness", "ratio": None, "ci_low": None, "ci_high": None, "ci_coverage": None, "n": None,
            "a_value": None, "b_value": None, "limit": None, "outcome": outcome, "reason": reason}


def variant_checks(summary):
    """Correctness of one cell's two compilers: every timed run succeeded and
    every output is byte-identical to the variant's warm-up output."""
    roles = [summary.get(role) or {} for role, _ in RETIREMENT_ROLES]
    ran = all(info.get("runs") for info in roles)
    failed = [label for (_, label), info in zip(RETIREMENT_ROLES, roles) if info.get("failed")]
    unstable = [label for (_, label), info in zip(RETIREMENT_ROLES, roles) if info.get("runs") and not info.get("deterministic")]
    plan = summary.get("plan") or {}
    expected = plan.get("pairs")
    complete = (summary.get("steps") or {}).get("timed") == "ok" and bool(expected) and plan.get("complete_pairs") == expected and all(
        info.get("runs") == expected for info in roles) and plan.get("fresh_copy") is True
    flags = [name for name, check in (summary.get("checks") or {}).items() if check.get("flag")]
    return {"measurement_complete": correctness_check("planned timed series completed with fresh copies", True if complete else None,
                                                      "all planned pairs retained" if complete else "timed stage, pair counts or fresh-copy evidence incomplete"),
            "measurement_stable": correctness_check("order and drift checks have no flags", None if flags else True,
                                                    "flagged: " + ", ".join(flags) if flags else "no order or drift flags"),
            "runs_succeeded": correctness_check("every timed run succeeded", None if not ran else not failed,
                                                "no timed runs" if not ran else "failed runs: " + ", ".join(failed) if failed else "all runs exited 0"),
            "deterministic": correctness_check("outputs deterministic", None if not ran else not unstable,
                                               "no timed runs" if not ran else "nondeterministic: " + ", ".join(unstable) if unstable
                                               else "every run byte-identical to its warm-up output")}


def cell_checks(kind, summary):
    """{check name: check} for one cell from its compare summary.json."""
    metrics = summary.get("metrics") or {}
    expected_pairs = (summary.get("plan") or {}).get("pairs")
    checks = {}
    if kind == "compiler":
        code = summary.get("code_bytes") or {}
        checks["compiler_wall_time"] = interval_check(metrics.get("wall"), RETIREMENT_LIMITS["compiler_wall_time"], "compiler wall time", expected_pairs)
        checks["peak_rss"] = interval_check(metrics.get("peak_rss"), RETIREMENT_LIMITS["peak_rss"], "compiler peak RSS", expected_pairs)
        outcome, reason = exact_outcome(code.get("ratio"), RETIREMENT_LIMITS["code_bytes"], code.get("note"))
        checks["code_bytes"] = {"label": "generated code bytes", "kind": "exact", "ratio": code.get("ratio"), "ci_low": None, "ci_high": None,
                                "ci_coverage": None, "n": None, "a_value": code.get("a_value"), "b_value": code.get("b_value"),
                                "limit": RETIREMENT_LIMITS["code_bytes"], "outcome": outcome, "reason": reason}
    else:
        checks["generated_runtime"] = interval_check(metrics.get("wall"), RETIREMENT_LIMITS["generated_runtime"], "generated-program runtime", expected_pairs)
        identical = summary.get("outputs_identical")
        checks["generated_compilers_agree"] = correctness_check(
            "generated compilers agree", identical,
            {True: "both stage-1 compilers wrote byte-identical outputs", False: "the stage-1 compilers' outputs differ",
             None: "outputs not compared"}[identical])
    checks.update(variant_checks(summary))
    return checks


def cell_outcome(checks):
    outcomes = {check["outcome"] for check in checks.values()}
    return "FAIL" if "FAIL" in outcomes else "PASS" if outcomes == {"PASS"} else "INCONCLUSIVE"


def retirement_verdict(cells, required):
    """Overall #512 outcome: FAIL when any check fails; PASS only when every
    required cell ran and every one of its checks passed; else INCONCLUSIVE
    (a missing, partial or imprecise cell never passes)."""
    by_name = {cell["cell"]: cell for cell in cells}
    missing = [name for name in required if name not in by_name or by_name[name]["status"] == "not run"]
    failed, open_ = [], []
    for cell in cells:
        for name, check in cell["checks"].items():
            if check["outcome"] == "FAIL":
                failed.append("%s %s (%s)" % (cell["cell"], name, check["reason"]))
            elif check["outcome"] != "PASS":
                open_.append("%s %s (%s)" % (cell["cell"], name, check["reason"]))
        if cell["status"] == "failed":
            open_.append("%s sub-run failed (%s)" % (cell["cell"], cell["note"]))
    if failed:
        outcome = "FAIL"
    elif missing or open_ or not cells:
        outcome = "INCONCLUSIVE"
    else:
        outcome = "PASS"
    text = {"PASS": "PASS: every required cell (%s) is within the #511 limits and its correctness checks hold." % ", ".join(required),
            "FAIL": "FAIL: %s." % "; ".join(failed),
            "INCONCLUSIVE": "INCONCLUSIVE: %s." % "; ".join(
                (["cells not measured: " + ", ".join(missing)] if missing else []) + open_)}[outcome]
    return {"outcome": outcome, "text": text, "failed": failed, "inconclusive": open_, "missing_cells": missing}


def retirement_cells(arguments):
    """[(cell, kind, extra compile args)] in run order."""
    modes = [mode for mode in arguments.modes.replace(" ", "").split(",") if mode]
    cells = [(mode, "compiler", ["-fregister-allocator=" + mode]) for mode in modes]
    return cells + [(RUNTIME_CELL, "generated-runtime", ["-fregister-allocator=" + RUNTIME_SOURCE_MODE])]


def run_cell(directory, name, baseline, candidate, extra, arguments):
    """One `compare` sub-run in DIR/<name>; (status, note).  Its own
    exit (compare stopped early) or an exception is recorded, not raised."""
    argv = ["compare", "--baseline", baseline, "--candidate", candidate, "--repo-root", arguments.repo_root,
            "--cpu", str(arguments.cpu), "--output", os.path.join(directory, name), "--perf", arguments.perf,
            "--seed", str(arguments.seed), "--warmups", str(arguments.warmups)]
    argv += ["--pairs", str(arguments.pairs)] if arguments.pairs is not None else ["--target-minutes", str(arguments.target_minutes_per_cell)]
    argv += (["--profile-steps", arguments.profile_steps] if arguments.profile_steps else []) + (["--sudo"] if arguments.sudo else [])
    print("uarch_lab: retirement cell %s: compare %s" % (name, " ".join(extra)), flush=True)
    try:
        main(argv + ["--"] + extra)
        return "ok", ""
    except SystemExit as error:
        return "failed", str(error.code)
    except Exception as error:  # recorded; the gate then reads the cell as failed
        return "failed", "%s: %s" % (type(error).__name__, error)


def stage1_compilers(directory, config):
    """Copy the stage-1 executables A and B wrote in the source cell (their
    reference outputs) to DIR/generated-runtime/stage1/<role>/ide, executable;
    returns ({role: path}, note) or (None, reason)."""
    source = os.path.join(directory, RUNTIME_SOURCE_MODE)
    summary = json.loads(read_text(os.path.join(source, "summary.json")) or "{}")
    unstable = [role for role, _ in RETIREMENT_ROLES if not (summary.get(role) or {}).get("deterministic")]
    if not summary or unstable:
        return None, "%s cell has no deterministic stage-1 output for %s" % (RUNTIME_SOURCE_MODE, ", ".join(unstable) or "either variant")
    paths = {}
    for (key, _), (role, _) in zip(VARIANTS, RETIREMENT_ROLES):
        target = os.path.join(directory, RUNTIME_CELL, "stage1", role, "ide")
        shutil.rmtree(os.path.dirname(target), ignore_errors=True)
        os.makedirs(os.path.dirname(target))
        fresh_binary_copy(os.path.join(source, key, "reference.exe"), target)
        os.chmod(target, 0o755)
        paths[role] = target
        config["stage1"][role] = {"path": target, "sha256": sha256_file(target), "size_bytes": os.path.getsize(target)}
    return paths, ""


def retirement_legacy_compilers_available(arguments):
    """The frozen v1 gate needs archived binaries with all four old modes.

    Only the distinctive allocator rejection identifies a removed mode. Other
    failures retain their existing recorded sub-run diagnostics.
    """
    with tempfile.TemporaryDirectory(prefix="buster-retirement-allocator-") as temporary:
        source = os.path.join(temporary, "probe.c")
        write_text(source, "int native_retirement_allocator_probe(void) { return 0; }\n")
        for compiler in dict.fromkeys((arguments.baseline, arguments.candidate)):
            for mode in RETIREMENT_MODES[:2]:
                try:
                    observed = subprocess.run([os.path.abspath(compiler), "cc", "-fsyntax-only",
                                               "-fregister-allocator=" + mode, source],
                                              cwd=arguments.repo_root, capture_output=True, timeout=30)
                except (OSError, subprocess.TimeoutExpired):
                    continue
                if b"unsupported register allocator" in observed.stdout + observed.stderr:
                    sys.exit("uarch_lab: frozen native-retirement v1 execution requires archived compilers supporting none and "
                             "mir-stack; the current FAST/QUALITY compiler cannot execute that historical gate. "
                             "Use compare for current FAST/QUALITY measurements or report for archived v1 results.")


def command_retirement(arguments):
    modes = [mode for mode in arguments.modes.replace(" ", "").split(",") if mode]
    unknown = [mode for mode in modes if mode not in RETIREMENT_MODES]
    if unknown or not modes or len(set(modes)) != len(modes):
        sys.exit("uarch_lab: --modes takes distinct values from %s" % ",".join(RETIREMENT_MODES))
    if arguments.pairs is not None and arguments.pairs < 1:
        sys.exit("uarch_lab: --pairs must be at least 1")
    retirement_legacy_compilers_available(arguments)
    directory = os.path.abspath(arguments.output)
    if os.path.isdir(directory) and os.listdir(directory):
        sys.exit("uarch_lab: retirement --output must be a new or empty directory; retain prior attempts separately")
    os.makedirs(directory, exist_ok=True)
    binaries, frozen_binaries = {}, {}
    for role, label in RETIREMENT_ROLES:
        path = os.path.abspath(getattr(arguments, role))
        if not os.path.isfile(path):
            sys.exit("uarch_lab: no %s binary at %s" % (role, path))
        binaries[role] = {"path": path, "sha256": sha256_file(path), "size_bytes": os.path.getsize(path),
                          "revision": getattr(arguments, role + "_rev"), "label": label}
        frozen = os.path.join(directory, "inputs", role, "ide")
        os.makedirs(os.path.dirname(frozen))
        fresh_binary_copy(path, frozen)
        if sha256_file(frozen) != binaries[role]["sha256"]:
            sys.exit("uarch_lab: %s binary changed while freezing the campaign" % role)
        frozen_binaries[role] = frozen
    cells = retirement_cells(arguments)
    per_cell = "%d pairs (--pairs)" % arguments.pairs if arguments.pairs is not None else "about %g min" % arguments.target_minutes_per_cell
    estimate = None if arguments.pairs is not None else len(cells) * arguments.target_minutes_per_cell
    config = {"version": 1, "mode": "retirement", "baseline": binaries["baseline"], "candidate": binaries["candidate"],
              "repo_root": os.path.abspath(arguments.repo_root), "cpu": arguments.cpu if arguments.cpu >= 0 else None,
              "perf": arguments.perf, "modes": modes, "pairs": arguments.pairs, "target_minutes_per_cell": arguments.target_minutes_per_cell,
              "estimated_minutes": estimate, "seed": arguments.seed, "warmups": arguments.warmups, "profile_steps": arguments.profile_steps,
              "sudo": arguments.sudo, "frozen_binaries": frozen_binaries,
              "cells": [{"cell": name, "kind": kind, "extra": extra, "status": "not run", "note": ""}
                                                for name, kind, extra in cells], "stage1": {}}
    write_json(os.path.join(directory, "retirement-config.json"), config)
    print("uarch_lab: retirement gate (#512, %s) in %s\nuarch_lab: plan: %d cells x %s%s:" % (
        RETIREMENT_DECISION, directory, len(cells), per_cell, " = about %g min" % estimate if estimate else ""), flush=True)
    for name, kind, extra in cells:
        print("    %-18s %s" % (name, "stage-1 self-host compile, %s" % " ".join(extra) if kind == "compiler" else
                                "the %s cell's stage-1 compilers (A-built vs B-built) compiling the same source, %s" % (
                                    RUNTIME_SOURCE_MODE, " ".join(extra))), flush=True)
    started = time.monotonic()
    for entry in config["cells"]:
        if entry["kind"] == "compiler":
            entry["status"], entry["note"] = run_cell(directory, entry["cell"], frozen_binaries["baseline"],
                                                      frozen_binaries["candidate"], entry["extra"], arguments)
        elif RUNTIME_SOURCE_MODE not in modes:
            entry["note"] = "needs the %s cell (--modes)" % RUNTIME_SOURCE_MODE
        else:
            paths, note = stage1_compilers(directory, config)
            if paths is None:
                entry["note"] = note
            else:
                entry["status"], entry["note"] = run_cell(directory, entry["cell"], paths["baseline"], paths["candidate"], entry["extra"], arguments)
        write_json(os.path.join(directory, "retirement-config.json"), config)
    config["total_s"] = round(time.monotonic() - started, 1)
    write_json(os.path.join(directory, "retirement-config.json"), config)
    render_retirement(directory)
    summary = json.loads(read_text(os.path.join(directory, "retirement.json")) or "{}")
    print("uarch_lab: retirement total %.1f s\n%s\nuarch_lab: report %s, summary %s" % (
        config["total_s"], summary.get("verdict", {}).get("text", "no verdict"), os.path.join(directory, "retirement.md"),
        os.path.join(directory, "retirement.json")))


def retirement_summary(directory):
    """retirement.json (RETIREMENT_SCHEMA) from retirement-config.json and each cell's summary.json."""
    config = json.loads(read_text(os.path.join(directory, "retirement-config.json")) or "{}")
    cells, warnings, host = [], [], None
    for entry in config.get("cells", []):
        cell_dir = os.path.join(directory, entry["cell"])
        summary = json.loads(read_text(os.path.join(cell_dir, "summary.json")) or "{}") if entry["status"] != "not run" else {}
        checks = cell_checks(entry["kind"], summary) if summary else {}
        if entry["status"] == "not run" or not summary:
            checks["cell_measured"] = correctness_check("cell measured", None, entry.get("note") or "no summary.json")
        host = host or (summary.get("host") if summary else None)
        warnings += ["%s: %s" % (entry["cell"], warning) for warning in summary.get("warnings", [])]
        diagnostics = {key: {"ratio": row.get("ratio"), "ci_low": row.get("ci_low"), "ci_high": row.get("ci_high"), "outcome": row.get("outcome")}
                       for key, row in (summary.get("metrics") or {}).items() if key in ("task_clock", "instructions", "cycles", "peak_rss")}
        cells.append({"cell": entry["cell"], "kind": entry["kind"], "extra_args": entry["extra"], "status": entry["status"],
                      "note": entry.get("note", ""), "directory": cell_dir,
                      "summary": os.path.join(entry["cell"], "summary.json") if summary else None,
                      "summary_schema": summary.get("schema"),
                      "baseline": {key: (summary.get("baseline") or {}).get(key) for key in ("path", "sha256", "runs", "failed", "deterministic")},
                      "candidate": {key: (summary.get("candidate") or {}).get(key) for key in ("path", "sha256", "runs", "failed", "deterministic")},
                      "complete_pairs": (summary.get("plan") or {}).get("complete_pairs"), "outputs_identical": summary.get("outputs_identical"),
                      "checks": checks, "diagnostics": diagnostics, "outcome": cell_outcome(checks)})
    required = list(RETIREMENT_MODES) + [RUNTIME_CELL]
    if config.get("pairs") is not None:
        warnings.insert(0, "pair count fixed by --pairs %d instead of --target-minutes-per-cell: a test setting, not the gate's plan"
                        % config["pairs"])
    if config.get("cpu") is None:
        warnings.insert(0, "runs were not pinned to a CPU (--cpu -1)")
    return {"schema": RETIREMENT_SCHEMA, "directory": directory, "decision": RETIREMENT_DECISION, "contract": RETIREMENT_CONTRACT,
            "verdict": retirement_verdict(cells, required),
            "baseline": config.get("baseline"), "candidate": config.get("candidate"), "stage1": config.get("stage1", {}),
            "repo_root": config.get("repo_root"), "cpu": config.get("cpu"), "host": host,
            "plan": {"modes": config.get("modes"), "required_cells": required, "pairs": config.get("pairs"),
                     "target_minutes_per_cell": config.get("target_minutes_per_cell"), "estimated_minutes": config.get("estimated_minutes"),
                     "seed": config.get("seed"), "warmups": config.get("warmups"), "profile_steps": config.get("profile_steps"),
                     "total_s": config.get("total_s")},
            "limits": {name: {"limit": limit, "applies_to": RETIREMENT_LIMIT_TEXT[name][0], "bound": RETIREMENT_LIMIT_TEXT[name][1]}
                       for name, limit in RETIREMENT_LIMITS.items()},
            "cells": cells, "aggregates": retirement_aggregates(cells), "external_checks": RETIREMENT_EXTERNAL, "warnings": warnings}


def retirement_aggregates(cells):
    """Diagnostic, not gating: per interval metric of the allocator-mode
    cells, the geometric mean of their median ratios and of their upper
    bounds, beside #511's 1.02 primary aggregate limit."""
    compiler = [cell for cell in cells if cell["kind"] == "compiler"]
    result = {}
    for name in ("compiler_wall_time", "peak_rss"):
        rows = [cell["checks"].get(name) or {} for cell in compiler]
        complete = bool(rows) and all(positive(row.get("ratio")) and positive(row.get("ci_high")) for row in rows)
        result[name] = {"cells": len(rows), "limit": RETIREMENT_AGGREGATE_LIMIT, "gating": False,
                        "geomean_ratio": math.exp(statistics.fmean(math.log(row["ratio"]) for row in rows)) if complete else None,
                        "geomean_upper_bound": math.exp(statistics.fmean(math.log(row["ci_high"]) for row in rows)) if complete else None}
    return result


def retirement_markdown(summary):
    verdict = summary["verdict"]
    lines = ["# Native-retirement performance gate (#512)", "", "**%s**" % verdict["text"], "",
             "Decision record: %s; limits: `%s` (#511), per cell." % (summary["decision"], summary["contract"]), ""]
    for role in ("baseline", "candidate"):
        info = summary.get(role) or {}
        lines.append("- %s (%s): `%s` sha256 `%s`%s" % (role, "A" if role == "baseline" else "B", info.get("path"), info.get("sha256"),
                                                       ", revision %s" % info["revision"] if info.get("revision") else ""))
    for role, info in sorted(summary.get("stage1", {}).items()):
        lines.append("- stage-1 compiler built by the %s (%s cell): sha256 `%s`, %s bytes" % (role, RUNTIME_SOURCE_MODE, info["sha256"], fmt(info["size_bytes"])))
    plan = summary["plan"]
    lines += ["- source `%s`, %s; per cell %s; seed %s" % (
        summary["repo_root"], "unpinned" if summary["cpu"] is None else "pinned to CPU %s" % summary["cpu"],
        "%d pairs (--pairs)" % plan["pairs"] if plan.get("pairs") is not None else "about %g min" % plan["target_minutes_per_cell"], plan["seed"]),
              "- machine-readable: `retirement.json` (schema `%s`); `python3 tools/uarch_lab.py report %s` re-renders it and every cell" % (
                  summary["schema"], summary["directory"]), "", "## Cells", ""]
    rows = []
    for cell in summary["cells"]:
        for name, check in cell["checks"].items():
            interval = "[%.4f, %.4f]" % (check["ci_low"], check["ci_high"]) if check["ci_low"] is not None else "exact" if check["kind"] == "exact" else "-"
            rows.append([cell["cell"], check["label"], "-" if check["ratio"] is None else "%.4f" % check["ratio"], interval,
                         "-" if check["limit"] is None else "%g" % check["limit"], "**%s**" % check["outcome"], check["reason"]])
    lines += table(["cell", "metric", "B/A", "95% CI", "limit", "outcome", "reason"], rows)
    lines += ["", "Cell outcomes: " + ", ".join("%s %s (%s)" % (cell["cell"], cell["outcome"], cell["status"]) for cell in summary["cells"]),
              "", "## Limits (#511, per cell)", ""]
    lines += ["- %s: B/A %s at most %g (%s)" % (name, row["bound"], row["limit"], row["applies_to"]) for name, row in summary["limits"].items()]
    lines += ["", "Diagnostic only (not in the decision record's table): geometric mean over the %d allocator-mode cells against #511's "
              "%g primary aggregate limit: %s." % (
                  summary["aggregates"]["compiler_wall_time"]["cells"], RETIREMENT_AGGREGATE_LIMIT,
                  "; ".join("%s ratio %s, upper bounds %s" % (name, fmt(row["geomean_ratio"], ".4f"), fmt(row["geomean_upper_bound"], ".4f"))
                            for name, row in summary["aggregates"].items()))]
    lines += ["", "## Not checked by the lab", ""] + ["- %s: %s" % (row["check"], row["how"]) for row in summary["external_checks"]]
    lines += ["", "## Warnings", ""] + (["- " + warning for warning in summary["warnings"]] or ["- none"])
    lines += ["", "Each cell is a full `compare` directory (`<cell>/report.md`, `<cell>/summary.json`). Interval metrics use the "
              "median paired ratio and its exact sign-test 95% CI; code bytes are the exact ratio of executable-section bytes of the "
              "two stage-1 outputs. PASS needs the upper bound within the limit, FAIL a lower bound above it; an interval that "
              "crosses it is INCONCLUSIVE."]
    return "\n".join(lines) + "\n"


def render_retirement(directory, cells=False):
    """retirement.json and retirement.md from the raw files; with cells, each
    cell's compare directory is re-rendered first."""
    if cells:
        config = json.loads(read_text(os.path.join(directory, "retirement-config.json")) or "{}")
        for entry in config.get("cells", []):
            if os.path.isfile(os.path.join(directory, entry["cell"], "compare.json")):
                render_compare(os.path.join(directory, entry["cell"]))
    summary = retirement_summary(directory)
    write_json(os.path.join(directory, "retirement.json"), summary)
    text = retirement_markdown(summary)
    write_text(os.path.join(directory, "retirement.md"), text)
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
    lab = Lab(arguments.output, arguments.perf, cpu, ide, arguments.repo_root, extra, arguments.sudo, arguments.fresh_copy)
    lab.meta["config"] = {"command": shell_join(lab.workload("OUT")), "cpu": cpu, "runs": arguments.runs, "perf": arguments.perf,
                          "repo_root": lab.repo_root, "skip": arguments.skip, "sudo": arguments.sudo, "ide": ide,
                          "target_minutes": arguments.target_minutes, "fresh_copy": arguments.fresh_copy}
    lab.save_meta()
    print("uarch_lab: output %s; %s" % (lab.output, "%d timed runs (--runs)" % arguments.runs if arguments.runs is not None else
                                        "timed-run count chosen after %d pilot runs to land near %g min" % (PILOT_RUNS, arguments.target_minutes)), flush=True)
    started = time.monotonic()
    for step in STEPS:
        if step in arguments.skip or (step == "ibs" and not arguments.sudo) or (step == "threads" and not arguments.threads):
            lab.meta["steps"][step] = {"status": "skipped", "elapsed_s": 0.0, "note": "--skip" if step in arguments.skip else
                                       "needs --sudo" if step == "ibs" else "opt-in: --threads"}
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
                                  estimate_other_compiles(facts.get("metric_groups"), arguments.skip, arguments.sudo, arguments.threads))
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
            elif step == "threads":
                note = step_threads(lab)
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
    run.add_argument("--threads", action="store_true",
                     help="add the Superluminal-style thread capture for Hotspot (step_threads; richest with perf_event_paranoid -1)")
    run.add_argument("--skip", nargs="*", default=[], choices=STEPS)
    run.add_argument("--no-fresh-copy", dest="fresh_copy", action="store_false",
                     help="run the binary in place instead of a fresh copy per timed run and capture")
    run.add_argument("extra", nargs=argparse.REMAINDER, help="-- extra compile arguments")
    compare = commands.add_parser("compare", help="A/B: paired ABBA timing of two compilers on the same source, verdict with a 95%% CI")
    compare.add_argument("--baseline", required=True, help="the A compiler (e.g. a Release ide built from the merge base)")
    compare.add_argument("--candidate", required=True, help="the B compiler (the change under test)")
    compare.add_argument("--repo-root", default=".", help="the frozen source tree both compilers compile")
    compare.add_argument("--cpu", type=int, default=2, help="CPU to pin to (-1: unpinned)")
    compare.add_argument("--output", required=True)
    compare.add_argument("--pairs", type=int, default=None, help="timed pairs (overrides --target-minutes)")
    compare.add_argument("--target-minutes", type=float, default=15.0,
                         help="choose the pair count after the %d-pair pilot block so the comparison takes about this long" % PILOT_PAIRS)
    compare.add_argument("--warmups", type=int, default=1)
    compare.add_argument("--perf", default="perf")
    compare.add_argument("--profile-steps", default="", help="comma list of %s for both variants (default: none)" % ",".join(PROFILE_STEPS))
    compare.add_argument("--sudo", action="store_true", help="add the ibs profile step")
    compare.add_argument("--seed", type=int, default=DEFAULT_SEED, help="bootstrap seed (recorded)")
    compare.add_argument("--require-identical-output", action="store_true",
                         help="stop before timing when A and B outputs differ (default: report it)")
    compare.add_argument("--min-effect", type=float, default=DEFAULT_MIN_EFFECT, metavar="PCT",
                         help="practical floor in percent: faster/slower only when the whole 95%% CI lies beyond it (default %(default)s)")
    compare.add_argument("--no-fresh-copy", dest="fresh_copy", action="store_false",
                         help="run each binary in place instead of a fresh copy per run (the setting that showed a 0.5%% A/A bias in LAB3)")
    compare.add_argument("extra", nargs=argparse.REMAINDER, help="-- extra compile arguments")
    retire = commands.add_parser("retirement", help="historical #512 native-retirement v1 gate (archived compilers required): compare per allocator mode plus generated-program "
                                 "runtime, judged against the #511 limits (retirement.md, retirement.json)")
    retire.add_argument("--baseline", required=True, help="the A compiler: Clang Release ide from main")
    retire.add_argument("--candidate", required=True, help="the B compiler: Clang Release ide of the MIR-only tree")
    retire.add_argument("--repo-root", default=".", help="the frozen source tree every cell compiles")
    retire.add_argument("--cpu", type=int, default=2, help="CPU to pin to (-1: unpinned)")
    retire.add_argument("--output", required=True)
    retire.add_argument("--modes", default=",".join(RETIREMENT_MODES),
                        help="historical allocator modes (archived compilers required; default all four; fewer is a partial, never passing, run)")
    retire.add_argument("--target-minutes-per-cell", type=float, default=DEFAULT_CELL_MINUTES,
                        help="each cell's compare --target-minutes (default %(default)s)")
    retire.add_argument("--pairs", type=int, default=None, help="fixed pairs per cell (testing; overrides --target-minutes-per-cell)")
    retire.add_argument("--warmups", type=int, default=1)
    retire.add_argument("--perf", default="perf")
    retire.add_argument("--profile-steps", default="", help="passed to every cell's compare (default: none)")
    retire.add_argument("--sudo", action="store_true", help="passed to every cell's compare (adds the ibs profile step)")
    retire.add_argument("--seed", type=int, default=DEFAULT_SEED, help="bootstrap seed (recorded)")
    retire.add_argument("--baseline-rev", default=None, help="revision label of the baseline (recorded)")
    retire.add_argument("--candidate-rev", default=None, help="revision label of the candidate (recorded)")
    report = commands.add_parser("report", help="re-render DIR/report.md and DIR/summary.json from raw files (run, compare or retirement)")
    report.add_argument("directory")
    report.add_argument("--perf", default=None, help="perf used to derive reports an older run lacks (default: the recorded one, then PATH)")
    arguments = parser.parse_args(argv)
    if arguments.command == "run":
        command_run(arguments)
    elif arguments.command == "compare":
        command_compare(arguments)
    elif arguments.command == "retirement":
        command_retirement(arguments)
    else:
        directory = os.path.abspath(arguments.directory)
        if os.path.isfile(os.path.join(directory, "retirement-config.json")):
            print(render_retirement(directory, cells=True), end="")
        elif os.path.isfile(os.path.join(directory, "compare.json")):
            print(render_compare(directory), end="")
        else:
            print(render_report(directory, resolve_perf(load_meta(directory), arguments.perf)), end="")


if __name__ == "__main__":
    main()
