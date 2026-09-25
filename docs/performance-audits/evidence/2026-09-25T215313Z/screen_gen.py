#!/usr/bin/env python3
"""Deterministic geometric C input families for complexity screening.
usage: gen.py FAMILY N OUT.c   (no randomness; N is the varied dimension)"""
import sys

def fam_globals_refs(n):
    # N globals, one function referencing each once (refs grow with N too)
    s = [f"int g{i};" for i in range(n)]
    s.append("int f(void) { int x = 0;")
    s += [f"  x += g{i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_globals_fixedrefs(n):
    # N unused globals + fixed 64 refs to the LAST global
    s = [f"int g{i};" for i in range(n)]
    s.append("int f(void) { int x = 0;")
    s += [f"  x += g{n-1};" for _ in range(64)]
    s.append("  return x; }")
    return s

def fam_functions(n):
    # N small functions each calling the previous one
    s = ["int f0(int a) { return a + 1; }"]
    s += [f"int f{i}(int a) {{ return f{i-1}(a) + {i}; }}" for i in range(1, n)]
    return s

def fam_extern_calls(n):
    # one function calling N distinct extern functions
    s = [f"int e{i}(int);" for i in range(n)]
    s.append("int f(int a) { int x = 0;")
    s += [f"  x += e{i}(a);" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_straight(n):
    s = ["int f(int a) { int x = a;"]
    s += [f"  x = x * 3 + {i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_locals(n):
    s = ["int f(int a) {"]
    s += [f"  int v{i} = a + {i};" for i in range(n)]
    s.append("  int x = 0;")
    s += [f"  x += v{i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_switch(n):
    s = ["int f(int a) { int x = 0; switch (a) {"]
    s += [f"  case {i}: x = {i*7+1}; break;" for i in range(n)]
    s.append("  default: x = -1; } return x; }")
    return s

def fam_ifchain(n):
    s = ["int f(int a) { int x = 0;"]
    s.append("  if (a == 0) x = 1;")
    s += [f"  else if (a == {i}) x = {i*3};" for i in range(1, n)]
    s.append("  return x; }")
    return s

def fam_loops(n):
    s = ["int f(int a) { int x = 0;"]
    s += [f"  for (int i{i} = 0; i{i} < a; i{i}++) x += i{i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_gotos(n):
    s = ["int f(int a) { int x = 0;"]
    s += [f"  L{i}: x += {i}; if (x > a) goto L{(i*7)%n};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_macros(n):
    s = [f"#define M{i} ({i} + 1)" for i in range(n)]
    s.append("int f(void) { int x = 0;")
    s += [f"  x += M{i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_macros_unused(n):
    s = [f"#define M{i} ({i} + 1)" for i in range(n)]
    s.append("int f(void) { int x = 0;")
    s += [f"  x += M{n-1};" for _ in range(64)]
    s.append("  return x; }")
    return s

def fam_strings(n):
    s = ["int puts(const char *);", "void f(void) {"]
    s += [f'  puts("string literal number {i}");' for i in range(n)]
    s.append("}")
    return s

def fam_structs(n):
    s = [f"struct S{i} {{ int a; int b{i}; }};" for i in range(n)]
    s.append("int f(void) { int x = 0;")
    s += [f"  {{ struct S{i} v; v.a = {i}; x += v.a; }}" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_members(n):
    s = ["struct Big {"]
    s += [f"  int m{i};" for i in range(n)]
    s.append("};")
    s.append("int f(struct Big *p) { int x = 0;")
    s += [f"  x += p->m{i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_enums(n):
    s = ["enum E {"]
    s += [f"  E{i} = {i}," for i in range(n)]
    s.append("};")
    s.append("int f(void) { int x = 0;")
    s += [f"  x += E{i};" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_initializer(n):
    s = ["int table[] = {"]
    s += [f"  {i*31 % 1000}," for i in range(n)]
    s.append("};")
    return s

def fam_struct_init(n):
    s = ["struct P { int a; const char *b; int c; };", "struct P table[] = {"]
    s += [f'  {{ {i}, "s{i}", {i*2} }},' for i in range(n)]
    s.append("};")
    return s

def fam_ptr_init(n):
    s = [f"int g{i};" for i in range(n)]
    s.append("int *table[] = {")
    s += [f"  &g{i}," for i in range(n)]
    s.append("};")
    return s

def fam_typedefs(n):
    s = ["typedef int T0;"]
    s += [f"typedef T{i-1} T{i};" for i in range(1, n)]
    s.append("int f(void) { int x = 0;")
    s += [f"  {{ T{i} v = {i}; x += v; }}" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_prototypes(n):
    s = []
    for i in range(n):
        s.append(f"static int h{i}(int);")
    for i in range(n):
        s.append(f"static int h{i}(int a) {{ return a + {i}; }}")
    s.append("int f(int a) { return " + " + ".join(f"h{i}(a)" for i in range(0, n, max(1, n//64))) + "; }")
    return s

def fam_nested(n):
    s = ["int f(int a) { int x = 0;"]
    s += ["  {" for i in range(n)]
    s.append("  x += a;")
    s += ["  }" for i in range(n)]
    s.append("  return x; }")
    return s

def fam_longexpr(n):
    return ["int f(int a) { return " + " + ".join("a" for _ in range(n)) + "; }"]

def fam_statics(n):
    # N static locals across N functions
    s = []
    for i in range(n):
        s.append(f"int f{i}(void) {{ static int c{i}; return ++c{i}; }}")
    return s

def fam_compound_lits(n):
    s = ["struct P { int a, b; };", "int use(struct P *);", "void f(void) {"]
    s += [f"  use(&(struct P){{ {i}, {i+1} }});" for i in range(n)]
    s.append("}")
    return s

def fam_includes(n, outdir):
    import os
    s = []
    for i in range(n):
        p = os.path.join(outdir, f"inc_{i}.h")
        with open(p, "w") as fh:
            fh.write(f"#ifndef INC_{i}_H\n#define INC_{i}_H\nint inc_decl_{i}(int);\n#endif\n")
        s.append(f'#include "inc_{i}.h"')
        s.append(f'#include "inc_{i}.h"')
    s.append("int f(void) { return 0; }")
    return s

FAMS = {k[4:]: v for k, v in globals().items() if k.startswith("fam_")}

if __name__ == "__main__":
    fam, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    import os
    if fam == "includes":
        lines = fam_includes(n, os.path.dirname(os.path.abspath(out)))
    else:
        lines = FAMS[fam](n)
    with open(out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
