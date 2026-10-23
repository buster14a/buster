// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Whole public-stage costs for GVN, LICM and SROA. Include after their tests and ir_optimize_benchmark.c.
// Frozen synthetic fixtures are built and checked before timing. These are pass costs, not emitted-code gains.

typedef struct IrPassBenchmarkResult IrPassBenchmarkResult;
struct IrPassBenchmarkResult
{
    IrFunction function;
    uint64_t analysis_bytes;
    uint32_t changed;
};

static bool ir_pass_benchmark_once(const IrFunction* input, Arena* output, Arena* scratch, uint32_t pass, bool scalar, IrPassBenchmarkResult* result)
{
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    IrPassBenchmarkResult candidate = { 0 };
    if (pass == 0)
    {
        IrGvnOptions options = ir_gvn_options();
        options.scalar = scalar;
        IrGvnResult run = { 0 };
        check = ir_optimize_gvn(input, output, scratch, options, &run);
        candidate.function = run.function;
        candidate.analysis_bytes = run.scratch_bytes;
        candidate.changed = run.removed;
    }
    else if (pass == 1)
    {
        IrLicmOptions options = ir_licm_options();
        options.scalar = scalar;
        IrLicmResult run = { 0 };
        check = ir_optimize_licm(input, output, scratch, options, &run);
        candidate.function = run.function;
        candidate.analysis_bytes = run.scratch_bytes;
        candidate.changed = run.moved;
    }
    else if (pass == 2)
    {
        IrSroaOptions options = ir_sroa_options();
        options.scalar = scalar;
        IrSroaResult run = { 0 };
        check = ir_optimize_sroa(input, output, scratch, options, &run);
        candidate.function = run.function;
        candidate.analysis_bytes = run.scratch_bytes;
        candidate.changed = run.fields;
    }
    const bool valid = check.error == IR_OK;
    if (valid)
    {
        *result = candidate;
    }
    return valid;
}

static __attribute__((noinline)) bool ir_pass_benchmark_repeat(const IrFunction* input, Arena* output, Arena* scratch, uint32_t pass, bool scalar, uint32_t repeats)
{
    const uint64_t mark = output->length;
    bool valid = true;
    for (uint32_t repetition = 0; valid && repetition < repeats; repetition += 1)
    {
        IrPassBenchmarkResult result = { 0 };
        valid = ir_pass_benchmark_once(input, output, scratch, pass, scalar, &result);
        if (valid)
        {
            ir_optimize_benchmark_sink += result.function.instruction_count + result.changed;
            __asm__ volatile("" : : "r"(result.function.op) : "memory");
        }
        arena_pop_to(output, mark);
    }
    return valid;
}

