// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-checker-deferred: what #113's deferred reader (c_check_deferred.c) costs against source order's check_input,
// with its position checks apart, beside the parser's time on the same unit.
//
//   ./build/compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus LIST] [--every-lane] [--warm] [--cold]
//                                               [--base-page-tokens N] [--raw] [--rss] [preprocessor options] file...
//
// Each file is preprocessed once. Each pass then parses it (parse_translation_unit, the vector path), and checks it with
// check_input, and with check_input_deferred on one lane and on up to --lanes (8), each time into the same arenas taken
// back and a fresh table, in one of their six orders, in turn; the deferred runs time their phases
// (CheckDeferred.timing). The deferred readings share one state, which is released after every reading, so that every
// reading reserves its memory and faults its pages in afresh, as a process's one reading does; with --warm, the state
// keeps its memory from one reading to the next, as a process's readings after its first do (c_check_deferred.c). With
// --cold, every reading also takes fresh caller arenas (released and reserved again, rather than taken back), so that
// the merge's columns on the caller's scratch are fresh huge pages as in a process's one reading (#874); without it,
// from the second pass on those pages are already mapped, and no cold reading is measured. The
// report gives the medians of --passes (24, a multiple of six) passes in nanoseconds per token: the parse, source
// order's reading, and each deferred one's whole, its top level's walk, the file scope's history (the environment's
// index), the bodies' walks (summed over the lanes) and their position checks (each body's heads from the history and
// its table's view, set and taken back), the lanes' span, the merge and the rest (the reader's phases after the merge,
// as after source order's walk); the position checks per body too, the lanes the bodies ran on (`active`: as many as
// the reader's rule gives them, or with --every-lane every lane they can take) and how many copied the reader (those
// that took a part). Then what a lane costs, in nanoseconds: its copy of the reader; lane_run's dispatch, from its call
// to the first lane's arrival; the others' wake-up, from the first lane's arrival to the last one's, which on one lane
// is none; and the lanes' arenas before they start, reserved or taken back (`arenas`); and in
// how many passes the first lane read the largest body (`largest_first`); and where more than one lane read the bodies
// of a unit that isn't large, how many 2 MB regions of the rare columns' huge pages the lanes claimed (`claimed`,
// check_input_rare_claim). The unit's line gives its bodies' tokens, the
// largest body's and the largest of the others' (`second_body`), the top level's tokens, the bodies' tokens that pay
// for a lane by the reader's rule (`lane_tokens`) and whether the unit is large (CheckDeferred.large): from
// --base-page-tokens N tokens on, which for the measurements of #847 takes the place of
// BUSTER_CHECK_DEFERRED_BASE_PAGE_TOKENS (0, the default, keeps it). A unit the deferred reader leaves to source order is
// said so. This measures the reader (check_input) alone, not the graph's checking after it.
//
// Then the readings' absolute times (#447): the medians of the parse, source order's reading and each deferred one, in
// whole nanoseconds. With --raw, every pass's samples follow, unrounded: its order of the three readings, each one's
// nanoseconds, and the lanes the last deferred reading ran on and copied the reader on. With --rss, each reading also
// takes its resident memory (CheckDeferredResident), outside its time: the process's resident set as it starts, its
// arenas taken back or reserved again, and the peak through it, from which `growth`, the pages it faulted in beyond
// those it found resident. Without --cold, the caller's arenas keep the pages earlier readings touched, so a reading's
// growth is what it took beyond them (the deferred state's own memory is released after every reading but with
// --warm); with --cold, every reading starts from fresh caller arenas and its growth is all it touched. Both are the
// kernel's counts, /proc/self/status's VmRSS and VmHWM.
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
    // The merge's parts (CheckDeferred.merge_replay_ns and the rest).
    uint64_t merge_parts[6][BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t rest[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t active[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t copies[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t copy[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t dispatch[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t wake[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t arenas[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t claimed[BUSTER_CHECK_SOURCE_MAX_PASSES];
    // With --rss, the peak resident set through the reading and its growth over the reading's start, in bytes.
    uint64_t peak[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t growth[BUSTER_CHECK_SOURCE_MAX_PASSES];
    // The passes in which the first lane read the largest body (the first of the largest in source order).
    uint32_t largest_first;
};

// The room check_deferred_benchmark_status reads /proc/self/status into: the whole file, about 1.5 KB.
#define BUSTER_CHECK_DEFERRED_STATUS_BYTES 8192u

// A field of /proc/self/status in bytes: "VmRSS:", the resident set, or "VmHWM:", its peak. 0 if it can't be read.
static uint64_t check_deferred_benchmark_status(const char* field)
{
    char text[BUSTER_CHECK_DEFERRED_STATUS_BYTES];
    const int file = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    uint64_t length = 0;
    ssize_t got = file >= 0 ? 1 : -1;
    while (got > 0 && length < sizeof(text) - 1)
    {
        got = read(file, text + length, sizeof(text) - 1 - length);
        length += got > 0 ? (uint64_t)got : 0;
    }
    if (file >= 0)
    {
        close(file);
    }
    text[length] = 0;
    const char* line = strstr(text, field);
    // The value is in kB, after blanks.
    const uint64_t kilobytes = line != 0 ? strtoull(line + strlen(field), 0, 10) : 0;
    return kilobytes * 1024;
}

// A reading's resident memory starts (--rss): its peak, reset to the resident set by writing 5 to /proc/self/clear_refs.
// The resident set in bytes, or 0 if the peak couldn't be reset or the resident set read.
static uint64_t check_deferred_benchmark_resident_begin(void)
{
    const int file = open("/proc/self/clear_refs", O_WRONLY | O_CLOEXEC);
    const bool reset = file >= 0 && write(file, "5", 1) == 1;
    if (file >= 0)
    {
        close(file);
    }
    return reset ? check_deferred_benchmark_status("VmRSS:") : 0;
}

// The peak resident set since check_deferred_benchmark_resident_begin, in bytes, or 0 if it can't be read.
static uint64_t check_deferred_benchmark_resident_peak(void)
{
    return check_deferred_benchmark_status("VmHWM:");
}

// The pages a reading faulted in beyond those resident as it started: its peak over its start.
static uint64_t check_deferred_benchmark_growth(uint64_t start, uint64_t peak)
{
    return peak > start ? peak - start : 0;
}

// A reading's resident memory (--rss), in bytes: the process's resident set as the reading starts and its peak through
// the reading. Not `known` where either couldn't be taken.
typedef struct CheckDeferredResident CheckDeferredResident;
struct CheckDeferredResident
{
    uint64_t start;
    uint64_t peak;
    bool known;
};

// The caller arenas' capacities, as check_deferred_benchmark_file reserves them: with `cold`, every reading releases
// and reserves them again, rather than taking them back, so that each reading faults fresh pages (#874).
static const uint64_t check_deferred_benchmark_capacities[4] = { UINT64_C(1) << 36, UINT64_C(1) << 34, UINT64_C(1) << 36, UINT64_C(1) << 36 };

// One check of the parsed unit: source order's (lanes 0) or the deferred reader's, into `arenas` (types, its index,
// the input and scratch), taken back first, or with `cold` released and reserved again. Whether it ran, with the deferred reader's state in `deferred`.
// With `resident`, its resident memory too, taken outside its time.
static bool check_deferred_benchmark_run(const Parser* parser, Preprocessor* pp, Arena* arenas, uint32_t lanes, const uint32_t* cpus, bool every_lane,
                                         uint32_t base_page_tokens, CheckDeferred* deferred, uint64_t* nanoseconds, bool cold, CheckDeferredResident* resident)
{
    for (uint32_t i = 0; i < 4; i += 1)
    {
        if (cold)
        {
            arena_release(arenas + i);
            arenas[i] = arena_reserve(check_deferred_benchmark_capacities[i]);
        }
        else
        {
            arena_pop_to(arenas + i, 0);
        }
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
    if (resident != 0)
    {
        resident->start = check_deferred_benchmark_resident_begin();
    }
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
        deferred->base_page_tokens = base_page_tokens;
        check_input_deferred(deferred, &input, parser, pp, &types, arenas + 2, arenas + 3, lanes, CHECK_INPUT_BINDINGS_DEFAULT);
    }
    *nanoseconds = check_source_time() - begin;
    if (resident != 0)
    {
        resident->peak = resident->start != 0 ? check_deferred_benchmark_resident_peak() : 0;
        resident->known = resident->start != 0 && resident->peak != 0;
    }
    return valid;
}

static bool check_deferred_benchmark_file(const char* file, char** options, uint32_t option_count, uint64_t passes, uint32_t lanes, const uint32_t* cpus,
                                          bool every_lane, bool warm, uint32_t base_page_tokens, bool cold, bool raw, bool rss)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    // The options read and refused as the driver's -fsyntax-only reads and refuses them (check_request_parse, #1057).
    PPOptions pp_options;
    const char* input = 0;
    CheckRequestRefusal refusal = { .error = CHECK_REQUEST_ACCEPTED };
    bool valid = check_request_parse(&pp_options, &refusal, &input, file, options, option_count, false, &option_arena);
    if (refusal.error != CHECK_REQUEST_ACCEPTED)
    {
        fprintf(stderr, "error: ");
        check_request_describe(stderr, &refusal);
        fprintf(stderr, "\n");
    }
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, input);
    Arena arenas[4] = { arena_reserve(UINT64_C(1) << 36), arena_reserve(UINT64_C(1) << 34), arena_reserve(UINT64_C(1) << 36), arena_reserve(UINT64_C(1) << 36) };
    static uint64_t parses[BUSTER_CHECK_SOURCE_MAX_PASSES];
    static uint64_t sequential[BUSTER_CHECK_SOURCE_MAX_PASSES];
    static uint64_t sequential_peak[BUSTER_CHECK_SOURCE_MAX_PASSES];
    static uint64_t sequential_growth[BUSTER_CHECK_SOURCE_MAX_PASSES];
    static CheckDeferredSample samples[2];
    static CheckDeferred deferred;
    samples[0].largest_first = 0;
    samples[1].largest_first = 0;
    // Whether every reading's resident memory was taken (--rss).
    bool resident_known = rss;
    uint64_t bodies = 0;
    uint64_t body_tokens = 0;
    uint32_t largest_body = 0;
    uint32_t second_body = 0;
    uint32_t top_tokens = 0;
    uint32_t lane_tokens = 0;
    bool large = false;
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
            CheckDeferredResident resident = { 0 };
            const uint32_t run_lanes = way == 0 ? 0 : way == 1 ? 1 : lanes;
            valid = check_deferred_benchmark_run(&parser, &pp, arenas, run_lanes, cpus, every_lane, base_page_tokens, &deferred, &time, cold, rss ? &resident : 0);
            resident_known &= resident.known;
            if (way == 0)
            {
                sequential[pass] = time;
                sequential_peak[pass] = resident.peak;
                sequential_growth[pass] = check_deferred_benchmark_growth(resident.start, resident.peak);
            }
            else
            {
                CheckDeferredSample* sample = samples + (way - 1);
                sample->whole[pass] = time;
                sample->peak[pass] = resident.peak;
                sample->growth[pass] = check_deferred_benchmark_growth(resident.start, resident.peak);
                sample->top[pass] = deferred.top_ns;
                sample->environment[pass] = deferred.environment_ns;
                sample->walks[pass] = deferred.walk_ns;
                sample->positions[pass] = deferred.position_ns;
                sample->lanes[pass] = deferred.lanes_ns;
                sample->merge[pass] = deferred.merge_ns;
                sample->merge_parts[0][pass] = deferred.merge_replay_ns;
                sample->merge_parts[1][pass] = deferred.merge_tables_ns;
                sample->merge_parts[2][pass] = deferred.merge_events_ns;
                sample->merge_parts[3][pass] = deferred.merge_tags_ns;
                sample->merge_parts[4][pass] = deferred.merge_pass_ns;
                sample->merge_parts[5][pass] = deferred.merge_rest_ns;
                const uint64_t phases = deferred.top_ns + deferred.environment_ns + deferred.lanes_ns + deferred.merge_ns;
                sample->rest[pass] = time > phases ? time - phases : 0;
                sample->active[pass] = deferred.active;
                sample->copies[pass] = deferred.copies;
                sample->copy[pass] = deferred.copies != 0 ? deferred.copy_ns / deferred.copies : 0;
                sample->dispatch[pass] = deferred.dispatch_ns;
                sample->wake[pass] = deferred.wake_ns;
                sample->arenas[pass] = deferred.arenas_ns;
                sample->claimed[pass] = deferred.claimed;
                sample->largest_first += deferred.largest_lane == 0;
                merged &= !deferred.unsupported;
                reason = deferred.unsupported ? deferred.reason : reason;
                bodies = deferred.bodies;
                body_tokens = deferred.body_tokens;
                largest_body = deferred.largest_body;
                second_body = deferred.second_body;
                top_tokens = deferred.top_tokens;
                lane_tokens = deferred.lane_tokens;
                large = deferred.large;
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
               " lane_tokens=%u large=%s history_reads=%" PRIu64 " merged=%s%s%s%s parse=%.2f source_order=%.2f ns/token\n", file, pp.output.count, bodies,
               body_tokens, largest_body, second_body, top_tokens, lane_tokens, large ? "yes" : "no", history_reads, merged ? "yes" : "no",
               merged ? "" : " (", merged ? "" : (reason != 0 ? reason : "unknown"), merged ? "" : ")", (double)check_source_median(parses, count) / tokens,
               (double)check_source_median(sequential, count) / tokens);
        for (uint32_t way = 0; way < 2; way += 1)
        {
            const CheckDeferredSample* sample = samples + way;
            printf("checker-deferred:   lanes=%u active=%" PRIu64 " copies=%" PRIu64 " whole=%.2f top=%.2f environment=%.3f walks=%.2f positions=%.3f"
                   " lanes_span=%.2f merge=%.2f rest=%.2f ns/token, positions=%.1f ns/body, copy=%" PRIu64 " ns/lane, dispatch=%" PRIu64 " ns,"
                   " wake=%" PRIu64 " ns, arenas=%" PRIu64 " ns, claimed=%" PRIu64 ", largest_first=%u/%u\n",
                   way == 0 ? 1 : lanes, check_source_median(sample->active, count), check_source_median(sample->copies, count),
                   (double)check_source_median(sample->whole, count) / tokens,
                   (double)check_source_median(sample->top, count) / tokens, (double)check_source_median(sample->environment, count) / tokens,
                   (double)check_source_median(sample->walks, count) / tokens, (double)check_source_median(sample->positions, count) / tokens,
                   (double)check_source_median(sample->lanes, count) / tokens, (double)check_source_median(sample->merge, count) / tokens,
                   (double)check_source_median(sample->rest, count) / tokens, (double)check_source_median(sample->positions, count) / per_body,
                   check_source_median(sample->copy, count), check_source_median(sample->dispatch, count), check_source_median(sample->wake, count),
                   check_source_median(sample->arenas, count), check_source_median(sample->claimed, count),
                   sample->largest_first, count);
            printf("checker-deferred:   lanes=%u merge's parts: replay=%.3f tables=%.3f events=%.3f tags=%.3f pass=%.3f rest=%.3f ns/token\n",
                   way == 0 ? 1 : lanes, (double)check_source_median(sample->merge_parts[0], count) / tokens,
                   (double)check_source_median(sample->merge_parts[1], count) / tokens, (double)check_source_median(sample->merge_parts[2], count) / tokens,
                   (double)check_source_median(sample->merge_parts[3], count) / tokens, (double)check_source_median(sample->merge_parts[4], count) / tokens,
                   (double)check_source_median(sample->merge_parts[5], count) / tokens);
        }
        printf("checker-deferred:   absolute: parse=%" PRIu64 " source_order=%" PRIu64 " deferred_1=%" PRIu64 " deferred_%u=%" PRIu64 " ns, medians of %u passes\n",
               check_source_median(parses, count), check_source_median(sequential, count), check_source_median(samples[0].whole, count), lanes,
               check_source_median(samples[1].whole, count), count);
        if (rss && resident_known)
        {
            printf("checker-deferred:   resident: source_order peak=%" PRIu64 " growth=%" PRIu64 ", deferred_1 peak=%" PRIu64 " growth=%" PRIu64 ", deferred_%u peak=%" PRIu64
                   " growth=%" PRIu64 " KiB, medians of %u passes\n",
                   check_source_median(sequential_peak, count) / 1024, check_source_median(sequential_growth, count) / 1024,
                   check_source_median(samples[0].peak, count) / 1024, check_source_median(samples[0].growth, count) / 1024, lanes,
                   check_source_median(samples[1].peak, count) / 1024, check_source_median(samples[1].growth, count) / 1024, count);
        }
        else if (rss)
        {
            printf("checker-deferred:   resident: unknown, /proc/self/clear_refs or /proc/self/status couldn't be used\n");
        }
        // Every pass's samples: its readings' order (s source order, 1 the deferred reader on one lane, n on --lanes).
        for (uint32_t pass = 0; raw && pass < count; pass += 1)
        {
            const uint8_t* order = check_deferred_benchmark_orders[pass % BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS];
            const char names[3] = { 's', '1', 'n' };
            printf("checker-deferred:   pass=%u order=%c%c%c parse=%" PRIu64 " source_order=%" PRIu64 " deferred_1=%" PRIu64 " deferred_%u=%" PRIu64 " ns active=%" PRIu64
                   " copies=%" PRIu64, pass, names[order[0]], names[order[1]], names[order[2]], parses[pass], sequential[pass], samples[0].whole[pass], lanes,
                   samples[1].whole[pass], samples[1].active[pass], samples[1].copies[pass]);
            if (rss && resident_known)
            {
                printf(" source_order_peak=%" PRIu64 " source_order_growth=%" PRIu64 " deferred_1_peak=%" PRIu64 " deferred_1_growth=%" PRIu64 " deferred_%u_peak=%" PRIu64
                       " deferred_%u_growth=%" PRIu64 " KiB",
                       sequential_peak[pass] / 1024, sequential_growth[pass] / 1024, samples[0].peak[pass] / 1024, samples[0].growth[pass] / 1024, lanes,
                       samples[1].peak[pass] / 1024, lanes, samples[1].growth[pass] / 1024);
            }
            printf("\n");
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
    uint64_t base_page_tokens = 0;
    bool every_lane = false;
    bool cold = false;
    bool warm = false;
    bool raw = false;
    bool rss = false;
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
        else if (strcmp(arguments[i], "--base-page-tokens") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[i + 1], &base_page_tokens) && base_page_tokens <= UINT32_MAX;
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
        else if (strcmp(arguments[i], "--cold") == 0)
        {
            cold = true;
        }
        else if (strcmp(arguments[i], "--raw") == 0)
        {
            raw = true;
        }
        else if (strcmp(arguments[i], "--rss") == 0)
        {
            rss = true;
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
        measured &= check_deferred_benchmark_file(files[i], options, option_count, passes, (uint32_t)lanes, pinned ? cpus : 0, every_lane, warm, (uint32_t)base_page_tokens, cold,
                                                  raw, rss);
        fflush(stdout);
    }
    if (pinned)
    {
        lane_team_stop(&team);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus LIST] [--every-lane] [--warm] [--cold] [--base-page-tokens N]"
                        " [--raw] [--rss] [preprocessor options] file...\n"
                        "  --passes: a multiple of %u, the readings' orders, up to %u (24 by default)\n",
                        BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS, BUSTER_CHECK_SOURCE_MAX_PASSES / BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS * BUSTER_CHECK_DEFERRED_BENCHMARK_ORDERS);
    }
    free(files);
    free(options);
    return valid && measured ? 0 : 1;
}
