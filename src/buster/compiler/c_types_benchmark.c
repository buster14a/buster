// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The type table's benchmark: each vector path against its scalar twin (docs/c-types.md has the numbers).
//
// Usage: build/compiler benchmark-types [--seed N] [--requests N] [--records N] [--pairs N] [--only NAME]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...). The cases:
//   - construction: --requests derivation requests (65,536) over a working set of derived types, as a checker asks
//     for them: pointers most often, then arrays, and atomic, aligned, vector, _BitInt and complex types, from a skewed
//     choice of bases so that a few types are asked for often. Once to warm the table, then timed: every request is a
//     hit, as most of a checker's are, one at a time (type_derive) and as one batch (type_derive_batch); then with
//     misses: the same stream into a fresh table each time, 1 in 8 requests new to it.
//   - layout: --records records (4,096) of 0 to 24 members of every kind, bit-fields among them, defined once and laid
//     out again before each run: one at a time in the order they were defined (type_record_layout, as at each closing
//     brace), and in levels of records whose members are complete, by the scalar layout (type_record_layout over each
//     level) and by the vector layout (type_layout_records on each level).
//   - conversions: --pairs pairs (65,536) of arithmetic codes drawn as a C program's operands are (int most often, then
//     the unsigned and long types, double, char and short, a few enums, complex and _BitInt types), through the scalar
//     twin and both kernels (vpermb's 64-entry integer table, vpermi2b's 128-entry one); the promotions; and the same
//     pairs as TypeIds through type_usual_arithmetic_types against a loop of type_usual_arithmetic.
// Each case first checks that its paths agree, then reports the median of BUSTER_TYPE_BENCHMARK_PASSES passes that
// alternate which path runs first, each run repeated until it lasts BUSTER_TYPE_BENCHMARK_SAMPLE_NS: nanoseconds, and
// cycles and instructions from the hardware counters (zero when unavailable), per item. The exit status is 1 if the
// arguments are invalid or the paths disagree.

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

#define BUSTER_TYPE_BENCHMARK_PASSES 21
#define BUSTER_TYPE_BENCHMARK_SAMPLE_NS 2000000ull
#define BUSTER_TYPE_BENCHMARK_MAX_REPEATS (1u << 16)
#define BUSTER_TYPE_BENCHMARK_VARIANTS 5
#define BUSTER_TYPE_BENCHMARK_WORKING_SET 4096
#define BUSTER_TYPE_BENCHMARK_MEMBERS 24

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the hardware counters

static uint64_t type_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// A counter of `config` (PERF_COUNT_HW_*) for the calling thread in user space, or -1 where perf_event_open refuses.
static int type_benchmark_counter(uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t type_benchmark_count(int counter)
{
    uint64_t result = 0;
    if (counter < 0 || read(counter, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        result = 0;
    }
    return result;
}

// The median of BUSTER_TYPE_BENCHMARK_PASSES values, by rank (no callback).
static double type_benchmark_median(const double* values)
{
    double result = 0;
    for (uint32_t i = 0; i < BUSTER_TYPE_BENCHMARK_PASSES; i += 1)
    {
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t j = 0; j < BUSTER_TYPE_BENCHMARK_PASSES; j += 1)
        {
            less += values[j] < values[i] ? 1 : 0;
            equal += values[j] == values[i] ? 1 : 0;
        }
        if (less <= BUSTER_TYPE_BENCHMARK_PASSES / 2 && less + equal > BUSTER_TYPE_BENCHMARK_PASSES / 2)
        {
            result = values[i];
        }
    }
    return result;
}

static uint64_t type_benchmark_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t result = *state;
    result = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9ull;
    result = (result ^ (result >> 27)) * 0x94D049BB133111EBull;
    return result ^ (result >> 31);
}

static uint64_t type_benchmark_below(uint64_t* state, uint64_t bound)
{
    return type_benchmark_random(state) % bound;
}

// A skewed choice of [0, bound): the smaller of two draws, twice, so that the first items are asked for most.
static uint64_t type_benchmark_skewed(uint64_t* state, uint64_t bound)
{
    const uint64_t a = type_benchmark_below(state, bound);
    const uint64_t b = type_benchmark_below(state, bound);
    const uint64_t c = type_benchmark_below(state, bound);
    const uint64_t smaller = a < b ? a : b;
    return smaller < c ? smaller : c;
}

typedef struct TypeBenchmarkTable TypeBenchmarkTable;
struct TypeBenchmarkTable
{
    TypeTable table;
    Arena arena;
    Arena index_arena;
};

static bool type_benchmark_open(TypeBenchmarkTable* table, uint32_t types)
{
    const TypeCapacity capacity = {
        .types = types,
        .parameters = types,
        .tags = types,
        .members = types * BUSTER_TYPE_BENCHMARK_MEMBERS,
    };
    table->arena = arena_reserve(type_capacity_bytes(capacity));
    table->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    return type_initialize(&table->table, &table->arena, &table->index_arena, capacity);
}

