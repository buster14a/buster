#!/usr/bin/env python3
"""gen_fast.py F B OUT.c: one function with F aggregate locals whose address
escapes (fixed slots) and whose first touch is a partial field store placed
after B conditional arms (about 2 blocks each), so every fixed slot is
upward-exposed and live from entry. Deterministic; F and B vary independently."""
import sys
F, B, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
s = ["void use(int *);", "struct P { int a; int b; };", "int f(int a) {", "    int x = 0;"]
s += [f"    struct P v{i};" for i in range(F)]
s += [f"    if (a > {k}) x += {k};" for k in range(B)]
for i in range(F):
    s.append(f"    v{i}.a = a + {i};")
    s.append(f"    use(&v{i}.b);")
s += ["    return x;", "}"]
open(out, "w").write("\n".join(s) + "\n")
