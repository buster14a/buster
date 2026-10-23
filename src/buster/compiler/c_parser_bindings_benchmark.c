// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real-source binding measurements for #128. Include after c_parser_bindings_test.c and c_bindings_benchmark.c.
// Preprocessing is outside timing. `prepare` times source-event production from a parsed unit; scalar/batch include
// resolving and writing the per-token declaration column; sort includes key packing. `source-*` include parsing,
// source-event production and resolution together, so an optional scalar trace cannot be hidden as free preparation.
// Every result is checked against the producer's immediate answers. Cycles are hardware counters, never TSC ticks.

#define BUSTER_PARSE_BINDING_BENCHMARK_MAX_PASSES 101

typedef enum ParseBindingTimed
{
    PARSE_BINDING_TIMED_PREPARE,
    PARSE_BINDING_TIMED_SCALAR,
    PARSE_BINDING_TIMED_BATCH,
    PARSE_BINDING_TIMED_SORT,
    PARSE_BINDING_TIMED_SOURCE_SCALAR,
    PARSE_BINDING_TIMED_SOURCE_BATCH,
    PARSE_BINDING_TIMED_COUNT,
} ParseBindingTimed;

static const char* const parse_binding_timed_names[] =
{
    "prepare",
    "scalar",
    "batch",
    "sort",
    "source-scalar",
    "source-batch",
};

static uint64_t parse_binding_benchmark_median(const CBindingSample* samples, uint32_t count, bool cycles)
{
    uint64_t ordered[BUSTER_PARSE_BINDING_BENCHMARK_MAX_PASSES];
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t value = cycles ? samples[i].cycles : samples[i].nanoseconds;
        uint32_t j = i;
        while (j != 0 && ordered[j - 1] > value)
        {
            ordered[j] = ordered[j - 1];
            j -= 1;
        }
        ordered[j] = value;
    }
    return ordered[count / 2];
}

static void parse_binding_benchmark_map(const ParseBindings* bindings, const uint32_t* events, uint32_t* declarations)
{
    for (uint32_t t = 0; t < bindings->count; t += 1)
    {
        const uint32_t event = bindings->token_events[t];
        const uint32_t resolved = event != 0 ? events[event - 1] : 0;
        declarations[t] = resolved != 0 ? bindings->events.declaration[resolved - 1] : UINT32_MAX;
    }
}

