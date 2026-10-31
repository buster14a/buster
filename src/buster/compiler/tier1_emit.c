// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A bounded Tier 1 machine consumer: selected straight-line IR -> liveness -> required locations -> allocation ->
// frame -> native records. Include after link.c, tier1_select.c, tier1_allocate.c, tier1_rematerialize.c, tier1_cfg.c,
// tier1_liveness.c, tier1_frame.c and tier1_frame_emit.c. The existing uniform-register selector remains a separate fail-closed API.
//
// Every selected input and definition requires a register at its machine phase. At an input phase, all evicted
// register values are stored before any reload reuses those registers. Definition-phase evictions are stored before
// the instruction writes its destination; source registers were captured at the preceding input phase, so the
// selector's destructive-operand repairs still apply. Integer constants may be recomputed after eviction, a zero by a
// zeroing xor: no flag is live between two nodes' records (BUSTER_TIER1_EMIT_BOUNDARY_FLAGS). Initial definitions are
// still emitted. Direct scalar calls publish symbolic CALL
// references, save live caller registers in fixed frame rows and resolve argument moves with reserved R11.
//
// This consumer accepts a straight-line chain ending in RET, UNREACHABLE or an empty self-loop sink; every earlier
// block must branch to the immediately following block. The sink has no values to reconcile and retains its branch.
// Every other function, general control flow and phis, goes to tier1_emit_cfg.c, which allocates over the CFG and
// moves values along its edges; numeric fragment order is not a substitute.
// Indirect/variadic/wide-ABI calls, dynamic/variadic frames and direct register-to-register
// fragment transitions are explicit unsupported cases. R11 remains reserved for selection and frame probes.
//
// Borrowed inputs are immutable and may precede allocations in the output arena, but must not overlap either arena's
// unused capacity. The arenas are disjoint. Failure restores both lengths and preserves the result. All retained
// columns have their exact logical length, without required padding. The existing encoder owns its output padding.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUSTER_TIER1_EMIT_GENERAL_REGISTERS 16
#define BUSTER_TIER1_EMIT_CALL_RECORDS (2 * BUSTER_TIER1_EMIT_GENERAL_REGISTERS + 3 * BUSTER_ABI_INTEGER_ARGUMENTS + 6)

typedef enum Tier1EmitError
{
    TIER1_EMIT_OK,
    TIER1_EMIT_INVALID,
    TIER1_EMIT_UNSUPPORTED,
    TIER1_EMIT_MEMORY,
    TIER1_EMIT_ALLOCATION,
    TIER1_EMIT_FRAME,
    TIER1_EMIT_TARGET,
    TIER1_EMIT_LIMIT,
} Tier1EmitError;

// The access column's values the consumers take (Tier0Function.symbol_access's BUSTER_TIER0_ACCESS_DIRECT and _GOT, which
// c_check_tier1.c checks are these): a symbol's address directly, or from its GOT slot.
#define BUSTER_TIER1_ACCESS_DIRECT 0
#define BUSTER_TIER1_ACCESS_GOT 1

typedef struct Tier1EmitOptions Tier1EmitOptions;
struct Tier1EmitOptions
{
    uint32_t register_mask;
    uint32_t frame_flags;
    bool vector;
    bool rematerialize;
    // How each symbol below `symbol_count` is reached (Tier0Function.symbol_access's BUSTER_TIER0_ACCESS_*: directly,
    // through its GOT slot, or a thread-local's, which the consumers refuse), for its address (IR_SYMBOL_ADDR). Null
    // reaches every symbol directly.
    const uint8_t* symbol_access;
    uint32_t symbol_count;
};

typedef struct Tier1EmitPrepared Tier1EmitPrepared;
struct Tier1EmitPrepared
{
    Tier1AllocationInput allocation;
    Tier1RematerializationInput recipes;
    // The recipes' values, which the CFG consumer completes after the frame's layout: a frame object's address's
    // displacement (tier1_emit_cfg_frame).
    uint64_t* recipe_values;
    uint32_t* value_intervals;
    uint32_t* starts;
    uint32_t* ends;
    uint32_t* sizes;
    uint32_t* alignments;
    uint32_t reserved_registers;
    uint32_t call_count;
    uint32_t call_save_mask;
    uint32_t slot_columns;
    AbiCall call_abi;
};

