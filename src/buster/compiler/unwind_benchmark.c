// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The available backend chain, with opt-in CFI charged to its producer: frozen typed functions -> Tier0 -> sizing ->
// encoding -> optional frame analysis and batch CFI emission. Every path uses identical vector code generation. Input
// generation, ABI assignment and one-time initialization are outside timing; per-function allocation/cleanup is inside.
// This does not measure source mapping, file output, parsing, or overlap between compilation and writing. The fourth
// path is the C driver's (#395): the frame Tier 0 published (unwind_tier0_frame) rather than the analysis, and the
// vector writer's batch with one shared CIE.

typedef struct UnwindBenchmarkResult UnwindBenchmarkResult;
struct UnwindBenchmarkResult
{
    uint64_t records;
    uint64_t bytes;
    uint64_t hash;
    uint64_t peak;
    bool valid;
};

static UnwindBenchmarkResult unwind_benchmark_chain(const Tier0Function* functions, uint32_t count, uint32_t way, Arena* arena, UnwindFunction* descriptions,
                                                    UnwindOutput* output, bool verify)
{
    UnwindBenchmarkResult result = {
        .hash = 14695981039346656037ull,
        .valid = true,
    };
    for (uint32_t function = 0; result.valid && function < count; function += 1)
    {
        const Tier0Code code = tier0_compile(&functions[function], arena);
        result.valid = code.error == TIER0_OK;
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, code.count);
        uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code.reference_count));
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code.count);
        uint8_t* bytes = arena_push(arena, capacity, 16);
        result.valid &= offsets != 0 && ends != 0 && bytes != 0;
        if (result.valid)
        {
            const BranchSizing sizing = size_branches(code.records, code.count, code.branches, code.targets, code.branch_count, offsets);
            const int64_t length = sizing.error == BRANCH_SIZING_OK ? x86_encode(code.records, code.count, bytes, capacity, 0, ends) : -1;
            result.valid &= length >= 0 && (uint64_t)length == sizing.length;
            if (result.valid && way != 0)
            {
                const UnwindError error = way == 1 ? unwind_tier0_scalar(&code, offsets, (uint32_t)length, &descriptions[function]) :
                                          way == 2 ? unwind_tier0(&code, offsets, (uint32_t)length, &descriptions[function]) :
                                                     unwind_tier0_frame(&code, offsets, (uint32_t)length, &descriptions[function]);
                result.valid &= error == UNWIND_OK;
            }
            if (result.valid)
            {
                result.records += code.count;
                result.bytes += (uint64_t)length;
                for (uint64_t byte = 0; verify && byte < (uint64_t)length; byte += 1)
                {
                    result.hash = (result.hash ^ bytes[byte]) * 1099511628211ull;
                }
            }
        }
        result.peak = arena->length > result.peak ? arena->length : result.peak;
        arena_pop_to(arena, 0);
    }
    if (result.valid && way != 0)
    {
        result.valid = unwind_write(descriptions, count, output, way == 1) == UNWIND_OK;
    }
    return result;
}

