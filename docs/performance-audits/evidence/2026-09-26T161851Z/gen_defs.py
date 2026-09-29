#!/usr/bin/env python3
"""Deterministic C family for definition-token lookups (#1382).

  defs T Q        T array typedefs (type rows that are not definitions), then
                  Q file-scope declarations that each define an anonymous struct
  defs_multi T Q  the same, each definition shared by two declarators
  local T Q       the T typedefs, then one function with Q block-scope
                  declarations that each define a tagged struct
"""
import sys


def emit(family, fillers, queries):
    lines = [f"typedef int A{index}[{index + 1}];" for index in range(fillers)]
    if family == "defs":
        lines += [f"struct {{ int a{index}; }} v{index};" for index in range(queries)]
    elif family == "defs_multi":
        lines += [f"struct {{ int a{index}; }} v{index}, *w{index};" for index in range(queries)]
    elif family == "local":
        lines.append("int f(void) {")
        lines.append("    int total = 0;")
        for index in range(queries):
            lines.append(f"    struct L{index} {{ int a; }} l{index} = {{{index}}}; total += l{index}.a;")
        lines.append("    return total;")
        lines.append("}")
    else:
        raise SystemExit(f"unknown family {family}")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.stdout.write(emit(sys.argv[1], int(sys.argv[2]), int(sys.argv[3])))
