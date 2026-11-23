// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The calling convention's benchmark (#137): the vector paths of calling_convention.c, on a table of generated types
// and on random calls over them (docs/calling-convention.md has the numbers).
//
// Usage: build/compiler benchmark-calling-convention [--seed N] [--count N] [--only classification|assignment]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...). The cases:
//   - classification: a table of --count types (32,768), records and unions of 0 to 8 members mostly (scalars,
//     pointers, bit-fields, arrays, small vectors, nested records and unions, a few packed, under #pragma pack or
//     aligned) and the pointers, arrays, vectors and aligned typedefs their members have, classified from nothing (the
//     builtin types aside) by abi_classify_all at each target vector width (16, 32 and 64 bytes), per type and per
//     record member; then, at 16 bytes, by abi_classify in the batches a front end hands it as it completes records:
//     windows of the 1, 4 or 16 types it made since its last call, each in levels of types that depend only on earlier
//     ones, a call per level; then by abi_classify_all with no scratch (no-scratch: its first scratch arena of no room),
//     and a chain of 1,024 records against the table's order, with scratch and without (chain, chain-starved).
//   - assignment: --count calls (32,768) over the table's types, at 16 bytes: most of 0 to 6 arguments, some of 4 to
//     14 and a few of 15 to 22, one in four variadic, whose arguments are integers and pointers mostly, then
//     floating types, records that pass in registers and in memory, and now and then long double, __int128, a complex
//     type or a vector (abi_benchmark_named_type and abi_benchmark_unnamed_type have the mix); by abi_assign, per
//     argument, with the share of arguments abi_assign left to its per-call kernel, abi_assign_lanes. Then the same
//     calls' shapes with integers, pointers and doubles alone (assign-simple), each of which needs one register of one
//     class.
// Every run takes the whole table or every call once, and the defaults are more types and arguments than Zen 5's
// branch predictor learns from run to run (#144's lesson): on the 9700X, with a table of 4,096 types the
// classification mispredicts 3 to 10 times less often than with 16,384 or more, and runs up to 2.3 times as fast.
// Each case first checks its results. For the default seed and --count, the classification's tables (every type's pass
// bits, and each classified one's row, stack bytes, alignment, storage, layout and result) and the assignment's calls
// and locations must match digests frozen from main's runs in which the vector paths agreed with the scalar twins #2924
// removed (abi_benchmark_frozen). For any seed, the batches must classify as abi_classify_all does, and each call
// assigned alone must get what the batch gave it. Then the case reports the median of BUSTER_ABI_BENCHMARK_PASSES
// passes, each sample runs lasting at least BUSTER_ABI_BENCHMARK_SAMPLE_NS: nanoseconds, and cycles, instructions,
// branch mispredictions and the throttle from the hardware counters (zero when unavailable), per item. The throttle is
// PMCx1A0's umask 0x08, which #199 reads as the dispatch slots Zen 5's per-core power cap takes. The exit status is 1
// if the arguments are invalid or a check fails.
//
// Layout, in this order: the clock, the counters, the medians and the random numbers; the world of types
// (abi_benchmark_world_build); the measurement (AbiBenchmarkCase, the timed wrappers, abi_benchmark_measure); the
// digests; the classification; the assignment (abi_benchmark_calls, abi_benchmark_deferred);
// run_calling_convention_benchmark.

#include "host_isa.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

#include "buster/compiler/lane.c"
#include "buster/compiler/c_types.c"
#include "buster/compiler/calling_convention.c"

#define BUSTER_ABI_BENCHMARK_PASSES 21
#define BUSTER_ABI_BENCHMARK_SAMPLE_NS 2000000ull
#define BUSTER_ABI_BENCHMARK_MAX_RUNS (1u << 16)
#define BUSTER_ABI_BENCHMARK_DEFAULT_COUNT 32768
#define BUSTER_ABI_BENCHMARK_MIN_COUNT 16
#define BUSTER_ABI_BENCHMARK_MAX_COUNT (1u << 18)
#define BUSTER_ABI_BENCHMARK_MEMBERS 8
// Room for the types one record makes besides itself (its members' pointers, arrays, vectors and aligned typedefs)
// past --count, where the world stops, and the few the world makes first.
#define BUSTER_ABI_BENCHMARK_SLACK_TYPES 64
// The benchmark's own arrays: reserved, and touched only as far as they're used.
#define BUSTER_ABI_BENCHMARK_ARENA_BYTES (16ull << 30)
// The target's vector width for the batches and the assignment: the psABI's baseline, no AVX.
#define BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES 16
// PMCx1A0 (dispatch slots not used) with its undocumented umask 0x08, as a raw perf config: the event's low byte in
// bits 0-7, the umask in bits 8-15 and the event's high nibble in bits 32-35 (benchmark-dataflow's, #199).
#define BUSTER_ABI_BENCHMARK_THROTTLE_EVENT 0x1000008a0ull

// The default seed: its runs at the default --count check their results by the frozen digests.
#define BUSTER_ABI_BENCHMARK_DEFAULT_SEED 1

// The cases' kinds.
#define BUSTER_ABI_BENCHMARK_CLASSIFY_ALL 0
#define BUSTER_ABI_BENCHMARK_BATCHES 1
#define BUSTER_ABI_BENCHMARK_ASSIGN 2
// Each call of `signatures` assigned alone, a call of abi_assign each.
#define BUSTER_ABI_BENCHMARK_ASSIGN_ALONE 3

static const uint32_t abi_benchmark_widths[] = {
    16,
    32,
    64,
};

// The batches' windows: how many newly made types a front end hands abi_classify at a time.
static const uint32_t abi_benchmark_windows[] = {
    1,
    4,
    16,
};

// The default seed's and --count's digests, from main's runs at bd197b2a6, in which the vector paths agreed with the
// scalar twins #2924 removed: abi_benchmark_table_digest of classify-all's table at each of abi_benchmark_widths, and
// abi_benchmark_assignment_digest of the assignment's mix and of its simple mix.
static const uint64_t abi_benchmark_classify_digests[] = {
    UINT64_C(0x24e6d3f777700e76),
    UINT64_C(0xded879c3aa29ec2e),
    UINT64_C(0x1309837698220d4e),
};

static const uint64_t abi_benchmark_assign_digests[] = {
    UINT64_C(0x68122fcf4197e2af),
    UINT64_C(0x838e403c882f2115),
};

// ---------------------------------------------------------------------------------------------------------------------
// The clock, the counters, the medians and the random numbers

static uint64_t abi_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

typedef struct AbiBenchmarkCounters AbiBenchmarkCounters;
struct AbiBenchmarkCounters
{
    int cycles;
    int instructions;
    int branch_misses;
    int throttle;
};

// A counter of `config` for the calling thread in user space, or -1 where perf_event_open refuses. Four counters and
// the NMI watchdog's fit Zen 5's six, so the kernel never multiplexes them.
static int abi_benchmark_counter(uint32_t type, uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = type;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t abi_benchmark_count(int counter)
{
    uint64_t result = 0;
    if (counter < 0 || read(counter, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        result = 0;
    }
    return result;
}

// The clock and the counters at a run's start, or what runs took.
typedef struct AbiBenchmarkReading AbiBenchmarkReading;
struct AbiBenchmarkReading
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t branch_misses;
    uint64_t throttle;
};

static AbiBenchmarkReading abi_benchmark_begin(AbiBenchmarkCounters counters)
{
    AbiBenchmarkReading result;
    result.cycles = abi_benchmark_count(counters.cycles);
    result.instructions = abi_benchmark_count(counters.instructions);
    result.branch_misses = abi_benchmark_count(counters.branch_misses);
    result.throttle = abi_benchmark_count(counters.throttle);
    result.nanoseconds = abi_benchmark_now();
    return result;
}

// Adds what the run since `begin` took to `total`.
static void abi_benchmark_end(AbiBenchmarkCounters counters, AbiBenchmarkReading begin, AbiBenchmarkReading* total)
{
    const uint64_t now = abi_benchmark_now();
    total->nanoseconds += now - begin.nanoseconds;
    total->cycles += abi_benchmark_count(counters.cycles) - begin.cycles;
    total->instructions += abi_benchmark_count(counters.instructions) - begin.instructions;
    total->branch_misses += abi_benchmark_count(counters.branch_misses) - begin.branch_misses;
    total->throttle += abi_benchmark_count(counters.throttle) - begin.throttle;
}

// The median of BUSTER_ABI_BENCHMARK_PASSES values, by rank (no callback).
static double abi_benchmark_median(const double* values)
{
    double result = 0;
    for (uint32_t i = 0; i < BUSTER_ABI_BENCHMARK_PASSES; i += 1)
    {
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t j = 0; j < BUSTER_ABI_BENCHMARK_PASSES; j += 1)
        {
            less += values[j] < values[i] ? 1 : 0;
            equal += values[j] == values[i] ? 1 : 0;
        }
        if (less <= BUSTER_ABI_BENCHMARK_PASSES / 2 && less + equal > BUSTER_ABI_BENCHMARK_PASSES / 2)
        {
            result = values[i];
        }
    }
    return result;
}

static uint64_t abi_benchmark_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t result = *state;
    result = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9ull;
    result = (result ^ (result >> 27)) * 0x94D049BB133111EBull;
    return result ^ (result >> 31);
}

static uint64_t abi_benchmark_below(uint64_t* state, uint64_t bound)
{
    return abi_benchmark_random(state) % bound;
}

// A skewed choice of [0, bound): the smallest of three draws, so that the first items are chosen most.
static uint64_t abi_benchmark_skewed(uint64_t* state, uint64_t bound)
{
    const uint64_t a = abi_benchmark_below(state, bound);
    const uint64_t b = abi_benchmark_below(state, bound);
    const uint64_t c = abi_benchmark_below(state, bound);
    const uint64_t smaller = a < b ? a : b;
    return smaller < c ? smaller : c;
}

// ---------------------------------------------------------------------------------------------------------------------
// The world of types

