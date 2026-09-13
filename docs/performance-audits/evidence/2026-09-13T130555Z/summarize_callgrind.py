#!/usr/bin/env python3
"""Recover issue #564 source-line attribution from a raw Callgrind file.

The measured compiler is optimized, so the verifier is inlined into its caller.
This parser deliberately reports source-line self cost and direct child cost
separately.  It leaves line-zero, header, and out-of-range self cost
unattributed instead of redistributing it across check families.
"""

import argparse
import collections
import gzip
import json
import re


VALIDATOR_RANGES = (
    ("ownership", 4475, 4533),
    ("conversion_legality_helper", 4534, 4622),
    ("globals_and_relocations", 4623, 4725),
    ("values_and_value_provenance", 4726, 4811),
    ("block_parameters_and_edge_provenance", 4812, 4889),
    ("bit_field_helper", 4890, 4911),
    ("opcode_operation_relationships", 4912, 5556),
    ("instruction_chain_ids_results_terminators", 5557, 5629),
    ("block_structure", 5630, 5658),
    ("aliases", 5659, 5685),
    ("initializers", 5686, 5704),
    ("module_traversal", 5705, 5760),
)

# These are drill-down views inside the disjoint rows above.  Some deliberately
# overlap (a cast also validates provenance), so they must not be summed.
DETAIL_RANGES = (
    ("global_relocation_pair_loop", ((4696, 4704),)),
    ("value_provenance", ((4779, 4798),)),
    ("parameter_provenance_call_sites", ((4832, 4835), (4871, 4873))),
    ("call_and_signature_relationships", ((5145, 5178),)),
    ("conversion_legality", ((4534, 4622), (5286, 5317))),
    ("instruction_provenance_including_cast", ((5179, 5207), (5286, 5317), (5441, 5457), (5486, 5523))),
    ("control_opcode_relationships", ((5467, 5485), (5524, 5550))),
    ("instruction_header_type_and_storage", ((5557, 5578),)),
    ("operand_ids", ((5579, 5586),)),
    ("target_ids", ((5587, 5594),)),
    ("result_definition_and_type", ((5595, 5605),)),
    ("terminator_and_chain_advance", ((5611, 5627),)),
)


def reference(line, prefix):
    match = re.match(prefix + r"=\((\d+)\)(?:\s+.+)?$", line)
    return int(match.group(1)) if match else None


def position(token, previous):
    if token == "*":
        return previous
    if token[0] in "+-":
        return previous + int(token)
    return int(token)


def maps(lines):
    files = {}
    functions = {}
    for line in lines:
        match = re.match(r"(?:fl|fi|cfi)=\((\d+)\)\s+(.+)", line)
        if match:
            files[int(match.group(1))] = match.group(2)
        match = re.match(r"(?:fn|cfn)=\((\d+)\)\s+(.+)", line)
        if match:
            functions[int(match.group(1))] = match.group(2)
    return files, functions


