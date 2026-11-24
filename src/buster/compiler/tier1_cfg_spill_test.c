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

// A phi starts in memory because the header's first position blocks its only register; its piece after the split
// waits in the slot until its use reloads it (#2768). Its self edge needs no store. A different operand must still store, even when the old phi has a current slot:
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
            tier1_cfg_test_case(tally, &input, output, scratch, "current slot on a critical loop edge", false, 0, 0, true);
            const uint64_t saved = output->length;
            Tier1CfgAllocation result = { 0 };
            const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
            tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "the current-slot loop allocates");
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
            const uint64_t mark = output->length;
            Tier1CfgAllocation result = { 0 };
            const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
            tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "the spill-pressure loop allocates");
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
        arena_pop_to(output, saved);
    }
}

// Both checkers on the result with its current slots edited (tier1_cfg_test_slots_edited): whether both refuse it.
static bool tier1_cfg_test_slots_refused(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Tier1CfgAllocation* result, uint32_t block, uint32_t value, bool add, Arena* output, Arena* scratch)
{
    const uint64_t saved = scratch->length;
    const uint32_t blocks = input->block_count;
    uint32_t* offsets = arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* slots = arena_push_array(scratch, (uint64_t)result->current_slot_offsets[blocks] + 2, sizeof(uint32_t), _Alignof(uint32_t));
    bool refused = false;
    if (offsets != 0 && slots != 0)
    {
        tier1_cfg_test_slots_edited(result, blocks, block, value, add, offsets, slots);
        uint32_t* const kept_offsets = result->current_slot_offsets;
        uint32_t* const kept_slots = result->current_slots;
        result->current_slot_offsets = offsets;
        result->current_slots = slots;
        Tier1CfgCheck check;
        const bool verified = tier1_cfg_verify(input, result, scratch, &check);
        const bool certified = tier1_cfg_test_certify(tally, input, result, output, scratch, false);
        refused = !verified && !certified;
        result->current_slot_offsets = kept_offsets;
        result->current_slots = kept_slots;
    }
    arena_pop_to(scratch, saved);
    return refused;
}

