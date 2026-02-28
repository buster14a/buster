#pragma once
#include <buster/arena.h>
#include <buster/os.h>
#include <buster/assertion.h>
#include <buster/integer.h>

BUSTER_GLOBAL_LOCAL bool arena_lock_pages = true;
__attribute__((used)) BUSTER_GLOBAL_LOCAL u64 page_size = BUSTER_KB(4);
BUSTER_GLOBAL_LOCAL u64 default_granularity = BUSTER_MB(2);

BUSTER_GLOBAL_LOCAL u64 default_reserve_size = BUSTER_GB(4);
BUSTER_GLOBAL_LOCAL u64 initial_size_granularity_factor = 4;

BUSTER_IMPL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    let aligned_offset = align_forward(arena->position, alignment);
    let aligned_size_after = aligned_offset + size;
    let arena_byte_pointer = (u8*)arena;
    let os_position = arena->os_position;

    if (BUSTER_UNLIKELY(aligned_size_after > os_position))
    {
        let target_committed_size = align_forward(aligned_size_after, arena->granularity);
        let size_to_commit = target_committed_size - os_position;
        let commit_pointer = arena_byte_pointer + os_position;
        if (os_commit(commit_pointer, size_to_commit, (ProtectionFlags) { .read = 1, .write = 1 }, arena_lock_pages))
        {
            arena->os_position = target_committed_size;
        }
    }

    let result = arena_byte_pointer + aligned_offset;
    arena->position = aligned_size_after;
    BUSTER_CHECK(arena->position <= arena->os_position);

    return result;
}

BUSTER_IMPL void arena_reset_to_start(Arena* arena)
{
    arena_set_position(arena, arena_minimum_position);
}

BUSTER_IMPL void arena_set_position(Arena* arena, u64 position)
{
    arena->position = position;
}

BUSTER_IMPL Arena* arena_create(ArenaCreation initialization)
{
    if (!initialization.reserved_size)
    {
        initialization.reserved_size = default_reserve_size;
    }

    if (!initialization.count)
    {
        initialization.count = 1;
    }

    let count = initialization.count;
    let individual_reserved_size = initialization.reserved_size;
    let total_reserved_size = individual_reserved_size * count;

    ProtectionFlags protection_flags = { .read = 1, .write = 1 };
    MapFlags map_flags = { .private = 1, .anonymous = 1, .no_reserve = 1, .populate = 0 };
    let raw_pointer = os_reserve(0, total_reserved_size, protection_flags, map_flags);

    if (!initialization.granularity)
    {
        initialization.granularity = default_granularity;
    }

    if (!initialization.initial_size)
    {
        initialization.initial_size = default_granularity * initial_size_granularity_factor;
    }

    for (u64 i = 0; i < count; i += 1)
    {
        let arena = (Arena*)((u8*)raw_pointer + (individual_reserved_size * i));
        os_commit(arena, initialization.initial_size, protection_flags, arena_lock_pages);
        *arena = (Arena){ 
            .reserved_size = individual_reserved_size,
            .position = arena_minimum_position,
            .os_position = initialization.initial_size,
            .granularity = initialization.granularity,
        };
    }

    return (Arena*)raw_pointer;
}

BUSTER_IMPL bool arena_destroy(Arena* arena, u64 count)
{
    count = count == 0 ? 1 : count;
    let reserved_size = arena->reserved_size;
    let size = reserved_size * count;
    return os_unreserve(arena, size);
}

BUSTER_IMPL void* arena_current_pointer(Arena* arena, u64 alignment)
{
    return (u8*)arena + align_forward(arena->position, alignment);
}
