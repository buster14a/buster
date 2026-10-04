#!/usr/bin/env python3
"""Summarize a run_probe.sh output directory: ledger deltas, callgrind Ir
deltas, inclusive per-function Ir, output identity, wall-time pairs.

usage: summarize.py <output-dir> [workload ...]
"""
import csv
import pathlib
import re
import statistics
import sys

out = pathlib.Path(sys.argv[1])
workloads = sys.argv[2:] or ["self", "sqlite"]

FUNCTIONS = [
    "main", "compiler_driver_execute_c_single", "c_preprocess", "c_parse_ast", "c_analyze_semantics_core",
    "c_parse_bind_function_body", "c_parse_validate_lowering_constraints", "c_parse_expression_type_query",
    "c_type_parse_machine_run", "c_parse_validate_initializer_shape", "c_parse_infer_initializer_array_count_core",
    "c_parse_validate_static_initializers", "c_parse_infer_file_array_bounds", "c_parse_scope_for_token",
    "c_parse_lookup_entity_symbol", "c_parse_type_layout_core", "c_type_parse_frame_checkpoint", "c_type_parse_rollback",
    "c_lower_to_ir_with_options", "c_ir_lower_body", "c_ir_ssa_finish", "c_ir_query_execute",
    "c_ir_predict_expression_type", "c_ir_global_initializer", "c_ir_infer_incomplete_array_bounds",
    "ir_prepare_canonical_module", "ir_validate_canonical_module", "codegen_generate_canonical_module_with_trace",
    "object_from_canonical_codegen_module", "__memcpy_avx_unaligned_erms", "__memset_avx2_unaligned_erms",
    "__memcpy_evex_unaligned_erms", "__memset_evex_unaligned_erms", "memcpy", "memset",
]


def read_metrics(path):
    metrics = {}
    if path.exists():
        for line in path.read_text().splitlines():
            key, _, value = line.partition("=")
            if value.strip().lstrip("-").isdigit():
                metrics[key.strip()] = int(value)
    return metrics


def callgrind_total(path):
    total = None
    if path.exists():
        for line in path.open(errors="replace"):
            m = re.match(r"^(summary|totals):\s+(\d+)", line)
            if m:
                total = int(m.group(2))
    return total


def annotate_table(path):
    """function name -> Ir from a callgrind_annotate listing (first match per name)."""
    table = {}
    if not path.exists():
        return table
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"^\s*([\d,]+)\s+\(\s*[\d.]+%\)\s+(\S.*)$", line)
        if not m:
            continue
        where = m.group(2)
        func = where.split(" [")[0]
        func = func.rsplit(":", 1)[-1]
        func = func.split("'")[0]
        if func not in table:
            table[func] = int(m.group(1).replace(",", ""))
    return table


def show_metrics(w):
    base = read_metrics(out / f"{w}-base.metrics")
    probe = read_metrics(out / f"{w}-probe.metrics")
    syn = read_metrics(out / f"{w}-base-syntaxonly.metrics")
    if not base:
        print(f"[{w}] no base metrics")
        return
    print(f"\n[{w}] WORK LEDGER (base, probe, probe-base, base syntax-only)")
    prefixes = ("work.", "c_census.semantic.", "c_census.lower.", "c_census.parse.", "ir_construction.", "allocation.",
                "preprocessed.", "lexed.translated_bytes", "lexed.tokens")
    for key in sorted(base):
        if not key.startswith(prefixes):
            continue
        b = base[key]
        p = probe.get(key)
        s = syn.get(key)
        d = (p - b) if p is not None else None
        flag = ""
        if d:
            flag = "  <<<" if abs(d) >= max(1000, abs(b) // 50) else ""
        print(f"  {key:60} {b:>18,} {'' if p is None else f'{p:>18,}'} {'' if d is None else f'{d:>+18,}'} {'' if s is None else f'{s:>18,}'}{flag}")


def show_callgrind(w):
    print(f"\n[{w}] CALLGRIND (deterministic Ir)")
    totals = {}
    for s in ("base", "probe"):
        totals[s] = callgrind_total(out / f"{w}-{s}.callgrind")
        print(f"  total Ir {s:5}: {totals[s]:>18,}" if totals[s] else f"  total Ir {s:5}: missing")
    if totals["base"] and totals["probe"]:
        d = totals["probe"] - totals["base"]
        print(f"  probe - base: {d:>+18,}  ({100 * d / totals['base']:+.2f}% of base)")
    for s in ("base", "probe"):
        table = annotate_table(out / f"{w}-{s}.inclusive.txt")
        total = totals[s] or 1
        if not table:
            print(f"  [{s}] no inclusive listing")
            continue
        print(f"  [{s}] inclusive Ir by function (share of total)")
        for f in FUNCTIONS:
            if f in table:
                print(f"    {f:52} {table[f]:>18,}  {100 * table[f] / total:6.2f}%")
        excl = annotate_table(out / f"{w}-{s}.exclusive.txt")
        top = sorted(excl.items(), key=lambda kv: -kv[1])[:25]
        print(f"  [{s}] top exclusive")
        for f, ir in top:
            print(f"    {f:52} {ir:>18,}  {100 * ir / total:6.2f}%")


def show_timing(w):
    path = out / f"{w}-timing.csv"
    if not path.exists():
        print(f"[{w}] no timing")
        return
    rows = list(csv.DictReader(path.open()))
    by = {"base": [], "probe": []}
    pairs = {}
    hashes = {"base": set(), "probe": set()}
    for r in rows:
        try:
            wall = float(r["wall_s"])
        except ValueError:
            continue
        by[r["subject"]].append(wall)
        pairs.setdefault(r["pair"], {})[r["subject"]] = wall
        hashes[r["subject"]].add(r["sha256"])
    ratios = [p["probe"] / p["base"] for p in pairs.values() if "base" in p and "probe" in p]
    print(f"\n[{w}] WALL TIME (taskset -c 1, {len(ratios)} alternating pairs)")
    for s in ("base", "probe"):
        if by[s]:
            print(f"  {s:5} median {statistics.median(by[s]):.3f}s  min {min(by[s]):.3f}s  max {max(by[s]):.3f}s  n={len(by[s])}  distinct output hashes={len(hashes[s])}")
    if ratios:
        print(f"  paired probe/base: median {statistics.median(ratios):.4f}  min {min(ratios):.4f}  max {max(ratios):.4f}")


def show_identity(w):
    print(f"\n[{w}] OUTPUT IDENTITY")
    shas = {}
    p = out / "outputs.sha256"
    if p.exists():
        for line in p.read_text().splitlines():
            h, _, name = line.partition("  ")
            shas[pathlib.Path(name).name] = h
    for name in (f"{w}-base-ledger.out", f"{w}-base-plain.out", f"{w}-probe-ledger.out", f"{w}-probe-plain.out",
                 f"{w}-base-cg.out", f"{w}-probe-cg.out"):
        print(f"  {name:28} {shas.get(name, 'missing')}")
    bp, pp = shas.get(f"{w}-base-plain.out"), shas.get(f"{w}-probe-plain.out")
    print(f"  base == probe (plain): {bp is not None and bp == pp}")


print((out / "identity.txt").read_text() if (out / "identity.txt").exists() else "no identity.txt")
print((out / "statuses.txt").read_text() if (out / "statuses.txt").exists() else "no statuses.txt")
for w in workloads:
    show_identity(w)
    show_callgrind(w)
    show_timing(w)
    show_metrics(w)
