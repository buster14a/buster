// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A frozen, independently verified allocation has a paid ordinary parameter row and calls in successive blocks.
// Test actual kind/edge reconstruction, positive and negative current-slot certificates, exact epoch/S/L pricing,
// a mixed join, an unseeded loop, and the real phase/edge records across a hostile native callee.

static __attribute__((noinline)) void tier1_spill_region_hostile(void)
{
    __asm__ volatile("xor %%eax, %%eax\n\txor %%ecx, %%ecx\n\txor %%edx, %%edx\n\txor %%esi, %%esi\n\txor %%edi, %%edi\n\t"
                     "xor %%r8d, %%r8d\n\txor %%r9d, %%r9d\n\txor %%r10d, %%r10d\n\txor %%r11d, %%r11d"
                     : : : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "cc", "memory");
}

static void tier1_spill_region_native(Tier1EmitTest* test, const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                      const Tier1EmitPrepared* prepared, uint32_t interval)
{
    // Use the policy's real opening fragment and its reconstructed final edge. The two external calls destroy
    // every caller GPR, while a true ordinary store and edge reload retain the original parameter value.
    uint32_t offsets[30] = { 0 };
    uint32_t fragments[1] = { cfg->allocation.fragment_offsets[interval] + 1 };
    uint32_t nodes[12] = { 0 };
    uint8_t registers[14];
    uint32_t stamps[14] = { 0 };
    memset(registers, UINT8_MAX, sizeof(registers));
    registers[0] = X86_REGISTER_RDI;
    const uint32_t position = cfg->allocation.starts[fragments[0]];
    for (uint32_t q = position + 1; q < 30; q += 1)
    {
        offsets[q] = 1;
    }
    Tier1EmitTransitions transitions = { .offsets = offsets, .fragments = fragments, .values = nodes, .registers = registers,
                                        .slot_stamps = stamps, .stamp = 1 };
    for (uint32_t r = 0; r < BUSTER_TIER1_EMIT_LOCATIONS; r += 1)
    {
        transitions.owners[r] = BUSTER_IR_NONE;
    }
    transitions.owners[X86_REGISTER_RDI] = 0;
    int32_t frame_offsets[] = { 0 };
    uint32_t value_nodes[] = { 0 };
    Tier1EmitResult emitted = { .cfg_input = *input, .cfg = *cfg, .allocation = cfg->allocation, .prepared = *prepared,
                               .value_nodes = value_nodes, .frame = { .count = 1, .offset = frame_offsets, .base_register = X86_REGISTER_RSP } };
    X86Instruction records[16] = { 0 };
    uint32_t origins[16] = { 0 }, origin_values[16] = { 0 };
    Tier1EmitBuffer buffer = { .records = records, .capacity = 16, .origins = origins, .origin_values = origin_values };
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    uint8_t kinds[16] = { 0 };
    buffer.measure_kinds = kinds;
    buffer.measure_kind = 'n';
#endif
    bool ok = tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = 24 });
    tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_OTHER, 0), BUSTER_IR_NONE);
    Tier1EmitError error = ok ? tier1_emit_phase(prepared, &cfg->allocation, &emitted.frame, &transitions, position, &buffer, &emitted) : TIER1_EMIT_MEMORY;
    const uint32_t stored = buffer.count;
    // A second seed in the same proven stamp emits no second store, but still has the same ordinary allocation role.
    error = error == TIER1_EMIT_OK ? tier1_emit_phase(prepared, &cfg->allocation, &emitted.frame, &transitions, position, &buffer, &emitted) : error;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && buffer.count == stored && emitted.spill_stores == 1 &&
                                  origins[1] == tier1_origin(TIER1_ORIGIN_MOVE, position),
                          "a real parameter spill uses MOVE provenance and an already current ordinary row elides a repeat store");
    for (uint32_t c = 0; error == TIER1_EMIT_OK && c < 2; c += 1)
    {
        ok = tier1_emit_append(&buffer, tier1_call_constant(X86_REGISTER_R11, (uintptr_t)&tier1_spill_region_hostile)) &&
             tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_CALL_RM64, .rm = X86_REGISTER_R11 });
        error = ok ? error : TIER1_EMIT_MEMORY;
    }
    error = error == TIER1_EMIT_OK ? tier1_emit_cfg_moves(&emitted, input->edge_count - 1, &buffer) : error;
    if (error == TIER1_EMIT_OK)
    {
        ok = tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI }) &&
             tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_ADD_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = 24 }) &&
             tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_RET });
        error = ok ? error : TIER1_EMIT_MEMORY;
    }
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && emitted.spill_stores == 1 && emitted.spill_loads == 1,
                          "the native call region has one actual ordinary store and one actual reconstructed edge reload");
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    tier1_emit_test_expect(test, kinds[1] == 'S' && kinds[6] == 'u',
                          "the true ordinary store and carried-value edge reload retain separate S and u measurement tags");
