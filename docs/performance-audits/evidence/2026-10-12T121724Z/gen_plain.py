# Writes plain.c: the no-TLS control, 4096 globals and 4096 functions. Usage: gen_plain.py OUT
import sys
with open(sys.argv[1], "w") as f:
    for i in range(4096):
        f.write(f"long g{i}={i}; long f{i}(long x){{return x+g{i};}}\n")
    f.write("long sum_all(void){long s=0;\n" + "".join(f"s=f{i}(s);\n" for i in range(4096)) + "return s;}\n")
