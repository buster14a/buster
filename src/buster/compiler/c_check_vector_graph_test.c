// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Compact vector member handoff, independently expected lane types and malformed metadata rollback.
#ifndef BUSTER_C_CHECK_VECTOR_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_VECTOR_GRAPH_TEST_INCLUDED

static void check_test_vector_graph(CheckTest* test, TypeTable* table)
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
        CheckStream scalar;
        const CheckOptions scalar_options = { .scalar = true };
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &scalar_options, &scalar);
        check_test_expect(test, checked && scalar.report_count == 0,
                          "sorted compact vector members bind to the exact source graph nodes");
        if (checked)
        {
            const TypeId pair = type_vector(table, BUSTER_TYPE_INT, 2, true);
            check_test_expect(test, scalar.values[first].type == pair && scalar.values[first].category == CHECK_LVALUE &&
                                   (scalar.values[first].flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0 &&
                                   (scalar.values[first].flags & BUSTER_CHECK_VECTOR_DUPLICATE) == 0 &&
                                   scalar.values[second].type == pair && (scalar.values[second].flags & BUSTER_CHECK_VECTOR_DUPLICATE) != 0,
                              "member lane order and duplicate classification preserve exact vector result types");
            const uint64_t mark = output.length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                const CheckOptions options = { .batch_size = batch };
                CheckStream actual;
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &options, &actual) &&
                                       check_test_stream_equal(&scalar, &actual, 0),
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
                check_test_expect(test, !check_graph_run(table, &invalid_graph, &output, &scratch, &scalar_options, &invalid) &&
                                       invalid.status == CHECK_STATUS_INVALID_GRAPH && output.length == mark && scratch.length == 0 &&
                                       invalid.values == NULL && invalid.count == 0,
                                  "malformed compact vector metadata fails within its bounds and rolls publication back");
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

#endif
