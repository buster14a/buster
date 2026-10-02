#!/usr/bin/env python3
"""Summarize diagnostic phase-order artifacts without invoking a compiler.

Counts with different units remain separate. Code bytes and static instruction
counts do not establish runtime or compilation-performance acceptance.
"""

import argparse
import collections
import hashlib
import json
from pathlib import Path


PASSES = {0: "F", 1: "A", 2: "D", 3: "P"}
WORK_FIELDS = ("visits", "changes", "setup_values", "seed_rows", "block_visits", "sweeps")


def records(row, kind):
    return [record for record in row.get("records", []) if record.get("record") == kind]


def last_record(row, kind):
    values = records(row, kind)
    return values[-1] if values else {}


def indexed_functions(row):
    functions = {}
    ordinals = collections.Counter()
    for function in row.get("functions", []):
        name = function.get("before", {}).get("function", "<unknown>")
        ordinal = ordinals[name]
        ordinals[name] += 1
        functions[(name, ordinal)] = function
    return functions


def pass_changes(function):
    result = {name: 0 for name in PASSES.values()}
    for step in function.get("steps", []):
        name = PASSES.get(step.get("pass"))
        if name:
            result[name] += step.get("changes", 0)
    return result


def text_signature(row):
    sections = row.get("artifact", {}).get("text")
    if sections is None:
        return None
    return tuple((section["name"], section["bytes"], section["sha256"]) for section in sections)


def run_metrics(row):
    functions = list(indexed_functions(row).values())
    admitted = [function for function in functions if function.get("steps")]
    work = {name: {field: 0 for field in WORK_FIELDS} for name in PASSES.values()}
    second_f_steps = []
    for function in admitted:
        folds = []
        for step in function["steps"]:
            name = PASSES.get(step.get("pass"))
            if name:
                for field in WORK_FIELDS:
                    work[name][field] += step.get(field, 0)
            if name == "F":
                folds.append(step)
        second_f_steps.extend(folds[1:])
    fast = last_record(row, "IR_FAST")
    codegen = last_record(row, "CODEGEN")
    allocator = last_record(row, "CODEGEN_ALLOCATOR")
    # ide.c prints this record only when reloads || spills || copies is nonzero.
    triple = {key: allocator.get(key, 0 if codegen else None) for key in ("spills", "reloads", "copies")}
    artifact = row.get("artifact", {})
    result = {key: row.get(key) for key in ("source", "frontend", "allocator", "schedule")}
    result.update(
        status=row.get("command", {}).get("status"),
        observed_functions=len(functions), admitted_functions=len(admitted),
        completed_functions=sum("after" in function for function in functions),
        no_step_functions=len(functions) - len(admitted),
        changed_functions=sum(any(step.get("changes", 0) for step in function["steps"]) for function in admitted),
        no_match_functions=sum(not any(step.get("changes", 0) for step in function["steps"]) for function in admitted),
        p_changed_functions=sum(pass_changes(function)["P"] > 0 for function in admitted),
        p_removals=work["P"]["changes"], second_f_steps=len(second_f_steps),
        second_f_changed_steps=sum(step.get("changes", 0) > 0 for step in second_f_steps),
        second_f_no_match_steps=sum(step.get("changes", 0) == 0 for step in second_f_steps),
        second_f_changes=sum(step.get("changes", 0) for step in second_f_steps),
        work=work,
        instructions_before=sum(function.get("before", {}).get("instructions", 0) for function in functions),
        instructions_after=sum(function["after"]["instructions"] for function in functions if "after" in function),
        incomplete_instruction_population=sum("after" not in function for function in functions),
        ir_fast_skips={key: fast.get(key) for key in ("validation_skips", "budget_skips", "provenance_skips", "parameter_budget_hits")},
        text_bytes=artifact.get("text_bytes"), object_sha256=artifact.get("object_sha256"),
        allocator_counts=triple,
        stack_frame_bytes=codegen.get("stack_frame_bytes"),
        max_stack_frame_bytes=codegen.get("max_stack_frame_bytes"),
        stack_value_bytes=codegen.get("stack_value_bytes"), codegen_instructions=codegen.get("instructions"),
        fallback_functions=codegen.get("fallback_functions"),
        instrumentation_available=bool(functions),
    )
    if row.get("schedule", "").upper().count("F") > 1:
        result["second_f_skipped_functions"] = len(admitted) - sum(
            sum(step.get("pass") == 0 for step in function["steps"]) > 1 for function in admitted)
    return result


