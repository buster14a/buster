#!/usr/bin/env python3
"""Compile each probe with the census compiler and tabulate lowering counters.

Prints one row per probe with the per-function values after subtracting the
t_empty.c baseline (fixed per-unit costs). Operation counts only; no timing.
"""
import subprocess, sys, re
from pathlib import Path

IDE = sys.argv[1]
root = Path("/var/tmp/buster-probes")
probes = ["t_empty", "t_arith", "t_shortcircuit", "t_control", "t_aggregate", "t_extensions", "t_calls", "t_assign"]
extra_flags = sys.argv[2:]

KEYS = [
    ("tokens", "preprocessed.tokens"),
    ("body_tok", "ir_construction.body_tokens"),
    ("instr", "ir_construction.instruction_appends"),
    ("values", "ir_construction.value_appends"),
    ("blocks", "ir_construction.block_appends"),
    ("retract", "ir_construction.commit_retractions"),
    ("val_pre_ssa", "ir_construction.before_ssa_value_rows"),
    ("val_post_ssa", "ir_construction.after_ssa_value_rows"),
    ("frames", "work.lower.frame_pushes"),
    ("dispatch", "work.lower.dispatch_steps"),
    ("expr_roots", "work.lower.expression_roots"),
    ("core_runs", "work.lower.expression_core_runs"),
    ("core_tok", "work.lower.expression_core_tokens"),
    ("root_scans", "work.lower.root_scans"),
    ("rs_cond", "work.lower.root_scan_conditional_tokens"),
    ("rs_logic", "work.lower.root_scan_logical_tokens"),
    ("rs_assign", "work.lower.root_scan_assignment_tokens"),
    ("rs_comma", "work.lower.root_scan_comma_tokens"),
    ("rs_step", "work.lower.root_scan_step_tokens"),
    ("rs_update", "work.lower.root_scan_update_tokens"),
    ("prep_ctl", "work.lower.prepare_control_tokens"),
    ("prep_call", "work.lower.prepare_call_tokens"),
    ("prep_list", "work.lower.prepared_control_list_visits"),
    ("unary_scans", "work.lower.unary_end_scans"),
    ("unary_tok", "work.lower.unary_end_tokens"),
    ("stmt_scans", "work.lower.statement_span_scans"),
    ("stmt_tok", "work.lower.statement_span_tokens"),
    ("delim_q", "work.lower.delimiter_queries"),
    ("delim_miss", "work.lower.delimiter_index_misses"),
    ("delim_scan", "work.lower.delimiter_scans"),
    ("delim_tok", "work.lower.delimiter_scan_tokens"),
    ("str_eq", "c_census.lower.string_equal_calls"),
    ("spell_rd", "c_census.lower.spelling_reads"),
    ("arena_calls", "c_census.lower.arena_calls"),
    ("arena_bytes", "c_census.lower.arena_bytes"),
    ("type_pred", "work.rederive.lower_type_predictions"),
    ("query_roots", "work.rederive.lower_query_roots"),
]


def metrics_for(name):
    metrics_path = root / f"{name}.metrics"
    out = root / f"{name}.o"
    cmd = [IDE, "cc", "-g0", "-c", "-march=znver3", "-v", f"-fsource-metrics={metrics_path}", str(root / f"{name}.c"), "-o", str(out)] + extra_flags
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"{name}: FAILED\n{proc.stderr[-2000:]}", file=sys.stderr)
        return None, None
    values = {}
    for line in metrics_path.read_text().splitlines():
        key, _, value = line.partition("=")
        if value.isdigit():
            values[key] = int(value)
    verbose = {}
    for line in (proc.stdout + proc.stderr).splitlines():
        if line.startswith("IR_FRONTEND_SSA") or line.startswith("CODEGEN "):
            for match in re.finditer(r"(\w+)=(\d+)", line):
                verbose[("ssa_" if line.startswith("IR_FRONTEND_SSA") else "cg_") + match.group(1)] = int(match.group(2))
    return values, verbose


results = {}
for probe in probes:
    values, verbose = metrics_for(probe)
    if values is not None:
        results[probe] = (values, verbose)

base = results["t_empty"][0]
header = ["probe"] + [short for short, _ in KEYS] + ["ssa_params_created", "ssa_params_removed", "cg_instructions", "cg_values"]
print("\t".join(header))
for probe in probes[1:]:
    if probe not in results:
        continue
    values, verbose = results[probe]
    row = [probe]
    for short, key in KEYS:
        delta = values.get(key, 0) - (base.get(key, 0) if key not in ("preprocessed.tokens",) else 0)
        row.append(str(delta))
    row.append(str(verbose.get("ssa_parameters_created", 0)))
    row.append(str(verbose.get("ssa_parameters_removed", 0)))
    row.append(str(verbose.get("cg_instructions", 0)))
    row.append(str(verbose.get("cg_values", 0)))
    print("\t".join(row))
