// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's charged typed-builtin pipeline: initialize the stream, construct the recipe, publish its columns and locals,
// hand off a Tier0Function, compile, size branches, encode, consume the output and reclaim per-function storage. The
// scalar publication path uses scalar Tier 0, sizing and encoding; the vector path uses their vector twins. Include
// after c_builtin_lower_test.c for its stream layout/helper. Native correctness is the separate test-builtin-lowering
// command; this command validates the handoff and compares final bytes outside its timed samples, without forking.
//
// Fixed synthetic typed calls have already evaluated arguments; parsing, source checking, expression evaluation,
// Tier 1 and generated-program execution are outside this benchmark. No performance claim follows from these shapes.
// Tables, arena reservation, validation, first touch and calibration are outside the 21 samples. --after-scalar MS
// conditions each vector sample with the entire scalar pipeline. Run each process under the 9700X lock on CPU 2.
// The output arena is rewound per function; consumers rewind their thread scratch. Reported arena use is its exact
// logical high-water length, not resident memory. Thread scratch virtual reservation is reported separately because
// its public arena interface has no high-water counter.

#include <time.h>

#define BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES 21u
#define BUSTER_CBUILTIN_LOWER_BENCHMARK_SAMPLE_NS UINT64_C(2000000)
#define BUSTER_CBUILTIN_LOWER_BENCHMARK_REPEAT_LIMIT (1u << 20)
#define BUSTER_CBUILTIN_LOWER_BENCHMARK_ARENA_BYTES (UINT64_C(1) << 24)

typedef struct CBuiltinLowerBenchmarkShape CBuiltinLowerBenchmarkShape;
struct CBuiltinLowerBenchmarkShape
{
    const char* name;
    CBuiltinLowerCall call;
};

typedef struct CBuiltinLowerBenchmarkOutput CBuiltinLowerBenchmarkOutput;
struct CBuiltinLowerBenchmarkOutput
{
    Tier0Code code;
    uint8_t* bytes;
    uint32_t byte_count;
    uint32_t entry_count;
    uint32_t temporary_count;
    uint32_t local_count;
    uint64_t arena_bytes;
};

static uint64_t cbuiltin_lower_benchmark_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    const uint64_t result = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return result;
}

