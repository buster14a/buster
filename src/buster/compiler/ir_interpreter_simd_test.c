// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Shared-interpreter SIMD tests, independent of the typed adapter and straight-line SIMD executor.

static void ir_interpreter_test_splats(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    static const IrType scalars[] = { IR_I8, IR_I16, IR_I32, IR_I64, IR_F32, IR_F64 };
    // Integer boundaries and raw floating encodings, including both zero signs, denormals and signaling/quiet NaNs.
    // No floating operation computes the expectation: a splat must not quiet a NaN or change its payload.
    static const uint64_t patterns[] = {
        0, 1, UINT64_MAX, UINT64_C(0x8000000000000081), UINT64_C(0x80000000),
        UINT64_C(0x7f800001), UINT64_C(0x7fc12345), UINT64_C(0x7ff0000000000001),
        UINT64_C(0x7ff8123456789abc), UINT64_C(0x8000000000000000),
    };
    const uint64_t mark = memory->length;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t scalar = 0; scalar < BUSTER_ARRAY_LENGTH(scalars); scalar += 1)
        {
            const IrType type = scalars[scalar];
            const uint32_t scalar_bytes = ir_type_bits[type] / 8;
            const uint32_t bytes = 16u << width;
            IrFunction function = {0};
            const IrSizes sizes = {
                .instruction_count = 5,
                .block_count = 1,
                .parameter_count = 2,
            };
            const bool created = ir_create(memory, &sizes, &function) == IR_OK;
            ir_interpreter_test_expect(tally, created, "scalar splat execution fixture allocation");
            if (created)
            {
                uint8_t actual[80];
                uint8_t expected[80];
                function.parameter_types[0] = IR_PTR;
                function.parameter_types[1] = (uint8_t)type;
                function.block_offsets[1] = 5;
                ir_interpreter_test_instruction(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_interpreter_test_instruction(&function, 1, IR_PARAM, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_interpreter_test_instruction(&function, 2, IR_INTRINSIC, (IrType)(IR_V4I32 + width), 1, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT);
                ir_interpreter_test_instruction(&function, 3, IR_STORE, IR_VOID, 0, 2, BUSTER_IR_NONE);
                ir_interpreter_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_interpreter_test_expect(tally, ir_verify_complete(&function, execution).error == IR_OK,
                                           "the complete verifier accepts a splat's single scalar use");
                const InterpreterBinding object = { .symbol = 2, .bytes = actual, .size = sizeof(actual), .writable = true };
                const IrInterpreterFunction binding = { .symbol = 71, .function = &function };
                const IrInterpreterProgram program = { .functions = &binding, .function_count = 1, .bindings = &object, .binding_count = 1 };
                for (uint32_t pattern = 0; pattern < BUSTER_ARRAY_LENGTH(patterns); pattern += 1)
                {
                    const uint64_t bits = patterns[pattern];
                    const InterpreterValue arguments[] = { { .low = (uintptr_t)(actual + 8) }, { .low = bits } };
                    memset(actual, 0xa5, sizeof(actual));
                    memset(expected, 0xa5, sizeof(expected));
                    for (uint32_t byte = 0; byte < bytes; byte += 1)
                    {
                        expected[8 + byte] = (uint8_t)(bits >> (8 * (byte % scalar_bytes)));
                    }
                    ir_interpreter_test_run(tally, &program, 71, arguments, 2, 0, execution, INTERPRETER_OK, (InterpreterValue){0},
                                            "scalar splats execute without an absent second operand");
                    ir_interpreter_test_expect(tally, memcmp(actual, expected, sizeof(actual)) == 0,
                                               "every scalar splat lane preserves the source bits and exact-width canaries");
                }
                function.op[1] = IR_UNDEF;
                function.a[1] = BUSTER_IR_NONE;
                const InterpreterValue arguments[] = { { .low = (uintptr_t)(actual + 8) }, {0} };
                memset(actual, 0xa5, sizeof(actual));
                memset(expected, 0xa5, sizeof(expected));
                ir_interpreter_test_run(tally, &program, 71, arguments, 2, 0, execution, INTERPRETER_UNDEFINED, (InterpreterValue){0},
                                        "a scalar splat consumes its undefined source");
                ir_interpreter_test_expect(tally, memcmp(actual, expected, sizeof(actual)) == 0, "undefined splats refuse before their store");
            }
            arena_pop_to(memory, mark);
        }
    }
}

