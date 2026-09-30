#!/usr/bin/env python3
# Minimal /usr/bin/time replacement: wall, user, sys, maxrss for a child command.
import os, sys, time, resource, subprocess
t0 = time.perf_counter()
p = subprocess.run(sys.argv[1:])
t1 = time.perf_counter()
ru = resource.getrusage(resource.RUSAGE_CHILDREN)
sys.stderr.write(f"RTIME wall={t1-t0:.4f}s user={ru.ru_utime:.4f}s sys={ru.ru_stime:.4f}s maxrss={ru.ru_maxrss/1024:.1f}MB minflt={ru.ru_minflt} exit={p.returncode}\n")
sys.exit(p.returncode)
