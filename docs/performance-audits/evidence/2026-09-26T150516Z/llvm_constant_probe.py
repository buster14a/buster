#!/usr/bin/env python3
"""Disposable LLVM constant-pool probe for audit 2026-09-26T150516Z (E4).

Rewrites <tree>/src/buster/lib/compiler/llvm/bitcode.c in place so that the
built compiler prints, at exit, every llvm_bc_add_constant search, the searches
made after the pool is locked (value numbering and emission) and the pool
records compared. The same anchors exist in the baseline and in the candidate,
so both are counted by identical code. Apply it only to a disposable worktree
and rebuild that tree's Release ide; it is never part of production code.

usage: llvm_constant_probe.py <tree>
"""
import os
import sys


def main():
    path = os.path.join(sys.argv[1], "src", "buster", "lib", "compiler", "llvm", "bitcode.c")
    source = open(path).read()
    if "LLVM_CONSTANT_PROBE" in source:
        return
    definition = "static u32 llvm_bc_add_constant(LlvmBcContext* context, u32 type_id, u32 code, u64 const* operands, u32 operand_count)\n{\n"
    loop = "\n    {\n        LlvmBcConstant* constant = context->constants + index;\n"
    if source.count(definition) != 1 or source.count(loop) != 1:
        sys.exit("llvm_constant_probe: anchors not found in " + path)
    counters = """#include <stdio.h>
#include <stdlib.h>
static unsigned long long probe_searches;
static unsigned long long probe_locked_searches;
static unsigned long long probe_records;
static void probe_print(void)
{
    fprintf(stderr, "LLVM_CONSTANT_PROBE searches=%llu locked_searches=%llu records_compared=%llu\\n", probe_searches,
            probe_locked_searches, probe_records);
}
"""
    entry = """    static int probe_registered;
    if (!probe_registered)
    {
        probe_registered = 1;
        atexit(probe_print);
    }
    probe_searches += 1;
    probe_locked_searches += context->constants_locked;
"""
    source = source.replace(definition, counters + definition + entry, 1)
    source = source.replace(loop, loop + "        probe_records += 1;\n", 1)
    open(path, "w").write(source)


if __name__ == "__main__":
    main()
