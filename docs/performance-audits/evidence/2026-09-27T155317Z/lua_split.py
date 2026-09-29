import subprocess, sys, time, os, glob, statistics
SP, L = sys.argv[1], sys.argv[2]
ide = "build/Release/ide"
flags = ["-g", "-DLUA_COMPAT_5_3", "-DLUA_USE_LINUX", f"-I{L}"]
def t(args, reps=5):
    best = 1e9
    for _ in range(reps):
        t0 = time.perf_counter(); p = subprocess.run([ide, "cc"] + args, capture_output=True); t1 = time.perf_counter()
        if p.returncode: return None
        best = min(best, t1 - t0)
    return best
def preamble(path):
    out = []; in_comment = False
    lines = open(path, encoding="latin-1").read().split("\n")
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if in_comment:
            out.append(lines[i]); in_comment = "*/" not in s; i += 1; continue
        if s == "" or s.startswith("//"):
            out.append(lines[i]); i += 1; continue
        if s.startswith("/*"):
            out.append(lines[i]); in_comment = "*/" not in s; i += 1; continue
        if s.startswith("#"):
            out.append(lines[i])
            while lines[i].rstrip().endswith("\\"):
                i += 1; out.append(lines[i])
            i += 1; continue
        break
    return "\n".join(out) + "\nint buster_preamble_marker;\n"
empty = f"{SP}/empty.c"
floor_c = t(["-c", empty, "-o", f"{SP}/e.o"]); floor_s = t(["-fsyntax-only", empty])
rows = []
for f in sorted(glob.glob(f"{L}/*.c")):
    b = os.path.basename(f)
    if b in ("onelua.c", "ltests.c"): continue
    pre = f"{SP}/pre_{b}"
    open(pre, "w").write(preamble(f))
    full_c = t(flags + ["-c", f, "-o", f"{SP}/x.o"])
    full_s = t(flags + ["-fsyntax-only", f])
    pre_s = t(flags + ["-fsyntax-only", pre])
    rows.append((b, full_c, full_s, pre_s))
print(f"floor: -c empty {floor_c*1000:.1f} ms, -fsyntax-only empty {floor_s*1000:.1f} ms")
tot = {"c": 0, "s": 0, "p": 0}
for b, c, s, p in rows:
    if None in (c, s, p):
        print(b, "failed", c, s, p); continue
    tot["c"] += c; tot["s"] += s; tot["p"] += p
n = sum(1 for r in rows if None not in r[1:])
C, S, P = tot["c"], tot["s"], tot["p"]
print(f"files={n} sum(-c)={C*1000:.0f} ms  sum(-fsyntax-only)={S*1000:.0f} ms  sum(preamble -fsyntax-only)={P*1000:.0f} ms")
print(f"  process+codegen floor  ~ {n*floor_c*1000:.0f} ms ({100*n*floor_c/C:.0f}%)")
print(f"  header preamble (net of syntax floor) ~ {(P - n*floor_s)*1000:.0f} ms ({100*(P - n*floor_s)/C:.0f}%)")
print(f"  own-code frontend ~ {(S - P)*1000:.0f} ms ({100*(S-P)/C:.0f}%)")
print(f"  backend+object (net of codegen floor) ~ {(C - S - n*(floor_c - floor_s))*1000:.0f} ms ({100*(C - S - n*(floor_c-floor_s))/C:.0f}%)")
