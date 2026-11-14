// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact Clang 23.1.1 generated metadata, not implementation or source capability. The generated include retains
// LLVM data and its provenance; tools/clang_builtin_metadata.c reproduces it from hash-pinned installed inputs.
// Language/header tokens, custom checking and feature expressions are facts for the consumer to enforce. An E
// attribute does not establish C constant-expression acceptance. An I modifier does not supply target constraints.
// A lookup grants no checker, code-generation, target-feature or preprocessor capability.
//
// Bounded names need neither termination nor readable padding. Immutable table rows remain valid for the process.
// Scalar lookup is the twin of the eight-lane batch: binary-search positions advance in SIMD, while irregular
// bounded name comparisons inspect only logical bytes. No throughput improvement is claimed without measurement.
#ifndef BUSTER_C_BUILTIN_INTRINSIC_CATALOG_INCLUDED
#define BUSTER_C_BUILTIN_INTRINSIC_CATALOG_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"
#include "c_builtin_fortify.c"
#include "c_builtin_library.c"

typedef struct BuiltinIntrinsicInfo BuiltinIntrinsicInfo;
struct BuiltinIntrinsicInfo
{
    const char* name;
    const char* encoding;
    const char* attributes;
    const char* features;
    const char* header_token;
    const char* language_token;
    const char* shard;
    const char* record_kind;
    uint32_t name_length;
    uint32_t encoding_length;
    uint32_t attributes_length;
    uint32_t features_length;
    uint32_t source_line;
    uint32_t source_ordinal;
    bool custom_typechecking;
    bool constant_evaluable_attribute;
};

#define BUSTER_BUILTIN_METADATA(n, t, a, f, h, l, s, line, ordinal, custom, evaluable, kind, constraints) \
    { n, t, a, f, h, l, s, kind, sizeof(n) - 1, sizeof(t) - 1, sizeof(a) - 1, sizeof(f) - 1, line, ordinal, custom, evaluable },
static const BuiltinIntrinsicInfo builtin_intrinsic_infos[] =
{
#include "generated/clang_builtin_metadata_23_1_1.inc"
};
static const BuiltinIntrinsicInfo builtin_intrinsic_own_main_infos[] =
{
#include "generated/clang_builtin_own_main_23_1_1.inc"
};
#undef BUSTER_BUILTIN_METADATA

#define BUSTER_BUILTIN_INTRINSIC_COUNT ((uint32_t)(sizeof(builtin_intrinsic_infos) / sizeof(builtin_intrinsic_infos[0])))
#define BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT ((uint32_t)(sizeof(builtin_intrinsic_own_main_infos) / sizeof(builtin_intrinsic_own_main_infos[0])))
_Static_assert(BUSTER_BUILTIN_INTRINSIC_COUNT > 0 && BUSTER_BUILTIN_INTRINSIC_COUNT < INT32_MAX, "bounded search indices");
_Static_assert(BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT > 0 && BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT < INT32_MAX, "bounded scope search indices");

typedef struct BuiltinIntrinsicName BuiltinIntrinsicName;
struct BuiltinIntrinsicName
{
    const char* name;
    uint32_t length;
};

#include "c_builtin_intrinsic_filter.c"

static int builtin_intrinsic_compare(const BuiltinIntrinsicName* name, const BuiltinIntrinsicInfo* info)
{
    const uint32_t common = name->length < info->name_length ? name->length : info->name_length;
    int result = common == 0 ? 0 : memcmp(name->name, info->name, common);
    if (result == 0)
    {
        result = (name->length > info->name_length) - (name->length < info->name_length);
    }
    return result;
}

static __attribute__((unused)) const BuiltinIntrinsicInfo* builtin_intrinsic_at(uint32_t index)
{
    const BuiltinIntrinsicInfo* result = index < BUSTER_BUILTIN_INTRINSIC_COUNT ? builtin_intrinsic_infos + index : 0;
    return result;
}

