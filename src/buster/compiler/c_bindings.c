// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Scoped C names (#128). Include after lane.c. A parser records declarations, identifier uses and undo marks in
// source order. A declaration is recorded AFTER its complete declarator (an enumerator after its definition),
// before its initializer. Tags bind after their name. A definition's saved parameters may be activated again in
// its body with the same declaration token. Event order, rather than spelling position, orders those activations.
//
// c_bindings_declare/use/restore maintain dense per-IdentId heads and a scope undo log. c_bindings_scalar replays
// that log independently. c_bindings_batch stably radix-sorts (namespace, IdentId, event position), scans the name
// segments in AVX-512, then checks the candidate's visibility interval. It bounds backward walks and lazily replays
// the scalar path once if needed. No hardware gathers/scatters, callbacks, recursion or hash table are used.
//
// Results are activation event + 1 (0 means no declaration). declaration[result - 1] is the declaration token;
// kind[result - 1] distinguishes typedefs, objects, functions, enumerators and tags. Labels use a separate head
// array; their function's definitions must be registered before its uses, since gotos may precede labels. Member
// names require their containing type and are left to the type checker. The parser owns all semantic decisions.
//
// The arena and result buffers belong to the caller. No input padding is required. Capacity exhaustion is sticky:
// callers must reject the partial unit, then reset. Reset pops the file scope, reusing heads across translation
// units without clearing every process-wide IdentId. A workspace is private to its compilation thread.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_C_BINDING_NONE UINT32_MAX
#define BUSTER_C_BINDING_WALK_LIMIT 8
#define BUSTER_C_BINDING_LANES 16

typedef enum CBindingNamespace
{
    C_BINDING_ORDINARY,
    C_BINDING_TAG,
    C_BINDING_LABEL,
    C_BINDING_NAMESPACES,
} CBindingNamespace;

typedef enum CBindingKind
{
    C_BINDING_TYPEDEF,
    C_BINDING_OBJECT,
    C_BINDING_FUNCTION,
    C_BINDING_ENUMERATOR,
    C_BINDING_TAG_KIND,
    C_BINDING_LABEL_KIND,
    C_BINDING_KINDS,
} CBindingKind;

typedef enum CBindingOperation
{
    C_BINDING_DECLARE,
    C_BINDING_USE,
    C_BINDING_RESTORE,
} CBindingOperation;

typedef struct CBindings CBindings;
struct CBindings
{
    uint32_t* heads;
    uint32_t name_capacity;
    uint32_t capacity;
    uint32_t count;
    uint32_t* active;
    uint32_t active_count;
    // namespace * name_capacity + IdentId; IdentId zero is not an identifier.
    uint32_t* key;
    uint32_t* declaration;
    uint32_t* point;
    uint32_t* end;
    uint32_t* previous;
    uint32_t* depth;
    uint8_t* operation;
    uint8_t* kind;
    bool valid;
    // Events below base aren't this stream's own (#113: a body's stream continues a copy of the top level's), and
    // don't order its points: its first event may come before the last of them.
    uint32_t base;
};

typedef struct CBindingStatistics CBindingStatistics;
struct CBindingStatistics
{
    uint64_t identifiers;
    uint64_t declarations;
    uint64_t walks;
    uint64_t walked_uses;
    uint64_t fallback_uses;
};

typedef struct CBindingReplay CBindingReplay;
struct CBindingReplay
{
    uint32_t* heads;
    uint32_t* active;
    uint32_t* previous;
    uint32_t name_capacity;
    uint32_t capacity;
};

static __attribute__((unused)) bool c_bindings_initialize(CBindings* bindings, Arena* arena, uint32_t names, uint32_t capacity)
{
    const uint64_t mark = arena->length;
    *bindings = (CBindings){ .name_capacity = names, .capacity = capacity };
    bool valid = names != 0 && names <= UINT32_MAX / C_BINDING_NAMESPACES && capacity != UINT32_MAX;
    bindings->heads = valid ? arena_push_array(arena, (uint64_t)names * C_BINDING_NAMESPACES, 4, 64) : 0;
    bindings->active = bindings->heads != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->key = bindings->active != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->declaration = bindings->key != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->point = bindings->declaration != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->end = bindings->point != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->previous = bindings->end != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->depth = bindings->previous != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    bindings->operation = bindings->depth != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 1, 64) : 0;
    bindings->kind = bindings->operation != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 1, 64) : 0;
    valid = bindings->kind != 0;
    if (valid)
    {
        memset(bindings->heads, 0, (uint64_t)names * C_BINDING_NAMESPACES * 4);
        bindings->valid = true;
    }
    else
    {
        arena_pop_to(arena, mark);
        *bindings = (CBindings){ 0 };
    }
    return valid;
}

