// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Compact varargs graph context/results: source identity, ignored va_start operand, requested types, warnings,
// malformed metadata and allocation rollback. Include after the shared checker/graph-test helpers.
#ifndef BUSTER_C_CHECK_VARARGS_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_VARARGS_GRAPH_TEST_INCLUDED

static TypeId check_test_graph_va_list(TypeTable* table)
{
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 133401);
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeMember members[] = {
        { .name = 133402, .type = BUSTER_TYPE_UINT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 133403, .type = BUSTER_TYPE_UINT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 133404, .type = pointer, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 133405, .type = pointer, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
    };
    const bool complete = record != 0 && pointer != 0 && type_complete_record(table, record, members, BUSTER_ARRAY_LENGTH(members), (TypeRecordAttributes){ 0 });
    return complete ? type_array(table, record, 1) : 0;
}

static uint32_t check_test_varargs_node(CheckGraph* graph, CBuiltinId id, uint32_t count, uint32_t a, uint32_t b, TypeId type, CheckVarargsContext context)
{
    const uint32_t node = check_graph_add(graph, CHECK_OP_VARARGS, graph->count + 10, a, b, BUSTER_CHECK_NONE);
    if (node != BUSTER_CHECK_NONE && graph->varargs_count < graph->varargs_capacity)
    {
        graph->members[node] = id;
        graph->types[node] = type;
        graph->argument_offsets[node] = graph->varargs_count;
        graph->argument_counts[node] = count;
        graph->varargs_calls[graph->varargs_count++] = (CheckVarargsCall){
            .node = node,
            .second_argument_declaration = context.last_parameter,
            .context = context,
        };
    }
    return node;
}

