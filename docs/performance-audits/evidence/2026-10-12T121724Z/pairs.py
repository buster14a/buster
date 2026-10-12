# Paired latency driver. Usage: python3 -I pairs.py BASELINE_IDE CANDIDATE_IDE WORK_DIR RAW_CSV
# Each cell: two warmup pairs (recorded, phase=warmup), then P measured pairs whose
# order alternates AB (even index) and BA (odd). One CSV row per process.
import csv, os, subprocess, sys, time
A, B, work, out = sys.argv[1:5]
os.chdir(work)
cells = []
for n in (256, 1024, 4096, 16384):
    for mode in ("-no-pie", "-pie"):
        cells.append((f"T={2*n} {mode} object", [mode, "main.o", f"m{n}.o"], 15))
for n in (1024, 16384):
    cells.append((f"T={2*n} -no-pie archive", ["-no-pie", "main.o", f"m{n}.a"], 15))
cells.append(("no-TLS plain.o -no-pie", ["-no-pie", "main.o", "plain.o"], 25))
cells.append(("no-TLS plain.o -pie", ["-pie", "main.o", "plain.o"], 25))
cells.append(("T=512 -no-pie object, repeat", ["-no-pie", "main.o", "m256.o"], 25))
def run(binary, args, target):
    start = time.perf_counter_ns()
    rc = subprocess.run(["taskset", "-c", "2", binary, "cc"] + args + ["-o", target]).returncode
    return time.perf_counter_ns() - start, rc
with open(out, "w", newline="") as f:
    w = csv.writer(f, lineterminator="\n")
    w.writerow(["cell", "phase", "pair", "position", "binary", "wall_ns", "returncode"])
    for name, args, pairs in cells:
        for phase, count in (("warmup", 2), ("measured", pairs)):
            for i in range(count):
                order = ("baseline", "candidate") if i % 2 == 0 else ("candidate", "baseline")
                for position, which in enumerate(order):
                    ns, rc = run(A if which == "baseline" else B, args, "lat-" + which)
                    w.writerow([name, phase, i, position, which, ns, rc])
        f.flush()