static __attribute__((unused)) void c_bindings_reset(CBindings* bindings)
{
    while (bindings->active_count != 0)
    {
        const uint32_t event = bindings->active[--bindings->active_count];
        bindings->heads[bindings->key[event]] = bindings->previous[event];
    }
    bindings->count = 0;
    bindings->valid = bindings->heads != 0;
}

static __attribute__((unused)) uint32_t c_bindings_mark(const CBindings* bindings)
{
    return bindings->active_count;
}

static uint32_t c_bindings_append(CBindings* bindings, CBindingNamespace space, uint32_t name, uint32_t token, uint32_t point, uint32_t depth, CBindingOperation operation, CBindingKind kind)
{
    uint32_t result = 0;
    const bool ordered = bindings->count == bindings->base || point >= bindings->point[bindings->count - 1];
    bindings->valid = bindings->valid && bindings->count < bindings->capacity && ordered &&
                      (uint32_t)space < C_BINDING_NAMESPACES && name != 0 && name < bindings->name_capacity && (uint32_t)kind < C_BINDING_KINDS;
    if (bindings->valid)
    {
        const uint32_t event = bindings->count++;
        const uint32_t key = (uint32_t)space * bindings->name_capacity + name;
        bindings->key[event] = key;
        bindings->declaration[event] = token;
        bindings->point[event] = point;
        bindings->end[event] = BUSTER_C_BINDING_NONE;
        bindings->depth[event] = depth;
        bindings->operation[event] = operation;
        bindings->kind[event] = kind;
        bindings->previous[event] = bindings->heads[key];
        result = event + 1;
        if (operation == C_BINDING_DECLARE)
        {
            bindings->active[bindings->active_count++] = event;
            bindings->heads[key] = result;
        }
    }
    return result;
}

static uint32_t c_bindings_declare(CBindings* bindings, CBindingNamespace space, uint32_t name, CBindingKind kind, uint32_t token, uint32_t point, uint32_t depth)
{
    return c_bindings_append(bindings, space, name, token, point, depth, C_BINDING_DECLARE, kind);
}

static uint32_t c_bindings_use(CBindings* bindings, CBindingNamespace space, uint32_t name, uint32_t token, uint32_t point)
{
    return c_bindings_append(bindings, space, name, token, point, 0, C_BINDING_USE, C_BINDING_OBJECT);
}

static uint32_t c_bindings_lookup(const CBindings* bindings, CBindingNamespace space, uint32_t name)
{
    uint32_t result = 0;
    if (bindings->valid && (uint32_t)space < C_BINDING_NAMESPACES && name != 0 && name < bindings->name_capacity)
    {
        result = bindings->heads[(uint32_t)space * bindings->name_capacity + name];
    }
    return result;
}

static bool c_bindings_restore(CBindings* bindings, uint32_t mark, uint32_t point)
{
    const bool ordered = bindings->count == bindings->base || point >= bindings->point[bindings->count - 1];
    bindings->valid = bindings->valid && mark <= bindings->active_count && bindings->count < bindings->capacity && ordered;
    if (bindings->valid)
    {
        const uint32_t event = bindings->count++;
        bindings->operation[event] = C_BINDING_RESTORE;
        bindings->declaration[event] = mark;
        bindings->point[event] = point;
        // A restore names no declaration: its other fields are written all the same, so that the stream's columns
        // don't keep whatever its slot held (#113's lanes reuse their slots from body to body).
        bindings->key[event] = 0;
        bindings->end[event] = BUSTER_C_BINDING_NONE;
        bindings->previous[event] = 0;
        bindings->depth[event] = 0;
        bindings->kind[event] = 0;
        while (bindings->active_count > mark)
        {
            const uint32_t old = bindings->active[--bindings->active_count];
            bindings->end[old] = event;
            bindings->heads[bindings->key[old]] = bindings->previous[old];
        }
    }
    return bindings->valid;
}

