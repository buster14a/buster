import os, sys, resource
pid = os.fork()
if pid == 0:
    os.execv(sys.argv[1], sys.argv[1:])
_, status, usage = os.wait4(pid, 0)
print(f"exit={os.waitstatus_to_exitcode(status)} maxrss_kb={usage.ru_maxrss}", file=sys.stderr)
