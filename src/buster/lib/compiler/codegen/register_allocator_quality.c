#include <buster/lib/compiler/codegen/machine.h>

// QRA stages 7 and 8: global assignment over the machine IR. A live
// interval per virtual register, a weight that pays for keeping it in a
// register, and a priority walk that hands the highest-weight
// non-overlapping intervals a callee-saved register. Everything the walk
// cannot place falls to the local scan, so QUALITY is FAST plus a global
// layer rather than a second allocator to keep correct.
//
// Stage 8 scopes each reservation to the interval that earned it: the
// local scan owns a pin register at every instruction no pinned span
// covers, so binding the whole callee-saved file no longer starves the
// scan the way the whole-function pins of 2026-08-09al measured.
//
// Callee-saved is the register class that makes a span binding sound
// without a clobber analysis: calls preserve those registers, and every
// encoder scratch and macro-op sequence in this backend works out of the
// caller-saved half.

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
    MachineTargetDescription const* description = function->target;
    if (!description)
    {
        return (MachineStackPlacement){0};
    }
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
    u8* disqualified = arena_allocate(scratch.arena, u8, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        interval_starts[register_index] = UINT32_MAX;
        interval_ends[register_index] = 0;
        disqualified[register_index] = 0;
    }
    // Interval pass. A value touched by an opcode whose encoder pins its
    // operands cannot hold an arbitrary register for its whole life, so it
    // is disqualified rather than special-cased.
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        MachineInstruction* instruction = function->instructions + instruction_index;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        if (!info)
        {
            scratch_end(scratch);
            return (MachineStackPlacement){0};
        }
        bool constrained = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED) != 0;
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
            disqualified[virtual_register] |= constrained ? 1u : 0u;
        }
    }
    // Loop extension: a backward edge can re-run everything between its
    // target and itself, so any interval meeting that span must cover the
    // whole span or two values alive in different iterations could share a
    // register. The full fixed point is needed — extending into one span
    // can push an interval into an overlapping or nested one — because
    // the span scoping below leans on the closed property that an
    // interval meeting a loop covers it: it is what makes a span opening
    // mid-block a point that cannot re-execute, and a span alive at a
    // latch alive at its head. Overlapping and touching spans merge into
    // disjoint regions first, which makes one ascending pass exact: no
    // extension can reach past its own merged region into an earlier one,
    // so nothing cascades and nothing needs a second look.
    u32 backward_edge_count = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                backward_edge_count += machine_ref_kind(instruction->operands[slot]) == MACHINE_REF_BLOCK &&
                                       machine_ref_payload(instruction->operands[slot]) <= block_index;
            }
        }
    }
    u64* loop_spans = arena_allocate(scratch.arena, u64, backward_edge_count ? backward_edge_count : 1);
    u64* loop_span_scratch = arena_allocate(scratch.arena, u64, backward_edge_count ? backward_edge_count : 1);
    u32 loop_span_count = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_BLOCK ||
                    machine_ref_payload(instruction->operands[slot]) > block_index)
                {
                    continue;
                }
                u32 loop_start = function->blocks[machine_ref_payload(instruction->operands[slot])].first_instruction;
                u32 loop_end = block->first_instruction + block->instruction_count - 1;
                loop_spans[loop_span_count] = ((u64)loop_start << 32) | loop_end;
                loop_span_count += 1;
            }
        }
    }
    // Bottom-up stable merge sort by packed start, then the standard
    // overlap merge; touching spans merge too, since an interval ending
    // exactly where the next span starts meets both.
    for (u32 width = 1; width < loop_span_count; width *= 2)
    {
        for (u32 sort_start = 0; sort_start < loop_span_count; sort_start += 2 * width)
        {
            u32 middle = BUSTER_MIN(sort_start + width, loop_span_count);
            u32 limit = BUSTER_MIN(sort_start + 2 * width, loop_span_count);
            u32 left = sort_start;
            u32 right = middle;
            u32 out = sort_start;
            while (left < middle && right < limit)
            {
                loop_span_scratch[out++] = loop_spans[right] < loop_spans[left] ? loop_spans[right++] : loop_spans[left++];
            }
            while (left < middle)
            {
                loop_span_scratch[out++] = loop_spans[left++];
            }
            while (right < limit)
            {
                loop_span_scratch[out++] = loop_spans[right++];
            }
        }
        for (u32 span_index = 0; span_index < loop_span_count; span_index += 1)
        {
            loop_spans[span_index] = loop_span_scratch[span_index];
        }
    }
    u32 merged_span_count = 0;
    for (u32 span_index = 0; span_index < loop_span_count; span_index += 1)
    {
        u32 span_start = (u32)(loop_spans[span_index] >> 32);
        u32 span_end = (u32)loop_spans[span_index];
        if (merged_span_count && span_start <= (u32)loop_spans[merged_span_count - 1])
        {
            u32 merged_end = BUSTER_MAX((u32)loop_spans[merged_span_count - 1], span_end);
            loop_spans[merged_span_count - 1] = (loop_spans[merged_span_count - 1] & ~(u64)UINT32_MAX) | merged_end;
            continue;
        }
        loop_spans[merged_span_count] = ((u64)span_start << 32) | span_end;
        merged_span_count += 1;
    }
    for (u32 span_index = 0; span_index < merged_span_count; span_index += 1)
    {
        u32 loop_start = (u32)(loop_spans[span_index] >> 32);
        u32 loop_end = (u32)loop_spans[span_index];
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            if (interval_starts[register_index] == UINT32_MAX || interval_starts[register_index] > loop_end ||
                interval_ends[register_index] < loop_start)
            {
                continue;
            }
            interval_starts[register_index] = BUSTER_MIN(interval_starts[register_index], loop_start);
            interval_ends[register_index] = BUSTER_MAX(interval_ends[register_index], loop_end);
        }
    }
    // Rather than guess which values would spill, run the local scan once
    // and count what it actually did. A pin's benefit is the memory
    // traffic it removes, which is exactly the edit count of that value,
    // and heuristics guessing at this were measurably worse than the
    // truth (2026-08-09ai).
    MachineStackPlacement baseline = machine_fast_placement_build_pinned(arena, function, 0, 0, 0);
    if (!baseline.valid)
    {
        scratch_end(scratch);
        return baseline;
    }
    u32* baseline_traffic = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        baseline_traffic[register_index] = 0;
    }
    for (u32 edit_index = 0; edit_index < baseline.edit_count; edit_index += 1)
    {
        MachineEdit* edit = baseline.edits + edit_index;
        if (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD)
        {
            baseline_traffic[edit->subject] += 1;
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
        // A pin must remove more memory operations than the push and pop
        // it adds, so the value has to have cost the local scan at least
        // three. Priority is the measured traffic, not the guess.
        if (disqualified[register_index] || interval_starts[register_index] == UINT32_MAX || baseline_traffic[register_index] < 3)
        {
            continue;
        }
        heap[heap_count] = (MachineQualityInterval){
            .virtual_register = register_index,
            .start = interval_starts[register_index],
            .end = interval_ends[register_index],
            .weight = baseline_traffic[register_index],
        };
        heap_count += 1;
    }
    for (u32 root = heap_count / 2; root > 0; root -= 1)
    {
        machine_quality_heap_sift(heap, heap_count, root - 1);
    }
    u32 pin_file_count = BUSTER_MIN(description->quality_pin_register_count, MACHINE_TARGET_QUALITY_PIN_LIMIT);
    u32 assigned_starts[MACHINE_TARGET_QUALITY_PIN_LIMIT][8];
    u32 assigned_ends[MACHINE_TARGET_QUALITY_PIN_LIMIT][8];
    u32 assigned_counts[MACHINE_TARGET_QUALITY_PIN_LIMIT];
    for (u32 file_index = 0; file_index < pin_file_count; file_index += 1)
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
        for (u32 file_index = 0; file_index < pin_file_count; file_index += 1)
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
            pinned_registers[candidate.virtual_register] = description->quality_pin_registers[file_index];
            pinned_mask |= 1u << description->quality_pin_registers[file_index];
            break;
        }
    }
    u32 baseline_traffic_total = baseline.reload_count + baseline.spill_count;
    if (!pinned_mask || !function->instruction_count)
    {
        scratch_end(scratch);
        return baseline;
    }
    // Span scoping. One register mask per instruction paints where each
    // pin actually holds; the local scan owns the register everywhere
    // outside. The entry masks mark each span's first instruction, the
    // one point where a spill naming the register is legitimate — the
    // eviction that hands it over.
    u32* pin_active_masks = arena_allocate(scratch.arena, u32, function->instruction_count);
    u32* pin_entry_masks = arena_allocate(scratch.arena, u32, function->instruction_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        pin_active_masks[instruction_index] = 0;
        pin_entry_masks[instruction_index] = 0;
    }
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        if (pinned_registers[register_index] == UINT32_MAX)
        {
            continue;
        }
        u32 pin_bit = 1u << pinned_registers[register_index];
        pin_entry_masks[interval_starts[register_index]] |= pin_bit;
        for (u32 instruction_index = interval_starts[register_index]; instruction_index <= interval_ends[register_index]; instruction_index += 1)
        {
            pin_active_masks[instruction_index] |= pin_bit;
        }
    }
    MachineStackPlacement placement = machine_fast_placement_build_pinned(arena, function, pinned_registers, pinned_mask, pin_active_masks);
    // Accept only on modeled improvement: the traffic removed has to beat
    // the push and pop each newly reserved register costs at entry, and a
    // pin also raises local pressure, which can add traffic elsewhere.
    u32 baseline_saved_registers = 0;
    for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
    {
        baseline_saved_registers += (baseline.callee_saved_mask >> physical_register) & 1u;
    }
    u32 placement_saved_registers = 0;
    for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
    {
        placement_saved_registers += (placement.callee_saved_mask >> physical_register) & 1u;
    }
    u32 added_prologue_cost = 2 * (placement_saved_registers - BUSTER_MIN(placement_saved_registers, baseline_saved_registers));
    u32 placement_traffic_total = placement.reload_count + placement.spill_count;
    if (!placement.valid || placement_traffic_total + added_prologue_cost >= baseline_traffic_total)
    {
        scratch_end(scratch);
        return baseline;
    }
    // Pin verification. Within its span a pinned register belongs to its
    // value: no other value's operand may sit in it there, no edit may
    // name it as a target — except the spill that vacates it where the
    // span opens — and no copy may read it as a source. The local scan
    // has several paths that bypass its own register pool — copy
    // coalescing was one — and a wrong pin is a silent miscompile, so the
    // placement is checked rather than trusted, and a violation falls
    // back to the local allocator alone.
    bool pins_hold = true;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count && pins_hold; instruction_index += 1)
    {
        u32 active = pin_active_masks[instruction_index];
        if (!active)
        {
            continue;
        }
        MachineInstruction* instruction = function->instructions + instruction_index;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        u8 const* operand_registers = placement.operand_registers + (u64)instruction_index * 4;
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            if (operand_registers[slot] == UINT8_MAX || !((active >> operand_registers[slot]) & 1u))
            {
                continue;
            }
            MachineRef ref = instruction->operands[slot];
            pins_hold &= machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER && pinned_registers[machine_ref_payload(ref)] == operand_registers[slot];
        }
    }
    for (u32 edit_index = 0; edit_index < placement.edit_count && pins_hold; edit_index += 1)
    {
        MachineEdit* edit = placement.edits + edit_index;
        u32 active = pin_active_masks[machine_point_instruction(edit->point)];
        bool vacating = edit->kind == MACHINE_EDIT_SPILL && ((pin_entry_masks[machine_point_instruction(edit->point)] >> edit->location) & 1u);
        pins_hold &= !((active >> edit->location) & 1u) || vacating;
        pins_hold &= edit->kind != MACHINE_EDIT_COPY || !((active >> edit->subject) & 1u);
    }
    scratch_end(scratch);
    if (!pins_hold)
    {
        return baseline;
    }
    placement.pinned_register_count = 0;
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        placement.pinned_register_count += pinned_registers[register_index] != UINT32_MAX;
    }
    return placement;
}
