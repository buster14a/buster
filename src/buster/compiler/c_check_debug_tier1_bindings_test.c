// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse scalar proofs for test-check-debug (#1114), included after c_check_tier1.c. Each expected cell gives the
// actual whole-value carrier before a record: register 0..31, one exact frame displacement, literal bytes or NONE.
// The complete constructor, CFG fixed point and incremental count/fill publisher all run against these tables.
// Final-order arrays include the driver's terminal record_order[count] == count sentinel.

#define BUSTER_CHECK_DEBUG_VALUE_MEMORY(offset) (UINT32_C(0x40000000) | (uint16_t)(offset))
#define BUSTER_CHECK_DEBUG_VALUE_LITERAL(value) (UINT32_C(0x80000000) | (value))

static void check_debug_tier1_values_binding_case(CheckDebugTestTally* tally, const Tier0Function* function, const IrFunction* ir,
                                                const Tier1EmitResult* emitted, const uint32_t* order, const uint32_t* entries,
                                                const uint8_t* safe, uint32_t epilogue, const uint32_t* expected, const char* description)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        _Alignas(64) uint8_t storage[32768] = { 0 };
        Arena scratch = { .base = storage, .capacity = sizeof(storage) };
        CheckTier1DebugValues proof = { 0 };
        EmittedLocalRange locations[128] = { 0 };
        uint32_t actual[128];
        memset(actual, 0xff, sizeof(actual));
        const uint64_t cells = (uint64_t)function->local_count * emitted->code.count;
        bool valid = cells <= BUSTER_ARRAY_LENGTH(actual) && check_tier1_debug_values(function, ir, emitted, order, path != 0, &scratch, &proof);
        const uint64_t count = valid ? check_tier1_debug_value_ranges(&proof, entries, safe, emitted->body_record, epilogue, path != 0, NULL) : 0;
        valid = valid && count <= BUSTER_ARRAY_LENGTH(locations);
        valid = valid && check_tier1_debug_value_ranges(&proof, entries, safe, emitted->body_record, epilogue, path != 0, locations) == count;
        for (uint64_t k = 0; valid && k < count; k += 1)
        {
            const EmittedLocalRange location = locations[k];
            valid = location.local < function->local_count && location.begin < location.end && location.end <= emitted->code.count;
            uint32_t value = BUSTER_IR_NONE;
            if (valid && location.value_bytes != 0)
            {
                valid = location.value_bytes == tier0_class_bytes(function->local_classes[location.local]) && location.value < UINT32_C(0x40000000);
                value = BUSTER_CHECK_DEBUG_VALUE_LITERAL((uint32_t)location.value);
            }
            else if (valid && location.home.is_register)
            {
                valid = location.home.reg < 16 && location.home.mode == 0;
                value = location.home.reg + (tier0_class_floating(function->local_classes[location.local]) ? 16 : 0);
            }
            else if (valid)
            {
                valid = location.home.reg == X86_REGISTER_RSP && location.home.mode == BUSTER_X86_MEMORY_BASE &&
                        location.home.displacement >= INT16_MIN && location.home.displacement <= INT16_MAX;
                value = BUSTER_CHECK_DEBUG_VALUE_MEMORY(location.home.displacement);
            }
            for (uint32_t r = location.begin; valid && r < location.end; r += 1)
            {
                const uint64_t cell = (uint64_t)location.local * emitted->code.count + r;
                valid = actual[cell] == BUSTER_IR_NONE;
                actual[cell] = value;
            }
        }
        check_debug_test_expect(tally, valid && memcmp(actual, expected, cells * sizeof(uint32_t)) == 0,
                                "sparse scalar ranges match independently specified whole-value carriers on both paths", description);
    }
}

