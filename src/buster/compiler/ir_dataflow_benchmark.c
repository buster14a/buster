// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Optional real-function measurements for test-ir-source --dataflow-benchmark. Include after ir_dataflow_test.c and
// tier0_benchmark_measure.c. The caller supplies already lowered/promoted functions and owns the hardware counters.
// No frontend, lowering, SSA construction, text printing or oracle runs inside a sample. The two measured operations
// are solve alone (including phi live-out restoration) and prepare+solve (including IR verification and allocation).
// Both paths get the same repetitions per operation. Twenty-one passes rotate the four variants' order. Counters are
// hardware CPU cycles, not timestamp ticks; a missing or unreadable cycle counter fails, never reports zero speed.

#define BUSTER_IR_DATAFLOW_BENCHMARK_VARIANTS 4

typedef struct IrDataflowSample IrDataflowSample;
struct IrDataflowSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t iterations;
    bool valid;
};

static IrDataflowSample ir_dataflow_sample(const IrFunction* function, IrDataflow* prepared, uint32_t variant, uint32_t repeats,
                                          Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    const bool scalar = (variant & 1) != 0;
    const bool build = variant >= 2;
    uint64_t iterations = 0;
    bool valid = true;
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    for (uint32_t i = 0; valid && i < repeats; i += 1)
    {
        IrDataflow made = { 0 };
        IrDataflow* flow = build ? &made : prepared;
        if (build)
        {
            const IrCheck check = scalar ? ir_dataflow_prepare_scalar(function, output, scratch, flow) : ir_dataflow_prepare(function, output, scratch, flow);
            valid = check.error == IR_OK;
        }
        if (valid)
        {
            iterations += scalar ? ir_dataflow_solve_scalar(flow) : ir_dataflow_solve(flow);
            // Make each repetition's matrix writes observable to the compiler without timing a digest or oracle.
            __asm__ volatile("" : : "r"(flow->flow.in), "r"(flow->flow.out) : "memory");
        }
        if (build)
        {
            arena_pop_to(output, mark);
        }
    }
    const Tier0BenchmarkReading end = tier0_benchmark_read(counters);
    // The shared counter reader returns zero on read failure. Preparation has already executed before sampling,
    // so neither opening a readable counter nor a failed read can masquerade as a valid zero starting endpoint.
    valid = valid && output->length == mark && scratch->length == temporary && begin.cycles != 0 && begin.instructions != 0 &&
        end.cycles > begin.cycles && end.instructions > begin.instructions && end.nanoseconds > begin.nanoseconds;
    const IrDataflowSample sample = {
        .nanoseconds = valid ? end.nanoseconds - begin.nanoseconds : 0,
        .cycles = valid ? end.cycles - begin.cycles : 0,
        .instructions = valid ? end.instructions - begin.instructions : 0,
        .iterations = iterations,
        .valid = valid,
    };
    return sample;
}