static void type_benchmark_close(TypeBenchmarkTable* table)
{
    arena_release(&table->arena);
    arena_release(&table->index_arena);
}

// What a case's run measured: nanoseconds, cycles and instructions per item, per pass and variant.
typedef struct TypeBenchmarkSamples TypeBenchmarkSamples;
struct TypeBenchmarkSamples
{
    double nanoseconds[BUSTER_TYPE_BENCHMARK_VARIANTS][BUSTER_TYPE_BENCHMARK_PASSES];
    double cycles[BUSTER_TYPE_BENCHMARK_VARIANTS][BUSTER_TYPE_BENCHMARK_PASSES];
    double instructions[BUSTER_TYPE_BENCHMARK_VARIANTS][BUSTER_TYPE_BENCHMARK_PASSES];
};

typedef struct TypeBenchmarkCounters TypeBenchmarkCounters;
struct TypeBenchmarkCounters
{
    int cycles;
    int instructions;
};

static void type_benchmark_print(const char* case_name, const char* const* names, uint32_t variants, const TypeBenchmarkSamples* samples, const char* unit)
{
    for (uint32_t v = 0; v < variants; v += 1)
    {
        const double nanoseconds = type_benchmark_median(samples->nanoseconds[v]);
        const double cycles = type_benchmark_median(samples->cycles[v]);
        const double instructions = type_benchmark_median(samples->instructions[v]);
        printf("  %-14s %-12s %9.3f ns/%s %9.2f cycles/%s %9.2f instructions/%s %5.2f IPC\n", case_name, names[v], nanoseconds, unit, cycles, unit, instructions, unit,
               cycles != 0 ? instructions / cycles : 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Construction

typedef struct TypeBenchmarkRequests TypeBenchmarkRequests;
struct TypeBenchmarkRequests
{
    uint8_t* kinds;
    uint8_t* flags;
    TypeId* bases;
    uint64_t* extents;
    TypeId* results;
    TypeId* expected;
    uint32_t count;
};

// The working set: types a translation unit's checker would have made by the time it asks again.
static uint32_t type_benchmark_working_set(TypeTable* table, uint64_t* random, TypeId* set)
{
    const TypeId scalars[] = {
        BUSTER_TYPE_CHAR,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_ULONG,
        BUSTER_TYPE_DOUBLE,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_CHAR | BUSTER_TYPE_CONST,
        BUSTER_TYPE_INT | BUSTER_TYPE_CONST,
    };
    uint32_t count = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(scalars); i += 1)
    {
        set[count] = scalars[i];
        count += 1;
    }
    while (count < BUSTER_TYPE_BENCHMARK_WORKING_SET)
    {
        const TypeId base = set[type_benchmark_skewed(random, count)];
        const uint64_t pick = type_benchmark_below(random, 10);
        TypeId made = 0;
        if (pick < 6)
        {
            made = type_pointer(table, base);
        }
        else if (pick < 8)
        {
            made = type_array(table, base, 1 + type_benchmark_below(random, 64));
        }
        else if (pick == 8)
        {
            const TypeId parameters[2] = {
                set[type_benchmark_skewed(random, count)],
                set[type_benchmark_skewed(random, count)],
            };
            const bool valid = type_kind(table, parameters[0]) == BUSTER_TYPE_KIND_POINTER && type_kind(table, parameters[1]) == BUSTER_TYPE_KIND_POINTER;
            made = valid ? type_function(table, BUSTER_TYPE_INT, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE) : 0;
        }
        else
        {
            made = type_qualify(table, base, BUSTER_TYPE_CONST);
        }
        table->last_error = TYPE_ERROR_NONE;
        if (made != 0)
        {
            set[count] = made;
            count += 1;
        }
    }
    return count;
}

// The stream: requests over the working set, and with `misses` one in eight of a kind or extent the set lacks.
static void type_benchmark_requests(TypeBenchmarkRequests* requests, const TypeId* set, uint32_t set_count, uint64_t seed, bool misses)
{
    uint64_t random = seed;
    for (uint32_t i = 0; i < requests->count; i += 1)
    {
        const uint64_t pick = type_benchmark_below(&random, 100);
        const TypeId base = set[type_benchmark_skewed(&random, set_count)];
        uint8_t kind = BUSTER_TYPE_KIND_POINTER;
        uint64_t extent = 0;
        uint8_t flags = 0;
        TypeId request_base = base;
        if (pick < 55)
        {
            kind = BUSTER_TYPE_KIND_POINTER;
        }
        else if (pick < 75)
        {
            kind = BUSTER_TYPE_KIND_ARRAY;
            request_base = BUSTER_TYPE_CHAR;
            extent = 1 + type_benchmark_skewed(&random, 256);
        }
        else if (pick < 82)
        {
            kind = BUSTER_TYPE_KIND_ATOMIC;
            request_base = type_unqualified(base);
        }
        else if (pick < 88)
        {
            kind = BUSTER_TYPE_KIND_ALIGNED;
            extent = 4;
        }
        else if (pick < 93)
        {
            kind = BUSTER_TYPE_KIND_VECTOR;
            request_base = BUSTER_TYPE_INT;
            extent = 4;
        }
        else
        {
            kind = BUSTER_TYPE_KIND_BIT_INT;
            request_base = 0;
            extent = 2 + type_benchmark_below(&random, 64);
        }
        if (misses && type_benchmark_below(&random, 8) == 0)
        {
            kind = BUSTER_TYPE_KIND_ARRAY;
            request_base = base;
            extent = 1000 + i;
        }
        requests->kinds[i] = kind;
        requests->flags[i] = flags;
        requests->bases[i] = request_base;
        requests->extents[i] = extent;
    }
}

static __attribute__((noinline)) void type_benchmark_one_at_a_time(TypeTable* table, const TypeBenchmarkRequests* requests)
{
    for (uint32_t i = 0; i < requests->count; i += 1)
    {
        requests->results[i] = type_derive(table, requests->kinds[i], requests->flags[i], requests->bases[i], requests->extents[i]);
    }
}

static __attribute__((noinline)) void type_benchmark_batch(TypeTable* table, const TypeBenchmarkRequests* requests)
{
    const TypeKeys keys = {
        .kinds = requests->kinds,
        .flags = requests->flags,
        .bases = requests->bases,
        .extents = requests->extents,
    };
    type_derive_batch(table, keys, requests->count, requests->results);
}

// A table holding the working set, whose types *set gets.
static bool type_benchmark_construction_table(TypeBenchmarkTable* table, uint64_t seed, uint32_t requests, TypeId* set, uint32_t* set_count)
{
    uint64_t random = seed;
    const bool result = type_benchmark_open(table, BUSTER_TYPE_BENCHMARK_WORKING_SET * 4 + requests * 2);
    if (result)
    {
        *set_count = type_benchmark_working_set(&table->table, &random, set);
    }
    return result;
}

static bool type_benchmark_construction(uint64_t seed, uint32_t count, TypeBenchmarkCounters counters)
{
    TypeBenchmarkTable table;
    TypeId set[BUSTER_TYPE_BENCHMARK_WORKING_SET];
    uint32_t set_count = 0;
    Arena arena = arena_reserve(1ull << 30);
    TypeBenchmarkRequests requests = {
        .kinds = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .flags = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .bases = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .extents = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count),
        .results = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .expected = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .count = count,
    };
    bool valid = requests.kinds != 0 && requests.flags != 0 && requests.bases != 0 && requests.extents != 0 && requests.results != 0 && requests.expected != 0 &&
                 type_benchmark_construction_table(&table, seed, count, set, &set_count);
    static TypeBenchmarkSamples samples;
    const char* names[2] = {
        "one-at-a-time",
        "batch",
    };
    if (valid)
    {
        // Hits: warm the table with one pass, check both paths agree, then time them.
        type_benchmark_requests(&requests, set, set_count, seed ^ 0x77, false);
        type_benchmark_one_at_a_time(&table.table, &requests);
        memcpy(requests.expected, requests.results, count * sizeof(TypeId));
        type_benchmark_batch(&table.table, &requests);
        valid = memcmp(requests.expected, requests.results, count * sizeof(TypeId)) == 0;
        uint64_t repeats = 1;
        for (bool calibrated = false; valid && !calibrated; repeats *= 2)
        {
            const uint64_t begin = type_benchmark_now();
            for (uint64_t r = 0; r < repeats; r += 1)
            {
                type_benchmark_one_at_a_time(&table.table, &requests);
            }
            calibrated = type_benchmark_now() - begin >= BUSTER_TYPE_BENCHMARK_SAMPLE_NS || repeats >= BUSTER_TYPE_BENCHMARK_MAX_REPEATS;
        }
        for (uint32_t pass = 0; valid && pass < BUSTER_TYPE_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; turn < 2; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 2;
                const uint64_t cycles = type_benchmark_count(counters.cycles);
                const uint64_t instructions = type_benchmark_count(counters.instructions);
                const uint64_t begin = type_benchmark_now();
                for (uint64_t r = 0; r < repeats; r += 1)
                {
                    if (variant == 0)
                    {
                        type_benchmark_one_at_a_time(&table.table, &requests);
                    }
                    else
                    {
                        type_benchmark_batch(&table.table, &requests);
                    }
                    __asm__ volatile("" : : "r"(requests.results) : "memory");
                }
                const double items = (double)repeats * count;
                samples.nanoseconds[variant][pass] = (double)(type_benchmark_now() - begin) / items;
                samples.cycles[variant][pass] = (double)(type_benchmark_count(counters.cycles) - cycles) / items;
                samples.instructions[variant][pass] = (double)(type_benchmark_count(counters.instructions) - instructions) / items;
            }
        }
        if (valid)
        {
            printf("construction: %u requests, a working set of %u types, every request a hit (%u types, %u buckets)\n", count, set_count, table.table.types.count,
                   table.table.index.bucket_count);
            type_benchmark_print("hits", names, 2, &samples, "request");
        }
        type_benchmark_close(&table);
    }
    if (valid)
    {
        // Misses: a fresh table each run (made outside the timing), 1 in 8 requests new to it.
        type_benchmark_requests(&requests, set, set_count, seed ^ 0x99, true);
        TypeBenchmarkTable fresh[2];
        for (uint32_t pass = 0; valid && pass < BUSTER_TYPE_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; valid && turn < 2; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 2;
                valid = type_benchmark_construction_table(&fresh[variant], seed, count, set, &set_count);
                const uint64_t cycles = type_benchmark_count(counters.cycles);
                const uint64_t instructions = type_benchmark_count(counters.instructions);
                const uint64_t begin = type_benchmark_now();
                if (variant == 0)
                {
                    type_benchmark_one_at_a_time(&fresh[variant].table, &requests);
                    memcpy(requests.expected, requests.results, count * sizeof(TypeId));
                }
                else
                {
                    type_benchmark_batch(&fresh[variant].table, &requests);
                }
                samples.nanoseconds[variant][pass] = (double)(type_benchmark_now() - begin) / count;
                samples.cycles[variant][pass] = (double)(type_benchmark_count(counters.cycles) - cycles) / count;
                samples.instructions[variant][pass] = (double)(type_benchmark_count(counters.instructions) - instructions) / count;
                if (variant == 1 && turn == 1)
                {
                    valid = memcmp(requests.expected, requests.results, count * sizeof(TypeId)) == 0;
                }
                type_benchmark_close(&fresh[variant]);
            }
        }
        if (valid)
        {
            printf("construction: the same stream into a fresh table, 1 in 8 requests a miss\n");
            type_benchmark_print("misses", names, 2, &samples, "request");
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-types: construction failed or the paths disagree\n");
    }
    arena_release(&arena);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout

// Records of 0 to BUSTER_TYPE_BENCHMARK_MEMBERS members: scalars, pointers, arrays and earlier records, a third of the
// records with bit-fields, some packed or under #pragma pack.
static uint32_t type_benchmark_records(TypeTable* table, uint64_t seed, uint32_t count, TypeId* records, uint64_t* member_count)
{
    uint64_t random = seed;
    const TypeId members[] = {
        BUSTER_TYPE_CHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_DOUBLE,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_UCHAR,
        type_pointer(table, BUSTER_TYPE_VOID),
        type_pointer(table, BUSTER_TYPE_CHAR),
        type_array(table, BUSTER_TYPE_CHAR, 16),
        type_array(table, BUSTER_TYPE_INT, 4),
        BUSTER_TYPE_LONG_DOUBLE,
        type_vector(table, BUSTER_TYPE_FLOAT, 4, false),
    };
    uint32_t made = 0;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const bool bit_fields = type_benchmark_below(&random, 3) == 0;
        const uint32_t n = (uint32_t)type_benchmark_below(&random, BUSTER_TYPE_BENCHMARK_MEMBERS + 1);
        TypeMember list[BUSTER_TYPE_BENCHMARK_MEMBERS];
        for (uint32_t m = 0; m < n; m += 1)
        {
            TypeId type = members[type_benchmark_below(&random, BUSTER_ARRAY_LENGTH(members))];
            if (made != 0 && type_benchmark_below(&random, 8) == 0)
            {
                type = records[type_benchmark_skewed(&random, made)];
            }
            const bool bit_field = bit_fields && type_integer_width(table, type) != 0 && type_benchmark_below(&random, 2) == 0;
            list[m] = (TypeMember){
                .name = m + 1,
                .type = type,
                .bit_width = bit_field ? (uint32_t)(1 + type_benchmark_below(&random, type_integer_width(table, type))) : BUSTER_TYPE_NOT_BIT_FIELD,
                .alignment_log2 = 0,
                .packed = false,
            };
        }
        const TypeRecordAttributes attributes = {
            .packed = type_benchmark_below(&random, 16) == 0,
            .pack_log2 = type_benchmark_below(&random, 16) == 0 ? (uint8_t)(1 + type_benchmark_below(&random, 3)) : 0,
            .alignment_log2 = 0,
        };
        const TypeId record = type_tag(table, type_benchmark_below(&random, 6) == 0 ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, r + 1);
        // Records are laid out as they're made here, so that later ones can hold them; the runs lay them all out
        // again from type_benchmark_reset.
        if (record != 0 && type_complete_record(table, record, list, n, attributes))
        {
            records[made] = record;
            made += 1;
            *member_count += n;
        }
    }
    return made;
}

