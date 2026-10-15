import os, sys, time, subprocess, json
# usage: rus.py OUTJSON STDOUT STDERR -- cmd...
out, so, se = sys.argv[1:4]; cmd = sys.argv[5:]
with open(so, "wb") as fo, open(se, "wb") as fe:
    t0 = time.perf_counter_ns()
    p = subprocess.Popen(cmd, stdout=fo, stderr=fe)
    _, status, ru = os.wait4(p.pid, 0)
    t1 = time.perf_counter_ns()
r = dict(wall_ns=t1-t0, status=status, utime=ru.ru_utime, stime=ru.ru_stime, maxrss_kb=ru.ru_maxrss,
         minflt=ru.ru_minflt, majflt=ru.ru_majflt, nvcsw=ru.ru_nvcsw, nivcsw=ru.ru_nivcsw)
open(out, "w").write(json.dumps(r) + "\n")
print(json.dumps(r))
