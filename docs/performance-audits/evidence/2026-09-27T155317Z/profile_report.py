#!/usr/bin/env python3
# Symbolize sampler dumps (glob) against the ide binary; print inclusive/exclusive tables.
import bisect, subprocess, sys, struct, collections, glob
pattern, binary = sys.argv[1], sys.argv[2]
focus = sys.argv[3:]
syms = []
for line in subprocess.run(["nm", "-n", "--defined-only", binary], capture_output=True, text=True).stdout.splitlines():
    p = line.split(maxsplit=2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [s[0] for s in syms]
excl = collections.Counter(); incl = collections.Counter(); total = 0
for dump_path in sorted(glob.glob(pattern)):
    raw = open(dump_path, "rb").read()
    header_end = raw.index(b"WORDS ")
    line_end = raw.index(b"\n", header_end)
    words = int(raw[header_end + 6:line_end])
    vals = struct.unpack(f"<{words}Q", raw[line_end + 1:line_end + 1 + words * 8])
    base = None
    for line in raw[:header_end].decode().splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[6].endswith("/ide") and parts[2] == "r--p" and parts[3] == "00000000":
            base = int(parts[1].split("-")[0], 16)
    if base is None:
        for line in raw[:header_end].decode().splitlines():
            parts = line.split()
            if len(parts) >= 7 and parts[6].endswith("/ide"):
                base = int(parts[1].split("-")[0], 16) - int(parts[3], 16); break
    cache = {}
    def name(a):
        n = cache.get(a)
        if n is None:
            r = a - base
            i = bisect.bisect_right(addrs, r) - 1
            n = syms[i][1] if i >= 0 and r - addrs[i] < (1 << 20) else "?"
            cache[a] = n
        return n
    i = 0
    while i < len(vals):
        d = vals[i]; frames = vals[i + 1:i + 1 + d]; i += 1 + d
        total += 1
        names = [name(a if k == 0 else a - 1) for k, a in enumerate(frames)]
        excl[names[0]] += 1
        for n in set(names):
            incl[n] += 1
print(f"samples={total}")
if focus:
    for f in focus:
        print(f"INCL {100.0*incl[f]/max(total,1):6.2f}%  {f}")
else:
    print("-- top inclusive")
    for n, c in incl.most_common(70):
        print(f"{100.0*c/total:6.2f}%  {n}")
    print("-- top exclusive")
    for n, c in excl.most_common(45):
        print(f"{100.0*c/total:6.2f}%  {n}")
