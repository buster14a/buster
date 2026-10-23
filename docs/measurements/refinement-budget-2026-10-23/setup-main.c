// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// A separate accounting control: fresh table initialization, refinement and the whole preprocessing run.

#include "buster/compiler/c_preprocessor_benchmark.c"

int main(int argc, char** argv)
{
    int result = 1;
    if (argc == 1)
    {
        result = run_preprocessor_benchmark(argc, argv);
    }
    else
    {
        Arena arena = arena_reserve(1ull << 30);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        bool passed = arena.base != 0 && pp_parse_options(argc - 1, argv + 1, &options, &arena, &input, &output, &markers);
        uint64_t setup[21];
        uint64_t refine[21];
        uint64_t whole[21];
        uint64_t digest = 0;
        uint64_t input_tokens = 0;
        uint64_t output_tokens = 0;
        uint64_t buckets = 0;
        for (uint32_t pass = 0; passed && pass <= 21; pass += 1)
        {
            InternTable table = { 0 };
            Preprocessor pp = { 0 };
            options.scalar = false;
            options.timing = true;
            const uint64_t start = pp_benchmark_nanoseconds();
            const uint64_t tick = __rdtsc();
            passed = intern_initialize(&table);
            const uint64_t initialized = pp_benchmark_nanoseconds();
            passed = passed && pp_initialize(&pp, &options, &table) && pp_run(&pp, input);
            const uint64_t elapsed_ticks = __rdtsc() - tick;
            const uint64_t end = pp_benchmark_nanoseconds();
            if (pass != 0)
            {
                setup[pass - 1] = initialized - start;
                whole[pass - 1] = end - start;
                refine[pass - 1] = elapsed_ticks != 0 ? (uint64_t)((double)pp.ticks[PP_PHASE_REFINE] * (double)(end - start) / (double)elapsed_ticks) : 0;
            }
            uint64_t current = 14695981039346656037ull;
            for (uint32_t i = 0; i < pp.output.count; i += 1)
            {
                current = (current ^ pp.output.locations[i]) * 1099511628211ull;
                current = (current ^ pp.output.values[i]) * 1099511628211ull;
                current = (current ^ pp.output.kinds[i] ^ ((uint64_t)pp.output.flags[i] << 8)) * 1099511628211ull;
            }
            passed = passed && (pass == 0 || current == digest);
            digest = current;
            input_tokens = pp.input_tokens;
            output_tokens = pp.output.count;
            buckets = (table.bucket_mask + 1) * BUSTER_INTERN_BUCKET_BYTES;
            pp_release(&pp);
            intern_release(&table);
        }
        if (passed)
        {
            pp_benchmark_sort(setup, 21);
            pp_benchmark_sort(refine, 21);
            pp_benchmark_sort(whole, 21);
            printf("21 passes, min/median/max ms: setup %.6f/%.6f/%.6f refine %.6f/%.6f/%.6f whole %.6f/%.6f/%.6f\n",
                   setup[0] / 1e6, setup[10] / 1e6, setup[20] / 1e6, refine[0] / 1e6, refine[10] / 1e6, refine[20] / 1e6,
                   whole[0] / 1e6, whole[10] / 1e6, whole[20] / 1e6);
            printf("input %lu output %lu digest %016lx bucket bytes %lu\n", input_tokens, output_tokens, digest, buckets);
        }
        arena_release(&arena);
        result = passed ? 0 : 1;
    }
    return result;
}
