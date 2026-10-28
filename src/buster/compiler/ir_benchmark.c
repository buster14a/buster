// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic IR benchmark, included after ir.c. run_ir_benchmark measures the complete verifier/derived-array entry
// points on a small function and a larger integer/control-flow function. The producer, independent accounting checks,
// scalar/vector comparison and calibration are outside the recorded samples. Every recorded sample covers at least
// 2 ms; repeated rebuilds reuse arena checkpoints, including the allocation, initialization and fill the API performs.
// No source corpus or bytes/source-token claim is made. TSC ticks are reported as TSC ticks, not core clock cycles.
//
// Usage: benchmark-ir [--instructions N] [--passes N] [--only scalar|vector|uses|cfg] [--after-scalar MS]
// Pin the optimized executable to one CPU on the approved host under its shared benchmark lock for target results.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include "host_isa.h"

#define BUSTER_IR_BENCHMARK_INSTRUCTIONS 262144u
#define BUSTER_IR_BENCHMARK_MAX_INSTRUCTIONS (1u << 24)
#define BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS 32u
#define BUSTER_IR_BENCHMARK_PASSES 21u
#define BUSTER_IR_BENCHMARK_MAX_PASSES 101u
#define BUSTER_IR_BENCHMARK_MIN_NS 2000000ull
#define BUSTER_IR_BENCHMARK_MAX_REPETITIONS (1u << 20)
#define BUSTER_IR_BENCHMARK_MAX_AFTER_SCALAR_MS 10000u

typedef enum IrBenchmarkVariant
{
    IR_BENCHMARK_SCALAR,
    IR_BENCHMARK_VECTOR,
    IR_BENCHMARK_USES,
    IR_BENCHMARK_CFG,
    IR_BENCHMARK_VARIANT_COUNT,
} IrBenchmarkVariant;

static const char* const ir_benchmark_names[IR_BENCHMARK_VARIANT_COUNT] = {
    "scalar",
    "vector",
    "uses",
    "cfg",
};

typedef struct IrBenchmarkSample IrBenchmarkSample;
struct IrBenchmarkSample
{
    uint64_t nanoseconds;
    uint64_t tsc_ticks;
    uint32_t repetitions;
    IrError error;
};

// The accumulated public results remain observable; the empty assembly in ir_benchmark_run additionally makes the
// derived array stores observable without introducing a second timed pass over their bytes.
static volatile uint64_t ir_benchmark_sink;

static uint64_t ir_benchmark_now(void)
{
    struct timespec value = { 0 };
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * 1000000000ull + (uint64_t)value.tv_nsec;
}

static uint64_t ir_benchmark_tsc(void)
{
    _mm_lfence();
    const uint64_t value = __rdtsc();
    _mm_lfence();
    return value;
}

static bool ir_benchmark_number(const char* text, uint32_t minimum, uint32_t maximum, uint32_t* output)
{
    uint32_t value = 0;
    bool valid = text[0] != 0;
    for (uint32_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint8_t)text[i] - (uint32_t)'0';
        valid = digit <= 9 && value <= maximum / 10 && (value != maximum / 10 || digit <= maximum % 10);
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    valid = valid && value >= minimum;
    if (valid)
    {
        *output = value;
    }
    return valid;
}

static double ir_benchmark_median(const double* values, uint32_t count)
{
    double sorted[BUSTER_IR_BENCHMARK_MAX_PASSES];
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t position = i;
        while (position != 0 && sorted[position - 1] > values[i])
        {
            sorted[position] = sorted[position - 1];
            position -= 1;
        }
        sorted[position] = values[i];
    }
    const double result = count % 2 != 0 ? sorted[count / 2] : (sorted[count / 2 - 1] + sorted[count / 2]) * 0.5;
    return result;
}

