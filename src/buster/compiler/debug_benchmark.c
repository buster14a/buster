// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic DWARF producer + sizing + emission + address finalization. No source/codegen producer or file I/O is
// timed: these numbers are a bounded module comparison, not whole -g compilation or streaming acceptance.
#include <linux/perf_event.h>
#include <sys/ioctl.h>

static uint64_t debug_benchmark_time(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * 1000000000ull + value.tv_nsec;
}

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_LINK
static int run_debug_benchmark(int argc, char** argv)
{
    uint32_t count = 4096;
    uint32_t rows = 64;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if ((strcmp(argv[i], "--functions") == 0 || strcmp(argv[i], "--rows") == 0) && i + 1 < argc)
        {
            char* end = 0;
            const unsigned long value = strtoul(argv[i + 1], &end, 10);
            valid = end != argv[i + 1] && *end == 0 && value <= 65536 && value != 0;
            if (strcmp(argv[i], "--functions") == 0)
            {
                count = (uint32_t)value;
            }
            else
            {
                rows = (uint32_t)value;
            }
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid &= (uint64_t)count * rows <= (1u << 22);
    Arena arena = arena_reserve(256ull << 20);
    DebugFunction* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, DebugFunction, count);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, (uint64_t)count * rows);
    uint32_t* lines = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, (uint64_t)count * rows);
    uint64_t* addresses = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count);
    DebugOutput output = { 0 };
    valid &= functions != 0 && offsets != 0 && lines != 0 && addresses != 0;
    const String8 file = BUSTER_S8("generated.c");
    const DebugUnit unit = {
        .directory = BUSTER_S8("."),
        .files = &file,
        .file_count = 1,
        .functions = functions,
        .function_count = count,
        .offsets = offsets,
        .lines = lines,
        .row_count = count * rows,
    };
    if (valid)
    {
        for (uint32_t function = 0; function < count; function += 1)
        {
            functions[function] = (DebugFunction){
                .name = BUSTER_S8("generated"),
                .size = rows * 8 + 1,
                .first_row = function * rows,
                .row_count = rows,
                .line = function + 1,
            };
            addresses[function] = 0x401000 + (uint64_t)function * (rows * 8 + 16);
            for (uint32_t row = 0; row < rows; row += 1)
            {
                offsets[function * rows + row] = row * 8;
                lines[function * rows + row] = function + 1 + row / 3;
            }
        }
        valid &= debug_write(&unit, &output) == DEBUG_ERROR_CAPACITY;
        output.line_addresses = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count);
        output.address_capacity = count;
        valid &= output.line_addresses != 0;
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            output.sections[section].capacity = output.sections[section].size;
            output.sections[section].bytes = arena_push(&arena, output.sections[section].size, 64);
            valid &= output.sections[section].bytes != 0;
        }
        valid &= debug_write(&unit, &output) == DEBUG_OK && debug_finalize(&unit, &output, addresses) == DEBUG_OK;
    }
    if (valid)
    {
        uint64_t samples[21] = { 0 };
        uint64_t cycle_samples[21] = { 0 };
        struct perf_event_attr attribute = {
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
        const uint32_t iterations = unit.row_count < 256 ? 4096 : unit.row_count < 262144 ? 262144 / unit.row_count : 1;
        for (uint32_t pass = 0; valid && pass < 21; pass += 1)
        {
            if (counter >= 0)
            {
                cycles_available &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
            }
            const uint64_t start = debug_benchmark_time();
            for (uint32_t iteration = 0; iteration < iterations; iteration += 1)
            {
                valid &= debug_write(&unit, &output) == DEBUG_OK && debug_finalize(&unit, &output, addresses) == DEBUG_OK;
            }
            const uint64_t elapsed = debug_benchmark_time() - start;
            uint64_t cycles[3] = { 0 };
            if (counter >= 0)
            {
                cycles_available &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, cycles, sizeof(cycles)) == sizeof(cycles);
                cycles_available &= cycles[1] == cycles[2] && cycles[2] != 0;
            }
            uint32_t at = pass;
            while (at != 0 && samples[at - 1] > elapsed)
            {
                samples[at] = samples[at - 1];
                at -= 1;
            }
            samples[at] = elapsed;
            at = pass;
            while (at != 0 && cycle_samples[at - 1] > cycles[0])
            {
                cycle_samples[at] = cycle_samples[at - 1];
                at -= 1;
            }
            cycle_samples[at] = cycles[0];
        }
        uint64_t output_bytes = 0;
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            output_bytes += output.sections[section].size;
        }
        printf("debug: %u functions, %u rows, %llu output bytes; no emitter scratch; 21 passes, %u iterations\n", count, unit.row_count, (unsigned long long)output_bytes, iterations);
        const double denominator = (double)unit.row_count * iterations;
        printf("  %.3f ns/row [%.3f, %.3f]; %.3f ms/unit; %.3f GB/s", samples[10] / denominator, samples[0] / denominator, samples[20] / denominator,
               (double)samples[10] / iterations / 1000000, (double)output_bytes * iterations / samples[10]);
        if (cycles_available)
        {
            printf("; %.3f cycles/row\n", cycle_samples[10] / denominator);
        }
        else
        {
            printf("; cycles unavailable\n");
        }
        if (counter >= 0)
        {
            close(counter);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "debug benchmark: invalid arguments, allocation failure or a failed write\nusage: benchmark-debug [--functions N] [--rows N]\n");
    }
    arena_release(&arena);
    return valid ? 0 : 1;
}
#endif