// #2553: a slot current across blocks. With one register, `a` is stored where `b` is defined, and the loop's header,
// which reads `b` and then `a`, takes `a` in its slot and reloads it, storing `b`; the body continues the header's
// ranges, reading `a` in its register. The back edge returns `a` to the header's place for it, its slot, which still
// holds it: every path to the body went through the header's start with `a` there. The predecessor's own proof
// (tier1_cfg_slot_current) sees no store of `a` in the body, and before tier1_cfg_current_slots the back edge stored it
// on every iteration. Both checkers refuse the allocation once the body's assertion is taken back (the back edge still
// leaves the store out), and once `b`'s slot, which the entry doesn't store, is asserted current at the header.
static void tier1_cfg_test_spill_across(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    for (uint8_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        memset(&builder, 0, sizeof(builder));
        builder.block_count = 4;
        builder.instructions[0] = 2;
        builder.instructions[1] = 2;
        builder.instructions[2] = 1;
        builder.instructions[3] = 1;
        builder.scratch[0] = 11;
        builder.scratch[1] = 15;
        builder.registers[class] = class == BUSTER_TIER1_ALLOCATE_MASK ? 2 : 1;
        tier1_cfg_test_edge(&builder, 0, 1);
        tier1_cfg_test_edge(&builder, 1, 2);
        tier1_cfg_test_edge(&builder, 1, 3);
        tier1_cfg_test_edge(&builder, 2, 1);
        const uint32_t a = tier1_cfg_test_value(&builder, 0, 0, class, builder.registers[class]);
        const uint32_t b = tier1_cfg_test_value(&builder, 0, 1, class, builder.registers[class]);
        tier1_cfg_test_definition_use(&builder, a);
        tier1_cfg_test_definition_use(&builder, b);
        tier1_cfg_test_use(&builder, b, 1, 0, true);
        tier1_cfg_test_use(&builder, a, 1, 1, true);
        tier1_cfg_test_use(&builder, a, 2, 0, true);
        tier1_cfg_test_use(&builder, a, 3, 0, true);
        const uint64_t saved = output->length;
        Tier1CfgInput input;
        const bool built = tier1_cfg_test_build(&builder, output, &input);
        tier1_cfg_test_expect(tally, built, "the slot-across-blocks loop builds");
        if (built)
        {
            tier1_cfg_test_case(tally, &input, output, scratch, "a slot current across blocks", false, 0, 0, true);
            const uint64_t mark = output->length;
            const uint64_t scratch_mark = scratch->length;
            Tier1CfgAllocation result = { 0 };
            const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
            tier1_cfg_test_expect(tally, error == TIER1_CFG_OK && result.current_slot_offsets != 0, "the slot-across-blocks loop allocates, with current slots");
            uint32_t* spill_ends = error == TIER1_CFG_OK ? arena_push_array(scratch, (uint64_t)result.allocation.fragment_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
            if (error == TIER1_CFG_OK && result.current_slot_offsets != 0 && spill_ends != 0)
            {
                uint32_t location = 0;
                uint8_t kind = 0;
                const uint32_t exit = input.block_starts[3] - 1;
                const bool header = tier1_cfg_entry_location(&result, a, input.block_starts[1], &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_SPILL;
                const bool body = tier1_cfg_location(&result, a, input.block_starts[2], &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                const bool leaving = tier1_cfg_location(&result, a, exit, &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                tier1_cfg_test_expect(tally, header && body && leaving, "`a` enters the header in its slot and the body in its register, and leaves it there");
                tier1_cfg_spill_prefix(&result, spill_ends);
                tier1_cfg_test_expect(tally, !tier1_cfg_slot_current(&result, spill_ends, a, exit, input.block_starts[2]), "the body itself doesn't store `a`");
                const uint32_t slot = BUSTER_TIER1_CFG_SPILL + result.allocation.spill_ids[a];
                // The back edge is the successor CSR's last: blocks 0 and 1's edges come before it.
                const uint32_t back = input.edge_count - 1;
                bool stored = false;
                for (uint32_t m = result.edge_move_offsets[back]; m < result.edge_move_offsets[back + 1]; m += 1)
                {
                    stored = stored || result.move_destinations[m] == slot;
                }
                tier1_cfg_test_expect(tally, input.successors[back] == 1 && !stored, "the back edge leaves out `a`'s store into its current slot");
                tier1_cfg_test_expect(tally, tier1_cfg_test_slot_listed(&result, 2, a), "`a`'s slot is current at the body's start");
                tier1_cfg_test_expect(tally, tier1_cfg_test_slots_refused(tally, &input, &result, 2, a, false, output, scratch), "both checkers refuse a store left out without its current slot");
                const bool in_register = tier1_cfg_entry_location(&result, b, input.block_starts[1], &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                tier1_cfg_test_expect(tally, in_register && result.allocation.spill_ids[b] != UINT32_MAX && !tier1_cfg_test_slot_listed(&result, 1, b), "`b` enters the header in its register, its slot not current");
                tier1_cfg_test_expect(tally, tier1_cfg_test_slots_refused(tally, &input, &result, 1, b, true, output, scratch), "both checkers refuse a slot asserted current that no store gives");
            }
            arena_pop_to(scratch, scratch_mark);
            arena_pop_to(output, mark);
        }
        arena_pop_to(output, saved);
    }
}

// Each diamond feeds a two-block loop. The first value spills at home. The second spills on one arm or both:
// only the latter is current at the join and latch. Layout puts the latch before the join and both before the arms,
// so losses must travel forward around the loop. Number the second values after all the first ones to exercise the
// ordering across groups, and give the solve less scratch than a full set of block rows.
static void tier1_cfg_test_slot_groups(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    const uint64_t saved = output->length;
    const uint32_t diamonds = 2049;
    const uint32_t blocks = 5 * diamonds;
    const uint32_t values = 2 * diamonds;
    const uint32_t edges = 6 * diamonds;
    const uint32_t successor_offsets[] = { 0, 2, 3, 4, 5, 6 };
    const uint32_t successors[] = { 3, 4, 2, 1, 2, 2 };
    const uint32_t predecessor_offsets[] = { 0, 0, 1, 4, 5, 6 };
    const uint32_t predecessors[] = { 2, 1, 3, 4, 0, 0 };
    uint32_t* starts = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, blocks + 1);
    uint32_t* succ_offsets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, blocks + 1);
    uint32_t* pred_offsets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, blocks + 1);
    uint32_t* succ = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, edges);
    uint32_t* pred = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, edges);
    uint32_t* begin = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, values);
    uint32_t* end = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, values);
    uint32_t* ids = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, values + 1);
    uint32_t* fragments = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, values + 1);
    uint32_t* fragment_starts = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, 2 * values);
    uint32_t* fragment_ends = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, 2 * values);
    uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, 2 * values);
    const bool room = starts && succ_offsets && pred_offsets && succ && pred && begin && end && ids && fragments && fragment_starts && fragment_ends && kinds;
    tier1_cfg_test_expect(tally, room, "space for the spill-slot diamonds");
    if (room)
    {
        for (uint32_t b = 0; b <= blocks; b += 1)
        {
            starts[b] = 2 * b;
            succ_offsets[b] = (b / 5) * 6 + successor_offsets[b % 5];
            pred_offsets[b] = (b / 5) * 6 + predecessor_offsets[b % 5];
        }
        for (uint32_t e = 0; e < edges; e += 1)
        {
            succ[e] = (e / 6) * 5 + successors[e % 6];
            pred[e] = (e / 6) * 5 + predecessors[e % 6];
        }
        for (uint32_t v = 0; v < values; v += 1)
        {
            const uint32_t d = v % diamonds;
            const uint32_t home = 5 * d;
            const bool second = v >= diamonds;
            const uint32_t split = starts[home + (second ? 3 + (d & 1) : 1)];
            begin[v] = starts[home] + second;
            end[v] = starts[home + 5];
            ids[v] = v;
            fragments[v] = 2 * v;
            fragment_starts[2 * v] = begin[v];
            fragment_ends[2 * v] = split;
            fragment_starts[2 * v + 1] = split;
            fragment_ends[2 * v + 1] = end[v];
            kinds[2 * v] = second ? BUSTER_TIER1_ALLOCATE_REGISTER : BUSTER_TIER1_ALLOCATE_SPILL;
            kinds[2 * v + 1] = second ? BUSTER_TIER1_ALLOCATE_SPILL : BUSTER_TIER1_ALLOCATE_REGISTER;
        }
        ids[values] = values;
        fragments[values] = 2 * values;
        const Tier1CfgInput input = {
            .block_starts = starts,
            .successors = succ,
            .successor_offsets = succ_offsets,
            .predecessors = pred,
            .predecessor_offsets = pred_offsets,
            .definitions = begin,
            .block_count = blocks,
            .value_count = values,
            .edge_count = edges,
            .position_count = starts[blocks],
        };
        Tier1CfgAllocation result = {
            .value_interval_offsets = ids,
            .intervals = {
                .starts = begin,
                .ends = end,
            },
            .allocation = {
                .spill_ids = ids,
                .fragment_offsets = fragments,
                .starts = fragment_starts,
                .ends = fragment_ends,
                .kinds = kinds,
            },
        };
        const uint64_t scratch_mark = scratch->length;
        const uint64_t allowance = 768u << 10;
        Arena bounded = {
            .base = arena_push(scratch, allowance, BUSTER_DATAFLOW_VECTOR_BYTES),
            .capacity = allowance,
        };
        const bool solved = tier1_cfg_current_slots(&input, &result, output, &bounded);
        bool exact = solved;
        for (uint32_t b = 0; exact && b < blocks; b += 1)
        {
            const uint32_t d = b / 5;
            const uint32_t block = b % 5;
            const bool second = (d & 1) == 0 && (block == 1 || block == 2);
            const uint32_t k = result.current_slot_offsets[b];
            exact = result.current_slot_offsets[b + 1] - k == (uint32_t)(block != 0) + second;
            exact = exact && (block == 0 || result.current_slots[k] == d);
            exact = exact && (!second || result.current_slots[k + 1] == diamonds + d);
        }
        tier1_cfg_test_expect(tally, solved && bounded.length == 0, "4,098 spill candidates fit 768 KiB of scratch");
        tier1_cfg_test_expect(tally, exact, "each diamond holds exactly the values stored on all paths, in value order");
        // Refuse partway through building the first group's rows. A previous successful result remains usable.
        bounded.capacity = allowance / 2;
        const uint64_t output_mark = output->length;
        const Tier1CfgAllocation kept = result;
        const bool refused = !tier1_cfg_current_slots(&input, &result, output, &bounded);
        tier1_cfg_test_expect(tally, refused && bounded.length == 0 && output->length == output_mark && result.current_slot_offsets == kept.current_slot_offsets && result.current_slots == kept.current_slots, "slot-row exhaustion restores both arenas and preserves the published lists");
        arena_pop_to(scratch, scratch_mark);
    }
    arena_pop_to(output, saved);
}

static void tier1_cfg_test_current_slots(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    tier1_cfg_test_spill_prefix(tally);
    tier1_cfg_test_spill_edges(tally, output, scratch);
    tier1_cfg_test_spill_pressure(tally, output, scratch);
    tier1_cfg_test_spill_across(tally, output, scratch);
    tier1_cfg_test_slot_groups(tally, output, scratch);
}
