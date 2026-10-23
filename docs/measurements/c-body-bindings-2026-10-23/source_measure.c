// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original producer/handoff/body-binding measurement. Reuse the original source setup/equality/timing helpers.
#define BUSTER_ENVIRONMENT_MEASURE_HELPERS_ONLY
#include "../c-environments-2026-10-23/source_measure.c"

typedef struct BodyMeasure BodyMeasure;
struct BodyMeasure
{
    const CheckEnvironment* environment;
    Arena plans;
    CBodyBindingSpan* spans;
    CBodyBindings* results;
    Arena output[8];
    Arena scratch[8];
    CBindingReplay replay[8];
    const uint32_t* cpus;
    uint64_t prefix[8];
    uint32_t bodies;
    uint32_t lanes;
    uint32_t maximum;
    bool scalar;
    bool valid[8];
};

static bool body_measure_open(BodyMeasure* run, const CheckEnvironment* environment, bool scalar, uint32_t lanes)
{
    run->plans = arena_reserve(1u << 28);
    run->bodies = environment->body_count;
    run->lanes = lanes;
    run->scalar = scalar;
    run->spans = BUSTER_ARENA_PUSH_ARRAY(&run->plans, CBodyBindingSpan, (uint64_t)run->bodies + 1);
    run->results = BUSTER_ARENA_PUSH_ARRAY(&run->plans, CBodyBindings, (uint64_t)run->bodies + 1);
    bool valid = run->spans != 0 && run->results != 0;
    valid = valid && c_body_bindings_spans(run->spans, run->bodies, environment, scalar, &run->maximum);
    for (uint32_t lane = 0; valid && lane < lanes; lane += 1)
    {
        run->output[lane] = arena_reserve(1u << 28);
        run->scratch[lane] = arena_reserve(1u << 28);
        valid = run->output[lane].base != 0 && run->scratch[lane].base != 0;
        valid = valid && (!scalar || c_bindings_replay_initialize(run->replay + lane, run->scratch + lane, environment->bindings->name_capacity, run->maximum));
        run->prefix[lane] = run->scratch[lane].length;
    }
    return valid;
}

static void body_measure_close(BodyMeasure* run)
{
    for (uint32_t lane = 0; lane < 8; lane += 1)
    {
        arena_release(run->scratch + lane);
        arena_release(run->output + lane);
    }
    arena_release(&run->plans);
}

static void body_measure_jobs(void* argument)
{
    BodyMeasure* run = argument;
    const uint32_t lane = lane_index();
    bool valid = true;
    for (uint32_t reverse = lane; valid && reverse < run->bodies; reverse += lane_count())
    {
        const uint32_t slot = run->bodies - 1 - reverse;
        valid = c_body_bindings_resolve(run->results + slot, run->environment, run->spans + slot, run->scalar,
                                       run->replay + lane, run->output + lane, run->scratch + lane);
        valid &= run->scratch[lane].length == run->prefix[lane];
    }
    run->valid[lane] = valid;
}

static bool body_measure_run(BodyMeasure* bodies, SourceCheck* run, Parser* parser, Preprocessor* pp, TypeCapacity capacity)
{
    uint64_t ignored = 0;
    bool valid = environment_measure_run(run, parser, pp, capacity, bodies->scalar, false, &ignored);
    bodies->environment = &run->input.environment;
    valid = valid && bodies->environment->body_count == bodies->bodies;
    uint32_t maximum = 0;
    valid = valid && c_body_bindings_spans(bodies->spans, bodies->bodies, bodies->environment, bodies->scalar, &maximum) && maximum <= bodies->maximum;
    for (uint32_t lane = 0; valid && lane < bodies->lanes; lane += 1)
    {
        arena_pop_to(bodies->output + lane, 0);
        valid &= bodies->scratch[lane].length == bodies->prefix[lane];
    }
    valid = valid && lane_run(bodies->lanes, body_measure_jobs, bodies, bodies->cpus);
    for (uint32_t lane = 0; valid && lane < bodies->lanes; lane += 1)
    {
        valid &= bodies->valid[lane];
    }
    return valid;
}

