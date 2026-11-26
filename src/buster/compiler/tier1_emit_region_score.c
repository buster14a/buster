// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact changing-record deltas for a scoring-only regional proposal. The accepted epoch cache supplies C/R;
// changed argument fragments select exact ABI resolutions; sparse reconstructed edges supply copy deltas.
// Two later path certificates use preservation_gains and block_gains respectively, with the same edge_gains.

typedef struct Tier1EmitRegionScore Tier1EmitRegionScore;
struct Tier1EmitRegionScore
{
    Tier1EmitRecolorCost cost;
    int64_t* preservation_gains;
    int64_t* block_gains;
    int64_t* edge_gains;
};

static uint64_t tier1_emit_region_score_bytes(const Tier1CfgInput* input)
{
    return 16 * ((uint64_t)input->block_count + 1) + 8 * ((uint64_t)input->edge_count + 1) + 3 * 63;
}

static uint64_t tier1_emit_region_preservation(const uint32_t* stores, const uint32_t* loads, uint32_t callee)
{
    const __mmask16 callers = (__mmask16)~callee;
    const __m512i c = _mm512_maskz_loadu_epi32(callers, stores);
    const __m512i r = _mm512_maskz_loadu_epi32(callers, loads);
    const __m512i low = _mm512_add_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(c)), _mm512_cvtepu32_epi64(_mm512_castsi512_si256(r)));
    const __m512i high = _mm512_add_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(c, 1)), _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(r, 1)));
    return (uint64_t)_mm512_reduce_add_epi64(_mm512_add_epi64(low, high));
}

// The same conservative control contract as the complete-graph guard, applied to the proposal's changed edges.
// Equal SPLIT bits preserve conditional layout; equal SUCCESSOR flags at indirect blocks preserve any dispatch
// fusion. All other edges and all immutable aggregate edge copies retain their original control behavior.
static bool tier1_emit_region_score_control(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                            uint32_t edge, uint8_t old_place, uint8_t new_place)
{
    const IrFunction* function = plan->function;
    const uint32_t added = input->block_count - function->block_count;
    bool ok = (old_place == TIER1_CFG_EDGE_SPLIT) == (new_place == TIER1_CFG_EDGE_SPLIT);
    const uint32_t target = input->successors[edge];
    if (ok && target >= added)
    {
        const uint32_t block = target - added;
        const uint32_t first = function->block_offsets[block];
        const uint32_t end = function->block_offsets[block + 1];
        const bool indirect = first < end && function->op[end - 1] == IR_BR_INDIRECT;
        ok = !indirect || (old_place == TIER1_CFG_EDGE_SUCCESSOR) == (new_place == TIER1_CFG_EDGE_SUCCESSOR);
    }
    return ok;
}

static Tier1EmitError tier1_emit_region_score_initial(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                      const Tier1EmitRecolorModel* model, const Tier1EmitRegionArguments* arguments,
                                                      const Tier1EmitEpochCache* cache, Arena* scratch, uint64_t* work,
                                                      Tier1EmitRecolorCost* result)
{
    Tier1EmitRecolorCost cost = { 0 };
    Tier1EmitError error = cache->blocks == input->block_count && arguments->call_count == model->count ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_recolor_charge((uint64_t)input->block_count * 32 + input->edge_count, work) ? error : TIER1_EMIT_LIMIT;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < input->block_count; b += 1)
    {
        const uint64_t row = (uint64_t)b * 16;
        const uint64_t records = tier1_emit_region_preservation(cache->stores + row, cache->loads + row, model->callee);
        cost.preservation += (__int128)model->weights[b] * records;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < model->count; k += 1)
    {
        const uint64_t count = model->calls[k].count;
        error = tier1_emit_recolor_charge(count * count + count + 1, work) ? error : TIER1_EMIT_LIMIT;
        uint32_t records = 0;
        if (error == TIER1_EMIT_OK)
        {
            error = tier1_emit_region_argument_records(arguments, model, cfg, k, scratch, &records) ? error : TIER1_EMIT_MEMORY;
        }
        if (error == TIER1_EMIT_OK)
        {
            cost.arguments += (__int128)model->weights[model->calls[k].block] * records;
        }
    }
    for (uint32_t e = 0; error == TIER1_EMIT_OK && e < input->edge_count; e += 1)
    {
        const uint64_t records = (uint64_t)cfg->edge_move_offsets[e + 1] - cfg->edge_move_offsets[e] + (cfg->edge_placements[e] == TIER1_CFG_EDGE_SPLIT);
        cost.edges += (__int128)model->edge_weights[e] * records;
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = cost;
    }
    return error;
}

