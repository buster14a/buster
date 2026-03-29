#pragma once
#include <buster/base.h>
STRUCT(ArenaFlags)
{
    u64 execute:1;
    u64 flags:63;
};

STRUCT(Arena)
{
    u64 reserved_size;
    u64 position;
    u64 os_position;
    u64 granularity;
    ArenaFlags flags;
    u8 reserved[24];
};

// The arenas need to be aligned in order for SIMD data (AVX buffers, vertex data) to work as expected
static_assert(sizeof(Arena) == 64);

STRUCT(ArenaCreation)
{
    u64 reserved_size;
    u64 granularity;
    u64 initial_size;
    u64 count;
    ArenaFlags flags;
};

STRUCT(TemporalArena)
{
    Arena* arena;
    u64 position;
};

BUSTER_GLOBAL_LOCAL u64 arena_minimum_position = sizeof(Arena);

BUSTER_F_DECL Arena* arena_create(ArenaCreation initialization);
BUSTER_F_DECL bool arena_destroy(Arena* arena, u64 count);
BUSTER_F_DECL u8* arena_get_byte_pointer(Arena* arena, u64 position);
BUSTER_F_DECL void arena_set_position(Arena* arena, u64 position);
BUSTER_F_DECL void arena_reset_to_start(Arena* arena);
BUSTER_F_DECL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment);
BUSTER_F_DECL void* arena_current_byte_pointer(Arena* arena, u64 alignment);

BUSTER_F_DECL TemporalArena arena_begin_temporal(Arena* arena);
BUSTER_F_DECL void arena_end_temporal(TemporalArena temporal);

BUSTER_F_DECL TemporalArena scratch_begin(Arena** conflicts, u64 count);
BUSTER_F_DECL void scratch_end(TemporalArena scratch);

#define arena_allocate(arena, T, count) (T*) arena_allocate_bytes(arena, sizeof(T) * (count), alignof(T))
#define arena_buffer_is_empty(arena) ((arena)->position == arena_minimum_position)
#define arena_buffer_size(arena) ((arena)->position - arena_minimum_position)
#define arena_buffer_start(arena) ((u8*)arena + arena_minimum_position)
#define arena_get_pointer_at_position(arena, T, position) ((T*)arena_get_byte_pointer((arena), (position)))
#define arena_get_pointer_at_index(arena, T, index) (((T*)arena_get_byte_pointer((arena), arena_minimum_position)) + (index))
#define arena_get_slice_at_position(arena, T, start, end) ((Slice<T>){ .pointer = arena_get_pointer_at_position((arena), T, (start)), .length = (u64)(arena_get_pointer_at_position((arena), T, (end)) - arena_get_pointer_at_position((arena), T, (start))) })
#define arena_current_pointer(arena, T) ((T*)arena_current_byte_pointer((arena), alignof(T)))
