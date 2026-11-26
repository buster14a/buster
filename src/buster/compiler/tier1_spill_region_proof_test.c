// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Small explicit CFG walks qualify the ordinary-spill policy's two separate obligations: no losing path for
// any metric, and at least one complete path with fewer real memory records. Heuristic weights prove neither.

static void tier1_spill_region_test_proof(Tier1EmitTest* test, const Tier1CfgInput* input,
                                          const Tier1EmitSpillScore* score, Tier1EmitRegionPathStatus expected,
                                          uint32_t metric, uint64_t initial_work, const char* message)
{
    const uint64_t mark = test->scratch.length;
    uint64_t work = initial_work;
    uint32_t failed = UINT32_MAX;
    const Tier1EmitRegionPathStatus status = tier1_emit_spill_regions_proof(input, score, &test->scratch, &work, &failed);
    tier1_emit_test_expect(test, status == expected && failed == metric && test->scratch.length == mark, message);
}

static void tier1_spill_region_test_witness(Tier1EmitTest* test, const Tier1CfgInput* input,
                                            const int64_t* blocks, const int64_t* edges, Tier1EmitError expected,
                                            bool positive, uint64_t initial_work, const char* message)
{
    const uint64_t mark = test->scratch.length;
    uint64_t work = initial_work;
    bool witness = !positive;
    const Tier1EmitError error = tier1_emit_spill_regions_witness(input, blocks, edges, &test->scratch, &work, &witness);
    tier1_emit_test_expect(test, error == expected && witness == (error == TIER1_EMIT_OK ? positive : !positive) &&
                                  test->scratch.length == mark, message);
}