def subtract(candidate, baseline):
    return candidate - baseline if isinstance(candidate, int) and isinstance(baseline, int) else None


def comparison(baseline, candidate):
    first, second = indexed_functions(baseline), indexed_functions(candidate)
    differences, paired = [], 0
    for name, ordinal in sorted(first.keys() & second.keys()):
        left, right = first[(name, ordinal)], second[(name, ordinal)]
        if "after" not in left or "after" not in right:
            continue
        paired += 1
        left_changes, right_changes = pass_changes(left), pass_changes(right)
        before_delta = subtract(right["before"].get("instructions"), left["before"].get("instructions"))
        after_delta = subtract(right["after"].get("instructions"), left["after"].get("instructions"))
        change_delta = {key: right_changes[key] - left_changes[key] for key in PASSES.values()}
        if before_delta or after_delta or any(change_delta.values()):
            differences.append({"function": name, "name_ordinal": ordinal,
                                "instructions_before_delta": before_delta, "instructions_after_delta": after_delta,
                                "pass_changes_delta": change_delta})
    first_metrics, second_metrics = run_metrics(baseline), run_metrics(candidate)
    result = {key: candidate.get(key) for key in ("source", "frontend", "allocator", "schedule")}
    result.update(baseline_schedule=baseline["schedule"],
                  both_compile_success=baseline.get("command", {}).get("status") == 0 and candidate.get("command", {}).get("status") == 0,
                  paired_complete_functions=paired, baseline_functions=len(first), candidate_functions=len(second),
                  unpaired_baseline_functions=len(first.keys() - second.keys()),
                  unpaired_candidate_functions=len(second.keys() - first.keys()),
                  function_differences=differences,
                  instructions_after_delta=subtract(second_metrics["instructions_after"], first_metrics["instructions_after"]),
                  p_removals_delta=second_metrics["p_removals"] - first_metrics["p_removals"],
                  text_bytes_delta=subtract(second_metrics["text_bytes"], first_metrics["text_bytes"]),
                  text_content_differs=(text_signature(candidate) != text_signature(baseline))
                                      if text_signature(candidate) is not None and text_signature(baseline) is not None else None,
                  allocator_delta={key: subtract(second_metrics["allocator_counts"][key], first_metrics["allocator_counts"][key])
                                   for key in ("spills", "reloads", "copies")},
                  stack_frame_bytes_delta=subtract(second_metrics["stack_frame_bytes"], first_metrics["stack_frame_bytes"]),
                  work_delta={name: {field: second_metrics["work"][name][field] - first_metrics["work"][name][field]
                                    for field in WORK_FIELDS} for name in PASSES.values()})
    return result


def division_count(symbol):
    return sum(count for mnemonic, count in symbol.get("mnemonics", {}).items()
               if mnemonic.startswith(("div", "idiv")))


def symbol_metrics(symbol):
    return {key: symbol.get(key) for key in ("instructions", "encoded_bytes", "stack_references")} | {
        "division_instructions": division_count(symbol)}


def fixture_report(rows):
    result = {"baselines": [], "differences": [], "semantics": []}
    index = {(row["frontend"], row["allocator"], row["schedule"]): row for row in rows if row["source"] == "interactions"}
    for key, row in index.items():
        frontend, mode, schedule = key
        result["semantics"].append({"frontend": frontend, "allocator": mode, "schedule": schedule,
                                    "compile_status": row.get("command", {}).get("status"),
                                    **row.get("semantics", {"unavailable": True})})
        baseline = index.get((frontend, mode, "FADP"))
        if schedule == "FADP":
            result["baselines"].append({"frontend": frontend, "allocator": mode,
                                       "symbols": {name: symbol_metrics(symbol) for name, symbol in row.get("disassembly", {}).items()}})
        elif baseline:
            for name in sorted(row.get("disassembly", {}).keys() & baseline.get("disassembly", {}).keys()):
                left, right = baseline["disassembly"][name], row["disassembly"][name]
                first, second = symbol_metrics(left), symbol_metrics(right)
                delta = {field: subtract(second[field], first[field]) for field in first}
                mnemonics = {mnemonic: right.get("mnemonics", {}).get(mnemonic, 0) - left.get("mnemonics", {}).get(mnemonic, 0)
                             for mnemonic in left.get("mnemonics", {}).keys() | right.get("mnemonics", {}).keys()}
                mnemonics = {name: value for name, value in mnemonics.items() if value}
                if any(delta.values()) or mnemonics:
                    result["differences"].append({"frontend": frontend, "allocator": mode, "schedule": schedule,
                                                   "symbol": name, "delta": delta, "mnemonic_delta": mnemonics})
    return result