static bool parse_binding_benchmark_file(const char* path, const PPOptions* options, uint32_t passes, uint32_t only, uint32_t after_scalar)
{
    static InternTable table;
    static Preprocessor pp;
    static Parser parser;
    const ParseOptions parse_options = { 0 };
    Arena arena = arena_reserve(1ull << 34);
    Arena scratch = arena_reserve(1ull << 34);
    Arena prepared = arena_reserve(1ull << 34);
    ParseBindings bindings = { 0 };
    bool valid = intern_initialize(&table) && pp_initialize(&pp, options, &table) && parse_initialize(&parser, &parse_options, options->language) &&
                 pp_run(&pp, path) && parse_translation_unit(&parser, &pp) && parse_bindings_build(&bindings, &parser, &arena);
    const uint64_t production_bytes = arena.length;
    CBindingReplay replay = { 0 };
    uint32_t* expected = 0;
    uint32_t* output = 0;
    uint32_t* event_output = 0;
    if (valid)
    {
        valid = c_bindings_replay_initialize(&replay, &arena, bindings.events.name_capacity, bindings.events.capacity);
        expected = valid ? arena_push_array(&arena, (uint64_t)bindings.count + 1, 4, 64) : 0;
        output = expected != 0 ? arena_push_array(&arena, (uint64_t)bindings.count + 1, 4, 64) : 0;
        event_output = output != 0 ? arena_push_array(&arena, (uint64_t)bindings.events.capacity + 1, 4, 64) : 0;
        valid = event_output != 0;
        if (valid)
        {
            memcpy(expected, bindings.declarations, (uint64_t)bindings.count * 4);
        }
    }
    uint64_t identifiers = 0;
    uint32_t event_identifiers = 0;
    for (uint32_t t = 0; valid && t < bindings.count; t += 1)
    {
        identifiers += bindings.token_events[t] != 0;
    }
    for (uint32_t e = 0; valid && e < bindings.events.count; e += 1)
    {
        event_identifiers += bindings.events.operation[e] != C_BINDING_RESTORE;
    }
    struct perf_event_attr attribute = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    const int counter = valid ? (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC) : -1;
    bool cycles_available = counter >= 0;
    CBindingSample samples[PARSE_BINDING_TIMED_COUNT][BUSTER_PARSE_BINDING_BENCHMARK_MAX_PASSES] = { 0 };
    CBindingStatistics statistics = { 0 };
    uint64_t scratch_high = 0;
    for (uint32_t pass = 0; valid && pass <= passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < PARSE_BINDING_TIMED_COUNT; step += 1)
        {
            const uint32_t which = pass % 2 ? PARSE_BINDING_TIMED_COUNT - 1 - step : step;
            if (only == PARSE_BINDING_TIMED_COUNT || only == which)
            {
                const uint64_t until = c_binding_benchmark_time() + (uint64_t)after_scalar * 1000000;
                while (valid && after_scalar != 0 && c_binding_benchmark_time() < until)
                {
                    valid = c_bindings_replay(&bindings.events, &replay, event_output);
                    __asm__ volatile("" : : "r"(event_output) : "memory");
                }
                uint64_t* keys = 0;
                uint32_t key_count = 0;
                ParseBindings produced = { 0 };
                const ParseBindings* current = &bindings;
                if (counter >= 0)
                {
                    cycles_available &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
                }
                const uint64_t begin = c_binding_benchmark_time();
                if (which == PARSE_BINDING_TIMED_SOURCE_SCALAR || which == PARSE_BINDING_TIMED_SOURCE_BATCH)
                {
                    valid = parse_translation_unit(&parser, &pp);
                }
                if (valid && (which == PARSE_BINDING_TIMED_PREPARE || which == PARSE_BINDING_TIMED_SOURCE_SCALAR || which == PARSE_BINDING_TIMED_SOURCE_BATCH))
                {
                    valid = parse_bindings_build(&produced, &parser, &prepared);
                    current = &produced;
                }
                if (valid && (which == PARSE_BINDING_TIMED_SCALAR || which == PARSE_BINDING_TIMED_SOURCE_SCALAR))
                {
                    valid = c_bindings_replay(&current->events, &replay, event_output);
                    if (valid)
                    {
                        parse_binding_benchmark_map(current, event_output, output);
                    }
                }
                else if (valid && (which == PARSE_BINDING_TIMED_BATCH || which == PARSE_BINDING_TIMED_SOURCE_BATCH))
                {
                    valid = c_bindings_batch(&current->events, event_output, &scratch, &statistics);
                    if (valid)
                    {
                        parse_binding_benchmark_map(current, event_output, output);
                    }
                }
                else if (valid && which == PARSE_BINDING_TIMED_SORT)
                {
                    valid = c_bindings_sorted(&bindings.events, &scratch, &keys, &key_count, true);
                }
                const uint64_t elapsed = c_binding_benchmark_time() - begin;
                uint64_t counts[3] = { 0 };
                if (counter >= 0)
                {
                    cycles_available &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts);
                    cycles_available &= counts[1] == counts[2] && counts[2] != 0;
                }
                if (pass != 0)
                {
                    samples[which][pass - 1] = (CBindingSample){ .nanoseconds = elapsed, .cycles = counts[0] };
                }
                if (which == PARSE_BINDING_TIMED_SORT)
                {
                    valid = valid && key_count == event_identifiers;
                    for (uint32_t k = 0; valid && k < key_count; k += 1)
                    {
                        const uint32_t event = (uint32_t)keys[k];
                        valid = event < bindings.events.count && bindings.events.operation[event] != C_BINDING_RESTORE &&
                                (uint32_t)(keys[k] >> 32) == bindings.events.key[event] && (k == 0 || keys[k - 1] < keys[k]);
                    }
                }
                else
                {
                    const uint32_t* answer = which == PARSE_BINDING_TIMED_PREPARE ? produced.declarations : output;
                    valid = valid && current->count == bindings.count && memcmp(answer, expected, (uint64_t)bindings.count * 4) == 0;
                }
                scratch_high = scratch.length > scratch_high ? scratch.length : scratch_high;
                arena_pop_to(&scratch, 0);
                arena_pop_to(&prepared, 0);
            }
        }
    }
    if (valid)
    {
        valid = c_bindings_batch(&bindings.events, event_output, &scratch, &statistics);
        if (valid)
        {
            parse_binding_benchmark_map(&bindings, event_output, output);
            valid = memcmp(output, expected, (uint64_t)bindings.count * 4) == 0;
        }
    }
    if (valid)
    {
        printf("%s: %u tokens, %llu source identifier tokens, %u identifier events, %u events, %u passes, after-scalar %u ms\n",
               path, bindings.count, (unsigned long long)identifiers, event_identifiers, bindings.events.count, passes, after_scalar);
        printf("  output allocation bytes %llu, reusable replay/results bytes %llu, observed sort scratch bytes %llu; preprocessing excluded\n",
               (unsigned long long)production_bytes, (unsigned long long)(arena.length - production_bytes), (unsigned long long)scratch_high);
        printf("  allocation capacities include spare slots; parser/trace scratch and peak RSS are not measured\n");
        for (uint32_t which = 0; which < PARSE_BINDING_TIMED_COUNT; which += 1)
        {
            if (only == PARSE_BINDING_TIMED_COUNT || only == which)
            {
                const uint64_t ns = parse_binding_benchmark_median(samples[which], passes, false);
                printf("  %s: %.3f ms/unit", parse_binding_timed_names[which], (double)ns / 1000000.0);
                if (identifiers != 0)
                {
                    printf(", %.3f ns/source-identifier", (double)ns / (double)identifiers);
                    if (cycles_available)
                    {
                        printf(", %.3f cycles/source-identifier", (double)parse_binding_benchmark_median(samples[which], passes, true) / (double)identifiers);
                    }
                    else
                    {
                        printf(", cycles unavailable");
                    }
                }
                else
                {
                    printf(", no source identifiers");
                }
                printf("\n");
            }
        }
        if (only == PARSE_BINDING_TIMED_COUNT)
        {
            const uint64_t sort = parse_binding_benchmark_median(samples[PARSE_BINDING_TIMED_SORT], passes, false);
            const uint64_t batch = parse_binding_benchmark_median(samples[PARSE_BINDING_TIMED_BATCH], passes, false);
            printf("  sort share of batch+column time %.2f%%\n", batch != 0 ? 100.0 * (double)sort / (double)batch : 0.0);
        }
        printf("  walks %llu, walked uses %llu, fallback uses %llu; every declaration column identical\n", (unsigned long long)statistics.walks,
               (unsigned long long)statistics.walked_uses, (unsigned long long)statistics.fallback_uses);
    }
    else
    {
        fprintf(stderr, "%s: source loading, allocation, binding or output comparison failed\n", path);
    }
    if (counter >= 0)
    {
        close(counter);
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&table);
    arena_release(&prepared);
    arena_release(&scratch);
    arena_release(&arena);
    return valid;
}

