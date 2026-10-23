// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original frozen synthetic IR -> selection -> effective uses -> liveness -> allocation -> frame/fragments -> encoder.
// Separate selection and complete-pipeline rows include verification and arena resets. No source/corpus or generated
// run-time performance is inferred. Timing uses elapsed nanoseconds and invariant TSC ticks, explicitly not core cycles.
// The 9700X invocation takes its shared per-run lock and CPU 2 pin. --only and --after-scalar expose both power states.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <assert.h>

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/link.c"
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/x86_target.c"
#include "buster/compiler/x86_target_forms.c"
#include "buster/compiler/x86_zen5.c"
#include "buster/compiler/ir.c"
#include "buster/compiler/dataflow.c"
#include "buster/compiler/c_types.c"
#include "buster/compiler/calling_convention.c"
#include "buster/compiler/tier1_select.c"
#include "buster/compiler/tier1_allocate.c"
#include "buster/compiler/tier1_rematerialize.c"
#include "buster/compiler/tier1_liveness.c"
#include "buster/compiler/tier1_frame.c"
#include "buster/compiler/tier1_frame_emit.c"
#include "buster/compiler/tier1_emit.c"
#include "buster/compiler/branch_sizing.c"
#pragma clang diagnostic pop

#define BUSTER_TIER1_SELECT_BENCH_PASSES 7
#define BUSTER_TIER1_SELECT_BENCH_SAMPLE_NS UINT64_C(5000000)

typedef struct Tier1SelectBench Tier1SelectBench;
struct Tier1SelectBench
{
    IrFunction* functions;
    uint32_t function_count;
    uint64_t instruction_count;
    Arena output;
    Arena scratch;
    uint8_t* allowed;
    uint64_t records;
    uint64_t code_bytes;
    uint64_t unknown_costs;
    uint64_t peak_retained;
    uint64_t peak_scratch;
    uint64_t spill_slots;
    uint64_t spill_stores;
    uint64_t spill_loads;
    uint64_t rematerializations;
    volatile uint64_t sink;
};

