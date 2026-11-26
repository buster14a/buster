// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2829: the admitted single-record floating rows keep their original instruction and operand order when r/m takes
// a load. The native oracle is that original register instruction, with explicit loads and a store, independent of
// selection, allocation and the memory kernel. Compare all result bits and MXCSR's exception flags, including NaN
// payloads, signed zeros, subnormals and all rounding modes. The constructed IR also exercises refusal boundaries,
// frame and symbol addresses, target allowlists and a certificate mutation.

typedef struct Tier1FloatRowLoadCase Tier1FloatRowLoadCase;
struct Tier1FloatRowLoadCase
{
    uint16_t form;
    uint8_t source_bytes;
    uint8_t result_bytes;
    uint8_t operands;
    bool scalar;
    bool aligned;
};

static const Tier1FloatRowLoadCase tier1_float_row_load_cases[] = {
    { X86_FORM_SQRTSS_X_XM32, 4, 4, 1, true, false },
    { X86_FORM_SQRTSD_X_XM64, 8, 8, 1, true, false },
    { X86_FORM_VSQRTSS_X_X_XM32, 4, 4, 1, true, false },
    { X86_FORM_VSQRTSD_X_X_XM64_EVEX, 8, 8, 1, true, false },
    { X86_FORM_MINPS_X_XM128, 16, 16, 2, false, true },
    { X86_FORM_MAXPD_X_XM128, 16, 16, 2, false, true },
    { X86_FORM_SQRTPS_X_XM128, 16, 16, 1, false, true },
    { X86_FORM_VMINPS_X_X_XM128, 16, 16, 2, false, false },
    { X86_FORM_VMAXPD_Y_Y_YM256, 32, 32, 2, false, false },
    { X86_FORM_VMINPS_Z_Z_ZM512, 64, 64, 2, false, false },
    { X86_FORM_VSQRTPS_X_XM128, 16, 16, 1, false, false },
    { X86_FORM_VSQRTPD_Y_YM256, 32, 32, 1, false, false },
    { X86_FORM_VSQRTPS_Z_ZM512, 64, 64, 1, false, false },
    { X86_FORM_VADDPS_Y_Y_YM256, 32, 32, 2, false, false },
    { X86_FORM_VSUBPD_X_X_XM128, 16, 16, 2, false, false },
    { X86_FORM_VMULPS_Z_Z_ZM512, 64, 64, 2, false, false },
    { X86_FORM_VDIVPD_Y_Y_YM256, 32, 32, 2, false, false },
    { X86_FORM_CVTPS2PD_X_XM64, 8, 16, 1, false, true },
    { X86_FORM_VCVTPS2PD_Y_XM128, 16, 32, 1, false, false },
    { X86_FORM_VCVTPD2PS_X_YM256, 32, 16, 1, false, false },
    { X86_FORM_VCVTDQ2PS_Y_YM256, 32, 32, 1, false, false },
    { X86_FORM_VCVTTPS2DQ_Z_ZM512, 64, 64, 1, false, false },
    { X86_FORM_CVTSS2SD_X_XM32, 4, 8, 1, true, false },
    { X86_FORM_VCVTSD2SS_X_X_XM64, 8, 4, 1, true, false },
    { X86_FORM_VCVTSS2SD_X_X_XM32_EVEX, 4, 8, 1, true, false },
};

