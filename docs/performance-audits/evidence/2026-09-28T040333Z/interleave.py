#!/usr/bin/env python3
# Interleaved timing of named commands: every round runs each command once, in
# order, so host drift lands on all rows alike. Reports min-max and median of
# wall, user+sys CPU and peak RSS (wait4 rusage of the one child).
# usage: interleave.py <rounds> <spec.json>   spec: [[label, [argv...]], ...]
import json, os, statistics, sys, time

def run(argv):
    t0 = time.perf_counter()
    pid = os.fork()
    if pid == 0:
        fd = os.open(os.devnull, os.O_WRONLY)
        os.dup2(fd, 1); os.dup2(fd, 2)
        os.execvp(argv[0], argv)
    _, status, ru = os.wait4(pid, 0)
    wall = time.perf_counter() - t0
    return wall, ru.ru_utime + ru.ru_stime, ru.ru_maxrss / 1024.0, os.waitstatus_to_exitcode(status)

rounds = int(sys.argv[1])
spec = json.load(open(sys.argv[2]))
results = {label: [] for label, _ in spec}
for _ in range(rounds):
    for label, argv in spec:
        results[label].append(run(argv))
print(f"{'row':44s} {'wall s (min-max, median)':>28s} {'cpu s median':>13s} {'peak RSS MB':>12s} exit")
for label, _ in spec:
    r = results[label]
    w = [x[0] for x in r]; c = [x[1] for x in r]; m = [x[2] for x in r]
    print(f"{label:44s} {min(w):7.3f}-{max(w):7.3f} ({statistics.median(w):7.3f}) {statistics.median(c):13.3f} {statistics.median(m):12.1f} {sorted({x[3] for x in r})}")
