#!/usr/bin/env python3
"""Census of local x86-64 instruction patterns in Buster-generated objects.

Research tool, run by hand; it is outside the build graph and nothing in the
compiler depends on it. It disassembles ELF x86-64 objects with GNU objdump
(Intel syntax), rebuilds each function's basic blocks and a conservative
register/flag liveness solution, and counts normalized windows of one to
three consecutive instructions inside a block.

A window's key keeps what a rewrite rule can depend on and nothing else:

* mnemonic and operand widths;
* register relationships, as window-local names (A, B, ...) for general
  registers and literal rbp/rsp/rip frame and stack bases;
* memory operands as base/index/scale plus window-local displacement names,
  so "the same slot" is visible, with the displacement's encoded width;
* immediates by class (zero, one, all-ones, power of two, signed-8, ...).

Each key also carries its encoded byte total, a histogram of which
window-written registers are live after the window, a count of instances in
which any arithmetic flag is live after it, and example locations. The
evidence for a rewrite is this census plus an independent proof; the census
alone never justifies one.

Usage:
    python3 tools/machine_pattern_census.py --output census.json --report census.md a.o b.o
    python3 tools/machine_pattern_census.py --self-test
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

VERSION = 1

GPR_NAMES = [
    ("rax", "eax", "ax", "al"), ("rcx", "ecx", "cx", "cl"), ("rdx", "edx", "dx", "dl"), ("rbx", "ebx", "bx", "bl"),
    ("rsp", "esp", "sp", "spl"), ("rbp", "ebp", "bp", "bpl"), ("rsi", "esi", "si", "sil"), ("rdi", "edi", "di", "dil"),
] + [(f"r{n}", f"r{n}d", f"r{n}w", f"r{n}b") for n in range(8, 16)]
GPR = {}
for family, names in enumerate(GPR_NAMES):
    for width, name in zip((64, 32, 16, 8), names):
        GPR[name] = (family, width)
for family, name in ((0, "ah"), (1, "ch"), (2, "dh"), (3, "bh")):
    GPR[name] = (family, 8)
RSP, RBP = 4, 5
FLAG_BASE = 16
CF, PF, AF, ZF, SF, OF = (1 << (FLAG_BASE + bit) for bit in range(6))
ALL_FLAGS = CF | PF | AF | ZF | SF | OF
VECTOR_BASE = 22
ALL_GPRS = (1 << 16) - 1
ALL_VECTORS = ((1 << 32) - 1) << VECTOR_BASE
EVERYTHING = ALL_GPRS | ALL_FLAGS | ALL_VECTORS


def bit(family: int) -> int:
    return 1 << family


def vector_family(name: str) -> int | None:
    match = re.fullmatch(r"[xyz]mm(\d+)", name)
    return int(match.group(1)) if match else None


# SysV x86-64: argument registers read by a call, caller-saved registers it
# defines, and what a return keeps live (results and callee-saved state).
CALL_USES = bit(7) | bit(6) | bit(2) | bit(1) | bit(8) | bit(9) | bit(0) | bit(RSP) | (0xFF << VECTOR_BASE)
CALL_DEFS = bit(0) | bit(1) | bit(2) | bit(6) | bit(7) | bit(8) | bit(9) | bit(10) | bit(11) | ALL_FLAGS | ALL_VECTORS
RET_USES = bit(0) | bit(2) | bit(3) | bit(RSP) | bit(RBP) | bit(12) | bit(13) | bit(14) | bit(15) | (0x3 << VECTOR_BASE)

CONDITION_FLAGS = {
    "o": OF, "no": OF, "b": CF, "c": CF, "nae": CF, "ae": CF, "nb": CF, "nc": CF,
    "e": ZF, "z": ZF, "ne": ZF, "nz": ZF, "be": CF | ZF, "na": CF | ZF, "a": CF | ZF, "nbe": CF | ZF,
    "s": SF, "ns": SF, "p": PF, "pe": PF, "np": PF, "po": PF,
    "l": SF | OF, "nge": SF | OF, "ge": SF | OF, "nl": SF | OF, "le": ZF | SF | OF, "ng": ZF | SF | OF,
    "g": ZF | SF | OF, "nle": ZF | SF | OF,
}
ARITH_FLAGS = {"add", "sub", "and", "or", "xor", "adc", "sbb", "cmp", "test", "neg", "imul", "bsf", "bsr",
               "popcnt", "lzcnt", "tzcnt", "ucomisd", "ucomiss", "comisd", "comiss", "bt"}
NO_FLAG_EFFECT = {"mov", "movzx", "movsx", "movsxd", "movabs", "lea", "push", "pop", "not", "nop", "cqo", "cdq",
                  "cdqe", "cwde", "cbw", "cwd", "movq", "movd", "movss", "movsd", "movaps", "movups", "movapd",
                  "movupd", "movdqa", "movdqu", "cvtsi2sd", "cvtsi2ss", "cvttsd2si", "cvttss2si", "cvtsd2ss",
                  "cvtss2sd", "addsd", "addss", "subsd", "subss", "mulsd", "mulss", "divsd", "divss", "sqrtsd",
                  "sqrtss", "xorps", "xorpd", "andps", "andpd", "orps", "orpd", "pxor", "vmovdqu8", "vmovdqu64",
                  "vmovdqa64", "vmovq", "vmovd", "kmovq", "kmovw", "kmovb", "kmovd", "vzeroupper", "leave",
                  "endbr64", "int3", "ud2", "mfence", "lfence", "sfence", "pause"}
TERMINATORS = {"jmp", "ret", "ud2"}


class Operand:
    __slots__ = ("kind", "text", "family", "width", "vector", "base", "index", "scale", "displacement",
                 "relocated", "value", "size")

    def __init__(self, kind: str, text: str):
        self.kind = kind
        self.text = text
        self.family = None
        self.width = 0
        self.vector = None
        self.base = None
        self.index = None
        self.scale = 1
        self.displacement = 0
        self.relocated = False
        self.value = 0
        self.size = 0

    def registers(self) -> int:
        mask = 0
        if self.kind == "reg" and self.family is not None:
            mask |= bit(self.family)
        if self.kind == "reg" and self.vector is not None:
            mask |= 1 << (VECTOR_BASE + self.vector)
        if self.kind == "mem":
            for register in (self.base, self.index):
                if register is not None and register != "rip":
                    mask |= bit(register)
        return mask


MEMORY_SIZES = {"BYTE": 8, "WORD": 16, "DWORD": 32, "QWORD": 64, "XMMWORD": 128, "YMMWORD": 256, "ZMMWORD": 512,
                "TBYTE": 80, "FWORD": 48}


def parse_operand(text: str) -> Operand:
    text = text.strip()
    if text in GPR:
        operand = Operand("reg", text)
        operand.family, operand.width = GPR[text]
        return operand
    vector = vector_family(text)
    if vector is not None:
        operand = Operand("reg", text)
        operand.vector = vector
        operand.width = {"x": 128, "y": 256, "z": 512}[text[0]]
        return operand
    if re.fullmatch(r"k[0-7]", text):
        return Operand("kreg", text)
    if "[" in text:
        operand = Operand("mem", text)
        size_match = re.match(r"(\w+) PTR", text)
        operand.size = MEMORY_SIZES.get(size_match.group(1), 0) if size_match else 0
        inner = text[text.index("[") + 1:text.rindex("]")]
        prefix = text[:text.index("[")]
        if ":" in prefix:
            operand.base = "segment"
        for term in re.findall(r"[+-]?[^+-]+", inner):
            sign = -1 if term.startswith("-") else 1
            term = term.lstrip("+-")
            if "*" in term:
                register, scale = term.split("*")
                operand.index = GPR[register][0]
                operand.scale = int(scale)
            elif term == "rip":
                operand.base = "rip"
            elif term in GPR:
                if operand.base is None:
                    operand.base = GPR[term][0]
                else:
                    operand.index = GPR[term][0]
            else:
                operand.displacement += sign * int(term, 16)
        if operand.base == "segment":
            operand.base = None
        return operand
    if re.fullmatch(r"-?0x[0-9a-f]+", text):
        operand = Operand("imm", text)
        operand.value = int(text, 16)
        return operand
    return Operand("other", text)


def split_operands(text: str) -> list[str]:
    parts, depth, current = [], 0, []
    for character in text:
        if character == "[":
            depth += 1
        elif character == "]":
            depth -= 1
        if character == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(character)
    if current and "".join(current).strip():
        parts.append("".join(current))
    return parts


class Instruction:
    __slots__ = ("address", "size", "mnemonic", "operands", "target", "relocation", "prefix", "uses", "defs",
                 "text")

    def __init__(self, address: int, size: int, mnemonic: str, operand_text: str, prefix: str):
        self.address = address
        self.size = size
        self.mnemonic = mnemonic
        self.prefix = prefix
        self.target = None
        self.relocation = None
        self.text = (prefix + " " if prefix else "") + mnemonic + (" " + operand_text if operand_text else "")
        target_match = re.match(r"([0-9a-f]+) <", operand_text)
        if mnemonic in ("call", "jmp") or (mnemonic.startswith("j") and mnemonic[1:] in CONDITION_FLAGS):
            if target_match:
                self.target = int(target_match.group(1), 16)
                operand_text = ""
        operand_text = re.sub(r"\s*#.*$", "", operand_text)
        self.operands = [parse_operand(part) for part in split_operands(operand_text)] if operand_text else []
        self.uses, self.defs = effects(self)


def condition_of(mnemonic: str, prefix: str) -> str | None:
    if mnemonic.startswith(prefix) and mnemonic[len(prefix):] in CONDITION_FLAGS:
        return mnemonic[len(prefix):]
    return None


def effects(instruction: Instruction) -> tuple[int, int]:
    """Conservative (uses, defs) register/flag masks for one instruction."""
    mnemonic = instruction.mnemonic
    operands = instruction.operands
    uses = 0
    defs = 0
    for operand in operands:
        if operand.kind == "mem":
            uses |= operand.registers()

    def write(operand: Operand) -> None:
        nonlocal uses, defs
        if operand.kind == "reg":
            if operand.family is not None and operand.width < 32:
                uses |= bit(operand.family)
            if operand.vector is not None and operand.width < 512 and mnemonic.startswith("v") is False:
                uses |= operand.registers()
            defs |= operand.registers()

    def read(operand: Operand) -> None:
        nonlocal uses
        uses |= operand.registers()

    same_register_idiom = (len(operands) == 2 and operands[0].kind == "reg" and operands[1].kind == "reg" and
                           operands[0].text == operands[1].text)
    if instruction.prefix:
        uses |= EVERYTHING
    elif mnemonic in ("mov", "movzx", "movsx", "movsxd", "movabs", "lea") and len(operands) == 2:
        if mnemonic != "lea":
            read(operands[1])
        if operands[0].kind == "mem":
            pass
        else:
            write(operands[0])
    elif mnemonic in ("add", "sub", "and", "or", "xor", "adc", "sbb") and len(operands) == 2:
        if mnemonic in ("xor", "sub") and same_register_idiom:
            pass
        else:
            read(operands[0])
            read(operands[1])
        if mnemonic in ("adc", "sbb"):
            uses |= CF
        if operands[0].kind != "mem":
            write(operands[0])
        defs |= ALL_FLAGS
    elif mnemonic in ("cmp", "test") and len(operands) == 2:
        read(operands[0])
        read(operands[1])
        defs |= ALL_FLAGS
    elif mnemonic in ("inc", "dec") and len(operands) == 1:
        read(operands[0])
        if operands[0].kind != "mem":
            write(operands[0])
        defs |= ALL_FLAGS & ~CF
    elif mnemonic in ("neg", "not") and len(operands) == 1:
        read(operands[0])
        if operands[0].kind != "mem":
            write(operands[0])
        if mnemonic == "neg":
            defs |= ALL_FLAGS
    elif mnemonic == "imul" and len(operands) in (2, 3):
        for operand in operands[1:]:
            read(operand)
        if len(operands) == 2:
            read(operands[0])
        write(operands[0])
        defs |= ALL_FLAGS
    elif mnemonic in ("mul", "imul", "div", "idiv") and len(operands) == 1:
        read(operands[0])
        uses |= bit(0) | (bit(2) if mnemonic in ("div", "idiv") else 0)
        defs |= bit(0) | bit(2) | ALL_FLAGS
    elif mnemonic in ("shl", "shr", "sar", "rol", "ror", "sal") and len(operands) == 2:
        read(operands[0])
        if operands[1].kind == "reg":
            read(operands[1])
            uses |= ALL_FLAGS  # a zero count leaves the flags unchanged
        if operands[0].kind != "mem":
            write(operands[0])
        defs |= ALL_FLAGS
    elif condition_of(mnemonic, "set") and len(operands) == 1:
        uses |= CONDITION_FLAGS[condition_of(mnemonic, "set")]
        if operands[0].kind != "mem":
            write(operands[0])
    elif condition_of(mnemonic, "cmov") and len(operands) == 2:
        uses |= CONDITION_FLAGS[condition_of(mnemonic, "cmov")]
        read(operands[0])
        read(operands[1])
        write(operands[0])
    elif condition_of(mnemonic, "j"):
        uses |= CONDITION_FLAGS[condition_of(mnemonic, "j")]
    elif mnemonic == "jmp":
        for operand in operands:
            read(operand)
    elif mnemonic == "call":
        for operand in operands:
            read(operand)
        uses |= CALL_USES
        defs |= CALL_DEFS
    elif mnemonic == "ret":
        uses |= RET_USES
    elif mnemonic == "push" and len(operands) == 1:
        read(operands[0])
        uses |= bit(RSP)
        defs |= bit(RSP)
    elif mnemonic == "pop" and len(operands) == 1:
        uses |= bit(RSP)
        defs |= bit(RSP)
        write(operands[0])
    elif mnemonic == "leave":
        uses |= bit(RBP)
        defs |= bit(RSP) | bit(RBP)
    elif mnemonic in ("cqo", "cdq"):
        uses |= bit(0)
        defs |= bit(2)
    elif mnemonic in ("cdqe", "cwde"):
        uses |= bit(0)
        defs |= bit(0)
    elif mnemonic in ("bsf", "bsr", "popcnt", "lzcnt", "tzcnt") and len(operands) == 2:
        read(operands[1])
        if mnemonic in ("bsf", "bsr"):
            read(operands[0])  # the destination is unchanged for a zero source
        write(operands[0])
        defs |= ALL_FLAGS
    elif mnemonic in ("nop", "int3", "ud2", "endbr64", "mfence", "lfence", "sfence", "pause"):
        pass
    elif mnemonic in NO_FLAG_EFFECT or mnemonic in ("ucomisd", "ucomiss", "comisd", "comiss"):
        # Vector and conversion moves: read every source, conservatively also
        # the destination (merging forms), and define the destination.
        for operand in operands:
            read(operand)
        if operands and operands[0].kind == "reg":
            defs |= operands[0].registers()
        if mnemonic in ("ucomisd", "ucomiss", "comisd", "comiss"):
            defs |= ALL_FLAGS
    else:
        uses |= EVERYTHING
    return uses, defs


LINE = re.compile(r"^\s*([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t(.*)$")
RELOCATION = re.compile(r"^\s*([0-9a-f]+): (R_X86_64_\w+)\t(.*)$")
FUNCTION = re.compile(r"^([0-9a-f]+) <(.+)>:$")
SECTION = re.compile(r"^Disassembly of section (.+):$")
PREFIXES = {"lock", "rep", "repe", "repz", "repne", "repnz", "data16", "notrack", "bnd"}


def parse_objdump(text: str) -> list[tuple[str, list[Instruction]]]:
    functions: list[tuple[str, list[Instruction]]] = []
    current: list[Instruction] | None = None
    section = ""
    for line in text.splitlines():
        section_match = SECTION.match(line)
        if section_match:
            section = section_match.group(1)
            current = None
            continue
        function_match = FUNCTION.match(line)
        if function_match:
            current = []
            functions.append((section + ":" + function_match.group(2), current))
            continue
        if current is None:
            continue
        relocation_match = RELOCATION.match(line)
        if relocation_match:
            if current:
                current[-1].relocation = relocation_match.group(3)
                for operand in current[-1].operands:
                    if operand.kind == "mem":
                        operand.relocated = True
            continue
        instruction_match = LINE.match(line)
        if not instruction_match:
            continue
        address = int(instruction_match.group(1), 16)
        size = len(instruction_match.group(2).split())
        body = instruction_match.group(3).strip()
        if not body or body.startswith("(bad)"):
            continue
        words = body.split(None, 1)
        prefix = ""
        while words and words[0] in PREFIXES:
            prefix = (prefix + " " + words[0]).strip()
            words = words[1].split(None, 1) if len(words) > 1 else []
        if not words:
            continue
        mnemonic = words[0]
        operand_text = words[1].strip() if len(words) > 1 else ""
        current.append(Instruction(address, size, mnemonic, operand_text, prefix))
    return functions


def is_branch(instruction: Instruction) -> bool:
    return instruction.mnemonic == "jmp" or condition_of(instruction.mnemonic, "j") is not None


def split_blocks(instructions: list[Instruction]) -> tuple[list[list[Instruction]], list[list[int]], int]:
    """Blocks, their successor indices (-1: unknown/indirect) and padding count."""
    # Trailing alignment padding after the last terminator is not code.
    end = len(instructions)
    while end and instructions[end - 1].mnemonic in ("nop", "int3") and end - 1 > 0 and \
            instructions[end - 2].mnemonic in TERMINATORS | {"nop", "int3"}:
        end -= 1
    padding = len(instructions) - end
    body = instructions[:end]
    addresses = {instruction.address for instruction in body}
    leaders = {body[0].address} if body else set()
    for index, instruction in enumerate(body):
        if is_branch(instruction) or instruction.mnemonic in TERMINATORS:
            if instruction.target in addresses:
                leaders.add(instruction.target)
            if index + 1 < len(body):
                leaders.add(body[index + 1].address)
    blocks: list[list[Instruction]] = []
    for instruction in body:
        if instruction.address in leaders or not blocks:
            blocks.append([])
        blocks[-1].append(instruction)
    start_index = {block[0].address: index for index, block in enumerate(blocks)}
    successors: list[list[int]] = []
    for index, block in enumerate(blocks):
        last = block[-1]
        edges: list[int] = []
        falls_through = last.mnemonic not in TERMINATORS
        if is_branch(last):
            if last.target is not None and last.target in start_index:
                edges.append(start_index[last.target])
            else:
                edges.append(-1)
        if falls_through:
            edges.append(index + 1 if index + 1 < len(blocks) else -1)
        successors.append(edges)
    return blocks, successors, padding


def solve_liveness(blocks: list[list[Instruction]], successors: list[list[int]]) -> list[list[int]]:
    """live_after[block][position] as register/flag masks."""
    live_in = [0] * len(blocks)
    changed = True
    while changed:
        changed = False
        for index in range(len(blocks) - 1, -1, -1):
            live = 0
            for successor in successors[index]:
                live |= EVERYTHING if successor < 0 else live_in[successor]
            for instruction in reversed(blocks[index]):
                live = (live & ~instruction.defs) | instruction.uses
            if live != live_in[index]:
                live_in[index] = live
                changed = True
    result = []
    for index, block in enumerate(blocks):
        live = 0
        for successor in successors[index]:
            live |= EVERYTHING if successor < 0 else live_in[successor]
        after = [0] * len(block)
        for position in range(len(block) - 1, -1, -1):
            after[position] = live
            live = (live & ~block[position].defs) | block[position].uses
        result.append(after)
    return result


def immediate_class(value: int, width: int) -> str:
    mask = (1 << width) - 1 if width else (1 << 64) - 1
    value &= mask
    signed = value - (1 << width) if width and value >> (width - 1) else value
    if value == 0:
        return "0"
    if value == 1:
        return "1"
    if value == mask:
        return "m1"
    if value & (value - 1) == 0:
        return "p2"
    if -128 <= signed <= 127:
        return "s8"
    if -(1 << 31) <= signed < (1 << 31):
        return "s32"
    if value < (1 << 32):
        return "u32"
    return "i64"


class Normalizer:
    def __init__(self) -> None:
        self.registers: dict[int, str] = {}
        self.displacements: dict[int, str] = {}
        self.written: list[int] = []

    def register(self, family: int) -> str:
        if family == RSP:
            return "rsp"
        if family == RBP:
            return "rbp"
        if family not in self.registers:
            self.registers[family] = "ABCDEFGHIJKL"[len(self.registers)]
        return self.registers[family]

    def displacement(self, value: int) -> str:
        """Window-local displacement name. A displacement within 16 bytes of
        an earlier one is named relative to it (d0+8), so adjacent frame
        chunks stay recognizable; others get a fresh name."""
        if value == 0:
            return ""
        width = 8 if -128 <= value <= 127 else 32
        if value not in self.displacements:
            relative = None
            for earlier, name in self.displacements.items():
                if "+" not in name and "-" not in name and 0 < abs(value - earlier) <= 16:
                    delta = value - earlier
                    relative = f"{name}{'+' if delta > 0 else '-'}{abs(delta)}"
                    break
            self.displacements[value] = relative or f"d{sum(1 for n in self.displacements.values() if '+' not in n and '-' not in n)}"
        return f"+{self.displacements[value]}:{width}"

    def operand(self, operand: Operand, width_hint: int) -> str:
        if operand.kind == "reg" and operand.family is not None:
            return f"{self.register(operand.family)}{operand.width}"
        if operand.kind == "reg":
            return operand.text[0] + "mm"
        if operand.kind == "kreg":
            return "k"
        if operand.kind == "imm":
            return "#" + immediate_class(operand.value, width_hint)
        if operand.kind == "mem":
            if operand.base == "rip":
                base = "rip"
            elif operand.base is None:
                base = ""
            else:
                base = self.register(operand.base)
            index = f"+{self.register(operand.index)}*{operand.scale}" if operand.index is not None else ""
            displacement = "+sym" if operand.relocated else self.displacement(operand.displacement)
            return f"m{operand.size}[{base}{index}{displacement}]"
        return operand.text


def normalize(window: list[Instruction]) -> tuple[str, Normalizer]:
    normalizer = Normalizer()
    parts = []
    for instruction in window:
        width = 0
        for operand in instruction.operands:
            if operand.kind == "reg" and operand.family is not None:
                width = operand.width
                break
            if operand.kind == "mem":
                width = operand.size
                break
        rendered = [normalizer.operand(operand, width) for operand in instruction.operands]
        if instruction.target is not None:
            rendered.append("label")
        mnemonic = (instruction.prefix + " " if instruction.prefix else "") + instruction.mnemonic
        parts.append(mnemonic + (" " + ",".join(rendered) if rendered else ""))
    return " ; ".join(parts), normalizer


def enum_names(header: str, enum: str) -> list[str]:
    """Identifiers of a C enum with implicit sequential values, in order."""
    match = re.search(r"typedef enum " + enum + r"\s*\{(.*?)\}\s*" + enum + ";", header, re.S)
    if not match:
        raise ValueError(f"enum {enum} not found")
    body = re.sub(r"//[^\n]*", "", match.group(1))
    names = []
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            raise ValueError(f"explicit value in {enum}: {item}")
        names.append(item)
    return names


class Attribution:
    """ATTR <text offset> <tag> records from the research encoder trace.

    Tags: 0x8000|opcode for a MIR row, 0x4000|kind for an allocator edit,
    0x2000 for the prologue. An instruction's origin is the last record at
    or before its address."""

    def __init__(self, text: str, opcodes: list[str], edits: list[str]):
        records: dict[int, int] = {}
        for line in text.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[0] == "ATTR":
                records[int(parts[1], 16)] = int(parts[2], 16)
        self.offsets = sorted(records)
        self.tags = [records[offset] for offset in self.offsets]
        self.opcodes = opcodes
        self.edits = edits

    def name(self, tag: int) -> str:
        if tag & 0x8000:
            index = tag & 0x7fff
            name = self.opcodes[index] if index < len(self.opcodes) else f"op{index}"
            return name.replace("MACHINE_X64_", "row:")
        if tag & 0x4000:
            index = tag & 0x3fff
            name = self.edits[index] if index < len(self.edits) else f"edit{index}"
            return name.replace("MACHINE_EDIT_", "edit:")
        return "prologue"

    def origin(self, address: int) -> str:
        import bisect
        position = bisect.bisect_right(self.offsets, address) - 1
        return self.name(self.tags[position]) if position >= 0 else "unknown"


class Census:
    def __init__(self, max_window: int, examples: int):
        self.max_window = max_window
        self.examples = examples
        self.windows: dict[str, dict] = {}
        self.totals = collections.Counter()
        self.inputs: list[str] = []
        self.attribution: Attribution | None = None

    def add_function(self, source: str, name: str, instructions: list[Instruction]) -> None:
        if not instructions:
            return
        blocks, successors, padding = split_blocks(instructions)
        live_after = solve_liveness(blocks, successors)
        self.totals["functions"] += 1
        self.totals["padding_instructions"] += padding
        self.totals["padding_bytes"] += sum(i.size for i in instructions[len(instructions) - padding:])
        for block_index, block in enumerate(blocks):
            self.totals["blocks"] += 1
            self.totals["instructions"] += len(block)
            self.totals["bytes"] += sum(instruction.size for instruction in block)
            for start in range(len(block)):
                for length in range(1, self.max_window + 1):
                    if start + length > len(block):
                        break
                    window = block[start:start + length]
                    key, normalizer = normalize(window)
                    after = live_after[block_index][start + length - 1]
                    written = 0
                    for instruction in window:
                        written |= instruction.defs
                    signature = "".join(
                        name + ("L" if after & bit(family) else "D")
                        for family, name in sorted(normalizer.registers.items(), key=lambda item: item[1])
                        if written & bit(family))
                    flags_live = bool(after & ALL_FLAGS)
                    entry = self.windows.get(key)
                    if entry is None:
                        entry = {"length": length, "count": 0, "bytes": 0, "flags_live_after": 0,
                                 "liveness": collections.Counter(), "immediates": collections.Counter(),
                                 "origins": collections.Counter(), "examples": [], "block_end": 0}
                        self.windows[key] = entry
                    entry["count"] += 1
                    entry["bytes"] += sum(instruction.size for instruction in window)
                    entry["flags_live_after"] += flags_live
                    entry["block_end"] += start + length == len(block)
                    entry["liveness"][signature] += 1
                    if self.attribution is not None and name.startswith(".text:"):
                        entry["origins"][" ; ".join(self.attribution.origin(instruction.address)
                                                    for instruction in window)] += 1
                    for instruction in window:
                        for operand in instruction.operands:
                            if operand.kind == "imm":
                                entry["immediates"][hex(operand.value)] += 1
                    if len(entry["examples"]) < self.examples:
                        entry["examples"].append({"input": source, "function": name,
                                                  "address": hex(window[0].address),
                                                  "text": [instruction.text for instruction in window]})

    def to_json(self, top: int) -> dict:
        ranked = {}
        for length in range(1, self.max_window + 1):
            rows = [(key, value) for key, value in self.windows.items() if value["length"] == length]
            rows.sort(key=lambda item: (-item[1]["count"], item[0]))
            ranked[str(length)] = [
                {"pattern": key, "count": value["count"], "bytes": value["bytes"],
                 "flags_live_after": value["flags_live_after"], "block_end": value["block_end"],
                 "liveness": dict(value["liveness"].most_common(8)),
                 "immediates": dict(value["immediates"].most_common(8)),
                 "origins": dict(value["origins"].most_common(8)), "examples": value["examples"]}
                for key, value in rows[:top]]
        return {"version": VERSION, "inputs": self.inputs, "totals": dict(self.totals), "windows": ranked}


def disassemble(path: Path, objdump: str) -> str:
    completed = subprocess.run([objdump, "-d", "-r", "-M", "intel", "--insn-width=16", str(path)],
                               check=True, capture_output=True, text=True)
    return completed.stdout


def render_report(data: dict, top: int) -> str:
    totals = data["totals"]
    lines = [f"# Machine pattern census (version {data['version']})", "",
             f"Inputs: {len(data['inputs'])} objects; functions {totals.get('functions', 0)}; "
             f"blocks {totals.get('blocks', 0)}; instructions {totals.get('instructions', 0)}; "
             f"bytes {totals.get('bytes', 0)}; padding bytes {totals.get('padding_bytes', 0)}.", ""]
    for length, rows in data["windows"].items():
        lines += [f"## Windows of length {length}", "",
                  "| # | count | % insns | bytes | flags live after | top liveness | pattern | top origins |",
                  "|---:|---:|---:|---:|---:|---|---|---|"]
        for rank, row in enumerate(rows[:top], 1):
            share = 100.0 * row["count"] / max(1, totals.get("instructions", 1))
            liveness = ", ".join(f"{k or '-'}:{v}" for k, v in list(row["liveness"].items())[:3])
            pattern = row["pattern"].replace("|", "\\|")
            origins = "; ".join(f"{k}:{v}" for k, v in list(row.get("origins", {}).items())[:2]).replace("|", "\\|")
            lines.append(f"| {rank} | {row['count']} | {share:.2f} | {row['bytes']} | "
                         f"{row['flags_live_after']} | {liveness} | `{pattern}` | {origins} |")
        lines.append("")
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("objects", nargs="*", type=Path)
    parser.add_argument("--objdump", default="objdump")
    parser.add_argument("--window", type=int, default=3)
    parser.add_argument("--top", type=int, default=400)
    parser.add_argument("--report-top", type=int, default=60)
    parser.add_argument("--examples", type=int, default=3)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--attribution", type=Path,
                        help="research encoder ATTR trace for the single object input (see module docstring)")
    parser.add_argument("--machine-header", type=Path,
                        default=Path(__file__).resolve().parent.parent / "src/buster/lib/compiler/codegen/machine.h")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(CensusTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if not arguments.objects:
        parser.error("no objects")
    census = Census(arguments.window, arguments.examples)
    if arguments.attribution:
        if len(arguments.objects) != 1:
            parser.error("--attribution needs exactly one object")
        header = arguments.machine_header.read_text()
        census.attribution = Attribution(arguments.attribution.read_text(), enum_names(header, "MachineOpcode"),
                                         enum_names(header, "MachineEditKind"))
    for path in arguments.objects:
        census.inputs.append(str(path))
        for name, instructions in parse_objdump(disassemble(path, arguments.objdump)):
            census.add_function(path.name, name, instructions)
    data = census.to_json(arguments.top)
    if arguments.output:
        arguments.output.write_text(json.dumps(data, indent=1) + "\n")
    report = render_report(data, arguments.report_top)
    if arguments.report:
        arguments.report.write_text(report)
    else:
        sys.stdout.write(report)
    return 0


SAMPLE = """
Disassembly of section .text:

