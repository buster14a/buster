// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's machine consumer for a general CFG (#140): a selected plan whose blocks branch anywhere, with loops, and
// whose joins have phis. tier1_emit.c includes it after its straight-line helpers, and tier1_emit_function sends here
// every function that isn't a phi-free straight-line chain, which keeps the straight-line consumer.
//
// The plan becomes tier1_cfg.c's problem. Node n's effective uses are at base + 2n and its definition at the next
// position, with a required use there (the instruction writes a register). A phi is defined at its block's start, its
// operands in the predecessors' order. Parameters are defined at position 0, in their incoming registers, and
// constants are rematerializable when the options ask. tier1_cfg's entry has no predecessors, so where the IR's entry
// has some (a loop's header), an empty block goes before it and base is 2. tier1_cfg_allocate, or its scalar twin,
// then allocates the selected uses over the CFG.
//
// The records, block by block in layout order: the entry's prologue, then at each block's start its single incoming
// edge's moves where tier1_cfg placed them there; each node as the straight-line consumer emits it (its fragment
// transitions at both phases, its selected records, a call's saves and argument moves, a return's epilogue); then the
// block's terminator. A jump's edge moves go before it. A conditional branch's go at its successors' starts or, for a
// split critical edge, in a stub after the branch: the edge's moves, then a jump to the successor. A jump to the
// record that follows it is left out, and a branch is inverted where its true successor follows it or where its true
// edge alone has a stub, which then follows it. Branch targets are record indices, which branch sizing takes.
//
// At every block's start, the emitter's view of where each value is becomes the block's entry state: the intervals
// that cover its start, at tier1_cfg_entry_location, so that no predecessor's exit state leaks into another block. Move
// locations are tier1_cfg's: registers, the values' spill slots and the general class's temporary slot, a frame row
// after the call saves'. A spill slot's frame lifetime is its value's intervals' hull, a phi's widened to its
// predecessors' ends, where its edges write it; two slots share a frame row only where their values are never live at
// once. Fixed ranges and splits don't arise: calls save live registers in rows of their own, as in the straight-line
// consumer, and its values are general registers' alone.
//
// Borrowed inputs are immutable; the arenas are disjoint. tier1_emit_function restores both arenas' lengths on failure
// and leaves the result unchanged.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// A pending jump's goal: a block, or with this bit an edge's stub.
#define BUSTER_TIER1_EMIT_CFG_STUB 0x80000000u

// Whether the function is a straight-line chain without phis, the straight-line consumer's: every block but the last
// jumps to the next, and the last returns, traps or is an empty self-loop.
static bool tier1_emit_cfg_chain(const Tier1SelectPlan* plan)
{
    const IrFunction* function = plan->function;
    bool chain = true;
    for (uint32_t block = 0; chain && block < function->block_count; block += 1)
    {
        const uint32_t last = function->block_offsets[block + 1] - 1;
        const bool final = block + 1 == function->block_count;
        const bool sink = function->op[last] == IR_BR && function->a[last] == block && function->block_offsets[block] == last;
        const bool terminal = function->op[last] == IR_RET || function->op[last] == IR_UNREACHABLE || sink;
        chain = final ? terminal : function->op[last] == IR_BR && function->a[last] == block + 1;
    }
    // A phi needs edge moves, a frame object's address the frame's layout of it, a symbol's its reference, a narrow
    // parameter or one after other instructions the entry's definitions, and a division or a shift by a register its
    // fixed registers' sequence: the CFG consumer's.
    bool prefix = true;
    for (uint32_t node = 0; chain && node < function->instruction_count; node += 1)
    {
        const uint32_t op = function->op[node];
        const uint32_t type = function->type[node];
        const bool narrow = type == IR_I1 || type == IR_I8 || type == IR_I16;
        const uint8_t rule = plan->covered[node] ? TIER1_RULE_NONE : plan->patterns[node].rule;
        chain = op != IR_PHI && op != IR_STACK_ADDR && op != IR_SYMBOL_ADDR && op != IR_CALL_INDIRECT && (op != IR_PARAM || (prefix && !narrow)) && rule != TIER1_RULE_DIVIDE &&
                rule != TIER1_RULE_SHIFT;
        prefix &= op == IR_PARAM;
    }
    return chain;
}

// The inverse of a conditional jump's form, or X86_FORM_NONE for a form the selector doesn't choose.
static uint16_t tier1_emit_cfg_inverse(uint16_t form)
{
    static const uint16_t pairs[][2] = {
        { X86_FORM_JE_REL8, X86_FORM_JNE_REL8 },
        { X86_FORM_JL_REL8, X86_FORM_JGE_REL8 },
        { X86_FORM_JLE_REL8, X86_FORM_JG_REL8 },
        { X86_FORM_JB_REL8, X86_FORM_JAE_REL8 },
        { X86_FORM_JBE_REL8, X86_FORM_JA_REL8 },
    };
    uint16_t inverse = X86_FORM_NONE;
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(pairs); p += 1)
    {
        inverse = pairs[p][0] == form ? pairs[p][1] : pairs[p][1] == form ? pairs[p][0] : inverse;
    }
    return inverse;
}

