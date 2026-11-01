// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <stdlib.h>
#include "../../src/buster/compiler/tier1_problem.c"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Tier1Problem p = { 0 };
    if (tier1_problem_decode(data, size, &p) == TIER1_PROBLEM_OK)
    {
        uint8_t* copy = malloc(size);
        if (copy != 0)
        {
            size_t written = 0;
            if (tier1_problem_encode(&p, copy, size, &written) != TIER1_PROBLEM_OK || written != size || memcmp(data, copy, size) != 0)
            {
                abort();
            }
            free(copy);
        }
    }
    return 0;
}
