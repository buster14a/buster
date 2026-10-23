// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Builtin graph integration: exact results, ordered store descriptors, source histories, malformed IDs and
// bounded output rollback. Include after the shared checker and graph-test helpers.
#ifndef BUSTER_C_CHECK_BUILTIN_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_GRAPH_TEST_INCLUDED

static uint32_t check_test_builtin_node(CheckGraph* graph, CheckOp op, CBuiltinId id, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = check_graph_add(graph, op, graph->count + 10, a, b, c);
    if (node != BUSTER_CHECK_NONE)
    {
        graph->members[node] = (uint32_t)id;
    }
    return node;
}

static void check_test_builtin_graph_values(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, storage, 16, 0);
    check_test_expect(test, ready, "builtin graph fixture allocation");
    if (ready)
    {
        const TypeId destination = type_pointer(table, type_qualify(table, BUSTER_TYPE_SCHAR, BUSTER_TYPE_VOLATILE));
        const uint32_t a = check_test_leaf(&graph, 10, check_test_integer(BUSTER_TYPE_UCHAR, 255));
        const uint32_t b = check_test_leaf(&graph, 11, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t pointer = check_test_leaf(&graph, 12, (CheckValue){
            .type = destination,
            .constant_kind = CHECK_CONSTANT_ADDRESS,
            .object = 211,
            .offset = 3,
            .flags = BUSTER_CHECK_STATIC_OBJECT,
        });
        const uint32_t first = check_test_builtin_node(&graph, CHECK_OP_BUILTIN_OVERFLOW, CBUILTIN_ADD_OVERFLOW, a, b, pointer);
        const uint32_t unary = check_test_builtin_node(&graph, CHECK_OP_BUILTIN_UNARY, CBUILTIN_POPCOUNT, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t invalid = check_test_builtin_node(&graph, CHECK_OP_BUILTIN_UNARY, CBUILTIN_COUNT, a, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t second = check_test_builtin_node(&graph, CHECK_OP_BUILTIN_OVERFLOW, CBUILTIN_SUB_OVERFLOW, a, b, pointer);
        const uint32_t selected = check_graph_add(&graph, CHECK_OP_CONDITIONAL, 18, b, b, first);
        graph.contexts[selected] = BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
        const uint32_t query = check_graph_add(&graph, CHECK_OP_SIZEOF, 19, second, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.contexts[query] = BUSTER_CHECK_CONTEXT_ASSERT;
        CheckStream expected;
        const CheckOptions scalar = { .scalar = true };
        bool checked = check_graph_run(table, &graph, output, scratch, &scalar, &expected);
        check_test_expect(test, checked && expected.report_count == 1 && expected.reports[0].node == invalid &&
                               expected.diagnostics[invalid] == CHECK_DIAG_UNSUPPORTED && expected.effect_count == 2,
                          "invalid builtin IDs produce one diagnostic and no extra store descriptor");
        if (checked)
        {
            check_test_expect(test, expected.values[unary].integer == 8 && expected.values[unary].type == BUSTER_TYPE_INT &&
                                   (expected.values[unary].flags & BUSTER_CHECK_ICE) != 0,
                              "graph unary builtin has independently expected result and ICE classification");
            check_test_expect(test, expected.values[first].integer == 1 && expected.values[second].integer == 1 &&
                                   expected.values[first].type == BUSTER_TYPE_BOOL &&
                                   (expected.values[first].flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                   (expected.values[first].flags & BUSTER_CHECK_ICE) == 0,
                              "folded overflow keeps the boolean and required store effect distinct");
            check_test_expect(test, expected.effects[0].node == first && expected.effects[1].node == second &&
                                   expected.effects[0].value.type == BUSTER_TYPE_SCHAR && expected.effects[0].value.integer == 0 &&
                                   expected.effects[1].value.integer == 254 && expected.effects[0].destination.object == 211 &&
                                   expected.effects[0].destination.offset == 3 && expected.effects[0].destination.type == destination,
                              "compact store effects preserve source order, target bits and destination provenance");
            check_test_expect(test, expected.conversions[first].operands[0].type == BUSTER_TYPE_UCHAR &&
                                   expected.conversions[first].operands[1].type == BUSTER_TYPE_INT &&
                                   expected.conversions[first].store.type == type_element(table, destination),
                              "overflow graph conversions retain original integer shapes and volatile store type");
            check_test_expect(test, expected.values[selected].integer == 1 && (expected.values[selected].flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                                   expected.values[query].integer == 1 && (expected.values[query].flags & BUSTER_CHECK_SIDE_EFFECT) == 0,
                              "unselected and unevaluated overflow stores stay descriptors without becoming executed effects");
            const uint64_t comparison_mark = output->length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                const CheckOptions vector = { .batch_size = batch };
                CheckStream actual;
                checked = check_graph_run(table, &graph, output, scratch, &vector, &actual);
                check_test_expect(test, checked && check_test_stream_equal(&expected, &actual, graph.argument_count),
                                  "all legal graph partitions preserve full builtin values, conversions and effects");
                arena_pop_to(output, comparison_mark);
            }
        }
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, storage_mark);
}

static void check_test_builtin_graph_bounds(CheckTest* test, TypeTable* table, Arena* storage)
{
    const uint64_t storage_mark = storage->length;
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, storage, 4, 0);
    check_test_expect(test, ready, "builtin bounded-output fixture allocation");
    if (ready)
    {
        const uint32_t a = check_test_leaf(&graph, 10, check_test_integer(BUSTER_TYPE_INT, 2));
        const uint32_t b = check_test_leaf(&graph, 11, check_test_integer(BUSTER_TYPE_INT, 3));
        const uint32_t p = check_test_leaf(&graph, 12, (CheckValue){ .type = type_pointer(table, BUSTER_TYPE_INT) });
        check_test_builtin_node(&graph, CHECK_OP_BUILTIN_OVERFLOW, CBUILTIN_MUL_OVERFLOW, a, b, p);
        _Alignas(16) uint8_t output_bytes[4096];
        _Alignas(16) uint8_t scratch_bytes[4096];
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Arena output = { .base = output_bytes, .length = 7, .capacity = sizeof(output_bytes) - 16 };
            Arena scratch = { .base = scratch_bytes, .length = 13, .capacity = sizeof(scratch_bytes) - 16 };
            const CheckOptions options = { .scalar = path == 0 };
            CheckStream stream;
            const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
            check_test_expect(test, checked && stream.effect_count == 1 && stream.effects[0].value.integer == 6,
                              "builtin bounded-output fixture computes independently expected store");
            const uint64_t required[] = { output.length, 13 + stream.statistics.scratch_bytes };
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
                    const bool accepted = check_graph_run(table, &graph, &output, &scratch, &options, &stream);
                    check_test_expect(test, !accepted && stream.status == CHECK_STATUS_RESOURCE && stream.effects == NULL && stream.effect_count == 0 &&
                                           stream.values == NULL && output.length == 7 && scratch.length == 13 &&
                                           check_test_graph_canary(output_bytes, 0, 7) && check_test_graph_canary(scratch_bytes, 0, 13) &&
                                           check_test_graph_canary(output_bytes, output.capacity, output.capacity + 16) &&
                                           check_test_graph_canary(scratch_bytes, scratch.capacity, scratch.capacity + 16),
                                      "every partial builtin stream/effect allocation rolls back and preserves arena guards");
                }
            }
        }
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_builtin_graph_histories(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 212);
    const bool completed = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
    CheckGraph graph;
    bool ready = completed && check_graph_initialize(&graph, storage, 5, 0);
    uint32_t* history = ready ? BUSTER_ARENA_PUSH_ARRAY(storage, uint32_t, table->tags.count) : NULL;
    ready = ready && history != NULL;
    check_test_expect(test, ready, "builtin source-history fixture allocation");
    if (ready)
    {
        memset(history, 0, (uint64_t)table->tags.count * sizeof(uint32_t));
        history[table->types.index[type_index(enumeration)]] = 20;
        graph.tag_completions = history;
        graph.tag_completion_count = table->tags.count;
        const uint32_t a = check_test_leaf(&graph, 10, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t b = check_test_leaf(&graph, 11, check_test_integer(BUSTER_TYPE_INT, 2));
        const uint32_t p = check_test_leaf(&graph, 12, (CheckValue){ .type = type_pointer(table, enumeration) });
        const uint32_t early = check_test_builtin_node(&graph, CHECK_OP_BUILTIN_OVERFLOW, CBUILTIN_ADD_OVERFLOW, a, b, p);
        const uint32_t late = check_test_builtin_node(&graph, CHECK_OP_BUILTIN_OVERFLOW, CBUILTIN_ADD_OVERFLOW, a, b, p);
        for (uint32_t i = 0; i < graph.count; i += 1)
        {
            graph.source_positions[i] = i == late ? 20 : 19;
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const CheckOptions options = { .scalar = path == 0 };
            CheckStream stream;
            const bool checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, checked && stream.report_count == 1 && stream.diagnostics[early] == CHECK_DIAG_INCOMPLETE &&
                                   stream.diagnostics[late] == CHECK_DIAG_NONE && stream.effect_count == 1 &&
                                   stream.effects[0].node == late && stream.effects[0].value.integer == 3,
                              "builtin stores respect source-time enum completion and never retain invalid early effects");
            arena_pop_to(output, output_mark);
        }
    }
    arena_pop_to(storage, storage_mark);
}

static void check_test_builtin_graph_guards(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t storage_mark = storage->length;
    const uint64_t output_mark = output->length;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    CheckGraph graph;
    ready = check_graph_initialize(&graph, storage, 36, 0) && ready;
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    check_test_expect(test, ready && pointer != 0, "builtin protected member-column allocation");
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        graph.count = 0;
        graph.capacity = count == 0 ? 0 : count + 3;
        graph.members = (uint32_t*)(mapping + page - graph.capacity * sizeof(uint32_t));
        if (count != 0)
        {
            const uint32_t a = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 2));
            const uint32_t b = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, 3));
            const uint32_t p = check_test_leaf(&graph, 3, (CheckValue){ .type = pointer });
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_test_builtin_node(&graph, CHECK_OP_BUILTIN_OVERFLOW, CBUILTIN_MUL_OVERFLOW, a, b, p);
            }
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const CheckOptions options = { .scalar = path == 0 };
            CheckStream stream;
            const bool checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
            bool effects = checked && stream.report_count == 0 && stream.effect_count == count;
            for (uint32_t i = 0; effects && i < count; i += 1)
            {
                effects = stream.effects[i].node == i + 3 && stream.effects[i].value.integer == 6;
            }
            check_test_expect(test, effects && (count != 0 || stream.effects == NULL),
                              "zero/full/tail builtin graph slices read no protected member entries and append exactly their stores");
            arena_pop_to(output, output_mark);
        }
        if (count != 0)
        {
            const uint32_t last = graph.count - 1;
            const uint32_t invalid[] = { CBUILTIN_NONE, CBUILTIN_COUNT, UINT32_MAX, CBUILTIN_POPCOUNT };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
            {
                graph.members[last] = invalid[i];
                const CheckOptions options = { 0 };
                CheckStream stream;
                const bool checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
                check_test_expect(test, checked && stream.report_count == 1 && stream.reports[0].node == last && stream.effect_count == count - 1 &&
                                       stream.diagnostics[last] == (i == 3 ? CHECK_DIAG_ARGUMENT_COUNT : CHECK_DIAG_UNSUPPORTED),
                                  "invalid builtin IDs and family/arity mismatch at the protected tail do not publish a store");
                arena_pop_to(output, output_mark);
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, storage_mark);
}

