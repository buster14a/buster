// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Costs for the isolated #2610 interval recolor. The allocation remains frozen except for one parameter's register
// and its edge copies. Count actual lazy preservation epochs and exact ABI parallel copies. A uniform CFG model
// weights alternative paths; it is a static estimate, not a profile or a guarantee of dynamic profitability.

#define BUSTER_TIER1_RECOLOR_ARGUMENTS (BUSTER_ABI_INTEGER_ARGUMENTS + 3)

typedef struct Tier1EmitRecolorCall Tier1EmitRecolorCall;
struct Tier1EmitRecolorCall
{
    uint32_t node;
    uint32_t position;
    uint32_t block;
    uint32_t values[BUSTER_TIER1_RECOLOR_ARGUMENTS];
    uint32_t destinations[BUSTER_TIER1_RECOLOR_ARGUMENTS];
    uint32_t count;
};

typedef struct Tier1EmitRecolorModel Tier1EmitRecolorModel;
struct Tier1EmitRecolorModel
{
    const uint64_t* weights;
    const uint64_t* edge_weights;
    const Tier1EmitRecolorCall* calls;
    uint32_t count;
    uint32_t callee;
    uint32_t forbidden;
};

typedef struct Tier1EmitRecolorCost Tier1EmitRecolorCost;
struct Tier1EmitRecolorCost
{
    __int128 preservation;
    __int128 arguments;
    __int128 edges;
};

static __int128 tier1_emit_recolor_total(const Tier1EmitRecolorCost* cost)
{
    return cost->preservation + cost->arguments + cost->edges;
}

// An equal instruction estimate prefers fewer memory preservation records. All candidates additionally keep
// ordinary spills, edge memory traffic, fixed roles, entry binding, and the already paid callee-save set unchanged.
static bool tier1_emit_recolor_better(const Tier1EmitRecolorCost* next, const Tier1EmitRecolorCost* prior)
{
    const __int128 before = tier1_emit_recolor_total(prior);
    const __int128 after = tier1_emit_recolor_total(next);
    return next->preservation < prior->preservation && after <= before;
}

