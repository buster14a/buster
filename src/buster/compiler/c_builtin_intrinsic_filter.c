// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Negative-only filter for the existing eight-name catalog search (#893). Include after
// BuiltinIntrinsicName. Immutable 8 KiB state has no startup or per-unit construction/allocation cost.
// Irregular bounded loads supply eight keys; the mixing is SIMD and the bit probes are scalar loads,
// avoiding gathers. False positives always proceed to the existing exact comparison/search.
#ifndef BUSTER_C_BUILTIN_INTRINSIC_FILTER_INCLUDED
#define BUSTER_C_BUILTIN_INTRINSIC_FILTER_INCLUDED
#include "c_builtin_intrinsic_filter_hash.h"

static const uint64_t builtin_intrinsic_filter_bits[BUSTER_BUILTIN_INTRINSIC_FILTER_WORDS] =
{
#include "generated/clang_builtin_intrinsic_filter_23_1_1.inc"
};
_Static_assert(BUSTER_BUILTIN_INTRINSIC_FILTER_COUNT == BUSTER_BUILTIN_INTRINSIC_COUNT, "regenerate the intrinsic filter after catalog changes");

// At most eight input records, count zero permits null. Never read inactive records or a null/empty name.
// The scalar mixing is an exact twin of the vector mixing; the public scalar catalog lookup stays
// completely unfiltered and independently checks that filtering cannot hide a real catalog hit.
static uint32_t builtin_intrinsic_filter_candidates(const BuiltinIntrinsicName* inputs, uint32_t count, bool scalar)
{
    (void)scalar;
    uint32_t keys[8] = { 0 };
    uint32_t hashes[8] = { 0 };
    uint32_t valid = 0;
    for (uint32_t lane = 0; lane < count; lane += 1)
    {
        if (inputs[lane].name != NULL && inputs[lane].length != 0)
        {
            keys[lane] = builtin_intrinsic_filter_key(inputs[lane].name, inputs[lane].length);
            valid |= 1u << lane;
        }
    }
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            hashes[lane] = builtin_intrinsic_filter_hash(keys[lane]);
        }
    }
    else
    {
        const __m256i key = _mm256_loadu_si256((const __m256i*)keys);
        const __m256i mixed = _mm256_mullo_epi32(_mm256_xor_si256(key, _mm256_srli_epi32(key, 16)), _mm256_set1_epi32(0x7feb352d));
        const __m256i hash = _mm256_and_si256(_mm256_xor_si256(mixed, _mm256_srli_epi32(mixed, 15)), _mm256_set1_epi32(BUSTER_BUILTIN_INTRINSIC_FILTER_BITS - 1u));
        _mm256_storeu_si256((__m256i*)hashes, hash);
    }
    uint32_t result = 0;
    for (uint32_t lane = 0; lane < count; lane += 1)
    {
        if ((valid & (1u << lane)) != 0)
        {
            const uint32_t hash = hashes[lane];
            const uint32_t present = (uint32_t)((builtin_intrinsic_filter_bits[hash / 64] >> (hash % 64)) & 1u);
            result |= present << lane;
        }
    }
    return result;
}
#endif
