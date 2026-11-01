// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The lane tests, for the test-lanes command:
//
//   build/compiler test-lanes [--rounds N] [--seed N]
//
// main.c includes this file after lane.c and calls run_lane_tests for the command. The arenas are checked on the
// calling thread. Every other check runs on groups of 1, 2, 3 and 8 lanes (lane_test_counts), started by lane_run
// without pinning, and what the lanes produce is compared with a sequential reference. The checks, in this order:
//   - Arenas: a push at every power-of-two alignment from 1 to 2 MB after a push that misaligns the arena; invalid
//     alignments; a push that fills the arena exactly, and one more byte, which fails and leaves the arena as it was;
//     sizes and counts whose arithmetic overflows; pop_to; arenas that couldn't be reserved; a 64 GB reservation of which
//     only the touched page is committed (mincore); and the page sizes asked for, read back from /proc/self/smaps: base
//     pages up to base + BUSTER_ARENA_SMALL_BYTES, which is on a huge page boundary, and huge pages from there.
//   - Ranges: lane_range for every count from 0 to BUSTER_LANE_TEST_MAX_RANGE, and lane_range_by_cost for the same
//     counts with each pattern of lane_test_cost: the parts must cover [0, count) in lane order, lane_range's first
//     count % lanes parts must be the longer ones, and lane_range_by_cost must give each item to the lane whose share of
//     the total cost holds the item's midpoint, which keeps each part within the largest cost of an equal share. Once,
//     on the calling thread, the same of range_part and range_part_by_cost with lane_test_part_counts parts, for
//     lane_test_part_range_counts.
//   - Syncs: --rounds rounds (BUSTER_LANE_TEST_DEFAULT_ROUNDS by default) in which each lane writes its slot, syncs and
//     reads every lane's slot, and then gets a broadcast from a different lane each round. Once every
//     BUSTER_LANE_TEST_STALL_INTERVAL rounds a lane comes late, in turn to the sync and to the broadcast, and in turn
//     late enough that the others sleep on the futex and only so late that they wait in mwaitx; with more than one
//     lane, some must have slept. Then as many broadcasts back to back, from changing lanes, with no sync between them,
//     and narrow tasks spread over the lanes by lane_from_task_index, each result broadcast from its task's lane.
//   - Alone: the last lane makes itself a group of one, in which it syncs with no one, broadcasts to itself and gets
//     whole ranges, and then rejoins its group, which goes on broadcasting.
//   - Scratch: the scratch arenas of all lanes are distinct, a conflict with one gets the other, and scratch_end frees
//     what was pushed.
//   - Exchange: records whose keys repeat, split across the lanes, counted per hash partition
//     (BUSTER_LANE_TEST_PARTITIONS of them), placed and filled into one array, and resolved a range of partitions per
//     lane: each key's winner is its record of lowest precedence, the earliest of them on a tie, as a strong definition
//     beats a weak one. For each of lane_test_record_counts, the partitioned records and the winners must be
//     byte-identical to the sequential reference's for every lane count.
//   - Parts: for each of lane_test_part_inputs (a count and a cost pattern), every lane cuts the items with
//     lane_parts_by_cost and the lanes count, place and fill by part (rule 3 in "Lanes"): each item has 0 to 4 bytes of
//     output, the lanes take parts and count their bytes, and after a sync each lane places the parts it took by the
//     prefix sums of every part's bytes and fills them. One lane comes late to each input's loop, by turns long enough to
//     sleep and only briefly. Lane 0 then checks that the output is byte-identical to the sequential reference's, that
//     every part was taken once, that the boundaries are range_part_by_cost's, and that every lane got the same order:
//     the costliest part first (the first of them on a tie), then the others by decreasing bit width of cost and then
//     by index (one input's costs grow within one bucket, so that its costliest part is its bucket's last). The loops
//     follow each other with a sync between them, as the counter must reset itself.
//   - Advance: the lanes but lane 0 (all of them, with one) each advance one word with lane_advance, pausing together now
//     and then long enough that lane 0, waiting on it with lane_wait for every advance, sleeps on its futex (with more
//     than one lane, it must have slept): each value it sees must be even and larger than the last, and none lost.
//   - Shared CPUs: a group with more lanes than the CPUs the process may run on, which pins no CPU of its own, shares
//     them (lane_group_shares_cpus), and each of its lanes that waits then sleeps at once (lane_wait) rather than hold
//     the CPU a lane that hasn't arrived needs, in mwaitx or in the spin that takes its place without it: two lanes on one
//     CPU, which this thread's mask is cut to, read their group as sharing it, and the lane that waited for lane 0's
//     stall must have slept; two lanes on the process's own CPUs, where it has more than one, read theirs as not sharing
//     them.
//   - Teams: one group kept across operations with different active lane counts, with the same thread identities,
//     published inputs and completed outputs, no work on inactive lanes, and worker scratch released between runs, or
//     with lane_team_keep_scratch kept, emptied, and released when the team stops.
//     One-lane teams start no thread. Invalid and nested dispatches run nothing, including through lane_alone, and a
//     forked child refuses the inherited team without waiting for workers that exist only in its parent.
//   - Sessions (#905): sessions of 1, 2, 3 and 8 lanes, and lazy ones of 1, 3 and 8, each lane running the flow once,
//     lane 0 on the calling thread, with capacity - 1 threads created (LaneProcess.threads_created, and the process's
//     threads) and joined after; a session of one creates none. In the flow: every lane but lane 0 with the
//     asynchronous signals blocked and the synchronous ones not; a signal sent to the process handled on lane 0; parked
//     broadcasts from each lane; narrowings to 1, 2, 3, every lane and more, nested, each narrowed group syncing,
//     broadcasting and taking parts, the others untouched; a flow of rounds (LaneFlow) of 1, 3, every lane and more, of
//     two kinds, lane 0 running its part itself and the others theirs by kind, each round's lanes running it once, a
//     nested run in a round refused; a lane alone running lane_run on itself; a run, a team, a cold group and a session
//     of several refused where they would create a thread; and a child forked
//     from lane 0, which forgets the session and runs as one lane. Then sessions whose third lane fails to start
//     (LaneProcess.fail_at), eagerly and lazily, and a lazy one whose second does: the lanes created joined, the flow on
//     lane 0 alone; sessions of a lane per core at most (BUSTER_LANE_SESSION_PER_CORE), eagerly, lazily and lazily with
//     a flow of rounds that lane 0 begins before it settles, of 2 and 8 lanes, on test topologies (LaneProcess.topology)
//     of this process's CPUs in pairs, in fours and all one core: as many lanes as the capacity and the cores allow, each
//     round on them, each lane's lane_session_asked as many, a lazy session's lane 1 created though it runs on one,
//     where one core leaves it no other lane; and in a child, a session that leaves its lanes parked at its end (but in
//     sanitizer builds, which join them).
//   - Cores (#905): lane_cores on a test topology of SMT siblings n and n + 8 (Linux's numbering of AMD's), adjacent
//     pairs (Intel's), single threads (efficient cores), CPUs without a list, lists that don't parse (a dash without a
//     number, letters, nothing, a range backwards, a letter after it), that leave their CPU out or that the read's buffer
//     cuts, a newline, a CPU whose sibling's list doesn't parse (so that it must be skipped), siblings past the mask's
//     words and an empty mask, against counts worked out by hand; a topology that doesn't exist, every CPU a core; and
//     lane_core_count against /proc/cpuinfo's cores of this process's CPUs (lane_test_cpuinfo), where it lists them,
//     then with the process's affinity set to a core's two siblings, one, and to CPUs of two cores, two. Before them,
//     that oracle's reader on texts of /proc/cpuinfo's shape (lane_test_cpuinfo_controls): four processors' blocks with
//     long flags lines of names with digits, values at and past their bounds, malformed values and near-miss keys.
//
// A watchdog ends a group that makes no progress for BUSTER_LANE_TEST_TIMEOUT_SECONDS. Syncs may take longer when
// the lanes share CPUs, so completed rounds renew their watchdog; a stalled collective still times out. The other
// checks have a fixed deadline. The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the inputs (lane_test_random, lane_test_cost, lane_test_make_records and lane_test_output_byte),
// the arena checks (lane_test_arenas), the lanes' side of each check (lane_test_ranges, lane_test_syncs,
// lane_test_alone, lane_test_scratch, lane_test_exchange, lane_test_parts, lane_test_shared and lane_test_main, which
// runs them), their verification (lane_test_check_parts and the others), and run_lane_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define BUSTER_LANE_TEST_MAX_LANES 8
// Ranges: every count up to this, and the cost patterns of lane_test_cost.
#define BUSTER_LANE_TEST_MAX_RANGE 1000
#define BUSTER_LANE_TEST_COST_PATTERNS 7
// Syncs: rounds by default, and a late lane once every BUSTER_LANE_TEST_STALL_INTERVAL rounds. Its delay is in turn far
// past the BUSTER_LANE_WAIT_NANOSECONDS (1 ms) that the lanes waiting for it spend in mwaitx before they sleep,
// and well within them.
#define BUSTER_LANE_TEST_DEFAULT_ROUNDS 10000
#define BUSTER_LANE_TEST_STALL_INTERVAL 1000
#define BUSTER_LANE_TEST_STALL_NANOSECONDS 3000000
#define BUSTER_LANE_TEST_SHORT_STALL_NANOSECONDS 100000
// Exchange: the top bits of a record's hash pick its partition.
#define BUSTER_LANE_TEST_PARTITION_BITS 6
#define BUSTER_LANE_TEST_PARTITIONS (1u << BUSTER_LANE_TEST_PARTITION_BITS)
#define BUSTER_LANE_TEST_TIMEOUT_SECONDS 60
#define BUSTER_LANE_TEST_STRINGIFY_(value) #value
#define BUSTER_LANE_TEST_STRINGIFY(value) BUSTER_LANE_TEST_STRINGIFY_(value)
// The arenas: the large reservation, and how much of /proc/self/smaps is read.
#define BUSTER_LANE_TEST_LARGE_ARENA (64ull << 30)
#define BUSTER_LANE_TEST_SMAPS_BYTES (16ull << 20)
// 2^64 divided by the golden ratio: splitmix64's increment, and the multiplier that turns --seed N into a state.
#define BUSTER_LANE_TEST_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
// Each check prints its first problems, up to this many.
#define BUSTER_LANE_TEST_REPORTED 3
#define BUSTER_LANE_TEST_TEAM_ROUNDS 256
// Advance: each advancing lane's advances, and after how many of them each pauses, long enough
// (BUSTER_LANE_TEST_STALL_NANOSECONDS) that lane 0, waiting for the word, sleeps on its futex.
#define BUSTER_LANE_TEST_ADVANCES 20000
#define BUSTER_LANE_TEST_ADVANCE_PAUSE_INTERVAL 5000
#define BUSTER_LANE_TEST_TEAM_PARTS 13

static const uint32_t lane_test_counts[] = { 1, 2, 3, 8 };
static const uint64_t lane_test_record_counts[] = { 0, 1, 2, 3, 63, 64, 65, 1000, 100000 };
// range_part's checks: these part counts, beyond the lane counts, and these item counts, with every cost pattern.
static const uint64_t lane_test_part_counts[] = { 64, 512, 1001 };
static const uint64_t lane_test_part_range_counts[] = { 0, 1, 2, 3, 5, 31, 63, 64, 65, 100, 255, 256, 257, 511, 512, 513, 999, 1000 };

// The parts check's inputs: an item count and the cost pattern of lane_test_cost. Pattern 5's costs add up past 2^64
// beyond BUSTER_LANE_TEST_MAX_RANGE items.
typedef struct LaneTestPartInput LaneTestPartInput;
struct LaneTestPartInput
{
    uint64_t count;
    uint32_t pattern;
};

static const LaneTestPartInput lane_test_part_inputs[] = {
    {
        .count = 0,
        .pattern = 0,
    },
    {
        .count = 1,
        .pattern = 1,
    },
    {
        .count = 2,
        .pattern = 6,
    },
    {
        .count = 3,
        .pattern = 3,
    },
    {
        .count = 63,
        .pattern = 1,
    },
    {
        .count = 64,
        .pattern = 4,
    },
    {
        .count = 65,
        .pattern = 0,
    },
    {
        .count = 1000,
        .pattern = 2,
    },
    {
        .count = 1000,
        .pattern = 5,
    },
    {
        .count = 4097,
        .pattern = 6,
    },
    {
        .count = 100000,
        .pattern = 1,
    },
    {
        .count = 100000,
        .pattern = 3,
    },
    {
        .count = 100,
        .pattern = 7,
    },
};

#define BUSTER_LANE_TEST_PART_INPUT_COUNT (sizeof(lane_test_part_inputs) / sizeof(lane_test_part_inputs[0]))

typedef enum LaneTestCheck
{
    LANE_TEST_RANGES,
    LANE_TEST_SYNCS,
    LANE_TEST_ALONE,
    LANE_TEST_SCRATCH,
    LANE_TEST_EXCHANGE,
    LANE_TEST_PARTS,
    LANE_TEST_ADVANCE,
    LANE_TEST_SHARED,
} LaneTestCheck;

static const char* const lane_test_check_names[] = {
    "ranges",
    "syncs",
    "alone",
    "scratch",
    "exchange",
    "parts",
    "advance",
    "shared CPUs",
};

typedef struct LaneTestRecord LaneTestRecord;
struct LaneTestRecord
{
    uint64_t hash;
    uint32_t input;
    // The lowest wins, as a strong definition beats a weak one.
    uint32_t precedence;
};

// What a check's lanes share.
typedef struct LaneTest LaneTest;
struct LaneTest
{
    LaneTestCheck check;
    // Each lane's failures and futex sleeps in the check, each written by its lane.
    uint64_t failures[BUSTER_LANE_TEST_MAX_LANES];
    uint64_t sleeps[BUSTER_LANE_TEST_MAX_LANES];
    // Ranges: each pattern's inclusive cost prefix, and every lane's part of every count, lane_range's first and then each
    // pattern's (lane_test_part).
    const uint64_t* cost_prefix[BUSTER_LANE_TEST_COST_PATTERNS];
    Range64* parts;
    // Syncs: the rounds, and each lane's slot, double-buffered by the round's parity.
    uint64_t rounds;
    uint64_t slots[2][BUSTER_LANE_TEST_MAX_LANES];
    // Scratch: the bases of each lane's scratch arenas.
    const uint8_t* scratch_bases[BUSTER_LANE_TEST_MAX_LANES][BUSTER_SCRATCH_ARENA_COUNT];
    // Exchange: the records, each lane's counts per partition (row by lane), the partitioned records, and each input's
    // winner.
    const LaneTestRecord* records;
    uint64_t record_count;
    uint32_t counts[BUSTER_LANE_TEST_MAX_LANES * BUSTER_LANE_TEST_PARTITIONS];
    LaneTestRecord* exchanged;
    uint32_t* winners;
    // Parts: each input's cost prefix and its reference output, one after another (part_prefix_offsets and
    // part_output_offsets say where each starts); the lanes' output, the boundaries, each part's bytes and the lane that
    // took it (plus one), each lane's count of parts taken and its order, for lane 0 to check.
    const uint64_t* part_prefixes;
    const uint8_t* part_references;
    uint64_t part_prefix_offsets[BUSTER_LANE_TEST_PART_INPUT_COUNT];
    uint64_t part_output_offsets[BUSTER_LANE_TEST_PART_INPUT_COUNT + 1];
    uint8_t* part_output;
    uint64_t* part_boundaries;
    uint64_t* part_bytes;
    uint32_t* part_takers;
    uint64_t part_taken[BUSTER_LANE_TEST_MAX_LANES];
    const uint32_t* part_orders[BUSTER_LANE_TEST_MAX_LANES];
    // Advance: the word the lanes advance (lane_advance) and lane 0 waits on.
    _Alignas(64) uint32_t advanced;
    // Shared CPUs: whether each lane read its group as one that shares the process's CPUs (LaneGroup.shares_cpus).
    bool shares[BUSTER_LANE_TEST_MAX_LANES];
};

// ---------------------------------------------------------------------------------------------------------------------
// Inputs

// splitmix64's finalizer, which also turns a record's key into its hash.
static uint64_t lane_test_mix(uint64_t x)
{
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// splitmix64.
static uint64_t lane_test_random(uint64_t* state)
{
    *state += BUSTER_LANE_TEST_GOLDEN_RATIO;
    return lane_test_mix(*state);
}

// Item i's cost in a pattern, each skewed its own way.
static uint64_t lane_test_cost(uint64_t* state, uint32_t pattern, uint64_t i)
{
    uint64_t result = 0;
    switch (pattern)
    {
        // Small and uniform, zero included.
        case 0: result = lane_test_random(state) % 10; break;
        // Heavy-tailed: powers of two up to 2^23.
        case 1: result = 1ull << (lane_test_random(state) % 24); break;
        // One item outweighs all the others together.
        case 2: result = i == BUSTER_LANE_TEST_MAX_RANGE / 2 ? 1000000000 : 1; break;
        // Mostly free, with a costly item now and then.
        case 3: result = lane_test_random(state) % 50 == 0 ? 1000 : 0; break;
        // Growing: the last items cost the most.
        case 4: result = i * i; break;
        // So large that the boundary's arithmetic needs more than 64 bits: the total approaches 2^63.
        case 5: result = (1ull << 52) + lane_test_random(state) % (1ull << 52); break;
        // One bucket, growing: the costliest part is the last of its bucket, and goes first.
        case 7: result = (1ull << 20) + i; break;
        // No cost at all, which lane_range_by_cost splits as lane_range does.
        default: result = 0; break;
    }
    return result;
}

// Records whose keys repeat about four times each, with random precedences.
static void lane_test_make_records(uint64_t* state, LaneTestRecord* records, uint64_t count)
{
    const uint64_t keys = count / 4 + 1;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint64_t key = lane_test_random(state) % keys;
        records[i] = (LaneTestRecord){
            .hash = lane_test_mix(key + 1),
            .input = (uint32_t)i,
            .precedence = (uint32_t)(lane_test_random(state) % 5),
        };
    }
}

// How many bytes of output item `item` of the parts check makes (0 to 4), and the value of its byte `byte`.
static uint64_t lane_test_output_bytes(uint64_t item)
{
    return lane_test_mix(item * 3 + 1) % 5;
}

static uint8_t lane_test_output_byte(uint64_t item, uint64_t byte)
{
    return (uint8_t)lane_test_mix(item * 5 + byte + 7);
}

// ---------------------------------------------------------------------------------------------------------------------
// Arenas

