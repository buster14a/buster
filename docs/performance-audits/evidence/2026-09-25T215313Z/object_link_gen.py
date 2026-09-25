#!/usr/bin/env python3
"""object_link_gen.py N DIR: writes the three object/link families of issues #1309 and #1319.
  fn<N>.c   N trivial non-inlined functions (compile with --target=x86_64-apple-macos -c; control x86_64-linux-gnu)
  ctor<N>.c N __attribute__((constructor)) functions plus main (compile and link with ide cc)
  asm<N>.s  N global labels, each calling another label (ide cc -c)"""
import os, sys
n, out = int(sys.argv[1]), sys.argv[2]
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, f"fn{n}.c"), "w") as f:
    f.write("\n".join(f"int f{i}(int a) {{ return a + {i}; }}" for i in range(n)) + "\n")
with open(os.path.join(out, f"ctor{n}.c"), "w") as f:
    f.write("\n".join(["int g;"] + [f"__attribute__((constructor)) static void c{i}(void) {{ g += {i}; }}" for i in range(n)] +
                      ["int main(void) { return g & 1; }"]) + "\n")
with open(os.path.join(out, f"asm{n}.s"), "w") as f:
    f.write("\n".join([".text"] + [f".globl f{i}\nf{i}:\n    call f{(i * 7) % n}\n    ret" for i in range(n)]) + "\n")
