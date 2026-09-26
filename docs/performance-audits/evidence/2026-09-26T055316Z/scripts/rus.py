import os, resource, subprocess, sys, time
t=time.monotonic_ns()
r=subprocess.run(sys.argv[1:])
e=time.monotonic_ns()-t
u=resource.getrusage(resource.RUSAGE_CHILDREN)
sys.stderr.write(f"RUSAGE rc={r.returncode} wall_ns={e} user={u.ru_utime:.3f} sys={u.ru_stime:.3f} maxrss_kib={u.ru_maxrss} minflt={u.ru_minflt} majflt={u.ru_majflt}\n")
sys.exit(r.returncode)
