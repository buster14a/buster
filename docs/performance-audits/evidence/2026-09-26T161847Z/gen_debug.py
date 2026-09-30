#!/usr/bin/env python3
"""Deterministic C families for the two -g debug-model mechanisms.

  funcs N        N small functions: one debug seed and one IR function each
                 (debug_model_build symbol matching).
  locals N       one function with N locals, each conditionally updated in a
                 loop, so SSA gives each local block parameters and no single
                 place (machine_debug_values_build blocks x unresolved locals).
  locals_split N same statements spread over N/16 functions of 16 locals, a
                 control with the same total code and bounded per-function size.
"""
import sys


def emit(family, count, _unused=0):
    lines = []
    if family == "funcs":
        for index in range(count):
            lines.append(f"int f{index}(int x) {{ int y = x * {index + 1}; return y + {index}; }}")
    elif family in ("locals", "locals_split"):
        per = count if family == "locals" else 16
        for function in range(max(1, count // per)):
            lines.append(f"int f{function}(int n, const int *p) {{")
            lines.append("    " + " ".join(f"int v{k} = p[{k}];" for k in range(per)))
            lines.append("    for (int i = 0; i < n; i++) {")
            for k in range(per):
                lines.append(f"        if (p[i] & {k + 1}) v{k} += i;")
            lines.append("    }")
            lines.append("    return " + " + ".join(f"v{k}" for k in range(per)) + ";")
            lines.append("}")
    else:
        raise SystemExit(f"unknown family {family}")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.stdout.write(emit(sys.argv[1], int(sys.argv[2])))
