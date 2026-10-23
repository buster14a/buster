// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Handwritten execution and failure oracles for #176. Include after interpreter.c and interpreter_native.c.
// Native-code comparison uses the same production engine from tier0_test.c; these cases exercise its independent
// arithmetic boundaries, recursive frames, allocation lifetimes, undefined reads, exact input tails and budgets.

typedef struct InterpreterTestTally InterpreterTestTally;
struct InterpreterTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static void interpreter_test_expect(InterpreterTestTally* tally, bool condition, const char* name)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 20)
        {
            fprintf(stderr, "test-interpreter: %s\n", name);
        }
        tally->failures += 1;
    }
}

static InterpreterResult interpreter_test_run(const Tier0Function* function, Arena* scratch, const InterpreterOptions* options)
{
    const SymbolId symbol = 1;
    const InterpreterProgram program = {
        .functions = function,
        .function_symbols = &symbol,
        .function_count = 1,
    };
    InterpreterResult result;
    const uint64_t start = scratch->length;
    const bool ran = interpreter_typed_run(&program, 1, 0, 0, scratch, options, &result);
    if (ran != (result.status == INTERPRETER_OK) || scratch->length != start)
    {
        result.status = INTERPRETER_INVALID;
    }
    return result;
}

static void interpreter_test_arithmetic(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_RETURN,
    };
    uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    uint64_t values[] = {
        (uint64_t)-19,
        4,
        0,
        0,
    };
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = 4,
    };
    const uint8_t operations[] = {
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_SUB,
        BUSTER_TYPED_MUL,
        BUSTER_TYPED_DIV,
        BUSTER_TYPED_MOD,
        BUSTER_TYPED_AND,
        BUSTER_TYPED_OR,
        BUSTER_TYPED_XOR,
        BUSTER_TYPED_SHL,
        BUSTER_TYPED_SHR,
    };
    const uint64_t expected[] = {
        (uint64_t)-15,
        (uint64_t)-23,
        (uint64_t)-76,
        (uint64_t)-4,
        (uint64_t)-3,
        4,
        (uint64_t)-19,
        (uint64_t)-23,
        (uint64_t)-304,
        (uint64_t)-2,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
    {
        kinds[2] = operations[i];
        const InterpreterResult result = interpreter_test_run(&function, scratch, 0);
        interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == expected[i] && result.value.high == 0, "signed arithmetic");
    }
    kinds[2] = BUSTER_TYPED_DIV;
    values[1] = (uint64_t)-1;
    InterpreterResult result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 19, "ordinary negative-one divisor");
    values[0] = 0x8000000000000000ull;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_ARITHMETIC, "signed division overflow");
    values[0] = 19;
    values[1] = 0;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_ARITHMETIC, "division by zero");
    values[0] = 255;
    values[1] = 2;
    kinds[2] = BUSTER_TYPED_ADD;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        classes[i] = BUSTER_TYPED_CLASS_U8;
    }
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 1, "narrow arithmetic wraps");
    values[0] = 127;
    values[1] = 1;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        classes[i] = BUSTER_TYPED_CLASS_I8;
    }
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == (uint64_t)-128, "narrow signed normalization");
    for (uint32_t i = 0; i < 2; i += 1)
    {
        classes[i] = BUSTER_TYPED_CLASS_F64;
    }
    classes[2] = BUSTER_TYPED_CLASS_I32;
    classes[3] = BUSTER_TYPED_CLASS_I32;
    values[0] = 0x7ff8000000000000ull;
    values[1] = 0x3ff0000000000000ull;
    for (uint32_t operation = BUSTER_TYPED_EQ; operation <= BUSTER_TYPED_GE; operation += 1)
    {
        kinds[2] = (uint8_t)operation;
        result = interpreter_test_run(&function, scratch, 0);
        interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == (operation == BUSTER_TYPED_NE), "NaN comparison");
    }
}

