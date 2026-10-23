// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent expectations for generic builtin graph conversion/evaluation contracts and bounded publication.
#ifndef BUSTER_C_CHECK_SPECIAL_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_SPECIAL_GRAPH_TEST_INCLUDED

static uint32_t check_test_special_node(CheckGraph* graph, CheckOp op, CBuiltinId id, const uint32_t* arguments, uint32_t count)
{
    uint32_t node = BUSTER_CHECK_NONE;
    if (count <= graph->argument_capacity - graph->argument_count)
    {
        node = check_graph_add(graph, op, graph->count + 10, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            graph->members[node] = id;
            graph->argument_offsets[node] = graph->argument_count;
            graph->argument_counts[node] = count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                graph->argument_nodes[graph->argument_count++] = arguments[i];
            }
        }
    }
    return node;
}

static void check_test_special_graph_oracles(CheckTest* test, const char* clang)
{
    const char* sources[] = {
        "int side(void);_Static_assert(__builtin_expect(7,side())==7,\"constant evaluator\");\n",
        "int side(void);_Static_assert(__builtin_expect(7,side())==8,\"mutation\");\n",
        "_Static_assert(__builtin_constant_p(1/0)==0,\"unevaluated fault\");\n",
        "char x[4];_Static_assert(__builtin_object_size(x,0)==4,\"object witness\");\n",
        "char x[4];_Static_assert(__builtin_object_size(x,0)==3,\"mutated witness\");\n",
        "int side(void);_Static_assert(__builtin_fpclassify(side(),side(),7,side(),side(),1.0)==7,\"selected classification\");\n",
        "int side(void);_Static_assert(__builtin_fpclassify(side(),side(),7,side(),side(),1.0)==8,\"mutated classification\");\n",
        "_Static_assert(__builtin_isnan(__builtin_nan(\"0x1\")),\"payload\");\n",
        "volatile signed char x;void f(void){__atomic_store_n(&x,255,5);__sync_synchronize();}\n",
        "const signed char x=0;void f(void){__atomic_store_n(&x,255,5);}\n",
        "void f(void){__sync_synchronize(1);}\n",
        "_Static_assert(__builtin_object_size(\"abc\",0)==4,\"runtime-only literal whole-object size\");\n",
        "_Static_assert(__builtin_object_size(\"abc\",1)==4,\"source literal subobject size\");\n",
        "_Static_assert(__builtin_object_size(\"abc\",2)==4,\"runtime-only literal minimum whole-object size\");\n",
        "_Static_assert(__builtin_object_size(\"abc\",3)==4,\"source literal minimum subobject size\");\n",
        "_Static_assert(__builtin_constant_p(1.0+2.0i),\"complex constant proof\");\n",
        "int f(void){return __builtin_flt_rounds();}\n",
        "_Static_assert(__builtin_flt_rounds()==1,\"runtime rounding mode\");\n",
        "_Static_assert(__builtin_fabsf(-2.0f)==2.0f && __builtin_isinf_sign(-__builtin_inf())==-1,\"additional FP family\");\n",
    };
    const bool rejected[] = { false, true, false, false, true, false, true, false, false, true, true,
                              true, false, true, false, false, false, true, false };
    char directory[] = "/tmp/buster-check-special-graph-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* modes[] = { "-std=gnu17", "-std=gnu23" };
    for (uint32_t mode = 0; ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        const bool checked = parse_test_clang(clang, &modes[mode], 1, pointers, BUSTER_ARRAY_LENGTH(sources), outcomes);
        check_test_expect(test, checked, "independent generic builtin graph oracle runs");
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
        {
            char message[128];
            snprintf(message, sizeof(message), "independent builtin semantic mutation %u agrees in %s", i, modes[mode]);
            check_test_expect(test, outcomes[i].rejected == rejected[i], message);
            if (mode == 0)
            {
                const CheckSourceFixture fixture = { .name = message, .source = sources[i], .rejected = rejected[i] };
                check_test_source(test, &fixture, paths[i], outcomes + i);
            }
        }
    }
    check_test_expect(test, ready, "generic builtin oracle source creation");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_test_special_graph_bounds(CheckTest* test, TypeTable* table, const CheckGraph* graph)
{
    _Alignas(16) uint8_t output_bytes[32768];
    _Alignas(16) uint8_t scratch_bytes[32768];
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const CheckOptions options = { .scalar = path == 0 };
        Arena output = { .base = output_bytes, .length = 7, .capacity = sizeof(output_bytes) - 16 };
        Arena scratch = { .base = scratch_bytes, .length = 13, .capacity = sizeof(scratch_bytes) - 16 };
        CheckStream stream;
        const bool checked = check_graph_run(table, graph, &output, &scratch, &options, &stream);
        const uint64_t required[] = { output.length, 13 + stream.statistics.scratch_bytes };
        check_test_expect(test, checked, "generic compact descriptors fit bounded output fixture");
        for (uint32_t arena = 0; checked && arena < 2; arena += 1)
        {
            for (uint64_t capacity = arena == 0 ? 7 : 13; capacity < required[arena]; capacity += 1)
            {
                memset(output_bytes, 0xa5, sizeof(output_bytes));
                memset(scratch_bytes, 0xa5, sizeof(scratch_bytes));
                output.length = 7;
                scratch.length = 13;
                output.capacity = arena == 0 ? capacity : sizeof(output_bytes) - 16;
                scratch.capacity = arena == 1 ? capacity : sizeof(scratch_bytes) - 16;
                const bool accepted = check_graph_run(table, graph, &output, &scratch, &options, &stream);
                check_test_expect(test, !accepted && stream.status == CHECK_STATUS_RESOURCE && stream.values == NULL &&
                                       stream.misc == NULL && stream.misc_count == 0 && stream.floating == NULL && stream.floating_count == 0 &&
                                       stream.queries == NULL && stream.query_count == 0 && stream.atomics == NULL && stream.atomic_count == 0 &&
                                       stream.vectors == NULL && stream.vector_count == 0 &&
                                       stream.x86 == NULL && stream.x86_count == 0 &&
                                       output.length == 7 && scratch.length == 13 && check_test_graph_canary(output_bytes, 0, 7) &&
                                       check_test_graph_canary(scratch_bytes, 0, 13) &&
                                       check_test_graph_canary(output_bytes, output.capacity, output.capacity + 16) &&
                                       check_test_graph_canary(scratch_bytes, scratch.capacity, scratch.capacity + 16),
                                  "every partial generic descriptor/argument allocation fails atomically without crossing guards");
            }
        }
    }
}

