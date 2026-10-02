#!/usr/bin/env python3
"""Bounded search and verification of local x86-64 machine rewrites.

Research tool, run by hand; it is outside the build graph and the compiler
does not depend on it or on anything it imports. It consumes the window
census written by tools/machine_pattern_census.py and has three modes.

--search: for each frequent census window and each of its dominant
precondition classes (which window-written registers are live after it,
and whether any flag is), enumerate every replacement of at most
--max-length instructions from a closed vocabulary instantiated over the
window's own registers, memory operands and immediates plus one dead XMM
scratch, and keep those equivalent on every live output for all tested
inputs, cheapest first. Candidates are leads, not rules.

--verify: re-prove every rule in RULES, the accepted and rejected catalog,
with mechanisms that fail independently:

  1. exhaustive enumeration at a reduced register width (every register,
     cell, immediate and, where read, flag input) when the rule is
     structurally width-generic;
  2. randomized full-width testing with edge values;
  3. a Z3 bit-vector proof over all 64-bit inputs and all admissible
     addresses (optional import);
  4. execution of the real encodings of both sides in the Unicorn x86
     emulator (optional import), which shares nothing with this model and
     is compared against it as well;
  5. encoding with Buster's own assembler (--ide) and GNU as, decoding with
     objdump and Capstone (optional import), for legacy and REX registers;
  6. negative tests: each stated precondition is removed in turn and a
     counterexample must appear, so no precondition is decorative.

It also proves the control-flow rules (branch to the next instruction,
Jcc-over-JMP inversion, rel8/rel32 equivalence) over all 64 flag states.

The semantic domain is the integer and data-movement subset the Buster
x86-64 selector emits: general registers with 8/16/32/64-bit views (32-bit
writes zero-extend, 8/16-bit writes merge), 128-bit XMM registers used as
data (MOVUPS), the six arithmetic flags with x86 "undefined" results
modelled as undefined, byte-addressed memory, and the push/pop/leave stack
forms. Undefined equals nothing: a rewrite may leave a flag undefined only
where it is dead. No Buster-emitted consumer reads AF (Jcc, SETcc and CMOVcc
read CF/PF/ZF/SF/OF), so "all consumed flags" excludes AF.

Usage:
    python3 tools/machine_rewrite_search.py --self-test
    python3 tools/machine_rewrite_search.py --verify --ide build/Release/ide
    python3 tools/machine_rewrite_search.py --search census.json --top 40
"""
from __future__ import annotations

import argparse
import itertools
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from dataclasses import dataclass, field, replace
from pathlib import Path

FLAG_NAMES = ("CF", "PF", "AF", "ZF", "SF", "OF")
CONSUMED_FLAGS = ("CF", "PF", "ZF", "SF", "OF")
CONDITIONS = {
    "o": lambda f: f["OF"], "no": lambda f: 1 - f["OF"], "b": lambda f: f["CF"], "ae": lambda f: 1 - f["CF"],
    "e": lambda f: f["ZF"], "ne": lambda f: 1 - f["ZF"], "be": lambda f: f["CF"] | f["ZF"],
    "a": lambda f: (1 - f["CF"]) & (1 - f["ZF"]), "s": lambda f: f["SF"], "ns": lambda f: 1 - f["SF"],
    "p": lambda f: f["PF"], "np": lambda f: 1 - f["PF"], "l": lambda f: f["SF"] ^ f["OF"],
    "ge": lambda f: 1 - (f["SF"] ^ f["OF"]), "le": lambda f: f["ZF"] | (f["SF"] ^ f["OF"]),
    "g": lambda f: (1 - f["ZF"]) & (1 - (f["SF"] ^ f["OF"])),
}
CONDITION_READS = {
    "o": {"OF"}, "no": {"OF"}, "b": {"CF"}, "ae": {"CF"}, "e": {"ZF"}, "ne": {"ZF"}, "be": {"CF", "ZF"},
    "a": {"CF", "ZF"}, "s": {"SF"}, "ns": {"SF"}, "p": {"PF"}, "np": {"PF"}, "l": {"SF", "OF"},
    "ge": {"SF", "OF"}, "le": {"ZF", "SF", "OF"}, "g": {"ZF", "SF", "OF"},
}
# x86 condition-code nibbles (Jcc rel8 = 0x70+cc, Jcc rel32 = 0F 80+cc).
CONDITION_CODES = {"o": 0, "no": 1, "b": 2, "ae": 3, "e": 4, "ne": 5, "be": 6, "a": 7,
                   "s": 8, "ns": 9, "p": 10, "np": 11, "l": 12, "ge": 13, "le": 14, "g": 15}
INVERSE_CONDITION = {"o": "no", "no": "o", "b": "ae", "ae": "b", "e": "ne", "ne": "e", "be": "a", "a": "be",
                     "s": "ns", "ns": "s", "p": "np", "np": "p", "l": "ge", "ge": "l", "le": "g", "g": "le"}