0000000000000000 <f>:
       0:\t55                   \tpush   rbp
       1:\t48 89 e5             \tmov    rbp,rsp
       4:\t48 89 85 d8 ff ff ff \tmov    QWORD PTR [rbp-0x28],rax
       b:\t48 8b 85 d8 ff ff ff \tmov    rax,QWORD PTR [rbp-0x28]
      12:\t48 85 c0             \ttest   rax,rax
      15:\t0f 85 05 00 00 00    \tjne    20 <f+0x20>
      1b:\te9 00 00 00 00       \tjmp    20 <f+0x20>
      20:\tb8 00 00 00 00       \tmov    eax,0x0
      25:\te8 00 00 00 00       \tcall   2a <f+0x2a>
\t\t\t26: R_X86_64_PLT32\tg-0x4
      2a:\t48 89 ec             \tmov    rsp,rbp
      2d:\t5d                   \tpop    rbp
      2e:\tc3                   \tret
      2f:\t90                   \tnop
"""


class CensusTests(unittest.TestCase):
    def test_parse_and_blocks(self) -> None:
        functions = parse_objdump(SAMPLE)
        self.assertEqual(len(functions), 1)
        name, instructions = functions[0]
        self.assertEqual(name, ".text:f")
        self.assertEqual(instructions[2].operands[0].kind, "mem")
        self.assertEqual(instructions[2].operands[0].displacement, -0x28)
        self.assertEqual(instructions[8].relocation, "g-0x4")
        blocks, successors, padding = split_blocks(instructions)
        self.assertEqual(padding, 1)
        self.assertEqual([len(block) for block in blocks], [6, 1, 5])
        self.assertEqual(successors[0], [2, 1])
        self.assertEqual(successors[1], [2])

    def test_liveness(self) -> None:
        _, instructions = parse_objdump(SAMPLE)[0]
        blocks, successors, _ = split_blocks(instructions)
        after = solve_liveness(blocks, successors)
        # The reloaded rax is read by test; flags are read by jne.
        self.assertTrue(after[0][3] & bit(0))
        self.assertTrue(after[0][4] & ZF)
        # mov eax,0 is followed by a call that reads rax (variadic AL).
        self.assertTrue(after[2][0] & bit(0))
        # Nothing reads the flags after the call.
        self.assertFalse(after[2][1] & ALL_FLAGS)

    def test_normalize(self) -> None:
        _, instructions = parse_objdump(SAMPLE)[0]
        key, _ = normalize(instructions[2:4])
        self.assertEqual(key, "mov m64[rbp+d0:8],A64 ; mov A64,m64[rbp+d0:8]")
        key, _ = normalize(instructions[9:11])
        self.assertEqual(key, "mov rsp64,rbp64 ; pop rbp64")

    def test_relative_displacements(self) -> None:
        normalizer = Normalizer()
        self.assertEqual(normalizer.displacement(-0x48), "+d0:8")
        self.assertEqual(normalizer.displacement(-0x88), "+d1:32")
        self.assertEqual(normalizer.displacement(-0x40), "+d0+8:8")
        self.assertEqual(normalizer.displacement(-0x80), "+d1+8:8")
        self.assertEqual(normalizer.displacement(-0x200), "+d2:32")

    def test_census(self) -> None:
        census = Census(3, 2)
        for name, instructions in parse_objdump(SAMPLE):
            census.add_function("sample.o", name, instructions)
        data = census.to_json(100)
        patterns = {row["pattern"]: row for row in data["windows"]["2"]}
        row = patterns["mov m64[rbp+d0:8],A64 ; mov A64,m64[rbp+d0:8]"]
        self.assertEqual(row["count"], 1)
        self.assertEqual(row["bytes"], 14)
        self.assertEqual(row["liveness"], {"AL": 1})
        self.assertEqual(data["totals"]["instructions"], 12)

    def test_attribution(self) -> None:
        header = "typedef enum MachineOpcode\n{\n    A, // c\n    B,\n} MachineOpcode;\n"
        self.assertEqual(enum_names(header, "MachineOpcode"), ["A", "B"])
        attribution = Attribution("ATTR 0 2000\nATTR 4 8001\nATTR b 4002\n", ["X", "MACHINE_X64_MOV_RR"],
                                  ["N", "R", "MACHINE_EDIT_SPILL"])
        self.assertEqual(attribution.origin(0), "prologue")
        self.assertEqual(attribution.origin(5), "row:MOV_RR")
        self.assertEqual(attribution.origin(0xb), "edit:SPILL")

    def test_immediate_classes(self) -> None:
        self.assertEqual(immediate_class(0xffffffff, 32), "m1")
        self.assertEqual(immediate_class(0x80, 32), "p2")
        self.assertEqual(immediate_class(0x7f, 32), "s8")
        self.assertEqual(immediate_class(0x12345, 64), "s32")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
