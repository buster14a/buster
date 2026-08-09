#include <buster/tests/arena_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/os.h>

UnitTestResult arena_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

    // Companion to the reserved_size bound: filling an arena up to its
    // reservation stays within bounds and keeps working. Requests past
    // reserved_size abort via BUSTER_CHECK, so they cannot be observed
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
            BUSTER_TEST(arguments, arena_set_position_and_decommit(arena, retained_position));
            BUSTER_TEST(arguments, arena->position == retained_position);
            BUSTER_TEST(arguments, arena->os_position == expected_os_position);
            BUSTER_TEST(arguments, retained[0] == 0x3a && retained[retained_size - 1] == 0xc7);

            u8* recommitted = arena_allocate(arena, u8, page_size * 2);
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
            BUSTER_TEST(arguments, retained[0] == 0x3a && retained[retained_size - 1] == 0xc7);
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
        }
    }

    return result;
}
#endif
