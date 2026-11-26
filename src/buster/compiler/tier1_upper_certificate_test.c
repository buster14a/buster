// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3625: independent certificate mutations of the upper-vector fixtures. Included after
// tier1_upper_vectors_test.c. A call's actual upper-register saves and restores must carry every byte; removing
// both still fails, because the callee owns all 32 vector registers. A row's consistently renamed scratch may not
// overwrite a distinct allocated value. Illegal register numbers and legacy/VEX upper operands are refused.

static void tier1_upper_certificate_roles(Tier1EmitTest* test)
{
    static const uint16_t forms[] = {
        X86_FORM_MOVAPS_X_XM,
        X86_FORM_VMOVDQA_X_XM128,
        X86_FORM_VMOVDQA_Y_YM256,
        X86_FORM_VMOVDQA64_X_XM128,
        X86_FORM_VMOVDQA64_Y_YM256,
        X86_FORM_VMOVDQA64_Z_ZM512,
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(forms); f += 1)
    {
        for (uint32_t reg = 14; reg <= 32; reg += 1)
        {
            Tier1RowPlacement placement = { 0 };
            placement.vectors[0] = (uint8_t)reg;
            placement.vectors[1] = 1;
            const X86Instruction record = {
                .form = forms[f],
                .reg = BUSTER_TIER0_ROLE_VECTOR(0),
                .rm = BUSTER_TIER0_ROLE_VECTOR(1),
            };
            X86Instruction placed = { 0 };
            uint64_t named = 0;
            uint64_t explicit_registers = 0;
            uint32_t explicit_masks = 0;
            const bool accepted = tier1_cert_emitted_row_place(record, 0, &placement, &placed, &named, &explicit_registers, &explicit_masks);
            const bool expected = reg < (f < 3 ? 16u : 32u);
            tier1_emit_test_expect(test, accepted == expected && (!accepted || (placed.reg == reg && x86_validate(&placed, 1) == 1)),
                                   "a certificate row's upper registers obey its actual encoding, without truncation");
        }
    }
}

static bool tier1_upper_certificate_compile(Tier1EmitTest* test, uint32_t width, uint32_t shape, bool pointer, const uint8_t* forms, IrFunction* function, Tier1SelectPlan* plan, Tier1EmitResult* emitted)
{
    bool valid = tier1_upper_vectors_fixture(test, width, shape, function);
    valid = valid && ir_verify(function, &test->scratch, NULL).error == IR_OK;
    IrDominance dominance = { 0 };
    valid = valid && tier1_terminal_dominance(test, function, &dominance);
    uint32_t node = BUSTER_IR_NONE;
    valid = valid && tier1_select_plan(function, &dominance, &test->output, &test->scratch, plan, &node) == TIER1_SELECT_OK;
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .certify = true,
        .rematerialize = true,
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
    };
    const Tier1EmitError error = valid ? tier1_emit_function(plan, &options, forms, &test->output, &test->scratch, emitted, &node) : TIER1_EMIT_INVALID;
    valid = error == TIER1_EMIT_OK && emitted->general && emitted->code_checked && emitted->code_reason == TIER1_CERT_EMITTED_OK;
    if (!valid)
    {
        fprintf(stderr, "upper certificate fixture: width=%u shape=%u pointer=%u error=%u node=%u code_reason=%u\n", width, shape, pointer, error, node, emitted->code_reason);
    }
    tier1_emit_test_expect(test, valid, "the upper-register mutation fixture passes both allocation and emitted-code certificates");
    return valid;
}

