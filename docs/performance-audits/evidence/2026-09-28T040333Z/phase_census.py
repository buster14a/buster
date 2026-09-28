#!/usr/bin/env python3
# Attribute rss_sampler.c dumps (glob) of one ide binary to compile phases.
# usage: phase_census.py '<dump glob>' <ide binary> [--by-run]
# Each sample goes to the first PHASES entry found anywhere on its stack, so
# nested entries are listed before the phase that contains them. CPU share is
# the sample share. Memory: within each dump (one process), samples are in
# time order; whenever resident pages exceed the running maximum, the increase
# is charged to the phase of that sample. The charges sum to the sampled peak
# less the resident size at the first sample; growth between two samples that
# straddle a phase boundary is charged to the later phase.
import bisect, collections, glob, struct, subprocess, sys

PHASES = [
    ("prewarm: x86 tables", ["machine_x86_64_exact_prewarm", "buster_x86_metadata_prewarm"]),
    ("preprocess", ["c_preprocess"]),
    ("parse", ["c_parse_ast"]),
    ("sema: lowering constraints", ["c_parse_validate_lowering_constraints"]),
    ("sema: other", ["c_analyze_semantics_core"]),
    ("lower to IR", ["c_lower_to_ir_with_options"]),
    ("IR prepare (FAST, verify)", ["ir_prepare_canonical_module"]),
    ("debug info (-g): locations, lines, DWARF model", ["codegen_record_machine_locations", "codegen_record_machine_line_marks", "debug_model_build", "dwarf_build_model", "object_append_dwarf", "dwarf_cfi_build"]),
    ("codegen: predicate placement", ["machine_predicate_placement_build"]),
    ("codegen: instruction selection", ["machine_select_canonical_function_internal"]),
    ("codegen: encoding", ["machine_encode_x86_64"]),
    ("codegen: other", ["codegen_generate_canonical_module_with_trace"]),
    ("object writing", ["object_from_canonical_codegen_module"]),
]
PAGE = 4096

def load_symbols(binary):
    syms = []
    out = subprocess.run(["nm", "-n", "--defined-only", binary], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split(maxsplit=2)
        if len(p) == 3 and p[1] in "tTwW":
            syms.append((int(p[0], 16), p[2]))
    syms.sort()
    return syms

def main():
    pattern, binary = sys.argv[1], sys.argv[2]
    syms = load_symbols(binary)
    addrs = [s[0] for s in syms]
    known = {name for _, names in PHASES for name in names}
    missing = sorted(n for n in known if n not in {s[1] for s in syms})
    cpu = collections.Counter(); mem = collections.Counter()
    total = 0; runs = 0; peak_sum = 0; first_sum = 0
    for path in sorted(glob.glob(pattern)):
        raw = open(path, "rb").read()
        we = raw.index(b"WORDS "); le = raw.index(b"\n", we)
        words = int(raw[we + 6:le])
        vals = struct.unpack(f"<{words}Q", raw[le + 1:le + 1 + words * 8])
        base = None
        for line in raw[:we].decode().splitlines():
            parts = line.split()
            if len(parts) >= 7 and parts[6].endswith("/ide") and parts[2] == "r--p" and parts[3] == "00000000":
                base = int(parts[1].split("-")[0], 16)
        cache = {}
        def name(a):
            n = cache.get(a)
            if n is None:
                r = a - base
                i = bisect.bisect_right(addrs, r) - 1
                n = syms[i][1] if i >= 0 and r - addrs[i] < (1 << 20) else "?"
                cache[a] = n
            return n
        runs += 1
        running = None
        i = 0
        while i < len(vals):
            depth = vals[i]; rss = vals[i + 1] * PAGE; frames = vals[i + 2:i + 2 + depth]; i += 2 + depth
            total += 1
            names = {name(a if k == 0 else a - 1) for k, a in enumerate(frames)}
            phase = "other (driver, startup, exit, kernel entry)"
            for label, roots in PHASES:
                if any(r in names for r in roots):
                    phase = label
                    break
            cpu[phase] += 1
            if running is None:
                running = rss; first_sum += rss
            elif rss > running:
                mem[phase] += rss - running
                running = rss
        peak_sum += running or 0
    if missing:
        print("note: symbols not in binary:", ", ".join(missing))
    print(f"runs={runs} samples={total} mean_sampled_peak={peak_sum / max(runs, 1) / 2**20:.1f}MB mean_first_sample_rss={first_sum / max(runs, 1) / 2**20:.1f}MB")
    order = [label for label, _ in PHASES] + ["other (driver, startup, exit, kernel entry)"]
    print(f"{'phase':46s} {'cpu%':>7s} {'peak growth MB/run':>19s}")
    for label in order:
        print(f"{label:46s} {100.0 * cpu[label] / max(total, 1):6.1f}% {mem[label] / max(runs, 1) / 2**20:19.1f}")

main()
