// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1216: current spill slots on CFG edges. Included by tier1_cfg_test.c after its helpers.

// Earlier layout blocks do not establish a slot. A reload at a continuing block entry does: every incoming edge
// must supply that slot. A new interval at the same position must not inherit the earlier interval's proof.
static void tier1_cfg_test_spill_prefix(Tier1CfgTestTally* tally)
{
    uint32_t value_offsets[] = { 0, 3, 4, 5 };
    uint32_t starts[] = { 1, 4, 10, 1, 1 };
    uint32_t ends[] = { 4, 10, 12, 12, 12 };
    uint32_t offsets[] = { 0, 2, 5, 6, 8, 9 };
    uint32_t fragment_starts[] = { 1, 2, 4, 6, 8, 10, 1, 6, 1 };
    uint32_t fragment_ends[] = { 2, 4, 6, 8, 10, 12, 6, 12, 12 };
    uint32_t spill_values[] = { 1, 0 };
    uint32_t spill_ids[] = { 1, 0, UINT32_MAX };
    uint8_t kinds[] = {
        BUSTER_TIER1_ALLOCATE_REGISTER,
        BUSTER_TIER1_ALLOCATE_SPILL,
        BUSTER_TIER1_ALLOCATE_REGISTER,
        BUSTER_TIER1_ALLOCATE_SPILL,
        BUSTER_TIER1_ALLOCATE_REGISTER,
        BUSTER_TIER1_ALLOCATE_REGISTER,
        BUSTER_TIER1_ALLOCATE_REGISTER,
        BUSTER_TIER1_ALLOCATE_SPILL,
        BUSTER_TIER1_ALLOCATE_REGISTER,
    };
    const Tier1CfgAllocation result = {
        .intervals = {
            .starts = starts,
            .ends = ends,
            .count = 5,
            .value_count = 3,
        },
        .allocation = {
            .fragment_offsets = offsets,
            .starts = fragment_starts,
            .ends = fragment_ends,
            .kinds = kinds,
            .spill_ids = spill_ids,
            .spill_values = spill_values,
            .spill_count = 2,
            .fragment_count = 9,
        },
        .value_interval_offsets = value_offsets,
    };
    uint32_t prefix[] = { 0, 0, 0, 0, 0, 0, 0, 0, UINT32_MAX };
    tier1_cfg_spill_prefix(&result, prefix);
    const uint32_t expected[] = { 0, 4, 4, 8, 8, 8, 0, 12, UINT32_MAX };
    tier1_cfg_test_expect(tally, memcmp(prefix, expected, sizeof(expected)) == 0, "spill history crosses pieces without visiting register-only values");
    tier1_cfg_test_expect(tally, !tier1_cfg_slot_current(&result, prefix, 0, 1, 0), "a value not yet spilled has no current slot");
    tier1_cfg_test_expect(tally, tier1_cfg_slot_current(&result, prefix, 0, 5, 0), "a reloaded piece retains its block's earlier spill");
    tier1_cfg_test_expect(tally, !tier1_cfg_slot_current(&result, prefix, 0, 5, 4), "a new interval does not inherit a spill ending at its block's entry");
    tier1_cfg_test_expect(tally, tier1_cfg_slot_current(&result, prefix, 0, 9, 8), "a continuing interval's entry reload establishes the slot");
    tier1_cfg_test_expect(tally, tier1_cfg_slot_current(&result, prefix, 0, 11, 6), "a later piece retains a spill in the same block");
    tier1_cfg_test_expect(tally, !tier1_cfg_slot_current(&result, prefix, 0, 11, 10), "a spill in an earlier layout block proves nothing");
    tier1_cfg_test_expect(tally, !tier1_cfg_slot_current(&result, prefix, 1, 5, 0), "another value's spill proves nothing");
    tier1_cfg_test_expect(tally, !tier1_cfg_slot_current(&result, prefix, 0, 0, 0) && !tier1_cfg_slot_current(&result, prefix, 0, 12, 0), "positions outside the value's intervals have no current slot");
}

