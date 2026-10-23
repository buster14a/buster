// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Whole-batch observation, including fresh FileCache/intern/cache lifetimes.
// Includes the frozen kernels; output checking is charged to both variants.
#define BUSTER_PROBE_MAIN unused_sampled_benchmark_command
#include "probe.c"

int main(int argc, char** argv)
{
    Arena arguments = arena_reserve(16ull << 20);
    PPOptions options = { 0 };
    const char* input;
    const char* output;
    bool markers;
    int split = 1;
    while (split < argc && strcmp(argv[split], "--") != 0)
    {
        split += 1;
    }
    if (split < argc)
    {
        argv[split] = "dummy.c";
    }
    bool ok = split < argc && pp_parse_options(split, argv + 1, &options, &arguments, &input, &output, &markers);
    options.clang_features = true;
    options.expansion_starts = true;
    const char* passes_text = getenv("PROBE_PASSES");
    const uint32_t passes = passes_text != 0 ? (uint32_t)strtoul(passes_text, 0, 10) : 8;
    const char* condition_text = getenv("PROBE_CONDITION_MS");
    const uint32_t condition = condition_text != 0 ? (uint32_t)strtoul(condition_text, 0, 10) : 0;
    const bool parse = getenv("PROBE_PARSE") != 0;
    uint64_t* expected = calloc((size_t)argc, sizeof(uint64_t));
    ok = ok && expected != 0 && passes >= 2 && passes <= 100 && condition <= 1000;
    for (uint32_t pass = 0; ok && pass < passes; pass += 1)
    {
        if (condition != 0)
        {
            const uint64_t stop = probe_now() + (uint64_t)condition * 1000000;
            uint64_t value = 111;
            while (probe_now() < stop)
            {
                for (uint32_t i = 0; i < 1000; i += 1)
                {
                    value = (value ^ (value >> 13)) * 6364136223846793005ull + 1;
                }
                __asm__ volatile("" : "+r"(value));
            }
        }
        static InternTable table;
        static FileCache files;
#ifndef BUSTER_PROBE_REFERENCE
        static PPReplayCache cache;
#endif
        uint64_t pp_ns = 0;
        uint64_t main_ns = 0;
        uint64_t main_ticks = 0;
        uint64_t parse_ns = 0;
        uint64_t tokens = 0;
        uint32_t parse_failures = 0;
        probe_depth = 0;
        probe_ticks = 0;
        probe_headers = 0;
        const uint64_t begin = probe_now();
        ok = intern_initialize(&table) && file_cache_initialize(&files, &table, pp_prepare_options(&options));
        options.files = &files;
#ifndef BUSTER_PROBE_REFERENCE
        const bool replay = pass % 2 != 0;
        ok = ok && (!replay || replay_initialize(&cache));
        options.replay = replay ? &cache : 0;
#endif
        for (int unit = split + 1; ok && unit < argc; unit += 1)
        {
            static Preprocessor pp;
            static Parser parser;
            const uint64_t pp_begin = probe_now();
            ok = pp_initialize(&pp, &options, &table) && pp_run_prelude(&pp);
            const ParseOptions parse_options = {
                .tree_only = true
            };
            ok = ok && (!parse || parse_initialize(&parser, &parse_options, options.language));
            const uint32_t file = ok ? pp_lookup_path(&pp, argv[unit], (uint32_t)strlen(argv[unit]), BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
            const uint64_t main_begin = probe_now();
            const uint64_t tick_begin = __rdtsc();
            probe_enabled = true;
            ok = file != BUSTER_PP_NONE && pp.files[file].data != BUSTER_PP_NONE && pp_run_main(&pp, file);
            probe_enabled = false;
            main_ticks += __rdtsc() - tick_begin;
            const uint64_t main_end = probe_now();
            main_ns += main_end - main_begin;
            pp_ns += main_end - pp_begin;
            uint64_t digest = 1469598103934665603ull;
            if (parse && ok)
            {
                const uint64_t parse_begin = probe_now();
                const bool parsed = parse_translation_unit(&parser, &pp);
                parse_ns += probe_now() - parse_begin;
                parse_failures += !parsed;
                digest = (digest ^ parsed) * 1099511628211ull;
            }
            tokens += pp.output.count;
            for (uint32_t token = 0; token < pp.output.count; token += 1)
            {
                digest = (digest ^ pp.output.kinds[token]) * 1099511628211ull;
                digest = (digest ^ pp.output.flags[token]) * 1099511628211ull;
                digest = (digest ^ pp.output.values[token]) * 1099511628211ull;
                digest = (digest ^ pp.output.locations[token]) * 1099511628211ull;
            }
            if (pass == 0)
            {
                expected[unit] = digest;
            }
            else if (expected[unit] != digest)
            {
                fprintf(stderr, "stream mismatch pass %u: %s\n", pass, argv[unit]);
                ok = false;
            }
            if (parse)
            {
                parse_release(&parser);
            }
            const uint64_t release = probe_now();
            pp_release(&pp);
            pp_ns += probe_now() - release;
        }
#ifndef BUSTER_PROBE_REFERENCE
        const PPReplayCache report = cache;
        replay_release(&cache);
#endif
        file_cache_release(&files);
        intern_release(&table);
        const uint64_t elapsed = probe_now() - begin;
        printf("pass %u units %d total_ns %lu pp_ns %lu main_ns %lu main_ticks %lu header_ticks %lu headers %lu output %lu parse_ns %lu parse_failures %u\n", pass, argc - split - 1, elapsed, pp_ns, main_ns, main_ticks, probe_ticks, probe_headers, tokens, parse_ns, parse_failures);
#ifndef BUSTER_PROBE_REFERENCE
        printf("replay %u transcripts %u hits %lu misses %lu checks %lu reads %lu writes %lu rejected %lu bytes %lu\n", replay, report.count, report.hits, report.misses, report.checks, report.recorded_reads, report.recorded_writes, report.rejected, report.retained.length);
#endif
        fflush(stdout);
    }
    free(expected);
    arena_release(&arguments);
    return ok ? 0 : 1;
}