static const BuiltinIntrinsicInfo* builtin_intrinsic_lookup_in(const BuiltinIntrinsicInfo* infos, uint32_t count, const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* result = 0;
    uint32_t lower = 0;
    uint32_t upper = name != 0 && length != 0 ? count : 0;
    const BuiltinIntrinsicName input = { name, length };
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const BuiltinIntrinsicInfo* info = infos + middle;
        const int order = builtin_intrinsic_compare(&input, info);
        if (order == 0)
        {
            result = info;
            upper = lower;
        }
        else if (order < 0)
        {
            upper = middle;
        }
        else
        {
            lower = middle + 1;
        }
    }
    return result;
}

static __attribute__((unused)) const BuiltinIntrinsicInfo* builtin_intrinsic_lookup(const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* result = builtin_intrinsic_lookup_in(builtin_intrinsic_infos, BUSTER_BUILTIN_INTRINSIC_COUNT, name, length);
    return result;
}

// The catalogue's exact table (#893, #2654): each row by a hash of every byte of its name, in open addressing over
// BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS slots of 32 bits, each its row plus one in the low half (0 for an empty slot) and
// the hash's top 16 bits in the high half. A lookup hashes the name once and reads the slots from the hash's to the first
// empty one, comparing the names of the rows whose tag matches: one compare for a name of the catalogue, and almost never
// one for a name of none, where the binary search makes about twelve. It is baked into the executable (baked.h, #590) in
// the checker's unit; where it isn't (a PIE, BUSTER_BAKE=0, another unit's copy), builtin_intrinsic_table_initialize
// makes it, which a command of several units runs before its lanes start. The binary search is its oracle.
#define BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS 8192u
_Static_assert(BUSTER_BUILTIN_INTRINSIC_COUNT * 2 <= BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS, "the exact table stays at most half full");
_Static_assert(BUSTER_BUILTIN_INTRINSIC_COUNT < UINT16_MAX, "a row plus one fits a slot's low half");
// main.c's units have baked.h already (x86_64_encoder.c's); a unit of its own (a benchmark's) keeps the table in .bss.
// test-baked-tables fails a baked executable whose table isn't baked.
#if defined(BUSTER_BAKED_UNIT)
#define BUSTER_BUILTIN_INTRINSIC_BAKED BUSTER_BAKED_UNIT
#else
#define BUSTER_BUILTIN_INTRINSIC_BAKED
#endif
static BUSTER_BUILTIN_INTRINSIC_BAKED uint32_t builtin_intrinsic_table[BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS];
static BUSTER_BUILTIN_INTRINSIC_BAKED bool builtin_intrinsic_table_ready;

// The exact table's hash of a bounded name: its 8-byte words, the last one's bytes past the name zero, each folded in by
// a multiply and a shift, then a final mix. It reads the name's bytes alone.
static uint64_t builtin_intrinsic_table_hash(const char* name, uint32_t length)
{
    uint64_t hash = (uint64_t)length * UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t at = 0; at < length; at += 8)
    {
        uint64_t word = 0;
        if (length - at >= 8)
        {
            memcpy(&word, name + at, sizeof(word));
        }
        else
        {
            for (uint32_t i = 0; i < length - at; i += 1)
            {
                word |= (uint64_t)(uint8_t)name[at + i] << (8 * i);
            }
        }
        hash = (hash ^ word) * UINT64_C(0xbf58476d1ce4e5b9);
        hash ^= hash >> 31;
    }
    hash = (hash ^ (hash >> 29)) * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 32;
    return hash;
}

// Makes builtin_intrinsic_table, once: before any lane may read it. A table that isn't made answers every name as none's.
static __attribute__((unused)) bool builtin_intrinsic_table_initialize(void)
{
    for (uint32_t row = 0; !builtin_intrinsic_table_ready && row < BUSTER_BUILTIN_INTRINSIC_COUNT; row += 1)
    {
        const uint64_t hash = builtin_intrinsic_table_hash(builtin_intrinsic_infos[row].name, builtin_intrinsic_infos[row].name_length);
        uint32_t slot = (uint32_t)hash & (BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS - 1);
        while (builtin_intrinsic_table[slot] != 0)
        {
            slot = (slot + 1) & (BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS - 1);
        }
        builtin_intrinsic_table[slot] = (uint32_t)(hash >> 48) << 16 | (row + 1);
    }
    builtin_intrinsic_table_ready = true;
    return builtin_intrinsic_table_ready;
}

