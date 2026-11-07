// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1216: allocation regions follow straight CFG chains. A cold sibling's pressure or clobbers must not dictate
// the entry state of a hot handler just because it precedes that handler in layout. Included by tier1_cfg_test.c.

static bool tier1_cfg_test_region_edge_registers(const Tier1CfgInput* input, const Tier1CfgAllocation* result, uint32_t from, uint32_t to)
{
    bool found = false;
    bool registers = true;
    for (uint32_t e = input->successor_offsets[from]; e < input->successor_offsets[from + 1]; e += 1)
    {
        if (input->successors[e] == to)
        {
            found = true;
            for (uint32_t m = result->edge_move_offsets[e]; m < result->edge_move_offsets[e + 1]; m += 1)
            {
                registers = registers && result->move_sources[m] < BUSTER_TIER1_CFG_SPILL && result->move_destinations[m] < BUSTER_TIER1_CFG_SPILL;
            }
        }
    }
    return found && registers;
}

static bool tier1_cfg_test_region_spilled(const Tier1CfgInput* input, const Tier1CfgAllocation* result, uint32_t value, uint32_t block)
{
    bool spilled = false;
    const uint32_t start = input->block_starts[block];
    const uint32_t end = input->block_starts[block + 1];
    for (uint32_t i = result->value_interval_offsets[value]; i < result->value_interval_offsets[value + 1]; i += 1)
    {
        for (uint32_t f = result->allocation.fragment_offsets[i]; f < result->allocation.fragment_offsets[i + 1]; f += 1)
        {
            spilled |= result->allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL && result->allocation.starts[f] < end && result->allocation.ends[f] > start;
        }
    }
    return spilled;
}

