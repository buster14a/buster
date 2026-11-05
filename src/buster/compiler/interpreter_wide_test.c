// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The typed front's two-word values (#176), independently of IR lowering: high-word arithmetic, local storage,
// constants, checked conversions, control flow, nested interpreted calls and the native SysV boundary.

static InterpreterValue interpreter_test_wide_bits(unsigned __int128 bits)
{
    const InterpreterValue result = {
        .low = (uint64_t)bits,
        .high = (uint64_t)(bits >> 64),
    };
    return result;
}

static InterpreterValue interpreter_test_extended_bits(long double number)
{
    InterpreterValue result = { 0 };
    memcpy(&result, &number, 10);
    return result;
}

static void interpreter_test_wide_integer(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_ADD, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN };
    uint8_t classes[] = { BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128 };
    uint8_t locals[] = { BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128 };
    uint64_t values[] = { 0, 1, 0, 2, 2, 0 };
    Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = locals,
        .local_count = BUSTER_ARRAY_LENGTH(locals),
        .parameter_count = 2,
    };
    const SymbolId symbol = 1;
    const InterpreterProgram program = {
        .functions = &function,
        .function_symbols = &symbol,
        .function_count = 1,
    };
    const unsigned __int128 left = ((unsigned __int128)UINT64_C(0xf123456789abcdef) << 64) | UINT64_C(0xfedcba9876543210);
    const unsigned __int128 right = ((unsigned __int128)UINT64_C(0x123456789abc) << 64) | UINT64_C(0x13579bdf);
    const uint8_t operations[] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_DIV, BUSTER_TYPED_MOD, BUSTER_TYPED_AND, BUSTER_TYPED_OR, BUSTER_TYPED_XOR };
    const unsigned __int128 expected[] = { left + right, left - right, left * right, left / right, left % right, left & right, left | right, left ^ right };
    InterpreterValue arguments[] = { interpreter_test_wide_bits(left), interpreter_test_wide_bits(right) };
    InterpreterResult result;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
    {
        kinds[2] = operations[i];
        const bool ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
        const InterpreterValue want = interpreter_test_wide_bits(expected[i]);
        interpreter_test_expect(tally, ran && result.value.low == want.low && result.value.high == want.high && scratch->length == 0, "wide: unsigned arithmetic and local round trip preserve both words");
    }
    kinds[2] = BUSTER_TYPED_SHL;
    const uint32_t shifts[] = { 0, 1, 63, 64, 65, 127 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(shifts); i += 1)
    {
        arguments[1] = (InterpreterValue){ .low = shifts[i] };
        bool ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
        InterpreterValue want = interpreter_test_wide_bits(left << shifts[i]);
        interpreter_test_expect(tally, ran && result.value.low == want.low && result.value.high == want.high, "wide: left shifts cross the word boundary");
        kinds[2] = BUSTER_TYPED_SHR;
        ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
        want = interpreter_test_wide_bits(left >> shifts[i]);
        interpreter_test_expect(tally, ran && result.value.low == want.low && result.value.high == want.high, "wide: logical right shifts cross the word boundary");
        kinds[2] = BUSTER_TYPED_SHL;
    }
    arguments[1].low = 128;
    bool ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_ARITHMETIC, "wide: invalid shift count is diagnosed without a host invalid shift");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(classes); i += 1)
    {
        classes[i] = BUSTER_TYPED_CLASS_I128;
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(locals); i += 1)
    {
        locals[i] = BUSTER_TYPED_CLASS_I128;
    }
    const __int128 negative = -((__int128)1 << 100) - 19;
    arguments[0] = interpreter_test_wide_bits((unsigned __int128)negative);
    arguments[1] = (InterpreterValue){ .low = 7 };
    const __int128 signed_expected[] = { negative / 7, negative % 7, negative >> 7 };
    const uint8_t signed_operations[] = { BUSTER_TYPED_DIV, BUSTER_TYPED_MOD, BUSTER_TYPED_SHR };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(signed_operations); i += 1)
    {
        kinds[2] = signed_operations[i];
        ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
        const InterpreterValue want = interpreter_test_wide_bits((unsigned __int128)signed_expected[i]);
        interpreter_test_expect(tally, ran && result.value.low == want.low && result.value.high == want.high, "wide: signed division, remainder and arithmetic shift");
    }
    kinds[2] = BUSTER_TYPED_DIV;
    arguments[0] = (InterpreterValue){ .high = UINT64_C(1) << 63 };
    arguments[1] = (InterpreterValue){ .low = UINT64_MAX, .high = UINT64_MAX };
    ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_ARITHMETIC, "wide: signed minimum divided by minus one is diagnosed");
    arguments[1] = (InterpreterValue){ 0 };
    ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_ARITHMETIC, "wide: division by zero is diagnosed");
    // Comparison produces a narrow value; a high word alone makes an integer condition true.
    kinds[2] = BUSTER_TYPED_LT;
    classes[2] = BUSTER_TYPED_CLASS_I32;
    classes[3] = BUSTER_TYPED_CLASS_I32;
    classes[4] = BUSTER_TYPED_CLASS_I32;
    classes[5] = BUSTER_TYPED_CLASS_I32;
    locals[2] = BUSTER_TYPED_CLASS_I32;
    ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == 1 && result.value.high == 0, "wide: signed comparison reads the high word and yields a narrow value");
    const uint8_t branch_kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CONST, BUSTER_TYPED_RETURN, BUSTER_TYPED_CONST, BUSTER_TYPED_RETURN };
    const uint8_t branch_classes[] = { BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32 };
    const uint64_t branch_values[] = { 0, 4, 0, 0, 1, 0 };
    function.kinds = branch_kinds;
    function.classes = branch_classes;
    function.values = branch_values;
    ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == 1, "wide: high-word-only condition takes the branch");
}