static void interpreter_test_conversions(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONVERT,
        BUSTER_TYPED_RETURN,
    };
    uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_F64,
        BUSTER_TYPED_CLASS_F64,
    };
    uint64_t values[] = {
        11,
        0,
        0,
    };
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = 3,
    };
    for (uint32_t from = BUSTER_TYPED_CLASS_I8; from < BUSTER_TYPED_CLASSES; from += 1)
    {
        classes[0] = (uint8_t)from;
        values[0] = from == BUSTER_TYPED_CLASS_F32 ? 0x41300000ull : from == BUSTER_TYPED_CLASS_F64 ? 0x4026000000000000ull : 11;
        for (uint32_t to = BUSTER_TYPED_CLASS_I8; to < BUSTER_TYPED_CLASSES; to += 1)
        {
            classes[1] = (uint8_t)to;
            classes[2] = (uint8_t)to;
            const uint64_t expected = to == BUSTER_TYPED_CLASS_F32 ? 0x41300000ull : to == BUSTER_TYPED_CLASS_F64 ? 0x4026000000000000ull : 11;
            const InterpreterResult result = interpreter_test_run(&function, scratch, 0);
            interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == expected, "every scalar conversion pair");
        }
    }
    classes[0] = BUSTER_TYPED_CLASS_F64;
    classes[1] = BUSTER_TYPED_CLASS_U64;
    classes[2] = BUSTER_TYPED_CLASS_U64;
    values[0] = 0x43e0000000000000ull;
    InterpreterResult result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 0x8000000000000000ull, "unsigned floating conversion above signed range");
    values[0] = 0x7ff0000000000000ull;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_ARITHMETIC, "infinite floating conversion");
}

static void interpreter_test_recursion(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_LE,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SUB,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_MUL,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint64_t values[] = {
        0,
        1,
        0,
        6,
        1,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
    };
    const uint8_t local_class = BUSTER_TYPED_CLASS_I64;
    const uint32_t first = 0;
    const uint32_t count = 1;
    const uint8_t variadic = 0;
    const SymbolId symbol = 1;
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = &local_class,
        .local_count = 1,
        .parameter_count = 1,
        .call_targets = &symbol,
        .call_first = &first,
        .call_argument_counts = &count,
        .call_variadic = &variadic,
        .argument_classes = &local_class,
        .call_count = 1,
        .argument_count = 1,
    };
    const InterpreterProgram program = {
        .functions = &function,
        .function_symbols = &symbol,
        .function_count = 1,
    };
    const InterpreterValue argument = {
        .low = 10,
    };
    InterpreterResult result;
    bool ok = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    interpreter_test_expect(tally, ok && result.value.low == 3628800, "recursive factorial preserves caller stack");
    InterpreterOptions options = interpreter_default_options();
    options.max_frames = 4;
    ok = interpreter_typed_run(&program, 1, &argument, 1, scratch, &options, &result);
    interpreter_test_expect(tally, !ok && result.status == INTERPRETER_FRAME_LIMIT, "recursive frame limit");
    options = interpreter_default_options();
    options.max_locals = 4;
    ok = interpreter_typed_run(&program, 1, &argument, 1, scratch, &options, &result);
    interpreter_test_expect(tally, !ok && result.status == INTERPRETER_RESOURCE, "recursive local capacity");
    ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, !ok && result.status == INTERPRETER_INVALID, "entry argument count");
    Tier0Function mismatched[] = {
        function,
        function,
    };
    const SymbolId symbols[] = {
        1,
        2,
    };
    const SymbolId target = 2;
    const uint8_t floating = BUSTER_TYPED_CLASS_F64;
    mismatched[0].call_targets = &target;
    mismatched[1].local_classes = &floating;
    const InterpreterProgram bad_program = {
        .functions = mismatched,
        .function_symbols = symbols,
        .function_count = 2,
    };
    ok = interpreter_typed_run(&bad_program, 1, &argument, 1, scratch, 0, &result);
    interpreter_test_expect(tally, !ok && result.status == INTERPRETER_INVALID, "callee parameter class mismatch");
}

