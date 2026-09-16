#include <buster/tests/arena_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/os.h>
#include <buster/lib/os_internal.h>

UnitTestResult arena_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

#if BUSTER_BENCH_ALLOCATIONS
    {
        Arena* measured = arena_create((ArenaCreation){.reserved_size = BUSTER_KB(64), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
        ArenaBenchmarkCounters before = arena_benchmark_counters();
        arena_allocate_bytes(measured, 0, 1);
        u8* first = arena_allocate(measured, u8, 3);
        arena_allocate_bytes(measured, 5, 16);
        arena_allocate_zeroed(measured, u8, 17);
        memset(first, 0xa5, (size_t)(measured->position - arena_minimum_position));
        arena_reset_to_start(measured);
        u8* reused = (u8*)arena_allocate_zeroed_bytes(measured, 9, 16);
        u8* partly_fresh = arena_allocate_zeroed(measured, u8, 31);
        ArenaBenchmarkCounters after = arena_benchmark_counters();
        BUSTER_TEST(arguments, after.calls - before.calls == 6);
        BUSTER_TEST(arguments, after.requested_bytes - before.requested_bytes == 65);
        BUSTER_TEST(arguments, after.padding - before.padding == 13);
        BUSTER_TEST(arguments, after.empty - before.empty == 1);
        BUSTER_TEST(arguments, after.small - before.small == 5);
        BUSTER_TEST(arguments, after.zero_requested - before.zero_requested == 57);
        BUSTER_TEST(arguments, after.zero_written - before.zero_written == 38);
        BUSTER_TEST(arguments, after.maximum == BUSTER_MAX(before.maximum, 31));
        for (u32 index = 0; index < 9; index += 1)
        {
            BUSTER_TEST(arguments, reused[index] == 0);
        }
        for (u32 index = 0; index < 31; index += 1)
        {
            BUSTER_TEST(arguments, partly_fresh[index] == 0);
        }
        // Addressed/parenthesized calls remain counted, including the zeroed
        // wrapper's underlying bump exactly once.
        before = arena_benchmark_counters();
        void* (*raw_allocate)(Arena*, u64, u64) = &arena_allocate_bytes;
        raw_allocate(measured, 2, 1);
        (arena_allocate_zeroed_bytes)(measured, 4, 1);
        after = arena_benchmark_counters();
        BUSTER_TEST(arguments, after.calls - before.calls == 2);
        BUSTER_TEST(arguments, after.requested_bytes - before.requested_bytes == 6);
        BUSTER_TEST(arguments, after.zero_requested - before.zero_requested == 4);
        arena_destroy(measured, 1);
    }
    {
        // Exercise failure accounting through the recorder, without issuing a
        // deliberately failing native memory operation. OS traffic stays out
        // of logical arena totals.
        ArenaBenchmarkCounters arena_before = arena_benchmark_counters();
        ArenaBenchmarkCounters before = arena_benchmark_kind_counters(ARENA_BENCHMARK_OS_COMMIT);
        arena_benchmark_event(ARENA_BENCHMARK_OS_COMMIT, S8(__FILE__), S8(__func__), __LINE__, 17, 0, 0, 0, false);
        ArenaBenchmarkCounters after = arena_benchmark_kind_counters(ARENA_BENCHMARK_OS_COMMIT);
        BUSTER_TEST(arguments, after.calls - before.calls == 1);
        BUSTER_TEST(arguments, after.requested_bytes - before.requested_bytes == 17);
        BUSTER_TEST(arguments, after.failures - before.failures == 1);
        BUSTER_TEST(arguments, after.failed_bytes - before.failed_bytes == 17);
        BUSTER_TEST(arguments, arena_benchmark_counters().calls == arena_before.calls);
    }
#endif

#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS
    String8 failure_mode = os_get_environment_variable(S8("BUSTER_ARENA_FAILURE_MODE"));
    // Requesting prefaulting changes none of this: the commit is what the
    // arena depends on, so its failure stays fatal and keeps its diagnostic.
    bool prefaulting_mode = string_equal(failure_mode, S8("commit_prefault"));
    if (string_equal(failure_mode, S8("commit")) || prefaulting_mode || string_equal(failure_mode, S8("bound")) ||
        string_equal(failure_mode, S8("alignment_zero")) || string_equal(failure_mode, S8("alignment_three")) ||
        string_equal(failure_mode, S8("alignment_huge")) || string_equal(failure_mode, S8("cursor_low")) ||
        string_equal(failure_mode, S8("cursor_high")) || string_equal(failure_mode, S8("decommit_low")) ||
        string_equal(failure_mode, S8("decommit_high")))
    {
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(1),
            .initial_size = BUSTER_KB(64),
            .flags = {.no_pool = 1, .prefault_pages = prefaulting_mode},
        });
        BUSTER_VALIDATE(arena != 0);
        if (string_equal(failure_mode, S8("commit")) || prefaulting_mode)
        {
            arena_test_fail_next_commit();
            arena_allocate_bytes(arena, BUSTER_KB(128), 1);
        }
        else if (string_equal(failure_mode, S8("bound")))
        {
            // This is caller-derived validation, not an invariant. It must
            // still fail in an optimized build rather than becoming UB.
            arena_allocate_bytes(arena, arena->reserved_size, 1);
        }
        else if (string_equal(failure_mode, S8("alignment_zero")))
        {
            arena_allocate_bytes(arena, 1, 0);
        }
        else if (string_equal(failure_mode, S8("alignment_three")))
        {
            arena_allocate_bytes(arena, 1, 3);
        }
        else if (string_equal(failure_mode, S8("alignment_huge")))
        {
            // This alignment is valid by itself, but its rounded offset is
            // outside the reservation and must be rejected before committing.
            arena_allocate_bytes(arena, 1, (u64)1 << 63);
        }
        else if (string_equal(failure_mode, S8("cursor_low")))
        {
            arena_set_position(arena, arena_minimum_position - 1);
        }
        else if (string_equal(failure_mode, S8("cursor_high")))
        {
            arena_set_position(arena, arena->reserved_size + 1);
        }
        else if (string_equal(failure_mode, S8("decommit_low")))
        {
            arena_set_position_and_decommit(arena, arena_minimum_position - 1);
        }
        else
        {
            arena_set_position_and_decommit(arena, arena->position + 1);
        }
        BUSTER_UNREACHABLE();
    }