static uint64_t tier1_select_bench_time(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static uint64_t tier1_select_bench_ticks(void)
{
    _mm_lfence();
    const uint64_t ticks = __rdtsc();
    _mm_lfence();
    return ticks;
}

static uint64_t tier1_select_bench_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static bool tier1_select_bench_number(const char* text, uint32_t maximum, uint32_t* output)
{
    uint64_t value = 0;
    bool valid = text[0] != 0;
    for (uint32_t i = 0; valid && text[i] != 0; i += 1)
    {
        valid = text[i] >= '0' && text[i] <= '9';
        if (valid)
        {
            value = value * 10 + (uint32_t)(text[i] - '0');
            valid = value <= maximum;
        }
    }
    if (valid)
    {
        *output = (uint32_t)value;
    }
    return valid;
}

static bool tier1_select_bench_corpus(Tier1SelectBench* bench, Arena* input, uint32_t steps, uint32_t shape)
{
    bool valid = true;
    uint64_t random = UINT64_C(0x93b670c147582eef);
    bench->functions = BUSTER_ARENA_PUSH_ARRAY(input, IrFunction, bench->function_count);
    valid = bench->functions != 0;
    for (uint32_t f = 0; valid && f < bench->function_count; f += 1)
    {
        const bool wide = shape == 1;
        const bool pressure = shape == 2;
        const uint32_t count = steps * (pressure ? 3 : 2) + 2;
        const uint8_t type = (f & 1) != 0 ? IR_I32 : IR_I64;
        const IrSizes sizes =
        {
            .instruction_count = count,
            .block_count = 1,
            .constant_count = steps,
            .parameter_count = 1,
        };
        IrFunction* function = &bench->functions[f];
        valid = ir_create(input, &sizes, function) == IR_OK;
        if (valid)
        {
            function->parameter_types[0] = type;
            function->result_type = type;
            function->block_offsets[1] = count;
            function->op[0] = IR_PARAM;
            function->type[0] = type;
            function->a[0] = 0;
            uint32_t previous = 0;
            for (uint32_t step = 0; step < steps; step += 1)
            {
                const uint32_t constant = wide ? step + 1 : step * 2 + 1;
                const uint32_t arithmetic = wide ? steps + step + 1 : step * 2 + 2;
                const uint8_t operations[] = {IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR};
                const uint8_t operation = wide ? IR_MUL : pressure ? IR_XOR : operations[step % BUSTER_ARRAY_LENGTH(operations)];
                uint64_t value = wide ? UINT64_C(1) << (step % 30) : tier1_select_bench_random(&random);
                value = !wide && (step & 3) != 0 ? (uint64_t)(int64_t)(int8_t)value : value;
                value = type == IR_I32 ? (uint32_t)value : value;
                function->constants[step].low = value;
                function->op[constant] = IR_CONST;
                function->type[constant] = type;
                function->a[constant] = step;
                function->op[arithmetic] = operation;
                function->type[arithmetic] = type;
                function->a[arithmetic] = wide || pressure ? 0 : previous;
                function->b[arithmetic] = constant;
                previous = arithmetic;
            }
            if (pressure)
            {
                previous = 0;
                for (uint32_t step = 0; step < steps; step += 1)
                {
                    const uint32_t arithmetic = steps * 2 + 1 + step;
                    function->op[arithmetic] = IR_ADD;
                    function->type[arithmetic] = type;
                    function->a[arithmetic] = previous;
                    function->b[arithmetic] = step * 2 + 2;
                    previous = arithmetic;
                }
            }
            function->op[count - 1] = IR_RET;
            function->a[count - 1] = previous;
            bench->instruction_count += count;
        }
    }
    return valid;
}

static bool tier1_select_bench_run(Tier1SelectBench* bench, bool vector, bool complete, bool verify)
{
    bool valid = true;
    bench->records = 0;
    bench->code_bytes = 0;
    bench->unknown_costs = 0;
    bench->peak_retained = 0;
    bench->peak_scratch = 0;
    bench->spill_slots = 0;
    bench->spill_stores = 0;
    bench->spill_loads = 0;
    bench->rematerializations = 0;
    for (uint32_t f = 0; valid && f < bench->function_count; f += 1)
    {
        Tier1SelectPlan plan;
        uint32_t node;
        valid = tier1_select_plan(&bench->functions[f], 0, &bench->output, &bench->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
        if (valid && complete)
        {
            const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .rematerialize = true };
            Tier1EmitResult emitted;
            valid = tier1_emit_function(&plan, &options, bench->allowed, &bench->output, &bench->scratch, &emitted, &node) == TIER1_EMIT_OK;
            if (valid)
            {
                valid = !verify || tier1_allocation_verify(&emitted.prepared.allocation, &emitted.allocation);
                const uint64_t scratch_bytes = emitted.liveness.scratch_bytes > emitted.transition_scratch_bytes ? emitted.liveness.scratch_bytes : emitted.transition_scratch_bytes;
                bench->peak_scratch = scratch_bytes > bench->peak_scratch ? scratch_bytes : bench->peak_scratch;
                bench->spill_slots += emitted.allocation.spill_count;
                bench->spill_stores += emitted.spill_stores;
                bench->spill_loads += emitted.spill_loads;
                bench->rematerializations += emitted.rematerializations;
                Tier1SelectCode* code = &emitted.code;
                uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(&bench->output, uint32_t, code->count);
                valid = valid && offsets != 0;
                if (valid)
                {
                    const BranchSizing sized = size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets);
                    valid = sized.error == BRANCH_SIZING_OK;
                }
                const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
                uint8_t* bytes = arena_push(&bench->output, capacity, 64);
                const int64_t encoded = valid && bytes != 0 ? (vector ? x86_encode(code->records, code->count, bytes, capacity, 0, 0) : x86_encode_scalar(code->records, code->count, bytes, capacity, 0, 0)) : -1;
                valid = valid && encoded >= 0;
                if (valid)
                {
                    bench->records += code->count;
                    bench->code_bytes += (uint64_t)encoded;
                    bench->unknown_costs += code->unknown_costs;
                    bench->sink += bytes[0] + code->count;
                }
            }
        }
        if (valid)
        {
            bench->sink += plan.estimated_cost + plan.effective_use_count;
            bench->peak_retained = bench->output.length > bench->peak_retained ? bench->output.length : bench->peak_retained;
        }
        else
        {
            fprintf(stderr, "benchmark-tier1-select: %s pipeline failed at function %u, node %u\n", vector ? "vector" : "scalar", f, node);
        }
        arena_pop_to(&bench->output, 0);
        arena_pop_to(&bench->scratch, 0);
    }
    return valid;
}

static double tier1_select_bench_median(double* samples)
{
    for (uint32_t i = 1; i < BUSTER_TIER1_SELECT_BENCH_PASSES; i += 1)
    {
        const double value = samples[i];
        uint32_t p = i;
        while (p != 0 && samples[p - 1] > value)
        {
            samples[p] = samples[p - 1];
            p -= 1;
        }
        samples[p] = value;
    }
    return samples[BUSTER_TIER1_SELECT_BENCH_PASSES / 2];
}