typedef struct LaneTestTally LaneTestTally;
struct LaneTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static void lane_test_expect(LaneTestTally* tally, bool condition, const char* what)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        printf("  arenas: %s\n", what);
    }
}

// Lowercase hexadecimal digits from `text`, as /proc/self/smaps writes addresses. Returns the end of the digits.
static const char* lane_test_parse_hex(const char* text, const char* end, uint64_t* value)
{
    uint64_t result = 0;
    const char* p = text;
    while (p < end && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
    {
        result = result * 16 + (uint64_t)(*p <= '9' ? *p - '0' : *p - 'a' + 10);
        p += 1;
    }
    *value = result;
    return p;
}

// Whether the VmFlags of the mapping that holds `address`, in /proc/self/smaps, include `flag` (two letters: "hg" for
// MADV_HUGEPAGE, "nh" for MADV_NOHUGEPAGE). Sets *found if the mapping and its flags were found.
static bool lane_test_vm_flag(uintptr_t address, const char* flag, bool* found)
{
    bool result = false;
    *found = false;
    Scratch scratch = scratch_begin(0);
    char* buffer = arena_push(scratch.arena, BUSTER_LANE_TEST_SMAPS_BYTES, 1);
    const int fd = open("/proc/self/smaps", O_RDONLY | O_CLOEXEC);
    uint64_t length = 0;
    bool reading = buffer != 0 && fd >= 0;
    while (reading)
    {
        const ssize_t got = read(fd, buffer + length, BUSTER_LANE_TEST_SMAPS_BYTES - length);
        length += got > 0 ? (uint64_t)got : 0;
        reading = got > 0 && length < BUSTER_LANE_TEST_SMAPS_BYTES;
    }
    // A mapping's first line is "begin-end perms ...", and its VmFlags line is "VmFlags: rd wr mr mw me ac nh".
    const char* end = buffer + length;
    const char* line = buffer;
    bool inside = false;
    while (buffer != 0 && line < end && !*found)
    {
        const char* newline = memchr(line, '\n', (size_t)(end - line));
        const char* line_end = newline != 0 ? newline : end;
        uint64_t begin = 0;
        uint64_t stop = 0;
        const char* dash = lane_test_parse_hex(line, line_end, &begin);
        const bool header = dash > line && dash < line_end && *dash == '-' && lane_test_parse_hex(dash + 1, line_end, &stop) > dash + 1;
        if (header)
        {
            inside = begin <= address && address < stop;
        }
        else if (inside && line_end - line >= 8 && memcmp(line, "VmFlags:", 8) == 0)
        {
            *found = true;
            for (const char* p = line + 8; p + 2 <= line_end; p += 1)
            {
                result = result || (p[-1] == ' ' && p[0] == flag[0] && p[1] == flag[1] && (p + 2 == line_end || p[2] == ' '));
            }
        }
        line = line_end + 1;
    }
    if (fd >= 0)
    {
        close(fd);
    }
    scratch_end(scratch);
    return result;
}

// Whether the page at `address` is resident.
static bool lane_test_resident(const uint8_t* address)
{
    unsigned char vector = 0;
    return mincore((void*)address, BUSTER_ARENA_PAGE_BYTES, &vector) == 0 && (vector & 1) != 0;
}

static bool lane_test_arenas(void)
{
    LaneTestTally tally = { 0 };

    // Every power-of-two alignment, each after a 1-byte push that leaves the arena misaligned.
    Arena arena = arena_reserve(64ull << 20);
    lane_test_expect(&tally, arena.base != 0 && arena.capacity == 64ull << 20, "a 64 MB arena couldn't be reserved");
    for (uint64_t alignment = 1; arena.base != 0 && alignment <= BUSTER_ARENA_HUGE_PAGE_BYTES; alignment *= 2)
    {
        arena_pop_to(&arena, 0);
        const uint8_t* first = arena_push(&arena, 1, 1);
        const uint8_t* aligned = arena_push(&arena, 100, alignment);
        const uintptr_t expected = ((uintptr_t)arena.base + 1 + alignment - 1) & ~(alignment - 1);
        lane_test_expect(&tally, first == arena.base && (uintptr_t)aligned == expected && arena.length == expected - (uintptr_t)arena.base + 100,
                         "a push isn't at the first aligned address after the last");
    }

    // Invalid alignments fail and leave the arena as it was.
    static const uint64_t invalid_alignments[] = { 0, 3, 12, 48, 1000, (1ull << 20) + 1 };
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_alignments); i += 1)
    {
        const uint64_t length = arena.length;
        lane_test_expect(&tally, arena_push(&arena, 1, invalid_alignments[i]) == 0 && arena.length == length, "an alignment that isn't a power of two was accepted");
    }
    arena_release(&arena);
    lane_test_expect(&tally, arena.base == 0 && arena.capacity == 0 && arena.length == 0, "a released arena keeps its memory");

    // Filled exactly, then one more byte, which fails without changing the arena.
    const uint64_t small_capacity = 3 * BUSTER_ARENA_PAGE_BYTES + 100;
    Arena small = arena_reserve(small_capacity);
    uint8_t* all = arena_push(&small, small_capacity, 1);
    lane_test_expect(&tally, small.capacity == small_capacity && all == small.base && small.length == small_capacity, "a push of the whole capacity failed");
    lane_test_expect(&tally, arena_push(&small, 1, 1) == 0 && small.length == small_capacity, "a push past the capacity was accepted");
    lane_test_expect(&tally, arena_push(&small, 0, 1) == small.base + small_capacity && small.length == small_capacity, "an empty push at the end failed");
    if (all != 0)
    {
        // Every byte of the reservation can be written.
        memset(all, 0xab, small_capacity);
        lane_test_expect(&tally, all[0] == 0xab && all[small_capacity - 1] == 0xab, "the arena's bytes can't be written");
    }

    // Sizes and counts whose arithmetic overflows fail.
    arena_pop_to(&small, 10);
    lane_test_expect(&tally, arena_push(&small, UINT64_MAX, 1) == 0 && arena_push(&small, UINT64_MAX - 5, 1) == 0 && small.length == 10, "an enormous push was accepted");
    lane_test_expect(&tally, arena_push(&small, small_capacity - 9, 1) == 0 && small.length == 10, "a push one byte too large was accepted");
    lane_test_expect(&tally, arena_push(&small, 16, 1ull << 63) == 0 && small.length == 10, "a push aligned past the address space was accepted");
    lane_test_expect(&tally, arena_push_array(&small, 1ull << 62, 8, 8) == 0 && arena_push_array(&small, UINT64_MAX, UINT64_MAX, 1) == 0 && small.length == 10,
                     "an array whose size overflows was accepted");
    const uint64_t* words = BUSTER_ARENA_PUSH_ARRAY(&small, uint64_t, (small_capacity - 16) / 8);
    lane_test_expect(&tally, words == (const uint64_t*)(small.base + 16) && small.length == 16 + (small_capacity - 16) / 8 * 8, "an array that fits was refused");

    // A measuring arena takes, push for push, what an arena reserved at a page boundary takes, from a base past the user's
    // addresses, and refuses what the capacity doesn't hold.
    Arena measure = arena_measure();
    Arena reserved = arena_reserve_small(8ull << 20, 8ull << 20);
    bool alike = reserved.base != 0 && ((uintptr_t)reserved.base & (BUSTER_ARENA_PAGE_BYTES - 1)) == 0;
    uint64_t random = 0x9e3779b97f4a7c15ull;
    for (uint32_t i = 0; alike && i < 1000; i += 1)
    {
        random = random * 6364136223846793005ull + 1442695040888963407ull;
        const uint64_t size = (random >> 33) % 3000;
        const uint64_t alignment = 1ull << ((random >> 20) % 13);
        const uint8_t* counted = arena_push(&measure, size, alignment);
        const uint8_t* held = arena_push(&reserved, size, alignment);
        // As integers: a measuring arena's addresses are in no object, so subtracting them as pointers is undefined.
        alike = counted != 0 && held != 0 && measure.length == reserved.length &&
                (uintptr_t)counted - (uintptr_t)measure.base == (uintptr_t)held - (uintptr_t)reserved.base;
    }
    lane_test_expect(&tally, alike, "a measuring arena's pushes take other room than a reserved arena's");
    lane_test_expect(&tally, (uintptr_t)measure.base >= BUSTER_ARENA_MAX_CAPACITY && arena_push(&measure, BUSTER_ARENA_MAX_CAPACITY, 1) == 0,
                     "a measuring arena's base is a user's address, or it takes more than its capacity");
    arena_release(&reserved);

    // pop_to frees what was pushed since, and the next push gets the same memory.
    arena_pop_to(&small, 10);
    const uint8_t* again = arena_push(&small, 100, 1);
    lane_test_expect(&tally, again == small.base + 10, "a push after pop_to doesn't reuse the memory");
    bool found = false;
    lane_test_expect(&tally, lane_test_vm_flag((uintptr_t)small.base, "nh", &found) && found, "a small arena isn't kept in base pages");
    arena_release(&small);

    // Arenas that couldn't be reserved have no capacity, and every push fails.
    Arena empty = arena_reserve(0);
    Arena enormous = arena_reserve(1ull << 62);
    lane_test_expect(&tally, empty.base == 0 && empty.capacity == 0 && enormous.base == 0 && enormous.capacity == 0, "an impossible reservation succeeded");
    lane_test_expect(&tally, arena_push(&empty, 1, 1) == 0 && arena_push(&enormous, 1, 1) == 0 && BUSTER_ARENA_PUSH_ARRAY(&empty, uint32_t, 1) == 0,
                     "a push into an arena without capacity was accepted");
    arena_release(&empty);
    arena_release(&enormous);

    // A large reservation: only touched pages are committed, base pages up to base + BUSTER_ARENA_SMALL_BYTES and huge
    // pages from there.
    Arena large = arena_reserve(BUSTER_LANE_TEST_LARGE_ARENA);
    lane_test_expect(&tally, large.base != 0 && large.capacity == BUSTER_LANE_TEST_LARGE_ARENA, "a 64 GB arena couldn't be reserved");
    if (large.base != 0)
    {
        uint8_t* first = arena_push(&large, 1, 1);
        lane_test_expect(&tally, first != 0, "a large arena could not commit its first page");
        if (first != 0)
        {
            first[0] = 1;
        }
        lane_test_expect(&tally, lane_test_resident(large.base), "a touched page isn't committed");
        lane_test_expect(&tally, !lane_test_resident(large.base + BUSTER_ARENA_SMALL_BYTES - BUSTER_ARENA_PAGE_BYTES), "the first touch committed more than its page");
        lane_test_expect(&tally, !lane_test_resident(large.base + (BUSTER_LANE_TEST_LARGE_ARENA / 2)), "an untouched page is committed");
        lane_test_expect(&tally, ((uintptr_t)large.base + BUSTER_ARENA_SMALL_BYTES) % BUSTER_ARENA_HUGE_PAGE_BYTES == 0, "the huge pages don't start on a huge page boundary");
        const bool head = lane_test_vm_flag((uintptr_t)large.base, "nh", &found) && found;
        const bool head_end = lane_test_vm_flag((uintptr_t)large.base + BUSTER_ARENA_SMALL_BYTES - 1, "nh", &found) && found;
        const bool head_huge = lane_test_vm_flag((uintptr_t)large.base, "hg", &found);
        lane_test_expect(&tally, head && head_end && !head_huge, "the start of an arena isn't kept in base pages");
        const bool tail = lane_test_vm_flag((uintptr_t)large.base + BUSTER_ARENA_SMALL_BYTES, "hg", &found) && found;
        const bool tail_end = lane_test_vm_flag((uintptr_t)large.base + BUSTER_LANE_TEST_LARGE_ARENA - 1, "hg", &found) && found;
        lane_test_expect(&tally, tail && tail_end, "an arena doesn't ask for huge pages past its start");
    }
    arena_release(&large);

    printf("arenas: %lu checks, %lu failed\n", (unsigned long)tally.checks, (unsigned long)tally.failures);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The lanes' side of the checks

// Where lane `lane` of `lanes` writes its part of `count` items, for lane_range (pattern -1) or a cost pattern.
static uint64_t lane_test_part(int32_t pattern, uint64_t count, uint64_t lane, uint64_t lanes)
{
    return ((uint64_t)(pattern + 1) * (BUSTER_LANE_TEST_MAX_RANGE + 1) + count) * lanes + lane;
}

static void lane_test_ranges(LaneTest* test)
{
    const uint64_t lanes = lane_count();
    const uint64_t index = lane_index();
    for (uint64_t count = 0; count <= BUSTER_LANE_TEST_MAX_RANGE; count += 1)
    {
        test->parts[lane_test_part(-1, count, index, lanes)] = lane_range(count);
        for (int32_t pattern = 0; pattern < BUSTER_LANE_TEST_COST_PATTERNS; pattern += 1)
        {
            test->parts[lane_test_part(pattern, count, index, lanes)] = lane_range_by_cost(test->cost_prefix[pattern], count);
        }
    }
}

// The value lane `lane` writes in round `round`: different for every round and lane.
static uint64_t lane_test_value(uint64_t round, uint64_t lane)
{
    return lane_test_mix(round * BUSTER_LANE_TEST_MAX_LANES + lane + 1);
}

static void lane_test_stall(uint64_t nanoseconds)
{
    const struct timespec duration = {
        .tv_sec = 0,
        .tv_nsec = (long)nanoseconds,
    };
    nanosleep(&duration, 0);
}

// Written by lane 0 only after a completed sync and broadcast; sampled by the watchdog's signal handler. The word
// uses lock-free machine atomics so the handler can exchange it safely even if it interrupts a lane's update.
static _Alignas(64) uint32_t lane_test_progress;

static uint64_t lane_test_syncs(LaneTest* test)
{
    const uint32_t lanes = lane_count();
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    for (uint64_t round = 0; round < test->rounds; round += 1)
    {
        // A slot is written again two rounds later, after the broadcast in between has made sure that every lane has
        // read it. The stalls take turns: long to the sync, long to the broadcast, short to the sync, short to the
        // broadcast.
        const uint64_t stall = round / BUSTER_LANE_TEST_STALL_INTERVAL;
        const bool late = lanes > 1 && round % BUSTER_LANE_TEST_STALL_INTERVAL == BUSTER_LANE_TEST_STALL_INTERVAL / 2 && index == stall % lanes;
        const uint64_t stall_nanoseconds = stall % 4 < 2 ? BUSTER_LANE_TEST_STALL_NANOSECONDS : BUSTER_LANE_TEST_SHORT_STALL_NANOSECONDS;
        if (late && stall % 2 == 0)
        {
            lane_test_stall(stall_nanoseconds);
        }
        test->slots[round % 2][index] = lane_test_value(round, index);
        lane_sync();
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            failures += test->slots[round % 2][lane] != lane_test_value(round, lane) ? 1 : 0;
        }
        if (late && stall % 2 == 1)
        {
            lane_test_stall(stall_nanoseconds);
        }
        // The other lanes offer values of their own, which the broadcast must ignore.
        const uint32_t source = (uint32_t)(round % lanes);
        const uint64_t expected = ~lane_test_value(round, source);
        failures += lane_broadcast(index == source ? expected : lane_test_value(round, index), source) != expected ? 1 : 0;
        if (index == 0)
        {
            __atomic_store_n(&lane_test_progress, 1, __ATOMIC_RELAXED);
        }
    }
    // Broadcasts back to back, with nothing between them but the next broadcast: each source offers its value while
    // slower lanes may still be reading the last one.
    for (uint64_t round = 0; round < test->rounds; round += 1)
    {
        const uint32_t source = (uint32_t)((round * 7 + 3) % lanes);
        failures += lane_broadcast(lane_test_value(round, index), source) != lane_test_value(round, source) ? 1 : 0;
        if (index == 0)
        {
            __atomic_store_n(&lane_test_progress, 1, __ATOMIC_RELAXED);
        }
    }
    // Narrow tasks spread over the lanes, each result broadcast from the lane that ran its task.
    for (uint64_t task = 0; task < 3 * BUSTER_LANE_TEST_MAX_LANES; task += 1)
    {
        const uint32_t owner = lane_from_task_index(task);
        const uint64_t result = index == owner ? lane_test_value(task, owner) : 0;
        failures += owner != task % lanes || lane_broadcast(result, owner) != lane_test_value(task, owner) ? 1 : 0;
    }
    return failures;
}

// The shared-CPU check's lanes: each reads its group's flag, and lane 0 stalls before their sync, so that every other lane
// waits for it (lane_wait), long enough that a lane of a group that shares CPUs must have slept.
static uint64_t lane_test_shared(LaneTest* test)
{
    const uint32_t index = lane_index();
    const Lane lane = thread_context.lane;
    test->shares[index] = lane.group != 0 && lane.group->shares_cpus;
    if (index == 0 && lane.count > 1)
    {
        lane_test_stall(BUSTER_LANE_TEST_SHORT_STALL_NANOSECONDS);
    }
    lane_sync();
    return 0;
}

static uint64_t lane_test_alone(void)
{
    const uint32_t lanes = lane_count();
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    if (index == lanes - 1)
    {
        const Lane group = lane_alone();
        lane_sync();
        const Range64 range = lane_range(10);
        const uint64_t cost_prefix[3] = { 1, 5, 6 };
        const Range64 by_cost = lane_range_by_cost(cost_prefix, 3);
        failures += lane_index() != 0 || lane_count() != 1 || range.begin != 0 || range.end != 10 || by_cost.begin != 0 || by_cost.end != 3 ? 1 : 0;
        failures += lane_broadcast(12345, 0) != 12345 ? 1 : 0;
        // A lane of one takes from its own counter, which resets itself after each loop: twice 0 to 4, then 5.
        for (uint32_t loop = 0; loop < 2; loop += 1)
        {
            uint64_t taken = 0;
            uint64_t next = lane_take(5);
            while (next < 5)
            {
                failures += next != taken ? 1 : 0;
                taken += 1;
                next = lane_take(5);
            }
            failures += taken != 5 || next != 5 ? 1 : 0;
        }
        uint64_t boundaries[2] = { 0 };
        uint32_t order[1] = { 1 };
        const uint64_t parts = lane_parts_by_cost(cost_prefix, 3, boundaries, order);
        failures += parts != 1 || boundaries[0] != 0 || boundaries[1] != 3 || order[0] != 0 || lane_take(parts) != 0 || lane_take(parts) != 1 ? 1 : 0;
        lane_restore(group);
        failures += lane_index() != index || lane_count() != lanes ? 1 : 0;
    }
    // Had the lone lane arrived at the group's syncs, these would get the wrong values or never finish.
    for (uint64_t round = 0; round < 100; round += 1)
    {
        const uint32_t source = (uint32_t)(round % lanes);
        failures += lane_broadcast(lane_test_value(round, index), source) != lane_test_value(round, source) ? 1 : 0;
    }
    return failures;
}

