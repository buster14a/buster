// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 atomic semantic fixtures. Include after c_check_builtin_atomic.c and c_checker_test.c.
// Direct expected types/effects are independent of the action implementation. Separate C sources use the existing
// Clang subprocess oracle for constraints, result types and invalid-order warning classification. This is no atomic
// runtime/concurrency acceptance. Every build/test invocation uses the approved 9700X and its normal exclusive lock.
// Primary behavior references: llvmorg-23.1.1 clang Basic/Builtins.td, Sema/SemaChecking.cpp (BuildAtomicExpr),
// Sema/SemaExpr.cpp (pointer-sign conversion) and CodeGen/CGBuiltin.cpp (fence evaluation):
// https://github.com/llvm/llvm-project/tree/llvmorg-23.1.1/clang

typedef struct CheckBuiltinAtomicTest CheckBuiltinAtomicTest;
struct CheckBuiltinAtomicTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_builtin_atomic_test_expect(CheckBuiltinAtomicTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-atomic: %s\n", description);
        }
    }
}

static CheckValue check_builtin_atomic_test_pointer(TypeId type, uint32_t object, int64_t offset)
{
    const CheckValue value = {
        .type = type,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = object,
        .offset = offset,
    };
    return value;
}

static CheckBuiltinAtomicResult check_builtin_atomic_test_action(TypeTable* table, CBuiltinId id, const CheckValue* arguments, uint32_t count)
{
    const CheckBuiltinAtomicAction action = {
        .id = id,
        .arguments = arguments,
        .argument_count = count,
        .node = 148,
        .source_position = UINT32_MAX,
        .minimum_alignment = 16,
        .alignment_known = true,
    };
    CheckBuiltinAtomicResult result;
    check_builtin_atomic_one(table, &action, &result);
    return result;
}

static bool check_builtin_atomic_test_equal(const CheckBuiltinAtomicResult* a, const CheckBuiltinAtomicResult* b)
{
    bool same = check_test_action_equal(&a->action, &b->action) && check_test_value_equal(&a->object_address, &b->object_address) &&
                check_test_value_equal(&a->store_value, &b->store_value) && a->object_type == b->object_type && a->value_type == b->value_type &&
                a->object_size == b->object_size && a->required_alignment == b->required_alignment && a->minimum_alignment == b->minimum_alignment &&
                a->evaluated_operands == b->evaluated_operands && a->success_order == b->success_order && a->failure_order == b->failure_order &&
                a->known_orders == b->known_orders && a->warnings == b->warnings && a->warning_operands == b->warning_operands &&
                a->unsupported == b->unsupported && a->runtime_builtin == b->runtime_builtin && a->fence_scope == b->fence_scope &&
                a->alignment_known == b->alignment_known && a->volatile_access == b->volatile_access && a->reads_memory == b->reads_memory &&
                a->writes_memory == b->writes_memory && a->rmw == b->rmw && a->compiler_barrier == b->compiler_barrier;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(a->argument_conversions); i += 1)
    {
        same &= a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    return same;
}

