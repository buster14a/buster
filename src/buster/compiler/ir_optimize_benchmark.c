// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Included after ir_optimize_test.c to share original, independently executable IR fixtures. Timings cover the
// public optimizer call: validation, CFG/dominators/uses, analysis, allocation, rewrite and final SSA verification.
// The frozen input is prepared outside timing. These synthetic pass costs are not source-compiler or program gains.

#include <time.h>

#define BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES 21
#define BUSTER_IR_OPTIMIZE_BENCHMARK_MIN_NS 2000000ull

static volatile uint64_t ir_optimize_benchmark_sink;

static uint64_t ir_optimize_benchmark_time(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    const uint64_t result = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    return result;
}

static uint64_t ir_optimize_benchmark_ticks(void)
{
    _mm_lfence();
    const uint64_t result = __rdtsc();
    _mm_lfence();
    return result;
}

static __attribute__((noinline)) bool ir_optimize_benchmark_repeat(const IrFunction* input, Arena* output, Arena* scratch, IrOptimizeOptions options, uint32_t repeats)
{
    const uint64_t mark = output->length;
    bool valid = true;
    for (uint32_t repetition = 0; valid && repetition < repeats; repetition += 1)
    {
        IrOptimizeResult result = { 0 };
        valid = ir_optimize(input, output, scratch, options, &result).error == IR_OK;
        if (valid)
        {
            ir_optimize_benchmark_sink += result.function.instruction_count;
            __asm__ volatile("" : : "r"(result.function.op) : "memory");
        }
        arena_pop_to(output, mark);
    }
    return valid;
}

static double ir_optimize_benchmark_median(const double* samples, uint32_t count)
{
    double result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t lower = 0;
        for (uint32_t j = 0; j < count; j += 1)
        {
            lower += samples[j] < samples[i] || (samples[j] == samples[i] && j < i);
        }
        if (lower == count / 2)
        {
            result = samples[i];
        }
    }
    return result;
}

