// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The available typed-stream -> IR -> verifier boundary, measured as one path. Include after ir_benchmark.c. The existing Tier 0
// generator and ABI preparation make the inputs before timing; each timed ir_lower_typed call includes typed
// validation, block/stack planning, classification, output counting/allocation/emission and IR verification. The output
// arena is returned to its checkpoint after each function, as a lane would release a completed function's scratch.
//
// This measures generated typed entries, not source tokens or a complete C compilation. Memory totals are the sum of
// per-function core allocations, including alignment gaps; peak output is the largest one, not the process's peak RSS.
// The small and larger cases are value-only programs, as before #136's frame objects; a third case, reported apart, is
// the larger shape with the generator's objects (local arrays and structs, and COPY between them).
// Usage: benchmark-ir-lowering [--passes N] [--seed N] [--ordinary]

#define BUSTER_IR_LOWER_BENCHMARK_PROGRAMS 8u
#define BUSTER_IR_LOWER_BENCHMARK_INPUT_BYTES (512ull << 20)
#define BUSTER_IR_LOWER_BENCHMARK_OUTPUT_BYTES (256ull << 20)


typedef struct IrLowerBenchmarkFunction IrLowerBenchmarkFunction;
struct IrLowerBenchmarkFunction
{
    const Tier0Function* input;
    IrType result_type;
    uint32_t symbol_count;
    uint64_t seed;
    uint32_t index;
};

typedef struct IrLowerBenchmarkCorpus IrLowerBenchmarkCorpus;
struct IrLowerBenchmarkCorpus
{
    IrLowerBenchmarkFunction* functions;
    uint32_t count;
    uint64_t entries;
    uint64_t instructions;
    uint64_t core_bytes;
    uint64_t peak_output_bytes;
    uint64_t objects;
    uint64_t copies;
    uint64_t stack_ops;
    uint64_t scratch_bytes;
    uint64_t target_ops;
};

static bool ir_lower_benchmark_seed(const char* text, uint64_t* output)
{
    uint64_t value = 0;
    bool valid = text[0] != 0;
    for (uint32_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint8_t)text[i] - (uint32_t)'0';
        valid = digit <= 9 && value <= UINT64_MAX / 10 && (value != UINT64_MAX / 10 || digit <= UINT64_MAX % 10);
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    if (valid)
    {
        *output = value;
    }
    return valid;
}

static bool ir_lower_benchmark_generate(IrLowerBenchmarkCorpus* corpus, const Tier0Tables* tables, Arena* input, uint64_t seed, bool larger, bool objects, bool dynamic)
{
    const Tier0ProgramOptions options = {
        .functions = larger ? 8 : 4,
        .statements = larger ? 40 : 6,
        .expression_nodes = larger ? 30 : 6,
        .calls = true,
        .loops = true,
        .floating = !dynamic,
        .pointers = true,
        .defined = true,
        .objects = objects && !dynamic,
        .vlas = dynamic,
    };
    const uint32_t programs = larger ? BUSTER_IR_LOWER_BENCHMARK_PROGRAMS : 1;
    const uint32_t capacity = programs * options.functions;
    corpus->functions = BUSTER_ARENA_PUSH_ARRAY(input, IrLowerBenchmarkFunction, capacity);
    bool valid = corpus->functions != 0;
    for (uint32_t p = 0; valid && p < programs; p += 1)
    {
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(input, Tier0Program, 1);
        Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(input, Tier0Functions, 1);
        valid = program != 0 && functions != 0;
        if (valid)
        {
            valid = tier0_generate_program(program, input, seed + p, options, "ir_bench_") && tier0_program_functions(program, tables, input, functions);
        }
        for (uint32_t f = 0; valid && f < functions->count; f += 1)
        {
            valid = corpus->count < capacity;
            if (valid)
            {
                const IrLowerBenchmarkFunction item = {
                    .input = &functions->functions[f],
                    .result_type = (IrType)ir_typed_types[program->functions[f].result_class],
                    .symbol_count = program->global_count + program->function_count + 1,
                    .seed = seed + p,
                    .index = f,
                };
                corpus->functions[corpus->count] = item;
                corpus->count += 1;
                corpus->entries += item.input->count;
                for (uint32_t i = 0; i < item.input->count; i += 1)
                {
                    corpus->copies += item.input->kinds[i] == BUSTER_TYPED_COPY;
                    corpus->stack_ops += tier0_moves_stack(item.input->kinds[i]);
                }
                for (uint32_t l = 0; l < item.input->local_count; l += 1)
                {
                    corpus->objects += tier0_is_object(item.input, l);
                }
            }
        }
    }
    valid = valid && corpus->count != 0 && corpus->entries != 0;
    return valid;
}

