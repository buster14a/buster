// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exact catalogue prototypes, original Clang immediate mutations and compact x86 graph contracts.
#ifndef BUSTER_C_CHECK_X86_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_X86_GRAPH_TEST_INCLUDED

static void check_test_x86_graph_oracles(CheckTest* test, const char* clang)
{
    const char* sources[] = {
        "void f(void){__builtin_ia32_clui();__builtin_ia32_stui();}unsigned long long g(void){return __builtin_ia32_rdfsbase64();}\n",
        "void f(void){__builtin_ia32_clui(1);}\n",
        "typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,255);}\n",
        "typedef int V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_pshufd512(x,256);}\n",
        "typedef int V __attribute__((vector_size(64)));V f(V x,int n){return __builtin_ia32_pshufd512(x,n);}\n",
        "typedef float V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_rndscaleps_mask(x,0,x,65535,4);}\n",
        "typedef float V __attribute__((vector_size(64)));V f(V x){return __builtin_ia32_rndscaleps_mask(x,0,x,65535,5);}\n",
    };
    const bool rejected[] = { false, true, false, true, true, false, true };
    char directory[] = "/tmp/buster-check-x86-graph-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    bool outcomes[BUSTER_ARRAY_LENGTH(sources)] = { 0 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    char output_path[160];
    char error_path[160];
    snprintf(output_path, sizeof(output_path), "%s/out", directory);
    snprintf(error_path, sizeof(error_path), "%s/err", directory);
    bool checked = ready;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", paths[i], NULL };
        const int status = object_test_run(arguments, output_path, error_path);
        checked &= status == 0 || status == 1;
        outcomes[i] = status != 0;
    }
    check_test_expect(test, checked, "independent exact-prototype and immediate x86 oracle runs");
    for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        char description[96];
        snprintf(description, sizeof(description), "x86 prototype or immediate mutation %u agrees with Clang", i);
        check_test_expect(test, outcomes[i] == rejected[i], description);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    unlink(output_path);
    unlink(error_path);
    rmdir(directory);
}