static void tier1_upper_certificate_calls(Tier1EmitTest* test, uint32_t width, bool pointer, const uint8_t* forms)
{
    const uint64_t input_mark = test->input.length;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    IrFunction function = { 0 };
    Tier1SelectPlan plan = { 0 };
    Tier1EmitResult emitted = { 0 };
    const bool valid = tier1_upper_certificate_compile(test, width, 2, pointer, forms, &function, &plan, &emitted);
    uint32_t stored = UINT32_MAX;
    uint32_t loaded = UINT32_MAX;
    const Tier1SelectFloatForms* moves = tier1_select_class_forms(width, BUSTER_TIER1_ENCODING_EVEX);
    for (uint32_t r = 0; valid && stored == UINT32_MAX && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        const bool save = record.reg >= 16 && record.reg < 32 && record.mode == BUSTER_X86_MEMORY_BASE && record.form == moves->store &&
                          ((emitted.prepared.vector_save_mask >> record.reg) & 1) != 0;
        const uint32_t row = save ? tier1_emit_cfg_vector_save_row(&emitted, record.reg) : UINT32_MAX;
        if (row < emitted.frame.count && record.rm == emitted.frame.base_register && record.displacement == emitted.frame.offset[row])
        {
            for (uint32_t s = r + 1; loaded == UINT32_MAX && s < emitted.code.count; s += 1)
            {
                const X86Instruction later = emitted.code.records[s];
                const bool restore = later.form == moves->load && later.reg == record.reg && later.mode == record.mode && later.rm == record.rm && later.displacement == record.displacement;
                loaded = restore ? s : loaded;
            }
            stored = loaded != UINT32_MAX ? r : stored;
        }
    }
    const bool found = stored != UINT32_MAX && loaded != UINT32_MAX;
    tier1_emit_test_expect(test, found, "the certificate fixture really saves and restores a live allocated upper vector register across its call");
    if (found)
    {
        const X86Instruction store = emitted.code.records[stored];
        const X86Instruction load = emitted.code.records[loaded];
        for (uint32_t mutation = 0; mutation < 7; mutation += 1)
        {
            X86Instruction changed_store = store;
            X86Instruction changed_load = load;
            if (mutation < 3)
            {
                const uint16_t narrow_store = width > 16 ? tier1_select_class_forms(16, BUSTER_TIER1_ENCODING_EVEX)->store : X86_FORM_VMOVQ_XM64_X_EVEX;
                const uint16_t narrow_load = width > 16 ? tier1_select_class_forms(16, BUSTER_TIER1_ENCODING_EVEX)->load : X86_FORM_VMOVQ_X_XM64_EVEX;
                changed_store.form = mutation != 1 ? narrow_store : store.form;
                changed_load.form = mutation != 0 ? narrow_load : load.form;
            }
            else if (mutation == 3)
            {
                changed_store = tier1_select_class_move(store.reg, store.reg, width, BUSTER_TIER1_ENCODING_EVEX);
                changed_load = changed_store;
            }
            else if (mutation == 4)
            {
                changed_store.reg &= 15u;
            }
            else if (mutation == 5)
            {
                changed_load.reg = 32;
            }
            else
            {
                changed_load.form = width == 32 ? X86_FORM_VMOVDQU_Y_YM256 : X86_FORM_VMOVDQU_X_XM128;
            }
            emitted.code.records[stored] = changed_store;
            emitted.code.records[loaded] = changed_load;
            tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted),
                                   "narrowed, missing, aliased or unencodable upper-register call saves cannot be certified");
            emitted.code.records[stored] = store;
            emitted.code.records[loaded] = load;
        }
    }
    if (valid && width == 64 && !pointer)
    {
        uint32_t unused = UINT32_MAX;
        for (uint32_t r = 0; unused == UINT32_MAX && r < emitted.code.count; r += 1)
        {
            const X86Instruction record = emitted.code.records[r];
            const X86FormDescription* description = x86_form_description(record.form);
            unused = description->family == BUSTER_X86_FAMILY_VEX && description->vvvv == BUSTER_X86_OPERAND_NONE &&
                     x86_vex_canonical(record).form != record.form ? r : unused;
        }
        tier1_emit_test_expect(test, unused != UINT32_MAX, "the certificate fixture contains a VEX twin whose unused vvvv must remain zero");
        if (unused != UINT32_MAX)
        {
            const X86Instruction saved = emitted.code.records[unused];
            emitted.code.records[unused].vvvv = 31;
            tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted),
                                   "canonicalization cannot erase an illegal upper register in an unused VEX operand");
            emitted.code.records[unused] = saved;
        }
    }
    arena_pop_to(&test->scratch, scratch_mark);
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->input, input_mark);
}