// A phi starts in memory because the header's first position blocks its only register, then reloads at a split.
// Its self edge needs no store. A different operand must still store, even when the old phi has a current slot:
// an exit-block use keeps it live while the new operand takes the register. The back edge is critical.
static void tier1_cfg_test_spill_edges(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    const uint32_t block_starts[] = { 0, 4, 10, 12 };
    const uint32_t successor_offsets[] = { 0, 1, 3, 3 };
    const uint32_t successors[] = { 1, 1, 2 };
    const uint32_t predecessor_offsets[] = { 0, 0, 2, 3 };
    const uint32_t predecessors[] = { 0, 1, 1 };
    const uint32_t definitions[] = { 1, 4, 9 };
    const uint32_t use_offsets[] = { 0, 1, 3, 4 };
    const uint32_t use_positions[] = { 1, 6, 10, 9 };
    const uint8_t use_required[] = { 1, 1, 1, 1 };
    const uint32_t phi_offsets[] = { 0, 0, 1, 1 };
    const uint32_t phi_values[] = { 1 };
    const uint32_t phi_operand_offsets[] = { 0, 2 };
    const uint32_t fixed_starts[] = { 4 };
    const uint32_t fixed_ends[] = { 5 };
    for (uint8_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        const uint32_t mask = class == BUSTER_TIER1_ALLOCATE_MASK ? 2 : 1;
        const uint8_t classes[] = { class, class, class };
        const uint32_t allowed[] = { mask, mask, mask };
        for (uint32_t changed = 0; changed < 2; changed += 1)
        {
            const uint32_t operands[] = { 0, changed != 0 ? 2 : 1 };
            Tier1CfgInput input = {
                .block_starts = block_starts,
                .successor_offsets = successor_offsets,
                .successors = successors,
                .predecessor_offsets = predecessor_offsets,
                .predecessors = predecessors,
                .classes = classes,
                .allowed_registers = allowed,
                .definitions = definitions,
                .use_offsets = use_offsets,
                .use_positions = use_positions,
                .use_required = use_required,
                .phi_offsets = phi_offsets,
                .phi_values = phi_values,
                .phi_operand_offsets = phi_operand_offsets,
                .phi_operands = operands,
                .block_count = 3,
                .edge_count = 3,
                .value_count = 2 + changed,
                .use_count = 3 + changed,
                .phi_count = 1,
                .position_count = 12,
                .scratch_registers = { 11, 15, 0 },
                .fixed = {
                    .starts = fixed_starts,
                    .ends = fixed_ends,
                    .registers = &mask,
                    .classes = &class,
                    .count = 1,
                },
            };
            input.registers[class] = mask;
            tier1_cfg_test_case(tally, &input, output, scratch, "current slot on a critical loop edge", false, 0, 1, true);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t saved = output->length;
                Tier1CfgAllocation result = { 0 };
                const Tier1CfgError error = path == 0 ? tier1_cfg_allocate(&input, output, scratch, &result) : tier1_cfg_allocate_scalar(&input, output, scratch, &result);
                tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "the current-slot loop allocates on both paths");
                if (error == TIER1_CFG_OK)
                {
                    const uint32_t first = result.edge_move_offsets[1];
                    const uint32_t last = result.edge_move_offsets[2];
                    tier1_cfg_test_expect(tally, last - first == changed, "only a changed phi operand stores on the back edge");
                    tier1_cfg_test_expect(tally, result.edge_placements[1] == (changed != 0 ? TIER1_CFG_EDGE_SPLIT : TIER1_CFG_EDGE_EMPTY), "eliding the last move removes the critical edge's split");
                    tier1_cfg_test_expect(tally, result.edge_move_offsets[1] > result.edge_move_offsets[0], "the entry edge still initializes the phi slot");
                    if (changed != 0 && last - first == 1)
                    {
                        // Replace the required store with a register self-copy. Its shape is valid, but the phi's
                        // slot retains the old value instead of this iteration's new operand.
                        const uint32_t destination = result.move_destinations[first];
                        tier1_cfg_test_expect(tally, destination == BUSTER_TIER1_CFG_SPILL + result.allocation.spill_ids[1] && result.move_values[first] == 2, "the changed phi writes its own slot with the new operand");
                        result.move_destinations[first] = result.move_sources[first];
                        Tier1CfgCheck check;
                        tier1_cfg_test_expect(tally, !tier1_cfg_verify(&input, &result, scratch, &check), "the verifier rejects an omitted changed-phi store");
                        tier1_cfg_test_expect(tally, !tier1_cfg_test_certify(tally, &input, &result, output, scratch, false), "the certificate rejects an omitted changed-phi store");
                        result.move_destinations[first] = destination;
                    }
                }
                arena_pop_to(output, saved);
            }
        }
    }
}