static int run_unwind_benchmark(int argc, char** argv)
{
    uint32_t count = 256;
    uint32_t only = 15;
    uint32_t after_scalar = 0;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "baseline") == 0 ? 1 : strcmp(argv[i], "scalar") == 0 ? 2 : strcmp(argv[i], "vector") == 0 ? 4 : strcmp(argv[i], "published") == 0 ? 8 : 0;
            valid = only != 0;
        }
        else if ((strcmp(argv[i], "--functions") == 0 || strcmp(argv[i], "--after-scalar") == 0) && i + 1 < argc)
        {
            char* end = 0;
            const unsigned long value = strtoul(argv[i + 1], &end, 10);
            valid = end != argv[i + 1] && *end == 0;
            if (strcmp(argv[i], "--functions") == 0)
            {
                valid &= value != 0 && value <= 4096;
                count = (uint32_t)value;
            }
            else
            {
                valid &= value <= 1000;
                after_scalar = (uint32_t)value;
            }
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid &= x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize();
    Tier0Tables tables = { 0 };
    valid = valid && tier0_tables_open(&tables);
    Arena input = arena_reserve(1ull << 32);
    Arena temporary = arena_reserve(64ull << 20);
    Tier0Function* functions = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Function, count);
    // The scalar path's descriptions and pairs, the vector path's, the published path's with one CIE, and the exact
    // writer's of the same with one CIE, which the published path's must be.
    UnwindFunction* descriptions[4];
    UnwindOutput output[4] = { 0 };
    for (uint32_t way = 0; way < 4; way += 1)
    {
        descriptions[way] = BUSTER_ARENA_PUSH_ARRAY(&input, UnwindFunction, count);
        output[way].capacity = (uint64_t)count * 192;
        output[way].bytes = arena_push(&input, output[way].capacity, 64);
        output[way].location_capacity = count;
        output[way].locations = BUSTER_ARENA_PUSH_ARRAY(&input, uint32_t, count);
        output[way].shared = way >= 2;
        valid &= descriptions[way] != 0 && output[way].bytes != 0 && output[way].locations != 0;
    }
    valid &= functions != 0 && temporary.base != 0;
    uint64_t entries = 0;
    for (uint32_t first = 0; valid && first < count;)
    {
        const uint32_t shape = (first / 8) % 4;
        const Tier0ProgramOptions options = {
            .functions = count - first < 8 ? count - first : 8,
            .statements = shape == 0 ? 6 : shape == 1 ? 12 : 24,
            .expression_nodes = shape == 0 ? 6 : 10,
            .calls = shape != 0,
            .loops = shape != 0,
            .floating = shape >= 2,
            .pointers = shape == 3,
        };
        Tier0Program program = { 0 };
        Tier0Functions generated = { 0 };
        valid &= tier0_generate_program(&program, &input, 8800003ull + first, options, "unwind_bench") &&
                 tier0_program_functions(&program, &tables, &input, &generated);
        for (uint32_t i = 0; valid && i < generated.count; i += 1)
        {
            functions[first + i] = generated.functions[i];
            entries += generated.functions[i].count;
            valid &= tier0_validate(&functions[first + i]).error == TIER0_OK;
        }
        first += options.functions;
    }
    UnwindBenchmarkResult controls[4] = { 0 };
    for (uint32_t way = 0; valid && way < 4; way += 1)
    {
        const uint32_t index = way == 0 ? 0 : way - 1;
        controls[way] = unwind_benchmark_chain(functions, count, way, &temporary, descriptions[index], &output[index], true);
        valid &= controls[way].valid && controls[way].records == controls[0].records && controls[way].bytes == controls[0].bytes && controls[way].hash == controls[0].hash;
    }
    valid = valid && output[0].size == output[1].size && memcmp(output[0].bytes, output[1].bytes, output[0].size) == 0 &&
            memcmp(output[0].locations, output[1].locations, (uint64_t)count * sizeof(uint32_t)) == 0 &&
            memcmp(descriptions[0], descriptions[1], (uint64_t)count * sizeof(UnwindFunction)) == 0 &&
            memcmp(descriptions[0], descriptions[2], (uint64_t)count * sizeof(UnwindFunction)) == 0;
    valid = valid && unwind_write(descriptions[2], count, &output[3], true) == UNWIND_OK && output[2].size == output[3].size &&
            memcmp(output[2].bytes, output[3].bytes, output[2].size) == 0 && memcmp(output[2].locations, output[3].locations, (uint64_t)count * sizeof(uint32_t)) == 0;
    if (valid)
    {
        const struct perf_event_attr attribute = {
            .type = PERF_TYPE_HARDWARE,
            .size = sizeof(struct perf_event_attr),
            .config = PERF_COUNT_HW_CPU_CYCLES,
            .disabled = 1,
            .exclude_kernel = 1,
            .exclude_hv = 1,
            .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
        };
        const int counter = (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
        bool cycles_available = counter >= 0;
        uint64_t samples[4][21] = { 0 };
        uint64_t cycles[4][21] = { 0 };
        for (uint32_t pass = 0; valid && pass < 21; pass += 1)
        {
            for (uint32_t order = 0; valid && order < 4; order += 1)
            {
                const uint32_t way = (order + pass) % 4;
                if ((only & (1u << way)) != 0)
                {
                    const uint32_t index = way == 0 ? 0 : way - 1;
                    const uint64_t until = debug_benchmark_time() + (uint64_t)after_scalar * 1000000;
                    // The same light scalar-emission workload precedes every compared path. Calling the scalar
                    // CFI chain here would still run vector Tier0/sizing/encoding and would not isolate this state.
                    while (valid && after_scalar != 0 && debug_benchmark_time() < until)
                    {
                        valid &= unwind_write(descriptions[0], count, &output[0], true) == UNWIND_OK;
                    }
                    if (counter >= 0)
                    {
                        cycles_available &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
                    }
                    const uint64_t start = debug_benchmark_time();
                    const UnwindBenchmarkResult measured = unwind_benchmark_chain(functions, count, way, &temporary, descriptions[index], &output[index], false);
                    const uint64_t elapsed = debug_benchmark_time() - start;
                    valid &= measured.valid && measured.records == controls[0].records && measured.bytes == controls[0].bytes;
                    uint64_t reading[3] = { 0 };
                    if (counter >= 0)
                    {
                        cycles_available &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, reading, sizeof(reading)) == sizeof(reading);
                        cycles_available &= reading[1] == reading[2] && reading[2] != 0;
                    }
                    uint32_t at = pass;
                    while (at != 0 && samples[way][at - 1] > elapsed)
                    {
                        samples[way][at] = samples[way][at - 1];
                        at -= 1;
                    }
                    samples[way][at] = elapsed;
                    at = pass;
                    while (at != 0 && cycles[way][at - 1] > reading[0])
                    {
                        cycles[way][at] = cycles[way][at - 1];
                        at -= 1;
                    }
                    cycles[way][at] = reading[0];
                }
            }
        }
        if (valid)
        {
            printf("unwind chain: %u functions, %" PRIu64 " typed entries, %" PRIu64 " records, %" PRIu64 " code bytes, %" PRIu64 " CFI bytes (%" PRIu64
                   " with one CIE); peak temporary %" PRIu64 " bytes; descriptions %zu bytes/function; 21 passes, scalar-emitter conditioning %u ms\n", count, entries,
                   controls[0].records, controls[0].bytes, output[0].size, output[2].size, controls[0].peak, sizeof(UnwindFunction), after_scalar);
            const char* names[] = { "without CFI", "scalar CFI", "vector CFI", "published CFI" };
            for (uint32_t way = 0; way < 4; way += 1)
            {
                if ((only & (1u << way)) != 0)
                {
                    printf("  %s: %.3f ns/entry [%.3f, %.3f]; %.3f ms/unit", names[way], (double)samples[way][10] / entries,
                           (double)samples[way][0] / entries, (double)samples[way][20] / entries, samples[way][10] / 1000000.0);
                    if (cycles_available)
                    {
                        printf("; %.3f cycles/entry\n", (double)cycles[way][10] / entries);
                    }
                    else
                    {
                        printf("; cycles unavailable\n");
                    }
                }
            }
        }
        if (counter >= 0)
        {
            close(counter);
        }
    }
    arena_release(&temporary);
    arena_release(&input);
    tier0_tables_close(&tables);
    if (!valid)
    {
        fprintf(stderr, "benchmark-unwind: invalid arguments or backend/CFI contract failure\n");
    }
    return valid ? 0 : 1;
}
