# Writes m<N>.c: N global __thread longs, one function reading and incrementing
# each (two general-dynamic TLS sites per variable). Usage: gen_tls.py N OUT
import sys
n = int(sys.argv[1])
with open(sys.argv[2], "w") as f:
    for i in range(n):
        f.write(f"__thread long v{i} = {i};\n")
    f.write("long sum_all(void){ long s=0;\n")
    for i in range(n):
        f.write(f" s += v{i}; v{i} += 1;\n")
    f.write(" return s; }\n")