static bool c_bindings_replay_initialize(CBindingReplay* replay, Arena* arena, uint32_t names, uint32_t capacity)
{
    const uint64_t mark = arena->length;
    *replay = (CBindingReplay){ .name_capacity = names, .capacity = capacity };
    const bool bounded = names != 0 && names <= UINT32_MAX / C_BINDING_NAMESPACES && capacity != UINT32_MAX;
    replay->heads = bounded ? arena_push_array(arena, (uint64_t)names * C_BINDING_NAMESPACES, 4, 64) : 0;
    replay->active = replay->heads != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    replay->previous = replay->active != 0 ? arena_push_array(arena, (uint64_t)capacity + 1, 4, 64) : 0;
    const bool valid = replay->previous != 0;
    if (valid)
    {
        memset(replay->heads, 0, (uint64_t)names * C_BINDING_NAMESPACES * 4);
    }
    else
    {
        arena_pop_to(arena, mark);
        *replay = (CBindingReplay){ 0 };
    }
    return valid;
}

// Replay independently: neither the producer's head values, its previous-binding column nor its end points are
// read. Scope marks restore the replay's own undo log. Pop the remaining file scope afterward so that the heads
// stay reusable across units: no O(process-wide IdentIds) clearing on each run. `results` has count slots.
static bool c_bindings_replay(const CBindings* bindings, CBindingReplay* replay, uint32_t* results)
{
    const bool valid = bindings->valid && replay->heads != 0 && replay->name_capacity == bindings->name_capacity && replay->capacity >= bindings->count;
    if (valid)
    {
        uint32_t* heads = replay->heads;
        uint32_t* active = replay->active;
        uint32_t* previous = replay->previous;
        uint32_t active_count = 0;
        for (uint32_t i = 0; i < bindings->count; i += 1)
        {
            results[i] = 0;
            if (bindings->operation[i] == C_BINDING_RESTORE)
            {
                while (active_count > bindings->declaration[i])
                {
                    const uint32_t old = active[--active_count];
                    heads[bindings->key[old]] = previous[old];
                }
            }
            else
            {
                const uint32_t key = bindings->key[i];
                if (bindings->operation[i] == C_BINDING_DECLARE)
                {
                    previous[i] = heads[key];
                    heads[key] = i + 1;
                    active[active_count++] = i;
                }
                results[i] = heads[key];
            }
        }
        while (active_count != 0)
        {
            const uint32_t old = active[--active_count];
            heads[bindings->key[old]] = previous[old];
        }
    }
    return valid;
}

// Convenience oracle for a one-off comparison or a rare fallback. Repeated callers keep a CBindingReplay instead.
static bool c_bindings_scalar(const CBindings* bindings, uint32_t* results, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    CBindingReplay replay;
    const bool valid = bindings->valid && c_bindings_replay_initialize(&replay, scratch, bindings->name_capacity, bindings->count) && c_bindings_replay(bindings, &replay, results);
    arena_pop_to(scratch, mark);
    return valid;
}

// The 16 digits of a block of 64-bit keys. A tail uses masked loads, including a zero mask for the absent second
// half, without forming a pointer past the input's end. All shifts are over unsigned 64-bit values.
static __m512i c_bindings_digits(const uint64_t* keys, uint32_t lanes, uint32_t shift)
{
    const uint32_t first = lanes < 8 ? lanes : 8;
    const uint32_t second = lanes > 8 ? lanes - 8 : 0;
    const __m512i low = _mm512_maskz_loadu_epi64((__mmask8)((1u << first) - 1), keys);
    const __m512i high = _mm512_maskz_loadu_epi64((__mmask8)((1u << second) - 1), keys + first);
    const __m512i amount = _mm512_set1_epi64(shift);
    const __m256i a = _mm512_cvtepi64_epi32(_mm512_srlv_epi64(low, amount));
    const __m256i b = _mm512_cvtepi64_epi32(_mm512_srlv_epi64(high, amount));
    return _mm512_inserti64x4(_mm512_castsi256_si512(a), b, 1);
}

