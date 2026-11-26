// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Private #2610 experiment: score disjoint global GPR exchanges without changing the allocation. Fragment ranges,
// spill places and CFG conventions stay fixed. The score counts exact ABI register-shuffle records and an estimate
// of two records per live caller across a call. Lazy retained call-save rows can reduce that estimate; it is not a
// profile-independent performance guarantee. Actual generated-program measurement decides whether it pays.

#define BUSTER_TIER1_EXCHANGE_ARGUMENTS (BUSTER_ABI_INTEGER_ARGUMENTS + 3)

typedef struct Tier1EmitExchangeCall Tier1EmitExchangeCall;
struct Tier1EmitExchangeCall
{
    uint32_t node;
    uint32_t position;
    uint32_t source[BUSTER_TIER1_EXCHANGE_ARGUMENTS];
    uint32_t destination[BUSTER_TIER1_EXCHANGE_ARGUMENTS];
    uint32_t count;
    uint32_t moves;
};

// The first call whose definition phase is at or after position.
static uint32_t tier1_emit_exchange_first(const Tier1EmitExchangeCall* calls, uint32_t count, uint32_t position)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = calls[middle].position < position ? middle + 1 : low;
        high = calls[middle].position < position ? high : middle;
    }
    return low;
}

// The emitter's same bounded parallel-move resolver, including cycles and duplicate sources. Only source
// locations change: ABI destinations are fixed. The surrounding scratch allocations remain intact.
static bool tier1_emit_exchange_moves(const uint32_t* sources, const uint32_t* destinations, uint32_t count, const uint8_t permutation[16],
                                      Arena* scratch, uint32_t* records)
{
    const uint64_t mark = scratch->length;
    uint32_t moved[BUSTER_TIER1_EXCHANGE_ARGUMENTS];
    bool ok = count <= BUSTER_TIER1_EXCHANGE_ARGUMENTS;
    for (uint32_t i = 0; ok && i < count; i += 1)
    {
        ok = sources[i] < 16;
        moved[i] = ok ? permutation[sources[i]] : 0;
    }
    const Tier1ParallelMoves input = {
        .sources = moved,
        .destinations = destinations,
        .count = count,
        .scratch = BUSTER_TIER1_SELECT_SCRATCH,
    };
    Tier1ResolvedMoves output = { 0 };
    ok = ok && tier1_resolve_moves(&input, scratch, &output) == TIER1_ALLOCATION_OK;
    *records = ok ? output.count : 0;
    arena_pop_to(scratch, mark);
    return ok;
}

static uint32_t tier1_emit_exchange_mask(uint32_t mask, const uint8_t permutation[16])
{
    uint32_t result = 0;
    for (uint32_t remaining = mask; remaining != 0; remaining &= remaining - 1)
    {
        const uint32_t reg = (uint32_t)__builtin_ctz(remaining);
        result |= 1u << permutation[reg];
    }
    return result;
}

// A displaced callee must never become an additional caller-save demand at a call where the promoted caller was
// absent. Test sixteen call masks at once. Each selected pair is disjoint from the earlier pairs.
static bool tier1_emit_exchange_subset(const uint32_t* live, uint32_t count, uint32_t caller, uint32_t callee)
{
    const __m512i caller_bit = _mm512_set1_epi32(1u << caller);
    const __m512i callee_bit = _mm512_set1_epi32(1u << callee);
    bool subset = true;
    for (uint32_t i = 0; subset && i < count; i += 16)
    {
        const uint32_t remaining = count - i;
        const __mmask16 lanes = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
        const __m512i occupied = _mm512_maskz_loadu_epi32(lanes, live + i);
        const __mmask16 from = _mm512_test_epi32_mask(occupied, caller_bit);
        const __mmask16 to = _mm512_test_epi32_mask(occupied, callee_bit);
        subset = (to & ~from & lanes) == 0;
    }
    return subset;
}