static void interpreter_test_memory(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kinds[] = {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_RETURN,
    };
    uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
    };
    uint64_t values[] = {
        0,
        77,
        0,
        0,
        0,
    };
    const uint8_t local_class = BUSTER_TYPED_CLASS_I32;
    Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = &local_class,
        .local_count = 1,
    };
    InterpreterResult result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 77, "pointer writes initialize aliased locals");
    function.kinds = kinds + 3;
    function.classes = classes + 3;
    function.values = values + 3;
    function.count = 2;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_UNDEFINED, "uninitialized local read");
    InterpreterOptions options = interpreter_default_options();
    options.check_undefined = false;
    result = interpreter_test_run(&function, scratch, &options);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK, "undefined checking can be disabled");
    const uint8_t load_kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_LOAD,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t load_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
    };
    const uint64_t load_values[] = {
        1,
        0,
        0,
    };
    function.kinds = load_kinds;
    function.classes = load_classes;
    function.values = load_values;
    function.count = 3;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_MEMORY, "unknown pointer rejected before host access");
    uint64_t bytes = 5;
    InterpreterMemory memories[2];
    uint8_t defined[8] = { 0 };
    InterpreterRuntime runtime = {
        .memories = memories,
        .memory_capacity = 2,
        .check_memory = true,
        .check_undefined = true,
    };
    bool added = interpreter_memory_add(&runtime, (uint8_t*)&bytes, 8, defined, false, 1);
    interpreter_test_expect(tally, added, "memory registration");
    const uint64_t address = (uint64_t)(uintptr_t)&bytes;
    interpreter_test_expect(tally, interpreter_memory_access(&runtime, address, 4, true) == INTERPRETER_MEMORY, "readonly write");
    interpreter_test_expect(tally, interpreter_memory_access(&runtime, address + 8, 1, false) == INTERPRETER_MEMORY, "one-past access");
    interpreter_test_expect(tally, interpreter_memory_access(&runtime, address + 7, 2, false) == INTERPRETER_MEMORY, "allocation crossing");
    interpreter_test_expect(tally, interpreter_memory_access(&runtime, UINT64_MAX, 2, false) == INTERPRETER_MEMORY, "address overflow");
    interpreter_test_expect(tally, interpreter_memory_access(&runtime, address, 4, false) == INTERPRETER_UNDEFINED, "defined-byte read checking");
    interpreter_memory_remove_owner(&runtime, 1);
    interpreter_test_expect(tally, interpreter_memory_access(&runtime, address, 1, false) == INTERPRETER_MEMORY, "returned frame allocation lifetime");
    added = interpreter_memory_add(&runtime, (uint8_t*)&bytes, 8, 0, true, 0);
    interpreter_memory_remove(&runtime, (uint8_t*)&bytes);
    interpreter_test_expect(tally, added && runtime.memory_count == 0, "heap allocation removal");
}

static void interpreter_test_limits(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kind = BUSTER_TYPED_JUMP;
    uint8_t class = BUSTER_TYPED_CLASS_VOID;
    uint64_t value = 0;
    Tier0Function function = {
        .kinds = &kind,
        .classes = &class,
        .values = &value,
        .count = 1,
    };
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = 19;
    InterpreterResult result = interpreter_test_run(&function, scratch, &options);
    interpreter_test_expect(tally, result.status == INTERPRETER_STEP_LIMIT && result.steps == 19, "bounded infinite loop");
    kind = BUSTER_TYPED_ADD;
    class = BUSTER_TYPED_CLASS_I64;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "stack underflow");
    kind = BUSTER_TYPED_JUMP;
    class = BUSTER_TYPED_CLASS_VOID;
    value = UINT64_MAX;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "invalid branch target");
    function.count = 0;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 0, "empty void function");
    const uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_DUP,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
    };
    const uint64_t values[] = {
        1,
        0,
        0,
    };
    function.kinds = kinds;
    function.classes = classes;
    function.values = values;
    function.count = 3;
    options = interpreter_default_options();
    options.max_values = 1;
    result = interpreter_test_run(&function, scratch, &options);
    interpreter_test_expect(tally, result.status == INTERPRETER_VALUE_LIMIT, "value stack capacity");
    Arena tiny = arena_reserve(4096);
    result = interpreter_test_run(&function, &tiny, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_RESOURCE && tiny.length == 0, "arena exhaustion releases scratch");
    arena_release(&tiny);
}