// The scalars a member has most often, int, long and double more than once for their weight.
static const TypeId abi_benchmark_scalars[] = {
    BUSTER_TYPE_INT,
    BUSTER_TYPE_INT,
    BUSTER_TYPE_INT,
    BUSTER_TYPE_UINT,
    BUSTER_TYPE_LONG,
    BUSTER_TYPE_LONG,
    BUSTER_TYPE_ULONG,
    BUSTER_TYPE_LLONG,
    BUSTER_TYPE_CHAR,
    BUSTER_TYPE_UCHAR,
    BUSTER_TYPE_SHORT,
    BUSTER_TYPE_USHORT,
    BUSTER_TYPE_BOOL,
    BUSTER_TYPE_FLOAT,
    BUSTER_TYPE_DOUBLE,
    BUSTER_TYPE_DOUBLE,
};

typedef struct AbiBenchmarkWorld AbiBenchmarkWorld;
struct AbiBenchmarkWorld
{
    TypeTable table;
    Arena arena;
    Arena index_arena;
    // The records and unions, in the order they were completed, and how many members they have in all.
    TypeId* records;
    uint32_t record_count;
    uint64_t member_count;
    // Types the calls take that the members may not: void*, char* and a vector of four floats.
    TypeId void_pointer;
    TypeId char_pointer;
    TypeId float_vector;
};

static bool abi_benchmark_world_open(AbiBenchmarkWorld* world, Arena* arena, uint32_t count)
{
    const TypeCapacity capacity = {
        .types = count + BUSTER_ABI_BENCHMARK_SLACK_TYPES,
        .parameters = 1,
        .tags = 2 * count,
        .members = 2 * count * BUSTER_ABI_BENCHMARK_MEMBERS,
    };
    *world = (AbiBenchmarkWorld){ 0 };
    world->arena = arena_reserve(type_capacity_bytes(capacity));
    world->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    world->records = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, 2 * count);
    return world->records != 0 && type_initialize(&world->table, &world->arena, &world->index_arena, capacity);
}

static void abi_benchmark_world_close(AbiBenchmarkWorld* world)
{
    arena_release(&world->arena);
    arena_release(&world->index_arena);
}

static TypeId abi_benchmark_earlier_record(const AbiBenchmarkWorld* world, uint64_t* random)
{
    return world->records[abi_benchmark_skewed(random, world->record_count)];
}

// A member's type: scalars mostly, then pointers, arrays of small types, earlier records and unions (nested), small
// vectors, and now and then an aligned typedef, a complex or _BitInt type, long double or __int128.
static TypeId abi_benchmark_member_type(AbiBenchmarkWorld* world, uint64_t* random)
{
    TypeTable* table = &world->table;
    const uint64_t pick = abi_benchmark_below(random, 100);
    const TypeId scalar = abi_benchmark_scalars[abi_benchmark_below(random, BUSTER_ARRAY_LENGTH(abi_benchmark_scalars))];
    TypeId result = scalar;
    if (pick < 50)
    {
    }
    else if (pick < 62)
    {
        const TypeId pointees[4] = {
            BUSTER_TYPE_VOID,
            BUSTER_TYPE_CHAR,
            BUSTER_TYPE_INT,
            BUSTER_TYPE_DOUBLE,
        };
        const bool to_record = world->record_count != 0 && abi_benchmark_below(random, 2) == 0;
        result = type_pointer(table, to_record ? abi_benchmark_earlier_record(world, random) : pointees[abi_benchmark_below(random, 4)]);
    }
    else if (pick < 72)
    {
        // An array of a scalar, now and then of an earlier record: chars by the dozen, others a few.
        const bool of_record = world->record_count != 0 && abi_benchmark_below(random, 8) == 0;
        const TypeId element = of_record ? abi_benchmark_earlier_record(world, random) : scalar;
        const uint64_t length = type_size(table, element) == 1 ? 1 + abi_benchmark_below(random, 16) : 1 + abi_benchmark_below(random, 4);
        result = type_array(table, element, length);
    }
    else if (pick < 77)
    {
        // A vector of 8 or 16 bytes, one in eight of 32.
        const TypeId elements[4] = {
            BUSTER_TYPE_FLOAT,
            BUSTER_TYPE_INT,
            BUSTER_TYPE_DOUBLE,
            BUSTER_TYPE_CHAR,
        };
        const uint64_t bytes = abi_benchmark_below(random, 8) == 0 ? 32 : 8u << abi_benchmark_below(random, 2);
        result = type_vector_size(table, elements[abi_benchmark_below(random, 4)], bytes);
    }
    else if (pick < 92 && world->record_count != 0)
    {
        result = abi_benchmark_earlier_record(world, random);
    }
    else if (pick < 95)
    {
        result = type_aligned(table, scalar, (uint32_t)abi_benchmark_below(random, 6));
    }
    else if (pick < 97)
    {
        result = abi_benchmark_below(random, 2) == 0 ? BUSTER_TYPE_COMPLEX_FLOAT : BUSTER_TYPE_COMPLEX_DOUBLE;
    }
    else if (pick < 98)
    {
        result = type_bit_int(table, (uint32_t)(2 + abi_benchmark_below(random, 127)), abi_benchmark_below(random, 2) == 0);
    }
    else if (pick < 99)
    {
        result = BUSTER_TYPE_LONG_DOUBLE;
    }
    else
    {
        result = BUSTER_TYPE_INT128;
    }
    // None of these requests breaks a rule of the table's, which has room for them.
    table->last_error = TYPE_ERROR_NONE;
    return result != 0 ? result : scalar;
}

// A record of 0 to BUSTER_ABI_BENCHMARK_MEMBERS members, completed: a union one time in six, bit-fields (some unnamed)
// in a quarter of them, now and then a member with an aligned attribute or packed, or a flexible array member, and a
// few packed, under #pragma pack or aligned.
static void abi_benchmark_record(AbiBenchmarkWorld* world, uint64_t* random)
{
    TypeTable* table = &world->table;
    TypeMember members[BUSTER_ABI_BENCHMARK_MEMBERS];
    const bool is_union = abi_benchmark_below(random, 6) == 0;
    const bool bit_fields = abi_benchmark_below(random, 4) == 0;
    const uint32_t count = (uint32_t)abi_benchmark_below(random, BUSTER_ABI_BENCHMARK_MEMBERS + 1);
    for (uint32_t m = 0; m < count; m += 1)
    {
        const TypeId type = abi_benchmark_member_type(world, random);
        const uint64_t width = type_integer_width(table, type);
        const bool bit_field = bit_fields && width != 0 && abi_benchmark_below(random, 2) == 0;
        const bool unnamed = bit_field && abi_benchmark_below(random, 6) == 0;
        uint32_t bit_width = BUSTER_TYPE_NOT_BIT_FIELD;
        if (bit_field)
        {
            bit_width = unnamed ? (uint32_t)abi_benchmark_below(random, width + 1) : (uint32_t)(1 + abi_benchmark_below(random, width));
        }
        members[m] = (TypeMember){
            .name = unnamed ? 0 : m + 1,
            .type = type,
            .bit_width = bit_width,
            .alignment_log2 = !bit_field && abi_benchmark_below(random, 32) == 0 ? (uint8_t)(1 + abi_benchmark_below(random, 5)) : 0,
            .packed = !bit_field && abi_benchmark_below(random, 32) == 0,
        };
    }
    if (!is_union && count != 0 && abi_benchmark_below(random, 32) == 0)
    {
        const TypeId element = abi_benchmark_scalars[abi_benchmark_below(random, BUSTER_ARRAY_LENGTH(abi_benchmark_scalars))];
        const TypeId open = type_incomplete_array(table, element);
        if (open != 0)
        {
            members[count - 1] = (TypeMember){
                .name = count,
                .type = open,
                .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
                .alignment_log2 = 0,
                .packed = false,
            };
        }
    }
    const TypeRecordAttributes attributes = {
        .packed = abi_benchmark_below(random, 16) == 0,
        .pack_log2 = abi_benchmark_below(random, 16) == 0 ? (uint8_t)(1 + abi_benchmark_below(random, 4)) : 0,
        .alignment_log2 = abi_benchmark_below(random, 16) == 0 ? (uint8_t)(1 + abi_benchmark_below(random, 6)) : 0,
    };
    const TypeId record = type_tag(table, is_union ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, world->record_count + 1);
    if (record != 0 && type_complete_record(table, record, members, count, attributes))
    {
        world->records[world->record_count] = record;
        world->record_count += 1;
        world->member_count += count;
    }
    table->last_error = TYPE_ERROR_NONE;
}

// Records until the table has `count` types besides the builtin ones, after the calls' own types.
static void abi_benchmark_world_build(AbiBenchmarkWorld* world, uint64_t seed, uint32_t count)
{
    uint64_t random = seed;
    TypeTable* table = &world->table;
    world->void_pointer = type_pointer(table, BUSTER_TYPE_VOID);
    world->char_pointer = type_pointer(table, BUSTER_TYPE_CHAR);
    world->float_vector = type_vector_size(table, BUSTER_TYPE_FLOAT, 16);
    for (uint32_t attempt = 0; table->types.count < BUSTER_TYPE_BUILTIN_COUNT + count && attempt < 2 * count; attempt += 1)
    {
        abi_benchmark_record(world, &random);
    }
}