static bool ir_pass_benchmark_fields(Arena* arena, IrFunction* f, uint32_t fields)
{
    const IrSizes sizes = { .instruction_count = (uint64_t)fields * 4 + 4, .block_count = 1, .constant_count = 1, .parameter_count = 1, .slot_count = 1, .address_count = fields };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->block_offsets[1] = f->instruction_count;
        f->slots[0] = (IrSlot){ .size = (uint64_t)fields * 8, .alignment = 8, .type = IR_VOID };
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t field = 0; field < fields; field += 1)
        {
            const uint32_t i = 3 + field * 4;
            f->addresses[field] = (IrAddress){ .offset = (int64_t)field * 8, .scale = 1 };
            ir_optimize_test_instruction(f, i, IR_ADDRESS, IR_PTR, 2, BUSTER_IR_NONE, field);
            ir_optimize_test_instruction(f, i + 1, IR_STORE, IR_VOID, i, 0, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, i + 2, IR_LOAD, IR_I64, i, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, i + 3, IR_ADD, IR_I64, field == 0 ? 1 : i - 1, i + 2, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(f, f->instruction_count - 1, IR_RET, IR_VOID, f->instruction_count - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static bool ir_pass_benchmark_fixture(Arena* arena, IrFunction* f, IrOptimizeTest* generator, uint32_t pass, uint32_t shape, uint32_t length, uint32_t fields)
{
    bool valid = false;
    if (pass == 0)
    {
        valid = shape == 2 ? ir_gvn_test_cycle(arena, f) : ir_optimize_test_random_dag(arena, f, generator, shape == 0 ? 32 : length);
        for (uint32_t i = 4; valid && shape < 2 && i + 1 < f->instruction_count; i += 4)
        {
            ir_optimize_test_instruction(f, i, f->op[i - 1], f->type[i - 1], f->a[i - 1], f->b[i - 1], f->c[i - 1]);
        }
    }
    else if (pass == 1)
    {
        valid = shape == 0 ? ir_optimize_test_random_dag(arena, f, generator, 32) : shape == 1 ? ir_licm_test_nested(arena, f) : ir_licm_test_chain(arena, f, generator, length);
    }
    else
    {
        valid = shape == 0 ? ir_sroa_test_fields(arena, f, 0) : shape == 1 ? ir_sroa_test_cfg(arena, f, false) : ir_pass_benchmark_fields(arena, f, fields);
    }
    return valid;
}

static bool ir_pass_benchmark_check(const IrFunction* input, const IrPassBenchmarkResult* reference, uint32_t pass, uint32_t shape, uint32_t fields, Arena* output, Arena* scratch)
{
    bool valid = pass == 0 ? reference[1].changed >= reference[0].changed : ir_optimize_test_equal(&reference[0].function, &reference[1].function);
    if (pass == 2)
    {
        const IrFunction* a = &reference[0].function;
        const IrFunction* b = &reference[1].function;
        valid = valid && a->slot_count == b->slot_count && (a->slot_count == 0 || memcmp(a->slots, b->slots, (uint64_t)a->slot_count * sizeof(IrSlot)) == 0);
    }
    for (uint32_t trial = 0; valid && trial < 9; trial += 1)
    {
        const uint64_t parameter = trial == 8 ? UINT64_MAX : trial;
        if (pass == 2 && shape == 2)
        {
            for (uint32_t path = 0; valid && path < 2; path += 1)
            {
                const uint64_t mark = output->length;
                IrFunction promoted = { 0 };
                const IrCheck check = path == 0 ? ssa_ir_promote_scalar(&reference[path].function, output, scratch, &promoted) : ssa_ir_promote(&reference[path].function, output, scratch, &promoted);
                valid = check.error == IR_OK;
                if (valid)
                {
                    const IrOptimizeExecution result = ir_optimize_test_execute(&promoted, scratch, parameter, 0);
                    valid = result.status == 1 && result.value == parameter * fields;
                }
                arena_pop_to(output, mark);
            }
        }
        else
        {
            const IrOptimizeExecution before = ir_optimize_test_execute(input, scratch, parameter, 0);
            for (uint32_t path = 0; valid && path < 2; path += 1)
            {
                const IrOptimizeExecution after = ir_optimize_test_execute(&reference[path].function, scratch, parameter, 0);
                valid = before.status != 4 && before.status == after.status && before.value == after.value && (pass == 2 || before.effects == after.effects);
            }
        }
    }
    return valid;
}

static __attribute__((unused)) int run_ir_optimize_passes_benchmark(int argc, char** argv)
{
    uint32_t length = 4096;
    uint32_t fields = 64;
    uint32_t samples = BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES;
    uint32_t after_scalar = 0;
    int selected = -1;
    int only = -1;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--instructions") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], 1000000, &length) && length != 0;
        }
        else if (strcmp(argv[i], "--fields") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], 4096, &fields) && fields != 0;
        }
        else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES, &samples) && samples != 0;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], 1000, &after_scalar);
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : -1;
            valid = only >= 0;
        }
        else if (strcmp(argv[i], "--pass") == 0 && i + 1 < argc)
        {
            i += 1;
            selected = strcmp(argv[i], "gvn") == 0 ? 0 : strcmp(argv[i], "licm") == 0 ? 1 : strcmp(argv[i], "sroa") == 0 ? 2 : -1;
            valid = selected >= 0 || strcmp(argv[i], "all") == 0;
        }
        else
        {
            valid = false;
        }
    }
    Arena input_arena = arena_reserve(128ull << 20);
    Arena outputs[2] = { arena_reserve(128ull << 20), arena_reserve(128ull << 20) };
    Arena promoted = arena_reserve(128ull << 20);
    Arena scratch = arena_reserve(512ull << 20);
    valid = valid && input_arena.base != 0 && outputs[0].base != 0 && outputs[1].base != 0 && promoted.base != 0 && scratch.base != 0;
    const char* names[] = { "gvn", "licm", "sroa" };
    const char* shapes[][3] = { { "small-dag", "large-dag", "cyclic-phi" }, { "no-loop", "nested-loops", "invariant-chain" }, { "two-fields", "conditional-stores", "field-array" } };
    for (uint32_t pass = 0; valid && pass < 3; pass += 1)
    {
        for (uint32_t shape = 0; valid && (selected < 0 || selected == (int)pass) && shape < 3; shape += 1)
        {
            IrFunction input = { 0 };
            IrOptimizeTest generator = { .random = 139 + pass * 3 + shape };
            valid = ir_pass_benchmark_fixture(&input_arena, &input, &generator, pass, shape, length, fields);
            IrPassBenchmarkResult reference[2] = { 0 };
            for (uint32_t path = 0; valid && path < 2; path += 1)
            {
                valid = ir_pass_benchmark_once(&input, &outputs[path], &scratch, pass, path == 0, &reference[path]);
            }
            valid = valid && ir_pass_benchmark_check(&input, reference, pass, shape, fields, &promoted, &scratch);
            if (valid)
            {
                printf("ir-pass %s %s input=%u scalar-output=%u vector-output=%u scalar-changed=%u vector-changed=%u scalar-output-bytes=%" PRIu64 " vector-output-bytes=%" PRIu64 " scalar-analysis-bytes=%" PRIu64 " vector-analysis-bytes=%" PRIu64 "\n", names[pass], shapes[pass][shape], input.instruction_count, reference[0].function.instruction_count, reference[1].function.instruction_count, reference[0].changed, reference[1].changed, reference[0].function.arena_bytes, reference[1].function.arena_bytes, reference[0].analysis_bytes, reference[1].analysis_bytes);
            }
            arena_pop_to(&outputs[0], 0);
            arena_pop_to(&outputs[1], 0);
            uint32_t repetitions[2] = { 1, 1 };
            double times[2][BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES] = { 0 };
            double ticks[2][BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES] = { 0 };
            for (uint32_t path = 0; valid && path < 2; path += 1)
            {
                if (only < 0 || only == (int)path)
                {
                    uint64_t elapsed = 0;
                    while (valid && elapsed < BUSTER_IR_OPTIMIZE_BENCHMARK_MIN_NS && repetitions[path] < (1u << 20))
                    {
                        const uint64_t start = ir_optimize_benchmark_time();
                        valid = ir_pass_benchmark_repeat(&input, &outputs[path], &scratch, pass, path == 0, repetitions[path]);
                        elapsed = ir_optimize_benchmark_time() - start;
                        if (elapsed < BUSTER_IR_OPTIMIZE_BENCHMARK_MIN_NS)
                        {
                            repetitions[path] *= 2;
                        }
                    }
                }
            }
            for (uint32_t sample = 0; valid && sample < samples; sample += 1)
            {
                for (uint32_t order = 0; valid && order < 2; order += 1)
                {
                    const uint32_t path = (order + sample) & 1;
                    if (only < 0 || only == (int)path)
                    {
                        if (path == 1 && after_scalar != 0)
                        {
                            const uint64_t until = ir_optimize_benchmark_time() + (uint64_t)after_scalar * 1000000;
                            while (valid && ir_optimize_benchmark_time() < until)
                            {
                                valid = ir_pass_benchmark_repeat(&input, &outputs[0], &scratch, pass, true, 1);
                            }
                        }
                        const uint64_t start = ir_optimize_benchmark_time();
                        const uint64_t start_ticks = ir_optimize_benchmark_ticks();
                        valid = ir_pass_benchmark_repeat(&input, &outputs[path], &scratch, pass, path == 0, repetitions[path]);
                        const uint64_t elapsed_ticks = ir_optimize_benchmark_ticks() - start_ticks;
                        const uint64_t elapsed = ir_optimize_benchmark_time() - start;
                        times[path][sample] = (double)elapsed / repetitions[path];
                        ticks[path][sample] = (double)elapsed_ticks / repetitions[path];
                    }
                }
            }
            for (uint32_t path = 0; valid && path < 2; path += 1)
            {
                if (only < 0 || only == (int)path)
                {
                    double minimum = times[path][0];
                    double maximum = minimum;
                    for (uint32_t sample = 1; sample < samples; sample += 1)
                    {
                        minimum = times[path][sample] < minimum ? times[path][sample] : minimum;
                        maximum = times[path][sample] > maximum ? times[path][sample] : maximum;
                    }
                    const double median = ir_optimize_benchmark_median(times[path], samples);
                    printf("ir-pass %s %s %s samples=%u repeats=%u ns=%.2f min=%.2f max=%.2f tsc-ticks=%.2f instructions-per-second=%.2f\n", names[pass], shapes[pass][shape], path == 0 ? "scalar" : "vector", samples, repetitions[path], median, minimum, maximum, ir_optimize_benchmark_median(ticks[path], samples), median == 0 ? 0 : (double)input.instruction_count * 1000000000.0 / median);
                }
            }
            arena_pop_to(&input_arena, 0);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-ir-optimize-passes failed; usage: [--pass all|gvn|licm|sroa] [--instructions N] [--fields 1..4096] [--samples 1..21] [--only scalar|vector] [--after-scalar MS]\n");
    }
    arena_release(&scratch);
    arena_release(&promoted);
    arena_release(&outputs[1]);
    arena_release(&outputs[0]);
    arena_release(&input_arena);
    const int result = valid ? 0 : 1;
    return result;
}