static double interpreter_test_native_mixed(int64_t a, double b, int64_t c, double d)
{
    const double result = (double)a + b + (double)c + d;
    return result;
}

static void interpreter_test_native(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_F64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_F64,
        BUSTER_TYPED_CLASS_F64,
        BUSTER_TYPED_CLASS_F64,
    };
    const uint64_t values[] = {
        2,
        0x400c000000000000ull,
        4,
        0xbff4000000000000ull,
        0,
        0,
    };
    const SymbolId symbol = 1;
    const SymbolId native_symbol = 2;
    const uint32_t first = 0;
    const uint32_t count = 4;
    const uint8_t variadic = 0;
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .call_targets = &native_symbol,
        .call_first = &first,
        .call_argument_counts = &count,
        .call_variadic = &variadic,
        .argument_classes = classes,
        .call_count = 1,
        .argument_count = 4,
    };
    const InterpreterNativeBinding native = {
        .symbol = native_symbol,
        .address = (void*)interpreter_test_native_mixed,
    };
    InterpreterProgram program = {
        .functions = &function,
        .function_symbols = &symbol,
        .function_count = 1,
        .natives = &native,
        .native_count = 1,
    };
    InterpreterResult result;
    bool ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, ok && result.value.low == 0x4020800000000000ull, "typed mixed integer and floating native call");
    program.native_count = 0;
    ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, !ok && result.status == INTERPRETER_SYMBOL, "unresolved native symbol");
}

static void interpreter_test_heap(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_LOAD,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
    };
    const uint64_t values[] = {
        8,
        0,
        0,
        0,
        0x5a,
        8,
        1,
        0,
        0,
        0,
        1,
        0,
        2,
        1,
        0,
    };
    const uint8_t local_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
    };
    const SymbolId symbol = 1;
    const SymbolId targets[] = {
        2,
        3,
        4,
    };
    const uint32_t first[] = {
        0,
        1,
        4,
    };
    const uint32_t counts[] = {
        1,
        3,
        1,
    };
    const uint8_t variadic[] = {
        0,
        0,
        0,
    };
    const uint8_t arguments[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
    };
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = local_classes,
        .local_count = 2,
        .call_targets = targets,
        .call_first = first,
        .call_argument_counts = counts,
        .call_variadic = variadic,
        .argument_classes = arguments,
        .call_count = 3,
        .argument_count = 5,
    };
    const InterpreterNativeBinding natives[] = {
        {
            .symbol = 2,
            .address = (void*)malloc,
            .flags = BUSTER_INTERPRETER_NATIVE_ALLOCATE,
        },
        {
            .symbol = 3,
            .address = (void*)memset,
            .flags = BUSTER_INTERPRETER_NATIVE_SET,
        },
        {
            .symbol = 4,
            .address = (void*)free,
            .flags = BUSTER_INTERPRETER_NATIVE_FREE,
        },
    };
    const InterpreterProgram program = {
        .functions = &function,
        .function_symbols = &symbol,
        .function_count = 1,
        .natives = natives,
        .native_count = BUSTER_ARRAY_LENGTH(natives),
    };
    InterpreterResult result;
    const bool ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, ok && result.value.low == 0x5a5a5a5a5a5a5a5aull, "typed malloc memset load and free with allocation checks");
}

