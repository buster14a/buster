#pragma once

#include <buster/lib/base.h>
#include <buster/lib/target.h>

// Base A64 source-assembly encoder: one textual instruction (mnemonic plus
// operand text, GNU/LLVM syntax) to one 32-bit word. It owns the scalar
// integer, memory, floating-point, element-move, barrier/system and common
// AdvSIMD families, and the standard aliases compilers print. It runs after
// the table-driven `.s` owners in assembly.c refuse a statement, so
// already-accepted spellings keep their owner.
// Operands must be constants: labels, symbols and relocation specifiers are
// refused here and stay with the owners that model fixups.

typedef enum A64BaseAssemblyStatus
{
    A64_BASE_ASSEMBLY_OK,
    A64_BASE_ASSEMBLY_UNKNOWN_MNEMONIC,
    A64_BASE_ASSEMBLY_INVALID_OPERANDS,
    A64_BASE_ASSEMBLY_REQUIRES_FP,
    A64_BASE_ASSEMBLY_REQUIRES_FULLFP16,
    A64_BASE_ASSEMBLY_REQUIRES_NEON,
    A64_BASE_ASSEMBLY_REQUIRES_LSE,
} A64BaseAssemblyStatus;

BUSTER_F_DECL A64BaseAssemblyStatus a64_base_assemble(Target target, String8 mnemonic, String8 operands, u32* word);