static void tier1_spill_region_proof_tests(Tier1EmitTest* test)
{
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    const uint32_t chain_offsets[] = { 0, 1, 1 };
    const uint32_t chain_targets[] = { 1 };
    const uint32_t chain_predecessor_offsets[] = { 0, 0, 1 };
    const uint32_t chain_predecessors[] = { 0 };
    const Tier1CfgInput chain = {
        .successor_offsets = chain_offsets,
        .successors = chain_targets,
        .predecessor_offsets = chain_predecessor_offsets,
        .predecessors = chain_predecessors,
        .block_count = 2,
        .edge_count = 1,
    };
    int64_t chain_blocks[] = { 1, 0 };
    int64_t memory[] = { -2 };
    int64_t transport[] = { 0 };
    int64_t total[] = { 0 };
    Tier1EmitSpillScore score = {
        .records = { .block_gains = chain_blocks, .edge_gains = transport, .total_edge_gains = total },
        .memory_edges = memory,
    };
    tier1_spill_region_test_proof(test, &chain, &score, TIER1_REGION_PATH_LOSS, 1, 0,
                                  "spill memory proof rejects a real-memory loss even when other metrics improve");
    memory[0] = 0;
    transport[0] = -2;
    total[0] = 20;
    tier1_spill_region_test_proof(test, &chain, &score, TIER1_REGION_PATH_LOSS, 2, 0,
                                  "fewer branch instructions cannot pay for more spill transport");
    transport[0] = 0;
    total[0] = -2;
    tier1_spill_region_test_proof(test, &chain, &score, TIER1_REGION_PATH_LOSS, 3, 0,
                                  "spill total proof rejects added instructions after memory and transport pass");
    total[0] = 0;

    // An isolated call replaces C+R by S, then an exit load: +1 in the call block and -1 at its successor.
    // The only complete path saves zero, although unrelated block weights 100 and 1 report a gain of 99.
    chain_blocks[1] = -1;
    score.memory = (__int128)100 * chain_blocks[0] + chain_blocks[1];
    score.transport = score.memory;
    score.total = score.memory;
    tier1_emit_test_expect(test, score.memory > 0, "inconsistent heuristic weights can favor a neutral spill rewrite");
    tier1_spill_region_test_proof(test, &chain, &score, TIER1_REGION_PATH_OK, 0, 0,
                                  "an entirely neutral spill rewrite satisfies all three nonincrease checks");
    tier1_spill_region_test_witness(test, &chain, chain_blocks, memory, TIER1_EMIT_OK, false, 0,
                                    "strict spill witness rejects neutral C/R-to-S/L migration despite positive heuristic gain");
    chain_blocks[0] = 0;
    chain_blocks[1] = 0;
    tier1_spill_region_test_witness(test, &chain, chain_blocks, memory, TIER1_EMIT_OK, false, 0,
                                    "an isolated equal-count call rewrite has no positive complete-path witness");

    // Complete paths: 0,1,3,4,5 saves one; 0,2,4,5 saves zero; 0,5 calls nothing and changes nothing.
    // The last edge of each call path pays the real ordinary-slot reload; the bypass never traverses it.
    const uint32_t fork_offsets[] = { 0, 3, 4, 5, 6, 7, 7 };
    const uint32_t fork_targets[] = { 1, 2, 5, 3, 4, 4, 5 };
    const uint32_t fork_predecessor_offsets[] = { 0, 0, 1, 2, 3, 5, 7 };
    const uint32_t fork_predecessors[] = { 0, 0, 1, 2, 3, 0, 4 };
    const Tier1CfgInput fork = {
        .successor_offsets = fork_offsets,
        .successors = fork_targets,
        .predecessor_offsets = fork_predecessor_offsets,
        .predecessors = fork_predecessors,
        .block_count = 6,
        .edge_count = 7,
    };
    int64_t fork_blocks[] = { 0, 1, 1, 1, 0, 0 };
    int64_t fork_edges[] = { 0, 0, 0, 0, 0, 0, -1 };
    score = (Tier1EmitSpillScore){
        .records = { .block_gains = fork_blocks, .edge_gains = fork_edges, .total_edge_gains = fork_edges },
        .memory_edges = fork_edges,
    };
    tier1_spill_region_test_proof(test, &fork, &score, TIER1_REGION_PATH_OK, 0, 0,
                                  "successive-call savings admit an equal-cost one-call sibling and a free bypass");
    tier1_spill_region_test_witness(test, &fork, fork_blocks, fork_edges, TIER1_EMIT_OK, true, 0,
                                    "the multi-root spill proposal has a positive complete path without requiring every exit to improve");
    fork_edges[2] = -1;
    tier1_spill_region_test_proof(test, &fork, &score, TIER1_REGION_PATH_LOSS, 1, 0,
                                  "a new spill store on the call-free bypass rejects the whole proposal");
    tier1_spill_region_test_witness(test, &fork, fork_blocks, fork_edges, TIER1_EMIT_OK, true, 0,
                                    "a profitable walk alone cannot replace the separate all-path nonincrease proof");
    fork_edges[2] = 0;

    // A positive backedge has the explicit finite witness 0,1,1,2; the direct 0,1,2 path stays equal.
    const uint32_t loop_offsets[] = { 0, 1, 3, 3 };
    const uint32_t loop_targets[] = { 1, 1, 2 };
    const uint32_t loop_predecessor_offsets[] = { 0, 0, 2, 3 };
    const uint32_t loop_predecessors[] = { 0, 1, 1 };
    const Tier1CfgInput loop = {
        .successor_offsets = loop_offsets,
        .successors = loop_targets,
        .predecessor_offsets = loop_predecessor_offsets,
        .predecessors = loop_predecessors,
        .block_count = 3,
        .edge_count = 3,
    };
    int64_t loop_blocks[] = { 0, 0, 0 };
    int64_t loop_edges[] = { 0, 1, 0 };
    score = (Tier1EmitSpillScore){
        .records = { .block_gains = loop_blocks, .edge_gains = loop_edges, .total_edge_gains = loop_edges },
        .memory_edges = loop_edges,
    };
    tier1_spill_region_test_proof(test, &loop, &score, TIER1_REGION_PATH_OK, 0, 0,
                                  "a profitable loop with an equal direct exit has no losing path");
    tier1_spill_region_test_witness(test, &loop, loop_blocks, loop_edges, TIER1_EMIT_OK, true, 0,
                                    "a positive backedge with a terminal suffix supplies a finite real-memory witness");
    loop_edges[1] = 0;
    tier1_spill_region_test_witness(test, &loop, loop_blocks, loop_edges, TIER1_EMIT_OK, false, 0,
                                    "a neutral loop cannot manufacture strict spill profitability");
    loop_edges[1] = -1;
    tier1_spill_region_test_proof(test, &loop, &score, TIER1_REGION_PATH_CYCLE, 1, 0,
                                  "a losing backedge rejects a spill proposal even when its shortest exit is equal");

    // The profitable self-loop is reachable but has no route to a terminal. It is not a complete-path witness.
    const uint32_t trapped_offsets[] = { 0, 2, 3, 4, 4 };
    const uint32_t trapped_targets[] = { 1, 2, 1, 3 };
    const uint32_t trapped_predecessor_offsets[] = { 0, 0, 2, 3, 4 };
    const uint32_t trapped_predecessors[] = { 0, 1, 0, 2 };
    const Tier1CfgInput trapped = {
        .successor_offsets = trapped_offsets,
        .successors = trapped_targets,
        .predecessor_offsets = trapped_predecessor_offsets,
        .predecessors = trapped_predecessors,
        .block_count = 4,
        .edge_count = 4,
    };
    int64_t trapped_blocks[] = { 0, 0, 0, 0 };
    int64_t trapped_edges[] = { 0, 0, 1, 0 };
    tier1_spill_region_test_witness(test, &trapped, trapped_blocks, trapped_edges, TIER1_EMIT_OK, false, 0,
                                    "a positive reachable cycle without an exit cannot establish complete-path improvement");

    const uint32_t separate_offsets[] = { 0, 1, 1, 2, 2 };
    const uint32_t separate_targets[] = { 1, 3 };
    const uint32_t separate_predecessor_offsets[] = { 0, 0, 1, 1, 2 };
    const uint32_t separate_predecessors[] = { 0, 2 };
    const Tier1CfgInput separate = {
        .successor_offsets = separate_offsets,
        .successors = separate_targets,
        .predecessor_offsets = separate_predecessor_offsets,
        .predecessors = separate_predecessors,
        .block_count = 4,
        .edge_count = 2,
    };
    int64_t separate_blocks[] = { 0, 0, 5, 0 };
    int64_t separate_edges[] = { 0, 0 };
    tier1_spill_region_test_witness(test, &separate, separate_blocks, separate_edges, TIER1_EMIT_OK, false, 0,
                                    "an unreachable profitable component cannot qualify a spill proposal");

    chain_blocks[0] = INT64_MAX;
    chain_blocks[1] = 1;
    score = (Tier1EmitSpillScore){
        .records = { .block_gains = chain_blocks, .edge_gains = transport, .total_edge_gains = total },
        .memory_edges = memory,
    };
    tier1_spill_region_test_witness(test, &chain, chain_blocks, memory, TIER1_EMIT_UNSUPPORTED, true, 0,
                                    "overflow refuses strict spill profitability without publishing a witness");
    tier1_spill_region_test_proof(test, &chain, &score, TIER1_REGION_PATH_OVERFLOW, 1, 0,
                                  "overflow cannot pass the all-path real-memory proof");
    chain_blocks[0] = 0;
    chain_blocks[1] = 0;
    tier1_spill_region_test_witness(test, &chain, chain_blocks, memory, TIER1_EMIT_LIMIT, true,
                                    BUSTER_TIER1_RECOLOR_ACTIVE_WORK,
                                    "work exhaustion refuses a spill witness and restores its scratch arena");
    tier1_spill_region_test_proof(test, &chain, &score, TIER1_REGION_PATH_LIMIT, 1,
                                  BUSTER_TIER1_RECOLOR_ACTIVE_WORK,
                                  "work exhaustion refuses the first path metric without leaving scratch state");
    tier1_emit_test_expect(test, test->output.length == output_mark && test->scratch.length == scratch_mark,
                          "spill path and witness fixtures leave both fixture arenas unchanged");
}