static void check_test_varargs_graph_guards(CheckTest* test, TypeTable* table, TypeId list, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CheckGraph graph;
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0 &&
                       check_graph_initialize(&graph, storage, 34, 0);
    check_test_expect(test, ready, "varargs compact contexts end at a protected page");
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        graph.count = 0;
        graph.varargs_count = 0;
        graph.capacity = count == 0 ? 0 : count + 1;
        graph.varargs_capacity = count;
        graph.varargs_calls = (CheckVarargsCall*)(mapping + page - count * sizeof(CheckVarargsCall));
        const CheckVarargsContext context = { .va_list_type = list, .standard = 2017 };
        const uint32_t ap = count != 0 ? check_test_leaf(&graph, 1, (CheckValue){ .type = list, .category = CHECK_LVALUE }) : BUSTER_CHECK_NONE;
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_test_varargs_node(&graph, CBUILTIN_VA_END, 1, ap, BUSTER_CHECK_NONE, 0, context);
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const CheckOptions options = { .scalar = path == 0 };
            CheckStream stream;
            const bool accepted = check_graph_run(table, &graph, output, scratch, &options, &stream);
            bool exact = accepted && stream.report_count == 0 && stream.varargs_count == count && (count != 0 || stream.varargs == NULL);
            for (uint32_t i = 0; exact && i < count; i += 1)
            {
                exact = stream.varargs[i].node == i + 1 && stream.varargs[i].evaluated_operands == 1 && !stream.varargs[i].mutates_list;
            }
            check_test_expect(test, exact, "zero/full/tail varargs contexts read no padding and retain exact descriptor order");
            arena_pop_to(output, output_mark);
        }
        if (count != 0)
        {
            const uint32_t last = graph.count - 1;
            const uint32_t invalid_counts[] = { 0, 2, UINT32_MAX };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_counts); i += 1)
            {
                graph.argument_counts[last] = invalid_counts[i];
                graph.operand0[last] = invalid_counts[i] == 0 ? BUSTER_CHECK_NONE : ap;
                graph.operand1[last] = invalid_counts[i] < 2 ? BUSTER_CHECK_NONE : ap;
                const CheckOptions options = { 0 };
                CheckStream stream;
                const bool accepted = check_graph_run(table, &graph, output, scratch, &options, &stream);
                check_test_expect(test, accepted && stream.report_count == 1 && stream.diagnostics[last] == CHECK_DIAG_ARGUMENT_COUNT &&
                                       stream.varargs_count == count && stream.varargs[count - 1].evaluated_operands == 0 &&
                                       !stream.varargs[count - 1].mutates_list,
                                  "raw varargs count errors reject before reading beyond the two available operands");
                arena_pop_to(output, output_mark);
            }
            graph.argument_counts[last] = 1;
            graph.operand0[last] = ap;
            graph.operand1[last] = BUSTER_CHECK_NONE;
            graph.members[last] = UINT32_MAX;
            const CheckOptions options = { 0 };
            CheckStream stream;
            const bool accepted = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, accepted && stream.report_count == 1 && stream.diagnostics[last] == CHECK_DIAG_UNSUPPORTED &&
                                   stream.varargs[count - 1].evaluated_operands == 0,
                              "invalid sparse builtin ID at the protected context tail fails without fabricated effects");
            arena_pop_to(output, output_mark);
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_varargs_graph(CheckTest* test, TypeTable* table)
{
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    CheckGraph graph = { 0 };
    bool ready = storage.base != NULL && output.base != NULL && scratch.base != NULL && check_graph_initialize(&graph, &storage, 8, 0);
    const TypeId list = ready ? check_test_graph_va_list(table) : 0;
    const TypeId parameter = BUSTER_TYPE_INT;
    const TypeId function = ready ? type_function(table, BUSTER_TYPE_VOID, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC) : 0;
    graph.varargs_calls = ready ? BUSTER_ARENA_PUSH_ARRAY(&storage, CheckVarargsCall, 5) : NULL;
    graph.varargs_capacity = 5;
    ready = ready && list != 0 && function != 0 && graph.varargs_calls != NULL;
    check_test_expect(test, ready, "varargs graph metadata/type allocation");
    if (ready)
    {
        const CheckVarargsContext context = {
            .va_list_type = list,
            .function_type = function,
            .last_parameter = 17,
            .last_parameter_type = BUSTER_TYPE_INT,
            .standard = 2017,
            .variadic = true,
        };
        const uint32_t ap = check_test_leaf(&graph, 10, (CheckValue){ .type = list, .category = CHECK_LVALUE, .object = 221 });
        const uint32_t second = check_test_leaf(&graph, 11, (CheckValue){
            .type = BUSTER_TYPE_INT,
            .category = CHECK_LVALUE,
            .flags = BUSTER_CHECK_SIDE_EFFECT,
            .fault = CHECK_FAULT_DIVIDE_BY_ZERO,
            .fault_node = 1,
        });
        const uint32_t start = check_test_varargs_node(&graph, CBUILTIN_VA_START, 2, ap, second, 0, context);
        const uint32_t wide = check_test_varargs_node(&graph, CBUILTIN_VA_ARG, 2, ap, BUSTER_CHECK_NONE, BUSTER_TYPE_INT128, context);
        const uint32_t narrow = check_test_varargs_node(&graph, CBUILTIN_VA_ARG, 2, ap, BUSTER_CHECK_NONE, BUSTER_TYPE_UCHAR, context);
        const uint32_t copy = check_test_varargs_node(&graph, CBUILTIN_VA_COPY, 2, ap, ap, 0, context);
        const uint32_t end = check_test_varargs_node(&graph, CBUILTIN_VA_END, 1, ap, BUSTER_CHECK_NONE, 0, context);
        graph.varargs_calls[0].second_argument_declaration = 18;
        CheckStream expected;
        const CheckOptions scalar = { .scalar = true };
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &scalar, &expected);
        check_test_expect(test, checked && expected.report_count == 0 && expected.varargs_count == 5 && expected.effect_count == 0,
                          "varargs calls retain one compact descriptor each without overflow store records");
        if (checked)
        {
            check_test_expect(test, expected.values[start].type == BUSTER_TYPE_VOID && expected.values[start].fault == CHECK_FAULT_NONE &&
                                   expected.conversions[start].operands[1].type == 0 && expected.varargs[0].node == start &&
                                   expected.varargs[0].evaluated_operands == 1 && expected.varargs[0].mutates_list &&
                                   expected.varargs[0].warnings == BUSTER_CHECK_VARARGS_WARN_LAST_PARAMETER,
                              "va_start typechecks its source context but does not evaluate the second operand's fault/effect");
            check_test_expect(test, expected.values[wide].type == BUSTER_TYPE_INT128 && expected.varargs[1].requested_type == BUSTER_TYPE_INT128 &&
                                   expected.values[narrow].type == BUSTER_TYPE_UCHAR &&
                                   expected.varargs[2].warnings == BUSTER_CHECK_VARARGS_WARN_ARGUMENT_PROMOTION,
                              "semantic va_arg accepts wide types independently of backend class and retains promotion warning");
            check_test_expect(test, expected.varargs[3].node == copy && expected.varargs[3].evaluated_operands == 3 && expected.varargs[3].mutates_list &&
                                   expected.varargs[4].node == end && expected.varargs[4].evaluated_operands == 1 && !expected.varargs[4].mutates_list,
                              "va_copy and va_end retain distinct evaluation/lifetime contracts");
            const uint64_t mark = output.length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                const CheckOptions options = { .batch_size = batch };
                CheckStream actual;
                const bool accepted = check_graph_run(table, &graph, &output, &scratch, &options, &actual);
                check_test_expect(test, accepted && check_test_stream_equal(&expected, &actual, 0),
                                  "varargs graph results and metadata agree across every legal partition");
                arena_pop_to(&output, mark);
            }
            const uint32_t saved = graph.varargs_calls[4].node;
            graph.varargs_calls[4].node = start;
            CheckStream invalid;
            const bool accepted = check_graph_run(table, &graph, &output, &scratch, &scalar, &invalid);
            check_test_expect(test, !accepted && invalid.status == CHECK_STATUS_INVALID_GRAPH && invalid.failed_node == end &&
                                   invalid.varargs == NULL && invalid.varargs_count == 0 && output.length == mark,
                              "mismatched compact varargs identity rejects atomically before evaluation");
            graph.varargs_calls[4].node = saved;
        }
        arena_pop_to(&output, 0);
        _Alignas(16) uint8_t bytes[8192];
        Arena bounded = { .base = bytes, .length = 7, .capacity = sizeof(bytes) - 16 };
        CheckStream baseline;
        const bool complete = check_graph_run(table, &graph, &bounded, &scratch, &scalar, &baseline);
        const uint64_t required = bounded.length;
        check_test_expect(test, complete, "varargs bounded-output fixture fits");
        for (uint64_t limit = 7; complete && limit < required; limit += 1)
        {
            memset(bytes, 0xa5, sizeof(bytes));
            bounded.length = 7;
            bounded.capacity = limit;
            CheckStream failed;
            const bool accepted = check_graph_run(table, &graph, &bounded, &scratch, &scalar, &failed);
            check_test_expect(test, !accepted && failed.status == CHECK_STATUS_RESOURCE && failed.varargs == NULL && failed.varargs_count == 0 &&
                                   failed.values == NULL && failed.warnings == NULL && bounded.length == 7 && scratch.length == 0 &&
                                   check_test_graph_canary(bytes, 0, 7) && check_test_graph_canary(bytes, limit, limit + 16),
                              "every partial varargs descriptor allocation rolls back without crossing output guards");
        }
        check_test_varargs_graph_guards(test, table, list, &storage, &output, &scratch);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

#endif
