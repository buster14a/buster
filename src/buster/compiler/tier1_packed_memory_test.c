// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2829's packed memory operands: generated IR checks the existing motion proof and address mapping; native records
// compare the memory expansion with the original register-only operation, including destination aliases, NaN payloads,
// every rounding mode, unaligned inputs and exact-width reads at an inaccessible page. Included by tier1_emit_test.c.

static const uint8_t tier1_packed_memory_operations[10] =
{
    IR_FADD,
    IR_FSUB,
    IR_FMUL,
    IR_FDIV,
    IR_FMA,
    IR_FMA,
    IR_SITOFP,
    IR_UITOFP,
    IR_FPTOSI,
    IR_FPTOUI,
};

static Tier1SelectHoles tier1_packed_memory_holes(uint32_t operation, uint32_t lane, uint32_t width, uint32_t encoding, uint32_t destination)
{
    const bool convert = operation >= 6;
    const bool fma = operation == 4 || operation == 5;
    const Tier1SelectHoles result =
    {
        .rule = convert ? TIER1_RULE_VECTOR_CONVERT_LOAD : fma ? TIER1_RULE_VECTOR_FMA_LOAD : TIER1_RULE_VECTOR_LOAD_OPERAND,
        .operation = convert ? tier1_packed_memory_operations[operation] : (uint8_t)(BUSTER_TIER1_VECTOR_FADD + operation),
        .width = (uint8_t)width,
        .source_width = (uint8_t)lane,
        .condition = operation == 5,
        .destination = (uint8_t)(16 + destination),
        .left = 17,
        .right = operation == 4 ? 19 : 18,
        .third = 19,
        .vex = encoding != BUSTER_TIER1_ENCODING_EVEX,
        .sse = encoding == BUSTER_TIER1_ENCODING_SSE,
        .avx2 = true,
        .memory =
        {
            .rm = convert ? X86_REGISTER_RSI : operation == 5 ? X86_REGISTER_RCX : X86_REGISTER_RDX,
            .mode = BUSTER_X86_MEMORY_BASE,
        },
    };
    return result;
}

// The four-pointer ABI keeps memory addresses in general registers independently of the operation's vector aliases.
static uint32_t tier1_packed_memory_records(Tier1SelectHoles holes, uint32_t operation, bool folded, X86Instruction* records)
{
    const uint32_t encoding = tier1_select_holes_encoding(&holes);
    const bool convert = operation >= 6;
    const bool fma = operation == 4 || operation == 5;
    const uint32_t memory_operand = convert ? 0 : operation == 5 ? 2 : 1;
    const uint32_t operands = convert ? 1 : fma ? 3 : 2;
    const uint8_t pointers[3] = { X86_REGISTER_RSI, X86_REGISTER_RDX, X86_REGISTER_RCX };
    uint32_t count = 0;
    for (uint32_t p = 0; p < operands; p += 1)
    {
        if (!folded || p != memory_operand)
        {
            records[count++] = (X86Instruction)
            {
                .form = tier1_select_class_forms(holes.width, encoding)->load,
                .reg = (uint8_t)(p + 1),
                .rm = pointers[p],
                .mode = BUSTER_X86_MEMORY_BASE,
            };
        }
    }
    if (!folded)
    {
        holes.rule = convert ? TIER1_RULE_VECTOR_CONVERT : fma ? TIER1_RULE_VECTOR_FMA : TIER1_RULE_VECTOR;
        holes.scale = BUSTER_TIER1_VECTOR_BINARY;
        holes.left = 17;
        holes.right = 18;
        holes.third = 19;
        holes.destination = 20;
    }
    const Tier1SelectRecords expanded = tier1_select_expand(&holes);
    for (uint32_t r = 0; r < expanded.count; r += 1)
    {
        records[count++] = expanded.records[r];
    }
    records[count++] = (X86Instruction)
    {
        .form = tier1_select_class_forms(holes.width, encoding)->store,
        .reg = holes.destination & 15,
        .rm = X86_REGISTER_RDI,
        .mode = BUSTER_X86_MEMORY_BASE,
    };
    if (encoding != BUSTER_TIER1_ENCODING_SSE)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_VZEROUPPER };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    return count;
}