static bool cbuiltin_lower_benchmark_compile(Arena* arena, const CBuiltinLowerCall* call, bool scalar, bool validate, CBuiltinLowerBenchmarkOutput* output)
{
    const uint64_t mark = arena->length;
    CBuiltinLowerTestStream stream;
    cbuiltin_lower_test_stream(&stream, call);
    const CBuiltinLowerResult lowered = scalar ? cbuiltin_lower_scalar(&stream.buffer, call) : cbuiltin_lower(&stream.buffer, call);
    bool result = lowered.error == CBUILTIN_LOWER_OK;
    if (result)
    {
        const uint32_t convert = stream.buffer.count;
        const uint32_t end = convert + 1;
        stream.kinds[convert] = BUSTER_TYPED_CONVERT;
        stream.classes[convert] = BUSTER_TYPED_CLASS_U64;
        stream.values[convert] = 0;
        stream.kinds[end] = BUSTER_TYPED_RETURN;
        stream.classes[end] = BUSTER_TYPED_CLASS_U64;
        stream.values[end] = 0;
        stream.buffer.count += 2;
    }
    const AbiLocation parameters[3] =
    {
        {
            .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
        {
            .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
        {
            .registers = { BUSTER_ABI_RDX, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
    };
    const Tier0Function function =
    {
        .kinds = stream.buffer.kinds,
        .classes = stream.buffer.classes,
        .values = stream.buffer.values,
        .count = stream.buffer.count,
        .local_classes = stream.buffer.local_classes,
        .local_flags = stream.buffer.local_flags,
        .local_count = stream.buffer.local_count,
        .parameter_count = call->id >= CBUILTIN_ADD_OVERFLOW ? 3 : 1,
        .parameters = parameters,
        .result =
        {
            .kind = BUSTER_ABI_RESULT_REGISTERS,
            .part_bytes = 8,
            .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
        },
    };
    if (result && validate)
    {
        const Tier0Check checked = tier0_validate(&function);
        result = checked.error == TIER0_OK;
    }
    if (result)
    {
        output->code = scalar ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
        result = output->code.error == TIER0_OK && output->code.reference_count == 0;
    }
    if (result)
    {
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(output->code.count);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)output->code.count + 1);
        output->bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, capacity);
        result = offsets != 0 && output->bytes != 0;
        if (result)
        {
            const BranchSizing sized = scalar ? size_branches_scalar(output->code.records, output->code.count, output->code.branches, output->code.targets, output->code.branch_count, offsets)
                                              : size_branches(output->code.records, output->code.count, output->code.branches, output->code.targets, output->code.branch_count, offsets);
            const int64_t encoded = sized.error != BRANCH_SIZING_OK ? -1 :
                (scalar ? x86_encode_scalar(output->code.records, output->code.count, output->bytes, capacity, 0, 0)
                        : x86_encode(output->code.records, output->code.count, output->bytes, capacity, 0, 0));
            result = encoded > 0;
            if (result)
            {
                output->byte_count = (uint32_t)encoded;
                output->entry_count = function.count;
                output->temporary_count = lowered.temporary_count;
                output->local_count = function.local_count;
                output->arena_bytes = arena->length - mark;
            }
        }
    }
    return result;
}

// Each iteration charges output use and rewind. The barrier makes every emitted byte observable to the bootstrap
// compiler without adding a second hash traversal to the pipeline being measured.
static __attribute__((noinline)) bool cbuiltin_lower_benchmark_repeat(Arena* arena, const CBuiltinLowerCall* call, bool scalar, uint32_t repeats, uint64_t* checksum)
{
    bool result = true;
    for (uint32_t i = 0; result && i < repeats; i += 1)
    {
        const uint64_t mark = arena->length;
        CBuiltinLowerBenchmarkOutput output = { 0 };
        result = cbuiltin_lower_benchmark_compile(arena, call, scalar, false, &output);
        if (result)
        {
            *checksum += output.byte_count + output.bytes[0] + output.bytes[output.byte_count - 1];
            __asm__ volatile("" : : "r"(output.bytes), "r"(output.byte_count) : "memory");
        }
        arena_pop_to(arena, mark);
    }
    return result;
}

static void cbuiltin_lower_benchmark_sort(double* values)
{
    for (uint32_t i = 1; i < BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES; i += 1)
    {
        const double value = values[i];
        uint32_t place = i;
        while (place != 0 && values[place - 1] > value)
        {
            values[place] = values[place - 1];
            place -= 1;
        }
        values[place] = value;
    }
}

static bool cbuiltin_lower_benchmark_shape(Arena* arena, const CBuiltinLowerBenchmarkShape* shape, int only, uint32_t after_ms, uint64_t* checksum)
{
    const uint64_t mark = arena->length;
    CBuiltinLowerBenchmarkOutput outputs[2] = { 0 };
    bool result = cbuiltin_lower_benchmark_compile(arena, &shape->call, true, true, &outputs[0]) &&
                  cbuiltin_lower_benchmark_compile(arena, &shape->call, false, true, &outputs[1]);
    if (result)
    {
        result = cbuiltin_lower_test_same_code(&outputs[0].code, &outputs[1].code) && outputs[0].byte_count == outputs[1].byte_count &&
                 memcmp(outputs[0].bytes, outputs[1].bytes, outputs[0].byte_count) == 0;
    }
    if (result)
    {
        printf("%-21s entries=%u records=%u emitted=%u B temporaries=%u locals=%u output-arena=%" PRIu64 " B\n", shape->name, outputs[0].entry_count,
               outputs[0].code.count, outputs[0].byte_count, outputs[0].temporary_count, outputs[0].local_count, outputs[0].arena_bytes);
    }
    arena_pop_to(arena, mark);
    uint32_t repeats[2] =
    {
        1,
        1,
    };
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        if (only == -1 || only == (int)path)
        {
            uint64_t elapsed = 0;
            while (result && elapsed < BUSTER_CBUILTIN_LOWER_BENCHMARK_SAMPLE_NS && repeats[path] <= BUSTER_CBUILTIN_LOWER_BENCHMARK_REPEAT_LIMIT)
            {
                const uint64_t start = cbuiltin_lower_benchmark_time();
                result = cbuiltin_lower_benchmark_repeat(arena, &shape->call, path == 0, repeats[path], checksum);
                elapsed = cbuiltin_lower_benchmark_time() - start;
                if (elapsed < BUSTER_CBUILTIN_LOWER_BENCHMARK_SAMPLE_NS && repeats[path] < BUSTER_CBUILTIN_LOWER_BENCHMARK_REPEAT_LIMIT)
                {
                    repeats[path] *= 2;
                }
                else
                {
                    break;
                }
            }
        }
    }
    double samples[2][BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES] = { 0 };
    for (uint32_t pass = 0; result && pass < BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t order = 0; result && order < 2; order += 1)
        {
            const uint32_t path = order ^ (pass & 1);
            if (only == -1 || only == (int)path)
            {
                if (path == 1 && after_ms != 0)
                {
                    const uint64_t end = cbuiltin_lower_benchmark_time() + (uint64_t)after_ms * UINT64_C(1000000);
                    while (result && cbuiltin_lower_benchmark_time() < end)
                    {
                        result = cbuiltin_lower_benchmark_repeat(arena, &shape->call, true, 1, checksum);
                    }
                }
                const uint64_t start = cbuiltin_lower_benchmark_time();
                result = result && cbuiltin_lower_benchmark_repeat(arena, &shape->call, path == 0, repeats[path], checksum);
                samples[path][pass] = (double)(cbuiltin_lower_benchmark_time() - start) / repeats[path];
            }
        }
    }
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        if (only == -1 || only == (int)path)
        {
            cbuiltin_lower_benchmark_sort(samples[path]);
            printf("  %-6s median=%.2f ns/function range=[%.2f, %.2f] samples=%u repeats=%u after-scalar=%u ms\n", path == 0 ? "scalar" : "vector",
                   samples[path][BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES / 2], samples[path][0], samples[path][BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES - 1],
                   BUSTER_CBUILTIN_LOWER_BENCHMARK_PASSES, repeats[path], path == 1 ? after_ms : 0);
        }
    }
    if (!result)
    {
        fprintf(stderr, "benchmark-builtin-lowering: %s failed validation, byte comparison, compilation or allocation\n", shape->name);
    }
    return result;
}

