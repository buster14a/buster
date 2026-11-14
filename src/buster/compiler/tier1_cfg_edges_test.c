// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original CFG edge-contract regressions for #1356. These construct both CSR views independently, including
// unordered rows, duplicates and a wide dispatch/join, without the small random builder's degree limits. And #1399's
// use checks: validation through the position column against validation by search, over every placement of a value's
// definition and two uses in a small CFG.

static void tier1_cfg_test_edge_views(Tier1CfgTestTally* tally, Arena* scratch)
{
    const uint64_t saved = scratch->length;
    uint32_t starts[] = { 0, 1, 2, 3, 4, 5, 6 };
    uint32_t successor_offsets[] = { 0, 3, 4, 5, 6, 6, 6 };
    uint32_t successors[] = { 3, 1, 2, 4, 4, 4 };
    uint32_t predecessor_offsets[] = { 0, 0, 1, 2, 3, 6, 6 };
    uint32_t predecessors[] = { 0, 0, 0, 3, 1, 2 };
    uint32_t phis[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint32_t entry_edges[6];
    Tier1CfgInput input =
    {
        .block_starts = starts,
        .successor_offsets = successor_offsets,
        .successors = successors,
        .predecessor_offsets = predecessor_offsets,
        .predecessors = predecessors,
        .phi_offsets = phis,
        .phi_operand_offsets = phis,
        .block_count = 6,
        .edge_count = 6,
        .position_count = 6,
    };
    tier1_cfg_test_expect(tally, tier1_cfg_input_valid(&input, scratch), "unordered successor and predecessor rows describe the same edge set");
    tier1_cfg_single_predecessor_edges(&input, entry_edges);
    tier1_cfg_test_expect(tally, entry_edges[0] == BUSTER_TIER1_CFG_NONE && entry_edges[1] == 1 && entry_edges[2] == 2 && entry_edges[3] == 0 && entry_edges[4] == BUSTER_TIER1_CFG_NONE && entry_edges[5] == BUSTER_TIER1_CFG_NONE, "sole incoming edge indices preserve the successor order and exclude joins and unreachable blocks");
    predecessors[5] = 1;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "a repeated predecessor cannot hide a missing predecessor with the same row count");
    predecessors[5] = 5;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "a predecessor must have the matching successor");
    predecessors[5] = 6;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "an out-of-range predecessor is refused");
    predecessors[5] = 2;
    successors[0] = 6;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "an out-of-range successor is refused");
    successors[0] = 0;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "a successor cannot add an incoming edge to the entry");
    successors[0] = 3;
    successor_offsets[1] = 7;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "an intermediate successor offset beyond the edge count is refused before reading edges");
    successor_offsets[1] = 3;
    predecessor_offsets[2] = 7;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "an intermediate predecessor offset beyond the edge count is refused before reading edges");
    predecessor_offsets[2] = 1;
    tier1_cfg_test_expect(tally, tier1_cfg_input_valid(&input, scratch), "restoring both CSR views restores the valid graph");

    // Duplicate successor edges with matching duplicate predecessor entries still describe an invalid multigraph.
    uint32_t duplicate_successor_offsets[] = { 0, 2, 2, 2 };
    uint32_t duplicate_successors[] = { 1, 1 };
    uint32_t duplicate_predecessor_offsets[] = { 0, 0, 2, 2 };
    uint32_t duplicate_predecessors[] = { 0, 0 };
    input.block_count = 3;
    input.position_count = 3;
    input.edge_count = 2;
    input.successor_offsets = duplicate_successor_offsets;
    input.successors = duplicate_successors;
    input.predecessor_offsets = duplicate_predecessor_offsets;
    input.predecessors = duplicate_predecessors;
    tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "duplicate edges in both views are refused");

    // A non-entry self edge is legal, including a disconnected block with itself as its sole predecessor.
    duplicate_successors[1] = 2;
    duplicate_successor_offsets[1] = 1;
    duplicate_successor_offsets[2] = 1;
    duplicate_predecessor_offsets[2] = 1;
    duplicate_predecessors[1] = 2;
    tier1_cfg_test_expect(tally, tier1_cfg_input_valid(&input, scratch), "a non-entry self edge is accepted");
    tier1_cfg_single_predecessor_edges(&input, entry_edges);
    tier1_cfg_test_expect(tally, entry_edges[1] == 0 && entry_edges[2] == 1, "a self edge has its exact successor index");

    const uint32_t empty_offsets[] = { 0, 0 };
    input.block_count = 1;
    input.position_count = 1;
    input.edge_count = 0;
    input.successor_offsets = empty_offsets;
    input.predecessor_offsets = empty_offsets;
    input.successors = 0;
    input.predecessors = 0;
    tier1_cfg_test_expect(tally, tier1_cfg_input_valid(&input, scratch), "an edgeless graph needs no edge arrays");
    tier1_cfg_single_predecessor_edges(&input, entry_edges);
    tier1_cfg_test_expect(tally, entry_edges[0] == BUSTER_TIER1_CFG_NONE, "an edgeless graph has no incoming edge");
    tier1_cfg_test_expect(tally, scratch->length == saved, "edge validation restores scratch after valid and invalid inputs");
}

