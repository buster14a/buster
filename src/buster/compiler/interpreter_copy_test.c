// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed COPY's execution, representation-byte and malformed-stream regressions (#176). Included by
// interpreter_test.c: the public interpreter must consume both pointers, preserve lower stack values, carry
// definedness without reading padding as a scalar, and leave both bytes and metadata unchanged on refusal.

static void interpreter_test_copy_ranges(InterpreterTestTally* tally)
{
    // Keep an unregistered gap: one-past a range must not name the next registered allocation.
    uint8_t storage[2][32];
    uint8_t* source = storage[0];
    uint8_t* destination = storage[1];
    uint8_t source_defined[16];
    uint8_t destination_defined[16];
    for (uint32_t i = 0; i < 16; i += 1)
    {
        source[i] = (uint8_t)(17 + i);
        source_defined[i] = (uint8_t)(i % 2);
    }
    memset(destination, 0xa5, 16);
    memset(destination_defined, 1, sizeof(destination_defined));
    InterpreterMemory memories[] = {
        {
            .bytes = source,
            .size = 16,
            .defined = source_defined,
        },
        {
            .bytes = destination,
            .size = 16,
            .defined = destination_defined,
            .writable = true,
        },
    };
    InterpreterRuntime runtime = {
        .memories = memories,
        .memory_count = BUSTER_ARRAY_LENGTH(memories),
        .memory_capacity = BUSTER_ARRAY_LENGTH(memories),
        .check_memory = true,
        .check_undefined = true,
    };
    const uint64_t from = (uint64_t)(uintptr_t)source;
    const uint64_t to = (uint64_t)(uintptr_t)destination;
    InterpreterStatus status = interpreter_typed_copy(&runtime, to + 3, from + 2, 8);
    interpreter_test_expect(tally, status == INTERPRETER_OK && memcmp(destination + 3, source + 2, 8) == 0 &&
                            memcmp(destination_defined + 3, source_defined + 2, 8) == 0, "COPY transports undefined representation bytes and map offsets");
    bool guards = true;
    for (uint32_t i = 0; i < 16; i += 1)
    {
        if (i < 3 || i >= 11)
        {
            guards &= destination[i] == 0xa5 && destination_defined[i] == 1;
        }
    }
    interpreter_test_expect(tally, guards, "COPY preserves bytes and metadata outside its destination");
    uint8_t saved[16];
    uint8_t saved_defined[16];
    memcpy(saved, destination, sizeof(saved));
    memcpy(saved_defined, destination_defined, sizeof(saved_defined));
    status = interpreter_typed_copy(&runtime, to, to, 16);
    interpreter_test_expect(tally, status == INTERPRETER_OK && memcmp(saved, destination, sizeof(saved)) == 0 &&
                            memcmp(saved_defined, destination_defined, sizeof(saved_defined)) == 0, "COPY self-assignment does not define padding");
    status = interpreter_typed_copy(&runtime, to + 1, to, 8);
    interpreter_test_expect(tally, status == INTERPRETER_INVALID, "COPY refuses partial overlap");
    status = interpreter_typed_copy(&runtime, to, to + 1, 8);
    interpreter_test_expect(tally, status == INTERPRETER_INVALID, "COPY refuses reverse partial overlap");
    memories[1].writable = false;
    status = interpreter_typed_copy(&runtime, to, from, 8);
    interpreter_test_expect(tally, status == INTERPRETER_MEMORY, "COPY checks destination writability");
    memories[1].writable = true;
    const uint64_t bad_sources[] = {
        0,
        1,
        from + 16,
        from + 12,
        UINT64_MAX - 3,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad_sources); i += 1)
    {
        status = interpreter_typed_copy(&runtime, to, bad_sources[i], 8);
        interpreter_test_expect(tally, status == INTERPRETER_MEMORY, "COPY source bounds checked before host reads");
    }
    const uint64_t bad_destinations[] = {
        0,
        1,
        to + 16,
        to + 12,
        UINT64_MAX - 3,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad_destinations); i += 1)
    {
        status = interpreter_typed_copy(&runtime, bad_destinations[i], from, 8);
        interpreter_test_expect(tally, status == INTERPRETER_MEMORY, "COPY destination bounds checked before host writes");
    }
    interpreter_test_expect(tally, interpreter_typed_copy(&runtime, to, from, 0) == INTERPRETER_INVALID &&
                            interpreter_typed_copy(&runtime, to, from, (uint64_t)BUSTER_TIER0_MAX_COPY_BYTES + 1) == INTERPRETER_INVALID, "COPY byte-count contract");
    runtime.check_memory = false;
    interpreter_test_expect(tally, interpreter_typed_copy(&runtime, to + 12, from, 8) == INTERPRETER_MEMORY &&
                            interpreter_typed_copy(&runtime, to, from + 12, 8) == INTERPRETER_MEMORY, "unchecked COPY cannot escape a known allocation");
    interpreter_test_expect(tally, memcmp(saved, destination, sizeof(saved)) == 0 &&
                            memcmp(saved_defined, destination_defined, sizeof(saved_defined)) == 0, "refused COPY is transactional for bytes and metadata");
    runtime.check_memory = true;
    memories[0].defined = 0;
    status = interpreter_typed_copy(&runtime, to, from, 16);
    bool defined = true;
    for (uint32_t i = 0; i < 16; i += 1)
    {
        defined &= destination_defined[i] == 1;
    }
    interpreter_test_expect(tally, status == INTERPRETER_OK && defined && memcmp(destination, source, 16) == 0, "COPY from untracked defined source defines destination");
    memories[0].defined = source_defined;
    runtime.check_undefined = false;
    status = interpreter_typed_copy(&runtime, to, from, 16);
    interpreter_test_expect(tally, status == INTERPRETER_OK && memcmp(destination_defined, source_defined, 16) == 0, "COPY retains supplied metadata with undefined diagnostics disabled");
    runtime.check_memory = false;
    runtime.memory_count = 0;
    memset(destination, 0, 16);
    status = interpreter_typed_copy(&runtime, to, from, 16);
    interpreter_test_expect(tally, status == INTERPRETER_OK && memcmp(destination, source, 16) == 0, "unchecked COPY accepts valid unregistered host buffers");
}

