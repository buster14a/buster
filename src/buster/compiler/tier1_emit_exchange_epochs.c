// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Isolated #2610 exploration. These are the CFG emitter's call-preservation epochs, with each abstract GPR treated
// as caller-saved. A physical permutation selects the columns that actually need saving. This changes no source
// tree and emits no code. The owed/kept/saved and row invalidation operations are the actual emitter's helpers.
// Explicit fixed-sequence saves are excluded: an admissible permutation fixes every register such a sequence uses.

#include "tier1_emit_transition_state.c"

typedef struct Tier1EmitExchangeEpoch Tier1EmitExchangeEpoch;
struct Tier1EmitExchangeEpoch
{
    uint32_t blocks;
    uint32_t* stores;
    uint32_t* loads;
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
    uint32_t nodes;
    // Per-node masks are retained only for this proof harness, to compare instruction origins without guessing.
    uint16_t* node_stores;
    uint16_t* node_loads;
    uint32_t transitions;
    uint32_t carried_blocks;
    uint32_t dropped_definitions;
    uint32_t covered_nodes;
#endif
};

// The state-only portion of tier1_emit_phase's second pass. Spill-slot stamps and the records it emits cannot
// change the owner/register columns. The proposed shared-phase patch uses this exact update in the real emitter.
static void tier1_emit_exchange_epoch_phase(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation,
                                             Tier1EmitTransitions* transitions, uint32_t position)
{
    for (uint32_t e = transitions->offsets[position]; e < transitions->offsets[position + 1]; e += 1)
    {
        const uint32_t f = transitions->fragments[e];
        const uint32_t value = transitions->values[f];
        tier1_emit_transition_state(prepared, allocation, transitions, f, value);
    }
}

static void tier1_emit_exchange_epoch_count(uint32_t* counts, uint32_t mask)
{
    const __m512i previous = _mm512_loadu_si512(counts);
    _mm512_storeu_si512(counts, _mm512_mask_add_epi32(previous, (__mmask16)mask, previous, _mm512_set1_epi32(1)));
}

