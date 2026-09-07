#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/simd.h>

// QRA stages 7 and 8: global assignment over the machine IR. A live
// interval per virtual register, a weight that pays for keeping it in a
// register, and a priority walk that hands the highest-weight
// non-overlapping intervals a callee-saved register. Everything the walk
// cannot place falls to the local scan, so QUALITY is FAST plus a global
// layer rather than a second allocator to keep correct. Weights and the
// acceptance compare are in frequency-weighted units: every edit is
// priced by the loop depth of the block it executes in, because the
// unweighted economics weighed a cold-path edit equal to a hot one and
// every static refinement of that measured negative (2026-08-10l).
//
// Stage 8 scopes each reservation to the interval that earned it: the
// local scan owns a pin register at every instruction no pinned span
// covers, so binding the whole callee-saved file no longer starves the
// scan the way the whole-function pins of 2026-08-09al measured.
//
// Live-range splitting — the half of plan stage 7 the span-pin design
// deferred: a candidate whose whole interval cannot be placed may pin
// one merged loop region of it instead — the sub-span where its
// weighted traffic actually sits — with the boundary work in code the
// loop never repeats: entry installs ride the edge contracts of the
// region head's entering edges, and a value living past the region
// stores back at landing-pad blocks every exit passes at most once per
// function entry. A split span is exactly one merged region, which
// preserves the closure every span already leans on — a span meeting a
// loop covers it. The split gate reuses the same per-instruction
// frequency weights the whole-placement economics price everything
// else in: boundary rows sit outside the region by construction, so
// they price at their own (lower) block frequency with no separate
// constant needed, and the final whole-placement acceptance below
// weighs a split's entry/store edits exactly like any other edit.
//
// Callee-saved is the register class that makes a span binding sound
// without a clobber analysis: calls preserve those registers, and every
// encoder scratch and macro-op sequence in this backend works out of the
// caller-saved half. Interval construction scans every textual touch; it does
// not infer a live range from definition_point, so explicitly mutable values
// retain the conservative FAST/QUALITY behavior until promotion becomes SSA.

// Bounds keep the pass linear-ish and its worst case reportable.
#define MACHINE_QUALITY_MAXIMUM_CANDIDATES 4096

// Free registers an instruction keeps above its own register operands,
// so the local scan's LRU picks always have a candidate that is not one
// of the same row's operands. Rows whose operands are all forced into
// fixed registers — constrained layouts, the staging forms, calls — pick
// nothing and need no spare at all, which is what lets the callee-saved
// pins keep spanning call rows that foreclose the whole caller-saved
// file.
#define MACHINE_QUALITY_PIN_OPERAND_MARGIN 2u

// Rows a register's split boundary edits write outside any of its spans
// — entry installs at the entering terminators, landing-pad stores at
// the exit blocks — which no other span on the same register may cover.
#define MACHINE_QUALITY_EXCLUDED_ROW_LIMIT 16u
// Plan capacities; a function splitting past these keeps its remaining
// candidates whole rather than growing the plan.
#define MACHINE_QUALITY_SPLIT_ENTRY_LIMIT 128u
#define MACHINE_QUALITY_SPLIT_STORE_LIMIT 256u

// Execution-frequency weight of one block: 4 to the power of its
// frequency class (the loop-nesting depth the selector stamped), capped
// so a pathological nest cannot overflow the traffic sums. Class 0 keeps
// weight 1, so straight-line functions price exactly as they did when
// every edit counted once. Every static refinement of unweighted counts
// measured negative (2026-08-10l); the weighted form exists because the
// blind spot was where edits execute, not what was counted.
#define MACHINE_QUALITY_FREQUENCY_WEIGHT_SHIFT 2u
#define MACHINE_QUALITY_FREQUENCY_WEIGHT_LIMIT_SHIFT 12u

BUSTER_GLOBAL_LOCAL u32 machine_quality_frequency_weight(u16 frequency_class)
{
    u32 shift = BUSTER_MIN((u32)frequency_class * MACHINE_QUALITY_FREQUENCY_WEIGHT_SHIFT, MACHINE_QUALITY_FREQUENCY_WEIGHT_LIMIT_SHIFT);
    return 1u << shift;
}

