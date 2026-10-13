import re, sys
def per_line(p):
    out = {}; f = None; n = 0
    for line in open(p):
        if line.startswith("-- Auto-annotated source:") or line.startswith("-- User-annotated source:"):
            f = line.split(":", 1)[1].strip(); n = 1; continue
        m = re.match(r"^-- line (\d+) -", line)
        if m: n = int(m.group(1)); continue
        if f is None or line.startswith("--") or line.startswith("====="): continue
        toks = line.split(None, 9)
        if len(toks) >= 9 and all(re.fullmatch(r"[\d,]+|\.", t) for t in toks[:9]):
            txt = toks[9].rstrip() if len(toks) > 9 else ""
            if txt.startswith("=>") or txt.startswith("<="): continue  # call lines (inclusive)
            out[(f, n)] = ([0 if t == "." else int(t.replace(",", "")) for t in toks[:9]], txt)
            n += 1
    return out
a, b = per_line(sys.argv[1]), per_line(sys.argv[2]); want = sys.argv[3]; top = int(sys.argv[4])
rows = []
for k, (vb, txt) in b.items():
    if not k[0].endswith(want): continue
    va = a.get(k, ([0]*9, ""))[0]
    rows.append(((vb[7]+vb[8]) - (va[7]+va[8]), vb[0]-va[0], (vb[4]+vb[5])-(va[4]+va[5]), va[7]+va[8], k[1], txt))
print("%s: self-line sum dLLd=%d dIr=%d dD1m=%d" % (want, sum(r[0] for r in rows), sum(r[1] for r in rows), sum(r[2] for r in rows)))
rows.sort(key=lambda r: -r[0])
print(f"{'dLLd':>10s} {'dIr':>12s} {'dD1m':>10s} {'A LLd':>10s} line")
for r in rows[:top]: print(f"{r[0]:10,d} {r[1]:12,d} {r[2]:10,d} {r[3]:10,d} {r[4]:5d} {r[5].strip()[:90]}")
