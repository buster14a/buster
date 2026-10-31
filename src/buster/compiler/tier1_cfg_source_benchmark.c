// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #140's compile time on real C (benchmark-tier1-cfg-source): the time per IR instruction of allocating every function
// -c compiles of a set of files over its CFG, by both of tier1_cfg.c's paths, and the interval allocator's share
// (tier1_allocate on the CFG's own intervals), per IR instruction and per typed entry: benchmark-backend-source times
// Tier 0's compile of the same functions per typed entry (this command runs in the backend's unit, whose copies of Tier
// 0's tables ir_source_prepare's emission doesn't initialize: tier1_cfg_source_test.c). Include after
// tier1_cfg_source_test.c and tier0_benchmark_measure.c, whose clock, counters and medians it takes.
//
//   build/compiler benchmark-tier1-cfg-source [--only VARIANT] [--after-scalar MS] [preprocessor options] file...
//
// The files' functions are prepared, lowered and promoted as test-tier1-cfg-source prepares them, each adapted once
// (tier1_cfg_ir.c) and allocated once by both paths outside the time, whose results must pass tier1_cfg_verify; a
// function the model has no class for is left out of every variant, and counted. Any other failure fails
// the benchmark, which then reports no time. Each variant's run takes every function once, after a warm-up, in
// BUSTER_TIER0_BENCHMARK_PASSES passes that alternate which of each pair runs first; --only times one alone, back to
// back, and --after-scalar MS runs a vector variant's scalar twin, untimed, for MS ms before each of its passes. Run it
// under the 9700X's lock pinned to one CPU (taskset -c 2 ...).

#define BUSTER_TIER1_CFG_SOURCE_CFG 0
#define BUSTER_TIER1_CFG_SOURCE_CFG_SCALAR 1
#define BUSTER_TIER1_CFG_SOURCE_INTERVALS 2
#define BUSTER_TIER1_CFG_SOURCE_INTERVALS_SCALAR 3
#define BUSTER_TIER1_CFG_SOURCE_VARIANTS 4

static const char* const tier1_cfg_source_variant_names[BUSTER_TIER1_CFG_SOURCE_VARIANTS] = {
    "cfg",
    "cfg-scalar",
    "intervals",
    "intervals-scalar",
};

// The corpus: each function's allocation problem, with the intervals the vector path made of it.
typedef struct Tier1CfgSourceCorpus Tier1CfgSourceCorpus;
struct Tier1CfgSourceCorpus
{
    Tier1CfgInput* problems;
    Tier1AllocationInput* intervals;
    uint32_t count;
    uint32_t capacity;
    uint64_t files;
    uint64_t left_out;
    uint64_t entries;
    uint64_t instructions;
    uint64_t interval_count;
};

static Tier0BenchmarkReading tier1_cfg_source_run(uint32_t variant, const Tier1CfgSourceCorpus* corpus, Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    uint32_t failures = 0;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        Tier1CfgAllocation result;
        Tier1Allocation allocation;
        if (variant == BUSTER_TIER1_CFG_SOURCE_CFG)
        {
            failures += tier1_cfg_allocate(&corpus->problems[f], output, scratch, &result) != TIER1_CFG_OK;
        }
        else if (variant == BUSTER_TIER1_CFG_SOURCE_CFG_SCALAR)
        {
            failures += tier1_cfg_allocate_scalar(&corpus->problems[f], output, scratch, &result) != TIER1_CFG_OK;
        }
        else if (variant == BUSTER_TIER1_CFG_SOURCE_INTERVALS)
        {
            failures += tier1_allocate(&corpus->intervals[f], output, &allocation) != TIER1_ALLOCATION_OK;
        }
        else
        {
            failures += tier1_allocate_scalar(&corpus->intervals[f], output, &allocation) != TIER1_ALLOCATION_OK;
        }
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    const Tier0BenchmarkReading end = tier0_benchmark_read(counters);
    const Tier0BenchmarkReading taken = {
        .nanoseconds = end.nanoseconds - begin.nanoseconds,
        .cycles = end.cycles - begin.cycles,
        .instructions = end.instructions - begin.instructions,
        .throttle = end.throttle - begin.throttle,
        .failures = failures,
    };
    return taken;
}