// The only parameter dominates every later block. Each nonterminal row consumes the latest value and, for a binary
// row, parameter 0; branch instructions never accidentally become value operands. A short final block is intentional.
static IrError ir_benchmark_generate(Arena* memory, uint32_t count, IrFunction* function)
{
    const uint32_t blocks = (count + BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS - 1) / BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS;
    const IrSizes sizes = {
        .instruction_count = count,
        .block_count = blocks,
        .parameter_count = 1,
    };
    const IrError error = ir_create(memory, &sizes, function);
    if (error == IR_OK)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            function->block_offsets[block] = block * BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS;
        }
        function->block_offsets[blocks] = count;
        function->op[0] = IR_PARAM;
        function->type[0] = IR_I64;
        function->a[0] = 0;
        const uint8_t operations[8] = {
            IR_ADD,
            IR_XOR,
            IR_MUL,
            IR_SUB,
            IR_AND,
            IR_OR,
            IR_NEG,
            IR_COPY,
        };
        uint32_t previous = 0;
        for (uint32_t i = 1; i < count; i += 1)
        {
            const uint32_t block = i / BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS;
            if (i + 1 == count)
            {
                function->op[i] = IR_RET;
                function->a[i] = previous;
            }
            else if (i + 1 == function->block_offsets[block + 1])
            {
                function->op[i] = IR_BR;
                function->a[i] = block + 1;
            }
            else
            {
                const uint8_t op = operations[i % 8];
                function->op[i] = op;
                function->type[i] = IR_I64;
                function->a[i] = previous;
                if (op != IR_NEG && op != IR_COPY)
                {
                    function->b[i] = 0;
                }
                previous = i;
            }
        }
    }
    return error;
}

// These accounting checks are intentionally independent of the IR's operand and terminator tables. This synthetic
// producer uses only the following operations, so the expected compact graph and value-use multiset are exact.
static uint32_t ir_benchmark_operand_count(uint8_t op)
{
    uint32_t count = 2;
    if (op == IR_PARAM || op == IR_BR)
    {
        count = 0;
    }
    else if (op == IR_NEG || op == IR_COPY || op == IR_RET)
    {
        count = 1;
    }
    return count;
}

static bool ir_benchmark_validate(const IrFunction* function, Arena* derived, Arena* scratch, uint64_t* cfg_bytes, uint64_t* use_bytes)
{
    const uint64_t derived_mark = derived->length;
    const uint64_t scratch_mark = scratch->length;
    const IrCheck scalar = ir_verify_scalar(function, scratch, 0);
    const IrCheck vector = ir_verify(function, scratch, 0);
    bool valid = scalar.error == IR_OK && vector.error == scalar.error && vector.instruction == scalar.instruction && vector.detail == scalar.detail && vector.dominance_checked == scalar.dominance_checked;
    IrCfg cfg = { 0 };
    IrUses uses = { 0 };
    if (valid)
    {
        valid = ir_build_cfg(function, derived, scratch, &cfg) == IR_OK && ir_build_uses(function, derived, scratch, &uses) == IR_OK;
    }
    if (valid)
    {
        *cfg_bytes = cfg.arena_bytes;
        *use_bytes = uses.arena_bytes;
        valid = cfg.block_count == function->block_count && cfg.edge_count == function->block_count - 1 && uses.value_count == function->instruction_count;
    }
    for (uint32_t block = 0; valid && block < function->block_count; block += 1)
    {
        const uint32_t successor_count = block + 1 != function->block_count;
        const uint32_t predecessor_count = block != 0;
        valid = cfg.successor_offsets[block] == block && cfg.successor_offsets[block + 1] == block + successor_count;
        valid = valid && cfg.predecessor_offsets[block] == block - predecessor_count && cfg.predecessor_offsets[block + 1] == block;
        if (valid && successor_count != 0)
        {
            valid = cfg.successors[block] == block + 1;
        }
        if (valid && predecessor_count != 0)
        {
            valid = cfg.predecessors[block - 1] == block - 1;
        }
    }
    uint32_t* counts = 0;
    if (valid)
    {
        counts = arena_push_array(scratch, function->instruction_count, sizeof(uint32_t), 64);
        valid = counts != 0;
    }
    if (valid)
    {
        memset(counts, 0, (uint64_t)function->instruction_count * sizeof(uint32_t));
    }
    for (uint32_t i = 0; valid && i < function->instruction_count; i += 1)
    {
        valid = cfg.instruction_blocks[i] == i / BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS;
        const uint32_t count = ir_benchmark_operand_count(function->op[i]);
        if (count != 0)
        {
            counts[function->a[i]] += 1;
        }
        if (count == 2)
        {
            counts[function->b[i]] += 1;
        }
    }
    uint32_t prefix = 0;
    for (uint32_t value = 0; valid && value < function->instruction_count; value += 1)
    {
        valid = uses.offsets[value] == prefix;
        prefix += counts[value];
        valid = valid && uses.offsets[value + 1] == prefix && prefix <= uses.use_count;
        for (uint32_t p = uses.offsets[value]; valid && p < prefix; p += 1)
        {
            const IrUse use = uses.uses[p];
            valid = use.instruction < function->instruction_count;
            if (valid)
            {
                const uint32_t count = ir_benchmark_operand_count(function->op[use.instruction]);
                valid = use.operand < count && (use.operand == 0 ? function->a[use.instruction] : function->b[use.instruction]) == value;
            }
            if (valid && p != uses.offsets[value])
            {
                const IrUse previous = uses.uses[p - 1];
                valid = previous.instruction < use.instruction || (previous.instruction == use.instruction && previous.operand < use.operand);
            }
        }
    }
    valid = valid && prefix == uses.use_count;
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(derived, derived_mark);
    return valid;
}

