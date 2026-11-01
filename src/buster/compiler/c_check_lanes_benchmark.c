// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #151's measurement: a unit's functions compiled on lanes (check_program_emit_lanes). Include after
// c_lowering_source_benchmark.c.
//
//   build/compiler benchmark-check-lanes [--passes N] [--lanes LIST] [--cpus LIST] [preprocessor options] file...
//
// Each file is checked as the driver checks it (the vector path, check_corpus_check_policy), its static data and checked
// program prepared for the target its options give (-march and the -m flags, as -c compiles it) and its referenced
// definitions selected, outside the time. Then each pass times, in an order that
// rotates by pass, the emission on each lane count of --lanes (1,2,3,4,6,8 by default), every lane forced
// (CheckProgramLanes.every_lane), and on the lanes the default rule takes with room for the largest count: everything
// -c's check_program_emit does (the lowering, Tier 0, branch sizing, encoding and the link's code), from arenas put back
// after each run, the lanes' threads started for each run as a compile starts them, with lane i pinned to the i-th CPU of
// --cpus (unpinned without it). It reports per file and lane count the median, the fastest and the slowest of --passes
// (21) runs, the speedup over one lane, the lanes and parts each ran, and its steps' medians (CheckProgramLanes.timing):
// the lowering's preparation before the lanes, the lanes' run, and the merge after it. Every run must make the code
// bytes and the link records one lane's warm-up made, and each lane count's warm-up the same object, byte for byte.
//
// This is a benchmark outside the compile path, with no vector path of its own.

#define BUSTER_CHECK_LANES_BENCHMARK_MAX_COUNTS 16