// Adds `file`'s functions to the corpus: each one's problem and intervals in `kept`, after both paths' allocations of
// it passed the checker. Returns false on any failure but a function the model has no class for.
static bool tier1_cfg_source_corpus_file(Tier1CfgSourceCorpus* corpus, const char* name, char** options, uint32_t option_count, Arena* lowered, Arena* table, Arena* kept, Arena* output, Arena* scratch)
{
    IrSourceFile file;
    ir_source_prepare(&file, name, options, option_count, lowered, table, false);
    bool valid = file.status == IR_SOURCE_PREPARED;
    if (!valid)
    {
        printf("benchmark-tier1-cfg-source: file=%s %s=\"%s\"\n", name, file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
    }
    uint32_t joined = 0;
    for (uint32_t f = 0; valid && f < file.count; f += 1)
    {
        const IrSourceFunction* source = &file.functions[f];
        const uint64_t output_mark = output->length;
        IrFunction ir = { 0 };
        IrFunction promoted = { 0 };
        valid = ir_lower_typed_target(source->function, source->result_type, source->symbol_count, source->target_features, output, scratch, &ir).error == IR_OK;
        valid = valid && ssa_ir_promote(&ir, output, scratch, &promoted).error == IR_OK;
        Tier1CfgIrProblem problem = { 0 };
        const Tier1CfgIrStatus adapted = valid ? tier1_cfg_from_ir(&promoted, kept, scratch, &problem) : TIER1_CFG_IR_INVALID;
        valid = valid && (adapted == TIER1_CFG_IR_OK || adapted == TIER1_CFG_IR_UNSUPPORTED);
        corpus->left_out += adapted == TIER1_CFG_IR_UNSUPPORTED;
        if (valid && adapted == TIER1_CFG_IR_OK)
        {
            // Both paths' allocations, checked; the vector path's intervals kept for the interval allocator's variants.
            Tier1CfgAllocation results[2];
            for (uint32_t p = 0; valid && p < 2; p += 1)
            {
                Tier1CfgCheck check;
                valid = (p == 0 ? tier1_cfg_allocate(&problem.input, kept, scratch, &results[p]) : tier1_cfg_allocate_scalar(&problem.input, output, scratch, &results[p])) == TIER1_CFG_OK;
                valid = valid && tier1_cfg_verify(&problem.input, &results[p], scratch, &check);
            }
            valid = valid && corpus->count < corpus->capacity;
            if (valid)
            {
                corpus->problems[corpus->count] = problem.input;
                corpus->intervals[corpus->count] = results[0].intervals;
                corpus->count += 1;
                corpus->entries += source->function->count;
                corpus->instructions += problem.instructions;
                corpus->interval_count += results[0].intervals.count;
                joined += 1;
            }
        }
        if (!valid)
        {
            printf("benchmark-tier1-cfg-source: file=%s function=%s doesn't join the corpus\n", name, source->name);
        }
        arena_pop_to(output, output_mark);
    }
    printf("benchmark-tier1-cfg-source: file=%s functions=%u joined=%u\n", name, file.count, joined);
    corpus->files += valid;
    return valid;
}

static bool tier1_cfg_source_measure(const Tier1CfgSourceCorpus* corpus, Arena* output, Arena* scratch, uint32_t only, uint64_t after_scalar)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    uint32_t failed = BUSTER_TIER1_CFG_SOURCE_VARIANTS;
    for (uint32_t v = 0; v < BUSTER_TIER1_CFG_SOURCE_VARIANTS && failed == BUSTER_TIER1_CFG_SOURCE_VARIANTS; v += 1)
    {
        failed = (only == BUSTER_TIER1_CFG_SOURCE_VARIANTS || v == only) && tier1_cfg_source_run(v, corpus, output, scratch, counters).failures != 0 ? v : failed;
    }
    double values[BUSTER_TIER1_CFG_SOURCE_VARIANTS][4][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES && failed == BUSTER_TIER1_CFG_SOURCE_VARIANTS; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_TIER1_CFG_SOURCE_VARIANTS && failed == BUSTER_TIER1_CFG_SOURCE_VARIANTS; k += 1)
        {
            // Each pair's two paths take turns running first.
            const uint32_t v = (pass & 1) != 0 ? k ^ 1 : k;
            if (only == BUSTER_TIER1_CFG_SOURCE_VARIANTS || v == only)
            {
                if ((v & 1) == 0 && after_scalar != 0)
                {
                    const uint64_t until = tier0_benchmark_now() + after_scalar * 1000000ull;
                    while (tier0_benchmark_now() < until && failed == BUSTER_TIER1_CFG_SOURCE_VARIANTS)
                    {
                        failed = tier1_cfg_source_run(v + 1, corpus, output, scratch, counters).failures != 0 ? v + 1 : failed;
                    }
                }
                const Tier0BenchmarkReading taken = failed == BUSTER_TIER1_CFG_SOURCE_VARIANTS ? tier1_cfg_source_run(v, corpus, output, scratch, counters) : (Tier0BenchmarkReading){ 0 };
                failed = taken.failures != 0 ? v : failed;
                values[v][0][pass] = (double)taken.nanoseconds;
                values[v][1][pass] = (double)taken.cycles;
                values[v][2][pass] = (double)taken.instructions;
                values[v][3][pass] = (double)taken.throttle;
            }
        }
    }
    if (failed != BUSTER_TIER1_CFG_SOURCE_VARIANTS)
    {
        printf("benchmark-tier1-cfg-source: a function's %s failed, so its runs measured less than the corpus: no report\n", tier1_cfg_source_variant_names[failed]);
    }
    else
    {
        printf("%-17s %12s %12s %12s %12s %8s %12s\n", "variant", "ns/instr", "cyc/instr", "ins/instr", "cyc/entry", "IPC", "thr/instr");
        for (uint32_t v = 0; v < BUSTER_TIER1_CFG_SOURCE_VARIANTS; v += 1)
        {
            if (only == BUSTER_TIER1_CFG_SOURCE_VARIANTS || v == only)
            {
                const double ns = tier0_benchmark_median(values[v][0]);
                const double cycles = tier0_benchmark_median(values[v][1]);
                const double instructions = tier0_benchmark_median(values[v][2]);
                const double throttle = tier0_benchmark_median(values[v][3]);
                const double r = (double)corpus->instructions;
                const double e = (double)corpus->entries;
                printf("%-17s %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", tier1_cfg_source_variant_names[v], ns / r, cycles / r, instructions / r, cycles / e, cycles != 0 ? instructions / cycles : 0.0, throttle / r);
            }
        }
    }
    const int descriptors[] = {
        counters.cycles,
        counters.instructions,
        counters.throttle,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        if (descriptors[i] >= 0)
        {
            close(descriptors[i]);
        }
    }
    return failed == BUSTER_TIER1_CFG_SOURCE_VARIANTS;
}