static bool tier1_emit_recolor_model(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                    const Tier1CfgInput* input, Arena* scratch, Tier1EmitRecolorModel* result)
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const uint32_t capacity = emitted->prepared.call_count;
    const uint32_t base = 2 * (input->block_count - function->block_count);
    Tier1EmitRecolorModel built = { .callee = emitted->callee_registers & input->registers[BUSTER_TIER1_ALLOCATE_GPR] };
    bool ok = capacity != 0 && input->block_count != 0;
    for (uint32_t k = 0; ok && k < input->sequence_count; k += 1)
    {
        built.forbidden |= input->sequence_registers[k];
    }
    for (uint32_t k = 0; ok && k < input->fixed.count; k += 1)
    {
        built.forbidden |= input->fixed.classes[k] == BUSTER_TIER1_ALLOCATE_GPR ? input->fixed.registers[k] : 0;
    }
    for (uint32_t node = 0; ok && node < function->instruction_count; node += 1)
    {
        const uint8_t rule = plan->patterns[node].rule;
        ok = plan->covered[node] || (rule != TIER1_RULE_ROW && rule != TIER1_RULE_ASM);
    }
    Tier1EmitRecolorCall* calls = ok ? ir_allocate(scratch, (uint64_t)capacity + 1, sizeof(Tier1EmitRecolorCall), &ok) : 0;
    for (uint32_t node = 0; ok && node < function->instruction_count; node += 1)
    {
        if (tier1_select_function_call(function, node))
        {
            ok = built.count < capacity;
            if (ok)
            {
                Tier1EmitRecolorCall* target = &calls[built.count];
                *target = (Tier1EmitRecolorCall){ .node = node, .position = base + 2 * node + 1 };
                target->block = tier1_cfg_block_of(input, target->position);
                const IrCall* call = &function->calls[function->b[node]];
                const bool indirect = function->op[node] == IR_CALL_INDIRECT;
                const bool pointer = tier1_select_call_destination(function, node) != BUSTER_IR_NONE;
                Tier1SelectPlaces places = { 0 };
                for (uint32_t u = plan->effective_use_offsets[node]; ok && u < plan->effective_use_offsets[node + 1]; u += 1)
                {
                    const uint32_t argument = u - plan->effective_use_offsets[node];
                    const Tier1SelectPlace place = argument < call->argument_count ? tier1_select_argument_place(function, call, argument, &places) :
                                                                                   (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
                    if (place.kind == BUSTER_TIER1_PLACE_GENERAL)
                    {
                        const uint32_t value = emitted->node_values[plan->effective_use_values[u]];
                        const bool callee_argument = indirect && argument == call->argument_count;
                        const bool pointer_argument = pointer && argument == call->argument_count + indirect;
                        ok = target->count < BUSTER_TIER1_RECOLOR_ARGUMENTS && value != BUSTER_TIER1_CFG_NONE &&
                             (argument < call->argument_count || callee_argument || pointer_argument);
                        if (ok)
                        {
                            target->values[target->count] = value;
                            target->destinations[target->count] = argument < call->argument_count ? tier1_select_parameter_registers[place.index] :
                                                                  callee_argument ? tier1_select_callee_register(function, call) : X86_REGISTER_RDI;
                            target->count += 1;
                        }
                    }
                }
                built.count += 1;
            }
        }
    }
    // Hidden helper-call paths need their own argument model before this optional policy may change their operands.
    ok = ok && built.count == capacity;
    uint64_t* weights = 0;
    ok = ok && tier1_emit_exchange_frequency(input, scratch, &weights);
    uint64_t* edges = ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, sizeof(uint64_t), &ok) : 0;
    for (uint32_t b = 0; ok && b < input->block_count; b += 1)
    {
        const uint32_t first = input->successor_offsets[b];
        const uint32_t degree = input->successor_offsets[b + 1] - first;
        const uint64_t share = degree != 0 ? weights[b] / degree : 0;
        const uint32_t remainder = degree != 0 ? (uint32_t)(weights[b] % degree) : 0;
        // The frequency helper starts each rotor at zero and advances it by every distributed remainder. Over all
        // visits, those bonuses are exactly this prefix after the complete equal-share rounds have been counted.
        for (uint32_t j = 0; j < degree; j += 8)
        {
            const uint32_t left = degree - j;
            const __mmask8 lanes = left >= 8 ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
            const __m256i ordinal = _mm256_add_epi32(_mm256_set1_epi32(j), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
            const __mmask8 extra = lanes & _mm256_cmplt_epu32_mask(ordinal, _mm256_set1_epi32(remainder));
            const __m512i value = _mm512_mask_add_epi64(_mm512_set1_epi64(share), extra, _mm512_set1_epi64(share), _mm512_set1_epi64(1));
            _mm512_mask_storeu_epi64(edges + first + j, lanes, value);
        }
    }
    if (ok)
    {
        built.weights = weights;
        built.edge_weights = edges;
        built.calls = calls;
        *result = built;
    }
    else
    {
        arena_pop_to(scratch, mark);
    }
    return ok;
}

static bool tier1_emit_recolor_arguments(const Tier1EmitRecolorCall* call, const Tier1CfgAllocation* cfg, Arena* scratch, uint32_t* records)
{
    const uint64_t mark = scratch->length;
    uint32_t sources[BUSTER_TIER1_RECOLOR_ARGUMENTS];
    bool ok = call->count <= BUSTER_TIER1_RECOLOR_ARGUMENTS;
    for (uint32_t a = 0; ok && a < call->count; a += 1)
    {
        uint8_t kind = 0;
        ok = tier1_cfg_location(cfg, call->values[a], call->position - 1, &sources[a], &kind) &&
             kind == BUSTER_TIER1_ALLOCATE_REGISTER && sources[a] < 16;
    }
    const Tier1ParallelMoves input = {
        .sources = sources,
        .destinations = call->destinations,
        .count = call->count,
        .scratch = BUSTER_TIER1_SELECT_SCRATCH,
    };
    Tier1ResolvedMoves moves = { 0 };
    ok = ok && tier1_resolve_moves(&input, scratch, &moves) == TIER1_ALLOCATION_OK;
    if (ok)
    {
        *records = moves.count;
    }
    arena_pop_to(scratch, mark);
    return ok;
}

