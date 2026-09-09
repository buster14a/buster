#pragma once
#include <buster/lib/base.h>
#include <buster/lib/integer.h>
typedef struct ArenaFlags ArenaFlags;
struct ArenaFlags
{
    u64 execute : 1;
    u64 lock_pages : 1;
    // Opt-in to the destroy-side reuse pool for non-default reservation
    // sizes. A reused arena hands out dirty bytes, so only creation sites
    // whose consumers never assume freshly zeroed pages may set this.
    u64 pool_reuse : 1;
    // Force an otherwise-default-shaped arena to unmap on destruction. Use
    // this for large transient peaks that must not accumulate in a thread's
    // small-arena reuse pool.
    u64 no_pool : 1;
    u64 flags : 60;
};

typedef struct Arena Arena;
struct Arena
{
    u64 reserved_size;
    // Read outside this module; rewind through arena_set_position/scratch_end
    // so discarded allocations remain part of the dirty prefix.
    u64 position;
    u64 os_position;
    u64 granularity;
    ArenaFlags flags;
    // High-water mark retained across logical rewinds. Fresh mappings start
    // at the header; the live position supplies the current high water without
    // a store on every allocation, and pooled reuse carries the saved mark
    // across header reinitialization.
    u64 dirty_position;
    u8 reserved[16];
};

// The arenas need to be aligned in order for SIMD data (AVX buffers, vertex data) to work as expected
BUSTER_CT_CHECK(sizeof(Arena) == 64);

// After the struct, not beside base.h: os.h declares entry points taking
// `Arena*` and includes nothing itself, so it needs the typedef above to have
// been read. It is included at all because the inline bump at the bottom of
// this header states BUSTER_CHECK, which os.h owns.
#include <buster/lib/os.h>

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
// Every reservation is capped here so that positions, sizes and their sums
// stay far below 2^64 and the arithmetic in arena_allocate_bytes cannot wrap.
#define ARENA_MAX_RESERVATION ((u64)1 << 48)

BUSTER_F_DECL Arena* arena_create(ArenaCreation initialization);
BUSTER_F_DECL bool arena_destroy(Arena* arena, u64 count);
// Unmaps every otherwise-reusable arena parked in the calling OS thread's
// local pool. OS thread teardown must call this after releasing its context.
BUSTER_F_DECL u64 arena_pool_release_thread(void);
BUSTER_F_DECL u8* arena_get_byte_pointer_at_position(Arena* arena, u64 position);
BUSTER_F_DECL u8* arena_get_byte_pointer_at_position_check_aligned(Arena* arena, u64 position, u64 alignment);
BUSTER_F_DECL u64 arena_dirty_position(Arena* arena);
BUSTER_F_DECL void arena_set_position(Arena* arena, u64 position);
// Resets the logical position and releases only complete native pages beyond
// it. This remains safe for legal arenas whose granularity is sub-page.
// Apple retains the dirty watermark because its discard can preserve bytes;
// use zeroed allocation when recommitted storage must be initialized.
BUSTER_F_DECL bool arena_set_position_and_decommit(Arena* arena, u64 position);
BUSTER_F_DECL void arena_reset_to_start(Arena* arena);
// The commit half of arena_allocate_bytes, outlined so the bump below stays a
// handful of instructions at each of its ~1.400 call sites.
BUSTER_F_DECL void arena_allocate_commit(Arena* arena, u64 aligned_size_after);
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void arena_test_fail_next_commit(void);
#endif
BUSTER_F_DECL u8* arena_get_byte_pointer_align(Arena* arena, u64 position, u64 alignment);

BUSTER_F_DECL TemporalArena arena_begin_temporal(Arena* arena);

BUSTER_F_DECL TemporalArena scratch_begin(Arena** conflicts, u64 count);
BUSTER_F_DECL void scratch_end(TemporalArena scratch);

// sizeof(T) * count is the one multiplication every array allocation performs
// on caller-influenced data, so it is the one place a wrapped product could
// hand back a buffer smaller than the caller is about to write. The divisor is
// always a sizeof, so it folds to a constant and the guard costs one compare
// against an immediate; when the count is provably narrower than 64 bits
// (a u32 index, a literal) the compiler drops the compare entirely. Bounding
// the product by ARENA_MAX_RESERVATION rather than by UINT64_MAX is the same
// compare against a different immediate, and reports the caller's own
// arithmetic rather than leaving it to surface as a capacity failure.
BUSTER_NORETURN BUSTER_COLD BUSTER_F_DECL void arena_allocation_overflow(void);

BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_INLINE u64 arena_array_size(u64 element_size, u64 count)
{
    if (BUSTER_UNLIKELY(count > ARENA_MAX_RESERVATION / element_size))
    {
        arena_allocation_overflow();
    }
    return element_size * count;
}

