// SPDX-License-Identifier: MIT
// Copyright (c) Epic Games Tools. Local modifications Copyright (c) 2026 David Gonzalez Martin.
// Adapted interface/base-layer patterns from RAD Debugger; locally implemented barrier, allocation and scheduling.
// This file, including local changes, uses MIT. Retain LICENSES/RAD-Debugger.txt; see THIRD_PARTY_NOTICES.md.
//
// The lane base layer: groups of threads that all run the same code, and the arenas they allocate from.
//
// Every phase of the object writer and the linker is one function that every lane of a group runs (Ryan Fleury's
// "multi-core by default"; docs/object-writer-and-linker.md, "Lanes"): the lanes cut each loop into parts by cost with
// lane_parts_by_cost and take the parts with lane_take, costliest first, or split it once with lane_range or
// lane_range_by_cost; they meet at lane_sync, and each writes only what it owns. With one lane, which is how the
// compiler runs, lane_sync and lane_broadcast return at once, lane_range returns everything and a loop is one part.
//
// The interface is the RAD Debugger's, as its base layer has it today (base_thread_context.h, and radbin, which starts
// its lanes once and runs the whole program on them), in this repository's style. Three things differ, each measured
// by benchmark-lanes: the calling thread is lane 0 rather than waiting for the others, so one lane starts no thread; a
// broadcast is one barrier rather than two (see LaneGroup); and the barrier spins and then waits in mwaitx before it
// sleeps on a futex, where the RAD Debugger's on Linux is pthread_barrier_wait, which sleeps at every wait. Its GUI's
// asynchronous lanes are one group for the process's life too, which lane_session_run is, without its unconditional
// helper threads: a session of one lane creates none.
//
// Each translation unit that includes this file owns its thread context and any command-long team, but for main.c's
// units, which share one (thread_context): a command runs in its own unit's code and in the homes of the modules it
// calls (main.c). A team or a session must therefore be started in the command's code, not in main.c's dispatcher,
// which also runs the benchmarks' units, whose contexts are their own.
//
// The compiler's commands (the C driver's and link-foreign's) run on a session (#905): the process's lanes, created once,
// right after the command's options and before any of its inputs is read, the calling thread lane 0, with as many lanes
// as the command may take (one creates no thread). Every lane runs the command's flow (lane_session_run's entry), lane
// 0 its serial parts while the others park (lane_park), and each wide part on as many lanes as it pays for and the
// worker budget gives (lane_narrow), so that nothing in the command creates or joins a thread after its start. Each
// part declares its interface first, then defines it. In this order:
//   - Arenas: Arena, arena_reserve, arena_release, arena_push, arena_push_array (BUSTER_ARENA_PUSH_ARRAY) and
//     arena_pop_to. An arena reserves its address space up front, and the kernel commits each page when it is first
//     touched. A push that doesn't fit returns 0 and leaves the arena as it was: nothing is ever truncated.
//     arena_poison overwrites what pushes took, for tests of code that reuses an arena, and a thread's ArenaPool
//     (arena_pool) keeps the arenas it releases for its next reservations.
//   - The thread context: thread_context, which holds the thread's lane and its scratch arenas, and scratch_begin and
//     scratch_end.
//   - Lanes: Lane, LaneGroup, LaneSession, lane_index, lane_count, lane_sync, lane_broadcast, lane_from_task_index,
//     lane_range, lane_range_by_cost, range_part, range_part_by_cost, lane_part_count, lane_parts_by_cost,
//     parts_by_cost, lane_take, lane_alone, lane_restore, lane_park, lane_park_broadcast, lane_narrow and lane_widen.
//   - Starting lanes: lane_team_start and lane_team_stop own a command's fixed threads; lane_run dispatches a whole
//     operation to them, and lane_team_keep_scratch has them keep their scratch arenas from one to the next. Without a
//     team or a session, lane_run starts and joins a fresh group, as the cold-start benchmark needs.
//   - Sessions: lane_session_run, lane_session_current, and lane_serve_begin, lane_serve_end, lane_serve and
//     lane_serving, with which lane 0 runs the regions of serial code with lane_run calls (the in-process driver
//     tests'); and a command's flow of rounds: LaneFlow, lane_flow_begin, lane_flow_lanes, lane_flow_round_begin,
//     lane_flow_round_end, lane_flow_next and lane_flow_end.
//
// The barrier, dispatch/completion and lane_take use atomics. lane_take adds one to its group's counter, and the last
// take of a loop resets it, since every lane takes once more than it gets parts. In lane_arrive, each lane counts itself
// in, and the last one advances the group's generation word with lane_publish while the others wait for it in
// lane_wait. A waiting lane spins for BUSTER_LANE_SPINS pauses, waits in mwaitx for the word's line to be written for up
// to BUSTER_LANE_WAIT_NANOSECONDS (lane_wait_ticks), and then sleeps on the word with a futex, setting its bit 0 first
// so that the lane that advances it knows to wake the sleepers: a sync at which no lane slept makes no system call.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>

// OS-dependent includes
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>

// CPU architecture dependent includes: x86intrin.h has AMD's monitorx and mwaitx besides immintrin.h (lane_wait uses
// them where the build's target has mwaitx, __MWAITX__).
#include <x86intrin.h>

// Resource-exhaustion fixtures must leave the detector runtime free to allocate
// shadow/fake-stack memory and start its own threads. Diagnostic builds inject
// the same compiler-owned reservation/start failures instead of limiting the OS.
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define BUSTER_SANITIZER_RESOURCES 1
static bool arena_test_reservation_allowed(uint64_t bytes);
#else
#define BUSTER_SANITIZER_RESOURCES 0
#endif

// Code that runs while FS is a generated program's TLS block takes no detector's instrumentation, which reaches its
// thread's state through FS (the tests' native runs).
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define BUSTER_NO_SANITIZER_INSTRUMENTATION __attribute__((disable_sanitizer_instrumentation))
#else
#define BUSTER_NO_SANITIZER_INSTRUMENTATION
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Arenas

// x86-64's base page and its transparent huge page.
#define BUSTER_ARENA_PAGE_BYTES (4ull << 10)
#define BUSTER_ARENA_HUGE_PAGE_BYTES (2ull << 20)
// The start of every arena, which stays in base pages; past it, an arena asks for huge pages. On the 9700X the first
// touch of a huge page costs 15.1 µs, since the kernel clears all 2 MB, against 0.75 µs for a base page and 4.7 µs for
// all sixteen base pages here (benchmark-lanes), so an arena that is used for a few kilobytes is far cheaper in base
// pages. Past them huge pages win: they fault faster per byte and tear down 10 times faster
// (docs/object-writer-and-linker.md, "The floors").
#define BUSTER_ARENA_SMALL_BYTES (64ull << 10)
// The user address space of x86-64's 4-level paging.
#define BUSTER_ARENA_MAX_CAPACITY (1ull << 47)

typedef struct Arena Arena;
struct Arena
{
    // capacity bytes from base, of which length are in use: the next push starts at the first suitably aligned address
    // from base + length. Logical length and capacity are separate, and a push never takes more than capacity.
    uint8_t* base;
    uint64_t length;
    uint64_t capacity;
    // Where base is in its mapping, which starts that much earlier (see arena_reserve).
    uint64_t mapping_offset;
    // The most bytes from base that pushes have taken since the arena was reserved: no byte past it has been written.
    uint64_t peak;
    // The bytes at its start that are base pages (arena_reserve_small's `small`, with the floor).
    uint64_t small;
};

// Reserves `capacity` bytes, which the kernel commits a page at a time as they are first touched: the first
// BUSTER_ARENA_SMALL_BYTES in base pages, and the rest in huge pages. Returns an arena without capacity, in which every
// push fails, if the address space can't be reserved.
static __attribute__((unused)) Arena arena_reserve(uint64_t capacity);
// arena_reserve with the first `small` bytes in base pages (a multiple of BUSTER_ARENA_PAGE_BYTES, at least
// BUSTER_ARENA_SMALL_BYTES), for a table that usually stays below what a huge page is worth: the kernel clears a huge
// page whole when it is first touched, which costs as much as about 750 KB of base pages on the 9700X. The thread's
// arena_small_floor, if larger, takes the place of `small`.
static __attribute__((unused)) Arena arena_reserve_small(uint64_t capacity, uint64_t small);

// The base pages every arena the thread reserves starts with, at least (0 for BUSTER_ARENA_SMALL_BYTES alone; a
// multiple of BUSTER_ARENA_PAGE_BYTES). A thread whose arenas each hold a small and sparse part of its work, as a lane
// of the C driver's translation units does for a small unit (#152: a few kilobytes of each of about 70 arenas per
// unit, past their first 64 KB), sets it, so that such an arena's first touch past BUSTER_ARENA_SMALL_BYTES takes a
// base page rather than a huge page the kernel clears whole. Only the thread sets and reads it. main.c's units share
// it, as they share the thread context (below): an arena that one unit's code reserves is another's to use.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern _Thread_local uint64_t arena_small_floor;
#elif defined(BUSTER_UNIT)
_Thread_local uint64_t arena_small_floor;
#else
static _Thread_local uint64_t arena_small_floor;
#endif
// The arenas a thread keeps, emptied, for its next reservations (#152): a lane that compiles translation units one after
// another reserves and releases the same arenas, about 70 for each unit, and each reservation's mmap, its madvise calls
// and its munmap take the address space's lock for writing, which every other lane's page faults and reservations then
// wait for. arena_release puts an arena in the thread's pool, its pages freed with MADV_DONTNEED, which takes the lock
// for reading and leaves the mapping and its advice as they were, so that the next user finds zero pages as in a fresh
// arena; a full pool unmaps the arena it took longest ago to make room. arena_reserve_small takes the pool's latest
// arena of the capacity and base pages asked for.
#define BUSTER_ARENA_POOL_SLOTS 256
typedef struct ArenaPool ArenaPool;
struct ArenaPool
{
    uint32_t count;
    // The releases so far, and the one that put each arena here.
    uint64_t clock;
    uint64_t released[BUSTER_ARENA_POOL_SLOTS];
    Arena arenas[BUSTER_ARENA_POOL_SLOTS];
};
// The thread's pool, or null for none: set by the code that owns the pool (a units lane), which unmaps what it holds with
// arena_pool_release once it is done. Only the thread sets and reads it, and main.c's units share it, as
// arena_small_floor.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern _Thread_local ArenaPool* arena_pool;
#elif defined(BUSTER_UNIT)
_Thread_local ArenaPool* arena_pool;
#else
static _Thread_local ArenaPool* arena_pool;
#endif
// Unmaps the arenas `pool` holds, and leaves it empty.
static __attribute__((unused)) void arena_pool_release(ArenaPool* pool);
// An arena that holds no memory, for measuring what pushes take: its pushes take room as from an arena reserved at a
// page boundary, up to BUSTER_ARENA_MAX_CAPACITY, and its length after them is that room. Its base is past the
// addresses the kernel maps a process's memory at unasked (BUSTER_ARENA_MAX_CAPACITY), so that reading or writing what a
// push returns faults: only code that pushes and writes nothing in what it pushes may run on it. Its addresses are in no
// object, so compare or subtract them only as integers.
static __attribute__((unused)) Arena arena_measure(void);
// Unmaps the arena, or puts it in the thread's pool (arena_pool), and leaves it without capacity.
static __attribute__((unused)) void arena_release(Arena* arena);
// `size` bytes at the first address from base + length that is a multiple of `alignment`, a power of two. Returns 0 and
// leaves the arena as it was if they don't fit or the alignment isn't a power of two. The bytes aren't cleared: they
// are zero only the first time their pages are touched.
static __attribute__((unused)) void* arena_push(Arena* arena, uint64_t size, uint64_t alignment);
// `count` elements of `element_bytes` each; 0, as from arena_push, if count * element_bytes overflows.
static __attribute__((unused)) void* arena_push_array(Arena* arena, uint64_t count, uint64_t element_bytes, uint64_t alignment);
#define BUSTER_ARENA_PUSH_ARRAY(arena, Type, count) ((Type*)arena_push_array((arena), (count), sizeof(Type), _Alignof(Type)))
// Frees what was pushed since the arena's length was `length`. Its pages stay committed for the next pushes.
static __attribute__((unused)) void arena_pop_to(Arena* arena, uint64_t length);
// Sets every byte pushes have taken (to the arena's peak) to `value`, and empties the arena: a test's way to make what
// reuses the arena find no byte it didn't write as a fresh arena's zero (#152's --poison-arenas).
static __attribute__((unused)) void arena_poison(Arena* arena, uint8_t value);

