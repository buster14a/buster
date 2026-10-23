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
#include <immintrin.h>

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

static const BuiltinIntrinsicInfo builtin_intrinsic_infos[] =
{
#define BUSTER_BUILTIN_METADATA(n, t, a, f, h, l, s, line, ordinal, custom, evaluable, kind, constraints) \
    { n, t, a, f, h, l, s, kind, sizeof(n) - 1, sizeof(t) - 1, sizeof(a) - 1, sizeof(f) - 1, line, ordinal, custom, evaluable },
#include "generated/clang_builtin_metadata_23_1_1.inc"
#undef BUSTER_BUILTIN_METADATA
};

#define BUSTER_BUILTIN_INTRINSIC_COUNT ((uint32_t)(sizeof(builtin_intrinsic_infos) / sizeof(builtin_intrinsic_infos[0])))
_Static_assert(BUSTER_BUILTIN_INTRINSIC_COUNT > 0 && BUSTER_BUILTIN_INTRINSIC_COUNT < INT32_MAX, "bounded search indices");

typedef struct BuiltinIntrinsicName BuiltinIntrinsicName;
struct BuiltinIntrinsicName
{
    const char* name;
    uint32_t length;
};

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

static __attribute__((unused)) const BuiltinIntrinsicInfo* builtin_intrinsic_lookup(const char* name, uint32_t length)
{
    const BuiltinIntrinsicInfo* result = 0;
    uint32_t lower = 0;
    uint32_t upper = name != 0 && length != 0 ? BUSTER_BUILTIN_INTRINSIC_COUNT : 0;
    const BuiltinIntrinsicName input = { name, length };
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_infos + middle;
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

// Input/output slices are disjoint and bounded by count; zero count permits null. Positive count requires valid
// naturally aligned slices. Per-name null or empty slices simply miss. The vector tail never reads inactive names.
static __attribute__((unused)) void builtin_intrinsic_lookup_batch(const BuiltinIntrinsicName* inputs, const BuiltinIntrinsicInfo** outputs, uint32_t count, bool scalar)
{
    if (scalar)
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
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                outputs[base + lane] = 0;
                upper_lanes[lane] = inputs[base + lane].name != 0 && inputs[base + lane].length != 0 ? BUSTER_BUILTIN_INTRINSIC_COUNT : 0;
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