static void interpreter_test_copy_dispatch(InterpreterTestTally* tally, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint64_t capacity = (uint64_t)BUSTER_TIER0_MAX_COPY_BYTES + 2;
    uint8_t* source = arena_push(scratch, capacity, 1);
    uint8_t* destination = arena_push(scratch, capacity, 1);
    const bool allocated = source != 0 && destination != 0;
    interpreter_test_expect(tally, allocated, "COPY test allocation");
    if (allocated)
    {
        for (uint64_t i = 0; i < capacity; i += 1)
        {
            source[i] = (uint8_t)(i * 37 + 11);
        }
        uint8_t kinds[] = {
            BUSTER_TYPED_CONST,
            BUSTER_TYPED_GLOBAL_ADDRESS,
            BUSTER_TYPED_GLOBAL_ADDRESS,
            BUSTER_TYPED_COPY,
            BUSTER_TYPED_RETURN,
        };
        uint8_t classes[] = {
            BUSTER_TYPED_CLASS_I32,
            BUSTER_TYPED_CLASS_U64,
            BUSTER_TYPED_CLASS_U64,
            BUSTER_TYPED_CLASS_VOID,
            BUSTER_TYPED_CLASS_I32,
        };
        uint64_t values[] = {
            77,
            2,
            3,
            1,
            0,
        };
        Tier0Function function = {
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = BUSTER_ARRAY_LENGTH(kinds),
            .symbol_count = 4,
        };
        const SymbolId symbol = 1;
        InterpreterBinding bindings[] = {
            {
                .symbol = 2,
                .bytes = destination + 1,
                .size = BUSTER_TIER0_MAX_COPY_BYTES,
                .writable = true,
            },
            {
                .symbol = 3,
                .bytes = source + 1,
                .size = BUSTER_TIER0_MAX_COPY_BYTES,
            },
        };
        const InterpreterProgram program = {
            .functions = &function,
            .function_symbols = &symbol,
            .function_count = 1,
            .bindings = bindings,
            .binding_count = BUSTER_ARRAY_LENGTH(bindings),
        };
        const uint32_t sizes[] = {
            1,
            3,
            8,
            15,
            16,
            31,
            BUSTER_TIER0_MAX_COPY_BYTES,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
        {
            const uint32_t size = sizes[i];
            if (size <= BUSTER_TIER0_MAX_COPY_BYTES)
            {
                memset(destination, 0xa5, capacity);
                values[3] = size;
                const uint64_t start = scratch->length;
                InterpreterResult result;
                const bool ran = interpreter_typed_run(&program, symbol, 0, 0, scratch, 0, &result);
                interpreter_test_expect(tally, ran && result.status == INTERPRETER_OK && result.value.low == 77 &&
                                        scratch->length == start && memcmp(destination + 1, source + 1, size) == 0 &&
                                        destination[0] == 0xa5 && destination[size + 1] == 0xa5, "typed COPY consumes exactly two pointers and preserves lower values and guards");
            }
        }
        const uint64_t invalid_sizes[] = {
            0,
            (uint64_t)BUSTER_TIER0_MAX_COPY_BYTES + 1,
            UINT64_MAX,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_sizes); i += 1)
        {
            values[3] = invalid_sizes[i];
            InterpreterResult result;
            const uint64_t start = scratch->length;
            const bool ran = interpreter_typed_run(&program, symbol, 0, 0, scratch, 0, &result);
            interpreter_test_expect(tally, !ran && result.status == INTERPRETER_INVALID && result.entry == 3 && scratch->length == start, "typed COPY rejects invalid byte counts");
        }
        values[3] = 1;
        classes[3] = BUSTER_TYPED_CLASS_U64;
        InterpreterResult result;
        bool ran = interpreter_typed_run(&program, symbol, 0, 0, scratch, 0, &result);
        interpreter_test_expect(tally, !ran && result.status == INTERPRETER_INVALID && result.entry == 3, "typed COPY requires VOID result class");
        classes[3] = BUSTER_TYPED_CLASS_VOID;
        for (uint32_t operand = 1; operand <= 2; operand += 1)
        {
            kinds[operand] = BUSTER_TYPED_CONST;
            classes[operand] = BUSTER_TYPED_CLASS_U32;
            ran = interpreter_typed_run(&program, symbol, 0, 0, scratch, 0, &result);
            interpreter_test_expect(tally, !ran && result.status == INTERPRETER_INVALID && result.entry == 3, "typed COPY requires two pointer classes");
            kinds[operand] = BUSTER_TYPED_GLOBAL_ADDRESS;
            classes[operand] = BUSTER_TYPED_CLASS_U64;
        }
        bindings[0].writable = false;
        ran = interpreter_typed_run(&program, symbol, 0, 0, scratch, 0, &result);
        interpreter_test_expect(tally, !ran && result.status == INTERPRETER_MEMORY && result.entry == 3, "typed COPY propagates memory refusal");
        // Exact logical slices with zero or one operand, including the malformed non-VOID COPY that used to reach
        // the default binary handler with top == 1 and read stack[top - 2]. No trailing input entries are required.
        for (uint32_t operands = 0; operands <= 1; operands += 1)
        {
            function.kinds = kinds + 3 - operands;
            function.classes = classes + 3 - operands;
            function.values = values + 3 - operands;
            function.count = operands + 1;
            for (uint32_t malformed = 0; malformed <= 1; malformed += 1)
            {
                classes[3] = malformed ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID;
                const uint64_t start = scratch->length;
                ran = interpreter_typed_run(&program, symbol, 0, 0, scratch, 0, &result);
                interpreter_test_expect(tally, !ran && result.status == INTERPRETER_INVALID && result.entry == operands && scratch->length == start, "typed COPY rejects underflow before reading operands");
            }
        }
    }
    arena_pop_to(scratch, mark);
}

