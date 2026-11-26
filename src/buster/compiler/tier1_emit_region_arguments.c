// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Frozen argument-fragment identities for regional trials. Only a call reading a changed fragment needs its
// exact parallel-copy resolver rerun. ABI destinations and fragment extents/kinds remain unchanged throughout
// the search; no mutable argument-count cache needs to be committed with the allocation.

typedef struct Tier1EmitRegionArguments Tier1EmitRegionArguments;
struct Tier1EmitRegionArguments
{
    const uint32_t* fragments;
    const uint32_t* offsets;
    const uint32_t* calls;
    uint32_t fragment_count;
    uint32_t call_count;
};

static uint64_t tier1_emit_region_arguments_bytes(const Tier1CfgAllocation* cfg, const Tier1EmitRecolorModel* model)
{
    const uint64_t slots = (uint64_t)model->count * BUSTER_TIER1_RECOLOR_ARGUMENTS;
    return slots <= UINT32_MAX ? 4 * ((uint64_t)cfg->allocation.fragment_count + 1) + 8 * (slots + 1) + 3 * 63 : 0;
}

// All retained columns are allocated before temporary cursors, so output and scratch may be the same arena.
// Preparation charges structural visits before the bounded batches; the caller's overall work limit is shared.
static Tier1EmitError tier1_emit_region_arguments_prepare(const Tier1CfgAllocation* cfg, const Tier1EmitRecolorModel* model,
                                                         Arena* output, Arena* scratch, uint64_t* work,
                                                         Tier1EmitRegionArguments* result)
{
    const uint64_t output_mark = output->length;
    const uint32_t fragments = cfg->allocation.fragment_count;
    const uint64_t slots = (uint64_t)model->count * BUSTER_TIER1_RECOLOR_ARGUMENTS;
    Tier1EmitError error = slots <= UINT32_MAX ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_recolor_charge((uint64_t)fragments * 2 + slots * 4 + model->count, work) ? error : TIER1_EMIT_LIMIT;
    }
    bool ok = true;
    uint32_t* by_call = error == TIER1_EMIT_OK ? ir_allocate(output, slots + 1, 4, &ok) : 0;
    uint32_t* offsets = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)fragments + 1, 4, &ok) : 0;
    uint32_t* calls = error == TIER1_EMIT_OK && ok ? ir_allocate(output, slots + 1, 4, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    const uint64_t scratch_mark = scratch->length;
    uint32_t* cursors = error == TIER1_EMIT_OK ? ir_allocate(scratch, (uint64_t)fragments + 1, 4, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < model->count; k += 1)
    {
        const Tier1EmitRecolorCall* call = &model->calls[k];
        error = call->count <= BUSTER_TIER1_RECOLOR_ARGUMENTS && call->position != 0 ? error : TIER1_EMIT_INVALID;
        // A call has at most nine ordered GPR operands. Duplicate sources are intentionally retained for the
        // existing parallel resolver; the reverse index below deduplicates calls only when selecting work.
        for (uint32_t a = 0; error == TIER1_EMIT_OK && a < call->count; a += 1)
        {
            const uint32_t interval = tier1_cfg_interval_at(cfg, call->values[a], call->position - 1);
            const uint32_t fragment = interval != BUSTER_TIER1_CFG_NONE ? tier1_cfg_interval_fragment(cfg, interval, call->position - 1) : BUSTER_TIER1_CFG_NONE;
            const bool found = fragment < fragments && cfg->intervals.classes[interval] == BUSTER_TIER1_ALLOCATE_GPR &&
                               cfg->allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER && cfg->allocation.registers[fragment] < 16;
            error = found ? error : TIER1_EMIT_UNSUPPORTED;
            if (found)
            {
                by_call[(uint64_t)k * BUSTER_TIER1_RECOLOR_ARGUMENTS + a] = fragment;
                offsets[fragment + 1] += 1;
            }
        }
    }
    uint32_t total = 0;
    const __m512i lanes_index = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint32_t f = 0; error == TIER1_EMIT_OK && f < fragments; f += 16)
    {
        const uint32_t left = fragments - f;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        __m512i values = _mm512_maskz_loadu_epi32(lanes, offsets + f + 1);
        values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_sub_epi32(lanes_index, _mm512_set1_epi32(1)), values));
        values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_sub_epi32(lanes_index, _mm512_set1_epi32(2)), values));
        values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_sub_epi32(lanes_index, _mm512_set1_epi32(4)), values));
        values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_sub_epi32(lanes_index, _mm512_set1_epi32(8)), values));
        values = _mm512_add_epi32(values, _mm512_set1_epi32((int)total));
        _mm512_mask_storeu_epi32(offsets + f + 1, lanes, values);
        total = _mm512_mask_reduce_max_epu32(lanes, values);
    }
    if (error == TIER1_EMIT_OK)
    {
        memcpy(cursors, offsets, ((uint64_t)fragments + 1) * 4);
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < model->count; k += 1)
    {
        for (uint32_t a = 0; a < model->calls[k].count; a += 1)
        {
            const uint32_t fragment = by_call[(uint64_t)k * BUSTER_TIER1_RECOLOR_ARGUMENTS + a];
            calls[cursors[fragment]++] = k;
        }
    }
    arena_pop_to(scratch, scratch_mark);
    if (error == TIER1_EMIT_OK)
    {
        *result = (Tier1EmitRegionArguments){
            .fragments = by_call,
            .offsets = offsets,
            .calls = calls,
            .fragment_count = fragments,
            .call_count = model->count,
        };
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    return error;
}

