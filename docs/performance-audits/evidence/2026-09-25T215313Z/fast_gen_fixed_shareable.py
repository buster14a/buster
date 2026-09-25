#!/usr/bin/env python3
"""gen_fast3.py F B C OUT.c: F escaping aggregates first touched by a partial
store after the arms (fixed, upward-exposed), B plain conditional arms, and C
arms doing __int128 arithmetic (the selector's coalescible temporaries)."""
import sys
F, B, C, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
s = ["void use(int *);", "struct P { int a; int b; };", "int f(int a) {", "    int x = 0;", "    __int128 y = a;"]
s += [f"    struct P v{i};" for i in range(F)]
s += [f"    if (a > {k}) x += {k};" for k in range(B)]
s += [f"    if (a > {k}) y = y * {k + 3} / {k + 7};" for k in range(C)]
for i in range(F):
    s.append(f"    v{i}.a = a + {i};")
    s.append(f"    use(&v{i}.b);")
s += ["    return x + (int)(y >> 3);", "}"]
open(out, "w").write("\n".join(s) + "\n")