typedef struct Tier1EmitResult Tier1EmitResult;
struct Tier1EmitResult
{
    Tier1SelectCode code;
    Tier1Liveness liveness;
    Tier1EmitPrepared prepared;
    Tier1Allocation allocation;
    Tier1Frame frame;
    uint32_t spill_stores;
    uint32_t spill_loads;
    uint32_t rematerializations;
    uint32_t transition_count;
    LinkReference* references;
    uint32_t reference_count;
    // The references' room: a call's each, and in the CFG consumer a symbol's address's each too, with its options'
    // symbols' access (Tier1EmitOptions.symbol_access); and its divisions' and shifts' by a register, whose sequences
    // take records past a node's share.
    uint32_t reference_capacity;
    uint32_t fixed_count;
    // In the CFG consumer, the callee-saved registers its values may take (live across a call), and those they took,
    // which the frame saves; and how many values live across a call it gave them.
    uint32_t callee_registers;
    uint32_t callee_used;
    uint32_t crossing_values;
    const uint8_t* symbol_access;
    uint32_t symbol_access_count;
    uint32_t call_stores;
    uint32_t call_loads;
    uint64_t retained_bytes;
    uint64_t transition_scratch_bytes;
    // A function the straight-line consumer doesn't take goes through tier1_emit_cfg.c, which leaves `liveness` and
    // `prepared.allocation` empty: its CFG's allocation problem (values numbered densely, value_nodes[v] their nodes,
    // node_values the inverse, BUSTER_TIER1_CFG_NONE for a node without one), tier1_cfg's result, whose allocation
    // `allocation` is, and what its edges took: move records, stubs of split edges and jumps between blocks.
    bool general;
    Tier1CfgInput cfg_input;
    Tier1CfgAllocation cfg;
    uint32_t* value_nodes;
    uint32_t* node_values;
    uint32_t edge_moves;
    uint32_t edge_stubs;
    // Recomputed values' definitions that nothing read, which the CFG consumer leaves out (tier1_emit_cfg_dead_definition).
    uint32_t dropped_definitions;
    uint32_t jumps;
};

typedef struct Tier1EmitTransitions Tier1EmitTransitions;
struct Tier1EmitTransitions
{
    uint32_t* offsets;
    uint32_t* fragments;
    uint32_t* values;
    uint8_t* registers;
    uint32_t owners[BUSTER_TIER1_EMIT_GENERAL_REGISTERS];
};

static Tier1EmitError tier1_emit_prepare(const Tier1SelectPlan* plan, const Tier1Liveness* liveness, const Tier1EmitOptions* options, Arena* output, Tier1EmitPrepared* result)
{
    const Tier1AllocationInput* source = &liveness->allocation;
    const uint32_t count = plan->function->instruction_count;
    const uint64_t uses = (uint64_t)source->use_count + source->count;
    Tier1EmitError error = uses <= BUSTER_TIER1_ALLOCATE_MAX_USES ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1EmitPrepared prepared = { .allocation = *source };
    for (uint32_t node = 0; node < count; node += 1)
    {
        if (plan->function->op[node] == IR_CALL)
        {
            prepared.call_count += 1;
            const uint32_t arguments = plan->function->calls[plan->function->b[node]].argument_count;
            prepared.call_abi.integer_registers = prepared.call_abi.integer_registers > arguments ? prepared.call_abi.integer_registers : arguments;
        }
    }
    prepared.call_abi.stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    prepared.call_save_mask = prepared.call_count != 0 ? options->register_mask : 0;
    prepared.slot_columns = count + (uint32_t)__builtin_popcount(prepared.call_save_mask);
    uint32_t* allowed = 0;
    uint32_t* offsets = 0;
    uint32_t* positions = 0;
    uint8_t* required = 0;
    uint8_t* interval_remat = 0;
    uint8_t* operations = 0;
    uint8_t* widths = 0;
    uint8_t* flags = 0;
    uint8_t* marks = 0;
    uint64_t* values = 0;
    uint16_t* costs = 0;
    uint16_t* reload_costs = 0;
    if (error == TIER1_EMIT_OK)
    {
        bool ok = true;
        prepared.value_intervals = ir_allocate(output, count, 4, &ok);
        prepared.starts = ir_allocate(output, prepared.slot_columns, 4, &ok);
        prepared.ends = ir_allocate(output, prepared.slot_columns, 4, &ok);
        prepared.sizes = ir_allocate(output, prepared.slot_columns, 4, &ok);
        prepared.alignments = ir_allocate(output, prepared.slot_columns, 4, &ok);
        allowed = ir_allocate(output, source->count, 4, &ok);
        offsets = ir_allocate(output, (uint64_t)source->count + 1, 4, &ok);
        positions = ir_allocate(output, uses, 4, &ok);
        required = ir_allocate(output, uses, 1, &ok);
        interval_remat = ir_allocate(output, source->count, 1, &ok);
        operations = ir_allocate(output, count, 1, &ok);
        widths = ir_allocate(output, count, 1, &ok);
        flags = ir_allocate(output, count, 1, &ok);
        marks = ir_allocate(output, count, 1, &ok);
        values = ir_allocate(output, count, 8, &ok);
        costs = ir_allocate(output, count, 2, &ok);
        reload_costs = ir_allocate(output, count, 2, &ok);
        if (!ok)
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t row = count; row < prepared.slot_columns; row += 1)
        {
            prepared.starts[row] = 0;
            prepared.ends[row] = source->position_count;
            prepared.sizes[row] = 8;
            prepared.alignments[row] = 8;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            prepared.value_intervals[node] = BUSTER_IR_NONE;
            widths[node] = (uint8_t)tier1_select_width(plan->function->type[node]);
            if (options->rematerialize && !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_CONSTANT)
            {
                operations[node] = BUSTER_TIER1_REMAT_INTEGER;
                values[node] = plan->patterns[node].immediate;
            }
        }
        prepared.recipes = (Tier1RematerializationInput){
            .operations = operations,
            .widths = widths,
            .flags = flags,
            .values = values,
            .count = count,
        };
        prepared.recipe_values = values;
        // The model's costs, where it has them, before the marks read them (tier1_rematerialization_model_costs).
        const bool costed = tier1_rematerialization_model_costs(&prepared.recipes, X86_REGISTER_RSP, costs, reload_costs, options->vector);
        prepared.recipes.costs = costs;
        prepared.recipes.reload_costs = reload_costs;
        const bool marked = costed && (BUSTER_VECTOR_PATH(options->vector) ? tier1_rematerialization_marks(&prepared.recipes, marks) : tier1_rematerialization_marks_scalar(&prepared.recipes, marks));
        if (!marked)
        {
            error = TIER1_EMIT_INVALID;
        }
        uint32_t use = 0;
        for (uint32_t interval = 0; interval < source->count; interval += 1)
        {
            const uint32_t value = source->values[interval];
            prepared.value_intervals[value] = interval;
            prepared.starts[value] = source->starts[interval];
            prepared.ends[value] = source->ends[interval];
            prepared.sizes[value] = widths[value];
            prepared.alignments[value] = widths[value];
            interval_remat[interval] = marks[value];
            offsets[interval] = use;
            positions[use] = source->starts[interval];
            required[use++] = 1;
            for (uint32_t u = source->use_offsets[interval]; u < source->use_offsets[interval + 1]; u += 1)
            {
                positions[use] = source->use_positions[u];
                required[use++] = 1;
            }
        }
        offsets[source->count] = use;
        // The prospective spill set contains every selected definition. Its alignment determines frame-base
        // reservations before allocation; reducing it to actual spills later cannot add a reservation.
        const Tier1FrameRequest prospective = {
            .slots = {
                .start = prepared.starts,
                .end = prepared.ends,
                .size = prepared.sizes,
                .alignment = prepared.alignments,
                .indices = source->values,
                .count = source->count,
                .column_count = prepared.slot_columns,
            },
            .calls = &prepared.call_abi,
            .call_count = prepared.call_count != 0,
            .flags = options->frame_flags,
        };
        Tier1FrameRequirements requirements;
        if (tier1_frame_requirements(&prospective, &requirements) != TIER1_FRAME_OK)
        {
            error = TIER1_EMIT_FRAME;
        }
        else
        {
            prepared.reserved_registers = requirements.reserved_registers | (1u << BUSTER_TIER1_SELECT_SCRATCH);
            prepared.allocation.registers[0] = options->register_mask & ~prepared.reserved_registers;
            for (uint32_t interval = 0; interval < source->count; interval += 1)
            {
                allowed[interval] = source->allowed_registers[interval] & prepared.allocation.registers[0];
            }
            prepared.allocation.allowed_registers = allowed;
            prepared.allocation.use_offsets = offsets;
            prepared.allocation.use_positions = positions;
            prepared.allocation.use_required = required;
            prepared.allocation.use_count = use;
            prepared.allocation.rematerializable = interval_remat;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = prepared;
    }
    return error;
}