// The types a type's row is made from (a record's members', an array's element and an aligned typedef's base), and an
// _Atomic type's value, whose storage and layout it takes (abi_dependencies_classified's), into `dependencies`, which has
// room for BUSTER_ABI_BENCHMARK_MEMBERS; returns how many.
static uint32_t abi_benchmark_dependencies(const TypeTable* table, uint32_t index, uint32_t* dependencies)
{
    const uint8_t kind = table->types.kind[index];
    uint32_t result = 0;
    if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_QUALIFIED || kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        dependencies[0] = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        result = 1;
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
    {
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t count = table->tags.member_count[tag];
        for (uint32_t m = 0; m < count && m < BUSTER_ABI_BENCHMARK_MEMBERS; m += 1)
        {
            dependencies[m] = table->members.type[begin + m] >> BUSTER_TYPE_INDEX_SHIFT;
        }
        result = count < BUSTER_ABI_BENCHMARK_MEMBERS ? count : BUSTER_ABI_BENCHMARK_MEMBERS;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The measurement

// What a case measures: its kind and what it works on.
typedef struct AbiBenchmarkCase AbiBenchmarkCase;
struct AbiBenchmarkCase
{
    uint32_t kind;
    const char* name;
    const TypeTable* table;
    // The table it classifies into (classify-all starts it again before each run, from the arena that holds it), or the
    // table the assignment reads.
    AbiTable* abi;
    Arena* arena;
    uint32_t vector_bytes;
    // How many items a run takes (types or arguments), which each sample's totals are per, and what a classify-all
    // run returns.
    uint64_t items;
    uint32_t classified;
    // The batches: the types in batch order, and each batch's first, batch_count + 1 of them.
    const TypeId* types;
    const uint32_t* starts;
    uint32_t batch_count;
    // The assignment.
    AbiSignatures signatures;
    AbiLocation* locations;
    AbiCall* calls;
    // An arena of no room that classify-all runs with as the thread's first scratch arena, or 0.
    Arena* starved;
};

typedef struct AbiBenchmarkOptions AbiBenchmarkOptions;
struct AbiBenchmarkOptions
{
    uint64_t seed;
    uint32_t count;
    bool classification;
    bool assignment;
};

// What a case's passes measured: nanoseconds, cycles, instructions, branch mispredictions and the throttle's slots per
// item, per pass.
typedef struct AbiBenchmarkSamples AbiBenchmarkSamples;
struct AbiBenchmarkSamples
{
    double nanoseconds[BUSTER_ABI_BENCHMARK_PASSES];
    double cycles[BUSTER_ABI_BENCHMARK_PASSES];
    double instructions[BUSTER_ABI_BENCHMARK_PASSES];
    double branch_misses[BUSTER_ABI_BENCHMARK_PASSES];
    double throttle[BUSTER_ABI_BENCHMARK_PASSES];
};

// The timed calls, each in a function of its own that starts a 64-byte line, so that every build lays their loops out
// in lines the same way however much driver code comes before them (docs/dataflow.md, "Where the scalar twin's code
// falls").
static __attribute__((noinline, aligned(64))) uint32_t abi_benchmark_classify_all_vector(AbiTable* abi, const TypeTable* table)
{
    return abi_classify_all(abi, table);
}

// classify-all with `starved`, an arena of no room, as the thread's first scratch arena, the one abi_classify_all's
// scratch comes from.
static __attribute__((noinline, aligned(64))) uint32_t abi_benchmark_classify_all_starved(AbiTable* abi, const TypeTable* table, Arena* starved)
{
    const Arena saved = thread_context.scratch[0];
    thread_context.scratch[0] = *starved;
    const uint32_t result = abi_classify_all(abi, table);
    thread_context.scratch[0] = saved;
    return result;
}

static __attribute__((noinline, aligned(64))) void abi_benchmark_batches_vector(AbiTable* abi, const TypeTable* table, const TypeId* types, const uint32_t* starts,
                                                                               uint32_t batch_count)
{
    for (uint32_t b = 0; b < batch_count; b += 1)
    {
        abi_classify(abi, table, types + starts[b], starts[b + 1] - starts[b]);
    }
}

static __attribute__((noinline, aligned(64))) void abi_benchmark_assign_vector(const AbiTable* abi, AbiSignatures signatures, AbiLocation* locations, AbiCall* calls)
{
    abi_assign(abi, signatures, locations, calls);
}

// Each call alone, as a code generator assigns a single signature (a function's entry, va_start's named parameters).
static __attribute__((noinline, aligned(64))) void abi_benchmark_assign_alone(const AbiTable* abi, AbiSignatures signatures, AbiLocation* locations, AbiCall* calls)
{
    uint64_t first = 0;
    for (uint32_t c = 0; c < signatures.count; c += 1)
    {
        const AbiSignatures one = {
            .results = signatures.results + c,
            .counts = signatures.counts + c,
            .named = signatures.named + c,
            .variadic = signatures.variadic + c,
            .count = 1,
            .arguments = signatures.arguments + first,
        };
        abi_assign(abi, one, locations + first, calls + c);
        first += signatures.counts[c];
    }
}

// Readies a run, outside the time: classify-all starts its table again, with only the builtin types classified.
// Returns false if abi_initialize fails.
static bool abi_benchmark_prepare(const AbiBenchmarkCase* benchmark)
{
    bool result = true;
    if (benchmark->kind == BUSTER_ABI_BENCHMARK_CLASSIFY_ALL)
    {
        arena_pop_to(benchmark->arena, 0);
        result = abi_initialize(benchmark->abi, benchmark->arena, benchmark->table, benchmark->vector_bytes);
    }
    return result;
}

// One run. Returns false if a classify-all run classified another count of types than the case's.
static bool abi_benchmark_execute(const AbiBenchmarkCase* benchmark)
{
    AbiTable* abi = benchmark->abi;
    bool result = true;
    if (benchmark->kind == BUSTER_ABI_BENCHMARK_CLASSIFY_ALL && benchmark->starved != 0)
    {
        result = abi_benchmark_classify_all_starved(abi, benchmark->table, benchmark->starved) == benchmark->classified;
    }
    else if (benchmark->kind == BUSTER_ABI_BENCHMARK_CLASSIFY_ALL)
    {
        result = abi_benchmark_classify_all_vector(abi, benchmark->table) == benchmark->classified;
    }
    else if (benchmark->kind == BUSTER_ABI_BENCHMARK_BATCHES)
    {
        abi_benchmark_batches_vector(abi, benchmark->table, benchmark->types, benchmark->starts, benchmark->batch_count);
    }
    else if (benchmark->kind == BUSTER_ABI_BENCHMARK_ASSIGN_ALONE)
    {
        abi_benchmark_assign_alone(abi, benchmark->signatures, benchmark->locations, benchmark->calls);
    }
    else
    {
        abi_benchmark_assign_vector(abi, benchmark->signatures, benchmark->locations, benchmark->calls);
    }
    return result;
}

// A sample: runs, each readied outside the time, until they have lasted BUSTER_ABI_BENCHMARK_SAMPLE_NS; their totals
// in `total` and their count in `runs`.
static bool abi_benchmark_sample(const AbiBenchmarkCase* benchmark, AbiBenchmarkCounters counters, AbiBenchmarkReading* total, uint64_t* runs)
{
    bool valid = true;
    *total = (AbiBenchmarkReading){ 0 };
    *runs = 0;
    while (valid && total->nanoseconds < BUSTER_ABI_BENCHMARK_SAMPLE_NS && *runs < BUSTER_ABI_BENCHMARK_MAX_RUNS)
    {
        valid = abi_benchmark_prepare(benchmark);
        if (valid)
        {
            const AbiBenchmarkReading begin = abi_benchmark_begin(counters);
            valid = abi_benchmark_execute(benchmark);
            abi_benchmark_end(counters, begin, total);
            *runs += 1;
        }
    }
    return valid;
}

// A sample to warm the caches and the scratch arenas' pages, then the passes.
static bool abi_benchmark_measure(const AbiBenchmarkCase* benchmark, AbiBenchmarkCounters counters, AbiBenchmarkSamples* samples)
{
    AbiBenchmarkReading total;
    uint64_t runs = 0;
    bool valid = abi_benchmark_sample(benchmark, counters, &total, &runs);
    for (uint32_t pass = 0; valid && pass < BUSTER_ABI_BENCHMARK_PASSES; pass += 1)
    {
        valid = abi_benchmark_sample(benchmark, counters, &total, &runs);
        if (valid)
        {
            const double items = (double)runs * (double)benchmark->items;
            samples->nanoseconds[pass] = (double)total.nanoseconds / items;
            samples->cycles[pass] = (double)total.cycles / items;
            samples->instructions[pass] = (double)total.instructions / items;
            samples->branch_misses[pass] = (double)total.branch_misses / items;
            samples->throttle[pass] = (double)total.throttle / items;
        }
    }
    return valid;
}

// The medians per `unit`, and with `per_secondary` (items per secondary unit, 0 for none) the time and cycles per
// `secondary` too.
static void abi_benchmark_print(const char* name, const AbiBenchmarkSamples* samples, const char* unit, double per_secondary, const char* secondary)
{
    const double nanoseconds = abi_benchmark_median(samples->nanoseconds);
    const double cycles = abi_benchmark_median(samples->cycles);
    const double instructions = abi_benchmark_median(samples->instructions);
    const double branch_misses = abi_benchmark_median(samples->branch_misses);
    const double throttle = abi_benchmark_median(samples->throttle);
    printf("  %-13s %8.2f ns/%s %8.2f cycles/%s %8.2f instructions/%s %5.2f IPC %6.3f mispredicts/%s %7.3f throttle/%s", name, nanoseconds, unit, cycles, unit, instructions,
           unit, cycles != 0 ? instructions / cycles : 0, branch_misses, unit, throttle, unit);
    if (per_secondary != 0)
    {
        printf(" %7.2f ns/%s %7.2f cycles/%s", nanoseconds * per_secondary, secondary, cycles * per_secondary, secondary);
    }
    printf("\n");
}

// ---------------------------------------------------------------------------------------------------------------------
// The digests

// FNV-1a over bytes, continuing from hash.
static uint64_t abi_benchmark_hash(uint64_t hash, const void* data, uint64_t bytes)
{
    const uint8_t* byte = data;
    for (uint64_t i = 0; i < bytes; i += 1)
    {
        hash = (hash ^ byte[i]) * UINT64_C(0x100000001b3);
    }
    return hash;
}

// The classification of the types below `count`, field by field: every type's pass bits, and a classified type's row,
// stack bytes, alignment, storage, layout and result (test-calling-convention's digest).
static uint64_t abi_benchmark_table_digest(const AbiTable* abi, uint32_t count)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (uint32_t index = 1; index < count; index += 1)
    {
        hash = abi_benchmark_hash(hash, &abi->pass[index], sizeof(abi->pass[index]));
        if ((abi->pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0)
        {
            const AbiReturn result = abi->results[index];
            hash = abi_benchmark_hash(hash, abi->rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES, BUSTER_ABI_ROW_BYTES);
            hash = abi_benchmark_hash(hash, &abi->stack_bytes[index], sizeof(abi->stack_bytes[index]));
            hash = abi_benchmark_hash(hash, &abi->alignment_log2[index], sizeof(abi->alignment_log2[index]));
            hash = abi_benchmark_hash(hash, &abi->storage[index], sizeof(abi->storage[index]));
            hash = abi_benchmark_hash(hash, &abi->layout[index], sizeof(abi->layout[index]));
            hash = abi_benchmark_hash(hash, &result.kind, sizeof(result.kind));
            hash = abi_benchmark_hash(hash, &result.flags, sizeof(result.flags));
            hash = abi_benchmark_hash(hash, &result.part_bytes, sizeof(result.part_bytes));
            hash = abi_benchmark_hash(hash, result.registers, sizeof(result.registers));
        }
    }
    return hash;
}

// An assignment's calls and locations, field by field (test-calling-convention's digest).
static uint64_t abi_benchmark_assignment_digest(const AbiCall* calls, uint32_t call_count, const AbiLocation* locations, uint64_t location_count)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (uint32_t c = 0; c < call_count; c += 1)
    {
        const AbiCall call = calls[c];
        hash = abi_benchmark_hash(hash, &call.stack_bytes, sizeof(call.stack_bytes));
        hash = abi_benchmark_hash(hash, &call.stack_alignment_log2, sizeof(call.stack_alignment_log2));
        hash = abi_benchmark_hash(hash, &call.integer_registers, sizeof(call.integer_registers));
        hash = abi_benchmark_hash(hash, &call.vector_registers, sizeof(call.vector_registers));
        hash = abi_benchmark_hash(hash, &call.result.kind, sizeof(call.result.kind));
        hash = abi_benchmark_hash(hash, &call.result.flags, sizeof(call.result.flags));
        hash = abi_benchmark_hash(hash, &call.result.part_bytes, sizeof(call.result.part_bytes));
        hash = abi_benchmark_hash(hash, call.result.registers, sizeof(call.result.registers));
    }
    for (uint64_t l = 0; l < location_count; l += 1)
    {
        const AbiLocation location = locations[l];
        hash = abi_benchmark_hash(hash, &location.stack_offset, sizeof(location.stack_offset));
        hash = abi_benchmark_hash(hash, location.registers, sizeof(location.registers));
        hash = abi_benchmark_hash(hash, &location.kind, sizeof(location.kind));
        hash = abi_benchmark_hash(hash, &location.flags, sizeof(location.flags));
    }
    return hash;
}

// Whether a digest of a run at the default seed and --count is the frozen one; other runs have none to match.
static bool abi_benchmark_frozen(AbiBenchmarkOptions options, const char* what, uint64_t digest, uint64_t frozen)
{
    const bool checked = options.seed == BUSTER_ABI_BENCHMARK_DEFAULT_SEED && options.count == BUSTER_ABI_BENCHMARK_DEFAULT_COUNT;
    const bool result = !checked || digest == frozen;
    if (!result)
    {
        fprintf(stderr, "benchmark-calling-convention: %s: the digest 0x%016" PRIx64 " isn't the frozen 0x%016" PRIx64 "\n", what, digest, frozen);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Classification

// Whether two tables classified the same types alike: the same pass bits for every type, and for each classified one
// the same row, stack bytes, alignment, storage, layout and result. Names the first type that differs.
static bool abi_benchmark_same_tables(const AbiTable* left, const AbiTable* right, uint32_t count, const char* what)
{
    uint32_t differing = 0;
    for (uint32_t index = 1; differing == 0 && index < count; index += 1)
    {
        const bool classified = (left->pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0;
        const uint64_t row = (uint64_t)index * BUSTER_ABI_ROW_BYTES;
        const bool same = left->pass[index] == right->pass[index] &&
                          (!classified || (memcmp(left->rows + row, right->rows + row, BUSTER_ABI_ROW_BYTES) == 0 && left->stack_bytes[index] == right->stack_bytes[index] &&
                                           left->alignment_log2[index] == right->alignment_log2[index] && left->storage[index] == right->storage[index] &&
                                           left->layout[index] == right->layout[index] &&
                                           memcmp(&left->results[index], &right->results[index], sizeof(AbiReturn)) == 0));
        differing = same ? 0 : index;
    }
    if (differing != 0)
    {
        fprintf(stderr, "benchmark-calling-convention: %s: type %u differs: pass %08x/%08x, row[0] %02x/%02x, stack bytes %u/%u\n", what, differing, left->pass[differing],
                right->pass[differing], left->rows[(uint64_t)differing * BUSTER_ABI_ROW_BYTES], right->rows[(uint64_t)differing * BUSTER_ABI_ROW_BYTES],
                left->stack_bytes[differing], right->stack_bytes[differing]);
    }
    return differing == 0;
}

// The types abi_classify_all classifies (every classifiable one past the builtin types), in the order they were made,
// into `types`; returns how many.
static uint32_t abi_benchmark_classifiable(const TypeTable* table, TypeId* types)
{
    uint32_t result = 0;
    for (uint32_t index = BUSTER_TYPE_BUILTIN_COUNT; index < table->types.count; index += 1)
    {
        if (abi_classifiable(table, index))
        {
            types[result] = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
            result += 1;
        }
    }
    return result;
}

// Cuts `types` into the batches a front end hands abi_classify: windows of `window` types in the order they were made
// (what it completed since its last call), each in levels of types that depend only on the window's earlier levels
// and on earlier windows, a batch per level. order gets the types in batch order, starts[b] is batch b's first in it,
// and starts[batch count] the end. `window_of` and `level_of` have a slot per type index. Returns the number of
// batches.
static uint32_t abi_benchmark_cut_batches(const TypeTable* table, const TypeId* types, uint32_t count, uint32_t window, uint32_t* window_of, uint32_t* level_of,
                                          TypeId* order, uint32_t* starts)
{
    uint32_t batches = 0;
    uint32_t placed = 0;
    for (uint32_t index = 0; index < table->types.count; index += 1)
    {
        window_of[index] = UINT32_MAX;
    }
    for (uint32_t begin = 0; begin < count; begin += window)
    {
        const uint32_t end = count - begin > window ? begin + window : count;
        uint32_t levels = 0;
        for (uint32_t i = begin; i < end; i += 1)
        {
            const uint32_t index = types[i] >> BUSTER_TYPE_INDEX_SHIFT;
            uint32_t dependencies[BUSTER_ABI_BENCHMARK_MEMBERS];
            const uint32_t dependency_count = abi_benchmark_dependencies(table, index, dependencies);
            uint32_t level = 0;
            for (uint32_t d = 0; d < dependency_count; d += 1)
            {
                const uint32_t after = window_of[dependencies[d]] == begin ? level_of[dependencies[d]] + 1 : 0;
                level = after > level ? after : level;
            }
            window_of[index] = begin;
            level_of[index] = level;
            levels = level + 1 > levels ? level + 1 : levels;
        }
        for (uint32_t level = 0; level < levels; level += 1)
        {
            starts[batches] = placed;
            batches += 1;
            for (uint32_t i = begin; i < end; i += 1)
            {
                if (level_of[types[i] >> BUSTER_TYPE_INDEX_SHIFT] == level)
                {
                    order[placed] = types[i];
                    placed += 1;
                }
            }
        }
    }
    starts[batches] = placed;
    return batches;
}

// How deep the types nest: a type's level is one past its dependencies' highest among `types`, 0 for one without any.
// Returns the number of levels, about how many abi_classify_all classifies one after another.
static uint32_t abi_benchmark_levels(const TypeTable* table, const TypeId* types, uint32_t count, uint32_t* level_of)
{
    uint32_t result = 0;
    memset(level_of, 0xff, (uint64_t)table->types.count * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t index = types[i] >> BUSTER_TYPE_INDEX_SHIFT;
        uint32_t dependencies[BUSTER_ABI_BENCHMARK_MEMBERS];
        const uint32_t dependency_count = abi_benchmark_dependencies(table, index, dependencies);
        uint32_t level = 0;
        for (uint32_t d = 0; d < dependency_count; d += 1)
        {
            // The builtin types and a flexible array member's type are none of `types`.
            const uint32_t after = level_of[dependencies[d]] != UINT32_MAX ? level_of[dependencies[d]] + 1 : 0;
            level = after > level ? after : level;
        }
        level_of[index] = level;
        result = level + 1 > result ? level + 1 : result;
    }
    return result;
}

// The world's types by what they are, for the report.
static void abi_benchmark_describe(const AbiBenchmarkWorld* world, uint32_t classified, uint32_t levels)
{
    const TypeTable* table = &world->table;
    uint32_t pointers = 0;
    uint32_t arrays = 0;
    uint32_t vectors = 0;
    uint32_t aligned = 0;
    uint32_t unions = 0;
    for (uint32_t index = BUSTER_TYPE_BUILTIN_COUNT; index < table->types.count; index += 1)
    {
        const uint8_t kind = table->types.kind[index];
        pointers += kind == BUSTER_TYPE_KIND_POINTER ? 1 : 0;
        arrays += kind == BUSTER_TYPE_KIND_ARRAY ? 1 : 0;
        vectors += kind == BUSTER_TYPE_KIND_VECTOR ? 1 : 0;
        aligned += kind == BUSTER_TYPE_KIND_ALIGNED ? 1 : 0;
        unions += kind == BUSTER_TYPE_KIND_UNION && abi_classifiable(table, index) ? 1 : 0;
    }
    const uint32_t others = classified - world->record_count - pointers - arrays - vectors - aligned;
    printf("classification: %u types to classify: %u records (%u of them unions) with %" PRIu64 " members, %u pointers, %u arrays, %u vectors, %u aligned typedefs, %u others; "
           "%u levels of nesting\n",
           classified, world->record_count, unions, world->member_count, pointers, arrays, vectors, aligned, others, levels);
}

// How many of the world's records a table passes in memory.
static uint32_t abi_benchmark_records_in_memory(const AbiBenchmarkWorld* world, const AbiTable* abi)
{
    uint32_t result = 0;
    for (uint32_t r = 0; r < world->record_count; r += 1)
    {
        result += (abi->pass[world->records[r] >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_MEMORY) != 0 ? 1 : 0;
    }
    return result;
}

// The tables a case classifies into: the timed one, and the reference abi_classify_all classified, which the batches
// must match, each in an arena of its own.
#define BUSTER_ABI_BENCHMARK_TIMED 0
#define BUSTER_ABI_BENCHMARK_REFERENCE 1
#define BUSTER_ABI_BENCHMARK_TABLES 2

typedef struct AbiBenchmarkTables AbiBenchmarkTables;
struct AbiBenchmarkTables
{
    AbiTable abi[BUSTER_ABI_BENCHMARK_TABLES];
    Arena arena[BUSTER_ABI_BENCHMARK_TABLES];
};

static bool abi_benchmark_tables_open(AbiBenchmarkTables* tables, const TypeTable* table)
{
    bool result = true;
    for (uint32_t t = 0; t < BUSTER_ABI_BENCHMARK_TABLES; t += 1)
    {
        tables->arena[t] = arena_reserve(abi_capacity_bytes(table->types.capacity));
        result = result && tables->arena[t].base != 0;
    }
    return result;
}

static void abi_benchmark_tables_close(AbiBenchmarkTables* tables)
{
    for (uint32_t t = 0; t < BUSTER_ABI_BENCHMARK_TABLES; t += 1)
    {
        arena_release(&tables->arena[t]);
    }
}

// Starts table `t` again for `vector_bytes`, with only the builtin types classified.
static bool abi_benchmark_tables_start(AbiBenchmarkTables* tables, uint32_t t, const TypeTable* table, uint32_t vector_bytes)
{
    arena_pop_to(&tables->arena[t], 0);
    return abi_initialize(&tables->abi[t], &tables->arena[t], table, vector_bytes);
}

// classify-all at abi_benchmark_widths[w]: from nothing into the timed table, checked by its count and, for the default
// seed and --count, its frozen digest, then timed.
static bool abi_benchmark_classify_all(const AbiBenchmarkWorld* world, AbiBenchmarkTables* tables, uint32_t w, uint32_t classified, AbiBenchmarkOptions options,
                                       AbiBenchmarkCounters counters)
{
    const TypeTable* table = &world->table;
    const uint32_t vector_bytes = abi_benchmark_widths[w];
    AbiTable* abi = &tables->abi[BUSTER_ABI_BENCHMARK_TIMED];
    static AbiBenchmarkSamples samples;
    char what[64];
    snprintf(what, sizeof(what), "classify-all at %u-byte vectors", vector_bytes);
    bool valid = abi_benchmark_tables_start(tables, BUSTER_ABI_BENCHMARK_TIMED, table, vector_bytes);
    valid = valid && abi_classify_all(abi, table) == classified;
    valid = valid && abi_benchmark_frozen(options, what, abi_benchmark_table_digest(abi, table->types.count), abi_benchmark_classify_digests[w]);
    if (valid)
    {
        const AbiBenchmarkCase benchmark = {
            .kind = BUSTER_ABI_BENCHMARK_CLASSIFY_ALL,
            .name = "classify-all",
            .table = table,
            .abi = abi,
            .arena = &tables->arena[BUSTER_ABI_BENCHMARK_TIMED],
            .vector_bytes = vector_bytes,
            .items = classified,
            .classified = classified,
        };
        const uint32_t in_memory = abi_benchmark_records_in_memory(world, abi);
        valid = abi_benchmark_measure(&benchmark, counters, &samples);
        if (valid)
        {
            printf("classify-all, %u-byte vectors: %u of the %u records in memory\n", vector_bytes, in_memory, world->record_count);
            const double types_per_member = world->member_count != 0 ? (double)classified / (double)world->member_count : 0;
            abi_benchmark_print(benchmark.name, &samples, "type", types_per_member, "member");
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-calling-convention: %s failed or its results are wrong\n", what);
    }
    return valid;
}

// The batches at the base width: into a fresh table, which must match the reference abi_classify_all classified, then
// timed (classifying a type again gives it the same row, so runs need no reset).
static bool abi_benchmark_batches(const AbiBenchmarkWorld* world, AbiBenchmarkTables* tables, const TypeId* types, uint32_t classified, Arena* arena,
                                  AbiBenchmarkCounters counters)
{
    const TypeTable* table = &world->table;
    AbiTable* abi = &tables->abi[BUSTER_ABI_BENCHMARK_TIMED];
    static AbiBenchmarkSamples samples;
    const uint64_t length = arena->length;
    uint32_t* window_of = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, table->types.count);
    uint32_t* level_of = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, table->types.count);
    TypeId* order = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, classified);
    uint32_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)classified + 1);
    bool valid = window_of != 0 && level_of != 0 && order != 0 && starts != 0 &&
                 abi_benchmark_tables_start(tables, BUSTER_ABI_BENCHMARK_REFERENCE, table, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES);
    valid = valid && abi_classify_all(&tables->abi[BUSTER_ABI_BENCHMARK_REFERENCE], table) == classified;
    for (uint32_t s = 0; valid && s < BUSTER_ARRAY_LENGTH(abi_benchmark_windows); s += 1)
    {
        const uint32_t window = abi_benchmark_windows[s];
        const uint32_t batch_count = abi_benchmark_cut_batches(table, types, classified, window, window_of, level_of, order, starts);
        valid = abi_benchmark_tables_start(tables, BUSTER_ABI_BENCHMARK_TIMED, table, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES);
        if (valid)
        {
            abi_benchmark_batches_vector(abi, table, order, starts, batch_count);
            valid = abi_benchmark_same_tables(abi, &tables->abi[BUSTER_ABI_BENCHMARK_REFERENCE], table->types.count, "batches");
        }
        char name[32];
        snprintf(name, sizeof(name), "batches-%u", window);
        const AbiBenchmarkCase benchmark = {
            .kind = BUSTER_ABI_BENCHMARK_BATCHES,
            .name = name,
            .table = table,
            .abi = abi,
            .vector_bytes = BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES,
            .items = classified,
            .types = order,
            .starts = starts,
            .batch_count = batch_count,
        };
        valid = valid && abi_benchmark_measure(&benchmark, counters, &samples);
        if (valid)
        {
            printf("batches, windows of %u types, %u-byte vectors: %u calls of abi_classify, %.2f types each\n", window, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES, batch_count,
                   (double)classified / (double)batch_count);
            abi_benchmark_print(benchmark.name, &samples, "type", 0, 0);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-calling-convention: the batches failed or classified otherwise than abi_classify_all\n");
    }
    arena_pop_to(arena, length);
    return valid;
}

// An arena of no room, which a starved run puts in place of the thread's first scratch arena.
static Arena abi_benchmark_starved_arena(void)
{
    Arena result = arena_reserve(4096);
    result.capacity = result.length;
    return result;
}

// classify-all with no scratch at all (abi_benchmark_classify_all_starved), at the base width: into the timed table,
// which must match what classify-all with its scratch classified, then timed.
static bool abi_benchmark_classify_starved(const AbiBenchmarkWorld* world, AbiBenchmarkTables* tables, uint32_t classified, AbiBenchmarkCounters counters)
{
    const TypeTable* table = &world->table;
    AbiTable* abi = &tables->abi[BUSTER_ABI_BENCHMARK_TIMED];
    static AbiBenchmarkSamples samples;
    Arena starved = abi_benchmark_starved_arena();
    bool valid = starved.base != 0 && abi_benchmark_tables_start(tables, BUSTER_ABI_BENCHMARK_REFERENCE, table, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES) &&
                 abi_classify_all(&tables->abi[BUSTER_ABI_BENCHMARK_REFERENCE], table) == classified;
    valid = valid && abi_benchmark_tables_start(tables, BUSTER_ABI_BENCHMARK_TIMED, table, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES) &&
            abi_benchmark_classify_all_starved(abi, table, &starved) == classified;
    valid = valid && abi_benchmark_same_tables(abi, &tables->abi[BUSTER_ABI_BENCHMARK_REFERENCE], table->types.count, "classify-all with no scratch");
    const AbiBenchmarkCase benchmark = {
        .kind = BUSTER_ABI_BENCHMARK_CLASSIFY_ALL,
        .name = "no-scratch",
        .table = table,
        .abi = abi,
        .arena = &tables->arena[BUSTER_ABI_BENCHMARK_TIMED],
        .vector_bytes = BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES,
        .items = classified,
        .classified = classified,
        .starved = &starved,
    };
    valid = valid && abi_benchmark_measure(&benchmark, counters, &samples);
    if (valid)
    {
        printf("classify-all with no scratch, %u-byte vectors\n", BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES);
        abi_benchmark_print(benchmark.name, &samples, "type", 0, 0);
    }
    else
    {
        fprintf(stderr, "benchmark-calling-convention: classify-all with no scratch failed or classified otherwise\n");
    }
    arena_release(&starved);
    return valid;
}

// The links of the reversed chain.
#define BUSTER_ABI_BENCHMARK_CHAIN_LINKS 1024

// A chain of records against the table's order: tags R0 to R1023 made first, then completed from the last, R1023 {int}
// and Ri {Ri+1}, so that each needs the one after it. classify-all takes it by its levels after its scans, and with no
// scratch by its scans alone, a scan per link. Every link must be classified, and be what {int} is.
static bool abi_benchmark_chain(Arena* arena, AbiBenchmarkCounters counters)
{
    static AbiBenchmarkSamples samples;
    const uint32_t links = BUSTER_ABI_BENCHMARK_CHAIN_LINKS;
    const TypeCapacity capacity = {
        .types = links + BUSTER_ABI_BENCHMARK_SLACK_TYPES,
        .parameters = 1,
        .tags = links,
        .members = links,
    };
    const uint64_t length = arena->length;
    TypeTable table;
    Arena type_arena = arena_reserve(type_capacity_bytes(capacity));
    Arena index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    Arena starved = abi_benchmark_starved_arena();
    TypeId* chain = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, links);
    bool valid = chain != 0 && starved.base != 0 && type_initialize(&table, &type_arena, &index_arena, capacity);
    Arena abi_arena = arena_reserve(valid ? abi_capacity_bytes(table.types.capacity) : 0);
    for (uint32_t i = 0; valid && i < links; i += 1)
    {
        chain[i] = type_tag(&table, BUSTER_TYPE_KIND_STRUCT, i + 1);
        valid = chain[i] != 0;
    }
    const TypeRecordAttributes attributes = {
        .packed = false,
        .pack_log2 = 0,
        .alignment_log2 = 0,
    };
    for (uint32_t i = links; valid && i > 0; i -= 1)
    {
        const TypeMember member = {
            .name = 1,
            .type = i == links ? BUSTER_TYPE_INT : chain[i],
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
            .alignment_log2 = 0,
            .packed = false,
        };
        valid = type_complete_record(&table, chain[i - 1], &member, 1, attributes);
    }
    AbiTable abi;
    for (uint32_t route = 0; valid && route < 2; route += 1)
    {
        const AbiBenchmarkCase benchmark = {
            .kind = BUSTER_ABI_BENCHMARK_CLASSIFY_ALL,
            .name = route == 0 ? "chain" : "chain-starved",
            .table = &table,
            .abi = &abi,
            .arena = &abi_arena,
            .vector_bytes = BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES,
            .items = links,
            .classified = links,
            .starved = route == 0 ? 0 : &starved,
        };
        valid = abi_benchmark_prepare(&benchmark) && abi_benchmark_execute(&benchmark);
        for (uint32_t i = 0; valid && i < links; i += 1)
        {
            const uint32_t index = chain[i] >> BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t integer = BUSTER_TYPE_INT >> BUSTER_TYPE_INDEX_SHIFT;
            valid = abi.rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES] == abi.rows[(uint64_t)integer * BUSTER_ABI_ROW_BYTES] && abi.stack_bytes[index] == 8;
        }
        valid = valid && abi_benchmark_measure(&benchmark, counters, &samples);
        if (valid)
        {
            printf("a chain of %u records against the table's order, %s\n", links, route == 0 ? "with scratch" : "with no scratch");
            abi_benchmark_print(benchmark.name, &samples, "type", 0, 0);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-calling-convention: the reversed chain failed or classified otherwise than {int}\n");
    }
    arena_release(&starved);
    arena_release(&abi_arena);
    arena_release(&index_arena);
    arena_release(&type_arena);
    arena_pop_to(arena, length);
    return valid;
}

static bool abi_benchmark_classification(const AbiBenchmarkWorld* world, Arena* arena, AbiBenchmarkOptions options, AbiBenchmarkCounters counters)
{
    const TypeTable* table = &world->table;
    const uint64_t length = arena->length;
    AbiBenchmarkTables tables = { 0 };
    TypeId* types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, table->types.count);
    uint32_t* level_of = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, table->types.count);
    bool valid = types != 0 && level_of != 0 && abi_benchmark_tables_open(&tables, table);
    if (valid)
    {
        const uint32_t classified = abi_benchmark_classifiable(table, types);
        abi_benchmark_describe(world, classified, abi_benchmark_levels(table, types, classified, level_of));
        for (uint32_t w = 0; valid && w < BUSTER_ARRAY_LENGTH(abi_benchmark_widths); w += 1)
        {
            valid = abi_benchmark_classify_all(world, &tables, w, classified, options, counters);
        }
        valid = valid && abi_benchmark_batches(world, &tables, types, classified, arena, counters);
        valid = valid && abi_benchmark_classify_starved(world, &tables, classified, counters);
        valid = valid && abi_benchmark_chain(arena, counters);
    }
    abi_benchmark_tables_close(&tables);
    arena_pop_to(arena, length);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Assignment