// The bytes arena_reserve maps for `capacity`: whole pages, and past BUSTER_ARENA_SMALL_BYTES room to align the huge
// pages.
static uint64_t arena_mapping_bytes(uint64_t capacity)
{
    const uint64_t pages = (capacity + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
    return capacity > BUSTER_ARENA_SMALL_BYTES ? pages + BUSTER_ARENA_HUGE_PAGE_BYTES : pages;
}

// Past the first `small` bytes, base is placed in the mapping so that the huge pages start at base + small, on a huge
// page boundary (the mapping has a huge page's room for that past BUSTER_ARENA_SMALL_BYTES, whatever `small` is).
static Arena arena_reserve_small(uint64_t capacity, uint64_t small)
{
    Arena result = { 0 };
    small = arena_small_floor > small ? arena_small_floor : small;
    // The pool's latest arena of this shape, if it has one.
    ArenaPool* pool = arena_pool;
    uint32_t slot = BUSTER_ARENA_POOL_SLOTS;
    for (uint32_t i = 0; pool != 0 && i < pool->count; i += 1)
    {
        const bool match = pool->arenas[i].capacity == capacity && pool->arenas[i].small == small;
        slot = match && (slot == BUSTER_ARENA_POOL_SLOTS || pool->released[i] > pool->released[slot]) ? i : slot;
    }
    if (slot != BUSTER_ARENA_POOL_SLOTS)
    {
        result = pool->arenas[slot];
        pool->count -= 1;
        pool->arenas[slot] = pool->arenas[pool->count];
        pool->released[slot] = pool->released[pool->count];
    }
    else if (capacity != 0 && capacity <= BUSTER_ARENA_MAX_CAPACITY)
    {
        const uint64_t mapping_bytes = arena_mapping_bytes(capacity);
        bool allowed = true;
#if BUSTER_SANITIZER_RESOURCES
        allowed = arena_test_reservation_allowed(mapping_bytes);
        if (!allowed)
        {
            errno = ENOMEM;
        }
#endif
        uint8_t* mapping = allowed ? mmap(0, mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0) : MAP_FAILED;
        if (mapping != MAP_FAILED)
        {
            // The advice only chooses page sizes, so the arena works whether or not the kernel takes it. Without
            // MADV_NOHUGEPAGE, a kernel whose transparent huge pages are "always" would give the start a huge page too.
            uint64_t offset = 0;
            if (capacity > small)
            {
                const uintptr_t huge_begin = ((uintptr_t)mapping + small + BUSTER_ARENA_HUGE_PAGE_BYTES - 1) & ~(BUSTER_ARENA_HUGE_PAGE_BYTES - 1);
                offset = huge_begin - small - (uintptr_t)mapping;
                madvise(mapping, offset + small, MADV_NOHUGEPAGE);
                madvise((void*)huge_begin, mapping_bytes - offset - small, MADV_HUGEPAGE);
            }
            else
            {
                madvise(mapping, mapping_bytes, MADV_NOHUGEPAGE);
            }
            result.base = mapping + offset;
            result.capacity = capacity;
            result.mapping_offset = offset;
            result.small = small;
        }
    }
    return result;
}

static Arena arena_reserve(uint64_t capacity)
{
    return arena_reserve_small(capacity, BUSTER_ARENA_SMALL_BYTES);
}

static Arena arena_measure(void)
{
    return (Arena){
        .base = (uint8_t*)(uintptr_t)BUSTER_ARENA_MAX_CAPACITY,
        .capacity = BUSTER_ARENA_MAX_CAPACITY,
    };
}

static void arena_release(Arena* arena)
{
    if (arena->base != 0)
    {
        ArenaPool* pool = arena_pool;
        uint8_t* mapping = arena->base - arena->mapping_offset;
        const uint64_t bytes = arena_mapping_bytes(arena->capacity);
        const bool pooled = pool != 0 && madvise(mapping, bytes, MADV_DONTNEED) == 0;
        if (pooled)
        {
            // A free slot, or else the one whose arena was released longest ago, which is unmapped.
            uint32_t slot = pool->count;
            if (pool->count == BUSTER_ARENA_POOL_SLOTS)
            {
                slot = 0;
                for (uint32_t i = 1; i < pool->count; i += 1)
                {
                    slot = pool->released[i] < pool->released[slot] ? i : slot;
                }
                const Arena* old = pool->arenas + slot;
                munmap(old->base - old->mapping_offset, arena_mapping_bytes(old->capacity));
            }
            else
            {
                pool->count += 1;
            }
            pool->clock += 1;
            pool->released[slot] = pool->clock;
            pool->arenas[slot] = (Arena){
                .base = arena->base,
                .capacity = arena->capacity,
                .mapping_offset = arena->mapping_offset,
                .small = arena->small,
            };
        }
        else
        {
            munmap(mapping, bytes);
        }
    }
    *arena = (Arena){ 0 };
}

static void arena_pool_release(ArenaPool* pool)
{
    for (uint32_t i = 0; i < pool->count; i += 1)
    {
        munmap(pool->arenas[i].base - pool->arenas[i].mapping_offset, arena_mapping_bytes(pool->arenas[i].capacity));
    }
    pool->count = 0;
}

static void* arena_push(Arena* arena, uint64_t size, uint64_t alignment)
{
    void* result = 0;
    const uintptr_t position = (uintptr_t)arena->base + arena->length;
    const uintptr_t aligned = (position + alignment - 1) & ~(alignment - 1);
    const uint64_t begin = aligned - (uintptr_t)arena->base;
    // aligned < position only when rounding up wrapped around.
    if (alignment != 0 && (alignment & (alignment - 1)) == 0 && aligned >= position && begin <= arena->capacity && size <= arena->capacity - begin)
    {
        result = (void*)aligned;
        arena->length = begin + size;
        arena->peak = arena->length > arena->peak ? arena->length : arena->peak;
    }
    return result;
}

static void* arena_push_array(Arena* arena, uint64_t count, uint64_t element_bytes, uint64_t alignment)
{
    void* result = 0;
    uint64_t size = 0;
    if (!__builtin_mul_overflow(count, element_bytes, &size))
    {
        result = arena_push(arena, size, alignment);
    }
    return result;
}

static void arena_pop_to(Arena* arena, uint64_t length)
{
    assert(length <= arena->length);
    arena->length = length;
}

static void arena_poison(Arena* arena, uint8_t value)
{
    const uint64_t written = arena->peak > arena->length ? arena->peak : arena->length;
    if (arena->base != 0)
    {
        memset(arena->base, value, written < arena->capacity ? written : arena->capacity);
    }
    arena->length = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The thread context

// Each thread's scratch arenas: two, so that a function that pushes its results into one of them can take scratch memory
// from the other. Each is reserved when it is first used.
#define BUSTER_SCRATCH_ARENA_COUNT 2
#define BUSTER_SCRATCH_ARENA_CAPACITY (64ull << 30)

typedef struct LaneGroup LaneGroup;
typedef struct LaneTeam LaneTeam;
typedef struct LaneSession LaneSession;

// How deep lane_narrow's narrowings nest.
#define BUSTER_LANE_NARROW_DEPTH 8

// A thread's place in its group: its index and the group's size. A thread outside any group, or one that has made
// itself a group of one with lane_alone, is lane 0 of 1, without a group.
typedef struct Lane Lane;
struct Lane
{
    uint32_t index;
    uint32_t count;
    LaneGroup* group;
};

// A narrowing's record on its lane (lane_narrow): the lane and the single-lane counter it narrowed from, and whether the
// lane is one of the narrowed lanes.
typedef struct LaneNarrowing LaneNarrowing;
struct LaneNarrowing
{
    Lane outer;
    uint64_t taken;
    bool inside;
};

typedef struct ThreadContext ThreadContext;
struct ThreadContext
{
    Lane lane;
    // A command's fixed team, or 0 for standalone calls. running also guards calls made after lane_alone.
    LaneTeam* team;
    // The process's lane session the thread is a lane of (#905), or 0. Under a session nothing creates a thread.
    LaneSession* session;
    // Lane 0 serving its group's other lanes (lane_serve_begin): the group its lane_run regions run on, while lane 0
    // itself runs alone between them.
    Lane served;
    bool serving;
    // lane_narrow's narrowings, innermost last.
    LaneNarrowing narrowing[BUSTER_LANE_NARROW_DEPTH];
    uint32_t narrow_depth;
    bool running;
    Arena scratch[BUSTER_SCRATCH_ARENA_COUNT];
    // How many times the thread has slept on a futex, waiting for its group: for the tests and the benchmark.
    uint64_t sleeps;
    // lane_take's counter while the thread is a lane of one.
    uint64_t taken;
    // lane_wait's budget in TSC ticks once the thread has measured its TSC's rate, or 0, and the TSC's and
    // CLOCK_MONOTONIC's readings at its first wait past the spins, which it measures the rate from (lane_wait_ticks).
    uint64_t wait_ticks;
    uint64_t wait_origin_ticks;
    uint64_t wait_origin_nanoseconds;
};

// main.c's units share one context, the driver's unit's, since a command's code runs in more than one of them (main.c:
// a module's entry points compile in one unit, their home, which the others call); a unit of its own, such as a
// benchmark's, has its own.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern _Thread_local ThreadContext thread_context;
#else
#if defined(BUSTER_UNIT)
_Thread_local ThreadContext thread_context = {
#else
static _Thread_local ThreadContext thread_context = {
#endif
    .lane = {
        .index = 0,
        .count = 1,
        .group = 0,
    },
};
#endif

#if BUSTER_SANITIZER_RESOURCES
// The injected failures, the process's rather than a thread's: lanes reserve their arenas on their own threads. A test
// sets them before the lanes start and puts them back after they end.
typedef struct LaneTestResources LaneTestResources;
struct LaneTestResources
{
    bool no_threads;
    bool arena_limited;
    uint64_t arena_limit;
};

#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern LaneTestResources lane_test_resources;
#elif defined(BUSTER_UNIT)
LaneTestResources lane_test_resources;
#else
static LaneTestResources lane_test_resources;
#endif

static bool arena_test_reservation_allowed(uint64_t bytes)
{
    return !lane_test_resources.arena_limited || bytes <= lane_test_resources.arena_limit;
}
#endif

// What the process's lanes keep (#905): the threads the compiler has created in it (a session's, a team's and a cold
// group's, joined or not), for the tests' accounting; and a test's hook, in every build: a session's lane of index
// `fail_at` fails to be created, as pthread_create fails when the process may start no more threads (UINT32_MAX for
// none). Process-wide, as main.c's units share it, and in .data rather than .bss, as its initializer isn't zero: the
// first write to a 2 MB region of the executable's .bss clears a transparent huge page, which a compile that touched
// nothing else there paid for (2 MB of peak RSS, on the 9700X).
typedef struct LaneProcess LaneProcess;
struct LaneProcess
{
    uint64_t threads_created;
    uint32_t fail_at;
};
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern LaneProcess lane_process;
#elif defined(BUSTER_UNIT)
LaneProcess lane_process = { .fail_at = UINT32_MAX };
#else
static LaneProcess lane_process = { .fail_at = UINT32_MAX };
#endif

typedef struct Scratch Scratch;
struct Scratch
{
    Arena* arena;
    // The arena's length when the scratch began, which scratch_end returns it to.
    uint64_t length;
};

// Scratch memory from one of the thread's scratch arenas that isn't `conflict` (the arena the caller pushes its results
// into, or 0), until scratch_end frees it. If the arena can't be reserved, its pushes fail.
static __attribute__((unused)) Scratch scratch_begin(const Arena* conflict);
static __attribute__((unused)) void scratch_end(Scratch scratch);

static Scratch scratch_begin(const Arena* conflict)
{
    Arena* arena = &thread_context.scratch[0];
    for (uint32_t i = 1; arena == conflict && i < BUSTER_SCRATCH_ARENA_COUNT; i += 1)
    {
        arena = &thread_context.scratch[i];
    }
    if (arena->base == 0)
    {
        *arena = arena_reserve(BUSTER_SCRATCH_ARENA_CAPACITY);
    }
    Scratch result = {
        .arena = arena,
        .length = arena->length,
    };
    return result;
}

static void scratch_end(Scratch scratch)
{
    arena_pop_to(scratch.arena, scratch.length);
}

// Releases a worker's scratch arenas after its whole operation, including when its thread is kept for the next one.
static void scratch_release(void)
{
    for (uint32_t i = 0; i < BUSTER_SCRATCH_ARENA_COUNT; i += 1)
    {
        arena_release(&thread_context.scratch[i]);
    }
}

// Empties a worker's scratch arenas after its whole operation, keeping their pages for its next one.
static void scratch_empty(void)
{
    for (uint32_t i = 0; i < BUSTER_SCRATCH_ARENA_COUNT; i += 1)
    {
        arena_pop_to(&thread_context.scratch[i], 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Lanes

// How many times a waiting lane reads the generation word, with a pause between reads (11.7 ns on the 9700X), before it
// waits in mwaitx: 1.5 µs, far longer than a sync at which every lane arrives at once takes (35 to 200 ns with 2 to 8
// lanes), so such a sync never waits in mwaitx.
#define BUSTER_LANE_SPINS 128
// How long a lane then waits in mwaitx before it sleeps on a futex: 1 ms. Woken from mwaitx, a lane there leaves 0.2 to
// 0.3 µs after the word changes, against 10 to 14 µs from the futex, and a core waiting either way draws about 2 W, half
// what a spinning one does. What a long wait costs comes after it: a core that waited longer than 0.6 ms on the futex,
// or 1.5 ms in mwaitx, has its clock halved and loses 25 to 50% of the next millisecond's work
// (docs/object-writer-and-linker.md, "Lanes"). Past 1 ms the lane sleeps, since the waits that slow it down do so
// whichever way it waits, and on the futex its core can go into its deepest idle state, at 0.07 W.
#define BUSTER_LANE_WAIT_NANOSECONDS 1000000ull
// The same in TSC ticks at the 9700X's 3.79 GHz, which a thread waits until it has measured its own TSC's rate: the
// TSC runs at a rate of each CPU's own, so that another CPU's 1 ms is another number of ticks.
#define BUSTER_LANE_MWAITX_TICKS 3800000ull
// How far apart in time a thread's two readings of the TSC and CLOCK_MONOTONIC must be for their ratio to give the
// rate: 10 ms, over which the gap between the two clocks' reads (tens of nanoseconds) is an error of 10^-5.
#define BUSTER_LANE_WAIT_CALIBRATION_NANOSECONDS 10000000ull
// mwaitx's arguments: bit 1 of its extensions enables its timer, and its hint asks for C1, which on the 9700X wakes as
// fast as C0's (0xf0) and draws a little less.
#define BUSTER_LANE_MWAITX_TIMER 2u
#define BUSTER_LANE_MWAITX_C1 0u
#define BUSTER_LANE_CACHE_LINE_BYTES 64
// Zen's adjacent-line prefetcher fetches lines in pairs, so a line written by every lane is kept from its neighbours.
#define BUSTER_LANE_LINE_PAIR_BYTES 128
#define BUSTER_LANE_MAX_COUNT 256
// lane_parts_by_cost cuts a loop into this many parts per lane (docs/object-writer-and-linker.md, "Lanes", rule 1):
// enough that a lane that runs slow takes fewer, few enough that cutting and taking them costs a phase 2 to 7 µs with 8
// lanes on the 9700X.
#define BUSTER_LANE_PARTS_PER_LANE 64
#define BUSTER_LANE_MAX_PARTS (BUSTER_LANE_PARTS_PER_LANE * BUSTER_LANE_MAX_COUNT)
// lane_parts_by_cost's order: a part's bucket is the count of leading zeros of its cost, 64 for a part without one.
#define BUSTER_LANE_COST_BUCKETS 65

typedef struct Range64 Range64;
struct Range64
{
    uint64_t begin;
    uint64_t end;
};

// What a group's lanes share: the barrier's two cache lines, and lane_run's. Every lane writes the arrival line at every
// sync, while the generation line is written once per sync, by the last lane to arrive, and read by the lanes waiting
// for it, so arrivals never disturb the waiting. A broadcast's source offers its value on the arrival line, which it
// writes anyway as it arrives; the last lane to arrive, which has that line, copies the value next to the generation, and
// a waiting lane gets both in one read of the line. On the 9700X that makes a broadcast cost what a sync does, at every
// lane count, where two slots on the generation line, written by the source, made it about 40% dearer than a sync with 3
// to 8 lanes; putting everything on one line instead made syncs with 7 and 8 lanes 13-16% dearer (benchmark-lanes;
// docs/object-writer-and-linker.md, "Lanes").
struct LaneGroup
{
    // How many lanes have arrived at the current sync. The last one resets it to 0 before it advances the generation.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t arrived;
    // A broadcast's value, which its source lane writes before it arrives.
    uint64_t offered;
    // Twice the number of syncs completed. Bit 0 is set while a lane sleeps on the word.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t generation;
    // `offered` as it was when every lane had arrived, which the lanes read after the sync. The last lane to arrive at the
    // next sync, which is the next to write it, does so only after every lane has arrived there, and so read it.
    uint64_t value;
    // lane_run's: the gate its threads wait at until it has created them all, and the entry point they run.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t gate;
    void (*entry)(void* argument);
    void* argument;
    // lane_take's counter, which every lane adds to: on a pair of lines of its own, so that taking never disturbs the
    // barrier.
    _Alignas(BUSTER_LANE_LINE_PAIR_BYTES) uint64_t taken;
};

// A session's lane start (lane_session_run): its session and index, and with a CPU list whether its thread pinned
// itself, which it publishes in `ready`.
typedef struct LaneSessionStart LaneSessionStart;
struct LaneSessionStart
{
    LaneSession* session;
    uint32_t index;
    uint32_t ready;
    bool pinned;
};

// A process's lanes (#905): the calling thread as lane 0 and capacity - 1 threads, created once, before the command's
// work, which run its outer flow (entry) together, every lane calling it, until it ends. A session of one creates no
// thread. Under a session nothing creates a thread: lane_run runs its regions on the session's lanes.
struct LaneSession
{
    // Every lane's group: the outer flow's rendezvous and the announcements of lane 0's regions (lane_serve).
    LaneGroup group;
    // lane_narrow's groups, one per depth. A narrowing's lanes leave its group as they found it (every sync complete,
    // the take counter back at 0), so the next narrowing at its depth takes it as it is.
    LaneGroup narrowed[BUSTER_LANE_NARROW_DEPTH];
    // The start's gate, which the created lanes wait at until lane 0 knows every lane exists: BUSTER_LANE_GATE_RUN or
    // BUSTER_LANE_GATE_CANCEL.
    uint32_t gate;
    // The lanes asked for, the lanes that run (1 after a failed start), and the lanes that existed (lane 0's among them).
    uint32_t capacity;
    uint32_t count;
    uint32_t created;
    pid_t process;
    void (*entry)(void* argument);
    void* argument;
    // Whether the lanes stay parked when the flow ends, for the process's exit to end (a terminal command's), rather than
    // being joined; whether lane 1 creates the lanes after it while lane 0 runs (BUSTER_LANE_SESSION_LAZY); and whether
    // lane 0 knows how its lanes' start went (lane_session_settle), which only lane 0 reads and writes.
    bool park_at_end;
    bool lazy;
    bool settled;
    bool has_cpus;
    // Lane 0's thread, the one that settles.
    pthread_t owner;
    uint32_t cpus[BUSTER_LANE_MAX_COUNT];
    pthread_t threads[BUSTER_LANE_MAX_COUNT];
    LaneSessionStart starts[BUSTER_LANE_MAX_COUNT];
};

// The calling thread's lane: its index, and the group's size.
static __attribute__((unused)) uint32_t lane_index(void);
static __attribute__((unused)) uint32_t lane_count(void);
// Waits for every lane of the group. With one lane, returns at once.
static __attribute__((unused)) void lane_sync(void);
// Every lane gets source_lane's value. Like lane_sync, it waits for every lane.
static __attribute__((unused)) uint64_t lane_broadcast(uint64_t value, uint32_t source_lane);
// The lane that runs narrow task `task_index` of several spread over the lanes, each of which then broadcasts its
// task's result from that lane.
static __attribute__((unused)) uint32_t lane_from_task_index(uint64_t task_index);
// This lane's part of [0, count): contiguous, in lane order, the first count % lane_count() parts one longer.
static __attribute__((unused)) Range64 lane_range(uint64_t count);
// The same, split by cost: cost_prefix holds the inclusive prefix sums of the items' costs (so it never decreases), and
// each item goes to the lane whose share of the total cost holds the item's midpoint. Each part's cost is then within
// the largest item's of an equal share. Without any cost, it is lane_range's split.
static __attribute__((unused)) Range64 lane_range_by_cost(const uint64_t* cost_prefix, uint64_t count);
// Part `part` of [0, count) cut into `parts`: what lane_range would give lane `part` of a group of `parts` lanes.
static __attribute__((unused)) Range64 range_part(uint64_t count, uint64_t part, uint64_t parts);
// The same by cost: what lane_range_by_cost would give that lane.
static __attribute__((unused)) Range64 range_part_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t part, uint64_t parts);
// How many parts lane_parts_by_cost cuts `count` items into: BUSTER_LANE_PARTS_PER_LANE per lane, but no more than the
// items, and one part (none without items) with one lane.
static __attribute__((unused)) uint64_t lane_part_count(uint64_t count);
// Cuts [0, count) by cost into lane_part_count(count) parts, as range_part_by_cost cuts, and returns how many. The
// parts are contiguous and in canonical order: part i is [boundaries[i], boundaries[i + 1]), and boundaries gets
// parts + 1 values. order gets the parts in the order to take them: the costliest first (the first of them on a tie),
// then the others by decreasing cost, to within a factor of two (the cost's bit width), ties in index order. Every lane
// calls it with the same boundaries, which the lanes fill together and which is read-only once it returns, and with an
// order of its own, which every lane fills alike. Like lane_sync, it waits for every lane, so it can be the sync that
// starts a phase. A loop over the parts:
//
//     const uint64_t parts = lane_parts_by_cost(cost_prefix, count, boundaries, order);
//     for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
//     {
//         const uint32_t part = order[taken];
//         ... the items from boundaries[part] to boundaries[part + 1] ...
//     }
static __attribute__((unused)) uint64_t lane_parts_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t* boundaries, uint32_t* order);
// lane_parts_by_cost's parts and order for a group of `lanes` lanes (BUSTER_LANE_MAX_COUNT for more, as no group has
// more), made by the calling thread alone, and how many: for a phase cut before its lanes start, whose lanes then take
// the parts with no sync before their first take (c_check_deferred.c's first lane reads the costliest part while the
// others wake). The lanes read boundaries and order as lane_run's dispatch publishes them.
static __attribute__((unused)) uint64_t parts_by_cost(const uint64_t* cost_prefix, uint64_t count, uint32_t lanes, uint64_t* boundaries, uint32_t* order);
// The next number this lane takes from its group's counter: 0, 1, 2, ... across the group's lanes, and `parts` or more
// once they are all taken. Every lane of the group takes until it gets `parts` or more, and then no more until the
// lanes have synced: the last take of the loop resets the counter for the next one.
static __attribute__((unused)) uint64_t lane_take(uint64_t parts);
// Makes the calling thread a group of one (lane 0 of 1, which syncs with no one), to run a phase alone while the rest of
// its group does something else, and returns its lane for lane_restore.
static __attribute__((unused)) Lane lane_alone(void);
static __attribute__((unused)) void lane_restore(Lane lane);
// lane_sync and lane_broadcast for lanes that wait out a long stretch, such as lane 0's serial work (#905): a lane that
// waits sleeps on the futex at once, as a team's workers wait between operations, rather than spinning and waiting in
// mwaitx first. With one lane, they return at once.
static __attribute__((unused)) void lane_park(void);
static __attribute__((unused)) uint64_t lane_park_broadcast(uint64_t value, uint32_t source_lane);
// Narrows the group to its first `lanes` lanes for a region (#905): each of them becomes a lane of a group of that many,
// the session's group for the narrowing's depth, and gets true; the others get false and nothing changes for them.
// lane_widen ends the region: the narrowed lanes meet at their group's sync, and every lane gets its lane back. Every
// lane of the group calls both. Only a session's groups narrow to more than one lane, nesting to
// BUSTER_LANE_NARROW_DEPTH.
static __attribute__((unused)) bool lane_narrow(uint32_t lanes);
static __attribute__((unused)) void lane_widen(void);

static uint32_t lane_index(void)
{
    return thread_context.lane.index;
}

static uint32_t lane_count(void)
{
    return thread_context.lane.count;
}

// lane_wait's budget past the spins in TSC ticks, given the TSC read at the wait's start: BUSTER_LANE_WAIT_NANOSECONDS
// at this CPU's TSC rate. A thread measures the rate against CLOCK_MONOTONIC from its first such wait to the first one
// at least BUSTER_LANE_WAIT_CALIBRATION_NANOSECONDS later, waiting the 9700X's BUSTER_LANE_MWAITX_TICKS until then: a
// read of the clock (vDSO, about 20 ns) per wait that has spun 1.5 µs already, and none once measured. The budget is
// kept within mwaitx's 32-bit timer.
static uint64_t lane_wait_ticks(uint64_t now)
{
    uint64_t result = thread_context.wait_ticks;
    if (result == 0)
    {
        result = BUSTER_LANE_MWAITX_TICKS;
        struct timespec time;
        clock_gettime(CLOCK_MONOTONIC, &time);
        const uint64_t nanoseconds = (uint64_t)time.tv_sec * 1000000000ull + (uint64_t)time.tv_nsec;
        if (thread_context.wait_origin_nanoseconds == 0)
        {
            thread_context.wait_origin_ticks = now;
            thread_context.wait_origin_nanoseconds = nanoseconds;
        }
        else if (nanoseconds - thread_context.wait_origin_nanoseconds >= BUSTER_LANE_WAIT_CALIBRATION_NANOSECONDS)
        {
            const uint64_t elapsed = nanoseconds - thread_context.wait_origin_nanoseconds;
            const unsigned __int128 ticks = (unsigned __int128)(now - thread_context.wait_origin_ticks) * BUSTER_LANE_WAIT_NANOSECONDS;
            const unsigned __int128 measured = ticks / elapsed;
            result = measured == 0 ? 1 : measured > UINT32_MAX ? UINT32_MAX : (uint64_t)measured;
            thread_context.wait_ticks = result;
        }
    }
    return result;
}

// Waits while *word, but for its bit 0, is `value`, and returns its new value without bit 0: sets bit 0, so that whoever
// changes the word wakes it, and sleeps on a futex while the word is unchanged. lane_wait's last step, and on its own a
// wait that is likely long (#777's board, whose lanes without units wait for parts while the others compile), during
// which a core that spun or waited in mwaitx would draw power that the cores at work lack for their clocks.
static uint32_t lane_sleep(uint32_t* word, uint32_t value)
{
    uint32_t current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    while ((current & ~1u) == value)
    {
        if ((current & 1u) == 0)
        {
            // If the word changed meanwhile, the exchange fails and loads its new value into current.
            if (__atomic_compare_exchange_n(word, &current, current | 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
            {
                current |= 1u;
            }
        }
        else
        {
            // Returns at once if the word is no longer `current`, and on a wake or a signal.
            syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, current, 0, 0, 0);
            thread_context.sleeps += 1;
            current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
        }
    }
    return current & ~1u;
}

// Waits while *word, but for its bit 0, is `value`, and returns its new value without bit 0. Spins first, then waits in
// mwaitx; then sleeps (lane_sleep).
static uint32_t lane_wait(uint32_t* word, uint32_t value)
{
    uint32_t current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    for (uint32_t spin = 0; (current & ~1u) == value && spin < BUSTER_LANE_SPINS; spin += 1)
    {
        _mm_pause();
        current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    }
    // A wait that ended while spinning, as at most syncs, doesn't read the clock (rdtsc is 27 µops on Zen 5).
    if ((current & ~1u) == value)
    {
        const uint64_t begin = __rdtsc();
        const uint64_t budget = lane_wait_ticks(begin);
        uint64_t waited = 0;
        while ((current & ~1u) == value && waited < budget)
        {
#if defined(__MWAITX__)
            // The monitor is armed before the read, so a write after the read ends the wait. The wait also ends on an
            // interrupt, or when the timer runs out the rest of the ticks.
            _mm_monitorx(word, 0, 0);
            current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
            if ((current & ~1u) == value)
            {
                _mm_mwaitx(BUSTER_LANE_MWAITX_TIMER, BUSTER_LANE_MWAITX_C1, (uint32_t)(budget - waited));
                current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
            }
#else
            // A build host without mwaitx (-march=native on a CPU other than AMD's) spins with pause for the same ticks
            // instead: the same wake-up order, at the cost of a busy core while it waits. The 9700X builds the branch
            // above.
            _mm_pause();
            current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
#endif
            waited = __rdtsc() - begin;
        }
    }
    return (current & ~1u) == value ? lane_sleep(word, value) : current & ~1u;
}

// Sets *word to `value`, whose bit 0 is clear, and wakes the lanes sleeping on it in lane_wait. The exchange returns the
// word's latest value, so it sees bit 0 if a lane set it before the change; a lane that tries to set it after the change
// fails, sees the new value and doesn't sleep.
static void lane_publish(uint32_t* word, uint32_t value)
{
    const uint32_t previous = __atomic_exchange_n(word, value, __ATOMIC_RELEASE);
    if ((previous & 1u) != 0)
    {
        syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, INT_MAX, 0, 0, 0);
    }
}

// Adds two to *word, its bit 0 cleared, and wakes the lanes sleeping on it in lane_wait: for a word that several threads
// advance at once, whose advances lane_publish's exchanges could set back (#777's board). A lane waiting on the old
// value sees the change, and one asleep on it is woken; one that already waits on the new value, if this clears its bit
// 0, finds the word changed under its futex and sets the bit again before it sleeps.
static __attribute__((unused)) void lane_advance(uint32_t* word)
{
    const uint32_t previous = __atomic_fetch_add(word, 2, __ATOMIC_RELEASE);
    if ((previous & 1u) != 0)
    {
        __atomic_fetch_and(word, ~1u, __ATOMIC_RELAXED);
        syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, INT_MAX, 0, 0, 0);
    }
}

// Arrives at the sync of `generation`, read before arriving, and returns once every lane of the group has arrived. The
// release in each arrival and the acquire in the last one, and the release in advancing the generation and the acquire
// in each waiting lane's read of it, make every lane's writes before the sync visible to every lane after it.
static void lane_arrive(LaneGroup* group, uint32_t count, uint32_t generation)
{
    if (__atomic_add_fetch(&group->arrived, 1, __ATOMIC_ACQ_REL) == count)
    {
        __atomic_store_n(&group->arrived, 0, __ATOMIC_RELAXED);
        group->value = group->offered;
        lane_publish(&group->generation, generation + 2);
    }
    else
    {
        lane_wait(&group->generation, generation);
    }
}

static void lane_sync(void)
{
    const Lane lane = thread_context.lane;
    if (lane.count > 1)
    {
        // The generation can't advance until this lane has arrived.
        const uint32_t generation = __atomic_load_n(&lane.group->generation, __ATOMIC_RELAXED) & ~1u;
        lane_arrive(lane.group, lane.count, generation);
    }
}

static uint64_t lane_broadcast(uint64_t value, uint32_t source_lane)
{
    const Lane lane = thread_context.lane;
    uint64_t result = value;
    assert(source_lane < lane.count);
    if (lane.count > 1)
    {
        // `offered` was last read by the last lane to arrive at the previous sync, before it let this lane leave.
        const uint32_t generation = __atomic_load_n(&lane.group->generation, __ATOMIC_RELAXED) & ~1u;
        if (lane.index == source_lane)
        {
            lane.group->offered = value;
        }
        lane_arrive(lane.group, lane.count, generation);
        result = lane.group->value;
    }
    return result;
}

static uint32_t lane_from_task_index(uint64_t task_index)
{
    return (uint32_t)(task_index % thread_context.lane.count);
}

static Range64 range_part(uint64_t count, uint64_t part, uint64_t parts)
{
    const uint64_t share = count / parts;
    const uint64_t longer = count % parts;
    const uint64_t begin = part * share + (part < longer ? part : longer);
    Range64 result = {
        .begin = begin,
        .end = begin + share + (part < longer ? 1 : 0),
    };
    return result;
}

static Range64 lane_range(uint64_t count)
{
    return range_part(count, thread_context.lane.index, thread_context.lane.count);
}

// The first item of part `part` of `parts` by cost: the first whose midpoint, in cost, is past `part` shares of the
// total. With prefix sums P (P[-1] being 0), item i's midpoint is (P[i - 1] + P[i]) / 2 and part k's share starts at
// total * k / parts, so the comparison, multiplied out, is exact. Its products fit in 97 bits.
static uint64_t lane_cost_boundary(const uint64_t* cost_prefix, uint64_t count, uint64_t part, uint64_t parts)
{
    const unsigned __int128 target = (unsigned __int128)cost_prefix[count - 1] * 2 * part;
    uint64_t low = 0;
    uint64_t high = count;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        const unsigned __int128 before = middle == 0 ? 0 : cost_prefix[middle - 1];
        if ((before + cost_prefix[middle]) * parts > target)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return low;
}

static Range64 range_part_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t part, uint64_t parts)
{
    Range64 result;
    if (count == 0 || cost_prefix[count - 1] == 0)
    {
        result = range_part(count, part, parts);
    }
    else
    {
        result.begin = part == 0 ? 0 : lane_cost_boundary(cost_prefix, count, part, parts);
        result.end = part + 1 == parts ? count : lane_cost_boundary(cost_prefix, count, part + 1, parts);
    }
    return result;
}

static Range64 lane_range_by_cost(const uint64_t* cost_prefix, uint64_t count)
{
    return range_part_by_cost(cost_prefix, count, thread_context.lane.index, thread_context.lane.count);
}

// lane_part_count for a group of `lanes` lanes.
static uint64_t lane_part_count_for(uint64_t count, uint64_t lanes)
{
    const uint64_t parts = lanes == 1 ? 1 : BUSTER_LANE_PARTS_PER_LANE * lanes;
    return count < parts ? count : parts;
}

static uint64_t lane_part_count(uint64_t count)
{
    return lane_part_count_for(count, thread_context.lane.count);
}

// Part i's boundary, the first item of `parts` parts cut by cost (range_part_by_cost's begin), and `count` for i == parts.
static uint64_t lane_part_boundary(const uint64_t* cost_prefix, uint64_t count, uint64_t i, uint64_t parts)
{
    const bool costless = count == 0 || cost_prefix[count - 1] == 0;
    return i == parts ? count : costless ? range_part(count, i, parts).begin : i == 0 ? 0 : lane_cost_boundary(cost_prefix, count, i, parts);
}

// The parts' order to take them in, from their boundaries: a counting sort by bucket, which keeps index order within a
// bucket, after the costliest part, which it finds in the same pass: a bucket's costs differ by up to a factor of two,
// and a caller may give the costliest part to a lane of its choosing (c_check_deferred.c gives it to the first lane,
// which runs fastest). A part's cost is the prefix at its end less the prefix at the last part's end, so finding the
// buckets reads the prefix sums once per part.
static void lane_parts_order(const uint64_t* cost_prefix, const uint64_t* boundaries, uint64_t parts, uint32_t* order)
{
    uint8_t buckets[BUSTER_LANE_MAX_PARTS];
    uint64_t next[BUSTER_LANE_COST_BUCKETS] = { 0 };
    uint64_t before = 0;
    uint64_t costliest = 0;
    uint64_t most = 0;
    for (uint64_t part = 0; part < parts; part += 1)
    {
        const uint64_t end = boundaries[part + 1];
        const uint64_t after = end > boundaries[part] ? cost_prefix[end - 1] : before;
        const uint64_t cost = after - before;
        const uint32_t bucket = cost == 0 ? BUSTER_LANE_COST_BUCKETS - 1 : (uint32_t)__builtin_clzll(cost);
        buckets[part] = (uint8_t)bucket;
        next[bucket] += 1;
        costliest = cost > most ? part : costliest;
        most = cost > most ? cost : most;
        before = after;
    }
    // The costliest part takes the first place, and its bucket counts it no more.
    uint64_t offset = 0;
    if (parts != 0)
    {
        order[0] = (uint32_t)costliest;
        next[buckets[costliest]] -= 1;
        offset = 1;
    }
    for (uint32_t bucket = 0; bucket < BUSTER_LANE_COST_BUCKETS; bucket += 1)
    {
        const uint64_t in_bucket = next[bucket];
        next[bucket] = offset;
        offset += in_bucket;
    }
    for (uint64_t part = 0; part < parts; part += 1)
    {
        // The costliest part writes its own first place again, and leaves its bucket's next place to the others.
        const bool first = part == costliest;
        order[first ? 0 : next[buckets[part]]] = (uint32_t)part;
        next[buckets[part]] += !first;
    }
}

// The lanes find the boundaries together, a range of them each, with one binary search per boundary. Then each lane
// orders the parts (lane_parts_order).
static uint64_t lane_parts_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t* boundaries, uint32_t* order)
{
    const uint64_t parts = lane_part_count(count);
    const Range64 mine = lane_range(parts + 1);
    for (uint64_t i = mine.begin; i < mine.end; i += 1)
    {
        boundaries[i] = lane_part_boundary(cost_prefix, count, i, parts);
    }
    lane_sync();
    lane_parts_order(cost_prefix, boundaries, parts, order);
    return parts;
}

static uint64_t parts_by_cost(const uint64_t* cost_prefix, uint64_t count, uint32_t lanes, uint64_t* boundaries, uint32_t* order)
{
    const uint64_t parts = lane_part_count_for(count, lanes < BUSTER_LANE_MAX_COUNT ? lanes : BUSTER_LANE_MAX_COUNT);
    for (uint64_t i = 0; i <= parts; i += 1)
    {
        boundaries[i] = lane_part_boundary(cost_prefix, count, i, parts);
    }
    lane_parts_order(cost_prefix, boundaries, parts, order);
    return parts;
}

static uint64_t lane_take(uint64_t parts)
{
    const Lane lane = thread_context.lane;
    uint64_t result = 0;
    if (lane.count > 1)
    {
        // The data the parts are made of was published by the sync before the loop, or by lane_run's dispatch for parts
        // cut before it (parts_by_cost), so the take orders nothing else.
        result = __atomic_fetch_add(&lane.group->taken, 1, __ATOMIC_RELAXED);
        // Each lane's last take gets `parts` or more, so a loop makes parts + count takes in all, and the one that gets
        // the last number is the loop's last. The next loop's takes come after a sync, which this store precedes.
        if (result == parts + lane.count - 1)
        {
            __atomic_store_n(&lane.group->taken, 0, __ATOMIC_RELAXED);
        }
    }
    else
    {
        result = thread_context.taken;
        thread_context.taken = result < parts ? result + 1 : 0;
    }
    return result;
}

static Lane lane_alone(void)
{
    const Lane result = thread_context.lane;
    thread_context.lane = (Lane){
        .index = 0,
        .count = 1,
        .group = 0,
    };
    return result;
}

static void lane_restore(Lane lane)
{
    thread_context.lane = lane;
}

// lane_arrive for a lane that waits out a long stretch: it sleeps at once (lane_sleep).
static void lane_arrive_parked(LaneGroup* group, uint32_t count, uint32_t generation)
{
    if (__atomic_add_fetch(&group->arrived, 1, __ATOMIC_ACQ_REL) == count)
    {
        __atomic_store_n(&group->arrived, 0, __ATOMIC_RELAXED);
        group->value = group->offered;
        lane_publish(&group->generation, generation + 2);
    }
    else
    {
        lane_sleep(&group->generation, generation);
    }
}

static void lane_park(void)
{
    const Lane lane = thread_context.lane;
    if (lane.count > 1)
    {
        const uint32_t generation = __atomic_load_n(&lane.group->generation, __ATOMIC_RELAXED) & ~1u;
        lane_arrive_parked(lane.group, lane.count, generation);
    }
}

static uint64_t lane_park_broadcast(uint64_t value, uint32_t source_lane)
{
    const Lane lane = thread_context.lane;
    uint64_t result = value;
    assert(source_lane < lane.count);
    if (lane.count > 1)
    {
        // As lane_broadcast's: `offered` was last read by the last lane to arrive at the previous sync.
        const uint32_t generation = __atomic_load_n(&lane.group->generation, __ATOMIC_RELAXED) & ~1u;
        if (lane.index == source_lane)
        {
            lane.group->offered = value;
        }
        lane_arrive_parked(lane.group, lane.count, generation);
        result = lane.group->value;
    }
    return result;
}

static bool lane_narrow(uint32_t lanes)
{
    const Lane outer = thread_context.lane;
    const uint32_t depth = thread_context.narrow_depth;
    const uint32_t count = lanes == 0 ? 1 : lanes < outer.count ? lanes : outer.count;
    const bool inside = outer.index < count;
    assert(depth < BUSTER_LANE_NARROW_DEPTH);
    assert(count == 1 || thread_context.session != 0);
    thread_context.narrowing[depth] = (LaneNarrowing){
        .outer = outer,
        .taken = thread_context.taken,
        .inside = inside,
    };
    thread_context.narrow_depth = depth + 1;
    if (inside)
    {
        thread_context.lane = (Lane){
            .index = outer.index,
            .count = count,
            .group = count > 1 ? &thread_context.session->narrowed[depth] : 0,
        };
        thread_context.taken = 0;
    }
    return inside;
}

// lane_sync's arrival without the wait: the last lane to arrive advances the generation, and the others go on at once.
static void lane_arrive_only(void)
{
    const Lane lane = thread_context.lane;
    if (lane.count > 1)
    {
        const uint32_t generation = __atomic_load_n(&lane.group->generation, __ATOMIC_RELAXED) & ~1u;
        if (__atomic_add_fetch(&lane.group->arrived, 1, __ATOMIC_ACQ_REL) == lane.count)
        {
            __atomic_store_n(&lane.group->arrived, 0, __ATOMIC_RELAXED);
            lane.group->value = lane.group->offered;
            lane_publish(&lane.group->generation, generation + 2);
        }
    }
}

// The region ends at its group's sync, which only lane 0 waits at: it goes on from the region with the narrowed lanes'
// writes, while the others arrive and go straight on to the outer group's next rendezvous, asleep, rather than waiting
// for the region's slowest lane. None of them meets the narrowed group again before lane 0 has left it: the next
// narrowing at its depth comes after an outer rendezvous that lane 0 reaches only after this. The lanes the narrowing
// left out never entered the region.
static void lane_widen(void)
{
    const uint32_t depth = thread_context.narrow_depth - 1;
    const LaneNarrowing narrowing = thread_context.narrowing[depth];
    if (narrowing.inside && thread_context.lane.index == 0)
    {
        lane_sync();
    }
    else if (narrowing.inside)
    {
        lane_arrive_only();
    }
    thread_context.narrow_depth = depth;
    thread_context.lane = narrowing.outer;
    thread_context.taken = narrowing.taken;
}

// ---------------------------------------------------------------------------------------------------------------------
// Starting lanes

// The values of LaneGroup.gate: lane_run's threads wait while it is 0.
#define BUSTER_LANE_GATE_RUN 2u
#define BUSTER_LANE_GATE_CANCEL 4u
// A team's initial request, published as completed once a worker has attempted its pin. Later requests advance by 2.
#define BUSTER_LANE_TEAM_READY 2u
// sched_setaffinity's masks: 1,024 CPUs, as glibc's cpu_set_t.
#define BUSTER_LANE_CPU_MASK_WORDS 16

// Runs one whole operation on `count` lanes, with the calling thread as lane 0. Its phases call one another directly
// inside entry; this is the runtime's only work callback. With a command team, count selects a prefix of its fixed
// threads and cpus must match that prefix of the team's CPU list (including whether it has one). The call waits for
// all active lanes, then restores the caller's lane, single-lane counter and affinity. A nested, oversized or otherwise
// invalid team dispatch runs nothing and returns false; it never starts extra threads. A team inherited across fork
// also refuses dispatch, since its worker threads do not exist in the child.
//
// Without a team, starts count - 1 threads and joins them before returning, preserving the standalone and cold-start
// path. A failed thread creation runs no entry; a pinning failure on this path runs unpinned and returns false.
//
// Under a session (#905) it creates no thread. Lane 0 serving its group (lane_serve_begin) announces the operation as a
// region, which the first count lanes of the group run and the others skip; a lane alone (lane 0's serial work between
// regions, a session of one) runs it on its own; and a lane of a group of several that serves nothing is refused, as a
// nested run is. cpus must be null or a prefix of the session's.
static __attribute__((unused)) bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus);

