// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original SIMD IR admission controls, included after ir_test.c's fixture helpers.

static void ir_test_simd(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 6, .block_count = 1, .vector_constant_count = 2 };
        const bool ready = ir_create(output, &sizes, &function) == IR_OK;
        ir_test_expect(test, ready, "SIMD IR fixture allocates sparse constant payloads");
        if (ready)
        {
            const IrType type = (IrType)(IR_V4I32 + width);
            const IrType mask = width == 2 ? IR_M16 : IR_M8;
            function.result_type = type;
            function.block_offsets[1] = 6;
            function.vector_constants[0].words[0] = UINT64_C(0x0000000200000001);
            function.vector_constants[1].words[0] = UINT64_C(0x0000000100000002);
            ir_test_instruction(&function, 0, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_VCONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_ADD, type, 0, 1, BUSTER_IR_NONE);
            ir_test_instruction(&function, 3, IR_VCMP, mask, 0, 1, IR_LT);
            ir_test_instruction(&function, 4, IR_VSELECT, type, 3, 2, 1);
            ir_test_instruction(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_pair(test, &function, scratch, NULL, true, "first-class vector arithmetic, masks and predication verify");
            const IrSimdExecution executed = ir_simd_execute(&function, NULL, 0, NULL, scratch);
            ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_OK && executed.type == type &&
                           executed.value.words[0] == UINT64_C(0x0000000100000003) && ir_simd_value_valid(&executed.value, ir_simd_shape(type)),
                           "real IR arithmetic/compare-to-mask/predication executes against independent lane expectations");
            ir_test_printer(test, &function, output, scratch);
            function.type[3] = mask == IR_M8 ? IR_M16 : IR_M8;
            ir_test_pair(test, &function, scratch, NULL, false, "comparison cannot silently narrow or widen its predicate mask");
            function.type[3] = mask;
            function.type[2] = IR_I128;
            ir_test_pair(test, &function, scratch, NULL, false, "an equal-bit-width scalar is not a vector lane shape");
            function.type[2] = type;
            function.c[3] = IR_PREDICATE_COUNT;
            ir_test_pair(test, &function, scratch, NULL, false, "unknown vector predicates are rejected");
            function.c[3] = IR_LT;
            function.vector_constants[0].words[7] = width == 2 ? 0 : 1;
            ir_test_pair(test, &function, scratch, NULL, width == 2, "constant payload bits outside the exact vector shape are rejected");
            function.vector_constants[0].words[7] = 0;
            function.op[0] = IR_CONST;
            ir_test_pair(test, &function, scratch, NULL, false, "ordinary two-word constants cannot masquerade as wide vector constants");
            function.op[0] = IR_VCONST;
            function.flags[4] = BUSTER_IR_VOLATILE;
            ir_test_pair(test, &function, scratch, NULL, false, "pure vector selection cannot carry unmodeled memory effects");
        }
        arena_pop_to(output, mark);
    }
    IrFunction scalar = { 0 };
    const IrSizes ordinary = { .instruction_count = 1, .block_count = 1 };
    const bool ready = ir_create(output, &ordinary, &scalar) == IR_OK;
    ir_test_expect(test, ready && scalar.vector_constants == NULL && scalar.vector_constant_count == 0,
                   "ordinary scalar functions allocate no wide payload rows");
    arena_pop_to(output, mark);
    IrFunction preserved = { .instruction_count = 123 };
    const IrSizes huge = { .vector_constant_count = (uint64_t)UINT32_MAX + 1 };
    ir_test_expect(test, ir_create(output, &huge, &preserved) == IR_ERROR_LIMIT && preserved.instruction_count == 123 && output->length == mark,
                   "wide payload count overflow rejects before narrowing or publication");
    IrFunction memory = { 0 };
    const IrSizes memory_sizes = { .instruction_count = 5, .block_count = 1, .parameter_count = 1, .vector_constant_count = 1 };
    if (ir_create(output, &memory_sizes, &memory) == IR_OK)
    {
        memory.result_type = IR_V16I32;
        memory.parameter_types[0] = IR_PTR;
        memory.block_offsets[1] = 5;
        memory.vector_constants[0].words[7] = UINT64_C(0xfedcba9876543210);
        ir_test_instruction(&memory, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 1, IR_VCONST, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 3, IR_LOAD, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        memory.flags[2] = BUSTER_IR_VOLATILE;
        memory.flags[3] = BUSTER_IR_VOLATILE;
        ir_test_pair(test, &memory, scratch, NULL, true, "vector memory retains explicit pointer operands and volatile effects");
        uint8_t bytes[66];
        memset(bytes, 0xa5, sizeof(bytes));
        IrSimdMemory slice = { .bytes = bytes + 1, .size = 64 };
        IrSimdArgument argument = { 0 };
        IrSimdExecution executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_OK && executed.value.words[7] == UINT64_C(0xfedcba9876543210) &&
                       slice.reads == 64 && slice.writes == 64 && slice.volatile_accesses == 128 && bytes[0] == 0xa5 && bytes[65] == 0xa5,
                       "real IR vector memory roundtrip preserves ordered observable effects and exact source tails");
        argument.offset = 1;
        const IrSimdMemory before = slice;
        executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_ACCESS && executed.instruction == 2 && memcmp(&slice, &before, sizeof(slice)) == 0,
                       "real IR oracle rejects the first invalid vector access without performing it");
        memory.alignment_log2[2] = 6;
        slice.size = 65;
        executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_ACCESS && executed.instruction == 2 && slice.writes == before.writes && bytes[65] == 0xa5,
                       "promised alignment is checked against logical offsets before a vector memory access");
        memory.alignment_log2[2] = 0;
        slice.size = 64;
        argument.offset = 0;
        IrFunction with_argument = memory;
        uint8_t parameter_types[2] = { IR_PTR, IR_M8 };
        with_argument.parameter_count = 2;
        with_argument.parameter_types = parameter_types;
        const IrSimdValue malformed_argument = { .words = { 256 } };
        const IrSimdArgument arguments[2] = { argument, { .value = &malformed_argument } };
        executed = ir_simd_execute(&with_argument, arguments, 2, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_INVALID && memcmp(&slice, &before, sizeof(slice)) == 0,
                       "all arguments, including unused ones, are admitted before any vector store can occur");
        _Alignas(64) uint8_t small_bytes[16384];
        uint32_t failures = 0;
        uint32_t successes = 0;
        for (uint64_t capacity = 13; capacity <= sizeof(small_bytes); capacity += 127)
        {
            memset(small_bytes, 0xa5, 13);
            Arena small = { .base = small_bytes, .length = 13, .capacity = capacity };
            IrSimdMemory limited = slice;
            executed = ir_simd_execute(&memory, &argument, 1, &limited, &small);
            const bool complete = executed.status == IR_SIMD_EXECUTION_OK;
            failures += !complete;
            successes += complete;
            bool prefix = true;
            for (uint32_t byte = 0; byte < 13; byte += 1)
            {
                prefix &= small_bytes[byte] == 0xa5;
            }
            ir_test_expect(test, small.length == 13 && prefix && (complete || (executed.status == IR_SIMD_EXECUTION_RESOURCE &&
                           memcmp(&limited, &slice, sizeof(slice)) == 0)), "oracle allocation failure restores scratch/prefix and performs no memory access");
        }
        ir_test_expect(test, failures != 0 && successes != 0, "oracle resource sweep crosses allocation failure and success boundaries");
        memory.op[1] = IR_UNDEF;
        memory.a[1] = BUSTER_IR_NONE;
        argument.offset = 0;
        executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_UNSUPPORTED && executed.instruction == 1 && memcmp(&slice, &before, sizeof(slice)) == 0,
                       "an unsupported semantic form refuses before any memory operation instead of guessing a scalar value");
    }
    else
    {
        ir_test_expect(test, false, "vector memory fixture allocates");
    }
    arena_pop_to(output, mark);
}