#endif

    // Companion to the reserved_size bound: filling an arena up to its
    // reservation stays within bounds and keeps working. Requests past
    // reserved_size abort via BUSTER_VALIDATE, so they cannot be observed
    // in-process.
    {
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1)});
        BUSTER_TEST(arguments, arena != 0);
        if (arena)
        {
            void* fits = arena_allocate_bytes(arena, BUSTER_KB(1), 1);
            BUSTER_TEST(arguments, fits != 0);

            u64 remaining = arena->reserved_size - arena->position;
            void* rest = arena_allocate_bytes(arena, remaining, 1);
            BUSTER_TEST(arguments, rest != 0);
            BUSTER_TEST(arguments, arena->position == arena->reserved_size);

            arena_destroy(arena, 1);
        }
    }

    // Checked alignment is side-effect free, while a valid allocation and
    // rewind update the cursor and dirty watermark in their usual order.
    {
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = 1}});
        if (BUSTER_REQUIRE(arguments, arena != 0))
        {
            u64 start = arena->position;
            u64 committed = arena->os_position;
            u64 dirty = arena_dirty_position(arena);
            u64 rounded = UINT64_MAX;
            bool valid = align_forward_checked(start, 3, &rounded);
            BUSTER_TEST(arguments, !valid && rounded == UINT64_MAX && arena->position == start && arena->os_position == committed &&
                                       arena_dirty_position(arena) == dirty);

            void* empty = arena_allocate_bytes(arena, 0, 1);
            BUSTER_TEST(arguments, empty != 0 && arena->position == start);
            u8* bytes = (u8*)arena_allocate_bytes(arena, 17, 16);
            u64 high_water = arena->position;
            BUSTER_TEST(arguments, bytes != 0 && ((u64)bytes & 15) == 0 && high_water > start);
            arena_set_position(arena, arena_minimum_position);
            BUSTER_TEST(arguments, arena->position == arena_minimum_position);
            arena_set_position(arena, arena->reserved_size);
            BUSTER_TEST(arguments, arena->position == arena->reserved_size);
            BUSTER_TEST(arguments, arena_dirty_position(arena) == arena->reserved_size);
            arena_set_position(arena, start);
            BUSTER_TEST(arguments, arena->position == start && arena_dirty_position(arena) == arena->reserved_size);
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
        }
    }

    // A legal reservation need not be a commit-granularity multiple. Filling
    // the final partial granule must clamp the OS request to the reservation
    // rather than rejecting it or committing into an adjacent arena.
    {
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(1) + 64,
            .granularity = BUSTER_KB(64),
            .initial_size = BUSTER_KB(64),
            .flags = {.no_pool = 1},
        });
        BUSTER_TEST(arguments, arena != 0);
        if (arena)
        {
            u64 remaining = arena->reserved_size - arena->position;
            u8* bytes = arena_allocate(arena, u8, remaining);
            BUSTER_TEST(arguments, bytes != 0);
            if (bytes && remaining)
            {
                bytes[0] = 0x41;
                bytes[remaining - 1] = 0xb7;
                BUSTER_TEST(arguments, bytes[0] == 0x41 && bytes[remaining - 1] == 0xb7);
            }
            BUSTER_TEST(arguments, arena->position == arena->reserved_size);
            BUSTER_TEST(arguments, arena->os_position == arena->reserved_size);
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
        }
    }

    // The dirty watermark starts at the header, advances with allocations,
    // and survives temporal rewinds.  A rewind must not make bytes above the
    // cursor look fresh to a later consumer.
    {
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(1),
            .flags = {.no_pool = 1},
        });
        BUSTER_TEST(arguments, arena != 0);
        if (arena)
        {
            BUSTER_TEST(arguments, arena_dirty_position(arena) == arena_minimum_position);
            arena_allocate_bytes(arena, 37, 1);
            u64 high_water = arena_dirty_position(arena);
            BUSTER_TEST(arguments, high_water == arena->position);

            TemporalArena temporal = arena_begin_temporal(arena);
            arena_allocate_bytes(arena, 91, 16);
            BUSTER_TEST(arguments, arena_dirty_position(arena) == arena->position);
            scratch_end(temporal);
            BUSTER_TEST(arguments, arena->position < arena_dirty_position(arena));
            arena_reset_to_start(arena);
            BUSTER_TEST(arguments, arena_dirty_position(arena) == high_water + 91 + 15 - ((high_water + 15) & 15));
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
        }
    }

    // A thread-local reuse pool must be explicitly drainable before its TLS
    // root disappears. Start from a known-empty pool, then verify exact
    // reclamation and idempotence.
    {
        arena_pool_release_thread();
        BUSTER_TEST(arguments, arena_pool_release_thread() == 0);
        Arena* pooled[2] = {
            arena_create((ArenaCreation){0}),
            arena_create((ArenaCreation){0}),
        };
        BUSTER_TEST(arguments, pooled[0] != 0 && pooled[1] != 0);
        if (pooled[0] && pooled[1])
        {
            BUSTER_TEST(arguments, arena_destroy(pooled[0], 1));
            BUSTER_TEST(arguments, arena_destroy(pooled[1], 1));
            BUSTER_TEST(arguments, arena_pool_release_thread() == 2);
            BUSTER_TEST(arguments, arena_pool_release_thread() == 0);
        }
        else
        {
            if (pooled[0])
            {
                arena_destroy(pooled[0], 1);
            }
            if (pooled[1])
            {
                arena_destroy(pooled[1], 1);
            }
            arena_pool_release_thread();
        }
    }

    // Custom-sized arenas opt into the same pool explicitly.  The mapping is
    // dirty on reuse, and the pool link itself occupies the first payload
    // bytes, so both the allocation watermark and the link must survive the
    // header rewrite in arena_create.
    {
        ArenaCreation pooled_creation = {
            .reserved_size = BUSTER_MB(1),
            .initial_size = BUSTER_KB(64),
            .flags = {.pool_reuse = 1},
        };
        arena_pool_release_thread();
        Arena* pooled = arena_create(pooled_creation);
        BUSTER_TEST(arguments, pooled != 0);
        if (pooled)
        {
            arena_allocate_bytes(pooled, 256, 1);
            u64 expected_dirty = arena_dirty_position(pooled);
            BUSTER_TEST(arguments, arena_destroy(pooled, 1));
            Arena* reused = arena_create(pooled_creation);
            BUSTER_TEST(arguments, reused != 0);
            if (reused)
            {
                BUSTER_TEST(arguments, arena_dirty_position(reused) >= expected_dirty);
                BUSTER_TEST(arguments, arena_dirty_position(reused) >= arena_minimum_position + sizeof(Arena*));
                BUSTER_TEST(arguments, arena_destroy(reused, 1));
            }
            arena_pool_release_thread();
        }
    }

    // Decommit geometry follows native pages even when the arena's legal
    // allocation granularity is smaller. Retained bytes on the preceding page
    // must survive both discard and recommit cycles.
    {
        u64 page_size = os_get_page_size();
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = page_size * 8,
            .granularity = 64,
            .initial_size = page_size * 8,
            .flags = {.no_pool = 1},
        });
        BUSTER_TEST(arguments, arena != 0);
        if (arena)
        {
            u64 retained_size = page_size + 37;
            u8* retained = arena_allocate(arena, u8, retained_size);
            retained[0] = 0x3a;
            retained[retained_size - 1] = 0xc7;
            u64 retained_position = arena->position;
            u64 expected_os_position = page_size * 2;
            u8* dirty_tail = arena_allocate(arena, u8, page_size * 3);
            memset(dirty_tail, 0x74, page_size * 3);
            arena_set_position(arena, retained_position);
#if defined(__APPLE__)
            u64 expected_dirty_position = arena_dirty_position(arena);
#else
            u64 expected_dirty_position = expected_os_position;
#endif
            BUSTER_TEST(arguments, arena_dirty_position(arena) > expected_os_position);
            BUSTER_TEST(arguments, arena_set_position_and_decommit(arena, retained_position));
            BUSTER_TEST(arguments, arena->position == retained_position);
            BUSTER_TEST(arguments, arena->os_position == expected_os_position);
            BUSTER_TEST(arguments, arena_dirty_position(arena) == expected_dirty_position);
            BUSTER_TEST(arguments, retained[0] == 0x3a && retained[retained_size - 1] == 0xc7);

            // Clear both the retained partial page and the discarded pages.
            // This uses the real OS path, including Darwin's nonzero reuse.
            u8* recommitted = arena_allocate_zeroed(arena, u8, page_size * 2);
            u8 nonzero = 0;
            for (u64 index = 0; index < page_size * 2; index += 1)
            {
                nonzero |= recommitted[index];
            }
            BUSTER_TEST(arguments, nonzero == 0);
            recommitted[0] = 0x51;
            recommitted[page_size * 2 - 1] = 0x92;
            BUSTER_TEST(arguments, arena->os_position >= arena->position);
            BUSTER_TEST(arguments, retained[0] == 0x3a && retained[retained_size - 1] == 0xc7);

            // Cross another page boundary before resetting again. The first
            // growth must leave os_position page-aligned so this commit starts
            // at an address accepted by POSIX mprotect and Windows VirtualAlloc.
            u8* incremental = arena_allocate(arena, u8, page_size);
            incremental[0] = 0x18;
            incremental[page_size - 1] = 0xe4;
            BUSTER_TEST(arguments, arena->os_position >= arena->position);
            BUSTER_TEST(arguments, incremental[0] == 0x18 && incremental[page_size - 1] == 0xe4);
            BUSTER_TEST(arguments, arena_set_position_and_decommit(arena, retained_position));
            BUSTER_TEST(arguments, arena->os_position == expected_os_position);
            BUSTER_TEST(arguments, arena_dirty_position(arena) == expected_dirty_position);
            BUSTER_TEST(arguments, retained[0] == 0x3a && retained[retained_size - 1] == 0xc7);
            // The second cycle reuses the original tail's last partial page
            // as well as bytes written after the first recommit.
            u8* zeroed_tail = arena_allocate_zeroed(arena, u8, page_size * 3);
            nonzero = 0;
            for (u64 index = 0; index < page_size * 3; index += 1)
            {
                nonzero |= zeroed_tail[index];
            }
            BUSTER_TEST(arguments, nonzero == 0);
            BUSTER_TEST(arguments, retained[0] == 0x3a && retained[retained_size - 1] == 0xc7);
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
        }
    }

    // prefault_pages is advisory at the arena boundary too. An arena that did
    // not ask must issue no request; an arena that asked gets one request per
    // commitment, initial and incremental alike; and a refused request must
    // leave the cursor, the committed high water and the handed-out bytes
    // exactly as a granted one would.
    {
        OsPrefaultTestCounters quiet_before = os_prefault_test_counters();
        Arena* quiet = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(1),
            .initial_size = BUSTER_KB(64),
            .flags = {.no_pool = 1},
        });
        BUSTER_TEST(arguments, quiet != 0);
        if (quiet)
        {
            memset(arena_allocate_bytes(quiet, BUSTER_KB(128), 16), 0x11, BUSTER_KB(128));
            BUSTER_TEST(arguments, os_prefault_test_counters().requests == quiet_before.requests);
            BUSTER_TEST(arguments, arena_destroy(quiet, 1));
        }

        OsPrefaultTestCounters before = os_prefault_test_counters();
        os_prefault_test_force_next(OS_PREFAULT_REFUSED);
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(1),
            .initial_size = BUSTER_KB(64),
            .granularity = BUSTER_KB(64),
            .flags = {.prefault_pages = 1},
        });
        BUSTER_TEST(arguments, arena != 0);
        if (arena)
        {
            OsPrefaultTestCounters created = os_prefault_test_counters();
            BUSTER_TEST(arguments, created.requests == before.requests + 1);
            BUSTER_TEST(arguments, created.unpopulated == before.unpopulated + 1);
            BUSTER_TEST(arguments, created.last == OS_PREFAULT_REFUSED);
            BUSTER_TEST(arguments, arena->flags.prefault_pages);
            BUSTER_TEST(arguments, arena->position == arena_minimum_position);
            BUSTER_TEST(arguments, arena->os_position >= BUSTER_KB(64));

            // Growing past the committed high water refuses its own request
            // and still returns writable bytes at the expected cursor.
            os_prefault_test_force_next(OS_PREFAULT_REFUSED);
            u8* grown = (u8*)arena_allocate_bytes(arena, BUSTER_KB(128), 16);
            OsPrefaultTestCounters after_growth = os_prefault_test_counters();
            BUSTER_TEST(arguments, grown != 0);
            BUSTER_TEST(arguments, after_growth.requests == created.requests + 1);
            BUSTER_TEST(arguments, after_growth.last == OS_PREFAULT_REFUSED);
            memset(grown, 0x5a, BUSTER_KB(128));
            BUSTER_TEST(arguments, grown[0] == 0x5a && grown[BUSTER_KB(128) - 1] == 0x5a);
            BUSTER_TEST(arguments, arena->position == arena_minimum_position + BUSTER_KB(128));
            BUSTER_TEST(arguments, arena->os_position >= arena->position);
            BUSTER_TEST(arguments, arena_dirty_position(arena) == arena->position);

            // An arena that asked for prefaulting is never parked for reuse:
            // the pool would hand it back without reissuing the request.
            arena_pool_release_thread();
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
            BUSTER_TEST(arguments, arena_pool_release_thread() == 0);
        }
    }

