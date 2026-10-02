#!/usr/bin/env python3
"""Disposable #1912 placement census injection; no timing evidence."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
args = parser.parse_args()
text = args.source.read_text()
population = """        u32 group_count = slot_blocks.count
"""
population_log = """        fprintf(stderr, "CACHE_SLOT_POPULATION n=%u s=%u k=%u coalesce=%u block_count=%u block_candidate=%u\\n",
                function->instruction_count, function->stack_slot_count, shareable_slots, (unsigned)coalesce_slots,
                function->block_count, (unsigned)block_color_slots);
"""
color = """    u32* order = arena_allocate(arena, u32, selected_count ? selected_count : 1);
"""
color_start = text.index("BUSTER_GLOBAL_LOCAL u32 machine_fast_color_slots(")
color_end = text.index("// A convex row interval", color_start)
region = text[color_start:color_end]
if text.count(population) != 1 or region.count(color) != 1:
    raise SystemExit("Exact expected callsite/colorer anchors are not unique")
region = region.replace(color, """    fprintf(stderr, "CACHE_SLOT_COLOR n=%u s=%u k=%u\\n",
            function->instruction_count, function->stack_slot_count, selected_count);
""" + color)
text = text[:color_start] + region + text[color_end:]
text = text.replace(population, population_log + population)
text = "#include <stdio.h>\n" + text
args.source.write_text(text)