// The static CFG meet alone can hide a stale-iteration bug by discarding a first-trip-absent row. Walk the concrete
// backedge twice too: the same static phi/definition IDs have new dynamic values, while their old private spills
// still contain the preceding iteration's bytes. All observed source PCs stay inside the same witnessed region.
static void check_debug_tier1_values_fresh(CheckDebugTestTally* tally)
{
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64 };
    AbiLocation abi[] = { { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } } };
    const Tier0Function function = { .count = 32, .local_classes = classes, .local_count = 3, .parameters = abi, .parameter_count = 1 };
    uint8_t operations[] = { IR_PARAM, IR_CONST, IR_BR, IR_PHI, IR_ADD, IR_ICMP, IR_CBR, IR_RET };
    uint8_t types[] = { IR_I64, IR_I64, IR_VOID, IR_I64, IR_I64, IR_I1, IR_VOID, IR_VOID };
    uint32_t blocks[] = { 0, 3, 7, 8 };
    IrDebugBinding bindings[] = { { .local = 0, .entry = BUSTER_IR_NONE, .value = 0, .anchor = 0, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 } };
    uint8_t flags[] = { BUSTER_IR_DEBUG_BIND_ENTRY, BUSTER_IR_DEBUG_BIND_REGIONS, BUSTER_IR_DEBUG_BIND_REGIONS };
    IrDebugRegion regions[] = {
        { .local = 1, .block = 1, .begin = 10, .end = 20, .block_end = 20, .value = 3, .next = BUSTER_IR_NONE, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 },
        { .local = 2, .block = 1, .begin = 10, .end = 20, .block_end = 20, .value = 4, .next = BUSTER_IR_NONE, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 },
    };
    IrDebugInfo debug = { .bindings = bindings, .binding_count = 1, .binding_flags = flags, .first_block_end = 10, .regions = regions, .region_count = 2 };
    IrFunction ir = { .op = operations, .type = types, .instruction_count = 8, .block_offsets = blocks, .block_count = 3, .debug = &debug };
    X86Instruction records[] = {
        { .form = X86_FORM_SUB_RM64_IMM8, .mode = BUSTER_X86_REGISTER_DIRECT, .rm = X86_REGISTER_RSP, .immediate = 32 },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_MOV_RM64_R64, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .displacement = 8 },
        { .form = X86_FORM_ADD_RM64_IMM8, .mode = BUSTER_X86_REGISTER_DIRECT, .rm = X86_REGISTER_RAX, .immediate = 1 },
        { .form = X86_FORM_MOV_RM64_R64, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .displacement = 16 },
        { .form = X86_FORM_CMP_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_JNE_REL32 },
        { .form = X86_FORM_ADD_RM64_IMM8, .mode = BUSTER_X86_REGISTER_DIRECT, .rm = X86_REGISTER_RSP, .immediate = 32 },
        { .form = X86_FORM_RET },
    };
    uint32_t origins[] = {
        tier1_origin(TIER1_ORIGIN_FRAME, 0), tier1_origin(TIER1_ORIGIN_NODE, 0), tier1_origin(TIER1_ORIGIN_NODE, 2),
        tier1_origin(TIER1_ORIGIN_NODE, 3), tier1_origin(TIER1_ORIGIN_NODE, 4), tier1_origin(TIER1_ORIGIN_NODE, 4),
        tier1_origin(TIER1_ORIGIN_NODE, 5), tier1_origin(TIER1_ORIGIN_NODE, 6), tier1_origin(TIER1_ORIGIN_FRAME, 1), tier1_origin(TIER1_ORIGIN_NODE, 7),
    };
    uint32_t branches[] = { 7 };
    uint32_t targets[] = { 3 };
    uint32_t block_records[] = { 0, 3, 8, 10 };
    uint32_t physical[] = { 0, 1 };
    int32_t offsets[] = { 8, 16 };
    uint32_t sizes[] = { 8, 8, 0, 8, 8, 1, 0, 0 };
    uint32_t value_nodes[] = { 3, 4 };
    uint32_t spill_values[] = { 0, 1 };
    Tier1DebugDefinition definitions[8];
    Tier1DebugPhi phis[8];
    for (uint32_t node = 0; node < ir.instruction_count; node += 1)
    {
        definitions[node] = (Tier1DebugDefinition){ .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE };
        phis[node] = (Tier1DebugPhi){ .record = BUSTER_IR_NONE, .block = BUSTER_IR_NONE };
    }
    definitions[4] = (Tier1DebugDefinition){ .first = 4, .end = 5, .reg = X86_REGISTER_RAX, .class = BUSTER_TIER1_ALLOCATE_GPR, .bytes = 8 };
    phis[3] = (Tier1DebugPhi){ .record = 3, .block = 1, .reg = X86_REGISTER_RAX, .class = BUSTER_TIER1_ALLOCATE_GPR, .bytes = 8 };
    Tier1EmitResult emitted = {
        .code = { .records = records, .count = BUSTER_ARRAY_LENGTH(records), .branches = branches, .targets = targets, .branch_count = 1 },
        .origins = origins, .body_record = 1, .block_records = block_records, .general = true, .cfg_input = { .block_count = 3 },
        .cfg = { .allocation = { .spill_count = 2, .spill_values = spill_values } },
        .prepared = { .function = &ir, .sizes = sizes, .slot_columns = BUSTER_ARRAY_LENGTH(sizes) }, .value_nodes = value_nodes,
        .frame = { .count = 2, .slot_count = 2, .slot = physical, .offset = offsets, .base_register = X86_REGISTER_RSP, .stack_adjust_bytes = 32, .payload_bytes = 32 },
        .debug_definitions = definitions, .debug_phis = phis,
    };
    const uint32_t memory_phi = BUSTER_CHECK_DEBUG_VALUE_MEMORY(8);
    const uint32_t memory_result = BUSTER_CHECK_DEBUG_VALUE_MEMORY(16);
    const uint32_t expected[][BUSTER_ARRAY_LENGTH(records)] = {
        { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RAX, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI },
        { BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, X86_REGISTER_RAX, memory_phi, memory_phi, memory_phi, memory_phi, BUSTER_IR_NONE, BUSTER_IR_NONE },
        { BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, X86_REGISTER_RAX, memory_result, memory_result, BUSTER_IR_NONE, BUSTER_IR_NONE },
    };
    const uint32_t entries[] = { 0, 1, 2, 15, 15, 15, 15, 15, 20, 21 };
    const uint8_t safe[] = { 0, 1, 1, 1, 1, 1, 1, 1, 0, 0 };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, expected[0],
                                         "loop phi and result locations have exact carriers and initialization holes inside one witnessed source region");
    for (uint32_t path = 0; path < 2; path += 1)
    {
        _Alignas(64) uint8_t storage[32768] = { 0 };
        Arena scratch = { .base = storage, .capacity = sizeof(storage) };
        CheckTier1DebugValues proof = { 0 };
        bool valid = check_tier1_debug_values(&function, &ir, &emitted, NULL, path != 0, &scratch, &proof);
        check_debug_test_expect(tally, valid, "the dynamic backedge trace constructs the real bounded region and machine proof", "fresh phi and repeated definition identities");
        if (valid)
        {
            CheckTier1DebugValueLive live = { 0 };
            check_tier1_debug_value_start(&proof, &live);
            memset(proof.winner, 0xff, ((uint64_t)proof.value_count + 1) * 4);
            memset(live.users, 0xff, ((uint64_t)proof.value_count + 1) * 4);
            check_tier1_debug_value_change(&proof, &live, X86_REGISTER_RDI, 0);
            for (uint32_t r = 0; r < 3; r += 1)
            {
                check_tier1_debug_value_boundary(&proof, &live, r);
                check_tier1_debug_value_follow(&proof, &live, proof.transfers[r], path != 0);
            }
            // Observe each loop PC before executing it, then take the actual backedge once. Registers/private
            // rows use carrier IDs here (32 and 33); these winners are specified independently of the producer.
            const uint32_t trace[] = { 3, 4, 5, 6, 7, 3, 4, 5, 6, 7 };
            const uint32_t phi_winners[] = { X86_REGISTER_RAX, 32, 32, 32, 32, X86_REGISTER_RAX, 32, 32, 32, 32 };
            const uint32_t result_winners[] = { BUSTER_IR_NONE, BUSTER_IR_NONE, X86_REGISTER_RAX, 33, 33, BUSTER_IR_NONE, BUSTER_IR_NONE, X86_REGISTER_RAX, 33, 33 };
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(trace); k += 1)
            {
                const uint32_t r = trace[k];
                check_tier1_debug_value_boundary(&proof, &live, r);
                check_tier1_debug_value_winners(&proof, &live, false);
                const bool regions_visible = check_tier1_debug_value_region(&proof, 1, r, 15) == 0 && check_tier1_debug_value_region(&proof, 2, r, 15) == 1;
                const bool empty_old_rows = r != 3 || (proof.state[32] == BUSTER_IR_NONE && proof.state[33] == BUSTER_IR_NONE);
                check_debug_test_expect(tally, regions_visible && empty_old_rows && proof.winner[3] == phi_winners[k] && proof.winner[4] == result_winners[k],
                                        "a backedge starts fresh phi and RHS identities before any stale private row can win", "two concrete iterations with unchanged witnessed source origins");
                check_tier1_debug_value_follow(&proof, &live, proof.transfers[r], path != 0);
            }
            // Isolate completion invalidation as well: no fresh-block boundary intervenes between the old spill
            // and another execution of this checked definition. Its new register must beat no surviving old row.
            check_tier1_debug_value_follow(&proof, &live, proof.transfers[4], path != 0);
            check_tier1_debug_value_winners(&proof, &live, false);
            check_debug_test_expect(tally, proof.state[33] == BUSTER_IR_NONE && proof.state[X86_REGISTER_RAX] == 4 && proof.winner[4] == X86_REGISTER_RAX &&
                                    check_tier1_debug_value_region(&proof, 2, 5, 15) == 1,
                                    "reexecuting a checked definition discards every older same-ID spill before publishing the new register", "ordinary definition freshness independent of block-entry clearing");
        }
    }
}

