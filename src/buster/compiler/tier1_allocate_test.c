// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Test coordinator: preserve the existing fixtures verbatim in *_cases.c and
// check every successful allocator call they make, including nested suites.
// The allocator's arena and return status are untouched. The fragment sweep
// checks the present producer; the independent stream fixtures exercise the
// stronger certificate contract without pretending fragments are machine code.

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "buster/compiler/tier1_certificate.c"
#include "buster/compiler/tier1_certificate_oracle.c"
#include "buster/compiler/tier1_certificate_interval.c"
#include "buster/compiler/tier1_certificate_test.c"
#include "buster/compiler/tier1_certificate_interval_test.c"

static uint64_t tier1_certificate_checked_allocations;
static uint64_t tier1_certificate_allocation_failures;

static void tier1_certificate_test_allocation(const Tier1AllocationInput* input, const Tier1Allocation* allocation)
{
    const size_t capacity = tier1_interval_scratch_size(input, allocation);
    void* scratch = malloc(capacity);
    Tier1IntervalReport report = { 0 };
    const bool valid = scratch != 0 && tier1_allocation_certificate_check(input, allocation, scratch, capacity, &report) == TIER1_CERT_OK;
    tier1_certificate_checked_allocations += 1;
    if (!valid)
    {
        if (tier1_certificate_allocation_failures < 20)
        {
            fprintf(stderr, "allocation certificate failed: error %u, intervals %u, fragments %u\n", (unsigned)report.error, input->count, allocation->fragment_count);
        }
        tier1_certificate_allocation_failures += 1;
    }
    free(scratch);
}

static Tier1AllocationError tier1_certificate_test_allocate(const Tier1AllocationInput* input, Arena* arena, Tier1Allocation* result)
{
    const Tier1AllocationError error = tier1_allocate(input, arena, result);
    if (error == TIER1_ALLOCATION_OK)
    {
        tier1_certificate_test_allocation(input, result);
    }
    return error;
}

static Tier1AllocationError tier1_certificate_test_allocate_scalar(const Tier1AllocationInput* input, Arena* arena, Tier1Allocation* result)
{
    const Tier1AllocationError error = tier1_allocate_scalar(input, arena, result);
    if (error == TIER1_ALLOCATION_OK)
    {
        tier1_certificate_test_allocation(input, result);
    }
    return error;
}

// These macros are confined to test fixtures. Production allocation and its
// transactional capacity tests retain their original storage and error paths.
#define tier1_allocate tier1_certificate_test_allocate
#define tier1_allocate_scalar tier1_certificate_test_allocate_scalar
#define run_tier1_allocate_tests run_tier1_allocate_cases
#include "buster/compiler/tier1_allocate_test_cases.c"
#undef run_tier1_allocate_tests
#undef tier1_allocate_scalar
#undef tier1_allocate

static int run_tier1_allocate_tests(int argc, char** argv)
{
    tier1_certificate_checked_allocations = 0;
    tier1_certificate_allocation_failures = 0;
    int result = run_tier1_allocate_cases(argc, argv);
    result |= run_tier1_certificate_tests();
    result |= run_tier1_certificate_interval_tests();
    printf("allocation certificates: %" PRIu64 " successful allocations checked, %" PRIu64 " failures\n", tier1_certificate_checked_allocations, tier1_certificate_allocation_failures);
    result |= tier1_certificate_allocation_failures != 0;
    return result;
}
