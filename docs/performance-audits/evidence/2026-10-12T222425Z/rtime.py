import os,sys,time,resource,subprocess
t=time.monotonic(); r=subprocess.run(sys.argv[2:]); w=time.monotonic()-t
u=resource.getrusage(resource.RUSAGE_CHILDREN)
open(sys.argv[1],"w").write(f"wall={w:.2f}s user={u.ru_utime:.2f}s maxrss={u.ru_maxrss}KB exit={r.returncode}\n")
sys.exit(r.returncode)
