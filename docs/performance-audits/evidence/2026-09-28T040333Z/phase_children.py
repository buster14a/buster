#!/usr/bin/env python3
# rss_sampler.c dumps: for samples under PARENT, share of total CPU by the
# direct callee below PARENT's outermost frame, top exclusive symbols under
# PARENT, and the peak-growth MB/run charged to each callee (as phase_census.py).
# usage: phase_children.py '<dump glob>' <ide binary> <PARENT>
import bisect, collections, glob, struct, subprocess, sys
pattern, binary, parent = sys.argv[1], sys.argv[2], sys.argv[3]
focus = sys.argv[4:]; focus_hits = collections.Counter()
syms = []
for line in subprocess.run(["nm", "-n", "--defined-only", binary], capture_output=True, text=True).stdout.splitlines():
    p = line.split(maxsplit=2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
syms.sort(); addrs = [s[0] for s in syms]
child = collections.Counter(); excl = collections.Counter(); mem = collections.Counter()
total = 0; under = 0; runs = 0
for path in sorted(glob.glob(pattern)):
    raw = open(path, "rb").read()
    we = raw.index(b"WORDS "); le = raw.index(b"\n", we); words = int(raw[we + 6:le])
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
            r = a - base; i = bisect.bisect_right(addrs, r) - 1
            n = syms[i][1] if i >= 0 and r - addrs[i] < (1 << 20) else "?"
            cache[a] = n
        return n
    runs += 1; running = None; i = 0
    while i < len(vals):
        d = vals[i]; rss = vals[i + 1] * 4096; frames = vals[i + 2:i + 2 + d]; i += 2 + d; total += 1
        names = [name(a if k == 0 else a - 1) for k, a in enumerate(frames)]
        grow = 0
        if running is None: running = rss
        elif rss > running: grow = rss - running; running = rss
        if parent in names:
            under += 1
            j = len(names) - 1 - names[::-1].index(parent)
            c = names[j - 1] if j > 0 else "<self>"
            child[c] += 1; excl[names[0]] += 1; mem[c] += grow
            below = set(names[:j])
            for f in focus:
                if f in below: focus_hits[f] += 1
            if focus and any(f in below for f in focus): focus_hits["<any focus>"] += 1
print(f"samples={total} runs={runs} under {parent}: {100.0*under/total:.1f}%")
print("-- direct callees (% of total CPU, peak growth MB/run)")
for n, c in child.most_common(18):
    print(f"{100.0*c/total:6.2f}% {mem[n]/runs/2**20:7.1f}MB  {n}")
print("-- top exclusive under parent (% of total CPU)")
for n, c in excl.most_common(15):
    print(f"{100.0*c/total:6.2f}%  {n}")
for f in focus + (["<any focus>"] if focus else []):
    print(f"FOCUS {100.0*focus_hits[f]/total:6.2f}% of total, inclusive below {parent}: {f}")