static bool ir_optimize_benchmark_number(const char* text, uint32_t maximum, uint32_t* number)
{
    uint64_t value = 0;
    bool valid = text[0] != 0;
    for (uint32_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint8_t)text[i] - (uint32_t)'0';
        valid = digit < 10 && value <= (UINT32_MAX - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    valid = valid && value <= maximum;
    if (valid)
    {
        *number = value;
    }
    return valid;
}

static __attribute__((unused)) int run_ir_optimize_benchmark(int argc, char** argv)
{
    uint32_t length = 4096;
    uint32_t blocks = 128;
    uint32_t after_scalar = 0;
    uint32_t samples = BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES;
    uint32_t passes = BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG;
    int only = -1;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--instructions") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], 1000000, &length) && length >= 1;
        }
        else if (strcmp(argv[i], "--blocks") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], 1000000, &blocks) && blocks >= 1;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], 1000, &after_scalar);
        }
        else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
        {
            valid = ir_optimize_benchmark_number(argv[++i], BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES, &samples) && samples != 0;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : strcmp(argv[i], "auto") == 0 ? 2 : -1;
            valid = only >= 0;
        }
        else if (strcmp(argv[i], "--pass") == 0 && i + 1 < argc)
        {
            i += 1;
            passes = strcmp(argv[i], "sccp") == 0 ? BUSTER_IR_OPTIMIZE_SCCP : strcmp(argv[i], "dce") == 0 ? BUSTER_IR_OPTIMIZE_DCE : strcmp(argv[i], "cfg") == 0 ? BUSTER_IR_OPTIMIZE_CFG : strcmp(argv[i], "all") == 0 ? 7 : 0;
            valid = passes != 0;
        }
        else
        {
            valid = false;
        }
    }
    Arena input_arena = arena_reserve(64ull << 20);
    Arena outputs[3] = {
        arena_reserve(128ull << 20),
        arena_reserve(128ull << 20),
        arena_reserve(128ull << 20),
    };
    Arena scratch = arena_reserve(256ull << 20);
    valid = valid && input_arena.base != 0 && outputs[0].base != 0 && outputs[1].base != 0 && outputs[2].base != 0 && scratch.base != 0;
    const char* names[] = {
        "small-dag",
        "large-dag",
        "phi-diamond",
        "loop",
        "jump-chain",
    };
    IrOptimizeTest generator = { .random = 139 };
    for (uint32_t shape = 0; valid && shape < 5; shape += 1)
    {
        IrFunction input = { 0 };
        if (shape < 2)
        {
            valid = ir_optimize_test_random_dag(&input_arena, &input, &generator, shape == 0 ? 32 : length);
        }
        else if (shape == 2)
        {
            valid = ir_optimize_test_diamond(&input_arena, &input, 2);
        }
        else if (shape == 3)
        {
            valid = ir_optimize_test_loop(&input_arena, &input);
        }
        else
        {
            valid = ir_optimize_test_jumps(&input_arena, &input, blocks, false);
        }
        IrOptimizeResult reference[3] = { 0 };
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = passes;
        for (uint32_t path = 0; valid && path < 3; path += 1)
        {
            options.path = path == 0 ? BUSTER_IR_OPTIMIZE_SCALAR : path == 1 ? BUSTER_IR_OPTIMIZE_VECTOR : BUSTER_IR_OPTIMIZE_AUTO;
            valid = ir_optimize(&input, &outputs[path], &scratch, options, &reference[path]).error == IR_OK;
        }
        valid = valid && ir_optimize_test_equal(&reference[0].function, &reference[1].function) && ir_optimize_test_equal(&reference[0].function, &reference[2].function);
        if (valid)
        {
            printf("ir-optimize %s input=%u output=%u blocks=%u/%u folded=%u output-bytes=%" PRIu64 " analysis-bytes=%" PRIu64 "\n", names[shape], input.instruction_count, reference[1].function.instruction_count, input.block_count, reference[1].function.block_count, reference[1].folded, reference[1].function.arena_bytes, reference[1].scratch_bytes);
        }
        arena_pop_to(&outputs[0], 0);
        arena_pop_to(&outputs[1], 0);
        arena_pop_to(&outputs[2], 0);
        uint32_t repetitions[3] = { 1, 1, 1 };
        double times[3][BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES] = { 0 };
        double ticks[3][BUSTER_IR_OPTIMIZE_BENCHMARK_SAMPLES] = { 0 };
        for (uint32_t path = 0; valid && path < 3; path += 1)
        {
            if (only < 0 || only == (int)path)
            {
                options.path = path == 0 ? BUSTER_IR_OPTIMIZE_SCALAR : path == 1 ? BUSTER_IR_OPTIMIZE_VECTOR : BUSTER_IR_OPTIMIZE_AUTO;
                uint64_t elapsed = 0;
                while (valid && elapsed < BUSTER_IR_OPTIMIZE_BENCHMARK_MIN_NS && repetitions[path] < (1u << 20))
                {
                    const uint64_t start = ir_optimize_benchmark_time();
                    valid = ir_optimize_benchmark_repeat(&input, &outputs[path], &scratch, options, repetitions[path]);
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
            for (uint32_t order = 0; valid && order < 3; order += 1)
            {
                // Each path takes a turn running first.
                const uint32_t path = (order + sample) % 3;
                if (only < 0 || only == (int)path)
                {
                    if (path != 0 && after_scalar != 0)
                    {
                        options.path = BUSTER_IR_OPTIMIZE_SCALAR;
                        const uint64_t until = ir_optimize_benchmark_time() + (uint64_t)after_scalar * 1000000;
                        while (valid && ir_optimize_benchmark_time() < until)
                        {
                            valid = ir_optimize_benchmark_repeat(&input, &outputs[0], &scratch, options, 1);
                        }
                    }
                    options.path = path == 0 ? BUSTER_IR_OPTIMIZE_SCALAR : path == 1 ? BUSTER_IR_OPTIMIZE_VECTOR : BUSTER_IR_OPTIMIZE_AUTO;
                    const uint64_t start = ir_optimize_benchmark_time();
                    const uint64_t start_ticks = ir_optimize_benchmark_ticks();
                    valid = ir_optimize_benchmark_repeat(&input, &outputs[path], &scratch, options, repetitions[path]);
                    const uint64_t elapsed_ticks = ir_optimize_benchmark_ticks() - start_ticks;
                    const uint64_t elapsed = ir_optimize_benchmark_time() - start;
                    times[path][sample] = (double)elapsed / repetitions[path];
                    ticks[path][sample] = (double)elapsed_ticks / repetitions[path];
                }
            }
        }
        for (uint32_t path = 0; valid && path < 3; path += 1)
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
                const double tsc = ir_optimize_benchmark_median(ticks[path], samples);
                const char* name = path == 0 ? "scalar" : path == 1 ? "vector" : "auto";
                printf("  %s ns/function=%.1f [%.1f,%.1f] ns/input-instruction=%.3f TSC-ticks/input-instruction=%.3f instructions/second=%.0f\n", name, median, minimum, maximum, median / input.instruction_count, tsc / input.instruction_count, (double)input.instruction_count * 1000000000.0 / median);
            }
        }
        arena_pop_to(&input_arena, 0);
    }
    arena_release(&scratch);
    arena_release(&outputs[2]);
    arena_release(&outputs[1]);
    arena_release(&outputs[0]);
    arena_release(&input_arena);
    if (!valid)
    {
        fprintf(stderr, "benchmark-ir-optimize failed: [--instructions N] [--blocks N] [--samples 1..21] [--only scalar|vector|auto] [--pass all|sccp|dce|cfg] [--after-scalar MS <= 1000]\n");
    }
    return valid ? 0 : 1;
}
