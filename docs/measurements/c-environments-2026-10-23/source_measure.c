// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source-pipeline measurement for #113. Reuse the original checker harness's type/arena setup, equality
// checks, clock and median helpers. This harness additionally includes parsing in every timed call. Compile this
// identical file against the pre-handoff and candidate trees; BUSTER_MEASURE_ENVIRONMENT enables only candidate
// metadata and optional file-position query consumption. It does not remove publication from the candidate.
#define main environment_measure_previous_main
#include "../c-checker-2026-10-23/checker_source_measure.c"
#undef main

// Cross-build comparison for the frozen integer-only source workloads. Hash fields, not struct bytes/padding.
// The full scalar/vector equality check still runs after every sample; this printed fingerprint lets separate
// baseline/candidate processes compare declaration identities, graph topology and their integer typed output.
static uint64_t environment_measure_fingerprint(const SourceCheck* run)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t token = 0; token < run->input.token_count; token += 1)
    {
        hash = (hash ^ run->input.declarations[token]) * UINT64_C(1099511628211);
        hash = (hash ^ run->input.declared_types[token]) * UINT64_C(1099511628211);
        hash = (hash ^ run->input.objects[token]) * UINT64_C(1099511628211);
    }
    for (uint32_t node = 0; node < run->stream.count; node += 1)
    {
        const CheckGraph* graph = &run->input.graph;
        const CheckValue value = run->stream.values[node];
        const uint64_t fields[] =
        {
            graph->ops[node],
            graph->tokens[node],
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
            graph->types[node],
            graph->members[node],
            graph->contexts[node],
            value.type,
            value.category,
            value.flags,
            value.constant_kind,
            (uint64_t)value.integer,
            (uint64_t)(value.integer >> 64),
            value.object,
            (uint64_t)value.offset,
            value.bit_width,
            value.fault,
            value.fault_node,
            run->stream.diagnostics[node],
        };
        for (uint32_t field = 0; field < BUSTER_ARRAY_LENGTH(fields); field += 1)
        {
            hash = (hash ^ fields[field]) * UINT64_C(1099511628211);
        }
        for (uint32_t operand = 0; operand < 3; operand += 1)
        {
            const CheckConversion conversion = run->stream.conversions[node].operands[operand];
            hash = (hash ^ conversion.type) * UINT64_C(1099511628211);
            hash = (hash ^ conversion.flags) * UINT64_C(1099511628211);
        }
        hash = (hash ^ run->stream.conversions[node].store.type) * UINT64_C(1099511628211);
        hash = (hash ^ run->stream.conversions[node].store.flags) * UINT64_C(1099511628211);
    }
    return hash;
}

#ifdef BUSTER_MEASURE_ENVIRONMENT
// Consume all ordinary/tag use events as file-position queries. Some uses have a local declaration and would not
// actually reach a deferred body's file fallback. This deliberately measures the complete query trace, not a
// deferred checker or its semantic results. Compare with scalar lookups outside the timed interval.
static bool environment_measure_queries(const CheckEnvironment* environment, bool scalar, uint64_t* digest)
{
    uint32_t keys[16];
    uint32_t points[16];
    uint32_t results[16];
    uint32_t count = 0;
    uint64_t value = 0;
    bool valid = environment->valid;
    const CBindings* bindings = environment->bindings;
    for (uint32_t event = 0; valid && event <= bindings->count; event += 1)
    {
        if (event < bindings->count && bindings->operation[event] == C_BINDING_USE && bindings->key[event] < environment->file.keys)
        {
            keys[count] = bindings->key[event];
            points[count++] = bindings->point[event];
        }
        if (count != 0 && (count == 16 || event == bindings->count))
        {
            if (scalar)
            {
                for (uint32_t query = 0; query < count; query += 1)
                {
                    results[query] = c_environment_lookup_key(&environment->file, keys[query], points[query]);
                }
            }
            else
            {
                valid = c_environment_lookup_batch(&environment->file, keys, points, count, results);
            }
            for (uint32_t query = 0; valid && query < count; query += 1)
            {
                value = value * UINT64_C(6364136223846793005) + results[query];
            }
            count = 0;
        }
    }
    *digest = value;
    return valid;
}
#endif

static bool environment_measure_run(SourceCheck* run, Parser* parser, Preprocessor* pp, TypeCapacity capacity, bool scalar, bool queries, uint64_t* digest)
{
    bool valid = parse_translation_unit(parser, pp) && source_check_run(run, parser, pp, capacity, scalar);
    *digest = 0;
#ifdef BUSTER_MEASURE_ENVIRONMENT
    if (valid && queries)
    {
        valid = environment_measure_queries(&run->input.environment, scalar, digest);
    }
#else
    valid &= !queries;
#endif
    return valid;
}