// The integers a named argument has most often, int and long more than once for their weight, and those an unnamed one
// has after the default argument promotions.
static const TypeId abi_benchmark_integers[] = {
    BUSTER_TYPE_INT,
    BUSTER_TYPE_INT,
    BUSTER_TYPE_INT,
    BUSTER_TYPE_INT,
    BUSTER_TYPE_UINT,
    BUSTER_TYPE_LONG,
    BUSTER_TYPE_LONG,
    BUSTER_TYPE_ULONG,
    BUSTER_TYPE_ULONG,
    BUSTER_TYPE_LLONG,
    BUSTER_TYPE_CHAR,
    BUSTER_TYPE_UCHAR,
    BUSTER_TYPE_SHORT,
    BUSTER_TYPE_USHORT,
    BUSTER_TYPE_BOOL,
};

static const TypeId abi_benchmark_promoted[] = {
    BUSTER_TYPE_INT,
    BUSTER_TYPE_INT,
    BUSTER_TYPE_UINT,
    BUSTER_TYPE_LONG,
    BUSTER_TYPE_ULONG,
    BUSTER_TYPE_LLONG,
};

// The types the calls draw from besides the integers: the world's pointers, its records that pass in registers and in
// memory, and the rare ones (long double, __int128, the complex types and a vector).
#define BUSTER_ABI_BENCHMARK_RARE_TYPES 5