// Recreate the actual emitter's definition, transition, block-entry and dropped-definition state. Querying an
// allocation location independently at each use is insufficient: covered and dropped nodes retain the emitter's
// register column without establishing a new owner, and a block resets owners but deliberately not that column.
// Retain the result columns in scratch before marking temporary metadata. A scorer's outer scratch mark owns them;
// on refusal the scratch length and caller's result remain unchanged. No allocation or emitted input is modified.
static Tier1EmitError tier1_emit_exchange_epochs(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                 const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                 Arena* scratch, Tier1EmitExchangeEpoch* result)
{
    const uint64_t output_mark = scratch->length;
    const IrFunction* function = plan->function;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = input->block_count;
    const uint32_t added = blocks - function->block_count;
    const uint32_t base = 2 * added;
    const uint64_t row_count = (uint64_t)blocks * 16;
    Tier1EmitResult context = *emitted;
    context.cfg_input = *input;
    context.cfg = *cfg;
    Tier1EmitExchangeEpoch built = { .blocks = blocks };
    Tier1EmitTransitions transitions = { 0 };
    bool ok = blocks >= function->block_count && input->position_count >= base + 2 * (uint64_t)count;
    Tier1EmitError error = ok ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    built.stores = ok ? ir_allocate(scratch, row_count, 4, &ok) : 0;
    built.loads = ok ? ir_allocate(scratch, row_count, 4, &ok) : 0;
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
    built.nodes = count;
    built.node_stores = ok ? ir_allocate(scratch, (uint64_t)count + 1, 2, &ok) : 0;
    built.node_loads = ok ? ir_allocate(scratch, (uint64_t)count + 1, 2, &ok) : 0;
#endif
    const uint64_t scratch_mark = scratch->length;
    uint32_t* exit_rows = ok ? ir_allocate(scratch, row_count, 4, &ok) : 0;
    transitions.offsets = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 2, 4, &ok) : 0;
    transitions.fragments = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.values = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.registers = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    uint32_t* cursors = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok) : 0;
    uint32_t* entry_offsets = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* entry_cursors = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint8_t* dropped = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    uint32_t* entries = 0;
    if (error == TIER1_EMIT_OK)
    {
        memset(built.stores, 0, row_count * 4);
        memset(built.loads, 0, row_count * 4);
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
        memset(built.node_stores, 0, ((uint64_t)count + 1) * 2);
        memset(built.node_loads, 0, ((uint64_t)count + 1) * 2);
#endif
        memset(transitions.offsets, 0, ((uint64_t)input->position_count + 2) * 4);
        memset(transitions.registers, UINT8_MAX, (uint64_t)count + 1);
        memset(entry_offsets, 0, ((uint64_t)blocks + 1) * 4);
    }
    // Identical transition and entry-interval CSR construction to tier1_emit_cfg_records, without encoding/frame data.
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
                entry_cursors[b] = entry_offsets[b];
            }
            entries = ir_allocate(scratch, (uint64_t)entry_offsets[blocks] + 1, 4, &ok);
            error = ok ? error : TIER1_EMIT_MEMORY;
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
        {
            const uint32_t node = emitted->value_nodes[cfg->intervals.values[i]];
            const uint32_t start = cfg->intervals.starts[i];
            for (uint32_t f = allocation->fragment_offsets[i] + 1; error == TIER1_EMIT_OK && f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                const uint32_t q = allocation->starts[f];
                const uint8_t kind = allocation->kinds[f];
                const uint8_t previous = allocation->kinds[f - 1];
                error = q < input->position_count && (kind != BUSTER_TIER1_ALLOCATE_REGISTER || (previous != BUSTER_TIER1_ALLOCATE_REGISTER && ((q - base) & 1) == 0)) ? error : TIER1_EMIT_UNSUPPORTED;
                if (error == TIER1_EMIT_OK && pass == 0)
                {
                    transitions.offsets[q + 1] += 1;
                }
                else if (error == TIER1_EMIT_OK)
                {
                    transitions.fragments[cursors[q]] = f;
                    cursors[q] += 1;
                }
                transitions.values[f] = node;
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
                built.transitions += pass != 0;
#endif
            }
            uint32_t b = tier1_cfg_block_at(input, cfg, start);
            b += input->block_starts[b] < start ? 1 : 0;
            for (; error == TIER1_EMIT_OK && b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
            {
                if (pass == 0)
                {
                    entry_offsets[b + 1] += 1;
                }
                else
                {
                    entries[entry_cursors[b]] = i;
                    entry_cursors[b] += 1;
                }
            }
        }
    }
    for (uint32_t k = count; error == TIER1_EMIT_OK && k > 0; k -= 1)
    {
        const uint32_t node = k - 1;
        const uint32_t v = emitted->node_values[node];
        const bool selected = !plan->covered[node] && function->op[node] != IR_PHI && !tier1_select_entry_parameter(function, node);
        dropped[node] = selected && v != BUSTER_TIER1_CFG_NONE && tier1_emit_cfg_dead_definition(&context, v, base + 2 * node + 1, dropped, base);
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
        built.dropped_definitions += dropped[node] != 0;
        built.covered_nodes += plan->covered[node] != 0;
#endif
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t entry = input->block_starts[b];
        const uint32_t degree = input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
        const uint32_t only = degree == 1 ? input->predecessors[input->predecessor_offsets[b]] : UINT32_MAX;
        for (uint32_t location = 0; location < BUSTER_TIER1_EMIT_LOCATIONS; location += 1)
        {
            transitions.owners[location] = BUSTER_IR_NONE;
        }
        for (uint32_t x = entry_offsets[b]; error == TIER1_EMIT_OK && x < entry_offsets[b + 1]; x += 1)
        {
            const uint32_t v = cfg->intervals.values[entries[x]];
            const uint32_t node = emitted->value_nodes[v];
            uint32_t location = 0;
            uint8_t kind = 0;
            error = tier1_cfg_entry_location(cfg, v, entry, &location, &kind) ? error : TIER1_EMIT_ALLOCATION;
            const bool held = error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REGISTER && tier1_emit_cfg_register_location(location);
            transitions.registers[node] = held ? (uint8_t)location : UINT8_MAX;
            if (held)
            {
                transitions.owners[location] = node;
            }
        }
        Tier1EmitSaves saves;
        tier1_emit_saves_reset(&saves);
        if (only < b)
        {
            memcpy(saves.rows, exit_rows + (uint64_t)only * 16, sizeof(saves.rows));
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
            built.carried_blocks += 1;
#endif
        }
        const uint32_t first = b < added ? 0 : function->block_offsets[b - added];
        const uint32_t end = b < added ? 0 : function->block_offsets[b - added + 1];
        for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
        {
            const uint32_t op = function->op[node];
            const uint32_t use = base + 2 * node;
            const uint32_t v = emitted->node_values[node];
            const bool selected = !plan->covered[node] && op != IR_PHI && !tier1_select_entry_parameter(function, node);
            const bool calls = tier1_select_function_call(function, node);
            const bool leaves = op == IR_RET || op == IR_UNREACHABLE;
            const uint32_t interval = v != BUSTER_TIER1_CFG_NONE && selected ? tier1_cfg_interval_at(cfg, v, use + 1) : BUSTER_TIER1_CFG_NONE;
            if (saves.owed != 0)
            {
                const bool general = !calls && interval != BUSTER_TIER1_CFG_NONE && tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_GPR;
                const uint8_t defined = general ? allocation->registers[allocation->fragment_offsets[interval]] : UINT8_MAX;
                const uint32_t repaid = tier1_emit_owed(plan, &emitted->prepared, allocation, &transitions, node, use, defined, selected, node + 1 == end && !leaves, saves.owed);
                tier1_emit_exchange_epoch_count(built.loads + (uint64_t)b * 16, repaid);
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
                built.node_loads[node] = (uint16_t)repaid;
#endif
                saves.owed &= ~repaid;
            }
            tier1_emit_exchange_epoch_phase(&emitted->prepared, allocation, &transitions, use);
            if (v != BUSTER_TIER1_CFG_NONE && selected)
            {
                error = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == use + 1 ? error : TIER1_EMIT_ALLOCATION;
                if (error == TIER1_EMIT_OK)
                {
                    const uint32_t f = allocation->fragment_offsets[interval];
                    const uint32_t base_location = 32 * tier1_emit_class(&emitted->prepared, node);
                    transitions.registers[node] = allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER ? (uint8_t)(base_location + allocation->registers[f]) : UINT8_MAX;
                }
            }
            if (error == TIER1_EMIT_OK)
            {
                tier1_emit_exchange_epoch_phase(&emitted->prepared, allocation, &transitions, use + 1);
            }
            if (error == TIER1_EMIT_OK && calls)
            {
                uint32_t saved = 0;
                for (uint32_t reg = 0; reg < 16; reg += 1)
                {
                    const uint32_t owner = transitions.owners[reg];
                    const bool live = owner != BUSTER_IR_NONE && owner != node && transitions.registers[owner] == reg &&
                                      tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
                    saved |= live ? 1u << reg : 0;
                }
                const uint32_t stored = saved & ~tier1_emit_kept(&saves, &transitions, saved);
                tier1_emit_exchange_epoch_count(built.stores + (uint64_t)b * 16, stored);
#if defined(BUSTER_TIER1_EXCHANGE_EPOCH_PROOF)
                built.node_stores[node] = (uint16_t)stored;
#endif
                tier1_emit_saved(&saves, &transitions, saved);
            }
            else if (error == TIER1_EMIT_OK && selected && plan->patterns[node].rule == TIER1_RULE_ASM)
            {
                tier1_emit_fixed_rows(&saves, tier1_select_node_fixed(function, &plan->patterns[node]));
            }
            else if (error == TIER1_EMIT_OK && selected && tier1_select_fixed_registers(plan->patterns[node].rule) != 0)
            {
                tier1_emit_fixed_rows(&saves, tier1_select_fixed_registers(plan->patterns[node].rule));
            }
            if (error == TIER1_EMIT_OK && v != BUSTER_TIER1_CFG_NONE && selected && dropped[node] == 0 && tier1_emit_cfg_holds(&context, node, transitions.registers[node]))
            {
                transitions.owners[transitions.registers[node]] = node;
            }
            saves.owed = leaves ? 0 : saves.owed;
        }
        error = error != TIER1_EMIT_OK || saves.owed == 0 ? error : TIER1_EMIT_INVALID;
        memcpy(exit_rows + (uint64_t)b * 16, saves.rows, sizeof(saves.rows));
    }
    arena_pop_to(scratch, scratch_mark);
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    else
    {
        arena_pop_to(scratch, output_mark);
    }
    return error;
}
