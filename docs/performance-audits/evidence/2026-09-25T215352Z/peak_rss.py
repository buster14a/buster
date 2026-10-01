import resource, subprocess, sys, os
def peak(cmd, cwd):
    pid = os.fork()
    if pid == 0:
        os.chdir(cwd)
        fd = os.open(os.devnull, os.O_WRONLY); os.dup2(fd, 1); os.dup2(fd, 2)
        os.execv(cmd[0], cmd)
    _, status, ru = os.wait4(pid, 0)
    return ru.ru_maxrss, os.waitstatus_to_exitcode(status)
repo = "/home/user/buster"
NU = ["-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=0", "-DBUSTER_INCLUDE_TESTS=0"]
cases = {
 "tiny_c": ["cc", "-c", "/tmp/claude-0/ledger/fam/tiny.c", "-o", "/tmp/claude-0/ledger/rss.o"],
 "hello_link": ["cc", "/tmp/claude-0/ledger/fam/hello.c", "-o", "/tmp/claude-0/ledger/rss.out"],
 "ops_c": ["cc", "-c", "tests/basic_c_operations.c", "-o", "/tmp/claude-0/ledger/rss.o"],
 "string_c": ["cc", *NU, "-c", "src/buster/lib/string.c", "-o", "/tmp/claude-0/ledger/rss.o"],
 "stage1": ["cc", "-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g", "src/buster/apps/ide/ide.c", "-lm", "-o", "/tmp/claude-0/ledger/rss.stage1"],
}
bins = {"base": "/tmp/claude-0/frozen/ide-base", "cand": "/tmp/claude-0/frozen/ide-cand"}
reps = int(sys.argv[1]) if len(sys.argv) > 1 else 5
print("case\tbinary\tmaxrss_kib(min..max over %d runs)\trc" % reps)
for name, args in cases.items():
    for tag, b in bins.items():
        vals = []; rcs = set()
        for _ in range(reps if name != "stage1" else 2):
            r, rc = peak([b] + args, repo); vals.append(r); rcs.add(rc)
        print(f"{name}\t{tag}\t{min(vals)}..{max(vals)}\t{sorted(rcs)}")