static void check_test_builtin_graph_oracle(CheckTest* test, const char* clang)
{
    const char* sources[] = {
        "_Static_assert(__builtin_popcount((unsigned char)255)==8,\"count\");\n",
        "_Static_assert(__builtin_popcount((unsigned char)255)==7,\"mutation\");\n",
        "volatile signed char destination;_Static_assert(sizeof(__builtin_add_overflow((unsigned char)255,1,&destination))==1,\"unevaluated\");\n",
        "signed char destination;_Static_assert(1||__builtin_add_overflow((unsigned char)255,1,&destination),\"unselected\");\n",
        "signed char destination;_Static_assert(__builtin_add_overflow((unsigned char)255,1,&destination),\"selected store\");\n",
        "const int destination=0;int f(void){return __builtin_add_overflow(1,2,&destination);}\n",
        "int destination;int f(void){return __builtin_add_overflow(1.5,2,&destination);}\n",
        "_Static_assert(__builtin_clz(0),\"undefined count\");\n",
    };
    const bool rejected[] = { false, true, false, false, true, true, true, true };
    char directory[] = "/tmp/buster-check-builtin-graph-XXXXXX";
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
        check_test_expect(test, checked, "independent builtin graph-context oracle runs");
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
        {
            check_test_expect(test, outcomes[i].rejected == rejected[i], "Clang confirms builtin value/effect/constraint semantic mutations");
        }
    }
    check_test_expect(test, ready, "builtin graph oracle source creation");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_test_builtin_graph(CheckTest* test, TypeTable* table, const char* clang)
{
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool ready = storage.base != NULL && output.base != NULL && scratch.base != NULL;
    check_test_expect(test, ready, "builtin graph test arenas");
    if (ready)
    {
        check_test_builtin_graph_values(test, table, &storage, &output, &scratch);
        check_test_builtin_graph_bounds(test, table, &storage);
        check_test_builtin_graph_histories(test, table, &storage, &output, &scratch);
        check_test_builtin_graph_guards(test, table, &storage, &output, &scratch);
        check_test_builtin_graph_oracle(test, clang);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

#endif