// Score a complete proposed mapping against the unchanged baseline. Entry movement is charged once and every
// call's raw caller-save plus exact argument-shuffle estimate must be no worse at that call. Static totals then
// choose among the admissible mappings; no execution-frequency claim follows from adding those call-site totals.
static bool tier1_emit_exchange_score(const Tier1EmitExchangeCall* calls, const uint32_t* live, uint32_t count, uint32_t callee,
                                      const uint32_t* entry_sources, const uint32_t* entry_destinations, uint32_t entry_count, uint32_t entry_moves,
                                      const uint8_t permutation[16], Arena* scratch, bool* admissible, int64_t* score)
{
    uint32_t mapped_entry[BUSTER_ABI_INTEGER_ARGUMENTS];
    for (uint32_t i = 0; i < entry_count; i += 1)
    {
        mapped_entry[i] = permutation[entry_destinations[i]];
    }
    uint8_t identity[16];
    for (uint32_t r = 0; r < 16; r += 1)
    {
        identity[r] = (uint8_t)r;
    }
    uint32_t next_entry = 0;
    bool ok = tier1_emit_exchange_moves(entry_sources, mapped_entry, entry_count, identity, scratch, &next_entry);
    *admissible = ok;
    *score = (int64_t)entry_moves - next_entry;
    for (uint32_t k = 0; ok && *admissible && k < count; k += 1)
    {
        uint32_t moves = 0;
        ok = tier1_emit_exchange_moves(calls[k].source, calls[k].destination, calls[k].count, permutation, scratch, &moves);
        const uint32_t before = (uint32_t)__builtin_popcount(live[k] & ~callee);
        const uint32_t after = (uint32_t)__builtin_popcount(tier1_emit_exchange_mask(live[k], permutation) & ~callee);
        const int64_t saving = 2 * ((int64_t)before - after) + (int64_t)calls[k].moves - moves;
        *admissible = ok && after <= before && saving >= 0;
        *score += saving;
    }
    return ok;
}

