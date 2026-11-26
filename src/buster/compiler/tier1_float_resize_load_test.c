// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2829: scalar precision changes from a single-use load. Included by tier1_float_test.c and run in its bounded child.
// The external hardware oracle uses a register conversion. Guard pages check the source's exact access width;
// MXCSR modes and bit patterns cover subnormals, overflow, rounding boundaries and both kinds of NaN.

static uint64_t tier1_float_resize_load_oracle(uint64_t bits, uint32_t source_width)
{
    uint64_t result = 0;
    if (source_width == 4)
    {
        const uint32_t narrow = (uint32_t)bits;
        float source;
        double converted;
        memcpy(&source, &narrow, sizeof(source));
        __asm__ volatile("xorps %0, %0\n\tcvtss2sd %1, %0" : "=&x"(converted) : "x"(source));
        memcpy(&result, &converted, sizeof(converted));
    }
    else
    {
        double source;
        float converted;
        uint32_t narrow;
        memcpy(&source, &bits, sizeof(source));
        __asm__ volatile("xorps %0, %0\n\tcvtsd2ss %1, %0" : "=&x"(converted) : "x"(source));
        memcpy(&narrow, &converted, sizeof(converted));
        result = narrow;
    }
    return result;
}

static __attribute__((noinline, no_sanitize("function"))) uint64_t tier1_float_resize_load_call(void* code, const void* pointer)
{
    uint64_t (*function)(const void*, uint64_t) = NULL;
    memcpy(&function, &code, sizeof(function));
    return function(pointer, 3);
}

