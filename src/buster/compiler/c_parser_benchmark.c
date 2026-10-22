// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// Counter/timing/sort helpers adapted through c_lexer_benchmark.c (by way of #186's parse spike) retain MIT and the
// upstream notice: Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// benchmark-parser: the parser (c_parser.c) on translation units preprocessed beforehand, the scalar parser, the
// kernels' scalar twins and the vector path, as a whole and kernel by kernel.
//
// Usage: build/compiler benchmark-parser [--passes N] [--only NAME|stages] [--repeat N] [--counters pipeline|branches|memory|stalls]
//                                        [-E's options] file...
//
// Each file is preprocessed with -E's options and its tokens are turned into the parser's (ParseTokens), outside what
// is timed, as a compile finds them; the predefines for -march=native come from --predefines, as for -E. Then the
// paths are checked to agree (the vector path's outcome and tree against the scalar parser's), and timed: --passes
// interleaved passes (21 by default) after a warm-up one, of the whole paths over every file (the input's conversion
// from the preprocessor's output and its twin; the scalar parser; the twins; the vector path), then of each stage,
// timed per file on the shared arrays in parse_benchmark_stage_order, each after the stages before it. It reports
// medians per token, of time, cycles (with the fastest and slowest pass) and instructions, and the events of the
// --counters set; and the share of top-level declarations and tokens the vector path's fallback took. --only times
// one path or stage ("stages" for every stage); a stage named alone runs --repeat times per file and the stages after
// it don't run, so that perf sees mostly that stage. Run it pinned to one CPU, on the 9700X under its lock
// (`EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler benchmark-parser ...`). The
// exit status is 1 if the arguments are invalid, a file doesn't preprocess, or the paths disagree.
//
// Layout: the counters and the report; the files; the paths and the stages; run_parser_benchmark, called by main.c.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_predefines.c"
#include "buster/compiler/c_preprocessor.c"
#include "buster/compiler/c_parser.c"

#define BUSTER_PARSE_BENCHMARK_PASSES 21
#define BUSTER_PARSE_BENCHMARK_MAX_PASSES 101

// ---------------------------------------------------------------------------------------------------------------------
// The counters and the report

typedef enum ParseBenchmarkCounter
{
    PARSE_BENCHMARK_CYCLES,
    PARSE_BENCHMARK_INSTRUCTIONS,
    PARSE_BENCHMARK_FIRST,
    PARSE_BENCHMARK_SECOND,
    PARSE_BENCHMARK_THIRD,
    PARSE_BENCHMARK_COUNTERS,
} ParseBenchmarkCounter;

typedef enum ParseBenchmarkSet
{
    PARSE_BENCHMARK_PIPELINE,
    PARSE_BENCHMARK_BRANCHES,
    PARSE_BENCHMARK_MEMORY,
    PARSE_BENCHMARK_STALLS,
    PARSE_BENCHMARK_SETS,
} ParseBenchmarkSet;

static const char* const parse_benchmark_set_names[PARSE_BENCHMARK_SETS] =
{
    "pipeline",
    "branches",
    "memory",
    "stalls",
};

#define BUSTER_PARSE_BENCHMARK_DISPATCH_SLOTS 8

// AMD raw events: the event's low byte in bits 0-7, its high nibble in bits 32-35, the unit mask in bits 8-15.
// ex_ret_ops (0xc1), de_no_dispatch_per_slot.no_ops_from_frontend (0x1a0, 0x01) and .backend_stalls (0x1a0, 0x1e);
// ex_ret_brn (0xc2), ex_ret_brn_misp (0xc3) and ex_ret_brn_tkn (0xc4); ls_dmnd_fills_from_sys.local_l2 (0x43, 0x01),
// .local_ccx (0x43, 0x02) and .dram_io_near (0x43, 0x08); ls_bad_status2.stli_other (0x24, 0x02), the dispatch slots
// the per-core power cap throttles (0x1a0, 0x08: one of backend_stalls' bits) and
// de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall (0xae, 0x40).
static const uint64_t parse_benchmark_configs[PARSE_BENCHMARK_SETS][3] =
{
    { 0xc1, 0x1000001a0, 0x100001ea0 },
    { 0xc2, 0xc3, 0xc4 },
    { 0x143, 0x243, 0x843 },
    { 0x224, 0x1000008a0, 0x40ae },
};

static const char* const parse_benchmark_counter_names[PARSE_BENCHMARK_SETS][PARSE_BENCHMARK_COUNTERS] =
{
    { "cycles", "instructions", "ex_ret_ops", "de_no_dispatch_per_slot.no_ops_from_frontend", "de_no_dispatch_per_slot.backend_stalls" },
    { "cycles", "instructions", "ex_ret_brn", "ex_ret_brn_misp", "ex_ret_brn_tkn" },
    { "cycles", "instructions", "ls_dmnd_fills_from_sys.local_l2", "ls_dmnd_fills_from_sys.local_ccx", "ls_dmnd_fills_from_sys.dram_io_near" },
    { "cycles", "instructions", "ls_bad_status2.stli_other", "de_no_dispatch_per_slot.0x08", "de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall" },
};

typedef struct ParseBenchmarkCounters ParseBenchmarkCounters;
struct ParseBenchmarkCounters
{
    ParseBenchmarkSet set;
    // -1 where perf_event_open isn't allowed.
    int descriptors[PARSE_BENCHMARK_COUNTERS];
    // Each counter's page, read with rdpmc, or 0 where it can't be mapped (read() then, which costs a system call).
    volatile struct perf_event_mmap_page* pages[PARSE_BENCHMARK_COUNTERS];
};

