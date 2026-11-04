// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original SIMD IR admission controls, included after ir_test.c's fixture helpers.

// A mask's bitcasts (#179): to and from the integer of its bits, and to itself, which is the boundary a mask's typed-stream
// conversions lower through (ir_typed_convert_mask), but no other: not a narrower or wider integer, a float, a pointer
// or another mask. The pairs written out, each way, beside the scalar bitcasts the rule kept.
static void ir_test_simd_bitcasts(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const uint8_t pairs[][3] = {
        { IR_I8, IR_M8, true }, { IR_M8, IR_I8, true }, { IR_I16, IR_M16, true }, { IR_M16, IR_I16, true },
        { IR_I32, IR_M32, true }, { IR_M32, IR_I32, true }, { IR_I64, IR_M64, true }, { IR_M64, IR_I64, true },
        { IR_M8, IR_M8, true }, { IR_M64, IR_M64, true }, { IR_I32, IR_F32, true }, { IR_F64, IR_I64, true },
        { IR_I16, IR_M8, false }, { IR_M8, IR_I16, false }, { IR_I8, IR_M16, false }, { IR_M32, IR_I64, false },
        { IR_M16, IR_M8, false }, { IR_M32, IR_M16, false }, { IR_M8, IR_M64, false }, { IR_F32, IR_M32, false },
        { IR_M32, IR_F32, false }, { IR_F64, IR_M64, false }, { IR_M64, IR_F64, false }, { IR_PTR, IR_M64, false },
        { IR_M64, IR_PTR, false }, { IR_I1, IR_M8, false }, { IR_I128, IR_M64, false }, { IR_M16, IR_V4I32, false },
    };
    for (uint32_t p = 0; p < sizeof(pairs) / sizeof(*pairs); p += 1)
    {
        const uint64_t mark = output->length;
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 3, .block_count = 1 };
        if (ir_create(output, &sizes, &function) == IR_OK)
        {
            function.result_type = IR_VOID;
            function.block_offsets[1] = 3;
            ir_test_instruction(&function, 0, IR_UNDEF, (IrType)pairs[p][0], BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_BITCAST, (IrType)pairs[p][1], 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_pair(test, &function, scratch, NULL, pairs[p][2] != 0,
                         pairs[p][2] != 0 ? "a mask's bitcast to or from the integer of its bits, or a scalar's of its width, is refused" :
                                            "a bitcast between a mask and anything but the integer of its bits or itself is accepted");
        }
        else
        {
            ir_test_expect(test, false, "a bitcast fixture allocates");
        }
        arena_pop_to(output, mark);
    }
}