// lane_session_run's flags. PARK_AT_END: the lanes stay parked when the flow ends, for the process's exit to end them (a
// terminal command's, whose process exits next), but in sanitizer builds, which join them always; the session must then
// outlive the process. LAZY: lane 0 creates lane 1 alone and runs the flow at once, while lane 1 creates the others; lane
// 0 learns how their start went with lane_session_settle, which it must call before it meets any other lane or counts on
// them (but for lane_count, which says the lanes asked for until then). Without LAZY, or with a CPU list, lane 0 creates
// every lane before the flow.
#define BUSTER_LANE_SESSION_PARK_AT_END 1u
#define BUSTER_LANE_SESSION_LAZY 2u

// Runs entry(argument) on every lane of a new session of `capacity` lanes (#905), the process's outer flow: the calling
// thread is lane 0, and the session creates capacity - 1 threads (none for one) with the asynchronous signals blocked,
// so that a signal sent to the process reaches lane 0, and an explicit stack size. With `cpus`, each lane pins itself to
// its CPU, and lane 0 waits for the pins; without, it waits for nothing but each thread's creation, or with
// BUSTER_LANE_SESSION_LAZY for lane 1's alone. A failed creation or pin cancels every lane created, which the session
// joins, and runs the flow on lane 0 alone: one lane, never a retry. Returns how many lanes ran it (0 if a session of
// the calling thread's is live: sessions don't nest). The lanes are joined when the flow ends, but with
// BUSTER_LANE_SESSION_PARK_AT_END (`flags`).
static __attribute__((unused)) uint32_t lane_session_run(LaneSession* session, uint32_t capacity, const uint32_t* cpus, void (*entry)(void* argument),
                                                         void* argument, uint32_t flags);