typedef struct CheckLanesBenchmarkRun CheckLanesBenchmarkRun;
struct CheckLanesBenchmarkRun
{
    uint64_t total[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t prepare[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t lanes[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t merge[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint32_t active;
    uint32_t parts;
};

// One emission of the selected program on `lanes` into the arenas, put back after: whether it emitted the code bytes,
// atoms and fixups `expected` holds (all zero for the warm-up, which sets them), and with `object` its object's hash.
static bool check_lanes_benchmark_emit(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser, const InternTable* identifiers,
                                       const TypeTable* types, CheckProgramLanes* lanes, Arena* output, Arena* bytes, Arena* scratch, uint64_t expected[3],
                                       uint64_t* object)
{
    const uint64_t output_mark = output->length;
    const uint64_t byte_mark = bytes->length;
    const uint64_t scratch_mark = scratch->length;
    CheckProgramCode code = { 0 };
    bool valid = check_program_emit_lanes(&code, program, parser, identifiers, data->capacity, false, output, bytes, scratch, NULL, NULL, false, false, lanes);
    const uint64_t made[3] = { code.code_bytes, code.link.atoms.count, code.link.fixups.count };
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        valid = expected[i] == 0 || expected[i] == made[i];
        expected[i] = made[i];
    }
    if (valid && object != NULL)
    {
        CheckDataResult published = { 0 };
        ObjectUnits units = { 0 };
        ObjectSymbols symbols = { 0 };
        ObjectAtomSection* sections = 0;
        valid = check_data_publish(data, types, program->stream, program->object_symbols, program->object_count, &code.link, 0, bytes, scratch, &published) &&
                check_program_sections(&code, program, bytes, output, &sections) &&
                link_check(&code.link) == LINK_ERROR_NONE && object_prepare(&units, &code.link, 1, scratch) && object_map_symbols(&symbols, &code.link, scratch);
        const ObjectOptions options = {
            .atom_sections = sections,
            .atom_section_count = sections != 0 ? code.link.atoms.count : 0,
        };
        const Object built = valid ? object_build(&code.link, &units, &symbols, 0, options, (String8){ .pointer = "lanes.c", .length = 7 }, scratch) :
                                     (Object){ .error = OBJECT_ERROR_FULL };
        valid = built.error == OBJECT_ERROR_NONE;
        *object = valid ? link_hash(built.bytes, built.size) : 0;
    }
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(bytes, byte_mark);
    arena_pop_to(output, output_mark);
    return valid;
}

// The file's measurement on each of `counts` (`count_number` of them, the last the default rule's), printed: whether the
// file could be measured.
static bool check_lanes_benchmark_file(const char* file, char** options, uint32_t option_count, uint64_t passes, const uint32_t* counts, uint32_t count_number,
                                       const uint32_t* cpus)
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
    const char* failure = valid ? NULL : "its options couldn't be read";
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, input) &&
            check_corpus_check_policy(&path, &parser, &pp, false, pp_options.quiet);
    failure = failure == NULL && !valid ? "it couldn't be preprocessed, or the checker rejected it" : failure;
    CheckDataPrepared data = { 0 };
    CheckProgram program = { 0 };
    CheckProgramSelection selected = { 0 };
    valid = valid && check_data_prepare(&data, &path.input, &path.stream, &path.initializers, &path.types, parser.options.scalar, &path.output_arena, &path.scratch);
    const CheckProgramOptions program_options = {
        .initializers = &path.initializers,
        .asms = &path.asms,
        .identifiers = pp.table,
        .initializer_references = data.references,
        .initializer_reference_count = data.reference_count,
        // The options' target and predefines, as the driver passes them: -c's code for -march, not the baseline's.
        .target = pp_options.target,
        .predefines = &pp_options.predefines,
    };
    valid = valid && check_program(&program, &parser, &path.input, &path.stream, &path.types, program_options, &path.output_arena, &path.scratch) &&
            check_program_select_mode(&selected, &program, &data, C_LOWERING_REACHABLE, false, &path.output_arena, &path.scratch);
    failure = failure == NULL && !valid ? "its static data or checked program couldn't be prepared, or its definitions selected" : failure;
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 36);
    valid = valid && output.base != 0 && bytes.base != 0 && scratch.base != 0;
    // The warm-ups, each lane count's object the one lane's.
    CheckLanesBenchmarkRun* runs = calloc(count_number, sizeof(CheckLanesBenchmarkRun));
    CheckProgramLanes* lanes = calloc(1, sizeof(CheckProgramLanes));
    uint64_t expected[3] = { 0 };
    uint64_t first_object = 0;
    valid = valid && runs != 0 && lanes != 0;
    for (uint32_t c = 0; valid && c < count_number; c += 1)
    {
        const bool rule = c + 1 == count_number;
        *lanes = (CheckProgramLanes){ .lanes = counts[c], .every_lane = !rule, .cpus = cpus, .timing = true };
        uint64_t object = 0;
        valid = check_lanes_benchmark_emit(&selected.program, &selected.data, &parser, pp.table, &path.types, lanes, &output, &bytes, &scratch, expected, &object);
        failure = failure == NULL && !valid ? (c == 0 ? "it couldn't be emitted on one lane, or its object built" : "an emission on lanes failed") : failure;
        valid = valid && (c == 0 || object == first_object);
        failure = failure == NULL && !valid ? "an emission on lanes made another object than one lane's" : failure;
        first_object = c == 0 ? object : first_object;
        check_program_lanes_release(lanes);
    }
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < count_number; step += 1)
        {
            const uint32_t c = (uint32_t)((pass + step) % count_number);
            const bool rule = c + 1 == count_number;
            *lanes = (CheckProgramLanes){ .lanes = counts[c], .every_lane = !rule, .cpus = cpus, .timing = true };
            const uint64_t begin = check_source_time();
            valid = check_lanes_benchmark_emit(&selected.program, &selected.data, &parser, pp.table, &path.types, lanes, &output, &bytes, &scratch, expected, NULL);
            failure = failure == NULL && !valid ? "a timed run failed, or made other code than its warm-up" : failure;
            runs[c].total[pass] = check_source_time() - begin;
            runs[c].prepare[pass] = lanes->prepare_ns;
            runs[c].lanes[pass] = lanes->lanes_ns;
            runs[c].merge[pass] = lanes->merge_ns;
            runs[c].active = lanes->active;
            runs[c].parts = lanes->parts;
            check_program_lanes_release(lanes);
        }
    }
    if (valid)
    {
        uint64_t total_tokens = 0;
        uint64_t largest = 0;
        for (uint32_t b = 0; b < selected.program.body_count; b += 1)
        {
            const uint32_t opening = selected.program.bodies[b].opening;
            const uint64_t tokens = (uint64_t)parser.tree.parents[opening] - opening + 1;
            total_tokens += tokens;
            largest = tokens > largest ? tokens : largest;
        }
        printf("check-lanes: file=%s functions=%u body_tokens=%" PRIu64 " largest_body_tokens=%" PRIu64 " code_bytes=%" PRIu64 " passes=%" PRIu64 "\n", file,
               selected.program.body_count, total_tokens, largest, expected[0], passes);
        const uint64_t one = check_source_median(runs[0].total, (uint32_t)passes);
        for (uint32_t c = 0; c < count_number; c += 1)
        {
            uint64_t fastest = UINT64_MAX;
            uint64_t slowest = 0;
            for (uint64_t pass = 0; pass < passes; pass += 1)
            {
                fastest = runs[c].total[pass] < fastest ? runs[c].total[pass] : fastest;
                slowest = runs[c].total[pass] > slowest ? runs[c].total[pass] : slowest;
            }
            const uint64_t median = check_source_median(runs[c].total, (uint32_t)passes);
            printf("check-lanes:   %s=%u lanes=%u parts=%u median_us=%.1f fastest_us=%.1f slowest_us=%.1f speedup=%.2f prepare_us=%.1f lanes_us=%.1f merge_us=%.1f\n",
                   c + 1 == count_number ? "rule" : "forced", counts[c], runs[c].active, runs[c].parts, (double)median / 1000.0, (double)fastest / 1000.0,
                   (double)slowest / 1000.0, median != 0 ? (double)one / (double)median : 0.0,
                   (double)check_source_median(runs[c].prepare, (uint32_t)passes) / 1000.0, (double)check_source_median(runs[c].lanes, (uint32_t)passes) / 1000.0,
                   (double)check_source_median(runs[c].merge, (uint32_t)passes) / 1000.0);
        }
    }
    else
    {
        printf("check-lanes: file=%s failed=\"%s\"\n", file, failure != NULL ? failure : "its arenas couldn't be reserved");
    }
    fflush(stdout);
    free(lanes);
    free(runs);
    arena_release(&scratch);
    arena_release(&bytes);
    arena_release(&output);
    check_corpus_close(&path);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&option_arena);
    return valid;
}

