#include <buster/lib/compiler/codegen/machine.h>

// QRA stage 7: global assignment over the machine IR. A live interval per
// virtual register, a weight that pays for keeping it in a register, and a
// priority walk that hands the highest-weight non-overlapping intervals a
// callee-saved register for the whole function. Everything the walk cannot
// place falls to the local scan, so QUALITY is FAST plus a global layer
// rather than a second allocator to keep correct.
//
// Callee-saved is the register class that makes a whole-function binding
// sound without a clobber analysis: calls preserve those registers, and
// every encoder scratch and macro-op sequence in this backend works out of
// the caller-saved half.

// Bounds keep the pass linear-ish and its worst case reportable.
#define MACHINE_QUALITY_MAXIMUM_CANDIDATES 4096

typedef struct MachineQualityInterval MachineQualityInterval;
struct MachineQualityInterval
{
    u32 virtual_register;
    u32 start;
    u32 end;
    u32 weight;
};

BUSTER_GLOBAL_LOCAL u32 machine_quality_callee_saved[2] = {
    MACHINE_X64_R14, MACHINE_X64_R15,
};

// Sift-down over a max-heap keyed on weight, so the priority walk needs no
// sort routine and no recursion.
BUSTER_GLOBAL_LOCAL void machine_quality_heap_sift(MachineQualityInterval* heap, u32 count, u32 root)
{
    for (;;)
    {
        u32 largest = root;
        u32 left = 2 * root + 1;
        u32 right = left + 1;
        if (left < count && heap[left].weight > heap[largest].weight)
        {
            largest = left;
        }
        if (right < count && heap[right].weight > heap[largest].weight)
        {
            largest = right;
        }
        if (largest == root)
        {
            return;
        }
        MachineQualityInterval swapped = heap[root];
        heap[root] = heap[largest];
        heap[largest] = swapped;
        root = largest;
    }
}

