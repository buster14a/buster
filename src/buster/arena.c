#include <buster/arena.h>
#include <buster/os.h>
#include <buster/integer.h>

BUSTER_GLOBAL_LOCAL bool arena_lock_pages = true;
BUSTER_GLOBAL_LOCAL u64 default_granularity = BUSTER_KB(64);

BUSTER_GLOBAL_LOCAL u64 default_reserve_size = BUSTER_MB(64);
BUSTER_GLOBAL_LOCAL u64 initial_size_granularity_factor = 4;

void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    u64 aligned_offset = align_forward(arena->position, alignment);
    u64 aligned_size_after = aligned_offset + size;
    u64 target_committed_size = align_forward(aligned_size_after, arena->granularity);
    // Arenas created with count > 1 share one reservation, so committing past
    // reserved_size would land on the next arena's pages and corrupt it
    // silently; fail loudly instead. Callers never check for null, so this
    // must not return one.
    BUSTER_CHECK(target_committed_size <= arena->reserved_size);

    u8* arena_byte_pointer = (u8*)arena;
    u64 os_position = arena->os_position;

    if (BUSTER_UNLIKELY(aligned_size_after > os_position))
    {
        u64 size_to_commit = target_committed_size - os_position;
        u8* commit_pointer = arena_byte_pointer + os_position;

        if (os_commit(commit_pointer, size_to_commit, (ProtectionFlags) { .read = 1, .write = 1, .execute = arena->flags.execute }, arena_lock_pages))
        {
            arena->os_position = target_committed_size;
        }
    }

    u8* result = arena_byte_pointer + aligned_offset;
    arena->position = aligned_size_after;
    BUSTER_CHECK(arena->position <= arena->os_position);

    return result;
}

u8* arena_get_byte_pointer_at_position(Arena* arena, u64 position)
{
    return (u8*)arena + position;
}

u8* arena_get_byte_pointer_at_position_check_aligned(Arena* arena, u64 position, u64 alignment)
{
    u8* result = arena_get_byte_pointer_at_position(arena, position);
    BUSTER_CHECK(is_aligned((u64)result, alignment));
    return result;
}

u8* arena_get_byte_pointer_align(Arena* arena, u64 position, u64 alignment)
{
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(alignment));
    u8* result = arena_get_byte_pointer_at_position(arena, align_forward(position, alignment));
    return result;
}

void arena_reset_to_start(Arena* arena)
{
    arena_set_position(arena, arena_minimum_position);
}

void arena_set_position(Arena* arena, u64 position)
{
    arena->position = position;
}

BUSTER_GLOBAL_LOCAL ArenaCreation arena_creation_parameters(ArenaCreation original)
{
    ArenaCreation result = original;

    if (!result.reserved_size)
    {
        result.reserved_size = default_reserve_size;
    }

    if (!result.count)
    {
        result.count = 1;
    }

    if (!result.granularity)
    {
        result.granularity = default_granularity;
    }

    if (!result.initial_size)
    {
        result.initial_size = default_granularity * initial_size_granularity_factor;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool arena_destroy_extended(Arena* arena, u64 count, u64 reserved_size)
{
    u64 size = reserved_size * count;
    return os_unreserve(arena, size);
}

bool arena_destroy(Arena* arena, u64 count)
{
    count = count == 0 ? 1 : count;
    u64 reserved_size = arena->reserved_size;
    return arena_destroy_extended(arena, count, reserved_size);
}

Arena* arena_create(ArenaCreation original_creation)
{
    ArenaCreation creation = arena_creation_parameters(original_creation);
    u64 count = creation.count;
    u64 individual_reserved_size = creation.reserved_size;
    BUSTER_CHECK(count <= UINT64_MAX / individual_reserved_size);
    u64 total_reserved_size = individual_reserved_size * count;

    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(creation.granularity));
    BUSTER_CHECK(creation.initial_size >= arena_minimum_position);
    BUSTER_CHECK(creation.initial_size <= individual_reserved_size);

    ProtectionFlags protection_flags = { .read = 1, .write = 1, .execute = creation.flags.execute };
    MapFlags map_flags = { .priv = 1, .anonymous = 1, .no_reserve = 1, .populate = 0 };
    u8* result = (u8*)os_reserve(0, total_reserved_size, protection_flags, map_flags);

    if (result)
    {
        for (u64 i = 0; i < count; i += 1)
        {
            Arena* arena = (Arena*)(result + (individual_reserved_size * i));

            bool commit_result = os_commit(arena, creation.initial_size, protection_flags, arena_lock_pages);
            if (commit_result)
            {
                *arena = (Arena){ 
                    .reserved_size = individual_reserved_size,
                    .position = arena_minimum_position,
                    .os_position = creation.initial_size,
                    .granularity = creation.granularity,
                    .flags = creation.flags,
                };
            }
            else
            {
                bool destroy_result = arena_destroy_extended((Arena*)result, count, individual_reserved_size);
                result = 0;
                BUSTER_CHECK(destroy_result);
                break;
            }
        }
    }

    return (Arena*)result;
}

TemporalArena arena_begin_temporal(Arena* arena)
{
    TemporalArena result = { .arena = arena, .position = arena->position };
    return result;
}

TemporalArena scratch_begin(Arena** conflicts, u64 count)
{
    return arena_begin_temporal(thread_context_get_scratch(conflicts, count));
}

void scratch_end(TemporalArena temporal)
{
    Arena* arena = temporal.arena;
    arena->position = temporal.position;
}

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>

UnitTestResult arena_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

    // Companion to the reserved_size bound: filling an arena up to its
    // reservation stays within bounds and keeps working. Requests past
    // reserved_size abort via BUSTER_CHECK, so they cannot be observed
    // in-process.
    {
        Arena* arena = arena_create((ArenaCreation){ .reserved_size = BUSTER_MB(1) });
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
#endif
