#include <buster/tests/arena_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/os.h>

UnitTestResult arena_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS
    String8 failure_mode = os_get_environment_variable(S8("BUSTER_ARENA_FAILURE_MODE"));
    if (string_equal(failure_mode, S8("commit")) || string_equal(failure_mode, S8("bound")))
    {
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(1),
            .initial_size = BUSTER_KB(64),
            .flags = {.no_pool = 1},
        });
        BUSTER_VALIDATE(arena != 0);
        if (string_equal(failure_mode, S8("commit")))
        {
            arena_test_fail_next_commit();
            arena_allocate_bytes(arena, BUSTER_KB(128), 1);
        }
        else
        {
            // This is caller-derived validation, not an invariant. It must
            // still fail in an optimized build rather than becoming UB.
            arena_allocate_bytes(arena, arena->reserved_size, 1);
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

#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS
    // A failed recommit is reported at the allocation site instead of
    // returning a pointer into inaccessible memory. Run the fatal path in a
    // child so both Debug and Release can assert the diagnostic and exit.
    {
        String8 child_arguments[] = {
            program_state->input.arguments.pointer[0],
            S8("test"),
        };
        String8 modes[] = {S8("commit"), S8("bound")};
        String8 diagnostics[] = {S8("arena commit failed"), S8("validation failed")};
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
