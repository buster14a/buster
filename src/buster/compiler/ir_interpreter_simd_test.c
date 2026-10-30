// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Shared-interpreter SIMD tests, independent of the typed adapter and straight-line SIMD executor.

static void ir_interpreter_test_simd(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
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
        .vector_constant_count = 1,
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
        select.op[1] = IR_CONST;
        select.type[1] = IR_I128;
        select.op[2] = IR_BITCAST;
        select.a[2] = 1;
        select.constants[0] = (IrConstant){ .low = 1 };
        select.b[3] = 2;
        select.c[3] = 2;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 1 }, "i128-to-v4-to-i128 preserves bits");
    }
    arena_pop_to(memory, mark);
}