// The exact epoch walk is repeated for the candidate: removing a save in one block can change the private row
// inherited by a later block, even though no other allocation fragment moves. Never subtract an old column locally.
static bool tier1_emit_recolor_cost(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const Tier1CfgInput* input,
                                   const Tier1CfgAllocation* cfg, const Tier1EmitRecolorModel* model, Arena* scratch, Tier1EmitRecolorCost* result)
{
    const uint64_t mark = scratch->length;
    Tier1EmitExchangeEpoch epochs = { 0 };
    Tier1EmitRecolorCost cost = { 0 };
    bool ok = tier1_emit_exchange_epochs(plan, emitted, input, cfg, scratch, &epochs) == TIER1_EMIT_OK;
    for (uint32_t b = 0; ok && b < epochs.blocks; b += 1)
    {
        const uint64_t first = (uint64_t)b * 16;
        const __mmask16 callers = (__mmask16)~model->callee;
        const __m512i stores = _mm512_maskz_loadu_epi32(callers, epochs.stores + first);
        const __m512i loads = _mm512_maskz_loadu_epi32(callers, epochs.loads + first);
        const __m512i low = _mm512_add_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(stores)), _mm512_cvtepu32_epi64(_mm512_castsi512_si256(loads)));
        const __m512i high = _mm512_add_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(stores, 1)), _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(loads, 1)));
        const uint64_t records = (uint64_t)_mm512_reduce_add_epi64(_mm512_add_epi64(low, high));
        cost.preservation += (__int128)model->weights[b] * records;
    }
    for (uint32_t k = 0; ok && k < model->count; k += 1)
    {
        uint32_t records = 0;
        ok = tier1_emit_recolor_arguments(&model->calls[k], cfg, scratch, &records);
        cost.arguments += (__int128)model->weights[model->calls[k].block] * records;
    }
    for (uint32_t e = 0; ok && e < input->edge_count; e += 1)
    {
        // The candidate cannot add memory moves or critical-edge stubs. Variable-size recipes are unchanged;
        // their identical contributions cancel. Each added parameter/cycle register copy is exactly one record.
        const uint32_t records = cfg->edge_move_offsets[e + 1] - cfg->edge_move_offsets[e] + (cfg->edge_placements[e] == TIER1_CFG_EDGE_SPLIT);
        cost.edges += (__int128)model->edge_weights[e] * records;
    }
    if (ok)
    {
        *result = cost;
    }
    arena_pop_to(scratch, mark);
    return ok;
}

static bool tier1_emit_recolor_edge_traffic(const Tier1CfgInput* input, const Tier1CfgAllocation* before, const Tier1CfgAllocation* after)
{
    bool ok = (after->temp_slots & ~before->temp_slots) == 0;
    for (uint32_t e = 0; ok && e < input->edge_count; e += 1)
    {
        ok = after->edge_placements[e] != TIER1_CFG_EDGE_SPLIT || before->edge_placements[e] == TIER1_CFG_EDGE_SPLIT;
        uint32_t old_memory = 0;
        uint32_t new_memory = 0;
        uint32_t old_recipes = 0;
        uint32_t new_recipes = 0;
        for (uint32_t m = before->edge_move_offsets[e]; m < before->edge_move_offsets[e + 1]; m += 1)
        {
            const uint32_t source = before->move_sources[m];
            old_recipes += source == BUSTER_TIER1_CFG_REMATERIALIZE;
            old_memory += (source >= BUSTER_TIER1_CFG_SPILL && source != BUSTER_TIER1_CFG_REMATERIALIZE) || before->move_destinations[m] >= BUSTER_TIER1_CFG_SPILL;
        }
        for (uint32_t m = after->edge_move_offsets[e]; m < after->edge_move_offsets[e + 1]; m += 1)
        {
            const uint32_t source = after->move_sources[m];
            new_recipes += source == BUSTER_TIER1_CFG_REMATERIALIZE;
            new_memory += (source >= BUSTER_TIER1_CFG_SPILL && source != BUSTER_TIER1_CFG_REMATERIALIZE) || after->move_destinations[m] >= BUSTER_TIER1_CFG_SPILL;
        }
        ok = ok && new_memory <= old_memory && new_recipes == old_recipes;
    }
    return ok;
}
