// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone SSA work contracts. Allocation wrappers exist only in this translation unit, around the production
// modules under test; the compiler and its benchmark builds include neither this file nor these counters.

#if defined(BUSTER_EXECUTABLE_UNIT) || defined(BUSTER_UNIT)
#error "SSA work contracts must not be included in a compiler executable"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "buster/compiler/work_contract.h"
#include "buster/compiler/lane.c"

static uint64_t ssa_work_requested_bytes;
static bool ssa_work_request_overflow;

// Count requested payload, including requests that fail. A recycled temporary region is charged again, so a dense
// request cannot disappear when an operation restores its arena mark. Alignment slack and OS mapping policy are
// deliberately outside this metric. The fixtures and their semantic oracle's own buffers use the original API.
static void* ssa_work_arena_push(Arena* arena, uint64_t bytes, uint64_t alignment)
{
    uint64_t total = 0;
    const bool overflow = __builtin_add_overflow(ssa_work_requested_bytes, bytes, &total);
    ssa_work_request_overflow |= overflow;
    ssa_work_requested_bytes = overflow ? UINT64_MAX : total;
    return arena_push(arena, bytes, alignment);
}

static void* ssa_work_arena_push_array(Arena* arena, uint64_t count, uint64_t bytes, uint64_t alignment)
{
    uint64_t size = 0;
    void* result = NULL;
    if (__builtin_mul_overflow(count, bytes, &size))
    {
        ssa_work_request_overflow = true;
        ssa_work_requested_bytes = UINT64_MAX;
    }
    else
    {
        result = ssa_work_arena_push(arena, size, alignment);
    }
    return result;
}

#define arena_push ssa_work_arena_push
#define arena_push_array ssa_work_arena_push_array
#include "buster/compiler/dataflow.c"
#include "buster/compiler/dominators.c"
#ifndef BUSTER_WORK_SSA_SOURCE
#define BUSTER_WORK_SSA_SOURCE "buster/compiler/ssa.c"
#endif
#include BUSTER_WORK_SSA_SOURCE
#undef arena_push_array
#undef arena_push

#include "buster/compiler/ssa_work_contract_test.c"

int main(void)
{
    return ssa_work_contracts() ? 0 : 1;
}