static void tier1_cfg_test_wide_edges(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    const uint64_t saved = output->length;
    const uint64_t scratch_saved = scratch->length;
    const uint32_t width = 16384;
    const uint32_t blocks = width + 2;
    const uint32_t edges = width * 2;
    uint32_t* starts = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* successor_offsets = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* successors = arena_push_array(output, edges, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* predecessor_offsets = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* predecessors = arena_push_array(output, edges, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* phis = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* entry_edges = arena_push_array(output, blocks, sizeof(uint32_t), _Alignof(uint32_t));
    const bool built = starts != 0 && successor_offsets != 0 && successors != 0 && predecessor_offsets != 0 && predecessors != 0 && phis != 0 && entry_edges != 0;
    tier1_cfg_test_expect(tally, built, "wide dispatch and join fixture memory");
    if (built)
    {
        for (uint32_t b = 0; b <= blocks; b += 1)
        {
            starts[b] = b;
            phis[b] = 0;
            successor_offsets[b] = b == 0 ? 0 : b <= width + 1 ? width + b - 1 : edges;
            predecessor_offsets[b] = b <= 1 ? 0 : b <= width + 1 ? b - 1 : edges;
        }
        for (uint32_t j = 0; j < width; j += 1)
        {
            successors[j] = width - j;
            successors[width + j] = width + 1;
            predecessors[j] = 0;
            predecessors[width + j] = width - j;
        }
        const Tier1CfgInput input =
        {
            .block_starts = starts,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
            .phi_offsets = phis,
            .phi_operand_offsets = phis,
            .block_count = blocks,
            .edge_count = edges,
            .position_count = blocks,
        };
        tier1_cfg_test_expect(tally, tier1_cfg_input_valid(&input, scratch), "wide reverse-ordered dispatch and join validate");
        tier1_cfg_single_predecessor_edges(&input, entry_edges);
        bool mapped = entry_edges[0] == BUSTER_TIER1_CFG_NONE && entry_edges[width + 1] == BUSTER_TIER1_CFG_NONE;
        for (uint32_t b = 1; b <= width; b += 1)
        {
            mapped = mapped && entry_edges[b] == width - b;
        }
        tier1_cfg_test_expect(tally, mapped, "every wide dispatch successor maps to its incoming edge");
        predecessors[edges - 1] = predecessors[edges - 2];
        tier1_cfg_test_expect(tally, !tier1_cfg_input_valid(&input, scratch), "a duplicate at the end of a wide join is refused");
    }
    tier1_cfg_test_expect(tally, scratch->length == scratch_saved, "wide edge validation restores scratch");
    arena_pop_to(output, saved);
}

// Every definition position and every pair of use positions (one past the last position included) of one value in four
// blocks: the column's checks (tier1_cfg_input_valid_into) and the search's (tier1_cfg_input_valid) accept exactly the
// inputs whose uses are sorted, in range and none before the definition in its own block, from a block found by a
// linear scan here, and the column holds each position's block.
static void tier1_cfg_test_use_blocks(Tier1CfgTestTally* tally, Arena* scratch)
{
    const uint64_t saved = scratch->length;
    uint32_t starts[] = { 0, 3, 5, 9, 10 };
    uint32_t successor_offsets[] = { 0, 1, 2, 3, 3 };
    uint32_t successors[] = { 1, 2, 3 };
    uint32_t predecessor_offsets[] = { 0, 0, 1, 2, 3 };
    uint32_t predecessors[] = { 0, 1, 2 };
    uint32_t phis[] = { 0, 0, 0, 0, 0 };
    uint8_t classes[] = { BUSTER_TIER1_ALLOCATE_GPR };
    uint32_t allowed[] = { 0 };
    uint32_t definitions[] = { 0 };
    uint32_t use_offsets[] = { 0, 2 };
    uint32_t use_positions[] = { 0, 0 };
    uint32_t column[10];
    const Tier1CfgInput input =
    {
        .block_starts = starts,
        .successor_offsets = successor_offsets,
        .successors = successors,
        .predecessor_offsets = predecessor_offsets,
        .predecessors = predecessors,
        .phi_offsets = phis,
        .phi_operand_offsets = phis,
        .classes = classes,
        .allowed_registers = allowed,
        .definitions = definitions,
        .use_offsets = use_offsets,
        .use_positions = use_positions,
        .block_count = 4,
        .value_count = 1,
        .use_count = 2,
        .edge_count = 3,
        .position_count = 10,
    };
    uint32_t cases = 0;
    uint32_t agreed = 0;
    uint32_t accepted = 0;
    bool labeled = true;
    for (uint32_t d = 0; d < 10; d += 1)
    {
        for (uint32_t first = 0; first <= 10; first += 1)
        {
            for (uint32_t second = 0; second <= 10; second += 1)
            {
                definitions[0] = d;
                use_positions[0] = first;
                use_positions[1] = second;
                uint32_t home = 0;
                while (starts[home + 1] <= d)
                {
                    home += 1;
                }
                bool expected = first <= second && second < 10;
                for (uint32_t u = 0; expected && u < 2; u += 1)
                {
                    const uint32_t p = use_positions[u];
                    expected = !(starts[home] <= p && p < starts[home + 1]) || p >= d;
                }
                memset(column, 0xff, sizeof(column));
                const bool searched = tier1_cfg_input_valid(&input, scratch);
                const bool mapped = tier1_cfg_input_valid_into(&input, scratch, column);
                for (uint32_t q = 0; q < 10; q += 1)
                {
                    uint32_t b = 0;
                    while (starts[b + 1] <= q)
                    {
                        b += 1;
                    }
                    labeled = labeled && column[q] == b;
                }
                cases += 1;
                agreed += searched == expected && mapped == expected;
                accepted += expected;
            }
        }
    }
    tier1_cfg_test_expect(tally, agreed == cases && accepted != 0 && accepted != cases, "validation by the position column and by search agree on every use placement");
    tier1_cfg_test_expect(tally, labeled, "validation labels every position with its block");
    tier1_cfg_test_expect(tally, scratch->length == saved, "use validation restores scratch");
}
