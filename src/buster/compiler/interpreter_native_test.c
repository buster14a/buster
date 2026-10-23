// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent C callees and the host libc exercise the interpreter's native boundary. The mixed variadic fixture
// reads its own va_list, including both register classes, overflow slots, and aligned i128/long-double arguments.

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct InterpreterNativeTestTally InterpreterNativeTestTally;
struct InterpreterNativeTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static void interpreter_native_test_expect(InterpreterNativeTestTally* tally, bool condition, const char* message)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 12)
        {
            fprintf(stderr, "interpreter-native: %s\n", message);
        }
        tally->failures += 1;
    }
}

static InterpreterValue interpreter_native_test_value(const void* bytes, uint32_t size)
{
    InterpreterValue result = {
        0
    };
    memcpy(&result, bytes, size);
    return result;
}

static bool interpreter_native_test_call(void* address, uint32_t flags, const uint16_t* types, const InterpreterValue* arguments, uint32_t count, uint32_t named, uint16_t result_type, InterpreterRuntime* runtime, InterpreterValue* result)
{
    const InterpreterNativeBinding binding = {
        .address = address,
        .flags = flags,
    };
    const InterpreterNativeRequest request = {
        .binding = &binding,
        .types = types,
        .arguments = arguments,
        .argument_count = count,
        .named_count = named,
        .result_type = result_type,
        .variadic = named < count,
        .runtime = runtime,
    };
    const bool ok = interpreter_native_invoke(&request, result);
    return ok;
}

static __attribute__((noinline)) uint64_t interpreter_native_test_integers(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h, uint64_t i)
{
    const uint64_t result = a + 3 * b + 5 * c + 7 * d + 11 * e + 13 * f + 17 * g + 19 * h + 23 * i;
    return result;
}

static __attribute__((noinline)) double interpreter_native_test_vectors(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j)
{
    const double result = a + 3 * b + 5 * c + 7 * d + 11 * e + 13 * f + 17 * g + 19 * h + 23 * i + 29 * j;
    return result;
}

static __attribute__((noinline)) float interpreter_native_test_float(float a, float b)
{
    const float result = a * 3.0f + b;
    return result;
}

static __attribute__((noinline)) __int128 interpreter_native_test_pair(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, __int128 pair, uint64_t f)
{
    const __int128 result = pair + a + 3 * b + 5 * c + 7 * d + 11 * e + 13 * f;
    return result;
}

static __attribute__((noinline)) __int128 interpreter_native_test_pair_registers(__int128 a, __int128 b)
{
    const __int128 result = a ^ b;
    return result;
}

static __attribute__((noinline)) long double interpreter_native_test_long_double(uint64_t a, long double b, double c)
{
    const long double result = a + b * 3.0L + c;
    return result;
}

static __attribute__((noinline)) long double interpreter_native_test_variadic(uint64_t count, ...)
{
    va_list args;
    va_start(args, count);
    long double result = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint64_t integer = va_arg(args, uint64_t);
        const double floating = va_arg(args, double);
        result += (i + 1) * (integer + floating);
    }
    const long double extended = va_arg(args, long double);
    const __int128 pair = va_arg(args, __int128);
    result += extended + (uint64_t)pair + (uint64_t)((unsigned __int128)pair >> 64);
    va_end(args);
    return result;
}

static __attribute__((noinline)) int32_t interpreter_native_test_signed(int8_t a, int16_t b)
{
    const int32_t result = a + b;
    return result;
}

static __attribute__((noinline)) uint64_t interpreter_native_test_many(uint64_t count, ...)
{
    va_list args;
    va_start(args, count);
    uint64_t result = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint64_t value = va_arg(args, uint64_t);
        result += value;
    }
    va_end(args);
    return result;
}

static __attribute__((noinline)) uint64_t interpreter_native_test_empty(void)
{
    return 0x123456789abcdef0ull;
}

