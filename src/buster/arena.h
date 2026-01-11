#pragma once
#include <buster/base.h>
STRUCT(Arena)
{
    u64 reserved_size;
    u64 position;
    u64 os_position;
    u64 granularity;
};

STRUCT(ArenaInitialization)
{
    u64 reserved_size;
    u64 granularity;
    u64 initial_size;
    u64 count;
};

BUSTER_DECL Arena* arena_create(ArenaInitialization initialization);
BUSTER_DECL bool arena_destroy(Arena* arena, u64 count);
BUSTER_DECL void arena_set_position(Arena* arena, u64 position);
BUSTER_DECL void arena_reset_to_start(Arena* arena);
BUSTER_DECL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment);
BUSTER_DECL void* arena_current_pointer(Arena* arena, u64 alignment);

#define arena_allocate(arena, T, count) (T*) arena_allocate_bytes(arena, sizeof(T) * (count), alignof(T))
