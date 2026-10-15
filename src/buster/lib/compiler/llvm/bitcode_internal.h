#pragma once

#include <buster/lib/compiler/llvm/bitcode.h>

#if BUSTER_INCLUDE_TESTS
// Private access to the production CST_CODE_INTEGER signed-rotation operand.
// Tests supply widths 1 through 64; no module or arena allocation is needed.
BUSTER_F_DECL u64 llvm_bitcode_test_integer_operand(u64 bits, u32 width);

// Exercise the production type interner, including a forced full-hash
// collision control. Probes include lookup, insertion and index rebuilding.
typedef struct LlvmBitcodeTestType LlvmBitcodeTestType;
struct LlvmBitcodeTestType
{
    u64 const* operands;
    u32 code;
    u32 operand_count;
};
typedef struct LlvmBitcodeTestTypes LlvmBitcodeTestTypes;
struct LlvmBitcodeTestTypes
{
    u32 record_count;
    u64 probes;
};
BUSTER_F_DECL LlvmBitcodeTestTypes llvm_bitcode_test_type_ids(Arena* arena, LlvmBitcodeTestType const* records, u32 count, bool collision, u32* ids);

// Batch the production section interner without serialization. Probes count
// every inspected hash slot, including the terminating empty/matching slot.
typedef struct LlvmBitcodeTestSections LlvmBitcodeTestSections;
struct LlvmBitcodeTestSections
{
    u32 name_count;
    u64 probes;
};
BUSTER_F_DECL LlvmBitcodeTestSections llvm_bitcode_test_section_ids(Arena* arena, IrSymbol** symbols, bool const* declarations, u32 count, u32* ids);
#endif