static void check_test_special_graph_guards(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0 &&
                         mprotect(mapping + page * 3, page, PROT_NONE) == 0;
    check_test_expect(test, guarded, "generic builtin argument and witness protected tails allocate");
    const CheckOp ops[] = { CHECK_OP_BUILTIN_MISC, CHECK_OP_BUILTIN_FLOAT, CHECK_OP_BUILTIN_QUERY, CHECK_OP_BUILTIN_ATOMIC, CHECK_OP_BUILTIN_VECTOR };
    const CBuiltinId ids[] = { CBUILTIN_EXPECT, CBUILTIN_FPCLASSIFY, CBUILTIN_CONSTANT_P, CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ELEMENTWISE_ABS };
    const uint32_t arities[] = { 2, 6, 1, 1, 1 };
    for (uint32_t family = 0; guarded && family < BUSTER_ARRAY_LENGTH(ops); family += 1)
    {
        for (uint32_t count = 0; count <= 33; count += 1)
        {
            arena_pop_to(storage, storage_mark);
            CheckGraph graph;
            const bool ready = check_graph_initialize(&graph, storage, 3, 0);
            check_test_expect(test, ready, "generic protected-tail graph initializes");
            if (ready)
            {
                const uint32_t integer = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 0));
                const uint32_t real = check_test_leaf(&graph, 2, (CheckValue){ .type = BUSTER_TYPE_DOUBLE, .constant_kind = CHECK_CONSTANT_FLOATING, .floating = 1 });
                uint32_t* arguments = (uint32_t*)(mapping + page - count * sizeof(*arguments));
                for (uint32_t i = 0; i < count; i += 1)
                {
                    arguments[i] = family == 1 && i == 5 ? real : integer;
                }
                graph.argument_nodes = arguments;
                graph.argument_count = count;
                graph.argument_capacity = count;
                const uint32_t node = check_graph_add(&graph, ops[family], 3, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                graph.argument_counts[node] = count;
                graph.members[node] = ids[family];
                CheckBuiltinCall* witness = (CheckBuiltinCall*)(mapping + page * 3 - sizeof(*witness));
                *witness = (CheckBuiltinCall){ .node = node };
                graph.builtin_calls = witness;
                graph.builtin_count = family != 0 && family != 4;
                graph.builtin_capacity = graph.builtin_count;
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    const CheckOptions options = { .scalar = path == 0 };
                    CheckStream stream;
                    const bool checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
                    check_test_expect(test, checked && stream.report_count == (count != arities[family]) &&
                                           stream.diagnostics[node] == (count == arities[family] ? CHECK_DIAG_NONE : CHECK_DIAG_ARGUMENT_COUNT),
                                      "generic argument counts zero through 33 and final witnesses require no readable padding");
                    arena_pop_to(output, output_mark);
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 4);
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_special_graph_history(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 133603);
    const bool complete = type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
    CheckGraph graph;
    bool ready = complete && check_graph_initialize(&graph, storage, 4, 4);
    uint32_t* history = ready ? BUSTER_ARENA_PUSH_ARRAY(storage, uint32_t, table->tags.count) : NULL;
    ready = ready && history != NULL;
    check_test_expect(test, ready, "generic builtin source history allocation");
    if (ready)
    {
        memset(history, 0, (uint64_t)table->tags.count * sizeof(*history));
        history[table->types.index[type_index(enumeration)]] = 20;
        graph.tag_completions = history;
        graph.tag_completion_count = table->tags.count;
        const uint32_t value = check_test_leaf(&graph, 1, check_test_integer(enumeration, 7));
        const uint32_t zero = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, 0));
        const uint32_t arguments[] = { value, zero };
        const uint32_t early = check_test_special_node(&graph, CHECK_OP_BUILTIN_MISC, CBUILTIN_EXPECT, arguments, 2);
        const uint32_t late = check_test_special_node(&graph, CHECK_OP_BUILTIN_MISC, CBUILTIN_EXPECT, arguments, 2);
        for (uint32_t node = 0; node < graph.count; node += 1)
        {
            graph.source_positions[node] = node == late ? 20 : 19;
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const CheckOptions options = { .scalar = path == 0 };
            CheckStream stream;
            const bool accepted = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, accepted && stream.report_count == 1 && stream.diagnostics[early] == CHECK_DIAG_INCOMPLETE &&
                                   stream.diagnostics[late] == CHECK_DIAG_NONE && stream.misc_count == 2 &&
                                   stream.misc[0].evaluated_operands == 0 && stream.misc[1].constant_value.integer == 7,
                              "generic builtin parameter conversion uses source-time completion before returning a constant or effect mask");
            arena_pop_to(output, output_mark);
        }
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_special_graph(CheckTest* test, TypeTable* table, const char* clang)
{
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    CheckGraph graph = { 0 };
    bool ready = storage.base != NULL && output.base != NULL && scratch.base != NULL && check_graph_initialize(&graph, &storage, 40, 40);
    graph.builtin_calls = ready ? BUSTER_ARENA_PUSH_ARRAY(&storage, CheckBuiltinCall, 4) : NULL;
    graph.builtin_capacity = 4;
    ready = ready && graph.builtin_calls != NULL;
    check_test_expect(test, ready, "generic builtin graph allocation");
    if (ready)
    {
        const uint32_t seven = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 7));
        const uint32_t effect = check_test_leaf(&graph, 2, (CheckValue){
            .type = BUSTER_TYPE_INT,
            .flags = BUSTER_CHECK_SIDE_EFFECT,
            .fault = CHECK_FAULT_DIVIDE_BY_ZERO,
            .fault_node = 1,
        });
        const uint32_t expect_args[] = { seven, effect };
        const uint32_t expect = check_test_special_node(&graph, CHECK_OP_BUILTIN_MISC, CBUILTIN_EXPECT, expect_args, 2);
        const uint32_t stop = check_test_special_node(&graph, CHECK_OP_BUILTIN_MISC, CBUILTIN_TRAP, NULL, 0);
        const uint32_t query = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &effect, 1);
        const uint32_t zero = check_test_leaf(&graph, 6, check_test_integer(BUSTER_TYPE_INT, 0));
        const uint32_t string = check_test_leaf(&graph, 7, (CheckValue){
            .type = type_array(table, BUSTER_TYPE_CHAR, 4),
            .category = CHECK_LVALUE,
            .constant_kind = CHECK_CONSTANT_ADDRESS,
            .flags = BUSTER_CHECK_STATIC_OBJECT,
            .object = 133601,
        });
        const uint32_t size_args[] = { string, zero };
        const uint32_t size = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_OBJECT_SIZE, size_args, 2);
        graph.builtin_calls[graph.builtin_count++] = (CheckBuiltinCall){
            .node = size,
            .object_size_known = { true, true, true, true },
            .object_size_source_constant = { true, true, true, true },
            .object_sizes = { 4, 4, 4, 4 },
        };
        const uint32_t nan = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_NAN, &string, 1);
        graph.builtin_calls[graph.builtin_count++] = (CheckBuiltinCall){
            .node = nan,
            .nan_payload = (const uint8_t*)"0x1",
            .nan_payload_length = 3,
            .nan_payload_known = true,
        };
        const uint32_t real = check_test_leaf(&graph, 10, (CheckValue){ .type = BUSTER_TYPE_DOUBLE, .constant_kind = CHECK_CONSTANT_FLOATING, .floating = 1.0 });
        const uint32_t unknown = check_test_leaf(&graph, 11, (CheckValue){ .type = BUSTER_TYPE_DOUBLE });
        const uint32_t classify_args[] = { effect, effect, seven, effect, effect, real };
        const uint32_t classify = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_FPCLASSIFY, classify_args, 6);
        const uint32_t runtime_args[] = { seven, seven, seven, seven, seven, unknown };
        const uint32_t runtime = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_FPCLASSIFY, runtime_args, 6);
        const uint32_t pointer = check_test_leaf(&graph, 14, (CheckValue){
            .type = type_pointer(table, BUSTER_TYPE_SCHAR | BUSTER_TYPE_VOLATILE),
            .constant_kind = CHECK_CONSTANT_ADDRESS,
            .flags = BUSTER_CHECK_STATIC_OBJECT,
            .object = 133602,
        });
        const uint32_t value = check_test_leaf(&graph, 15, check_test_integer(BUSTER_TYPE_INT, 255));
        const uint32_t order = check_test_leaf(&graph, 16, check_test_integer(BUSTER_TYPE_INT, 5));
        const uint32_t store_args[] = { pointer, value, order };
        const uint32_t store = check_test_special_node(&graph, CHECK_OP_BUILTIN_ATOMIC, CBUILTIN_ATOMIC_STORE_N, store_args, 3);
        graph.builtin_calls[graph.builtin_count++] = (CheckBuiltinCall){ .node = store, .minimum_alignment = 1, .alignment_known = true };
        const uint32_t fence = check_test_special_node(&graph, CHECK_OP_BUILTIN_ATOMIC, CBUILTIN_SYNC_SYNCHRONIZE, NULL, 0);
        const uint32_t deferred = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_OBJECT_SIZE, size_args, 2);
        graph.builtin_calls[graph.builtin_count++] = (CheckBuiltinCall){
            .node = deferred,
            .object_size_known = { true, true, true, true },
            .object_sizes = { 4, 4, 4, 4 },
        };
        const uint32_t deferred_parent = check_graph_add(&graph, CHECK_OP_ADD, 20, seven, deferred, BUSTER_CHECK_NONE);
        const uint32_t rounds = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_FLT_ROUNDS, NULL, 0);
        const uint32_t absolute = check_test_special_node(&graph, CHECK_OP_BUILTIN_FLOAT, CBUILTIN_FABS, &real, 1);
        const uint32_t complex = check_test_leaf(&graph, 23, (CheckValue){
            .type = BUSTER_TYPE_COMPLEX_DOUBLE,
            .constant_kind = CHECK_CONSTANT_COMPLEX,
            .floating = 1,
            .imaginary = 2,
        });
        const uint32_t complex_query = check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &complex, 1);
        const CheckOptions scalar = { .scalar = true };
        CheckStream expected;
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &scalar, &expected);
        check_test_expect(test, checked && expected.report_count == 0 && expected.misc_count == 2 && expected.floating_count == 5 &&
                               expected.query_count == 4 && expected.atomic_count == 2, "all generic builtin families retain exactly one compact descriptor per call");
        if (checked)
        {
            check_test_expect(test, expected.values[expect].type == BUSTER_TYPE_LONG && expected.values[expect].fault == CHECK_FAULT_DIVIDE_BY_ZERO &&
                                   expected.misc[0].evaluated_operands == 3 && expected.misc[0].constant_evaluated_operands == 1 &&
                                   expected.misc[0].constant_value.type == BUSTER_TYPE_LONG && expected.misc[0].constant_value.integer == 7 &&
                                   expected.misc[0].constant_value.fault == CHECK_FAULT_NONE && expected.argument_conversions[0].type == BUSTER_TYPE_LONG &&
                                   expected.argument_conversions[1].type == BUSTER_TYPE_LONG,
                              "EXPECT retains both runtime operands while its separate source constant result visits only argument zero");
            check_test_expect(test, expected.values[stop].type == BUSTER_TYPE_VOID && expected.misc[1].terminates &&
                                   expected.values[query].integer == 0 && expected.values[query].fault == CHECK_FAULT_NONE &&
                                   expected.values[query].flags == BUSTER_CHECK_ICE && expected.queries[0].evaluated_operands == 0 &&
                                   expected.values[size].integer == 4 && expected.queries[1].known,
                              "terminal and unevaluated query contracts remain distinct from ordinary runtime argument evaluation");
            check_test_expect(test, expected.values[deferred].constant_kind == CHECK_CONSTANT_NONE &&
                                   expected.values[deferred].flags == BUSTER_CHECK_ICE_UNEVALUATED &&
                                   expected.queries[2].runtime_value.constant_kind == CHECK_CONSTANT_INTEGER &&
                                   expected.queries[2].runtime_value.integer == 4 && expected.queries[2].runtime_value.flags == 0 &&
                                   expected.values[deferred_parent].constant_kind == CHECK_CONSTANT_NONE &&
                                   expected.values[complex_query].integer == 1 && expected.values[complex_query].flags == BUSTER_CHECK_ICE,
                              "runtime-only object bounds cannot fold enclosing source expressions while actual complex values prove constant_p");
            check_test_expect(test, expected.values[rounds].type == BUSTER_TYPE_INT && expected.values[rounds].constant_kind == CHECK_CONSTANT_NONE &&
                                   (expected.values[rounds].flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                   expected.floating[3].runtime_builtin == CBUILTIN_FLT_ROUNDS && expected.floating[3].evaluated_operands == 0 &&
                                   expected.floating[3].compiler_barrier && expected.floating[3].reads_fp_environment &&
                                   expected.values[absolute].type == BUSTER_TYPE_DOUBLE && expected.values[absolute].floating == 1,
                              "rounding-mode queries retain environment ordering and extended FP catalog entries reach their typed family");
            check_test_expect(test, expected.values[nan].constant_kind == CHECK_CONSTANT_FLOATING && isnan(expected.values[nan].floating) &&
                                   expected.floating[0].folded && !expected.floating[0].reads_memory &&
                                   expected.values[classify].integer == 7 && expected.values[classify].fault == CHECK_FAULT_NONE &&
                                   expected.floating[1].evaluated_operands == 36 && expected.floating[1].definite_operands == 36 &&
                                   expected.floating[1].selected_selector == 2 && expected.floating[1].evaluation == CHECK_BUILTIN_FLOAT_EVALUATION_FOLDED_SELECTOR &&
                                   expected.values[runtime].constant_kind == CHECK_CONSTANT_NONE && expected.floating[2].evaluated_operands == 63 &&
                                   expected.floating[2].definite_operands == 48 && expected.floating[2].evaluation == CHECK_BUILTIN_FLOAT_EVALUATION_FPCLASSIFY_RUNTIME,
                              "NAN witnesses and six-operand fpclassify preserve exact selected/conditional evaluation metadata");
            check_test_expect(test, expected.atomics[0].node == store && expected.atomics[0].store_value.integer == 255 &&
                                   expected.atomics[0].store_value.type == BUSTER_TYPE_SCHAR && expected.atomics[0].evaluated_operands == 7 &&
                                   expected.atomics[0].volatile_access && expected.atomics[0].writes_memory && expected.atomics[0].alignment_known &&
                                   expected.atomics[0].unsupported == 0 && expected.atomics[1].node == fence && expected.atomics[1].evaluated_operands == 0 &&
                                   expected.atomics[1].fence_scope == CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM,
                              "atomic stores retain target bits and proved alignment while a fence has no fabricated memory value");
            const uint64_t mark = output.length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                const CheckOptions options = { .batch_size = batch };
                CheckStream actual;
                const bool accepted = check_graph_run(table, &graph, &output, &scratch, &options, &actual);
                check_test_expect(test, accepted && check_test_stream_equal(&expected, &actual, graph.argument_count) &&
                                       check_builtin_stream_metadata_equal(&expected, &actual, check_test_value_equal),
                                  "generic builtin conversions and compact metadata survive every legal graph partition");
                arena_pop_to(&output, mark);
            }
            graph.builtin_calls[1].node = size;
            CheckStream invalid;
            const bool accepted = check_graph_run(table, &graph, &output, &scratch, &scalar, &invalid);
            check_test_expect(test, !accepted && invalid.status == CHECK_STATUS_INVALID_GRAPH && invalid.misc == NULL && invalid.floating == NULL &&
                                   invalid.queries == NULL && invalid.atomics == NULL && output.length == mark,
                              "duplicate or out-of-order builtin witnesses reject before output publication");
            graph.builtin_calls[1].node = nan;
        }
        check_test_special_graph_bounds(test, table, &graph);
        check_test_special_graph_history(test, table, &storage, &output, &scratch);
        check_test_special_graph_guards(test, table, &storage, &output, &scratch);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    check_test_special_graph_oracles(test, clang);
}

#endif