static void tier1_upper_certificate_moves(Tier1EmitTest* test, uint32_t width, const uint8_t* forms)
{
    const uint64_t input_mark = test->input.length;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    IrFunction function = { 0 };
    Tier1SelectPlan plan = { 0 };
    Tier1EmitResult emitted = { 0 };
    const bool valid = tier1_upper_certificate_compile(test, width, 1, false, forms, &function, &plan, &emitted);
    const uint16_t form = tier1_select_vector_forms(width, BUSTER_TIER1_ENCODING_EVEX)->move;
    uint32_t moved = UINT32_MAX;
    for (uint32_t r = 0; valid && moved == UINT32_MAX && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        const bool upper = (record.reg >= 16 && record.reg < 32) || (record.rm >= 16 && record.rm < 32);
        moved = upper && record.form == form && record.mode == BUSTER_X86_REGISTER_DIRECT && record.reg != record.rm &&
                tier1_cert_emitted_kind(emitted.origins[r]) == TIER1_ORIGIN_EDGE ? r : moved;
    }
    tier1_emit_test_expect(test, moved != UINT32_MAX, "the certificate loop fixture copies an upper vector register on a phi edge");
    if (moved != UINT32_MAX)
    {
        const X86Instruction saved = emitted.code.records[moved];
        for (uint32_t mutation = 0; mutation < 4; mutation += 1)
        {
            X86Instruction changed = saved;
            if (mutation == 0)
            {
                changed.form = width > 16 ? tier1_select_vector_forms(16, BUSTER_TIER1_ENCODING_EVEX)->move : X86_FORM_VMOVQ_X_XM64_EVEX;
            }
            else if (mutation == 1)
            {
                changed.reg &= changed.reg >= 16 ? 15u : 31u;
                changed.rm &= changed.rm >= 16 ? 15u : 31u;
            }
            else if (mutation == 2)
            {
                changed.reg = 32;
            }
            else
            {
                changed.form = width == 32 ? X86_FORM_VMOVDQA_Y_YM256 : X86_FORM_VMOVDQA_X_XM128;
            }
            emitted.code.records[moved] = changed;
            tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted),
                                   "narrowed, aliased or unencodable upper-register phi copies cannot be certified");
            emitted.code.records[moved] = saved;
        }
    }
    arena_pop_to(&test->scratch, scratch_mark);
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->input, input_mark);
}