static uint64_t body_measure_reference(const CheckInput* input, const uint32_t* reference)
{
    const CheckEnvironment* environment = &input->environment;
    const CBindings* bindings = environment->bindings;
    uint32_t body = 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t event = 0; event < bindings->count; event += 1)
    {
        const uint32_t point = bindings->point[event];
        while (body < environment->body_count && point >= environment->bodies[body].end)
        {
            body += 1;
        }
        if (body < environment->body_count && point >= environment->bodies[body].begin && bindings->operation[event] == C_BINDING_USE)
        {
            const uint32_t token = bindings->declaration[event];
            hash = (hash ^ token) * UINT64_C(1099511628211);
            hash = (hash ^ reference[event]) * UINT64_C(1099511628211);
        }
    }
    return hash;
}

static bool body_measure_equal(const BodyMeasure* bodies, const CheckInput* input, const uint32_t* reference, uint64_t expected)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    bool equal = true;
    for (uint32_t slot = 0; equal && slot < bodies->bodies; slot += 1)
    {
        const CBodyBindings* result = bodies->results + slot;
        equal &= result->status == C_BODY_BINDINGS_OK && result->count == bodies->spans[slot].uses;
        uint32_t use = 0;
        const CBindings* source = input->environment.bindings;
        for (uint32_t event = bodies->spans[slot].begin; equal && event < bodies->spans[slot].end; event += 1)
        {
            if (source->operation[event] == C_BINDING_USE)
            {
                equal = use < result->count;
                if (equal)
                {
                    const uint32_t token = result->tokens[use];
                    const uint32_t handle = result->handles[use];
                    equal = token < input->token_count && token == source->declaration[event] && handle == reference[event] &&
                            handle != 0 && handle <= source->count && source->declaration[handle - 1] == input->declarations[token];
                    hash = (hash ^ token) * UINT64_C(1099511628211);
                    hash = (hash ^ handle) * UINT64_C(1099511628211);
                }
                use += 1;
            }
        }
        equal &= use == result->count;
    }
    return equal && hash == expected;
}

static bool body_measure_number(const char* text, uint32_t maximum, uint32_t* result)
{
    char* end = 0;
    errno = 0;
    const unsigned long number = strtoul(text, &end, 10);
    const bool valid = errno == 0 && end != text && *end == 0 && number <= maximum;
    *result = valid ? (uint32_t)number : 0;
    return valid;
}

