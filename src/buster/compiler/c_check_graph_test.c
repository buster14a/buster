// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Graph contract regressions for #133: partial allocation rollback, protected call-argument tails, independently
// expected type creation/diagnostic order, and single-LHS update conversions; and #530's vector validator against its exact
// twin, on random and broken graphs and at page edges. Include after shared fixture helpers.

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

// The validators' generator (#530): a random graph of `count` nodes over every operator class check_graph_validate_vector
// classifies, valid by construction: leaves of builtin, pointer and invalid types, edges within each operator's arity, casts
// to builtin, void and pointer types, sizeof of a type and of an operand, calls' argument slices, and the byte columns'
// bits. With `builtins`, also builtins' slices, statement values and variable-length arrays' bounds, which check_graph_run
// needs their metadata for; with `sideband`, the nodes only the exact path checks: VARARGS, x86 builtins and builtin,
// vector-member and overload entries. The graph has room for count nodes and 4 * count arguments.
static void check_test_graph_validation_world(CheckTest* test, CheckGraph* graph, uint32_t count, TypeId pointer, bool builtins, bool sideband)
{
    static const CheckOp unary[] =
    {
        CHECK_OP_POSITIVE,
        CHECK_OP_NEGATIVE,
        CHECK_OP_NOT,
        CHECK_OP_BIT_NOT,
        CHECK_OP_ADDRESS,
        CHECK_OP_DEREFERENCE,
        CHECK_OP_CONDITION,
        CHECK_OP_PRE_INCREMENT,
        CHECK_OP_POST_DECREMENT,
        CHECK_OP_SELECT,
        CHECK_OP_MEMBER,
        CHECK_OP_REAL,
    };
    static const CheckOp binary[] =
    {
        CHECK_OP_ADD,
        CHECK_OP_SUBTRACT,
        CHECK_OP_MULTIPLY,
        CHECK_OP_SHIFT_LEFT,
        CHECK_OP_LESS,
        CHECK_OP_EQUAL,
        CHECK_OP_BIT_AND,
        CHECK_OP_LOGICAL_OR,
        CHECK_OP_ASSIGN,
        CHECK_OP_COMMA,
        CHECK_OP_SUBSCRIPT,
        CHECK_OP_ADD_ASSIGN,
        CHECK_OP_BIT_XOR_ASSIGN,
    };
    const TypeId requested[] =
    {
        BUSTER_TYPE_INT,
        BUSTER_TYPE_DOUBLE | BUSTER_TYPE_CONST,
        BUSTER_TYPE_VOID,
        pointer,
        BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT128),
    };
    graph->count = 0;
    graph->argument_count = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t draw = check_test_random(test) % 20;
        const uint32_t a = i != 0 ? check_test_random(test) % i : BUSTER_CHECK_NONE;
        const uint32_t b = i != 0 ? check_test_random(test) % i : BUSTER_CHECK_NONE;
        const uint32_t c = i != 0 ? check_test_random(test) % i : BUSTER_CHECK_NONE;
        const TypeId type = requested[check_test_random(test) % BUSTER_ARRAY_LENGTH(requested)];
        uint32_t node = BUSTER_CHECK_NONE;
        if (i < 2 || draw < 6)
        {
            const TypeId leaves[] =
            {
                BUSTER_TYPE_INT,
                BUSTER_TYPE_ULONG | BUSTER_TYPE_VOLATILE,
                BUSTER_TYPE_LONG_DOUBLE,
                pointer,
                0,
            };
            CheckValue value = check_test_integer(leaves[check_test_random(test) % BUSTER_ARRAY_LENGTH(leaves)], i);
            value.category = (check_test_random(test) & 1) != 0 ? CHECK_LVALUE : CHECK_RVALUE;
            node = check_test_leaf(graph, i, value);
        }
        else if (draw < 9)
        {
            node = check_graph_add(graph, unary[check_test_random(test) % BUSTER_ARRAY_LENGTH(unary)], i, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        else if (draw < 12)
        {
            node = check_graph_add(graph, binary[check_test_random(test) % BUSTER_ARRAY_LENGTH(binary)], i, a, b, BUSTER_CHECK_NONE);
        }
        else if (draw == 12)
        {
            node = check_graph_add(graph, builtins && (check_test_random(test) & 1) != 0 ? CHECK_OP_BUILTIN_OVERFLOW : CHECK_OP_CONDITIONAL, i, a, b, c);
        }
        else if (draw == 13)
        {
            const CheckOp casts[] =
            {
                CHECK_OP_CAST,
                CHECK_OP_INITIALIZE,
                CHECK_OP_CONVERT,
            };
            node = check_graph_add(graph, casts[check_test_random(test) % BUSTER_ARRAY_LENGTH(casts)], i, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->types[node] = type;
        }
        else if (draw == 14)
        {
            const bool named = (check_test_random(test) & 1) != 0;
            node = check_graph_add(graph, (check_test_random(test) & 1) != 0 ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF, i, named ? BUSTER_CHECK_NONE : a, BUSTER_CHECK_NONE,
                                   BUSTER_CHECK_NONE);
            graph->types[node] = named ? type : 0;
        }
        else if (draw == 15 || draw == 16)
        {
            const CheckOp sliced[] =
            {
                CHECK_OP_CALL,
                CHECK_OP_BUILTIN_MISC,
                CHECK_OP_BUILTIN_FLOAT,
                CHECK_OP_BUILTIN_BIT_CAST,
            };
            const CheckOp op = builtins ? sliced[check_test_random(test) % BUSTER_ARRAY_LENGTH(sliced)] : CHECK_OP_CALL;
            const uint32_t arguments = check_test_random(test) % 5;
            node = check_graph_add(graph, op, i, op == CHECK_OP_CALL ? a : BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->argument_offsets[node] = graph->argument_count;
            graph->argument_counts[node] = arguments;
            for (uint32_t k = 0; k < arguments; k += 1)
            {
                graph->argument_nodes[graph->argument_count++] = check_test_random(test) % i;
            }
        }
        else if (builtins && draw == 17)
        {
            node = check_graph_add(graph, CHECK_OP_STATEMENT_VALUE, i, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->members[node] = (check_test_random(test) & 1) != 0 ? BUSTER_CHECK_SIDE_EFFECT : 0;
        }
        else if (builtins && draw == 18)
        {
            const bool extent = (check_test_random(test) & 1) != 0;
            node = check_graph_add(graph, extent ? CHECK_OP_VLA_EXTENT : CHECK_OP_VLA_BOUND, i, extent ? BUSTER_CHECK_NONE : a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->members[node] = 1 + check_test_random(test) % 7;
        }
        else if (sideband && graph->varargs_count < graph->varargs_capacity && (check_test_random(test) & 3) == 0)
        {
            const bool type_name = (check_test_random(test) & 1) != 0;
            node = check_graph_add(graph, CHECK_OP_VARARGS, i, a, type_name ? BUSTER_CHECK_NONE : b, BUSTER_CHECK_NONE);
            graph->members[node] = type_name ? CBUILTIN_VA_ARG : CBUILTIN_VA_COPY;
            graph->argument_offsets[node] = graph->varargs_count;
            graph->argument_counts[node] = type_name ? 1 : 2;
            graph->varargs_calls[graph->varargs_count++] = (CheckVarargsCall){ .node = node };
        }
        else if (sideband && graph->x86_count < graph->x86_capacity && (check_test_random(test) & 3) == 0)
        {
            node = check_graph_add(graph, CHECK_OP_BUILTIN_X86, i, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->x86_calls[graph->x86_count++] = (CheckX86Call){ .node = node, .info = (const BuiltinIntrinsicInfo*)graph };
        }
        else if (sideband && graph->builtin_count < graph->builtin_capacity && (check_test_random(test) & 1) == 0)
        {
            node = check_graph_add(graph, CHECK_OP_BUILTIN_QUERY, i, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->builtin_calls[graph->builtin_count++] = (CheckBuiltinCall){ .node = node };
        }
        else if (sideband && graph->vector_member_count < graph->vector_member_capacity && (check_test_random(test) & 1) == 0)
        {
            node = check_graph_add(graph, CHECK_OP_MEMBER, i, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->vector_members[graph->vector_member_count++] = (CheckVectorCall){ .node = node };
        }
        else if (sideband && graph->overload_count < graph->overload_capacity)
        {
            node = check_graph_add(graph, CHECK_OP_CALL, i, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            graph->argument_offsets[node] = graph->argument_count;
            graph->overload_calls[graph->overload_count++] = (CheckOverloadCall){ .node = node, .head = 0 };
        }
        else
        {
            node = check_graph_add(graph, CHECK_OP_COMMA, i, a, b, BUSTER_CHECK_NONE);
        }
        // Offsets past a slice's end are valid for an operator without arguments while they stay within the slots.
        graph->argument_offsets[node] = graph->argument_counts[node] == 0 && graph->ops[node] != CHECK_OP_VARARGS ? check_test_random(test) % (graph->argument_count + 1) :
                                        graph->argument_offsets[node];
        graph->contexts[node] = (uint8_t)(check_test_random(test) & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_GNU_CONSTANT));
        if (graph->fp_environments != NULL)
        {
            graph->fp_environments[node] = (uint8_t)(check_test_random(test) & BUSTER_CHECK_FP_ENV_ACCESS);
        }
        if (graph->warning_suppressed != NULL)
        {
            graph->warning_suppressed[node] = (uint8_t)(check_test_random(test) & (CHECK_WARNING_INTEGER_OVERFLOW | CHECK_WARNING_SHIFT_COUNT_NEGATIVE));
        }
        if (graph->tag_completions != NULL)
        {
            graph->source_positions[node] = i;
        }
    }
}

// check_graph_validate_vector and its exact twin on one graph: the same result and failed node, and for a valid graph the
// same purity and counters.
static void check_test_graph_validators_agree(CheckTest* test, const CheckGraph* graph, uint8_t* scalar_pure, uint8_t* vector_pure, const char* description)
{
    CheckGraphValidation scalar_state;
    CheckGraphValidation vector_state;
    uint32_t scalar_failed = 0;
    uint32_t vector_failed = 0;
    const bool scalar = check_graph_validate(graph, scalar_pure, &scalar_state, &scalar_failed);
    const bool vector = check_graph_validate_vector(graph, vector_pure, &vector_state, &vector_failed);
    bool equal = scalar == vector && scalar_failed == vector_failed;
    if (equal && scalar)
    {
        equal = memcmp(scalar_pure, vector_pure, graph->count) == 0 && memcmp(&scalar_state, &vector_state, sizeof(scalar_state)) == 0;
    }
    check_test_expect(test, equal, description);
}

// One of the rules a node can break, applied at `node`: each breaks a rule of a different column or list (a node without the
// field the rule reads stays valid).
static void check_test_graph_break(CheckTest* test, CheckGraph* graph, uint32_t node, uint32_t rule)
{
    switch (rule)
    {
        case 0: graph->ops[node] = (CheckOp)(CHECK_OP_COUNT + check_test_random(test) % 200); break;
        case 1: graph->operand0[node] = node; break;
        case 2: graph->operand1[node] = graph->operand1[node] == BUSTER_CHECK_NONE ? (node != 0 ? node - 1 : 0) : BUSTER_CHECK_NONE; break;
        case 3: graph->operand2[node] = graph->operand2[node] == BUSTER_CHECK_NONE ? 0 : node + 1; break;
        case 4: graph->contexts[node] |= 0x10; break;
        case 5: graph->fp_environments[node] |= 0x02; break;
        case 6: graph->warning_suppressed[node] |= (uint8_t)CHECK_WARNING_DISCARDED_QUALIFIERS; break;
        case 7: graph->members[node] = graph->ops[node] == CHECK_OP_STATEMENT_VALUE ? 0x20 : 0; break;
        case 8: graph->argument_offsets[node] += 1; break;
        case 9: graph->argument_counts[node] += 1 + check_test_random(test) % 3; break;
        case 10:
        {
            if (graph->argument_counts[node] != 0 && graph->ops[node] != CHECK_OP_VARARGS)
            {
                graph->argument_nodes[graph->argument_offsets[node] + graph->argument_counts[node] - 1] = node;
            }
        } break;
        case 11: graph->leaves[node].overload_head = 1 + check_test_random(test) % 3; break;
        case 12: graph->source_positions[node] = BUSTER_CHECK_NONE; break;
        case 13: graph->types[node] = graph->types[node] == 0 ? BUSTER_TYPE_INT : 0; break;
        case 14: graph->argument_offsets[node] = graph->argument_count + 1 + check_test_random(test) % 3; break;
        default: graph->operand0[node] = graph->operand0[node] == BUSTER_CHECK_NONE ? node : BUSTER_CHECK_NONE; break;
    }
}

#define BUSTER_CHECK_TEST_GRAPH_RULES 16u

// #530: check_graph_validate_vector against check_graph_validate, its exact twin, on random graphs of every count to 70
// and of 4,099 nodes, with and without the exact path's sideband nodes, the optional byte columns and tag history: each
// graph whole, and once per rule broken at a random node.
static void check_test_graph_validation(CheckTest* test, Arena* storage)
{
    const uint64_t mark = storage->length;
    const uint32_t capacity = 4099;
    CheckGraph graph;
    bool ready = check_graph_initialize(&graph, storage, capacity, capacity * 4);
    uint8_t* fp_environments = BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, capacity);
    uint8_t* suppressed = BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, capacity);
    uint8_t* scalar_pure = BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, capacity + 1);
    uint8_t* vector_pure = BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, capacity + 1);
    CheckVarargsCall* varargs = BUSTER_ARENA_PUSH_ARRAY(storage, CheckVarargsCall, 64);
    CheckX86Call* x86 = BUSTER_ARENA_PUSH_ARRAY(storage, CheckX86Call, 64);
    CheckBuiltinCall* builtins = BUSTER_ARENA_PUSH_ARRAY(storage, CheckBuiltinCall, 64);
    CheckVectorCall* vectors = BUSTER_ARENA_PUSH_ARRAY(storage, CheckVectorCall, 64);
    CheckOverloadCall* overloads = BUSTER_ARENA_PUSH_ARRAY(storage, CheckOverloadCall, 64);
    const CheckOverloadCandidate candidate = {
        .previous = BUSTER_CHECK_NONE,
    };
    const uint32_t tags[1] = { 0 };
    ready = ready && fp_environments != NULL && suppressed != NULL && scalar_pure != NULL && vector_pure != NULL && varargs != NULL && x86 != NULL &&
            builtins != NULL && vectors != NULL && overloads != NULL;
    check_test_expect(test, ready, "validator comparison graph allocation");
    const TypeId pointer = BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_COUNT) + (3u << BUSTER_TYPE_INDEX_SHIFT);
    for (uint32_t world = 0; ready && world < 160; world += 1)
    {
        const uint32_t count = world < 71 ? world : world < 150 ? 16 * (world % 9) + world % 3 : capacity;
        const bool sideband = (world & 1) != 0;
        graph.fp_environments = (world & 2) != 0 ? fp_environments : NULL;
        graph.warning_suppressed = (world & 4) != 0 ? suppressed : NULL;
        graph.tag_completions = (world & 8) != 0 ? tags : NULL;
        graph.tag_completion_count = (world & 8) != 0 ? 1 : 0;
        graph.varargs_calls = varargs;
        graph.varargs_capacity = sideband ? 64 : 0;
        graph.varargs_count = 0;
        graph.x86_calls = x86;
        graph.x86_capacity = sideband ? 64 : 0;
        graph.x86_count = 0;
        graph.builtin_calls = builtins;
        graph.builtin_capacity = sideband ? 64 : 0;
        graph.builtin_count = 0;
        graph.vector_members = vectors;
        graph.vector_member_capacity = sideband ? 64 : 0;
        graph.vector_member_count = 0;
        graph.overload_calls = overloads;
        graph.overload_capacity = sideband ? 64 : 0;
        graph.overload_count = 0;
        graph.overload_candidates = &candidate;
        graph.overload_candidate_count = sideband ? 1 : 0;
        check_test_graph_validation_world(test, &graph, count, pointer, true, sideband);
        check_test_graph_validators_agree(test, &graph, scalar_pure, vector_pure, "vector graph validation differs from its exact twin on a valid graph");
        for (uint32_t rule = 0; count != 0 && rule < BUSTER_CHECK_TEST_GRAPH_RULES; rule += 1)
        {
            const uint32_t node = check_test_random(test) % count;
            const bool column = (rule != 5 || graph.fp_environments != NULL) && (rule != 6 || graph.warning_suppressed != NULL) &&
                                (rule != 11 || graph.ops[node] == CHECK_OP_VALUE) && (rule != 12 || graph.tag_completions != NULL);
            if (column)
            {
                const uint32_t saved_ops = graph.ops[node];
                const uint32_t saved_operands[3] = { graph.operand0[node], graph.operand1[node], graph.operand2[node] };
                const uint8_t saved_bytes[3] = { graph.contexts[node], fp_environments[node], suppressed[node] };
                const uint32_t saved_words[5] = { graph.members[node], graph.argument_offsets[node], graph.argument_counts[node], graph.source_positions[node], graph.types[node] };
                const uint32_t last_argument = graph.argument_count != 0 ? graph.argument_nodes[graph.argument_count - 1] : 0;
                const uint32_t slot = graph.argument_counts[node] != 0 && graph.ops[node] != CHECK_OP_VARARGS ? graph.argument_offsets[node] + graph.argument_counts[node] - 1 : 0;
                const uint32_t saved_argument = graph.argument_count != 0 ? graph.argument_nodes[slot] : 0;
                const uint32_t saved_head = graph.leaves[node].overload_head;
                check_test_graph_break(test, &graph, node, rule);
                check_test_graph_validators_agree(test, &graph, scalar_pure, vector_pure, "vector graph validation differs from its exact twin on a broken rule");
                graph.ops[node] = (CheckOp)saved_ops;
                graph.operand0[node] = saved_operands[0];
                graph.operand1[node] = saved_operands[1];
                graph.operand2[node] = saved_operands[2];
                graph.contexts[node] = saved_bytes[0];
                fp_environments[node] = saved_bytes[1];
                suppressed[node] = saved_bytes[2];
                graph.members[node] = saved_words[0];
                graph.argument_offsets[node] = saved_words[1];
                graph.argument_counts[node] = saved_words[2];
                graph.source_positions[node] = saved_words[3];
                graph.types[node] = saved_words[4];
                if (graph.argument_count != 0)
                {
                    graph.argument_nodes[slot] = saved_argument;
                    graph.argument_nodes[graph.argument_count - 1] = last_argument;
                }
                graph.leaves[node].overload_head = saved_head;
            }
        }
        // Without an optional column the blocks read, the vector path is the exact one throughout.
        uint32_t* members = graph.members;
        graph.members = NULL;
        check_test_graph_validators_agree(test, &graph, scalar_pure, vector_pure, "vector graph validation without member column differs from its exact twin");
        graph.members = members;
    }
    arena_pop_to(storage, mark);
}

// A copy of `bytes` bytes that ends where a PROT_NONE page starts, in its own mapping (for check_test_graph_validation_edges).
static void* check_test_graph_validation_page(const void* source, uint64_t bytes, uint64_t page, void** mapping, uint64_t* length)
{
    const uint64_t rounded = (bytes + page - 1) / page * page;
    *length = rounded + page;
    uint8_t* base = mmap(0, *length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* result = NULL;
    if (base != MAP_FAILED && mprotect(base + rounded, page, PROT_NONE) == 0)
    {
        result = base + rounded - bytes;
        memcpy(result, source, bytes);
    }
    *mapping = base != MAP_FAILED ? base : NULL;
    return result;
}

// #530: the vector validator and the leaf pass at page edges: every count to 70 with each column the blocks and the leaf pass
// read ending at a PROT_NONE page, through both validators and both of check_graph_run's paths, whose streams must agree.
static void check_test_graph_validation_edges(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t mark = storage->length;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, storage, 70, 280);
    uint8_t* scalar_pure = BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, 71);
    uint8_t* vector_pure = BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, 71);
    check_test_expect(test, ready && scalar_pure != NULL && vector_pure != NULL, "guarded validator graph allocation");
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    for (uint32_t count = 0; ready && scalar_pure != NULL && vector_pure != NULL && count <= 70; count += 1)
    {
        check_test_graph_validation_world(test, &graph, count, pointer, false, false);
        CheckGraph guarded = graph;
        void* mappings[11] = { 0 };
        uint64_t lengths[11] = { 0 };
        guarded.ops = check_test_graph_validation_page(graph.ops, (uint64_t)count * sizeof(CheckOp), page, &mappings[0], &lengths[0]);
        guarded.operand0 = check_test_graph_validation_page(graph.operand0, (uint64_t)count * sizeof(uint32_t), page, &mappings[1], &lengths[1]);
        guarded.operand1 = check_test_graph_validation_page(graph.operand1, (uint64_t)count * sizeof(uint32_t), page, &mappings[2], &lengths[2]);
        guarded.operand2 = check_test_graph_validation_page(graph.operand2, (uint64_t)count * sizeof(uint32_t), page, &mappings[3], &lengths[3]);
        guarded.types = check_test_graph_validation_page(graph.types, (uint64_t)count * sizeof(TypeId), page, &mappings[4], &lengths[4]);
        guarded.argument_offsets = check_test_graph_validation_page(graph.argument_offsets, (uint64_t)count * sizeof(uint32_t), page, &mappings[5], &lengths[5]);
        guarded.argument_counts = check_test_graph_validation_page(graph.argument_counts, (uint64_t)count * sizeof(uint32_t), page, &mappings[6], &lengths[6]);
        guarded.members = check_test_graph_validation_page(graph.members, (uint64_t)count * sizeof(uint32_t), page, &mappings[7], &lengths[7]);
        guarded.contexts = check_test_graph_validation_page(graph.contexts, count, page, &mappings[8], &lengths[8]);
        guarded.leaves = check_test_graph_validation_page(graph.leaves, (uint64_t)count * sizeof(CheckValue), page, &mappings[9], &lengths[9]);
        guarded.argument_nodes = check_test_graph_validation_page(graph.argument_nodes, (uint64_t)graph.argument_count * sizeof(uint32_t), page, &mappings[10], &lengths[10]);
        bool placed = true;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(mappings); i += 1)
        {
            placed &= mappings[i] != NULL;
        }
        check_test_expect(test, placed, "guarded validator columns");
        if (placed)
        {
            check_test_graph_validators_agree(test, &guarded, scalar_pure, vector_pure, "vector graph validation differs from its exact twin at a page edge");
            check_test_compare_graph(test, table, &guarded, output, scratch, false);
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(mappings); i += 1)
        {
            if (mappings[i] != NULL)
            {
                munmap(mappings[i], lengths[i]);
            }
        }
    }
    arena_pop_to(storage, mark);
}

static __attribute__((noinline)) void check_test_graph_regressions(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    check_test_graph_partial_allocations(test, table, storage);
    check_test_graph_argument_guards(test, table, storage, output, scratch);
    check_test_graph_barriers(test, storage, output, scratch);
    check_test_graph_report_order(test, table, storage, output, scratch);
    check_test_graph_updates(test, table, storage, output, scratch);
    check_test_graph_validation(test, storage);
    check_test_graph_validation_edges(test, table, storage, output, scratch);
}