static void interpreter_test_wide_shift_count(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_SHL, BUSTER_TYPED_RETURN };
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    const uint8_t locals[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U128 };
    const uint64_t values[] = { 0, 1, 0, 0 };
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = locals,
        .local_count = 2,
        .parameter_count = 2,
    };
    const SymbolId symbol = 1;
    const InterpreterProgram program = { .functions = &function, .function_symbols = &symbol, .function_count = 1 };
    InterpreterValue arguments[] = { { .low = 1 }, { .low = 63 } };
    InterpreterResult result;
    bool ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == (UINT64_C(1) << 63), "wide: small wide shift count works with a narrow left operand");
    arguments[1] = (InterpreterValue){ .high = 1 };
    ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_ARITHMETIC, "wide: narrow shifts reject a nonzero high word in the count");
}

static void interpreter_test_wide_conversions(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kinds[] = { BUSTER_TYPED_CONST, BUSTER_TYPED_CONVERT, BUSTER_TYPED_RETURN };
    uint8_t classes[] = { BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128 };
    uint64_t values[] = { UINT64_MAX, 0, 0 };
    Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
    };
    InterpreterResult result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == UINT64_MAX && result.value.high == UINT64_MAX, "wide: signed inline constant sign-extends to two words");
    classes[0] = BUSTER_TYPED_CLASS_U128;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == UINT64_MAX && result.value.high == 0, "wide: unsigned inline constant zero-extends to two words");
    const long double floating[] = { -0x1p127L, 0x1p126L, 0x1p127L, 0x1p128L, -0.75L, __builtin_infl(), __builtin_nanl("") };
    uint64_t pool[2] = { 0 };
    function.x87_constants = pool;
    function.x87_constant_count = 1;
    classes[0] = BUSTER_TYPED_CLASS_F80;
    classes[1] = BUSTER_TYPED_CLASS_I128;
    classes[2] = BUSTER_TYPED_CLASS_I128;
    values[0] = 0;
    const InterpreterStatus statuses[] = { INTERPRETER_OK, INTERPRETER_OK, INTERPRETER_ARITHMETIC, INTERPRETER_ARITHMETIC, INTERPRETER_OK, INTERPRETER_ARITHMETIC, INTERPRETER_ARITHMETIC };
    const InterpreterValue expected[] = { { .high = UINT64_C(1) << 63 }, { .high = UINT64_C(1) << 62 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(floating); i += 1)
    {
        memset(pool, 0, sizeof(pool));
        memcpy(pool, &floating[i], 10);
        result = interpreter_test_run(&function, scratch, 0);
        interpreter_test_expect(tally, result.status == statuses[i] && (statuses[i] != INTERPRETER_OK || (result.value.low == expected[i].low && result.value.high == expected[i].high)), "wide: checked long-double conversion handles integer boundaries, fractions, infinities and NaNs");
    }
    classes[1] = BUSTER_TYPED_CLASS_U128;
    classes[2] = BUSTER_TYPED_CLASS_U128;
    memcpy(pool, &floating[2], 10);
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 0 && result.value.high == (UINT64_C(1) << 63), "wide: unsigned conversion accepts the positive high bit");
    values[0] = 1;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "wide: long-double constant pool bounds checked before reading");
    values[0] = 0;
    function.x87_constants = 0;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "wide: missing long-double constant pool refused");
}

