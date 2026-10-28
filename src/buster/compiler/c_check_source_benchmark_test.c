// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original extraction regression. Independent region slices import expression dependencies from an excluded
// function, so selecting a body must close those edges and compact the control slices without changing targets.
// Include after c_check_source_benchmark.c; the shared checker command calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_SOURCE_BENCHMARK_TEST_INCLUDED
#define BUSTER_C_CHECK_SOURCE_BENCHMARK_TEST_INCLUDED

static __attribute__((noinline)) void check_test_source_benchmark(CheckTest* test)
{
    static const char source[] =
        "int before(void) { return ({ 9; 11; 13; }); }\n"
        "int selected(void) { return ({ 5; }); }\n"
        "int after(void) { return ({ 7; }); }\n";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = {
        .language = { .standard = 2017, .gnu = true },
        .no_standard_includes = true,
    };
    const bool prepared = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                          pp_run_source(&pp, "region-extraction.c", (const uint8_t*)source, sizeof(source) - 1);
    check_test_expect(test, prepared, "body extraction original source preprocesses");
    for (uint32_t path = 0; prepared && path < 2; path += 1)
    {
        CheckCorpusPath checked = { 0 };
        bool valid = check_corpus_check(&checked, &parser, &pp, path == 0);
        check_test_expect(test, valid, "body extraction original source passes complete checking");
        CheckGraph* original = &checked.input.graph;
        uint32_t owners[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t dependencies[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t owner_count = 0;
        for (uint32_t node = 0; valid && node < original->count; node += 1)
        {
            if (original->ops[node] == CHECK_OP_STATEMENT_VALUE && owner_count < BUSTER_ARRAY_LENGTH(owners))
            {
                owners[owner_count++] = node;
            }
            if (original->ops[node] == CHECK_OP_VALUE)
            {
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(dependencies); i += 1)
                {
                    if (check_graph_leaf_of(original, node)->constant_kind == CHECK_CONSTANT_INTEGER && check_graph_leaf_of(original, node)->integer == 9 + 2 * i)
                    {
                        dependencies[i] = node;
                    }
                }
            }
        }
        valid = valid && owner_count == BUSTER_ARRAY_LENGTH(owners);
        for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(dependencies); i += 1)
        {
            valid = dependencies[i] < owners[0];
        }
        check_test_expect(test, valid, "three ordered regions and distinct earlier dependency leaves exist");
        CheckRegionProof proofs[3] = {
            { .node = owners[0], .first = 0, .count = 1 },
            { .node = owners[1], .first = 1, .count = 5 },
            { .node = owners[2], .first = 6, .count = 1 },
        };
        CheckRegionProofStep steps[7] = {
            { .op = CHECK_REGION_END, .node = BUSTER_CHECK_NONE, .operand = BUSTER_CHECK_NONE, .target = BUSTER_CHECK_NONE },
            { .op = CHECK_REGION_CASE, .node = dependencies[0], .operand = dependencies[1], .target = 3 },
            { .op = CHECK_REGION_BRANCH, .node = dependencies[2], .operand = BUSTER_CHECK_NONE, .target = 4 },
            { .op = CHECK_REGION_JUMP, .node = BUSTER_CHECK_NONE, .operand = BUSTER_CHECK_NONE, .target = 4 },
            { .op = CHECK_REGION_EVAL, .node = dependencies[0], .operand = BUSTER_CHECK_NONE, .target = BUSTER_CHECK_NONE },
            { .op = CHECK_REGION_END, .node = BUSTER_CHECK_NONE, .operand = BUSTER_CHECK_NONE, .target = BUSTER_CHECK_NONE },
            { .op = CHECK_REGION_END, .node = BUSTER_CHECK_NONE, .operand = BUSTER_CHECK_NONE, .target = BUSTER_CHECK_NONE },
        };
        original->region_proofs = proofs;
        original->region_proof_count = original->region_proof_capacity = BUSTER_ARRAY_LENGTH(proofs);
        original->region_steps = steps;
        original->region_step_count = original->region_step_capacity = BUSTER_ARRAY_LENGTH(steps);
        Arena graph_arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        CheckGraph graph = { 0 };
        uint32_t bodies = 0;
        uint32_t* mapping = NULL;
        valid = valid && graph_arena.base != NULL && output.base != NULL && scratch.base != NULL &&
                check_source_body_graph(&graph, &checked.input, &parser, &pp, "selected", &graph_arena, &scratch, &bodies, &mapping);
        check_test_expect(test, valid, "body selection closes typed region dependencies");
        if (valid)
        {
            check_test_expect(test, bodies == 1 && graph.region_proof_count == 1 && graph.region_step_count == 5 &&
                              graph.region_proofs[0].first == 0 && graph.region_proofs[0].count == 5 &&
                              graph.region_proofs[0].node == mapping[owners[1]] && mapping[owners[0]] == BUSTER_CHECK_NONE &&
                              mapping[owners[2]] == BUSTER_CHECK_NONE, "body selection compacts exactly its region and removes unrelated owners");
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(dependencies); i += 1)
            {
                check_test_expect(test, mapping[dependencies[i]] != BUSTER_CHECK_NONE, "region dependency outside the chosen body survives extraction");
            }
            check_test_expect(test, graph.region_steps[0].op == CHECK_REGION_CASE && graph.region_steps[0].node == mapping[dependencies[0]] &&
                              graph.region_steps[0].operand == mapping[dependencies[1]] && graph.region_steps[0].target == 3 &&
                              graph.region_steps[1].op == CHECK_REGION_BRANCH && graph.region_steps[1].node == mapping[dependencies[2]] &&
                              graph.region_steps[1].operand == BUSTER_CHECK_NONE && graph.region_steps[1].target == 4 &&
                              graph.region_steps[2].op == CHECK_REGION_JUMP && graph.region_steps[2].node == BUSTER_CHECK_NONE && graph.region_steps[2].target == 4 &&
                              graph.region_steps[3].op == CHECK_REGION_EVAL && graph.region_steps[3].node == mapping[dependencies[0]] &&
                              graph.region_steps[4].op == CHECK_REGION_END, "region nodes remap while relative CASE, branch and jump targets remain exact");
            // Each of the benchmark's paths: scalar, the vector path's postorder batches and its level order.
            for (uint32_t mode = 0; mode < BUSTER_CHECK_SOURCE_PATHS; mode += 1)
            {
                CheckStream stream = { 0 };
                const bool ran = check_source_graph_run(&checked.types, &graph, &output, &scratch, mode, &stream);
                check_test_expect(test, ran, "extracted region graph validates and checks");
                if (ran)
                {
                    check_test_expect(test, check_source_body_equal(original, &checked.stream, &graph, &stream, mapping),
                                      "extracted region typed stream and compact metadata equal the source");
                    graph.region_steps[0].target = 4;
                    check_test_expect(test, !check_source_body_equal(original, &checked.stream, &graph, &stream, mapping),
                                      "extraction proof detects a changed relative CASE target");
                    graph.region_steps[0].target = 3;
                    graph.region_steps[0].operand = mapping[dependencies[0]];
                    check_test_expect(test, !check_source_body_equal(original, &checked.stream, &graph, &stream, mapping),
                                      "extraction proof detects a changed CASE constant dependency");
                    graph.region_steps[0].operand = mapping[dependencies[1]];
                }
            }
        }
        arena_release(&graph_arena);
        arena_release(&output);
        arena_release(&scratch);
        check_corpus_close(&checked);
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

#endif
