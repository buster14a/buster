#!/usr/bin/env python3
# Paired A/B/A-A compile measurement. Each iteration runs the three compilers in a
# rotated order (base, cand, base_aa) under `perf stat`, records wall, user/sys,
# max RSS (wait4 rusage), cycles:u and instructions:u, and writes raw rows to CSV.
import os, sys, time, subprocess, statistics, csv, shutil

repo = "/home/david/dev/devin/buster"
debug = sys.argv[1]            # "-g" or "-g0"
iterations = int(sys.argv[2])
out_csv = sys.argv[3]
binaries = {
    "base": "/tmp/ide-baseline",
    "cand": os.path.join(repo, "build/Release/ide"),
    "base_aa": "/tmp/ide-baseline-aa",
}
if not os.path.exists(binaries["base_aa"]):
    shutil.copy2(binaries["base"], binaries["base_aa"])
common = ["cc", "-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0",
          debug, "src/buster/apps/ide/ide.c", "-lm"]
rows = []
order = ["base", "cand", "base_aa"]
for it in range(iterations):
    for k in range(3):
        name = order[(it + k) % 3]
        out = f"/tmp/ab/out-{name}{debug}"
        stat = f"/tmp/ab/stat-{name}.txt"
        argv = ["perf", "stat", "-x,", "-e", "cycles:u,instructions:u", "-o", stat,
                binaries[name]] + common + ["-o", out]
        t0 = time.perf_counter()
        pid = os.fork()
        if pid == 0:
            os.chdir(repo)
            with open(os.devnull, "wb") as devnull:
                os.dup2(devnull.fileno(), 1)
                os.dup2(devnull.fileno(), 2)
            os.execvp(argv[0], argv)
        _, status, ru = os.wait4(pid, 0)
        wall = time.perf_counter() - t0
        assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, (name, status)
        counters = {}
        with open(stat) as f:
            for line in f:
                parts = line.strip().split(",")
                if len(parts) >= 3 and parts[2] in ("cycles:u", "instructions:u"):
                    counters[parts[2]] = int(parts[0])
        rows.append({"iteration": it, "binary": name, "wall_s": round(wall, 4),
                     "user_s": round(ru.ru_utime, 4), "sys_s": round(ru.ru_stime, 4),
                     "maxrss_kb": ru.ru_maxrss, "cycles_u": counters["cycles:u"],
                     "instructions_u": counters["instructions:u"]})
        print(rows[-1], flush=True)
with open(out_csv, "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

def summarize(metric):
    print(f"\n== {metric} ({debug}) ==")
    per = {n: [r[metric] for r in rows if r["binary"] == n] for n in order}
    for n in order:
        v = per[n]
        print(f"  {n:8s} min={min(v):>14} median={statistics.median(v):>16} mean={statistics.mean(v):>16.1f} max={max(v):>14}")
    def paired(a, b):
        d = [(x - y) / y for x, y in zip(per[a], per[b])]
        return statistics.median(d), min(d), max(d)
    m, lo, hi = paired("cand", "base")
    print(f"  cand vs base   : median {m*100:+.2f}%  (range {lo*100:+.2f}% .. {hi*100:+.2f}%)")
    m, lo, hi = paired("base_aa", "base")
    print(f"  base_aa vs base: median {m*100:+.2f}%  (range {lo*100:+.2f}% .. {hi*100:+.2f}%)  [A/A noise floor]")

for metric in ("instructions_u", "cycles_u", "wall_s", "user_s", "maxrss_kb"):
    summarize(metric)