static void interpreter_test_mappings(InterpreterTestTally* tally, Arena* scratch)
{
    const Tier0Function functions[] = {
        { 0 },
        { 0 },
    };
    SymbolId symbols[] = {
        1,
        2,
    };
    uint64_t bytes = 0;
    InterpreterBinding bindings[] = {
        {
            .symbol = 3,
            .bytes = (uint8_t*)&bytes,
            .size = sizeof(bytes),
            .writable = true,
        },
        {
            .symbol = 4,
            .bytes = (uint8_t*)&bytes,
            .size = sizeof(bytes),
            .writable = true,
        },
    };
    InterpreterNativeBinding natives[] = {
        {
            .symbol = 5,
            .address = (void*)interpreter_test_native_mixed,
        },
        {
            .symbol = 6,
            .address = (void*)interpreter_test_native_mixed,
        },
    };
    const InterpreterProgram program = {
        .functions = functions,
        .function_symbols = symbols,
        .function_count = BUSTER_ARRAY_LENGTH(functions),
        .bindings = bindings,
        .binding_count = BUSTER_ARRAY_LENGTH(bindings),
        .natives = natives,
        .native_count = BUSTER_ARRAY_LENGTH(natives),
    };
    InterpreterResult result;
    bool ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, ok && result.status == INTERPRETER_OK, "distinct IDs may alias memory and native addresses");
    SymbolId* ids[] = {
        &symbols[0],
        &symbols[1],
        &bindings[0].symbol,
        &bindings[1].symbol,
        &natives[0].symbol,
        &natives[1].symbol,
    };
    const SymbolId reserved[] = {
        0,
        UINT32_MAX,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
    {
        const SymbolId saved = *ids[i];
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(reserved); r += 1)
        {
            *ids[i] = reserved[r];
            const uint64_t mark = scratch->length;
            ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
            interpreter_test_expect(tally, !ok && result.status == INTERPRETER_INVALID && scratch->length == mark, "reserved IDs rejected before execution");
        }
        *ids[i] = saved;
    }
    for (uint32_t i = 1; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
    {
        const SymbolId saved = *ids[i];
        for (uint32_t p = 0; p < i; p += 1)
        {
            *ids[i] = *ids[p];
            ok = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
            interpreter_test_expect(tally, !ok && result.status == INTERPRETER_INVALID, "duplicate and cross-kind IDs rejected before execution");
        }
        *ids[i] = saved;
    }
    interpreter_test_expect(tally, interpreter_mappings_valid(0, 0, 0, 0, 0, 0, 0), "empty mapping slices");
    interpreter_test_expect(tally, !interpreter_mappings_valid(symbols, 1, 0, 0, 0, 0, 0), "undersized function-symbol stride");
    interpreter_test_expect(tally, !interpreter_mappings_valid((const void*)(uintptr_t)(UINT64_MAX - 3), 1, sizeof(SymbolId), 0, 0, 0, 0), "function-symbol address overflow");
}

static void interpreter_test_guarded(InterpreterTestTally* tally, Arena* scratch)
{
    const uint32_t counts[] = {
        0,
        1,
        2,
        15,
        16,
        17,
        63,
        64,
        65,
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 6, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool protected = mapping != MAP_FAILED;
    if (protected)
    {
        protected = mprotect(mapping + page, page, PROT_NONE) == 0 && mprotect(mapping + 3 * page, page, PROT_NONE) == 0 && mprotect(mapping + 5 * page, page, PROT_NONE) == 0;
    }
    interpreter_test_expect(tally, protected, "guarded input allocation");
    for (uint32_t c = 0; protected && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        const uint32_t count = counts[c];
        uint8_t* kinds = mapping + page - count;
        uint8_t* classes = mapping + 3 * page - count;
        uint64_t* values = (uint64_t*)(mapping + 5 * page - (uint64_t)count * 8);
        for (uint32_t i = 0; i < count; i += 1)
        {
            kinds[i] = i % 2 == 0 ? BUSTER_TYPED_CONST : BUSTER_TYPED_DISCARD;
            classes[i] = BUSTER_TYPED_CLASS_I32;
            values[i] = i;
        }
        if (count % 2 != 0)
        {
            kinds[count - 1] = BUSTER_TYPED_RETURN;
            classes[count - 1] = BUSTER_TYPED_CLASS_VOID;
        }
        const Tier0Function function = {
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = count,
        };
        const InterpreterResult result = interpreter_test_run(&function, scratch, 0);
        interpreter_test_expect(tally, result.status == INTERPRETER_OK, "exact unpadded columns at guarded tails");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 6);
    }
}