static void tier1_float_resize_load_tests(Tier1EmitTest* test)
{
    static const uint64_t patterns[2][16] =
    {
        {
            UINT32_C(0x00000000),
            UINT32_C(0x80000000),
            UINT32_C(0x00000001),
            UINT32_C(0x007fffff),
            UINT32_C(0x00800000),
            UINT32_C(0x3f800001),
            UINT32_C(0x3f000000),
            UINT32_C(0x7f7fffff),
            UINT32_C(0xff7fffff),
            UINT32_C(0x7f800000),
            UINT32_C(0xff800000),
            UINT32_C(0x7fc12345),
            UINT32_C(0xffc54321),
            UINT32_C(0x7f812345),
            UINT32_C(0xff854321),
            UINT32_C(0x80800001),
        },
        {
            UINT64_C(0x0000000000000000),
            UINT64_C(0x8000000000000000),
            UINT64_C(0x0000000000000001),
            UINT64_C(0x36a0000000000000),
            UINT64_C(0x3690000000000001),
            UINT64_C(0x3ff0000010000000),
            UINT64_C(0x3ff0000010000001),
            UINT64_C(0x47effffff0000000),
            UINT64_C(0xc7effffff0000000),
            UINT64_C(0x7ff0000000000000),
            UINT64_C(0xfff0000000000000),
            UINT64_C(0x7ff8123456789abc),
            UINT64_C(0xfff854321abcdef0),
            UINT64_C(0x7ff0123456789abc),
            UINT64_C(0xfff054321abcdef0),
            UINT64_C(0x8010000000000001),
        },
    };
    static const char* targets[] = { "x86-64", "x86-64-v3", "x86-64-v4" };
    const long queried_page = sysconf(_SC_PAGESIZE);
    const uint64_t page = queried_page > 0 ? (uint64_t)queried_page : 4096;
    uint8_t* memory = mmap(NULL, page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* allowed = malloc(X86_FORM_COUNT);
    uint8_t* original_allowed = test->allowed;
    const bool ready = memory != MAP_FAILED && allowed != NULL && mprotect(memory, page, PROT_READ | PROT_WRITE) == 0;
    tier1_emit_test_expect(test, ready, "precision conversion inputs have an inaccessible following page");
    const uint32_t saved_mxcsr = _mm_getcsr();
    for (uint32_t precision = 0; ready && precision < 2; precision += 1)
    {
        const uint32_t source_width = precision == 0 ? 4 : 8;
        const uint32_t source = precision == 0 ? IR_F32 : IR_F64;
        const uint32_t target = precision == 0 ? IR_F64 : IR_F32;
        for (uint32_t arch = 0; arch < BUSTER_ARRAY_LENGTH(targets); arch += 1)
        {
            X86Target policy = {0};
            const bool parsed = x86_target_parse(targets[arch], strlen(targets[arch]), &policy);
            tier1_emit_test_expect(test, parsed, "precision conversion output target parses");
            if (parsed)
            {
                x86_target_forms(&policy, allowed);
                test->allowed = allowed;
            }
            for (uint32_t observable = 0; parsed && observable < 2; observable += 1)
            {
                IrFunction function;
                const IrSizes sizes = { .instruction_count = 8, .block_count = 1, .parameter_count = 2, .address_count = 1 };
                const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
                tier1_emit_test_expect(test, created, "create an indexed precision conversion");
                if (created)
                {
                    function.result_type = IR_I64;
                    function.block_offsets[1] = 8;
                    function.parameter_types[0] = IR_PTR;
                    function.parameter_types[1] = IR_I64;
                    function.addresses[0] = (IrAddress){ .offset = -3, .scale = 4 };
                    tier1_emit_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE);
                    tier1_emit_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE);
                    tier1_emit_test_node(&function, 2, IR_ADDRESS, IR_PTR, 0, 1);
                    function.c[2] = 0;
                    tier1_emit_test_node(&function, 3, IR_LOAD, (IrType)source, 2, BUSTER_IR_NONE);
                    function.flags[3] = observable ? BUSTER_IR_VOLATILE : 0;
                    tier1_emit_test_node(&function, 4, precision == 0 ? IR_FEXT : IR_FTRUNC, (IrType)target, 3, BUSTER_IR_NONE);
                    tier1_emit_test_node(&function, 5, IR_BITCAST, precision == 0 ? IR_I64 : IR_I32, 4, BUSTER_IR_NONE);
                    tier1_emit_test_node(&function, 6, precision == 0 ? IR_COPY : IR_ZEXT, IR_I64, 5, BUSTER_IR_NONE);
                    tier1_emit_test_node(&function, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE);
                    Tier1SelectPlan plan;
                    uint32_t node = BUSTER_IR_NONE;
                    const bool planned = tier1_select_plan(&function, NULL, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
                    tier1_emit_test_expect(test, planned, "select an indexed precision conversion");
                    if (planned)
                    {
                        tier1_emit_test_expect(test, (plan.covered[3] != 0) == (observable == 0) &&
                                                     plan.patterns[4].rule == (observable ? TIER1_RULE_FLOAT_CONVERT : TIER1_RULE_FLOAT_CONVERT_LOAD),
                                               "a precision conversion folds only the ordinary single-use load");
                    }
                    const Tier1EmitOptions options =
                    {
                        .register_mask = tier1_liveness_register_mask(),
                        .vector_register_mask = 3,
                        .rematerialize = true,
                        .certify = true,
                    };
                    Tier1EmitResult emitted;
                    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
                    const bool certified = error == TIER1_EMIT_OK && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
                    tier1_emit_test_expect(test, certified, "precision conversion allocation and emitted code certify at each target");
                    void* executable = MAP_FAILED;
                    uint64_t bytes = 0;
                    if (certified && tier1_emit_test_load_unmeasured(test, &emitted.code, &executable, &bytes))
                    {
                        uint8_t* input = memory + page - source_width;
                        for (uint32_t mode = 0; mode < 16; mode += 1)
                        {
                            const uint32_t control = UINT32_C(0x1f80) | ((mode & 3) << 13) | ((mode & 4) << 4) | ((mode & 8) << 12);
                            for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(patterns[precision]); sample += 1)
                            {
                                const uint64_t bits = patterns[precision][sample];
                                memcpy(input, &bits, source_width);
                                _mm_setcsr(control);
                                const uint64_t expected = tier1_float_resize_load_oracle(bits, source_width);
                                const uint32_t expected_flags = _mm_getcsr() & 63;
                                _mm_setcsr(control);
                                const uint64_t actual = tier1_float_resize_load_call(executable, input - 9);
                                const uint32_t actual_flags = _mm_getcsr() & 63;
                                _mm_setcsr(saved_mxcsr);
                                tier1_emit_test_expect(test, actual == expected && actual_flags == expected_flags,
                                                       "memory precision conversion preserves result bits and all MXCSR exception flags");
                            }
                        }
                    }
                    if (executable != MAP_FAILED)
                    {
                        munmap(executable, bytes);
                    }
                }
                arena_pop_to(&test->output, 0);
                arena_pop_to(&test->scratch, 0);
                arena_pop_to(&test->input, 0);
            }
        }
    }
    _mm_setcsr(saved_mxcsr);
    test->allowed = original_allowed;
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 2);
    }
    free(allowed);
}
