// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Build the same lexer comparison the draw runs, interposing only on its vector path's masked byte load.
// Both seeds keep the original load bounds. The scalar oracle and the benchmark's status are unchanged.
#include <immintrin.h>
#include <stdlib.h>
#include <string.h>

static const char* vector_seed;

static __m512i seeded_load(__m512i padding, __mmask64 valid, const void* address)
{
    if (strcmp(vector_seed, "tail") == 0 && valid == 1)
    {
        valid = 0; // Drop the 65th byte of a two-lane input.
    }
    __m512i bytes = _mm512_mask_loadu_epi8(padding, valid, address);
    if (strcmp(vector_seed, "token") == 0)
    {
        const __mmask64 plus = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('+'));
        bytes = _mm512_mask_set1_epi8(bytes, plus, '-');
    }
    return bytes;
}

#undef _mm512_mask_loadu_epi8
#define _mm512_mask_loadu_epi8 seeded_load
#include "buster/compiler/c_lexer_benchmark.c"

int main(int argc, char** argv)
{
    vector_seed = getenv("BUSTER_TEST_VECTOR_SEED");
    int status = 2;
    if (vector_seed != NULL && argc == 3 && strcmp(argv[1], "benchmark-lexer") == 0)
    {
        status = run_c_lexer_benchmark(argc - 1, argv + 1);
    }
    return status;
}