static void tier1_upper_certificate_scratch(Tier1EmitTest* test, const uint8_t* forms)
{
    const uint64_t input_mark = test->input.length;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    IrFunction function = { 0 };
    Tier1SelectPlan plan = { 0 };
    Tier1EmitResult emitted = { 0 };
    const bool valid = tier1_upper_certificate_compile(test, 64, 3, false, forms, &function, &plan, &emitted);
    uint32_t node = BUSTER_IR_NONE;
    for (uint32_t i = 0; valid && node == BUSTER_IR_NONE && i < function.instruction_count; i += 1)
    {
        node = plan.patterns[i].rule == TIER1_RULE_ROW ? i : node;
    }
    const uint32_t position = valid ? 2 * (emitted.cfg_input.block_count - function.block_count) + 2 * node : 0;
    uint32_t victim = UINT32_MAX;
    uint32_t fragment = UINT32_MAX;
    for (uint32_t v = 0; valid && node != BUSTER_IR_NONE && victim == UINT32_MAX && v < emitted.cfg_input.value_count; v += 1)
    {
        uint32_t location = UINT32_MAX;
        uint8_t kind = UINT8_MAX;
        const bool at = emitted.cfg_input.classes[v] == BUSTER_TIER1_ALLOCATE_VECTOR && tier1_cfg_location(&emitted.cfg, v, position + 1, &location, &kind);
        const uint32_t reg = location - BUSTER_TIER1_EMIT_VECTOR_LOCATION;
        const Tier1RowPlacement* placement = &emitted.row_placements[node];
        const bool other = reg != placement->vectors[0] && reg != placement->vectors[1] && reg != placement->vectors[2] && reg != placement->early;
        if (at && kind == BUSTER_TIER1_ALLOCATE_REGISTER && reg >= 16 && reg < 32 && other)
        {
            victim = reg;
            const uint32_t interval = tier1_cfg_interval_at(&emitted.cfg, v, position + 1);
            fragment = tier1_cfg_interval_fragment(&emitted.cfg, interval, position + 1);
        }
    }
    uint32_t first = UINT32_MAX;
    for (uint32_t r = 0; valid && node != BUSTER_IR_NONE && first == UINT32_MAX && r < emitted.code.count; r += 1)
    {
        first = emitted.origins[r] == tier1_origin(TIER1_ORIGIN_NODE, node) ? r : first;
    }
    const bool found = first != UINT32_MAX && victim != UINT32_MAX && fragment < emitted.cfg.allocation.fragment_count;
    tier1_emit_test_expect(test, found, "a row scratch register has a distinct allocated upper-register value live beside it");
    if (found)
    {
        const uint8_t saved_register = emitted.cfg.allocation.registers[fragment];
        const uint8_t scratch = emitted.row_placements[node].vectors[2];
        emitted.cfg.allocation.registers[fragment] = scratch;
        const Tier1CertError collision = tier1_certificate_cfg_check(&emitted.cfg_input, &emitted.cfg, &test->output, &test->scratch, NULL, NULL);
        tier1_emit_test_expect(test, collision != TIER1_CERT_OK, "the allocation certificate rejects a live value placed in a reserved row scratch register");
        emitted.cfg.allocation.registers[fragment] = 32;
        const Tier1CertError illegal = tier1_certificate_cfg_check(&emitted.cfg_input, &emitted.cfg, &test->output, &test->scratch, NULL, NULL);
        tier1_emit_test_expect(test, illegal != TIER1_CERT_OK, "the allocation certificate rejects register 32 rather than truncating it");
        emitted.cfg.allocation.registers[fragment] = saved_register;

        const Tier0Operation* row = &function.rows[(uint32_t)plan.patterns[node].immediate];
        X86Instruction saved[BUSTER_TIER0_OPERATION_RECORDS];
        memcpy(saved, emitted.code.records + first, (uint64_t)row->record_count * sizeof(X86Instruction));
        emitted.row_placements[node].vectors[2] = (uint8_t)victim;
        bool placed = true;
        for (uint32_t r = 0; placed && r < row->record_count; r += 1)
        {
            uint64_t named = 0;
            uint64_t explicit_registers = 0;
            uint32_t explicit_masks = 0;
            placed = tier1_cert_emitted_row_place(row->records[r], r, &emitted.row_placements[node], &emitted.code.records[first + r], &named, &explicit_registers, &explicit_masks);
        }
        tier1_emit_test_expect(test, placed && tier1_code_certificate_refused(test, &plan, &emitted),
                               "the emitted certificate rejects a consistently renamed row scratch that destroys another live upper-register value");
        emitted.row_placements[node].vectors[2] = scratch;
        memcpy(emitted.code.records + first, saved, (uint64_t)row->record_count * sizeof(X86Instruction));
    }
    arena_pop_to(&test->scratch, scratch_mark);
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->input, input_mark);
}

static void tier1_upper_certificate_tests(Tier1EmitTest* test)
{
    uint8_t* forms = malloc(X86_FORM_COUNT);
    X86Target target = { 0 };
    const bool ready = forms != NULL && x86_target_parse("x86-64-v4", 9, &target);
    tier1_emit_test_expect(test, ready, "upper-register certificate target forms");
    if (ready)
    {
        x86_target_forms(&target, forms);
        tier1_upper_certificate_roles(test);
        for (uint32_t width = 16; width <= 64; width *= 2)
        {
            for (uint32_t pointer = 0; pointer < 2; pointer += 1)
            {
                tier1_upper_certificate_calls(test, width, pointer != 0, forms);
            }
            tier1_upper_certificate_moves(test, width, forms);
        }
        tier1_upper_certificate_scratch(test, forms);
    }
    free(forms);
}
