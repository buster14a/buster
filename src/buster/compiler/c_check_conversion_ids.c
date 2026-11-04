// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Optional #1238 typed-stream conversion IDs, included by c_checker.c after CheckStream/CheckOptions. An ID is zero
// for an empty record, has its high bit set for an inline operand-0 type with no other fields, or indexes a run's
// immutable conversion records (index plus one). Types whose high bit is set use the ordinary table, without loss.
// Inline types are snapshots, not references to an operand's mutable value during query replay. Readers never
// re-derive a conversion from that value. Each writer publishes its ID in node order, beside its other columns.
//
// The table grows geometrically in the caller's output arena. No insertion borrows the scheduler's scratch arena:
// nested actions and query proofs rewind it. Old arrays remain arena-owned and are charged to output_bytes. This is
// an opt-in experiment, not a claim that its extra hash/growth work pays on the 9700X. Allocation failure is sticky;
// check_graph_run rolls back the whole output, rather than exposing a partially interned stream.
#ifndef BUSTER_C_CHECK_CONVERSION_IDS_INCLUDED
#define BUSTER_C_CHECK_CONVERSION_IDS_INCLUDED

#define BUSTER_CHECK_CONVERSION_INLINE UINT32_C(0x80000000)
#define BUSTER_CHECK_CONVERSION_GROUP 16u

_Static_assert(sizeof(CheckNodeConversions) == 32 && sizeof(CheckConversion) == 8,
               "conversion keys have eight 32-bit fields and no padding");

// A value, never a pointer into a table which a later insertion may move. The dense case preserves hand-built streams.
static inline __attribute__((always_inline)) CheckNodeConversions check_stream_conversions(const CheckStream* stream, uint32_t node)
{
    CheckNodeConversions result;
    const uint32_t* const ids = stream->conversion_ids;
    if (ids == NULL)
    {
        result = stream->conversions[node];
    }
    else
    {
        const uint32_t id = ids[node];
        if (id == 0 || (id & BUSTER_CHECK_CONVERSION_INLINE) != 0)
        {
            result = (CheckNodeConversions){ .operands = { { .type = id & ~BUSTER_CHECK_CONVERSION_INLINE } } };
        }
        else
        {
            result = stream->conversion_table.values[id - 1];
        }
    }
    return result;
}

// The scalar twin compares the same eight fields. No masked or full-width load extends beyond a complete record.
static bool check_conversion_id_equal(const CheckNodeConversions* a, const CheckNodeConversions* b, bool scalar)
{
    bool equal;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const __m256i x = _mm256_loadu_si256((const __m256i*)a);
        const __m256i y = _mm256_loadu_si256((const __m256i*)b);
        equal = _mm256_movemask_epi8(_mm256_cmpeq_epi32(x, y)) == -1;
    }
    else
#endif
    {
        equal = a->store.type == b->store.type && a->store.flags == b->store.flags;
        for (uint32_t i = 0; i < 3; i += 1)
        {
            equal &= a->operands[i].type == b->operands[i].type && a->operands[i].flags == b->operands[i].flags;
        }
    }
    return equal;
}

// All fields participate, including flags on a zero type. A collision is always checked against the complete record.
static uint32_t check_conversion_id_hash(const CheckNodeConversions* value)
{
    uint32_t hash = UINT32_C(0x9e3779b9);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        hash = (hash ^ value->operands[i].type) * UINT32_C(0x85ebca6b);
        hash = (hash ^ value->operands[i].flags) * UINT32_C(0xc2b2ae35);
    }
    hash = (hash ^ value->store.type) * UINT32_C(0x85ebca6b);
    hash = (hash ^ value->store.flags) * UINT32_C(0xc2b2ae35);
    hash ^= hash >> 16;
    return hash | 1u;
}

// Every probe is a complete, aligned group of 16 slots within a power-of-two table. Zero hashes denote empty slots;
// the companion ID is read only for a matching nonzero hash. The first empty slot determines stable insertion order.
static uint32_t check_conversion_id_candidates(const uint32_t* hashes, uint32_t hash, bool scalar, uint32_t* empty)
{
    uint32_t matches = 0;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const __m512i group = _mm512_loadu_si512(hashes);
        matches = (uint32_t)_mm512_cmpeq_epi32_mask(group, _mm512_set1_epi32((int)hash));
        *empty = (uint32_t)_mm512_cmpeq_epi32_mask(group, _mm512_setzero_si512());
    }
    else
#endif
    {
        uint32_t zeros = 0;
        for (uint32_t lane = 0; lane < BUSTER_CHECK_CONVERSION_GROUP; lane += 1)
        {
            matches |= (uint32_t)(hashes[lane] == hash) << lane;
            zeros |= (uint32_t)(hashes[lane] == 0) << lane;
        }
        *empty = zeros;
    }
    return matches;
}

