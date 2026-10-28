// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The backend's measurement (#136), which its two commands share: benchmark-backend's generated programs
// (tier0_benchmark.c, a translation unit of its own) and benchmark-backend-source's C files (tier0_source_benchmark.c,
// in main.c's checker unit). A corpus of Tier 0's functions, each compiled by both paths, which must give the same
// records, then timed: the vector path, its scalar twin, branch sizing and encoding, and the stream's validation
// (tier0_validate, the lowering's check of every function it lowers, #569) and its scalar twin, each a run over every
// function. Include after x86_64_encoder.c, branch_sizing.c and tier0.c.
//
// Layout: the clock and the counters; the corpus (tier0_benchmark_reserve, tier0_benchmark_add); the variants
// (tier0_benchmark_run); tier0_benchmark_measure.
#ifndef BUSTER_TIER0_BENCHMARK_MEASURE_INCLUDED
#define BUSTER_TIER0_BENCHMARK_MEASURE_INCLUDED

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

#define BUSTER_TIER0_BENCHMARK_PASSES 21
#define BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS (1u << 20)
#define BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT 0x1000008a0ull

#define BUSTER_TIER0_BENCHMARK_VECTOR 0
#define BUSTER_TIER0_BENCHMARK_SCALAR 1
#define BUSTER_TIER0_BENCHMARK_SIZING 2
#define BUSTER_TIER0_BENCHMARK_ENCODE 3
#define BUSTER_TIER0_BENCHMARK_VALIDATE 4
#define BUSTER_TIER0_BENCHMARK_VALIDATE_SCALAR 5
#define BUSTER_TIER0_BENCHMARK_VARIANTS 6

static const char* const tier0_benchmark_names[BUSTER_TIER0_BENCHMARK_VARIANTS] = {
    "vector",
    "scalar",
    "sizing",
    "encode",
    "validate",
    "validate-scalar",
};

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the counters

static uint64_t tier0_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

typedef struct Tier0BenchmarkCounters Tier0BenchmarkCounters;
struct Tier0BenchmarkCounters
{
    int cycles;
    int instructions;
    int throttle;
};

static int tier0_benchmark_counter(uint32_t type, uint64_t config)
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

static uint64_t tier0_benchmark_count(int counter)
{
    uint64_t result = 0;
    if (counter < 0 || read(counter, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        result = 0;
    }
    return result;
}

typedef struct Tier0BenchmarkReading Tier0BenchmarkReading;
struct Tier0BenchmarkReading
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t throttle;
    // A run's functions whose compile, sizing or encoding failed (tier0_benchmark_run): a run with any measured less.
    uint32_t failures;
};

static Tier0BenchmarkReading tier0_benchmark_read(Tier0BenchmarkCounters counters)
{
    Tier0BenchmarkReading reading;
    reading.cycles = tier0_benchmark_count(counters.cycles);
    reading.instructions = tier0_benchmark_count(counters.instructions);
    reading.throttle = tier0_benchmark_count(counters.throttle);
    reading.nanoseconds = tier0_benchmark_now();
    return reading;
}

static double tier0_benchmark_median(const double* values)
{
    double result = 0;
    for (uint32_t i = 0; i < BUSTER_TIER0_BENCHMARK_PASSES; i += 1)
    {
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t j = 0; j < BUSTER_TIER0_BENCHMARK_PASSES; j += 1)
        {
            less += values[j] < values[i] ? 1 : 0;
            equal += values[j] == values[i] ? 1 : 0;
        }
        if (less <= BUSTER_TIER0_BENCHMARK_PASSES / 2 && less + equal > BUSTER_TIER0_BENCHMARK_PASSES / 2)
        {
            result = values[i];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The corpus

// The functions measured, with each one's records as the vector path makes them (for sizing), its branch columns, and
// room for its sized records and its machine code.
typedef struct Tier0BenchmarkCorpus Tier0BenchmarkCorpus;
struct Tier0BenchmarkCorpus
{
    const Tier0Function** functions;
    Tier0Code* codes;
    X86Instruction** sized;
    uint32_t** offsets;
    uint32_t** ends;
    uint8_t** machine;
    uint32_t count;
    uint32_t capacity;
    uint64_t entries;
    uint64_t records;
    uint64_t deferred;
    uint64_t bytes;
};

// An empty corpus with room for `capacity` functions (BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS but in the tests), from
// `arena`: false if it has none.
static bool tier0_benchmark_reserve(Tier0BenchmarkCorpus* corpus, Arena* arena, uint32_t capacity)
{
    memset(corpus, 0, sizeof(*corpus));
    corpus->capacity = capacity;
    corpus->functions = BUSTER_ARENA_PUSH_ARRAY(arena, const Tier0Function*, capacity);
    corpus->codes = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Code, capacity);
    corpus->sized = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction*, capacity);
    corpus->offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t*, capacity);
    corpus->ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t*, capacity);
    corpus->machine = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, capacity);
    return corpus->functions != 0 && corpus->codes != 0 && corpus->sized != 0 && corpus->offsets != 0 && corpus->ends != 0 && corpus->machine != 0;
}

