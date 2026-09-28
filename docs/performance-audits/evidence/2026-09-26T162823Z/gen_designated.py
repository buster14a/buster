#!/usr/bin/env python3
"""Deterministic C families for constant-initializer relocation compaction.

  ordered N   int *t[N] = {[0] = &g0, ..., [N-1] = &gN-1}
  reverse N   the same designators in descending order
  shuffle N   the same designators in a fixed LCG permutation
  override N  N ordered designators, then every slot designated again
  range N     GNU [0 ... N-1] = &d default, then N/8 ordered overrides
"""
import sys


def lcg_permutation(count, seed=0x9E3779B9):
    order = list(range(count))
    state = seed & 0xFFFFFFFF
    for index in range(count - 1, 0, -1):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        swap = state % (index + 1)
        order[index], order[swap] = order[swap], order[index]
    return order


def emit(family, count, _unused=0):
    lines = [f"int g{index};" for index in range(count)] + ["int d;"]
    if family == "ordered":
        order = list(range(count))
    elif family == "reverse":
        order = list(reversed(range(count)))
    elif family == "shuffle":
        order = lcg_permutation(count)
    elif family == "override":
        order = list(range(count)) + list(range(count))
    elif family == "range":
        order = list(range(0, count, 8))
    else:
        raise SystemExit(f"unknown family {family}")
    body = ", ".join(f"[{index}] = &g{index}" for index in order)
    if family == "range":
        body = f"[0 ... {count - 1}] = &d, " + body
    lines.append(f"int *table[{count}] = {{ {body} }};")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.stdout.write(emit(sys.argv[1], int(sys.argv[2])))
