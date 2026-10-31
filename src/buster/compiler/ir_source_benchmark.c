// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #131's measurement on real C (benchmark-ir-source): the IR's bytes per source token of the functions the driver's -c
// compiles of a set of files, with the largest function's share (#15: sqlite3.c's sqlite3VdbeExec, 5.0% of its -O0
// code), and the time per typed entry of lowering them (ir_lower_typed and its scalar twin), of verifying the result
// (ir_verify and ir_verify_scalar), of promoting it to SSA (ssa_ir_promote and ssa_ir_promote_scalar), with each
// promotion's phis (#134), of #866's two GVN paths on the lowered IR, and of the shared SCCP/DCE/CFG optimizer's
// default and forced-scalar paths (#860). Include after ir_source_test.c and
// tier0_benchmark_measure.c, whose clock, counters and medians it takes.
//
//   build/compiler benchmark-ir-source [--only VARIANT] [--after-scalar MS] [--functions] [preprocessor options] file...
//
// The files' functions are prepared as test-ir-source prepares them (ir_source_prepare), each lowered once by both
// paths, which must agree, into the IR the verifier, promotion and GVN variants read, and promoted once by both (and
// given to GVN once by both), outside the time. Any file that fails or is left out, and any function either path
// refuses, fails the benchmark, which then reports no time: its corpus would be less than the files'. The bytes are
// the function arenas' (IrFunction.arena_bytes: every column and side table, with their alignment gaps, each function
// in an arena of its own), lowered and promoted; the tokens the parser's. Each variant's
// run takes every function once, after a warm-up, in BUSTER_TIER0_BENCHMARK_PASSES passes that alternate which of each
// pair runs first; --only times one alone, back to back, and --after-scalar MS runs a vector variant's scalar twin, untimed,
// for MS ms before each of its passes. Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...).

#define BUSTER_IR_SOURCE_LOWER 0
#define BUSTER_IR_SOURCE_LOWER_SCALAR 1
#define BUSTER_IR_SOURCE_VERIFY 2
#define BUSTER_IR_SOURCE_VERIFY_SCALAR 3
#define BUSTER_IR_SOURCE_PROMOTE 4
#define BUSTER_IR_SOURCE_PROMOTE_SCALAR 5
#define BUSTER_IR_SOURCE_GVN 6
#define BUSTER_IR_SOURCE_GVN_SCALAR 7
#define BUSTER_IR_SOURCE_OPTIMIZE 8
#define BUSTER_IR_SOURCE_OPTIMIZE_SCALAR 9
#define BUSTER_IR_SOURCE_OPTIMIZE_VECTOR 10
#define BUSTER_IR_SOURCE_VARIANTS 11
#define BUSTER_IR_SOURCE_LARGEST 10

static const char* const ir_source_variant_names[BUSTER_IR_SOURCE_VARIANTS] = {
    "lower",
    "lower-scalar",
    "verify",
    "verify-scalar",
    "promote",
    "promote-scalar",
    "gvn",
    "gvn-scalar",
    "optimize",
    "optimize-scalar",
    "optimize-vector",
};

// The corpus: every file's functions (their streams in `lowered`, their table in `table`), each one's IR as the vector
// path lowered it (in `kept`), and what they came to.
typedef struct IrSourceCorpus IrSourceCorpus;
struct IrSourceCorpus
{
    IrSourceFunction* functions;
    IrFunction* lowered;
    uint32_t count;
    uint32_t capacity;
    uint64_t files;
    uint64_t tokens;
    uint64_t entries;
    uint64_t instructions;
    uint64_t bytes;
    uint64_t promoted_instructions;
    uint64_t promoted_bytes;
    // Each promotion's phis (#134): the vector path's (ssa_ir_promote), then its twin's.
    uint64_t phis[2];
    // #866's GVN on the lowered IR, run once by each path outside the time: the instructions it removed, its
    // refinement rounds, key comparisons and collision-budget sort completions.
    uint64_t gvn_changed[2];
    uint64_t gvn_rounds;
    uint64_t gvn_probes;
    uint64_t gvn_sort_fallbacks;
    // The lowered IR's bytes of its side tables, without the alignment gaps (ir_source_payload).
    uint64_t tables;
    // Functions a path refused or the paths lowered apart, and files that failed or were left out.
    uint64_t failures;
};