// The plan's allocation problem, in `output`, which the result keeps (see the header), with the maps between nodes and
// values and the prepared columns the shared helpers read: recipes and sizes by node, the calls' save rows, and the
// frame's columns (a node's slot, each caller-save row, then the temporary slot).
static Tier1EmitError tier1_emit_cfg_problem(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, Arena* output, Arena* scratch, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const uint32_t count = function->instruction_count;
    const bool vector = BUSTER_VECTOR_PATH(options->vector);
    const Tier1LivenessError checked = tier1_liveness_validate_plan(plan, scratch, vector, true);
    Tier1EmitError error = checked == TIER1_LIVENESS_OK ? TIER1_EMIT_OK : checked == TIER1_LIVENESS_MEMORY ? TIER1_EMIT_MEMORY : checked == TIER1_LIVENESS_LIMIT ? TIER1_EMIT_LIMIT : checked == TIER1_LIVENESS_UNSUPPORTED ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
    error = error == TIER1_EMIT_OK && count == 0 ? TIER1_EMIT_INVALID : error;
    IrCfg cfg = { 0 };
    if (error == TIER1_EMIT_OK)
    {
        const IrError built = ir_build_cfg(function, output, scratch, &cfg);
        error = built == IR_OK ? TIER1_EMIT_OK : built == IR_ERROR_MEMORY ? TIER1_EMIT_MEMORY : TIER1_EMIT_INVALID;
    }
    const uint32_t added = error == TIER1_EMIT_OK && cfg.predecessor_offsets[1] != 0 ? 1 : 0;
    const uint32_t base = 2 * added;
    const uint32_t blocks = function->block_count + added;
    const uint32_t edges = cfg.edge_count + added;
    const uint64_t positions = (uint64_t)base + 2 * (uint64_t)count;
    error = error == TIER1_EMIT_OK && positions > BUSTER_TIER1_ALLOCATE_MAX_POSITIONS ? TIER1_EMIT_LIMIT : error;
    // Values, densely: every node with a result that no consumer covers.
    bool ok = error == TIER1_EMIT_OK;
    uint32_t* node_values = ok ? ir_allocate(output, count, 4, &ok) : 0;
    uint32_t* value_nodes = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t value_count = 0;
    uint32_t phi_count = 0;
    uint32_t operand_count = 0;
    uint32_t returns = 0;
    uint32_t symbols = 0;
    uint32_t fixed_saves = 0;
    Tier1EmitPrepared* prepared = &emitted->prepared;
    for (uint32_t node = 0; ok && node < count; node += 1)
    {
        const bool value = plan->covered[node] == 0 && function->type[node] != IR_VOID;
        node_values[node] = value ? value_count : BUSTER_TIER1_CFG_NONE;
        value_nodes[value_count] = node;
        value_count += value;
        phi_count += function->op[node] == IR_PHI;
        operand_count += function->op[node] == IR_PHI ? function->b[node] : 0;
        returns += function->op[node] == IR_RET;
        symbols += function->op[node] == IR_SYMBOL_ADDR;
        // A division's rax and rdx, and a shift's rcx, may need call-save rows for the values it doesn't take.
        const uint8_t rule = plan->covered[node] ? TIER1_RULE_NONE : plan->patterns[node].rule;
        fixed_saves |= rule == TIER1_RULE_DIVIDE ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX) : rule == TIER1_RULE_SHIFT ? 1u << X86_REGISTER_RCX : 0;
        emitted->fixed_count += rule == TIER1_RULE_DIVIDE || rule == TIER1_RULE_SHIFT;
        if (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT)
        {
            prepared->call_count += 1;
            const uint32_t arguments = function->calls[function->b[node]].argument_count;
            prepared->call_abi.integer_registers = prepared->call_abi.integer_registers > arguments ? prepared->call_abi.integer_registers : arguments;
        }
    }
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    // A reference for each call and each symbol's address, and how the symbols are reached.
    emitted->reference_capacity = prepared->call_count + symbols;
    emitted->symbol_access = options->symbol_access;
    emitted->symbol_access_count = options->symbol_access != 0 ? options->symbol_count : 0;
    prepared->call_abi.stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    prepared->call_save_mask = (prepared->call_count != 0 ? options->register_mask : 0) | (fixed_saves & options->register_mask);
    const uint32_t call_slots = (uint32_t)__builtin_popcount(prepared->call_save_mask);
    // The slots' columns: a value's at its node, the caller-save rows', the temporary slot's, then each frame object's,
    // live throughout (tier1_emit_cfg_object).
    const uint32_t objects = function->slot_count;
    prepared->slot_columns = count + call_slots + 1 + objects;
    ok = error == TIER1_EMIT_OK;
    prepared->starts = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    prepared->ends = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    prepared->sizes = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    prepared->alignments = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    uint8_t* operations = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint8_t* widths = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint8_t* flags = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint8_t* marks = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint64_t* constants = ok ? ir_allocate(output, count, 8, &ok) : 0;
    uint32_t* prospective = ok ? ir_allocate(scratch, (uint64_t)value_count + call_slots + 1 + objects, 4, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t column = 0; column < prepared->slot_columns; column += 1)
        {
            prepared->starts[column] = 0;
            prepared->ends[column] = (uint32_t)positions;
            prepared->sizes[column] = 8;
            prepared->alignments[column] = 8;
        }
        for (uint32_t o = 0; o < objects; o += 1)
        {
            const IrSlot* slot = &function->slots[o];
            prepared->sizes[count + call_slots + 1 + o] = slot->size != 0 ? (uint32_t)slot->size : 1;
            prepared->alignments[count + call_slots + 1 + o] = slot->alignment != 0 ? slot->alignment : 1;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            widths[node] = (uint8_t)tier1_select_width(function->type[node]);
            prepared->sizes[node] = widths[node] != 0 ? widths[node] : 8;
            prepared->alignments[node] = prepared->sizes[node];
            if (options->rematerialize && !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_CONSTANT)
            {
                operations[node] = BUSTER_TIER1_REMAT_INTEGER;
                constants[node] = plan->patterns[node].immediate;
            }
        }
        prepared->recipes = (Tier1RematerializationInput){
            .operations = operations,
            .widths = widths,
            .flags = flags,
            .values = constants,
            .count = count,
        };
        const bool marked = vector ? tier1_rematerialization_marks(&prepared->recipes, marks) : tier1_rematerialization_marks_scalar(&prepared->recipes, marks);
        error = marked ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    }
    // Frame-base reservations before allocation, from every value as a prospective slot, the caller-save rows and the
    // temporary slot: the actual set, a subset of it, can't add one.
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            prospective[v] = value_nodes[v];
        }
        for (uint32_t slot = 0; slot <= call_slots + objects; slot += 1)
        {
            prospective[value_count + slot] = count + slot;
        }
        const Tier1FrameRequest request = {
            .slots = {
                .start = prepared->starts,
                .end = prepared->ends,
                .size = prepared->sizes,
                .alignment = prepared->alignments,
                .indices = prospective,
                .count = value_count + call_slots + 1 + objects,
                .column_count = prepared->slot_columns,
            },
            .calls = &prepared->call_abi,
            .call_count = prepared->call_count != 0,
            .flags = options->frame_flags,
        };
        Tier1FrameRequirements requirements = { 0 };
        error = tier1_frame_requirements(&request, &requirements) == TIER1_FRAME_OK ? TIER1_EMIT_OK : TIER1_EMIT_FRAME;
        prepared->reserved_registers = requirements.reserved_registers | (1u << BUSTER_TIER1_SELECT_SCRATCH);
    }
    const uint32_t registers = options->register_mask & ~prepared->reserved_registers;
    ok = error == TIER1_EMIT_OK;
    uint32_t* block_starts = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* successor_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* successors = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    uint32_t* predecessor_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* predecessors = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    uint8_t* classes = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint32_t* allowed = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* defined = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* definitions = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint8_t* rematerializable = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint32_t* use_offsets = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    const uint64_t use_capacity = (uint64_t)plan->effective_use_count + value_count + 1;
    uint32_t* use_positions = ok ? ir_allocate(output, use_capacity, 4, &ok) : 0;
    uint8_t* use_required = ok ? ir_allocate(output, use_capacity, 1, &ok) : 0;
    uint32_t* use_cursors = ok ? ir_allocate(scratch, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* phi_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* phi_values = ok ? ir_allocate(output, (uint64_t)phi_count + 1, 4, &ok) : 0;
    uint32_t* phi_operand_offsets = ok ? ir_allocate(output, (uint64_t)phi_count + 1, 4, &ok) : 0;
    uint32_t* phi_operands = ok ? ir_allocate(output, (uint64_t)operand_count + 1, 4, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK)
    {
        // Blocks and edges, the added entry's first: each IR block one later, its successors in the IR's order (a
        // conditional branch's true successor first) and its predecessors in block order.
        block_starts[0] = 0;
        successor_offsets[0] = 0;
        predecessor_offsets[0] = 0;
        uint32_t edge = 0;
        if (added != 0)
        {
            successors[0] = 1;
            edge = 1;
        }
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            block_starts[b + added] = base + 2 * function->block_offsets[b];
            successor_offsets[b + added] = edge;
            for (uint32_t e = cfg.successor_offsets[b]; e < cfg.successor_offsets[b + 1]; e += 1)
            {
                successors[edge] = cfg.successors[e] + added;
                edge += 1;
            }
        }
        block_starts[blocks] = (uint32_t)positions;
        successor_offsets[blocks] = edge;
        edge = 0;
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            predecessor_offsets[b + added] = edge;
            if (added != 0 && b == 0)
            {
                predecessors[edge] = 0;
                edge += 1;
            }
            for (uint32_t e = cfg.predecessor_offsets[b]; e < cfg.predecessor_offsets[b + 1]; e += 1)
            {
                predecessors[edge] = cfg.predecessors[e] + added;
                edge += 1;
            }
        }
        predecessor_offsets[blocks] = edge;
        // Values: a parameter in its incoming register from position 0 (its definition's interval alone: elsewhere it may
        // be in any register), a phi at its block's start, any other at its node's definition phase.
        use_offsets[0] = 0;
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            const uint32_t node = value_nodes[v];
            const uint32_t op = function->op[node];
            classes[v] = 0;
            allowed[v] = registers;
            defined[v] = op == IR_PARAM ? tier1_liveness_parameter_mask(function->a[node]) & registers : 0;
            definitions[v] = op == IR_PARAM ? 0 : op == IR_PHI ? block_starts[cfg.instruction_blocks[node] + added] : base + 2 * node + 1;
            rematerializable[v] = marks[node];
            use_offsets[v + 1] = op != IR_PHI;
        }
        // Uses by value, counted then filled in node order, which is position order: a node's effective uses, then its
        // definition's own use. A parameter's, at position 0, precedes every other use of it.
        for (uint32_t node = 0; node < count; node += 1)
        {
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                use_offsets[node_values[plan->effective_use_values[use]] + 1] += 1;
            }
        }
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            use_offsets[v + 1] += use_offsets[v];
            use_cursors[v] = use_offsets[v];
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            const uint32_t defined = node_values[node];
            if (defined != BUSTER_TIER1_CFG_NONE && function->op[node] == IR_PARAM)
            {
                use_positions[use_cursors[defined]] = 0;
                use_required[use_cursors[defined]] = 1;
                use_cursors[defined] += 1;
            }
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t v = node_values[plan->effective_use_values[use]];
                use_positions[use_cursors[v]] = base + 2 * node;
                use_required[use_cursors[v]] = 1;
                use_cursors[v] += 1;
            }
            if (defined != BUSTER_TIER1_CFG_NONE && function->op[node] != IR_PARAM && function->op[node] != IR_PHI)
            {
                use_positions[use_cursors[defined]] = base + 2 * node + 1;
                use_required[use_cursors[defined]] = 1;
                use_cursors[defined] += 1;
            }
        }
        // Phis, at their blocks' starts, each operand in its block's predecessors' order: the IR's pairs name each
        // predecessor once. The IR's entry has no phi, having no predecessor; with an added entry before it, its phis
        // would need an operand for the function's entry, which the IR has none of.
        uint32_t phi = 0;
        uint32_t operand = 0;
        for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
        {
            phi_offsets[b] = phi;
            const uint32_t first = b < added ? 0 : function->block_offsets[b - added];
            const uint32_t end = b < added ? 0 : function->block_offsets[b - added + 1];
            bool leading = true;
            for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
            {
                const bool is_phi = function->op[node] == IR_PHI;
                error = !is_phi || (leading && b != added) ? error : added != 0 ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
                leading = leading && is_phi;
                if (error == TIER1_EMIT_OK && is_phi)
                {
                    phi_values[phi] = node_values[node];
                    phi_operand_offsets[phi] = operand;
                    for (uint32_t e = predecessor_offsets[b]; error == TIER1_EMIT_OK && e < predecessor_offsets[b + 1]; e += 1)
                    {
                        const uint32_t source = predecessors[e] - added;
                        uint32_t incoming = BUSTER_IR_NONE;
                        for (uint32_t pair = 0; pair < function->b[node]; pair += 1)
                        {
                            const uint32_t* entry = function->extra + function->a[node] + (uint64_t)pair * 2;
                            incoming = entry[0] == source ? entry[1] : incoming;
                        }
                        const uint32_t v = incoming < count ? node_values[incoming] : BUSTER_TIER1_CFG_NONE;
                        error = v != BUSTER_TIER1_CFG_NONE ? error : TIER1_EMIT_INVALID;
                        phi_operands[operand] = v;
                        operand += 1;
                    }
                    phi += 1;
                }
            }
        }
        phi_offsets[blocks] = phi;
        phi_operand_offsets[phi] = operand;
        emitted->cfg_input = (Tier1CfgInput){
            .block_starts = block_starts,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
            .classes = classes,
            .allowed_registers = allowed,
            .definition_registers = defined,
            .definitions = definitions,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = use_required,
            .phi_offsets = phi_offsets,
            .phi_values = phi_values,
            .phi_operand_offsets = phi_operand_offsets,
            .phi_operands = phi_operands,
            .block_count = blocks,
            .edge_count = edges,
            .value_count = value_count,
            .use_count = use_offsets[value_count],
            .phi_count = phi,
            .position_count = (uint32_t)positions,
            .registers = {
                registers,
                0,
            },
            .scratch_registers = {
                BUSTER_TIER1_SELECT_SCRATCH,
                0,
            },
        };
        emitted->value_nodes = value_nodes;
        emitted->node_values = node_values;
        // The records' capacity counts the epilogues, one per return: tier1_emit_cfg_records reads it here.
        emitted->jumps = returns;
    }
    return error;
}

