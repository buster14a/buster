#!/usr/bin/env python3
"""Independent record-layout oracle: Clang-derived data images per target.

Buster lays a record out with one rule per target (CRecordLayoutRule), placed
by one authority that both of its layout engines call. Agreement between the
engines, or between Buster's own targets, is not evidence that the rule is the
target ABI's (issue #1439). This tool derives the expected answers from Clang,
an implementation that shares no code with Buster, without executing anything:
the same C source is compiled for each target, and the static data it defines
is read back out of the object.

For every record R the source defines

  R<i>_fold   {sizeof, _Alignof, offsetof(member)...} folded into enumerators:
              the parse-side engine (c_parse_type_layout_core).
  R<i>_img_m  `R x = { .m = <pattern> }`: which bits and bytes member m
              occupies in the emitted object -- the IR layout and the static
              initializer writer.
  R<i>_wrap   `struct { char c; R r; }`: the IR layout's alignment and size.

Subcommands (run from the repository root; a correctness tool, not a
benchmark):

  generate  Write src/buster/tests/compiler/frontend/c/record_layout_corpus
            .generated.h: the corpus source and, per group of targets whose
            answers coincide, every symbol's expected bytes. Needs a Clang with
            every target listed in TARGETS. The fixed-seed random records are
            deterministic, so regeneration is reproducible for a given Clang.
  campaign  Compare a built ide with Clang on fresh random records for every
            target, reporting each mismatching record. Nothing is written into
            the tree.

  tools/record_layout_oracle.py generate [--clang clang]
  tools/record_layout_oracle.py campaign --ide build/Release/ide [--seeds 1:9]
      [--count 80] [--attributes] [--targets x86_64-windows,...]

The corpus avoids what Buster does not claim here: MinGW's GCC `ms_struct`
corners (its Windows targets are the MSVC ABI), x86-64 UEFI (PE/COFF does not
imply the Windows C layout, docs/uefi-target.md), and aligned attributes under
#pragma pack (#1244/#1248).
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile

# (Buster target, Clang triple). Microsoft: Windows; AAPCS64: AArch64 Linux
# and Android; Itanium: the rest.
TARGETS = [
    ("x86_64-linux", "x86_64-linux-gnu"),
    ("aarch64-linux", "aarch64-linux-gnu"),
    ("x86_64-windows", "x86_64-pc-windows-msvc"),
    ("aarch64-windows", "aarch64-pc-windows-msvc"),
    ("x86_64-macos", "x86_64-apple-macos11"),
    ("arm64-macos", "arm64-apple-macos11"),
    ("arm64-ios", "arm64-apple-ios14"),
    ("aarch64-android", "aarch64-linux-android29"),
    ("x86_64-android", "x86_64-linux-android29"),
    ("bpfel-unknown-linux", "bpfel"),
]
# bpf output is an eBPF artifact rather than a native object, so the in-tree
# test (record_layout_tests) covers the native targets and campaigns cover bpf.
IN_TREE_EXCLUDED = {"bpfel-unknown-linux"}

INTEGER_TYPES = [("_Bool", 1), ("char", 8), ("signed char", 8), ("unsigned char", 8), ("short", 16),
                 ("unsigned short", 16), ("int", 32), ("unsigned int", 32), ("long long", 64),
                 ("unsigned long long", 64)]
ORDINARY_TYPES = ["char", "short", "int", "long long", "double", "float", "void *"]
GENERATED = Path("src/buster/tests/compiler/frontend/c/record_layout_corpus.generated.h")

# Directed shapes: the #1439 witnesses, #1344's AAPCS64 shapes, #1318's
# #pragma pack straddle, empty and zero-width-only records, unions, nesting,
# packing and aligned bit-fields. Each is a list of member declarations.
DIRECTED = [
    ("struct", "", None, ["char a;", "int b : 4;", "char c;"]),
    ("struct", "", None, ["short a : 4;", "int b : 4;", "char c;"]),
    ("struct", "", None, ["char a;", "int : 0;", "char c;"]),
    ("struct", "", None, ["int a : 4;", "char : 0;", "char c;"]),
    ("struct", "", None, ["char a : 4;", "char b : 4;", "int d : 4;", "char c;"]),
    ("union", "", None, ["char a;", "int b : 3;"]),
    ("struct", "", None, ["char a;", "long long b : 40;", "char c;"]),
    ("struct", "", None, ["float m0;", "long long f1 : 27;", "unsigned short f2 : 7;", "unsigned short f3 : 1;"]),
    ("struct", "", None, ["int a : 3;", "unsigned char b : 1;"]),
    ("struct", "", None, ["int a : 3;", "unsigned int b : 29;", "char c;"]),
    ("struct", "", None, ["unsigned char a : 3;", "unsigned short b : 9;", "unsigned int c : 20;", "char d;"]),
    ("struct", "", None, ["char before;", "long long : 0;", "char after;"]),
    ("struct", "", None, ["char lead[5];", "int trailing : 3;"]),
    ("struct", "", None, ["short first : 9;", "char second : 7;", "short third : 9;"]),
    ("struct", "", None, ["int wide : 30;", "unsigned char narrow : 5;"]),
    ("struct", "", None, ["char a;", "unsigned int : 0;", "char b;"]),
    ("struct", "", None, ["unsigned int : 0;", "char a;", "char b;"]),
    ("struct", "", None, ["char a;", "char b;", "unsigned int : 0;"]),
    ("struct", "", None, ["char a;", "unsigned int : 7;", "char b;"]),
    ("struct", "", None, ["char a;", "unsigned int : 16;", "char b;"]),
    ("struct", "", None, ["char a;", "unsigned int : 25;", "char b;"]),
    ("union", "", None, ["char a;", "unsigned int : 0;"]),
    ("union", "", None, ["char a;", "unsigned int : 1;"]),
    ("struct", "", None, ["char a;", "long long : 38;", "char b;"]),
    ("struct", "", None, ["int a : 3;", "int : 0;"]),
    ("union", "", None, ["int a : 3;", "long long : 0;"]),
    ("union", "", None, ["char f0 : 3;", "unsigned long long : 0;", "unsigned int f2 : 20;"]),
    ("struct", "", 8, ["int : 6;", "short s : 12;", "char x;"]),
    ("struct", "", 2, ["short s;", "int b : 24;", "char c;"]),
    ("struct", "", 4, ["char a;", "int b : 30;", "long long c : 40;", "char d;"]),
    ("struct", "", 1, ["char a;", "int b : 4;", "char c;"]),
    ("struct", "", 2, ["char a;", "int b : 4;", "short c : 12;", "char d;"]),
    ("struct", " __attribute__((packed))", None, ["char c;", "int b : 5;", "char t;"]),
    ("struct", " __attribute__((packed))", None, ["int a : 3;", "int : 0;", "int b : 3;"]),
    ("union", " __attribute__((packed))", None, ["long long b : 40;"]),
    ("struct", "", None, ["unsigned a : 20;", "unsigned b : 5 __attribute__((aligned(1)));", "char c;"]),
    ("struct", "", None, ["char a;", "int b : 4 __attribute__((aligned(8)));", "char c;"]),
    ("struct", "", None, ["_Bool a : 1;", "_Bool b : 1;", "int c : 2;", "_Bool d : 1;"]),
    ("struct", "", None, ["unsigned long long a : 1;", "unsigned int b : 1;", "unsigned short c : 1;", "unsigned char d : 1;"]),
    ("struct", "", None, ["unsigned char a : 1;", "unsigned short b : 1;", "unsigned int c : 1;", "unsigned long long d : 1;"]),
    ("struct", "", None, ["int x;", "struct { char a : 3; int b : 4; } inner;", "char y;"]),
    ("struct", "", None, ["char x;", "struct { char a; unsigned int : 0; char b; } inner[2];", "char y;"]),
    ("struct", "", None, ["double d;", "int a : 12;", "int b : 12;", "int c : 12;", "short s;"]),
    ("struct", "", None, ["void * p;", "unsigned short a : 3;", "unsigned int b : 3;", "unsigned long long c : 3;"]),
]


def directed_records():
    records = []
    for kind, attribute, pack, members in DIRECTED:
        records.append({"kind": kind, "attribute": attribute, "pack": pack, "members": members})
    return records


def random_record(rng: random.Random, attributes: bool):
    kind = "union" if rng.random() < 0.15 else "struct"
    members = []
    for index in range(rng.randint(1, 6)):
        if rng.random() < 0.55:
            spelling, bits = rng.choice(INTEGER_TYPES)
            width = 0 if rng.random() < 0.15 else rng.randint(1, bits)
            name = "" if width == 0 or rng.random() < 0.15 else f"f{index}"
            members.append(f"{spelling} {name} : {width};")
        else:
            spelling = rng.choice(ORDINARY_TYPES)
            members.append(f"{spelling} m{index}{rng.choice(['', '', '', '[3]'])};")
    if all(re.search(r"\s:\s", member) and not re.search(r"\bf\d+\s:", member) for member in members):
        members.append("char tail;")
    attribute = ""
    pack = None
    if attributes:
        draw = rng.random()
        if draw < 0.15:
            attribute = " __attribute__((packed))"
        elif draw < 0.3:
            pack = rng.choice([1, 2, 4])
    return {"kind": kind, "attribute": attribute, "pack": pack, "members": members}


MEMBER = re.compile(r"^(?P<type>.*?)\s*(?P<name>\b[a-z]\w*)?\s*(?P<array>\[\d+\])?\s*(?::\s*(?P<width>\d+))?\s*(?:__attribute__.*)?;$")


def member_facts(declaration: str):
    """(type, name, width or None, array) for one generated member declaration."""
    if declaration.startswith("struct {"):
        name = re.search(r"}\s*(\w+)(\[\d+\])?;$", declaration)
        return "record", name.group(1), None, name.group(2) or ""
    match = MEMBER.match(declaration)
    spelling = match.group("type").strip()
    name = match.group("name") or ""
    if name in ("char", "short", "int", "long", "signed", "unsigned", "float", "double", "void", "_Bool"):
        spelling, name = f"{spelling} {name}".strip(), ""
    width = match.group("width")
    return spelling, name, int(width) if width is not None else None, match.group("array") or ""


def pattern(spelling: str, width, array: str) -> str:
    if spelling == "record":
        return "{ 0 }"
    if spelling in ("double", "float"):
        value = "-1.5"
    elif spelling == "void *":
        value = "(void *)0"
    elif spelling == "_Bool":
        value = "1"
    else:
        value = f"({spelling})-1"
    return "{ " + value + " }" if array else value


def render(records) -> str:
    # No header: a cross-target compile then needs no SDK or resource tree.
    out = []
    for index, record in enumerate(records):
        if record["pack"]:
            out.append(f"#pragma pack(push, {record['pack']})")
        body = "\n".join("    " + member for member in record["members"])
        out.append(f"{record['kind']}{record['attribute']} R{index}\n{{\n{body}\n}};")
        if record["pack"]:
            out.append("#pragma pack(pop)")
        tag = f"{record['kind']} R{index}"
        facts = [member_facts(member) for member in record["members"]]
        ordinary = [name for spelling, name, width, array in facts if width is None and name]
        enumerators = [f"R{index}_S = sizeof({tag})", f"R{index}_A = _Alignof({tag})"]
        enumerators += [f"R{index}_O_{name} = __builtin_offsetof({tag}, {name})" for name in ordinary]
        out.append("enum { " + ", ".join(enumerators) + " };")
        folded = [f"R{index}_S", f"R{index}_A"] + [f"R{index}_O_{name}" for name in ordinary]
        out.append(f"unsigned R{index}_fold[] = {{ {', '.join(folded)}, 0x5a5a5a5au }};")
        for spelling, name, width, array in facts:
            if not name or spelling == "void *" or (spelling == "record"):
                continue
            out.append(f"{tag} R{index}_img_{name} = {{ .{name} = {pattern(spelling, width, array)} }};")
        out.append(f"struct {{ char c; {tag} r; }} R{index}_wrap = {{ 1 }};")
    return "\n".join(out) + "\n"


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def object_symbols(path: str):
    """{symbol: (section name, address)} and {section name: {address: byte}}."""
    symbols = {}
    for line in run(["llvm-objdump", "-t", path]).stdout.splitlines():
        coff = re.match(r"^\[\s*\d+\]\(sec\s+(\d+)\).*0x([0-9a-f]+)\s+(\S+)$", line)
        if coff:
            symbols[coff.group(3)] = ("#" + coff.group(1), int(coff.group(2), 16))
            continue
        # ELF rows carry a size column before the name; Mach-O rows do not.
        generic = re.match(r"^([0-9a-f]{8,16})\s.{7}\s(\S+)(?:\s+[0-9a-f]{8,16})?\s+(\S+)$", line)
        if generic:
            name = generic.group(3)
            symbols[name[1:] if name.startswith("_R") else name] = (generic.group(2), int(generic.group(1), 16))
    sections = {}
    current = None
    for line in run(["llvm-objdump", "-s", path]).stdout.splitlines():
        header = re.match(r"^Contents of section (\S+):", line)
        if header:
            current = header.group(1).split(",")[-1]
            sections[current] = {}
            continue
        row = re.match(r"^ ([0-9a-f]+) ((?:[0-9a-f]{2,8} ?){1,4})", line)
        if row and current:
            base = int(row.group(1), 16)
            digits = row.group(2).replace(" ", "")
            for offset in range(0, len(digits), 2):
                sections[current][base + offset // 2] = int(digits[offset:offset + 2], 16)
    numbered = {}
    for line in run(["llvm-objdump", "-h", path]).stdout.splitlines():
        entry = re.match(r"^\s*(\d+)\s+(\S+)\s+[0-9a-f]+\s+[0-9a-f]+", line)
        if entry:
            numbered["#" + str(int(entry.group(1)) + 1)] = entry.group(2)
    return symbols, sections, numbered


def symbol_bytes(object_file, name: str, length: int):
    """A symbol's bytes. A missing symbol or section is fatal: comparing two
    absent answers would pass vacuously."""
    symbols, sections, numbered = object_file
    if name not in symbols:
        raise SystemExit(f"symbol {name} is missing from the object")
    section, address = symbols[name]
    section = numbered.get(section, section).split(",")[-1]
    data = sections.get(section)
    if data is None or any(address + offset not in data for offset in range(length)):
        raise SystemExit(f"symbol {name} has no {length} data bytes in {section}")
    return bytes(data[address + offset] for offset in range(length))


def subject_bytes(object_file, name: str, length: int):
    """The subject's bytes, or None when it lacks them: a mismatch to report,
    never a pass, and not a reason to stop the campaign."""
    try:
        return symbol_bytes(object_file, name, length)
    except SystemExit:
        return None


def expected_images(records, object_file):
    """Every probe symbol's bytes, with lengths derived from the object's own fold."""
    images = {}
    for index, record in enumerate(records):
        facts = [member_facts(member) for member in record["members"]]
        ordinary = [name for spelling, name, width, array in facts if width is None and name]
        fold_length = 4 * (3 + len(ordinary))
        fold = symbol_bytes(object_file, f"R{index}_fold", fold_length)
        size = int.from_bytes(fold[0:4], "little")
        alignment = int.from_bytes(fold[4:8], "little")
        images[f"R{index}_fold"] = fold
        for spelling, name, width, array in facts:
            if not name or spelling in ("void *", "record"):
                continue
            images[f"R{index}_img_{name}"] = symbol_bytes(object_file, f"R{index}_img_{name}", size)
        wrap_length = alignment + size
        images[f"R{index}_wrap"] = symbol_bytes(object_file, f"R{index}_wrap", wrap_length)
    return images


def compile_clang(clang: str, triple: str, source: str, output: str):
    result = run([clang, "-target", triple, "-std=gnu17", "-w", "-fno-common", "-O0", "-c", source, "-o", output])
    if result.returncode:
        raise SystemExit(f"{clang} -target {triple} failed:\n{result.stderr}")


def corpus_records():
    records = directed_records()
    for seed in range(1, 4):
        rng = random.Random(seed)
        records += [random_record(rng, seed != 1) for _ in range(20)]
    return records


def c_string(text: str) -> str:
    """The source as S8_INITIALIZER rows, one per line, newline included."""
    return "\n".join('    S8_INITIALIZER("' + line.replace("\\", "\\\\").replace('"', '\\"') + '\\n"),' for line in text.split("\n"))


def generate(arguments) -> int:
    records = corpus_records()
    source_text = render(records)
    version = run([arguments.clang, "--version"]).stdout.splitlines()[0]
    with tempfile.TemporaryDirectory() as directory:
        source = os.path.join(directory, "corpus.c")
        Path(source).write_text(source_text)
        groups = []
        for buster_target, triple in TARGETS:
            output = os.path.join(directory, buster_target + ".o")
            compile_clang(arguments.clang, triple, source, output)
            images = expected_images(records, object_symbols(output))
            for group in groups:
                if group["images"] == images:
                    group["targets"].append((buster_target, triple))
                    break
            else:
                groups.append({"images": images, "targets": [(buster_target, triple)]})
    lines = [
        "// Generated by tools/record_layout_oracle.py generate; do not edit.",
        f"// Oracle: {version}, one compile per target triple, bytes read from the",
        "// objects' data sections. The expectations come from Clang alone; Buster's",
        "// two layout engines are what record_layout_tests checks against them.",
        "#pragma once",
        "",
        "// One string per line: ISO C bounds a single literal's length.",
        "BUSTER_GLOBAL_LOCAL String8 record_layout_corpus_lines[] = {",
        c_string(source_text.rstrip("\n")),
        "};",
        "",
    ]
    for group_index, group in enumerate(groups):
        lines.append(f"BUSTER_GLOBAL_LOCAL RecordLayoutExpectation const record_layout_expectations_{group_index}[] = {{")
        for name, value in group["images"].items():
            lines.append(f'    {{S8_INITIALIZER("{name}"), S8_INITIALIZER("{value.hex()}")}},')
        lines.append("};")
        lines.append("")
    lines.append("BUSTER_GLOBAL_LOCAL RecordLayoutTarget const record_layout_targets[] = {")
    for group_index, group in enumerate(groups):
        for buster_target, triple in group["targets"]:
            if buster_target in IN_TREE_EXCLUDED:
                continue
            lines.append(f'    {{S8_INITIALIZER("{buster_target}"), S8_INITIALIZER("{triple}"), record_layout_expectations_{group_index},'
                         f" BUSTER_ARRAY_LENGTH(record_layout_expectations_{group_index})}},")
    lines.append("};")
    GENERATED.write_text("\n".join(lines) + "\n")
    print(f"{GENERATED}: {len(records)} records, {len(groups)} distinct target groups, {sum(len(g['images']) for g in groups)} expectations")
    for group_index, group in enumerate(groups):
        print(f"  group {group_index}: " + ", ".join(target for target, _ in group["targets"]))
    return 0


def campaign(arguments) -> int:
    first, last = (int(part) for part in arguments.seeds.split(":"))
    selected = [pair for pair in TARGETS if not arguments.targets or pair[0] in arguments.targets.split(",")]
    failures = 0
    rejections = 0
    with tempfile.TemporaryDirectory() as directory:
        source = os.path.join(directory, "campaign.c")
        for seed in range(first, last):
            rng = random.Random(seed)
            records = [random_record(rng, arguments.attributes) for _ in range(arguments.count)]
            for buster_target, triple in selected:
                live = list(range(len(records)))
                while True:
                    subset = [records[index] for index in live]
                    Path(source).write_text(render(subset))
                    buster_object = os.path.join(directory, "buster.o")
                    compiled = run([arguments.ide, "cc", "-target", buster_target, "-c", source, "-o", buster_object])
                    if not compiled.returncode:
                        break
                    rejected = re.search(r"'R(\d+)_", compiled.stdout + compiled.stderr)
                    if not rejected:
                        print(f"seed {seed} {buster_target}: compile failure\n{compiled.stdout}{compiled.stderr}")
                        failures += 1
                        subset = []
                        break
                    rejections += 1
                    print(f"seed {seed} {buster_target}: rejected {subset[int(rejected.group(1))]}")
                    del live[int(rejected.group(1))]
                if not subset:
                    continue
                clang_object = os.path.join(directory, "clang.o")
                compile_clang(arguments.clang, triple, source, clang_object)
                expected = expected_images(subset, object_symbols(clang_object))
                buster = object_symbols(buster_object)
                for index, record in enumerate(subset):
                    wrong = [name for name, value in expected.items()
                             if name.startswith(f"R{index}_") and subject_bytes(buster, name, len(value)) != value]
                    if wrong:
                        failures += 1
                        print(f"seed {seed} {buster_target}: {record} differs in {wrong}")
    print(f"campaign: failures={failures} rejections={rejections}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generate_parser = commands.add_parser("generate")
    generate_parser.add_argument("--clang", default="clang")
    campaign_parser = commands.add_parser("campaign")
    campaign_parser.add_argument("--ide", required=True)
    campaign_parser.add_argument("--clang", default="clang")
    campaign_parser.add_argument("--seeds", default="1:5")
    campaign_parser.add_argument("--count", type=int, default=80)
    campaign_parser.add_argument("--attributes", action="store_true")
    campaign_parser.add_argument("--targets", default="")
    arguments = parser.parse_args()
    return generate(arguments) if arguments.command == "generate" else campaign(arguments)


if __name__ == "__main__":
    sys.exit(main())
