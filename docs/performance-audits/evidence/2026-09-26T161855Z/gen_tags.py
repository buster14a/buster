#!/usr/bin/env python3
"""Deterministic C families for tag type names in lowering (#1297).

  unique T Q   T file-scope struct tags, then one function with Q casts
               `(struct S{i % T} *)p` and Q `sizeof (struct S{i % T})`
  shadow T Q   the same, with struct S0 also defined in a block that holds
               every reference -- each name of it has two candidate rows
"""
import sys


def emit(family, tags, queries):
    lines = [f"struct S{index} {{ int a; long b[{index % 7 + 1}]; }};" for index in range(tags)]
    lines.append("long f(void *p) {")
    lines.append("    long total = 0;")
    if family == "shadow":
        lines.append("    {")
        lines.append("    struct S0 { char c; long d; };")
    elif family != "unique":
        raise SystemExit(f"unknown family {family}")
    for index in range(queries):
        tag = 0 if family == "shadow" else index % tags
        lines.append(f"    total += ((struct S{tag} *)p)->{'d' if family == 'shadow' else 'a'} + (long)sizeof (struct S{tag});")
    if family == "shadow":
        lines.append("    }")
    lines.append("    return total;")
    lines.append("}")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.stdout.write(emit(sys.argv[1], int(sys.argv[2]), int(sys.argv[3])))