// The frame: each spilled value's slot over its intervals' hull (a phi's widened to its predecessors' ends), the
// caller-save rows, and the temporary slot where a move used it, in that row order.
static Tier1EmitError tier1_emit_cfg_frame(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, Arena* output, Arena* scratch, Tier1EmitResult* emitted)
{
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    Tier1EmitPrepared* prepared = &emitted->prepared;
    const uint32_t count = plan->function->instruction_count;
    const uint32_t spills = cfg->allocation.spill_count;
    const uint32_t call_slots = (uint32_t)__builtin_popcount(prepared->call_save_mask);
    const uint32_t temporary = (cfg->temp_slots & 1u) != 0;
    bool ok = true;
    const uint32_t objects = plan->function->slot_count;
    uint32_t* indices = ir_allocate(scratch, (uint64_t)spills + call_slots + 1 + objects, 4, &ok);
    uint8_t* phis = ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok);
    Tier1EmitError error = ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK)
    {
        memset(phis, 0, (uint64_t)input->value_count + 1);
        for (uint32_t k = 0; k < input->phi_count; k += 1)
        {
            phis[input->phi_values[k]] = 1;
        }
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < spills; k += 1)
    {
        const uint32_t v = cfg->allocation.spill_values[k];
        const uint32_t node = emitted->value_nodes[v];
        uint32_t start = input->position_count;
        uint32_t end = 0;
        for (uint32_t i = cfg->value_interval_offsets[v]; i < cfg->value_interval_offsets[v + 1]; i += 1)
        {
            start = cfg->intervals.starts[i] < start ? cfg->intervals.starts[i] : start;
            end = cfg->intervals.ends[i] > end ? cfg->intervals.ends[i] : end;
        }
        // A phi's slot is written at its predecessors' ends, where the phi isn't live yet.
        const uint32_t home = phis[v] != 0 ? tier1_cfg_block_of(input, input->definitions[v]) : 0;
        for (uint32_t e = phis[v] != 0 ? input->predecessor_offsets[home] : 0; phis[v] != 0 && e < input->predecessor_offsets[home + 1]; e += 1)
        {
            const uint32_t exit = input->block_starts[input->predecessors[e] + 1] - 1;
            start = exit < start ? exit : start;
            end = exit + 1 > end ? exit + 1 : end;
        }
        prepared->starts[node] = start;
        prepared->ends[node] = end;
        indices[k] = node;
    }
    for (uint32_t slot = 0; error == TIER1_EMIT_OK && slot < call_slots + temporary; slot += 1)
    {
        indices[spills + slot] = count + slot;
    }
    // The frame objects' rows last, each its column past the temporary slot's.
    for (uint32_t o = 0; error == TIER1_EMIT_OK && o < objects; o += 1)
    {
        indices[spills + call_slots + temporary + o] = count + call_slots + 1 + o;
    }
    if (error == TIER1_EMIT_OK)
    {
        const Tier1FrameRequest request = {
            .slots = {
                .start = prepared->starts,
                .end = prepared->ends,
                .size = prepared->sizes,
                .alignment = prepared->alignments,
                .indices = indices,
                .count = spills + call_slots + temporary + objects,
                .column_count = prepared->slot_columns,
            },
            .calls = &prepared->call_abi,
            .call_count = prepared->call_count != 0,
            .flags = options->frame_flags,
        };
        const Tier1FrameError framed = BUSTER_VECTOR_PATH(options->vector) ? tier1_frame_plan(&request, output, scratch, &emitted->frame) : tier1_frame_plan_scalar(&request, output, scratch, &emitted->frame);
        error = framed == TIER1_FRAME_OK ? TIER1_EMIT_OK : framed == TIER1_FRAME_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_FRAME;
    }
    return error;
}