// Makes laid-out records pending again (defined, not complete), with their completion numbers from 1 again.
static void type_benchmark_reset(TypeTable* table, const TypeId* records, uint32_t count)
{
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint32_t index = type_index(records[r]);
        table->types.flags[index] &= (uint8_t)~BUSTER_TYPE_FLAG_COMPLETE;
        table->tags.completion[table->types.index[index]] = 0;
    }
    table->tags.completions = 0;
}

static __attribute__((noinline)) void type_benchmark_layout_scalar(TypeTable* table, const TypeId* records, uint32_t count)
{
    for (uint32_t r = 0; r < count; r += 1)
    {
        type_record_layout(table, records[r]);
    }
}

static __attribute__((noinline)) void type_benchmark_layout_vector(TypeTable* table, const TypeId* records, uint32_t count)
{
    type_layout_records(table, records, count);
}

// Records nested in later ones must be complete before those are laid out: a batch is a level of records whose members
// are all complete. Here every member record comes earlier in the array. The runs lay out the whole array: one at a
// time in the array's order, as at each record's closing brace, and the scalar and vector layouts the same levels, in
// the order of by_level (the array's within each level).
static uint32_t type_benchmark_levels(const TypeTable* table, const TypeId* records, uint32_t count, uint32_t* level_of, uint32_t* order, uint32_t* level_starts)
{
    uint32_t levels = 0;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint32_t tag = table->types.index[type_index(records[r])];
        uint32_t level = 0;
        for (uint32_t m = 0; m < table->tags.member_count[tag]; m += 1)
        {
            const TypeId type = table->members.type[table->tags.member_begin[tag] + m];
            const uint8_t kind = type_kind(table, type);
            if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
            {
                for (uint32_t earlier = 0; earlier < r; earlier += 1)
                {
                    level = records[earlier] == type && level_of[earlier] + 1 > level ? level_of[earlier] + 1 : level;
                }
            }
        }
        level_of[r] = level;
        levels = level + 1 > levels ? level + 1 : levels;
    }
    uint32_t position = 0;
    for (uint32_t level = 0; level < levels; level += 1)
    {
        level_starts[level] = position;
        for (uint32_t r = 0; r < count; r += 1)
        {
            if (level_of[r] == level)
            {
                order[position] = r;
                position += 1;
            }
        }
    }
    level_starts[levels] = position;
    return levels;
}

