// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Score disjoint global GPR exchanges without changing fragments, spill places or CFG conventions. Call preservation
// uses the emitter's actual lazy save/restore epochs, and ABI shuffles use its parallel-move resolver. Uniform CFG
// flow weights the costs on alternative paths; entry copies are charged once. The flow is a static heuristic, not a
// profile or a guarantee about the paths a program executes. An unsupported or unbounded estimate keeps the allocation.

#define BUSTER_TIER1_EXCHANGE_ARGUMENTS (BUSTER_ABI_INTEGER_ARGUMENTS + 3)

typedef struct Tier1EmitWeightedCall Tier1EmitWeightedCall;
struct Tier1EmitWeightedCall
{
    uint32_t node;
    uint32_t position;
    uint32_t block;
    uint32_t source[BUSTER_TIER1_EXCHANGE_ARGUMENTS];
    uint32_t destination[BUSTER_TIER1_EXCHANGE_ARGUMENTS];
    uint32_t count;
    uint32_t moves;
};

// The emitter's same bounded parallel-move resolver, including cycles and duplicate sources. Only source
// locations change: ABI destinations are fixed. The surrounding scratch allocations remain intact.
static bool tier1_emit_exchange_weighted_moves(const uint32_t* sources, const uint32_t* destinations, uint32_t count, const uint8_t permutation[16],
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

// A complete mapping's predicted saving over the unchanged allocation, in Q32 records per function invocation.
// The wide accumulator preserves fractional path costs without overflowing when a loop executes repeatedly.
static bool tier1_emit_exchange_weighted_score(const Tier1EmitWeightedCall* calls, uint32_t count, uint32_t callee,
                                      const uint64_t* weights, const __int128 columns[16],
                                      const uint32_t* entry_sources, const uint32_t* entry_destinations, uint32_t entry_count, uint32_t entry_moves,
                                      const uint8_t permutation[16], Arena* scratch, __int128* score)
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
    bool ok = tier1_emit_exchange_weighted_moves(entry_sources, mapped_entry, entry_count, identity, scratch, &next_entry);
    *score = ((__int128)entry_moves - next_entry) * (UINT64_C(1) << 32);
    for (uint32_t r = 0; r < 16; r += 1)
    {
        const int32_t before = (callee & (1u << r)) == 0;
        const int32_t after = (callee & (1u << permutation[r])) == 0;
        *score += columns[r] * (before - after);
    }
    for (uint32_t k = 0; ok && k < count; k += 1)
    {
        uint32_t moves = 0;
        ok = tier1_emit_exchange_weighted_moves(calls[k].source, calls[k].destination, calls[k].count, permutation, scratch, &moves);
        *score += (__int128)weights[calls[k].block] * ((int64_t)calls[k].moves - moves);
    }
    return ok;
}

