#!/usr/bin/env python3
"""Offline x86-64 ELF branch-width census; never rewrites object bytes.

Input must be independently identified as a Buster-produced ELF64 ET_REL object.
ELF symbols/relocations delimit bodies and exclude unresolved branch fields;
GNU objdump supplies instruction boundaries, cross-checked against section bytes.
The model freezes every noncandidate fragment, INCLUDING observed padding. It
does not recover/reapply assembler alignment directives, resolve relocations,
or claim whole-object executability, minimum aligned size, or compiler speedup.
"""

import argparse
import bisect
import collections
import hashlib
import json
import os
import pathlib
import re
import struct
import subprocess


def read_elf(path):
    data = pathlib.Path(path).read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("requires ELF64 little-endian version 1")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    if header[1:3] != (1, 62):
        raise ValueError("requires x86-64 ET_REL, not a linked executable")
    section_offset, section_stride, section_count, name_section = header[6], header[11], header[12], header[13]
    if section_stride != 64 or not section_count or name_section >= section_count:
        raise ValueError("unsupported extended/invalid section-header form")
    sections = []
    for i in range(section_count):
        row = struct.unpack_from("<IIQQQQIIQQ", data, section_offset + i * section_stride)
        section = dict(zip(("name_offset", "type", "flags", "address", "offset", "size", "link", "info", "alignment", "entry_size"), row))
        section["index"] = i
        section["data"] = data[section["offset"]:section["offset"] + section["size"]] if section["type"] != 8 else b""
        if section["type"] != 8 and len(section["data"]) != section["size"]:
            raise ValueError("section payload is outside object")
        sections.append(section)

    def string(table, offset):
        if not 0 <= offset < len(table):
            raise ValueError("string-table reference out of bounds")
        end = table.find(b"\0", offset)
        if end < 0:
            raise ValueError("unterminated string-table reference")
        return table[offset:end].decode("utf-8", errors="replace")

    for section in sections:
        section["name"] = string(sections[name_section]["data"], section["name_offset"])
    symbols = []
    symbol_tables = {}
    for section in sections:
        if section["type"] == 2:
            if section["entry_size"] != 24 or section["size"] % 24:
                raise ValueError("invalid ELF symbol-table stride")
            table = []
            names = sections[section["link"]]["data"]
            for pos in range(0, section["size"], 24):
                name, info, other, index, value, size = struct.unpack_from("<IBBHQQ", section["data"], pos)
                symbol = {"name": string(names, name), "type": info & 15, "binding": info >> 4,
                          "section": index, "value": value, "size": size}
                symbols.append(symbol)
                table.append(symbol)
            symbol_tables[section["index"]] = table
    relocations = collections.defaultdict(list)
    for section in sections:
        if section["type"] in (4, 9):
            stride = 24 if section["type"] == 4 else 16
            if section["entry_size"] != stride or section["size"] % stride:
                raise ValueError("invalid ELF relocation-table stride")
            symbols_for_table = symbol_tables[section["link"]]
            for pos in range(0, section["size"], stride):
                if stride == 24:
                    offset, info, addend = struct.unpack_from("<QQq", section["data"], pos)
                else:
                    offset, info = struct.unpack_from("<QQ", section["data"], pos)
                    addend = None
                relocations[section["info"]].append({"offset": offset, "type": info & 0xffffffff,
                                                   "symbol": symbols_for_table[info >> 32]["name"], "addend": addend})
    return data, sections, symbols, relocations