// Advance: the lanes but lane 0 (all of them, with one) each advance one word BUSTER_LANE_TEST_ADVANCES times with
// lane_advance, pausing together now and then, while lane 0 waits on it with lane_wait until it holds every advance: each
// value lane 0 sees must be even and larger than the one before, and none may be lost.
static uint64_t lane_test_advance(LaneTest* test)
{
    const uint32_t lanes = lane_count();
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    if (index == 0)
    {
        test->advanced = 0;
    }
    lane_sync();
    const uint32_t total = 2 * BUSTER_LANE_TEST_ADVANCES * (lanes > 1 ? lanes - 1 : 1);
    for (uint32_t a = 0; (lanes == 1 || index != 0) && a < BUSTER_LANE_TEST_ADVANCES; a += 1)
    {
        lane_advance(&test->advanced);
        if (a % BUSTER_LANE_TEST_ADVANCE_PAUSE_INTERVAL == BUSTER_LANE_TEST_ADVANCE_PAUSE_INTERVAL - 1)
        {
            const struct timespec pause = { .tv_sec = 0, .tv_nsec = BUSTER_LANE_TEST_STALL_NANOSECONDS };
            nanosleep(&pause, NULL);
        }
    }
    uint32_t seen = __atomic_load_n(&test->advanced, __ATOMIC_ACQUIRE) & ~1u;
    while (index == 0 && seen != total && failures == 0)
    {
        const uint32_t next = lane_wait(&test->advanced, seen);
        failures += (next & 1u) != 0 || next <= seen || next > total ? 1 : 0;
        seen = next;
    }
    lane_sync();
    failures += (__atomic_load_n(&test->advanced, __ATOMIC_ACQUIRE) & ~1u) != total ? 1 : 0;
    return failures;
}

static uint64_t lane_test_scratch(LaneTest* test)
{
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    Scratch outer = scratch_begin(0);
    uint8_t* a = arena_push(outer.arena, 1000, 64);
    Scratch inner = scratch_begin(outer.arena);
    uint8_t* b = arena_push(inner.arena, 3000, 4096);
    failures += a == 0 || b == 0 || inner.arena == outer.arena ? 1 : 0;
    if (a != 0 && b != 0)
    {
        memset(a, (int)index, 1000);
        memset(b, (int)index + 1, 3000);
        failures += a[999] != (uint8_t)index || b[2999] != (uint8_t)(index + 1) ? 1 : 0;
    }
    test->scratch_bases[index][0] = outer.arena->base;
    test->scratch_bases[index][1] = inner.arena->base;
    scratch_end(inner);
    failures += inner.arena->length != inner.length ? 1 : 0;
    // A conflict with the inner arena gets the outer one, whose next push follows a's.
    Scratch again = scratch_begin(inner.arena);
    const uint8_t* c = arena_push(again.arena, 1, 1);
    failures += again.arena != outer.arena || c != a + 1000 ? 1 : 0;
    scratch_end(again);
    scratch_end(outer);
    failures += outer.arena->length != outer.length ? 1 : 0;
    // A lane's thread releases its arenas when it ends, so no lane may end before every lane has reserved its own.
    lane_sync();
    return failures;
}

// The partition of a hash: its top bits. The tables use its low bits.
static uint32_t lane_test_partition(uint64_t hash)
{
    return (uint32_t)(hash >> (64 - BUSTER_LANE_TEST_PARTITION_BITS));
}

// The slot of `hash` in an open-addressing table of `capacity` entries (a power of two), each 0 for an empty slot or 1
// plus the position in `records` of its key's winner so far: the slot that holds the hash, or the empty one it goes in.
static uint64_t lane_test_find(const uint32_t* table, uint64_t capacity, const LaneTestRecord* records, uint64_t hash)
{
    uint64_t slot = hash & (capacity - 1);
    while (table[slot] != 0 && records[table[slot] - 1].hash != hash)
    {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

// Resolves `count` records in input order: each key's winner is its record of lowest precedence, the earliest of them on
// a tie. Writes winners[input] for each record, whose inputs are below winner_count unless a broken sync let records go
// missing. Returns false if the table doesn't fit in scratch memory or an input is out of range.
static bool lane_test_resolve(const LaneTestRecord* records, uint64_t count, uint32_t* winners, uint64_t winner_count)
{
    bool result = true;
    Scratch scratch = scratch_begin(0);
    uint64_t capacity = 16;
    while (capacity < 2 * count)
    {
        capacity *= 2;
    }
    uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, capacity);
    if (table != 0)
    {
        memset(table, 0, capacity * sizeof(uint32_t));
        for (uint64_t i = 0; i < count; i += 1)
        {
            const uint64_t slot = lane_test_find(table, capacity, records, records[i].hash);
            if (table[slot] == 0 || records[i].precedence < records[table[slot] - 1].precedence)
            {
                table[slot] = (uint32_t)(i + 1);
            }
        }
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (records[i].input < winner_count)
            {
                winners[records[i].input] = records[table[lane_test_find(table, capacity, records, records[i].hash)] - 1].input;
            }
            result = result && records[i].input < winner_count;
        }
    }
    scratch_end(scratch);
    return result && table != 0;
}

// The design's exchange (docs/object-writer-and-linker.md, "Lanes", rule 4): count, place, fill, then resolve by
// partition.
static uint64_t lane_test_exchange(LaneTest* test)
{
    const uint32_t lanes = lane_count();
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    // Count this lane's records per partition.
    const Range64 records = lane_range(test->record_count);
    uint32_t* counts = test->counts + index * BUSTER_LANE_TEST_PARTITIONS;
    memset(counts, 0, BUSTER_LANE_TEST_PARTITIONS * sizeof(uint32_t));
    for (uint64_t i = records.begin; i < records.end; i += 1)
    {
        counts[lane_test_partition(test->records[i].hash)] += 1;
    }
    lane_sync();
    // Place: every lane works out for itself where each partition starts, and where its own segment of each starts,
    // after the earlier partitions and after the earlier lanes' segments of the same partition.
    uint64_t partition_begin[BUSTER_LANE_TEST_PARTITIONS + 1];
    uint64_t next[BUSTER_LANE_TEST_PARTITIONS];
    uint64_t offset = 0;
    for (uint32_t partition = 0; partition < BUSTER_LANE_TEST_PARTITIONS; partition += 1)
    {
        partition_begin[partition] = offset;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            if (lane == index)
            {
                next[partition] = offset;
            }
            offset += test->counts[lane * BUSTER_LANE_TEST_PARTITIONS + partition];
        }
    }
    partition_begin[BUSTER_LANE_TEST_PARTITIONS] = offset;
    // Fill this lane's segments with its records, in input order. Had a sync let a lane read counts that weren't all
    // written, the places could be anywhere: they are checked, so that the check fails rather than crashes.
    for (uint64_t i = records.begin; i < records.end; i += 1)
    {
        const LaneTestRecord record = test->records[i];
        const uint32_t partition = lane_test_partition(record.hash);
        if (next[partition] < test->record_count)
        {
            test->exchanged[next[partition]] = record;
        }
        failures += next[partition] < test->record_count ? 0 : 1;
        next[partition] += 1;
    }
    lane_sync();
    // Resolve a range of partitions, whose records come from every lane in input order.
    const Range64 partitions = lane_range(BUSTER_LANE_TEST_PARTITIONS);
    const bool placed = offset == test->record_count;
    for (uint64_t partition = partitions.begin; placed && partition < partitions.end; partition += 1)
    {
        const uint64_t begin = partition_begin[partition];
        failures += lane_test_resolve(test->exchanged + begin, partition_begin[partition + 1] - begin, test->winners, test->record_count) ? 0 : 1;
    }
    failures += placed ? 0 : 1;
    return failures;
}

// The parts check's cost of a part, and its bucket: the leading zeros of its cost, 64 without one.
static uint64_t lane_test_part_cost(const uint64_t* cost_prefix, const uint64_t* boundaries, uint64_t part)
{
    const uint64_t begin = boundaries[part];
    const uint64_t end = boundaries[part + 1];
    return end == begin ? 0 : cost_prefix[end - 1] - (begin == 0 ? 0 : cost_prefix[begin - 1]);
}

static uint32_t lane_test_cost_bucket(const uint64_t* cost_prefix, const uint64_t* boundaries, uint64_t part)
{
    const uint64_t cost = lane_test_part_cost(cost_prefix, boundaries, part);
    return cost == 0 ? 64 : (uint32_t)__builtin_clzll(cost);
}

// An order's failures: the parts that exist, the costliest first (the first of them on a tie), then the others by
// bucket, then by index.
static uint64_t lane_test_order_failures(const uint64_t* cost_prefix, const uint64_t* boundaries, uint64_t parts, const uint32_t* order)
{
    uint64_t failures = 0;
    bool in_range = true;
    uint64_t costliest = 0;
    for (uint64_t taken_index = 0; taken_index < parts; taken_index += 1)
    {
        in_range = in_range && order[taken_index] < parts;
        const uint64_t cost = lane_test_part_cost(cost_prefix, boundaries, taken_index);
        costliest = cost > lane_test_part_cost(cost_prefix, boundaries, costliest) ? taken_index : costliest;
    }
    failures += in_range && (parts == 0 || order[0] == costliest) ? 0 : 1;
    for (uint64_t taken_index = 2; in_range && taken_index < parts; taken_index += 1)
    {
        const uint32_t previous = lane_test_cost_bucket(cost_prefix, boundaries, order[taken_index - 1]);
        const uint32_t current = lane_test_cost_bucket(cost_prefix, boundaries, order[taken_index]);
        failures += previous > current || (previous == current && order[taken_index - 1] >= order[taken_index]) ? 1 : 0;
    }
    return failures;
}

// parts_by_cost's checks of one input, by lane 0 alone: for the group's lane count, the group's boundaries and order
// (`boundaries` and `order`, from lane_parts_by_cost); and for others, as many parts as a group of them would cut (of
// BUSTER_LANE_MAX_COUNT lanes for more), range_part_by_cost's boundaries, and an order of every part once by
// lane_parts_by_cost's rules.
static uint64_t lane_test_check_parts_alone(const uint64_t* cost_prefix, uint64_t count, uint64_t parts, const uint64_t* boundaries, const uint32_t* order)
{
    uint64_t failures = 0;
    Scratch scratch = scratch_begin(0);
    uint64_t* alone_boundaries = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_LANE_MAX_PARTS + 1);
    uint32_t* alone_order = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_LANE_MAX_PARTS);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, BUSTER_LANE_MAX_PARTS);
    const uint32_t lane_counts[] = { lane_count(), 1, 2, 3, 5, 8, 64, BUSTER_LANE_MAX_COUNT, BUSTER_LANE_MAX_COUNT + 1 };
    for (uint32_t l = 0; alone_boundaries != 0 && alone_order != 0 && seen != 0 && l < BUSTER_ARRAY_LENGTH(lane_counts); l += 1)
    {
        const uint64_t lanes = lane_counts[l] < BUSTER_LANE_MAX_COUNT ? lane_counts[l] : BUSTER_LANE_MAX_COUNT;
        const uint64_t most = lanes == 1 ? 1 : BUSTER_LANE_PARTS_PER_LANE * lanes;
        const uint64_t expected = count < most ? count : most;
        const uint64_t made = parts_by_cost(cost_prefix, count, lane_counts[l], alone_boundaries, alone_order);
        failures += made != expected ? 1 : 0;
        if (made == expected)
        {
            memset(seen, 0, made);
            for (uint64_t part = 0; part < made; part += 1)
            {
                failures += alone_boundaries[part] != range_part_by_cost(cost_prefix, count, part, made).begin ? 1 : 0;
                failures += alone_order[part] < made && seen[alone_order[part]] == 0 ? 0 : 1;
                seen[alone_order[part] < made ? alone_order[part] : 0] = 1;
            }
            failures += alone_boundaries[made] != count ? 1 : 0;
            failures += lane_test_order_failures(cost_prefix, alone_boundaries, made, alone_order);
            // The group's own lane count: the very parts and order the lanes made together.
            failures += l == 0 && (made != parts || memcmp(alone_boundaries, boundaries, (made + 1) * sizeof(uint64_t)) != 0 ||
                                   memcmp(alone_order, order, made * sizeof(uint32_t)) != 0) ? 1 : 0;
        }
    }
    failures += alone_boundaries != 0 && alone_order != 0 && seen != 0 ? 0 : 1;
    scratch_end(scratch);
    return failures;
}

// Lane 0's checks of one input's loop: the output, every part taken once, the boundaries, every lane's order, and
// parts_by_cost's for the same input.
static uint64_t lane_test_check_part_loop(LaneTest* test, const uint64_t* cost_prefix, uint64_t count, uint64_t parts, uint64_t input, uint64_t bytes)
{
    const uint32_t lanes = lane_count();
    uint64_t failures = 0;
    const uint64_t reference_bytes = test->part_output_offsets[input + 1] - test->part_output_offsets[input];
    failures += bytes != reference_bytes || memcmp(test->part_output, test->part_references + test->part_output_offsets[input], reference_bytes) != 0 ? 1 : 0;
    uint64_t taken = 0;
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        taken += test->part_taken[lane];
    }
    failures += taken != parts || parts != (lanes == 1 ? (count < 1 ? count : 1) : (count < BUSTER_LANE_PARTS_PER_LANE * lanes ? count : BUSTER_LANE_PARTS_PER_LANE * lanes)) ? 1 : 0;
    for (uint64_t part = 0; part < parts; part += 1)
    {
        failures += test->part_takers[part] == 0 ? 1 : 0;
        test->part_takers[part] = 0;
        failures += test->part_boundaries[part] != range_part_by_cost(cost_prefix, count, part, parts).begin ? 1 : 0;
    }
    failures += test->part_boundaries[parts] != count ? 1 : 0;
    // The order: parts that exist, the costliest first (the first of them on a tie), then the others by bucket, then by
    // index; and the same for every lane.
    const uint32_t* order = test->part_orders[0];
    failures += lane_test_order_failures(cost_prefix, test->part_boundaries, parts, order);
    for (uint32_t lane = 1; lane < lanes; lane += 1)
    {
        failures += memcmp(test->part_orders[lane], order, parts * sizeof(uint32_t)) != 0 ? 1 : 0;
    }
    failures += lane_test_check_parts_alone(cost_prefix, count, parts, test->part_boundaries, order);
    return failures;
}

// Count, place and fill by part, for every input in turn (docs/object-writer-and-linker.md, "Lanes", rules 1 and 3).
static uint64_t lane_test_parts(LaneTest* test)
{
    const uint32_t lanes = lane_count();
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    Scratch scratch = scratch_begin(0);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_LANE_MAX_PARTS);
    uint32_t* mine = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_LANE_MAX_PARTS);
    uint64_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_LANE_MAX_PARTS);
    test->part_orders[index] = order;
    for (uint64_t input = 0; input < BUSTER_LANE_TEST_PART_INPUT_COUNT; input += 1)
    {
        const uint64_t count = lane_test_part_inputs[input].count;
        const uint64_t* cost_prefix = test->part_prefixes + test->part_prefix_offsets[input];
        const uint64_t parts = lane_parts_by_cost(cost_prefix, count, test->part_boundaries, order);
        // One lane comes late to the loop, so the others take its parts: by turns for long enough to sleep, and briefly.
        if (lanes > 1 && index == input % lanes)
        {
            lane_test_stall(input % 3 == 0 ? BUSTER_LANE_TEST_STALL_NANOSECONDS : BUSTER_LANE_TEST_SHORT_STALL_NANOSECONDS);
        }
        // Count.
        uint64_t taken = 0;
        for (uint64_t next = lane_take(parts); next < parts; next = lane_take(parts))
        {
            const uint32_t part = order[next];
            uint64_t bytes = 0;
            for (uint64_t item = test->part_boundaries[part]; item < test->part_boundaries[part + 1]; item += 1)
            {
                bytes += lane_test_output_bytes(item);
            }
            test->part_bytes[part] = bytes;
            test->part_takers[part] = index + 1;
            mine[taken] = part;
            taken += 1;
        }
        test->part_taken[index] = taken;
        lane_sync();
        // Place: every lane adds up every part's bytes for itself.
        uint64_t total = 0;
        for (uint64_t part = 0; part < parts; part += 1)
        {
            offsets[part] = total;
            total += test->part_bytes[part];
        }
        // Fill the parts this lane took.
        for (uint64_t i = 0; i < taken; i += 1)
        {
            const uint32_t part = mine[i];
            uint64_t offset = offsets[part];
            for (uint64_t item = test->part_boundaries[part]; item < test->part_boundaries[part + 1]; item += 1)
            {
                for (uint64_t byte = 0; byte < lane_test_output_bytes(item); byte += 1)
                {
                    test->part_output[offset] = lane_test_output_byte(item, byte);
                    offset += 1;
                }
            }
        }
        lane_sync();
        if (index == 0)
        {
            failures += lane_test_check_part_loop(test, cost_prefix, count, parts, input, total);
        }
        lane_sync();
    }
    scratch_end(scratch);
    return failures;
}

// The entry point of the checks' lanes.
static void lane_test_main(void* argument)
{
    LaneTest* test = argument;
    const uint64_t sleeps = thread_context.sleeps;
    uint64_t failures = 0;
    switch (test->check)
    {
        case LANE_TEST_RANGES: lane_test_ranges(test); break;
        case LANE_TEST_SYNCS: failures = lane_test_syncs(test); break;
        case LANE_TEST_ALONE: failures = lane_test_alone(); break;
        case LANE_TEST_SCRATCH: failures = lane_test_scratch(test); break;
        case LANE_TEST_EXCHANGE: failures = lane_test_exchange(test); break;
        case LANE_TEST_PARTS: failures = lane_test_parts(test); break;
        case LANE_TEST_ADVANCE: failures = lane_test_advance(test); break;
        case LANE_TEST_SHARED: failures = lane_test_shared(test); break;
    }
    test->failures[lane_index()] = failures;
    test->sleeps[lane_index()] = thread_context.sleeps - sleeps;
}

// ---------------------------------------------------------------------------------------------------------------------
// Verification

// The check and lane count the watchdog names if a group doesn't finish.
// One atomic value selects an immutable diagnostic and the progress policy. The handler cannot race a message being
// formatted for the next group, even if a previous alarm was already pending when it was canceled.
static uint32_t lane_test_watch_state;
_Static_assert(sizeof(uint32_t) == sizeof(unsigned int) && __GCC_ATOMIC_INT_LOCK_FREE == 2, "the watchdog needs lock-free atomics");