static __attribute__((noinline)) IrError ir_benchmark_run(const IrFunction* function, IrBenchmarkVariant variant, uint32_t repetitions, Arena* derived, Arena* scratch)
{
    const uint64_t derived_mark = derived->length;
    IrError error = IR_OK;
    uint64_t observable = 0;
    for (uint32_t i = 0; error == IR_OK && i < repetitions; i += 1)
    {
        __asm__ volatile("" : : "r"(function) : "memory");
        if (variant == IR_BENCHMARK_SCALAR)
        {
            const IrCheck check = ir_verify_scalar(function, scratch, 0);
            error = check.error;
            observable += check.instruction;
        }
        else if (variant == IR_BENCHMARK_VECTOR)
        {
            const IrCheck check = ir_verify(function, scratch, 0);
            error = check.error;
            observable += check.instruction;
        }
        else if (variant == IR_BENCHMARK_USES)
        {
            IrUses uses = { 0 };
            error = ir_build_uses(function, derived, scratch, &uses);
            __asm__ volatile("" : : "r"(uses.offsets), "r"(uses.uses) : "memory");
            observable += uses.use_count;
        }
        else
        {
            IrCfg cfg = { 0 };
            error = ir_build_cfg(function, derived, scratch, &cfg);
            __asm__ volatile("" : : "r"(cfg.successor_offsets), "r"(cfg.successors), "r"(cfg.predecessor_offsets), "r"(cfg.predecessors), "r"(cfg.instruction_blocks) : "memory");
            observable += cfg.edge_count;
        }
        arena_pop_to(derived, derived_mark);
    }
    ir_benchmark_sink += observable;
    return error;
}

static IrError ir_benchmark_after_scalar(const IrFunction* function, Arena* derived, Arena* scratch, uint32_t milliseconds)
{
    const uint64_t duration = (uint64_t)milliseconds * 1000000ull;
    const uint64_t start = ir_benchmark_now();
    IrError error = IR_OK;
    while (error == IR_OK && ir_benchmark_now() - start < duration)
    {
        error = ir_benchmark_run(function, IR_BENCHMARK_SCALAR, 1, derived, scratch);
    }
    return error;
}

