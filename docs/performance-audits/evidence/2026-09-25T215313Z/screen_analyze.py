#!/usr/bin/env python3
import os, re, subprocess, sys, math, glob
def fn_table(cg):
    a = subprocess.run(["callgrind_annotate", "--auto=no", "--threshold=100", cg], stdout=subprocess.PIPE, text=True).stdout
    fns = {}; total = None; on = False
    for line in a.splitlines():
        if "PROGRAM TOTALS" in line:
            total = int(line.split()[0].replace(",", ""))
        if "file:function" in line: on = True; continue
        if not on: continue
        m = re.match(r"\s*([\d,]+)\s+\(\s*[\d.]+%\)\s+(\S+)", line)
        if m:
            name = m.group(2).rsplit(":", 1)[-1]
            fns[name] = fns.get(name, 0) + int(m.group(1).replace(",", ""))
    return total, fns
def main():
    d = sys.argv[1]
    files = sorted(glob.glob(os.path.join(d, "cg_*.out")), key=lambda p: int(re.findall(r"cg_(\d+)", p)[0]))
    rows = [r for r in ((int(re.findall(r"cg_(\d+)", p)[0]),) + fn_table(p) for p in files) if r[1]]
    print(os.path.basename(d), " ".join(f"n={n}:{t/1e6:.0f}M" for n, t, _ in rows))
    if len(rows) < 2: return
    (n1, t1, f1), (n2, t2, f2) = rows[-2], rows[-1]
    k = math.log(n2 / n1)
    growth = t2 - t1
    out = []
    for name, ir2 in f2.items():
        ir1 = f1.get(name, 0)
        delta = ir2 - ir1
        if delta < 0.02 * growth or ir1 <= 0: continue
        out.append((delta / growth, math.log(ir2 / ir1) / k, name, ir1, ir2))
    out.sort(reverse=True)
    for share, e, name, ir1, ir2 in out[:8]:
        print(f"   growthshare={share*100:5.1f}% exp={e:4.2f} {name} {ir1/1e6:.1f}M->{ir2/1e6:.1f}M" + ("  <<<" if e >= 1.3 else ""))
main()