// A function's bytes but its instruction columns' and its alignment gaps: its blocks', side tables' and records'.
static uint64_t ir_source_tables(const IrFunction* f)
{
    return ((uint64_t)f->block_count + 1) * 4 + (uint64_t)f->extra_count * 4 + (uint64_t)f->constant_count * sizeof(IrConstant) +
           (uint64_t)f->slot_count * sizeof(IrSlot) + (uint64_t)f->address_count * sizeof(IrAddress) + (uint64_t)f->call_count * sizeof(IrCall) + f->call_type_count +
           f->parameter_count + (uint64_t)f->label_count * 4 + (uint64_t)f->abi_call_count * sizeof(IrAbiCall) + (uint64_t)f->passed_count * sizeof(IrPassed);
}

// One run of `variant` over the corpus, its output put back after each function; `failures` counts the functions it
// refused.
static Tier0BenchmarkReading ir_source_run(uint32_t variant, const IrSourceCorpus* corpus, Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    uint32_t failures = 0;
    const uint64_t mark = output->length;
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        const IrSourceFunction* source = &corpus->functions[f];
        const IrFunction* lowered = &corpus->lowered[f];
        IrFunction result;
        IrCheck check = { .error = IR_OK };
        switch (variant)
        {
        case BUSTER_IR_SOURCE_LOWER: check = ir_lower_typed(source->function, source->result_type, source->symbol_count, output, scratch, &result); break;
        case BUSTER_IR_SOURCE_LOWER_SCALAR: check = ir_lower_typed_scalar(source->function, source->result_type, source->symbol_count, output, scratch, &result); break;
        case BUSTER_IR_SOURCE_VERIFY: check = ir_verify(lowered, scratch, 0); break;
        case BUSTER_IR_SOURCE_VERIFY_SCALAR: check = ir_verify_scalar(lowered, scratch, 0); break;
        case BUSTER_IR_SOURCE_PROMOTE: check = ssa_ir_promote(lowered, output, scratch, &result); break;
        case BUSTER_IR_SOURCE_GVN:
        case BUSTER_IR_SOURCE_GVN_SCALAR:
        {
            IrGvnOptions options = ir_gvn_options();
            options.scalar = variant == BUSTER_IR_SOURCE_GVN_SCALAR;
            IrGvnResult run = { 0 };
            check = ir_optimize_gvn(lowered, output, scratch, options, &run);
        }
        break;
        case BUSTER_IR_SOURCE_OPTIMIZE:
        case BUSTER_IR_SOURCE_OPTIMIZE_SCALAR:
        case BUSTER_IR_SOURCE_OPTIMIZE_VECTOR:
        {
            // #860: `optimize` is the default AUTO path, as a caller's ir_optimize_options gives it; optimize-scalar
            // and optimize-vector are the forced twins, so the corpus measures the bound's cost against the paths it
            // chooses between.
            IrOptimizeOptions options = ir_optimize_options();
            options.path = variant == BUSTER_IR_SOURCE_OPTIMIZE_SCALAR ? BUSTER_IR_OPTIMIZE_SCALAR : variant == BUSTER_IR_SOURCE_OPTIMIZE_VECTOR ? BUSTER_IR_OPTIMIZE_VECTOR : BUSTER_IR_OPTIMIZE_AUTO;
            IrOptimizeResult run = { 0 };
            check = ir_optimize(lowered, output, scratch, options, &run);
        }
        break;
        default: check = ssa_ir_promote_scalar(lowered, output, scratch, &result); break;
        }
        failures += check.error != IR_OK;
        arena_pop_to(output, mark);
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

// Adds `file`'s functions to the corpus, each lowered by both paths (which must agree, the vector path's IR kept in
// `kept`) and promoted by both (into `output`, put back), and prints the file's line: whether every function joined.
static bool ir_source_corpus_file(IrSourceCorpus* corpus, const char* name, char** options, uint32_t option_count, Arena* lowered, Arena* table, Arena* kept,
                                  Arena* output, Arena* scratch)
{
    IrSourceFile file;
    ir_source_prepare(&file, name, options, option_count, lowered, table, false);
    bool joined = file.status == IR_SOURCE_PREPARED && corpus->count + file.count <= corpus->capacity;
    uint64_t bytes = 0;
    uint64_t instructions = 0;
    for (uint32_t f = 0; joined && f < file.count; f += 1)
    {
        const IrSourceFunction* source = &file.functions[f];
        IrFunction* vector = &corpus->lowered[corpus->count];
        IrFunction scalar = { 0 };
        IrFunction promoted = { 0 };
        IrFunction promoted_twin = { 0 };
        const uint64_t mark = output->length;
        // Each function starts its kept columns at a 64-byte boundary, as in an arena of its own (#131's one per
        // function), so that its bytes, alignment gaps and all, don't depend on the functions before it.
        const uint64_t padding = (64 - (kept->length & 63)) & 63;
        bool lowers = padding == 0 || arena_push(kept, padding, 1) != 0;
        lowers = lowers && ir_lower_typed(source->function, source->result_type, source->symbol_count, kept, scratch, vector).error == IR_OK &&
                            ir_lower_typed_scalar(source->function, source->result_type, source->symbol_count, output, scratch, &scalar).error == IR_OK &&
                            ir_lower_test_equal(vector, &scalar);
        // Each promotion from the same mark, an arena of its own's start, as test-ir-source's are.
        arena_pop_to(output, mark);
        const IrCheck promotion = lowers ? ssa_ir_promote(vector, output, scratch, &promoted) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        const uint64_t promoted_bytes = promoted.arena_bytes;
        uint32_t trivial = 0;
        const uint32_t phis = promotion.error == IR_OK ? ssa_ir_phis(&promoted, &trivial) : 0;
        arena_pop_to(output, mark);
        const IrCheck promotion_twin = lowers ? ssa_ir_promote_scalar(vector, output, scratch, &promoted_twin) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        const uint32_t twin_phis = promotion_twin.error == IR_OK ? ssa_ir_phis(&promoted_twin, &trivial) : 0;
        arena_pop_to(output, mark);
        // #866: both GVN paths on the lowered IR, once, outside the time. GVN verifies its output; the scalar
        // available-expression path must accept the function too, and its removal count is the cyclic-quality control
        // against the vector congruence solver's.
        IrGvnOptions gvn_options = ir_gvn_options();
        IrGvnResult gvn = { 0 };
        const IrCheck gvn_check = lowers ? ir_optimize_gvn(vector, output, scratch, gvn_options, &gvn) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        arena_pop_to(output, mark);
        gvn_options.scalar = true;
        IrGvnResult gvn_twin = { 0 };
        const IrCheck gvn_twin_check = lowers ? ir_optimize_gvn(vector, output, scratch, gvn_options, &gvn_twin) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        arena_pop_to(output, mark);
        // #860: the optimizer's default AUTO path and both forced twins on the lowered IR, once, outside the time.
        // The twins must accept and agree exactly: a corpus function they don't is left out, as the optimizer's own
        // fixtures require of the paths it chooses between.
        IrOptimizeOptions optimize_options = ir_optimize_options();
        IrOptimizeResult optimized = { 0 };
        IrOptimizeResult optimized_vector = { 0 };
        IrOptimizeResult optimized_scalar = { 0 };
        const IrCheck optimize_check = lowers ? ir_optimize(vector, output, scratch, optimize_options, &optimized) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        arena_pop_to(output, mark);
        optimize_options.path = BUSTER_IR_OPTIMIZE_VECTOR;
        const IrCheck optimize_vector_check = lowers ? ir_optimize(vector, output, scratch, optimize_options, &optimized_vector) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        arena_pop_to(output, mark);
        optimize_options.path = BUSTER_IR_OPTIMIZE_SCALAR;
        const IrCheck optimize_scalar_check = lowers ? ir_optimize(vector, output, scratch, optimize_options, &optimized_scalar) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
        arena_pop_to(output, mark);
        const bool optimizes = optimize_check.error == IR_OK && optimize_vector_check.error == IR_OK && optimize_scalar_check.error == IR_OK &&
                               ir_optimize_test_equal(&optimized.function, &optimized_vector.function) && ir_optimize_test_equal(&optimized.function, &optimized_scalar.function);
        joined = promotion.error == IR_OK && promotion_twin.error == IR_OK && gvn_check.error == IR_OK && gvn_twin_check.error == IR_OK && optimizes;
        if (joined)
        {
            corpus->functions[corpus->count] = *source;
            corpus->count += 1;
            corpus->entries += source->function->count;
            corpus->instructions += vector->instruction_count;
            corpus->bytes += vector->arena_bytes;
            corpus->tables += ir_source_tables(vector);
            corpus->promoted_instructions += promoted.instruction_count;
            corpus->promoted_bytes += promoted_bytes;
            corpus->phis[0] += phis;
            corpus->phis[1] += twin_phis;
            corpus->gvn_changed[0] += gvn.removed;
            corpus->gvn_changed[1] += gvn_twin.removed;
            corpus->gvn_rounds += gvn.rounds;
            corpus->gvn_probes += gvn.partition_probes;
            corpus->gvn_sort_fallbacks += gvn.partition_sort_fallbacks;
            bytes += vector->arena_bytes;
            instructions += vector->instruction_count;
        }
        else
        {
            const char* reason = !lowers ? "a lowering refused it, or the paths disagree" : promotion.error != IR_OK || promotion_twin.error != IR_OK ? "a promotion refused it" :
                                 gvn_check.error != IR_OK || gvn_twin_check.error != IR_OK ? "GVN refused it" : "the optimizer refused it, or its paths disagree";
            printf("benchmark-ir-source: file=%s function=%s failed=\"%s\"\n", name, source->name, reason);
        }
        arena_pop_to(output, mark);
    }
    if (joined)
    {
        corpus->files += 1;
        corpus->tokens += file.tokens;
        printf("benchmark-ir-source: file=%s functions=%u tokens=%" PRIu64 " instructions=%" PRIu64 " bytes=%" PRIu64 " (%.2f per token)\n", name, file.count, file.tokens,
               instructions, bytes, file.tokens != 0 ? (double)bytes / (double)file.tokens : 0.0);
    }
    else if (file.status != IR_SOURCE_PREPARED)
    {
        printf("benchmark-ir-source: file=%s %s=\"%s\"\n", name, file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
    }
    corpus->failures += !joined;
    return joined;
}

// The bytes report: the totals, per source token, typed entry and IR instruction, and the largest functions, by their
// lowered IR's bytes, each with its share of the whole.
static void ir_source_bytes(const IrSourceCorpus* corpus)
{
    const double tokens = (double)corpus->tokens;
    printf("benchmark-ir-source: %u functions of %" PRIu64 " files, %" PRIu64 " source tokens, %" PRIu64 " typed entries (%.3f per token)\n", corpus->count, corpus->files,
           corpus->tokens, corpus->entries, (double)corpus->entries / tokens);
    printf("benchmark-ir-source: lowered IR: %" PRIu64 " instructions (%.3f per token, %.3f per entry), %" PRIu64 " bytes: %.2f per token, %.2f per entry, %.2f per instruction\n",
           corpus->instructions, (double)corpus->instructions / tokens, (double)corpus->instructions / (double)corpus->entries, corpus->bytes, (double)corpus->bytes / tokens,
           (double)corpus->bytes / (double)corpus->entries, (double)corpus->bytes / (double)corpus->instructions);
    const uint64_t columns = 16 * corpus->instructions;
    const uint64_t gaps = corpus->bytes - columns - corpus->tables;
    printf("benchmark-ir-source: lowered IR's bytes per token: %.2f the instruction columns', %.2f the blocks' and side tables', %.2f the 64-byte alignment gaps\n",
           (double)columns / tokens, (double)corpus->tables / tokens, (double)gaps / tokens);
    printf("benchmark-ir-source: promoted IR: %" PRIu64 " instructions (%.3f per token), %" PRIu64 " bytes: %.2f per token\n", corpus->promoted_instructions,
           (double)corpus->promoted_instructions / tokens, corpus->promoted_bytes, (double)corpus->promoted_bytes / tokens);
    printf("benchmark-ir-source: phis: %" PRIu64 " by promote (%.4f per lowered instruction), %" PRIu64 " by promote-scalar (%.4f)\n", corpus->phis[0],
           (double)corpus->phis[0] / (double)corpus->instructions, corpus->phis[1], (double)corpus->phis[1] / (double)corpus->instructions);
    printf("benchmark-ir-source: GVN removed %" PRIu64 " instructions by gvn and %" PRIu64 " by gvn-scalar; %" PRIu64 " refinement rounds, %" PRIu64 " key comparisons, %" PRIu64 " collision-budget sort completions\n",
           corpus->gvn_changed[0], corpus->gvn_changed[1], corpus->gvn_rounds, corpus->gvn_probes, corpus->gvn_sort_fallbacks);
    // The largest functions, by insertion into a short list sorted by bytes, the first function of equal bytes first.
    uint32_t largest[BUSTER_IR_SOURCE_LARGEST];
    uint32_t kept = 0;
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        uint32_t place = kept < BUSTER_IR_SOURCE_LARGEST ? kept : BUSTER_IR_SOURCE_LARGEST;
        while (place != 0 && corpus->lowered[largest[place - 1]].arena_bytes < corpus->lowered[f].arena_bytes)
        {
            place -= 1;
        }
        if (place < BUSTER_IR_SOURCE_LARGEST)
        {
            const uint32_t last = kept < BUSTER_IR_SOURCE_LARGEST ? kept : BUSTER_IR_SOURCE_LARGEST - 1;
            memmove(largest + place + 1, largest + place, (size_t)(last - place) * sizeof(largest[0]));
            largest[place] = f;
            kept += kept < BUSTER_IR_SOURCE_LARGEST ? 1 : 0;
        }
    }
    uint64_t sum = 0;
    for (uint32_t k = 0; k < kept; k += 1)
    {
        const IrSourceFunction* source = &corpus->functions[largest[k]];
        const IrFunction* function = &corpus->lowered[largest[k]];
        sum += function->arena_bytes;
        printf("benchmark-ir-source: largest %2u: %s, %" PRIu64 " bytes (%.2f%% of the bytes), %u instructions, %u typed entries; the %u largest %.2f%%\n", k + 1,
               source->name, function->arena_bytes, 100.0 * (double)function->arena_bytes / (double)corpus->bytes, function->instruction_count, source->function->count,
               k + 1, 100.0 * (double)sum / (double)corpus->bytes);
    }
}

// Times the variants (all, or `only`), each after a warm-up, and prints their medians per typed entry and per lowered IR
// instruction: whether every run of every function succeeded.
static bool ir_source_measure(const IrSourceCorpus* corpus, Arena* output, Arena* scratch, uint32_t only, uint64_t after_scalar)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    uint32_t failed = BUSTER_IR_SOURCE_VARIANTS;
    for (uint32_t v = 0; v < BUSTER_IR_SOURCE_VARIANTS && failed == BUSTER_IR_SOURCE_VARIANTS; v += 1)
    {
        failed = (only == BUSTER_IR_SOURCE_VARIANTS || v == only) && ir_source_run(v, corpus, output, scratch, counters).failures != 0 ? v : failed;
    }
    double values[BUSTER_IR_SOURCE_VARIANTS][4][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES && failed == BUSTER_IR_SOURCE_VARIANTS; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_IR_SOURCE_VARIANTS && failed == BUSTER_IR_SOURCE_VARIANTS; k += 1)
        {
            // Each pair's two paths take turns running first; optimize-vector has no twin to turn with.
            const uint32_t v = (pass & 1) != 0 && (k ^ 1) < BUSTER_IR_SOURCE_VARIANTS ? k ^ 1 : k;
            if (only != BUSTER_IR_SOURCE_VARIANTS && v != only)
            {
                continue;
            }
            if ((v & 1) == 0 && v + 1 < BUSTER_IR_SOURCE_VARIANTS && after_scalar != 0)
            {
                const uint64_t until = tier0_benchmark_now() + after_scalar * 1000000ull;
                while (tier0_benchmark_now() < until && failed == BUSTER_IR_SOURCE_VARIANTS)
                {
                    failed = ir_source_run(v + 1, corpus, output, scratch, counters).failures != 0 ? v + 1 : failed;
                }
            }
            const Tier0BenchmarkReading taken = failed == BUSTER_IR_SOURCE_VARIANTS ? ir_source_run(v, corpus, output, scratch, counters) : (Tier0BenchmarkReading){ 0 };
            failed = taken.failures != 0 ? v : failed;
            values[v][0][pass] = (double)taken.nanoseconds;
            values[v][1][pass] = (double)taken.cycles;
            values[v][2][pass] = (double)taken.instructions;
            values[v][3][pass] = (double)taken.throttle;
        }
    }
    if (failed != BUSTER_IR_SOURCE_VARIANTS)
    {
        printf("benchmark-ir-source: a function's %s failed, so its runs measured less than the corpus: no report\n", ir_source_variant_names[failed]);
    }
    else
    {
        printf("%-15s %12s %12s %12s %12s %12s %8s %12s\n", "variant", "ns/entry", "cyc/entry", "ins/entry", "ns/instr", "cyc/instr", "IPC", "thr/instr");
        for (uint32_t v = 0; v < BUSTER_IR_SOURCE_VARIANTS; v += 1)
        {
            if (only != BUSTER_IR_SOURCE_VARIANTS && v != only)
            {
                continue;
            }
            const double ns = tier0_benchmark_median(values[v][0]);
            const double cycles = tier0_benchmark_median(values[v][1]);
            const double instructions = tier0_benchmark_median(values[v][2]);
            const double throttle = tier0_benchmark_median(values[v][3]);
            const double e = (double)corpus->entries;
            const double r = (double)corpus->instructions;
            printf("%-15s %12.2f %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", ir_source_variant_names[v], ns / e, cycles / e, instructions / e, ns / r, cycles / r,
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
    return failed == BUSTER_IR_SOURCE_VARIANTS;
}

// --functions (#787): each function's promotion by both paths timed apart, with its blocks, slots and instructions:
// where the vector path pays against its twin. A sample is `repetitions` runs of a path, enough for both paths' one run
// (the calibration) to take BUSTER_IR_SOURCE_FUNCTION_NS together; each path's line takes its least time per run over
// BUSTER_IR_SOURCE_FUNCTION_SAMPLES samples, the paths taking turns running first.
#define BUSTER_IR_SOURCE_FUNCTION_NS 500000ull
#define BUSTER_IR_SOURCE_FUNCTION_SAMPLES 5

static uint64_t ir_source_function_run(const IrFunction* lowered, bool vector, uint32_t repetitions, Arena* output, Arena* scratch, bool* failed)
{
    const uint64_t mark = output->length;
    const uint64_t begin = tier0_benchmark_now();
    for (uint32_t r = 0; r < repetitions; r += 1)
    {
        IrFunction result;
        const IrCheck check = vector ? ssa_ir_promote(lowered, output, scratch, &result) : ssa_ir_promote_scalar(lowered, output, scratch, &result);
        *failed |= check.error != IR_OK;
        arena_pop_to(output, mark);
    }
    return tier0_benchmark_now() - begin;
}

// #860's per-function optimizer times, timed as the promotions' are: which twin each size should take on AUTO.
static uint64_t ir_source_optimize_run(const IrFunction* lowered, uint32_t path, uint32_t repetitions, Arena* output, Arena* scratch, bool* failed)
{
    const uint64_t mark = output->length;
    const uint64_t begin = tier0_benchmark_now();
    for (uint32_t r = 0; r < repetitions; r += 1)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.path = path;
        IrOptimizeResult result = { 0 };
        const IrCheck check = ir_optimize(lowered, output, scratch, options, &result);
        *failed |= check.error != IR_OK;
        arena_pop_to(output, mark);
    }
    return tier0_benchmark_now() - begin;
}

static bool ir_source_functions(const IrSourceCorpus* corpus, Arena* output, Arena* scratch)
{
    bool failed = false;
    for (uint32_t f = 0; f < corpus->count && !failed; f += 1)
    {
        const IrFunction* lowered = &corpus->lowered[f];
        const uint64_t once = ir_source_function_run(lowered, true, 1, output, scratch, &failed) + ir_source_function_run(lowered, false, 1, output, scratch, &failed);
        const uint64_t wanted = BUSTER_IR_SOURCE_FUNCTION_NS / (once != 0 ? once : 1) + 1;
        const uint32_t repetitions = wanted < 100000 ? (uint32_t)wanted : 100000;
        uint64_t least[2] = { UINT64_MAX, UINT64_MAX };
        for (uint32_t sample = 0; sample < BUSTER_IR_SOURCE_FUNCTION_SAMPLES && !failed; sample += 1)
        {
            for (uint32_t turn = 0; turn < 2; turn += 1)
            {
                const uint32_t path = (sample & 1) != 0 ? 1 - turn : turn;
                const uint64_t taken = ir_source_function_run(lowered, path == 0, repetitions, output, scratch, &failed);
                least[path] = taken < least[path] ? taken : least[path];
            }
        }
        IrFunction promoted;
        const uint64_t mark = output->length;
        failed |= ssa_ir_promote(lowered, output, scratch, &promoted).error != IR_OK;
        const uint32_t promoted_instructions = promoted.instruction_count;
        arena_pop_to(output, mark);
        uint64_t optimize_least[3] = { UINT64_MAX, UINT64_MAX, UINT64_MAX };
        const uint64_t optimize_once = ir_source_optimize_run(lowered, BUSTER_IR_OPTIMIZE_VECTOR, 1, output, scratch, &failed) +
                                       ir_source_optimize_run(lowered, BUSTER_IR_OPTIMIZE_SCALAR, 1, output, scratch, &failed) +
                                       ir_source_optimize_run(lowered, BUSTER_IR_OPTIMIZE_AUTO, 1, output, scratch, &failed);
        const uint64_t optimize_wanted = BUSTER_IR_SOURCE_FUNCTION_NS / (optimize_once != 0 ? optimize_once : 1) + 1;
        const uint32_t optimize_repetitions = optimize_wanted < 100000 ? (uint32_t)optimize_wanted : 100000;
        for (uint32_t sample = 0; sample < BUSTER_IR_SOURCE_FUNCTION_SAMPLES && !failed; sample += 1)
        {
            for (uint32_t turn = 0; turn < 3; turn += 1)
            {
                const uint32_t path = (sample & 1) != 0 ? 2 - turn : turn;
                const uint64_t taken = ir_source_optimize_run(lowered, path == 0 ? BUSTER_IR_OPTIMIZE_VECTOR : path == 1 ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_AUTO,
                                                              optimize_repetitions, output, scratch, &failed);
                optimize_least[path] = taken < optimize_least[path] ? taken : optimize_least[path];
            }
        }
        if (!failed)
        {
            printf("benchmark-ir-source: function=%s blocks=%u slots=%u instructions=%u promoted_instructions=%u vector_ns=%.1f scalar_ns=%.1f optimize_vector_ns=%.1f optimize_scalar_ns=%.1f optimize_auto_ns=%.1f\n", corpus->functions[f].name,
                   lowered->block_count, lowered->slot_count, lowered->instruction_count, promoted_instructions, (double)least[0] / repetitions, (double)least[1] / repetitions,
                   (double)optimize_least[0] / optimize_repetitions, (double)optimize_least[1] / optimize_repetitions, (double)optimize_least[2] / optimize_repetitions);
        }
    }
    return !failed;
}

static int run_ir_source_benchmark(int count, char** arguments)
{
    uint32_t only = BUSTER_IR_SOURCE_VARIANTS;
    uint64_t after_scalar = 0;
    bool functions = false;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--only") == 0)
        {
            uint32_t named = BUSTER_IR_SOURCE_VARIANTS;
            valid = i + 1 < count;
            for (uint32_t v = 0; valid && v < BUSTER_IR_SOURCE_VARIANTS; v += 1)
            {
                named = strcmp(arguments[i + 1], ir_source_variant_names[v]) == 0 ? v : named;
            }
            only = named;
            valid = valid && only != BUSTER_IR_SOURCE_VARIANTS;
            i += 1;
        }
        else if (strcmp(arguments[i], "--after-scalar") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &after_scalar) && after_scalar <= 10000;
        }
        else if (strcmp(arguments[i], "--functions") == 0)
        {
            functions = true;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0;
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    Arena table = arena_reserve(UINT64_C(1) << 32);
    Arena kept = arena_reserve(UINT64_C(1) << 36);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    IrSourceCorpus corpus = { .capacity = BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS };
    corpus.functions = valid && table.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&table, IrSourceFunction, corpus.capacity) : 0;
    corpus.lowered = corpus.functions != 0 ? BUSTER_ARENA_PUSH_ARRAY(&table, IrFunction, corpus.capacity) : 0;
    valid = valid && lowered.base != 0 && kept.base != 0 && output.base != 0 && scratch.base != 0 && corpus.lowered != 0;
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        ir_source_corpus_file(&corpus, files[i], options, option_count, &lowered, &table, &kept, &output, &scratch);
        fflush(stdout);
    }
    bool reported = false;
    if (valid && corpus.failures == 0 && corpus.count != 0 && corpus.entries != 0 && corpus.tokens != 0)
    {
        ir_source_bytes(&corpus);
        reported = functions ? ir_source_functions(&corpus, &output, &scratch) : ir_source_measure(&corpus, &output, &scratch, only, after_scalar);
    }
    else if (valid)
    {
        printf("benchmark-ir-source: %" PRIu64 " files or functions failed, or nothing was measured: no report\n", corpus.failures);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-ir-source [--only lower|lower-scalar|verify|verify-scalar|promote|promote-scalar|gvn|gvn-scalar|optimize|optimize-scalar|optimize-vector] [--after-scalar MS] [--functions] "
                        "[preprocessor options] file...\n");
    }
    fflush(stdout);
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&kept);
    arena_release(&table);
    arena_release(&lowered);
    free(files);
    free(options);
    return reported ? 0 : 1;
}
