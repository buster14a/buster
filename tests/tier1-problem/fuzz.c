// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// The codec's round trip on every input. With BUSTER_TP_INTEGRATION, every problem that decodes is also allocated by
// both of buster's allocator paths as #938's harness reports it (tier1_problem_allocate): an allocation that fails its
// certificate (#939) aborts, since the allocator made it or the checker refused a correct one.
#define _GNU_SOURCE
#include <stdlib.h>
#include "../../src/buster/compiler/tier1_problem.c"
#ifdef BUSTER_TP_INTEGRATION
#include "../../src/buster/compiler/lane.c"
#include "../../src/buster/compiler/tier1_allocate.c"
#include "../../src/buster/compiler/tier1_certificate.c"
#include "../../src/buster/compiler/tier1_certificate_interval.c"
#include "../../src/buster/compiler/tier1_problem_intervals.c"
#endif

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
#ifdef BUSTER_TP_INTEGRATION
        static _Alignas(64) uint8_t memory[UINT32_C(1) << 22];
        Arena arena = { .base = memory, .length = 0, .capacity = sizeof(memory) };
        Tier1ProblemAllocation made = { 0 };
        if (tier1_problem_allocate(&p, &arena, &made) == TIER1_PROBLEM_CERTIFICATE_REJECTED)
        {
            abort();
        }
#endif
    }
    return 0;
}
