// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-position environments for deferred bodies (#113). Include after c_bindings.c, c_types.c and c_parser.c.
// c_environment_build freezes the file-scope declaration history of a complete CBindings stream. Initializers and
// tag/enum definitions MUST have been processed too: bracket depth is not C scope depth. Each body keeps its own
// local stacks; an unresolved local name consults this immutable index at the USE's preprocessed token position.
// Results are original activation handles, so redeclarations retain their historical type, never a final composite.
// The source CBindings and all its columns remain alive and unchanged until the last reader joins. Reset invalidates
// the environment. No worker writes shared types, heads or completion points. The caller owns all arenas and arrays.
//
// Completion points are a separate column indexed by TypeTags index, not TypeTags.completion's completion ordinal.
// The top-level checker records the token immediately after a tag's closing brace (or its language-defined earlier
// completion point) once, before publishing the column. UINT32_MAX means never complete. Local tags belong to their
// body's own table/column. c_environment_types checks complete-object requirements; it does not decide whether an
// operator requires completeness or whether a language extension permits void/functions.
//
// Index construction counts/places/fills source-ordered declarations. Lookup searches 16 independent name histories
// together; scalar loads feed comparisons rather than Zen 5 hardware gathers. Both paths include partial tails.
// c_environment_bodies consumes the VALIDATED parser role/depth columns, never raw bytes or a brace skim. It assigns
// source-order output slots before scheduling; a worker's results belong in its slot, irrespective of worker order.

#include "host_isa.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUSTER_C_ENVIRONMENT_LANES 16
#define BUSTER_C_ENVIRONMENT_INCOMPLETE UINT32_MAX

typedef struct CEnvironment CEnvironment;
struct CEnvironment
{
    const CBindings* bindings;
    uint32_t* offsets;
    uint32_t* points;
    uint32_t* handles;
    uint32_t names;
    uint32_t keys;
    uint32_t count;
};

typedef struct CBodyRange CBodyRange;
struct CBodyRange
{
    uint32_t begin;
    uint32_t end;
};