static void interpreter_native_test_limits(InterpreterNativeTestTally* tally)
{
    uint16_t types[BUSTER_INTERPRETER_NATIVE_ARGUMENTS];
    InterpreterValue arguments[BUSTER_INTERPRETER_NATIVE_ARGUMENTS];
    const uint64_t count = BUSTER_INTERPRETER_NATIVE_ARGUMENTS - 1;
    for (uint32_t i = 0; i < BUSTER_INTERPRETER_NATIVE_ARGUMENTS; i += 1)
    {
        types[i] = IR_I64;
        arguments[i] = (InterpreterValue){
            .low = i == 0 ? count : i,
        };
    }
    InterpreterValue result = {
        0
    };
    bool ok = interpreter_native_test_call((void*)interpreter_native_test_many, 0, types, arguments, BUSTER_INTERPRETER_NATIVE_ARGUMENTS, 1, IR_I64, 0, &result);
    interpreter_native_test_expect(tally, ok && result.low == count * (count + 1) / 2, "maximum native argument count and overflow area");
    ok = interpreter_native_test_call((void*)interpreter_native_test_empty, 0, 0, 0, 0, 0, IR_I64, 0, &result);
    interpreter_native_test_expect(tally, ok && result.low == 0x123456789abcdef0ull && result.high == 0, "zero arguments with null input columns");
}

static void interpreter_native_test_registers(InterpreterNativeTestTally* tally)
{
    uint16_t types[10];
    InterpreterValue arguments[10];
    InterpreterValue result = {
        0
    };
    for (uint32_t i = 0; i < 10; i += 1)
    {
        types[i] = IR_I64;
        arguments[i] = (InterpreterValue){
            .low = i + 1,
        };
    }
    bool ok = interpreter_native_test_call((void*)interpreter_native_test_integers, 0, types, arguments, 9, 9, IR_I64, 0, &result);
    const uint64_t expected_integer = interpreter_native_test_integers(1, 2, 3, 4, 5, 6, 7, 8, 9);
    interpreter_native_test_expect(tally, ok && result.low == expected_integer && result.high == 0, "integer registers and overflow slots");
    for (uint32_t i = 0; i < 10; i += 1)
    {
        const double value = (double)i + 0.25;
        types[i] = IR_F64;
        arguments[i] = interpreter_native_test_value(&value, sizeof(value));
    }
    ok = interpreter_native_test_call((void*)interpreter_native_test_vectors, 0, types, arguments, 10, 10, IR_F64, 0, &result);
    const double expected_vector = interpreter_native_test_vectors(0.25, 1.25, 2.25, 3.25, 4.25, 5.25, 6.25, 7.25, 8.25, 9.25);
    const InterpreterValue vector_bits = interpreter_native_test_value(&expected_vector, sizeof(expected_vector));
    interpreter_native_test_expect(tally, ok && result.low == vector_bits.low && result.high == 0, "xmm registers and overflow slots");
    const float floats[2] = {
        1.25f,
        -2.5f,
    };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        types[i] = IR_F32;
        arguments[i] = interpreter_native_test_value(floats + i, sizeof(float));
    }
    ok = interpreter_native_test_call((void*)interpreter_native_test_float, 0, types, arguments, 2, 2, IR_F32, 0, &result);
    const float expected_float = interpreter_native_test_float(floats[0], floats[1]);
    const InterpreterValue float_bits = interpreter_native_test_value(&expected_float, sizeof(expected_float));
    interpreter_native_test_expect(tally, ok && result.low == float_bits.low && result.high == 0, "float arguments and result");
}

