#!/usr/bin/env python3
"""Differential probe for type-embedded constants; a correctness diagnostic, not a benchmark.

Every C type fact that is a constant expression -- a bit-field width, an array
bound, an alignment request -- reaches two layout engines: the sizeof/offsetof
folding in c_parse.c and the IrType mapping in c_gen.c. This probe compiles one
program per case that prints, from the same binary,

  pf  the value folded during semantic analysis (enumerator constants),
  ir  the layout of the emitted object (address arithmetic),

and runs the same source through a reference compiler. A row whose pf and ir
differ is an internal disagreement; a row whose ir differs from the reference
is an ABI divergence. A compile the reference accepts and Buster rejects is
reported separately.

Run on a correctness executor only. No timing, PMU, service access or
generated-binding edits; preserve every result.

  tools/type_constant_layout_probe.py --ide build/Release/ide --out /tmp/probe \
      [--reference clang] [--seeds 20] [--count 30] [--families width]
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import random
import re
import subprocess
import sys

SCALARS = ["char", "short", "int", "long long", "float", "double", "unsigned char", "unsigned"]
INT_BITS = {"char": 8, "unsigned char": 8, "short": 16, "unsigned short": 16, "int": 32, "unsigned": 32,
            "long long": 64, "unsigned long long": 64}
FAMILIES = ("width", "bound", "packed", "aligned", "pragma")

# Hand-written shapes, each a single aggregate T with a member x. Families:
# width, bound and alignment expressions, plus controls every engine agrees on.
TARGETED = {
    "width_paren": "typedef struct { char c; int b : (5); char x; } T;",
    "width_enumerator": "enum { W = 5 }; typedef struct { char c; int b : W; char x; } T;",
    "width_cast": "typedef struct { char c; unsigned b : (unsigned char)261; char x; } T;",
    "width_sizeof": "typedef struct { char c; unsigned b : sizeof(int) * 8 - 7; char x; } T;",
    "width_hex": "typedef struct { char c; unsigned b : 0x5; char x; } T;",
    "width_suffix": "typedef struct { char c; unsigned b : 5u; char x; } T;",
    "width_character": "typedef struct { char c; unsigned b : '\\5'; char x; } T;",
    "width_os_h_shape": "enum { SC = 3 }; typedef struct { unsigned long long cap : (unsigned long)SC; unsigned long long u : 1;"
                        " unsigned long long r : sizeof(unsigned long long) * 8 - (unsigned long)SC - 1; char x; } T;",
    "bound_cast": "typedef struct { char c; char x[(unsigned char)259]; } T;",
    "bound_sizeof_object": "static int g[3]; typedef struct { char c; char x[sizeof g]; } T;",
    "bound_sizeof_ratio": "static int g[3]; typedef struct { char c; char x[sizeof(g) / sizeof(g[0])]; } T;",
    "bound_sizeof_array_type": "typedef struct { char c; char x[sizeof(char[3])]; } T;",
    "bound_compound_literal": "typedef struct { char c; char x[sizeof((int[3]){1, 2, 3})]; } T;",
    "bound_offsetof": "struct In { int a; char b; }; typedef struct { char c; char x[__builtin_offsetof(struct In, b)]; } T;",
    "bound_string": "typedef struct { char c; char x[sizeof(\"abcde\")]; } T;",
    "bound_float_cast": "typedef struct { char c; char x[(int)3.9]; } T;",
    "align_enumerator": "enum { A = 8 }; typedef struct { char c; int x __attribute__((aligned(A))); } T;",
    "alignas_enumerator": "enum { A = 8 }; typedef struct { char c; _Alignas(A) int x; } T;",
    "align_sizeof": "typedef struct { char c; int x __attribute__((aligned(sizeof(double)))); } T;",
    "pack_caps_aligned": "#pragma pack(push, 2)\ntypedef struct { char c; int x __attribute__((aligned(8))); } T;\n#pragma pack(pop)",
    "pack_caps_alignas": "#pragma pack(push, 2)\ntypedef struct { char c; _Alignas(8) int x; } T;\n#pragma pack(pop)",
    "control_literal_width": "typedef struct { char c; int b : 5; char x; } T;",
    "control_literal_bound": "typedef struct { char c; char x[3]; } T;",
    "control_literal_aligned": "typedef struct { char c; int x __attribute__((aligned(8))); } T;",
}


def width_spelling(rng: random.Random, n: int, families: set[str], enumerators: set[tuple[str, int]]) -> str:
    kinds = ["literal"] + (["paren", "sum", "enumerator", "sizeof", "cast", "macro"] if "width" in families else [])
    kind = rng.choice(kinds)
    if kind == "paren":
        return f"({n})"
    if kind == "sum" and n:
        return f"{n - 1} + 1"
    if kind == "enumerator":
        enumerators.add((f"EW_{n}", n))
        return f"EW_{n}"
    if kind == "sizeof":
        return f"sizeof(char) * {n}"
    if kind == "cast":
        return f"(int){n}"
    if kind == "macro":
        return f"MW_{n}"
    return str(n)


def bound_spelling(rng: random.Random, n: int, families: set[str]) -> str:
    if "bound" not in families:
        return str(n)
    return rng.choice([str(n), f"({n})", f"{n} + 0", f"EB_{n}", f"sizeof(int) / sizeof(int) * {n}"])


def aggregate(rng: random.Random, index: int, families: set[str], enumerators: set[tuple[str, int]], earlier: list[str]):
    kind = "union" if rng.random() < 0.2 else "struct"
    attributes = "__attribute__((packed)) " if "packed" in families and rng.random() < 0.3 else ""
    aggregate_aligned = f" __attribute__((aligned({rng.choice([1, 2, 4, 8, 16])})))" if "aligned" in families and rng.random() < 0.15 else ""
    pack = rng.choice([1, 2, 4]) if "pragma" in families and rng.random() < 0.2 else None
    members = []
    for m in range(rng.randint(1, 7)):
        name = f"m{m}"
        r = rng.random()
        if r < 0.45:
            ty = rng.choice(list(INT_BITS))
            width = rng.randint(0, INT_BITS[ty])
            spelling = width_spelling(rng, width, families, enumerators)
            if not width:
                members.append(("zero", f"{ty} : {spelling};"))
                continue
            packed = " __attribute__((packed))" if "packed" in families and rng.random() < 0.1 else ""
            members.append(("bit", f"{ty} {name} : {spelling}{packed};"))
        elif r < 0.6 and earlier:
            members.append(("field", f"{rng.choice(earlier)} {name};", name))
        elif r < 0.72:
            members.append(("field", f"{rng.choice(SCALARS)} {name}[{bound_spelling(rng, rng.randint(1, 5), families)}];", name))
        else:
            extra = f" __attribute__((aligned({rng.choice([1, 2, 4, 8, 16])})))" if "aligned" in families and rng.random() < 0.15 else ""
            extra += " __attribute__((packed))" if "packed" in families and rng.random() < 0.1 else ""
            members.append(("field", f"{rng.choice(SCALARS)} {name}{extra};", name))
    if not any(member[0] != "zero" for member in members):
        members.append(("field", "int tail;", "tail"))
    tag = f"T{index}"
    lines = [f"#pragma pack(push, {pack})"] if pack else []
    lines.append(f"typedef {kind} {attributes}{tag}_tag {{")
    lines += ["    " + member[1] for member in members]
    lines.append(f"}}{aggregate_aligned} {tag};")
    if pack:
        lines.append("#pragma pack(pop)")
    return tag, [member[2] for member in members if member[0] == "field"], "\n".join(lines)


def random_program(seed: int, count: int, families: set[str]) -> str:
    rng = random.Random(seed)
    enumerators: set[tuple[str, int]] = set()
    types = []
    definitions = []
    for index in range(count):
        tag, fields, text = aggregate(rng, index, families, enumerators, [t for t, _ in types][-4:])
        types.append((tag, fields))
        definitions.append(text)
    out = ["#include <stdio.h>", "#include <stddef.h>"]
    out += [f"#define MW_{n} ({n})" for n in range(65)]
    out.append("enum { " + ", ".join(f"EB_{n} = {n}" for n in range(1, 6)) + " };")
    if enumerators:
        out.append("enum { " + ", ".join(f"{a} = {b}" for a, b in sorted(enumerators)) + " };")
    out += definitions
    for tag, fields in types:
        constants = [f"{tag}_PS = sizeof({tag})", f"{tag}_PA = _Alignof({tag})"]
        constants += [f"{tag}_PO_{f} = offsetof({tag}, {f})" for f in fields]
        out.append("enum { " + ", ".join(constants) + " };")
    out.append("int main(void)\n{")
    for tag, fields in types:
        out.append(f"    {{ static {tag} o[2];")
        out.append(f'      printf("{tag} size pf=%d ir=%td align pf=%d\\n", (int){tag}_PS, (char*)&o[1] - (char*)&o[0], (int){tag}_PA);')
        for f in fields:
            out.append(f'      printf("{tag}.{f} offset pf=%d ir=%td\\n", (int){tag}_PO_{f}, (char*)&o[0].{f} - (char*)&o[0]);')
        out.append("    }")
    out.append("    return 0;\n}")
    return "\n".join(out) + "\n"


def targeted_program(body: str) -> str:
    return ("#include <stdio.h>\n#include <stddef.h>\n" + body +
            "\nenum { PS = sizeof(T), PA = _Alignof(T), PO = offsetof(T, x) };\n"
            "int main(void) { static T o[2];\n"
            '  printf("T size pf=%d ir=%td align pf=%d\\n", PS, (char*)&o[1] - (char*)&o[0], PA);\n'
            '  printf("T.x offset pf=%d ir=%td\\n", PO, (char*)&o[0].x - (char*)&o[0]); return 0; }\n')


def build_and_run(compiler: list[str], source: Path, binary: Path) -> dict:
    compiled = subprocess.run(compiler + [str(source), "-o", str(binary)], text=True, capture_output=True, timeout=300)
    if compiled.returncode:
        return {"compiled": False, "diagnostic": (compiled.stdout + compiled.stderr).strip().splitlines()[:1]}
    ran = subprocess.run([str(binary)], text=True, capture_output=True, timeout=60)
    return {"compiled": True, "exit": ran.returncode, "rows": ran.stdout.splitlines()}


def compare(buster: dict, reference: dict) -> dict:
    result = {"rows": 0, "internal": [], "abi": [], "rejected_valid": not buster["compiled"] and reference["compiled"]}
    if not buster["compiled"] or not reference["compiled"]:
        return result
    for mine, theirs in zip(buster["rows"], reference["rows"]):
        result["rows"] += 1
        values = dict(re.findall(r"(pf|ir)=(-?\d+)", mine.split(" align ")[0]))
        if len(set(values.values())) > 1:
            result["internal"].append(mine)
        if re.findall(r"ir=(-?\d+)", mine) != re.findall(r"ir=(-?\d+)", theirs) or \
           re.findall(r"align pf=(\d+)", mine) != re.findall(r"align pf=(\d+)", theirs):
            result["abi"].append(f"{mine} || reference {theirs}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ide", required=True, type=Path)
    parser.add_argument("--reference", default="clang")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--families", default="width", help=f"comma-separated subset of {','.join(FAMILIES)}")
    parser.add_argument("--no-targeted", action="store_true")
    arguments = parser.parse_args()
    families = set(filter(None, arguments.families.split(",")))
    if not families <= set(FAMILIES):
        parser.error(f"unknown family in {arguments.families}")
    arguments.out.mkdir(parents=True, exist_ok=True)
    buster = [str(arguments.ide), "cc"]
    reference = [arguments.reference, "-w"]
    cases = [] if arguments.no_targeted else [(name, targeted_program(body)) for name, body in TARGETED.items()]
    cases += [(f"random-{'-'.join(sorted(families))}-{seed}", random_program(seed, arguments.count, families))
              for seed in range(1, arguments.seeds + 1)]
    report = {"ide": str(arguments.ide), "reference": arguments.reference, "families": sorted(families), "cases": {}}
    totals = {"rows": 0, "internal": 0, "abi": 0, "rejected_valid": 0}
    for name, program in cases:
        source = arguments.out / f"{name}.c"
        source.write_text(program)
        mine = build_and_run(buster, source, arguments.out / f"{name}.buster")
        theirs = build_and_run(reference, source, arguments.out / f"{name}.reference")
        outcome = compare(mine, theirs)
        report["cases"][name] = {"buster": mine, "reference": theirs, **outcome}
        totals["rows"] += outcome["rows"]
        totals["internal"] += len(outcome["internal"])
        totals["abi"] += len(outcome["abi"])
        totals["rejected_valid"] += int(outcome["rejected_valid"])
        if name in TARGETED:
            state = "REJECTED " + " ".join(mine.get("diagnostic", [])) if outcome["rejected_valid"] else \
                    ("INTERNAL " if outcome["internal"] else "") + ("ABI " if outcome["abi"] else "") or "ok"
            print(f"{name:26} {state}")
    report["totals"] = totals
    (arguments.out / "report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(totals))
    return 0


if __name__ == "__main__":
    sys.exit(main())
