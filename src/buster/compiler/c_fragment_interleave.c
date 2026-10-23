// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded byte routing for the expression-fragment experiment (#106). Include before c_fragments.c. A fragment
// supplies count live positions and operator_mask; each set bit consumes the next operator byte, each clear bit
// the next leaf byte. c_fragment_interleave and its scalar twin produce those bytes in position order.
//
// count is 0..64 and no mask bit may lie outside it. Invalid metadata returns false without accessing any buffer.
// The operator and leaf slices contain exactly popcount(operator_mask) and count-popcount(operator_mask) bytes;
// output has count writable bytes. No padding or alignment is required. An empty slice may be null or point at a
// PROT_NONE page. The caller owns every buffer through the call; output must not overlap either live input slice.
// Input slices may overlap each other. No bytes outside the slices are read or written and there is no allocation.
//
// This is an original implementation of the intrinsic sequence specified by #106, not imported research code.
// Routing kinds alone does not implement the pending semantic consumer, its entry contexts, diagnostics or IR.

#ifndef BUSTER_C_FRAGMENT_INTERLEAVE_INCLUDED
#define BUSTER_C_FRAGMENT_INTERLEAVE_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <immintrin.h>

#define BUSTER_C_FRAGMENT_INTERLEAVE_BYTES 64u

// A saturated mask also keeps invalid counts safe while the entry points reject them. In particular, 64 never
// reaches the shift expression; count == 0 produces an empty mask.
static uint64_t c_fragment_interleave_mask(uint32_t count)
{
    return count < BUSTER_C_FRAGMENT_INTERLEAVE_BYTES ? (UINT64_C(1) << count) - 1 : UINT64_MAX;
}

static bool c_fragment_interleave_scalar(const uint8_t* operators, const uint8_t* leaves, uint64_t operator_mask, uint32_t count, uint8_t* output)
{
    const uint64_t valid_mask = c_fragment_interleave_mask(count);
    const bool valid = count <= BUSTER_C_FRAGMENT_INTERLEAVE_BYTES && (operator_mask & ~valid_mask) == 0;
    if (valid)
    {
        uint32_t operator_cursor = 0;
        uint32_t leaf_cursor = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            if ((operator_mask >> i) & 1)
            {
                output[i] = operators[operator_cursor++];
            }
            else
            {
                output[i] = leaves[leaf_cursor++];
            }
        }
    }
    return valid;
}

static bool c_fragment_interleave(const uint8_t* operators, const uint8_t* leaves, uint64_t operator_mask, uint32_t count, uint8_t* output)
{
    const uint64_t valid_mask = c_fragment_interleave_mask(count);
    const bool valid = count <= BUSTER_C_FRAGMENT_INTERLEAVE_BYTES && (operator_mask & ~valid_mask) == 0;
    if (valid && count != 0)
    {
        const uint32_t operator_count = (uint32_t)__builtin_popcountll(operator_mask);
        const uint32_t leaf_count = count - operator_count;
        const uint64_t operand_mask = valid_mask ^ operator_mask;
        const __m512i packed_operators = _mm512_maskz_loadu_epi8((__mmask64)c_fragment_interleave_mask(operator_count), operators);
        const __m512i packed_leaves = _mm512_maskz_loadu_epi8((__mmask64)c_fragment_interleave_mask(leaf_count), leaves);
        const __m512i expanded_operators = _mm512_maskz_expand_epi8((__mmask64)operator_mask, packed_operators);
        const __m512i interleaved = _mm512_mask_expand_epi8(expanded_operators, (__mmask64)operand_mask, packed_leaves);
        _mm512_mask_storeu_epi8(output, (__mmask64)valid_mask, interleaved);
    }
    return valid;
}

#endif