static void check_debug_tier1_values_bindings(CheckDebugTestTally* tally)
{
    uint8_t classes[] = { BUSTER_TYPED_CLASS_I64 };
    AbiLocation abi[] = { { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } } };
    Tier0Function function = { .count = 2, .local_classes = classes, .local_count = 1, .parameters = abi, .parameter_count = 1 };
    uint8_t types[] = { IR_I64, IR_VOID, IR_VOID, IR_VOID };
    IrDebugBinding bindings[] = { { .local = 0, .entry = BUSTER_IR_NONE, .value = 0, .anchor = 0, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 } };
    uint8_t flags[] = { BUSTER_IR_DEBUG_BIND_ENTRY };
    IrDebugInfo debug = { .bindings = bindings, .binding_count = 1, .binding_flags = flags, .first_block_end = 2 };
    IrFunction ir = { .type = types, .instruction_count = 2, .debug = &debug };
    X86Instruction records[] = {
        { .form = X86_FORM_SUB_RM64_IMM8, .mode = BUSTER_X86_REGISTER_DIRECT, .rm = X86_REGISTER_RSP, .immediate = 32 },
        { .form = X86_FORM_MOV_RM64_R64, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSP, .displacement = 8 },
        { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_CALL_REL32 },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_RSP, .displacement = 8 },
        { .form = X86_FORM_MOV_RM8_R8, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .displacement = 11 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_ADD_RM64_IMM8, .mode = BUSTER_X86_REGISTER_DIRECT, .rm = X86_REGISTER_RSP, .immediate = 32 },
        { .form = X86_FORM_RET },
    };
    uint32_t origins[BUSTER_ARRAY_LENGTH(records)];
    uint32_t entries[BUSTER_ARRAY_LENGTH(records)];
    uint8_t safe[BUSTER_ARRAY_LENGTH(records)] = { 0, 1, 1, 1, 1, 1, 1, 1, 0, 0 };
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(records); r += 1)
    {
        origins[r] = tier1_origin(r == 0 || r == 8 ? TIER1_ORIGIN_FRAME : TIER1_ORIGIN_NODE, r == 8 ? 1 : 0);
        entries[r] = 1;
    }
    uint32_t physical[] = { 0, 0 };
    int32_t offsets[] = { 8, 8 };
    uint32_t sizes[] = { 8, 0 };
    uint32_t value_nodes[] = { 0 };
    uint32_t spill_values[] = { 0 };
    Tier1EmitResult emitted = {
        .code = { .records = records, .count = BUSTER_ARRAY_LENGTH(records) }, .origins = origins, .body_record = 1,
        .frame = { .count = 1, .slot_count = 1, .slot = physical, .offset = offsets, .base_register = X86_REGISTER_RSP, .stack_adjust_bytes = 32, .payload_bytes = 32 },
        .general = true, .cfg = { .allocation = { .spill_count = 1, .spill_values = spill_values } },
        .prepared = { .function = &ir, .sizes = sizes, .slot_columns = BUSTER_ARRAY_LENGTH(sizes) }, .value_nodes = value_nodes,
    };
    const uint32_t memory = BUSTER_CHECK_DEBUG_VALUE_MEMORY(8);
    const uint32_t saved[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, memory, memory, memory, memory, memory, X86_REGISTER_R10, X86_REGISTER_R10, X86_REGISTER_R10 };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, saved,
                                         "a private compiler row survives a call, reloads exactly and its register copy survives a partial row overwrite");
    records[1].form = X86_FORM_MOV_RM32_R32;
    const uint32_t partial[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RDI, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, partial,
                                         "a four-byte store cannot preserve an eight-byte parameter across its register overwrite");
    records[1].form = X86_FORM_MOV_RM64_R64;
    records[3] = records[6];
    records[3].displacement = 15;
    const uint32_t clobbered[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, memory, memory, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, clobbered,
                                         "one foreign byte at the end of a cached scalar kills the whole row and its later reload");
    records[3].displacement = 16;
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, saved,
                                         "a write immediately past the cached scalar cannot kill a disjoint private row");
    records[3] = (X86Instruction){ .form = X86_FORM_NOP };
    emitted.frame.count = 2;
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, clobbered,
                                         "a compiler row shared with an addressable object is unavailable at the call and after its unknown alias writes");
    emitted.frame.count = 1;
    records[3] = records[6];
    records[3].rm = X86_REGISTER_RAX;
    records[3].displacement = 0;
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, entries, safe, 8, saved,
                                         "an unknown pointer store kills addressable rows but cannot reach private compiler storage");

    // A source assignment changes only after its checked RHS completes. The old source origin at record 5 is
    // deliberately later in machine order: it must not revive literal 7 after the call result became current.
    function.count = 12;
    function.parameter_count = 0;
    classes[0] = BUSTER_TYPED_CLASS_I32;
    types[0] = IR_I32;
    types[1] = IR_I32;
    ir.instruction_count = 4;
    IrDebugBinding assigned[] = {
        { .literal = 7, .local = 0, .entry = 1, .value = BUSTER_IR_NONE, .anchor = 0, .kind = IR_DEBUG_BIND_CONSTANT, .bytes = 4, .type = IR_I32 },
        { .local = 0, .entry = 5, .value = 1, .anchor = 1, .kind = IR_DEBUG_BIND_VALUE, .bytes = 4, .type = IR_I32 },
    };
    debug.bindings = assigned;
    debug.binding_count = 2;
    debug.first_block_end = 12;
    X86Instruction sequence[] = {
        { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RCX, .immediate = 7 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_CALL_REL32 },
        { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RCX },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_RET },
    };
    Tier1DebugDefinition definitions[] = {
        { .first = 0, .end = 1, .reg = X86_REGISTER_RCX, .class = BUSTER_TIER1_ALLOCATE_GPR, .bytes = 4 },
        { .first = 2, .end = 4, .reg = X86_REGISTER_RDX, .class = BUSTER_TIER1_ALLOCATE_GPR, .bytes = 4 },
        { .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE },
        { .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE },
    };
    uint32_t sequence_origins[] = {
        tier1_origin(TIER1_ORIGIN_NODE, 0), tier1_origin(TIER1_ORIGIN_NODE, 0), tier1_origin(TIER1_ORIGIN_NODE, 1),
        tier1_origin(TIER1_ORIGIN_NODE, 1), tier1_origin(TIER1_ORIGIN_NODE, 2), tier1_origin(TIER1_ORIGIN_NODE, 0),
        tier1_origin(TIER1_ORIGIN_NODE, 2), tier1_origin(TIER1_ORIGIN_NODE, 2), tier1_origin(TIER1_ORIGIN_NODE, 3),
    };
    uint32_t sequence_entries[] = { 1, 2, 4, 5, 6, 4, 6, 6, 11 };
    uint8_t sequence_safe[] = { 1, 1, 1, 1, 1, 1, 1, 1, 0 };
    emitted = (Tier1EmitResult){ .code = { .records = sequence, .count = BUSTER_ARRAY_LENGTH(sequence) }, .origins = sequence_origins, .debug_definitions = definitions };
    const uint32_t literal = BUSTER_CHECK_DEBUG_VALUE_LITERAL(7);
    const uint32_t completed[] = { BUSTER_IR_NONE, literal, literal, BUSTER_IR_NONE, X86_REGISTER_RDX, BUSTER_IR_NONE, X86_REGISTER_RDX, BUSTER_IR_NONE, BUSTER_IR_NONE };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, sequence_entries, sequence_safe, 8, completed,
                                         "the previous literal remains current during the call, the result starts after its copy, and an older origin cannot revive it");
    uint32_t delayed_order[] = { 0, 1, 2, 4, 3, 5, 6, 7, 8, 9 };
    uint32_t delayed_entries[] = { 1, 2, 4, 6, 5, 4, 6, 6, 11 };
    const uint32_t delayed[] = { BUSTER_IR_NONE, literal, literal, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, X86_REGISTER_RDX, BUSTER_IR_NONE, BUSTER_IR_NONE };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, delayed_order, delayed_entries, sequence_safe, 8, delayed,
                                         "a future source origin scheduled before the result copy cannot expose an incomplete RHS");
    uint32_t absent[BUSTER_ARRAY_LENGTH(sequence)];
    memset(absent, 0xff, sizeof(absent));
    definitions[1].end = 5;
    sequence_origins[4] = tier1_origin(TIER1_ORIGIN_NODE, 1);
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, sequence_entries, sequence_safe, 8, delayed,
                                         "a checked multi-record sequence does not finish merely because its result register was already written");
    uint32_t overwritten_order[] = { 0, 1, 2, 3, 6, 4, 5, 7, 8, 9 };
    uint32_t overwritten_entries[] = { 1, 2, 4, 5, 6, 6, 4, 6, 11 };
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, overwritten_order, overwritten_entries, sequence_safe, 8, absent,
                                         "a foreign result-register overwrite between the kernel's last write and completion prevents a false seed");
    definitions[1].end = 4;
    sequence_origins[4] = tier1_origin(TIER1_ORIGIN_NODE, 2);
    definitions[1].bytes = 2;
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, sequence_entries, sequence_safe, 8, absent,
                                         "an insufficient-width definition cannot seed a scalar or bound its previous assignment safely");
    definitions[1].bytes = 4;
    emitted.debug_definitions = NULL;
    check_debug_tier1_values_binding_case(tally, &function, &ir, &emitted, NULL, sequence_entries, sequence_safe, 8, absent,
                                         "without a checked later RHS, old source origins cannot justify stale assignment ranges");
    check_debug_tier1_values_fresh(tally);
}

#undef BUSTER_CHECK_DEBUG_VALUE_MEMORY
#undef BUSTER_CHECK_DEBUG_VALUE_LITERAL
