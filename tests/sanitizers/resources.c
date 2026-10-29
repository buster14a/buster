// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Diagnostic failure injection must affect compiler resources alone, preserve
// one-lane work, and leave normal reservations available after restoration.
#define _GNU_SOURCE
#include <stdio.h>
#include "buster/compiler/lane.c"

static void test_entry(void* argument)
{
    uint32_t* calls = argument;
    *calls += 1;
}

int main(void)
{
    uint32_t calls = 0;
    bool result = lane_run(1, test_entry, &calls, 0) && calls == 1;
#if BUSTER_SANITIZER_RESOURCES
    thread_context.test_no_threads = true;
    LaneTeam team = { 0 };
    result = !lane_run(2, test_entry, &calls, 0) && !lane_team_start(&team, 2, 0) && calls == 1 && result;
    result = lane_run(1, test_entry, &calls, 0) && calls == 2 && result;
    thread_context.test_no_threads = false;
    thread_context.test_arena_limited = true;
    thread_context.test_arena_limit = BUSTER_ARENA_PAGE_BYTES;
    Arena small = arena_reserve(BUSTER_ARENA_PAGE_BYTES);
    Arena large = arena_reserve(BUSTER_ARENA_PAGE_BYTES + 1);
    result = small.base != 0 && large.base == 0 && errno == ENOMEM && result;
    arena_release(&large);
    arena_release(&small);
    thread_context.test_arena_limited = false;
#endif
    Arena restored = arena_reserve(BUSTER_ARENA_PAGE_BYTES * 2);
    result = restored.base != 0 && result;
    arena_release(&restored);
    scratch_release();
    puts(result ? "sanitizer resource boundaries: pass" : "sanitizer resource boundaries: FAIL");
    return result ? 0 : 1;
}