// The frame offset of a move location's slot: a value's spill slot, or the temporary slot after the call saves' rows.
static bool tier1_emit_cfg_slot(const Tier1EmitResult* emitted, uint32_t location, int32_t* offset)
{
    const uint32_t spills = emitted->cfg.allocation.spill_count;
    const uint32_t index = location - BUSTER_TIER1_CFG_SPILL;
    const uint32_t row = index < spills ? index : spills + (uint32_t)__builtin_popcount(emitted->prepared.call_save_mask);
    const bool found = location >= BUSTER_TIER1_CFG_SPILL && index <= spills && row < emitted->frame.count;
    *offset = found ? emitted->frame.offset[row] : 0;
    return found;
}

// The frame offset of frame object `object`: its row, the last ones (tier1_emit_cfg_frame).
static bool tier1_emit_cfg_object(const Tier1EmitResult* emitted, uint32_t object, int32_t* offset)
{
    const uint32_t temporary = (emitted->cfg.temp_slots & 1u) != 0;
    const uint32_t row = emitted->cfg.allocation.spill_count + (uint32_t)__builtin_popcount(emitted->prepared.call_save_mask) + temporary + object;
    const bool found = row < emitted->frame.count;
    *offset = found ? emitted->frame.offset[row] : 0;
    return found;
}

// A division or remainder, or a shift by a register (TIER1_RULE_DIVIDE, TIER1_RULE_SHIFT): x86's fixed registers, rax
// and rdx or cl, taken after the operands are read, the divisor or the shifted value through the scratch register; any
// other value live past the node in those registers saved around it in its call-save row (tier1_emit_cfg_problem gives
// it one).
static Tier1EmitError tier1_emit_cfg_fixed(Tier1EmitResult* emitted, const Tier1EmitTransitions* transitions, const Tier1SelectPattern* pattern, uint32_t node, uint32_t use,
                                           const Tier1SelectHoles* holes, Tier1EmitBuffer* buffer)
{
    const bool divide = pattern->rule == TIER1_RULE_DIVIDE;
    const uint32_t width = pattern->width;
    const uint32_t fixed = divide ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX) : 1u << X86_REGISTER_RCX;
    const uint8_t destination = holes->destination;
    const uint8_t scratch = BUSTER_TIER1_SELECT_SCRATCH;
    uint32_t saved = 0;
    for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        const uint32_t owner = transitions->owners[reg];
        const bool live = ((fixed >> reg) & 1) != 0 && owner != BUSTER_IR_NONE && owner != node && transitions->registers[owner] == reg &&
                          tier1_cfg_interval_at(&emitted->cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
        saved |= live && reg != destination ? 1u << reg : 0;
    }
    bool appended = (saved & ~emitted->prepared.call_save_mask) == 0;
    for (uint32_t reg = 0; appended && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        appended = ((saved >> reg) & 1) == 0 || tier1_emit_append(buffer, tier1_emit_call_save(emitted, reg, false));
        emitted->call_stores += (saved >> reg) & 1;
    }
    if (divide)
    {
        const bool signed_divide = (pattern->immediate & 1) != 0;
        const uint8_t result = (pattern->immediate & 2) != 0 ? X86_REGISTER_RDX : X86_REGISTER_RAX;
        appended = appended && tier1_emit_append(buffer, tier1_emit_move(holes->right, scratch, 8));
        appended = appended && (holes->left == X86_REGISTER_RAX || tier1_emit_append(buffer, tier1_emit_move(holes->left, X86_REGISTER_RAX, 8)));
        const X86Instruction extend = signed_divide ? (X86Instruction){ .form = width == 8 ? X86_FORM_CQO : X86_FORM_CDQ } :
                                                      (X86Instruction){ .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RDX };
        appended = appended && tier1_emit_append(buffer, extend);
        const uint16_t form = width == 8 ? (signed_divide ? X86_FORM_IDIV_RM64 : X86_FORM_DIV_RM64) : (signed_divide ? X86_FORM_IDIV_RM32 : X86_FORM_DIV_RM32);
        appended = appended && tier1_emit_append(buffer, (X86Instruction){ .form = form, .rm = scratch });
        appended = appended && (destination == result || tier1_emit_append(buffer, tier1_emit_move(result, destination, width)));
    }
    else
    {
        const uint16_t form = pattern->operation == X86_OPERATION_SHL ? (width == 8 ? X86_FORM_SHL_RM64_CL : X86_FORM_SHL_RM32_CL) :
                              pattern->operation == X86_OPERATION_SHR ? (width == 8 ? X86_FORM_SHR_RM64_CL : X86_FORM_SHR_RM32_CL) :
                                                                        (width == 8 ? X86_FORM_SAR_RM64_CL : X86_FORM_SAR_RM32_CL);
        appended = appended && tier1_emit_append(buffer, tier1_emit_move(holes->left, scratch, 8));
        appended = appended && (holes->right == X86_REGISTER_RCX || tier1_emit_append(buffer, tier1_emit_move(holes->right, X86_REGISTER_RCX, 4)));
        appended = appended && tier1_emit_append(buffer, (X86Instruction){ .form = form, .rm = scratch });
        appended = appended && tier1_emit_append(buffer, tier1_emit_move(scratch, destination, width));
    }
    for (uint32_t reg = 0; appended && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        appended = ((saved >> reg) & 1) == 0 || tier1_emit_append(buffer, tier1_emit_call_save(emitted, reg, true));
        emitted->call_loads += (saved >> reg) & 1;
    }
    return appended ? TIER1_EMIT_OK : (saved & ~emitted->prepared.call_save_mask) != 0 ? TIER1_EMIT_ALLOCATION : TIER1_EMIT_MEMORY;
}