static void lane_test_watch(uint32_t check, uint32_t lanes, bool progress)
{
    __atomic_store_n(&lane_test_progress, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&lane_test_watch_state, (check << 8) | lanes | (progress ? 0x10000u : 0), __ATOMIC_RELAXED);
}

// Called only after all of the watched group's workers have joined. Block delivery to the sole remaining thread,
// cancel the timer, and consume any process-pending alarm before a new group can install its watchdog state. This
// also cancels a timer that a handler rearmed immediately before the block.
static bool lane_test_unwatch(void)
{
    sigset_t mask;
    sigset_t previous;
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    bool result = pthread_sigmask(SIG_BLOCK, &mask, &previous) == 0;
    alarm(0);
    if (result)
    {
        const struct timespec now = { 0 };
        int signal_number;
        do
        {
            signal_number = sigtimedwait(&mask, 0, &now);
        }
        while (signal_number == SIGALRM || (signal_number < 0 && errno == EINTR));
        result = signal_number < 0 && errno == EAGAIN;
        result = pthread_sigmask(SIG_SETMASK, &previous, 0) == 0 && result;
    }
    return result;
}

static void lane_test_report_hang(int signal_number)
{
    (void)signal_number;
    static const char* const names[] = { "ranges", "syncs", "alone", "scratch", "exchange", "parts", "advance", "shared CPUs", "team lifetime", "sessions" };
    static const uint8_t lengths[] = { 6, 5, 5, 7, 8, 5, 7, 11, 13, 8 };
    static const char digits[] = "012345678";
    static const char middle[] = " on ";
    static const char elapsed[] = " lanes didn't finish in " BUSTER_LANE_TEST_STRINGIFY(BUSTER_LANE_TEST_TIMEOUT_SECONDS) " s\n";
    static const char stalled[] = " lanes made no progress during the last " BUSTER_LANE_TEST_STRINGIFY(BUSTER_LANE_TEST_TIMEOUT_SECONDS) " s\n";
    const uint32_t state = __atomic_load_n(&lane_test_watch_state, __ATOMIC_RELAXED);
    const uint32_t check = (state >> 8) & 0xffu;
    const uint32_t lanes = state & 0xffu;
    const bool progress = (state & 0x10000u) != 0;
    if (progress && __atomic_exchange_n(&lane_test_progress, 0, __ATOMIC_RELAXED) != 0)
    {
        alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
        return;
    }
    if (check < BUSTER_ARRAY_LENGTH(names) && lanes < sizeof(digits) - 1)
    {
        (void)write(STDOUT_FILENO, names[check], lengths[check]);
        (void)write(STDOUT_FILENO, middle, sizeof(middle) - 1);
        (void)write(STDOUT_FILENO, &digits[lanes], 1);
        (void)write(STDOUT_FILENO, progress ? stalled : elapsed, progress ? sizeof(stalled) - 1 : sizeof(elapsed) - 1);
    }
    _exit(1);
}

// Runs a check on a group of `lanes` lanes, under the watchdog. Returns the failures the lanes counted, and their sleeps
// in *sleeps; if the group couldn't be started, one failure.
static uint64_t lane_test_run(LaneTest* test, LaneTestCheck check, uint32_t lanes, uint64_t* sleeps)
{
    test->check = check;
    memset(test->failures, 0, sizeof(test->failures));
    memset(test->sleeps, 0, sizeof(test->sleeps));
    lane_test_watch(check, lanes, check == LANE_TEST_SYNCS);
    alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
    const bool started = lane_run(lanes, lane_test_main, test, 0);
    const bool unwatched = lane_test_unwatch();
    uint64_t result = (started ? 0 : 1) + (unwatched ? 0 : 1);
    *sleeps = 0;
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        result += test->failures[lane];
        *sleeps += test->sleeps[lane];
    }
    if (!started)
    {
        printf("  %s: a group of %u lanes couldn't be started\n", lane_test_check_names[check], lanes);
    }
    if (!unwatched)
    {
        printf("  %s: couldn't stop the watchdog\n", lane_test_check_names[check]);
    }
    return result;
}

// Exercise the alarm paths without waiting for the real deadline. A child with a completed round must return from its
// first alarm, but if nothing else completes, its next alarm must terminate it. The pipe proves the first alarm returned.
static bool lane_test_watchdog(void)
{
    bool passed = true;
    lane_test_watch(LANE_TEST_SYNCS, 2, false);
    alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
    for (uint32_t scenario = 0; scenario < 4; scenario += 1)
    {
        int marker[2];
        const bool piped = pipe(marker) == 0;
        const pid_t child = piped ? fork() : -1;
        if (child == 0)
        {
            close(marker[0]);
            const int sink = open("/dev/null", O_WRONLY);
            if (sink < 0 || dup2(sink, STDOUT_FILENO) < 0)
            {
                _exit(3);
            }
            close(sink);
            lane_test_watch(LANE_TEST_SYNCS, 2, true);
            alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
            if (scenario == 3)
            {
                sigset_t mask;
                sigset_t previous;
                sigset_t pending;
                sigemptyset(&mask);
                sigaddset(&mask, SIGALRM);
                const bool blocked = pthread_sigmask(SIG_BLOCK, &mask, &previous) == 0;
                const bool raised = blocked && raise(SIGALRM) == 0;
                const bool drained = raised && lane_test_unwatch();
                const bool empty = drained && sigpending(&pending) == 0 && sigismember(&pending, SIGALRM) == 0;
                if (!empty || pthread_sigmask(SIG_SETMASK, &previous, 0) != 0)
                {
                    _exit(3);
                }
                const char byte = 1;
                _exit(write(marker[1], &byte, 1) == 1 ? 2 : 3);
            }
            __atomic_store_n(&lane_test_progress, scenario != 0, __ATOMIC_RELAXED);
            raise(SIGALRM);
            const char byte = 1;
            if (write(marker[1], &byte, 1) != 1)
            {
                _exit(3);
            }
            if (scenario == 2)
            {
                raise(SIGALRM);
            }
            _exit(2);
        }
        if (piped)
        {
            close(marker[1]);
        }
        char byte = 0;
        const ssize_t bytes = piped ? read(marker[0], &byte, 1) : -1;
        if (piped)
        {
            close(marker[0]);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        passed = waited && WIFEXITED(status) && WEXITSTATUS(status) == (scenario == 1 || scenario == 3 ? 2 : 1) &&
                 bytes == (scenario == 0 ? 0 : 1) && passed;
    }
    passed = lane_test_unwatch() && passed;
    printf("watchdog: progress and stalled rounds %s\n", passed ? "passed" : "failed");
    return passed;
}

// The lane that lane_range_by_cost must give item i, with total cost `total` (not 0): the first k whose share's end,
// total * (k + 1) / lanes, is at or past the item's midpoint (P[i - 1] + P[i]) / 2.
static uint64_t lane_test_cost_lane(const uint64_t* cost_prefix, uint64_t i, uint64_t total, uint64_t lanes)
{
    const unsigned __int128 scaled = ((unsigned __int128)(i == 0 ? 0 : cost_prefix[i - 1]) + cost_prefix[i]) * lanes;
    const unsigned __int128 twice_total = (unsigned __int128)total * 2;
    const uint64_t ceiling = (uint64_t)((scaled + twice_total - 1) / twice_total);
    return ceiling == 0 ? 0 : ceiling - 1;
}

// Checks the lanes' parts of `count` items: they cover [0, count) in lane order, and each has the length lane_range
// gives it or, with a cost prefix that has a cost, the items whose midpoints its share holds, which keeps its cost
// within the largest item's of an equal share.
static bool lane_test_check_parts(const Range64* parts, uint64_t lanes, uint64_t count, const uint64_t* cost_prefix)
{
    const uint64_t total = cost_prefix != 0 && count != 0 ? cost_prefix[count - 1] : 0;
    uint64_t largest = 0;
    for (uint64_t i = 0; i < count && total != 0; i += 1)
    {
        const uint64_t cost = cost_prefix[i] - (i == 0 ? 0 : cost_prefix[i - 1]);
        largest = cost > largest ? cost : largest;
    }
    bool result = true;
    uint64_t next = 0;
    for (uint64_t lane = 0; lane < lanes; lane += 1)
    {
        const Range64 part = parts[lane];
        result = result && part.begin == next && part.end >= part.begin && part.end <= count;
        if (result && total == 0)
        {
            result = part.end - part.begin == count / lanes + (lane < count % lanes ? 1 : 0);
        }
        for (uint64_t i = part.begin; result && total != 0 && i < part.end; i += 1)
        {
            result = lane_test_cost_lane(cost_prefix, i, total, lanes) == lane;
        }
        if (result && total != 0)
        {
            const uint64_t cost = (part.end == 0 ? 0 : cost_prefix[part.end - 1]) - (part.begin == 0 ? 0 : cost_prefix[part.begin - 1]);
            const unsigned __int128 scaled = (unsigned __int128)cost * lanes;
            const unsigned __int128 slack = (unsigned __int128)largest * lanes;
            result = scaled <= total + slack && scaled + slack >= total;
        }
        next = part.end;
    }
    return result && next == count;
}

static uint64_t lane_test_check_ranges(const LaneTest* test, uint32_t lanes, uint64_t* checked)
{
    uint64_t failures = 0;
    *checked = 0;
    for (int32_t pattern = -1; pattern < BUSTER_LANE_TEST_COST_PATTERNS; pattern += 1)
    {
        for (uint64_t count = 0; count <= BUSTER_LANE_TEST_MAX_RANGE; count += 1)
        {
            const Range64* parts = test->parts + lane_test_part(pattern, count, 0, lanes);
            const bool good = lane_test_check_parts(parts, lanes, count, pattern < 0 ? 0 : test->cost_prefix[pattern]);
            if (!good && failures < BUSTER_LANE_TEST_REPORTED)
            {
                printf("  ranges: %s of %lu items on %u lanes:", pattern < 0 ? "by count" : "by cost", (unsigned long)count, lanes);
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    printf(" [%lu, %lu)", (unsigned long)parts[lane].begin, (unsigned long)parts[lane].end);
                }
                if (pattern >= 0)
                {
                    printf(" (cost pattern %d)", pattern);
                }
                printf("\n");
            }
            failures += good ? 0 : 1;
            *checked += 1;
        }
    }
    return failures;
}

// range_part and range_part_by_cost with more parts than lanes, on the calling thread: lane_test_part_counts parts of
// each of lane_test_part_range_counts items, without cost and with each cost pattern.
static uint64_t lane_test_part_ranges(const LaneTest* test, uint64_t* checked)
{
    uint64_t failures = 0;
    *checked = 0;
    Scratch scratch = scratch_begin(0);
    Range64* parts = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, Range64, 1001);
    for (uint64_t p = 0; parts != 0 && p < BUSTER_ARRAY_LENGTH(lane_test_part_counts); p += 1)
    {
        const uint64_t part_count = lane_test_part_counts[p];
        for (int32_t pattern = -1; pattern < BUSTER_LANE_TEST_COST_PATTERNS; pattern += 1)
        {
            for (uint64_t c = 0; c < BUSTER_ARRAY_LENGTH(lane_test_part_range_counts); c += 1)
            {
                const uint64_t count = lane_test_part_range_counts[c];
                for (uint64_t part = 0; part < part_count; part += 1)
                {
                    parts[part] = pattern < 0 ? range_part(count, part, part_count) : range_part_by_cost(test->cost_prefix[pattern], count, part, part_count);
                }
                const bool good = lane_test_check_parts(parts, part_count, count, pattern < 0 ? 0 : test->cost_prefix[pattern]);
                if (!good && failures < BUSTER_LANE_TEST_REPORTED)
                {
                    printf("  parts: %lu items in %lu parts %s are wrong\n", (unsigned long)count, (unsigned long)part_count, pattern < 0 ? "by count" : "by cost");
                }
                failures += good ? 0 : 1;
                *checked += 1;
            }
        }
    }
    scratch_end(scratch);
    return failures + (parts == 0 ? 1 : 0);
}

// Whether every lane's scratch arenas are distinct and reserved.
static bool lane_test_check_scratch(const LaneTest* test, uint32_t lanes)
{
    bool result = true;
    for (uint32_t i = 0; i < lanes * BUSTER_SCRATCH_ARENA_COUNT; i += 1)
    {
        const uint8_t* base = test->scratch_bases[i / BUSTER_SCRATCH_ARENA_COUNT][i % BUSTER_SCRATCH_ARENA_COUNT];
        result = result && base != 0;
        for (uint32_t j = 0; j < i; j += 1)
        {
            result = result && base != test->scratch_bases[j / BUSTER_SCRATCH_ARENA_COUNT][j % BUSTER_SCRATCH_ARENA_COUNT];
        }
    }
    return result;
}

// The sequential reference of the exchange: the records partitioned by a stable counting sort, and resolved all at once.
static bool lane_test_reference(const LaneTestRecord* records, uint64_t count, LaneTestRecord* exchanged, uint32_t* winners)
{
    uint64_t next[BUSTER_LANE_TEST_PARTITIONS] = { 0 };
    for (uint64_t i = 0; i < count; i += 1)
    {
        next[lane_test_partition(records[i].hash)] += 1;
    }
    uint64_t offset = 0;
    for (uint32_t partition = 0; partition < BUSTER_LANE_TEST_PARTITIONS; partition += 1)
    {
        const uint64_t partition_count = next[partition];
        next[partition] = offset;
        offset += partition_count;
    }
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint32_t partition = lane_test_partition(records[i].hash);
        exchanged[next[partition]] = records[i];
        next[partition] += 1;
    }
    return lane_test_resolve(records, count, winners, count);
}

static bool lane_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0;
    *value = result ? (uint64_t)parsed : 0;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// A command's team, kept across many operations

typedef struct LaneTeamTest LaneTeamTest;
struct LaneTeamTest
{
    uint32_t lanes;
    uint64_t round;
    pid_t caller;
    const uint32_t* cpus;
    pid_t identities[BUSTER_LANE_TEST_MAX_LANES];
    pid_t observed[BUSTER_LANE_TEST_MAX_LANES];
    uint64_t called[BUSTER_LANE_TEST_MAX_LANES];
    uint64_t inputs[BUSTER_LANE_TEST_MAX_LANES];
    uint64_t outputs[BUSTER_LANE_TEST_MAX_LANES];
    uint64_t failures[BUSTER_LANE_TEST_MAX_LANES];
    const uint8_t* scratch_bases[BUSTER_LANE_TEST_MAX_LANES][BUSTER_SCRATCH_ARENA_COUNT];
    uint32_t parts[BUSTER_LANE_TEST_TEAM_PARTS];
    uint32_t forbidden_calls;
    uint32_t invalid_indices;
    // Whether the team keeps its workers' scratch arenas (lane_team_keep_scratch).
    bool keep;
};

// A refused operation must not enter even its calling lane. Atomic so a broken refusal reports duplicated work
// without a data race in the test itself.
static void lane_test_team_forbidden(void* argument)
{
    LaneTeamTest* test = argument;
    __atomic_add_fetch(&test->forbidden_calls, 1, __ATOMIC_RELAXED);
}

static void lane_test_team_main(void* argument)
{
    LaneTeamTest* test = argument;
    const uint32_t index = lane_index();
    const uint32_t lanes = lane_count();
    if (index >= BUSTER_LANE_TEST_MAX_LANES)
    {
        __atomic_add_fetch(&test->invalid_indices, 1, __ATOMIC_RELAXED);
    }
    else
    {
        uint64_t failures = lanes != test->lanes || index >= test->lanes ? 1 : 0;
        test->called[index] += 1;
        test->observed[index] = (pid_t)syscall(SYS_gettid);
        failures += (index == 0) != (test->observed[index] == test->caller) ? 1 : 0;
        if (test->cpus != 0)
        {
            uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
            failures += syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) <= 0 ? 1 : 0;
            for (uint32_t word = 0; word < BUSTER_LANE_CPU_MASK_WORDS; word += 1)
            {
                const uint64_t expected = word == test->cpus[index] / 64 ? 1ull << (test->cpus[index] % 64) : 0;
                failures += mask[word] != expected ? 1 : 0;
            }
        }
        for (uint32_t i = 0; i < BUSTER_LANE_TEST_MAX_LANES; i += 1)
        {
            failures += test->inputs[i] != lane_test_value(test->round, i) ? 1 : 0;
        }
        // Leave the workers' allocations live at return, to require cleanup before their next operation. The
        // calling lane instead preserves a scratch allocation owned by the command around all its operations, and a
        // team that keeps its workers' arenas gives each worker those of its last operation, emptied.
        for (uint32_t i = 0; index != 0 && i < BUSTER_SCRATCH_ARENA_COUNT; i += 1)
        {
            const uint8_t* kept = test->keep ? test->scratch_bases[index][i] : 0;
            failures += thread_context.scratch[i].base != kept || thread_context.scratch[i].length != 0 ? 1 : 0;
        }
        Scratch first = scratch_begin(0);
        Scratch second = scratch_begin(first.arena);
        uint64_t* first_value = BUSTER_ARENA_PUSH_ARRAY(first.arena, uint64_t, 1);
        uint64_t* second_value = BUSTER_ARENA_PUSH_ARRAY(second.arena, uint64_t, 1);
        failures += first_value == 0 || second_value == 0 ? 1 : 0;
        if (first_value != 0 && second_value != 0)
        {
            *first_value = test->inputs[index];
            *second_value = ~test->inputs[index];
        }
        test->scratch_bases[index][0] = first.arena->base;
        test->scratch_bases[index][1] = second.arena->base;
        if (lanes > 1 && test->round % 64 == 7 && index == test->round % lanes)
        {
            lane_test_stall(BUSTER_LANE_TEST_STALL_NANOSECONDS);
        }
        test->outputs[index] = ~test->inputs[index];
        lane_sync();
        for (uint32_t i = 0; i < lanes && i < BUSTER_LANE_TEST_MAX_LANES; i += 1)
        {
            failures += test->outputs[i] != ~lane_test_value(test->round, i) ? 1 : 0;
            for (uint32_t arena = 0; arena < BUSTER_SCRATCH_ARENA_COUNT; arena += 1)
            {
                failures += test->scratch_bases[i][arena] == 0 ? 1 : 0;
                for (uint32_t earlier = 0; earlier < i * BUSTER_SCRATCH_ARENA_COUNT + arena; earlier += 1)
                {
                    failures += test->scratch_bases[i][arena] == test->scratch_bases[earlier / BUSTER_SCRATCH_ARENA_COUNT][earlier % BUSTER_SCRATCH_ARENA_COUNT] ? 1 : 0;
                }
            }
        }
        const uint32_t source = (uint32_t)(test->round % lanes);
        failures += lane_broadcast(test->outputs[index], source) != ~lane_test_value(test->round, source) ? 1 : 0;
        for (uint64_t part = lane_take(BUSTER_LANE_TEST_TEAM_PARTS); part < BUSTER_LANE_TEST_TEAM_PARTS; part = lane_take(BUSTER_LANE_TEST_TEAM_PARTS))
        {
            __atomic_add_fetch(&test->parts[part], 1, __ATOMIC_RELAXED);
        }
        lane_sync();
        if (index == 0)
        {
            for (uint32_t part = 0; part < BUSTER_LANE_TEST_TEAM_PARTS; part += 1)
            {
                failures += test->parts[part] != 1 ? 1 : 0;
            }
        }
        if (test->round < 2)
        {
            failures += lane_run(1, lane_test_team_forbidden, test, 0) ? 1 : 0;
            const Lane previous = lane_alone();
            failures += lane_run(1, lane_test_team_forbidden, test, 0) ? 1 : 0;
            LaneTeam nested = { 0 };
            const bool started = lane_team_start(&nested, 1, 0);
            failures += started ? 1 : 0;
            lane_team_stop(&nested);
            lane_restore(previous);
        }
        if (first_value != 0 && second_value != 0)
        {
            failures += *first_value != test->inputs[index] || *second_value != ~test->inputs[index] ? 1 : 0;
        }
        if (index == 0)
        {
            scratch_end(second);
            scratch_end(first);
        }
        test->failures[index] = failures;
    }
}