static void check_builtin_atomic_test_values(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    const TypeId types[] = { BUSTER_TYPE_BOOL, BUSTER_TYPE_CHAR, BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT,
                             BUSTER_TYPE_USHORT, BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG,
                             BUSTER_TYPE_LLONG, BUSTER_TYPE_ULLONG, type_pointer(table, BUSTER_TYPE_INT) };
    const uint64_t sizes[] = { 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 8, 8, 8 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(types); i += 1)
    {
        const TypeId object = type_qualify(table, types[i], BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
        const TypeId pointer = type_pointer(table, object);
        CheckValue arguments[3] = {
            check_builtin_atomic_test_pointer(pointer, i + 1, 0),
            check_test_integer(BUSTER_TYPE_INT, 0),
            check_test_integer(BUSTER_TYPE_INT, 5),
        };
        CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == 0 &&
                                        result.action.value.type == types[i] && result.value_type == types[i] && result.object_type == object &&
                                        result.object_size == sizes[i] && result.required_alignment == sizes[i] && result.minimum_alignment == 16 &&
                                        result.alignment_known && result.volatile_access && result.reads_memory && !result.writes_memory && !result.rmw &&
                                        result.compiler_barrier && result.evaluated_operands == 3 && result.runtime_builtin == CBUILTIN_ATOMIC_LOAD_N &&
                                        result.argument_conversions[0].type == pointer && result.argument_conversions[1].type == BUSTER_TYPE_INT &&
                                        result.action.value.constant_kind == CHECK_CONSTANT_NONE && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                        (result.action.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED)) == 0,
                                        "qualified scalar load retains exact storage/width, removes result qualifiers and never invents a constant");
        arguments[0].type = type_pointer(table, type_qualify(table, types[i], BUSTER_TYPE_VOLATILE));
        result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == 0 &&
                                        result.action.value.type == BUSTER_TYPE_VOID && !result.reads_memory && result.writes_memory && result.volatile_access &&
                                        result.evaluated_operands == 7 && result.store_value.type == types[i] &&
                                        result.argument_conversions[1].type == types[i] && result.success_order == 5 && result.known_orders == 1,
                                        "store converts its value to unqualified pointee type and preserves the volatile write");
    }

    const TypeId floating_types[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE };
    const uint64_t floating_sizes[] = { 4, 8 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(floating_types); i += 1)
    {
        CheckValue values[3] = {
            check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, floating_types[i], BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE)), 46, 0),
            check_test_integer(BUSTER_TYPE_INT, 0),
            check_test_integer(BUSTER_TYPE_INT, 0),
        };
        CheckBuiltinAtomicResult floating_result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, values, 2);
        check_builtin_atomic_test_expect(test, floating_result.action.diagnostic == CHECK_DIAG_NONE && floating_result.unsupported == 0 &&
                                        floating_result.action.value.type == floating_types[i] && floating_result.value_type == floating_types[i] &&
                                        floating_result.object_size == floating_sizes[i] && floating_result.volatile_access && floating_result.reads_memory &&
                                        floating_result.action.value.constant_kind == CHECK_CONSTANT_NONE,
                                        "Clang 23.1.1 real floating loads retain their exact F32/F64 source type and raw storage shape");
        values[0].type = type_pointer(table, type_qualify(table, floating_types[i], BUSTER_TYPE_VOLATILE));
        values[1] = check_test_float(BUSTER_TYPE_DOUBLE, 1.25L);
        floating_result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, values, 3);
        check_builtin_atomic_test_expect(test, floating_result.action.diagnostic == CHECK_DIAG_NONE && floating_result.unsupported == 0 &&
                                        floating_result.writes_memory && floating_result.store_value.type == floating_types[i] &&
                                        floating_result.store_value.constant_kind == CHECK_CONSTANT_FLOATING && floating_result.store_value.floating == 1.25L &&
                                        floating_result.argument_conversions[1].type == floating_types[i],
                                        "real floating store converts its value numerically before the producer transfers representation bits to the helper");
    }
    const TypeId other_floating_types[] = { BUSTER_TYPE_BF16, BUSTER_TYPE_FLOAT16, BUSTER_TYPE_HALF, BUSTER_TYPE_FLOAT128,
                                           type_complex(table, BUSTER_TYPE_FLOAT), type_complex(table, BUSTER_TYPE_DOUBLE), type_complex(table, BUSTER_TYPE_LONG_DOUBLE) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(other_floating_types); i += 1)
    {
        CheckValue values[3] = {
            check_builtin_atomic_test_pointer(type_pointer(table, other_floating_types[i]), 47, 0),
            check_test_integer(BUSTER_TYPE_INT, 0),
            check_test_integer(BUSTER_TYPE_INT, 0),
        };
        CheckBuiltinAtomicResult floating_result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, values, 2);
        check_builtin_atomic_test_expect(test, floating_result.action.diagnostic == CHECK_DIAG_NONE && floating_result.value_type == other_floating_types[i] &&
                                        (floating_result.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT) != 0 &&
                                        floating_result.action.value.constant_kind == CHECK_CONSTANT_NONE,
                                        "valid extended/complex floating load is typed and explicitly unsupported instead of receiving a false source error");
        floating_result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, values, 3);
        check_builtin_atomic_test_expect(test, floating_result.action.diagnostic == CHECK_DIAG_NONE && floating_result.store_value.type == other_floating_types[i] &&
                                        floating_result.argument_conversions[1].type == other_floating_types[i] && floating_result.store_value.constant_kind == CHECK_CONSTANT_NONE &&
                                        (floating_result.unsupported & (BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT | BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION)) ==
                                            (BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT | BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION),
                                        "valid unsupported floating store publishes exact conversion intent without fabricating a payload");
    }

    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148040);
    const bool completed = type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
    check_builtin_atomic_test_expect(test, completed, "atomic enum fixture construction");
    CheckValue arguments[3] = {
        check_builtin_atomic_test_pointer(type_pointer(table, enumeration), 31, 0),
        check_test_integer(BUSTER_TYPE_ULLONG, UINT64_C(0x100000001)),
        check_test_integer(BUSTER_TYPE_INT, 0),
    };
    CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_value.type == enumeration &&
                                    result.store_value.integer == 1 && result.object_size == 4, "GNU atomic enum objects retain their type and value conversion");
    arguments[0].type = type_pointer(table, BUSTER_TYPE_BOOL);
    arguments[1] = check_test_float(BUSTER_TYPE_DOUBLE, -0.25L);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_value.integer == 1 &&
                                    result.store_value.type == BUSTER_TYPE_BOOL, "bool store performs truth conversion rather than low-bit truncation");
    arguments[0].type = type_pointer(table, BUSTER_TYPE_UCHAR);
    arguments[1] = check_test_float(BUSTER_TYPE_DOUBLE, 257.75L);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.value.fault == CHECK_FAULT_NONE && result.store_value.integer == 255 &&
                                    result.action.diagnostic == CHECK_DIAG_NONE,
                                    "out-of-range floating store conversion saturates, as clang folds it, without executing memory");
    arguments[1] = check_test_integer(BUSTER_TYPE_ULLONG, 257);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_value.integer == 1,
                                    "integer store conversion narrows to exact byte width");

    const TypeId pointer_value = type_pointer(table, BUSTER_TYPE_INT);
    arguments[0].type = type_pointer(table, pointer_value);
    arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST)), 32, 0);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_value.type == pointer_value &&
                                    (result.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS) != 0 && result.warning_operands == 2,
                                    "pointer-value store retains ordinary parameter qualifier-loss warnings");
    const TypeId signed_pointees[] = { BUSTER_TYPE_CHAR, BUSTER_TYPE_SCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_INT,
                                       BUSTER_TYPE_LONG, BUSTER_TYPE_LLONG, BUSTER_TYPE_INT128 };
    const TypeId unsigned_pointees[] = { BUSTER_TYPE_UCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_UINT,
                                         BUSTER_TYPE_ULONG, BUSTER_TYPE_ULLONG, BUSTER_TYPE_UINT128 };
    for (uint32_t pair = 0; pair < BUSTER_ARRAY_LENGTH(signed_pointees); pair += 1)
    {
        for (uint32_t reverse = 0; reverse < 2; reverse += 1)
        {
            const TypeId to = type_pointer(table, reverse != 0 ? unsigned_pointees[pair] : signed_pointees[pair]);
            const TypeId from = type_pointer(table, reverse != 0 ? signed_pointees[pair] : unsigned_pointees[pair]);
            arguments[0].type = type_pointer(table, to);
            arguments[1] = check_builtin_atomic_test_pointer(from, 43, 9);
            result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
            check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == 0 &&
                                            result.warnings == BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN && result.warning_operands == 2 &&
                                            result.store_value.type == to && result.store_value.object == 43 && result.store_value.offset == 9 &&
                                            result.argument_conversions[1].type == to && result.object_size == 8,
                                            "pointer-valued store accepts corresponding integer signedness with a warning and exact pointer conversion");
        }
    }
    arguments[0].type = type_pointer(table, pointer_value);
    arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, BUSTER_TYPE_UINT, BUSTER_TYPE_CONST)), 44, 0);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
                                    result.warnings == BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN && result.warning_operands == 2,
                                    "corresponding signedness selects pointer-sign after ordinary C qualifier loss remains compatible");
    const TypeId restricted = type_qualify(table, pointer_value, BUSTER_TYPE_RESTRICT);
    arguments[0].type = type_pointer(table, type_qualify(table, restricted, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.value_type == restricted &&
                                    result.action.value.type == restricted && result.volatile_access,
                                    "pointer-object restrict survives deduced value typing while local const/volatile are removed");
    arguments[0].type = type_array(table, BUSTER_TYPE_INT, 2);
    arguments[0].category = CHECK_LVALUE;
    arguments[0].flags = BUSTER_CHECK_STATIC_OBJECT;
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_INT &&
                                    (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0,
                                    "array first argument decays before atomic pointer inference");

    arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 33, 0);
    arguments[0].flags = BUSTER_CHECK_SIDE_EFFECT;
    arguments[1] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_LVALUE, .fault = CHECK_FAULT_DIVIDE_BY_ZERO, .fault_node = 77 };
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 3 && result.known_orders == 0 &&
                                    result.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.action.value.fault_node == 77 &&
                                    (result.argument_conversions[1].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0,
                                    "runtime pointer/order operands and deferred faults are retained exactly once");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    arguments[2] = (CheckValue){ .type = type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_VOLATILE), .category = CHECK_LVALUE };
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 7 && result.known_orders == 0 &&
                                    (result.argument_conversions[2].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0,
                                    "volatile dynamic order is evaluated even when a stronger helper ignores its numeric value");
}