// Shape 0 ordinary; 1 volatile, 2 shared load, 3 intervening store, 4 intervening load, 5 fence, 6 unaligned legacy,
// 7 the same argument in both machine inputs, 8 a two-record row; 9 a frame object and 10 a direct symbol;
// 11 through 13 a surviving first input of 4, 8 or 16 bytes, narrower than its machine register field.
static bool tier1_float_row_load_function(Tier1EmitTest* test, const Tier1FloatRowLoadCase* entry, uint32_t shape,
                                          IrFunction* function, uint32_t* consumer, uint32_t* loaded)
{
    const IrSizes sizes = {
        .instruction_count = 16, .block_count = 1, .extra_count = entry->operands, .constant_count = 1,
        .slot_count = shape == 9, .address_count = 1, .call_count = 1, .call_type_count = entry->operands,
        .parameter_count = 4, .row_count = 1,
    };
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        const uint32_t source_class = entry->scalar ? (entry->source_bytes == 4 ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_F64) : BUSTER_TYPED_CLASS_VECTOR;
        const uint32_t result_class = entry->scalar ? (entry->result_bytes == 4 ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_F64) : BUSTER_TYPED_CLASS_VECTOR;
        const uint32_t source_type = ir_row_type(source_class, entry->source_bytes);
        const uint32_t result_type = ir_row_type(result_class, entry->result_bytes);
        const uint32_t first_bytes = shape >= 11 && shape <= 13 ? 4u << (shape - 11) : entry->source_bytes;
        const uint32_t first_type = ir_row_type(source_class, first_bytes);
        const uint8_t first = entry->scalar ? BUSTER_TIER0_ROLE_FLOATING(0) : BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t last = entry->scalar ? BUSTER_TIER0_ROLE_FLOATING(entry->operands - 1) : BUSTER_TIER0_ROLE_VECTOR(entry->operands - 1);
        const uint8_t result = entry->scalar ? BUSTER_TIER0_ROLE_FLOATING_RESULT : BUSTER_TIER0_ROLE_VECTOR(0);
        const bool vvvv = x86_form_description(entry->form)->vvvv != BUSTER_X86_OPERAND_NONE;
        Tier0Operation* row = &function->rows[0];
        row->record_count = shape == 8 ? 2 : 1;
        row->operand_count = entry->operands;
        row->result_class = (uint8_t)result_class;
        row->result_bytes = entry->result_bytes;
        row->records[0] = (X86Instruction){ .form = entry->form, .reg = result, .rm = last, .vvvv = vvvv ? first : 0 };
        row->records[1] = row->records[0];
        for (uint32_t a = 0; a < entry->operands; a += 1)
        {
            row->operand_classes[a] = (uint8_t)source_class;
            row->operand_bytes[a] = (uint8_t)(a == 0 && entry->operands == 2 ? first_bytes : entry->source_bytes);
            function->call_types[a] = (uint8_t)(a == 0 && entry->operands == 2 ? first_type : source_type);
        }
        function->result_type = IR_VOID;
        function->calls[0] = (IrCall){ .argument_count = entry->operands, .parameter_count = entry->operands, .result_type = (uint8_t)result_type, .row = 1 };
        function->addresses[0] = (IrAddress){ .scale = 1 };
        uint32_t next = 0;
        for (uint32_t p = 0; p < 4; p += 1)
        {
            function->parameter_types[p] = p == 3 ? IR_I64 : IR_PTR;
            tier1_select_test_append(function, &next, IR_PARAM, function->parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t other = BUSTER_IR_NONE;
        if (entry->operands == 2)
        {
            other = tier1_select_test_append(function, &next, IR_LOAD, first_type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t base = entry->operands == 2 ? 1 : 0;
        if (shape == 9)
        {
            const uint32_t initial = tier1_select_test_append(function, &next, IR_LOAD, source_type, base, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->slots[0] = (IrSlot){ .size = 64, .alignment = 64, .type = (uint8_t)source_type, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            base = tier1_select_test_append(function, &next, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, base, initial, BUSTER_IR_NONE);
        }
        else if (shape == 10)
        {
            function->symbol_count = 2;
            base = tier1_select_test_append(function, &next, IR_SYMBOL_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t address = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, base, shape >= 9 ? BUSTER_IR_NONE : 3, 0);
        *loaded = tier1_select_test_append(function, &next, IR_LOAD, source_type, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->alignment_log2[*loaded] = entry->aligned && shape != 6 ? (uint8_t)__builtin_ctz(entry->source_bytes) : 0;
        function->flags[*loaded] = shape == 1 ? BUSTER_IR_VOLATILE : 0;
        uint32_t intervening = BUSTER_IR_NONE;
        if (shape == 3)
        {
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, 2, other, BUSTER_IR_NONE);
        }
        else if (shape == 4)
        {
            intervening = tier1_select_test_append(function, &next, IR_LOAD, source_type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else if (shape == 5)
        {
            tier1_select_test_append(function, &next, IR_FENCE, IR_VOID, BUSTER_IR_FENCE_SEQ_CST, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        function->extra[0] = entry->operands == 1 || shape == 7 ? *loaded : other;
        function->extra[entry->operands - 1] = *loaded;
        *consumer = tier1_select_test_append(function, &next, IR_CALL, result_type, 0, 0, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_STORE, IR_VOID, 2, *consumer, BUSTER_IR_NONE);
        if (intervening != BUSTER_IR_NONE)
        {
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, 2, intervening, BUSTER_IR_NONE);
        }
        if (shape == 2)
        {
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, 2, *loaded, BUSTER_IR_NONE);
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[1] = next;
        const IrCheck checked = ir_verify(function, &test->scratch, 0);
        ready = next <= sizes.instruction_count && checked.error == IR_OK;
        if (!ready && test->failures < 20)
        {
            fprintf(stderr, "  row-load form=%u shape=%u IR=%u node=%u detail=%u\n", entry->form, shape, checked.error, checked.instruction, checked.detail);
        }
    }
    return ready;
}

static uint16_t tier1_float_row_load_move(uint32_t width, bool store)
{
    return width == 4 ? (store ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSS_X_XM32) :
           width == 8 ? (store ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOVSD_X_XM64) :
           width == 16 ? (store ? X86_FORM_MOVUPS_XM_X : X86_FORM_MOVUPS_X_XM) :
           width == 32 ? (store ? X86_FORM_VMOVDQU_YM256_Y : X86_FORM_VMOVDQU_Y_YM256) :
                         (store ? X86_FORM_VMOVDQU64_ZM512_Z : X86_FORM_VMOVDQU64_Z_ZM512);
}

static __attribute__((no_sanitize("function"))) uint32_t tier1_float_row_load_call(const void* code, const void* a, const void* b, void* result, uint32_t environment)
{
    void (*function)(const void*, const void*, void*, uint64_t);
    memcpy(&function, &code, sizeof(function));
    const uint32_t saved = _mm_getcsr();
    _mm_setcsr(environment);
    function(a, b, result, 0);
    const uint32_t flags = _mm_getcsr() & 0x3fu;
    _mm_setcsr(saved);
    return flags;
}

static void tier1_float_row_load_execute(Tier1EmitTest* test, const Tier1FloatRowLoadCase* entry, Tier1EmitResult* emitted)
{
    X86Instruction records[5];
    uint32_t count = 0;
    records[count++] = (X86Instruction){ .form = tier1_float_row_load_move(entry->source_bytes, false), .reg = 0, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE };
    if (entry->operands == 2)
    {
        records[count++] = (X86Instruction){ .form = tier1_float_row_load_move(entry->source_bytes, false), .reg = 1, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE };
    }
    records[count++] = (X86Instruction){ .form = entry->form, .reg = 0, .rm = (uint8_t)(entry->operands - 1), .vvvv = 0 };
    records[count++] = (X86Instruction){ .form = tier1_float_row_load_move(entry->result_bytes, true), .reg = 0, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE };
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    Tier1SelectCode reference = { .records = records, .count = count };
    void* code = MAP_FAILED;
    void* oracle = MAP_FAILED;
    uint64_t code_bytes = 0;
    uint64_t oracle_bytes = 0;
    const bool folded_native = tier1_emit_test_load_unmeasured(test, &emitted->code, &code, &code_bytes);
    const bool oracle_native = tier1_emit_test_load_unmeasured(test, &reference, &oracle, &oracle_bytes);
    const bool native = folded_native && oracle_native;
    if (native)
    {
        static const uint32_t singles[] = { 0x7fc01234u, 0x7fc05678u, 0x7f801234u, 0, 0x80000000u, 0x7f800000u, 0xff800000u, 1, 0x007fffffu, 0x3f800000u, 0xc0800000u, 0x7f7fffffu };
        static const uint64_t doubles[] = { UINT64_C(0x7ff8000000001234), UINT64_C(0x7ff8000000005678), UINT64_C(0x7ff0000000001234), 0, UINT64_C(0x8000000000000000),
                                           UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), 1, UINT64_C(0x000fffffffffffff), UINT64_C(0x3ff0000000000000),
                                           UINT64_C(0xc010000000000000), UINT64_C(0x7fefffffffffffff) };
        _Alignas(64) uint8_t a[128];
        _Alignas(64) uint8_t b[128];
        uint8_t actual[64];
        uint8_t expected[64];
        uint64_t random = UINT64_C(0x2829a54ff53a5f1d);
        for (uint32_t sample = 0; sample < 36; sample += 1)
        {
            for (uint32_t lane = 0; lane < 16; lane += 1)
            {
                const uint32_t pick = (sample + lane) % BUSTER_ARRAY_LENGTH(singles);
                const uint32_t next = (pick + 1) % BUSTER_ARRAY_LENGTH(singles);
                const uint64_t left = sample < 12 ? doubles[pick] : sample < 24 ? ((uint64_t)singles[next] << 32) | singles[pick] : tier1_emit_test_random(&random);
                const uint64_t right = sample < 12 ? doubles[next] : sample < 24 ? ((uint64_t)singles[pick] << 32) | singles[next] : tier1_emit_test_random(&random);
                memcpy(a + 8 * lane, &left, 8);
                memcpy(b + 8 * lane, &right, 8);
            }
            // Packed VEX/EVEX memory operands may be unaligned; legacy packed cases carry their alignment proof.
            const uint32_t offset = entry->aligned ? 0 : sample & 1;
            for (uint32_t rounding = 0; rounding < 4; rounding += 1)
            {
                const uint32_t environment = 0x1f80u | (rounding << 13) | ((sample & 2) != 0 ? 0x8040u : 0);
                memset(actual, 0xa5, sizeof(actual));
                memset(expected, 0xa5, sizeof(expected));
                const uint32_t flags = tier1_float_row_load_call(code, a + offset, b + offset, actual, environment);
                const uint32_t wanted = tier1_float_row_load_call(oracle, a + offset, b + offset, expected, environment);
                tier1_emit_test_expect(test, flags == wanted && memcmp(actual, expected, sizeof(actual)) == 0, "folded row preserves every result bit and MXCSR exception flag");
            }
        }
        uint8_t* pages = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool guard = pages != MAP_FAILED && mprotect(pages + 4096, 4096, PROT_NONE) == 0;
        tier1_emit_test_expect(test, guard, "the folded row's exact-width guard page");
        if (guard)
        {
            uint8_t* edge = pages + 4096 - entry->source_bytes;
            memcpy(edge, b, entry->source_bytes);
            const void* left = entry->operands == 1 ? edge : a;
            const uint32_t flags = tier1_float_row_load_call(code, left, edge, actual, 0x1f80);
            const uint32_t wanted = tier1_float_row_load_call(oracle, left, edge, expected, 0x1f80);
            tier1_emit_test_expect(test, flags == wanted && memcmp(actual, expected, entry->result_bytes) == 0, "folded row reads exactly the original load width at a guard page");
        }
        if (pages != MAP_FAILED)
        {
            munmap(pages, 8192);
        }
    }
    if (code != MAP_FAILED)
    {
        munmap(code, code_bytes);
    }
    if (oracle != MAP_FAILED)
    {
        munmap(oracle, oracle_bytes);
    }
}

static void tier1_float_row_load_compile(Tier1EmitTest* test, const Tier1FloatRowLoadCase* entry, uint32_t shape, bool fold)
{
    test->terms = entry->form;
    test->width = shape;
    IrFunction function;
    uint32_t consumer = BUSTER_IR_NONE;
    uint32_t loaded = BUSTER_IR_NONE;
    const bool created = tier1_float_row_load_function(test, entry, shape, &function, &consumer, &loaded);
    tier1_emit_test_expect(test, created, "construct a floating machine-row load fixture");
    const Tier0Operation original = created ? function.rows[0] : (Tier0Operation){ 0 };
    IrDominance dominance;
    const bool dominated = created && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t failed = BUSTER_IR_NONE;
    const Tier1SelectError selection = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed) : TIER1_SELECT_ERROR_INVALID;
    const bool selected = selection == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, selected, "select a floating machine-row load fixture");
    if (dominated && !selected && test->failures < 20)
    {
        fprintf(stderr, "  row-load form=%u shape=%u selection=%u node=%u\n", entry->form, shape, selection, failed);
    }
    if (selected)
    {
        tier1_emit_test_expect(test, memcmp(&original, &function.rows[0], sizeof(original)) == 0, "selection leaves the producer's machine row immutable");
        tier1_emit_test_expect(test, (plan.patterns[consumer].rule == TIER1_RULE_FLOAT_ROW_LOAD) == fold && plan.covered[loaded] == fold,
                               "only an exact-width, single-use and safely ordered machine input folds");
        if (fold)
        {
            const Tier1SelectPattern* pattern = &plan.patterns[consumer];
            tier1_emit_test_expect(test, pattern->immediate == entry->form && pattern->source_width == entry->source_bytes,
                                   "the memory kernel keeps the original form and access width");
            tier1_emit_test_expect(test, shape < 9 || (pattern->stack_object == (shape == 9 ? 0u : 1u) && pattern->rip == (shape == 10)),
                                   "the row kernel carries frame and direct-symbol addresses");
            if (shape == 0 && entry->form == X86_FORM_SQRTSS_X_XM32)
            {
                tier1_emit_test_expect(test, tier1_liveness_validate_plan(&plan, &test->scratch, true) == TIER1_LIVENESS_OK,
                                       "a folded row's selected address uses pass CFG plan validation");
                const Tier1SelectPattern saved = plan.patterns[consumer];
                for (uint32_t mutation = 0; mutation < 5; mutation += 1)
                {
                    if (mutation == 0)
                    {
                        plan.patterns[consumer].rule = TIER1_RULE_ROW;
                    }
                    else if (mutation == 1)
                    {
                        plan.patterns[consumer].immediate = X86_FORM_XORPS_X_XM128;
                    }
                    else if (mutation == 2)
                    {
                        plan.patterns[consumer].source_width = 8;
                    }
                    else if (mutation == 3)
                    {
                        plan.patterns[consumer].fold_count = 0;
                    }
                    else
                    {
                        plan.patterns[consumer].values[1] = loaded;
                    }
                    tier1_emit_test_expect(test, tier1_liveness_validate_plan(&plan, &test->scratch, true) == TIER1_LIVENESS_INVALID,
                                           "a malformed folded-row rule, form, width or selected use is refused");
                    plan.patterns[consumer] = saved;
                }
                const Tier1SelectPattern parameter = plan.patterns[3];
                plan.patterns[3].rule = TIER1_RULE_FLOAT_ROW_LOAD;
                tier1_emit_test_expect(test, tier1_liveness_validate_plan(&plan, &test->scratch, true) == TIER1_LIVENESS_INVALID,
                                       "a floating row kernel attached to a non-row node is refused");
                plan.patterns[3] = parameter;
            }
            Tier1EmitDiagnostic diagnostic = { 0 };
            const Tier1EmitOptions options = {
                .register_mask = tier1_liveness_register_mask(), .vector_register_mask = 0x7, .certify = true,
                .frame_flags = shape == 9 ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0, .symbol_count = function.symbol_count,
                .diagnostic = &diagnostic,
            };
            Tier1EmitResult emitted;
            const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
            tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "the floating row memory kernel emits and certifies");
            if (error != TIER1_EMIT_OK && test->failures < 20)
            {
                fprintf(stderr, "  row-load form=%u shape=%u emission=%u node=%u cause=%u detail=%u\n", entry->form, shape, error, failed, diagnostic.cause, diagnostic.error);
            }
            if (error == TIER1_EMIT_OK)
            {
                uint32_t memory = UINT32_MAX;
                for (uint32_t r = 0; r < emitted.code.count; r += 1)
                {
                    const X86Instruction record = emitted.code.records[r];
                    if (x86_effects(record.form)->operation == x86_effects(entry->form)->operation && (record.mode & BUSTER_X86_MODE_MEMORY) != 0)
                    {
                        memory = r;
                    }
                }
                tier1_emit_test_expect(test, memory != UINT32_MAX, "the selected floating instruction has a memory operand");
                if (memory != UINT32_MAX)
                {
                    X86Instruction mutant = emitted.code.records[memory];
                    mutant.displacement += entry->source_bytes;
                    tier1_emit_test_expect(test, tier1_code_certificate_refuses(test, &plan, &emitted, memory, mutant, UINT32_MAX),
                                           "the row certificate rejects a changed folded address");
                }
                tier1_emit_test_expect(test, memcmp(&original, &function.rows[0], sizeof(original)) == 0, "folding and emitting leave the producer's machine row immutable");
                if (shape == 10)
                {
                    tier1_emit_test_expect(test, emitted.reference_count == 1, "a folded row's direct symbol keeps its relocation");
                }
                else
                {
                    tier1_float_row_load_execute(test, entry, &emitted);
                }
            }
            if (shape == 0 && x86_form_description(entry->form)->family == BUSTER_X86_FAMILY_VEX)
            {
                const uint8_t allowed = test->allowed[entry->form];
                test->allowed[entry->form] = 0;
                const Tier1EmitError refused = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                test->allowed[entry->form] = allowed;
                tier1_emit_test_expect(test, refused == TIER1_EMIT_TARGET, "a row load fold does not bypass its original form's target allowlist");
            }
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_float_row_load_tests(Tier1EmitTest* test)
{
    uint8_t* forms = malloc(2u * X86_FORM_COUNT);
    tier1_emit_test_expect(test, forms != 0, "floating row targets' forms");
    if (forms != 0)
    {
        uint8_t* saved_forms = test->allowed;
        const uint8_t* saved_native = test->native_allowed;
        X86Target target;
        const bool parsed = x86_target_parse("x86-64-v4", 9, &target);
        const X86Target native = x86_target_native();
        tier1_emit_test_expect(test, parsed, "floating rows' target policy");
        x86_target_forms(&target, forms);
        x86_target_forms(&native, forms + X86_FORM_COUNT);
        test->allowed = forms;
        test->native_allowed = forms + X86_FORM_COUNT;
        for (uint32_t c = 0; parsed && c < BUSTER_ARRAY_LENGTH(tier1_float_row_load_cases); c += 1)
        {
            tier1_float_row_load_compile(test, &tier1_float_row_load_cases[c], 0, true);
        }
        for (uint32_t shape = 1; parsed && shape <= 8; shape += 1)
        {
            tier1_float_row_load_compile(test, &tier1_float_row_load_cases[4], shape, false);
        }
        // Scalar x86 intrinsics carry full xmm values: their 16-byte load cannot become m32, even if only its low
        // lane is arithmetical. A narrow carrier in both r/m and vvvv still supplies observable upper result lanes.
        const Tier1FloatRowLoadCase wide = { X86_FORM_VSQRTSS_X_X_XM32, 16, 16, 1, false, false };
        const Tier1FloatRowLoadCase merge = { X86_FORM_VSQRTSS_X_X_XM32, 4, 16, 1, false, false };
        const Tier1FloatRowLoadCase scalar_merge = { X86_FORM_VSQRTSS_X_X_XM32, 4, 8, 1, true, false };
        const Tier1FloatRowLoadCase legacy_merge = { X86_FORM_SQRTSS_X_XM32, 4, 16, 1, false, false };
        const Tier1FloatRowLoadCase legacy_convert_merge = { X86_FORM_CVTSD2SS_X_XM64, 8, 16, 1, false, false };
        tier1_float_row_load_compile(test, &wide, 0, false);
        tier1_float_row_load_compile(test, &merge, 0, false);
        tier1_float_row_load_compile(test, &scalar_merge, 0, false);
        // The ordinary row zero-extends its narrow carriers. A folded kernel cannot read those values directly
        // through a wider destination or vvvv field, even though its separate r/m load has the exact access width.
        tier1_float_row_load_compile(test, &tier1_float_row_load_cases[4], 11, false);
        for (uint32_t shape = 11; shape <= 13; shape += 1)
        {
            tier1_float_row_load_compile(test, &tier1_float_row_load_cases[8], shape, false);
        }
        tier1_float_row_load_compile(test, &tier1_float_row_load_cases[9], 13, false);
        tier1_float_row_load_compile(test, &legacy_merge, 0, false);
        tier1_float_row_load_compile(test, &legacy_convert_merge, 0, false);
        tier1_float_row_load_compile(test, &tier1_float_row_load_cases[1], 9, true);
        tier1_float_row_load_compile(test, &tier1_float_row_load_cases[2], 10, true);
        test->allowed = saved_forms;
        test->native_allowed = saved_native;
        free(forms);
    }
}