// What tier0_benchmark_add made of a function: it joined the corpus; Tier 0 refused it alike by both paths, with an
// error other than a resource's; the paths disagree on it; or memory ran out (the corpus is full, an arena has no room,
// or a path's own TIER0_ERROR_MEMORY), which never leaves a function out unnoticed.
typedef enum Tier0BenchmarkAdd
{
    TIER0_BENCHMARK_ADDED,
    TIER0_BENCHMARK_REFUSED,
    TIER0_BENCHMARK_DISAGREE,
    TIER0_BENCHMARK_MEMORY,
} Tier0BenchmarkAdd;

// `function`, compiled by both paths into `code_arena`, joins the corpus with room for its sized records and machine
// code from `arena`, when both paths give it the same records and the corpus has room. Changes nothing otherwise.
static Tier0BenchmarkAdd tier0_benchmark_add(Tier0BenchmarkCorpus* corpus, const Tier0Function* function, Arena* arena, Arena* code_arena)
{
    const uint32_t index = corpus->count;
    Tier0BenchmarkAdd result = TIER0_BENCHMARK_MEMORY;
    if (index < corpus->capacity)
    {
        const Tier0Code scalar = tier0_compile_scalar(function, code_arena);
        const Tier0Code vector = tier0_compile(function, code_arena);
        const bool same = scalar.error == vector.error && scalar.count == vector.count && scalar.branch_count == vector.branch_count &&
                          memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0;
        const bool memory = scalar.error == TIER0_ERROR_MEMORY || vector.error == TIER0_ERROR_MEMORY;
        result = memory ? TIER0_BENCHMARK_MEMORY : !same ? TIER0_BENCHMARK_DISAGREE : vector.error != TIER0_OK ? TIER0_BENCHMARK_REFUSED : TIER0_BENCHMARK_ADDED;
        X86Instruction* sized = result == TIER0_BENCHMARK_ADDED ? BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, (uint64_t)vector.count + 1) : 0;
        uint32_t* offsets = sized != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)vector.count + 1) : 0;
        uint32_t* ends = offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(vector.reference_count)) : 0;
        uint8_t* machine = ends != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_X86_ENCODER_CAPACITY(vector.count)) : 0;
        result = result == TIER0_BENCHMARK_ADDED && machine == 0 ? TIER0_BENCHMARK_MEMORY : result;
        if (result == TIER0_BENCHMARK_ADDED)
        {
            corpus->functions[index] = function;
            corpus->codes[index] = vector;
            corpus->sized[index] = sized;
            corpus->offsets[index] = offsets;
            corpus->ends[index] = ends;
            corpus->machine[index] = machine;
            corpus->count += 1;
            corpus->entries += function->count;
            corpus->records += vector.count;
            corpus->deferred += vector.deferred;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The variants

// One run of a variant over every function: compiling (into `scratch`, put back after each), sizing (each function's
// records copied fresh before the timing), encoding (the sized records) or validating (every function the corpus has is
// valid: a refusal is a failure).
static Tier0BenchmarkReading tier0_benchmark_run(uint32_t variant, Tier0BenchmarkCorpus* corpus, Arena* scratch, Tier0BenchmarkCounters counters)
{
    if (variant == BUSTER_TIER0_BENCHMARK_SIZING)
    {
        for (uint32_t f = 0; f < corpus->count; f += 1)
        {
            memcpy(corpus->sized[f], corpus->codes[f].records, (uint64_t)corpus->codes[f].count * sizeof(X86Instruction));
        }
    }
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    uint64_t bytes = 0;
    uint32_t failures = 0;
    const uint64_t mark = scratch->length;
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        const Tier0Code* code = &corpus->codes[f];
        if (variant == BUSTER_TIER0_BENCHMARK_VECTOR)
        {
            failures += tier0_compile(corpus->functions[f], scratch).error != TIER0_OK;
            arena_pop_to(scratch, mark);
        }
        else if (variant == BUSTER_TIER0_BENCHMARK_SCALAR)
        {
            failures += tier0_compile_scalar(corpus->functions[f], scratch).error != TIER0_OK;
            arena_pop_to(scratch, mark);
        }
        else if (variant == BUSTER_TIER0_BENCHMARK_SIZING)
        {
            failures += size_branches(corpus->sized[f], code->count, code->branches, code->targets, code->branch_count, corpus->offsets[f]).error != BRANCH_SIZING_OK;
        }
        else if (variant == BUSTER_TIER0_BENCHMARK_VALIDATE)
        {
            failures += tier0_validate(corpus->functions[f]).error != TIER0_OK;
        }
        else if (variant == BUSTER_TIER0_BENCHMARK_VALIDATE_SCALAR)
        {
            failures += tier0_validate_scalar(corpus->functions[f]).error != TIER0_OK;
        }
        else
        {
            const int64_t length = x86_encode(corpus->sized[f], code->count, corpus->machine[f], BUSTER_X86_ENCODER_CAPACITY(code->count), 0, corpus->ends[f]);
            failures += length <= 0;
            bytes += length > 0 ? (uint64_t)length : 0;
        }
    }
    const Tier0BenchmarkReading end = tier0_benchmark_read(counters);
    Tier0BenchmarkReading taken = {
        .nanoseconds = end.nanoseconds - begin.nanoseconds,
        .cycles = end.cycles - begin.cycles,
        .instructions = end.instructions - begin.instructions,
        .throttle = end.throttle - begin.throttle,
        .failures = failures,
    };
    corpus->bytes = variant == BUSTER_TIER0_BENCHMARK_ENCODE ? bytes : corpus->bytes;
    return taken;
}