// Isolate each stack operation's complete lowering cost. Restore repeats use one captured position kept below DUP,
// so 512 restores need only one save. No source, execution or native-code latency is inferred from these streams.
static bool ir_lower_benchmark_stack(IrLowerBenchmarkCorpus* corpus, Arena* input, uint8_t kind)
{
    const uint32_t operations = 512;
    const uint32_t count = kind == BUSTER_TYPED_ALLOCA ? operations * 3 + 1 : operations * 2 + (kind == BUSTER_TYPED_STACK_RESTORE ? 3 : 1);
    Tier0Function* function = BUSTER_ARENA_PUSH_ARRAY(input, Tier0Function, 1);
    IrLowerBenchmarkFunction* item = BUSTER_ARENA_PUSH_ARRAY(input, IrLowerBenchmarkFunction, 1);
    uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(input, uint8_t, count);
    uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(input, uint8_t, count);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(input, uint64_t, count);
    const bool valid = function != 0 && item != 0 && kinds != 0 && classes != 0 && values != 0;
    if (valid)
    {
        memset(classes, BUSTER_TYPED_CLASS_U64, count);
        memset(values, 0, (uint64_t)count * sizeof(uint64_t));
        uint32_t next = 0;
        if (kind == BUSTER_TYPED_STACK_RESTORE)
        {
            kinds[next++] = BUSTER_TYPED_STACK_SAVE;
        }
        for (uint32_t i = 0; i < operations; i += 1)
        {
            if (kind == BUSTER_TYPED_ALLOCA)
            {
                kinds[next] = BUSTER_TYPED_CONST;
                values[next++] = 16;
                kinds[next++] = BUSTER_TYPED_ALLOCA;
                kinds[next++] = BUSTER_TYPED_DISCARD;
            }
            else if (kind == BUSTER_TYPED_STACK_SAVE)
            {
                kinds[next++] = BUSTER_TYPED_STACK_SAVE;
                kinds[next++] = BUSTER_TYPED_DISCARD;
            }
            else
            {
                kinds[next++] = BUSTER_TYPED_DUP;
                kinds[next++] = BUSTER_TYPED_STACK_RESTORE;
            }
        }
        if (kind == BUSTER_TYPED_STACK_RESTORE)
        {
            kinds[next++] = BUSTER_TYPED_DISCARD;
        }
        kinds[next] = BUSTER_TYPED_RETURN;
        classes[next] = BUSTER_TYPED_CLASS_VOID;
        *function = (Tier0Function){
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = count,
        };
        *item = (IrLowerBenchmarkFunction){
            .input = function,
            .result_type = IR_VOID,
            .symbol_count = 1,
        };
        corpus->functions = item;
        corpus->count = 1;
        corpus->entries = count;
        corpus->stack_ops = operations + (kind == BUSTER_TYPED_STACK_RESTORE);
        corpus->target_ops = operations;
    }
    return valid;
}