static Tier1EmitError tier1_emit_transitions(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, Arena* scratch, Tier1EmitTransitions* result)
{
    const Tier1AllocationInput* input = &prepared->allocation;
    Tier1EmitError error = TIER1_EMIT_OK;
    Tier1EmitTransitions transitions = {0};
    bool ok = true;
    transitions.offsets = ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok);
    transitions.fragments = ir_allocate(scratch, allocation->fragment_count, 4, &ok);
    transitions.values = ir_allocate(scratch, allocation->fragment_count, 4, &ok);
    transitions.registers = ir_allocate(scratch, input->value_count, 1, &ok);
    uint32_t* cursors = ir_allocate(scratch, input->position_count, 4, &ok);
    if (!ok)
    {
        error = TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            transitions.owners[reg] = BUSTER_IR_NONE;
        }
        if (input->value_count != 0)
        {
            memset(transitions.registers, UINT8_MAX, input->value_count);
        }
    }
    for (uint32_t interval = 0; error == TIER1_EMIT_OK && interval < input->count; interval += 1)
    {
        const uint32_t first = allocation->fragment_offsets[interval];
        const uint32_t end = allocation->fragment_offsets[interval + 1];
        const uint32_t value = input->values[interval];
        if (first >= end || end > allocation->fragment_count || allocation->kinds[first] != BUSTER_TIER1_ALLOCATE_REGISTER)
        {
            error = TIER1_EMIT_ALLOCATION;
        }
        else
        {
            transitions.registers[value] = allocation->registers[first];
        }
        uint32_t position = input->starts[interval];
        for (uint32_t f = first; error == TIER1_EMIT_OK && f < end; f += 1)
        {
            const uint32_t kind = allocation->kinds[f];
            if (allocation->starts[f] != position || allocation->ends[f] <= position || allocation->ends[f] > input->ends[interval] || kind > BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (kind == BUSTER_TIER1_ALLOCATE_REGISTER && !tier1_select_register_allowed(allocation->registers[f]))
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (f != first && kind == BUSTER_TIER1_ALLOCATE_REGISTER && (allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REGISTER || (position & 1) != 0))
            {
                // A definition-phase reload could overwrite an already captured source. Required definitions and
                // even input phases produced above do not create such a transition.
                error = TIER1_EMIT_UNSUPPORTED;
            }
            else
            {
                transitions.values[f] = value;
                if (f != first)
                {
                    transitions.offsets[position + 1] += 1;
                }
                position = allocation->ends[f];
            }
        }
        if (error == TIER1_EMIT_OK && position != input->ends[interval])
        {
            error = TIER1_EMIT_ALLOCATION;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t position = 0; position < input->position_count; position += 1)
        {
            transitions.offsets[position + 1] += transitions.offsets[position];
            cursors[position] = transitions.offsets[position];
        }
        for (uint32_t interval = 0; interval < input->count; interval += 1)
        {
            for (uint32_t f = allocation->fragment_offsets[interval] + 1; f < allocation->fragment_offsets[interval + 1]; f += 1)
            {
                const uint32_t position = allocation->starts[f];
                transitions.fragments[cursors[position]++] = f;
            }
        }
        *result = transitions;
    }
    return error;
}

