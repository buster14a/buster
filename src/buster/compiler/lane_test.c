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
//   - Teams: one group kept across operations with different active lane counts, with the same thread identities,
//     published inputs and completed outputs, no work on inactive lanes, and worker scratch released between runs, or
//     with lane_team_keep_scratch kept, emptied, and released when the team stops.
//     One-lane teams start no thread. Invalid and nested dispatches run nothing, including through lane_alone, and a
//     forked child refuses the inherited team without waiting for workers that exist only in its parent.
//
// A watchdog ends a group that makes no progress for BUSTER_LANE_TEST_TIMEOUT_SECONDS. Syncs may take longer when
// the lanes share CPUs, so completed rounds renew their watchdog; a stalled collective still times out. The other
// checks have a fixed deadline. The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the inputs (lane_test_random, lane_test_cost, lane_test_make_records and lane_test_output_byte),
// the arena checks (lane_test_arenas), the lanes' side of each check (lane_test_ranges, lane_test_syncs,
// lane_test_alone, lane_test_scratch, lane_test_exchange, lane_test_parts and lane_test_main, which runs them), their
// verification (lane_test_check_parts and the others), and run_lane_tests.

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
} LaneTestCheck;

static const char* const lane_test_check_names[] = {
    "ranges",
    "syncs",
    "alone",
    "scratch",
    "exchange",
    "parts",
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
        large.base[0] = 1;
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
    static const char* const names[] = { "ranges", "syncs", "alone", "scratch", "exchange", "parts", "team lifetime" };
    static const uint8_t lengths[] = { 6, 5, 5, 7, 8, 5, 13 };
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

static uint64_t lane_test_teams(void)
{
    lane_test_watch(BUSTER_ARRAY_LENGTH(lane_test_check_names), BUSTER_LANE_TEST_MAX_LANES, false);
    alarm(BUSTER_LANE_TEST_TIMEOUT_SECONDS);
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

            printf("%u lane%s ranges %lu checked, %lu wrong; syncs %lu rounds, %lu sleeps, %lu wrong; alone %lu wrong; scratch %lu wrong; exchange %lu inputs, %lu differ; "
                   "parts %lu inputs, %lu wrong\n",
                   lanes, lanes == 1 ? ": " : "s:", (unsigned long)checked, (unsigned long)ranges_wrong, (unsigned long)rounds, (unsigned long)sync_sleeps,
                   (unsigned long)syncs_wrong, (unsigned long)alone_wrong, (unsigned long)scratch_wrong, (unsigned long)BUSTER_ARRAY_LENGTH(lane_test_record_counts),
                   (unsigned long)exchange_differ, (unsigned long)BUSTER_LANE_TEST_PART_INPUT_COUNT, (unsigned long)parts_wrong);
            if (!slept)
            {
                printf("  syncs: %lu sleeps on %u lanes, where the late lanes should have made the others sleep only with more than one\n", (unsigned long)sync_sleeps, lanes);
            }
            fflush(stdout);
            passed = passed && ranges_wrong == 0 && syncs_wrong == 0 && slept && alone_wrong == 0 && scratch_wrong == 0 && exchange_differ == 0 && parts_wrong == 0;
        }
        result = passed ? 0 : 1;
    }
    arena_release(&memory);
    return result;
}