int main(int argc, char** argv)
{
    const char* path = argc >= 2 ? argv[1] : 0;
    const uint32_t selected = argc >= 3 && strcmp(argv[2], "scalar") == 0 ? 0 : 1;
    uint32_t lanes = 1;
    uint32_t after = 0;
    bool valid = argc >= 2 && argc <= 5 && (argc < 3 || strcmp(argv[2], "scalar") == 0 || strcmp(argv[2], "vector") == 0);
    valid = valid && (argc < 4 || body_measure_number(argv[3], 8, &lanes)) && (argc < 5 || body_measure_number(argv[4], 1000, &after));
    valid = valid && (lanes == 1 || lanes == 8) && (selected == 1 || after == 0);
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && pp_run(&pp, path);
    for (uint32_t mode = 0; valid && mode < 2; mode += 1)
    {
        const ParseOptions parse_options = { .scalar = mode == 0 };
        valid = parse_initialize(parsers + mode, &parse_options, options.language) && parse_translation_unit(parsers + mode, &pp);
    }
    const uint32_t tokens = parsers[0].tokens.count;
    const TypeCapacity capacity =
    {
        .types = tokens < (1u << 27) ? tokens * 4 + 64 : 0,
        .parameters = tokens + 1,
        .tags = tokens + 1,
        .members = tokens + 1,
    };
    SourceCheck runs[3] = { 0 };
    BodyMeasure bodies[2] = { 0 };
    for (uint32_t mode = 0; valid && mode < 3; mode += 1)
    {
        valid = capacity.types != 0 && source_check_open(runs + mode, capacity);
    }
    uint64_t ignored = 0;
    valid = valid && environment_measure_run(runs + 2, parsers, &pp, capacity, true, false, &ignored);
    Arena reference_storage = arena_reserve(1u << 30);
    uint32_t* reference = valid ? BUSTER_ARENA_PUSH_ARRAY(&reference_storage, uint32_t, (uint64_t)runs[2].input.binding_event_count + 1) : 0;
    valid = valid && reference != 0 && c_bindings_scalar(runs[2].input.environment.bindings, reference, &reference_storage);
    const uint64_t expected = valid ? body_measure_reference(&runs[2].input, reference) : 0;
    for (uint32_t mode = 0; valid && mode < 2; mode += 1)
    {
        valid = body_measure_open(bodies + mode, &runs[2].input.environment, mode == 0, lanes);
    }
    static LaneTeam team;
    const uint32_t cpus[] =
    {
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
    };
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        bodies[mode].cpus = cpus;
    }
    valid = valid && lane_team_start(&team, lanes, cpus);
    const struct perf_event_attr attribute =
    {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    // The existing per-thread counter does not measure aggregate multicore cycles. Eight-lane runs report wall
    // time/throughput, and cycles stay unavailable rather than mislabeling the timing thread as the whole group.
    const int counter = valid && lanes == 1 ? (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, 0) : -1;
    bool counters = counter >= 0;
    uint64_t nanoseconds[BUSTER_SOURCE_PASSES] = { 0 };
    uint64_t cycles[BUSTER_SOURCE_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass <= BUSTER_SOURCE_PASSES; pass += 1)
    {
        if (selected == 1 && after != 0)
        {
            const uint64_t end = source_time() + (uint64_t)after * 1000000;
            while (valid && source_time() < end)
            {
                valid = body_measure_run(bodies, runs, parsers, &pp, capacity);
            }
        }
        if (counter >= 0)
        {
            counters &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
        }
        const uint64_t begin = source_time();
        const bool checked = body_measure_run(bodies + selected, runs + selected, parsers + selected, &pp, capacity);
        const uint64_t elapsed = source_time() - begin;
        uint64_t counts[3] = { 0 };
        if (counter >= 0)
        {
            counters &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts);
            counters &= counts[1] != 0 && counts[1] == counts[2];
        }
        valid = valid && checked && source_check_equal(runs + 2, runs + selected, tokens) && body_measure_equal(bodies + selected, &runs[selected].input, reference, expected);
        if (pass != 0)
        {
            nanoseconds[pass - 1] = elapsed;
            cycles[pass - 1] = counts[0];
        }
    }
    if (valid)
    {
        const BodyMeasure* measured = bodies + selected;
        uint64_t retained = measured->plans.length;
        uint64_t workspace = 0;
        uint64_t file_queries = 0;
        uint64_t fallback = 0;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            retained += measured->output[lane].length;
            workspace += measured->prefix[lane];
        }
        for (uint32_t slot = 0; slot < measured->bodies; slot += 1)
        {
            file_queries += measured->results[slot].file_queries;
            fallback += measured->results[slot].statistics.fallback_uses;
        }
        const uint64_t median = source_median(nanoseconds);
        const double items = tokens != 0 ? tokens : 1;
        printf("source=%s path=%s lanes=%u tokens=%u bodies=%u passes=%u after_scalar_ms=%u comparison=identical\n", path,
               selected == 0 ? "scalar" : "vector", lanes, tokens, measured->bodies, BUSTER_SOURCE_PASSES, after);
        printf("ns=%" PRIu64 " ns_per_token=%.3f cycles_per_token=%.3f tokens_per_second=%.0f\n", median,
               (double)median / items, counters ? (double)source_median(cycles) / items : -1.0, median != 0 ? items * 1e9 / (double)median : 0.0);
        printf("body_result_plan_bytes=%" PRIu64 " persistent_replay_bytes=%" PRIu64 " max_body_events=%u file_queries=%" PRIu64 " fallback_uses=%" PRIu64 "\n",
               retained, workspace, measured->maximum, file_queries, fallback);
        printf("integer_source_fingerprint=%" PRIu64 " body_reference_digest=%" PRIu64 "\n", environment_measure_fingerprint(runs + selected), expected);
    }
    else
    {
        fprintf(stderr, "usage: body-source-measure SOURCE [scalar|vector] [1|8] [after-scalar-ms]; measurement also refuses unsupported/unequal source results\n");
    }
    if (counter >= 0)
    {
        close(counter);
    }
    lane_team_stop(&team);
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        body_measure_close(bodies + mode);
        parse_release(parsers + mode);
    }
    for (uint32_t mode = 0; mode < 3; mode += 1)
    {
        source_check_close(runs + mode);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&reference_storage);
    return !valid;
}