static void interpreter_native_test_wide(InterpreterNativeTestTally* tally)
{
    const __int128 pair = ((__int128)0x123456789abc << 64) | 0xfedcba987654321ull;
    const uint16_t types[7] = {
        IR_I64,
        IR_I64,
        IR_I64,
        IR_I64,
        IR_I64,
        IR_I128,
        IR_I64,
    };
    const InterpreterValue arguments[7] = {
        { .low = 1 },
        { .low = 2 },
        { .low = 3 },
        { .low = 4 },
        { .low = 5 },
        {
            .low = (uint64_t)pair,
            .high = (uint64_t)((unsigned __int128)pair >> 64),
        },
        { .low = 6 },
    };
    InterpreterValue result = {
        0
    };
    bool ok = interpreter_native_test_call((void*)interpreter_native_test_pair, 0, types, arguments, 7, 7, IR_I128, 0, &result);
    const __int128 expected = interpreter_native_test_pair(1, 2, 3, 4, 5, pair, 6);
    interpreter_native_test_expect(tally, ok && result.low == (uint64_t)expected && result.high == (uint64_t)((unsigned __int128)expected >> 64), "whole i128 stack fallback preserves last gp register");
    const uint16_t pair_types[2] = {
        IR_I128,
        IR_I128,
    };
    const InterpreterValue pair_arguments[2] = {
        {
            .low = 0x0123456789abcdefull,
            .high = 0x123456789abcdef0ull,
        },
        {
            .low = 0xfedcba9876543210ull,
            .high = 0xfedcba9876543210ull,
        },
    };
    ok = interpreter_native_test_call((void*)interpreter_native_test_pair_registers, 0, pair_types, pair_arguments, 2, 2, IR_I128, 0, &result);
    interpreter_native_test_expect(tally, ok && result.low == UINT64_MAX && result.high == (pair_arguments[0].high ^ pair_arguments[1].high), "i128 argument and return register pairs");
    const uint16_t extended_types[3] = {
        IR_I64,
        IR_F80,
        IR_F64,
    };
    const long double extended = 0x1.123456789abcdefp+4L;
    const double floating = 0.5;
    const InterpreterValue extended_arguments[3] = {
        { .low = 17 },
        interpreter_native_test_value(&extended, 10),
        interpreter_native_test_value(&floating, sizeof(floating)),
    };
    ok = interpreter_native_test_call((void*)interpreter_native_test_long_double, 0, extended_types, extended_arguments, 3, 3, IR_F80, 0, &result);
    const long double extended_expected = interpreter_native_test_long_double(17, extended, floating);
    const InterpreterValue expected_bits = interpreter_native_test_value(&extended_expected, 10);
    interpreter_native_test_expect(tally, ok && result.low == expected_bits.low && result.high == expected_bits.high, "long double stack argument and x87 return");
}

static void interpreter_native_test_varargs(InterpreterNativeTestTally* tally)
{
    uint16_t types[23];
    InterpreterValue arguments[23];
    types[0] = IR_I64;
    arguments[0] = (InterpreterValue){
        .low = 10,
    };
    long double expected = 0;
    for (uint32_t i = 0; i < 10; i += 1)
    {
        const double value = (double)i + 0.5;
        types[1 + 2 * i] = IR_I64;
        types[2 + 2 * i] = IR_F64;
        arguments[1 + 2 * i] = (InterpreterValue){
            .low = i + 3,
        };
        arguments[2 + 2 * i] = interpreter_native_test_value(&value, sizeof(value));
        expected += (i + 1) * (i + 3 + value);
    }
    const long double extended = 0x1.123456789abcdefp+2L;
    types[21] = IR_F80;
    arguments[21] = interpreter_native_test_value(&extended, 10);
    types[22] = IR_I128;
    arguments[22] = (InterpreterValue){
        .low = 19,
        .high = 23,
    };
    expected += extended + 19 + 23;
    InterpreterValue result = {
        0
    };
    const bool ok = interpreter_native_test_call((void*)interpreter_native_test_variadic, 0, types, arguments, 23, 1, IR_F80, 0, &result);
    const InterpreterValue expected_bits = interpreter_native_test_value(&expected, 10);
    interpreter_native_test_expect(tally, ok && result.low == expected_bits.low && result.high == expected_bits.high, "mixed variadic gp/xmm/overflow va_list");
    char output[128];
    const char* format = "%s:%d:%.2f:%lld:%.2Lf";
    const char* word = "native";
    const double floating = 1.25;
    const long double long_floating = 2.5L;
    const uint16_t print_types[8] = {
        IR_PTR,
        IR_I64,
        IR_PTR,
        IR_PTR,
        IR_I32,
        IR_F64,
        IR_I64,
        IR_F80,
    };
    const InterpreterValue print_arguments[8] = {
        { .low = (uint64_t)(uintptr_t)output },
        { .low = sizeof(output) },
        { .low = (uint64_t)(uintptr_t)format },
        { .low = (uint64_t)(uintptr_t)word },
        { .low = 42 },
        interpreter_native_test_value(&floating, sizeof(floating)),
        { .low = 1234567890123ull },
        interpreter_native_test_value(&long_floating, 10),
    };
    const bool printed = interpreter_native_test_call((void*)snprintf, 0, print_types, print_arguments, 8, 3, IR_I32, 0, &result);
    const char* expected_output = "native:42:1.25:1234567890123:2.50";
    interpreter_native_test_expect(tally, printed && result.low == strlen(expected_output) && strcmp(output, expected_output) == 0, "host libc snprintf mixed variadic arguments");
    const uint16_t printf_types[2] = {
        IR_PTR,
        IR_PTR,
    };
    const InterpreterValue printf_arguments[2] = {
        { .low = (uint64_t)(uintptr_t)"%s" },
        { .low = (uint64_t)(uintptr_t)"" },
    };
    const bool printf_ok = interpreter_native_test_call((void*)printf, 0, printf_types, printf_arguments, 2, 1, IR_I32, 0, &result);
    interpreter_native_test_expect(tally, printf_ok && result.low == 0, "host libc printf");
}