static void* tier1_packed_memory_code(Tier1EmitTest* test, X86Instruction* records, uint32_t count, const uint8_t* allowed)
{
    void* code = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool encoded = code != MAP_FAILED && x86_target_validate(allowed, records, count) == count && x86_encode(records, count, code, 4096, 0, 0) > 0;
    const bool executable = encoded && mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0;
    tier1_emit_test_expect(test, executable, "packed memory code obeys its target, encodes and becomes executable");
    if (!executable && code != MAP_FAILED)
    {
        munmap(code, 4096);
        code = MAP_FAILED;
    }
    return code;
}

static __attribute__((no_sanitize("function"))) void tier1_packed_memory_call(const void* code, void* output, const void* a, const void* b, const void* c)
{
    void (*function)(void*, const void*, const void*, const void*);
    memcpy(&function, &code, sizeof(function));
    function(output, a, b, c);
}

static void tier1_packed_memory_inputs(uint8_t* memory[3], uint32_t lane, uint32_t width, uint32_t sample)
{
    static const uint32_t singles[16] =
    {
        0x7fc10001, 0x7fc20002, 0xffc30003, 0x7f800001,
        0x00000001, 0x80000000, 0x00000000, 0x3f800000,
        0xbf800000, 0x7f800000, 0xff800000, 0x4f800000,
        0x7f7fffff, 0x80800000, 0x3faaaaab, 0x4b800001,
    };
    static const uint64_t doubles[16] =
    {
        UINT64_C(0x7ff8000000010001), UINT64_C(0x7ff8000000020002), UINT64_C(0xfff8000000030003), UINT64_C(0x7ff0000000000001),
        UINT64_C(0x0000000000000001), UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000000), UINT64_C(0x3ff0000000000000),
        UINT64_C(0xbff0000000000000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x43f0000000000000),
        UINT64_C(0x7fefffffffffffff), UINT64_C(0x8010000000000000), UINT64_C(0x3ff5555555555555), UINT64_C(0x4340000000000001),
    };
    const uint32_t bytes = lane == IR_SIMD_LANE_F64 ? 8 : 4;
    for (uint32_t p = 0; p < 3; p += 1)
    {
        for (uint32_t at = 0; at < width; at += bytes)
        {
            const uint32_t value = (sample + p + at / bytes) & 15;
            if (bytes == 4)
            {
                memcpy(memory[p] + at, &singles[value], 4);
            }
            else
            {
                memcpy(memory[p] + at, &doubles[value], 8);
            }
        }
    }
}

