// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source-adapter/checker measurement harness. Parsing is outside the measured region.
#define BUSTER_UNIT 99
#include "buster/compiler/main.c"
#include <linux/perf_event.h>
#include <sys/ioctl.h>

#define BUSTER_SOURCE_PASSES 21u

typedef struct SourceCheck SourceCheck;
struct SourceCheck
{
    Arena types_arena;
    Arena index_arena;
    Arena input_arena;
    Arena output_arena;
    Arena scratch;
    TypeTable types;
    CheckInput input;
    CheckStream stream;
};

static bool source_check_open(SourceCheck* run, TypeCapacity capacity)
{
    run->types_arena = arena_reserve(type_capacity_bytes(capacity));
    run->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    run->input_arena = arena_reserve(UINT64_C(1) << 36);
    run->output_arena = arena_reserve(UINT64_C(1) << 36);
    run->scratch = arena_reserve(UINT64_C(1) << 36);
    return run->types_arena.base != 0 && run->index_arena.base != 0 && run->input_arena.base != 0 &&
           run->output_arena.base != 0 && run->scratch.base != 0;
}

static void source_check_close(SourceCheck* run)
{
    arena_release(&run->scratch);
    arena_release(&run->output_arena);
    arena_release(&run->input_arena);
    arena_release(&run->index_arena);
    arena_release(&run->types_arena);
}

static bool source_check_run(SourceCheck* run, const Parser* parser, Preprocessor* pp, TypeCapacity capacity, bool scalar)
{
    arena_pop_to(&run->types_arena, 0);
    arena_pop_to(&run->index_arena, 0);
    arena_pop_to(&run->input_arena, 0);
    arena_pop_to(&run->output_arena, 0);
    arena_pop_to(&run->scratch, 0);
    const CheckOptions options = { .scalar = scalar };
    const bool valid = type_initialize(&run->types, &run->types_arena, &run->index_arena, capacity) &&
                       check_input(&run->input, parser, pp, &run->types, &run->input_arena, &run->scratch) &&
                       check_graph_run(&run->types, &run->input.graph, &run->output_arena, &run->scratch, &options, &run->stream) &&
                       run->stream.report_count == 0;
    return valid;
}

static bool source_check_equal(const SourceCheck* a, const SourceCheck* b, uint32_t tokens)
{
    const CheckGraph* x = &a->input.graph;
    const CheckGraph* y = &b->input.graph;
    bool equal = x->count == y->count && x->argument_count == y->argument_count &&
                 check_test_stream_equal(&a->stream, &b->stream, x->argument_count);
    for (uint32_t token = 0; equal && token < tokens; token += 1)
    {
        equal = a->input.declarations[token] == b->input.declarations[token];
#ifdef BUSTER_CHECK_MEASURE_EXTENDED
        equal = equal && a->input.declared_types[token] == b->input.declared_types[token] &&
                a->input.declaration_kinds[token] == b->input.declaration_kinds[token] &&
                a->input.objects[token] == b->input.objects[token] && a->input.static_objects[token] == b->input.static_objects[token] &&
                a->input.registers[token] == b->input.registers[token] && a->input.linkage[token] == b->input.linkage[token] &&
                a->input.defined[token] == b->input.defined[token] && a->input.tentative[token] == b->input.tentative[token] &&
                a->input.initializer_nodes[token] == b->input.initializer_nodes[token];
#endif
    }
    for (uint32_t node = 0; equal && node < x->count; node += 1)
    {
        equal = x->ops[node] == y->ops[node] && x->tokens[node] == y->tokens[node] && x->operand0[node] == y->operand0[node] &&
                x->operand1[node] == y->operand1[node] && x->operand2[node] == y->operand2[node] && x->types[node] == y->types[node] &&
                x->members[node] == y->members[node] && x->contexts[node] == y->contexts[node] &&
                x->argument_offsets[node] == y->argument_offsets[node] && x->argument_counts[node] == y->argument_counts[node];
    }
    for (uint32_t argument = 0; equal && argument < x->argument_count; argument += 1)
    {
        equal = x->argument_nodes[argument] == y->argument_nodes[argument];
    }
    return equal;
}

static uint64_t source_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static uint64_t source_median(const uint64_t* values)
{
    uint64_t ordered[BUSTER_SOURCE_PASSES];
    for (uint32_t i = 0; i < BUSTER_SOURCE_PASSES; i += 1)
    {
        uint32_t at = i;
        while (at != 0 && ordered[at - 1] > values[i])
        {
            ordered[at] = ordered[at - 1];
            at -= 1;
        }
        ordered[at] = values[i];
    }
    return ordered[BUSTER_SOURCE_PASSES / 2];
}

