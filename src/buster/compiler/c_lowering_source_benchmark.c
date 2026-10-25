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
// time saved, then the totals over the files that lower both ways. A file the checker rejects, or whose emission fails
// either way in its warm-up (the backend's refusals name their feature), is reported and left out of the totals:
// lowering everything also lowers what nothing calls, which the backend may not support. Every timed run must lower
// what its warm-up did, the same counts and object bytes: a run that fails or differs leaves its file out of the totals
// and fails the benchmark, since its time would be of other work.
//
// This measures the stages after checking alone: every body is checked whichever way the definitions are selected
// (#155). It is a benchmark outside the compile path, with no vector path of its own.
#ifndef BUSTER_C_LOWERING_SOURCE_BENCHMARK_INCLUDED
#define BUSTER_C_LOWERING_SOURCE_BENCHMARK_INCLUDED

// One way of selecting a file's definitions: what it lowered, its runs' times and why it failed, if it did.
typedef struct LoweringSourceWay LoweringSourceWay;
struct LoweringSourceWay
{
    uint32_t functions;
    uint32_t objects;
    uint64_t object_bytes;
    uint64_t nanoseconds[BUSTER_CHECK_SOURCE_MAX_PASSES];
    const char* failure;
    char feature[192];
};

// Selects, lowers, publishes and builds the object of a checked file's program `full` with `mode`: whether it could,
// with the counts in `way` and the reason in way->failure if not. Every arena is put back as it was.
static bool lowering_source_run(LoweringSourceWay* way, const CheckProgram* full, const CheckDataPrepared* data, CLoweringMode mode,
                                const Parser* parser, Preprocessor* pp, TypeTable* types, const char* file, Arena* output, Arena* scratch,
                                Arena* code_output, Arena* bytes)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint64_t code_mark = code_output->length;
    const uint64_t byte_mark = bytes->length;
    CheckProgramSelection selected = { 0 };
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE };
    way->failure = NULL;
    bool valid = check_program_select_mode(&selected, full, data, mode, false, output, scratch);
    way->failure = way->failure == NULL && !valid ? "the selection failed" : way->failure;
    const bool lowered = valid && check_program_emit(&code, &selected.program, parser, pp->table, selected.data.capacity, false, code_output, bytes, scratch,
                                                     NULL, false);
    if (valid && !lowered)
    {
        snprintf(way->feature, sizeof(way->feature), "%s%s", code.status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported: " : "the backend failed: ",
                 code.reason != NULL ? code.reason : "this C type or operation");
        way->failure = way->feature;
    }
    valid = lowered && check_data_publish(&selected.data, types, selected.program.stream, selected.program.object_symbols, selected.program.object_count,
                                          &code.link, 0, bytes, scratch, &emission);
    way->failure = lowered && !valid ? "its static data couldn't be published" : way->failure;
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
    // Files whose timed runs didn't all lower what their warm-ups did.
    uint64_t failures;
};

// The file's measurement, printed and added to `totals` when both ways lower it: whether the file could be measured
// (a rejection or a refusal of either way is a result, not a failure of the benchmark).
static bool lowering_source_file(const char* file, char** options, uint32_t option_count, uint64_t passes, LoweringSourceTotals* totals)
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
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, input);
    const bool accepted = valid && check_corpus_check_policy(&path, &parser, &pp, false, pp_options.quiet);
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
    // Each way's warm-up, then its last timed run.
    LoweringSourceWay ways[4] = { 0 };
    bool lowered[2] = { false, false };
    Arena code_output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    prepared = prepared && code_output.base != 0 && bytes.base != 0;
    for (uint32_t way = 0; prepared && way < 2; way += 1)
    {
        // The warm-up, which also says whether this way lowers the file at all.
        lowered[way] = lowering_source_run(ways + way, &program, &data, way == 0 ? C_LOWERING_REACHABLE : C_LOWERING_ALL, &parser, &pp, &path.types, input,
                                           &path.output_arena, &path.scratch, &code_output, &bytes);
    }
    bool measured = lowered[0] && lowered[1];
    uint32_t failed_pass = 0;
    uint32_t failed_way = 0;
    for (uint32_t pass = 0; measured && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; measured && step < 2; step += 1)
        {
            const uint32_t way = (pass + step) & 1;
            LoweringSourceWay* run = ways + 2 + way;
            const uint64_t begin = check_source_time();
            const bool ran = lowering_source_run(run, &program, &data, way == 0 ? C_LOWERING_REACHABLE : C_LOWERING_ALL, &parser, &pp, &path.types, input,
                                                 &path.output_arena, &path.scratch, &code_output, &bytes);
            ways[way].nanoseconds[pass] = check_source_time() - begin;
            measured = ran && run->functions == ways[way].functions && run->objects == ways[way].objects && run->object_bytes == ways[way].object_bytes;
            failed_pass = pass;
            failed_way = way;
        }
    }
    const bool consistent = !(lowered[0] && lowered[1]) || measured;
    totals->failures += !consistent;
    if (!accepted)
    {
        printf("lowering-source: file=%s rejected=\"%s:%u:%u: %s\"\n", file, path.file, path.line, path.column, path.message);
    }
    else if (!prepared)
    {
        printf("lowering-source: file=%s failed=\"its static data or checked program couldn't be prepared\"\n", file);
    }
    else
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
            printf(" referenced=\"%s\" all=\"%s\"\n", lowered[0] ? "lowered" : ways[0].failure, lowered[1] ? "lowered" : ways[1].failure);
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
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = lowering_source_file(files[i], options, option_count, passes, &totals);
        fflush(stdout);
    }
    if (valid)
    {
        printf("lowering-source: totals files=%" PRIu64 " functions_defined=%" PRIu64 " functions_lowered=%" PRIu64 " functions_skipped=%" PRIu64
               " objects_defined=%" PRIu64 " objects_lowered=%" PRIu64 " objects_skipped=%" PRIu64 " lower_all_functions=%" PRIu64 " lower_all_objects=%" PRIu64
               " referenced_ns=%" PRIu64 " all_ns=%" PRIu64 " saved_ns=%" PRId64 " passes=%" PRIu64 " failures=%" PRIu64 "\n",
               totals.files, totals.functions, totals.lowered_functions[0], totals.functions - totals.lowered_functions[0], totals.objects,
               totals.lowered_objects[0], totals.objects - totals.lowered_objects[0], totals.lowered_functions[1], totals.lowered_objects[1],
               totals.nanoseconds[0], totals.nanoseconds[1], (int64_t)totals.nanoseconds[1] - (int64_t)totals.nanoseconds[0], passes, totals.failures);
    }
    else
    {
        fprintf(stderr, "usage: compiler benchmark-lowering-source [--passes N] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return valid && totals.failures == 0 ? 0 : 1;
}

#endif
