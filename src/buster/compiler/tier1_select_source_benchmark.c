// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #141's selection on real C (benchmark-tier1-select-source): the time per IR instruction of selecting every function -c
// compiles of a set of files (tier1_select_plan_verified, as the C driver plans the optimizer's verified result), by both
// paths. Each function is lowered, promoted and optimized as the C driver's --tier1 takes it (c_check_tier1.c), and its
// dominance found, outside the time; a function a step before
// selection refuses, or that either path's selection refuses, is left out of both variants and counted by the step. Any
// other failure fails the benchmark, which then reports no time. Each variant's run selects every function once, after a
// warm-up, in BUSTER_TIER0_BENCHMARK_PASSES passes that alternate which path runs first; --only times one alone, back to
// back, and --after-scalar MS runs the scalar path, untimed, for MS ms before each of the vector path's passes. Include
// after tier1_cfg_source_benchmark.c and tier0_benchmark_measure.c, whose clock, counters and medians it takes. Run it
// under the 9700X's lock pinned to one CPU (taskset -c 2 ...).
//
//   build/compiler benchmark-tier1-select-source [--only select|select-scalar] [--after-scalar MS] [preprocessor options] file...

#define BUSTER_TIER1_SELECT_SOURCE_VECTOR 0
#define BUSTER_TIER1_SELECT_SOURCE_SCALAR 1
#define BUSTER_TIER1_SELECT_SOURCE_VARIANTS 2

static const char* const tier1_select_source_variant_names[BUSTER_TIER1_SELECT_SOURCE_VARIANTS] = {
    "select",
    "select-scalar",
};

// The corpus: each function's optimized IR and its dominance, in the kept arena, which both selections took.
typedef struct Tier1SelectSourceCorpus Tier1SelectSourceCorpus;
struct Tier1SelectSourceCorpus
{
    IrFunction* functions;
    IrDominance* dominance;
    uint32_t count;
    uint32_t capacity;
    uint64_t files;
    uint64_t seen;
    uint64_t unprepared;
    uint64_t refused;
    uint64_t entries;
    uint64_t instructions;
};