typedef struct ParseBenchmarkSample ParseBenchmarkSample;
struct ParseBenchmarkSample
{
    uint64_t nanoseconds;
    uint64_t counts[PARSE_BENCHMARK_COUNTERS];
};

static ParseBenchmarkCounters parse_benchmark_open_counters(ParseBenchmarkSet set)
{
    ParseBenchmarkCounters result;
    result.set = set;
    for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        struct perf_event_attr attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.size = sizeof(attributes);
        attributes.type = c < PARSE_BENCHMARK_FIRST ? PERF_TYPE_HARDWARE : PERF_TYPE_RAW;
        attributes.config = c == PARSE_BENCHMARK_CYCLES ? PERF_COUNT_HW_CPU_CYCLES : c == PARSE_BENCHMARK_INSTRUCTIONS ? PERF_COUNT_HW_INSTRUCTIONS :
                            parse_benchmark_configs[set][c - PARSE_BENCHMARK_FIRST];
        attributes.exclude_kernel = 1;
        attributes.exclude_hv = 1;
        attributes.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        result.descriptors[c] = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
        void* page = result.descriptors[c] >= 0 ? mmap(0, (size_t)sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, result.descriptors[c], 0) : MAP_FAILED;
        result.pages[c] = page != MAP_FAILED ? (volatile struct perf_event_mmap_page*)page : 0;
    }
    return result;
}

// Prints the counters that couldn't count all the time (shared with others by multiplexing), then closes them.
static void parse_benchmark_close_counters(ParseBenchmarkCounters* counters)
{
    for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        uint64_t values[3] = { 0, 0, 0 };
        if (counters->descriptors[c] >= 0 && read(counters->descriptors[c], values, sizeof(values)) == sizeof(values) && values[2] < values[1])
        {
            fprintf(stderr, "%s counted %.1f%% of the time: its numbers are partial\n", parse_benchmark_counter_names[counters->set][c],
                    values[1] ? 100.0 * (double)values[2] / (double)values[1] : 0.0);
        }
        if (counters->pages[c] != 0)
        {
            munmap((void*)counters->pages[c], (size_t)sysconf(_SC_PAGESIZE));
        }
        if (counters->descriptors[c] >= 0)
        {
            close(counters->descriptors[c]);
        }
    }
}

// The counter's value: with rdpmc from its page when the kernel allows it, under the page's sequence lock, as
// perf_event.h describes; otherwise with read().
static uint64_t parse_benchmark_read_counter(const ParseBenchmarkCounters* counters, uint32_t c)
{
    uint64_t value = 0;
    volatile struct perf_event_mmap_page* page = counters->pages[c];
    if (page != 0 && page->cap_user_rdpmc)
    {
        bool stable = false;
        while (!stable)
        {
            const uint32_t sequence = page->lock;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            const uint32_t index = page->index;
            int64_t count = page->offset;
            if (index != 0)
            {
                const uint32_t shift = 64 - page->pmc_width;
                count += (int64_t)(__rdpmc((int)index - 1) << shift) >> shift;
            }
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            stable = page->lock == sequence;
            value = (uint64_t)count;
        }
    }
    else if (counters->descriptors[c] >= 0)
    {
        uint64_t values[3] = { 0, 0, 0 };
        value = read(counters->descriptors[c], values, sizeof(values)) == sizeof(values) ? values[0] : 0;
    }
    return value;
}

static ParseBenchmarkSample parse_benchmark_sample(const ParseBenchmarkCounters* counters)
{
    ParseBenchmarkSample result;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    result.nanoseconds = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        result.counts[c] = parse_benchmark_read_counter(counters, c);
    }
    return result;
}

// after - before, less `overhead`, added to `total`. The values wrap: a total is read as signed at the end.
static void parse_benchmark_accumulate(ParseBenchmarkSample* total, ParseBenchmarkSample before, ParseBenchmarkSample after, const ParseBenchmarkSample* overhead)
{
    total->nanoseconds += after.nanoseconds - before.nanoseconds - overhead->nanoseconds;
    for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        total->counts[c] += after.counts[c] - before.counts[c] - overhead->counts[c];
    }
}

static void parse_benchmark_sort(uint64_t* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

// The interval between two samples taken back to back, the median of many: what a sample adds to each interval that
// one is taken around, which the stages, timed per file, subtract.
static ParseBenchmarkSample parse_benchmark_overhead(const ParseBenchmarkCounters* counters)
{
    enum { PARSE_BENCHMARK_OVERHEAD_SAMPLES = 1001 };
    static uint64_t differences[PARSE_BENCHMARK_COUNTERS + 1][PARSE_BENCHMARK_OVERHEAD_SAMPLES];
    ParseBenchmarkSample previous = parse_benchmark_sample(counters);
    for (uint32_t i = 0; i < PARSE_BENCHMARK_OVERHEAD_SAMPLES; i += 1)
    {
        const ParseBenchmarkSample next = parse_benchmark_sample(counters);
        differences[PARSE_BENCHMARK_COUNTERS][i] = next.nanoseconds - previous.nanoseconds;
        for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
        {
            differences[c][i] = next.counts[c] - previous.counts[c];
        }
        previous = next;
    }
    ParseBenchmarkSample result;
    for (uint32_t c = 0; c <= PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        parse_benchmark_sort(differences[c], PARSE_BENCHMARK_OVERHEAD_SAMPLES);
    }
    result.nanoseconds = differences[PARSE_BENCHMARK_COUNTERS][PARSE_BENCHMARK_OVERHEAD_SAMPLES / 2];
    for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        result.counts[c] = differences[c][PARSE_BENCHMARK_OVERHEAD_SAMPLES / 2];
    }
    return result;
}

