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
// as the command may take (one creates no thread), or for the C driver's one source file once it is preprocessed, if it
// is large enough for a second lane to pay (#1444, BUSTER_LANE_SESSION_ON_DEMAND). Every lane runs the command's flow
// (lane_session_run's entry), lane 0 its serial parts while the others park (lane_park), and each wide part on as many
// lanes as it pays for and the worker budget gives (lane_narrow), so that nothing in the command creates or joins a
// thread after its start. Each part declares its interface first, then defines it. In this order:
//   - Arenas: Arena, arena_reserve, arena_release, arena_push, arena_push_array (BUSTER_ARENA_PUSH_ARRAY) and
//     arena_pop_to. An arena reserves inaccessible address space up front, committing its prefix as pushes need it. A
//     push that doesn't fit returns 0 and leaves the arena as it was: nothing is ever truncated. arena_poison
//     overwrites what pushes took, for tests of code that reuses an arena, and a thread's ArenaPool (arena_pool) keeps
//     the arenas it releases for its next reservations, which take them zero-filled, their committed prefixes still
//     writable unless commit charge is bounded, and their mappings perhaps larger than the capacity asked for.
//     arena_reserve_tables lays out tables of fixed sizes that are reserved and released together in one arena, each
//     in the pages an arena of its own would have.
//   - The thread context: thread_context, which holds the thread's lane and its scratch arenas, and scratch_begin and
//     scratch_end.
//   - Lanes: Lane, LaneGroup, LaneSession, lane_index, lane_count, lane_sync, lane_broadcast, lane_from_task_index,
//     lane_range, lane_range_by_cost, range_part, range_part_by_cost, lane_part_count, lane_parts_by_cost,
//     parts_by_cost, lane_take, lane_alone, lane_restore, lane_park, lane_park_broadcast, lane_narrow and lane_widen.
//   - Starting lanes: lane_team_start and lane_team_stop own a command's fixed threads; lane_run dispatches a whole
//     operation to them, and lane_team_keep_scratch has them keep their scratch arenas from one to the next. Without a
//     team or a session, lane_run starts and joins a fresh group, as the cold-start benchmark needs.
//   - Sessions: lane_session_run, lane_session_start, lane_session_current, lane_session_settle, and a command's flow of
//     rounds: LaneFlow, lane_flow_begin, lane_flow_lanes, lane_flow_round_begin, lane_flow_round_end, lane_flow_next and
//     lane_flow_end.
//
// The barrier, dispatch/completion and lane_take use atomics. lane_take adds one to its group's counter, and the last
// take of a loop resets it, since every lane takes once more than it gets parts. In lane_arrive, each lane counts itself
// in, and the last one advances the group's generation word with lane_publish while the others wait for it in
// lane_wait. A waiting lane spins for BUSTER_LANE_SPINS pauses, waits in mwaitx for the word's line to be written for up
// to BUSTER_LANE_WAIT_NANOSECONDS (lane_wait_ticks), and then sleeps on the word with a futex, setting its bit 0 first
// so that the lane that advances it knows to wake the sleepers: a sync at which no lane slept makes no system call.
// A group with more lanes than the CPUs the process may run on shares them (lane_group_shares_cpus), and there a lane
// that would wait in mwaitx sleeps at once instead: a wait in mwaitx, or the spin that takes its place in a build without
// it, keeps the lane's CPU for up to BUSTER_LANE_WAIT_NANOSECONDS, and the lanes that haven't arrived need those CPUs
// before they can arrive, so that every sync would cost a timeslice.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

// OS-dependent includes
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <linux/futex.h>

// CPU architecture dependent includes: x86intrin.h has AMD's monitorx and mwaitx besides immintrin.h (lane_wait uses
// them where the build's target has mwaitx, __MWAITX__); cpuid.h, the hypervisor bit (arena_page_policy).
#include <x86intrin.h>
#include <cpuid.h>

// Resource-exhaustion fixtures must leave sanitizer runtimes free to allocate
// memory for diagnostics, map shadow/fake-stack memory and start their threads.
// Even standalone UBSan can need allocations to report a forced stack overflow.
// Inject compiler-owned reservation/start failures rather than limiting the OS.
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
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
    // Whether its pages past `small` have been advised huge (arena_commit, as the first commit reaches them).
    bool huge;
    // Owned mapping size and writable end, measured from base - mapping_offset. Zero mapping_bytes denotes borrowed
    // storage (including arena_measure); its owner has already made the bytes writable. Neither follows logical
    // capacity when a bounded view/test temporarily narrows it.
    uint64_t mapping_bytes;
    uint64_t committed;
    // Set when a failed commit left pages past `committed` whose protection could not be verified or restored: the
    // arena is never pooled (its writable extent is unknown); capacity is clamped below them, so no push reaches
    // them, and release unmaps the whole reservation.
    bool unaccounted;
    // The requested ceiling can exceed capacity after a constrained-address-space reservation.
    uint64_t requested_capacity;
    // For an owned mapping, the capacity it was laid out for, at least capacity (0 for borrowed storage, including
    // arena_measure): its pages past `small` are huge only if this is past it, and a reservation from the pool takes
    // the arena for any capacity up to it (arena_reserve_policy).
    uint64_t reserved;
    // For an arena of arena_reserve_tables, its tables' layout (arena_tables_layout), never 0; 0 for any other. The pool
    // gives an arena only to a reservation of the same layout.
    uint64_t layout;
};