def table(headers, rows):
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |"]
    for row in rows:
        lines.append("| " + " | ".join("NA" if cell is None else str(cell).replace("|", "\\|") for cell in row) + " |")
    return "\n".join(lines)


def markdown(report):
    lines = ["# Phase-order diagnostic comparison", "", f"Revision: `{report['revision']}`.", "",
             "Primary tables use direct-SSA FAST only. Failed compilations retain diagnostic counts but are excluded from successful paired conclusions. No-step functions are outside the no-match denominator. Counts represent observed operations, not wall time or acceptance.", "",
             "Step visits retain each pass's existing currency; DCE visits include its use-count and deletion work. D clear-values, D seed-rows, P block-visits and P sweeps are separate columns and must not be added as interchangeable operations. Alias resolution, initial replacement setup and compaction are not fully counted.", "",
             "## Primary population and artifacts", ""]
    lines.append(table(["Source", "Schedule", "Status", "Observed/admitted", "Changed/no-match", "P changed", "F2 changed/no-match", "IR rows", ".text bytes"],
                       [[row["source"], row["schedule"], row["status"], f"{row['observed_functions']}/{row['admitted_functions']}",
                         f"{row['changed_functions']}/{row['no_match_functions']}", row["p_changed_functions"],
                         f"{row['second_f_changed_steps']}/{row['second_f_no_match_steps']}", row["instructions_after"], row["text_bytes"]]
                        for row in report["primary"]]))
    lines.extend(["", "## Primary work and allocation", ""])
    lines.append(table(["Source", "Schedule", "F visits", "A visits", "D visits", "P visits", "D clear-values", "D seed-rows", "P blocks", "P sweeps", "Spills/reloads/copies", "Frame bytes"],
                       [[row["source"], row["schedule"], *[row["work"][name]["visits"] for name in PASSES.values()],
                         row["work"]["D"]["setup_values"], row["work"]["D"]["seed_rows"],
                         row["work"]["P"]["block_visits"], row["work"]["P"]["sweeps"],
                         "/".join("NA" if row["allocator_counts"][key] is None else str(row["allocator_counts"][key])
                                  for key in ("spills", "reloads", "copies")), row["stack_frame_bytes"]]
                        for row in report["primary"]]))
    lines.extend(["", "## FAPD versus FADP", ""])
    lines.append(table(["Source", "Successful pair", "Paired functions", "Different function counts", "IR row delta", "P removal delta", ".text delta", ".text differs", "Spill/reload/copy delta", "Frame delta"],
                       [[row["source"], row["both_compile_success"], row["paired_complete_functions"], len(row["function_differences"]),
                         row["instructions_after_delta"], row["p_removals_delta"], row["text_bytes_delta"], row["text_content_differs"],
                         "/".join("NA" if value is None else str(value) for value in row["allocator_delta"].values()),
                         row["stack_frame_bytes_delta"]] for row in report["fapd_comparisons"]]))
    lines.extend(["", "## Additional FAPfD bytes versus FAPD", ""])
    wins = report["fapfd_text_reductions_vs_fapd"]
    lines.append(table(["Source", "Bytes saved", "IR row delta", "P removal delta"],
                       [[row["source"], -row["text_bytes_delta"], row["instructions_after_delta"], row["p_removals_delta"]] for row in wins])
                 if wins else "No successful paired source has fewer .text bytes under FAPfD. This does not establish runtime or compile-time equivalence.")
    lines.extend(["", "## Fixture symbol differences versus FADP", ""])
    lines.append(table(["Frontend", "Allocator", "Schedule", "Symbol", "Instruction delta", "Byte delta", "Stack-reference delta", "Division delta"],
                       [[row["frontend"], row["allocator"], row["schedule"], row["symbol"], row["delta"]["instructions"],
                         row["delta"]["encoded_bytes"], row["delta"]["stack_references"], row["delta"]["division_instructions"]]
                        for row in report["fixtures"]["differences"]]))
    lines.extend(["", "Stack references count static operands mentioning rsp/rbp, including address calculations; they are not dynamic memory-traffic measurements.", "",
                  f"Semantic comparisons: {report['semantic_counts']['matched']} matched, {report['semantic_counts']['mismatched']} mismatched, {report['semantic_counts']['unavailable']} unavailable.", "",
                  f"Primary runs: {report['denominators']['primary_runs']}; failed: {report['denominators']['failed_primary_runs']}; uninstrumented: {report['denominators']['uninstrumented_primary_runs']}; observed no-step functions: {report['denominators']['no_step_primary_function_runs']}.", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--output", required=True, type=Path, help="new output directory for comparisons.json and comparisons.md")
    args = parser.parse_args()
    raw = args.summary.read_bytes()
    source = json.loads(raw)
    rows = source.get("runs", [])
    primary_rows = [row for row in rows if row.get("frontend") == "direct-ssa" and row.get("allocator") == "fast"]
    primary = [run_metrics(row) for row in primary_rows]
    index = {(row["source"], row["frontend"], row["allocator"], row["schedule"]): row for row in rows}
    comparisons = []
    all_comparisons = []
    fapfd = []
    for row in primary_rows:
        source_key = (row["source"], "direct-ssa", "fast")
        if row["schedule"] != "FADP" and (*source_key, "FADP") in index:
            paired = comparison(index[(*source_key, "FADP")], row)
            all_comparisons.append(paired)
            if row["schedule"] == "FAPD":
                comparisons.append(paired)
        if row["schedule"] == "FAPfD" and (*source_key, "FAPD") in index:
            fapfd.append(comparison(index[(*source_key, "FAPD")], row))
    fixtures = fixture_report(rows)
    semantic_counts = collections.Counter()
    for observation in fixtures["semantics"]:
        semantic_counts["unavailable" if observation.get("unavailable") else
                        "matched" if observation.get("matches_clang") else "mismatched"] += 1
    report = {"schema": "buster-phase-order-comparison-v1", "revision": source.get("revision"),
              "summary_path": str(args.summary.resolve()), "summary_sha256": hashlib.sha256(raw).hexdigest(),
              "compiler_sha256": source.get("compiler_sha256"), "source_manifest": source.get("source_manifest"),
              "scope": "diagnostic work counts and code artifacts; no accepted performance measurements",
              "primary": primary, "all_primary_comparisons_vs_fadp": all_comparisons,
              "fapd_comparisons": comparisons, "fapfd_comparisons_vs_fapd": fapfd,
              "fapd_count_or_text_differences": [row["source"] for row in comparisons if row["both_compile_success"] and
                                                (row["function_differences"] or row["text_content_differs"])],
              "fapfd_text_reductions_vs_fapd": [row for row in fapfd if row["both_compile_success"] and
                                                row["text_bytes_delta"] is not None and row["text_bytes_delta"] < 0],
              "fixtures": fixtures, "semantic_counts": {key: semantic_counts[key] for key in ("matched", "mismatched", "unavailable")},
              "denominators": {"total_runs": len(rows), "primary_runs": len(primary),
                               "failed_primary_runs": sum(row["status"] != 0 for row in primary),
                               "uninstrumented_primary_runs": sum(not row["instrumentation_available"] for row in primary),
                               "no_step_primary_function_runs": sum(row["no_step_functions"] for row in primary)},
              "failures": source.get("failures", []), "unavailable": source.get("unavailable", []),
              "limitations": ["Count-identical functions are not proven IR-identical.",
                              "Failed-compilation structural diagnostics are observations, not accepted artifacts.",
                              "No-step functions are excluded from no-match denominators.",
                              "Allocator record absence implies zero spills/reloads/copies only when CODEGEN was emitted.",
                              "Canonical instruction counts, encoded instructions, visits and bytes retain separate currencies.",
                              "No phase clocks, generated-program timing, dedicated-host throughput or RSS acceptance was collected."]}
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "comparisons.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    (args.output / "comparisons.md").write_text(markdown(report))
    print(json.dumps({"output": str(args.output.resolve()), "primary_runs": len(primary),
                      "fapd_different_sources": report["fapd_count_or_text_differences"],
                      "fapfd_text_reduction_sources": [row["source"] for row in report["fapfd_text_reductions_vs_fapd"]],
                      "semantics": report["semantic_counts"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