def disassemble(path, sections, objdump):
    command = [objdump, "-dw", "--disassemble-zeroes", str(path)]
    result = subprocess.run(command, text=True, capture_output=True, check=True,
                            env={**os.environ, "LC_ALL": "C"})
    by_name = {s["name"]: s for s in sections}
    instructions = collections.defaultdict(list)
    section = None
    pattern = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(\S.*)$")
    for line in result.stdout.splitlines():
        if line.startswith("Disassembly of section ") and line.endswith(":"):
            name = line[len("Disassembly of section "):-1]
            section = by_name[name]
        else:
            match = pattern.match(line) if section else None
            if match:
                offset = int(match.group(1), 16)
                raw = bytes.fromhex(match.group(2))
                if not raw or section["data"][offset:offset + len(raw)] != raw:
                    raise ValueError("objdump bytes disagree with ELF section at %s+%x" % (section["name"], offset))
                instructions[section["index"]].append({"offset": offset, "raw": raw, "text": match.group(3)})
    for rows in instructions.values():
        rows.sort(key=lambda row: row["offset"])
    return instructions, command


def relative_branch(raw):
    """Only exact unprefixed x86 forms; prefix/indirect branches stay opaque."""
    if len(raw) == 5 and raw[0] in (0xe8, 0xe9):
        return ("call" if raw[0] == 0xe8 else "jmp", 32, int.from_bytes(raw[1:], "little", signed=True))
    if len(raw) == 6 and raw[0] == 0x0f and 0x80 <= raw[1] <= 0x8f:
        return ("jcc", 32, int.from_bytes(raw[2:], "little", signed=True))
    if len(raw) == 2 and (raw[0] == 0xeb or 0x70 <= raw[0] <= 0x7f or 0xe0 <= raw[0] <= 0xe3):
        kind = "jmp" if raw[0] == 0xeb else "jcc" if raw[0] < 0x80 else "loop-or-jcxz"
        return (kind, 8, int.from_bytes(raw[1:], "little", signed=True))
    return None


def layout(lengths, candidates, short_flags):
    offsets = [0]
    for i, length in enumerate(lengths):
        candidate = candidates.get(i)
        offsets.append(offsets[-1] + (2 if candidate is not None and short_flags[candidate] else length))
    return offsets


def violations(offsets, lengths, edges, candidates, short_flags):
    bad = []
    for edge in edges:
        source, target, width = edge["source"], edge["target"], edge["width"]
        if source in candidates:
            width = 8 if short_flags[candidates[source]] else 32
        delta = offsets[target] - offsets[source + 1]
        if not -(1 << (width - 1)) <= delta < (1 << (width - 1)):
            bad.append((source, width, delta))
    return bad


