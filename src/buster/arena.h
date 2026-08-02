#pragma once
#include <buster/base.h>
typedef struct ArenaFlags ArenaFlags;
struct ArenaFlags
{
    u64 execute : 1;
    u64 lock_pages : 1;
    u64 flags : 62;
};

typedef struct Arena Arena;
struct Arena
{
    u64 reserved_size;
    u64 position;
    u64 os_position;
    u64 granularity;
    ArenaFlags flags;
    u8 reserved[24];
};

// The arenas need to be aligned in order for SIMD data (AVX buffers, vertex data) to work as expected
BUSTER_CT_CHECK(sizeof(Arena) == 64);

typedef struct ArenaCreation ArenaCreation;
struct ArenaCreation
{
    u64 reserved_size;
    u64 granularity;
    u64 initial_size;
    u64 count;
    ArenaFlags flags;
};

typedef struct TemporalArena TemporalArena;
struct TemporalArena
{
    Arena* arena;
    u64 position;
};

#define arena_minimum_position ((u64)sizeof(Arena))

BUSTER_F_DECL Arena* arena_create(ArenaCreation initialization);
BUSTER_F_DECL bool arena_destroy(Arena* arena, u64 count);
BUSTER_F_DECL u8* arena_get_byte_pointer_at_position(Arena* arena, u64 position);
BUSTER_F_DECL u8* arena_get_byte_pointer_at_position_check_aligned(Arena* arena, u64 position, u64 alignment);
BUSTER_F_DECL void arena_set_position(Arena* arena, u64 position);
BUSTER_F_DECL void arena_reset_to_start(Arena* arena);
BUSTER_F_DECL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment);
BUSTER_F_DECL u8* arena_get_byte_pointer_align(Arena* arena, u64 position, u64 alignment);

BUSTER_F_DECL TemporalArena arena_begin_temporal(Arena* arena);

BUSTER_F_DECL TemporalArena scratch_begin(Arena** conflicts, u64 count);
BUSTER_F_DECL void scratch_end(TemporalArena scratch);

#define arena_allocate(arena, T, count) (T*)arena_allocate_bytes(arena, sizeof(T) * (count), BUSTER_ALIGN_OF(T))
#define arena_buffer_is_empty(arena) ((arena)->position == arena_minimum_position)
#define arena_buffer_size(arena) ((arena)->position - arena_minimum_position)
#define arena_buffer_start(arena) ((u8*)arena + arena_minimum_position)
#define arena_get_pointer_at_position(arena, T, position) ((T*)arena_get_byte_pointer_at_position_check_aligned((arena), (position), BUSTER_ALIGN_OF(T)))
#define arena_get_pointer_at_index(arena, T, index) (((T*)arena_get_byte_pointer_at_position((arena), arena_minimum_position)) + (index))
#define arena_get_slice_at_position(arena, T, start, end)                                                                                                      \
    ((Slice##T){.pointer = arena_get_pointer_at_position((arena), T, (start)),                                                                                 \
                .length = (u64)(arena_get_pointer_at_position((arena), T, (end)) - arena_get_pointer_at_position((arena), T, (start)))})
#define arena_get_pointer_at_position_align(arena, T, position) ((T*)arena_get_byte_pointer_align((arena), (position), BUSTER_ALIGN_OF(T)))
#define arena_get_current_pointer(arena, T) arena_get_pointer_at_position_align((arena), T, (arena)->position)