static bool tier1_emit_append(Tier1EmitBuffer* buffer, X86Instruction instruction)
{
    const bool room = buffer->count < buffer->capacity;
    if (room)
    {
        buffer->records[buffer->count++] = instruction;
    }
    return room;
}

// The flags live between two nodes' records, where fragment transitions and edge moves run: none. Selection keeps each
// flag's producer and its readers in one node's records (a compare and its setcc, conditional jump or cmov), as its
// constants' immediate requests already assume (tier1_select_request's live_flags 0), so that a recomputed zero is a
// zeroing xor there rather than a mov (tier1_rematerialize).
#define BUSTER_TIER1_EMIT_BOUNDARY_FLAGS 0

// A recomputed value's copy (tier1_rematerialize's) at the end of `buffer`, with its reference where it is a symbol's: a
// PC32 for its lea from rip, a LOAD_GOT for its GOT slot's load, each copy one more for link_add_code_fixups (#98).
// TIER1_EMIT_ALLOCATION where the copy can't be made or has no room for its reference, which the consumers count before
// they emit.
static Tier1EmitError tier1_emit_copy(Tier1EmitResult* result, const Tier1RematerializationInput* recipes, uint32_t index, uint8_t destination, uint8_t frame_base, Tier1EmitBuffer* buffer)
{
    Tier1Rematerialized copy = { 0 };
    const bool made = tier1_rematerialize(recipes, index, destination, frame_base, BUSTER_TIER1_EMIT_BOUNDARY_FLAGS, &copy);
    const bool symbolic = made && copy.symbol != BUSTER_TIER1_REMAT_NO_SYMBOL;
    Tier1EmitError error = made && (!symbolic || result->reference_count < result->reference_capacity) ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    if (error == TIER1_EMIT_OK && symbolic)
    {
        result->references[result->reference_count++] = (LinkReference){
            .instruction = buffer->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = copy.got ? BUSTER_LINK_FIXUP_LOAD_GOT : BUSTER_LINK_FIXUP_PC32,
            .target = copy.symbol,
            .addend = copy.addend,
        };
    }
    error = error != TIER1_EMIT_OK || tier1_emit_append(buffer, copy.instruction) ? error : TIER1_EMIT_MEMORY;
    result->rematerializations += error == TIER1_EMIT_OK;
    return error;
}

static X86Instruction tier1_emit_spill(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, const Tier1Frame* frame, uint32_t value, uint8_t reg, bool load)
{
    const uint32_t width = prepared->sizes[value];
    const X86Instruction record = {
        .displacement = frame->offset[allocation->spill_ids[value]],
        .reg = reg,
        .rm = (uint8_t)frame->base_register,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = load ? (width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32) : (width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32),
    };
    return record;
}