// At most half full. Rehash the live records into a newly cleared table; old arrays may not be overwritten because
// another output allocation can lie between them. All allocations happen before publishing any changed pointers.
static bool check_conversion_id_grow(CheckConversionTable* table)
{
    const uint32_t old_capacity = table->capacity;
    const uint32_t count = table->count;
    bool result = old_capacity < (UINT32_C(1) << 29);
    if (result)
    {
        const uint32_t capacity = old_capacity == 0 ? BUSTER_CHECK_CONVERSION_GROUP : old_capacity * 2;
        const uint32_t slots = capacity * 2;
        Arena* const arena = table->arena;
        const uint64_t mark = arena->length;
        CheckNodeConversions* values = BUSTER_ARENA_PUSH_ARRAY(arena, CheckNodeConversions, capacity);
        uint32_t* hashes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
        uint32_t* ids = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
        result = values != NULL && hashes != NULL && ids != NULL;
        if (result)
        {
            if (count != 0)
            {
                memcpy(values, table->values, (uint64_t)count * sizeof(*values));
            }
            memset(hashes, 0, (uint64_t)slots * sizeof(*hashes));
            const uint32_t mask = slots - 1;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t hash = check_conversion_id_hash(values + i);
                uint32_t group = (hash & mask) & ~(BUSTER_CHECK_CONVERSION_GROUP - 1);
                uint32_t empty = 0;
                while (empty == 0)
                {
                    check_conversion_id_candidates(hashes + group, hash, table->scalar, &empty);
                    if (empty == 0)
                    {
                        group = (group + BUSTER_CHECK_CONVERSION_GROUP) & mask;
                    }
                }
                const uint32_t slot = group + (uint32_t)__builtin_ctz(empty);
                hashes[slot] = hash;
                ids[slot] = i + 1;
            }
            table->values = values;
            table->hashes = hashes;
            table->slots = ids;
            table->capacity = capacity;
            table->slot_count = slots;
        }
        else
        {
            arena_pop_to(arena, mark);
        }
    }
    return result;
}

static uint32_t check_conversion_id_intern(CheckConversionTable* table, const CheckNodeConversions* value)
{
    uint32_t id = 0;
    const uint32_t rest = value->operands[0].flags | value->operands[1].type | value->operands[1].flags |
                          value->operands[2].type | value->operands[2].flags | value->store.type | value->store.flags;
    const TypeId type = value->operands[0].type;
    if (rest == 0 && (type & BUSTER_CHECK_CONVERSION_INLINE) == 0)
    {
        id = type == 0 ? 0 : BUSTER_CHECK_CONVERSION_INLINE | type;
    }
    else if (!table->failed)
    {
        bool ready = table->slot_count != 0 || check_conversion_id_grow(table);
        uint32_t slot = 0;
        const uint32_t hash = check_conversion_id_hash(value);
        if (ready)
        {
            // Only the insertion/growth below mutates these fields; cache them across the probe loop.
            const uint32_t mask = table->slot_count - 1;
            const uint32_t* const hashes = table->hashes;
            const uint32_t* const ids = table->slots;
            const CheckNodeConversions* const values = table->values;
            const bool scalar = table->scalar;
            uint32_t group = (hash & mask) & ~(BUSTER_CHECK_CONVERSION_GROUP - 1);
            uint32_t empty = 0;
            while (id == 0 && empty == 0)
            {
                uint32_t matches = check_conversion_id_candidates(hashes + group, hash, scalar, &empty);
                while (id == 0 && matches != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(matches);
                    const uint32_t candidate = ids[group + lane];
                    if (check_conversion_id_equal(values + candidate - 1, value, scalar))
                    {
                        id = candidate;
                    }
                    matches &= matches - 1;
                }
                if (empty != 0)
                {
                    slot = group + (uint32_t)__builtin_ctz(empty);
                }
                else
                {
                    group = (group + BUSTER_CHECK_CONVERSION_GROUP) & mask;
                }
            }
        }
        if (ready && id == 0)
        {
            if (table->count == table->capacity)
            {
                ready = check_conversion_id_grow(table);
                if (ready)
                {
                    const uint32_t mask = table->slot_count - 1;
                    uint32_t group = (hash & mask) & ~(BUSTER_CHECK_CONVERSION_GROUP - 1);
                    uint32_t empty = 0;
                    while (empty == 0)
                    {
                        check_conversion_id_candidates(table->hashes + group, hash, table->scalar, &empty);
                        if (empty == 0)
                        {
                            group = (group + BUSTER_CHECK_CONVERSION_GROUP) & mask;
                        }
                    }
                    slot = group + (uint32_t)__builtin_ctz(empty);
                }
            }
            if (ready)
            {
                const uint32_t index = table->count;
                table->values[index] = *value;
                table->hashes[slot] = hash;
                table->slots[slot] = index + 1;
                table->count = index + 1;
                id = index + 1;
            }
        }
        table->failed |= !ready;
    }
    return id;
}

// On failure publish a harmless empty conversion so any remaining nodes in an already scheduled batch are safe to
// visit. The sticky failure prevents the enclosing run from returning this stream, even if replay replaces diagnostics.
static void check_stream_set_conversions(CheckStream* stream, uint32_t node, const CheckNodeConversions* value)
{
    if (stream->conversion_ids == NULL)
    {
        stream->conversions[node] = *value;
    }
    else
    {
        CheckConversionTable* const table = &stream->conversion_table;
        const bool failed = table->failed;
        stream->conversion_ids[node] = check_conversion_id_intern(table, value);
        if (!failed && table->failed)
        {
            table->failed_node = node;
        }
    }
}

#endif
