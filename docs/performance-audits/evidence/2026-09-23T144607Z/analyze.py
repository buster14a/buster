#!/usr/bin/env python3
"""Replay the #906 phase and self-cost tables from symbolized perf samples."""

import collections
import gzip
from pathlib import Path
import re


HERE = Path(__file__).resolve().parent
HEADER = re.compile(r"(\S+)\s+\d+/\d+\s+[\d.]+:\s*(\d+)\s+cycles:([uk]):")
FRAME = re.compile(r"\s*[0-9a-f]+\s+(.+?)\s+\(")
PHASE_ROOTS = {
    "semantic analysis": ("c_analyze_semantics_core",),
    "C-to-IR lowering": ("c_lower_to_ir_with_options",),
    "canonical code generation": ("codegen_generate_canonical_module_with_trace", "codegen_generate_canonical_module_attempt"),
    "IR preparation/validation": ("ir_prepare_canonical_module",),
    "preprocessing": ("c_preprocess",),
    "object/DWARF construction": ("object_from_canonical_codegen_module",),
    "AST parsing": ("c_parse_ast",),
}
CHECK_SYMBOLS = (
    "c_parse_validate_lowering_constraints",
    "c_ir_ssa_finish",
    "machine_fast_prepass_build",
    "machine_fast_sort_edits",
    "c_ir_decode_quoted",
    "machine_select_canonical_function_x86_64",
    "codegen_machine_debug_index_build",
)


def samples(path, event):
    with gzip.open(path, "rt") as source:
        for block in source.read().split("\n\n"):
            lines = block.splitlines()
            if not lines:
                continue
            header = HEADER.match(lines[0])
            if not header or header.group(3) != event or header.group(1) != "main_thread":
                continue
            frames = []
            for line in lines[1:]:
                frame = FRAME.match(line)
                if frame:
                    frames.append(frame.group(1).split("+")[0])
            if frames:
                yield int(header.group(2)), frames


def print_table(name, values, total, limit=None):
    print(name)
    ordered = sorted(values.items(), key=lambda item: item[1][0], reverse=True)
    for label, (period, count) in ordered[:limit]:
        print(f"{label:52} {100 * period / total:6.2f}% {count:5d} samples {period:12d} period")


user = list(samples(HERE / "cycles-stacks.txt.gz", "u"))
user_total = sum(period for period, _ in user)
assert len(user) == 2958 and user_total == 7808066308
phase = collections.defaultdict(lambda: [0, 0])
self_cost = collections.defaultdict(lambda: [0, 0])
inclusive = collections.defaultdict(lambda: [0, 0])
for period, frames in user:
    matches = [name for name, roots in PHASE_ROOTS.items() if any(root in frames for root in roots)]
    assert len(matches) <= 1, matches
    category = matches[0] if matches else "other compiler/driver"
    phase[category][0] += period
    phase[category][1] += 1
    self_cost[frames[0]][0] += period
    self_cost[frames[0]][1] += 1
    for symbol in set(frames):
        inclusive[symbol][0] += period
        inclusive[symbol][1] += 1
print(f"USER cycles:u main_thread: {len(user)} samples, {user_total} sampled period units")
print_table("PHASE (exclusive root categories, sampled user cycles)", phase, user_total)
print_table("SELF (uninlined leaf symbols; [unknown] is unattributed)", self_cost, user_total, 30)
print("SELECTED INCLUSIVE (nested; do not add percentages)")
for symbol in CHECK_SYMBOLS:
    period, count = inclusive[symbol]
    print(f"{symbol:52} {100 * period / user_total:6.2f}% {count:5d} samples {period:12d} period")

kernel = list(samples(HERE / "kernel-stacks.txt.gz", "k"))
kernel_total = sum(period for period, _ in kernel)
assert len(kernel) >= 100
kernel_self = collections.defaultdict(lambda: [0, 0])
for period, frames in kernel:
    kernel_self[frames[0]][0] += period
    kernel_self[frames[0]][1] += 1
print(f"KERNEL cycles:k main_thread: {len(kernel)} samples, {kernel_total} sampled period units")
print_table("KERNEL SELF (separate process-tree replay)", kernel_self, kernel_total, 12)
