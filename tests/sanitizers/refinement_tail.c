// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// All 16-bit placement masks against independent expected values. Only live
// compressed IDs are initialized: MSan must reject a full-vector tail load.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#define BUSTER_ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"

static __attribute__((noinline)) bool test_mask(uint16_t mask)
{
    RefineIdentifiers identifiers;
    memset(identifiers.masks, 0, sizeof(identifiers.masks));
    identifiers.masks[0] = mask;
    uint32_t live = 0;
    uint32_t values[16];
    for (uint32_t lane = 0; lane < 16; lane += 1)
    {
        values[lane] = UINT32_MAX;
        if (((uint32_t)mask & (1u << lane)) != 0)
        {
            identifiers.ids[live] = 100 + lane;
            live += 1;
        }
    }
    refine_pass_c(&identifiers, 0, 16, values);
    bool result = true;
    for (uint32_t lane = 0; lane < 16; lane += 1)
    {
        const uint32_t expected = ((uint32_t)mask & (1u << lane)) != 0 ? 100 + lane : UINT32_MAX;
        result = result && values[lane] == expected;
    }
    return result;
}

int main(void)
{
    bool result = true;
    for (uint32_t mask = 0; result && mask <= UINT16_MAX; mask += 1)
    {
        result = test_mask((uint16_t)mask);
    }
    puts(result ? "refinement tails: 65536 masks pass" : "refinement tail mismatch");
    return result ? 0 : 1;
}
