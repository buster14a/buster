// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measures the complete checker graph API, including validation, dependency scheduling, action preparation,
// typed output and diagnostics. Graph generation is outside timing. This synthetic benchmark does not establish
// parser/corpus or Zen 5 acceptance. Every measured output is compared with the postorder oracle outside timing.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#include "buster/compiler/lane.c"
#include "buster/compiler/c_types.c"
// The builtin lookups, vector member decoding and overload declaration serve the source reader and their own tests;
// the synthetic graphs use only the checker's graph API.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/c_builtin_intrinsic_catalog.c"
#include "buster/compiler/c_builtin.c"
#include "buster/compiler/c_check_actions.c"
#include "buster/compiler/c_check_builtin_x86.c"
#include "buster/compiler/c_check_builtin.c"
#include "buster/compiler/c_check_varargs.c"
#include "buster/compiler/c_check_builtin_misc.c"
#include "buster/compiler/c_check_builtin_query.c"
#include "buster/compiler/c_check_builtin_float.c"
#include "buster/compiler/c_check_builtin_math.c"
#include "buster/compiler/c_check_builtin_atomic.c"
#include "buster/compiler/c_check_builtin_atomic_rmw.c"
#include "buster/compiler/c_check_builtin_vector.c"
#include "buster/compiler/c_check_builtin_bit_cast.c"
#include "buster/compiler/c_check_builtin_typed_overflow.c"
#include "buster/compiler/c_check_builtin_rounds.c"
#include "buster/compiler/c_checker.c"
#pragma clang diagnostic pop

#define BUSTER_CHECK_BENCHMARK_PASSES 21u

typedef struct CheckBenchmarkSample CheckBenchmarkSample;
struct CheckBenchmarkSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
};

static uint64_t check_benchmark_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static uint64_t check_benchmark_median(const CheckBenchmarkSample* samples, uint32_t column)
{
    uint64_t ordered[BUSTER_CHECK_BENCHMARK_PASSES];
    for (uint32_t i = 0; i < BUSTER_CHECK_BENCHMARK_PASSES; i += 1)
    {
        const uint64_t value = column == 0 ? samples[i].nanoseconds : column == 1 ? samples[i].cycles : samples[i].instructions;
        uint32_t j = i;
        while (j != 0 && ordered[j - 1] > value)
        {
            ordered[j] = ordered[j - 1];
            j -= 1;
        }
        ordered[j] = value;
    }
    return ordered[BUSTER_CHECK_BENCHMARK_PASSES / 2];
}