// The same optimization without fixed ranges or split pieces: two phis compete for one register. The second is
// needed immediately; the first spills, reloads for its later use, and feeds both phis on the back edge. Its slot
// already contains it, and the other phi takes the register it leaves in, so the whole back edge is empty.
static void tier1_cfg_test_spill_pressure(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    for (uint8_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        memset(&builder, 0, sizeof(builder));
        builder.block_count = 3;
        builder.instructions[0] = 2;
        builder.instructions[1] = 3;
        builder.instructions[2] = 1;
        builder.scratch[0] = 11;
        builder.scratch[1] = 15;
        builder.registers[class] = class == BUSTER_TIER1_ALLOCATE_MASK ? 2 : 1;
        tier1_cfg_test_edge(&builder, 0, 1);
        tier1_cfg_test_edge(&builder, 1, 1);
        tier1_cfg_test_edge(&builder, 1, 2);
        const uint32_t a = tier1_cfg_test_value(&builder, 0, 0, class, builder.registers[class]);
        const uint32_t b = tier1_cfg_test_value(&builder, 0, 1, class, builder.registers[class]);
        tier1_cfg_test_definition_use(&builder, a);
        tier1_cfg_test_definition_use(&builder, b);
        const uint32_t first_operands[] = { a, builder.value_count };
        const uint32_t first = tier1_cfg_test_phi(&builder, 1, class, first_operands);
        const uint32_t second_operands[] = { b, first };
        const uint32_t second = tier1_cfg_test_phi(&builder, 1, class, second_operands);
        tier1_cfg_test_use(&builder, second, 1, 0, true);
        tier1_cfg_test_use(&builder, first, 1, 1, true);
        tier1_cfg_test_use(&builder, first, 2, 0, true);
        const uint64_t saved = output->length;
        Tier1CfgInput input;
        const bool built = tier1_cfg_test_build(&builder, output, &input);
        tier1_cfg_test_expect(tally, built, "the spill-pressure loop builds");
        if (built)
        {
            tier1_cfg_test_case(tally, &input, output, scratch, "current slot under register pressure", false, 0, 0, true);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t mark = output->length;
                Tier1CfgAllocation result = { 0 };
                const Tier1CfgError error = path == 0 ? tier1_cfg_allocate(&input, output, scratch, &result) : tier1_cfg_allocate_scalar(&input, output, scratch, &result);
                tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "the spill-pressure loop allocates on both paths");
                if (error == TIER1_CFG_OK)
                {
                    uint32_t location = 0;
                    uint8_t kind = 0;
                    const bool spilled = tier1_cfg_entry_location(&result, first, input.block_starts[1], &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_SPILL;
                    const bool reloaded = tier1_cfg_location(&result, first, input.block_starts[2] - 1, &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                    tier1_cfg_test_expect(tally, spilled && reloaded, "pressure spills the first phi at entry and reloads it before exit");
                    tier1_cfg_test_expect(tally, result.edge_move_offsets[1] == result.edge_move_offsets[2] && result.edge_placements[1] == TIER1_CFG_EDGE_EMPTY, "the reloaded self-phi needs no back-edge store under pressure");
                    tier1_cfg_test_expect(tally, result.split_count == 0, "the pressure case uses fragment transitions, not split pieces");
                }
                arena_pop_to(output, mark);
            }
        }
        arena_pop_to(output, saved);
    }
}

static void tier1_cfg_test_current_slots(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    tier1_cfg_test_spill_prefix(tally);
    tier1_cfg_test_spill_edges(tally, output, scratch);
    tier1_cfg_test_spill_pressure(tally, output, scratch);
}
