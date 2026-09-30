#!/usr/bin/env python3
# Lua 5.4.8 per-file build, one process per file, serial; TinyCC and Buster
# rounds interleaved. Reports total wall and summed child CPU per round, and
# the median over files of each file's peak RSS.
# usage: lua_perfile.py <rounds> <lua dir> <ide> <tccdrv> <libtcc.so> <tcc rtlib> <outdir>
import os, statistics, sys, time
rounds, L, ide, drv, lib, rt, out = int(sys.argv[1]), *sys.argv[2:8]
files = sorted(f for f in os.listdir(L) if f.endswith(".c") and f not in ("onelua.c", "ltests.c"))
defs = ["-DLUA_COMPAT_5_3", "-DLUA_USE_LINUX"]
def run(argv):
    pid = os.fork()
    if pid == 0:
        os.chdir(L)
        fd = os.open(os.devnull, os.O_WRONLY); os.dup2(fd, 1); os.dup2(fd, 2)
        os.execv(argv[0], argv)
    _, st, ru = os.wait4(pid, 0)
    return ru.ru_utime + ru.ru_stime, ru.ru_maxrss / 1024.0, os.waitstatus_to_exitcode(st)
def build(kind):
    t0 = time.perf_counter(); cpu = 0.0; rss = []; bad = 0
    for f in files:
        o = os.path.join(out, kind + "_" + f[:-2] + ".o")
        argv = [ide, "cc", "-c", "-g"] + defs + [f, "-o", o] if kind == "buster" else [drv, lib, rt, o, "-g", f] + defs
        c, m, e = run(argv); cpu += c; rss.append(m); bad += e != 0
    return time.perf_counter() - t0, cpu, statistics.median(rss), max(rss), bad
res = {"tcc": [], "buster": []}
for _ in range(rounds):
    for k in ("tcc", "buster"):
        res[k].append(build(k))
print(f"files={len(files)}")
for k, r in res.items():
    w = [x[0] for x in r]; c = [x[1] for x in r]
    print(f"{k:7s} wall {min(w):.3f}-{max(w):.3f} (median {statistics.median(w):.3f}) s; cpu median {statistics.median(c):.3f} s; per-file peak RSS median {statistics.median(x[2] for x in r):.1f} MB, max {max(x[3] for x in r):.1f} MB; failures {sum(x[4] for x in r)}")