LEGACY_GPRS = ["rax", "rcx", "rdx", "rbx", "rsi", "rdi"]
EXTENDED_GPRS = ["r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
REGISTER_VIEWS = {
    "rax": ("rax", "eax", "ax", "al"), "rcx": ("rcx", "ecx", "cx", "cl"), "rdx": ("rdx", "edx", "dx", "dl"),
    "rbx": ("rbx", "ebx", "bx", "bl"), "rsi": ("rsi", "esi", "si", "sil"), "rdi": ("rdi", "edi", "di", "dil"),
    "rsp": ("rsp", "esp", "sp", "spl"), "rbp": ("rbp", "ebp", "bp", "bpl"),
    **{f"r{n}": (f"r{n}", f"r{n}d", f"r{n}w", f"r{n}b") for n in range(8, 16)},
}
WIDTH_INDEX = {64: 0, 32: 1, 16: 2, 8: 3}
SIZE_WORD = {8: "BYTE", 16: "WORD", 32: "DWORD", 64: "QWORD", 128: "XMMWORD"}
MASK64 = (1 << 64) - 1


# --------------------------------------------------------------------------
# Instruction representation


@dataclass(frozen=True)
class Reg:
    name: str      # window token (A, B, ..., P for a pointer, X for XMM) or rsp/rbp
    width: int     # 8/16/32/64 general views; 128 is an XMM register


@dataclass(frozen=True)
class Mem:
    base: str | None
    displacement: object  # int, a token "d0", or (token, byte offset)
    width: int
    index: str | None = None
    scale: int = 1


@dataclass(frozen=True)
class Imm:
    value: object  # int, or a token such as "k0"


@dataclass(frozen=True)
class Insn:
    mnemonic: str
    operands: tuple = ()

    def __str__(self) -> str:
        return render(self, None)


def displacement_token(displacement) -> str | None:
    if isinstance(displacement, str):
        return displacement
    if isinstance(displacement, tuple):
        return displacement[0]
    return None


def displacement_value(displacement, table):
    """Concrete int, or (for Z3) whatever the table holds plus the offset."""
    if isinstance(displacement, int):
        return displacement
    if isinstance(displacement, str):
        return table[displacement]
    return table[displacement[0]] + displacement[1]


@dataclass
class Binding:
    registers: dict
    displacements: dict
    immediates: dict


def physical_name(name: str, width: int, binding: Binding | None) -> str:
    physical = binding.registers.get(name, name) if binding else name
    if width == 128:
        return physical if physical.startswith("xmm") else f"{name}128"
    if physical in REGISTER_VIEWS:
        return REGISTER_VIEWS[physical][WIDTH_INDEX[width]]
    return f"{name}{width}"


def render_operand(operand, binding: Binding | None) -> str:
    if isinstance(operand, Reg):
        return physical_name(operand.name, operand.width, binding)
    if isinstance(operand, Imm):
        value = binding.immediates.get(operand.value, operand.value) if binding else operand.value
        return hex(value) if isinstance(value, int) else f"#{value}"
    if isinstance(operand, Mem):
        parts = []
        if operand.base is not None:
            parts.append(physical_name(operand.base, 64, binding))
        if operand.index is not None:
            parts.append(f"{physical_name(operand.index, 64, binding)}*{operand.scale}")
        text = "+".join(parts)
        try:
            value = displacement_value(operand.displacement, binding.displacements) if binding else operand.displacement
        except KeyError:
            value = operand.displacement
        if isinstance(value, int):
            if value:
                text += ("-" if value < 0 else "+") + hex(abs(value))
        elif isinstance(value, tuple):
            text += f"+{value[0]}{value[1]:+d}"
        else:
            text += f"+{value}"
        size = SIZE_WORD.get(operand.width)
        return (f"{size} PTR " if size else "") + f"[{text}]"
    raise TypeError(operand)


def render(insn: Insn, binding: Binding | None) -> str:
    return insn.mnemonic + (" " + ", ".join(render_operand(o, binding) for o in insn.operands) if insn.operands else "")


# --------------------------------------------------------------------------
# Width-generic semantics over a backend (concrete integers or Z3)


class Concrete:
    """Python-integer backend. Bits are 0/1 ints; undefined is None."""

    def const(self, value: int, width: int) -> int:
        return value & ((1 << width) - 1)

    def add(self, a, b, width):
        return (a + b) & ((1 << width) - 1)

    def sub(self, a, b, width):
        return (a - b) & ((1 << width) - 1)

    def band(self, a, b, width):
        return a & b

    def bor(self, a, b, width):
        return a | b

    def bxor(self, a, b, width):
        return a ^ b

    def bnot(self, a, width):
        return ~a & ((1 << width) - 1)

    def extract(self, a, high, low):
        return (a >> low) & ((1 << (high - low + 1)) - 1)

    def zext(self, a, from_width, to_width):
        return a

    def sext(self, a, from_width, to_width):
        if a >> (from_width - 1) & 1:
            return a | (((1 << to_width) - 1) ^ ((1 << from_width) - 1))
        return a

    def concat(self, high, low, low_width):
        return (high << low_width) | low

    def shl(self, a, count, width):
        return (a << count) & ((1 << width) - 1)

    def lshr(self, a, count, width):
        return a >> count

    def ashr(self, a, count, width):
        return self.extract(self.sext(a, width, 2 * width) >> count, width - 1, 0)

    def mul(self, a, b, width):
        return (a * b) & ((1 << width) - 1)

    def eq(self, a, b):
        return int(a == b)

    def is_zero(self, a):
        return int(a == 0)

    def bit(self, a, index):
        return (a >> index) & 1

    def bit_not(self, a):
        return 1 - a

    def zero_bit(self):
        return 0

    def parity8(self, a):
        return 1 - (bin(a & 0xff).count("1") & 1)


class Z3Backend:
    """Z3 bit-vector backend; bits are 1-bit vectors."""

    def __init__(self, z3):
        self.z3 = z3

    def const(self, value: int, width: int):
        return self.z3.BitVecVal(value & ((1 << width) - 1), width)

    def add(self, a, b, width):
        return a + b

    def sub(self, a, b, width):
        return a - b

    def band(self, a, b, width):
        return a & b

    def bor(self, a, b, width):
        return a | b

    def bxor(self, a, b, width):
        return a ^ b

    def bnot(self, a, width):
        return ~a

    def extract(self, a, high, low):
        return self.z3.Extract(high, low, a)

    def zext(self, a, from_width, to_width):
        return self.z3.ZeroExt(to_width - from_width, a) if to_width > from_width else a

    def sext(self, a, from_width, to_width):
        return self.z3.SignExt(to_width - from_width, a) if to_width > from_width else a

    def concat(self, high, low, low_width):
        return self.z3.Concat(high, low)

    def shl(self, a, count, width):
        return a << count

    def lshr(self, a, count, width):
        return self.z3.LShR(a, count)

    def ashr(self, a, count, width):
        return a >> count

    def mul(self, a, b, width):
        return a * b

    def eq(self, a, b):
        return self.z3.If(a == b, self.z3.BitVecVal(1, 1), self.z3.BitVecVal(0, 1))

    def is_zero(self, a):
        return self.z3.If(a == 0, self.z3.BitVecVal(1, 1), self.z3.BitVecVal(0, 1))

    def bit(self, a, index):
        return self.z3.Extract(index, index, a)

    def bit_not(self, a):
        return ~a

    def zero_bit(self):
        return self.z3.BitVecVal(0, 1)

    def parity8(self, a):
        folded = self.z3.BitVecVal(1, 1)
        for index in range(8):
            folded = folded ^ self.z3.Extract(index, index, a)
        return folded


class ByteMemory:
    """Byte-addressed memory over concrete 64-bit addresses."""

    def __init__(self, seed: int):
        self.seed = seed
        self.bytes: dict[int, int] = {}
        self.written: set[int] = set()

    def initial(self, address: int) -> int:
        return ((address * 0x9E3779B97F4A7C15 + self.seed * 0xBF58476D1CE4E5B9) >> 29) & 0xff

    def load(self, backend, address, bits):
        value = 0
        for index in range(bits // 8):
            location = (address + index) & MASK64
            value |= self.bytes.get(location, self.initial(location)) << (8 * index)
        return value

    def store(self, backend, address, value, bits):
        for index in range(bits // 8):
            location = (address + index) & MASK64
            self.bytes[location] = (value >> (8 * index)) & 0xff
            self.written.add(location)


class Z3Memory:
    def __init__(self, z3, name: str):
        self.z3 = z3
        self.array = z3.Array(name, z3.BitVecSort(64), z3.BitVecSort(8))

    def load(self, backend, address, bits):
        parts = [self.z3.Select(self.array, address + index) for index in range(bits // 8)]
        return parts[0] if len(parts) == 1 else self.z3.Concat(*reversed(parts))

    def store(self, backend, address, value, bits):
        for index in range(bits // 8):
            self.array = self.z3.Store(self.array, address + index, self.z3.Extract(8 * index + 7, 8 * index, value))


class CellMemory:
    """Reduced-width memory: one W-bit cell per distinct memory operand.

    Valid only for rules whose memory operands are distinct, non-overlapping
    frame slots; full-width byte memory checks that precondition separately."""

    def __init__(self, width: int, initial: dict):
        self.width = width
        self.cells = dict(initial)

    def load(self, backend, key, bits):
        return backend.extract(self.cells[key], bits - 1, 0) if bits < self.width else self.cells[key]

    def store(self, backend, key, value, bits):
        if bits < self.width:
            value = backend.concat(backend.extract(self.cells[key], self.width - 1, bits), value, bits)
        self.cells[key] = value


class UnsupportedInstruction(Exception):
    pass


class State:
    """Machine state at general-register width `width` (64 for real x86)."""

    def __init__(self, backend, width: int, registers: dict, flags: dict, memory, symbols=None):
        self.k = backend
        self.width = width
        self.registers = dict(registers)
        self.flags = dict(flags)
        self.memory = memory
        self.symbols = symbols or {}

    def view(self, bits: int) -> int:
        """Architectural width `bits` projected onto the model width."""
        if bits == 128:
            if self.width != 64:
                raise UnsupportedInstruction("XMM at reduced width")
            return 128
        return bits * self.width // 64

    def read_register(self, operand: Reg):
        bits = self.view(operand.width)
        value = self.registers[operand.name]
        return self.k.extract(value, bits - 1, 0) if bits < self.width else value

    def write_register(self, operand: Reg, value) -> None:
        bits = self.view(operand.width)
        if operand.width in (64, 128):
            self.registers[operand.name] = value
        elif operand.width == 32:
            self.registers[operand.name] = self.k.zext(value, bits, self.width)
        else:
            old = self.registers[operand.name]
            self.registers[operand.name] = self.k.concat(self.k.extract(old, self.width - 1, bits), value, bits)

    def address(self, operand: Mem):
        if isinstance(self.memory, CellMemory):
            return (operand.base, operand.index, operand.scale, operand.displacement)
        value = self.k.const(0, 64)
        if operand.base is not None:
            value = self.k.add(value, self.registers[operand.base], 64)
        if operand.index is not None:
            value = self.k.add(value, self.k.mul(self.registers[operand.index], self.k.const(operand.scale, 64), 64), 64)
        displacement = displacement_value(operand.displacement, self.symbols)
        if isinstance(displacement, int):
            displacement = self.k.const(displacement, 64)
        return self.k.add(value, displacement, 64)

    def immediate(self, operand: Imm, architectural: int):
        """x86 immediate at the operation width (sign-extension is the caller's
        job for imm32 forms; values are stored at their architectural width)."""
        bits = self.view(architectural)
        value = self.symbols.get(operand.value, operand.value)
        if isinstance(value, int):
            return self.k.const(value, bits)
        return value if architectural == 64 or not hasattr(value, "size") else value

    def read(self, operand, architectural: int):
        if isinstance(operand, Reg):
            return self.read_register(operand)
        if isinstance(operand, Imm):
            return self.immediate(operand, architectural)
        if isinstance(operand, Mem):
            return self.memory.load(self.k, self.address(operand), self.view(operand.width))
        raise UnsupportedInstruction(repr(operand))

    def write(self, operand, value) -> None:
        if isinstance(operand, Reg):
            self.write_register(operand, value)
        elif isinstance(operand, Mem):
            self.memory.store(self.k, self.address(operand), value, self.view(operand.width))
        else:
            raise UnsupportedInstruction(repr(operand))

    def result_flags(self, result, bits: int) -> None:
        self.flags["ZF"] = self.k.is_zero(result)
        self.flags["SF"] = self.k.bit(result, bits - 1)
        self.flags["PF"] = self.k.parity8(result) if bits >= 8 else None

    def defined(self, names) -> dict:
        for name in names:
            if self.flags[name] is None:
                raise UnsupportedInstruction("read of undefined flag " + name)
        return self.flags

    def require_full_width(self) -> None:
        if self.width != 64 or isinstance(self.memory, CellMemory):
            raise UnsupportedInstruction("stack forms need full-width byte memory")

    def execute(self, insn: Insn) -> None:
        k = self.k
        m = insn.mnemonic
        ops = insn.operands
        if m == "nop":
            return
        if m in ("mov", "movabs"):
            destination, source = ops
            self.write(destination, self.read(source, destination.width))
            return
        if m in ("movups", "movdqu"):
            destination, source = ops
            if destination.width != 128 or source.width != 128:
                raise UnsupportedInstruction("movups width")
            self.write(destination, self.read(source, 128))
            return
        if m in ("movzx", "movsx", "movsxd"):
            destination, source = ops
            bits_from = self.view(source.width)
            bits_to = self.view(destination.width)
            value = self.read(source, source.width)
            value = k.zext(value, bits_from, bits_to) if m == "movzx" else k.sext(value, bits_from, bits_to)
            self.write(destination, value)
            return
        if m == "lea":
            destination, source = ops
            if isinstance(self.memory, CellMemory):
                raise UnsupportedInstruction("lea in cell memory")
            address = self.address(source)
            bits = self.view(destination.width)
            self.write(destination, k.extract(address, bits - 1, 0) if bits < 64 else address)
            return
        if m in ("add", "sub", "cmp", "and", "or", "xor", "test"):
            destination, source = ops
            architectural = destination.width
            bits = self.view(architectural)
            a = self.read(destination, architectural)
            if isinstance(source, Imm):
                b = self.immediate(source, architectural)
            else:
                b = self.read(source, architectural)
            if m == "add":
                wide = k.add(k.zext(a, bits, bits + 1), k.zext(b, bits, bits + 1), bits + 1)
                result = k.extract(wide, bits - 1, 0)
                self.flags["CF"] = k.bit(wide, bits)
                self.flags["OF"] = k.bit(k.band(k.bnot(k.bxor(a, b, bits), bits), k.bxor(a, result, bits), bits), bits - 1)
                self.flags["AF"] = k.bit(k.bxor(k.bxor(a, b, bits), result, bits), 4) if bits > 4 else None
                self.result_flags(result, bits)
                self.write(destination, result)
            elif m in ("sub", "cmp"):
                wide = k.sub(k.zext(a, bits, bits + 1), k.zext(b, bits, bits + 1), bits + 1)
                result = k.extract(wide, bits - 1, 0)
                self.flags["CF"] = k.bit(wide, bits)
                self.flags["OF"] = k.bit(k.band(k.bxor(a, b, bits), k.bxor(a, result, bits), bits), bits - 1)
                self.flags["AF"] = k.bit(k.bxor(k.bxor(a, b, bits), result, bits), 4) if bits > 4 else None
                self.result_flags(result, bits)
                if m == "sub":
                    self.write(destination, result)
            else:
                operation = {"and": k.band, "or": k.bor, "xor": k.bxor, "test": k.band}[m]
                result = operation(a, b, bits)
                self.flags["CF"] = k.zero_bit()
                self.flags["OF"] = k.zero_bit()
                self.flags["AF"] = None
                self.result_flags(result, bits)
                if m != "test":
                    self.write(destination, result)
            return
        if m in ("inc", "dec", "neg", "not"):
            (destination,) = ops
            bits = self.view(destination.width)
            a = self.read(destination, destination.width)
            if m == "not":
                self.write(destination, k.bnot(a, bits))
                return
            one = k.const(1, bits)
            if m == "inc":
                result = k.add(a, one, bits)
                self.flags["OF"] = k.bit(k.band(k.bnot(a, bits), result, bits), bits - 1)
                self.flags["AF"] = k.bit(k.bxor(k.bxor(a, one, bits), result, bits), 4) if bits > 4 else None
            elif m == "dec":
                result = k.sub(a, one, bits)
                self.flags["OF"] = k.bit(k.band(a, k.bnot(result, bits), bits), bits - 1)
                self.flags["AF"] = k.bit(k.bxor(k.bxor(a, one, bits), result, bits), 4) if bits > 4 else None
            else:
                result = k.sub(k.const(0, bits), a, bits)
                self.flags["CF"] = k.bit_not(k.is_zero(a))
                self.flags["OF"] = k.bit(k.band(a, result, bits), bits - 1)
                self.flags["AF"] = k.bit(k.bxor(a, result, bits), 4) if bits > 4 else None
            self.result_flags(result, bits)
            self.write(destination, result)
            return
        if m in ("shl", "shr", "sar"):
            destination, source = ops
            if not isinstance(source, Imm) or not isinstance(source.value, int):
                raise UnsupportedInstruction("variable shift")
            if self.width != 64:
                raise UnsupportedInstruction("shift counts do not scale")
            bits = destination.width
            count = source.value & (63 if bits == 64 else 31)
            if count == 0:
                return
            a = self.read(destination, bits)
            if m == "shl":
                result = k.shl(a, count, bits)
                self.flags["CF"] = k.bit(a, bits - count)
                self.flags["OF"] = (k.bit(result, bits - 1) ^ self.flags["CF"]) if count == 1 else None
            elif m == "shr":
                result = k.lshr(a, count, bits)
                self.flags["CF"] = k.bit(a, count - 1)
                self.flags["OF"] = k.bit(a, bits - 1) if count == 1 else None
            else:
                result = k.ashr(a, count, bits)
                self.flags["CF"] = k.bit(a, count - 1)
                self.flags["OF"] = k.zero_bit() if count == 1 else None
            self.flags["AF"] = None
            self.result_flags(result, bits)
            self.write(destination, result)
            return
        if m == "imul" and len(ops) in (2, 3):
            destination = ops[0]
            left, right = (ops[0], ops[1]) if len(ops) == 2 else (ops[1], ops[2])
            architectural = destination.width
            bits = self.view(architectural)
            a = self.read(left, architectural)
            b = self.immediate(right, architectural) if isinstance(right, Imm) else self.read(right, architectural)
            full = k.mul(k.sext(a, bits, 2 * bits), k.sext(b, bits, 2 * bits), 2 * bits)
            result = k.extract(full, bits - 1, 0)
            overflow = k.bit_not(k.eq(k.sext(result, bits, 2 * bits), full))
            self.flags.update({"CF": overflow, "OF": overflow, "SF": None, "ZF": None, "PF": None, "AF": None})
            self.write(destination, result)
            return
        if m.startswith("set") and m[3:] in CONDITIONS:
            (destination,) = ops
            value = CONDITIONS[m[3:]](self.defined(CONDITION_READS[m[3:]]))
            if isinstance(value, int):
                value = k.const(value, 1)
            bits = self.view(8)
            self.write(destination, k.zext(value, 1, bits) if bits > 1 else value)
            return
        if m == "push":
            (source,) = ops
            self.require_full_width()
            self.registers["rsp"] = k.sub(self.registers["rsp"], k.const(8, 64), 64)
            self.memory.store(k, self.registers["rsp"], self.read(source, 64), 64)
            return
        if m == "pop":
            (destination,) = ops
            self.require_full_width()
            value = self.memory.load(k, self.registers["rsp"], 64)
            self.registers["rsp"] = k.add(self.registers["rsp"], k.const(8, 64), 64)
            self.write(destination, value)
            return
        if m == "leave":
            self.require_full_width()
            self.registers["rsp"] = self.registers["rbp"]
            self.registers["rbp"] = self.memory.load(k, self.registers["rsp"], 64)
            self.registers["rsp"] = k.add(self.registers["rsp"], k.const(8, 64), 64)
            return
        raise UnsupportedInstruction(m)


# --------------------------------------------------------------------------
# Rules: pattern, replacement, preconditions


@dataclass
class Rule:
    name: str
    pattern: list
    replacement: list
    # Tokens whose value after the window must be preserved. Every other
    # register the pattern writes is dead after it; registers the pattern
    # only reads are inputs and must be preserved as well (they are live
    # unless listed in `scratch`).
    live_registers: tuple = ()
    # Registers dead on entry and exit that the replacement may clobber
    # (the pattern's own data scratches, a fresh XMM).
    scratch: tuple = ()
    # Flags live after the window: "all" (every consumed flag) or a tuple.
    live_flags: object = "all"
    # Immediate tokens: name -> (class, architectural width).
    immediates: dict = field(default_factory=dict)
    # Bytes each frame displacement token's object spans; distinct tokens
    # name distinct, non-overlapping frame objects.
    slot_extent: int = 8
    # Pointer base registers -> bytes their object spans. The object is
    # either disjoint from every frame object or exactly equal to one (C11
    # 6.5.16.1p3: an overlapping assignment must overlap exactly).
    pointers: dict = field(default_factory=dict)
    # accepted: proved and integrated. deferred: proved, not integrated (the
    # integration names why). rejected-policy: proved but refused on cost or
    # policy grounds. rejected-invalid: not an equivalence; the proofs must
    # find a counterexample.
    status: str = "accepted"
    rationale: str = ""
    # Precondition name -> mutation that removes it (negative tests).
    negative: dict = field(default_factory=dict)
    # The overlap mode a negative test uses to violate the aliasing precondition.
    overlap: bool = False
    reduced_width: bool = True
    stack: bool = False
    integration: str = ""
    coverage: str = ""


def operands_of(rule: Rule):
    for insn in rule.pattern + rule.replacement:
        yield from insn.operands


def register_widths(rule: Rule) -> dict:
    widths: dict[str, int] = {}
    for operand in operands_of(rule):
        names = [(operand.name, operand.width)] if isinstance(operand, Reg) else \
            [(operand.base, 64), (operand.index, 64)] if isinstance(operand, Mem) else []
        for name, width in names:
            if name:
                widths[name] = max(widths.get(name, 0), 128 if width == 128 else 64)
    return widths


def memory_operands(rule: Rule) -> list:
    result = []
    for operand in operands_of(rule):
        if isinstance(operand, Mem) and operand not in result:
            result.append(operand)
    return result


def displacement_tokens(rule: Rule) -> list:
    tokens = []
    for operand in memory_operands(rule):
        token = displacement_token(operand.displacement)
        if token and token not in tokens:
            tokens.append(token)
    return tokens


def immediate_tokens(rule: Rule) -> list:
    tokens = []
    for operand in operands_of(rule):
        if isinstance(operand, Imm) and isinstance(operand.value, str) and operand.value not in tokens:
            tokens.append(operand.value)
    return tokens


def live_flag_set(rule: Rule) -> tuple:
    return CONSUMED_FLAGS if rule.live_flags == "all" else tuple(rule.live_flags)


def preserved_registers(rule: Rule) -> list:
    """Registers whose final value is compared: declared live ones plus the
    pattern's read-only inputs (a replacement must not clobber them)."""
    written = {insn.operands[0].name for insn in rule.pattern
               if insn.operands and isinstance(insn.operands[0], Reg) and insn.mnemonic not in ("cmp", "test", "push")}
    names = set(rule.live_registers)
    for name in register_widths(rule):
        if name not in written and name not in rule.scratch and name not in ("rsp", "rbp"):
            names.add(name)
    return sorted(names)


EDGE_VALUES = [0, 1, 2, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff,
               0x100000000, 0x7fffffffffffffff, 0x8000000000000000, 0xffffffffffffffff, 0xfffffffffffffffe]


def random_value(generator: random.Random, width: int = 64) -> int:
    choice = generator.random()
    if choice < 0.25:
        value = generator.choice(EDGE_VALUES)
    elif choice < 0.5:
        value = generator.getrandbits(generator.choice((8, 16, 32)))
    elif choice < 0.6:
        value = (-generator.getrandbits(generator.choice((7, 15, 31)))) & MASK64
    else:
        value = generator.getrandbits(64)
    if width == 128:
        value |= generator.getrandbits(64) << 64
    return value


def sample_immediate(rule: Rule, token: str, generator: random.Random) -> int:
    predicate, width = rule.immediates[token]
    for _ in range(100000):
        value = random_value(generator) & ((1 << width) - 1)
        if generator.random() < 0.3:
            value = generator.choice(EDGE_VALUES + [(-v) & MASK64 for v in EDGE_VALUES]) & ((1 << width) - 1)
        if predicate(value, width):
            return value
    raise RuntimeError(f"no admissible value for {token}")


def make_binding(rule: Rule, generator: random.Random, register_class: str = "random") -> Binding:
    widths = register_widths(rule)
    general = [name for name in widths if widths[name] != 128 and name not in ("rsp", "rbp")]
    vectors = [name for name in widths if widths[name] == 128]
    if register_class == "legacy":
        pool = list(LEGACY_GPRS)
        vector_pool = [f"xmm{n}" for n in range(8)]
    elif register_class == "extended":
        pool = list(EXTENDED_GPRS)
        vector_pool = [f"xmm{n}" for n in range(8, 16)]
    else:
        pool = LEGACY_GPRS + EXTENDED_GPRS
        generator.shuffle(pool)
        vector_pool = [f"xmm{n}" for n in range(16)]
        generator.shuffle(vector_pool)
    registers = {"rsp": "rsp", "rbp": "rbp"}
    for name in general:
        registers[name] = pool.pop(0)
    for name in vectors:
        registers[name] = vector_pool.pop(0)
    displacements = {}
    used = []
    for token in displacement_tokens(rule):
        for _ in range(10000):
            value = -8 * generator.randint(2 * rule.slot_extent // 8, 400)
            if all(abs(value - other) >= rule.slot_extent for other in used):
                break
        used.append(value)
        displacements[token] = value
    immediates = {token: sample_immediate(rule, token, generator) for token in immediate_tokens(rule)}
    return Binding(registers, displacements, immediates)


def initial_state(rule: Rule, binding: Binding, generator: random.Random, overlap: bool = False) -> tuple[dict, dict]:
    widths = register_widths(rule)
    registers = {name: random_value(generator, widths[name]) for name in widths}
    base = 0x7ff0_0000_0000 + 16 * generator.randint(0, 1 << 20)
    registers["rbp"] = base
    registers["rsp"] = base - 16 * generator.randint(1, 512)
    if rule.stack:
        registers["rbp"] = registers["rsp"] + 8 * generator.randint(0, 64)
    tokens = list(binding.displacements.values())
    for name, extent in rule.pointers.items():
        if overlap and tokens:
            # Partial overlap with a frame object: the violated precondition.
            registers[name] = registers["rbp"] + tokens[0] + generator.choice((-8, -4, -1, 1, 4, 8))
        elif tokens and generator.random() < 0.2:
            # Exact overlap is admissible: the object is the frame object.
            registers[name] = registers["rbp"] + tokens[0]
        else:
            registers[name] = registers["rbp"] + 0x800 + 8 * generator.randint(0, 64)
    flags = {name: generator.getrandbits(1) for name in FLAG_NAMES}
    return registers, flags


def run_concrete(sequence: list, rule: Rule, binding: Binding, registers: dict, flags: dict, seed: int) -> State:
    state = State(Concrete(), 64, registers, flags, ByteMemory(seed), {**binding.displacements, **binding.immediates})
    for insn in sequence:
        state.execute(insn)
    return state


def compare(rule: Rule, left: State, right: State) -> str | None:
    """None when equivalent on every live output, else a description."""
    for name in list(preserved_registers(rule)) + ["rsp", "rbp"]:
        if left.registers.get(name) != right.registers.get(name):
            return f"register {name}: {left.registers.get(name)!r} != {right.registers.get(name)!r}"
    for flag in live_flag_set(rule):
        if left.flags[flag] is None:
            continue
        if right.flags[flag] is None or left.flags[flag] != right.flags[flag]:
            return f"flag {flag}: {left.flags[flag]} != {right.flags[flag]}"
    for location in sorted(left.memory.written | right.memory.written):
        a = left.memory.bytes.get(location, left.memory.initial(location))
        b = right.memory.bytes.get(location, right.memory.initial(location))
        if a != b:
            return f"memory {location:#x}: {a:#x} != {b:#x}"
    return None


def random_check(rule: Rule, trials: int, seed: int, overlap: bool = False) -> str | None:
    generator = random.Random(seed)
    for trial in range(trials):
        binding = make_binding(rule, generator)
        registers, flags = initial_state(rule, binding, generator, overlap)
        left = run_concrete(rule.pattern, rule, binding, registers, flags, seed + trial)
        right = run_concrete(rule.replacement, rule, binding, registers, flags, seed + trial)
        difference = compare(rule, left, right)
        if difference:
            return f"trial {trial}: {difference}; binding {binding}"
    return None


def reduced_width_check(rule: Rule, width: int) -> tuple[str, int]:
    """Exhaustive over every register, cell, immediate and read flag at
    `width` bits. Returns (status, cases)."""
    if not rule.reduced_width or rule.stack or rule.pointers:
        return "skipped: not width-generic", 0
    widths = register_widths(rule)
    if any(value == 128 for value in widths.values()):
        return "skipped: XMM data is not width-generic", 0
    names = [name for name in widths if name not in ("rsp", "rbp")]
    cells = []
    for operand in memory_operands(rule):
        key = (operand.base, operand.index, operand.scale, operand.displacement)
        if key not in cells:
            cells.append(key)
    for insn in rule.pattern + rule.replacement:
        for operand in insn.operands:
            if isinstance(operand, Imm) and isinstance(operand.value, int) and operand.value not in (0, 1):
                return "skipped: literal immediate does not scale", 0
    tokens = immediate_tokens(rule)
    free = len(names) + len(cells) + len(tokens)
    if free * width > 20:
        return "skipped: input space too large", 0
    backend = Concrete()
    reads_flags = any(insn.mnemonic.startswith(("set", "j", "cmov")) for insn in rule.pattern)
    flag_inputs = list(itertools.product((0, 1), repeat=6)) if reads_flags else [(0,) * 6]
    preserved = preserved_registers(rule)
    cases = 0
    for values in itertools.product(range(1 << width), repeat=free):
        register_values = dict(zip(names, values[:len(names)]))
        cell_values = dict(zip(cells, values[len(names):len(names) + len(cells)]))
        immediate_values = dict(zip(tokens, values[len(names) + len(cells):]))
        admissible = True
        for token, value in immediate_values.items():
            predicate, architectural = rule.immediates[token]
            scaled = architectural * width // 64
            if value >> scaled or not predicate(value, scaled):
                admissible = False
        if not admissible:
            continue
        for flag_values in flag_inputs:
            flags = dict(zip(FLAG_NAMES, flag_values))
            results = []
            try:
                for sequence in (rule.pattern, rule.replacement):
                    state = State(backend, width, register_values, flags, CellMemory(width, cell_values), immediate_values)
                    for insn in sequence:
                        state.execute(insn)
                    results.append(state)
            except UnsupportedInstruction as error:
                return f"skipped: {error}", cases
            cases += 1
            left, right = results
            for name in preserved:
                if left.registers[name] != right.registers[name]:
                    return f"FAILED at width {width}: {name} {register_values} {cell_values} {immediate_values}", cases
            for flag in live_flag_set(rule):
                if left.flags[flag] is not None and left.flags[flag] != right.flags[flag]:
                    return f"FAILED at width {width}: flag {flag} {register_values} {cell_values}", cases
            if left.memory.cells != right.memory.cells:
                return f"FAILED at width {width}: memory {register_values} {cell_values}", cases
    return "proved", cases


def z3_check(rule: Rule, allow_overlap: bool = False) -> str:
    try:
        import z3
    except ImportError:
        return "skipped: z3 not installed"
    backend = Z3Backend(z3)
    widths = register_widths(rule)
    registers = {name: z3.BitVec(name, widths[name]) for name in widths}
    registers.setdefault("rbp", z3.BitVec("rbp", 64))
    registers.setdefault("rsp", z3.BitVec("rsp", 64))
    flags = {name: z3.BitVec(name, 1) for name in FLAG_NAMES}
    displacements = {token: z3.BitVec(token, 64) for token in displacement_tokens(rule)}
    symbols = dict(displacements)
    constraints = []
    low, high = z3.BitVecVal(1 << 32, 64), z3.BitVecVal(1 << 60, 64)
    rbp = registers["rbp"]
    constraints += [rbp & 7 == 0, z3.UGT(rbp, low), z3.ULT(rbp, high)]
    for token, symbol in displacements.items():
        constraints += [symbol & 7 == 0, symbol < 0, symbol > -(1 << 24)]
    for a, b in itertools.combinations(displacements.values(), 2):
        constraints.append(z3.Or(a - b >= rule.slot_extent, b - a >= rule.slot_extent))
    for name, extent in rule.pointers.items():
        pointer = registers[name]
        constraints += [z3.UGT(pointer, low), z3.ULT(pointer, high)]
        if not allow_overlap:
            for symbol in displacements.values():
                slot = rbp + symbol
                constraints.append(z3.Or(z3.ULE(pointer + extent, slot), z3.ULE(slot + rule.slot_extent, pointer),
                                         pointer == slot))
    for token in immediate_tokens(rule):
        predicate, width = rule.immediates[token]
        symbol = z3.BitVec(token, width)
        symbols[token] = symbol
        constraint = getattr(predicate, "z3", None)
        if constraint is None:
            return f"skipped: immediate class {token} has no z3 form"
        constraints.append(constraint(z3, symbol, width))
    if rule.stack:
        constraints += [registers["rsp"] & 7 == 0, z3.UGE(rbp, registers["rsp"]), z3.UGT(registers["rsp"], low)]
    memories = [Z3Memory(z3, "memory"), Z3Memory(z3, "memory")]
    try:
        states = []
        for sequence, memory in ((rule.pattern, memories[0]), (rule.replacement, memories[1])):
            state = State(backend, 64, registers, flags, memory, symbols)
            for insn in sequence:
                state.execute(insn)
            states.append(state)
    except UnsupportedInstruction as error:
        return f"skipped: {error}"
    left, right = states
    differences = [left.registers[name] != right.registers[name] for name in preserved_registers(rule) + ["rsp", "rbp"]]
    for flag in live_flag_set(rule):
        if left.flags[flag] is None:
            continue
        if right.flags[flag] is None:
            return f"FAILED: replacement leaves live {flag} undefined"
        differences.append(left.flags[flag] != right.flags[flag])
    probe = z3.BitVec("probe_address", 64)
    differences.append(z3.Select(memories[0].array, probe) != z3.Select(memories[1].array, probe))
    solver = z3.Solver()
    solver.set("timeout", 120000)
    solver.add(*constraints)
    solver.add(z3.Or(*differences))
    outcome = solver.check()
    if outcome == z3.unsat:
        return "proved"
    if outcome == z3.sat:
        model = solver.model()
        return "FAILED: counterexample " + ", ".join(f"{d}={model[d]}" for d in model.decls()[:8])
    return "unknown (solver timeout)"


# --------------------------------------------------------------------------
# Encoding, decoding and emulation of concrete instances


def assemble(lines: list, assembler: str, ide: str | None, work: Path) -> bytes | None:
    if not lines:
        return b""
    source = work / "candidate.s"
    output = work / "candidate.o"
    source.write_text(".intel_syntax noprefix\n.text\n.globl f\nf:\n" + "".join(f"  {line}\n" for line in lines))
    if output.exists():
        output.unlink()
    command = [ide, "cc", "-c", str(source), "-o", str(output)] if assembler == "ide" else \
        ["as", "--64", str(source), "-o", str(output)]
    if subprocess.run(command, capture_output=True, text=True).returncode != 0:
        return None
    binary = work / "text.bin"
    if subprocess.run(["objcopy", "-O", "binary", "--only-section=.text", str(output), str(binary)],
                      capture_output=True).returncode != 0:
        return None
    return binary.read_bytes()


def objdump_decode(code: bytes, work: Path) -> list:
    if not code:
        return []
    (work / "decode.bin").write_bytes(code)
    completed = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
                                str(work / "decode.bin")], capture_output=True, text=True, check=True)
    lines = []
    for line in completed.stdout.splitlines():
        match = re.match(r"^\s*[0-9a-f]+:\t(?:[0-9a-f]{2} )+\s*\t(.*)$", line)
        if match:
            lines.append(re.sub(r"\s+", " ", match.group(1)).strip())
    return lines


def capstone_decode(code: bytes) -> list | None:
    try:
        import capstone
    except ImportError:
        return None
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    disassembler.syntax = capstone.CS_OPT_SYNTAX_INTEL
    return [f"{i.mnemonic} {i.op_str}".strip() for i in disassembler.disasm(code, 0)]


def unicorn_registers():
    from unicorn import x86_const as x86
    names = {"rax": x86.UC_X86_REG_RAX, "rcx": x86.UC_X86_REG_RCX, "rdx": x86.UC_X86_REG_RDX,
             "rbx": x86.UC_X86_REG_RBX, "rsi": x86.UC_X86_REG_RSI, "rdi": x86.UC_X86_REG_RDI,
             "rsp": x86.UC_X86_REG_RSP, "rbp": x86.UC_X86_REG_RBP}
    names.update({f"r{n}": getattr(x86, f"UC_X86_REG_R{n}") for n in range(8, 16)})
    names.update({f"xmm{n}": getattr(x86, f"UC_X86_REG_XMM{n}") for n in range(16)})
    return names


EFLAGS_BITS = {"CF": 0, "PF": 2, "AF": 4, "ZF": 6, "SF": 7, "OF": 11}


def emulate(code: bytes, registers: dict, flags: dict, binding: Binding, seed: int, stop: int | None = None):
    """Run `code` in Unicorn; return (registers, flags, memory, rip)."""
    import unicorn
    from unicorn import x86_const as x86
    names = unicorn_registers()
    emulator = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    code_base = 0x10000
    emulator.mem_map(code_base, 0x10000)
    emulator.mem_write(code_base, code)
    region = (min(registers["rsp"], registers["rbp"]) - 0x4000) & ~0xfff
    size = ((registers["rbp"] + 0x2000) - region + 0xfff) & ~0xfff
    emulator.mem_map(region, size)
    reference = ByteMemory(seed)
    emulator.mem_write(region, bytes(reference.initial(address) for address in range(region, region + size)))
    for name, value in registers.items():
        emulator.reg_write(names[binding.registers.get(name, name)], value)
    eflags = 0x2
    for name, index in EFLAGS_BITS.items():
        if flags[name]:
            eflags |= 1 << index
    emulator.reg_write(x86.UC_X86_REG_EFLAGS, eflags)
    end = code_base + len(code) if stop is None else code_base + stop
    if code:
        emulator.emu_start(code_base, end)
    result = {name: emulator.reg_read(names[binding.registers.get(name, name)]) for name in registers}
    eflags = emulator.reg_read(x86.UC_X86_REG_EFLAGS)
    result_flags = {name: eflags >> index & 1 for name, index in EFLAGS_BITS.items()}
    return result, result_flags, bytes(emulator.mem_read(region, size)), emulator.reg_read(x86.UC_X86_REG_RIP) - code_base


def emulator_check(rule: Rule, pattern_code: bytes, replacement_code: bytes, binding: Binding, trials: int,
                   seed: int) -> str:
    try:
        import unicorn  # noqa: F401
    except ImportError:
        return "skipped: unicorn not installed"
    generator = random.Random(seed)
    preserved = preserved_registers(rule) + ["rsp", "rbp"]
    for trial in range(trials):
        registers, flags = initial_state(rule, binding, generator)
        left = emulate(pattern_code, registers, flags, binding, seed + trial)
        right = emulate(replacement_code, registers, flags, binding, seed + trial)
        model = run_concrete(rule.pattern, rule, binding, registers, flags, seed + trial)
        for name in preserved:
            if left[0][name] != right[0][name]:
                return f"FAILED trial {trial}: {name} {left[0][name]:#x} != {right[0][name]:#x}"
            if left[0][name] != model.registers[name]:
                return f"MODEL MISMATCH trial {trial}: {name} emulator {left[0][name]:#x} model {model.registers[name]:#x}"
        for flag in live_flag_set(rule):
            if model.flags[flag] is None:
                continue
            if left[1][flag] != right[1][flag]:
                return f"FAILED trial {trial}: flag {flag}"
            if left[1][flag] != model.flags[flag]:
                return f"MODEL MISMATCH trial {trial}: flag {flag} emulator {left[1][flag]} model {model.flags[flag]}"
        if left[2] != right[2]:
            return f"FAILED trial {trial}: memory"
    return f"agrees ({trials} trials)"


# --------------------------------------------------------------------------
# Immediate classes


def immediate_class(predicate, z3_form, description):
    predicate.z3 = z3_form
    predicate.description = description
    return predicate


def _signed(value: int, width: int) -> int:
    return value - (1 << width) if value >> (width - 1) & 1 else value


ANY = immediate_class(lambda v, w: True, lambda z3, s, w: z3.BoolVal(True), "any")
UIMM31 = immediate_class(lambda v, w: 0 <= _signed(v, w), lambda z3, s, w: s >= 0,
                         "nonnegative as a signed value of its width")


# --------------------------------------------------------------------------
# The catalog


def R(name, width):
    return Reg(name, width)


def M(base, displacement, width, index=None, scale=1):
    return Mem(base, displacement, width, index, scale)


def I(value):
    return Imm(value)


def X(mnemonic, *operands):
    return Insn(mnemonic, tuple(operands))


def keep_live(register):
    def mutate(rule: Rule) -> Rule:
        return replace(rule, live_registers=tuple(sorted(set(rule.live_registers) | {register})),
                       scratch=tuple(name for name in rule.scratch if name != register))
    return mutate


def all_flags_live(rule: Rule) -> Rule:
    return replace(rule, live_flags="all")


def partial_overlap(rule: Rule) -> Rule:
    return replace(rule, overlap=True)


def widen_immediate(token):
    def mutate(rule: Rule) -> Rule:
        immediates = dict(rule.immediates)
        immediates[token] = (ANY, immediates[token][1])
        return replace(rule, immediates=immediates)
    return mutate


def chunk_copy(source, destination, chunks: int) -> list:
    """The existing expansion: eight-byte chunks through RAX (token A)."""
    sequence = []
    for chunk in range(chunks):
        sequence.append(X("mov", R("A", 64), source(8 * chunk, 64)))
        sequence.append(X("mov", destination(8 * chunk, 64), R("A", 64)))
    return sequence


def xmm_copy(source, destination, chunks: int, tail: bool) -> list:
    sequence = []
    for pair in range(chunks // 2):
        sequence.append(X("movups", R("X", 128), source(16 * pair, 128)))
        sequence.append(X("movups", destination(16 * pair, 128), R("X", 128)))
    if tail:
        sequence += chunk_copy(lambda o, w: source(o + 16 * (chunks // 2), w),
                               lambda o, w: destination(o + 16 * (chunks // 2), w), 1)
    return sequence


def frame(token):
    return lambda offset, width: M("rbp", (token, offset), width)


def pointer(name):
    return lambda offset, width: M(name, offset, width)


COPY_RATIONALE = ("A 16-byte MOVUPS load/store pair moves the same bytes as two 8-byte GPR chunk pairs: MOVUPS "
                  "is an unaligned, uninterpreted data move (no floating-point exception, no NaN handling). "
                  "Equivalence needs the source and destination objects to be disjoint or identical; C11 "
                  "6.5.16.1p3 makes any other overlap of an assignment undefined. XMM0 must be dead; the row "
                  "declares it clobbered like the scalar float rows do.")

RULES: list = [
    Rule("self-copy-64",
         [X("mov", R("A", 64), R("A", 64))], [],
         live_registers=("A",),
         rationale="A 64-bit register copy onto itself changes no register, flag or memory state.",
         integration="Allocator COPY edits whose source and destination are the same register emit nothing.",
         coverage="self-host 10,857 COPY edits (32.6 KB)"),
    Rule("self-copy-32-is-not-a-nop",
         [X("mov", R("A", 32), R("A", 32))], [], live_registers=("A",), status="rejected-invalid",
         rationale="A 32-bit self-move clears bits 63:32: it is a zero-extension, not a no-op."),
    Rule("store-reload-same-slot-64",
         [X("mov", M("rbp", "d0", 64), R("A", 64)), X("mov", R("A", 64), M("rbp", "d0", 64))],
         [X("mov", M("rbp", "d0", 64), R("A", 64))],
         live_registers=("A",),
         rationale="The load reads back the eight bytes just stored from the same register, so both the "
                   "register and memory already hold its result. Requires adjacency (nothing between, no "
                   "branch target between) and a load that is an allocator reload (never a volatile access).",
         integration="An allocator RELOAD/TEMP_RELOAD edit immediately after a 64-bit spill of the same "
                     "register to the same frame offset, within one block, emits nothing.",
         coverage="self-host 44,231 reload edits (222 KB after disp8)"),
    Rule("store-reload-same-slot-32-is-not-a-nop",
         [X("mov", M("rbp", "d0", 32), R("A", 32)), X("mov", R("A", 32), M("rbp", "d0", 32))],
         [X("mov", M("rbp", "d0", 32), R("A", 32))], live_registers=("A",), status="rejected-invalid",
         rationale="A 32-bit reload zero-extends and clears the register's upper half, which the store "
                   "never observed."),
    Rule("zero-idiom",
         [X("mov", R("A", 32), I(0))], [X("xor", R("A", 32), R("A", 32))],
         live_registers=("A",), live_flags=(),
         negative={"no flag live after": all_flags_live},
         rationale="XOR r32,r32 writes zero and zero-extends exactly like MOV r32,0, but writes "
                   "CF/OF/SF/ZF/PF (AF undefined); valid only where no flag is live.",
         integration="MOV_RI rows and REMATERIALIZE edits of zero use XOR r32,r32 when a conservative "
                     "block-local backward scan proves the flags dead at that point.",
         coverage="self-host 207,177 of 208,381 zero materializations (621 KB); 1,204 keep live flags"),
    Rule("zero-compare-to-test",
         [X("mov", R("A", 32), I(0)), X("cmp", R("B", 64), R("A", 64))], [X("test", R("B", 64), R("B", 64))],
         live_registers=("B",), scratch=("A",), live_flags=CONSUMED_FLAGS,
         negative={"materialized zero dead after": keep_live("A")},
         status="deferred",
         rationale="CMP B,0 and TEST B,B agree on CF=0, OF=0, ZF, SF and PF; only AF differs (0 versus "
                   "undefined) and no Buster consumer reads AF.",
         integration="Deferred: needs a compare-with-immediate MIR form or register liveness at the row.",
         coverage="self-host 2,712 of 3,264 with the zero dead, external 756 of 1,049; not integrated"),
    Rule("epilogue-leave",
         [X("mov", R("rsp", 64), R("rbp", 64)), X("pop", R("rbp", 64))], [X("leave")],
         stack=True, reduced_width=False,
         rationale="LEAVE is architecturally MOV RSP,RBP followed by POP RBP (64-bit operand size in 64-bit "
                   "mode) and has no flag effect. System V only: the Win64 epilogue grammar forbids it, and "
                   "SysV unwind records do not describe the epilogue.",
         integration="RET rows of frames without callee-saved pushes emit LEAVE; RET.",
         coverage="self-host 6,307 epilogues (18.9 KB)"),
    Rule("copy-forward-into-add",
         [X("mov", R("A", 64), R("B", 64)), X("add", R("C", 64), R("A", 64))], [X("add", R("C", 64), R("B", 64))],
         live_registers=("C",), scratch=("A",),
         negative={"copy dead after": keep_live("A")},
         status="deferred",
         rationale="When the copy's destination dies at the add, the add can read the copy's source.",
         integration="Deferred: the copy is an allocator COPY edit; its death needs allocator liveness. "
                     "Recorded as an allocator follow-up rather than an encoder peephole.",
         coverage="self-host 22,419 of 27,661 with the copy dead (67 KB); not integrated"),
    Rule("byte-zero-extend-drop-rex-w",
         [X("movzx", R("A", 64), R("B", 8))], [X("movzx", R("A", 32), R("B", 8))],
         live_registers=("A",),
         status="deferred",
         rationale="MOVZX r32 zero-extends through bit 63; REX.W adds nothing. Saves a byte only when no "
                   "other REX bit is needed.",
         integration="Deferred: 1 byte on legacy registers only (about 25 KB on the self-host object); the "
                     "exact-form keys of four recipes would have to be regenerated.",
         coverage="self-host 40,436 rows, legacy-register subset only"),
    Rule("constant-sign-extend-fold",
         [X("mov", R("A", 32), I("k0")), X("movsxd", R("B", 64), R("A", 32))], [X("mov", R("B", 32), I("k0"))],
         live_registers=("B",), scratch=("A",), immediates={"k0": (UIMM31, 32)},
         negative={"constant nonnegative": widen_immediate("k0"), "temporary dead after": keep_live("A")},
         status="deferred",
         rationale="Sign-extending a nonnegative 32-bit constant equals zero-extending it.",
         integration="Deferred: a canonical-IR constant-cast fold, not a machine peephole.",
         coverage="self-host 9,537 of 10,393 zero constants with the temporary dead, external 1,095 of 1,535; not integrated"),
    Rule("add-one-to-inc",
         [X("add", R("A", 64), I(1))], [X("inc", R("A", 64))],
         live_registers=("A",), live_flags=("PF", "ZF", "SF", "OF"),
         negative={"CF dead after": all_flags_live}, status="rejected-policy",
         rationale="Valid when CF is dead (INC leaves CF unchanged), but the MIR has no ADD-immediate-1 row "
                   "outside address folding: no coverage."),
    Rule("frame-copy-16",
         chunk_copy(frame("d0"), frame("d1"), 2), xmm_copy(frame("d0"), frame("d1"), 2, False),
         scratch=("A", "X"), slot_extent=16, reduced_width=False,
         negative={"objects disjoint": lambda rule: replace(rule, slot_extent=8)},
         rationale=COPY_RATIONALE,
         integration="COPY_FRAME_FROM_FRAME emits 16-byte MOVUPS chunks through XMM0, then 8/4/2/1 tails.",
         coverage="self-host 64,535 rows"),
    Rule("frame-copy-24",
         chunk_copy(frame("d0"), frame("d1"), 3), xmm_copy(frame("d0"), frame("d1"), 3, True),
         scratch=("A", "X"), slot_extent=24, reduced_width=False,
         rationale=COPY_RATIONALE + " A remainder keeps the RAX chunk."),
    Rule("frame-copy-16-same-object",
         chunk_copy(frame("d0"), frame("d0"), 2), xmm_copy(frame("d0"), frame("d0"), 2, False),
         scratch=("A", "X"), slot_extent=16, reduced_width=False,
         rationale="Exact overlap (self-assignment) copies each byte onto itself in both forms."),
    Rule("pointer-destination-copy-16",
         chunk_copy(frame("d0"), pointer("P"), 2), xmm_copy(frame("d0"), pointer("P"), 2, False),
         scratch=("A", "X"), slot_extent=16, pointers={"P": 16}, reduced_width=False,
         negative={"objects disjoint or identical": partial_overlap},
         rationale=COPY_RATIONALE,
         integration="COPY_PTR_FROM_FRAME emits 16-byte MOVUPS chunks through XMM0.",
         coverage="self-host 29,387 rows"),
    Rule("pointer-source-copy-16",
         chunk_copy(pointer("P"), frame("d0"), 2), xmm_copy(pointer("P"), frame("d0"), 2, False),
         scratch=("A", "X"), slot_extent=16, pointers={"P": 16}, reduced_width=False,
         negative={"objects disjoint or identical": partial_overlap},
         rationale=COPY_RATIONALE,
         integration="COPY_FRAME_FROM_PTR emits 16-byte MOVUPS chunks through XMM0.",
         coverage="self-host 8,208 rows"),
]


def binding_bytes(rule: Rule, register_class: str, ide: str | None, work: Path) -> dict:
    binding = make_binding(rule, random.Random(0), register_class)
    result = {"binding": {k: v for k, v in binding.registers.items() if k not in ("rsp", "rbp")}}
    for assembler in ("ide", "as"):
        if (assembler == "ide" and not ide) or (assembler == "as" and not shutil.which("as")):
            continue
        codes = [assemble([render(i, binding) for i in sequence], assembler, ide, work)
                 for sequence in (rule.pattern, rule.replacement)]
        result[assembler] = [None if code is None else code.hex() for code in codes]
    return result


def verify_rule(rule: Rule, ide: str | None, trials: int, seed: int) -> dict:
    report = {"name": rule.name, "status": rule.status, "rationale": rule.rationale,
              "integration": rule.integration, "coverage": rule.coverage,
              "pattern": [str(i) for i in rule.pattern], "replacement": [str(i) for i in rule.replacement]}
    counterexample = random_check(rule, trials, seed, rule.overlap)
    report["random"] = f"no counterexample in {trials} trials" if counterexample is None else "counterexample: " + counterexample
    status, cases = reduced_width_check(rule, 8)
    report["exhaustive"] = f"width 8: {status} ({cases} cases)"
    if status.startswith("skipped: input space"):
        status, cases = reduced_width_check(rule, 4)
        report["exhaustive"] = f"width 4: {status} ({cases} cases)"
    report["z3"] = z3_check(rule, rule.overlap)
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        encodings = {register_class: binding_bytes(rule, register_class, ide, work)
                     for register_class in ("legacy", "extended")}
        report["encodings"] = encodings
        agree = True
        sizes = {}
        for register_class, value in encodings.items():
            codes = [value[name] for name in ("ide", "as") if name in value]
            agree = agree and all(code == codes[0] for code in codes) and all(c is not None for c in codes[0])
            if codes and all(c is not None for c in codes[0]):
                sizes[register_class] = [len(bytes.fromhex(c)) for c in codes[0]]
        report["encoders_agree"] = agree
        report["bytes"] = sizes
        legacy = encodings["legacy"]
        first = legacy.get("ide") or legacy.get("as")
        if first and all(c is not None for c in first):
            pattern_code, replacement_code = (bytes.fromhex(c) for c in first)
            decoded = capstone_decode(pattern_code)
            if decoded is not None:
                replacement_decoded = capstone_decode(replacement_code)
                report["capstone"] = {"pattern": decoded, "replacement": replacement_decoded}
                report["decoded_counts_match"] = (len(decoded) == len(rule.pattern) and
                                                  len(replacement_decoded) == len(rule.replacement))
            report["objdump"] = {"pattern": objdump_decode(pattern_code, work),
                                 "replacement": objdump_decode(replacement_code, work)}
            binding = make_binding(rule, random.Random(0), "legacy")
            report["emulator"] = emulator_check(rule, pattern_code, replacement_code, binding, 200, seed)
    negatives = {}
    for precondition, mutate in rule.negative.items():
        mutated = mutate(rule)
        found = random_check(mutated, trials, seed + 7, mutated.overlap)
        proof = z3_check(mutated, mutated.overlap)
        negatives[precondition] = {"random": "counterexample found" if found else "NO COUNTEREXAMPLE",
                                   "z3": "counterexample found" if proof.startswith("FAILED") else proof}
    report["negative"] = negatives
    return report


def expects_equivalence(rule: Rule) -> bool:
    return rule.status != "rejected-invalid"


def rule_is_valid(report: dict) -> bool:
    return (report["random"].startswith("no counterexample") and not report["z3"].startswith("FAILED") and
            "FAILED" not in report["exhaustive"] and
            not report.get("emulator", "").startswith(("FAILED", "MODEL MISMATCH")))


# --------------------------------------------------------------------------
# Control-flow rules: proved over all 64 flag states


def verify_control(ide: str | None) -> dict:
    report = {}
    # Inversion is exact negation under every flag state.
    inversion = all(CONDITIONS[cc](dict(zip(FLAG_NAMES, bits))) ==
                    1 - CONDITIONS[INVERSE_CONDITION[cc]](dict(zip(FLAG_NAMES, bits)))
                    for cc in CONDITIONS for bits in itertools.product((0, 1), repeat=6))
    report["model: inverse condition is exact negation (16 conditions x 64 flag states)"] = inversion
    try:
        import unicorn  # noqa: F401
    except ImportError:
        report["emulator"] = "skipped: unicorn not installed"
        return report
    binding = Binding({"rsp": "rsp", "rbp": "rbp"}, {}, {})
    registers = {"rax": 0x1234, "rsp": 0x7ff000001000, "rbp": 0x7ff000001100}
    checks = 0
    failures = []
    for cc, code in CONDITION_CODES.items():
        inverse = CONDITION_CODES[INVERSE_CONDITION[cc]]
        for bits in itertools.product((0, 1), repeat=6):
            flags = dict(zip(FLAG_NAMES, bits))
            # Original layout: 0: jcc rel32 -> L(11); 6: jmp rel32 -> M(0x100); 11: L.
            original = bytes([0x0F, 0x80 + code]) + (5).to_bytes(4, "little") + \
                bytes([0xE9]) + (0x100 - 11).to_bytes(4, "little")
            # Rewritten: 0: jncc rel32 -> M(0x100); 6: L.
            rewritten = bytes([0x0F, 0x80 + inverse]) + (0x100 - 6).to_bytes(4, "little")
            # Short forms of the rewritten branch: jncc rel8 would need M within
            # reach; use M' = 0x40 for the rel8/rel32 comparison.
            short = bytes([0x70 + inverse, 0x40 - 2])
            near = bytes([0x0F, 0x80 + inverse]) + (0x40 - 6).to_bytes(4, "little")
            padding = b"\x90" * 0x200
            left = emulate(original + padding, {"rax": 1, "rsp": registers["rsp"], "rbp": registers["rbp"]},
                           flags, binding, 1, stop=None if False else None)
            # Execute exactly the branch instructions: stop when leaving the prefix.
            left_rip = execute_branches(original + padding, flags, [0, 6])
            right_rip = execute_branches(rewritten + padding, flags, [0])
            left_target = "M" if left_rip == 0x100 else "L" if left_rip == 11 else hex(left_rip)
            right_target = "M" if right_rip == 0x100 else "L" if right_rip == 6 else hex(right_rip)
            if left_target != right_target:
                failures.append(f"inversion {cc} {bits}: {left_target} vs {right_target}")
            if execute_branches(short + padding, flags, [0]) != execute_branches(near + padding, flags, [0]) - 0 and \
                    not (execute_branches(short + padding, flags, [0]) in (2, 0x40) and
                         execute_branches(near + padding, flags, [0]) in (6, 0x40) and
                         (execute_branches(short + padding, flags, [0]) == 0x40) ==
                         (execute_branches(near + padding, flags, [0]) == 0x40)):
                failures.append(f"rel8/rel32 {cc} {bits}")
            checks += 1
    # JMP to the next instruction versus falling through.
    for bits in itertools.product((0, 1), repeat=6):
        flags = dict(zip(FLAG_NAMES, bits))
        for jump in (bytes([0xEB, 0x00]), bytes([0xE9, 0, 0, 0, 0])):
            state_after = emulate(jump, {"rax": 7, "rsp": registers["rsp"], "rbp": registers["rbp"]}, flags, binding, 1)
            state_none = emulate(b"", {"rax": 7, "rsp": registers["rsp"], "rbp": registers["rbp"]}, flags, binding, 1)
            if state_after[0] != state_none[0] or state_after[1] != state_none[1] or state_after[2] != state_none[2] \
                    or state_after[3] != len(jump):
                failures.append(f"jmp-to-next {jump.hex()} {bits}")
            checks += 1
    report["emulator checks"] = checks
    report["emulator failures"] = failures
    decoded = capstone_decode(bytes([0x0F, 0x84]) + (0x10).to_bytes(4, "little"))
    report["capstone rel32 decode"] = decoded
    report["capstone rel8 decode"] = capstone_decode(bytes([0x74, 0x14]))
    return report


# Encoding-only rules: the same instruction in a shorter encoding. Frame
# chunks (spills, reloads, edge temporaries, aggregate-copy frame sides) move
# from a forced disp32 to disp8 when the final RBP displacement is a nonzero
# signed byte. Proved per register, width and direction: both encodings decode
# to identical instructions (objdump and Capstone) and execute identically
# (Unicorn), and the disp8 form is exactly three bytes shorter.
FRAME_CHUNK_FORMS = (
    ("load", 1, "movzx {r64}, BYTE PTR [rbp{d}]"), ("load", 2, "movzx {r64}, WORD PTR [rbp{d}]"),
    ("load", 4, "mov {r32}, DWORD PTR [rbp{d}]"), ("load", 8, "mov {r64}, QWORD PTR [rbp{d}]"),
    ("store", 1, "mov BYTE PTR [rbp{d}], {r8}"), ("store", 2, "mov WORD PTR [rbp{d}], {r16}"),
    ("store", 4, "mov DWORD PTR [rbp{d}], {r32}"), ("store", 8, "mov QWORD PTR [rbp{d}], {r64}"),
    ("load", 16, "movups xmm0, XMMWORD PTR [rbp{d}]"), ("store", 16, "movups XMMWORD PTR [rbp{d}], xmm0"),
)


def verify_encoding_rules() -> dict:
    report = {"rule": "frame-displacement-disp8", "forms": 0, "failures": []}
    if not shutil.which("as"):
        report["status"] = "skipped: GNU as not available"
        return report
    displacements = (-8, -0x10, -0x7f, -0x80, 0x10, 0x7f)
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        lines = []
        cases = []
        for direction, width, template in FRAME_CHUNK_FORMS:
            registers = [None] if width == 16 else list(REGISTER_VIEWS)
            for physical in registers:
                if physical in ("rsp", "rbp"):
                    continue
                views = REGISTER_VIEWS.get(physical, ("", "", "", ""))
                for displacement in displacements:
                    text = template.format(r64=views[0], r32=views[1], r16=views[2], r8=views[3],
                                           d=("-" if displacement < 0 else "+") + hex(abs(displacement)))
                    lines.append("{disp32} " + text)
                    lines.append(text)
                    cases.append((text, physical, displacement))
        source = work / "forms.s"
        source.write_text(".intel_syntax noprefix\n.text\n" + "".join(line + "\n" for line in lines))
        subprocess.run(["as", "--64", str(source), "-o", str(work / "forms.o")], check=True, capture_output=True)
        subprocess.run(["objcopy", "-O", "binary", "--only-section=.text", str(work / "forms.o"), str(work / "forms.bin")],
                       check=True, capture_output=True)
        code = (work / "forms.bin").read_bytes()
        try:
            import capstone
            disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
            decoded = [(i.address, i.size, f"{i.mnemonic} {i.op_str}") for i in disassembler.disasm(code, 0)]
        except ImportError:
            report["status"] = "skipped: capstone not installed"
            return report
        if len(decoded) != len(lines):
            report["failures"].append(f"decoded {len(decoded)} instructions for {len(lines)} lines")
            return report
        generator = random.Random(11)
        for index, (text, physical, displacement) in enumerate(cases):
            wide, short = decoded[2 * index], decoded[2 * index + 1]
            if wide[2] != short[2] or wide[1] != short[1] + 3:
                report["failures"].append(f"{text}: {wide} vs {short}")
                continue
            wide_code = code[wide[0]:wide[0] + wide[1]]
            short_code = code[short[0]:short[0] + short[1]]
            try:
                import unicorn  # noqa: F401
                binding = Binding({"rsp": "rsp", "rbp": "rbp"}, {}, {})
                for trial in range(4):
                    registers = {name: random_value(generator) for name in LEGACY_GPRS + EXTENDED_GPRS}
                    registers["rbp"] = 0x7ff0_0000_1000 + 16 * generator.randint(0, 1 << 16)
                    registers["rsp"] = registers["rbp"] - 0x400
                    flags = {name: generator.getrandbits(1) for name in FLAG_NAMES}
                    left = emulate(wide_code, registers, flags, binding, trial)
                    right = emulate(short_code, registers, flags, binding, trial)
                    if left[:3] != right[:3]:
                        report["failures"].append(f"{text}: emulated states differ")
                        break
            except ImportError:
                pass
            report["forms"] += 1
    report["status"] = "proved" if not report["failures"] else "FAILED"
    return report


def execute_branches(code: bytes, flags: dict, positions: list) -> int:
    """RIP after executing the branch instructions starting at `positions`
    in order, or the address where control left that chain."""
    import unicorn
    from unicorn import x86_const as x86
    emulator = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    base = 0x10000
    emulator.mem_map(base, 0x10000)
    emulator.mem_write(base, code)
    eflags = 0x2
    for name, index in EFLAGS_BITS.items():
        if flags[name]:
            eflags |= 1 << index
    emulator.reg_write(x86.UC_X86_REG_EFLAGS, eflags)
    rip = 0
    for position in positions:
        if rip != position:
            break
        emulator.emu_start(base + position, base + len(code), count=1)
        rip = emulator.reg_read(x86.UC_X86_REG_RIP) - base
    return rip


# --------------------------------------------------------------------------
# Search over census windows


TOKEN = re.compile(r"^([A-L])(\d+)$")


def parse_census_operand(text: str):
    match = TOKEN.match(text)
    if match:
        return Reg(match.group(1), int(match.group(2)))
    match = re.match(r"^(rsp|rbp)(\d+)$", text)
    if match:
        return Reg(match.group(1), int(match.group(2)))
    if text.startswith("#"):
        return Imm({"0": 0, "1": 1}.get(text[1:], "k_" + text[1:]))
    match = re.match(r"^m(\d+)\[(.*)\]$", text)
    if match:
        width = int(match.group(1))
        base = index = None
        scale = 1
        displacement = 0
        for part in re.findall(r"[+]?[^+]+(?:[+-]\d+:\d+)?", match.group(2)):
            part = part.lstrip("+")
            if "*" in part:
                index, scale_text = part.split("*")
                scale = int(scale_text)
            elif part.startswith("d"):
                token = part.split(":")[0]
                relative = re.match(r"^(d\d+)([+-]\d+)$", token)
                displacement = (relative.group(1), int(relative.group(2))) if relative else (token, 0)
            elif part == "sym":
                raise ValueError("relocated operand")
            elif base is None:
                base = part
            else:
                index = part
        return Mem(base, displacement, width, index, scale)
    raise ValueError(text)


def parse_census_pattern(pattern: str) -> list:
    instructions = []
    for part in pattern.split(" ; "):
        words = part.split(" ", 1)
        operands = []
        if len(words) > 1:
            for text in words[1].split(","):
                if text == "label":
                    raise ValueError("branch")
                operands.append(parse_census_operand(text))
        instructions.append(Insn(words[0], tuple(operands)))
    return instructions


def class_predicate(token: str):
    table = {"m1": immediate_class(lambda v, w: v == (1 << w) - 1, lambda z3, s, w: s == (1 << w) - 1, "all ones"),
             "p2": immediate_class(lambda v, w: v > 1 and v & (v - 1) == 0,
                                   lambda z3, s, w: z3.And(z3.UGT(s, 1), s & (s - 1) == 0), "power of two"),
             "s8": immediate_class(lambda v, w: -128 <= _signed(v, w) <= 127 and v not in (0, 1) and
                                   v != (1 << w) - 1 and not (v > 1 and v & (v - 1) == 0),
                                   lambda z3, s, w: z3.And(s >= -128, s <= 127), "signed 8-bit")}
    return table.get(token[2:], ANY)


def vocabulary(pattern: list) -> list:
    registers: dict[str, set] = {}
    memories: list = []
    immediates: list = [Imm(0), Imm(1)]
    for insn in pattern:
        for operand in insn.operands:
            if isinstance(operand, Reg) and operand.name not in ("rsp", "rbp"):
                registers.setdefault(operand.name, set()).add(operand.width)
            elif isinstance(operand, Mem) and operand not in memories:
                memories.append(operand)
            elif isinstance(operand, Imm) and operand not in immediates:
                immediates.append(operand)
    names = sorted(registers)
    result = []
    for d in names:
        for w in (32, 64):
            destination = Reg(d, w)
            result.append(Insn("xor", (destination, destination)))
            for s in names:
                if s != d:
                    result.append(Insn("mov", (destination, Reg(s, w))))
                for op in ("add", "sub", "and", "or", "xor", "cmp", "test"):
                    result.append(Insn(op, (destination, Reg(s, w))))
            for immediate in immediates:
                result.append(Insn("mov", (destination, immediate)))
                for op in ("add", "sub", "and", "or", "cmp", "test"):
                    result.append(Insn(op, (destination, immediate)))
            for memory in memories:
                if memory.width == w:
                    result.append(Insn("mov", (destination, memory)))
                    result.append(Insn("cmp", (destination, memory)))
            for op in ("inc", "dec", "neg", "not"):
                result.append(Insn(op, (destination,)))
            for s in names:
                for sw in (8, 16):
                    result.append(Insn("movzx", (destination, Reg(s, sw))))
                if w == 64:
                    result.append(Insn("movsxd", (destination, Reg(s, 32))))
    for memory in memories:
        for s in names:
            if memory.width in (8, 16, 32, 64):
                result.append(Insn("mov", (memory, Reg(s, memory.width))))
        # Sixteen bytes at this operand through a fresh dead XMM register,
        # when the window also touches the following eight bytes.
        if memory.width == 64 and isinstance(memory.displacement, tuple):
            follower = Mem(memory.base, (memory.displacement[0], memory.displacement[1] + 8), 64)
            if follower in memories:
                wide = Mem(memory.base, memory.displacement, 128)
                result.append(Insn("movups", (Reg("X", 128), wide)))
                result.append(Insn("movups", (wide, Reg("X", 128))))
    return result


def approximate_size(insn: Insn) -> int:
    """Byte-size model used only to order search candidates; accepted rules
    are re-encoded by real assemblers in --verify."""
    operands = insn.operands
    memory = next((o for o in operands if isinstance(o, Mem)), None)
    size = 1 if any(isinstance(o, (Reg, Mem)) and o.width == 64 for o in operands) else 0
    size += 2 if insn.mnemonic in ("movzx", "movsx", "movups") else 1
    if memory is not None:
        size += 1 + (4 if isinstance(memory.displacement, (str, tuple)) else 0)
    elif operands:
        size += 1
    for o in operands:
        if isinstance(o, Imm):
            size += 4 if insn.mnemonic == "mov" else 1
    return size


def search_window(pattern: list, live_registers: tuple, live_flags, max_length: int, seed: int) -> list:
    immediates = {operand.value: (class_predicate(operand.value), 64)
                  for insn in pattern for operand in insn.operands
                  if isinstance(operand, Imm) and isinstance(operand.value, str)}
    written = {insn.operands[0].name for insn in pattern
               if insn.operands and isinstance(insn.operands[0], Reg) and insn.mnemonic not in ("cmp", "test")}
    scratch = tuple(sorted(name for name in written if name not in live_registers)) + ("X",)
    tokens = {displacement_token(o.displacement) for insn in pattern for o in insn.operands if isinstance(o, Mem)}
    extent = 24 if any(isinstance(o, Mem) and isinstance(o.displacement, tuple) and o.displacement[1]
                       for insn in pattern for o in insn.operands) else 8
    base_rule = Rule("search", pattern, [], live_registers=tuple(live_registers), scratch=scratch,
                     live_flags=live_flags, immediates=immediates, slot_extent=extent)
    generator = random.Random(seed)
    tests = []
    for trial in range(16):
        binding = make_binding(base_rule, generator)
        try:
            registers, flags = initial_state(base_rule, binding, generator)
            registers.setdefault("X", random_value(generator, 128))
            reference = run_concrete(pattern, base_rule, binding, registers, flags, seed + trial)
        except (UnsupportedInstruction, KeyError):
            return []
        tests.append((binding, registers, flags, reference))
    original = sum(approximate_size(insn) for insn in pattern)
    words = vocabulary(pattern)
    found = []
    candidates = itertools.chain([[]], ([w] for w in words),
                                 ([a, b] for a in words for b in words) if max_length >= 2 else [])
    for candidate in candidates:
        if len(candidate) > len(pattern):
            continue
        cost = sum(approximate_size(insn) for insn in candidate)
        if cost >= original:
            continue
        probe = replace(base_rule, replacement=candidate)
        ok = True
        for trial, (binding, registers, flags, reference) in enumerate(tests):
            try:
                state = run_concrete(candidate, probe, binding, registers, flags, seed + trial)
            except (UnsupportedInstruction, KeyError):
                ok = False
                break
            if compare(probe, reference, state) is not None:
                ok = False
                break
        if ok:
            found.append((cost, candidate))
    found.sort(key=lambda item: (item[0], len(item[1]), sum(idiom_rank(i) for i in item[1]), [str(i) for i in item[1]]))
    return found


# Among equally short candidates, prefer canonical dependency-breaking and
# plain forms (XOR r, r is the zeroing idiom; AND/SUB variants read or merge).
IDIOM_PREFERENCE = ("xor", "mov", "movzx", "movsx", "movsxd", "movups", "test", "cmp", "add", "lea", "sub", "and", "or",
                    "inc", "dec", "neg", "not", "imul")


def idiom_rank(insn: Insn) -> int:
    return IDIOM_PREFERENCE.index(insn.mnemonic) if insn.mnemonic in IDIOM_PREFERENCE else len(IDIOM_PREFERENCE)


def search(census_path: Path, top: int, max_length: int, seed: int) -> list:
    data = json.loads(census_path.read_text())
    results = []
    for length in sorted(data["windows"]):
        for row in data["windows"][length][:top]:
            try:
                pattern = parse_census_pattern(row["pattern"])
            except ValueError:
                continue
            classes = []
            for signature, count in list(row["liveness"].items())[:2]:
                if count < 0.05 * row["count"] and classes:
                    continue
                live = tuple(signature[i] for i in range(0, len(signature), 2) if signature[i + 1:i + 2] == "L")
                flags_dead = row["count"] - row["flags_live_after"]
                share = count / row["count"]
                if flags_dead:
                    classes.append((signature, (), round(count * flags_dead / row["count"]), live))
                if row["flags_live_after"]:
                    classes.append((signature, "all", round(count * row["flags_live_after"] / row["count"]), live))
            for signature, live_flags, instances, live in classes:
                read_only = {o.name for insn in pattern for o in insn.operands if isinstance(o, Reg)} - \
                    {insn.operands[0].name for insn in pattern if insn.operands and isinstance(insn.operands[0], Reg)
                     and insn.mnemonic not in ("cmp", "test")}
                live_registers = tuple(sorted((set(live) | read_only) - {"rsp", "rbp"}))
                found = search_window(pattern, live_registers, live_flags, max_length, seed)
                best = found[0] if found else None
                original = sum(approximate_size(i) for i in pattern)
                results.append({
                    "pattern": row["pattern"], "window_instances": row["count"], "class_instances": instances,
                    "liveness": signature or "-", "flags_live_after": live_flags == "all",
                    "origins": dict(list(row.get("origins", {}).items())[:3]),
                    "best": None if best is None else [str(i) for i in best[1]],
                    "approximate_bytes_saved_each": None if best is None else original - best[0],
                    "alternatives": [[str(i) for i in item[1]] for item in found[1:4]],
                })
    return results


# --------------------------------------------------------------------------


# Whole-row check for the aggregate-copy rules. The per-chunk rules prove one
# sixteen-byte step under the disjoint-or-identical precondition; this checks
# the complete row plan (sixteen-byte chunks, then 8/4/2/1 tails, each loaded
# whole before it is stored, ascending) against the former plan (8/4/2/1)
# for every size through 256 and every overlap: the new plan is exact in
# every case the former one was, so it adds no aliasing assumption.
def copy_plan(size: int, widths: tuple) -> list:
    plan, at = [], 0
    for width in widths:
        while size - at >= width:
            plan.append((at, width))
            at += width
    return plan


def copy_plan_is_exact(size: int, delta: int, widths: tuple) -> bool:
    base = 2 * size + 32
    memory = list(range(4 * size + 64))
    wanted = memory[base:base + size]
    for at, width in copy_plan(size, widths):
        memory[base + delta + at:base + delta + at + width] = memory[base + at:base + at + width]
    return memory[base + delta:base + delta + size] == wanted


def verify_copy_overlap(limit: int = 256) -> dict:
    report = {"rule": "aggregate-copy row plan", "sizes": limit, "cases": 0, "regressions": [], "disjoint_or_identical_failures": []}
    for size in range(1, limit + 1):
        for delta in range(-size - 16, size + 17):
            report["cases"] += 1
            former = copy_plan_is_exact(size, delta, (8, 4, 2, 1))
            current = copy_plan_is_exact(size, delta, (16, 8, 4, 2, 1))
            if former and not current:
                report["regressions"].append((size, delta))
            if (delta == 0 or abs(delta) >= size) and not current:
                report["disjoint_or_identical_failures"].append((size, delta))
    report["status"] = "FAILED" if report["regressions"] or report["disjoint_or_identical_failures"] else "proved"
    return report


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--search", type=Path)
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--max-length", type=int, default=2)
    parser.add_argument("--ide", default=None, help="Buster ide executable, the primary assembler")
    parser.add_argument("--trials", type=int, default=3000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--rule", action="append", default=[])
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(SearchTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    failed = False
    if arguments.verify:
        reports = []
        for rule in RULES:
            if arguments.rule and rule.name not in arguments.rule:
                continue
            report = verify_rule(rule, arguments.ide, arguments.trials, arguments.seed)
            valid = rule_is_valid(report)
            report["verdict"] = "valid" if valid else "invalid"
            if expects_equivalence(rule) != valid:
                failed = True
                report["verdict"] += " (DISAGREES WITH CATALOG STATUS)"
            for precondition, result in report["negative"].items():
                if result["random"] != "counterexample found" and result["z3"] != "counterexample found":
                    failed = True
                    report["verdict"] += f" (precondition '{precondition}' not shown necessary)"
            reports.append(report)
            print(f"{rule.name}: {rule.status}, {report['verdict']}", flush=True)
        if not arguments.rule:
            encoding = verify_encoding_rules()
            reports.append({"name": "encoding", "report": encoding})
            failed = failed or encoding.get("status") == "FAILED"
            print(f"frame-displacement-disp8: {encoding.get('status')} over {encoding.get('forms', 0)} forms", flush=True)
            overlap = verify_copy_overlap()
            reports.append({"name": "copy-overlap", "report": overlap})
            failed = failed or overlap["status"] == "FAILED"
            print(f"aggregate-copy row plan: {overlap['status']} over {overlap['cases']} size/overlap cases", flush=True)
            control = verify_control(arguments.ide)
            reports.append({"name": "control-flow", "report": control})
            failed = failed or bool(control.get("emulator failures")) or \
                not control["model: inverse condition is exact negation (16 conditions x 64 flag states)"]
            print(f"control-flow: {len(control.get('emulator failures', []))} failures in "
                  f"{control.get('emulator checks', 0)} emulator checks", flush=True)
        output = reports
    elif arguments.search:
        output = search(arguments.search, arguments.top, arguments.max_length, arguments.seed)
        for row in output:
            print(json.dumps(row))
    else:
        parser.error("choose --self-test, --verify or --search")
    if arguments.output:
        arguments.output.write_text(json.dumps(output, indent=1) + "\n")
    if failed:
        print("verification FAILED", file=sys.stderr)
    return 1 if failed else 0


class SearchTests(unittest.TestCase):
    def test_copy_plan_overlap(self) -> None:
        self.assertEqual(copy_plan(29, (16, 8, 4, 2, 1)), [(0, 16), (16, 8), (24, 4), (28, 1)])
        self.assertTrue(copy_plan_is_exact(40, -8, (16, 8, 4, 2, 1)))
        self.assertFalse(copy_plan_is_exact(40, 8, (16, 8, 4, 2, 1)))
        self.assertEqual(verify_copy_overlap(48)["status"], "proved")

    def test_add_flags(self) -> None:
        state = State(Concrete(), 64, {"A": 0x7fffffffffffffff, "B": 1}, dict.fromkeys(FLAG_NAMES, 0), ByteMemory(1))
        state.execute(X("add", R("A", 64), R("B", 64)))
        self.assertEqual(state.registers["A"], 0x8000000000000000)
        self.assertEqual((state.flags["OF"], state.flags["CF"], state.flags["SF"], state.flags["ZF"]), (1, 0, 1, 0))

    def test_sub_borrow(self) -> None:
        state = State(Concrete(), 64, {"A": 0, "B": 1}, dict.fromkeys(FLAG_NAMES, 0), ByteMemory(1))
        state.execute(X("cmp", R("A", 32), R("B", 32)))
        self.assertEqual((state.flags["CF"], state.flags["ZF"], state.flags["SF"]), (1, 0, 1))

    def test_partial_writes(self) -> None:
        state = State(Concrete(), 64, {"A": MASK64, "B": 0}, dict.fromkeys(FLAG_NAMES, 0), ByteMemory(1))
        state.execute(X("mov", R("A", 32), R("B", 32)))
        self.assertEqual(state.registers["A"], 0)
        state.registers["A"] = MASK64
        state.execute(X("mov", R("A", 8), R("B", 8)))
        self.assertEqual(state.registers["A"], 0xffffffffffffff00)

    def test_xmm_copy(self) -> None:
        memory = ByteMemory(3)
        state = State(Concrete(), 64, {"rbp": 0x1000, "X": 0}, dict.fromkeys(FLAG_NAMES, 0), memory, {"d0": -0x40, "d1": -0x80})
        for insn in xmm_copy(frame("d0"), frame("d1"), 2, False):
            state.execute(insn)
        self.assertEqual(memory.load(None, 0x1000 - 0x80, 128), memory.load(None, 0x1000 - 0x40, 128))

    def test_reduced_width_views(self) -> None:
        state = State(Concrete(), 8, {"A": 0xff, "B": 0}, dict.fromkeys(FLAG_NAMES, 0), CellMemory(8, {}))
        state.execute(X("mov", R("A", 32), R("B", 32)))
        self.assertEqual(state.registers["A"], 0)

    def test_catalog_statuses(self) -> None:
        for rule in RULES:
            with self.subTest(rule=rule.name):
                result = random_check(rule, 300, 3, rule.overlap)
                self.assertEqual(result is None, expects_equivalence(rule), result)

    def test_negative_preconditions(self) -> None:
        for rule in RULES:
            for name, mutate in rule.negative.items():
                with self.subTest(rule=rule.name, precondition=name):
                    mutated = mutate(rule)
                    self.assertIsNotNone(random_check(mutated, 2000, 5, mutated.overlap))

    def test_census_pattern_parse(self) -> None:
        pattern = parse_census_pattern("mov A64,m64[rbp+d0:32] ; mov m64[rbp+d1:32],A64 ; mov A64,m64[rbp+d0+8:32]")
        self.assertEqual(pattern[0], X("mov", R("A", 64), M("rbp", ("d0", 0), 64)))
        self.assertEqual(pattern[2], X("mov", R("A", 64), M("rbp", ("d0", 8), 64)))

    def test_search_finds_zero_idiom(self) -> None:
        found = search_window([X("mov", R("A", 32), I(0))], ("A",), (), 1, 1)
        self.assertTrue(any(candidate == [X("xor", R("A", 32), R("A", 32))] for _, candidate in found))
        found = search_window([X("mov", R("A", 32), I(0))], ("A",), "all", 1, 1)
        self.assertFalse(any(candidate and candidate[0].mnemonic == "xor" for _, candidate in found))

    def test_search_finds_xmm_copy(self) -> None:
        pattern = parse_census_pattern("mov A64,m64[rbp+d0:32] ; mov m64[rbp+d1:32],A64 ; "
                                       "mov A64,m64[rbp+d0+8:32] ; mov m64[rbp+d1+8:32],A64")
        found = search_window(pattern, (), "all", 2, 1)
        self.assertTrue(found and [i.mnemonic for i in found[0][1]] == ["movups", "movups"], found[:2])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