// Entry 0 -> dispatch 1 -> cold 2 / hot 3; both handlers return to 1. The two carried values fit in registers.
// With pressure, a required temporary in 2 evicts one, but neither 1 -> 3 nor 3 -> 1 visits that temporary.
// Merging by layout alone instead makes 3 inherit 2's spill, so the hot edge stores it and the handler reloads it.
// With a partial clobber in 2, the old merged interval loses that register even at 1, before either branch is taken.
// The phi-free variant also covers a join with several predecessors and a layout predecessor whose sole edge is to it.
static void tier1_cfg_test_region_siblings(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    for (uint8_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        for (uint32_t phis = 0; phis < 2; phis += 1)
        {
            for (uint32_t clobber = 0; clobber < 2; clobber += 1)
            {
                memset(&builder, 0, sizeof(builder));
                builder.block_count = 4;
                builder.instructions[0] = 2;
                builder.instructions[1] = 1;
                builder.instructions[2] = 2;
                builder.instructions[3] = 2;
                builder.scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
                builder.scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
                builder.registers[class] = class == BUSTER_TIER1_ALLOCATE_MASK ? 0x06 : 0x03;
                const uint32_t registers = builder.registers[class];
                tier1_cfg_test_edge(&builder, 0, 1);
                tier1_cfg_test_edge(&builder, 2, 1);
                tier1_cfg_test_edge(&builder, 3, 1);
                tier1_cfg_test_edge(&builder, 1, 2);
                tier1_cfg_test_edge(&builder, 1, 3);
                uint32_t carried[2];
                carried[0] = tier1_cfg_test_value(&builder, 0, 0, class, registers);
                carried[1] = tier1_cfg_test_value(&builder, 0, 1, class, registers);
                tier1_cfg_test_definition_use(&builder, carried[0]);
                tier1_cfg_test_definition_use(&builder, carried[1]);
                if (phis != 0)
                {
                    for (uint32_t v = 0; v < 2; v += 1)
                    {
                        const uint32_t operands[] = {
                            carried[v],
                            builder.value_count,
                            builder.value_count,
                        };
                        carried[v] = tier1_cfg_test_phi(&builder, 1, class, operands);
                    }
                }
                if (clobber != 0)
                {
                    builder.fixed_count = 1;
                    builder.fixed_block[0] = 2;
                    builder.fixed_instruction[0] = 0;
                    builder.fixed_class[0] = class;
                    builder.fixed_register[0] = class == BUSTER_TIER1_ALLOCATE_MASK ? 0x02 : 0x01;
                }
                else
                {
                    const uint32_t temporary = tier1_cfg_test_value(&builder, 2, 0, class, registers);
                    tier1_cfg_test_definition_use(&builder, temporary);
                    tier1_cfg_test_use(&builder, temporary, 2, 1, true);
                }
                tier1_cfg_test_use(&builder, carried[0], 3, 0, true);
                tier1_cfg_test_use(&builder, carried[1], 3, 1, true);
                const uint64_t saved = output->length;
                Tier1CfgInput input;
                const bool built = tier1_cfg_test_build(&builder, output, &input);
                tier1_cfg_test_expect(tally, built, "the sibling-region input builds");
                if (built)
                {
                    const char* name = clobber != 0 ? "a sibling's partial clobber stays in that region" : "a sibling's register pressure stays in that region";
                    tier1_cfg_test_case(tally, &input, output, scratch, name, true, 0, 0, true);
                    for (uint32_t path = 0; path < 2; path += 1)
                    {
                        const uint64_t mark = output->length;
                        Tier1CfgAllocation result = { 0 };
                        const Tier1CfgError error = path == 0 ? tier1_cfg_allocate(&input, output, scratch, &result) : tier1_cfg_allocate_scalar(&input, output, scratch, &result);
                        tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "the sibling regions allocate on both paths");
                        if (error == TIER1_CFG_OK)
                        {
                            bool cold_spilled = false;
                            for (uint32_t v = 0; v < 2; v += 1)
                            {
                                uint32_t location = 0;
                                uint8_t kind = 0;
                                const bool header = tier1_cfg_entry_location(&result, carried[v], input.block_starts[1], &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                                const bool hot = tier1_cfg_entry_location(&result, carried[v], input.block_starts[3], &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                                tier1_cfg_test_expect(tally, header && hot, "dispatch and hot-handler entries keep both carried values in registers");
                                tier1_cfg_test_expect(tally, !tier1_cfg_test_region_spilled(&input, &result, carried[v], 1) && !tier1_cfg_test_region_spilled(&input, &result, carried[v], 3), "the carried values never spill on the hot path");
                                cold_spilled |= tier1_cfg_test_region_spilled(&input, &result, carried[v], 2);
                                const uint32_t interval = tier1_cfg_interval_oracle(&result, carried[v], input.block_starts[1]);
                                tier1_cfg_test_expect(tally, interval != BUSTER_TIER1_CFG_NONE && result.intervals.starts[interval] == input.block_starts[1], "a loop entry does not continue its preheader's interval");
                            }
                            tier1_cfg_test_expect(tally, cold_spilled, "the cold region retains its necessary spill");
                            tier1_cfg_test_expect(tally, tier1_cfg_test_region_edge_registers(&input, &result, 1, 3) && tier1_cfg_test_region_edge_registers(&input, &result, 3, 1), "the hot dispatch and back edge need no spill or reload moves");
                            tier1_cfg_test_expect(tally, result.split_count == 0, "region boundaries use CFG edges, not within-block splits");
                        }
                        arena_pop_to(output, mark);
                    }
                }
                arena_pop_to(output, saved);
            }
        }
    }
}

// Keeping a real single-entry, single-exit chain merged avoids adding edge moves or extra intervals to straight code.
static void tier1_cfg_test_region_chain(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    for (uint8_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        memset(&builder, 0, sizeof(builder));
        builder.block_count = 3;
        builder.instructions[0] = 2;
        builder.instructions[1] = 1;
        builder.instructions[2] = 2;
        builder.scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
        builder.scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
        builder.registers[class] = class == BUSTER_TIER1_ALLOCATE_MASK ? 0x06 : 0x03;
        tier1_cfg_test_edge(&builder, 0, 1);
        tier1_cfg_test_edge(&builder, 1, 2);
        const uint32_t first = tier1_cfg_test_value(&builder, 0, 0, class, builder.registers[class]);
        const uint32_t second = tier1_cfg_test_value(&builder, 0, 1, class, builder.registers[class]);
        tier1_cfg_test_definition_use(&builder, first);
        tier1_cfg_test_definition_use(&builder, second);
        tier1_cfg_test_use(&builder, first, 2, 0, true);
        tier1_cfg_test_use(&builder, second, 2, 1, true);
        const uint64_t saved = output->length;
        Tier1CfgInput input;
        const bool built = tier1_cfg_test_build(&builder, output, &input);
        tier1_cfg_test_expect(tally, built, "the straight-chain input builds");
        if (built)
        {
            tier1_cfg_test_case(tally, &input, output, scratch, "a straight CFG chain stays merged", true, 0, 0, true);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t mark = output->length;
                Tier1CfgAllocation result = { 0 };
                const Tier1CfgError error = path == 0 ? tier1_cfg_allocate(&input, output, scratch, &result) : tier1_cfg_allocate_scalar(&input, output, scratch, &result);
                tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "the straight chain allocates on both paths");
                if (error == TIER1_CFG_OK)
                {
                    tier1_cfg_test_expect(tally, result.value_interval_offsets[first + 1] - result.value_interval_offsets[first] == 1 && result.value_interval_offsets[second + 1] - result.value_interval_offsets[second] == 1, "each value keeps one interval through a straight chain");
                    tier1_cfg_test_expect(tally, result.move_count == 0 && result.allocation.spill_count == 0, "a fitting straight chain needs no edge moves or spill slots");
                }
                arena_pop_to(output, mark);
            }
        }
        arena_pop_to(output, saved);
    }
}

static void tier1_cfg_test_regions(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    tier1_cfg_test_region_siblings(tally, output, scratch);
    tier1_cfg_test_region_chain(tally, output, scratch);
}