// Counts this process's threads, including the calling one. The one-lane check must not create an idle worker.
static uint32_t lane_test_thread_count(void)
{
    DIR* directory = opendir("/proc/self/task");
    uint32_t result = directory != 0 ? 0 : UINT32_MAX;
    if (directory != 0)
    {
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0)
        {
            result += entry->d_name[0] >= '0' && entry->d_name[0] <= '9' ? 1 : 0;
        }
        closedir(directory);
    }
    return result;
}

// pthread_join observes thread completion; /proc can briefly retain the kernel's task record while it finishes its
// exit. Bound that observation separately from the operation watchdog.
static bool lane_test_threads_stopped(uint32_t expected)
{
    uint32_t count = lane_test_thread_count();
    for (uint32_t attempt = 0; count != expected && count != UINT32_MAX && attempt < 100; attempt += 1)
    {
        lane_test_stall(BUSTER_LANE_TEST_SHORT_STALL_NANOSECONDS);
        count = lane_test_thread_count();
    }
    return count == expected;
}

static uint64_t lane_test_team_operation(LaneTeamTest* test, uint32_t lanes, uint64_t round, const uint32_t* cpus)
{
    test->lanes = lanes;
    test->round = round;
    test->cpus = cpus;
    memset(test->called, 0, sizeof(test->called));
    memset(test->observed, 0, sizeof(test->observed));
    memset(test->outputs, 0, sizeof(test->outputs));
    memset(test->failures, 0, sizeof(test->failures));
    memset(test->parts, 0, sizeof(test->parts));
    for (uint32_t i = 0; i < BUSTER_LANE_TEST_MAX_LANES; i += 1)
    {
        test->inputs[i] = lane_test_value(round, i);
    }
    const Lane previous = thread_context.lane;
    const uint64_t previous_taken = thread_context.taken;
    thread_context.taken = 47;
    const bool ran = lane_run(lanes, lane_test_team_main, test, cpus);
    uint64_t failures = ran ? 0 : 1;
    failures += thread_context.lane.index != previous.index || thread_context.lane.count != previous.count || thread_context.lane.group != previous.group ? 1 : 0;
    failures += thread_context.taken != 47 ? 1 : 0;
    thread_context.taken = previous_taken;
    for (uint32_t i = 0; i < BUSTER_LANE_TEST_MAX_LANES; i += 1)
    {
        const bool active = i < lanes;
        failures += test->called[i] != (active ? 1u : 0u) ? 1 : 0;
        failures += test->failures[i];
        if (active)
        {
            failures += test->outputs[i] != ~test->inputs[i] || test->observed[i] == 0 ? 1 : 0;
            failures += test->identities[i] != 0 && test->identities[i] != test->observed[i] ? 1 : 0;
            test->identities[i] = test->observed[i];
            for (uint32_t earlier = 0; earlier < i; earlier += 1)
            {
                failures += test->observed[i] == test->observed[earlier] ? 1 : 0;
            }
        }
    }
    failures += test->forbidden_calls != 0 || test->invalid_indices != 0 ? 1 : 0;
    return failures;
}

