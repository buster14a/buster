// Research-only per-function quotient of promotion's Boolean dataflow kernel.
// Included only by the disposable, blob-anchored overlay.patch. No public API.
// Original event/value/type/source identities and all materialization stay owned
// by ir_promote_global. This sidecar dies with that function's scratch arena.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PQ_CAPACITY 8u

typedef struct PromotionQuotient PromotionQuotient;
struct PromotionQuotient
{
    u8* keys;
    u8* results;
    u8* current;
    u32 count;
    u32 blocks;
    bool enabled;
    bool share;
    bool trace;
    bool initial_safe[PQ_CAPACITY];
    bool result_safe[PQ_CAPACITY];
    u64 block_visits[PQ_CAPACITY];
    u64 forward_edges[PQ_CAPACITY];
    u64 backward_edges[PQ_CAPACITY];
};

typedef struct PromotionQuotientProbe PromotionQuotientProbe;
struct PromotionQuotientProbe
{
    u32 slot;
    bool hit;
    bool initial_safe;
    u64 comparisons;
    u64 compared_bytes;
    u64 block_visits;
    u64 forward_edges;
    u64 backward_edges;
};

BUSTER_GLOBAL_LOCAL void promotion_quotient_initialize(Arena* arena, PromotionQuotient* quotient, u32 blocks)
{
    char const* mode = getenv("BUSTER_RESEARCH_PROMOTION_QUOTIENT");
    quotient->enabled = mode && (!strcmp(mode, "observe") || !strcmp(mode, "share"));
    if (quotient->enabled)
    {
        quotient->share = !strcmp(mode, "share");
        quotient->trace = getenv("BUSTER_RESEARCH_PROMOTION_TRACE") != NULL;
        quotient->blocks = blocks;
        quotient->keys = arena_allocate(arena, u8, (u64)PQ_CAPACITY * blocks);
        quotient->results = arena_allocate(arena, u8, (u64)PQ_CAPACITY * blocks);
        quotient->current = arena_allocate(arena, u8, blocks);
    }
}

BUSTER_GLOBAL_LOCAL PromotionQuotientProbe promotion_quotient_begin(PromotionQuotient* quotient, u8 const* effects, u8* live, bool* safe)
{
    PromotionQuotientProbe probe = {.slot = UINT32_MAX, .initial_safe = *safe};
    if (quotient->enabled)
    {
        for (u32 block = 0; block < quotient->blocks; block += 1)
        {
            quotient->current[block] = (u8)(effects[block] | (live[block] << 2));
        }
        for (u32 slot = 0; slot < quotient->count && !probe.hit; slot += 1)
        {
            probe.comparisons += 1;
            bool equal = quotient->initial_safe[slot] == *safe;
            for (u32 block = 0; block < quotient->blocks && equal; block += 1)
            {
                probe.compared_bytes += 1;
                equal = quotient->keys[(u64)slot * quotient->blocks + block] == quotient->current[block];
            }
            if (equal)
            {
                probe.hit = true;
                probe.slot = slot;
            }
        }
        if (probe.hit && quotient->share)
        {
            *safe = quotient->result_safe[probe.slot];
            memcpy(live, quotient->results + (u64)probe.slot * quotient->blocks, quotient->blocks);
            probe.block_visits = quotient->block_visits[probe.slot];
            probe.forward_edges = quotient->forward_edges[probe.slot];
            probe.backward_edges = quotient->backward_edges[probe.slot];
        }
    }
    return probe;
}

BUSTER_GLOBAL_LOCAL void promotion_quotient_finish(Arena* arena, PromotionQuotient* quotient, PromotionQuotientProbe* probe,
                                                   IrFunction* function, IrPromoteLocal* local, bool safe, u8 const* live)
{
    if (quotient->enabled)
    {
        bool inserted = !probe->hit && quotient->count < PQ_CAPACITY;
        if (inserted)
        {
            probe->slot = quotient->count++;
            quotient->initial_safe[probe->slot] = probe->initial_safe;
            quotient->result_safe[probe->slot] = safe;
            memcpy(quotient->keys + (u64)probe->slot * quotient->blocks, quotient->current, quotient->blocks);
            memcpy(quotient->results + (u64)probe->slot * quotient->blocks, live, quotient->blocks);
            quotient->block_visits[probe->slot] = probe->block_visits;
            quotient->forward_edges[probe->slot] = probe->forward_edges;
            quotient->backward_edges[probe->slot] = probe->backward_edges;
        }
        if (probe->hit && !quotient->share)
        {
            BUSTER_CHECK(quotient->result_safe[probe->slot] == safe);
            BUSTER_CHECK(memcmp(quotient->results + (u64)probe->slot * quotient->blocks, live, quotient->blocks) == 0);
            BUSTER_CHECK(quotient->block_visits[probe->slot] == probe->block_visits);
            BUSTER_CHECK(quotient->forward_edges[probe->slot] == probe->forward_edges);
            BUSTER_CHECK(quotient->backward_edges[probe->slot] == probe->backward_edges);
        }
        if (quotient->trace)
        {
            // One write per row: no global mutable table or cross-lane ownership.
            // Trace-format work and its temporary buffer are diagnostic-only costs.
            TemporalArena trace_scratch = scratch_begin(&arena, 1);
            u64 capacity = (u64)quotient->blocks + 512;
            char* text = arena_allocate(trace_scratch.arena, char, capacity);
            int prefix = snprintf(text, (size_t)capacity, "PQ %u %u %u %u ", function->id.value, local->value,
                                  quotient->blocks, (u32)probe->initial_safe);
            BUSTER_CHECK(prefix > 0 && (u64)prefix < capacity);
            u64 at = (u64)prefix;
            for (u32 block = 0; block < quotient->blocks; block += 1)
            {
                text[at++] = (char)('0' + quotient->current[block]);
            }
            int suffix = snprintf(text + at, (size_t)(capacity - at), " %llu %llu %llu %u %u %u %llu %llu %u %llu\n",
                                  (unsigned long long)probe->block_visits, (unsigned long long)probe->forward_edges,
                                  (unsigned long long)probe->backward_edges, (u32)safe, (u32)probe->hit,
                                  (u32)(probe->hit && quotient->share), (unsigned long long)probe->comparisons,
                                  (unsigned long long)probe->compared_bytes, (u32)inserted,
                                  (unsigned long long)((u64)(2 * PQ_CAPACITY + 1) * quotient->blocks + sizeof(*quotient)));
            BUSTER_CHECK(suffix > 0 && (u64)suffix < capacity - at);
            at += (u64)suffix;
            BUSTER_CHECK(fwrite(text, 1, (size_t)at, stderr) == at);
            scratch_end(trace_scratch);
        }
    }
}