// Allocates only the immutable file-scope index; scratch is released even on success. The output is empty and both
// arena marks are restored on failure. The arenas must be distinct. Input columns sharing an arena must precede
// its entry mark, so neither allocation nor rollback can invalidate them.
static bool c_environment_build(CEnvironment* environment, const CBindings* bindings, Arena* arena, Arena* scratch)
{
    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    *environment = (CEnvironment){ 0 };
    bool valid = arena != scratch && bindings->valid && bindings->name_capacity != 0 && bindings->name_capacity <= UINT32_MAX / C_BINDING_NAMESPACES;
    const uint32_t keys = valid ? bindings->name_capacity * 2 : 0;
    uint32_t* offsets = valid ? arena_push_array(arena, (uint64_t)keys + 1, 4, 64) : 0;
    uint32_t* cursors = offsets != 0 ? arena_push_array(scratch, keys, 4, 64) : 0;
    valid = cursors != 0;
    uint32_t count = 0;
    if (valid)
    {
        memset(offsets, 0, ((uint64_t)keys + 1) * 4);
        for (uint32_t i = 0; i < bindings->count; i += 1)
        {
            if (bindings->operation[i] == C_BINDING_DECLARE && bindings->depth[i] == 0 && bindings->key[i] < keys)
            {
                offsets[bindings->key[i] + 1] += 1;
                count += 1;
            }
        }
        uint32_t sum = 0;
        const __m512i zero = _mm512_setzero_si512();
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (uint64_t block = 0; block <= keys; block += BUSTER_C_ENVIRONMENT_LANES)
        {
            const uint32_t live = (uint64_t)keys + 1 - block < 16 ? (uint32_t)((uint64_t)keys + 1 - block) : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            __m512i prefix = _mm512_maskz_loadu_epi32(mask, offsets + block);
            for (uint32_t shift = 1; shift < 16; shift *= 2)
            {
                const __m512i previous = _mm512_sub_epi32(lanes, _mm512_set1_epi32((int)shift));
                const __mmask16 active = (__mmask16)(0xffffu << shift);
                prefix = _mm512_add_epi32(prefix, _mm512_mask_permutexvar_epi32(zero, active, previous, prefix));
            }
            prefix = _mm512_add_epi32(prefix, _mm512_set1_epi32((int)sum));
            _mm512_mask_storeu_epi32(offsets + block, mask, prefix);
            sum = offsets[block + live - 1];
        }
        memcpy(cursors, offsets, (uint64_t)keys * 4);
    }
    uint32_t* points = valid ? arena_push_array(arena, (uint64_t)count + 1, 4, 64) : 0;
    uint32_t* handles = points != 0 ? arena_push_array(arena, (uint64_t)count + 1, 4, 64) : 0;
    valid = handles != 0;
    if (valid)
    {
        for (uint32_t i = 0; i < bindings->count; i += 1)
        {
            if (bindings->operation[i] == C_BINDING_DECLARE && bindings->depth[i] == 0 && bindings->key[i] < keys)
            {
                const uint32_t slot = cursors[bindings->key[i]]++;
                points[slot] = bindings->point[i];
                handles[slot] = i + 1;
            }
        }
        *environment = (CEnvironment)
        {
            .bindings = bindings,
            .offsets = offsets,
            .points = points,
            .handles = handles,
            .names = bindings->name_capacity,
            .keys = keys,
            .count = count,
        };
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

// A query's key is namespace * names + IdentId, for ordinary or tag names only. IdentId 0 is invalid. Positions
// are activation/use positions in one preprocessed token stream, not byte offsets, #line values or completion IDs.
static bool c_environment_key_valid(const CEnvironment* environment, uint32_t key)
{
    return environment->bindings != 0 && key < environment->keys && key % environment->names != 0;
}

static uint32_t c_environment_lookup_key(const CEnvironment* environment, uint32_t key, uint32_t point)
{
    uint32_t result = 0;
    if (c_environment_key_valid(environment, key))
    {
        const uint32_t begin = environment->offsets[key];
        uint32_t low = begin;
        uint32_t high = environment->offsets[key + 1];
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (environment->points[middle] <= point)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        result = low == begin ? 0 : environment->handles[low - 1];
    }
    return result;
}

static uint32_t c_environment_lookup(const CEnvironment* environment, CBindingNamespace space, uint32_t name, uint32_t point)
{
    uint32_t result = 0;
    if ((uint32_t)space < C_BINDING_LABEL && name != 0 && name < environment->names && environment->bindings != 0)
    {
        result = c_environment_lookup_key(environment, (uint32_t)space * environment->names + name, point);
    }
    return result;
}

// Input/output columns have exactly count readable/writable u32s, no padding. All columns are disjoint from each
// other and the environment. No allocation. False means an invalid key; discard all output. Empty batches succeed.
static bool c_environment_lookup_batch(const CEnvironment* environment, const uint32_t* keys, const uint32_t* points, uint32_t count, uint32_t* results)
{
    bool valid = environment->bindings != 0;
    for (uint64_t block = 0; valid && block < count; block += BUSTER_C_ENVIRONMENT_LANES)
    {
        const uint32_t live = count - block < 16 ? (uint32_t)(count - block) : 16;
        const __mmask16 mask = (__mmask16)((1u << live) - 1);
        uint32_t begins[16] = { 0 };
        uint32_t lows[16] = { 0 };
        uint32_t highs[16] = { 0 };
        for (uint32_t lane = 0; lane < live; lane += 1)
        {
            const uint32_t key = keys[block + lane];
            const bool bounded = c_environment_key_valid(environment, key);
            valid &= bounded;
            if (bounded)
            {
                begins[lane] = lows[lane] = environment->offsets[key];
                highs[lane] = environment->offsets[key + 1];
            }
        }
        __m512i low = _mm512_loadu_si512(lows);
        __m512i high = _mm512_loadu_si512(highs);
        const __m512i use = _mm512_maskz_loadu_epi32(mask, points + block);
        __mmask16 active = _mm512_mask_cmplt_epu32_mask(mask, low, high);
        while (active != 0)
        {
            const __m512i middle = _mm512_add_epi32(low, _mm512_srli_epi32(_mm512_sub_epi32(high, low), 1));
            uint32_t indices[16];
            uint32_t values[16] = { 0 };
            _mm512_storeu_si512(indices, middle);
            uint32_t pending = active;
            while (pending != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(pending);
                pending &= pending - 1;
                values[lane] = environment->points[indices[lane]];
            }
            const __mmask16 before = _mm512_mask_cmple_epu32_mask(active, _mm512_loadu_si512(values), use);
            low = _mm512_mask_mov_epi32(low, before, _mm512_add_epi32(middle, _mm512_set1_epi32(1)));
            high = _mm512_mask_mov_epi32(high, active & ~before, middle);
            active = _mm512_mask_cmplt_epu32_mask(mask, low, high);
        }
        _mm512_storeu_si512(lows, low);
        for (uint32_t lane = 0; lane < live; lane += 1)
        {
            results[block + lane] = lows[lane] == begins[lane] ? 0 : environment->handles[lows[lane] - 1];
        }
    }
    return valid;
}

// TypeIds come from a validated TypeTable. A missing position for ANY completed tag fails closed. Pointers are
// complete regardless of their pointee. Arrays, atomic/aligned wrappers, complex and vector types inherit the
// availability of the object type they contain. A record's own completion subsumes its members' requirements.
static uint32_t c_environment_type_point(const TypeTable* types, const uint32_t* completions, uint32_t tags, TypeId type)
{
    uint32_t result = 0;
    bool done = false;
    while (!done)
    {
        if (!type_is_valid(types, type) || !type_is_complete(types, type))
        {
            result = BUSTER_C_ENVIRONMENT_INCOMPLETE;
            done = true;
        }
        else
        {
            const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
            const uint8_t kind = types->types.kind[index];
            if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
            {
                const uint32_t tag = types->types.index[index];
                result = tag < tags ? completions[tag] : BUSTER_C_ENVIRONMENT_INCOMPLETE;
                done = true;
            }
            else if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR ||
                     kind == BUSTER_TYPE_KIND_COMPLEX || type_kind_is_array(kind))
            {
                type = types->types.base[index];
            }
            else
            {
                done = true;
            }
        }
    }
    return result;
}

// These entry points include TypeId-to-completion-column preparation. Results have count bytes, exactly. The
// completion column has tags u32s; tag zero is unused. UINT32_MAX positions remain reserved for never-complete.
static void c_environment_types_scalar(const TypeTable* types, const uint32_t* completions, uint32_t tags, const TypeId* requested, const uint32_t* points, uint32_t count, uint8_t* results)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t required = c_environment_type_point(types, completions, tags, requested[i]);
        results[i] = required != BUSTER_C_ENVIRONMENT_INCOMPLETE && required <= points[i];
    }
}

