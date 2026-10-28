// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The x86 semantics' benchmark (benchmark-x86-semantics): what x86_semantics.c costs a code generator, apart from
// encoding. Measurement helpers are original to this module, modeled on dataflow_benchmark.c's.
//
// Usage: build/compiler benchmark-x86-semantics [--requests N] [--functions N] [--seed N] [--after-scalar MS] [--only NAME]
//
//   - tables: the effects' records and index and the choice's tables, in bytes, and x86_semantics_initialize's median
//     time (each run rebuilds every table).
//   - choice: --requests requests (65,536) shaped like a code generator's constants (x86_semantics_benchmark_request:
//     mostly small values, add, sub, cmp and mov first, conservative live flags most of the time), by the scalar twin
//     (choose-scalar) and the AVX-512 path (choose-avx512), per request; the forms they give must be the same. Then the
//     chosen records' bytes against the longest form of each request's row that takes the value (what a selector that
//     always used the imm32 form would emit).
//   - effects: records of --functions functions (2,048) shaped like clang's -O0 code and like its -O2 code (branch
//     sizing's generator, branch_sizing_generate, whose filler has uncovered EVEX forms and empty records too), by the
//     scalar twin (effects-scalar) and the AVX-512 path (effects-avx512), per record, beside x86_encode of the same
//     records (encode), the step the effects come before; the sets must be the same.
//   - code: the run time and size of the choices that change the instruction sequence (or only its encoding), as blocks
//     of 64 instructions over 8 registers in a loop, per instruction: add r64 with an imm8 against the same value as an
//     imm32 (a form the chooser never picks for it), inc against add 1, xor r32, r32 against mov r32, 0, test r, r
//     against cmp r, 0, test r8, imm8 against test r64, imm32, and mov r32, imm32 against movabs.
// Each timed case is the median of BUSTER_X86_SEMANTICS_BENCHMARK_PASSES passes that alternate which path runs first,
// each run repeated until it lasts BUSTER_X86_SEMANTICS_BENCHMARK_SAMPLE_NS: nanoseconds, and from the hardware counters
// (zero when unavailable) cycles, instructions and the throttle (PMCx1A0 umask 0x08, read as the dispatch slots Zen 5's
// power cap takes, #199). --only NAME times one path alone, back to back; --after-scalar MS runs the scalar twin of the
// case for MS ms before each AVX-512 sample (100 to 300: the state a compiler's vector phases run in after lighter
// ones). Run it under the 9700X's lock pinned to one CPU (taskset -c 2). The exit status is 1 if the arguments are
// invalid, a table doesn't build, or the paths disagree.
//
// Layout: the counters and timing (x86_semantics_benchmark_*), the streams, the cases (tables, choice, effects, code)
// and run_x86_semantics_benchmark.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/branch_sizing_generate.c"
#pragma clang diagnostic pop
#include "buster/compiler/x86_semantics.c"

#define BUSTER_X86_SEMANTICS_BENCHMARK_PASSES 21
#define BUSTER_X86_SEMANTICS_BENCHMARK_SAMPLE_NS 2000000ull
#define BUSTER_X86_SEMANTICS_BENCHMARK_MAX_REPEATS (1u << 20)
#define BUSTER_X86_SEMANTICS_BENCHMARK_AFTER_SCALAR_MAX_MS 10000
#define BUSTER_X86_SEMANTICS_BENCHMARK_REQUESTS 65536
#define BUSTER_X86_SEMANTICS_BENCHMARK_FUNCTIONS 2048
// A function's records: sqlite3's at clang -O0 average about 90 (docs/branch-sizing.md).
#define BUSTER_X86_SEMANTICS_BENCHMARK_FUNCTION_RECORDS 96
// PMCx1A0 (dispatch slots not used) with its undocumented umask 0x08, as a raw perf config: the event's low byte in
// bits 0-7, the umask in bits 8-15 and the event's high nibble in bits 32-35. #199 reads it as the slots that Zen 5's
// per-core power cap withholds from a heavy vector loop at a boosted clock, an inference from its controls.
#define BUSTER_X86_SEMANTICS_BENCHMARK_THROTTLE_EVENT 0x1000008a0ull
#define BUSTER_X86_SEMANTICS_BENCHMARK_READOUT_WORDS 6
// The code case's block: instructions per loop iteration and iterations per run.
#define BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK 64
#define BUSTER_X86_SEMANTICS_BENCHMARK_ITERATIONS 4096

// ---------------------------------------------------------------------------------------------------------------------
// Counters and timing

typedef struct X86SemanticsBenchmarkCounters X86SemanticsBenchmarkCounters;
struct X86SemanticsBenchmarkCounters
{
    int cycles;
    int instructions;
    int throttle;
    bool available;
    uint64_t time_enabled;
    uint64_t time_running;
};

typedef struct X86SemanticsBenchmarkSample X86SemanticsBenchmarkSample;
struct X86SemanticsBenchmarkSample
{
    double nanoseconds;
    double cycles;
    double instructions;
    double throttle;
};

static X86SemanticsBenchmarkCounters x86_semantics_benchmark_open_counters(void)
{
    X86SemanticsBenchmarkCounters result = {
        .cycles = -1,
        .instructions = -1,
        .throttle = -1,
        .available = false,
    };
    struct perf_event_attr attributes = { 0 };
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = PERF_COUNT_HW_CPU_CYCLES;
    attributes.disabled = 1;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    attributes.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    result.cycles = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (result.cycles >= 0)
    {
        attributes.config = PERF_COUNT_HW_INSTRUCTIONS;
        attributes.disabled = 0;
        attributes.read_format = 0;
        result.instructions = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, result.cycles, PERF_FLAG_FD_CLOEXEC);
        result.available = result.instructions >= 0;
    }
    if (result.available)
    {
        attributes.type = PERF_TYPE_RAW;
        attributes.config = BUSTER_X86_SEMANTICS_BENCHMARK_THROTTLE_EVENT;
        result.throttle = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, result.cycles, PERF_FLAG_FD_CLOEXEC);
    }
    return result;
}

