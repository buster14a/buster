// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #129's measurement: what lowering only the referenced definitions saves against lowering all of them (the driver's
// --lower-all), on real translation units. Include after c_check_source_benchmark.c.
//
//   build/compiler benchmark-lowering-source [--passes N] [preprocessor options] file...
//
// Each file is checked as the driver checks it (the vector path, check_corpus_check_policy), and its static data and
// checked program prepared, outside the time. Then each pass times, in turns that alternate which goes first, the two
// selections (check_program_select_mode, C_LOWERING_REACHABLE and C_LOWERING_ALL) and everything -c does after one:
// lowering, Tier 0, branch sizing, encoding and the link's code (check_program_emit), the static data's publication
// (check_data_publish) and the object's bytes (object_build), not the file's write. The arenas are reserved once per
// file and put back after each run, so each pass starts from the same state, with their pages touched by the warm-up.
// The report gives each file's functions and objects defined, lowered and skipped, the medians of both ways and the
// time saved, then the totals over the files that lower both ways. A file the checker rejects is left out of the
// totals, and so is one refused by name, with the reason the report names: CHECK_PROGRAM_UNSUPPORTED from the checked
// program's preparation or either way's emission, or CHECK_DATA_UNSUPPORTED from either way's publication of its
// static data (lowering everything also lowers what nothing calls, which the backend may not support). Any other
// failure fails the benchmark, with the file left out and the reason printed: a file that doesn't preprocess or that
// exhausts the checker's resources, a preparation or a warm-up that fails otherwise (the backend's, a resource's, an
// invalid input's), and a timed run that fails or lowers other counts or object bytes than its warm-up, since its
// time would be of other work. So does a run that measures no file.
//
// This measures the stages after checking alone: every body is checked whichever way the definitions are selected
// (#155). It is a benchmark outside the compile path, with no vector path of its own.
#ifndef BUSTER_C_LOWERING_SOURCE_BENCHMARK_INCLUDED
#define BUSTER_C_LOWERING_SOURCE_BENCHMARK_INCLUDED

// One way of selecting a file's definitions: what it lowered, its runs' times and why it failed, if it did, with
// `unsupported` when it failed for a feature refused by name, which leaves the file out without failing the benchmark.
typedef struct LoweringSourceWay LoweringSourceWay;
struct LoweringSourceWay
{
    uint32_t functions;
    uint32_t objects;
    uint64_t object_bytes;
    uint64_t nanoseconds[BUSTER_CHECK_SOURCE_MAX_PASSES];
    const char* failure;
    char feature[192];
    bool unsupported;
};

// Where the tests (test-checker's check_test_lowering_source_benchmark) inject a failure: the checker, which runs out
// of a resource there (a rejection is a real fixture's), the checked program's preparation, the referenced way's
// emission or static data's publication in its warm-up, or its emission in the last timed run.
typedef enum LoweringSourceStage
{
    LOWERING_SOURCE_STAGE_NONE,
    LOWERING_SOURCE_STAGE_CHECK,
    LOWERING_SOURCE_STAGE_PREPARE,
    LOWERING_SOURCE_STAGE_EMIT,
    LOWERING_SOURCE_STAGE_PUBLISH,
    LOWERING_SOURCE_STAGE_TIMED,
} LoweringSourceStage;

// An injected failure, with the status a real one there would have: a refusal by name (CHECK_PROGRAM_UNSUPPORTED or
// CHECK_DATA_UNSUPPORTED) when `unsupported`, a resource's failure otherwise (always, in the checker). The benchmark
// classifies it as it would the real one. In a warm-up (EMIT, PUBLISH) it fails the `ways` whose bits it has, the
// referenced definitions' (1) and all of them (2), and it fails the run's file whose index + 1 is `file`, or every
// file for 0.
typedef struct LoweringSourceFault LoweringSourceFault;
struct LoweringSourceFault
{
    LoweringSourceStage stage;
    bool unsupported;
    uint32_t ways;
    uint32_t file;
};

