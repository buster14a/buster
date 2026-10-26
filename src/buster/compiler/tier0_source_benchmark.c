// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's measurement on real C (benchmark-backend-source): benchmark-backend's variants (tier0_benchmark_measure.c),
// Tier 0's vector path, its scalar twin, branch sizing and encoding, on the functions the driver's -c compiles. Include
// after c_lowering_source_benchmark.c.
//
//   build/compiler benchmark-backend-source [--only vector|scalar|sizing|encode] [--after-scalar MS]
//                                           [preprocessor options] file...
//
// Each file is checked as the driver checks it (the vector path, check_corpus_check_policy), and the definitions -c
// lowers (check_program_select_mode's C_LOWERING_REACHABLE) lowered and emitted by check_program_emit, outside the time.
// Their typed streams stay in an arena that outlives the file's other state, with the symbols' access given again
// (check_program_access), and join one corpus of every file's functions, which the benchmark measures as
// benchmark-backend measures generated ones: both paths must give each function the same records, then the medians of
// BUSTER_TIER0_BENCHMARK_PASSES passes per typed entry and per record, and the share of entries the vector path
// defers. A file the checker rejects, or whose checked program or emission refuses a feature by name
// (CHECK_PROGRAM_UNSUPPORTED), is left out with the reason. Any other failure fails the benchmark, with the file left
// out: a file that doesn't preprocess, a resource that runs out, an emission that fails otherwise, and paths that
// disagree on a function. So does a run with no function to measure. Run it under the 9700X's lock pinned to one CPU
// (taskset -c 2 ...). It is a benchmark outside the compile path, with no vector path of its own.
#ifndef BUSTER_TIER0_SOURCE_BENCHMARK_INCLUDED
#define BUSTER_TIER0_SOURCE_BENCHMARK_INCLUDED

#include "buster/compiler/tier0_benchmark_measure.c"

// The corpus of the files' functions and its arenas: `lowered` holds every file's emission (the lowered functions,
// with their symbols' access), `arena` the corpus's columns and room, `code` their records by both paths. Then what the
// files came to.
typedef struct BackendSource BackendSource;
struct BackendSource
{
    Tier0BenchmarkCorpus corpus;
    Arena lowered;
    Arena arena;
    Arena code;
    uint64_t files;
    uint64_t left_out;
    uint64_t failures;
};

// The file's functions, added to the corpus, or what kept them out of it, printed: whether the options could be read.
// `refuse`, for the tests, turns the file's emission into a refusal by name, as the backend's own refusals are made.
static bool backend_source_file(BackendSource* source, const char* file, char** options, uint32_t option_count, bool refuse)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    char** own = BUSTER_ARENA_PUSH_ARRAY(&option_arena, char*, (uint64_t)option_count + 2);
    bool valid = own != 0;
    for (uint32_t i = 0; valid && i < option_count; i += 1)
    {
        own[i] = options[i];
    }
    PPOptions pp_options;
    const char* input = 0;
    const char* output_file = 0;
    bool markers = false;
    if (valid)
    {
        own[option_count] = (char*)file;
    }
    valid = valid && pp_parse_options((int)option_count + 1, own, &pp_options, &option_arena, &input, &output_file, &markers) && output_file == 0 && input != 0;
    pp_options.semantic_pragmas = true;
    CheckCorpusPath path = { 0 };
    const bool preprocessed = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, input);
    const bool accepted = preprocessed && check_corpus_check_policy(&path, &parser, &pp, false, pp_options.quiet);
    // The checker's rejection of the file is a result; its running out of a resource is the benchmark's failure.
    const bool exhausted = preprocessed && !accepted && path.resource;
    CheckDataPrepared data = { 0 };
    CheckProgram program = { 0 };
    bool prepared = accepted && check_data_prepare(&data, &path.input, &path.stream, &path.initializers, &path.types, &path.output_arena, &path.scratch);
    const CheckProgramOptions program_options = {
        .initializers = &path.initializers,
        .asms = &path.asms,
        .identifiers = pp.table,
        .initializer_references = data.references,
        .initializer_reference_count = data.reference_count,
    };
    prepared = prepared && check_program(&program, &parser, &path.input, &path.stream, &path.types, program_options, &path.output_arena, &path.scratch);
    // A statement the checked program refuses by name leaves the file out, as -c refuses it.
    const bool unsupported = accepted && !prepared && program.status == CHECK_PROGRAM_UNSUPPORTED;
    CheckProgramSelection selected = { 0 };
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    const bool chosen = prepared && bytes.base != 0 &&
                        check_program_select_mode(&selected, &program, &data, C_LOWERING_REACHABLE, false, &path.output_arena, &path.scratch);
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    bool emitted = chosen && check_program_emit(&code, &selected.program, &parser, pp.table, selected.data.capacity, false, &source->lowered, &bytes,
                                                &path.scratch, NULL, NULL, false);
    if (emitted && refuse)
    {
        emitted = false;
        code.status = CHECK_PROGRAM_UNSUPPORTED;
        code.reason = "the tests' injection";
    }
    const bool refused = chosen && !emitted && code.status == CHECK_PROGRAM_UNSUPPORTED;
    const uint32_t count = selected.program.body_count;
    const Tier0BenchmarkCorpus before = source->corpus;
    bool same = true;
    uint32_t b = 0;
    uint8_t* defined = 0;
    bool added = emitted && check_program_access(&code.lowered, &selected.program, &source->lowered, &path.scratch, &defined);
    while (added && b < count)
    {
        added = tier0_benchmark_add(&source->corpus, &code.lowered.functions[b], &source->arena, &source->code, &same);
        b += added ? 1 : 0;
    }
    // A file is measured whole or not at all: one that fails partway takes its functions out again.
    source->corpus = emitted && !added ? before : source->corpus;
    const bool failed = (valid && !preprocessed) || exhausted || (accepted && !prepared && !unsupported) || (prepared && !chosen) ||
                        (chosen && !emitted && !refused) || (emitted && !added);
    source->failures += failed;
    source->left_out += valid && !failed && !added;
    source->files += added;
    if (valid && !preprocessed)
    {
        printf("backend-source: file=%s failed=\"it couldn't be preprocessed\"\n", file);
    }
    else if (valid && exhausted)
    {
        printf("backend-source: file=%s failed=\"the checker ran out of a resource\"\n", file);
    }
    else if (valid && !accepted)
    {
        printf("backend-source: file=%s rejected=\"%s:%u:%u: %s\"\n", file, path.file, path.line, path.column, path.message);
    }
    else if (valid && !prepared)
    {
        printf("backend-source: file=%s %s\n", file,
               unsupported ? "unsupported=\"its checked program: an unsupported C statement\"" : "failed=\"its static data or checked program couldn't be prepared\"");
    }
    else if (valid && !chosen)
    {
        printf("backend-source: file=%s failed=\"its definitions couldn't be selected\"\n", file);
    }
    else if (valid && !emitted)
    {
        printf("backend-source: file=%s %s=\"%s\"\n", file, refused ? "unsupported" : "failed", code.reason != NULL ? code.reason : "this C type or operation");
    }
    else if (valid && !added)
    {
        printf("backend-source: file=%s failed=\"%s\"\n", file, same ? "memory ran out" : "the paths disagree on a function");
        if (!same)
        {
            printf("backend-source: file=%s function=%u of %u differs between the paths\n", file, b, count);
        }
    }
    else if (valid)
    {
        printf("backend-source: file=%s functions=%u entries=%" PRIu64 " records=%" PRIu64 "\n", file, source->corpus.count - before.count,
               source->corpus.entries - before.entries, source->corpus.records - before.records);
    }
    arena_release(&bytes);
    check_corpus_close(&path);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&option_arena);
    return valid;
}

