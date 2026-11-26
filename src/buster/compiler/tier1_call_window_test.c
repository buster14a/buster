// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A paid callee register is dead before one call and first defined again at the following definition phase. A
// parameter can occupy it for that call alone. The no-op template, the allocation certificate, real typed MOV edits,
// lazy debt, and exact argument/copy costs are checked separately, without relying on the policy to certify itself.

static void tier1_call_window_tests(Tier1EmitTest* test)
{
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    uint32_t blocks[] = { 0, 2, 16 };
    uint32_t successor_offsets[] = { 0, 1, 1 };
    uint32_t successors[] = { 1 };
    uint32_t predecessor_offsets[] = { 0, 0, 1 };
    uint32_t predecessors[] = { 0 };
    uint32_t phis[] = { 0, 0, 0 };
    uint32_t definitions[] = { 0, 3, 9 };
    uint32_t bindings[] = { 1u << X86_REGISTER_RDI, 0, 0 };
    const uint32_t callee = 1u << X86_REGISTER_RBX;
    const uint32_t pool = (1u << X86_REGISTER_RDI) | callee;
    uint32_t allowed[] = { pool, callee, callee };
    uint8_t classes[] = { 0, 0, 0, 0 };
    uint32_t uses[] = { 0, 14, 3, 4, 9, 12 };
    uint32_t use_offsets[] = { 0, 2, 4, 6 };
    uint32_t interval_uses[] = { 0, 1, 2, 4, 6 };
    uint8_t required[] = { 1, 1, 1, 1, 1, 1 };
    uint32_t starts[] = { 0, 2, 3, 9 };
    uint32_t ends[] = { 2, 16, 5, 13 };
    uint32_t values[] = { 0, 0, 1, 2 };
    uint32_t masks[] = { 1u << X86_REGISTER_RDI, pool, callee, callee };
    uint32_t fragments[] = { 0, 1, 2, 3, 4 };
    uint8_t kinds[] = { 0, 0, 0, 0 };
    uint8_t registers[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RBX, X86_REGISTER_RBX };
    uint8_t summaries[] = { X86_REGISTER_RDI, X86_REGISTER_RBX, X86_REGISTER_RBX };
    uint32_t spill_ids[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
    uint32_t intervals[] = { 0, 2, 3, 4 };
    uint32_t move_offsets[] = { 0, 0 };
    uint8_t placements[] = { TIER1_CFG_EDGE_EMPTY };
    uint32_t split_offsets[] = { 0 };
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
        .block_count = 2,
        .edge_count = 1,
        .value_count = 3,
        .use_count = 6,
        .position_count = 16,
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
            .use_offsets = interval_uses,
            .use_positions = uses,
            .use_required = required,
            .use_count = 6,
            .count = 4,
            .value_count = 3,
            .position_count = 16,
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
    };
    Tier1CfgCheck check = { 0 };
    tier1_emit_test_expect(test, tier1_cfg_verify(&input, &original, &test->scratch, &check), "call-window fixture's original allocation is independently valid");
    tier1_emit_test_expect(test, tier1_emit_call_window_bounds(&input, &original, 1, 6) &&
                                  !tier1_emit_call_window_bounds(&input, &original, 1, 2) &&
                                  !tier1_emit_call_window_bounds(&input, &original, 1, 7) &&
                                  !tier1_emit_call_window_bounds(&input, &original, 1, 14),
                          "a call window has two even boundaries strictly inside its block and original interval");
    tier1_emit_test_expect(test, tier1_emit_recolor_free(&original, 6, 9, callee) == callee &&
                                  tier1_emit_recolor_free(&original, 6, 9, 0) == 0,
                          "a paid callee free through the restore use is available, and no unpaid register is proposed");
    starts[3] = 7;
    tier1_emit_test_expect(test, tier1_emit_recolor_free(&original, 6, 9, callee) == 0,
                          "a call result occupying the callee at use plus one refuses the window");
    starts[3] = 8;
    tier1_emit_test_expect(test, tier1_emit_recolor_free(&original, 6, 9, callee) == 0,
                          "a reload or operand occupying the callee at the restore use refuses the window");
    starts[3] = 9;
    Tier1EmitCallWindow window = { .interval = 1, .original_fragment = 1, .value = 0, .use = 6, .choices = callee, .source = X86_REGISTER_RDI };
    Tier1CfgAllocation next = original;
    uint64_t work = 0;
    Tier1EmitError changed = tier1_emit_call_window_template(&original, &window, 1, &test->output, &test->scratch, &work, &next);
    tier1_emit_test_expect(test, changed == TIER1_EMIT_OK && next.allocation.fragment_count == 6 && window.fragment == 2 &&
                                  next.allocation.starts[2] == 6 && next.allocation.ends[2] == 8 &&
                                  next.allocation.fragment_offsets[2] == 4 && next.allocation.spill_ids == spill_ids &&
                                  next.edge_move_offsets == move_offsets && next.value_interval_offsets == intervals,
                          "the template owns three register fragments while retaining interval, edge and private-slot identities");
    if (changed == TIER1_EMIT_OK)
    {
        bool same = true;
        for (uint32_t v = 0; same && v < input.value_count; v += 1)
        {
            for (uint32_t q = 0; same && q < input.position_count; q += 1)
            {
                uint32_t before = 0, after = 0;
                uint8_t before_kind = 0, after_kind = 0;
                const bool a = tier1_cfg_location_oracle(&original, v, q, &before, &before_kind);
                const bool b = tier1_cfg_location_oracle(&next, v, q, &after, &after_kind);
                same = a == b && (!a || (before == after && before_kind == after_kind));
            }
        }
        tier1_emit_test_expect(test, same && tier1_cfg_verify(&input, &next, &test->scratch, &check),
                              "identity normalization preserves every physical location and verifies without edge changes");
        next.allocation.registers[2] = X86_REGISTER_RBX;
        next.allocation.value_registers[0] = UINT8_MAX;
        tier1_emit_test_expect(test, tier1_cfg_verify(&input, &next, &test->scratch, &check),
                              "the independent CFG verifier accepts the real caller-to-callee-to-caller window");
        Tier1CertReport report = { 0 };
        Tier1CertCfgStats certified = { 0 };
        tier1_emit_test_expect(test, tier1_certificate_cfg_check(&input, &next, &test->output, &test->scratch, &report, &certified) == TIER1_CERT_OK,
                              "the independent allocation certificate proves the two interior register edits");

        // Emit the actual two phases at both canonical GPR widths. Identity boundaries must leave debt alone;
        // active boundaries must repay the source before their MOVE, and carry ordinary allocation provenance.
        uint32_t sizes[8] = { 0 };
        uint32_t empty_uses[9] = { 0 };
        uint32_t offsets[18];
        for (uint32_t q = 0; q < 18; q += 1)
        {
            offsets[q] = (q > 6) + (q > 8);
        }
        uint32_t events[] = { 2, 3 };
        uint32_t nodes[] = { 0, 0, 0, 0, 1, 4 };
        uint8_t locations[8];
        IrFunction function = { .instruction_count = 8 };
        Tier1SelectPlan plan = { .function = &function, .effective_use_offsets = empty_uses };
        Tier1EmitPrepared prepared = { .sizes = sizes };
        Tier1Frame frame = { 0 };
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            sizes[0] = width;
            for (uint32_t active = 0; active < 2; active += 1)
            {
                next.allocation.registers[2] = active ? X86_REGISTER_RBX : X86_REGISTER_RDI;
                memset(locations, UINT8_MAX, sizeof(locations));
                locations[0] = X86_REGISTER_RDI;
                Tier1EmitTransitions transitions = { .offsets = offsets, .fragments = events, .values = nodes, .registers = locations };
                for (uint32_t r = 0; r < BUSTER_TIER1_EMIT_LOCATIONS; r += 1)
                {
                    transitions.owners[r] = BUSTER_IR_NONE;
                }
                transitions.owners[X86_REGISTER_RDI] = 0;
                X86Instruction records[2] = { 0 };
                uint32_t origins[2] = { 0 };
                uint32_t origin_values[2] = { 0 };
                Tier1EmitBuffer buffer = { .records = records, .capacity = 2, .origins = origins, .origin_values = origin_values };
#if defined(BUSTER_TIER1_MEASURE_KINDS)
                uint8_t measure[2] = { 0 };
                buffer.measure_kinds = measure;
                buffer.measure_kind = 'a';
#endif
                Tier1EmitResult emitted = { .prepared = prepared, .code = { .records = records } };
                const uint32_t owed = tier1_emit_owed(&plan, &prepared, &next.allocation, &transitions, 3, 6, UINT8_MAX, false, false, 1u << X86_REGISTER_RDI);
                const Tier1EmitError opening = tier1_emit_phase(&prepared, &next.allocation, &frame, &transitions, 6, &buffer, &emitted);
                const Tier1EmitError closing = tier1_emit_phase(&prepared, &next.allocation, &frame, &transitions, 8, &buffer, &emitted);
                bool moved = opening == TIER1_EMIT_OK && closing == TIER1_EMIT_OK && buffer.count == 2 * active &&
                             emitted.transition_count == 2 * active && owed == (active ? 1u << X86_REGISTER_RDI : 0) &&
                             locations[0] == X86_REGISTER_RDI && transitions.owners[X86_REGISTER_RDI] == 0;
                if (active)
                {
                    moved = moved && records[0].reg == X86_REGISTER_RBX && records[0].rm == X86_REGISTER_RDI &&
                            records[1].reg == X86_REGISTER_RDI && records[1].rm == X86_REGISTER_RBX &&
                            records[0].form == (width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32) &&
                            records[1].form == records[0].form && origins[0] == tier1_origin(TIER1_ORIGIN_MOVE, 6) &&
                            origins[1] == tier1_origin(TIER1_ORIGIN_MOVE, 8) && origin_values[0] == 0 && origin_values[1] == 0;
#if defined(BUSTER_TIER1_MEASURE_KINDS)
                    moved = moved && measure[0] == 'm' && measure[1] == 'm';
#endif
                    uint32_t references[] = { UINT32_MAX, UINT32_MAX };
                    Tier1CertLocation decoded_locations[32] = { 0 };
                    for (uint32_t r = 0; r < 16; r += 1)
                    {
                        decoded_locations[2 * r] = (Tier1CertLocation){ .class = TIER1_CERT_GPR, .reg = (uint8_t)r, .width = 8 };
                        decoded_locations[2 * r + 1] = (Tier1CertLocation){ .class = TIER1_CERT_GPR, .reg = (uint8_t)r, .width = 4 };
                    }
                    Tier1CertEmittedStats stats = { 0 };
                    Tier1CertEmittedBuild build = { .emitted = &emitted, .stats = &stats, .references = references,
                                                   .locations = decoded_locations, .base = X86_REGISTER_RSP, .frame_pointer = INT64_MIN };
                    for (uint32_t r = 0; r < 2; r += 1)
                    {
                        Tier1CertInstruction edit = { 0 };
                        moved = moved && tier1_cert_emitted_move(&build, r, 0, &edit) && edit.op == TIER1_CERT_MOVE &&
                                edit.a == tier1_cert_emitted_gpr(records[r].reg, width) && edit.b == tier1_cert_emitted_gpr(records[r].rm, width);
                    }
                }
                tier1_emit_test_expect(test, moved, active ? "typed interior copies have real MOVE provenance, separate transport tags, and independent decoding" :
                                                                 "identity register boundaries emit nothing and never force lazy call debt");
            }
        }

        // Two removed C/R records pay exactly two allocation copies. Changing an argument from its ABI register
        // adds a third copy and must lose; an already copied argument keeps its one ABI copy and can tie in total.
        uint64_t weights[] = { UINT64_C(1) << 32, UINT64_C(1) << 32 };
        Tier1EmitRecolorCall call = { .node = 3, .position = 7, .block = 1, .values = { 0 }, .destinations = { X86_REGISTER_RDI }, .count = 1 };
        Tier1EmitRecolorModel model = { .weights = weights, .calls = &call, .count = 1, .callee = callee };
        Tier1EmitRegionArguments arguments = { 0 };
        next.allocation.registers[2] = X86_REGISTER_RDI;
        changed = tier1_emit_region_arguments_prepare(&next, &model, &test->output, &test->scratch, &work, &arguments);
        tier1_emit_test_expect(test, changed == TIER1_EMIT_OK, "the argument model records the middle fragment at the call use phase");
        if (changed == TIER1_EMIT_OK)
        {
            uint8_t candidate_registers[6];
            memcpy(candidate_registers, next.allocation.registers, sizeof(candidate_registers));
            candidate_registers[2] = X86_REGISTER_RBX;
            Tier1CfgAllocation candidate = next;
            candidate.allocation.registers = candidate_registers;
            uint32_t old_stores[32] = { 0 }, old_loads[32] = { 0 };
            old_stores[16 + X86_REGISTER_RDI] = 1;
            old_loads[16 + X86_REGISTER_RDI] = 1;
            uint32_t after_stores[16] = { 0 }, after_loads[16] = { 0 };
            uint32_t changed_fragments[] = { 2 }, changed_blocks[] = { 1 };
            Tier1EmitEpochCache cache = { .stores = old_stores, .loads = old_loads };
            Tier1EmitEpochTrial epochs = { .fragment_count = 1, .fragments = changed_fragments, .block_count = 1,
                                           .blocks = changed_blocks, .stores = after_stores, .loads = after_loads };
            for (uint32_t copied = 0; copied < 2; copied += 1)
            {
                call.destinations[0] = copied ? X86_REGISTER_RSI : X86_REGISTER_RDI;
                Tier1EmitRegionScore score = { 0 };
                changed = tier1_emit_call_window_score(&input, &next, &candidate, &model, &arguments, &cache, &epochs, 1,
                                                       &test->output, &test->scratch, &work, &score);
                const Tier1EmitRegionPathStatus path = changed == TIER1_EMIT_OK ? tier1_emit_region_certify(&input, &score, &test->scratch, &work) : TIER1_REGION_PATH_INVALID;
                tier1_emit_test_expect(test, changed == TIER1_EMIT_OK && score.cost.preservation == -((__int128)2 << 32) &&
                                              score.cost.edges == ((__int128)2 << 32) && score.preservation_gains[1] == 0 &&
                                              tier1_emit_recolor_total(&score.cost) == (copied ? 0 : (__int128)1 << 32) &&
                                              path == (copied ? TIER1_REGION_PATH_OK : TIER1_REGION_PATH_LOSS),
                                      copied ? "two allocation copies can tie two memory records when exact ABI transport stays unchanged" :
                                               "the total-path proof refuses an added ABI copy even when memory preservation falls");
            }
        }
    }
    arena_pop_to(&test->output, output_mark);
    next = original;
    Arena no_room = { 0 };
    tier1_emit_test_expect(test, tier1_emit_call_window_template(&original, &window, 1, &no_room, &test->scratch, &work, &next) == TIER1_EMIT_MEMORY &&
                                  next.allocation.starts == starts && no_room.length == 0 && test->scratch.length == scratch_mark,
                          "template allocation failure leaves the original and both arenas unchanged");
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->scratch, scratch_mark);
}
