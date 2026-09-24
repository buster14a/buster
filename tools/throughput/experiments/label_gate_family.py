#!/usr/bin/env python3
"""Deterministic scaling family for the label-provenance gate census.

Writes one C translation unit per (shape, M, K, D) point into --output and a
manifest.json describing them. Each unit has one function of interest:

  conjunction  a goto label, M assignments whose value has K identifiers and a
               trailing `&& a0`, D nested blocks around them, no label address
  parenthesized  the same with the K-identifier sum parenthesized before `&&`,
               so the `)&&` proof of the new gate runs its cast path
  address      `conjunction` plus one real `&&retry` label address and a
               computed goto (both gates must walk this body)
  unlabeled    `conjunction` without the label and goto (neither gate walks)

The counts printed by the census-instrumented compiler on these units are the
evidence; this script produces inputs only.
"""

import argparse
import json
import pathlib


def body(shape, m, k, d):
    parameters = ", ".join(f"int a{i}" for i in range(k))
    operands = " + ".join(f"a{i}" for i in range(k))
    if shape == "parenthesized":
        value = f"({operands}) && a0"
    else:
        value = f"{operands} && a0"
    lines = [f"int label_gate_{shape}_{m}_{k}_{d}({parameters})", "{", "    int r = 0;"]
    if shape == "address":
        lines.append("    void *p = &&retry;")
    indent = "    "
    for _ in range(d):
        lines.append(indent + "{")
        indent += "    "
    if shape != "unlabeled":
        lines.append("retry:")
    for _ in range(m):
        lines.append(f"{indent}r = {value};")
    if shape == "address":
        lines.append(f"{indent}if (r && a0) goto *p;")
    elif shape == "unlabeled":
        lines.append(f"{indent}if (r && a0) r = 0;")
    else:
        lines.append(f"{indent}if (r && a0) goto retry;")
    for _ in range(d):
        indent = indent[:-4]
        lines.append(indent + "}")
    lines.append("    return r;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    arguments = parser.parse_args()
    output = pathlib.Path(arguments.output)
    output.mkdir(parents=True, exist_ok=True)
    base = (32, 16, 4)
    points = set()
    for m in (8, 32, 128):
        points.add((m, base[1], base[2]))
    for k in (4, 16, 64):
        points.add((base[0], k, base[2]))
    for d in (1, 4, 16):
        points.add((base[0], base[1], d))
    manifest = []
    for shape in ("conjunction", "parenthesized", "address", "unlabeled"):
        for m, k, d in sorted(points):
            name = f"{shape}_{m}_{k}_{d}.c"
            (output / name).write_text(body(shape, m, k, d))
            manifest.append({"file": name, "shape": shape, "M": m, "K": k, "D": d})
    (output / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"wrote {len(manifest)} units to {output}")


if __name__ == "__main__":
    main()
