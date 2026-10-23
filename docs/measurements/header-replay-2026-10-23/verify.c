// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Full stream, history and printed-output comparison for the isolated #111 experiment.
#ifndef BUSTER_HEADER111_VERIFY_INCLUDED
#define BUSTER_HEADER111_VERIFY_INCLUDED
#define main unused_probe_main
#include "probe.c"
#undef main

static bool verify_bytes(const char* field, const void* a, const void* b, uint64_t count, uint32_t size)
{
    const bool result = count == 0 || memcmp(a, b, count * size) == 0;
    if (!result)
    {
        fprintf(stderr, "mismatch: %s\n", field);
    }
    return result;
}

static bool verify_print(Preprocessor* a, Preprocessor* b, uint32_t mode)
{
    char* texts[2] = { 0 };
    size_t lengths[2] = { 0 };
    bool result = true;
    for (uint32_t v = 0; result && v < 2; v += 1)
    {
        FILE* stream = open_memstream(&texts[v], &lengths[v]);
        Preprocessor* pp = v == 0 ? a : b;
        result = stream != 0 && (mode == 2 ? pp_print_macros(pp, stream) : pp_print(pp, stream, mode != 0));
        if (stream != 0)
        {
            result &= fclose(stream) == 0;
        }
    }
    result = result && lengths[0] == lengths[1] && verify_bytes(mode == 2 ? "macro table" : mode == 1 ? "line markers" : "-P text", texts[0], texts[1], lengths[0], 1);
    free(texts[0]);
    free(texts[1]);
    return result;
}

#ifndef BUSTER_VERIFY_MAIN
#define BUSTER_VERIFY_MAIN main
#endif
int BUSTER_VERIFY_MAIN(int argc, char** argv)
{
    setenv("SOURCE_DATE_EPOCH", "1767225600", 1);
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
    if (split < argc)
    {
        argv[split] = "dummy.c";
    }
    bool ok = split < argc && pp_parse_options(split, argv + 1, &options, &arguments, &input, &output, &markers);
    static InternTable table;
    static FileCache files;
    static PPReplayCache replay;
    options.clang_features = true;
    options.expansion_starts = true;
    options.scalar |= getenv("VERIFY_PREPROCESSOR_SCALAR") != 0;
    ok = ok && intern_initialize(&table) && file_cache_initialize(&files, &table, pp_prepare_options(&options)) && replay_initialize(&replay);
    options.files = &files;
    replay.scalar_copy = getenv("VERIFY_CACHE_SCALAR") != 0;
    uint64_t units = 0;
    uint64_t tokens = 0;
    for (uint32_t repeat = 0; ok && repeat < 3; repeat += 1)
    {
        for (int unit = split + 1; ok && unit < argc; unit += 1)
        {
            static Preprocessor runs[2];
            bool accepted[2] = { false, false };
            char* diagnostics[2] = { 0 };
            size_t lengths[2] = { 0 };
            FILE* streams[2] = { 0 };
            for (uint32_t v = 0; ok && v < 2; v += 1)
            {
                PPOptions with = options;
                with.replay = v != 0 ? &replay : 0;
                streams[v] = open_memstream(&diagnostics[v], &lengths[v]);
                with.diagnostics = streams[v];
                ok = streams[v] != 0 && pp_initialize(&runs[v], &with, &table);
                accepted[v] = ok && pp_run(&runs[v], argv[unit]);
                if (streams[v] != 0)
                {
                    fclose(streams[v]);
                }
            }
            Preprocessor* a = &runs[0];
            Preprocessor* b = &runs[1];
            ok = ok && accepted[0] == accepted[1] && a->errors == b->errors && a->warnings == b->warnings && a->fatal == b->fatal;
            ok = ok && lengths[0] == lengths[1] && verify_bytes("diagnostics", diagnostics[0], diagnostics[1], lengths[0], 1);
            ok = ok && a->output.count == b->output.count;
            ok = ok && verify_bytes("kinds", a->output.kinds, b->output.kinds, a->output.count, 1);
            ok = ok && verify_bytes("flags", a->output.flags, b->output.flags, a->output.count, 1);
            ok = ok && verify_bytes("values", a->output.values, b->output.values, a->output.count, 4);
            ok = ok && verify_bytes("locations", a->output.locations, b->output.locations, a->output.count, 4);
#define VERIFY_TABLE(field, count, type) \
            ok = ok && a->count == b->count && verify_bytes(#field, a->field, b->field, a->count, sizeof(type));
            VERIFY_TABLE(history, history_count, PPExpansion)
            VERIFY_TABLE(substitutions, substitution_count, PPSubstitution)
            VERIFY_TABLE(generated, generated_count, PPGenerated)
            VERIFY_TABLE(history_sources, history_source_count, PPSource)
            VERIFY_TABLE(history_runs, history_run_count, PPHistoryRun)
            VERIFY_TABLE(expansion_starts, expansion_start_count, PPExpansionStart)
#undef VERIFY_TABLE
            for (uint32_t mode = 0; ok && mode < 3; mode += 1)
            {
                ok = verify_print(a, b, mode);
            }
            if (!ok)
            {
                fprintf(stderr, "FAIL repeat %u unit %s history %u/%u substitutions %u/%u generated %u/%u starts %u/%u hits %lu\n", repeat, argv[unit],
                        a->history_count, b->history_count, a->substitution_count, b->substitution_count, a->generated_count, b->generated_count,
                        a->expansion_start_count, b->expansion_start_count, replay.hits);
            }
            tokens += a->output.count;
            units += 1;
            pp_release(a);
            pp_release(b);
            free(diagnostics[0]);
            free(diagnostics[1]);
        }
    }
    printf("verify ok=%u units=%lu tokens=%lu transcripts=%u hits=%lu checks=%lu retained=%lu\n", ok, units, tokens, replay.count, replay.hits, replay.checks, replay.retained.length);
    replay_release(&replay);
    file_cache_release(&files);
    intern_release(&table);
    arena_release(&arguments);
    return ok ? 0 : 1;
}
#endif
