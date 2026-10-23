// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Graph contract regressions for #133: partial allocation rollback, protected call-argument tails, independently
// expected type creation/diagnostic order, and single-LHS update conversions. Include after shared fixture helpers.

static bool check_test_graph_cleared(const CheckGraph* graph)
{
    return graph->ops == 0 && graph->tokens == 0 && graph->operand0 == 0 && graph->operand1 == 0 && graph->operand2 == 0 && graph->leaves == 0 && graph->types == 0 && graph->members == 0 && graph->contexts == 0 && graph->count == 0 && graph->capacity == 0 && graph->argument_offsets == 0 && graph->argument_counts == 0 && graph->argument_nodes == 0 && graph->argument_count == 0 && graph->argument_capacity == 0;
}

static bool check_test_graph_failed_stream(const CheckStream* stream)
{
    return stream->values == 0 && stream->conversions == 0 && stream->argument_conversions == 0 && stream->diagnostics == 0 && stream->reports == 0 && stream->count == 0 && stream->report_count == 0 && stream->status == CHECK_STATUS_RESOURCE && stream->failed_node == BUSTER_CHECK_NONE && stream->statistics.nodes == 0 && stream->statistics.levels == 0 && stream->statistics.batches == 0 && stream->statistics.scalar_nodes == 0 && stream->statistics.vector_nodes == 0 && stream->statistics.output_bytes == 0 && stream->statistics.scratch_bytes == 0;
}

static bool check_test_graph_canary(const uint8_t* bytes, uint64_t begin, uint64_t end)
{
    bool preserved = true;
    for (uint64_t i = begin; i < end; i += 1)
    {
        preserved = preserved && bytes[i] == 0xa5;
    }
    return preserved;
}