MachineStackPlacement machine_quality_placement_build(Arena* arena, MachineFunction* function)
{
    // A switch's targets live in the case table rather than in block-ref
    // operands, so the loop extension below cannot see whether any of them
    // points backwards. Rather than reason about that, functions with a
    // case table keep the local allocator alone.
    if (function->switch_case_count)
    {
        return machine_fast_placement_build(arena, function);
    }
    TemporalArena scratch = scratch_begin(&arena, 1);
    u32 register_count = function->virtual_register_count;
    u32* pinned_registers = arena_allocate(arena, u32, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        pinned_registers[register_index] = UINT32_MAX;
    }
    u32* interval_starts = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    u32* interval_ends = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    u32* weights = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    u8* disqualified = arena_allocate(scratch.arena, u8, register_count ? register_count : 1);
    u8* crosses_call = arena_allocate(scratch.arena, u8, register_count ? register_count : 1);
    u8* loop_resident = arena_allocate(scratch.arena, u8, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        interval_starts[register_index] = UINT32_MAX;
        interval_ends[register_index] = 0;
        weights[register_index] = 0;
        disqualified[register_index] = 0;
        crosses_call[register_index] = 0;
        loop_resident[register_index] = 0;
    }
    // Interval and weight pass. A value touched by an opcode whose encoder
    // pins its operands cannot hold an arbitrary register for its whole
    // life, so it is disqualified rather than special-cased.
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        MachineInstruction* instruction = function->instructions + instruction_index;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        if (!info)
        {
            scratch_end(scratch);
            return (MachineStackPlacement){0};
        }
        bool constrained = machine_fast_opcode_is_constrained(instruction->opcode);
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            MachineRef ref = instruction->operands[slot];
            if (machine_ref_kind(ref) != MACHINE_REF_VIRTUAL_REGISTER)
            {
                continue;
            }
            u32 virtual_register = machine_ref_payload(ref);
            interval_starts[virtual_register] = BUSTER_MIN(interval_starts[virtual_register], instruction_index);
            interval_ends[virtual_register] = BUSTER_MAX(interval_ends[virtual_register], instruction_index);
            weights[virtual_register] += 1;
            disqualified[virtual_register] |= constrained ? 1u : 0u;
        }
    }
    // Loop extension: a backward edge can re-run everything between its
    // target and itself, so any interval meeting that span must cover the
    // whole span or two values alive in different iterations could share a
    // register.
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_BLOCK)
                {
                    continue;
                }
                u32 successor = machine_ref_payload(instruction->operands[slot]);
                if (successor > block_index)
                {
                    continue;
                }
                u32 loop_start = function->blocks[successor].first_instruction;
                u32 loop_end = block->first_instruction + block->instruction_count - 1;
                for (u32 register_index = 0; register_index < register_count; register_index += 1)
                {
                    if (interval_starts[register_index] == UINT32_MAX || interval_starts[register_index] > loop_end ||
                        interval_ends[register_index] < loop_start)
                    {
                        continue;
                    }
                    interval_starts[register_index] = BUSTER_MIN(interval_starts[register_index], loop_start);
                    interval_ends[register_index] = BUSTER_MAX(interval_ends[register_index], loop_end);
                    // Surviving a loop is what makes a global binding pay.
                    weights[register_index] += 8;
                    loop_resident[register_index] = 1;
                }
            }
        }
    }
    // A pinned register costs its function a push and a pop on every
    // call, so it only pays for a value that would otherwise cross a call
    // in memory. Values that never meet a call are already served by the
    // local scan out of the caller-saved half.
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        MachineOpcodeInfo const* info = machine_opcode_info(function->instructions[instruction_index].opcode);
        if (!(info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL))
        {
            continue;
        }
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            crosses_call[register_index] |= interval_starts[register_index] != UINT32_MAX && interval_starts[register_index] < instruction_index &&
                                                    instruction_index < interval_ends[register_index]
                                                ? 1u
                                                : 0u;
        }
    }
    // Priority walk. The heap orders by weight; each candidate takes the
    // first callee-saved register whose assigned spans it misses entirely.
    MachineQualityInterval* heap = arena_allocate(scratch.arena, MachineQualityInterval, MACHINE_QUALITY_MAXIMUM_CANDIDATES);
    u32 heap_count = 0;
    for (u32 register_index = 0; register_index < register_count && heap_count < MACHINE_QUALITY_MAXIMUM_CANDIDATES; register_index += 1)
    {
        // The threshold is the break-even point: fewer touches than this
        // and the push/pop pair costs more than the memory traffic saved.
        // Break-even: a pin costs a push and a pop on every entry, so it
        // must serve a value that lives in a loop *and* crosses a call
        // there. Anything narrower is already served by the local scan out
        // of the caller-saved half, which pays nothing at entry.
        if (disqualified[register_index] || interval_starts[register_index] == UINT32_MAX || !crosses_call[register_index] ||
            !loop_resident[register_index] || weights[register_index] < 12)
        {
            continue;
        }
        heap[heap_count] = (MachineQualityInterval){
            .virtual_register = register_index,
            .start = interval_starts[register_index],
            .end = interval_ends[register_index],
            .weight = weights[register_index],
        };
        heap_count += 1;
    }
    for (u32 root = heap_count / 2; root > 0; root -= 1)
    {
        machine_quality_heap_sift(heap, heap_count, root - 1);
    }
    u32 assigned_starts[BUSTER_ARRAY_LENGTH(machine_quality_callee_saved)][8];
    u32 assigned_ends[BUSTER_ARRAY_LENGTH(machine_quality_callee_saved)][8];
    u32 assigned_counts[BUSTER_ARRAY_LENGTH(machine_quality_callee_saved)];
    for (u32 file_index = 0; file_index < BUSTER_ARRAY_LENGTH(machine_quality_callee_saved); file_index += 1)
    {
        assigned_counts[file_index] = 0;
    }
    u32 pinned_mask = 0;
    while (heap_count)
    {
        MachineQualityInterval candidate = heap[0];
        heap_count -= 1;
        heap[0] = heap[heap_count];
        machine_quality_heap_sift(heap, heap_count, 0);
        for (u32 file_index = 0; file_index < BUSTER_ARRAY_LENGTH(machine_quality_callee_saved); file_index += 1)
        {
            if (assigned_counts[file_index] == BUSTER_ARRAY_LENGTH(assigned_starts[0]))
            {
                continue;
            }
            bool overlaps = false;
            for (u32 span = 0; span < assigned_counts[file_index]; span += 1)
            {
                overlaps |= candidate.start <= assigned_ends[file_index][span] && assigned_starts[file_index][span] <= candidate.end;
            }
            if (overlaps)
            {
                continue;
            }
            assigned_starts[file_index][assigned_counts[file_index]] = candidate.start;
            assigned_ends[file_index][assigned_counts[file_index]] = candidate.end;
            assigned_counts[file_index] += 1;
            pinned_registers[candidate.virtual_register] = machine_quality_callee_saved[file_index];
            pinned_mask |= 1u << machine_quality_callee_saved[file_index];
            break;
        }
    }
    scratch_end(scratch);
    MachineStackPlacement placement = machine_fast_placement_build_pinned(arena, function, pinned_registers, pinned_mask);
    // Pin verification. A pinned register belongs to its value for the
    // whole interval, so no other value's operand and no edit may name it.
    // The local scan has several paths that bypass its own register pool —
    // copy coalescing was one — and a wrong pin is a silent miscompile, so
    // the placement is checked rather than trusted, and a violation falls
    // back to the local allocator alone.
    bool pins_hold = true;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count && pinned_mask && pins_hold; instruction_index += 1)
    {
        MachineInstruction* instruction = function->instructions + instruction_index;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        u8 const* operand_registers = placement.operand_registers + (u64)instruction_index * 4;
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            if (operand_registers[slot] == UINT8_MAX || !((pinned_mask >> operand_registers[slot]) & 1u))
            {
                continue;
            }
            MachineRef ref = instruction->operands[slot];
            pins_hold &= machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER && pinned_registers[machine_ref_payload(ref)] == operand_registers[slot];
        }
    }
    for (u32 edit_index = 0; edit_index < placement.edit_count && pinned_mask && pins_hold; edit_index += 1)
    {
        pins_hold &= !((pinned_mask >> placement.edits[edit_index].location) & 1u);
    }
    if (!pins_hold)
    {
        return machine_fast_placement_build(arena, function);
    }
    placement.pinned_register_count = 0;
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        placement.pinned_register_count += pinned_registers[register_index] != UINT32_MAX;
    }
    return placement;
}
