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
// sleeps on a futex, where the RAD Debugger's on Linux is pthread_barrier_wait, which sleeps at every wait.
//
// Each translation unit that includes this file owns its thread context and any command-long team, but for main.c's
// units, which share one (thread_context): a command runs in its own unit's code and in the homes of the modules it
// calls (main.c). A team must therefore be started in the command's code, not in main.c's dispatcher, which also runs
// the benchmarks' units, whose contexts are their own. Each part declares its interface first, then defines it. In this
// order:
//   - Arenas: Arena, arena_reserve, arena_release, arena_push, arena_push_array (BUSTER_ARENA_PUSH_ARRAY) and
//     arena_pop_to. An arena reserves its address space up front, and the kernel commits each page when it is first
//     touched. A push that doesn't fit returns 0 and leaves the arena as it was: nothing is ever truncated.
//   - The thread context: thread_context, which holds the thread's lane and its scratch arenas, and scratch_begin and
//     scratch_end.
//   - Lanes: Lane, LaneGroup, lane_index, lane_count, lane_sync, lane_broadcast, lane_from_task_index, lane_range,
//     lane_range_by_cost, range_part, range_part_by_cost, lane_part_count, lane_parts_by_cost, parts_by_cost, lane_take,
//     lane_alone and lane_restore.
//   - Starting lanes: lane_team_start and lane_team_stop own a command's fixed threads; lane_run dispatches a whole
//     operation to them, and lane_team_keep_scratch has them keep their scratch arenas from one to the next. Without a
//     team, lane_run starts and joins a fresh group, as the cold-start benchmark needs.
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

// OS-dependent includes
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>

// CPU architecture dependent includes: x86intrin.h has AMD's monitorx and mwaitx besides immintrin.h (lane_wait uses
// them where the build's target has mwaitx, __MWAITX__).
#include <x86intrin.h>

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
};

// Reserves `capacity` bytes, which the kernel commits a page at a time as they are first touched: the first
// BUSTER_ARENA_SMALL_BYTES in base pages, and the rest in huge pages. Returns an arena without capacity, in which every
// push fails, if the address space can't be reserved.
static __attribute__((unused)) Arena arena_reserve(uint64_t capacity);
// arena_reserve with the first `small` bytes in base pages (a multiple of BUSTER_ARENA_PAGE_BYTES, at least
// BUSTER_ARENA_SMALL_BYTES), for a table that usually stays below what a huge page is worth: the kernel clears a huge
// page whole when it is first touched, which costs as much as about 750 KB of base pages on the 9700X.
static __attribute__((unused)) Arena arena_reserve_small(uint64_t capacity, uint64_t small);
// An arena that holds no memory, for measuring what pushes take: its pushes take room as from an arena reserved at a
// page boundary, up to BUSTER_ARENA_MAX_CAPACITY, and its length after them is that room. Its base is past the
// addresses the kernel maps a process's memory at unasked (BUSTER_ARENA_MAX_CAPACITY), so that reading or writing what a
// push returns faults: only code that pushes and writes nothing in what it pushes may run on it. Its addresses are in no
// object, so compare or subtract them only as integers.
static __attribute__((unused)) Arena arena_measure(void);
// Unmaps the arena, and leaves it without capacity.
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
    if (capacity != 0 && capacity <= BUSTER_ARENA_MAX_CAPACITY)
    {
        const uint64_t mapping_bytes = arena_mapping_bytes(capacity);
        uint8_t* mapping = mmap(0, mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
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
        munmap(arena->base - arena->mapping_offset, arena_mapping_bytes(arena->capacity));
    }
    *arena = (Arena){ 0 };
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

// ---------------------------------------------------------------------------------------------------------------------
// The thread context

// Each thread's scratch arenas: two, so that a function that pushes its results into one of them can take scratch memory
// from the other. Each is reserved when it is first used.
#define BUSTER_SCRATCH_ARENA_COUNT 2
#define BUSTER_SCRATCH_ARENA_CAPACITY (64ull << 30)

typedef struct LaneGroup LaneGroup;
typedef struct LaneTeam LaneTeam;

// A thread's place in its group: its index and the group's size. A thread outside any group, or one that has made
// itself a group of one with lane_alone, is lane 0 of 1, without a group.
typedef struct Lane Lane;
struct Lane
{
    uint32_t index;
    uint32_t count;
    LaneGroup* group;
};

typedef struct ThreadContext ThreadContext;
struct ThreadContext
{
    Lane lane;
    // A command's fixed team, or 0 for standalone calls. running also guards calls made after lane_alone.
    LaneTeam* team;
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

// Waits while *word, but for its bit 0, is `value`, and returns its new value without bit 0. Spins first, then waits in
// mwaitx; then sets bit 0, so that whoever changes the word wakes it, and sleeps on a futex while the word is unchanged.
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
static __attribute__((unused)) bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus);

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
    bool result = team != 0 && capacity >= 1 && capacity <= BUSTER_LANE_MAX_COUNT && thread_context.team == 0 && !thread_context.running;
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
    const bool valid = count >= 1 && count <= BUSTER_LANE_MAX_COUNT && entry != 0;
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

static bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    return thread_context.team != 0 ? lane_team_run(thread_context.team, count, entry, argument, cpus) : lane_run_cold(count, entry, argument, cpus);
}
