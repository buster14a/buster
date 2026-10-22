// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// One lexer variant as its own translation unit, for benchmark-lexer's comparisons inside one process: `./build.sh
// lexer-variant NAME=SOURCE...` compiles this file once per variant, with BUSTER_LEXER_VARIANT_SOURCE the path of a
// copy of c_lexer.c and BUSTER_LEXER_VARIANT_NAME the function it exports, c_lexer_variant_NAME, which is that copy's
// c_tokenize. Each copy's static functions stay its own, so the variants link beside the tree's lexer.

#include <stdint.h>

#include BUSTER_LEXER_VARIANT_SOURCE

int64_t BUSTER_LEXER_VARIANT_NAME(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

int64_t BUSTER_LEXER_VARIANT_NAME(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    return c_tokenize(source, length, kinds, starts, ends, flags);
}
