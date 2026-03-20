#pragma once
#include <buster/base.h>
STRUCT(Arena)
{
    u64 reserved_size;
    u64 position;
    u64 os_position;
    u64 granularity;
};

STRUCT(ArenaCreation)
{
    u64 reserved_size;
    u64 granularity;
    u64 initial_size;
    u64 count;
};

STRUCT(TemporalArena)
{
    Arena* arena;
    u64 position;
};

BUSTER_GLOBAL_LOCAL u64 arena_minimum_position = sizeof(Arena);

BUSTER_F_DECL Arena* arena_create(ArenaCreation initialization);
BUSTER_F_DECL bool arena_destroy(Arena* arena, u64 count);
BUSTER_F_DECL void arena_set_position(Arena* arena, u64 position);
BUSTER_F_DECL void arena_reset_to_start(Arena* arena);
BUSTER_F_DECL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment);
BUSTER_F_DECL void* arena_current_pointer(Arena* arena, u64 alignment);

BUSTER_F_DECL TemporalArena arena_begin_temporal(Arena* arena);
BUSTER_F_DECL void arena_end_temporal(TemporalArena temporal);

BUSTER_F_DECL TemporalArena scratch_begin(Arena** conflicts, u64 count);
BUSTER_F_DECL void scratch_end(TemporalArena scratch);

#define arena_allocate(arena, T, count) (T*) arena_allocate_bytes(arena, sizeof(T) * (count), alignof(T))
#define arena_buffer_is_empty(arena) ((arena)->position == arena_minimum_position)
#define arena_buffer_size(arena) ((arena)->position - arena_minimum_position)
#define arena_buffer_start(arena) ((u8*)arena + arena_minimum_position)