static void interpreter_test_wide_float_rounding(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONVERT, BUSTER_TYPED_RETURN };
    uint8_t classes[] = { BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_F32 };
    const uint64_t values[] = { 0, 0, 0 };
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = classes,
        .local_count = 1,
        .parameter_count = 1,
    };
    const SymbolId symbol = 1;
    const InterpreterProgram program = { .functions = &function, .function_symbols = &symbol, .function_count = 1 };
    const uint8_t destinations[] = { BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_F64 };
    const uint32_t midpoint_bits[] = { 76, 47 };
    const uint64_t expected[] = { UINT64_C(0x71800001), UINT64_C(0x4630000000000001) };
    const uint64_t sign_bits[] = { UINT64_C(1) << 31, UINT64_C(1) << 63 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(destinations); i += 1)
    {
        classes[1] = destinations[i];
        classes[2] = destinations[i];
        // One integer beyond a midpoint above 2^100: x87 loses that final bit, but a direct cast must not.
        const unsigned __int128 magnitude = ((unsigned __int128)1 << 100) + ((unsigned __int128)1 << midpoint_bits[i]) + 1;
        for (uint32_t variant = 0; variant < 3; variant += 1)
        {
            classes[0] = variant == 0 ? BUSTER_TYPED_CLASS_U128 : BUSTER_TYPED_CLASS_I128;
            const bool negative = variant == 2;
            const InterpreterValue argument = interpreter_test_wide_bits(negative ? 0 - magnitude : magnitude);
            InterpreterResult result;
            const bool ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
            const uint64_t want = expected[i] | (negative ? sign_bits[i] : 0);
            interpreter_test_expect(tally, ran && result.value.low == want && result.value.high == 0, "wide: signed and unsigned integer casts round directly to float and double");
        }
    }
}

static __attribute__((noinline)) long double interpreter_test_wide_native(__int128 a, long double b)
{
    const long double result = (long double)a + b;
    return result;
}