#ifndef BUSTER_ENVIRONMENT_MEASURE_HELPERS_ONLY
int main(int argc, char** argv)
{
    uint32_t path_index = 1;
    uint32_t after = 0;
    bool queries = false;
    Arena options_arena = arena_reserve(1u << 20);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(&options_arena, char*, (uint64_t)argc);
    int argument_count = 0;
    bool valid = arguments != 0;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            const char* value = argv[++i];
            path_index = strcmp(value, "scalar") == 0 ? 0 : strcmp(value, "vector") == 0 ? 1 : 2;
            valid = path_index != 2;
        }
        else if (strcmp(argv[i], "--queries") == 0)
        {
            queries = true;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            char* end = 0;
            const unsigned long value = strtoul(argv[++i], &end, 10);
            valid = end != argv[i] && *end == 0 && value <= 1000;
            after = (uint32_t)value;
        }
        else
        {
            arguments[argument_count++] = argv[i];
        }
    }
    PPOptions pp_options;
    const char* path = 0;
    const char* ignored_output = 0;
    bool markers = false;
    valid = valid && pp_parse_options(argument_count, arguments, &pp_options, &options_arena, &path, &ignored_output, &markers) && check_input_target_supported(&pp_options);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, path);
    for (uint32_t p = 0; valid && p < 2; p += 1)
    {
        const ParseOptions options = { .scalar = p == 0 };
        valid = parse_initialize(&parsers[p], &options, pp_options.language) && parse_translation_unit(&parsers[p], &pp);
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
    for (uint32_t p = 0; valid && p < 3; p += 1)
    {
        valid = capacity.types != 0 && source_check_open(runs + p, capacity);
    }
    uint64_t expected = 0;
    valid = valid && environment_measure_run(runs + 2, parsers, &pp, capacity, true, queries, &expected);
    if (!valid && runs[2].input.report_count != 0)
    {
        const CheckInputReport report = runs[2].input.reports[0];
        check_driver_diagnose(parsers, &pp, report.token, check_driver_input_message(report.message));
    }
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
    const int counter = valid ? (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, 0) : -1;
    bool counters = counter >= 0;
    uint64_t nanoseconds[BUSTER_SOURCE_PASSES] = { 0 };
    uint64_t cycles[BUSTER_SOURCE_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass <= BUSTER_SOURCE_PASSES; pass += 1)
    {
        if (path_index == 1 && after != 0)
        {
            const uint64_t end = source_time() + (uint64_t)after * 1000000;
            uint64_t ignored = 0;
            while (valid && source_time() < end)
            {
                valid = environment_measure_run(runs, parsers, &pp, capacity, true, queries, &ignored);
            }
        }
        if (counter >= 0)
        {
            counters &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
        }
        uint64_t digest = 0;
        const uint64_t begin = source_time();
        const bool checked = environment_measure_run(runs + path_index, parsers + path_index, &pp, capacity, path_index == 0, queries, &digest);
        const uint64_t elapsed = source_time() - begin;
        uint64_t counts[3] = { 0 };
        if (counter >= 0)
        {
            counters &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts);
            counters &= counts[1] != 0 && counts[1] == counts[2];
        }
        valid = valid && checked && digest == expected && source_check_equal(runs + 2, runs + path_index, tokens);
        if (pass != 0)
        {
            nanoseconds[pass - 1] = elapsed;
            cycles[pass - 1] = counts[0];
        }
    }
    if (valid)
    {
        const double items = tokens != 0 ? tokens : 1;
        const uint64_t median = source_median(nanoseconds);
        printf("source=%s path=%s tokens=%u nodes=%u passes=%u after_scalar_ms=%u queries=%u comparison=identical\n", path,
               path_index == 0 ? "scalar" : "vector", tokens, runs[path_index].stream.count, BUSTER_SOURCE_PASSES, after, queries);
        printf("ns=%" PRIu64 " ns_per_token=%.3f cycles_per_token=%.3f tokens_per_second=%.0f input_bytes=%" PRIu64 " output_bytes=%" PRIu64 " digest=%" PRIu64 "\n",
               median, (double)median / items, counters ? (double)source_median(cycles) / items : -1.0,
               median != 0 ? items * 1e9 / (double)median : 0.0, runs[path_index].input_arena.length, runs[path_index].output_arena.length, expected);
        printf("integer_source_fingerprint=%" PRIu64 "\n", environment_measure_fingerprint(runs + path_index));
#ifdef BUSTER_MEASURE_ENVIRONMENT
        const CheckEnvironment* environment = &runs[path_index].input.environment;
        printf("environment_bytes=%" PRIu64 " events=%u file_declarations=%u bodies=%u tags=%u\n", environment->retained_bytes,
               environment->bindings->count, environment->file.count, environment->body_count, environment->tag_count);
#endif
    }
    else
    {
        fprintf(stderr, "source environment measurement refused: invalid options, unsupported input, exhausted storage or unequal results\n");
    }
    if (counter >= 0)
    {
        close(counter);
    }
    for (uint32_t p = 0; p < 3; p += 1)
    {
        source_check_close(runs + p);
    }
    for (uint32_t p = 0; p < 2; p += 1)
    {
        parse_release(parsers + p);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&options_arena);
    return !valid;
}
#endif