static bool ir_dataflow_benchmark_function(const IrFunction* function, const char* file, const char* name, const char* form,
                                          Arena* output, Arena* scratch, Tier0BenchmarkCounters counters)
{
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    IrDataflow prepared[2] = { 0 };
    bool valid = counters.cycles >= 0 &&
        ir_dataflow_prepare(function, output, scratch, &prepared[0]).error == IR_OK &&
        ir_dataflow_prepare_scalar(function, output, scratch, &prepared[1]).error == IR_OK;
    uint64_t iterations[2] = { 0 };
    if (valid)
    {
        iterations[0] = ir_dataflow_solve(&prepared[0]);
        iterations[1] = ir_dataflow_solve_scalar(&prepared[1]);
        valid = iterations[0] == iterations[1] && ir_dataflow_test_equal(&prepared[0], &prepared[1]);
    }
    uint32_t repeats[2] = { 1, 1 };
    // Untimed warm-up and wall-time calibration. Target roughly 250 us per batch, cap tiny-function repetition, and
    // use the slower path's estimate so very large functions are never repeated thousands of times.
    for (uint32_t phase = 0; valid && phase < 2; phase += 1)
    {
        uint64_t maximum = 1;
        for (uint32_t path = 0; valid && path < 2; path += 1)
        {
            const IrDataflowSample calibration = ir_dataflow_sample(function, &prepared[path], phase * 2 + path, 1, output, scratch, counters);
            valid = calibration.valid && calibration.iterations == iterations[path];
            maximum = calibration.nanoseconds > maximum ? calibration.nanoseconds : maximum;
        }
        uint64_t count = 250000 / maximum;
        count = count == 0 ? 1 : count > 4096 ? 4096 : count;
        repeats[phase] = (uint32_t)count;
    }
    IrDataflowSample samples[BUSTER_IR_DATAFLOW_BENCHMARK_VARIANTS][BUSTER_TIER0_BENCHMARK_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass < BUSTER_TIER0_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t place = 0; valid && place < BUSTER_IR_DATAFLOW_BENCHMARK_VARIANTS; place += 1)
        {
            const uint32_t variant = (pass + place) % BUSTER_IR_DATAFLOW_BENCHMARK_VARIANTS;
            const uint32_t path = variant & 1;
            const uint32_t count = repeats[variant / 2];
            const IrDataflowSample sample = ir_dataflow_sample(function, &prepared[path], variant, count, output, scratch, counters);
            samples[variant][pass] = sample;
            valid = sample.valid && sample.iterations == iterations[path] * count;
        }
    }
    valid = valid && ir_dataflow_test_equal(&prepared[0], &prepared[1]) && ir_dataflow_test_equations(function, &prepared[0], scratch);
    // Raw records are provisional until the entire command exits successfully and reports qualified=1. A later
    // refused/left-out source file makes the command fail: it is not silently a benchmark of a smaller corpus.
    for (uint32_t variant = 0; valid && variant < BUSTER_IR_DATAFLOW_BENCHMARK_VARIANTS; variant += 1)
    {
        const uint32_t count = repeats[variant / 2];
        const char* operation = variant < 2 ? "solve" : "prepare-solve";
        const char* path = (variant & 1) == 0 ? "vector" : "scalar";
        double cycles[BUSTER_TIER0_BENCHMARK_PASSES];
        double nanoseconds[BUSTER_TIER0_BENCHMARK_PASSES];
        for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES; pass += 1)
        {
            const IrDataflowSample sample = samples[variant][pass];
            cycles[pass] = (double)sample.cycles / count;
            nanoseconds[pass] = (double)sample.nanoseconds / count;
            printf("ir-dataflow-sample: file=%s function=%s form=%s operation=%s path=%s pass=%u repeats=%u cycles=%" PRIu64
                   " instructions=%" PRIu64 " ns=%" PRIu64 " iterations=%" PRIu64 "\n",
                   file, name, form, operation, path, pass, count, sample.cycles, sample.instructions, sample.nanoseconds, sample.iterations);
        }
        const double median = tier0_benchmark_median(cycles);
        const uint64_t block_iterations = (uint64_t)function->block_count * iterations[variant & 1];
        printf("ir-dataflow-benchmark: file=%s function=%s form=%s operation=%s path=%s vector_host=%u blocks=%u instructions=%u dense=%u phi_uses=%u"
               " row_words=%" PRIu64 " matrix_bytes=%" PRIu64 " retained_bytes=%" PRIu64 " iterations=%" PRIu64
               " cycles_per_function=%.3f cycles_per_block_iteration=%.3f ns_per_function=%.3f\n",
               file, name, form, operation, path, BUSTER_HOST_VECTOR, function->block_count, function->instruction_count, prepared[0].flow.value_count,
               prepared[0].phi_use_count, prepared[0].flow.row_words, (uint64_t)function->block_count * prepared[0].flow.row_words * 32,
               prepared[0].arena_bytes, iterations[variant & 1], median, block_iterations != 0 ? median / (double)block_iterations : 0.0,
               tier0_benchmark_median(nanoseconds));
    }
    if (!valid)
    {
        fprintf(stderr, "ir-dataflow-benchmark: file=%s function=%s form=%s failed (preparation, counter, arena, iteration or correctness check)\n", file, name, form);
    }
    arena_pop_to(output, mark);
    arena_pop_to(scratch, temporary);
    return valid;
}