typedef struct AbiBenchmarkPools AbiBenchmarkPools;
struct AbiBenchmarkPools
{
    TypeId* pointers;
    uint32_t pointer_count;
    TypeId* register_records;
    uint32_t register_record_count;
    TypeId* memory_records;
    uint32_t memory_record_count;
    TypeId rare[BUSTER_ABI_BENCHMARK_RARE_TYPES];
};

static bool abi_benchmark_pools(AbiBenchmarkPools* pools, const AbiBenchmarkWorld* world, const AbiTable* abi, Arena* arena)
{
    const TypeTable* table = &world->table;
    *pools = (AbiBenchmarkPools){
        .pointers = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, table->types.count),
        .register_records = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, world->record_count),
        .memory_records = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, world->record_count),
        .rare = {
            BUSTER_TYPE_LONG_DOUBLE,
            BUSTER_TYPE_INT128,
            BUSTER_TYPE_COMPLEX_DOUBLE,
            BUSTER_TYPE_COMPLEX_FLOAT,
            world->float_vector,
        },
    };
    const bool result = pools->pointers != 0 && pools->register_records != 0 && pools->memory_records != 0;
    for (uint32_t index = BUSTER_TYPE_BUILTIN_COUNT; result && index < table->types.count; index += 1)
    {
        if (table->types.kind[index] == BUSTER_TYPE_KIND_POINTER)
        {
            pools->pointers[pools->pointer_count] = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
            pools->pointer_count += 1;
        }
    }
    for (uint32_t r = 0; result && r < world->record_count; r += 1)
    {
        const uint32_t index = world->records[r] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint32_t pass = abi->pass[index];
        // Every record passes (none is UNSUPPORTED, nor larger than 4 GB): in registers, or else in memory.
        if ((pass & BUSTER_ABI_PASS_MEMORY) == 0)
        {
            pools->register_records[pools->register_record_count] = world->records[r];
            pools->register_record_count += 1;
        }
        else
        {
            pools->memory_records[pools->memory_record_count] = world->records[r];
            pools->memory_record_count += 1;
        }
    }
    return result && pools->pointer_count != 0;
}