static int run_builtin_lower_benchmark(int argc, char** argv)
{
    int only = -1;
    uint32_t after_ms = 0;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : -1;
            valid = only >= 0;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end = 0;
            const unsigned long value = strtoul(argv[i], &end, 10);
            valid = argv[i][0] != 0 && *end == 0 && value <= 1000;
            after_ms = valid ? (uint32_t)value : 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: build/compiler benchmark-builtin-lowering [--only scalar|vector] [--after-scalar MS <= 1000]\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "benchmark-builtin-lowering: table initialization failed\n");
    }
    else
    {
        const CBuiltinLowerBenchmarkShape shapes[4] =
        {
            {
                .name = "popcount32",
                .call =
                {
                    .id = CBUILTIN_POPCOUNT,
                    .a = 0,
                },
            },
            {
                .name = "bswap64",
                .call =
                {
                    .id = CBUILTIN_BSWAP64,
                    .a = 0,
                },
            },
            {
                .name = "mul-I64-U32-to-I16",
                .call =
                {
                    .id = CBUILTIN_MUL_OVERFLOW,
                    .a = 0,
                    .b = 1,
                    .destination = 2,
                    .a_type = 64 | BUSTER_CBUILTIN_SIGNED,
                    .b_type = 32,
                    .result_type = 16 | BUSTER_CBUILTIN_SIGNED,
                },
            },
            {
                .name = "add-U8-bool-to-bool",
                .call =
                {
                    .id = CBUILTIN_ADD_OVERFLOW,
                    .a = 0,
                    .b = 1,
                    .destination = 2,
                    .a_type = 8,
                    .b_type = 1,
                    .result_type = 1,
                },
            },
        };
        Arena arena = arena_reserve(BUSTER_CBUILTIN_LOWER_BENCHMARK_ARENA_BYTES);
        uint64_t checksum = 0;
        valid = arena.base != 0;
        printf("benchmark-builtin-lowering: synthetic typed-call expansion + handoff + Tier 0 + branch sizing + encoding + use + rewind\n");
        printf("storage: stream=%zu B recipe=%zu B reserved-output-arena=%" PRIu64 " B; ns/function excludes initialization and validation\n",
               sizeof(CBuiltinLowerTestStream), sizeof(CBuiltinLowerRecipe), arena.capacity);
        for (uint32_t shape = 0; valid && shape < 4; shape += 1)
        {
            valid = cbuiltin_lower_benchmark_shape(&arena, &shapes[shape], only, after_ms, &checksum);
        }
        uint64_t scratch_reserved = 0;
        for (uint32_t i = 0; i < BUSTER_SCRATCH_ARENA_COUNT; i += 1)
        {
            scratch_reserved += thread_context.scratch[i].capacity;
        }
        printf("scratch virtual reservation=%" PRIu64 " B (logical high-water unavailable); checksum=%" PRIu64 "\n", scratch_reserved, checksum);
        arena_release(&arena);
        result = valid ? 0 : 1;
    }
    return result;
}
