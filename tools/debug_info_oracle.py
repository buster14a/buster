#!/usr/bin/env python3
"""Check Buster's debug information with consumers that share none of its code.

Buster's DWARF and CodeView tests parse its output with small readers written
for those tests, and those readers restated the writer's beliefs: arrays with
no subrange, bit-fields without bit geometry, CodeView registers numbered in
hardware order and LF_ULONG spelled 0x8003 all passed (#1440). This tool asks
real consumers instead: lldb evaluates variables in a Buster-built executable
and must print what the program stored; llvm-dwarfdump and llvm-readobj
describe Buster's objects and must agree with Clang's description of the same
source. It is a correctness tool, run by hand or on a GitHub-hosted runner; it
writes nothing into the tree.

  tools/debug_info_oracle.py --ide build/Release/ide [--clang clang]

Exit status 1 on any disagreement, 2 when a required consumer is missing.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PROGRAM = r"""
struct bits { char a; unsigned int b : 3; unsigned int c : 13; long long d : 40; unsigned char e : 1; short f; };
int table[3][2] = {{1, 2}, {3, 4}, {5, 6}};
struct bits packed_bits = {'x', 5, 1000, -7, 1, -3};
double doubles[4] = {1.5, -2.25, 3.0, 4.75};
int main(void) { return table[1][1] + packed_bits.b + (int)doubles[1]; }
"""

EXPECTED_LLDB = [
    "(int[3][2]) table = {",
    "[0] = ([0] = 1, [1] = 2)",
    "[2] = ([0] = 5, [1] = 6)",
    "b = 5",
    "c = 1000",
    "d = -7",
    "e = '\\x01'",
    "f = -3",
    "[1] = -2.25",
]

CODEVIEW_PROGRAM = r"""
struct bits { char a; unsigned int b : 3; unsigned int c : 13; long long d : 40; };
struct bits g;
int arr[7];
int f(int x, double y) { volatile int keep = x * 3; return keep + (int)y + g.c + arr[x & 3]; }
"""


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require(tool: str) -> str:
    path = shutil.which(tool)
    if not path:
        print(f"debug-info oracle: required consumer {tool} was not found", file=sys.stderr)
        sys.exit(2)
    return path


def lldb_values(executable: Path) -> str:
    result = run([require("lldb"), "-b", "-o", "target variable table packed_bits doubles", str(executable)])
    return result.stdout


def check_lldb(arguments, directory: Path) -> list[str]:
    source = directory / "program.c"
    source.write_text(PROGRAM)
    failures = []
    for label, command in (("clang", [arguments.clang, "-g", "-O0"]), ("buster", [arguments.ide, "cc", "-g", "-O0"])):
        executable = directory / f"program-{label}"
        built = run(command + [str(source), "-o", str(executable)])
        if built.returncode:
            failures.append(f"{label} did not build the program: {built.stdout}{built.stderr}")
            continue
        text = lldb_values(executable)
        missing = [line for line in EXPECTED_LLDB if line not in text]
        if missing:
            failures.append(f"lldb reading the {label} build lacks {missing}:\n{text}")
    return failures


def dwarf_members(obj: Path) -> dict:
    """{struct name: [(member, data_member_location, data_bit_offset, bit_size)]} from llvm-dwarfdump."""
    text = run([require("llvm-dwarfdump"), "--debug-info", str(obj)]).stdout
    records = {}
    current = None
    member = None
    for line in text.splitlines():
        if "DW_TAG_structure_type" in line:
            current = []
            member = None
        elif "DW_TAG_member" in line and current is not None:
            member = {}
            current.append(member)
        elif "DW_TAG_" in line and "DW_TAG_member" not in line:
            member = None
        match = re.search(r"(DW_AT_\w+)\s+\((.*)\)", line)
        if not match:
            continue
        attribute, value = match.groups()
        if member is not None:
            member[attribute] = value.strip('"')
        elif current is not None and attribute == "DW_AT_name":
            records[value.strip('"')] = current
    return {name: [(m.get("DW_AT_name"), m.get("DW_AT_data_member_location"), m.get("DW_AT_data_bit_offset"), m.get("DW_AT_bit_size"))
                   for m in members] for name, members in records.items()}


def normalize(value):
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return value


def check_dwarf(arguments, directory: Path) -> list[str]:
    source = directory / "program.c"
    source.write_text(PROGRAM)
    objects = {}
    for label, command in (("clang", [arguments.clang, "-g", "-O0", "-c"]), ("buster", [arguments.ide, "cc", "-g", "-O0", "-c"])):
        objects[label] = directory / f"program-{label}.o"
        built = run(command + [str(source), "-o", str(objects[label])])
        if built.returncode:
            return [f"{label} did not compile the program: {built.stdout}{built.stderr}"]
    failures = []
    reference = dwarf_members(objects["clang"])
    subject = dwarf_members(objects["buster"])
    for name, members in reference.items():
        for expected, actual in zip(members, subject.get(name, [])):
            if tuple(map(normalize, expected)) != tuple(map(normalize, actual)):
                failures.append(f"DWARF member {name}.{expected[0]}: Clang {expected[1:]}, Buster {actual[1:]}")
    verify = run([require("llvm-dwarfdump"), "--verify", str(objects["buster"])])
    if verify.returncode:
        failures.append(f"llvm-dwarfdump --verify rejects Buster's DWARF:\n{verify.stdout}")
    return failures


def codeview_facts(obj: Path) -> dict:
    text = run([require("llvm-readobj"), "--codeview", str(obj)]).stdout
    facts = {"bitfields": [], "array_sizes": [], "registers": {}}
    for match in re.finditer(r"LF_BITFIELD \(0x1205\)\s+Type: [^\n]*\n\s+BitSize: (\d+)\s+BitOffset: (\d+)", text):
        facts["bitfields"].append((int(match.group(1)), int(match.group(2))))
    for match in re.finditer(r"LF_ARRAY \(0x1503\)[^}]*?SizeOf: (\d+)", text):
        facts["array_sizes"].append(int(match.group(1)))
    for match in re.finditer(r"VarName: (\w+)\s+\}\s+DefRangeRegisterSym \{\s+Kind[^\n]*\n\s+Register: (\w+)", text):
        facts["registers"].setdefault(match.group(1), match.group(2))
    return facts


def check_codeview(arguments, directory: Path) -> list[str]:
    source = directory / "codeview.c"
    source.write_text(CODEVIEW_PROGRAM)
    failures = []
    for buster_target, clang_triple in (("x86_64-windows", "x86_64-pc-windows-msvc"), ("aarch64-windows", "aarch64-pc-windows-msvc")):
        clang_object = directory / f"cv-{buster_target}-clang.obj"
        buster_object = directory / f"cv-{buster_target}-buster.obj"
        compiled = run([arguments.clang, "-target", clang_triple, "-g", "-gcodeview", "-O0", "-c", str(source), "-o", str(clang_object)])
        if compiled.returncode:
            failures.append(f"clang {clang_triple}: {compiled.stderr}")
            continue
        compiled = run([arguments.ide, "cc", "-target", buster_target, "-g", "-O0", "-c", str(source), "-o", str(buster_object)])
        if compiled.returncode:
            failures.append(f"buster {buster_target}: {compiled.stdout}{compiled.stderr}")
            continue
        reference = codeview_facts(clang_object)
        subject = codeview_facts(buster_object)
        if sorted(reference["bitfields"]) != sorted(subject["bitfields"]):
            failures.append(f"{buster_target} LF_BITFIELD (size, offset): Clang {reference['bitfields']}, Buster {subject['bitfields']}")
        if 28 not in subject["array_sizes"]:
            failures.append(f"{buster_target} LF_ARRAY sizes {subject['array_sizes']} lack int[7]'s 28 bytes")
        # Parameter x arrives in the first integer argument register.
        entry = {"x86_64-windows": "RCX", "aarch64-windows": "ARM64_X0"}[buster_target]
        if subject["registers"].get("x") not in (None, entry) and not subject["registers"]["x"].startswith(("RDX", "RAX", "R8", "R9", "ARM64_X")):
            failures.append(f"{buster_target} places x in {subject['registers'].get('x')}, not a general register")
        if "NOREG" in "".join(subject["registers"].values()):
            failures.append(f"{buster_target} names a register CodeView cannot resolve: {subject['registers']}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ide", required=True)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--skip-lldb", action="store_true", help="for hosts without an lldb")
    arguments = parser.parse_args()
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        if not arguments.skip_lldb:
            failures += check_lldb(arguments, path)
        failures += check_dwarf(arguments, path)
        failures += check_codeview(arguments, path)
    for failure in failures:
        print(failure)
    print(f"debug-info oracle: {len(failures)} disagreement(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
