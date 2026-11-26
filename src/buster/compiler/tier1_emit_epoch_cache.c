// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact cached replay of the qualified call-preservation epoch walk. Only physical GPR fragment registers may change;
// ranges, kinds, classes, uses, recipes, CFG/phi identity and selected IR remain immutable for this cache's lifetime.
// Allowed masks may widen: neither these epochs nor the cached expressions inspect them. Include after the full
// tier1_emit_exchange_epochs module, which remains the differential oracle and optional fallback.

#define BUSTER_TIER1_EPOCH_SELECTED 1u
#define BUSTER_TIER1_EPOCH_CALL 2u
#define BUSTER_TIER1_EPOCH_LEAVES 4u

typedef struct Tier1EmitEpochCache Tier1EmitEpochCache;
struct Tier1EmitEpochCache
{
    const Tier1SelectPlan* plan;
    const Tier1EmitResult* emitted;
    const Tier1CfgInput* input;
    Tier1CfgAllocation shape;
    uint32_t nodes;
    uint32_t blocks;
    uint32_t added;
    uint32_t base;
    uint32_t* transition_offsets;
    uint32_t* transition_fragments;
    uint32_t* fragment_nodes;
    uint8_t* fragment_bases;
    uint32_t* entry_offsets;
    uint32_t* entry_nodes;
    uint32_t* entry_fragments;
    uint32_t* definitions;
    uint8_t* flags;
    uint8_t* dropped;
    uint32_t* preload_offsets;
    uint32_t* preload_nodes;
    uint32_t* preload_fragments;
    uint32_t* dependency_offsets;
    uint32_t* dependency_blocks;
    uint32_t* carry_parent;
    uint32_t* child_offsets;
    uint32_t* children;
    // Mutable accepted state. No trial writes it; publication is explicit and requires the matching generation.
    uint8_t* registers;
    uint32_t* stores;
    uint32_t* loads;
    uint32_t* exit_rows;
    uint64_t generation;
};

typedef struct Tier1EmitEpochTrial Tier1EmitEpochTrial;
struct Tier1EmitEpochTrial
{
    const Tier1EmitEpochCache* cache;
    uint64_t generation;
    uint32_t fragment_count;
    uint32_t* fragments;
    uint8_t* registers;
    uint32_t block_count;
    // Block IDs are strictly increasing. Each row below has sixteen elements, indexed by replay index, not block ID.
    uint32_t* blocks;
    uint32_t* stores;
    uint32_t* loads;
    uint32_t* exit_rows;
};

static bool tier1_emit_epoch_cache_charge(uint64_t amount, uint64_t limit, uint64_t* work)
{
    const bool ok = work != 0 && *work <= limit && amount <= limit - *work;
    if (ok)
    {
        *work += amount;
    }
    return ok;
}

static void* tier1_emit_epoch_cache_allocate(Arena* arena, uint64_t count, uint64_t bytes, Tier1EmitError* error)
{
    void* result = 0;
    if (*error == TIER1_EMIT_OK)
    {
        result = arena_push_array(arena, count != 0 ? count : 1, bytes, 64);
        *error = result != 0 ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    }
    return result;
}

static void tier1_emit_epoch_cache_fill(uint32_t* values, uint64_t count, uint32_t value)
{
    const __m512i repeated = _mm512_set1_epi32((int)value);
    for (uint64_t i = 0; i < count; i += 16)
    {
        const uint64_t left = count - i;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        _mm512_mask_storeu_epi32(values + i, lanes, repeated);
    }
}

static bool tier1_emit_epoch_cache_shape(const Tier1EmitEpochCache* cache, const Tier1CfgAllocation* cfg)
{
    const Tier1CfgAllocation* prior = &cache->shape;
    const Tier1AllocationInput* a = &prior->intervals;
    const Tier1AllocationInput* b = &cfg->intervals;
    const Tier1Allocation* x = &prior->allocation;
    const Tier1Allocation* y = &cfg->allocation;
    // Pointer identity enforces the frozen-column contract; clients must not mutate their borrowed contents. Allowed
    // masks, edge programs and register columns are deliberately omitted: register-only transforms may replace them.
    return a->count == b->count && a->value_count == b->value_count && a->position_count == b->position_count &&
           a->starts == b->starts && a->ends == b->ends && a->values == b->values && a->classes == b->classes &&
           a->rematerializable == b->rematerializable && a->use_offsets == b->use_offsets && a->use_positions == b->use_positions &&
           a->use_required == b->use_required && a->use_count == b->use_count &&
           x->fragment_count == y->fragment_count && x->interval_count == y->interval_count &&
           x->fragment_offsets == y->fragment_offsets && x->starts == y->starts && x->ends == y->ends &&
           x->kinds == y->kinds && x->spill_ids == y->spill_ids && (y->fragment_count == 0 || y->registers != 0) &&
           prior->value_interval_offsets == cfg->value_interval_offsets && prior->position_blocks == cfg->position_blocks &&
           prior->value_block_base == cfg->value_block_base && prior->value_block_intervals == cfg->value_block_intervals &&
           prior->block_starts == cfg->block_starts && prior->block_count == cfg->block_count && prior->unread_entries == cfg->unread_entries;
}