// Stable LSD radix sort. Input is already in event order, so only the four bytes of (namespace, IdentId) need
// sorting. Conflict masks give each lane's rank in its bucket. Only the last lane updates the histogram/cursor;
// stores to the destination use scalar addresses, never the expensive hardware scatter on Zen 5. `varying` includes
// every high-half bit that differs between input keys. The producer computes it while packing keys, so the vector
// path skips constant digits without scanning them. The scalar oracle always takes all four passes. Return the
// buffer holding the sorted keys: an odd number of passes ends in temporary, with no copy back. Both buffers have
// count readable/writable entries, do not overlap, and stay alive through the consumer; count <= 1 reads neither.
static uint64_t* c_bindings_sort(uint64_t* keys, uint64_t* temporary, uint32_t count, bool vector, uint32_t varying)
{
    (void)vector;

    uint64_t* input = keys;
    uint64_t* output = temporary;
    for (uint32_t pass = 0; count > 1 && pass < 4; pass += 1)
    {
        if (!BUSTER_VECTOR_PATH(vector) || ((varying >> (pass * 8)) & 255) != 0)
        {
            uint32_t histogram[256] = { 0 };
            const uint32_t shift = 32 + pass * 8;
            for (uint32_t phase = 0; phase < 2; phase += 1)
            {
                if (phase == 1)
                {
                    uint32_t total = 0;
                    for (uint32_t bucket = 0; bucket < 256; bucket += 1)
                    {
                        const uint32_t frequency = histogram[bucket];
                        histogram[bucket] = total;
                        total += frequency;
                    }
                }
                if (BUSTER_VECTOR_PATH(vector))
                {
                    for (uint32_t begin = 0; begin < count;)
                    {
                        const uint32_t lanes = count - begin < 16 ? count - begin : 16;
                        const __mmask16 mask = (__mmask16)((1u << lanes) - 1);
                        const __m512i digits = _mm512_and_si512(c_bindings_digits(input + begin, lanes, shift), _mm512_set1_epi32(255));
                        const __m512i conflicts = _mm512_maskz_conflict_epi32(mask, digits);
                        const __m512i ranks = _mm512_popcnt_epi32(conflicts);
                        const uint32_t earlier = (uint32_t)_mm512_reduce_or_epi32(conflicts);
                        uint32_t buckets[16];
                        uint32_t offsets[16];
                        _mm512_storeu_si512(buckets, digits);
                        _mm512_storeu_si512(offsets, ranks);
                        if (phase == 1)
                        {
                            for (uint32_t lane = 0; lane < lanes; lane += 1)
                            {
                                output[histogram[buckets[lane]] + offsets[lane]] = input[begin + lane];
                            }
                        }
                        uint32_t last = (uint32_t)mask & ~earlier;
                        while (last != 0)
                        {
                            const uint32_t lane = (uint32_t)__builtin_ctz(last);
                            histogram[buckets[lane]] += offsets[lane] + 1;
                            last &= last - 1;
                        }
                        begin += lanes;
                    }
                }
                else
                {
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        const uint32_t bucket = (uint32_t)(input[i] >> shift) & 255;
                        if (phase == 0)
                        {
                            histogram[bucket] += 1;
                        }
                        else
                        {
                            output[histogram[bucket]++] = input[i];
                        }
                    }
                }
            }
            uint64_t* swap = input;
            input = output;
            output = swap;
        }
    }
    return input;
}

// Allocate and sort the active records. The caller keeps the scratch mark alive through c_bindings_scan, then
// pops it. Exposing these two phases lets the benchmark measure the sort's share without reading a clock here.
static bool c_bindings_sorted(const CBindings* bindings, Arena* scratch, uint64_t** keys, uint32_t* count, bool vector)
{
    (void)vector;

    *keys = bindings->valid ? arena_push_array(scratch, (uint64_t)bindings->count + 1, 8, 64) : 0;
    uint64_t* temporary = *keys != 0 ? arena_push_array(scratch, (uint64_t)bindings->count + 1, 8, 64) : 0;
    const bool valid = temporary != 0;
    *count = 0;
    if (valid)
    {
        uint32_t any = 0;
        uint32_t every = UINT32_MAX;
        for (uint32_t i = 0; i < bindings->count; i += 1)
        {
            if (bindings->operation[i] != C_BINDING_RESTORE)
            {
                const uint32_t key = bindings->key[i];
                (*keys)[(*count)++] = ((uint64_t)key << 32) | i;
                any |= key;
                every &= key;
            }
        }
        *keys = c_bindings_sort(*keys, temporary, *count, BUSTER_VECTOR_PATH(vector), any ^ every);
    }
    return valid;
}