static bool type_benchmark_layout(uint64_t seed, uint32_t count, TypeBenchmarkCounters counters)
{
    TypeBenchmarkTable table;
    Arena arena = arena_reserve(1ull << 30);
    TypeId* records = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count);
    TypeId* by_level = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count);
    uint32_t* level_of = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count);
    uint32_t* level_starts = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count + 1);
    // Every member's offset, and member record 0's.
    uint64_t* offsets = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, (uint64_t)count * BUSTER_TYPE_BENCHMARK_MEMBERS + 1);
    uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count);
    bool valid = records != 0 && by_level != 0 && level_of != 0 && order != 0 && level_starts != 0 && offsets != 0 && sizes != 0 && type_benchmark_open(&table, count * 4 + 64);
    static TypeBenchmarkSamples samples;
    const char* names[3] = {
        "one-at-a-time",
        "scalar",
        "vector",
    };
    if (valid)
    {
        uint64_t member_count = 0;
        const uint32_t made = type_benchmark_records(&table.table, seed, count, records, &member_count);
        const uint32_t levels = type_benchmark_levels(&table.table, records, made, level_of, order, level_starts);
        for (uint32_t i = 0; i < made; i += 1)
        {
            by_level[i] = records[order[i]];
        }
        // The scalar layout's offsets and sizes, which the vector layout must give again.
        const TypeMembers* members = &table.table.members;
        memcpy(offsets, members->offset, members->count * sizeof(uint64_t));
        for (uint32_t r = 0; r < made; r += 1)
        {
            sizes[r] = type_size(&table.table, records[r]);
        }
        type_benchmark_reset(&table.table, by_level, made);
        // Record 0 is no member's: only the members' offsets are poisoned.
        memset(members->offset + 1, 0xff, (members->count - 1) * sizeof(uint64_t));
        for (uint32_t level = 0; level < levels; level += 1)
        {
            type_benchmark_layout_vector(&table.table, by_level + level_starts[level], level_starts[level + 1] - level_starts[level]);
        }
        valid = memcmp(offsets, members->offset, members->count * sizeof(uint64_t)) == 0;
        for (uint32_t r = 0; valid && r < made; r += 1)
        {
            valid = sizes[r] == type_size(&table.table, records[r]);
        }
        for (uint32_t pass = 0; valid && pass < BUSTER_TYPE_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; turn < 3; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 3;
                uint64_t elapsed = 0;
                uint64_t cycles = 0;
                uint64_t instructions = 0;
                uint64_t runs = 0;
                while (elapsed < BUSTER_TYPE_BENCHMARK_SAMPLE_NS && runs < BUSTER_TYPE_BENCHMARK_MAX_REPEATS)
                {
                    type_benchmark_reset(&table.table, by_level, made);
                    const uint64_t cycles_before = type_benchmark_count(counters.cycles);
                    const uint64_t instructions_before = type_benchmark_count(counters.instructions);
                    const uint64_t begin = type_benchmark_now();
                    if (variant == 2)
                    {
                        for (uint32_t level = 0; level < levels; level += 1)
                        {
                            type_benchmark_layout_vector(&table.table, by_level + level_starts[level], level_starts[level + 1] - level_starts[level]);
                        }
                    }
                    else
                    {
                        type_benchmark_layout_scalar(&table.table, variant == 0 ? records : by_level, made);
                    }
                    elapsed += type_benchmark_now() - begin;
                    cycles += type_benchmark_count(counters.cycles) - cycles_before;
                    instructions += type_benchmark_count(counters.instructions) - instructions_before;
                    runs += 1;
                }
                const double items = (double)runs * (double)member_count;
                samples.nanoseconds[variant][pass] = (double)elapsed / items;
                samples.cycles[variant][pass] = (double)cycles / items;
                samples.instructions[variant][pass] = (double)instructions / items;
            }
        }
        if (valid)
        {
            printf("layout: %u records, %" PRIu64 " members, %u levels of nesting\n", made, member_count, levels);
            type_benchmark_print("layout", names, 3, &samples, "member");
        }
        type_benchmark_close(&table);
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-types: layout failed or the layouts disagree\n");
    }
    arena_release(&arena);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Conversions

