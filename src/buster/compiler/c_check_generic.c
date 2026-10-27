// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Shared association validation for the expression and declaration-time _Generic readers. Include after
// CheckInputReader. A nominal record reached through pointers, arrays or atomics is a necessary compatibility key;
// every other type shares an ambiguous key. In particular, do not traverse function parameters: transparent unions
// can make different parameter tags compatible. Exact queries retain the selection's source position.
//
// The scratch-owned key/type columns have a power-of-two capacity, a multiple of 16, and at most half occupancy.
// Each probe reads exactly 16 owned keys. Insertion into the first empty lane preserves source order among equal
// keys, including across colliding groups; no sorting, deletion or rehash can change the first failing comparison.

#define BUSTER_CHECK_GENERIC_GROUP 16u

typedef struct CheckGenericSet CheckGenericSet;
struct CheckGenericSet
{
    TypeId* keys;
    TypeId* types;
    TypeId control;
    TypeId control_key;
    uint32_t capacity;
    uint32_t source_position;
    uint64_t duplicate_queries;
    uint64_t control_queries;
    TypeId local_keys[BUSTER_CHECK_GENERIC_GROUP];
    TypeId local_types[BUSTER_CHECK_GENERIC_GROUP];
};

static TypeId check_generic_key(const TypeTable* types, TypeId type)
{
    type = type_strip_aligned(types, type);
    uint8_t kind = type_kind(types, type);
    while (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ATOMIC || type_kind_is_array(kind))
    {
        type = type_strip_aligned(types, types->types.base[type_index(type)]);
        kind = type_kind(types, type);
    }
    // 1 is not an unqualified TypeId. Enums and their underlying integers, compatible arrays/functions and the
    // vector kinds deliberately meet in this group rather than mistaking TypeId inequality for incompatibility.
    return kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ? type_unqualified(type) : 1;
}

static bool check_generic_initialize(CheckInputReader* reader, CheckGenericSet* set, uint32_t token_span, TypeId control, uint32_t source_position)
{
    *set = (CheckGenericSet)
    {
        .control = control,
        .control_key = check_generic_key(reader->types, control),
        .source_position = source_position,
    };
    // Each association needs at least a type/default, a colon, an expression and its separator/closing token.
    // This upper bound avoids another punctuation pass, and 64-bit sizing rejects an unrepresentable capacity.
    const uint64_t maximum = (uint64_t)token_span / 4;
    uint64_t capacity = BUSTER_CHECK_GENERIC_GROUP;
    while (capacity < maximum * 2)
    {
        capacity *= 2;
    }
    bool valid = capacity <= UINT32_MAX;
    if (valid)
    {
        set->capacity = (uint32_t)capacity;
        set->keys = capacity == BUSTER_CHECK_GENERIC_GROUP ? set->local_keys : arena_push_array(reader->scratch, capacity, sizeof(TypeId), 4);
        set->types = capacity == BUSTER_CHECK_GENERIC_GROUP ? set->local_types : arena_push_array(reader->scratch, capacity, sizeof(TypeId), 4);
        valid = set->keys != NULL && set->types != NULL;
        if (valid)
        {
            memset(set->keys, 0, capacity * sizeof(TypeId));
        }
    }
    reader->output->resource |= !valid;
    return valid;
}

static uint32_t check_generic_masks(const TypeId* keys, TypeId key, bool scalar)
{
    uint32_t matches = 0;
    uint32_t empty = 0;
    if (scalar)
    {
        for (uint32_t lane = 0; lane < BUSTER_CHECK_GENERIC_GROUP; lane += 1)
        {
            matches |= (uint32_t)(keys[lane] == key) << lane;
            empty |= (uint32_t)(keys[lane] == 0) << lane;
        }
    }
    else
    {
        const __m512i block = _mm512_loadu_si512(keys);
        matches = _mm512_cmpeq_epi32_mask(block, _mm512_set1_epi32((int)key));
        empty = _mm512_cmpeq_epi32_mask(block, _mm512_setzero_si512());
    }
    return matches | (empty << BUSTER_CHECK_GENERIC_GROUP);
}

// Validate and append one association, then match the control. Counters count exact compatibility calls, not hash
// probes; the tests assert the distinct-tag scaling bound. Callers stop on false, preserving their diagnostic order.
static bool check_generic_association(CheckInputReader* reader, CheckGenericSet* set, TypeId type, bool* matches)
{
    const TypeId key = check_generic_key(reader->types, type);
    uint32_t hash = key * UINT32_C(0x9e3779b9);
    hash ^= hash >> 16;
    uint32_t group = (hash & (set->capacity - 1)) & ~(BUSTER_CHECK_GENERIC_GROUP - 1);
    bool valid = true;
    bool inserted = false;
    *matches = false;
    while (valid && !inserted)
    {
        const uint32_t masks = check_generic_masks(set->keys + group, key, reader->parser->options.scalar);
        uint32_t candidates = masks & UINT16_MAX;
        const uint32_t empty = masks >> BUSTER_CHECK_GENERIC_GROUP;
        while (valid && candidates != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(candidates);
            candidates &= candidates - 1;
            set->duplicate_queries += 1;
            const TypeAnswer answer = type_compatible_at(reader->types, type, set->types[group + lane], reader->transparent_unions, reader->types->tags.count, set->source_position);
            reader->output->resource |= answer == TYPE_ANSWER_FULL;
            valid = answer == TYPE_ANSWER_NO;
        }
        if (valid && empty != 0)
        {
            const uint32_t slot = group + (uint32_t)__builtin_ctz(empty);
            set->keys[slot] = key;
            set->types[slot] = type;
            inserted = true;
        }
        group = (group + BUSTER_CHECK_GENERIC_GROUP) & (set->capacity - 1);
    }
    if (valid && key == set->control_key)
    {
        set->control_queries += 1;
        const TypeAnswer answer = type_compatible_at(reader->types, type, set->control, reader->transparent_unions, reader->types->tags.count, set->source_position);
        reader->output->resource |= answer == TYPE_ANSWER_FULL;
        valid = answer != TYPE_ANSWER_FULL;
        *matches = answer == TYPE_ANSWER_YES;
    }
    return valid;
}