def function_costs(lines, files, functions, target):
    targets = {identifier for identifier, name in functions.items() if name == target}
    if len(targets) != 1:
        raise SystemExit(f"expected one id for {target}, found {sorted(targets)}")
    target_id = targets.pop()
    active = False
    primary_file = inline_file = callee = None
    current_line = 0
    pending_call = False
    self_cost = collections.Counter()
    child_cost = collections.Counter()
    children = collections.Counter()
    for line in lines:
        identifier = reference(line, "fl")
        if identifier is not None:
            if active:
                break
            primary_file = identifier
            inline_file = None
            continue
        identifier = reference(line, "fn")
        if identifier is not None:
            active = identifier == target_id
            inline_file = None
            current_line = 0
            pending_call = False
            continue
        if not active:
            continue
        identifier = reference(line, "fi")
        if identifier is not None:
            inline_file = identifier
            continue
        identifier = reference(line, "fe")
        if identifier is not None:
            inline_file = None
            continue
        identifier = reference(line, "cfn")
        if identifier is not None:
            callee = identifier
            continue
        if line.startswith("calls="):
            pending_call = True
            continue
        if not line or "=" in line:
            continue
        fields = line.split()
        if len(fields) < 2 or not re.fullmatch(r"(?:\*|[+-]?\d+)", fields[0]):
            continue
        current_line = position(fields[0], current_line)
        cost = int(fields[1])
        key = (files.get(inline_file or primary_file, str(inline_file or primary_file)), current_line)
        if pending_call:
            child_cost[key] += cost
            children[(key, functions.get(callee, str(callee)))] += cost
            pending_call = False
        else:
            self_cost[key] += cost
    return self_cost, child_cost, children


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("callgrind")
    parser.add_argument("--validator-total", type=int, required=True)
    arguments = parser.parse_args()
    opener = gzip.open if arguments.callgrind.endswith(".gz") else open
    with opener(arguments.callgrind, "rt", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    files, functions = maps(lines)
    self_cost, child_cost, children = function_costs(lines, files, functions, "ir_validate_canonical_module")
    ir_paths = [path for path in files.values() if path.endswith("/src/buster/lib/compiler/ir/ir.c")]
    if len(set(ir_paths)) != 1:
        raise SystemExit(f"expected one ir.c path, found {sorted(set(ir_paths))}")
    ir_path = ir_paths[0]
    rows = []
    attributed = 0
    for name, first, last in VALIDATOR_RANGES:
        exclusive = sum(cost for (path, line), cost in self_cost.items() if path == ir_path and first <= line <= last)
        child = sum(cost for (path, line), cost in child_cost.items() if path == ir_path and first <= line <= last)
        inclusive = exclusive + child
        attributed += inclusive
        rows.append({
            "family": name,
            "first_line": first,
            "last_line": last,
            "exclusive_source_ir": exclusive,
            "direct_child_ir": child,
            "inclusive_source_ir": inclusive,
            "validator_share": inclusive / arguments.validator_total,
        })
    details = []
    for name, ranges in DETAIL_RANGES:
        exclusive = sum(
            cost
            for (path, line), cost in self_cost.items()
            if path == ir_path and any(first <= line <= last for first, last in ranges)
        )
        child = sum(
            cost
            for (path, line), cost in child_cost.items()
            if path == ir_path and any(first <= line <= last for first, last in ranges)
        )
        details.append({
            "detail": name,
            "ranges": ranges,
            "exclusive_source_ir": exclusive,
            "direct_child_ir": child,
            "inclusive_source_ir": exclusive + child,
            "validator_share": (exclusive + child) / arguments.validator_total,
        })
    parsed_total = sum(self_cost.values()) + sum(child_cost.values())
    line_zero = sum(cost for (path, line), cost in self_cost.items() if line == 0)
    out_of_range_ir = sum(
        cost
        for (path, line), cost in self_cost.items()
        if path == ir_path and line != 0 and not any(first <= line <= last for _, first, last in VALIDATOR_RANGES)
    )
    other_files = sum(cost for (path, line), cost in self_cost.items() if path != ir_path and line != 0)
    other_files += sum(cost for (path, line), cost in child_cost.items() if path != ir_path and line != 0)
    result = {
        "callgrind": arguments.callgrind,
        "event": "Ir",
        "validator_inclusive_ir": arguments.validator_total,
        "parsed_validator_ir": parsed_total,
        "source_ranges": rows,
        "overlapping_drill_down_do_not_sum": details,
        "unattributed_ir": arguments.validator_total - attributed,
        "unattributed_share": (arguments.validator_total - attributed) / arguments.validator_total,
        "unattributed_breakdown": {
            "line_zero_ir": line_zero,
            "ir_c_out_of_range_inlined_helpers_ir": out_of_range_ir,
            "other_source_files_ir": other_files,
        },
        "top_direct_children": [
            {"file": key[0][0], "line": key[0][1], "function": key[1], "ir": cost}
            for key, cost in children.most_common(20)
        ],
    }
    if parsed_total != arguments.validator_total:
        raise SystemExit(f"parsed {parsed_total}, expected {arguments.validator_total}")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