int run_tier1_select_benchmark(int argc, char** argv);
int run_tier1_select_benchmark(int argc, char** argv)
{
    uint32_t steps = 256;
    uint32_t functions = 32;
    uint32_t after_scalar = 0;
    uint32_t only = 2;
    bool valid = true;
    for (int argument = 1; valid && argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--steps") == 0 && argument + 1 < argc)
        {
            valid = tier1_select_bench_number(argv[++argument], 8192, &steps) && steps != 0;
        }
        else if (strcmp(argv[argument], "--functions") == 0 && argument + 1 < argc)
        {
            valid = tier1_select_bench_number(argv[++argument], 256, &functions) && functions != 0;
        }
        else if (strcmp(argv[argument], "--after-scalar") == 0 && argument + 1 < argc)
        {
            valid = tier1_select_bench_number(argv[++argument], 10000, &after_scalar);
        }
        else if (strcmp(argv[argument], "--only") == 0 && argument + 1 < argc)
        {
            argument += 1;
            only = strcmp(argv[argument], "scalar") == 0 ? 0 : strcmp(argv[argument], "vector") == 0 ? 1 : 3;
            valid = only != 3;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-tier1-select [--steps N] [--functions N] [--only scalar|vector] [--after-scalar MS]\n");
    }
    else
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
        Arena input = arena_reserve(UINT64_C(256) << 20);
        Tier1SelectBench bench =
        {
            .function_count = functions,
            .output = arena_reserve(UINT64_C(128) << 20),
            .scratch = arena_reserve(UINT64_C(128) << 20),
        };
        uint8_t allowed[X86_FORM_COUNT];
        const X86Target target = { .architecture = X86_ARCH_BASELINE };
        x86_target_forms(&target, allowed);
        bench.allowed = allowed;
        printf("synthetic IR; complete = plan + liveness + required locations + allocation + frame + fragments + branch sizing + encode; TSC ticks are not core cycles\n");
        printf("shape,phase,path,IR,records,bytes,unknown_costs,spill_slots,stores,reloads,rematerializations,retained_peak,scratch_liveness_transition_peak,ns/IR,TSC_ticks/IR\n");
        for (uint32_t shape = 0; valid && shape < 3; shape += 1)
        {
            bench.instruction_count = 0;
            valid = tier1_select_bench_corpus(&bench, &input, steps, shape);
            for (uint32_t phase = 0; valid && phase < 2; phase += 1)
            {
                double nanoseconds[2][BUSTER_TIER1_SELECT_BENCH_PASSES] = {0};
                double ticks[2][BUSTER_TIER1_SELECT_BENCH_PASSES] = {0};
                valid = tier1_select_bench_run(&bench, false, phase != 0, true) && tier1_select_bench_run(&bench, true, phase != 0, true);
                for (uint32_t pass = 0; valid && pass < BUSTER_TIER1_SELECT_BENCH_PASSES; pass += 1)
                {
                    for (uint32_t order = 0; valid && order < 2; order += 1)
                    {
                        const uint32_t path = order ^ (pass & 1);
                        if (only == 2 || only == path)
                        {
                            if (path != 0 && after_scalar != 0)
                            {
                                const uint64_t end = tier1_select_bench_time() + (uint64_t)after_scalar * 1000000;
                                do
                                {
                                    valid = tier1_select_bench_run(&bench, false, phase != 0, false);
                                } while (valid && tier1_select_bench_time() < end);
                            }
                            const uint64_t start = tier1_select_bench_time();
                            const uint64_t first_tick = tier1_select_bench_ticks();
                            uint32_t repetitions = 0;
                            uint64_t end;
                            do
                            {
                                valid = tier1_select_bench_run(&bench, path != 0, phase != 0, false);
                                repetitions += 1;
                                end = tier1_select_bench_time();
                            } while (valid && end - start < BUSTER_TIER1_SELECT_BENCH_SAMPLE_NS);
                            const uint64_t last_tick = tier1_select_bench_ticks();
                            const double instructions = (double)bench.instruction_count * repetitions;
                            nanoseconds[path][pass] = (double)(end - start) / instructions;
                            ticks[path][pass] = (double)(last_tick - first_tick) / instructions;
                        }
                    }
                }
                for (uint32_t path = 0; valid && path < 2; path += 1)
                {
                    if (only == 2 || only == path)
                    {
                        valid = tier1_select_bench_run(&bench, path != 0, phase != 0, false);
                        printf("%s,%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.3f,%.3f\n", shape == 0 ? "chain" : shape == 1 ? "wide" : "pressure", phase == 0 ? "selection" : "complete", path == 0 ? "scalar" : "vector", bench.instruction_count, bench.records, bench.code_bytes, bench.unknown_costs, bench.spill_slots, bench.spill_stores, bench.spill_loads, bench.rematerializations, bench.peak_retained, bench.peak_scratch, tier1_select_bench_median(nanoseconds[path]), tier1_select_bench_median(ticks[path]));
                    }
                }
            }
            arena_pop_to(&input, 0);
        }
        arena_release(&bench.scratch);
        arena_release(&bench.output);
        arena_release(&input);
        result = valid ? 0 : 1;
    }
    return result;
}