static void interpreter_test_wide_calls(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t caller_kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CALL, BUSTER_TYPED_RETURN };
    const uint8_t caller_classes[] = { BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
    const uint64_t caller_values[] = { 0, 1, 0, 0 };
    const uint8_t callee_kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONVERT, BUSTER_TYPED_LOCAL, BUSTER_TYPED_ADD, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN };
    const uint8_t callee_classes[] = { BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
    const uint64_t callee_values[] = { 0, 0, 1, 0, 2, 2, 0 };
    const uint8_t locals[] = { BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
    SymbolId target = 2;
    const uint32_t first = 0;
    const uint32_t count = 2;
    const uint8_t variadic = 0;
    Tier0Function functions[] = {
        {
            .kinds = caller_kinds,
            .classes = caller_classes,
            .values = caller_values,
            .count = BUSTER_ARRAY_LENGTH(caller_kinds),
            .local_classes = locals,
            .local_count = 2,
            .parameter_count = 2,
            .call_targets = &target,
            .call_first = &first,
            .call_argument_counts = &count,
            .call_variadic = &variadic,
            .argument_classes = locals,
            .call_count = 1,
            .argument_count = 2,
        },
        {
            .kinds = callee_kinds,
            .classes = callee_classes,
            .values = callee_values,
            .count = BUSTER_ARRAY_LENGTH(callee_kinds),
            .local_classes = locals,
            .local_count = 3,
            .parameter_count = 2,
        },
    };
    const SymbolId symbols[] = { 1, 2 };
    const InterpreterNativeBinding native = { .symbol = 3, .address = (void*)interpreter_test_wide_native };
    const InterpreterProgram program = {
        .functions = functions,
        .function_symbols = symbols,
        .function_count = 2,
        .natives = &native,
        .native_count = 1,
    };
    const __int128 integer = -((__int128)1 << 70) - 2048;
    const long double extended = 0x1.123456789abcdefp+60L;
    const InterpreterValue arguments[] = { interpreter_test_wide_bits((unsigned __int128)integer), interpreter_test_extended_bits(extended) };
    const InterpreterValue expected = interpreter_test_extended_bits(interpreter_test_wide_native(integer, extended));
    InterpreterResult result;
    bool ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == expected.low && result.value.high == expected.high, "wide: nested interpreted calls transport both words and exact x87 precision");
    target = 3;
    ran = interpreter_typed_run(&program, 1, arguments, 2, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == expected.low && result.value.high == expected.high, "wide: typed native calls marshal i128 and f80 and capture st0");
}

static void interpreter_test_wide_memory(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = { BUSTER_TYPED_GLOBAL, BUSTER_TYPED_STORE_GLOBAL, BUSTER_TYPED_RETURN };
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
    const uint64_t values[] = { 2, 3, 0 };
    const Tier0Function function = { .kinds = kinds, .classes = classes, .values = values, .count = BUSTER_ARRAY_LENGTH(kinds) };
    const SymbolId symbol = 1;
    const long double number = 0x1.123456789abcdefp+20L;
    const InterpreterValue expected = interpreter_test_extended_bits(number);
    uint8_t source[12];
    uint8_t destination[12];
    memset(source, 0xa5, sizeof(source));
    memset(destination, 0x5a, sizeof(destination));
    memcpy(source + 1, &expected, 10);
    InterpreterBinding bindings[] = {
        { .symbol = 2, .bytes = source + 1, .size = 10, .writable = false },
        { .symbol = 3, .bytes = destination + 1, .size = 10, .writable = true },
    };
    const InterpreterProgram program = {
        .functions = &function,
        .function_symbols = &symbol,
        .function_count = 1,
        .bindings = bindings,
        .binding_count = BUSTER_ARRAY_LENGTH(bindings),
    };
    InterpreterResult result;
    bool ran = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == expected.low && result.value.high == expected.high &&
                            memcmp(source + 1, destination + 1, 10) == 0 && destination[0] == 0x5a && destination[11] == 0x5a,
                            "wide: unaligned F80 global access needs exactly ten bytes and preserves its neighbors");
    bindings[1].size = 9;
    memset(destination, 0x5a, sizeof(destination));
    ran = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    bool untouched = true;
    for (uint32_t i = 0; i < sizeof(destination); i += 1)
    {
        untouched &= destination[i] == 0x5a;
    }
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_MEMORY && untouched, "wide: an undersized F80 destination is rejected before any byte is written");
    bindings[0].size = 9;
    bindings[1].size = 10;
    ran = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_MEMORY && result.entry == 0, "wide: an undersized F80 source is rejected before reading");

    const uint8_t local_kinds[] = { BUSTER_TYPED_CONST, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CONST, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_LOAD, BUSTER_TYPED_RETURN };
    const uint8_t local_classes[] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_U8 };
    const uint8_t locals[] = { BUSTER_TYPED_CLASS_F80 };
    uint64_t local_values[] = { 0, 0, 0, 9, 1, 0, 0 };
    const uint64_t pool[] = { expected.low, expected.high };
    const Tier0Function local = {
        .kinds = local_kinds,
        .classes = local_classes,
        .values = local_values,
        .count = BUSTER_ARRAY_LENGTH(local_kinds),
        .local_classes = locals,
        .local_count = 1,
        .x87 = true,
        .x87_constants = pool,
        .x87_constant_count = 1,
    };
    result = interpreter_test_run(&local, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == (expected.high >> 8), "wide: a scalar F80 store defines its tenth meaningful byte");
    local_values[3] = 10;
    result = interpreter_test_run(&local, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_UNDEFINED, "wide: scalar F80 padding remains undefined");
    local_values[3] = 16;
    result = interpreter_test_run(&local, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_MEMORY, "wide: scalar F80 storage has its exact sixteen-byte extent");
}

