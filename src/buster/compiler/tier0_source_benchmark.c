// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #136's measurement on real C (benchmark-backend-source): benchmark-backend's variants (tier0_benchmark_measure.c),
// Tier 0's vector path, its scalar twin, branch sizing, encoding, the validation and the immediates' form choice (#221),
// on checked functions' typed streams. Include after c_lowering_source_benchmark.c.
//
//   build/compiler benchmark-backend-source [--only vector|scalar|sizing|encode|validate|validate-scalar|choose|choose-scalar]
//                                           [--after-scalar MS] [preprocessor options] file...
//
// Each file is checked as the driver checks it (the vector path, check_corpus_check_policy), and its reachable
// definitions (check_program_select_mode's C_LOWERING_REACHABLE) lowered and emitted by check_program_emit, outside the time,
// for the target its options give (-march and the -m features: PPOptions.target), and each function for its target
// attribute's, as -c compiles them (#1041).
// Admission ends at function emission: static-data publication and object-section placement (including descriptors
// for assembled file-scope atoms) are outside this benchmark. A valid typed function stream can be measured when
// whole-object publication is unsupported; use the driver/corpus object checks to establish that eligibility.
// Their typed streams stay in an arena that outlives the file's other state, with the symbols' access given again
// (check_program_access), and join one corpus of every file's functions, which the benchmark measures as
// benchmark-backend measures generated ones: both paths must give each function the same records, then the medians of
// BUSTER_TIER0_BENCHMARK_PASSES passes per typed entry and per record, and the share of entries the vector path
// defers. A file the checker rejects, or whose checked program or emission refuses a feature by name
// (CHECK_PROGRAM_UNSUPPORTED), is left out with the reason. Any other failure fails the benchmark, with the file left
// out: a file that doesn't preprocess, a resource that runs out, an emission that fails otherwise, and a function that
// doesn't join the corpus (the paths disagree on it, Tier 0 refuses it now, or memory runs out). So does a run with no
// function to measure, and a measurement whose runs failed on a function (tier0_benchmark_measure). Run it under the
// 9700X's lock pinned to one CPU (taskset -c 2 ...). It is a benchmark outside the compile path, with no vector path of
// its own.
#ifndef BUSTER_TIER0_SOURCE_BENCHMARK_INCLUDED
#define BUSTER_TIER0_SOURCE_BENCHMARK_INCLUDED

#include "buster/compiler/tier0_benchmark_measure.c"