// A C program's operand codes: int most often, then unsigned, long and unsigned long, double, char, short, float and
// long long; a few complex and _BitInt codes (which the kernels defer) and others.
static uint8_t type_benchmark_code(uint64_t* random)
{
    const uint64_t pick = type_benchmark_below(random, 1000);
    uint8_t result = BUSTER_TYPE_CODE_INT;
    if (pick < 450)
    {
        result = BUSTER_TYPE_CODE_INT;
    }
    else if (pick < 560)
    {
        result = BUSTER_TYPE_CODE_UINT;
    }
    else if (pick < 680)
    {
        result = BUSTER_TYPE_CODE_LONG;
    }
    else if (pick < 780)
    {
        result = BUSTER_TYPE_CODE_ULONG;
    }
    else if (pick < 850)
    {
        result = BUSTER_TYPE_CODE_DOUBLE;
    }
    else if (pick < 900)
    {
        result = BUSTER_TYPE_CODE_CHAR;
    }
    else if (pick < 930)
    {
        result = BUSTER_TYPE_CODE_UCHAR;
    }
    else if (pick < 950)
    {
        result = BUSTER_TYPE_CODE_SHORT;
    }
    else if (pick < 970)
    {
        result = BUSTER_TYPE_CODE_FLOAT;
    }
    else if (pick < 985)
    {
        result = BUSTER_TYPE_CODE_LLONG;
    }
    else if (pick < 992)
    {
        result = BUSTER_TYPE_CODE_BOOL;
    }
    else if (pick < 996)
    {
        result = BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_DOUBLE;
    }
    else if (pick < 998)
    {
        result = BUSTER_TYPE_CODE_BIT_INT;
    }
    else
    {
        result = BUSTER_TYPE_CODE_LONG_DOUBLE;
    }
    return result;
}