// Reserves up to `capacity` bytes (less under address-space constraints), zero-filled. A fresh mapping is initially
// inaccessible; an arena from the thread's pool (arena_pool) may keep the prefix its last pushes made writable (unless
// commit charge is bounded: arena_commit_unbounded), and its mapping may be larger than its capacity (Arena.reserved).
// Pushes make their prefix writable; the kernel allocates physical pages on first touch: the first
// BUSTER_ARENA_SMALL_BYTES in base pages, and the rest in huge pages. Returns an arena without capacity, in which every
// push fails, if the address space can't be reserved.
static __attribute__((unused)) Arena arena_reserve(uint64_t capacity);
// arena_reserve with the first `small` bytes in base pages (a multiple of BUSTER_ARENA_PAGE_BYTES, at least
// BUSTER_ARENA_SMALL_BYTES), for a table that usually stays below what a huge page is worth: the kernel clears a huge
// page whole when it is first touched, which costs as much as about 750 KB of base pages on the 9700X. The thread's
// arena_small_floor, if larger, takes the place of `small`.
static __attribute__((unused)) Arena arena_reserve_small(uint64_t capacity, uint64_t small);
// One of the tables arena_reserve_tables lays out: its `bytes`, of which the first `small` (with the minimum and the floor
// of arena_reserve_small's) are base pages and the rest huge pages, as in an arena of its own from arena_reserve_small.
// The reservation sets `base`, or leaves it null if it fails.
typedef struct ArenaTable ArenaTable;
struct ArenaTable
{
    uint64_t bytes;
    uint64_t small;
    void* base;
};
#define BUSTER_ARENA_TABLES 64
// Reserves one arena for `count` tables of fixed sizes, at most BUSTER_ARENA_TABLES, each zero-filled, writable, starting
// on a page and in base and huge pages as an arena of its own would be (#1128). A group of tables that are reserved and
// released together, as a unit's preprocessor's are, so takes one mapping, one commit and one discard rather than one of
// each per table, every one of which takes the address space's lock or sends the other lanes a TLB flush. The arena is
// pushed full and released as one (arena_release); the thread's pool gives it again only to a reservation of the same
// tables. Its commit is charged only where commit charge is bounded (#1521: strict overcommit or a finite RLIMIT_DATA),
// as the kernel ignores the mapping's MAP_NORESERVE there. Returns an arena without capacity, and the bases null, if the
// address space can't be reserved or the commit is refused (which sets arena_commit_refused).
static __attribute__((unused)) Arena arena_reserve_tables(ArenaTable* tables, uint32_t count);

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
// The arenas a thread keeps, emptied, for its next reservations (#152). Reusing a mapping avoids repeating its
// allocation, alignment and page advice, and keeping its committed prefix writable avoids repeating its commits (#1128):
// each of those calls takes the address space's lock for writing, which the other lanes' reservations, commits and
// faults wait for. Release frees the pages with MADV_DONTNEED, which takes the lock only for reading, so that reuse finds
// them zero. Where commit charge is bounded (arena_commit_unbounded), release reprotects the prefix first, returning its
// charge, and reuse starts inaccessible. A reservation takes the arena of the smallest mapping that holds its capacity
// with the same base pages and page layout, the latest released of those; a full pool evicts its oldest arena.
#define BUSTER_ARENA_POOL_SLOTS 256
typedef struct ArenaPool ArenaPool;
struct ArenaPool
{
    uint32_t count;
    // Read at the pool's first reservation or release, and again after arena_pool_release (arena_pool_limits): the
    // RLIMIT_AS share each reservation admits (arena_reservation_limit), and whether its arenas keep their committed
    // prefixes writable.
    bool limits;
    bool keeps_commits;
    uint64_t address_share;
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
// The process's pages (#1109), set once at its start, before any arena is reserved: on a virtual machine (CPUID's
// hypervisor bit), transparent huge pages off for the whole process (PR_SET_THP_DISABLE, which MADV_HUGEPAGE doesn't
// override, and which the processes it starts inherit), so that the arenas take base pages. `setting`,
// BUSTER_HUGE_PAGES's value, decides instead when it is "0" (off) or "1" (on, as the arenas ask, even where the process
// that started this one turned them off). Elsewhere the process keeps what it was started with, but on a busy host,
// whose tasks runnable at the start are at least a quarter of its CPUs online (#672: arena_host_busy), its own arenas
// take base pages (arena_pages_busy). That leaves the process's other memory, and the processes it starts, as they were.
// It also reads the kernel's release, which says whether a reservation can be mapped base pages (arena_stack_base_pages).
static __attribute__((unused)) void arena_page_policy(const char* setting);
// Whether arena_page_policy found the host busy (#672): then every arena the process reserves is advised base pages
// (MADV_NOHUGEPAGE over its mapping) and never huge ones, which --time-stages reports as `busy`. Set once, before the
// process starts any thread; main.c's units share it. BUSTER_TEST_HOST_BUSY ("1" or "0") decides it instead, for tests.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern bool arena_pages_busy;
#elif defined(BUSTER_UNIT)
bool arena_pages_busy;
#else
static bool arena_pages_busy;
#endif
// Whether a mapping made with MAP_STACK is VM_NOHUGEPAGE from the start, as Linux's are since 6.7 (#1444): then a
// reservation that its layout doesn't keep in base pages (arena_reserve_policy's base pages of 2 MB or more, a busy
// host's arenas, an arena of tables of 2 MB or more) is mapped so, rather than advised MADV_NOHUGEPAGE by a call of its
// own, which split its mapping and took the address space's lock for writing. A later MADV_HUGEPAGE replaces it where
// an arena's huge pages start, as it replaced the advice. arena_page_policy sets it from the kernel's release, once,
// before the process starts any thread; elsewhere, or on an older kernel, the reservations advise as before. main.c's
// units share it.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern bool arena_stack_base_pages;
#elif defined(BUSTER_UNIT)
bool arena_stack_base_pages;
#else
static bool arena_stack_base_pages;
#endif
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
// are zero only where no push since the arena's reservation took them (past its peak): a fresh mapping's pages are
// zero, and so are a pooled arena's, whose pages the pool discards.
static __attribute__((unused)) void* arena_push(Arena* arena, uint64_t size, uint64_t alignment);
// `count` elements of `element_bytes` each; 0, as from arena_push, if count * element_bytes overflows.
static __attribute__((unused)) void* arena_push_array(Arena* arena, uint64_t count, uint64_t element_bytes, uint64_t alignment);
#define BUSTER_ARENA_PUSH_ARRAY(arena, Type, count) ((Type*)arena_push_array((arena), (count), sizeof(Type), _Alignof(Type)))
// Frees what was pushed since the arena's length was `length`. Its pages stay committed for the next pushes.
static __attribute__((unused)) void arena_pop_to(Arena* arena, uint64_t length);
// Sets every byte pushes have taken (to the arena's peak) to `value`, and empties the arena: a test's way to make what
// reuses the arena find no byte it didn't write as a fresh arena's zero (#152's --poison-arenas).
static __attribute__((unused)) void arena_poison(Arena* arena, uint8_t value);
// Whether every byte past the arena's length is still as the kernel mapped it, zero: the arena owns its mapping (not
// borrowed storage, whose owner may have written it), and no push through it has reached past its length since its pages
// were mapped, or discarded by the pool. A push through a copy of the arena doesn't move its peak, so only a caller that
// knows no copy pushed past its length may take the answer for its next pushes' bytes (#628: the C driver's arena).
static __attribute__((unused)) bool arena_untouched(const Arena* arena);
// Gives the kernel back the pages that pushes touched past the arena's length, from the first huge page boundary past it,
// which the kernel maps again, zero, wherever a later push touches them: for an arena whose next users touch far fewer
// of its pages than its last ones (#628: the C driver's scratch, which the reader and check_program take to 77 MB of
// sqlite3.c's and the backend after them to 24 MB). Borrowed storage, and an arena whose writable extent is unknown, keep
// their pages. The peak stays where it was: past the length, the pages up to the boundary still hold what was written.
static __attribute__((unused)) void arena_discard(Arena* arena);
// arena_discard in whole huge pages (#777): from the first huge page boundary past the length to the last one at or
// below the peak, for an arena whose next user pushes as far again, a lane of the C driver's units with another unit to
// compile. The huge page a discard frees part of is split by the kernel into base pages, which later pushes fault in
// one at a time: by one lane, synth's units took 130 thousand faults more for them on the 9700X. That huge page is
// kept, at most 2 MB: freeing it whole made each unit take a fresh one, cleared by the kernel, and synth's link by 8
// lanes took 4.3% longer than with arena_discard, where keeping it took 2.2% less.
static __attribute__((unused)) void arena_discard_huge(Arena* arena);
// arena_discard, or with `whole` arena_discard_huge, of the pages past `kept` bytes from the arena's start, or past its
// length if that is further: for an arena whose next users push about `kept` bytes into it again, which they then find
// resident rather than fault in again, cleared (#628: the C driver's scratch after the reader).
static __attribute__((unused)) void arena_discard_past(Arena* arena, uint64_t kept, bool whole);
// Keeps the executable's .bss in base pages (#652), for a compile's commands to call first: it is 220 MB of statics, the
// tests' and benchmarks' arrays among them, of which a compile writes 11 pages whatever its size (an empty file's -c as
// sqlite3.c's: the C driver's state, the parser's tables), in three 2 MB stretches. With transparent huge pages `always`,
// as on the 9700X, the first write in two of them took a huge page each: 4 MB cleared for 44 KB of statics.
static __attribute__((unused)) void arena_static_base_pages(void);
// Makes the pages that [begin, begin + bytes) touches writable and present now, zero, in one system call
// (MADV_POPULATE_WRITE, Linux 5.14), for memory a caller knows it is about to write all over, and that is writable
// already (an arena's pushed bytes, the stack's next pages). On the 9700X a page so costs 172 ns, its first write
// fault 290 ns, and a read of a fresh page before its first write 544 ns, two faults: the zero page's, then its copy's
// (#1444). Where the call fails (an older kernel, a refused allocation), the pages are left to fault as before.
static __attribute__((unused)) void arena_populate(void* begin, uint64_t bytes);
// Reads the process's RLIMIT_AS once, for every later reservation and pool of the process (ArenaLimits, #652): for a
// compile's commands to call first, before they start any thread. A setrlimit after it, or another process's prlimit,
// changes no reservation's share.
static __attribute__((unused)) void arena_limits_fix(void);

// The bytes arena_reserve maps for `capacity`: whole pages, and past BUSTER_ARENA_SMALL_BYTES room to align the huge
// pages, or else an inaccessible page on each side of them.
static uint64_t arena_mapping_bytes(uint64_t capacity)
{
    const uint64_t pages = (capacity + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
    return capacity > BUSTER_ARENA_SMALL_BYTES ? pages + BUSTER_ARENA_HUGE_PAGE_BYTES : pages + 2 * BUSTER_ARENA_PAGE_BYTES;
}

// A failed large mapping switches this thread to bounded reservations (also used by tools with a restricted virtual
// address layout). A finite RLIMIT_AS is divided among arenas before reserving: greedily halving a failed 64 GiB map
// would let the first arena consume almost the entire process limit. This is a per-arena admission ceiling, not a
// promise that an arbitrary number of arenas/threads fits. Consumers must use the returned capacity.
#define BUSTER_ARENA_COMPACT_BYTES (64ull << 20)
#define BUSTER_ARENA_LIMIT_SHARE 128

typedef struct ArenaFailure ArenaFailure;
struct ArenaFailure
{
    uint64_t bytes;
    int error;
    bool commit;
};

// arena_commit_refused: a commit of this thread's was refused (#1521: strict overcommit, a finite RLIMIT_DATA, or a
// single commit larger than heuristic overcommit admits). From then on the tables that are committed whole as they are
// reserved, whose sizes arena_capacity_shift decides (the preprocessor's and its file caches'), take the compact shape,
// so that they charge less. The thread's other reservations keep their capacities: an arena commits only what its pushes
// reach, so capping its reservation at BUSTER_ARENA_COMPACT_BYTES, as arena_compact does for a constrained address
// space, would charge nothing less and only refuse a large unit's pushes as capacity errors.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern _Thread_local bool arena_compact;
extern _Thread_local bool arena_commit_refused;
extern _Thread_local ArenaFailure arena_failure;
#elif defined(BUSTER_UNIT)
_Thread_local bool arena_compact;
_Thread_local bool arena_commit_refused;
_Thread_local ArenaFailure arena_failure;
#else
static _Thread_local bool arena_compact;
static _Thread_local bool arena_commit_refused;
static _Thread_local ArenaFailure arena_failure;
#endif

// The RLIMIT_AS share each reservation admits (arena_address_share), read once for the rest of the process by
// arena_limits_fix (#652). A compile's commands call it first: they don't change their limits, and their reservations
// without a pool (about 30 for an empty file's -fsyntax-only) each made a getrlimit call before. Without it, as in the
// tests that lower RLIMIT_AS in-process or in a forked child, every reservation reads the limit, and each pool reads it
// once (arena_pool_limits). Written before the process starts any thread; main.c's units share it.
typedef struct ArenaLimits ArenaLimits;
struct ArenaLimits
{
    bool fixed;
    uint64_t address_share;
};
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern ArenaLimits arena_limits;
#elif defined(BUSTER_UNIT)
ArenaLimits arena_limits;
#else
static ArenaLimits arena_limits;
#endif

// The share of a finite RLIMIT_AS that one arena admits, or BUSTER_ARENA_MAX_CAPACITY.
static uint64_t arena_address_share(void)
{
    struct rlimit limit;
    uint64_t result = BUSTER_ARENA_MAX_CAPACITY;
    if (getrlimit(RLIMIT_AS, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
    {
        const uint64_t share = (uint64_t)limit.rlim_cur / BUSTER_ARENA_LIMIT_SHARE;
        result = share < result ? share : result;
    }
    return result;
}

// Whether the writable pages a pooled arena keeps take nothing another reservation may need: the kernel charges a
// private mapping's writable pages against a limit only under strict overcommit (vm.overcommit_memory 2) or a finite
// RLIMIT_DATA. False if the mode can't be read.
static bool arena_commit_unbounded(void)
{
    struct rlimit data;
    char mode = 0;
    bool result = getrlimit(RLIMIT_DATA, &data) == 0 && data.rlim_cur == RLIM_INFINITY;
    const int file = result ? open("/proc/sys/vm/overcommit_memory", O_RDONLY | O_CLOEXEC) : -1;
    result = file >= 0 && read(file, &mode, 1) == 1 && mode != '2';
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// What the pool's thread reads once rather than at each of its units' reservations and releases (about 66 a unit of
// Lua's, #1128): only a setrlimit, or a change of the kernel's policy, changes them while the pool's owner runs.
static void arena_pool_limits(ArenaPool* pool)
{
    if (!pool->limits)
    {
        pool->address_share = arena_limits.fixed ? arena_limits.address_share : arena_address_share();
        pool->keeps_commits = arena_commit_unbounded();
        pool->limits = true;
    }
}

static __attribute__((unused)) void arena_limits_fix(void)
{
    arena_limits = (ArenaLimits){
        .fixed = true,
        .address_share = arena_address_share(),
    };
}

static uint64_t arena_reservation_limit(void)
{
    ArenaPool* pool = arena_pool;
    uint64_t share = BUSTER_ARENA_MAX_CAPACITY;
    if (pool != 0)
    {
        arena_pool_limits(pool);
        share = pool->address_share;
    }
    else
    {
        share = arena_limits.fixed ? arena_limits.address_share : arena_address_share();
    }
    uint64_t result = arena_compact ? BUSTER_ARENA_COMPACT_BYTES : BUSTER_ARENA_MAX_CAPACITY;
    result = share < result ? share : result;
    return result & ~(BUSTER_ARENA_PAGE_BYTES - 1);
}

static __attribute__((unused)) void arena_report_failure(FILE* stream, const char* operation, uint64_t requested)
{
    const ArenaFailure failure = arena_failure;
    fprintf(stream, "fatal error: can't %s memory for %s (%llu bytes requested): %s\n",
            failure.commit ? "commit" : "reserve", operation,
            (unsigned long long)(failure.bytes != 0 ? failure.bytes : requested), strerror(failure.error != 0 ? failure.error : ENOMEM));
}

// The pooled arena a reservation of `capacity` bytes, `small` of them base pages, takes, or BUSTER_ARENA_POOL_SLOTS for
// none: of those with the same base pages and page layout, huge pages past `small` or none, the same tables' layout
// (Arena.layout) and room for the capacity, the smallest, released last.
static uint32_t arena_pool_choose(const ArenaPool* pool, uint64_t capacity, uint64_t small, uint64_t layout)
{
    const uint32_t count = pool != 0 ? pool->count : 0;
    const bool huge = capacity > small;
    uint32_t slot = BUSTER_ARENA_POOL_SLOTS;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const Arena* pooled = pool->arenas + i;
        const Arena* best = pool->arenas + (slot != BUSTER_ARENA_POOL_SLOTS ? slot : i);
        const bool match = pooled->layout == layout && pooled->small == small && (pooled->reserved > small) == huge && pooled->reserved >= capacity;
        const bool better = slot == BUSTER_ARENA_POOL_SLOTS || pooled->reserved < best->reserved ||
                            (pooled->reserved == best->reserved && pool->released[i] > pool->released[slot]);
        slot = match && better ? i : slot;
    }
    return slot;
}

// Takes the arena in `slot` out of the pool, with the reservation's capacity, as a fresh arena's would be, whatever its
// mapping holds.
static Arena arena_pool_take(ArenaPool* pool, uint32_t slot, uint64_t capacity, uint64_t requested)
{
    Arena result = pool->arenas[slot];
    result.capacity = capacity;
    result.requested_capacity = requested;
    pool->count -= 1;
    pool->arenas[slot] = pool->arenas[pool->count];
    pool->released[slot] = pool->released[pool->count];
    return result;
}

// The flags of the mapping (`mapping_bytes`) that reserves an arena of `capacity` bytes, the first `small` of them base
// pages: with arena_stack_base_pages, MAP_STACK, which maps it VM_NOHUGEPAGE throughout, where arena_reserve_policy
// would advise MADV_NOHUGEPAGE (base pages of 2 MB or more, or a busy host's arena of 2 MB or more). Its huge pages past
// `small`, if any, are advised MADV_HUGEPAGE by the first commit that reaches them (arena_commit), as without it: until
// then they are inaccessible, whatever their advice.
static int arena_reservation_flags(uint64_t capacity, uint64_t small, uint64_t mapping_bytes)
{
    const uint64_t base_pages = capacity > small ? small : mapping_bytes;
    const bool advised = (arena_pages_busy && mapping_bytes >= BUSTER_ARENA_HUGE_PAGE_BYTES) || base_pages >= BUSTER_ARENA_HUGE_PAGE_BYTES;
    return MAP_PRIVATE | MAP_ANONYMOUS | (advised && arena_stack_base_pages ? MAP_STACK : 0);
}

// Past the first `small` bytes, base is placed so that base + small starts on a huge-page boundary, a page or more past
// the mapping's start. The reservation itself is inaccessible and uncharged under strict overcommit; arena_push makes
// only its used prefix writable.
//
// So the layout decides most of the page sizes, and the advice is only where it doesn't (#652): the kernel takes a huge
// page only where one writable mapping covers its whole 2 MB from a huge-page boundary. Below base + small, the writable
// bytes start at base, past inaccessible ones, and end at that boundary, where the huge pages' differently advised
// mapping starts: fewer than 2 MB of them hold no huge page, nor can they join a writable neighbour's mapping. An arena
// of BUSTER_ARENA_SMALL_BYTES or less, base pages alone, has inaccessible pages on both sides for that. From 2 MB,
// MADV_NOHUGEPAGE keeps them base pages. Past base + small, the first commit that reaches there advises MADV_HUGEPAGE
// (arena_commit), which a host whose transparent huge pages are `madvise` needs. An arena that stays in its base pages,
// as most of a small unit's 70 do, takes one mmap and one mprotect, without the two madvise calls, which also split its
// mapping in three, that each reservation made before.
static Arena arena_reserve_policy(uint64_t capacity, uint64_t small, bool bounded)
{
    Arena result = { 0 };
    const uint64_t requested = capacity;
    const uint64_t limit = bounded ? arena_reservation_limit() : BUSTER_ARENA_MAX_CAPACITY;
    capacity = capacity < limit ? capacity : limit;
    // At least BUSTER_ARENA_SMALL_BYTES, past which arena_mapping_bytes leaves room to align the huge pages.
    small = small > BUSTER_ARENA_SMALL_BYTES ? small : BUSTER_ARENA_SMALL_BYTES;
    small = arena_small_floor > small ? arena_small_floor : small;
    ArenaPool* pool = arena_pool;
    const uint32_t slot = arena_pool_choose(pool, capacity, small, 0);
    if (requested != 0 && requested <= BUSTER_ARENA_MAX_CAPACITY && capacity != 0 && slot != BUSTER_ARENA_POOL_SLOTS)
    {
        result = arena_pool_take(pool, slot, capacity, requested);
    }
    else if (requested != 0 && requested <= BUSTER_ARENA_MAX_CAPACITY)
    {
        uint64_t mapping_bytes = arena_mapping_bytes(capacity);
        bool allowed = capacity != 0;
#if BUSTER_SANITIZER_RESOURCES
        allowed = allowed && arena_test_reservation_allowed(arena_mapping_bytes(requested));
#endif
        errno = ENOMEM;
        uint8_t* mapping = allowed ? mmap(0, mapping_bytes, PROT_NONE, arena_reservation_flags(capacity, small, mapping_bytes), -1, 0) : MAP_FAILED;
        // Do not evade an injected failure. Valgrind may reject a valid oversized mapping with EINVAL rather than
        // ENOMEM. Both layout refusals get a bounded retry, never a greedy half-limit map.
        if (bounded && allowed && mapping == MAP_FAILED && (errno == ENOMEM || errno == EINVAL) && capacity > BUSTER_ARENA_COMPACT_BYTES)
        {
            arena_compact = true;
            capacity = BUSTER_ARENA_COMPACT_BYTES;
            mapping_bytes = arena_mapping_bytes(capacity);
            mapping = mmap(0, mapping_bytes, PROT_NONE, arena_reservation_flags(capacity, small, mapping_bytes), -1, 0);
        }
        if (mapping != MAP_FAILED)
        {
            // An arena of base pages alone starts at its mapping's start (c_check_deferred.c's lanes' slices rely on it), or
            // within BUSTER_ARENA_SMALL_BYTES a page past it, with a page past its end too.
            uint64_t offset = capacity <= BUSTER_ARENA_SMALL_BYTES ? BUSTER_ARENA_PAGE_BYTES : 0;
            if (capacity > small)
            {
                const uintptr_t huge_begin = ((uintptr_t)mapping + BUSTER_ARENA_PAGE_BYTES + small + BUSTER_ARENA_HUGE_PAGE_BYTES - 1) & ~(BUSTER_ARENA_HUGE_PAGE_BYTES - 1);
                offset = huge_begin - small - (uintptr_t)mapping;
            }
            const uint64_t base_pages = capacity > small ? small : mapping_bytes;
            // Where arena_reservation_flags mapped it VM_NOHUGEPAGE (arena_stack_base_pages), the mapping needs no advice.
            const bool mapped = arena_stack_base_pages;
            if (!mapped && arena_pages_busy && mapping_bytes >= BUSTER_ARENA_HUGE_PAGE_BYTES)
            {
                // A busy host's arenas are base pages throughout: arena_commit advises none of them huge.
                madvise(mapping, mapping_bytes, MADV_NOHUGEPAGE);
            }
            else if (!mapped && base_pages >= BUSTER_ARENA_HUGE_PAGE_BYTES)
            {
                madvise(mapping, offset + base_pages, MADV_NOHUGEPAGE);
            }
            result.base = mapping + offset;
            result.capacity = capacity;
            result.mapping_offset = offset;
            result.small = small;
            result.mapping_bytes = mapping_bytes;
            result.committed = offset & ~(BUSTER_ARENA_PAGE_BYTES - 1);
            result.requested_capacity = requested;
            result.reserved = capacity;
        }
        else
        {
            arena_failure = (ArenaFailure){ .bytes = requested, .error = errno };
        }
    }
    return result;
}

static Arena arena_reserve_small(uint64_t capacity, uint64_t small)
{
    return arena_reserve_policy(capacity, small, true);
}

// Fixed-size address-indexed tables cannot accept a shortened reservation: their producer's published ids already
// determine the bound. This still reserves PROT_NONE and charges only pushes; it never changes that bound silently.
static __attribute__((unused)) Arena arena_reserve_exact(uint64_t capacity, uint64_t small)
{
    return arena_reserve_policy(capacity, small, false);
}

// arena_reserve_tables' layout, from the first table's start: each table's start, the end of its base pages and its
// end, in whole pages. A table larger than its base pages starts where they end on a huge-page boundary, as an arena of
// its own lays them out (arena_reserve_policy); one of base pages alone starts at the previous table's end, as does the
// first with huge pages, which sets `phase`, where in a huge page the arena's base must be. So each table takes its
// bytes and, but for the first with huge pages, at most a huge page less a page before it: the tables' mapping
// (arena_tables_mapping_bytes) is at most a page larger than their own arenas' (arena_mapping_bytes: their bytes and a
// huge page each), and no larger for two tables or more, which a finite RLIMIT_AS bounds (#1203's review). Returns the
// bytes the tables span, in `huge` whether any has huge pages, and in `layout` a hash of their sizes and base pages,
// never 0.
static uint64_t arena_tables_layout(const ArenaTable* tables, uint32_t count, uint64_t* starts, uint64_t* smalls, uint64_t* ends, uint64_t* phase, bool* huge,
                                    uint64_t* layout)
{
    uint64_t at = 0;
    uint64_t hash = 0xcbf29ce484222325ull ^ count;
    *phase = 0;
    *huge = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t bytes = (tables[i].bytes + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
        uint64_t small = (tables[i].small + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
        small = small > BUSTER_ARENA_SMALL_BYTES ? small : BUSTER_ARENA_SMALL_BYTES;
        small = arena_small_floor > small ? arena_small_floor : small;
        const bool pages = bytes > small;
        const uint64_t into = (at + small) & (BUSTER_ARENA_HUGE_PAGE_BYTES - 1);
        *phase = pages && !*huge ? (BUSTER_ARENA_HUGE_PAGE_BYTES - into) & (BUSTER_ARENA_HUGE_PAGE_BYTES - 1) : *phase;
        *huge = *huge || pages;
        const uint64_t start = pages ? ((at + small + *phase + BUSTER_ARENA_HUGE_PAGE_BYTES - 1) & ~(BUSTER_ARENA_HUGE_PAGE_BYTES - 1)) - *phase - small : at;
        starts[i] = start;
        smalls[i] = start + (pages ? small : bytes);
        ends[i] = start + bytes;
        at = ends[i];
        hash = (hash ^ bytes) * 0x100000001b3ull;
        hash = (hash ^ small) * 0x100000001b3ull;
    }
    *layout = hash | 1;
    return at;
}

// The bytes arena_reserve_tables maps for tables that span `bytes`: an inaccessible page on each side, and where any of
// them has huge pages, room to place the arena's base at its phase within a huge page.
static uint64_t arena_tables_mapping_bytes(uint64_t bytes, bool huge)
{
    return huge ? bytes + BUSTER_ARENA_HUGE_PAGE_BYTES + BUSTER_ARENA_PAGE_BYTES : bytes + 2 * BUSTER_ARENA_PAGE_BYTES;
}

// The tables' mapping starts its first table a page or more past its start, at the layout's phase in a huge page,
// and ends an inaccessible page or more past their end. All of it is base pages but the tables' huge pages (#652's
// advice where the layout doesn't decide): one MADV_NOHUGEPAGE for the mapping and one MADV_HUGEPAGE for each table with
// huge pages, which stay advised while the pool keeps the mapping. Pushing the arena full commits it in one mprotect. A
// mapping refused for its size (ENOMEM, or Valgrind's EINVAL) past BUSTER_ARENA_COMPACT_BYTES switches the thread to
// compact reservations as arena_reserve_policy's does, and a refused commit sets arena_commit_refused (arena_commit), so
// that a caller that sizes its tables by arena_capacity_shift (pp_initialize) can try smaller ones.
//
// The mapping is MAP_NORESERVE (#1521). Its commit makes the tables writable whole, but they are written sparsely, where
// their units reach: the preprocessor's, about 10 GB in their full shape, take a few pages of each for a small unit. The
// kernel's heuristic overcommit (vm.overcommit_memory 0, the default) refuses a single commit larger than its RAM and
// swap, whatever is touched, so a host of 8 GB refused the preprocessor's at every unit's start. Uncharged, their commit
// isn't refused there, and their pages are allocated as they are first written, as every arena's are. Where commit
// charge is bounded the kernel ignores MAP_NORESERVE (strict overcommit) or RLIMIT_DATA still counts the writable
// mapping, so the commit is charged and may be refused, as before.
static Arena arena_reserve_tables(ArenaTable* tables, uint32_t count)
{
    Arena result = { 0 };
    uint64_t starts[BUSTER_ARENA_TABLES];
    uint64_t smalls[BUSTER_ARENA_TABLES];
    uint64_t ends[BUSTER_ARENA_TABLES];
    uint64_t phase = 0;
    bool huge = false;
    uint64_t layout = 0;
    bool valid = count != 0 && count <= BUSTER_ARENA_TABLES;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = tables[i].bytes != 0 && tables[i].bytes <= BUSTER_ARENA_MAX_CAPACITY / BUSTER_ARENA_TABLES;
    }
    const uint64_t bytes = valid ? arena_tables_layout(tables, count, starts, smalls, ends, &phase, &huge, &layout) : 0;
    ArenaPool* pool = arena_pool;
    // An arena of tables' base pages are its capacity (as `small`), so that its commit takes it whole and advises nothing.
    const uint32_t slot = valid ? arena_pool_choose(pool, bytes, bytes, layout) : BUSTER_ARENA_POOL_SLOTS;
    if (slot != BUSTER_ARENA_POOL_SLOTS)
    {
        result = arena_pool_take(pool, slot, bytes, bytes);
    }
    else if (valid)
    {
        const uint64_t mapping_bytes = arena_tables_mapping_bytes(bytes, huge);
        bool allowed = true;
#if BUSTER_SANITIZER_RESOURCES
        allowed = arena_test_reservation_allowed(mapping_bytes);
#endif
        errno = ENOMEM;
        // Base pages but where a table's huge pages are advised below: from 2 MB, by the mapping itself
        // (arena_stack_base_pages: MAP_STACK) or else by advice.
        const bool advised = bytes >= BUSTER_ARENA_HUGE_PAGE_BYTES;
        const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (advised && arena_stack_base_pages ? MAP_STACK : 0);
        uint8_t* mapping = allowed ? mmap(0, mapping_bytes, PROT_NONE, flags, -1, 0) : MAP_FAILED;
        if (mapping != MAP_FAILED)
        {
            // The base, `phase` past a huge-page boundary, a page or more past the mapping's start (and so at most a huge
            // page past it).
            const uintptr_t after_guard = (uintptr_t)mapping + BUSTER_ARENA_PAGE_BYTES;
            const uint64_t offset = BUSTER_ARENA_PAGE_BYTES + (huge ? (phase - after_guard) & (BUSTER_ARENA_HUGE_PAGE_BYTES - 1) : 0);
            if (advised && !arena_stack_base_pages)
            {
                madvise(mapping + offset, bytes, MADV_NOHUGEPAGE);
            }
            for (uint32_t i = 0; !arena_pages_busy && i < count; i += 1)
            {
                if (ends[i] > smalls[i])
                {
                    madvise(mapping + offset + smalls[i], ends[i] - smalls[i], MADV_HUGEPAGE);
                }
            }
            result = (Arena){
                .base = mapping + offset,
                .capacity = bytes,
                .mapping_offset = offset,
                .small = bytes,
                .mapping_bytes = mapping_bytes,
                .committed = offset,
                .requested_capacity = bytes,
                .reserved = bytes,
                .layout = layout,
            };
        }
        else
        {
            arena_failure = (ArenaFailure){ .bytes = bytes, .error = errno };
            arena_compact = arena_compact || (allowed && (errno == ENOMEM || errno == EINVAL) && bytes > BUSTER_ARENA_COMPACT_BYTES);
        }
    }
    uint8_t* base = result.base != 0 ? arena_push(&result, bytes, BUSTER_ARENA_PAGE_BYTES) : 0;
    if (base == 0)
    {
        arena_release(&result);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        tables[i].base = base != 0 ? base + starts[i] : 0;
    }
    return result;
}

// Powers-of-two table capacities keep their hashing invariants. Ten reductions still leave 1024 file records and
// contexts, more than the fixed include-depth limit. If even that shape cannot be admitted, initialization fails. Once
// a commit of the thread's has been refused (arena_commit_refused), the shape is the compact one, as with compact
// reservations: these tables are committed whole.
static __attribute__((unused)) uint32_t arena_capacity_shift(uint64_t largest)
{
    const uint64_t reservation = arena_reservation_limit();
    const uint64_t limit = arena_commit_refused && reservation > BUSTER_ARENA_COMPACT_BYTES ? BUSTER_ARENA_COMPACT_BYTES : reservation;
    uint32_t shift = 0;
    while (shift < 10 && (largest >> shift) + 64 > limit)
    {
        shift += 1;
    }
    return shift;
}

static Arena arena_reserve(uint64_t capacity)
{
    return arena_reserve_small(capacity, BUSTER_ARENA_SMALL_BYTES);
}

// A busy host is one whose tasks runnable now, this process's among them (/proc/loadavg's fourth field's first number),
// are at least 1/BUSTER_ARENA_BUSY_SHARE of its CPUs online, and two or more.
#define BUSTER_ARENA_BUSY_SHARE 4

// The tasks runnable that `length` bytes of /proc/loadavg say ("LOAD1 LOAD5 LOAD15 RUNNABLE/TASKS LAST": the digits
// before the slash), at most UINT32_MAX; 0 for text that doesn't say.
static uint64_t arena_loadavg_runnable(const char* text, uint64_t length)
{
    const char* slash = length > 0 ? memchr(text, '/', length) : 0;
    const char* digits = slash;
    while (digits != 0 && digits > text && digits[-1] >= '0' && digits[-1] <= '9')
    {
        digits -= 1;
    }
    uint64_t runnable = 0;
    for (const char* digit = digits; digit != 0 && digit < slash; digit += 1)
    {
        runnable = runnable * 10 + (uint64_t)(*digit - '0');
        runnable = runnable < UINT32_MAX ? runnable : UINT32_MAX;
    }
    return runnable;
}

// Whether the host is busy: other processes run beside this one on enough of its cores that their memory traffic, more
// than their faults, may bound what each does (#672). In that issue's exploratory record (unpaired rounds in a fixed
// order), independent compiles of the frozen sqlite3.c on the 9700X, one per core (8 cores, 15 CPUs online), had
// observed medians of 269.7 MB/s of source with base pages against 233.2 with huge pages on 8, about the same on 4,
// and lower on 2 and 1 (104.8 against 117.8, 56.4 against 67.0). A huge page's first touch clears 2 MB at once, which
// an arena may use little of, and which the other cores' traffic can evict before the arena writes it; a base page is
// cleared as it is first written. Reads two small files, the second only where more than this process runs.
static bool arena_host_busy(void)
{
    const char* forced = getenv("BUSTER_TEST_HOST_BUSY");
    bool result = forced != 0 && strcmp(forced, "1") == 0;
    char text[128];
    const int file = forced == 0 ? open("/proc/loadavg", O_RDONLY | O_CLOEXEC) : -1;
    const ssize_t length = file >= 0 ? read(file, text, sizeof(text)) : -1;
    if (file >= 0)
    {
        close(file);
    }
    const uint64_t runnable = length > 0 ? arena_loadavg_runnable(text, (uint64_t)length) : 0;
    if (forced == 0 && runnable >= 2)
    {
        const long online = sysconf(_SC_NPROCESSORS_ONLN);
        result = online > 0 && runnable * BUSTER_ARENA_BUSY_SHARE >= (uint64_t)online;
    }
    return result;
}

// Whether `release`, the kernel's (uname's), is Linux 6.7 or later, whose MAP_STACK maps a region VM_NOHUGEPAGE: its
// major and minor numbers, compared in order. A release that doesn't start with them is taken for an older kernel.
static bool arena_kernel_maps_stack_base_pages(const char* release)
{
    uint64_t numbers[2] = { 0, 0 };
    uint32_t at = 0;
    bool valid = true;
    for (uint32_t part = 0; valid && part < 2; part += 1)
    {
        const uint32_t start = at;
        while (release[at] >= '0' && release[at] <= '9' && at - start < 9)
        {
            numbers[part] = numbers[part] * 10 + (uint64_t)(release[at] - '0');
            at += 1;
        }
        valid = at > start && (part == 1 || release[at] == '.');
        at += part == 0 && valid ? 1 : 0;
    }
    return valid && (numbers[0] > 6 || (numbers[0] == 6 && numbers[1] >= 7));
}

// A huge page's first touch on a virtual machine made the host back all of it, whatever the arena used of it: on #1109's
// 4-core VM a fault took about 1 ms, and the C driver's checking of main.c ran 2 to 4 times as long as with base pages, a
// small unit 30% longer at the median and up to 7 times at the tail. On the 9700X a huge page clears in about 40 µs, and
// a large unit's arenas fill theirs (docs/translation-units.md, "Memory"), but not where the other cores are busy
// (arena_host_busy): then the arenas alone take base pages (arena_pages_busy), by their own advice, which no process this
// one starts inherits. A process started with them off keeps them off.
static void arena_page_policy(const char* setting)
{
    struct utsname kernel;
    arena_stack_base_pages = uname(&kernel) == 0 && arena_kernel_maps_stack_base_pages(kernel.release);
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    const bool virtual_machine = __get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0 && (ecx & (1u << 31)) != 0;
    const bool off = setting != 0 && strcmp(setting, "0") == 0;
    const bool on = setting != 0 && strcmp(setting, "1") == 0;
    arena_pages_busy = !off && !on && !virtual_machine && prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0) == 0 && arena_host_busy();
    if (off || (virtual_machine && !on))
    {
        prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0);
    }
    else if (on)
    {
        prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0);
    }
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
    if (arena->base != 0 && arena->mapping_bytes != 0)
    {
        ArenaPool* pool = arena_pool;
        uint8_t* mapping = arena->base - arena->mapping_offset;
        const uint64_t bytes = arena->mapping_bytes;
        const uint64_t begin = arena->mapping_offset & ~(BUSTER_ARENA_PAGE_BYTES - 1);
        // What stays writable for the arena's next user: its committed prefix, or none where commit charge is bounded.
        uint64_t committed = begin;
        if (pool != 0)
        {
            arena_pool_limits(pool);
            committed = pool->keeps_commits ? arena->committed : begin;
        }
        const bool protected = pool != 0 && !arena->unaccounted &&
                               (arena->committed == committed || mprotect(mapping + begin, arena->committed - begin, PROT_NONE) == 0);
        const bool pooled = protected && madvise(mapping, bytes, MADV_DONTNEED) == 0;
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
                munmap(old->base - old->mapping_offset, old->mapping_bytes);
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
                .huge = arena->huge,
                .mapping_bytes = bytes,
                .committed = committed,
                .requested_capacity = arena->requested_capacity,
                .reserved = arena->reserved,
                .layout = arena->layout,
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
        munmap(pool->arenas[i].base - pool->arenas[i].mapping_offset, pool->arenas[i].mapping_bytes);
    }
    pool->count = 0;
    pool->limits = false;
}

// Undoes a commit attempt that may have applied to a leading part of [committed, target) before refusing across a
// VMA boundary: PROT_NONE every page of it, halving a range that refuses until it applies or is one page. PROT_NONE
// only removes access, so retried parts stay correct; a single-page failure ends the walk, with the pages before it
// certainly inaccessible again — [at, target) may still be writable, which the caller accounts for.
static uint64_t arena_reprotect(uint8_t* mapping, uint64_t committed, uint64_t target)
{
    uint64_t at = committed;
    while (at < target)
    {
        uint64_t span = target - at;
        bool applied = mprotect(mapping + at, span, PROT_NONE) == 0;
        while (!applied && span > BUSTER_ARENA_PAGE_BYTES)
        {
            span = (span >> 1) & ~(uint64_t)(BUSTER_ARENA_PAGE_BYTES - 1);
            applied = mprotect(mapping + at, span, PROT_NONE) == 0;
        }
        if (!applied)
        {
            break;
        }
        at += span;
    }
    return at;
}

// An attempt's residue `at` could not be removed: clamp capacity below it, so that no push's writes reach it, and
// flag the descriptor — arena_release never pools it and unmaps the whole reservation instead.
static void arena_unaccounted(Arena* arena, uint64_t at)
{
    arena->unaccounted = true;
    const uint64_t room = at > arena->mapping_offset ? at - arena->mapping_offset : 0;
    arena->capacity = room < arena->capacity ? room : arena->capacity;
}

// Cold path: amortize protection changes geometrically, keeping tiny arenas in base pages and the huge-page region
// aligned. The descriptor is published only after mprotect succeeds: a failed push changes neither length nor peak.
static bool arena_commit(Arena* arena, uint64_t end)
{
    const uint64_t offset = arena->mapping_offset;
    const uint64_t needed = (offset + end + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
    const uint64_t committed = arena->committed;
    bool result = true;
    if (arena->mapping_bytes != 0 && needed > committed)
    {
        const uint64_t used = committed > offset ? committed - offset : 0;
        uint64_t grown = used > BUSTER_ARENA_SMALL_BYTES ? used * 2 : BUSTER_ARENA_SMALL_BYTES;
        grown = grown > end ? grown : end;
        grown = grown < arena->capacity ? grown : arena->capacity;
        uint64_t target = (offset + grown + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
        if (grown > arena->small && target < arena->mapping_bytes)
        {
            const uintptr_t mapping = (uintptr_t)arena->base - offset;
            const uint64_t huge = ((mapping + target + BUSTER_ARENA_HUGE_PAGE_BYTES - 1) & ~(BUSTER_ARENA_HUGE_PAGE_BYTES - 1)) - mapping;
            target = huge < arena->mapping_bytes ? huge : arena->mapping_bytes;
        }
        uint8_t* mapping = arena->base - offset;
        // The most an attempt could have made writable: an mprotect that refuses across a VMA boundary may still have
        // applied to the leading VMAs, leaving [committed, x) writable and charged for some x at most `applied`. Any
        // such residue is undone before the result is decided (arena_reprotect); what cannot be is accounted for.
        const uint64_t applied = target;
        result = mprotect(mapping + committed, target - committed, PROT_READ | PROT_WRITE) == 0;
        if (!result && target > needed)
        {
            target = needed;
            result = mprotect(mapping + committed, target - committed, PROT_READ | PROT_WRITE) == 0;
        }
        const int error = errno;
        if (result)
        {
            // A successful retry commits only to `needed`; an earlier attempt's residue may run past it, so the undo
            // covers what the attempts touched beyond the commit, never the published prefix [begin, committed).
            if (applied > target)
            {
                const uint64_t at = arena_reprotect(mapping, target, applied);
                if (at < applied)
                {
                    arena_unaccounted(arena, at);
                }
            }
            arena->committed = target;
            // The pages past the base pages, advised huge as the first commit reaches them, before a push returns any
            // of their bytes (arena_reserve_policy).
            const uint64_t small_end = offset + arena->small;
            if (!arena->huge && !arena_pages_busy && arena->capacity > arena->small && target > small_end)
            {
                madvise(mapping + small_end, arena->mapping_bytes - small_end, MADV_HUGEPAGE);
                arena->huge = true;
            }
        }
        else
        {
            const uint64_t at = arena_reprotect(mapping, committed, applied);
            if (at < applied)
            {
                arena_unaccounted(arena, at);
            }
            // A refused commit charges nothing less in a smaller reservation, so it leaves the thread's reservations as
            // they were (#1521) and only its tables that are committed whole shrink (arena_commit_refused).
            arena_failure = (ArenaFailure){ .bytes = target - committed, .error = error, .commit = true };
            arena_commit_refused = true;
        }
    }
    return result;
}

static void* arena_push(Arena* arena, uint64_t size, uint64_t alignment)
{
    void* result = 0;
    const uintptr_t base = (uintptr_t)arena->base;
    const uintptr_t position = base + arena->length;
    const uintptr_t aligned = (position + alignment - 1) & ~(alignment - 1);
    const uint64_t begin = aligned - base;
    const uint64_t capacity = arena->capacity;
    if (base != 0 && alignment != 0 && (alignment & (alignment - 1)) == 0 && aligned >= position && begin <= capacity && size <= capacity - begin)
    {
        const uint64_t end = begin + size;
        if (arena->mapping_bytes == 0 || arena->mapping_offset + end <= arena->committed || arena_commit(arena, end))
        {
            result = (void*)aligned;
            arena->length = end;
            arena->peak = end > arena->peak ? end : arena->peak;
        }
    }
    else if (arena->mapping_bytes != 0 && size > capacity - (begin < capacity ? begin : capacity))
    {
        arena_failure = (ArenaFailure){ .bytes = size, .error = ENOMEM };
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

static bool arena_untouched(const Arena* arena)
{
    return arena->base != 0 && arena->mapping_bytes != 0 && !arena->unaccounted && arena->peak <= arena->length;
}

// The pages arena_discard gives back, past the length from the first huge page boundary past it: to the peak's page, or
// with `whole` to the last huge page boundary at or below the peak (arena_discard_huge).
static void arena_discard_to(Arena* arena, uint64_t from, bool whole)
{
    if (arena->base != 0 && arena->mapping_bytes != 0 && !arena->unaccounted)
    {
        const uintptr_t base = (uintptr_t)arena->base;
        const uint64_t kept = from > arena->length ? from : arena->length;
        const uintptr_t begin = (base + kept + BUSTER_ARENA_HUGE_PAGE_BYTES - 1) & ~(uintptr_t)(BUSTER_ARENA_HUGE_PAGE_BYTES - 1);
        const uint64_t written = arena->committed > arena->mapping_offset ? arena->committed - arena->mapping_offset : 0;
        const uintptr_t last = base + (arena->peak < written ? arena->peak : written);
        const uintptr_t end = whole ? last & ~(uintptr_t)(BUSTER_ARENA_HUGE_PAGE_BYTES - 1) :
                                      (last + BUSTER_ARENA_PAGE_BYTES - 1) & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
        if (end > begin)
        {
            madvise((void*)begin, end - begin, MADV_DONTNEED);
        }
    }
}

static void arena_discard(Arena* arena)
{
    arena_discard_to(arena, 0, false);
}

static void arena_discard_past(Arena* arena, uint64_t kept, bool whole)
{
    arena_discard_to(arena, kept, whole);
}

static void arena_discard_huge(Arena* arena)
{
    arena_discard_to(arena, 0, true);
}

static void arena_static_base_pages(void)
{
    // The linker's bounds of .bss; its first page may hold .data's last bytes, which are file-backed.
    extern char __bss_start[];
    extern char _end[];
    const uintptr_t begin = ((uintptr_t)__bss_start + BUSTER_ARENA_PAGE_BYTES - 1) & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
    const uintptr_t end = ((uintptr_t)_end + BUSTER_ARENA_PAGE_BYTES - 1) & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
    if (end > begin)
    {
        madvise((void*)begin, end - begin, MADV_NOHUGEPAGE);
    }
}

static void arena_populate(void* begin, uint64_t bytes)
{
    const uintptr_t first = (uintptr_t)begin & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
    const uintptr_t end = ((uintptr_t)begin + bytes + BUSTER_ARENA_PAGE_BYTES - 1) & ~(uintptr_t)(BUSTER_ARENA_PAGE_BYTES - 1);
    if (bytes != 0)
    {
        madvise((void*)first, end - first, MADV_POPULATE_WRITE);
    }
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
// The base pages each starts with (#672). A thread's temporaries are mostly a few kilobytes, and its larger ones mostly
// room of which it touches the start: a large file's windowed preparation (pp_prepare_windows) pushes three arrays of a
// word for every 64 of its bytes, 3.5 MB for sqlite3.c's 9.5 MB, and touches each one's start. On #672's cloud VM, in
// huge pages those took two, 4 MB; in base pages the C driver's scratch held 1 MB at sqlite3.c's peak.
#define BUSTER_SCRATCH_ARENA_SMALL_BYTES (4ull << 20)

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
// group's, joined or not), for the tests' accounting; and two tests' hooks, in every build: a directory of CPU topology
// files that lane_core_count reads in place of sysfs' (0 for sysfs'), and a session's lane of index `fail_at` that
// fails to be created, as pthread_create fails when the process may start no more threads (UINT32_MAX for none).
// Process-wide, as main.c's units share it, and in .data rather than .bss, as its initializer isn't zero: the first
// write to a 2 MB region of the executable's .bss clears a transparent huge page, which a compile that touched nothing
// else there paid for (2 MB of peak RSS, on the 9700X).
typedef struct LaneProcess LaneProcess;
struct LaneProcess
{
    uint64_t threads_created;
    const char* topology;
    uint32_t fail_at;
};
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
extern LaneProcess lane_process;
#elif defined(BUSTER_UNIT)
LaneProcess lane_process = { .fail_at = UINT32_MAX };
// The threads the process's lanes started (LaneProcess.threads_created), written at its exit to the file
// BUSTER_TEST_THREADS names, for the tests that count a command's threads (#652): a library preloaded into a dynamic
// executable counts its calls of pthread_create too, which a static one doesn't load.
static __attribute__((destructor, unused)) void lane_threads_report(void)
{
    const char* path = getenv("BUSTER_TEST_THREADS");
    FILE* file = path != 0 ? fopen(path, "w") : 0;
    if (file != 0)
    {
        fprintf(file, "%llu\n", (unsigned long long)__atomic_load_n(&lane_process.threads_created, __ATOMIC_RELAXED));
        fclose(file);
    }
}
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
        *arena = arena_reserve_small(BUSTER_SCRATCH_ARENA_CAPACITY, BUSTER_SCRATCH_ARENA_SMALL_BYTES);
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
    // Whether the group's lanes share CPUs (lane_group_shares_cpus), which a lane that waits for its group reads
    // (lane_wait): with the barrier's lines written every sync, this one is written as the group starts. A narrowed
    // group's lanes each copy the session's answer in as they start (lane_narrow), all the same value, so a read and a
    // write of it there are atomic.
    bool shares_cpus;
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
// thread. Under a session nothing creates a thread: a command's wide parts are rounds of its flow (LaneFlow) on the
// session's lanes, and lane_run runs an operation on a lane alone.
struct LaneSession
{
    // Every lane's group: the outer flow's rendezvous and the announcements of its rounds (LaneFlow).
    LaneGroup group;
    // lane_narrow's groups, one per depth. A narrowing's lanes leave its group as they found it (every sync complete,
    // the take counter back at 0), so the next narrowing at its depth takes it as it is.
    LaneGroup narrowed[BUSTER_LANE_NARROW_DEPTH];
    // The start's gate, which the created lanes wait at until lane 0 knows every lane exists: BUSTER_LANE_GATE_RUN or
    // BUSTER_LANE_GATE_CANCEL.
    uint32_t gate;
    // The lanes the command may take (its capacity); of them, the lanes the session asks for, at most one per core with
    // BUSTER_LANE_SESSION_PER_CORE, shifted left by one for lane_publish's bit 0 (0 until a lazy session's lane 1 has
    // counted the cores, lane_session_asked); the lanes that run (1 after a failed start); and the lanes that existed
    // (lane 0's among them).
    uint32_t capacity;
    uint32_t asked;
    uint32_t count;
    uint32_t created;
    pid_t process;
    void (*entry)(void* argument);
    void* argument;
    // Whether the lanes stay parked when the flow ends, for the process's exit to end (a terminal command's), rather than
    // being joined; whether lane 1 creates the lanes after it while lane 0 runs (BUSTER_LANE_SESSION_LAZY); whether the
    // session runs on a lane per core at most (BUSTER_LANE_SESSION_PER_CORE); whether lane 0 knows how its lanes' start
    // went (lane_session_settle), and whether it has yet to start them (BUSTER_LANE_SESSION_ON_DEMAND,
    // lane_session_start), which only lane 0 reads and writes: no other lane exists until it starts them.
    bool park_at_end;
    bool lazy;
    bool per_core;
    bool settled;
    bool pending;
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
// The same for `parts` parts (no more than `count`, and at most BUSTER_LANE_MAX_PARTS), whatever the lanes: for a caller
// whose parts are fewer per lane than lane_parts_by_cost's (c_check_program_emit.c's growing teams).
static __attribute__((unused)) uint64_t parts_cut_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t parts, uint64_t* boundaries, uint32_t* order);
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
// mwaitx; then sleeps (lane_sleep). A lane of a group that shares CPUs (LaneGroup.shares_cpus) leaves the waiting to
// lane_sleep: a wait in mwaitx, or the spin that takes its place in a build without it, keeps the lane's CPU for up to
// BUSTER_LANE_WAIT_NANOSECONDS, and the lanes that haven't arrived need those CPUs before they can arrive, so that every
// sync would cost a timeslice.
static uint32_t lane_wait(uint32_t* word, uint32_t value)
{
    uint32_t current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    for (uint32_t spin = 0; (current & ~1u) == value && spin < BUSTER_LANE_SPINS; spin += 1)
    {
        _mm_pause();
        current = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    }
    const LaneGroup* group = thread_context.lane.group;
    const bool shares_cpus = group != 0 && __atomic_load_n(&group->shares_cpus, __ATOMIC_RELAXED);
    // A wait that ended while spinning, as at most syncs, doesn't read the clock (rdtsc is 27 µops on Zen 5).
    if (!shares_cpus && (current & ~1u) == value)
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

static uint64_t parts_cut_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t parts, uint64_t* boundaries, uint32_t* order)
{
    uint64_t cut = parts < count ? parts : count;
    cut = cut < BUSTER_LANE_MAX_PARTS ? cut : BUSTER_LANE_MAX_PARTS;
    for (uint64_t i = 0; i <= cut; i += 1)
    {
        boundaries[i] = lane_part_boundary(cost_prefix, count, i, cut);
    }
    lane_parts_order(cost_prefix, boundaries, cut, order);
    return cut;
}

static uint64_t parts_by_cost(const uint64_t* cost_prefix, uint64_t count, uint32_t lanes, uint64_t* boundaries, uint32_t* order)
{
    return parts_cut_by_cost(cost_prefix, count, lane_part_count_for(count, lanes < BUSTER_LANE_MAX_COUNT ? lanes : BUSTER_LANE_MAX_COUNT), boundaries, order);
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
        // A narrowed group of several lanes shares the CPUs its session's lanes do, which the session worked out as it
        // started (LaneGroup.shares_cpus). Every inside lane copies it, the same value, as the lanes enter the group.
        if (count > 1)
        {
            __atomic_store_n(&thread_context.session->narrowed[depth].shares_cpus,
                             __atomic_load_n(&thread_context.session->group.shares_cpus, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
        }
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

// The CPUs the process may run on, counted from its affinity mask (lane_core_count counts those CPUs' cores instead).
// One read is a system call, and a group that pins no CPU of its own makes it once, as it starts.
static uint32_t lane_cpu_count(void)
{
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const long written = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask);
    const uint32_t words = written > 0 ? (uint32_t)((uint64_t)written / 8) : 0;
    uint32_t result = 0;
    for (uint32_t word = 0; word < words && word < BUSTER_LANE_CPU_MASK_WORDS; word += 1)
    {
        result += (uint32_t)__builtin_popcountll(mask[word]);
    }
    return result != 0 ? result : 1;
}

// Whether a group of `lanes` lanes shares the CPUs the process may run on: more lanes than CPUs, none of them pinned to a
// CPU of its own (`pinned`). A lane of such a group that waits sleeps at once (lane_wait), rather than hold the CPU a lane
// that hasn't arrived needs for up to BUSTER_LANE_WAIT_NANOSECONDS; with one lane per CPU its wait costs the group
// nothing.
static bool lane_group_shares_cpus(uint32_t lanes, bool pinned)
{
    return !pinned && lanes > 1 && lanes > lane_cpu_count();
}

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
// Under a session (#905) it creates no thread: a lane alone (lane 0 between its flow's rounds, a session of one) runs
// the operation on its own, and a lane of a group of several is refused, as a nested run is; a command's wide parts are
// rounds of its flow (LaneFlow). cpus must be null or start with the session's first.
static __attribute__((unused)) bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus);

// lane_session_run's flags. PARK_AT_END: the lanes stay parked when the flow ends, for the process's exit to end them (a
// terminal command's, whose process exits next), but in sanitizer builds, which join them always; the session must then
// outlive the process. LAZY: lane 0 creates lane 1 alone and runs the flow at once, while lane 1 creates the others; lane
// 0 learns how their start went with lane_session_settle, which it must call before it meets any other lane or counts on
// them (but for lane_count, which says the capacity until then, and lane_session_asked). Without LAZY, or with a CPU
// list, lane 0 creates every lane before the flow. PER_CORE: the session asks for no more lanes than the cores of the
// CPUs the process may run on (lane_core_count), as two lanes on one core's SMT siblings share its pipes, caches and
// power: lazily, from 2 lanes on, lane 1 counts them while lane 0 runs, before it creates the lanes after it, and an
// eager session's lane 0 counts them before it creates any. Ignored with a CPU list. ON_DEMAND, of a lazy session
// (#1444): lane 0 creates no lane as the session starts, but as it calls lane_session_start, from which the lanes start
// as LAZY's do. Lane 0 runs on the capacity until then, as LAZY's does until it settles, and must start them before it
// first settles or asks for lanes (lane_session_settle, lane_session_asked), which otherwise leave the session on lane 0
// alone, asking for one lane: a flow that doesn't call it creates no thread, for a command whose input may not pay for
// its lanes (the C driver's one source file).
#define BUSTER_LANE_SESSION_PARK_AT_END 1u
#define BUSTER_LANE_SESSION_LAZY 2u
#define BUSTER_LANE_SESSION_PER_CORE 4u
#define BUSTER_LANE_SESSION_REQUIRE_START 8u
#define BUSTER_LANE_SESSION_ON_DEMAND 16u

// Runs entry(argument) on every lane of a new session of `capacity` lanes (#905), the process's outer flow: the calling
// thread is lane 0, and the session creates capacity - 1 threads (none for one; with BUSTER_LANE_SESSION_PER_CORE, one
// fewer than the lanes it asks for, but lazily lane 1 where one core leaves it none) with the asynchronous signals
// blocked, so that a signal sent to the process reaches lane 0, and an explicit stack size. With `cpus`, each lane pins
// itself to its CPU, and lane 0 waits for the pins; without, it waits for nothing but each thread's creation, or with
// BUSTER_LANE_SESSION_LAZY for lane 1's alone, and with BUSTER_LANE_SESSION_ON_DEMAND for none until lane_session_start.
// A failed creation or pin cancels every lane created, which the session joins, and runs the flow on lane 0 alone: one
// lane, never a retry. Returns how many lanes ran it (0 if a session of the calling thread's is live: sessions don't
// nest). With REQUIRE_START, a failed start returns 0 without running the flow, rather than falling back to one lane. The
// lanes are joined when the flow ends, but with BUSTER_LANE_SESSION_PARK_AT_END (`flags`).
static __attribute__((unused)) uint32_t lane_session_run(LaneSession* session, uint32_t capacity, const uint32_t* cpus, void (*entry)(void* argument),
                                                         void* argument, uint32_t flags);
// On lane 0 of an on-demand session (BUSTER_LANE_SESSION_ON_DEMAND) that it hasn't settled or asked for lanes yet, starts
// its lanes as a lazy session's start does: lane 0 creates lane 1, which counts the cores, creates the lanes after it and
// opens the gate while lane 0 goes on, which learns how their start went as it settles (lane_session_settle). Its first
// call alone does this; elsewhere, and after, it does nothing.
static __attribute__((unused)) void lane_session_start(void);
// The lanes the calling thread's session runs on: for lane 0 of a lazy session, once lane 1 has said how their start
// went, which it waits for the first time; lane 0's lane in the session's group then counts them, and on a failed start
// becomes a lane alone, the session's count 1. 1 outside a session, and for an on-demand session that lane 0 settles
// before it starts its lanes, which then stays on lane 0 alone.
static __attribute__((unused)) uint32_t lane_session_settle(void);
// The lanes the calling thread's session asks for: its capacity, at most one per core with BUSTER_LANE_SESSION_PER_CORE.
// Lane 0 of a lazy session waits only for lane 1 to have counted the cores, not for their lanes' start
// (lane_session_settle), which may still leave it on one. 1 outside a session, and for an on-demand session that lane 0
// asks before it starts its lanes, which then stays on lane 0 alone.
static __attribute__((unused)) uint32_t lane_session_asked(void);
// The cores of the CPUs in `mask`, `words` 64-bit words of a sched_getaffinity mask, at least 1: each CPU's core is the
// set of its siblings that `topology`'s cpuN/topology/core_cpus_list gives ("0,8", "0-1" or "5", sysfs' format), read
// for one CPU of each core, the others skipped. A CPU whose list can't be read, doesn't parse or leaves it out counts as
// a core of its own.
static __attribute__((unused)) uint32_t lane_cores(const char* topology, const uint64_t* mask, uint32_t words);
// The cores of the CPUs this process may run on, at least 1: lane_cores of sched_getaffinity's mask, in sysfs (or in a
// test's LaneProcess.topology).
static __attribute__((unused)) uint32_t lane_core_count(void);
// The session the calling thread is a lane of, or 0. A child forked from a lane of a session forgets it here, and runs as
// one lane: its other lanes don't exist in the child.
static __attribute__((unused)) LaneSession* lane_session_current(void);
// A terminal command's exit (#1444), called last before the process exits, by a process that created no thread
// (LaneProcess.threads_created): the C library's streams flushed and every descriptor closed, it starts a helper, a
// process that shares the address space and holds nothing else, which ends as soon as the caller has, so that the
// caller's exit leaves the address space's teardown to it rather than run it before its parent's wait returns. By
// default only where orphans are reaped (lane_exit_reaped); BUSTER_EXIT_HELPER=1 starts it wherever it can, =0 never.
// Never in a sanitizer build. What the caller writes after it reaches no descriptor it had.
static __attribute__((unused)) void lane_exit_helper_start(void);
// Whether this process's PID namespace reaps orphans, by its PID 1, which takes them where no subreaper does first:
// whether it blocks, ignores or catches SIGCHLD (/proc/1/stat), as an init that waits does (systemd's signalfd, tini's
// sigwait, a shell's handler), or the kernel does for it (ignored).
static __attribute__((unused)) bool lane_exit_reaped(void);

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
            .shares_cpus = lane_group_shares_cpus(count, team->has_cpus),
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
    // The lane is the group's before the gate, which a group that shares CPUs then waits at with lane_sleep (lane_wait):
    // the lanes created so far would otherwise hold their CPUs while the others are created.
    thread_context.lane = (Lane){
        .index = start->index,
        .count = start->count,
        .group = start->group,
    };
    if (lane_wait(&start->group->gate, 0) == BUSTER_LANE_GATE_RUN)
    {
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
        .shares_cpus = lane_group_shares_cpus(count, cpus != 0),
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

// lane_run under a session: an operation of a lane alone, run on it as lane 0 of 1.
static bool lane_session_alone(LaneSession* session, uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    const bool result = count >= 1 && entry != 0 && thread_context.lane.count == 1 && (cpus == 0 || (session->has_cpus && cpus[0] == session->cpus[0]));
    if (result)
    {
        const Lane previous = thread_context.lane;
        const uint64_t taken = thread_context.taken;
        thread_context.lane = (Lane){ .count = 1 };
        thread_context.taken = 0;
        entry(argument);
        thread_context.lane = previous;
        thread_context.taken = taken;
    }
    return result;
}

static bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus)
{
    LaneSession* session = lane_session_current();
    return session != 0                ? lane_session_alone(session, count, entry, argument, cpus) :
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
        thread_context.narrow_depth = 0;
        thread_context.running = false;
        thread_context.taken = 0;
        session = 0;
    }
    return session;
}

// lane_cores' number at text[*at], of the decimal digits there, which *at passes and *digits counts; past the most CPUs
// a mask holds, it stays at BUSTER_LANE_CORES_SATURATED.
#define BUSTER_LANE_CORES_SATURATED (1u << 24)
static uint32_t lane_cores_number(const char* text, ssize_t length, ssize_t* at, uint32_t* digits)
{
    uint32_t value = 0;
    *digits = 0;
    while (*at < length && text[*at] >= '0' && text[*at] <= '9')
    {
        value = value < BUSTER_LANE_CORES_SATURATED ? value * 10 + (uint32_t)(text[*at] - '0') : BUSTER_LANE_CORES_SATURATED;
        *digits += 1;
        *at += 1;
    }
    return value;
}

// A list's read is its open, kernfs' formatting and the close: on the 9700X 9.4 µs for its 8 cores, and 19 to 55 µs as
// a process's first count; on a 16-CPU Zen 4 31 µs, and 50 to 65 µs. The time is the system calls', with no vector path
// to take, and a session counts on lane 1, off lane 0's path (BUSTER_LANE_SESSION_PER_CORE).
static uint32_t lane_cores(const char* topology, const uint64_t* mask, uint32_t words)
{
    const uint32_t cpus = (words < BUSTER_LANE_CPU_MASK_WORDS ? words : BUSTER_LANE_CPU_MASK_WORDS) * 64;
    uint64_t counted[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint32_t cores = 0;
    for (uint32_t cpu = 0; cpu < cpus; cpu += 1)
    {
        const uint64_t bit = UINT64_C(1) << (cpu % 64);
        if ((mask[cpu / 64] & bit) != 0 && (counted[cpu / 64] & bit) == 0)
        {
            char path[256];
            char text[512];
            const int written = snprintf(path, sizeof(path), "%s/cpu%u/topology/core_cpus_list", topology, cpu);
            const int fd = written > 0 && (uint32_t)written < sizeof(path) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
            const ssize_t length = fd >= 0 ? read(fd, text, sizeof(text)) : -1;
            if (fd >= 0)
            {
                close(fd);
            }
            // The list's items, "a" or "a-b", up to a newline or its end; a list the buffer cuts, or with anything else,
            // is none. Its CPUs past the mask's don't count.
            uint64_t siblings[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
            bool valid = length > 0 && length < (ssize_t)sizeof(text);
            ssize_t at = 0;
            bool more = valid;
            while (more)
            {
                uint32_t digits = 0;
                const uint32_t first = lane_cores_number(text, length, &at, &digits);
                // A range's second number follows its dash.
                const bool range = digits != 0 && at < length && text[at] == '-';
                at += range ? 1 : 0;
                uint32_t last_digits = digits;
                const uint32_t last = range ? lane_cores_number(text, length, &at, &last_digits) : first;
                valid = digits != 0 && last_digits != 0 && first <= last;
                for (uint32_t sibling = first; valid && sibling <= last && sibling < cpus; sibling += 1)
                {
                    siblings[sibling / 64] |= UINT64_C(1) << (sibling % 64);
                }
                more = valid && at < length && text[at] == ',';
                at += more ? 1 : 0;
            }
            valid = valid && (at == length || text[at] == '\n') && (siblings[cpu / 64] & bit) != 0;
            for (uint32_t word = 0; valid && word < cpus / 64; word += 1)
            {
                counted[word] |= siblings[word];
            }
            counted[cpu / 64] |= bit;
            cores += 1;
        }
    }
    return cores != 0 ? cores : 1;
}

static uint32_t lane_core_count(void)
{
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const long written = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask);
    const char* topology = lane_process.topology != 0 ? lane_process.topology : "/sys/devices/system/cpu";
    return written > 0 ? lane_cores(topology, mask, (uint32_t)((uint64_t)written / 8)) : 1;
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

// lane_session_create's lanes, created with every asynchronous signal blocked, which they keep, so that a signal sent to
// the process reaches lane 0; the synchronous ones stay as they are, for the handlers of tests that fault on purpose.
// The calling thread's mask is its own again after.
static uint32_t lane_session_spawn(LaneSession* session, uint32_t first, uint32_t end)
{
    sigset_t blocked;
    sigset_t previous;
    sigfillset(&blocked);
    const int synchronous[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP, SIGSYS };
    for (uint32_t i = 0; i < sizeof(synchronous) / sizeof(synchronous[0]); i += 1)
    {
        sigdelset(&blocked, synchronous[i]);
    }
    const bool masked = pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0;
    const uint32_t created = masked ? lane_session_create(session, first, end) : 0;
    if (masked)
    {
        pthread_sigmask(SIG_SETMASK, &previous, 0);
    }
    return created;
}

// A created lane: it pins itself if asked, waits at the gate until lane 0 (or, in a lazy session, lane 1) knows every
// lane exists, and runs the flow. A lazy session's lane 1 creates the lanes after it first, and opens the gate: with
// BUSTER_LANE_SESSION_PER_CORE, once it has counted the cores and published the lanes it asks for, which may leave it
// none to create, or itself none to run (one core).
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
        const uint32_t cores = session->per_core ? lane_core_count() : session->capacity;
        const uint32_t asked = cores < session->capacity ? cores : session->capacity;
        if (session->per_core)
        {
            lane_publish(&session->asked, asked << 1);
        }
        const uint32_t created = 2 + lane_session_create(session, 2, asked);
        const bool started = asked > 1 && created == asked;
        session->count = started ? asked : 1;
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
    uint32_t lanes = capacity == 0 ? 1 : capacity < BUSTER_LANE_MAX_COUNT ? capacity : BUSTER_LANE_MAX_COUNT;
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
        session->per_core = (flags & BUSTER_LANE_SESSION_PER_CORE) != 0 && cpus == 0 && lanes > 1;
        // Lazily from 3 lanes, or from 2 counting the cores, which lane 1 then does; on demand, once lane 0 starts them.
        session->lazy = (flags & BUSTER_LANE_SESSION_LAZY) != 0 && (flags & BUSTER_LANE_SESSION_REQUIRE_START) == 0 && cpus == 0 && (lanes > 2 || session->per_core);
        session->pending = (flags & BUSTER_LANE_SESSION_ON_DEMAND) != 0 && session->lazy;
        if (session->per_core && !session->lazy)
        {
            const uint32_t cores = lane_core_count();
            lanes = cores < lanes ? cores : lanes;
        }
        // A lazy session counting the cores asks for its lanes once lane 1 has (lane_session_asked).
        session->asked = session->per_core && session->lazy ? 0 : lanes << 1;
        session->has_cpus = cpus != 0;
        if (cpus != 0)
        {
            memcpy(session->cpus, cpus, lanes * sizeof(*cpus));
        }
        uint64_t affinity[BUSTER_LANE_CPU_MASK_WORDS];
        const bool saved = cpus != 0 && syscall(SYS_sched_getaffinity, 0, sizeof(affinity), affinity) > 0;
        bool valid = cpus == 0 || (saved && lane_pin(cpus[0]));
        uint32_t created = 1;
        if (lanes > 1 && !session->pending)
        {
            // Lazily, lane 0 creates lane 1 alone, which creates the others while lane 0 goes on, and decides the gate;
            // the session's count is the lanes asked for until lane 1 opens it.
            session->count = lanes;
            created += valid ? lane_session_spawn(session, 1, session->lazy ? 2 : lanes) : 0;
            for (uint32_t i = 1; cpus != 0 && i < created; i += 1)
            {
                lane_wait(&session->starts[i].ready, 0);
                valid = valid && session->starts[i].pinned;
            }
        }
        bool started = valid && created == lanes;
        if (session->pending)
        {
            // No lane but lane 0 until it starts them (lane_session_start), which runs on the capacity meanwhile.
            session->count = 1;
            session->created = 1;
            started = true;
        }
        else if (session->lazy && created == 2)
        {
            // Lane 1 opens the gate; lane 0 runs on the capacity until it settles (lane_session_settle).
            started = true;
        }
        else
        {
            // The created lanes read `lazy` as they start: it changes only for a lazy session whose lane 1 didn't start,
            // which has no other lane, and never counted the cores: the session asked for its capacity.
            if (session->lazy)
            {
                session->lazy = false;
                session->asked = lanes << 1;
            }
            session->count = started ? lanes : 1;
            session->created = created;
            __atomic_store_n(&session->group.shares_cpus, lane_group_shares_cpus(session->count, session->has_cpus), __ATOMIC_RELAXED);
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
        const bool run_entry = started || (flags & BUSTER_LANE_SESSION_REQUIRE_START) == 0;
        if (run_entry)
        {
            entry(argument);
        }
        // A lazy flow that never met its lanes: they are lane 1's to start, and the session's to end; an on-demand one's
        // that lane 0 never started stays on it alone.
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
        result = run_entry ? session->count : 0;
    }
    return result;
}

// An on-demand session that lane 0 settles or asks before it has started its lanes: it never will, and stays on lane 0
// alone, which asked for one lane. Lane 0 settles it as a lazy session whose lane 1 didn't start: its gate cancelled, its
// count 1 and no lane created but lane 0's, as they are until lane_session_start.
static void lane_session_forgo(LaneSession* session)
{
    session->pending = false;
    session->asked = 1u << 1;
    lane_publish(&session->gate, BUSTER_LANE_GATE_CANCEL);
}

static void lane_session_start(void)
{
    LaneSession* session = lane_session_current();
    if (session != 0 && pthread_equal(session->owner, pthread_self()) && session->pending)
    {
        session->pending = false;
        // Lane 1 counts the cores, creates the lanes after it and publishes the count, the lanes it created and the
        // gate's state (lane_session_thread_main): the session's start, as lane_session_run's lazy one, from here. If it
        // doesn't start, the session runs on lane 0 alone, which never counted the cores: it asked for its capacity.
        if (lane_session_spawn(session, 1, 2) == 0)
        {
            session->asked = session->capacity << 1;
            lane_publish(&session->gate, BUSTER_LANE_GATE_CANCEL);
        }
    }
}

static uint32_t lane_session_settle(void)
{
    LaneSession* session = lane_session_current();
    uint32_t result = 1;
    // `settled` is lane 0's alone, which it writes while the other lanes run: they never read it.
    if (session != 0 && pthread_equal(session->owner, pthread_self()) && !session->settled)
    {
        if (session->pending)
        {
            lane_session_forgo(session);
        }
        // Lane 1 publishes the count and the lanes it created before it opens the gate.
        const bool run = lane_sleep(&session->gate, 0) == BUSTER_LANE_GATE_RUN;
        session->settled = true;
        for (uint32_t i = 1; !run && i < session->created; i += 1)
        {
            pthread_join(session->threads[i], 0);
        }
        // Whether the lanes that run share the CPUs they run on, whichever group lane 0 is in as it settles: the
        // session's, or a flow's, which lane 0 began before it settled (lane_flow_begin makes it a group of one, as the C
        // driver's and link-foreign's flows do), and whose rounds then narrow from the session's answer.
        __atomic_store_n(&session->group.shares_cpus, lane_group_shares_cpus(run ? session->count : 1, session->has_cpus), __ATOMIC_RELAXED);
        // The lanes that run: with one per core, fewer than the capacity lane 0 started on.
        if (thread_context.lane.group == &session->group)
        {
            thread_context.lane = run ? (Lane){ .index = 0, .count = session->count, .group = &session->group } : (Lane){ .count = 1 };
        }
    }
    result = session != 0 ? session->count : 1;
    return result;
}

static uint32_t lane_session_asked(void)
{
    LaneSession* session = lane_session_current();
    if (session != 0 && pthread_equal(session->owner, pthread_self()) && session->pending)
    {
        lane_session_forgo(session);
    }
    // A lazy session's lane 1 publishes it before it creates the lanes after it, which read it after the gate.
    return session != 0 ? lane_sleep(&session->asked, 0) >> 1 : 1;
}

static void lane_flow_begin(LaneFlow* flow)
{
    flow->group = lane_alone();
    flow->alone = flow->group;
    flow->argument = 0;
}

// The flow's group, which lane 0 kept before its session settled (lane_flow_begin), as settled: a lazy session's lane 0
// learns here how its lanes started, and a group of the session's then counts the lanes that run, with one per core
// (BUSTER_LANE_SESSION_PER_CORE) fewer than lane 0 started on; one without other lanes to run the flow's rounds on.
static Lane lane_flow_group(const LaneFlow* flow)
{
    const uint32_t lanes = lane_session_settle();
    LaneSession* session = lane_session_current();
    const bool grouped = session != 0 && flow->group.group == &session->group;
    const Lane group = grouped ? (Lane){ .index = flow->group.index, .count = lanes, .group = flow->group.group } : flow->group;
    return lanes > 1 && group.count > 1 ? group : (Lane){ .count = 1 };
}

static uint32_t lane_flow_lanes(const LaneFlow* flow)
{
    return lane_flow_group(flow).count;
}

static void lane_flow_round_begin(LaneFlow* flow, uint32_t kind, void* argument, uint32_t lanes)
{
    assert(kind != 0);
    flow->argument = argument;
    const Lane group = lane_flow_group(flow);
    flow->alone = lane_alone();
    if (group.count > 1)
    {
        lane_restore(group);
        lane_park_broadcast((uint64_t)kind << 32 | lanes, 0);
    }
    lane_narrow(group.count > 1 ? lanes : 1);
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
    const Lane group = lane_flow_group(flow);
    if (group.count > 1)
    {
        lane_restore(group);
        lane_park_broadcast(0, 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// A terminal command's exit (#1444)
//
// A process tears its address space down in its exit (exit_mmap) before its parent's wait4 returns, unless another task
// still holds it: on the 9700X 90 µs of a one-line -c's 1.1 ms, 130 µs of a #include <stdio.h> file's. Linux 6.18 leaves
// it to a kworker when the process has 6 threads or more (measured, not documented), which a command whose session
// started its lanes has, but not one that runs on lane 0 alone. lane_exit_helper_start gives such a process's teardown to
// a helper: a process (CLONE_VM without CLONE_THREAD) that shares the address space and nothing else, started as the
// command exits so that only a process that would tear down in its exit pays for its start. The helper ends with the
// caller (PR_SET_PDEATHSIG's SIGKILL, or at once if the caller has already gone), and its exit tears the address space
// down while the caller's parent goes on. Its CPU time moves rather than disappears: it is the helper's, an orphan's
// that the nearest subreaper or init reaps, outside the caller's resource use.
//
// The caller flushes the C library's streams and closes every descriptor first, so that the helper holds none: a
// reader of the caller's output, or a jobserver, sees its end as the caller exits, not as the helper does. The helper
// runs on a stack of its own, its signal handlers cleared (CLONE_CLEAR_SIGHAND) and every signal blocked, and makes only
// raw system calls: the C library's state, the caller's stack and its heap are the caller's. A kernel without clone3,
// CLONE_CLEAR_SIGHAND or close_range leaves the caller to exit as before.
//
// An orphan stays a zombie, a process ID, until its new parent waits for it: a PID 1 that never does (a container's or
// a virtual machine's agent run as init) would keep one per compile, and a build of tens of thousands of compiles would
// run out of process IDs. So by default the helper starts only where PID 1 handles SIGCHLD (lane_exit_reaped): about
// 3 µs, a read of /proc/1/stat. Without a helper, everything happens as before it.

// clone3's arguments, its first version (CLONE_ARGS_SIZE_VER0), and the flags the helper takes: CLONE_VM and
// CLONE_CLEAR_SIGHAND (Linux 5.5), no exit signal.
typedef struct LaneExitCloneArguments LaneExitCloneArguments;
struct LaneExitCloneArguments
{
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
};
#define BUSTER_LANE_EXIT_CLONE_VM UINT64_C(0x100)
#define BUSTER_LANE_EXIT_CLONE_CLEAR_SIGHAND (UINT64_C(1) << 32)
#define BUSTER_LANE_EXIT_SYS_CLONE3 435
#define BUSTER_LANE_EXIT_SYS_CLOSE_RANGE 436

// The helper's stack, which only it touches, and its caller's process ID, which the caller writes before it starts it.
static _Alignas(64) uint8_t lane_exit_stack[4096];
static pid_t lane_exit_caller;

// A raw system call of up to four arguments, the helper's only way into the kernel.
static __attribute__((unused)) long lane_exit_syscall(long number, long first, long second, long third, long fourth)
{
    long result = number;
    register long fourth_register __asm__("r10") = fourth;
    __asm__ volatile("syscall" : "+a"(result) : "D"(first), "S"(second), "d"(third), "r"(fourth_register) : "rcx", "r11", "memory");
    return result;
}

// The helper: every signal blocked, it ends with its caller, by the SIGKILL PR_SET_PDEATHSIG sends as the caller exits,
// or at once where the caller had gone before the request took (its parent no longer the caller).
static __attribute__((noreturn, unused)) void lane_exit_helper_main(void)
{
    const uint64_t blocked = UINT64_MAX;
    lane_exit_syscall(SYS_rt_sigprocmask, SIG_SETMASK, (long)&blocked, 0, sizeof(blocked));
    lane_exit_syscall(SYS_prctl, PR_SET_PDEATHSIG, SIGKILL, 0, 0);
    while (lane_exit_syscall(SYS_getppid, 0, 0, 0, 0) == lane_exit_caller)
    {
        lane_exit_syscall(SYS_pause, 0, 0, 0, 0);
    }
    while (true)
    {
        lane_exit_syscall(SYS_exit, 0, 0, 0, 0);
    }
}

// clone3 of `arguments`: in the caller, the helper's process ID or a negative error; the helper starts in `entry`, on the
// arguments' stack, which it never returns from.
static __attribute__((unused)) long lane_exit_clone(const LaneExitCloneArguments* arguments, void (*entry)(void))
{
    long result = BUSTER_LANE_EXIT_SYS_CLONE3;
    __asm__ volatile("syscall\n\t"
                     "testq %%rax, %%rax\n\t"
                     "jnz 1f\n\t"
                     "callq *%%rdx\n\t"
                     "ud2\n"
                     "1:"
                     : "+a"(result)
                     : "D"(arguments), "S"(sizeof(*arguments)), "d"(entry)
                     : "rcx", "r11", "memory");
    return result;
}

// /proc/1/stat's fields 32 to 34, the signals PID 1 blocks, ignores and catches, in decimal, after its command's
// parenthesis (which may hold spaces) and its state (field 3, a letter).
static bool lane_exit_reaped(void)
{
    char text[1024];
    const int fd = open("/proc/1/stat", O_RDONLY | O_CLOEXEC);
    const ssize_t length = fd >= 0 ? read(fd, text, sizeof(text) - 1) : -1;
    if (fd >= 0)
    {
        close(fd);
    }
    text[length > 0 ? length : 0] = 0;
    const char* parenthesis = length > 0 ? strrchr(text, ')') : 0;
    const char* at = parenthesis != 0 ? parenthesis + 1 : 0;
    uint64_t handled = 0;
    uint32_t parsed = 0;
    // Each field from 3 on follows one space: past a number's digits, or for the state, at the next space.
    for (uint32_t field = 3; at != 0 && *at == ' ' && field <= 34; field += 1)
    {
        at += 1;
        char* after = 0;
        const unsigned long long value = strtoull(at, &after, 10);
        const bool number = after != at;
        handled |= field >= 32 && number ? (uint64_t)value : 0;
        parsed += field >= 32 && number ? 1 : 0;
        at = number ? after : strchr(at, ' ');
    }
    return parsed == 3 && (handled & (UINT64_C(1) << (SIGCHLD - 1))) != 0;
}

static void lane_exit_helper_start(void)
{
    const char* setting = getenv("BUSTER_EXIT_HELPER");
    const bool forced = setting != 0 && strcmp(setting, "1") == 0;
    const bool refused = setting != 0 && strcmp(setting, "0") == 0;
    const bool wanted = !BUSTER_SANITIZER_RESOURCES && __atomic_load_n(&lane_process.threads_created, __ATOMIC_RELAXED) == 0 && !refused &&
                        (forced || lane_exit_reaped());
    if (wanted)
    {
        fflush(0);
        const bool closed = syscall(BUSTER_LANE_EXIT_SYS_CLOSE_RANGE, 0u, ~0u, 0u) == 0;
        lane_exit_caller = getpid();
        const LaneExitCloneArguments arguments = {
            .flags = BUSTER_LANE_EXIT_CLONE_VM | BUSTER_LANE_EXIT_CLONE_CLEAR_SIGHAND,
            .stack = (uint64_t)(uintptr_t)lane_exit_stack,
            .stack_size = sizeof(lane_exit_stack),
        };
        const long helper = closed ? lane_exit_clone(&arguments, lane_exit_helper_main) : -1;
        // A test's hook (test-check-lanes): the caller waits this many milliseconds after the helper's start, so that a
        // reader of its output can see that output end while both still run.
        const char* pause = helper > 0 ? getenv("BUSTER_TEST_EXIT_HELPER_PAUSE") : 0;
        const long milliseconds = pause != 0 ? strtol(pause, 0, 10) : 0;
        if (milliseconds > 0)
        {
            const struct timespec wait = {
                .tv_sec = milliseconds / 1000,
                .tv_nsec = (milliseconds % 1000) * 1000000,
            };
            nanosleep(&wait, 0);
        }
    }
}
