#!/usr/bin/env python3
"""Callgrind complexity screen: per-function self Ir across geometric sizes.
usage: screen.py IDE FAMILY SIZES... [--flags "..."]
Writes results under screen/out/FAMILY/ and prints functions whose self-Ir
growth exponent (log2 of ratio between the two largest sizes / size ratio) is
>= 1.4 and whose share at the largest size is >= 1%."""
import os, subprocess, sys, math, re, json, shlex
HERE = os.path.dirname(os.path.abspath(__file__))
def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)
def parse_annotate(text):
    fns = {}
    total = None
    for line in text.splitlines():
        m = re.match(r"\s*([\d,]+)\s+\(\s*[\d.]+%\)\s+(.*)$", line)
        if m:
            ir = int(m.group(1).replace(",", ""))
            name = m.group(2).strip()
            if "PROGRAM TOTALS" in name:
                total = ir
                continue
            name = re.sub(r"^\S*:", "", name, count=1) if ":" in name and not name.startswith("0x") else name
            name = name.split(" [")[0]
            fns[name] = fns.get(name, 0) + ir
    return total, fns
def main():
    args = sys.argv[1:]
    flags = "-g0 -O0"
    if "--flags" in args:
        i = args.index("--flags"); flags = args[i+1]; del args[i:i+2]
    ide, fam, sizes = args[0], args[1], [int(x) for x in args[2:]]
    tag = fam + ("" if flags == "-g0 -O0" else "_" + re.sub(r"[^A-Za-z0-9]+", "", flags))
    out = os.path.join(HERE, "out", tag); os.makedirs(out, exist_ok=True)
    rows = []
    for n in sizes:
        src = os.path.join(out, f"{fam}_{n}.c")
        run([sys.executable, os.path.join(HERE, "screen_gen.py"), fam, str(n), src])
        cg = os.path.join(out, f"cg_{n}.out")
        cmd = ["valgrind", "--tool=callgrind", f"--callgrind-out-file={cg}", ide, "cc", *shlex.split(flags), "-c", src, "-o", os.path.join(out, f"{n}.o")]
        r = run(cmd, timeout=1800)
        ok = os.path.exists(os.path.join(out, f"{n}.o"))
        a = run(["callgrind_annotate", "--threshold=100", cg])
        total, fns = parse_annotate(a.stdout)
        if not ok:
            print(f"[{tag}] n={n} COMPILE FAILED: {r.stdout[-600:]}")
        rows.append((n, total, fns))
        print(f"[{tag}] n={n} total_Ir={total} srcbytes={os.path.getsize(src)} ok={ok}", flush=True)
    json.dump([(n, t, f) for n, t, f in rows], open(os.path.join(out, "rows.json"), "w"))
    (n1, t1, f1), (n2, t2, f2) = rows[-2], rows[-1]
    k = math.log(n2 / n1)
    print(f"[{tag}] total exponent {math.log(t2/t1)/k:.2f}")
    cand = []
    for name, ir2 in f2.items():
        ir1 = f1.get(name, 0)
        if ir2 < 0.01 * t2 or ir1 <= 0: continue
        e = math.log(ir2 / ir1) / k
        cand.append((e, ir2 / t2, name, ir1, ir2))
    cand.sort(reverse=True)
    for e, share, name, ir1, ir2 in cand[:12]:
        mark = "  <<<" if e >= 1.4 else ""
        print(f"   exp={e:5.2f} share={share*100:5.1f}% {name} ({ir1}->{ir2}){mark}")
if __name__ == "__main__":
    main()