static __attribute__((noinline)) unsigned __int128 interpreter_test_wide_native_integer(unsigned __int128 a)
{
    const unsigned __int128 result = a + ((unsigned __int128)1 << 100) + 7;
    return result;
}

static void interpreter_test_wide_integer_return(InterpreterTestTally* tally, Arena* scratch)
{
    const uint8_t kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CALL, BUSTER_TYPED_RETURN };
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128 };
    const uint64_t values[] = { 0, 0, 0 };
    const SymbolId target = 2;
    const uint32_t first = 0;
    const uint32_t count = 1;
    const uint8_t variadic = 0;
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = classes,
        .local_count = 1,
        .parameter_count = 1,
        .call_targets = &target,
        .call_first = &first,
        .call_argument_counts = &count,
        .call_variadic = &variadic,
        .argument_classes = classes,
        .call_count = 1,
        .argument_count = 1,
    };
    const SymbolId symbol = 1;
    const InterpreterNativeBinding native = { .symbol = 2, .address = (void*)interpreter_test_wide_native_integer };
    const InterpreterProgram program = {
        .functions = &function,
        .function_symbols = &symbol,
        .function_count = 1,
        .natives = &native,
        .native_count = 1,
    };
    const unsigned __int128 integer = ((unsigned __int128)UINT64_C(0x123456789abcdef0) << 64) | UINT64_MAX;
    const InterpreterValue argument = interpreter_test_wide_bits(integer);
    const InterpreterValue expected = interpreter_test_wide_bits(interpreter_test_wide_native_integer(integer));
    InterpreterResult result;
    const bool ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    interpreter_test_expect(tally, ran && result.value.low == expected.low && result.value.high == expected.high, "wide: typed native integer returns preserve both rax and rdx");
}