// A total's value, as signed: what was subtracted for the samples can make a tiny stage's total negative.
static uint64_t parse_benchmark_clamp(uint64_t value)
{
    return (int64_t)value < 0 ? 0 : value;
}

// Prints the medians of `samples` per token: time, cycles (with the fastest and slowest pass), instructions, and the
// set's events: the dispatch slots' split (ops that retired, slots the front end left empty, back-end stalls, and the
// rest), branches per token (mispredicted, and taken), fills per token (from the L2, the L3 and DRAM), or
// store-to-load conflicts, the throttled slots and the FP queue's stalls.
static void parse_benchmark_report(const char* name, const ParseBenchmarkSample* samples, uint32_t count, uint64_t tokens, ParseBenchmarkSet set)
{
    uint64_t nanoseconds[BUSTER_PARSE_BENCHMARK_MAX_PASSES];
    uint64_t counts[PARSE_BENCHMARK_COUNTERS][BUSTER_PARSE_BENCHMARK_MAX_PASSES];
    for (uint32_t i = 0; i < count; i += 1)
    {
        nanoseconds[i] = parse_benchmark_clamp(samples[i].nanoseconds);
        for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
        {
            counts[c][i] = parse_benchmark_clamp(samples[i].counts[c]);
        }
    }
    parse_benchmark_sort(nanoseconds, count);
    for (uint32_t c = 0; c < PARSE_BENCHMARK_COUNTERS; c += 1)
    {
        parse_benchmark_sort(counts[c], count);
    }
    const double t = (double)tokens;
    const double cycles = (double)counts[PARSE_BENCHMARK_CYCLES][count / 2];
    const double first = (double)counts[PARSE_BENCHMARK_FIRST][count / 2];
    const double second = (double)counts[PARSE_BENCHMARK_SECOND][count / 2];
    const double third = (double)counts[PARSE_BENCHMARK_THIRD][count / 2];
    printf("  %-22s %9.3f ms %7.3f ns/token %7.3f cycles/token [%.3f-%.3f] %8.3f instructions/token IPC %.2f  ", name, (double)nanoseconds[count / 2] / 1e6,
           (double)nanoseconds[count / 2] / t, cycles / t, (double)counts[PARSE_BENCHMARK_CYCLES][0] / t, (double)counts[PARSE_BENCHMARK_CYCLES][count - 1] / t,
           (double)counts[PARSE_BENCHMARK_INSTRUCTIONS][count / 2] / t, cycles != 0 ? (double)counts[PARSE_BENCHMARK_INSTRUCTIONS][count / 2] / cycles : 0.0);
    if (set == PARSE_BENCHMARK_PIPELINE)
    {
        const double slots = cycles * BUSTER_PARSE_BENCHMARK_DISPATCH_SLOTS;
        const double retiring = slots != 0 ? first / slots : 0.0;
        const double frontend = slots != 0 ? second / slots : 0.0;
        const double backend = slots != 0 ? third / slots : 0.0;
        printf("slots: retiring %4.1f%% frontend %4.1f%% backend %4.1f%% other %4.1f%%\n", 100.0 * retiring, 100.0 * frontend, 100.0 * backend,
               100.0 * (1.0 - retiring - frontend - backend));
    }
    else if (set == PARSE_BENCHMARK_BRANCHES)
    {
        printf("branches/token %.3f mispredicted %.4f (%.2f%%) taken %.3f\n", first / t, second / t, first != 0 ? 100.0 * second / first : 0.0, third / t);
    }
    else if (set == PARSE_BENCHMARK_MEMORY)
    {
        printf("fills/token from L2 %.4f L3 %.4f DRAM %.4f\n", first / t, second / t, third / t);
    }
    else
    {
        const double slots = cycles * BUSTER_PARSE_BENCHMARK_DISPATCH_SLOTS;
        printf("store-to-load conflicts/token %.4f throttled slots %.1f%% FP queue stalls %.1f%%\n", first / t, slots != 0 ? 100.0 * second / slots : 0.0,
               cycles != 0 ? 100.0 * third / cycles : 0.0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The files

// A file's unit, preprocessed and read by a Parser once (its tokens, meanings and predeclared names), with what the
// vector path did with it.
typedef struct ParseBenchmarkFile ParseBenchmarkFile;
struct ParseBenchmarkFile
{
    const char* path;
    // The preprocessor's output, which the input's conversion reads, and the parser with the file's tokens.
    PPTokens output;
    Parser parser;
    ParseFallback fallback;
};

// ---------------------------------------------------------------------------------------------------------------------
// The paths and the stages

// What is timed: the whole paths over every file; then the stages, each kernel's twin's and AVX-512's, in
// parse_benchmark_stage_order, timed per file on the shared arrays.
typedef enum ParseBenchmarkTimed
{
    PARSE_TIMED_INPUT_TWIN,
    PARSE_TIMED_INPUT,
    PARSE_TIMED_SCALAR_PARSER,
    PARSE_TIMED_TWINS,
    PARSE_TIMED_VECTOR,
    PARSE_TIMED_TWIN_K1,
    PARSE_TIMED_TWIN_K3_PASS,
    PARSE_TIMED_TWIN_K3,
    PARSE_TIMED_TWIN_K2,
    PARSE_TIMED_TWIN_SECONDARY,
    PARSE_TIMED_TWIN_K4,
    PARSE_TIMED_TWIN_K5,
    PARSE_TIMED_TWIN_K6,
    PARSE_TIMED_TWIN_FALLBACK,
    PARSE_TIMED_VECTOR_K1,
    PARSE_TIMED_VECTOR_K3_PASS,
    PARSE_TIMED_VECTOR_K3,
    PARSE_TIMED_VECTOR_K2,
    PARSE_TIMED_VECTOR_SECONDARY,
    PARSE_TIMED_VECTOR_K4_PREPARE,
    PARSE_TIMED_VECTOR_K4_LEFT,
    PARSE_TIMED_VECTOR_K4_RIGHT,
    PARSE_TIMED_VECTOR_K4,
    PARSE_TIMED_VECTOR_K5,
    PARSE_TIMED_VECTOR_K6,
    PARSE_TIMED_VECTOR_FALLBACK,
    PARSE_TIMED_POSTORDER_SORTED,
    PARSE_TIMED_POSTORDER,
    PARSE_TIMED_COUNT,
} ParseBenchmarkTimed;

static const char* const parse_benchmark_timed_names[PARSE_TIMED_COUNT] =
{
    "input-twin",
    "input",
    "scalar-parser",
    "twins",
    "vector",
    "twin-k1",
    "twin-k3-pass",
    "twin-k3",
    "twin-k2",
    "twin-k1-secondary",
    "twin-k4",
    "twin-k5",
    "twin-k6",
    "twin-fallback",
    "vector-k1",
    "vector-k3-pass",
    "vector-k3",
    "vector-k2",
    "vector-k1-secondary",
    "vector-k4-prepare",
    "vector-k4-left",
    "vector-k4-right",
    "vector-k4",
    "vector-k5",
    "vector-k6",
    "vector-fallback",
    "postorder-sorted",
    "postorder",
};

// The stages run in this order, so that each finds its inputs; K4's loops come alone before the whole K4.
static const ParseBenchmarkTimed parse_benchmark_stage_order[] =
{
    PARSE_TIMED_TWIN_K1,
    PARSE_TIMED_TWIN_K3_PASS,
    PARSE_TIMED_TWIN_K3,
    PARSE_TIMED_TWIN_K2,
    PARSE_TIMED_TWIN_SECONDARY,
    PARSE_TIMED_TWIN_K4,
    PARSE_TIMED_TWIN_K5,
    PARSE_TIMED_TWIN_K6,
    PARSE_TIMED_TWIN_FALLBACK,
    PARSE_TIMED_VECTOR_K1,
    PARSE_TIMED_VECTOR_K3_PASS,
    PARSE_TIMED_VECTOR_K3,
    PARSE_TIMED_VECTOR_K2,
    PARSE_TIMED_VECTOR_SECONDARY,
    PARSE_TIMED_VECTOR_K4_PREPARE,
    PARSE_TIMED_VECTOR_K4_LEFT,
    PARSE_TIMED_VECTOR_K4_RIGHT,
    PARSE_TIMED_VECTOR_K4,
    PARSE_TIMED_VECTOR_K5,
    PARSE_TIMED_VECTOR_K6,
    PARSE_TIMED_VECTOR_FALLBACK,
    PARSE_TIMED_POSTORDER_SORTED,
    PARSE_TIMED_POSTORDER,
};

typedef struct ParseBenchmark ParseBenchmark;
struct ParseBenchmark
{
    ParseBenchmarkFile* files;
    uint32_t file_count;
    uint64_t token_count;
    // The shared arrays, for the largest file: the kernels', the vector path's, the scalar parser's tree, and the twins'
    // typed states and statements; and the tokens the input's conversion writes.
    ParseKernels kernels;
    ParseVectorBuffers buffers;
    ParseTree tree;
    uint8_t* typed;
    ParseStatement* statements;
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_capacity;
    ParseTokens converted;
    Arena scratch;
    // K5's result for the file the stages are on, which the fallback reads; whether the file is past K4's 16-bit keys.
    bool statements_ok;
    bool deep;
};

static ParseInput parse_benchmark_input(ParseBenchmark* bench, ParseBenchmarkFile* file)
{
    return (ParseInput){
        .kinds = file->parser.tokens.kinds,
        .names = file->parser.tokens.names,
        .flags = file->parser.tokens.flags,
        .count = file->parser.tokens.count,
        .language = file->parser.language,
        .meanings = &file->parser.meanings,
        .scratch = &bench->scratch,
        .diagnostics = bench->diagnostics,
        .diagnostic_capacity = bench->diagnostic_capacity,
    };
}

// Runs a whole path over every file.
static void parse_benchmark_run_path(ParseBenchmark* bench, ParseBenchmarkTimed which)
{
    for (uint32_t f = 0; f < bench->file_count; f += 1)
    {
        ParseBenchmarkFile* file = &bench->files[f];
        const ParseInput input = parse_benchmark_input(bench, file);
        uint32_t diagnostic_count = 0;
        switch (which)
        {
            case PARSE_TIMED_INPUT_TWIN: parse_tokens_from_output_scalar(&file->output, &bench->converted); break;
            case PARSE_TIMED_INPUT: parse_tokens_from_output(&file->output, &bench->converted); break;
            case PARSE_TIMED_SCALAR_PARSER:
                parse_scalar(input.kinds, input.names, input.flags, input.count, input.language, input.meanings, &bench->tree, &bench->scratch, bench->diagnostics,
                             bench->diagnostic_capacity, &diagnostic_count);
                break;
            case PARSE_TIMED_TWINS: parse_twins(&bench->kernels, &input, file->parser.predeclared, bench->typed, bench->statements); break;
            case PARSE_TIMED_VECTOR: file->fallback = parse_vector(&bench->kernels, &input, file->parser.predeclared, &bench->buffers).fallback; break;
            default: break;
        }
    }
}

// Runs one stage on file `f`, whose earlier stages in parse_benchmark_stage_order have run.
static void parse_benchmark_run_stage(ParseBenchmark* bench, ParseBenchmarkTimed which, uint32_t f)
{
    ParseBenchmarkFile* file = &bench->files[f];
    const ParseInput input = parse_benchmark_input(bench, file);
    const uint32_t count = input.count;
    const uint8_t* kinds = input.kinds;
    const uint32_t* names = input.names;
    ParseVectorBuffers* buffers = &bench->buffers;
    ParseKernels* k = &bench->kernels;
    switch (which)
    {
        case PARSE_TIMED_TWIN_K1: k->unbalanced = !parse_brackets_scalar(kinds, parse_kind_deltas, count, k->depths, k->enclosing, k->stack); break;
        case PARSE_TIMED_VECTOR_K1:
            k->unbalanced = !parse_brackets_avx512(kinds, parse_kind_deltas, count, k->depths, k->enclosing, buffers->open_stack, k->bracket_blocks);
            break;
        case PARSE_TIMED_TWIN_K3_PASS:
        case PARSE_TIMED_VECTOR_K3_PASS:
            parse_typedefs_clear(k);
            k->unsupported = false;
            k->typedef_count = 0;
            parse_kernels_predeclared(k, file->parser.predeclared, true);
            if (!k->unbalanced)
            {
                parse_typedefs(k, kinds, names, count);
            }
            break;
        case PARSE_TIMED_TWIN_K3: parse_classify_scalar(k, kinds, names, count); break;
        case PARSE_TIMED_VECTOR_K3: parse_classify_mask_avx512(k, kinds, names, count); break;
        case PARSE_TIMED_TWIN_K2: parse_roles_scalar(k, count, bench->typed); break;
        case PARSE_TIMED_VECTOR_K2: parse_roles_avx512(k, count, buffers->typed, &buffers->groups, buffers->contexts, buffers->rounds); break;
        case PARSE_TIMED_TWIN_SECONDARY:
            parse_secondary_scalar(k, count, k->stack);
            parse_after_colons(k, count);
            break;
        case PARSE_TIMED_VECTOR_SECONDARY:
            parse_secondary_avx512(k, count, buffers->secondary);
            parse_after_colons_avx512(k, count);
            break;
        case PARSE_TIMED_TWIN_K4: parse_parents_scalar(k, count); break;
        // K4's three loops alone, in turn; a file too deep for them gets the twin in the first.
        case PARSE_TIMED_VECTOR_K4_PREPARE:
            bench->deep = parse_deepest(k, count) > BUSTER_PARSE_MAX_VECTOR_DEPTH;
            if (bench->deep)
            {
                parse_parents_scalar(k, count);
            }
            else
            {
                parse_top_prepare8_avx512(k->roles, k->node_depths, count, buffers->works);
            }
            break;
        case PARSE_TIMED_VECTOR_K4_LEFT:
            if (!bench->deep)
            {
                parse_top_left8_avx512(k->roles, k->node_depths, count, buffers->lefts, buffers->left_keys, buffers->works, &buffers->slots, &buffers->wide_blocks);
            }
            break;
        case PARSE_TIMED_VECTOR_K4_RIGHT:
            if (!bench->deep)
            {
                parse_top_right8_avx512(k->roles, k->node_depths, count, buffers->lefts, buffers->left_keys, buffers->works, k->parents, &buffers->slots);
            }
            break;
        case PARSE_TIMED_VECTOR_K4: parse_parents_vector(k, count, buffers); break;
        case PARSE_TIMED_TWIN_K5: bench->statements_ok = parse_statements(k, count, bench->statements); break;
        case PARSE_TIMED_VECTOR_K5: bench->statements_ok = parse_statements_vector(k, count, buffers); break;
        case PARSE_TIMED_TWIN_K6:
        {
            const ParseCheckFrames frames = parse_check_frames(k);
            parse_check_scalar(k, names, input.flags, count, bench->typed, &frames, k->check_classes, 0, 0);
            break;
        }
        case PARSE_TIMED_VECTOR_K6: parse_check_avx512(k, names, input.flags, count, buffers->typed, &buffers->check_twins); break;
        case PARSE_TIMED_TWIN_FALLBACK:
        case PARSE_TIMED_VECTOR_FALLBACK:
        {
            const bool everything = k->unbalanced || k->unsupported || !bench->statements_ok;
            parse_fallback(k, &input, everything);
            break;
        }
        case PARSE_TIMED_POSTORDER_SORTED:
            parse_postorder_sorted(k->parents, count, buffers->last_children, buffers->subtree_ends, buffers->cursors, buffers->postorder, buffers->first_children,
                                   buffers->subtree_starts, buffers->sizes);
            break;
        case PARSE_TIMED_POSTORDER: parse_postorder(k->parents, count, buffers->last_children, buffers->postorder); break;
        default: break;
    }
}

// Reads a file's tokens, so that its first stage finds them in cache, as after the preprocessor.
static uint32_t parse_benchmark_touch(const ParseBenchmarkFile* file)
{
    __m512i sum = _mm512_setzero_si512();
    const ParseTokens* tokens = &file->parser.tokens;
    for (uint64_t i = 0; i < tokens->count; i += 64)
    {
        const uint64_t live = tokens->count - i < 64 ? tokens->count - i : 64;
        const __mmask64 mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        sum = _mm512_add_epi32(sum, _mm512_maskz_loadu_epi8(mask, tokens->kinds + i));
        for (uint32_t q = 0; q < 4; q += 1)
        {
            sum = _mm512_add_epi32(sum, _mm512_maskz_loadu_epi32((__mmask16)(mask >> (16 * q)), tokens->names + i + 16 * q));
        }
    }
    return (uint32_t)_mm512_reduce_add_epi32(sum);
}

// Times the whole paths, then the stages (only `only`, if it names one; every stage for PARSE_TIMED_COUNT), `passes`
// interleaved passes after a warm-up one, and prints the medians. A stage named alone runs `repeat` times per file
// after the stages before it, and the ones after it don't run.
static void parse_benchmark_time(ParseBenchmark* bench, uint32_t passes, int32_t only, uint32_t repeat, ParseBenchmarkSet set)
{
    ParseBenchmarkCounters counters = parse_benchmark_open_counters(set);
    if (counters.descriptors[PARSE_BENCHMARK_CYCLES] < 0 || counters.descriptors[PARSE_BENCHMARK_INSTRUCTIONS] < 0)
    {
        fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): the counts read as 0\n");
    }
    const ParseBenchmarkSample overhead = parse_benchmark_overhead(&counters);
    const ParseBenchmarkSample none = { 0 };
    const bool stages = only < 0 || only >= PARSE_TIMED_TWIN_K1;
    static ParseBenchmarkSample samples[PARSE_TIMED_COUNT][BUSTER_PARSE_BENCHMARK_MAX_PASSES];
    volatile uint32_t sink = 0;
    for (uint32_t pass = 0; pass <= passes; pass += 1)
    {
        for (uint32_t which = PARSE_TIMED_INPUT_TWIN; which <= PARSE_TIMED_VECTOR; which += 1)
        {
            if (only < 0 || only == (int32_t)which)
            {
                ParseBenchmarkSample total = { 0 };
                const ParseBenchmarkSample before = parse_benchmark_sample(&counters);
                parse_benchmark_run_path(bench, (ParseBenchmarkTimed)which);
                parse_benchmark_accumulate(&total, before, parse_benchmark_sample(&counters), &none);
                samples[which][pass == 0 ? 0 : pass - 1] = total;
            }
        }
        if (stages)
        {
            ParseBenchmarkSample totals[PARSE_TIMED_COUNT];
            memset(totals, 0, sizeof(totals));
            for (uint32_t f = 0; f < bench->file_count; f += 1)
            {
                sink += parse_benchmark_touch(&bench->files[f]);
                ParseBenchmarkSample before = parse_benchmark_sample(&counters);
                bool done = false;
                for (uint32_t i = 0; !done && i < sizeof(parse_benchmark_stage_order) / sizeof(parse_benchmark_stage_order[0]); i += 1)
                {
                    const ParseBenchmarkTimed which = parse_benchmark_stage_order[i];
                    const bool alone = only == (int32_t)which;
                    for (uint32_t r = 0; r < (alone ? repeat : 1); r += 1)
                    {
                        parse_benchmark_run_stage(bench, which, f);
                        const ParseBenchmarkSample after = parse_benchmark_sample(&counters);
                        parse_benchmark_accumulate(&totals[which], before, after, &overhead);
                        before = after;
                    }
                    done = alone;
                }
                parse_typedefs_clear(&bench->kernels);
                parse_kernels_predeclared(&bench->kernels, bench->files[f].parser.predeclared, false);
            }
            for (uint32_t which = PARSE_TIMED_TWIN_K1; which < PARSE_TIMED_COUNT; which += 1)
            {
                samples[which][pass == 0 ? 0 : pass - 1] = totals[which];
            }
        }
    }
    (void)sink;
    printf("%u interleaved passes over %u files, %llu tokens, medians (stages per file, less %llu cycles a sample; %s counters):\n", passes, bench->file_count,
           (unsigned long long)bench->token_count, (unsigned long long)overhead.counts[PARSE_BENCHMARK_CYCLES], parse_benchmark_set_names[set]);
    for (uint32_t which = 0; which < PARSE_TIMED_COUNT; which += 1)
    {
        const bool path = which <= PARSE_TIMED_VECTOR;
        if (only < 0 || only == (int32_t)which || (!path && only == PARSE_TIMED_COUNT))
        {
            const bool alone = !path && only == (int32_t)which;
            parse_benchmark_report(parse_benchmark_timed_names[which], samples[which], passes, bench->token_count * (alone ? repeat : 1), set);
        }
    }
    parse_benchmark_close_counters(&counters);
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

int run_parser_benchmark(int argc, char** argv)
{
    int result = 1;
    uint32_t passes = BUSTER_PARSE_BENCHMARK_PASSES;
    uint32_t repeat = 1;
    int32_t only = -1;
    ParseBenchmarkSet set = PARSE_BENCHMARK_PIPELINE;
    bool valid = true;
    char** passed = calloc((size_t)argc + 2, sizeof(char*));
    char** paths = calloc((size_t)argc + 1, sizeof(char*));
    int passed_count = 0;
    uint32_t path_count = 0;
    for (int i = 1; valid && passed != 0 && paths != 0 && i < argc; i += 1)
    {
        const char* argument = argv[i];
        const bool takes_value = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0 ||
                                 pp_own_option_with_value(argument) || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
        if (strcmp(argument, "--passes") == 0 && i + 1 < argc)
        {
            i += 1;
            passes = (uint32_t)strtoul(argv[i], 0, 10);
            valid = passes >= 1 && passes <= BUSTER_PARSE_BENCHMARK_MAX_PASSES;
        }
        else if (strcmp(argument, "--repeat") == 0 && i + 1 < argc)
        {
            i += 1;
            repeat = (uint32_t)strtoul(argv[i], 0, 10);
            valid = repeat >= 1;
        }
        else if (strcmp(argument, "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "stages") == 0 ? PARSE_TIMED_COUNT : -2;
            for (uint32_t which = 0; which < PARSE_TIMED_COUNT; which += 1)
            {
                only = strcmp(argv[i], parse_benchmark_timed_names[which]) == 0 ? (int32_t)which : only;
            }
            valid = only != -2;
        }
        else if (strcmp(argument, "--counters") == 0 && i + 1 < argc)
        {
            i += 1;
            set = PARSE_BENCHMARK_SETS;
            for (uint32_t s = 0; s < PARSE_BENCHMARK_SETS; s += 1)
            {
                set = strcmp(argv[i], parse_benchmark_set_names[s]) == 0 ? (ParseBenchmarkSet)s : set;
            }
            valid = set != PARSE_BENCHMARK_SETS;
        }
        else if (argument[0] == '-')
        {
            passed[passed_count] = argv[i];
            passed_count += 1;
            if (takes_value && i + 1 < argc)
            {
                i += 1;
                passed[passed_count] = argv[i];
                passed_count += 1;
            }
        }
        else
        {
            paths[path_count] = argv[i];
            path_count += 1;
        }
    }
    // -E's options, through -E's parser with the first file as its input.
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input_path;
    const char* output_path;
    bool markers;
    if (valid && passed != 0 && path_count != 0)
    {
        passed[passed_count] = paths[0];
        valid = pp_parse_options(passed_count + 1, passed, &options, &arena, &input_path, &output_path, &markers);
    }
    ParseBenchmark bench = { 0 };
    // At the files' own alignment, which calloc promises only to 16 bytes: clang's aligned stores into a member aligned
    // past that fault when it isn't (a Preprocessor's, 64 bytes, when the files held one).
    bench.files = valid && path_count != 0 ? aligned_alloc(_Alignof(ParseBenchmarkFile), path_count * sizeof(ParseBenchmarkFile)) : 0;
    if (bench.files != 0)
    {
        memset(bench.files, 0, path_count * sizeof(ParseBenchmarkFile));
    }
    if (!valid || path_count == 0 || bench.files == 0)
    {
        fprintf(stderr, "usage: compiler benchmark-parser [--passes N] [--only NAME|stages] [--repeat N] [--counters pipeline|branches|memory|stalls] "
                        "[-E's options] file...\n");
    }
    else
    {
        // Each file preprocessed, and parsed once by each path, which must agree.
        uint32_t largest = 0;
        bool loaded = true;
        for (uint32_t f = 0; loaded && f < path_count; f += 1)
        {
            // A file's intern table and preprocessor are needed only to load it: the stages read its output and its
            // tokens.
            static InternTable table;
            static Preprocessor pp;
            ParseBenchmarkFile* file = &bench.files[f];
            const ParseOptions parse_options = { .scalar = false };
            file->path = paths[f];
            loaded = intern_initialize(&table) && pp_initialize(&pp, &options, &table) && parse_initialize(&file->parser, &parse_options, options.language) &&
                     pp_run(&pp, file->path);
            if (!loaded)
            {
                fprintf(stderr, "%s: doesn't preprocess\n", file->path);
            }
            else
            {
                static Parser scalar;
                const ParseOptions scalar_options = { .scalar = true };
                const bool vector_ok = parse_translation_unit(&file->parser, &pp);
                const bool scalar_ok = parse_initialize(&scalar, &scalar_options, options.language) && parse_translation_unit(&scalar, &pp);
                bool same = vector_ok == scalar_ok && file->parser.tokens.count == scalar.tokens.count;
                for (uint32_t t = 0; same && vector_ok && t < scalar.tokens.count; t += 1)
                {
                    same = file->parser.tree.parents[t] == scalar.tree.parents[t] && file->parser.tree.depths[t] == scalar.tree.depths[t];
                }
                if (!same)
                {
                    fprintf(stderr, "%s: the vector path and the scalar parser disagree\n", file->path);
                    loaded = false;
                }
                printf("%s: %u tokens%s; the fallback took %llu top-level declarations, %llu tokens (%.3f%%)%s\n", file->path, file->parser.tokens.count,
                       vector_ok ? "" : " (with errors)", (unsigned long long)file->parser.statistics.fallback_items,
                       (unsigned long long)file->parser.statistics.fallback_tokens,
                       file->parser.tokens.count != 0 ? 100.0 * (double)file->parser.statistics.fallback_tokens / (double)file->parser.tokens.count : 0.0,
                       file->parser.statistics.whole ? ", the whole unit" : "");
                parse_release(&scalar);
                bench.token_count += file->parser.tokens.count;
                largest = file->parser.tokens.count > largest ? file->parser.tokens.count : largest;
            }
            // What the stages don't read goes, the reservations of hundreds of gigabytes a preprocessor and a parser
            // make among them: kept for every file, a corpus of hundreds of files would exhaust the address space.
            file->output = pp.output;
            pp.output = (PPTokens){ 0 };
            pp_release(&pp);
            intern_release(&table);
            arena_release(&file->parser.scratch);
            arena_release(&file->parser.vector_arena);
        }
        bench.file_count = path_count;
        // The shared arrays, for the largest file and every file's names.
        uint32_t names = 0;
        for (uint32_t f = 0; loaded && f < path_count; f += 1)
        {
            names = bench.files[f].parser.meanings.capacity > names ? bench.files[f].parser.meanings.capacity : names;
        }
        const uint64_t slots = (uint64_t)largest + 2 * BUSTER_PARSE_TOKEN_PADDING;
        Arena shared = arena_reserve(slots * 1024 + names + (256ull << 20));
        bench.scratch = arena_reserve(1ull << 36);
        bench.diagnostic_capacity = largest + 16;
        if (loaded)
        {
            const uint64_t capacity = 2 * (uint64_t)largest + 64;
            bench.tree.roles = BUSTER_ARENA_PUSH_ARRAY(&shared, uint8_t, slots);
            bench.tree.depths = BUSTER_ARENA_PUSH_ARRAY(&shared, uint32_t, slots);
            bench.tree.parents = BUSTER_ARENA_PUSH_ARRAY(&shared, uint32_t, slots);
            bench.typed = BUSTER_ARENA_PUSH_ARRAY(&shared, uint8_t, slots);
            bench.statements = BUSTER_ARENA_PUSH_ARRAY(&shared, ParseStatement, slots);
            bench.diagnostics = BUSTER_ARENA_PUSH_ARRAY(&shared, ParseDiagnostic, bench.diagnostic_capacity);
            uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(&shared, uint8_t, capacity + 2 * BUSTER_PARSE_TOKEN_PADDING);
            uint32_t* token_names = BUSTER_ARENA_PUSH_ARRAY(&shared, uint32_t, capacity + 2 * BUSTER_PARSE_TOKEN_PADDING);
            uint32_t* sources = BUSTER_ARENA_PUSH_ARRAY(&shared, uint32_t, capacity + 2 * BUSTER_PARSE_TOKEN_PADDING);
            uint8_t* flags = BUSTER_ARENA_PUSH_ARRAY(&shared, uint8_t, capacity + 2 * BUSTER_PARSE_TOKEN_PADDING);
            uint32_t* pragma_sources = BUSTER_ARENA_PUSH_ARRAY(&shared, uint32_t, capacity);
            uint32_t* pragma_positions = BUSTER_ARENA_PUSH_ARRAY(&shared, uint32_t, capacity);
            bench.converted = (ParseTokens){
                .kinds = kinds != 0 ? kinds + BUSTER_PARSE_TOKEN_PADDING : 0,
                .names = token_names != 0 ? token_names + BUSTER_PARSE_TOKEN_PADDING : 0,
                .sources = sources != 0 ? sources + BUSTER_PARSE_TOKEN_PADDING : 0,
                .flags = flags != 0 ? flags + BUSTER_PARSE_TOKEN_PADDING : 0,
                .count = 0,
                .capacity = (uint32_t)capacity,
                .pragma_sources = pragma_sources,
                .pragma_positions = pragma_positions,
                .pragma_count = 0,
            };
            loaded = bench.tree.roles != 0 && bench.tree.depths != 0 && bench.tree.parents != 0 && bench.typed != 0 && bench.statements != 0 && bench.diagnostics != 0 &&
                     kinds != 0 && token_names != 0 && sources != 0 && flags != 0 && pragma_sources != 0 && pragma_positions != 0 &&
                     parse_kernels_reserve(&bench.kernels, &shared, largest, names) && parse_vector_buffers_reserve(&bench.buffers, &shared, largest);
            bench.tree.roles = bench.tree.roles != 0 ? bench.tree.roles + BUSTER_PARSE_TOKEN_PADDING : 0;
            bench.tree.depths = bench.tree.depths != 0 ? bench.tree.depths + BUSTER_PARSE_TOKEN_PADDING : 0;
            bench.tree.parents = bench.tree.parents != 0 ? bench.tree.parents + BUSTER_PARSE_TOKEN_PADDING : 0;
        }
        if (loaded)
        {
            parse_benchmark_time(&bench, passes, only, repeat, set);
            result = 0;
        }
        for (uint32_t f = 0; f < path_count; f += 1)
        {
            parse_release(&bench.files[f].parser);
            pp_tokens_release(&bench.files[f].output);
        }
        arena_release(&shared);
        arena_release(&bench.scratch);
    }
    free(bench.files);
    free(passed);
    free(paths);
    arena_release(&arena);
    return result;
}
