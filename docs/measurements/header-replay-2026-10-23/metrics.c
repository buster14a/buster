// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Inspect a fresh cache after one batch. This is a diagnostic, never a timer.
// It includes the frozen experiment without changing its measured source.
#define BUSTER_PROBE_MAIN unused_benchmark_command
#include "probe.c"

static void metrics_path(const char* path)
{
    fputc('"', stdout);
    for (uint32_t at = 0; path[at] != 0; at += 1)
    {
        if (path[at] == '"')
        {
            fputc('"', stdout);
        }
        fputc(path[at], stdout);
    }
    fputc('"', stdout);
}

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
    static InternTable table;
    static FileCache files;
    static PPReplayCache cache;
    options.clang_features = true;
    options.expansion_starts = true;
    ok = ok && intern_initialize(&table) && file_cache_initialize(&files, &table, pp_prepare_options(&options));
    options.files = &files;
    for (uint32_t pass = 0; ok && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            ok = replay_initialize(&cache);
            options.replay = &cache;
        }
        for (int unit = split + 1; ok && unit < argc; unit += 1)
        {
            static Preprocessor pp;
            ok = pp_initialize(&pp, &options, &table) && pp_run(&pp, argv[unit]);
            pp_release(&pp);
        }
    }
    if (ok)
    {
        printf("file,variants,transcript,reads,writes,output,ranges,scratch,history,substitutions,generated,sources\n");
        for (uint32_t at = 0; at < cache.count; at += 1)
        {
            const ReplayTranscript* transcript = &cache.transcripts[at];
            uint32_t variants = 0;
            for (uint32_t item = cache.heads[transcript->record]; item != 0; item = cache.transcripts[item - 1].next)
            {
                variants += 1;
            }
            metrics_path(transcript->files[0].path);
            printf(",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", variants, at, transcript->read_count, transcript->write_count, transcript->output_count, transcript->range_count, transcript->scratch_count, transcript->history_count, transcript->substitution_count, transcript->generated_count, transcript->source_count);
        }
        ok = fflush(stdout) == 0;
        fprintf(stderr, "units=%d transcripts=%u hits=%lu misses=%lu checks=%lu reads=%lu writes=%lu rejected=%lu retained=%lu\n", argc - split - 1, cache.count, cache.hits, cache.misses, cache.checks, cache.recorded_reads, cache.recorded_writes, cache.rejected, cache.retained.length);
    }
    const char* manifest = getenv("PROBE_FILES_MANIFEST");
    if (ok && manifest != 0)
    {
        FILE* stream = fopen(manifest, "w");
        ok = stream != 0;
        for (uint32_t at = 0; ok && at < files.path_count; at += 1)
        {
            if (files.paths[at].record != BUSTER_PP_NONE && files.paths[at].record != BUSTER_FILE_CACHE_PENDING)
            {
                ok = fprintf(stream, "%s\n", files.paths[at].path) > 0;
            }
        }
        if (stream != 0)
        {
            ok &= fclose(stream) == 0;
        }
    }
    ok = ok && fflush(stderr) == 0;
    replay_release(&cache);
    file_cache_release(&files);
    intern_release(&table);
    arena_release(&arguments);
    return ok ? 0 : 1;
}