// The row of a bounded name by the exact table (made: builtin_intrinsic_table_ready), or null; null or empty names miss.
// The same answer as builtin_intrinsic_lookup's.
static __attribute__((unused)) const BuiltinIntrinsicInfo* builtin_intrinsic_table_lookup(const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* result = NULL;
    const bool named = name != NULL && length != 0;
    const uint64_t hash = named ? builtin_intrinsic_table_hash(name, length) : 0;
    const uint32_t tag = (uint32_t)(hash >> 48);
    uint32_t slot = (uint32_t)hash & (BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS - 1);
    uint32_t entry = named ? builtin_intrinsic_table[slot] : 0;
    while (entry != 0 && result == NULL)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + (entry & 0xffffu) - 1;
        result = entry >> 16 == tag && info->name_length == length && memcmp(info->name, name, length) == 0 ? info : NULL;
        slot = (slot + 1) & (BUSTER_BUILTIN_INTRINSIC_TABLE_SLOTS - 1);
        entry = builtin_intrinsic_table[slot];
    }
    return result;
}

// Exact frozen workload membership is narrower than the complete installed catalogue. The two parser/type gap
// records retain their explicit non-prototype kinds here. Membership does not attest successful call checking.
static __attribute__((unused)) const BuiltinIntrinsicInfo* builtin_intrinsic_own_main_lookup(const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* result = builtin_intrinsic_lookup_in(builtin_intrinsic_own_main_infos, BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT, name, length);
    return result;
}

static __attribute__((unused)) bool builtin_intrinsic_own_main_x86_in_scope(const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* info = builtin_intrinsic_own_main_lookup(name, length);
    const bool result = info != 0 && (strcmp(info->shard, "x86") == 0 || strcmp(info->shard, "x86_64") == 0);
    return result;
}

// The x86 names past the frozen inventory whose prototype, immediate, feature and lowering contracts are separately
// audited (c_builtin_supplementary_scope.inc, in strcmp's order). The own-main workload inventory and its tests remain
// unchanged.
#define BUSTER_CBUILTIN_SUPPLEMENTARY_NAME(n, f) { n, (uint32_t)sizeof(n) - 1 },
static const BuiltinIntrinsicName builtin_intrinsic_supplementary_names[] = {
#include "c_builtin_supplementary_scope.inc"
};
#undef BUSTER_CBUILTIN_SUPPLEMENTARY_NAME

// Whether a name is one of the supplementary scope's: a binary search of its sorted names, by the bytes of the bounded
// slice alone.
static __attribute__((unused)) bool builtin_intrinsic_supplementary_in_scope(const char* name, uint32_t length)
{
    bool result = false;
    uint32_t lower = 0;
    uint32_t upper = name != NULL && length != 0 ? (uint32_t)(sizeof(builtin_intrinsic_supplementary_names) / sizeof(builtin_intrinsic_supplementary_names[0])) : 0;
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const BuiltinIntrinsicName* entry = builtin_intrinsic_supplementary_names + middle;
        const uint32_t common = length < entry->length ? length : entry->length;
        const int bytes = memcmp(name, entry->name, common);
        const int order = bytes != 0 ? bytes : (length > entry->length) - (length < entry->length);
        if (order == 0)
        {
            result = true;
            upper = lower;
        }
        else if (order < 0)
        {
            upper = middle;
        }
        else
        {
            lower = middle + 1;
        }
    }
    return result;
}

// The frozen scope's generic table builtins that no dedicated checker module owns (no CBuiltinId) and whose clang Sema
// checking is their prototype alone: llvmorg-23.1.1's SemaChecking.cpp has no case for the rotates or
// __builtin_unpredictable, and __builtin_assume's (BuiltinAssume, line 6624) only warns of an argument's side effects.
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaChecking.cpp
static const char* const builtin_intrinsic_prototype_names[] =
{
    "__builtin_assume",
    "__builtin_rotateleft16",
    "__builtin_rotateleft32",
    "__builtin_rotateleft64",
    "__builtin_rotateleft8",
    "__builtin_rotateright16",
    "__builtin_rotateright32",
    "__builtin_rotateright64",
    "__builtin_rotateright8",
    "__builtin_unpredictable",
};