// Identity means no profitable exchange. False reports an unavailable estimate; all caller-owned inputs
// remain unchanged and scratch is restored on every path. At most six disjoint caller/callee pairs can be selected.
static bool tier1_emit_exchange_weighted_cost(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const Tier1CfgInput* input,
                                     const Tier1CfgAllocation* cfg, Arena* scratch, uint8_t permutation[16])
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
    const uint32_t trace_nodes = trace_env != 0 ? (uint32_t)strtoul(trace_env, 0, 0) : 0;
    const bool trace = trace_env != 0 && (trace_nodes == 0 || trace_nodes == function->instruction_count);
    for (uint32_t r = 0; r < 16; r += 1)
    {
        permutation[r] = (uint8_t)r;
    }
    uint32_t forbidden = 0;
    bool rows = false;
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
        // Intrinsic rows choose temporaries from a physical caller-saved pool. Exchanging its roles can change
        // admission even when ordinary fragment interference is unchanged; retain the original allocation.
        rows |= !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ROW;
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
    bool supported = function->block_count != 0 && cfg->intervals.hints == 0 && !rows;
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
    if (trace)
    {
        fprintf(stderr, "WEIGHTED eligibility instructions=%u incoming=%x preserved=%x forbidden=%x rows=%u calls=%u supported=%u\n", function->instruction_count, incoming, preserved, forbidden, rows, capacity, supported);
    }
    Tier1EmitWeightedCall* calls = supported ? ir_allocate(scratch, (uint64_t)capacity + 1, sizeof(Tier1EmitWeightedCall), &ok) : 0;
    uint32_t count = 0;
    const uint32_t base = 2 * (input->block_count - function->block_count);
    for (uint32_t node = 0; supported && ok && node < function->instruction_count; node += 1)
    {
        if (tier1_select_function_call(function, node))
        {
            supported = count < capacity;
            if (supported)
            {
                Tier1EmitWeightedCall* target = &calls[count];
                *target = (Tier1EmitWeightedCall){ .node = node, .position = base + 2 * node + 1 };
                target->block = tier1_cfg_block_at(input, cfg, target->position);
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
                ok = !supported || tier1_emit_exchange_weighted_moves(target->source, target->destination, target->count, permutation, scratch, &target->moves);
                count += 1;
            }
        }
    }
    uint32_t entry_moves = 0;
    ok = !supported || !ok ? ok : tier1_emit_exchange_weighted_moves(entry_sources, entry_destinations, entry_count, permutation, scratch, &entry_moves);
    Tier1EmitExchangeEpoch epochs = { 0 };
    uint64_t* weights = 0;
    if (supported && ok)
    {
        const Tier1EmitError epoch_error = tier1_emit_exchange_epochs(plan, emitted, input, cfg, scratch, &epochs);
        const bool frequency_ok = epoch_error == TIER1_EMIT_OK && tier1_emit_exchange_frequency(input, scratch, &weights);
        ok = epoch_error == TIER1_EMIT_OK && frequency_ok;
        if (trace)
        {
            fprintf(stderr, "WEIGHTED model instructions=%u epoch_error=%u frequency_ok=%u blocks=%u\n", function->instruction_count, epoch_error, frequency_ok, epochs.blocks);
        }
    }
    __int128 columns[16] = { 0 };
    for (uint32_t b = 0; supported && ok && b < epochs.blocks; b += 1)
    {
        for (uint32_t r = 0; r < 16; r += 1)
        {
            const uint64_t cell = (uint64_t)b * 16 + r;
            columns[r] += (__int128)weights[b] * ((uint64_t)epochs.stores[cell] + epochs.loads[cell]);
        }
    }
    if (trace && supported && ok)
    {
        for (uint32_t r = 0; r < 16; r += 1)
        {
            fprintf(stderr, "WEIGHTED column instructions=%u register=%u cost=%.12Lf\n", function->instruction_count, r, (long double)columns[r] / (UINT64_C(1) << 32));
        }
    }
    __int128 score = 0;
    for (uint32_t selected = 0; supported && ok && selected < BUSTER_ABI_INTEGER_ARGUMENTS && incoming != 0 && preserved != 0; selected += 1)
    {
        __int128 best = score;
        uint32_t chosen_caller = 16;
        uint32_t chosen_callee = 16;
        for (uint32_t from = incoming; ok && from != 0; from &= from - 1)
        {
            const uint32_t caller = (uint32_t)__builtin_ctz(from);
            for (uint32_t to = preserved; ok && to != 0; to &= to - 1)
            {
                const uint32_t saved = (uint32_t)__builtin_ctz(to);
                uint8_t candidate[16];
                memcpy(candidate, permutation, sizeof(candidate));
                candidate[caller] = (uint8_t)saved;
                candidate[saved] = (uint8_t)caller;
                __int128 candidate_score = 0;
                ok = tier1_emit_exchange_weighted_score(calls, count, callee, weights, columns, entry_sources, entry_destinations, entry_count, entry_moves,
                                              candidate, scratch, &candidate_score);
                if (trace)
                {
                    fprintf(stderr, "WEIGHTED pair instructions=%u caller=%u callee=%u selected=%u ok=%u score=%.12Lf prior=%.12Lf\n", function->instruction_count, caller, saved, selected, ok, (long double)candidate_score / (UINT64_C(1) << 32), (long double)score / (UINT64_C(1) << 32));
                }
                if (ok && candidate_score > best)
                {
                    best = candidate_score;
                    chosen_caller = caller;
                    chosen_callee = saved;
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
            if (trace)
            {
                fprintf(stderr, "WEIGHTED chosen instructions=%u caller=%u callee=%u score=%.12Lf\n", function->instruction_count, chosen_caller, chosen_callee, (long double)score / (UINT64_C(1) << 32));
            }
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