static void check_test_graph_partial_allocations(CheckTest* test, TypeTable* table, Arena* storage)
{
    const uint64_t storage_mark = storage->length;
    _Alignas(16) uint8_t graph_bytes[4096];
    _Alignas(16) uint8_t output_bytes[4096];
    _Alignas(16) uint8_t scratch_bytes[4096];
    Arena graph_arena = {
        .base = graph_bytes,
        .length = 7,
        .capacity = sizeof(graph_bytes) - 16,
    };
    CheckGraph graph;
    const bool initialized = check_graph_initialize(&graph, &graph_arena, 6, 2);
    check_test_expect(test, initialized, "partial-allocation fixture fits bounded storage");
    const uint64_t graph_required = graph_arena.length;
    for (uint64_t limit = 7; initialized && limit < graph_required; limit += 1)
    {
        memset(graph_bytes, 0xa5, sizeof(graph_bytes));
        graph_arena.length = 7;
        graph_arena.capacity = limit;
        memset(&graph, 0xa5, sizeof(graph));
        const bool valid = check_graph_initialize(&graph, &graph_arena, 6, 2);
        check_test_expect(test, !valid && check_test_graph_cleared(&graph) && graph_arena.length == 7 && check_test_graph_canary(graph_bytes, 0, 7) && check_test_graph_canary(graph_bytes, limit, limit + 16), "partial graph initialization did not roll back exactly");
    }
    const bool ready = check_graph_initialize(&graph, storage, 6, 2);
    check_test_expect(test, ready, "partial-output fixture graph allocation");
    if (ready)
    {
        const TypeId parameters[] = {
            BUSTER_TYPE_INT,
            BUSTER_TYPE_LONG,
        };
        const TypeId function = type_function(table, BUSTER_TYPE_INT, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE);
        const uint32_t callee = check_test_leaf(&graph, 10, (CheckValue){
            .type = function,
            .category = CHECK_FUNCTION,
        });
        const uint32_t first = check_test_leaf(&graph, 11, check_test_integer(BUSTER_TYPE_SHORT, 7));
        const uint32_t second = check_test_leaf(&graph, 12, check_test_integer(BUSTER_TYPE_INT, 9));
        const uint32_t call = check_graph_add(&graph, CHECK_OP_CALL, 13, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.argument_count = 2;
        graph.argument_counts[call] = 2;
        graph.argument_nodes[0] = first;
        graph.argument_nodes[1] = second;
        const uint32_t one = check_test_leaf(&graph, 14, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t sum = check_graph_add(&graph, CHECK_OP_ADD, 15, first, one, BUSTER_CHECK_NONE);
        graph.contexts[sum] = BUSTER_CHECK_CONTEXT_ASSERT;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const CheckOptions options = {
                .scalar = path == 0,
            };
            Arena output = {
                .base = output_bytes,
                .length = 17,
                .capacity = sizeof(output_bytes) - 16,
            };
            Arena scratch = {
                .base = scratch_bytes,
                .length = 13,
                .capacity = sizeof(scratch_bytes) - 16,
            };
            memset(output_bytes, 0xa5, sizeof(output_bytes));
            memset(scratch_bytes, 0xa5, sizeof(scratch_bytes));
            CheckStream stream;
            const bool complete = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
            check_test_expect(test, complete && stream.report_count == 0 && stream.values[sum].integer == 8 && stream.argument_conversions[0].type == BUSTER_TYPE_INT && stream.argument_conversions[1].type == BUSTER_TYPE_LONG, "partial-allocation fixture has independently expected results");
            const uint64_t output_required = output.length;
            const uint64_t scratch_required = 13 + stream.statistics.scratch_bytes;
            const uint32_t type_count = table->types.count;
            for (uint32_t arena = 0; complete && arena < 2; arena += 1)
            {
                const uint64_t start = arena == 0 ? 17 : 13;
                const uint64_t end = arena == 0 ? output_required : scratch_required;
                for (uint64_t limit = start; limit < end; limit += 1)
                {
                    memset(output_bytes, 0xa5, sizeof(output_bytes));
                    memset(scratch_bytes, 0xa5, sizeof(scratch_bytes));
                    output.length = 17;
                    scratch.length = 13;
                    output.capacity = arena == 0 ? limit : sizeof(output_bytes) - 16;
                    scratch.capacity = arena == 1 ? limit : sizeof(scratch_bytes) - 16;
                    memset(&stream, 0xa5, sizeof(stream));
                    const bool valid = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
                    const bool prefixes = check_test_graph_canary(output_bytes, 0, 17) && check_test_graph_canary(scratch_bytes, 0, 13);
                    const bool tails = check_test_graph_canary(output_bytes, output.capacity, output.capacity + 16) && check_test_graph_canary(scratch_bytes, scratch.capacity, scratch.capacity + 16);
                    check_test_expect(test, !valid && check_test_graph_failed_stream(&stream) && output.length == 17 && scratch.length == 13 && table->types.count == type_count && prefixes && tails, "partial checker allocation escaped capacity or retained output");
                }
            }
        }
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_graph_argument_guards(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = mapping != MAP_FAILED;
    if (ready)
    {
        ready = mprotect(mapping + page, page, PROT_NONE) == 0;
    }
    CheckGraph graph;
    ready = check_graph_initialize(&graph, storage, 35, 0) && ready;
    check_test_expect(test, ready, "guarded call argument storage");
    const TypeId function = type_function(table, BUSTER_TYPE_INT, 0, 0, 0);
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        graph.count = 0;
        graph.argument_count = count;
        graph.argument_capacity = count;
        graph.argument_nodes = (uint32_t*)(mapping + page - count * sizeof(uint32_t));
        const uint32_t callee = check_test_leaf(&graph, 0, (CheckValue){
            .type = function,
            .category = CHECK_FUNCTION,
        });
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t argument = check_test_leaf(&graph, i + 1, check_test_integer(BUSTER_TYPE_UCHAR, i));
            graph.argument_nodes[i] = argument;
        }
        const uint32_t call = check_graph_add(&graph, CHECK_OP_CALL, count + 1, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.argument_counts[call] = count;
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        const CheckOptions options = { 0 };
        CheckStream stream;
        const bool valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, valid && stream.report_count == 0 && stream.values[call].type == BUSTER_TYPE_INT, "guarded old-style call checks its exact argument tail");
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            check_test_expect(test, stream.argument_conversions[i].type == BUSTER_TYPE_INT && (stream.argument_conversions[i].flags & BUSTER_CHECK_CONVERT_PROMOTION) != 0, "old-style call promotes each unsigned-char argument to int");
        }
        arena_pop_to(output, output_mark);
        if (count != 0)
        {
            graph.argument_nodes[count - 1] = call;
            const bool invalid = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, !invalid && stream.status == CHECK_STATUS_INVALID_GRAPH && stream.failed_node == call && output->length == output_mark, "invalid last argument at a protected edge is rejected atomically");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, storage_mark);
}