def evaluate(lengths, edges, candidate_sources):
    candidates = {source: i for i, source in enumerate(candidate_sources)}
    count = len(candidates)
    baseline_flags = [False] * count
    baseline = layout(lengths, candidates, baseline_flags)
    if violations(baseline, lengths, edges, candidates, baseline_flags):
        raise ValueError("original decoded intra-function branch is not representable")
    edge_by_source = {edge["source"]: edge for edge in edges}
    one_shot_flags = []
    for source in candidate_sources:
        edge = edge_by_source[source]
        delta = baseline[edge["target"]] - baseline[source + 1]
        # Forward targets move with this instruction's deleted bytes, exactly
        # cancelling the next-IP change. Backward/self targets do not move;
        # prospective shortening therefore makes delta less negative.
        if edge["target"] <= source:
            delta += lengths[source] - 2
        one_shot_flags.append(-128 <= delta <= 127)
    one_shot = layout(lengths, candidates, one_shot_flags)
    one_shot_bad = violations(one_shot, lengths, edges, candidates, one_shot_flags)
    flags = [True] * count
    iterations, widenings, branch_checks = 0, 0, 0
    while True:
        current = layout(lengths, candidates, flags)
        iterations += 1
        bad = violations(current, lengths, edges, candidates, flags)
        branch_checks += len(edges)
        changed = False
        for source, width, delta in bad:
            if source not in candidates or not flags[candidates[source]]:
                raise ValueError("widening cannot repair fixed edge; model is not valid")
            flags[candidates[source]] = False
            widenings += 1
            changed = True
        if not changed:
            break
        if iterations > count:
            raise ValueError("widening exceeded its monotonic bound")
    result = {
        "baseline_bytes": baseline[-1], "baseline_branch_legal": True,
        "one_shot_bytes": one_shot[-1], "one_shot_short_branches": sum(one_shot_flags),
        "one_shot_branch_legal": not one_shot_bad,
        "shortest_widen_bytes": current[-1], "shortest_widen_short_branches": sum(flags),
        "shortest_widen_branch_legal": not bad, "widening_iterations": iterations,
        "widenings": widenings, "widening_branch_checks": branch_checks,
        "widening_fragment_visits": iterations * len(lengths),
        "candidate_count": count, "edge_count": len(edges),
        "oracle": None,
    }
    if count <= 12:
        best_size, best_mask, legal_count = None, None, 0
        for mask in range(1 << count):
            # Independent address equation, rather than the subject algorithms'
            # layout/violations helpers: delete each selected instruction's
            # excess bytes and count deletions strictly before each boundary.
            deletions = [(source, lengths[source] - 2) for i, source in enumerate(candidate_sources) if mask & (1 << i)]
            legal = True
            for edge in edges:
                source, target = edge["source"], edge["target"]
                target_removed = sum(size for position, size in deletions if position < target)
                source_end_removed = sum(size for position, size in deletions if position < source + 1)
                delta = baseline[target] - target_removed - (baseline[source + 1] - source_end_removed)
                width = 8 if source in candidates and mask & (1 << candidates[source]) else edge["width"]
                if not -(1 << (width - 1)) <= delta < (1 << (width - 1)):
                    legal = False
                    break
            trial_size = baseline[-1] - sum(size for position, size in deletions)
            if legal:
                legal_count += 1
                if best_size is None or trial_size < best_size:
                    best_size, best_mask = trial_size, mask
        result["oracle"] = {"assignments": 1 << count, "legal_assignments": legal_count,
                            "minimum_model_bytes": best_size, "first_minimum_mask": best_mask,
                            "widening_matches_minimum": current[-1] == best_size,
                            "one_shot_matches_minimum": one_shot[-1] == best_size}
    return result


def prepare_section_indexes(instructions, symbols, relocations):
    """Build each section's address indexes once, outside the function loop."""
    symbols_by_section = collections.defaultdict(list)
    for symbol in symbols:
        symbols_by_section[symbol["section"]].append(symbol)
    section_ids = set(instructions) | set(symbols_by_section) | set(relocations)
    indexes = {}
    for section_id in section_ids:
        rows = instructions.get(section_id, [])
        section_symbols = sorted(symbols_by_section[section_id], key=lambda symbol: symbol["value"])
        section_relocs = sorted(relocations.get(section_id, []), key=lambda relocation: relocation["offset"])
        indexes[section_id] = {
            "rows": rows,
            "row_offsets": [row["offset"] for row in rows],
            "symbols": section_symbols,
            "symbol_values": [symbol["value"] for symbol in section_symbols],
            "relocations": section_relocs,
            "relocation_offsets": [relocation["offset"] for relocation in section_relocs],
        }
    return indexes


def overlapping_function_bodies(bodies):
    """Mark the same nonidentical overlapping extents as the pairwise scan."""
    overlapping = set()
    current_section, maximum_end, maximum_body = None, None, None
    for key in sorted(bodies):
        section, start, size = key
        if section != current_section:
            current_section, maximum_end, maximum_body = section, start + size, key
        else:
            if start < maximum_end:
                overlapping.add(maximum_body)
                overlapping.add(key)
            if start + size > maximum_end:
                maximum_end, maximum_body = start + size, key
    return overlapping