// Each attempt is bounded by the repetition cap. A recorded sample that becomes faster than calibration predicted
// is repeated with twice the work; that short attempt is discarded, including its after-scalar preparation.
static IrBenchmarkSample ir_benchmark_sample(const IrFunction* function, IrBenchmarkVariant variant, Arena* derived, Arena* scratch, uint32_t* repetitions, uint32_t after_scalar_ms)
{
    IrBenchmarkSample sample = { 0 };
    bool complete = false;
    while (!complete && sample.error == IR_OK)
    {
        if (variant == IR_BENCHMARK_VECTOR && after_scalar_ms != 0)
        {
            sample.error = ir_benchmark_after_scalar(function, derived, scratch, after_scalar_ms);
        }
        if (sample.error == IR_OK)
        {
            const uint64_t start_ns = ir_benchmark_now();
            const uint64_t start_tsc = ir_benchmark_tsc();
            sample.error = ir_benchmark_run(function, variant, *repetitions, derived, scratch);
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
    }
    return sample;
}

static bool ir_benchmark_case(uint32_t instructions, uint32_t passes, uint32_t selected, uint32_t after_scalar_ms)
{
    // Virtual reservations are bounds, not readable padding. Each operation still allocates its own exact columns.
    const uint64_t capacity = (uint64_t)instructions * 256 + (1u << 20);
    Arena memory = arena_reserve(capacity);
    Arena derived = arena_reserve(capacity);
    Arena scratch = arena_reserve(capacity);
    IrFunction function = { 0 };
    uint64_t cfg_bytes = 0;
    uint64_t use_bytes = 0;
    bool valid = ir_benchmark_generate(&memory, instructions, &function) == IR_OK;
    if (valid)
    {
        valid = ir_benchmark_validate(&function, &derived, &scratch, &cfg_bytes, &use_bytes);
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-ir: allocation or independent validation failed for %u instructions\n", instructions);
    }
    uint32_t repetitions[IR_BENCHMARK_VARIANT_COUNT] = {
        1,
        1,
        1,
        1,
    };
    double nanoseconds[IR_BENCHMARK_VARIANT_COUNT][BUSTER_IR_BENCHMARK_MAX_PASSES];
    double ticks[IR_BENCHMARK_VARIANT_COUNT][BUSTER_IR_BENCHMARK_MAX_PASSES];
    for (uint32_t variant = 0; valid && variant < IR_BENCHMARK_VARIANT_COUNT; variant += 1)
    {
        if ((selected & (1u << variant)) != 0)
        {
            // Warm-up and calibration are discarded. This also commits every repeatedly used output/scratch page.
            valid = ir_benchmark_run(&function, (IrBenchmarkVariant)variant, 1, &derived, &scratch) == IR_OK;
            if (valid)
            {
                const IrBenchmarkSample calibration = ir_benchmark_sample(&function, (IrBenchmarkVariant)variant, &derived, &scratch, &repetitions[variant], after_scalar_ms);
                valid = calibration.error == IR_OK;
            }
        }
    }
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < IR_BENCHMARK_VARIANT_COUNT; step += 1)
        {
            const uint32_t variant = (step + pass) % IR_BENCHMARK_VARIANT_COUNT;
            if ((selected & (1u << variant)) != 0)
            {
                const IrBenchmarkSample sample = ir_benchmark_sample(&function, (IrBenchmarkVariant)variant, &derived, &scratch, &repetitions[variant], after_scalar_ms);
                valid = sample.error == IR_OK;
                if (valid)
                {
                    nanoseconds[variant][pass] = (double)sample.nanoseconds / sample.repetitions;
                    ticks[variant][pass] = (double)sample.tsc_ticks / sample.repetitions;
                }
            }
        }
    }
    if (valid)
    {
        printf("\nfunction: %u instructions, %u blocks; instruction columns: %" PRIu64 " bytes\n", instructions, function.block_count, (uint64_t)instructions * 16);
        printf("arena bytes: core=%" PRIu64 " (%.3f/instruction), cfg=%" PRIu64 ", uses=%" PRIu64 "; core+cfg+uses=%" PRIu64 "\n", function.arena_bytes, (double)function.arena_bytes / instructions, cfg_bytes, use_bytes, function.arena_bytes + cfg_bytes + use_bytes);
        printf("%-8s %14s %16s %18s %20s %12s\n", "path", "ns/function", "ns/instruction", "M instructions/s", "TSC ticks/instruction", "repetitions");
        for (uint32_t variant = 0; variant < IR_BENCHMARK_VARIANT_COUNT; variant += 1)
        {
            if ((selected & (1u << variant)) != 0)
            {
                const double ns = ir_benchmark_median(nanoseconds[variant], passes);
                const double tsc = ir_benchmark_median(ticks[variant], passes);
                printf("%-8s %14.1f %16.3f %18.3f %20.3f %12u\n", ir_benchmark_names[variant], ns, ns / instructions, (double)instructions * 1000.0 / ns, tsc / instructions, repetitions[variant]);
            }
        }
    }
    else
    {
        fprintf(stderr, "benchmark-ir: validation, rebuild or minimum-duration calibration failed\n");
    }
    arena_release(&scratch);
    arena_release(&derived);
    arena_release(&memory);
    return valid;
}