static void check_test_graph_barriers(CheckTest* test, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    CheckTestTypes tables[2] = { 0 };
    const bool opened = check_test_types_open(&tables[0]) && check_test_types_open(&tables[1]);
    check_test_expect(test, opened, "fresh type tables for graph mutation barriers");
    if (opened)
    {
        TypeId function = 0;
        TypeId array = 0;
        TypeId const_pointer = 0;
        TypeId volatile_pointer = 0;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            TypeTable* table = &tables[path].table;
            function = type_function(table, BUSTER_TYPE_INT, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
            array = type_array(table, BUSTER_TYPE_SHORT, 3);
            const_pointer = type_pointer(table, BUSTER_TYPE_LONG | BUSTER_TYPE_CONST);
            volatile_pointer = type_pointer(table, BUSTER_TYPE_LONG | BUSTER_TYPE_VOLATILE);
        }
        const uint32_t first_derived = tables[0].table.types.count;
        CheckGraph graph;
        const bool ready = check_graph_initialize(&graph, storage, 16, 0);
        check_test_expect(test, ready, "graph barrier fixture allocation");
        if (ready)
        {
            const uint32_t object = check_test_leaf(&graph, 0, (CheckValue){
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
            });
            const uint32_t one = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 1));
            const uint32_t positive = check_graph_add(&graph, CHECK_OP_POSITIVE, 2, one, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            const uint32_t address = check_graph_add(&graph, CHECK_OP_ADDRESS, 3, object, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            const uint32_t array_value = check_test_leaf(&graph, 4, (CheckValue){
                .type = array,
                .category = CHECK_LVALUE,
            });
            const uint32_t decay = check_graph_add(&graph, CHECK_OP_ADD, 5, array_value, positive, BUSTER_CHECK_NONE);
            const uint32_t callee = check_test_leaf(&graph, 6, (CheckValue){
                .type = function,
                .category = CHECK_FUNCTION,
            });
            const uint32_t call = check_graph_add(&graph, CHECK_OP_CALL, 7, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            const uint32_t left = check_test_leaf(&graph, 8, (CheckValue){
                .type = const_pointer,
            });
            const uint32_t right = check_test_leaf(&graph, 9, (CheckValue){
                .type = volatile_pointer,
            });
            const uint32_t conditional = check_graph_add(&graph, CHECK_OP_CONDITIONAL, 10, one, left, right);
            const uint32_t query = check_graph_add(&graph, CHECK_OP_SIZEOF, 11, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph.types[query] = array;
            const uint32_t sum = check_graph_add(&graph, CHECK_OP_ADD, 12, query, one, BUSTER_CHECK_NONE);
            CheckStream streams[2];
            const CheckOptions options[2] = {
                {
                    .scalar = true,
                },
                {
                    .batch_size = 3,
                },
            };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                TypeTable* table = &tables[path].table;
                CheckStream* stream = &streams[path];
                const bool valid = check_graph_run(table, &graph, output, scratch, &options[path], stream);
                check_test_expect(test, valid && stream->report_count == 0, "graph with address, decay, call and composite barriers checks successfully");
                if (valid)
                {
                    const TypeId int_pointer = (TypeId)first_derived << BUSTER_TYPE_INDEX_SHIFT;
                    const TypeId short_pointer = (TypeId)(first_derived + 1) << BUSTER_TYPE_INDEX_SHIFT;
                    const TypeId function_pointer = (TypeId)(first_derived + 2) << BUSTER_TYPE_INDEX_SHIFT;
                    const TypeId qualified_pointer = (TypeId)(first_derived + 3) << BUSTER_TYPE_INDEX_SHIFT;
                    check_test_expect(test, stream->values[address].type == int_pointer && type_element(table, int_pointer) == BUSTER_TYPE_INT, "address creates the first source-order derived type");
                    check_test_expect(test, stream->values[decay].type == short_pointer && stream->conversions[decay].operands[0].type == short_pointer && type_element(table, short_pointer) == BUSTER_TYPE_SHORT, "array decay creates the next source-order derived type");
                    check_test_expect(test, stream->values[call].type == BUSTER_TYPE_INT && stream->conversions[call].operands[0].type == function_pointer && type_element(table, function_pointer) == function, "callee decay retains source-order type identity");
                    check_test_expect(test, stream->values[conditional].type == qualified_pointer && type_element(table, qualified_pointer) == (BUSTER_TYPE_LONG | BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE), "conditional pointer composite follows earlier type creation");
                    check_test_expect(test, stream->values[query].integer == 6 && stream->values[sum].integer == 7 && table->types.count == first_derived + 4, "builtin region after barriers reads completed dependencies without new types");
                }
            }
            check_test_expect(test, check_test_stream_equal(&streams[0], &streams[1], 0), "fresh-table graph barriers preserve every typed result and conversion");
        }
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, storage_mark);
    check_test_types_close(&tables[1]);
    check_test_types_close(&tables[0]);
}

static void check_test_graph_report_order(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, storage, 14, 0);
    check_test_expect(test, ready, "diagnostic-order graph allocation");
    if (ready)
    {
        const uint32_t floating = check_test_leaf(&graph, 90, check_test_float(BUSTER_TYPE_FLOAT, 1));
        const uint32_t one = check_test_leaf(&graph, 91, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t zero = check_test_leaf(&graph, 92, check_test_integer(BUSTER_TYPE_INT, 0));
        const uint32_t invalid = check_graph_add(&graph, CHECK_OP_REMAINDER, UINT32_C(0x10000), floating, one, BUSTER_CHECK_NONE);
        check_graph_add(&graph, CHECK_OP_POSITIVE, 0, invalid, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        check_graph_add(&graph, CHECK_OP_LOGICAL_AND, 1, zero, invalid, BUSTER_CHECK_NONE);
        const uint32_t divide = check_graph_add(&graph, CHECK_OP_DIVIDE, 255, one, zero, BUSTER_CHECK_NONE);
        const uint32_t skipped = check_graph_add(&graph, CHECK_OP_LOGICAL_AND, 2, zero, divide, BUSTER_CHECK_NONE);
        graph.contexts[skipped] = BUSTER_CHECK_CONTEXT_ICE;
        const uint32_t query = check_graph_add(&graph, CHECK_OP_SIZEOF, 3, divide, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.contexts[query] = BUSTER_CHECK_CONTEXT_ICE;
        const uint32_t selected = check_graph_add(&graph, CHECK_OP_LOGICAL_OR, 4, zero, divide, BUSTER_CHECK_NONE);
        graph.contexts[selected] = BUSTER_CHECK_CONTEXT_ICE;
        const uint32_t tokens[] = {
            UINT32_MAX,
            256,
            UINT32_MAX,
            256,
        };
        uint32_t assertions[BUSTER_ARRAY_LENGTH(tokens)];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(tokens); i += 1)
        {
            assertions[i] = check_test_leaf(&graph, tokens[i], check_test_integer(BUSTER_TYPE_INT, 0));
            graph.contexts[assertions[i]] = BUSTER_CHECK_CONTEXT_ASSERT;
        }
        const uint32_t expected[] = {
            divide,
            assertions[1],
            assertions[3],
            invalid,
            assertions[0],
            assertions[2],
        };
        for (uint32_t batch = 0; batch <= BUSTER_CHECK_BATCH; batch += 1)
        {
            const CheckOptions options = {
                .scalar = batch == 0,
                .batch_size = batch,
            };
            CheckStream stream;
            const bool valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, valid && stream.report_count == BUSTER_ARRAY_LENGTH(expected), "graph reports primary constraints and selected faults exactly once");
            if (valid && stream.report_count == BUSTER_ARRAY_LENGTH(expected))
            {
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(expected); i += 1)
                {
                    check_test_expect(test, stream.reports[i].node == expected[i] && stream.reports[i].token == graph.tokens[expected[i]], "source diagnostic order is stable across all token bytes and equal tokens");
                }
                check_test_expect(test, stream.reports[0].message == CHECK_MESSAGE_CONSTANT && stream.reports[0].fault == CHECK_FAULT_DIVIDE_BY_ZERO && stream.reports[3].message == CHECK_MESSAGE_ACTION && stream.reports[3].diagnostic == CHECK_DIAG_INTEGER_REQUIRED, "evaluation suppression preserves independent type constraints and fault origins");
            }
            arena_pop_to(output, output_mark);
        }
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_graph_updates(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const CheckOp operations[] = {
        CHECK_OP_PRE_INCREMENT,
        CHECK_OP_PRE_DECREMENT,
        CHECK_OP_POST_INCREMENT,
        CHECK_OP_POST_DECREMENT,
        CHECK_OP_ADD_ASSIGN,
        CHECK_OP_SUBTRACT_ASSIGN,
        CHECK_OP_MULTIPLY_ASSIGN,
        CHECK_OP_DIVIDE_ASSIGN,
        CHECK_OP_REMAINDER_ASSIGN,
        CHECK_OP_SHIFT_LEFT_ASSIGN,
        CHECK_OP_SHIFT_RIGHT_ASSIGN,
        CHECK_OP_BIT_AND_ASSIGN,
        CHECK_OP_BIT_OR_ASSIGN,
        CHECK_OP_BIT_XOR_ASSIGN,
    };
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, storage, 24, 0);
    check_test_expect(test, ready, "update graph allocation");
    if (ready)
    {
        const uint32_t left = check_test_leaf(&graph, 0, (CheckValue){
            .type = BUSTER_TYPE_UCHAR | BUSTER_TYPE_VOLATILE,
            .category = CHECK_LVALUE,
        });
        const uint32_t right = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 2));
        uint32_t nodes[BUSTER_ARRAY_LENGTH(operations)];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
        {
            const uint32_t rhs = i < 4 ? BUSTER_CHECK_NONE : right;
            nodes[i] = check_graph_add(&graph, operations[i], i + 2, left, rhs, BUSTER_CHECK_NONE);
        }
        const uint32_t floating = check_test_leaf(&graph, 16, check_test_float(BUSTER_TYPE_DOUBLE, 0.5L));
        const uint32_t mixed = check_graph_add(&graph, CHECK_OP_ADD_ASSIGN, 17, left, floating, BUSTER_CHECK_NONE);
        const TypeId pointer = type_pointer(table, BUSTER_TYPE_LONG);
        const uint32_t pointer_left = check_test_leaf(&graph, 18, (CheckValue){
            .type = pointer,
            .category = CHECK_LVALUE,
        });
        const uint32_t pointer_update = check_graph_add(&graph, CHECK_OP_POST_INCREMENT, 19, pointer_left, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t query = check_graph_add(&graph, CHECK_OP_SIZEOF, 20, pointer_update, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.contexts[query] = BUSTER_CHECK_CONTEXT_ICE;
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        const CheckOptions options = { 0 };
        CheckStream stream;
        const bool valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, valid && stream.report_count == 0, "all update arities check successfully");
        for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(nodes); i += 1)
        {
            const uint32_t node = nodes[i];
            const CheckValue* value = &stream.values[node];
            const CheckNodeConversions* conversions = &stream.conversions[node];
            check_test_expect(test, value->type == BUSTER_TYPE_UCHAR && value->category == CHECK_RVALUE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && value->constant_kind == CHECK_CONSTANT_NONE && (value->flags & BUSTER_CHECK_ICE) == 0, "update expression is an unqualified nonconstant value with a side effect");
            check_test_expect(test, conversions->operands[0].type == BUSTER_TYPE_INT && (conversions->operands[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0 && conversions->store.type == BUSTER_TYPE_UCHAR && (conversions->store.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0, "update retains distinct promoted LHS read and narrow store conversions");
        }
        if (valid)
        {
            check_test_expect(test, stream.values[mixed].type == BUSTER_TYPE_UCHAR && stream.conversions[mixed].operands[0].type == BUSTER_TYPE_DOUBLE && stream.conversions[mixed].operands[1].type == BUSTER_TYPE_DOUBLE && stream.conversions[mixed].store.type == BUSTER_TYPE_UCHAR, "mixed compound assignment retains floating arithmetic and integer storage types");
            check_test_expect(test, stream.values[pointer_update].type == pointer && stream.conversions[pointer_update].store.type == pointer && stream.values[query].integer == 8 && (stream.values[query].flags & BUSTER_CHECK_SIDE_EFFECT) == 0, "pointer update stays typed across its barrier and unevaluated sizeof suppresses its effect");
            check_test_expect(test, stream.conversions[left].store.type == 0 && stream.conversions[right].store.type == 0 && stream.conversions[query].store.type == 0, "ordinary nodes do not retain stale store conversions");
        }
        arena_pop_to(output, output_mark);
        graph.operand1[nodes[0]] = right;
        bool accepted = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !accepted && stream.status == CHECK_STATUS_INVALID_GRAPH && stream.failed_node == nodes[0], "unary update rejects an extra RHS edge");
        graph.operand1[nodes[0]] = BUSTER_CHECK_NONE;
        graph.operand1[nodes[4]] = BUSTER_CHECK_NONE;
        accepted = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !accepted && stream.status == CHECK_STATUS_INVALID_GRAPH && stream.failed_node == nodes[4], "compound assignment rejects a missing RHS edge");
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, storage_mark);
}

static void check_test_graph_regressions(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    check_test_graph_partial_allocations(test, table, storage);
    check_test_graph_argument_guards(test, table, storage, output, scratch);
    check_test_graph_barriers(test, storage, output, scratch);
    check_test_graph_report_order(test, table, storage, output, scratch);
    check_test_graph_updates(test, table, storage, output, scratch);
}
