// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original measurement probe for Buster issue #111. Not a production driver.
#ifndef BUSTER_HEADER111_PROBE_INCLUDED
#define BUSTER_HEADER111_PROBE_INCLUDED
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
static bool probe_enabled;
static uint32_t probe_depth;
static uint64_t probe_start;
static uint64_t probe_ticks;
static uint64_t probe_headers;
static uint64_t probe_builtin[128];
#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_predefines.c"
#include "buster/compiler/c_preprocessor.c"
#include "buster/compiler/c_parser.c"

static uint64_t probe_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
}

#ifndef BUSTER_PROBE_MAIN
#define BUSTER_PROBE_MAIN main
#endif
int BUSTER_PROBE_MAIN(int argc, char** argv)
{
    (void)parse_twins;
    (void)parse_postorder;
    Arena arguments = arena_reserve(16ull << 20);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    int split = 1;
    while (split < argc && strcmp(argv[split], "--") != 0)
    {
        split += 1;
    }
    const char* dummy = "probe.c";
    if (split < argc)
    {
        argv[split] = (char*)dummy;
    }
    bool ok = split < argc && pp_parse_options(split, argv + 1, &options, &arguments, &input, &output, &markers);
    static InternTable table;
    static FileCache cache;
#ifndef BUSTER_PROBE_REFERENCE
    static PPReplayCache replay;
#endif
    options.quiet = false;
    options.clang_features = true;
    options.expansion_starts = true;
    ok = ok && intern_initialize(&table) && file_cache_initialize(&cache, &table, pp_prepare_options(&options));
    options.files = &cache;
    uint64_t* expected = calloc((size_t)argc, sizeof(uint64_t));
    ok = ok && expected != 0;
    const char* passes_text = getenv("PROBE_PASSES");
    const uint32_t passes = passes_text != 0 ? (uint32_t)strtoul(passes_text, 0, 10) : 8;
    const char* condition_text = getenv("PROBE_CONDITION_MS");
    const uint32_t condition = condition_text != 0 ? (uint32_t)strtoul(condition_text, 0, 10) : 0;
    ok = ok && passes >= 2 && passes <= 100 && condition <= 1000;
    const bool parse = getenv("PROBE_PARSE") != 0;
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
        uint64_t total = 0;
#ifndef BUSTER_PROBE_REFERENCE
        if (pass % 2 != 0)
        {
            const uint64_t setup = probe_now();
            ok = ok && replay_initialize(&replay);
            total += probe_now() - setup;
        }
        options.replay = pass % 2 != 0 ? &replay : 0;
#endif
        uint64_t prelude = 0;
        uint64_t main_ticks = 0;
        uint64_t tokens = 0;
        uint64_t main_ns = 0;
        uint64_t parse_ns = 0;
        uint32_t parse_failures = 0;
        probe_ticks = 0;
        probe_headers = 0;
        for (int unit = split + 1; ok && unit < argc; unit += 1)
        {
            static Preprocessor pp;
            static Parser parser;
            const uint64_t begin = probe_now();
            ok = pp_initialize(&pp, &options, &table);
            const ParseOptions parse_options = { .tree_only = true };
            ok = ok && (!parse || parse_initialize(&parser, &parse_options, options.language));
            const uint64_t first = __rdtsc();
            ok = ok && pp_run_prelude(&pp);
            const uint64_t ready = __rdtsc();
            prelude += ready - first;
            const uint32_t file = ok ? pp_lookup_path(&pp, argv[unit], (uint32_t)strlen(argv[unit]), BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
            probe_enabled = true;
            const uint64_t main_start = probe_now();
            const uint64_t start = __rdtsc();
            ok = file != BUSTER_PP_NONE && pp.files[file].data != BUSTER_PP_NONE && pp_run_main(&pp, file);
            main_ticks += __rdtsc() - start;
            const uint64_t main_end = probe_now();
            main_ns += main_end - main_start;
            probe_enabled = false;
            tokens += pp.output.count;
            total += main_end - begin;
            uint64_t hash = 1469598103934665603ull;
            if (parse && ok)
            {
                const uint64_t parse_start = probe_now();
                const bool parsed = parse_translation_unit(&parser, &pp);
                const uint64_t elapsed = probe_now() - parse_start;
                parse_ns += elapsed;
                total += elapsed;
                parse_failures += !parsed;
                hash = (hash ^ parsed) * 1099511628211ull;
            }
            for (uint32_t t = 0; t < pp.output.count; t += 1)
            {
                hash = (hash ^ pp.output.kinds[t]) * 1099511628211ull;
                hash = (hash ^ pp.output.flags[t]) * 1099511628211ull;
                hash = (hash ^ pp.output.values[t]) * 1099511628211ull;
                hash = (hash ^ pp.output.locations[t]) * 1099511628211ull;
            }
            if (pass == 0)
            {
                expected[unit] = hash;
            }
            else if (expected[unit] != hash)
            {
                fprintf(stderr, "stream mismatch pass %u: %s\n", pass, argv[unit]);
                ok = false;
            }
            const uint64_t release_begin = probe_now();
            if (parse)
            {
                parse_release(&parser);
            }
            pp_release(&pp);
            total += probe_now() - release_begin;
            if (!ok)
            {
                fprintf(stderr, "failed: %s\n", argv[unit]);
            }
        }
#ifndef BUSTER_PROBE_REFERENCE
        const PPReplayCache report = replay;
        if (pass % 2 != 0)
        {
            const uint64_t release = probe_now();
            replay_release(&replay);
            total += probe_now() - release;
        }
#endif
        printf("pass %u units %d total_ns %lu prelude_ticks %lu main_ticks %lu header_ticks %lu headers %lu output %lu main_ns %lu parse_ns %lu parse_failures %u\n", pass,
               argc - split - 1, total, prelude, main_ticks, probe_ticks, probe_headers, tokens, main_ns, parse_ns, parse_failures);
        fflush(stdout);
#ifndef BUSTER_PROBE_REFERENCE
        printf("replay %u transcripts %u hits %lu misses %lu checks %lu reads %lu writes %lu rejected %lu bytes %lu\n", pass % 2, report.count,
               report.hits, report.misses, report.checks, report.recorded_reads, report.recorded_writes, report.rejected, report.retained.length);
        printf("rejections location %lu generated %lu variants %lu diagnostics %lu\n", report.why[0], report.why[1], report.why[2], report.why[3]);
#endif
    }
    const char* manifest = getenv("PROBE_FILES_MANIFEST");
    if (manifest != 0)
    {
        FILE* files = fopen(manifest, "w");
        ok = ok && files != 0;
        for (uint32_t p = 0; files != 0 && p < cache.path_count; p += 1)
        {
            if (cache.paths[p].record != BUSTER_PP_NONE && cache.paths[p].record != BUSTER_FILE_CACHE_PENDING)
            {
                ok &= fprintf(files, "%s\n", cache.paths[p].path) > 0;
            }
        }
        if (files != 0)
        {
            ok &= fclose(files) == 0;
        }
    }
    file_cache_release(&cache);
    intern_release(&table);
    arena_release(&arguments);
#ifndef BUSTER_PROBE_REFERENCE
    replay_release(&replay);
#endif
    free(expected);
    for (uint32_t b = 0; b < 128; b += 1)
    {
        if (probe_builtin[b] != 0) printf("builtin %u count %lu\n", b, probe_builtin[b]);
    }
    return ok ? 0 : 1;
}
#endif