static void interpreter_native_test_allocations(InterpreterNativeTestTally* tally)
{
    InterpreterMemory memories[4];
    InterpreterRuntime runtime = {
        .memories = memories,
        .memory_capacity = 4,
        .check_memory = true,
        .check_undefined = true,
    };
    const uint16_t allocate_types[1] = {
        IR_I64,
    };
    const InterpreterValue allocate_arguments[1] = {
        { .low = 32 },
    };
    InterpreterValue allocation = {
        0
    };
    bool ok = interpreter_native_test_call((void*)malloc, BUSTER_INTERPRETER_NATIVE_ALLOCATE, allocate_types, allocate_arguments, 1, 1, IR_PTR, &runtime, &allocation);
    interpreter_native_test_expect(tally, ok && allocation.low != 0 && runtime.memory_count == 1 && runtime.memories[0].size == 32, "malloc registers its allocation");
    interpreter_native_test_expect(tally, ok && allocation.low != 0 && interpreter_memory_access(&runtime, allocation.low, 1, false) == INTERPRETER_UNDEFINED, "fresh malloc bytes are undefined");
    if (ok && allocation.low != 0)
    {
        uint8_t source[32];
        for (uint32_t i = 0; i < sizeof(source); i += 1)
        {
            source[i] = (uint8_t)(i * 7 + 3);
        }
        ok = interpreter_memory_add(&runtime, source, sizeof(source), 0, false, 0);
        const uint16_t copy_types[3] = {
            IR_PTR,
            IR_PTR,
            IR_I64,
        };
        const InterpreterValue copy_arguments[3] = {
            allocation,
            { .low = (uint64_t)(uintptr_t)source },
            { .low = sizeof(source) },
        };
        InterpreterValue result = {
            0
        };
        ok = ok && interpreter_native_test_call((void*)memcpy, BUSTER_INTERPRETER_NATIVE_COPY, copy_types, copy_arguments, 3, 3, IR_PTR, &runtime, &result);
        interpreter_native_test_expect(tally, ok && result.low == allocation.low && memcmp((void*)(uintptr_t)allocation.low, source, sizeof(source)) == 0, "host memcpy and registered memory");
        InterpreterValue invalid_copy[3];
        memcpy(invalid_copy, copy_arguments, sizeof(invalid_copy));
        invalid_copy[2].low += 1;
        const bool invalid = interpreter_native_test_call((void*)memcpy, BUSTER_INTERPRETER_NATIVE_COPY, copy_types, invalid_copy, 3, 3, IR_PTR, &runtime, &result);
        interpreter_native_test_expect(tally, !invalid, "native memcpy bounds violation rejected");
        const uint16_t reallocate_types[2] = {
            IR_PTR,
            IR_I64,
        };
        const InterpreterValue reallocate_arguments[2] = {
            allocation,
            { .low = 64 },
        };
        ok = interpreter_native_test_call((void*)realloc, BUSTER_INTERPRETER_NATIVE_REALLOCATE, reallocate_types, reallocate_arguments, 2, 2, IR_PTR, &runtime, &result);
        interpreter_native_test_expect(tally, ok && result.low != 0 && runtime.memory_count == 2 && memcmp((void*)(uintptr_t)result.low, source, sizeof(source)) == 0, "realloc replaces its registration and preserves bytes");
        if (ok && result.low != 0)
        {
            allocation = result;
            interpreter_native_test_expect(tally, interpreter_memory_access(&runtime, allocation.low, 32, false) == INTERPRETER_OK && interpreter_memory_access(&runtime, allocation.low + 32, 1, false) == INTERPRETER_UNDEFINED, "realloc preserves defined prefix and undefined new tail");
        }
        const uint16_t free_types[1] = {
            IR_PTR,
        };
        ok = interpreter_native_test_call((void*)free, BUSTER_INTERPRETER_NATIVE_FREE, free_types, &allocation, 1, 1, IR_VOID, &runtime, &result);
        interpreter_native_test_expect(tally, ok && runtime.memory_count == 1 && interpreter_memory_access(&runtime, allocation.low, 1, false) == INTERPRETER_MEMORY, "free removes its registration");
        interpreter_memory_remove(&runtime, source);
    }
    const uint16_t calloc_types[2] = {
        IR_I64,
        IR_I64,
    };
    const InterpreterValue calloc_arguments[2] = {
        { .low = 7 },
        { .low = 3 },
    };
    ok = interpreter_native_test_call((void*)calloc, BUSTER_INTERPRETER_NATIVE_CALLOC, calloc_types, calloc_arguments, 2, 2, IR_PTR, &runtime, &allocation);
    bool zero = ok && allocation.low != 0;
    if (zero)
    {
        const uint8_t* bytes = (const uint8_t*)(uintptr_t)allocation.low;
        for (uint32_t i = 0; i < 21; i += 1)
        {
            zero = zero && bytes[i] == 0;
        }
    }
    interpreter_native_test_expect(tally, zero && runtime.memory_count == 1 && runtime.memories[0].size == 21 && interpreter_memory_access(&runtime, allocation.low, 21, false) == INTERPRETER_OK, "calloc product and defined zeroed bytes");
    if (allocation.low != 0)
    {
        interpreter_native_release(&runtime);
        interpreter_native_release(&runtime);
        interpreter_native_test_expect(tally, runtime.memories[0].defined == 0 && *(uint8_t*)(uintptr_t)allocation.low == 0, "teardown releases heap metadata and preserves program allocation");
        const uint16_t free_types[1] = {
            IR_PTR,
        };
        InterpreterValue result = {
            0
        };
        ok = interpreter_native_test_call((void*)free, BUSTER_INTERPRETER_NATIVE_FREE, free_types, &allocation, 1, 1, IR_VOID, &runtime, &result);
        interpreter_native_test_expect(tally, ok && runtime.memory_count == 0, "calloc free releases its registration and byte map");
    }
    uint8_t global[4] = {
        0
    };
    uint8_t global_defined[4] = {
        0
    };
    ok = interpreter_memory_add(&runtime, global, sizeof(global), global_defined, true, 0);
    const uint16_t free_types[1] = {
        IR_PTR,
    };
    const InterpreterValue free_arguments[1] = {
        { .low = (uint64_t)(uintptr_t)global },
    };
    InterpreterValue global_result = {
        0
    };
    const bool freed_global = interpreter_native_test_call((void*)free, BUSTER_INTERPRETER_NATIVE_FREE, free_types, free_arguments, 1, 1, IR_VOID, &runtime, &global_result);
    interpreter_native_test_expect(tally, ok && !freed_global && runtime.memory_count == 1, "native free rejects registered nonheap objects");
    const uint16_t set_types[3] = {
        IR_PTR,
        IR_I32,
        IR_I64,
    };
    const InterpreterValue set_arguments[3] = {
        { .low = (uint64_t)(uintptr_t)global },
        { .low = 0xab },
        { .low = 3 },
    };
    ok = interpreter_native_test_call((void*)memset, BUSTER_INTERPRETER_NATIVE_SET, set_types, set_arguments, 3, 3, IR_PTR, &runtime, &global_result);
    interpreter_native_test_expect(tally, ok && global[0] == 0xab && global[2] == 0xab && global[3] == 0 && global_defined[0] == 1 && global_defined[2] == 1 && global_defined[3] == 0, "native memset marks precisely its written bytes defined");
    interpreter_memory_remove(&runtime, global);
    InterpreterRuntime full = {
        .check_memory = true,
    };
    InterpreterValue untouched = {
        .low = 123,
        .high = 456,
    };
    ok = interpreter_native_test_call((void*)malloc, BUSTER_INTERPRETER_NATIVE_ALLOCATE, allocate_types, allocate_arguments, 1, 1, IR_PTR, &full, &untouched);
    interpreter_native_test_expect(tally, !ok && untouched.low == 123 && untouched.high == 456, "full registry rejects allocation before invoking host");
}