static void x86_semantics_benchmark_close_counters(X86SemanticsBenchmarkCounters* counters)
{
    if (counters->throttle >= 0)
    {
        close(counters->throttle);
    }
    if (counters->instructions >= 0)
    {
        close(counters->instructions);
    }
    if (counters->cycles >= 0)
    {
        close(counters->cycles);
    }
    *counters = (X86SemanticsBenchmarkCounters){
        .cycles = -1,
        .instructions = -1,
        .throttle = -1,
        .available = false,
    };
}

// Reads the group into `readout` (BUSTER_X86_SEMANTICS_BENCHMARK_READOUT_WORDS zeroed words, the last zero without the
// throttle). Returns false if the read fails or lacks a member.
static bool x86_semantics_benchmark_read_counters(const X86SemanticsBenchmarkCounters* counters, uint64_t* readout)
{
    const uint64_t members = counters->throttle >= 0 ? 3 : 2;
    const ssize_t bytes = read(counters->cycles, readout, BUSTER_X86_SEMANTICS_BENCHMARK_READOUT_WORDS * sizeof(uint64_t));
    return bytes == (ssize_t)((3 + members) * sizeof(uint64_t)) && readout[0] == members;
}

static void x86_semantics_benchmark_start_counters(X86SemanticsBenchmarkCounters* counters)
{
    if (counters->available)
    {
        // RESET clears the counts, not the enabled and running clocks: keep those, so that a sample's scaling uses its
        // own time.
        uint64_t readout[BUSTER_X86_SEMANTICS_BENCHMARK_READOUT_WORDS] = { 0 };
        const bool reset = ioctl(counters->cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
        const bool read_ok = reset && x86_semantics_benchmark_read_counters(counters, readout);
        if (read_ok)
        {
            counters->time_enabled = readout[1];
            counters->time_running = readout[2];
        }
        if (!read_ok || ioctl(counters->cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) != 0)
        {
            x86_semantics_benchmark_close_counters(counters);
        }
    }
}

static void x86_semantics_benchmark_stop_counters(X86SemanticsBenchmarkCounters* counters, X86SemanticsBenchmarkSample* sample)
{
    sample->cycles = 0;
    sample->instructions = 0;
    sample->throttle = 0;
    if (counters->available)
    {
        uint64_t readout[BUSTER_X86_SEMANTICS_BENCHMARK_READOUT_WORDS] = { 0 };
        const bool disabled = ioctl(counters->cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
        const bool read_ok = disabled && x86_semantics_benchmark_read_counters(counters, readout);
        if (read_ok && readout[1] >= counters->time_enabled && readout[2] > counters->time_running)
        {
            const double scale = (double)(readout[1] - counters->time_enabled) / (double)(readout[2] - counters->time_running);
            sample->cycles = (double)readout[3] * scale;
            sample->instructions = (double)readout[4] * scale;
            sample->throttle = (double)readout[5] * scale;
        }
        else
        {
            x86_semantics_benchmark_close_counters(counters);
        }
    }
}

static uint64_t x86_semantics_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static double x86_semantics_benchmark_median(double* samples)
{
    // Selection by rank: 21 entries.
    double result = 0;
    for (uint32_t candidate = 0; candidate < BUSTER_X86_SEMANTICS_BENCHMARK_PASSES; candidate += 1)
    {
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t other = 0; other < BUSTER_X86_SEMANTICS_BENCHMARK_PASSES; other += 1)
        {
            less += samples[other] < samples[candidate];
            equal += samples[other] == samples[candidate];
        }
        if (less <= BUSTER_X86_SEMANTICS_BENCHMARK_PASSES / 2 && less + equal > BUSTER_X86_SEMANTICS_BENCHMARK_PASSES / 2)
        {
            result = samples[candidate];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The streams

// splitmix64.
static uint64_t x86_semantics_benchmark_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t x = *state;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static uint32_t x86_semantics_benchmark_below(uint64_t* state, uint32_t bound)
{
    return (uint32_t)((x86_semantics_benchmark_random(state) >> 32) * bound >> 32);
}

// A code generator's constants: an operation and width by weight (per mille), and a value mostly small.
typedef struct X86SemanticsBenchmarkPattern X86SemanticsBenchmarkPattern;
struct X86SemanticsBenchmarkPattern
{
    uint8_t operation;
    uint8_t width;
    uint16_t weight;
};

static const X86SemanticsBenchmarkPattern x86_semantics_benchmark_patterns[] = {
    { X86_OPERATION_ADD, 8, 140 },
    { X86_OPERATION_SUB, 8, 80 },
    { X86_OPERATION_ADD, 4, 60 },
    { X86_OPERATION_CMP, 4, 120 },
    { X86_OPERATION_CMP, 8, 60 },
    { X86_OPERATION_MOV, 4, 120 },
    { X86_OPERATION_MOV, 8, 80 },
    { X86_OPERATION_MOV, 1, 20 },
    { X86_OPERATION_AND, 4, 40 },
    { X86_OPERATION_AND, 8, 20 },
    { X86_OPERATION_OR, 4, 20 },
    { X86_OPERATION_XOR, 4, 15 },
    { X86_OPERATION_TEST, 4, 40 },
    { X86_OPERATION_TEST, 8, 20 },
    { X86_OPERATION_SHL, 8, 40 },
    { X86_OPERATION_SHR, 4, 25 },
    { X86_OPERATION_SAR, 8, 20 },
    { X86_OPERATION_IMUL, 8, 25 },
    { X86_OPERATION_IMUL, 4, 15 },
    { X86_OPERATION_ADC, 8, 5 },
    { X86_OPERATION_SUB, 2, 5 },
    { X86_OPERATION_CMP, 1, 20 },
    { X86_OPERATION_PUSH, 8, 10 },
};

// A request: its row, a value (half in [0, 16], then small negatives, bytes, words, 32-bit and 64-bit values; a shift's
// count below its masking) and a context (a register 70% of the time, RAX 15% of those; every flag live 70% of the time,
// none otherwise; inc and dec allowed half the time).
static void x86_semantics_benchmark_request(uint64_t* state, uint16_t* row, int64_t* value, uint16_t* context)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_semantics_benchmark_patterns); i += 1)
    {
        total += x86_semantics_benchmark_patterns[i].weight;
    }
    uint32_t pick = x86_semantics_benchmark_below(state, total);
    uint32_t pattern = 0;
    while (pick >= x86_semantics_benchmark_patterns[pattern].weight)
    {
        pick -= x86_semantics_benchmark_patterns[pattern].weight;
        pattern += 1;
    }
    const X86SemanticsBenchmarkPattern* chosen = &x86_semantics_benchmark_patterns[pattern];
    *row = (uint16_t)x86_choice_row(chosen->operation, chosen->width);
    const bool shift = chosen->operation >= X86_OPERATION_ROL && chosen->operation <= X86_OPERATION_SAR;
    const uint32_t magnitude = x86_semantics_benchmark_below(state, 100);
    const uint64_t random = x86_semantics_benchmark_random(state);
    int64_t result = 0;
    if (shift)
    {
        result = 1 + (int64_t)x86_semantics_benchmark_below(state, chosen->width == 8 ? 63 : 31);
    }
    else if (magnitude < 50)
    {
        result = (int64_t)(random % 17);
    }
    else if (magnitude < 65)
    {
        result = -1 - (int64_t)(random % 16);
    }
    else if (magnitude < 80)
    {
        result = 17 + (int64_t)(random % 239);
    }
    else if (magnitude < 90)
    {
        result = 256 + (int64_t)(random % 65280);
    }
    else if (magnitude < 95)
    {
        result = (int64_t)(int32_t)random;
    }
    else
    {
        result = (int64_t)random;
    }
    // Keep the value one of the width, as a selector's are.
    const uint64_t mask = x86_width_mask(chosen->width);
    result = chosen->width == 8 || shift ? result : (int64_t)((uint64_t)result & mask);
    *value = result;
    const uint32_t where = x86_semantics_benchmark_below(state, 100);
    uint16_t word = where < 70 ? BUSTER_X86_CONTEXT_REGISTER : 0;
    word |= where < 10 ? BUSTER_X86_CONTEXT_ACCUMULATOR : 0;
    word |= x86_semantics_benchmark_below(state, 100) < 70 ? BUSTER_X86_FLAGS_STATUS : 0;
    word |= x86_semantics_benchmark_below(state, 2) ? BUSTER_X86_CONTEXT_INC_DEC : 0;
    *context = word;
}

// ---------------------------------------------------------------------------------------------------------------------
// The cases

// Each wrapper starts a 64-byte line, so that every build lays the paths' loops out the same way (dataflow_benchmark.c's
// lesson: docs/dataflow.md, "Where the scalar twin's code falls").
static __attribute__((noinline, aligned(64))) void x86_semantics_benchmark_choose_scalar(const uint16_t* rows, const int64_t* values, const uint16_t* contexts,
                                                                                         uint64_t count, uint16_t* forms)
{
    x86_choose_forms_scalar(rows, values, contexts, count, forms);
}

static __attribute__((noinline, aligned(64))) void x86_semantics_benchmark_choose_vector(const uint16_t* rows, const int64_t* values, const uint16_t* contexts,
                                                                                         uint64_t count, uint16_t* forms)
{
    x86_choose_forms(rows, values, contexts, count, forms);
}

static __attribute__((noinline, aligned(64))) void x86_semantics_benchmark_effects_scalar(const X86Instruction* records, uint64_t count, uint64_t* reads, uint64_t* writes)
{
    x86_instructions_effects_scalar(records, count, reads, writes);
}

static __attribute__((noinline, aligned(64))) void x86_semantics_benchmark_effects_vector(const X86Instruction* records, uint64_t count, uint64_t* reads, uint64_t* writes)
{
    x86_instructions_effects(records, count, reads, writes);
}

static __attribute__((noinline, aligned(64))) int64_t x86_semantics_benchmark_encode(const X86Instruction* records, uint64_t count, uint8_t* output, uint64_t capacity)
{
    return x86_encode(records, count, output, capacity, 0, 0);
}

// The paths a case times: the scalar twin, the AVX-512 path and x86_encode (for the effects).
typedef enum X86SemanticsBenchmarkPath
{
    X86_SEMANTICS_BENCHMARK_SCALAR,
    X86_SEMANTICS_BENCHMARK_VECTOR,
    X86_SEMANTICS_BENCHMARK_ENCODE,
    X86_SEMANTICS_BENCHMARK_PATHS,
} X86SemanticsBenchmarkPath;

// What a case runs on.
typedef struct X86SemanticsBenchmarkData X86SemanticsBenchmarkData;
struct X86SemanticsBenchmarkData
{
    bool effects;
    uint64_t count;
    uint16_t* rows;
    int64_t* values;
    uint16_t* contexts;
    uint16_t* forms;
    X86Instruction* records;
    uint64_t* reads;
    uint64_t* writes;
    uint8_t* code;
    uint64_t code_capacity;
};

static void x86_semantics_benchmark_once(X86SemanticsBenchmarkData* data, X86SemanticsBenchmarkPath path)
{
    if (!data->effects)
    {
        if (path == X86_SEMANTICS_BENCHMARK_SCALAR)
        {
            x86_semantics_benchmark_choose_scalar(data->rows, data->values, data->contexts, data->count, data->forms);
        }
        else
        {
            x86_semantics_benchmark_choose_vector(data->rows, data->values, data->contexts, data->count, data->forms);
        }
        __asm__ volatile("" : : "r"(data->forms) : "memory");
    }
    else
    {
        if (path == X86_SEMANTICS_BENCHMARK_SCALAR)
        {
            x86_semantics_benchmark_effects_scalar(data->records, data->count, data->reads, data->writes);
        }
        else if (path == X86_SEMANTICS_BENCHMARK_VECTOR)
        {
            x86_semantics_benchmark_effects_vector(data->records, data->count, data->reads, data->writes);
        }
        else
        {
            x86_semantics_benchmark_encode(data->records, data->count, data->code, data->code_capacity);
        }
        __asm__ volatile("" : : "r"(data->reads), "r"(data->writes), "r"(data->code) : "memory");
    }
}

static X86SemanticsBenchmarkSample x86_semantics_benchmark_run(X86SemanticsBenchmarkData* data, X86SemanticsBenchmarkPath path, uint64_t repeats,
                                                               X86SemanticsBenchmarkCounters* counters)
{
    X86SemanticsBenchmarkSample sample = { 0 };
    x86_semantics_benchmark_start_counters(counters);
    const uint64_t begin = x86_semantics_benchmark_now();
    for (uint64_t repeat = 0; repeat < repeats; repeat += 1)
    {
        x86_semantics_benchmark_once(data, path);
    }
    const uint64_t end = x86_semantics_benchmark_now();
    x86_semantics_benchmark_stop_counters(counters, &sample);
    sample.nanoseconds = (double)(end - begin);
    return sample;
}

// --after-scalar: the case's scalar twin for at least `milliseconds`, untimed.
static void x86_semantics_benchmark_after_scalar(X86SemanticsBenchmarkData* data, uint64_t milliseconds)
{
    const uint64_t end = x86_semantics_benchmark_now() + milliseconds * 1000000ull;
    while (x86_semantics_benchmark_now() < end)
    {
        x86_semantics_benchmark_once(data, X86_SEMANTICS_BENCHMARK_SCALAR);
    }
}

// Times the paths in `timed` (a bit per X86SemanticsBenchmarkPath) on the data: a warm-up and a repetition count that
// makes each run last BUSTER_X86_SEMANTICS_BENCHMARK_SAMPLE_NS, then the passes, the paths taking turns; and prints each
// path's medians per item.
static void x86_semantics_benchmark_case(const char* name, X86SemanticsBenchmarkData* data, uint32_t timed, uint64_t after_scalar, X86SemanticsBenchmarkCounters* counters)
{
    static const char* const path_names[2][X86_SEMANTICS_BENCHMARK_PATHS] = {
        { "choose-scalar", "choose-avx512", "" },
        { "effects-scalar", "effects-avx512", "encode" },
    };
    X86SemanticsBenchmarkCounters no_counters = {
        .cycles = -1,
        .instructions = -1,
        .throttle = -1,
        .available = false,
    };
    uint64_t repeats = 1;
    bool ready = false;
    while (!ready && repeats < BUSTER_X86_SEMANTICS_BENCHMARK_MAX_REPEATS)
    {
        double shortest = 1e30;
        for (uint32_t path = 0; path < X86_SEMANTICS_BENCHMARK_PATHS; path += 1)
        {
            if (timed & (1u << path))
            {
                const X86SemanticsBenchmarkSample sample = x86_semantics_benchmark_run(data, (X86SemanticsBenchmarkPath)path, repeats, &no_counters);
                shortest = sample.nanoseconds < shortest ? sample.nanoseconds : shortest;
            }
        }
        ready = shortest >= (double)BUSTER_X86_SEMANTICS_BENCHMARK_SAMPLE_NS;
        repeats = ready ? repeats : repeats * 2;
    }
    double nanoseconds[X86_SEMANTICS_BENCHMARK_PATHS][BUSTER_X86_SEMANTICS_BENCHMARK_PASSES] = { { 0 } };
    double cycles[X86_SEMANTICS_BENCHMARK_PATHS][BUSTER_X86_SEMANTICS_BENCHMARK_PASSES] = { { 0 } };
    double instructions[X86_SEMANTICS_BENCHMARK_PATHS][BUSTER_X86_SEMANTICS_BENCHMARK_PASSES] = { { 0 } };
    double throttle[X86_SEMANTICS_BENCHMARK_PATHS][BUSTER_X86_SEMANTICS_BENCHMARK_PASSES] = { { 0 } };
    for (uint32_t pass = 0; pass < BUSTER_X86_SEMANTICS_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t turn = 0; turn < X86_SEMANTICS_BENCHMARK_PATHS; turn += 1)
        {
            const uint32_t path = (pass + turn) % X86_SEMANTICS_BENCHMARK_PATHS;
            if (timed & (1u << path))
            {
                if (after_scalar != 0 && path == X86_SEMANTICS_BENCHMARK_VECTOR)
                {
                    x86_semantics_benchmark_after_scalar(data, after_scalar);
                }
                const X86SemanticsBenchmarkSample sample = x86_semantics_benchmark_run(data, (X86SemanticsBenchmarkPath)path, repeats, counters);
                const double items = (double)repeats * (double)data->count;
                nanoseconds[path][pass] = sample.nanoseconds / items;
                cycles[path][pass] = sample.cycles / items;
                instructions[path][pass] = sample.instructions / items;
                throttle[path][pass] = sample.throttle / items;
            }
        }
    }
    printf("%s: %llu %s, %llu repeats\n", name, (unsigned long long)data->count, data->effects ? "records" : "requests", (unsigned long long)repeats);
    for (uint32_t path = 0; path < X86_SEMANTICS_BENCHMARK_PATHS; path += 1)
    {
        if (timed & (1u << path))
        {
            const double time = x86_semantics_benchmark_median(nanoseconds[path]);
            const double cycle = x86_semantics_benchmark_median(cycles[path]);
            const double instruction = x86_semantics_benchmark_median(instructions[path]);
            const double slots = x86_semantics_benchmark_median(throttle[path]);
            printf("  %-15s %7.3f ns %7.2f cycles %7.2f instructions %5.2f IPC %6.3f throttle per %s\n", path_names[data->effects][path], time, cycle, instruction,
                   cycle != 0 ? instruction / cycle : 0, slots, data->effects ? "record" : "request");
        }
    }
}