static void interpreter_test_programs(InterpreterTestTally* tally, Arena* scratch, uint64_t seed, uint32_t count)
{
    Arena arena = arena_reserve(1ull << 30);
    Tier0Tables tables;
    bool ready = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && tier0_tables_open(&tables);
    interpreter_test_expect(tally, ready, "generated-program tables");
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    uint8_t* globals = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
    const bool allocated = code != 0 && code_bytes != 0 && globals != 0;
    interpreter_test_expect(tally, allocated, "generated-program memory");
    const Tier0ProgramOptions shape = {
        .functions = 4,
        .statements = 16,
        .expression_nodes = 14,
        .calls = true,
        .loops = true,
        .floating = true,
        .pointers = true,
        .defined = true,
    };
    for (uint32_t p = 0; ready && allocated && p < count; p += 1)
    {
        const uint64_t mark = arena.length;
        const uint64_t program_seed = seed + p;
        Tier0Program* source = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Program, 1);
        Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Functions, 1);
        bool ok = source != 0 && functions != 0 && tier0_generate_program(source, &arena, program_seed, shape, "oracle_");
        ok = ok && tier0_program_functions(source, &tables, &arena, functions);
        interpreter_test_expect(tally, ok, "generated typed streams");
        Tier0TestTally native_tally = { 0 };
        if (ok)
        {
            *code = (Tier0TestCode){
                .code = code_bytes,
            };
            ok = tier0_test_build(&native_tally, functions, &arena, code, program_seed);
            interpreter_test_expect(tally, ok && native_tally.failures == 0, "both Tier 0 compilers agree");
        }
        if (ok)
        {
            const Tier0TestData data = tier0_test_layout(source);
            uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
            tier0_test_inputs(program_seed, inputs);
            Tier0TestOutcome native[BUSTER_TIER0_TEST_INPUTS];
            int status = 0;
            ok = tier0_test_run(source, code, &data, inputs, native, &status);
            interpreter_test_expect(tally, ok, "generated native program executes");
            InterpreterBinding bindings[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
            SymbolId symbols[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
            for (uint32_t g = 0; g < source->global_count; g += 1)
            {
                bindings[g] = (InterpreterBinding){
                    .symbol = g + 1,
                    .bytes = globals + data.offsets[g],
                    .size = (uint64_t)tier0_class_bytes(source->global_classes[g]) * source->global_elements[g],
                    .writable = true,
                };
            }
            for (uint32_t f = 0; f < source->function_count; f += 1)
            {
                symbols[f] = source->global_count + f + 1;
            }
            const InterpreterProgram program = {
                .functions = functions->functions,
                .function_symbols = symbols,
                .function_count = source->function_count,
                .bindings = bindings,
                .binding_count = source->global_count,
            };
            InterpreterOptions limits = interpreter_default_options();
            // The existing generator and native loader deliberately initialize every local to zero.
            limits.check_undefined = false;
            for (uint32_t r = 0; ok && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
            {
                tier0_test_initial(source, &data, globals);
                InterpreterValue arguments[3] = { 0 };
                for (uint32_t a = 0; a < 3; a += 1)
                {
                    arguments[a].low = inputs[r][a];
                }
                InterpreterResult result;
                const uint32_t parameters = functions->functions[source->function_count - 1].parameter_count;
                const bool ran = interpreter_typed_run(&program, symbols[source->function_count - 1], arguments, parameters, scratch, &limits, &result);
                const uint64_t hash = tier0_test_hash(globals, data.size);
                const bool same = ran && result.value.low == native[r].result && hash == native[r].globals;
                if (!same && tally->failures < 20)
                {
                    fprintf(stderr, "test-interpreter: seed %" PRIu64 " input %u status %u function %u entry %u, result %016" PRIx64 " expected %016" PRIx64 "\n", program_seed, r, (unsigned)result.status, result.function, result.entry, result.value.low, native[r].result);
                }
                interpreter_test_expect(tally, same, "production oracle agrees with native result and global bytes");
            }
        }
        arena_pop_to(&arena, mark);
    }
    if (ready)
    {
        tier0_tables_close(&tables);
    }
    arena_release(&arena);
}

static void interpreter_test_text_native(InterpreterTestTally* tally, Arena* scratch)
{
    Arena source = arena_reserve(1ull << 20);
    Arena arena = arena_reserve(32ull << 20);
    String8 text = { 0 };
    bool ok = ir_parse_test_read("tests/ir/tier0-add.ir", &source, &text);
    IrFunction parsed = { 0 };
    if (ok)
    {
        const IrParseResult result = ir_parse(text, &arena, scratch, &parsed);
        ok = result.error == IR_PARSE_OK;
    }
    interpreter_test_expect(tally, ok, "handwritten IR fixture parses");
    const uint8_t kinds[] = {
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint64_t values[] = {
        0,
        1,
        0,
        0,
    };
    const uint8_t locals[] = {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint8_t flags[] = {
        0,
        0,
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
        .count = 4,
        .local_classes = locals,
        .local_flags = flags,
        .local_count = 2,
        .parameter_count = 2,
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
    Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Functions, 1);
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    Tier0Program* generated = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Program, 1);
    ok = ok && functions != 0 && code != 0 && code_bytes != 0 && generated != 0;
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
        ok = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && tier0_test_build(&native_tally, functions, &arena, code, 175);
        interpreter_test_expect(tally, ok && native_tally.failures == 0, "handwritten equivalent typed function compiles in both paths");
    }
    if (ok)
    {
        const Tier0TestData data = { 0 };
        uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
        tier0_test_inputs(175, inputs);
        Tier0TestOutcome native[BUSTER_TIER0_TEST_INPUTS];
        int status = 0;
        ok = tier0_test_run(generated, code, &data, inputs, native, &status);
        interpreter_test_expect(tally, ok, "handwritten typed function executes natively");
        const IrInterpreterFunction function = {
            .symbol = 1,
            .function = &parsed,
        };
        const IrInterpreterProgram program = {
            .functions = &function,
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
            const InterpreterResult result = ir_interpret(&program, 1, arguments, 2, 0, scratch);
            interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == native[r].result, "text IR oracle agrees with both Tier 0 compilers and native output");
        }
    }
    arena_release(&arena);
    arena_release(&source);
}

