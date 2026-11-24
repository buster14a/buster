// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Masked gathers at unsigned 32-bit indices (#3448). AVX-512's gathers sign-extend their 32-bit indices, so an index past
// INT32_MAX would read before the base. These take the lanes whose index has its top bit by a second gather from 2^31
// elements further on, with the index less 2^31; a step with no such lane (every realistic one) skips it by one branch
// on the mask. The second gather keeps the first one's lanes, so the result is the same as one gather at 64-bit indices.
#ifndef BUSTER_GATHER_H
#define BUSTER_GATHER_H

#include "host_isa.h"
#include <stdint.h>

// 32-bit elements at `base` (a scale of 4): `source`'s lanes where `mask` is clear.
static inline __attribute__((always_inline)) __m512i buster_gather_u32(__m512i source, __mmask16 mask, __m512i indices, const uint32_t* base)
{
    const __mmask16 upper = _mm512_mask_test_epi32_mask(mask, indices, _mm512_set1_epi32(INT32_MIN));
    __m512i values = _mm512_mask_i32gather_epi32(source, mask & (__mmask16)~upper, indices, base, 4);
    if (upper != 0)
    {
        values = _mm512_mask_i32gather_epi32(values, upper, _mm512_xor_si512(indices, _mm512_set1_epi32(INT32_MIN)), base + UINT32_C(0x80000000), 4);
    }
    return values;
}

#endif