#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS
    // A failed recommit is reported at the allocation site instead of
    // returning a pointer into inaccessible memory. Run the fatal path in a
    // child so both Debug and Release can assert the diagnostic and exit.
    {
        String8 child_arguments[] = {
            program_state->input.arguments.pointer[0],
            S8("test"),
        };
        String8 modes[] = {
            S8("commit"), S8("commit_prefault"), S8("bound"), S8("alignment_zero"), S8("alignment_three"),
            S8("alignment_huge"), S8("cursor_low"), S8("cursor_high"), S8("decommit_low"), S8("decommit_high"),
        };
        String8 diagnostics[] = {
            S8("arena commit failed"), S8("arena commit failed"), S8("validation failed"), S8("validation failed"),
            S8("validation failed"), S8("validation failed"), S8("validation failed"), S8("validation failed"),
            S8("validation failed"), S8("validation failed"),
        };
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(modes) == BUSTER_ARRAY_LENGTH(diagnostics));
        for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(modes); mode_index += 1)
        {
            String8 environment_keys[] = {S8("BUSTER_ARENA_FAILURE_MODE")};
            String8 environment_values[] = {modes[mode_index]};
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                         (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                         (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values),
                                                         (ProcessSpawnOptions){
                                                             .capture = (u64)1 << STANDARD_STREAM_ERROR,
                                                         });
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, 30000000);
                String8 error = {
                    .pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer,
                    .length = wait.streams[STANDARD_STREAM_ERROR].length,
                };
                BUSTER_TEST(arguments, !wait.timed_out);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED);
                BUSTER_TEST(arguments, string_first_sequence(error, diagnostics[mode_index]) != BUSTER_STRING_NO_MATCH);
            }
        }
    }
#endif

    return result;
}
#endif