// The tables' footprint, and initialization's median time and cycles.
static bool x86_semantics_benchmark_tables(X86SemanticsBenchmarkCounters* counters)
{
    double nanoseconds[BUSTER_X86_SEMANTICS_BENCHMARK_PASSES];
    double cycles[BUSTER_X86_SEMANTICS_BENCHMARK_PASSES];
    bool valid = true;
    for (uint32_t pass = 0; pass < BUSTER_X86_SEMANTICS_BENCHMARK_PASSES; pass += 1)
    {
        X86SemanticsBenchmarkSample sample = { 0 };
        x86_semantics_benchmark_start_counters(counters);
        const uint64_t begin = x86_semantics_benchmark_now();
        valid &= x86_semantics_initialize();
        const uint64_t end = x86_semantics_benchmark_now();
        x86_semantics_benchmark_stop_counters(counters, &sample);
        nanoseconds[pass] = (double)(end - begin);
        cycles[pass] = sample.cycles;
    }
    const uint64_t records = (uint64_t)x86_effects_count * sizeof(X86Effects);
    const uint64_t choice = sizeof(x86_choice_forms_by_candidate) + sizeof(x86_choice_conditions_by_candidate) + sizeof(x86_choice_row_info);
    printf("tables: %u covered forms, records %llu bytes (%u of 16), index %llu bytes (a u16 per form of %u), choice %llu bytes (%u rows of %u candidates) "
           "and %llu more for the scalar twin's rows\n",
           x86_effects_count - 1, (unsigned long long)records, x86_effects_count, (unsigned long long)sizeof(x86_effects_index), (uint32_t)X86_FORM_COUNT,
           (unsigned long long)choice, x86_choice_row_count, BUSTER_X86_CHOICE_CANDIDATES, (unsigned long long)(x86_choice_row_count * sizeof(X86ChoiceRow)));
    printf("tables: x86_semantics_initialize %.1f us, %.0f cycles (median of %u)\n", x86_semantics_benchmark_median(nanoseconds) / 1000.0,
           x86_semantics_benchmark_median(cycles), BUSTER_X86_SEMANTICS_BENCHMARK_PASSES);
    return valid;
}