static bool tier1_emit_region_argument_records(const Tier1EmitRegionArguments* arguments, const Tier1EmitRecolorModel* model,
                                               const Tier1CfgAllocation* cfg, uint32_t index, Arena* scratch, uint32_t* records)
{
    const uint64_t mark = scratch->length;
    const Tier1EmitRecolorCall* call = &model->calls[index];
    uint32_t sources[BUSTER_TIER1_RECOLOR_ARGUMENTS];
    bool ok = call->count <= BUSTER_TIER1_RECOLOR_ARGUMENTS;
    for (uint32_t a = 0; ok && a < call->count; a += 1)
    {
        const uint32_t fragment = arguments->fragments[(uint64_t)index * BUSTER_TIER1_RECOLOR_ARGUMENTS + a];
        sources[a] = cfg->allocation.registers[fragment];
        ok = sources[a] < 16;
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

// Add exact old-minus-new argument records to the caller's zero-initialized block gains. Call selection follows
// changed fragment identities, including duplicate operands and indirect-call/hidden-pointer destinations.
static Tier1EmitError tier1_emit_region_argument_gains(const Tier1EmitRegionArguments* arguments, const Tier1EmitRecolorModel* model,
                                                      const Tier1CfgAllocation* before, const Tier1CfgAllocation* after,
                                                      const uint32_t* changed, uint32_t changed_count, Arena* scratch,
                                                      uint64_t* work, int64_t* block_gains, __int128* weighted_gain)
{
    const uint64_t mark = scratch->length;
    bool ok = true;
    Tier1EmitError error = tier1_emit_recolor_charge((uint64_t)changed_count + model->count, work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    uint8_t* marked = error == TIER1_EMIT_OK ? ir_allocate(scratch, (uint64_t)model->count + 1, 1, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    for (uint32_t f = 0; error == TIER1_EMIT_OK && f < changed_count; f += 1)
    {
        const uint32_t fragment = changed[f];
        error = fragment < arguments->fragment_count ? error : TIER1_EMIT_INVALID;
        if (error == TIER1_EMIT_OK)
        {
            const uint32_t first = arguments->offsets[fragment];
            const uint32_t past = arguments->offsets[fragment + 1];
            error = tier1_emit_recolor_charge(past - first, work) ? error : TIER1_EMIT_LIMIT;
            for (uint32_t c = first; error == TIER1_EMIT_OK && c < past; c += 1)
            {
                marked[arguments->calls[c]] = 1;
            }
        }
    }
    __int128 gain = 0;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < model->count; k += 16)
    {
        const uint32_t left = model->count - k;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        uint32_t pending = lanes & _mm_cmpneq_epi8_mask(_mm_maskz_loadu_epi8(lanes, marked + k), _mm_setzero_si128());
        while (error == TIER1_EMIT_OK && pending != 0)
        {
            const uint32_t index = k + (uint32_t)__builtin_ctz(pending);
            pending &= pending - 1;
            const uint64_t count = model->calls[index].count;
            error = tier1_emit_recolor_charge(2 * (count * count + count + 1), work) ? error : TIER1_EMIT_LIMIT;
            uint32_t old_records = 0;
            uint32_t new_records = 0;
            if (error == TIER1_EMIT_OK)
            {
                ok = tier1_emit_region_argument_records(arguments, model, before, index, scratch, &old_records) &&
                     tier1_emit_region_argument_records(arguments, model, after, index, scratch, &new_records);
                error = ok ? error : TIER1_EMIT_MEMORY;
            }
            if (error == TIER1_EMIT_OK)
            {
                const int64_t delta = (int64_t)old_records - new_records;
                const uint32_t block = model->calls[index].block;
                block_gains[block] += delta;
                gain += (__int128)model->weights[block] * delta;
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *weighted_gain = gain;
    }
    arena_pop_to(scratch, mark);
    return error;
}
