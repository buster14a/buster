#pragma once

#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/ir_construction.h>

#if BUSTER_COMPILER_MSVC
#define IR_APPEND_NOINLINE __declspec(noinline)
#define IR_APPEND_UNUSED
#pragma warning(push)
#pragma warning(disable : 4505)
#else
#define IR_APPEND_NOINLINE __attribute__((noinline))
#define IR_APPEND_UNUSED __attribute__((unused))
#endif

// Keep validation, CFG invalidation, allocation, and growth out of the
// capacity-available path. The checked public helper remains the single owner
// of those semantics.
static IR_APPEND_UNUSED BUSTER_COLD IR_APPEND_NOINLINE IrInstructionId ir_instruction_append_slow(Arena* arena, IrFunction* function,
                                                                                                 IrInstruction instruction,
                                                                                                 IrSourceRange canonical_source)
{
    return ir_function_add_instruction(arena, function, instruction, canonical_source);
}

// Construction-only fast path. Callers must provide a non-null arena and
// function, an unpublished CFG, and valid instruction/source storage for the
// current count and capacity. Callers that cannot prove those invariants must
// use ir_function_add_instruction instead.
static IR_APPEND_UNUSED BUSTER_INLINE IrInstructionId ir_instruction_append_trusted(Arena* arena, IrFunction* function,
                                                                                    IrInstruction instruction,
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

// The row-level preconditions every commit shares: storage behind each count,
// operands and targets that name rows the function already holds, and a
// result value no row defines yet, of the row's own type. Values and blocks
// are never renumbered while construction is open, so a reference that is in
// range here stays in range. O(operands + targets), all already in hand.
static IR_APPEND_UNUSED BUSTER_INLINE IrCommitRefusal ir_row_commit_refusal(IrFunction const* function, IrInstruction const* instruction)
{
    IrCommitRefusal refusal = IR_COMMIT_ACCEPTED;
    if ((instruction->operand_count && !instruction->operands) || (instruction->target_count && !instruction->targets) ||
        (instruction->immediate_count && !instruction->immediates))
    {
        refusal = IR_COMMIT_REFUSED_STORAGE;
    }
    for (u32 index = 0; refusal == IR_COMMIT_ACCEPTED && index < instruction->operand_count; index += 1)
    {
        refusal = instruction->operands[index].value < function->value_count ? IR_COMMIT_ACCEPTED : IR_COMMIT_REFUSED_OPERAND;
    }
    for (u32 index = 0; refusal == IR_COMMIT_ACCEPTED && index < instruction->target_count; index += 1)
    {
        refusal = instruction->targets[index].value < function->block_count ? IR_COMMIT_ACCEPTED : IR_COMMIT_REFUSED_TARGET;
    }
    IrValueId result = instruction->result;
    if (refusal == IR_COMMIT_ACCEPTED && result.value != IR_ID_UNDERLYING_INVALID &&
        (result.value >= function->value_count || function->values[result.value].definition.value != IR_ID_UNDERLYING_INVALID ||
         function->values[result.value].canonical_type.value != instruction->canonical_type.value))
    {
        refusal = IR_COMMIT_REFUSED_RESULT;
    }
    IR_CONSTRUCTION_RECORD(COMMIT_CHECKS, 1);
    IR_CONSTRUCTION_RECORD(COMMIT_OPERAND_CHECKS, instruction->operand_count);
    IR_CONSTRUCTION_RECORD(COMMIT_TARGET_CHECKS, instruction->target_count);
    IR_CONSTRUCTION_RECORD(COMMIT_REFUSALS, refusal != IR_COMMIT_ACCEPTED);
    return refusal;
}

// Why appending `instruction` to `block` would be refused, or ACCEPTED.
static IR_APPEND_UNUSED BUSTER_INLINE IrCommitRefusal ir_block_commit_refusal(IrFunction const* function, IrBlockId block, IrInstruction const* instruction)
{
    IrCommitRefusal refusal;
    if (!function || !function->blocks || block.value >= function->block_count)
    {
        refusal = IR_COMMIT_REFUSED_BLOCK;
        IR_CONSTRUCTION_RECORD(COMMIT_REFUSALS, 1);
    }
    else if (function->blocks[block.value].terminated)
    {
        refusal = IR_COMMIT_REFUSED_CLOSED;
        IR_CONSTRUCTION_RECORD(COMMIT_REFUSALS, 1);
    }
    else
    {
        refusal = ir_row_commit_refusal(function, instruction);
    }
    return refusal;
}

// The one place a committed row joins its block: row `id` becomes the chain's
// tail, the result's definition names it, and a terminator closes the block.
// The block was open, so a tail that is not a terminator leaves it open.
static IR_APPEND_UNUSED BUSTER_INLINE void ir_block_link_committed(IrFunction* function, IrBlockId block_id, IrInstruction const* instruction,
                                                                   IrInstructionId id)
{
    IrBlock* block = function->blocks + block_id.value;
    if (block->last_instruction.value != IR_ID_UNDERLYING_INVALID)
    {
        function->instructions[block->last_instruction.value].next = id;
    }
    else
    {
        block->first_instruction = id;
    }
    block->last_instruction = id;
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
    {
        function->values[instruction->result.value].definition = id;
        IR_CONSTRUCTION_RECORD(COMMIT_RESULT_BINDS, 1);
    }
    block->terminated = ir_instruction_is_terminator(instruction);
    IR_CONSTRUCTION_RECORD(COMMIT_CLOSES, block->terminated);
}

// Stores and links a row whose commit was accepted.
static IR_APPEND_UNUSED BUSTER_INLINE IrInstructionId ir_block_commit_accepted(Arena* arena, IrFunction* function, IrBlockId block,
                                                                               IrInstruction instruction, IrSourceRange canonical_source)
{
    instruction.next = IR_INSTRUCTION_ID_INVALID;
    IrInstructionId result = ir_instruction_append_trusted(arena, function, instruction, canonical_source);
    ir_block_link_committed(function, block, &instruction, result);
    return result;
}

// Construction-only commit for producers that own a fresh, unpublished
// function (the preconditions of ir_instruction_append_trusted). Returns
// INVALID and changes nothing when the row is refused.
static IR_APPEND_UNUSED BUSTER_INLINE IrInstructionId ir_block_commit_trusted(Arena* arena, IrFunction* function, IrBlockId block,
                                                                              IrInstruction instruction, IrSourceRange canonical_source,
                                                                              IrCommitRefusal* refusal_out)
{
    IrInstructionId result = IR_INSTRUCTION_ID_INVALID;
    IrCommitRefusal refusal = arena ? ir_block_commit_refusal(function, block, &instruction) : IR_COMMIT_REFUSED_BLOCK;
    if (refusal == IR_COMMIT_ACCEPTED)
    {
        result = ir_block_commit_accepted(arena, function, block, instruction, canonical_source);
    }
    *refusal_out = refusal;
    return result;
}

#if BUSTER_COMPILER_MSVC
#pragma warning(pop)
#endif
#undef IR_APPEND_UNUSED
#undef IR_APPEND_NOINLINE