static void c_environment_types(const TypeTable* types, const uint32_t* completions, uint32_t tags, const TypeId* requested, const uint32_t* points, uint32_t count, uint8_t* results)
{
#if !BUSTER_HOST_VECTOR
    c_environment_types_scalar(types, completions, tags, requested, points, count, results);
#else
    for (uint64_t block = 0; block < count; block += BUSTER_C_ENVIRONMENT_LANES)
    {
        const uint32_t live = count - block < 16 ? (uint32_t)(count - block) : 16;
        const __mmask16 mask = (__mmask16)((1u << live) - 1);
        uint32_t required[16] = { 0 };
        for (uint32_t lane = 0; lane < live; lane += 1)
        {
            required[lane] = c_environment_type_point(types, completions, tags, requested[block + lane]);
        }
        const __m512i completion = _mm512_loadu_si512(required);
        const __m512i use = _mm512_maskz_loadu_epi32(mask, points + block);
        const __mmask16 present = _mm512_mask_cmpneq_epu32_mask(mask, completion, _mm512_set1_epi32(-1));
        const __mmask16 complete = _mm512_mask_cmple_epu32_mask(present, completion, use);
        _mm_mask_storeu_epi8(results + block, mask, _mm512_cvtepi32_epi8(_mm512_maskz_set1_epi32(complete, 1)));
    }
#endif
}

// On success [begin,end) includes both braces. OPEN_BODY/CLOSE_BODY are function roles, unlike ordinary blocks,
// compound literals, enums and record definitions. The parser has already matched them. Output must not alias
// its role/depth columns. On failure count is zero and the caller discards partially written ranges.
static bool c_environment_bodies(const ParseTree* tree, uint32_t tokens, CBodyRange* ranges, uint32_t capacity, uint32_t* count)
{
    bool valid = true;
    uint32_t written = 0;
    uint32_t opener = UINT32_MAX;
    for (uint64_t block = 0; valid && block < tokens; block += 64)
    {
        const uint32_t live = tokens - block < 64 ? (uint32_t)(tokens - block) : 64;
        const __mmask64 mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const __m512i roles = _mm512_maskz_loadu_epi8(mask, tree->roles + block);
        uint64_t edges = _mm512_mask_cmpeq_epi8_mask(mask, roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY)) |
                         _mm512_mask_cmpeq_epi8_mask(mask, roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_BODY));
        while (valid && edges != 0)
        {
            const uint32_t token = (uint32_t)block + (uint32_t)__builtin_ctzll(edges);
            edges &= edges - 1;
            if (tree->roles[token] == PARSE_ROLE_OPEN_BODY)
            {
                valid = opener == UINT32_MAX;
                opener = token;
            }
            else
            {
                valid = opener != UINT32_MAX && written < capacity;
                valid = valid && tree->depths[opener] == tree->depths[token];
                if (valid)
                {
                    ranges[written++] = (CBodyRange)
                    {
                        .begin = opener,
                        .end = token + 1,
                    };
                    opener = UINT32_MAX;
                }
            }
        }
    }
    valid &= opener == UINT32_MAX;
    *count = valid ? written : 0;
    return valid;
}