// The lanes the calling thread's session runs on: for lane 0 of a lazy session, once lane 1 has said how their start
// went, which it waits for the first time; on a failed start, lane 0's lane in the session's group becomes a lane alone,
// and the session's count 1. 1 outside a session.
static __attribute__((unused)) uint32_t lane_session_settle(void);
// The session the calling thread is a lane of, or 0. A child forked from a lane of a session forgets it here, and runs as
// one lane: its other lanes don't exist in the child.
static __attribute__((unused)) LaneSession* lane_session_current(void);
// Lane 0 of a session's group serves the others' regions (lane_run) from its serial work: lane_serve_begin, which settles
// a lazy session first (lane_session_settle), makes it run alone between them, and lane_serve_end ends the serving
// lanes' lane_serve. The other lanes of the group call lane_serve meanwhile (#905): for serial code with lane_run calls,
// which the in-process driver tests are, where a command's flow is a LaneFlow (below).
static __attribute__((unused)) void lane_serve_begin(void);
static __attribute__((unused)) void lane_serve_end(void);
static __attribute__((unused)) void lane_serve(void);
// Whether the calling thread is lane 0 serving its group's other lanes.
static __attribute__((unused)) bool lane_serving(void);

// A command's flow of rounds (#905), which every lane of a session's group runs: lane 0 runs the command alone from
// lane_flow_begin, and each of its wide parts is a round, which lane 0 announces with its kind (the command's own,
// nonzero) and its lanes. Lane 0 runs its part of a round itself, between lane_flow_round_begin and
// lane_flow_round_end; the flow's other lanes take each round from lane_flow_next and run its operation by its kind
// when they are among its lanes, then lane_widen; lane_flow_end, which they get as kind 0, ends the flow. Lane 0 calling
// its own operation directly, and the others by kind, keeps a command's calls from leading back to its round through a
// shared dispatch, which the call graph check refuses. A flow without other lanes (a session of one, one whose lanes
// didn't start, or none) runs its rounds on lane 0 alone. Only a round's lanes read its operation, before they arrive at
// its end, which lane 0 waits for before it announces another.
typedef struct LaneFlow LaneFlow;
struct LaneFlow
{
    // The flow's group, which lane 0 leaves to run alone, and lane 0's lane alone during a round.
    Lane group;
    Lane alone;
    // The operation of the round lane 0 announced last.
    void* argument;
};
static __attribute__((unused)) void lane_flow_begin(LaneFlow* flow);
// The lanes the flow's rounds may take: its group's, or one; a lazy session's lane 0 learns here how its lanes started.
static __attribute__((unused)) uint32_t lane_flow_lanes(const LaneFlow* flow);
// Lane 0: a round of `kind` on `lanes` of the flow's lanes (at most its own), `argument` its operation's, which lane 0,
// always among its lanes, runs before lane_flow_round_end.
static __attribute__((unused)) void lane_flow_round_begin(LaneFlow* flow, uint32_t kind, void* argument, uint32_t lanes);
static __attribute__((unused)) void lane_flow_round_end(LaneFlow* flow);
// The flow's other lanes: the next round's kind, 0 at the flow's end, and with one, its operation in `*argument` for a
// lane among its lanes or null for the others; either then calls lane_widen.
static __attribute__((unused)) uint32_t lane_flow_next(LaneFlow* flow, void** argument);
static __attribute__((unused)) void lane_flow_end(LaneFlow* flow);

