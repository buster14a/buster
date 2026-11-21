// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Deterministic work-contract reports (#2041), for standalone correctness tests only. Each record names a fixture,
// its input size and an operation-specific upper bound; a small count never excuses an incorrect semantic result.
// Fixture and metric are fixed, whitespace-free identifiers owned by the test. No clock or process resource enters
// the verdict. The helper observes counters supplied by the caller and instruments no compiler path.

#if defined(BUSTER_EXECUTABLE_UNIT) || defined(BUSTER_UNIT)
#error "work contracts belong to standalone correctness tests, not the compiler executable"
#endif

#ifndef BUSTER_WORK_CONTRACT_INCLUDED
#define BUSTER_WORK_CONTRACT_INCLUDED

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct WorkContract WorkContract;
struct WorkContract
{
    const char* fixture;
    const char* metric;
    uint64_t input;
    uint64_t actual;
    uint64_t bound;
    bool semantic;
};

// Every attempted contract produces its own record, including failures. Flush the small report so a buffered write
// failure cannot leave an apparently successful contract without its evidence; callers also include this result in
// their suite's exit status. The output stream remains owned by the caller.
static bool work_contract_report(FILE* output, const WorkContract* contract)
{
    const bool passed = contract->semantic && contract->actual <= contract->bound;
    const int written = fprintf(output, "work-contract fixture=%s size=%" PRIu64 " metric=%s actual=%" PRIu64 " bound=%" PRIu64 " semantic=%s result=%s\n",
                                contract->fixture, contract->input, contract->metric, contract->actual, contract->bound,
                                contract->semantic ? "pass" : "fail", passed ? "pass" : "fail");
    const bool flushed = fflush(output) == 0;
    return passed && written >= 0 && flushed && !ferror(output);
}

#endif