// Moves [first, end) of tier1_cfg's columns: registers and slots by tier1_cfg's location ids, and recomputations of
// constants, into a slot through the scratch register.
static Tier1EmitError tier1_emit_cfg_moves(Tier1EmitResult* emitted, uint32_t first, uint32_t end, Tier1EmitBuffer* buffer)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    Tier1EmitError error = TIER1_EMIT_OK;
    for (uint32_t m = first; error == TIER1_EMIT_OK && m < end; m += 1)
    {
        const uint32_t from = cfg->move_sources[m];
        const uint32_t to = cfg->move_destinations[m];
        const uint32_t node = emitted->value_nodes[cfg->move_values[m]];
        const uint32_t width = emitted->prepared.sizes[node];
        X86Instruction records[2] = { { 0 }, { 0 } };
        uint32_t record_count = 0;
        int32_t offset = 0;
        if (from == BUSTER_TIER1_CFG_REMATERIALIZE)
        {
            const uint8_t target = to < 16 ? (uint8_t)to : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
            Tier1Rematerialized copy = { 0 };
            const bool placed = to < 16 || tier1_emit_cfg_slot(emitted, to, &offset);
            const bool made = placed && tier1_rematerialize(&emitted->prepared.recipes, node, target, base, UINT8_MAX, &copy) && copy.symbol == BUSTER_TIER1_REMAT_NO_SYMBOL;
            error = made ? error : TIER1_EMIT_ALLOCATION;
            records[record_count++] = copy.instruction;
            if (to >= 16)
            {
                records[record_count++] = (X86Instruction){ .displacement = offset, .reg = target, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32 };
            }
            emitted->rematerializations += 1;
        }
        else if (from < 16 && to < 16)
        {
            records[record_count++] = tier1_emit_move(from, to, 8);
        }
        else if (from < 16 && tier1_emit_cfg_slot(emitted, to, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)from, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32 };
            emitted->spill_stores += 1;
        }
        else if (to < 16 && tier1_emit_cfg_slot(emitted, from, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)to, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32 };
            emitted->spill_loads += 1;
        }
        else
        {
            // tier1_cfg moves slot to slot through the scratch register, in two moves.
            error = TIER1_EMIT_ALLOCATION;
        }
        for (uint32_t r = 0; error == TIER1_EMIT_OK && r < record_count; r += 1)
        {
            error = tier1_emit_append(buffer, records[r]) ? error : TIER1_EMIT_MEMORY;
        }
        emitted->edge_moves += error == TIER1_EMIT_OK ? record_count : 0;
    }
    return error;
}

// A jump of `form` to `goal` (a block, or an edge's stub with BUSTER_TIER1_EMIT_CFG_STUB), resolved once every block's
// records are in: its branch slot and goal go in the pending columns.
static Tier1EmitError tier1_emit_cfg_jump(Tier1EmitBuffer* buffer, uint16_t form, uint32_t goal, uint32_t* pending_branches, uint32_t* pending_goals, uint32_t* pending, Tier1EmitResult* emitted)
{
    const bool room = buffer->branch_count < buffer->branch_capacity && tier1_emit_append(buffer, (X86Instruction){ .form = form });
    if (room)
    {
        buffer->branches[buffer->branch_count] = buffer->count - 1;
        buffer->targets[buffer->branch_count] = 0;
        pending_branches[*pending] = buffer->branch_count;
        pending_goals[*pending] = goal;
        *pending += 1;
        buffer->branch_count += 1;
        emitted->jumps += 1;
    }
    return room ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
}

// A conditional branch's records after its comparison: the jumps to its two successors' blocks or stubs, and the stubs
// of its split edges, `e_true` and `e_true + 1` its edges and `b` its block.
static Tier1EmitError tier1_emit_cfg_branch(Tier1EmitResult* emitted, uint32_t b, uint32_t e_true, uint16_t condition, Tier1EmitBuffer* buffer, uint32_t* stub_first, uint32_t* pending_branches, uint32_t* pending_goals, uint32_t* pending)
{
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t e_false = e_true + 1;
    const uint32_t t = input->successors[e_true];
    const uint32_t f = input->successors[e_false];
    const bool split_true = cfg->edge_placements[e_true] == TIER1_CFG_EDGE_SPLIT;
    const bool split_false = cfg->edge_placements[e_false] == TIER1_CFG_EDGE_SPLIT;
    const uint16_t inverse = tier1_emit_cfg_inverse(condition);
    Tier1EmitError error = inverse != X86_FORM_NONE ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK && t == b + 1 && !split_true && !split_false)
    {
        // The true successor follows: jump on the inverse to the false one, and fall through.
        error = tier1_emit_cfg_jump(buffer, inverse, f, pending_branches, pending_goals, pending, emitted);
    }
    else if (error == TIER1_EMIT_OK && split_true && !split_false)
    {
        // Only the true edge has a stub: jump on the inverse to the false successor and fall into the stub, so that the
        // true path (a loop's back edge, often) takes one jump rather than two.
        error = tier1_emit_cfg_jump(buffer, inverse, f, pending_branches, pending_goals, pending, emitted);
        stub_first[e_true] = buffer->count;
        emitted->edge_stubs += 1;
        error = error == TIER1_EMIT_OK ? tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_true], cfg->edge_move_offsets[e_true + 1], buffer) : error;
        if (error == TIER1_EMIT_OK && t != b + 1)
        {
            error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, t, pending_branches, pending_goals, pending, emitted);
        }
    }
    else if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_jump(buffer, condition, split_true ? BUSTER_TIER1_EMIT_CFG_STUB | e_true : t, pending_branches, pending_goals, pending, emitted);
        // The false edge: its stub falls through from the branch, or a jump, unless its block follows.
        if (error == TIER1_EMIT_OK && split_false)
        {
            stub_first[e_false] = buffer->count;
            emitted->edge_stubs += 1;
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_false], cfg->edge_move_offsets[e_false + 1], buffer);
        }
        if (error == TIER1_EMIT_OK && (f != b + 1 || split_true))
        {
            error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, f, pending_branches, pending_goals, pending, emitted);
        }
        if (error == TIER1_EMIT_OK && split_true)
        {
            stub_first[e_true] = buffer->count;
            emitted->edge_stubs += 1;
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_true], cfg->edge_move_offsets[e_true + 1], buffer);
            if (error == TIER1_EMIT_OK && t != b + 1)
            {
                error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, t, pending_branches, pending_goals, pending, emitted);
            }
        }
    }
    return error;
}