def model_function(function, section, section_index):
    start, end = function["value"], function["value"] + function["size"]
    rows, offsets = section_index["rows"], section_index["row_offsets"]
    begin = bisect.bisect_left(offsets, start)
    finish = bisect.bisect_left(offsets, end)
    body = rows[begin:finish]
    if not body or body[0]["offset"] != start or body[-1]["offset"] + len(body[-1]["raw"]) != end:
        raise ValueError("function bounds do not tile decoded fragments")
    lengths = [len(row["raw"]) for row in body]
    boundaries = [row["offset"] for row in body] + [end]
    if any(boundaries[i + 1] - boundaries[i] != length for i, length in enumerate(lengths)):
        raise ValueError("function has an undecoded gap or overlapping fragments")
    boundary_indices = {point: i for i, point in enumerate(boundaries)}
    symbol_begin = bisect.bisect_left(section_index["symbol_values"], start)
    symbol_end = bisect.bisect_right(section_index["symbol_values"], end)
    function_symbols = section_index["symbols"][symbol_begin:symbol_end]
    symbol_points = {s["value"] for s in function_symbols}
    reloc_begin = bisect.bisect_left(section_index["relocation_offsets"], start)
    reloc_end = bisect.bisect_left(section_index["relocation_offsets"], end)
    function_relocs = section_index["relocations"][reloc_begin:reloc_end]
    reloc_points = {r["offset"] for r in function_relocs}
    candidate_sources, edges = [], []
    excluded = collections.Counter()
    for i, row in enumerate(body):
        raw = row["raw"]
        branch = relative_branch(raw)
        if branch:
            kind, width, delta = branch
            field = row["offset"] + len(raw) - width // 8
            if any(row["offset"] <= point < row["offset"] + len(raw) for point in reloc_points):
                excluded["relocated_relative_branches"] += 1
                continue
            target = row["offset"] + len(raw) + delta
            if not start <= target <= end:
                excluded["cross_function_relative_branches"] += 1
                continue
            if target not in boundary_indices:
                raise ValueError("intra-function branch targets a fragment interior")
            edge = {"source": i, "target": boundary_indices[target], "width": width, "kind": kind}
            edges.append(edge)
            if width == 32 and kind in ("jmp", "jcc"):
                if any(row["offset"] < point < row["offset"] + len(raw) for point in symbol_points):
                    excluded["branch_with_interior_symbol"] += 1
                else:
                    candidate_sources.append(i)
    result = evaluate(lengths, edges, candidate_sources)
    result.update({"name": function["name"], "section": section["name"], "original_start": start,
                   "fragment_count": len(lengths), "fixed_rel8_edges": sum(e["width"] == 8 for e in edges),
                   "fixed_local_calls": sum(e["kind"] == "call" for e in edges),
                   "excluded_branches": dict(excluded), "symbols_requiring_offset_mapping": len(function_symbols),
                   "code_relocations_requiring_offset_mapping": len(function_relocs),
                   "rip_relative_instructions_requiring_separate_fixup_authority": sum("(%rip)" in row["text"] for row in body),
                   "padding_policy": "observed bytes frozen; original alignment directives are unavailable"})
    return result