static bool c_bindings_scan(const CBindings* bindings, const uint64_t* keys, uint32_t count, uint32_t* results, Arena* scratch, CBindingStatistics* statistics)
{
    const uint64_t mark = scratch->length;
    uint32_t* previous = arena_push_array(scratch, (uint64_t)bindings->count + 1, 4, 64);
    bool valid = previous != 0;
    uint32_t* fallback = 0;
    uint32_t carry_key = UINT32_MAX;
    uint32_t carry_declaration = 0;
    *statistics = (CBindingStatistics){ 0 };
    if (valid)
    {
        if (bindings->count != 0)
        {
            memset(results, 0, (uint64_t)bindings->count * 4);
        }
        const __m512i lanes_vector = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (uint32_t begin = 0; valid && begin < count;)
        {
            const uint32_t lanes = count - begin < 16 ? count - begin : 16;
            const __m512i names = c_bindings_digits(keys + begin, lanes, 32);
            uint32_t candidates[16] = { 0 };
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                const uint32_t event = (uint32_t)keys[begin + lane];
                const bool declaration = bindings->operation[event] == C_BINDING_DECLARE;
                candidates[lane] = declaration ? event + 1 : 0;
                statistics->declarations += declaration;
            }
            __m512i latest = _mm512_loadu_si512(candidates);
            for (uint32_t step = 1; step < 16; step *= 2)
            {
                const __m512i indices = _mm512_sub_epi32(lanes_vector, _mm512_set1_epi32(step));
                const __m512i prior_names = _mm512_permutexvar_epi32(indices, names);
                const __m512i prior = _mm512_permutexvar_epi32(indices, latest);
                const __mmask16 same = _mm512_cmpeq_epi32_mask(names, prior_names) & (__mmask16)(0xffffu << step);
                latest = _mm512_mask_max_epu32(latest, same, latest, prior);
            }
            const __mmask16 continued = _mm512_cmpeq_epi32_mask(names, _mm512_set1_epi32(carry_key));
            latest = _mm512_mask_max_epu32(latest, continued, latest, _mm512_set1_epi32(carry_declaration));
            _mm512_storeu_si512(candidates, latest);
            for (uint32_t lane = 0; valid && lane < lanes; lane += 1)
            {
                const uint32_t event = (uint32_t)keys[begin + lane];
                const uint32_t key = (uint32_t)(keys[begin + lane] >> 32);
                const uint32_t prior = key == carry_key ? carry_declaration : 0;
                uint32_t candidate = candidates[lane];
                if (bindings->operation[event] == C_BINDING_DECLARE)
                {
                    previous[event] = prior;
                }
                else
                {
                    uint32_t walks = 0;
                    while (candidate != 0 && bindings->end[candidate - 1] <= event && walks < BUSTER_C_BINDING_WALK_LIMIT)
                    {
                        candidate = previous[candidate - 1];
                        walks += 1;
                    }
                    statistics->walks += walks;
                    statistics->walked_uses += walks != 0;
                    if (candidate != 0 && bindings->end[candidate - 1] <= event)
                    {
                        if (fallback == 0)
                        {
                            fallback = arena_push_array(scratch, (uint64_t)bindings->count + 1, 4, 64);
                            valid = fallback != 0 && c_bindings_scalar(bindings, fallback, scratch);
                        }
                        candidate = valid ? fallback[event] : 0;
                        statistics->fallback_uses += 1;
                    }
                }
                results[event] = candidate;
                carry_key = key;
                carry_declaration = candidates[lane];
            }
            begin += lanes;
        }
    }
    statistics->identifiers = count;
    arena_pop_to(scratch, mark);
    return valid;
}

static __attribute__((unused)) bool c_bindings_batch(const CBindings* bindings, uint32_t* results, Arena* scratch, CBindingStatistics* statistics)
{
    const uint64_t mark = scratch->length;
    uint64_t* keys = 0;
    uint32_t count = 0;
    *statistics = (CBindingStatistics){ 0 };
    const bool valid = c_bindings_sorted(bindings, scratch, &keys, &count, true) && c_bindings_scan(bindings, keys, count, results, scratch, statistics);
    arena_pop_to(scratch, mark);
    return valid;
}