static int run_parser_bindings_benchmark(int argc, char** argv)
{
    ParseBindingArguments arguments;
    bool valid = parse_binding_arguments_initialize(&arguments, argc);
    uint32_t passes = 21;
    uint32_t only = PARSE_BINDING_TIMED_COUNT;
    uint32_t after_scalar = 0;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = PARSE_BINDING_TIMED_COUNT;
            for (uint32_t p = 0; p < PARSE_BINDING_TIMED_COUNT; p += 1)
            {
                if (strcmp(argv[i], parse_binding_timed_names[p]) == 0)
                {
                    only = p;
                }
            }
            valid = only != PARSE_BINDING_TIMED_COUNT;
        }
        else if ((strcmp(argv[i], "--passes") == 0 || strcmp(argv[i], "--after-scalar") == 0) && i + 1 < argc)
        {
            const bool conditioning = strcmp(argv[i], "--after-scalar") == 0;
            char* end = 0;
            errno = 0;
            const uint64_t value = strtoull(argv[++i], &end, 10);
            valid = errno == 0 && end != argv[i] && *end == 0 && value <= (conditioning ? 1000 : BUSTER_PARSE_BINDING_BENCHMARK_MAX_PASSES) &&
                    (conditioning || value != 0);
            if (conditioning)
            {
                after_scalar = (uint32_t)value;
            }
            else
            {
                passes = (uint32_t)value;
            }
        }
        else
        {
            valid = parse_binding_arguments_add(&arguments, argc, argv, &i);
        }
    }
    Arena arena = arena_reserve(1ull << 26);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    valid = valid && arguments.input_count != 0;
    if (valid)
    {
        arguments.options[arguments.option_count] = arguments.inputs[0];
        valid = pp_parse_options(arguments.option_count + 1, arguments.options, &options, &arena, &input, &output, &markers);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler benchmark-parser-bindings [--passes N] [--only prepare|scalar|batch|sort|source-scalar|source-batch] "
                        "[--after-scalar MS] [-E's options] file...\n");
    }
    for (uint32_t i = 0; valid && i < arguments.input_count; i += 1)
    {
        valid = parse_binding_benchmark_file(arguments.inputs[i], &options, passes, only, after_scalar);
    }
    arena_release(&arena);
    parse_binding_arguments_release(&arguments);
    return !valid;
}