// Whether a name is checked by its prototype (and an x86 one also by its SemaX86 rules): an x86 record of the frozen
// scope or its separately attested additions (the supplementary scope), or a generic one of builtin_intrinsic_prototype_names.
static __attribute__((unused)) bool builtin_intrinsic_own_main_prototype_in_scope(const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* info = builtin_intrinsic_own_main_lookup(name, length);
    bool result = builtin_intrinsic_own_main_x86_in_scope(name, length) || builtin_intrinsic_supplementary_in_scope(name, length);
    const bool generic = info != 0 && strcmp(info->shard, "generic") == 0 && strcmp(info->record_kind, "table-record") == 0;
    for (uint32_t i = 0; generic && !result && i < sizeof(builtin_intrinsic_prototype_names) / sizeof(builtin_intrinsic_prototype_names[0]); i += 1)
    {
        result = strlen(builtin_intrinsic_prototype_names[i]) == length && memcmp(builtin_intrinsic_prototype_names[i], name, length) == 0;
    }
    return result;
}

// Input/output slices are disjoint and bounded by count; zero count permits null. Positive count requires valid
// naturally aligned slices. Per-name null or empty slices simply miss. The vector tail never reads inactive names.
static __attribute__((unused)) void builtin_intrinsic_lookup_batch(const BuiltinIntrinsicName* inputs, const BuiltinIntrinsicInfo** outputs, uint32_t count, bool scalar)
{
    (void)scalar;

    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            outputs[i] = builtin_intrinsic_lookup(inputs[i].name, inputs[i].length);
        }
    }
    else
    {
        for (uint32_t base = 0; base < count;)
        {
            const uint32_t lanes = count - base < 8 ? count - base : 8;
            int32_t upper_lanes[8] = { 0 };
            const uint32_t candidates = builtin_intrinsic_filter_candidates(inputs + base, lanes, false);
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                outputs[base + lane] = 0;
                upper_lanes[lane] = (candidates & (1u << lane)) != 0 ? BUSTER_BUILTIN_INTRINSIC_COUNT : 0;
            }
            __m256i lower = _mm256_setzero_si256();
            __m256i upper = _mm256_loadu_si256((const __m256i*)upper_lanes);
            __m256i active = _mm256_cmpgt_epi32(upper, lower);
            while (_mm256_movemask_epi8(active) != 0)
            {
                const __m256i middle = _mm256_add_epi32(lower, _mm256_srli_epi32(_mm256_sub_epi32(upper, lower), 1));
                int32_t middle_lanes[8];
                int32_t orders[8] = { 0 };
                _mm256_storeu_si256((__m256i*)middle_lanes, middle);
                const uint32_t mask = (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(active));
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    if ((mask & (1u << lane)) != 0)
                    {
                        const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + middle_lanes[lane];
                        orders[lane] = builtin_intrinsic_compare(inputs + base + lane, info);
                        if (orders[lane] == 0)
                        {
                            outputs[base + lane] = info;
                        }
                    }
                }
                const __m256i order = _mm256_loadu_si256((const __m256i*)orders);
                const __m256i greater = _mm256_and_si256(active, _mm256_cmpgt_epi32(order, _mm256_setzero_si256()));
                const __m256i less = _mm256_and_si256(active, _mm256_cmpgt_epi32(_mm256_setzero_si256(), order));
                const __m256i next_lower = _mm256_blendv_epi8(lower, _mm256_add_epi32(middle, _mm256_set1_epi32(1)), greater);
                const __m256i next_upper = _mm256_blendv_epi8(next_lower, middle, less);
                // A match or an inactive lane closes its interval; unmatched lanes retain the opposite endpoint.
                upper = _mm256_blendv_epi8(next_upper, upper, greater);
                lower = next_lower;
                active = _mm256_cmpgt_epi32(upper, lower);
            }
            base += lanes;
        }
    }
}

#endif