// A named argument's type, or a result's: integers (40%) and pointers (38%) mostly, then double and float (10%),
// records that pass in registers (7%) and in memory (3%), and now and then (2%) a rare one.
static TypeId abi_benchmark_named_type(const AbiBenchmarkPools* pools, uint64_t* random)
{
    const uint64_t pick = abi_benchmark_below(random, 100);
    TypeId result = abi_benchmark_integers[abi_benchmark_below(random, BUSTER_ARRAY_LENGTH(abi_benchmark_integers))];
    if (pick < 40)
    {
    }
    else if (pick < 78)
    {
        result = pools->pointers[abi_benchmark_below(random, pools->pointer_count)];
    }
    else if (pick < 88)
    {
        result = abi_benchmark_below(random, 4) == 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
    }
    else if (pick < 95 && pools->register_record_count != 0)
    {
        result = pools->register_records[abi_benchmark_below(random, pools->register_record_count)];
    }
    else if (pick < 98 && pools->memory_record_count != 0)
    {
        result = pools->memory_records[abi_benchmark_below(random, pools->memory_record_count)];
    }
    else if (pick >= 98)
    {
        result = pools->rare[abi_benchmark_below(random, BUSTER_ABI_BENCHMARK_RARE_TYPES)];
    }
    return result;
}

// The simple mix's argument or result: an integer (45%), a pointer (40%) or a double or float (15%), or for an unnamed
// argument a promoted integer (45%), a pointer (35%) or a double (20%): each needs one register of one class, or an
// 8-byte stack slot.
static TypeId abi_benchmark_simple_type(const AbiBenchmarkPools* pools, uint64_t* random, bool unnamed)
{
    const uint64_t pick = abi_benchmark_below(random, 100);
    TypeId result = unnamed ? abi_benchmark_promoted[abi_benchmark_below(random, BUSTER_ARRAY_LENGTH(abi_benchmark_promoted))]
                            : abi_benchmark_integers[abi_benchmark_below(random, BUSTER_ARRAY_LENGTH(abi_benchmark_integers))];
    if (pick < 45)
    {
    }
    else if (pick < (unnamed ? 80 : 85))
    {
        result = pools->pointers[abi_benchmark_below(random, pools->pointer_count)];
    }
    else
    {
        result = unnamed || abi_benchmark_below(random, 4) != 0 ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_FLOAT;
    }
    return result;
}

// An unnamed argument's type, after the default argument promotions: integers (40%), pointers (35%) and doubles (22%),
// and now and then a long double or a record (1% each of those in registers and in memory).
static TypeId abi_benchmark_unnamed_type(const AbiBenchmarkPools* pools, uint64_t* random)
{
    const uint64_t pick = abi_benchmark_below(random, 100);
    TypeId result = abi_benchmark_promoted[abi_benchmark_below(random, BUSTER_ARRAY_LENGTH(abi_benchmark_promoted))];
    if (pick < 40)
    {
    }
    else if (pick < 75)
    {
        result = pools->pointers[abi_benchmark_below(random, pools->pointer_count)];
    }
    else if (pick < 97)
    {
        result = BUSTER_TYPE_DOUBLE;
    }
    else if (pick < 98)
    {
        result = BUSTER_TYPE_LONG_DOUBLE;
    }
    else if (pick < 99 && pools->register_record_count != 0)
    {
        result = pools->register_records[abi_benchmark_below(random, pools->register_record_count)];
    }
    else if (pick >= 99 && pools->memory_record_count != 0)
    {
        result = pools->memory_records[abi_benchmark_below(random, pools->memory_record_count)];
    }
    return result;
}

// Random calls, as columns of AbiSignatures, and the arguments' count.
typedef struct AbiBenchmarkCalls AbiBenchmarkCalls;
struct AbiBenchmarkCalls
{
    TypeId* results;
    uint32_t* counts;
    uint32_t* named;
    uint8_t* variadic;
    TypeId* arguments;
    uint32_t count;
    uint64_t argument_count;
    uint32_t variadic_count;
};