// Every block's records: see the header.
static Tier1EmitError tier1_emit_cfg_records(const Tier1SelectPlan* plan, const uint8_t* allowed_forms, bool vector, Arena* output, Arena* scratch, Tier1EmitResult* emitted, uint32_t* error_node)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = input->block_count;
    const uint32_t added = blocks - function->block_count;
    const uint32_t base = 2 * added;
    const uint32_t returns = emitted->jumps;
    emitted->jumps = 0;
    const uint64_t capacity = (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE + (uint64_t)allocation->fragment_count * 2 + (uint64_t)cfg->move_count * 2 + BUSTER_TIER1_FRAME_EMIT_RECORDS * ((uint64_t)returns + 1) + (uint64_t)emitted->prepared.call_count * BUSTER_TIER1_EMIT_CALL_RECORDS + (uint64_t)blocks * 2 + input->edge_count +
                              (uint64_t)emitted->fixed_count * 6;
    const uint64_t branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES * ((uint64_t)returns + 1) + (uint64_t)blocks * 2 + input->edge_count;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitError error = capacity <= UINT32_MAX && branch_capacity <= UINT32_MAX ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1SelectCode* code = &emitted->code;
    Tier1EmitBuffer buffer = { .capacity = (uint32_t)capacity, .branch_capacity = (uint32_t)branch_capacity };
    Tier1SelectRequests requests = { .vector = vector };
    // The emitter's view by node, as the straight-line consumer keeps it, and the allocation with its spill ids by node,
    // which the shared helpers read.
    Tier1EmitTransitions transitions = { 0 };
    Tier1Allocation view = *allocation;
    bool ok = error == TIER1_EMIT_OK;
    code->records = ok ? ir_allocate(output, capacity, sizeof(X86Instruction), &ok) : 0;
    code->cost_ids = ok ? ir_allocate(output, capacity, 1, &ok) : 0;
    code->node_first = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    code->branches = ok ? ir_allocate(output, branch_capacity, 4, &ok) : 0;
    code->targets = ok ? ir_allocate(output, branch_capacity, 4, &ok) : 0;
    emitted->references = ok && emitted->reference_capacity != 0 ? ir_allocate(output, emitted->reference_capacity, sizeof(LinkReference), &ok) : 0;
    transitions.offsets = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 2, 4, &ok) : 0;
    transitions.fragments = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.values = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.registers = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    uint32_t* spill_ids = ok ? ir_allocate(scratch, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* cursors = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok) : 0;
    uint32_t* entry_offsets = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* block_first = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* stub_first = ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 4, &ok) : 0;
    uint32_t* pending_branches = ok ? ir_allocate(scratch, branch_capacity + 1, 4, &ok) : 0;
    uint32_t* pending_goals = ok ? ir_allocate(scratch, branch_capacity + 1, 4, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    uint32_t* entries = 0;
    uint32_t pending = 0;
    if (error == TIER1_EMIT_OK)
    {
        buffer.records = code->records;
        buffer.branches = code->branches;
        buffer.targets = code->targets;
        view.spill_ids = spill_ids;
        memset(transitions.offsets, 0, ((uint64_t)input->position_count + 2) * 4);
        memset(transitions.registers, UINT8_MAX, (uint64_t)count + 1);
        memset(spill_ids, 0xff, ((uint64_t)count + 1) * 4);
        memset(entry_offsets, 0, ((uint64_t)blocks + 1) * 4);
        for (uint32_t v = 0; v < input->value_count; v += 1)
        {
            spill_ids[emitted->value_nodes[v]] = allocation->spill_ids[v];
        }
    }
    // Fragment transitions by position, and each block's entry intervals (those that cover its start), each counted then
    // filled. A definition's first fragment is its register (its use there is required); a reload at a definition
    // phase, or a move from register to register within an interval, is the straight-line consumer's refusal too.
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            for (uint32_t q = 0; q < input->position_count; q += 1)
            {
                transitions.offsets[q + 1] += transitions.offsets[q];
                cursors[q] = transitions.offsets[q];
            }
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                entry_offsets[b + 1] += entry_offsets[b];
                block_first[b] = entry_offsets[b];
            }
            entries = ir_allocate(scratch, (uint64_t)entry_offsets[blocks] + 1, 4, &ok);
            error = ok ? error : TIER1_EMIT_MEMORY;
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
        {
            const uint32_t v = cfg->intervals.values[i];
            const uint32_t node = emitted->value_nodes[v];
            const uint32_t first = allocation->fragment_offsets[i];
            const uint32_t start = cfg->intervals.starts[i];
            const bool defines = start == input->definitions[v] && function->op[node] != IR_PHI;
            error = !defines || allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER ? error : TIER1_EMIT_ALLOCATION;
            for (uint32_t f = first + 1; error == TIER1_EMIT_OK && f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                const uint8_t kind = allocation->kinds[f];
                const uint8_t previous = allocation->kinds[f - 1];
                const uint32_t q = allocation->starts[f];
                error = kind != BUSTER_TIER1_ALLOCATE_REGISTER || (previous != BUSTER_TIER1_ALLOCATE_REGISTER && ((q - base) & 1) == 0) ? error : TIER1_EMIT_UNSUPPORTED;
                error = kind != BUSTER_TIER1_ALLOCATE_SPILL || previous != BUSTER_TIER1_ALLOCATE_REMATERIALIZE ? error : TIER1_EMIT_ALLOCATION;
                if (pass == 0)
                {
                    transitions.offsets[q + 1] += 1;
                }
                else
                {
                    transitions.fragments[cursors[q]] = f;
                    cursors[q] += 1;
                }
                transitions.values[f] = node;
            }
            // The blocks whose starts the interval covers.
            uint32_t b = tier1_cfg_block_of(input, start);
            b += input->block_starts[b] < start ? 1 : 0;
            for (; error == TIER1_EMIT_OK && b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
            {
                if (pass == 0)
                {
                    entry_offsets[b + 1] += 1;
                }
                else
                {
                    entries[block_first[b]] = i;
                    block_first[b] += 1;
                }
            }
        }
    }
    if (error == TIER1_EMIT_OK && tier1_frame_emit(&emitted->frame, false, &buffer) != TIER1_FRAME_EMIT_OK)
    {
        error = TIER1_EMIT_FRAME;
    }
    // A narrow parameter into its canonical 32-bit storage, in its register before anything reads it: its caller
    // extends it by its own type (sign or zero), which the canonical form doesn't keep.
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < count && function->block_count != 0 && node < function->block_offsets[1]; node += 1)
    {
        const uint32_t type = function->type[node];
        const uint32_t narrow = type == IR_I1 || type == IR_I8 ? 1 : type == IR_I16 ? 2 : 0;
        if (function->op[node] == IR_PARAM && narrow != 0 && emitted->node_values[node] != BUSTER_TIER1_CFG_NONE)
        {
            const uint8_t reg = (uint8_t)tier1_select_parameter_registers[function->a[node]];
            const X86Instruction extend = { .form = narrow == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, .reg = reg, .rm = reg };
            error = tier1_emit_append(&buffer, extend) ? error : TIER1_EMIT_MEMORY;
        }
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        block_first[b] = b == 0 ? 0 : buffer.count;
        const uint32_t entry = input->block_starts[b];
        // The single incoming edge's moves, where tier1_cfg placed them at this block's start.
        if (input->predecessor_offsets[b + 1] - input->predecessor_offsets[b] == 1)
        {
            const uint32_t p = input->predecessors[input->predecessor_offsets[b]];
            uint32_t e = input->successor_offsets[p];
            while (input->successors[e] != b)
            {
                e += 1;
            }
            if (cfg->edge_placements[e] == TIER1_CFG_EDGE_SUCCESSOR)
            {
                error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e], cfg->edge_move_offsets[e + 1], &buffer);
            }
        }
        // The entry state: every interval that covers the block's start, where control brings it.
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            transitions.owners[reg] = BUSTER_IR_NONE;
        }
        for (uint32_t x = entry_offsets[b]; error == TIER1_EMIT_OK && x < entry_offsets[b + 1]; x += 1)
        {
            const uint32_t v = cfg->intervals.values[entries[x]];
            const uint32_t node = emitted->value_nodes[v];
            uint32_t location = 0;
            uint8_t kind = 0;
            error = tier1_cfg_entry_location(cfg, v, entry, &location, &kind) ? error : TIER1_EMIT_ALLOCATION;
            const bool held = error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16;
            transitions.registers[node] = held ? (uint8_t)location : UINT8_MAX;
            if (held)
            {
                transitions.owners[location] = node;
            }
        }
        const uint32_t first = b < added ? 0 : function->block_offsets[b - added];
        const uint32_t end = b < added ? 0 : function->block_offsets[b - added + 1];
        for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
        {
            const uint32_t op = function->op[node];
            const uint32_t use = base + 2 * node;
            const uint32_t v = emitted->node_values[node];
            const bool selected = !plan->covered[node] && op != IR_PHI && op != IR_PARAM;
            code->node_first[node] = buffer.count;
            error = tier1_emit_phase(&emitted->prepared, &view, &emitted->frame, &transitions, use, &buffer, emitted);
            // A definition writes its interval's first register; a parameter and a phi are where the entry state put
            // them.
            if (error == TIER1_EMIT_OK && v != BUSTER_TIER1_CFG_NONE && selected)
            {
                const uint32_t interval = tier1_cfg_interval_at(cfg, v, use + 1);
                error = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == use + 1 ? error : TIER1_EMIT_ALLOCATION;
                transitions.registers[node] = error == TIER1_EMIT_OK ? allocation->registers[allocation->fragment_offsets[interval]] : UINT8_MAX;
            }
            const bool calls = op == IR_CALL || op == IR_CALL_INDIRECT;
            Tier1SelectHoles holes = { 0 };
            // The arguments' registers, and an indirect call's callee's after them.
            uint32_t argument_registers[BUSTER_ABI_INTEGER_ARGUMENTS + 1] = { 0 };
            if (error == TIER1_EMIT_OK && selected)
            {
                const Tier1SelectPattern* pattern = &plan->patterns[node];
                bool registered = function->type[node] == IR_VOID || tier1_select_register_allowed(transitions.registers[node]);
                for (uint32_t p = 0; p < 3; p += 1)
                {
                    const uint32_t value = pattern->values[p];
                    registered &= value == BUSTER_IR_NONE || tier1_select_register_allowed(transitions.registers[value]);
                }
                for (uint32_t u = plan->effective_use_offsets[node]; calls && u < plan->effective_use_offsets[node + 1]; u += 1)
                {
                    const uint32_t value = plan->effective_use_values[u];
                    argument_registers[u - plan->effective_use_offsets[node]] = transitions.registers[value];
                    registered &= tier1_select_register_allowed(transitions.registers[value]);
                }
                error = registered ? error : TIER1_EMIT_ALLOCATION;
                if (error == TIER1_EMIT_OK && !calls)
                {
                    holes = tier1_select_holes(plan, transitions.registers, node);
                }
            }
            if (error == TIER1_EMIT_OK)
            {
                error = tier1_emit_phase(&emitted->prepared, &view, &emitted->frame, &transitions, use + 1, &buffer, emitted);
            }
            if (error == TIER1_EMIT_OK && calls)
            {
                // The registers of values that live past the call: their intervals cover its definition phase.
                uint32_t saved = 0;
                for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
                {
                    const uint32_t owner = transitions.owners[reg];
                    const bool live = owner != BUSTER_IR_NONE && owner != node && transitions.registers[owner] == reg && tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
                    saved |= live ? 1u << reg : 0;
                }
                error = tier1_emit_call(plan, node, argument_registers, saved, transitions.registers[node], &buffer, scratch, emitted);
            }
            else if (error == TIER1_EMIT_OK && selected && (op == IR_BR || op == IR_CBR))
            {
                // The terminator, by its block's edges (one for a conditional branch whose successors are one block).
                const uint32_t e_first = input->successor_offsets[b];
                if (input->successor_offsets[b + 1] - e_first == 1)
                {
                    const uint32_t s = input->successors[e_first];
                    if (cfg->edge_placements[e_first] == TIER1_CFG_EDGE_PREDECESSOR)
                    {
                        error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_first], cfg->edge_move_offsets[e_first + 1], &buffer);
                    }
                    if (error == TIER1_EMIT_OK && s != b + 1)
                    {
                        error = tier1_emit_cfg_jump(&buffer, X86_FORM_JMP_REL8, s, pending_branches, pending_goals, &pending, emitted);
                    }
                }
                else
                {
                    // The comparison's records, the selector's (its last two are the conditional jump and the jump),
                    // with its immediate's request.
                    const Tier1SelectRecords selected_records = tier1_select_expand(&holes);
                    const uint32_t compares = selected_records.count >= 2 ? selected_records.count - 2u : 0;
                    error = selected_records.count >= 2 && (!selected_records.requested || selected_records.request < compares) ? error : TIER1_EMIT_INVALID;
                    const uint32_t compare_first = buffer.count;
                    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < compares; r += 1)
                    {
                        error = tier1_emit_append(&buffer, selected_records.records[r]) ? error : TIER1_EMIT_MEMORY;
                    }
                    if (error == TIER1_EMIT_OK)
                    {
                        tier1_select_requests_add(&requests, &selected_records, compare_first, buffer.records);
                        error = tier1_emit_cfg_branch(emitted, b, e_first, selected_records.records[compares].form, &buffer, stub_first, pending_branches, pending_goals, &pending);
                    }
                }
            }
            else if (error == TIER1_EMIT_OK && selected && (plan->patterns[node].rule == TIER1_RULE_DIVIDE || plan->patterns[node].rule == TIER1_RULE_SHIFT))
            {
                error = tier1_emit_cfg_fixed(emitted, &transitions, &plan->patterns[node], node, use, &holes, &buffer);
            }
            else if (error == TIER1_EMIT_OK && selected)
            {
                Tier1SelectRecords selected_records = tier1_select_expand(&holes);
                // A frame object's address, from the frame's base at the object's offset.
                int32_t object_offset = 0;
                if (plan->patterns[node].rule == TIER1_RULE_STACK_ADDRESS)
                {
                    error = selected_records.count == 1 && tier1_emit_cfg_object(emitted, (uint32_t)plan->patterns[node].immediate, &object_offset) ? error : TIER1_EMIT_FRAME;
                    selected_records.records[0].rm = (uint8_t)emitted->frame.base_register;
                    selected_records.records[0].displacement = object_offset;
                }
                // A symbol's address, its reference the record's: a lea of it, or a load of its GOT slot; a thread-local's
                // is the driver's Tier 0's.
                if (plan->patterns[node].rule == TIER1_RULE_SYMBOL_ADDRESS && error == TIER1_EMIT_OK)
                {
                    const uint32_t symbol = (uint32_t)plan->patterns[node].immediate;
                    const uint8_t access = symbol < emitted->symbol_access_count ? emitted->symbol_access[symbol] : BUSTER_TIER1_ACCESS_DIRECT;
                    error = selected_records.count == 1 && emitted->reference_count < emitted->reference_capacity ? error : TIER1_EMIT_INVALID;
                    error = error != TIER1_EMIT_OK || access == BUSTER_TIER1_ACCESS_DIRECT || access == BUSTER_TIER1_ACCESS_GOT ? error : TIER1_EMIT_UNSUPPORTED;
                    selected_records.records[0].form = access == BUSTER_TIER1_ACCESS_GOT ? X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP : selected_records.records[0].form;
                    if (error == TIER1_EMIT_OK)
                    {
                        emitted->references[emitted->reference_count++] = (LinkReference){
                            .instruction = buffer.count,
                            .field = X86_FIXUP_DISPLACEMENT,
                            .kind = access == BUSTER_TIER1_ACCESS_GOT ? BUSTER_LINK_FIXUP_LOAD_GOT : BUSTER_LINK_FIXUP_PC32,
                            .target = symbol,
                            .addend = 0,
                        };
                    }
                }
                const bool returns_here = op == IR_RET;
                error = !returns_here || (selected_records.count != 0 && selected_records.records[selected_records.count - 1].form == X86_FORM_RET) ? error : TIER1_EMIT_INVALID;
                // A return's epilogue goes before its ret, the last record, after any request's record.
                const uint32_t record_first = buffer.count;
                for (uint32_t r = 0; error == TIER1_EMIT_OK && r < selected_records.count; r += 1)
                {
                    if (returns_here && r + 1 == selected_records.count && tier1_frame_emit(&emitted->frame, true, &buffer) != TIER1_FRAME_EMIT_OK)
                    {
                        error = TIER1_EMIT_FRAME;
                    }
                    else if (!tier1_emit_append(&buffer, selected_records.records[r]))
                    {
                        error = TIER1_EMIT_MEMORY;
                    }
                }
                if (error == TIER1_EMIT_OK)
                {
                    tier1_select_requests_add(&requests, &selected_records, record_first, buffer.records);
                }
            }
            if (error == TIER1_EMIT_OK && v != BUSTER_TIER1_CFG_NONE && selected && tier1_select_register_allowed(transitions.registers[node]))
            {
                transitions.owners[transitions.registers[node]] = node;
            }
            if (error != TIER1_EMIT_OK && error_node != 0)
            {
                *error_node = node;
            }
        }
        // The added entry's single edge: its moves, then it falls through into the IR's entry.
        if (error == TIER1_EMIT_OK && b < added && cfg->edge_placements[0] == TIER1_CFG_EDGE_PREDECESSOR)
        {
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[0], cfg->edge_move_offsets[1], &buffer);
        }
    }
    // The pending jumps' targets, now that every block and stub has its first record.
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < pending; k += 1)
    {
        const uint32_t goal = pending_goals[k];
        buffer.targets[pending_branches[k]] = (goal & BUSTER_TIER1_EMIT_CFG_STUB) != 0 ? stub_first[goal & ~BUSTER_TIER1_EMIT_CFG_STUB] : block_first[goal];
    }
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
        emitted->transition_scratch_bytes = scratch->length - scratch_mark;
    }
    return error;
}