// A row call (#141, IrCall.row): a masked byte addition's row of two vectors and a mask, its result a vector, and a masked
// store's of an address, a vector and a mask, of no result, each a call of no symbol whose types are its row's classes'
// (ir_row_type). Both verify, both scans find SIMD values, and the interpreter refuses them. Then each changed one way,
// each refused: a symbol, a row past the column, an argument or the result of another type, an argument too few, a
// variadic call, an indirect one, and a row's operand of a class no row call takes (a vector of 8 bytes).
static void ir_test_simd_rows(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    IrFunction function = { 0 };
    const IrSizes sizes = { .instruction_count = 7, .block_count = 1, .extra_count = 6, .call_count = 2, .call_type_count = 6, .parameter_count = 4, .row_count = 2 };
    const bool ready = ir_create(output, &sizes, &function) == IR_OK;
    ir_test_expect(test, ready, "a row call fixture allocates its rows");
    if (ready)
    {
        function.result_type = IR_V16I32;
        function.block_offsets[1] = 7;
        const uint8_t parameters[4] = { IR_V16I32, IR_V16I32, IR_M64, IR_I64 };
        memcpy(function.parameter_types, parameters, sizeof(parameters));
        Tier0Operation* add = &function.rows[0];
        add->records[0] = (X86Instruction){ .form = X86_FORM_VPADDB_Z_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1),
                                            .mode = BUSTER_X86_REGISTER_DIRECT, .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
        add->record_count = 1;
        add->operand_count = 3;
        add->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        add->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        add->operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        add->operand_bytes[0] = 64;
        add->operand_bytes[1] = 64;
        add->operand_bytes[2] = 8;
        add->result_class = BUSTER_TYPED_CLASS_VECTOR;
        add->result_bytes = 64;
        Tier0Operation* store = &function.rows[1];
        store->records[0] = (X86Instruction){ .form = X86_FORM_VMOVDQU8_ZM512_Z, .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE,
                                              .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
        store->record_count = 1;
        store->operand_count = 3;
        store->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        store->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        store->operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        store->operand_bytes[1] = 64;
        store->operand_bytes[2] = 8;
        store->result_class = BUSTER_TYPED_CLASS_VOID;
        const uint32_t extra[6] = { 0, 1, 2, 3, 4, 2 };
        memcpy(function.extra, extra, sizeof(extra));
        const uint8_t types[6] = { IR_V16I32, IR_V16I32, IR_M64, IR_I64, IR_V16I32, IR_M64 };
        memcpy(function.call_types, types, sizeof(types));
        function.calls[0] = (IrCall){ .argument_first = 0, .argument_count = 3, .type_first = 0, .parameter_count = 3, .result_type = IR_V16I32, .row = 1 };
        function.calls[1] = (IrCall){ .argument_first = 3, .argument_count = 3, .type_first = 3, .parameter_count = 3, .result_type = IR_VOID, .row = 2 };
        for (uint32_t p = 0; p < 4; p += 1)
        {
            ir_test_instruction(&function, p, IR_PARAM, (IrType)parameters[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_test_instruction(&function, 4, IR_CALL, IR_V16I32, 0, 0, BUSTER_IR_NONE);
        ir_test_instruction(&function, 5, IR_CALL, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(&function, 6, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_pair(test, &function, scratch, NULL, true, "row calls of a vector result and of none verify");
        ir_test_expect(test, ir_function_has_simd_path(&function, true) && ir_function_has_simd_path(&function, false) && ir_function_has_vectors_path(&function, true) &&
                       ir_function_has_vectors_path(&function, false), "a function with machine rows has SIMD values on both scans' paths");
        ir_test_expect(test, !ir_interpreter_simd_supported(&function), "the interpreter refuses a machine row, which it has no model of");
        ir_test_printer(test, &function, output, scratch);
        function.a[4] = 1;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call names no symbol");
        function.a[4] = 0;
        function.calls[0].row = 3;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call's row is in the rows' column");
        function.calls[0].row = 1;
        function.call_types[2] = IR_M32;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call's argument is of its operand's class's type");
        function.call_types[2] = IR_M64;
        function.type[4] = IR_V8I32;
        function.calls[0].result_type = IR_V8I32;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call's result is of its row's result's type");
        function.type[4] = IR_V16I32;
        function.calls[0].result_type = IR_V16I32;
        function.calls[0].argument_count = 2;
        function.calls[0].parameter_count = 2;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call has an argument per operand of its row");
        function.calls[0].argument_count = 3;
        function.calls[0].parameter_count = 3;
        function.calls[0].variadic = true;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call isn't variadic");
        function.calls[0].variadic = false;
        function.op[4] = IR_CALL_INDIRECT;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call is a direct call's");
        function.op[4] = IR_CALL;
        add->operand_bytes[0] = 8;
        ir_test_pair(test, &function, scratch, NULL, false, "a vector of 8 bytes is no row call's operand");
        add->operand_bytes[0] = 64;
        ir_test_pair(test, &function, scratch, NULL, true, "the row calls verify again once restored");
    }
    arena_pop_to(output, mark);
}

static void ir_test_simd(IrTestTally* test, Arena* output, Arena* scratch)
{
    ir_test_simd_bitcasts(test, output, scratch);
    ir_test_simd_rows(test, output, scratch);
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