typedef struct LaneTeamWorker LaneTeamWorker;
struct LaneTeamWorker
{
    // Only this worker is woken for a dispatch which includes it. Unused workers remain asleep.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t request;
    LaneTeam* team;
    pthread_t thread;
    uint32_t index;
    // The last request issued by the owner, without lane_wait's sleeper bit.
    uint32_t issued;
    bool pinned;
    // A separate line: the worker publishes completion after it has stopped using the operation's shared data.
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t completed;
};

struct LaneTeam
{
    // The operation's barrier, broadcast and part counter, distinct from dispatch and completion.
    LaneGroup group;
    LaneTeamWorker workers[BUSTER_LANE_MAX_COUNT];
    uint32_t cpus[BUSTER_LANE_MAX_COUNT];
    uint32_t capacity;
    uint32_t count;
    pthread_t owner;
    pid_t process;
    bool has_cpus;
    bool stopping;
    // Whether the workers keep their scratch arenas, emptied, from one operation to the next (lane_team_keep_scratch).
    bool keep_scratch;
};

// A zero-initialized handle is closed. Start creates capacity - 1 workers once, validates every requested pin, and
// selects the team on its calling thread only after all workers are ready. Failure joins any partial group and leaves
// the handle closed. It neither changes the caller's lane nor leaves it pinned. Another team, or an operation in
// progress even under lane_alone, refuses a start. Keep the handle alive until stop and use it only on its owner.
static __attribute__((unused)) bool lane_team_start(LaneTeam* team, uint32_t capacity, const uint32_t* cpus);
// Joins the fixed workers and closes the handle. A closed, non-current or running team's stop is a no-op. In a forked
// child, forgets the inherited handle without joining vanished workers. Stopping twice is safe; a closed handle may
// be started again. The caller's own scratch arenas keep their existing lifetime.
static __attribute__((unused)) void lane_team_stop(LaneTeam* team);
// Has the team's workers keep their scratch arenas from one operation to the next, emptied, rather than release them
// after each: a command whose operations follow one another (link-foreign's, #501) then finds the pages its last one
// touched, rather than reserving, faulting in and unmapping them again. The workers release them when the team stops.
// Call it on the owner, between operations; a closed, non-current or running team's is a no-op.
static __attribute__((unused)) void lane_team_keep_scratch(LaneTeam* team);