// What a selector that doesn't classify values would emit for a request: the row's widest field up to an imm32 that takes
// the value (not the accumulator's form, nor a narrower one), or movabs past it.
static uint32_t x86_semantics_benchmark_longest(uint32_t row, int64_t value)
{
    const X86ChoiceRow* choice = &x86_choice_rows[row];
    uint32_t result = X86_FORM_NONE;
    uint32_t longest = 0;
    for (uint32_t candidate = 0; candidate < BUSTER_X86_CHOICE_CANDIDATES; candidate += 1)
    {
        const uint32_t form = choice->forms[candidate];
        const X86FormDescription* description = &x86_form_descriptions[form];
        const bool takes = form != X86_FORM_NONE && description->immediate_bytes != 0 && x86_immediate_encodes(form, value) &&
                           description->syntax != X86_SYNTAX_ACC_IMM && x86_effects(form)->width == choice->width;
        // An imm32 over a movabs; otherwise the widest.
        const uint32_t rank = description->immediate_bytes == 8 ? 1 : description->immediate_bytes + 1;
        if (takes && rank >= longest)
        {
            longest = rank;
            result = form;
        }
    }
    return result;
}

static bool x86_semantics_benchmark_choice(uint64_t seed, uint64_t count, uint32_t timed, uint64_t after_scalar, X86SemanticsBenchmarkCounters* counters)
{
    X86SemanticsBenchmarkData data = {
        .effects = false,
        .count = count,
        .rows = malloc(count * sizeof(uint16_t)),
        .values = malloc(count * sizeof(int64_t)),
        .contexts = malloc(count * sizeof(uint16_t)),
        .forms = malloc(count * sizeof(uint16_t)),
    };
    uint16_t* check = malloc(count * sizeof(uint16_t));
    bool valid = data.rows && data.values && data.contexts && data.forms && check;
    if (valid)
    {
        uint64_t state = seed;
        for (uint64_t i = 0; i < count; i += 1)
        {
            x86_semantics_benchmark_request(&state, &data.rows[i], &data.values[i], &data.contexts[i]);
        }
        x86_choose_forms_scalar(data.rows, data.values, data.contexts, count, check);
        x86_choose_forms(data.rows, data.values, data.contexts, count, data.forms);
        valid = memcmp(check, data.forms, count * sizeof(uint16_t)) == 0;
        if (!valid)
        {
            fprintf(stderr, "choice: the scalar and AVX-512 paths disagree\n");
        }
    }
    if (valid)
    {
        // The chosen records' bytes against the longest form's, each with a register (or memory) destination.
        uint64_t chosen_bytes = 0;
        uint64_t longest_bytes = 0;
        uint64_t without = 0;
        uint64_t compared = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            const uint32_t longest = x86_semantics_benchmark_longest(data.rows[i], data.values[i]);
            without += data.forms[i] == X86_FORM_NONE;
            if (data.forms[i] != X86_FORM_NONE && longest != X86_FORM_NONE)
            {
                X86Instruction target = { 0 };
                const bool in_register = (data.contexts[i] & BUSTER_X86_CONTEXT_REGISTER) != 0;
                target.rm = in_register ? (uint8_t)((data.contexts[i] & BUSTER_X86_CONTEXT_ACCUMULATOR) ? X86_REGISTER_RAX : X86_REGISTER_RBX) : X86_REGISTER_RBP;
                target.mode = in_register ? 0 : BUSTER_X86_MEMORY_BASE;
                target.displacement = in_register ? 0 : -16;
                target.reg = x86_choice_rows[data.rows[i]].operation == X86_OPERATION_IMUL ? (uint8_t)X86_REGISTER_RCX : 0;
                const X86Instruction chosen = x86_choice_instruction(data.forms[i], target, data.values[i]);
                const X86Instruction naive = x86_choice_instruction(longest, target, data.values[i]);
                uint8_t bytes[2 * BUSTER_X86_ENCODER_CAPACITY(1)];
                const int64_t chosen_length = x86_encode_scalar(&chosen, 1, bytes, sizeof(bytes), 0, 0);
                const int64_t naive_length = x86_encode_scalar(&naive, 1, bytes, sizeof(bytes), 0, 0);
                if (chosen_length > 0 && naive_length > 0)
                {
                    chosen_bytes += (uint64_t)chosen_length;
                    longest_bytes += (uint64_t)naive_length;
                    compared += 1;
                }
            }
        }
        printf("choice: %.1f%% of the requests need a register (no form); the chosen forms take %.2f bytes per instruction, the longest that takes the value "
               "%.2f (%llu compared)\n",
               100.0 * (double)without / (double)count, compared != 0 ? (double)chosen_bytes / (double)compared : 0,
               compared != 0 ? (double)longest_bytes / (double)compared : 0, (unsigned long long)compared);
        x86_semantics_benchmark_case("choice", &data, timed & 3, after_scalar, counters);
    }
    free(data.rows);
    free(data.values);
    free(data.contexts);
    free(data.forms);
    free(check);
    return valid;
}

