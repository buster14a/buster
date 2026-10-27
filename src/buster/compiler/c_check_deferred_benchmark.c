// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-checker-deferred: what #113's deferred reader (c_check_deferred.c) costs against source order's check_input,
// with its position checks apart, beside the parser's time on the same unit.
//
//   ./build/compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus LIST] [--every-lane] [--warm] [preprocessor options] file...
//
// Each file is preprocessed once. Each pass then parses it (parse_translation_unit, the vector path), and checks it with
// check_input, and with check_input_deferred on one lane and on up to --lanes (8), each time into the same arenas taken
// back and a fresh table, in one of their six orders, in turn; the deferred runs time their phases
// (CheckDeferred.timing). The deferred readings share one state, which is released after every reading, so that every
// reading reserves its memory and faults its pages in afresh, as a process's one reading does; with --warm, the state
// keeps its memory from one reading to the next, as a process's readings after its first do (c_check_deferred.c). The
// report gives the medians of --passes (24, a multiple of six) passes in nanoseconds per token: the parse, source
// order's reading, and each deferred one's whole, its top level's walk, the file scope's history (the environment's
// index), the bodies' walks (summed over the lanes) and their position checks (each body's heads from the history and
// its table's view, set and taken back), the lanes' span, the merge and the rest (the reader's phases after the merge,
// as after source order's walk); the position checks per body too, the lanes the bodies ran on (`active`: as many as
// the reader's rule gives them, or with --every-lane every lane they can take) and how many copied the reader (those
// that took a part). Then what a lane costs, in nanoseconds: its copy of the reader; lane_run's dispatch, from its call
// to the first lane's arrival; the others' wake-up, from the first lane's arrival to the last one's, which on one lane
// is none; and the lanes' arenas before they start, reserved or taken back (`arenas`); and in
// how many passes the first lane read the largest body (`largest_first`). The unit's line gives its bodies' tokens, the
// largest body's and the largest of the others' (`second_body`), the top level's tokens and the bodies' tokens that pay
// for a lane by the reader's rule (`lane_tokens`). A unit the deferred reader leaves to source order is said so. This
// measures the reader (check_input) alone, not the graph's checking after it.
//
// With --cpus (a comma-separated list of at least --lanes CPUs), the command pins itself to the first, where the parse,
// source order and the top level run, and its lanes to the list in order, in a team of its own that every run reuses.
//
// Include after c_check_source_benchmark.c and c_check_deferred.c.