static void ir_interpreter_test_simd_permute(IrInterpreterTestTally* tally, Arena* memory)
{
    const uint64_t mark = memory->length;
    IrFunction function = {0};
    const IrSizes sizes = { .instruction_count = 4, .block_count = 1, .extra_count = 1 };
    const bool created = ir_create(memory, &sizes, &function) == IR_OK;
    ir_interpreter_test_expect(tally, created, "SIMD permute fixture allocation");
    if (created)
    {
        // %0 is a scalar. The two tables of permute2 name the same vector, whose first half reads its
        // second half and vice versa. Even placing the result in that table must use its original bytes.
        uint32_t slots[] = { BUSTER_IR_NONE, 0, 1, 2 };
        IrSimdValue vectors[3] = {0};
        IrInterpreterFrame frame = { .function = &function, .vector_indices = slots, .vector_values = vectors };
        function.type[0] = IR_I32;
        function.extra[0] = 1;
        for (uint32_t width = 0; width < 3; width += 1)
        {
            const IrType type = (IrType)(IR_V4I32 + width);
            const IrSimdShape shape = ir_simd_shape(type);
            const uint32_t bytes = 16u << width;
            IrSimdValue table = {0};
            IrSimdValue indices = {0};
            for (uint32_t byte = 0; byte < bytes; byte += 1)
            {
                ((uint8_t*)table.words)[byte] = (uint8_t)(3 * byte + 1);
                ((uint8_t*)indices.words)[byte] = (uint8_t)(bytes - 1 - byte + (byte & 1) * bytes);
            }
            const IrSimdValue* operands[] = { &table, &indices, &table };
            IrSimdAction action = { .operation = IR_SIMD_PERMUTE2, .result = shape, .operands = { shape, shape, shape } };
            function.type[1] = type;
            function.type[2] = type;
            ir_interpreter_test_instruction(&function, 3, IR_INTRINSIC, type, 1, 2, ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_PERMUTE2, 0));
            for (uint32_t part = 0; part <= 4; part += 1)
            {
                action.index = (uint8_t)part;
                function.flags[3] = (uint8_t)part;
                vectors[0] = table;
                vectors[1] = indices;
                IrSimdValue expected = table;
                const bool valid = ir_simd_evaluate(&action, operands, NULL, NULL, 0, &expected);
                const InterpreterStatus status = ir_interpreter_simd_operation(&frame, 3, &vectors[0]);
                ir_interpreter_test_expect(tally, valid == (part < 4) && status == (valid ? INTERPRETER_OK : INTERPRETER_UNSUPPORTED) &&
                                           memcmp(vectors, &expected, sizeof(expected)) == 0 && memcmp(vectors + 1, &indices, sizeof(indices)) == 0,
                                           "in-place permute matches the lane model and an unsupported size leaves the table untouched");
            }
        }
    }
    arena_pop_to(memory, mark);
}