typedef struct TypeBenchmarkPairs TypeBenchmarkPairs;
struct TypeBenchmarkPairs
{
    uint8_t* left;
    uint8_t* right;
    uint8_t* results;
    TypeId* left_types;
    TypeId* right_types;
    TypeId* type_results;
    uint32_t count;
};

static __attribute__((noinline)) void type_benchmark_conversion(TypeTable* table, TypeBenchmarkPairs* pairs, uint32_t variant)
{
    if (variant == 0)
    {
        type_usual_arithmetic_codes_scalar(pairs->left, pairs->right, pairs->count, pairs->results);
    }
    else if (variant == 1)
    {
        type_usual_arithmetic_codes(pairs->left, pairs->right, pairs->count, pairs->results);
    }
    else if (variant == 2)
    {
        type_usual_arithmetic_codes_pairs(pairs->left, pairs->right, pairs->count, pairs->results);
    }
    else if (variant == 3)
    {
        for (uint32_t i = 0; i < pairs->count; i += 1)
        {
            pairs->type_results[i] = type_usual_arithmetic(table, pairs->left_types[i], pairs->right_types[i]);
        }
    }
    else
    {
        type_usual_arithmetic_types(table, pairs->left_types, pairs->right_types, pairs->count, pairs->type_results);
    }
}