static int run_interpreter_tests(int argc, char** argv)
{
    uint64_t seed = 176;
    uint32_t count = 32;
    bool valid = true;
    for (int i = 0; i < argc && valid; i += 1)
    {
        if ((strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "--count") == 0) && i + 1 < argc)
        {
            const bool is_seed = strcmp(argv[i], "--seed") == 0;
            char* end = 0;
            errno = 0;
            const uint64_t value = strtoull(argv[++i], &end, 0);
            valid = errno == 0 && end != argv[i] && *end == 0 && argv[i][0] != '-' && (is_seed || (value != 0 && value <= 100000));
            if (valid && is_seed)
            {
                seed = value;
            }
            else if (valid)
            {
                count = (uint32_t)value;
            }
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-interpreter [--seed N] [--count N]\n");
    }
    else
    {
        InterpreterTestTally tally = { 0 };
        Arena scratch = arena_reserve(16ull << 20);
        interpreter_test_arithmetic(&tally, &scratch);
        interpreter_test_conversions(&tally, &scratch);
        interpreter_test_recursion(&tally, &scratch);
        interpreter_test_memory(&tally, &scratch);
        interpreter_test_limits(&tally, &scratch);
        interpreter_test_native(&tally, &scratch);
        interpreter_test_heap(&tally, &scratch);
        interpreter_test_mappings(&tally, &scratch);
        interpreter_test_guarded(&tally, &scratch);
        interpreter_test_text_native(&tally, &scratch);
        interpreter_test_programs(&tally, &scratch, seed, count);
        arena_release(&scratch);
        printf("test-interpreter: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures != 0;
    }
    return result;
}