// Times the variants over the corpus, each after a warm-up, in BUSTER_TIER0_BENCHMARK_PASSES passes that alternate which
// compile path runs first, and which validation (sizing before encoding, which takes the sized records), and prints the
// report under `name`: the corpus's totals, then each variant's medians per typed entry and per record. `only`, unless
// it is BUSTER_TIER0_BENCHMARK_VARIANTS, times one variant alone (encoding's sizing untimed), back to back;
// `after_scalar` runs the scalar twin, untimed, for that many ms before each pass of the vector path and of the
// validation. A run in which a function's compile, sizing, encoding or validation failed (`scratch` out of room, for
// one) measured less than the corpus: the first one, warm-up or timed, ends the measurement, which then prints that
// instead of a report and returns false. So does a corpus without a typed entry or a record, which has nothing to
// measure per entry: no run times it.
static bool tier0_benchmark_measure(Tier0BenchmarkCorpus* corpus, Arena* scratch, uint32_t only, uint64_t after_scalar, const char* name)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    // A definition that lowers to no entry (`int f(void) {}`, #514's review) has only its prologue and epilogue: a
    // corpus of them has records but no entry to divide them by.
    const bool measurable = corpus->entries != 0 && corpus->records != 0;
    // A warm-up of each variant (sizing before encoding, which takes the sized records), and the variant of the first run
    // that failed, BUSTER_TIER0_BENCHMARK_VARIANTS while none has.
    uint32_t failed = BUSTER_TIER0_BENCHMARK_VARIANTS;
    for (uint32_t v = 0; measurable && v < BUSTER_TIER0_BENCHMARK_VARIANTS && failed == BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
    {
        failed = tier0_benchmark_run(v, corpus, scratch, counters).failures != 0 ? v : failed;
    }
    double values[BUSTER_TIER0_BENCHMARK_VARIANTS][4][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; measurable && pass < BUSTER_TIER0_BENCHMARK_PASSES && failed == BUSTER_TIER0_BENCHMARK_VARIANTS; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_TIER0_BENCHMARK_VARIANTS && failed == BUSTER_TIER0_BENCHMARK_VARIANTS; k += 1)
        {
            // The compile paths alternate which runs first, and so do the validations; encoding always follows sizing.
            const uint32_t v = k < 2 ? ((pass & 1) != 0 ? 1 - k : k)
                               : k >= BUSTER_TIER0_BENCHMARK_VALIDATE && (pass & 1) != 0 ? BUSTER_TIER0_BENCHMARK_VALIDATE + BUSTER_TIER0_BENCHMARK_VALIDATE_SCALAR - k
                                                                                         : k;
            if (only != BUSTER_TIER0_BENCHMARK_VARIANTS && v != only && !(only == BUSTER_TIER0_BENCHMARK_ENCODE && v == BUSTER_TIER0_BENCHMARK_SIZING))
            {
                continue;
            }
            if ((v == BUSTER_TIER0_BENCHMARK_VECTOR || v == BUSTER_TIER0_BENCHMARK_VALIDATE) && after_scalar != 0)
            {
                const uint32_t twin = v == BUSTER_TIER0_BENCHMARK_VECTOR ? BUSTER_TIER0_BENCHMARK_SCALAR : BUSTER_TIER0_BENCHMARK_VALIDATE_SCALAR;
                const uint64_t until = tier0_benchmark_now() + after_scalar * 1000000ull;
                while (tier0_benchmark_now() < until && failed == BUSTER_TIER0_BENCHMARK_VARIANTS)
                {
                    failed = tier0_benchmark_run(twin, corpus, scratch, counters).failures != 0 ? twin : failed;
                }
            }
            const Tier0BenchmarkReading taken = failed == BUSTER_TIER0_BENCHMARK_VARIANTS ? tier0_benchmark_run(v, corpus, scratch, counters) : (Tier0BenchmarkReading){ 0 };
            failed = taken.failures != 0 ? v : failed;
            values[v][0][pass] = (double)taken.nanoseconds;
            values[v][1][pass] = (double)taken.cycles;
            values[v][2][pass] = (double)taken.instructions;
            values[v][3][pass] = (double)taken.throttle;
        }
    }
    if (!measurable)
    {
        printf("%s: no typed entry in the corpus's functions (%u), so nothing to measure per entry: no report\n", name, corpus->count);
    }
    else if (failed != BUSTER_TIER0_BENCHMARK_VARIANTS)
    {
        printf("%s: a function's %s failed, so its runs measured less than the corpus: no report\n", name,
               failed == BUSTER_TIER0_BENCHMARK_SIZING ? "sizing" : failed == BUSTER_TIER0_BENCHMARK_ENCODE ? "encoding" :
               failed == BUSTER_TIER0_BENCHMARK_VECTOR ? "vector path's compile" : failed == BUSTER_TIER0_BENCHMARK_SCALAR ? "scalar path's compile" :
               failed == BUSTER_TIER0_BENCHMARK_VALIDATE ? "validation" : "scalar validation");
    }
    else
    {
        // The functions whose validation the vector path takes, and their entries: the rest it leaves to the scalar
        // validator, which then runs after it.
        uint32_t taken = 0;
        uint64_t taken_entries = 0;
        for (uint32_t f = 0; f < corpus->count; f += 1)
        {
            const bool accepted = tier0_validate_accepts(corpus->functions[f]);
            taken += accepted ? 1 : 0;
            taken_entries += accepted ? corpus->functions[f]->count : 0;
        }
        printf("%s: %u functions, %" PRIu64 " typed entries, %" PRIu64 " records (%.3f per entry), %" PRIu64 " bytes; %" PRIu64 " entries deferred (%.2f%%); "
               "%u functions' validation (%" PRIu64 " entries) by the vector path alone\n",
               name, corpus->count, corpus->entries, corpus->records, (double)corpus->records / (double)corpus->entries, corpus->bytes, corpus->deferred,
               100.0 * (double)corpus->deferred / (double)corpus->entries, taken, taken_entries);
        printf("%-15s %12s %12s %12s %12s %12s %8s %12s\n", "variant", "ns/entry", "cyc/entry", "ins/entry", "ns/record", "cyc/record", "IPC", "thr/record");
        for (uint32_t v = 0; v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
        {
            if (only != BUSTER_TIER0_BENCHMARK_VARIANTS && v != only)
            {
                continue;
            }
            const double ns = tier0_benchmark_median(values[v][0]);
            const double cycles = tier0_benchmark_median(values[v][1]);
            const double instructions = tier0_benchmark_median(values[v][2]);
            const double throttle = tier0_benchmark_median(values[v][3]);
            const double e = (double)corpus->entries;
            const double r = (double)corpus->records;
            printf("%-15s %12.2f %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", tier0_benchmark_names[v], ns / e, cycles / e, instructions / e, ns / r, cycles / r,
                   cycles != 0 ? instructions / cycles : 0.0, throttle / r);
        }
    }
    const int descriptors[] = { counters.cycles, counters.instructions, counters.throttle };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        if (descriptors[i] >= 0)
        {
            close(descriptors[i]);
        }
    }
    return measurable && failed == BUSTER_TIER0_BENCHMARK_VARIANTS;
}

#endif