static Tier0BenchmarkReading tier1_select_source_run(uint32_t variant, const Tier1SelectSourceCorpus* corpus, Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    uint32_t failures = 0;
    const uint64_t output_mark = output->length;
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        // As the driver's --tier1 plans the optimizer's verified result (c_check_tier1.c).
        failures += tier1_select_plan_verified(&corpus->functions[f], &corpus->dominance[f], output, scratch, variant == BUSTER_TIER1_SELECT_SOURCE_VECTOR, &plan, &node) != TIER1_SELECT_OK;
        arena_pop_to(output, output_mark);
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

// Adds `file`'s functions to the corpus: each one lowered into `kept[0]`, promoted into `kept[1]` and optimized into
// `kept[2]` as the driver's --tier1 steps take it (each step's output in an arena apart from its input's, and kept, since
// the optimized function may share its input's columns), with its dominance, where every step and both paths' selections
// take it. Returns false on a failure the driver doesn't count as a refusal: a file that fails, or a corpus past its
// capacity.
static bool tier1_select_source_corpus_file(Tier1SelectSourceCorpus* corpus, const char* name, char** options, uint32_t option_count, Arena* lowered, Arena* table, Arena* kept, Arena* output, Arena* scratch)
{
    IrSourceFile file;
    ir_source_prepare(&file, name, options, option_count, lowered, table, false);
    bool valid = file.status == IR_SOURCE_PREPARED;
    if (!valid)
    {
        printf("benchmark-tier1-select-source: file=%s %s=\"%s\"\n", name, file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
    }
    uint32_t joined = 0;
    for (uint32_t f = 0; valid && f < file.count; f += 1)
    {
        const IrSourceFunction* source = &file.functions[f];
        const uint64_t marks[3] = { kept[0].length, kept[1].length, kept[2].length };
        IrFunction lowered_function = { 0 };
        IrFunction promoted = { 0 };
        IrOptimizeResult optimized = { 0 };
        IrCfg cfg = { 0 };
        Dominators dominators = { 0 };
        bool prepared = ir_lower_typed_target(source->function, source->result_type, source->symbol_count, source->target_features, &kept[0], scratch, &lowered_function).error == IR_OK;
        prepared = prepared && ssa_ir_promote(&lowered_function, &kept[1], scratch, &promoted).error == IR_OK;
        prepared = prepared && ir_optimize(&promoted, &kept[2], scratch, ir_optimize_options(), &optimized).error == IR_OK;
        prepared = prepared && ir_build_cfg(&optimized.function, &kept[2], scratch, &cfg) == IR_OK;
        if (prepared)
        {
            const DominatorCfg graph = {
                .block_count = cfg.block_count,
                .edge_count = cfg.edge_count,
                .entry = 0,
                .successor_offsets = cfg.successor_offsets,
                .successors = cfg.successors,
                .predecessor_offsets = cfg.predecessor_offsets,
                .predecessors = cfg.predecessors,
            };
            DominatorOptions dominator_options = dominators_options();
            dominator_options.outputs = 0;
            prepared = dominators_analyze(&kept[2], scratch, &graph, dominator_options, &dominators) == DOMINATOR_OK;
        }
        const IrDominance dominance = {
            .enter = dominators.preorder,
            .leave = dominators.subtree_end,
            .block_count = dominators.block_count,
        };
        bool selected = prepared;
        for (uint32_t path = 0; selected && path < 2; path += 1)
        {
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const uint64_t output_mark = output->length;
            selected = tier1_select_plan(&optimized.function, &dominance, output, scratch, path == 0, &plan, &node) == TIER1_SELECT_OK;
            arena_pop_to(output, output_mark);
        }
        corpus->seen += 1;
        corpus->unprepared += !prepared;
        corpus->refused += prepared && !selected;
        valid = !selected || corpus->count < corpus->capacity;
        if (selected && valid)
        {
            corpus->functions[corpus->count] = optimized.function;
            corpus->dominance[corpus->count] = dominance;
            corpus->count += 1;
            corpus->entries += source->function->count;
            corpus->instructions += optimized.function.instruction_count;
            joined += 1;
        }
        else
        {
            for (uint32_t k = 0; k < 3; k += 1)
            {
                arena_pop_to(&kept[k], marks[k]);
            }
        }
    }
    printf("benchmark-tier1-select-source: file=%s functions=%u joined=%u\n", name, file.count, joined);
    corpus->files += valid;
    return valid;
}

static bool tier1_select_source_measure(const Tier1SelectSourceCorpus* corpus, Arena* output, Arena* scratch, uint32_t only, uint64_t after_scalar)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    uint32_t failed = BUSTER_TIER1_SELECT_SOURCE_VARIANTS;
    for (uint32_t v = 0; v < BUSTER_TIER1_SELECT_SOURCE_VARIANTS && failed == BUSTER_TIER1_SELECT_SOURCE_VARIANTS; v += 1)
    {
        failed = (only == BUSTER_TIER1_SELECT_SOURCE_VARIANTS || v == only) && tier1_select_source_run(v, corpus, output, scratch, counters).failures != 0 ? v : failed;
    }
    double values[BUSTER_TIER1_SELECT_SOURCE_VARIANTS][4][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES && failed == BUSTER_TIER1_SELECT_SOURCE_VARIANTS; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_TIER1_SELECT_SOURCE_VARIANTS && failed == BUSTER_TIER1_SELECT_SOURCE_VARIANTS; k += 1)
        {
            // The two paths take turns running first.
            const uint32_t v = (pass & 1) != 0 ? k ^ 1 : k;
            if (only == BUSTER_TIER1_SELECT_SOURCE_VARIANTS || v == only)
            {
                if (v == BUSTER_TIER1_SELECT_SOURCE_VECTOR && after_scalar != 0)
                {
                    const uint64_t until = tier0_benchmark_now() + after_scalar * 1000000ull;
                    while (tier0_benchmark_now() < until && failed == BUSTER_TIER1_SELECT_SOURCE_VARIANTS)
                    {
                        failed = tier1_select_source_run(BUSTER_TIER1_SELECT_SOURCE_SCALAR, corpus, output, scratch, counters).failures != 0 ? BUSTER_TIER1_SELECT_SOURCE_SCALAR : failed;
                    }
                }
                const Tier0BenchmarkReading taken = failed == BUSTER_TIER1_SELECT_SOURCE_VARIANTS ? tier1_select_source_run(v, corpus, output, scratch, counters) : (Tier0BenchmarkReading){ 0 };
                failed = taken.failures != 0 ? v : failed;
                values[v][0][pass] = (double)taken.nanoseconds;
                values[v][1][pass] = (double)taken.cycles;
                values[v][2][pass] = (double)taken.instructions;
                values[v][3][pass] = (double)taken.throttle;
            }
        }
    }
    if (failed != BUSTER_TIER1_SELECT_SOURCE_VARIANTS)
    {
        printf("benchmark-tier1-select-source: a function's %s failed, so its runs measured less than the corpus: no report\n", tier1_select_source_variant_names[failed]);
    }
    else
    {
        printf("%-14s %12s %12s %12s %12s %8s %12s\n", "variant", "ns/instr", "cyc/instr", "ins/instr", "cyc/entry", "IPC", "thr/instr");
        for (uint32_t v = 0; v < BUSTER_TIER1_SELECT_SOURCE_VARIANTS; v += 1)
        {
            if (only == BUSTER_TIER1_SELECT_SOURCE_VARIANTS || v == only)
            {
                const double ns = tier0_benchmark_median(values[v][0]);
                const double cycles = tier0_benchmark_median(values[v][1]);
                const double instructions = tier0_benchmark_median(values[v][2]);
                const double throttle = tier0_benchmark_median(values[v][3]);
                const double r = (double)corpus->instructions;
                const double e = (double)corpus->entries;
                printf("%-14s %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", tier1_select_source_variant_names[v], ns / r, cycles / r, instructions / r, cycles / e, cycles != 0 ? instructions / cycles : 0.0, throttle / r);
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
    return failed == BUSTER_TIER1_SELECT_SOURCE_VARIANTS;
}

static __attribute__((unused)) int run_tier1_select_source_benchmark(int count, char** arguments)
{
    uint32_t only = BUSTER_TIER1_SELECT_SOURCE_VARIANTS;
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
            for (uint32_t v = 0; valid && v < BUSTER_TIER1_SELECT_SOURCE_VARIANTS; v += 1)
            {
                only = strcmp(arguments[i + 1], tier1_select_source_variant_names[v]) == 0 ? v : only;
            }
            valid = valid && only != BUSTER_TIER1_SELECT_SOURCE_VARIANTS;
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
    Arena kept[3] = {
        arena_reserve(UINT64_C(1) << 36),
        arena_reserve(UINT64_C(1) << 36),
        arena_reserve(UINT64_C(1) << 36),
    };
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    Tier1SelectSourceCorpus corpus = {
        .capacity = BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS,
    };
    valid = valid && lowered.base != 0 && table.base != 0 && kept[0].base != 0 && kept[1].base != 0 && kept[2].base != 0 && output.base != 0 && scratch.base != 0;
    corpus.functions = valid ? arena_push_array(&kept[2], corpus.capacity, sizeof(IrFunction), _Alignof(IrFunction)) : 0;
    corpus.dominance = valid ? arena_push_array(&kept[2], corpus.capacity, sizeof(IrDominance), _Alignof(IrDominance)) : 0;
    valid = valid && corpus.functions != 0 && corpus.dominance != 0;
    bool joined = valid;
    for (uint32_t i = 0; joined && i < file_count; i += 1)
    {
        // The files' preparations accumulate, as benchmark-tier1-cfg-source's do.
        joined = tier1_select_source_corpus_file(&corpus, files[i], options, option_count, &lowered, &table, kept, &output, &scratch);
        fflush(stdout);
    }
    bool measured = false;
    if (valid && joined)
    {
        printf("benchmark-tier1-select-source: files=%" PRIu64 " functions=%" PRIu64 " selected=%u unprepared=%" PRIu64 " refused=%" PRIu64 " entries=%" PRIu64 " instructions=%" PRIu64 "\n", corpus.files, corpus.seen, corpus.count,
               corpus.unprepared, corpus.refused, corpus.entries, corpus.instructions);
        measured = corpus.count != 0 && corpus.instructions != 0 && tier1_select_source_measure(&corpus, &output, &scratch, only, after_scalar);
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-tier1-select-source [--only select|select-scalar] [--after-scalar MS] [preprocessor options] file...\n");
    }
    arena_release(&scratch);
    arena_release(&output);
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&kept[k]);
    }
    arena_release(&table);
    arena_release(&lowered);
    free(files);
    free(options);
    return measured ? 0 : 1;
}