static Tier1EmitError tier1_emit_phase(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, const Tier1Frame* frame, Tier1EmitTransitions* transitions, uint32_t position, Tier1EmitBuffer* buffer, Tier1EmitResult* result)
{
    Tier1EmitError error = TIER1_EMIT_OK;
    const uint32_t first = transitions->offsets[position];
    const uint32_t end = transitions->offsets[position + 1];
    // Stores are a separate pass: a reload may use any register whose old value is evicted at this position.
    for (uint32_t e = first; error == TIER1_EMIT_OK && e < end; e += 1)
    {
        const uint32_t f = transitions->fragments[e];
        const uint32_t value = transitions->values[f];
        if (allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL)
        {
            if (allocation->spill_ids[value] >= frame->count)
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (!tier1_emit_append(buffer, tier1_emit_spill(prepared, allocation, frame, value, allocation->registers[f - 1], false)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            else
            {
                result->spill_stores += 1;
            }
        }
    }
    for (uint32_t e = first; error == TIER1_EMIT_OK && e < end; e += 1)
    {
        const uint32_t f = transitions->fragments[e];
        const uint32_t value = transitions->values[f];
        const uint8_t kind = allocation->kinds[f];
        const uint8_t previous = allocation->kinds[f - 1];
        const uint8_t reg = allocation->registers[f];
        const uint8_t old_register = allocation->registers[f - 1];
        if (previous == BUSTER_TIER1_ALLOCATE_REGISTER && transitions->owners[old_register] == value)
        {
            transitions->owners[old_register] = BUSTER_IR_NONE;
        }
        if (kind == BUSTER_TIER1_ALLOCATE_REGISTER && previous == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
        {
            // The value's definition again, rather than a reload: it has no slot.
            error = tier1_emit_copy(result, &prepared->recipes, value, reg, (uint8_t)frame->base_register, buffer);
            transitions->owners[reg] = value;
        }
        else if (kind == BUSTER_TIER1_ALLOCATE_REGISTER)
        {
            const bool reload = previous == BUSTER_TIER1_ALLOCATE_SPILL && allocation->spill_ids[value] < frame->count;
            error = reload ? error : TIER1_EMIT_ALLOCATION;
            if (error == TIER1_EMIT_OK && !tier1_emit_append(buffer, tier1_emit_spill(prepared, allocation, frame, value, reg, true)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            result->spill_loads += error == TIER1_EMIT_OK;
            transitions->owners[reg] = value;
        }
        transitions->registers[value] = kind == BUSTER_TIER1_ALLOCATE_REGISTER ? reg : UINT8_MAX;
        result->transition_count += 1;
    }
    return error;
}

static X86Instruction tier1_emit_call_save(const Tier1EmitResult* result, uint32_t reg, bool load)
{
    const uint32_t preceding = result->prepared.call_save_mask & ((1u << reg) - 1);
    const uint32_t row = result->allocation.spill_count + (uint32_t)__builtin_popcount(preceding);
    const X86Instruction record = {
        .displacement = result->frame.offset[row],
        .reg = (uint8_t)reg,
        .rm = (uint8_t)result->frame.base_register,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = load ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_RM64_R64,
    };
    return record;
}

static X86Instruction tier1_emit_move(uint32_t source, uint32_t destination, uint32_t width)
{
    const X86Instruction record = {
        .reg = (uint8_t)destination,
        .rm = (uint8_t)source,
        .form = width == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64,
    };
    return record;
}

// The allocator has already split ordinary spills. Call preservation is local to a bounded machine-register set:
// a surviving register is saved before argument shuffles and restored after capturing the return value. Keeping
// this separate from fixed interval constraints avoids banning caller registers over an entire live interval.
// The registers a call at `node` saves: those holding values that live past it, here a value's interval end.
static uint32_t tier1_emit_call_saved(const Tier1EmitResult* result, const Tier1EmitTransitions* transitions, uint32_t node)
{
    const uint32_t position = node * 2 + 1;
    uint32_t saved = 0;
    for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        const uint32_t owner = transitions->owners[reg];
        if (owner != BUSTER_IR_NONE && owner != node && result->prepared.ends[owner] > position && transitions->registers[owner] == reg)
        {
            saved |= 1u << reg;
        }
    }
    return saved;
}

static Tier1EmitError tier1_emit_call(const Tier1SelectPlan* plan, uint32_t node, const uint32_t* argument_registers, uint32_t saved, uint32_t destination, Tier1EmitBuffer* buffer, Arena* scratch, Tier1EmitResult* result)
{
    const IrFunction* function = plan->function;
    const IrCall* call = &function->calls[function->b[node]];
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitError error = TIER1_EMIT_OK;
    if ((saved & ~result->prepared.call_save_mask) != 0 || (function->type[node] != IR_VOID && ((saved >> destination) & 1)))
    {
        error = TIER1_EMIT_ALLOCATION;
    }
    // The arguments into their registers, and an indirect call's callee, its effective use after them, into R10, which no
    // argument takes: one parallel move, so that no source is overwritten before it is read.
    const bool indirect = function->op[node] == IR_CALL_INDIRECT;
    uint32_t sources[BUSTER_ABI_INTEGER_ARGUMENTS + 1];
    uint32_t destinations[BUSTER_ABI_INTEGER_ARGUMENTS + 1];
    for (uint32_t argument = 0; argument < call->argument_count; argument += 1)
    {
        sources[argument] = argument_registers[argument];
        destinations[argument] = tier1_select_parameter_registers[argument];
    }
    if (indirect)
    {
        sources[call->argument_count] = argument_registers[call->argument_count];
        destinations[call->argument_count] = X86_REGISTER_R10;
    }
    const Tier1ParallelMoves moves = {
        .sources = sources,
        .destinations = destinations,
        .count = call->argument_count + indirect,
        .scratch = BUSTER_TIER1_SELECT_SCRATCH,
    };
    Tier1ResolvedMoves resolved = {0};
    if (error == TIER1_EMIT_OK)
    {
        const Tier1AllocationError moved = tier1_resolve_moves(&moves, scratch, &resolved);
        error = moved == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    for (uint32_t reg = 0; error == TIER1_EMIT_OK && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        if ((saved >> reg) & 1)
        {
            if (!tier1_emit_append(buffer, tier1_emit_call_save(result, reg, false)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            result->call_stores += 1;
        }
    }
    for (uint32_t move = 0; error == TIER1_EMIT_OK && move < resolved.count; move += 1)
    {
        if (!tier1_emit_append(buffer, tier1_emit_move(resolved.sources[move], resolved.destinations[move], 8)))
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    // Resolve mixed-width cycles using full registers first. Narrowing a source early could corrupt a distinct
    // 64-bit argument that shares it; only the completed 32-bit destinations are then zero-extended.
    for (uint32_t argument = 0; error == TIER1_EMIT_OK && argument < call->argument_count; argument += 1)
    {
        if (function->call_types[call->type_first + argument] == IR_I32 && !tier1_emit_append(buffer, tier1_emit_move(destinations[argument], destinations[argument], 4)))
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    // A variadic call's al: how many vector registers its arguments take, none, since the selector passes integers alone.
    if (error == TIER1_EMIT_OK && call->variadic && !tier1_emit_append(buffer, (X86Instruction){ .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }))
    {
        error = TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK && indirect)
    {
        error = tier1_emit_append(buffer, (X86Instruction){ .form = X86_FORM_CALL_RM64, .rm = X86_REGISTER_R10 }) ? error : TIER1_EMIT_MEMORY;
    }
    else if (error == TIER1_EMIT_OK)
    {
        const LinkReference reference = {
            .instruction = buffer->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = function->a[node],
            .addend = 0,
        };
        const X86Instruction instruction = { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP };
        if (result->reference_count >= result->reference_capacity || !tier1_emit_append(buffer, instruction))
        {
            error = TIER1_EMIT_MEMORY;
        }
        else
        {
            result->references[result->reference_count++] = reference;
        }
    }
    // The result into its register, a narrow one zero-extended into its canonical 32-bit storage: the callee leaves the
    // bits past its width unspecified, as clang's code does.
    const uint32_t result_type = function->type[node];
    const uint32_t result_width = result_type != IR_VOID ? tier1_select_width(result_type) : 0;
    const uint32_t narrow = result_type == IR_I1 || result_type == IR_I8 ? 1 : result_type == IR_I16 ? 2 : 0;
    if (error == TIER1_EMIT_OK && narrow != 0)
    {
        const X86Instruction extend = { .form = narrow == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, .reg = (uint8_t)destination, .rm = X86_REGISTER_RAX };
        error = tier1_emit_append(buffer, extend) ? error : TIER1_EMIT_MEMORY;
    }
    else if (error == TIER1_EMIT_OK && result_width != 0 && (destination != X86_REGISTER_RAX || function->type[node] == IR_I32))
    {
        if (!tier1_emit_append(buffer, tier1_emit_move(X86_REGISTER_RAX, destination, result_width)))
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    for (uint32_t reg = 0; error == TIER1_EMIT_OK && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        if ((saved >> reg) & 1)
        {
            if (!tier1_emit_append(buffer, tier1_emit_call_save(result, reg, true)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            result->call_loads += 1;
        }
    }
    arena_pop_to(scratch, scratch_mark);
    return error;
}

static Tier1EmitError tier1_emit_records(const Tier1SelectPlan* plan, const uint8_t* allowed_forms, bool vector, Arena* output, Arena* scratch, Tier1EmitResult* result, uint32_t* error_node)
{
    const uint32_t count = plan->function->instruction_count;
    const uint64_t capacity = (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE + (uint64_t)result->allocation.fragment_count * 2 + BUSTER_TIER1_FRAME_EMIT_RECORDS * 2 + (uint64_t)result->prepared.call_count * BUSTER_TIER1_EMIT_CALL_RECORDS;
    const bool terminal_sink = count != 0 && plan->function->op[count - 1] == IR_BR;
    const uint32_t branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES * 2 + terminal_sink;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitError error = capacity <= UINT32_MAX ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1SelectCode* code = &result->code;
    Tier1EmitBuffer buffer = { .capacity = (uint32_t)capacity, .branch_capacity = branch_capacity };
    Tier1EmitTransitions transitions = {0};
    Tier1SelectRequests requests = { .vector = vector };
    if (error == TIER1_EMIT_OK)
    {
        bool ok = true;
        code->records = ir_allocate(output, capacity, sizeof(X86Instruction), &ok);
        code->cost_ids = ir_allocate(output, capacity, 1, &ok);
        code->node_first = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        code->branches = ir_allocate(output, branch_capacity, 4, &ok);
        code->targets = ir_allocate(output, branch_capacity, 4, &ok);
        if (result->prepared.call_count != 0)
        {
            result->references = ir_allocate(output, result->prepared.call_count, sizeof(LinkReference), &ok);
            result->reference_capacity = result->prepared.call_count;
        }
        if (!ok)
        {
            error = TIER1_EMIT_MEMORY;
        }
        else
        {
            buffer.records = code->records;
            buffer.branches = code->branches;
            buffer.targets = code->targets;
            error = tier1_emit_transitions(&result->prepared, &result->allocation, scratch, &transitions);
        }
    }
    if (error == TIER1_EMIT_OK && count != 0 && tier1_frame_emit(&result->frame, false, &buffer) != TIER1_FRAME_EMIT_OK)
    {
        error = TIER1_EMIT_FRAME;
    }
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < count; node += 1)
    {
        code->node_first[node] = buffer.count;
        error = tier1_emit_phase(&result->prepared, &result->allocation, &result->frame, &transitions, node * 2, &buffer, result);
        Tier1SelectHoles holes = {0};
        uint32_t argument_registers[BUSTER_ABI_INTEGER_ARGUMENTS] = {0};
        const bool calls = plan->function->op[node] == IR_CALL;
        if (error == TIER1_EMIT_OK && !plan->covered[node] && plan->function->op[node] != IR_BR)
        {
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            bool allocated = plan->function->type[node] == IR_VOID || tier1_select_register_allowed(transitions.registers[node]);
            for (uint32_t p = 0; p < 3; p += 1)
            {
                const uint32_t value = pattern->values[p];
                allocated &= value == BUSTER_IR_NONE || tier1_select_register_allowed(transitions.registers[value]);
            }
            if (plan->function->op[node] == IR_PARAM)
            {
                allocated &= transitions.registers[node] == tier1_select_parameter_registers[plan->function->a[node]];
            }
            if (calls)
            {
                const uint32_t first = plan->effective_use_offsets[node];
                const uint32_t end = plan->effective_use_offsets[node + 1];
                for (uint32_t use = first; use < end; use += 1)
                {
                    const uint32_t value = plan->effective_use_values[use];
                    argument_registers[use - first] = transitions.registers[value];
                    allocated &= tier1_select_register_allowed(transitions.registers[value]);
                }
            }
            if (!allocated)
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (!calls)
            {
                holes = tier1_select_holes(plan, transitions.registers, node);
            }
        }
        if (error == TIER1_EMIT_OK)
        {
            error = tier1_emit_phase(&result->prepared, &result->allocation, &result->frame, &transitions, node * 2 + 1, &buffer, result);
        }
        if (error == TIER1_EMIT_OK && calls)
        {
            error = tier1_emit_call(plan, node, argument_registers, tier1_emit_call_saved(result, &transitions, node), transitions.registers[node], &buffer, scratch, result);
        }
        else if (error == TIER1_EMIT_OK && terminal_sink && node + 1 == count)
        {
            // Only the validated one-instruction final block reaches here. Keep the actual infinite loop if a
            // preceding ordinary call returns; a helper's name is not proof that replacing the loop is legal.
            if (buffer.branch_count == buffer.branch_capacity || !tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_JMP_REL8 }))
            {
                error = TIER1_EMIT_MEMORY;
            }
            else
            {
                buffer.branches[buffer.branch_count] = buffer.count - 1;
                buffer.targets[buffer.branch_count] = code->node_first[node];
                buffer.branch_count += 1;
            }
        }
        else if (error == TIER1_EMIT_OK && !plan->covered[node] && plan->function->op[node] != IR_BR)
        {
            const Tier1SelectRecords selected = tier1_select_expand(&holes);
            const bool returns = plan->function->op[node] == IR_RET;
            if (returns && (selected.count == 0 || selected.records[selected.count - 1].form != X86_FORM_RET))
            {
                error = TIER1_EMIT_INVALID;
            }
            // A return's epilogue goes before its ret, the last record, after any request's record.
            const uint32_t first = buffer.count;
            for (uint32_t r = 0; error == TIER1_EMIT_OK && r < selected.count; r += 1)
            {
                if (returns && r + 1 == selected.count && tier1_frame_emit(&result->frame, true, &buffer) != TIER1_FRAME_EMIT_OK)
                {
                    error = TIER1_EMIT_FRAME;
                }
                else if (!tier1_emit_append(&buffer, selected.records[r]))
                {
                    error = TIER1_EMIT_MEMORY;
                }
            }
            if (error == TIER1_EMIT_OK)
            {
                tier1_select_requests_add(&requests, &selected, first, buffer.records);
            }
        }
        if (error == TIER1_EMIT_OK && !plan->covered[node] && plan->function->type[node] != IR_VOID)
        {
            transitions.owners[transitions.registers[node]] = node;
        }
        if (error != TIER1_EMIT_OK && error_node != 0)
        {
            *error_node = node;
        }
    }
    // The function's last immediates' forms, chosen before the records are checked.
    if (error == TIER1_EMIT_OK)
    {
        tier1_select_requests_flush(&requests, buffer.records);
    }
    if (error == TIER1_EMIT_OK && x86_validate(buffer.records, buffer.count) != buffer.count)
    {
        error = TIER1_EMIT_INVALID;
    }
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < buffer.count; r += 1)
    {
        if (!x86_target_form_allowed(allowed_forms, buffer.records[r].form))
        {
            error = TIER1_EMIT_TARGET;
        }
        else
        {
            code->cost_ids[r] = x86_zen5_id(&buffer.records[r]);
            code->unknown_costs += code->cost_ids[r] == 0;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        code->count = buffer.count;
        code->branch_count = buffer.branch_count;
        code->node_first[count] = buffer.count;
        code->error_node = BUSTER_IR_NONE;
        code->arena_bytes = output->length - output_mark;
        result->transition_scratch_bytes = scratch->length - scratch_mark;
    }
    return error;
}

#include "tier1_emit_cfg.c"

static __attribute__((unused)) Tier1EmitError tier1_emit_function(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const uint8_t* allowed_forms, Arena* output, Arena* scratch, Tier1EmitResult* result, uint32_t* error_node)
{
    const bool arguments = plan && plan->function && options && allowed_forms && output && scratch && output != scratch && output->base != scratch->base && result;
    Tier1EmitError error = arguments ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    const uint64_t output_mark = arguments ? output->length : 0;
    const uint64_t scratch_mark = arguments ? scratch->length : 0;
    Tier1EmitResult emitted = {0};
    if (error_node != 0)
    {
        *error_node = BUSTER_IR_NONE;
    }
    if (error == TIER1_EMIT_OK)
    {
        const uint32_t flags = BUSTER_TIER1_FRAME_KEEP_POINTER | BUSTER_TIER1_FRAME_NO_RED_ZONE | BUSTER_TIER1_FRAME_PROBE_STACK;
        if ((options->frame_flags & ~flags) != 0)
        {
            error = TIER1_EMIT_UNSUPPORTED;
        }
        else if ((options->register_mask & ~tier1_liveness_register_mask()) != 0)
        {
            error = TIER1_EMIT_INVALID;
        }
    }
    // A phi-free straight-line chain keeps this consumer; every other function goes through the CFG consumer.
    const bool chain = error == TIER1_EMIT_OK && tier1_emit_cfg_chain(plan);
    if (error == TIER1_EMIT_OK && !chain)
    {
        error = tier1_emit_cfg_function(plan, options, allowed_forms, output, scratch, &emitted, error_node);
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        const Tier1LivenessError live = tier1_liveness_build(plan, output, scratch, BUSTER_VECTOR_PATH(options->vector), &emitted.liveness);
        error = live == TIER1_LIVENESS_OK ? TIER1_EMIT_OK : live == TIER1_LIVENESS_MEMORY ? TIER1_EMIT_MEMORY : live == TIER1_LIVENESS_LIMIT ? TIER1_EMIT_LIMIT : live == TIER1_LIVENESS_UNSUPPORTED ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        error = tier1_emit_prepare(plan, &emitted.liveness, options, output, &emitted.prepared);
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        const Tier1AllocationError allocated = BUSTER_VECTOR_PATH(options->vector) ? tier1_allocate(&emitted.prepared.allocation, output, &emitted.allocation) : tier1_allocate_scalar(&emitted.prepared.allocation, output, &emitted.allocation);
        error = allocated == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : allocated == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        const uint32_t spill_count = emitted.allocation.spill_count;
        const uint32_t call_slots = (uint32_t)__builtin_popcount(emitted.prepared.call_save_mask);
        const uint32_t* indices = emitted.allocation.spill_values;
        if (call_slots != 0)
        {
            bool ok = true;
            uint32_t* combined = ir_allocate(scratch, (uint64_t)spill_count + call_slots, 4, &ok);
            if (!ok)
            {
                error = TIER1_EMIT_MEMORY;
            }
            else
            {
                if (spill_count != 0)
                {
                    memcpy(combined, indices, (uint64_t)spill_count * sizeof(*combined));
                }
                for (uint32_t slot = 0; slot < call_slots; slot += 1)
                {
                    combined[spill_count + slot] = plan->function->instruction_count + slot;
                }
                indices = combined;
            }
        }
        const Tier1FrameRequest request = {
            .slots = {
                .start = emitted.prepared.starts,
                .end = emitted.prepared.ends,
                .size = emitted.prepared.sizes,
                .alignment = emitted.prepared.alignments,
                .indices = indices,
                .count = spill_count + call_slots,
                .column_count = emitted.prepared.slot_columns,
            },
            .calls = &emitted.prepared.call_abi,
            .call_count = emitted.prepared.call_count != 0,
            .flags = options->frame_flags,
        };
        if (error == TIER1_EMIT_OK)
        {
            const Tier1FrameError framed = BUSTER_VECTOR_PATH(options->vector) ? tier1_frame_plan(&request, output, scratch, &emitted.frame) : tier1_frame_plan_scalar(&request, output, scratch, &emitted.frame);
            error = framed == TIER1_FRAME_OK ? TIER1_EMIT_OK : framed == TIER1_FRAME_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_FRAME;
        }
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        error = tier1_emit_records(plan, allowed_forms, BUSTER_VECTOR_PATH(options->vector), output, scratch, &emitted, error_node);
    }
    if (arguments)
    {
        arena_pop_to(scratch, scratch_mark);
        if (error == TIER1_EMIT_OK)
        {
            emitted.retained_bytes = output->length - output_mark;
            *result = emitted;
        }
        else
        {
            arena_pop_to(output, output_mark);
        }
    }
    return error;
}