static uint8_t tier1_emit_epoch_cache_expression(const Tier1EmitEpochCache* cache, const Tier1Allocation* allocation, uint32_t fragment)
{
    return fragment != BUSTER_TIER1_CFG_NONE ? (uint8_t)(cache->fragment_bases[fragment] + allocation->registers[fragment]) : UINT8_MAX;
}

static bool tier1_emit_epoch_cache_size_add(uint64_t count, uint64_t bytes, uint64_t* total)
{
    uint64_t size = 0;
    uint64_t padded = 0;
    uint64_t sum = 0;
    const bool ok = !__builtin_mul_overflow(count != 0 ? count : 1, bytes, &size) &&
                    !__builtin_add_overflow(size, UINT64_C(63), &padded) && !__builtin_add_overflow(*total, padded, &sum);
    *total = ok ? sum : *total;
    return ok;
}

// Conservative retained-arena bounds, including every sentinel and each allocation's worst 64-byte alignment gap.
// entry_count is the same frozen sum used by tier1_emit_recolor_entries. A stale or underestimated count is not a
// safety issue (the arena refuses), but the caller must pass the real count to obtain the promised capacity bound.
static uint64_t tier1_emit_epoch_cache_bytes(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                             const Tier1CfgAllocation* cfg, uint64_t entry_count)
{
    const uint64_t n = plan->function->instruction_count;
    const uint64_t b = input->block_count;
    const uint64_t f = cfg->allocation.fragment_count;
    const uint64_t p = input->position_count;
    const uint64_t r = plan->effective_use_offsets[n] + n * BUSTER_TIER1_SELECT_VALUES;
    const uint64_t d = entry_count + r + n + 2 * f;
    bool ok = entry_count < UINT32_MAX && r < UINT32_MAX && d < UINT32_MAX;
    const uint64_t counts[] = { f+1, b*16, b*16, b*16, p+2, f+1, f+1, f+1, b+1, n+1, n+1, n+1,
                                b+1, b+1, b+1, entry_count+1, entry_count+1, b+1, r+1, r+1, f+1, d+1 };
    const uint8_t sizes[] = { 1, 4, 4, 4, 4, 4, 4, 1, 4, 4, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 };
    uint64_t total = 0;
    for (uint32_t i = 0; ok && i < sizeof(counts) / sizeof(counts[0]); i += 1)
    {
        ok = tier1_emit_epoch_cache_size_add(counts[i], sizes[i], &total);
    }
    return ok ? total : 0;
}

static uint64_t tier1_emit_epoch_trial_bytes(const Tier1EmitEpochCache* cache)
{
    const uint64_t f = cache->shape.allocation.fragment_count;
    const uint64_t b = cache->blocks;
    const uint64_t counts[] = { f+1, f+1, b+1, b*16, b*16, b*16 };
    const uint8_t sizes[] = { 4, 1, 4, 4, 4, 4 };
    uint64_t total = 0;
    bool ok = true;
    for (uint32_t i = 0; ok && i < sizeof(counts) / sizeof(counts[0]); i += 1)
    {
        ok = tier1_emit_epoch_cache_size_add(counts[i], sizes[i], &total);
    }
    return ok ? total : 0;
}

// Maximum temporary demand across preparation passes and one trial. Persistent output arenas are disjoint from
// this workspace, but may be separately carved from the same outer scratch reservation before either API is called.
static uint64_t tier1_emit_epoch_cache_scratch_bytes(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                     const Tier1CfgAllocation* cfg)
{
    const uint64_t n = plan->function->instruction_count;
    const uint64_t b = input->block_count;
    const uint64_t f = cfg->allocation.fragment_count;
    const uint64_t p = input->position_count;
    const uint64_t counts[4][4] = { { p+1, b+1, 0, 0 }, { n+1, n+1, b+1, 0 }, { f+1, f+1, 0, 0 },
                                     { f+1, (b+63)/64, b+1, n+1 } };
    const uint8_t sizes[4][4] = { { 4,4,0,0 }, { 4,4,4,0 }, { 4,4,0,0 }, { 4,8,4,1 } };
    uint64_t maximum = 0;
    bool ok = true;
    for (uint32_t row = 0; ok && row < 4; row += 1)
    {
        uint64_t total = 0;
        for (uint32_t i = 0; ok && i < 4; i += 1)
        {
            ok = sizes[row][i] == 0 || tier1_emit_epoch_cache_size_add(counts[row][i], sizes[row][i], &total);
        }
        maximum = maximum > total ? maximum : total;
    }
    return ok ? maximum : 0;
}

#include "tier1_emit_epoch_cache_model.c"
#include "tier1_emit_epoch_cache_replay.c"
#include "tier1_emit_epoch_cache_trial.c"
