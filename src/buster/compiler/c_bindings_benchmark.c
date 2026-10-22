// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Resolver microbenchmark on a deterministic synthetic event stream; NOT corpus/parser acceptance for #128.
// Every measured result is checked against the reusable stack replay. Preparation is outside the timing; the
// batch includes key construction, sorting and scope checks. Sort alone includes key construction. --only isolates
// a path; --after-scalar MS runs the stack replay before a sample to expose the vector path after lighter work.
// CPU cycles come from perf, not TSC ticks. A denied counter is reported as unavailable, not zero cycles.

#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_C_BINDING_BENCHMARK_PASSES 21

typedef struct CBindingSample CBindingSample;
struct CBindingSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
};

static uint64_t c_binding_benchmark_time(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + value.tv_nsec;
}

static uint64_t c_binding_benchmark_median(const CBindingSample* samples, bool cycles)
{
    uint64_t ordered[BUSTER_C_BINDING_BENCHMARK_PASSES];
    for (uint32_t i = 0; i < BUSTER_C_BINDING_BENCHMARK_PASSES; i += 1)
    {
        const uint64_t value = cycles ? samples[i].cycles : samples[i].nanoseconds;
        uint32_t j = i;
        while (j != 0 && ordered[j - 1] > value)
        {
            ordered[j] = ordered[j - 1];
            j -= 1;
        }
        ordered[j] = value;
    }
    return ordered[BUSTER_C_BINDING_BENCHMARK_PASSES / 2];
}

static bool c_binding_benchmark_generate(CBindings* bindings, uint32_t count)
{
    uint32_t marks[64];
    uint32_t depth = 0;
    uint64_t random = 128;
    for (uint32_t i = 0; i < count; i += 1)
    {
        random = random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        const uint32_t word = (uint32_t)(random >> 32);
        const uint32_t name = 1 + word % (bindings->name_capacity - 1);
        const uint32_t action = word % 32;
        if (action == 0 && depth < 64)
        {
            marks[depth++] = c_bindings_mark(bindings);
        }
        else if (action == 1 && depth != 0)
        {
            c_bindings_restore(bindings, marks[--depth], i);
        }
        else if (action < 9)
        {
            c_bindings_declare(bindings, C_BINDING_ORDINARY, name, (CBindingKind)(word % 4), i, i, depth);
        }
        else
        {
            c_bindings_use(bindings, C_BINDING_ORDINARY, name, i, i);
        }
    }
    return c_bindings_restore(bindings, 0, count);
}

