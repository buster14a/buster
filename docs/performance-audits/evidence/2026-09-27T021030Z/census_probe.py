#!/usr/bin/env python3
"""Disposable upstream census probe for the object-emission ledger.

Applies anchor-checked counter increments to a THROWAWAY worktree (never to a
branch that is pushed) and prints one OBJECT_PROBE line under `ide cc -v`:

  module_reserved   code-buffer bytes reserved per codegen attempt
  attempts          codegen attempts (a retry doubles the reservation)
  encoder_budget    per-function encoder scratch bytes reserved (x86 + AArch64)
  encoder_copied    machine-code bytes memcpy'd from encoder scratch to module
  machine_functions functions emitted by the machine path
  code_bytes        final CodegenModule.code length
  text_copy         .text bytes copied into the ObjectFile
  index_symbols     symbols inserted into the object symbol-name index
  index_bytes       bytes of the name-index slot table (fresh zeroed arena)
  name_lookups      object_symbol_name_slot calls
  slot_reads        hash-table slots read by those calls (probes)
  hashed_bytes      name bytes run through the FNV hash
  relocations       codegen relocations converted into ObjectRelocations
  entry_map_bytes   bytes of the entry_by_symbol map filled with 0xFF
  sort_comparisons  initializer insertion-sort comparisons
  symbols           final ObjectFile symbol count
  object_relocations final ObjectFile relocation count

Usage: census_probe.py <worktree>
"""
import sys


def patch(path, edits):
    with open(path) as handle:
        text = handle.read()
    for anchor, replacement in edits:
        if text.count(anchor) != 1:
            raise SystemExit(f"{path}: anchor matched {text.count(anchor)} times: {anchor[:80]!r}")
        text = text.replace(anchor, replacement)
    with open(path, "w") as handle:
        handle.write(text)


root = sys.argv[1]
P = "buster_object_probe"
patch(f"{root}/src/buster/lib/base.h", [(
    "#pragma once\n",
    "#pragma once\nextern unsigned long long buster_object_probe[32];\n",
)])
patch(f"{root}/src/buster/lib/compiler/codegen/codegen.c", [
    ("    CodegenBuffer buffer = {\n        .bytes = arena_allocate(arena, u8, capacity),",
     f"    {P}[0] += capacity;\n    {P}[1] += 1;\n    CodegenBuffer buffer = {{\n        .bytes = arena_allocate(arena, u8, capacity),"),
])
text = open(f"{root}/src/buster/lib/compiler/codegen/codegen.c").read()
site = "memcpy(buffer.bytes + buffer.count, encoded.bytes, encoded.byte_count);"
if text.count(site) != 2:
    raise SystemExit("codegen copy sites changed")
text = text.replace(site, site + f" {P}[3] += encoded.byte_count; {P}[4] += 1;")
open(f"{root}/src/buster/lib/compiler/codegen/codegen.c", "w").write(text)
patch(f"{root}/src/buster/lib/compiler/codegen/machine_x86_64.c", [(
    "    MachineX64Encoder encoder = {\n        .bytes = arena_allocate(arena, u8, capacity64),",
    f"    {P}[2] += capacity64;\n    MachineX64Encoder encoder = {{\n        .bytes = arena_allocate(arena, u8, capacity64),",
)])
patch(f"{root}/src/buster/lib/compiler/codegen/machine_aarch64.c", [(
    "    MachineA64Encoder encoder = {\n        .bytes = arena_allocate(arena, u8, capacity64),",
    f"    {P}[2] += capacity64;\n    MachineA64Encoder encoder = {{\n        .bytes = arena_allocate(arena, u8, capacity64),",
)])
patch(f"{root}/src/buster/lib/compiler/object/object.c", [
    ("#include <buster/lib/compiler/object/object.h>\n#include <buster/lib/compiler/object/object_internal.h>\n",
     f"#include <buster/lib/compiler/object/object.h>\n#include <buster/lib/compiler/object/object_internal.h>\nunsigned long long {P}[32];\n"),
    ("    u64 hash = 1469598103934665603ull;\n    for (u64 index = 0; index < name.length; index += 1)",
     f"    {P}[11] += name.length;\n    u64 hash = 1469598103934665603ull;\n    for (u64 index = 0; index < name.length; index += 1)"),
    ("    ObjectSymbolNameSlot* slot = table.slots + slot_index;\n    while (slot->used && !string_equal(slot->name, name))\n    {",
     f"    ObjectSymbolNameSlot* slot = table.slots + slot_index;\n    {P}[9] += 1;\n    {P}[10] += 1;\n    while (slot->used && !string_equal(slot->name, name))\n    {{\n        {P}[10] += 1;"),
    ("    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)\n    {\n        object_symbol_name_index_add(table, symbols + symbol_index, symbol_index);",
     f"    {P}[7] += capacity * sizeof(ObjectSymbolNameSlot);\n    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)\n    {{\n        {P}[6] += 1;\n        object_symbol_name_index_add(table, symbols + symbol_index, symbol_index);"),
    ("    u8* text = arena_allocate(arena, u8, module->code.length);",
     f"    {P}[5] += module->code.length;\n    {P}[8] += module->code.length;\n    u8* text = arena_allocate(arena, u8, module->code.length);"),
    ("            IrModuleInitializer previous = ir_module->initializers[initializer_order[position - 1]];",
     f"            IrModuleInitializer previous = ir_module->initializers[initializer_order[position - 1]];\n            {P}[14] += 1;"),
    ("    memset(entry_by_symbol, 0xFF, sizeof(*entry_by_symbol) * entry_symbol_capacity);",
     f"    memset(entry_by_symbol, 0xFF, sizeof(*entry_by_symbol) * entry_symbol_capacity);\n    {P}[13] += sizeof(*entry_by_symbol) * entry_symbol_capacity;"),
    ("        CodegenModuleRelocation source = module->relocations[relocation_index];\n        ObjectRelocationKind kind = OBJECT_RELOCATION_X86_64_PC32;",
     f"        CodegenModuleRelocation source = module->relocations[relocation_index];\n        {P}[12] += 1;\n        ObjectRelocationKind kind = OBJECT_RELOCATION_X86_64_PC32;"),
    ("    scratch_end(name_temporary);\n\n    return result;\n}",
     f"    scratch_end(name_temporary);\n    {P}[15] += result.symbol_count;\n    {P}[16] += result.relocation_count;\n\n    return result;\n}}"),
])
patch(f"{root}/src/buster/apps/ide/ide.c", [(
    '        string_print(S8("CODEGEN_MIR mutable_virtual_registers={u64}\\n"), compile.codegen_statistics.mutable_virtual_register_count);',
    '        string_print(S8("CODEGEN_MIR mutable_virtual_registers={u64}\\n"), compile.codegen_statistics.mutable_virtual_register_count);\n'
    '        string_print(S8("OBJECT_PROBE module_reserved={u64} attempts={u64} encoder_budget={u64} encoder_copied={u64} machine_functions={u64} '
    'text_copy={u64} index_symbols={u64} index_bytes={u64} code_bytes={u64} name_lookups={u64} slot_reads={u64} hashed_bytes={u64} '
    'relocations={u64} entry_map_bytes={u64} sort_comparisons={u64} symbols={u64} object_relocations={u64}\\n"), '
    + ", ".join(f"(u64)buster_object_probe[{i}]" for i in range(17)) + ");",
)])
print("census probe applied to", root)