int main(int argc, char** argv)
{
    (void)main_command;
    uint32_t only = 2;
    uint32_t after = 0;
    Arena options_arena = arena_reserve(UINT64_C(1) << 20);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(&options_arena, char*, (uint64_t)argc);
    int argument_count = 0;
    bool valid = arguments != 0;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            const char* value = argv[++i];
            only = strcmp(value, "scalar") == 0 ? 0 : strcmp(value, "vector") == 0 ? 1 : 2;
            valid = only != 2;
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
    valid = valid && pp_parse_options(argument_count, arguments, &pp_options, &options_arena, &path, &ignored_output, &markers) &&
            check_input_target_supported(&pp_options);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, path);
    for (uint32_t p = 0; valid && p < 2; p += 1)
    {
        const ParseOptions options = { .scalar = p == 0 };
        valid = parse_initialize(&parsers[p], &options, pp_options.language) && parse_translation_unit(&parsers[p], &pp);
        parse_print_diagnostics(&parsers[p], &pp);
    }
    const uint32_t tokens = parsers[0].tokens.count;
    const TypeCapacity capacity = {
        .types = tokens < (1u << 27) ? tokens * 4 + 64 : 0,
        .parameters = tokens,
        .tags = tokens,
        .members = tokens,
    };
    SourceCheck runs[3] = { 0 };
    for (uint32_t p = 0; valid && p < 3; p += 1)
    {
        valid = capacity.types != 0 && source_check_open(runs + p, capacity);
    }
    valid = valid && source_check_run(runs + 2, parsers, &pp, capacity, true);
    if (!valid && runs[2].input.report_count != 0)
    {
        const CheckInputReport report = runs[2].input.reports[0];
        check_driver_diagnose(parsers, &pp, report.token, check_driver_input_message(report.message));
    }
    const struct perf_event_attr attribute = {
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
    uint64_t nanoseconds[2][BUSTER_SOURCE_PASSES] = { 0 };
    uint64_t cycles[2][BUSTER_SOURCE_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass <= BUSTER_SOURCE_PASSES; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 2; step += 1)
        {
            const uint32_t p = (pass + step) & 1;
            if (only == 2 || only == p)
            {
                if (p == 1 && after != 0)
                {
                    const uint64_t end = source_time() + (uint64_t)after * 1000000;
                    while (valid && source_time() < end)
                    {
                        valid = source_check_run(runs, parsers, &pp, capacity, true);
                    }
                }
                if (counter >= 0)
                {
                    counters &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
                }
                const uint64_t begin = source_time();
                const bool checked = source_check_run(runs + p, parsers + p, &pp, capacity, p == 0);
                const uint64_t elapsed = source_time() - begin;
                uint64_t counts[3] = { 0 };
                if (counter >= 0)
                {
                    counters &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts);
                    counters &= counts[1] != 0 && counts[1] == counts[2];
                }
                valid = checked && source_check_equal(runs + 2, runs + p, tokens);
                if (pass != 0)
                {
                    nanoseconds[p][pass - 1] = elapsed;
                    cycles[p][pass - 1] = counts[0];
                }
            }
        }
    }
    if (valid)
    {
        printf("source=%s tokens=%u nodes=%u passes=%u after_scalar_ms=%u comparison=identical\n", path, tokens, runs[2].stream.count, BUSTER_SOURCE_PASSES, after);
        for (uint32_t p = 0; p < 2; p += 1)
        {
            if (only == 2 || only == p)
            {
                const double nodes = runs[p].stream.count != 0 ? runs[p].stream.count : 1;
                const CheckStatistics statistics = runs[p].stream.statistics;
                printf("path=%s ns=%" PRIu64 " ns_per_node=%.3f cycles_per_node=%.3f scalar_nodes=%u vector_nodes=%u input_bytes=%" PRIu64 " output_bytes=%" PRIu64 "\n",
                       p == 0 ? "scalar" : "vector", source_median(nanoseconds[p]), (double)source_median(nanoseconds[p]) / nodes,
                       counters ? (double)source_median(cycles[p]) / nodes : -1.0, statistics.scalar_nodes, statistics.vector_nodes,
                       runs[p].input_arena.length, runs[p].output_arena.length);
#ifdef BUSTER_CHECK_MEASURE_EXTENDED
                const CBindingStatistics binding = runs[p].input.binding_statistics;
                printf("path=%s binding_events=%u identifiers=%" PRIu64 " declarations=%" PRIu64 " walks=%" PRIu64 " walked_uses=%" PRIu64 " fallback_uses=%" PRIu64 "\n",
                       p == 0 ? "scalar" : "vector", runs[p].input.binding_event_count, binding.identifiers,
                       binding.declarations, binding.walks, binding.walked_uses, binding.fallback_uses);
#endif
            }
        }
    }
    else
    {
        fprintf(stderr, "source measurement refused: invalid options, unsupported input, resource failure or unequal results\n");
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
