#!/usr/bin/env python3
"""Generate observer-only source variants into diagnostic clones, never main."""
import argparse
import difflib
import hashlib
from pathlib import Path

SOURCE_SHA256 = "6e5c12ba16cb14b238794e9baab73444c5833e1605b69aa3f8c6e0781c879243"
SOURCE_PATH = "src/buster/lib/compiler/codegen/register_allocator_quality.c"


def diagnostic_source(source, variant):
    if hashlib.sha256(source.encode()).hexdigest() != SOURCE_SHA256:
        raise ValueError("source differs from frozen 8f67df736f13d4edc055110a7a6d619a00a22eaf")
    include = "#include <buster/lib/compiler/codegen/register_allocator_quality_internal.h>\n"
    source = source.replace(include, include + "#include <buster/lib/string.h>\n\nBUSTER_CT_CHECK(sizeof(MachineQualityInterval) == 24);\n", 1)
    anchor = "    if (!baseline.valid)\n    {\n        scratch_end(scratch);\n        return baseline;\n    }\n"
    eligible = "(baseline.valid && !baseline.spill_count && !baseline.reload_count)"
    skip = eligible if variant == "candidate" else "0"
    observe = (
        "    // Isolated diagnostic record: requested payload, not RSS or timing.\n"
        "    string_print(S8(\"EFFORT_DIAGNOSTIC variant=" + variant + " rows={u32} values={u32} valid={u32} spills={u32} reloads={u32} eligible={u32} skip={u32} avoided_payload={u64} interval_size={u32}\\n\"),\n"
        "                 function->instruction_count, register_count, (u32)baseline.valid, baseline.spill_count, baseline.reload_count,\n"
        "                 (u32)" + eligible + ", (u32)" + skip + ",\n"
        "                 " + skip + " ? 4ULL * BUSTER_MAX(function->instruction_count, 1u) + 16ULL * BUSTER_MAX(register_count, 1u) + 24ULL * MACHINE_QUALITY_MAXIMUM_CANDIDATES : 0ULL,\n"
        "                 (u32)sizeof(MachineQualityInterval));\n"
    )
    changed = anchor
    if variant == "candidate":
        changed = anchor.replace("if (!baseline.valid)", "if (!baseline.valid || (!baseline.spill_count && !baseline.reload_count))").replace("        scratch_end(scratch);", "        if (baseline.valid) BUSTER_QUALITY_COUNT(empty_candidate_functions, 1);\n        scratch_end(scratch);")
    if source.count(anchor) != 1:
        raise ValueError("baseline placement anchor missing or ambiguous")
    source = source.replace(anchor, observe + changed, 1)
    heap_anchor = "    BUSTER_QUALITY_COUNT(candidates, heap_count);\n"
    heap_observe = (
        "    string_print(S8(\"EFFORT_HEAP rows={u32} values={u32} eligible={u32} candidates={u32}\\n\"),\n"
        "                 function->instruction_count, register_count, (u32)" + eligible + ", heap_count);\n"
    )
    if source.count(heap_anchor) != 1:
        raise ValueError("heap anchor missing or ambiguous")
    source = source.replace(heap_anchor, heap_observe + heap_anchor, 1)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--variant", choices=("baseline", "candidate"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--diff", action="store_true")
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("output must differ from source; patch only an isolated clone")
    source = args.source.read_text()
    result = diagnostic_source(source, args.variant)
    if args.diff:
        result = "".join(difflib.unified_diff(source.splitlines(True), result.splitlines(True), fromfile="a/" + SOURCE_PATH, tofile="b/" + SOURCE_PATH))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)


if __name__ == "__main__":
    main()