static bool x86_semantics_benchmark_effects(uint64_t seed, uint64_t functions, bool optimized, uint32_t timed, uint64_t after_scalar, X86SemanticsBenchmarkCounters* counters)
{
    const uint64_t capacity = functions * BUSTER_X86_SEMANTICS_BENCHMARK_FUNCTION_RECORDS;
    X86SemanticsBenchmarkData data = {
        .effects = true,
        .records = malloc(capacity * sizeof(X86Instruction)),
        .reads = malloc(capacity * sizeof(uint64_t)),
        .writes = malloc(capacity * sizeof(uint64_t)),
        .code = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .code_capacity = BUSTER_X86_ENCODER_CAPACITY(capacity),
    };
    uint32_t* branches = malloc(capacity * sizeof(uint32_t));
    uint32_t* targets = malloc(capacity * sizeof(uint32_t));
    uint64_t* check_reads = malloc(capacity * sizeof(uint64_t));
    uint64_t* check_writes = malloc(capacity * sizeof(uint64_t));
    bool valid = data.records && data.reads && data.writes && data.code && branches && targets && check_reads && check_writes;
    if (valid)
    {
        // The shapes of clang's -O0 and -O2 code (branch_sizing_benchmark.c's), functions of 96 records.
        const BranchSizingShape shape = {
            .count = BUSTER_X86_SEMANTICS_BENCHMARK_FUNCTION_RECORDS,
            .branch_period = optimized ? 7 : 9,
            .near = optimized ? 128 : 64,
            .far_per_mille = optimized ? 200 : 150,
            .jmp_per_mille = optimized ? 290 : 450,
            .backward_per_mille = optimized ? 230 : 55,
            .compiler_like = true,
        };
        uint64_t state = seed;
        for (uint64_t function = 0; function < functions; function += 1)
        {
            branch_sizing_generate(&state, &shape, data.records + function * BUSTER_X86_SEMANTICS_BENCHMARK_FUNCTION_RECORDS, branches, targets);
        }
        data.count = capacity;
        uint64_t covered = 0;
        for (uint64_t i = 0; i < capacity; i += 1)
        {
            covered += x86_effects(data.records[i].form)->operation != X86_OPERATION_NONE;
        }
        x86_instructions_effects_scalar(data.records, capacity, check_reads, check_writes);
        x86_instructions_effects(data.records, capacity, data.reads, data.writes);
        valid = memcmp(check_reads, data.reads, capacity * sizeof(uint64_t)) == 0 && memcmp(check_writes, data.writes, capacity * sizeof(uint64_t)) == 0;
        if (!valid)
        {
            fprintf(stderr, "effects: the scalar and AVX-512 paths disagree\n");
        }
        if (valid)
        {
            printf("effects: %s, %.1f%% of the records covered (the rest EVEX forms and empty records)\n", optimized ? "-O2's shape" : "-O0's shape",
                   100.0 * (double)covered / (double)capacity);
            x86_semantics_benchmark_case(optimized ? "effects, -O2's shape" : "effects, -O0's shape", &data, timed & 7, after_scalar, counters);
        }
    }
    free(data.records);
    free(data.reads);
    free(data.writes);
    free(data.code);
    free(branches);
    free(targets);
    free(check_reads);
    free(check_writes);
    return valid;
}

