#include <buster/lib/arena.h>
#include <buster/lib/os.h>
#include <buster/lib/integer.h>

BUSTER_GLOBAL_LOCAL u64 default_granularity = BUSTER_KB(64);

BUSTER_GLOBAL_LOCAL u64 default_reserve_size = BUSTER_MB(64);
BUSTER_GLOBAL_LOCAL u64 initial_size_granularity_factor = 4;

void arena_allocation_overflow(void)
{
    os_fail_message(S8("arena allocation size overflowed"));
}

void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    // Arenas created with count > 1 share one reservation, so committing past
    // reserved_size would land on the next arena's pages and corrupt it
    // silently; fail loudly instead. Callers never check for null, so this
    // must not return one.
    //
    // Bounding the operand is what makes that bound hold for every `size`
    // rather than only for the ones that happen not to wrap: with `size` and
    // `position` both under ARENA_MAX_RESERVATION (the second enforced in
    // arena_create), the sum cannot carry past 2^64, so the comparison against
    // `reserved_size` is exact instead of bypassable by a large enough `size`.
    // The remaining-space form `size <= reserved_size - aligned_offset` needs
    // one compare fewer, but only if reservations are alignment-granular, and
    // they are not — the rendering boundary tests reserve 256 bytes on
    // purpose. Rounding up to the commit granularity moved into the branch
    // that needs it, which pays for the operand bound: the fast path no longer
    // loads `granularity` at all.
    BUSTER_CHECK(size <= ARENA_MAX_RESERVATION);
    u64 aligned_offset = align_forward(arena->position, alignment);
    u64 aligned_size_after = aligned_offset + size;
    BUSTER_CHECK(aligned_size_after <= arena->reserved_size);

    u8* arena_byte_pointer = (u8*)arena;
    u64 os_position = arena->os_position;

    if (BUSTER_UNLIKELY(aligned_size_after > os_position))
    {
        u64 target_committed_size = align_forward(aligned_size_after, arena->granularity);
        BUSTER_CHECK(target_committed_size <= arena->reserved_size);
        u64 size_to_commit = target_committed_size - os_position;
        u8* commit_pointer = arena_byte_pointer + os_position;

        if (os_commit(commit_pointer, size_to_commit, (ProtectionFlags){.read = 1, .write = 1, .execute = arena->flags.execute}, arena->flags.lock_pages))
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

// Destroyed default-shaped reservations park here, per thread, for reuse:
// the mapping and its already-faulted pages survive, so the next
// arena_create skips the unmap/remap pair and the kernel's first-touch page
// zeroing. A reused arena hands out dirty bytes — the same contract
// arena_reset_to_start already imposes on every allocation site. The parked
// arena's first buffer bytes hold the pool link and its header keeps the
// reservation size the reuse match reads. Non-default reservation sizes
// join only through the opt-in pool_reuse flag, because a consumer of a
// custom-size arena may still assume freshly zeroed pages; reuse always
// requires an exact reservation-size match, so differently shaped arenas
// never satisfy each other's requests. Multi-arena reservations, execute or
// locked pages, and entries past the cap unmap exactly as before.
#define ARENA_POOL_LIMIT 16
BUSTER_THREAD_LOCAL_DECL Arena* arena_pool_head;
BUSTER_THREAD_LOCAL_DECL u64 arena_pool_count;

BUSTER_GLOBAL_LOCAL bool arena_pool_eligible(u64 reserved_size, u64 count, ArenaFlags flags)
{
    return count == 1 && !flags.execute && !flags.lock_pages && (reserved_size == default_reserve_size || flags.pool_reuse);
}

bool arena_destroy(Arena* arena, u64 count)
{
    count = count == 0 ? 1 : count;
    u64 reserved_size = arena->reserved_size;
    if (arena_pool_eligible(reserved_size, count, arena->flags) && arena_pool_count < ARENA_POOL_LIMIT)
    {
        *(Arena**)((u8*)arena + arena_minimum_position) = arena_pool_head;
        arena_pool_head = arena;
        arena_pool_count += 1;
        return true;
    }
    return arena_destroy_extended(arena, count, reserved_size);
}

Arena* arena_create(ArenaCreation original_creation)
{
    ArenaCreation creation = arena_creation_parameters(original_creation);
    u64 count = creation.count;
    u64 individual_reserved_size = creation.reserved_size;
    // The ceiling is what lets every later allocation reason about `position`
    // and `reserved_size` as small numbers: with both under 2^48 no sum or
    // alignment round-up in arena_allocate_bytes can reach 2^64.
    BUSTER_CHECK(individual_reserved_size <= ARENA_MAX_RESERVATION);
    BUSTER_CHECK(count <= UINT64_MAX / individual_reserved_size);
    u64 total_reserved_size = individual_reserved_size * count;

    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(creation.granularity));
    BUSTER_CHECK(creation.initial_size >= arena_minimum_position);
    BUSTER_CHECK(creation.initial_size <= individual_reserved_size);

    if (arena_pool_eligible(individual_reserved_size, count, creation.flags))
    {
        Arena* previous = 0;
        for (Arena* pooled = arena_pool_head; pooled; previous = pooled, pooled = *(Arena**)((u8*)pooled + arena_minimum_position))
        {
            if (pooled->reserved_size != individual_reserved_size)
            {
                continue;
            }
            Arena* next = *(Arena**)((u8*)pooled + arena_minimum_position);
            if (previous)
            {
                *(Arena**)((u8*)previous + arena_minimum_position) = next;
            }
            else
            {
                arena_pool_head = next;
            }
            arena_pool_count -= 1;
            u64 committed = pooled->os_position;
            bool committed_enough = committed >= creation.initial_size;
            if (!committed_enough)
            {
                committed_enough = os_commit(pooled, creation.initial_size, (ProtectionFlags){.read = 1, .write = 1}, false);
                committed = creation.initial_size;
            }
            if (committed_enough)
            {
                *pooled = (Arena){
                    .reserved_size = individual_reserved_size,
                    .position = arena_minimum_position,
                    .os_position = committed,
                    .granularity = creation.granularity,
                    .flags = creation.flags,
                };
                return pooled;
            }
            arena_destroy_extended(pooled, 1, individual_reserved_size);
            break;
        }
    }

    ProtectionFlags protection_flags = {.read = 1, .write = 1, .execute = creation.flags.execute};
    MapFlags map_flags = {.priv = 1, .anonymous = 1, .no_reserve = 1, .populate = 0};
    u8* result = (u8*)os_reserve(0, total_reserved_size, protection_flags, map_flags);

    if (result)
    {
        for (u64 i = 0; i < count; i += 1)
        {
            Arena* arena = (Arena*)(result + (individual_reserved_size * i));

            bool commit_result = os_commit(arena, creation.initial_size, protection_flags, creation.flags.lock_pages);
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
    TemporalArena result = {.arena = arena, .position = arena->position};
    return result;
}

TemporalArena scratch_begin(Arena** conflicts, u64 count)
{
    Arena* arena = thread_context_get_scratch(conflicts, count);
    // Null means every scratch arena conflicted with the caller's arenas;
    // fail here instead of dereferencing null in arena_begin_temporal.
    BUSTER_CHECK(arena);
    return arena_begin_temporal(arena);
}

void scratch_end(TemporalArena temporal)
{
    Arena* arena = temporal.arena;
    arena->position = temporal.position;
}
