// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Compact vector member handoff, independently expected lane types and malformed metadata rollback.
#ifndef BUSTER_C_CHECK_VECTOR_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_VECTOR_GRAPH_TEST_INCLUDED

static void check_test_vector_arrow_graph(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    const bool types_ready = check_test_types_open(&types);
    TypeTable* table = &types.table;
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(131072);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const TypeId record = types_ready ? type_tag(table, BUSTER_TYPE_KIND_STRUCT, 2278001) : 0;
    const TypeMember members[] = {
        {
            .name = 2278002,
            .type = BUSTER_TYPE_CHAR,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        },
        {
            .name = 2278003,
            .type = BUSTER_TYPE_INT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        },
    };
    const bool complete = record != 0 && type_complete_record(table, record, members, BUSTER_ARRAY_LENGTH(members), (TypeRecordAttributes){ 0 });
    const bool ready = complete && check_graph_initialize(&graph, &storage, 16, 0);
    check_test_expect(test, ready, "mixed record and vector arrow graph storage");
    if (ready)
    {
        const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, true);
        const TypeId pair = type_vector(table, BUSTER_TYPE_INT, 2, true);
        const CheckValue record_value = {
            .type = record,
            .category = CHECK_LVALUE,
        };
        const CheckValue vector_value = {
            .type = vector,
            .category = CHECK_LVALUE,
        };
        const CheckValue vector_address = {
            .type = type_pointer(table, vector | BUSTER_TYPE_VOLATILE),
            .flags = BUSTER_CHECK_REGISTER,
        };
        const uint32_t object = check_test_leaf(&graph, 1, record_value);
        const uint32_t pointer = check_test_leaf(&graph, 2, (CheckValue){ .type = type_pointer(table, record) });
        const uint32_t vector_object = check_test_leaf(&graph, 3, vector_value);
        const uint32_t vector_pointer = check_test_leaf(&graph, 4, vector_address);
        const uint32_t record_dot = check_graph_add(&graph, CHECK_OP_MEMBER, 5, object, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t vector_dot = check_graph_add(&graph, CHECK_OP_MEMBER, 6, vector_object, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t record_arrow = check_graph_add(&graph, CHECK_OP_ARROW, 7, pointer, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t vector_arrow = check_graph_add(&graph, CHECK_OP_ARROW, 8, vector_pointer, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t duplicate_arrow = check_graph_add(&graph, CHECK_OP_ARROW, 9, vector_pointer, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.members[record_dot] = members[1].name;
        graph.members[record_arrow] = members[1].name;
        uint8_t lanes[] = {
            3,
            1,
            0,
            0,
            0,
        };
        CheckVectorCall calls[] = {
            {
                .node = record_dot,
                .member = {
                    .lanes = lanes,
                    .count = 1,
                    .kind = CHECK_VECTOR_MEMBER_LANES,
                },
            },
            {
                .node = vector_dot,
                .member = {
                    .lanes = lanes + 1,
                    .count = 2,
                    .kind = CHECK_VECTOR_MEMBER_LANES,
                },
            },
            {
                .node = record_arrow,
                .member = {
                    .lanes = lanes,
                    .count = 1,
                    .kind = CHECK_VECTOR_MEMBER_LANES,
                },
            },
            {
                .node = vector_arrow,
                .member = {
                    .lanes = lanes + 3,
                    .count = 1,
                    .kind = CHECK_VECTOR_MEMBER_LANES,
                },
            },
            {
                .node = duplicate_arrow,
                .member = {
                    .lanes = lanes + 3,
                    .count = 2,
                    .kind = CHECK_VECTOR_MEMBER_LANES,
                },
            },
        };
        graph.vector_members = calls;
        graph.vector_member_count = BUSTER_ARRAY_LENGTH(calls);
        graph.vector_member_capacity = BUSTER_ARRAY_LENGTH(calls);
        CheckStream reference;
        const CheckOptions node_order = { 0 };
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &node_order, &reference);
        check_test_expect(test, checked && reference.report_count == 0, "ordinary members and vector swizzles share a sorted side table");
        if (checked)
        {
            check_test_expect(test, check_stream_type(&reference, record_dot) == BUSTER_TYPE_INT && check_stream_type(&reference, record_arrow) == BUSTER_TYPE_INT &&
                                   (check_stream_flags(&reference, record_dot) & BUSTER_CHECK_VECTOR_ELEMENT) == 0 &&
                                   (check_stream_flags(&reference, record_arrow) & BUSTER_CHECK_VECTOR_ELEMENT) == 0 &&
                                   reference.member_offsets[record_dot] == 32 && reference.member_offsets[record_arrow] == 32,
                              "record fields whose names decode as swizzles keep their types and offsets through dot and arrow");
            check_test_expect(test, check_stream_type(&reference, vector_dot) == pair &&
                                   check_stream_type(&reference, vector_arrow) == (BUSTER_TYPE_INT | BUSTER_TYPE_VOLATILE) &&
                                   check_stream_category(&reference, vector_arrow) == CHECK_LVALUE &&
                                   (check_stream_flags(&reference, vector_arrow) & (BUSTER_CHECK_VECTOR_ELEMENT | BUSTER_CHECK_REGISTER)) == BUSTER_CHECK_VECTOR_ELEMENT &&
                                   check_stream_type(&reference, duplicate_arrow) == (pair | BUSTER_TYPE_VOLATILE) &&
                                   (check_stream_flags(&reference, duplicate_arrow) & BUSTER_CHECK_VECTOR_DUPLICATE) != 0,
                              "lazy arrow lookup selects its own lanes, pointee qualifiers and duplicate flag");
            CheckAction action;
            check_graph_action(&graph, &reference, record_arrow, NULL, &action);
            check_test_expect(test, action.vector_member_count == graph.vector_member_count && action.vector_members == graph.vector_members,
                              "ordinary arrows carry the bounded side table without searching it");
            check_graph_action(&graph, &reference, vector_dot, NULL, &action);
            check_test_expect(test, action.vector_member_count == 0 && action.vector_member == &calls[1].member,
                              "dot member actions retain their direct descriptor handoff");
            const uint64_t mark = output.length;
            for (uint32_t batch = 0; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                const CheckOptions options = {
                    .batch_size = batch,
                    .regions = batch != 0,
                };
                CheckStream actual;
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &options, &actual) &&
                                       check_test_stream_equal(&reference, &actual, 0),
                                  "every scheduler partition preserves lazy arrows beside eager dot members");
                arena_pop_to(&output, mark);
            }
            CheckVectorCall sparse[] = {
                calls[1],
                calls[4],
            };
            for (uint32_t missing = 0; missing < 5; missing += 1)
            {
                CheckGraph incomplete = graph;
                incomplete.vector_members = missing == 0 ? NULL : missing <= 2 ? calls : missing == 3 ? calls + 4 : sparse;
                incomplete.vector_member_count = missing <= 1 ? 0 : missing == 3 ? 1 : 2;
                incomplete.vector_member_capacity = incomplete.vector_member_count;
                check_graph_action(&incomplete, &reference, vector_arrow, NULL, &action);
                check_test_expect(test, missing > 1 || (action.vector_member_count == 0 && action.vector_member == NULL),
                                  "an empty graph side table hands off no direct descriptor even when its storage is allocated");
                const CheckActionResult absent = check_test_action(test, table, action);
                check_test_expect(test, absent.diagnostic == CHECK_DIAG_MEMBER,
                                  "an empty table or a missing arrow before, within or after its records reports a member error");
                check_graph_action(&incomplete, &reference, record_arrow, NULL, &action);
                const CheckActionResult ordinary = check_test_action(test, table, action);
                check_test_expect(test, ordinary.diagnostic == CHECK_DIAG_NONE && ordinary.value.type == BUSTER_TYPE_INT && ordinary.member_offset == 32,
                                  "ordinary arrow semantics do not depend on a swizzle record being present");
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    check_test_types_close(&types);
}

static __attribute__((noinline)) void check_test_vector_graph(CheckTest* test, TypeTable* table)
{
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(131072);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, &storage, 8, 0);
    check_test_expect(test, ready, "vector member graph storage");
    if (ready)
    {
        const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, true);
        const uint32_t base = check_test_leaf(&graph, 1, (CheckValue){ .type = vector, .category = CHECK_LVALUE, .object = 401 });
        const uint32_t first = check_graph_add(&graph, CHECK_OP_MEMBER, 2, base, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        const uint32_t second = check_graph_add(&graph, CHECK_OP_MEMBER, 3, base, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        uint8_t lanes[] = { 3, 1, 2, 2 };
        CheckVectorCall calls[] = {
            { .node = first, .member = { .lanes = lanes, .count = 2, .kind = CHECK_VECTOR_MEMBER_LANES } },
            { .node = second, .member = { .lanes = lanes + 2, .count = 2, .kind = CHECK_VECTOR_MEMBER_LANES } },
        };
        graph.vector_members = calls;
        graph.vector_member_count = 2;
        graph.vector_member_capacity = 2;
        CheckStream reference;
        const CheckOptions node_order = { 0 };
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &node_order, &reference);
        check_test_expect(test, checked && reference.report_count == 0,
                          "sorted compact vector members bind to the exact source graph nodes");
        if (checked)
        {
            const TypeId pair = type_vector(table, BUSTER_TYPE_INT, 2, true);
            check_test_expect(test, check_stream_type(&reference, first) == pair && check_stream_category(&reference, first) == CHECK_LVALUE &&
                                   (check_stream_flags(&reference, first) & BUSTER_CHECK_VECTOR_ELEMENT) != 0 &&
                                   (check_stream_flags(&reference, first) & BUSTER_CHECK_VECTOR_DUPLICATE) == 0 &&
                                   check_stream_type(&reference, second) == pair && (check_stream_flags(&reference, second) & BUSTER_CHECK_VECTOR_DUPLICATE) != 0,
                              "member lane order and duplicate classification preserve exact vector result types");
            const uint64_t mark = output.length;
            check_test_expect(test, check_test_stream_recorded(&reference, &graph, 0, "vector-graphs"), "vector-graphs: the reference stream is its recorded digest");
            for (uint32_t batch = 0; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                const CheckOptions options = { .batch_size = batch, .regions = batch != 0 };
                CheckStream actual;
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &options, &actual) &&
                                       check_test_stream_equal(&reference, &actual, 0),
                                  "all graph scheduler partitions preserve compact member payload semantics");
                arena_pop_to(&output, mark);
            }
            for (uint32_t mutation = 0; mutation < 7; mutation += 1)
            {
                CheckGraph invalid_graph = graph;
                CheckVectorCall invalid_calls[] = { calls[0], calls[1] };
                invalid_graph.vector_members = invalid_calls;
                switch (mutation)
                {
                    case 0: invalid_graph.vector_member_count = 3; break;
                    case 1: invalid_graph.vector_members = NULL; break;
                    case 2: invalid_calls[0].node = second; break;
                    case 3: invalid_calls[1].node = first; break;
                    case 4: invalid_calls[0].node = base; break;
                    case 5: invalid_calls[0].member.lanes = NULL; break;
                    default: invalid_calls[0].member.kind = CHECK_VECTOR_MEMBER_ODD + 1; break;
                }
                CheckStream invalid;
                check_test_expect(test, !check_graph_run(table, &invalid_graph, &output, &scratch, &node_order, &invalid) &&
                                       invalid.status == CHECK_STATUS_INVALID_GRAPH && output.length == mark && scratch.length == 0 &&
                                       !check_stream_has_values(&invalid) && invalid.count == 0,
                                  "malformed compact vector metadata fails within its bounds and rolls publication back");
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    check_test_vector_arrow_graph(test);
}

#endif