// The code case's variants: a block of the same instruction over 8 registers (RBX, RSI, RDI and R8 to R12), each its own
// dependency chain.
typedef struct X86SemanticsBenchmarkVariant X86SemanticsBenchmarkVariant;
struct X86SemanticsBenchmarkVariant
{
    const char* name;
    uint16_t form;
    int64_t value;
    // Both operands the block's register (xor r, r; test r, r).
    bool same;
};

static const X86SemanticsBenchmarkVariant x86_semantics_benchmark_variants[] = {
    { "add r64, imm8 (chosen for 5)", X86_FORM_ADD_RM64_IMM8, 5, false },
    { "add r64, imm32 (the same 5)", X86_FORM_ADD_RM64_IMM32, 5, false },
    { "add r64, 1", X86_FORM_ADD_RM64_IMM8, 1, false },
    { "inc r64 (for add 1, CF dead)", X86_FORM_INC_RM64, 0, false },
    { "mov r32, 0", X86_FORM_MOV_R32_IMM32, 0, false },
    { "xor r32, r32 (for mov 0, flags dead)", X86_FORM_XOR_RM32_R32, 0, true },
    { "cmp r64, 0", X86_FORM_CMP_RM64_IMM8, 0, false },
    { "test r64, r64 (for cmp 0, AF dead)", X86_FORM_TEST_RM64_R64, 0, true },
    { "test r64, imm32 (100)", X86_FORM_TEST_RM64_IMM32, 100, false },
    { "test r8, imm8 (for it)", X86_FORM_TEST_RM8_IMM8, 100, false },
    { "movabs r64, imm64 (100)", X86_FORM_MOV_R64_IMM64, 100, false },
    { "mov r32, imm32 (for it)", X86_FORM_MOV_R32_IMM32, 100, false },
};