static int run_c_bindings_benchmark(int argc, char** argv)
{
    uint32_t count = 262144;
    uint32_t names = 8192;
    uint32_t after_scalar = 0;
    uint32_t only = 3;
    bool valid = true;
    const char* const paths[] = { "scalar", "batch", "sort" };
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = 3;
            for (uint32_t p = 0; p < 3; p += 1)
            {
                if (strcmp(argv[i], paths[p]) == 0)
                {
                    only = p;
                }
            }
            valid = only != 3;
        }
        else if ((strcmp(argv[i], "--events") == 0 || strcmp(argv[i], "--names") == 0 || strcmp(argv[i], "--after-scalar") == 0) && i + 1 < argc)
        {
            const bool events = strcmp(argv[i], "--events") == 0;
            const bool identifiers = strcmp(argv[i], "--names") == 0;
            char* end = 0;
            errno = 0;
            const unsigned long value = strtoul(argv[++i], &end, 10);
            valid = errno == 0 && *end == 0 && end != argv[i] && value <= (events ? 10000000ul : identifiers ? 1000000ul : 1000ul) && value >= (events ? 16ul : identifiers ? 2ul : 0ul);
            if (events)
            {
                count = (uint32_t)value;
            }
            else if (identifiers)
            {
                names = (uint32_t)value;
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
    Arena arena = { 0 };
    Arena scratch = { 0 };
    CBindings bindings = { 0 };
    CBindingReplay replay = { 0 };
    uint32_t* expected = 0;
    uint32_t* output = 0;
    if (valid)
    {
        arena = arena_reserve(((uint64_t)count + 16) * 64 + (uint64_t)names * 32 + 4096);
        scratch = arena_reserve(((uint64_t)count + 16) * 64 + (uint64_t)names * 16 + 4096);
        valid = c_bindings_initialize(&bindings, &arena, names, count + 2) && c_binding_benchmark_generate(&bindings, count) &&
                c_bindings_replay_initialize(&replay, &arena, names, bindings.count);
        expected = valid ? arena_push_array(&arena, (uint64_t)bindings.count + 1, 4, 64) : 0;
        output = expected != 0 ? arena_push_array(&arena, (uint64_t)bindings.count + 1, 4, 64) : 0;
        valid = output != 0 && c_bindings_replay(&bindings, &replay, expected);
    }
    uint32_t identifiers = 0;
    for (uint32_t i = 0; valid && i < bindings.count; i += 1)
    {
        identifiers += bindings.operation[i] != C_BINDING_RESTORE;
    }
    struct perf_event_attr attribute = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    const int counter = valid ? (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC) : -1;
    bool cycles_available = counter >= 0;
    CBindingSample samples[3][BUSTER_C_BINDING_BENCHMARK_PASSES] = { 0 };
    CBindingStatistics statistics = { 0 };
    for (uint32_t pass = 0; valid && pass <= BUSTER_C_BINDING_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 3; step += 1)
        {
            const uint32_t path = pass % 2 ? 2 - step : step;
            if (only == 3 || only == path)
            {
                const uint64_t until = c_binding_benchmark_time() + (uint64_t)after_scalar * 1000000;
                while (after_scalar != 0 && c_binding_benchmark_time() < until)
                {
                    valid = c_bindings_replay(&bindings, &replay, output);
                    // Keep every conditioning pass observable even if a future optimizer inlines the replay.
                    __asm__ volatile("" : : "r"(output) : "memory");
                }
                const uint64_t mark = scratch.length;
                uint64_t* keys = 0;
                uint32_t key_count = 0;
                if (counter >= 0)
                {
                    cycles_available &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
                }
                const uint64_t begin = c_binding_benchmark_time();
                if (path == 0)
                {
                    valid = c_bindings_replay(&bindings, &replay, output);
                }
                else if (path == 1)
                {
                    valid = c_bindings_batch(&bindings, output, &scratch, &statistics);
                }
                else
                {
                    valid = c_bindings_sorted(&bindings, &scratch, &keys, &key_count, true);
                }
                const uint64_t elapsed = c_binding_benchmark_time() - begin;
                uint64_t counts[3] = { 0 };
                if (counter >= 0)
                {
                    cycles_available &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts);
                    cycles_available &= counts[1] == counts[2] && counts[2] != 0;
                }
                if (pass != 0)
                {
                    samples[path][pass - 1] = (CBindingSample){ .nanoseconds = elapsed, .cycles = counts[0] };
                }
                if (path != 2)
                {
                    valid = valid && memcmp(output, expected, (uint64_t)bindings.count * 4) == 0;
                }
                else
                {
                    // Consume the timed sort's output outside the timing. Strict ordering and one valid record per
                    // key, with the exact count, also establish that no identifier event was lost or duplicated.
                    valid = valid && key_count == identifiers;
                    for (uint32_t i = 0; valid && i < key_count; i += 1)
                    {
                        const uint32_t event = (uint32_t)keys[i];
                        valid = event < bindings.count && bindings.operation[event] != C_BINDING_RESTORE &&
                                (uint32_t)(keys[i] >> 32) == bindings.key[event] && (i == 0 || keys[i - 1] < keys[i]);
                    }
                }
                arena_pop_to(&scratch, mark);
            }
        }
    }
    if (valid)
    {
        valid = c_bindings_batch(&bindings, output, &scratch, &statistics) && memcmp(output, expected, (uint64_t)bindings.count * 4) == 0;
    }
    if (valid)
    {
        printf("bindings synthetic: %llu identifiers, %llu declarations, %u names, %u passes, after-scalar %u ms\n",
               (unsigned long long)statistics.identifiers, (unsigned long long)statistics.declarations, names, BUSTER_C_BINDING_BENCHMARK_PASSES, after_scalar);
        for (uint32_t path = 0; path < 3; path += 1)
        {
            if (only == 3 || only == path)
            {
                printf("  %s: %.3f ns/identifier", paths[path], (double)c_binding_benchmark_median(samples[path], false) / statistics.identifiers);
                if (cycles_available)
                {
                    printf(", %.3f cycles/identifier", (double)c_binding_benchmark_median(samples[path], true) / statistics.identifiers);
                }
                else
                {
                    printf(", cycles unavailable");
                }
                printf("\n");
            }
        }
        printf("  walks %llu, walked uses %llu, fallback uses %llu; outputs identical\n", (unsigned long long)statistics.walks,
               (unsigned long long)statistics.walked_uses, (unsigned long long)statistics.fallback_uses);
    }
    else
    {
        fprintf(stderr, "benchmark-bindings: invalid arguments, allocation failure or mismatched output\nusage: benchmark-bindings [--events N] [--names N] [--only scalar|batch|sort] [--after-scalar MS]\n");
    }
    if (counter >= 0)
    {
        close(counter);
    }
    arena_release(&scratch);
    arena_release(&arena);
    return !valid;
}
