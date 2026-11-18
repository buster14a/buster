// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original measurement harness for diagnostic ordering. Included after diagnostic_test.c, whose frozen key generator
// supplies the input and whose diagnostic_test_sorted checks every sample's result against the sort's definition.
// diagnostic_sort sorts copies back to back; copying is included. --after-scalar MS conditions each sample with MS ms
// of scalar integer work over the keys (diagnostic_benchmark_scalar_work), the state a compiler's vector phases run in
// after lighter ones. Report elapsed nanoseconds as well as invariant-TSC ticks, not core-cycle counters.

static uint64_t diagnostic_benchmark_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
}

static uint64_t diagnostic_benchmark_median(uint64_t* values, uint32_t count)
{
    for (uint32_t i = 0; i <= count / 2; i += 1)
    {
        uint32_t smallest = i;
        for (uint32_t j = i + 1; j < count; j += 1)
        {
            smallest = values[j] < values[smallest] ? j : smallest;
        }
        const uint64_t swap = values[i];
        values[i] = values[smallest];
        values[smallest] = swap;
    }
    return values[count / 2];
}

// Scalar work for --after-scalar: a dependent chain of multiplies over the keys, which the compiler can't vectorize.
static __attribute__((noinline)) uint64_t diagnostic_benchmark_scalar_work(const DiagnosticOrder* input, uint32_t count)
{
    uint64_t hash = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        hash = hash * 0x100000001b3ull ^ input[i].source ^ input[i].sequence;
    }
    return hash;
}

static int run_diagnostic_benchmark(int argc, char** argv)
{
    uint64_t after_scalar = 0;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end;
            errno = 0;
            after_scalar = strtoull(argv[i], &end, 10);
            valid = errno == 0 && end != argv[i] && *end == 0 && argv[i][0] != '-' && after_scalar <= 1000;
        }
        else
        {
            valid = false;
        }
    }
    static const uint32_t counts[] =
    {
        32,
        64,
        256,
        4096,
    };
    Arena arena = arena_reserve(1 << 20);
    DiagnosticOrder* input = BUSTER_ARENA_PUSH_ARRAY(&arena, DiagnosticOrder, 4096);
    DiagnosticOrder* items = BUSTER_ARENA_PUSH_ARRAY(&arena, DiagnosticOrder, 4096);
    DiagnosticOrder* scratch = BUSTER_ARENA_PUSH_ARRAY(&arena, DiagnosticOrder, 4096);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, 4096);
    bool result = valid && input != 0 && items != 0 && scratch != 0 && seen != 0;
    uint64_t seed = 153;
    volatile uint64_t observed = 0;
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-diagnostics [--after-scalar MS (0..1000)]\n");
    }
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        const uint32_t count = counts[c];
        // Isolated unconditioned samples run long enough to measure sustained sorting. Conditioned samples retain
        // the short batch so its initial transition is not averaged over a long vector phase.
        const uint32_t repeats = count < 256 ? 256 : count < 4096 ? 16 : after_scalar == 0 ? 32 : 1;
        const uint64_t bytes = (uint64_t)count * sizeof(DiagnosticOrder);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint64_t source = diagnostic_test_random(&seed);
            const uint64_t sequence = diagnostic_test_random(&seed);
            input[i] = (DiagnosticOrder)
            {
                .source = source,
                .sequence = sequence,
                .record = i,
            };
        }
        uint64_t times[21];
        uint64_t ticks[21];
        for (uint32_t pass = 0; result && pass < 22; pass += 1)
        {
            const uint64_t until = diagnostic_benchmark_nanoseconds() + after_scalar * 1000000;
            while (diagnostic_benchmark_nanoseconds() < until)
            {
                observed = diagnostic_benchmark_scalar_work(input, count);
            }
            const uint64_t start_ns = diagnostic_benchmark_nanoseconds();
            _mm_lfence();
            const uint64_t start_ticks = __rdtsc();
            for (uint32_t repeat = 0; repeat < repeats; repeat += 1)
            {
                memcpy(items, input, bytes);
                diagnostic_sort(items, count, scratch);
                observed = items[0].source;
            }
            _mm_lfence();
            const uint64_t end_ticks = __rdtsc();
            const uint64_t end_ns = diagnostic_benchmark_nanoseconds();
            if (pass != 0)
            {
                times[pass - 1] = end_ns - start_ns;
                ticks[pass - 1] = end_ticks - start_ticks;
            }
            result = diagnostic_test_sorted(input, items, count, seen);
        }
        if (result)
        {
            const double work = (double)count * repeats;
            const uint64_t ns = diagnostic_benchmark_median(times, 21);
            const uint64_t tsc = diagnostic_benchmark_median(ticks, 21);
            printf("diagnostics %4u: %.2f ns/key, %.2f TSC ticks/key, after-scalar %llu ms\n", count, ns / work, tsc / work, (unsigned long long)after_scalar);
        }
    }
    (void)observed;
    arena_release(&arena);
    return result ? 0 : 1;
}