#endif
    Tier1SelectCode code = { .records = records, .count = buffer.count };
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    const bool loaded = error == TIER1_EMIT_OK && tier1_emit_test_load(test, &code, &executable, &bytes);
    uint64_t random = UINT64_C(0x2bc6443dc5e8939b);
    for (uint32_t sample = 0; loaded && sample < 16; sample += 1)
    {
        const uint64_t value = sample == 0 ? UINT64_MAX : tier1_emit_test_random(&random);
        tier1_emit_test_expect(test, tier1_emit_test_call(executable, value, 0) == value,
                              "ordinary parameter residency survives two hostile native calls without a call-save row");
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
}

static void tier1_spill_region_tests(Tier1EmitTest* test)
{
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    uint32_t blocks[] = { 0, 2, 8, 14, 20, 28 };
    uint32_t successor_offsets[] = { 0, 1, 2, 3, 4, 4 };
    uint32_t successors[] = { 1, 2, 3, 4, 0, 0 };
    uint32_t predecessor_offsets[] = { 0, 0, 1, 2, 3, 4 };
    uint32_t predecessors[] = { 0, 1, 2, 3, 0, 0 };
    uint32_t phis[6] = { 0 };
    uint32_t definitions[] = { 0 };
    uint32_t masks[5] = { 1u << X86_REGISTER_RDI, 1u << X86_REGISTER_RDI, 1u << X86_REGISTER_RDI, 1u << X86_REGISTER_RDI, 1u << X86_REGISTER_RDI };
    uint8_t classes[5] = { 0 };
    uint32_t uses[] = { 0, 26 };
    uint32_t use_offsets[] = { 0, 2 };
    uint32_t interval_uses[] = { 0, 1, 1, 1, 1, 2 };
    uint8_t required[] = { 1, 1 };
    uint32_t starts[] = { 0, 2, 8, 14, 20 };
    uint32_t ends[] = { 2, 8, 14, 20, 27 };
    uint32_t values[5] = { 0 };
    uint32_t fragments[] = { 0, 1, 2, 3, 4, 7 };
    uint32_t fragment_starts[] = { 0, 2, 8, 14, 20, 21, 24 };
    uint32_t fragment_ends[] = { 2, 8, 14, 20, 21, 24, 27 };
    uint8_t kinds[] = { 0, 0, 0, 0, 0, BUSTER_TIER1_ALLOCATE_SPILL, 0 };
    uint8_t registers[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, UINT8_MAX, X86_REGISTER_RDI };
    uint8_t summaries[] = { UINT8_MAX };
    uint32_t slots[] = { 0 };
    uint8_t widths[] = { 8 };
    uint32_t intervals[] = { 0, 5 };
    Tier1CfgInput input = {
        .block_starts = blocks, .successor_offsets = successor_offsets, .successors = successors,
        .predecessor_offsets = predecessor_offsets, .predecessors = predecessors, .phi_offsets = phis, .phi_operand_offsets = phis,
        .classes = classes, .allowed_registers = masks, .definition_registers = masks, .definitions = definitions,
        .use_offsets = use_offsets, .use_positions = uses, .use_required = required, .slot_widths = widths,
        .block_count = 5, .edge_count = 4, .value_count = 1, .use_count = 2, .position_count = 28,
        .registers = { 1u << X86_REGISTER_RDI, 0, 0 },
        .scratch_registers = { BUSTER_TIER1_SELECT_SCRATCH, BUSTER_TIER1_SELECT_FLOAT_SCRATCH, 0 },
    };
    Tier1CfgAllocation original = {
        .intervals = { .starts = starts, .ends = ends, .values = values, .allowed_registers = masks, .classes = classes,
                       .use_offsets = interval_uses, .use_positions = uses, .use_required = required, .use_count = 2,
                       .count = 5, .value_count = 1, .position_count = 28, .registers = { 1u << X86_REGISTER_RDI, 0, 0 } },
        .allocation = { .fragment_offsets = fragments, .starts = fragment_starts, .ends = fragment_ends, .kinds = kinds, .registers = registers,
                        .fragment_count = 7, .interval_count = 5, .value_registers = summaries, .spill_ids = slots, .spill_values = slots, .spill_count = 1 },
        .value_interval_offsets = intervals,
    };
    uint32_t ir_blocks[] = { 0, 3, 6, 9, 13 };
    uint8_t operations[13], types[13], covered[13];
    uint32_t operands[13] = { 0 }, descriptors[13] = { 0 };
    uint32_t effective_offsets[14] = { 0 }, effective_values[] = { 0 };
    uint32_t node_values[14], value_nodes[] = { 0 }, sizes[14] = { 8 };
    Tier1SelectPattern patterns[13] = { 0 };
    memset(operations, IR_BR, sizeof(operations));
    memset(types, IR_VOID, sizeof(types));
    memset(covered, 1, sizeof(covered));
    memset(node_values, 0xff, sizeof(node_values));
    for (uint32_t n = 0; n < 13; n += 1)
    {
        for (uint32_t p = 0; p < BUSTER_TIER1_SELECT_VALUES; p += 1)
        {
            patterns[n].values[p] = BUSTER_IR_NONE;
        }
    }
    operations[0] = IR_PARAM;
    operations[1] = IR_CALL;
    operations[3] = IR_CALL;
    operations[12] = IR_RET;
    covered[1] = covered[3] = covered[12] = 0;
    node_values[0] = 0;
    effective_offsets[13] = 1;
    patterns[12].values[0] = 0;
    IrCall called = { .result_type = IR_VOID };
    IrFunction function = { .instruction_count = 13, .block_count = 4, .block_offsets = ir_blocks, .op = operations,
                            .type = types, .a = operands, .b = descriptors, .calls = &called };
    Tier1SelectPlan plan = { .function = &function, .covered = covered, .patterns = patterns,
                            .effective_use_offsets = effective_offsets, .effective_use_values = effective_values };
    Tier1EmitResult emitted = { .prepared = { .sizes = sizes }, .node_values = node_values, .value_nodes = value_nodes };
    Tier1EmitRecolorCall calls[] = { { .node = 1, .position = 5, .block = 1 }, { .node = 3, .position = 9, .block = 2 } };
    uint64_t weights[5] = { UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32 };
    uint64_t edge_weights[6] = { UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32, UINT64_C(1) << 32 };
    Tier1EmitRecolorModel model = { .calls = calls, .count = 2, .weights = weights, .edge_weights = edge_weights };
    Tier1EmitResult assembly = emitted;
    assembly.cfg_input = input;
    assembly.cfg = original;
    assembly.prepared.call_save_mask = 1u << X86_REGISTER_RDI;
    assembly.prepared.call_count = 2;
    assembly.prepared.asm_count = 1;
    uint64_t assembly_work = UINT64_MAX;
    const uint64_t assembly_mark = test->scratch.length;
    const uint32_t* assembly_rows = tier1_emit_cfg_parameter_rows_limit(&plan, &assembly, &test->scratch,
                                                                        BUSTER_TIER1_PARAMETER_ROW_WORK, &assembly_work);
    tier1_emit_test_expect(test, assembly_rows == 0 && assembly_work == 0 && test->scratch.length == assembly_mark,
                          "parameter-row retention refuses unmodeled simultaneous inline-assembly result owners before reading their metadata");
    Tier1CfgPrepared prepared = { 0 };
    bool ready = tier1_cfg_prepare(&input, &test->output, &test->scratch, &prepared) == TIER1_CFG_OK;
    ready = ready && tier1_cfg_finish(&input, &prepared, &test->output, &test->scratch, &original) == TIER1_CFG_OK;
    Tier1CfgCheck check = { 0 };
    tier1_emit_test_expect(test, ready && tier1_cfg_verify(&input, &original, &test->scratch, &check),
                          "the spill-region original has a real paid S/L home and an independently valid allocation");
    uint64_t work = 0;
    Tier1EmitSpillRegions regions = { 0 };
    Tier1EmitError error = ready ? tier1_emit_spill_regions_plan(&plan, &emitted, &input, &original, &model, 0, X86_REGISTER_RDI,
                                                                &test->output, &test->scratch, &work, &regions) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && regions.roots == 1 && regions.blocks == 3 && regions.calls[2] == 9 &&
                                  regions.entries[1] == 0 && regions.entries[2] != 0 && regions.entries[3] != 0 && regions.eligible[4] == 0,
                          "successive call blocks coalesce through a no-read relay, and a block-first seed is before the clobber");
    Tier1CfgAllocation candidate = original;
    error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_materialize(&input, &prepared, &original, &regions, 0, X86_REGISTER_RDI,
                                                                        &test->output, &test->scratch, &work, &candidate) : error;
    Tier1CertReport report = { 0 };
    Tier1CertCfgStats stats = { 0 };
    const bool certified = error == TIER1_EMIT_OK && tier1_certificate_cfg_check(&input, &candidate, &test->output, &test->scratch, &report, &stats) == TIER1_CERT_OK;
    tier1_emit_test_expect(test, certified && tier1_cfg_verify(&input, &candidate, &test->scratch, &check),
                          "the independent allocation certificate proves the real SPILL entries and reconstructed exit load");
    if (certified)
    {
        tier1_emit_test_expect(test, candidate.move_count == 1 && candidate.move_sources[0] == BUSTER_TIER1_CFG_SPILL &&
                                      candidate.move_destinations[0] == X86_REGISTER_RDI && candidate.current_slot_offsets[4] < candidate.current_slot_offsets[5],
                              "one exit edge reloads the original register and finish proves the ordinary home current afterward");
        uint32_t* old_counts = 0;
        uint32_t* new_counts = 0;
        error = tier1_emit_spill_regions_memory(&input, &original, 0, &test->scratch, &work, &old_counts);
        error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_memory(&input, &candidate, 0, &test->scratch, &work, &new_counts) : error;
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK && old_counts[4] == 2 && new_counts[1] == 1 && new_counts[4] == 1,
                              "exact S/L replay counts the new root store and elides the later store into the already current ordinary home");
        Tier1EmitSpillScore score = { 0 };
        error = tier1_emit_spill_regions_score(&plan, &emitted, &input, &original, &candidate, &model, 0, &test->scratch, &work, &score);
        uint32_t failed = 0;
        const Tier1EmitRegionPathStatus proof = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_proof(&input, &score, &test->scratch, &work, &failed) : TIER1_REGION_PATH_INVALID;
        bool witness = false;
        error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_witness(&input, score.records.block_gains, score.memory_edges, &test->scratch, &work, &witness) : error;
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK && proof == TIER1_REGION_PATH_OK && witness && score.memory == (__int128)2 * (UINT64_C(1) << 32),
                              "full lazy epochs and true S/L/edge costs prove two actual memory instructions saved on the complete call path");
        uint32_t bad_offsets[] = { 0, 0, 1, 1, 1, 1 };
        uint32_t bad_values[] = { 0 };
        Tier1CfgAllocation corrupted = original;
        corrupted.current_slot_offsets = bad_offsets;
        corrupted.current_slots = bad_values;
        const Tier1CertError bad = tier1_certificate_cfg_check(&input, &corrupted, &test->output, &test->scratch, &report, &stats);
        tier1_emit_test_expect(test, bad != TIER1_CERT_OK,
                              "an ordinary home asserted current before any real store is independently rejected");
        tier1_spill_region_native(test, &input, &candidate, &emitted.prepared, 1);
    }
    // Reuse the frozen REGISTER allocation with a two-root diamond, then remove one root's call. The latter's
    // mixed join keeps REGISTER, and its call-free arm must receive neither a seed nor an edge store.
    const uint32_t diamond_offsets[] = { 0, 2, 3, 4, 5, 5 };
    const uint32_t diamond_targets[] = { 1, 2, 3, 3, 4 };
    const uint32_t diamond_predecessors[] = { 0, 0, 1, 2, 3 };
    const uint32_t diamond_pre_offsets[] = { 0, 0, 1, 2, 4, 5 };
    input.successor_offsets = diamond_offsets;
    input.successors = diamond_targets;
    input.predecessor_offsets = diamond_pre_offsets;
    input.predecessors = diamond_predecessors;
    input.edge_count = 5;
    uint8_t placements[5] = { 0 };
    original.edge_placements = placements;
    for (uint32_t both = 0; both < 2; both += 1)
    {
        model.count = both != 0 ? 2 : 1;
        work = 0;
        error = tier1_emit_spill_regions_plan(&plan, &emitted, &input, &original, &model, 0, X86_REGISTER_RDI,
                                              &test->output, &test->scratch, &work, &regions);
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK && (regions.entries[3] != 0) == (both != 0) &&
                                      (both != 0 ? regions.roots == 2 : regions.roots == 1 && regions.entries[2] == 0 && regions.exits[2] == 0),
                              "unanimous independently seeded roots coalesce; a call-free bypass instead keeps the mixed join in REGISTER");
    }
    Tier1CfgPrepared diamond_prepared = { 0 };
    Tier1CfgAllocation diamond = original;
    ready = tier1_cfg_prepare(&input, &test->output, &test->scratch, &diamond_prepared) == TIER1_CFG_OK;
    diamond.split_count = 0;
    diamond.split_edges = 0;
    ready = ready && tier1_cfg_finish(&input, &diamond_prepared, &test->output, &test->scratch, &diamond) == TIER1_CFG_OK;
    work = 0;
    error = ready ? tier1_emit_spill_regions_materialize(&input, &diamond_prepared, &diamond, &regions, 0, X86_REGISTER_RDI,
                                                         &test->output, &test->scratch, &work, &candidate) : TIER1_EMIT_INVALID;
    const uint32_t odd_fragment = error == TIER1_EMIT_OK ? candidate.allocation.fragment_offsets[2] + 1 : 0;
    const bool odd_certified = error == TIER1_EMIT_OK && candidate.allocation.starts[odd_fragment] == 9 &&
                               candidate.allocation.kinds[odd_fragment] == BUSTER_TIER1_ALLOCATE_SPILL &&
                               tier1_certificate_cfg_check(&input, &candidate, &test->output, &test->scratch, &report, &stats) == TIER1_CERT_OK;
    tier1_emit_test_expect(test, odd_certified,
                          "a real block-first call's odd seed retains REGISTER entry and passes the independent allocation certificate");
    if (odd_certified)
    {
        tier1_spill_region_native(test, &input, &candidate, &emitted.prepared, 2);
    }
    // A reachable unseeded cycle has an entry path from REGISTER. Greatest-fixed-point initialization must not
    // circularly manufacture a current ordinary home in either loop block.
    const uint32_t loop_offsets[] = { 0, 1, 2, 4, 5, 5 };
    const uint32_t loop_targets[] = { 1, 2, 1, 3, 4 };
    const uint32_t loop_pre_offsets[] = { 0, 0, 2, 3, 4, 5 };
    const uint32_t loop_predecessors[] = { 0, 2, 1, 2, 3 };
    input.successor_offsets = loop_offsets;
    input.successors = loop_targets;
    input.predecessor_offsets = loop_pre_offsets;
    input.predecessors = loop_predecessors;
    model.count = 0;
    work = 0;
    error = tier1_emit_spill_regions_plan(&plan, &emitted, &input, &original, &model, 0, X86_REGISTER_RDI,
                                          &test->output, &test->scratch, &work, &regions);
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && regions.blocks == 0 && regions.entries[1] == 0 && regions.entries[2] == 0,
                          "an unseeded loop cannot prove its own ordinary slot current");
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->scratch, scratch_mark);
}