// The CFG consumer's whole function: problem, allocation, frame and records, out of line, so that tier1_emit_function's
// straight-line path compiles as it did without it.
static __attribute__((noinline)) Tier1EmitError tier1_emit_cfg_function(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const uint8_t* allowed_forms, Arena* output, Arena* scratch, Tier1EmitResult* emitted, uint32_t* error_node)
{
    emitted->general = true;
    Tier1EmitError error = tier1_emit_cfg_problem(plan, options, output, scratch, emitted);
    if (error == TIER1_EMIT_OK)
    {
        const Tier1CfgError allocated = BUSTER_VECTOR_PATH(options->vector) ? tier1_cfg_allocate(&emitted->cfg_input, output, scratch, &emitted->cfg) : tier1_cfg_allocate_scalar(&emitted->cfg_input, output, scratch, &emitted->cfg);
        error = allocated == TIER1_CFG_OK ? TIER1_EMIT_OK : allocated == TIER1_CFG_CAPACITY ? TIER1_EMIT_MEMORY : allocated == TIER1_CFG_CONSTRAINT ? TIER1_EMIT_ALLOCATION : TIER1_EMIT_INVALID;
        // Without fixed ranges there are no splits; the general class alone has values.
        error = error != TIER1_EMIT_OK || (emitted->cfg.split_count == 0 && (emitted->cfg.temp_slots & ~1u) == 0) ? error : TIER1_EMIT_ALLOCATION;
    }
    if (error == TIER1_EMIT_OK)
    {
        emitted->allocation = emitted->cfg.allocation;
        error = tier1_emit_cfg_frame(plan, options, output, scratch, emitted);
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_records(plan, allowed_forms, BUSTER_VECTOR_PATH(options->vector), output, scratch, emitted, error_node);
    }
    return error;
}