static __attribute__((unused)) int run_tier1_cfg_source_benchmark(int count, char** arguments)
{
    uint32_t only = BUSTER_TIER1_CFG_SOURCE_VARIANTS;
    uint64_t after_scalar = 0;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--only") == 0)
        {
            valid = i + 1 < count;
            for (uint32_t v = 0; valid && v < BUSTER_TIER1_CFG_SOURCE_VARIANTS; v += 1)
            {
                only = strcmp(arguments[i + 1], tier1_cfg_source_variant_names[v]) == 0 ? v : only;
            }
            valid = valid && only != BUSTER_TIER1_CFG_SOURCE_VARIANTS;
            i += 1;
        }
        else if (strcmp(arguments[i], "--after-scalar") == 0)
        {
            valid = i + 1 < count;
            char* end = 0;
            after_scalar = valid ? strtoull(arguments[i + 1], &end, 10) : 0;
            valid = valid && end != arguments[i + 1] && *end == 0 && after_scalar <= 1000;
            i += 1;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count] = arguments[i];
            file_count += 1;
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            options[option_count] = arguments[i];
            option_count += 1;
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count] = valid ? arguments[i + 1] : 0;
                option_count += 1;
                i += 1;
            }
        }
    }
    valid = valid && file_count != 0;
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    Arena table = arena_reserve(UINT64_C(1) << 32);
    Arena kept = arena_reserve(UINT64_C(1) << 35);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    Tier1CfgSourceCorpus corpus = {
        .capacity = BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS,
    };
    valid = valid && lowered.base != 0 && table.base != 0 && kept.base != 0 && output.base != 0 && scratch.base != 0;
    corpus.problems = valid ? arena_push_array(&kept, corpus.capacity, sizeof(Tier1CfgInput), _Alignof(Tier1CfgInput)) : 0;
    corpus.intervals = valid ? arena_push_array(&kept, corpus.capacity, sizeof(Tier1AllocationInput), _Alignof(Tier1AllocationInput)) : 0;
    valid = valid && corpus.problems != 0 && corpus.intervals != 0;
    bool joined = valid;
    for (uint32_t i = 0; joined && i < file_count; i += 1)
    {
        // The files' preparations accumulate, as benchmark-ir-source's do.
        joined = tier1_cfg_source_corpus_file(&corpus, files[i], options, option_count, &lowered, &table, &kept, &output, &scratch);
        fflush(stdout);
    }
    bool measured = false;
    if (valid && joined)
    {
        printf("benchmark-tier1-cfg-source: files=%" PRIu64 " functions=%u left_out=%" PRIu64 " entries=%" PRIu64 " instructions=%" PRIu64 " intervals=%" PRIu64 "\n", corpus.files, corpus.count, corpus.left_out, corpus.entries, corpus.instructions, corpus.interval_count);
        measured = corpus.count != 0 && corpus.instructions != 0 && tier1_cfg_source_measure(&corpus, &output, &scratch, only, after_scalar);
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-tier1-cfg-source [--only cfg|cfg-scalar|intervals|intervals-scalar] [--after-scalar MS] [preprocessor options] file...\n");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&kept);
    arena_release(&table);
    arena_release(&lowered);
    free(files);
    free(options);
    return measured ? 0 : 1;
}