static void tier1_packed_memory_native(Tier1EmitTest* test)
{
    const uint32_t saved_csr = _mm_getcsr();
    const X86Target host = x86_target_native();
    uint8_t* allowed = malloc(X86_FORM_COUNT);
    uint8_t* native = malloc(X86_FORM_COUNT);
    uint8_t* pages[3];
    bool ready = allowed != 0 && native != 0;
    for (uint32_t p = 0; p < 3; p += 1)
    {
        pages[p] = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= pages[p] != MAP_FAILED && mprotect(pages[p] + 4096, 4096, PROT_NONE) == 0;
    }
    tier1_emit_test_expect(test, ready, "packed memory oracle has guarded operands");
    if (ready)
    {
        x86_target_forms(&host, native);
    }
    for (uint32_t encoding = 0; ready && encoding <= BUSTER_TIER1_ENCODING_SSE; encoding += 1)
    {
        X86Target target = { .architecture = X86_ARCH_BASELINE };
        if (encoding != BUSTER_TIER1_ENCODING_SSE)
        {
            const char* name = encoding == BUSTER_TIER1_ENCODING_EVEX ? "x86-64-v4" : "x86-64-v3";
            tier1_emit_test_expect(test, x86_target_parse(name, 9, &target), "packed memory output target exists");
        }
        x86_target_forms(&target, allowed);
        const uint32_t maximum = encoding == BUSTER_TIER1_ENCODING_EVEX ? 64 : encoding == BUSTER_TIER1_ENCODING_VEX ? 32 : 16;
        for (uint32_t width = 16; width <= maximum; width *= 2)
        {
            for (uint32_t lane = IR_SIMD_LANE_F32; lane <= IR_SIMD_LANE_F64; lane += 1)
            {
                for (uint32_t operation = 0; operation < 10; operation += 1)
                {
                    const bool convert = operation >= 6;
                    const bool supported = convert ? lane == IR_SIMD_LANE_F32 && tier1_select_packed_convert_form(tier1_packed_memory_operations[operation], width, encoding) != X86_FORM_NONE :
                                                   operation < 4 || encoding != BUSTER_TIER1_ENCODING_SSE;
                    for (uint32_t destination = 1; supported && destination <= 4; destination += 1)
                    {
                        Tier1SelectHoles holes = tier1_packed_memory_holes(operation, lane, width, encoding, destination);
                        const Tier1SelectRecords expansion = tier1_select_expand(&holes);
                        uint32_t memories = 0;
                        for (uint32_t r = 0; r < expansion.count; r += 1)
                        {
                            memories += (expansion.records[r].mode & BUSTER_X86_MODE_MEMORY) != 0;
                        }
                        tier1_emit_test_expect(test, memories == 1 && !expansion.overflowed, "a packed fold keeps one exact-width memory operand");
                        // Indexed forms retain all addressing fields; scalar `scale` is never reused as the operation.
                        holes.memory.index = X86_REGISTER_R8;
                        holes.memory.mode = BUSTER_X86_MEMORY_BASE_INDEX(3);
                        holes.memory.displacement = -17;
                        const Tier1SelectRecords indexed = tier1_select_expand(&holes);
                        const uint32_t memory_record = encoding == BUSTER_TIER1_ENCODING_SSE ? 0 : indexed.count - 1;
                        const X86Instruction memory = indexed.records[memory_record];
                        tier1_emit_test_expect(test, memory.mode == holes.memory.mode && memory.rm == holes.memory.rm && memory.index == X86_REGISTER_R8 && memory.displacement == -17,
                                               "a packed memory expansion retains base, scaled index and displacement");
                        holes.memory.index = 0;
                        holes.memory.mode = BUSTER_X86_MEMORY_BASE;
                        holes.memory.displacement = 0;
                        X86Instruction folded_records[16];
                        X86Instruction oracle_records[16];
                        const uint32_t folded_count = tier1_packed_memory_records(holes, operation, true, folded_records);
                        const uint32_t oracle_count = tier1_packed_memory_records(holes, operation, false, oracle_records);
                        void* folded = tier1_packed_memory_code(test, folded_records, folded_count, allowed);
                        void* oracle = tier1_packed_memory_code(test, oracle_records, oracle_count, allowed);
                        const bool runs = folded != MAP_FAILED && oracle != MAP_FAILED && x86_target_validate(native, folded_records, folded_count) == folded_count;
                        for (uint32_t misaligned = 0; runs && misaligned < 2; misaligned += 1)
                        {
                            uint8_t* inputs[3] = { pages[0] + 4096 - width - misaligned, pages[1] + 4096 - width - misaligned, pages[2] + 4096 - width - misaligned };
                            for (uint32_t sample = 0; sample < 16; sample += 1)
                            {
                                tier1_packed_memory_inputs(inputs, lane, width, sample);
                                for (uint32_t mode = 0; mode < 4; mode += 1)
                                {
                                    const uint32_t environment = (saved_csr & ~(uint32_t)(_MM_EXCEPT_MASK | _MM_ROUND_MASK)) | _MM_MASK_MASK | mode << 13;
                                    uint8_t expected[64];
                                    uint8_t actual[64];
                                    _mm_setcsr(environment);
                                    tier1_packed_memory_call(oracle, expected, inputs[0], inputs[1], inputs[2]);
                                    const uint32_t expected_flags = _mm_getcsr() & _MM_EXCEPT_MASK;
                                    _mm_setcsr(environment);
                                    tier1_packed_memory_call(folded, actual, inputs[0], inputs[1], inputs[2]);
                                    const uint32_t actual_flags = _mm_getcsr() & _MM_EXCEPT_MASK;
                                    _mm_setcsr(saved_csr);
                                    const bool same = memcmp(expected, actual, width) == 0 && expected_flags == actual_flags;
                                    if (!same && test->failures < 20)
                                    {
                                        fprintf(stderr, "  packed memory op=%u lane=%u width=%u encoding=%u destination=%u sample=%u rounding=%u unaligned=%u flags=%x/%x\n",
                                                operation, lane, width, encoding, destination, sample, mode, misaligned, actual_flags, expected_flags);
                                    }
                                    tier1_emit_test_expect(test, same, "packed memory and ordered register operations agree on every result bit and MXCSR flag");
                                }
                            }
                        }
                        if (folded != MAP_FAILED)
                        {
                            munmap(folded, 4096);
                        }
                        if (oracle != MAP_FAILED)
                        {
                            munmap(oracle, 4096);
                        }
                    }
                }
            }
        }
    }
    _mm_setcsr(saved_csr);
    for (uint32_t p = 0; p < 3; p += 1)
    {
        if (pages[p] != MAP_FAILED)
        {
            munmap(pages[p], 8192);
        }
    }
    free(allowed);
    free(native);
}