// `count` calls: most (78%) of 0 to 6 arguments, some (18%) of 4 to 14 and a few (4%) of 15 to 22; one in four
// variadic, with 1 to 3 named arguments; a result one time in four void. Their types are the mix's, or with `simple`
// the simple mix's.
static bool abi_benchmark_calls(AbiBenchmarkCalls* calls, const AbiBenchmarkPools* pools, uint32_t count, uint64_t seed, bool simple, Arena* arena)
{
    uint64_t random = seed;
    *calls = (AbiBenchmarkCalls){
        .results = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, count),
        .counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count),
        .named = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count),
        .variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count),
        .count = count,
    };
    bool result = calls->results != 0 && calls->counts != 0 && calls->named != 0 && calls->variadic != 0;
    for (uint32_t c = 0; result && c < count; c += 1)
    {
        const uint64_t shape = abi_benchmark_below(&random, 100);
        const uint64_t arguments_drawn = shape < 78 ? abi_benchmark_below(&random, 7) : shape < 96 ? 4 + abi_benchmark_below(&random, 11) : 15 + abi_benchmark_below(&random, 8);
        const uint32_t arguments = (uint32_t)arguments_drawn;
        const bool variadic = abi_benchmark_below(&random, 4) == 0;
        const uint32_t named = (uint32_t)(1 + abi_benchmark_below(&random, 3));
        calls->counts[c] = arguments;
        calls->named[c] = variadic && named < arguments ? named : arguments;
        calls->variadic[c] = variadic ? 1 : 0;
        const TypeId result = simple ? abi_benchmark_simple_type(pools, &random, false) : abi_benchmark_named_type(pools, &random);
        calls->results[c] = abi_benchmark_below(&random, 4) == 0 ? BUSTER_TYPE_VOID : result;
        calls->argument_count += arguments;
        calls->variadic_count += variadic ? 1 : 0;
    }
    calls->arguments = result ? BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, calls->argument_count) : 0;
    result = result && calls->arguments != 0;
    uint64_t first = 0;
    for (uint32_t c = 0; result && c < count; c += 1)
    {
        for (uint32_t a = 0; a < calls->counts[c]; a += 1)
        {
            const bool unnamed = a >= calls->named[c];
            if (simple)
            {
                calls->arguments[first + a] = abi_benchmark_simple_type(pools, &random, unnamed);
            }
            else
            {
                calls->arguments[first + a] = unnamed ? abi_benchmark_unnamed_type(pools, &random) : abi_benchmark_named_type(pools, &random);
            }
        }
        first += calls->counts[c];
    }
    return result;
}

// What abi_assign leaves to its per-call kernel, abi_assign_lanes, and why (a call may have several reasons).
typedef struct AbiBenchmarkDeferred AbiBenchmarkDeferred;
struct AbiBenchmarkDeferred
{
    uint64_t arguments;
    uint32_t calls;
    uint32_t uncounted;
    uint32_t inexact;
    uint32_t padded;
    uint32_t wide_named;
    uint32_t empty;
    uint32_t general_low;
    uint32_t demoted;
    uint32_t long_calls;
};

// abi_assign's rules for which calls go to abi_assign_lanes, mirrored here a call at a time (keep them in step with
// abi_assign's deferred_lanes), since its results don't tell: the kernel's are the same. The prefix sums of a call's
// registers and stack slots count every argument that needs a register as using it, so a call goes to the kernel when an
// argument is UNCOUNTED, doesn't fit (the sum of a class it needs past the registers) while a register it needs is
// still free (inexact), goes to the stack at an offset its alignment doesn't divide or aligned to more than 32 bits hold
// (padded), is a named WIDE vector of a variadic call, is on the stack with a slot that depends on clang's count (EMPTY,
// which may take none, or GROWN), or has a LOW_GENERAL low eightbyte, or when the call's result is a vector demoted to a
// hidden pointer, which clang doesn't count, or the call has more arguments than its 16-bit register sums can count
// (BUSTER_ABI_COUNTED_ARGUMENTS).
static AbiBenchmarkDeferred abi_benchmark_deferred(const AbiTable* abi, AbiSignatures signatures)
{
    AbiBenchmarkDeferred result = { 0 };
    uint64_t first = 0;
    for (uint32_t c = 0; c < signatures.count; c += 1)
    {
        const uint32_t count = signatures.counts[c];
        const uint32_t named = signatures.named[c] < count ? signatures.named[c] : count;
        const bool variadic = signatures.variadic[c] != 0;
        const AbiReturn call_result = abi->results[signatures.results[c] >> BUSTER_TYPE_INDEX_SHIFT];
        const bool hidden = call_result.kind == BUSTER_ABI_RESULT_MEMORY;
        uint32_t integers = hidden ? 1 : 0;
        uint32_t vectors = 0;
        uint64_t stack = 0;
        bool uncounted = false;
        bool inexact = false;
        bool padded = false;
        bool wide_named = false;
        bool empty = false;
        bool general_low = false;
        const bool demoted = count != 0 && hidden && (call_result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0;
        const bool long_call = count > BUSTER_ABI_COUNTED_ARGUMENTS;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t index = signatures.arguments[first + i] >> BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t pass = abi->pass[index];
            const bool unnamed = i >= named;
            const bool wide = (pass & BUSTER_ABI_PASS_WIDE) != 0;
            const bool in_memory = (pass & BUSTER_ABI_PASS_MEMORY) != 0 || (wide && unnamed);
            const uint32_t need_integers = in_memory ? 0 : ((pass & BUSTER_ABI_PASS_LOW_INTEGER) != 0 ? 1u : 0u) + ((pass & BUSTER_ABI_PASS_HIGH_INTEGER) != 0 ? 1u : 0u);
            const uint32_t need_vectors = in_memory ? 0 : ((pass & BUSTER_ABI_PASS_LOW_SSE) != 0 ? 1u : 0u) + ((pass & BUSTER_ABI_PASS_HIGH_SSE) != 0 ? 1u : 0u);
            const bool fits = (need_integers == 0 || integers + need_integers <= BUSTER_ABI_INTEGER_ARGUMENTS) &&
                              (need_vectors == 0 || vectors + need_vectors <= BUSTER_ABI_VECTOR_ARGUMENTS);
            const bool spills = need_integers + need_vectors != 0 && !fits;
            const bool on_stack = (in_memory || spills) && (pass & BUSTER_ABI_PASS_IGNORE) == 0;
            const uint32_t alignment_log2 = (pass & BUSTER_ABI_PASS_ALIGNMENT) >> BUSTER_ABI_PASS_ALIGNMENT_SHIFT;
            const uint64_t alignment = 1ull << alignment_log2;
            uncounted = uncounted || (pass & BUSTER_ABI_PASS_UNCOUNTED) != 0;
            inexact = inexact || (spills && ((need_integers != 0 && integers < BUSTER_ABI_INTEGER_ARGUMENTS) || (need_vectors != 0 && vectors < BUSTER_ABI_VECTOR_ARGUMENTS)));
            padded = padded || (on_stack && ((stack & (alignment - 1)) != 0 || alignment_log2 > 31));
            wide_named = wide_named || (wide && variadic && !unnamed);
            empty = empty || (on_stack && (pass & (BUSTER_ABI_PASS_EMPTY | BUSTER_ABI_PASS_GROWN)) != 0);
            general_low = general_low || (pass & BUSTER_ABI_PASS_LOW_GENERAL) != 0;
            integers += need_integers;
            vectors += need_vectors;
            stack += on_stack ? abi->stack_bytes[index] : 0;
        }
        const bool deferred = uncounted || inexact || padded || wide_named || empty || general_low || demoted || long_call;
        result.arguments += deferred ? count : 0;
        result.calls += deferred ? 1 : 0;
        result.uncounted += uncounted ? 1 : 0;
        result.inexact += inexact ? 1 : 0;
        result.padded += padded ? 1 : 0;
        result.wide_named += wide_named ? 1 : 0;
        result.empty += empty ? 1 : 0;
        result.general_low += general_low ? 1 : 0;
        result.demoted += demoted ? 1 : 0;
        result.long_calls += long_call ? 1 : 0;
        first += count;
    }
    return result;
}

// Whether each call of a batch, assigned alone, gets the locations and the call the batch gave it.
static bool abi_benchmark_alone(const AbiTable* abi, AbiSignatures signatures, const AbiLocation* locations, const AbiCall* calls, AbiLocation* alone, const char* what)
{
    uint32_t differing = UINT32_MAX;
    uint64_t first = 0;
    for (uint32_t c = 0; differing == UINT32_MAX && c < signatures.count; c += 1)
    {
        const uint32_t count = signatures.counts[c];
        const AbiSignatures one = {
            .results = signatures.results + c,
            .counts = signatures.counts + c,
            .named = signatures.named + c,
            .variadic = signatures.variadic + c,
            .count = 1,
            .arguments = signatures.arguments + first,
        };
        AbiCall call;
        abi_assign(abi, one, alone, &call);
        const bool same = memcmp(alone, locations + first, count * sizeof(AbiLocation)) == 0 && memcmp(&call, &calls[c], sizeof(AbiCall)) == 0;
        differing = same ? UINT32_MAX : c;
        first += count;
    }
    if (differing != UINT32_MAX)
    {
        fprintf(stderr, "benchmark-calling-convention: %s: call %u assigned alone differs from its batch's\n", what, differing);
    }
    return differing == UINT32_MAX;
}

