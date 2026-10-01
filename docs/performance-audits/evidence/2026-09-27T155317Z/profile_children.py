#!/usr/bin/env python3
# For each sample containing PARENT, attribute to the frame directly below PARENT (its callee).
import bisect, subprocess, sys, struct, collections, glob
pattern, binary, parent = sys.argv[1], sys.argv[2], sys.argv[3]
syms = []
for line in subprocess.run(["nm", "-n", "--defined-only", binary], capture_output=True, text=True).stdout.splitlines():
    p = line.split(maxsplit=2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
syms.sort(); addrs = [s[0] for s in syms]
child = collections.Counter(); total = 0; with_parent = 0
for dump_path in sorted(glob.glob(pattern)):
    raw = open(dump_path, "rb").read()
    he = raw.index(b"WORDS "); le = raw.index(b"\n", he); words = int(raw[he + 6:le])
    vals = struct.unpack(f"<{words}Q", raw[le + 1:le + 1 + words * 8])
    base = None
    for line in raw[:he].decode().splitlines():
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
    i = 0
    while i < len(vals):
        d = vals[i]; frames = vals[i + 1:i + 1 + d]; i += 1 + d; total += 1
        names = [name(a if k == 0 else a - 1) for k, a in enumerate(frames)]
        if parent in names:
            with_parent += 1
            j = len(names) - 1 - names[::-1].index(parent)  # outermost occurrence
            child[names[j - 1] if j > 0 else "<self>"] += 1
print(f"samples={total} under {parent}: {with_parent} ({100.0*with_parent/total:.2f}%)")
for n, c in child.most_common(25):
    print(f"{100.0*c/total:6.2f}% of total  {n}")