// Selects, lowers, publishes and builds the object of a checked file's program `full` with `mode`: whether it could,
// with the counts in `way` and the reason in way->failure if not. `fault`, of stage EMIT or PUBLISH, fails that stage
// as the tests inject it. Every arena is put back as it was.
static bool lowering_source_run(LoweringSourceWay* way, const CheckProgram* full, const CheckDataPrepared* data, CLoweringMode mode,
                                const Parser* parser, Preprocessor* pp, TypeTable* types, const char* file, Arena* output, Arena* scratch,
                                Arena* code_output, Arena* bytes, LoweringSourceFault fault)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint64_t code_mark = code_output->length;
    const uint64_t byte_mark = bytes->length;
    CheckProgramSelection selected = { 0 };
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE };
    way->failure = NULL;
    way->unsupported = false;
    bool valid = check_program_select_mode(&selected, full, data, mode, false, output, scratch);
    way->failure = way->failure == NULL && !valid ? "the selection failed" : way->failure;
    bool lowered = valid && check_program_emit(&code, &selected.program, parser, pp->table, selected.data.capacity, false, code_output, bytes, scratch,
                                               NULL, NULL, false, false);
    if (lowered && fault.stage == LOWERING_SOURCE_STAGE_EMIT)
    {
        lowered = false;
        code.status = fault.unsupported ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE;
        code.reason = "the tests' injection";
    }
    if (valid && !lowered)
    {
        snprintf(way->feature, sizeof(way->feature), "%s%s", code.status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported: " : "the backend failed: ",
                 code.reason != NULL ? code.reason : "this C type or operation");
        way->failure = way->feature;
        way->unsupported = code.status == CHECK_PROGRAM_UNSUPPORTED;
    }
    valid = lowered && check_data_publish(&selected.data, types, selected.program.stream, selected.program.object_symbols, selected.program.object_count,
                                          &code.link, 0, bytes, scratch, &emission);
    if (valid && fault.stage == LOWERING_SOURCE_STAGE_PUBLISH)
    {
        valid = false;
        emission.status = fault.unsupported ? CHECK_DATA_UNSUPPORTED : CHECK_DATA_RESOURCE;
    }
    way->unsupported |= lowered && !valid && emission.status == CHECK_DATA_UNSUPPORTED;
    way->failure = lowered && !valid ? (way->unsupported ? "unsupported: its static data's publication" : "its static data couldn't be published") : way->failure;
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    valid = valid && link_check(&code.link) == LINK_ERROR_NONE && object_prepare(&units, &code.link, 1, scratch) && object_map_symbols(&symbols, &code.link, scratch);
    if (valid)
    {
        const Object object = object_build(&code.link, &units, &symbols, 0, (ObjectOptions){ 0 }, (String8){ .pointer = (char*)file, .length = strlen(file) }, scratch);
        valid = object.error == OBJECT_ERROR_NONE;
        way->object_bytes = object.size;
    }
    way->failure = way->failure == NULL && !valid ? "its object couldn't be built" : way->failure;
    way->functions = selected.selected_functions;
    way->objects = selected.selected_objects;
    arena_pop_to(bytes, byte_mark);
    arena_pop_to(code_output, code_mark);
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(output, output_mark);
    return valid;
}

typedef struct LoweringSourceTotals LoweringSourceTotals;
struct LoweringSourceTotals
{
    uint64_t files;
    uint64_t functions;
    uint64_t objects;
    uint64_t lowered_functions[2];
    uint64_t lowered_objects[2];
    uint64_t nanoseconds[2];
    // Files the benchmark failed on: one that doesn't preprocess or exhausts the checker, a preparation or a warm-up
    // that fails for another reason than a refusal by name, and timed runs that didn't all lower what their warm-ups did.
    uint64_t failures;
};

