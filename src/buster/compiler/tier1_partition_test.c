// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

static void tier1_partition_tests(Tier1EmitTest* test)
{
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    uint32_t blocks[] = { 0, 2, 6, 10, 14 };
    uint32_t successor_offsets[] = { 0, 1, 2, 3, 3 };
    uint32_t successors[] = { 1, 2, 3 };
    uint32_t predecessor_offsets[] = { 0, 0, 1, 2, 3 };
    uint32_t predecessors[] = { 0, 1, 2 };
    uint32_t phis[] = { 0, 0, 0, 0, 0 };
    uint32_t definitions[] = { 0, 5 };
    uint32_t bindings[] = { 1u << X86_REGISTER_RDI, 0 };
    const uint32_t pool = (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RBX);
    uint32_t allowed[] = { pool, 1u << X86_REGISTER_RBX };
    uint8_t classes[] = { 0, 0, 0, 0 };
    uint32_t uses[] = { 0, 6, 6, 12, 5, 8 };
    uint32_t use_offsets[] = { 0, 4, 6 };
    uint32_t interval_uses[] = { 0, 1, 3, 4, 6 };
    uint8_t required[] = { 1, 1, 1, 1, 1, 1 };
    uint32_t starts[] = { 0, 2, 10, 5 };
    uint32_t ends[] = { 2, 10, 13, 9 };
    uint32_t values[] = { 0, 0, 0, 1 };
    uint32_t masks[] = { 1u << X86_REGISTER_RDI, pool, pool, 1u << X86_REGISTER_RBX };
    uint32_t fragments[] = { 0, 1, 2, 3, 4 };
    uint8_t kinds[] = { 0, 0, 0, 0 };
    uint8_t registers[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RBX };
    uint8_t summaries[] = { X86_REGISTER_RDI, X86_REGISTER_RBX };
    uint32_t spill_ids[] = { UINT32_MAX, UINT32_MAX };
    uint32_t intervals[] = { 0, 3, 4 };
    uint32_t move_offsets[] = { 0, 0, 0, 0 };
    uint8_t placements[] = { TIER1_CFG_EDGE_EMPTY, TIER1_CFG_EDGE_EMPTY, TIER1_CFG_EDGE_EMPTY };
    uint32_t split_offsets[] = { 0 };
    uint8_t parameters[] = { 1, 0 };
    uint8_t flags[] = { BUSTER_TIER1_ALLOCATE_REQUIRED, 0, 0, 0 };
    uint8_t hints[] = { X86_REGISTER_RDI, UINT8_MAX, UINT8_MAX, X86_REGISTER_RBX };
    uint32_t hint_values[] = { UINT32_MAX, 0, 0, 0 };
    uint32_t hint_intervals[] = { UINT32_MAX, BUSTER_TIER1_ALLOCATE_HINT_EXIT, BUSTER_TIER1_ALLOCATE_HINT_EXIT | 1u, 1 };
    uint32_t alternates[] = { UINT32_MAX, UINT32_MAX, 1, UINT32_MAX };
    uint32_t continuations[] = { 0, 1, 1, 3 };
    uint32_t avoided[] = { 0, 1u << X86_REGISTER_RBX, 0, 0 };
    uint8_t register_hints[] = { UINT8_MAX, X86_REGISTER_RDI, UINT8_MAX, X86_REGISTER_RBX };
    uint8_t recipes[] = { 0, 0, 0, 0 };
    uint8_t unread[] = { 0, 0, 0, 0 };
    uint32_t position_blocks[] = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
    uint32_t lookup_base[] = { 0, 4 };
    uint32_t lookup[] = { 0, 1, 1, 2, UINT32_MAX, 3, 3, UINT32_MAX };
    Tier1CfgInput input = {
        .block_starts = blocks,
        .successor_offsets = successor_offsets,
        .successors = successors,
        .predecessor_offsets = predecessor_offsets,
        .predecessors = predecessors,
        .classes = classes,
        .allowed_registers = allowed,
        .definitions = definitions,
        .definition_registers = bindings,
        .use_offsets = use_offsets,
        .use_positions = uses,
        .use_required = required,
        .phi_offsets = phis,
        .phi_operand_offsets = phis,
        .block_count = 4,
        .edge_count = 3,
        .value_count = 2,
        .use_count = 6,
        .position_count = 14,
        .registers = { pool, 0, 0 },
        .scratch_registers = { BUSTER_TIER1_SELECT_SCRATCH, BUSTER_TIER1_SELECT_FLOAT_SCRATCH, 0 },
    };
    Tier1CfgAllocation original = {
        .intervals = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = masks,
            .classes = classes,
            .flags = flags,
            .hints = hints,
            .hint_values = hint_values,
            .hint_intervals = hint_intervals,
            .alternate_intervals = alternates,
            .continuation_intervals = continuations,
            .register_hints = register_hints,
            .avoided_registers = avoided,
            .rematerializable = recipes,
            .use_offsets = interval_uses,
            .use_positions = uses,
            .use_required = required,
            .use_count = 6,
            .count = 4,
            .value_count = 2,
            .position_count = 14,
            .registers = { pool, 0, 0 },
        },
        .allocation = {
            .fragment_offsets = fragments,
            .starts = starts,
            .ends = ends,
            .kinds = kinds,
            .registers = registers,
            .fragment_count = 4,
            .interval_count = 4,
            .value_registers = summaries,
            .spill_ids = spill_ids,
        },
        .value_interval_offsets = intervals,
        .edge_placements = placements,
        .edge_move_offsets = move_offsets,
        .split_move_offsets = split_offsets,
        .position_blocks = position_blocks,
        .value_block_base = lookup_base,
        .value_block_intervals = lookup,
        .block_starts = blocks,
        .block_count = 4,
        .unread_entries = unread,
    };
    Tier1CfgCheck check = { 0 };
    tier1_emit_test_expect(test, tier1_cfg_verify(&input, &original, &test->scratch, &check), "partition fixture's original allocation is independently valid");
    Tier1CfgAllocation next = original;
    const bool changed = tier1_emit_partition_parameters(&input, parameters, &test->output, &test->scratch, &next);
    tier1_emit_test_expect(test, changed && next.intervals.count == 5 && next.allocation.fragment_count == 5 &&
                                  next.value_interval_offsets[1] == 4 && next.value_interval_offsets[2] == 5 &&
                                  next.intervals.starts[2] == 6 && next.intervals.ends[1] == 6 &&
                                  next.intervals.use_offsets[1] == 1 && next.intervals.use_offsets[2] == 1 && next.intervals.use_offsets[3] == 3,
                          "partition duplicates no uses and puts both uses at a cut in its new child");
    if (changed && next.intervals.count == 5)
    {
        tier1_emit_test_expect(test, next.intervals.hint_intervals[3] == (BUSTER_TIER1_ALLOCATE_HINT_EXIT | 2u) &&
                                      next.intervals.hint_intervals[4] == 1 && next.intervals.alternate_intervals[3] == 1 &&
                                      next.intervals.continuation_intervals[4] == 4 && next.intervals.avoided_registers[2] == avoided[1] &&
                                      next.intervals.register_hints[2] == register_hints[1] && next.intervals.flags[0] == flags[0] &&
                                      tier1_allocation_hints_valid(&next.intervals),
                              "all per-interval columns follow their children and EXIT hints name the final child");
        tier1_emit_test_expect(test, next.edge_placements == original.edge_placements && next.edge_move_offsets == original.edge_move_offsets &&
                                      next.split_move_offsets == original.split_move_offsets && next.allocation.spill_ids == original.allocation.spill_ids &&
                                      next.allocation.value_registers == original.allocation.value_registers && next.intervals.allowed_registers[0] == bindings[0],
                              "partition preserves transport, spill rows, value summaries, and incoming definition binding");
        bool same = next.value_block_intervals != lookup && next.position_blocks == position_blocks;
        for (uint32_t v = 0; same && v < input.value_count; v += 1)
        {
            for (uint32_t q = 0; same && q < input.position_count; q += 1)
            {
                uint32_t before = 0, after = 0;
                uint8_t before_kind = 0, after_kind = 0;
                const bool a = tier1_cfg_location_oracle(&original, v, q, &before, &before_kind);
                const bool b = tier1_cfg_location(&next, v, q, &after, &after_kind);
                same = a == b && (!a || (before == after && before_kind == after_kind)) &&
                       tier1_cfg_interval_at(&next, v, q) == tier1_cfg_interval_oracle(&next, v, q);
            }
            for (uint32_t b = 0; same && b < input.block_count; b += 1)
            {
                uint32_t before = 0, after = 0;
                uint8_t before_kind = 0, after_kind = 0;
                const bool a = tier1_cfg_entry_location_oracle(&original, v, blocks[b], &before, &before_kind);
                const bool next_entry = tier1_cfg_entry_location(&next, v, blocks[b], &after, &after_kind);
                same = a == next_entry && (!a || (before == after && before_kind == after_kind));
            }
        }
        tier1_emit_test_expect(test, same, "mapped and oracle lookups preserve every position and semantic entry-minus-one location");
        const bool valid = tier1_cfg_verify(&input, &next, &test->scratch, &check);
        tier1_emit_test_expect(test, valid, "the independent verifier accepts the partition without new edge programs");
        if (valid)
        {
            Tier1CertReport report = { 0 };
            Tier1CertCfgStats certified = { 0 };
            tier1_emit_test_expect(test, tier1_certificate_cfg_check(&input, &next, &test->output, &test->scratch, &report, &certified) == TIER1_CERT_OK,
                                  "the independent allocation certificate accepts the partition");
        }
    }
    arena_pop_to(&test->output, output_mark);
    next = original;
    blocks[2] = 7;
    tier1_emit_test_expect(test, !tier1_emit_partition_parameters(&input, parameters, &test->output, &test->scratch, &next) &&
                                  next.intervals.starts == original.intervals.starts && test->output.length == output_mark && test->scratch.length == scratch_mark,
                          "an odd instruction-phase boundary refuses partition transactionally");
    blocks[2] = 6;
    Arena no_room = { 0 };
    tier1_emit_test_expect(test, !tier1_emit_partition_parameters(&input, parameters, &no_room, &test->scratch, &next) &&
                                  next.intervals.starts == original.intervals.starts && no_room.length == 0 && test->scratch.length == scratch_mark,
                          "partition capacity refusal leaves the original metadata and arenas unchanged");
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->scratch, scratch_mark);
}