// A list of numbers, "1,2,3": into `values`, at most `capacity` of them, each from `least` to `most`; how many, 0 for
// none or a malformed list.
static uint32_t check_lanes_benchmark_list(const char* text, uint32_t* values, uint32_t capacity, uint32_t least, uint32_t most)
{
    uint32_t count = 0;
    bool valid = text != 0 && *text != 0;
    while (valid && *text != 0)
    {
        char* end = 0;
        const unsigned long value = strtoul(text, &end, 10);
        valid = end != text && value >= least && value <= most && count < capacity && (*end == ',' || *end == 0);
        values[count] = (uint32_t)value;
        count += valid ? 1 : 0;
        text = valid && *end == ',' ? end + 1 : end;
        valid = valid && !(*end == ',' && *text == 0);
    }
    return valid ? count : 0;
}

static int run_check_lanes_benchmark(int count, char** arguments)
{
    uint64_t passes = 21;
    uint32_t counts[BUSTER_CHECK_LANES_BENCHMARK_MAX_COUNTS + 1] = { 1, 2, 3, 4, 6, 8 };
    uint32_t count_number = 6;
    uint32_t cpus[BUSTER_CHECK_EMIT_MAX_LANES] = { 0 };
    uint32_t cpu_count = 0;
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
        else if (strcmp(arguments[i], "--lanes") == 0)
        {
            count_number = i + 1 < count ? check_lanes_benchmark_list(arguments[++i], counts, BUSTER_CHECK_LANES_BENCHMARK_MAX_COUNTS, 1, BUSTER_CHECK_EMIT_MAX_LANES) : 0;
            valid = count_number != 0;
        }
        else if (strcmp(arguments[i], "--cpus") == 0)
        {
            cpu_count = i + 1 < count ? check_lanes_benchmark_list(arguments[++i], cpus, BUSTER_CHECK_EMIT_MAX_LANES, 0, 1023) : 0;
            valid = cpu_count != 0;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            // An option and its value, which pp_parse_options reads with each file (clang's --no-default-config aside).
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    // The rule's lanes last, with room for the largest count; every count within the CPUs given.
    uint32_t largest = 0;
    for (uint32_t c = 0; valid && c < count_number; c += 1)
    {
        largest = counts[c] > largest ? counts[c] : largest;
    }
    counts[count_number] = largest;
    valid = valid && file_count != 0 && passes != 0 && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES && (cpu_count == 0 || cpu_count >= largest);
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = check_lanes_benchmark_file(files[i], options, option_count, passes, counts, count_number + 1, cpu_count != 0 ? cpus : NULL);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-check-lanes [--passes N] [--lanes LIST] [--cpus LIST] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return valid ? 0 : 1;
}
