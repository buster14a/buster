// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2574: which recomputable values tier1_emit_cfg_hot_recipes finds read on every iteration of the loops they are live
// across, over CFGs built by tier1_cfg_test.c's builder. Included by tier1_emit_test.c. Each fixture's values have
// recipes and are candidates; the expected answers are written by hand from the CFGs, not by another walk.

// A fixture's answers: the CFG, its candidates, and which of them must be hot.
static void tier1_hot_recipes_check(Tier1EmitTest* test, Tier1CfgTestBuilder* builder, const uint32_t* values, const uint8_t* expected, uint32_t count, const char* name)
{
    for (uint32_t k = 0; k < count; k += 1)
    {
        builder->remat[values[k]] = 1;
    }
    Tier1CfgInput input = { 0 };
    bool ready = tier1_cfg_test_build(builder, &test->input, &input);
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        Tier1CfgOrder order;
        const bool prepared = tier1_cfg_order_prepare(&input, &test->output, &test->scratch, path == 0, &order) == TIER1_CFG_OK;
        uint8_t* hot = prepared ? arena_push(&test->scratch, (uint64_t)input.value_count + 1, 1) : 0;
        bool same = hot != 0;
        if (same)
        {
            memset(hot, 0, (uint64_t)input.value_count + 1);
            for (uint32_t k = 0; k < count; k += 1)
            {
                hot[values[k]] = 2;
            }
            same = tier1_emit_cfg_hot_recipes(&input, &order.prepared.candidate, count, &test->output, &test->scratch, hot);
        }
        for (uint32_t v = 0; same && v < input.value_count; v += 1)
        {
            uint8_t wanted = 0;
            for (uint32_t k = 0; k < count; k += 1)
            {
                wanted = values[k] == v ? expected[k] : wanted;
            }
            same = hot[v] == wanted;
        }
        tier1_emit_test_expect(test, same, name);
        if (prepared)
        {
            tier1_cfg_release(&order.prepared, &test->scratch);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_hot_recipes_tests(Tier1EmitTest* test)
{
    static Tier1CfgTestBuilder builder;
    // A dispatch loop: 0 -> 1, 1 -> 2, 1 -> 3, 2 -> 1, 3 -> 1, 3 -> 4. The table is read at the end of both handlers (2
    // and 3), a header value in the header (1), a frame address in handler 2 alone, an exit value after the loop (4).
    memset(&builder, 0, sizeof(builder));
    builder.block_count = 5;
    for (uint32_t b = 0; b < 5; b += 1)
    {
        builder.instructions[b] = 4;
    }
    tier1_cfg_test_edge(&builder, 0, 1);
    tier1_cfg_test_edge(&builder, 1, 2);
    tier1_cfg_test_edge(&builder, 1, 3);
    tier1_cfg_test_edge(&builder, 2, 1);
    tier1_cfg_test_edge(&builder, 3, 1);
    tier1_cfg_test_edge(&builder, 3, 4);
    builder.registers[0] = 0x00ff;
    builder.scratch[0] = 11;
    const uint32_t table = tier1_cfg_test_value(&builder, 0, 0, 0, 0x00ff);
    const uint32_t header = tier1_cfg_test_value(&builder, 0, 1, 0, 0x00ff);
    const uint32_t frame = tier1_cfg_test_value(&builder, 0, 2, 0, 0x00ff);
    const uint32_t exit = tier1_cfg_test_value(&builder, 0, 3, 0, 0x00ff);
    tier1_cfg_test_use(&builder, table, 2, 3, true);
    tier1_cfg_test_use(&builder, table, 3, 3, true);
    tier1_cfg_test_use(&builder, header, 1, 1, true);
    tier1_cfg_test_use(&builder, frame, 2, 1, true);
    tier1_cfg_test_use(&builder, exit, 4, 0, true);
    const uint32_t dispatch_values[] = { table, header, frame, exit };
    const uint8_t dispatch_hot[] = { 1, 1, 0, 0 };
    tier1_hot_recipes_check(test, &builder, dispatch_values, dispatch_hot, 4, "a dispatch loop's table and its header's value are hot; a handler's own address and a value read after the loop are not");
    // An inner loop inside an outer one: 0 -> 1, 1 -> 2, 2 -> 3, 3 -> 2, 2 -> 4, 4 -> 1, 1 -> 5. The value is read on
    // every turn of the inner loop (3) but the outer loop's turn 1 -> 2 -> 4 -> 1 reads it nowhere: it is live across the
    // outer loop, whose iterations don't all read it, so its registers, chosen for its whole range, stay as they were.
    // The second value is defined in the outer loop's body and read in the inner loop's: its outermost loop is the inner.
    memset(&builder, 0, sizeof(builder));
    builder.block_count = 6;
    for (uint32_t b = 0; b < 6; b += 1)
    {
        builder.instructions[b] = 4;
    }
    tier1_cfg_test_edge(&builder, 0, 1);
    tier1_cfg_test_edge(&builder, 1, 2);
    tier1_cfg_test_edge(&builder, 1, 5);
    tier1_cfg_test_edge(&builder, 2, 3);
    tier1_cfg_test_edge(&builder, 2, 4);
    tier1_cfg_test_edge(&builder, 3, 2);
    tier1_cfg_test_edge(&builder, 4, 1);
    builder.registers[0] = 0x00ff;
    builder.scratch[0] = 11;
    const uint32_t outer = tier1_cfg_test_value(&builder, 0, 0, 0, 0x00ff);
    const uint32_t inner = tier1_cfg_test_value(&builder, 1, 0, 0, 0x00ff);
    tier1_cfg_test_use(&builder, outer, 3, 0, true);
    tier1_cfg_test_use(&builder, inner, 3, 1, true);
    const uint32_t nested_values[] = { outer, inner };
    const uint8_t nested_hot[] = { 0, 1 };
    tier1_hot_recipes_check(test, &builder, nested_values, nested_hot, 2, "a value read on every turn of an inner loop but live across an outer one isn't hot; one live across the inner loop alone is");
    // No loop: a value read in both arms of a diamond is live across no loop's header.
    memset(&builder, 0, sizeof(builder));
    builder.block_count = 4;
    for (uint32_t b = 0; b < 4; b += 1)
    {
        builder.instructions[b] = 2;
    }
    tier1_cfg_test_edge(&builder, 0, 1);
    tier1_cfg_test_edge(&builder, 0, 2);
    tier1_cfg_test_edge(&builder, 1, 3);
    tier1_cfg_test_edge(&builder, 2, 3);
    builder.registers[0] = 0x00ff;
    builder.scratch[0] = 11;
    const uint32_t straight = tier1_cfg_test_value(&builder, 0, 0, 0, 0x00ff);
    tier1_cfg_test_use(&builder, straight, 1, 0, true);
    tier1_cfg_test_use(&builder, straight, 2, 0, true);
    const uint32_t straight_values[] = { straight };
    const uint8_t straight_hot[] = { 0 };
    tier1_hot_recipes_check(test, &builder, straight_values, straight_hot, 1, "a value live across no loop isn't hot");
}