#if BUSTER_BENCH_ALLOCATIONS
// Calling-thread cumulative request traffic. These counters are not live bytes,
// physical OS zeroing, libc allocation, or RSS. Reporting never resets them.
typedef struct ArenaBenchmarkCounters ArenaBenchmarkCounters;
struct ArenaBenchmarkCounters
{
    u64 calls;
    u64 requested_bytes;
    u64 padding;
    u64 zero_requested;
    u64 zero_written;
    u64 empty;
    u64 small;
    u64 maximum;
    u64 failures;
    u64 failed_bytes;
};
typedef enum ArenaBenchmarkKind
{
    ARENA_BENCHMARK_ARENA,
    ARENA_BENCHMARK_OS_RESERVE,
    ARENA_BENCHMARK_OS_COMMIT,
    ARENA_BENCHMARK_OS_DECOMMIT,
    ARENA_BENCHMARK_OS_UNRESERVE,
    ARENA_BENCHMARK_KIND_COUNT,
} ArenaBenchmarkKind;
BUSTER_F_DECL ArenaBenchmarkCounters arena_benchmark_counters(void);
BUSTER_F_DECL ArenaBenchmarkCounters arena_benchmark_kind_counters(ArenaBenchmarkKind kind);
BUSTER_F_DECL void arena_benchmark_event(ArenaBenchmarkKind kind, String8 file, String8 function, u32 line,
                                        u64 size, u64 padding, u64 zero_requested, u64 zero_written, bool success);
BUSTER_F_DECL void* arena_benchmark_allocate(Arena* arena, u64 size, u64 alignment, bool zeroed,
                                            String8 file, String8 function, u32 line);
// Capture emission preference before workers start. The single recorder remains
// enabled throughout the instrumented process, including startup and cleanup.
BUSTER_F_DECL void arena_benchmark_report_enable(bool enabled);
BUSTER_F_DECL void arena_benchmark_flush(bool final);
// Preserve the ordinary inline implementation in disabled builds. Instrumented
// wrappers below observe exactly one request around these same primitives.
#define arena_allocate_bytes arena_benchmark_allocate_bytes_raw
#define arena_allocate_zeroed_bytes arena_benchmark_allocate_zeroed_bytes_raw
#endif

// The bump is inline and the commit is not. Every allocation performs the same
// four operations -- align the position, add the size, test the committed
// high-water mark, publish the new position -- and the test fails on the order
// of once per arena page, so the branch is predicted and the call it used to
// make was most of the cost of an allocation that never touches the OS. The
// bounds reasoning the outlined body carried stays with it in arena.c. The
// caller-controlled reservation bound remains validation in every build; the
// final committed-position relation is an established invariant.
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_INLINE void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    BUSTER_VALIDATE(size <= ARENA_MAX_RESERVATION);
    u64 aligned_offset = align_forward(arena->position, alignment);
    u64 aligned_size_after = aligned_offset + size;
    BUSTER_VALIDATE(aligned_size_after <= arena->reserved_size);
    if (BUSTER_UNLIKELY(aligned_size_after > arena->os_position))
    {
        arena_allocate_commit(arena, aligned_size_after);
    }
    void* result = (u8*)arena + aligned_offset;
    arena->position = aligned_size_after;
    BUSTER_CHECK(arena->position <= arena->os_position);
    return result;
}

// An allocation that comes back zeroed, without zeroing what the operating
// system already zeroed.
//
// A fresh mapping's pages are zero, and an arena hands its bytes out in one
// direction, so the only part of a new allocation that can hold anything is
// the part below the arena's allocation high-water mark -- what a temporal
// rewind or a pooled reuse left behind. Everything above it has never been
// written in this arena and needs no fill. `dirty_position` is that mark
// (`position` supplies the live half of it, so no store is paid per
// allocation), and clearing through it also covers a boundary that falls
// inside an element, leaving every field above it zero.
//
// This is the general form of the x86 template cache's clear, which measured
// this on one buffer first; the compiler's largest fills are single
// allocations of a few megabytes each -- the linker's output image, the
// per-token arrays of the analysis and the lowering -- and every one of them
// is fresh in a result arena.
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_INLINE void* arena_allocate_zeroed_bytes(Arena* arena, u64 size, u64 alignment)
{
    u64 dirty_position = arena_dirty_position(arena);
    u8* result = (u8*)arena_allocate_bytes(arena, size, alignment);
    u64 end = arena->position;
    u64 start = end - size;
    u64 clear_end = BUSTER_MIN(dirty_position, end);
    if (clear_end > start)
    {
        memset(result, 0, clear_end - start);
    }
    return result;
}

#if BUSTER_BENCH_ALLOCATIONS
#undef arena_allocate_bytes
#undef arena_allocate_zeroed_bytes
// Function-address/parenthesized uses remain observed, attributed to these
// fallback wrappers. Ordinary calls below retain their original source site.
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_INLINE void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    return arena_benchmark_allocate(arena, size, alignment, false, S8(__FILE__), S8(__func__), __LINE__);
}
BUSTER_UNUSED_DECL BUSTER_GLOBAL_LOCAL BUSTER_INLINE void* arena_allocate_zeroed_bytes(Arena* arena, u64 size, u64 alignment)
{
    return arena_benchmark_allocate(arena, size, alignment, true, S8(__FILE__), S8(__func__), __LINE__);
}
#define arena_allocate_bytes(arena, size, alignment) \
    arena_benchmark_allocate((arena), (size), (alignment), false, S8(__FILE__), S8(__func__), __LINE__)
#define arena_allocate_zeroed_bytes(arena, size, alignment) \
    arena_benchmark_allocate((arena), (size), (alignment), true, S8(__FILE__), S8(__func__), __LINE__)
#endif

#define arena_allocate(arena, T, count) (T*)arena_allocate_bytes(arena, arena_array_size(sizeof(T), count), BUSTER_ALIGN_OF(T))
// The zeroed form of arena_allocate: same array, already zero, with only the
// overlap with the arena's dirty prefix actually written.
#define arena_allocate_zeroed(arena, T, count) (T*)arena_allocate_zeroed_bytes(arena, arena_array_size(sizeof(T), count), BUSTER_ALIGN_OF(T))
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