// Pinning failure is startup failure too: the caller's mask and the number of live threads must both be restored.
// With a valid mask, both the calling lane and its workers are pinned only for their specified lifetime.
static uint64_t lane_test_team_affinity(LaneTeam* team, uint32_t threads_before)
{
    uint64_t before[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint64_t after[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool saved = syscall(SYS_sched_getaffinity, 0, sizeof(before), before) > 0;
    uint64_t failures = saved ? 0 : 1;
    if (saved)
    {
        const uint32_t invalid = BUSTER_LANE_CPU_MASK_WORDS * 64;
        uint32_t valid = 0;
        while (valid < invalid && (before[valid / 64] & (1ull << (valid % 64))) == 0)
        {
            valid += 1;
        }
        const uint32_t bad_caller[] = { invalid };
        const bool caller_started = lane_team_start(team, 1, bad_caller);
        failures += caller_started ? 1 : 0;
        lane_team_stop(team);
        const uint32_t bad_worker[] = { valid, invalid };
        const bool worker_started = lane_team_start(team, 2, bad_worker);
        failures += worker_started ? 1 : 0;
        lane_team_stop(team);
        failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
        bool restored = syscall(SYS_sched_getaffinity, 0, sizeof(after), after) > 0 && memcmp(before, after, sizeof(before)) == 0;
        failures += restored ? 0 : 1;

        // Use one allowed CPU for both lanes, so the test also works in a one-CPU affinity set.
        const uint32_t cpus[] = { valid, valid };
        const bool started = lane_team_start(team, 2, cpus);
        failures += started ? 0 : 1;
        if (started)
        {
            LaneTeamTest test = {
                .caller = (pid_t)syscall(SYS_gettid),
            };
            failures += lane_run(1, lane_test_team_forbidden, &test, 0) ? 1 : 0;
            failures += lane_run(1, lane_test_team_forbidden, &test, bad_caller) ? 1 : 0;
            failures += lane_test_team_operation(&test, 2, BUSTER_LANE_TEST_TEAM_ROUNDS + 1, cpus);
            failures += lane_test_team_operation(&test, 1, BUSTER_LANE_TEST_TEAM_ROUNDS + 2, cpus);
            failures += lane_test_team_operation(&test, 2, BUSTER_LANE_TEST_TEAM_ROUNDS + 3, cpus);
            failures += test.forbidden_calls != 0 ? 1 : 0;
            memset(after, 0, sizeof(after));
            restored = syscall(SYS_sched_getaffinity, 0, sizeof(after), after) > 0 && memcmp(before, after, sizeof(before)) == 0;
            failures += restored ? 0 : 1;
        }
        lane_team_stop(team);
        failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
        memset(after, 0, sizeof(after));
        restored = syscall(SYS_sched_getaffinity, 0, sizeof(after), after) > 0 && memcmp(before, after, sizeof(before)) == 0;
        failures += restored ? 0 : 1;
    }
    return failures;
}

// A team that keeps its workers' scratch arenas (lane_team_keep_scratch, #501): across operations of changing lane
// counts each worker finds the arenas of its last operation, emptied (lane_test_team_main), and once the team has
// stopped, the arenas are unmapped: mincore refuses each one's first page.
static uint64_t lane_test_team_keep(uint32_t threads_before)
{
    static const uint32_t counts[] = { 8, 2, 8, 1, 3, 8 };
    LaneTeam team = { 0 };
    LaneTeamTest test = {
        .caller = (pid_t)syscall(SYS_gettid),
        .keep = true,
    };
    const bool started = lane_team_start(&team, BUSTER_LANE_TEST_MAX_LANES, 0);
    uint64_t failures = started ? 0 : 1;
    if (started)
    {
        lane_team_keep_scratch(&team);
        for (uint32_t round = 0; round < BUSTER_ARRAY_LENGTH(counts); round += 1)
        {
            failures += lane_test_team_operation(&test, counts[round], BUSTER_LANE_TEST_TEAM_ROUNDS + 4 + round, 0);
        }
    }
    lane_team_stop(&team);
    failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
    for (uint32_t index = 1; index < BUSTER_LANE_TEST_MAX_LANES; index += 1)
    {
        for (uint32_t i = 0; i < BUSTER_SCRATCH_ARENA_COUNT; i += 1)
        {
            unsigned char vector = 0;
            const uint8_t* base = test.scratch_bases[index][i];
            failures += base == 0 || mincore((void*)base, BUSTER_ARENA_PAGE_BYTES, &vector) == 0 || errno != ENOMEM ? 1 : 0;
        }
    }
    return failures;
}

#if __has_feature(thread_sanitizer)
static void* lane_test_nothing(void* argument)
{
    return argument;
}
#endif

static uint64_t lane_test_teams(void)
{
    lane_test_watch(BUSTER_ARRAY_LENGTH(lane_test_check_names), BUSTER_LANE_TEST_MAX_LANES, false);
    alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
#if __has_feature(thread_sanitizer)
    // ThreadSanitizer starts a thread of its own with the process's first other thread, which the counts below would
    // take for a lane's: one started and joined first leaves the detector's running before them.
    pthread_t first;
    if (pthread_create(&first, 0, lane_test_nothing, 0) == 0)
    {
        pthread_join(first, 0);
    }
#endif
    const uint32_t threads_before = lane_test_thread_count();
    const Lane previous = thread_context.lane;
    Scratch retained = scratch_begin(0);
    uint64_t* retained_value = BUSTER_ARENA_PUSH_ARRAY(retained.arena, uint64_t, 1);
    uint64_t failures = retained_value == 0 || threads_before == UINT32_MAX ? 1 : 0;
    if (retained_value != 0)
    {
        *retained_value = BUSTER_LANE_TEST_GOLDEN_RATIO;
    }
    LaneTeam team = { 0 };
    LaneTeamTest test = {
        .caller = (pid_t)syscall(SYS_gettid),
    };
    const bool zero_started = lane_team_start(&team, 0, 0);
    failures += zero_started ? 1 : 0;
    lane_team_stop(&team);
    const bool oversized_started = lane_team_start(&team, BUSTER_LANE_MAX_COUNT + 1, 0);
    failures += oversized_started ? 1 : 0;
    lane_team_stop(&team);
    const bool single_started = lane_team_start(&team, 1, 0);
    failures += single_started ? 0 : 1;
    if (single_started)
    {
        failures += lane_test_thread_count() != threads_before ? 1 : 0;
        failures += lane_test_team_operation(&test, 1, 0, 0);
        failures += lane_test_team_operation(&test, 1, 1, 0);
    }
    lane_team_stop(&team);
    failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
    const bool started = lane_team_start(&team, BUSTER_LANE_TEST_MAX_LANES, 0);
    failures += started ? 0 : 1;
    if (started)
    {
        failures += lane_test_thread_count() != threads_before + BUSTER_LANE_TEST_MAX_LANES - 1 ? 1 : 0;
        LaneTeam second = { 0 };
        const bool second_started = lane_team_start(&second, 1, 0);
        failures += second_started ? 1 : 0;
        lane_team_stop(&second);
        failures += lane_run(0, lane_test_team_forbidden, &test, 0) ? 1 : 0;
        failures += lane_run(BUSTER_LANE_TEST_MAX_LANES + 1, lane_test_team_forbidden, &test, 0) ? 1 : 0;
        failures += lane_run(1, 0, &test, 0) ? 1 : 0;
        const uint32_t cpus[BUSTER_LANE_TEST_MAX_LANES] = { 0 };
        failures += lane_run(1, lane_test_team_forbidden, &test, cpus) ? 1 : 0;
        static const uint32_t first_counts[] = { 1, 8, 2, 8 };
        for (uint64_t round = 0; round < BUSTER_LANE_TEST_TEAM_ROUNDS; round += 1)
        {
            const uint32_t lanes = round < BUSTER_ARRAY_LENGTH(first_counts) ? first_counts[round] : lane_test_counts[(round * 7) % BUSTER_ARRAY_LENGTH(lane_test_counts)];
            failures += lane_test_team_operation(&test, lanes, round, 0);
            failures += retained_value == 0 || *retained_value != BUSTER_LANE_TEST_GOLDEN_RATIO ? 1 : 0;
            if (round % 64 == 31)
            {
                lane_test_stall(BUSTER_LANE_TEST_STALL_NANOSECONDS);
            }
        }
        // Only the calling thread survives fork. The child must refuse the inherited team and forget it on stop,
        // without waiting for any of the parent's worker identities.
        fflush(stdout);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
            const bool ran = lane_run(1, lane_test_team_forbidden, &test, 0);
            lane_team_stop(&team);
            _exit(ran || test.forbidden_calls != 0 ? 1 : 0);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        failures += waited && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
        failures += lane_test_team_operation(&test, BUSTER_LANE_TEST_MAX_LANES, BUSTER_LANE_TEST_TEAM_ROUNDS, 0);
    }
    lane_team_stop(&team);
    lane_team_stop(&team);
    failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
    failures += lane_test_team_affinity(&team, threads_before);
    failures += lane_test_team_keep(threads_before);
    failures += thread_context.lane.index != previous.index || thread_context.lane.count != previous.count || thread_context.lane.group != previous.group ? 1 : 0;
    failures += retained_value == 0 || *retained_value != BUSTER_LANE_TEST_GOLDEN_RATIO ? 1 : 0;
    failures += test.forbidden_calls != 0 || test.invalid_indices != 0 ? 1 : 0;
    scratch_end(retained);
    failures += lane_test_unwatch() ? 0 : 1;
    printf("teams: %u operations with changing lane counts, %lu wrong\n", BUSTER_LANE_TEST_TEAM_ROUNDS + 12, (unsigned long)failures);
    return failures;
}

// The parts a session check's narrowed lanes take.
#define BUSTER_LANE_TEST_SESSION_PARTS 29

// /proc/cpuinfo's cores (#905), an oracle of lane_cores' that doesn't read sysfs' sibling lists, for this file's tests
// and the drivers' (test-check-lanes, test-foreign, test-parallel). A line's key is what comes before its colon, but
// blanks, and only the processor, physical id and core id lines' values are read (#933's review: the flags line's
// digits, read as one number, overflowed a signed one): each value is one decimal below UINT32_MAX, between blanks, so
// that no package and core pack into the sentinel, UINT64_MAX. Whether `line` (`length` bytes) is `key`'s.
static bool lane_test_cpuinfo_key(const char* line, size_t length, const char* key)
{
    const size_t key_length = strlen(key);
    const char* colon = memchr(line, ':', length);
    bool matches = colon != 0 && (size_t)(colon - line) >= key_length && memcmp(line, key, key_length) == 0;
    for (const char* c = line + key_length; matches && c < colon; c += 1)
    {
        matches = *c == ' ' || *c == '\t';
    }
    return matches;
}

// The value of a line (`length` bytes) that lane_test_cpuinfo_key matched, or -1 where it isn't one decimal below
// UINT32_MAX between blanks; the conversion stops before its value could pass the bound.
static int64_t lane_test_cpuinfo_value(const char* line, size_t length)
{
    const char* end = line + length;
    const char* colon = memchr(line, ':', length);
    const char* c = colon != 0 ? colon + 1 : end;
    while (c < end && (*c == ' ' || *c == '\t'))
    {
        c += 1;
    }
    uint64_t value = 0;
    bool valid = c < end && *c >= '0' && *c <= '9';
    while (valid && c < end && *c >= '0' && *c <= '9')
    {
        const uint64_t digit = (uint64_t)(*c - '0');
        valid = value <= (UINT32_MAX - 1 - digit) / 10;
        value = valid ? value * 10 + digit : value;
        c += 1;
    }
    while (c < end && (*c == ' ' || *c == '\t' || *c == '\n'))
    {
        c += 1;
    }
    return valid && c == end ? (int64_t)value : -1;
}

// lane_test_cpuinfo's reading of `file` (null for none): each processor's physical id and core id as one number,
// package << 32 | core, in `cores` (BUSTER_LANE_CPU_MASK_WORDS * 64 entries, UINT64_MAX for a processor it doesn't list
// with both). A processor line starts a processor and forgets the package, and a line of one of the three keys whose
// value doesn't read leaves its field unknown. Returns how many processors it lists with both.
static uint32_t lane_test_cpuinfo_read(FILE* file, uint64_t* cores)
{
    const uint32_t room = BUSTER_LANE_CPU_MASK_WORDS * 64;
    for (uint32_t cpu = 0; cpu < room; cpu += 1)
    {
        cores[cpu] = UINT64_MAX;
    }
    char* line = 0;
    size_t size = 0;
    int64_t processor = -1;
    int64_t package = -1;
    uint32_t listed = 0;
    ssize_t length = file != 0 ? getline(&line, &size, file) : -1;
    while (length > 0)
    {
        const bool processor_line = lane_test_cpuinfo_key(line, (size_t)length, "processor");
        const bool package_line = lane_test_cpuinfo_key(line, (size_t)length, "physical id");
        const bool core_line = lane_test_cpuinfo_key(line, (size_t)length, "core id");
        const int64_t value = processor_line || package_line || core_line ? lane_test_cpuinfo_value(line, (size_t)length) : -1;
        processor = processor_line ? value : processor;
        package = processor_line ? -1 : package_line ? value : package;
        if (core_line && processor >= 0 && processor < room && package >= 0 && value >= 0 && cores[processor] == UINT64_MAX)
        {
            cores[processor] = (uint64_t)package << 32 | (uint64_t)value;
            listed += 1;
        }
        length = getline(&line, &size, file);
    }
    free(line);
    return listed;
}

// /proc/cpuinfo's cores, by lane_test_cpuinfo_read.
static uint32_t lane_test_cpuinfo(uint64_t* cores)
{
    FILE* file = fopen("/proc/cpuinfo", "r");
    const uint32_t listed = lane_test_cpuinfo_read(file, cores);
    if (file != 0)
    {
        fclose(file);
    }
    return listed;
}

// The distinct cores of the CPUs in `mask` (BUSTER_LANE_CPU_MASK_WORDS words) by lane_test_cpuinfo's `cores`, and in
// *unlisted how many of those CPUs it doesn't list, for which it is no oracle.
static uint32_t lane_test_cpuinfo_cores(const uint64_t* cores, const uint64_t* mask, uint32_t* unlisted)
{
    uint32_t distinct = 0;
    *unlisted = 0;
    for (uint32_t cpu = 0; cpu < BUSTER_LANE_CPU_MASK_WORDS * 64; cpu += 1)
    {
        if (((mask[cpu / 64] >> (cpu % 64)) & 1) != 0)
        {
            bool seen = false;
            for (uint32_t earlier = 0; earlier < cpu; earlier += 1)
            {
                seen = seen || (((mask[earlier / 64] >> (earlier % 64)) & 1) != 0 && cores[earlier] == cores[cpu]);
            }
            distinct += cores[cpu] != UINT64_MAX && !seen ? 1 : 0;
            *unlisted += cores[cpu] == UINT64_MAX ? 1 : 0;
        }
    }
    return distinct;
}

_Static_assert(BUSTER_LANE_CPU_MASK_WORDS * 64 == 1024, "lane_test_cpuinfo_controls' texts name the room's last processor and the next");

// lane_test_cpuinfo_read on texts of /proc/cpuinfo's shape, read through fmemopen (#933's review). First four
// processors' blocks of every line a Zen 5's has, each flags line 1,024 names with digits: two packages, processors 0
// and 2 one core's siblings. Then texts of one processor each, which must give it its entry or none:
//   - blanks around the colon and none, a last line without its newline, and leading zeros;
//   - the room's last processor and the next, and the largest package and core and the next of each;
//   - numbers past 64 bits, and values that aren't one decimal (a sign, letters, a fraction, two numbers, none);
//   - keys that only start like the three, or that start with them, and a line without a colon;
//   - a processor line that doesn't read, which mustn't leave its core to the processor before;
//   - a processor without a package, a core before any processor, and a processor listed twice.
static uint64_t lane_test_cpuinfo_controls(void)
{
    uint64_t failures = 0;
    uint64_t* cores = malloc(BUSTER_LANE_CPU_MASK_WORDS * 64 * sizeof(uint64_t));
    char* text = 0;
    size_t text_length = 0;
    FILE* stream = cores != 0 ? open_memstream(&text, &text_length) : 0;
    static const char* const names[] = { "fpu", "sse4_1", "sse4_2", "x2apic", "3dnowprefetch", "avx2", "avx512f", "avx512dq",
                                          "avx512_bf16", "avx512_vp2intersect", "avx_vnni", "f16c", "x86_64", "v_vmsave_vmload", "perfctr_core", "cpb" };
    static const uint32_t packages[] = { 0, 0, 0, 1 };
    static const uint32_t core_ids[] = { 0, 1, 0, 0 };
    for (uint32_t processor = 0; stream != 0 && processor < BUSTER_ARRAY_LENGTH(packages); processor += 1)
    {
        fprintf(stream, "processor\t: %u\nvendor_id\t: AuthenticAMD\ncpu family\t: 26\nmodel\t\t: 68\nmodel name\t: AMD Ryzen 7 9700X 8-Core Processor\n", processor);
        fprintf(stream, "stepping\t: 0\nmicrocode\t: 0xb404023\ncpu MHz\t\t: 4950.000\ncache size\t: 1024 KB\nphysical id\t: %u\nsiblings\t: 16\n", packages[processor]);
        fprintf(stream, "core id\t\t: %u\ncpu cores\t: 8\napicid\t\t: 15\ninitial apicid\t: 15\nfpu\t\t: yes\ncpuid level\t: 16\nflags\t\t:", core_ids[processor]);
        for (uint32_t i = 0; i < 64 * BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            fprintf(stream, " %s", names[i % BUSTER_ARRAY_LENGTH(names)]);
        }
        fprintf(stream, "\nbugs\t\t: sysret_ss_attrs spectre_v1 spectre_v2 spec_store_bypass srso\nbogomips\t: 7586.11\nTLB size\t: 3072 4K pages\n");
        fprintf(stream, "clflush size\t: 64\naddress sizes\t: 48 bits physical, 48 bits virtual\npower management: ts ttp tm hwpstate cpb eff_freq_ro [13] [14]\n\n");
    }
    const bool written = stream != 0 && fclose(stream) == 0;
    FILE* blocks = written ? fmemopen(text, text_length, "r") : 0;
    const uint32_t blocks_listed = blocks != 0 ? lane_test_cpuinfo_read(blocks, cores) : 0;
    const uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0x1f };
    uint32_t unlisted = 0;
    const uint32_t distinct = blocks != 0 ? lane_test_cpuinfo_cores(cores, mask, &unlisted) : 0;
    failures += blocks != 0 && blocks_listed == 4 && cores[0] == 0 && cores[1] == 1 && cores[2] == 0 && cores[3] == UINT64_C(1) << 32 && cores[4] == UINT64_MAX ? 0 : 1;
    failures += blocks != 0 && distinct == 3 && unlisted == 1 ? 0 : 1;
    if (blocks != 0)
    {
        fclose(blocks);
    }
    free(text);
    typedef struct LaneTestCpuinfo LaneTestCpuinfo;
    struct LaneTestCpuinfo
    {
        const char* text;
        // The processor the text names, the entry it must get (UINT64_MAX for none) and the processors it lists.
        uint32_t processor;
        uint64_t entry;
        uint32_t listed;
    };
    static const LaneTestCpuinfo cases[] = {
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 2\n", 3, UINT64_C(1) << 32 | 2, 1 },
        { "processor: 3\nphysical id:1\t\ncore id   :  2 \n", 3, UINT64_C(1) << 32 | 2, 1 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 2", 3, UINT64_C(1) << 32 | 2, 1 },
        { "processor\t: 003\nphysical id\t: 01\ncore id\t\t: 0002\n", 3, UINT64_C(1) << 32 | 2, 1 },
        { "processor\t: 1023\nphysical id\t: 4294967294\ncore id\t\t: 4294967294\n", 1023, UINT64_C(0xfffffffefffffffe), 1 },
        { "processor\t: 1024\nphysical id\t: 0\ncore id\t\t: 0\n", 0, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 4294967295\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 4294967295\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 18446744073709551616\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 999999999999999999999999999999999999\n", 3, UINT64_MAX, 0 },
        { "processor\t: 99999999999999999999\nphysical id\t: 1\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: -2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: +2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 2x\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 0x2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: one\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3.5\nphysical id\t: 1\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 2 3\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t:\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: \n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore idx\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncpu core id\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\n core id\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\nCore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore  id\t: 2\n", 3, UINT64_MAX, 0 },
        { "processors\t: 3\nphysical id\t: 1\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical ids\t: 1\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t 3\nphysical id\t: 1\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\nprocessor\t: x\ncore id\t\t: 2\n", 3, UINT64_MAX, 0 },
        { "processor\t: 2\nphysical id\t: 1\ncore id\t\t: 0\nprocessor\t: 3\ncore id\t\t: 2\n", 2, UINT64_C(1) << 32, 1 },
        { "core id\t\t: 2\nprocessor\t: 3\nphysical id\t: 1\n", 3, UINT64_MAX, 0 },
        { "processor\t: 3\nphysical id\t: 1\ncore id\t\t: 2\nprocessor\t: 3\nphysical id\t: 0\ncore id\t\t: 0\n", 3, UINT64_C(1) << 32 | 2, 1 },
    };
    for (uint32_t i = 0; cores != 0 && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char buffer[256];
        const size_t length = strlen(cases[i].text);
        memcpy(buffer, cases[i].text, length);
        FILE* file = fmemopen(buffer, length, "r");
        const uint32_t listed = file != 0 ? lane_test_cpuinfo_read(file, cores) : UINT32_MAX;
        bool right = listed == cases[i].listed;
        for (uint32_t cpu = 0; cpu < BUSTER_LANE_CPU_MASK_WORDS * 64; cpu += 1)
        {
            right = right && cores[cpu] == (cpu == cases[i].processor ? cases[i].entry : UINT64_MAX);
        }
        if (!right)
        {
            fprintf(stderr, "cpuinfo: text %u read wrong (it listed %u)\n", i, listed);
        }
        failures += right ? 0 : 1;
        if (file != 0)
        {
            fclose(file);
        }
    }
    failures += cores != 0 ? 0 : 1;
    free(cores);
    printf("cpuinfo: four processors with 1,024 flags each and %u texts of one, %lu wrong\n", (uint32_t)BUSTER_ARRAY_LENGTH(cases), (unsigned long)failures);
    return failures;
}

// The cores of the CPUs this process may run on by /proc/cpuinfo (lane_test_cpuinfo), or 0 where it doesn't list every
// one of them.
static __attribute__((unused)) uint32_t lane_test_process_cores(void)
{
    uint64_t* cores = malloc(BUSTER_LANE_CPU_MASK_WORDS * 64 * sizeof(uint64_t));
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool known = cores != 0 && syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
    uint32_t unlisted = 0;
    const uint32_t distinct = known && lane_test_cpuinfo(cores) != 0 ? lane_test_cpuinfo_cores(cores, mask, &unlisted) : 0;
    free(cores);
    return unlisted == 0 ? distinct : 0;
}

// Among the CPUs this process may run on, by /proc/cpuinfo (lane_test_cpuinfo), which must list each: a core's two
// siblings, into `siblings`, and the first of them with a CPU of another core, into `apart` (BUSTER_LANE_CPU_MASK_WORDS
// words each). Returns whether it found both.
static __attribute__((unused)) bool lane_test_sibling_masks(uint64_t* siblings, uint64_t* apart)
{
    uint64_t* ids = malloc(BUSTER_LANE_CPU_MASK_WORDS * 64 * sizeof(uint64_t));
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool known = ids != 0 && syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed) > 0 && lane_test_cpuinfo(ids) != 0;
    uint32_t unlisted = 0;
    const bool listed = known && lane_test_cpuinfo_cores(ids, allowed, &unlisted) != 0 && unlisted == 0;
    uint32_t first = UINT32_MAX;
    uint32_t sibling = UINT32_MAX;
    uint32_t other = UINT32_MAX;
    for (uint32_t a = 0; listed && first == UINT32_MAX && a < BUSTER_LANE_CPU_MASK_WORDS * 64; a += 1)
    {
        for (uint32_t b = a + 1; ((allowed[a / 64] >> (a % 64)) & 1) != 0 && first == UINT32_MAX && b < BUSTER_LANE_CPU_MASK_WORDS * 64; b += 1)
        {
            const bool both = ((allowed[b / 64] >> (b % 64)) & 1) != 0 && ids[a] == ids[b];
            first = both ? a : first;
            sibling = both ? b : sibling;
        }
    }
    for (uint32_t c = 0; first != UINT32_MAX && other == UINT32_MAX && c < BUSTER_LANE_CPU_MASK_WORDS * 64; c += 1)
    {
        other = ((allowed[c / 64] >> (c % 64)) & 1) != 0 && ids[c] != ids[first] ? c : other;
    }
    const bool found = first != UINT32_MAX && other != UINT32_MAX;
    memset(siblings, 0, BUSTER_LANE_CPU_MASK_WORDS * sizeof(uint64_t));
    memset(apart, 0, BUSTER_LANE_CPU_MASK_WORDS * sizeof(uint64_t));
    if (found)
    {
        siblings[first / 64] |= UINT64_C(1) << (first % 64);
        siblings[sibling / 64] |= UINT64_C(1) << (sibling % 64);
        apart[first / 64] |= UINT64_C(1) << (first % 64);
        apart[other / 64] |= UINT64_C(1) << (other % 64);
    }
    free(ids);
    return found;
}

// Writes CPU `cpu`'s sibling list, `length` bytes at `list`, in a test topology under `root`, where lane_cores reads it
// in sysfs (cpuN/topology/core_cpus_list). Returns whether it could.
static bool lane_test_topology_write(const char* root, uint32_t cpu, const char* list, uint64_t length)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/cpu%u", root, cpu);
    bool written = mkdir(path, 0700) == 0 || errno == EEXIST;
    snprintf(path, sizeof(path), "%s/cpu%u/topology", root, cpu);
    written = written && (mkdir(path, 0700) == 0 || errno == EEXIST);
    snprintf(path, sizeof(path), "%s/cpu%u/topology/core_cpus_list", root, cpu);
    const int fd = written ? open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
    written = fd >= 0 && write(fd, list, length) == (ssize_t)length;
    if (fd >= 0)
    {
        close(fd);
    }
    return written;
}

// Removes a test topology under `root`: its CPUs' lists and directories, below `cpus`, then the root.
static void lane_test_topology_remove(const char* root, uint32_t cpus)
{
    char path[PATH_MAX];
    for (uint32_t cpu = 0; cpu < cpus; cpu += 1)
    {
        snprintf(path, sizeof(path), "%s/cpu%u/topology/core_cpus_list", root, cpu);
        unlink(path);
        snprintf(path, sizeof(path), "%s/cpu%u/topology", root, cpu);
        rmdir(path);
        snprintf(path, sizeof(path), "%s/cpu%u", root, cpu);
        rmdir(path);
    }
    rmdir(root);
}

// A new directory for a test topology, under $TMPDIR (or /tmp), into `root`. Returns whether it could make it.
static bool lane_test_topology_root(char* root, uint64_t size)
{
    const char* temporary = getenv("TMPDIR");
    snprintf(root, size, "%s/buster-test-lanes-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    return mkdtemp(root) != 0;
}

// A test topology under `root` of the CPUs in `allowed` (BUSTER_LANE_CPU_MASK_WORDS words): their siblings in groups of
// `size`, in order, or with `size` 0 all of them one core. Returns its cores, or 0 if it couldn't write it, and in *end
// the CPU past the last.
static uint32_t lane_test_topology_process(const char* root, const uint64_t* allowed, uint32_t size, uint32_t* end)
{
    uint32_t cpus[BUSTER_LANE_CPU_MASK_WORDS * 64];
    uint32_t count = 0;
    for (uint32_t cpu = 0; cpu < BUSTER_LANE_CPU_MASK_WORDS * 64; cpu += 1)
    {
        if (((allowed[cpu / 64] >> (cpu % 64)) & 1) != 0)
        {
            cpus[count] = cpu;
            count += 1;
        }
    }
    *end = count != 0 ? cpus[count - 1] + 1 : 0;
    bool written = count != 0;
    char list[128];
    for (uint32_t i = 0; written && i < count; i += 1)
    {
        int length = size == 0 ? snprintf(list, sizeof(list), "%u-%u", cpus[0], cpus[count - 1]) : 0;
        const uint32_t first = size != 0 ? i - i % size : 0;
        for (uint32_t sibling = first; size != 0 && sibling < first + size && sibling < count; sibling += 1)
        {
            length += snprintf(list + length, sizeof(list) - (uint64_t)length, sibling == first ? "%u" : ",%u", cpus[sibling]);
        }
        written = lane_test_topology_write(root, cpus[i], list, (uint64_t)length);
    }
    return written ? (size == 0 ? 1 : (count + size - 1) / size) : 0;
}

// lane_cores and lane_core_count (#905): see the file's comment.
static uint64_t lane_test_cores(void)
{
    uint64_t failures = 0;
    char root[PATH_MAX];
    bool made = lane_test_topology_root(root, sizeof(root));
    // The topology: SMT siblings n and n + 8 (as Linux numbers AMD's), adjacent pairs (as Intel's), single threads (as
    // efficient cores), CPUs without a list, lists that don't parse or leave their CPU out, one longer than the read's
    // buffer, one with a newline, one whose sibling's list doesn't parse and must be skipped, and siblings past a mask's
    // first words.
    char list[700];
    for (uint32_t cpu = 0; made && cpu < 48; cpu += 1)
    {
        const int length = cpu < 16 ? snprintf(list, sizeof(list), "%u,%u", cpu % 8, cpu % 8 + 8) :
                           cpu < 32 ? snprintf(list, sizeof(list), "%u-%u", cpu & ~1u, (cpu & ~1u) + 1) :
                                      snprintf(list, sizeof(list), "%u", cpu);
        made = lane_test_topology_write(root, cpu, list, (uint64_t)length);
    }
    typedef struct LaneTestCoresList LaneTestCoresList;
    struct LaneTestCoresList
    {
        uint32_t cpu;
        const char* list;
    };
    static const LaneTestCoresList lists[] = {
        { 50, "50-" },
        { 51, "x" },
        { 52, "" },
        { 53, "52-53,99999999999" },
        { 60, "61" },
        { 61, "60-61" },
        { 62, "63-62" },
        { 63, "62,63x" },
        { 64, "64,65\n" },
        { 65, "64-65\n" },
        { 70, "70,71" },
        { 71, "garbage" },
        { 81, "81" },
        { 130, "130,194" },
        { 194, "130,194" },
    };
    for (uint32_t i = 0; made && i < BUSTER_ARRAY_LENGTH(lists); i += 1)
    {
        made = lane_test_topology_write(root, lists[i].cpu, lists[i].list, strlen(lists[i].list));
    }
    uint64_t used = 0;
    while (used + 3 < 600)
    {
        memcpy(list + used, "80,", 3);
        used += 3;
    }
    memcpy(list + used, "80", 2);
    made = made && lane_test_topology_write(root, 80, list, used + 2);
    failures += made ? 0 : 1;
    typedef struct LaneTestCores LaneTestCores;
    struct LaneTestCores
    {
        uint64_t mask[4];
        uint32_t words;
        uint32_t expected;
    };
    static const LaneTestCores cases[] = {
        { { 0xffff }, 1, 8 },
        { { 0xf }, 1, 4 },
        { { 0x101 }, 1, 1 },
        { { 0x100 }, 1, 1 },
        { { 0x300 }, 1, 2 },
        { { 0xf0f }, 1, 4 },
        { { 0xffff0000 }, 1, 8 },
        { { UINT64_C(1) << 17 }, 1, 1 },
        { { UINT64_C(0xffffffffffff) }, 1, 32 },
        { { UINT64_C(3) << 48 }, 1, 2 },
        { { UINT64_C(7) << 50 }, 1, 3 },
        { { UINT64_C(3) << 52 }, 1, 2 },
        { { UINT64_C(1) << 53 }, 1, 1 },
        { { UINT64_C(3) << 60 }, 1, 2 },
        { { UINT64_C(3) << 62 }, 1, 2 },
        { { 0, 3 }, 2, 1 },
        { { 0, UINT64_C(3) << 6 }, 2, 1 },
        { { 0, UINT64_C(1) << 7 }, 2, 1 },
        { { 0, UINT64_C(3) << 16 }, 2, 2 },
        { { 0, 0, UINT64_C(1) << 2, UINT64_C(1) << 2 }, 4, 1 },
        { { 0, 0, UINT64_C(1) << 2 }, 3, 1 },
        { { 0 }, 4, 1 },
    };
    for (uint32_t i = 0; made && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const uint32_t cores = lane_cores(root, cases[i].mask, cases[i].words);
        if (cores != cases[i].expected)
        {
            printf("  cores: case %u counted %u cores, %u expected\n", i, cores, cases[i].expected);
            failures += 1;
        }
    }
    // Without a topology, each CPU is a core of its own.
    static const uint64_t four[1] = { 0xf };
    failures += lane_cores("/nonexistent-buster-topology", four, 1) == 4 ? 0 : 1;
    if (made)
    {
        lane_test_topology_remove(root, 195);
    }
    // This process's CPUs: sysfs' cores against /proc/cpuinfo's, which must list each of them; then a core's two siblings
    // alone, one core, and with a CPU of another core, two, where the process has them.
    const uint32_t expected = lane_test_process_cores();
    const uint32_t counted = lane_core_count();
    failures += expected != 0 && counted != expected ? 1 : 0;
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint64_t siblings[BUSTER_LANE_CPU_MASK_WORDS];
    uint64_t apart[BUSTER_LANE_CPU_MASK_WORDS];
    const bool paired = syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed) > 0 && lane_test_sibling_masks(siblings, apart);
    if (paired)
    {
        failures += syscall(SYS_sched_setaffinity, 0, sizeof(siblings), siblings) == 0 && lane_core_count() == 1 ? 0 : 1;
        failures += syscall(SYS_sched_setaffinity, 0, sizeof(apart), apart) == 0 && lane_core_count() == 2 ? 0 : 1;
        failures += syscall(SYS_sched_setaffinity, 0, sizeof(allowed), allowed) == 0 ? 0 : 1;
    }
    const char* oracle = expected != 0 ? "/proc/cpuinfo's too" : "unchecked: /proc/cpuinfo doesn't list them";
    const char* restricted = paired ? "a core's two siblings and two cores' CPUs" : "no core with two of its CPUs";
    printf("cores: %u masks of a test topology, this process's %u cores (%s), %s, %lu wrong\n", (uint32_t)BUSTER_ARRAY_LENGTH(cases) + 1, counted, oracle,
           restricted, (unsigned long)failures);
    return failures;
}

// The CPUs this thread may run on, counted here from its affinity mask: an answer to lane_cpu_count's of its own, so
// that a group's answer is checked against a count that doesn't come from it.
static uint32_t lane_test_cpu_count(void)
{
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const long written = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask);
    const uint32_t words = written > 0 ? (uint32_t)((uint64_t)written / 8) : 0;
    uint32_t result = 0;
    for (uint32_t word = 0; word < words && word < BUSTER_LANE_CPU_MASK_WORDS; word += 1)
    {
        for (uint32_t bit = 0; bit < 64; bit += 1)
        {
            result += (mask[word] & (UINT64_C(1) << bit)) != 0 ? 1u : 0u;
        }
    }
    return result != 0 ? result : 1;
}