// One mix's calls over `abi`: assigned over poison, checked by each call assigned alone and, for the default seed and
// --count, by the frozen digest, then timed.
static bool abi_benchmark_assign_mix(const AbiTable* abi, const TypeTable* table, const AbiBenchmarkPools* pools, bool simple, Arena* arena, AbiBenchmarkOptions options,
                                     AbiBenchmarkCounters counters)
{
    static AbiBenchmarkSamples samples;
    const char* name = simple ? "assign-simple" : "assign";
    const uint64_t length = arena->length;
    const uint32_t call_count = options.count;
    AbiBenchmarkCalls calls;
    bool valid = abi_benchmark_calls(&calls, pools, call_count, options.seed ^ 0x5bd1e995ull, simple, arena);
    AbiLocation* locations = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, AbiLocation, calls.argument_count) : 0;
    AbiCall* results = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, AbiCall, call_count) : 0;
    // Room for the longest call assigned alone.
    AbiLocation* alone = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, AbiLocation, calls.argument_count) : 0;
    valid = valid && locations != 0 && results != 0 && alone != 0;
    if (valid)
    {
        const AbiSignatures signatures = {
            .results = calls.results,
            .counts = calls.counts,
            .named = calls.named,
            .variadic = calls.variadic,
            .count = calls.count,
            .arguments = calls.arguments,
        };
        memset(locations, 0x5a, calls.argument_count * sizeof(AbiLocation));
        memset(results, 0x5a, (uint64_t)call_count * sizeof(AbiCall));
        abi_assign(abi, signatures, locations, results);
        valid = abi_benchmark_alone(abi, signatures, locations, results, alone, name) &&
                abi_benchmark_frozen(options, name, abi_benchmark_assignment_digest(results, call_count, locations, calls.argument_count),
                                     abi_benchmark_assign_digests[simple ? 1 : 0]);
        uint64_t kinds[4] = { 0 };
        for (uint64_t a = 0; valid && a < calls.argument_count; a += 1)
        {
            kinds[locations[a].kind & 3] += 1;
        }
        const AbiBenchmarkDeferred deferred = abi_benchmark_deferred(abi, signatures);
        const AbiBenchmarkCase benchmark = {
            .kind = BUSTER_ABI_BENCHMARK_ASSIGN,
            .name = name,
            .table = table,
            .abi = (AbiTable*)abi,
            .vector_bytes = BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES,
            .items = calls.argument_count,
            .signatures = signatures,
            .locations = locations,
            .calls = results,
        };
        valid = valid && abi_benchmark_measure(&benchmark, counters, &samples);
        if (valid)
        {
            const double arguments = (double)calls.argument_count;
            printf("%s, %u-byte vectors: %u calls (%u variadic), %" PRIu64 " arguments, %.2f per call: %.1f%% in registers, %.1f%% on the stack, %.1f%% split, %.1f%% "
                   "ignored\n",
                   benchmark.name, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES, calls.count, calls.variadic_count, calls.argument_count, arguments / calls.count,
                   100.0 * kinds[BUSTER_ABI_LOCATION_REGISTERS] / arguments, 100.0 * kinds[BUSTER_ABI_LOCATION_STACK] / arguments,
                   100.0 * kinds[BUSTER_ABI_LOCATION_SPLIT] / arguments, 100.0 * kinds[BUSTER_ABI_LOCATION_IGNORE] / arguments);
            printf("%s: the vector path left %.2f%% of the arguments (%u calls, %.2f%%) to abi_assign_lanes: %u calls uncounted, %u inexact, %u padded, %u named wide, "
                   "%u empty, %u general low, %u demoted, %u long\n",
                   benchmark.name, 100.0 * (double)deferred.arguments / arguments, deferred.calls, 100.0 * deferred.calls / calls.count, deferred.uncounted, deferred.inexact,
                   deferred.padded, deferred.wide_named, deferred.empty, deferred.general_low, deferred.demoted, deferred.long_calls);
            abi_benchmark_print(benchmark.name, &samples, "argument", 0, 0);
        }
        // The same calls, each assigned alone; and for the mix, only the calls the prefix sums leave to the per-call
        // kernel, each alone (single signatures whose kernel runs one lane of eight).
        AbiBenchmarkCase alone_case = benchmark;
        alone_case.kind = BUSTER_ABI_BENCHMARK_ASSIGN_ALONE;
        alone_case.name = simple ? "simple-alone" : "assign-alone";
        valid = valid && abi_benchmark_measure(&alone_case, counters, &samples);
        if (valid)
        {
            abi_benchmark_print(alone_case.name, &samples, "argument", 0, 0);
        }
        if (valid && !simple && deferred.calls != 0)
        {
            // The deferred calls' columns, gathered: each call alone through abi_benchmark_deferred.
            TypeId* kept_results = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, deferred.calls);
            uint32_t* kept_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, deferred.calls);
            uint32_t* kept_named = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, deferred.calls);
            uint8_t* kept_variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, deferred.calls);
            TypeId* kept_arguments = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, deferred.arguments + 1);
            valid = kept_results != 0 && kept_counts != 0 && kept_named != 0 && kept_variadic != 0 && kept_arguments != 0;
            uint32_t kept = 0;
            uint64_t kept_total = 0;
            uint64_t first = 0;
            for (uint32_t c = 0; valid && c < signatures.count; c += 1)
            {
                const AbiSignatures one = {
                    .results = signatures.results + c,
                    .counts = signatures.counts + c,
                    .named = signatures.named + c,
                    .variadic = signatures.variadic + c,
                    .count = 1,
                    .arguments = signatures.arguments + first,
                };
                if (abi_benchmark_deferred(abi, one).calls != 0)
                {
                    kept_results[kept] = signatures.results[c];
                    kept_counts[kept] = signatures.counts[c];
                    kept_named[kept] = signatures.named[c];
                    kept_variadic[kept] = signatures.variadic[c];
                    memcpy(kept_arguments + kept_total, signatures.arguments + first, signatures.counts[c] * sizeof(TypeId));
                    kept_total += signatures.counts[c];
                    kept += 1;
                }
                first += signatures.counts[c];
            }
            AbiBenchmarkCase deferred_case = alone_case;
            deferred_case.name = "deferred-alone";
            deferred_case.items = kept_total;
            deferred_case.signatures = (AbiSignatures){
                .results = kept_results,
                .counts = kept_counts,
                .named = kept_named,
                .variadic = kept_variadic,
                .count = kept,
                .arguments = kept_arguments,
            };
            valid = valid && kept_total != 0 && abi_benchmark_measure(&deferred_case, counters, &samples);
            if (valid)
            {
                printf("deferred-alone: the %u calls (%" PRIu64 " arguments) the prefix sums leave, each alone\n", kept, kept_total);
                abi_benchmark_print(deferred_case.name, &samples, "argument", 0, 0);
            }
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-calling-convention: %s failed or its results are wrong\n", name);
    }
    arena_pop_to(arena, length);
    return valid;
}

// The assignment over the world's types classified at the base width: the mix, then the simple mix.
static bool abi_benchmark_assignment(const AbiBenchmarkWorld* world, Arena* arena, AbiBenchmarkOptions options, AbiBenchmarkCounters counters)
{
    const TypeTable* table = &world->table;
    const uint64_t length = arena->length;
    Arena abi_arena = arena_reserve(abi_capacity_bytes(table->types.capacity));
    AbiTable abi;
    AbiBenchmarkPools pools;
    bool valid = abi_arena.base != 0 && abi_initialize(&abi, &abi_arena, table, BUSTER_ABI_BENCHMARK_BASE_VECTOR_BYTES);
    valid = valid && abi_classify_all(&abi, table) != 0 && abi_benchmark_pools(&pools, world, &abi, arena);
    valid = valid && abi_benchmark_assign_mix(&abi, table, &pools, false, arena, options, counters);
    valid = valid && abi_benchmark_assign_mix(&abi, table, &pools, true, arena, options, counters);
    if (!valid)
    {
        fprintf(stderr, "benchmark-calling-convention: the assignment failed or its results are wrong\n");
    }
    arena_release(&abi_arena);
    arena_pop_to(arena, length);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool abi_benchmark_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno == 0;
    if (result)
    {
        *value = (uint64_t)parsed;
    }
    return result;
}

int run_calling_convention_benchmark(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t count = BUSTER_ABI_BENCHMARK_DEFAULT_COUNT;
    const char* only = 0;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        uint64_t* value = 0;
        if (strcmp(argv[i], "--seed") == 0)
        {
            value = &seed;
        }
        else if (strcmp(argv[i], "--count") == 0)
        {
            value = &count;
        }
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[i + 1];
            i += 1;
        }
        else if (value != 0 && i + 1 < argc)
        {
            valid = valid && abi_benchmark_parse(argv[i + 1], value);
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && count >= BUSTER_ABI_BENCHMARK_MIN_COUNT && count <= BUSTER_ABI_BENCHMARK_MAX_COUNT;
    valid = valid && (only == 0 || strcmp(only, "classification") == 0 || strcmp(only, "assignment") == 0);
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--seed N] [--count N] [--only classification|assignment]\n", argv[0]);
    }
    else
    {
        const AbiBenchmarkOptions options = {
            .seed = seed,
            .count = (uint32_t)count,
            .classification = only == 0 || strcmp(only, "classification") == 0,
            .assignment = only == 0 || strcmp(only, "assignment") == 0,
        };
        const AbiBenchmarkCounters counters = {
            .cycles = abi_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
            .instructions = abi_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
            .branch_misses = abi_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES),
            .throttle = abi_benchmark_counter(PERF_TYPE_RAW, BUSTER_ABI_BENCHMARK_THROTTLE_EVENT),
        };
        printf("benchmark-calling-convention: seed %" PRIu64 ", %u types and calls, %d passes' medians; %s\n", seed, options.count, BUSTER_ABI_BENCHMARK_PASSES,
               seed == BUSTER_ABI_BENCHMARK_DEFAULT_SEED && options.count == BUSTER_ABI_BENCHMARK_DEFAULT_COUNT ? "results checked against main's frozen digests"
                                                                                                         : "no frozen digests for this seed and count");
        printf("throttle: PMCx1A0 umask 0x08, read as the dispatch slots Zen 5's power cap takes (#199)%s\n",
               counters.cycles < 0 ? "; hardware counters unavailable: cycles, instructions, mispredicts and throttle are zero"
               : counters.throttle < 0 ? "; its raw event is unavailable: throttle is zero"
                                       : "");
        Arena arena = arena_reserve(BUSTER_ABI_BENCHMARK_ARENA_BYTES);
        AbiBenchmarkWorld world;
        valid = arena.base != 0 && abi_benchmark_world_open(&world, &arena, options.count);
        if (valid)
        {
            abi_benchmark_world_build(&world, seed, options.count);
            valid = world.record_count != 0 && world.void_pointer != 0 && world.char_pointer != 0 && world.float_vector != 0;
            if (valid && options.classification)
            {
                valid = abi_benchmark_classification(&world, &arena, options, counters);
            }
            if (valid && options.assignment)
            {
                valid = abi_benchmark_assignment(&world, &arena, options, counters);
            }
            abi_benchmark_world_close(&world);
        }
        else
        {
            fprintf(stderr, "benchmark-calling-convention: the type table can't be made\n");
        }
        arena_release(&arena);
        if (counters.cycles >= 0)
        {
            close(counters.cycles);
        }
        if (counters.instructions >= 0)
        {
            close(counters.instructions);
        }
        if (counters.branch_misses >= 0)
        {
            close(counters.branch_misses);
        }
        if (counters.throttle >= 0)
        {
            close(counters.throttle);
        }
    }
    return valid ? 0 : 1;
}