static void check_builtin_atomic_test_enum_pointers(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    const TypeId signed_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148042);
    const TypeId other_signed_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148043);
    const TypeId unsigned_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148044);
    const TypeId incomplete_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148045);
    const bool ready = type_enum_complete(table, signed_enum, BUSTER_TYPE_INT, 0) &&
                       type_enum_complete(table, other_signed_enum, BUSTER_TYPE_INT, 0) &&
                       type_enum_complete(table, unsigned_enum, BUSTER_TYPE_UINT, 0);
    check_builtin_atomic_test_expect(test, ready, "signed/unsigned enum pointer fixtures constructed");
    if (ready)
    {
        uint32_t completions[512] = { 0 };
        completions[table->types.index[type_index(signed_enum)]] = 20;
        completions[table->types.index[type_index(other_signed_enum)]] = 30;
        completions[table->types.index[type_index(unsigned_enum)]] = 40;
        const TypeId enum_parameter[] = { signed_enum };
        const TypeId int_parameter[] = { BUSTER_TYPE_INT };
        const TypeId enum_function = type_function(table, BUSTER_TYPE_INT, enum_parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId int_function = type_function(table, BUSTER_TYPE_INT, int_parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId old_function = type_function(table, BUSTER_TYPE_INT, NULL, 0, 0);
        const struct
        {
            TypeId to;
            TypeId from;
            uint32_t source_position;
            CheckDiagnostic diagnostic;
            uint32_t warnings;
        } cases[] =
        {
            { signed_enum, BUSTER_TYPE_INT, 20, CHECK_DIAG_NONE, 0 },
            { BUSTER_TYPE_INT, signed_enum, 20, CHECK_DIAG_NONE, 0 },
            { signed_enum, BUSTER_TYPE_UINT, 20, CHECK_DIAG_NONE, BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN },
            { BUSTER_TYPE_UINT, signed_enum, 20, CHECK_DIAG_NONE, BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN },
            { unsigned_enum, BUSTER_TYPE_UINT, 40, CHECK_DIAG_NONE, 0 },
            { BUSTER_TYPE_UINT, unsigned_enum, 40, CHECK_DIAG_NONE, 0 },
            { unsigned_enum, BUSTER_TYPE_INT, 40, CHECK_DIAG_INCOMPATIBLE, 0 },
            { BUSTER_TYPE_INT, unsigned_enum, 40, CHECK_DIAG_INCOMPATIBLE, 0 },
            { signed_enum, other_signed_enum, 30, CHECK_DIAG_NONE, BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN },
            { other_signed_enum, signed_enum, 30, CHECK_DIAG_NONE, BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN },
            { signed_enum, BUSTER_TYPE_INT, 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { BUSTER_TYPE_INT, signed_enum, 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { signed_enum, BUSTER_TYPE_UINT, 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { BUSTER_TYPE_UINT, signed_enum, 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { unsigned_enum, BUSTER_TYPE_UINT, 39, CHECK_DIAG_INCOMPATIBLE, 0 },
            { BUSTER_TYPE_UINT, unsigned_enum, 39, CHECK_DIAG_INCOMPATIBLE, 0 },
            { signed_enum, other_signed_enum, 29, CHECK_DIAG_INCOMPATIBLE, 0 },
            { signed_enum, signed_enum, 19, CHECK_DIAG_NONE, 0 },
            { signed_enum, BUSTER_TYPE_VOID, 19, CHECK_DIAG_NONE, 0 },
            { BUSTER_TYPE_VOID, signed_enum, 19, CHECK_DIAG_NONE, 0 },
            { incomplete_enum, incomplete_enum, 0, CHECK_DIAG_NONE, 0 },
            { incomplete_enum, BUSTER_TYPE_VOID, 0, CHECK_DIAG_NONE, 0 },
            { BUSTER_TYPE_VOID, incomplete_enum, 0, CHECK_DIAG_NONE, 0 },
            { incomplete_enum, BUSTER_TYPE_INT, UINT32_MAX, CHECK_DIAG_INCOMPATIBLE, 0 },
            { type_pointer(table, signed_enum), type_pointer(table, BUSTER_TYPE_INT), 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { type_pointer(table, signed_enum), type_pointer(table, BUSTER_TYPE_INT), 20, CHECK_DIAG_NONE, 0 },
            { enum_function, int_function, 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { enum_function, int_function, 20, CHECK_DIAG_NONE, 0 },
            { enum_function, old_function, 19, CHECK_DIAG_INCOMPATIBLE, 0 },
            { enum_function, old_function, 20, CHECK_DIAG_NONE, 0 },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            const TypeId stored_type = type_pointer(table, cases[i].to);
            const CheckValue arguments[] = {
                check_builtin_atomic_test_pointer(type_pointer(table, stored_type), i + 60, 0),
                check_builtin_atomic_test_pointer(type_pointer(table, cases[i].from), i + 100, 0),
                check_test_integer(BUSTER_TYPE_INT, 0),
            };
            const CheckBuiltinAtomicAction action = {
                .id = CBUILTIN_ATOMIC_STORE_N,
                .arguments = arguments,
                .argument_count = BUSTER_ARRAY_LENGTH(arguments),
                .node = 9999,
                .tag_completions = completions,
                .tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
                .source_position = cases[i].source_position,
                .minimum_alignment = 8,
                .alignment_known = true,
            };
            CheckBuiltinAtomicResult result;
            check_builtin_atomic_one(table, &action, &result);
            check_builtin_atomic_test_expect(test, result.action.diagnostic == cases[i].diagnostic && result.warnings == cases[i].warnings &&
                                            result.warning_operands == (cases[i].warnings != 0 ? 2u : 0u) && result.unsupported == 0,
                                            "enum pointer conversion follows ordinary compatibility, signed-only mapping and source-time visibility");
            if (cases[i].diagnostic == CHECK_DIAG_NONE)
            {
                check_builtin_atomic_test_expect(test, result.store_value.type == stored_type && result.argument_conversions[1].type == stored_type,
                                                "accepted enum pointer conversion retains the exact destination pointer type");
            }
        }
    }
}

static void check_builtin_atomic_test_orders(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    CheckValue arguments[3] = {
        check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 1, 0),
        check_test_integer(BUSTER_TYPE_INT, 0),
        check_test_integer(BUSTER_TYPE_INT, 0),
    };
    for (uint32_t raw = 0; raw <= 7; raw += 1)
    {
        const uint32_t order = raw == 7 ? UINT32_MAX : raw;
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, order);
        CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
        const bool load_warning = order > 5 || order == 3 || order == 4;
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.success_order == order && result.known_orders == 1 &&
                                        ((result.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0) == load_warning &&
                                        result.warning_operands == (load_warning ? 2u : 0u), "known load order validity is a warning, never an arity/type rejection");
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, 11);
        arguments[2] = check_test_integer(BUSTER_TYPE_INT, order);
        result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
        const bool store_warning = order > 5 || order == 1 || order == 2 || order == 4;
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.success_order == order && result.known_orders == 1 &&
                                        ((result.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0) == store_warning &&
                                        result.warning_operands == (store_warning ? 4u : 0u), "known store order rules preserve source warning classification");
        const CBuiltinId fences[] = { CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ATOMIC_SIGNAL_FENCE };
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fences); f += 1)
        {
            const CheckValue value = check_test_integer(BUSTER_TYPE_INT, order);
            result = check_builtin_atomic_test_action(table, fences[f], &value, 1);
            check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
                                            result.success_order == order && result.known_orders == 1 && result.warnings == 0 &&
                                            result.evaluated_operands == 1 && result.compiler_barrier &&
                                            result.fence_scope == (f == 0 ? CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM : CHECK_BUILTIN_ATOMIC_FENCE_SIGNAL),
                                            "fence ordinary int parameter has exact scope and no invented invalid-order Sema warning");
        }
    }
    arguments[1] = check_test_integer(BUSTER_TYPE_ULLONG, UINT64_C(0x100000003));
    CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.success_order == 3 && result.argument_conversions[1].type == BUSTER_TYPE_INT &&
                                    (result.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0, "order converts to target int before warning validation");
    arguments[1] = check_test_float(BUSTER_TYPE_DOUBLE, 3.75L);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.success_order == 3 && result.known_orders == 1 &&
                                    result.warnings == 0, "implicit floating order conversion is accepted without falsely classifying it as an ICE");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 3);
    arguments[1].flags = 0;
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.success_order == 3 && result.known_orders == 1 && result.warnings == 0,
                                    "folded non-ICE order knowledge does not invent Clang's ICE-only warning");
    result = check_builtin_atomic_test_action(table, CBUILTIN_SYNC_SYNCHRONIZE, NULL, 0);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
                                    result.evaluated_operands == 0 && result.success_order == 5 && result.known_orders == 1 && result.compiler_barrier &&
                                    result.fence_scope == CHECK_BUILTIN_ATOMIC_FENCE_SYSTEM && !result.reads_memory && !result.writes_memory,
                                    "legacy synchronize is an argument-free sequentially consistent system fence");
}

