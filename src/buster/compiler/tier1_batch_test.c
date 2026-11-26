// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A frozen allocation makes the batch's distinguishing case explicit: a callee owner is defined after the
// seed call and dies in the next parameter interval. Both allocation verifiers inspect the resulting transport.

static void tier1_batch_tests(Tier1EmitTest* test)
{
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    uint32_t block_starts[] = { 0, 2, 6, 10, 14 };
    uint32_t successor_offsets[] = { 0, 1, 2, 3, 3 };
    uint32_t successors[] = { 1, 2, 3 };
    uint32_t predecessor_offsets[] = { 0, 0, 1, 2, 3 };
    uint32_t predecessors[] = { 0, 1, 2 };
    uint32_t phis[] = { 0, 0, 0, 0, 0 };
    uint32_t definitions[] = { 0, 5 };
    uint32_t bindings[] = { 1u << X86_REGISTER_RDI, 0 };
    const uint32_t pool = (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RBX);
    uint32_t allowed[] = { pool, 1u << X86_REGISTER_RBX };
    uint8_t classes[] = { 0, 0, 0, 0, 0 };
    uint32_t uses[] = { 0, 12, 5, 8 };
    uint32_t use_offsets[] = { 0, 2, 4 };
    uint32_t interval_uses[] = { 0, 1, 1, 1, 2, 4 };
    uint8_t required[] = { 1, 1, 1, 1 };
    uint32_t starts[] = { 0, 2, 6, 10, 5 };
    uint32_t ends[] = { 2, 6, 10, 13, 9 };
    uint32_t values[] = { 0, 0, 0, 0, 1 };
    uint32_t masks[] = { pool, pool, pool, pool, 1u << X86_REGISTER_RBX };
    uint32_t fragments[] = { 0, 1, 2, 3, 4, 5 };
    uint8_t kinds[] = { 0, 0, 0, 0, 0 };
    uint8_t registers[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RBX };
    uint8_t summaries[] = { X86_REGISTER_RDI, X86_REGISTER_RBX };
    uint32_t spill_ids[] = { UINT32_MAX, UINT32_MAX };
    uint32_t intervals[] = { 0, 4, 5 };
    uint32_t move_offsets[] = { 0, 0, 0, 0 };
    uint8_t placements[] = { TIER1_CFG_EDGE_EMPTY, TIER1_CFG_EDGE_EMPTY, TIER1_CFG_EDGE_EMPTY };
    uint32_t split_offsets[] = { 0 };
    Tier1CfgInput input = {
        .block_starts = block_starts,
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
        .use_count = 4,
        .position_count = 14,
        .registers = { pool, 0, 0 },
        .scratch_registers = { BUSTER_TIER1_SELECT_SCRATCH, BUSTER_TIER1_SELECT_FLOAT_SCRATCH, 0 },
    };
    Tier1CfgAllocation cfg = {
        .intervals = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = masks,
            .classes = classes,
            .use_offsets = interval_uses,
            .use_positions = uses,
            .use_required = required,
            .use_count = 4,
            .count = 5,
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
            .fragment_count = 5,
            .interval_count = 5,
            .value_registers = summaries,
            .spill_ids = spill_ids,
        },
        .value_interval_offsets = intervals,
        .edge_placements = placements,
        .edge_move_offsets = move_offsets,
        .split_move_offsets = split_offsets,
    };
    Tier1CfgCheck check = { 0 };
    tier1_emit_test_expect(test, tier1_cfg_verify(&input, &cfg, &test->scratch, &check), "batch fixture's original allocation is independently valid");
    Tier1CfgInput next_input = { 0 };
    Tier1CfgAllocation next = { 0 };
    Tier1EmitRegionExchangeStats stats = { 0 };
    const uint32_t callee = 1u << X86_REGISTER_RBX;
    Tier1EmitError changed = tier1_emit_cfg_region_exchange(&input, &cfg, 1, X86_REGISTER_RBX, callee,
                                                            &test->output, &test->scratch, &next_input, &next, &stats);
    tier1_emit_test_expect(test, changed == TIER1_EMIT_INVALID && stats.failure_reason == TIER1_REGION_EXCHANGE_CONTAINMENT &&
                                  test->output.length == output_mark && test->scratch.length == scratch_mark,
                          "a scalar seed refuses the owner extending past its end without publishing storage");
    uint64_t work = 0;
    Tier1EmitBatchRange range = { 1, 2 };
    changed = tier1_emit_batch_owners(&input, &cfg, 0, X86_REGISTER_RDI, X86_REGISTER_RBX, &work, &range);
    tier1_emit_test_expect(test, changed == TIER1_EMIT_OK && range.first == 1 && range.past == 3,
                          "complete-owner closure reaches the adjacent interval containing its last use");
    changed = tier1_emit_cfg_region_exchange_range(&input, &cfg, range.first, range.past, X86_REGISTER_RBX, callee,
                                                    &test->output, &test->scratch, &next_input, &next, &stats);
    tier1_emit_test_expect(test, changed == TIER1_EMIT_OK && next.allocation.registers[1] == X86_REGISTER_RBX &&
                                  next.allocation.registers[2] == X86_REGISTER_RBX && next.allocation.registers[4] == X86_REGISTER_RDI &&
                                  next.move_count == 2 && next.edge_move_offsets[1] == next.edge_move_offsets[2],
                          "batch transport cancels its internal copies and carries the displaced owner in the freed caller");
    if (changed == TIER1_EMIT_OK)
    {
        tier1_emit_test_expect(test, tier1_cfg_verify(&next_input, &next, &test->scratch, &check), "the independent CFG verifier accepts complete batch transport");
        Tier1CertReport report = { 0 };
        Tier1CertCfgStats certified = { 0 };
        tier1_emit_test_expect(test, tier1_certificate_cfg_check(&next_input, &next, &test->output, &test->scratch, &report, &certified) == TIER1_CERT_OK,
                              "the independent allocation certificate accepts the batch");
    }
    arena_pop_to(&test->output, output_mark);
    for (uint32_t refusal = 0; refusal < 3; refusal += 1)
    {
        const uint32_t fixed = callee;
        bindings[1] = refusal == 0 ? callee : 0;
        input.fixed = (Tier1FixedIntervals){ .count = refusal == 1, .classes = classes, .registers = &fixed, .starts = starts, .ends = ends };
        kinds[2] = refusal == 2 ? BUSTER_TIER1_ALLOCATE_SPILL : BUSTER_TIER1_ALLOCATE_REGISTER;
        changed = tier1_emit_cfg_region_exchange_range(&input, &cfg, 1, 3, X86_REGISTER_RBX, callee,
                                                        &test->output, &test->scratch, &next_input, &next, &stats);
        tier1_emit_test_expect(test, changed == TIER1_EMIT_INVALID && test->output.length == output_mark && test->scratch.length == scratch_mark &&
                                      registers[1] == X86_REGISTER_RDI && registers[4] == X86_REGISTER_RBX,
                              "bound owner, fixed role, and spill-gap refusals leave the original and both arenas unchanged");
    }
    const uint32_t diamond_offsets[] = { 0, 2, 3, 4, 4 };
    const uint32_t diamond_targets[] = { 1, 2, 3, 3 };
    const int64_t gains[] = { 0, 2, 0, 0 };
    const int64_t transport[] = { -1, -1, -1, -1 };
    input.successor_offsets = diamond_offsets;
    input.successors = diamond_targets;
    input.edge_count = 4;
    work = 0;
    tier1_emit_test_expect(test, tier1_emit_region_path_gain(&input, gains, transport, &test->scratch, BUSTER_TIER1_RECOLOR_WORK, &work, 0) == TIER1_REGION_PATH_LOSS &&
                                  test->scratch.length == scratch_mark && registers[1] == X86_REGISTER_RDI,
                          "a call-free cold arm rejects the complete batch despite the other arm's preservation saving");
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->scratch, scratch_mark);
}
