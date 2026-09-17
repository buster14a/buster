#pragma once

#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/ir_construction.h>

#if BUSTER_COMPILER_MSVC
#define IR_APPEND_NOINLINE __declspec(noinline)
#else
#define IR_APPEND_NOINLINE __attribute__((noinline))
#endif

// Keep validation, CFG invalidation, allocation, and growth out of the
// capacity-available path. The checked public helper remains the single owner
// of those semantics.
static BUSTER_COLD IR_APPEND_NOINLINE IrInstructionId ir_instruction_append_slow(Arena* arena, IrFunction* function,
                                                                                IrInstruction instruction,
                                                                                IrSourceRange canonical_source)
{
    return ir_function_add_instruction(arena, function, instruction, canonical_source);
}

// Construction-only fast path. Callers must provide a non-null arena and
// function, an unpublished CFG, and valid instruction/source storage for the
// current count and capacity. Callers that cannot prove those invariants must
// use ir_function_add_instruction instead.
static BUSTER_INLINE IrInstructionId ir_instruction_append_trusted(Arena* arena, IrFunction* function, IrInstruction instruction,
                                                                  IrSourceRange canonical_source)
{
    u32 instruction_count = function->instruction_count;
    IrInstructionId result;
    if (BUSTER_UNLIKELY(instruction_count >= function->instruction_capacity))
    {
        result = ir_instruction_append_slow(arena, function, instruction, canonical_source);
    }
    else
    {
        result = (IrInstructionId){.value = instruction_count};
        function->instructions[instruction_count] = instruction;
        function->instruction_count = instruction_count + 1;
        if ((IR_OPCODE_SUMMARY_TRACKED >> instruction.opcode) & 1)
        {
            function->opcode_summary |= IR_OPCODE_BIT(instruction.opcode);
        }
        if (function->instruction_canonical_sources)
        {
            function->instruction_canonical_sources[instruction_count] = canonical_source;
        }
        IR_CONSTRUCTION_RECORD(INSTRUCTION_APPENDS, 1);
        IR_CONSTRUCTION_RECORD(OPERAND_SLOTS_APPENDED, instruction.operand_count);
    }
    return result;
}

#undef IR_APPEND_NOINLINE