// Source order, one lane and --lanes lanes, a pass's three readings, in their six orders, one a pass in turn: in any
// six passes in a row, each reading comes first, after the parse, twice, and after each other reading twice. A
// deferred reading after the deferred reader's own ran up to 3% faster than after source order's, which three orders
// that rotate gave the reading on --lanes twice as often, so --passes is a multiple of six.
#define BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS 6u
static const uint8_t check_deferred_benchmark_orders[BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS][3] = {
    { 0, 1, 2 },
    { 1, 2, 0 },
    { 2, 0, 1 },
    { 0, 2, 1 },
    { 2, 1, 0 },
    { 1, 0, 2 },
};

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
    uint64_t active[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t copies[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t copy[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t dispatch[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t wake[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t arenas[BUSTER_CHECK_SOURCE_MAX_PASSES];
    // The passes in which the first lane read the largest body (the first of the largest in source order).
    uint32_t largest_first;
};

// One check of the parsed unit: source order's (lanes 0) or the deferred reader's, into `arenas` (types, its index,
// the input and scratch), taken back first. Whether it ran, with the deferred reader's state in `deferred`.
static bool check_deferred_benchmark_run(const Parser* parser, Preprocessor* pp, Arena* arenas, uint32_t lanes, const uint32_t* cpus, bool every_lane,
                                         CheckDeferred* deferred, uint64_t* nanoseconds)
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
        deferred->every_lane = every_lane;
        check_input_deferred(deferred, &input, parser, pp, &types, arenas + 2, arenas + 3, lanes, CHECK_INPUT_BINDINGS_DEFAULT);
    }
    *nanoseconds = check_source_time() - begin;
    return valid;
}

static bool check_deferred_benchmark_file(const char* file, char** options, uint32_t option_count, uint64_t passes, uint32_t lanes, const uint32_t* cpus,
                                          bool every_lane, bool warm)
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
    samples[0].largest_first = 0;
    samples[1].largest_first = 0;
    uint64_t bodies = 0;
    uint64_t body_tokens = 0;
    uint32_t largest_body = 0;
    uint32_t second_body = 0;
    uint32_t top_tokens = 0;
    uint32_t lane_tokens = 0;
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
            const uint32_t way = check_deferred_benchmark_orders[pass % BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS][step];
            uint64_t time = 0;
            const uint32_t run_lanes = way == 0 ? 0 : way == 1 ? 1 : lanes;
            valid = check_deferred_benchmark_run(&parser, &pp, arenas, run_lanes, cpus, every_lane, &deferred, &time);
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
                sample->active[pass] = deferred.active;
                sample->copies[pass] = deferred.copies;
                sample->copy[pass] = deferred.copies != 0 ? deferred.copy_ns / deferred.copies : 0;
                sample->dispatch[pass] = deferred.dispatch_ns;
                sample->wake[pass] = deferred.wake_ns;
                sample->arenas[pass] = deferred.arenas_ns;
                sample->largest_first += deferred.largest_lane == 0;
                merged &= !deferred.unsupported;
                reason = deferred.unsupported ? deferred.reason : reason;
                bodies = deferred.bodies;
                body_tokens = deferred.body_tokens;
                largest_body = deferred.largest_body;
                second_body = deferred.second_body;
                top_tokens = deferred.top_tokens;
                lane_tokens = deferred.lane_tokens;
                history_reads = deferred.history_reads;
                if (!warm)
                {
                    check_deferred_release(&deferred);
                }
            }
        }
        parse_release(&parser);
    }
    if (valid)
    {
        const uint32_t count = (uint32_t)passes;
        const double tokens = pp.output.count != 0 ? (double)pp.output.count : 1.0;
        const double per_body = bodies != 0 ? (double)bodies : 1.0;
        printf("checker-deferred: file=%s tokens=%u bodies=%" PRIu64 " body_tokens=%" PRIu64 " largest_body=%u second_body=%u top_tokens=%u"
               " lane_tokens=%u history_reads=%" PRIu64 " merged=%s%s%s%s parse=%.2f source_order=%.2f ns/token\n", file, pp.output.count, bodies,
               body_tokens, largest_body, second_body, top_tokens, lane_tokens, history_reads, merged ? "yes" : "no",
               merged ? "" : " (", merged ? "" : (reason != 0 ? reason : "unknown"), merged ? "" : ")", (double)check_source_median(parses, count) / tokens,
               (double)check_source_median(sequential, count) / tokens);
        for (uint32_t way = 0; way < 2; way += 1)
        {
            const CheckDeferredSample* sample = samples + way;
            printf("checker-deferred:   lanes=%u active=%" PRIu64 " copies=%" PRIu64 " whole=%.2f top=%.2f environment=%.3f walks=%.2f positions=%.3f"
                   " lanes_span=%.2f merge=%.2f rest=%.2f ns/token, positions=%.1f ns/body, copy=%" PRIu64 " ns/lane, dispatch=%" PRIu64 " ns,"
                   " wake=%" PRIu64 " ns, arenas=%" PRIu64 " ns, largest_first=%u/%u\n",
                   way == 0 ? 1 : lanes, check_source_median(sample->active, count), check_source_median(sample->copies, count),
                   (double)check_source_median(sample->whole, count) / tokens,
                   (double)check_source_median(sample->top, count) / tokens, (double)check_source_median(sample->environment, count) / tokens,
                   (double)check_source_median(sample->walks, count) / tokens, (double)check_source_median(sample->positions, count) / tokens,
                   (double)check_source_median(sample->lanes, count) / tokens, (double)check_source_median(sample->merge, count) / tokens,
                   (double)check_source_median(sample->rest, count) / tokens, (double)check_source_median(sample->positions, count) / per_body,
                   check_source_median(sample->copy, count), check_source_median(sample->dispatch, count), check_source_median(sample->wake, count),
                   check_source_median(sample->arenas, count), sample->largest_first, count);
        }
    }
    else
    {
        printf("checker-deferred: file=%s failed=\"it couldn't be preprocessed, parsed or checked\"\n", file);
    }
    // A unit read in source order measures no deferred reading: the command fails, having said why.
    valid = valid && merged;
    check_deferred_release(&deferred);
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
    uint64_t passes = 24;
    uint64_t lanes = 8;
    bool every_lane = false;
    bool warm = false;
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
        else if (strcmp(arguments[i], "--every-lane") == 0)
        {
            every_lane = true;
        }
        else if (strcmp(arguments[i], "--warm") == 0)
        {
            warm = true;
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
    valid = valid && file_count != 0 && passes != 0 && passes % BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS == 0 && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES &&
            lanes != 0 && lanes <= BUSTER_CHECK_DEFERRED_MAX_LANES && (!cpus_given || cpu_count >= lanes);
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
        measured &= check_deferred_benchmark_file(files[i], options, option_count, passes, (uint32_t)lanes, pinned ? cpus : 0, every_lane, warm);
        fflush(stdout);
    }
    if (pinned)
    {
        lane_team_stop(&team);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus LIST] [--every-lane] [--warm] [preprocessor options] file...\n"
                        "  --passes: a multiple of %u, the readings' orders, up to %u (24 by default)\n",
                        BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS, BUSTER_CHECK_SOURCE_MAX_PASSES / BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS * BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS);
    }
    free(files);
    free(options);
    return valid && measured ? 0 : 1;
}