static void ir_interpreter_test_simd_conversions(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    // The existing scalar interpreter decodes the significand before truncating and checking range. Require the
    // vector interpreter to give the same value or arithmetic status, including negative unsigned fractions.
    static const uint64_t inputs[][2] = {
        {
            0,
            0,
        },
        {
            0x80000000,
            UINT64_C(0x8000000000000000),
        },
        {
            0x3fe00000,
            UINT64_C(0x3ffc000000000000),
        },
        {
            0xbfe00000,
            UINT64_C(0xbffc000000000000),
        },
        {
            0xbf000000,
            UINT64_C(0xbfe0000000000000),
        },
        {
            0xbf7fffff,
            UINT64_C(0xbfefffffffffffff),
        },
        {
            0xbf800000,
            UINT64_C(0xbff0000000000000),
        },
        {
            0x80000001,
            UINT64_C(0x8000000000000001),
        },
        {
            0xcf000000,
            UINT64_C(0xc3e0000000000000),
        },
        {
            0xcf000001,
            UINT64_C(0xc3e0000000000001),
        },
        {
            0x4effffff,
            UINT64_C(0x43dfffffffffffff),
        },
        {
            0x4f000000,
            UINT64_C(0x43e0000000000000),
        },
        {
            0x4f7fffff,
            UINT64_C(0x43efffffffffffff),
        },
        {
            0x4f800000,
            UINT64_C(0x43f0000000000000),
        },
        {
            0x7f800000,
            UINT64_C(0x7ff0000000000000),
        },
        {
            0xff800000,
            UINT64_C(0xfff0000000000000),
        },
        {
            0x7fc12345,
            UINT64_C(0x7ff8123456789abc),
        },
        {
            0x7f800001,
            UINT64_C(0x7ff0000000000001),
        },
    };
    const uint64_t mark = memory->length;
    const uint32_t saved = _mm_getcsr();
    _mm_setcsr(0x1f80);
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrType type = (IrType)(IR_V4I32 + width);
        const uint32_t bytes = 16u << width;
        IrFunction function = {0};
        const IrSizes sizes = {
            .instruction_count = 5,
            .block_count = 1,
            .parameter_count = 1,
            .vector_constant_count = 1,
        };
        const bool created = ir_create(memory, &sizes, &function) == IR_OK;
        ir_interpreter_test_expect(tally, created, "packed conversion execution fixture allocation");
        if (created)
        {
            uint8_t actual[80];
            uint8_t expected[80];
            function.parameter_types[0] = IR_PTR;
            function.block_offsets[1] = 5;
            ir_interpreter_test_instruction(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 1, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 3, IR_STORE, IR_VOID, 0, 2, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const InterpreterBinding object = {
                .symbol = 2,
                .bytes = actual,
                .size = sizeof(actual),
                .writable = true,
            };
            const IrInterpreterFunction binding = {
                .symbol = 71,
                .function = &function,
            };
            const IrInterpreterProgram program = {
                .functions = &binding,
                .function_count = 1,
                .bindings = &object,
                .binding_count = 1,
            };
            const InterpreterValue argument = { .low = (uintptr_t)(actual + 8) };
            for (uint32_t wide = 0; wide < 2; wide += 1)
            {
                const uint32_t element = 4u << wide;
                const uint64_t one = wide != 0 ? UINT64_C(0x3ff0000000000000) : 0x3f800000;
                const uint64_t integer_one = 1;
                for (uint32_t byte = 0; byte < bytes; byte += element)
                {
                    memcpy((uint8_t*)function.vector_constants[0].words + byte, &one, element);
                }
                for (uint32_t sign = 0; sign < 2; sign += 1)
                {
                    ir_interpreter_test_instruction(&function, 2, sign != 0 ? IR_FPTOSI : IR_FPTOUI, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.flags[2] = wide != 0 ? IR_SIMD_LANE_F64 : IR_SIMD_LANE_F32;
                    ir_interpreter_test_expect(tally, ir_verify_complete(&function, execution).error == IR_OK,
                                               "same-width signed and unsigned conversions verify with F32 and F64 lanes");
                    for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(inputs); sample += 1)
                    {
                        const uint64_t bits = inputs[sample][wide];
                        unsigned __int128 converted = 0;
                        const InterpreterStatus status = interpreter_value_float_integer((InterpreterValue){ .low = bits },
                                                          wide != 0 ? IR_F64 : IR_F32, wide != 0 ? IR_I64 : IR_I32, sign != 0, &converted);
                        const uint64_t value = (uint64_t)converted;
                        // Put the boundary case last so a failed lane cannot publish earlier converted lanes.
                        memcpy((uint8_t*)function.vector_constants[0].words + bytes - element, &bits, element);
                        memset(actual, 0xa5, sizeof(actual));
                        memset(expected, 0xa5, sizeof(expected));
                        if (status == INTERPRETER_OK)
                        {
                            for (uint32_t byte = 0; byte < bytes; byte += element)
                            {
                                memcpy(expected + 8 + byte, &integer_one, element);
                            }
                            memcpy(expected + 8 + bytes - element, &value, element);
                        }
                        ir_interpreter_test_run(tally, &program, 71, &argument, 1, NULL, execution, status, (InterpreterValue){0},
                                                "packed conversion values and arithmetic failures match the existing scalar interpreter");
                        ir_interpreter_test_expect(tally, memcmp(actual, expected, sizeof(actual)) == 0,
                                                   "packed conversion stores exact-width results or stops before any store on arithmetic failure");
                    }
                    IrSimdValue source = {0};
                    memcpy(source.words, function.vector_constants[0].words, bytes);
                    uint32_t slots[] = {
                        BUSTER_IR_NONE,
                        0,
                        1,
                        BUSTER_IR_NONE,
                        BUSTER_IR_NONE,
                    };
                    const IrInterpreterFrame frame = {
                        .function = &function,
                        .vector_indices = slots,
                        .vector_values = &source,
                    };
                    const IrSimdValue before = source;
                    function.flags[2] = IR_SIMD_LANE_COUNT;
                    ir_interpreter_test_expect(tally, ir_interpreter_simd_operation(&frame, 2, &source) == INTERPRETER_UNSUPPORTED &&
                                               memcmp(&source, &before, sizeof(source)) == 0,
                                               "invalid conversion lane flags remain unsupported and leave the destination untouched");
                    function.flags[2] = wide != 0 ? IR_SIMD_LANE_F64 : IR_SIMD_LANE_F32;
                    if (width < 2)
                    {
                        source.words[bytes / 8] = 1;
                        const IrSimdValue malformed = source;
                        ir_interpreter_test_expect(tally, ir_interpreter_simd_operation(&frame, 2, &source) == INTERPRETER_UNSUPPORTED &&
                                                   memcmp(&source, &malformed, sizeof(source)) == 0,
                                                   "malformed conversion input tails remain unsupported rather than arithmetic failures");
                    }
                }
            }
        }
        arena_pop_to(memory, mark);
    }
    _mm_setcsr(saved);
}

