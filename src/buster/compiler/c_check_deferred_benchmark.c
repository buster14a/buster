// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-checker-deferred: what #113's deferred reader (c_check_deferred.c) costs against source order's check_input,
// with its position checks apart, beside the parser's time on the same unit.
//
//   ./build/compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus LIST] [preprocessor options] file...
//
// Each file is preprocessed once. Each pass then parses it (parse_translation_unit, the vector path), and checks it with
// check_input, and with check_input_deferred on one lane and on --lanes (8), each time into fresh arenas and a fresh
// table, in turns that rotate which goes first; the deferred runs time their phases (CheckDeferred.timing). The report
// gives the medians of --passes (21) passes in nanoseconds per token: the parse, source order's reading, and each
// deferred one's whole, its top level's walk, the file scope's history (the environment's index), the bodies' walks
// (summed over the lanes) and their position checks (each body's heads from the history and its table's view, set and
// taken back), the lanes' span, the merge and the rest (the reader's phases after the merge, as after source order's
// walk); the position checks per body too, and how many lanes copied the reader (those that took a part, no more than
// the bodies). A unit the deferred reader leaves to source order is said so. This measures the reader (check_input)
// alone, not the graph's checking after it.
//
// With --cpus (a comma-separated list of at least --lanes CPUs), the command pins itself to the first, where the parse,
// source order and the top level run, and its lanes to the list in order, in a team of its own that every run reuses.
//
// Include after c_check_source_benchmark.c and c_check_deferred.c.

