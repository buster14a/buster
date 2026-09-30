#!/usr/bin/env python3
"""Deterministic C families that drive per-global relocation validation.

Each family is parameterized by R (relocations in one table) and optionally G
(number of such tables). Output is valid C17 that trusted Clang accepts with
-fsyntax-only. Families:

  ptr       G tables of R ordered `&gK` pointers
  str       G tables of R ordered string-literal pointers
  struct    G struct tables {int, const char *, int} x R (one pointer each)
  reverse   G tables of R designated pointers written in descending index order
  shuffle   G tables of R designated pointers in a fixed LCG permutation
  control   R globals and G scalar tables of R ints (no relocations)
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


def emit(family, relocations, tables):
    lines = []
    if family in ("ptr", "reverse", "shuffle", "control"):
        for index in range(relocations):
            lines.append(f"int g{index};")
    for table in range(tables):
        if family == "ptr":
            body = ", ".join(f"&g{index}" for index in range(relocations))
            lines.append(f"int *table{table}[] = {{ {body} }};")
        elif family == "str":
            body = ", ".join(f'"t{table}s{index}"' for index in range(relocations))
            lines.append(f"const char *table{table}[] = {{ {body} }};")
        elif family == "struct":
            lines.append(f"struct P{table} {{ int a; const char *b; int c; }};")
            body = ", ".join(f'{{ {index}, "t{table}s{index}", {2 * index} }}' for index in range(relocations))
            lines.append(f"struct P{table} table{table}[] = {{ {body} }};")
        elif family == "reverse":
            body = ", ".join(f"[{index}] = &g{index}" for index in reversed(range(relocations)))
            lines.append(f"int *table{table}[{relocations}] = {{ {body} }};")
        elif family == "shuffle":
            body = ", ".join(f"[{index}] = &g{index}" for index in lcg_permutation(relocations, 0x9E3779B9 + table))
            lines.append(f"int *table{table}[{relocations}] = {{ {body} }};")
        elif family == "control":
            body = ", ".join(str(index) for index in range(relocations))
            lines.append(f"int table{table}[] = {{ {body} }};")
        else:
            raise SystemExit(f"unknown family {family}")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: reloc_gen.py FAMILY RELOCATIONS TABLES > out.c")
    sys.stdout.write(emit(sys.argv[1], int(sys.argv[2]), int(sys.argv[3])))