// The file's measurement, printed and added to `totals` when both ways lower it, or a failure counted in `totals`
// (the checker's rejection or a refusal by name is a result, not a failure), with `fault` injected: whether the
// options could be read.
static bool lowering_source_file(const char* file, char** options, uint32_t option_count, uint64_t passes, LoweringSourceFault fault,
                                 LoweringSourceTotals* totals)
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
    bool accepted = preprocessed && check_corpus_check_policy(&path, &parser, &pp, false, pp_options.quiet);
    if (accepted && fault.stage == LOWERING_SOURCE_STAGE_CHECK)
    {
        accepted = false;
        path.resource = true;
    }
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
    if (prepared && fault.stage == LOWERING_SOURCE_STAGE_PREPARE)
    {
        prepared = false;
        program.status = fault.unsupported ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE;
    }
    // A statement the checked program refuses by name leaves the file out; any other failure to prepare is the
    // benchmark's (the data's preparation refuses nothing by name: its failures are resources and invalid inputs).
    const bool unsupported = accepted && !prepared && program.status == CHECK_PROGRAM_UNSUPPORTED;
    // Each way's warm-up, then its last timed run.
    LoweringSourceWay ways[4] = { 0 };
    bool lowered[2] = { false, false };
    Arena code_output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    prepared = prepared && code_output.base != 0 && bytes.base != 0;
    for (uint32_t way = 0; prepared && way < 2; way += 1)
    {
        // The warm-up, which also says whether this way lowers the file at all.
        const bool injected = (fault.ways & (1u << way)) != 0 && (fault.stage == LOWERING_SOURCE_STAGE_EMIT || fault.stage == LOWERING_SOURCE_STAGE_PUBLISH);
        lowered[way] = lowering_source_run(ways + way, &program, &data, way == 0 ? C_LOWERING_REACHABLE : C_LOWERING_ALL, &parser, &pp, &path.types, input,
                                           &path.output_arena, &path.scratch, &code_output, &bytes, injected ? fault : (LoweringSourceFault){ 0 });
    }
    // Both ways warmed up, or every way that didn't was refused a feature by name, which only leaves the file out.
    const bool warmed = lowered[0] && lowered[1];
    const bool refused = prepared && !warmed && (lowered[0] || ways[0].unsupported) && (lowered[1] || ways[1].unsupported);
    bool measured = warmed;
    uint32_t failed_pass = 0;
    uint32_t failed_way = 0;
    for (uint32_t pass = 0; measured && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; measured && step < 2; step += 1)
        {
            const uint32_t way = (pass + step) & 1;
            LoweringSourceWay* run = ways + 2 + way;
            const uint64_t begin = check_source_time();
            const bool injected = fault.stage == LOWERING_SOURCE_STAGE_TIMED && pass + 1 == passes && way == 0;
            const LoweringSourceFault timed = { .stage = injected ? LOWERING_SOURCE_STAGE_EMIT : LOWERING_SOURCE_STAGE_NONE, .unsupported = fault.unsupported };
            const bool ran = lowering_source_run(run, &program, &data, way == 0 ? C_LOWERING_REACHABLE : C_LOWERING_ALL, &parser, &pp, &path.types, input,
                                                 &path.output_arena, &path.scratch, &code_output, &bytes, timed);
            ways[way].nanoseconds[pass] = check_source_time() - begin;
            measured = ran && run->functions == ways[way].functions && run->objects == ways[way].objects && run->object_bytes == ways[way].object_bytes;
            failed_pass = pass;
            failed_way = way;
        }
    }
    const bool consistent = !warmed || measured;
    const bool failed = (valid && !preprocessed) || exhausted || (accepted && !prepared && !unsupported) || (prepared && !warmed && !refused) || !consistent;
    totals->failures += failed;
    if (valid && !preprocessed)
    {
        printf("lowering-source: file=%s failed=\"it couldn't be preprocessed\"\n", file);
    }
    else if (valid && exhausted)
    {
        printf("lowering-source: file=%s failed=\"the checker ran out of a resource\"\n", file);
    }
    else if (valid && !accepted)
    {
        printf("lowering-source: file=%s rejected=\"%s:%u:%u: %s\"\n", file, path.file, path.line, path.column, path.message);
    }
    else if (valid && !prepared)
    {
        printf("lowering-source: file=%s %s\n", file,
               unsupported ? "unsupported=\"its checked program: an unsupported C statement\"" : "failed=\"its static data, checked program or arenas couldn't be prepared\"");
    }
    else if (valid)
    {
        const uint64_t reachable = measured ? check_source_median(ways[0].nanoseconds, (uint32_t)passes) : 0;
        const uint64_t all = measured ? check_source_median(ways[1].nanoseconds, (uint32_t)passes) : 0;
        printf("lowering-source: file=%s functions_defined=%u functions_lowered=%u functions_skipped=%u objects_defined=%u objects_lowered=%u objects_skipped=%u",
               file, program.body_count, ways[0].functions, program.body_count - ways[0].functions, data.count, ways[0].objects, data.count - ways[0].objects);
        printf(" lower_all_functions=%u lower_all_objects=%u", ways[1].functions, ways[1].objects);
        if (measured)
        {
            printf(" referenced_ns=%" PRIu64 " all_ns=%" PRIu64 " saved_ns=%" PRId64 " referenced_object_bytes=%" PRIu64 " all_object_bytes=%" PRIu64 "\n", reachable,
                   all, (int64_t)all - (int64_t)reachable, ways[0].object_bytes, ways[1].object_bytes);
            totals->files += 1;
            totals->functions += program.body_count;
            totals->objects += data.count;
            for (uint32_t way = 0; way < 2; way += 1)
            {
                totals->lowered_functions[way] += ways[way].functions;
                totals->lowered_objects[way] += ways[way].objects;
            }
            totals->nanoseconds[0] += reachable;
            totals->nanoseconds[1] += all;
        }
        else if (!consistent)
        {
            const LoweringSourceWay* run = ways + 2 + failed_way;
            printf(" failed=\"timed pass %u of %s: %s\"\n", failed_pass + 1, failed_way == 0 ? "referenced" : "all",
                   run->failure != NULL ? run->failure : "it lowered other counts or object bytes than its warm-up");
        }
        else
        {
            printf(" referenced=\"%s\" all=\"%s\"%s\n", lowered[0] ? "lowered" : ways[0].failure, lowered[1] ? "lowered" : ways[1].failure,
                   refused ? "" : " failed=\"a warm-up failed for another reason than a feature refused by name\"");
        }
    }
    arena_release(&bytes);
    arena_release(&code_output);
    check_corpus_close(&path);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&option_arena);
    return valid;
}