static bool ir_lower_benchmark_validate(IrLowerBenchmarkCorpus* corpus, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    bool valid = true;
    for (uint32_t f = 0; valid && f < corpus->count; f += 1)
    {
        const IrLowerBenchmarkFunction item = corpus->functions[f];
        IrFunction function = { 0 };
        const IrCheck check = ir_lower_typed(item.input, item.result_type, item.symbol_count, output, scratch, &function);
        valid = check.error == IR_OK && output->length - output_mark == function.arena_bytes && scratch->length == scratch_mark;
        if (valid)
        {
            corpus->instructions += function.instruction_count;
            corpus->core_bytes += function.arena_bytes;
            if (function.arena_bytes > corpus->peak_output_bytes)
            {
                corpus->peak_output_bytes = function.arena_bytes;
            }
        }
        else
        {
            fprintf(stderr, "benchmark-ir-lowering: preflight failed at seed %" PRIu64 ", function %u: error %u at %u\n", item.seed, item.index, check.error, check.instruction);
        }
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    return valid;
}

// Probe after recording this shape's samples so the capacity search cannot warm them up. Identical-harness A/B
// measurements also keep the work between shapes equal. It includes temporary verifier storage.
static bool ir_lower_benchmark_scratch(IrLowerBenchmarkCorpus* corpus, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    bool valid = true;
    for (uint32_t f = 0; valid && f < corpus->count; f += 1)
    {
        const IrLowerBenchmarkFunction item = corpus->functions[f];
        const uint64_t capacity = scratch->capacity;
        uint64_t low = scratch_mark;
        uint64_t high = capacity;
        while (low < high && valid)
        {
            const uint64_t middle = low + (high - low) / 2;
            scratch->capacity = middle;
            IrFunction probe = { 0 };
            const IrCheck check = ir_lower_typed(item.input, item.result_type, item.symbol_count, output, scratch, &probe);
            valid = check.error == IR_OK || check.error == IR_ERROR_MEMORY;
            if (check.error == IR_OK)
            {
                high = middle;
            }
            else
            {
                low = middle + 1;
            }
            arena_pop_to(output, output_mark);
        }
        const uint64_t bytes = high - scratch_mark;
        corpus->scratch_bytes = bytes > corpus->scratch_bytes ? bytes : corpus->scratch_bytes;
        valid = valid && scratch->length == scratch_mark;
        scratch->capacity = capacity;
    }
    return valid;
}

static __attribute__((noinline)) IrError ir_lower_benchmark_run(const IrLowerBenchmarkCorpus* corpus, uint32_t repetitions, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    IrError error = IR_OK;
    uint64_t observable = 0;
    for (uint32_t repeat = 0; error == IR_OK && repeat < repetitions; repeat += 1)
    {
        for (uint32_t f = 0; error == IR_OK && f < corpus->count; f += 1)
        {
            const IrLowerBenchmarkFunction item = corpus->functions[f];
            IrFunction function = { 0 };
            __asm__ volatile("" : : "r"(item.input) : "memory");
            const IrCheck check = ir_lower_typed(item.input, item.result_type, item.symbol_count, output, scratch, &function);
            error = check.error;
            // Keep the published columns observable without adding a second traversal to the measured pipeline.
            __asm__ volatile("" : : "r"(&function) : "memory");
            observable += function.instruction_count;
            arena_pop_to(output, output_mark);
        }
    }
    ir_benchmark_sink += observable;
    return error;
}

static IrBenchmarkSample ir_lower_benchmark_sample(const IrLowerBenchmarkCorpus* corpus, Arena* output, Arena* scratch, uint32_t* repetitions)
{
    IrBenchmarkSample sample = { 0 };
    bool complete = false;
    while (!complete && sample.error == IR_OK)
    {
        const uint64_t start_ns = ir_benchmark_now();
        const uint64_t start_tsc = ir_benchmark_tsc();
        sample.error = ir_lower_benchmark_run(corpus, *repetitions, output, scratch);
        sample.tsc_ticks = ir_benchmark_tsc() - start_tsc;
        sample.nanoseconds = ir_benchmark_now() - start_ns;
        sample.repetitions = *repetitions;
        complete = sample.nanoseconds >= BUSTER_IR_BENCHMARK_MIN_NS;
        if (!complete)
        {
            if (*repetitions == BUSTER_IR_BENCHMARK_MAX_REPETITIONS)
            {
                sample.error = IR_ERROR_LIMIT;
            }
            else
            {
                *repetitions *= 2;
            }
        }
    }
    return sample;
}

static bool ir_lower_benchmark_case(const Tier0Tables* tables, uint64_t seed, bool larger, bool objects, bool dynamic, uint8_t stack_kind, uint32_t passes)
{
    const char* name = stack_kind == BUSTER_TYPED_ALLOCA ? "alloca isolated" : stack_kind == BUSTER_TYPED_STACK_SAVE ? "stack-save isolated" : stack_kind == BUSTER_TYPED_STACK_RESTORE ? "stack-restore isolated" : dynamic ? "larger dynamic stack" : objects ? "larger object" : larger ? "larger mixed" : "small mixed";
    Arena input = arena_reserve(BUSTER_IR_LOWER_BENCHMARK_INPUT_BYTES);
    Arena output = arena_reserve(BUSTER_IR_LOWER_BENCHMARK_OUTPUT_BYTES);
    Arena scratch = arena_reserve(BUSTER_IR_LOWER_BENCHMARK_OUTPUT_BYTES);
    IrLowerBenchmarkCorpus corpus = { 0 };
    bool valid = input.base != 0 && output.base != 0 && scratch.base != 0;
    valid = valid && (stack_kind != 0 ? ir_lower_benchmark_stack(&corpus, &input, stack_kind) : ir_lower_benchmark_generate(&corpus, tables, &input, seed, larger, objects, dynamic));
    valid = valid && ir_lower_benchmark_validate(&corpus, &output, &scratch);
    uint32_t repetitions = 1;
    double nanoseconds[BUSTER_IR_BENCHMARK_MAX_PASSES];
    double ticks[BUSTER_IR_BENCHMARK_MAX_PASSES];
    valid = valid && ir_lower_benchmark_run(&corpus, 1, &output, &scratch) == IR_OK;
    if (valid)
    {
        const IrBenchmarkSample calibration = ir_lower_benchmark_sample(&corpus, &output, &scratch, &repetitions);
        valid = calibration.error == IR_OK;
    }
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        const IrBenchmarkSample sample = ir_lower_benchmark_sample(&corpus, &output, &scratch, &repetitions);
        valid = sample.error == IR_OK;
        if (valid)
        {
            nanoseconds[pass] = (double)sample.nanoseconds / sample.repetitions;
            ticks[pass] = (double)sample.tsc_ticks / sample.repetitions;
        }
    }
    valid = valid && ir_lower_benchmark_scratch(&corpus, &output, &scratch);
    if (valid)
    {
        printf("\n%s programs: %u functions, %" PRIu64 " typed entries, %" PRIu64 " IR instructions (%.3f/typed entry), %" PRIu64 " objects, %" PRIu64 " copies\n", name, corpus.count, corpus.entries, corpus.instructions,
               (double)corpus.instructions / corpus.entries, corpus.objects, corpus.copies);
        printf("core arena bytes: sum=%" PRIu64 " (%.3f/typed entry), peak/function=%" PRIu64 "\n", corpus.core_bytes, (double)corpus.core_bytes / corpus.entries, corpus.peak_output_bytes);
        printf("minimum scratch capacity, peak/function: %" PRIu64 "; dynamic stack operations=%" PRIu64 "\n", corpus.scratch_bytes, corpus.stack_ops);
        printf("%-8s %14s %16s %18s %20s %12s\n", "path", "ns/corpus", "ns/typed entry", "M typed entries/s", "TSC ticks/entry", "repetitions");
        const double ns = ir_benchmark_median(nanoseconds, passes);
        const double tsc = ir_benchmark_median(ticks, passes);
        printf("%-8s %14.1f %16.3f %18.3f %20.3f %12u\n", "lower", ns, ns / corpus.entries, (double)corpus.entries * 1000.0 / ns, tsc / corpus.entries, repetitions);
        if (corpus.stack_ops != 0)
        {
            printf("  %.3f ns/stack operation over the full lowering pipeline\n", ns / corpus.stack_ops);
        }
        if (corpus.target_ops != 0)
        {
            printf("  %.3f ns/target operation, %" PRIu64 " targets; %u helper saves (validation, planning, emission and verification included)\n", ns / corpus.target_ops, corpus.target_ops, stack_kind == BUSTER_TYPED_STACK_RESTORE ? 1u : 0u);
        }
    }
    else
    {
        fprintf(stderr, "benchmark-ir-lowering: generation, validation, lowering or calibration failed for the %s case\n", name);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
    return valid;
}

static int run_ir_lower_benchmark(int argc, char** argv)
{
    uint32_t passes = BUSTER_IR_BENCHMARK_PASSES;
    uint64_t seed = 1;
    bool ordinary = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--ordinary") == 0)
        {
            ordinary = true;
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = ir_benchmark_number(argv[i], 1, BUSTER_IR_BENCHMARK_MAX_PASSES, &passes);
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = ir_lower_benchmark_seed(argv[i], &seed);
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-ir-lowering [--passes N] [--seed N] [--ordinary]\n");
        fprintf(stderr, "  passes: 1..%u; seed: decimal u64\n", BUSTER_IR_BENCHMARK_MAX_PASSES);
    }
#ifndef __OPTIMIZE__
    fprintf(stderr, "benchmark-ir-lowering: an optimized build is required; unoptimized timings are not reported\n");
    valid = false;
#endif
    Tier0Tables tables = { 0 };
    if (valid)
    {
        valid = tier0_tables_open(&tables);
        if (!valid)
        {
            fprintf(stderr, "benchmark-ir-lowering: type/ABI tables could not be allocated\n");
        }
    }
    if (valid)
    {
        printf("benchmark-ir-lowering: generated mixed typed programs, seed=%" PRIu64 ", %u passes; each sample >= 2 ms\n", seed, passes);
        printf("Timed: typed validation + planning + classification + count/emission + IR verification; generator/ABI preparation excluded.\n");
        printf("Counts are typed entries, not source tokens. TSC ticks are not core cycles; pin to one CPU for measurements.\n");
        valid = ir_lower_benchmark_case(&tables, seed, false, false, false, 0, passes);
        if (valid)
        {
            valid = ir_lower_benchmark_case(&tables, seed, true, false, false, 0, passes);
        }
        if (valid)
        {
            valid = ir_lower_benchmark_case(&tables, seed, true, true, false, 0, passes);
        }
        if (valid && !ordinary)
        {
            valid = ir_lower_benchmark_case(&tables, seed, true, true, true, 0, passes);
        }
        for (uint8_t kind = BUSTER_TYPED_ALLOCA; valid && !ordinary && kind <= BUSTER_TYPED_STACK_RESTORE; kind += 1)
        {
            valid = ir_lower_benchmark_case(&tables, seed, false, false, true, kind, passes);
        }
    }
    tier0_tables_close(&tables);
    return valid ? 0 : 1;
}