static __attribute__((noinline)) void check_test_x86_graph(CheckTest* test, TypeTable* table, const char* clang)
{
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(131072);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, &storage, 64, 32);
    check_test_expect(test, ready, "compact x86 graph fixture storage");
    const char* names[] = { "__builtin_ia32_clui", "__builtin_ia32_stui", "__builtin_ia32_rdfsbase64", "__builtin_ia32_pshufd512", "__builtin_ia32_rndscaleps_mask" };
    CheckX86Call calls[BUSTER_ARRAY_LENGTH(names)];
    uint32_t nodes[BUSTER_ARRAY_LENGTH(names)] = { 0 };
    const TypeId expected[] = { BUSTER_TYPE_VOID, BUSTER_TYPE_VOID, BUSTER_TYPE_ULLONG,
                               type_vector(table, BUSTER_TYPE_INT, 16, false), type_vector(table, BUSTER_TYPE_FLOAT, 16, false) };
    graph.x86_calls = calls;
    graph.x86_capacity = BUSTER_ARRAY_LENGTH(calls);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(names[i], (uint32_t)strlen(names[i]));
        BuiltinIntrinsicTypeResult declaration = { 0 };
        if (info != NULL)
        {
            const BuiltinIntrinsicTypeInput input = {
                .encoding = info->encoding,
                .encoding_length = info->encoding_length,
                .attributes = info->attributes,
                .attributes_length = info->attributes_length,
                .custom_typechecking = info->custom_typechecking,
            };
            builtin_intrinsic_types_decode(table, &input, &declaration);
        }
        const bool decoded = info != NULL && declaration.status == BUILTIN_INTRINSIC_TYPES_SUCCESS;
        check_test_expect(test, decoded && declaration.result_type == expected[i], "frozen x86 declaration has independently expected prototype result");
        if (decoded)
        {
            uint32_t arguments[5];
            for (uint32_t j = 0; j < declaration.parameter_count; j += 1)
            {
                CheckValue value = { .type = declaration.parameters[j] };
                if (check_action_is_integer(table, value.type))
                {
                    value = check_test_integer(value.type, i == 3 ? 255 : j == 3 ? 65535 : j == 4 ? 4 : 0);
                }
                arguments[j] = check_test_leaf(&graph, graph.count + 1, value);
            }
            nodes[i] = check_test_special_node(&graph, CHECK_OP_BUILTIN_X86, CBUILTIN_NONE, arguments, declaration.parameter_count);
            graph.types[nodes[i]] = declaration.function_type;
            calls[graph.x86_count++] = (CheckX86Call){
                .node = nodes[i],
                .info = info,
                .required_ice_arguments = declaration.ice_parameters,
                .ice_arguments = declaration.ice_parameters | check_graph_x86_ice_mask(info, false),
                .reachability = CHECK_BUILTIN_X86_REACHABLE,
            };
        }
    }
    if (ready && graph.x86_count == BUSTER_ARRAY_LENGTH(calls))
    {
        const CheckOptions options = { .scalar = true };
        CheckStream scalar;
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &scalar);
        check_test_expect(test, checked && scalar.report_count == 0 && scalar.x86_count == graph.x86_count,
                          "exact x86 calls retain all argument conversions and compact constraint results");
        for (uint32_t i = 0; checked && i < graph.x86_count; i += 1)
        {
            check_test_expect(test, scalar.values[nodes[i]].type == expected[i] && scalar.x86[i].status == CHECK_BUILTIN_X86_OK &&
                                   scalar.x86[i].node == nodes[i] && scalar.x86[i].info == calls[i].info && scalar.x86[i].range_event_count == 0,
                              "x86 semantic result keeps exact type, metadata identity and clean immediate status");
        }
        if (checked)
        {
            check_test_expect(test, scalar.x86[3].checked_ice_arguments == 2 && scalar.x86[4].checked_ice_arguments == 18 &&
                                   check_graph_x86_ice_mask(calls[4].info, true) == 16,
                              "decoded and handwritten x86 ICE constraints retain independent source positions");
            const uint64_t mark = output.length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                CheckStream actual;
                const CheckOptions partition = { .batch_size = batch };
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &partition, &actual) &&
                                       check_test_stream_equal(&scalar, &actual, graph.argument_count),
                                  "all graph partitions preserve x86 conversions and event metadata");
                arena_pop_to(&output, mark);
            }
            const uint32_t immediate = graph.argument_nodes[graph.argument_offsets[nodes[3]] + 1];
            graph.leaves[immediate].integer = 256;
            CheckStream range;
            const bool ranged = check_graph_run(table, &graph, &output, &scratch, &options, &range);
            check_test_expect(test, ranged && range.diagnostics[nodes[3]] == CHECK_DIAG_NONE && range.x86[3].range_event_count == 1 &&
                                   range.x86[3].range_events[0].disposition == CHECK_BUILTIN_X86_READY &&
                                   range.x86[3].range_events[0].default_error && range.x86[3].range_events[0].value.integer == 256,
                              "out-of-range x86 immediate is an exact default-error policy event, separate from constraints");
            arena_pop_to(&output, mark);
            calls[3].ice_arguments = 0;
            CheckStream missing;
            check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &options, &missing) &&
                                   missing.diagnostics[nodes[3]] == CHECK_DIAG_INCOMPATIBLE &&
                                   missing.x86[3].constraint == CHECK_BUILTIN_X86_CONSTRAINT_ICE,
                              "a numeric payload cannot impersonate original source ICE proof");
            arena_pop_to(&output, mark);
            calls[3].ice_arguments = 2;
            graph.leaves[immediate].integer = 255;
            const uint32_t saved = calls[0].node;
            calls[0].node = nodes[1];
            CheckStream invalid;
            check_test_expect(test, !check_graph_run(table, &graph, &output, &scratch, &options, &invalid) &&
                                   invalid.status == CHECK_STATUS_INVALID_GRAPH && invalid.x86 == NULL && invalid.x86_count == 0 &&
                                   output.length == mark && scratch.length == 0,
                              "out-of-order x86 context rejects atomically");
            calls[0].node = saved;
        }
        CheckGraph small = graph;
        small.count = nodes[0] + 1;
        small.argument_count = 0;
        small.x86_count = 1;
        check_test_special_graph_bounds(test, table, &small);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    check_test_x86_graph_oracles(test, clang);
}

#endif