static __attribute__((noinline)) void type_benchmark_promotion(TypeBenchmarkPairs* pairs, uint32_t variant)
{
    if (variant == 0)
    {
        type_promote_codes_scalar(pairs->left, pairs->count, pairs->results);
    }
    else
    {
        type_promote_codes(pairs->left, pairs->count, pairs->results);
    }
}

static bool type_benchmark_conversions(uint64_t seed, uint32_t count, TypeBenchmarkCounters counters)
{
    TypeBenchmarkTable table;
    Arena arena = arena_reserve(1ull << 30);
    TypeBenchmarkPairs pairs = {
        .left = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .right = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .results = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .left_types = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .right_types = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .type_results = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .count = count,
    };
    uint8_t* expected = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count);
    TypeId* expected_types = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count);
    bool valid = pairs.left != 0 && pairs.right != 0 && pairs.results != 0 && pairs.left_types != 0 && pairs.right_types != 0 && pairs.type_results != 0 && expected != 0 &&
                 expected_types != 0 && type_benchmark_open(&table, 4096);
    static TypeBenchmarkSamples samples;
    const char* names[5] = {
        "scalar",
        "vpermb",
        "vpermi2b",
        "types-scalar",
        "types-batch",
    };
    if (valid)
    {
        uint64_t random = seed;
        // Two enums (of unsigned int and of unsigned char) and _BitInt(37) stand for those codes as TypeIds.
        const TypeId enumeration = type_tag(&table.table, BUSTER_TYPE_KIND_ENUM, 1);
        type_enum_complete(&table.table, enumeration, BUSTER_TYPE_UINT, 0);
        const TypeId bit_int = type_bit_int(&table.table, 37, false);
        for (uint32_t i = 0; i < count; i += 1)
        {
            pairs.left[i] = type_benchmark_code(&random);
            pairs.right[i] = type_benchmark_code(&random);
            pairs.left_types[i] = pairs.left[i] == BUSTER_TYPE_CODE_BIT_INT ? bit_int : BUSTER_TYPE_BUILTIN(pairs.left[i]);
            pairs.right_types[i] = pairs.right[i] == BUSTER_TYPE_CODE_BIT_INT ? bit_int : BUSTER_TYPE_BUILTIN(pairs.right[i]);
            if (pairs.left[i] == BUSTER_TYPE_CODE_UINT && type_benchmark_below(&random, 4) == 0)
            {
                pairs.left_types[i] = enumeration;
            }
        }
        type_benchmark_conversion(&table.table, &pairs, 0);
        memcpy(expected, pairs.results, count);
        for (uint32_t v = 1; valid && v < 3; v += 1)
        {
            type_benchmark_conversion(&table.table, &pairs, v);
            valid = memcmp(expected, pairs.results, count) == 0;
        }
        type_benchmark_conversion(&table.table, &pairs, 3);
        memcpy(expected_types, pairs.type_results, count * sizeof(TypeId));
        type_benchmark_conversion(&table.table, &pairs, 4);
        valid = valid && memcmp(expected_types, pairs.type_results, count * sizeof(TypeId)) == 0;
        uint64_t repeats = 1;
        for (bool calibrated = false; valid && !calibrated; repeats *= 2)
        {
            const uint64_t begin = type_benchmark_now();
            for (uint64_t r = 0; r < repeats; r += 1)
            {
                type_benchmark_conversion(&table.table, &pairs, 1);
            }
            calibrated = type_benchmark_now() - begin >= BUSTER_TYPE_BENCHMARK_SAMPLE_NS / 8 || repeats >= BUSTER_TYPE_BENCHMARK_MAX_REPEATS;
        }
        for (uint32_t pass = 0; valid && pass < BUSTER_TYPE_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; turn < 5; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 5;
                // The vector kernels are far faster: each variant runs until its sample is long enough.
                uint64_t runs = 0;
                uint64_t elapsed = 0;
                const uint64_t cycles = type_benchmark_count(counters.cycles);
                const uint64_t instructions = type_benchmark_count(counters.instructions);
                const uint64_t begin = type_benchmark_now();
                while (elapsed < BUSTER_TYPE_BENCHMARK_SAMPLE_NS && runs < repeats * 64)
                {
                    type_benchmark_conversion(&table.table, &pairs, variant);
                    __asm__ volatile("" : : "r"(pairs.results), "r"(pairs.type_results) : "memory");
                    runs += 1;
                    elapsed = type_benchmark_now() - begin;
                }
                const double items = (double)runs * count;
                samples.nanoseconds[variant][pass] = (double)elapsed / items;
                samples.cycles[variant][pass] = (double)(type_benchmark_count(counters.cycles) - cycles) / items;
                samples.instructions[variant][pass] = (double)(type_benchmark_count(counters.instructions) - instructions) / items;
            }
        }
        if (valid)
        {
            printf("conversions: %u pairs of operands' codes (and TypeIds, a quarter of the unsigned ints as an enum)\n", count);
            type_benchmark_print("conversions", names, 5, &samples, "pair");
        }
        for (uint32_t pass = 0; valid && pass < BUSTER_TYPE_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; turn < 2; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 2;
                uint64_t runs = 0;
                uint64_t elapsed = 0;
                const uint64_t cycles = type_benchmark_count(counters.cycles);
                const uint64_t instructions = type_benchmark_count(counters.instructions);
                const uint64_t begin = type_benchmark_now();
                while (elapsed < BUSTER_TYPE_BENCHMARK_SAMPLE_NS && runs < repeats * 64)
                {
                    type_benchmark_promotion(&pairs, variant);
                    __asm__ volatile("" : : "r"(pairs.results) : "memory");
                    runs += 1;
                    elapsed = type_benchmark_now() - begin;
                }
                const double items = (double)runs * count;
                samples.nanoseconds[variant][pass] = (double)elapsed / items;
                samples.cycles[variant][pass] = (double)(type_benchmark_count(counters.cycles) - cycles) / items;
                samples.instructions[variant][pass] = (double)(type_benchmark_count(counters.instructions) - instructions) / items;
            }
        }
        if (valid)
        {
            const char* promotion_names[2] = {
                "scalar",
                "vpermb",
            };
            type_benchmark_print("promotions", promotion_names, 2, &samples, "code");
        }
        type_benchmark_close(&table);
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-types: conversions failed or the kernels disagree\n");
    }
    arena_release(&arena);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool type_benchmark_parse(const char* text, uint64_t* value)
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