static void lane_test_shared_fail(uint64_t* failures, bool condition, const char* what)
{
    *failures += condition ? 0 : 1;
    if (!condition)
    {
        printf("  shared CPUs: %s\n", what);
    }
}

// A group shares CPUs when it has more lanes than the process's and pins no CPU of its own (lane_group_shares_cpus), and
// each lane of such a group then sleeps at once rather than hold the CPU a lane that hasn't arrived needs: a wait in
// mwaitx, or the spin that takes its place in a build without it, would cost every sync a timeslice. Two lanes on one
// CPU, which this thread's mask is cut to here, share it, and lane 0 stalls before their sync: the lane that waited must
// have slept. Two lanes on the process's own CPUs, where it has more than one, share none.
static uint64_t lane_test_shared_cpus(void)
{
    uint64_t failures = 0;
    const uint32_t cpus = lane_test_cpu_count();
    lane_test_shared_fail(&failures, !lane_group_shares_cpus(0, false) && !lane_group_shares_cpus(1, false), "a group of one lane shares CPUs");
    lane_test_shared_fail(&failures, !lane_group_shares_cpus(cpus, false), "a group of the process's CPUs, and no more, shares them");
    lane_test_shared_fail(&failures, !lane_group_shares_cpus(cpus + 1, true) && !lane_group_shares_cpus(2, true), "a group pinned to CPUs shares them");
    if (cpus < BUSTER_LANE_MAX_COUNT)
    {
        lane_test_shared_fail(&failures, lane_group_shares_cpus(cpus + 1, false), "a group of more lanes than the process's CPUs doesn't share them");
    }
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint64_t one[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool saved = syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed) > 0;
    uint32_t cpu = 0;
    while (saved && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && (allowed[cpu / 64] & (UINT64_C(1) << (cpu % 64))) == 0)
    {
        cpu += 1;
    }
    if (cpu < BUSTER_LANE_CPU_MASK_WORDS * 64)
    {
        one[cpu / 64] = UINT64_C(1) << (cpu % 64);
    }
    const bool cut = saved && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && syscall(SYS_sched_setaffinity, 0, sizeof(one), one) == 0;
    lane_test_shared_fail(&failures, cut, "this thread's CPUs couldn't be cut to one");
    if (cut)
    {
        LaneTest shared = { 0 };
        uint64_t sleeps = 0;
        failures += lane_test_run(&shared, LANE_TEST_SHARED, 2, &sleeps);
        lane_test_shared_fail(&failures, shared.shares[0] && shared.shares[1], "two lanes on one CPU didn't read their group as sharing it");
        // The lane that waited must have slept on its futex, in either build: with mwaitx, a wait in it would hold the
        // CPU the stalling lane needs until the timer, and without it the pause spin would.
        lane_test_shared_fail(&failures, sleeps != 0, "a lane that waited on a shared CPU didn't sleep");
    }
    const bool restored = cut && syscall(SYS_sched_setaffinity, 0, sizeof(allowed), allowed) == 0;
    lane_test_shared_fail(&failures, restored, "this thread's CPUs couldn't be restored");
    if (cpus > 1)
    {
        LaneTest apart = { 0 };
        uint64_t sleeps = 0;
        failures += lane_test_run(&apart, LANE_TEST_SHARED, 2, &sleeps);
        lane_test_shared_fail(&failures, !apart.shares[0] && !apart.shares[1], "two lanes on the process's CPUs read their group as sharing them");
    }
    printf("shared CPUs: %u CPUs, a group of one more than them, pinned and unpinned, a group of two cut to one CPU%s, %lu wrong\n", cpus,
           cpus > 1 ? " and of two on them" : "", (unsigned long)failures);
    return failures;
}

// What a session's lanes share (#905): the lanes it should run, what each lane saw and did, and the failures each
// counted.
typedef struct LaneSessionTest LaneSessionTest;
struct LaneSessionTest
{
    pid_t caller;
    uint32_t expected;
    // The lanes the session asks for (lane_session_asked).
    uint32_t asked;
    uint32_t calls[BUSTER_LANE_TEST_MAX_LANES];
    pid_t threads[BUSTER_LANE_TEST_MAX_LANES];
    uint64_t failures[BUSTER_LANE_TEST_MAX_LANES];
    // A flow of rounds (LaneFlow), lane 0's, which the others follow, and each lane's calls of each of its two kinds.
    LaneFlow flow;
    uint32_t round_calls[2][BUSTER_LANE_TEST_MAX_LANES];
    uint32_t parts[BUSTER_LANE_TEST_SESSION_PARTS];
    uint32_t forbidden_calls;
    // Whether lane 0 forks a child in the flow.
    bool forks;
};

// The thread that handled the sessions check's signal.
static volatile sig_atomic_t lane_test_signalled;

static void lane_test_session_signal(int signal_number)
{
    (void)signal_number;
    lane_test_signalled = (sig_atomic_t)syscall(SYS_gettid);
}

static void lane_test_session_forbidden(void* argument)
{
    LaneSessionTest* test = argument;
    __atomic_add_fetch(&test->forbidden_calls, 1, __ATOMIC_RELAXED);
}

// A flow or an operation that each of its lanes counts itself in.
static void lane_test_session_count(void* argument)
{
    LaneSessionTest* test = argument;
    if (lane_index() < BUSTER_LANE_TEST_MAX_LANES)
    {
        test->calls[lane_index()] += 1;
    }
}

// An operation on a lane alone: it must run there, as lane 0 of 1.
static void lane_test_session_alone(void* argument)
{
    uint32_t* calls = argument;
    *calls += lane_count() == 1 && lane_index() == 0 ? 1 : 100;
}

// A flow's round of `kind` (1 or 2): each of its lanes counts itself, and they sync and broadcast in their own group; a
// nested run of a round of several lanes is refused.
static void lane_test_session_round(LaneSessionTest* test, uint32_t kind)
{
    const uint32_t index = lane_index();
    test->round_calls[kind - 1][index < BUSTER_LANE_TEST_MAX_LANES ? index : 0] += 1;
    lane_sync();
    const uint32_t lanes = lane_count();
    uint64_t failures = lane_broadcast(index + kind, lanes - 1) != lanes - 1 + kind ? 1 : 0;
    failures += lanes > 1 && lane_run(2, lane_test_session_forbidden, test, 0) ? 1 : 0;
    test->failures[index < BUSTER_LANE_TEST_MAX_LANES ? index : 0] += failures;
}

// A narrowing of every lane of the current group to `lanes` of them: the narrowed lanes sync, broadcast and take every
// part once in their own group, and the others don't enter; then every lane has its lane back.
static uint64_t lane_test_session_narrow(LaneSessionTest* test, uint32_t lanes)
{
    const Lane outer = thread_context.lane;
    const uint32_t expected = lanes == 0 ? 1 : lanes < outer.count ? lanes : outer.count;
    uint64_t failures = 0;
    if (lane_narrow(lanes))
    {
        failures += lane_count() != expected || lane_index() != outer.index ? 1 : 0;
        lane_sync();
        failures += lane_broadcast(lane_index() * 3, expected - 1) != (expected - 1) * 3 ? 1 : 0;
        for (uint64_t part = lane_take(BUSTER_LANE_TEST_SESSION_PARTS); part < BUSTER_LANE_TEST_SESSION_PARTS; part = lane_take(BUSTER_LANE_TEST_SESSION_PARTS))
        {
            __atomic_add_fetch(&test->parts[part], 1, __ATOMIC_RELAXED);
        }
    }
    else
    {
        failures += outer.index < expected ? 1 : 0;
    }
    lane_widen();
    failures += thread_context.lane.index != outer.index || thread_context.lane.count != outer.count || thread_context.lane.group != outer.group ? 1 : 0;
    // The narrowed lanes took every part once: lane 0 checks once every lane is past them, and clears them before the
    // next narrowing.
    lane_park();
    if (lane_index() == 0)
    {
        for (uint32_t part = 0; part < BUSTER_LANE_TEST_SESSION_PARTS; part += 1)
        {
            failures += test->parts[part] != 1 ? 1 : 0;
            test->parts[part] = 0;
        }
    }
    lane_park();
    return failures;
}

// A lazy session's flow whose lane 0 begins a flow of rounds before it settles (lane_flow_begin keeps its group with the
// capacity's count, as the C driver's lane 0 does): its rounds run on the lanes the session settled at, with one per
// core fewer, each once, and lane 0 is a lane of them again at the flow's end (lane_flow_group).
static void lane_test_session_early_flow(void* argument)
{
    LaneSessionTest* test = argument;
    const uint32_t index = lane_index();
    uint64_t failures = 0;
    if (index == 0)
    {
        lane_flow_begin(&test->flow);
        failures += lane_flow_lanes(&test->flow) != test->expected ? 1 : 0;
        memset(test->round_calls, 0, sizeof(test->round_calls));
        lane_flow_round_begin(&test->flow, 1, test, BUSTER_LANE_TEST_MAX_LANES + 9);
        lane_test_session_round(test, 1);
        lane_flow_round_end(&test->flow);
        for (uint32_t lane = 0; lane < BUSTER_LANE_TEST_MAX_LANES; lane += 1)
        {
            failures += test->round_calls[0][lane] != (lane < test->expected ? 1u : 0u) ? 1 : 0;
        }
        lane_flow_end(&test->flow);
    }
    else
    {
        void* operation = NULL;
        for (uint32_t kind = lane_flow_next(&test->flow, &operation); kind != 0; kind = lane_flow_next(&test->flow, &operation))
        {
            if (operation != NULL)
            {
                lane_test_session_round(operation, kind);
            }
            lane_widen();
        }
    }
    failures += lane_count() != test->expected || lane_session_asked() != test->asked ? 1 : 0;
    if (index < BUSTER_LANE_TEST_MAX_LANES)
    {
        test->calls[index] += 1;
        test->failures[index] += failures;
    }
}

// A session's flow (see the file's comment), which every lane runs.
static void lane_test_session_main(void* argument)
{
    LaneSessionTest* test = argument;
    // A lazy session's lane 0 learns how its lanes started before it counts on them; the others exist only once they did.
    lane_session_settle();
    const uint32_t index = lane_index();
    const uint32_t lanes = lane_count();
    uint64_t failures = lanes != test->expected || index >= lanes || lane_session_current() == 0 || lane_session_asked() != test->asked ? 1 : 0;
    if (index < BUSTER_LANE_TEST_MAX_LANES)
    {
        test->calls[index] += 1;
        test->threads[index] = (pid_t)syscall(SYS_gettid);
        failures += (index == 0) != (test->threads[index] == test->caller) ? 1 : 0;
    }
    sigset_t mask;
    failures += pthread_sigmask(SIG_SETMASK, 0, &mask) != 0 ? 1 : 0;
    failures += index != 0 && (sigismember(&mask, SIGUSR1) != 1 || sigismember(&mask, SIGTERM) != 1 || sigismember(&mask, SIGSEGV) != 0) ? 1 : 0;
    failures += index == 0 && sigismember(&mask, SIGUSR1) != 0 ? 1 : 0;
    lane_park();
    // A signal sent to the process, while the other lanes are parked: lane 0 alone takes it.
    if (index == 0)
    {
        lane_test_signalled = 0;
        failures += kill(getpid(), SIGUSR1) != 0 || lane_test_signalled != test->threads[0] ? 1 : 0;
    }
    lane_park();
    for (uint32_t source = 0; source < lanes; source += 1)
    {
        failures += lane_park_broadcast(index * 11 + 5, source) != source * 11 + 5 ? 1 : 0;
    }
    static const uint32_t narrowings[] = { 1, 2, 3, BUSTER_LANE_TEST_MAX_LANES, BUSTER_LANE_TEST_MAX_LANES + 5, 0 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(narrowings); i += 1)
    {
        failures += lane_test_session_narrow(test, narrowings[i]);
    }
    if (lane_narrow(lanes))
    {
        failures += lane_test_session_narrow(test, 2);
    }
    lane_widen();
    // lane_run from a group of several that serves nothing is refused; on a lane alone, it runs there. A team, a cold
    // group of several and a session of its own would create threads: refused.
    failures += lanes > 1 && lane_run(2, lane_test_session_forbidden, test, 0) ? 1 : 0;
    const Lane group = lane_alone();
    uint32_t alone_calls = 0;
    failures += lane_run(4, lane_test_session_alone, &alone_calls, 0) && alone_calls == 1 ? 0 : 1;
    LaneTeam team = { 0 };
    failures += lane_team_start(&team, 2, 0) ? 1 : 0;
    lane_team_stop(&team);
    failures += lane_run_cold(2, lane_test_session_forbidden, test, 0) ? 1 : 0;
    LaneSession nested;
    failures += lane_session_run(&nested, 2, 0, lane_test_session_forbidden, test, 0) != 0 ? 1 : 0;
    lane_restore(group);
    // A flow of rounds (LaneFlow): lane 0 runs rounds of 1, 3, every lane and more lanes than the session has, of each of
    // two kinds, its part itself; the others run each by its kind on its lanes; each round's lanes run it once, and lane
    // 0 is a lane of the session's group again at the end.
    static const uint32_t rounds[] = { 1, 3, BUSTER_LANE_TEST_MAX_LANES, BUSTER_LANE_TEST_MAX_LANES + 9 };
    if (index == 0)
    {
        lane_flow_begin(&test->flow);
        failures += lane_count() != 1 || lane_flow_lanes(&test->flow) != lanes ? 1 : 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rounds); i += 1)
        {
            const uint32_t round_lanes = rounds[i] < lanes ? rounds[i] : lanes;
            for (uint32_t kind = 1; kind <= 2; kind += 1)
            {
                memset(test->round_calls, 0, sizeof(test->round_calls));
                lane_flow_round_begin(&test->flow, kind, test, rounds[i]);
                lane_test_session_round(test, kind);
                lane_flow_round_end(&test->flow);
                for (uint32_t lane = 0; lane < BUSTER_LANE_TEST_MAX_LANES; lane += 1)
                {
                    failures += test->round_calls[kind - 1][lane] != (lane < round_lanes ? 1u : 0u) || test->round_calls[2 - kind][lane] != 0 ? 1 : 0;
                }
                failures += lane_count() != 1 ? 1 : 0;
            }
        }
        lane_flow_end(&test->flow);
        failures += lane_count() != lanes || lane_index() != 0 ? 1 : 0;
    }
    else
    {
        void* argument = NULL;
        for (uint32_t kind = lane_flow_next(&test->flow, &argument); kind != 0; kind = lane_flow_next(&test->flow, &argument))
        {
            if (argument != NULL)
            {
                lane_test_session_round(argument, kind);
            }
            lane_widen();
        }
        failures += lane_count() != lanes ? 1 : 0;
    }
    // A child forked from lane 0 while the others park: it forgets the session, is one lane, and may start a cold group
    // of its own, which no session forbids there.
    if (index == 0 && test->forks)
    {
        fflush(stdout);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
            const bool forgot = lane_session_current() == 0 && lane_count() == 1 && lane_index() == 0;
#if __has_feature(thread_sanitizer)
            // ThreadSanitizer kills a child that starts a thread after a fork from several, as this one was forked: its
            // group is of one there.
            const uint32_t own_lanes = 1;
#else
            const uint32_t own_lanes = 2;
#endif
            LaneSessionTest own = { 0 };
            const bool ran = lane_run(own_lanes, lane_test_session_count, &own, 0) && own.calls[0] == 1 && own.calls[1] == own_lanes - 1;
            _exit(forgot && ran ? 0 : 1);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        failures += waited && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
    }
    lane_park();
    if (index < BUSTER_LANE_TEST_MAX_LANES)
    {
        test->failures[index] += failures;
    }
}

