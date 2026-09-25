// Branch-only mechanism witness. Uses the real persistent lane gang, allocator,
// entry/teardown, and existing reserve-failure seam. No timings are collected.
#define BUSTER_UNITY_BUILD 1
#define BUSTER_INCLUDE_TESTS 1
#include <buster/lib/base.h>
#include <buster/lib/os.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>
#include <stdio.h>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/arena.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/integer.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/target.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/compiler/assembly/x86_64_metadata.c>
#endif

enum { OWNER_PROBE_MAX_LANES = 2, OWNER_PROBE_WORDS = 128, OWNER_PROBE_COHORTS = 15 };
typedef enum OwnerProbePhase
{
    OWNER_PROBE_CLEAR,
    OWNER_PROBE_PRODUCE,
    OWNER_PROBE_RECLAIM,
    OWNER_PROBE_REUSE,
} OwnerProbePhase;
typedef struct OwnerProbe OwnerProbe;
struct OwnerProbe
{
    Arena* arenas[OWNER_PROBE_MAX_LANES];
    u64* payloads[OWNER_PROBE_MAX_LANES];
    u64 released[OWNER_PROBE_MAX_LANES];
    bool reuse[OWNER_PROBE_MAX_LANES];
    bool hook_consumed[OWNER_PROBE_MAX_LANES];
    u32 observed_lanes;
    u32 generation;
    OwnerProbePhase phase;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType owner_probe_lane(void* argument)
{
    OwnerProbe* probe = (OwnerProbe*)argument;
    u32 index = (u32)lane_index();
    if (!index)
    {
        probe->observed_lanes = (u32)lane_count();
    }
    if (probe->phase == OWNER_PROBE_CLEAR)
    {
        probe->released[index] = arena_pool_release_thread();
    }
    else if (probe->phase == OWNER_PROBE_RECLAIM)
    {
        if (probe->arenas[index])
        {
            arena_destroy(probe->arenas[index], 1);
            probe->arenas[index] = 0;
            probe->payloads[index] = 0;
        }
    }
    else
    {
        bool testing_reuse = probe->phase == OWNER_PROBE_REUSE;
        if (testing_reuse)
        {
            arena_test_fail_next_reserve();
        }
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(32), .flags = {.pool_reuse = 1}});
        probe->arenas[index] = arena;
        probe->payloads[index] = 0;
        if (testing_reuse)
        {
            probe->reuse[index] = arena != 0;
            probe->hook_consumed[index] = true;
            if (arena)
            {
                // Pool hits bypass reservation and leave the one-shot hook armed.
                // Consume it on a deliberately nonpooled creation before returning.
                Arena* control = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = 1}});
                probe->hook_consumed[index] = control == 0;
                if (control)
                {
                    arena_destroy(control, 1);
                }
            }
        }
        if (arena)
        {
            u64* words = arena_allocate(arena, u64, OWNER_PROBE_WORDS);
            for (u32 word = 0; word < OWNER_PROBE_WORDS; word += 1)
            {
                words[word] = (u64)probe->generation * 10000 + (u64)index * 1000 + word;
            }
            probe->payloads[index] = words;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool owner_probe_consume(OwnerProbe* probe)
{
    bool result = true;
    for (u32 index = 0; index < probe->observed_lanes; index += 1)
    {
        bool present = probe->arenas[index] && probe->payloads[index];
        result = result && present;
        if (present)
        {
            for (u32 word = 0; word < OWNER_PROBE_WORDS; word += 1)
            {
                result = result && probe->payloads[index][word] == (u64)probe->generation * 10000 + (u64)index * 1000 + word;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void owner_probe_caller_reclaim(OwnerProbe* probe)
{
    for (u32 index = 0; index < probe->observed_lanes; index += 1)
    {
        if (probe->arenas[index])
        {
            arena_destroy(probe->arenas[index], 1);
            probe->arenas[index] = 0;
            probe->payloads[index] = 0;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool owner_probe_case(u32 requested, bool return_to_owner)
{
    OwnerProbe probe = {.phase = OWNER_PROBE_CLEAR};
    lane_run(requested, &owner_probe_lane, &probe);
    probe.phase = OWNER_PROBE_PRODUCE;
    probe.generation = 1;
    lane_run(requested, &owner_probe_lane, &probe);
    bool result = owner_probe_consume(&probe);
    // Completion precedes every last-consumer read above. Reclamation is later.
    if (return_to_owner)
    {
        probe.phase = OWNER_PROBE_RECLAIM;
        lane_run(requested, &owner_probe_lane, &probe);
    }
    else
    {
        owner_probe_caller_reclaim(&probe);
    }
    probe.phase = OWNER_PROBE_REUSE;
    probe.generation = 2;
    lane_run(requested, &owner_probe_lane, &probe);
    for (u32 index = 0; index < probe.observed_lanes; index += 1)
    {
        bool expected = return_to_owner || index == 0;
        result = result && probe.reuse[index] == expected && probe.hook_consumed[index];
        printf("OWNER_REUSE owner_return=%u requested=%u active=%u lane=%u hit=%u expected=%u hook_consumed=%u\n",
               (u32)return_to_owner, requested, probe.observed_lanes, index, (u32)probe.reuse[index], (u32)expected,
               (u32)probe.hook_consumed[index]);
    }
    owner_probe_caller_reclaim(&probe);
    // A deterministic allocation failure must not contaminate the next dispatch.
    probe.phase = OWNER_PROBE_PRODUCE;
    probe.generation = 3;
    lane_run(requested, &owner_probe_lane, &probe);
    result = owner_probe_consume(&probe) && result;
    owner_probe_caller_reclaim(&probe);
    probe.phase = OWNER_PROBE_CLEAR;
    lane_run(requested, &owner_probe_lane, &probe);
    return result;
}

BUSTER_GLOBAL_LOCAL bool owner_probe_accumulation(u32 requested)
{
    OwnerProbe probe = {.phase = OWNER_PROBE_CLEAR};
    lane_run(requested, &owner_probe_lane, &probe);
    bool result = true;
    for (u32 cohort = 0; cohort < OWNER_PROBE_COHORTS; cohort += 1)
    {
        probe.phase = OWNER_PROBE_PRODUCE;
        probe.generation = cohort + 1;
        lane_run(requested, &owner_probe_lane, &probe);
        result = owner_probe_consume(&probe) && result;
        owner_probe_caller_reclaim(&probe);
    }
    probe.phase = OWNER_PROBE_CLEAR;
    lane_run(requested, &owner_probe_lane, &probe);
    for (u32 index = 0; index < probe.observed_lanes; index += 1)
    {
        u64 expected = index ? 0 : BUSTER_MIN((u64)16, (u64)probe.observed_lanes +
                                               (OWNER_PROBE_COHORTS - 1) * (u64)(probe.observed_lanes - 1));
        result = result && probe.released[index] == expected;
        printf("OWNER_POOL cohorts=%u active=%u lane=%u released=%llu expected=%llu\n", OWNER_PROBE_COHORTS,
               probe.observed_lanes, index, (unsigned long long)probe.released[index], (unsigned long long)expected);
    }
    return result;
}

ProcessResult process_arguments(void)
{
    return PROCESS_RESULT_SUCCESS;
}

ProcessResult entry_point(void)
{
    bool success = BUSTER_SINGLE_THREADED || os_get_logical_thread_count() >= OWNER_PROBE_MAX_LANES;
    if (success)
    {
        u32 requests[] = {1, 2, 1, 2};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(requests); index += 1)
        {
            success = owner_probe_case(requests[index], false) && success;
            success = owner_probe_case(requests[index], true) && success;
        }
        success = owner_probe_accumulation(2) && success;
    }
    printf("OWNER_RESULT status=%s single_threaded=%u evidence=mechanism-only timing=none\n", success ? "pass" : "fail",
           (u32)BUSTER_SINGLE_THREADED);
    return success ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