static void check_builtin_atomic_test_constraints(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    CheckValue arguments[3] = {
        check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 1, 0),
        check_test_integer(BUSTER_TYPE_INT, 1),
        check_test_integer(BUSTER_TYPE_INT, 0),
    };
    const CBuiltinId ids[] = { CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_STORE_N, CBUILTIN_SYNC_SYNCHRONIZE,
                               CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ATOMIC_SIGNAL_FENCE };
    const uint32_t arities[] = { 2, 3, 0, 1, 1 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
    {
        for (uint32_t count = 0; count < 5; count += 1)
        {
            if (count != arities[i])
            {
                const CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, ids[i], NULL, count);
                check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT,
                                                "wrong arity is rejected before any argument slice read");
            }
        }
    }
    CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, NULL, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "nonempty null argument slice is rejected");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cbuiltin_catalog); i += 1)
    {
        const CBuiltinId id = cbuiltin_catalog[i].id;
        const bool family = (id >= CBUILTIN_ATOMIC_LOAD && id <= CBUILTIN_ATOMIC_IS_LOCK_FREE) ||
                            (id >= CBUILTIN_SYNC_FETCH_AND_ADD && id <= CBUILTIN_SYNC_LOCK_RELEASE);
        if (family && id != CBUILTIN_ATOMIC_LOAD_N && id != CBUILTIN_ATOMIC_STORE_N && id != CBUILTIN_SYNC_SYNCHRONIZE &&
            id != CBUILTIN_ATOMIC_THREAD_FENCE && id != CBUILTIN_ATOMIC_SIGNAL_FENCE && id != CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE &&
            id != CBUILTIN_ATOMIC_IS_LOCK_FREE)
        {
            result = check_builtin_atomic_test_action(table, id, NULL, UINT32_MAX);
            check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED &&
                                            result.unsupported == BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID,
                                            "every other catalog atomic/sync identity is explicitly unsupported before reading its arguments");
        }
    }
    arguments[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_POINTER_REQUIRED, "integer zero is not a deduced atomic pointer");
    const TypeId invalid[] = { BUSTER_TYPE_LONG_DOUBLE, type_atomic(table, BUSTER_TYPE_INT), type_bit_int(table, 8, true), type_complex(table, BUSTER_TYPE_INT) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, invalid[i]), 2, 0);
        result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE,
                                        "GNU _n rejects direct x87 long double, _Atomic, _BitInt and complex integer object types");
    }
    arguments[0].type = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED,
                                    "const object load allowance never permits a const store");
    arguments[0].type = type_pointer(table, BUSTER_TYPE_INT);
    arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 3, 0);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_LOAD_N, arguments, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 1,
                                    "memory order uses ordinary int parameter constraints");
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 1,
                                    "stored pointer cannot silently become an integer value");
    const TypeId unrelated_to[] = { BUSTER_TYPE_INT, BUSTER_TYPE_LONG, type_pointer(table, BUSTER_TYPE_INT) };
    const TypeId unrelated_from[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_LLONG, type_pointer(table, BUSTER_TYPE_UINT) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unrelated_to); i += 1)
    {
        arguments[0].type = type_pointer(table, type_pointer(table, unrelated_to[i]));
        arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, unrelated_from[i]), 45, 0);
        result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_STORE_N, arguments, 3);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 1 &&
                                        (result.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN) == 0,
                                        "unrelated equal-width and nested pointer types never become warning-compatible signedness pairs");
    }

    arguments[0].type = type_pointer(table, BUSTER_TYPE_INT);
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    CheckBuiltinAtomicAction action = {
        .id = CBUILTIN_ATOMIC_LOAD_N,
        .arguments = arguments,
        .argument_count = 2,
        .source_position = UINT32_MAX,
    };
    check_builtin_atomic_one(table, &action, &result);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.alignment_known && result.minimum_alignment == 0 &&
                                    (result.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT) != 0,
                                    "unknown address alignment remains an explicit unsupported helper path without invented natural-alignment proof");
    action.alignment_known = true;
    action.minimum_alignment = 1;
    check_builtin_atomic_one(table, &action, &result);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.minimum_alignment == 1 && result.required_alignment == 4 &&
                                    (result.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT) != 0 &&
                                    (result.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT) != 0,
                                    "known packed alignment preserves valid Clang source and refuses the non-atomic helper access path");
    action.minimum_alignment = 3;
    check_builtin_atomic_one(table, &action, &result);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "invalid producer alignment metadata is diagnosed");
    action.minimum_alignment = 16;
    arguments[0].type = type_pointer(table, BUSTER_TYPE_UINT128);
    check_builtin_atomic_one(table, &action, &result);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.object_size == 16 &&
                                    (result.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH) != 0,
                                    "valid 128-bit GNU object remains typed but explicitly unsupported by the baseline helper");

    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148041);
    const bool complete = type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0);
    check_builtin_atomic_test_expect(test, complete, "source-time atomic enum fixture construction");
    if (complete)
    {
        uint32_t completions[512] = { 0 };
        const uint32_t tag = table->types.index[type_index(enumeration)];
        completions[tag] = 20;
        arguments[0].type = type_pointer(table, enumeration);
        action.tag_completions = completions;
        action.tag_completion_count = BUSTER_ARRAY_LENGTH(completions);
        action.source_position = 19;
        action.node = 999;
        check_builtin_atomic_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE,
                                        "later enum completion is hidden from an earlier atomic object access");
        action.source_position = 20;
        check_builtin_atomic_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.value_type == enumeration,
                                        "source completion positions remain distinct from diagnostic node IDs");
    }
}