// Runs each variant's block in a loop (RCX the count: dec rcx; jnz back) from a page of its own, and prints the cycles and
// nanoseconds per instruction (from the median of the passes) and the bytes per instruction.
static bool x86_semantics_benchmark_code(X86SemanticsBenchmarkCounters* counters)
{
    const uint64_t page = 1u << 16;
    uint8_t* code = mmap(0, page, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = code != MAP_FAILED;
    static const uint8_t registers[8] = { X86_REGISTER_RBX, X86_REGISTER_RSI, X86_REGISTER_RDI, X86_REGISTER_R8,
                                          X86_REGISTER_R9,  X86_REGISTER_R10, X86_REGISTER_R11, X86_REGISTER_R12 };
    for (uint32_t v = 0; valid && v < BUSTER_ARRAY_LENGTH(x86_semantics_benchmark_variants); v += 1)
    {
        const X86SemanticsBenchmarkVariant* variant = &x86_semantics_benchmark_variants[v];
        X86Instruction records[BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK + 3];
        for (uint32_t i = 0; i < BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK; i += 1)
        {
            X86Instruction target = {
                .rm = registers[i % 8],
            };
            records[i] = x86_choice_instruction(variant->form, target, variant->value);
            records[i].reg = variant->same ? records[i].rm : records[i].reg;
        }
        // The block's length, then the loop: dec rcx; jnz to the start (rel32); ret.
        uint8_t block[BUSTER_X86_ENCODER_CAPACITY(BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK)];
        const int64_t block_length = x86_encode_scalar(records, BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK, block, sizeof(block), 0, 0);
        records[BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK] = (X86Instruction){
            .form = X86_FORM_DEC_RM64,
            .rm = X86_REGISTER_RCX,
        };
        records[BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK + 1] = (X86Instruction){
            .form = X86_FORM_JNE_REL32,
            .displacement = (int32_t)(-(block_length + 3 + 6)),
        };
        records[BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK + 2] = (X86Instruction){
            .form = X86_FORM_RET,
        };
        const int64_t length = block_length > 0 ? x86_encode_scalar(records, BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK + 3, code, page, 0, 0) : -1;
        valid = length > 0 && x86_validate(records, BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK + 3) == BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK + 3;
        double nanoseconds[BUSTER_X86_SEMANTICS_BENCHMARK_PASSES];
        double cycles[BUSTER_X86_SEMANTICS_BENCHMARK_PASSES];
        for (uint32_t pass = 0; valid && pass < BUSTER_X86_SEMANTICS_BENCHMARK_PASSES; pass += 1)
        {
            X86SemanticsBenchmarkSample sample = { 0 };
            x86_semantics_benchmark_start_counters(counters);
            const uint64_t begin = x86_semantics_benchmark_now();
            // The block may change RBX, RSI, RDI and R8 to R12 and the flags: the asm says so. RCX is the count. The call's
            // return address goes below the red zone, which the compiler may use here.
            uint64_t iterations = BUSTER_X86_SEMANTICS_BENCHMARK_ITERATIONS;
            __asm__ volatile("sub $128, %%rsp\n\tcall *%1\n\tadd $128, %%rsp"
                             : "+c"(iterations)
                             : "r"(code)
                             : "rbx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "memory", "cc");
            const uint64_t end = x86_semantics_benchmark_now();
            x86_semantics_benchmark_stop_counters(counters, &sample);
            const double instructions = (double)BUSTER_X86_SEMANTICS_BENCHMARK_ITERATIONS * BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK;
            nanoseconds[pass] = (double)(end - begin) / instructions;
            cycles[pass] = sample.cycles / instructions;
        }
        if (valid)
        {
            printf("  %-38s %6.3f ns %6.3f cycles %5.2f bytes per instruction\n", variant->name, x86_semantics_benchmark_median(nanoseconds),
                   x86_semantics_benchmark_median(cycles), (double)block_length / BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK);
        }
    }
    if (code != MAP_FAILED)
    {
        munmap(code, page);
    }
    return valid;
}

// A number in `base` (0 takes C's prefixes), into `value` only if the whole text is one.
static bool x86_semantics_benchmark_number(const char* text, int base, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, base);
    const bool valid = text[0] >= '0' && text[0] <= '9' && end != text && *end == '\0' && errno == 0;
    if (valid)
    {
        *value = (uint64_t)parsed;
    }
    return valid;
}

int run_x86_semantics_benchmark(int argc, char** argv)
{
    bool valid = true;
    uint64_t seed = 1;
    uint64_t requests = BUSTER_X86_SEMANTICS_BENCHMARK_REQUESTS;
    uint64_t functions = BUSTER_X86_SEMANTICS_BENCHMARK_FUNCTIONS;
    uint64_t after_scalar = 0;
    // A bit per path of each case (scalar, AVX-512, encode); --only keeps one, of one case.
    uint32_t choice_paths = 3;
    uint32_t effects_paths = 7;
    bool code = true;
    for (int argument = 1; argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            valid = valid && x86_semantics_benchmark_number(argv[argument + 1], 0, &seed);
            argument += 1;
        }
        else if (strcmp(argv[argument], "--requests") == 0 && argument + 1 < argc)
        {
            valid = valid && x86_semantics_benchmark_number(argv[argument + 1], 10, &requests) && requests != 0 && requests <= (1u << 26);
            argument += 1;
        }
        else if (strcmp(argv[argument], "--functions") == 0 && argument + 1 < argc)
        {
            valid = valid && x86_semantics_benchmark_number(argv[argument + 1], 10, &functions) && functions != 0 && functions <= (1u << 20);
            argument += 1;
        }
        else if (strcmp(argv[argument], "--after-scalar") == 0 && argument + 1 < argc)
        {
            valid = valid && x86_semantics_benchmark_number(argv[argument + 1], 10, &after_scalar) && after_scalar <= BUSTER_X86_SEMANTICS_BENCHMARK_AFTER_SCALAR_MAX_MS;
            argument += 1;
        }
        else if (strcmp(argv[argument], "--only") == 0 && argument + 1 < argc)
        {
            const char* name = argv[argument + 1];
            choice_paths = strcmp(name, "choose-scalar") == 0 ? 1 : strcmp(name, "choose-avx512") == 0 ? 2 : 0;
            effects_paths = strcmp(name, "effects-scalar") == 0 ? 1 : strcmp(name, "effects-avx512") == 0 ? 2 : strcmp(name, "encode") == 0 ? 4 : 0;
            code = strcmp(name, "code") == 0;
            valid = valid && (choice_paths != 0 || effects_paths != 0 || code);
            argument += 1;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--requests N] [--functions N] [--seed N] [--after-scalar MS] [--only choose-scalar|choose-avx512|effects-scalar|effects-avx512|encode|code]\n",
                argv[0]);
    }
    else
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize();
        X86SemanticsBenchmarkCounters counters = x86_semantics_benchmark_open_counters();
        printf("x86 semantics: seed %llu, %u interleaved-pass medians", (unsigned long long)seed, BUSTER_X86_SEMANTICS_BENCHMARK_PASSES);
        if (after_scalar != 0)
        {
            printf("; each AVX-512 sample after %llu ms of its scalar twin", (unsigned long long)after_scalar);
        }
        printf("\nthrottle: PMCx1A0 umask 0x08, read as the dispatch slots Zen 5's power cap takes (#199)\n");
        if (!counters.available)
        {
            printf("hardware counters unavailable: cycles, instructions, IPC and throttle are zero\n");
        }
        valid = valid && x86_semantics_benchmark_tables(&counters);
        if (valid && choice_paths != 0)
        {
            valid = x86_semantics_benchmark_choice(seed, requests, choice_paths, after_scalar, &counters);
        }
        if (valid && effects_paths != 0)
        {
            valid = x86_semantics_benchmark_effects(seed, functions, false, effects_paths, after_scalar, &counters) &&
                    x86_semantics_benchmark_effects(seed + 1, functions, true, effects_paths, after_scalar, &counters);
        }
        if (valid && code)
        {
            printf("code: blocks of %u instructions over 8 registers, %u iterations, per instruction\n", BUSTER_X86_SEMANTICS_BENCHMARK_BLOCK,
                   BUSTER_X86_SEMANTICS_BENCHMARK_ITERATIONS);
            valid = x86_semantics_benchmark_code(&counters);
        }
        x86_semantics_benchmark_close_counters(&counters);
        if (!valid)
        {
            fprintf(stderr, "benchmark-x86-semantics failed (a table, a disagreement or the code's mapping)\n");
        }
    }
    return valid ? 0 : 1;
}

#else
int run_x86_semantics_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