static void interpreter_test_copy_defined(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_COPY,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_LOAD,
        BUSTER_TYPED_RETURN,
    };
    uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U8,
    };
    uint64_t values[] = {
        0,
        0x5a,
        0,
        1,
        0,
        8,
        1,
        0,
        0,
    };
    const uint8_t local_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
    };
    const uint8_t local_flags[] = {
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    };
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = BUSTER_ARRAY_LENGTH(local_classes),
    };
    InterpreterResult result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 0x5a, "typed COPY accepts partial source initialization and reads its defined byte");
    classes[7] = BUSTER_TYPED_CLASS_U64;
    classes[8] = BUSTER_TYPED_CLASS_U64;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_UNDEFINED && result.entry == 7, "typed COPY preserves undefined bytes for a later scalar read");
    values[3] = 0;
    values[6] = 0;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_UNDEFINED && result.entry == 7, "typed self-COPY does not hide undefined reads");
}

static void interpreter_test_copy_native(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_COPY,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint64_t values[] = {
        0,
        1,
        8,
        0,
        0,
    };
    const uint8_t locals[] = {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint8_t flags[] = {
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    };
    const AbiLocation parameters[] = {
        {
            .registers = {
                BUSTER_ABI_RDI,
                BUSTER_ABI_REGISTER_NONE,
            },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
        {
            .registers = {
                BUSTER_ABI_RSI,
                BUSTER_ABI_REGISTER_NONE,
            },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
    };
    const Tier0Function typed = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = locals,
        .local_flags = flags,
        .local_count = BUSTER_ARRAY_LENGTH(locals),
        .parameter_count = BUSTER_ARRAY_LENGTH(parameters),
        .parameters = parameters,
        .result = {
            .kind = BUSTER_ABI_RESULT_REGISTERS,
            .part_bytes = 8,
            .registers = {
                BUSTER_ABI_RAX,
                BUSTER_ABI_REGISTER_NONE,
                BUSTER_ABI_REGISTER_NONE,
                BUSTER_ABI_REGISTER_NONE,
            },
        },
    };
    Arena arena = arena_reserve(32ull << 20);
    Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Functions, 1);
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    Tier0Program* generated = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Program, 1);
    bool ok = functions != 0 && code != 0 && code_bytes != 0 && generated != 0;
    interpreter_test_expect(tally, ok, "COPY native test allocations");
    if (ok)
    {
        *functions = (Tier0Functions){
            .count = 1,
        };
        functions->functions[0] = typed;
        *code = (Tier0TestCode){
            .code = code_bytes,
        };
        *generated = (Tier0Program){
            .function_count = 1,
        };
        Tier0TestTally native_tally = { 0 };
        ok = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && tier0_validate(&typed).error == TIER0_OK &&
             tier0_test_build(&native_tally, functions, &arena, code, 176);
        interpreter_test_expect(tally, ok && native_tally.failures == 0, "COPY fixture compiles identically through both Tier 0 paths");
    }
    if (ok)
    {
        const Tier0TestData data = { 0 };
        uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
        tier0_test_inputs(176, inputs);
        Tier0TestOutcome native[BUSTER_TIER0_TEST_INPUTS];
        int status = 0;
        ok = tier0_test_run(generated, code, &data, inputs, native, &status);
        interpreter_test_expect(tally, ok, "COPY fixture executes natively");
        const SymbolId symbol = 1;
        const InterpreterProgram program = {
            .functions = &typed,
            .function_symbols = &symbol,
            .function_count = 1,
        };
        for (uint32_t r = 0; ok && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            const InterpreterValue arguments[] = {
                {
                    .low = inputs[r][0],
                },
                {
                    .low = inputs[r][1],
                },
            };
            const uint64_t mark = scratch->length;
            InterpreterResult result;
            const bool ran = interpreter_typed_run(&program, symbol, arguments, BUSTER_ARRAY_LENGTH(arguments), scratch, 0, &result);
            interpreter_test_expect(tally, ran && result.status == INTERPRETER_OK && result.value.low == inputs[r][1] &&
                                    result.value.low == native[r].result && scratch->length == mark, "typed COPY agrees with independent expected value and native Tier 0");
        }
    }
    arena_release(&arena);
}

static void interpreter_test_copy(InterpreterTestTally* tally, Arena* scratch)
{
    interpreter_test_copy_ranges(tally);
    interpreter_test_copy_dispatch(tally, scratch);
    interpreter_test_copy_defined(tally, scratch);
    interpreter_test_copy_native(tally, scratch);
}