static void check_builtin_atomic_test_pages(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    enum { MAXIMUM = 17 };
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_region = page != 0 ? ((MAXIMUM * sizeof(CheckBuiltinAtomicAction) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((MAXIMUM * sizeof(CheckBuiltinAtomicResult) + 16 + page - 1) / page) * page : 0;
    const size_t argument_region = page != 0 ? ((3 * MAXIMUM * sizeof(CheckValue) + 16 + page - 1) / page) * page : 0;
    const size_t size = input_region + output_region + argument_region + 3 * page;
    uint8_t* mapping = page != 0 ? mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* input_end = ready ? mapping + input_region : NULL;
    uint8_t* output_end = ready ? input_end + page + output_region : NULL;
    uint8_t* argument_end = ready ? output_end + page + argument_region : NULL;
    if (ready)
    {
        ready = mprotect(input_end, page, PROT_NONE) == 0 && mprotect(output_end, page, PROT_NONE) == 0 && mprotect(argument_end, page, PROT_NONE) == 0;
    }
    check_builtin_atomic_test_expect(test, ready, "guarded atomic action/result/argument slices allocated");
    const CBuiltinId ids[] = { CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_STORE_N, CBUILTIN_SYNC_SYNCHRONIZE,
                               CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ATOMIC_SIGNAL_FENCE };
    const uint32_t arities[] = { 2, 3, 0, 1, 1 };
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    for (uint32_t count = 0; ready && count <= MAXIMUM; count += 1)
    {
        uint32_t total_arguments = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            total_arguments += arities[i % BUSTER_ARRAY_LENGTH(ids)];
        }
        CheckBuiltinAtomicAction* actions = (CheckBuiltinAtomicAction*)(input_end - count * sizeof(*actions));
        CheckBuiltinAtomicResult* results = (CheckBuiltinAtomicResult*)(output_end - count * sizeof(*results));
        CheckValue* arguments = (CheckValue*)(argument_end - total_arguments * sizeof(*arguments));
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        memset((uint8_t*)arguments - 16, 0xa5, 16 + total_arguments * sizeof(*arguments));
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t kind = i % BUSTER_ARRAY_LENGTH(ids);
            for (uint32_t arg = 0; arg < arities[kind]; arg += 1)
            {
                arguments[cursor + arg] = kind <= 1 && arg == 0 ? check_builtin_atomic_test_pointer(pointer, i + 1, 0) :
                                             check_test_integer(BUSTER_TYPE_INT, (i + arg) % 6);
            }
            actions[i] = (CheckBuiltinAtomicAction){
                .id = ids[kind],
                .arguments = arguments + cursor,
                .argument_count = arities[kind],
                .node = i,
                .source_position = UINT32_MAX,
                .minimum_alignment = 4,
                .alignment_known = i % 2 == 0,
            };
            cursor += arities[kind];
        }
        CheckBuiltinAtomicAction input_copy[MAXIMUM];
        CheckValue argument_copy[3 * MAXIMUM];
        memcpy(input_copy, actions, count * sizeof(*actions));
        memcpy(argument_copy, arguments, total_arguments * sizeof(*arguments));
        CheckBuiltinAtomicResult expected[MAXIMUM];
        check_builtin_atomic(table, actions, expected, count);
        check_builtin_atomic(table, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_atomic_test_expect(test, check_builtin_atomic_test_equal(expected + i, results + i), "guarded atomic batch changed semantic output");
        }
        for (uint32_t partition = 1; partition <= 9; partition += 1)
        {
            for (uint32_t first = 0; first < count; first += partition)
            {
                const uint32_t remaining = count - first < partition ? count - first : partition;
                check_builtin_atomic(table, actions + first, results + first, remaining);
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_atomic_test_expect(test, check_builtin_atomic_test_equal(expected + i, results + i), "legal atomic batch partition changed semantic output");
            }
        }
        // By field: the actions' and the values' padding is no part of them.
        bool unchanged = true;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckBuiltinAtomicAction* x = input_copy + i;
            const CheckBuiltinAtomicAction* y = actions + i;
            unchanged &= x->id == y->id && x->arguments == y->arguments && x->argument_count == y->argument_count && x->node == y->node &&
                x->tag_completions == y->tag_completions && x->tag_completion_count == y->tag_completion_count && x->source_position == y->source_position &&
                x->extensions == y->extensions && x->minimum_alignment == y->minimum_alignment && x->alignment_known == y->alignment_known;
        }
        for (uint32_t i = 0; i < total_arguments; i += 1)
        {
            unchanged &= check_test_value_equal(argument_copy + i, arguments + i);
        }
        check_builtin_atomic_test_expect(test, unchanged, "atomic actions never mutate borrowed inputs");
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_atomic_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5 && *((uint8_t*)arguments - i) == 0xa5,
                                            "atomic batch wrote before a logical slice");
        }
    }
    if (ready)
    {
        CheckBuiltinAtomicResult result;
        const CheckBuiltinAtomicAction invalid = { .id = CBUILTIN_ATOMIC_STORE_N, .arguments = (const CheckValue*)argument_end, .argument_count = 6 };
        check_builtin_atomic_one(table, &invalid, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "bad atomic arity does not inspect protected argument storage");
    }
    check_builtin_atomic(NULL, NULL, NULL, 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, size);
    }
}

static bool check_builtin_atomic_test_warning_oracle(const char* clang, const char* path, const char* warning, bool* rejected)
{
    // Keep diagnostic policy exact: parse_test_clang supplies -w and cannot serve this oracle.
    char* arguments[] = { (char*)clang, "-fsyntax-only", "-std=gnu17", (char*)warning, (char*)path, NULL };
    OracleRun run = {
        .purpose = "checker atomic warning oracle",
        .arguments = arguments,
        .dependencies = true,
        .timeout = 10,
    };
    const int status = oracle_test_run(&run, 0, 0, 0, 0);
    const bool ready = status == 0 || status == 1;
    *rejected = ready && status == 1;
    return ready;
}

