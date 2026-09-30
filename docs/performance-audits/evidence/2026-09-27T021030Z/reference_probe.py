#!/usr/bin/env python3
"""Disposable probe: route `-c` ELF64 output through the frozen reference writer.

Applies one anchor-checked edit to a THROWAWAY worktree of the candidate so
that object_write takes the pre-plan ELF64 writer that test builds keep as
object_test_write_elf64_reference. That writer counts its work the same way
ObjectWriteStatistics counts the planned writer's, so `ide cc -v -c` prints
the old path's OBJECT_WRITE ledger on the same input. The probe's objects must
equal the baseline compiler's byte for byte; the evidence runs check that.

Usage: reference_probe.py <worktree>   (the worktree must build with tests)
"""
import sys

path = f"{sys.argv[1]}/src/buster/lib/compiler/object/object.c"
anchor = """ObjectArtifact object_write(Arena* arena, ObjectFile* object, ObjectFormat format)
{
    return object_write_core(arena, object, format, false);
}"""
replacement = """ObjectArtifact object_write(Arena* arena, ObjectFile* object, ObjectFormat format)
{
    // DISPOSABLE PROBE: measure the frozen pre-plan ELF writer on real inputs.
    return object_write_core(arena, object, format, format == OBJECT_FORMAT_ELF64);
}"""
with open(path) as handle:
    text = handle.read()
if text.count(anchor) != 1:
    raise SystemExit(f"{path}: anchor matched {text.count(anchor)} times")
with open(path, "w") as handle:
    handle.write(text.replace(anchor, replacement))
print("reference probe applied to", sys.argv[1])