static void ir_interpreter_test_simd(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    ir_interpreter_test_splats(tally, memory, execution);
    ir_interpreter_test_simd_permute(tally, memory);
    ir_interpreter_test_simd_conversions(tally, memory, execution);
    const uint64_t mark = memory->length;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrType type = (IrType)(IR_V4I32 + width);
        const IrType mask = width == 2 ? IR_M16 : IR_M8;
        const uint32_t bytes = 16u << width;
        const uint64_t active = (UINT64_C(1) << (bytes / 4)) - 1;
        IrFunction loop = {0};
        const IrSizes sizes = {
            .instruction_count = 16,
            .block_count = 3,
            .constant_count = 3,
            .vector_constant_count = 2,
            .extra_count = 12,
        };
        const bool created = ir_create(memory, &sizes, &loop) == IR_OK;
        ir_interpreter_test_expect(tally, created, "SIMD phi fixture allocation");
        if (created)
        {
            loop.result_type = mask;
            loop.constants[0].low = 0;
            loop.constants[1].low = 1;
            loop.constants[2].low = 2;
            memset(loop.vector_constants[0].words, 0x35, bytes);
            memset(loop.vector_constants[1].words, 0xca, bytes);
            const uint32_t offsets[] = { 0, 6, 12, 16 };
            const uint32_t incoming[] = { 0, 0, 1, 7, 0, 1, 1, 6, 0, 2, 1, 9 };
            memcpy(loop.block_offsets, offsets, sizeof(offsets));
            memcpy(loop.extra, incoming, sizeof(incoming));
            ir_interpreter_test_instruction(&loop, 0, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 1, IR_VCONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            for (uint32_t c = 0; c < 3; c += 1)
            {
                ir_interpreter_test_instruction(&loop, c + 2, IR_CONST, IR_I64, c, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            ir_interpreter_test_instruction(&loop, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 6, IR_PHI, type, 0, 2, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 7, IR_PHI, type, 4, 2, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 8, IR_PHI, IR_I64, 8, 2, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 9, IR_ADD, IR_I64, 8, 3, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 10, IR_ICMP, IR_I1, 9, 4, IR_LT);
            ir_interpreter_test_instruction(&loop, 11, IR_CBR, IR_VOID, 10, 1, 2);
            ir_interpreter_test_instruction(&loop, 12, IR_VCMP, mask, 6, 1, IR_EQ);
            ir_interpreter_test_instruction(&loop, 13, IR_VCMP, mask, 7, 0, IR_EQ);
            ir_interpreter_test_instruction(&loop, 14, IR_AND, mask, 12, 13, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&loop, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const IrInterpreterFunction binding = { .symbol = 71, .function = &loop };
            const IrInterpreterProgram program = { .functions = &binding, .function_count = 1 };
            ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = active }, "SIMD phi cycles snapshot every incoming vector before writes");
            InterpreterOptions options = interpreter_default_options();
            options.max_steps = 8;
            const InterpreterResult limited = ir_interpreter_test_run(tally, &program, 71, 0, 0, &options, execution, INTERPRETER_STEP_LIMIT, (InterpreterValue){0}, "SIMD phis respect step budget");
            ir_interpreter_test_expect(tally, limited.steps == 8, "SIMD budget exact stop");
            options = interpreter_default_options();
            options.max_values = 1;
            ir_interpreter_test_run(tally, &program, 71, 0, 0, &options, execution, INTERPRETER_VALUE_LIMIT, (InterpreterValue){0}, "SIMD storage respects value budget");
        }
        arena_pop_to(memory, mark);
        IrFunction access = {0};
        const IrSizes access_sizes = {
            .instruction_count = 6,
            .block_count = 1,
            .parameter_count = 1,
            .vector_constant_count = 1,
        };
        const bool allocated = ir_create(memory, &access_sizes, &access) == IR_OK;
        ir_interpreter_test_expect(tally, allocated, "SIMD memory fixture allocation");
        if (allocated)
        {
            uint8_t actual[66];
            memset(actual, 0xa5, sizeof(actual));
            access.result_type = mask;
            access.parameter_types[0] = IR_PTR;
            access.block_offsets[1] = 6;
            memset(access.vector_constants[0].words, 0x37, bytes);
            ir_interpreter_test_instruction(&access, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&access, 1, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&access, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&access, 3, IR_LOAD, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&access, 4, IR_VCMP, mask, 3, 1, IR_EQ);
            ir_interpreter_test_instruction(&access, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            access.flags[2] = BUSTER_IR_VOLATILE;
            access.flags[3] = BUSTER_IR_VOLATILE;
            InterpreterBinding object = { .symbol = 2, .bytes = actual + 1, .size = bytes, .writable = true };
            const IrInterpreterFunction binding = { .symbol = 71, .function = &access };
            const IrInterpreterProgram program = { .functions = &binding, .function_count = 1, .bindings = &object, .binding_count = 1 };
            const InterpreterValue argument = { .low = (uintptr_t)(actual + 1) };
            ir_interpreter_test_run(tally, &program, 71, &argument, 1, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = active }, "unaligned volatile vectors load/store their exact width");
            ir_interpreter_test_expect(tally, actual[0] == 0xa5 && actual[bytes + 1] == 0xa5 && memcmp(actual + 1, access.vector_constants[0].words, bytes) == 0, "SIMD exact-width memory canaries");
            memset(actual, 0xa5, sizeof(actual));
            object.size -= 1;
            ir_interpreter_test_run(tally, &program, 71, &argument, 1, 0, execution, INTERPRETER_MEMORY, (InterpreterValue){0}, "short vector binding refuses before a partial store");
            bool unchanged = true;
            for (uint32_t i = 0; i < sizeof(actual); i += 1)
            {
                unchanged &= actual[i] == 0xa5;
            }
            ir_interpreter_test_expect(tally, unchanged, "failed SIMD store preserves all bytes");
            object.size += 1;
            object.writable = false;
            ir_interpreter_test_run(tally, &program, 71, &argument, 1, 0, execution, INTERPRETER_MEMORY, (InterpreterValue){0}, "read-only vector binding refuses writes");
        }
        arena_pop_to(memory, mark);
    }
    IrFunction select = {0};
    const IrSizes sizes = {
        .instruction_count = 6,
        .block_count = 1,
        .constant_count = 1,
        .vector_constant_count = 2,
    };
    const bool created = ir_create(memory, &sizes, &select) == IR_OK;
    ir_interpreter_test_expect(tally, created, "SIMD select fixture allocation");
    if (created)
    {
        select.result_type = IR_I128;
        select.block_offsets[1] = 6;
        select.constants[0].low = 0xff;
        select.vector_constants[0].words[0] = UINT64_C(0x0123456789abcdef);
        select.vector_constants[0].words[1] = UINT64_C(0xfedcba9876543210);
        ir_interpreter_test_instruction(&select, 0, IR_CONST, IR_M8, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&select, 1, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&select, 2, IR_UNDEF, IR_V4I32, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&select, 3, IR_VSELECT, IR_V4I32, 0, 1, 2);
        ir_interpreter_test_instruction(&select, 4, IR_BITCAST, IR_I128, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&select, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction binding = { .symbol = 71, .function = &select };
        const IrInterpreterProgram program = { .functions = &binding, .function_count = 1 };
        const InterpreterValue expected = { .low = select.vector_constants[0].words[0], .high = select.vector_constants[0].words[1] };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, expected, "all-active select ignores its undef alternative and bitcasts to i128");
        select.constants[0].low = 0xf0;
        select.b[3] = 2;
        select.c[3] = 1;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, expected, "v4 select ignores high mask bits and unselected undef lanes");
        select.constants[0].low = 1;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_UNDEFINED, (InterpreterValue){0}, "one selected undef lane is a semantic use");
        // Each operand can be undefined independently. Only the low four mask bits select i32 lanes.
        select.b[3] = 1;
        select.c[3] = 2;
        select.vector_constants[1].words[0] = ~select.vector_constants[0].words[0];
        select.vector_constants[1].words[1] = ~select.vector_constants[0].words[1];
        const uint8_t masks[] = { 0xf0, 0xa5, 0xff };
        for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(masks); m += 1)
        {
            select.constants[0].low = masks[m];
            const IrSimdValue bits = { .words = { masks[m] } };
            const IrSimdValue* operands[] = { &bits, select.vector_constants, select.vector_constants + 1 };
            const IrSimdAction action = { .operation = IR_SIMD_SELECT, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_M8, IR_SIMD_V4I32, IR_SIMD_V4I32 } };
            IrSimdValue lanes = {0};
            ir_interpreter_test_expect(tally, ir_simd_evaluate(&action, operands, NULL, NULL, 0, &lanes), "select lane model");
            const uint32_t used = 1u | ((masks[m] & 15) != 0 ? 2u : 0u) | ((masks[m] & 15) != 15 ? 4u : 0u);
            for (uint32_t undef = 0; undef < 8; undef += 1)
            {
                for (uint32_t p = 0; p < 3; p += 1)
                {
                    const bool missing = (undef & (1u << p)) != 0;
                    select.op[p] = missing ? IR_UNDEF : p == 0 ? IR_CONST : IR_VCONST;
                    select.a[p] = missing ? BUSTER_IR_NONE : (uint32_t)(p == 2);
                }
                const InterpreterStatus status = (used & undef) != 0 ? INTERPRETER_UNDEFINED : INTERPRETER_OK;
                ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, status, (InterpreterValue){ .low = lanes.words[0], .high = lanes.words[1] },
                                        "select reads the mask and only the definedness of its active alternatives");
            }
        }
        ir_interpreter_test_instruction(&select, 0, IR_CONST, IR_M8, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        select.op[1] = IR_CONST;
        select.type[1] = IR_I128;
        select.a[1] = 0;
        select.op[2] = IR_BITCAST;
        select.a[2] = 1;
        select.constants[0] = (IrConstant){ .low = 1 };
        select.b[3] = 2;
        select.c[3] = 2;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 1 }, "i128-to-v4-to-i128 preserves bits");
    }
    arena_pop_to(memory, mark);
}