static void check_builtin_atomic_test_oracle(CheckBuiltinAtomicTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
        bool order_error;
    } cases[] =
    {
        { "int x; _Static_assert(_Generic(__atomic_load_n(&x,0),int:1,default:0),\"int result\");\n", false, false },
        { "const volatile unsigned long x=1; _Static_assert(_Generic(__atomic_load_n(&x,0),unsigned long:1,default:0),\"unqualified result\");\n", false, false },
        { "_Bool b; _Static_assert(_Generic(__atomic_load_n(&b,0),_Bool:1,default:0),\"bool result\");\n", false, false },
        { "enum E { A }; enum E f(enum E*p) { return __atomic_load_n(p,0); } void g(enum E*p) { __atomic_store_n(p,257,0); }\n", false, false },
        { "int *p; _Static_assert(_Generic(__atomic_load_n(&p,0),int*:1,default:0),\"pointer result\");\n", false, false },
        { "int *restrict p; int *f(void) { return __atomic_load_n(&p,0); } void g(int*q) { __atomic_store_n(&p,q,0); }\n", false, false },
        { "int *restrict const volatile p=0; int *f(void) { return __atomic_load_n(&p,0); }\n", false, false },
        { "void f(_Bool*p,int*q) { __atomic_store_n(p,q,0); }\n", false, false },
        { "void f(int**p,const int*q) { __atomic_store_n(p,q,0); }\n", false, false },
        { "int f(int*p,int m) { return __atomic_load_n(p++,m++); } void g(int*p,int v,int m) { __atomic_store_n(p++,v++,m++); }\n", false, false },
        { "int f(int*p) { return __atomic_load_n(p,3.75); }\n", false, false },
        { "int f(int*p) { return __atomic_load_n(p,3); }\n", false, true },
        { "int f(int*p) { return __atomic_load_n(p,4); }\n", false, true },
        { "int f(int*p) { return __atomic_load_n(p,6); }\n", false, true },
        { "int f(int*p) { return __atomic_load_n(p,4294967299ULL); }\n", false, true },
        { "void f(int*p) { __atomic_store_n(p,1,2); }\n", false, true },
        { "void f(int*p) { __atomic_store_n(p,1,3); }\n", false, false },
        { "void f(int m) { __atomic_thread_fence(m++); __atomic_signal_fence(m++); __sync_synchronize(); }\n", false, false },
        { "void f(void) { __atomic_thread_fence(6); __atomic_signal_fence(-1); }\n", false, false },
        { "void f(void) { __atomic_thread_fence(3.75); __atomic_signal_fence(2.5); }\n", false, false },
        { "struct __attribute__((packed)) S { char c; int x; }; int f(struct S*p) { return __atomic_load_n(&p->x,0); }\n", false, false },
        { "unsigned __int128 f(unsigned __int128*p) { return __atomic_load_n(p,0); }\n", false, false },
        { "int x; _Static_assert(__atomic_load_n(&x,0)==0,\"never ICE\");\n", true, false },
        { "int x; int y=__atomic_load_n(&x,0);\n", true, false },
        { "int f(int*p) { return __atomic_load_n(p); }\n", true, false },
        { "int f(int*p) { return __atomic_load_n(p,0,1); }\n", true, false },
        { "void f(int*p) { __atomic_store_n(p,1); }\n", true, false },
        { "void f(int*p) { __atomic_store_n(p,1,0,1); }\n", true, false },
        { "void f(void) { __sync_synchronize(1); }\n", true, false },
        { "void f(void) { __atomic_thread_fence(); }\n", true, false },
        { "void f(void) { __atomic_signal_fence(0,0); }\n", true, false },
        { "int f(void) { return __atomic_load_n(0,0); }\n", true, false },
        { "float f(const volatile float*p) { return __atomic_load_n(p,0); } void g(volatile float*p,double v) { __atomic_store_n(p,v,0); }\n", false, false },
        { "double f(const volatile double*p) { return __atomic_load_n(p,0); } void g(volatile double*p,int v) { __atomic_store_n(p,v,0); }\n", false, false },
        { "void f(_Float16*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "void f(__fp16*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "void f(__bf16*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "void f(__float128*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "void f(float _Complex*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "void f(double _Complex*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "void f(long double _Complex*p) { (void)__atomic_load_n(p,0); __atomic_store_n(p,1,0); }\n", false, false },
        { "long double f(long double*p) { return __atomic_load_n(p,0); }\n", true, false },
        { "void f(long double*p) { __atomic_store_n(p,1,0); }\n", true, false },
        { "void f(int _Complex*p) { (void)__atomic_load_n(p,0); }\n", true, false },
        { "struct S { int x; }; struct S f(struct S*p) { return __atomic_load_n(p,0); }\n", true, false },
        { "int f(_Atomic(int)*p) { return __atomic_load_n(p,0); }\n", true, false },
        { "unsigned _BitInt(8) f(unsigned _BitInt(8)*p) { return __atomic_load_n(p,0); }\n", true, false },
        { "void f(const int*p) { __atomic_store_n(p,1,0); }\n", true, false },
        { "int f(int*p,int*q) { return __atomic_load_n(p,q); }\n", true, false },
        { "void f(int*p,int*q) { __atomic_store_n(p,q,0); }\n", true, false },
        { "void f(int*p) { __atomic_thread_fence(p); }\n", true, false },
    };
    char directory[] = "/tmp/buster-check-builtin-atomic-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(cases)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(cases)];
    ParseTestClang results[BUSTER_ARRAY_LENGTH(cases)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], cases[i].source, strlen(cases[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(cases), results);
    check_builtin_atomic_test_expect(test, ready, "independent Clang atomic syntax oracle ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        check_builtin_atomic_test_expect(test, results[i].rejected == cases[i].rejected, "Clang disagrees with original atomic type/arity/constant fixture");
    }
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        bool rejected = false;
        ready = check_builtin_atomic_test_warning_oracle(clang, paths[i], "-Werror=atomic-memory-ordering", &rejected);
        check_builtin_atomic_test_expect(test, ready, "independent Clang atomic order-warning oracle ran");
        check_builtin_atomic_test_expect(test, !ready || rejected == (cases[i].rejected || cases[i].order_error),
                                        "Clang disagrees with invalid-order warning classification");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_builtin_atomic_test_pointer_oracle(CheckBuiltinAtomicTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
        bool sign_warning;
    } cases[] =
    {
        { "void f(char**p,unsigned char*q,unsigned char**r,signed char*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "void f(short**p,unsigned short*q,unsigned short**r,short*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "void f(int**p,unsigned int*q,unsigned int**r,int*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "void f(long**p,unsigned long*q,unsigned long**r,long*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "void f(long long**p,unsigned long long*q,unsigned long long**r,long long*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "void f(__int128**p,unsigned __int128*q,unsigned __int128**r,__int128*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "void f(int**p,const unsigned int*q) { __atomic_store_n(p,q,0); }\n", false, true },
        { "void f(int**p,const int*q) { __atomic_store_n(p,q,0); }\n", false, false },
        { "void f(int**p,float*q) { __atomic_store_n(p,q,0); }\n", true, false },
        { "void f(long**p,long long*q) { __atomic_store_n(p,q,0); }\n", true, false },
        { "void f(int***p,unsigned int**q) { __atomic_store_n(p,q,0); }\n", true, false },
        { "enum E:int { A }; void f(enum E**p,int*q,int**r,enum E*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, false },
        { "enum E:int { A }; void f(enum E**p,unsigned int*q,unsigned int**r,enum E*s) { __atomic_store_n(p,q,0); __atomic_store_n(r,s,0); }\n", false, true },
        { "enum E:unsigned int { A }; void f(enum E**p,int*q) { __atomic_store_n(p,q,0); }\n", true, false },
        { "enum E:unsigned int { A }; void f(enum E**p,unsigned int*q) { __atomic_store_n(p,q,0); }\n", false, false },
        { "enum E:int { A }; enum F:int { B }; void f(enum E**p,enum F*q) { __atomic_store_n(p,q,0); }\n", false, true },
        { "enum E:unsigned int { A }; enum F:unsigned int { B }; void f(enum E**p,enum F*q) { __atomic_store_n(p,q,0); }\n", true, false },
        { "enum E; void f(enum E**p,int*q) { __atomic_store_n(p,q,0); } enum E { A=-1 };\n", true, false },
        { "enum E; void f(enum E**p,unsigned int*q) { __atomic_store_n(p,q,0); } enum E { A=-1 };\n", true, false },
        { "enum E; void f(enum E**p,enum E*q) { __atomic_store_n(p,q,0); } enum E { A=-1 };\n", false, false },
        { "enum E; void f(enum E**p,void*q) { __atomic_store_n(p,q,0); } enum E { A=-1 };\n", false, false },
        { "enum E; void f(enum E***p,int**q) { __atomic_store_n(p,q,0); } enum E { A=-1 };\n", true, false },
        { "enum E { A=-1 }; void f(enum E***p,int**q) { __atomic_store_n(p,q,0); }\n", false, false },
        { "enum E; typedef int F(enum E); typedef int G(int); void f(F**p,G*q) { __atomic_store_n(p,q,0); } enum E { A=-1 };\n", true, false },
        { "enum E { A=-1 }; typedef int F(enum E); typedef int G(int); void f(F**p,G*q) { __atomic_store_n(p,q,0); }\n", false, false },
    };
    char directory[] = "/tmp/buster-check-builtin-atomic-pointer-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(cases)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(cases)];
    ParseTestClang results[BUSTER_ARRAY_LENGTH(cases)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], cases[i].source, strlen(cases[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(cases), results);
    check_builtin_atomic_test_expect(test, ready, "independent Clang atomic pointer conversion oracle ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        check_builtin_atomic_test_expect(test, results[i].rejected == cases[i].rejected,
                                        "Clang disagrees with corresponding-signedness versus unrelated-pointer store fixture");
        bool rejected = false;
        ready = check_builtin_atomic_test_warning_oracle(clang, paths[i], "-Werror=pointer-sign", &rejected);
        check_builtin_atomic_test_expect(test, ready, "independent Clang atomic pointer-sign warning oracle ran");
        check_builtin_atomic_test_expect(test, !ready || rejected == (cases[i].rejected || cases[i].sign_warning),
                                        "Clang disagrees with atomic pointer-sign diagnostic classification");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_builtin_atomic_test_c11(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    const TypeId values[] = { BUSTER_TYPE_BOOL, BUSTER_TYPE_CHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_INT, BUSTER_TYPE_ULONG, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_INT128 };
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(values); t += 1)
    {
        const TypeId atomic = type_atomic(table, values[t]);
        const TypeId pointer = type_pointer(table, atomic);
        CheckValue arguments[5] = {
            check_builtin_atomic_test_pointer(pointer, t + 1, 0),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 0),
        };
        CheckBuiltinAtomicAction action = {
            .id = CBUILTIN_C11_ATOMIC_LOAD,
            .arguments = arguments,
            .argument_count = 2,
            .alignment_known = true,
            .minimum_alignment = type_alignment(table, atomic),
        };
        CheckBuiltinAtomicResult result;
        check_builtin_atomic_c11_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == values[t] &&
                                        result.object_type == atomic && result.object_size == type_size(table, atomic) && result.reads_memory && !result.writes_memory &&
                                        result.evaluated_operands == 3 && result.runtime_builtin == CBUILTIN_ATOMIC_LOAD_N,
                                        "C11 load retains its atomic storage and unqualified value types");
        action.id = CBUILTIN_C11_ATOMIC_STORE;
        action.argument_count = 3;
        check_builtin_atomic_c11_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
                                        result.argument_conversions[1].type == values[t] && result.writes_memory && !result.reads_memory && result.evaluated_operands == 7,
                                        "C11 store converts its value without discarding atomic storage metadata");
        arguments[0].type = type_pointer(table, values[t]);
        check_builtin_atomic_c11_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE,
                                        "C11 operations reject pointers to ordinary objects");
        arguments[0].type = type_pointer(table, type_qualify(table, atomic, BUSTER_TYPE_CONST));
        check_builtin_atomic_c11_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED,
                                        "C11 writes reject const atomic objects");
        arguments[0].type = pointer;
        arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, values[t]), t + 20, 0);
        action.id = CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG;
        action.argument_count = 5;
        check_builtin_atomic_c11_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_BOOL &&
                                        result.argument_conversions[1].type == arguments[1].type && result.argument_conversions[2].type == values[t] &&
                                        result.evaluated_operands == 31 && result.rmw && result.runtime_builtin == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N,
                                        "C11 compare-exchange keeps five evaluated source operands and the plain expected pointer");
        action.argument_count = 4;
        check_builtin_atomic_c11_one(table, &action, &result);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "C11 compare-exchange enforces source arity before reading operands");
    }
}