typedef struct LaneStart LaneStart;
struct LaneStart
{
    LaneGroup* group;
    uint32_t index;
    uint32_t count;
    // The CPU to pin the lane to, or -1.
    int32_t cpu;
    // Whether the lane's thread could pin itself, written by that thread.
    bool pinned;
};

// Pins the calling thread to `cpu`. Returns false if the kernel refuses (the CPU is offline or not allowed).
static bool lane_pin(uint32_t cpu)
{
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    bool result = false;
    if (cpu < BUSTER_LANE_CPU_MASK_WORDS * 64)
    {
        mask[cpu / 64] = 1ull << (cpu % 64);
        result = syscall(SYS_sched_setaffinity, 0, sizeof(mask), mask) == 0;
    }
    return result;
}

// Between whole operations there is no useful work to spin for. Park directly, with the same sleeper-bit protocol as
// lane_wait, so a publish either sees and wakes this waiter or prevents it from sleeping on an obsolete value.
static uint32_t lane_team_wait(uint32_t* word, uint32_t value)
{
    uint32_t current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    while ((current & ~1u) == value)
    {
        if ((current & 1u) == 0)
        {
            if (__atomic_compare_exchange_n(word, &current, current | 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
            {
                current |= 1u;
            }
        }
        else
        {
            syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, current, 0, 0, 0);
            thread_context.sleeps += 1;
            current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
        }
    }
    return current & ~1u;
}

static void* lane_team_thread_main(void* parameter)
{
    LaneTeamWorker* worker = parameter;
    LaneTeam* team = worker->team;
    thread_context.team = team;
    worker->pinned = !team->has_cpus || lane_pin(team->cpus[worker->index]);
    // Request 2 is the initial parked state; publishing completion 2 reports that this worker has attempted its pin.
    uint32_t request = BUSTER_LANE_TEAM_READY;
    lane_publish(&worker->completed, request);
    bool stopping = false;
    while (!stopping)
    {
        request = lane_team_wait(&worker->request, request);
        stopping = team->stopping;
        if (!stopping)
        {
            thread_context.lane = (Lane){
                .index = worker->index,
                .count = team->count,
                .group = &team->group,
            };
            thread_context.taken = 0;
            thread_context.running = true;
            team->group.entry(team->group.argument);
            if (team->keep_scratch)
            {
                scratch_empty();
            }
            else
            {
                scratch_release();
            }
            thread_context.running = false;
            thread_context.lane = (Lane){ .count = 1 };
            // Everything the operation wrote, including scratch cleanup, precedes the owner's completion wait.
            lane_publish(&worker->completed, request);
        }
    }
    scratch_release();
    thread_context.team = 0;
    return 0;
}

// The owner alone calls this, after all dispatched callbacks have returned. Startup uses it for a partial group too.
static void lane_team_join(LaneTeam* team, uint32_t created)
{
    team->stopping = true;
    for (uint32_t i = 1; i < created; i += 1)
    {
        LaneTeamWorker* worker = &team->workers[i];
        worker->issued += 2;
        lane_publish(&worker->request, worker->issued);
    }
    for (uint32_t i = 1; i < created; i += 1)
    {
        pthread_join(team->workers[i].thread, 0);
    }
    *team = (LaneTeam){ 0 };
}

static bool lane_team_start(LaneTeam* team, uint32_t capacity, const uint32_t* cpus)
{
    bool result = team != 0 && capacity >= 1 && capacity <= BUSTER_LANE_MAX_COUNT && thread_context.team == 0 && !thread_context.running &&
                  lane_session_current() == 0;
#if BUSTER_SANITIZER_RESOURCES
    result = result && (capacity == 1 || !lane_test_resources.no_threads);
#endif
    result = result && team->capacity == 0;
    if (result)
    {
        *team = (LaneTeam){
            .capacity = capacity,
            .owner = pthread_self(),
            .process = getpid(),
            .has_cpus = cpus != 0,
        };
        if (cpus != 0)
        {
            memcpy(team->cpus, cpus, capacity * sizeof(*cpus));
            uint64_t affinity[BUSTER_LANE_CPU_MASK_WORDS];
            const bool saved = syscall(SYS_sched_getaffinity, 0, sizeof(affinity), affinity) > 0;
            result = saved && lane_pin(cpus[0]);
            if (saved)
            {
                result = syscall(SYS_sched_setaffinity, 0, sizeof(affinity), affinity) == 0 && result;
            }
        }
        uint32_t created = 1;
        for (uint32_t i = 1; result && i < capacity; i += 1)
        {
            LaneTeamWorker* worker = &team->workers[i];
            worker->team = team;
            worker->index = i;
            worker->request = BUSTER_LANE_TEAM_READY;
            worker->issued = BUSTER_LANE_TEAM_READY;
            result = pthread_create(&worker->thread, 0, lane_team_thread_main, worker) == 0;
            created += result ? 1 : 0;
        }
        __atomic_add_fetch(&lane_process.threads_created, created - 1, __ATOMIC_RELAXED);
        for (uint32_t i = 1; i < created; i += 1)
        {
            lane_wait(&team->workers[i].completed, 0);
            result = team->workers[i].pinned && result;
        }
        if (result)
        {
            thread_context.team = team;
        }
        else
        {
            lane_team_join(team, created);
        }
    }
    return result;
}

static void lane_team_stop(LaneTeam* team)
{
    if (team != 0 && thread_context.team == team && !thread_context.running)
    {
        if (team->process != getpid())
        {
            // fork copied the memory but not the other threads. There is no live group to signal or join here.
            thread_context.team = 0;
            *team = (LaneTeam){ 0 };
        }
        else if (pthread_equal(team->owner, pthread_self()))
        {
            thread_context.team = 0;
            lane_team_join(team, team->capacity);
        }
    }
}

static void lane_team_keep_scratch(LaneTeam* team)
{
    if (team != 0 && thread_context.team == team && !thread_context.running)
    {
        team->keep_scratch = true;
    }
}

static bool lane_team_run(LaneTeam* team, uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    bool result = !thread_context.running && team->process == getpid() && pthread_equal(team->owner, pthread_self()) &&
                  count >= 1 && count <= team->capacity && entry != 0 && team->has_cpus == (cpus != 0);
    for (uint32_t i = 0; result && cpus != 0 && i < count; i += 1)
    {
        result = cpus[i] == team->cpus[i];
    }
    uint64_t affinity[BUSTER_LANE_CPU_MASK_WORDS];
    const bool saved = result && cpus != 0 && syscall(SYS_sched_getaffinity, 0, sizeof(affinity), affinity) > 0;
    result = result && (cpus == 0 || (saved && lane_pin(cpus[0])));
    if (result)
    {
        team->group = (LaneGroup){
            .entry = entry,
            .argument = argument,
        };
        team->count = count;
        const Lane previous = thread_context.lane;
        const uint64_t taken = thread_context.taken;
        thread_context.lane = (Lane){
            .count = count,
            .group = &team->group,
        };
        thread_context.taken = 0;
        thread_context.running = true;
        for (uint32_t i = 1; i < count; i += 1)
        {
            LaneTeamWorker* worker = &team->workers[i];
            worker->issued += 2;
            lane_publish(&worker->request, worker->issued);
        }
        entry(argument);
        for (uint32_t i = 1; i < count; i += 1)
        {
            LaneTeamWorker* worker = &team->workers[i];
            lane_wait(&worker->completed, worker->issued - 2);
        }
        thread_context.running = false;
        thread_context.taken = taken;
        thread_context.lane = previous;
    }
    if (saved)
    {
        result = syscall(SYS_sched_setaffinity, 0, sizeof(affinity), affinity) == 0 && result;
    }
    return result;
}

// The entry point of lane_run's threads.
static void* lane_thread_main(void* parameter)
{
    LaneStart* start = parameter;
    start->pinned = start->cpu < 0 || lane_pin((uint32_t)start->cpu);
    if (lane_wait(&start->group->gate, 0) == BUSTER_LANE_GATE_RUN)
    {
        thread_context.lane = (Lane){
            .index = start->index,
            .count = start->count,
            .group = start->group,
        };
        thread_context.running = true;
        start->group->entry(start->group->argument);
    }
    scratch_release();
    return 0;
}

static bool lane_run_cold(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    LaneGroup group = {
        .entry = entry,
        .argument = argument,
    };
    LaneStart starts[BUSTER_LANE_MAX_COUNT];
    pthread_t threads[BUSTER_LANE_MAX_COUNT];
    // A session's lanes are the only threads under a session: a cold group of several is refused there.
    bool valid = count >= 1 && count <= BUSTER_LANE_MAX_COUNT && entry != 0 && (count == 1 || lane_session_current() == 0);
#if BUSTER_SANITIZER_RESOURCES
    valid = valid && (count == 1 || !lane_test_resources.no_threads);
#endif
    uint32_t created = 1;
    for (uint32_t i = 1; valid && i < count && created == i; i += 1)
    {
        starts[i] = (LaneStart){
            .group = &group,
            .index = i,
            .count = count,
            .cpu = cpus != 0 ? (int32_t)cpus[i] : -1,
            .pinned = false,
        };
        created += pthread_create(&threads[i], 0, lane_thread_main, &starts[i]) == 0 ? 1 : 0;
    }
    __atomic_add_fetch(&lane_process.threads_created, created - 1, __ATOMIC_RELAXED);
    const bool started = valid && created == count;
    lane_publish(&group.gate, started ? BUSTER_LANE_GATE_RUN : BUSTER_LANE_GATE_CANCEL);
    bool pinned = true;
    if (started)
    {
        uint64_t affinity[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
        const bool saved = cpus != 0 && syscall(SYS_sched_getaffinity, 0, sizeof(affinity), affinity) > 0;
        pinned = cpus == 0 || (saved && lane_pin(cpus[0]));
        const Lane previous = thread_context.lane;
        const uint64_t taken = thread_context.taken;
        const bool running = thread_context.running;
        thread_context.lane = (Lane){
            .index = 0,
            .count = count,
            .group = &group,
        };
        thread_context.taken = 0;
        thread_context.running = true;
        entry(argument);
        thread_context.running = running;
        thread_context.taken = taken;
        thread_context.lane = previous;
        if (saved)
        {
            pinned = syscall(SYS_sched_setaffinity, 0, sizeof(affinity), affinity) == 0 && pinned;
        }
    }
    for (uint32_t i = 1; i < created; i += 1)
    {
        pthread_join(threads[i], 0);
        pinned = pinned && starts[i].pinned;
    }
    return started && pinned;
}

// lane_run under a session: a region of lane 0's announced to the lanes that serve it, or an operation of a lane alone.
// The operation may serve lanes of its own (a command of several units, run as a region of a test's served lanes), which
// lane_serve_begin records over the caller's: the caller's serving is restored after it.
static bool lane_session_region(LaneSession* session, uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    const Lane served = thread_context.served;
    const bool serving = thread_context.serving;
    const uint32_t room = serving ? served.count : thread_context.lane.count;
    const uint32_t lanes = count < room ? count : room;
    bool result = count >= 1 && entry != 0 && (cpus == 0 || session->has_cpus);
    for (uint32_t i = 0; result && cpus != 0 && i < lanes; i += 1)
    {
        result = cpus[i] == session->cpus[i];
    }
    const uint64_t taken = thread_context.taken;
    if (result && serving && served.count > 1)
    {
        // The region's operation goes on the served group's line of lane_run's (each group served at its own depth has
        // its own), which only the region's lanes read, before they arrive at its end (lane_widen), which lane 0 waits
        // for before it writes the line again. The lanes the region leaves out read nothing but the announcement: lane
        // 0 doesn't wait for them, and they may still be on their way to the next announcement when it writes.
        const Lane alone = thread_context.lane;
        thread_context.lane = served;
        thread_context.serving = false;
        thread_context.lane.group->entry = entry;
        thread_context.lane.group->argument = argument;
        lane_park_broadcast(lanes, 0);
        if (lane_narrow(lanes))
        {
            entry(argument);
        }
        lane_widen();
        thread_context.lane = alone;
    }
    else if (result && thread_context.lane.count == 1)
    {
        // A lane alone, or lane 0 serving a group of one, which is alone too, and serves nothing in the operation.
        const Lane previous = thread_context.lane;
        thread_context.lane = (Lane){ .count = 1 };
        thread_context.serving = false;
        thread_context.taken = 0;
        entry(argument);
        thread_context.lane = previous;
    }
    else
    {
        result = false;
    }
    thread_context.served = served;
    thread_context.serving = serving;
    thread_context.taken = taken;
    return result;
}

static bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    LaneSession* session = lane_session_current();
    return session != 0                ? lane_session_region(session, count, entry, argument, cpus) :
           thread_context.team != 0 ? lane_team_run(thread_context.team, count, entry, argument, cpus) :
                                        lane_run_cold(count, entry, argument, cpus);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sessions (#905)

// The stack each of a session's lanes has, whatever RLIMIT_STACK says: glibc's default where that is 8 MB, of which a
// lane touches what its deepest phase takes.
#define BUSTER_LANE_STACK_BYTES (8ull << 20)

static LaneSession* lane_session_current(void)
{
    LaneSession* session = thread_context.session;
    if (session != 0 && session->process != getpid())
    {
        // fork copied this lane but not the others, as with a team (lane_team_stop): there is no group to meet here.
        thread_context.session = 0;
        thread_context.lane = (Lane){ .count = 1 };
        thread_context.serving = false;
        thread_context.narrow_depth = 0;
        thread_context.running = false;
        thread_context.taken = 0;
        session = 0;
    }
    return session;
}

static void* lane_session_thread_main(void* parameter);

// Creates the session's lanes from `first` to `end` (their start records' and threads' slots), with the calling
// thread's signal mask, which has every asynchronous signal blocked, and the lanes' stack size: lane 0's creations, or a
// lazy session's lane 1's. Returns how many it created, stopping at the first that fails.
static uint32_t lane_session_create(LaneSession* session, uint32_t first, uint32_t end)
{
    pthread_attr_t attributes;
    const bool attributed = pthread_attr_init(&attributes) == 0;
    bool valid = attributed && pthread_attr_setstacksize(&attributes, BUSTER_LANE_STACK_BYTES) == 0;
#if BUSTER_SANITIZER_RESOURCES
    valid = valid && !lane_test_resources.no_threads;
#endif
    uint32_t created = 0;
    for (uint32_t i = first; valid && i < end; i += 1)
    {
        session->starts[i] = (LaneSessionStart){
            .session = session,
            .index = i,
        };
        valid = i != lane_process.fail_at && pthread_create(&session->threads[i], &attributes, lane_session_thread_main, &session->starts[i]) == 0;
        created += valid ? 1 : 0;
    }
    if (attributed)
    {
        pthread_attr_destroy(&attributes);
    }
    __atomic_add_fetch(&lane_process.threads_created, created, __ATOMIC_RELAXED);
    return created;
}

// A created lane: it pins itself if asked, waits at the gate until lane 0 (or, in a lazy session, lane 1) knows every
// lane exists, and runs the flow. A lazy session's lane 1 creates the lanes after it first, and opens the gate.
static void* lane_session_thread_main(void* parameter)
{
    LaneSessionStart* start = parameter;
    LaneSession* session = start->session;
    if (session->has_cpus)
    {
        start->pinned = lane_pin(session->cpus[start->index]);
        lane_publish(&start->ready, BUSTER_LANE_GATE_RUN);
    }
    if (session->lazy && start->index == 1)
    {
        const uint32_t created = 2 + lane_session_create(session, 2, session->capacity);
        const bool started = created == session->capacity;
        session->count = started ? session->capacity : 1;
        session->created = created;
        lane_publish(&session->gate, started ? BUSTER_LANE_GATE_RUN : BUSTER_LANE_GATE_CANCEL);
    }
    // Asleep at once: the lanes after this one are created before the gate opens, and a lane that spun meanwhile would
    // take the CPU time they are created with.
    if (lane_sleep(&session->gate, 0) == BUSTER_LANE_GATE_RUN)
    {
        // Read before the flow: once it ends, a lane that stays parked reads nothing of the session's.
        const bool park = session->park_at_end;
        thread_context.session = session;
        thread_context.lane = (Lane){
            .index = start->index,
            .count = session->count,
            .group = &session->group,
        };
        thread_context.taken = 0;
        thread_context.running = true;
        session->entry(session->argument);
        // Lane 0 doesn't join a lane that stays parked; the process's exit ends it.
        uint32_t never = 0;
        while (park)
        {
            lane_sleep(&never, 0);
        }
        thread_context.running = false;
        thread_context.lane = (Lane){ .count = 1 };
        thread_context.session = 0;
    }
    scratch_release();
    return 0;
}

static uint32_t lane_session_run(LaneSession* session, uint32_t capacity, const uint32_t* cpus, void (*entry)(void* argument), void* argument,
                                 uint32_t flags)
{
    const uint32_t lanes = capacity == 0 ? 1 : capacity < BUSTER_LANE_MAX_COUNT ? capacity : BUSTER_LANE_MAX_COUNT;
    uint32_t result = 0;
    if (lane_session_current() == 0 && thread_context.team == 0 && !thread_context.running && entry != 0)
    {
        memset(session, 0, sizeof(*session));
        session->capacity = lanes;
        session->process = getpid();
        session->owner = pthread_self();
        session->entry = entry;
        session->argument = argument;
        session->park_at_end = (flags & BUSTER_LANE_SESSION_PARK_AT_END) != 0 && !BUSTER_SANITIZER_RESOURCES;
        session->lazy = (flags & BUSTER_LANE_SESSION_LAZY) != 0 && cpus == 0 && lanes > 2;
        session->has_cpus = cpus != 0;
        if (cpus != 0)
        {
            memcpy(session->cpus, cpus, lanes * sizeof(*cpus));
        }
        uint64_t affinity[BUSTER_LANE_CPU_MASK_WORDS];
        const bool saved = cpus != 0 && syscall(SYS_sched_getaffinity, 0, sizeof(affinity), affinity) > 0;
        bool valid = cpus == 0 || (saved && lane_pin(cpus[0]));
        uint32_t created = 1;
        if (lanes > 1)
        {
            // The lanes start with every asynchronous signal blocked, which they keep; the synchronous ones stay as they
            // are, for the handlers of tests that fault on purpose. Lazily, lane 0 creates lane 1 alone, which creates
            // the others while lane 0 goes on, and decides the gate.
            sigset_t blocked;
            sigset_t previous;
            sigfillset(&blocked);
            const int synchronous[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP, SIGSYS };
            for (uint32_t i = 0; i < sizeof(synchronous) / sizeof(synchronous[0]); i += 1)
            {
                sigdelset(&blocked, synchronous[i]);
            }
            const bool masked = pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0;
            // Lazily, the session's count is the lanes asked for until lane 1 opens the gate.
            session->count = lanes;
            created += masked && valid ? lane_session_create(session, 1, session->lazy ? 2 : lanes) : 0;
            if (masked)
            {
                pthread_sigmask(SIG_SETMASK, &previous, 0);
            }
            for (uint32_t i = 1; cpus != 0 && i < created; i += 1)
            {
                lane_wait(&session->starts[i].ready, 0);
                valid = valid && session->starts[i].pinned;
            }
        }
        bool started = valid && created == lanes;
        if (session->lazy && created == 2)
        {
            // Lane 1 opens the gate; lane 0 runs on the lanes asked for until it settles (lane_session_settle).
            started = true;
        }
        else
        {
            // The created lanes read `lazy` as they start: it changes only for a lazy session whose lane 1 didn't start,
            // which has no other lane.
            if (session->lazy)
            {
                session->lazy = false;
            }
            session->count = started ? lanes : 1;
            session->created = created;
            lane_publish(&session->gate, started ? BUSTER_LANE_GATE_RUN : BUSTER_LANE_GATE_CANCEL);
            for (uint32_t i = 1; !started && i < created; i += 1)
            {
                pthread_join(session->threads[i], 0);
            }
        }
        session->settled = !session->lazy;
        const uint32_t count = started ? lanes : 1;
        const Lane previous = thread_context.lane;
        const uint64_t taken = thread_context.taken;
        thread_context.session = session;
        thread_context.lane = (Lane){
            .count = count,
            .group = count > 1 ? &session->group : 0,
        };
        thread_context.taken = 0;
        thread_context.running = true;
        entry(argument);
        // A lazy flow that never met its lanes: they are lane 1's to start, and the session's to end.
        lane_session_settle();
        thread_context.running = false;
        thread_context.taken = taken;
        thread_context.lane = previous;
        thread_context.session = 0;
        for (uint32_t i = 1; session->count > 1 && !session->park_at_end && i < session->created; i += 1)
        {
            pthread_join(session->threads[i], 0);
        }
        if (saved)
        {
            syscall(SYS_sched_setaffinity, 0, sizeof(affinity), affinity);
        }
        result = session->count;
    }
    return result;
}

static uint32_t lane_session_settle(void)
{
    LaneSession* session = lane_session_current();
    uint32_t result = 1;
    // `settled` is lane 0's alone, which it writes while the other lanes run: they never read it.
    if (session != 0 && pthread_equal(session->owner, pthread_self()) && !session->settled)
    {
        // Lane 1 publishes the count and the lanes it created before it opens the gate.
        const bool run = lane_sleep(&session->gate, 0) == BUSTER_LANE_GATE_RUN;
        session->settled = true;
        for (uint32_t i = 1; !run && i < session->created; i += 1)
        {
            pthread_join(session->threads[i], 0);
        }
        if (!run && thread_context.lane.group == &session->group)
        {
            thread_context.lane = (Lane){ .count = 1 };
        }
    }
    result = session != 0 ? session->count : 1;
    return result;
}

static bool lane_serving(void)
{
    return thread_context.serving;
}

static void lane_serve_begin(void)
{
    // Serving doesn't nest on one lane: a region's operation starts serving nothing (lane_session_region), and may serve
    // its own region's lanes.
    assert(!thread_context.serving);
    // The lanes it serves must exist: a lazy session's lane 0 learns first how they started, and with none serves a group
    // of one.
    lane_session_settle();
    thread_context.served = thread_context.lane;
    thread_context.serving = true;
    thread_context.lane = (Lane){ .count = 1 };
}

static void lane_serve_end(void)
{
    thread_context.lane = thread_context.served;
    thread_context.serving = false;
    lane_park_broadcast(0, 0);
}

// Each announcement is a region's lanes, or 0 for the end; the region's operation is on the group's lane_run line, which
// only the region's lanes read (lane_session_region).
static void lane_serve(void)
{
    LaneGroup* group = thread_context.lane.group;
    for (uint64_t lanes = lane_park_broadcast(0, 0); lanes != 0; lanes = lane_park_broadcast(0, 0))
    {
        if (lane_narrow((uint32_t)lanes))
        {
            group->entry(group->argument);
        }
        lane_widen();
    }
}

static void lane_flow_begin(LaneFlow* flow)
{
    flow->group = lane_alone();
    flow->alone = flow->group;
    flow->argument = 0;
}

// Whether the flow has lanes besides lane 0 to run its rounds on.
static bool lane_flow_shared(const LaneFlow* flow)
{
    return lane_session_settle() > 1 && flow->group.count > 1;
}

static uint32_t lane_flow_lanes(const LaneFlow* flow)
{
    return lane_flow_shared(flow) ? flow->group.count : 1;
}

static void lane_flow_round_begin(LaneFlow* flow, uint32_t kind, void* argument, uint32_t lanes)
{
    assert(kind != 0);
    flow->argument = argument;
    const bool shared = lane_flow_shared(flow);
    flow->alone = lane_alone();
    if (shared)
    {
        lane_restore(flow->group);
        lane_park_broadcast((uint64_t)kind << 32 | lanes, 0);
    }
    lane_narrow(shared ? lanes : 1);
}

static void lane_flow_round_end(LaneFlow* flow)
{
    lane_widen();
    lane_restore(flow->alone);
}

static uint32_t lane_flow_next(LaneFlow* flow, void** argument)
{
    const uint64_t announced = lane_park_broadcast(0, 0);
    const uint32_t kind = (uint32_t)(announced >> 32);
    *argument = 0;
    if (kind != 0 && lane_narrow((uint32_t)announced))
    {
        *argument = flow->argument;
    }
    return kind;
}

static void lane_flow_end(LaneFlow* flow)
{
    if (lane_flow_shared(flow))
    {
        lane_restore(flow->group);
        lane_park_broadcast(0, 0);
    }
}