// Output and scratch may be the same arena: all retained gains precede the nested resolver scratch mark. On
// failure no result is published and output is restored. The accepted allocation/cache are always read-only.
static Tier1EmitError tier1_emit_region_score_trial(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                    const Tier1CfgAllocation* before, const Tier1EmitRegionProposal* proposal,
                                                    const Tier1EmitRecolorModel* model, const Tier1EmitRegionArguments* arguments,
                                                    const Tier1EmitEpochCache* cache, const Tier1EmitEpochTrial* epochs,
                                                    const Tier1EmitRecolorCost* current_cost, Arena* output, Arena* scratch,
                                                    uint64_t* work, Tier1EmitRegionScore* result)
{
    const uint64_t mark = output->length;
    Tier1EmitRegionScore built = { .cost = *current_cost };
    bool ok = true;
    const bool valid = epochs->cache == cache && epochs->generation == cache->generation && cache->blocks == input->block_count &&
                       arguments->call_count == model->count && arguments->fragment_count == before->allocation.fragment_count &&
                       proposal->edge_count <= input->edge_count && (proposal->temp_slots & ~before->temp_slots) == 0;
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_recolor_charge((uint64_t)input->block_count * 2 + input->edge_count + (uint64_t)epochs->block_count * 64 + proposal->edge_count, work) ? error : TIER1_EMIT_LIMIT;
    }
    built.preservation_gains = error == TIER1_EMIT_OK ? ir_allocate(output, (uint64_t)input->block_count + 1, 8, &ok) : 0;
    built.block_gains = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)input->block_count + 1, 8, &ok) : 0;
    built.edge_gains = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)input->edge_count + 1, 8, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < epochs->block_count; k += 1)
    {
        const uint32_t block = epochs->blocks[k];
        const uint64_t old_row = (uint64_t)block * 16;
        const uint64_t new_row = (uint64_t)k * 16;
        const uint64_t old_records = tier1_emit_region_preservation(cache->stores + old_row, cache->loads + old_row, model->callee);
        const uint64_t new_records = tier1_emit_region_preservation(epochs->stores + new_row, epochs->loads + new_row, model->callee);
        // At most 32 uint32 counters contribute to a block, so each signed difference fits comfortably in int64.
        const int64_t gain = (int64_t)old_records - (int64_t)new_records;
        built.preservation_gains[block] = gain;
        built.block_gains[block] = gain;
        built.cost.preservation -= (__int128)model->weights[block] * gain;
    }
    __int128 argument_gain = 0;
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_region_argument_gains(arguments, model, before, &proposal->cfg, epochs->fragments, epochs->fragment_count,
                                                scratch, work, built.block_gains, &argument_gain);
        built.cost.arguments -= argument_gain;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < proposal->edge_count; k += 1)
    {
        const uint32_t edge = proposal->edges[k];
        const bool ordered = edge < input->edge_count && (k == 0 || proposal->edges[k - 1] < edge);
        const bool traffic = proposal->after_memory[k] <= proposal->before_memory[k] && proposal->after_recipes[k] == proposal->before_recipes[k];
        error = ordered && traffic ? error : TIER1_EMIT_INVALID;
        if (error == TIER1_EMIT_OK)
        {
            error = tier1_emit_region_score_control(plan, input, edge, before->edge_placements[edge], proposal->placements[k]) ? error : TIER1_EMIT_INVALID;
        }
        if (error == TIER1_EMIT_OK)
        {
            const int64_t old_records = (int64_t)before->edge_move_offsets[edge + 1] - before->edge_move_offsets[edge];
            const int64_t new_records = (int64_t)proposal->offsets[k + 1] - proposal->offsets[k];
            const int64_t gain = old_records - new_records;
            built.edge_gains[edge] = gain;
            built.cost.edges -= (__int128)model->edge_weights[edge] * gain;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return error;
}