def summarize(functions):
    keys = ("baseline_bytes", "one_shot_bytes", "shortest_widen_bytes", "candidate_count", "edge_count",
            "one_shot_short_branches", "shortest_widen_short_branches", "widening_iterations", "widenings",
            "widening_branch_checks", "widening_fragment_visits", "fragment_count", "fixed_rel8_edges",
            "code_relocations_requiring_offset_mapping", "rip_relative_instructions_requiring_separate_fixup_authority")
    summary = {key: sum(f[key] for f in functions) for key in keys}
    summary["functions"] = len(functions)
    summary["one_shot_saved_bytes"] = summary["baseline_bytes"] - summary["one_shot_bytes"]
    summary["widening_saved_bytes"] = summary["baseline_bytes"] - summary["shortest_widen_bytes"]
    summary["all_modeled_branches_legal"] = all(f["one_shot_branch_legal"] and f["shortest_widen_branch_legal"] for f in functions)
    oracles = [f["oracle"] for f in functions if f["oracle"] is not None]
    summary["oracle_functions"] = len(oracles)
    summary["oracle_assignments"] = sum(o["assignments"] for o in oracles)
    summary["oracle_widening_mismatches"] = sum(not o["widening_matches_minimum"] for o in oracles)
    summary["oracle_one_shot_mismatches"] = sum(not o["one_shot_matches_minimum"] for o in oracles)
    summary["maximum_widening_iterations"] = max((f["widening_iterations"] for f in functions), default=0)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("objects", nargs="+")
    parser.add_argument("--output", required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--producer", default="Buster trusted compiler built from supplied revision")
    parser.add_argument("--objdump", default="objdump")
    parser.add_argument("--exclude-symbol", action="append", default=[])
    parser.add_argument("--proven-no-inline-asm", action="store_true", help="source-checked fixture batch only; caller supplies evidence")
    args = parser.parse_args()
    all_functions, objects = [], []
    for path in args.objects:
        data, sections, symbols, relocations = read_elf(path)
        instructions, command = disassemble(path, sections, args.objdump)
        section_indexes = prepare_section_indexes(instructions, symbols, relocations)
        bodies = collections.defaultdict(list)
        skipped = []
        for symbol in symbols:
            if symbol["type"] == 2 and 0 < symbol["section"] < len(sections) and sections[symbol["section"]]["flags"] & 4:
                if symbol["size"] == 0:
                    skipped.append({"name": symbol["name"], "reason": "zero symbol size; no invented function extent"})
                else:
                    bodies[(symbol["section"], symbol["value"], symbol["size"])].append(symbol)
        functions = []
        overlapping = overlapping_function_bodies(bodies)
        for key, aliases in sorted(bodies.items()):
            representative = min(aliases, key=lambda symbol: symbol["name"])
            if key in overlapping:
                skipped.append({"name": representative["name"], "reason": "overlapping nonidentical function symbol extents"})
                continue
            if any(symbol["name"] in args.exclude_symbol for symbol in aliases):
                skipped.append({"name": representative["name"], "reason": "caller excluded symbol"})
                continue
            try:
                function = model_function(representative, sections[key[0]], section_indexes[key[0]])
                function["aliases"] = [s["name"] for s in aliases]
                function["object"] = str(path)
                functions.append(function)
            except ValueError as error:
                skipped.append({"name": representative["name"], "reason": str(error)})
        all_functions.extend(functions)
        objects.append({"path": str(path), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
                        "objdump_command": command, "summary": summarize(functions), "skipped": skipped,
                        "executable_sections": [{"name": s["name"], "bytes": s["size"], "alignment": s["alignment"]}
                                                for s in sections if s["flags"] & 4],
                        "debug_unwind_sections": [{"name": s["name"], "bytes": s["size"]} for s in sections
                                                  if s["name"].startswith(".debug") or s["name"] in (".eh_frame", ".pdata", ".xdata")]})
    output = {"schema": "buster-offline-x86-layout-census-v1", "revision": args.revision, "producer": args.producer,
              "objdump_version": subprocess.run([args.objdump, "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0],
              "source_checked_no_inline_asm": args.proven_no_inline_asm,
              "model": "function-relative lengths; fixed noncandidate bytes and fixed emitted padding; exact intra-function unrelocated branches",
              "one_shot_rule": "original layout with prospective own shortening; backward/self delta adds near_length-2; forward delta unchanged",
              "limits": ["No object rewrite or relocation resolution performed",
                         "No alignment directives recovered or reapplied; unity census is frozen-padding sensitivity only",
                         "No claim that debug ranges, unwind offsets, RIP-relative fields, section boundaries or external references are valid after rewriting",
                         "No generated-program runtime or compiler throughput measurement",
                         "Exhaustive oracle only for candidate_count <= 12; minimum is for this unaligned fixed-fragment model"],
              "determinism": "ELF body order by section, start, size; aliases lexicographic; simultaneous widening; oracle masks increasing",
              "summary": summarize(all_functions), "objects": objects, "functions": all_functions}
    pathlib.Path(args.output).write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    print(json.dumps(output["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
