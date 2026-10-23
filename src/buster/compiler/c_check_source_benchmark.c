// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-body graph measurement. Include after c_check_corpus.c. The complete source must pass both checker
// paths, including initializer validation, before a body graph is selected. Preparation and extraction stay
// outside timing; each sample includes graph validation, dependency scheduling, actions and stream publication.
// Type interning is warm after full-source validation. This is explicitly a body-graph measurement, not the
// reader/initializer pipeline. Pair it with the full-source measurement for producer/consumer performance claims.
#ifndef BUSTER_C_CHECK_SOURCE_BENCHMARK_INCLUDED
#define BUSTER_C_CHECK_SOURCE_BENCHMARK_INCLUDED

#include <linux/perf_event.h>
#include <sys/ioctl.h>

#define BUSTER_CHECK_SOURCE_MAX_PASSES 101u

static uint64_t check_source_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static uint64_t check_source_median(const uint64_t* values, uint32_t count)
{
    uint64_t ordered[BUSTER_CHECK_SOURCE_MAX_PASSES];
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t at = i;
        while (at != 0 && ordered[at - 1] > values[i])
        {
            ordered[at] = ordered[at - 1];
            at -= 1;
        }
        ordered[at] = values[i];
    }
    return count != 0 ? ordered[count / 2] : 0;
}