// Adds each of `files` read with `options` to the corpus (the one at 1-based index `refuse` refused by name, for the
// tests), then measures it: whether the options could be read and the arenas reserved.
static bool backend_source_files(BackendSource* source, char** files, uint32_t file_count, char** options, uint32_t option_count, uint32_t only,
                                 uint64_t after_scalar, uint32_t refuse)
{
    Arena scratch = arena_reserve(UINT64_C(1) << 32);
    source->lowered = arena_reserve(UINT64_C(1) << 36);
    source->arena = arena_reserve(UINT64_C(1) << 36);
    source->code = arena_reserve(UINT64_C(1) << 36);
    bool valid = scratch.base != 0 && source->lowered.base != 0 && source->arena.base != 0 && source->code.base != 0 &&
                 tier0_benchmark_reserve(&source->corpus, &source->arena);
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = backend_source_file(source, files[i], options, option_count, refuse == i + 1);
        fflush(stdout);
    }
    if (valid && source->corpus.count == 0)
    {
        printf("backend-source: no function was measured\n");
    }
    if (valid)
    {
        printf("backend-source: totals files=%" PRIu64 " left_out=%" PRIu64 " failures=%" PRIu64 "\n", source->files, source->left_out, source->failures);
    }
    if (valid && source->corpus.count != 0)
    {
        tier0_benchmark_measure(&source->corpus, &scratch, only, after_scalar, "benchmark-backend-source");
    }
    fflush(stdout);
    arena_release(&source->code);
    arena_release(&source->arena);
    arena_release(&source->lowered);
    arena_release(&scratch);
    return valid;
}

// The command's exit status: 0 when it measured a function and failed on no file.
static int backend_source_status(bool valid, const BackendSource* source)
{
    return valid && source->failures == 0 && source->corpus.count != 0 ? 0 : 1;
}

static int run_backend_source_benchmark(int count, char** arguments)
{
    uint32_t only = BUSTER_TIER0_BENCHMARK_VARIANTS;
    uint64_t after_scalar = 0;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--only") == 0)
        {
            valid = i + 1 < count;
            for (uint32_t v = 0; valid && v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
            {
                only = strcmp(arguments[i + 1], tier0_benchmark_names[v]) == 0 ? v : only;
            }
            valid = valid && only != BUSTER_TIER0_BENCHMARK_VARIANTS;
            i += 1;
        }
        else if (strcmp(arguments[i], "--after-scalar") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &after_scalar) && after_scalar <= 10000;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            // An option and its value, which pp_parse_options reads with each file; clang's --no-default-config, which
            // the 9700X job's options for the frozen corpora pass, is dropped, as benchmark-checker-source drops it.
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0;
    BackendSource source = { 0 };
    valid = valid && backend_source_files(&source, files, file_count, options, option_count, only, after_scalar, 0);
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-backend-source [--only vector|scalar|sizing|encode] [--after-scalar MS] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return backend_source_status(valid, &source);
}

#endif
