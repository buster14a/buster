// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent raw-byte corpus: compile with Clang -O3 -march=znver5 -c, then extract .text with objcopy.
#include <stdint.h>
#include <immintrin.h>

uint64_t disassembler_sum(const uint32_t* values, uint64_t count)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        result += values[i];
    }
    return result;
}

void disassembler_map(float* restrict output, const float* restrict input, uint64_t count, float scale, float add)
{
    for (uint64_t i = 0; i < count; i += 1)
    {
        output[i] = input[i] * scale + add;
    }
}

void disassembler_masked(float* output, const float* input, uint64_t count, float threshold)
{
    for (uint64_t i = 0; i + 16 <= count; i += 16)
    {
        const __m512 values = _mm512_loadu_ps(input + i);
        const __mmask16 selected = _mm512_cmp_ps_mask(values, _mm512_set1_ps(threshold), _CMP_GT_OQ);
        const __m512 mapped = _mm512_maskz_add_ps(selected, values, _mm512_set1_ps(1.0f));
        _mm512_storeu_ps(output + i, mapped);
    }
}

uint64_t disassembler_bits(uint64_t bits, uint64_t selector)
{
    return _pext_u64(bits, selector) ^ _blsr_u64(bits) ^ _andn_u64(bits, selector);
}
