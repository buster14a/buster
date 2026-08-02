#include <buster/tests/arena_test.h>

BUSTER_TEST_F_DECL UnitTestResult arena_tests(UnitTestArguments* arguments)
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

    return result;
}