// Measures each of `files` read with `options`, then prints the totals, with `fault` injected in the file it names:
// whether the options could be read, with the files measured and failed on in `totals`.
static bool lowering_source_files(LoweringSourceTotals* totals, char** files, uint32_t file_count, char** options, uint32_t option_count, uint64_t passes,
                                  LoweringSourceFault fault)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = lowering_source_file(files[i], options, option_count, passes, fault.file == 0 || fault.file == i + 1 ? fault : (LoweringSourceFault){ 0 }, totals);
        fflush(stdout);
    }
    if (valid && totals->files == 0)
    {
        printf("lowering-source: no file was measured both ways\n");
    }
    if (valid)
    {
        printf("lowering-source: totals files=%" PRIu64 " functions_defined=%" PRIu64 " functions_lowered=%" PRIu64 " functions_skipped=%" PRIu64
               " objects_defined=%" PRIu64 " objects_lowered=%" PRIu64 " objects_skipped=%" PRIu64 " lower_all_functions=%" PRIu64 " lower_all_objects=%" PRIu64
               " referenced_ns=%" PRIu64 " all_ns=%" PRIu64 " saved_ns=%" PRId64 " passes=%" PRIu64 " failures=%" PRIu64 "\n",
               totals->files, totals->functions, totals->lowered_functions[0], totals->functions - totals->lowered_functions[0], totals->objects,
               totals->lowered_objects[0], totals->objects - totals->lowered_objects[0], totals->lowered_functions[1], totals->lowered_objects[1],
               totals->nanoseconds[0], totals->nanoseconds[1], (int64_t)totals->nanoseconds[1] - (int64_t)totals->nanoseconds[0], passes, totals->failures);
        fflush(stdout);
    }
    return valid;
}

// The command's exit status: 0 when it measured a file and failed on none.
static int lowering_source_status(bool valid, const LoweringSourceTotals* totals)
{
    return valid && totals->failures == 0 && totals->files != 0 ? 0 : 1;
}

static int run_lowering_source_benchmark(int count, char** arguments)
{
    uint64_t passes = 21;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--passes") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &passes);
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else
        {
            // An option and its value, which pp_parse_options reads with each file.
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0 && passes != 0 && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES;
    LoweringSourceTotals totals = { 0 };
    valid = valid && lowering_source_files(&totals, files, file_count, options, option_count, passes, (LoweringSourceFault){ 0 });
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-lowering-source [--passes N] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return lowering_source_status(valid, &totals);
}

#endif