// Sessions (#905): see the file's comment.
static uint64_t lane_test_sessions(void)
{
    lane_test_watch(BUSTER_ARRAY_LENGTH(lane_test_check_names) + 1, BUSTER_LANE_TEST_MAX_LANES, false);
    alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
    struct sigaction action = { .sa_handler = lane_test_session_signal };
    struct sigaction previous_action;
    const bool handled = sigaction(SIGUSR1, &action, &previous_action) == 0;
    uint64_t failures = handled ? 0 : 1;
    const uint32_t threads_before = lane_test_thread_count();
    const pid_t caller = (pid_t)syscall(SYS_gettid);
    static LaneSession session;
    // Each capacity eagerly, then lazily (lane 1 creating the lanes after it).
    static const uint32_t capacities[] = { 1, 2, 3, BUSTER_LANE_TEST_MAX_LANES, 1, 3, BUSTER_LANE_TEST_MAX_LANES };
    for (uint32_t i = 0; handled && i < BUSTER_ARRAY_LENGTH(capacities); i += 1)
    {
        LaneSessionTest test = {
            .caller = caller,
            .expected = capacities[i],
            .asked = capacities[i],
            .forks = i == 2,
        };
        const uint64_t created = lane_process.threads_created;
        const uint32_t flags = i >= 4 ? BUSTER_LANE_SESSION_LAZY : 0;
        const uint32_t ran = lane_session_run(&session, capacities[i], 0, lane_test_session_main, &test, flags);
        failures += ran != capacities[i] || lane_process.threads_created != created + capacities[i] - 1 ? 1 : 0;
        for (uint32_t lane = 0; lane < BUSTER_LANE_TEST_MAX_LANES; lane += 1)
        {
            failures += test.calls[lane] != (lane < capacities[i] ? 1u : 0u) || test.failures[lane] != 0 ? 1 : 0;
            for (uint32_t earlier = 0; lane < capacities[i] && earlier < lane; earlier += 1)
            {
                failures += test.threads[lane] == test.threads[earlier] ? 1 : 0;
            }
        }
        failures += test.forbidden_calls != 0 || lane_session_current() != 0 ? 1 : 0;
        failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
    }
    // Sessions whose third lane fails to start, eagerly and lazily, and a lazy one whose lane 1 does: the lanes created are
    // joined, and the flow runs on lane 0 alone.
    static const uint32_t failing[] = { 3, 3, 1 };
    for (uint32_t i = 0; handled && i < BUSTER_ARRAY_LENGTH(failing); i += 1)
    {
        LaneSessionTest failed = {
            .caller = caller,
            .expected = 1,
            .asked = BUSTER_LANE_TEST_MAX_LANES,
        };
        const uint64_t created = lane_process.threads_created;
        lane_process.fail_at = failing[i];
        const uint32_t ran = lane_session_run(&session, BUSTER_LANE_TEST_MAX_LANES, 0, lane_test_session_main, &failed, i == 0 ? 0 : BUSTER_LANE_SESSION_LAZY);
        lane_process.fail_at = UINT32_MAX;
        failures += ran != 1 || lane_process.threads_created != created + failing[i] - 1 || failed.calls[0] != 1 || failed.failures[0] != 0 ? 1 : 0;
        for (uint32_t lane = 1; lane < BUSTER_LANE_TEST_MAX_LANES; lane += 1)
        {
            failures += failed.calls[lane] != 0 ? 1 : 0;
        }
        failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
    }
    // Sessions of a lane per core at most (BUSTER_LANE_SESSION_PER_CORE), eager, lazy and lazy with a flow begun before
    // lane 0 settles, of 2 and 8 lanes, on test topologies of this process's CPUs in pairs, in fours and all in one core:
    // as many lanes as the capacity and the cores allow, asked for as many, and a lazy session's lane 1 created even
    // where it runs on one.
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool known = syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed) > 0;
    failures += known ? 0 : 1;
    static const uint32_t sizes[] = { 2, 4, 0 };
    for (uint32_t s = 0; handled && known && s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
    {
        char root[PATH_MAX];
        uint32_t end = 0;
        const bool rooted = lane_test_topology_root(root, sizeof(root));
        const uint32_t cores = rooted ? lane_test_topology_process(root, allowed, sizes[s], &end) : 0;
        failures += cores != 0 ? 0 : 1;
        lane_process.topology = root;
        static const uint32_t per_core[] = { 2, BUSTER_LANE_TEST_MAX_LANES };
        for (uint32_t i = 0; cores != 0 && i < 3 * BUSTER_ARRAY_LENGTH(per_core); i += 1)
        {
            const uint32_t capacity = per_core[i / 3];
            const bool lazy = i % 3 != 0;
            const bool early = i % 3 == 2;
            const uint32_t expected = cores < capacity ? cores : capacity;
            LaneSessionTest test = {
                .caller = caller,
                .expected = expected,
                .asked = expected,
            };
            const uint64_t created = lane_process.threads_created;
            const uint32_t flags = BUSTER_LANE_SESSION_PER_CORE | (lazy ? BUSTER_LANE_SESSION_LAZY : 0);
            const uint32_t ran = lane_session_run(&session, capacity, 0, early ? lane_test_session_early_flow : lane_test_session_main, &test, flags);
            const uint64_t threads = lazy && expected == 1 ? 1 : expected - 1;
            failures += ran != expected || lane_process.threads_created != created + threads ? 1 : 0;
            for (uint32_t lane = 0; lane < BUSTER_LANE_TEST_MAX_LANES; lane += 1)
            {
                failures += test.calls[lane] != (lane < expected ? 1u : 0u) || test.failures[lane] != 0 ? 1 : 0;
            }
            failures += lane_test_threads_stopped(threads_before) ? 0 : 1;
        }
        lane_process.topology = 0;
        if (rooted)
        {
            lane_test_topology_remove(root, end);
        }
    }
    // In a child, a session that leaves its lanes parked at its end, for the child's exit to end; a sanitizer build joins
    // them.
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
        static LaneSession parked;
        LaneSessionTest test = { 0 };
        const uint32_t before = lane_test_thread_count();
        const uint32_t lanes = lane_session_run(&parked, 4, 0, lane_test_session_count, &test, BUSTER_LANE_SESSION_PARK_AT_END);
        const bool joined = BUSTER_SANITIZER_RESOURCES != 0;
        const bool kept = joined ? lane_test_threads_stopped(before) : lane_test_thread_count() == before + 3;
        _exit(lanes == 4 && kept && test.calls[0] == 1 ? 0 : 1);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    failures += waited && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
    if (handled)
    {
        sigaction(SIGUSR1, &previous_action, 0);
    }
    failures += lane_test_unwatch() ? 0 : 1;
    printf("sessions: sessions of 1, 2, 3 and %u lanes, eager and lazy, three whose lanes didn't start, eighteen of a lane per core on three topologies and one "
           "parked at its end, %lu wrong\n",
           BUSTER_LANE_TEST_MAX_LANES, (unsigned long)failures);
    return failures;
}

static int run_lane_tests(int argc, char** argv)
{
    uint64_t rounds = BUSTER_LANE_TEST_DEFAULT_ROUNDS;
    uint64_t seed = 1;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc)
        {
            valid = valid && lane_test_parse_u64(argv[i + 1], &rounds);
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && lane_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else
        {
            valid = false;
        }
    }

    // Everything the checks use, from one arena: the parts of every count for the largest group, the cost prefixes, the
    // records with their references, for every record count one after another, and the parts check's inputs, references
    // and outputs.
    uint64_t total_records = 0;
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(lane_test_record_counts); i += 1)
    {
        total_records += lane_test_record_counts[i];
    }
    uint64_t total_part_items = 0;
    uint64_t most_part_items = 0;
    for (uint64_t i = 0; i < BUSTER_LANE_TEST_PART_INPUT_COUNT; i += 1)
    {
        total_part_items += lane_test_part_inputs[i].count;
        most_part_items = lane_test_part_inputs[i].count > most_part_items ? lane_test_part_inputs[i].count : most_part_items;
    }
    Arena memory = arena_reserve(1ull << 30);
    LaneTest* test = BUSTER_ARENA_PUSH_ARRAY(&memory, LaneTest, 1);
    Range64* parts = BUSTER_ARENA_PUSH_ARRAY(&memory, Range64, (BUSTER_LANE_TEST_COST_PATTERNS + 1) * (BUSTER_LANE_TEST_MAX_RANGE + 1) * BUSTER_LANE_TEST_MAX_LANES);
    uint64_t* cost_prefixes = BUSTER_ARENA_PUSH_ARRAY(&memory, uint64_t, BUSTER_LANE_TEST_COST_PATTERNS * BUSTER_LANE_TEST_MAX_RANGE);
    LaneTestRecord* records = BUSTER_ARENA_PUSH_ARRAY(&memory, LaneTestRecord, total_records);
    LaneTestRecord* reference_exchanged = BUSTER_ARENA_PUSH_ARRAY(&memory, LaneTestRecord, total_records);
    uint32_t* reference_winners = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, total_records);
    LaneTestRecord* exchanged = BUSTER_ARENA_PUSH_ARRAY(&memory, LaneTestRecord, total_records);
    uint32_t* winners = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, total_records);
    // Each item has at most 4 bytes of output.
    uint64_t* part_prefixes = BUSTER_ARENA_PUSH_ARRAY(&memory, uint64_t, total_part_items);
    uint8_t* part_references = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, 4 * total_part_items);
    uint8_t* part_output = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, 4 * most_part_items);
    uint64_t* part_boundaries = BUSTER_ARENA_PUSH_ARRAY(&memory, uint64_t, BUSTER_LANE_MAX_PARTS + 1);
    uint64_t* part_bytes = BUSTER_ARENA_PUSH_ARRAY(&memory, uint64_t, BUSTER_LANE_MAX_PARTS);
    uint32_t* part_takers = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, BUSTER_LANE_MAX_PARTS);
    struct sigaction action = { .sa_handler = lane_test_report_hang };
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-lanes [--rounds N] [--seed N]\n");
    }
    else if (test == 0 || parts == 0 || cost_prefixes == 0 || records == 0 || reference_exchanged == 0 || reference_winners == 0 || exchanged == 0 || winners == 0 ||
             part_prefixes == 0 || part_references == 0 || part_output == 0 || part_boundaries == 0 || part_bytes == 0 || part_takers == 0)
    {
        fprintf(stderr, "out of memory\n");
    }
    else if (sigaction(SIGALRM, &action, 0) != 0)
    {
        fprintf(stderr, "the watchdog's signal handler couldn't be installed\n");
    }
    else
    {
        bool passed = lane_test_watchdog();
        passed = lane_test_arenas() && passed;
        passed = lane_test_teams() == 0 && passed;
        passed = lane_test_cpuinfo_controls() == 0 && passed;
        passed = lane_test_cores() == 0 && passed;
        passed = lane_test_shared_cpus() == 0 && passed;
        passed = lane_test_sessions() == 0 && passed;
        fflush(stdout);

        uint64_t state = seed * BUSTER_LANE_TEST_GOLDEN_RATIO + 1;
        memset(test, 0, sizeof(*test));
        test->parts = parts;
        test->rounds = rounds;
        for (uint32_t pattern = 0; pattern < BUSTER_LANE_TEST_COST_PATTERNS; pattern += 1)
        {
            uint64_t* prefix = cost_prefixes + pattern * BUSTER_LANE_TEST_MAX_RANGE;
            uint64_t sum = 0;
            for (uint64_t i = 0; i < BUSTER_LANE_TEST_MAX_RANGE; i += 1)
            {
                sum += lane_test_cost(&state, pattern, i);
                prefix[i] = sum;
            }
            test->cost_prefix[pattern] = prefix;
        }
        uint64_t offset = 0;
        for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(lane_test_record_counts); i += 1)
        {
            const uint64_t count = lane_test_record_counts[i];
            lane_test_make_records(&state, records + offset, count);
            passed = lane_test_reference(records + offset, count, reference_exchanged + offset, reference_winners + offset) && passed;
            offset += count;
        }
        // The parts check's inputs: each input's cost prefix, and its output in item order.
        uint64_t prefix_offset = 0;
        uint64_t output_offset = 0;
        for (uint64_t i = 0; i < BUSTER_LANE_TEST_PART_INPUT_COUNT; i += 1)
        {
            test->part_prefix_offsets[i] = prefix_offset;
            test->part_output_offsets[i] = output_offset;
            uint64_t sum = 0;
            for (uint64_t item = 0; item < lane_test_part_inputs[i].count; item += 1)
            {
                sum += lane_test_cost(&state, lane_test_part_inputs[i].pattern, item);
                part_prefixes[prefix_offset + item] = sum;
                for (uint64_t byte = 0; byte < lane_test_output_bytes(item); byte += 1)
                {
                    part_references[output_offset] = lane_test_output_byte(item, byte);
                    output_offset += 1;
                }
            }
            prefix_offset += lane_test_part_inputs[i].count;
        }
        test->part_output_offsets[BUSTER_LANE_TEST_PART_INPUT_COUNT] = output_offset;
        test->part_prefixes = part_prefixes;
        test->part_references = part_references;
        test->part_output = part_output;
        test->part_boundaries = part_boundaries;
        test->part_bytes = part_bytes;
        test->part_takers = part_takers;
        memset(part_takers, 0, BUSTER_LANE_MAX_PARTS * sizeof(uint32_t));
        uint64_t part_ranges_checked = 0;
        const uint64_t part_ranges_wrong = lane_test_part_ranges(test, &part_ranges_checked);
        printf("part ranges: %lu checked, %lu wrong\n", (unsigned long)part_ranges_checked, (unsigned long)part_ranges_wrong);
        passed = passed && part_ranges_wrong == 0;

        for (uint64_t c = 0; c < BUSTER_ARRAY_LENGTH(lane_test_counts); c += 1)
        {
            const uint32_t lanes = lane_test_counts[c];
            uint64_t sleeps = 0;
            uint64_t checked = 0;
            uint64_t ranges_wrong = lane_test_run(test, LANE_TEST_RANGES, lanes, &sleeps);
            ranges_wrong += lane_test_check_ranges(test, lanes, &checked);

            uint64_t sync_sleeps = 0;
            const uint64_t syncs_wrong = lane_test_run(test, LANE_TEST_SYNCS, lanes, &sync_sleeps);
            // With more than one lane, the late lanes must have put the others to sleep (if there were enough rounds for one).
            const bool slept = lanes == 1 ? sync_sleeps == 0 : sync_sleeps > 0 || rounds <= BUSTER_LANE_TEST_STALL_INTERVAL / 2;

            const uint64_t alone_wrong = lane_test_run(test, LANE_TEST_ALONE, lanes, &sleeps);

            memset(test->scratch_bases, 0, sizeof(test->scratch_bases));
            uint64_t scratch_wrong = lane_test_run(test, LANE_TEST_SCRATCH, lanes, &sleeps);
            scratch_wrong += lane_test_check_scratch(test, lanes) ? 0 : 1;

            uint64_t exchange_differ = 0;
            offset = 0;
            for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(lane_test_record_counts); i += 1)
            {
                const uint64_t count = lane_test_record_counts[i];
                test->records = records + offset;
                test->record_count = count;
                test->exchanged = exchanged;
                test->winners = winners;
                memset(exchanged, 0xff, count * sizeof(LaneTestRecord));
                memset(winners, 0xff, count * sizeof(uint32_t));
                const uint64_t failures = lane_test_run(test, LANE_TEST_EXCHANGE, lanes, &sleeps);
                const bool same_exchanged = memcmp(exchanged, reference_exchanged + offset, count * sizeof(LaneTestRecord)) == 0;
                const bool same_winners = memcmp(winners, reference_winners + offset, count * sizeof(uint32_t)) == 0;
                if ((failures != 0 || !same_exchanged || !same_winners) && exchange_differ < BUSTER_LANE_TEST_REPORTED)
                {
                    printf("  exchange: %lu records on %u lanes: %lu failures,%s%s\n", (unsigned long)count, lanes, (unsigned long)failures,
                           same_exchanged ? "" : " the partitioned records differ,", same_winners ? "" : " the winners differ");
                }
                exchange_differ += failures != 0 || !same_exchanged || !same_winners ? 1 : 0;
                offset += count;
            }

            const uint64_t parts_wrong = lane_test_run(test, LANE_TEST_PARTS, lanes, &sleeps);
            // The advancers' pauses make lane 0 sleep on the word with more than one lane.
            uint64_t advance_sleeps = 0;
            const uint64_t advance_wrong = lane_test_run(test, LANE_TEST_ADVANCE, lanes, &advance_sleeps);
            const bool advance_slept = lanes == 1 ? advance_sleeps == 0 : advance_sleeps > 0;

            printf("%u lane%s ranges %lu checked, %lu wrong; syncs %lu rounds, %lu sleeps, %lu wrong; alone %lu wrong; scratch %lu wrong; exchange %lu inputs, %lu differ; "
                   "parts %lu inputs, %lu wrong; advance %lu sleeps, %lu wrong\n",
                   lanes, lanes == 1 ? ": " : "s:", (unsigned long)checked, (unsigned long)ranges_wrong, (unsigned long)rounds, (unsigned long)sync_sleeps,
                   (unsigned long)syncs_wrong, (unsigned long)alone_wrong, (unsigned long)scratch_wrong, (unsigned long)BUSTER_ARRAY_LENGTH(lane_test_record_counts),
                   (unsigned long)exchange_differ, (unsigned long)BUSTER_LANE_TEST_PART_INPUT_COUNT, (unsigned long)parts_wrong, (unsigned long)advance_sleeps,
                   (unsigned long)advance_wrong);
            if (!slept)
            {
                printf("  syncs: %lu sleeps on %u lanes, where the late lanes should have made the others sleep only with more than one\n", (unsigned long)sync_sleeps, lanes);
            }
            if (!advance_slept)
            {
                printf("  advance: %lu sleeps on %u lanes, where the advancers' pauses should have made lane 0 sleep only with more than one\n",
                       (unsigned long)advance_sleeps, lanes);
            }
            fflush(stdout);
            passed = passed && ranges_wrong == 0 && syncs_wrong == 0 && slept && alone_wrong == 0 && scratch_wrong == 0 && exchange_differ == 0 && parts_wrong == 0 &&
                     advance_wrong == 0 && advance_slept;
        }
        result = passed ? 0 : 1;
    }
    arena_release(&memory);
    return result;
}