static void interpreter_test_wide_indirect_calls(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kinds[] = { BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CALL, BUSTER_TYPED_RETURN };
    uint8_t classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128 };
    uint64_t values[] = { 2, 0, 0, 0 };
    const uint8_t local = BUSTER_TYPED_CLASS_U128;
    const SymbolId target = 0;
    const uint32_t first = 0;
    const uint32_t count = 1;
    const uint8_t variadic = 0;
    const uint8_t callee_kinds[] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_ADD, BUSTER_TYPED_RETURN };
    const uint8_t callee_classes[] = { BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128, BUSTER_TYPED_CLASS_U128 };
    const uint64_t callee_values[] = { 0, 7, 0, 0 };
    Tier0Function functions[] = {
        {
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = BUSTER_ARRAY_LENGTH(kinds),
            .local_classes = &local,
            .local_count = 1,
            .parameter_count = 1,
            .call_targets = &target,
            .call_first = &first,
            .call_argument_counts = &count,
            .call_variadic = &variadic,
            .argument_classes = &local,
            .call_count = 1,
            .argument_count = 1,
        },
        {
            .kinds = callee_kinds,
            .classes = callee_classes,
            .values = callee_values,
            .count = BUSTER_ARRAY_LENGTH(callee_kinds),
            .local_classes = &local,
            .local_count = 1,
            .parameter_count = 1,
        },
    };
    const SymbolId symbols[] = { 1, 2 };
    const InterpreterNativeBinding native = { .symbol = 2, .address = (void*)interpreter_test_wide_native_integer };
    InterpreterProgram program = { .functions = functions, .function_symbols = symbols, .function_count = 2 };
    const unsigned __int128 integer = ((unsigned __int128)UINT64_C(0x123456789abcdef0) << 64) | UINT64_MAX;
    const InterpreterValue argument = interpreter_test_wide_bits(integer);
    InterpreterResult result;
    bool ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    InterpreterValue expected = interpreter_test_wide_bits(integer + 7);
    interpreter_test_expect(tally, ran && result.value.low == expected.low && result.value.high == expected.high, "wide: an indirect interpreted call consumes its token and preserves both words");
    program.function_count = 1;
    program.natives = &native;
    program.native_count = 1;
    ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    expected = interpreter_test_wide_bits(interpreter_test_wide_native_integer(integer));
    interpreter_test_expect(tally, ran && result.value.low == expected.low && result.value.high == expected.high, "wide: an indirect native call resolves only a registered address");
    kinds[0] = BUSTER_TYPED_CONST;
    values[0] = 0;
    ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_SYMBOL && result.entry == 2, "wide: an unregistered indirect target is refused without host execution");
    classes[0] = BUSTER_TYPED_CLASS_I64;
    values[0] = (uint64_t)(uintptr_t)native.address;
    ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_INVALID && result.entry == 2, "wide: an indirect target must have pointer class");
    functions[0].kinds = kinds + 1;
    functions[0].classes = classes + 1;
    functions[0].values = values + 1;
    functions[0].count -= 1;
    ran = interpreter_typed_run(&program, 1, &argument, 1, scratch, 0, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_INVALID && result.entry == 1, "wide: indirect calls check the target's stack slot as well as their arguments");

    const uint8_t void_kinds[] = { BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CALL, BUSTER_TYPED_RETURN };
    const uint8_t void_classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID };
    const uint64_t void_values[] = { 2, 0, 0 };
    const uint32_t no_arguments = 0;
    functions[0] = (Tier0Function){
        .kinds = void_kinds,
        .classes = void_classes,
        .values = void_values,
        .count = BUSTER_ARRAY_LENGTH(void_kinds),
        .call_targets = &target,
        .call_first = &first,
        .call_argument_counts = &no_arguments,
        .call_variadic = &variadic,
        .call_count = 1,
    };
    functions[1] = (Tier0Function){ .kinds = void_kinds + 2, .classes = void_classes + 2, .values = void_values + 2, .count = 1 };
    program.function_count = 2;
    program.natives = 0;
    program.native_count = 0;
    ran = interpreter_typed_run(&program, 1, 0, 0, scratch, 0, &result);
    interpreter_test_expect(tally, ran, "wide: a zero-argument void indirect call consumes its target exactly once");
}

static void interpreter_test_wide(InterpreterTestTally* tally, Arena* scratch)
{
    interpreter_test_wide_integer(tally, scratch);
    interpreter_test_wide_shift_count(tally, scratch);
    interpreter_test_wide_conversions(tally, scratch);
    interpreter_test_wide_float_rounding(tally, scratch);
    interpreter_test_wide_calls(tally, scratch);
    interpreter_test_wide_memory(tally, scratch);
    interpreter_test_wide_integer_return(tally, scratch);
    interpreter_test_wide_indirect_calls(tally, scratch);
}