// Identity means no profitable admissible exchange. False reports scratch exhaustion; all caller-owned inputs
// remain unchanged and scratch is restored on every path. At most six disjoint caller/callee pairs can be selected.
static bool tier1_emit_exchange_cost(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const Tier1CfgInput* input,
                                     const Tier1CfgAllocation* cfg, Arena* scratch, uint8_t permutation[16])
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const Tier1Allocation* allocation = &cfg->allocation;
    for (uint32_t r = 0; r < 16; r += 1)
    {
        permutation[r] = (uint8_t)r;
    }
    uint32_t forbidden = 0;
    for (uint32_t k = 0; k < input->sequence_count; k += 1)
    {
        forbidden |= input->sequence_registers[k];
    }
    for (uint32_t f = 0; f < input->fixed.count; f += 1)
    {
        forbidden |= input->fixed.classes[f] == BUSTER_TIER1_ALLOCATE_GPR ? input->fixed.registers[f] : 0;
    }
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        if (!plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM)
        {
            forbidden |= tier1_select_node_fixed(function, &plan->patterns[node]);
        }
    }
    const uint32_t callee = emitted->callee_registers & input->registers[BUSTER_TIER1_ALLOCATE_GPR];
    uint32_t preserved = tier1_emit_cfg_callee_used(cfg, callee) & ~forbidden;
    uint32_t incoming = 0;
    uint32_t entry_sources[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t entry_destinations[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t entry_count = 0;
    bool supported = function->block_count != 0 && cfg->intervals.hints == 0;
    bool ok = true;
    for (uint32_t node = 0; supported && node < function->block_offsets[1]; node += 1)
    {
        if (tier1_select_entry_parameter(function, node))
        {
            const uint32_t value = emitted->node_values[node];
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            if (place.kind == BUSTER_TIER1_PLACE_GENERAL && value != BUSTER_TIER1_CFG_NONE)
            {
                uint32_t location = 0;
                uint8_t kind = 0;
                supported = entry_count < BUSTER_ABI_INTEGER_ARGUMENTS && tier1_cfg_location(cfg, value, 0, &location, &kind) &&
                            kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16;
                if (supported)
                {
                    const uint32_t reg = tier1_select_parameter_registers[place.index];
                    entry_sources[entry_count] = reg;
                    entry_destinations[entry_count] = location;
                    entry_count += 1;
                    incoming |= reg == location ? 1u << reg : 0;
                }
            }
        }
    }
    incoming &= input->registers[BUSTER_TIER1_ALLOCATE_GPR] & ~callee & ~forbidden;
    const uint32_t capacity = emitted->prepared.call_count;
    supported = supported && incoming != 0 && preserved != 0 && capacity != 0;
    Tier1EmitExchangeCall* calls = supported ? ir_allocate(scratch, (uint64_t)capacity + 1, sizeof(Tier1EmitExchangeCall), &ok) : 0;
    uint32_t* live = supported && ok ? ir_allocate(scratch, (uint64_t)capacity + 1, sizeof(uint32_t), &ok) : 0;
    uint32_t count = 0;
    const uint32_t base = 2 * (input->block_count - function->block_count);
    for (uint32_t node = 0; supported && ok && node < function->instruction_count; node += 1)
    {
        if (tier1_select_function_call(function, node))
        {
            supported = count < capacity;
            if (supported)
            {
                Tier1EmitExchangeCall* target = &calls[count];
                *target = (Tier1EmitExchangeCall){ .node = node, .position = base + 2 * node + 1 };
                live[count] = 0;
                const IrCall* call = &function->calls[function->b[node]];
                const bool indirect = function->op[node] == IR_CALL_INDIRECT;
                const bool pointer = tier1_select_call_destination(function, node) != BUSTER_IR_NONE;
                Tier1SelectPlaces places = { 0 };
                for (uint32_t u = plan->effective_use_offsets[node]; supported && u < plan->effective_use_offsets[node + 1]; u += 1)
                {
                    const uint32_t argument = u - plan->effective_use_offsets[node];
                    const Tier1SelectPlace place = argument < call->argument_count ? tier1_select_argument_place(function, call, argument, &places) :
                                                                                   (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
                    const bool named = place.kind == BUSTER_TIER1_PLACE_GENERAL;
                    if (named)
                    {
                        const uint32_t value = emitted->node_values[plan->effective_use_values[u]];
                        uint32_t location = 0;
                        uint8_t kind = 0;
                        supported = target->count < BUSTER_TIER1_EXCHANGE_ARGUMENTS && value != BUSTER_TIER1_CFG_NONE &&
                                    tier1_cfg_location(cfg, value, target->position - 1, &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16;
                        const bool callee_argument = indirect && argument == call->argument_count;
                        const bool pointer_argument = pointer && argument == call->argument_count + indirect;
                        supported = supported && (argument < call->argument_count || callee_argument || pointer_argument);
                        if (supported)
                        {
                            target->source[target->count] = location;
                            target->destination[target->count] = argument < call->argument_count ? tier1_select_parameter_registers[place.index] :
                                                                  callee_argument ? tier1_select_callee_register(function, call) : X86_REGISTER_RDI;
                            target->count += 1;
                        }
                    }
                }
                ok = !supported || tier1_emit_exchange_moves(target->source, target->destination, target->count, permutation, scratch, &target->moves);
                count += 1;
            }
        }
    }
    // A fragment contributes precisely at calls whose definition phase it covers, except its own call result.
    for (uint32_t i = 0; supported && ok && i < cfg->intervals.count; i += 1)
    {
        if (cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR)
        {
            const uint32_t node = emitted->value_nodes[cfg->intervals.values[i]];
            for (uint32_t f = allocation->fragment_offsets[i]; f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                if (allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER)
                {
                    const uint32_t reg = allocation->registers[f];
                    const uint32_t first = tier1_emit_exchange_first(calls, count, allocation->starts[f]);
                    for (uint32_t k = first; k < count && calls[k].position < allocation->ends[f]; k += 1)
                    {
                        live[k] |= calls[k].node != node ? 1u << reg : 0;
                    }
                }
            }
        }
    }
    uint32_t entry_moves = 0;
    ok = !supported || !ok ? ok : tier1_emit_exchange_moves(entry_sources, entry_destinations, entry_count, permutation, scratch, &entry_moves);
    int64_t score = 0;
    for (uint32_t selected = 0; supported && ok && selected < BUSTER_ABI_INTEGER_ARGUMENTS && incoming != 0 && preserved != 0; selected += 1)
    {
        int64_t best = score;
        uint32_t chosen_caller = 16;
        uint32_t chosen_callee = 16;
        for (uint32_t from = incoming; ok && from != 0; from &= from - 1)
        {
            const uint32_t caller = (uint32_t)__builtin_ctz(from);
            for (uint32_t to = preserved; ok && to != 0; to &= to - 1)
            {
                const uint32_t saved = (uint32_t)__builtin_ctz(to);
                if (tier1_emit_exchange_subset(live, count, caller, saved))
                {
                    uint8_t candidate[16];
                    memcpy(candidate, permutation, sizeof(candidate));
                    candidate[caller] = (uint8_t)saved;
                    candidate[saved] = (uint8_t)caller;
                    bool admissible = false;
                    int64_t candidate_score = 0;
                    ok = tier1_emit_exchange_score(calls, live, count, callee, entry_sources, entry_destinations, entry_count, entry_moves,
                                                  candidate, scratch, &admissible, &candidate_score);
                    if (ok && admissible && candidate_score > best)
                    {
                        best = candidate_score;
                        chosen_caller = caller;
                        chosen_callee = saved;
                    }
                }
            }
        }
        if (chosen_caller < 16 && chosen_callee < 16)
        {
            permutation[chosen_caller] = (uint8_t)chosen_callee;
            permutation[chosen_callee] = (uint8_t)chosen_caller;
            incoming &= ~(1u << chosen_caller);
            preserved &= ~(1u << chosen_callee);
            score = best;
        }
        else
        {
            supported = false;
        }
    }
    if (!ok)
    {
        for (uint32_t r = 0; r < 16; r += 1)
        {
            permutation[r] = (uint8_t)r;
        }
    }
    arena_pop_to(scratch, mark);
    return ok;
}