// A family's call through the shared dispatch (check_builtin_atomic_family), its object aligned to `alignment`.
static CheckBuiltinAtomicResult check_builtin_atomic_test_family(TypeTable* table, CBuiltinId id, const CheckValue* arguments, uint32_t count, uint64_t alignment)
{
    const CheckBuiltinAtomicAction action = {
        .id = id,
        .arguments = arguments,
        .argument_count = count,
        .node = 148,
        .source_position = UINT32_MAX,
        .minimum_alignment = alignment,
        .alignment_known = true,
    };
    CheckBuiltinAtomicResult result;
    check_builtin_atomic_family(table, &action, &result);
    return result;
}

// #148's families past the _n forms: the lock-free queries' constants, GNU's generic forms and the scoped families.
static void check_builtin_atomic_test_families(CheckBuiltinAtomicTest* test, TypeTable* table)
{
    // The queries: a size that is a power of two up to the inline width is lock-free for one byte, a constant address
    // aligned to it or a pointee aligned to it; __atomic_always_lock_free answers 0 where __atomic_is_lock_free asks
    // libatomic, and neither is a constant of a size that isn't one.
    const uint64_t sizes[] = { 0, 1, 2, 3, 4, 8, 16, 32 };
    const TypeId pointees[] = { BUSTER_TYPE_VOID, BUSTER_TYPE_CHAR, BUSTER_TYPE_INT, BUSTER_TYPE_LONG };
    for (uint32_t wide = 0; wide < 2; wide += 1)
    {
        table->atomic_inline_16 = wide != 0;
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
        {
            for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(pointees) + 2; p += 1)
            {
                // A typed pointer, the null pointer constant 0 and the constant address 8.
                const bool null = p == BUSTER_ARRAY_LENGTH(pointees);
                const bool address = p == BUSTER_ARRAY_LENGTH(pointees) + 1;
                CheckValue arguments[2] = {
                    check_test_integer(BUSTER_TYPE_ULONG, sizes[s]),
                    null ? check_test_integer(BUSTER_TYPE_INT, 0) :
                    address ? check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_VOID), 0, 8) :
                              check_builtin_atomic_test_pointer(type_pointer(table, pointees[p]), 21, 0),
                };
                arguments[0].flags |= BUSTER_CHECK_ICE;
                const uint64_t size = sizes[s];
                const bool fits = (size & (size - 1)) == 0 && size <= (wide != 0 ? 16u : 8u);
                const bool typed = !null && !address && p != 0 && type_alignment(table, pointees[p]) >= size;
                const bool aligned = null ? true : address ? size != 0 && 8 % size == 0 : false;
                const bool lock_free = fits && (size == 1 || typed || aligned);
                for (uint32_t always = 0; always < 2; always += 1)
                {
                    const CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, always != 0 ? CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE : CBUILTIN_ATOMIC_IS_LOCK_FREE, arguments, 2);
                    const bool constant = always != 0 || lock_free;
                    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_BOOL &&
                                                    (result.action.value.constant_kind == CHECK_CONSTANT_INTEGER) == constant &&
                                                    (!constant || (result.action.value.integer == lock_free && (result.action.value.flags & BUSTER_CHECK_ICE) != 0 &&
                                                                   result.evaluated_operands == 0)) &&
                                                    (constant || result.evaluated_operands == 3),
                                                    "a lock-free query's constant is clang's evaluator's, its arguments then never evaluated");
                }
            }
        }
    }
    table->atomic_inline_16 = false;
    CheckValue sized[2] = { (CheckValue){ .type = BUSTER_TYPE_ULONG, .category = CHECK_RVALUE }, check_test_integer(BUSTER_TYPE_INT, 0) };
    CheckBuiltinAtomicResult result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_ALWAYS_LOCK_FREE, sized, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.evaluated_operands == 3,
                                    "a query of a size that isn't a constant is none");
    sized[1] = check_test_integer(BUSTER_TYPE_INT, 4);
    sized[1].constant_kind = CHECK_CONSTANT_INTEGER;
    sized[1].integer = 4;
    sized[0] = check_test_integer(BUSTER_TYPE_ULONG, 4);
    result = check_builtin_atomic_test_action(table, CBUILTIN_ATOMIC_IS_LOCK_FREE, sized, 2);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a query's pointer isn't an integer but 0");
    // GNU's generic forms: their value pointers' conversions, any complete object but an _Atomic or a _BitInt one.
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 152);
    const TypeId objects[] = { BUSTER_TYPE_INT, BUSTER_TYPE_LONG_DOUBLE, type_array(table, BUSTER_TYPE_CHAR, 3), type_atomic(table, BUSTER_TYPE_INT),
                               type_bit_int(table, 32, false), BUSTER_TYPE_VOID, record };
    const CBuiltinId generic[] = { CBUILTIN_ATOMIC_LOAD, CBUILTIN_ATOMIC_STORE, CBUILTIN_ATOMIC_EXCHANGE, CBUILTIN_ATOMIC_COMPARE_EXCHANGE };
    const uint32_t counts[] = { 3, 3, 4, 6 };
    for (uint32_t g = 0; g < BUSTER_ARRAY_LENGTH(generic); g += 1)
    {
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(objects); o += 1)
        {
            // An array's qualifiers are its elements', so it is accessed as it is.
            const TypeId object = o == 2 ? objects[o] : type_qualify(table, objects[o], BUSTER_TYPE_VOLATILE);
            const TypeId value = type_conversion_operand(table, objects[o]);
            CheckValue arguments[6] = {
                check_builtin_atomic_test_pointer(type_pointer(table, object), 21, 0),
                check_builtin_atomic_test_pointer(type_pointer(table, value), 22, 0),
                check_builtin_atomic_test_pointer(type_pointer(table, value), 23, 0),
                check_test_integer(BUSTER_TYPE_INT, 5),
                check_test_integer(BUSTER_TYPE_INT, 5),
                check_test_integer(BUSTER_TYPE_INT, 2),
            };
            // A load's and a store's order is their third argument, an exchange's its fourth.
            if (g == 0 || g == 1)
            {
                arguments[2] = check_test_integer(BUSTER_TYPE_INT, 5);
            }
            if (g == 2)
            {
                arguments[3] = check_test_integer(BUSTER_TYPE_INT, 5);
            }
            result = check_builtin_atomic_test_family(table, generic[g], arguments, counts[g], 16);
            const bool valid = o < 3;
            const CheckDiagnostic expected = o < 3 ? CHECK_DIAG_NONE : o < 5 ? CHECK_DIAG_INCOMPATIBLE : CHECK_DIAG_INCOMPLETE;
            check_builtin_atomic_test_expect(test, result.action.diagnostic == expected, "a generic form's object is any complete one but an _Atomic or a _BitInt");
            if (valid)
            {
                // A store's value and a compare-exchange's desired one keep the first argument's qualifiers.
                const TypeId kept = type_pointer(table, object);
                const TypeId plain = type_pointer(table, value);
                check_builtin_atomic_test_expect(test, result.action.value.type == (g == 3 ? BUSTER_TYPE_BOOL : BUSTER_TYPE_VOID) && result.value_type == value &&
                                                result.object_size == type_size(table, objects[o]) && result.runtime_builtin == generic[g] &&
                                                result.evaluated_operands == (1u << counts[g]) - 1 && result.volatile_access == (o != 2) && result.unsupported == 0 &&
                                                result.argument_conversions[1].type == (g == 1 ? kept : plain) &&
                                                (g == 0 || g == 1 || result.argument_conversions[2].type == (g == 2 || g == 3 ? kept : plain)) &&
                                                (g != 3 || result.argument_conversions[3].type == BUSTER_TYPE_BOOL),
                                                "a generic form's value pointers convert to its object's types");
            }
            result = check_builtin_atomic_test_family(table, generic[g], arguments, counts[g] - 1, 16);
            check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a generic form takes its arguments exactly");
        }
        CheckValue constant[6] = {
            check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST)), 21, 0),
            check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 22, 0),
            g < 2 ? check_test_integer(BUSTER_TYPE_INT, 5) : check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 23, 0),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 5),
        };
        result = check_builtin_atomic_test_family(table, generic[g], constant, counts[g], 16);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == (g == 0 ? CHECK_DIAG_NONE : CHECK_DIAG_MODIFIABLE_REQUIRED),
                                        "a generic form writes no const object, which a load reads");
    }
    // The scoped families: their bases' operations with a scope last, checked against its model where it is an integer
    // constant expression, and evaluated.
    const struct
    {
        CBuiltinId id;
        CBuiltinId runtime;
        TypeId object;
        uint32_t count;
        uint32_t first_valid;
        uint32_t last_valid;
    } scoped[] = {
        { CBUILTIN_SCOPED_ATOMIC_FETCH_ADD, CBUILTIN_ATOMIC_FETCH_ADD, BUSTER_TYPE_INT, 4, 0, 5 },
        { CBUILTIN_SCOPED_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_LOAD_N, BUSTER_TYPE_INT, 3, 0, 5 },
        { CBUILTIN_HIP_ATOMIC_LOAD, CBUILTIN_ATOMIC_LOAD_N, BUSTER_TYPE_INT, 3, 1, 6 },
        { CBUILTIN_HIP_ATOMIC_FETCH_ADD, CBUILTIN_ATOMIC_FETCH_ADD, BUSTER_TYPE_INT, 4, 1, 6 },
        { CBUILTIN_OPENCL_ATOMIC_LOAD, CBUILTIN_ATOMIC_LOAD_N, type_atomic(table, BUSTER_TYPE_INT), 3, 1, 4 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(scoped); i += 1)
    {
        for (uint32_t model = 0; model < 8; model += 1)
        {
            for (uint32_t ice = 0; ice < 2; ice += 1)
            {
                CheckValue arguments[4] = {
                    check_builtin_atomic_test_pointer(type_pointer(table, scoped[i].object), 21, 0),
                    check_test_integer(BUSTER_TYPE_INT, 5),
                    check_test_integer(BUSTER_TYPE_INT, 5),
                    check_test_integer(BUSTER_TYPE_INT, 5),
                };
                arguments[scoped[i].count - 1] = check_test_integer(BUSTER_TYPE_INT, model);
                arguments[scoped[i].count - 1].flags = ice != 0 ? BUSTER_CHECK_ICE : 0;
                result = check_builtin_atomic_test_family(table, scoped[i].id, arguments, scoped[i].count, 16);
                const bool valid = ice == 0 || (model >= scoped[i].first_valid && model <= scoped[i].last_valid);
                check_builtin_atomic_test_expect(test, valid ? result.action.diagnostic == CHECK_DIAG_NONE && result.runtime_builtin == scoped[i].runtime &&
                                                                   result.evaluated_operands == (1u << scoped[i].count) - 1 &&
                                                                   result.argument_conversions[scoped[i].count - 1].type == BUSTER_TYPE_INT :
                                                               result.action.diagnostic == CHECK_DIAG_INVALID_TYPE && result.action.diagnostic_operand == scoped[i].count - 1,
                                                "a scope out of its model is clang's error where it is an integer constant expression");
            }
        }
        CheckValue arguments[4] = {
            check_builtin_atomic_test_pointer(type_pointer(table, scoped[i].object), 21, 0),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 5),
            check_test_integer(BUSTER_TYPE_INT, 5),
        };
        result = check_builtin_atomic_test_family(table, scoped[i].id, arguments, scoped[i].count - 1, 16);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a scoped builtin needs its scope");
        result = check_builtin_atomic_test_family(table, scoped[i].id, arguments, 0, 16);
        check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a scoped builtin of no arguments");
    }
    // HIP's object isn't _Atomic and OpenCL's is; __scoped_atomic_thread_fence's scope is any int; __opencl_atomic_init has none.
    CheckValue arguments[4] = {
        check_builtin_atomic_test_pointer(type_pointer(table, type_atomic(table, BUSTER_TYPE_INT)), 21, 0),
        check_test_integer(BUSTER_TYPE_INT, 5),
        check_test_integer(BUSTER_TYPE_INT, 1),
        check_test_integer(BUSTER_TYPE_INT, 1),
    };
    arguments[2].flags = BUSTER_CHECK_ICE;
    result = check_builtin_atomic_test_family(table, CBUILTIN_HIP_ATOMIC_LOAD, arguments, 3, 16);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "HIP's object isn't _Atomic");
    arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 21, 0);
    result = check_builtin_atomic_test_family(table, CBUILTIN_OPENCL_ATOMIC_LOAD, arguments, 3, 16);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "OpenCL's object is _Atomic");
    arguments[0] = check_test_integer(BUSTER_TYPE_INT, 5);
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 9);
    arguments[1].flags = BUSTER_CHECK_ICE;
    result = check_builtin_atomic_test_family(table, CBUILTIN_SCOPED_ATOMIC_THREAD_FENCE, arguments, 2, 16);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 3 && result.runtime_builtin == CBUILTIN_ATOMIC_THREAD_FENCE,
                                    "__scoped_atomic_thread_fence's scope is an ordinary int");
    arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, type_atomic(table, BUSTER_TYPE_INT)), 21, 0);
    result = check_builtin_atomic_test_family(table, CBUILTIN_OPENCL_ATOMIC_INIT, arguments, 2, 16);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 3, "__opencl_atomic_init has no scope");
    result = check_builtin_atomic_test_family(table, CBUILTIN_OPENCL_ATOMIC_INIT, arguments, 3, 16);
    check_builtin_atomic_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "__opencl_atomic_init takes two arguments");
}

static __attribute__((unused)) int run_check_builtin_atomic_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinAtomicTest test = { 0 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_atomic_test_expect(&test, options, "usage: test-check-builtins-atomic [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_atomic_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_atomic_test_values(&test, &types.table);
            check_builtin_atomic_test_c11(&test, &types.table);
            check_builtin_atomic_test_families(&test, &types.table);
            check_builtin_atomic_test_enum_pointers(&test, &types.table);
            check_builtin_atomic_test_orders(&test, &types.table);
            check_builtin_atomic_test_constraints(&test, &types.table);
            check_builtin_atomic_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_atomic_test_oracle(&test, clang);
        check_builtin_atomic_test_pointer_oracle(&test, clang);
    }
    printf("test-check-builtins-atomic: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    oracle_cache_report(oracle_cache(), "test-check-builtins-atomic");
    oracle_epoch_end(epoch);
    return test.failures != 0;
}