// Where the tests (tier0_source_benchmark_test.c) make the benchmark fail as the backend and resources would: the file
// whose emission is refused by name (1-based, 0 for none), the corpus's room for functions, and the bytes of the arena
// the corpus's records are compiled into and of the timed runs' scratch (0 for the defaults).
typedef struct BackendSourceFault BackendSourceFault;
struct BackendSourceFault
{
    uint32_t refuse;
    uint32_t capacity;
    uint64_t code_bytes;
    uint64_t scratch_bytes;
};

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
    // Whether the corpus's measurement reported (tier0_benchmark_measure), every run of it succeeding: false for one
    // whose runs failed, and for none: no function, or functions without a typed entry.
    bool reported;
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
    PPOptions pp_options = { 0 };
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
    bool prepared = accepted && check_data_prepare(&data, &path.input, &path.stream, &path.initializers, &path.types, parser.options.scalar, &path.output_arena, &path.scratch);
    const CheckProgramOptions program_options = {
        .initializers = &path.initializers,
        .asms = &path.asms,
        .identifiers = pp.table,
        .initializer_references = data.references,
        .initializer_reference_count = data.reference_count,
        // The options' target (-march, the -m features), as -c and ir_source_prepare compile for it: without it the
        // functions would be the baseline profile's, and a unit of wider vectors would be refused (#1041). And the
        // options it came from, against which a function's target attribute resolves (CheckProgram.body_targets), as -c
        // resolves it: without them every function would be the unit's target.
        .target = pp_options.target,
        .predefines = &pp_options.predefines,
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
                                                &path.scratch, NULL, NULL, false, false);
    if (emitted && refuse)
    {
        emitted = false;
        code.status = CHECK_PROGRAM_UNSUPPORTED;
        code.reason = "the tests' injection";
    }
    const bool refused = chosen && !emitted && code.status == CHECK_PROGRAM_UNSUPPORTED;
    const uint32_t count = selected.program.body_count;
    // Every function the emission compiled must join the corpus: one Tier 0 refuses now, or that the paths disagree
    // on, or for which memory runs out, fails the file.
    const Tier0BenchmarkCorpus before = source->corpus;
    Tier0BenchmarkAdd outcome = TIER0_BENCHMARK_ADDED;
    uint32_t b = 0;
    uint8_t* defined = 0;
    bool added = emitted && check_program_access(&code, &selected.program, &source->lowered, &path.scratch, &defined);
    outcome = emitted && !added ? TIER0_BENCHMARK_MEMORY : outcome;
    while (added && b < count)
    {
        outcome = tier0_benchmark_add(&source->corpus, &code.lowered.functions[b], &source->arena, &source->code);
        added = outcome == TIER0_BENCHMARK_ADDED;
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
        printf("backend-source: file=%s failed=\"%s\" function=%u of %u\n", file,
               outcome == TIER0_BENCHMARK_DISAGREE ? "the paths disagree on a function" :
               outcome == TIER0_BENCHMARK_REFUSED ? "Tier 0 refused a function its emission compiled" : "memory ran out",
               b, count);
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

// Adds each of `files` read with `options` to the corpus, with `fault` injected (zeros for none), then measures it, and
// prints the totals last, with the measurement's outcome, as the exit status takes them (backend_source_status):
// whether the options could be read and the arenas reserved.
static bool backend_source_files(BackendSource* source, char** files, uint32_t file_count, char** options, uint32_t option_count, uint32_t only,
                                 uint64_t after_scalar, BackendSourceFault fault)
{
    Arena scratch = arena_reserve(fault.scratch_bytes != 0 ? fault.scratch_bytes : UINT64_C(1) << 32);
    source->lowered = arena_reserve(UINT64_C(1) << 36);
    source->arena = arena_reserve(UINT64_C(1) << 36);
    source->code = arena_reserve(fault.code_bytes != 0 ? fault.code_bytes : UINT64_C(1) << 36);
    bool valid = scratch.base != 0 && source->lowered.base != 0 && source->arena.base != 0 && source->code.base != 0 &&
                 tier0_benchmark_reserve(&source->corpus, &source->arena, fault.capacity != 0 ? fault.capacity : BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = backend_source_file(source, files[i], options, option_count, fault.refuse == i + 1);
        fflush(stdout);
    }
    if (valid && source->corpus.count == 0)
    {
        printf("backend-source: no function was measured\n");
    }
    source->reported = valid && source->corpus.count != 0 && tier0_benchmark_measure(&source->corpus, &scratch, only, after_scalar, "benchmark-backend-source");
    if (valid)
    {
        const bool measurable = source->corpus.entries != 0 && source->corpus.records != 0;
        printf("backend-source: totals files=%" PRIu64 " left_out=%" PRIu64 " failures=%" PRIu64 " measurement=%s\n", source->files, source->left_out,
               source->failures, !measurable ? "none" : source->reported ? "reported" : "failed");
    }
    fflush(stdout);
    arena_release(&source->code);
    arena_release(&source->arena);
    arena_release(&source->lowered);
    arena_release(&scratch);
    return valid;
}

// The command's exit status: 0 when no file failed and the measurement of the corpus, of a function or more, reported.
static int backend_source_status(bool valid, const BackendSource* source)
{
    return valid && source->failures == 0 && source->reported ? 0 : 1;
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
            // Each --only names its variant or is refused, whatever one before it named (#514's review).
            uint32_t named = BUSTER_TIER0_BENCHMARK_VARIANTS;
            valid = i + 1 < count;
            for (uint32_t v = 0; valid && v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
            {
                named = strcmp(arguments[i + 1], tier0_benchmark_names[v]) == 0 ? v : named;
            }
            only = named;
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
    valid = valid && backend_source_files(&source, files, file_count, options, option_count, only, after_scalar, (BackendSourceFault){ 0 });
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-backend-source [--only vector|scalar|sizing|encode|validate|validate-scalar|choose|choose-scalar] [--after-scalar MS] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return backend_source_status(valid, &source);
}

#endif