static bool check_source_body_graph(CheckGraph* graph, const CheckInput* input, const Parser* parser, const Preprocessor* pp,
                                    const char* name, Arena* output, Arena* scratch, uint32_t* bodies, uint32_t** original_mapping)
{
    const CheckGraph* source = &input->graph;
    const uint64_t mark = scratch->length;
    uint8_t* tokens = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)parser->tokens.count + 1);
    uint8_t* selected = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)source->count + 1);
    uint32_t* mapping = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)source->count + 1);
    *original_mapping = mapping;
    bool valid = tokens != 0 && selected != 0 && mapping != 0;
    *bodies = 0;
    if (valid)
    {
        memset(tokens, 0, (uint64_t)parser->tokens.count + 1);
        memset(selected, 0, (uint64_t)source->count + 1);
        memset(mapping, 0xff, ((uint64_t)source->count + 1) * sizeof(uint32_t));
    }
    for (uint32_t token = 0; valid && token < input->token_count; token += 1)
    {
        const uint32_t declaration = input->body_declarations[token];
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            const bool matches = name == 0 || (intern_length(pp->table, identifier) == strlen(name) &&
                                               memcmp(intern_bytes(pp->table, identifier), name, strlen(name)) == 0);
            if (matches)
            {
                const uint32_t closing = parser->tree.parents[token];
                valid = closing >= token && closing < input->token_count;
                if (valid)
                {
                    memset(tokens + token, 1, (uint64_t)closing - token + 1);
                    *bodies += 1;
                }
            }
        }
    }
    for (uint32_t node = 0; valid && node < source->count; node += 1)
    {
        selected[node] = source->tokens[node] < input->token_count && tokens[source->tokens[node]] != 0;
    }
    // Operands always precede their parent, so one reverse scan closes the selected roots over all dependencies.
    for (uint32_t node = source->count; valid && node != 0; )
    {
        node -= 1;
        if (selected[node])
        {
            const uint32_t operands[] =
            {
                source->operand0[node],
                source->operand1[node],
                source->operand2[node],
            };
            for (uint32_t edge = 0; edge < BUSTER_ARRAY_LENGTH(operands); edge += 1)
            {
                if (operands[edge] != BUSTER_CHECK_NONE)
                {
                    selected[operands[edge]] = 1;
                }
            }
            for (uint32_t argument = 0; check_graph_has_arguments(source->ops[node]) && argument < source->argument_counts[node]; argument += 1)
            {
                selected[source->argument_nodes[source->argument_offsets[node] + argument]] = 1;
            }
        }
    }
    valid = valid && *bodies != 0 && check_graph_initialize(graph, output, source->count, source->argument_count);
    if (valid)
    {
        graph->extensions = source->extensions;
        graph->tag_completions = source->tag_completions;
        graph->tag_completion_count = source->tag_completion_count;
        graph->transparent_unions = source->transparent_unions;
        graph->fp_environments = source->fp_environments != NULL ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, source->count) : NULL;
        graph->warning_suppressed = source->warning_suppressed != NULL ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, source->count) : NULL;
        graph->varargs_calls = source->varargs_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckVarargsCall, source->varargs_count) : 0;
        graph->varargs_capacity = source->varargs_count;
        graph->builtin_calls = source->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinCall, source->builtin_count) : 0;
        graph->builtin_capacity = source->builtin_count;
        graph->vector_members = source->vector_member_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckVectorCall, source->vector_member_count) : NULL;
        graph->vector_member_capacity = source->vector_member_count;
        graph->x86_calls = source->x86_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckX86Call, source->x86_count) : NULL;
        graph->x86_capacity = source->x86_count;
        valid = (source->varargs_count == 0 || graph->varargs_calls != 0) && (source->builtin_count == 0 || graph->builtin_calls != 0) &&
                (source->fp_environments == NULL || graph->fp_environments != NULL) &&
                (source->warning_suppressed == NULL || graph->warning_suppressed != NULL) &&
                (source->vector_member_count == 0 || graph->vector_members != NULL) && (source->x86_count == 0 || graph->x86_calls != NULL);
    }
    uint32_t builtin = 0;
    uint32_t vector = 0;
    uint32_t x86 = 0;
    for (uint32_t node = 0; valid && node < source->count; node += 1)
    {
        if (selected[node])
        {
            const uint32_t a = source->operand0[node];
            const uint32_t b = source->operand1[node];
            const uint32_t c = source->operand2[node];
            const uint32_t added = check_graph_add(graph, source->ops[node], source->tokens[node],
                                                   a == BUSTER_CHECK_NONE ? a : mapping[a],
                                                   b == BUSTER_CHECK_NONE ? b : mapping[b],
                                                   c == BUSTER_CHECK_NONE ? c : mapping[c]);
            valid = added != BUSTER_CHECK_NONE;
            if (valid)
            {
                mapping[node] = added;
                graph->leaves[added] = source->leaves[node];
                graph->types[added] = source->types[node];
                graph->members[added] = source->members[node];
                graph->contexts[added] = source->contexts[node];
                graph->source_positions[added] = source->source_positions != 0 ? source->source_positions[node] : BUSTER_CHECK_NONE;
                if (source->fp_environments != NULL)
                {
                    graph->fp_environments[added] = source->fp_environments[node];
                }
                if (source->warning_suppressed != NULL)
                {
                    graph->warning_suppressed[added] = source->warning_suppressed[node];
                }
                if (source->ops[node] == CHECK_OP_VARARGS)
                {
                    graph->argument_offsets[added] = graph->varargs_count;
                    graph->argument_counts[added] = source->argument_counts[node];
                    CheckVarargsCall* call = graph->varargs_calls + graph->varargs_count++;
                    *call = source->varargs_calls[source->argument_offsets[node]];
                    call->node = added;
                }
                else if (check_graph_has_arguments(source->ops[node]) && source->argument_counts[node] != 0)
                {
                    graph->argument_offsets[added] = graph->argument_count;
                    graph->argument_counts[added] = source->argument_counts[node];
                    for (uint32_t argument = 0; argument < source->argument_counts[node]; argument += 1)
                    {
                        graph->argument_nodes[graph->argument_count++] = mapping[source->argument_nodes[source->argument_offsets[node] + argument]];
                    }
                }
                while (builtin < source->builtin_count && source->builtin_calls[builtin].node < node)
                {
                    builtin += 1;
                }
                if (builtin < source->builtin_count && source->builtin_calls[builtin].node == node)
                {
                    CheckBuiltinCall* call = graph->builtin_calls + graph->builtin_count++;
                    *call = source->builtin_calls[builtin++];
                    call->node = added;
                }
                while (vector < source->vector_member_count && source->vector_members[vector].node < node)
                {
                    vector += 1;
                }
                if (vector < source->vector_member_count && source->vector_members[vector].node == node)
                {
                    CheckVectorCall* call = graph->vector_members + graph->vector_member_count++;
                    *call = source->vector_members[vector++];
                    call->node = added;
                }
                while (x86 < source->x86_count && source->x86_calls[x86].node < node)
                {
                    x86 += 1;
                }
                if (x86 < source->x86_count && source->x86_calls[x86].node == node)
                {
                    CheckX86Call* call = graph->x86_calls + graph->x86_count++;
                    *call = source->x86_calls[x86++];
                    call->node = added;
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid && graph->count != 0;
}

static bool check_source_body_equal(const CheckGraph* source_graph, const CheckStream* source, const CheckGraph* graph,
                                    const CheckStream* stream, const uint32_t* mapping)
{
    bool equal = true;
    for (uint32_t node = 0; equal && node < source_graph->count; node += 1)
    {
        const uint32_t selected = mapping[node];
        if (selected != BUSTER_CHECK_NONE)
        {
            CheckValue expected = source->values[node];
            if (expected.fault != CHECK_FAULT_NONE && expected.fault_node != BUSTER_CHECK_NONE && expected.fault_node < source_graph->count)
            {
                expected.fault_node = mapping[expected.fault_node];
            }
            equal = check_test_value_equal(&expected, stream->values + selected) && source->diagnostics[node] == stream->diagnostics[selected] &&
                    source->warnings[node] == stream->warnings[selected] && source->warning_ignored[node] == stream->warning_ignored[selected];
            const CheckNodeConversions* a = source->conversions + node;
            const CheckNodeConversions* b = stream->conversions + selected;
            for (uint32_t operand = 0; equal && operand < 3; operand += 1)
            {
                equal = a->operands[operand].type == b->operands[operand].type && a->operands[operand].flags == b->operands[operand].flags;
            }
            equal = equal && a->store.type == b->store.type && a->store.flags == b->store.flags;
            for (uint32_t argument = 0; equal && check_graph_has_arguments(graph->ops[selected]) && argument < graph->argument_counts[selected]; argument += 1)
            {
                const CheckConversion* x = source->argument_conversions + source_graph->argument_offsets[node] + argument;
                const CheckConversion* y = stream->argument_conversions + graph->argument_offsets[selected] + argument;
                equal = x->type == y->type && x->flags == y->flags;
            }
        }
    }
    uint32_t selected_effects = 0;
    for (uint32_t effect = 0; equal && effect < source->effect_count; effect += 1)
    {
        const CheckStoreEffect* expected = source->effects + effect;
        equal = expected->node < source_graph->count;
        if (equal && mapping[expected->node] != BUSTER_CHECK_NONE)
        {
            equal = selected_effects < stream->effect_count;
            if (equal)
            {
                const CheckStoreEffect* actual = stream->effects + selected_effects;
                CheckStoreEffect remapped = *expected;
                remapped.node = mapping[expected->node];
                if (remapped.value.fault != CHECK_FAULT_NONE && remapped.value.fault_node != BUSTER_CHECK_NONE && remapped.value.fault_node < source_graph->count)
                {
                    remapped.value.fault_node = mapping[remapped.value.fault_node];
                }
                if (remapped.destination.fault != CHECK_FAULT_NONE && remapped.destination.fault_node != BUSTER_CHECK_NONE && remapped.destination.fault_node < source_graph->count)
                {
                    remapped.destination.fault_node = mapping[remapped.destination.fault_node];
                }
                equal = check_store_effect_equal(&remapped, actual, check_test_value_equal);
                selected_effects += 1;
            }
        }
    }
    equal = equal && selected_effects == stream->effect_count;
    uint32_t selected_varargs = 0;
    for (uint32_t effect = 0; equal && effect < source->varargs_count; effect += 1)
    {
        const CheckVarargsEffect* expected = source->varargs + effect;
        equal = expected->node < source_graph->count;
        if (equal && mapping[expected->node] != BUSTER_CHECK_NONE)
        {
            equal = selected_varargs < stream->varargs_count;
            if (equal)
            {
                const CheckVarargsEffect* actual = stream->varargs + selected_varargs++;
                equal = actual->node == mapping[expected->node] && actual->requested_type == expected->requested_type &&
                        actual->evaluated_operands == expected->evaluated_operands && actual->warnings == expected->warnings &&
                        actual->mutates_list == expected->mutates_list;
            }
        }
    }
    equal = equal && selected_varargs == stream->varargs_count;
    uint32_t selected_misc = 0;
    for (uint32_t effect = 0; equal && effect < source->misc_count; effect += 1)
    {
        CheckBuiltinMiscEffect expected = source->misc[effect];
        equal = expected.node < source_graph->count;
        if (equal && mapping[expected.node] != BUSTER_CHECK_NONE)
        {
            expected.node = mapping[expected.node];
            CheckValue* value = &expected.constant_value;
            if (value->fault != CHECK_FAULT_NONE && value->fault_node != BUSTER_CHECK_NONE && value->fault_node < source_graph->count)
            {
                value->fault_node = mapping[value->fault_node];
            }
            equal = selected_misc < stream->misc_count &&
                    check_builtin_misc_effect_equal(&expected, stream->misc + selected_misc++, check_test_value_equal);
        }
    }
    equal = equal && selected_misc == stream->misc_count;
    uint32_t selected_float = 0;
    for (uint32_t effect = 0; equal && effect < source->floating_count; effect += 1)
    {
        CheckBuiltinFloatEffect expected = source->floating[effect];
        equal = expected.node < source_graph->count;
        if (equal && mapping[expected.node] != BUSTER_CHECK_NONE)
        {
            expected.node = mapping[expected.node];
            for (uint32_t argument = 0; argument < BUSTER_ARRAY_LENGTH(expected.converted_arguments); argument += 1)
            {
                CheckValue* value = expected.converted_arguments + argument;
                if (value->fault != CHECK_FAULT_NONE && value->fault_node != BUSTER_CHECK_NONE && value->fault_node < source_graph->count)
                {
                    value->fault_node = mapping[value->fault_node];
                }
            }
            equal = selected_float < stream->floating_count &&
                    check_builtin_float_effect_equal(&expected, stream->floating + selected_float++, check_test_value_equal);
        }
    }
    equal = equal && selected_float == stream->floating_count;
    uint32_t selected_query = 0;
    for (uint32_t effect = 0; equal && effect < source->query_count; effect += 1)
    {
        CheckBuiltinQueryEffect expected = source->queries[effect];
        equal = expected.node < source_graph->count;
        if (equal && mapping[expected.node] != BUSTER_CHECK_NONE)
        {
            expected.node = mapping[expected.node];
            if (expected.runtime_value.fault != CHECK_FAULT_NONE && expected.runtime_value.fault_node != BUSTER_CHECK_NONE && expected.runtime_value.fault_node < source_graph->count)
            {
                expected.runtime_value.fault_node = mapping[expected.runtime_value.fault_node];
            }
            equal = selected_query < stream->query_count && check_builtin_query_effect_equal(&expected, stream->queries + selected_query++, check_test_value_equal);
        }
    }
    equal = equal && selected_query == stream->query_count;
    uint32_t selected_atomic = 0;
    for (uint32_t effect = 0; equal && effect < source->atomic_count; effect += 1)
    {
        CheckBuiltinAtomicEffect expected = source->atomics[effect];
        equal = expected.node < source_graph->count;
        if (equal && mapping[expected.node] != BUSTER_CHECK_NONE)
        {
            expected.node = mapping[expected.node];
            CheckValue* values[] =
            {
                &expected.object_address,
                &expected.store_value,
            };
            for (uint32_t value = 0; value < BUSTER_ARRAY_LENGTH(values); value += 1)
            {
                if (values[value]->fault != CHECK_FAULT_NONE && values[value]->fault_node != BUSTER_CHECK_NONE && values[value]->fault_node < source_graph->count)
                {
                    values[value]->fault_node = mapping[values[value]->fault_node];
                }
            }
            equal = selected_atomic < stream->atomic_count &&
                    check_builtin_atomic_effect_equal(&expected, stream->atomics + selected_atomic++, check_test_value_equal);
        }
    }
    equal = equal && selected_atomic == stream->atomic_count;
    uint32_t selected_vector = 0;
    for (uint32_t effect = 0; equal && effect < source->vector_count; effect += 1)
    {
        CheckBuiltinVectorEffect expected = source->vectors[effect];
        equal = expected.node < source_graph->count;
        if (equal && mapping[expected.node] != BUSTER_CHECK_NONE)
        {
            expected.node = mapping[expected.node];
            for (uint32_t argument = 0; argument < BUSTER_ARRAY_LENGTH(expected.converted_arguments); argument += 1)
            {
                CheckValue* value = expected.converted_arguments + argument;
                if (value->fault != CHECK_FAULT_NONE && value->fault_node != BUSTER_CHECK_NONE && value->fault_node < source_graph->count)
                {
                    value->fault_node = mapping[value->fault_node];
                }
            }
            equal = selected_vector < stream->vector_count &&
                    check_builtin_vector_effect_equal(&expected, stream->vectors + selected_vector++, check_test_value_equal);
        }
    }
    equal = equal && selected_vector == stream->vector_count;
    uint32_t selected_x86 = 0;
    for (uint32_t effect = 0; equal && effect < source->x86_count; effect += 1)
    {
        CheckBuiltinX86Effect expected = source->x86[effect];
        equal = expected.node < source_graph->count;
        if (equal && mapping[expected.node] != BUSTER_CHECK_NONE)
        {
            expected.node = mapping[expected.node];
            for (uint32_t range = 0; range < expected.range_event_count; range += 1)
            {
                CheckBuiltinX86RangeEvent* event = expected.range_events + range;
                if (event->node < source_graph->count)
                {
                    event->node = mapping[event->node];
                }
                CheckValue* value = &event->value;
                if (value->fault != CHECK_FAULT_NONE && value->fault_node != BUSTER_CHECK_NONE && value->fault_node < source_graph->count)
                {
                    value->fault_node = mapping[value->fault_node];
                }
            }
            equal = selected_x86 < stream->x86_count &&
                    check_builtin_x86_effect_equal(&expected, stream->x86 + selected_x86++, check_test_value_equal);
        }
    }
    equal = equal && selected_x86 == stream->x86_count;
    return equal;
}

static bool check_source_graph_run(TypeTable* types, const CheckGraph* graph, Arena* output, Arena* scratch, bool scalar, CheckStream* stream)
{
    arena_pop_to(output, 0);
    const CheckOptions options = {
        .scalar = scalar,
    };
    return check_graph_run(types, graph, output, scratch, &options, stream) && stream->report_count == 0;
}

static int run_checker_source_benchmark(int count, char** arguments)
{
    const char* body = 0;
    uint64_t passes = 21;
    uint64_t iterations = 0;
    uint64_t after = 0;
    uint32_t only = 2;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    char** own = BUSTER_ARENA_PUSH_ARRAY(&option_arena, char*, (uint64_t)count + 1);
    uint32_t used = 0;
    bool valid = own != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        const char* option = arguments[i];
        if (strcmp(option, "--body") == 0 || strcmp(option, "--only") == 0 || strcmp(option, "--passes") == 0 ||
            strcmp(option, "--iterations") == 0 || strcmp(option, "--after-scalar") == 0)
        {
            valid = i + 1 < count;
            const char* value = valid ? arguments[++i] : "";
            if (strcmp(option, "--body") == 0)
            {
                body = value;
            }
            else if (strcmp(option, "--only") == 0)
            {
                only = strcmp(value, "scalar") == 0 ? 0 : strcmp(value, "vector") == 0 ? 1 : 2;
                valid = valid && only != 2;
            }
            else
            {
                uint64_t* destination = strcmp(option, "--passes") == 0 ? &passes : strcmp(option, "--iterations") == 0 ? &iterations : &after;
                valid = valid && check_corpus_number(value, destination);
            }
        }
        else if (strcmp(option, "--no-default-config") != 0)
        {
            own[used++] = arguments[i];
        }
    }
    PPOptions pp_options;
    const char* file = 0;
    const char* output_file = 0;
    bool markers = false;
    valid = valid && passes != 0 && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES && iterations <= 1000000 && after <= 1000 &&
            pp_parse_options((int)used, own, &pp_options, &option_arena, &file, &output_file, &markers) && output_file == 0 && file != 0;
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    CheckCorpusPath paths[2] = { 0 };
    uint64_t validation[2] = { 0 };
    pp_options.semantic_pragmas = true;
    valid = valid && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) && pp_run(&pp, file);
    for (uint32_t path = 0; valid && path < 2; path += 1)
    {
        const uint64_t begin = check_source_time();
        valid = check_corpus_check_policy(paths + path, parsers + path, &pp, path == 0, pp_options.quiet);
        validation[path] = check_source_time() - begin;
        if (!valid)
        {
            fprintf(stderr, "source benchmark: %s:%u:%u: %s\n", paths[path].file, paths[path].line, paths[path].column, paths[path].message);
        }
    }
    valid = valid && check_corpus_equal(paths, paths + 1, parsers, parsers + 1);
    Arena graph_arena = arena_reserve(UINT64_C(1) << 36);
    Arena oracle_arena = arena_reserve(UINT64_C(1) << 36);
    Arena outputs[2] =
    {
        arena_reserve(UINT64_C(1) << 36),
        arena_reserve(UINT64_C(1) << 36),
    };
    Arena scratch = arena_reserve(UINT64_C(1) << 36);
    valid = valid && graph_arena.base != 0 && oracle_arena.base != 0 && outputs[0].base != 0 && outputs[1].base != 0 && scratch.base != 0;
    CheckGraph graph = { 0 };
    uint32_t bodies = 0;
    uint32_t* mapping = 0;
    const uint64_t selection_begin = check_source_time();
    valid = valid && check_source_body_graph(&graph, &paths[0].input, parsers, &pp, body, &graph_arena, &scratch, &bodies, &mapping);
    const uint64_t selection_ns = check_source_time() - selection_begin;
    CheckStream reference = { 0 };
    CheckStream streams[2] = { 0 };
    valid = valid && check_source_graph_run(&paths[0].types, &graph, &oracle_arena, &scratch, true, &reference);
    valid = valid && check_source_body_equal(&paths[0].input.graph, &paths[0].stream, &graph, &reference, mapping);
    for (uint32_t path = 0; valid && path < 2; path += 1)
    {
        valid = check_source_graph_run(&paths[path].types, &graph, outputs + path, &scratch, path == 0, streams + path) &&
                check_test_stream_equal(&reference, streams + path, graph.argument_count);
    }
    if (iterations == 0)
    {
        iterations = graph.count != 0 ? 1 + 1000000 / graph.count : 1;
        iterations = iterations <= 1024 ? iterations : 1024;
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
    uint64_t nanoseconds[2][BUSTER_CHECK_SOURCE_MAX_PASSES] = { 0 };
    uint64_t cycles[2][BUSTER_CHECK_SOURCE_MAX_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 2; step += 1)
        {
            const uint32_t path = (pass + step) & 1;
            if (only == 2 || only == path)
            {
                if (path == 1 && after != 0)
                {
                    const uint64_t until = check_source_time() + after * 1000000;
                    while (valid && check_source_time() < until)
                    {
                        valid = check_source_graph_run(&paths[0].types, &graph, outputs, &scratch, true, streams);
                    }
                }
                if (counter >= 0)
                {
                    counters &= ioctl(counter, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
                }
                const uint64_t begin = check_source_time();
                for (uint64_t iteration = 0; valid && iteration < iterations; iteration += 1)
                {
                    valid = check_source_graph_run(&paths[path].types, &graph, outputs + path, &scratch, path == 0, streams + path);
                }
                nanoseconds[path][pass] = check_source_time() - begin;
                uint64_t counts[3] = { 0 };
                if (counter >= 0)
                {
                    counters &= ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, counts, sizeof(counts)) == sizeof(counts) &&
                                counts[1] != 0 && counts[1] == counts[2];
                }
                cycles[path][pass] = counts[0];
                valid = valid && check_test_stream_equal(&reference, streams + path, graph.argument_count);
            }
        }
    }
    if (valid)
    {
        printf("checker-source-benchmark: source=%s bodies=%u selection=%s tokens=%u source_nodes=%u body_nodes=%u source_effects=%u body_effects=%u passes=%" PRIu64
               " iterations=%" PRIu64 " after_scalar_ms=%" PRIu64 " type_interning=warm comparison=identical\n",
               file, bodies, body != 0 ? body : "all", parsers[0].tokens.count, paths[0].input.graph.count, graph.count,
               paths[0].stream.effect_count, reference.effect_count, passes, iterations, after);
        printf("preparation: scalar_validation_ns=%" PRIu64 " vector_validation_ns=%" PRIu64 " extraction_ns=%" PRIu64
               " graph_bytes=%" PRIu64 " binding_events=%u binding_walked_uses=%" PRIu64 " binding_fallback_uses=%" PRIu64 "\n",
               validation[0], validation[1], selection_ns, graph_arena.length, paths[1].input.parser_binding_event_count,
               paths[1].input.parser_binding_statistics.walked_uses, paths[1].input.parser_binding_statistics.fallback_uses);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (only == 2 || only == path)
            {
                const double nodes = (double)graph.count * (double)iterations;
                const CheckStatistics statistics = streams[path].statistics;
                printf("body_graph: path=%s ns_per_node=%.3f cycles_per_node=%.3f cycles_available=%u scalar_nodes=%u vector_nodes=%u scalar_share=%.6f output_bytes=%" PRIu64 "\n",
                       path == 0 ? "scalar" : "vector", (double)check_source_median(nanoseconds[path], (uint32_t)passes) / nodes,
                       counters ? (double)check_source_median(cycles[path], (uint32_t)passes) / nodes : -1.0, counters,
                       statistics.scalar_nodes, statistics.vector_nodes, (double)statistics.scalar_nodes / graph.count, outputs[path].length);
            }
        }
    }
    else
    {
        fprintf(stderr, "source benchmark refused: invalid options, rejected source, missing body, resource failure or unequal results\n");
    }
    if (counter >= 0)
    {
        close(counter);
    }
    arena_release(&scratch);
    arena_release(outputs + 1);
    arena_release(outputs);
    arena_release(&oracle_arena);
    arena_release(&graph_arena);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        check_corpus_close(paths + path);
        parse_release(parsers + path);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&option_arena);
    return valid ? 0 : 1;
}

#endif
