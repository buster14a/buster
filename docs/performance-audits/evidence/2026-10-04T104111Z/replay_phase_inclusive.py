#!/usr/bin/env python3
"""Per-phase inclusive/self attribution over the retained #906 cycles:u stacks
(docs/performance-audits/evidence/2026-09-23T144607Z/cycles-stacks.txt.gz).

usage: replay_phase_inclusive.py <phase> [limit] [--within SYMBOL]
  phase: semantic | lowering | codegen | prepare | preprocess | object | parse
  --within SYMBOL restricts the sample set to stacks containing SYMBOL and
  additionally prints SYMBOL's direct callees (the frame just below it).
Phase roots are those of that audit's analyze.py; each sample lands in at most
one phase. Shares are of the whole user profile and of the selected set.
"""
import collections
import gzip
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
STACKS = HERE.parent / "2026-09-23T144607Z" / "cycles-stacks.txt.gz"
HEADER = re.compile(r"(\S+)\s+\d+/\d+\s+[\d.]+:\s*(\d+)\s+cycles:([uk]):")
FRAME = re.compile(r"\s*[0-9a-f]+\s+(.+?)\s+\(")
PHASE_ROOTS = {
    "semantic": ("c_analyze_semantics_core",),
    "lowering": ("c_lower_to_ir_with_options",),
    "codegen": ("codegen_generate_canonical_module_with_trace", "codegen_generate_canonical_module_attempt"),
    "prepare": ("ir_prepare_canonical_module",),
    "preprocess": ("c_preprocess",),
    "object": ("object_from_canonical_codegen_module",),
    "parse": ("c_parse_ast",),
}


def samples(path, event):
    with gzip.open(path, "rt") as source:
        for block in source.read().split("\n\n"):
            lines = block.splitlines()
            if not lines:
                continue
            header = HEADER.match(lines[0])
            if not header or header.group(3) != event or header.group(1) != "main_thread":
                continue
            frames = [m.group(1).split("+")[0] for m in map(FRAME.match, lines[1:]) if m]
            if frames:
                yield int(header.group(2)), frames


args = [a for a in sys.argv[1:] if not a.startswith("--")]
within = sys.argv[sys.argv.index("--within") + 1] if "--within" in sys.argv else None
phase = args[0] if args else "semantic"
limit = int(args[1]) if len(args) > 1 else 60
user = list(samples(STACKS, "u"))
total = sum(p for p, _ in user)
assert len(user) == 2958 and total == 7808066308, (len(user), total)

selected = 0
incl = collections.defaultdict(lambda: [0, 0])
selfc = collections.defaultdict(lambda: [0, 0])
callees = collections.defaultdict(lambda: [0, 0])
for period, frames in user:
    category = None
    for name, roots in PHASE_ROOTS.items():
        if any(root in frames for root in roots):
            category = name
    if category != phase or (within and within not in frames):
        continue
    selected += period
    selfc[frames[0]][0] += period
    selfc[frames[0]][1] += 1
    for symbol in set(frames):
        incl[symbol][0] += period
        incl[symbol][1] += 1
    if within:
        position = frames.index(within)
        child = frames[position - 1] if position > 0 else "<self>"
        callees[child][0] += period
        callees[child][1] += 1

print(f"phase={phase} within={within} share_of_user={100 * selected / total:.2f}% period={selected}")
if within:
    print("--- DIRECT CALLEES (share of user / share of selected / samples) ---")
    for symbol, (p, n) in sorted(callees.items(), key=lambda kv: -kv[1][0])[:limit]:
        print(f"{symbol:58} {100 * p / total:6.2f}% {100 * p / selected:6.2f}% {n:5d}")
print("--- INCLUSIVE (share of user / share of selected / samples) ---")
for symbol, (p, n) in sorted(incl.items(), key=lambda kv: -kv[1][0])[:limit]:
    print(f"{symbol:58} {100 * p / total:6.2f}% {100 * p / selected:6.2f}% {n:5d}")
print("--- SELF ---")
for symbol, (p, n) in sorted(selfc.items(), key=lambda kv: -kv[1][0])[:limit]:
    print(f"{symbol:58} {100 * p / total:6.2f}% {100 * p / selected:6.2f}% {n:5d}")