typedef struct MachineQualityInterval MachineQualityInterval;
struct MachineQualityInterval
{
    u32 virtual_register;
    u32 start;
    u32 end;
    u32 weight;
    // A candidate below the raw-count break-even, admitted on frequency
    // alone; it may only take a caller-saved pin register, so it can
    // never buy a prologue save with edits a cold loop may never run.
    // Split probing honors the same restriction for the same reason.
    u32 marginal;
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

// The merged loop region containing an instruction, or UINT32_MAX. The
// regions are disjoint and sorted, so the last one starting at or before
// the instruction is the only candidate.
BUSTER_GLOBAL_LOCAL u32 machine_quality_region_find(u64 const* spans, u32 count, u32 instruction_index)
{
    u32 low = 0;
    u32 high = count;
    while (low < high)
    {
        u32 middle = (low + high) / 2;
        if ((u32)(spans[middle] >> 32) <= instruction_index)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low && instruction_index <= (u32)spans[low - 1] ? low - 1 : UINT32_MAX;
}

// Foreclosure-prefix rows build lazily, on a register's first probe: the
// pin file is walked front-first and most functions place their pins
// within the callee-saved prefix, so eagerly summing every allocatable
// row was mostly work nobody read. Rows are pin-independent, so one
// build serves the whole-placement loop and the split probe alike, and
// persists across every attempt in the same pass.
BUSTER_GLOBAL_LOCAL u32* machine_quality_foreclosure_prefix_ensure(u32* foreclosure_prefix, u32* prefix_built_mask, u64 const* foreclosed_masks,
                                                                    u32 instruction_count, u32 pin_register)
{
    u32* prefix = foreclosure_prefix + (u64)pin_register * (instruction_count + 1);
    if (!((*prefix_built_mask >> pin_register) & 1u))
    {
        *prefix_built_mask |= 1u << pin_register;
        prefix[0] = 0;
        for (u32 instruction_index = 0; instruction_index < instruction_count; instruction_index += 1)
        {
            prefix[instruction_index + 1] = prefix[instruction_index] + ((foreclosed_masks[instruction_index] >> pin_register) & 1u);
        }
    }
    return prefix;
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
    // Frequency classes are consumed only by the economics below, so only
    // QUALITY pays the stamping walk — measured at +0.9% of fast-mode
    // compile cost when every machine mode stamped at selection.
    machine_function_stamp_frequency_classes(function);
    TemporalArena scratch = scratch_begin(&arena, 1);
    u32 register_count = function->virtual_register_count;
    u32* pinned_registers = arena_allocate(arena, u32, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        pinned_registers[register_index] = UINT32_MAX;
    }
    // One prepass feeds this whole pass and both scan runs below: the touch
    // intervals with their constrained-opcode disqualifications (a value
    // touched by an opcode whose encoder pins its operands cannot hold an
    // arbitrary register for its whole life), the raw backward-edge spans,
    // and everything the local scan derives that no pin set changes.
    MachineFastPrepass prepass = machine_fast_prepass_build(scratch.arena, function, true);
    if (!prepass.valid)
    {
        scratch_end(scratch);
        return (MachineStackPlacement){0};
    }
    u32* interval_starts = prepass.interval_starts;
    u32* interval_ends = prepass.interval_ends;
    u8* disqualified = prepass.disqualified;
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
    // so nothing cascades and nothing needs a second look. The merged,
    // sorted spans double as the split candidate's region table below.
    u64* loop_spans = prepass.loop_spans;
    u32 loop_span_count = prepass.loop_span_count;
    u64* loop_span_scratch = arena_allocate(scratch.arena, u64, loop_span_count ? loop_span_count : 1);
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
    MachineStackPlacement baseline = machine_fast_placement_build_prepassed(arena, function, &prepass, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!baseline.valid)
    {
        scratch_end(scratch);
        return baseline;
    }
    // Per-instruction frequency weights from the classes the selector
    // stamped. An edit is priced by the block its instruction sits in, so
    // a reload inside a loop outweighs the same reload on the entry path —
    // the execution-frequency awareness 2026-08-10l concluded every static
    // count lacks. A split's boundary edits (entry installs, landing-pad
    // stores) land at rows outside the region by construction, so this
    // same array prices them at their own — lower — enclosing frequency
    // with no separate accounting.
    u32* instruction_weights = arena_allocate(scratch.arena, u32, function->instruction_count ? function->instruction_count : 1);
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        u32 block_weight = machine_quality_frequency_weight(block->frequency_class);
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            instruction_weights[block->first_instruction + offset] = block_weight;
        }
    }
    u32* baseline_traffic = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    u32* baseline_traffic_counts = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        baseline_traffic[register_index] = 0;
        baseline_traffic_counts[register_index] = 0;
    }
    u64 baseline_traffic_total = 0;
    for (u32 edit_index = 0; edit_index < baseline.edit_count; edit_index += 1)
    {
        MachineEdit* edit = baseline.edits + edit_index;
        if (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD)
        {
            u32 edit_weight = instruction_weights[machine_point_instruction(edit->point)];
            baseline_traffic[edit->subject] += edit_weight;
            baseline_traffic_counts[edit->subject] += 1;
            baseline_traffic_total += edit_weight;
        }
    }
    // Priority walk. The heap orders by weight; each candidate takes the
    // first callee-saved register whose assigned spans it misses entirely.
    // `candidate_indices` maps a virtual register back to its heap slot so
    // the split probe below can look up its per-region traffic without a
    // linear search.
    u32* candidate_indices = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        candidate_indices[register_index] = UINT32_MAX;
    }
    MachineQualityInterval* heap = arena_allocate(scratch.arena, MachineQualityInterval, MACHINE_QUALITY_MAXIMUM_CANDIDATES);
    u32 heap_count = 0;
    for (u32 register_index = 0; register_index < register_count && heap_count < MACHINE_QUALITY_MAXIMUM_CANDIDATES; register_index += 1)
    {
        // The threshold is the break-even point: a pin must remove more
        // memory traffic than it costs. A value whose raw count reaches
        // three pays for a callee-saved push/pop the way stage 8 priced
        // it. Below that the value is marginal: its bar is frequency —
        // one in-loop edit clears the same weighted three — but it may
        // only take a caller-saved pin register in the walk below (and in
        // any split probe it enters), where the reservation costs no
        // prologue and a span crossing a call can never bind, because
        // calls foreclose the whole caller-saved file. The unrestricted
        // weighted form re-created the threshold-2 loss of 2026-08-10l at
        // scale (+6,865 pins, +284M on the frozen stage): a static loop is
        // not evidence the loop runs, so a marginal pin may only take a
        // register that is free. Priority is the measured weighted
        // traffic, not a guess. The pin file is general registers, so only
        // general-class values may enter candidacy: on System V no vector
        // register survives a call, and a vector pinned to a general
        // register would be a class miscompile.
        if (disqualified[register_index] || interval_starts[register_index] == UINT32_MAX || baseline_traffic[register_index] < 3 ||
            function->virtual_registers[register_index].register_class != MACHINE_REGISTER_CLASS_GENERAL)
        {
            continue;
        }
        candidate_indices[register_index] = heap_count;
        heap[heap_count] = (MachineQualityInterval){
            .virtual_register = register_index,
            .start = interval_starts[register_index],
            .end = interval_ends[register_index],
            .weight = baseline_traffic[register_index],
            .marginal = baseline_traffic_counts[register_index] < 3,
        };
        heap_count += 1;
    }
    for (u32 root = heap_count / 2; root > 0; root -= 1)
    {
        machine_quality_heap_sift(heap, heap_count, root - 1);
    }
    if (!heap_count || !function->instruction_count)
    {
        scratch_end(scratch);
        return baseline;
    }
    // The pin file: the target's callee-saved preference list first — the
    // heaviest values take the registers that survive everything, and the
    // local scan's caller-saved working pool is eaten last — then the
    // caller-saved members, descending, which only a span that crosses no
    // call may take and whose reservation costs no prologue save. The
    // degraded attempts below drop splitting first and then keep only the
    // callee-saved prefix.
    u32 pin_file[MACHINE_TARGET_REGISTER_LIMIT];
    u32 pin_file_count = 0;
    u64 caller_saved_allocatable = description->allocatable_mask & ~description->callee_saved_mask;
    u32 quality_pin_count = BUSTER_MIN(description->quality_pin_register_count, MACHINE_TARGET_QUALITY_PIN_LIMIT);
    for (u32 file_index = 0; file_index < quality_pin_count; file_index += 1)
    {
        pin_file[pin_file_count] = description->quality_pin_registers[file_index];
        pin_file_count += 1;
    }
    for (u32 physical_register = description->register_count; physical_register > 0; physical_register -= 1)
    {
        if ((caller_saved_allocatable >> (physical_register - 1)) & 1u)
        {
            pin_file[pin_file_count] = physical_register - 1;
            pin_file_count += 1;
        }
    }
    // Per-instruction foreclosures: the registers a span may not hold at
    // that instruction. A call forecloses every caller-saved register, a
    // constrained row the scratch of each populated register slot — the
    // slots the local scan actually forces there — a physical operand or
    // a declared clobber its named register, and the fixed-register
    // staging forms — the float bridge, the indirect callee — their stage
    // register. Per-register prefix counts make each candidate's
    // span-legality probe O(1), and the budget keeps each row's free-pick
    // need above its pins and foreclosures so the local scan always has
    // room to work.
    u32 allocatable_count = 0;
    for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
    {
        allocatable_count += (description->allocatable_mask >> physical_register) & 1u;
    }
    // Only allocatable registers are ever probed through the pin file, so
    // the prefix table stops at the highest allocatable index instead of
    // paying the unified file's vector tail for every instruction.
    u32 prefix_register_limit = 0;
    for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
    {
        prefix_register_limit = ((description->allocatable_mask >> physical_register) & 1u) ? physical_register + 1 : prefix_register_limit;
    }
    u32* foreclosure_prefix = arena_allocate(scratch.arena, u32, (u64)(function->instruction_count + 1) * prefix_register_limit);
    u8* pin_budgets = arena_allocate(scratch.arena, u8, function->instruction_count);
    u8* pin_depths = arena_allocate(scratch.arena, u8, function->instruction_count);
    u64* foreclosed_masks = arena_allocate(scratch.arena, u64, function->instruction_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        MachineInstruction* instruction = function->instructions + instruction_index;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        u64 foreclosed = info->clobber_mask;
        if (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL)
        {
            foreclosed |= caller_saved_allocatable;
        }
        bool legacy_constrained = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED) != 0;
        bool constrained = legacy_constrained || info->fixed_register_mask || info->early_clobber_mask || info->fixed_register_set;
        u32 register_operand_slots = 0;
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            MachineRefKind kind = machine_ref_kind(instruction->operands[slot]);
            if (kind == MACHINE_REF_PHYSICAL_REGISTER)
            {
                foreclosed |= 1ull << machine_ref_payload(instruction->operands[slot]);
            }
            else if (kind == MACHINE_REF_VIRTUAL_REGISTER)
            {
                register_operand_slots += 1;
                u32 fixed = machine_opcode_fixed_register(info, slot);
                if (fixed != UINT32_MAX && fixed < MACHINE_TARGET_REGISTER_LIMIT)
                {
                    foreclosed |= 1ull << fixed;
                }
                else if (legacy_constrained)
                {
                    u32 operand_class = (info->operand_info[slot] >> MACHINE_OPERAND_CLASS_SHIFT) & 0x7u;
                    u32 scratch_register = operand_class == MACHINE_REGISTER_CLASS_VECTOR ? description->vector_slot_scratch[slot] : description->slot_scratch[slot];
                    foreclosed |= 1ull << scratch_register;
                }
            }
        }
        if (instruction->opcode == description->float_bridge_opcode)
        {
            foreclosed |= 1ull << description->float_bridge_register;
        }
        if (instruction->opcode == description->indirect_call_opcode)
        {
            foreclosed |= 1ull << description->indirect_call_register;
        }
        foreclosed_masks[instruction_index] = foreclosed;
        // mask64_count rather than the raw builtin: MSVC has no
        // __builtin_popcountll, and simd.h already owns that portability
        // decision.
        u32 foreclosed_count = mask64_count((Mask64)(foreclosed & description->allocatable_mask));
        // A row whose register operands are all forced into fixed
        // registers picks nothing; every other row needs room for its
        // own operands plus the margin.
        bool operands_forced =
            constrained || instruction->opcode == description->float_bridge_opcode || instruction->opcode == description->indirect_call_opcode;
        u32 spare = !operands_forced && register_operand_slots ? register_operand_slots + MACHINE_QUALITY_PIN_OPERAND_MARGIN : 0;
        u32 headroom = allocatable_count - foreclosed_count;
        pin_budgets[instruction_index] = (u8)(headroom > spare ? headroom - spare : 0);
    }
    // Region metadata for splitting, all one pass over the block-ref
    // operands. A region accepts splits only when every entering edge can
    // host the install — a single-target terminator reaching the head
    // from outside, the shape the retroactive conform repairs fully — and
    // no edge from outside lands mid-region, where the install would
    // never have run. Exit stores additionally need every departure
    // target to be a landing pad: all of its predecessors inside the
    // region, and the block itself inside no region at all, which is what
    // proves its head runs at most once per function entry. `cold_blocks`
    // comes straight from the prepass — the same switch/both-backward
    // rule this needed locally before the prepass carried it.
    u8 const* cold_blocks = prepass.cold_blocks;
    u32* block_regions = 0;
    u32* region_head_blocks = 0;
    u8* region_ok = 0;
    u8* region_exit_ok = 0;
    u32* region_entry_counts = 0;
    u32* region_entry_offsets = 0;
    u32* region_entry_rows = 0;
    u32* region_exit_offsets = 0;
    u32* region_exit_counts = 0;
    u32* region_exit_blocks = 0;
    u32* candidate_region_traffic = 0;
    if (merged_span_count)
    {
        u32* instruction_blocks = arena_allocate(scratch.arena, u32, function->instruction_count);
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            MachineBlock* block = function->blocks + block_index;
            for (u32 offset = 0; offset < block->instruction_count; offset += 1)
            {
                instruction_blocks[block->first_instruction + offset] = block_index;
            }
        }
        block_regions = arena_allocate(scratch.arena, u32, function->block_count ? function->block_count : 1);
        region_head_blocks = arena_allocate(scratch.arena, u32, merged_span_count);
        region_ok = arena_allocate(scratch.arena, u8, merged_span_count);
        region_exit_ok = arena_allocate(scratch.arena, u8, merged_span_count);
        region_entry_counts = arena_allocate(scratch.arena, u32, merged_span_count);
        region_entry_offsets = arena_allocate(scratch.arena, u32, merged_span_count + 1);
        region_exit_offsets = arena_allocate(scratch.arena, u32, merged_span_count + 1);
        region_exit_counts = arena_allocate(scratch.arena, u32, merged_span_count);
        u32* region_exit_capacities = arena_allocate(scratch.arena, u32, merged_span_count);
        // Sole predecessor region per block: UINT32_MAX while no edge has
        // arrived, the region index while every edge agrees, and
        // merged_span_count once any edge comes from elsewhere.
        u32* sole_pred_regions = arena_allocate(scratch.arena, u32, function->block_count ? function->block_count : 1);
        for (u32 region_index = 0; region_index < merged_span_count; region_index += 1)
        {
            region_head_blocks[region_index] = UINT32_MAX;
            region_ok[region_index] = 1;
            region_exit_ok[region_index] = 1;
            region_entry_counts[region_index] = 0;
            region_exit_counts[region_index] = 0;
            region_exit_capacities[region_index] = 0;
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            MachineBlock* block = function->blocks + block_index;
            u32 region_index = machine_quality_region_find(loop_spans, merged_span_count, block->first_instruction);
            block_regions[block_index] = region_index;
            if (region_index != UINT32_MAX && (u32)(loop_spans[region_index] >> 32) == block->first_instruction)
            {
                region_head_blocks[region_index] = block_index;
            }
            sole_pred_regions[block_index] = UINT32_MAX;
        }
        for (u32 pass = 0; pass < 2; pass += 1)
        {
            if (pass == 1)
            {
                region_entry_offsets[0] = 0;
                region_exit_offsets[0] = 0;
                for (u32 region_index = 0; region_index < merged_span_count; region_index += 1)
                {
                    region_entry_offsets[region_index + 1] = region_entry_offsets[region_index] + region_entry_counts[region_index];
                    region_exit_offsets[region_index + 1] = region_exit_offsets[region_index] + region_exit_capacities[region_index];
                    region_entry_counts[region_index] = 0;
                }
                region_entry_rows = arena_allocate(scratch.arena, u32, region_entry_offsets[merged_span_count] ? region_entry_offsets[merged_span_count] : 1);
                region_exit_blocks = arena_allocate(scratch.arena, u32, region_exit_offsets[merged_span_count] ? region_exit_offsets[merged_span_count] : 1);
            }
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                MachineInstruction* instruction = function->instructions + instruction_index;
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                u32 targets[BUSTER_ARRAY_LENGTH(instruction->operands)];
                u32 target_count = 0;
                for (u32 slot = 0; slot < info->operand_count; slot += 1)
                {
                    if (machine_ref_kind(instruction->operands[slot]) == MACHINE_REF_BLOCK)
                    {
                        targets[target_count] = machine_ref_payload(instruction->operands[slot]);
                        target_count += 1;
                    }
                }
                if (!target_count)
                {
                    continue;
                }
                u32 source_block = instruction_blocks[instruction_index];
                u32 source_region = block_regions[source_block];
                for (u32 target_index = 0; target_index < target_count; target_index += 1)
                {
                    u32 target_block = targets[target_index];
                    u32 target_region = block_regions[target_block];
                    if (pass == 0)
                    {
                        u32 incoming = source_region == UINT32_MAX ? merged_span_count : source_region;
                        u32 sole = sole_pred_regions[target_block];
                        sole_pred_regions[target_block] = sole == UINT32_MAX || sole == incoming ? incoming : merged_span_count;
                    }
                    if (target_region != UINT32_MAX && target_region != source_region)
                    {
                        if (target_block == region_head_blocks[target_region])
                        {
                            if (pass == 0)
                            {
                                region_entry_counts[target_region] += 1;
                                if (target_count != 1 || target_block <= source_block)
                                {
                                    region_ok[target_region] = 0;
                                }
                            }
                            else
                            {
                                region_entry_rows[region_entry_offsets[target_region] + region_entry_counts[target_region]] = instruction_index;
                                region_entry_counts[target_region] += 1;
                            }
                        }
                        else if (pass == 0)
                        {
                            region_ok[target_region] = 0;
                        }
                    }
                    if (source_region != UINT32_MAX && target_region != source_region)
                    {
                        if (pass == 0)
                        {
                            region_exit_capacities[source_region] += 1;
                            if (target_region != UINT32_MAX)
                            {
                                region_exit_ok[source_region] = 0;
                            }
                        }
                        else
                        {
                            u32* successors = region_exit_blocks + region_exit_offsets[source_region];
                            bool seen = false;
                            for (u32 successor_index = 0; successor_index < region_exit_counts[source_region]; successor_index += 1)
                            {
                                seen |= successors[successor_index] == target_block;
                            }
                            if (!seen)
                            {
                                successors[region_exit_counts[source_region]] = target_block;
                                region_exit_counts[source_region] += 1;
                            }
                        }
                    }
                }
            }
        }
        for (u32 region_index = 0; region_index < merged_span_count; region_index += 1)
        {
            u32 head = region_head_blocks[region_index];
            if (head == UINT32_MAX || head == 0 || cold_blocks[head] || !region_entry_counts[region_index])
            {
                region_ok[region_index] = 0;
            }
            for (u32 successor_index = 0; successor_index < region_exit_counts[region_index]; successor_index += 1)
            {
                u32 successor = region_exit_blocks[region_exit_offsets[region_index] + successor_index];
                if (sole_pred_regions[successor] != region_index)
                {
                    region_exit_ok[region_index] = 0;
                }
            }
        }
        // Where each candidate's traffic actually sits: its baseline
        // edits bucketed by the region containing them, weighted exactly
        // like the whole-placement economics — each edit prices at its
        // own row's frequency, so a merged region spanning more than one
        // nesting depth still weighs its inner rows heavier than its
        // outer ones. This is the per-candidate number the split gate
        // compares against the boundary work — never a reorder of the
        // priority heap, which 2026-08-10l measured as wrong at every
        // weight.
        candidate_region_traffic = arena_allocate(scratch.arena, u32, (u64)heap_count * merged_span_count);
        for (u64 cell_index = 0; cell_index < (u64)heap_count * merged_span_count; cell_index += 1)
        {
            candidate_region_traffic[cell_index] = 0;
        }
        for (u32 edit_index = 0; edit_index < baseline.edit_count; edit_index += 1)
        {
            MachineEdit* edit = baseline.edits + edit_index;
            if (edit->kind != MACHINE_EDIT_SPILL && edit->kind != MACHINE_EDIT_RELOAD)
            {
                continue;
            }
            u32 candidate_slot = candidate_indices[edit->subject];
            if (candidate_slot == UINT32_MAX)
            {
                continue;
            }
            u32 edit_instruction = machine_point_instruction(edit->point);
            u32 region_index = machine_quality_region_find(loop_spans, merged_span_count, edit_instruction);
            if (region_index != UINT32_MAX)
            {
                candidate_region_traffic[(u64)candidate_slot * merged_span_count + region_index] += instruction_weights[edit_instruction];
            }
        }
    }
    MachineQualityInterval* heap_backup = arena_allocate(scratch.arena, MachineQualityInterval, heap_count);
    for (u32 backup_index = 0; backup_index < heap_count; backup_index += 1)
    {
        heap_backup[backup_index] = heap[backup_index];
    }
    u32 heap_backup_count = heap_count;
    u64* pin_active_masks = arena_allocate(scratch.arena, u64, function->instruction_count);
    u64* pin_entry_masks = arena_allocate(scratch.arena, u64, function->instruction_count);
    u32* span_starts = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    u32* span_ends = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
    u32 assigned_starts[MACHINE_TARGET_REGISTER_LIMIT][8];
    u32 assigned_ends[MACHINE_TARGET_REGISTER_LIMIT][8];
    u32 assigned_counts[MACHINE_TARGET_REGISTER_LIMIT];
    u32 excluded_rows[MACHINE_TARGET_REGISTER_LIMIT][MACHINE_QUALITY_EXCLUDED_ROW_LIMIT];
    u32 excluded_row_counts[MACHINE_TARGET_REGISTER_LIMIT];
    MachinePinSplitEntry split_entries[MACHINE_QUALITY_SPLIT_ENTRY_LIMIT];
    MachinePinSplitStore split_stores[MACHINE_QUALITY_SPLIT_STORE_LIMIT];
    // Prefix rows are pin-independent, so one build serves every attempt;
    // the mask persists across the attempt loop below.
    u32 prefix_built_mask = 0;
    // Attempt 0 packs the full file with splitting; a rejected or
    // unverifiable placement retries without the machinery it actually
    // used — splits first, then the caller-saved file — so over-pinning
    // one hot loop cannot cost a function the pins that were winning.
    for (u32 attempt = 0; attempt < 3; attempt += 1)
    {
        bool allow_splits = attempt == 0 && merged_span_count != 0;
        u32 const* attempt_file = pin_file;
        u32 attempt_file_count = attempt == 2 ? quality_pin_count : pin_file_count;
        if (!attempt_file_count)
        {
            continue;
        }
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            pinned_registers[register_index] = UINT32_MAX;
            span_starts[register_index] = interval_starts[register_index];
            span_ends[register_index] = interval_ends[register_index];
        }
        for (u32 file_index = 0; file_index < attempt_file_count; file_index += 1)
        {
            assigned_counts[file_index] = 0;
            excluded_row_counts[file_index] = 0;
        }
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            pin_depths[instruction_index] = 0;
        }
        heap_count = heap_backup_count;
        for (u32 backup_index = 0; backup_index < heap_backup_count; backup_index += 1)
        {
            heap[backup_index] = heap_backup[backup_index];
        }
        u64 pinned_mask = 0;
        u32 split_entry_count = 0;
        u32 split_store_count = 0;
        while (heap_count)
        {
            MachineQualityInterval candidate = heap[0];
            heap_count -= 1;
            heap[0] = heap[heap_count];
            machine_quality_heap_sift(heap, heap_count, 0);
            // A marginal candidate almost always dies on the call-crossing
            // check in call-dense code, and its foreclosure probes are O(1)
            // where the budget walk below is O(span): reject it on the
            // prefixes first. Outcome-neutral — every acceptance condition
            // must hold and none mutates state before a pin lands. The
            // prefix row must go through the same lazy-build guard as
            // every other reader: arenas hand out dirty, not zeroed,
            // memory on reuse (`arena_allocate_bytes`), so probing an
            // unbuilt row directly reads whatever an earlier function's
            // scratch pass left there — a real, previously-latent source
            // of run-to-run nondeterminism this early-reject must not
            // reintroduce.
            if (candidate.marginal)
            {
                bool caller_saved_open = false;
                for (u32 file_index = quality_pin_count; file_index < attempt_file_count && !caller_saved_open; file_index += 1)
                {
                    u32 pin_register = attempt_file[file_index];
                    u32 const* prefix = machine_quality_foreclosure_prefix_ensure(foreclosure_prefix, &prefix_built_mask, foreclosed_masks,
                                                                                  function->instruction_count, pin_register);
                    caller_saved_open = prefix[candidate.end + 1] == prefix[candidate.start];
                }
                if (!caller_saved_open)
                {
                    continue;
                }
            }
            // The budget is register-independent, so a candidate that
            // would leave any covered instruction without its spare is
            // dropped before any register is probed.
            bool over_budget = false;
            for (u32 instruction_index = candidate.start; instruction_index <= candidate.end && !over_budget; instruction_index += 1)
            {
                over_budget = pin_depths[instruction_index] >= pin_budgets[instruction_index];
            }
            bool assigned = false;
            if (!over_budget)
            {
                // A marginal candidate starts past the callee-saved
                // prefix: caller-saved registers only, so it costs no
                // prologue save, and in the degraded attempt — whose file
                // is the prefix alone — it never pins at all.
                for (u32 file_index = candidate.marginal ? quality_pin_count : 0; file_index < attempt_file_count; file_index += 1)
                {
                    u32 pin_register = attempt_file[file_index];
                    if (assigned_counts[file_index] == BUSTER_ARRAY_LENGTH(assigned_starts[0]))
                    {
                        continue;
                    }
                    u32* prefix = machine_quality_foreclosure_prefix_ensure(foreclosure_prefix, &prefix_built_mask, foreclosed_masks,
                                                                            function->instruction_count, pin_register);
                    if (prefix[candidate.end + 1] != prefix[candidate.start])
                    {
                        continue;
                    }
                    bool overlaps = false;
                    for (u32 span = 0; span < assigned_counts[file_index]; span += 1)
                    {
                        overlaps |= candidate.start <= assigned_ends[file_index][span] && assigned_starts[file_index][span] <= candidate.end;
                    }
                    for (u32 row_index = 0; row_index < excluded_row_counts[file_index]; row_index += 1)
                    {
                        overlaps |= candidate.start <= excluded_rows[file_index][row_index] && excluded_rows[file_index][row_index] <= candidate.end;
                    }
                    if (overlaps)
                    {
                        continue;
                    }
                    assigned_starts[file_index][assigned_counts[file_index]] = candidate.start;
                    assigned_ends[file_index][assigned_counts[file_index]] = candidate.end;
                    assigned_counts[file_index] += 1;
                    pinned_registers[candidate.virtual_register] = pin_register;
                    pinned_mask |= 1ull << pin_register;
                    for (u32 instruction_index = candidate.start; instruction_index <= candidate.end; instruction_index += 1)
                    {
                        pin_depths[instruction_index] += 1;
                    }
                    assigned = true;
                    break;
                }
            }
            u32 candidate_slot = candidate_indices[candidate.virtual_register];
            if (assigned || !allow_splits || !candidate_region_traffic || candidate_slot == UINT32_MAX)
            {
                continue;
            }
            // Split probe: the candidate could not be placed whole, so try
            // pinning exactly one merged region of it — regions in
            // descending order of the candidate's weighted traffic inside
            // them, each gated on modeled improvement for this candidate
            // alone: the weighted traffic the sub-span removes must beat
            // the boundary edits it adds (one install per entering edge,
            // one store per landing pad when the value lives past the
            // region, each priced by its own row's frequency through the
            // same `instruction_weights` array the whole-placement
            // economics use) plus the push/pop pair when the register
            // would newly join the saved set, at the unweighted cost the
            // prologue always prices at. A marginal candidate keeps its
            // caller-saved-only restriction here too, for the reason
            // candidacy imposed it.
            u32 previous_traffic = UINT32_MAX;
            u32 previous_region = UINT32_MAX;
            while (!assigned)
            {
                u32 best_region = UINT32_MAX;
                u32 best_traffic = 0;
                for (u32 region_index = 0; region_index < merged_span_count; region_index += 1)
                {
                    u32 traffic = candidate_region_traffic[(u64)candidate_slot * merged_span_count + region_index];
                    if (!traffic || traffic > previous_traffic || (traffic == previous_traffic && region_index <= previous_region))
                    {
                        continue;
                    }
                    if (traffic > best_traffic)
                    {
                        best_traffic = traffic;
                        best_region = region_index;
                    }
                }
                if (best_region == UINT32_MAX)
                {
                    break;
                }
                previous_traffic = best_traffic;
                previous_region = best_region;
                u32 region_start = (u32)(loop_spans[best_region] >> 32);
                u32 region_end = (u32)loop_spans[best_region];
                // The interval must begin before the region — the entry
                // install needs a value to install — and cover it whole,
                // which meeting it guarantees through the extension.
                if (!region_ok[best_region] || candidate.start >= region_start || candidate.end < region_end)
                {
                    continue;
                }
                // The prepass's raw last-use — never loop-extended — is
                // exactly the question a split needs answered: does the
                // value have a real occurrence past the region, obliging
                // an exit store, as opposed to an interval end that is
                // only loop-extension math.
                bool lives_past = prepass.last_use[candidate.virtual_register] > region_end;
                if (lives_past && !region_exit_ok[best_region])
                {
                    continue;
                }
                bool region_over_budget = false;
                for (u32 instruction_index = region_start; instruction_index <= region_end && !region_over_budget; instruction_index += 1)
                {
                    region_over_budget = pin_depths[instruction_index] >= pin_budgets[instruction_index];
                }
                if (region_over_budget)
                {
                    continue;
                }
                u32 entry_count = region_entry_counts[best_region];
                u32 exit_count = lives_past ? region_exit_counts[best_region] : 0;
                if (split_entry_count == MACHINE_QUALITY_SPLIT_ENTRY_LIMIT || split_store_count + exit_count > MACHINE_QUALITY_SPLIT_STORE_LIMIT)
                {
                    break;
                }
                u32 entry_weight = 0;
                for (u32 entry_index = 0; entry_index < entry_count; entry_index += 1)
                {
                    entry_weight += instruction_weights[region_entry_rows[region_entry_offsets[best_region] + entry_index]];
                }
                u32 exit_weight = 0;
                for (u32 successor_index = 0; successor_index < exit_count; successor_index += 1)
                {
                    u32 store_row = function->blocks[region_exit_blocks[region_exit_offsets[best_region] + successor_index]].first_instruction;
                    exit_weight += instruction_weights[store_row];
                }
                for (u32 file_index = candidate.marginal ? quality_pin_count : 0; file_index < attempt_file_count; file_index += 1)
                {
                    u32 pin_register = attempt_file[file_index];
                    if (assigned_counts[file_index] == BUSTER_ARRAY_LENGTH(assigned_starts[0]) ||
                        excluded_row_counts[file_index] + entry_count + exit_count > MACHINE_QUALITY_EXCLUDED_ROW_LIMIT)
                    {
                        continue;
                    }
                    u32* prefix = machine_quality_foreclosure_prefix_ensure(foreclosure_prefix, &prefix_built_mask, foreclosed_masks,
                                                                            function->instruction_count, pin_register);
                    if (prefix[region_end + 1] != prefix[region_start])
                    {
                        continue;
                    }
                    bool conflicts = false;
                    for (u32 span = 0; span < assigned_counts[file_index]; span += 1)
                    {
                        conflicts |= region_start <= assigned_ends[file_index][span] && assigned_starts[file_index][span] <= region_end;
                        // The boundary rows write the register outside this
                        // span, so no other span on it may cover them.
                        for (u32 entry_index = 0; entry_index < entry_count; entry_index += 1)
                        {
                            u32 entry_row = region_entry_rows[region_entry_offsets[best_region] + entry_index];
                            conflicts |= assigned_starts[file_index][span] <= entry_row && entry_row <= assigned_ends[file_index][span];
                        }
                        for (u32 successor_index = 0; successor_index < exit_count; successor_index += 1)
                        {
                            u32 store_row =
                                function->blocks[region_exit_blocks[region_exit_offsets[best_region] + successor_index]].first_instruction;
                            conflicts |= assigned_starts[file_index][span] <= store_row && store_row <= assigned_ends[file_index][span];
                        }
                    }
                    for (u32 row_index = 0; row_index < excluded_row_counts[file_index]; row_index += 1)
                    {
                        conflicts |= region_start <= excluded_rows[file_index][row_index] && excluded_rows[file_index][row_index] <= region_end;
                    }
                    if (conflicts)
                    {
                        continue;
                    }
                    u32 prologue_cost = ((description->callee_saved_mask >> pin_register) & 1u) && !((baseline.callee_saved_mask >> pin_register) & 1u)
                                            ? 2u
                                            : 0u;
                    if (best_traffic <= entry_weight + exit_weight + prologue_cost)
                    {
                        continue;
                    }
                    assigned_starts[file_index][assigned_counts[file_index]] = region_start;
                    assigned_ends[file_index][assigned_counts[file_index]] = region_end;
                    assigned_counts[file_index] += 1;
                    pinned_registers[candidate.virtual_register] = pin_register;
                    pinned_mask |= 1ull << pin_register;
                    span_starts[candidate.virtual_register] = region_start;
                    span_ends[candidate.virtual_register] = region_end;
                    for (u32 instruction_index = region_start; instruction_index <= region_end; instruction_index += 1)
                    {
                        pin_depths[instruction_index] += 1;
                    }
                    split_entries[split_entry_count] = (MachinePinSplitEntry){
                        .block = region_head_blocks[best_region],
                        .virtual_register = candidate.virtual_register,
                    };
                    split_entry_count += 1;
                    for (u32 entry_index = 0; entry_index < entry_count; entry_index += 1)
                    {
                        excluded_rows[file_index][excluded_row_counts[file_index]] = region_entry_rows[region_entry_offsets[best_region] + entry_index];
                        excluded_row_counts[file_index] += 1;
                    }
                    for (u32 successor_index = 0; successor_index < exit_count; successor_index += 1)
                    {
                        u32 store_row = function->blocks[region_exit_blocks[region_exit_offsets[best_region] + successor_index]].first_instruction;
                        excluded_rows[file_index][excluded_row_counts[file_index]] = store_row;
                        excluded_row_counts[file_index] += 1;
                        split_stores[split_store_count] = (MachinePinSplitStore){
                            .row = store_row,
                            .virtual_register = candidate.virtual_register,
                        };
                        split_store_count += 1;
                    }
                    assigned = true;
                    break;
                }
            }
        }
        if (!pinned_mask)
        {
            break;
        }
        // A first attempt that never assigned a caller-saved register
        // would degrade into an identical repack, so its rejection is
        // final.
        bool degradable = (pinned_mask & caller_saved_allocatable) != 0;
        // Span scoping. One register mask per instruction paints where
        // each pin actually holds; the local scan owns the register
        // everywhere outside. The entry masks mark each span's first
        // instruction, the one point where a spill naming the register is
        // legitimate — the eviction that hands it over.
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
            u64 pin_bit = 1ull << pinned_registers[register_index];
            pin_entry_masks[span_starts[register_index]] |= pin_bit;
            for (u32 instruction_index = span_starts[register_index]; instruction_index <= span_ends[register_index]; instruction_index += 1)
            {
                pin_active_masks[instruction_index] |= pin_bit;
            }
        }
        // The scan drains the stores through one row cursor.
        for (u32 store_index = 1; store_index < split_store_count; store_index += 1)
        {
            MachinePinSplitStore moved = split_stores[store_index];
            u32 store_slot = store_index;
            while (store_slot && split_stores[store_slot - 1].row > moved.row)
            {
                split_stores[store_slot] = split_stores[store_slot - 1];
                store_slot -= 1;
            }
            split_stores[store_slot] = moved;
        }
        MachineStackPlacement placement = machine_fast_placement_build_prepassed(arena, function, &prepass, pinned_registers, pinned_mask,
                                                                                  pin_active_masks, span_starts, span_ends, split_entries,
                                                                                  split_entry_count, split_stores, split_store_count);
        // Accept only on modeled improvement, in the same weighted units
        // the candidacy used: both placements' spill and reload edits are
        // priced by the frequency class of the block they land in, so a
        // placement that moves traffic out of a loop and into the entry
        // path wins even at an equal count, and one that does the reverse
        // is rejected — which is exactly what a split's landing-pad
        // stores and entry installs need, priced at their own row's
        // frequency with no special-casing. The prologue push/pop pair
        // each newly reserved callee-saved register costs executes once
        // per call and stays at weight one — a caller-saved pin costs no
        // prologue — and a pin also raises local pressure, which can add
        // traffic elsewhere.
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
        u64 placement_traffic_total = 0;
        for (u32 edit_index = 0; placement.valid && edit_index < placement.edit_count; edit_index += 1)
        {
            MachineEdit* edit = placement.edits + edit_index;
            if (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD)
            {
                placement_traffic_total += instruction_weights[machine_point_instruction(edit->point)];
            }
        }
        if (!placement.valid || placement_traffic_total + added_prologue_cost >= baseline_traffic_total)
        {
            if (attempt == 0 && split_entry_count)
            {
                // Retrying without splits is a genuinely different pack;
                // the normal attempt increment explores it.
                continue;
            }
            // No splits fired this round, so a plain retry at attempt 1
            // would repeat attempt 0 exactly — jump straight past it to
            // the callee-saved-only attempt when that can still help.
            if (degradable)
            {
                attempt = 1;
                continue;
            }
            break;
        }
        // Pin verification. Within its span a pinned register belongs to
        // its value: no other value's operand may sit in it there, no
        // edit may name it as a target — except the spill that vacates it
        // where the span opens — and no copy may read it as a source. A
        // split's boundary edits need no exception: its installs and
        // landing-pad stores all sit at rows the excluded-row bookkeeping
        // keeps outside every span of the register they touch. The local
        // scan has several paths that bypass its own register pool — copy
        // coalescing was one — and a wrong pin is a silent miscompile, so
        // the placement is checked rather than trusted, and a violation
        // degrades like a rejection.
        bool pins_hold = true;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count && pins_hold; instruction_index += 1)
        {
            u64 active = pin_active_masks[instruction_index];
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
                pins_hold &=
                    machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER && pinned_registers[machine_ref_payload(ref)] == operand_registers[slot];
            }
        }
        for (u32 edit_index = 0; edit_index < placement.edit_count && pins_hold; edit_index += 1)
        {
            MachineEdit* edit = placement.edits + edit_index;
            u32 edit_instruction = machine_point_instruction(edit->point);
            u64 active = pin_active_masks[edit_instruction];
            bool vacating = edit->kind == MACHINE_EDIT_SPILL && ((pin_entry_masks[edit_instruction] >> edit->location) & 1u);
            pins_hold &= !((active >> edit->location) & 1u) || vacating;
            pins_hold &= edit->kind != MACHINE_EDIT_COPY || !((active >> edit->subject) & 1u);
        }
        if (!pins_hold)
        {
            if (attempt == 0 && split_entry_count)
            {
                continue;
            }
            if (degradable)
            {
                attempt = 1;
                continue;
            }
            break;
        }
        placement.pinned_register_count = 0;
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            placement.pinned_register_count += pinned_registers[register_index] != UINT32_MAX;
        }
        placement.split_register_count = split_entry_count;
        scratch_end(scratch);
        return placement;
    }
    scratch_end(scratch);
    return baseline;
}