static void tier1_packed_memory_plans(Tier1EmitTest* test)
{
    uint8_t* allowed = malloc(X86_FORM_COUNT);
    X86Target target = {0};
    const bool target_ready = allowed != 0 && x86_target_parse("x86-64-v4", 9, &target);
    tier1_emit_test_expect(test, target_ready, "packed memory integration target exists");
    if (target_ready)
    {
        x86_target_forms(&target, allowed);
    }
    for (uint32_t width = 16; target_ready && width <= 64; width *= 2)
    {
        for (uint32_t operation = 0; operation < 10; operation += 1)
        {
            for (uint32_t shape = 0; shape < 7; shape += 1)
            {
                const bool convert = operation >= 6;
                const bool fma = operation == 4 || operation == 5;
                const IrType type = width == 16 ? IR_V4I32 : width == 32 ? IR_V8I32 : IR_V16I32;
                const IrSizes sizes = { .instruction_count = 20, .block_count = shape == 6 ? 2 : 1, .parameter_count = 5, .address_count = 1 };
                IrFunction function;
                const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
                tier1_emit_test_expect(test, created, "create packed memory selection fixture");
                if (created)
                {
                    uint32_t next = 0;
                    function.result_type = IR_VOID;
                    for (uint32_t p = 0; p < 5; p += 1)
                    {
                        function.parameter_types[p] = p == 4 ? IR_I64 : IR_PTR;
                        tier1_select_test_append(&function, &next, IR_PARAM, function.parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    }
                    uint32_t a = BUSTER_IR_NONE;
                    uint32_t b = BUSTER_IR_NONE;
                    uint32_t c = BUSTER_IR_NONE;
                    if (!convert)
                    {
                        a = tier1_select_test_append(&function, &next, IR_LOAD, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    }
                    if (fma)
                    {
                        const uint32_t other = tier1_select_test_append(&function, &next, IR_LOAD, type, operation == 4 ? 3 : 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        b = operation == 5 ? other : b;
                        c = operation == 4 ? other : c;
                    }
                    function.addresses[0] = (IrAddress){ .offset = -17, .scale = 8 };
                    const uint32_t pointer = convert ? 1 : operation == 5 ? 3 : 2;
                    const uint32_t address = tier1_select_test_append(&function, &next, IR_ADDRESS, IR_PTR, pointer, shape == 1 ? 4 : BUSTER_IR_NONE, 0);
                    const uint32_t load = tier1_select_test_append(&function, &next, IR_LOAD, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.flags[load] = shape == 2 ? BUSTER_IR_VOLATILE : 0;
                    uint32_t observed = BUSTER_IR_NONE;
                    if (shape == 3)
                    {
                        tier1_select_test_append(&function, &next, IR_STORE, IR_VOID, 0, load, BUSTER_IR_NONE);
                    }
                    else if (shape == 4)
                    {
                        observed = tier1_select_test_append(&function, &next, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    }
                    else if (shape == 5)
                    {
                        observed = tier1_select_test_append(&function, &next, IR_COPY, type, load, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    }
                    else if (shape == 6)
                    {
                        tier1_select_test_append(&function, &next, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        function.block_offsets[1] = next;
                    }
                    a = convert ? load : a;
                    b = !convert && operation != 5 ? load : b;
                    c = operation == 5 ? load : c;
                    const uint32_t root = tier1_select_test_append(&function, &next, tier1_packed_memory_operations[operation], type, a, b, c);
                    function.flags[root] = IR_SIMD_LANE_F32;
                    if (observed != BUSTER_IR_NONE)
                    {
                        tier1_select_test_append(&function, &next, IR_STORE, IR_VOID, 3, observed, BUSTER_IR_NONE);
                    }
                    tier1_select_test_append(&function, &next, IR_STORE, IR_VOID, 0, root, BUSTER_IR_NONE);
                    tier1_select_test_append(&function, &next, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.instruction_count = next;
                    function.block_offsets[function.block_count] = next;
                    const bool verified = ir_verify(&function, &test->scratch, 0).error == IR_OK;
                    tier1_emit_test_expect(test, verified, "generated packed memory fixtures are valid IR");
                    IrDominance dominance;
                    const bool dominated = verified && tier1_terminal_dominance(test, &function, &dominance);
                    Tier1SelectPlan plan;
                    uint32_t failed = BUSTER_IR_NONE;
                    const bool selected = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed) == TIER1_SELECT_OK;
                    tier1_emit_test_expect(test, selected, "select a packed load through the ordinary motion proof");
                    if (!selected && test->failures < 20)
                    {
                        fprintf(stderr, "  packed selection op=%u width=%u shape=%u node=%u\n", operation, width, shape, failed);
                    }
                    if (selected)
                    {
                        const Tier1SelectPattern pattern = plan.patterns[root];
                        const uint32_t rule = convert ? TIER1_RULE_VECTOR_CONVERT_LOAD : fma ? TIER1_RULE_VECTOR_FMA_LOAD : TIER1_RULE_VECTOR_LOAD_OPERAND;
                        const bool folded = shape < 2;
                        tier1_emit_test_expect(test, (pattern.rule == rule) == folded && (plan.covered[load] != 0) == folded,
                                               "packed folds require one use in the same block without any intervening access");
                        if (folded)
                        {
                            const uint32_t first = convert ? 0 : fma ? 2 : 1;
                            tier1_emit_test_expect(test, pattern.values[first] == pointer && pattern.values[first + 1] == (shape == 1 ? 4 : BUSTER_IR_NONE) &&
                                                  pattern.displacement == -17 && (shape != 1 || pattern.scale == 3),
                                                   "the selected packed fold carries its exact indexed address");
                            const Tier1EmitOptions options =
                            {
                                .register_mask = tier1_liveness_register_mask(),
                                .vector_register_mask = 0x000e,
                                .rematerialize = true,
                            };
                            Tier1EmitResult emitted;
                            const Tier1EmitError emission = tier1_emit_function(&plan, &options, allowed, &test->output, &test->scratch, &emitted, &failed);
                            tier1_emit_test_expect(test, emission == TIER1_EMIT_OK, "the actual packed memory plan allocates and emits through the CFG consumer");
                            if (emission != TIER1_EMIT_OK && test->failures < 20)
                            {
                                fprintf(stderr, "  packed emission error=%u op=%u width=%u shape=%u node=%u\n", emission, operation, width, shape, failed);
                            }
                            if (emission == TIER1_EMIT_OK)
                            {
                                tier1_emit_test_expect(test, x86_target_validate(allowed, emitted.code.records, emitted.code.count) == emitted.code.count,
                                                       "every integrated packed memory record is allowed by its output target");
                            }
                        }
                    }
                }
                arena_pop_to(&test->input, 0);
                arena_pop_to(&test->output, 0);
                arena_pop_to(&test->scratch, 0);
            }
        }
    }
    free(allowed);
}

static void tier1_packed_memory_tests(Tier1EmitTest* test)
{
    tier1_packed_memory_plans(test);
    tier1_packed_memory_native(test);
}