static int run_ir_benchmark(int argc, char** argv)
{
    uint32_t instructions = BUSTER_IR_BENCHMARK_INSTRUCTIONS;
    uint32_t passes = BUSTER_IR_BENCHMARK_PASSES;
    uint32_t selected = (1u << IR_BENCHMARK_VARIANT_COUNT) - 1;
    uint32_t after_scalar_ms = 0;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--instructions") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = ir_benchmark_number(argv[i], 2, BUSTER_IR_BENCHMARK_MAX_INSTRUCTIONS, &instructions);
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = ir_benchmark_number(argv[i], 1, BUSTER_IR_BENCHMARK_MAX_PASSES, &passes);
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = ir_benchmark_number(argv[i], 0, BUSTER_IR_BENCHMARK_MAX_AFTER_SCALAR_MS, &after_scalar_ms);
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            selected = 0;
            for (uint32_t variant = 0; variant < IR_BENCHMARK_VARIANT_COUNT; variant += 1)
            {
                if (strcmp(argv[i], ir_benchmark_names[variant]) == 0)
                {
                    selected = 1u << variant;
                }
            }
            valid = selected != 0;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-ir [--instructions N] [--passes N] [--only scalar|vector|uses|cfg] [--after-scalar MS]\n");
        fprintf(stderr, "  instructions: 2..%u; passes: 1..%u; after-scalar: 0..%u ms\n", BUSTER_IR_BENCHMARK_MAX_INSTRUCTIONS, BUSTER_IR_BENCHMARK_MAX_PASSES, BUSTER_IR_BENCHMARK_MAX_AFTER_SCALAR_MS);
    }
#ifndef __OPTIMIZE__
    fprintf(stderr, "benchmark-ir: an optimized build is required; unoptimized timings are not reported\n");
    valid = false;
#endif
    if (valid)
    {
        printf("benchmark-ir: synthetic integer/control-flow functions, %u passes; each sample >= 2 ms\n", passes);
        printf("No corpus bytes/source-token measurement; TSC ticks are not core cycles. Pin to one CPU for measurements.\n");
        if (after_scalar_ms != 0 && (selected & (1u << IR_BENCHMARK_VECTOR)) != 0)
        {
            printf("Each vector sample follows %u ms of untimed scalar verification.\n", after_scalar_ms);
        }
        const uint32_t small = instructions < BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS ? instructions : BUSTER_IR_BENCHMARK_BLOCK_INSTRUCTIONS;
        valid = ir_benchmark_case(small, passes, selected, after_scalar_ms);
        if (valid && instructions != small)
        {
            valid = ir_benchmark_case(instructions, passes, selected, after_scalar_ms);
        }
    }
    return valid ? 0 : 1;
}