static void interpreter_native_test_rejections(InterpreterNativeTestTally* tally)
{
    const InterpreterNativeBinding binding = {
        .address = (void*)interpreter_native_test_signed,
    };
    uint16_t types[2] = {
        IR_I8,
        IR_I16,
    };
    const uint8_t extensions[2] = {
        1,
        1,
    };
    const InterpreterValue arguments[2] = {
        { .low = (uint8_t)-3 },
        { .low = (uint16_t)-700 },
    };
    InterpreterNativeRequest request = {
        .binding = &binding,
        .types = types,
        .extensions = extensions,
        .arguments = arguments,
        .argument_count = 2,
        .named_count = 2,
        .result_type = IR_I32,
    };
    InterpreterValue result = {
        0
    };
    bool ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, ok && result.low == (uint32_t)-703, "signed small native argument extensions");
    request.variadic = true;
    request.named_count = 1;
    ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, !ok, "unpromoted variadic integer rejected");
    types[1] = IR_F32;
    ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, !ok, "unpromoted variadic float rejected");
    request.argument_count = BUSTER_INTERPRETER_NATIVE_ARGUMENTS + 1;
    ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, !ok, "oversized argument column rejected before reading");
    request.argument_count = 2;
    request.named_count = 3;
    ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, !ok, "named count past argument count rejected");
    request.named_count = 2;
    types[0] = IR_VOID;
    ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, !ok, "void argument rejected");
    types[0] = IR_I8;
    types[1] = IR_I16;
    request.variadic = false;
    request.extensions = 0;
    ok = interpreter_native_invoke(&request, &result);
    interpreter_native_test_expect(tally, !ok, "narrow native arguments require explicit extension metadata");
    ok = interpreter_native_invoke(0, &result);
    interpreter_native_test_expect(tally, !ok, "null native request rejected");
}

static __attribute__((unused)) int run_interpreter_native_tests(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    InterpreterNativeTestTally tally = {
        0
    };
    interpreter_native_test_registers(&tally);
    interpreter_native_test_limits(&tally);
    interpreter_native_test_wide(&tally);
    interpreter_native_test_varargs(&tally);
    interpreter_native_test_allocations(&tally);
    interpreter_native_test_rejections(&tally);
    printf("interpreter-native: %" PRIu64 " checks, %" PRIu64 " failures\n", tally.checks, tally.failures);
    return tally.failures != 0;
}