static int check_benchmark_counter(uint64_t event)
{
    const struct perf_event_attr attribute = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = event,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    return (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static bool check_benchmark_value_equal(const CheckValue* x, const CheckValue* y)
{
    const bool floating = (isnan(x->floating) && isnan(y->floating)) ||
                          (x->floating == y->floating && signbit(x->floating) == signbit(y->floating));
    const bool imaginary = (isnan(x->imaginary) && isnan(y->imaginary)) ||
                           (x->imaginary == y->imaginary && signbit(x->imaginary) == signbit(y->imaginary));
    const bool equal = x->type == y->type && x->category == y->category && x->flags == y->flags && x->constant_kind == y->constant_kind &&
                       x->integer == y->integer && floating && imaginary && x->object == y->object && x->offset == y->offset && x->overload_head == y->overload_head &&
                       x->bit_width == y->bit_width && x->fault == y->fault && x->fault_node == y->fault_node;
    return equal;
}

static bool check_benchmark_equal(const CheckStream* a, const CheckStream* b, uint32_t arguments)
{
    bool equal = a->status == b->status && a->count == b->count && a->report_count == b->report_count && a->effect_count == b->effect_count &&
                 a->varargs_count == b->varargs_count && check_builtin_stream_metadata_equal(a, b, check_benchmark_value_equal);
    for (uint32_t i = 0; equal && i < a->count; i += 1)
    {
        equal = check_benchmark_value_equal(&a->values[i], &b->values[i]) && a->diagnostics[i] == b->diagnostics[i] &&
                a->warnings[i] == b->warnings[i] && a->warning_ignored[i] == b->warning_ignored[i];
        equal = equal && a->conversions[i].store.type == b->conversions[i].store.type && a->conversions[i].store.flags == b->conversions[i].store.flags;
        for (uint32_t j = 0; equal && j < 3; j += 1)
        {
            equal = a->conversions[i].operands[j].type == b->conversions[i].operands[j].type &&
                    a->conversions[i].operands[j].flags == b->conversions[i].operands[j].flags;
        }
    }
    for (uint32_t i = 0; equal && i < arguments; i += 1)
    {
        equal = a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    for (uint32_t i = 0; equal && i < a->report_count; i += 1)
    {
        equal = a->reports[i].node == b->reports[i].node && a->reports[i].token == b->reports[i].token &&
                a->reports[i].message == b->reports[i].message && a->reports[i].diagnostic == b->reports[i].diagnostic &&
                a->reports[i].fault == b->reports[i].fault;
    }
    for (uint32_t i = 0; equal && i < a->effect_count; i += 1)
    {
        equal = check_store_effect_equal(a->effects + i, b->effects + i, check_benchmark_value_equal);
    }
    for (uint32_t i = 0; equal && i < a->varargs_count; i += 1)
    {
        const CheckVarargsEffect* x = a->varargs + i;
        const CheckVarargsEffect* y = b->varargs + i;
        equal = x->node == y->node && x->requested_type == y->requested_type && x->evaluated_operands == y->evaluated_operands &&
                x->warnings == y->warnings && x->mutates_list == y->mutates_list;
    }
    return equal;
}

static bool check_benchmark_generate(CheckGraph* graph, bool chain)
{
    uint64_t random = 133;
    bool valid = true;
    for (uint32_t i = 0; valid && i < graph->capacity; i += 1)
    {
        random = random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        const uint32_t local = i % 127;
        if (i == 0 || (!chain && local < 64))
        {
            const uint32_t node = check_graph_add(graph, CHECK_OP_VALUE, i, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            valid = node != BUSTER_CHECK_NONE;
            if (valid)
            {
                graph->leaves[node] = (CheckValue){
                    .type = BUSTER_TYPE_ULONG,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_ICE,
                    .constant_kind = CHECK_CONSTANT_INTEGER,
                    .integer = random >> 16,
                };
            }
        }
        else
        {
            const CheckOp operations[] = {
                CHECK_OP_ADD,
                CHECK_OP_SUBTRACT,
                CHECK_OP_MULTIPLY,
                CHECK_OP_BIT_XOR,
            };
            const uint32_t a = chain ? i - 1 : i - local + (local - 64) * 2;
            const uint32_t b = chain ? 0 : a + 1;
            const CheckOp op = operations[(random >> 32) & 3];
            valid = check_graph_add(graph, op, i, a, b, BUSTER_CHECK_NONE) != BUSTER_CHECK_NONE;
        }
    }
    return valid;
}

int run_checker_benchmark(int argc, char** argv)
{
    uint32_t nodes = 65535;
    uint32_t after_scalar = 0;
    uint32_t only = 2;
    bool chain = false;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : 2;
            valid = only != 2;
        }
        else if (strcmp(argv[i], "--shape") == 0 && i + 1 < argc)
        {
            i += 1;
            chain = strcmp(argv[i], "chain") == 0;
            valid = chain || strcmp(argv[i], "wide") == 0;
        }
        else if ((strcmp(argv[i], "--nodes") == 0 || strcmp(argv[i], "--after-scalar") == 0) && i + 1 < argc)
        {
            const bool size = strcmp(argv[i], "--nodes") == 0;
            char* end = 0;
            errno = 0;
            const char* text = argv[++i];
            const unsigned long value = strtoul(text, &end, 10);
            valid = errno == 0 && text != end && *end == 0 && *text != '-' && value <= (size ? 1000000ul : 1000ul) && (!size || value != 0);
            if (size)
            {
                nodes = (uint32_t)value;
            }
            else
            {
                after_scalar = (uint32_t)value;
            }
        }
        else
        {
            valid = false;
        }
    }
    const TypeCapacity capacity = {
        .types = 256,
        .parameters = 256,
        .tags = 16,
        .members = 64,
    };
    Arena type_storage = valid ? arena_reserve(type_capacity_bytes(capacity)) : (Arena){ 0 };
    Arena type_index = valid ? arena_reserve(type_index_capacity_bytes(capacity)) : (Arena){ 0 };
    Arena storage = valid ? arena_reserve((uint64_t)nodes * 256 + 65536) : (Arena){ 0 };
    Arena output = valid ? arena_reserve((uint64_t)nodes * 512 + 65536) : (Arena){ 0 };
    Arena scratch = valid ? arena_reserve((uint64_t)nodes * 512 + 65536) : (Arena){ 0 };
    TypeTable types = { 0 };
    CheckGraph graph = { 0 };
    CheckStream expected = { 0 };
    const CheckOptions scalar = {
        .scalar = true
    };
    const CheckOptions vector = { 0 };
    valid = valid && type_initialize(&types, &type_storage, &type_index, capacity) && check_graph_initialize(&graph, &storage, nodes, 0) &&
            check_benchmark_generate(&graph, chain) && check_graph_run(&types, &graph, &output, &scratch, &scalar, &expected);
    const uint64_t expected_end = output.length;
    CheckBenchmarkSample samples[2][BUSTER_CHECK_BENCHMARK_PASSES] = { 0 };
    CheckStatistics statistics[2] = { 0 };
    const int counters[2] = {
        valid ? check_benchmark_counter(PERF_COUNT_HW_CPU_CYCLES) : -1,
        valid ? check_benchmark_counter(PERF_COUNT_HW_INSTRUCTIONS) : -1,
    };
    bool available[2] = {
        counters[0] >= 0,
        counters[1] >= 0
    };
    for (uint32_t pass = 0; valid && pass <= BUSTER_CHECK_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 2; step += 1)
        {
            const uint32_t path = (pass + step) & 1;
            if (only == 2 || only == path)
            {
                if (path == 1 && after_scalar != 0)
                {
                    const uint64_t until = check_benchmark_time() + (uint64_t)after_scalar * 1000000;
                    while (valid && check_benchmark_time() < until)
                    {
                        CheckStream conditioning;
                        valid = check_graph_run(&types, &graph, &output, &scratch, &scalar, &conditioning);
                        __asm__ volatile("" : : "r"(conditioning.values) : "memory");
                        arena_pop_to(&output, expected_end);
                    }
                }
                for (uint32_t counter = 0; counter < 2; counter += 1)
                {
                    if (counters[counter] >= 0)
                    {
                        available[counter] &= ioctl(counters[counter], PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counters[counter], PERF_EVENT_IOC_ENABLE, 0) == 0;
                    }
                }
                CheckStream actual;
                const uint64_t begin = check_benchmark_time();
                const bool checked = check_graph_run(&types, &graph, &output, &scratch, path == 0 ? &scalar : &vector, &actual);
                const uint64_t elapsed = check_benchmark_time() - begin;
                uint64_t counts[2][3] = { 0 };
                for (uint32_t counter = 0; counter < 2; counter += 1)
                {
                    if (counters[counter] >= 0)
                    {
                        available[counter] &= ioctl(counters[counter], PERF_EVENT_IOC_DISABLE, 0) == 0 &&
                                              read(counters[counter], counts[counter], sizeof(counts[counter])) == sizeof(counts[counter]);
                        available[counter] &= counts[counter][1] == counts[counter][2] && counts[counter][2] != 0;
                    }
                }
                valid = valid && checked && check_benchmark_equal(&expected, &actual, graph.argument_count);
                if (valid)
                {
                    statistics[path] = actual.statistics;
                }
                if (pass != 0)
                {
                    samples[path][pass - 1] = (CheckBenchmarkSample){
                        .nanoseconds = elapsed,
                        .cycles = counts[0][0],
                        .instructions = counts[1][0],
                    };
                }
                arena_pop_to(&output, expected_end);
            }
        }
    }
    if (valid)
    {
        printf("checker synthetic %s graph: %u nodes, %u %s passes, after-scalar %u ms; outputs identical\n",
               chain ? "chain" : "wide", nodes, BUSTER_CHECK_BENCHMARK_PASSES, only == 2 ? "interleaved" : "isolated", after_scalar);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (only == 2 || only == path)
            {
                const uint64_t ns = check_benchmark_median(samples[path], 0);
                printf("  %s: %.3f ns/node, %.3f million nodes/s", path == 0 ? "scalar" : "vector", (double)ns / nodes, (double)nodes * 1000.0 / ns);
                if (available[0])
                {
                    printf(", %.3f cycles/node", (double)check_benchmark_median(samples[path], 1) / nodes);
                }
                else
                {
                    printf(", cycles unavailable");
                }
                if (available[1])
                {
                    printf(", %.3f instructions/node", (double)check_benchmark_median(samples[path], 2) / nodes);
                }
                else
                {
                    printf(", instructions unavailable");
                }
                printf("\n    %u levels, %u batches, %u scalar nodes, %u vector nodes; output %" PRIu64 " bytes, scratch %" PRIu64 " bytes\n",
                       statistics[path].levels, statistics[path].batches, statistics[path].scalar_nodes, statistics[path].vector_nodes,
                       statistics[path].output_bytes, statistics[path].scratch_bytes);
            }
        }
    }
    else
    {
        fprintf(stderr, "benchmark-checker: invalid arguments, allocation failure or unequal outputs\n"
                        "usage: benchmark-checker [--nodes 1..1000000] [--shape wide|chain] [--only scalar|vector] [--after-scalar 0..1000]\n");
    }
    for (uint32_t counter = 0; counter < 2; counter += 1)
    {
        if (counters[counter] >= 0)
        {
            close(counters[counter]);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    arena_release(&type_index);
    arena_release(&type_storage);
    return !valid;
}