typedef struct CheckDeferredSample CheckDeferredSample;
struct CheckDeferredSample
{
    uint64_t whole[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t top[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t environment[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t walks[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t positions[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t lanes[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t merge[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t rest[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t copies[BUSTER_CHECK_SOURCE_MAX_PASSES];
};

// One check of the parsed unit: source order's (lanes 0) or the deferred reader's, into `arenas` (types, its index,
// the input and scratch), taken back first. Whether it ran, with the deferred reader's state in `deferred`.
static bool check_deferred_benchmark_run(const Parser* parser, Preprocessor* pp, Arena* arenas, uint32_t lanes, const uint32_t* cpus, CheckDeferred* deferred,
                                         uint64_t* nanoseconds)
{
    for (uint32_t i = 0; i < 4; i += 1)
    {
        arena_pop_to(arenas + i, 0);
    }
    const uint32_t tokens = parser->tokens.count;
    const TypeCapacity capacity = {
        .types = tokens * 4 + 64,
        .parameters = tokens + 1,
        .tags = tokens + 1,
        .members = tokens + 1,
    };
    TypeTable types;
    CheckInput input;
    bool valid = type_initialize(&types, arenas + 0, arenas + 1, capacity);
    const uint64_t begin = check_source_time();
    if (valid && lanes == 0)
    {
        check_input(&input, parser, pp, &types, arenas + 2, arenas + 3);
    }
    else if (valid)
    {
        deferred->timing = true;
        deferred->cpus = cpus;
        check_input_deferred(deferred, &input, parser, pp, &types, arenas + 2, arenas + 3, lanes, CHECK_INPUT_BINDINGS_DEFAULT);
    }
    *nanoseconds = check_source_time() - begin;
    return valid;
}

static bool check_deferred_benchmark_file(const char* file, char** options, uint32_t option_count, uint64_t passes, uint32_t lanes, const uint32_t* cpus)
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
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, input);
    Arena arenas[4] = { arena_reserve(UINT64_C(1) << 36), arena_reserve(UINT64_C(1) << 34), arena_reserve(UINT64_C(1) << 36), arena_reserve(UINT64_C(1) << 36) };
    static uint64_t parses[BUSTER_CHECK_SOURCE_MAX_PASSES];
    static uint64_t sequential[BUSTER_CHECK_SOURCE_MAX_PASSES];
    static CheckDeferredSample samples[2];
    static CheckDeferred deferred;
    uint64_t bodies = 0;
    uint64_t history_reads = 0;
    bool merged = true;
    const char* reason = 0;
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        const ParseOptions parse_options = { 0 };
        valid = parse_initialize(&parser, &parse_options, pp.options.language);
        const uint64_t begin = check_source_time();
        valid = valid && parse_translation_unit(&parser, &pp) && parser.errors == 0;
        parses[pass] = check_source_time() - begin;
        for (uint32_t step = 0; valid && step < 3; step += 1)
        {
            // Source order, one lane and `lanes` lanes, in turns.
            const uint32_t way = (pass + step) % 3;
            uint64_t time = 0;
            const uint32_t run_lanes = way == 0 ? 0 : way == 1 ? 1 : lanes;
            valid = check_deferred_benchmark_run(&parser, &pp, arenas, run_lanes, cpus, &deferred, &time);
            if (way == 0)
            {
                sequential[pass] = time;
            }
            else
            {
                CheckDeferredSample* sample = samples + (way - 1);
                sample->whole[pass] = time;
                sample->top[pass] = deferred.top_ns;
                sample->environment[pass] = deferred.environment_ns;
                sample->walks[pass] = deferred.walk_ns;
                sample->positions[pass] = deferred.position_ns;
                sample->lanes[pass] = deferred.lanes_ns;
                sample->merge[pass] = deferred.merge_ns;
                const uint64_t phases = deferred.top_ns + deferred.environment_ns + deferred.lanes_ns + deferred.merge_ns;
                sample->rest[pass] = time > phases ? time - phases : 0;
                sample->copies[pass] = deferred.copies;
                merged &= !deferred.unsupported;
                reason = deferred.unsupported ? deferred.reason : reason;
                bodies = deferred.bodies;
                history_reads = deferred.history_reads;
                check_deferred_release(&deferred);
            }
        }
        parse_release(&parser);
    }
    if (valid)
    {
        const uint32_t count = (uint32_t)passes;
        const double tokens = pp.output.count != 0 ? (double)pp.output.count : 1.0;
        const double per_body = bodies != 0 ? (double)bodies : 1.0;
        printf("checker-deferred: file=%s tokens=%u bodies=%" PRIu64 " history_reads=%" PRIu64 " merged=%s%s%s%s parse=%.2f source_order=%.2f ns/token\n", file,
               pp.output.count, bodies, history_reads, merged ? "yes" : "no", merged ? "" : " (", merged ? "" : (reason != 0 ? reason : "unknown"), merged ? "" : ")",
               (double)check_source_median(parses, count) / tokens, (double)check_source_median(sequential, count) / tokens);
        for (uint32_t way = 0; way < 2; way += 1)
        {
            const CheckDeferredSample* sample = samples + way;
            printf("checker-deferred:   lanes=%u copies=%" PRIu64 " whole=%.2f top=%.2f environment=%.3f walks=%.2f positions=%.3f lanes_span=%.2f merge=%.2f"
                   " rest=%.2f ns/token, positions=%.1f ns/body\n", way == 0 ? 1 : lanes, check_source_median(sample->copies, count),
                   (double)check_source_median(sample->whole, count) / tokens,
                   (double)check_source_median(sample->top, count) / tokens, (double)check_source_median(sample->environment, count) / tokens,
                   (double)check_source_median(sample->walks, count) / tokens, (double)check_source_median(sample->positions, count) / tokens,
                   (double)check_source_median(sample->lanes, count) / tokens, (double)check_source_median(sample->merge, count) / tokens,
                   (double)check_source_median(sample->rest, count) / tokens, (double)check_source_median(sample->positions, count) / per_body);
        }
    }
    else
    {
        printf("checker-deferred: file=%s failed=\"it couldn't be preprocessed, parsed or checked\"\n", file);
    }
    // A unit read in source order measures no deferred reading: the command fails, having said why.
    valid = valid && merged;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        arena_release(arenas + i);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&option_arena);
    return valid;
}

// Parses a comma-separated list of CPUs into `cpus`, at most `capacity` of them: how many, or 0 if it is invalid. A
// list of more than `capacity` writes none past them (test-checker-deferred's check of 65 at a guard page).
static uint32_t check_deferred_benchmark_cpus(const char* text, uint32_t* cpus, uint32_t capacity)
{
    uint32_t count = 0;
    bool valid = true;
    const char* p = text;
    while (valid && *p != 0)
    {
        char* end = 0;
        const unsigned long cpu = strtoul(p, &end, 10);
        valid = *p >= '0' && *p <= '9' && end != p && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && count < capacity;
        if (valid)
        {
            cpus[count] = (uint32_t)cpu;
            count += 1;
            p = end;
        }
        valid = valid && (*p == 0 || (*p == ',' && p[1] != 0));
        p += valid && *p == ',';
    }
    return valid ? count : 0;
}

static int run_checker_deferred_benchmark(int count, char** arguments)
{
    uint64_t passes = 21;
    uint64_t lanes = 8;
    uint32_t cpus[BUSTER_CHECK_DEFERRED_MAX_LANES];
    uint32_t cpu_count = 0;
    bool cpus_given = false;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--passes") == 0 || strcmp(arguments[i], "--lanes") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[i + 1], strcmp(arguments[i], "--passes") == 0 ? &passes : &lanes);
            i += 1;
        }
        else if (strcmp(arguments[i], "--cpus") == 0)
        {
            cpu_count = i + 1 < count ? check_deferred_benchmark_cpus(arguments[i + 1], cpus, BUSTER_CHECK_DEFERRED_MAX_LANES) : 0;
            cpus_given = true;
            valid = cpu_count != 0;
            i += 1;
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
    valid = valid && file_count != 0 && passes != 0 && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES && lanes != 0 && lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES &&
            (!cpus_given || cpu_count >= lanes);
    // The command's lanes, pinned: the parse, source order and the top level on the first CPU, with lane 0.
    LaneTeam team = { 0 };
    bool pinned = valid && cpus_given;
    pinned = pinned && lane_pin(cpus[0]) && lane_team_start(&team, (uint32_t)lanes, cpus);
    if (valid && cpus_given && !pinned)
    {
        fprintf(stderr, "benchmark-checker-deferred: can't pin the command to CPU %u or start its %u lanes on --cpus\n", cpus[0], (uint32_t)lanes);
    }
    bool measured = valid && (!cpus_given || pinned);
    for (uint32_t i = 0; measured && i < file_count; i += 1)
    {
        measured &= check_deferred_benchmark_file(files[i], options, option_count, passes, (uint32_t)lanes, pinned ? cpus : 0);
        fflush(stdout);
    }
    if (pinned)
    {
        lane_team_stop(&team);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus LIST] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return valid && measured ? 0 : 1;
}