int run_type_benchmark(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t requests = 65536;
    uint64_t records = 4096;
    uint64_t pairs = 65536;
    const char* only = 0;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        uint64_t* value = 0;
        if (strcmp(argv[i], "--seed") == 0)
        {
            value = &seed;
        }
        else if (strcmp(argv[i], "--requests") == 0)
        {
            value = &requests;
        }
        else if (strcmp(argv[i], "--records") == 0)
        {
            value = &records;
        }
        else if (strcmp(argv[i], "--pairs") == 0)
        {
            value = &pairs;
        }
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[i + 1];
            i += 1;
        }
        else if (value != 0 && i + 1 < argc)
        {
            valid = valid && type_benchmark_parse(argv[i + 1], value);
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && requests >= 1 && requests <= (1u << 24) && records >= 1 && records <= (1u << 20) && pairs >= 1 && pairs <= (1u << 26);
    valid = valid && (only == 0 || strcmp(only, "construction") == 0 || strcmp(only, "layout") == 0 || strcmp(only, "conversions") == 0);
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--seed N] [--requests N] [--records N] [--pairs N] [--only construction|layout|conversions]\n", argv[0]);
    }
    else
    {
        const TypeBenchmarkCounters counters = {
            .cycles = type_benchmark_counter(PERF_COUNT_HW_CPU_CYCLES),
            .instructions = type_benchmark_counter(PERF_COUNT_HW_INSTRUCTIONS),
        };
        printf("benchmark-types: seed %" PRIu64 ", %d interleaved passes' medians%s\n", seed, BUSTER_TYPE_BENCHMARK_PASSES,
               counters.cycles < 0 ? " (hardware counters unavailable: cycles and instructions are zero)" : "");
        if (only == 0 || strcmp(only, "construction") == 0)
        {
            valid = type_benchmark_construction(seed, (uint32_t)requests, counters) && valid;
        }
        if (only == 0 || strcmp(only, "layout") == 0)
        {
            valid = type_benchmark_layout(seed, (uint32_t)records, counters) && valid;
        }
        if (only == 0 || strcmp(only, "conversions") == 0)
        {
            valid = type_benchmark_conversions(seed, (uint32_t)pairs, counters) && valid;
        }
        if (counters.cycles >= 0)
        {
            close(counters.cycles);
        }
        if (counters.instructions >= 0)
        {
            close(counters.instructions);
        }
    }
    return valid ? 0 : 1;
}
