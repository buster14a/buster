#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Disassemble one sized ELF function and optionally annotate its instructions."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def output(*args, **kwargs):
    return subprocess.check_output(args, text=True, env={**os.environ, "LC_ALL": "C"}, **kwargs)


def function(binary, symbol):
    if "file format elf64-x86-64" not in output("objdump", "-f", "--", binary):
        raise ValueError(f"{binary} is not an x86-64 ELF object or executable")
    rows = []
    for line in output("objdump", "-t", "--", binary).splitlines():
        match = re.fullmatch(r"([0-9a-f]+)\s+.*?\bF\s+(\S+)\s+([0-9a-f]+)\s+(?:(?:\.hidden|\.internal|\.protected) )?(.+)", line)
        if match and match[4] == symbol:
            rows.append((int(match[1], 16), match[2], int(match[3], 16)))
    if len(rows) != 1 or rows[0][2] == 0:
        raise ValueError(f"need one defined, sized function named {symbol!r} (found {len(rows)}). Inspect nm -nS or use its unstripped .o")
    start, section, size = rows[0]
    listing = output("objdump", "-drwz", "-Mintel", "-j", section, f"--start-address={start}",
                     f"--stop-address={start + size}", "--", binary)
    print(listing, flush=True)
    instructions = []
    position = start
    for line in listing.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t", line)
        if match:
            address = int(match[1], 16)
            if address != position:
                raise ValueError(f"non-contiguous disassembly at {address:#x}")
            data = bytes.fromhex(match[2])
            instructions.append((address, data))
            position += len(data)
    if position != start + size:
        raise ValueError(f"decoded {position - start} of {size} symbol bytes")
    return instructions


def uops(path, instructions):
    try:
        import xed
    except ImportError as error:
        raise ValueError("XED-to-XML's Python module is required for --uops (see docs/commands/profiling.md)") from error
    if not hasattr(xed, "matchXMLAttributes"):
        raise ValueError("--uops needs the xed module from XED-to-XML")
    decoded = []
    forms = {}
    for address, data in instructions:
        rows = xed.disasHex(data.hex())
        inst = rows[0] if len(rows) == 1 and rows[0]["opcode"].lower() == data.hex() else None
        if inst:
            forms.setdefault(inst["iform"], [])
        decoded.append((address, inst))
    # One pass over the caller's XML. Retain only forms used by this function.
    for event, node in ET.iterparse(path, events=("start", "end")):
        if node.tag == "root" and event == "start":
            print(f"uops.info measurements (https://uops.info/xml.html, {node.get('date', 'undated XML')}) from {path}")
        if node.tag == "instruction" and event == "end":
            if node.get("iform") in forms:
                measurements = {}
                for arch in ("ZEN4", "ZEN5"):
                    measure = node.find(f"architecture[@name='{arch}']/measurement")
                    if measure is not None:
                        measurements[arch] = (dict(measure.attrib), [dict(lat.attrib) for lat in measure.findall("latency")])
                forms[node.attrib["iform"]].append((dict(node.attrib), measurements))
            node.clear()
    print("TP_loop and TP_unrolled are measured cycles/instruction. Latencies use XML operand indices and cycles.")
    for address, inst in decoded:
        if inst is None:
            print(f"{address:#x}  unavailable (XED could not decode these bytes)")
            continue
        matches = [row for row in forms[inst["iform"]] if xed.matchXMLAttributes(inst, row[0])]
        if len(matches) != 1:
            print(f"{address:#x}  {inst['asm']}  unavailable ({len(matches)} XML form matches)")
            continue
        attributes, measurements = matches[0]
        print(f"{address:#x}  {inst['asm']}  [{attributes['string']}]")
        for arch in ("ZEN4", "ZEN5"):
            if arch not in measurements:
                print(f"  {arch}  unavailable")
                continue
            fields, latencies = measurements[arch]
            # Preserve the database's same-register and indexed-address cases
            # and all latency bounds. Several operand pairs can have different costs.
            keys = ("uops", "ports", "TP_ports", "TP_loop", "TP_unrolled")
            values = [f"{key}={value}" for key, value in fields.items()
                      if any(key == base + suffix for base in keys for suffix in ("", "_same_reg", "_indexed"))]
            print(f"  {arch}  {'  '.join(values)}")
            for lat in latencies:
                values = " ".join(f"{k}={v}" for k, v in lat.items() if k not in ("start_op", "target_op"))
                print(f"    {lat['start_op']} -> {lat['target_op']}  {values}")


def mca(cpu, instructions):
    llvm_mc = os.environ.get("LLVM_MC", "llvm-mc")
    llvm_mca = os.environ.get("LLVM_MCA", "llvm-mca")
    # Some LLVM releases print the supported CPUs and return 1 for -mcpu=help.
    cpus = subprocess.run([llvm_mca, "-mtriple=x86_64", "-mcpu=help"], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
    if not re.search(rf"^\s*{cpu}\s+-", cpus, re.MULTILINE):
        raise ValueError(f"{llvm_mca} has no {cpu} model")
    encoded = "".join(" ".join(f"0x{byte:02x}" for byte in data) + "\n" for _, data in instructions)
    asm = subprocess.run([llvm_mc, "--disassemble", "--triple=x86_64"], input=encoded,
                         text=True, capture_output=True, check=True)
    if asm.stderr.strip():
        raise ValueError(f"{llvm_mc} could not decode the full symbol cleanly:\n{asm.stderr}")
    print(f"LLVM scheduling model {cpu} (static instruction tables)", flush=True)
    subprocess.run([llvm_mca, "-mtriple=x86_64", f"-mcpu={cpu}", "--instruction-tables"],
                   input=asm.stdout, text=True, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("symbol")
    parser.add_argument("--uops", metavar="instructions.xml", type=Path, help="annotate instruction forms using XED and uops.info")
    parser.add_argument("--mca", choices=("znver4", "znver5"), action="append", default=[], help="show LLVM instruction tables (repeatable)")
    args = parser.parse_args()
    instructions = function(args.binary, args.symbol)
    if args.uops:
        uops(args.uops, instructions)
    for cpu in args.mca:
        mca(cpu, instructions)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as exc:
        sys.exit(f"function: {exc}")
