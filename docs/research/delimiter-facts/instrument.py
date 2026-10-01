#!/usr/bin/env python3
"""Disposable diagnostic overlay; never used for timing binaries."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
parse_path = root / "src/buster/lib/compiler/frontend/c/c_parse.c"
ledger_path = root / "src/buster/lib/compiler/work_ledger.h"
source = parse_path.read_text()
ledger = ledger_path.read_text()
counters = [
    ("DELIMITER_PAIRS", "pairs"),
    ("DELIMITER_REVERSE_STORES", "reverse_stores"),
    ("DELIMITER_BACKWARD_READS", "backward_reads"),
    ("DELIMITER_INVERSE_MAPS", "inverse_maps"),
    ("DELIMITER_INVERSE_TOKENS", "inverse_tokens"),
    ("DELIMITER_INVERSE_CLEAR_BYTES", "inverse_clear_bytes"),
    ("DELIMITER_INVERSE_REGION_NS", "inverse_region_ns"),
    ("DELIMITER_INVERSE_ALLOCATION_NS", "inverse_allocation_ns"),
]
addition = "".join("    X(" + name + ", delimiter, " + key + ") \\\n" for name, key in counters)
anchor = "#define WORK_LEDGER_COUNTERS(X) \\\n"
assert ledger.count(anchor) == 1
ledger = ledger.replace(anchor, anchor + addition)
source = source.replace('#include "c_internal.h"', '#include "c_internal.h"\n#include <buster/lib/time.h>', 1)
pair = re.compile(r"(        index->matching_delimiters_plus_one\[(?:stack\[--\*stack_count\]\.position|open)\] = token_index \+ 1;)")
source, pair_count = pair.subn(r"\1\n        WORK_LEDGER_RECORD(DELIMITER_PAIRS, 1);", source)
assert pair_count == 1, pair_count
reverse = "        index->matching_delimiters_plus_one[token_index] = open + 1;"
if reverse in source:
    source = source.replace(reverse, reverse + "\n        WORK_LEDGER_RECORD(DELIMITER_REVERSE_STORES, 1);", 1)
    anchor = "BUSTER_C_INTERNAL u32 c_parse_matching_opener_indexed(CParseResult* result, CPreprocessResult preprocess, u32 close, u32 start)\n{"
    assert source.count(anchor) == 1
    source = source.replace(anchor, anchor + "\n    WORK_LEDGER_RECORD(DELIMITER_BACKWARD_READS, 1);", 1)
allocations = re.compile(r"^([ ]*)((?:u32\* )?openers = arena_allocate\(machine->scratch_arena, u32, end - start\);)$", re.M)
source, allocation_count = allocations.subn(
    lambda m: m[1] + "TimeDataType inverse_allocation_start = timestamp_take();\n" + m[0] + "\n" +
              m[1] + "WORK_LEDGER_RECORD(DELIMITER_INVERSE_ALLOCATION_NS, timestamp_ns_between(inverse_allocation_start, timestamp_take()));",
    source)
cursor = 0
regions = 0
while True:
    match = re.search(r"^([ ]*)memset\(openers, 0xff, sizeof\(\*openers\) \* \(end - start\)\);", source[cursor:], re.M)
    if not match:
        break
    start = cursor + match.start()
    indent = match[1]
    loop_end = source.index("\n" + indent + "}\n", start) + len("\n" + indent + "}")
    old = source[start:loop_end]
    before = (indent + "TimeDataType inverse_region_start = timestamp_take();\n" +
              indent + "WORK_LEDGER_RECORD(DELIMITER_INVERSE_MAPS, 1);\n" +
              indent + "WORK_LEDGER_RECORD(DELIMITER_INVERSE_TOKENS, end - start);\n" +
              indent + "WORK_LEDGER_RECORD(DELIMITER_INVERSE_CLEAR_BYTES, sizeof(*openers) * (end - start));\n")
    after = "\n" + indent + "WORK_LEDGER_RECORD(DELIMITER_INVERSE_REGION_NS, timestamp_ns_between(inverse_region_start, timestamp_take()));"
    new = before + old + after
    source = source[:start] + new + source[loop_end:]
    cursor = start + len(new)
    regions += 1
assert allocation_count == regions and regions in (0, 3), (allocation_count, regions)
parse_path.write_text(source)
ledger_path.write_text(ledger)
print("diagnostic overlay:", regions, "inverse-map sites")
